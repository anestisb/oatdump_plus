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

#include "code_generator_arm64.h"
#include "mirror/array-inl.h"

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {
namespace arm64 {

using helpers::DRegisterFrom;
using helpers::VRegisterFrom;
using helpers::HeapOperand;
using helpers::InputRegisterAt;
using helpers::Int64ConstantFrom;
using helpers::XRegisterFrom;
using helpers::WRegisterFrom;

#define __ GetVIXLAssembler()->

void LocationsBuilderARM64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
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

void InstructionCodeGeneratorARM64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Dup(dst.V16B(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Dup(dst.V8H(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Dup(dst.V4S(), InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Dup(dst.V2D(), XRegisterFrom(locations->InAt(0)));
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Dup(dst.V4S(), VRegisterFrom(locations->InAt(0)).V4S(), 0);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Dup(dst.V2D(), VRegisterFrom(locations->InAt(0)).V2D(), 0);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecSumReduce(HVecSumReduce* instruction) {
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
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  Primitive::Type from = instruction->GetInputType();
  Primitive::Type to = instruction->GetResultType();
  if (from == Primitive::kPrimInt && to == Primitive::kPrimFloat) {
    DCHECK_EQ(4u, instruction->GetVectorLength());
    __ Scvtf(dst.V4S(), src.V4S());
  } else {
    LOG(FATAL) << "Unsupported SIMD type";
  }
}

void LocationsBuilderARM64::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Neg(dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Neg(dst.V8H(), src.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Neg(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Neg(dst.V2D(), src.V2D());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fneg(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fneg(dst.V2D(), src.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Abs(dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Abs(dst.V8H(), src.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Abs(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Abs(dst.V2D(), src.V2D());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fabs(dst.V4S(), src.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fabs(dst.V2D(), src.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
  }
}

void LocationsBuilderARM64::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister src = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:  // special case boolean-not
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Movi(dst.V16B(), 1);
      __ Eor(dst.V16B(), dst.V16B(), src.V16B());
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      __ Not(dst.V16B(), src.V16B());  // lanes do not matter
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

void LocationsBuilderARM64::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Add(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Add(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Add(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Add(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fadd(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fadd(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Urhadd(dst.V16B(), lhs.V16B(), rhs.V16B())
            : __ Uhadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      } else {
        instruction->IsRounded()
            ? __ Srhadd(dst.V16B(), lhs.V16B(), rhs.V16B())
            : __ Shadd(dst.V16B(), lhs.V16B(), rhs.V16B());
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Urhadd(dst.V8H(), lhs.V8H(), rhs.V8H())
            : __ Uhadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      } else {
        instruction->IsRounded()
            ? __ Srhadd(dst.V8H(), lhs.V8H(), rhs.V8H())
            : __ Shadd(dst.V8H(), lhs.V8H(), rhs.V8H());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sub(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sub(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Sub(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fsub(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fsub(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Mul(dst.V16B(), lhs.V16B(), rhs.V16B());
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Mul(dst.V8H(), lhs.V8H(), rhs.V8H());
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Mul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fmul(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fmul(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Fdiv(dst.V4S(), lhs.V4S(), rhs.V4S());
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Fdiv(dst.V2D(), lhs.V2D(), rhs.V2D());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ And(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void InstructionCodeGeneratorARM64::VisitVecAndNot(HVecAndNot* instruction) {
  LOG(FATAL) << "Unsupported SIMD instruction " << instruction->GetId();
}

void LocationsBuilderARM64::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Orr(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister rhs = VRegisterFrom(locations->InAt(1));
  VRegister dst = VRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Eor(dst.V16B(), lhs.V16B(), rhs.V16B());  // lanes do not matter
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

void LocationsBuilderARM64::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Shl(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Shl(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Shl(dst.V4S(), lhs.V4S(), value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Shl(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Sshr(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Sshr(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Sshr(dst.V4S(), lhs.V4S(), value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Sshr(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARM64::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  VRegister lhs = VRegisterFrom(locations->InAt(0));
  VRegister dst = VRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ Ushr(dst.V16B(), lhs.V16B(), value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Ushr(dst.V8H(), lhs.V8H(), value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Ushr(dst.V4S(), lhs.V4S(), value);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Ushr(dst.V2D(), lhs.V2D(), value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instr);
  switch (instr->GetPackedType()) {
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(
          HVecMultiplyAccumulate::kInputAccumulatorIndex, Location::RequiresFpuRegister());
      locations->SetInAt(
          HVecMultiplyAccumulate::kInputMulLeftIndex, Location::RequiresFpuRegister());
      locations->SetInAt(
          HVecMultiplyAccumulate::kInputMulRightIndex, Location::RequiresFpuRegister());
      DCHECK_EQ(HVecMultiplyAccumulate::kInputAccumulatorIndex, 0);
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

// Some early revisions of the Cortex-A53 have an erratum (835769) whereby it is possible for a
// 64-bit scalar multiply-accumulate instruction in AArch64 state to generate an incorrect result.
// However vector MultiplyAccumulate instruction is not affected.
void InstructionCodeGeneratorARM64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LocationSummary* locations = instr->GetLocations();
  VRegister acc = VRegisterFrom(locations->InAt(HVecMultiplyAccumulate::kInputAccumulatorIndex));
  VRegister left = VRegisterFrom(locations->InAt(HVecMultiplyAccumulate::kInputMulLeftIndex));
  VRegister right = VRegisterFrom(locations->InAt(HVecMultiplyAccumulate::kInputMulRightIndex));
  switch (instr->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instr->GetVectorLength());
      if (instr->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V16B(), left.V16B(), right.V16B());
      } else {
        __ Mls(acc.V16B(), left.V16B(), right.V16B());
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instr->GetVectorLength());
      if (instr->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V8H(), left.V8H(), right.V8H());
      } else {
        __ Mls(acc.V8H(), left.V8H(), right.V8H());
      }
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instr->GetVectorLength());
      if (instr->GetOpKind() == HInstruction::kAdd) {
        __ Mla(acc.V4S(), left.V4S(), right.V4S());
      } else {
        __ Mls(acc.V4S(), left.V4S(), right.V4S());
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
  }
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

// Helper to set up locations for vector memory operations. Returns the memory operand and,
// if used, sets the output parameter scratch to a temporary register used in this operand,
// so that the client can release it right after the memory operand use.
MemOperand InstructionCodeGeneratorARM64::VecAddress(
    HVecMemoryOperation* instruction,
    UseScratchRegisterScope* temps_scope,
    size_t size,
    bool is_string_char_at,
    /*out*/ Register* scratch) {
  LocationSummary* locations = instruction->GetLocations();
  Register base = InputRegisterAt(instruction, 0);
  Location index = locations->InAt(1);
  uint32_t offset = is_string_char_at
      ? mirror::String::ValueOffset().Uint32Value()
      : mirror::Array::DataOffset(size).Uint32Value();
  size_t shift = ComponentSizeShiftWidth(size);

  // HIntermediateAddress optimization is only applied for scalar ArrayGet and ArraySet.
  DCHECK(!instruction->InputAt(0)->IsIntermediateAddress());

  if (index.IsConstant()) {
    offset += Int64ConstantFrom(index) << shift;
    return HeapOperand(base, offset);
  } else {
    *scratch = temps_scope->AcquireSameSizeAs(base);
    __ Add(*scratch, base, Operand(WRegisterFrom(index), LSL, shift));
    return HeapOperand(*scratch, offset);
  }
}

void LocationsBuilderARM64::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ true);
}

void InstructionCodeGeneratorARM64::VisitVecLoad(HVecLoad* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  VRegister reg = VRegisterFrom(locations->Out());
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;

  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      // Special handling of compressed/uncompressed string load.
      if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
        vixl::aarch64::Label uncompressed_load, done;
        // Test compression bit.
        static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                      "Expecting 0=compressed, 1=uncompressed");
        uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
        Register length = temps.AcquireW();
        __ Ldr(length, HeapOperand(InputRegisterAt(instruction, 0), count_offset));
        __ Tbnz(length.W(), 0, &uncompressed_load);
        temps.Release(length);  // no longer needed
        // Zero extend 8 compressed bytes into 8 chars.
        __ Ldr(DRegisterFrom(locations->Out()).V8B(),
               VecAddress(instruction, &temps, 1, /*is_string_char_at*/ true, &scratch));
        __ Uxtl(reg.V8H(), reg.V8B());
        __ B(&done);
        if (scratch.IsValid()) {
          temps.Release(scratch);  // if used, no longer needed
        }
        // Load 8 direct uncompressed chars.
        __ Bind(&uncompressed_load);
        __ Ldr(reg, VecAddress(instruction, &temps, size, /*is_string_char_at*/ true, &scratch));
        __ Bind(&done);
        return;
      }
      FALLTHROUGH_INTENDED;
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
    case Primitive::kPrimLong:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ Ldr(reg, VecAddress(instruction, &temps, size, instruction->IsStringCharAt(), &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARM64::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorARM64::VisitVecStore(HVecStore* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  VRegister reg = VRegisterFrom(locations->InAt(2));
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register scratch;

  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimFloat:
    case Primitive::kPrimLong:
    case Primitive::kPrimDouble:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ Str(reg, VecAddress(instruction, &temps, size, /*is_string_char_at*/ false, &scratch));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

#undef __

}  // namespace arm64
}  // namespace art
