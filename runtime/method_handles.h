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

#include "dex_instruction.h"
#include "jvalue.h"

namespace art {

namespace mirror {
  class MethodType;
}

class ShadowFrame;

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
  kInvokeTransform,
  kInstanceGet,
  kInstancePut,
  kStaticGet,
  kStaticPut,
  kLastValidKind = kStaticPut,
  kLastInvokeKind = kInvokeTransform
};

// Whether the given method handle kind is some variant of an invoke.
inline bool IsInvoke(const MethodHandleKind handle_kind) {
  return handle_kind <= kLastInvokeKind;
}

// Performs a single argument conversion from type |from| to a distinct
// type |to|. Returns true on success, false otherwise.
REQUIRES_SHARED(Locks::mutator_lock_)
bool ConvertJValue(Handle<mirror::Class> from,
                   Handle<mirror::Class> to,
                   const JValue& from_value,
                   JValue* to_value) ALWAYS_INLINE;

// Perform argument conversions between |callsite_type| (the type of the
// incoming arguments) and |callee_type| (the type of the method being
// invoked). These include widening and narrowing conversions as well as
// boxing and unboxing. Returns true on success, on false on failure. A
// pending exception will always be set on failure.
template <bool is_range> REQUIRES_SHARED(Locks::mutator_lock_)
bool ConvertAndCopyArgumentsFromCallerFrame(Thread* self,
                                            Handle<mirror::MethodType> callsite_type,
                                            Handle<mirror::MethodType> callee_type,
                                            const ShadowFrame& caller_frame,
                                            uint32_t first_src_reg,
                                            uint32_t first_dest_reg,
                                            const uint32_t (&arg)[Instruction::kMaxVarArgRegs],
                                            ShadowFrame* callee_frame);

// Similar to |ConvertAndCopyArgumentsFromCallerFrame|, except that the
// arguments are copied from an |EmulatedStackFrame|.
template <bool is_range> REQUIRES_SHARED(Locks::mutator_lock_)
bool ConvertAndCopyArgumentsFromEmulatedStackFrame(Thread* self,
                                                   ObjPtr<mirror::Object> emulated_stack_frame,
                                                   Handle<mirror::MethodType> callee_type,
                                                   const uint32_t first_dest_reg,
                                                   ShadowFrame* callee_frame);


}  // namespace art

#endif  // ART_RUNTIME_METHOD_HANDLES_H_
