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

#include "art_jvmti.h"
#include "base/logging.h"
#include "events-inl.h"
#include "gc/allocation_listener.h"
#include "instrumentation.h"
#include "jni_env_ext-inl.h"
#include "mirror/class.h"
#include "mirror/object.h"
#include "runtime.h"
#include "ScopedLocalRef.h"

namespace openjdkjvmti {

void ObjectTagTable::Add(art::mirror::Object* obj, jlong tag) {
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

bool ObjectTagTable::Remove(art::mirror::Object* obj, jlong* tag) {
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

void ObjectTagTable::Sweep(art::IsMarkedVisitor* visitor) {
  if (event_handler_->IsEventEnabledAnywhere(JVMTI_EVENT_OBJECT_FREE)) {
    SweepImpl<true>(visitor);
  } else {
    SweepImpl<false>(visitor);
  }
}

template <bool kHandleNull>
void ObjectTagTable::SweepImpl(art::IsMarkedVisitor* visitor) {
  art::Thread* self = art::Thread::Current();
  art::MutexLock mu(self, allow_disallow_lock_);

  for (auto it = tagged_objects_.begin(); it != tagged_objects_.end(); ++it) {
    if (!it->first.IsNull()) {
      art::mirror::Object* original_obj = it->first.Read<art::kWithoutReadBarrier>();
      art::mirror::Object* target_obj = visitor->IsMarked(original_obj);
      if (original_obj != target_obj) {
        it->first = art::GcRoot<art::mirror::Object>(target_obj);

        if (kHandleNull && target_obj == nullptr) {
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

void ObjectTagTable::HandleNullSweep(jlong tag) {
  event_handler_->DispatchEvent(nullptr, JVMTI_EVENT_OBJECT_FREE, tag);
}

}  // namespace openjdkjvmti
