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
#include "mirror/array-inl.h"

namespace vixl32 = vixl::aarch32;
using namespace vixl32;  // NOLINT(build/namespaces)

namespace art {
namespace arm {

using helpers::DRegisterFrom;
using helpers::Int64ConstantFrom;
using helpers::InputDRegisterAt;
using helpers::InputRegisterAt;
using helpers::OutputDRegister;
using helpers::RegisterFrom;

#define __ GetVIXLAssembler()->

void LocationsBuilderARMVIXL::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorARMVIXL::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vdup(Untyped8, dst, InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vdup(Untyped16, dst, InputRegisterAt(instruction, 0));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vdup(Untyped32, dst, InputRegisterAt(instruction, 0));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
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
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(),
                        instruction->IsVecNot() ? Location::kOutputOverlap
                                                : Location::kNoOutputOverlap);
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
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
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister src = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vneg(DataTypeValue::S8, dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vneg(DataTypeValue::S16, dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vneg(DataTypeValue::S32, dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister src = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vabs(DataTypeValue::S8, dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vabs(DataTypeValue::S16, dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vabs(DataTypeValue::S32, dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister src = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:  // special case boolean-not
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vmov(I8, dst, 1);
      __ Veor(dst, dst, src);
      break;
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      __ Vmvn(I8, dst, src);  // lanes do not matter
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
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
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
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vadd(I8, dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vadd(I16, dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vadd(I32, dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Vrhadd(DataTypeValue::U8, dst, lhs, rhs)
            : __ Vhadd(DataTypeValue::U8, dst, lhs, rhs);
      } else {
        instruction->IsRounded()
            ? __ Vrhadd(DataTypeValue::S8, dst, lhs, rhs)
            : __ Vhadd(DataTypeValue::S8, dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        instruction->IsRounded()
            ? __ Vrhadd(DataTypeValue::U16, dst, lhs, rhs)
            : __ Vhadd(DataTypeValue::U16, dst, lhs, rhs);
      } else {
        instruction->IsRounded()
            ? __ Vrhadd(DataTypeValue::S16, dst, lhs, rhs)
            : __ Vhadd(DataTypeValue::S16, dst, lhs, rhs);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vsub(I8, dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vsub(I16, dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vsub(I32, dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vmul(I8, dst, lhs, rhs);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vmul(I16, dst, lhs, rhs);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vmul(I32, dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
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
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmin(DataTypeValue::U8, dst, lhs, rhs);
      } else {
        __ Vmin(DataTypeValue::S8, dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmin(DataTypeValue::U16, dst, lhs, rhs);
      } else {
        __ Vmin(DataTypeValue::S16, dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmin(DataTypeValue::U32, dst, lhs, rhs);
      } else {
        __ Vmin(DataTypeValue::S32, dst, lhs, rhs);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecMax(HVecMax* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmax(DataTypeValue::U8, dst, lhs, rhs);
      } else {
        __ Vmax(DataTypeValue::S8, dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmax(DataTypeValue::U16, dst, lhs, rhs);
      } else {
        __ Vmax(DataTypeValue::S16, dst, lhs, rhs);
      }
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (instruction->IsUnsigned()) {
        __ Vmax(DataTypeValue::U32, dst, lhs, rhs);
      } else {
        __ Vmax(DataTypeValue::S32, dst, lhs, rhs);
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      __ Vand(I8, dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
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
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      __ Vorr(I8, dst, lhs, rhs);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister rhs = DRegisterFrom(locations->InAt(1));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      __ Veor(I8, dst, lhs, rhs);
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
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)->AsConstant()));
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
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
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vshl(I8, dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vshl(I16, dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vshl(I32, dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::S8, dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::S16, dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::S32, dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorARMVIXL::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::DRegister lhs = DRegisterFrom(locations->InAt(0));
  vixl32::DRegister dst = DRegisterFrom(locations->Out());
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::U8, dst, lhs, value);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::U16, dst, lhs, value);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ Vshr(DataTypeValue::U32, dst, lhs, value);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void InstructionCodeGeneratorARMVIXL::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

// Return whether the vector memory access operation is guaranteed to be word-aligned (ARM word
// size equals to 4).
static bool IsWordAligned(HVecMemoryOperation* instruction) {
  return instruction->GetAlignment().IsAlignedAt(4u);
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
MemOperand InstructionCodeGeneratorARMVIXL::VecAddress(
        HVecMemoryOperation* instruction,
        UseScratchRegisterScope* temps_scope,
        /*out*/ vixl32::Register* scratch) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::Register base = InputRegisterAt(instruction, 0);

  Location index = locations->InAt(1);
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  uint32_t offset = mirror::Array::DataOffset(size).Uint32Value();
  size_t shift = ComponentSizeShiftWidth(size);

  // HIntermediateAddress optimization is only applied for scalar ArrayGet and ArraySet.
  DCHECK(!instruction->InputAt(0)->IsIntermediateAddress());

  if (index.IsConstant()) {
    offset += Int64ConstantFrom(index) << shift;
    return MemOperand(base, offset);
  } else {
    *scratch = temps_scope->Acquire();
    __ Add(*scratch, base, Operand(RegisterFrom(index), ShiftType::LSL, shift));

    return MemOperand(*scratch, offset);
  }
}

AlignedMemOperand InstructionCodeGeneratorARMVIXL::VecAddressUnaligned(
        HVecMemoryOperation* instruction,
        UseScratchRegisterScope* temps_scope,
        /*out*/ vixl32::Register* scratch) {
  LocationSummary* locations = instruction->GetLocations();
  vixl32::Register base = InputRegisterAt(instruction, 0);

  Location index = locations->InAt(1);
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  uint32_t offset = mirror::Array::DataOffset(size).Uint32Value();
  size_t shift = ComponentSizeShiftWidth(size);

  // HIntermediateAddress optimization is only applied for scalar ArrayGet and ArraySet.
  DCHECK(!instruction->InputAt(0)->IsIntermediateAddress());

  if (index.IsConstant()) {
    offset += Int64ConstantFrom(index) << shift;
    __ Add(*scratch, base, offset);
  } else {
    *scratch = temps_scope->Acquire();
    __ Add(*scratch, base, offset);
    __ Add(*scratch, *scratch, Operand(RegisterFrom(index), ShiftType::LSL, shift));
  }
  return AlignedMemOperand(*scratch, kNoAlignment);
}

void LocationsBuilderARMVIXL::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ true);
}

void InstructionCodeGeneratorARMVIXL::VisitVecLoad(HVecLoad* instruction) {
  vixl32::DRegister reg = OutputDRegister(instruction);
  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Register scratch;

  DCHECK(instruction->GetPackedType() != Primitive::kPrimChar || !instruction->IsStringCharAt());

  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vldr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vld1(Untyped8,
            NeonRegisterList(reg, kMultipleLanes),
            VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vldr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vld1(Untyped16,
            NeonRegisterList(reg, kMultipleLanes),
            VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vldr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vld1(Untyped32,
            NeonRegisterList(reg, kMultipleLanes),
            VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderARMVIXL::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorARMVIXL::VisitVecStore(HVecStore* instruction) {
  vixl32::DRegister reg = InputDRegisterAt(instruction, 2);
  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Register scratch;
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vstr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vst1(Untyped8,
                NeonRegisterList(reg, kMultipleLanes),
                VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vstr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vst1(Untyped16,
                NeonRegisterList(reg, kMultipleLanes),
                VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      if (IsWordAligned(instruction)) {
        __ Vstr(reg, VecAddress(instruction, &temps, &scratch));
      } else {
        __ Vst1(Untyped32,
                NeonRegisterList(reg, kMultipleLanes),
                VecAddressUnaligned(instruction, &temps, &scratch));
      }
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

#undef __

}  // namespace arm
}  // namespace art
