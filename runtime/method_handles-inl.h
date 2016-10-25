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

#ifndef ART_RUNTIME_METHOD_HANDLES_INL_H_
#define ART_RUNTIME_METHOD_HANDLES_INL_H_

#include "method_handles.h"

#include "common_throws.h"
#include "dex_instruction.h"
#include "interpreter/interpreter_common.h"
#include "jvalue.h"
#include "mirror/class.h"
#include "mirror/method_type.h"
#include "mirror/object.h"
#include "reflection.h"
#include "stack.h"

namespace art {

// Assigns |type| to the primitive type associated with |dst_class|. Returns
// true iff. |dst_class| was a boxed type (Integer, Long etc.), false otherwise.
REQUIRES_SHARED(Locks::mutator_lock_)
static inline bool GetPrimitiveType(ObjPtr<mirror::Class> dst_class, Primitive::Type* type) {
  if (dst_class->DescriptorEquals("Ljava/lang/Boolean;")) {
    (*type) = Primitive::kPrimBoolean;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Byte;")) {
    (*type) = Primitive::kPrimByte;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Character;")) {
    (*type) = Primitive::kPrimChar;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Float;")) {
    (*type) = Primitive::kPrimFloat;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Double;")) {
    (*type) = Primitive::kPrimDouble;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Integer;")) {
    (*type) = Primitive::kPrimInt;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Long;")) {
    (*type) = Primitive::kPrimLong;
    return true;
  } else if (dst_class->DescriptorEquals("Ljava/lang/Short;")) {
    (*type) = Primitive::kPrimShort;
    return true;
  } else {
    return false;
  }
}

// A convenience class that allows for iteration through a list of
// input argument registers |arg| for non-range invokes or a list of
// consecutive registers starting with a given based for range
// invokes.
template <bool is_range> class ArgIterator {
 public:
  ArgIterator(size_t first_src_reg,
              const uint32_t (&arg)[Instruction::kMaxVarArgRegs]) :
      first_src_reg_(first_src_reg),
      arg_(arg),
      arg_index_(0) {
  }

  uint32_t Next() {
    const uint32_t next = (is_range ? first_src_reg_ + arg_index_ : arg_[arg_index_]);
    ++arg_index_;

    return next;
  }

  uint32_t NextPair() {
    const uint32_t next = (is_range ? first_src_reg_ + arg_index_ : arg_[arg_index_]);
    arg_index_ += 2;

    return next;
  }

 private:
  const size_t first_src_reg_;
  const uint32_t (&arg_)[Instruction::kMaxVarArgRegs];
  size_t arg_index_;
};

REQUIRES_SHARED(Locks::mutator_lock_)
bool ConvertJValue(Handle<mirror::Class> from,
                   Handle<mirror::Class> to,
                   const JValue& from_value,
                   JValue* to_value) {
  const Primitive::Type from_type = from->GetPrimitiveType();
  const Primitive::Type to_type = to->GetPrimitiveType();

  // This method must be called only when the types don't match.
  DCHECK(from.Get() != to.Get());

  if ((from_type != Primitive::kPrimNot) && (to_type != Primitive::kPrimNot)) {
    // Throws a ClassCastException if we're unable to convert a primitive value.
    return ConvertPrimitiveValue(false, from_type, to_type, from_value, to_value);
  } else if ((from_type == Primitive::kPrimNot) && (to_type == Primitive::kPrimNot)) {
    // They're both reference types. If "from" is null, we can pass it
    // through unchanged. If not, we must generate a cast exception if
    // |to| is not assignable from the dynamic type of |ref|.
    mirror::Object* const ref = from_value.GetL();
    if (ref == nullptr || to->IsAssignableFrom(ref->GetClass())) {
      to_value->SetL(ref);
      return true;
    } else {
      ThrowClassCastException(to.Get(), ref->GetClass());
      return false;
    }
  } else {
    // Precisely one of the source or the destination are reference types.
    // We must box or unbox.
    if (to_type == Primitive::kPrimNot) {
      // The target type is a reference, we must box.
      Primitive::Type type;
      // TODO(narayan): This is a CHECK for now. There might be a few corner cases
      // here that we might not have handled yet. For exmple, if |to| is java/lang/Number;,
      // we will need to box this "naturally".
      CHECK(GetPrimitiveType(to.Get(), &type));
      // First perform a primitive conversion to the unboxed equivalent of the target,
      // if necessary. This should be for the rarer cases like (int->Long) etc.
      if (UNLIKELY(from_type != type)) {
         if (!ConvertPrimitiveValue(false, from_type, type, from_value, to_value)) {
           return false;
         }
      } else {
        *to_value = from_value;
      }

      // Then perform the actual boxing, and then set the reference.
      ObjPtr<mirror::Object> boxed = BoxPrimitive(type, from_value);
      to_value->SetL(boxed.Ptr());
      return true;
    } else {
      // The target type is a primitive, we must unbox.
      ObjPtr<mirror::Object> ref(from_value.GetL());

      // Note that UnboxPrimitiveForResult already performs all of the type
      // conversions that we want, based on |to|.
      JValue unboxed_value;
      return UnboxPrimitiveForResult(ref, to.Get(), to_value);
    }
  }

  return true;
}

template <bool is_range>
bool PerformArgumentConversions(Thread* self,
                                Handle<mirror::MethodType> callsite_type,
                                Handle<mirror::MethodType> callee_type,
                                const ShadowFrame& caller_frame,
                                uint32_t first_src_reg,
                                uint32_t first_dest_reg,
                                const uint32_t (&arg)[Instruction::kMaxVarArgRegs],
                                ShadowFrame* callee_frame) {
  StackHandleScope<4> hs(self);
  Handle<mirror::ObjectArray<mirror::Class>> from_types(hs.NewHandle(callsite_type->GetPTypes()));
  Handle<mirror::ObjectArray<mirror::Class>> to_types(hs.NewHandle(callee_type->GetPTypes()));

  const int32_t num_method_params = from_types->GetLength();
  if (to_types->GetLength() != num_method_params) {
    ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
    return false;
  }

  ArgIterator<is_range> input_args(first_src_reg, arg);
  size_t to_arg_index = 0;
  MutableHandle<mirror::Class> from(hs.NewHandle<mirror::Class>(nullptr));
  MutableHandle<mirror::Class> to(hs.NewHandle<mirror::Class>(nullptr));
  for (int32_t i = 0; i < num_method_params; ++i) {
    from.Assign(from_types->GetWithoutChecks(i));
    to.Assign(to_types->GetWithoutChecks(i));

    const Primitive::Type from_type = from->GetPrimitiveType();
    const Primitive::Type to_type = to->GetPrimitiveType();

    // Easy case - the types are identical. Nothing left to do except to pass
    // the arguments along verbatim.
    if (from.Get() == to.Get()) {
      interpreter::AssignRegister(callee_frame,
                                  caller_frame,
                                  first_dest_reg + to_arg_index,
                                  input_args.Next());
      ++to_arg_index;

      // This is a wide argument, we must use the second half of the register
      // pair as well.
      if (Primitive::Is64BitType(from_type)) {
        interpreter::AssignRegister(callee_frame,
                                    caller_frame,
                                    first_dest_reg + to_arg_index,
                                    input_args.Next());
        ++to_arg_index;
      }

      continue;
    } else {
      JValue from_value;
      JValue to_value;

      if (Primitive::Is64BitType(from_type)) {
        from_value.SetJ(caller_frame.GetVRegLong(input_args.NextPair()));
      } else if (from_type == Primitive::kPrimNot) {
        from_value.SetL(caller_frame.GetVRegReference(input_args.Next()));
      } else {
        from_value.SetI(caller_frame.GetVReg(input_args.Next()));
      }

      if (!ConvertJValue(from, to, from_value, &to_value)) {
        DCHECK(self->IsExceptionPending());
        return false;
      }

      if (Primitive::Is64BitType(to_type)) {
        callee_frame->SetVRegLong(first_dest_reg + to_arg_index, to_value.GetJ());
        to_arg_index += 2;
      } else if (to_type == Primitive::kPrimNot) {
        callee_frame->SetVRegReference(first_dest_reg + to_arg_index, to_value.GetL());
        ++to_arg_index;
      } else {
        callee_frame->SetVReg(first_dest_reg + to_arg_index, to_value.GetI());
        ++to_arg_index;
      }
    }
  }

  return true;
}

}  // namespace art

#endif  // ART_RUNTIME_METHOD_HANDLES_INL_H_
