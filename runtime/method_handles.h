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

#ifndef ART_RUNTIME_METHOD_HANDLES_H_
#define ART_RUNTIME_METHOD_HANDLES_H_

#include <ostream>

namespace art {

// Defines the behaviour of a given method handle. The behaviour
// of a handle of a given kind is identical to the dex bytecode behaviour
// of the equivalent instruction.
//
// NOTE: These must be kept in sync with the constants defined in
// java.lang.invoke.MethodHandle.
enum MethodHandleKind {
  kInvokeVirtual = 0,
  kInvokeSuper,
  kInvokeDirect,
  kInvokeStatic,
  kInvokeInterface,
  kInstanceGet,
  kInstancePut,
  kStaticGet,
  kStaticPut,
  kLastValidKind = kStaticPut,
  kLastInvokeKind = kInvokeInterface
};

// Whether the given method handle kind is some variant of an invoke.
inline bool IsInvoke(const MethodHandleKind handle_kind) {
  return handle_kind <= kLastInvokeKind;
}

}  // namespace art

#endif  // ART_RUNTIME_METHOD_HANDLES_H_
