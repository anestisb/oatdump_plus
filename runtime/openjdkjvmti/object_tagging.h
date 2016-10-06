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

class ObjectTagTable : public art::gc::SystemWeakHolder {
 public:
  ObjectTagTable() : art::gc::SystemWeakHolder(art::LockLevel::kAllocTrackerLock) {
  }

  void Add(art::mirror::Object* obj, jlong tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    art::MutexLock mu(self, allow_disallow_lock_);
    Wait(self);

    if (first_free_ == tagged_objects_.size()) {
      tagged_objects_.push_back(Entry(art::GcRoot<art::mirror::Object>(obj), tag));
      first_free_++;
    } else {
      DCHECK_LT(first_free_, tagged_objects_.size());
      DCHECK(tagged_objects_[first_free_].first.IsNull());
      tagged_objects_[first_free_] = Entry(art::GcRoot<art::mirror::Object>(obj), tag);
      // TODO: scan for free elements.
      first_free_ = tagged_objects_.size();
    }
  }

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

  bool Remove(art::mirror::Object* obj, jlong* tag)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    art::MutexLock mu(self, allow_disallow_lock_);
    Wait(self);

    for (auto it = tagged_objects_.begin(); it != tagged_objects_.end(); ++it) {
      if (it->first.Read(nullptr) == obj) {
        if (tag != nullptr) {
          *tag = it->second;
        }
        it->first = art::GcRoot<art::mirror::Object>(nullptr);

        size_t index = it - tagged_objects_.begin();
        if (index < first_free_) {
          first_free_ = index;
        }

        // TODO: compaction.

        return true;
      }
    }

    return false;
  }

  void Sweep(art::IsMarkedVisitor* visitor)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!allow_disallow_lock_) {
    art::Thread* self = art::Thread::Current();
    art::MutexLock mu(self, allow_disallow_lock_);

    for (auto it = tagged_objects_.begin(); it != tagged_objects_.end(); ++it) {
      if (!it->first.IsNull()) {
        art::mirror::Object* original_obj = it->first.Read<art::kWithoutReadBarrier>();
        art::mirror::Object* target_obj = visitor->IsMarked(original_obj);
        if (original_obj != target_obj) {
          it->first = art::GcRoot<art::mirror::Object>(target_obj);

          if (target_obj == nullptr) {
            HandleNullSweep(it->second);
          }
        }
      } else {
        size_t index = it - tagged_objects_.begin();
        if (index < first_free_) {
          first_free_ = index;
        }
      }
    }
  }

 private:
  using Entry = std::pair<art::GcRoot<art::mirror::Object>, jlong>;

  void HandleNullSweep(jlong tag ATTRIBUTE_UNUSED) {
    // TODO: Handle deallocation reporting here. We'll have to enqueue tags temporarily, as we
    //       probably shouldn't call the callbacks directly (to avoid any issues).
  }

  std::vector<Entry> tagged_objects_ GUARDED_BY(allow_disallow_lock_);
  size_t first_free_ = 0;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_OBJECT_TAGGING_H_
