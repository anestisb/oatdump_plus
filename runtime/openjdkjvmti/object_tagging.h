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

#include <unordered_map>

#include "base/mutex.h"
#include "gc/system_weak.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jvmti.h"
#include "mirror/object.h"
#include "thread-inl.h"

namespace openjdkjvmti {

class EventHandler;

class ObjectTagTable : public art::gc::SystemWeakHolder {
 public:
  explicit ObjectTagTable(EventHandler* event_handler)
      : art::gc::SystemWeakHolder(art::LockLevel::kAllocTrackerLock),
        update_since_last_sweep_(false),
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

    return GetTagLocked(self, obj, result);
  }

  void Sweep(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

  jvmtiError GetTaggedObjects(jvmtiEnv* jvmti_env,
                              jint tag_count,
                              const jlong* tags,
                              jint* count_ptr,
                              jobject** object_result_ptr,
                              jlong** tag_result_ptr)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);

 private:
  bool SetLocked(art::Thread* self, art::mirror::Object* obj, jlong tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  bool RemoveLocked(art::Thread* self, art::mirror::Object* obj, jlong* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  bool GetTagLocked(art::Thread* self, art::mirror::Object* obj, jlong* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_) {
    auto it = tagged_objects_.find(art::GcRoot<art::mirror::Object>(obj));
    if (it != tagged_objects_.end()) {
      *result = it->second;
      return true;
    }

    if (art::kUseReadBarrier &&
        self != nullptr &&
        self->GetIsGcMarking() &&
        !update_since_last_sweep_) {
      return GetTagSlowPath(self, obj, result);
    }

    return false;
  }

  // Slow-path for GetTag. We didn't find the object, but we might be storing from-pointers and
  // are asked to retrieve with a to-pointer.
  bool GetTagSlowPath(art::Thread* self, art::mirror::Object* obj, jlong* result)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  // Update the table by doing read barriers on each element, ensuring that to-space pointers
  // are stored.
  void UpdateTableWithReadBarrier()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  template <bool kHandleNull>
  void SweepImpl(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_);
  void HandleNullSweep(jlong tag);

  enum TableUpdateNullTarget {
    kIgnoreNull,
    kRemoveNull,
    kCallHandleNull
  };

  template <typename T, TableUpdateNullTarget kTargetNull>
  void UpdateTableWith(T& updater)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(allow_disallow_lock_);

  struct HashGcRoot {
    size_t operator()(const art::GcRoot<art::mirror::Object>& r) const
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      return reinterpret_cast<uintptr_t>(r.Read<art::kWithoutReadBarrier>());
    }
  };

  struct EqGcRoot {
    bool operator()(const art::GcRoot<art::mirror::Object>& r1,
                    const art::GcRoot<art::mirror::Object>& r2) const
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      return r1.Read<art::kWithoutReadBarrier>() == r2.Read<art::kWithoutReadBarrier>();
    }
  };

  std::unordered_map<art::GcRoot<art::mirror::Object>,
                     jlong,
                     HashGcRoot,
                     EqGcRoot> tagged_objects_
      GUARDED_BY(allow_disallow_lock_)
      GUARDED_BY(art::Locks::mutator_lock_);
  // To avoid repeatedly scanning the whole table, remember if we did that since the last sweep.
  bool update_since_last_sweep_;

  EventHandler* event_handler_;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
