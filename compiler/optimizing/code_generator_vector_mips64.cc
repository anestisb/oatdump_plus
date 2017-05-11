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

#include "code_generator_mips64.h"
#include "mirror/array-inl.h"

namespace art {
namespace mips64 {

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<Mips64Assembler*>(GetAssembler())->  // NOLINT

VectorRegister VectorRegisterFrom(Location location) {
  DCHECK(location.IsFpuRegister());
  return static_cast<VectorRegister>(location.AsFpuRegister<FpuRegister>());
}

void LocationsBuilderMIPS64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorMIPS64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ FillB(dst, locations->InAt(0).AsRegister<GpuRegister>());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ FillH(dst, locations->InAt(0).AsRegister<GpuRegister>());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FillW(dst, locations->InAt(0).AsRegister<GpuRegister>());
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FillD(dst, locations->InAt(0).AsRegister<GpuRegister>());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ ReplicateFPToVectorRegister(dst,
                                     locations->InAt(0).AsFpuRegister<FpuRegister>(),
                                     /* is_double */ false);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ ReplicateFPToVectorRegister(dst,
                                     locations->InAt(0).AsFpuRegister<FpuRegister>(),
                                     /* is_double */ true);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS64::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorMIPS64::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

// Helper to set up locations for vector unary operations.
static void CreateVecUnOpLocations(ArenaAllocator* arena, HVecUnaryOperation* instruction) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(),
                        instruction->IsVecNot() ? Location::kOutputOverlap
                                                : Location::kNoOutputOverlap);
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(),
                        (instruction->IsVecNeg() || instruction->IsVecAbs())
                            ? Location::kOutputOverlap
                            : Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister src = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  Primitive::Type from = instruction->GetInputType();
  Primitive::Type to = instruction->GetResultType();
  if (from == Primitive::kPrimInt && to == Primitive::kPrimFloat) {
    DCHECK_EQ(4u, instruction->GetVectorLength());
    __ Ffint_sW(dst, src);
  } else {
    LOG(FATAL) << "Unsupported SIMD type";
    UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister src = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ FillB(dst, ZERO);
      __ SubvB(dst, dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ FillH(dst, ZERO);
      __ SubvH(dst, dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FillW(dst, ZERO);
      __ SubvW(dst, dst, src);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FillD(dst, ZERO);
      __ SubvD(dst, dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FillW(dst, ZERO);
      __ FsubW(dst, dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FillD(dst, ZERO);
      __ FsubD(dst, dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister src = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ FillB(dst, ZERO);       // all zeroes
      __ Add_aB(dst, dst, src);  // dst = abs(0) + abs(src)
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ FillH(dst, ZERO);       // all zeroes
      __ Add_aH(dst, dst, src);  // dst = abs(0) + abs(src)
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FillW(dst, ZERO);       // all zeroes
      __ Add_aW(dst, dst, src);  // dst = abs(0) + abs(src)
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FillD(dst, ZERO);       // all zeroes
      __ Add_aD(dst, dst, src);  // dst = abs(0) + abs(src)
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ LdiW(dst, -1);          // all ones
      __ SrliW(dst, dst, 1);
      __ AndV(dst, dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ LdiD(dst, -1);          // all ones
      __ SrliD(dst, dst, 1);
      __ AndV(dst, dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister src = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:  // special case boolean-not
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ LdiB(dst, 1);
      __ XorV(dst, dst, src);
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ NorV(dst, src, src);  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
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
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ AddvB(dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ AddvH(dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ AddvW(dst, lhs, rhs);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ AddvD(dst, lhs, rhs);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FaddW(dst, lhs, rhs);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FaddD(dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Aver_uB(dst, lhs, rhs)
            : __ Ave_uB(dst, lhs, rhs);
      } else {
        instruction->IsRounded()
            ? __ Aver_sB(dst, lhs, rhs)
            : __ Ave_sB(dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Aver_uH(dst, lhs, rhs)
            : __ Ave_uH(dst, lhs, rhs);
      } else {
        instruction->IsRounded()
            ? __ Aver_sH(dst, lhs, rhs)
            : __ Ave_sH(dst, lhs, rhs);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ SubvB(dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ SubvH(dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ SubvW(dst, lhs, rhs);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ SubvD(dst, lhs, rhs);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FsubW(dst, lhs, rhs);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FsubD(dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ MulvB(dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ MulvH(dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ MulvW(dst, lhs, rhs);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ MulvD(dst, lhs, rhs);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FmulW(dst, lhs, rhs);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FmulD(dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ FdivW(dst, lhs, rhs);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ FdivD(dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS64::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS64::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ AndV(dst, lhs, rhs);  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecAndNot(HVecAndNot* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderMIPS64::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ OrV(dst, lhs, rhs);  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister rhs = VectorRegisterFrom(locations->InAt(1));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ XorV(dst, lhs, rhs);  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
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
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)->AsConstant()));
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ SlliB(dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ SlliH(dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ SlliW(dst, lhs, value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ SlliD(dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ SraiB(dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ SraiH(dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ SraiW(dst, lhs, value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ SraiD(dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorMIPS64::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VectorRegister lhs = VectorRegisterFrom(locations->InAt(0));
  VectorRegister dst = VectorRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ SrliB(dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ SrliH(dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ SrliW(dst, lhs, value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ SrliD(dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void InstructionCodeGeneratorMIPS64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

// Helper to set up locations for vector memory operations.
static void CreateVecMemLocations(ArenaAllocator* arena,
                                  HVecMemoryOperation* instruction,
                                  bool is_load) {
  LocationSummary* locations = new (arena) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      if (is_load) {
        locations->SetOut(Location::RequiresFpuRegister());
      } else {
        locations->SetInAt(2, Location::RequiresFpuRegister());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Helper to prepare register and offset for vector memory operations. Returns the offset and sets
// the output parameter adjusted_base to the original base or to a reserved temporary register (AT).
int32_t InstructionCodeGeneratorMIPS64::VecAddress(LocationSummary* locations,
                                                   size_t size,
                                                   /* out */ GpuRegister* adjusted_base) {
  GpuRegister base = locations->InAt(0).AsRegister<GpuRegister>();
  Location index = locations->InAt(1);
  int scale = TIMES_1;
  switch (size) {
    case 2: scale = TIMES_2; break;
    case 4: scale = TIMES_4; break;
    case 8: scale = TIMES_8; break;
    default: break;
  }
  int32_t offset = mirror::Array::DataOffset(size).Int32Value();

  if (index.IsConstant()) {
    offset += index.GetConstant()->AsIntConstant()->GetValue() << scale;
    __ AdjustBaseOffsetAndElementSizeShift(base, offset, scale);
    *adjusted_base = base;
  } else {
    GpuRegister index_reg = index.AsRegister<GpuRegister>();
    if (scale != TIMES_1) {
      __ Dlsa(AT, index_reg, base, scale);
    } else {
      __ Daddu(AT, base, index_reg);
    }
    *adjusted_base = AT;
  }
  return offset;
}

void LocationsBuilderMIPS64::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /* is_load */ true);
}

void InstructionCodeGeneratorMIPS64::VisitVecLoad(HVecLoad* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  VectorRegister reg = VectorRegisterFrom(locations->Out());
  GpuRegister base;
  int32_t offset = VecAddress(locations, size, &base);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ LdB(reg, base, offset);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      // Loading 8-bytes (needed if dealing with compressed strings in StringCharAt) from unaligned
      // memory address may cause a trap to the kernel if the CPU doesn't directly support unaligned
      // loads and stores.
      // TODO: Implement support for StringCharAt.
      DCHECK(!instruction->IsStringCharAt());
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ LdH(reg, base, offset);
      break;
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ LdW(reg, base, offset);
      break;
    case Primitive::kPrimLong:
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ LdD(reg, base, offset);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderMIPS64::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /* is_load */ false);
}

void InstructionCodeGeneratorMIPS64::VisitVecStore(HVecStore* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  VectorRegister reg = VectorRegisterFrom(locations->InAt(2));
  GpuRegister base;
  int32_t offset = VecAddress(locations, size, &base);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ StB(reg, base, offset);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ StH(reg, base, offset);
      break;
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ StW(reg, base, offset);
      break;
    case Primitive::kPrimLong:
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ StD(reg, base, offset);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

#undef __

}  // namespace mips64
}  // namespace art
