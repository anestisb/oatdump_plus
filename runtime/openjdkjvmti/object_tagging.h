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

#ifndef ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
#define ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_

#include "base/mutex.h"
#include "gc/system_weak.h"
#include "gc_root-inl.h"
#include "mirror/object.h"
#include "thread-inl.h"

namespace openjdkjvmti {

class EventHandler;

class ObjectTagTable : public art::gc::SystemWeakHolder {
 public:
  explicit ObjectTagTable(EventHandler* event_handler)
      : art::gc::SystemWeakHolder(art::LockLevel::kAllocTrackerLock),
        event_handler_(event_handler) {
  }

  void Add(art::mirror::Object* obj, jlong tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  bool Remove(art::mirror::Object* obj, jlong* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  bool Set(art::mirror::Object* obj, jlong tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  bool GetTag(art::mirror::Object* obj, jlong* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    art::MutexLock mu(self, allow_disallow_lock_);
    Wait(self);

    for (const auto& pair : tagged_objects_) {
      if (pair.first.Read(nullptr) == obj) {
        *result = pair.second;
        return true;
      }
    }

    return false;
  }

  void Sweep(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

 private:
  using Entry = std::pair<art::GcRoot<art::mirror::Object>, jlong>;

  template <bool kHandleNull>
  void SweepImpl(art::IsMarkedVisitor* visitor)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!allow_disallow_lock_);
  void HandleNullSweep(jlong tag);

  std::vector<Entry> tagged_objects_ GUARDED_BY(allow_disallow_lock_);
  size_t first_free_ = 0;

  EventHandler* event_handler_;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
