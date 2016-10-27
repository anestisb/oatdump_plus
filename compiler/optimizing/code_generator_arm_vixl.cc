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

#include "code_generator_arm_vixl.h"

#include "arch/arm/instruction_set_features_arm.h"
#include "art_method.h"
#include "code_generator_utils.h"
#include "common_arm.h"
#include "compiled_method.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "gc/accounting/card_table.h"
#include "mirror/array-inl.h"
#include "mirror/class-inl.h"
#include "thread.h"
#include "utils/arm/assembler_arm_vixl.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/assembler.h"
#include "utils/stack_checks.h"

namespace art {
namespace arm {

namespace vixl32 = vixl::aarch32;
using namespace vixl32;  // NOLINT(build/namespaces)

using helpers::DRegisterFrom;
using helpers::DWARFReg;
using helpers::FromLowSToD;
using helpers::HighDRegisterFrom;
using helpers::HighRegisterFrom;
using helpers::InputOperandAt;
using helpers::InputRegisterAt;
using helpers::InputSRegisterAt;
using helpers::InputVRegisterAt;
using helpers::LocationFrom;
using helpers::LowRegisterFrom;
using helpers::LowSRegisterFrom;
using helpers::OutputRegister;
using helpers::OutputSRegister;
using helpers::OutputVRegister;
using helpers::RegisterFrom;
using helpers::SRegisterFrom;

using RegisterList = vixl32::RegisterList;

static bool ExpectedPairLayout(Location location) {
  // We expected this for both core and fpu register pairs.
  return ((location.low() & 1) == 0) && (location.low() + 1 == location.high());
}

static constexpr size_t kArmInstrMaxSizeInBytes = 4u;

#ifdef __
#error "ARM Codegen VIXL macro-assembler macro already defined."
#endif

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<CodeGeneratorARMVIXL*>(codegen)->GetVIXLAssembler()->  // NOLINT
#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kArmPointerSize, x).Int32Value()

// Marker that code is yet to be, and must, be implemented.
#define TODO_VIXL32(level) LOG(level) << __PRETTY_FUNCTION__ << " unimplemented "

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
//
// TODO(VIXL): All this code should be unnecessary once the VIXL AArch32 backend provides helpers
// for lists of floating-point registers.
static size_t SaveContiguousSRegisterList(size_t first,
                                          size_t last,
                                          CodeGenerator* codegen,
                                          size_t stack_offset) {
  static_assert(kSRegSizeInBytes == kArmWordSize, "Broken assumption on reg/word sizes.");
  static_assert(kDRegSizeInBytes == 2 * kArmWordSize, "Broken assumption on reg/word sizes.");
  DCHECK_LE(first, last);
  if ((first == last) && (first == 0)) {
    __ Vstr(vixl32::SRegister(first), MemOperand(sp, stack_offset));
    return stack_offset + kSRegSizeInBytes;
  }
  if (first % 2 == 1) {
    __ Vstr(vixl32::SRegister(first++), MemOperand(sp, stack_offset));
    stack_offset += kSRegSizeInBytes;
  }

  bool save_last = false;
  if (last % 2 == 0) {
    save_last = true;
    --last;
  }

  if (first < last) {
    vixl32::DRegister d_reg = vixl32::DRegister(first / 2);
    DCHECK_EQ((last - first + 1) % 2, 0u);
    size_t number_of_d_regs = (last - first + 1) / 2;

    if (number_of_d_regs == 1) {
      __ Vstr(d_reg, MemOperand(sp, stack_offset));
    } else if (number_of_d_regs > 1) {
      UseScratchRegisterScope temps(down_cast<CodeGeneratorARMVIXL*>(codegen)->GetVIXLAssembler());
      vixl32::Register base = sp;
      if (stack_offset != 0) {
        base = temps.Acquire();
        __ Add(base, sp, stack_offset);
      }
      __ Vstm(F64, base, NO_WRITE_BACK, DRegisterList(d_reg, number_of_d_regs));
    }
    stack_offset += number_of_d_regs * kDRegSizeInBytes;
  }

  if (save_last) {
    __ Vstr(vixl32::SRegister(last + 1), MemOperand(sp, stack_offset));
    stack_offset += kSRegSizeInBytes;
  }

  return stack_offset;
}

static size_t RestoreContiguousSRegisterList(size_t first,
                                             size_t last,
                                             CodeGenerator* codegen,
                                             size_t stack_offset) {
  static_assert(kSRegSizeInBytes == kArmWordSize, "Broken assumption on reg/word sizes.");
  static_assert(kDRegSizeInBytes == 2 * kArmWordSize, "Broken assumption on reg/word sizes.");
  DCHECK_LE(first, last);
  if ((first == last) && (first == 0)) {
    __ Vldr(vixl32::SRegister(first), MemOperand(sp, stack_offset));
    return stack_offset + kSRegSizeInBytes;
  }
  if (first % 2 == 1) {
    __ Vldr(vixl32::SRegister(first++), MemOperand(sp, stack_offset));
    stack_offset += kSRegSizeInBytes;
  }

  bool restore_last = false;
  if (last % 2 == 0) {
    restore_last = true;
    --last;
  }

  if (first < last) {
    vixl32::DRegister d_reg = vixl32::DRegister(first / 2);
    DCHECK_EQ((last - first + 1) % 2, 0u);
    size_t number_of_d_regs = (last - first + 1) / 2;
    if (number_of_d_regs == 1) {
      __ Vldr(d_reg, MemOperand(sp, stack_offset));
    } else if (number_of_d_regs > 1) {
      UseScratchRegisterScope temps(down_cast<CodeGeneratorARMVIXL*>(codegen)->GetVIXLAssembler());
      vixl32::Register base = sp;
      if (stack_offset != 0) {
        base = temps.Acquire();
        __ Add(base, sp, stack_offset);
      }
      __ Vldm(F64, base, NO_WRITE_BACK, DRegisterList(d_reg, number_of_d_regs));
    }
    stack_offset += number_of_d_regs * kDRegSizeInBytes;
  }

  if (restore_last) {
    __ Vldr(vixl32::SRegister(last + 1), MemOperand(sp, stack_offset));
    stack_offset += kSRegSizeInBytes;
  }

  return stack_offset;
}

void SlowPathCodeARMVIXL::SaveLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) {
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

  CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
  arm_codegen->GetAssembler()->StoreRegisterList(core_spills, orig_offset);

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

void SlowPathCodeARMVIXL::RestoreLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) {
  size_t stack_offset = codegen->GetFirstRegisterSlotInSlowPath();
  size_t orig_offset = stack_offset;

  const uint32_t core_spills = codegen->GetSlowPathSpills(locations, /* core_registers */ true);
  for (uint32_t i : LowToHighBits(core_spills)) {
    DCHECK_LT(stack_offset, codegen->GetFrameSize() - codegen->FrameEntrySpillSize());
    DCHECK_LT(i, kMaximumNumberOfExpectedRegisters);
    stack_offset += kArmWordSize;
  }

  // TODO(VIXL): Check the coherency of stack_offset after this with a test.
  CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
  arm_codegen->GetAssembler()->LoadRegisterList(core_spills, orig_offset);

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

class NullCheckSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  explicit NullCheckSlowPathARMVIXL(HNullCheck* instruction) : SlowPathCodeARMVIXL(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
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

  const char* GetDescription() const OVERRIDE { return "NullCheckSlowPathARMVIXL"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullCheckSlowPathARMVIXL);
};

class DivZeroCheckSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  explicit DivZeroCheckSlowPathARMVIXL(HDivZeroCheck* instruction)
      : SlowPathCodeARMVIXL(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    __ Bind(GetEntryLabel());
    arm_codegen->InvokeRuntime(kQuickThrowDivZero, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowDivZero, void, void>();
  }

  bool IsFatal() const OVERRIDE { return true; }

  const char* GetDescription() const OVERRIDE { return "DivZeroCheckSlowPathARMVIXL"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DivZeroCheckSlowPathARMVIXL);
};

class SuspendCheckSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  SuspendCheckSlowPathARMVIXL(HSuspendCheck* instruction, HBasicBlock* successor)
      : SlowPathCodeARMVIXL(instruction), successor_(successor) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    __ Bind(GetEntryLabel());
    arm_codegen->InvokeRuntime(kQuickTestSuspend, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickTestSuspend, void, void>();
    if (successor_ == nullptr) {
      __ B(GetReturnLabel());
    } else {
      __ B(arm_codegen->GetLabelOf(successor_));
    }
  }

  vixl32::Label* GetReturnLabel() {
    DCHECK(successor_ == nullptr);
    return &return_label_;
  }

  HBasicBlock* GetSuccessor() const {
    return successor_;
  }

  const char* GetDescription() const OVERRIDE { return "SuspendCheckSlowPathARMVIXL"; }

 private:
  // If not null, the block to branch to after the suspend check.
  HBasicBlock* const successor_;

  // If `successor_` is null, the label to branch to after the suspend check.
  vixl32::Label return_label_;

  DISALLOW_COPY_AND_ASSIGN(SuspendCheckSlowPathARMVIXL);
};

class LoadClassSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  LoadClassSlowPathARMVIXL(HLoadClass* cls, HInstruction* at, uint32_t dex_pc, bool do_clinit)
      : SlowPathCodeARMVIXL(at), cls_(cls), at_(at), dex_pc_(dex_pc), do_clinit_(do_clinit) {
    DCHECK(at->IsLoadClass() || at->IsClinitCheck());
  }

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    LocationSummary* locations = at_->GetLocations();

    CodeGeneratorARMVIXL* arm_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);

    InvokeRuntimeCallingConventionARMVIXL calling_convention;
    __ Mov(calling_convention.GetRegisterAt(0), cls_->GetTypeIndex());
    QuickEntrypointEnum entrypoint = do_clinit_ ? kQuickInitializeStaticStorage
                                                : kQuickInitializeType;
    arm_codegen->InvokeRuntime(entrypoint, at_, dex_pc_, this);
    if (do_clinit_) {
      CheckEntrypointTypes<kQuickInitializeStaticStorage, void*, uint32_t>();
    } else {
      CheckEntrypointTypes<kQuickInitializeType, void*, uint32_t>();
    }

    // Move the class to the desired location.
    Location out = locations->Out();
    if (out.IsValid()) {
      DCHECK(out.IsRegister() && !locations->GetLiveRegisters()->ContainsCoreRegister(out.reg()));
      arm_codegen->Move32(locations->Out(), LocationFrom(r0));
    }
    RestoreLiveRegisters(codegen, locations);
    __ B(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "LoadClassSlowPathARMVIXL"; }

 private:
  // The class this slow path will load.
  HLoadClass* const cls_;

  // The instruction where this slow path is happening.
  // (Might be the load class or an initialization check).
  HInstruction* const at_;

  // The dex PC of `at_`.
  const uint32_t dex_pc_;

  // Whether to initialize the class.
  const bool do_clinit_;

  DISALLOW_COPY_AND_ASSIGN(LoadClassSlowPathARMVIXL);
};

inline vixl32::Condition ARMCondition(IfCondition cond) {
  switch (cond) {
    case kCondEQ: return eq;
    case kCondNE: return ne;
    case kCondLT: return lt;
    case kCondLE: return le;
    case kCondGT: return gt;
    case kCondGE: return ge;
    case kCondB:  return lo;
    case kCondBE: return ls;
    case kCondA:  return hi;
    case kCondAE: return hs;
  }
  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

// Maps signed condition to unsigned condition.
inline vixl32::Condition ARMUnsignedCondition(IfCondition cond) {
  switch (cond) {
    case kCondEQ: return eq;
    case kCondNE: return ne;
    // Signed to unsigned.
    case kCondLT: return lo;
    case kCondLE: return ls;
    case kCondGT: return hi;
    case kCondGE: return hs;
    // Unsigned remain unchanged.
    case kCondB:  return lo;
    case kCondBE: return ls;
    case kCondA:  return hi;
    case kCondAE: return hs;
  }
  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

inline vixl32::Condition ARMFPCondition(IfCondition cond, bool gt_bias) {
  // The ARM condition codes can express all the necessary branches, see the
  // "Meaning (floating-point)" column in the table A8-1 of the ARMv7 reference manual.
  // There is no dex instruction or HIR that would need the missing conditions
  // "equal or unordered" or "not equal".
  switch (cond) {
    case kCondEQ: return eq;
    case kCondNE: return ne /* unordered */;
    case kCondLT: return gt_bias ? cc : lt /* unordered */;
    case kCondLE: return gt_bias ? ls : le /* unordered */;
    case kCondGT: return gt_bias ? hi /* unordered */ : gt;
    case kCondGE: return gt_bias ? cs /* unordered */ : ge;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}

void CodeGeneratorARMVIXL::DumpCoreRegister(std::ostream& stream, int reg) const {
  stream << vixl32::Register(reg);
}

void CodeGeneratorARMVIXL::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  stream << vixl32::SRegister(reg);
}

static uint32_t ComputeSRegisterListMask(const SRegisterList& regs) {
  uint32_t mask = 0;
  for (uint32_t i = regs.GetFirstSRegister().GetCode();
       i <= regs.GetLastSRegister().GetCode();
       ++i) {
    mask |= (1 << i);
  }
  return mask;
}

#undef __

CodeGeneratorARMVIXL::CodeGeneratorARMVIXL(HGraph* graph,
                                           const ArmInstructionSetFeatures& isa_features,
                                           const CompilerOptions& compiler_options,
                                           OptimizingCompilerStats* stats)
    : CodeGenerator(graph,
                    kNumberOfCoreRegisters,
                    kNumberOfSRegisters,
                    kNumberOfRegisterPairs,
                    kCoreCalleeSaves.GetList(),
                    ComputeSRegisterListMask(kFpuCalleeSaves),
                    compiler_options,
                    stats),
      block_labels_(graph->GetArena()->Adapter(kArenaAllocCodeGenerator)),
      location_builder_(graph, this),
      instruction_visitor_(graph, this),
      move_resolver_(graph->GetArena(), this),
      assembler_(graph->GetArena()),
      isa_features_(isa_features) {
  // Always save the LR register to mimic Quick.
  AddAllocatedRegister(Location::RegisterLocation(LR));
  // Give d14 and d15 as scratch registers to VIXL.
  // They are removed from the register allocator in `SetupBlockedRegisters()`.
  // TODO(VIXL): We need two scratch D registers for `EmitSwap` when swapping two double stack
  // slots. If that is sufficiently rare, and we have pressure on FP registers, we could instead
  // spill in `EmitSwap`. But if we actually are guaranteed to have 32 D registers, we could give
  // d30 and d31 to VIXL to avoid removing registers from the allocator. If that is the case, we may
  // also want to investigate giving those 14 other D registers to the allocator.
  GetVIXLAssembler()->GetScratchVRegisterList()->Combine(d14);
  GetVIXLAssembler()->GetScratchVRegisterList()->Combine(d15);
}

#define __ reinterpret_cast<ArmVIXLAssembler*>(GetAssembler())->GetVIXLAssembler()->

void CodeGeneratorARMVIXL::Finalize(CodeAllocator* allocator) {
  GetAssembler()->FinalizeCode();
  CodeGenerator::Finalize(allocator);
}

void CodeGeneratorARMVIXL::SetupBlockedRegisters() const {
  // Stack register, LR and PC are always reserved.
  blocked_core_registers_[SP] = true;
  blocked_core_registers_[LR] = true;
  blocked_core_registers_[PC] = true;

  // Reserve thread register.
  blocked_core_registers_[TR] = true;

  // Reserve temp register.
  blocked_core_registers_[IP] = true;

  // Registers s28-s31 (d14-d15) are left to VIXL for scratch registers.
  // (They are given to the `MacroAssembler` in `CodeGeneratorARMVIXL::CodeGeneratorARMVIXL`.)
  blocked_fpu_registers_[28] = true;
  blocked_fpu_registers_[29] = true;
  blocked_fpu_registers_[30] = true;
  blocked_fpu_registers_[31] = true;

  if (GetGraph()->IsDebuggable()) {
    // Stubs do not save callee-save floating point registers. If the graph
    // is debuggable, we need to deal with these registers differently. For
    // now, just block them.
    for (uint32_t i = kFpuCalleeSaves.GetFirstSRegister().GetCode();
         i <= kFpuCalleeSaves.GetLastSRegister().GetCode();
         ++i) {
      blocked_fpu_registers_[i] = true;
    }
  }
}

InstructionCodeGeneratorARMVIXL::InstructionCodeGeneratorARMVIXL(HGraph* graph,
                                                                 CodeGeneratorARMVIXL* codegen)
      : InstructionCodeGenerator(graph, codegen),
        assembler_(codegen->GetAssembler()),
        codegen_(codegen) {}

void CodeGeneratorARMVIXL::ComputeSpillMask() {
  core_spill_mask_ = allocated_registers_.GetCoreRegisters() & core_callee_save_mask_;
  DCHECK_NE(core_spill_mask_, 0u) << "At least the return address register must be saved";
  // There is no easy instruction to restore just the PC on thumb2. We spill and
  // restore another arbitrary register.
  core_spill_mask_ |= (1 << kCoreAlwaysSpillRegister.GetCode());
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

void CodeGeneratorARMVIXL::GenerateFrameEntry() {
  bool skip_overflow_check =
      IsLeafMethod() && !FrameNeedsStackCheck(GetFrameSize(), InstructionSet::kArm);
  DCHECK(GetCompilerOptions().GetImplicitStackOverflowChecks());
  __ Bind(&frame_entry_label_);

  if (HasEmptyFrame()) {
    return;
  }

  if (!skip_overflow_check) {
    UseScratchRegisterScope temps(GetVIXLAssembler());
    vixl32::Register temp = temps.Acquire();
    __ Sub(temp, sp, static_cast<int32_t>(GetStackOverflowReservedBytes(kArm)));
    // The load must immediately precede RecordPcInfo.
    AssemblerAccurateScope aas(GetVIXLAssembler(),
                               kArmInstrMaxSizeInBytes,
                               CodeBufferCheckScope::kMaximumSize);
    __ ldr(temp, MemOperand(temp));
    RecordPcInfo(nullptr, 0);
  }

  __ Push(RegisterList(core_spill_mask_));
  GetAssembler()->cfi().AdjustCFAOffset(kArmWordSize * POPCOUNT(core_spill_mask_));
  GetAssembler()->cfi().RelOffsetForMany(DWARFReg(kMethodRegister),
                                         0,
                                         core_spill_mask_,
                                         kArmWordSize);
  if (fpu_spill_mask_ != 0) {
    uint32_t first = LeastSignificantBit(fpu_spill_mask_);

    // Check that list is contiguous.
    DCHECK_EQ(fpu_spill_mask_ >> CTZ(fpu_spill_mask_), ~0u >> (32 - POPCOUNT(fpu_spill_mask_)));

    __ Vpush(SRegisterList(vixl32::SRegister(first), POPCOUNT(fpu_spill_mask_)));
    GetAssembler()->cfi().AdjustCFAOffset(kArmWordSize * POPCOUNT(fpu_spill_mask_));
    GetAssembler()->cfi().RelOffsetForMany(DWARFReg(s0), 0, fpu_spill_mask_, kArmWordSize);
  }
  int adjust = GetFrameSize() - FrameEntrySpillSize();
  __ Sub(sp, sp, adjust);
  GetAssembler()->cfi().AdjustCFAOffset(adjust);
  GetAssembler()->StoreToOffset(kStoreWord, kMethodRegister, sp, 0);
}

void CodeGeneratorARMVIXL::GenerateFrameExit() {
  if (HasEmptyFrame()) {
    __ Bx(lr);
    return;
  }
  GetAssembler()->cfi().RememberState();
  int adjust = GetFrameSize() - FrameEntrySpillSize();
  __ Add(sp, sp, adjust);
  GetAssembler()->cfi().AdjustCFAOffset(-adjust);
  if (fpu_spill_mask_ != 0) {
    uint32_t first = LeastSignificantBit(fpu_spill_mask_);

    // Check that list is contiguous.
    DCHECK_EQ(fpu_spill_mask_ >> CTZ(fpu_spill_mask_), ~0u >> (32 - POPCOUNT(fpu_spill_mask_)));

    __ Vpop(SRegisterList(vixl32::SRegister(first), POPCOUNT(fpu_spill_mask_)));
    GetAssembler()->cfi().AdjustCFAOffset(
        -static_cast<int>(kArmWordSize) * POPCOUNT(fpu_spill_mask_));
    GetAssembler()->cfi().RestoreMany(DWARFReg(vixl32::SRegister(0)), fpu_spill_mask_);
  }
  // Pop LR into PC to return.
  DCHECK_NE(core_spill_mask_ & (1 << kLrCode), 0U);
  uint32_t pop_mask = (core_spill_mask_ & (~(1 << kLrCode))) | 1 << kPcCode;
  __ Pop(RegisterList(pop_mask));
  GetAssembler()->cfi().RestoreState();
  GetAssembler()->cfi().DefCFAOffset(GetFrameSize());
}

void CodeGeneratorARMVIXL::Bind(HBasicBlock* block) {
  __ Bind(GetLabelOf(block));
}

void CodeGeneratorARMVIXL::Move32(Location destination, Location source) {
  if (source.Equals(destination)) {
    return;
  }
  if (destination.IsRegister()) {
    if (source.IsRegister()) {
      __ Mov(RegisterFrom(destination), RegisterFrom(source));
    } else if (source.IsFpuRegister()) {
      __ Vmov(RegisterFrom(destination), SRegisterFrom(source));
    } else {
      GetAssembler()->LoadFromOffset(kLoadWord,
                                     RegisterFrom(destination),
                                     sp,
                                     source.GetStackIndex());
    }
  } else if (destination.IsFpuRegister()) {
    if (source.IsRegister()) {
      __ Vmov(SRegisterFrom(destination), RegisterFrom(source));
    } else if (source.IsFpuRegister()) {
      __ Vmov(SRegisterFrom(destination), SRegisterFrom(source));
    } else {
      GetAssembler()->LoadSFromOffset(SRegisterFrom(destination), sp, source.GetStackIndex());
    }
  } else {
    DCHECK(destination.IsStackSlot()) << destination;
    if (source.IsRegister()) {
      GetAssembler()->StoreToOffset(kStoreWord,
                                    RegisterFrom(source),
                                    sp,
                                    destination.GetStackIndex());
    } else if (source.IsFpuRegister()) {
      GetAssembler()->StoreSToOffset(SRegisterFrom(source), sp, destination.GetStackIndex());
    } else {
      DCHECK(source.IsStackSlot()) << source;
      UseScratchRegisterScope temps(GetVIXLAssembler());
      vixl32::Register temp = temps.Acquire();
      GetAssembler()->LoadFromOffset(kLoadWord, temp, sp, source.GetStackIndex());
      GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
    }
  }
}

void CodeGeneratorARMVIXL::MoveConstant(Location destination ATTRIBUTE_UNUSED,
                                        int32_t value ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::MoveLocation(Location dst, Location src, Primitive::Type dst_type) {
  // TODO(VIXL): Maybe refactor to have the 'move' implementation here and use it in
  // `ParallelMoveResolverARMVIXL::EmitMove`, as is done in the `arm64` backend.
  HParallelMove move(GetGraph()->GetArena());
  move.AddMove(src, dst, dst_type, nullptr);
  GetMoveResolver()->EmitNativeCode(&move);
}

void CodeGeneratorARMVIXL::AddLocationAsTemp(Location location ATTRIBUTE_UNUSED,
                                             LocationSummary* locations ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  ValidateInvokeRuntime(entrypoint, instruction, slow_path);
  GenerateInvokeRuntime(GetThreadOffset<kArmPointerSize>(entrypoint).Int32Value());
  if (EntrypointRequiresStackMap(entrypoint)) {
    // TODO(VIXL): If necessary, use a scope to ensure we record the pc info immediately after the
    // previous instruction.
    RecordPcInfo(instruction, dex_pc, slow_path);
  }
}

void CodeGeneratorARMVIXL::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                               HInstruction* instruction,
                                                               SlowPathCode* slow_path) {
  ValidateInvokeRuntimeWithoutRecordingPcInfo(instruction, slow_path);
  GenerateInvokeRuntime(entry_point_offset);
}

void CodeGeneratorARMVIXL::GenerateInvokeRuntime(int32_t entry_point_offset) {
  GetAssembler()->LoadFromOffset(kLoadWord, lr, tr, entry_point_offset);
  __ Blx(lr);
}

void LocationsBuilderARMVIXL::VisitClinitCheck(HClinitCheck* check) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(check, LocationSummary::kCallOnSlowPath);
  locations->SetInAt(0, Location::RequiresRegister());
  if (check->HasUses()) {
    locations->SetOut(Location::SameAsFirstInput());
  }
}

void InstructionCodeGeneratorARMVIXL::VisitClinitCheck(HClinitCheck* check) {
  // We assume the class is not null.
  LoadClassSlowPathARMVIXL* slow_path =
      new (GetGraph()->GetArena()) LoadClassSlowPathARMVIXL(check->GetLoadClass(),
                                                            check,
                                                            check->GetDexPc(),
                                                            /* do_clinit */ true);
  codegen_->AddSlowPath(slow_path);
  GenerateClassInitializationCheck(slow_path, InputRegisterAt(check, 0));
}

void InstructionCodeGeneratorARMVIXL::GenerateClassInitializationCheck(
    LoadClassSlowPathARMVIXL* slow_path, vixl32::Register class_reg) {
  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  GetAssembler()->LoadFromOffset(kLoadWord,
                                 temp,
                                 class_reg,
                                 mirror::Class::StatusOffset().Int32Value());
  __ Cmp(temp, mirror::Class::kStatusInitialized);
  __ B(lt, slow_path->GetEntryLabel());
  // Even if the initialized flag is set, we may be in a situation where caches are not synced
  // properly. Therefore, we do a memory fence.
  __ Dmb(ISH);
  __ Bind(slow_path->GetExitLabel());
}

// Check if the desired_string_load_kind is supported. If it is, return it,
// otherwise return a fall-back kind that should be used instead.
HLoadString::LoadKind CodeGeneratorARMVIXL::GetSupportedLoadStringKind(
      HLoadString::LoadKind desired_string_load_kind ATTRIBUTE_UNUSED) {
  // TODO(VIXL): Implement optimized code paths. For now we always use the simpler fallback code.
  return HLoadString::LoadKind::kDexCacheViaMethod;
}

void LocationsBuilderARMVIXL::VisitLoadString(HLoadString* load) {
  LocationSummary::CallKind call_kind = load->NeedsEnvironment()
      ? LocationSummary::kCallOnMainOnly
      : LocationSummary::kNoCall;
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(load, call_kind);

  // TODO(VIXL): Implement optimized code paths.
  // See InstructionCodeGeneratorARMVIXL::VisitLoadString.
  HLoadString::LoadKind load_kind = load->GetLoadKind();
  if (load_kind == HLoadString::LoadKind::kDexCacheViaMethod) {
    locations->SetInAt(0, Location::RequiresRegister());
    // TODO(VIXL): Use InvokeRuntimeCallingConventionARMVIXL instead.
    locations->SetOut(LocationFrom(r0));
  } else {
    locations->SetOut(Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARMVIXL::VisitLoadString(HLoadString* load) {
  // TODO(VIXL): Implement optimized code paths.
  // We implemented the simplest solution to get first ART tests passing, we deferred the
  // optimized path until later, we should implement it using ARM64 implementation as a
  // reference. The same related to LocationsBuilderARMVIXL::VisitLoadString.

  // TODO: Re-add the compiler code to do string dex cache lookup again.
  DCHECK_EQ(load->GetLoadKind(), HLoadString::LoadKind::kDexCacheViaMethod);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  __ Mov(calling_convention.GetRegisterAt(0), load->GetStringIndex());
  codegen_->InvokeRuntime(kQuickResolveString, load, load->GetDexPc());
  CheckEntrypointTypes<kQuickResolveString, void*, uint32_t>();
}

// Check if the desired_class_load_kind is supported. If it is, return it,
// otherwise return a fall-back kind that should be used instead.
HLoadClass::LoadKind CodeGeneratorARMVIXL::GetSupportedLoadClassKind(
      HLoadClass::LoadKind desired_class_load_kind ATTRIBUTE_UNUSED) {
  // TODO(VIXL): Implement optimized code paths.
  return HLoadClass::LoadKind::kDexCacheViaMethod;
}

// Check if the desired_dispatch_info is supported. If it is, return it,
// otherwise return a fall-back info that should be used instead.
HInvokeStaticOrDirect::DispatchInfo CodeGeneratorARMVIXL::GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info ATTRIBUTE_UNUSED,
      HInvokeStaticOrDirect* invoke ATTRIBUTE_UNUSED) {
  // TODO(VIXL): Implement optimized code paths.
  return {
    HInvokeStaticOrDirect::MethodLoadKind::kDexCacheViaMethod,
    HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod,
    0u,
    0u
  };
}

// Copy the result of a call into the given target.
void CodeGeneratorARMVIXL::MoveFromReturnRegister(Location trg ATTRIBUTE_UNUSED,
                                                  Primitive::Type type ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void InstructionCodeGeneratorARMVIXL::HandleGoto(HInstruction* got, HBasicBlock* successor) {
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
  if (!codegen_->GoesToNextBlock(block, successor)) {
    __ B(codegen_->GetLabelOf(successor));
  }
}

void LocationsBuilderARMVIXL::VisitGoto(HGoto* got) {
  got->SetLocations(nullptr);
}

void InstructionCodeGeneratorARMVIXL::VisitGoto(HGoto* got) {
  HandleGoto(got, got->GetSuccessor());
}

void LocationsBuilderARMVIXL::VisitTryBoundary(HTryBoundary* try_boundary) {
  try_boundary->SetLocations(nullptr);
}

void InstructionCodeGeneratorARMVIXL::VisitTryBoundary(HTryBoundary* try_boundary) {
  HBasicBlock* successor = try_boundary->GetNormalFlowSuccessor();
  if (!successor->IsExitBlock()) {
    HandleGoto(try_boundary, successor);
  }
}

void LocationsBuilderARMVIXL::VisitExit(HExit* exit) {
  exit->SetLocations(nullptr);
}

void InstructionCodeGeneratorARMVIXL::VisitExit(HExit* exit ATTRIBUTE_UNUSED) {
}

void InstructionCodeGeneratorARMVIXL::GenerateVcmp(HInstruction* instruction) {
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
      __ Vcmp(F32, InputSRegisterAt(instruction, 0), 0.0);
    } else {
      DCHECK_EQ(type, Primitive::kPrimDouble);
      __ Vcmp(F64, FromLowSToD(LowSRegisterFrom(lhs_loc)), 0.0);
    }
  } else {
    if (type == Primitive::kPrimFloat) {
      __ Vcmp(InputSRegisterAt(instruction, 0), InputSRegisterAt(instruction, 1));
    } else {
      DCHECK_EQ(type, Primitive::kPrimDouble);
      __ Vcmp(FromLowSToD(LowSRegisterFrom(lhs_loc)), FromLowSToD(LowSRegisterFrom(rhs_loc)));
    }
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateFPJumps(HCondition* cond,
                                                      vixl32::Label* true_label,
                                                      vixl32::Label* false_label ATTRIBUTE_UNUSED) {
  // To branch on the result of the FP compare we transfer FPSCR to APSR (encoded as PC in VMRS).
  __ Vmrs(RegisterOrAPSR_nzcv(kPcCode), FPSCR);
  __ B(ARMFPCondition(cond->GetCondition(), cond->IsGtBias()), true_label);
}

void InstructionCodeGeneratorARMVIXL::GenerateLongComparesAndJumps(HCondition* cond,
                                                                   vixl32::Label* true_label,
                                                                   vixl32::Label* false_label) {
  LocationSummary* locations = cond->GetLocations();
  Location left = locations->InAt(0);
  Location right = locations->InAt(1);
  IfCondition if_cond = cond->GetCondition();

  vixl32::Register left_high = HighRegisterFrom(left);
  vixl32::Register left_low = LowRegisterFrom(left);
  IfCondition true_high_cond = if_cond;
  IfCondition false_high_cond = cond->GetOppositeCondition();
  vixl32::Condition final_condition = ARMUnsignedCondition(if_cond);  // unsigned on lower part

  // Set the conditions for the test, remembering that == needs to be
  // decided using the low words.
  // TODO: consider avoiding jumps with temporary and CMP low+SBC high
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

    __ Cmp(left_high, val_high);
    if (if_cond == kCondNE) {
      __ B(ARMCondition(true_high_cond), true_label);
    } else if (if_cond == kCondEQ) {
      __ B(ARMCondition(false_high_cond), false_label);
    } else {
      __ B(ARMCondition(true_high_cond), true_label);
      __ B(ARMCondition(false_high_cond), false_label);
    }
    // Must be equal high, so compare the lows.
    __ Cmp(left_low, val_low);
  } else {
    vixl32::Register right_high = HighRegisterFrom(right);
    vixl32::Register right_low = LowRegisterFrom(right);

    __ Cmp(left_high, right_high);
    if (if_cond == kCondNE) {
      __ B(ARMCondition(true_high_cond), true_label);
    } else if (if_cond == kCondEQ) {
      __ B(ARMCondition(false_high_cond), false_label);
    } else {
      __ B(ARMCondition(true_high_cond), true_label);
      __ B(ARMCondition(false_high_cond), false_label);
    }
    // Must be equal high, so compare the lows.
    __ Cmp(left_low, right_low);
  }
  // The last comparison might be unsigned.
  // TODO: optimize cases where this is always true/false
  __ B(final_condition, true_label);
}

void InstructionCodeGeneratorARMVIXL::GenerateCompareTestAndBranch(HCondition* condition,
                                                                   vixl32::Label* true_target_in,
                                                                   vixl32::Label* false_target_in) {
  // Generated branching requires both targets to be explicit. If either of the
  // targets is nullptr (fallthrough) use and bind `fallthrough` instead.
  vixl32::Label fallthrough;
  vixl32::Label* true_target = (true_target_in == nullptr) ? &fallthrough : true_target_in;
  vixl32::Label* false_target = (false_target_in == nullptr) ? &fallthrough : false_target_in;

  Primitive::Type type = condition->InputAt(0)->GetType();
  switch (type) {
    case Primitive::kPrimLong:
      GenerateLongComparesAndJumps(condition, true_target, false_target);
      break;
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      GenerateVcmp(condition);
      GenerateFPJumps(condition, true_target, false_target);
      break;
    default:
      LOG(FATAL) << "Unexpected compare type " << type;
  }

  if (false_target != &fallthrough) {
    __ B(false_target);
  }

  if (true_target_in == nullptr || false_target_in == nullptr) {
    __ Bind(&fallthrough);
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateTestAndBranch(HInstruction* instruction,
                                                            size_t condition_input_index,
                                                            vixl32::Label* true_target,
                                                            vixl32::Label* false_target) {
  HInstruction* cond = instruction->InputAt(condition_input_index);

  if (true_target == nullptr && false_target == nullptr) {
    // Nothing to do. The code always falls through.
    return;
  } else if (cond->IsIntConstant()) {
    // Constant condition, statically compared against "true" (integer value 1).
    if (cond->AsIntConstant()->IsTrue()) {
      if (true_target != nullptr) {
        __ B(true_target);
      }
    } else {
      DCHECK(cond->AsIntConstant()->IsFalse()) << cond->AsIntConstant()->GetValue();
      if (false_target != nullptr) {
        __ B(false_target);
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
    if (kIsDebugBuild) {
      Location cond_val = instruction->GetLocations()->InAt(condition_input_index);
      DCHECK(cond_val.IsRegister());
    }
    if (true_target == nullptr) {
      __ Cbz(InputRegisterAt(instruction, condition_input_index), false_target);
    } else {
      __ Cbnz(InputRegisterAt(instruction, condition_input_index), true_target);
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

    LocationSummary* locations = cond->GetLocations();
    DCHECK(locations->InAt(0).IsRegister());
    vixl32::Register left = InputRegisterAt(cond, 0);
    Location right = locations->InAt(1);
    if (right.IsRegister()) {
      __ Cmp(left, InputRegisterAt(cond, 1));
    } else {
      DCHECK(right.IsConstant());
      __ Cmp(left, CodeGenerator::GetInt32ValueOf(right.GetConstant()));
    }
    if (true_target == nullptr) {
      __ B(ARMCondition(condition->GetOppositeCondition()), false_target);
    } else {
      __ B(ARMCondition(condition->GetCondition()), true_target);
    }
  }

  // If neither branch falls through (case 3), the conditional branch to `true_target`
  // was already emitted (case 2) and we need to emit a jump to `false_target`.
  if (true_target != nullptr && false_target != nullptr) {
    __ B(false_target);
  }
}

void LocationsBuilderARMVIXL::VisitIf(HIf* if_instr) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(if_instr);
  if (IsBooleanValueOrMaterializedCondition(if_instr->InputAt(0))) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorARMVIXL::VisitIf(HIf* if_instr) {
  HBasicBlock* true_successor = if_instr->IfTrueSuccessor();
  HBasicBlock* false_successor = if_instr->IfFalseSuccessor();
  vixl32::Label* true_target = codegen_->GoesToNextBlock(if_instr->GetBlock(), true_successor) ?
      nullptr : codegen_->GetLabelOf(true_successor);
  vixl32::Label* false_target = codegen_->GoesToNextBlock(if_instr->GetBlock(), false_successor) ?
      nullptr : codegen_->GetLabelOf(false_successor);
  GenerateTestAndBranch(if_instr, /* condition_input_index */ 0, true_target, false_target);
}

void LocationsBuilderARMVIXL::VisitSelect(HSelect* select) {
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(select);
  if (Primitive::IsFloatingPointType(select->GetType())) {
    locations->SetInAt(0, Location::RequiresFpuRegister());
    locations->SetInAt(1, Location::RequiresFpuRegister());
  } else {
    locations->SetInAt(0, Location::RequiresRegister());
    locations->SetInAt(1, Location::RequiresRegister());
  }
  if (IsBooleanValueOrMaterializedCondition(select->GetCondition())) {
    locations->SetInAt(2, Location::RequiresRegister());
  }
  locations->SetOut(Location::SameAsFirstInput());
}

void InstructionCodeGeneratorARMVIXL::VisitSelect(HSelect* select) {
  LocationSummary* locations = select->GetLocations();
  vixl32::Label false_target;
  GenerateTestAndBranch(select,
                        /* condition_input_index */ 2,
                        /* true_target */ nullptr,
                        &false_target);
  codegen_->MoveLocation(locations->Out(), locations->InAt(1), select->GetType());
  __ Bind(&false_target);
}

void CodeGeneratorARMVIXL::GenerateNop() {
  __ Nop();
}

void LocationsBuilderARMVIXL::HandleCondition(HCondition* cond) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(cond, LocationSummary::kNoCall);
  // Handle the long/FP comparisons made in instruction simplification.
  switch (cond->InputAt(0)->GetType()) {
    case Primitive::kPrimLong:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(cond->InputAt(1)));
      if (!cond->IsEmittedAtUseSite()) {
        locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
      }
      break;

    // TODO(VIXL): https://android-review.googlesource.com/#/c/252265/
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
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

void InstructionCodeGeneratorARMVIXL::HandleCondition(HCondition* cond) {
  if (cond->IsEmittedAtUseSite()) {
    return;
  }

  vixl32::Register out = OutputRegister(cond);
  vixl32::Label true_label, false_label;

  switch (cond->InputAt(0)->GetType()) {
    default: {
      // Integer case.
      __ Cmp(InputRegisterAt(cond, 0), InputOperandAt(cond, 1));
      AssemblerAccurateScope aas(GetVIXLAssembler(),
                                 kArmInstrMaxSizeInBytes * 3u,
                                 CodeBufferCheckScope::kMaximumSize);
      __ ite(ARMCondition(cond->GetCondition()));
      __ mov(ARMCondition(cond->GetCondition()), OutputRegister(cond), 1);
      __ mov(ARMCondition(cond->GetOppositeCondition()), OutputRegister(cond), 0);
      return;
    }
    case Primitive::kPrimLong:
      GenerateLongComparesAndJumps(cond, &true_label, &false_label);
      break;
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      GenerateVcmp(cond);
      GenerateFPJumps(cond, &true_label, &false_label);
      break;
  }

  // Convert the jumps into the result.
  vixl32::Label done_label;

  // False case: result = 0.
  __ Bind(&false_label);
  __ Mov(out, 0);
  __ B(&done_label);

  // True case: result = 1.
  __ Bind(&true_label);
  __ Mov(out, 1);
  __ Bind(&done_label);
}

void LocationsBuilderARMVIXL::VisitEqual(HEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitEqual(HEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitNotEqual(HNotEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitNotEqual(HNotEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitLessThan(HLessThan* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitLessThan(HLessThan* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitLessThanOrEqual(HLessThanOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitLessThanOrEqual(HLessThanOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitGreaterThan(HGreaterThan* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitGreaterThan(HGreaterThan* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitGreaterThanOrEqual(HGreaterThanOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitGreaterThanOrEqual(HGreaterThanOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitBelow(HBelow* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitBelow(HBelow* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitBelowOrEqual(HBelowOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitBelowOrEqual(HBelowOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitAbove(HAbove* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitAbove(HAbove* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitAboveOrEqual(HAboveOrEqual* comp) {
  HandleCondition(comp);
}

void InstructionCodeGeneratorARMVIXL::VisitAboveOrEqual(HAboveOrEqual* comp) {
  HandleCondition(comp);
}

void LocationsBuilderARMVIXL::VisitIntConstant(HIntConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitIntConstant(HIntConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARMVIXL::VisitNullConstant(HNullConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitNullConstant(HNullConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARMVIXL::VisitLongConstant(HLongConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitLongConstant(HLongConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARMVIXL::VisitFloatConstant(HFloatConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitFloatConstant(HFloatConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARMVIXL::VisitDoubleConstant(HDoubleConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitDoubleConstant(HDoubleConstant* constant ATTRIBUTE_UNUSED) {
  // Will be generated at use site.
}

void LocationsBuilderARMVIXL::VisitMemoryBarrier(HMemoryBarrier* memory_barrier) {
  memory_barrier->SetLocations(nullptr);
}

void InstructionCodeGeneratorARMVIXL::VisitMemoryBarrier(HMemoryBarrier* memory_barrier) {
  codegen_->GenerateMemoryBarrier(memory_barrier->GetBarrierKind());
}

void LocationsBuilderARMVIXL::VisitReturnVoid(HReturnVoid* ret) {
  ret->SetLocations(nullptr);
}

void InstructionCodeGeneratorARMVIXL::VisitReturnVoid(HReturnVoid* ret ATTRIBUTE_UNUSED) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderARMVIXL::VisitReturn(HReturn* ret) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(ret, LocationSummary::kNoCall);
  locations->SetInAt(0, parameter_visitor_.GetReturnLocation(ret->InputAt(0)->GetType()));
}

void InstructionCodeGeneratorARMVIXL::VisitReturn(HReturn* ret ATTRIBUTE_UNUSED) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderARMVIXL::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) {
  // Explicit clinit checks triggered by static invokes must have been pruned by
  // art::PrepareForRegisterAllocation.
  DCHECK(!invoke->IsStaticWithExplicitClinitCheck());

  // TODO(VIXL): TryDispatch

  HandleInvoke(invoke);
}

void InstructionCodeGeneratorARMVIXL::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) {
  // Explicit clinit checks triggered by static invokes must have been pruned by
  // art::PrepareForRegisterAllocation.
  DCHECK(!invoke->IsStaticWithExplicitClinitCheck());

  // TODO(VIXL): TryGenerateIntrinsicCode

  LocationSummary* locations = invoke->GetLocations();
  DCHECK(locations->HasTemps());
  codegen_->GenerateStaticOrDirectCall(invoke, locations->GetTemp(0));
  // TODO(VIXL): If necessary, use a scope to ensure we record the pc info immediately after the
  // previous instruction.
  codegen_->RecordPcInfo(invoke, invoke->GetDexPc());
}

void LocationsBuilderARMVIXL::HandleInvoke(HInvoke* invoke) {
  InvokeDexCallingConventionVisitorARM calling_convention_visitor;
  CodeGenerator::CreateCommonInvokeLocationSummary(invoke, &calling_convention_visitor);
}

void LocationsBuilderARMVIXL::VisitInvokeVirtual(HInvokeVirtual* invoke) {
  // TODO(VIXL): TryDispatch

  HandleInvoke(invoke);
}

void InstructionCodeGeneratorARMVIXL::VisitInvokeVirtual(HInvokeVirtual* invoke) {
  // TODO(VIXL): TryGenerateIntrinsicCode

  codegen_->GenerateVirtualCall(invoke, invoke->GetLocations()->GetTemp(0));
  DCHECK(!codegen_->IsLeafMethod());
  // TODO(VIXL): If necessary, use a scope to ensure we record the pc info immediately after the
  // previous instruction.
  codegen_->RecordPcInfo(invoke, invoke->GetDexPc());
}

void LocationsBuilderARMVIXL::VisitTypeConversion(HTypeConversion* conversion) {
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
          InvokeRuntimeCallingConventionARMVIXL calling_convention;
          locations->SetInAt(0, LocationFrom(calling_convention.GetFpuRegisterAt(0)));
          locations->SetOut(LocationFrom(r0, r1));
          break;
        }

        case Primitive::kPrimDouble: {
          // Processing a Dex `double-to-long' instruction.
          InvokeRuntimeCallingConventionARMVIXL calling_convention;
          locations->SetInAt(0, LocationFrom(calling_convention.GetFpuRegisterAt(0),
                                             calling_convention.GetFpuRegisterAt(1)));
          locations->SetOut(LocationFrom(r0, r1));
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
          InvokeRuntimeCallingConventionARMVIXL calling_convention;
          locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0),
                                             calling_convention.GetRegisterAt(1)));
          locations->SetOut(LocationFrom(calling_convention.GetFpuRegisterAt(0)));
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

void InstructionCodeGeneratorARMVIXL::VisitTypeConversion(HTypeConversion* conversion) {
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
          __ Sbfx(OutputRegister(conversion), LowRegisterFrom(in), 0, 8);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-byte' instruction.
          __ Sbfx(OutputRegister(conversion), InputRegisterAt(conversion, 0), 0, 8);
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
          __ Sbfx(OutputRegister(conversion), LowRegisterFrom(in), 0, 16);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimInt:
        case Primitive::kPrimChar:
          // Processing a Dex `int-to-short' instruction.
          __ Sbfx(OutputRegister(conversion), InputRegisterAt(conversion, 0), 0, 16);
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
            __ Mov(OutputRegister(conversion), LowRegisterFrom(in));
          } else if (in.IsDoubleStackSlot()) {
            GetAssembler()->LoadFromOffset(kLoadWord,
                                           OutputRegister(conversion),
                                           sp,
                                           in.GetStackIndex());
          } else {
            DCHECK(in.IsConstant());
            DCHECK(in.GetConstant()->IsLongConstant());
            int64_t value = in.GetConstant()->AsLongConstant()->GetValue();
            __ Mov(OutputRegister(conversion), static_cast<int32_t>(value));
          }
          break;

        case Primitive::kPrimFloat: {
          // Processing a Dex `float-to-int' instruction.
          vixl32::SRegister temp = LowSRegisterFrom(locations->GetTemp(0));
          __ Vcvt(I32, F32, temp, InputSRegisterAt(conversion, 0));
          __ Vmov(OutputRegister(conversion), temp);
          break;
        }

        case Primitive::kPrimDouble: {
          // Processing a Dex `double-to-int' instruction.
          vixl32::SRegister temp_s = LowSRegisterFrom(locations->GetTemp(0));
          __ Vcvt(I32, F64, temp_s, FromLowSToD(LowSRegisterFrom(in)));
          __ Vmov(OutputRegister(conversion), temp_s);
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
          __ Mov(LowRegisterFrom(out), InputRegisterAt(conversion, 0));
          // Sign extension.
          __ Asr(HighRegisterFrom(out), LowRegisterFrom(out), 31);
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
          __ Ubfx(OutputRegister(conversion), LowRegisterFrom(in), 0, 16);
          break;
        case Primitive::kPrimBoolean:
          // Boolean input is a result of code transformations.
        case Primitive::kPrimByte:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
          // Processing a Dex `int-to-char' instruction.
          __ Ubfx(OutputRegister(conversion), InputRegisterAt(conversion, 0), 0, 16);
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
          __ Vmov(OutputSRegister(conversion), InputRegisterAt(conversion, 0));
          __ Vcvt(F32, I32, OutputSRegister(conversion), OutputSRegister(conversion));
          break;
        }

        case Primitive::kPrimLong:
          // Processing a Dex `long-to-float' instruction.
          codegen_->InvokeRuntime(kQuickL2f, conversion, conversion->GetDexPc());
          CheckEntrypointTypes<kQuickL2f, float, int64_t>();
          break;

        case Primitive::kPrimDouble:
          // Processing a Dex `double-to-float' instruction.
          __ Vcvt(F32, F64, OutputSRegister(conversion), FromLowSToD(LowSRegisterFrom(in)));
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
          __ Vmov(LowSRegisterFrom(out), InputRegisterAt(conversion, 0));
          __ Vcvt(F64, I32, FromLowSToD(LowSRegisterFrom(out)), LowSRegisterFrom(out));
          break;
        }

        case Primitive::kPrimLong: {
          // Processing a Dex `long-to-double' instruction.
          vixl32::Register low = LowRegisterFrom(in);
          vixl32::Register high = HighRegisterFrom(in);

          vixl32::SRegister out_s = LowSRegisterFrom(out);
          vixl32::DRegister out_d = FromLowSToD(out_s);

          vixl32::SRegister temp_s = LowSRegisterFrom(locations->GetTemp(0));
          vixl32::DRegister temp_d = FromLowSToD(temp_s);

          vixl32::SRegister constant_s = LowSRegisterFrom(locations->GetTemp(1));
          vixl32::DRegister constant_d = FromLowSToD(constant_s);

          // temp_d = int-to-double(high)
          __ Vmov(temp_s, high);
          __ Vcvt(F64, I32, temp_d, temp_s);
          // constant_d = k2Pow32EncodingForDouble
          __ Vmov(constant_d, bit_cast<double, int64_t>(k2Pow32EncodingForDouble));
          // out_d = unsigned-to-double(low)
          __ Vmov(out_s, low);
          __ Vcvt(F64, U32, out_d, out_s);
          // out_d += temp_d * constant_d
          __ Vmla(F64, out_d, temp_d, constant_d);
          break;
        }

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-double' instruction.
          __ Vcvt(F64, F32, FromLowSToD(LowSRegisterFrom(out)), InputSRegisterAt(conversion, 0));
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

void LocationsBuilderARMVIXL::VisitAdd(HAdd* add) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(add, LocationSummary::kNoCall);
  switch (add->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(add->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    // TODO(VIXL): https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
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
      LOG(FATAL) << "Unexpected add type " << add->GetResultType();
  }
}

void InstructionCodeGeneratorARMVIXL::VisitAdd(HAdd* add) {
  LocationSummary* locations = add->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  switch (add->GetResultType()) {
    case Primitive::kPrimInt: {
      __ Add(OutputRegister(add), InputRegisterAt(add, 0), InputOperandAt(add, 1));
      }
      break;

    // TODO(VIXL): https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
      DCHECK(second.IsRegisterPair());
      __ Adds(LowRegisterFrom(out), LowRegisterFrom(first), LowRegisterFrom(second));
      __ Adc(HighRegisterFrom(out), HighRegisterFrom(first), HighRegisterFrom(second));
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Vadd(OutputVRegister(add), InputVRegisterAt(add, 0), InputVRegisterAt(add, 1));
      break;

    default:
      LOG(FATAL) << "Unexpected add type " << add->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitSub(HSub* sub) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(sub, LocationSummary::kNoCall);
  switch (sub->GetResultType()) {
    case Primitive::kPrimInt: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(sub->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    // TODO(VIXL): https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
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
      LOG(FATAL) << "Unexpected sub type " << sub->GetResultType();
  }
}

void InstructionCodeGeneratorARMVIXL::VisitSub(HSub* sub) {
  LocationSummary* locations = sub->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  switch (sub->GetResultType()) {
    case Primitive::kPrimInt: {
      __ Sub(OutputRegister(sub), InputRegisterAt(sub, 0), InputOperandAt(sub, 1));
      break;
    }

    // TODO(VIXL): https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
      DCHECK(second.IsRegisterPair());
      __ Subs(LowRegisterFrom(out), LowRegisterFrom(first), LowRegisterFrom(second));
      __ Sbc(HighRegisterFrom(out), HighRegisterFrom(first), HighRegisterFrom(second));
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Vsub(OutputVRegister(sub), InputVRegisterAt(sub, 0), InputVRegisterAt(sub, 1));
      break;

    default:
      LOG(FATAL) << "Unexpected sub type " << sub->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitMul(HMul* mul) {
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

void InstructionCodeGeneratorARMVIXL::VisitMul(HMul* mul) {
  LocationSummary* locations = mul->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);
  switch (mul->GetResultType()) {
    case Primitive::kPrimInt: {
      __ Mul(OutputRegister(mul), InputRegisterAt(mul, 0), InputRegisterAt(mul, 1));
      break;
    }
    case Primitive::kPrimLong: {
      vixl32::Register out_hi = HighRegisterFrom(out);
      vixl32::Register out_lo = LowRegisterFrom(out);
      vixl32::Register in1_hi = HighRegisterFrom(first);
      vixl32::Register in1_lo = LowRegisterFrom(first);
      vixl32::Register in2_hi = HighRegisterFrom(second);
      vixl32::Register in2_lo = LowRegisterFrom(second);

      // Extra checks to protect caused by the existence of R1_R2.
      // The algorithm is wrong if out.hi is either in1.lo or in2.lo:
      // (e.g. in1=r0_r1, in2=r2_r3 and out=r1_r2);
      DCHECK_NE(out_hi.GetCode(), in1_lo.GetCode());
      DCHECK_NE(out_hi.GetCode(), in2_lo.GetCode());

      // input: in1 - 64 bits, in2 - 64 bits
      // output: out
      // formula: out.hi : out.lo = (in1.lo * in2.hi + in1.hi * in2.lo)* 2^32 + in1.lo * in2.lo
      // parts: out.hi = in1.lo * in2.hi + in1.hi * in2.lo + (in1.lo * in2.lo)[63:32]
      // parts: out.lo = (in1.lo * in2.lo)[31:0]

      UseScratchRegisterScope temps(GetVIXLAssembler());
      vixl32::Register temp = temps.Acquire();
      // temp <- in1.lo * in2.hi
      __ Mul(temp, in1_lo, in2_hi);
      // out.hi <- in1.lo * in2.hi + in1.hi * in2.lo
      __ Mla(out_hi, in1_hi, in2_lo, temp);
      // out.lo <- (in1.lo * in2.lo)[31:0];
      __ Umull(out_lo, temp, in1_lo, in2_lo);
      // out.hi <- in2.hi * in1.lo +  in2.lo * in1.hi + (in1.lo * in2.lo)[63:32]
      __ Add(out_hi, out_hi, temp);
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Vmul(OutputVRegister(mul), InputVRegisterAt(mul, 0), InputVRegisterAt(mul, 1));
      break;

    default:
      LOG(FATAL) << "Unexpected mul type " << mul->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitNewArray(HNewArray* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->AddTemp(LocationFrom(calling_convention.GetRegisterAt(0)));
  locations->SetOut(LocationFrom(r0));
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(2)));
}

void InstructionCodeGeneratorARMVIXL::VisitNewArray(HNewArray* instruction) {
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  __ Mov(calling_convention.GetRegisterAt(0), instruction->GetTypeIndex());
  // Note: if heap poisoning is enabled, the entry point takes cares
  // of poisoning the reference.
  codegen_->InvokeRuntime(instruction->GetEntrypoint(), instruction, instruction->GetDexPc());
  CheckEntrypointTypes<kQuickAllocArrayWithAccessCheck, void*, uint32_t, int32_t, ArtMethod*>();
}

void LocationsBuilderARMVIXL::VisitNewInstance(HNewInstance* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  if (instruction->IsStringAlloc()) {
    locations->AddTemp(LocationFrom(kMethodRegister));
  } else {
    InvokeRuntimeCallingConventionARMVIXL calling_convention;
    locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
    locations->SetInAt(1, LocationFrom(calling_convention.GetRegisterAt(1)));
  }
  locations->SetOut(LocationFrom(r0));
}

void InstructionCodeGeneratorARMVIXL::VisitNewInstance(HNewInstance* instruction) {
  // Note: if heap poisoning is enabled, the entry point takes cares
  // of poisoning the reference.
  if (instruction->IsStringAlloc()) {
    // String is allocated through StringFactory. Call NewEmptyString entry point.
    vixl32::Register temp = RegisterFrom(instruction->GetLocations()->GetTemp(0));
    MemberOffset code_offset = ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArmPointerSize);
    GetAssembler()->LoadFromOffset(kLoadWord, temp, tr, QUICK_ENTRY_POINT(pNewEmptyString));
    GetAssembler()->LoadFromOffset(kLoadWord, lr, temp, code_offset.Int32Value());
    AssemblerAccurateScope aas(GetVIXLAssembler(),
                               kArmInstrMaxSizeInBytes,
                               CodeBufferCheckScope::kMaximumSize);
    __ blx(lr);
    codegen_->RecordPcInfo(instruction, instruction->GetDexPc());
  } else {
    codegen_->InvokeRuntime(instruction->GetEntrypoint(), instruction, instruction->GetDexPc());
    CheckEntrypointTypes<kQuickAllocObjectWithAccessCheck, void*, uint32_t, ArtMethod*>();
  }
}

void LocationsBuilderARMVIXL::VisitParameterValue(HParameterValue* instruction) {
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

void InstructionCodeGeneratorARMVIXL::VisitParameterValue(
    HParameterValue* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, the parameter is already at its location.
}

void LocationsBuilderARMVIXL::VisitCurrentMethod(HCurrentMethod* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(LocationFrom(kMethodRegister));
}

void InstructionCodeGeneratorARMVIXL::VisitCurrentMethod(
    HCurrentMethod* instruction ATTRIBUTE_UNUSED) {
  // Nothing to do, the method is already at its location.
}

void LocationsBuilderARMVIXL::VisitNot(HNot* not_) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(not_, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARMVIXL::VisitNot(HNot* not_) {
  LocationSummary* locations = not_->GetLocations();
  Location out = locations->Out();
  Location in = locations->InAt(0);
  switch (not_->GetResultType()) {
    case Primitive::kPrimInt:
      __ Mvn(OutputRegister(not_), InputRegisterAt(not_, 0));
      break;

    case Primitive::kPrimLong:
      __ Mvn(LowRegisterFrom(out), LowRegisterFrom(in));
      __ Mvn(HighRegisterFrom(out), HighRegisterFrom(in));
      break;

    default:
      LOG(FATAL) << "Unimplemented type for not operation " << not_->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitCompare(HCompare* compare) {
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

void InstructionCodeGeneratorARMVIXL::VisitCompare(HCompare* compare) {
  LocationSummary* locations = compare->GetLocations();
  vixl32::Register out = OutputRegister(compare);
  Location left = locations->InAt(0);
  Location right = locations->InAt(1);

  vixl32::Label less, greater, done;
  Primitive::Type type = compare->InputAt(0)->GetType();
  vixl32::Condition less_cond = vixl32::Condition(kNone);
  switch (type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
    case Primitive::kPrimShort:
    case Primitive::kPrimChar:
    case Primitive::kPrimInt: {
      // Emit move to `out` before the `Cmp`, as `Mov` might affect the status flags.
      __ Mov(out, 0);
      __ Cmp(RegisterFrom(left), RegisterFrom(right));  // Signed compare.
      less_cond = lt;
      break;
    }
    case Primitive::kPrimLong: {
      __ Cmp(HighRegisterFrom(left), HighRegisterFrom(right));  // Signed compare.
      __ B(lt, &less);
      __ B(gt, &greater);
      // Emit move to `out` before the last `Cmp`, as `Mov` might affect the status flags.
      __ Mov(out, 0);
      __ Cmp(LowRegisterFrom(left), LowRegisterFrom(right));  // Unsigned compare.
      less_cond = lo;
      break;
    }
    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble: {
      __ Mov(out, 0);
      GenerateVcmp(compare);
      // To branch on the FP compare result we transfer FPSCR to APSR (encoded as PC in VMRS).
      __ Vmrs(RegisterOrAPSR_nzcv(kPcCode), FPSCR);
      less_cond = ARMFPCondition(kCondLT, compare->IsGtBias());
      break;
    }
    default:
      LOG(FATAL) << "Unexpected compare type " << type;
      UNREACHABLE();
  }

  __ B(eq, &done);
  __ B(less_cond, &less);

  __ Bind(&greater);
  __ Mov(out, 1);
  __ B(&done);

  __ Bind(&less);
  __ Mov(out, -1);

  __ Bind(&done);
}

void LocationsBuilderARMVIXL::VisitPhi(HPhi* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  for (size_t i = 0, e = locations->GetInputCount(); i < e; ++i) {
    locations->SetInAt(i, Location::Any());
  }
  locations->SetOut(Location::Any());
}

void InstructionCodeGeneratorARMVIXL::VisitPhi(HPhi* instruction ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unreachable";
}

void CodeGeneratorARMVIXL::GenerateMemoryBarrier(MemBarrierKind kind) {
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
  __ Dmb(flavor);
}

void InstructionCodeGeneratorARMVIXL::GenerateWideAtomicLoad(vixl32::Register addr,
                                                             uint32_t offset,
                                                             vixl32::Register out_lo,
                                                             vixl32::Register out_hi) {
  UseScratchRegisterScope temps(GetVIXLAssembler());
  if (offset != 0) {
    vixl32::Register temp = temps.Acquire();
    __ Add(temp, addr, offset);
    addr = temp;
  }
  __ Ldrexd(out_lo, out_hi, addr);
}

void InstructionCodeGeneratorARMVIXL::GenerateWideAtomicStore(vixl32::Register addr,
                                                              uint32_t offset,
                                                              vixl32::Register value_lo,
                                                              vixl32::Register value_hi,
                                                              vixl32::Register temp1,
                                                              vixl32::Register temp2,
                                                              HInstruction* instruction) {
  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Label fail;
  if (offset != 0) {
    vixl32::Register temp = temps.Acquire();
    __ Add(temp, addr, offset);
    addr = temp;
  }
  __ Bind(&fail);
  // We need a load followed by store. (The address used in a STREX instruction must
  // be the same as the address in the most recently executed LDREX instruction.)
  __ Ldrexd(temp1, temp2, addr);
  codegen_->MaybeRecordImplicitNullCheck(instruction);
  __ Strexd(temp1, value_lo, value_hi, addr);
  __ Cbnz(temp1, &fail);
}

void InstructionCodeGeneratorARMVIXL::DivRemOneOrMinusOne(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  Location second = instruction->GetLocations()->InAt(1);
  DCHECK(second.IsConstant());

  vixl32::Register out = OutputRegister(instruction);
  vixl32::Register dividend = InputRegisterAt(instruction, 0);
  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  DCHECK(imm == 1 || imm == -1);

  if (instruction->IsRem()) {
    __ Mov(out, 0);
  } else {
    if (imm == 1) {
      __ Mov(out, dividend);
    } else {
      __ Rsb(out, dividend, 0);
    }
  }
}

void InstructionCodeGeneratorARMVIXL::DivRemByPowerOfTwo(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  vixl32::Register out = OutputRegister(instruction);
  vixl32::Register dividend = InputRegisterAt(instruction, 0);
  vixl32::Register temp = RegisterFrom(locations->GetTemp(0));
  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  uint32_t abs_imm = static_cast<uint32_t>(AbsOrMin(imm));
  int ctz_imm = CTZ(abs_imm);

  if (ctz_imm == 1) {
    __ Lsr(temp, dividend, 32 - ctz_imm);
  } else {
    __ Asr(temp, dividend, 31);
    __ Lsr(temp, temp, 32 - ctz_imm);
  }
  __ Add(out, temp, dividend);

  if (instruction->IsDiv()) {
    __ Asr(out, out, ctz_imm);
    if (imm < 0) {
      __ Rsb(out, out, 0);
    }
  } else {
    __ Ubfx(out, out, 0, ctz_imm);
    __ Sub(out, out, temp);
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateDivRemWithAnyConstant(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
  DCHECK(second.IsConstant());

  vixl32::Register out = OutputRegister(instruction);
  vixl32::Register dividend = InputRegisterAt(instruction, 0);
  vixl32::Register temp1 = RegisterFrom(locations->GetTemp(0));
  vixl32::Register temp2 = RegisterFrom(locations->GetTemp(1));
  int64_t imm = second.GetConstant()->AsIntConstant()->GetValue();

  int64_t magic;
  int shift;
  CalculateMagicAndShiftForDivRem(imm, false /* is_long */, &magic, &shift);

  __ Mov(temp1, magic);
  __ Smull(temp2, temp1, dividend, temp1);

  if (imm > 0 && magic < 0) {
    __ Add(temp1, temp1, dividend);
  } else if (imm < 0 && magic > 0) {
    __ Sub(temp1, temp1, dividend);
  }

  if (shift != 0) {
    __ Asr(temp1, temp1, shift);
  }

  if (instruction->IsDiv()) {
    __ Sub(out, temp1, Operand(temp1, vixl32::Shift(ASR), 31));
  } else {
    __ Sub(temp1, temp1, Operand(temp1, vixl32::Shift(ASR), 31));
    // TODO: Strength reduction for mls.
    __ Mov(temp2, imm);
    __ Mls(out, temp1, temp2, dividend);
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateDivRemConstantIntegral(
    HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  Location second = instruction->GetLocations()->InAt(1);
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

void LocationsBuilderARMVIXL::VisitDiv(HDiv* div) {
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
        TODO_VIXL32(FATAL);
      }
      break;
    }
    case Primitive::kPrimLong: {
      TODO_VIXL32(FATAL);
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

void InstructionCodeGeneratorARMVIXL::VisitDiv(HDiv* div) {
  Location rhs = div->GetLocations()->InAt(1);

  switch (div->GetResultType()) {
    case Primitive::kPrimInt: {
      if (rhs.IsConstant()) {
        GenerateDivRemConstantIntegral(div);
      } else if (codegen_->GetInstructionSetFeatures().HasDivideInstruction()) {
        __ Sdiv(OutputRegister(div), InputRegisterAt(div, 0), InputRegisterAt(div, 1));
      } else {
        TODO_VIXL32(FATAL);
      }
      break;
    }

    case Primitive::kPrimLong: {
      TODO_VIXL32(FATAL);
      break;
    }

    case Primitive::kPrimFloat:
    case Primitive::kPrimDouble:
      __ Vdiv(OutputVRegister(div), InputVRegisterAt(div, 0), InputVRegisterAt(div, 1));
      break;

    default:
      LOG(FATAL) << "Unexpected div type " << div->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  // TODO(VIXL): https://android-review.googlesource.com/#/c/275337/
  LocationSummary::CallKind call_kind = instruction->CanThrowIntoCatchBlock()
      ? LocationSummary::kCallOnSlowPath
      : LocationSummary::kNoCall;
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction, call_kind);
  locations->SetInAt(0, Location::RegisterOrConstant(instruction->InputAt(0)));
  if (instruction->HasUses()) {
    locations->SetOut(Location::SameAsFirstInput());
  }
}

void InstructionCodeGeneratorARMVIXL::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  DivZeroCheckSlowPathARMVIXL* slow_path =
      new (GetGraph()->GetArena()) DivZeroCheckSlowPathARMVIXL(instruction);
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
        __ Cbz(InputRegisterAt(instruction, 0), slow_path->GetEntryLabel());
      } else {
        DCHECK(value.IsConstant()) << value;
        if (value.GetConstant()->AsIntConstant()->GetValue() == 0) {
          __ B(slow_path->GetEntryLabel());
        }
      }
      break;
    }
    case Primitive::kPrimLong: {
      if (value.IsRegisterPair()) {
        UseScratchRegisterScope temps(GetVIXLAssembler());
        vixl32::Register temp = temps.Acquire();
        __ Orrs(temp, LowRegisterFrom(value), HighRegisterFrom(value));
        __ B(eq, slow_path->GetEntryLabel());
      } else {
        DCHECK(value.IsConstant()) << value;
        if (value.GetConstant()->AsLongConstant()->GetValue() == 0) {
          __ B(slow_path->GetEntryLabel());
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected type for HDivZeroCheck " << instruction->GetType();
  }
}

void LocationsBuilderARMVIXL::HandleFieldSet(
    HInstruction* instruction, const FieldInfo& field_info) {
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
      locations->AddTemp(LocationFrom(r2));
      locations->AddTemp(LocationFrom(r3));
    }
  }
}

void InstructionCodeGeneratorARMVIXL::HandleFieldSet(HInstruction* instruction,
                                                     const FieldInfo& field_info,
                                                     bool value_can_be_null) {
  DCHECK(instruction->IsInstanceFieldSet() || instruction->IsStaticFieldSet());

  LocationSummary* locations = instruction->GetLocations();
  vixl32::Register base = InputRegisterAt(instruction, 0);
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
      GetAssembler()->StoreToOffset(kStoreByte, RegisterFrom(value), base, offset);
      break;
    }

    case Primitive::kPrimShort:
    case Primitive::kPrimChar: {
      GetAssembler()->StoreToOffset(kStoreHalfword, RegisterFrom(value), base, offset);
      break;
    }

    case Primitive::kPrimInt:
    case Primitive::kPrimNot: {
      if (kPoisonHeapReferences && needs_write_barrier) {
        // Note that in the case where `value` is a null reference,
        // we do not enter this block, as a null reference does not
        // need poisoning.
        DCHECK_EQ(field_type, Primitive::kPrimNot);
        vixl32::Register temp = RegisterFrom(locations->GetTemp(0));
        __ Mov(temp, RegisterFrom(value));
        GetAssembler()->PoisonHeapReference(temp);
        GetAssembler()->StoreToOffset(kStoreWord, temp, base, offset);
      } else {
        GetAssembler()->StoreToOffset(kStoreWord, RegisterFrom(value), base, offset);
      }
      break;
    }

    case Primitive::kPrimLong: {
      if (is_volatile && !atomic_ldrd_strd) {
        GenerateWideAtomicStore(base,
                                offset,
                                LowRegisterFrom(value),
                                HighRegisterFrom(value),
                                RegisterFrom(locations->GetTemp(0)),
                                RegisterFrom(locations->GetTemp(1)),
                                instruction);
      } else {
        GetAssembler()->StoreToOffset(kStoreWordPair, LowRegisterFrom(value), base, offset);
        codegen_->MaybeRecordImplicitNullCheck(instruction);
      }
      break;
    }

    case Primitive::kPrimFloat: {
      GetAssembler()->StoreSToOffset(SRegisterFrom(value), base, offset);
      break;
    }

    case Primitive::kPrimDouble: {
      vixl32::DRegister value_reg = FromLowSToD(LowSRegisterFrom(value));
      if (is_volatile && !atomic_ldrd_strd) {
        vixl32::Register value_reg_lo = RegisterFrom(locations->GetTemp(0));
        vixl32::Register value_reg_hi = RegisterFrom(locations->GetTemp(1));

        __ Vmov(value_reg_lo, value_reg_hi, value_reg);

        GenerateWideAtomicStore(base,
                                offset,
                                value_reg_lo,
                                value_reg_hi,
                                RegisterFrom(locations->GetTemp(2)),
                                RegisterFrom(locations->GetTemp(3)),
                                instruction);
      } else {
        GetAssembler()->StoreDToOffset(value_reg, base, offset);
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
    vixl32::Register temp = RegisterFrom(locations->GetTemp(0));
    vixl32::Register card = RegisterFrom(locations->GetTemp(1));
    codegen_->MarkGCCard(temp, card, base, RegisterFrom(value), value_can_be_null);
  }

  if (is_volatile) {
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }
}

Location LocationsBuilderARMVIXL::ArithmeticZeroOrFpuRegister(HInstruction* input) {
  DCHECK(Primitive::IsFloatingPointType(input->GetType())) << input->GetType();
  if ((input->IsFloatConstant() && (input->AsFloatConstant()->IsArithmeticZero())) ||
      (input->IsDoubleConstant() && (input->AsDoubleConstant()->IsArithmeticZero()))) {
    return Location::ConstantLocation(input->AsConstant());
  } else {
    return Location::RequiresFpuRegister();
  }
}

void LocationsBuilderARMVIXL::HandleFieldGet(HInstruction* instruction,
                                             const FieldInfo& field_info) {
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

void InstructionCodeGeneratorARMVIXL::HandleFieldGet(HInstruction* instruction,
                                                     const FieldInfo& field_info) {
  DCHECK(instruction->IsInstanceFieldGet() || instruction->IsStaticFieldGet());

  LocationSummary* locations = instruction->GetLocations();
  vixl32::Register base = InputRegisterAt(instruction, 0);
  Location out = locations->Out();
  bool is_volatile = field_info.IsVolatile();
  bool atomic_ldrd_strd = codegen_->GetInstructionSetFeatures().HasAtomicLdrdAndStrd();
  Primitive::Type field_type = field_info.GetFieldType();
  uint32_t offset = field_info.GetFieldOffset().Uint32Value();

  switch (field_type) {
    case Primitive::kPrimBoolean:
      GetAssembler()->LoadFromOffset(kLoadUnsignedByte, RegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimByte:
      GetAssembler()->LoadFromOffset(kLoadSignedByte, RegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimShort:
      GetAssembler()->LoadFromOffset(kLoadSignedHalfword, RegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimChar:
      GetAssembler()->LoadFromOffset(kLoadUnsignedHalfword, RegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimInt:
      GetAssembler()->LoadFromOffset(kLoadWord, RegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimNot: {
      // /* HeapReference<Object> */ out = *(base + offset)
      if (kEmitCompilerReadBarrier && kUseBakerReadBarrier) {
        TODO_VIXL32(FATAL);
      } else {
        GetAssembler()->LoadFromOffset(kLoadWord, RegisterFrom(out), base, offset);
        // TODO(VIXL): Scope to guarantee the position immediately after the load.
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        if (is_volatile) {
          codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
        }
        // If read barriers are enabled, emit read barriers other than
        // Baker's using a slow path (and also unpoison the loaded
        // reference, if heap poisoning is enabled).
        codegen_->MaybeGenerateReadBarrierSlow(instruction, out, out, locations->InAt(0), offset);
      }
      break;
    }

    case Primitive::kPrimLong:
      if (is_volatile && !atomic_ldrd_strd) {
        GenerateWideAtomicLoad(base, offset, LowRegisterFrom(out), HighRegisterFrom(out));
      } else {
        GetAssembler()->LoadFromOffset(kLoadWordPair, LowRegisterFrom(out), base, offset);
      }
      break;

    case Primitive::kPrimFloat:
      GetAssembler()->LoadSFromOffset(SRegisterFrom(out), base, offset);
      break;

    case Primitive::kPrimDouble: {
      vixl32::DRegister out_dreg = FromLowSToD(LowSRegisterFrom(out));
      if (is_volatile && !atomic_ldrd_strd) {
        vixl32::Register lo = RegisterFrom(locations->GetTemp(0));
        vixl32::Register hi = RegisterFrom(locations->GetTemp(1));
        GenerateWideAtomicLoad(base, offset, lo, hi);
        // TODO(VIXL): Do we need to be immediately after the ldrexd instruction? If so we need a
        // scope.
        codegen_->MaybeRecordImplicitNullCheck(instruction);
        __ Vmov(out_dreg, lo, hi);
      } else {
        GetAssembler()->LoadDFromOffset(out_dreg, base, offset);
        // TODO(VIXL): Scope to guarantee the position immediately after the load.
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
    // Address cases other than reference and double that may require an implicit null check.
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

void LocationsBuilderARMVIXL::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARMVIXL::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  HandleFieldSet(instruction, instruction->GetFieldInfo(), instruction->GetValueCanBeNull());
}

void LocationsBuilderARMVIXL::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARMVIXL::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void LocationsBuilderARMVIXL::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void InstructionCodeGeneratorARMVIXL::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  HandleFieldGet(instruction, instruction->GetFieldInfo());
}

void LocationsBuilderARMVIXL::VisitNullCheck(HNullCheck* instruction) {
  // TODO(VIXL): https://android-review.googlesource.com/#/c/275337/
  LocationSummary::CallKind call_kind = instruction->CanThrowIntoCatchBlock()
      ? LocationSummary::kCallOnSlowPath
      : LocationSummary::kNoCall;
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(instruction, call_kind);
  locations->SetInAt(0, Location::RequiresRegister());
  if (instruction->HasUses()) {
    locations->SetOut(Location::SameAsFirstInput());
  }
}

void CodeGeneratorARMVIXL::GenerateImplicitNullCheck(HNullCheck* instruction) {
  if (CanMoveNullCheckToUser(instruction)) {
    return;
  }

  UseScratchRegisterScope temps(GetVIXLAssembler());
  AssemblerAccurateScope aas(GetVIXLAssembler(),
                             kArmInstrMaxSizeInBytes,
                             CodeBufferCheckScope::kMaximumSize);
  __ ldr(temps.Acquire(), MemOperand(InputRegisterAt(instruction, 0)));
  RecordPcInfo(instruction, instruction->GetDexPc());
}

void CodeGeneratorARMVIXL::GenerateExplicitNullCheck(HNullCheck* instruction) {
  NullCheckSlowPathARMVIXL* slow_path =
      new (GetGraph()->GetArena()) NullCheckSlowPathARMVIXL(instruction);
  AddSlowPath(slow_path);
  __ Cbz(InputRegisterAt(instruction, 0), slow_path->GetEntryLabel());
}

void InstructionCodeGeneratorARMVIXL::VisitNullCheck(HNullCheck* instruction) {
  codegen_->GenerateNullCheck(instruction);
}

void LocationsBuilderARMVIXL::VisitArrayLength(HArrayLength* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorARMVIXL::VisitArrayLength(HArrayLength* instruction) {
  uint32_t offset = CodeGenerator::GetArrayLengthOffset(instruction);
  vixl32::Register obj = InputRegisterAt(instruction, 0);
  vixl32::Register out = OutputRegister(instruction);
  GetAssembler()->LoadFromOffset(kLoadWord, out, obj, offset);
  codegen_->MaybeRecordImplicitNullCheck(instruction);
  // TODO(VIXL): https://android-review.googlesource.com/#/c/272625/
}

void CodeGeneratorARMVIXL::MarkGCCard(vixl32::Register temp,
                                      vixl32::Register card,
                                      vixl32::Register object,
                                      vixl32::Register value,
                                      bool can_be_null) {
  vixl32::Label is_null;
  if (can_be_null) {
    __ Cbz(value, &is_null);
  }
  GetAssembler()->LoadFromOffset(
      kLoadWord, card, tr, Thread::CardTableOffset<kArmPointerSize>().Int32Value());
  __ Lsr(temp, object, gc::accounting::CardTable::kCardShift);
  __ Strb(card, MemOperand(card, temp));
  if (can_be_null) {
    __ Bind(&is_null);
  }
}

void LocationsBuilderARMVIXL::VisitParallelMove(HParallelMove* instruction ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorARMVIXL::VisitParallelMove(HParallelMove* instruction) {
  codegen_->GetMoveResolver()->EmitNativeCode(instruction);
}

void LocationsBuilderARMVIXL::VisitSuspendCheck(HSuspendCheck* instruction) {
  new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnSlowPath);
  // TODO(VIXL): https://android-review.googlesource.com/#/c/275337/ and related.
}

void InstructionCodeGeneratorARMVIXL::VisitSuspendCheck(HSuspendCheck* instruction) {
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

void InstructionCodeGeneratorARMVIXL::GenerateSuspendCheck(HSuspendCheck* instruction,
                                                           HBasicBlock* successor) {
  SuspendCheckSlowPathARMVIXL* slow_path =
      down_cast<SuspendCheckSlowPathARMVIXL*>(instruction->GetSlowPath());
  if (slow_path == nullptr) {
    slow_path = new (GetGraph()->GetArena()) SuspendCheckSlowPathARMVIXL(instruction, successor);
    instruction->SetSlowPath(slow_path);
    codegen_->AddSlowPath(slow_path);
    if (successor != nullptr) {
      DCHECK(successor->IsLoopHeader());
      codegen_->ClearSpillSlotsFromLoopPhisInStackMap(instruction);
    }
  } else {
    DCHECK_EQ(slow_path->GetSuccessor(), successor);
  }

  UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  GetAssembler()->LoadFromOffset(
      kLoadUnsignedHalfword, temp, tr, Thread::ThreadFlagsOffset<kArmPointerSize>().Int32Value());
  if (successor == nullptr) {
    __ Cbnz(temp, slow_path->GetEntryLabel());
    __ Bind(slow_path->GetReturnLabel());
  } else {
    __ Cbz(temp, codegen_->GetLabelOf(successor));
    __ B(slow_path->GetEntryLabel());
  }
}

ArmVIXLAssembler* ParallelMoveResolverARMVIXL::GetAssembler() const {
  return codegen_->GetAssembler();
}

void ParallelMoveResolverARMVIXL::EmitMove(size_t index) {
  UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
  MoveOperands* move = moves_[index];
  Location source = move->GetSource();
  Location destination = move->GetDestination();

  if (source.IsRegister()) {
    if (destination.IsRegister()) {
      __ Mov(RegisterFrom(destination), RegisterFrom(source));
    } else if (destination.IsFpuRegister()) {
      __ Vmov(SRegisterFrom(destination), RegisterFrom(source));
    } else {
      DCHECK(destination.IsStackSlot());
      GetAssembler()->StoreToOffset(kStoreWord,
                                    RegisterFrom(source),
                                    sp,
                                    destination.GetStackIndex());
    }
  } else if (source.IsStackSlot()) {
    if (destination.IsRegister()) {
      GetAssembler()->LoadFromOffset(kLoadWord,
                                     RegisterFrom(destination),
                                     sp,
                                     source.GetStackIndex());
    } else if (destination.IsFpuRegister()) {
      GetAssembler()->LoadSFromOffset(SRegisterFrom(destination), sp, source.GetStackIndex());
    } else {
      DCHECK(destination.IsStackSlot());
      vixl32::Register temp = temps.Acquire();
      GetAssembler()->LoadFromOffset(kLoadWord, temp, sp, source.GetStackIndex());
      GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
    }
  } else if (source.IsFpuRegister()) {
    if (destination.IsRegister()) {
      TODO_VIXL32(FATAL);
    } else if (destination.IsFpuRegister()) {
      __ Vmov(SRegisterFrom(destination), SRegisterFrom(source));
    } else {
      DCHECK(destination.IsStackSlot());
      GetAssembler()->StoreSToOffset(SRegisterFrom(source), sp, destination.GetStackIndex());
    }
  } else if (source.IsDoubleStackSlot()) {
    if (destination.IsDoubleStackSlot()) {
      vixl32::DRegister temp = temps.AcquireD();
      GetAssembler()->LoadDFromOffset(temp, sp, source.GetStackIndex());
      GetAssembler()->StoreDToOffset(temp, sp, destination.GetStackIndex());
    } else if (destination.IsRegisterPair()) {
      DCHECK(ExpectedPairLayout(destination));
      GetAssembler()->LoadFromOffset(
          kLoadWordPair, LowRegisterFrom(destination), sp, source.GetStackIndex());
    } else {
      DCHECK(destination.IsFpuRegisterPair()) << destination;
      GetAssembler()->LoadDFromOffset(DRegisterFrom(destination), sp, source.GetStackIndex());
    }
  } else if (source.IsRegisterPair()) {
    if (destination.IsRegisterPair()) {
      __ Mov(LowRegisterFrom(destination), LowRegisterFrom(source));
      __ Mov(HighRegisterFrom(destination), HighRegisterFrom(source));
    } else if (destination.IsFpuRegisterPair()) {
      __ Vmov(FromLowSToD(LowSRegisterFrom(destination)),
              LowRegisterFrom(source),
              HighRegisterFrom(source));
    } else {
      DCHECK(destination.IsDoubleStackSlot()) << destination;
      DCHECK(ExpectedPairLayout(source));
      GetAssembler()->StoreToOffset(kStoreWordPair,
                                    LowRegisterFrom(source),
                                    sp,
                                    destination.GetStackIndex());
    }
  } else if (source.IsFpuRegisterPair()) {
    if (destination.IsRegisterPair()) {
      TODO_VIXL32(FATAL);
    } else if (destination.IsFpuRegisterPair()) {
      __ Vmov(DRegisterFrom(destination), DRegisterFrom(source));
    } else {
      DCHECK(destination.IsDoubleStackSlot()) << destination;
      GetAssembler()->StoreDToOffset(DRegisterFrom(source), sp, destination.GetStackIndex());
    }
  } else {
    DCHECK(source.IsConstant()) << source;
    HConstant* constant = source.GetConstant();
    if (constant->IsIntConstant() || constant->IsNullConstant()) {
      int32_t value = CodeGenerator::GetInt32ValueOf(constant);
      if (destination.IsRegister()) {
        __ Mov(RegisterFrom(destination), value);
      } else {
        DCHECK(destination.IsStackSlot());
        vixl32::Register temp = temps.Acquire();
        __ Mov(temp, value);
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
      }
    } else if (constant->IsLongConstant()) {
      int64_t value = constant->AsLongConstant()->GetValue();
      if (destination.IsRegisterPair()) {
        __ Mov(LowRegisterFrom(destination), Low32Bits(value));
        __ Mov(HighRegisterFrom(destination), High32Bits(value));
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        vixl32::Register temp = temps.Acquire();
        __ Mov(temp, Low32Bits(value));
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
        __ Mov(temp, High32Bits(value));
        GetAssembler()->StoreToOffset(kStoreWord,
                                      temp,
                                      sp,
                                      destination.GetHighStackIndex(kArmWordSize));
      }
    } else if (constant->IsDoubleConstant()) {
      double value = constant->AsDoubleConstant()->GetValue();
      if (destination.IsFpuRegisterPair()) {
        __ Vmov(FromLowSToD(LowSRegisterFrom(destination)), value);
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        uint64_t int_value = bit_cast<uint64_t, double>(value);
        vixl32::Register temp = temps.Acquire();
        __ Mov(temp, Low32Bits(int_value));
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
        __ Mov(temp, High32Bits(int_value));
        GetAssembler()->StoreToOffset(kStoreWord,
                                      temp,
                                      sp,
                                      destination.GetHighStackIndex(kArmWordSize));
      }
    } else {
      DCHECK(constant->IsFloatConstant()) << constant->DebugName();
      float value = constant->AsFloatConstant()->GetValue();
      if (destination.IsFpuRegister()) {
        __ Vmov(SRegisterFrom(destination), value);
      } else {
        DCHECK(destination.IsStackSlot());
        vixl32::Register temp = temps.Acquire();
        __ Mov(temp, bit_cast<int32_t, float>(value));
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
      }
    }
  }
}

void ParallelMoveResolverARMVIXL::Exchange(vixl32::Register reg, int mem) {
  UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  __ Mov(temp, reg);
  GetAssembler()->LoadFromOffset(kLoadWord, reg, sp, mem);
  GetAssembler()->StoreToOffset(kStoreWord, temp, sp, mem);
}

void ParallelMoveResolverARMVIXL::Exchange(int mem1, int mem2) {
  // TODO(VIXL32): Double check the performance of this implementation.
  UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  vixl32::SRegister temp_s = temps.AcquireS();

  __ Ldr(temp, MemOperand(sp, mem1));
  __ Vldr(temp_s, MemOperand(sp, mem2));
  __ Str(temp, MemOperand(sp, mem2));
  __ Vstr(temp_s, MemOperand(sp, mem1));
}

void ParallelMoveResolverARMVIXL::EmitSwap(size_t index) {
  MoveOperands* move = moves_[index];
  Location source = move->GetSource();
  Location destination = move->GetDestination();
  UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());

  if (source.IsRegister() && destination.IsRegister()) {
    vixl32::Register temp = temps.Acquire();
    DCHECK(!RegisterFrom(source).Is(temp));
    DCHECK(!RegisterFrom(destination).Is(temp));
    __ Mov(temp, RegisterFrom(destination));
    __ Mov(RegisterFrom(destination), RegisterFrom(source));
    __ Mov(RegisterFrom(source), temp);
  } else if (source.IsRegister() && destination.IsStackSlot()) {
    Exchange(RegisterFrom(source), destination.GetStackIndex());
  } else if (source.IsStackSlot() && destination.IsRegister()) {
    Exchange(RegisterFrom(destination), source.GetStackIndex());
  } else if (source.IsStackSlot() && destination.IsStackSlot()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsFpuRegister() && destination.IsFpuRegister()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsRegisterPair() && destination.IsRegisterPair()) {
    vixl32::DRegister temp = temps.AcquireD();
    __ Vmov(temp, LowRegisterFrom(source), HighRegisterFrom(source));
    __ Mov(LowRegisterFrom(source), LowRegisterFrom(destination));
    __ Mov(HighRegisterFrom(source), HighRegisterFrom(destination));
    __ Vmov(LowRegisterFrom(destination), HighRegisterFrom(destination), temp);
  } else if (source.IsRegisterPair() || destination.IsRegisterPair()) {
    vixl32::Register low_reg = LowRegisterFrom(source.IsRegisterPair() ? source : destination);
    int mem = source.IsRegisterPair() ? destination.GetStackIndex() : source.GetStackIndex();
    DCHECK(ExpectedPairLayout(source.IsRegisterPair() ? source : destination));
    vixl32::DRegister temp = temps.AcquireD();
    __ Vmov(temp, low_reg, vixl32::Register(low_reg.GetCode() + 1));
    GetAssembler()->LoadFromOffset(kLoadWordPair, low_reg, sp, mem);
    GetAssembler()->StoreDToOffset(temp, sp, mem);
  } else if (source.IsFpuRegisterPair() && destination.IsFpuRegisterPair()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsFpuRegisterPair() || destination.IsFpuRegisterPair()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsFpuRegister() || destination.IsFpuRegister()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsDoubleStackSlot() && destination.IsDoubleStackSlot()) {
    vixl32::DRegister temp1 = temps.AcquireD();
    vixl32::DRegister temp2 = temps.AcquireD();
    __ Vldr(temp1, MemOperand(sp, source.GetStackIndex()));
    __ Vldr(temp2, MemOperand(sp, destination.GetStackIndex()));
    __ Vstr(temp1, MemOperand(sp, destination.GetStackIndex()));
    __ Vstr(temp2, MemOperand(sp, source.GetStackIndex()));
  } else {
    LOG(FATAL) << "Unimplemented" << source << " <-> " << destination;
  }
}

void ParallelMoveResolverARMVIXL::SpillScratch(int reg ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void ParallelMoveResolverARMVIXL::RestoreScratch(int reg ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void LocationsBuilderARMVIXL::VisitLoadClass(HLoadClass* cls) {
  if (cls->NeedsAccessCheck()) {
    InvokeRuntimeCallingConventionARMVIXL calling_convention;
    CodeGenerator::CreateLoadClassLocationSummary(
        cls,
        LocationFrom(calling_convention.GetRegisterAt(0)),
        LocationFrom(r0),
        /* code_generator_supports_read_barrier */ true);
    return;
  }

  // TODO(VIXL): read barrier code.
  LocationSummary::CallKind call_kind = (cls->NeedsEnvironment() || kEmitCompilerReadBarrier)
      ? LocationSummary::kCallOnSlowPath
      : LocationSummary::kNoCall;
  LocationSummary* locations = new (GetGraph()->GetArena()) LocationSummary(cls, call_kind);
  HLoadClass::LoadKind load_kind = cls->GetLoadKind();
  if (load_kind == HLoadClass::LoadKind::kReferrersClass ||
      load_kind == HLoadClass::LoadKind::kDexCacheViaMethod ||
      load_kind == HLoadClass::LoadKind::kDexCachePcRelative) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARMVIXL::VisitLoadClass(HLoadClass* cls) {
  LocationSummary* locations = cls->GetLocations();
  if (cls->NeedsAccessCheck()) {
    codegen_->MoveConstant(locations->GetTemp(0), cls->GetTypeIndex());
    codegen_->InvokeRuntime(kQuickInitializeTypeAndVerifyAccess, cls, cls->GetDexPc());
    CheckEntrypointTypes<kQuickInitializeTypeAndVerifyAccess, void*, uint32_t>();
    return;
  }

  Location out_loc = locations->Out();
  vixl32::Register out = OutputRegister(cls);

  // TODO(VIXL): read barrier code.
  bool generate_null_check = false;
  switch (cls->GetLoadKind()) {
    case HLoadClass::LoadKind::kReferrersClass: {
      DCHECK(!cls->CanCallRuntime());
      DCHECK(!cls->MustGenerateClinitCheck());
      // /* GcRoot<mirror::Class> */ out = current_method->declaring_class_
      vixl32::Register current_method = InputRegisterAt(cls, 0);
      GenerateGcRootFieldLoad(cls,
                              out_loc,
                              current_method,
                              ArtMethod::DeclaringClassOffset().Int32Value());
      break;
    }
    case HLoadClass::LoadKind::kDexCacheViaMethod: {
      // /* GcRoot<mirror::Class>[] */ out =
      //        current_method.ptr_sized_fields_->dex_cache_resolved_types_
      vixl32::Register current_method = InputRegisterAt(cls, 0);
      const int32_t resolved_types_offset =
          ArtMethod::DexCacheResolvedTypesOffset(kArmPointerSize).Int32Value();
      GetAssembler()->LoadFromOffset(kLoadWord, out, current_method, resolved_types_offset);
      // /* GcRoot<mirror::Class> */ out = out[type_index]
      size_t offset = CodeGenerator::GetCacheOffset(cls->GetTypeIndex());
      GenerateGcRootFieldLoad(cls, out_loc, out, offset);
      generate_null_check = !cls->IsInDexCache();
      break;
    }
    default:
      TODO_VIXL32(FATAL);
  }

  if (generate_null_check || cls->MustGenerateClinitCheck()) {
    DCHECK(cls->CanCallRuntime());
    LoadClassSlowPathARMVIXL* slow_path = new (GetGraph()->GetArena()) LoadClassSlowPathARMVIXL(
        cls, cls, cls->GetDexPc(), cls->MustGenerateClinitCheck());
    codegen_->AddSlowPath(slow_path);
    if (generate_null_check) {
      __ Cbz(out, slow_path->GetEntryLabel());
    }
    if (cls->MustGenerateClinitCheck()) {
      GenerateClassInitializationCheck(slow_path, out);
    } else {
      __ Bind(slow_path->GetExitLabel());
    }
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateGcRootFieldLoad(
    HInstruction* instruction ATTRIBUTE_UNUSED,
    Location root,
    vixl32::Register obj,
    uint32_t offset,
    bool requires_read_barrier) {
  vixl32::Register root_reg = RegisterFrom(root);
  if (requires_read_barrier) {
    TODO_VIXL32(FATAL);
  } else {
    // Plain GC root load with no read barrier.
    // /* GcRoot<mirror::Object> */ root = *(obj + offset)
    GetAssembler()->LoadFromOffset(kLoadWord, root_reg, obj, offset);
    // Note that GC roots are not affected by heap poisoning, thus we
    // do not have to unpoison `root_reg` here.
  }
}

vixl32::Register CodeGeneratorARMVIXL::GetInvokeStaticOrDirectExtraParameter(
    HInvokeStaticOrDirect* invoke, vixl32::Register temp) {
  DCHECK_EQ(invoke->InputCount(), invoke->GetNumberOfArguments() + 1u);
  Location location = invoke->GetLocations()->InAt(invoke->GetSpecialInputIndex());
  if (!invoke->GetLocations()->Intrinsified()) {
    return RegisterFrom(location);
  }
  // For intrinsics we allow any location, so it may be on the stack.
  if (!location.IsRegister()) {
    GetAssembler()->LoadFromOffset(kLoadWord, temp, sp, location.GetStackIndex());
    return temp;
  }
  // For register locations, check if the register was saved. If so, get it from the stack.
  // Note: There is a chance that the register was saved but not overwritten, so we could
  // save one load. However, since this is just an intrinsic slow path we prefer this
  // simple and more robust approach rather that trying to determine if that's the case.
  SlowPathCode* slow_path = GetCurrentSlowPath();
  DCHECK(slow_path != nullptr);  // For intrinsified invokes the call is emitted on the slow path.
  if (slow_path->IsCoreRegisterSaved(RegisterFrom(location).GetCode())) {
    int stack_offset = slow_path->GetStackOffsetOfCoreRegister(RegisterFrom(location).GetCode());
    GetAssembler()->LoadFromOffset(kLoadWord, temp, sp, stack_offset);
    return temp;
  }
  return RegisterFrom(location);
}

void CodeGeneratorARMVIXL::GenerateStaticOrDirectCall(
    HInvokeStaticOrDirect* invoke, Location temp) {
  Location callee_method = temp;  // For all kinds except kRecursive, callee will be in temp.
  vixl32::Register temp_reg = RegisterFrom(temp);

  switch (invoke->GetMethodLoadKind()) {
    case HInvokeStaticOrDirect::MethodLoadKind::kStringInit: {
      uint32_t offset =
          GetThreadOffset<kArmPointerSize>(invoke->GetStringInitEntryPoint()).Int32Value();
      // temp = thread->string_init_entrypoint
      GetAssembler()->LoadFromOffset(kLoadWord, temp_reg, tr, offset);
      break;
    }
    case HInvokeStaticOrDirect::MethodLoadKind::kDexCacheViaMethod: {
      Location current_method = invoke->GetLocations()->InAt(invoke->GetSpecialInputIndex());
      vixl32::Register method_reg;
      if (current_method.IsRegister()) {
        method_reg = RegisterFrom(current_method);
      } else {
        TODO_VIXL32(FATAL);
      }
      // /* ArtMethod*[] */ temp = temp.ptr_sized_fields_->dex_cache_resolved_methods_;
      GetAssembler()->LoadFromOffset(
          kLoadWord,
          temp_reg,
          method_reg,
          ArtMethod::DexCacheResolvedMethodsOffset(kArmPointerSize).Int32Value());
      // temp = temp[index_in_cache];
      // Note: Don't use invoke->GetTargetMethod() as it may point to a different dex file.
      uint32_t index_in_cache = invoke->GetDexMethodIndex();
      GetAssembler()->LoadFromOffset(
          kLoadWord, temp_reg, temp_reg, CodeGenerator::GetCachePointerOffset(index_in_cache));
      break;
    }
    default:
      TODO_VIXL32(FATAL);
  }

  // TODO(VIXL): Support `CodePtrLocation` values other than `kCallArtMethod`.
  if (invoke->GetCodePtrLocation() != HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod) {
    TODO_VIXL32(FATAL);
  }

  // LR = callee_method->entry_point_from_quick_compiled_code_
  GetAssembler()->LoadFromOffset(
      kLoadWord,
      lr,
      RegisterFrom(callee_method),
      ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArmPointerSize).Int32Value());
  // LR()
  __ Blx(lr);

  DCHECK(!IsLeafMethod());
}

void CodeGeneratorARMVIXL::GenerateVirtualCall(HInvokeVirtual* invoke, Location temp_location) {
  vixl32::Register temp = RegisterFrom(temp_location);
  uint32_t method_offset = mirror::Class::EmbeddedVTableEntryOffset(
      invoke->GetVTableIndex(), kArmPointerSize).Uint32Value();

  // Use the calling convention instead of the location of the receiver, as
  // intrinsics may have put the receiver in a different register. In the intrinsics
  // slow path, the arguments have been moved to the right place, so here we are
  // guaranteed that the receiver is the first register of the calling convention.
  InvokeDexCallingConventionARMVIXL calling_convention;
  vixl32::Register receiver = calling_convention.GetRegisterAt(0);
  uint32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  // /* HeapReference<Class> */ temp = receiver->klass_
  GetAssembler()->LoadFromOffset(kLoadWord, temp, receiver, class_offset);
  MaybeRecordImplicitNullCheck(invoke);
  // Instead of simply (possibly) unpoisoning `temp` here, we should
  // emit a read barrier for the previous class reference load.
  // However this is not required in practice, as this is an
  // intermediate/temporary reference and because the current
  // concurrent copying collector keeps the from-space memory
  // intact/accessible until the end of the marking phase (the
  // concurrent copying collector may not in the future).
  GetAssembler()->MaybeUnpoisonHeapReference(temp);

  // temp = temp->GetMethodAt(method_offset);
  uint32_t entry_point = ArtMethod::EntryPointFromQuickCompiledCodeOffset(
      kArmPointerSize).Int32Value();
  GetAssembler()->LoadFromOffset(kLoadWord, temp, temp, method_offset);
  // LR = temp->GetEntryPoint();
  GetAssembler()->LoadFromOffset(kLoadWord, lr, temp, entry_point);
  // LR();
  __ Blx(lr);
}

static int32_t GetExceptionTlsOffset() {
  return Thread::ExceptionOffset<kArmPointerSize>().Int32Value();
}

void LocationsBuilderARMVIXL::VisitLoadException(HLoadException* load) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(load, LocationSummary::kNoCall);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorARMVIXL::VisitLoadException(HLoadException* load) {
  vixl32::Register out = OutputRegister(load);
  GetAssembler()->LoadFromOffset(kLoadWord, out, tr, GetExceptionTlsOffset());
}

void LocationsBuilderARMVIXL::VisitClearException(HClearException* clear) {
  new (GetGraph()->GetArena()) LocationSummary(clear, LocationSummary::kNoCall);
}

void InstructionCodeGeneratorARMVIXL::VisitClearException(HClearException* clear ATTRIBUTE_UNUSED) {
  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  __ Mov(temp, 0);
  GetAssembler()->StoreToOffset(kStoreWord, temp, tr, GetExceptionTlsOffset());
}

void LocationsBuilderARMVIXL::VisitThrow(HThrow* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(instruction, LocationSummary::kCallOnMainOnly);
  InvokeRuntimeCallingConventionARMVIXL calling_convention;
  locations->SetInAt(0, LocationFrom(calling_convention.GetRegisterAt(0)));
}

void InstructionCodeGeneratorARMVIXL::VisitThrow(HThrow* instruction) {
  codegen_->InvokeRuntime(kQuickDeliverException, instruction, instruction->GetDexPc());
  CheckEntrypointTypes<kQuickDeliverException, void, mirror::Object*>();
}

void CodeGeneratorARMVIXL::MaybeGenerateReadBarrierSlow(HInstruction* instruction ATTRIBUTE_UNUSED,
                                                        Location out,
                                                        Location ref ATTRIBUTE_UNUSED,
                                                        Location obj ATTRIBUTE_UNUSED,
                                                        uint32_t offset ATTRIBUTE_UNUSED,
                                                        Location index ATTRIBUTE_UNUSED) {
  if (kEmitCompilerReadBarrier) {
    DCHECK(!kUseBakerReadBarrier);
    TODO_VIXL32(FATAL);
  } else if (kPoisonHeapReferences) {
    GetAssembler()->UnpoisonHeapReference(RegisterFrom(out));
  }
}

#undef __
#undef QUICK_ENTRY_POINT
#undef TODO_VIXL32

}  // namespace arm
}  // namespace art
