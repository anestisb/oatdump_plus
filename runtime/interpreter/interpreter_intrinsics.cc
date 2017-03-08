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
    default:
      res = false;  // Punt
      break;
  }
  return res;
}

}  // namespace interpreter
}  // namespace art
