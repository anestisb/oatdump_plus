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

#include <array>

#include "events.h"

#include "art_jvmti.h"

namespace openjdkjvmti {

static inline ArtJvmtiEvent GetArtJvmtiEvent(ArtJvmTiEnv* env, jvmtiEvent e) {
  if (UNLIKELY(e == JVMTI_EVENT_CLASS_FILE_LOAD_HOOK)) {
    if (env->capabilities.can_retransform_classes) {
      return ArtJvmtiEvent::kClassFileLoadHookRetransformable;
    } else {
      return ArtJvmtiEvent::kClassFileLoadHookNonRetransformable;
    }
  } else {
    return static_cast<ArtJvmtiEvent>(e);
  }
}

template <typename FnType>
ALWAYS_INLINE static inline FnType* GetCallback(ArtJvmTiEnv* env, ArtJvmtiEvent event) {
  if (env->event_callbacks == nullptr) {
    return nullptr;
  }

  // TODO: Add a type check. Can be done, for example, by an explicitly instantiated template
  //       function.

  switch (event) {
    case ArtJvmtiEvent::kVmInit:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMInit);
    case ArtJvmtiEvent::kVmDeath:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMDeath);
    case ArtJvmtiEvent::kThreadStart:
      return reinterpret_cast<FnType*>(env->event_callbacks->ThreadStart);
    case ArtJvmtiEvent::kThreadEnd:
      return reinterpret_cast<FnType*>(env->event_callbacks->ThreadEnd);
    case ArtJvmtiEvent::kClassFileLoadHookRetransformable:
    case ArtJvmtiEvent::kClassFileLoadHookNonRetransformable:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassFileLoadHook);
    case ArtJvmtiEvent::kClassLoad:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassLoad);
    case ArtJvmtiEvent::kClassPrepare:
      return reinterpret_cast<FnType*>(env->event_callbacks->ClassPrepare);
    case ArtJvmtiEvent::kVmStart:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMStart);
    case ArtJvmtiEvent::kException:
      return reinterpret_cast<FnType*>(env->event_callbacks->Exception);
    case ArtJvmtiEvent::kExceptionCatch:
      return reinterpret_cast<FnType*>(env->event_callbacks->ExceptionCatch);
    case ArtJvmtiEvent::kSingleStep:
      return reinterpret_cast<FnType*>(env->event_callbacks->SingleStep);
    case ArtJvmtiEvent::kFramePop:
      return reinterpret_cast<FnType*>(env->event_callbacks->FramePop);
    case ArtJvmtiEvent::kBreakpoint:
      return reinterpret_cast<FnType*>(env->event_callbacks->Breakpoint);
    case ArtJvmtiEvent::kFieldAccess:
      return reinterpret_cast<FnType*>(env->event_callbacks->FieldAccess);
    case ArtJvmtiEvent::kFieldModification:
      return reinterpret_cast<FnType*>(env->event_callbacks->FieldModification);
    case ArtJvmtiEvent::kMethodEntry:
      return reinterpret_cast<FnType*>(env->event_callbacks->MethodEntry);
    case ArtJvmtiEvent::kMethodExit:
      return reinterpret_cast<FnType*>(env->event_callbacks->MethodExit);
    case ArtJvmtiEvent::kNativeMethodBind:
      return reinterpret_cast<FnType*>(env->event_callbacks->NativeMethodBind);
    case ArtJvmtiEvent::kCompiledMethodLoad:
      return reinterpret_cast<FnType*>(env->event_callbacks->CompiledMethodLoad);
    case ArtJvmtiEvent::kCompiledMethodUnload:
      return reinterpret_cast<FnType*>(env->event_callbacks->CompiledMethodUnload);
    case ArtJvmtiEvent::kDynamicCodeGenerated:
      return reinterpret_cast<FnType*>(env->event_callbacks->DynamicCodeGenerated);
    case ArtJvmtiEvent::kDataDumpRequest:
      return reinterpret_cast<FnType*>(env->event_callbacks->DataDumpRequest);
    case ArtJvmtiEvent::kMonitorWait:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorWait);
    case ArtJvmtiEvent::kMonitorWaited:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorWaited);
    case ArtJvmtiEvent::kMonitorContendedEnter:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorContendedEnter);
    case ArtJvmtiEvent::kMonitorContendedEntered:
      return reinterpret_cast<FnType*>(env->event_callbacks->MonitorContendedEntered);
    case ArtJvmtiEvent::kResourceExhausted:
      return reinterpret_cast<FnType*>(env->event_callbacks->ResourceExhausted);
    case ArtJvmtiEvent::kGarbageCollectionStart:
      return reinterpret_cast<FnType*>(env->event_callbacks->GarbageCollectionStart);
    case ArtJvmtiEvent::kGarbageCollectionFinish:
      return reinterpret_cast<FnType*>(env->event_callbacks->GarbageCollectionFinish);
    case ArtJvmtiEvent::kObjectFree:
      return reinterpret_cast<FnType*>(env->event_callbacks->ObjectFree);
    case ArtJvmtiEvent::kVmObjectAlloc:
      return reinterpret_cast<FnType*>(env->event_callbacks->VMObjectAlloc);
  }
  return nullptr;
}

template <typename ...Args>
inline void EventHandler::DispatchEvent(art::Thread* thread,
                                        ArtJvmtiEvent event,
                                        Args... args) const {
  using FnType = void(jvmtiEnv*, Args...);
  for (ArtJvmTiEnv* env : envs) {
    if (ShouldDispatch(event, env, thread)) {
      FnType* callback = GetCallback<FnType>(env, event);
      if (callback != nullptr) {
        (*callback)(env, args...);
      }
    }
  }
}

inline bool EventHandler::ShouldDispatch(ArtJvmtiEvent event,
                                         ArtJvmTiEnv* env,
                                         art::Thread* thread) {
  bool dispatch = env->event_masks.global_event_mask.Test(event);

  if (!dispatch && thread != nullptr && env->event_masks.unioned_thread_event_mask.Test(event)) {
    EventMask* mask = env->event_masks.GetEventMaskOrNull(thread);
    dispatch = mask != nullptr && mask->Test(event);
  }
  return dispatch;
}

inline void EventHandler::RecalculateGlobalEventMask(ArtJvmtiEvent event) {
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

inline bool EventHandler::NeedsEventUpdate(ArtJvmTiEnv* env,
                                           const jvmtiCapabilities& caps,
                                           bool added) {
  ArtJvmtiEvent event = added ? ArtJvmtiEvent::kClassFileLoadHookNonRetransformable
                              : ArtJvmtiEvent::kClassFileLoadHookRetransformable;
  return caps.can_retransform_classes == 1 &&
      IsEventEnabledAnywhere(event) &&
      env->event_masks.IsEnabledAnywhere(event);
}

inline void EventHandler::HandleChangedCapabilities(ArtJvmTiEnv* env,
                                                    const jvmtiCapabilities& caps,
                                                    bool added) {
  if (UNLIKELY(NeedsEventUpdate(env, caps, added))) {
    env->event_masks.HandleChangedCapabilities(caps, added);
    if (caps.can_retransform_classes == 1) {
      RecalculateGlobalEventMask(ArtJvmtiEvent::kClassFileLoadHookRetransformable);
      RecalculateGlobalEventMask(ArtJvmtiEvent::kClassFileLoadHookNonRetransformable);
    }
  }
}

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_EVENTS_INL_H_
