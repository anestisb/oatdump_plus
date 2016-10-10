/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_
#define ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_

#include "events.h"

#include "art_jvmti.h"

namespace openjdkjvmti {

template <typename FnType>
ALWAYS_INLINE static inline FnType* GetCallback(ArtJvmTiEnv* env, jvmtiEvent event) {
  if (env->event_callbacks == nullptr) {
    return nullptr;
  }

  switch (event) {
    case JVMTI_EVENT_VM_INIT:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMInit);
    case JVMTI_EVENT_VM_DEATH:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMDeath);
    case JVMTI_EVENT_THREAD_START:
      return reinterpret_cast<FnType*>(env->event_callbacks->ThreadStart);
    case JVMTI_EVENT_THREAD_END:
      return reinterpret_cast<FnType*>(env->event_callbacks->ThreadEnd);
    case JVMTI_EVENT_CLASS_FILE_LOAD_HOOK:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassFileLoadHook);
    case JVMTI_EVENT_CLASS_LOAD:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassLoad);
    case JVMTI_EVENT_CLASS_PREPARE:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassPrepare);
    case JVMTI_EVENT_VM_START:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMStart);
    case JVMTI_EVENT_EXCEPTION:
      return reinterpret_cast<FnType*>(env->event_callbacks->Exception);
    case JVMTI_EVENT_EXCEPTION_CATCH:
      return reinterpret_cast<FnType*>(env->event_callbacks->ExceptionCatch);
    case JVMTI_EVENT_SINGLE_STEP:
      return reinterpret_cast<FnType*>(env->event_callbacks->SingleStep);
    case JVMTI_EVENT_FRAME_POP:
      return reinterpret_cast<FnType*>(env->event_callbacks->FramePop);
    case JVMTI_EVENT_BREAKPOINT:
      return reinterpret_cast<FnType*>(env->event_callbacks->Breakpoint);
    case JVMTI_EVENT_FIELD_ACCESS:
      return reinterpret_cast<FnType*>(env->event_callbacks->FieldAccess);
    case JVMTI_EVENT_FIELD_MODIFICATION:
      return reinterpret_cast<FnType*>(env->event_callbacks->FieldModification);
    case JVMTI_EVENT_METHOD_ENTRY:
      return reinterpret_cast<FnType*>(env->event_callbacks->MethodEntry);
    case JVMTI_EVENT_METHOD_EXIT:
      return reinterpret_cast<FnType*>(env->event_callbacks->MethodExit);
    case JVMTI_EVENT_NATIVE_METHOD_BIND:
      return reinterpret_cast<FnType*>(env->event_callbacks->NativeMethodBind);
    case JVMTI_EVENT_COMPILED_METHOD_LOAD:
      return reinterpret_cast<FnType*>(env->event_callbacks->CompiledMethodLoad);
    case JVMTI_EVENT_COMPILED_METHOD_UNLOAD:
      return reinterpret_cast<FnType*>(env->event_callbacks->CompiledMethodUnload);
    case JVMTI_EVENT_DYNAMIC_CODE_GENERATED:
      return reinterpret_cast<FnType*>(env->event_callbacks->DynamicCodeGenerated);
    case JVMTI_EVENT_DATA_DUMP_REQUEST:
      return reinterpret_cast<FnType*>(env->event_callbacks->DataDumpRequest);
    case JVMTI_EVENT_MONITOR_WAIT:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorWait);
    case JVMTI_EVENT_MONITOR_WAITED:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorWaited);
    case JVMTI_EVENT_MONITOR_CONTENDED_ENTER:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorContendedEnter);
    case JVMTI_EVENT_MONITOR_CONTENDED_ENTERED:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorContendedEntered);
    case JVMTI_EVENT_RESOURCE_EXHAUSTED:
      return reinterpret_cast<FnType*>(env->event_callbacks->ResourceExhausted);
    case JVMTI_EVENT_GARBAGE_COLLECTION_START:
      return reinterpret_cast<FnType*>(env->event_callbacks->GarbageCollectionStart);
    case JVMTI_EVENT_GARBAGE_COLLECTION_FINISH:
      return reinterpret_cast<FnType*>(env->event_callbacks->GarbageCollectionFinish);
    case JVMTI_EVENT_OBJECT_FREE:
      return reinterpret_cast<FnType*>(env->event_callbacks->ObjectFree);
    case JVMTI_EVENT_VM_OBJECT_ALLOC:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMObjectAlloc);
  }
  return nullptr;
}

template <typename ...Args>
inline void EventHandler::DispatchEvent(art::Thread* thread, jvmtiEvent event, Args... args) {
  using FnType = void(jvmtiEnv*, Args...);
  for (ArtJvmTiEnv* env : envs) {
    bool dispatch = env->event_masks.global_event_mask.Test(event);

    if (!dispatch && thread != nullptr && env->event_masks.unioned_thread_event_mask.Test(event)) {
      EventMask* mask = env->event_masks.GetEventMaskOrNull(thread);
      dispatch = mask != nullptr && mask->Test(event);
    }

    if (dispatch) {
      FnType* callback = GetCallback<FnType>(env, event);
      if (callback != nullptr) {
        (*callback)(env, args...);
      }
    }
  }
}

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_
