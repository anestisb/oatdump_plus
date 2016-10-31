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

REQUIRES_SHARED(Locks::mutator_lock_)
inline bool ConvertJValue(Handle<mirror::Class> from,
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

template <typename G, typename S>
bool PerformConversions(Thread* self,
                        Handle<mirror::ObjectArray<mirror::Class>> from_types,
                        Handle<mirror::ObjectArray<mirror::Class>> to_types,
                        G* getter,
                        S* setter,
                        int32_t num_conversions) {
  StackHandleScope<2> hs(self);
  MutableHandle<mirror::Class> from(hs.NewHandle<mirror::Class>(nullptr));
  MutableHandle<mirror::Class> to(hs.NewHandle<mirror::Class>(nullptr));

  for (int32_t i = 0; i < num_conversions; ++i) {
    from.Assign(from_types->GetWithoutChecks(i));
    to.Assign(to_types->GetWithoutChecks(i));

    const Primitive::Type from_type = from->GetPrimitiveType();
    const Primitive::Type to_type = to->GetPrimitiveType();

    if (from.Get() == to.Get()) {
      // Easy case - the types are identical. Nothing left to do except to pass
      // the arguments along verbatim.
      if (Primitive::Is64BitType(from_type)) {
        setter->SetLong(getter->GetLong());
      } else if (from_type == Primitive::kPrimNot) {
        setter->SetReference(getter->GetReference());
      } else {
        setter->Set(getter->Get());
      }

      continue;
    } else {
      JValue from_value;
      JValue to_value;

      if (Primitive::Is64BitType(from_type)) {
        from_value.SetJ(getter->GetLong());
      } else if (from_type == Primitive::kPrimNot) {
        from_value.SetL(getter->GetReference());
      } else {
        from_value.SetI(getter->Get());
      }

      if (!ConvertJValue(from, to, from_value, &to_value)) {
        DCHECK(self->IsExceptionPending());
        return false;
      }

      if (Primitive::Is64BitType(to_type)) {
        setter->SetLong(to_value.GetJ());
      } else if (to_type == Primitive::kPrimNot) {
        setter->SetReference(to_value.GetL());
      } else {
        setter->Set(to_value.GetI());
      }
    }
  }

  return true;
}

template <bool is_range>
bool ConvertAndCopyArgumentsFromCallerFrame(Thread* self,
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

  ShadowFrameGetter<is_range> getter(first_src_reg, arg, caller_frame);
  ShadowFrameSetter setter(callee_frame, first_dest_reg);

  return PerformConversions<ShadowFrameGetter<is_range>, ShadowFrameSetter>(self,
                                                                            from_types,
                                                                            to_types,
                                                                            &getter,
                                                                            &setter,
                                                                            num_method_params);
}

}  // namespace art

#endif  // ART_RUNTIME_METHOD_HANDLES_INL_H_
