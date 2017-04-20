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

#include "code_generator_arm_vixl.h"

namespace art {
namespace arm {

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ reinterpret_cast<ArmVIXLAssembler*>(GetAssembler())->GetVIXLAssembler()->  // NOLINT

void LocationsBuilderARMVIXL::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

// Helper to set up locations for vector unary operations.
static void CreateVecUnOpLocations(ArenaAllocator* arena, HVecUnaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK(locations);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecCnv(HVecCnv* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecNeg(HVecNeg* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAbs(HVecAbs* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecNot(HVecNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

// Helper to set up locations for vector binary operations.
static void CreateVecBinOpLocations(ArenaAllocator* arena, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK(locations);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAdd(HVecAdd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecSub(HVecSub* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecMul(HVecMul* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecDiv(HVecDiv* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAnd(HVecAnd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecAndNot(HVecAndNot* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecOr(HVecOr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecXor(HVecXor* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

// Helper to set up locations for vector shift operations.
static void CreateVecShiftLocations(ArenaAllocator* arena, HVecBinaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK(locations);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecShl(HVecShl* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecShr(HVecShr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecUShr(HVecUShr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void LocationsBuilderARMVIXL::VisitVecLoad(HVecLoad* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecLoad(HVecLoad* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARMVIXL::VisitVecStore(HVecStore* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecStore(HVecStore* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

#undef __

}  // namespace arm
}  // namespace art
