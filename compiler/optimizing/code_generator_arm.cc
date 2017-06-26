/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "code_generator_arm.h"

#include "arch/arm/instruction_set_features_arm.h"
#include "art_method.h"
#include "code_generator_utils.h"
#include "common_arm.h"
#include "compiled_method.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "gc/accounting/card_table.h"
#include "intrinsics.h"
#include "intrinsics_arm.h"
#include "mirror/array-inl.h"
#include "mirror/class-inl.h"
#include "thread.h"
#include "utils/arm/assembler_arm.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/assembler.h"
#include "utils/stack_checks.h"

namespace art {

template<class MirrorType>
class GcRoot;

namespace arm {

static bool ExpectedPairLayout(Location location) {
  // We expected this for both core and fpu register pairs.
  return ((location.low() & 1) == 0) && (location.low() + 1 == location.high());
}

static constexpr int kCurrentMethodStackOffset = 0;
static constexpr Register kMethodRegisterArgument = R0;

static constexpr Register kCoreAlwaysSpillRegister = R5;
static constexpr Register kCoreCalleeSaves[] =
    { R5, R6, R7, R8, R10, R11, LR };
static constexpr SRegister kFpuCalleeSaves[] =
    { S16, S17, S18, S19, S20, S21, S22, S23, S24, S25, S26, S27, S28, S29, S30, S31 };

// D31 cannot be split into two S registers, and the register allocator only works on
// S registers. Therefore there is no need to block it.
static constexpr DRegister DTMP = D31;

static constexpr uint32_t kPackedSwitchCompareJumpThreshold = 7;

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<ArmAssembler*>(codegen->GetAssembler())->  // NOLINT
#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kArmPointerSize, x).Int32Value()

static constexpr int kRegListThreshold = 4;

// SaveLiveRegisters and RestoreLiveRegisters from SlowPathCodeARM operate on sets of S registers,
// for each live D registers they treat two corresponding S registers as live ones.
//
// Two following functions (SaveContiguousSRegisterList, RestoreContiguousSRegisterList) build
// from a list of contiguous S registers a list of contiguous D registers (processing first/last
// S registers corner cases) and save/restore this new list treating them as D registers.
// - decreasing code size
// - avoiding hazards on Cortex-A57, when a pair of S registers for an actual live D register is
//   restored and then used in regular non SlowPath code as D register.
//
// For the following example (v means the S register is live):
//   D names: |    D0   |    D1   |    D2   |    D4   | ...
//   S names: | S0 | S1 | S2 | S3 | S4 | S5 | S6 | S7 | ...
//   Live?    |    |  v |  v |  v |  v |  v |  v |    | ...
//
// S1 and S6 will be saved/restored independently; D registers list (D1, D2) will be processed
// as D registers.
static size_t SaveContiguousSRegisterList(size_t first,
                                          size_t last,
                                          CodeGenerator* codegen,
                                          size_t stack_offset) {
  DCHECK_LE(first, last);
  if ((first == last) && (first == 0)) {
    stack_offset += codegen->SaveFloatingPointRegister(stack_offset, first);
    return stack_offset;
  }
  if (first % 2 == 1) {
    stack_offset += codegen->SaveFloatingPointRegister(stack_offset, first++);
  }

  bool save_last = false;
  if (last % 2 == 0) {
    save_last = true;
    --last;
  }

  if (first < last) {
    DRegister d_reg = static_cast<DRegister>(first / 2);
    DCHECK_EQ((last - first + 1) % 2, 0u);
    size_t number_of_d_regs = (last - first + 1) / 2;

    if (number_of_d_regs == 1) {
      __ StoreDToOffset(d_reg, SP, stack_offset);
    } else if (number_of_d_regs > 1) {
      __ add(IP, SP, ShifterOperand(stack_offset));
      __ vstmiad(IP, d_reg, number_of_d_regs);
    }
    stack_offset += number_of_d_regs * kArmWordSize * 2;
  }

  if (save_last) {
    stack_offset += codegen->SaveFloatingPointRegister(stack_offset, last + 1);
  }

  return stack_offset;
}

static size_t RestoreContiguousSRegisterList(size_t first,
                                             size_t last,
                                             CodeGenerator* codegen,
                                             size_t stack_offset) {
  DCHECK_LE(first, last);
  if ((first == last) && (first == 0)) {
    stack_offset += codegen->RestoreFloatingPointRegister(stack_offset, first);
    return stack_offset;
  }
  if (first % 2 == 1) {
    stack_offset += codegen->RestoreFloatingPointRegister(stack_offset, first++);
  }

  bool restore_last = false;
  if (last % 2 == 0) {
    restore_last = true;
    --last;
  }

  if (first < last) {
    DRegister d_reg = static_cast<DRegister>(first / 2);
    DCHECK_EQ((last - first + 1) % 2, 0u);
    size_t number_of_d_regs = (last - first + 1) / 2;
    if (number_of_d_regs == 1) {
      __ LoadDFromOffset(d_reg, SP, stack_offset);
    } else if (number_of_d_regs > 1) {
      __ add(IP, SP, ShifterOperand(stack_offset));
      __ vldmiad(IP, d_reg, number_of_d_regs);
    }
    stack_offset += number_of_d_regs * kArmWordSize * 2;
  }

  if (restore_last) {
    stack_offset += codegen->RestoreFloatingPointRegister(stack_offset, last + 1);
  }

  return stack_offset;
}

void SlowPathCodeARM::SaveLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) {
  size_t stack_offset = codegen->GetFirstRegisterSlotInSlowPath();
  size_t orig_offset = stack_offset;

  const uint32_t core_spills = codegen->GetSlowPathSpills(locations, /* core_registers */ true);
  for (uint32_t i : LowToHighBits(core_spills)) {
    // If the register holds an object, update the stack mask.
    if (locations->RegisterContainsObject(i)) {
      locations->SetStackBit(stack_offset / kVRegSize);
    }
    DCHECK_LT(stack_offset, codegen->GetFrameSize() - codegen->FrameEntrySpillSize());
    DCHECK_LT(i, kMaximumNumberOfExpectedRegisters);
    saved_core_stack_offsets_[i] = stack_offset;
    stack_offset += kArmWordSize;
  }

  int reg_num = POPCOUNT(core_spills);
  if (reg_num != 0) {
    if (reg_num > kRegListThreshold) {
      __ StoreList(RegList(core_spills), orig_offset);
    } else {
      stack_offset = orig_offset;
      for (uint32_t i : LowToHighBits(core_spills)) {
        stack_offset += codegen->SaveCoreRegister(stack_offset, i);
      }
    }
  }

  uint32_t fp_spills = codegen->GetSlowPathSpills(locations, /* core_registers */ false);
  orig_offset = stack_offset;
  for (uint32_t i : LowToHighBits(fp_spills)) {
    DCHECK_LT(i, kMaximumNumberOfExpectedRegisters);
    saved_fpu_stack_offsets_[i] = stack_offset;
    stack_offset += kArmWordSize;
  }

  stack_offset = orig_offset;
  while (fp_spills != 0u) {
    uint32_t begin = CTZ(fp_spills);
    uint32_t tmp = fp_spills + (1u << begin);
    fp_spills &= tmp;  // Clear the contiguous range of 1s.
    uint32_t end = (tmp == 0u) ? 32u : CTZ(tmp);  // CTZ(0) is undefined.
    stack_offset = SaveContiguousSRegisterList(begin, end - 1, codegen, stack_offset);
  }
  DCHECK_LE(stack_offset, codegen->GetFrameSize() - codegen->FrameEntrySpillSize());
}

void SlowPathCodeARM::RestoreLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) {
  size_t stack_offset = codegen->GetFirstRegisterSlotInSlowPath();
  size_t orig_offset = stack_offset;

  const uint32_t core_spills = codegen->GetSlowPathSpills(locations, /* core_registers */ true);
  for (uint32_t i : LowToHighBits(core_spills)) {
    DCHECK_LT(stack_offset, codegen->GetFrameSize() - codegen->FrameEntrySpillSize());
    DCHECK_LT(i, kMaximumNumberOfExpectedRegisters);
    stack_offset += kArmWordSize;
  }

  int reg_num = POPCOUNT(core_spills);
  if (reg_num != 0) {
    if (reg_num > kRegListThreshold) {
      __ LoadList(RegList(core_spills), orig_offset);
    } else {
      stack_offset = orig_offset;
      for (uint32_t i : LowToHighBits(core_spills)) {
        stack_offset += codegen->RestoreCoreRegister(stack_offset, i);
      }
    }
  }

  uint32_t fp_spills = codegen->GetSlowPathSpills(locations, /* core_registers */ false);
  while (fp_spills != 0u) {
    uint32_t begin = CTZ(fp_spills);
    uint32_t tmp = fp_spills + (1u << begin);
    fp_spills &= tmp;  // Clear the contiguous range of 1s.
    uint32_t end = (tmp == 0u) ? 32u : CTZ(tmp);  // CTZ(0) is undefined.
    stack_offset = RestoreContiguousSRegisterList(begin, end - 1, codegen, stack_offset);
  }
  DCHECK_LE(stack_offset, codegen->GetFrameSize() - codegen->FrameEntrySpillSize());
}

class NullCheckSlowPathARM : public SlowPathCodeARM {
 public:
  explicit NullCheckSlowPathARM(HNullCheck* instruction) : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    arm_codegen->InvokeRuntime(kQuickThrowNullPointer,
                               instruction_,
                               instruction_->GetDexPc(),
                               this);
    CheckEntrypointTypes<kQuickThrowNullPointer, void, void>();
  }

  bool IsFatal() const OVERRIDE { return true; }

  const char* GetDescription() const OVERRIDE { return "NullCheckSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullCheckSlowPathARM);
};

class DivZeroCheckSlowPathARM : public SlowPathCodeARM {
 public:
  explicit DivZeroCheckSlowPathARM(HDivZeroCheck* instruction) : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    arm_codegen->InvokeRuntime(kQuickThrowDivZero, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowDivZero, void, void>();
  }

  bool IsFatal() const OVERRIDE { return true; }

  const char* GetDescription() const OVERRIDE { return "DivZeroCheckSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DivZeroCheckSlowPathARM);
};

class SuspendCheckSlowPathARM : public SlowPathCodeARM {
 public:
  SuspendCheckSlowPathARM(HSuspendCheck* instruction, HBasicBlock* successor)
      : SlowPathCodeARM(instruction), successor_(successor) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    arm_codegen->InvokeRuntime(kQuickTestSuspend, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickTestSuspend, void, void>();
    if (successor_ == nullptr) {
      __ b(GetReturnLabel());
    } else {
      __ b(arm_codegen->GetLabelOf(successor_));
    }
  }

  Label* GetReturnLabel() {
    DCHECK(successor_ == nullptr);
    return &return_label_;
  }

  HBasicBlock* GetSuccessor() const {
    return successor_;
  }

  const char* GetDescription() const OVERRIDE { return "SuspendCheckSlowPathARM"; }

 private:
  // If not null, the block to branch to after the suspend check.
  HBasicBlock* const successor_;

  // If `successor_` is null, the label to branch to after the suspend check.
  Label return_label_;

  DISALLOW_COPY_AND_ASSIGN(SuspendCheckSlowPathARM);
};

class BoundsCheckSlowPathARM : public SlowPathCodeARM {
 public:
  explicit BoundsCheckSlowPathARM(HBoundsCheck* instruction)
      : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    LocationSummary* locations = instruction_->GetLocations();

    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    // We're moving two locations to locations that could overlap, so we need a parallel
    // move resolver.
    InvokeRuntimeCallingConvention calling_convention;
    codegen->EmitParallelMoves(
        locations->InAt(0),
        Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
        Primitive::kPrimInt,
        locations->InAt(1),
        Location::RegisterLocation(calling_convention.GetRegisterAt(1)),
        Primitive::kPrimInt);
    QuickEntrypointEnum entrypoint = instruction_->AsBoundsCheck()->IsStringCharAt()
        ? kQuickThrowStringBounds
        : kQuickThrowArrayBounds;
    arm_codegen->InvokeRuntime(entrypoint, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowStringBounds, void, int32_t, int32_t>();
    CheckEntrypointTypes<kQuickThrowArrayBounds, void, int32_t, int32_t>();
  }

  bool IsFatal() const OVERRIDE { return true; }

  const char* GetDescription() const OVERRIDE { return "BoundsCheckSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(BoundsCheckSlowPathARM);
};

class LoadClassSlowPathARM : public SlowPathCodeARM {
 public:
  LoadClassSlowPathARM(HLoadClass* cls, HInstruction* at, uint32_t dex_pc, bool do_clinit)
      : SlowPathCodeARM(at), cls_(cls), dex_pc_(dex_pc), do_clinit_(do_clinit) {
    DCHECK(at->IsLoadClass() || at->IsClinitCheck());
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    Location out = locations->Out();
    constexpr bool call_saves_everything_except_r0 = (!kUseReadBarrier || kUseBakerReadBarrier);

    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    InvokeRuntimeCallingConvention calling_convention;
    // For HLoadClass/kBssEntry/kSaveEverything, make sure we preserve the address of the entry.
    DCHECK_EQ(instruction_->IsLoadClass(), cls_ == instruction_);
    bool is_load_class_bss_entry =
        (cls_ == instruction_) && (cls_->GetLoadKind() == HLoadClass::LoadKind::kBssEntry);
    Register entry_address = kNoRegister;
    if (is_load_class_bss_entry && call_saves_everything_except_r0) {
      Register temp = locations->GetTemp(0).AsRegister<Register>();
      // In the unlucky case that the `temp` is R0, we preserve the address in `out` across
      // the kSaveEverything call.
      bool temp_is_r0 = (temp == calling_convention.GetRegisterAt(0));
      entry_address = temp_is_r0 ? out.AsRegister<Register>() : temp;
      DCHECK_NE(entry_address, calling_convention.GetRegisterAt(0));
      if (temp_is_r0) {
        __ mov(entry_address, ShifterOperand(temp));
      }
    }
    dex::TypeIndex type_index = cls_->GetTypeIndex();
    __ LoadImmediate(calling_convention.GetRegisterAt(0), type_index.index_);
    QuickEntrypointEnum entrypoint = do_clinit_ ? kQuickInitializeStaticStorage
                                                : kQuickInitializeType;
    arm_codegen->InvokeRuntime(entrypoint, instruction_, dex_pc_, this);
    if (do_clinit_) {
      CheckEntrypointTypes<kQuickInitializeStaticStorage, void*, uint32_t>();
    } else {
      CheckEntrypointTypes<kQuickInitializeType, void*, uint32_t>();
    }

    // For HLoadClass/kBssEntry, store the resolved Class to the BSS entry.
    if (is_load_class_bss_entry) {
      if (call_saves_everything_except_r0) {
        // The class entry address was preserved in `entry_address` thanks to kSaveEverything.
        __ str(R0, Address(entry_address));
      } else {
        // For non-Baker read barrier, we need to re-calculate the address of the string entry.
        Register temp = IP;
        CodeGeneratorARM::PcRelativePatchInfo* labels =
            arm_codegen->NewTypeBssEntryPatch(cls_->GetDexFile(), type_index);
        __ BindTrackedLabel(&labels->movw_label);
        __ movw(temp, /* placeholder */ 0u);
        __ BindTrackedLabel(&labels->movt_label);
        __ movt(temp, /* placeholder */ 0u);
        __ BindTrackedLabel(&labels->add_pc_label);
        __ add(temp, temp, ShifterOperand(PC));
        __ str(R0, Address(temp));
      }
    }
    // Move the class to the desired location.
    if (out.IsValid()) {
      DCHECK(out.IsRegister() && !locations->GetLiveRegisters()->ContainsCoreRegister(out.reg()));
      arm_codegen->Move32(locations->Out(), Location::RegisterLocation(R0));
    }
    RestoreLiveRegisters(codegen, locations);
    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "LoadClassSlowPathARM"; }

 private:
  // The class this slow path will load.
  HLoadClass* const cls_;

  // The dex PC of `at_`.
  const uint32_t dex_pc_;

  // Whether to initialize the class.
  const bool do_clinit_;

  DISALLOW_COPY_AND_ASSIGN(LoadClassSlowPathARM);
};

class LoadStringSlowPathARM : public SlowPathCodeARM {
 public:
  explicit LoadStringSlowPathARM(HLoadString* instruction) : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    DCHECK(instruction_->IsLoadString());
    DCHECK_EQ(instruction_->AsLoadString()->GetLoadKind(), HLoadString::LoadKind::kBssEntry);
    LocationSummary* locations = instruction_->GetLocations();
    DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(locations->Out().reg()));
    HLoadString* load = instruction_->AsLoadString();
    const dex::StringIndex string_index = load->GetStringIndex();
    Register out = locations->Out().AsRegister<Register>();
    constexpr bool call_saves_everything_except_r0 = (!kUseReadBarrier || kUseBakerReadBarrier);

    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    InvokeRuntimeCallingConvention calling_convention;
    // In the unlucky case that the `temp` is R0, we preserve the address in `out` across
    // the kSaveEverything call.
    Register entry_address = kNoRegister;
    if (call_saves_everything_except_r0) {
      Register temp = locations->GetTemp(0).AsRegister<Register>();
      bool temp_is_r0 = (temp == calling_convention.GetRegisterAt(0));
      entry_address = temp_is_r0 ? out : temp;
      DCHECK_NE(entry_address, calling_convention.GetRegisterAt(0));
      if (temp_is_r0) {
        __ mov(entry_address, ShifterOperand(temp));
      }
    }

    __ LoadImmediate(calling_convention.GetRegisterAt(0), string_index.index_);
    arm_codegen->InvokeRuntime(kQuickResolveString, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickResolveString, void*, uint32_t>();

    // Store the resolved String to the .bss entry.
    if (call_saves_everything_except_r0) {
      // The string entry address was preserved in `entry_address` thanks to kSaveEverything.
      __ str(R0, Address(entry_address));
    } else {
      // For non-Baker read barrier, we need to re-calculate the address of the string entry.
      Register temp = IP;
      CodeGeneratorARM::PcRelativePatchInfo* labels =
          arm_codegen->NewPcRelativeStringPatch(load->GetDexFile(), string_index);
      __ BindTrackedLabel(&labels->movw_label);
      __ movw(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->movt_label);
      __ movt(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->add_pc_label);
      __ add(temp, temp, ShifterOperand(PC));
      __ str(R0, Address(temp));
    }

    arm_codegen->Move32(locations->Out(), Location::RegisterLocation(R0));
    RestoreLiveRegisters(codegen, locations);

    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "LoadStringSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadStringSlowPathARM);
};

class TypeCheckSlowPathARM : public SlowPathCodeARM {
 public:
  TypeCheckSlowPathARM(HInstruction* instruction, bool is_fatal)
      : SlowPathCodeARM(instruction), is_fatal_(is_fatal) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    DCHECK(instruction_->IsCheckCast()
           || !locations->GetLiveRegisters()->ContainsCoreRegister(locations->Out().reg()));

    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());

    if (!is_fatal_) {
      SaveLiveRegisters(codegen, locations);
    }

    // We're moving two locations to locations that could overlap, so we need a parallel
    // move resolver.
    InvokeRuntimeCallingConvention calling_convention;
    codegen->EmitParallelMoves(locations->InAt(0),
                               Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
                               Primitive::kPrimNot,
                               locations->InAt(1),
                               Location::RegisterLocation(calling_convention.GetRegisterAt(1)),
                               Primitive::kPrimNot);
    if (instruction_->IsInstanceOf()) {
      arm_codegen->InvokeRuntime(kQuickInstanceofNonTrivial,
                                 instruction_,
                                 instruction_->GetDexPc(),
                                 this);
      CheckEntrypointTypes<kQuickInstanceofNonTrivial, size_t, mirror::Object*, mirror::Class*>();
      arm_codegen->Move32(locations->Out(), Location::RegisterLocation(R0));
    } else {
      DCHECK(instruction_->IsCheckCast());
      arm_codegen->InvokeRuntime(kQuickCheckInstanceOf,
                                 instruction_,
                                 instruction_->GetDexPc(),
                                 this);
      CheckEntrypointTypes<kQuickCheckInstanceOf, void, mirror::Object*, mirror::Class*>();
    }

    if (!is_fatal_) {
      RestoreLiveRegisters(codegen, locations);
      __ b(GetExitLabel());
    }
  }

  const char* GetDescription() const OVERRIDE { return "TypeCheckSlowPathARM"; }

  bool IsFatal() const OVERRIDE { return is_fatal_; }

 private:
  const bool is_fatal_;

  DISALLOW_COPY_AND_ASSIGN(TypeCheckSlowPathARM);
};

class DeoptimizationSlowPathARM : public SlowPathCodeARM {
 public:
  explicit DeoptimizationSlowPathARM(HDeoptimize* instruction)
    : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    __ Bind(GetEntryLabel());
    LocationSummary* locations = instruction_->GetLocations();
    SaveLiveRegisters(codegen, locations);
    InvokeRuntimeCallingConvention calling_convention;
    __ LoadImmediate(calling_convention.GetRegisterAt(0),
                     static_cast<uint32_t>(instruction_->AsDeoptimize()->GetDeoptimizationKind()));
    arm_codegen->InvokeRuntime(kQuickDeoptimize, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickDeoptimize, void, DeoptimizationKind>();
  }

  const char* GetDescription() const OVERRIDE { return "DeoptimizationSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DeoptimizationSlowPathARM);
};

class ArraySetSlowPathARM : public SlowPathCodeARM {
 public:
  explicit ArraySetSlowPathARM(HInstruction* instruction) : SlowPathCodeARM(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    InvokeRuntimeCallingConvention calling_convention;
    HParallelMove parallel_move(codegen->GetGraph()->GetArena());
    parallel_move.AddMove(
        locations->InAt(0),
        Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
        Primitive::kPrimNot,
        nullptr);
    parallel_move.AddMove(
        locations->InAt(1),
        Location::RegisterLocation(calling_convention.GetRegisterAt(1)),
        Primitive::kPrimInt,
        nullptr);
    parallel_move.AddMove(
        locations->InAt(2),
        Location::RegisterLocation(calling_convention.GetRegisterAt(2)),
        Primitive::kPrimNot,
        nullptr);
    codegen->GetMoveResolver()->EmitNativeCode(&parallel_move);

    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    arm_codegen->InvokeRuntime(kQuickAputObject, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickAputObject, void, mirror::Array*, int32_t, mirror::Object*>();
    RestoreLiveRegisters(codegen, locations);
    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "ArraySetSlowPathARM"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(ArraySetSlowPathARM);
};

// Abstract base class for read barrier slow paths marking a reference
// `ref`.
//
// Argument `entrypoint` must be a register location holding the read
// barrier marking runtime entry point to be invoked.
class ReadBarrierMarkSlowPathBaseARM : public SlowPathCodeARM {
 protected:
  ReadBarrierMarkSlowPathBaseARM(HInstruction* instruction, Location ref, Location entrypoint)
      : SlowPathCodeARM(instruction), ref_(ref), entrypoint_(entrypoint) {
    DCHECK(kEmitCompilerReadBarrier);
  }

  const char* GetDescription() const OVERRIDE { return "ReadBarrierMarkSlowPathBaseARM"; }

  // Generate assembly code calling the read barrier marking runtime
  // entry point (ReadBarrierMarkRegX).
  void GenerateReadBarrierMarkRuntimeCall(CodeGenerator* codegen) {
    Register ref_reg = ref_.AsRegister<Register>();

    // No need to save live registers; it's taken care of by the
    // entrypoint. Also, there is no need to update the stack mask,
    // as this runtime call will not trigger a garbage collection.
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    DCHECK_NE(ref_reg, SP);
    DCHECK_NE(ref_reg, LR);
    DCHECK_NE(ref_reg, PC);
    // IP is used internally by the ReadBarrierMarkRegX entry point
    // as a temporary, it cannot be the entry point's input/output.
    DCHECK_NE(ref_reg, IP);
    DCHECK(0 <= ref_reg && ref_reg < kNumberOfCoreRegisters) << ref_reg;
    // "Compact" slow path, saving two moves.
    //
    // Instead of using the standard runtime calling convention (input
    // and output in R0):
    //
    //   R0 <- ref
    //   R0 <- ReadBarrierMark(R0)
    //   ref <- R0
    //
    // we just use rX (the register containing `ref`) as input and output
    // of a dedicated entrypoint:
    //
    //   rX <- ReadBarrierMarkRegX(rX)
    //
    if (entrypoint_.IsValid()) {
      arm_codegen->ValidateInvokeRuntimeWithoutRecordingPcInfo(instruction_, this);
      __ blx(entrypoint_.AsRegister<Register>());
    } else {
      // Entrypoint is not already loaded, load from the thread.
      int32_t entry_point_offset =
          CodeGenerator::GetReadBarrierMarkEntryPointsOffset<kArmPointerSize>(ref_reg);
      // This runtime call does not require a stack map.
      arm_codegen->InvokeRuntimeWithoutRecordingPcInfo(entry_point_offset, instruction_, this);
    }
  }

  // The location (register) of the marked object reference.
  const Location ref_;

  // The location of the entrypoint if it is already loaded.
  const Location entrypoint_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ReadBarrierMarkSlowPathBaseARM);
};

// Slow path marking an object reference `ref` during a read
// barrier. The field `obj.field` in the object `obj` holding this
// reference does not get updated by this slow path after marking.
//
// This means that after the execution of this slow path, `ref` will
// always be up-to-date, but `obj.field` may not; i.e., after the
// flip, `ref` will be a to-space reference, but `obj.field` will
// probably still be a from-space reference (unless it gets updated by
// another thread, or if another thread installed another object
// reference (different from `ref`) in `obj.field`).
//
// If `entrypoint` is a valid location it is assumed to already be
// holding the entrypoint. The case where the entrypoint is passed in
// is when the decision to mark is based on whether the GC is marking.
class ReadBarrierMarkSlowPathARM : public ReadBarrierMarkSlowPathBaseARM {
 public:
  ReadBarrierMarkSlowPathARM(HInstruction* instruction,
                             Location ref,
                             Location entrypoint = Location::NoLocation())
      : ReadBarrierMarkSlowPathBaseARM(instruction, ref, entrypoint) {
    DCHECK(kEmitCompilerReadBarrier);
  }

  const char* GetDescription() const OVERRIDE { return "ReadBarrierMarkSlowPathARM"; }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    DCHECK(locations->CanCall());
    if (kIsDebugBuild) {
      Register ref_reg = ref_.AsRegister<Register>();
      DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(ref_reg)) << ref_reg;
    }
    DCHECK(instruction_->IsLoadClass() || instruction_->IsLoadString())
        << "Unexpected instruction in read barrier marking slow path: "
        << instruction_->DebugName();

    __ Bind(GetEntryLabel());
    GenerateReadBarrierMarkRuntimeCall(codegen);
    __ b(GetExitLabel());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ReadBarrierMarkSlowPathARM);
};

// Slow path loading `obj`'s lock word, loading a reference from
// object `*(obj + offset + (index << scale_factor))` into `ref`, and
// marking `ref` if `obj` is gray according to the lock word (Baker
// read barrier). The field `obj.field` in the object `obj` holding
// this reference does not get updated by this slow path after marking
// (see LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM
// below for that).
//
// This means that after the execution of this slow path, `ref` will
// always be up-to-date, but `obj.field` may not; i.e., after the
// flip, `ref` will be a to-space reference, but `obj.field` will
// probably still be a from-space reference (unless it gets updated by
// another thread, or if another thread installed another object
// reference (different from `ref`) in `obj.field`).
//
// Argument `entrypoint` must be a register location holding the read
// barrier marking runtime entry point to be invoked.
class LoadReferenceWithBakerReadBarrierSlowPathARM : public ReadBarrierMarkSlowPathBaseARM {
 public:
  LoadReferenceWithBakerReadBarrierSlowPathARM(HInstruction* instruction,
                                               Location ref,
                                               Register obj,
                                               uint32_t offset,
                                               Location index,
                                               ScaleFactor scale_factor,
                                               bool needs_null_check,
                                               Register temp,
                                               Location entrypoint)
      : ReadBarrierMarkSlowPathBaseARM(instruction, ref, entrypoint),
        obj_(obj),
        offset_(offset),
        index_(index),
        scale_factor_(scale_factor),
        needs_null_check_(needs_null_check),
        temp_(temp) {
    DCHECK(kEmitCompilerReadBarrier);
    DCHECK(kUseBakerReadBarrier);
  }

  const char* GetDescription() const OVERRIDE {
    return "LoadReferenceWithBakerReadBarrierSlowPathARM";
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    Register ref_reg = ref_.AsRegister<Register>();
    DCHECK(locations->CanCall());
    DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(ref_reg)) << ref_reg;
    DCHECK_NE(ref_reg, temp_);
    DCHECK(instruction_->IsInstanceFieldGet() ||
           instruction_->IsStaticFieldGet() ||
           instruction_->IsArrayGet() ||
           instruction_->IsArraySet() ||
           instruction_->IsInstanceOf() ||
           instruction_->IsCheckCast() ||
           (instruction_->IsInvokeVirtual() && instruction_->GetLocations()->Intrinsified()) ||
           (instruction_->IsInvokeStaticOrDirect() && instruction_->GetLocations()->Intrinsified()))
        << "Unexpected instruction in read barrier marking slow path: "
        << instruction_->DebugName();
    // The read barrier instrumentation of object ArrayGet
    // instructions does not support the HIntermediateAddress
    // instruction.
    DCHECK(!(instruction_->IsArrayGet() &&
             instruction_->AsArrayGet()->GetArray()->IsIntermediateAddress()));

    __ Bind(GetEntryLabel());

    // When using MaybeGenerateReadBarrierSlow, the read barrier call is
    // inserted after the original load. However, in fast path based
    // Baker's read barriers, we need to perform the load of
    // mirror::Object::monitor_ *before* the original reference load.
    // This load-load ordering is required by the read barrier.
    // The fast path/slow path (for Baker's algorithm) should look like:
    //
    //   uint32_t rb_state = Lockword(obj->monitor_).ReadBarrierState();
    //   lfence;  // Load fence or artificial data dependency to prevent load-load reordering
    //   HeapReference<mirror::Object> ref = *src;  // Original reference load.
    //   bool is_gray = (rb_state == ReadBarrier::GrayState());
    //   if (is_gray) {
    //     ref = entrypoint(ref);  // ref = ReadBarrier::Mark(ref);  // Runtime entry point call.
    //   }
    //
    // Note: the original implementation in ReadBarrier::Barrier is
    // slightly more complex as it performs additional checks that we do
    // not do here for performance reasons.

    // /* int32_t */ monitor = obj->monitor_
    uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();
    __ LoadFromOffset(kLoadWord, temp_, obj_, monitor_offset);
    if (needs_null_check_) {
      codegen->MaybeRecordImplicitNullCheck(instruction_);
    }
    // /* LockWord */ lock_word = LockWord(monitor)
    static_assert(sizeof(LockWord) == sizeof(int32_t),
                  "art::LockWord and int32_t have different sizes.");

    // Introduce a dependency on the lock_word including the rb_state,
    // which shall prevent load-load reordering without using
    // a memory barrier (which would be more expensive).
    // `obj` is unchanged by this operation, but its value now depends
    // on `temp`.
    __ add(obj_, obj_, ShifterOperand(temp_, LSR, 32));

    // The actual reference load.
    // A possible implicit null check has already been handled above.
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    arm_codegen->GenerateRawReferenceLoad(
        instruction_, ref_, obj_, offset_, index_, scale_factor_, /* needs_null_check */ false);

    // Mark the object `ref` when `obj` is gray.
    //
    // if (rb_state == ReadBarrier::GrayState())
    //   ref = ReadBarrier::Mark(ref);
    //
    // Given the numeric representation, it's enough to check the low bit of the
    // rb_state. We do that by shifting the bit out of the lock word with LSRS
    // which can be a 16-bit instruction unlike the TST immediate.
    static_assert(ReadBarrier::WhiteState() == 0, "Expecting white to have value 0");
    static_assert(ReadBarrier::GrayState() == 1, "Expecting gray to have value 1");
    __ Lsrs(temp_, temp_, LockWord::kReadBarrierStateShift + 1);
    __ b(GetExitLabel(), CC);  // Carry flag is the last bit shifted out by LSRS.
    GenerateReadBarrierMarkRuntimeCall(codegen);

    __ b(GetExitLabel());
  }

 private:
  // The register containing the object holding the marked object reference field.
  Register obj_;
  // The offset, index and scale factor to access the reference in `obj_`.
  uint32_t offset_;
  Location index_;
  ScaleFactor scale_factor_;
  // Is a null check required?
  bool needs_null_check_;
  // A temporary register used to hold the lock word of `obj_`.
  Register temp_;

  DISALLOW_COPY_AND_ASSIGN(LoadReferenceWithBakerReadBarrierSlowPathARM);
};

// Slow path loading `obj`'s lock word, loading a reference from
// object `*(obj + offset + (index << scale_factor))` into `ref`, and
// marking `ref` if `obj` is gray according to the lock word (Baker
// read barrier). If needed, this slow path also atomically updates
// the field `obj.field` in the object `obj` holding this reference
// after marking (contrary to
// LoadReferenceWithBakerReadBarrierSlowPathARM above, which never
// tries to update `obj.field`).
//
// This means that after the execution of this slow path, both `ref`
// and `obj.field` will be up-to-date; i.e., after the flip, both will
// hold the same to-space reference (unless another thread installed
// another object reference (different from `ref`) in `obj.field`).
//
// Argument `entrypoint` must be a register location holding the read
// barrier marking runtime entry point to be invoked.
class LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM
    : public ReadBarrierMarkSlowPathBaseARM {
 public:
  LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM(HInstruction* instruction,
                                                             Location ref,
                                                             Register obj,
                                                             uint32_t offset,
                                                             Location index,
                                                             ScaleFactor scale_factor,
                                                             bool needs_null_check,
                                                             Register temp1,
                                                             Register temp2,
                                                             Location entrypoint)
      : ReadBarrierMarkSlowPathBaseARM(instruction, ref, entrypoint),
        obj_(obj),
        offset_(offset),
        index_(index),
        scale_factor_(scale_factor),
        needs_null_check_(needs_null_check),
        temp1_(temp1),
        temp2_(temp2) {
    DCHECK(kEmitCompilerReadBarrier);
    DCHECK(kUseBakerReadBarrier);
  }

  const char* GetDescription() const OVERRIDE {
    return "LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM";
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    Register ref_reg = ref_.AsRegister<Register>();
    DCHECK(locations->CanCall());
    DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(ref_reg)) << ref_reg;
    DCHECK_NE(ref_reg, temp1_);

    // This slow path is only used by the UnsafeCASObject intrinsic at the moment.
    DCHECK((instruction_->IsInvokeVirtual() && instruction_->GetLocations()->Intrinsified()))
        << "Unexpected instruction in read barrier marking and field updating slow path: "
        << instruction_->DebugName();
    DCHECK(instruction_->GetLocations()->Intrinsified());
    DCHECK_EQ(instruction_->AsInvoke()->GetIntrinsic(), Intrinsics::kUnsafeCASObject);
    DCHECK_EQ(offset_, 0u);
    DCHECK_EQ(scale_factor_, ScaleFactor::TIMES_1);
    // The location of the offset of the marked reference field within `obj_`.
    Location field_offset = index_;
    DCHECK(field_offset.IsRegisterPair()) << field_offset;

    __ Bind(GetEntryLabel());

    // /* int32_t */ monitor = obj->monitor_
    uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();
    __ LoadFromOffset(kLoadWord, temp1_, obj_, monitor_offset);
    if (needs_null_check_) {
      codegen->MaybeRecordImplicitNullCheck(instruction_);
    }
    // /* LockWord */ lock_word = LockWord(monitor)
    static_assert(sizeof(LockWord) == sizeof(int32_t),
                  "art::LockWord and int32_t have different sizes.");

    // Introduce a dependency on the lock_word including the rb_state,
    // which shall prevent load-load reordering without using
    // a memory barrier (which would be more expensive).
    // `obj` is unchanged by this operation, but its value now depends
    // on `temp1`.
    __ add(obj_, obj_, ShifterOperand(temp1_, LSR, 32));

    // The actual reference load.
    // A possible implicit null check has already been handled above.
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    arm_codegen->GenerateRawReferenceLoad(
        instruction_, ref_, obj_, offset_, index_, scale_factor_, /* needs_null_check */ false);

    // Mark the object `ref` when `obj` is gray.
    //
    // if (rb_state == ReadBarrier::GrayState())
    //   ref = ReadBarrier::Mark(ref);
    //
    // Given the numeric representation, it's enough to check the low bit of the
    // rb_state. We do that by shifting the bit out of the lock word with LSRS
    // which can be a 16-bit instruction unlike the TST immediate.
    static_assert(ReadBarrier::WhiteState() == 0, "Expecting white to have value 0");
    static_assert(ReadBarrier::GrayState() == 1, "Expecting gray to have value 1");
    __ Lsrs(temp1_, temp1_, LockWord::kReadBarrierStateShift + 1);
    __ b(GetExitLabel(), CC);  // Carry flag is the last bit shifted out by LSRS.

    // Save the old value of the reference before marking it.
    // Note that we cannot use IP to save the old reference, as IP is
    // used internally by the ReadBarrierMarkRegX entry point, and we
    // need the old reference after the call to that entry point.
    DCHECK_NE(temp1_, IP);
    __ Mov(temp1_, ref_reg);

    GenerateReadBarrierMarkRuntimeCall(codegen);

    // If the new reference is different from the old reference,
    // update the field in the holder (`*(obj_ + field_offset)`).
    //
    // Note that this field could also hold a different object, if
    // another thread had concurrently changed it. In that case, the
    // LDREX/SUBS/ITNE sequence of instructions in the compare-and-set
    // (CAS) operation below would abort the CAS, leaving the field
    // as-is.
    __ cmp(temp1_, ShifterOperand(ref_reg));
    __ b(GetExitLabel(), EQ);

    // Update the the holder's field atomically.  This may fail if
    // mutator updates before us, but it's OK.  This is achieved
    // using a strong compare-and-set (CAS) operation with relaxed
    // memory synchronization ordering, where the expected value is
    // the old reference and the desired value is the new reference.

    // Convenience aliases.
    Register base = obj_;
    // The UnsafeCASObject intrinsic uses a register pair as field
    // offset ("long offset"), of which only the low part contains
    // data.
    Register offset = field_offset.AsRegisterPairLow<Register>();
    Register expected = temp1_;
    Register value = ref_reg;
    Register tmp_ptr = IP;       // Pointer to actual memory.
    Register tmp = temp2_;       // Value in memory.

    __ add(tmp_ptr, base, ShifterOperand(offset));

    if (kPoisonHeapReferences) {
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

    Label loop_head, exit_loop;
    __ Bind(&loop_head);

    __ ldrex(tmp, tmp_ptr);

    __ subs(tmp, tmp, ShifterOperand(expected));

    __ it(NE);
    __ clrex(NE);

    __ b(&exit_loop, NE);

    __ strex(tmp, value, tmp_ptr);
    __ cmp(tmp, ShifterOperand(1));
    __ b(&loop_head, EQ);

    __ Bind(&exit_loop);

    if (kPoisonHeapReferences) {
      __ UnpoisonHeapReference(expected);
      if (value == expected) {
        // Do not unpoison `value`, as it is the same register as
        // `expected`, which has just been unpoisoned.
      } else {
        __ UnpoisonHeapReference(value);
      }
    }

    __ b(GetExitLabel());
  }

 private:
  // The register containing the object holding the marked object reference field.
  const Register obj_;
  // The offset, index and scale factor to access the reference in `obj_`.
  uint32_t offset_;
  Location index_;
  ScaleFactor scale_factor_;
  // Is a null check required?
  bool needs_null_check_;
  // A temporary register used to hold the lock word of `obj_`; and
  // also to hold the original reference value, when the reference is
  // marked.
  const Register temp1_;
  // A temporary register used in the implementation of the CAS, to
  // update the object's reference field.
  const Register temp2_;

  DISALLOW_COPY_AND_ASSIGN(LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM);
};

// Slow path generating a read barrier for a heap reference.
class ReadBarrierForHeapReferenceSlowPathARM : public SlowPathCodeARM {
 public:
  ReadBarrierForHeapReferenceSlowPathARM(HInstruction* instruction,
                                         Location out,
                                         Location ref,
                                         Location obj,
                                         uint32_t offset,
                                         Location index)
      : SlowPathCodeARM(instruction),
        out_(out),
        ref_(ref),
        obj_(obj),
        offset_(offset),
        index_(index) {
    DCHECK(kEmitCompilerReadBarrier);
    // If `obj` is equal to `out` or `ref`, it means the initial object
    // has been overwritten by (or after) the heap object reference load
    // to be instrumented, e.g.:
    //
    //   __ LoadFromOffset(kLoadWord, out, out, offset);
    //   codegen_->GenerateReadBarrierSlow(instruction, out_loc, out_loc, out_loc, offset);
    //
    // In that case, we have lost the information about the original
    // object, and the emitted read barrier cannot work properly.
    DCHECK(!obj.Equals(out)) << "obj=" << obj << " out=" << out;
    DCHECK(!obj.Equals(ref)) << "obj=" << obj << " ref=" << ref;
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    LocationSummary* locations = instruction_->GetLocations();
    Register reg_out = out_.AsRegister<Register>();
    DCHECK(locations->CanCall());
    DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(reg_out));
    DCHECK(instruction_->IsInstanceFieldGet() ||
           instruction_->IsStaticFieldGet() ||
           instruction_->IsArrayGet() ||
           instruction_->IsInstanceOf() ||
           instruction_->IsCheckCast() ||
           (instruction_->IsInvokeVirtual() && instruction_->GetLocations()->Intrinsified()))
        << "Unexpected instruction in read barrier for heap reference slow path: "
        << instruction_->DebugName();
    // The read barrier instrumentation of object ArrayGet
    // instructions does not support the HIntermediateAddress
    // instruction.
    DCHECK(!(instruction_->IsArrayGet() &&
             instruction_->AsArrayGet()->GetArray()->IsIntermediateAddress()));

    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    // We may have to change the index's value, but as `index_` is a
    // constant member (like other "inputs" of this slow path),
    // introduce a copy of it, `index`.
    Location index = index_;
    if (index_.IsValid()) {
      // Handle `index_` for HArrayGet and UnsafeGetObject/UnsafeGetObjectVolatile intrinsics.
      if (instruction_->IsArrayGet()) {
        // Compute the actual memory offset and store it in `index`.
        Register index_reg = index_.AsRegister<Register>();
        DCHECK(locations->GetLiveRegisters()->ContainsCoreRegister(index_reg));
        if (codegen->IsCoreCalleeSaveRegister(index_reg)) {
          // We are about to change the value of `index_reg` (see the
          // calls to art::arm::Thumb2Assembler::Lsl and
          // art::arm::Thumb2Assembler::AddConstant below), but it has
          // not been saved by the previous call to
          // art::SlowPathCode::SaveLiveRegisters, as it is a
          // callee-save register --
          // art::SlowPathCode::SaveLiveRegisters does not consider
          // callee-save registers, as it has been designed with the
          // assumption that callee-save registers are supposed to be
          // handled by the called function.  So, as a callee-save
          // register, `index_reg` _would_ eventually be saved onto
          // the stack, but it would be too late: we would have
          // changed its value earlier.  Therefore, we manually save
          // it here into another freely available register,
          // `free_reg`, chosen of course among the caller-save
          // registers (as a callee-save `free_reg` register would
          // exhibit the same problem).
          //
          // Note we could have requested a temporary register from
          // the register allocator instead; but we prefer not to, as
          // this is a slow path, and we know we can find a
          // caller-save register that is available.
          Register free_reg = FindAvailableCallerSaveRegister(codegen);
          __ Mov(free_reg, index_reg);
          index_reg = free_reg;
          index = Location::RegisterLocation(index_reg);
        } else {
          // The initial register stored in `index_` has already been
          // saved in the call to art::SlowPathCode::SaveLiveRegisters
          // (as it is not a callee-save register), so we can freely
          // use it.
        }
        // Shifting the index value contained in `index_reg` by the scale
        // factor (2) cannot overflow in practice, as the runtime is
        // unable to allocate object arrays with a size larger than
        // 2^26 - 1 (that is, 2^28 - 4 bytes).
        __ Lsl(index_reg, index_reg, TIMES_4);
        static_assert(
            sizeof(mirror::HeapReference<mirror::Object>) == sizeof(int32_t),
            "art::mirror::HeapReference<art::mirror::Object> and int32_t have different sizes.");
        __ AddConstant(index_reg, index_reg, offset_);
      } else {
        // In the case of the UnsafeGetObject/UnsafeGetObjectVolatile
        // intrinsics, `index_` is not shifted by a scale factor of 2
        // (as in the case of ArrayGet), as it is actually an offset
        // to an object field within an object.
        DCHECK(instruction_->IsInvoke()) << instruction_->DebugName();
        DCHECK(instruction_->GetLocations()->Intrinsified());
        DCHECK((instruction_->AsInvoke()->GetIntrinsic() == Intrinsics::kUnsafeGetObject) ||
               (instruction_->AsInvoke()->GetIntrinsic() == Intrinsics::kUnsafeGetObjectVolatile))
            << instruction_->AsInvoke()->GetIntrinsic();
        DCHECK_EQ(offset_, 0U);
        DCHECK(index_.IsRegisterPair());
        // UnsafeGet's offset location is a register pair, the low
        // part contains the correct offset.
        index = index_.ToLow();
      }
    }

    // We're moving two or three locations to locations that could
    // overlap, so we need a parallel move resolver.
    InvokeRuntimeCallingConvention calling_convention;
    HParallelMove parallel_move(codegen->GetGraph()->GetArena());
    parallel_move.AddMove(ref_,
                          Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
                          Primitive::kPrimNot,
                          nullptr);
    parallel_move.AddMove(obj_,
                          Location::RegisterLocation(calling_convention.GetRegisterAt(1)),
                          Primitive::kPrimNot,
                          nullptr);
    if (index.IsValid()) {
      parallel_move.AddMove(index,
                            Location::RegisterLocation(calling_convention.GetRegisterAt(2)),
                            Primitive::kPrimInt,
                            nullptr);
      codegen->GetMoveResolver()->EmitNativeCode(&parallel_move);
    } else {
      codegen->GetMoveResolver()->EmitNativeCode(&parallel_move);
      __ LoadImmediate(calling_convention.GetRegisterAt(2), offset_);
    }
    arm_codegen->InvokeRuntime(kQuickReadBarrierSlow, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<
        kQuickReadBarrierSlow, mirror::Object*, mirror::Object*, mirror::Object*, uint32_t>();
    arm_codegen->Move32(out_, Location::RegisterLocation(R0));

    RestoreLiveRegisters(codegen, locations);
    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "ReadBarrierForHeapReferenceSlowPathARM"; }

 private:
  Register FindAvailableCallerSaveRegister(CodeGenerator* codegen) {
    size_t ref = static_cast<int>(ref_.AsRegister<Register>());
    size_t obj = static_cast<int>(obj_.AsRegister<Register>());
    for (size_t i = 0, e = codegen->GetNumberOfCoreRegisters(); i < e; ++i) {
      if (i != ref && i != obj && !codegen->IsCoreCalleeSaveRegister(i)) {
        return static_cast<Register>(i);
      }
    }
    // We shall never fail to find a free caller-save register, as
    // there are more than two core caller-save registers on ARM
    // (meaning it is possible to find one which is different from
    // `ref` and `obj`).
    DCHECK_GT(codegen->GetNumberOfCoreCallerSaveRegisters(), 2u);
    LOG(FATAL) << "Could not find a free caller-save register";
    UNREACHABLE();
  }

  const Location out_;
  const Location ref_;
  const Location obj_;
  const uint32_t offset_;
  // An additional location containing an index to an array.
  // Only used for HArrayGet and the UnsafeGetObject &
  // UnsafeGetObjectVolatile intrinsics.
  const Location index_;

  DISALLOW_COPY_AND_ASSIGN(ReadBarrierForHeapReferenceSlowPathARM);
};

// Slow path generating a read barrier for a GC root.
class ReadBarrierForRootSlowPathARM : public SlowPathCodeARM {
 public:
  ReadBarrierForRootSlowPathARM(HInstruction* instruction, Location out, Location root)
      : SlowPathCodeARM(instruction), out_(out), root_(root) {
    DCHECK(kEmitCompilerReadBarrier);
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = instruction_->GetLocations();
    Register reg_out = out_.AsRegister<Register>();
    DCHECK(locations->CanCall());
    DCHECK(!locations->GetLiveRegisters()->ContainsCoreRegister(reg_out));
    DCHECK(instruction_->IsLoadClass() || instruction_->IsLoadString())
        << "Unexpected instruction in read barrier for GC root slow path: "
        << instruction_->DebugName();

    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    InvokeRuntimeCallingConvention calling_convention;
    CodeGeneratorARM* arm_codegen = down_cast<CodeGeneratorARM*>(codegen);
    arm_codegen->Move32(Location::RegisterLocation(calling_convention.GetRegisterAt(0)), root_);
    arm_codegen->InvokeRuntime(kQuickReadBarrierForRootSlow,
                               instruction_,
                               instruction_->GetDexPc(),
                               this);
    CheckEntrypointTypes<kQuickReadBarrierForRootSlow, mirror::Object*, GcRoot<mirror::Object>*>();
    arm_codegen->Move32(out_, Location::RegisterLocation(R0));

    RestoreLiveRegisters(codegen, locations);
    __ b(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "ReadBarrierForRootSlowPathARM"; }

 private:
  const Location out_;
  const Location root_;

  DISALLOW_COPY_AND_ASSIGN(ReadBarrierForRootSlowPathARM);
};

inline Condition ARMCondition(IfCondition cond) {
  switch (cond) {
    case kCondEQ: return EQ;
    case kCondNE: return NE;
    case kCondLT: return LT;
    case kCondLE: return LE;
    case kCondGT: return GT;
    case kCondGE: return GE;
    case kCondB:  return LO;
    case kCondBE: return LS;
    case kCondA:  return HI;
    case kCondAE: return HS;
  }
  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

// Maps signed condition to unsigned condition.
inline Condition ARMUnsignedCondition(IfCondition cond) {
  switch (cond) {
    case kCondEQ: return EQ;
    case kCondNE: return NE;
    // Signed to unsigned.
    case kCondLT: return LO;
    case kCondLE: return LS;
    case kCondGT: return HI;
    case kCondGE: return HS;
    // Unsigned remain unchanged.
    case kCondB:  return LO;
    case kCondBE: return LS;
    case kCondA:  return HI;
    case kCondAE: return HS;
  }
  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

inline Condition ARMFPCondition(IfCondition cond, bool gt_bias) {
  // The ARM condition codes can express all the necessary branches, see the
  // "Meaning (floating-point)" column in the table A8-1 of the ARMv7 reference manual.
  // There is no dex instruction or HIR that would need the missing conditions
  // "equal or unordered" or "not equal".
  switch (cond) {
    case kCondEQ: return EQ;
    case kCondNE: return NE /* unordered */;
    case kCondLT: return gt_bias ? CC : LT /* unordered */;
    case kCondLE: return gt_bias ? LS : LE /* unordered */;
    case kCondGT: return gt_bias ? HI /* unordered */ : GT;
    case kCondGE: return gt_bias ? CS /* unordered */ : GE;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}

inline Shift ShiftFromOpKind(HDataProcWithShifterOp::OpKind op_kind) {
  switch (op_kind) {
    case HDataProcWithShifterOp::kASR: return ASR;
    case HDataProcWithShifterOp::kLSL: return LSL;
    case HDataProcWithShifterOp::kLSR: return LSR;
    default:
      LOG(FATAL) << "Unexpected op kind " << op_kind;
      UNREACHABLE();
  }
}

static void GenerateDataProcInstruction(HInstruction::InstructionKind kind,
                                        Register out,
                                        Register first,
                                        const ShifterOperand& second,
                                        CodeGeneratorARM* codegen) {
  if (second.IsImmediate() && second.GetImmediate() == 0) {
    const ShifterOperand in = kind == HInstruction::kAnd
        ? ShifterOperand(0)
        : ShifterOperand(first);

    __ mov(out, in);
  } else {
    switch (kind) {
      case HInstruction::kAdd:
        __ add(out, first, second);
        break;
      case HInstruction::kAnd:
        __ and_(out, first, second);
        break;
      case HInstruction::kOr:
        __ orr(out, first, second);
        break;
      case HInstruction::kSub:
        __ sub(out, first, second);
        break;
      case HInstruction::kXor:
        __ eor(out, first, second);
        break;
      default:
        LOG(FATAL) << "Unexpected instruction kind: " << kind;
        UNREACHABLE();
    }
  }
}

static void GenerateDataProc(HInstruction::InstructionKind kind,
                             const Location& out,
                             const Location& first,
                             const ShifterOperand& second_lo,
                             const ShifterOperand& second_hi,
                             CodeGeneratorARM* codegen) {
  const Register first_hi = first.AsRegisterPairHigh<Register>();
  const Register first_lo = first.AsRegisterPairLow<Register>();
  const Register out_hi = out.AsRegisterPairHigh<Register>();
  const Register out_lo = out.AsRegisterPairLow<Register>();

  if (kind == HInstruction::kAdd) {
    __ adds(out_lo, first_lo, second_lo);
    __ adc(out_hi, first_hi, second_hi);
  } else if (kind == HInstruction::kSub) {
    __ subs(out_lo, first_lo, second_lo);
    __ sbc(out_hi, first_hi, second_hi);
  } else {
    GenerateDataProcInstruction(kind, out_lo, first_lo, second_lo, codegen);
    GenerateDataProcInstruction(kind, out_hi, first_hi, second_hi, codegen);
  }
}

static ShifterOperand GetShifterOperand(Register rm, Shift shift, uint32_t shift_imm) {
  return shift_imm == 0 ? ShifterOperand(rm) : ShifterOperand(rm, shift, shift_imm);
}

static void GenerateLongDataProc(HDataProcWithShifterOp* instruction, CodeGeneratorARM* codegen) {
  DCHECK_EQ(instruction->GetType(), Primitive::kPrimLong);
  DCHECK(HDataProcWithShifterOp::IsShiftOp(instruction->GetOpKind()));

  const LocationSummary* const locations = instruction->GetLocations();
  const uint32_t shift_value = instruction->GetShiftAmount();
  const HInstruction::InstructionKind kind = instruction->GetInstrKind();
  const Location first = locations->InAt(0);
  const Location second = locations->InAt(1);
  const Location out = locations->Out();
  const Register first_hi = first.AsRegisterPairHigh<Register>();
  const Register first_lo = first.AsRegisterPairLow<Register>();
  const Register out_hi = out.AsRegisterPairHigh<Register>();
  const Register out_lo = out.AsRegisterPairLow<Register>();
  const Register second_hi = second.AsRegisterPairHigh<Register>();
  const Register second_lo = second.AsRegisterPairLow<Register>();
  const Shift shift = ShiftFromOpKind(instruction->GetOpKind());

  if (shift_value >= 32) {
    if (shift == LSL) {
      GenerateDataProcInstruction(kind,
                                  out_hi,
                                  first_hi,
                                  ShifterOperand(second_lo, LSL, shift_value - 32),
                                  codegen);
      GenerateDataProcInstruction(kind,
                                  out_lo,
                                  first_lo,
                                  ShifterOperand(0),
                                  codegen);
    } else if (shift == ASR) {
      GenerateDataProc(kind,
                       out,
                       first,
                       GetShifterOperand(second_hi, ASR, shift_value - 32),
                       ShifterOperand(second_hi, ASR, 31),
                       codegen);
    } else {
      DCHECK_EQ(shift, LSR);
      GenerateDataProc(kind,
                       out,
                       first,
                       GetShifterOperand(second_hi, LSR, shift_value - 32),
                       ShifterOperand(0),
                       codegen);
    }
  } else {
    DCHECK_GT(shift_value, 1U);
    DCHECK_LT(shift_value, 32U);

    if (shift == LSL) {
      // We are not doing this for HInstruction::kAdd because the output will require
      // Location::kOutputOverlap; not applicable to other cases.
      if (kind == HInstruction::kOr || kind == HInstruction::kXor) {
        GenerateDataProcInstruction(kind,
                                    out_hi,
                                    first_hi,
                                    ShifterOperand(second_hi, LSL, shift_value),
                                    codegen);
        GenerateDataProcInstruction(kind,
                                    out_hi,
                                    out_hi,
                                    ShifterOperand(second_lo, LSR, 32 - shift_value),
                                    codegen);
        GenerateDataProcInstruction(kind,
                                    out_lo,
                                    first_lo,
                                    ShifterOperand(second_lo, LSL, shift_value),
                                    codegen);
      } else {
        __ Lsl(IP, second_hi, shift_value);
        __ orr(IP, IP, ShifterOperand(second_lo, LSR, 32 - shift_value));
        GenerateDataProc(kind,
                         out,
                         first,
                         ShifterOperand(second_lo, LSL, shift_value),
                         ShifterOperand(IP),
                         codegen);
      }
    } else {
      DCHECK(shift == ASR || shift == LSR);

      // We are not doing this for HInstruction::kAdd because the output will require
      // Location::kOutputOverlap; not applicable to other cases.
      if (kind == HInstruction::kOr || kind == HInstruction::kXor) {
        GenerateDataProcInstruction(kind,
                                    out_lo,
                                    first_lo,
                                    ShifterOperand(second_lo, LSR, shift_value),
                                    codegen);
        GenerateDataProcInstruction(kind,
                                    out_lo,
                                    out_lo,
                                    ShifterOperand(second_hi, LSL, 32 - shift_value),
                                    codegen);
        GenerateDataProcInstruction(kind,
                                    out_hi,
                                    first_hi,
                                    ShifterOperand(second_hi, shift, shift_value),
                                    codegen);
      } else {
        __ Lsr(IP, second_lo, shift_value);
        __ orr(IP, IP, ShifterOperand(second_hi, LSL, 32 - shift_value));
        GenerateDataProc(kind,
                         out,
                         first,
                         ShifterOperand(IP),
                         ShifterOperand(second_hi, shift, shift_value),
                         codegen);
      }
    }
  }
}

static void GenerateVcmp(HInstruction* instruction, CodeGeneratorARM* codegen) {
  Primitive::Type type = instruction->InputAt(0)->GetType();
  Location lhs_loc = instruction->GetLocations()->InAt(0);
  Location rhs_loc = instruction->GetLocations()->InAt(1);
  if (rhs_loc.IsConstant()) {
    // 0.0 is the only immediate that can be encoded directly in
    // a VCMP instruction.
    //
    // Both the JLS (section 15.20.1) and the JVMS (section 6.5)
    // specify that in a floating-point comparison, positive zero
    // and negative zero are considered equal, so we can use the
    // literal 0.0 for both cases here.
    //
    // Note however that some methods (Float.equal, Float.compare,
    // Float.compareTo, Double.equal, Double.compare,
    // Double.compareTo, Math.max, Math.min, StrictMath.max,
    // StrictMath.min) consider 0.0 to be (strictly) greater than
    // -0.0. So if we ever translate calls to these methods into a
    // HCompare instruction, we must handle the -0.0 case with
    // care here.
    DCHECK(rhs_loc.GetConstant()->IsArithmeticZero());
    if (type == Primitive::kPrimFloat) {
      __ vcmpsz(lhs_loc.AsFpuRegister<SRegister>());
    } else {
      DCHECK_EQ(type, Primitive::kPrimDouble);
      __ vcmpdz(FromLowSToD(lhs_loc.AsFpuRegisterPairLow<SRegister>()));
    }
  } else {
    if (type == Primitive::kPrimFloat) {
      __ vcmps(lhs_loc.AsFpuRegister<SRegister>(), rhs_loc.AsFpuRegister<SRegister>());
    } else {
      DCHECK_EQ(type, Primitive::kPrimDouble);
      __ vcmpd(FromLowSToD(lhs_loc.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(rhs_loc.AsFpuRegisterPairLow<SRegister>()));
    }
  }
}

static std::pair<Condition, Condition> GenerateLongTestConstant(HCondition* condition,
                                                                bool invert,
                                                                CodeGeneratorARM* codegen) {
  DCHECK_EQ(condition->GetLeft()->GetType(), Primitive::kPrimLong);

  const LocationSummary* const locations = condition->GetLocations();
  IfCondition cond = condition->GetCondition();
  IfCondition opposite = condition->GetOppositeCondition();

  if (invert) {
    std::swap(cond, opposite);
  }

  std::pair<Condition, Condition> ret;
  const Location left = locations->InAt(0);
  const Location right = locations->InAt(1);

  DCHECK(right.IsConstant());

  const Register left_high = left.AsRegisterPairHigh<Register>();
  const Register left_low = left.AsRegisterPairLow<Register>();
  int64_t value = right.GetConstant()->AsLongConstant()->GetValue();

  switch (cond) {
    case kCondEQ:
    case kCondNE:
    case kCondB:
    case kCondBE:
    case kCondA:
    case kCondAE:
      __ CmpConstant(left_high, High32Bits(value));
      __ it(EQ);
      __ cmp(left_low, ShifterOperand(Low32Bits(value)), EQ);
      ret = std::make_pair(ARMUnsignedCondition(cond), ARMUnsignedCondition(opposite));
      break;
    case kCondLE:
    case kCondGT:
      // Trivially true or false.
      if (value == std::numeric_limits<int64_t>::max()) {
        __ cmp(left_low, ShifterOperand(left_low));
        ret = cond == kCondLE ? std::make_pair(EQ, NE) : std::make_pair(NE, EQ);
        break;
      }

      if (cond == kCondLE) {
        DCHECK_EQ(opposite, kCondGT);
        cond = kCondLT;
        opposite = kCondGE;
      } else {
        DCHECK_EQ(cond, kCondGT);
        DCHECK_EQ(opposite, kCondLE);
        cond = kCondGE;
        opposite = kCondLT;
      }

      value++;
      FALLTHROUGH_INTENDED;
    case kCondGE:
    case kCondLT:
      __ CmpConstant(left_low, Low32Bits(value));
      __ sbcs(IP, left_high, ShifterOperand(High32Bits(value)));
      ret = std::make_pair(ARMCondition(cond), ARMCondition(opposite));
      break;
    default:
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
  }

  return ret;
}

static std::pair<Condition, Condition> GenerateLongTest(HCondition* condition,
                                                        bool invert,
                                                        CodeGeneratorARM* codegen) {
  DCHECK_EQ(condition->GetLeft()->GetType(), Primitive::kPrimLong);

  const LocationSummary* const locations = condition->GetLocations();
  IfCondition cond = condition->GetCondition();
  IfCondition opposite = condition->GetOppositeCondition();

  if (invert) {
    std::swap(cond, opposite);
  }

  std::pair<Condition, Condition> ret;
  Location left = locations->InAt(0);
  Location right = locations->InAt(1);

  DCHECK(right.IsRegisterPair());

  switch (cond) {
    case kCondEQ:
    case kCondNE:
    case kCondB:
    case kCondBE:
    case kCondA:
    case kCondAE:
      __ cmp(left.AsRegisterPairHigh<Register>(),
             ShifterOperand(right.AsRegisterPairHigh<Register>()));
      __ it(EQ);
      __ cmp(left.AsRegisterPairLow<Register>(),
             ShifterOperand(right.AsRegisterPairLow<Register>()),
             EQ);
      ret = std::make_pair(ARMUnsignedCondition(cond), ARMUnsignedCondition(opposite));
      break;
    case kCondLE:
    case kCondGT:
      if (cond == kCondLE) {
        DCHECK_EQ(opposite, kCondGT);
        cond = kCondGE;
        opposite = kCondLT;
      } else {
        DCHECK_EQ(cond, kCondGT);
        DCHECK_EQ(opposite, kCondLE);
        cond = kCondLT;
        opposite = kCondGE;
      }

      std::swap(left, right);
      FALLTHROUGH_INTENDED;
    case kCondGE:
    case kCondLT:
      __ cmp(left.AsRegisterPairLow<Register>(),
             ShifterOperand(right.AsRegisterPairLow<Register>()));
      __ sbcs(IP,
              left.AsRegisterPairHigh<Register>(),
              ShifterOperand(right.AsRegisterPairHigh<Register>()));
      ret = std::make_pair(ARMCondition(cond), ARMCondition(opposite));
      break;
    default:
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
  }

  return ret;
}

static std::pair<Condition, Condition> GenerateTest(HCondition* condition,
                                                    bool invert,
                                                    CodeGeneratorARM* codegen) {
  const LocationSummary* const locations = condition->GetLocations();
  const Primitive::Type type = condition->GetLeft()->GetType();
  IfCondition cond = condition->GetCondition();
  IfCondition opposite = condition->GetOppositeCondition();
  std::pair<Condition, Condition> ret;
  const Location right = locations->InAt(1);

  if (invert) {
    std::swap(cond, opposite);
  }

  if (type == Primitive::kPrimLong) {
    ret = locations->InAt(1).IsConstant()
        ? GenerateLongTestConstant(condition, invert, codegen)
        : GenerateLongTest(condition, invert, codegen);
  } else if (Primitive::IsFloatingPointType(type)) {
    GenerateVcmp(condition, codegen);
    __ vmstat();
    ret = std::make_pair(ARMFPCondition(cond, condition->IsGtBias()),
                         ARMFPCondition(opposite, condition->IsGtBias()));
  } else {
    DCHECK(Primitive::IsIntegralType(type) || type == Primitive::kPrimNot) << type;

    const Register left = locations->InAt(0).AsRegister<Register>();

    if (right.IsRegister()) {
      __ cmp(left, ShifterOperand(right.AsRegister<Register>()));
    } else {
      DCHECK(right.IsConstant());
      __ CmpConstant(left, CodeGenerator::GetInt32ValueOf(right.GetConstant()));
    }

    ret = std::make_pair(ARMCondition(cond), ARMCondition(opposite));
  }

  return ret;
}

static bool CanGenerateTest(HCondition* condition, ArmAssembler* assembler) {
  if (condition->GetLeft()->GetType() == Primitive::kPrimLong) {
    const LocationSummary* const locations = condition->GetLocations();
    const IfCondition c = condition->GetCondition();

    if (locations->InAt(1).IsConstant()) {
      const int64_t value = locations->InAt(1).GetConstant()->AsLongConstant()->GetValue();
      ShifterOperand so;

      if (c < kCondLT || c > kCondGE) {
        // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
        // we check that the least significant half of the first input to be compared
        // is in a low register (the other half is read outside an IT block), and
        // the constant fits in an 8-bit unsigned integer, so that a 16-bit CMP
        // encoding can be used.
        if (!ArmAssembler::IsLowRegister(locations->InAt(0).AsRegisterPairLow<Register>()) ||
            !IsUint<8>(Low32Bits(value))) {
          return false;
        }
      } else if (c == kCondLE || c == kCondGT) {
        if (value < std::numeric_limits<int64_t>::max() &&
            !assembler->ShifterOperandCanHold(kNoRegister,
                                              kNoRegister,
                                              SBC,
                                              High32Bits(value + 1),
                                              kCcSet,
                                              &so)) {
          return false;
        }
      } else if (!assembler->ShifterOperandCanHold(kNoRegister,
                                                   kNoRegister,
                                                   SBC,
                                                   High32Bits(value),
                                                   kCcSet,
                                                   &so)) {
        return false;
      }
    }
  }

  return true;
}

static bool CanEncodeConstantAs8BitImmediate(HConstant* constant) {
  const Primitive::Type type = constant->GetType();
  bool ret = false;

  DCHECK(Primitive::IsIntegralType(type) || type == Primitive::kPrimNot) << type;

  if (type == Primitive::kPrimLong) {
    const uint64_t value = constant->AsLongConstant()->GetValueAsUint64();

    ret = IsUint<8>(Low32Bits(value)) && IsUint<8>(High32Bits(value));
  } else {
    ret = IsUint<8>(CodeGenerator::GetInt32ValueOf(constant));
  }

  return ret;
}

static Location Arm8BitEncodableConstantOrRegister(HInstruction* constant) {
  DCHECK(!Primitive::IsFloatingPointType(constant->GetType()));

  if (constant->IsConstant() && CanEncodeConstantAs8BitImmediate(constant->AsConstant())) {
    return Location::ConstantLocation(constant->AsConstant());
  }

  return Location::RequiresRegister();
}

static bool CanGenerateConditionalMove(const Location& out, const Location& src) {
  // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
  // we check that we are not dealing with floating-point output (there is no
  // 16-bit VMOV encoding).
  if (!out.IsRegister() && !out.IsRegisterPair()) {
    return false;
  }

  // For constants, we also check that the output is in one or two low registers,
  // and that the constants fit in an 8-bit unsigned integer, so that a 16-bit
  // MOV encoding can be used.
  if (src.IsConstant()) {
    if (!CanEncodeConstantAs8BitImmediate(src.GetConstant())) {
      return false;
    }

    if (out.IsRegister()) {
      if (!ArmAssembler::IsLowRegister(out.AsRegister<Register>())) {
        return false;
      }
    } else {
      DCHECK(out.IsRegisterPair());

      if (!ArmAssembler::IsLowRegister(out.AsRegisterPairHigh<Register>())) {
        return false;
      }
    }
  }

  return true;
}

#undef __
// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<ArmAssembler*>(GetAssembler())->  // NOLINT

Label* CodeGeneratorARM::GetFinalLabel(HInstruction* instruction, Label* final_label) {
  DCHECK(!instruction->IsControlFlow() && !instruction->IsSuspendCheck());
  DCHECK(!instruction->IsInvoke() || !instruction->GetLocations()->CanCall());

  const HBasicBlock* const block = instruction->GetBlock();
  const HLoopInformation* const info = block->GetLoopInformation();
  HInstruction* const next = instruction->GetNext();

  // Avoid a branch to a branch.
  if (next->IsGoto() && (info == nullptr ||
                         !info->IsBackEdge(*block) ||
                         !info->HasSuspendCheck())) {
    final_label = GetLabelOf(next->AsGoto()->GetSuccessor());
  }

  return final_label;
}

void CodeGeneratorARM::DumpCoreRegister(std::ostream& stream, int reg) const {
  stream << Register(reg);
}

void CodeGeneratorARM::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  stream << SRegister(reg);
}

size_t CodeGeneratorARM::SaveCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ StoreToOffset(kStoreWord, static_cast<Register>(reg_id), SP, stack_index);
  return kArmWordSize;
}

size_t CodeGeneratorARM::RestoreCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ LoadFromOffset(kLoadWord, static_cast<Register>(reg_id), SP, stack_index);
  return kArmWordSize;
}

size_t CodeGeneratorARM::SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  __ StoreSToOffset(static_cast<SRegister>(reg_id), SP, stack_index);
  return kArmWordSize;
}

size_t CodeGeneratorARM::RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  __ LoadSFromOffset(static_cast<SRegister>(reg_id), SP, stack_index);
  return kArmWordSize;
}

CodeGeneratorARM::CodeGeneratorARM(HGraph* graph,
                                   const ArmInstructionSetFeatures& isa_features,
                                   const CompilerOptions& compiler_options,
                                   OptimizingCompilerStats* stats)
    : CodeGenerator(graph,
                    kNumberOfCoreRegisters,
                    kNumberOfSRegisters,
                    kNumberOfRegisterPairs,
                    ComputeRegisterMask(reinterpret_cast<const int*>(kCoreCalleeSaves),
                                        arraysize(kCoreCalleeSaves)),
                    ComputeRegisterMask(reinterpret_cast<const int*>(kFpuCalleeSaves),
                                        arraysize(kFpuCalleeSaves)),
                    compiler_options,
                    stats),
      block_labels_(nullptr),
      location_builder_(graph, this),
      instruction_visitor_(graph, this),
      move_resolver_(graph->GetArena(), this),
      assembler_(graph->GetArena()),
      isa_features_(isa_features),
      uint32_literals_(std::less<uint32_t>(),
                       graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      pc_relative_dex_cache_patches_(graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_string_patches_(StringReferenceValueComparator(),
                                 graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      pc_relative_string_patches_(graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_type_patches_(TypeReferenceValueComparator(),
                               graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      pc_relative_type_patches_(graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      type_bss_entry_patches_(graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      jit_string_patches_(StringReferenceValueComparator(),
                          graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      jit_class_patches_(TypeReferenceValueComparator(),
                         graph->GetArena()->Adapter(kArenaAllocCodeGenerator)) {
  // Always save the LR register to mimic Quick.
  AddAllocatedRegister(Location::RegisterLocation(LR));
}

void CodeGeneratorARM::Finalize(CodeAllocator* allocator) {
  // Ensure that we fix up branches and literal loads and emit the literal pool.
  __ FinalizeCode();

  // Adjust native pc offsets in stack maps.
  for (size_t i = 0, num = stack_map_stream_.GetNumberOfStackMaps(); i != num; ++i) {
    uint32_t old_position =
        stack_map_stream_.GetStackMap(i).native_pc_code_offset.Uint32Value(kThumb2);
    uint32_t new_position = __ GetAdjustedPosition(old_position);
    stack_map_stream_.SetStackMapNativePcOffset(i, new_position);
  }
  // Adjust pc offsets for the disassembly information.
  if (disasm_info_ != nullptr) {
    GeneratedCodeInterval* frame_entry_interval = disasm_info_->GetFrameEntryInterval();
    frame_entry_interval->start = __ GetAdjustedPosition(frame_entry_interval->start);
    frame_entry_interval->end = __ GetAdjustedPosition(frame_entry_interval->end);
    for (auto& it : *disasm_info_->GetInstructionIntervals()) {
      it.second.start = __ GetAdjustedPosition(it.second.start);
      it.second.end = __ GetAdjustedPosition(it.second.end);
    }
    for (auto& it : *disasm_info_->GetSlowPathIntervals()) {
      it.code_interval.start = __ GetAdjustedPosition(it.code_interval.start);
      it.code_interval.end = __ GetAdjustedPosition(it.code_interval.end);
    }
  }

  CodeGenerator::Finalize(allocator);
}

void CodeGeneratorARM::SetupBlockedRegisters() const {
  // Stack register, LR and PC are always reserved.
  blocked_core_registers_[SP] = true;
  blocked_core_registers_[LR] = true;
  blocked_core_registers_[PC] = true;

  // Reserve thread register.
  blocked_core_registers_[TR] = true;

  // Reserve temp register.
  blocked_core_registers_[IP] = true;

  if (GetGraph()->IsDebuggable()) {
    // Stubs do not save callee-save floating point registers. If the graph
    // is debuggable, we need to deal with these registers differently. For
    // now, just block them.
    for (size_t i = 0; i < arraysize(kFpuCalleeSaves); ++i) {
      blocked_fpu_registers_[kFpuCalleeSaves[i]] = true;
    }
  }
}

InstructionCodeGeneratorARM::InstructionCodeGeneratorARM(HGraph* graph, CodeGeneratorARM* codegen)
      : InstructionCodeGenerator(graph, codegen),
        assembler_(codegen->GetAssembler()),
        codegen_(codegen) {}

void CodeGeneratorARM::ComputeSpillMask() {
  core_spill_mask_ = allocated_registers_.GetCoreRegisters() & core_callee_save_mask_;
  DCHECK_NE(core_spill_mask_, 0u) << "At least the return address register must be saved";
  // There is no easy instruction to restore just the PC on thumb2. We spill and
  // restore another arbitrary register.
  core_spill_mask_ |= (1 << kCoreAlwaysSpillRegister);
  fpu_spill_mask_ = allocated_registers_.GetFloatingPointRegisters() & fpu_callee_save_mask_;
  // We use vpush and vpop for saving and restoring floating point registers, which take
  // a SRegister and the number of registers to save/restore after that SRegister. We
  // therefore update the `fpu_spill_mask_` to also contain those registers not allocated,
  // but in the range.
  if (fpu_spill_mask_ != 0) {
    uint32_t least_significant_bit = LeastSignificantBit(fpu_spill_mask_);
    uint32_t most_significant_bit = MostSignificantBit(fpu_spill_mask_);
    for (uint32_t i = least_significant_bit + 1 ; i < most_significant_bit; ++i) {
      fpu_spill_mask_ |= (1 << i);
    }
  }
}

static dwarf::Reg DWARFReg(Register reg) {
  return dwarf::Reg::ArmCore(static_cast<int>(reg));
}

static dwarf::Reg DWARFReg(SRegister reg) {
  return dwarf::Reg::ArmFp(static_cast<int>(reg));
}

void CodeGeneratorARM::GenerateFrameEntry() {
  bool skip_overflow_check =
      IsLeafMethod() && !FrameNeedsStackCheck(GetFrameSize(), InstructionSet::kArm);
  DCHECK(GetCompilerOptions().GetImplicitStackOverflowChecks());
  __ Bind(&frame_entry_label_);

  if (HasEmptyFrame()) {
    return;
  }

  if (!skip_overflow_check) {
    __ AddConstant(IP, SP, -static_cast<int32_t>(GetStackOverflowReservedBytes(kArm)));
    __ LoadFromOffset(kLoadWord, IP, IP, 0);
    RecordPcInfo(nullptr, 0);
  }

  __ PushList(core_spill_mask_);
  __ cfi().AdjustCFAOffset(kArmWordSize * POPCOUNT(core_spill_mask_));
  __ cfi().RelOffsetForMany(DWARFReg(kMethodRegisterArgument), 0, core_spill_mask_, kArmWordSize);
  if (fpu_spill_mask_ != 0) {
    SRegister start_register = SRegister(LeastSignificantBit(fpu_spill_mask_));
    __ vpushs(start_register, POPCOUNT(fpu_spill_mask_));
    __ cfi().AdjustCFAOffset(kArmWordSize * POPCOUNT(fpu_spill_mask_));
    __ cfi().RelOffsetForMany(DWARFReg(S0), 0, fpu_spill_mask_, kArmWordSize);
  }

  if (GetGraph()->HasShouldDeoptimizeFlag()) {
    // Initialize should_deoptimize flag to 0.
    __ mov(IP, ShifterOperand(0));
    __ StoreToOffset(kStoreWord, IP, SP, -kShouldDeoptimizeFlagSize);
  }

  int adjust = GetFrameSize() - FrameEntrySpillSize();
  __ AddConstant(SP, -adjust);
  __ cfi().AdjustCFAOffset(adjust);

  // Save the current method if we need it. Note that we do not
  // do this in HCurrentMethod, as the instruction might have been removed
  // in the SSA graph.
  if (RequiresCurrentMethod()) {
    __ StoreToOffset(kStoreWord, kMethodRegisterArgument, SP, 0);
  }
}

void CodeGeneratorARM::GenerateFrameExit() {
  if (HasEmptyFrame()) {
    __ bx(LR);
    return;
  }
  __ cfi().RememberState();
  int adjust = GetFrameSize() - FrameEntrySpillSize();
  __ AddConstant(SP, adjust);
  __ cfi().AdjustCFAOffset(-adjust);
  if (fpu_spill_mask_ != 0) {
    SRegister start_register = SRegister(LeastSignificantBit(fpu_spill_mask_));
    __ vpops(start_register, POPCOUNT(fpu_spill_mask_));
    __ cfi().AdjustCFAOffset(-static_cast<int>(kArmPointerSize) * POPCOUNT(fpu_spill_mask_));
    __ cfi().RestoreMany(DWARFReg(SRegister(0)), fpu_spill_mask_);
  }
  // Pop LR into PC to return.
  DCHECK_NE(core_spill_mask_ & (1 << LR), 0U);
  uint32_t pop_mask = (core_spill_mask_ & (~(1 << LR))) | 1 << PC;
  __ PopList(pop_mask);
  __ cfi().RestoreState();
  __ cfi().DefCFAOffset(GetFrameSize());
}

void CodeGeneratorARM::Bind(HBasicBlock* block) {
  Label* label = GetLabelOf(block);
  __ BindTrackedLabel(label);
}

Location InvokeDexCallingConventionVisitorARM::GetNextLocation(Primitive::Type type) {
  switch (type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimNot: {
      uint32_t index = gp_index_++;
      uint32_t stack_index = stack_index_++;
      if (index < calling_convention.GetNumberOfRegisters()) {
        return Location::RegisterLocation(calling_convention.GetRegisterAt(index));
      } else {
        return Location::StackSlot(calling_convention.GetStackOffsetOf(stack_index));
      }
    }

    case Primitive::kPrimLong: {
      uint32_t index = gp_index_;
      uint32_t stack_index = stack_index_;
      gp_index_ += 2;
      stack_index_ += 2;
      if (index + 1 < calling_convention.GetNumberOfRegisters()) {
        if (calling_convention.GetRegisterAt(index) == R1) {
          // Skip R1, and use R2_R3 instead.
          gp_index_++;
          index++;
        }
      }
      if (index + 1 < calling_convention.GetNumberOfRegisters()) {
        DCHECK_EQ(calling_convention.GetRegisterAt(index) + 1,
                  calling_convention.GetRegisterAt(index + 1));

        return Location::RegisterPairLocation(calling_convention.GetRegisterAt(index),
                                              calling_convention.GetRegisterAt(index + 1));
      } else {
        return Location::DoubleStackSlot(calling_convention.GetStackOffsetOf(stack_index));
      }
    }

    case Primitive::kPrimFloat: {
      uint32_t stack_index = stack_index_++;
      if (float_index_ % 2 == 0) {
        float_index_ = std::max(double_index_, float_index_);
      }
      if (float_index_ < calling_convention.GetNumberOfFpuRegisters()) {
        return Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(float_index_++));
      } else {
        return Location::StackSlot(calling_convention.GetStackOffsetOf(stack_index));
      }
    }

    case Primitive::kPrimDouble: {
      double_index_ = std::max(double_index_, RoundUp(float_index_, 2));
      uint32_t stack_index = stack_index_;
      stack_index_ += 2;
      if (double_index_ + 1 < calling_convention.GetNumberOfFpuRegisters()) {
        uint32_t index = double_index_;
        double_index_ += 2;
        Location result = Location::FpuRegisterPairLocation(
          calling_convention.GetFpuRegisterAt(index),
          calling_convention.GetFpuRegisterAt(index + 1));
        DCHECK(ExpectedPairLayout(result));
        return result;
      } else {
        return Location::DoubleStackSlot(calling_convention.GetStackOffsetOf(stack_index));
      }
    }

    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unexpected parameter type " << type;
      break;
  }
  return Location::NoLocation();
}

Location InvokeDexCallingConventionVisitorARM::GetReturnLocation(Primitive::Type type) const {
  switch (type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt:
    case Primitive::kPrimNot: {
      return Location::RegisterLocation(R0);
    }

    case Primitive::kPrimFloat: {
      return Location::FpuRegisterLocation(S0);
    }

    case Primitive::kPrimLong: {
      return Location::RegisterPairLocation(R0, R1);
    }

    case Primitive::kPrimDouble: {
      return Location::FpuRegisterPairLocation(S0, S1);
    }

    case Primitive::kPrimVoid:
      return Location::NoLocation();
  }

  UNREACHABLE();
}

Location InvokeDexCallingConventionVisitorARM::GetMethodLocation() const {
  return Location::RegisterLocation(kMethodRegisterArgument);
}

void CodeGeneratorARM::Move32(Location destination, Location source) {
  if (source.Equals(destination)) {
    return;
  }
  if (destination.IsRegister()) {
    if (source.IsRegister()) {
      __ Mov(destination.AsRegister<Register>(), source.AsRegister<Register>());
    } else if (source.IsFpuRegister()) {
      __ vmovrs(destination.AsRegister<Register>(), source.AsFpuRegister<SRegister>());
    } else {
      __ LoadFromOffset(kLoadWord, destination.AsRegister<Register>(), SP, source.GetStackIndex());
    }
  } else if (destination.IsFpuRegister()) {
    if (source.IsRegister()) {
      __ vmovsr(destination.AsFpuRegister<SRegister>(), source.AsRegister<Register>());
    } else if (source.IsFpuRegister()) {
      __ vmovs(destination.AsFpuRegister<SRegister>(), source.AsFpuRegister<SRegister>());
    } else {
      __ LoadSFromOffset(destination.AsFpuRegister<SRegister>(), SP, source.GetStackIndex());
    }
  } else {
    DCHECK(destination.IsStackSlot()) << destination;
    if (source.IsRegister()) {
      __ StoreToOffset(kStoreWord, source.AsRegister<Register>(), SP, destination.GetStackIndex());
    } else if (source.IsFpuRegister()) {
      __ StoreSToOffset(source.AsFpuRegister<SRegister>(), SP, destination.GetStackIndex());
    } else {
      DCHECK(source.IsStackSlot()) << source;
      __ LoadFromOffset(kLoadWord, IP, SP, source.GetStackIndex());
      __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
    }
  }
}

void CodeGeneratorARM::Move64(Location destination, Location source) {
  if (source.Equals(destination)) {
    return;
  }
  if (destination.IsRegisterPair()) {
    if (source.IsRegisterPair()) {
      EmitParallelMoves(
          Location::RegisterLocation(source.AsRegisterPairHigh<Register>()),
          Location::RegisterLocation(destination.AsRegisterPairHigh<Register>()),
          Primitive::kPrimInt,
          Location::RegisterLocation(source.AsRegisterPairLow<Register>()),
          Location::RegisterLocation(destination.AsRegisterPairLow<Register>()),
          Primitive::kPrimInt);
    } else if (source.IsFpuRegister()) {
      UNIMPLEMENTED(FATAL);
    } else if (source.IsFpuRegisterPair()) {
      __ vmovrrd(destination.AsRegisterPairLow<Register>(),
                 destination.AsRegisterPairHigh<Register>(),
                 FromLowSToD(source.AsFpuRegisterPairLow<SRegister>()));
    } else {
      DCHECK(source.IsDoubleStackSlot());
      DCHECK(ExpectedPairLayout(destination));
      __ LoadFromOffset(kLoadWordPair, destination.AsRegisterPairLow<Register>(),
                        SP, source.GetStackIndex());
    }
  } else if (destination.IsFpuRegisterPair()) {
    if (source.IsDoubleStackSlot()) {
      __ LoadDFromOffset(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()),
                         SP,
                         source.GetStackIndex());
    } else if (source.IsRegisterPair()) {
      __ vmovdrr(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()),
                 source.AsRegisterPairLow<Register>(),
                 source.AsRegisterPairHigh<Register>());
    } else {
      UNIMPLEMENTED(FATAL);
    }
  } else {
    DCHECK(destination.IsDoubleStackSlot());
    if (source.IsRegisterPair()) {
      // No conflict possible, so just do the moves.
      if (source.AsRegisterPairLow<Register>() == R1) {
        DCHECK_EQ(source.AsRegisterPairHigh<Register>(), R2);
        __ StoreToOffset(kStoreWord, R1, SP, destination.GetStackIndex());
        __ StoreToOffset(kStoreWord, R2, SP, destination.GetHighStackIndex(kArmWordSize));
      } else {
        __ StoreToOffset(kStoreWordPair, source.AsRegisterPairLow<Register>(),
                         SP, destination.GetStackIndex());
      }
    } else if (source.IsFpuRegisterPair()) {
      __ StoreDToOffset(FromLowSToD(source.AsFpuRegisterPairLow<SRegister>()),
                        SP,
                        destination.GetStackIndex());
    } else {
      DCHECK(source.IsDoubleStackSlot());
      EmitParallelMoves(
          Location::StackSlot(source.GetStackIndex()),
          Location::StackSlot(destination.GetStackIndex()),
          Primitive::kPrimInt,
          Location::StackSlot(source.GetHighStackIndex(kArmWordSize)),
          Location::StackSlot(destination.GetHighStackIndex(kArmWordSize)),
          Primitive::kPrimInt);
    }
  }
}

void CodeGeneratorARM::MoveConstant(Location location, int32_t value) {
  DCHECK(location.IsRegister());
  __ LoadImmediate(location.AsRegister<Register>(), value);
}

void CodeGeneratorARM::MoveLocation(Location dst, Location src, Primitive::Type dst_type) {
  HParallelMove move(GetGraph()->GetArena());
  move.AddMove(src, dst, dst_type, nullptr);
  GetMoveResolver()->EmitNativeCode(&move);
}

void CodeGeneratorARM::AddLocationAsTemp(Location location, LocationSummary* locations) {
  if (location.IsRegister()) {
    locations->AddTemp(location);
  } else if (location.IsRegisterPair()) {
    locations->AddTemp(Location::RegisterLocation(location.AsRegisterPairLow<Register>()));
    locations->AddTemp(Location::RegisterLocation(location.AsRegisterPairHigh<Register>()));
  } else {
    UNIMPLEMENTED(FATAL) << "AddLocationAsTemp not implemented for location " << location;
  }
}

void CodeGeneratorARM::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                     HInstruction* instruction,
                                     uint32_t dex_pc,
                                     SlowPathCode* slow_path) {
  ValidateInvokeRuntime(entrypoint, instruction, slow_path);
  GenerateInvokeRuntime(GetThreadOffset<kArmPointerSize>(entrypoint).Int32Value());
  if (EntrypointRequiresStackMap(entrypoint)) {
    RecordPcInfo(instruction, dex_pc, slow_path);
  }
}

void CodeGeneratorARM::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                           HInstruction* instruction,
                                                           SlowPathCode* slow_path) {
  ValidateInvokeRuntimeWithoutRecordingPcInfo(instruction, slow_path);
  GenerateInvokeRuntime(entry_point_offset);
}

void CodeGeneratorARM::GenerateInvokeRuntime(int32_t entry_point_offset) {
  __ LoadFromOffset(kLoadWord, LR, TR, entry_point_offset);
  __ blx(LR);
}

void InstructionCodeGeneratorARM::HandleGoto(HInstruction* got, HBasicBlock* successor) {
  DCHECK(!successor->IsExitBlock());

  HBasicBlock* block = got->GetBlock();
  HInstruction* previous = got->GetPrevious();

  HLoopInformation* info = block->GetLoopInformation();
  if (info != nullptr && info->IsBackEdge(*block) && info->HasSuspendCheck()) {
    codegen_->ClearSpillSlotsFromLoopPhisInStackMap(info->GetSuspendCheck());
    GenerateSuspendCheck(info->GetSuspendCheck(), successor);
    return;
  }

  if (block->IsEntryBlock() && (previous != nullptr) && previous->IsSuspendCheck()) {
    GenerateSuspendCheck(previous->AsSuspendCheck(), nullptr);
  }
  if (!codegen_->GoesToNextBlock(got->GetBlock(), successor)) {
    __ b(codegen_->GetLabelOf(successor));
  }
}

void LocationsBuilderARM::VisitGoto(HGoto* got) {
  got->SetLocations(nullptr);
}

void InstructionCodeGeneratorARM::VisitGoto(HGoto* got) {
  HandleGoto(got, got->GetSuccessor());
}

void LocationsBuilderARM::VisitTryBoundary(HTryBoundary* try_boundary) {
  try_boundary->SetLocations(nullptr);
}

void InstructionCodeGeneratorARM::VisitTryBoundary(HTryBoundary* try_boundary) {
  HBasicBlock* successor = try_boundary->GetNormalFlowSuccessor();
  if (!successor->IsExitBlock()) {
    HandleGoto(try_boundary, successor);
  }
}

void LocationsBuilderARM::VisitExit(HExit* exit) {
  exit->SetLocations(nullptr);
}

void InstructionCodeGeneratorARM::VisitExit(HExit* exit ATTRIBUTE_UNUSED) {
}

void InstructionCodeGeneratorARM::GenerateLongComparesAndJumps(HCondition* cond,
                                                               Label* true_label,
                                                               Label* false_label) {
  LocationSummary* locations = cond->GetLocations();
  Location left = locations->InAt(0);
  Location right = locations->InAt(1);
  IfCondition if_cond = cond->GetCondition();

  Register left_high = left.AsRegisterPairHigh<Register>();
  Register left_low = left.AsRegisterPairLow<Register>();
  IfCondition true_high_cond = if_cond;
  IfCondition false_high_cond = cond->GetOppositeCondition();
  Condition final_condition = ARMUnsignedCondition(if_cond);  // unsigned on lower part

  // Set the conditions for the test, remembering that == needs to be
  // decided using the low words.
  switch (if_cond) {
    case kCondEQ:
    case kCondNE:
      // Nothing to do.
      break;
    case kCondLT:
      false_high_cond = kCondGT;
      break;
    case kCondLE:
      true_high_cond = kCondLT;
      break;
    case kCondGT:
      false_high_cond = kCondLT;
      break;
    case kCondGE:
      true_high_cond = kCondGT;
      break;
    case kCondB:
      false_high_cond = kCondA;
      break;
    case kCondBE:
      true_high_cond = kCondB;
      break;
    case kCondA:
      false_high_cond = kCondB;
      break;
    case kCondAE:
      true_high_cond = kCondA;
      break;
  }
  if (right.IsConstant()) {
    int64_t value = right.GetConstant()->AsLongConstant()->GetValue();
    int32_t val_low = Low32Bits(value);
    int32_t val_high = High32Bits(value);

    __ CmpConstant(left_high, val_high);
    if (if_cond == kCondNE) {
      __ b(true_label, ARMCondition(true_high_cond));
    } else if (if_cond == kCondEQ) {
      __ b(false_label, ARMCondition(false_high_cond));
    } else {
      __ b(true_label, ARMCondition(true_high_cond));
      __ b(false_label, ARMCondition(false_high_cond));
    }
    // Must be equal high, so compare the lows.
    __ CmpConstant(left_low, val_low);
  } else {
    Register right_high = right.AsRegisterPairHigh<Register>();
    Register right_low = right.AsRegisterPairLow<Register>();

    __ cmp(left_high, ShifterOperand(right_high));
    if (if_cond == kCondNE) {
      __ b(true_label, ARMCondition(true_high_cond));
    } else if (if_cond == kCondEQ) {
      __ b(false_label, ARMCondition(false_high_cond));
    } else {
      __ b(true_label, ARMCondition(true_high_cond));
      __ b(false_label, ARMCondition(false_high_cond));
    }
    // Must be equal high, so compare the lows.
    __ cmp(left_low, ShifterOperand(right_low));
  }
  // The last comparison might be unsigned.
  // TODO: optimize cases where this is always true/false
  __ b(true_label, final_condition);
}

void InstructionCodeGeneratorARM::GenerateCompareTestAndBranch(HCondition* condition,
                                                               Label* true_target_in,
                                                               Label* false_target_in) {
  if (CanGenerateTest(condition, codegen_->GetAssembler())) {
    Label* non_fallthrough_target;
    bool invert;
    bool emit_both_branches;

    if (true_target_in == nullptr) {
      // The true target is fallthrough.
      DCHECK(false_target_in != nullptr);
      non_fallthrough_target = false_target_in;
      invert = true;
      emit_both_branches = false;
    } else {
      // Either the false target is fallthrough, or there is no fallthrough
      // and both branches must be emitted.
      non_fallthrough_target = true_target_in;
      invert = false;
      emit_both_branches = (false_target_in != nullptr);
    }

    const auto cond = GenerateTest(condition, invert, codegen_);

    __ b(non_fallthrough_target, cond.first);

    if (emit_both_branches) {
      // No target falls through, we need to branch.
      __ b(false_target_in);
    }

    return;
  }

  // Generated branching requires both targets to be explicit. If either of the
  // targets is nullptr (fallthrough) use and bind `fallthrough_target` instead.
  Label fallthrough_target;
  Label* true_target = true_target_in == nullptr ? &fallthrough_target : true_target_in;
  Label* false_target = false_target_in == nullptr ? &fallthrough_target : false_target_in;

  DCHECK_EQ(condition->InputAt(0)->GetType(), Primitive::kPrimLong);
  GenerateLongComparesAndJumps(condition, true_target, false_target);

  if (false_target != &fallthrough_target) {
    __ b(false_target);
  }

  if (fallthrough_target.IsLinked()) {
    __ Bind(&fallthrough_target);
  }
}

void InstructionCodeGeneratorARM::GenerateTestAndBranch(HInstruction* instruction,
                                                        size_t condition_input_index,
                                                        Label* true_target,
                                                        Label* false_target) {
  HInstruction* cond = instruction->InputAt(condition_input_index);

  if (true_target == nullptr && false_target == nullptr) {
    // Nothing to do. The code always falls through.
    return;
  } else if (cond->IsIntConstant()) {
    // Constant condition, statically compared against "true" (integer value 1).
    if (cond->AsIntConstant()->IsTrue()) {
      if (true_target != nullptr) {
        __ b(true_target);
      }
    } else {
      DCHECK(cond->AsIntConstant()->IsFalse()) << cond->AsIntConstant()->GetValue();
      if (false_target != nullptr) {
        __ b(false_target);
      }
    }
    return;
  }

  // The following code generates these patterns:
  //  (1) true_target == nullptr && false_target != nullptr
  //        - opposite condition true => branch to false_target
  //  (2) true_target != nullptr && false_target == nullptr
  //        - condition true => branch to true_target
  //  (3) true_target != nullptr && false_target != nullptr
  //        - condition true => branch to true_target
  //        - branch to false_target
  if (IsBooleanValueOrMaterializedCondition(cond)) {
    // Condition has been materialized, compare the output to 0.
    Location cond_val = instruction->GetLocations()->InAt(condition_input_index);
    DCHECK(cond_val.IsRegister());
    if (true_target == nullptr) {
      __ CompareAndBranchIfZero(cond_val.AsRegister<Register>(), false_target);
    } else {
      __ CompareAndBranchIfNonZero(cond_val.AsRegister<Register>(), true_target);
    }
  } else {
    // Condition has not been materialized. Use its inputs as the comparison and
    // its condition as the branch condition.
    HCondition* condition = cond->AsCondition();

    // If this is a long or FP comparison that has been folded into
    // the HCondition, generate the comparison directly.
    Primitive::Type type = condition->InputAt(0)->GetType();
    if (type == Primitive::kPrimLong || Primitive::IsFloatingPointType(type)) {
      GenerateCompareTestAndBranch(condition, true_target, false_target);
      return;
    }

    Label* non_fallthrough_target;
    Condition arm_cond;
    LocationSummary* locations = cond->GetLocations();
    DCHECK(locations->InAt(0).IsRegister());
    Register left = locations->InAt(0).AsRegister<Register>();
    Location right = locations->InAt(1);

    if (true_target == nullptr) {
      arm_cond = ARMCondition(condition->GetOppositeCondition());
      non_fallthrough_target = false_target;
    } else {
      arm_cond = ARMCondition(condition->GetCondition());
      non_fallthrough_target = true_target;
    }

    if (right.IsConstant() && (arm_cond == NE || arm_cond == EQ) &&
        CodeGenerator::GetInt32ValueOf(right.GetConstant()) == 0) {
      if (arm_cond == EQ) {
        __ CompareAndBranchIfZero(left, non_fallthrough_target);
      } else {
        DCHECK_EQ(arm_cond, NE);
        __ CompareAndBranchIfNonZero(left, non_fallthrough_target);
      }
    } else {
      if (right.IsRegister()) {
        __ cmp(left, ShifterOperand(right.AsRegister<Register>()));
      } else {
        DCHECK(right.IsConstant());
        __ CmpConstant(left, CodeGenerator::GetInt32ValueOf(right.GetConstant()));
      }

      __ b(non_fallthrough_target, arm_cond);
    }
  }

  // If neither branch falls through (case 3), the conditional branch to `true_target`
  // was already emitted (case 2) and we need to emit a jump to `false_target`.
  if (true_target != nullptr && false_target != nullptr) {
    __ b(false_target);
  }
}

void LocationsBuilderARM::VisitIf(HIf* if_instr) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(if_instr);
  if (IsBooleanValueOrMaterializedCondition(if_instr->InputAt(0))) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARM::VisitIf(HIf* if_instr) {
  HBasicBlock* true_successor = if_instr->IfTrueSuccessor();
  HBasicBlock* false_successor = if_instr->IfFalseSuccessor();
  Label* true_target = codegen_->GoesToNextBlock(if_instr->GetBlock(), true_successor) ?
      nullptr : codegen_->GetLabelOf(true_successor);
  Label* false_target = codegen_->GoesToNextBlock(if_instr->GetBlock(), false_successor) ?
      nullptr : codegen_->GetLabelOf(false_successor);
  GenerateTestAndBranch(if_instr, /* condition_input_index */ 0, true_target, false_target);
}

void LocationsBuilderARM::VisitDeoptimize(HDeoptimize* deoptimize) {
  LocationSummary* locations = new (GetGraph()->GetArena())
      LocationSummary(deoptimize, LocationSummary::kCallOnSlowPath);
  InvokeRuntimeCallingConvention calling_convention;
  RegisterSet caller_saves = RegisterSet::Empty();
  caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetCustomSlowPathCallerSaves(caller_saves);
  if (IsBooleanValueOrMaterializedCondition(deoptimize->InputAt(0))) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARM::VisitDeoptimize(HDeoptimize* deoptimize) {
  SlowPathCodeARM* slow_path = deopt_slow_paths_.NewSlowPath<DeoptimizationSlowPathARM>(deoptimize);
  GenerateTestAndBranch(deoptimize,
                        /* condition_input_index */ 0,
                        slow_path->GetEntryLabel(),
                        /* false_target */ nullptr);
}

void LocationsBuilderARM::VisitShouldDeoptimizeFlag(HShouldDeoptimizeFlag* flag) {
  LocationSummary* locations = new (GetGraph()->GetArena())
      LocationSummary(flag, LocationSummary::kNoCall);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM::VisitShouldDeoptimizeFlag(HShouldDeoptimizeFlag* flag) {
  __ LoadFromOffset(kLoadWord,
                    flag->GetLocations()->Out().AsRegister<Register>(),
                    SP,
                    codegen_->GetStackOffsetOfShouldDeoptimizeFlag());
}

void LocationsBuilderARM::VisitSelect(HSelect* select) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(select);
  const bool is_floating_point = Primitive::IsFloatingPointType(select->GetType());

  if (is_floating_point) {
    locations->SetInAt(0, Location::RequiresFpuRegister());
    locations->SetInAt(1, Location::FpuRegisterOrConstant(select->GetTrueValue()));
  } else {
    locations->SetInAt(0, Location::RequiresRegister());
    locations->SetInAt(1, Arm8BitEncodableConstantOrRegister(select->GetTrueValue()));
  }

  if (IsBooleanValueOrMaterializedCondition(select->GetCondition())) {
    locations->SetInAt(2, Location::RegisterOrConstant(select->GetCondition()));
    // The code generator handles overlap with the values, but not with the condition.
    locations->SetOut(Location::SameAsFirstInput());
  } else if (is_floating_point) {
    locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
  } else {
    if (!locations->InAt(1).IsConstant()) {
      locations->SetInAt(0, Arm8BitEncodableConstantOrRegister(select->GetFalseValue()));
    }

    locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
  }
}

void InstructionCodeGeneratorARM::VisitSelect(HSelect* select) {
  HInstruction* const condition = select->GetCondition();
  const LocationSummary* const locations = select->GetLocations();
  const Primitive::Type type = select->GetType();
  const Location first = locations->InAt(0);
  const Location out = locations->Out();
  const Location second = locations->InAt(1);
  Location src;

  if (condition->IsIntConstant()) {
    if (condition->AsIntConstant()->IsFalse()) {
      src = first;
    } else {
      src = second;
    }

    codegen_->MoveLocation(out, src, type);
    return;
  }

  if (!Primitive::IsFloatingPointType(type) &&
      (IsBooleanValueOrMaterializedCondition(condition) ||
       CanGenerateTest(condition->AsCondition(), codegen_->GetAssembler()))) {
    bool invert = false;

    if (out.Equals(second)) {
      src = first;
      invert = true;
    } else if (out.Equals(first)) {
      src = second;
    } else if (second.IsConstant()) {
      DCHECK(CanEncodeConstantAs8BitImmediate(second.GetConstant()));
      src = second;
    } else if (first.IsConstant()) {
      DCHECK(CanEncodeConstantAs8BitImmediate(first.GetConstant()));
      src = first;
      invert = true;
    } else {
      src = second;
    }

    if (CanGenerateConditionalMove(out, src)) {
      if (!out.Equals(first) && !out.Equals(second)) {
        codegen_->MoveLocation(out, src.Equals(first) ? second : first, type);
      }

      std::pair<Condition, Condition> cond;

      if (IsBooleanValueOrMaterializedCondition(condition)) {
        __ CmpConstant(locations->InAt(2).AsRegister<Register>(), 0);
        cond = invert ? std::make_pair(EQ, NE) : std::make_pair(NE, EQ);
      } else {
        cond = GenerateTest(condition->AsCondition(), invert, codegen_);
      }

      if (out.IsRegister()) {
        ShifterOperand operand;

        if (src.IsConstant()) {
          operand = ShifterOperand(CodeGenerator::GetInt32ValueOf(src.GetConstant()));
        } else {
          DCHECK(src.IsRegister());
          operand = ShifterOperand(src.AsRegister<Register>());
        }

        __ it(cond.first);
        __ mov(out.AsRegister<Register>(), operand, cond.first);
      } else {
        DCHECK(out.IsRegisterPair());

        ShifterOperand operand_high;
        ShifterOperand operand_low;

        if (src.IsConstant()) {
          const int64_t value = src.GetConstant()->AsLongConstant()->GetValue();

          operand_high = ShifterOperand(High32Bits(value));
          operand_low = ShifterOperand(Low32Bits(value));
        } else {
          DCHECK(src.IsRegisterPair());
          operand_high = ShifterOperand(src.AsRegisterPairHigh<Register>());
          operand_low = ShifterOperand(src.AsRegisterPairLow<Register>());
        }

        __ it(cond.first);
        __ mov(out.AsRegisterPairLow<Register>(), operand_low, cond.first);
        __ it(cond.first);
        __ mov(out.AsRegisterPairHigh<Register>(), operand_high, cond.first);
      }

      return;
    }
  }

  Label* false_target = nullptr;
  Label* true_target = nullptr;
  Label select_end;
  Label* target = codegen_->GetFinalLabel(select, &select_end);

  if (out.Equals(second)) {
    true_target = target;
    src = first;
  } else {
    false_target = target;
    src = second;

    if (!out.Equals(first)) {
      codegen_->MoveLocation(out, first, type);
    }
  }

  GenerateTestAndBranch(select, 2, true_target, false_target);
  codegen_->MoveLocation(out, src, type);

  if (select_end.IsLinked()) {
    __ Bind(&select_end);
  }
}

void LocationsBuilderARM::VisitNativeDebugInfo(HNativeDebugInfo* info) {
  new (GetGraph()->GetArena()) LocationSummary(info);
}

void InstructionCodeGeneratorARM::VisitNativeDebugInfo(HNativeDebugInfo*) {
  // MaybeRecordNativeDebugInfo is already called implicitly in CodeGenerator::Compile.
}

void CodeGeneratorARM::GenerateNop() {
  __ nop();
}

void LocationsBuilderARM::HandleCondition(HCondition* cond) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(cond, LocationSummary::kNoCall);
  // Handle the long/FP comparisons made in instruction simplification.
  switch (cond->InputAt(0)->GetType()) {
    case Primitive::kPrimLong:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(cond->InputAt(1)));
      if (!cond->IsEmittedAtUseSite()) {
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      }
      break;

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, ArithmeticZeroOrFpuRegister(cond->InputAt(1)));
      if (!cond->IsEmittedAtUseSite()) {
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      }
      break;

    default:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(cond->InputAt(1)));
      if (!cond->IsEmittedAtUseSite()) {
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      }
  }
}

void InstructionCodeGeneratorARM::HandleCondition(HCondition* cond) {
  if (cond->IsEmittedAtUseSite()) {
    return;
  }

  const Register out = cond->GetLocations()->Out().AsRegister<Register>();

  if (ArmAssembler::IsLowRegister(out) && CanGenerateTest(cond, codegen_->GetAssembler())) {
    const auto condition = GenerateTest(cond, false, codegen_);

    __ it(condition.first);
    __ mov(out, ShifterOperand(1), condition.first);
    __ it(condition.second);
    __ mov(out, ShifterOperand(0), condition.second);
    return;
  }

  // Convert the jumps into the result.
  Label done_label;
  Label* const final_label = codegen_->GetFinalLabel(cond, &done_label);

  if (cond->InputAt(0)->GetType() == Primitive::kPrimLong) {
    Label true_label, false_label;

    GenerateLongComparesAndJumps(cond, &true_label, &false_label);

    // False case: result = 0.
    __ Bind(&false_label);
    __ LoadImmediate(out, 0);
    __ b(final_label);

    // True case: result = 1.
    __ Bind(&true_label);
    __ LoadImmediate(out, 1);
  } else {
    DCHECK(CanGenerateTest(cond, codegen_->GetAssembler()));

    const auto condition = GenerateTest(cond, false, codegen_);

    __ mov(out, ShifterOperand(0), AL, kCcKeep);
    __ b(final_label, condition.second);
    __ LoadImmediate(out, 1);
  }

  if (done_label.IsLinked()) {
    __ Bind(&done_label);
  }
}

void LocationsBuilderARM::VisitEqual(HEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitEqual(HEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitNotEqual(HNotEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitNotEqual(HNotEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitLessThan(HLessThan* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitLessThan(HLessThan* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitLessThanOrEqual(HLessThanOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitLessThanOrEqual(HLessThanOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitGreaterThan(HGreaterThan* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitGreaterThan(HGreaterThan* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitGreaterThanOrEqual(HGreaterThanOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitGreaterThanOrEqual(HGreaterThanOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitBelow(HBelow* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitBelow(HBelow* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitBelowOrEqual(HBelowOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitBelowOrEqual(HBelowOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitAbove(HAbove* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitAbove(HAbove* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitAboveOrEqual(HAboveOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARM::VisitAboveOrEqual(HAboveOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARM::VisitIntConstant(HIntConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARM::VisitIntConstant(HIntConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARM::VisitNullConstant(HNullConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARM::VisitNullConstant(HNullConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARM::VisitLongConstant(HLongConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARM::VisitLongConstant(HLongConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARM::VisitFloatConstant(HFloatConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARM::VisitFloatConstant(HFloatConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARM::VisitDoubleConstant(HDoubleConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARM::VisitDoubleConstant(HDoubleConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARM::VisitMemoryBarrier(HMemoryBarrier* memory_barrier) {
  memory_barrier->SetLocations(nullptr);
}

void InstructionCodeGeneratorARM::VisitMemoryBarrier(HMemoryBarrier* memory_barrier) {
  codegen_->GenerateMemoryBarrier(memory_barrier->GetBarrierKind());
}

void LocationsBuilderARM::VisitReturnVoid(HReturnVoid* ret) {
  ret->SetLocations(nullptr);
}

void InstructionCodeGeneratorARM::VisitReturnVoid(HReturnVoid* ret ATTRIBUTE_UNUSED) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderARM::VisitReturn(HReturn* ret) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(ret, LocationSummary::kNoCall);
  locations->SetInAt(0, parameter_visitor_.GetReturnLocation(ret->InputAt(0)->GetType()));
}

void InstructionCodeGeneratorARM::VisitReturn(HReturn* ret ATTRIBUTE_UNUSED) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderARM::VisitInvokeUnresolved(HInvokeUnresolved* invoke) {
  // The trampoline uses the same calling convention as dex calling conventions,
  // except instead of loading arg0/r0 with the target Method*, arg0/r0 will contain
  // the method_idx.
  HandleInvoke(invoke);
}

void InstructionCodeGeneratorARM::VisitInvokeUnresolved(HInvokeUnresolved* invoke) {
  codegen_->GenerateInvokeUnresolvedRuntimeCall(invoke);
}

void LocationsBuilderARM::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) {
  // Explicit clinit checks triggered by static invokes must have been pruned by
  // art::PrepareForRegisterAllocation.
  DCHECK(!invoke->IsStaticWithExplicitClinitCheck());

  IntrinsicLocationsBuilderARM intrinsic(codegen_);
  if (intrinsic.TryDispatch(invoke)) {
    if (invoke->GetLocations()->CanCall() && invoke->HasPcRelativeDexCache()) {
      invoke->GetLocations()->SetInAt(invoke->GetSpecialInputIndex(), Location::Any());
    }
    return;
  }

  HandleInvoke(invoke);

  // For PC-relative dex cache the invoke has an extra input, the PC-relative address base.
  if (invoke->HasPcRelativeDexCache()) {
    invoke->GetLocations()->SetInAt(invoke->GetSpecialInputIndex(), Location::RequiresRegister());
  }
}

static bool TryGenerateIntrinsicCode(HInvoke* invoke, CodeGeneratorARM* codegen) {
  if (invoke->GetLocations()->Intrinsified()) {
    IntrinsicCodeGeneratorARM intrinsic(codegen);
    intrinsic.Dispatch(invoke);
    return true;
  }
  return false;
}

void InstructionCodeGeneratorARM::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) {
  // Explicit clinit checks triggered by static invokes must have been pruned by
  // art::PrepareForRegisterAllocation.
  DCHECK(!invoke->IsStaticWithExplicitClinitCheck());

  if (TryGenerateIntrinsicCode(invoke, codegen_)) {
    return;
  }

  LocationSummary* locations = invoke->GetLocations();
  codegen_->GenerateStaticOrDirectCall(
      invoke, locations->HasTemps() ? locations->GetTemp(0) : Location::NoLocation());
  codegen_->RecordPcInfo(invoke, invoke->GetDexPc());
}

void LocationsBuilderARM::HandleInvoke(HInvoke* invoke) {
  InvokeDexCallingConventionVisitorARM calling_convention_visitor;
  CodeGenerator::CreateCommonInvokeLocationSummary(invoke, &calling_convention_visitor);
}

void LocationsBuilderARM::VisitInvokeVirtual(HInvokeVirtual* invoke) {
  IntrinsicLocationsBuilderARM intrinsic(codegen_);
  if (intrinsic.TryDispatch(invoke)) {
    return;
  }

  HandleInvoke(invoke);
}

void InstructionCodeGeneratorARM::VisitInvokeVirtual(HInvokeVirtual* invoke) {
  if (TryGenerateIntrinsicCode(invoke, codegen_)) {
    return;
  }

  codegen_->GenerateVirtualCall(invoke, invoke->GetLocations()->GetTemp(0));
  DCHECK(!codegen_->IsLeafMethod());
  codegen_->RecordPcInfo(invoke, invoke->GetDexPc());
}

void LocationsBuilderARM::VisitInvokeInterface(HInvokeInterface* invoke) {
  HandleInvoke(invoke);
  // Add the hidden argument.
  invoke->GetLocations()->AddTemp(Location::RegisterLocation(R12));
}

void InstructionCodeGeneratorARM::VisitInvokeInterface(HInvokeInterface* invoke) {
  // TODO: b/18116999, our IMTs can miss an IncompatibleClassChangeError.
  LocationSummary* locations = invoke->GetLocations();
  Register temp = locations->GetTemp(0).AsRegister<Register>();
  Register hidden_reg = locations->GetTemp(1).AsRegister<Register>();
  Location receiver = locations->InAt(0);
  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();

  // Set the hidden argument. This is safe to do this here, as R12
  // won't be modified thereafter, before the `blx` (call) instruction.
  DCHECK_EQ(R12, hidden_reg);
  __ LoadImmediate(hidden_reg, invoke->GetDexMethodIndex());

  if (receiver.IsStackSlot()) {
    __ LoadFromOffset(kLoadWord, temp, SP, receiver.GetStackIndex());
    // /* HeapReference<Class> */ temp = temp->klass_
    __ LoadFromOffset(kLoadWord, temp, temp, class_offset);
  } else {
    // /* HeapReference<Class> */ temp = receiver->klass_
    __ LoadFromOffset(kLoadWord, temp, receiver.AsRegister<Register>(), class_offset);
  }
  codegen_->MaybeRecordImplicitNullCheck(invoke);
  // Instead of simply (possibly) unpoisoning `temp` here, we should
  // emit a read barrier for the previous class reference load.
  // However this is not required in practice, as this is an
  // intermediate/temporary reference and because the current
  // concurrent copying collector keeps the from-space memory
  // intact/accessible until the end of the marking phase (the
  // concurrent copying collector may not in the future).
  __ MaybeUnpoisonHeapReference(temp);
  __ LoadFromOffset(kLoadWord, temp, temp,
        mirror::Class::ImtPtrOffset(kArmPointerSize).Uint32Value());
  uint32_t method_offset = static_cast<uint32_t>(ImTable::OffsetOfElement(
      invoke->GetImtIndex(), kArmPointerSize));
  // temp = temp->GetImtEntryAt(method_offset);
  __ LoadFromOffset(kLoadWord, temp, temp, method_offset);
  uint32_t entry_point =
      ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArmPointerSize).Int32Value();
  // LR = temp->GetEntryPoint();
  __ LoadFromOffset(kLoadWord, LR, temp, entry_point);
  // LR();
  __ blx(LR);
  DCHECK(!codegen_->IsLeafMethod());
  codegen_->RecordPcInfo(invoke, invoke->GetDexPc());
}

void LocationsBuilderARM::VisitInvokePolymorphic(HInvokePolymorphic* invoke) {
  HandleInvoke(invoke);
}

void InstructionCodeGeneratorARM::VisitInvokePolymorphic(HInvokePolymorphic* invoke) {
  codegen_->GenerateInvokePolymorphicCall(invoke);
}

void LocationsBuilderARM::VisitNeg(HNeg* neg) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(neg, LocationSummary::kNoCall);
  switch (neg->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }
    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;

    default:
      LOG(FATAL) << "Unexpected neg type " << neg->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitNeg(HNeg* neg) {
  LocationSummary* locations = neg->GetLocations();
  Location out = locations->Out();
  Location in = locations->InAt(0);
  switch (neg->GetResultType()) {
    case Primitive::kPrimInt:
      DCHECK(in.IsRegister());
      __ rsb(out.AsRegister<Register>(), in.AsRegister<Register>(), ShifterOperand(0));
      break;

    case Primitive::kPrimLong:
      DCHECK(in.IsRegisterPair());
      // out.lo = 0 - in.lo (and update the carry/borrow (C) flag)
      __ rsbs(out.AsRegisterPairLow<Register>(),
              in.AsRegisterPairLow<Register>(),
              ShifterOperand(0));
      // We cannot emit an RSC (Reverse Subtract with Carry)
      // instruction here, as it does not exist in the Thumb-2
      // instruction set.  We use the following approach
      // using SBC and SUB instead.
      //
      // out.hi = -C
      __ sbc(out.AsRegisterPairHigh<Register>(),
             out.AsRegisterPairHigh<Register>(),
             ShifterOperand(out.AsRegisterPairHigh<Register>()));
      // out.hi = out.hi - in.hi
      __ sub(out.AsRegisterPairHigh<Register>(),
             out.AsRegisterPairHigh<Register>(),
             ShifterOperand(in.AsRegisterPairHigh<Register>()));
      break;

    case Primitive::kPrimFloat:
      DCHECK(in.IsFpuRegister());
      __ vnegs(out.AsFpuRegister<SRegister>(), in.AsFpuRegister<SRegister>());
      break;

    case Primitive::kPrimDouble:
      DCHECK(in.IsFpuRegisterPair());
      __ vnegd(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(in.AsFpuRegisterPairLow<SRegister>()));
      break;

    default:
      LOG(FATAL) << "Unexpected neg type " << neg->GetResultType();
  }
}

void LocationsBuilderARM::VisitTypeConversion(HTypeConversion* conversion) {
  Primitive::Type result_type = conversion->GetResultType();
  Primitive::Type input_type = conversion->GetInputType();
  DCHECK_NE(result_type, input_type);

  // The float-to-long, double-to-long and long-to-float type conversions
  // rely on a call to the runtime.
  LocationSummary::CallKind call_kind =
      (((input_type == Primitive::kPrimFloat || input_type == Primitive::kPrimDouble)
        && result_type == Primitive::kPrimLong)
       || (input_type == Primitive::kPrimLong && result_type == Primitive::kPrimFloat))
      ? LocationSummary::kCallOnMainOnly
      : LocationSummary::kNoCall;
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(conversion, call_kind);

  // The Java language does not allow treating boolean as an integral type but
  // our bit representation makes it safe.

  switch (result_type) {
    case Primitive::kPrimByte:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to byte is a result of code transformations.
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-byte' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimShort:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to short is a result of code transformations.
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-short' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimInt:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Processing a Dex `long-to-int' instruction.
          locations->SetInAt(0, Location::Any());
          locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
          break;

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-int' instruction.
          locations->SetInAt(0, Location::RequiresFpuRegister());
          locations->SetOut(Location::RequiresRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          break;

        case Primitive::kPrimDouble:
          // Processing a Dex `double-to-int' instruction.
          locations->SetInAt(0, Location::RequiresFpuRegister());
          locations->SetOut(Location::RequiresRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimLong:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-long' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
          break;

        case Primitive::kPrimFloat: {
          // Processing a Dex `float-to-long' instruction.
          InvokeRuntimeCallingConvention calling_convention;
          locations->SetInAt(0, Location::FpuRegisterLocation(
              calling_convention.GetFpuRegisterAt(0)));
          locations->SetOut(Location::RegisterPairLocation(R0, R1));
          break;
        }

        case Primitive::kPrimDouble: {
          // Processing a Dex `double-to-long' instruction.
          InvokeRuntimeCallingConvention calling_convention;
          locations->SetInAt(0, Location::FpuRegisterPairLocation(
              calling_convention.GetFpuRegisterAt(0),
              calling_convention.GetFpuRegisterAt(1)));
          locations->SetOut(Location::RegisterPairLocation(R0, R1));
          break;
        }

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimChar:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to char is a result of code transformations.
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
          // Processing a Dex `int-to-char' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimFloat:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-float' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresFpuRegister());
          break;

        case Primitive::kPrimLong: {
          // Processing a Dex `long-to-float' instruction.
          InvokeRuntimeCallingConvention calling_convention;
          locations->SetInAt(0, Location::RegisterPairLocation(
              calling_convention.GetRegisterAt(0), calling_convention.GetRegisterAt(1)));
          locations->SetOut(Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
          break;
        }

        case Primitive::kPrimDouble:
          // Processing a Dex `double-to-float' instruction.
          locations->SetInAt(0, Location::RequiresFpuRegister());
          locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      };
      break;

    case Primitive::kPrimDouble:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-double' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresFpuRegister());
          break;

        case Primitive::kPrimLong:
          // Processing a Dex `long-to-double' instruction.
          locations->SetInAt(0, Location::RequiresRegister());
          locations->SetOut(Location::RequiresFpuRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          locations->AddTemp(Location::RequiresFpuRegister());
          break;

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-double' instruction.
          locations->SetInAt(0, Location::RequiresFpuRegister());
          locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      };
      break;

    default:
      LOG(FATAL) << "Unexpected type conversion from " << input_type
                 << " to " << result_type;
  }
}

void InstructionCodeGeneratorARM::VisitTypeConversion(HTypeConversion* conversion) {
  LocationSummary* locations = conversion->GetLocations();
  Location out = locations->Out();
  Location in = locations->InAt(0);
  Primitive::Type result_type = conversion->GetResultType();
  Primitive::Type input_type = conversion->GetInputType();
  DCHECK_NE(result_type, input_type);
  switch (result_type) {
    case Primitive::kPrimByte:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to byte is a result of code transformations.
          __ sbfx(out.AsRegister<Register>(), in.AsRegisterPairLow<Register>(), 0, 8);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-byte' instruction.
          __ sbfx(out.AsRegister<Register>(), in.AsRegister<Register>(), 0, 8);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimShort:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to short is a result of code transformations.
          __ sbfx(out.AsRegister<Register>(), in.AsRegisterPairLow<Register>(), 0, 16);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-short' instruction.
          __ sbfx(out.AsRegister<Register>(), in.AsRegister<Register>(), 0, 16);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimInt:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Processing a Dex `long-to-int' instruction.
          DCHECK(out.IsRegister());
          if (in.IsRegisterPair()) {
            __ Mov(out.AsRegister<Register>(), in.AsRegisterPairLow<Register>());
          } else if (in.IsDoubleStackSlot()) {
            __ LoadFromOffset(kLoadWord, out.AsRegister<Register>(), SP, in.GetStackIndex());
          } else {
            DCHECK(in.IsConstant());
            DCHECK(in.GetConstant()->IsLongConstant());
            int64_t value = in.GetConstant()->AsLongConstant()->GetValue();
            __ LoadImmediate(out.AsRegister<Register>(), static_cast<int32_t>(value));
          }
          break;

        case Primitive::kPrimFloat: {
          // Processing a Dex `float-to-int' instruction.
          SRegister temp = locations->GetTemp(0).AsFpuRegisterPairLow<SRegister>();
          __ vcvtis(temp, in.AsFpuRegister<SRegister>());
          __ vmovrs(out.AsRegister<Register>(), temp);
          break;
        }

        case Primitive::kPrimDouble: {
          // Processing a Dex `double-to-int' instruction.
          SRegister temp_s = locations->GetTemp(0).AsFpuRegisterPairLow<SRegister>();
          __ vcvtid(temp_s, FromLowSToD(in.AsFpuRegisterPairLow<SRegister>()));
          __ vmovrs(out.AsRegister<Register>(), temp_s);
          break;
        }

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimLong:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-long' instruction.
          DCHECK(out.IsRegisterPair());
          DCHECK(in.IsRegister());
          __ Mov(out.AsRegisterPairLow<Register>(), in.AsRegister<Register>());
          // Sign extension.
          __ Asr(out.AsRegisterPairHigh<Register>(),
                 out.AsRegisterPairLow<Register>(),
                 31);
          break;

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-long' instruction.
          codegen_->InvokeRuntime(kQuickF2l, conversion, conversion->GetDexPc());
          CheckEntrypointTypes<kQuickF2l, int64_t, float>();
          break;

        case Primitive::kPrimDouble:
          // Processing a Dex `double-to-long' instruction.
          codegen_->InvokeRuntime(kQuickD2l, conversion, conversion->GetDexPc());
          CheckEntrypointTypes<kQuickD2l, int64_t, double>();
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimChar:
      switch (input_type) {
        case Primitive::kPrimLong:
          // Type conversion from long to char is a result of code transformations.
          __ ubfx(out.AsRegister<Register>(), in.AsRegisterPairLow<Register>(), 0, 16);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
          // Processing a Dex `int-to-char' instruction.
          __ ubfx(out.AsRegister<Register>(), in.AsRegister<Register>(), 0, 16);
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      }
      break;

    case Primitive::kPrimFloat:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar: {
          // Processing a Dex `int-to-float' instruction.
          __ vmovsr(out.AsFpuRegister<SRegister>(), in.AsRegister<Register>());
          __ vcvtsi(out.AsFpuRegister<SRegister>(), out.AsFpuRegister<SRegister>());
          break;
        }

        case Primitive::kPrimLong:
          // Processing a Dex `long-to-float' instruction.
          codegen_->InvokeRuntime(kQuickL2f, conversion, conversion->GetDexPc());
          CheckEntrypointTypes<kQuickL2f, float, int64_t>();
          break;

        case Primitive::kPrimDouble:
          // Processing a Dex `double-to-float' instruction.
          __ vcvtsd(out.AsFpuRegister<SRegister>(),
                    FromLowSToD(in.AsFpuRegisterPairLow<SRegister>()));
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      };
      break;

    case Primitive::kPrimDouble:
      switch (input_type) {
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar: {
          // Processing a Dex `int-to-double' instruction.
          __ vmovsr(out.AsFpuRegisterPairLow<SRegister>(), in.AsRegister<Register>());
          __ vcvtdi(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
                    out.AsFpuRegisterPairLow<SRegister>());
          break;
        }

        case Primitive::kPrimLong: {
          // Processing a Dex `long-to-double' instruction.
          Register low = in.AsRegisterPairLow<Register>();
          Register high = in.AsRegisterPairHigh<Register>();
          SRegister out_s = out.AsFpuRegisterPairLow<SRegister>();
          DRegister out_d = FromLowSToD(out_s);
          SRegister temp_s = locations->GetTemp(0).AsFpuRegisterPairLow<SRegister>();
          DRegister temp_d = FromLowSToD(temp_s);
          SRegister constant_s = locations->GetTemp(1).AsFpuRegisterPairLow<SRegister>();
          DRegister constant_d = FromLowSToD(constant_s);

          // temp_d = int-to-double(high)
          __ vmovsr(temp_s, high);
          __ vcvtdi(temp_d, temp_s);
          // constant_d = k2Pow32EncodingForDouble
          __ LoadDImmediate(constant_d, bit_cast<double, int64_t>(k2Pow32EncodingForDouble));
          // out_d = unsigned-to-double(low)
          __ vmovsr(out_s, low);
          __ vcvtdu(out_d, out_s);
          // out_d += temp_d * constant_d
          __ vmlad(out_d, temp_d, constant_d);
          break;
        }

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-double' instruction.
          __ vcvtds(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
                    in.AsFpuRegister<SRegister>());
          break;

        default:
          LOG(FATAL) << "Unexpected type conversion from " << input_type
                     << " to " << result_type;
      };
      break;

    default:
      LOG(FATAL) << "Unexpected type conversion from " << input_type
                 << " to " << result_type;
  }
}

void LocationsBuilderARM::VisitAdd(HAdd* add) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(add, LocationSummary::kNoCall);
  switch (add->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(add->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, ArmEncodableConstantOrRegister(add->InputAt(1), ADD));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    }

    default:
      LOG(FATAL) << "Unexpected add type " << add->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitAdd(HAdd* add) {
  LocationSummary* locations = add->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  switch (add->GetResultType()) {
    case Primitive::kPrimInt:
      if (second.IsRegister()) {
        __ add(out.AsRegister<Register>(),
               first.AsRegister<Register>(),
               ShifterOperand(second.AsRegister<Register>()));
      } else {
        __ AddConstant(out.AsRegister<Register>(),
                       first.AsRegister<Register>(),
                       second.GetConstant()->AsIntConstant()->GetValue());
      }
      break;

    case Primitive::kPrimLong: {
      if (second.IsConstant()) {
        uint64_t value = static_cast<uint64_t>(Int64FromConstant(second.GetConstant()));
        GenerateAddLongConst(out, first, value);
      } else {
        DCHECK(second.IsRegisterPair());
        __ adds(out.AsRegisterPairLow<Register>(),
                first.AsRegisterPairLow<Register>(),
                ShifterOperand(second.AsRegisterPairLow<Register>()));
        __ adc(out.AsRegisterPairHigh<Register>(),
               first.AsRegisterPairHigh<Register>(),
               ShifterOperand(second.AsRegisterPairHigh<Register>()));
      }
      break;
    }

    case Primitive::kPrimFloat:
      __ vadds(out.AsFpuRegister<SRegister>(),
               first.AsFpuRegister<SRegister>(),
               second.AsFpuRegister<SRegister>());
      break;

    case Primitive::kPrimDouble:
      __ vaddd(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(first.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(second.AsFpuRegisterPairLow<SRegister>()));
      break;

    default:
      LOG(FATAL) << "Unexpected add type " << add->GetResultType();
  }
}

void LocationsBuilderARM::VisitSub(HSub* sub) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(sub, LocationSummary::kNoCall);
  switch (sub->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(sub->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, ArmEncodableConstantOrRegister(sub->InputAt(1), SUB));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected sub type " << sub->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitSub(HSub* sub) {
  LocationSummary* locations = sub->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  switch (sub->GetResultType()) {
    case Primitive::kPrimInt: {
      if (second.IsRegister()) {
        __ sub(out.AsRegister<Register>(),
               first.AsRegister<Register>(),
               ShifterOperand(second.AsRegister<Register>()));
      } else {
        __ AddConstant(out.AsRegister<Register>(),
                       first.AsRegister<Register>(),
                       -second.GetConstant()->AsIntConstant()->GetValue());
      }
      break;
    }

    case Primitive::kPrimLong: {
      if (second.IsConstant()) {
        uint64_t value = static_cast<uint64_t>(Int64FromConstant(second.GetConstant()));
        GenerateAddLongConst(out, first, -value);
      } else {
        DCHECK(second.IsRegisterPair());
        __ subs(out.AsRegisterPairLow<Register>(),
                first.AsRegisterPairLow<Register>(),
                ShifterOperand(second.AsRegisterPairLow<Register>()));
        __ sbc(out.AsRegisterPairHigh<Register>(),
               first.AsRegisterPairHigh<Register>(),
               ShifterOperand(second.AsRegisterPairHigh<Register>()));
      }
      break;
    }

    case Primitive::kPrimFloat: {
      __ vsubs(out.AsFpuRegister<SRegister>(),
               first.AsFpuRegister<SRegister>(),
               second.AsFpuRegister<SRegister>());
      break;
    }

    case Primitive::kPrimDouble: {
      __ vsubd(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(first.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(second.AsFpuRegisterPairLow<SRegister>()));
      break;
    }


    default:
      LOG(FATAL) << "Unexpected sub type " << sub->GetResultType();
  }
}

void LocationsBuilderARM::VisitMul(HMul* mul) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(mul, LocationSummary::kNoCall);
  switch (mul->GetResultType()) {
    case Primitive::kPrimInt:
    case Primitive::kPrimLong:  {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RequiresRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    }

    default:
      LOG(FATAL) << "Unexpected mul type " << mul->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitMul(HMul* mul) {
  LocationSummary* locations = mul->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  switch (mul->GetResultType()) {
    case Primitive::kPrimInt: {
      __ mul(out.AsRegister<Register>(),
             first.AsRegister<Register>(),
             second.AsRegister<Register>());
      break;
    }
    case Primitive::kPrimLong: {
      Register out_hi = out.AsRegisterPairHigh<Register>();
      Register out_lo = out.AsRegisterPairLow<Register>();
      Register in1_hi = first.AsRegisterPairHigh<Register>();
      Register in1_lo = first.AsRegisterPairLow<Register>();
      Register in2_hi = second.AsRegisterPairHigh<Register>();
      Register in2_lo = second.AsRegisterPairLow<Register>();

      // Extra checks to protect caused by the existence of R1_R2.
      // The algorithm is wrong if out.hi is either in1.lo or in2.lo:
      // (e.g. in1=r0_r1, in2=r2_r3 and out=r1_r2);
      DCHECK_NE(out_hi, in1_lo);
      DCHECK_NE(out_hi, in2_lo);

      // input: in1 - 64 bits, in2 - 64 bits
      // output: out
      // formula: out.hi : out.lo = (in1.lo * in2.hi + in1.hi * in2.lo)* 2^32 + in1.lo * in2.lo
      // parts: out.hi = in1.lo * in2.hi + in1.hi * in2.lo + (in1.lo * in2.lo)[63:32]
      // parts: out.lo = (in1.lo * in2.lo)[31:0]

      // IP <- in1.lo * in2.hi
      __ mul(IP, in1_lo, in2_hi);
      // out.hi <- in1.lo * in2.hi + in1.hi * in2.lo
      __ mla(out_hi, in1_hi, in2_lo, IP);
      // out.lo <- (in1.lo * in2.lo)[31:0];
      __ umull(out_lo, IP, in1_lo, in2_lo);
      // out.hi <- in2.hi * in1.lo +  in2.lo * in1.hi + (in1.lo * in2.lo)[63:32]
      __ add(out_hi, out_hi, ShifterOperand(IP));
      break;
    }

    case Primitive::kPrimFloat: {
      __ vmuls(out.AsFpuRegister<SRegister>(),
               first.AsFpuRegister<SRegister>(),
               second.AsFpuRegister<SRegister>());
      break;
    }

    case Primitive::kPrimDouble: {
      __ vmuld(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(first.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(second.AsFpuRegisterPairLow<SRegister>()));
      break;
    }

    default:
      LOG(FATAL) << "Unexpected mul type " << mul->GetResultType();
  }
}

void InstructionCodeGeneratorARM::DivRemOneOrMinusOne(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  Register out = locations->Out().AsRegister<Register>();
  Register dividend = locations->InAt(0).AsRegister<Register>();
  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  DCHECK(imm == 1 || imm == -1);

  if (instruction->IsRem()) {
    __ LoadImmediate(out, 0);
  } else {
    if (imm == 1) {
      __ Mov(out, dividend);
    } else {
      __ rsb(out, dividend, ShifterOperand(0));
    }
  }
}

void InstructionCodeGeneratorARM::DivRemByPowerOfTwo(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  Register out = locations->Out().AsRegister<Register>();
  Register dividend = locations->InAt(0).AsRegister<Register>();
  Register temp = locations->GetTemp(0).AsRegister<Register>();
  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  uint32_t abs_imm = static_cast<uint32_t>(AbsOrMin(imm));
  int ctz_imm = CTZ(abs_imm);

  if (ctz_imm == 1) {
    __ Lsr(temp, dividend, 32 - ctz_imm);
  } else {
    __ Asr(temp, dividend, 31);
    __ Lsr(temp, temp, 32 - ctz_imm);
  }
  __ add(out, temp, ShifterOperand(dividend));

  if (instruction->IsDiv()) {
    __ Asr(out, out, ctz_imm);
    if (imm < 0) {
      __ rsb(out, out, ShifterOperand(0));
    }
  } else {
    __ ubfx(out, out, 0, ctz_imm);
    __ sub(out, out, ShifterOperand(temp));
  }
}

void InstructionCodeGeneratorARM::GenerateDivRemWithAnyConstant(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  Register out = locations->Out().AsRegister<Register>();
  Register dividend = locations->InAt(0).AsRegister<Register>();
  Register temp1 = locations->GetTemp(0).AsRegister<Register>();
  Register temp2 = locations->GetTemp(1).AsRegister<Register>();
  int64_t imm = second.GetConstant()->AsIntConstant()->GetValue();

  int64_t magic;
  int shift;
  CalculateMagicAndShiftForDivRem(imm, false /* is_long */, &magic, &shift);

  __ LoadImmediate(temp1, magic);
  __ smull(temp2, temp1, dividend, temp1);

  if (imm > 0 && magic < 0) {
    __ add(temp1, temp1, ShifterOperand(dividend));
  } else if (imm < 0 && magic > 0) {
    __ sub(temp1, temp1, ShifterOperand(dividend));
  }

  if (shift != 0) {
    __ Asr(temp1, temp1, shift);
  }

  if (instruction->IsDiv()) {
    __ sub(out, temp1, ShifterOperand(temp1, ASR, 31));
  } else {
    __ sub(temp1, temp1, ShifterOperand(temp1, ASR, 31));
    // TODO: Strength reduction for mls.
    __ LoadImmediate(temp2, imm);
    __ mls(out, temp1, temp2, dividend);
  }
}

void InstructionCodeGeneratorARM::GenerateDivRemConstantIntegral(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  if (imm == 0) {
    // Do not generate anything. DivZeroCheck would prevent any code to be executed.
  } else if (imm == 1 || imm == -1) {
    DivRemOneOrMinusOne(instruction);
  } else if (IsPowerOfTwo(AbsOrMin(imm))) {
    DivRemByPowerOfTwo(instruction);
  } else {
    DCHECK(imm <= -2 || imm >= 2);
    GenerateDivRemWithAnyConstant(instruction);
  }
}

void LocationsBuilderARM::VisitDiv(HDiv* div) {
  LocationSummary::CallKind call_kind = LocationSummary::kNoCall;
  if (div->GetResultType() == Primitive::kPrimLong) {
    // pLdiv runtime call.
    call_kind = LocationSummary::kCallOnMainOnly;
  } else if (div->GetResultType() == Primitive::kPrimInt && div->InputAt(1)->IsConstant()) {
    // sdiv will be replaced by other instruction sequence.
  } else if (div->GetResultType() == Primitive::kPrimInt &&
             !codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
    // pIdivmod runtime call.
    call_kind = LocationSummary::kCallOnMainOnly;
  }

  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(div, call_kind);

  switch (div->GetResultType()) {
    case Primitive::kPrimInt: {
      if (div->InputAt(1)->IsConstant()) {
        locations->SetInAt(0, Location::RequiresRegister());
        locations->SetInAt(1, Location::ConstantLocation(div->InputAt(1)->AsConstant()));
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
        int32_t value = div->InputAt(1)->AsIntConstant()->GetValue();
        if (value == 1 || value == 0 || value == -1) {
          // No temp register required.
        } else {
          locations->AddTemp(Location::RequiresRegister());
          if (!IsPowerOfTwo(AbsOrMin(value))) {
            locations->AddTemp(Location::RequiresRegister());
          }
        }
      } else if (codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
        locations->SetInAt(0, Location::RequiresRegister());
        locations->SetInAt(1, Location::RequiresRegister());
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      } else {
        InvokeRuntimeCallingConvention calling_convention;
        locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
        locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
        // Note: divmod will compute both the quotient and the remainder as the pair R0 and R1, but
        //       we only need the former.
        locations->SetOut(Location::RegisterLocation(R0));
      }
      break;
    }
    case Primitive::kPrimLong: {
      InvokeRuntimeCallingConvention calling_convention;
      locations->SetInAt(0, Location::RegisterPairLocation(
          calling_convention.GetRegisterAt(0), calling_convention.GetRegisterAt(1)));
      locations->SetInAt(1, Location::RegisterPairLocation(
          calling_convention.GetRegisterAt(2), calling_convention.GetRegisterAt(3)));
      locations->SetOut(Location::RegisterPairLocation(R0, R1));
      break;
    }
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    }

    default:
      LOG(FATAL) << "Unexpected div type " << div->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitDiv(HDiv* div) {
  LocationSummary* locations = div->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  switch (div->GetResultType()) {
    case Primitive::kPrimInt: {
      if (second.IsConstant()) {
        GenerateDivRemConstantIntegral(div);
      } else if (codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
        __ sdiv(out.AsRegister<Register>(),
                first.AsRegister<Register>(),
                second.AsRegister<Register>());
      } else {
        InvokeRuntimeCallingConvention calling_convention;
        DCHECK_EQ(calling_convention.GetRegisterAt(0), first.AsRegister<Register>());
        DCHECK_EQ(calling_convention.GetRegisterAt(1), second.AsRegister<Register>());
        DCHECK_EQ(R0, out.AsRegister<Register>());

        codegen_->InvokeRuntime(kQuickIdivmod, div, div->GetDexPc());
        CheckEntrypointTypes<kQuickIdivmod, int32_t, int32_t, int32_t>();
      }
      break;
    }

    case Primitive::kPrimLong: {
      InvokeRuntimeCallingConvention calling_convention;
      DCHECK_EQ(calling_convention.GetRegisterAt(0), first.AsRegisterPairLow<Register>());
      DCHECK_EQ(calling_convention.GetRegisterAt(1), first.AsRegisterPairHigh<Register>());
      DCHECK_EQ(calling_convention.GetRegisterAt(2), second.AsRegisterPairLow<Register>());
      DCHECK_EQ(calling_convention.GetRegisterAt(3), second.AsRegisterPairHigh<Register>());
      DCHECK_EQ(R0, out.AsRegisterPairLow<Register>());
      DCHECK_EQ(R1, out.AsRegisterPairHigh<Register>());

      codegen_->InvokeRuntime(kQuickLdiv, div, div->GetDexPc());
      CheckEntrypointTypes<kQuickLdiv, int64_t, int64_t, int64_t>();
      break;
    }

    case Primitive::kPrimFloat: {
      __ vdivs(out.AsFpuRegister<SRegister>(),
               first.AsFpuRegister<SRegister>(),
               second.AsFpuRegister<SRegister>());
      break;
    }

    case Primitive::kPrimDouble: {
      __ vdivd(FromLowSToD(out.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(first.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(second.AsFpuRegisterPairLow<SRegister>()));
      break;
    }

    default:
      LOG(FATAL) << "Unexpected div type " << div->GetResultType();
  }
}

void LocationsBuilderARM::VisitRem(HRem* rem) {
  Primitive::Type type = rem->GetResultType();

  // Most remainders are implemented in the runtime.
  LocationSummary::CallKind call_kind = LocationSummary::kCallOnMainOnly;
  if (rem->GetResultType() == Primitive::kPrimInt && rem->InputAt(1)->IsConstant()) {
    // sdiv will be replaced by other instruction sequence.
    call_kind = LocationSummary::kNoCall;
  } else if ((rem->GetResultType() == Primitive::kPrimInt)
             && codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
    // Have hardware divide instruction for int, do it with three instructions.
    call_kind = LocationSummary::kNoCall;
  }

  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(rem, call_kind);

  switch (type) {
    case Primitive::kPrimInt: {
      if (rem->InputAt(1)->IsConstant()) {
        locations->SetInAt(0, Location::RequiresRegister());
        locations->SetInAt(1, Location::ConstantLocation(rem->InputAt(1)->AsConstant()));
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
        int32_t value = rem->InputAt(1)->AsIntConstant()->GetValue();
        if (value == 1 || value == 0 || value == -1) {
          // No temp register required.
        } else {
          locations->AddTemp(Location::RequiresRegister());
          if (!IsPowerOfTwo(AbsOrMin(value))) {
            locations->AddTemp(Location::RequiresRegister());
          }
        }
      } else if (codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
        locations->SetInAt(0, Location::RequiresRegister());
        locations->SetInAt(1, Location::RequiresRegister());
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
        locations->AddTemp(Location::RequiresRegister());
      } else {
        InvokeRuntimeCallingConvention calling_convention;
        locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
        locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
        // Note: divmod will compute both the quotient and the remainder as the pair R0 and R1, but
        //       we only need the latter.
        locations->SetOut(Location::RegisterLocation(R1));
      }
      break;
    }
    case Primitive::kPrimLong: {
      InvokeRuntimeCallingConvention calling_convention;
      locations->SetInAt(0, Location::RegisterPairLocation(
          calling_convention.GetRegisterAt(0), calling_convention.GetRegisterAt(1)));
      locations->SetInAt(1, Location::RegisterPairLocation(
          calling_convention.GetRegisterAt(2), calling_convention.GetRegisterAt(3)));
      // The runtime helper puts the output in R2,R3.
      locations->SetOut(Location::RegisterPairLocation(R2, R3));
      break;
    }
    case Primitive::kPrimFloat: {
      InvokeRuntimeCallingConvention calling_convention;
      locations->SetInAt(0, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
      locations->SetInAt(1, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(1)));
      locations->SetOut(Location::FpuRegisterLocation(S0));
      break;
    }

    case Primitive::kPrimDouble: {
      InvokeRuntimeCallingConvention calling_convention;
      locations->SetInAt(0, Location::FpuRegisterPairLocation(
          calling_convention.GetFpuRegisterAt(0), calling_convention.GetFpuRegisterAt(1)));
      locations->SetInAt(1, Location::FpuRegisterPairLocation(
          calling_convention.GetFpuRegisterAt(2), calling_convention.GetFpuRegisterAt(3)));
      locations->SetOut(Location::Location::FpuRegisterPairLocation(S0, S1));
      break;
    }

    default:
      LOG(FATAL) << "Unexpected rem type " << type;
  }
}

void InstructionCodeGeneratorARM::VisitRem(HRem* rem) {
  LocationSummary* locations = rem->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  Primitive::Type type = rem->GetResultType();
  switch (type) {
    case Primitive::kPrimInt: {
        if (second.IsConstant()) {
          GenerateDivRemConstantIntegral(rem);
        } else if (codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
        Register reg1 = first.AsRegister<Register>();
        Register reg2 = second.AsRegister<Register>();
        Register temp = locations->GetTemp(0).AsRegister<Register>();

        // temp = reg1 / reg2  (integer division)
        // dest = reg1 - temp * reg2
        __ sdiv(temp, reg1, reg2);
        __ mls(out.AsRegister<Register>(), temp, reg2, reg1);
      } else {
        InvokeRuntimeCallingConvention calling_convention;
        DCHECK_EQ(calling_convention.GetRegisterAt(0), first.AsRegister<Register>());
        DCHECK_EQ(calling_convention.GetRegisterAt(1), second.AsRegister<Register>());
        DCHECK_EQ(R1, out.AsRegister<Register>());

        codegen_->InvokeRuntime(kQuickIdivmod, rem, rem->GetDexPc());
        CheckEntrypointTypes<kQuickIdivmod, int32_t, int32_t, int32_t>();
      }
      break;
    }

    case Primitive::kPrimLong: {
      codegen_->InvokeRuntime(kQuickLmod, rem, rem->GetDexPc());
        CheckEntrypointTypes<kQuickLmod, int64_t, int64_t, int64_t>();
      break;
    }

    case Primitive::kPrimFloat: {
      codegen_->InvokeRuntime(kQuickFmodf, rem, rem->GetDexPc());
      CheckEntrypointTypes<kQuickFmodf, float, float, float>();
      break;
    }

    case Primitive::kPrimDouble: {
      codegen_->InvokeRuntime(kQuickFmod, rem, rem->GetDexPc());
      CheckEntrypointTypes<kQuickFmod, double, double, double>();
      break;
    }

    default:
      LOG(FATAL) << "Unexpected rem type " << type;
  }
}

void LocationsBuilderARM::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  LocationSummary* locations = codegen_->CreateThrowingSlowPathLocations(instruction);
  locations->SetInAt(0, Location::RegisterOrConstant(instruction->InputAt(0)));
}

void InstructionCodeGeneratorARM::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) DivZeroCheckSlowPathARM(instruction);
  codegen_->AddSlowPath(slow_path);

  LocationSummary* locations = instruction->GetLocations();
  Location value = locations->InAt(0);

  switch (instruction->GetType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
    case Primitive::kPrimInt: {
      if (value.IsRegister()) {
        __ CompareAndBranchIfZero(value.AsRegister<Register>(), slow_path->GetEntryLabel());
      } else {
        DCHECK(value.IsConstant()) << value;
        if (value.GetConstant()->AsIntConstant()->GetValue() == 0) {
          __ b(slow_path->GetEntryLabel());
        }
      }
      break;
    }
    case Primitive::kPrimLong: {
      if (value.IsRegisterPair()) {
        __ orrs(IP,
                value.AsRegisterPairLow<Register>(),
                ShifterOperand(value.AsRegisterPairHigh<Register>()));
        __ b(slow_path->GetEntryLabel(), EQ);
      } else {
        DCHECK(value.IsConstant()) << value;
        if (value.GetConstant()->AsLongConstant()->GetValue() == 0) {
          __ b(slow_path->GetEntryLabel());
        }
      }
      break;
    default:
      LOG(FATAL) << "Unexpected type for HDivZeroCheck " << instruction->GetType();
    }
  }
}

void InstructionCodeGeneratorARM::HandleIntegerRotate(LocationSummary* locations) {
  Register in = locations->InAt(0).AsRegister<Register>();
  Location rhs = locations->InAt(1);
  Register out = locations->Out().AsRegister<Register>();

  if (rhs.IsConstant()) {
    // Arm32 and Thumb2 assemblers require a rotation on the interval [1,31],
    // so map all rotations to a +ve. equivalent in that range.
    // (e.g. left *or* right by -2 bits == 30 bits in the same direction.)
    uint32_t rot = CodeGenerator::GetInt32ValueOf(rhs.GetConstant()) & 0x1F;
    if (rot) {
      // Rotate, mapping left rotations to right equivalents if necessary.
      // (e.g. left by 2 bits == right by 30.)
      __ Ror(out, in, rot);
    } else if (out != in) {
      __ Mov(out, in);
    }
  } else {
    __ Ror(out, in, rhs.AsRegister<Register>());
  }
}

// Gain some speed by mapping all Long rotates onto equivalent pairs of Integer
// rotates by swapping input regs (effectively rotating by the first 32-bits of
// a larger rotation) or flipping direction (thus treating larger right/left
// rotations as sub-word sized rotations in the other direction) as appropriate.
void InstructionCodeGeneratorARM::HandleLongRotate(HRor* ror) {
  LocationSummary* locations = ror->GetLocations();
  Register in_reg_lo = locations->InAt(0).AsRegisterPairLow<Register>();
  Register in_reg_hi = locations->InAt(0).AsRegisterPairHigh<Register>();
  Location rhs = locations->InAt(1);
  Register out_reg_lo = locations->Out().AsRegisterPairLow<Register>();
  Register out_reg_hi = locations->Out().AsRegisterPairHigh<Register>();

  if (rhs.IsConstant()) {
    uint64_t rot = CodeGenerator::GetInt64ValueOf(rhs.GetConstant());
    // Map all rotations to +ve. equivalents on the interval [0,63].
    rot &= kMaxLongShiftDistance;
    // For rotates over a word in size, 'pre-rotate' by 32-bits to keep rotate
    // logic below to a simple pair of binary orr.
    // (e.g. 34 bits == in_reg swap + 2 bits right.)
    if (rot >= kArmBitsPerWord) {
      rot -= kArmBitsPerWord;
      std::swap(in_reg_hi, in_reg_lo);
    }
    // Rotate, or mov to out for zero or word size rotations.
    if (rot != 0u) {
      __ Lsr(out_reg_hi, in_reg_hi, rot);
      __ orr(out_reg_hi, out_reg_hi, ShifterOperand(in_reg_lo, arm::LSL, kArmBitsPerWord - rot));
      __ Lsr(out_reg_lo, in_reg_lo, rot);
      __ orr(out_reg_lo, out_reg_lo, ShifterOperand(in_reg_hi, arm::LSL, kArmBitsPerWord - rot));
    } else {
      __ Mov(out_reg_lo, in_reg_lo);
      __ Mov(out_reg_hi, in_reg_hi);
    }
  } else {
    Register shift_right = locations->GetTemp(0).AsRegister<Register>();
    Register shift_left = locations->GetTemp(1).AsRegister<Register>();
    Label end;
    Label shift_by_32_plus_shift_right;
    Label* final_label = codegen_->GetFinalLabel(ror, &end);

    __ and_(shift_right, rhs.AsRegister<Register>(), ShifterOperand(0x1F));
    __ Lsrs(shift_left, rhs.AsRegister<Register>(), 6);
    __ rsb(shift_left, shift_right, ShifterOperand(kArmBitsPerWord), AL, kCcKeep);
    __ b(&shift_by_32_plus_shift_right, CC);

    // out_reg_hi = (reg_hi << shift_left) | (reg_lo >> shift_right).
    // out_reg_lo = (reg_lo << shift_left) | (reg_hi >> shift_right).
    __ Lsl(out_reg_hi, in_reg_hi, shift_left);
    __ Lsr(out_reg_lo, in_reg_lo, shift_right);
    __ add(out_reg_hi, out_reg_hi, ShifterOperand(out_reg_lo));
    __ Lsl(out_reg_lo, in_reg_lo, shift_left);
    __ Lsr(shift_left, in_reg_hi, shift_right);
    __ add(out_reg_lo, out_reg_lo, ShifterOperand(shift_left));
    __ b(final_label);

    __ Bind(&shift_by_32_plus_shift_right);  // Shift by 32+shift_right.
    // out_reg_hi = (reg_hi >> shift_right) | (reg_lo << shift_left).
    // out_reg_lo = (reg_lo >> shift_right) | (reg_hi << shift_left).
    __ Lsr(out_reg_hi, in_reg_hi, shift_right);
    __ Lsl(out_reg_lo, in_reg_lo, shift_left);
    __ add(out_reg_hi, out_reg_hi, ShifterOperand(out_reg_lo));
    __ Lsr(out_reg_lo, in_reg_lo, shift_right);
    __ Lsl(shift_right, in_reg_hi, shift_left);
    __ add(out_reg_lo, out_reg_lo, ShifterOperand(shift_right));

    if (end.IsLinked()) {
      __ Bind(&end);
    }
  }
}

void LocationsBuilderARM::VisitRor(HRor* ror) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(ror, LocationSummary::kNoCall);
  switch (ror->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(ror->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }
    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      if (ror->InputAt(1)->IsConstant()) {
        locations->SetInAt(1, Location::ConstantLocation(ror->InputAt(1)->AsConstant()));
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
        locations->AddTemp(Location::RequiresRegister());
        locations->AddTemp(Location::RequiresRegister());
      }
      locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected operation type " << ror->GetResultType();
  }
}

void InstructionCodeGeneratorARM::VisitRor(HRor* ror) {
  LocationSummary* locations = ror->GetLocations();
  Primitive::Type type = ror->GetResultType();
  switch (type) {
    case Primitive::kPrimInt: {
      HandleIntegerRotate(locations);
      break;
    }
    case Primitive::kPrimLong: {
      HandleLongRotate(ror);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected operation type " << type;
      UNREACHABLE();
  }
}

void LocationsBuilderARM::HandleShift(HBinaryOperation* op) {
  DCHECK(op->IsShl() || op->IsShr() || op->IsUShr());

  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(op, LocationSummary::kNoCall);

  switch (op->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      if (op->InputAt(1)->IsConstant()) {
        locations->SetInAt(1, Location::ConstantLocation(op->InputAt(1)->AsConstant()));
        locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
        // Make the output overlap, as it will be used to hold the masked
        // second input.
        locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      }
      break;
    }
    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      if (op->InputAt(1)->IsConstant()) {
        locations->SetInAt(1, Location::ConstantLocation(op->InputAt(1)->AsConstant()));
        // For simplicity, use kOutputOverlap even though we only require that low registers
        // don't clash with high registers which the register allocator currently guarantees.
        locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
        locations->AddTemp(Location::RequiresRegister());
        locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected operation type " << op->GetResultType();
  }
}

void InstructionCodeGeneratorARM::HandleShift(HBinaryOperation* op) {
  DCHECK(op->IsShl() || op->IsShr() || op->IsUShr());

  LocationSummary* locations = op->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  Primitive::Type type = op->GetResultType();
  switch (type) {
    case Primitive::kPrimInt: {
      Register out_reg = out.AsRegister<Register>();
      Register first_reg = first.AsRegister<Register>();
      if (second.IsRegister()) {
        Register second_reg = second.AsRegister<Register>();
        // ARM doesn't mask the shift count so we need to do it ourselves.
        __ and_(out_reg, second_reg, ShifterOperand(kMaxIntShiftDistance));
        if (op->IsShl()) {
          __ Lsl(out_reg, first_reg, out_reg);
        } else if (op->IsShr()) {
          __ Asr(out_reg, first_reg, out_reg);
        } else {
          __ Lsr(out_reg, first_reg, out_reg);
        }
      } else {
        int32_t cst = second.GetConstant()->AsIntConstant()->GetValue();
        uint32_t shift_value = cst & kMaxIntShiftDistance;
        if (shift_value == 0) {  // ARM does not support shifting with 0 immediate.
          __ Mov(out_reg, first_reg);
        } else if (op->IsShl()) {
          __ Lsl(out_reg, first_reg, shift_value);
        } else if (op->IsShr()) {
          __ Asr(out_reg, first_reg, shift_value);
        } else {
          __ Lsr(out_reg, first_reg, shift_value);
        }
      }
      break;
    }
    case Primitive::kPrimLong: {
      Register o_h = out.AsRegisterPairHigh<Register>();
      Register o_l = out.AsRegisterPairLow<Register>();

      Register high = first.AsRegisterPairHigh<Register>();
      Register low = first.AsRegisterPairLow<Register>();

      if (second.IsRegister()) {
        Register temp = locations->GetTemp(0).AsRegister<Register>();

        Register second_reg = second.AsRegister<Register>();

        if (op->IsShl()) {
          __ and_(o_l, second_reg, ShifterOperand(kMaxLongShiftDistance));
          // Shift the high part
          __ Lsl(o_h, high, o_l);
          // Shift the low part and `or` what overflew on the high part
          __ rsb(temp, o_l, ShifterOperand(kArmBitsPerWord));
          __ Lsr(temp, low, temp);
          __ orr(o_h, o_h, ShifterOperand(temp));
          // If the shift is > 32 bits, override the high part
          __ subs(temp, o_l, ShifterOperand(kArmBitsPerWord));
          __ it(PL);
          __ Lsl(o_h, low, temp, PL);
          // Shift the low part
          __ Lsl(o_l, low, o_l);
        } else if (op->IsShr()) {
          __ and_(o_h, second_reg, ShifterOperand(kMaxLongShiftDistance));
          // Shift the low part
          __ Lsr(o_l, low, o_h);
          // Shift the high part and `or` what underflew on the low part
          __ rsb(temp, o_h, ShifterOperand(kArmBitsPerWord));
          __ Lsl(temp, high, temp);
          __ orr(o_l, o_l, ShifterOperand(temp));
          // If the shift is > 32 bits, override the low part
          __ subs(temp, o_h, ShifterOperand(kArmBitsPerWord));
          __ it(PL);
          __ Asr(o_l, high, temp, PL);
          // Shift the high part
          __ Asr(o_h, high, o_h);
        } else {
          __ and_(o_h, second_reg, ShifterOperand(kMaxLongShiftDistance));
          // same as Shr except we use `Lsr`s and not `Asr`s
          __ Lsr(o_l, low, o_h);
          __ rsb(temp, o_h, ShifterOperand(kArmBitsPerWord));
          __ Lsl(temp, high, temp);
          __ orr(o_l, o_l, ShifterOperand(temp));
          __ subs(temp, o_h, ShifterOperand(kArmBitsPerWord));
          __ it(PL);
          __ Lsr(o_l, high, temp, PL);
          __ Lsr(o_h, high, o_h);
        }
      } else {
        // Register allocator doesn't create partial overlap.
        DCHECK_NE(o_l, high);
        DCHECK_NE(o_h, low);
        int32_t cst = second.GetConstant()->AsIntConstant()->GetValue();
        uint32_t shift_value = cst & kMaxLongShiftDistance;
        if (shift_value > 32) {
          if (op->IsShl()) {
            __ Lsl(o_h, low, shift_value - 32);
            __ LoadImmediate(o_l, 0);
          } else if (op->IsShr()) {
            __ Asr(o_l, high, shift_value - 32);
            __ Asr(o_h, high, 31);
          } else {
            __ Lsr(o_l, high, shift_value - 32);
            __ LoadImmediate(o_h, 0);
          }
        } else if (shift_value == 32) {
          if (op->IsShl()) {
            __ mov(o_h, ShifterOperand(low));
            __ LoadImmediate(o_l, 0);
          } else if (op->IsShr()) {
            __ mov(o_l, ShifterOperand(high));
            __ Asr(o_h, high, 31);
          } else {
            __ mov(o_l, ShifterOperand(high));
            __ LoadImmediate(o_h, 0);
          }
        } else if (shift_value == 1) {
          if (op->IsShl()) {
            __ Lsls(o_l, low, 1);
            __ adc(o_h, high, ShifterOperand(high));
          } else if (op->IsShr()) {
            __ Asrs(o_h, high, 1);
            __ Rrx(o_l, low);
          } else {
            __ Lsrs(o_h, high, 1);
            __ Rrx(o_l, low);
          }
        } else {
          DCHECK(2 <= shift_value && shift_value < 32) << shift_value;
          if (op->IsShl()) {
            __ Lsl(o_h, high, shift_value);
            __ orr(o_h, o_h, ShifterOperand(low, LSR, 32 - shift_value));
            __ Lsl(o_l, low, shift_value);
          } else if (op->IsShr()) {
            __ Lsr(o_l, low, shift_value);
            __ orr(o_l, o_l, ShifterOperand(high, LSL, 32 - shift_value));
            __ Asr(o_h, high, shift_value);
          } else {
            __ Lsr(o_l, low, shift_value);
            __ orr(o_l, o_l, ShifterOperand(high, LSL, 32 - shift_value));
            __ Lsr(o_h, high, shift_value);
          }
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected operation type " << type;
      UNREACHABLE();
  }
}

void LocationsBuilderARM::VisitShl(HShl* shl) {
  HandleShift(shl);
}

void InstructionCodeGeneratorARM::VisitShl(HShl* shl) {
  HandleShift(shl);
}

void LocationsBuilderARM::VisitShr(HShr* shr) {
  HandleShift(shr);
}

void InstructionCodeGeneratorARM::VisitShr(HShr* shr) {
  HandleShift(shr);
}

void LocationsBuilderARM::VisitUShr(HUShr* ushr) {
  HandleShift(ushr);
}

void InstructionCodeGeneratorARM::VisitUShr(HUShr* ushr) {
  HandleShift(ushr);
}

void LocationsBuilderARM::VisitNewInstance(HNewInstance* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  if (instruction->IsStringAlloc()) {
    locations->AddTemp(Location::RegisterLocation(kMethodRegisterArgument));
  } else {
    InvokeRuntimeCallingConvention calling_convention;
    locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  }
  locations->SetOut(Location::RegisterLocation(R0));
}

void InstructionCodeGeneratorARM::VisitNewInstance(HNewInstance* instruction) {
  // Note: if heap poisoning is enabled, the entry point takes cares
  // of poisoning the reference.
  if (instruction->IsStringAlloc()) {
    // String is allocated through StringFactory. Call NewEmptyString entry point.
    Register temp = instruction->GetLocations()->GetTemp(0).AsRegister<Register>();
    MemberOffset code_offset = ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArmPointerSize);
    __ LoadFromOffset(kLoadWord, temp, TR, QUICK_ENTRY_POINT(pNewEmptyString));
    __ LoadFromOffset(kLoadWord, LR, temp, code_offset.Int32Value());
    __ blx(LR);
    codegen_->RecordPcInfo(instruction, instruction->GetDexPc());
  } else {
    codegen_->InvokeRuntime(instruction->GetEntrypoint(), instruction, instruction->GetDexPc());
    CheckEntrypointTypes<kQuickAllocObjectWithChecks, void*, mirror::Class*>();
  }
}

void LocationsBuilderARM::VisitNewArray(HNewArray* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetOut(Location::RegisterLocation(R0));
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
}

void InstructionCodeGeneratorARM::VisitNewArray(HNewArray* instruction) {
  // Note: if heap poisoning is enabled, the entry point takes cares
  // of poisoning the reference.
  QuickEntrypointEnum entrypoint =
      CodeGenerator::GetArrayAllocationEntrypoint(instruction->GetLoadClass()->GetClass());
  codegen_->InvokeRuntime(entrypoint, instruction, instruction->GetDexPc());
  CheckEntrypointTypes<kQuickAllocArrayResolved, void*, mirror::Class*, int32_t>();
  DCHECK(!codegen_->IsLeafMethod());
}

void LocationsBuilderARM::VisitParameterValue(HParameterValue* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  Location location = parameter_visitor_.GetNextLocation(instruction->GetType());
  if (location.IsStackSlot()) {
    location = Location::StackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  } else if (location.IsDoubleStackSlot()) {
    location = Location::DoubleStackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  }
  locations->SetOut(location);
}

void InstructionCodeGeneratorARM::VisitParameterValue(
    HParameterValue* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, the parameter is already at its location.
}

void LocationsBuilderARM::VisitCurrentMethod(HCurrentMethod* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::RegisterLocation(kMethodRegisterArgument));
}

void InstructionCodeGeneratorARM::VisitCurrentMethod(HCurrentMethod* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, the method is already at its location.
}

void LocationsBuilderARM::VisitNot(HNot* not_) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(not_, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitNot(HNot* not_) {
  LocationSummary* locations = not_->GetLocations();
  Location out = locations->Out();
  Location in = locations->InAt(0);
  switch (not_->GetResultType()) {
    case Primitive::kPrimInt:
      __ mvn(out.AsRegister<Register>(), ShifterOperand(in.AsRegister<Register>()));
      break;

    case Primitive::kPrimLong:
      __ mvn(out.AsRegisterPairLow<Register>(),
             ShifterOperand(in.AsRegisterPairLow<Register>()));
      __ mvn(out.AsRegisterPairHigh<Register>(),
             ShifterOperand(in.AsRegisterPairHigh<Register>()));
      break;

    default:
      LOG(FATAL) << "Unimplemented type for not operation " << not_->GetResultType();
  }
}

void LocationsBuilderARM::VisitBooleanNot(HBooleanNot* bool_not) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(bool_not, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitBooleanNot(HBooleanNot* bool_not) {
  LocationSummary* locations = bool_not->GetLocations();
  Location out = locations->Out();
  Location in = locations->InAt(0);
  __ eor(out.AsRegister<Register>(), in.AsRegister<Register>(), ShifterOperand(1));
}

void LocationsBuilderARM::VisitCompare(HCompare* compare) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(compare, LocationSummary::kNoCall);
  switch (compare->InputAt(0)->GetType()) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
    case Primitive::kPrimInt:
    case Primitive::kPrimLong: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RequiresRegister());
      // Output overlaps because it is written before doing the low comparison.
      locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      break;
    }
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, ArithmeticZeroOrFpuRegister(compare->InputAt(1)));
      locations->SetOut(Location::RequiresRegister());
      break;
    }
    default:
      LOG(FATAL) << "Unexpected type for compare operation " << compare->InputAt(0)->GetType();
  }
}

void InstructionCodeGeneratorARM::VisitCompare(HCompare* compare) {
  LocationSummary* locations = compare->GetLocations();
  Register out = locations->Out().AsRegister<Register>();
  Location left = locations->InAt(0);
  Location right = locations->InAt(1);

  Label less, greater, done;
  Label* final_label = codegen_->GetFinalLabel(compare, &done);
  Primitive::Type type = compare->InputAt(0)->GetType();
  Condition less_cond;
  switch (type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
    case Primitive::kPrimInt: {
      __ LoadImmediate(out, 0);
      __ cmp(left.AsRegister<Register>(),
             ShifterOperand(right.AsRegister<Register>()));  // Signed compare.
      less_cond = LT;
      break;
    }
    case Primitive::kPrimLong: {
      __ cmp(left.AsRegisterPairHigh<Register>(),
             ShifterOperand(right.AsRegisterPairHigh<Register>()));  // Signed compare.
      __ b(&less, LT);
      __ b(&greater, GT);
      // Do LoadImmediate before the last `cmp`, as LoadImmediate might affect the status flags.
      __ LoadImmediate(out, 0);
      __ cmp(left.AsRegisterPairLow<Register>(),
             ShifterOperand(right.AsRegisterPairLow<Register>()));  // Unsigned compare.
      less_cond = LO;
      break;
    }
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      __ LoadImmediate(out, 0);
      GenerateVcmp(compare, codegen_);
      __ vmstat();  // transfer FP status register to ARM APSR.
      less_cond = ARMFPCondition(kCondLT, compare->IsGtBias());
      break;
    }
    default:
      LOG(FATAL) << "Unexpected compare type " << type;
      UNREACHABLE();
  }

  __ b(final_label, EQ);
  __ b(&less, less_cond);

  __ Bind(&greater);
  __ LoadImmediate(out, 1);
  __ b(final_label);

  __ Bind(&less);
  __ LoadImmediate(out, -1);

  if (done.IsLinked()) {
    __ Bind(&done);
  }
}

void LocationsBuilderARM::VisitPhi(HPhi* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  for (size_t i = 0, e = locations->GetInputCount(); i < e; ++i) {
    locations->SetInAt(i, Location::Any());
  }
  locations->SetOut(Location::Any());
}

void InstructionCodeGeneratorARM::VisitPhi(HPhi* instruction ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unreachable";
}

void CodeGeneratorARM::GenerateMemoryBarrier(MemBarrierKind kind) {
  // TODO (ported from quick): revisit ARM barrier kinds.
  DmbOptions flavor = DmbOptions::ISH;  // Quiet C++ warnings.
  switch (kind) {
    case MemBarrierKind::kAnyStore:
    case MemBarrierKind::kLoadAny:
    case MemBarrierKind::kAnyAny: {
      flavor = DmbOptions::ISH;
      break;
    }
    case MemBarrierKind::kStoreStore: {
      flavor = DmbOptions::ISHST;
      break;
    }
    default:
      LOG(FATAL) << "Unexpected memory barrier " << kind;
  }
  __ dmb(flavor);
}

void InstructionCodeGeneratorARM::GenerateWideAtomicLoad(Register addr,
                                                         uint32_t offset,
                                                         Register out_lo,
                                                         Register out_hi) {
  if (offset != 0) {
    // Ensure `out_lo` is different from `addr`, so that loading
    // `offset` into `out_lo` does not clutter `addr`.
    DCHECK_NE(out_lo, addr);
    __ LoadImmediate(out_lo, offset);
    __ add(IP, addr, ShifterOperand(out_lo));
    addr = IP;
  }
  __ ldrexd(out_lo, out_hi, addr);
}

void InstructionCodeGeneratorARM::GenerateWideAtomicStore(Register addr,
                                                          uint32_t offset,
                                                          Register value_lo,
                                                          Register value_hi,
                                                          Register temp1,
                                                          Register temp2,
                                                          HInstruction* instruction) {
  Label fail;
  if (offset != 0) {
    __ LoadImmediate(temp1, offset);
    __ add(IP, addr, ShifterOperand(temp1));
    addr = IP;
  }
  __ Bind(&fail);
  // We need a load followed by store. (The address used in a STREX instruction must
  // be the same as the address in the most recently executed LDREX instruction.)
  __ ldrexd(temp1, temp2, addr);
  codegen_->MaybeRecordImplicitNullCheck(instruction);
  __ strexd(temp1, value_lo, value_hi, addr);
  __ CompareAndBranchIfNonZero(temp1, &fail);
}

void LocationsBuilderARM::HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info) {
  DCHECK(instruction->IsInstanceFieldSet() || instruction->IsStaticFieldSet());

  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());

  Primitive::Type field_type = field_info.GetFieldType();
  if (Primitive::IsFloatingPointType(field_type)) {
    locations->SetInAt(1, Location::RequiresFpuRegister());
  } else {
    locations->SetInAt(1, Location::RequiresRegister());
  }

  bool is_wide = field_type == Primitive::kPrimLong || field_type == Primitive::kPrimDouble;
  bool generate_volatile = field_info.IsVolatile()
      && is_wide
      && !codegen_->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
  bool needs_write_barrier =
      CodeGenerator::StoreNeedsWriteBarrier(field_type, instruction->InputAt(1));
  // Temporary registers for the write barrier.
  // TODO: consider renaming StoreNeedsWriteBarrier to StoreNeedsGCMark.
  if (needs_write_barrier) {
    locations->AddTemp(Location::RequiresRegister());  // Possibly used for reference poisoning too.
    locations->AddTemp(Location::RequiresRegister());
  } else if (generate_volatile) {
    // ARM encoding have some additional constraints for ldrexd/strexd:
    // - registers need to be consecutive
    // - the first register should be even but not R14.
    // We don't test for ARM yet, and the assertion makes sure that we
    // revisit this if we ever enable ARM encoding.
    DCHECK_EQ(InstructionSet::kThumb2, codegen_->GetInstructionSet());

    locations->AddTemp(Location::RequiresRegister());
    locations->AddTemp(Location::RequiresRegister());
    if (field_type == Primitive::kPrimDouble) {
      // For doubles we need two more registers to copy the value.
      locations->AddTemp(Location::RegisterLocation(R2));
      locations->AddTemp(Location::RegisterLocation(R3));
    }
  }
}

void InstructionCodeGeneratorARM::HandleFieldSet(HInstruction* instruction,
                                                 const FieldInfo& field_info,
                                                 bool value_can_be_null) {
  DCHECK(instruction->IsInstanceFieldSet() || instruction->IsStaticFieldSet());

  LocationSummary* locations = instruction->GetLocations();
  Register base = locations->InAt(0).AsRegister<Register>();
  Location value = locations->InAt(1);

  bool is_volatile = field_info.IsVolatile();
  bool atomic_ldrd_strd = codegen_->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
  Primitive::Type field_type = field_info.GetFieldType();
  uint32_t offset = field_info.GetFieldOffset().Uint32Value();
  bool needs_write_barrier =
      CodeGenerator::StoreNeedsWriteBarrier(field_type, instruction->InputAt(1));

  if (is_volatile) {
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kAnyStore);
  }

  switch (field_type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte: {
      __ StoreToOffset(kStoreByte, value.AsRegister<Register>(), base, offset);
      break;
    }

    case Primitive::kPrimShort:
    case Primitive::kPrimChar: {
      __ StoreToOffset(kStoreHalfword, value.AsRegister<Register>(), base, offset);
      break;
    }

    case Primitive::kPrimInt:
    case Primitive::kPrimNot: {
      if (kPoisonHeapReferences && needs_write_barrier) {
        // Note that in the case where `value` is a null reference,
        // we do not enter this block, as a null reference does not
        // need poisoning.
        DCHECK_EQ(field_type, Primitive::kPrimNot);
        Register temp = locations->GetTemp(0).AsRegister<Register>();
        __ Mov(temp, value.AsRegister<Register>());
        __ PoisonHeapReference(temp);
        __ StoreToOffset(kStoreWord, temp, base, offset);
      } else {
        __ StoreToOffset(kStoreWord, value.AsRegister<Register>(), base, offset);
      }
      break;
    }

    case Primitive::kPrimLong: {
      if (is_volatile && !atomic_ldrd_strd) {
        GenerateWideAtomicStore(base, offset,
                                value.AsRegisterPairLow<Register>(),
                                value.AsRegisterPairHigh<Register>(),
                                locations->GetTemp(0).AsRegister<Register>(),
                                locations->GetTemp(1).AsRegister<Register>(),
                                instruction);
      } else {
        __ StoreToOffset(kStoreWordPair, value.AsRegisterPairLow<Register>(), base, offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }
      break;
    }

    case Primitive::kPrimFloat: {
      __ StoreSToOffset(value.AsFpuRegister<SRegister>(), base, offset);
      break;
    }

    case Primitive::kPrimDouble: {
      DRegister value_reg = FromLowSToD(value.AsFpuRegisterPairLow<SRegister>());
      if (is_volatile && !atomic_ldrd_strd) {
        Register value_reg_lo = locations->GetTemp(0).AsRegister<Register>();
        Register value_reg_hi = locations->GetTemp(1).AsRegister<Register>();

        __ vmovrrd(value_reg_lo, value_reg_hi, value_reg);

        GenerateWideAtomicStore(base, offset,
                                value_reg_lo,
                                value_reg_hi,
                                locations->GetTemp(2).AsRegister<Register>(),
                                locations->GetTemp(3).AsRegister<Register>(),
                                instruction);
      } else {
        __ StoreDToOffset(value_reg, base, offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }
      break;
    }

    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unreachable type " << field_type;
      UNREACHABLE();
  }

  // Longs and doubles are handled in the switch.
  if (field_type != Primitive::kPrimLong && field_type != Primitive::kPrimDouble) {
    codegen_->MaybeRecordImplicitNullCheck(instruction);
  }

  if (CodeGenerator::StoreNeedsWriteBarrier(field_type, instruction->InputAt(1))) {
    Register temp = locations->GetTemp(0).AsRegister<Register>();
    Register card = locations->GetTemp(1).AsRegister<Register>();
    codegen_->MarkGCCard(
        temp, card, base, value.AsRegister<Register>(), value_can_be_null);
  }

  if (is_volatile) {
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }
}

void LocationsBuilderARM::HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info) {
  DCHECK(instruction->IsInstanceFieldGet() || instruction->IsStaticFieldGet());

  bool object_field_get_with_read_barrier =
      kEmitCompilerReadBarrier && (field_info.GetFieldType() == Primitive::kPrimNot);
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction,
                                                   object_field_get_with_read_barrier ?
                                                       LocationSummary::kCallOnSlowPath :
                                                       LocationSummary::kNoCall);
  if (object_field_get_with_read_barrier && kUseBakerReadBarrier) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::RequiresRegister());

  bool volatile_for_double = field_info.IsVolatile()
      && (field_info.GetFieldType() == Primitive::kPrimDouble)
      && !codegen_->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
  // The output overlaps in case of volatile long: we don't want the
  // code generated by GenerateWideAtomicLoad to overwrite the
  // object's location.  Likewise, in the case of an object field get
  // with read barriers enabled, we do not want the load to overwrite
  // the object's location, as we need it to emit the read barrier.
  bool overlap = (field_info.IsVolatile() && (field_info.GetFieldType() == Primitive::kPrimLong)) ||
      object_field_get_with_read_barrier;

  if (Primitive::IsFloatingPointType(instruction->GetType())) {
    locations->SetOut(Location::RequiresFpuRegister());
  } else {
    locations->SetOut(Location::RequiresRegister(),
                      (overlap ? Location::kOutputOverlap : Location::kNoOutputOverlap));
  }
  if (volatile_for_double) {
    // ARM encoding have some additional constraints for ldrexd/strexd:
    // - registers need to be consecutive
    // - the first register should be even but not R14.
    // We don't test for ARM yet, and the assertion makes sure that we
    // revisit this if we ever enable ARM encoding.
    DCHECK_EQ(InstructionSet::kThumb2, codegen_->GetInstructionSet());
    locations->AddTemp(Location::RequiresRegister());
    locations->AddTemp(Location::RequiresRegister());
  } else if (object_field_get_with_read_barrier && kUseBakerReadBarrier) {
    // We need a temporary register for the read barrier marking slow
    // path in CodeGeneratorARM::GenerateFieldLoadWithBakerReadBarrier.
    locations->AddTemp(Location::RequiresRegister());
  }
}

Location LocationsBuilderARM::ArithmeticZeroOrFpuRegister(HInstruction* input) {
  DCHECK(input->GetType() == Primitive::kPrimDouble || input->GetType() == Primitive::kPrimFloat)
      << input->GetType();
  if ((input->IsFloatConstant() && (input->AsFloatConstant()->IsArithmeticZero())) ||
      (input->IsDoubleConstant() && (input->AsDoubleConstant()->IsArithmeticZero()))) {
    return Location::ConstantLocation(input->AsConstant());
  } else {
    return Location::RequiresFpuRegister();
  }
}

Location LocationsBuilderARM::ArmEncodableConstantOrRegister(HInstruction* constant,
                                                             Opcode opcode) {
  DCHECK(!Primitive::IsFloatingPointType(constant->GetType()));
  if (constant->IsConstant() &&
      CanEncodeConstantAsImmediate(constant->AsConstant(), opcode)) {
    return Location::ConstantLocation(constant->AsConstant());
  }
  return Location::RequiresRegister();
}

bool LocationsBuilderARM::CanEncodeConstantAsImmediate(HConstant* input_cst,
                                                       Opcode opcode) {
  uint64_t value = static_cast<uint64_t>(Int64FromConstant(input_cst));
  if (Primitive::Is64BitType(input_cst->GetType())) {
    Opcode high_opcode = opcode;
    SetCc low_set_cc = kCcDontCare;
    switch (opcode) {
      case SUB:
        // Flip the operation to an ADD.
        value = -value;
        opcode = ADD;
        FALLTHROUGH_INTENDED;
      case ADD:
        if (Low32Bits(value) == 0u) {
          return CanEncodeConstantAsImmediate(High32Bits(value), opcode, kCcDontCare);
        }
        high_opcode = ADC;
        low_set_cc = kCcSet;
        break;
      default:
        break;
    }
    return CanEncodeConstantAsImmediate(Low32Bits(value), opcode, low_set_cc) &&
        CanEncodeConstantAsImmediate(High32Bits(value), high_opcode, kCcDontCare);
  } else {
    return CanEncodeConstantAsImmediate(Low32Bits(value), opcode);
  }
}

bool LocationsBuilderARM::CanEncodeConstantAsImmediate(uint32_t value,
                                                       Opcode opcode,
                                                       SetCc set_cc) {
  ShifterOperand so;
  ArmAssembler* assembler = codegen_->GetAssembler();
  if (assembler->ShifterOperandCanHold(kNoRegister, kNoRegister, opcode, value, set_cc, &so)) {
    return true;
  }
  Opcode neg_opcode = kNoOperand;
  uint32_t neg_value = 0;
  switch (opcode) {
    case AND: neg_opcode = BIC; neg_value = ~value; break;
    case ORR: neg_opcode = ORN; neg_value = ~value; break;
    case ADD: neg_opcode = SUB; neg_value = -value; break;
    case ADC: neg_opcode = SBC; neg_value = ~value; break;
    case SUB: neg_opcode = ADD; neg_value = -value; break;
    case SBC: neg_opcode = ADC; neg_value = ~value; break;
    case MOV: neg_opcode = MVN; neg_value = ~value; break;
    default:
      return false;
  }

  if (assembler->ShifterOperandCanHold(kNoRegister,
                                       kNoRegister,
                                       neg_opcode,
                                       neg_value,
                                       set_cc,
                                       &so)) {
    return true;
  }

  return opcode == AND && IsPowerOfTwo(value + 1);
}

void InstructionCodeGeneratorARM::HandleFieldGet(HInstruction* instruction,
                                                 const FieldInfo& field_info) {
  DCHECK(instruction->IsInstanceFieldGet() || instruction->IsStaticFieldGet());

  LocationSummary* locations = instruction->GetLocations();
  Location base_loc = locations->InAt(0);
  Register base = base_loc.AsRegister<Register>();
  Location out = locations->Out();
  bool is_volatile = field_info.IsVolatile();
  bool atomic_ldrd_strd = codegen_->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
  Primitive::Type field_type = field_info.GetFieldType();
  uint32_t offset = field_info.GetFieldOffset().Uint32Value();

  switch (field_type) {
    case Primitive::kPrimBoolean:
      __ LoadFromOffset(kLoadUnsignedByte, out.AsRegister<Register>(), base, offset);
      break;

    case Primitive::kPrimByte:
      __ LoadFromOffset(kLoadSignedByte, out.AsRegister<Register>(), base, offset);
      break;

    case Primitive::kPrimShort:
      __ LoadFromOffset(kLoadSignedHalfword, out.AsRegister<Register>(), base, offset);
      break;

    case Primitive::kPrimChar:
      __ LoadFromOffset(kLoadUnsignedHalfword, out.AsRegister<Register>(), base, offset);
      break;

    case Primitive::kPrimInt:
      __ LoadFromOffset(kLoadWord, out.AsRegister<Register>(), base, offset);
      break;

    case Primitive::kPrimNot: {
      // /* HeapReference<Object> */ out = *(base + offset)
      if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
        Location temp_loc = locations->GetTemp(0);
        // Note that a potential implicit null check is handled in this
        // CodeGeneratorARM::GenerateFieldLoadWithBakerReadBarrier call.
        codegen_->GenerateFieldLoadWithBakerReadBarrier(
            instruction, out, base, offset, temp_loc, /* needs_null_check */ true);
        if (is_volatile) {
          codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
        }
      } else {
        __ LoadFromOffset(kLoadWord, out.AsRegister<Register>(), base, offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        if (is_volatile) {
          codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
        }
        // If read barriers are enabled, emit read barriers other than
        // Baker's using a slow path (and also unpoison the loaded
        // reference, if heap poisoning is enabled).
        codegen_->MaybeGenerateReadBarrierSlow(instruction, out, out, base_loc, offset);
      }
      break;
    }

    case Primitive::kPrimLong:
      if (is_volatile && !atomic_ldrd_strd) {
        GenerateWideAtomicLoad(base, offset,
                               out.AsRegisterPairLow<Register>(),
                               out.AsRegisterPairHigh<Register>());
      } else {
        __ LoadFromOffset(kLoadWordPair, out.AsRegisterPairLow<Register>(), base, offset);
      }
      break;

    case Primitive::kPrimFloat:
      __ LoadSFromOffset(out.AsFpuRegister<SRegister>(), base, offset);
      break;

    case Primitive::kPrimDouble: {
      DRegister out_reg = FromLowSToD(out.AsFpuRegisterPairLow<SRegister>());
      if (is_volatile && !atomic_ldrd_strd) {
        Register lo = locations->GetTemp(0).AsRegister<Register>();
        Register hi = locations->GetTemp(1).AsRegister<Register>();
        GenerateWideAtomicLoad(base, offset, lo, hi);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        __ vmovdrr(out_reg, lo, hi);
      } else {
        __ LoadDFromOffset(out_reg, base, offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }
      break;
    }

    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unreachable type " << field_type;
      UNREACHABLE();
  }

  if (field_type == Primitive::kPrimNot || field_type == Primitive::kPrimDouble) {
    // Potential implicit null checks, in the case of reference or
    // double fields, are handled in the previous switch statement.
  } else {
    codegen_->MaybeRecordImplicitNullCheck(instruction);
  }

  if (is_volatile) {
    if (field_type == Primitive::kPrimNot) {
      // Memory barriers, in the case of references, are also handled
      // in the previous switch statement.
    } else {
      codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
    }
  }
}

void LocationsBuilderARM::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARM::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo(), instruction->GetValueCanBeNull());
}

void LocationsBuilderARM::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARM::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void LocationsBuilderARM::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARM::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void LocationsBuilderARM::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARM::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo(), instruction->GetValueCanBeNull());
}

void LocationsBuilderARM::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->CreateUnresolvedFieldLocationSummary(
      instruction, instruction->GetFieldType(), calling_convention);
}

void InstructionCodeGeneratorARM::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->GenerateUnresolvedFieldAccess(instruction,
                                          instruction->GetFieldType(),
                                          instruction->GetFieldIndex(),
                                          instruction->GetDexPc(),
                                          calling_convention);
}

void LocationsBuilderARM::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->CreateUnresolvedFieldLocationSummary(
      instruction, instruction->GetFieldType(), calling_convention);
}

void InstructionCodeGeneratorARM::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->GenerateUnresolvedFieldAccess(instruction,
                                          instruction->GetFieldType(),
                                          instruction->GetFieldIndex(),
                                          instruction->GetDexPc(),
                                          calling_convention);
}

void LocationsBuilderARM::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->CreateUnresolvedFieldLocationSummary(
      instruction, instruction->GetFieldType(), calling_convention);
}

void InstructionCodeGeneratorARM::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->GenerateUnresolvedFieldAccess(instruction,
                                          instruction->GetFieldType(),
                                          instruction->GetFieldIndex(),
                                          instruction->GetDexPc(),
                                          calling_convention);
}

void LocationsBuilderARM::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->CreateUnresolvedFieldLocationSummary(
      instruction, instruction->GetFieldType(), calling_convention);
}

void InstructionCodeGeneratorARM::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  FieldAccessCallingConventionARM calling_convention;
  codegen_->GenerateUnresolvedFieldAccess(instruction,
                                          instruction->GetFieldType(),
                                          instruction->GetFieldIndex(),
                                          instruction->GetDexPc(),
                                          calling_convention);
}

void LocationsBuilderARM::VisitNullCheck(HNullCheck* instruction) {
  LocationSummary* locations = codegen_->CreateThrowingSlowPathLocations(instruction);
  locations->SetInAt(0, Location::RequiresRegister());
}

void CodeGeneratorARM::GenerateImplicitNullCheck(HNullCheck* instruction) {
  if (CanMoveNullCheckToUser(instruction)) {
    return;
  }
  Location obj = instruction->GetLocations()->InAt(0);

  __ LoadFromOffset(kLoadWord, IP, obj.AsRegister<Register>(), 0);
  RecordPcInfo(instruction, instruction->GetDexPc());
}

void CodeGeneratorARM::GenerateExplicitNullCheck(HNullCheck* instruction) {
  SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) NullCheckSlowPathARM(instruction);
  AddSlowPath(slow_path);

  LocationSummary* locations = instruction->GetLocations();
  Location obj = locations->InAt(0);

  __ CompareAndBranchIfZero(obj.AsRegister<Register>(), slow_path->GetEntryLabel());
}

void InstructionCodeGeneratorARM::VisitNullCheck(HNullCheck* instruction) {
  codegen_->GenerateNullCheck(instruction);
}

static LoadOperandType GetLoadOperandType(Primitive::Type type) {
  switch (type) {
    case Primitive::kPrimNot:
      return kLoadWord;
    case Primitive::kPrimBoolean:
      return kLoadUnsignedByte;
    case Primitive::kPrimByte:
      return kLoadSignedByte;
    case Primitive::kPrimChar:
      return kLoadUnsignedHalfword;
    case Primitive::kPrimShort:
      return kLoadSignedHalfword;
    case Primitive::kPrimInt:
      return kLoadWord;
    case Primitive::kPrimLong:
      return kLoadWordPair;
    case Primitive::kPrimFloat:
      return kLoadSWord;
    case Primitive::kPrimDouble:
      return kLoadDWord;
    default:
      LOG(FATAL) << "Unreachable type " << type;
      UNREACHABLE();
  }
}

static StoreOperandType GetStoreOperandType(Primitive::Type type) {
  switch (type) {
    case Primitive::kPrimNot:
      return kStoreWord;
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      return kStoreByte;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      return kStoreHalfword;
    case Primitive::kPrimInt:
      return kStoreWord;
    case Primitive::kPrimLong:
      return kStoreWordPair;
    case Primitive::kPrimFloat:
      return kStoreSWord;
    case Primitive::kPrimDouble:
      return kStoreDWord;
    default:
      LOG(FATAL) << "Unreachable type " << type;
      UNREACHABLE();
  }
}

void CodeGeneratorARM::LoadFromShiftedRegOffset(Primitive::Type type,
                                                Location out_loc,
                                                Register base,
                                                Register reg_offset,
                                                Condition cond) {
  uint32_t shift_count = Primitive::ComponentSizeShift(type);
  Address mem_address(base, reg_offset, Shift::LSL, shift_count);

  switch (type) {
    case Primitive::kPrimByte:
      __ ldrsb(out_loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimBoolean:
      __ ldrb(out_loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimShort:
      __ ldrsh(out_loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimChar:
      __ ldrh(out_loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimNot:
    case Primitive::kPrimInt:
      __ ldr(out_loc.AsRegister<Register>(), mem_address, cond);
      break;
    // T32 doesn't support LoadFromShiftedRegOffset mem address mode for these types.
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
    default:
      LOG(FATAL) << "Unreachable type " << type;
      UNREACHABLE();
  }
}

void CodeGeneratorARM::StoreToShiftedRegOffset(Primitive::Type type,
                                               Location loc,
                                               Register base,
                                               Register reg_offset,
                                               Condition cond) {
  uint32_t shift_count = Primitive::ComponentSizeShift(type);
  Address mem_address(base, reg_offset, Shift::LSL, shift_count);

  switch (type) {
    case Primitive::kPrimByte:
    case Primitive::kPrimBoolean:
      __ strb(loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
      __ strh(loc.AsRegister<Register>(), mem_address, cond);
      break;
    case Primitive::kPrimNot:
    case Primitive::kPrimInt:
      __ str(loc.AsRegister<Register>(), mem_address, cond);
      break;
    // T32 doesn't support StoreToShiftedRegOffset mem address mode for these types.
    case Primitive::kPrimLong:
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
    default:
      LOG(FATAL) << "Unreachable type " << type;
      UNREACHABLE();
  }
}

void LocationsBuilderARM::VisitArrayGet(HArrayGet* instruction) {
  bool object_array_get_with_read_barrier =
      kEmitCompilerReadBarrier && (instruction->GetType() == Primitive::kPrimNot);
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction,
                                                   object_array_get_with_read_barrier ?
                                                       LocationSummary::kCallOnSlowPath :
                                                       LocationSummary::kNoCall);
  if (object_array_get_with_read_barrier && kUseBakerReadBarrier) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
  if (Primitive::IsFloatingPointType(instruction->GetType())) {
    locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
  } else {
    // The output overlaps in the case of an object array get with
    // read barriers enabled: we do not want the move to overwrite the
    // array's location, as we need it to emit the read barrier.
    locations->SetOut(
        Location::RequiresRegister(),
        object_array_get_with_read_barrier ? Location::kOutputOverlap : Location::kNoOutputOverlap);
  }
  // We need a temporary register for the read barrier marking slow
  // path in CodeGeneratorARM::GenerateArrayLoadWithBakerReadBarrier.
  // Also need for String compression feature.
  if ((object_array_get_with_read_barrier && kUseBakerReadBarrier)
      || (mirror::kUseStringCompression && instruction->IsStringCharAt())) {
    locations->AddTemp(Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARM::VisitArrayGet(HArrayGet* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location obj_loc = locations->InAt(0);
  Register obj = obj_loc.AsRegister<Register>();
  Location index = locations->InAt(1);
  Location out_loc = locations->Out();
  uint32_t data_offset = CodeGenerator::GetArrayDataOffset(instruction);
  Primitive::Type type = instruction->GetType();
  const bool maybe_compressed_char_at = mirror::kUseStringCompression &&
                                        instruction->IsStringCharAt();
  HInstruction* array_instr = instruction->GetArray();
  bool has_intermediate_address = array_instr->IsIntermediateAddress();

  switch (type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
    case Primitive::kPrimInt: {
      Register length;
      if (maybe_compressed_char_at) {
        length = locations->GetTemp(0).AsRegister<Register>();
        uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
        __ LoadFromOffset(kLoadWord, length, obj, count_offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }
      if (index.IsConstant()) {
        int32_t const_index = index.GetConstant()->AsIntConstant()->GetValue();
        if (maybe_compressed_char_at) {
          Label uncompressed_load, done;
          Label* final_label = codegen_->GetFinalLabel(instruction, &done);
          __ Lsrs(length, length, 1u);  // LSRS has a 16-bit encoding, TST (immediate) does not.
          static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                        "Expecting 0=compressed, 1=uncompressed");
          __ b(&uncompressed_load, CS);
          __ LoadFromOffset(kLoadUnsignedByte,
                            out_loc.AsRegister<Register>(),
                            obj,
                            data_offset + const_index);
          __ b(final_label);
          __ Bind(&uncompressed_load);
          __ LoadFromOffset(GetLoadOperandType(Primitive::kPrimChar),
                            out_loc.AsRegister<Register>(),
                            obj,
                            data_offset + (const_index << 1));
          if (done.IsLinked()) {
            __ Bind(&done);
          }
        } else {
          uint32_t full_offset = data_offset + (const_index << Primitive::ComponentSizeShift(type));

          LoadOperandType load_type = GetLoadOperandType(type);
          __ LoadFromOffset(load_type, out_loc.AsRegister<Register>(), obj, full_offset);
        }
      } else {
        Register temp = IP;

        if (has_intermediate_address) {
          // We do not need to compute the intermediate address from the array: the
          // input instruction has done it already. See the comment in
          // `TryExtractArrayAccessAddress()`.
          if (kIsDebugBuild) {
            HIntermediateAddress* tmp = array_instr->AsIntermediateAddress();
            DCHECK_EQ(tmp->GetOffset()->AsIntConstant()->GetValueAsUint64(), data_offset);
          }
          temp = obj;
        } else {
          __ add(temp, obj, ShifterOperand(data_offset));
        }
        if (maybe_compressed_char_at) {
          Label uncompressed_load, done;
          Label* final_label = codegen_->GetFinalLabel(instruction, &done);
          __ Lsrs(length, length, 1u);  // LSRS has a 16-bit encoding, TST (immediate) does not.
          static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                        "Expecting 0=compressed, 1=uncompressed");
          __ b(&uncompressed_load, CS);
          __ ldrb(out_loc.AsRegister<Register>(),
                  Address(temp, index.AsRegister<Register>(), Shift::LSL, 0));
          __ b(final_label);
          __ Bind(&uncompressed_load);
          __ ldrh(out_loc.AsRegister<Register>(),
                  Address(temp, index.AsRegister<Register>(), Shift::LSL, 1));
          if (done.IsLinked()) {
            __ Bind(&done);
          }
        } else {
          codegen_->LoadFromShiftedRegOffset(type, out_loc, temp, index.AsRegister<Register>());
        }
      }
      break;
    }

    case Primitive::kPrimNot: {
      // The read barrier instrumentation of object ArrayGet
      // instructions does not support the HIntermediateAddress
      // instruction.
      DCHECK(!(has_intermediate_address && kEmitCompilerReadBarrier));

      static_assert(
          sizeof(mirror::HeapReference<mirror::Object>) == sizeof(int32_t),
          "art::mirror::HeapReference<art::mirror::Object> and int32_t have different sizes.");
      // /* HeapReference<Object> */ out =
      //     *(obj + data_offset + index * sizeof(HeapReference<Object>))
      if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
        Location temp = locations->GetTemp(0);
        // Note that a potential implicit null check is handled in this
        // CodeGeneratorARM::GenerateArrayLoadWithBakerReadBarrier call.
        codegen_->GenerateArrayLoadWithBakerReadBarrier(
            instruction, out_loc, obj, data_offset, index, temp, /* needs_null_check */ true);
      } else {
        Register out = out_loc.AsRegister<Register>();
        if (index.IsConstant()) {
          size_t offset =
              (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
          __ LoadFromOffset(kLoadWord, out, obj, offset);
          codegen_->MaybeRecordImplicitNullCheck(instruction);
          // If read barriers are enabled, emit read barriers other than
          // Baker's using a slow path (and also unpoison the loaded
          // reference, if heap poisoning is enabled).
          codegen_->MaybeGenerateReadBarrierSlow(instruction, out_loc, out_loc, obj_loc, offset);
        } else {
          Register temp = IP;

          if (has_intermediate_address) {
            // We do not need to compute the intermediate address from the array: the
            // input instruction has done it already. See the comment in
            // `TryExtractArrayAccessAddress()`.
            if (kIsDebugBuild) {
              HIntermediateAddress* tmp = array_instr->AsIntermediateAddress();
              DCHECK_EQ(tmp->GetOffset()->AsIntConstant()->GetValueAsUint64(), data_offset);
            }
            temp = obj;
          } else {
            __ add(temp, obj, ShifterOperand(data_offset));
          }
          codegen_->LoadFromShiftedRegOffset(type, out_loc, temp, index.AsRegister<Register>());

          codegen_->MaybeRecordImplicitNullCheck(instruction);
          // If read barriers are enabled, emit read barriers other than
          // Baker's using a slow path (and also unpoison the loaded
          // reference, if heap poisoning is enabled).
          codegen_->MaybeGenerateReadBarrierSlow(
              instruction, out_loc, out_loc, obj_loc, data_offset, index);
        }
      }
      break;
    }

    case Primitive::kPrimLong: {
      if (index.IsConstant()) {
        size_t offset =
            (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_8) + data_offset;
        __ LoadFromOffset(kLoadWordPair, out_loc.AsRegisterPairLow<Register>(), obj, offset);
      } else {
        __ add(IP, obj, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_8));
        __ LoadFromOffset(kLoadWordPair, out_loc.AsRegisterPairLow<Register>(), IP, data_offset);
      }
      break;
    }

    case Primitive::kPrimFloat: {
      SRegister out = out_loc.AsFpuRegister<SRegister>();
      if (index.IsConstant()) {
        size_t offset = (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
        __ LoadSFromOffset(out, obj, offset);
      } else {
        __ add(IP, obj, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_4));
        __ LoadSFromOffset(out, IP, data_offset);
      }
      break;
    }

    case Primitive::kPrimDouble: {
      SRegister out = out_loc.AsFpuRegisterPairLow<SRegister>();
      if (index.IsConstant()) {
        size_t offset = (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_8) + data_offset;
        __ LoadDFromOffset(FromLowSToD(out), obj, offset);
      } else {
        __ add(IP, obj, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_8));
        __ LoadDFromOffset(FromLowSToD(out), IP, data_offset);
      }
      break;
    }

    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unreachable type " << type;
      UNREACHABLE();
  }

  if (type == Primitive::kPrimNot) {
    // Potential implicit null checks, in the case of reference
    // arrays, are handled in the previous switch statement.
  } else if (!maybe_compressed_char_at) {
    codegen_->MaybeRecordImplicitNullCheck(instruction);
  }
}

void LocationsBuilderARM::VisitArraySet(HArraySet* instruction) {
  Primitive::Type value_type = instruction->GetComponentType();

  bool needs_write_barrier =
      CodeGenerator::StoreNeedsWriteBarrier(value_type, instruction->GetValue());
  bool may_need_runtime_call_for_type_check = instruction->NeedsTypeCheck();

  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(
      instruction,
      may_need_runtime_call_for_type_check ?
          LocationSummary::kCallOnSlowPath :
          LocationSummary::kNoCall);

  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
  if (Primitive::IsFloatingPointType(value_type)) {
    locations->SetInAt(2, Location::RequiresFpuRegister());
  } else {
    locations->SetInAt(2, Location::RequiresRegister());
  }
  if (needs_write_barrier) {
    // Temporary registers for the write barrier.
    locations->AddTemp(Location::RequiresRegister());  // Possibly used for ref. poisoning too.
    locations->AddTemp(Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARM::VisitArraySet(HArraySet* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location array_loc = locations->InAt(0);
  Register array = array_loc.AsRegister<Register>();
  Location index = locations->InAt(1);
  Primitive::Type value_type = instruction->GetComponentType();
  bool may_need_runtime_call_for_type_check = instruction->NeedsTypeCheck();
  bool needs_write_barrier =
      CodeGenerator::StoreNeedsWriteBarrier(value_type, instruction->GetValue());
  uint32_t data_offset =
      mirror::Array::DataOffset(Primitive::ComponentSize(value_type)).Uint32Value();
  Location value_loc = locations->InAt(2);
  HInstruction* array_instr = instruction->GetArray();
  bool has_intermediate_address = array_instr->IsIntermediateAddress();

  switch (value_type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
    case Primitive::kPrimInt: {
      if (index.IsConstant()) {
        int32_t const_index = index.GetConstant()->AsIntConstant()->GetValue();
        uint32_t full_offset =
            data_offset + (const_index << Primitive::ComponentSizeShift(value_type));
        StoreOperandType store_type = GetStoreOperandType(value_type);
        __ StoreToOffset(store_type, value_loc.AsRegister<Register>(), array, full_offset);
      } else {
        Register temp = IP;

        if (has_intermediate_address) {
          // We do not need to compute the intermediate address from the array: the
          // input instruction has done it already. See the comment in
          // `TryExtractArrayAccessAddress()`.
          if (kIsDebugBuild) {
            HIntermediateAddress* tmp = array_instr->AsIntermediateAddress();
            DCHECK(tmp->GetOffset()->AsIntConstant()->GetValueAsUint64() == data_offset);
          }
          temp = array;
        } else {
          __ add(temp, array, ShifterOperand(data_offset));
        }
        codegen_->StoreToShiftedRegOffset(value_type,
                                          value_loc,
                                          temp,
                                          index.AsRegister<Register>());
      }
      break;
    }

    case Primitive::kPrimNot: {
      Register value = value_loc.AsRegister<Register>();
      // TryExtractArrayAccessAddress optimization is never applied for non-primitive ArraySet.
      // See the comment in instruction_simplifier_shared.cc.
      DCHECK(!has_intermediate_address);

      if (instruction->InputAt(2)->IsNullConstant()) {
        // Just setting null.
        if (index.IsConstant()) {
          size_t offset =
              (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
          __ StoreToOffset(kStoreWord, value, array, offset);
        } else {
          DCHECK(index.IsRegister()) << index;
          __ add(IP, array, ShifterOperand(data_offset));
          codegen_->StoreToShiftedRegOffset(value_type,
                                            value_loc,
                                            IP,
                                            index.AsRegister<Register>());
        }
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        DCHECK(!needs_write_barrier);
        DCHECK(!may_need_runtime_call_for_type_check);
        break;
      }

      DCHECK(needs_write_barrier);
      Location temp1_loc = locations->GetTemp(0);
      Register temp1 = temp1_loc.AsRegister<Register>();
      Location temp2_loc = locations->GetTemp(1);
      Register temp2 = temp2_loc.AsRegister<Register>();
      uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
      uint32_t super_offset = mirror::Class::SuperClassOffset().Int32Value();
      uint32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
      Label done;
      Label* final_label = codegen_->GetFinalLabel(instruction, &done);
      SlowPathCodeARM* slow_path = nullptr;

      if (may_need_runtime_call_for_type_check) {
        slow_path = new (GetGraph()->GetArena()) ArraySetSlowPathARM(instruction);
        codegen_->AddSlowPath(slow_path);
        if (instruction->GetValueCanBeNull()) {
          Label non_zero;
          __ CompareAndBranchIfNonZero(value, &non_zero);
          if (index.IsConstant()) {
            size_t offset =
               (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
            __ StoreToOffset(kStoreWord, value, array, offset);
          } else {
            DCHECK(index.IsRegister()) << index;
            __ add(IP, array, ShifterOperand(data_offset));
            codegen_->StoreToShiftedRegOffset(value_type,
                                              value_loc,
                                              IP,
                                              index.AsRegister<Register>());
          }
          codegen_->MaybeRecordImplicitNullCheck(instruction);
          __ b(final_label);
          __ Bind(&non_zero);
        }

        // Note that when read barriers are enabled, the type checks
        // are performed without read barriers.  This is fine, even in
        // the case where a class object is in the from-space after
        // the flip, as a comparison involving such a type would not
        // produce a false positive; it may of course produce a false
        // negative, in which case we would take the ArraySet slow
        // path.

        // /* HeapReference<Class> */ temp1 = array->klass_
        __ LoadFromOffset(kLoadWord, temp1, array, class_offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        __ MaybeUnpoisonHeapReference(temp1);

        // /* HeapReference<Class> */ temp1 = temp1->component_type_
        __ LoadFromOffset(kLoadWord, temp1, temp1, component_offset);
        // /* HeapReference<Class> */ temp2 = value->klass_
        __ LoadFromOffset(kLoadWord, temp2, value, class_offset);
        // If heap poisoning is enabled, no need to unpoison `temp1`
        // nor `temp2`, as we are comparing two poisoned references.
        __ cmp(temp1, ShifterOperand(temp2));

        if (instruction->StaticTypeOfArrayIsObjectArray()) {
          Label do_put;
          __ b(&do_put, EQ);
          // If heap poisoning is enabled, the `temp1` reference has
          // not been unpoisoned yet; unpoison it now.
          __ MaybeUnpoisonHeapReference(temp1);

          // /* HeapReference<Class> */ temp1 = temp1->super_class_
          __ LoadFromOffset(kLoadWord, temp1, temp1, super_offset);
          // If heap poisoning is enabled, no need to unpoison
          // `temp1`, as we are comparing against null below.
          __ CompareAndBranchIfNonZero(temp1, slow_path->GetEntryLabel());
          __ Bind(&do_put);
        } else {
          __ b(slow_path->GetEntryLabel(), NE);
        }
      }

      Register source = value;
      if (kPoisonHeapReferences) {
        // Note that in the case where `value` is a null reference,
        // we do not enter this block, as a null reference does not
        // need poisoning.
        DCHECK_EQ(value_type, Primitive::kPrimNot);
        __ Mov(temp1, value);
        __ PoisonHeapReference(temp1);
        source = temp1;
      }

      if (index.IsConstant()) {
        size_t offset =
            (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
        __ StoreToOffset(kStoreWord, source, array, offset);
      } else {
        DCHECK(index.IsRegister()) << index;

        __ add(IP, array, ShifterOperand(data_offset));
        codegen_->StoreToShiftedRegOffset(value_type,
                                          Location::RegisterLocation(source),
                                          IP,
                                          index.AsRegister<Register>());
      }

      if (!may_need_runtime_call_for_type_check) {
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }

      codegen_->MarkGCCard(temp1, temp2, array, value, instruction->GetValueCanBeNull());

      if (done.IsLinked()) {
        __ Bind(&done);
      }

      if (slow_path != nullptr) {
        __ Bind(slow_path->GetExitLabel());
      }

      break;
    }

    case Primitive::kPrimLong: {
      Location value = locations->InAt(2);
      if (index.IsConstant()) {
        size_t offset =
            (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_8) + data_offset;
        __ StoreToOffset(kStoreWordPair, value.AsRegisterPairLow<Register>(), array, offset);
      } else {
        __ add(IP, array, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_8));
        __ StoreToOffset(kStoreWordPair, value.AsRegisterPairLow<Register>(), IP, data_offset);
      }
      break;
    }

    case Primitive::kPrimFloat: {
      Location value = locations->InAt(2);
      DCHECK(value.IsFpuRegister());
      if (index.IsConstant()) {
        size_t offset = (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_4) + data_offset;
        __ StoreSToOffset(value.AsFpuRegister<SRegister>(), array, offset);
      } else {
        __ add(IP, array, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_4));
        __ StoreSToOffset(value.AsFpuRegister<SRegister>(), IP, data_offset);
      }
      break;
    }

    case Primitive::kPrimDouble: {
      Location value = locations->InAt(2);
      DCHECK(value.IsFpuRegisterPair());
      if (index.IsConstant()) {
        size_t offset = (index.GetConstant()->AsIntConstant()->GetValue() << TIMES_8) + data_offset;
        __ StoreDToOffset(FromLowSToD(value.AsFpuRegisterPairLow<SRegister>()), array, offset);
      } else {
        __ add(IP, array, ShifterOperand(index.AsRegister<Register>(), LSL, TIMES_8));
        __ StoreDToOffset(FromLowSToD(value.AsFpuRegisterPairLow<SRegister>()), IP, data_offset);
      }

      break;
    }

    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unreachable type " << value_type;
      UNREACHABLE();
  }

  // Objects are handled in the switch.
  if (value_type != Primitive::kPrimNot) {
    codegen_->MaybeRecordImplicitNullCheck(instruction);
  }
}

void LocationsBuilderARM::VisitArrayLength(HArrayLength* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitArrayLength(HArrayLength* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  uint32_t offset = CodeGenerator::GetArrayLengthOffset(instruction);
  Register obj = locations->InAt(0).AsRegister<Register>();
  Register out = locations->Out().AsRegister<Register>();
  __ LoadFromOffset(kLoadWord, out, obj, offset);
  codegen_->MaybeRecordImplicitNullCheck(instruction);
  // Mask out compression flag from String's array length.
  if (mirror::kUseStringCompression && instruction->IsStringLength()) {
    __ Lsr(out, out, 1u);
  }
}

void LocationsBuilderARM::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);

  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RegisterOrConstant(instruction->GetOffset()));
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  if (second.IsRegister()) {
    __ add(out.AsRegister<Register>(),
           first.AsRegister<Register>(),
           ShifterOperand(second.AsRegister<Register>()));
  } else {
    __ AddConstant(out.AsRegister<Register>(),
                   first.AsRegister<Register>(),
                   second.GetConstant()->AsIntConstant()->GetValue());
  }
}

void LocationsBuilderARM::VisitBoundsCheck(HBoundsCheck* instruction) {
  RegisterSet caller_saves = RegisterSet::Empty();
  InvokeRuntimeCallingConvention calling_convention;
  caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  LocationSummary* locations = codegen_->CreateThrowingSlowPathLocations(instruction, caller_saves);

  HInstruction* index = instruction->InputAt(0);
  HInstruction* length = instruction->InputAt(1);
  // If both index and length are constants we can statically check the bounds. But if at least one
  // of them is not encodable ArmEncodableConstantOrRegister will create
  // Location::RequiresRegister() which is not desired to happen. Instead we create constant
  // locations.
  bool both_const = index->IsConstant() && length->IsConstant();
  locations->SetInAt(0, both_const
      ? Location::ConstantLocation(index->AsConstant())
      : ArmEncodableConstantOrRegister(index, CMP));
  locations->SetInAt(1, both_const
      ? Location::ConstantLocation(length->AsConstant())
      : ArmEncodableConstantOrRegister(length, CMP));
}

void InstructionCodeGeneratorARM::VisitBoundsCheck(HBoundsCheck* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location index_loc = locations->InAt(0);
  Location length_loc = locations->InAt(1);

  if (length_loc.IsConstant()) {
    int32_t length = helpers::Int32ConstantFrom(length_loc);
    if (index_loc.IsConstant()) {
      // BCE will remove the bounds check if we are guaranteed to pass.
      int32_t index = helpers::Int32ConstantFrom(index_loc);
      if (index < 0 || index >= length) {
        SlowPathCodeARM* slow_path =
            new (GetGraph()->GetArena()) BoundsCheckSlowPathARM(instruction);
        codegen_->AddSlowPath(slow_path);
        __ b(slow_path->GetEntryLabel());
      } else {
        // Some optimization after BCE may have generated this, and we should not
        // generate a bounds check if it is a valid range.
      }
      return;
    }

    SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) BoundsCheckSlowPathARM(instruction);
    __ cmp(index_loc.AsRegister<Register>(), ShifterOperand(length));
    codegen_->AddSlowPath(slow_path);
    __ b(slow_path->GetEntryLabel(), HS);
  } else {
    SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) BoundsCheckSlowPathARM(instruction);
    if (index_loc.IsConstant()) {
      int32_t index = helpers::Int32ConstantFrom(index_loc);
      __ cmp(length_loc.AsRegister<Register>(), ShifterOperand(index));
    } else {
      __ cmp(length_loc.AsRegister<Register>(), ShifterOperand(index_loc.AsRegister<Register>()));
    }
    codegen_->AddSlowPath(slow_path);
    __ b(slow_path->GetEntryLabel(), LS);
  }
}

void CodeGeneratorARM::MarkGCCard(Register temp,
                                  Register card,
                                  Register object,
                                  Register value,
                                  bool can_be_null) {
  Label is_null;
  if (can_be_null) {
    __ CompareAndBranchIfZero(value, &is_null);
  }
  __ LoadFromOffset(kLoadWord, card, TR, Thread::CardTableOffset<kArmPointerSize>().Int32Value());
  __ Lsr(temp, object, gc::accounting::CardTable::kCardShift);
  __ strb(card, Address(card, temp));
  if (can_be_null) {
    __ Bind(&is_null);
  }
}

void LocationsBuilderARM::VisitParallelMove(HParallelMove* instruction ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorARM::VisitParallelMove(HParallelMove* instruction) {
  codegen_->GetMoveResolver()->EmitNativeCode(instruction);
}

void LocationsBuilderARM::VisitSuspendCheck(HSuspendCheck* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnSlowPath);
  locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
}

void InstructionCodeGeneratorARM::VisitSuspendCheck(HSuspendCheck* instruction) {
  HBasicBlock* block = instruction->GetBlock();
  if (block->GetLoopInformation() != nullptr) {
    DCHECK(block->GetLoopInformation()->GetSuspendCheck() == instruction);
    // The back edge will generate the suspend check.
    return;
  }
  if (block->IsEntryBlock() && instruction->GetNext()->IsGoto()) {
    // The goto will generate the suspend check.
    return;
  }
  GenerateSuspendCheck(instruction, nullptr);
}

void InstructionCodeGeneratorARM::GenerateSuspendCheck(HSuspendCheck* instruction,
                                                       HBasicBlock* successor) {
  SuspendCheckSlowPathARM* slow_path =
      down_cast<SuspendCheckSlowPathARM*>(instruction->GetSlowPath());
  if (slow_path == nullptr) {
    slow_path = new (GetGraph()->GetArena()) SuspendCheckSlowPathARM(instruction, successor);
    instruction->SetSlowPath(slow_path);
    codegen_->AddSlowPath(slow_path);
    if (successor != nullptr) {
      DCHECK(successor->IsLoopHeader());
      codegen_->ClearSpillSlotsFromLoopPhisInStackMap(instruction);
    }
  } else {
    DCHECK_EQ(slow_path->GetSuccessor(), successor);
  }

  __ LoadFromOffset(
      kLoadUnsignedHalfword, IP, TR, Thread::ThreadFlagsOffset<kArmPointerSize>().Int32Value());
  if (successor == nullptr) {
    __ CompareAndBranchIfNonZero(IP, slow_path->GetEntryLabel());
    __ Bind(slow_path->GetReturnLabel());
  } else {
    __ CompareAndBranchIfZero(IP, codegen_->GetLabelOf(successor));
    __ b(slow_path->GetEntryLabel());
  }
}

ArmAssembler* ParallelMoveResolverARM::GetAssembler() const {
  return codegen_->GetAssembler();
}

void ParallelMoveResolverARM::EmitMove(size_t index) {
  MoveOperands* move = moves_[index];
  Location source = move->GetSource();
  Location destination = move->GetDestination();

  if (source.IsRegister()) {
    if (destination.IsRegister()) {
      __ Mov(destination.AsRegister<Register>(), source.AsRegister<Register>());
    } else if (destination.IsFpuRegister()) {
      __ vmovsr(destination.AsFpuRegister<SRegister>(), source.AsRegister<Register>());
    } else {
      DCHECK(destination.IsStackSlot());
      __ StoreToOffset(kStoreWord, source.AsRegister<Register>(),
                       SP, destination.GetStackIndex());
    }
  } else if (source.IsStackSlot()) {
    if (destination.IsRegister()) {
      __ LoadFromOffset(kLoadWord, destination.AsRegister<Register>(),
                        SP, source.GetStackIndex());
    } else if (destination.IsFpuRegister()) {
      __ LoadSFromOffset(destination.AsFpuRegister<SRegister>(), SP, source.GetStackIndex());
    } else {
      DCHECK(destination.IsStackSlot());
      __ LoadFromOffset(kLoadWord, IP, SP, source.GetStackIndex());
      __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
    }
  } else if (source.IsFpuRegister()) {
    if (destination.IsRegister()) {
      __ vmovrs(destination.AsRegister<Register>(), source.AsFpuRegister<SRegister>());
    } else if (destination.IsFpuRegister()) {
      __ vmovs(destination.AsFpuRegister<SRegister>(), source.AsFpuRegister<SRegister>());
    } else {
      DCHECK(destination.IsStackSlot());
      __ StoreSToOffset(source.AsFpuRegister<SRegister>(), SP, destination.GetStackIndex());
    }
  } else if (source.IsDoubleStackSlot()) {
    if (destination.IsDoubleStackSlot()) {
      __ LoadDFromOffset(DTMP, SP, source.GetStackIndex());
      __ StoreDToOffset(DTMP, SP, destination.GetStackIndex());
    } else if (destination.IsRegisterPair()) {
      DCHECK(ExpectedPairLayout(destination));
      __ LoadFromOffset(
          kLoadWordPair, destination.AsRegisterPairLow<Register>(), SP, source.GetStackIndex());
    } else {
      DCHECK(destination.IsFpuRegisterPair()) << destination;
      __ LoadDFromOffset(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()),
                         SP,
                         source.GetStackIndex());
    }
  } else if (source.IsRegisterPair()) {
    if (destination.IsRegisterPair()) {
      __ Mov(destination.AsRegisterPairLow<Register>(), source.AsRegisterPairLow<Register>());
      __ Mov(destination.AsRegisterPairHigh<Register>(), source.AsRegisterPairHigh<Register>());
    } else if (destination.IsFpuRegisterPair()) {
      __ vmovdrr(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()),
                 source.AsRegisterPairLow<Register>(),
                 source.AsRegisterPairHigh<Register>());
    } else {
      DCHECK(destination.IsDoubleStackSlot()) << destination;
      DCHECK(ExpectedPairLayout(source));
      __ StoreToOffset(
          kStoreWordPair, source.AsRegisterPairLow<Register>(), SP, destination.GetStackIndex());
    }
  } else if (source.IsFpuRegisterPair()) {
    if (destination.IsRegisterPair()) {
      __ vmovrrd(destination.AsRegisterPairLow<Register>(),
                 destination.AsRegisterPairHigh<Register>(),
                 FromLowSToD(source.AsFpuRegisterPairLow<SRegister>()));
    } else if (destination.IsFpuRegisterPair()) {
      __ vmovd(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()),
               FromLowSToD(source.AsFpuRegisterPairLow<SRegister>()));
    } else {
      DCHECK(destination.IsDoubleStackSlot()) << destination;
      __ StoreDToOffset(FromLowSToD(source.AsFpuRegisterPairLow<SRegister>()),
                        SP,
                        destination.GetStackIndex());
    }
  } else {
    DCHECK(source.IsConstant()) << source;
    HConstant* constant = source.GetConstant();
    if (constant->IsIntConstant() || constant->IsNullConstant()) {
      int32_t value = CodeGenerator::GetInt32ValueOf(constant);
      if (destination.IsRegister()) {
        __ LoadImmediate(destination.AsRegister<Register>(), value);
      } else {
        DCHECK(destination.IsStackSlot());
        __ LoadImmediate(IP, value);
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
      }
    } else if (constant->IsLongConstant()) {
      int64_t value = constant->AsLongConstant()->GetValue();
      if (destination.IsRegisterPair()) {
        __ LoadImmediate(destination.AsRegisterPairLow<Register>(), Low32Bits(value));
        __ LoadImmediate(destination.AsRegisterPairHigh<Register>(), High32Bits(value));
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        __ LoadImmediate(IP, Low32Bits(value));
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
        __ LoadImmediate(IP, High32Bits(value));
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetHighStackIndex(kArmWordSize));
      }
    } else if (constant->IsDoubleConstant()) {
      double value = constant->AsDoubleConstant()->GetValue();
      if (destination.IsFpuRegisterPair()) {
        __ LoadDImmediate(FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>()), value);
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        uint64_t int_value = bit_cast<uint64_t, double>(value);
        __ LoadImmediate(IP, Low32Bits(int_value));
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
        __ LoadImmediate(IP, High32Bits(int_value));
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetHighStackIndex(kArmWordSize));
      }
    } else {
      DCHECK(constant->IsFloatConstant()) << constant->DebugName();
      float value = constant->AsFloatConstant()->GetValue();
      if (destination.IsFpuRegister()) {
        __ LoadSImmediate(destination.AsFpuRegister<SRegister>(), value);
      } else {
        DCHECK(destination.IsStackSlot());
        __ LoadImmediate(IP, bit_cast<int32_t, float>(value));
        __ StoreToOffset(kStoreWord, IP, SP, destination.GetStackIndex());
      }
    }
  }
}

void ParallelMoveResolverARM::Exchange(Register reg, int mem) {
  __ Mov(IP, reg);
  __ LoadFromOffset(kLoadWord, reg, SP, mem);
  __ StoreToOffset(kStoreWord, IP, SP, mem);
}

void ParallelMoveResolverARM::Exchange(int mem1, int mem2) {
  ScratchRegisterScope ensure_scratch(this, IP, R0, codegen_->GetNumberOfCoreRegisters());
  int stack_offset = ensure_scratch.IsSpilled() ? kArmWordSize : 0;
  __ LoadFromOffset(kLoadWord, static_cast<Register>(ensure_scratch.GetRegister()),
                    SP, mem1 + stack_offset);
  __ LoadFromOffset(kLoadWord, IP, SP, mem2 + stack_offset);
  __ StoreToOffset(kStoreWord, static_cast<Register>(ensure_scratch.GetRegister()),
                   SP, mem2 + stack_offset);
  __ StoreToOffset(kStoreWord, IP, SP, mem1 + stack_offset);
}

void ParallelMoveResolverARM::EmitSwap(size_t index) {
  MoveOperands* move = moves_[index];
  Location source = move->GetSource();
  Location destination = move->GetDestination();

  if (source.IsRegister() && destination.IsRegister()) {
    DCHECK_NE(source.AsRegister<Register>(), IP);
    DCHECK_NE(destination.AsRegister<Register>(), IP);
    __ Mov(IP, source.AsRegister<Register>());
    __ Mov(source.AsRegister<Register>(), destination.AsRegister<Register>());
    __ Mov(destination.AsRegister<Register>(), IP);
  } else if (source.IsRegister() && destination.IsStackSlot()) {
    Exchange(source.AsRegister<Register>(), destination.GetStackIndex());
  } else if (source.IsStackSlot() && destination.IsRegister()) {
    Exchange(destination.AsRegister<Register>(), source.GetStackIndex());
  } else if (source.IsStackSlot() && destination.IsStackSlot()) {
    Exchange(source.GetStackIndex(), destination.GetStackIndex());
  } else if (source.IsFpuRegister() && destination.IsFpuRegister()) {
    __ vmovrs(IP, source.AsFpuRegister<SRegister>());
    __ vmovs(source.AsFpuRegister<SRegister>(), destination.AsFpuRegister<SRegister>());
    __ vmovsr(destination.AsFpuRegister<SRegister>(), IP);
  } else if (source.IsRegisterPair() && destination.IsRegisterPair()) {
    __ vmovdrr(DTMP, source.AsRegisterPairLow<Register>(), source.AsRegisterPairHigh<Register>());
    __ Mov(source.AsRegisterPairLow<Register>(), destination.AsRegisterPairLow<Register>());
    __ Mov(source.AsRegisterPairHigh<Register>(), destination.AsRegisterPairHigh<Register>());
    __ vmovrrd(destination.AsRegisterPairLow<Register>(),
               destination.AsRegisterPairHigh<Register>(),
               DTMP);
  } else if (source.IsRegisterPair() || destination.IsRegisterPair()) {
    Register low_reg = source.IsRegisterPair()
        ? source.AsRegisterPairLow<Register>()
        : destination.AsRegisterPairLow<Register>();
    int mem = source.IsRegisterPair()
        ? destination.GetStackIndex()
        : source.GetStackIndex();
    DCHECK(ExpectedPairLayout(source.IsRegisterPair() ? source : destination));
    __ vmovdrr(DTMP, low_reg, static_cast<Register>(low_reg + 1));
    __ LoadFromOffset(kLoadWordPair, low_reg, SP, mem);
    __ StoreDToOffset(DTMP, SP, mem);
  } else if (source.IsFpuRegisterPair() && destination.IsFpuRegisterPair()) {
    DRegister first = FromLowSToD(source.AsFpuRegisterPairLow<SRegister>());
    DRegister second = FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>());
    __ vmovd(DTMP, first);
    __ vmovd(first, second);
    __ vmovd(second, DTMP);
  } else if (source.IsFpuRegisterPair() || destination.IsFpuRegisterPair()) {
    DRegister reg = source.IsFpuRegisterPair()
        ? FromLowSToD(source.AsFpuRegisterPairLow<SRegister>())
        : FromLowSToD(destination.AsFpuRegisterPairLow<SRegister>());
    int mem = source.IsFpuRegisterPair()
        ? destination.GetStackIndex()
        : source.GetStackIndex();
    __ vmovd(DTMP, reg);
    __ LoadDFromOffset(reg, SP, mem);
    __ StoreDToOffset(DTMP, SP, mem);
  } else if (source.IsFpuRegister() || destination.IsFpuRegister()) {
    SRegister reg = source.IsFpuRegister() ? source.AsFpuRegister<SRegister>()
                                           : destination.AsFpuRegister<SRegister>();
    int mem = source.IsFpuRegister()
        ? destination.GetStackIndex()
        : source.GetStackIndex();

    __ vmovrs(IP, reg);
    __ LoadSFromOffset(reg, SP, mem);
    __ StoreToOffset(kStoreWord, IP, SP, mem);
  } else if (source.IsDoubleStackSlot() && destination.IsDoubleStackSlot()) {
    Exchange(source.GetStackIndex(), destination.GetStackIndex());
    Exchange(source.GetHighStackIndex(kArmWordSize), destination.GetHighStackIndex(kArmWordSize));
  } else {
    LOG(FATAL) << "Unimplemented" << source << " <-> " << destination;
  }
}

void ParallelMoveResolverARM::SpillScratch(int reg) {
  __ Push(static_cast<Register>(reg));
}

void ParallelMoveResolverARM::RestoreScratch(int reg) {
  __ Pop(static_cast<Register>(reg));
}

HLoadClass::LoadKind CodeGeneratorARM::GetSupportedLoadClassKind(
    HLoadClass::LoadKind desired_class_load_kind) {
  switch (desired_class_load_kind) {
    case HLoadClass::LoadKind::kInvalid:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    case HLoadClass::LoadKind::kReferrersClass:
      break;
    case HLoadClass::LoadKind::kBootImageLinkTimeAddress:
      DCHECK(!GetCompilerOptions().GetCompilePic());
      break;
    case HLoadClass::LoadKind::kBootImageLinkTimePcRelative:
      DCHECK(GetCompilerOptions().GetCompilePic());
      break;
    case HLoadClass::LoadKind::kBootImageAddress:
      break;
    case HLoadClass::LoadKind::kBssEntry:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kDexCacheViaMethod:
      break;
  }
  return desired_class_load_kind;
}

void LocationsBuilderARM::VisitLoadClass(HLoadClass* cls) {
  HLoadClass::LoadKind load_kind = cls->GetLoadKind();
  if (load_kind == HLoadClass::LoadKind::kDexCacheViaMethod) {
    InvokeRuntimeCallingConvention calling_convention;
    CodeGenerator::CreateLoadClassRuntimeCallLocationSummary(
        cls,
        Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
        Location::RegisterLocation(R0));
    DCHECK_EQ(calling_convention.GetRegisterAt(0), R0);
    return;
  }
  DCHECK(!cls->NeedsAccessCheck());

  const bool requires_read_barrier = kEmitCompilerReadBarrier && !cls->IsInBootImage();
  LocationSummary::CallKind call_kind = (cls->NeedsEnvironment() || requires_read_barrier)
      ? LocationSummary::kCallOnSlowPath
      : LocationSummary::kNoCall;
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(cls, call_kind);
  if (kUseBakerReadBarrier && requires_read_barrier && !cls->NeedsEnvironment()) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }

  if (load_kind == HLoadClass::LoadKind::kReferrersClass) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
  locations->SetOut(Location::RequiresRegister());
  if (load_kind == HLoadClass::LoadKind::kBssEntry) {
    if (!kUseReadBarrier || kUseBakerReadBarrier) {
      // Rely on the type resolution or initialization and marking to save everything we need.
      // Note that IP may be clobbered by saving/restoring the live register (only one thanks
      // to the custom calling convention) or by marking, so we request a different temp.
      locations->AddTemp(Location::RequiresRegister());
      RegisterSet caller_saves = RegisterSet::Empty();
      InvokeRuntimeCallingConvention calling_convention;
      caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
      // TODO: Add GetReturnLocation() to the calling convention so that we can DCHECK()
      // that the the kPrimNot result register is the same as the first argument register.
      locations->SetCustomSlowPathCallerSaves(caller_saves);
    } else {
      // For non-Baker read barrier we have a temp-clobbering call.
    }
  }
}

// NO_THREAD_SAFETY_ANALYSIS as we manipulate handles whose internal object we know does not
// move.
void InstructionCodeGeneratorARM::VisitLoadClass(HLoadClass* cls) NO_THREAD_SAFETY_ANALYSIS {
  HLoadClass::LoadKind load_kind = cls->GetLoadKind();
  if (load_kind == HLoadClass::LoadKind::kDexCacheViaMethod) {
    codegen_->GenerateLoadClassRuntimeCall(cls);
    return;
  }
  DCHECK(!cls->NeedsAccessCheck());

  LocationSummary* locations = cls->GetLocations();
  Location out_loc = locations->Out();
  Register out = out_loc.AsRegister<Register>();

  const ReadBarrierOption read_barrier_option = cls->IsInBootImage()
      ? kWithoutReadBarrier
      : kCompilerReadBarrierOption;
  bool generate_null_check = false;
  switch (load_kind) {
    case HLoadClass::LoadKind::kReferrersClass: {
      DCHECK(!cls->CanCallRuntime());
      DCHECK(!cls->MustGenerateClinitCheck());
      // /* GcRoot<mirror::Class> */ out = current_method->declaring_class_
      Register current_method = locations->InAt(0).AsRegister<Register>();
      GenerateGcRootFieldLoad(cls,
                              out_loc,
                              current_method,
                              ArtMethod::DeclaringClassOffset().Int32Value(),
                              read_barrier_option);
      break;
    }
    case HLoadClass::LoadKind::kBootImageLinkTimeAddress: {
      DCHECK(codegen_->GetCompilerOptions().IsBootImage());
      DCHECK_EQ(read_barrier_option, kWithoutReadBarrier);
      __ LoadLiteral(out, codegen_->DeduplicateBootImageTypeLiteral(cls->GetDexFile(),
                                                                    cls->GetTypeIndex()));
      break;
    }
    case HLoadClass::LoadKind::kBootImageLinkTimePcRelative: {
      DCHECK(codegen_->GetCompilerOptions().IsBootImage());
      DCHECK_EQ(read_barrier_option, kWithoutReadBarrier);
      CodeGeneratorARM::PcRelativePatchInfo* labels =
          codegen_->NewPcRelativeTypePatch(cls->GetDexFile(), cls->GetTypeIndex());
      __ BindTrackedLabel(&labels->movw_label);
      __ movw(out, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->movt_label);
      __ movt(out, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->add_pc_label);
      __ add(out, out, ShifterOperand(PC));
      break;
    }
    case HLoadClass::LoadKind::kBootImageAddress: {
      DCHECK_EQ(read_barrier_option, kWithoutReadBarrier);
      uint32_t address = dchecked_integral_cast<uint32_t>(
          reinterpret_cast<uintptr_t>(cls->GetClass().Get()));
      DCHECK_NE(address, 0u);
      __ LoadLiteral(out, codegen_->DeduplicateBootImageAddressLiteral(address));
      break;
    }
    case HLoadClass::LoadKind::kBssEntry: {
      Register temp = (!kUseReadBarrier || kUseBakerReadBarrier)
          ? locations->GetTemp(0).AsRegister<Register>()
          : out;
      CodeGeneratorARM::PcRelativePatchInfo* labels =
          codegen_->NewTypeBssEntryPatch(cls->GetDexFile(), cls->GetTypeIndex());
      __ BindTrackedLabel(&labels->movw_label);
      __ movw(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->movt_label);
      __ movt(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->add_pc_label);
      __ add(temp, temp, ShifterOperand(PC));
      GenerateGcRootFieldLoad(cls, out_loc, temp, /* offset */ 0, read_barrier_option);
      generate_null_check = true;
      break;
    }
    case HLoadClass::LoadKind::kJitTableAddress: {
      __ LoadLiteral(out, codegen_->DeduplicateJitClassLiteral(cls->GetDexFile(),
                                                               cls->GetTypeIndex(),
                                                               cls->GetClass()));
      // /* GcRoot<mirror::Class> */ out = *out
      GenerateGcRootFieldLoad(cls, out_loc, out, /* offset */ 0, read_barrier_option);
      break;
    }
    case HLoadClass::LoadKind::kDexCacheViaMethod:
    case HLoadClass::LoadKind::kInvalid:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }

  if (generate_null_check || cls->MustGenerateClinitCheck()) {
    DCHECK(cls->CanCallRuntime());
    SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) LoadClassSlowPathARM(
        cls, cls, cls->GetDexPc(), cls->MustGenerateClinitCheck());
    codegen_->AddSlowPath(slow_path);
    if (generate_null_check) {
      __ CompareAndBranchIfZero(out, slow_path->GetEntryLabel());
    }
    if (cls->MustGenerateClinitCheck()) {
      GenerateClassInitializationCheck(slow_path, out);
    } else {
      __ Bind(slow_path->GetExitLabel());
    }
  }
}

void LocationsBuilderARM::VisitClinitCheck(HClinitCheck* check) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(check, LocationSummary::kCallOnSlowPath);
  locations->SetInAt(0, Location::RequiresRegister());
  if (check->HasUses()) {
    locations->SetOut(Location::SameAsFirstInput());
  }
}

void InstructionCodeGeneratorARM::VisitClinitCheck(HClinitCheck* check) {
  // We assume the class is not null.
  SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) LoadClassSlowPathARM(
      check->GetLoadClass(), check, check->GetDexPc(), true);
  codegen_->AddSlowPath(slow_path);
  GenerateClassInitializationCheck(slow_path,
                                   check->GetLocations()->InAt(0).AsRegister<Register>());
}

void InstructionCodeGeneratorARM::GenerateClassInitializationCheck(
    SlowPathCodeARM* slow_path, Register class_reg) {
  __ LoadFromOffset(kLoadWord, IP, class_reg, mirror::Class::StatusOffset().Int32Value());
  __ cmp(IP, ShifterOperand(mirror::Class::kStatusInitialized));
  __ b(slow_path->GetEntryLabel(), LT);
  // Even if the initialized flag is set, we may be in a situation where caches are not synced
  // properly. Therefore, we do a memory fence.
  __ dmb(ISH);
  __ Bind(slow_path->GetExitLabel());
}

HLoadString::LoadKind CodeGeneratorARM::GetSupportedLoadStringKind(
    HLoadString::LoadKind desired_string_load_kind) {
  switch (desired_string_load_kind) {
    case HLoadString::LoadKind::kBootImageLinkTimeAddress:
      DCHECK(!GetCompilerOptions().GetCompilePic());
      break;
    case HLoadString::LoadKind::kBootImageLinkTimePcRelative:
      DCHECK(GetCompilerOptions().GetCompilePic());
      break;
    case HLoadString::LoadKind::kBootImageAddress:
      break;
    case HLoadString::LoadKind::kBssEntry:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kDexCacheViaMethod:
      break;
  }
  return desired_string_load_kind;
}

void LocationsBuilderARM::VisitLoadString(HLoadString* load) {
  LocationSummary::CallKind call_kind = CodeGenerator::GetLoadStringCallKind(load);
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(load, call_kind);
  HLoadString::LoadKind load_kind = load->GetLoadKind();
  if (load_kind == HLoadString::LoadKind::kDexCacheViaMethod) {
    locations->SetOut(Location::RegisterLocation(R0));
  } else {
    locations->SetOut(Location::RequiresRegister());
    if (load_kind == HLoadString::LoadKind::kBssEntry) {
      if (!kUseReadBarrier || kUseBakerReadBarrier) {
        // Rely on the pResolveString and marking to save everything we need, including temps.
        // Note that IP may be clobbered by saving/restoring the live register (only one thanks
        // to the custom calling convention) or by marking, so we request a different temp.
        locations->AddTemp(Location::RequiresRegister());
        RegisterSet caller_saves = RegisterSet::Empty();
        InvokeRuntimeCallingConvention calling_convention;
        caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
        // TODO: Add GetReturnLocation() to the calling convention so that we can DCHECK()
        // that the the kPrimNot result register is the same as the first argument register.
        locations->SetCustomSlowPathCallerSaves(caller_saves);
      } else {
        // For non-Baker read barrier we have a temp-clobbering call.
      }
    }
  }
}

// NO_THREAD_SAFETY_ANALYSIS as we manipulate handles whose internal object we know does not
// move.
void InstructionCodeGeneratorARM::VisitLoadString(HLoadString* load) NO_THREAD_SAFETY_ANALYSIS {
  LocationSummary* locations = load->GetLocations();
  Location out_loc = locations->Out();
  Register out = out_loc.AsRegister<Register>();
  HLoadString::LoadKind load_kind = load->GetLoadKind();

  switch (load_kind) {
    case HLoadString::LoadKind::kBootImageLinkTimeAddress: {
      DCHECK(codegen_->GetCompilerOptions().IsBootImage());
      __ LoadLiteral(out, codegen_->DeduplicateBootImageStringLiteral(load->GetDexFile(),
                                                                      load->GetStringIndex()));
      return;  // No dex cache slow path.
    }
    case HLoadString::LoadKind::kBootImageLinkTimePcRelative: {
      DCHECK(codegen_->GetCompilerOptions().IsBootImage());
      CodeGeneratorARM::PcRelativePatchInfo* labels =
          codegen_->NewPcRelativeStringPatch(load->GetDexFile(), load->GetStringIndex());
      __ BindTrackedLabel(&labels->movw_label);
      __ movw(out, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->movt_label);
      __ movt(out, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->add_pc_label);
      __ add(out, out, ShifterOperand(PC));
      return;  // No dex cache slow path.
    }
    case HLoadString::LoadKind::kBootImageAddress: {
      uint32_t address = dchecked_integral_cast<uint32_t>(
          reinterpret_cast<uintptr_t>(load->GetString().Get()));
      DCHECK_NE(address, 0u);
      __ LoadLiteral(out, codegen_->DeduplicateBootImageAddressLiteral(address));
      return;  // No dex cache slow path.
    }
    case HLoadString::LoadKind::kBssEntry: {
      DCHECK(!codegen_->GetCompilerOptions().IsBootImage());
      Register temp = (!kUseReadBarrier || kUseBakerReadBarrier)
          ? locations->GetTemp(0).AsRegister<Register>()
          : out;
      CodeGeneratorARM::PcRelativePatchInfo* labels =
          codegen_->NewPcRelativeStringPatch(load->GetDexFile(), load->GetStringIndex());
      __ BindTrackedLabel(&labels->movw_label);
      __ movw(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->movt_label);
      __ movt(temp, /* placeholder */ 0u);
      __ BindTrackedLabel(&labels->add_pc_label);
      __ add(temp, temp, ShifterOperand(PC));
      GenerateGcRootFieldLoad(load, out_loc, temp, /* offset */ 0, kCompilerReadBarrierOption);
      SlowPathCode* slow_path = new (GetGraph()->GetArena()) LoadStringSlowPathARM(load);
      codegen_->AddSlowPath(slow_path);
      __ CompareAndBranchIfZero(out, slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
      return;
    }
    case HLoadString::LoadKind::kJitTableAddress: {
      __ LoadLiteral(out, codegen_->DeduplicateJitStringLiteral(load->GetDexFile(),
                                                                load->GetStringIndex(),
                                                                load->GetString()));
      // /* GcRoot<mirror::String> */ out = *out
      GenerateGcRootFieldLoad(load, out_loc, out, /* offset */ 0, kCompilerReadBarrierOption);
      return;
    }
    default:
      break;
  }

  // TODO: Consider re-adding the compiler code to do string dex cache lookup again.
  DCHECK(load_kind == HLoadString::LoadKind::kDexCacheViaMethod);
  InvokeRuntimeCallingConvention calling_convention;
  DCHECK_EQ(calling_convention.GetRegisterAt(0), out);
  __ LoadImmediate(calling_convention.GetRegisterAt(0), load->GetStringIndex().index_);
  codegen_->InvokeRuntime(kQuickResolveString, load, load->GetDexPc());
  CheckEntrypointTypes<kQuickResolveString, void*, uint32_t>();
}

static int32_t GetExceptionTlsOffset() {
  return Thread::ExceptionOffset<kArmPointerSize>().Int32Value();
}

void LocationsBuilderARM::VisitLoadException(HLoadException* load) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(load, LocationSummary::kNoCall);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM::VisitLoadException(HLoadException* load) {
  Register out = load->GetLocations()->Out().AsRegister<Register>();
  __ LoadFromOffset(kLoadWord, out, TR, GetExceptionTlsOffset());
}

void LocationsBuilderARM::VisitClearException(HClearException* clear) {
  new (GetGraph()->GetArena()) LocationSummary(clear, LocationSummary::kNoCall);
}

void InstructionCodeGeneratorARM::VisitClearException(HClearException* clear ATTRIBUTE_UNUSED) {
  __ LoadImmediate(IP, 0);
  __ StoreToOffset(kStoreWord, IP, TR, GetExceptionTlsOffset());
}

void LocationsBuilderARM::VisitThrow(HThrow* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
}

void InstructionCodeGeneratorARM::VisitThrow(HThrow* instruction) {
  codegen_->InvokeRuntime(kQuickDeliverException, instruction, instruction->GetDexPc());
  CheckEntrypointTypes<kQuickDeliverException, void, mirror::Object*>();
}

// Temp is used for read barrier.
static size_t NumberOfInstanceOfTemps(TypeCheckKind type_check_kind) {
  if (kEmitCompilerReadBarrier &&
       (kUseBakerReadBarrier ||
          type_check_kind == TypeCheckKind::kAbstractClassCheck ||
          type_check_kind == TypeCheckKind::kClassHierarchyCheck ||
          type_check_kind == TypeCheckKind::kArrayObjectCheck)) {
    return 1;
  }
  return 0;
}

// Interface case has 3 temps, one for holding the number of interfaces, one for the current
// interface pointer, one for loading the current interface.
// The other checks have one temp for loading the object's class.
static size_t NumberOfCheckCastTemps(TypeCheckKind type_check_kind) {
  if (type_check_kind == TypeCheckKind::kInterfaceCheck) {
    return 3;
  }
  return 1 + NumberOfInstanceOfTemps(type_check_kind);
}

void LocationsBuilderARM::VisitInstanceOf(HInstanceOf* instruction) {
  LocationSummary::CallKind call_kind = LocationSummary::kNoCall;
  TypeCheckKind type_check_kind = instruction->GetTypeCheckKind();
  bool baker_read_barrier_slow_path = false;
  switch (type_check_kind) {
    case TypeCheckKind::kExactCheck:
    case TypeCheckKind::kAbstractClassCheck:
    case TypeCheckKind::kClassHierarchyCheck:
    case TypeCheckKind::kArrayObjectCheck:
      call_kind =
          kEmitCompilerReadBarrier ? LocationSummary::kCallOnSlowPath : LocationSummary::kNoCall;
      baker_read_barrier_slow_path = kUseBakerReadBarrier;
      break;
    case TypeCheckKind::kArrayCheck:
    case TypeCheckKind::kUnresolvedCheck:
    case TypeCheckKind::kInterfaceCheck:
      call_kind = LocationSummary::kCallOnSlowPath;
      break;
  }

  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction, call_kind);
  if (baker_read_barrier_slow_path) {
    locations->SetCustomSlowPathCallerSaves(RegisterSet::Empty());  // No caller-save registers.
  }
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // The "out" register is used as a temporary, so it overlaps with the inputs.
  // Note that TypeCheckSlowPathARM uses this register too.
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
  locations->AddRegisterTemps(NumberOfInstanceOfTemps(type_check_kind));
}

void InstructionCodeGeneratorARM::VisitInstanceOf(HInstanceOf* instruction) {
  TypeCheckKind type_check_kind = instruction->GetTypeCheckKind();
  LocationSummary* locations = instruction->GetLocations();
  Location obj_loc = locations->InAt(0);
  Register obj = obj_loc.AsRegister<Register>();
  Register cls = locations->InAt(1).AsRegister<Register>();
  Location out_loc = locations->Out();
  Register out = out_loc.AsRegister<Register>();
  const size_t num_temps = NumberOfInstanceOfTemps(type_check_kind);
  DCHECK_LE(num_temps, 1u);
  Location maybe_temp_loc = (num_temps >= 1) ? locations->GetTemp(0) : Location::NoLocation();
  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  uint32_t super_offset = mirror::Class::SuperClassOffset().Int32Value();
  uint32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
  uint32_t primitive_offset = mirror::Class::PrimitiveTypeOffset().Int32Value();
  Label done;
  Label* const final_label = codegen_->GetFinalLabel(instruction, &done);
  SlowPathCodeARM* slow_path = nullptr;

  // Return 0 if `obj` is null.
  // avoid null check if we know obj is not null.
  if (instruction->MustDoNullCheck()) {
    DCHECK_NE(out, obj);
    __ LoadImmediate(out, 0);
    __ CompareAndBranchIfZero(obj, final_label);
  }

  switch (type_check_kind) {
    case TypeCheckKind::kExactCheck: {
      // /* HeapReference<Class> */ out = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        out_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp_loc,
                                        kCompilerReadBarrierOption);
      // Classes must be equal for the instanceof to succeed.
      __ cmp(out, ShifterOperand(cls));
      // We speculatively set the result to false without changing the condition
      // flags, which allows us to avoid some branching later.
      __ mov(out, ShifterOperand(0), AL, kCcKeep);

      // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
      // we check that the output is in a low register, so that a 16-bit MOV
      // encoding can be used.
      if (ArmAssembler::IsLowRegister(out)) {
        __ it(EQ);
        __ mov(out, ShifterOperand(1), EQ);
      } else {
        __ b(final_label, NE);
        __ LoadImmediate(out, 1);
      }

      break;
    }

    case TypeCheckKind::kAbstractClassCheck: {
      // /* HeapReference<Class> */ out = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        out_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp_loc,
                                        kCompilerReadBarrierOption);
      // If the class is abstract, we eagerly fetch the super class of the
      // object to avoid doing a comparison we know will fail.
      Label loop;
      __ Bind(&loop);
      // /* HeapReference<Class> */ out = out->super_class_
      GenerateReferenceLoadOneRegister(instruction,
                                       out_loc,
                                       super_offset,
                                       maybe_temp_loc,
                                       kCompilerReadBarrierOption);
      // If `out` is null, we use it for the result, and jump to the final label.
      __ CompareAndBranchIfZero(out, final_label);
      __ cmp(out, ShifterOperand(cls));
      __ b(&loop, NE);
      __ LoadImmediate(out, 1);
      break;
    }

    case TypeCheckKind::kClassHierarchyCheck: {
      // /* HeapReference<Class> */ out = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        out_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp_loc,
                                        kCompilerReadBarrierOption);
      // Walk over the class hierarchy to find a match.
      Label loop, success;
      __ Bind(&loop);
      __ cmp(out, ShifterOperand(cls));
      __ b(&success, EQ);
      // /* HeapReference<Class> */ out = out->super_class_
      GenerateReferenceLoadOneRegister(instruction,
                                       out_loc,
                                       super_offset,
                                       maybe_temp_loc,
                                       kCompilerReadBarrierOption);
      // This is essentially a null check, but it sets the condition flags to the
      // proper value for the code that follows the loop, i.e. not `EQ`.
      __ cmp(out, ShifterOperand(1));
      __ b(&loop, HS);

      // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
      // we check that the output is in a low register, so that a 16-bit MOV
      // encoding can be used.
      if (ArmAssembler::IsLowRegister(out)) {
        // If `out` is null, we use it for the result, and the condition flags
        // have already been set to `NE`, so the IT block that comes afterwards
        // (and which handles the successful case) turns into a NOP (instead of
        // overwriting `out`).
        __ Bind(&success);
        // There is only one branch to the `success` label (which is bound to this
        // IT block), and it has the same condition, `EQ`, so in that case the MOV
        // is executed.
        __ it(EQ);
        __ mov(out, ShifterOperand(1), EQ);
      } else {
        // If `out` is null, we use it for the result, and jump to the final label.
        __ b(final_label);
        __ Bind(&success);
        __ LoadImmediate(out, 1);
      }

      break;
    }

    case TypeCheckKind::kArrayObjectCheck: {
      // /* HeapReference<Class> */ out = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        out_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp_loc,
                                        kCompilerReadBarrierOption);
      // Do an exact check.
      Label exact_check;
      __ cmp(out, ShifterOperand(cls));
      __ b(&exact_check, EQ);
      // Otherwise, we need to check that the object's class is a non-primitive array.
      // /* HeapReference<Class> */ out = out->component_type_
      GenerateReferenceLoadOneRegister(instruction,
                                       out_loc,
                                       component_offset,
                                       maybe_temp_loc,
                                       kCompilerReadBarrierOption);
      // If `out` is null, we use it for the result, and jump to the final label.
      __ CompareAndBranchIfZero(out, final_label);
      __ LoadFromOffset(kLoadUnsignedHalfword, out, out, primitive_offset);
      static_assert(Primitive::kPrimNot == 0, "Expected 0 for kPrimNot");
      __ cmp(out, ShifterOperand(0));
      // We speculatively set the result to false without changing the condition
      // flags, which allows us to avoid some branching later.
      __ mov(out, ShifterOperand(0), AL, kCcKeep);

      // Since IT blocks longer than a 16-bit instruction are deprecated by ARMv8,
      // we check that the output is in a low register, so that a 16-bit MOV
      // encoding can be used.
      if (ArmAssembler::IsLowRegister(out)) {
        __ Bind(&exact_check);
        __ it(EQ);
        __ mov(out, ShifterOperand(1), EQ);
      } else {
        __ b(final_label, NE);
        __ Bind(&exact_check);
        __ LoadImmediate(out, 1);
      }

      break;
    }

    case TypeCheckKind::kArrayCheck: {
      // No read barrier since the slow path will retry upon failure.
      // /* HeapReference<Class> */ out = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        out_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp_loc,
                                        kWithoutReadBarrier);
      __ cmp(out, ShifterOperand(cls));
      DCHECK(locations->OnlyCallsOnSlowPath());
      slow_path = new (GetGraph()->GetArena()) TypeCheckSlowPathARM(instruction,
                                                                    /* is_fatal */ false);
      codegen_->AddSlowPath(slow_path);
      __ b(slow_path->GetEntryLabel(), NE);
      __ LoadImmediate(out, 1);
      break;
    }

    case TypeCheckKind::kUnresolvedCheck:
    case TypeCheckKind::kInterfaceCheck: {
      // Note that we indeed only call on slow path, but we always go
      // into the slow path for the unresolved and interface check
      // cases.
      //
      // We cannot directly call the InstanceofNonTrivial runtime
      // entry point without resorting to a type checking slow path
      // here (i.e. by calling InvokeRuntime directly), as it would
      // require to assign fixed registers for the inputs of this
      // HInstanceOf instruction (following the runtime calling
      // convention), which might be cluttered by the potential first
      // read barrier emission at the beginning of this method.
      //
      // TODO: Introduce a new runtime entry point taking the object
      // to test (instead of its class) as argument, and let it deal
      // with the read barrier issues. This will let us refactor this
      // case of the `switch` code as it was previously (with a direct
      // call to the runtime not using a type checking slow path).
      // This should also be beneficial for the other cases above.
      DCHECK(locations->OnlyCallsOnSlowPath());
      slow_path = new (GetGraph()->GetArena()) TypeCheckSlowPathARM(instruction,
                                                                    /* is_fatal */ false);
      codegen_->AddSlowPath(slow_path);
      __ b(slow_path->GetEntryLabel());
      break;
    }
  }

  if (done.IsLinked()) {
    __ Bind(&done);
  }

  if (slow_path != nullptr) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void LocationsBuilderARM::VisitCheckCast(HCheckCast* instruction) {
  LocationSummary::CallKind call_kind = LocationSummary::kNoCall;
  bool throws_into_catch = instruction->CanThrowIntoCatchBlock();

  TypeCheckKind type_check_kind = instruction->GetTypeCheckKind();
  switch (type_check_kind) {
    case TypeCheckKind::kExactCheck:
    case TypeCheckKind::kAbstractClassCheck:
    case TypeCheckKind::kClassHierarchyCheck:
    case TypeCheckKind::kArrayObjectCheck:
      call_kind = (throws_into_catch || kEmitCompilerReadBarrier) ?
          LocationSummary::kCallOnSlowPath :
          LocationSummary::kNoCall;  // In fact, call on a fatal (non-returning) slow path.
      break;
    case TypeCheckKind::kArrayCheck:
    case TypeCheckKind::kUnresolvedCheck:
    case TypeCheckKind::kInterfaceCheck:
      call_kind = LocationSummary::kCallOnSlowPath;
      break;
  }

  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction, call_kind);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->AddRegisterTemps(NumberOfCheckCastTemps(type_check_kind));
}

void InstructionCodeGeneratorARM::VisitCheckCast(HCheckCast* instruction) {
  TypeCheckKind type_check_kind = instruction->GetTypeCheckKind();
  LocationSummary* locations = instruction->GetLocations();
  Location obj_loc = locations->InAt(0);
  Register obj = obj_loc.AsRegister<Register>();
  Register cls = locations->InAt(1).AsRegister<Register>();
  Location temp_loc = locations->GetTemp(0);
  Register temp = temp_loc.AsRegister<Register>();
  const size_t num_temps = NumberOfCheckCastTemps(type_check_kind);
  DCHECK_LE(num_temps, 3u);
  Location maybe_temp2_loc = (num_temps >= 2) ? locations->GetTemp(1) : Location::NoLocation();
  Location maybe_temp3_loc = (num_temps >= 3) ? locations->GetTemp(2) : Location::NoLocation();
  const uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  const uint32_t super_offset = mirror::Class::SuperClassOffset().Int32Value();
  const uint32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
  const uint32_t primitive_offset = mirror::Class::PrimitiveTypeOffset().Int32Value();
  const uint32_t iftable_offset = mirror::Class::IfTableOffset().Uint32Value();
  const uint32_t array_length_offset = mirror::Array::LengthOffset().Uint32Value();
  const uint32_t object_array_data_offset =
      mirror::Array::DataOffset(kHeapReferenceSize).Uint32Value();

  // Always false for read barriers since we may need to go to the entrypoint for non-fatal cases
  // from false negatives. The false negatives may come from avoiding read barriers below. Avoiding
  // read barriers is done for performance and code size reasons.
  bool is_type_check_slow_path_fatal = false;
  if (!kEmitCompilerReadBarrier) {
    is_type_check_slow_path_fatal =
        (type_check_kind == TypeCheckKind::kExactCheck ||
         type_check_kind == TypeCheckKind::kAbstractClassCheck ||
         type_check_kind == TypeCheckKind::kClassHierarchyCheck ||
         type_check_kind == TypeCheckKind::kArrayObjectCheck) &&
        !instruction->CanThrowIntoCatchBlock();
  }
  SlowPathCodeARM* type_check_slow_path =
      new (GetGraph()->GetArena()) TypeCheckSlowPathARM(instruction,
                                                        is_type_check_slow_path_fatal);
  codegen_->AddSlowPath(type_check_slow_path);

  Label done;
  Label* final_label = codegen_->GetFinalLabel(instruction, &done);
  // Avoid null check if we know obj is not null.
  if (instruction->MustDoNullCheck()) {
    __ CompareAndBranchIfZero(obj, final_label);
  }

  switch (type_check_kind) {
    case TypeCheckKind::kExactCheck:
    case TypeCheckKind::kArrayCheck: {
      // /* HeapReference<Class> */ temp = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);

      __ cmp(temp, ShifterOperand(cls));
      // Jump to slow path for throwing the exception or doing a
      // more involved array check.
      __ b(type_check_slow_path->GetEntryLabel(), NE);
      break;
    }

    case TypeCheckKind::kAbstractClassCheck: {
      // /* HeapReference<Class> */ temp = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);

      // If the class is abstract, we eagerly fetch the super class of the
      // object to avoid doing a comparison we know will fail.
      Label loop;
      __ Bind(&loop);
      // /* HeapReference<Class> */ temp = temp->super_class_
      GenerateReferenceLoadOneRegister(instruction,
                                       temp_loc,
                                       super_offset,
                                       maybe_temp2_loc,
                                       kWithoutReadBarrier);

      // If the class reference currently in `temp` is null, jump to the slow path to throw the
      // exception.
      __ CompareAndBranchIfZero(temp, type_check_slow_path->GetEntryLabel());

      // Otherwise, compare the classes.
      __ cmp(temp, ShifterOperand(cls));
      __ b(&loop, NE);
      break;
    }

    case TypeCheckKind::kClassHierarchyCheck: {
      // /* HeapReference<Class> */ temp = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);

      // Walk over the class hierarchy to find a match.
      Label loop;
      __ Bind(&loop);
      __ cmp(temp, ShifterOperand(cls));
      __ b(final_label, EQ);

      // /* HeapReference<Class> */ temp = temp->super_class_
      GenerateReferenceLoadOneRegister(instruction,
                                       temp_loc,
                                       super_offset,
                                       maybe_temp2_loc,
                                       kWithoutReadBarrier);

      // If the class reference currently in `temp` is null, jump to the slow path to throw the
      // exception.
      __ CompareAndBranchIfZero(temp, type_check_slow_path->GetEntryLabel());
      // Otherwise, jump to the beginning of the loop.
      __ b(&loop);
      break;
    }

    case TypeCheckKind::kArrayObjectCheck: {
      // /* HeapReference<Class> */ temp = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);

      // Do an exact check.
      __ cmp(temp, ShifterOperand(cls));
      __ b(final_label, EQ);

      // Otherwise, we need to check that the object's class is a non-primitive array.
      // /* HeapReference<Class> */ temp = temp->component_type_
      GenerateReferenceLoadOneRegister(instruction,
                                       temp_loc,
                                       component_offset,
                                       maybe_temp2_loc,
                                       kWithoutReadBarrier);
      // If the component type is null, jump to the slow path to throw the exception.
      __ CompareAndBranchIfZero(temp, type_check_slow_path->GetEntryLabel());
      // Otherwise,the object is indeed an array, jump to label `check_non_primitive_component_type`
      // to further check that this component type is not a primitive type.
      __ LoadFromOffset(kLoadUnsignedHalfword, temp, temp, primitive_offset);
      static_assert(Primitive::kPrimNot == 0, "Expected 0 for art::Primitive::kPrimNot");
      __ CompareAndBranchIfNonZero(temp, type_check_slow_path->GetEntryLabel());
      break;
    }

    case TypeCheckKind::kUnresolvedCheck:
      // We always go into the type check slow path for the unresolved check case.
      // We cannot directly call the CheckCast runtime entry point
      // without resorting to a type checking slow path here (i.e. by
      // calling InvokeRuntime directly), as it would require to
      // assign fixed registers for the inputs of this HInstanceOf
      // instruction (following the runtime calling convention), which
      // might be cluttered by the potential first read barrier
      // emission at the beginning of this method.

      __ b(type_check_slow_path->GetEntryLabel());
      break;

    case TypeCheckKind::kInterfaceCheck: {
      // Avoid read barriers to improve performance of the fast path. We can not get false
      // positives by doing this.
      // /* HeapReference<Class> */ temp = obj->klass_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        obj_loc,
                                        class_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);

      // /* HeapReference<Class> */ temp = temp->iftable_
      GenerateReferenceLoadTwoRegisters(instruction,
                                        temp_loc,
                                        temp_loc,
                                        iftable_offset,
                                        maybe_temp2_loc,
                                        kWithoutReadBarrier);
      // Iftable is never null.
      __ ldr(maybe_temp2_loc.AsRegister<Register>(), Address(temp, array_length_offset));
      // Loop through the iftable and check if any class matches.
      Label start_loop;
      __ Bind(&start_loop);
      __ CompareAndBranchIfZero(maybe_temp2_loc.AsRegister<Register>(),
                                type_check_slow_path->GetEntryLabel());
      __ ldr(maybe_temp3_loc.AsRegister<Register>(), Address(temp, object_array_data_offset));
      __ MaybeUnpoisonHeapReference(maybe_temp3_loc.AsRegister<Register>());
      // Go to next interface.
      __ add(temp, temp, ShifterOperand(2 * kHeapReferenceSize));
      __ sub(maybe_temp2_loc.AsRegister<Register>(),
             maybe_temp2_loc.AsRegister<Register>(),
             ShifterOperand(2));
      // Compare the classes and continue the loop if they do not match.
      __ cmp(cls, ShifterOperand(maybe_temp3_loc.AsRegister<Register>()));
      __ b(&start_loop, NE);
      break;
    }
  }

  if (done.IsLinked()) {
    __ Bind(&done);
  }

  __ Bind(type_check_slow_path->GetExitLabel());
}

void LocationsBuilderARM::VisitMonitorOperation(HMonitorOperation* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
}

void InstructionCodeGeneratorARM::VisitMonitorOperation(HMonitorOperation* instruction) {
  codegen_->InvokeRuntime(instruction->IsEnter() ? kQuickLockObject : kQuickUnlockObject,
                          instruction,
                          instruction->GetDexPc());
  if (instruction->IsEnter()) {
    CheckEntrypointTypes<kQuickLockObject, void, mirror::Object*>();
  } else {
    CheckEntrypointTypes<kQuickUnlockObject, void, mirror::Object*>();
  }
}

void LocationsBuilderARM::VisitAnd(HAnd* instruction) { HandleBitwiseOperation(instruction, AND); }
void LocationsBuilderARM::VisitOr(HOr* instruction) { HandleBitwiseOperation(instruction, ORR); }
void LocationsBuilderARM::VisitXor(HXor* instruction) { HandleBitwiseOperation(instruction, EOR); }

void LocationsBuilderARM::HandleBitwiseOperation(HBinaryOperation* instruction, Opcode opcode) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt
         || instruction->GetResultType() == Primitive::kPrimLong);
  // Note: GVN reorders commutative operations to have the constant on the right hand side.
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, ArmEncodableConstantOrRegister(instruction->InputAt(1), opcode));
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitAnd(HAnd* instruction) {
  HandleBitwiseOperation(instruction);
}

void InstructionCodeGeneratorARM::VisitOr(HOr* instruction) {
  HandleBitwiseOperation(instruction);
}

void InstructionCodeGeneratorARM::VisitXor(HXor* instruction) {
  HandleBitwiseOperation(instruction);
}


void LocationsBuilderARM::VisitBitwiseNegatedRight(HBitwiseNegatedRight* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt
         || instruction->GetResultType() == Primitive::kPrimLong);

  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitBitwiseNegatedRight(HBitwiseNegatedRight* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  Location out = locations->Out();

  if (instruction->GetResultType() == Primitive::kPrimInt) {
    Register first_reg = first.AsRegister<Register>();
    ShifterOperand second_reg(second.AsRegister<Register>());
    Register out_reg = out.AsRegister<Register>();

    switch (instruction->GetOpKind()) {
      case HInstruction::kAnd:
        __ bic(out_reg, first_reg, second_reg);
        break;
      case HInstruction::kOr:
        __ orn(out_reg, first_reg, second_reg);
        break;
      // There is no EON on arm.
      case HInstruction::kXor:
      default:
        LOG(FATAL) << "Unexpected instruction " << instruction->DebugName();
        UNREACHABLE();
    }
    return;

  } else {
    DCHECK_EQ(instruction->GetResultType(), Primitive::kPrimLong);
    Register first_low = first.AsRegisterPairLow<Register>();
    Register first_high = first.AsRegisterPairHigh<Register>();
    ShifterOperand second_low(second.AsRegisterPairLow<Register>());
    ShifterOperand second_high(second.AsRegisterPairHigh<Register>());
    Register out_low = out.AsRegisterPairLow<Register>();
    Register out_high = out.AsRegisterPairHigh<Register>();

    switch (instruction->GetOpKind()) {
      case HInstruction::kAnd:
        __ bic(out_low, first_low, second_low);
        __ bic(out_high, first_high, second_high);
        break;
      case HInstruction::kOr:
        __ orn(out_low, first_low, second_low);
        __ orn(out_high, first_high, second_high);
        break;
      // There is no EON on arm.
      case HInstruction::kXor:
      default:
        LOG(FATAL) << "Unexpected instruction " << instruction->DebugName();
        UNREACHABLE();
    }
  }
}

void LocationsBuilderARM::VisitDataProcWithShifterOp(
    HDataProcWithShifterOp* instruction) {
  DCHECK(instruction->GetType() == Primitive::kPrimInt ||
         instruction->GetType() == Primitive::kPrimLong);
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  const bool overlap = instruction->GetType() == Primitive::kPrimLong &&
                       HDataProcWithShifterOp::IsExtensionOp(instruction->GetOpKind());

  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(),
                    overlap ? Location::kOutputOverlap : Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitDataProcWithShifterOp(
    HDataProcWithShifterOp* instruction) {
  const LocationSummary* const locations = instruction->GetLocations();
  const HInstruction::InstructionKind kind = instruction->GetInstrKind();
  const HDataProcWithShifterOp::OpKind op_kind = instruction->GetOpKind();
  const Location left = locations->InAt(0);
  const Location right = locations->InAt(1);
  const Location out = locations->Out();

  if (instruction->GetType() == Primitive::kPrimInt) {
    DCHECK(!HDataProcWithShifterOp::IsExtensionOp(op_kind));

    const Register second = instruction->InputAt(1)->GetType() == Primitive::kPrimLong
        ? right.AsRegisterPairLow<Register>()
        : right.AsRegister<Register>();

    GenerateDataProcInstruction(kind,
                                out.AsRegister<Register>(),
                                left.AsRegister<Register>(),
                                ShifterOperand(second,
                                               ShiftFromOpKind(op_kind),
                                               instruction->GetShiftAmount()),
                                codegen_);
  } else {
    DCHECK_EQ(instruction->GetType(), Primitive::kPrimLong);

    if (HDataProcWithShifterOp::IsExtensionOp(op_kind)) {
      const Register second = right.AsRegister<Register>();

      DCHECK_NE(out.AsRegisterPairLow<Register>(), second);
      GenerateDataProc(kind,
                       out,
                       left,
                       ShifterOperand(second),
                       ShifterOperand(second, ASR, 31),
                       codegen_);
    } else {
      GenerateLongDataProc(instruction, codegen_);
    }
  }
}

void InstructionCodeGeneratorARM::GenerateAndConst(Register out, Register first, uint32_t value) {
  // Optimize special cases for individual halfs of `and-long` (`and` is simplified earlier).
  if (value == 0xffffffffu) {
    if (out != first) {
      __ mov(out, ShifterOperand(first));
    }
    return;
  }
  if (value == 0u) {
    __ mov(out, ShifterOperand(0));
    return;
  }
  ShifterOperand so;
  if (__ ShifterOperandCanHold(kNoRegister, kNoRegister, AND, value, &so)) {
    __ and_(out, first, so);
  } else if (__ ShifterOperandCanHold(kNoRegister, kNoRegister, BIC, ~value, &so)) {
    __ bic(out, first, ShifterOperand(~value));
  } else {
    DCHECK(IsPowerOfTwo(value + 1));
    __ ubfx(out, first, 0, WhichPowerOf2(value + 1));
  }
}

void InstructionCodeGeneratorARM::GenerateOrrConst(Register out, Register first, uint32_t value) {
  // Optimize special cases for individual halfs of `or-long` (`or` is simplified earlier).
  if (value == 0u) {
    if (out != first) {
      __ mov(out, ShifterOperand(first));
    }
    return;
  }
  if (value == 0xffffffffu) {
    __ mvn(out, ShifterOperand(0));
    return;
  }
  ShifterOperand so;
  if (__ ShifterOperandCanHold(kNoRegister, kNoRegister, ORR, value, &so)) {
    __ orr(out, first, so);
  } else {
    DCHECK(__ ShifterOperandCanHold(kNoRegister, kNoRegister, ORN, ~value, &so));
    __ orn(out, first, ShifterOperand(~value));
  }
}

void InstructionCodeGeneratorARM::GenerateEorConst(Register out, Register first, uint32_t value) {
  // Optimize special case for individual halfs of `xor-long` (`xor` is simplified earlier).
  if (value == 0u) {
    if (out != first) {
      __ mov(out, ShifterOperand(first));
    }
    return;
  }
  __ eor(out, first, ShifterOperand(value));
}

void InstructionCodeGeneratorARM::GenerateAddLongConst(Location out,
                                                       Location first,
                                                       uint64_t value) {
  Register out_low = out.AsRegisterPairLow<Register>();
  Register out_high = out.AsRegisterPairHigh<Register>();
  Register first_low = first.AsRegisterPairLow<Register>();
  Register first_high = first.AsRegisterPairHigh<Register>();
  uint32_t value_low = Low32Bits(value);
  uint32_t value_high = High32Bits(value);
  if (value_low == 0u) {
    if (out_low != first_low) {
      __ mov(out_low, ShifterOperand(first_low));
    }
    __ AddConstant(out_high, first_high, value_high);
    return;
  }
  __ AddConstantSetFlags(out_low, first_low, value_low);
  ShifterOperand so;
  if (__ ShifterOperandCanHold(out_high, first_high, ADC, value_high, kCcDontCare, &so)) {
    __ adc(out_high, first_high, so);
  } else if (__ ShifterOperandCanHold(out_low, first_low, SBC, ~value_high, kCcDontCare, &so)) {
    __ sbc(out_high, first_high, so);
  } else {
    LOG(FATAL) << "Unexpected constant " << value_high;
    UNREACHABLE();
  }
}

void InstructionCodeGeneratorARM::HandleBitwiseOperation(HBinaryOperation* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  Location out = locations->Out();

  if (second.IsConstant()) {
    uint64_t value = static_cast<uint64_t>(Int64FromConstant(second.GetConstant()));
    uint32_t value_low = Low32Bits(value);
    if (instruction->GetResultType() == Primitive::kPrimInt) {
      Register first_reg = first.AsRegister<Register>();
      Register out_reg = out.AsRegister<Register>();
      if (instruction->IsAnd()) {
        GenerateAndConst(out_reg, first_reg, value_low);
      } else if (instruction->IsOr()) {
        GenerateOrrConst(out_reg, first_reg, value_low);
      } else {
        DCHECK(instruction->IsXor());
        GenerateEorConst(out_reg, first_reg, value_low);
      }
    } else {
      DCHECK_EQ(instruction->GetResultType(), Primitive::kPrimLong);
      uint32_t value_high = High32Bits(value);
      Register first_low = first.AsRegisterPairLow<Register>();
      Register first_high = first.AsRegisterPairHigh<Register>();
      Register out_low = out.AsRegisterPairLow<Register>();
      Register out_high = out.AsRegisterPairHigh<Register>();
      if (instruction->IsAnd()) {
        GenerateAndConst(out_low, first_low, value_low);
        GenerateAndConst(out_high, first_high, value_high);
      } else if (instruction->IsOr()) {
        GenerateOrrConst(out_low, first_low, value_low);
        GenerateOrrConst(out_high, first_high, value_high);
      } else {
        DCHECK(instruction->IsXor());
        GenerateEorConst(out_low, first_low, value_low);
        GenerateEorConst(out_high, first_high, value_high);
      }
    }
    return;
  }

  if (instruction->GetResultType() == Primitive::kPrimInt) {
    Register first_reg = first.AsRegister<Register>();
    ShifterOperand second_reg(second.AsRegister<Register>());
    Register out_reg = out.AsRegister<Register>();
    if (instruction->IsAnd()) {
      __ and_(out_reg, first_reg, second_reg);
    } else if (instruction->IsOr()) {
      __ orr(out_reg, first_reg, second_reg);
    } else {
      DCHECK(instruction->IsXor());
      __ eor(out_reg, first_reg, second_reg);
    }
  } else {
    DCHECK_EQ(instruction->GetResultType(), Primitive::kPrimLong);
    Register first_low = first.AsRegisterPairLow<Register>();
    Register first_high = first.AsRegisterPairHigh<Register>();
    ShifterOperand second_low(second.AsRegisterPairLow<Register>());
    ShifterOperand second_high(second.AsRegisterPairHigh<Register>());
    Register out_low = out.AsRegisterPairLow<Register>();
    Register out_high = out.AsRegisterPairHigh<Register>();
    if (instruction->IsAnd()) {
      __ and_(out_low, first_low, second_low);
      __ and_(out_high, first_high, second_high);
    } else if (instruction->IsOr()) {
      __ orr(out_low, first_low, second_low);
      __ orr(out_high, first_high, second_high);
    } else {
      DCHECK(instruction->IsXor());
      __ eor(out_low, first_low, second_low);
      __ eor(out_high, first_high, second_high);
    }
  }
}

void InstructionCodeGeneratorARM::GenerateReferenceLoadOneRegister(
    HInstruction* instruction,
    Location out,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  Register out_reg = out.AsRegister<Register>();
  if (read_barrier_option == kWithReadBarrier) {
    CHECK(kEmitCompilerReadBarrier);
    DCHECK(maybe_temp.IsRegister()) << maybe_temp;
    if (kUseBakerReadBarrier) {
      // Load with fast path based Baker's read barrier.
      // /* HeapReference<Object> */ out = *(out + offset)
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          instruction, out, out_reg, offset, maybe_temp, /* needs_null_check */ false);
    } else {
      // Load with slow path based read barrier.
      // Save the value of `out` into `maybe_temp` before overwriting it
      // in the following move operation, as we will need it for the
      // read barrier below.
      __ Mov(maybe_temp.AsRegister<Register>(), out_reg);
      // /* HeapReference<Object> */ out = *(out + offset)
      __ LoadFromOffset(kLoadWord, out_reg, out_reg, offset);
      codegen_->GenerateReadBarrierSlow(instruction, out, out, maybe_temp, offset);
    }
  } else {
    // Plain load with no read barrier.
    // /* HeapReference<Object> */ out = *(out + offset)
    __ LoadFromOffset(kLoadWord, out_reg, out_reg, offset);
    __ MaybeUnpoisonHeapReference(out_reg);
  }
}

void InstructionCodeGeneratorARM::GenerateReferenceLoadTwoRegisters(
    HInstruction* instruction,
    Location out,
    Location obj,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  Register out_reg = out.AsRegister<Register>();
  Register obj_reg = obj.AsRegister<Register>();
  if (read_barrier_option == kWithReadBarrier) {
    CHECK(kEmitCompilerReadBarrier);
    if (kUseBakerReadBarrier) {
      DCHECK(maybe_temp.IsRegister()) << maybe_temp;
      // Load with fast path based Baker's read barrier.
      // /* HeapReference<Object> */ out = *(obj + offset)
      codegen_->GenerateFieldLoadWithBakerReadBarrier(
          instruction, out, obj_reg, offset, maybe_temp, /* needs_null_check */ false);
    } else {
      // Load with slow path based read barrier.
      // /* HeapReference<Object> */ out = *(obj + offset)
      __ LoadFromOffset(kLoadWord, out_reg, obj_reg, offset);
      codegen_->GenerateReadBarrierSlow(instruction, out, out, obj, offset);
    }
  } else {
    // Plain load with no read barrier.
    // /* HeapReference<Object> */ out = *(obj + offset)
    __ LoadFromOffset(kLoadWord, out_reg, obj_reg, offset);
    __ MaybeUnpoisonHeapReference(out_reg);
  }
}

void InstructionCodeGeneratorARM::GenerateGcRootFieldLoad(HInstruction* instruction,
                                                          Location root,
                                                          Register obj,
                                                          uint32_t offset,
                                                          ReadBarrierOption read_barrier_option) {
  Register root_reg = root.AsRegister<Register>();
  if (read_barrier_option == kWithReadBarrier) {
    DCHECK(kEmitCompilerReadBarrier);
    if (kUseBakerReadBarrier) {
      // Fast path implementation of art::ReadBarrier::BarrierForRoot when
      // Baker's read barrier are used.
      //
      // Note that we do not actually check the value of
      // `GetIsGcMarking()` to decide whether to mark the loaded GC
      // root or not.  Instead, we load into `temp` the read barrier
      // mark entry point corresponding to register `root`. If `temp`
      // is null, it means that `GetIsGcMarking()` is false, and vice
      // versa.
      //
      //   temp = Thread::Current()->pReadBarrierMarkReg ## root.reg()
      //   GcRoot<mirror::Object> root = *(obj+offset);  // Original reference load.
      //   if (temp != nullptr) {  // <=> Thread::Current()->GetIsGcMarking()
      //     // Slow path.
      //     root = temp(root);  // root = ReadBarrier::Mark(root);  // Runtime entry point call.
      //   }

      // Slow path marking the GC root `root`. The entrypoint will already be loaded in `temp`.
      Location temp = Location::RegisterLocation(LR);
      SlowPathCodeARM* slow_path = new (GetGraph()->GetArena()) ReadBarrierMarkSlowPathARM(
          instruction, root, /* entrypoint */ temp);
      codegen_->AddSlowPath(slow_path);

      // temp = Thread::Current()->pReadBarrierMarkReg ## root.reg()
      const int32_t entry_point_offset =
          CodeGenerator::GetReadBarrierMarkEntryPointsOffset<kArmPointerSize>(root.reg());
      // Loading the entrypoint does not require a load acquire since it is only changed when
      // threads are suspended or running a checkpoint.
      __ LoadFromOffset(kLoadWord, temp.AsRegister<Register>(), TR, entry_point_offset);

      // /* GcRoot<mirror::Object> */ root = *(obj + offset)
      __ LoadFromOffset(kLoadWord, root_reg, obj, offset);
      static_assert(
          sizeof(mirror::CompressedReference<mirror::Object>) == sizeof(GcRoot<mirror::Object>),
          "art::mirror::CompressedReference<mirror::Object> and art::GcRoot<mirror::Object> "
          "have different sizes.");
      static_assert(sizeof(mirror::CompressedReference<mirror::Object>) == sizeof(int32_t),
                    "art::mirror::CompressedReference<mirror::Object> and int32_t "
                    "have different sizes.");

      // The entrypoint is null when the GC is not marking, this prevents one load compared to
      // checking GetIsGcMarking.
      __ CompareAndBranchIfNonZero(temp.AsRegister<Register>(), slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
    } else {
      // GC root loaded through a slow path for read barriers other
      // than Baker's.
      // /* GcRoot<mirror::Object>* */ root = obj + offset
      __ AddConstant(root_reg, obj, offset);
      // /* mirror::Object* */ root = root->Read()
      codegen_->GenerateReadBarrierForRootSlow(instruction, root, root);
    }
  } else {
    // Plain GC root load with no read barrier.
    // /* GcRoot<mirror::Object> */ root = *(obj + offset)
    __ LoadFromOffset(kLoadWord, root_reg, obj, offset);
    // Note that GC roots are not affected by heap poisoning, thus we
    // do not have to unpoison `root_reg` here.
  }
}

void CodeGeneratorARM::GenerateFieldLoadWithBakerReadBarrier(HInstruction* instruction,
                                                             Location ref,
                                                             Register obj,
                                                             uint32_t offset,
                                                             Location temp,
                                                             bool needs_null_check) {
  DCHECK(kEmitCompilerReadBarrier);
  DCHECK(kUseBakerReadBarrier);

  // /* HeapReference<Object> */ ref = *(obj + offset)
  Location no_index = Location::NoLocation();
  ScaleFactor no_scale_factor = TIMES_1;
  GenerateReferenceLoadWithBakerReadBarrier(
      instruction, ref, obj, offset, no_index, no_scale_factor, temp, needs_null_check);
}

void CodeGeneratorARM::GenerateArrayLoadWithBakerReadBarrier(HInstruction* instruction,
                                                             Location ref,
                                                             Register obj,
                                                             uint32_t data_offset,
                                                             Location index,
                                                             Location temp,
                                                             bool needs_null_check) {
  DCHECK(kEmitCompilerReadBarrier);
  DCHECK(kUseBakerReadBarrier);

  static_assert(
      sizeof(mirror::HeapReference<mirror::Object>) == sizeof(int32_t),
      "art::mirror::HeapReference<art::mirror::Object> and int32_t have different sizes.");
  // /* HeapReference<Object> */ ref =
  //     *(obj + data_offset + index * sizeof(HeapReference<Object>))
  ScaleFactor scale_factor = TIMES_4;
  GenerateReferenceLoadWithBakerReadBarrier(
      instruction, ref, obj, data_offset, index, scale_factor, temp, needs_null_check);
}

void CodeGeneratorARM::GenerateReferenceLoadWithBakerReadBarrier(HInstruction* instruction,
                                                                 Location ref,
                                                                 Register obj,
                                                                 uint32_t offset,
                                                                 Location index,
                                                                 ScaleFactor scale_factor,
                                                                 Location temp,
                                                                 bool needs_null_check,
                                                                 bool always_update_field,
                                                                 Register* temp2) {
  DCHECK(kEmitCompilerReadBarrier);
  DCHECK(kUseBakerReadBarrier);

  // Query `art::Thread::Current()->GetIsGcMarking()` to decide
  // whether we need to enter the slow path to mark the reference.
  // Then, in the slow path, check the gray bit in the lock word of
  // the reference's holder (`obj`) to decide whether to mark `ref` or
  // not.
  //
  // Note that we do not actually check the value of `GetIsGcMarking()`;
  // instead, we load into `temp3` the read barrier mark entry point
  // corresponding to register `ref`. If `temp3` is null, it means
  // that `GetIsGcMarking()` is false, and vice versa.
  //
  //   temp3 = Thread::Current()->pReadBarrierMarkReg ## root.reg()
  //   if (temp3 != nullptr) {  // <=> Thread::Current()->GetIsGcMarking()
  //     // Slow path.
  //     uint32_t rb_state = Lockword(obj->monitor_).ReadBarrierState();
  //     lfence;  // Load fence or artificial data dependency to prevent load-load reordering
  //     HeapReference<mirror::Object> ref = *src;  // Original reference load.
  //     bool is_gray = (rb_state == ReadBarrier::GrayState());
  //     if (is_gray) {
  //       ref = temp3(ref);  // ref = ReadBarrier::Mark(ref);  // Runtime entry point call.
  //     }
  //   } else {
  //     HeapReference<mirror::Object> ref = *src;  // Original reference load.
  //   }

  Register temp_reg = temp.AsRegister<Register>();

  // Slow path marking the object `ref` when the GC is marking. The
  // entrypoint will already be loaded in `temp3`.
  Location temp3 = Location::RegisterLocation(LR);
  SlowPathCodeARM* slow_path;
  if (always_update_field) {
    DCHECK(temp2 != nullptr);
    // LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM only
    // supports address of the form `obj + field_offset`, where `obj`
    // is a register and `field_offset` is a register pair (of which
    // only the lower half is used). Thus `offset` and `scale_factor`
    // above are expected to be null in this code path.
    DCHECK_EQ(offset, 0u);
    DCHECK_EQ(scale_factor, ScaleFactor::TIMES_1);
    Location field_offset = index;
    slow_path =
        new (GetGraph()->GetArena()) LoadReferenceWithBakerReadBarrierAndUpdateFieldSlowPathARM(
            instruction,
            ref,
            obj,
            offset,
            /* index */ field_offset,
            scale_factor,
            needs_null_check,
            temp_reg,
            *temp2,
            /* entrypoint */ temp3);
  } else {
    slow_path = new (GetGraph()->GetArena()) LoadReferenceWithBakerReadBarrierSlowPathARM(
        instruction,
        ref,
        obj,
        offset,
        index,
        scale_factor,
        needs_null_check,
        temp_reg,
        /* entrypoint */ temp3);
  }
  AddSlowPath(slow_path);

  // temp3 = Thread::Current()->pReadBarrierMarkReg ## ref.reg()
  const int32_t entry_point_offset =
      CodeGenerator::GetReadBarrierMarkEntryPointsOffset<kArmPointerSize>(ref.reg());
  // Loading the entrypoint does not require a load acquire since it is only changed when
  // threads are suspended or running a checkpoint.
  __ LoadFromOffset(kLoadWord, temp3.AsRegister<Register>(), TR, entry_point_offset);
  // The entrypoint is null when the GC is not marking, this prevents one load compared to
  // checking GetIsGcMarking.
  __ CompareAndBranchIfNonZero(temp3.AsRegister<Register>(), slow_path->GetEntryLabel());
  // Fast path: just load the reference.
  GenerateRawReferenceLoad(instruction, ref, obj, offset, index, scale_factor, needs_null_check);
  __ Bind(slow_path->GetExitLabel());
}

void CodeGeneratorARM::GenerateRawReferenceLoad(HInstruction* instruction,
                                                Location ref,
                                                Register obj,
                                                uint32_t offset,
                                                Location index,
                                                ScaleFactor scale_factor,
                                                bool needs_null_check) {
  Register ref_reg = ref.AsRegister<Register>();

  if (index.IsValid()) {
    // Load types involving an "index": ArrayGet,
    // UnsafeGetObject/UnsafeGetObjectVolatile and UnsafeCASObject
    // intrinsics.
    // /* HeapReference<mirror::Object> */ ref = *(obj + offset + (index << scale_factor))
    if (index.IsConstant()) {
      size_t computed_offset =
          (index.GetConstant()->AsIntConstant()->GetValue() << scale_factor) + offset;
      __ LoadFromOffset(kLoadWord, ref_reg, obj, computed_offset);
    } else {
      // Handle the special case of the
      // UnsafeGetObject/UnsafeGetObjectVolatile and UnsafeCASObject
      // intrinsics, which use a register pair as index ("long
      // offset"), of which only the low part contains data.
      Register index_reg = index.IsRegisterPair()
          ? index.AsRegisterPairLow<Register>()
          : index.AsRegister<Register>();
      __ add(IP, obj, ShifterOperand(index_reg, LSL, scale_factor));
      __ LoadFromOffset(kLoadWord, ref_reg, IP, offset);
    }
  } else {
    // /* HeapReference<mirror::Object> */ ref = *(obj + offset)
    __ LoadFromOffset(kLoadWord, ref_reg, obj, offset);
  }

  if (needs_null_check) {
    MaybeRecordImplicitNullCheck(instruction);
  }

  // Object* ref = ref_addr->AsMirrorPtr()
  __ MaybeUnpoisonHeapReference(ref_reg);
}

void CodeGeneratorARM::GenerateReadBarrierSlow(HInstruction* instruction,
                                               Location out,
                                               Location ref,
                                               Location obj,
                                               uint32_t offset,
                                               Location index) {
  DCHECK(kEmitCompilerReadBarrier);

  // Insert a slow path based read barrier *after* the reference load.
  //
  // If heap poisoning is enabled, the unpoisoning of the loaded
  // reference will be carried out by the runtime within the slow
  // path.
  //
  // Note that `ref` currently does not get unpoisoned (when heap
  // poisoning is enabled), which is alright as the `ref` argument is
  // not used by the artReadBarrierSlow entry point.
  //
  // TODO: Unpoison `ref` when it is used by artReadBarrierSlow.
  SlowPathCodeARM* slow_path = new (GetGraph()->GetArena())
      ReadBarrierForHeapReferenceSlowPathARM(instruction, out, ref, obj, offset, index);
  AddSlowPath(slow_path);

  __ b(slow_path->GetEntryLabel());
  __ Bind(slow_path->GetExitLabel());
}

void CodeGeneratorARM::MaybeGenerateReadBarrierSlow(HInstruction* instruction,
                                                    Location out,
                                                    Location ref,
                                                    Location obj,
                                                    uint32_t offset,
                                                    Location index) {
  if (kEmitCompilerReadBarrier) {
    // Baker's read barriers shall be handled by the fast path
    // (CodeGeneratorARM::GenerateReferenceLoadWithBakerReadBarrier).
    DCHECK(!kUseBakerReadBarrier);
    // If heap poisoning is enabled, unpoisoning will be taken care of
    // by the runtime within the slow path.
    GenerateReadBarrierSlow(instruction, out, ref, obj, offset, index);
  } else if (kPoisonHeapReferences) {
    __ UnpoisonHeapReference(out.AsRegister<Register>());
  }
}

void CodeGeneratorARM::GenerateReadBarrierForRootSlow(HInstruction* instruction,
                                                      Location out,
                                                      Location root) {
  DCHECK(kEmitCompilerReadBarrier);

  // Insert a slow path based read barrier *after* the GC root load.
  //
  // Note that GC roots are not affected by heap poisoning, so we do
  // not need to do anything special for this here.
  SlowPathCodeARM* slow_path =
      new (GetGraph()->GetArena()) ReadBarrierForRootSlowPathARM(instruction, out, root);
  AddSlowPath(slow_path);

  __ b(slow_path->GetEntryLabel());
  __ Bind(slow_path->GetExitLabel());
}

HInvokeStaticOrDirect::DispatchInfo CodeGeneratorARM::GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info,
      HInvokeStaticOrDirect* invoke ATTRIBUTE_UNUSED) {
  return desired_dispatch_info;
}

Register CodeGeneratorARM::GetInvokeStaticOrDirectExtraParameter(HInvokeStaticOrDirect* invoke,
                                                                 Register temp) {
  DCHECK_EQ(invoke->InputCount(), invoke->GetNumberOfArguments() + 1u);
  Location location = invoke->GetLocations()->InAt(invoke->GetSpecialInputIndex());
  if (!invoke->GetLocations()->Intrinsified()) {
    return location.AsRegister<Register>();
  }
  // For intrinsics we allow any location, so it may be on the stack.
  if (!location.IsRegister()) {
    __ LoadFromOffset(kLoadWord, temp, SP, location.GetStackIndex());
    return temp;
  }
  // For register locations, check if the register was saved. If so, get it from the stack.
  // Note: There is a chance that the register was saved but not overwritten, so we could
  // save one load. However, since this is just an intrinsic slow path we prefer this
  // simple and more robust approach rather that trying to determine if that's the case.
  SlowPathCode* slow_path = GetCurrentSlowPath();
  if (slow_path != nullptr && slow_path->IsCoreRegisterSaved(location.AsRegister<Register>())) {
    int stack_offset = slow_path->GetStackOffsetOfCoreRegister(location.AsRegister<Register>());
    __ LoadFromOffset(kLoadWord, temp, SP, stack_offset);
    return temp;
  }
  return location.AsRegister<Register>();
}

Location CodeGeneratorARM::GenerateCalleeMethodStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                                                  Location temp) {
  Location callee_method = temp;  // For all kinds except kRecursive, callee will be in temp.
  switch (invoke->GetMethodLoadKind()) {
    case HInvokeStaticOrDirect::MethodLoadKind::kStringInit: {
      uint32_t offset =
          GetThreadOffset<kArmPointerSize>(invoke->GetStringInitEntryPoint()).Int32Value();
      // temp = thread->string_init_entrypoint
      __ LoadFromOffset(kLoadWord, temp.AsRegister<Register>(), TR, offset);
      break;
    }
    case HInvokeStaticOrDirect::MethodLoadKind::kRecursive:
      callee_method = invoke->GetLocations()->InAt(invoke->GetSpecialInputIndex());
      break;
    case HInvokeStaticOrDirect::MethodLoadKind::kDirectAddress:
      __ LoadImmediate(temp.AsRegister<Register>(), invoke->GetMethodAddress());
      break;
    case HInvokeStaticOrDirect::MethodLoadKind::kDexCachePcRelative: {
      HArmDexCacheArraysBase* base =
          invoke->InputAt(invoke->GetSpecialInputIndex())->AsArmDexCacheArraysBase();
      Register base_reg = GetInvokeStaticOrDirectExtraParameter(invoke,
                                                                temp.AsRegister<Register>());
      int32_t offset = invoke->GetDexCacheArrayOffset() - base->GetElementOffset();
      __ LoadFromOffset(kLoadWord, temp.AsRegister<Register>(), base_reg, offset);
      break;
    }
    case HInvokeStaticOrDirect::MethodLoadKind::kDexCacheViaMethod: {
      Location current_method = invoke->GetLocations()->InAt(invoke->GetSpecialInputIndex());
      Register method_reg;
      Register reg = temp.AsRegister<Register>();
      if (current_method.IsRegister()) {
        method_reg = current_method.AsRegister<Register>();
      } else {
        DCHECK(invoke->GetLocations()->Intrinsified());
        DCHECK(!current_method.IsValid());
        method_reg = reg;
        __ LoadFromOffset(kLoadWord, reg, SP, kCurrentMethodStackOffset);
      }
      // /* ArtMethod*[] */ temp = temp.ptr_sized_fields_->dex_cache_resolved_methods_;
      __ LoadFromOffset(kLoadWord,
                        reg,
                        method_reg,
                        ArtMethod::DexCacheResolvedMethodsOffset(kArmPointerSize).Int32Value());
      // temp = temp[index_in_cache];
      // Note: Don't use invoke->GetTargetMethod() as it may point to a different dex file.
      uint32_t index_in_cache = invoke->GetDexMethodIndex();
      __ LoadFromOffset(kLoadWord, reg, reg, CodeGenerator::GetCachePointerOffset(index_in_cache));
      break;
    }
  }
  return callee_method;
}

void CodeGeneratorARM::GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp) {
  Location callee_method = GenerateCalleeMethodStaticOrDirectCall(invoke, temp);

  switch (invoke->GetCodePtrLocation()) {
    case HInvokeStaticOrDirect::CodePtrLocation::kCallSelf:
      __ bl(GetFrameEntryLabel());
      break;
    case HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod:
      // LR = callee_method->entry_point_from_quick_compiled_code_
      __ LoadFromOffset(
          kLoadWord, LR, callee_method.AsRegister<Register>(),
          ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArmPointerSize).Int32Value());
      // LR()
      __ blx(LR);
      break;
  }

  DCHECK(!IsLeafMethod());
}

void CodeGeneratorARM::GenerateVirtualCall(HInvokeVirtual* invoke, Location temp_location) {
  Register temp = temp_location.AsRegister<Register>();
  uint32_t method_offset = mirror::Class::EmbeddedVTableEntryOffset(
      invoke->GetVTableIndex(), kArmPointerSize).Uint32Value();

  // Use the calling convention instead of the location of the receiver, as
  // intrinsics may have put the receiver in a different register. In the intrinsics
  // slow path, the arguments have been moved to the right place, so here we are
  // guaranteed that the receiver is the first register of the calling convention.
  InvokeDexCallingConvention calling_convention;
  Register receiver = calling_convention.GetRegisterAt(0);
  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  // /* HeapReference<Class> */ temp = receiver->klass_
  __ LoadFromOffset(kLoadWord, temp, receiver, class_offset);
  MaybeRecordImplicitNullCheck(invoke);
  // Instead of simply (possibly) unpoisoning `temp` here, we should
  // emit a read barrier for the previous class reference load.
  // However this is not required in practice, as this is an
  // intermediate/temporary reference and because the current
  // concurrent copying collector keeps the from-space memory
  // intact/accessible until the end of the marking phase (the
  // concurrent copying collector may not in the future).
  __ MaybeUnpoisonHeapReference(temp);
  // temp = temp->GetMethodAt(method_offset);
  uint32_t entry_point = ArtMethod::EntryPointFromQuickCompiledCodeOffset(
      kArmPointerSize).Int32Value();
  __ LoadFromOffset(kLoadWord, temp, temp, method_offset);
  // LR = temp->GetEntryPoint();
  __ LoadFromOffset(kLoadWord, LR, temp, entry_point);
  // LR();
  __ blx(LR);
}

CodeGeneratorARM::PcRelativePatchInfo* CodeGeneratorARM::NewPcRelativeStringPatch(
    const DexFile& dex_file, dex::StringIndex string_index) {
  return NewPcRelativePatch(dex_file, string_index.index_, &pc_relative_string_patches_);
}

CodeGeneratorARM::PcRelativePatchInfo* CodeGeneratorARM::NewPcRelativeTypePatch(
    const DexFile& dex_file, dex::TypeIndex type_index) {
  return NewPcRelativePatch(dex_file, type_index.index_, &pc_relative_type_patches_);
}

CodeGeneratorARM::PcRelativePatchInfo* CodeGeneratorARM::NewTypeBssEntryPatch(
    const DexFile& dex_file, dex::TypeIndex type_index) {
  return NewPcRelativePatch(dex_file, type_index.index_, &type_bss_entry_patches_);
}

CodeGeneratorARM::PcRelativePatchInfo* CodeGeneratorARM::NewPcRelativeDexCacheArrayPatch(
    const DexFile& dex_file, uint32_t element_offset) {
  return NewPcRelativePatch(dex_file, element_offset, &pc_relative_dex_cache_patches_);
}

CodeGeneratorARM::PcRelativePatchInfo* CodeGeneratorARM::NewPcRelativePatch(
    const DexFile& dex_file, uint32_t offset_or_index, ArenaDeque<PcRelativePatchInfo>* patches) {
  patches->emplace_back(dex_file, offset_or_index);
  return &patches->back();
}

Literal* CodeGeneratorARM::DeduplicateBootImageStringLiteral(const DexFile& dex_file,
                                                             dex::StringIndex string_index) {
  return boot_image_string_patches_.GetOrCreate(
      StringReference(&dex_file, string_index),
      [this]() { return __ NewLiteral<uint32_t>(/* placeholder */ 0u); });
}

Literal* CodeGeneratorARM::DeduplicateBootImageTypeLiteral(const DexFile& dex_file,
                                                           dex::TypeIndex type_index) {
  return boot_image_type_patches_.GetOrCreate(
      TypeReference(&dex_file, type_index),
      [this]() { return __ NewLiteral<uint32_t>(/* placeholder */ 0u); });
}

Literal* CodeGeneratorARM::DeduplicateBootImageAddressLiteral(uint32_t address) {
  return DeduplicateUint32Literal(dchecked_integral_cast<uint32_t>(address), &uint32_literals_);
}

Literal* CodeGeneratorARM::DeduplicateJitStringLiteral(const DexFile& dex_file,
                                                       dex::StringIndex string_index,
                                                       Handle<mirror::String> handle) {
  jit_string_roots_.Overwrite(StringReference(&dex_file, string_index),
                              reinterpret_cast64<uint64_t>(handle.GetReference()));
  return jit_string_patches_.GetOrCreate(
      StringReference(&dex_file, string_index),
      [this]() { return __ NewLiteral<uint32_t>(/* placeholder */ 0u); });
}

Literal* CodeGeneratorARM::DeduplicateJitClassLiteral(const DexFile& dex_file,
                                                      dex::TypeIndex type_index,
                                                      Handle<mirror::Class> handle) {
  jit_class_roots_.Overwrite(TypeReference(&dex_file, type_index),
                             reinterpret_cast64<uint64_t>(handle.GetReference()));
  return jit_class_patches_.GetOrCreate(
      TypeReference(&dex_file, type_index),
      [this]() { return __ NewLiteral<uint32_t>(/* placeholder */ 0u); });
}

template <LinkerPatch (*Factory)(size_t, const DexFile*, uint32_t, uint32_t)>
inline void CodeGeneratorARM::EmitPcRelativeLinkerPatches(
    const ArenaDeque<PcRelativePatchInfo>& infos,
    ArenaVector<LinkerPatch>* linker_patches) {
  for (const PcRelativePatchInfo& info : infos) {
    const DexFile& dex_file = info.target_dex_file;
    size_t offset_or_index = info.offset_or_index;
    DCHECK(info.add_pc_label.IsBound());
    uint32_t add_pc_offset = dchecked_integral_cast<uint32_t>(info.add_pc_label.Position());
    // Add MOVW patch.
    DCHECK(info.movw_label.IsBound());
    uint32_t movw_offset = dchecked_integral_cast<uint32_t>(info.movw_label.Position());
    linker_patches->push_back(Factory(movw_offset, &dex_file, add_pc_offset, offset_or_index));
    // Add MOVT patch.
    DCHECK(info.movt_label.IsBound());
    uint32_t movt_offset = dchecked_integral_cast<uint32_t>(info.movt_label.Position());
    linker_patches->push_back(Factory(movt_offset, &dex_file, add_pc_offset, offset_or_index));
  }
}

void CodeGeneratorARM::EmitLinkerPatches(ArenaVector<LinkerPatch>* linker_patches) {
  DCHECK(linker_patches->empty());
  size_t size =
      /* MOVW+MOVT for each entry */ 2u * pc_relative_dex_cache_patches_.size() +
      boot_image_string_patches_.size() +
      /* MOVW+MOVT for each entry */ 2u * pc_relative_string_patches_.size() +
      boot_image_type_patches_.size() +
      /* MOVW+MOVT for each entry */ 2u * pc_relative_type_patches_.size() +
      /* MOVW+MOVT for each entry */ 2u * type_bss_entry_patches_.size();
  linker_patches->reserve(size);
  EmitPcRelativeLinkerPatches<LinkerPatch::DexCacheArrayPatch>(pc_relative_dex_cache_patches_,
                                                               linker_patches);
  for (const auto& entry : boot_image_string_patches_) {
    const StringReference& target_string = entry.first;
    Literal* literal = entry.second;
    DCHECK(literal->GetLabel()->IsBound());
    uint32_t literal_offset = literal->GetLabel()->Position();
    linker_patches->push_back(LinkerPatch::StringPatch(literal_offset,
                                                       target_string.dex_file,
                                                       target_string.string_index.index_));
  }
  if (!GetCompilerOptions().IsBootImage()) {
    DCHECK(pc_relative_type_patches_.empty());
    EmitPcRelativeLinkerPatches<LinkerPatch::StringBssEntryPatch>(pc_relative_string_patches_,
                                                                  linker_patches);
  } else {
    EmitPcRelativeLinkerPatches<LinkerPatch::RelativeTypePatch>(pc_relative_type_patches_,
                                                                linker_patches);
    EmitPcRelativeLinkerPatches<LinkerPatch::RelativeStringPatch>(pc_relative_string_patches_,
                                                                  linker_patches);
  }
  EmitPcRelativeLinkerPatches<LinkerPatch::TypeBssEntryPatch>(type_bss_entry_patches_,
                                                              linker_patches);
  for (const auto& entry : boot_image_type_patches_) {
    const TypeReference& target_type = entry.first;
    Literal* literal = entry.second;
    DCHECK(literal->GetLabel()->IsBound());
    uint32_t literal_offset = literal->GetLabel()->Position();
    linker_patches->push_back(LinkerPatch::TypePatch(literal_offset,
                                                     target_type.dex_file,
                                                     target_type.type_index.index_));
  }
  DCHECK_EQ(size, linker_patches->size());
}

Literal* CodeGeneratorARM::DeduplicateUint32Literal(uint32_t value, Uint32ToLiteralMap* map) {
  return map->GetOrCreate(
      value,
      [this, value]() { return __ NewLiteral<uint32_t>(value); });
}

Literal* CodeGeneratorARM::DeduplicateMethodLiteral(MethodReference target_method,
                                                    MethodToLiteralMap* map) {
  return map->GetOrCreate(
      target_method,
      [this]() { return __ NewLiteral<uint32_t>(/* placeholder */ 0u); });
}

void LocationsBuilderARM::VisitMultiplyAccumulate(HMultiplyAccumulate* instr) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instr, LocationSummary::kNoCall);
  locations->SetInAt(HMultiplyAccumulate::kInputAccumulatorIndex,
                     Location::RequiresRegister());
  locations->SetInAt(HMultiplyAccumulate::kInputMulLeftIndex, Location::RequiresRegister());
  locations->SetInAt(HMultiplyAccumulate::kInputMulRightIndex, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARM::VisitMultiplyAccumulate(HMultiplyAccumulate* instr) {
  LocationSummary* locations = instr->GetLocations();
  Register res = locations->Out().AsRegister<Register>();
  Register accumulator =
      locations->InAt(HMultiplyAccumulate::kInputAccumulatorIndex).AsRegister<Register>();
  Register mul_left =
      locations->InAt(HMultiplyAccumulate::kInputMulLeftIndex).AsRegister<Register>();
  Register mul_right =
      locations->InAt(HMultiplyAccumulate::kInputMulRightIndex).AsRegister<Register>();

  if (instr->GetOpKind() == HInstruction::kAdd) {
    __ mla(res, mul_left, mul_right, accumulator);
  } else {
    __ mls(res, mul_left, mul_right, accumulator);
  }
}

void LocationsBuilderARM::VisitBoundType(HBoundType* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, this should be removed during prepare for register allocator.
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorARM::VisitBoundType(HBoundType* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, this should be removed during prepare for register allocator.
  LOG(FATAL) << "Unreachable";
}

// Simple implementation of packed switch - generate cascaded compare/jumps.
void LocationsBuilderARM::VisitPackedSwitch(HPackedSwitch* switch_instr) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(switch_instr, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  if (switch_instr->GetNumEntries() > kPackedSwitchCompareJumpThreshold &&
      codegen_->GetAssembler()->IsThumb()) {
    locations->AddTemp(Location::RequiresRegister());  // We need a temp for the table base.
    if (switch_instr->GetStartValue() != 0) {
      locations->AddTemp(Location::RequiresRegister());  // We need a temp for the bias.
    }
  }
}

void InstructionCodeGeneratorARM::VisitPackedSwitch(HPackedSwitch* switch_instr) {
  int32_t lower_bound = switch_instr->GetStartValue();
  uint32_t num_entries = switch_instr->GetNumEntries();
  LocationSummary* locations = switch_instr->GetLocations();
  Register value_reg = locations->InAt(0).AsRegister<Register>();
  HBasicBlock* default_block = switch_instr->GetDefaultBlock();

  if (num_entries <= kPackedSwitchCompareJumpThreshold || !codegen_->GetAssembler()->IsThumb()) {
    // Create a series of compare/jumps.
    Register temp_reg = IP;
    // Note: It is fine for the below AddConstantSetFlags() using IP register to temporarily store
    // the immediate, because IP is used as the destination register. For the other
    // AddConstantSetFlags() and GenerateCompareWithImmediate(), the immediate values are constant,
    // and they can be encoded in the instruction without making use of IP register.
    __ AddConstantSetFlags(temp_reg, value_reg, -lower_bound);

    const ArenaVector<HBasicBlock*>& successors = switch_instr->GetBlock()->GetSuccessors();
    // Jump to successors[0] if value == lower_bound.
    __ b(codegen_->GetLabelOf(successors[0]), EQ);
    int32_t last_index = 0;
    for (; num_entries - last_index > 2; last_index += 2) {
      __ AddConstantSetFlags(temp_reg, temp_reg, -2);
      // Jump to successors[last_index + 1] if value < case_value[last_index + 2].
      __ b(codegen_->GetLabelOf(successors[last_index + 1]), LO);
      // Jump to successors[last_index + 2] if value == case_value[last_index + 2].
      __ b(codegen_->GetLabelOf(successors[last_index + 2]), EQ);
    }
    if (num_entries - last_index == 2) {
      // The last missing case_value.
      __ CmpConstant(temp_reg, 1);
      __ b(codegen_->GetLabelOf(successors[last_index + 1]), EQ);
    }

    // And the default for any other value.
    if (!codegen_->GoesToNextBlock(switch_instr->GetBlock(), default_block)) {
      __ b(codegen_->GetLabelOf(default_block));
    }
  } else {
    // Create a table lookup.
    Register temp_reg = locations->GetTemp(0).AsRegister<Register>();

    // Materialize a pointer to the switch table
    std::vector<Label*> labels(num_entries);
    const ArenaVector<HBasicBlock*>& successors = switch_instr->GetBlock()->GetSuccessors();
    for (uint32_t i = 0; i < num_entries; i++) {
      labels[i] = codegen_->GetLabelOf(successors[i]);
    }
    JumpTable* table = __ CreateJumpTable(std::move(labels), temp_reg);

    // Remove the bias.
    Register key_reg;
    if (lower_bound != 0) {
      key_reg = locations->GetTemp(1).AsRegister<Register>();
      __ AddConstant(key_reg, value_reg, -lower_bound);
    } else {
      key_reg = value_reg;
    }

    // Check whether the value is in the table, jump to default block if not.
    __ CmpConstant(key_reg, num_entries - 1);
    __ b(codegen_->GetLabelOf(default_block), Condition::HI);

    // Load the displacement from the table.
    __ ldr(temp_reg, Address(temp_reg, key_reg, Shift::LSL, 2));

    // Dispatch is a direct add to the PC (for Thumb2).
    __ EmitJumpTableDispatch(table, temp_reg);
  }
}

void LocationsBuilderARM::VisitArmDexCacheArraysBase(HArmDexCacheArraysBase* base) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(base);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM::VisitArmDexCacheArraysBase(HArmDexCacheArraysBase* base) {
  Register base_reg = base->GetLocations()->Out().AsRegister<Register>();
  CodeGeneratorARM::PcRelativePatchInfo* labels =
      codegen_->NewPcRelativeDexCacheArrayPatch(base->GetDexFile(), base->GetElementOffset());
  __ BindTrackedLabel(&labels->movw_label);
  __ movw(base_reg, /* placeholder */ 0u);
  __ BindTrackedLabel(&labels->movt_label);
  __ movt(base_reg, /* placeholder */ 0u);
  __ BindTrackedLabel(&labels->add_pc_label);
  __ add(base_reg, base_reg, ShifterOperand(PC));
}

void CodeGeneratorARM::MoveFromReturnRegister(Location trg, Primitive::Type type) {
  if (!trg.IsValid()) {
    DCHECK_EQ(type, Primitive::kPrimVoid);
    return;
  }

  DCHECK_NE(type, Primitive::kPrimVoid);

  Location return_loc = InvokeDexCallingConventionVisitorARM().GetReturnLocation(type);
  if (return_loc.Equals(trg)) {
    return;
  }

  // TODO: Consider pairs in the parallel move resolver, then this could be nicely merged
  //       with the last branch.
  if (type == Primitive::kPrimLong) {
    HParallelMove parallel_move(GetGraph()->GetArena());
    parallel_move.AddMove(return_loc.ToLow(), trg.ToLow(), Primitive::kPrimInt, nullptr);
    parallel_move.AddMove(return_loc.ToHigh(), trg.ToHigh(), Primitive::kPrimInt, nullptr);
    GetMoveResolver()->EmitNativeCode(&parallel_move);
  } else if (type == Primitive::kPrimDouble) {
    HParallelMove parallel_move(GetGraph()->GetArena());
    parallel_move.AddMove(return_loc.ToLow(), trg.ToLow(), Primitive::kPrimFloat, nullptr);
    parallel_move.AddMove(return_loc.ToHigh(), trg.ToHigh(), Primitive::kPrimFloat, nullptr);
    GetMoveResolver()->EmitNativeCode(&parallel_move);
  } else {
    // Let the parallel move resolver take care of all of this.
    HParallelMove parallel_move(GetGraph()->GetArena());
    parallel_move.AddMove(return_loc, trg, type, nullptr);
    GetMoveResolver()->EmitNativeCode(&parallel_move);
  }
}

void LocationsBuilderARM::VisitClassTableGet(HClassTableGet* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARM::VisitClassTableGet(HClassTableGet* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  if (instruction->GetTableKind() == HClassTableGet::TableKind::kVTable) {
    uint32_t method_offset = mirror::Class::EmbeddedVTableEntryOffset(
        instruction->GetIndex(), kArmPointerSize).SizeValue();
    __ LoadFromOffset(kLoadWord,
                      locations->Out().AsRegister<Register>(),
                      locations->InAt(0).AsRegister<Register>(),
                      method_offset);
  } else {
    uint32_t method_offset = static_cast<uint32_t>(ImTable::OffsetOfElement(
        instruction->GetIndex(), kArmPointerSize));
    __ LoadFromOffset(kLoadWord,
                      locations->Out().AsRegister<Register>(),
                      locations->InAt(0).AsRegister<Register>(),
                      mirror::Class::ImtPtrOffset(kArmPointerSize).Uint32Value());
    __ LoadFromOffset(kLoadWord,
                      locations->Out().AsRegister<Register>(),
                      locations->Out().AsRegister<Register>(),
                      method_offset);
  }
}

static void PatchJitRootUse(uint8_t* code,
                            const uint8_t* roots_data,
                            Literal* literal,
                            uint64_t index_in_table) {
  DCHECK(literal->GetLabel()->IsBound());
  uint32_t literal_offset = literal->GetLabel()->Position();
  uintptr_t address =
      reinterpret_cast<uintptr_t>(roots_data) + index_in_table * sizeof(GcRoot<mirror::Object>);
  uint8_t* data = code + literal_offset;
  reinterpret_cast<uint32_t*>(data)[0] = dchecked_integral_cast<uint32_t>(address);
}

void CodeGeneratorARM::EmitJitRootPatches(uint8_t* code, const uint8_t* roots_data) {
  for (const auto& entry : jit_string_patches_) {
    const auto& it = jit_string_roots_.find(entry.first);
    DCHECK(it != jit_string_roots_.end());
    PatchJitRootUse(code, roots_data, entry.second, it->second);
  }
  for (const auto& entry : jit_class_patches_) {
    const auto& it = jit_class_roots_.find(entry.first);
    DCHECK(it != jit_class_roots_.end());
    PatchJitRootUse(code, roots_data, entry.second, it->second);
  }
}

#undef __
#undef QUICK_ENTRY_POINT

}  // namespace arm
}  // namespace art
