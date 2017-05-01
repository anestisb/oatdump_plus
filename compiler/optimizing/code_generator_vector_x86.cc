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

#include "code_generator_x86.h"
#include "mirror/array-inl.h"

namespace art {
namespace x86 {

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<X86Assembler*>(GetAssembler())->  // NOLINT

void LocationsBuilderX86::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimLong:
      // Long needs extra temporary to load the register pair.
      locations->AddTemp(Location::RequiresFpuRegister());
      FALLTHROUGH_INTENDED;
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorX86::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister reg = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ movd(reg, locations->InAt(0).AsRegister<Register>());
      __ punpcklbw(reg, reg);
      __ punpcklwd(reg, reg);
      __ pshufd(reg, reg, Immediate(0));
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ movd(reg, locations->InAt(0).AsRegister<Register>());
      __ punpcklwd(reg, reg);
      __ pshufd(reg, reg, Immediate(0));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ movd(reg, locations->InAt(0).AsRegister<Register>());
      __ pshufd(reg, reg, Immediate(0));
      break;
    case Primitive::kPrimLong: {
      XmmRegister tmp = locations->GetTemp(0).AsFpuRegister<XmmRegister>();
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ movd(reg, locations->InAt(0).AsRegisterPairLow<Register>());
      __ movd(tmp, locations->InAt(0).AsRegisterPairHigh<Register>());
      __ punpckldq(reg, tmp);
      __ punpcklqdq(reg, reg);
      break;
    }
    case Primitive::kPrimFloat:
      DCHECK(locations->InAt(0).Equals(locations->Out()));
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ shufps(reg, reg, Immediate(0));
      break;
    case Primitive::kPrimDouble:
      DCHECK(locations->InAt(0).Equals(locations->Out()));
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ shufpd(reg, reg, Immediate(0));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorX86::VisitVecSetScalars(HVecSetScalars* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderX86::VisitVecSumReduce(HVecSumReduce* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void InstructionCodeGeneratorX86::VisitVecSumReduce(HVecSumReduce* instruction) {
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
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecCnv(HVecCnv* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecCnv(HVecCnv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  Primitive::Type from = instruction->GetInputType();
  Primitive::Type to = instruction->GetResultType();
  if (from == Primitive::kPrimInt && to == Primitive::kPrimFloat) {
    DCHECK_EQ(4u, instruction->GetVectorLength());
    __ cvtdq2ps(dst, src);
  } else {
    LOG(FATAL) << "Unsupported SIMD type";
  }
}

void LocationsBuilderX86::VisitVecNeg(HVecNeg* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecNeg(HVecNeg* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ pxor(dst, dst);
      __ psubb(dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ pxor(dst, dst);
      __ psubw(dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ pxor(dst, dst);
      __ psubd(dst, src);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ pxor(dst, dst);
      __ psubq(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ xorps(dst, dst);
      __ subps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ xorpd(dst, dst);
      __ subpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecAbs(HVecAbs* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
  // Integral-abs requires a temporary for the comparison.
  if (instruction->GetPackedType() == Primitive::kPrimInt) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86::VisitVecAbs(HVecAbs* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimInt: {
      DCHECK_EQ(4u, instruction->GetVectorLength());
      XmmRegister tmp = locations->GetTemp(0).AsFpuRegister<XmmRegister>();
      __ movaps(dst, src);
      __ pxor(tmp, tmp);
      __ pcmpgtd(tmp, dst);
      __ pxor(dst, tmp);
      __ psubd(dst, tmp);
      break;
    }
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ pcmpeqb(dst, dst);  // all ones
      __ psrld(dst, Immediate(1));
      __ andps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ pcmpeqb(dst, dst);  // all ones
      __ psrlq(dst, Immediate(1));
      __ andpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecNot(HVecNot* instruction) {
  CreateVecUnOpLocations(GetGraph()->GetArena(), instruction);
  // Boolean-not requires a temporary to construct the 16 x one.
  if (instruction->GetPackedType() == Primitive::kPrimBoolean) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86::VisitVecNot(HVecNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XmmRegister src = locations->InAt(0).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean: {  // special case boolean-not
      DCHECK_EQ(16u, instruction->GetVectorLength());
      XmmRegister tmp = locations->GetTemp(0).AsFpuRegister<XmmRegister>();
      __ pxor(dst, dst);
      __ pcmpeqb(tmp, tmp);  // all ones
      __ psubb(dst, tmp);  // 16 x one
      __ pxor(dst, src);
      break;
    }
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ pcmpeqb(dst, dst);  // all ones
      __ pxor(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ pcmpeqb(dst, dst);  // all ones
      __ xorps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ pcmpeqb(dst, dst);  // all ones
      __ xorpd(dst, src);
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
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecAdd(HVecAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecAdd(HVecAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ paddb(dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ paddw(dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ paddd(dst, src);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ paddq(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ addps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ addpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();

  DCHECK(instruction->IsRounded());
  DCHECK(instruction->IsUnsigned());

  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
     __ pavgb(dst, src);
     return;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ pavgw(dst, src);
      return;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecSub(HVecSub* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecSub(HVecSub* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimByte:
      DCHECK_EQ(16u, instruction->GetVectorLength());
      __ psubb(dst, src);
      break;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ psubw(dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ psubd(dst, src);
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ psubq(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ subps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ subpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecMul(HVecMul* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecMul(HVecMul* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ pmullw(dst, src);
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ pmulld(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ mulps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ mulpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecDiv(HVecDiv* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecDiv(HVecDiv* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ divps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ divpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecMin(HVecMin* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecMin(HVecMin* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderX86::VisitVecMax(HVecMax* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecMax(HVecMax* instruction) {
  LOG(FATAL) << "No SIMD for " << instruction->GetId();
}

void LocationsBuilderX86::VisitVecAnd(HVecAnd* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecAnd(HVecAnd* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ pand(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ andps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ andpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecAndNot(HVecAndNot* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecAndNot(HVecAndNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ pandn(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ andnps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ andnpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecOr(HVecOr* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecOr(HVecOr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ por(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ orps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ orpd(dst, src);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecXor(HVecXor* instruction) {
  CreateVecBinOpLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecXor(HVecXor* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  XmmRegister src = locations->InAt(1).AsFpuRegister<XmmRegister>();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      __ pxor(dst, src);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ xorps(dst, src);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ xorpd(dst, src);
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
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::ConstantLocation(instruction->InputAt(1)->AsConstant()));
      locations->SetOut(Location::SameAsFirstInput());
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecShl(HVecShl* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecShl(HVecShl* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ psllw(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ pslld(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ psllq(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecShr(HVecShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecShr(HVecShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ psraw(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ psrad(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecUShr(HVecUShr* instruction) {
  CreateVecShiftLocations(GetGraph()->GetArena(), instruction);
}

void InstructionCodeGeneratorX86::VisitVecUShr(HVecUShr* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  DCHECK(locations->InAt(0).Equals(locations->Out()));
  int32_t value = locations->InAt(1).GetConstant()->AsIntConstant()->GetValue();
  XmmRegister dst = locations->Out().AsFpuRegister<XmmRegister>();
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      __ psrlw(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    case Primitive::kPrimInt:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      __ psrld(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    case Primitive::kPrimLong:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      __ psrlq(dst, Immediate(static_cast<uint8_t>(value)));
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
  LOG(FATAL) << "No SIMD for " << instr->GetId();
}

void InstructionCodeGeneratorX86::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instr) {
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

// Helper to construct address for vector memory operations.
static Address VecAddress(LocationSummary* locations, size_t size, bool is_string_char_at) {
  Location base = locations->InAt(0);
  Location index = locations->InAt(1);
  ScaleFactor scale = TIMES_1;
  switch (size) {
    case 2: scale = TIMES_2; break;
    case 4: scale = TIMES_4; break;
    case 8: scale = TIMES_8; break;
    default: break;
  }
  uint32_t offset = is_string_char_at
      ? mirror::String::ValueOffset().Uint32Value()
      : mirror::Array::DataOffset(size).Uint32Value();
  return CodeGeneratorX86::ArrayAddress(base.AsRegister<Register>(), index, scale, offset);
}

void LocationsBuilderX86::VisitVecLoad(HVecLoad* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ true);
  // String load requires a temporary for the compressed load.
  if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
    instruction->GetLocations()->AddTemp(Location::RequiresFpuRegister());
  }
}

void InstructionCodeGeneratorX86::VisitVecLoad(HVecLoad* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  Address address = VecAddress(locations, size, instruction->IsStringCharAt());
  XmmRegister reg = locations->Out().AsFpuRegister<XmmRegister>();
  bool is_aligned16 = instruction->GetAlignment().IsAlignedAt(16);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimChar:
      DCHECK_EQ(8u, instruction->GetVectorLength());
      // Special handling of compressed/uncompressed string load.
      if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
        NearLabel done, not_compressed;
        XmmRegister tmp = locations->GetTemp(0).AsFpuRegister<XmmRegister>();
        // Test compression bit.
        static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                      "Expecting 0=compressed, 1=uncompressed");
        uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
        __ testb(Address(locations->InAt(0).AsRegister<Register>(), count_offset), Immediate(1));
        __ j(kNotZero, &not_compressed);
        // Zero extend 8 compressed bytes into 8 chars.
        __ movsd(reg, VecAddress(locations, 1, /*is_string_char_at*/ true));
        __ pxor(tmp, tmp);
        __ punpcklbw(reg, tmp);
        __ jmp(&done);
        // Load 4 direct uncompressed chars.
        __ Bind(&not_compressed);
        is_aligned16 ?  __ movdqa(reg, address) :  __ movdqu(reg, address);
        __ Bind(&done);
        return;
      }
      FALLTHROUGH_INTENDED;
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      is_aligned16 ? __ movdqa(reg, address) : __ movdqu(reg, address);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      is_aligned16 ? __ movaps(reg, address) : __ movups(reg, address);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      is_aligned16 ? __ movapd(reg, address) : __ movupd(reg, address);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

void LocationsBuilderX86::VisitVecStore(HVecStore* instruction) {
  CreateVecMemLocations(GetGraph()->GetArena(), instruction, /*is_load*/ false);
}

void InstructionCodeGeneratorX86::VisitVecStore(HVecStore* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t size = Primitive::ComponentSize(instruction->GetPackedType());
  Address address = VecAddress(locations, size, /*is_string_char_at*/ false);
  XmmRegister reg = locations->InAt(2).AsFpuRegister<XmmRegister>();
  bool is_aligned16 = instruction->GetAlignment().IsAlignedAt(16);
  switch (instruction->GetPackedType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:
      DCHECK_LE(2u, instruction->GetVectorLength());
      DCHECK_LE(instruction->GetVectorLength(), 16u);
      is_aligned16 ? __ movdqa(address, reg) : __ movdqu(address, reg);
      break;
    case Primitive::kPrimFloat:
      DCHECK_EQ(4u, instruction->GetVectorLength());
      is_aligned16 ? __ movaps(address, reg) : __ movups(address, reg);
      break;
    case Primitive::kPrimDouble:
      DCHECK_EQ(2u, instruction->GetVectorLength());
      is_aligned16 ? __ movapd(address, reg) : __ movupd(address, reg);
      break;
    default:
      LOG(FATAL) << "Unsupported SIMD type";
      UNREACHABLE();
  }
}

#undef __

}  // namespace x86
}  // namespace art
