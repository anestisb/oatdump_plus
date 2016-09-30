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

#ifndef ART_RUNTIME_OBJ_PTR_INL_H_
#define ART_RUNTIME_OBJ_PTR_INL_H_

#include "obj_ptr.h"
#include "thread-inl.h"

namespace art {

template<class MirrorType, bool kPoison>
inline bool ObjPtr<MirrorType, kPoison>::IsValid() const {
  if (!kPoison || IsNull()) {
    return true;
  }
  return GetCookie() == TrimCookie(Thread::Current()->GetPoisonObjectCookie());
}

template<class MirrorType, bool kPoison>
inline void ObjPtr<MirrorType, kPoison>::AssertValid() const {
  if (kPoison) {
    CHECK(IsValid()) << "Stale object pointer " << DecodeUnchecked() << " , expected cookie "
        << TrimCookie(Thread::Current()->GetPoisonObjectCookie()) << " but got " << GetCookie();
  }
}

template<class MirrorType, bool kPoison>
inline uintptr_t ObjPtr<MirrorType, kPoison>::Encode(MirrorType* ptr) {
  uintptr_t ref = reinterpret_cast<uintptr_t>(ptr);
  DCHECK_ALIGNED(ref, kObjectAlignment);
  if (kPoison && ref != 0) {
    DCHECK_LE(ref, 0xFFFFFFFFU);
    ref >>= kObjectAlignmentShift;
    // Put cookie in high bits.
    Thread* self = Thread::Current();
    DCHECK(self != nullptr);
    ref |= self->GetPoisonObjectCookie() << kCookieShift;
  }
  return ref;
}

template<class MirrorType, bool kPoison>
inline std::ostream& operator<<(std::ostream& os, ObjPtr<MirrorType, kPoison> ptr) {
  // May be used for dumping bad pointers, do not use the checked version.
  return os << ptr.DecodeUnchecked();
}

}  // namespace art

#endif  // ART_RUNTIME_OBJ_PTR_INL_H_
