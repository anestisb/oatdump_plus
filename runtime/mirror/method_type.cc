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

#include "method_type.h"

#include "class-inl.h"
#include "gc_root-inl.h"
#include "method_handles.h"

namespace art {
namespace mirror {

GcRoot<mirror::Class> MethodType::static_class_;

mirror::MethodType* MethodType::Create(Thread* const self,
                                       Handle<Class> return_type,
                                       Handle<ObjectArray<Class>> param_types) {
  StackHandleScope<1> hs(self);
  Handle<mirror::MethodType> mt(
      hs.NewHandle(ObjPtr<MethodType>::DownCast(StaticClass()->AllocObject(self))));

  // TODO: Do we ever create a MethodType during a transaction ? There doesn't
  // seem like a good reason to do a polymorphic invoke that results in the
  // resolution of a method type in an unstarted runtime.
  mt->SetFieldObject<false>(FormOffset(), nullptr);
  mt->SetFieldObject<false>(MethodDescriptorOffset(), nullptr);
  mt->SetFieldObject<false>(RTypeOffset(), return_type.Get());
  mt->SetFieldObject<false>(PTypesOffset(), param_types.Get());
  mt->SetFieldObject<false>(WrapAltOffset(), nullptr);

  return mt.Get();
}

size_t MethodType::NumberOfVRegs() REQUIRES_SHARED(Locks::mutator_lock_) {
  mirror::ObjectArray<Class>* const p_types = GetPTypes();
  const int32_t p_types_length = p_types->GetLength();

  // Initialize |num_vregs| with number of parameters and only increment it for
  // types requiring a second vreg.
  size_t num_vregs = static_cast<size_t>(p_types_length);
  for (int32_t i = 0; i < p_types_length; ++i) {
    mirror::Class* klass = p_types->GetWithoutChecks(i);
    if (klass->IsPrimitiveLong() || klass->IsPrimitiveDouble()) {
      ++num_vregs;
    }
  }
  return num_vregs;
}

bool MethodType::IsExactMatch(mirror::MethodType* target) REQUIRES_SHARED(Locks::mutator_lock_) {
  mirror::ObjectArray<Class>* const p_types = GetPTypes();
  const int32_t params_length = p_types->GetLength();

  mirror::ObjectArray<Class>* const target_p_types = target->GetPTypes();
  if (params_length != target_p_types->GetLength()) {
    return false;
  }
  for (int32_t i = 0; i < params_length; ++i) {
    if (p_types->GetWithoutChecks(i) != target_p_types->GetWithoutChecks(i)) {
      return false;
    }
  }
  return GetRType() == target->GetRType();
}

bool MethodType::IsConvertible(mirror::MethodType* target) REQUIRES_SHARED(Locks::mutator_lock_) {
  mirror::ObjectArray<Class>* const p_types = GetPTypes();
  const int32_t params_length = p_types->GetLength();

  mirror::ObjectArray<Class>* const target_p_types = target->GetPTypes();
  if (params_length != target_p_types->GetLength()) {
    return false;
  }

  // Perform return check before invoking method handle otherwise side
  // effects from the invocation may be observable before
  // WrongMethodTypeException is raised.
  if (!IsReturnTypeConvertible(target->GetRType(), GetRType())) {
    return false;
  }

  for (int32_t i = 0; i < params_length; ++i) {
    if (!IsParameterTypeConvertible(p_types->GetWithoutChecks(i),
                                    target_p_types->GetWithoutChecks(i))) {
      return false;
    }
  }
  return true;
}

std::string MethodType::PrettyDescriptor() REQUIRES_SHARED(Locks::mutator_lock_) {
  std::ostringstream ss;
  ss << "(";

  mirror::ObjectArray<Class>* const p_types = GetPTypes();
  const int32_t params_length = p_types->GetLength();
  for (int32_t i = 0; i < params_length; ++i) {
    ss << p_types->GetWithoutChecks(i)->PrettyDescriptor();
    if (i != (params_length - 1)) {
      ss << ", ";
    }
  }

  ss << ")";
  ss << GetRType()->PrettyDescriptor();

  return ss.str();
}

void MethodType::SetClass(Class* klass) {
  CHECK(static_class_.IsNull()) << static_class_.Read() << " " << klass;
  CHECK(klass != nullptr);
  static_class_ = GcRoot<Class>(klass);
}

void MethodType::ResetClass() {
  CHECK(!static_class_.IsNull());
  static_class_ = GcRoot<Class>(nullptr);
}

void MethodType::VisitRoots(RootVisitor* visitor) {
  static_class_.VisitRootIfNonNull(visitor, RootInfo(kRootStickyClass));
}

}  // namespace mirror
}  // namespace art
