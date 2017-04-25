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

#include "code_generator_mips.h"

namespace art {
namespace mips {

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<MipsAssembler*>(GetAssembler())->  // NOLINT

void LocationsBuilderMIPS::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecSumReduce(HVecSumReduce* instruction) {
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

void LocationsBuilderMIPS::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecCnv(HVecCnv* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecNeg(HVecNeg* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecAbs(HVecAbs* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecNot(HVecNot* instruction) {
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

void LocationsBuilderMIPS::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecAdd(HVecAdd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecSub(HVecSub* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecMul(HVecMul* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecDiv(HVecDiv* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecAnd(HVecAnd* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecAndNot(HVecAndNot* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecOr(HVecOr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecXor(HVecXor* instruction) {
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

void LocationsBuilderMIPS::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecShl(HVecShl* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecShr(HVecShr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS::VisitVecUShr(HVecUShr* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void LocationsBuilderMIPS::VisitVecLoad(HVecLoad* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecLoad(HVecLoad* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS::VisitVecStore(HVecStore* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS::VisitVecStore(HVecStore* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

#undef __

}  // namespace mips
}  // namespace art
