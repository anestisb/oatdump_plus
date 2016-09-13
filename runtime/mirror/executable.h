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

#ifndef ART_RUNTIME_MIRROR_EXECUTABLE_H_
#define ART_RUNTIME_MIRROR_EXECUTABLE_H_

#include "accessible_object.h"
#include "gc_root.h"
#include "object.h"
#include "object_callbacks.h"
#include "read_barrier_option.h"

namespace art {

struct ExecutableOffsets;
class ArtMethod;

namespace mirror {

// C++ mirror of java.lang.reflect.Executable.
class MANAGED Executable : public AccessibleObject {
 private:
  uint16_t has_real_parameter_data_;
  HeapReference<mirror::Array> parameters_;

  friend struct art::ExecutableOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(Executable);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_EXECUTABLE_H_
