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

#ifndef ART_RUNTIME_MIRROR_METHOD_HANDLE_IMPL_H_
#define ART_RUNTIME_MIRROR_METHOD_HANDLE_IMPL_H_

#include "class.h"
#include "gc_root.h"
#include "object.h"
#include "method_type.h"

namespace art {

struct MethodHandleImplOffsets;

namespace mirror {

// C++ mirror of java.lang.invoke.MethodHandle
class MANAGED MethodHandle : public Object {
 public:
  mirror::MethodType* GetMethodType() REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetFieldObject<mirror::MethodType>(OFFSET_OF_OBJECT_MEMBER(MethodHandle, method_type_));
  }

  ArtMethod* GetTargetMethod() REQUIRES_SHARED(Locks::mutator_lock_) {
    return reinterpret_cast<ArtMethod*>(
        GetField64(OFFSET_OF_OBJECT_MEMBER(MethodHandle, art_field_or_method_)));
  }

 private:
  HeapReference<mirror::Object> as_type_cache_;
  HeapReference<mirror::MethodType> method_type_;
  uint64_t art_field_or_method_;
  uint32_t handle_kind_;

 private:
  static MemberOffset AsTypeCacheOffset() {
    return MemberOffset(OFFSETOF_MEMBER(MethodHandle, as_type_cache_));
  }
  static MemberOffset MethodTypeOffset() {
    return MemberOffset(OFFSETOF_MEMBER(MethodHandle, method_type_));
  }
  static MemberOffset ArtFieldOrMethodOffset() {
    return MemberOffset(OFFSETOF_MEMBER(MethodHandle, art_field_or_method_));
  }
  static MemberOffset HandleKindOffset() {
    return MemberOffset(OFFSETOF_MEMBER(MethodHandle, handle_kind_));
  }

  friend struct art::MethodHandleImplOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(MethodHandle);
};

// C++ mirror of java.lang.invoke.MethodHandleImpl
class MANAGED MethodHandleImpl : public MethodHandle {
 public:
  static mirror::Class* StaticClass() REQUIRES_SHARED(Locks::mutator_lock_) {
    return static_class_.Read();
  }

  static void SetClass(Class* klass) REQUIRES_SHARED(Locks::mutator_lock_);
  static void ResetClass() REQUIRES_SHARED(Locks::mutator_lock_);
  static void VisitRoots(RootVisitor* visitor) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  static GcRoot<mirror::Class> static_class_;  // java.lang.invoke.MethodHandleImpl.class

  friend struct art::MethodHandleImplOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(MethodHandleImpl);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_METHOD_HANDLE_IMPL_H_
