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

#include "method_handles-inl.h"

#include "android-base/stringprintf.h"

#include "jvalue.h"
#include "jvalue-inl.h"
#include "reflection.h"
#include "reflection-inl.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

namespace {

#define PRIMITIVES_LIST(V) \
  V(Primitive::kPrimBoolean, Boolean, Boolean, Z) \
  V(Primitive::kPrimByte, Byte, Byte, B)          \
  V(Primitive::kPrimChar, Char, Character, C)     \
  V(Primitive::kPrimShort, Short, Short, S)       \
  V(Primitive::kPrimInt, Int, Integer, I)         \
  V(Primitive::kPrimLong, Long, Long, J)          \
  V(Primitive::kPrimFloat, Float, Float, F)       \
  V(Primitive::kPrimDouble, Double, Double, D)

// Assigns |type| to the primitive type associated with |klass|. Returns
// true iff. |klass| was a boxed type (Integer, Long etc.), false otherwise.
bool GetUnboxedPrimitiveType(ObjPtr<mirror::Class> klass, Primitive::Type* type)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension ants(__FUNCTION__);
#define LOOKUP_PRIMITIVE(primitive, _, __, ___)                         \
  if (klass->DescriptorEquals(Primitive::BoxedDescriptor(primitive))) { \
    *type = primitive;                                                  \
    return true;                                                        \
  }

  PRIMITIVES_LIST(LOOKUP_PRIMITIVE);
#undef LOOKUP_PRIMITIVE
  return false;
}

ObjPtr<mirror::Class> GetBoxedPrimitiveClass(Primitive::Type type)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension ants(__FUNCTION__);
  jmethodID m = nullptr;
  switch (type) {
#define CASE_PRIMITIVE(primitive, _, java_name, __)              \
    case primitive:                                              \
      m = WellKnownClasses::java_lang_ ## java_name ## _valueOf; \
      break;
    PRIMITIVES_LIST(CASE_PRIMITIVE);
#undef CASE_PRIMITIVE
    case Primitive::Type::kPrimNot:
    case Primitive::Type::kPrimVoid:
      return nullptr;
  }
  return jni::DecodeArtMethod(m)->GetDeclaringClass();
}

bool GetUnboxedTypeAndValue(ObjPtr<mirror::Object> o, Primitive::Type* type, JValue* value)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension ants(__FUNCTION__);
  ObjPtr<mirror::Class> klass = o->GetClass();
  ArtField* primitive_field = &klass->GetIFieldsPtr()->At(0);
#define CASE_PRIMITIVE(primitive, abbrev, _, shorthand)         \
  if (klass == GetBoxedPrimitiveClass(primitive)) {             \
    *type = primitive;                                          \
    value->Set ## shorthand(primitive_field->Get ## abbrev(o)); \
    return true;                                                \
  }
  PRIMITIVES_LIST(CASE_PRIMITIVE)
#undef CASE_PRIMITIVE
  return false;
}

inline bool IsReferenceType(Primitive::Type type) {
  return type == Primitive::kPrimNot;
}

inline bool IsPrimitiveType(Primitive::Type type) {
  return !IsReferenceType(type);
}

}  // namespace

bool IsParameterTypeConvertible(ObjPtr<mirror::Class> from, ObjPtr<mirror::Class> to)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // This function returns true if there's any conceivable conversion
  // between |from| and |to|. It's expected this method will be used
  // to determine if a WrongMethodTypeException should be raised. The
  // decision logic follows the documentation for MethodType.asType().
  if (from == to) {
    return true;
  }

  Primitive::Type from_primitive = from->GetPrimitiveType();
  Primitive::Type to_primitive = to->GetPrimitiveType();
  DCHECK(from_primitive != Primitive::Type::kPrimVoid);
  DCHECK(to_primitive != Primitive::Type::kPrimVoid);

  // If |to| and |from| are references.
  if (IsReferenceType(from_primitive) && IsReferenceType(to_primitive)) {
    // Assignability is determined during parameter conversion when
    // invoking the associated method handle.
    return true;
  }

  // If |to| and |from| are primitives and a widening conversion exists.
  if (Primitive::IsWidenable(from_primitive, to_primitive)) {
    return true;
  }

  // If |to| is a reference and |from| is a primitive, then boxing conversion.
  if (IsReferenceType(to_primitive) && IsPrimitiveType(from_primitive)) {
    return to->IsAssignableFrom(GetBoxedPrimitiveClass(from_primitive));
  }

  // If |from| is a reference and |to| is a primitive, then unboxing conversion.
  if (IsPrimitiveType(to_primitive) && IsReferenceType(from_primitive)) {
    if (from->DescriptorEquals("Ljava/lang/Object;")) {
      // Object might be converted into a primitive during unboxing.
      return true;
    } else if (Primitive::IsNumericType(to_primitive) &&
               from->DescriptorEquals("Ljava/lang/Number;")) {
      // Number might be unboxed into any of the number primitive types.
      return true;
    }
    Primitive::Type unboxed_type;
    if (GetUnboxedPrimitiveType(from, &unboxed_type)) {
      if (unboxed_type == to_primitive) {
        // Straightforward unboxing conversion such as Boolean => boolean.
        return true;
      } else {
        // Check if widening operations for numeric primitives would work,
        // such as Byte => byte => long.
        return Primitive::IsWidenable(unboxed_type, to_primitive);
      }
    }
  }

  return false;
}

bool IsReturnTypeConvertible(ObjPtr<mirror::Class> from, ObjPtr<mirror::Class> to)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (to->GetPrimitiveType() == Primitive::Type::kPrimVoid) {
    // Result will be ignored.
    return true;
  } else if (from->GetPrimitiveType() == Primitive::Type::kPrimVoid) {
    // Returned value will be 0 / null.
    return true;
  } else {
    // Otherwise apply usual parameter conversion rules.
    return IsParameterTypeConvertible(from, to);
  }
}

bool ConvertJValueCommon(
    Handle<mirror::MethodType> callsite_type,
    Handle<mirror::MethodType> callee_type,
    ObjPtr<mirror::Class> from,
    ObjPtr<mirror::Class> to,
    JValue* value) {
  // The reader maybe concerned about the safety of the heap object
  // that may be in |value|. There is only one case where allocation
  // is obviously needed and that's for boxing. However, in the case
  // of boxing |value| contains a non-reference type.

  const Primitive::Type from_type = from->GetPrimitiveType();
  const Primitive::Type to_type = to->GetPrimitiveType();

  // Put incoming value into |src_value| and set return value to 0.
  // Errors and conversions from void require the return value to be 0.
  const JValue src_value(*value);
  value->SetJ(0);

  // Conversion from void set result to zero.
  if (from_type == Primitive::kPrimVoid) {
    return true;
  }

  // This method must be called only when the types don't match.
  DCHECK(from != to);

  if (IsPrimitiveType(from_type) && IsPrimitiveType(to_type)) {
    // The source and target types are both primitives.
    if (UNLIKELY(!ConvertPrimitiveValueNoThrow(from_type, to_type, src_value, value))) {
      ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
      return false;
    }
    return true;
  } else if (IsReferenceType(from_type) && IsReferenceType(to_type)) {
    // They're both reference types. If "from" is null, we can pass it
    // through unchanged. If not, we must generate a cast exception if
    // |to| is not assignable from the dynamic type of |ref|.
    //
    // Playing it safe with StackHandleScope here, not expecting any allocation
    // in mirror::Class::IsAssignable().
    StackHandleScope<2> hs(Thread::Current());
    Handle<mirror::Class> h_to(hs.NewHandle(to));
    Handle<mirror::Object> h_obj(hs.NewHandle(src_value.GetL()));
    if (h_obj.Get() != nullptr && !to->IsAssignableFrom(h_obj->GetClass())) {
      ThrowClassCastException(h_to.Get(), h_obj->GetClass());
      return false;
    }
    value->SetL(h_obj.Get());
    return true;
  } else if (IsReferenceType(to_type)) {
    DCHECK(IsPrimitiveType(from_type));
    // The source type is a primitive and the target type is a reference, so we must box.
    // The target type maybe a super class of the boxed source type, for example,
    // if the source type is int, it's boxed type is java.lang.Integer, and the target
    // type could be java.lang.Number.
    Primitive::Type type;
    if (!GetUnboxedPrimitiveType(to, &type)) {
      ObjPtr<mirror::Class> boxed_from_class = GetBoxedPrimitiveClass(from_type);
      if (boxed_from_class->IsSubClass(to)) {
        type = from_type;
      } else {
        ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
        return false;
      }
    }

    if (UNLIKELY(from_type != type)) {
      ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
      return false;
    }

    if (!ConvertPrimitiveValueNoThrow(from_type, type, src_value, value)) {
      ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
      return false;
    }

    // Then perform the actual boxing, and then set the reference.
    ObjPtr<mirror::Object> boxed = BoxPrimitive(type, src_value);
    value->SetL(boxed.Ptr());
    return true;
  } else {
    // The source type is a reference and the target type is a primitive, so we must unbox.
    DCHECK(IsReferenceType(from_type));
    DCHECK(IsPrimitiveType(to_type));

    ObjPtr<mirror::Object> from_obj(src_value.GetL());
    if (UNLIKELY(from_obj == nullptr)) {
      ThrowNullPointerException(
          StringPrintf("Expected to unbox a '%s' primitive type but was returned null",
                       from->PrettyDescriptor().c_str()).c_str());
      return false;
    }

    Primitive::Type unboxed_type;
    JValue unboxed_value;
    if (UNLIKELY(!GetUnboxedTypeAndValue(from_obj, &unboxed_type, &unboxed_value))) {
      ThrowWrongMethodTypeException(callee_type.Get(), callsite_type.Get());
      return false;
    }

    if (UNLIKELY(!ConvertPrimitiveValueNoThrow(unboxed_type, to_type, unboxed_value, value))) {
      ThrowClassCastException(from, to);
      return false;
    }

    return true;
  }
}

}  // namespace art
