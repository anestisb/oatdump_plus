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

#include "art_field-inl.h"
#include "art_jvmti.h"
#include "art_method-inl.h"
#include "base/logging.h"
#include "gc/allocation_listener.h"
#include "gc/gc_pause_listener.h"
#include "gc/heap.h"
#include "gc/scoped_gc_critical_section.h"
#include "handle_scope-inl.h"
#include "instrumentation.h"
#include "jni_env_ext-inl.h"
#include "jni_internal.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "nativehelper/ScopedLocalRef.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "ti_phase.h"

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

template<typename Type>
static Type AddLocalRef(art::JNIEnvExt* e, art::mirror::Object* obj)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  return (obj == nullptr) ? nullptr : e->AddLocalReference<Type>(obj);
}

class JvmtiMethodTraceListener FINAL : public art::instrumentation::InstrumentationListener {
 public:
  explicit JvmtiMethodTraceListener(EventHandler* handler) : event_handler_(handler) {}

  template<ArtJvmtiEvent kEvent, typename ...Args>
  void RunEventCallback(art::Thread* self, art::JNIEnvExt* jnienv, Args... args)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    ScopedLocalRef<jthread> thread_jni(jnienv, AddLocalRef<jthread>(jnienv, self->GetPeer()));
    // Just give the event a good sized JNI frame. 100 should be fine.
    jnienv->PushFrame(100);
    {
      // Need to do trampoline! :(
      art::ScopedThreadSuspension sts(self, art::ThreadState::kNative);
      event_handler_->DispatchEvent<kEvent>(self,
                                            static_cast<JNIEnv*>(jnienv),
                                            thread_jni.get(),
                                            args...);
    }
    jnienv->PopFrame();
  }

  // Call-back for when a method is entered.
  void MethodEntered(art::Thread* self,
                     art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                     art::ArtMethod* method,
                     uint32_t dex_pc ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (!method->IsRuntimeMethod() &&
        event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kMethodEntry)) {
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      RunEventCallback<ArtJvmtiEvent::kMethodEntry>(self,
                                                    jnienv,
                                                    art::jni::EncodeArtMethod(method));
    }
  }

  // Callback for when a method is exited with a reference return value.
  void MethodExited(art::Thread* self,
                    art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                    art::ArtMethod* method,
                    uint32_t dex_pc ATTRIBUTE_UNUSED,
                    art::Handle<art::mirror::Object> return_value)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (!method->IsRuntimeMethod() &&
        event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kMethodExit)) {
      DCHECK_EQ(method->GetReturnTypePrimitive(), art::Primitive::kPrimNot)
          << method->PrettyMethod();
      DCHECK(!self->IsExceptionPending());
      jvalue val;
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      ScopedLocalRef<jobject> return_jobj(jnienv, AddLocalRef<jobject>(jnienv, return_value.Get()));
      val.l = return_jobj.get();
      RunEventCallback<ArtJvmtiEvent::kMethodExit>(
          self,
          jnienv,
          art::jni::EncodeArtMethod(method),
          /*was_popped_by_exception*/ static_cast<jboolean>(JNI_FALSE),
          val);
    }
  }

  // Call-back for when a method is exited.
  void MethodExited(art::Thread* self,
                    art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                    art::ArtMethod* method,
                    uint32_t dex_pc ATTRIBUTE_UNUSED,
                    const art::JValue& return_value)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (!method->IsRuntimeMethod() &&
        event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kMethodExit)) {
      DCHECK_NE(method->GetReturnTypePrimitive(), art::Primitive::kPrimNot)
          << method->PrettyMethod();
      DCHECK(!self->IsExceptionPending());
      jvalue val;
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      // 64bit integer is the largest value in the union so we should be fine simply copying it into
      // the union.
      val.j = return_value.GetJ();
      RunEventCallback<ArtJvmtiEvent::kMethodExit>(
          self,
          jnienv,
          art::jni::EncodeArtMethod(method),
          /*was_popped_by_exception*/ static_cast<jboolean>(JNI_FALSE),
          val);
    }
  }

  // Call-back for when a method is popped due to an exception throw. A method will either cause a
  // MethodExited call-back or a MethodUnwind call-back when its activation is removed.
  void MethodUnwind(art::Thread* self,
                    art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                    art::ArtMethod* method,
                    uint32_t dex_pc ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (!method->IsRuntimeMethod() &&
        event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kMethodExit)) {
      jvalue val;
      // Just set this to 0xffffffffffffffff so it's not uninitialized.
      val.j = static_cast<jlong>(-1);
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      art::StackHandleScope<1> hs(self);
      art::Handle<art::mirror::Throwable> old_exception(hs.NewHandle(self->GetException()));
      CHECK(!old_exception.IsNull());
      self->ClearException();
      RunEventCallback<ArtJvmtiEvent::kMethodExit>(
          self,
          jnienv,
          art::jni::EncodeArtMethod(method),
          /*was_popped_by_exception*/ static_cast<jboolean>(JNI_TRUE),
          val);
      // Match RI behavior of just throwing away original exception if a new one is thrown.
      if (LIKELY(!self->IsExceptionPending())) {
        self->SetException(old_exception.Get());
      }
    }
  }

  // Call-back for when the dex pc moves in a method.
  void DexPcMoved(art::Thread* self,
                  art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                  art::ArtMethod* method,
                  uint32_t new_dex_pc)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    DCHECK(!method->IsRuntimeMethod());
    // Default methods might be copied to multiple classes. We need to get the canonical version of
    // this method so that we can check for breakpoints correctly.
    // TODO We should maybe do this on other events to ensure that we are consistent WRT default
    // methods. This could interact with obsolete methods if we ever let interface redefinition
    // happen though.
    method = method->GetCanonicalMethod();
    art::JNIEnvExt* jnienv = self->GetJniEnv();
    jmethodID jmethod = art::jni::EncodeArtMethod(method);
    jlocation location = static_cast<jlocation>(new_dex_pc);
    // Step event is reported first according to the spec.
    if (event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kSingleStep)) {
      RunEventCallback<ArtJvmtiEvent::kSingleStep>(self, jnienv, jmethod, location);
    }
    // Next we do the Breakpoint events. The Dispatch code will filter the individual
    if (event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kBreakpoint)) {
      RunEventCallback<ArtJvmtiEvent::kBreakpoint>(self, jnienv, jmethod, location);
    }
  }

  // Call-back for when we read from a field.
  void FieldRead(art::Thread* self,
                 art::Handle<art::mirror::Object> this_object,
                 art::ArtMethod* method,
                 uint32_t dex_pc,
                 art::ArtField* field)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kFieldAccess)) {
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      // DCHECK(!self->IsExceptionPending());
      ScopedLocalRef<jobject> this_ref(jnienv, AddLocalRef<jobject>(jnienv, this_object.Get()));
      ScopedLocalRef<jobject> fklass(jnienv,
                                     AddLocalRef<jobject>(jnienv,
                                                          field->GetDeclaringClass().Ptr()));
      RunEventCallback<ArtJvmtiEvent::kFieldAccess>(self,
                                                    jnienv,
                                                    art::jni::EncodeArtMethod(method),
                                                    static_cast<jlocation>(dex_pc),
                                                    static_cast<jclass>(fklass.get()),
                                                    this_ref.get(),
                                                    art::jni::EncodeArtField(field));
    }
  }

  void FieldWritten(art::Thread* self,
                    art::Handle<art::mirror::Object> this_object,
                    art::ArtMethod* method,
                    uint32_t dex_pc,
                    art::ArtField* field,
                    art::Handle<art::mirror::Object> new_val)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kFieldModification)) {
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      // DCHECK(!self->IsExceptionPending());
      ScopedLocalRef<jobject> this_ref(jnienv, AddLocalRef<jobject>(jnienv, this_object.Get()));
      ScopedLocalRef<jobject> fklass(jnienv,
                                     AddLocalRef<jobject>(jnienv,
                                                          field->GetDeclaringClass().Ptr()));
      ScopedLocalRef<jobject> fval(jnienv, AddLocalRef<jobject>(jnienv, new_val.Get()));
      jvalue val;
      val.l = fval.get();
      RunEventCallback<ArtJvmtiEvent::kFieldModification>(
          self,
          jnienv,
          art::jni::EncodeArtMethod(method),
          static_cast<jlocation>(dex_pc),
          static_cast<jclass>(fklass.get()),
          field->IsStatic() ? nullptr :  this_ref.get(),
          art::jni::EncodeArtField(field),
          'L',  // type_char
          val);
    }
  }

  // Call-back for when we write into a field.
  void FieldWritten(art::Thread* self,
                    art::Handle<art::mirror::Object> this_object,
                    art::ArtMethod* method,
                    uint32_t dex_pc,
                    art::ArtField* field,
                    const art::JValue& field_value)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    if (event_handler_->IsEventEnabledAnywhere(ArtJvmtiEvent::kFieldModification)) {
      art::JNIEnvExt* jnienv = self->GetJniEnv();
      DCHECK(!self->IsExceptionPending());
      ScopedLocalRef<jobject> this_ref(jnienv, AddLocalRef<jobject>(jnienv, this_object.Get()));
      ScopedLocalRef<jobject> fklass(jnienv,
                                     AddLocalRef<jobject>(jnienv,
                                                          field->GetDeclaringClass().Ptr()));
      char type_char = art::Primitive::Descriptor(field->GetTypeAsPrimitiveType())[0];
      jvalue val;
      // 64bit integer is the largest value in the union so we should be fine simply copying it into
      // the union.
      val.j = field_value.GetJ();
      RunEventCallback<ArtJvmtiEvent::kFieldModification>(
          self,
          jnienv,
          art::jni::EncodeArtMethod(method),
          static_cast<jlocation>(dex_pc),
          static_cast<jclass>(fklass.get()),
          field->IsStatic() ? nullptr :  this_ref.get(),  // nb static field modification get given
                                                          // the class as this_object for some
                                                          // reason.
          art::jni::EncodeArtField(field),
          type_char,
          val);
    }
  }

  // Call-back when an exception is caught.
  void ExceptionCaught(art::Thread* self ATTRIBUTE_UNUSED,
                       art::Handle<art::mirror::Throwable> exception_object ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    return;
  }

  // Call-back for when we execute a branch.
  void Branch(art::Thread* self ATTRIBUTE_UNUSED,
              art::ArtMethod* method ATTRIBUTE_UNUSED,
              uint32_t dex_pc ATTRIBUTE_UNUSED,
              int32_t dex_pc_offset ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    return;
  }

  // Call-back for when we get an invokevirtual or an invokeinterface.
  void InvokeVirtualOrInterface(art::Thread* self ATTRIBUTE_UNUSED,
                                art::Handle<art::mirror::Object> this_object ATTRIBUTE_UNUSED,
                                art::ArtMethod* caller ATTRIBUTE_UNUSED,
                                uint32_t dex_pc ATTRIBUTE_UNUSED,
                                art::ArtMethod* callee ATTRIBUTE_UNUSED)
      REQUIRES_SHARED(art::Locks::mutator_lock_) OVERRIDE {
    return;
  }

 private:
  EventHandler* const event_handler_;
};

static uint32_t GetInstrumentationEventsFor(ArtJvmtiEvent event) {
  switch (event) {
    case ArtJvmtiEvent::kMethodEntry:
      return art::instrumentation::Instrumentation::kMethodEntered;
    case ArtJvmtiEvent::kMethodExit:
      return art::instrumentation::Instrumentation::kMethodExited |
             art::instrumentation::Instrumentation::kMethodUnwind;
    case ArtJvmtiEvent::kFieldModification:
      return art::instrumentation::Instrumentation::kFieldWritten;
    case ArtJvmtiEvent::kFieldAccess:
      return art::instrumentation::Instrumentation::kFieldRead;
    case ArtJvmtiEvent::kBreakpoint:
    case ArtJvmtiEvent::kSingleStep:
      return art::instrumentation::Instrumentation::kDexPcMoved;
    default:
      LOG(FATAL) << "Unknown event ";
      return 0;
  }
}

static void SetupTraceListener(JvmtiMethodTraceListener* listener,
                               ArtJvmtiEvent event,
                               bool enable) {
  art::ScopedThreadStateChange stsc(art::Thread::Current(), art::ThreadState::kNative);
  uint32_t new_events = GetInstrumentationEventsFor(event);
  art::instrumentation::Instrumentation* instr = art::Runtime::Current()->GetInstrumentation();
  art::gc::ScopedGCCriticalSection gcs(art::Thread::Current(),
                                       art::gc::kGcCauseInstrumentation,
                                       art::gc::kCollectorTypeInstrumentation);
  art::ScopedSuspendAll ssa("jvmti method tracing installation");
  if (enable) {
    // TODO Depending on the features being used we should be able to avoid deoptimizing everything
    // like we do here.
    if (!instr->AreAllMethodsDeoptimized()) {
      instr->EnableMethodTracing("jvmti-tracing", /*needs_interpreter*/true);
    }
    instr->AddListener(listener, new_events);
  } else {
    instr->RemoveListener(listener, new_events);
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

    case ArtJvmtiEvent::kBreakpoint:
    case ArtJvmtiEvent::kSingleStep: {
      ArtJvmtiEvent other = (event == ArtJvmtiEvent::kBreakpoint) ? ArtJvmtiEvent::kSingleStep
                                                                  : ArtJvmtiEvent::kBreakpoint;
      // We only need to do anything if there isn't already a listener installed/held-on by the
      // other jvmti event that uses DexPcMoved.
      if (!IsEventEnabledAnywhere(other)) {
        SetupTraceListener(method_trace_listener_.get(), event, enable);
      }
      return;
    }
    case ArtJvmtiEvent::kMethodEntry:
    case ArtJvmtiEvent::kMethodExit:
    case ArtJvmtiEvent::kFieldAccess:
    case ArtJvmtiEvent::kFieldModification:
      SetupTraceListener(method_trace_listener_.get(), event, enable);
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

void EventHandler::Shutdown() {
  // Need to remove the method_trace_listener_ if it's there.
  art::Thread* self = art::Thread::Current();
  art::gc::ScopedGCCriticalSection gcs(self,
                                       art::gc::kGcCauseInstrumentation,
                                       art::gc::kCollectorTypeInstrumentation);
  art::ScopedSuspendAll ssa("jvmti method tracing uninstallation");
  // Just remove every possible event.
  art::Runtime::Current()->GetInstrumentation()->RemoveListener(method_trace_listener_.get(), ~0);
}

EventHandler::EventHandler() {
  alloc_listener_.reset(new JvmtiAllocationListener(this));
  gc_pause_listener_.reset(new JvmtiGcPauseListener(this));
  method_trace_listener_.reset(new JvmtiMethodTraceListener(this));
}

EventHandler::~EventHandler() {
}

}  // namespace openjdkjvmti
