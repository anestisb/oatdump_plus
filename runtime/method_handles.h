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
inline bool ConvertJValue(Handle<mirror::Class> from,
                          Handle<mirror::Class> to,
                          const JValue& from_value,
                          JValue* to_value) ALWAYS_INLINE;

// Perform argument conversions between |callsite_type| (the type of the
// incoming arguments) and |callee_type| (the type of the method being
// invoked). These include widening and narrowing conversions as well as
// boxing and unboxing. Returns true on success, on false on failure. A
// pending exception will always be set on failure.
//
// The values to be converted are read from an input source (of type G)
// that provides three methods :
//
// class G {
//   // Used to read the next boolean/short/int or float value from the
//   // source.
//   uint32_t Get();
//
//   // Used to the read the next reference value from the source.
//   ObjPtr<mirror::Object> GetReference();
//
//   // Used to read the next double or long value from the source.
//   int64_t GetLong();
// }
//
// After conversion, the values are written to an output sink (of type S)
// that provides three methods :
//
// class S {
//   void Set(uint32_t);
//   void SetReference(ObjPtr<mirror::Object>)
//   void SetLong(int64_t);
// }
//
// The semantics and usage of the Set methods are analagous to the getter
// class.
//
// This method is instantiated in three different scenarions :
// - <S = ShadowFrameSetter, G = ShadowFrameGetter> : copying from shadow
//   frame to shadow frame, used in a regular polymorphic non-exact invoke.
// - <S = EmulatedShadowFrameAccessor, G = ShadowFrameGetter> : entering into
//   a transformer method from a polymorphic invoke.
// - <S = ShadowFrameStter, G = EmulatedStackFrameAccessor> : entering into
//   a regular poly morphic invoke from a transformer method.
//
// TODO(narayan): If we find that the instantiations of this function take
// up too much space, we can make G / S abstract base classes that are
// overridden by concrete classes.
template <typename G, typename S>
REQUIRES_SHARED(Locks::mutator_lock_)
bool PerformConversions(Thread* self,
                        Handle<mirror::ObjectArray<mirror::Class>> from_types,
                        Handle<mirror::ObjectArray<mirror::Class>> to_types,
                        G* getter,
                        S* setter,
                        int32_t num_conversions);

// A convenience wrapper around |PerformConversions|, for the case where
// the setter and getter are both ShadowFrame based.
template <bool is_range> REQUIRES_SHARED(Locks::mutator_lock_)
bool ConvertAndCopyArgumentsFromCallerFrame(Thread* self,
                                            Handle<mirror::MethodType> callsite_type,
                                            Handle<mirror::MethodType> callee_type,
                                            const ShadowFrame& caller_frame,
                                            uint32_t first_src_reg,
                                            uint32_t first_dest_reg,
                                            const uint32_t (&arg)[Instruction::kMaxVarArgRegs],
                                            ShadowFrame* callee_frame);

// A convenience class that allows for iteration through a list of
// input argument registers |arg| for non-range invokes or a list of
// consecutive registers starting with a given based for range
// invokes.
//
// This is used to iterate over input arguments while performing standard
// argument conversions.
template <bool is_range> class ShadowFrameGetter {
 public:
  ShadowFrameGetter(size_t first_src_reg,
                    const uint32_t (&arg)[Instruction::kMaxVarArgRegs],
                    const ShadowFrame& shadow_frame) :
      first_src_reg_(first_src_reg),
      arg_(arg),
      shadow_frame_(shadow_frame),
      arg_index_(0) {
  }

  ALWAYS_INLINE uint32_t Get() REQUIRES_SHARED(Locks::mutator_lock_) {
    const uint32_t next = (is_range ? first_src_reg_ + arg_index_ : arg_[arg_index_]);
    ++arg_index_;

    return shadow_frame_.GetVReg(next);
  }

  ALWAYS_INLINE int64_t GetLong() REQUIRES_SHARED(Locks::mutator_lock_) {
    const uint32_t next = (is_range ? first_src_reg_ + arg_index_ : arg_[arg_index_]);
    arg_index_ += 2;

    return shadow_frame_.GetVRegLong(next);
  }

  ALWAYS_INLINE ObjPtr<mirror::Object> GetReference() REQUIRES_SHARED(Locks::mutator_lock_) {
    const uint32_t next = (is_range ? first_src_reg_ + arg_index_ : arg_[arg_index_]);
    ++arg_index_;

    return shadow_frame_.GetVRegReference(next);
  }

 private:
  const size_t first_src_reg_;
  const uint32_t (&arg_)[Instruction::kMaxVarArgRegs];
  const ShadowFrame& shadow_frame_;
  size_t arg_index_;
};

// A convenience class that allows values to be written to a given shadow frame,
// starting at location |first_dst_reg|.
class ShadowFrameSetter {
 public:
  ShadowFrameSetter(ShadowFrame* shadow_frame,
                    size_t first_dst_reg) :
    shadow_frame_(shadow_frame),
    arg_index_(first_dst_reg) {
  }

  ALWAYS_INLINE void Set(uint32_t value) REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame_->SetVReg(arg_index_++, value);
  }

  ALWAYS_INLINE void SetReference(ObjPtr<mirror::Object> value)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame_->SetVRegReference(arg_index_++, value.Ptr());
  }

  ALWAYS_INLINE void SetLong(int64_t value) REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame_->SetVRegLong(arg_index_, value);
    arg_index_ += 2;
  }

 private:
  ShadowFrame* shadow_frame_;
  size_t arg_index_;
};

}  // namespace art

#endif  // ART_RUNTIME_METHOD_HANDLES_H_
