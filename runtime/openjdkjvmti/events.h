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

#ifndef ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_
#define ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_

#include <bitset>
#include <vector>

#include "base/logging.h"
#include "jvmti.h"
#include "thread.h"

namespace openjdkjvmti {

struct ArtJvmTiEnv;
class JvmtiAllocationListener;
class JvmtiGcPauseListener;

struct EventMask {
  static constexpr size_t kEventsSize = JVMTI_MAX_EVENT_TYPE_VAL - JVMTI_MIN_EVENT_TYPE_VAL + 1;
  std::bitset<kEventsSize> bit_set;

  static bool EventIsInRange(jvmtiEvent event) {
    return event >= JVMTI_MIN_EVENT_TYPE_VAL && event <= JVMTI_MAX_EVENT_TYPE_VAL;
  }

  void Set(jvmtiEvent event, bool value = true) {
    DCHECK(EventIsInRange(event));
    bit_set.set(event - JVMTI_MIN_EVENT_TYPE_VAL, value);
  }

  bool Test(jvmtiEvent event) const {
    DCHECK(EventIsInRange(event));
    return bit_set.test(event - JVMTI_MIN_EVENT_TYPE_VAL);
  }
};

struct EventMasks {
  // The globally enabled events.
  EventMask global_event_mask;

  // The per-thread enabled events.

  // It is not enough to store a Thread pointer, as these may be reused. Use the pointer and the
  // thread id.
  // Note: We could just use the tid like tracing does.
  using UniqueThread = std::pair<art::Thread*, uint32_t>;
  // TODO: Native thread objects are immovable, so we can use them as keys in an (unordered) map,
  //       if necessary.
  std::vector<std::pair<UniqueThread, EventMask>> thread_event_masks;

  // A union of the per-thread events, for fast-pathing.
  EventMask unioned_thread_event_mask;

  EventMask& GetEventMask(art::Thread* thread);
  EventMask* GetEventMaskOrNull(art::Thread* thread);
  void EnableEvent(art::Thread* thread, jvmtiEvent event);
  void DisableEvent(art::Thread* thread, jvmtiEvent event);
};

// Helper class for event handling.
class EventHandler {
 public:
  EventHandler();
  ~EventHandler();

  // Register an env. It is assumed that this happens on env creation, that is, no events are
  // enabled, yet.
  void RegisterArtJvmTiEnv(ArtJvmTiEnv* env);

  bool IsEventEnabledAnywhere(jvmtiEvent event) {
    if (!EventMask::EventIsInRange(event)) {
      return false;
    }
    return global_mask.Test(event);
  }

  jvmtiError SetEvent(ArtJvmTiEnv* env, art::Thread* thread, jvmtiEvent event, jvmtiEventMode mode);

  template <typename ...Args>
  ALWAYS_INLINE inline void DispatchEvent(art::Thread* thread, jvmtiEvent event, Args... args);

 private:
  void HandleEventType(jvmtiEvent event, bool enable);

  // List of all JvmTiEnv objects that have been created, in their creation order.
  std::vector<ArtJvmTiEnv*> envs;

  // A union of all enabled events, anywhere.
  EventMask global_mask;

  std::unique_ptr<JvmtiAllocationListener> alloc_listener_;
  std::unique_ptr<JvmtiGcPauseListener> gc_pause_listener_;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_EVENTS_H_
