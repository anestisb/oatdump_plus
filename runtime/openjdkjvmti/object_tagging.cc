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

#include "object_tagging.h"

#include <limits>

#include "art_jvmti.h"
#include "base/logging.h"
#include "events-inl.h"
#include "gc/allocation_listener.h"
#include "instrumentation.h"
#include "jni_env_ext-inl.h"
#include "jvmti_allocator.h"
#include "mirror/class.h"
#include "mirror/object.h"
#include "runtime.h"
#include "ScopedLocalRef.h"

namespace openjdkjvmti {

void ObjectTagTable::UpdateTableWithReadBarrier() {
  update_since_last_sweep_ = true;

  auto WithReadBarrierUpdater = [&](const art::GcRoot<art::mirror::Object>& original_root,
                                    art::mirror::Object* original_obj ATTRIBUTE_UNUSED)
     REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return original_root.Read<art::kWithReadBarrier>();
  };

  UpdateTableWith<decltype(WithReadBarrierUpdater), kIgnoreNull>(WithReadBarrierUpdater);
}

bool ObjectTagTable::GetTagSlowPath(art::Thread* self, art::mirror::Object* obj, jlong* result) {
  // Under concurrent GC, there is a window between moving objects and sweeping of system
  // weaks in which mutators are active. We may receive a to-space object pointer in obj,
  // but still have from-space pointers in the table. Explicitly update the table once.
  // Note: this will keep *all* objects in the table live, but should be a rare occurrence.
  UpdateTableWithReadBarrier();
  return GetTagLocked(self, obj, result);
}

void ObjectTagTable::Add(art::mirror::Object* obj, jlong tag) {
  // Same as Set(), as we don't have duplicates in an unordered_map.
  Set(obj, tag);
}

bool ObjectTagTable::Remove(art::mirror::Object* obj, jlong* tag) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);
  Wait(self);

  return RemoveLocked(self, obj, tag);
}

bool ObjectTagTable::RemoveLocked(art::Thread* self, art::mirror::Object* obj, jlong* tag) {
  auto it = tagged_objects_.find(art::GcRoot<art::mirror::Object>(obj));
  if (it != tagged_objects_.end()) {
    if (tag != nullptr) {
      *tag = it->second;
    }
    tagged_objects_.erase(it);
    return true;
  }

  if (art::kUseReadBarrier && self->GetIsGcMarking() && !update_since_last_sweep_) {
    // Under concurrent GC, there is a window between moving objects and sweeping of system
    // weaks in which mutators are active. We may receive a to-space object pointer in obj,
    // but still have from-space pointers in the table. Explicitly update the table once.
    // Note: this will keep *all* objects in the table live, but should be a rare occurrence.

    // Update the table.
    UpdateTableWithReadBarrier();

    // And try again.
    return RemoveLocked(self, obj, tag);
  }

  // Not in here.
  return false;
}

bool ObjectTagTable::Set(art::mirror::Object* obj, jlong new_tag) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);
  Wait(self);

  return SetLocked(self, obj, new_tag);
}

bool ObjectTagTable::SetLocked(art::Thread* self, art::mirror::Object* obj, jlong new_tag) {
  auto it = tagged_objects_.find(art::GcRoot<art::mirror::Object>(obj));
  if (it != tagged_objects_.end()) {
    it->second = new_tag;
    return true;
  }

  if (art::kUseReadBarrier && self->GetIsGcMarking() && !update_since_last_sweep_) {
    // Under concurrent GC, there is a window between moving objects and sweeping of system
    // weaks in which mutators are active. We may receive a to-space object pointer in obj,
    // but still have from-space pointers in the table. Explicitly update the table once.
    // Note: this will keep *all* objects in the table live, but should be a rare occurrence.

    // Update the table.
    UpdateTableWithReadBarrier();

    // And try again.
    return SetLocked(self, obj, new_tag);
  }

  // New element.
  auto insert_it = tagged_objects_.emplace(art::GcRoot<art::mirror::Object>(obj), new_tag);
  DCHECK(insert_it.second);
  return false;
}

void ObjectTagTable::Sweep(art::IsMarkedVisitor* visitor) {
  if (event_handler_->IsEventEnabledAnywhere(JVMTI_EVENT_OBJECT_FREE)) {
    SweepImpl<true>(visitor);
  } else {
    SweepImpl<false>(visitor);
  }

  // Under concurrent GC, there is a window between moving objects and sweeping of system
  // weaks in which mutators are active. We may receive a to-space object pointer in obj,
  // but still have from-space pointers in the table. We explicitly update the table then
  // to ensure we compare against to-space pointers. But we want to do this only once. Once
  // sweeping is done, we know all objects are to-space pointers until the next GC cycle,
  // so we re-enable the explicit update for the next marking.
  update_since_last_sweep_ = false;
}

template <bool kHandleNull>
void ObjectTagTable::SweepImpl(art::IsMarkedVisitor* visitor) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);

  auto IsMarkedUpdater = [&](const art::GcRoot<art::mirror::Object>& original_root ATTRIBUTE_UNUSED,
                             art::mirror::Object* original_obj) {
    return visitor->IsMarked(original_obj);
  };

  UpdateTableWith<decltype(IsMarkedUpdater),
                  kHandleNull ? kCallHandleNull : kRemoveNull>(IsMarkedUpdater);
}

void ObjectTagTable::HandleNullSweep(jlong tag) {
  event_handler_->DispatchEvent(nullptr, JVMTI_EVENT_OBJECT_FREE, tag);
}

template <typename T, ObjectTagTable::TableUpdateNullTarget kTargetNull>
ALWAYS_INLINE inline void ObjectTagTable::UpdateTableWith(T& updater) {
  // We optimistically hope that elements will still be well-distributed when re-inserting them.
  // So play with the map mechanics, and postpone rehashing. This avoids the need of a side
  // vector and two passes.
  float original_max_load_factor = tagged_objects_.max_load_factor();
  tagged_objects_.max_load_factor(std::numeric_limits<float>::max());
  // For checking that a max load-factor actually does what we expect.
  size_t original_bucket_count = tagged_objects_.bucket_count();

  for (auto it = tagged_objects_.begin(); it != tagged_objects_.end();) {
    DCHECK(!it->first.IsNull());
    art::mirror::Object* original_obj = it->first.Read<art::kWithoutReadBarrier>();
    art::mirror::Object* target_obj = updater(it->first, original_obj);
    if (original_obj != target_obj) {
      if (kTargetNull == kIgnoreNull && target_obj == nullptr) {
        // Ignore null target, don't do anything.
      } else {
        jlong tag = it->second;
        it = tagged_objects_.erase(it);
        if (target_obj != nullptr) {
          tagged_objects_.emplace(art::GcRoot<art::mirror::Object>(target_obj), tag);
          DCHECK_EQ(original_bucket_count, tagged_objects_.bucket_count());
        } else if (kTargetNull == kCallHandleNull) {
          HandleNullSweep(tag);
        }
        continue;  // Iterator was implicitly updated by erase.
      }
    }
    it++;
  }

  tagged_objects_.max_load_factor(original_max_load_factor);
  // TODO: consider rehash here.
}

template <typename T, class Allocator = std::allocator<T>>
struct ReleasableContainer {
  using allocator_type = Allocator;

  explicit ReleasableContainer(const allocator_type& alloc, size_t reserve = 10)
      : allocator(alloc),
        data(reserve > 0 ? allocator.allocate(reserve) : nullptr),
        size(0),
        capacity(reserve) {
  }

  ~ReleasableContainer() {
    if (data != nullptr) {
      allocator.deallocate(data, capacity);
      capacity = 0;
      size = 0;
    }
  }

  T* Release() {
    T* tmp = data;

    data = nullptr;
    size = 0;
    capacity = 0;

    return tmp;
  }

  void Resize(size_t new_capacity) {
    CHECK_GT(new_capacity, capacity);

    T* tmp = allocator.allocate(new_capacity);
    DCHECK(tmp != nullptr);
    if (data != nullptr) {
      memcpy(tmp, data, sizeof(T) * size);
    }
    T* old = data;
    data = tmp;
    allocator.deallocate(old, capacity);
    capacity = new_capacity;
  }

  void Pushback(const T& elem) {
    if (size == capacity) {
      size_t new_capacity = 2 * capacity + 1;
      Resize(new_capacity);
    }
    data[size++] = elem;
  }

  Allocator allocator;
  T* data;
  size_t size;
  size_t capacity;
};

jvmtiError ObjectTagTable::GetTaggedObjects(jvmtiEnv* jvmti_env,
                                            jint tag_count,
                                            const jlong* tags,
                                            jint* count_ptr,
                                            jobject** object_result_ptr,
                                            jlong** tag_result_ptr) {
  if (tag_count < 0) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (tag_count > 0) {
    for (size_t i = 0; i != static_cast<size_t>(tag_count); ++i) {
      if (tags[i] == 0) {
        return ERR(ILLEGAL_ARGUMENT);
      }
    }
  }
  if (tags == nullptr) {
    return ERR(NULL_POINTER);
  }
  if (count_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);
  Wait(self);

  art::JNIEnvExt* jni_env = self->GetJniEnv();

  constexpr size_t kDefaultSize = 10;
  size_t initial_object_size;
  size_t initial_tag_size;
  if (tag_count == 0) {
    initial_object_size = (object_result_ptr != nullptr) ? tagged_objects_.size() : 0;
    initial_tag_size = (tag_result_ptr != nullptr) ? tagged_objects_.size() : 0;
  } else {
    initial_object_size = initial_tag_size = kDefaultSize;
  }
  JvmtiAllocator<void> allocator(jvmti_env);
  ReleasableContainer<jobject, JvmtiAllocator<jobject>> selected_objects(allocator, initial_object_size);
  ReleasableContainer<jlong, JvmtiAllocator<jlong>> selected_tags(allocator, initial_tag_size);

  size_t count = 0;
  for (auto& pair : tagged_objects_) {
    bool select;
    if (tag_count > 0) {
      select = false;
      for (size_t i = 0; i != static_cast<size_t>(tag_count); ++i) {
        if (tags[i] == pair.second) {
          select = true;
          break;
        }
      }
    } else {
      select = true;
    }

    if (select) {
      art::mirror::Object* obj = pair.first.Read<art::kWithReadBarrier>();
      if (obj != nullptr) {
        count++;
        if (object_result_ptr != nullptr) {
          selected_objects.Pushback(jni_env->AddLocalReference<jobject>(obj));
        }
        if (tag_result_ptr != nullptr) {
          selected_tags.Pushback(pair.second);
        }
      }
    }
  }

  if (object_result_ptr != nullptr) {
    *object_result_ptr = selected_objects.Release();
  }
  if (tag_result_ptr != nullptr) {
    *tag_result_ptr = selected_tags.Release();
  }
  *count_ptr = static_cast<jint>(count);
  return ERR(NONE);
}

}  // namespace openjdkjvmti
