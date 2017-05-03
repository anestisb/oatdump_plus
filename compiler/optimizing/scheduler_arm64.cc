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

#include "scheduler_arm64.h"
#include "code_generator_utils.h"
#include "mirror/array-inl.h"

namespace art {
namespace arm64 {

void SchedulingLatencyVisitorARM64::VisitBinaryOperation(HBinaryOperation* instr) {
  last_visited_latency_ = Primitive::IsFloatingPointType(instr->GetResultType())
      ? kArm64FloatingPointOpLatency
      : kArm64IntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitBitwiseNegatedRight(
    HBitwiseNegatedRight* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64IntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitDataProcWithShifterOp(
    HDataProcWithShifterOp* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64DataProcWithShifterOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitIntermediateAddress(
    HIntermediateAddress* ATTRIBUTE_UNUSED) {
  // Although the code generated is a simple `add` instruction, we found through empirical results
  // that spacing it from its use in memory accesses was beneficial.
  last_visited_latency_ = kArm64IntegerOpLatency + 2;
}

void SchedulingLatencyVisitorARM64::VisitIntermediateAddressIndex(
    HIntermediateAddressIndex* instr ATTRIBUTE_UNUSED) {
  // Although the code generated is a simple `add` instruction, we found through empirical results
  // that spacing it from its use in memory accesses was beneficial.
  last_visited_latency_ = kArm64DataProcWithShifterOpLatency + 2;
}

void SchedulingLatencyVisitorARM64::VisitMultiplyAccumulate(HMultiplyAccumulate* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64MulIntegerLatency;
}

void SchedulingLatencyVisitorARM64::VisitArrayGet(HArrayGet* instruction) {
  if (!instruction->GetArray()->IsIntermediateAddress()) {
    // Take the intermediate address computation into account.
    last_visited_internal_latency_ = kArm64IntegerOpLatency;
  }
  last_visited_latency_ = kArm64MemoryLoadLatency;
}

void SchedulingLatencyVisitorARM64::VisitArrayLength(HArrayLength* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64MemoryLoadLatency;
}

void SchedulingLatencyVisitorARM64::VisitArraySet(HArraySet* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64MemoryStoreLatency;
}

void SchedulingLatencyVisitorARM64::VisitBoundsCheck(HBoundsCheck* ATTRIBUTE_UNUSED) {
  last_visited_internal_latency_ = kArm64IntegerOpLatency;
  // Users do not use any data results.
  last_visited_latency_ = 0;
}

void SchedulingLatencyVisitorARM64::VisitDiv(HDiv* instr) {
  Primitive::Type type = instr->GetResultType();
  switch (type) {
    case Primitive::kPrimFloat:
      last_visited_latency_ = kArm64DivFloatLatency;
      break;
    case Primitive::kPrimDouble:
      last_visited_latency_ = kArm64DivDoubleLatency;
      break;
    default:
      // Follow the code path used by code generation.
      if (instr->GetRight()->IsConstant()) {
        int64_t imm = Int64FromConstant(instr->GetRight()->AsConstant());
        if (imm == 0) {
          last_visited_internal_latency_ = 0;
          last_visited_latency_ = 0;
        } else if (imm == 1 || imm == -1) {
          last_visited_internal_latency_ = 0;
          last_visited_latency_ = kArm64IntegerOpLatency;
        } else if (IsPowerOfTwo(AbsOrMin(imm))) {
          last_visited_internal_latency_ = 4 * kArm64IntegerOpLatency;
          last_visited_latency_ = kArm64IntegerOpLatency;
        } else {
          DCHECK(imm <= -2 || imm >= 2);
          last_visited_internal_latency_ = 4 * kArm64IntegerOpLatency;
          last_visited_latency_ = kArm64MulIntegerLatency;
        }
      } else {
        last_visited_latency_ = kArm64DivIntegerLatency;
      }
      break;
  }
}

void SchedulingLatencyVisitorARM64::VisitInstanceFieldGet(HInstanceFieldGet* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64MemoryLoadLatency;
}

void SchedulingLatencyVisitorARM64::VisitInstanceOf(HInstanceOf* ATTRIBUTE_UNUSED) {
  last_visited_internal_latency_ = kArm64CallInternalLatency;
  last_visited_latency_ = kArm64IntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitInvoke(HInvoke* ATTRIBUTE_UNUSED) {
  last_visited_internal_latency_ = kArm64CallInternalLatency;
  last_visited_latency_ = kArm64CallLatency;
}

void SchedulingLatencyVisitorARM64::VisitLoadString(HLoadString* ATTRIBUTE_UNUSED) {
  last_visited_internal_latency_ = kArm64LoadStringInternalLatency;
  last_visited_latency_ = kArm64MemoryLoadLatency;
}

void SchedulingLatencyVisitorARM64::VisitMul(HMul* instr) {
  last_visited_latency_ = Primitive::IsFloatingPointType(instr->GetResultType())
      ? kArm64MulFloatingPointLatency
      : kArm64MulIntegerLatency;
}

void SchedulingLatencyVisitorARM64::VisitNewArray(HNewArray* ATTRIBUTE_UNUSED) {
  last_visited_internal_latency_ = kArm64IntegerOpLatency + kArm64CallInternalLatency;
  last_visited_latency_ = kArm64CallLatency;
}

void SchedulingLatencyVisitorARM64::VisitNewInstance(HNewInstance* instruction) {
  if (instruction->IsStringAlloc()) {
    last_visited_internal_latency_ = 2 + kArm64MemoryLoadLatency + kArm64CallInternalLatency;
  } else {
    last_visited_internal_latency_ = kArm64CallInternalLatency;
  }
  last_visited_latency_ = kArm64CallLatency;
}

void SchedulingLatencyVisitorARM64::VisitRem(HRem* instruction) {
  if (Primitive::IsFloatingPointType(instruction->GetResultType())) {
    last_visited_internal_latency_ = kArm64CallInternalLatency;
    last_visited_latency_ = kArm64CallLatency;
  } else {
    // Follow the code path used by code generation.
    if (instruction->GetRight()->IsConstant()) {
      int64_t imm = Int64FromConstant(instruction->GetRight()->AsConstant());
      if (imm == 0) {
        last_visited_internal_latency_ = 0;
        last_visited_latency_ = 0;
      } else if (imm == 1 || imm == -1) {
        last_visited_internal_latency_ = 0;
        last_visited_latency_ = kArm64IntegerOpLatency;
      } else if (IsPowerOfTwo(AbsOrMin(imm))) {
        last_visited_internal_latency_ = 4 * kArm64IntegerOpLatency;
        last_visited_latency_ = kArm64IntegerOpLatency;
      } else {
        DCHECK(imm <= -2 || imm >= 2);
        last_visited_internal_latency_ = 4 * kArm64IntegerOpLatency;
        last_visited_latency_ = kArm64MulIntegerLatency;
      }
    } else {
      last_visited_internal_latency_ = kArm64DivIntegerLatency;
      last_visited_latency_ = kArm64MulIntegerLatency;
    }
  }
}

void SchedulingLatencyVisitorARM64::VisitStaticFieldGet(HStaticFieldGet* ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64MemoryLoadLatency;
}

void SchedulingLatencyVisitorARM64::VisitSuspendCheck(HSuspendCheck* instruction) {
  HBasicBlock* block = instruction->GetBlock();
  DCHECK((block->GetLoopInformation() != nullptr) ||
         (block->IsEntryBlock() && instruction->GetNext()->IsGoto()));
  // Users do not use any data results.
  last_visited_latency_ = 0;
}

void SchedulingLatencyVisitorARM64::VisitTypeConversion(HTypeConversion* instr) {
  if (Primitive::IsFloatingPointType(instr->GetResultType()) ||
      Primitive::IsFloatingPointType(instr->GetInputType())) {
    last_visited_latency_ = kArm64TypeConversionFloatingPointIntegerLatency;
  } else {
    last_visited_latency_ = kArm64IntegerOpLatency;
  }
}

void SchedulingLatencyVisitorARM64::HandleSimpleArithmeticSIMD(HVecOperation *instr) {
  if (Primitive::IsFloatingPointType(instr->GetPackedType())) {
    last_visited_latency_ = kArm64SIMDFloatingPointOpLatency;
  } else {
    last_visited_latency_ = kArm64SIMDIntegerOpLatency;
  }
}

void SchedulingLatencyVisitorARM64::VisitVecReplicateScalar(
    HVecReplicateScalar* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDReplicateOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecSetScalars(HVecSetScalars* instr) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instr->GetId();
}

void SchedulingLatencyVisitorARM64::VisitVecSumReduce(HVecSumReduce* instr) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instr->GetId();
}

void SchedulingLatencyVisitorARM64::VisitVecCnv(HVecCnv* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDTypeConversionInt2FPLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecNeg(HVecNeg* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecAbs(HVecAbs* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecNot(HVecNot* instr) {
  if (instr->GetPackedType() == Primitive::kPrimBoolean) {
    last_visited_internal_latency_ = kArm64SIMDIntegerOpLatency;
  }
  last_visited_latency_ = kArm64SIMDIntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecAdd(HVecAdd* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecHalvingAdd(HVecHalvingAdd* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecSub(HVecSub* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecMul(HVecMul* instr) {
  if (Primitive::IsFloatingPointType(instr->GetPackedType())) {
    last_visited_latency_ = kArm64SIMDMulFloatingPointLatency;
  } else {
    last_visited_latency_ = kArm64SIMDMulIntegerLatency;
  }
}

void SchedulingLatencyVisitorARM64::VisitVecDiv(HVecDiv* instr) {
  if (instr->GetPackedType() == Primitive::kPrimFloat) {
    last_visited_latency_ = kArm64SIMDDivFloatLatency;
  } else {
    DCHECK(instr->GetPackedType() == Primitive::kPrimDouble);
    last_visited_latency_ = kArm64SIMDDivDoubleLatency;
  }
}

void SchedulingLatencyVisitorARM64::VisitVecMin(HVecMin* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecMax(HVecMax* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecAnd(HVecAnd* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDIntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecAndNot(HVecAndNot* instr) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instr->GetId();
}

void SchedulingLatencyVisitorARM64::VisitVecOr(HVecOr* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDIntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecXor(HVecXor* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDIntegerOpLatency;
}

void SchedulingLatencyVisitorARM64::VisitVecShl(HVecShl* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecShr(HVecShr* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecUShr(HVecUShr* instr) {
  HandleSimpleArithmeticSIMD(instr);
}

void SchedulingLatencyVisitorARM64::VisitVecMultiplyAccumulate(
    HVecMultiplyAccumulate* instr ATTRIBUTE_UNUSED) {
  last_visited_latency_ = kArm64SIMDMulIntegerLatency;
}

void SchedulingLatencyVisitorARM64::HandleVecAddress(
    HVecMemoryOperation* instruction,
    size_t size ATTRIBUTE_UNUSED) {
  HInstruction* index = instruction->InputAt(1);
  if (!index->IsConstant()) {
    last_visited_internal_latency_ += kArm64DataProcWithShifterOpLatency;
  }
}

void SchedulingLatencyVisitorARM64::VisitVecLoad(HVecLoad* instr) {
  last_visited_internal_latency_ = 0;
  size_t size = Primitive::ComponentSize(instr->GetPackedType());

  if (instr->GetPackedType() == Primitive::kPrimChar
      && mirror::kUseStringCompression
      && instr->IsStringCharAt()) {
    // Set latencies for the uncompressed case.
    last_visited_internal_latency_ += kArm64MemoryLoadLatency + kArm64BranchLatency;
    HandleVecAddress(instr, size);
    last_visited_latency_ = kArm64SIMDMemoryLoadLatency;
  } else {
    HandleVecAddress(instr, size);
    last_visited_latency_ = kArm64SIMDMemoryLoadLatency;
  }
}

void SchedulingLatencyVisitorARM64::VisitVecStore(HVecStore* instr) {
  last_visited_internal_latency_ = 0;
  size_t size = Primitive::ComponentSize(instr->GetPackedType());
  HandleVecAddress(instr, size);
  last_visited_latency_ = kArm64SIMDMemoryStoreLatency;
}

}  // namespace arm64
}  // namespace art
