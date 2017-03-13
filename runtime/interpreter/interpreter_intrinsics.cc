/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "interpreter/interpreter_common.h"
#include "interpreter/interpreter_intrinsics.h"

namespace art {
namespace interpreter {

#define BINARY_SIMPLE_INTRINSIC(name, op, get, set, offset)  \
static ALWAYS_INLINE bool name(ShadowFrame* shadow_frame,    \
                               const Instruction* inst,      \
                               uint16_t inst_data,           \
                               JValue* result_register)      \
    REQUIRES_SHARED(Locks::mutator_lock_) {                  \
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};            \
  inst->GetVarArgs(arg, inst_data);                          \
  result_register->set(op(shadow_frame->get(arg[0]), shadow_frame->get(arg[offset]))); \
  return true;                                               \
}

#define UNARY_SIMPLE_INTRINSIC(name, op, get, set)           \
static ALWAYS_INLINE bool name(ShadowFrame* shadow_frame,    \
                               const Instruction* inst,      \
                               uint16_t inst_data,           \
                               JValue* result_register)      \
    REQUIRES_SHARED(Locks::mutator_lock_) {                  \
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};            \
  inst->GetVarArgs(arg, inst_data);                          \
  result_register->set(op(shadow_frame->get(arg[0])));       \
  return true;                                               \
}

// java.lang.Math.min(II)I
BINARY_SIMPLE_INTRINSIC(MterpMathMinIntInt, std::min, GetVReg, SetI, 1);
// java.lang.Math.min(JJ)J
BINARY_SIMPLE_INTRINSIC(MterpMathMinLongLong, std::min, GetVRegLong, SetJ, 2);
// java.lang.Math.max(II)I
BINARY_SIMPLE_INTRINSIC(MterpMathMaxIntInt, std::max, GetVReg, SetI, 1);
// java.lang.Math.max(JJ)J
BINARY_SIMPLE_INTRINSIC(MterpMathMaxLongLong, std::max, GetVRegLong, SetJ, 2);
// java.lang.Math.abs(I)I
UNARY_SIMPLE_INTRINSIC(MterpMathAbsInt, std::abs, GetVReg, SetI);
// java.lang.Math.abs(J)J
UNARY_SIMPLE_INTRINSIC(MterpMathAbsLong, std::abs, GetVRegLong, SetJ);
// java.lang.Math.abs(F)F
UNARY_SIMPLE_INTRINSIC(MterpMathAbsFloat, 0x7fffffff&, GetVReg, SetI);
// java.lang.Math.abs(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathAbsDouble, INT64_C(0x7fffffffffffffff)&, GetVRegLong, SetJ);
// java.lang.Math.sqrt(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathSqrt, std::sqrt, GetVRegDouble, SetD);
// java.lang.Math.ceil(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathCeil, std::ceil, GetVRegDouble, SetD);
// java.lang.Math.floor(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathFloor, std::floor, GetVRegDouble, SetD);
// java.lang.Math.sin(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathSin, std::sin, GetVRegDouble, SetD);
// java.lang.Math.cos(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathCos, std::cos, GetVRegDouble, SetD);
// java.lang.Math.tan(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathTan, std::tan, GetVRegDouble, SetD);
// java.lang.Math.asin(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathAsin, std::asin, GetVRegDouble, SetD);
// java.lang.Math.acos(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathAcos, std::acos, GetVRegDouble, SetD);
// java.lang.Math.atan(D)D
UNARY_SIMPLE_INTRINSIC(MterpMathAtan, std::atan, GetVRegDouble, SetD);

// java.lang.String.charAt(I)C
static ALWAYS_INLINE bool MterpStringCharAt(ShadowFrame* shadow_frame,
                                            const Instruction* inst,
                                            uint16_t inst_data,
                                            JValue* result_register)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};
  inst->GetVarArgs(arg, inst_data);
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString();
  int length = str->GetLength();
  int index = shadow_frame->GetVReg(arg[1]);
  uint16_t res;
  if (UNLIKELY(index < 0) || (index >= length)) {
    return false;  // Punt and let non-intrinsic version deal with the throw.
  }
  if (str->IsCompressed()) {
    res = str->GetValueCompressed()[index];
  } else {
    res = str->GetValue()[index];
  }
  result_register->SetC(res);
  return true;
}

// java.lang.String.compareTo(Ljava/lang/string)I
static ALWAYS_INLINE bool MterpStringCompareTo(ShadowFrame* shadow_frame,
                                               const Instruction* inst,
                                               uint16_t inst_data,
                                               JValue* result_register)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};
  inst->GetVarArgs(arg, inst_data);
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString();
  mirror::Object* arg1 = shadow_frame->GetVRegReference(arg[1]);
  if (arg1 == nullptr) {
    return false;
  }
  result_register->SetI(str->CompareTo(arg1->AsString()));
  return true;
}

#define STRING_INDEXOF_INTRINSIC(name, starting_pos)             \
static ALWAYS_INLINE bool Mterp##name(ShadowFrame* shadow_frame, \
                                      const Instruction* inst,   \
                                      uint16_t inst_data,        \
                                      JValue* result_register)   \
    REQUIRES_SHARED(Locks::mutator_lock_) {                      \
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};                \
  inst->GetVarArgs(arg, inst_data);                              \
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString(); \
  int ch = shadow_frame->GetVReg(arg[1]);                        \
  if (ch >= 0x10000) {                                           \
    /* Punt if supplementary char. */                            \
    return false;                                                \
  }                                                              \
  result_register->SetI(str->FastIndexOf(ch, starting_pos));     \
  return true;                                                   \
}

// java.lang.String.indexOf(I)I
STRING_INDEXOF_INTRINSIC(StringIndexOf, 0);

// java.lang.String.indexOf(II)I
STRING_INDEXOF_INTRINSIC(StringIndexOfAfter, shadow_frame->GetVReg(arg[2]));

#define SIMPLE_STRING_INTRINSIC(name, operation)                 \
static ALWAYS_INLINE bool Mterp##name(ShadowFrame* shadow_frame, \
                                      const Instruction* inst,   \
                                      uint16_t inst_data,        \
                                      JValue* result_register)   \
    REQUIRES_SHARED(Locks::mutator_lock_) {                      \
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};                \
  inst->GetVarArgs(arg, inst_data);                              \
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString(); \
  result_register->operation;                                    \
  return true;                                                   \
}

// java.lang.String.isEmpty()Z
SIMPLE_STRING_INTRINSIC(StringIsEmpty, SetZ(str->GetLength() == 0))

// java.lang.String.length()I
SIMPLE_STRING_INTRINSIC(StringLength, SetI(str->GetLength()))

// java.lang.String.getCharsNoCheck(II[CI)V
static ALWAYS_INLINE bool MterpStringGetCharsNoCheck(ShadowFrame* shadow_frame,
                                                     const Instruction* inst,
                                                     uint16_t inst_data,
                                                     JValue* result_register ATTRIBUTE_UNUSED)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Start, end & index already checked by caller - won't throw.  Destination is uncompressed.
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};
  inst->GetVarArgs(arg, inst_data);
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString();
  int32_t start = shadow_frame->GetVReg(arg[1]);
  int32_t end = shadow_frame->GetVReg(arg[2]);
  int32_t index = shadow_frame->GetVReg(arg[4]);
  mirror::CharArray* array = shadow_frame->GetVRegReference(arg[3])->AsCharArray();
  uint16_t* dst = array->GetData() + index;
  int32_t len = (end - start);
  if (str->IsCompressed()) {
    const uint8_t* src_8 = str->GetValueCompressed() + start;
    for (int i = 0; i < len; i++) {
      dst[i] = src_8[i];
    }
  } else {
    uint16_t* src_16 = str->GetValue() + start;
    memcpy(dst, src_16, len * sizeof(uint16_t));
  }
  return true;
}

// java.lang.String.equalsLjava/lang/Object;)Z
static ALWAYS_INLINE bool MterpStringEquals(ShadowFrame* shadow_frame,
                                            const Instruction* inst,
                                            uint16_t inst_data,
                                            JValue* result_register)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  uint32_t arg[Instruction::kMaxVarArgRegs] = {};
  inst->GetVarArgs(arg, inst_data);
  mirror::String* str = shadow_frame->GetVRegReference(arg[0])->AsString();
  mirror::Object* obj = shadow_frame->GetVRegReference(arg[1]);
  bool res = false;  // Assume not equal.
  if ((obj != nullptr) && obj->IsString()) {
    mirror::String* str2 = obj->AsString();
    if (str->GetCount() == str2->GetCount()) {
      // Length & compression status are same.  Can use block compare.
      void* bytes1;
      void* bytes2;
      int len = str->GetLength();
      if (str->IsCompressed()) {
        bytes1 = str->GetValueCompressed();
        bytes2 = str2->GetValueCompressed();
      } else {
        len *= sizeof(uint16_t);
        bytes1 = str->GetValue();
        bytes2 = str2->GetValue();
      }
      res = (memcmp(bytes1, bytes2, len) == 0);
    }
  }
  result_register->SetZ(res);
  return true;
}

#define INTRINSIC_CASE(name)                                           \
    case Intrinsics::k##name:                                          \
      res = Mterp##name(shadow_frame, inst, inst_data, result_register); \
      break;

bool MterpHandleIntrinsic(ShadowFrame* shadow_frame,
                          ArtMethod* const called_method,
                          const Instruction* inst,
                          uint16_t inst_data,
                          JValue* result_register)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  Intrinsics intrinsic = static_cast<Intrinsics>(called_method->GetIntrinsic());
  bool res = false;  // Assume failure
  switch (intrinsic) {
    INTRINSIC_CASE(MathMinIntInt)
    INTRINSIC_CASE(MathMinLongLong)
    INTRINSIC_CASE(MathMaxIntInt)
    INTRINSIC_CASE(MathMaxLongLong)
    INTRINSIC_CASE(MathAbsInt)
    INTRINSIC_CASE(MathAbsLong)
    INTRINSIC_CASE(MathAbsFloat)
    INTRINSIC_CASE(MathAbsDouble)
    INTRINSIC_CASE(MathSqrt)
    INTRINSIC_CASE(MathCeil)
    INTRINSIC_CASE(MathFloor)
    INTRINSIC_CASE(MathSin)
    INTRINSIC_CASE(MathCos)
    INTRINSIC_CASE(MathTan)
    INTRINSIC_CASE(MathAsin)
    INTRINSIC_CASE(MathAcos)
    INTRINSIC_CASE(MathAtan)
    INTRINSIC_CASE(StringCharAt)
    INTRINSIC_CASE(StringCompareTo)
    INTRINSIC_CASE(StringIndexOf)
    INTRINSIC_CASE(StringIndexOfAfter)
    INTRINSIC_CASE(StringEquals)
    INTRINSIC_CASE(StringGetCharsNoCheck)
    INTRINSIC_CASE(StringIsEmpty)
    INTRINSIC_CASE(StringLength)
    default:
      res = false;  // Punt
      break;
  }
  return res;
}

}  // namespace interpreter
}  // namespace art
