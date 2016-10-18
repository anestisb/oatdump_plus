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
#include "instrumentation.h"
#include "jni_env_ext-inl.h"
#include "mirror/class.h"
#include "mirror/object.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

namespace openjdkjvmti {

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


void EventMasks::EnableEvent(art::Thread* thread, jvmtiEvent event) {
  DCHECK(EventMask::EventIsInRange(event));
  GetEventMask(thread).Set(event);
  if (thread != nullptr) {
    unioned_thread_event_mask.Set(event, true);
  }
}

void EventMasks::DisableEvent(art::Thread* thread, jvmtiEvent event) {
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

void EventHandler::RegisterArtJvmTiEnv(ArtJvmTiEnv* env) {
  envs.push_back(env);
}

static bool IsThreadControllable(jvmtiEvent event) {
  switch (event) {
    case JVMTI_EVENT_VM_INIT:
    case JVMTI_EVENT_VM_START:
    case JVMTI_EVENT_VM_DEATH:
    case JVMTI_EVENT_THREAD_START:
    case JVMTI_EVENT_COMPILED_METHOD_LOAD:
    case JVMTI_EVENT_COMPILED_METHOD_UNLOAD:
    case JVMTI_EVENT_DYNAMIC_CODE_GENERATED:
    case JVMTI_EVENT_DATA_DUMP_REQUEST:
      return false;

    default:
      return true;
  }
}

class JvmtiAllocationListener : public art::gc::AllocationListener {
 public:
  explicit JvmtiAllocationListener(EventHandler* handler) : handler_(handler) {}

  void ObjectAllocated(art::Thread* self, art::ObjPtr<art::mirror::Object>* obj, size_t byte_count)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK_EQ(self, art::Thread::Current());

    if (handler_->IsEventEnabledAnywhere(JVMTI_EVENT_VM_OBJECT_ALLOC)) {
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

      handler_->DispatchEvent(self,
                              JVMTI_EVENT_VM_OBJECT_ALLOC,
                              jni_env,
                              thread.get(),
                              object.get(),
                              klass.get(),
                              byte_count);
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

// Handle special work for the given event type, if necessary.
void EventHandler::HandleEventType(jvmtiEvent event, bool enable) {
  if (event == JVMTI_EVENT_VM_OBJECT_ALLOC) {
    SetupObjectAllocationTracking(alloc_listener_.get(), enable);
    return;
  }
}

jvmtiError EventHandler::SetEvent(ArtJvmTiEnv* env,
                                  art::Thread* thread,
                                  jvmtiEvent event,
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

  // TODO: Capability check.

  if (mode != JVMTI_ENABLE && mode != JVMTI_DISABLE) {
    return ERR(ILLEGAL_ARGUMENT);
  }

  if (!EventMask::EventIsInRange(event)) {
    return ERR(INVALID_EVENT_TYPE);
  }

  bool old_state = global_mask.Test(event);

  if (mode == JVMTI_ENABLE) {
    env->event_masks.EnableEvent(thread, event);
    global_mask.Set(event);
  } else {
    DCHECK_EQ(mode, JVMTI_DISABLE);

    env->event_masks.DisableEvent(thread, event);

    // Gotta recompute the global mask.
    bool union_value = false;
    for (const ArtJvmTiEnv* stored_env : envs) {
      union_value |= stored_env->event_masks.global_event_mask.Test(event);
      union_value |= stored_env->event_masks.unioned_thread_event_mask.Test(event);
      if (union_value) {
        break;
      }
    }
    global_mask.Set(event, union_value);
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
}

EventHandler::~EventHandler() {
}

}  // namespace openjdkjvmti
