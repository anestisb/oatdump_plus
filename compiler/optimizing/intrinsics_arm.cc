/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "intrinsics_arm.h"

#include "arch/arm/instruction_set_features_arm.h"
#include "art_method.h"
#include "code_generator_arm.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "intrinsics.h"
#include "intrinsics_utils.h"
#include "lock_word.h"
#include "mirror/array-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/reference.h"
#include "mirror/string.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "utils/arm/assembler_arm.h"

namespace art {

namespace arm {

ArmAssembler* IntrinsicCodeGeneratorARM::GetAssembler() {
  return codegen_->GetAssembler();
}

ArenaAllocator* IntrinsicCodeGeneratorARM::GetAllocator() {
  return codegen_->GetGraph()->GetArena();
}

using IntrinsicSlowPathARM = IntrinsicSlowPath<InvokeDexCallingConventionVisitorARM>;

#define __ assembler->

// Compute base address for the System.arraycopy intrinsic in `base`.
static void GenSystemArrayCopyBaseAddress(ArmAssembler* assembler,
                                          Primitive::Type type,
                                          const Register& array,
                                          const Location& pos,
                                          const Register& base) {
  // This routine is only used by the SystemArrayCopy intrinsic at the
  // moment. We can allow Primitive::kPrimNot as `type` to implement
  // the SystemArrayCopyChar intrinsic.
  DCHECK_EQ(type, Primitive::kPrimNot);
  const int32_t element_size = Primitive::ComponentSize(type);
  const uint32_t element_size_shift = Primitive::ComponentSizeShift(type);
  const uint32_t data_offset = mirror::Array::DataOffset(element_size).Uint32Value();

  if (pos.IsConstant()) {
    int32_t constant = pos.GetConstant()->AsIntConstant()->GetValue();
    __ AddConstant(base, array, element_size * constant + data_offset);
  } else {
    __ add(base, array, ShifterOperand(pos.AsRegister<Register>(), LSL, element_size_shift));
    __ AddConstant(base, data_offset);
  }
}

// Compute end address for the System.arraycopy intrinsic in `end`.
static void GenSystemArrayCopyEndAddress(ArmAssembler* assembler,
                                         Primitive::Type type,
                                         const Location& copy_length,
                                         const Register& base,
                                         const Register& end) {
  // This routine is only used by the SystemArrayCopy intrinsic at the
  // moment. We can allow Primitive::kPrimNot as `type` to implement
  // the SystemArrayCopyChar intrinsic.
  DCHECK_EQ(type, Primitive::kPrimNot);
  const int32_t element_size = Primitive::ComponentSize(type);
  const uint32_t element_size_shift = Primitive::ComponentSizeShift(type);

  if (copy_length.IsConstant()) {
    int32_t constant = copy_length.GetConstant()->AsIntConstant()->GetValue();
    __ AddConstant(end, base, element_size * constant);
  } else {
    __ add(end, base, ShifterOperand(copy_length.AsRegister<Register>(), LSL, element_size_shift));
  }
}

#undef __

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<ArmAssembler*>(codegen->GetAssembler())->  // NOLINT

// Slow path implementing the SystemArrayCopy intrinsic copy loop with read barriers.
class ReadBarrierSystemArrayCopySlowPathARM : public SlowPathCode {
 public:
  explicit ReadBarrierSystemArrayCopySlowPathARM(HInstruction* instruction)
      : SlowPathCode(instruction) {
    DCHECK(kEmitCompilerReadBarrier);
    DCHECK(kUseBakerReadBarrier);
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    ArmAssembler* assembler = arm_codegen->GetAssembler();
    LocationSummary* locations = instruction_->GetLocations();
    DCHECK(locations->CanCall());
    DCHECK(instruction_->IsInvokeStaticOrDirect())
        << "Unexpected instruction in read barrier arraycopy slow path: "
        << instruction_->DebugName();
    DCHECK(instruction_->GetLocations()->Intrinsified());
    DCHECK_EQ(instruction_->AsInvoke()->GetIntrinsic(), Intrinsics::kSystemArrayCopy);

    Primitive::Type type = Primitive::kPrimNot;
    const int32_t element_size = Primitive::ComponentSize(type);

    Register dest = locations->InAt(2).AsRegister<Register>();
    Location dest_pos = locations->InAt(3);
    Register src_curr_addr = locations->GetTemp(0).AsRegister<Register>();
    Register dst_curr_addr = locations->GetTemp(1).AsRegister<Register>();
    Register src_stop_addr = locations->GetTemp(2).AsRegister<Register>();
    Register tmp = locations->GetTemp(3).AsRegister<Register>();

    __ Bind(GetEntryLabel());
    // Compute the base destination address in `dst_curr_addr`.
    GenSystemArrayCopyBaseAddress(assembler, type, dest, dest_pos, dst_curr_addr);

    Label loop;
    __ Bind(&loop);
    __ ldr(tmp, Address(src_curr_addr, element_size, Address::PostIndex));
    __ MaybeUnpoisonHeapReference(tmp);
    // TODO: Inline the mark bit check before calling the runtime?
    // tmp = ReadBarrier::Mark(tmp);
    // No need to save live registers; it's taken care of by the
    // entrypoint. Also, there is no need to update the stack mask,
    // as this runtime call will not trigger a garbage collection.
    // (See ReadBarrierMarkSlowPathARM::EmitNativeCode for more
    // explanations.)
    DCHECK_NE(tmp, SP);
    DCHECK_NE(tmp, LR);
    DCHECK_NE(tmp, PC);
    // IP is used internally by the ReadBarrierMarkRegX entry point
    // as a temporary (and not preserved).  It thus cannot be used by
    // any live register in this slow path.
    DCHECK_NE(src_curr_addr, IP);
    DCHECK_NE(dst_curr_addr, IP);
    DCHECK_NE(src_stop_addr, IP);
    DCHECK_NE(tmp, IP);
    DCHECK(0 <= tmp && tmp < kNumberOfCoreRegisters) << tmp;
    // TODO: Load the entrypoint once before the loop, instead of
    // loading it at every iteration.
    int32_t entry_point_offset =
        CodeGenerator::GetReadBarrierMarkEntryPointsOffset<kArmPointerSize>(tmp);
    // This runtime call does not require a stack map.
    arm_codegen->InvokeRuntimeWithoutRecordingPcInfo(entry_point_offset, instruction_, this);
    __ MaybePoisonHeapReference(tmp);
    __ str(tmp, Address(dst_curr_addr, element_size, Address::PostIndex));
    __ cmp(src_curr_addr, ShifterOperand(src_stop_addr));
    __ b(&loop, NE);
    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "ReadBarrierSystemArrayCopySlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(ReadBarrierSystemArrayCopySlowPathARM);
};

#undef __

IntrinsicLocationsBuilderARM::IntrinsicLocationsBuilderARM(CodeGeneratorARM* codegen)
    : arena_(codegen->GetGraph()->GetArena()),
      codegen_(codegen),
      assembler_(codegen->GetAssembler()),
      features_(codegen->GetInstructionSetFeatures()) {}

bool IntrinsicLocationsBuilderARM::TryDispatch(HInvoke* invoke) {
  Dispatch(invoke);
  LocationSummary* res = invoke->GetLocations();
  if (res == nullptr) {
    return false;
  }
  return res->Intrinsified();
}

#define __ assembler->

static void CreateFPToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void CreateIntToFPLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
}

static void MoveFPToInt(LocationSummary* locations, bool is64bit, ArmAssembler* assembler) {
  Location input = locations->InAt(0);
  Location output = locations->Out();
  if (is64bit) {
    __ vmovrrd(output.AsRegisterPairLow<Register>(),
               output.AsRegisterPairHigh<Register>(),
               FromLowSToD(input.AsFpuRegisterPairLow<SRegister>()));
  } else {
    __ vmovrs(output.AsRegister<Register>(), input.AsFpuRegister<SRegister>());
  }
}

static void MoveIntToFP(LocationSummary* locations, bool is64bit, ArmAssembler* assembler) {
  Location input = locations->InAt(0);
  Location output = locations->Out();
  if (is64bit) {
    __ vmovdrr(FromLowSToD(output.AsFpuRegisterPairLow<SRegister>()),
               input.AsRegisterPairLow<Register>(),
               input.AsRegisterPairHigh<Register>());
  } else {
    __ vmovsr(output.AsFpuRegister<SRegister>(), input.AsRegister<Register>());
  }
}

void IntrinsicLocationsBuilderARM::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}
void IntrinsicLocationsBuilderARM::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  CreateIntToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}
void IntrinsicCodeGeneratorARM::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}
void IntrinsicLocationsBuilderARM::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  CreateIntToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}
void IntrinsicCodeGeneratorARM::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

static void CreateIntToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void CreateFPToFPLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void GenNumberOfLeadingZeros(HInvoke* invoke,
                                    Primitive::Type type,
                                    CodeGeneratorARM* codegen) {
  ArmAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location in = locations->InAt(0);
  Register out = locations->Out().AsRegister<Register>();

  DCHECK((type == Primitive::kPrimInt) || (type == Primitive::kPrimLong));

  if (type == Primitive::kPrimLong) {
    Register in_reg_lo = in.AsRegisterPairLow<Register>();
    Register in_reg_hi = in.AsRegisterPairHigh<Register>();
    Label end;
    Label* final_label = codegen->GetFinalLabel(invoke, &end);
    __ clz(out, in_reg_hi);
    __ CompareAndBranchIfNonZero(in_reg_hi, final_label);
    __ clz(out, in_reg_lo);
    __ AddConstant(out, 32);
    if (end.IsLinked()) {
      __ Bind(&end);
    }
  } else {
    __ clz(out, in.AsRegister<Register>());
  }
}

void IntrinsicLocationsBuilderARM::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeros(invoke, Primitive::kPrimInt, codegen_);
}

void IntrinsicLocationsBuilderARM::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeros(invoke, Primitive::kPrimLong, codegen_);
}

static void GenNumberOfTrailingZeros(HInvoke* invoke,
                                     Primitive::Type type,
                                     CodeGeneratorARM* codegen) {
  DCHECK((type == Primitive::kPrimInt) || (type == Primitive::kPrimLong));

  ArmAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Register out = locations->Out().AsRegister<Register>();

  if (type == Primitive::kPrimLong) {
    Register in_reg_lo = locations->InAt(0).AsRegisterPairLow<Register>();
    Register in_reg_hi = locations->InAt(0).AsRegisterPairHigh<Register>();
    Label end;
    Label* final_label = codegen->GetFinalLabel(invoke, &end);
    __ rbit(out, in_reg_lo);
    __ clz(out, out);
    __ CompareAndBranchIfNonZero(in_reg_lo, final_label);
    __ rbit(out, in_reg_hi);
    __ clz(out, out);
    __ AddConstant(out, 32);
    if (end.IsLinked()) {
      __ Bind(&end);
    }
  } else {
    Register in = locations->InAt(0).AsRegister<Register>();
    __ rbit(out, in);
    __ clz(out, out);
  }
}

void IntrinsicLocationsBuilderARM::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeros(invoke, Primitive::kPrimInt, codegen_);
}

void IntrinsicLocationsBuilderARM::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeros(invoke, Primitive::kPrimLong, codegen_);
}

static void MathAbsFP(LocationSummary* locations, bool is64bit, ArmAssembler* assembler) {
  Location in = locations->InAt(0);
  Location out = locations->Out();

  if (is64bit) {
    __ vabsd(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
             FromLowSToD(in.AsFpuRegisterPairLow<SRegister>()));
  } else {
    __ vabss(out.AsFpuRegister<SRegister>(), in.AsFpuRegister<SRegister>());
  }
}

void IntrinsicLocationsBuilderARM::VisitMathAbsDouble(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAbsDouble(HInvoke* invoke) {
  MathAbsFP(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitMathAbsFloat(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAbsFloat(HInvoke* invoke) {
  MathAbsFP(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

static void CreateIntToIntPlusTemp(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);

  locations->AddTemp(Location::RequiresRegister());
}

static void GenAbsInteger(LocationSummary* locations,
                          bool is64bit,
                          ArmAssembler* assembler) {
  Location in = locations->InAt(0);
  Location output = locations->Out();

  Register mask = locations->GetTemp(0).AsRegister<Register>();

  if (is64bit) {
    Register in_reg_lo = in.AsRegisterPairLow<Register>();
    Register in_reg_hi = in.AsRegisterPairHigh<Register>();
    Register out_reg_lo = output.AsRegisterPairLow<Register>();
    Register out_reg_hi = output.AsRegisterPairHigh<Register>();

    DCHECK_NE(out_reg_lo, in_reg_hi) << "Diagonal overlap unexpected.";

    __ Asr(mask, in_reg_hi, 31);
    __ adds(out_reg_lo, in_reg_lo, ShifterOperand(mask));
    __ adc(out_reg_hi, in_reg_hi, ShifterOperand(mask));
    __ eor(out_reg_lo, mask, ShifterOperand(out_reg_lo));
    __ eor(out_reg_hi, mask, ShifterOperand(out_reg_hi));
  } else {
    Register in_reg = in.AsRegister<Register>();
    Register out_reg = output.AsRegister<Register>();

    __ Asr(mask, in_reg, 31);
    __ add(out_reg, in_reg, ShifterOperand(mask));
    __ eor(out_reg, mask, ShifterOperand(out_reg));
  }
}

void IntrinsicLocationsBuilderARM::VisitMathAbsInt(HInvoke* invoke) {
  CreateIntToIntPlusTemp(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAbsInt(HInvoke* invoke) {
  GenAbsInteger(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}


void IntrinsicLocationsBuilderARM::VisitMathAbsLong(HInvoke* invoke) {
  CreateIntToIntPlusTemp(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAbsLong(HInvoke* invoke) {
  GenAbsInteger(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

static void GenMinMax(LocationSummary* locations,
                      bool is_min,
                      ArmAssembler* assembler) {
  Register op1 = locations->InAt(0).AsRegister<Register>();
  Register op2 = locations->InAt(1).AsRegister<Register>();
  Register out = locations->Out().AsRegister<Register>();

  __ cmp(op1, ShifterOperand(op2));

  __ it((is_min) ? Condition::LT : Condition::GT, kItElse);
  __ mov(out, ShifterOperand(op1), is_min ? Condition::LT : Condition::GT);
  __ mov(out, ShifterOperand(op2), is_min ? Condition::GE : Condition::LE);
}

static void CreateIntIntToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void IntrinsicLocationsBuilderARM::VisitMathMinIntInt(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathMinIntInt(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ true, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitMathMaxIntInt(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathMaxIntInt(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ false, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitMathSqrt(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathSqrt(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  ArmAssembler* assembler = GetAssembler();
  __ vsqrtd(FromLowSToD(locations->Out().AsFpuRegisterPairLow<SRegister>()),
            FromLowSToD(locations->InAt(0).AsFpuRegisterPairLow<SRegister>()));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPeekByte(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPeekByte(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ ldrsb(invoke->GetLocations()->Out().AsRegister<Register>(),
           Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPeekIntNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ ldr(invoke->GetLocations()->Out().AsRegister<Register>(),
         Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPeekLongNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  Register addr = invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>();
  // Worst case: Control register bit SCTLR.A = 0. Then unaligned accesses throw a processor
  // exception. So we can't use ldrd as addr may be unaligned.
  Register lo = invoke->GetLocations()->Out().AsRegisterPairLow<Register>();
  Register hi = invoke->GetLocations()->Out().AsRegisterPairHigh<Register>();
  if (addr == lo) {
    __ ldr(hi, Address(addr, 4));
    __ ldr(lo, Address(addr, 0));
  } else {
    __ ldr(lo, Address(addr, 0));
    __ ldr(hi, Address(addr, 4));
  }
}

void IntrinsicLocationsBuilderARM::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPeekShortNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  __ ldrsh(invoke->GetLocations()->Out().AsRegister<Register>(),
           Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

static void CreateIntIntToVoidLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
}

void IntrinsicLocationsBuilderARM::VisitMemoryPokeByte(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPokeByte(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  __ strb(invoke->GetLocations()->InAt(1).AsRegister<Register>(),
          Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPokeIntNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPokeIntNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  __ str(invoke->GetLocations()->InAt(1).AsRegister<Register>(),
         Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPokeLongNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPokeLongNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  // Ignore upper 4B of long address.
  Register addr = invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>();
  // Worst case: Control register bit SCTLR.A = 0. Then unaligned accesses throw a processor
  // exception. So we can't use ldrd as addr may be unaligned.
  __ str(invoke->GetLocations()->InAt(1).AsRegisterPairLow<Register>(), Address(addr, 0));
  __ str(invoke->GetLocations()->InAt(1).AsRegisterPairHigh<Register>(), Address(addr, 4));
}

void IntrinsicLocationsBuilderARM::VisitMemoryPokeShortNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMemoryPokeShortNative(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  __ strh(invoke->GetLocations()->InAt(1).AsRegister<Register>(),
          Address(invoke->GetLocations()->InAt(0).AsRegisterPairLow<Register>()));
}

void IntrinsicLocationsBuilderARM::VisitThreadCurrentThread(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARM::VisitThreadCurrentThread(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  __ LoadFromOffset(kLoadWord,
                    invoke->GetLocations()->Out().AsRegister<Register>(),
                    TR,
                    Thread::PeerOffset<kArmPointerSize>().Int32Value());
}

static void GenUnsafeGet(HInvoke* invoke,
                         Primitive::Type type,
                         bool is_volatile,
                         CodeGeneratorARM* codegen) {
  LocationSummary* locations = invoke->GetLocations();
  ArmAssembler* assembler = codegen->GetAssembler();
  Location base_loc = locations->InAt(1);
  Register base = base_loc.AsRegister<Register>();             // Object pointer.
  Location offset_loc = locations->InAt(2);
  Register offset = offset_loc.AsRegisterPairLow<Register>();  // Long offset, lo part only.
  Location trg_loc = locations->Out();

  switch (type) {
    case Primitive::kPrimInt: {
      Register trg = trg_loc.AsRegister<Register>();
      __ ldr(trg, Address(base, offset));
      if (is_volatile) {
        __ dmb(ISH);
      }
      break;
    }

    case Primitive::kPrimNot: {
      Register trg = trg_loc.AsRegister<Register>();
      if (kEmitCompilerReadBarrier) {
        if (kUseBakerReadBarrier) {
          Location temp = locations->GetTemp(0);
          codegen->GenerateReferenceLoadWithBakerReadBarrier(
              invoke, trg_loc, base, 0U, offset_loc, TIMES_1, temp, /* needs_null_check */ false);
          if (is_volatile) {
            __ dmb(ISH);
          }
        } else {
          __ ldr(trg, Address(base, offset));
          if (is_volatile) {
            __ dmb(ISH);
          }
          codegen->GenerateReadBarrierSlow(invoke, trg_loc, trg_loc, base_loc, 0U, offset_loc);
        }
      } else {
        __ ldr(trg, Address(base, offset));
        if (is_volatile) {
          __ dmb(ISH);
        }
        __ MaybeUnpoisonHeapReference(trg);
      }
      break;
    }

    case Primitive::kPrimLong: {
      Register trg_lo = trg_loc.AsRegisterPairLow<Register>();
      __ add(IP, base, ShifterOperand(offset));
      if (is_volatile && !codegen->GetInstructionSetFeatures().HasAtomicLdrdAndStrd()) {
        Register trg_hi = trg_loc.AsRegisterPairHigh<Register>();
        __ ldrexd(trg_lo, trg_hi, IP);
      } else {
        __ ldrd(trg_lo, Address(IP));
      }
      if (is_volatile) {
        __ dmb(ISH);
      }
      break;
    }

    default:
      LOG(FATAL) << "Unexpected type " << type;
      UNREACHABLE();
  }
}

static void CreateIntIntIntToIntLocations(ArenaAllocator* arena,
                                          HInvoke* invoke,
                                          Primitive::Type type) {
  bool can_call = kEmitCompilerReadBarrier &&
      (invoke->GetIntrinsic() == Intrinsics::kUnsafeGetObject ||
       invoke->GetIntrinsic() == Intrinsics::kUnsafeGetObjectVolatile);
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           (can_call
                                                                ? LocationSummary::kCallOnSlowPath
                                                                : LocationSummary::kNoCall),
                                                           kIntrinsified);
  if (can_call && kUseBakerReadBarrier) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(),
                    (can_call ? Location::kOutputOverlap : Location::kNoOutputOverlap));
  if (type == Primitive::kPrimNot && kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
    // We need a temporary register for the read barrier marking slow
    // path in InstructionCodeGeneratorARM::GenerateReferenceLoadWithBakerReadBarrier.
    locations->AddTemp(Location::RequiresRegister());
  }
}

void IntrinsicLocationsBuilderARM::VisitUnsafeGet(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimInt);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeGetVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimInt);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeGetLong(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimLong);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimLong);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeGetObject(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimNot);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke, Primitive::kPrimNot);
}

void IntrinsicCodeGeneratorARM::VisitUnsafeGet(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimInt, /* is_volatile */ false, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeGetVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimInt, /* is_volatile */ true, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeGetLong(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimLong, /* is_volatile */ false, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimLong, /* is_volatile */ true, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeGetObject(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimNot, /* is_volatile */ false, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimNot, /* is_volatile */ true, codegen_);
}

static void CreateIntIntIntIntToVoid(ArenaAllocator* arena,
                                     const ArmInstructionSetFeatures& features,
                                     Primitive::Type type,
                                     bool is_volatile,
                                     HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());

  if (type == Primitive::kPrimLong) {
    // Potentially need temps for ldrexd-strexd loop.
    if (is_volatile && !features.HasAtomicLdrdAndStrd()) {
      locations->AddTemp(Location::RequiresRegister());  // Temp_lo.
      locations->AddTemp(Location::RequiresRegister());  // Temp_hi.
    }
  } else if (type == Primitive::kPrimNot) {
    // Temps for card-marking.
    locations->AddTemp(Location::RequiresRegister());  // Temp.
    locations->AddTemp(Location::RequiresRegister());  // Card.
  }
}

void IntrinsicLocationsBuilderARM::VisitUnsafePut(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimInt, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimInt, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimInt, /* is_volatile */ true, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutObject(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimNot, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimNot, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, features_, Primitive::kPrimNot, /* is_volatile */ true, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutLong(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(
      arena_, features_, Primitive::kPrimLong, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(
      arena_, features_, Primitive::kPrimLong, /* is_volatile */ false, invoke);
}
void IntrinsicLocationsBuilderARM::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(
      arena_, features_, Primitive::kPrimLong, /* is_volatile */ true, invoke);
}

static void GenUnsafePut(LocationSummary* locations,
                         Primitive::Type type,
                         bool is_volatile,
                         bool is_ordered,
                         CodeGeneratorARM* codegen) {
  ArmAssembler* assembler = codegen->GetAssembler();

  Register base = locations->InAt(1).AsRegister<Register>();           // Object pointer.
  Register offset = locations->InAt(2).AsRegisterPairLow<Register>();  // Long offset, lo part only.
  Register value;

  if (is_volatile || is_ordered) {
    __ dmb(ISH);
  }

  if (type == Primitive::kPrimLong) {
    Register value_lo = locations->InAt(3).AsRegisterPairLow<Register>();
    value = value_lo;
    if (is_volatile && !codegen->GetInstructionSetFeatures().HasAtomicLdrdAndStrd()) {
      Register temp_lo = locations->GetTemp(0).AsRegister<Register>();
      Register temp_hi = locations->GetTemp(1).AsRegister<Register>();
      Register value_hi = locations->InAt(3).AsRegisterPairHigh<Register>();

      __ add(IP, base, ShifterOperand(offset));
      Label loop_head;
      __ Bind(&loop_head);
      __ ldrexd(temp_lo, temp_hi, IP);
      __ strexd(temp_lo, value_lo, value_hi, IP);
      __ cmp(temp_lo, ShifterOperand(0));
      __ b(&loop_head, NE);
    } else {
      __ add(IP, base, ShifterOperand(offset));
      __ strd(value_lo, Address(IP));
    }
  } else {
    value = locations->InAt(3).AsRegister<Register>();
    Register source = value;
    if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
      Register temp = locations->GetTemp(0).AsRegister<Register>();
      __ Mov(temp, value);
      __ PoisonHeapReference(temp);
      source = temp;
    }
    __ str(source, Address(base, offset));
  }

  if (is_volatile) {
    __ dmb(ISH);
  }

  if (type == Primitive::kPrimNot) {
    Register temp = locations->GetTemp(0).AsRegister<Register>();
    Register card = locations->GetTemp(1).AsRegister<Register>();
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(temp, card, base, value, value_can_be_null);
  }
}

void IntrinsicCodeGeneratorARM::VisitUnsafePut(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutObject(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutLong(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}

static void CreateIntIntIntIntIntToIntPlusTemps(ArenaAllocator* arena,
                                                HInvoke* invoke,
                                                Primitive::Type type) {
  bool can_call = kEmitCompilerReadBarrier &&
      kUseBakerReadBarrier &&
      (invoke->GetIntrinsic() == Intrinsics::kUnsafeCASObject);
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           (can_call
                                                                ? LocationSummary::kCallOnSlowPath
                                                                : LocationSummary::kNoCall),
                                                           kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  // If heap poisoning is enabled, we don't want the unpoisoning
  // operations to potentially clobber the output. Likewise when
  // emitting a (Baker) read barrier, which may call.
  Location::OutputOverlap overlaps =
      ((kPoisonHeapReferences && type == Primitive::kPrimNot) || can_call)
      ? Location::kOutputOverlap
      : Location::kNoOutputOverlap;
  locations->SetOut(Location::RequiresRegister(), overlaps);

  // Temporary registers used in CAS. In the object case
  // (UnsafeCASObject intrinsic), these are also used for
  // card-marking, and possibly for (Baker) read barrier.
  locations->AddTemp(Location::RequiresRegister());  // Pointer.
  locations->AddTemp(Location::RequiresRegister());  // Temp 1.
}

static void GenCas(HInvoke* invoke, Primitive::Type type, CodeGeneratorARM* codegen) {
  DCHECK_NE(type, Primitive::kPrimLong);

  ArmAssembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Location out_loc = locations->Out();
  Register out = out_loc.AsRegister<Register>();                  // Boolean result.

  Register base = locations->InAt(1).AsRegister<Register>();      // Object pointer.
  Location offset_loc = locations->InAt(2);
  Register offset = offset_loc.AsRegisterPairLow<Register>();     // Offset (discard high 4B).
  Register expected = locations->InAt(3).AsRegister<Register>();  // Expected.
  Register value = locations->InAt(4).AsRegister<Register>();     // Value.

  Location tmp_ptr_loc = locations->GetTemp(0);
  Register tmp_ptr = tmp_ptr_loc.AsRegister<Register>();          // Pointer to actual memory.
  Register tmp = locations->GetTemp(1).AsRegister<Register>();    // Value in memory.

  if (type == Primitive::kPrimNot) {
    // The only read barrier implementation supporting the
    // UnsafeCASObject intrinsic is the Baker-style read barriers.
    DCHECK(!kEmitCompilerReadBarrier || kUseBakerReadBarrier);

    // Mark card for object assuming new value is stored. Worst case we will mark an unchanged
    // object and scan the receiver at the next GC for nothing.
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(tmp_ptr, tmp, base, value, value_can_be_null);

    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      // Need to make sure the reference stored in the field is a to-space
      // one before attempting the CAS or the CAS could fail incorrectly.
      codegen->GenerateReferenceLoadWithBakerReadBarrier(
          invoke,
          out_loc,  // Unused, used only as a "temporary" within the read barrier.
          base,
          /* offset */ 0u,
          /* index */ offset_loc,
          ScaleFactor::TIMES_1,
          tmp_ptr_loc,
          /* needs_null_check */ false,
          /* always_update_field */ true,
          &tmp);
    }
  }

  // Prevent reordering with prior memory operations.
  // Emit a DMB ISH instruction instead of an DMB ISHST one, as the
  // latter allows a preceding load to be delayed past the STXR
  // instruction below.
  __ dmb(ISH);

  __ add(tmp_ptr, base, ShifterOperand(offset));

  if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
    __ PoisonHeapReference(expected);
    if (value == expected) {
      // Do not poison `value`, as it is the same register as
      // `expected`, which has just been poisoned.
    } else {
      __ PoisonHeapReference(value);
    }
  }

  // do {
  //   tmp = [r_ptr] - expected;
  // } while (tmp == 0 && failure([r_ptr] <- r_new_value));
  // result = tmp != 0;

  Label loop_head;
  __ Bind(&loop_head);

  __ ldrex(tmp, tmp_ptr);

  __ subs(tmp, tmp, ShifterOperand(expected));

  __ it(EQ, ItState::kItT);
  __ strex(tmp, value, tmp_ptr, EQ);
  __ cmp(tmp, ShifterOperand(1), EQ);

  __ b(&loop_head, EQ);

  __ dmb(ISH);

  __ rsbs(out, tmp, ShifterOperand(1));
  __ it(CC);
  __ mov(out, ShifterOperand(0), CC);

  if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
    __ UnpoisonHeapReference(expected);
    if (value == expected) {
      // Do not unpoison `value`, as it is the same register as
      // `expected`, which has just been unpoisoned.
    } else {
      __ UnpoisonHeapReference(value);
    }
  }
}

void IntrinsicLocationsBuilderARM::VisitUnsafeCASInt(HInvoke* invoke) {
  CreateIntIntIntIntIntToIntPlusTemps(arena_, invoke, Primitive::kPrimInt);
}
void IntrinsicLocationsBuilderARM::VisitUnsafeCASObject(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // UnsafeCASObject intrinsic is the Baker-style read barriers.
  if (kEmitCompilerReadBarrier && !kUseBakerReadBarrier) {
    return;
  }

  CreateIntIntIntIntIntToIntPlusTemps(arena_, invoke, Primitive::kPrimNot);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeCASInt(HInvoke* invoke) {
  GenCas(invoke, Primitive::kPrimInt, codegen_);
}
void IntrinsicCodeGeneratorARM::VisitUnsafeCASObject(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // UnsafeCASObject intrinsic is the Baker-style read barriers.
  DCHECK(!kEmitCompilerReadBarrier || kUseBakerReadBarrier);

  GenCas(invoke, Primitive::kPrimNot, codegen_);
}

void IntrinsicLocationsBuilderARM::VisitStringCompareTo(HInvoke* invoke) {
  // The inputs plus one temp.
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            invoke->InputAt(1)->CanBeNull()
                                                                ? LocationSummary::kCallOnSlowPath
                                                                : LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  // Need temporary registers for String compression's feature.
  if (mirror::kUseStringCompression) {
    locations->AddTemp(Location::RequiresRegister());
  }
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitStringCompareTo(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register str = locations->InAt(0).AsRegister<Register>();
  Register arg = locations->InAt(1).AsRegister<Register>();
  Register out = locations->Out().AsRegister<Register>();

  Register temp0 = locations->GetTemp(0).AsRegister<Register>();
  Register temp1 = locations->GetTemp(1).AsRegister<Register>();
  Register temp2 = locations->GetTemp(2).AsRegister<Register>();
  Register temp3;
  if (mirror::kUseStringCompression) {
    temp3 = locations->GetTemp(3).AsRegister<Register>();
  }

  Label loop;
  Label find_char_diff;
  Label end;
  Label different_compression;

  // Get offsets of count and value fields within a string object.
  const int32_t count_offset = mirror::String::CountOffset().Int32Value();
  const int32_t value_offset = mirror::String::ValueOffset().Int32Value();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Take slow path and throw if input can be and is null.
  SlowPathCode* slow_path = nullptr;
  const bool can_slow_path = invoke->InputAt(1)->CanBeNull();
  if (can_slow_path) {
    slow_path = new (GetAllocator()) IntrinsicSlowPathARM(invoke);
    codegen_->AddSlowPath(slow_path);
    __ CompareAndBranchIfZero(arg, slow_path->GetEntryLabel());
  }

  // Reference equality check, return 0 if same reference.
  __ subs(out, str, ShifterOperand(arg));
  __ b(&end, EQ);

  if (mirror::kUseStringCompression) {
    // Load `count` fields of this and argument strings.
    __ ldr(temp3, Address(str, count_offset));
    __ ldr(temp2, Address(arg, count_offset));
    // Extract lengths from the `count` fields.
    __ Lsr(temp0, temp3, 1u);
    __ Lsr(temp1, temp2, 1u);
  } else {
    // Load lengths of this and argument strings.
    __ ldr(temp0, Address(str, count_offset));
    __ ldr(temp1, Address(arg, count_offset));
  }
  // out = length diff.
  __ subs(out, temp0, ShifterOperand(temp1));
  // temp0 = min(len(str), len(arg)).
  __ it(GT);
  __ mov(temp0, ShifterOperand(temp1), GT);
  // Shorter string is empty?
  __ CompareAndBranchIfZero(temp0, &end);

  if (mirror::kUseStringCompression) {
    // Check if both strings using same compression style to use this comparison loop.
    __ eor(temp2, temp2, ShifterOperand(temp3));
    __ Lsrs(temp2, temp2, 1u);
    __ b(&different_compression, CS);
    // For string compression, calculate the number of bytes to compare (not chars).
    // This could in theory exceed INT32_MAX, so treat temp0 as unsigned.
    __ Lsls(temp3, temp3, 31u);  // Extract purely the compression flag.
    __ it(NE);
    __ add(temp0, temp0, ShifterOperand(temp0), NE);
  }

  // Store offset of string value in preparation for comparison loop.
  __ mov(temp1, ShifterOperand(value_offset));

  // Assertions that must hold in order to compare multiple characters at a time.
  CHECK_ALIGNED(value_offset, 8);
  static_assert(IsAligned<8>(kObjectAlignment),
                "String data must be 8-byte aligned for unrolled CompareTo loop.");

  const size_t char_size = Primitive::ComponentSize(Primitive::kPrimChar);
  DCHECK_EQ(char_size, 2u);

  Label find_char_diff_2nd_cmp;
  // Unrolled loop comparing 4x16-bit chars per iteration (ok because of string data alignment).
  __ Bind(&loop);
  __ ldr(IP, Address(str, temp1));
  __ ldr(temp2, Address(arg, temp1));
  __ cmp(IP, ShifterOperand(temp2));
  __ b(&find_char_diff, NE);
  __ add(temp1, temp1, ShifterOperand(char_size * 2));

  __ ldr(IP, Address(str, temp1));
  __ ldr(temp2, Address(arg, temp1));
  __ cmp(IP, ShifterOperand(temp2));
  __ b(&find_char_diff_2nd_cmp, NE);
  __ add(temp1, temp1, ShifterOperand(char_size * 2));
  // With string compression, we have compared 8 bytes, otherwise 4 chars.
  __ subs(temp0, temp0, ShifterOperand(mirror::kUseStringCompression ? 8 : 4));
  __ b(&loop, HI);
  __ b(&end);

  __ Bind(&find_char_diff_2nd_cmp);
  if (mirror::kUseStringCompression) {
    __ subs(temp0, temp0, ShifterOperand(4));  // 4 bytes previously compared.
    __ b(&end, LS);  // Was the second comparison fully beyond the end?
  } else {
    // Without string compression, we can start treating temp0 as signed
    // and rely on the signed comparison below.
    __ sub(temp0, temp0, ShifterOperand(2));
  }

  // Find the single character difference.
  __ Bind(&find_char_diff);
  // Get the bit position of the first character that differs.
  __ eor(temp1, temp2, ShifterOperand(IP));
  __ rbit(temp1, temp1);
  __ clz(temp1, temp1);

  // temp0 = number of characters remaining to compare.
  // (Without string compression, it could be < 1 if a difference is found by the second CMP
  // in the comparison loop, and after the end of the shorter string data).

  // Without string compression (temp1 >> 4) = character where difference occurs between the last
  // two words compared, in the interval [0,1].
  // (0 for low half-word different, 1 for high half-word different).
  // With string compression, (temp1 << 3) = byte where the difference occurs,
  // in the interval [0,3].

  // If temp0 <= (temp1 >> (kUseStringCompression ? 3 : 4)), the difference occurs outside
  // the remaining string data, so just return length diff (out).
  // The comparison is unsigned for string compression, otherwise signed.
  __ cmp(temp0, ShifterOperand(temp1, LSR, mirror::kUseStringCompression ? 3 : 4));
  __ b(&end, mirror::kUseStringCompression ? LS : LE);

  // Extract the characters and calculate the difference.
  if (mirror::kUseStringCompression) {
    // For compressed strings we need to clear 0x7 from temp1, for uncompressed we need to clear
    // 0xf. We also need to prepare the character extraction mask `uncompressed ? 0xffffu : 0xffu`.
    // The compression flag is now in the highest bit of temp3, so let's play some tricks.
    __ orr(temp3, temp3, ShifterOperand(0xffu << 23));  // uncompressed ? 0xff800000u : 0x7ff80000u
    __ bic(temp1, temp1, ShifterOperand(temp3, LSR, 31 - 3));  // &= ~(uncompressed ? 0xfu : 0x7u)
    __ Asr(temp3, temp3, 7u);                           // uncompressed ? 0xffff0000u : 0xff0000u.
    __ Lsr(temp2, temp2, temp1);                        // Extract second character.
    __ Lsr(temp3, temp3, 16u);                          // uncompressed ? 0xffffu : 0xffu
    __ Lsr(out, IP, temp1);                             // Extract first character.
    __ and_(temp2, temp2, ShifterOperand(temp3));
    __ and_(out, out, ShifterOperand(temp3));
  } else {
    __ bic(temp1, temp1, ShifterOperand(0xf));
    __ Lsr(temp2, temp2, temp1);
    __ Lsr(out, IP, temp1);
    __ movt(temp2, 0);
    __ movt(out, 0);
  }

  __ sub(out, out, ShifterOperand(temp2));

  if (mirror::kUseStringCompression) {
    __ b(&end);
    __ Bind(&different_compression);

    // Comparison for different compression style.
    const size_t c_char_size = Primitive::ComponentSize(Primitive::kPrimByte);
    DCHECK_EQ(c_char_size, 1u);

    // We want to free up the temp3, currently holding `str.count`, for comparison.
    // So, we move it to the bottom bit of the iteration count `temp0` which we tnen
    // need to treat as unsigned. Start by freeing the bit with an ADD and continue
    // further down by a LSRS+SBC which will flip the meaning of the flag but allow
    // `subs temp0, #2; bhi different_compression_loop` to serve as the loop condition.
    __ add(temp0, temp0, ShifterOperand(temp0));  // Unlike LSL, this ADD is always 16-bit.
    // `temp1` will hold the compressed data pointer, `temp2` the uncompressed data pointer.
    __ mov(temp1, ShifterOperand(str));
    __ mov(temp2, ShifterOperand(arg));
    __ Lsrs(temp3, temp3, 1u);                // Continue the move of the compression flag.
    __ it(CS, kItThen);                       // Interleave with selection of temp1 and temp2.
    __ mov(temp1, ShifterOperand(arg), CS);   // Preserves flags.
    __ mov(temp2, ShifterOperand(str), CS);   // Preserves flags.
    __ sbc(temp0, temp0, ShifterOperand(0));  // Complete the move of the compression flag.

    // Adjust temp1 and temp2 from string pointers to data pointers.
    __ add(temp1, temp1, ShifterOperand(value_offset));
    __ add(temp2, temp2, ShifterOperand(value_offset));

    Label different_compression_loop;
    Label different_compression_diff;

    // Main loop for different compression.
    __ Bind(&different_compression_loop);
    __ ldrb(IP, Address(temp1, c_char_size, Address::PostIndex));
    __ ldrh(temp3, Address(temp2, char_size, Address::PostIndex));
    __ cmp(IP, ShifterOperand(temp3));
    __ b(&different_compression_diff, NE);
    __ subs(temp0, temp0, ShifterOperand(2));
    __ b(&different_compression_loop, HI);
    __ b(&end);

    // Calculate the difference.
    __ Bind(&different_compression_diff);
    __ sub(out, IP, ShifterOperand(temp3));
    // Flip the difference if the `arg` is compressed.
    // `temp0` contains inverted `str` compression flag, i.e the same as `arg` compression flag.
    __ Lsrs(temp0, temp0, 1u);
    static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                  "Expecting 0=compressed, 1=uncompressed");
    __ it(CC);
    __ rsb(out, out, ShifterOperand(0), CC);
  }

  __ Bind(&end);

  if (can_slow_path) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARM::VisitStringEquals(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // Temporary registers to store lengths of strings and for calculations.
  // Using instruction cbz requires a low register, so explicitly set a temp to be R0.
  locations->AddTemp(Location::RegisterLocation(R0));
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());

  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARM::VisitStringEquals(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register str = locations->InAt(0).AsRegister<Register>();
  Register arg = locations->InAt(1).AsRegister<Register>();
  Register out = locations->Out().AsRegister<Register>();

  Register temp = locations->GetTemp(0).AsRegister<Register>();
  Register temp1 = locations->GetTemp(1).AsRegister<Register>();
  Register temp2 = locations->GetTemp(2).AsRegister<Register>();

  Label loop;
  Label end;
  Label return_true;
  Label return_false;
  Label* final_label = codegen_->GetFinalLabel(invoke, &end);

  // Get offsets of count, value, and class fields within a string object.
  const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
  const uint32_t value_offset = mirror::String::ValueOffset().Uint32Value();
  const uint32_t class_offset = mirror::Object::ClassOffset().Uint32Value();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  StringEqualsOptimizations optimizations(invoke);
  if (!optimizations.GetArgumentNotNull()) {
    // Check if input is null, return false if it is.
    __ CompareAndBranchIfZero(arg, &return_false);
  }

  // Reference equality check, return true if same reference.
  __ cmp(str, ShifterOperand(arg));
  __ b(&return_true, EQ);

  if (!optimizations.GetArgumentIsString()) {
    // Instanceof check for the argument by comparing class fields.
    // All string objects must have the same type since String cannot be subclassed.
    // Receiver must be a string object, so its class field is equal to all strings' class fields.
    // If the argument is a string object, its class field must be equal to receiver's class field.
    __ ldr(temp, Address(str, class_offset));
    __ ldr(temp1, Address(arg, class_offset));
    __ cmp(temp, ShifterOperand(temp1));
    __ b(&return_false, NE);
  }

  // Load `count` fields of this and argument strings.
  __ ldr(temp, Address(str, count_offset));
  __ ldr(temp1, Address(arg, count_offset));
  // Check if `count` fields are equal, return false if they're not.
  // Also compares the compression style, if differs return false.
  __ cmp(temp, ShifterOperand(temp1));
  __ b(&return_false, NE);
  // Return true if both strings are empty. Even with string compression `count == 0` means empty.
  static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                "Expecting 0=compressed, 1=uncompressed");
  __ cbz(temp, &return_true);

  // Assertions that must hold in order to compare strings 4 bytes at a time.
  DCHECK_ALIGNED(value_offset, 4);
  static_assert(IsAligned<4>(kObjectAlignment), "String data must be aligned for fast compare.");

  if (mirror::kUseStringCompression) {
    // For string compression, calculate the number of bytes to compare (not chars).
    // This could in theory exceed INT32_MAX, so treat temp as unsigned.
    __ Lsrs(temp, temp, 1u);                        // Extract length and check compression flag.
    __ it(CS);                                      // If uncompressed,
    __ add(temp, temp, ShifterOperand(temp), CS);   //   double the byte count.
  }

  // Store offset of string value in preparation for comparison loop.
  __ LoadImmediate(temp1, value_offset);

  // Loop to compare strings 4 bytes at a time starting at the front of the string.
  // Ok to do this because strings are zero-padded to kObjectAlignment.
  __ Bind(&loop);
  __ ldr(out, Address(str, temp1));
  __ ldr(temp2, Address(arg, temp1));
  __ add(temp1, temp1, ShifterOperand(sizeof(uint32_t)));
  __ cmp(out, ShifterOperand(temp2));
  __ b(&return_false, NE);
  // With string compression, we have compared 4 bytes, otherwise 2 chars.
  __ subs(temp, temp, ShifterOperand(mirror::kUseStringCompression ? 4 : 2));
  __ b(&loop, HI);

  // Return true and exit the function.
  // If loop does not result in returning false, we return true.
  __ Bind(&return_true);
  __ LoadImmediate(out, 1);
  __ b(final_label);

  // Return false and exit the function.
  __ Bind(&return_false);
  __ LoadImmediate(out, 0);

  if (end.IsLinked()) {
    __ Bind(&end);
  }
}

static void GenerateVisitStringIndexOf(HInvoke* invoke,
                                       ArmAssembler* assembler,
                                       CodeGeneratorARM* codegen,
                                       ArenaAllocator* allocator,
                                       bool start_at_zero) {
  LocationSummary* locations = invoke->GetLocations();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Check for code points > 0xFFFF. Either a slow-path check when we don't know statically,
  // or directly dispatch for a large constant, or omit slow-path for a small constant or a char.
  SlowPathCode* slow_path = nullptr;
  HInstruction* code_point = invoke->InputAt(1);
  if (code_point->IsIntConstant()) {
    if (static_cast<uint32_t>(code_point->AsIntConstant()->GetValue()) >
        std::numeric_limits<uint16_t>::max()) {
      // Always needs the slow-path. We could directly dispatch to it, but this case should be
      // rare, so for simplicity just put the full slow-path down and branch unconditionally.
      slow_path = new (allocator) IntrinsicSlowPathARM(invoke);
      codegen->AddSlowPath(slow_path);
      __ b(slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
      return;
    }
  } else if (code_point->GetType() != Primitive::kPrimChar) {
    Register char_reg = locations->InAt(1).AsRegister<Register>();
    // 0xffff is not modified immediate but 0x10000 is, so use `>= 0x10000` instead of `> 0xffff`.
    __ cmp(char_reg,
           ShifterOperand(static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1));
    slow_path = new (allocator) IntrinsicSlowPathARM(invoke);
    codegen->AddSlowPath(slow_path);
    __ b(slow_path->GetEntryLabel(), HS);
  }

  if (start_at_zero) {
    Register tmp_reg = locations->GetTemp(0).AsRegister<Register>();
    DCHECK_EQ(tmp_reg, R2);
    // Start-index = 0.
    __ LoadImmediate(tmp_reg, 0);
  }

  codegen->InvokeRuntime(kQuickIndexOf, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickIndexOf, int32_t, void*, uint32_t, uint32_t>();

  if (slow_path != nullptr) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderARM::VisitStringIndexOf(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetOut(Location::RegisterLocation(R0));

  // Need to send start-index=0.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
}

void IntrinsicCodeGeneratorARM::VisitStringIndexOf(HInvoke* invoke) {
  GenerateVisitStringIndexOf(
      invoke, GetAssembler(), codegen_, GetAllocator(), /* start_at_zero */ true);
}

void IntrinsicLocationsBuilderARM::VisitStringIndexOfAfter(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetOut(Location::RegisterLocation(R0));
}

void IntrinsicCodeGeneratorARM::VisitStringIndexOfAfter(HInvoke* invoke) {
  GenerateVisitStringIndexOf(
      invoke, GetAssembler(), codegen_, GetAllocator(), /* start_at_zero */ false);
}

void IntrinsicLocationsBuilderARM::VisitStringNewStringFromBytes(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetInAt(3, Location::RegisterLocation(calling_convention.GetRegisterAt(3)));
  locations->SetOut(Location::RegisterLocation(R0));
}

void IntrinsicCodeGeneratorARM::VisitStringNewStringFromBytes(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register byte_array = locations->InAt(0).AsRegister<Register>();
  __ cmp(byte_array, ShifterOperand(0));
  SlowPathCode* slow_path = new (GetAllocator()) IntrinsicSlowPathARM(invoke);
  codegen_->AddSlowPath(slow_path);
  __ b(slow_path->GetEntryLabel(), EQ);

  codegen_->InvokeRuntime(kQuickAllocStringFromBytes, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromBytes, void*, void*, int32_t, int32_t, int32_t>();
  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARM::VisitStringNewStringFromChars(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainOnly,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetOut(Location::RegisterLocation(R0));
}

void IntrinsicCodeGeneratorARM::VisitStringNewStringFromChars(HInvoke* invoke) {
  // No need to emit code checking whether `locations->InAt(2)` is a null
  // pointer, as callers of the native method
  //
  //   java.lang.StringFactory.newStringFromChars(int offset, int charCount, char[] data)
  //
  // all include a null check on `data` before calling that method.
  codegen_->InvokeRuntime(kQuickAllocStringFromChars, invoke, invoke->GetDexPc());
  CheckEntrypointTypes<kQuickAllocStringFromChars, void*, int32_t, int32_t, void*>();
}

void IntrinsicLocationsBuilderARM::VisitStringNewStringFromString(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetOut(Location::RegisterLocation(R0));
}

void IntrinsicCodeGeneratorARM::VisitStringNewStringFromString(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register string_to_copy = locations->InAt(0).AsRegister<Register>();
  __ cmp(string_to_copy, ShifterOperand(0));
  SlowPathCode* slow_path = new (GetAllocator()) IntrinsicSlowPathARM(invoke);
  codegen_->AddSlowPath(slow_path);
  __ b(slow_path->GetEntryLabel(), EQ);

  codegen_->InvokeRuntime(kQuickAllocStringFromString, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromString, void*, void*>();

  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARM::VisitSystemArrayCopy(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // SystemArrayCopy intrinsic is the Baker-style read barriers.
  if (kEmitCompilerReadBarrier && !kUseBakerReadBarrier) {
    return;
  }

  CodeGenerator::CreateSystemArrayCopyLocationSummary(invoke);
  LocationSummary* locations = invoke->GetLocations();
  if (locations == nullptr) {
    return;
  }

  HIntConstant* src_pos = invoke->InputAt(1)->AsIntConstant();
  HIntConstant* dest_pos = invoke->InputAt(3)->AsIntConstant();
  HIntConstant* length = invoke->InputAt(4)->AsIntConstant();

  if (src_pos != nullptr && !assembler_->ShifterOperandCanAlwaysHold(src_pos->GetValue())) {
    locations->SetInAt(1, Location::RequiresRegister());
  }
  if (dest_pos != nullptr && !assembler_->ShifterOperandCanAlwaysHold(dest_pos->GetValue())) {
    locations->SetInAt(3, Location::RequiresRegister());
  }
  if (length != nullptr && !assembler_->ShifterOperandCanAlwaysHold(length->GetValue())) {
    locations->SetInAt(4, Location::RequiresRegister());
  }
  if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
    // Temporary register IP cannot be used in
    // ReadBarrierSystemArrayCopySlowPathARM (because that register
    // is clobbered by ReadBarrierMarkRegX entry points). Get an extra
    // temporary register from the register allocator.
    locations->AddTemp(Location::RequiresRegister());
  }
}

static void CheckPosition(ArmAssembler* assembler,
                          Location pos,
                          Register input,
                          Location length,
                          SlowPathCode* slow_path,
                          Register temp,
                          bool length_is_input_length = false) {
  // Where is the length in the Array?
  const uint32_t length_offset = mirror::Array::LengthOffset().Uint32Value();

  if (pos.IsConstant()) {
    int32_t pos_const = pos.GetConstant()->AsIntConstant()->GetValue();
    if (pos_const == 0) {
      if (!length_is_input_length) {
        // Check that length(input) >= length.
        __ LoadFromOffset(kLoadWord, temp, input, length_offset);
        if (length.IsConstant()) {
          __ cmp(temp, ShifterOperand(length.GetConstant()->AsIntConstant()->GetValue()));
        } else {
          __ cmp(temp, ShifterOperand(length.AsRegister<Register>()));
        }
        __ b(slow_path->GetEntryLabel(), LT);
      }
    } else {
      // Check that length(input) >= pos.
      __ LoadFromOffset(kLoadWord, temp, input, length_offset);
      __ subs(temp, temp, ShifterOperand(pos_const));
      __ b(slow_path->GetEntryLabel(), LT);

      // Check that (length(input) - pos) >= length.
      if (length.IsConstant()) {
        __ cmp(temp, ShifterOperand(length.GetConstant()->AsIntConstant()->GetValue()));
      } else {
        __ cmp(temp, ShifterOperand(length.AsRegister<Register>()));
      }
      __ b(slow_path->GetEntryLabel(), LT);
    }
  } else if (length_is_input_length) {
    // The only way the copy can succeed is if pos is zero.
    Register pos_reg = pos.AsRegister<Register>();
    __ CompareAndBranchIfNonZero(pos_reg, slow_path->GetEntryLabel());
  } else {
    // Check that pos >= 0.
    Register pos_reg = pos.AsRegister<Register>();
    __ cmp(pos_reg, ShifterOperand(0));
    __ b(slow_path->GetEntryLabel(), LT);

    // Check that pos <= length(input).
    __ LoadFromOffset(kLoadWord, temp, input, length_offset);
    __ subs(temp, temp, ShifterOperand(pos_reg));
    __ b(slow_path->GetEntryLabel(), LT);

    // Check that (length(input) - pos) >= length.
    if (length.IsConstant()) {
      __ cmp(temp, ShifterOperand(length.GetConstant()->AsIntConstant()->GetValue()));
    } else {
      __ cmp(temp, ShifterOperand(length.AsRegister<Register>()));
    }
    __ b(slow_path->GetEntryLabel(), LT);
  }
}

void IntrinsicCodeGeneratorARM::VisitSystemArrayCopy(HInvoke* invoke) {
  // The only read barrier implementation supporting the
  // SystemArrayCopy intrinsic is the Baker-style read barriers.
  DCHECK(!kEmitCompilerReadBarrier || kUseBakerReadBarrier);

  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  uint32_t super_offset = mirror::Class::SuperClassOffset().Int32Value();
  uint32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
  uint32_t primitive_offset = mirror::Class::PrimitiveTypeOffset().Int32Value();
  uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();

  Register src = locations->InAt(0).AsRegister<Register>();
  Location src_pos = locations->InAt(1);
  Register dest = locations->InAt(2).AsRegister<Register>();
  Location dest_pos = locations->InAt(3);
  Location length = locations->InAt(4);
  Location temp1_loc = locations->GetTemp(0);
  Register temp1 = temp1_loc.AsRegister<Register>();
  Location temp2_loc = locations->GetTemp(1);
  Register temp2 = temp2_loc.AsRegister<Register>();
  Location temp3_loc = locations->GetTemp(2);
  Register temp3 = temp3_loc.AsRegister<Register>();

  SlowPathCode* intrinsic_slow_path = new (GetAllocator()) IntrinsicSlowPathARM(invoke);
  codegen_->AddSlowPath(intrinsic_slow_path);

  Label conditions_on_positions_validated;
  SystemArrayCopyOptimizations optimizations(invoke);

  // If source and destination are the same, we go to slow path if we need to do
  // forward copying.
  if (src_pos.IsConstant()) {
    int32_t src_pos_constant = src_pos.GetConstant()->AsIntConstant()->GetValue();
    if (dest_pos.IsConstant()) {
      int32_t dest_pos_constant = dest_pos.GetConstant()->AsIntConstant()->GetValue();
      if (optimizations.GetDestinationIsSource()) {
        // Checked when building locations.
        DCHECK_GE(src_pos_constant, dest_pos_constant);
      } else if (src_pos_constant < dest_pos_constant) {
        __ cmp(src, ShifterOperand(dest));
        __ b(intrinsic_slow_path->GetEntryLabel(), EQ);
      }

      // Checked when building locations.
      DCHECK(!optimizations.GetDestinationIsSource()
             || (src_pos_constant >= dest_pos.GetConstant()->AsIntConstant()->GetValue()));
    } else {
      if (!optimizations.GetDestinationIsSource()) {
        __ cmp(src, ShifterOperand(dest));
        __ b(&conditions_on_positions_validated, NE);
      }
      __ cmp(dest_pos.AsRegister<Register>(), ShifterOperand(src_pos_constant));
      __ b(intrinsic_slow_path->GetEntryLabel(), GT);
    }
  } else {
    if (!optimizations.GetDestinationIsSource()) {
      __ cmp(src, ShifterOperand(dest));
      __ b(&conditions_on_positions_validated, NE);
    }
    if (dest_pos.IsConstant()) {
      int32_t dest_pos_constant = dest_pos.GetConstant()->AsIntConstant()->GetValue();
      __ cmp(src_pos.AsRegister<Register>(), ShifterOperand(dest_pos_constant));
    } else {
      __ cmp(src_pos.AsRegister<Register>(), ShifterOperand(dest_pos.AsRegister<Register>()));
    }
    __ b(intrinsic_slow_path->GetEntryLabel(), LT);
  }

  __ Bind(&conditions_on_positions_validated);

  if (!optimizations.GetSourceIsNotNull()) {
    // Bail out if the source is null.
    __ CompareAndBranchIfZero(src, intrinsic_slow_path->GetEntryLabel());
  }

  if (!optimizations.GetDestinationIsNotNull() && !optimizations.GetDestinationIsSource()) {
    // Bail out if the destination is null.
    __ CompareAndBranchIfZero(dest, intrinsic_slow_path->GetEntryLabel());
  }

  // If the length is negative, bail out.
  // We have already checked in the LocationsBuilder for the constant case.
  if (!length.IsConstant() &&
      !optimizations.GetCountIsSourceLength() &&
      !optimizations.GetCountIsDestinationLength()) {
    __ cmp(length.AsRegister<Register>(), ShifterOperand(0));
    __ b(intrinsic_slow_path->GetEntryLabel(), LT);
  }

  // Validity checks: source.
  CheckPosition(assembler,
                src_pos,
                src,
                length,
                intrinsic_slow_path,
                temp1,
                optimizations.GetCountIsSourceLength());

  // Validity checks: dest.
  CheckPosition(assembler,
                dest_pos,
                dest,
                length,
                intrinsic_slow_path,
                temp1,
                optimizations.GetCountIsDestinationLength());

  if (!optimizations.GetDoesNotNeedTypeCheck()) {
    // Check whether all elements of the source array are assignable to the component
    // type of the destination array. We do two checks: the classes are the same,
    // or the destination is Object[]. If none of these checks succeed, we go to the
    // slow path.

    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      if (!optimizations.GetSourceIsNonPrimitiveArray()) {
        // /* HeapReference<Class> */ temp1 = src->klass_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, src, class_offset, temp2_loc, /* needs_null_check */ false);
        // Bail out if the source is not a non primitive array.
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, temp1, component_offset, temp2_loc, /* needs_null_check */ false);
        __ CompareAndBranchIfZero(temp1, intrinsic_slow_path->GetEntryLabel());
        // If heap poisoning is enabled, `temp1` has been unpoisoned
        // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
        // /* uint16_t */ temp1 = static_cast<uint16>(temp1->primitive_type_);
        __ LoadFromOffset(kLoadUnsignedHalfword, temp1, temp1, primitive_offset);
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
      }

      // /* HeapReference<Class> */ temp1 = dest->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp1_loc, dest, class_offset, temp2_loc, /* needs_null_check */ false);

      if (!optimizations.GetDestinationIsNonPrimitiveArray()) {
        // Bail out if the destination is not a non primitive array.
        //
        // Register `temp1` is not trashed by the read barrier emitted
        // by GenerateFieldLoadWithBakerReadBarrier below, as that
        // method produces a call to a ReadBarrierMarkRegX entry point,
        // which saves all potentially live registers, including
        // temporaries such a `temp1`.
        // /* HeapReference<Class> */ temp2 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp2_loc, temp1, component_offset, temp3_loc, /* needs_null_check */ false);
        __ CompareAndBranchIfZero(temp2, intrinsic_slow_path->GetEntryLabel());
        // If heap poisoning is enabled, `temp2` has been unpoisoned
        // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
        // /* uint16_t */ temp2 = static_cast<uint16>(temp2->primitive_type_);
        __ LoadFromOffset(kLoadUnsignedHalfword, temp2, temp2, primitive_offset);
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp2, intrinsic_slow_path->GetEntryLabel());
      }

      // For the same reason given earlier, `temp1` is not trashed by the
      // read barrier emitted by GenerateFieldLoadWithBakerReadBarrier below.
      // /* HeapReference<Class> */ temp2 = src->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp2_loc, src, class_offset, temp3_loc, /* needs_null_check */ false);
      // Note: if heap poisoning is on, we are comparing two unpoisoned references here.
      __ cmp(temp1, ShifterOperand(temp2));

      if (optimizations.GetDestinationIsTypedObjectArray()) {
        Label do_copy;
        __ b(&do_copy, EQ);
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            invoke, temp1_loc, temp1, component_offset, temp2_loc, /* needs_null_check */ false);
        // /* HeapReference<Class> */ temp1 = temp1->super_class_
        // We do not need to emit a read barrier for the following
        // heap reference load, as `temp1` is only used in a
        // comparison with null below, and this reference is not
        // kept afterwards.
        __ LoadFromOffset(kLoadWord, temp1, temp1, super_offset);
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
        __ Bind(&do_copy);
      } else {
        __ b(intrinsic_slow_path->GetEntryLabel(), NE);
      }
    } else {
      // Non read barrier code.

      // /* HeapReference<Class> */ temp1 = dest->klass_
      __ LoadFromOffset(kLoadWord, temp1, dest, class_offset);
      // /* HeapReference<Class> */ temp2 = src->klass_
      __ LoadFromOffset(kLoadWord, temp2, src, class_offset);
      bool did_unpoison = false;
      if (!optimizations.GetDestinationIsNonPrimitiveArray() ||
          !optimizations.GetSourceIsNonPrimitiveArray()) {
        // One or two of the references need to be unpoisoned. Unpoison them
        // both to make the identity check valid.
        __ MaybeUnpoisonHeapReference(temp1);
        __ MaybeUnpoisonHeapReference(temp2);
        did_unpoison = true;
      }

      if (!optimizations.GetDestinationIsNonPrimitiveArray()) {
        // Bail out if the destination is not a non primitive array.
        // /* HeapReference<Class> */ temp3 = temp1->component_type_
        __ LoadFromOffset(kLoadWord, temp3, temp1, component_offset);
        __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
        __ MaybeUnpoisonHeapReference(temp3);
        // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
        __ LoadFromOffset(kLoadUnsignedHalfword, temp3, temp3, primitive_offset);
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
      }

      if (!optimizations.GetSourceIsNonPrimitiveArray()) {
        // Bail out if the source is not a non primitive array.
        // /* HeapReference<Class> */ temp3 = temp2->component_type_
        __ LoadFromOffset(kLoadWord, temp3, temp2, component_offset);
        __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
        __ MaybeUnpoisonHeapReference(temp3);
        // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
        __ LoadFromOffset(kLoadUnsignedHalfword, temp3, temp3, primitive_offset);
        static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
        __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
      }

      __ cmp(temp1, ShifterOperand(temp2));

      if (optimizations.GetDestinationIsTypedObjectArray()) {
        Label do_copy;
        __ b(&do_copy, EQ);
        if (!did_unpoison) {
          __ MaybeUnpoisonHeapReference(temp1);
        }
        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        __ LoadFromOffset(kLoadWord, temp1, temp1, component_offset);
        __ MaybeUnpoisonHeapReference(temp1);
        // /* HeapReference<Class> */ temp1 = temp1->super_class_
        __ LoadFromOffset(kLoadWord, temp1, temp1, super_offset);
        // No need to unpoison the result, we're comparing against null.
        __ CompareAndBranchIfNonZero(temp1, intrinsic_slow_path->GetEntryLabel());
        __ Bind(&do_copy);
      } else {
        __ b(intrinsic_slow_path->GetEntryLabel(), NE);
      }
    }
  } else if (!optimizations.GetSourceIsNonPrimitiveArray()) {
    DCHECK(optimizations.GetDestinationIsNonPrimitiveArray());
    // Bail out if the source is not a non primitive array.
    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      // /* HeapReference<Class> */ temp1 = src->klass_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp1_loc, src, class_offset, temp2_loc, /* needs_null_check */ false);
      // /* HeapReference<Class> */ temp3 = temp1->component_type_
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          invoke, temp3_loc, temp1, component_offset, temp2_loc, /* needs_null_check */ false);
      __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
      // If heap poisoning is enabled, `temp3` has been unpoisoned
      // by the the previous call to GenerateFieldLoadWithBakerReadBarrier.
    } else {
      // /* HeapReference<Class> */ temp1 = src->klass_
      __ LoadFromOffset(kLoadWord, temp1, src, class_offset);
      __ MaybeUnpoisonHeapReference(temp1);
      // /* HeapReference<Class> */ temp3 = temp1->component_type_
      __ LoadFromOffset(kLoadWord, temp3, temp1, component_offset);
      __ CompareAndBranchIfZero(temp3, intrinsic_slow_path->GetEntryLabel());
      __ MaybeUnpoisonHeapReference(temp3);
    }
    // /* uint16_t */ temp3 = static_cast<uint16>(temp3->primitive_type_);
    __ LoadFromOffset(kLoadUnsignedHalfword, temp3, temp3, primitive_offset);
    static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
    __ CompareAndBranchIfNonZero(temp3, intrinsic_slow_path->GetEntryLabel());
  }

  if (length.IsConstant() && length.GetConstant()->AsIntConstant()->GetValue() == 0) {
    // Null constant length: not need to emit the loop code at all.
  } else {
    Label done;
    const Primitive::Type type = Primitive::kPrimNot;
    const int32_t element_size = Primitive::ComponentSize(type);

    if (length.IsRegister()) {
      // Don't enter the copy loop if the length is null.
      __ CompareAndBranchIfZero(length.AsRegister<Register>(), &done);
    }

    if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
      // TODO: Also convert this intrinsic to the IsGcMarking strategy?

      // SystemArrayCopy implementation for Baker read barriers (see
      // also CodeGeneratorARM::GenerateReferenceLoadWithBakerReadBarrier):
      //
      //   uint32_t rb_state = Lockword(src->monitor_).ReadBarrierState();
      //   lfence;  // Load fence or artificial data dependency to prevent load-load reordering
      //   bool is_gray = (rb_state == ReadBarrier::GrayState());
      //   if (is_gray) {
      //     // Slow-path copy.
      //     do {
      //       *dest_ptr++ = MaybePoison(ReadBarrier::Mark(MaybeUnpoison(*src_ptr++)));
      //     } while (src_ptr != end_ptr)
      //   } else {
      //     // Fast-path copy.
      //     do {
      //       *dest_ptr++ = *src_ptr++;
      //     } while (src_ptr != end_ptr)
      //   }

      // /* int32_t */ monitor = src->monitor_
      __ LoadFromOffset(kLoadWord, temp2, src, monitor_offset);
      // /* LockWord */ lock_word = LockWord(monitor)
      static_assert(sizeof(LockWord) == sizeof(int32_t),
                    "art::LockWord and int32_t have different sizes.");

      // Introduce a dependency on the lock_word including the rb_state,
      // which shall prevent load-load reordering without using
      // a memory barrier (which would be more expensive).
      // `src` is unchanged by this operation, but its value now depends
      // on `temp2`.
      __ add(src, src, ShifterOperand(temp2, LSR, 32));

      // Compute the base source address in `temp1`.
      // Note that `temp1` (the base source address) is computed from
      // `src` (and `src_pos`) here, and thus honors the artificial
      // dependency of `src` on `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, src, src_pos, temp1);
      // Compute the end source address in `temp3`.
      GenSystemArrayCopyEndAddress(GetAssembler(), type, length, temp1, temp3);
      // The base destination address is computed later, as `temp2` is
      // used for intermediate computations.

      // Slow path used to copy array when `src` is gray.
      // Note that the base destination address is computed in `temp2`
      // by the slow path code.
      SlowPathCode* read_barrier_slow_path =
          new (GetAllocator()) ReadBarrierSystemArrayCopySlowPathARM(invoke);
      codegen_->AddSlowPath(read_barrier_slow_path);

      // Given the numeric representation, it's enough to check the low bit of the
      // rb_state. We do that by shifting the bit out of the lock word with LSRS
      // which can be a 16-bit instruction unlike the TST immediate.
      static_assert(ReadBarrier::WhiteState() == 0, "Expecting white to have value 0");
      static_assert(ReadBarrier::GrayState() == 1, "Expecting gray to have value 1");
      __ Lsrs(temp2, temp2, LockWord::kReadBarrierStateShift + 1);
      // Carry flag is the last bit shifted out by LSRS.
      __ b(read_barrier_slow_path->GetEntryLabel(), CS);

      // Fast-path copy.
      // Compute the base destination address in `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, dest, dest_pos, temp2);
      // Iterate over the arrays and do a raw copy of the objects. We don't need to
      // poison/unpoison.
      Label loop;
      __ Bind(&loop);
      __ ldr(IP, Address(temp1, element_size, Address::PostIndex));
      __ str(IP, Address(temp2, element_size, Address::PostIndex));
      __ cmp(temp1, ShifterOperand(temp3));
      __ b(&loop, NE);

      __ Bind(read_barrier_slow_path->GetExitLabel());
    } else {
      // Non read barrier code.
      // Compute the base source address in `temp1`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, src, src_pos, temp1);
      // Compute the base destination address in `temp2`.
      GenSystemArrayCopyBaseAddress(GetAssembler(), type, dest, dest_pos, temp2);
      // Compute the end source address in `temp3`.
      GenSystemArrayCopyEndAddress(GetAssembler(), type, length, temp1, temp3);
      // Iterate over the arrays and do a raw copy of the objects. We don't need to
      // poison/unpoison.
      Label loop;
      __ Bind(&loop);
      __ ldr(IP, Address(temp1, element_size, Address::PostIndex));
      __ str(IP, Address(temp2, element_size, Address::PostIndex));
      __ cmp(temp1, ShifterOperand(temp3));
      __ b(&loop, NE);
    }
    __ Bind(&done);
  }

  // We only need one card marking on the destination array.
  codegen_->MarkGCCard(temp1, temp2, dest, Register(kNoRegister), /* value_can_be_null */ false);

  __ Bind(intrinsic_slow_path->GetExitLabel());
}

static void CreateFPToFPCallLocations(ArenaAllocator* arena, HInvoke* invoke) {
  // If the graph is debuggable, all callee-saved floating-point registers are blocked by
  // the code generator. Furthermore, the register allocator creates fixed live intervals
  // for all caller-saved registers because we are doing a function call. As a result, if
  // the input and output locations are unallocated, the register allocator runs out of
  // registers and fails; however, a debuggable graph is not the common case.
  if (invoke->GetBlock()->GetGraph()->IsDebuggable()) {
    return;
  }

  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK_EQ(invoke->InputAt(0)->GetType(), Primitive::kPrimDouble);
  DCHECK_EQ(invoke->GetType(), Primitive::kPrimDouble);

  LocationSummary* const locations = new (arena) LocationSummary(invoke,
                                                                 LocationSummary::kCallOnMainOnly,
                                                                 kIntrinsified);
  const InvokeRuntimeCallingConvention calling_convention;

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister());
  // Native code uses the soft float ABI.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
}

static void CreateFPFPToFPCallLocations(ArenaAllocator* arena, HInvoke* invoke) {
  // If the graph is debuggable, all callee-saved floating-point registers are blocked by
  // the code generator. Furthermore, the register allocator creates fixed live intervals
  // for all caller-saved registers because we are doing a function call. As a result, if
  // the input and output locations are unallocated, the register allocator runs out of
  // registers and fails; however, a debuggable graph is not the common case.
  if (invoke->GetBlock()->GetGraph()->IsDebuggable()) {
    return;
  }

  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK_EQ(invoke->InputAt(0)->GetType(), Primitive::kPrimDouble);
  DCHECK_EQ(invoke->InputAt(1)->GetType(), Primitive::kPrimDouble);
  DCHECK_EQ(invoke->GetType(), Primitive::kPrimDouble);

  LocationSummary* const locations = new (arena) LocationSummary(invoke,
                                                                 LocationSummary::kCallOnMainOnly,
                                                                 kIntrinsified);
  const InvokeRuntimeCallingConvention calling_convention;

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister());
  // Native code uses the soft float ABI.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(3)));
}

static void GenFPToFPCall(HInvoke* invoke,
                          ArmAssembler* assembler,
                          CodeGeneratorARM* codegen,
                          QuickEntrypointEnum entry) {
  LocationSummary* const locations = invoke->GetLocations();
  const InvokeRuntimeCallingConvention calling_convention;

  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK(locations->WillCall() && locations->Intrinsified());
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(0)));
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(1)));

  // Native code uses the soft float ABI.
  __ vmovrrd(calling_convention.GetRegisterAt(0),
             calling_convention.GetRegisterAt(1),
             FromLowSToD(locations->InAt(0).AsFpuRegisterPairLow<SRegister>()));
  codegen->InvokeRuntime(entry, invoke, invoke->GetDexPc());
  __ vmovdrr(FromLowSToD(locations->Out().AsFpuRegisterPairLow<SRegister>()),
             calling_convention.GetRegisterAt(0),
             calling_convention.GetRegisterAt(1));
}

static void GenFPFPToFPCall(HInvoke* invoke,
                          ArmAssembler* assembler,
                          CodeGeneratorARM* codegen,
                          QuickEntrypointEnum entry) {
  LocationSummary* const locations = invoke->GetLocations();
  const InvokeRuntimeCallingConvention calling_convention;

  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK(locations->WillCall() && locations->Intrinsified());
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(0)));
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(1)));
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(2)));
  DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(calling_convention.GetRegisterAt(3)));

  // Native code uses the soft float ABI.
  __ vmovrrd(calling_convention.GetRegisterAt(0),
             calling_convention.GetRegisterAt(1),
             FromLowSToD(locations->InAt(0).AsFpuRegisterPairLow<SRegister>()));
  __ vmovrrd(calling_convention.GetRegisterAt(2),
             calling_convention.GetRegisterAt(3),
             FromLowSToD(locations->InAt(1).AsFpuRegisterPairLow<SRegister>()));
  codegen->InvokeRuntime(entry, invoke, invoke->GetDexPc());
  __ vmovdrr(FromLowSToD(locations->Out().AsFpuRegisterPairLow<SRegister>()),
             calling_convention.GetRegisterAt(0),
             calling_convention.GetRegisterAt(1));
}

void IntrinsicLocationsBuilderARM::VisitMathCos(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathCos(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCos);
}

void IntrinsicLocationsBuilderARM::VisitMathSin(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathSin(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickSin);
}

void IntrinsicLocationsBuilderARM::VisitMathAcos(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAcos(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAcos);
}

void IntrinsicLocationsBuilderARM::VisitMathAsin(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAsin(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAsin);
}

void IntrinsicLocationsBuilderARM::VisitMathAtan(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAtan(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAtan);
}

void IntrinsicLocationsBuilderARM::VisitMathCbrt(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathCbrt(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCbrt);
}

void IntrinsicLocationsBuilderARM::VisitMathCosh(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathCosh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickCosh);
}

void IntrinsicLocationsBuilderARM::VisitMathExp(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathExp(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickExp);
}

void IntrinsicLocationsBuilderARM::VisitMathExpm1(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathExpm1(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickExpm1);
}

void IntrinsicLocationsBuilderARM::VisitMathLog(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathLog(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickLog);
}

void IntrinsicLocationsBuilderARM::VisitMathLog10(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathLog10(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickLog10);
}

void IntrinsicLocationsBuilderARM::VisitMathSinh(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathSinh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickSinh);
}

void IntrinsicLocationsBuilderARM::VisitMathTan(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathTan(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickTan);
}

void IntrinsicLocationsBuilderARM::VisitMathTanh(HInvoke* invoke) {
  CreateFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathTanh(HInvoke* invoke) {
  GenFPToFPCall(invoke, GetAssembler(), codegen_, kQuickTanh);
}

void IntrinsicLocationsBuilderARM::VisitMathAtan2(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathAtan2(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickAtan2);
}

void IntrinsicLocationsBuilderARM::VisitMathHypot(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathHypot(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickHypot);
}

void IntrinsicLocationsBuilderARM::VisitMathNextAfter(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitMathNextAfter(HInvoke* invoke) {
  GenFPFPToFPCall(invoke, GetAssembler(), codegen_, kQuickNextAfter);
}

void IntrinsicLocationsBuilderARM::VisitIntegerReverse(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitIntegerReverse(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register out = locations->Out().AsRegister<Register>();
  Register in  = locations->InAt(0).AsRegister<Register>();

  __ rbit(out, in);
}

void IntrinsicLocationsBuilderARM::VisitLongReverse(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitLongReverse(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register in_reg_lo  = locations->InAt(0).AsRegisterPairLow<Register>();
  Register in_reg_hi  = locations->InAt(0).AsRegisterPairHigh<Register>();
  Register out_reg_lo = locations->Out().AsRegisterPairLow<Register>();
  Register out_reg_hi = locations->Out().AsRegisterPairHigh<Register>();

  __ rbit(out_reg_lo, in_reg_hi);
  __ rbit(out_reg_hi, in_reg_lo);
}

void IntrinsicLocationsBuilderARM::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitIntegerReverseBytes(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register out = locations->Out().AsRegister<Register>();
  Register in  = locations->InAt(0).AsRegister<Register>();

  __ rev(out, in);
}

void IntrinsicLocationsBuilderARM::VisitLongReverseBytes(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorARM::VisitLongReverseBytes(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register in_reg_lo  = locations->InAt(0).AsRegisterPairLow<Register>();
  Register in_reg_hi  = locations->InAt(0).AsRegisterPairHigh<Register>();
  Register out_reg_lo = locations->Out().AsRegisterPairLow<Register>();
  Register out_reg_hi = locations->Out().AsRegisterPairHigh<Register>();

  __ rev(out_reg_lo, in_reg_hi);
  __ rev(out_reg_hi, in_reg_lo);
}

void IntrinsicLocationsBuilderARM::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitShortReverseBytes(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register out = locations->Out().AsRegister<Register>();
  Register in  = locations->InAt(0).AsRegister<Register>();

  __ revsh(out, in);
}

static void GenBitCount(HInvoke* instr, Primitive::Type type, ArmAssembler* assembler) {
  DCHECK(Primitive::IsIntOrLongType(type)) << type;
  DCHECK_EQ(instr->GetType(), Primitive::kPrimInt);
  DCHECK_EQ(Primitive::PrimitiveKind(instr->InputAt(0)->GetType()), type);

  bool is_long = type == Primitive::kPrimLong;
  LocationSummary* locations = instr->GetLocations();
  Location in = locations->InAt(0);
  Register src_0 = is_long ? in.AsRegisterPairLow<Register>() : in.AsRegister<Register>();
  Register src_1 = is_long ? in.AsRegisterPairHigh<Register>() : src_0;
  SRegister tmp_s = locations->GetTemp(0).AsFpuRegisterPairLow<SRegister>();
  DRegister tmp_d = FromLowSToD(tmp_s);
  Register  out_r = locations->Out().AsRegister<Register>();

  // Move data from core register(s) to temp D-reg for bit count calculation, then move back.
  // According to Cortex A57 and A72 optimization guides, compared to transferring to full D-reg,
  // transferring data from core reg to upper or lower half of vfp D-reg requires extra latency,
  // That's why for integer bit count, we use 'vmov d0, r0, r0' instead of 'vmov d0[0], r0'.
  __ vmovdrr(tmp_d, src_1, src_0);                         // Temp DReg |--src_1|--src_0|
  __ vcntd(tmp_d, tmp_d);                                  // Temp DReg |c|c|c|c|c|c|c|c|
  __ vpaddld(tmp_d, tmp_d, 8, /* is_unsigned */ true);     // Temp DReg |--c|--c|--c|--c|
  __ vpaddld(tmp_d, tmp_d, 16, /* is_unsigned */ true);    // Temp DReg |------c|------c|
  if (is_long) {
    __ vpaddld(tmp_d, tmp_d, 32, /* is_unsigned */ true);  // Temp DReg |--------------c|
  }
  __ vmovrs(out_r, tmp_s);
}

void IntrinsicLocationsBuilderARM::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
  invoke->GetLocations()->AddTemp(Location::RequiresFpuRegister());
}

void IntrinsicCodeGeneratorARM::VisitIntegerBitCount(HInvoke* invoke) {
  GenBitCount(invoke, Primitive::kPrimInt, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitLongBitCount(HInvoke* invoke) {
  VisitIntegerBitCount(invoke);
}

void IntrinsicCodeGeneratorARM::VisitLongBitCount(HInvoke* invoke) {
  GenBitCount(invoke, Primitive::kPrimLong, GetAssembler());
}

void IntrinsicLocationsBuilderARM::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  // Temporary registers to store lengths of strings and for calculations.
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARM::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  ArmAssembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  // Check assumption that sizeof(Char) is 2 (used in scaling below).
  const size_t char_size = Primitive::ComponentSize(Primitive::kPrimChar);
  DCHECK_EQ(char_size, 2u);

  // Location of data in char array buffer.
  const uint32_t data_offset = mirror::Array::DataOffset(char_size).Uint32Value();

  // Location of char array data in string.
  const uint32_t value_offset = mirror::String::ValueOffset().Uint32Value();

  // void getCharsNoCheck(int srcBegin, int srcEnd, char[] dst, int dstBegin);
  // Since getChars() calls getCharsNoCheck() - we use registers rather than constants.
  Register srcObj = locations->InAt(0).AsRegister<Register>();
  Register srcBegin = locations->InAt(1).AsRegister<Register>();
  Register srcEnd = locations->InAt(2).AsRegister<Register>();
  Register dstObj = locations->InAt(3).AsRegister<Register>();
  Register dstBegin = locations->InAt(4).AsRegister<Register>();

  Register num_chr = locations->GetTemp(0).AsRegister<Register>();
  Register src_ptr = locations->GetTemp(1).AsRegister<Register>();
  Register dst_ptr = locations->GetTemp(2).AsRegister<Register>();

  Label done, compressed_string_loop;
  Label* final_label = codegen_->GetFinalLabel(invoke, &done);
  // dst to be copied.
  __ add(dst_ptr, dstObj, ShifterOperand(data_offset));
  __ add(dst_ptr, dst_ptr, ShifterOperand(dstBegin, LSL, 1));

  __ subs(num_chr, srcEnd, ShifterOperand(srcBegin));
  // Early out for valid zero-length retrievals.
  __ b(final_label, EQ);

  // src range to copy.
  __ add(src_ptr, srcObj, ShifterOperand(value_offset));
  Label compressed_string_preloop;
  if (mirror::kUseStringCompression) {
    // Location of count in string.
    const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
    // String's length.
    __ ldr(IP, Address(srcObj, count_offset));
    __ tst(IP, ShifterOperand(1));
    __ b(&compressed_string_preloop, EQ);
  }
  __ add(src_ptr, src_ptr, ShifterOperand(srcBegin, LSL, 1));

  // Do the copy.
  Label loop, remainder;

  // Save repairing the value of num_chr on the < 4 character path.
  __ subs(IP, num_chr, ShifterOperand(4));
  __ b(&remainder, LT);

  // Keep the result of the earlier subs, we are going to fetch at least 4 characters.
  __ mov(num_chr, ShifterOperand(IP));

  // Main loop used for longer fetches loads and stores 4x16-bit characters at a time.
  // (LDRD/STRD fault on unaligned addresses and it's not worth inlining extra code
  // to rectify these everywhere this intrinsic applies.)
  __ Bind(&loop);
  __ ldr(IP, Address(src_ptr, char_size * 2));
  __ subs(num_chr, num_chr, ShifterOperand(4));
  __ str(IP, Address(dst_ptr, char_size * 2));
  __ ldr(IP, Address(src_ptr, char_size * 4, Address::PostIndex));
  __ str(IP, Address(dst_ptr, char_size * 4, Address::PostIndex));
  __ b(&loop, GE);

  __ adds(num_chr, num_chr, ShifterOperand(4));
  __ b(final_label, EQ);

  // Main loop for < 4 character case and remainder handling. Loads and stores one
  // 16-bit Java character at a time.
  __ Bind(&remainder);
  __ ldrh(IP, Address(src_ptr, char_size, Address::PostIndex));
  __ subs(num_chr, num_chr, ShifterOperand(1));
  __ strh(IP, Address(dst_ptr, char_size, Address::PostIndex));
  __ b(&remainder, GT);

  if (mirror::kUseStringCompression) {
    __ b(final_label);

    const size_t c_char_size = Primitive::ComponentSize(Primitive::kPrimByte);
    DCHECK_EQ(c_char_size, 1u);
    // Copy loop for compressed src, copying 1 character (8-bit) to (16-bit) at a time.
    __ Bind(&compressed_string_preloop);
    __ add(src_ptr, src_ptr, ShifterOperand(srcBegin));
    __ Bind(&compressed_string_loop);
    __ ldrb(IP, Address(src_ptr, c_char_size, Address::PostIndex));
    __ strh(IP, Address(dst_ptr, char_size, Address::PostIndex));
    __ subs(num_chr, num_chr, ShifterOperand(1));
    __ b(&compressed_string_loop, GT);
  }

  if (done.IsLinked()) {
    __ Bind(&done);
  }
}

void IntrinsicLocationsBuilderARM::VisitFloatIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitFloatIsInfinite(HInvoke* invoke) {
  ArmAssembler* const assembler = GetAssembler();
  LocationSummary* const locations = invoke->GetLocations();
  const Register out = locations->Out().AsRegister<Register>();
  // Shifting left by 1 bit makes the value encodable as an immediate operand;
  // we don't care about the sign bit anyway.
  constexpr uint32_t infinity = kPositiveInfinityFloat << 1U;

  __ vmovrs(out, locations->InAt(0).AsFpuRegister<SRegister>());
  // We don't care about the sign bit, so shift left.
  __ Lsl(out, out, 1);
  __ eor(out, out, ShifterOperand(infinity));
  // If the result is 0, then it has 32 leading zeros, and less than that otherwise.
  __ clz(out, out);
  // Any number less than 32 logically shifted right by 5 bits results in 0;
  // the same operation on 32 yields 1.
  __ Lsr(out, out, 5);
}

void IntrinsicLocationsBuilderARM::VisitDoubleIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorARM::VisitDoubleIsInfinite(HInvoke* invoke) {
  ArmAssembler* const assembler = GetAssembler();
  LocationSummary* const locations = invoke->GetLocations();
  const Register out = locations->Out().AsRegister<Register>();
  // The highest 32 bits of double precision positive infinity separated into
  // two constants encodable as immediate operands.
  constexpr uint32_t infinity_high  = 0x7f000000U;
  constexpr uint32_t infinity_high2 = 0x00f00000U;

  static_assert((infinity_high | infinity_high2) == static_cast<uint32_t>(kPositiveInfinityDouble >> 32U),
                "The constants do not add up to the high 32 bits of double precision positive infinity.");
  __ vmovrrd(IP, out, FromLowSToD(locations->InAt(0).AsFpuRegisterPairLow<SRegister>()));
  __ eor(out, out, ShifterOperand(infinity_high));
  __ eor(out, out, ShifterOperand(infinity_high2));
  // We don't care about the sign bit, so shift left.
  __ orr(out, IP, ShifterOperand(out, LSL, 1));
  // If the result is 0, then it has 32 leading zeros, and less than that otherwise.
  __ clz(out, out);
  // Any number less than 32 logically shifted right by 5 bits results in 0;
  // the same operation on 32 yields 1.
  __ Lsr(out, out, 5);
}

void IntrinsicLocationsBuilderARM::VisitReferenceGetReferent(HInvoke* invoke) {
  if (kEmitCompilerReadBarrier) {
    // Do not intrinsify this call with the read barrier configuration.
    return;
  }
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnSlowPath,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::SameAsFirstInput());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorARM::VisitReferenceGetReferent(HInvoke* invoke) {
  DCHECK(!kEmitCompilerReadBarrier);
  ArmAssembler* const assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  Register obj = locations->InAt(0).AsRegister<Register>();
  Register out = locations->Out().AsRegister<Register>();

  SlowPathCode* slow_path = new (GetAllocator()) IntrinsicSlowPathARM(invoke);
  codegen_->AddSlowPath(slow_path);

  // Load ArtMethod first.
  HInvokeStaticOrDirect* invoke_direct = invoke->AsInvokeStaticOrDirect();
  DCHECK(invoke_direct != nullptr);
  Register temp = codegen_->GenerateCalleeMethodStaticOrDirectCall(
      invoke_direct, locations->GetTemp(0)).AsRegister<Register>();

  // Now get declaring class.
  __ ldr(temp, Address(temp, ArtMethod::DeclaringClassOffset().Int32Value()));

  uint32_t slow_path_flag_offset = codegen_->GetReferenceSlowFlagOffset();
  uint32_t disable_flag_offset = codegen_->GetReferenceDisableFlagOffset();
  DCHECK_NE(slow_path_flag_offset, 0u);
  DCHECK_NE(disable_flag_offset, 0u);
  DCHECK_NE(slow_path_flag_offset, disable_flag_offset);

  // Check static flags that prevent using intrinsic.
  __ ldr(IP, Address(temp, disable_flag_offset));
  __ ldr(temp, Address(temp, slow_path_flag_offset));
  __ orr(IP, IP, ShifterOperand(temp));
  __ CompareAndBranchIfNonZero(IP, slow_path->GetEntryLabel());

  // Fast path.
  __ ldr(out, Address(obj, mirror::Reference::ReferentOffset().Int32Value()));
  codegen_->MaybeRecordImplicitNullCheck(invoke);
  __ MaybeUnpoisonHeapReference(out);
  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderARM::VisitIntegerValueOf(HInvoke* invoke) {
  InvokeRuntimeCallingConvention calling_convention;
  IntrinsicVisitor::ComputeIntegerValueOfLocations(
      invoke,
      codegen_,
      Location::RegisterLocation(R0),
      Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
}

void IntrinsicCodeGeneratorARM::VisitIntegerValueOf(HInvoke* invoke) {
  IntrinsicVisitor::IntegerValueOfInfo info = IntrinsicVisitor::ComputeIntegerValueOfInfo();
  LocationSummary* locations = invoke->GetLocations();
  ArmAssembler* const assembler = GetAssembler();

  Register out = locations->Out().AsRegister<Register>();
  InvokeRuntimeCallingConvention calling_convention;
  Register argument = calling_convention.GetRegisterAt(0);
  if (invoke->InputAt(0)->IsConstant()) {
    int32_t value = invoke->InputAt(0)->AsIntConstant()->GetValue();
    if (value >= info.low && value <= info.high) {
      // Just embed the j.l.Integer in the code.
      ScopedObjectAccess soa(Thread::Current());
      mirror::Object* boxed = info.cache->Get(value + (-info.low));
      DCHECK(boxed != nullptr && Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(boxed));
      uint32_t address = dchecked_integral_cast<uint32_t>(reinterpret_cast<uintptr_t>(boxed));
      __ LoadLiteral(out, codegen_->DeduplicateBootImageAddressLiteral(address));
    } else {
      // Allocate and initialize a new j.l.Integer.
      // TODO: If we JIT, we could allocate the j.l.Integer now, and store it in the
      // JIT object table.
      uint32_t address =
          dchecked_integral_cast<uint32_t>(reinterpret_cast<uintptr_t>(info.integer));
      __ LoadLiteral(argument, codegen_->DeduplicateBootImageAddressLiteral(address));
      codegen_->InvokeRuntime(kQuickAllocObjectInitialized, invoke, invoke->GetDexPc());
      CheckEntrypointTypes<kQuickAllocObjectWithChecks, void*, mirror::Class*>();
      __ LoadImmediate(IP, value);
      __ StoreToOffset(kStoreWord, IP, out, info.value_offset);
      // `value` is a final field :-( Ideally, we'd merge this memory barrier with the allocation
      // one.
      codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    }
  } else {
    Register in = locations->InAt(0).AsRegister<Register>();
    // Check bounds of our cache.
    __ AddConstant(out, in, -info.low);
    __ CmpConstant(out, info.high - info.low + 1);
    Label allocate, done;
    __ b(&allocate, HS);
    // If the value is within the bounds, load the j.l.Integer directly from the array.
    uint32_t data_offset = mirror::Array::DataOffset(kHeapReferenceSize).Uint32Value();
    uint32_t address = dchecked_integral_cast<uint32_t>(reinterpret_cast<uintptr_t>(info.cache));
    __ LoadLiteral(IP, codegen_->DeduplicateBootImageAddressLiteral(data_offset + address));
    codegen_->LoadFromShiftedRegOffset(Primitive::kPrimNot, locations->Out(), IP, out);
    __ MaybeUnpoisonHeapReference(out);
    __ b(&done);
    __ Bind(&allocate);
    // Otherwise allocate and initialize a new j.l.Integer.
    address = dchecked_integral_cast<uint32_t>(reinterpret_cast<uintptr_t>(info.integer));
    __ LoadLiteral(argument, codegen_->DeduplicateBootImageAddressLiteral(address));
    codegen_->InvokeRuntime(kQuickAllocObjectInitialized, invoke, invoke->GetDexPc());
    CheckEntrypointTypes<kQuickAllocObjectWithChecks, void*, mirror::Class*>();
    __ StoreToOffset(kStoreWord, in, out, info.value_offset);
    // `value` is a final field :-( Ideally, we'd merge this memory barrier with the allocation
    // one.
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    __ Bind(&done);
  }
}

UNIMPLEMENTED_INTRINSIC(ARM, MathMinDoubleDouble)
UNIMPLEMENTED_INTRINSIC(ARM, MathMinFloatFloat)
UNIMPLEMENTED_INTRINSIC(ARM, MathMaxDoubleDouble)
UNIMPLEMENTED_INTRINSIC(ARM, MathMaxFloatFloat)
UNIMPLEMENTED_INTRINSIC(ARM, MathMinLongLong)
UNIMPLEMENTED_INTRINSIC(ARM, MathMaxLongLong)
UNIMPLEMENTED_INTRINSIC(ARM, MathCeil)          // Could be done by changing rounding mode, maybe?
UNIMPLEMENTED_INTRINSIC(ARM, MathFloor)         // Could be done by changing rounding mode, maybe?
UNIMPLEMENTED_INTRINSIC(ARM, MathRint)
UNIMPLEMENTED_INTRINSIC(ARM, MathRoundDouble)   // Could be done by changing rounding mode, maybe?
UNIMPLEMENTED_INTRINSIC(ARM, MathRoundFloat)    // Could be done by changing rounding mode, maybe?
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeCASLong)     // High register pressure.
UNIMPLEMENTED_INTRINSIC(ARM, SystemArrayCopyChar)
UNIMPLEMENTED_INTRINSIC(ARM, IntegerHighestOneBit)
UNIMPLEMENTED_INTRINSIC(ARM, LongHighestOneBit)
UNIMPLEMENTED_INTRINSIC(ARM, IntegerLowestOneBit)
UNIMPLEMENTED_INTRINSIC(ARM, LongLowestOneBit)

UNIMPLEMENTED_INTRINSIC(ARM, StringStringIndexOf);
UNIMPLEMENTED_INTRINSIC(ARM, StringStringIndexOfAfter);
UNIMPLEMENTED_INTRINSIC(ARM, StringBufferAppend);
UNIMPLEMENTED_INTRINSIC(ARM, StringBufferLength);
UNIMPLEMENTED_INTRINSIC(ARM, StringBufferToString);
UNIMPLEMENTED_INTRINSIC(ARM, StringBuilderAppend);
UNIMPLEMENTED_INTRINSIC(ARM, StringBuilderLength);
UNIMPLEMENTED_INTRINSIC(ARM, StringBuilderToString);

// 1.8.
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeGetAndAddInt)
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeGetAndAddLong)
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeGetAndSetInt)
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeGetAndSetLong)
UNIMPLEMENTED_INTRINSIC(ARM, UnsafeGetAndSetObject)

UNREACHABLE_INTRINSICS(ARM)

#undef __

}  // namespace arm
}  // namespace art
