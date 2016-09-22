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

using helpers::DWARFReg;
using helpers::FromLowSToD;
using helpers::OutputRegister;
using helpers::InputRegisterAt;
using helpers::InputOperandAt;
using helpers::OutputSRegister;
using helpers::InputSRegisterAt;

using RegisterList = vixl32::RegisterList;

static bool ExpectedPairLayout(Location location) {
  // We expected this for both core and fpu register pairs.
  return ((location.low() & 1) == 0) && (location.low() + 1 == location.high());
}

static constexpr size_t kArmInstrMaxSizeInBytes = 4u;

#ifdef __
#error "ARM Codegen VIXL macro-assembler macro already defined."
#endif

// TODO: Remove with later pop when codegen complete.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// NOLINT on __ macro to suppress wrong warning/fix (misc-macro-parentheses) from clang-tidy.
#define __ down_cast<CodeGeneratorARMVIXL*>(codegen)->GetVIXLAssembler()->  // NOLINT
#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kArmPointerSize, x).Int32Value()

// Marker that code is yet to be, and must, be implemented.
#define TODO_VIXL32(level) LOG(level) << __PRETTY_FUNCTION__ << " unimplemented "

class DivZeroCheckSlowPathARMVIXL : public SlowPathCodeARMVIXL {
 public:
  explicit DivZeroCheckSlowPathARMVIXL(HDivZeroCheck* instruction)
      : SlowPathCodeARMVIXL(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) OVERRIDE {
    CodeGeneratorARMVIXL* armvixl_codegen = down_cast<CodeGeneratorARMVIXL*>(codegen);
    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    armvixl_codegen->InvokeRuntime(kQuickThrowDivZero,
                                   instruction_,
                                   instruction_->GetDexPc(),
                                   this);
    CheckEntrypointTypes<kQuickThrowDivZero, void, void>();
  }

  bool IsFatal() const OVERRIDE { return true; }

  const char* GetDescription() const OVERRIDE { return "DivZeroCheckSlowPathARMVIXL"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DivZeroCheckSlowPathARMVIXL);
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

void SlowPathCodeARMVIXL::SaveLiveRegisters(CodeGenerator* codegen ATTRIBUTE_UNUSED,
                                            LocationSummary* locations ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void SlowPathCodeARMVIXL::RestoreLiveRegisters(CodeGenerator* codegen ATTRIBUTE_UNUSED,
                                               LocationSummary* locations ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::DumpCoreRegister(std::ostream& stream, int reg) const {
  stream << vixl32::Register(reg);
}

void CodeGeneratorARMVIXL::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  stream << vixl32::SRegister(reg);
}

static uint32_t ComputeSRegisterMask(const SRegisterList& regs) {
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
                    ComputeSRegisterMask(kFpuCalleeSaves),
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
}

#define __ reinterpret_cast<ArmVIXLAssembler*>(GetAssembler())->GetVIXLAssembler()->

void CodeGeneratorARMVIXL::Finalize(CodeAllocator* allocator) {
  GetAssembler()->FinalizeCode();
  CodeGenerator::Finalize(allocator);
}

void CodeGeneratorARMVIXL::SetupBlockedRegisters() const {
  // Don't allocate the dalvik style register pair passing.
  blocked_register_pairs_[R1_R2] = true;

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
    for (uint32_t i = kFpuCalleeSaves.GetFirstSRegister().GetCode();
         i <= kFpuCalleeSaves.GetLastSRegister().GetCode();
         ++i) {
      blocked_fpu_registers_[i] = true;
    }
  }

  UpdateBlockedPairRegisters();
}

// Blocks all register pairs containing blocked core registers.
void CodeGeneratorARMVIXL::UpdateBlockedPairRegisters() const {
  for (int i = 0; i < kNumberOfRegisterPairs; i++) {
    ArmManagedRegister current =
        ArmManagedRegister::FromRegisterPair(static_cast<RegisterPair>(i));
    if (blocked_core_registers_[current.AsRegisterPairLow()]
        || blocked_core_registers_[current.AsRegisterPairHigh()]) {
      blocked_register_pairs_[i] = true;
    }
  }
}

void InstructionCodeGeneratorARMVIXL::GenerateSuspendCheck(HSuspendCheck* instruction,
                                                           HBasicBlock* successor) {
  TODO_VIXL32(FATAL);
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

  UseScratchRegisterScope temps(GetVIXLAssembler());
  vixl32::Register temp = temps.Acquire();
  if (!skip_overflow_check) {
    __ Sub(temp, sp, static_cast<int32_t>(GetStackOverflowReservedBytes(kArm)));
    // The load must immediately precede RecordPcInfo.
    {
      AssemblerAccurateScope aas(GetVIXLAssembler(),
                                 kArmInstrMaxSizeInBytes,
                                 CodeBufferCheckScope::kMaximumSize);
      __ ldr(temp, MemOperand(temp));
      RecordPcInfo(nullptr, 0);
    }
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
    GetAssembler()->cfi().RelOffsetForMany(DWARFReg(s0),
                                           0,
                                           fpu_spill_mask_,
                                           kArmWordSize);
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
    GetAssembler()->cfi().RestoreMany(DWARFReg(vixl32::SRegister(0)),
                                      fpu_spill_mask_);
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

void CodeGeneratorARMVIXL::MoveConstant(Location destination, int32_t value) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::MoveLocation(Location dst, Location src, Primitive::Type dst_type) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::AddLocationAsTemp(Location location, LocationSummary* locations) {
  TODO_VIXL32(FATAL);
}

uintptr_t CodeGeneratorARMVIXL::GetAddressOf(HBasicBlock* block) {
  TODO_VIXL32(FATAL);
  return 0;
}

void CodeGeneratorARMVIXL::GenerateImplicitNullCheck(HNullCheck* null_check) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::GenerateExplicitNullCheck(HNullCheck* null_check) {
  TODO_VIXL32(FATAL);
}

void CodeGeneratorARMVIXL::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  ValidateInvokeRuntime(entrypoint, instruction, slow_path);
  GenerateInvokeRuntime(GetThreadOffset<kArmPointerSize>(entrypoint).Int32Value());
  if (EntrypointRequiresStackMap(entrypoint)) {
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

// Check if the desired_string_load_kind is supported. If it is, return it,
// otherwise return a fall-back kind that should be used instead.
HLoadString::LoadKind CodeGeneratorARMVIXL::GetSupportedLoadStringKind(
      HLoadString::LoadKind desired_string_load_kind) {
  TODO_VIXL32(FATAL);
  return desired_string_load_kind;
}

// Check if the desired_class_load_kind is supported. If it is, return it,
// otherwise return a fall-back kind that should be used instead.
HLoadClass::LoadKind CodeGeneratorARMVIXL::GetSupportedLoadClassKind(
      HLoadClass::LoadKind desired_class_load_kind) {
  TODO_VIXL32(FATAL);
  return desired_class_load_kind;
}

// Check if the desired_dispatch_info is supported. If it is, return it,
// otherwise return a fall-back info that should be used instead.
HInvokeStaticOrDirect::DispatchInfo CodeGeneratorARMVIXL::GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info,
      MethodReference target_method) {
  TODO_VIXL32(FATAL);
  return desired_dispatch_info;
}

// Generate a call to a static or direct method.
void CodeGeneratorARMVIXL::GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                                      Location temp) {
  TODO_VIXL32(FATAL);
}

// Generate a call to a virtual method.
void CodeGeneratorARMVIXL::GenerateVirtualCall(HInvokeVirtual* invoke, Location temp) {
  TODO_VIXL32(FATAL);
}

// Copy the result of a call into the given target.
void CodeGeneratorARMVIXL::MoveFromReturnRegister(Location trg, Primitive::Type type) {
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
      __ Vcmp(F64, FromLowSToD(lhs_loc.AsFpuRegisterPairLow<vixl32::SRegister>()), 0.0);
    }
  } else {
    if (type == Primitive::kPrimFloat) {
      __ Vcmp(F32, InputSRegisterAt(instruction, 0), InputSRegisterAt(instruction, 1));
    } else {
      DCHECK_EQ(type, Primitive::kPrimDouble);
      __ Vcmp(F64,
              FromLowSToD(lhs_loc.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(rhs_loc.AsFpuRegisterPairLow<vixl32::SRegister>()));
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

  vixl32::Register left_high = left.AsRegisterPairHigh<vixl32::Register>();
  vixl32::Register left_low = left.AsRegisterPairLow<vixl32::Register>();
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
    vixl32::Register right_high = right.AsRegisterPairHigh<vixl32::Register>();
    vixl32::Register right_low = right.AsRegisterPairLow<vixl32::Register>();

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
  vixl32::Label* true_target =
      codegen_->GoesToNextBlock(if_instr->GetBlock(), true_successor) ?
          nullptr : codegen_->GetLabelOf(true_successor);
  vixl32::Label* false_target =
      codegen_->GoesToNextBlock(if_instr->GetBlock(), false_successor) ?
          nullptr : codegen_->GetLabelOf(false_successor);
  GenerateTestAndBranch(if_instr, /* condition_input_index */ 0, true_target, false_target);
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

    // TODO: https://android-review.googlesource.com/#/c/252265/
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

  LocationSummary* locations = cond->GetLocations();
  Location right = locations->InAt(1);
  vixl32::Register out = OutputRegister(cond);
  vixl32::Label true_label, false_label;

  switch (cond->InputAt(0)->GetType()) {
    default: {
      // Integer case.
      if (right.IsRegister()) {
        __ Cmp(InputRegisterAt(cond, 0), InputRegisterAt(cond, 1));
      } else {
        DCHECK(right.IsConstant());
        __ Cmp(InputRegisterAt(cond, 0), CodeGenerator::GetInt32ValueOf(right.GetConstant()));
      }
      {
        AssemblerAccurateScope aas(GetVIXLAssembler(),
                                   kArmInstrMaxSizeInBytes * 3u,
                                   CodeBufferCheckScope::kMaximumSize);
        __ ite(ARMCondition(cond->GetCondition()));
        __ mov(ARMCondition(cond->GetCondition()), OutputRegister(cond), 1);
        __ mov(ARMCondition(cond->GetOppositeCondition()), OutputRegister(cond), 0);
      }
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

void LocationsBuilderARMVIXL::VisitLongConstant(HLongConstant* constant) {
  LocationSummary* locations =
      new (GetGraph()->GetArena()) LocationSummary(constant, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(constant));
}

void InstructionCodeGeneratorARMVIXL::VisitLongConstant(HLongConstant* constant ATTRIBUTE_UNUSED) {
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
          __ Sbfx(OutputRegister(conversion), in.AsRegisterPairLow<vixl32::Register>(), 0, 8);
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
          __ Sbfx(OutputRegister(conversion), in.AsRegisterPairLow<vixl32::Register>(), 0, 16);
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
            __ Mov(OutputRegister(conversion), in.AsRegisterPairLow<vixl32::Register>());
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
          vixl32::SRegister temp = locations->GetTemp(0).AsFpuRegisterPairLow<vixl32::SRegister>();
          __ Vcvt(I32, F32, temp, InputSRegisterAt(conversion, 0));
          __ Vmov(OutputRegister(conversion), temp);
          break;
        }

        case Primitive::kPrimDouble: {
          // Processing a Dex `double-to-int' instruction.
          vixl32::SRegister temp_s =
              locations->GetTemp(0).AsFpuRegisterPairLow<vixl32::SRegister>();
          __ Vcvt(I32, F64, temp_s, FromLowSToD(in.AsFpuRegisterPairLow<vixl32::SRegister>()));
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
          __ Mov(out.AsRegisterPairLow<vixl32::Register>(), InputRegisterAt(conversion, 0));
          // Sign extension.
          __ Asr(out.AsRegisterPairHigh<vixl32::Register>(),
                 out.AsRegisterPairLow<vixl32::Register>(),
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
          __ Ubfx(OutputRegister(conversion), in.AsRegisterPairLow<vixl32::Register>(), 0, 16);
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
          __ Vcvt(F32,
                  F64,
                  OutputSRegister(conversion),
                  FromLowSToD(in.AsFpuRegisterPairLow<vixl32::SRegister>()));
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
          __ Vmov(out.AsFpuRegisterPairLow<vixl32::SRegister>(), InputRegisterAt(conversion, 0));
          __ Vcvt(F64,
                  I32,
                  FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
                  out.AsFpuRegisterPairLow<vixl32::SRegister>());
          break;
        }

        case Primitive::kPrimLong: {
          // Processing a Dex `long-to-double' instruction.
          vixl32::Register low = in.AsRegisterPairLow<vixl32::Register>();
          vixl32::Register high = in.AsRegisterPairHigh<vixl32::Register>();

          vixl32::SRegister out_s = out.AsFpuRegisterPairLow<vixl32::SRegister>();
          vixl32::DRegister out_d = FromLowSToD(out_s);

          vixl32::SRegister temp_s =
              locations->GetTemp(0).AsFpuRegisterPairLow<vixl32::SRegister>();
          vixl32::DRegister temp_d = FromLowSToD(temp_s);

          vixl32::SRegister constant_s =
              locations->GetTemp(1).AsFpuRegisterPairLow<vixl32::SRegister>();
          vixl32::DRegister constant_d = FromLowSToD(constant_s);

          // temp_d = int-to-double(high)
          __ Vmov(temp_s, high);
          __ Vcvt(F64, I32, temp_d, temp_s);
          // constant_d = k2Pow32EncodingForDouble
          __ Vmov(F64,
                  constant_d,
                  vixl32::DOperand(bit_cast<double, int64_t>(k2Pow32EncodingForDouble)));
          // out_d = unsigned-to-double(low)
          __ Vmov(out_s, low);
          __ Vcvt(F64, U32, out_d, out_s);
          // out_d += temp_d * constant_d
          __ Vmla(F64, out_d, temp_d, constant_d);
          break;
        }

        case Primitive::kPrimFloat:
          // Processing a Dex `float-to-double' instruction.
          __ Vcvt(F64,
                  F32,
                  FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
                  InputSRegisterAt(conversion, 0));
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

    // TODO: https://android-review.googlesource.com/#/c/254144/
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

    // TODO: https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
      DCHECK(second.IsRegisterPair());
      __ Adds(out.AsRegisterPairLow<vixl32::Register>(),
              first.AsRegisterPairLow<vixl32::Register>(),
              Operand(second.AsRegisterPairLow<vixl32::Register>()));
      __ Adc(out.AsRegisterPairHigh<vixl32::Register>(),
             first.AsRegisterPairHigh<vixl32::Register>(),
             second.AsRegisterPairHigh<vixl32::Register>());
      break;
    }

    case Primitive::kPrimFloat: {
      __ Vadd(F32, OutputSRegister(add), InputSRegisterAt(add, 0), InputSRegisterAt(add, 1));
      }
      break;

    case Primitive::kPrimDouble:
      __ Vadd(F64,
              FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(first.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(second.AsFpuRegisterPairLow<vixl32::SRegister>()));
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

    // TODO: https://android-review.googlesource.com/#/c/254144/
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
      if (second.IsRegister()) {
        __ Sub(OutputRegister(sub), InputRegisterAt(sub, 0), InputRegisterAt(sub, 1));
      } else {
        __ Sub(OutputRegister(sub),
               InputRegisterAt(sub, 0),
               second.GetConstant()->AsIntConstant()->GetValue());
      }
      break;
    }

    // TODO: https://android-review.googlesource.com/#/c/254144/
    case Primitive::kPrimLong: {
      DCHECK(second.IsRegisterPair());
      __ Subs(out.AsRegisterPairLow<vixl32::Register>(),
              first.AsRegisterPairLow<vixl32::Register>(),
              Operand(second.AsRegisterPairLow<vixl32::Register>()));
      __ Sbc(out.AsRegisterPairHigh<vixl32::Register>(),
             first.AsRegisterPairHigh<vixl32::Register>(),
             Operand(second.AsRegisterPairHigh<vixl32::Register>()));
      break;
    }

    case Primitive::kPrimFloat: {
      __ Vsub(F32, OutputSRegister(sub), InputSRegisterAt(sub, 0), InputSRegisterAt(sub, 1));
      break;
    }

    case Primitive::kPrimDouble: {
      __ Vsub(F64,
              FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(first.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(second.AsFpuRegisterPairLow<vixl32::SRegister>()));
      break;
    }

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
      vixl32::Register out_hi = out.AsRegisterPairHigh<vixl32::Register>();
      vixl32::Register out_lo = out.AsRegisterPairLow<vixl32::Register>();
      vixl32::Register in1_hi = first.AsRegisterPairHigh<vixl32::Register>();
      vixl32::Register in1_lo = first.AsRegisterPairLow<vixl32::Register>();
      vixl32::Register in2_hi = second.AsRegisterPairHigh<vixl32::Register>();
      vixl32::Register in2_lo = second.AsRegisterPairLow<vixl32::Register>();

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
      __ Add(out_hi, out_hi, Operand(temp));
      break;
    }

    case Primitive::kPrimFloat: {
      __ Vmul(F32, OutputSRegister(mul), InputSRegisterAt(mul, 0), InputSRegisterAt(mul, 1));
      break;
    }

    case Primitive::kPrimDouble: {
      __ Vmul(F64,
              FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(first.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(second.AsFpuRegisterPairLow<vixl32::SRegister>()));
      break;
    }

    default:
      LOG(FATAL) << "Unexpected mul type " << mul->GetResultType();
  }
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
      __ Mvn(out.AsRegisterPairLow<vixl32::Register>(),
             Operand(in.AsRegisterPairLow<vixl32::Register>()));
      __ Mvn(out.AsRegisterPairHigh<vixl32::Register>(),
             Operand(in.AsRegisterPairHigh<vixl32::Register>()));
      break;

    default:
      LOG(FATAL) << "Unimplemented type for not operation " << not_->GetResultType();
  }
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

void InstructionCodeGeneratorARMVIXL::DivRemOneOrMinusOne(HBinaryOperation* instruction) {
  DCHECK(instruction->IsDiv() || instruction->IsRem());
  DCHECK(instruction->GetResultType() == Primitive::kPrimInt);

  LocationSummary* locations = instruction->GetLocations();
  Location second = locations->InAt(1);
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
  vixl32::Register temp = locations->GetTemp(0).AsRegister<vixl32::Register>();
  int32_t imm = second.GetConstant()->AsIntConstant()->GetValue();
  uint32_t abs_imm = static_cast<uint32_t>(AbsOrMin(imm));
  int ctz_imm = CTZ(abs_imm);

  if (ctz_imm == 1) {
    __ Lsr(temp, dividend, 32 - ctz_imm);
  } else {
    __ Asr(temp, dividend, 31);
    __ Lsr(temp, temp, 32 - ctz_imm);
  }
  __ Add(out, temp, Operand(dividend));

  if (instruction->IsDiv()) {
    __ Asr(out, out, ctz_imm);
    if (imm < 0) {
      __ Rsb(out, out, Operand(0));
    }
  } else {
    __ Ubfx(out, out, 0, ctz_imm);
    __ Sub(out, out, Operand(temp));
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
  vixl32::Register temp1 = locations->GetTemp(0).AsRegister<vixl32::Register>();
  vixl32::Register temp2 = locations->GetTemp(1).AsRegister<vixl32::Register>();
  int64_t imm = second.GetConstant()->AsIntConstant()->GetValue();

  int64_t magic;
  int shift;
  CalculateMagicAndShiftForDivRem(imm, false /* is_long */, &magic, &shift);

  __ Mov(temp1, magic);
  __ Smull(temp2, temp1, dividend, temp1);

  if (imm > 0 && magic < 0) {
    __ Add(temp1, temp1, Operand(dividend));
  } else if (imm < 0 && magic > 0) {
    __ Sub(temp1, temp1, Operand(dividend));
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
  LocationSummary* locations = div->GetLocations();
  Location out = locations->Out();
  Location first = locations->InAt(0);
  Location second = locations->InAt(1);

  switch (div->GetResultType()) {
    case Primitive::kPrimInt: {
      if (second.IsConstant()) {
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

    case Primitive::kPrimFloat: {
      __ Vdiv(F32, OutputSRegister(div), InputSRegisterAt(div, 0), InputSRegisterAt(div, 1));
      break;
    }

    case Primitive::kPrimDouble: {
      __ Vdiv(F64,
              FromLowSToD(out.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(first.AsFpuRegisterPairLow<vixl32::SRegister>()),
              FromLowSToD(second.AsFpuRegisterPairLow<vixl32::SRegister>()));
      break;
    }

    default:
      LOG(FATAL) << "Unexpected div type " << div->GetResultType();
  }
}

void LocationsBuilderARMVIXL::VisitDivZeroCheck(HDivZeroCheck* instruction) {
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
        __ Orrs(temp,
                value.AsRegisterPairLow<vixl32::Register>(),
                Operand(value.AsRegisterPairHigh<vixl32::Register>()));
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

void LocationsBuilderARMVIXL::VisitParallelMove(HParallelMove* instruction ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorARMVIXL::VisitParallelMove(HParallelMove* instruction) {
  codegen_->GetMoveResolver()->EmitNativeCode(instruction);
}

ArmVIXLAssembler* ParallelMoveResolverARMVIXL::GetAssembler() const {
  return codegen_->GetAssembler();
}

void ParallelMoveResolverARMVIXL::EmitMove(size_t index) {
  MoveOperands* move = moves_[index];
  Location source = move->GetSource();
  Location destination = move->GetDestination();

  if (source.IsRegister()) {
    if (destination.IsRegister()) {
      __ Mov(destination.AsRegister<vixl32::Register>(), source.AsRegister<vixl32::Register>());
    } else if (destination.IsFpuRegister()) {
      __ Vmov(destination.AsFpuRegister<vixl32::SRegister>(),
              source.AsRegister<vixl32::Register>());
    } else {
      DCHECK(destination.IsStackSlot());
      GetAssembler()->StoreToOffset(kStoreWord,
                                    source.AsRegister<vixl32::Register>(),
                                    sp,
                                    destination.GetStackIndex());
    }
  } else if (source.IsStackSlot()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsFpuRegister()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsDoubleStackSlot()) {
    TODO_VIXL32(FATAL);
  } else if (source.IsRegisterPair()) {
    if (destination.IsRegisterPair()) {
      __ Mov(destination.AsRegisterPairLow<vixl32::Register>(),
             source.AsRegisterPairLow<vixl32::Register>());
      __ Mov(destination.AsRegisterPairHigh<vixl32::Register>(),
             source.AsRegisterPairHigh<vixl32::Register>());
    } else if (destination.IsFpuRegisterPair()) {
      __ Vmov(FromLowSToD(destination.AsFpuRegisterPairLow<vixl32::SRegister>()),
              source.AsRegisterPairLow<vixl32::Register>(),
              source.AsRegisterPairHigh<vixl32::Register>());
    } else {
      DCHECK(destination.IsDoubleStackSlot()) << destination;
      DCHECK(ExpectedPairLayout(source));
      GetAssembler()->StoreToOffset(kStoreWordPair,
                                    source.AsRegisterPairLow<vixl32::Register>(),
                                    sp,
                                    destination.GetStackIndex());
    }
  } else if (source.IsFpuRegisterPair()) {
    TODO_VIXL32(FATAL);
  } else {
    DCHECK(source.IsConstant()) << source;
    HConstant* constant = source.GetConstant();
    if (constant->IsIntConstant() || constant->IsNullConstant()) {
      int32_t value = CodeGenerator::GetInt32ValueOf(constant);
      if (destination.IsRegister()) {
        __ Mov(destination.AsRegister<vixl32::Register>(), value);
      } else {
        DCHECK(destination.IsStackSlot());
        UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
        vixl32::Register temp = temps.Acquire();
        __ Mov(temp, value);
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
      }
    } else if (constant->IsLongConstant()) {
      int64_t value = constant->AsLongConstant()->GetValue();
      if (destination.IsRegisterPair()) {
        __ Mov(destination.AsRegisterPairLow<vixl32::Register>(), Low32Bits(value));
        __ Mov(destination.AsRegisterPairHigh<vixl32::Register>(), High32Bits(value));
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
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
        __ Vmov(F64, FromLowSToD(destination.AsFpuRegisterPairLow<vixl32::SRegister>()), value);
      } else {
        DCHECK(destination.IsDoubleStackSlot()) << destination;
        uint64_t int_value = bit_cast<uint64_t, double>(value);
        UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
        vixl32::Register temp = temps.Acquire();
        GetAssembler()->LoadImmediate(temp, Low32Bits(int_value));
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
        GetAssembler()->LoadImmediate(temp, High32Bits(int_value));
        GetAssembler()->StoreToOffset(kStoreWord,
                                      temp,
                                      sp,
                                      destination.GetHighStackIndex(kArmWordSize));
      }
    } else {
      DCHECK(constant->IsFloatConstant()) << constant->DebugName();
      float value = constant->AsFloatConstant()->GetValue();
      if (destination.IsFpuRegister()) {
        __ Vmov(F32, destination.AsFpuRegister<vixl32::SRegister>(), value);
      } else {
        DCHECK(destination.IsStackSlot());
        UseScratchRegisterScope temps(GetAssembler()->GetVIXLAssembler());
        vixl32::Register temp = temps.Acquire();
        GetAssembler()->LoadImmediate(temp, bit_cast<int32_t, float>(value));
        GetAssembler()->StoreToOffset(kStoreWord, temp, sp, destination.GetStackIndex());
      }
    }
  }
}

void ParallelMoveResolverARMVIXL::Exchange(Register reg, int mem) {
  TODO_VIXL32(FATAL);
}

void ParallelMoveResolverARMVIXL::Exchange(int mem1, int mem2) {
  TODO_VIXL32(FATAL);
}

void ParallelMoveResolverARMVIXL::EmitSwap(size_t index) {
  TODO_VIXL32(FATAL);
}

void ParallelMoveResolverARMVIXL::SpillScratch(int reg ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}

void ParallelMoveResolverARMVIXL::RestoreScratch(int reg ATTRIBUTE_UNUSED) {
  TODO_VIXL32(FATAL);
}


// TODO: Remove when codegen complete.
#pragma GCC diagnostic pop

#undef __
#undef QUICK_ENTRY_POINT
#undef TODO_VIXL32

}  // namespace arm
}  // namespace art
