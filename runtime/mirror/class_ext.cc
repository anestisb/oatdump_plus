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

#include "class_ext.h"

#include "art_method-inl.h"
#include "base/casts.h"
#include "base/enums.h"
#include "class-inl.h"
#include "dex_file-inl.h"
#include "gc/accounting/card_table-inl.h"
#include "object-inl.h"
#include "object_array.h"
#include "object_array-inl.h"
#include "stack_trace_element.h"
#include "utils.h"
#include "well_known_classes.h"

namespace art {
namespace mirror {

GcRoot<Class> ClassExt::dalvik_system_ClassExt_;

ClassExt* ClassExt::Alloc(Thread* self) {
  DCHECK(dalvik_system_ClassExt_.Read() != nullptr);
  return down_cast<ClassExt*>(dalvik_system_ClassExt_.Read()->AllocObject(self).Ptr());
}

void ClassExt::SetVerifyError(ObjPtr<Object> err) {
  if (Runtime::Current()->IsActiveTransaction()) {
    SetFieldObject<true>(OFFSET_OF_OBJECT_MEMBER(ClassExt, verify_error_), err);
  } else {
    SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(ClassExt, verify_error_), err);
  }
}

void ClassExt::SetClass(ObjPtr<Class> dalvik_system_ClassExt) {
  CHECK(dalvik_system_ClassExt != nullptr);
  dalvik_system_ClassExt_ = GcRoot<Class>(dalvik_system_ClassExt);
}

void ClassExt::ResetClass() {
  CHECK(!dalvik_system_ClassExt_.IsNull());
  dalvik_system_ClassExt_ = GcRoot<Class>(nullptr);
}

void ClassExt::VisitRoots(RootVisitor* visitor) {
  dalvik_system_ClassExt_.VisitRootIfNonNull(visitor, RootInfo(kRootStickyClass));
}

}  // namespace mirror
}  // namespace art
