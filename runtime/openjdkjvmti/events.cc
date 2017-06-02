/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "events-inl.h"

#include "art_jvmti.h"
#include "base/logging.h"
#include "gc/allocation_listener.h"
#include "gc/gc_pause_listener.h"
#include "gc/heap.h"
#include "handle_scope-inl.h"
#include "instrumentation.h"
#include "jni_env_ext-inl.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace openjdkjvmti {

bool EventMasks::IsEnabledAnywhere(ArtJvmtiEvent event) {
  return global_event_mask.Test(event) || unioned_thread_event_mask.Test(event);
}

EventMask& EventMasks::GetEventMask(art::Thread* thread) {
  if (thread == nullptr) {
    return global_event_mask;
  }

  for (auto& pair : thread_event_masks) {
    const UniqueThread& unique_thread = pair.first;
    if (unique_thread.first == thread &&
        unique_thread.second == static_cast<uint32_t>(thread->GetTid())) {
      return pair.second;
    }
  }

  // TODO: Remove old UniqueThread with the same pointer, if exists.

  thread_event_masks.emplace_back(UniqueThread(thread, thread->GetTid()), EventMask());
  return thread_event_masks.back().second;
}

EventMask* EventMasks::GetEventMaskOrNull(art::Thread* thread) {
  if (thread == nullptr) {
    return &global_event_mask;
  }

  for (auto& pair : thread_event_masks) {
    const UniqueThread& unique_thread = pair.first;
    if (unique_thread.first == thread &&
        unique_thread.second == static_cast<uint32_t>(thread->GetTid())) {
      return &pair.second;
    }
  }

  return nullptr;
}


void EventMasks::EnableEvent(art::Thread* thread, ArtJvmtiEvent event) {
  DCHECK(EventMask::EventIsInRange(event));
  GetEventMask(thread).Set(event);
  if (thread != nullptr) {
    unioned_thread_event_mask.Set(event, true);
  }
}

void EventMasks::DisableEvent(art::Thread* thread, ArtJvmtiEvent event) {
  DCHECK(EventMask::EventIsInRange(event));
  GetEventMask(thread).Set(event, false);
  if (thread != nullptr) {
    // Regenerate union for the event.
    bool union_value = false;
    for (auto& pair : thread_event_masks) {
      union_value |= pair.second.Test(event);
      if (union_value) {
        break;
      }
    }
    unioned_thread_event_mask.Set(event, union_value);
  }
}

void EventMasks::HandleChangedCapabilities(const jvmtiCapabilities& caps, bool caps_added) {
  if (UNLIKELY(caps.can_retransform_classes == 1)) {
    // If we are giving this env the retransform classes cap we need to switch all events of
    // NonTransformable to Transformable and vice versa.
    ArtJvmtiEvent to_remove = caps_added ? ArtJvmtiEvent::kClassFileLoadHookNonRetransformable
                                         : ArtJvmtiEvent::kClassFileLoadHookRetransformable;
    ArtJvmtiEvent to_add = caps_added ? ArtJvmtiEvent::kClassFileLoadHookRetransformable
                                      : ArtJvmtiEvent::kClassFileLoadHookNonRetransformable;
    if (global_event_mask.Test(to_remove)) {
      CHECK(!global_event_mask.Test(to_add));
      global_event_mask.Set(to_remove, false);
      global_event_mask.Set(to_add, true);
    }

    if (unioned_thread_event_mask.Test(to_remove)) {
      CHECK(!unioned_thread_event_mask.Test(to_add));
      unioned_thread_event_mask.Set(to_remove, false);
      unioned_thread_event_mask.Set(to_add, true);
    }
    for (auto thread_mask : thread_event_masks) {
      if (thread_mask.second.Test(to_remove)) {
        CHECK(!thread_mask.second.Test(to_add));
        thread_mask.second.Set(to_remove, false);
        thread_mask.second.Set(to_add, true);
      }
    }
  }
}

void EventHandler::RegisterArtJvmTiEnv(ArtJvmTiEnv* env) {
  // Since we never shrink this array we might as well try to fill gaps.
  auto it = std::find(envs.begin(), envs.end(), nullptr);
  if (it != envs.end()) {
    *it = env;
  } else {
    envs.push_back(env);
  }
}

void EventHandler::RemoveArtJvmTiEnv(ArtJvmTiEnv* env) {
  // Since we might be currently iterating over the envs list we cannot actually erase elements.
  // Instead we will simply replace them with 'nullptr' and skip them manually.
  auto it = std::find(envs.begin(), envs.end(), env);
  if (it != envs.end()) {
    *it = nullptr;
    for (size_t i = static_cast<size_t>(ArtJvmtiEvent::kMinEventTypeVal);
         i <= static_cast<size_t>(ArtJvmtiEvent::kMaxEventTypeVal);
         ++i) {
      RecalculateGlobalEventMask(static_cast<ArtJvmtiEvent>(i));
    }
  }
}

static bool IsThreadControllable(ArtJvmtiEvent event) {
  switch (event) {
    case ArtJvmtiEvent::kVmInit:
    case ArtJvmtiEvent::kVmStart:
    case ArtJvmtiEvent::kVmDeath:
    case ArtJvmtiEvent::kThreadStart:
    case ArtJvmtiEvent::kCompiledMethodLoad:
    case ArtJvmtiEvent::kCompiledMethodUnload:
    case ArtJvmtiEvent::kDynamicCodeGenerated:
    case ArtJvmtiEvent::kDataDumpRequest:
      return false;

    default:
      return true;
  }
}

class JvmtiAllocationListener : public art::gc::AllocationListener {
 public:
  explicit JvmtiAllocationListener(EventHandler* handler) : handler_(handler) {}

  void ObjectAllocated(art::Thread* self, art::ObjPtr<art::mirror::Object>* obj, size_t byte_count)
      OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK_EQ(self, art::Thread::Current());

    if (handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kVmObjectAlloc)) {
      art::StackHandleScope<1> hs(self);
      auto h = hs.NewHandleWrapper(obj);
      // jvmtiEventVMObjectAlloc parameters:
      //      jvmtiEnv *jvmti_env,
      //      JNIEnv* jni_env,
      //      jthread thread,
      //      jobject object,
      //      jclass object_klass,
      //      jlong size
      art::JNIEnvExt* jni_env = self->GetJniEnv();

      jthread thread_peer;
      if (self->IsStillStarting()) {
        thread_peer = nullptr;
      } else {
        thread_peer = jni_env->AddLocalReference<jthread>(self->GetPeer());
      }

      ScopedLocalRef<jthread> thread(jni_env, thread_peer);
      ScopedLocalRef<jobject> object(
          jni_env, jni_env->AddLocalReference<jobject>(*obj));
      ScopedLocalRef<jclass> klass(
          jni_env, jni_env->AddLocalReference<jclass>(obj->Ptr()->GetClass()));

      handler_->DispatchEvent<ArtJvmtiEvent::kVmObjectAlloc>(self,
                                                             reinterpret_cast<JNIEnv*>(jni_env),
                                                             thread.get(),
                                                             object.get(),
                                                             klass.get(),
                                                             static_cast<jlong>(byte_count));
    }
  }

 private:
  EventHandler* handler_;
};

static void SetupObjectAllocationTracking(art::gc::AllocationListener* listener, bool enable) {
  // We must not hold the mutator lock here, but if we're in FastJNI, for example, we might. For
  // now, do a workaround: (possibly) acquire and release.
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ScopedThreadSuspension sts(soa.Self(), art::ThreadState::kSuspended);
  if (enable) {
    art::Runtime::Current()->GetHeap()->SetAllocationListener(listener);
  } else {
    art::Runtime::Current()->GetHeap()->RemoveAllocationListener();
  }
}

// Report GC pauses (see spec) as GARBAGE_COLLECTION_START and GARBAGE_COLLECTION_END.
class JvmtiGcPauseListener : public art::gc::GcPauseListener {
 public:
  explicit JvmtiGcPauseListener(EventHandler* handler)
      : handler_(handler),
        start_enabled_(false),
        finish_enabled_(false) {}

  void StartPause() OVERRIDE {
    handler_->DispatchEvent<ArtJvmtiEvent::kGarbageCollectionStart>(nullptr);
  }

  void EndPause() OVERRIDE {
    handler_->DispatchEvent<ArtJvmtiEvent::kGarbageCollectionFinish>(nullptr);
  }

  bool IsEnabled() {
    return start_enabled_ || finish_enabled_;
  }

  void SetStartEnabled(bool e) {
    start_enabled_ = e;
  }

  void SetFinishEnabled(bool e) {
    finish_enabled_ = e;
  }

 private:
  EventHandler* handler_;
  bool start_enabled_;
  bool finish_enabled_;
};

static void SetupGcPauseTracking(JvmtiGcPauseListener* listener, ArtJvmtiEvent event, bool enable) {
  bool old_state = listener->IsEnabled();

  if (event == ArtJvmtiEvent::kGarbageCollectionStart) {
    listener->SetStartEnabled(enable);
  } else {
    listener->SetFinishEnabled(enable);
  }

  bool new_state = listener->IsEnabled();

  if (old_state != new_state) {
    if (new_state) {
      art::Runtime::Current()->GetHeap()->SetGcPauseListener(listener);
    } else {
      art::Runtime::Current()->GetHeap()->RemoveGcPauseListener();
    }
  }
}

// Handle special work for the given event type, if necessary.
void EventHandler::HandleEventType(ArtJvmtiEvent event, bool enable) {
  switch (event) {
    case ArtJvmtiEvent::kVmObjectAlloc:
      SetupObjectAllocationTracking(alloc_listener_.get(), enable);
      return;

    case ArtJvmtiEvent::kGarbageCollectionStart:
    case ArtJvmtiEvent::kGarbageCollectionFinish:
      SetupGcPauseTracking(gc_pause_listener_.get(), event, enable);
      return;

    default:
      break;
  }
}

// Checks to see if the env has the capabilities associated with the given event.
static bool HasAssociatedCapability(ArtJvmTiEnv* env,
                                    ArtJvmtiEvent event) {
  jvmtiCapabilities caps = env->capabilities;
  switch (event) {
    case ArtJvmtiEvent::kBreakpoint:
      return caps.can_generate_breakpoint_events == 1;

    case ArtJvmtiEvent::kCompiledMethodLoad:
    case ArtJvmtiEvent::kCompiledMethodUnload:
      return caps.can_generate_compiled_method_load_events == 1;

    case ArtJvmtiEvent::kException:
    case ArtJvmtiEvent::kExceptionCatch:
      return caps.can_generate_exception_events == 1;

    case ArtJvmtiEvent::kFieldAccess:
      return caps.can_generate_field_access_events == 1;

    case ArtJvmtiEvent::kFieldModification:
      return caps.can_generate_field_modification_events == 1;

    case ArtJvmtiEvent::kFramePop:
      return caps.can_generate_frame_pop_events == 1;

    case ArtJvmtiEvent::kGarbageCollectionStart:
    case ArtJvmtiEvent::kGarbageCollectionFinish:
      return caps.can_generate_garbage_collection_events == 1;

    case ArtJvmtiEvent::kMethodEntry:
      return caps.can_generate_method_entry_events == 1;

    case ArtJvmtiEvent::kMethodExit:
      return caps.can_generate_method_exit_events == 1;

    case ArtJvmtiEvent::kMonitorContendedEnter:
    case ArtJvmtiEvent::kMonitorContendedEntered:
    case ArtJvmtiEvent::kMonitorWait:
    case ArtJvmtiEvent::kMonitorWaited:
      return caps.can_generate_monitor_events == 1;

    case ArtJvmtiEvent::kNativeMethodBind:
      return caps.can_generate_native_method_bind_events == 1;

    case ArtJvmtiEvent::kObjectFree:
      return caps.can_generate_object_free_events == 1;

    case ArtJvmtiEvent::kSingleStep:
      return caps.can_generate_single_step_events == 1;

    case ArtJvmtiEvent::kVmObjectAlloc:
      return caps.can_generate_vm_object_alloc_events == 1;

    default:
      return true;
  }
}

jvmtiError EventHandler::SetEvent(ArtJvmTiEnv* env,
                                  art::Thread* thread,
                                  ArtJvmtiEvent event,
                                  jvmtiEventMode mode) {
  if (thread != nullptr) {
    art::ThreadState state = thread->GetState();
    if (state == art::ThreadState::kStarting ||
        state == art::ThreadState::kTerminated ||
        thread->IsStillStarting()) {
      return ERR(THREAD_NOT_ALIVE);
    }
    if (!IsThreadControllable(event)) {
      return ERR(ILLEGAL_ARGUMENT);
    }
  }

  if (mode != JVMTI_ENABLE && mode != JVMTI_DISABLE) {
    return ERR(ILLEGAL_ARGUMENT);
  }

  if (!EventMask::EventIsInRange(event)) {
    return ERR(INVALID_EVENT_TYPE);
  }

  if (!HasAssociatedCapability(env, event)) {
    return ERR(MUST_POSSESS_CAPABILITY);
  }

  bool old_state = global_mask.Test(event);

  if (mode == JVMTI_ENABLE) {
    env->event_masks.EnableEvent(thread, event);
    global_mask.Set(event);
  } else {
    DCHECK_EQ(mode, JVMTI_DISABLE);

    env->event_masks.DisableEvent(thread, event);
    RecalculateGlobalEventMask(event);
  }

  bool new_state = global_mask.Test(event);

  // Handle any special work required for the event type.
  if (new_state != old_state) {
    HandleEventType(event, mode == JVMTI_ENABLE);
  }

  return ERR(NONE);
}

EventHandler::EventHandler() {
  alloc_listener_.reset(new JvmtiAllocationListener(this));
  gc_pause_listener_.reset(new JvmtiGcPauseListener(this));
}

EventHandler::~EventHandler() {
}

}  // namespace openjdkjvmti
