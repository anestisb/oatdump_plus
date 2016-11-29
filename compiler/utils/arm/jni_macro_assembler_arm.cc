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

#include "jni_macro_assembler_arm.h"

#include <algorithm>

#include "assembler_thumb2.h"
#include "base/arena_allocator.h"
#include "base/bit_utils.h"
#include "base/logging.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "offsets.h"
#include "thread.h"

namespace art {
namespace arm {

constexpr size_t kFramePointerSize = static_cast<size_t>(kArmPointerSize);

// Slowpath entered when Thread::Current()->_exception is non-null
class ArmExceptionSlowPath FINAL : public SlowPath {
 public:
  ArmExceptionSlowPath(ArmManagedRegister scratch, size_t stack_adjust)
      : scratch_(scratch), stack_adjust_(stack_adjust) {
  }
  void Emit(Assembler *sp_asm) OVERRIDE;
 private:
  const ArmManagedRegister scratch_;
  const size_t stack_adjust_;
};

ArmJNIMacroAssembler::ArmJNIMacroAssembler(ArenaAllocator* arena, InstructionSet isa) {
  switch (isa) {
    case kArm:
    case kThumb2:
      asm_.reset(new (arena) Thumb2Assembler(arena));
      break;

    default:
      LOG(FATAL) << isa;
      UNREACHABLE();
  }
}

ArmJNIMacroAssembler::~ArmJNIMacroAssembler() {
}

size_t ArmJNIMacroAssembler::CodeSize() const {
  return asm_->CodeSize();
}

DebugFrameOpCodeWriterForAssembler& ArmJNIMacroAssembler::cfi() {
  return asm_->cfi();
}

void ArmJNIMacroAssembler::FinalizeCode() {
  asm_->FinalizeCode();
}

void ArmJNIMacroAssembler::FinalizeInstructions(const MemoryRegion& region) {
  asm_->FinalizeInstructions(region);
}

static dwarf::Reg DWARFReg(Register reg) {
  return dwarf::Reg::ArmCore(static_cast<int>(reg));
}

static dwarf::Reg DWARFReg(SRegister reg) {
  return dwarf::Reg::ArmFp(static_cast<int>(reg));
}

#define __ asm_->

void ArmJNIMacroAssembler::BuildFrame(size_t frame_size,
                                      ManagedRegister method_reg,
                                      ArrayRef<const ManagedRegister> callee_save_regs,
                                      const ManagedRegisterEntrySpills& entry_spills) {
  CHECK_EQ(CodeSize(), 0U);  // Nothing emitted yet
  CHECK_ALIGNED(frame_size, kStackAlignment);
  CHECK_EQ(R0, method_reg.AsArm().AsCoreRegister());

  // Push callee saves and link register.
  RegList core_spill_mask = 1 << LR;
  uint32_t fp_spill_mask = 0;
  for (const ManagedRegister& reg : callee_save_regs) {
    if (reg.AsArm().IsCoreRegister()) {
      core_spill_mask |= 1 << reg.AsArm().AsCoreRegister();
    } else {
      fp_spill_mask |= 1 << reg.AsArm().AsSRegister();
    }
  }
  __ PushList(core_spill_mask);
  cfi().AdjustCFAOffset(POPCOUNT(core_spill_mask) * kFramePointerSize);
  cfi().RelOffsetForMany(DWARFReg(Register(0)), 0, core_spill_mask, kFramePointerSize);
  if (fp_spill_mask != 0) {
    __ vpushs(SRegister(CTZ(fp_spill_mask)), POPCOUNT(fp_spill_mask));
    cfi().AdjustCFAOffset(POPCOUNT(fp_spill_mask) * kFramePointerSize);
    cfi().RelOffsetForMany(DWARFReg(SRegister(0)), 0, fp_spill_mask, kFramePointerSize);
  }

  // Increase frame to required size.
  int pushed_values = POPCOUNT(core_spill_mask) + POPCOUNT(fp_spill_mask);
  CHECK_GT(frame_size, pushed_values * kFramePointerSize);  // Must at least have space for Method*.
  IncreaseFrameSize(frame_size - pushed_values * kFramePointerSize);  // handles CFI as well.

  // Write out Method*.
  __ StoreToOffset(kStoreWord, R0, SP, 0);

  // Write out entry spills.
  int32_t offset = frame_size + kFramePointerSize;
  for (size_t i = 0; i < entry_spills.size(); ++i) {
    ArmManagedRegister reg = entry_spills.at(i).AsArm();
    if (reg.IsNoRegister()) {
      // only increment stack offset.
      ManagedRegisterSpill spill = entry_spills.at(i);
      offset += spill.getSize();
    } else if (reg.IsCoreRegister()) {
      __ StoreToOffset(kStoreWord, reg.AsCoreRegister(), SP, offset);
      offset += 4;
    } else if (reg.IsSRegister()) {
      __ StoreSToOffset(reg.AsSRegister(), SP, offset);
      offset += 4;
    } else if (reg.IsDRegister()) {
      __ StoreDToOffset(reg.AsDRegister(), SP, offset);
      offset += 8;
    }
  }
}

void ArmJNIMacroAssembler::RemoveFrame(size_t frame_size,
                                       ArrayRef<const ManagedRegister> callee_save_regs) {
  CHECK_ALIGNED(frame_size, kStackAlignment);
  cfi().RememberState();

  // Compute callee saves to pop and PC.
  RegList core_spill_mask = 1 << PC;
  uint32_t fp_spill_mask = 0;
  for (const ManagedRegister& reg : callee_save_regs) {
    if (reg.AsArm().IsCoreRegister()) {
      core_spill_mask |= 1 << reg.AsArm().AsCoreRegister();
    } else {
      fp_spill_mask |= 1 << reg.AsArm().AsSRegister();
    }
  }

  // Decrease frame to start of callee saves.
  int pop_values = POPCOUNT(core_spill_mask) + POPCOUNT(fp_spill_mask);
  CHECK_GT(frame_size, pop_values * kFramePointerSize);
  DecreaseFrameSize(frame_size - (pop_values * kFramePointerSize));  // handles CFI as well.

  if (fp_spill_mask != 0) {
    __ vpops(SRegister(CTZ(fp_spill_mask)), POPCOUNT(fp_spill_mask));
    cfi().AdjustCFAOffset(-kFramePointerSize * POPCOUNT(fp_spill_mask));
    cfi().RestoreMany(DWARFReg(SRegister(0)), fp_spill_mask);
  }

  // Pop callee saves and PC.
  __ PopList(core_spill_mask);

  // The CFI should be restored for any code that follows the exit block.
  cfi().RestoreState();
  cfi().DefCFAOffset(frame_size);
}

void ArmJNIMacroAssembler::IncreaseFrameSize(size_t adjust) {
  __ AddConstant(SP, -adjust);
  cfi().AdjustCFAOffset(adjust);
}

static void DecreaseFrameSizeImpl(ArmAssembler* assembler, size_t adjust) {
  assembler->AddConstant(SP, adjust);
  assembler->cfi().AdjustCFAOffset(-adjust);
}

void ArmJNIMacroAssembler::DecreaseFrameSize(size_t adjust) {
  DecreaseFrameSizeImpl(asm_.get(), adjust);
}

void ArmJNIMacroAssembler::Store(FrameOffset dest, ManagedRegister msrc, size_t size) {
  ArmManagedRegister src = msrc.AsArm();
  if (src.IsNoRegister()) {
    CHECK_EQ(0u, size);
  } else if (src.IsCoreRegister()) {
    CHECK_EQ(4u, size);
    __ StoreToOffset(kStoreWord, src.AsCoreRegister(), SP, dest.Int32Value());
  } else if (src.IsRegisterPair()) {
    CHECK_EQ(8u, size);
    __ StoreToOffset(kStoreWord, src.AsRegisterPairLow(), SP, dest.Int32Value());
    __ StoreToOffset(kStoreWord, src.AsRegisterPairHigh(), SP, dest.Int32Value() + 4);
  } else if (src.IsSRegister()) {
    __ StoreSToOffset(src.AsSRegister(), SP, dest.Int32Value());
  } else {
    CHECK(src.IsDRegister()) << src;
    __ StoreDToOffset(src.AsDRegister(), SP, dest.Int32Value());
  }
}

void ArmJNIMacroAssembler::StoreRef(FrameOffset dest, ManagedRegister msrc) {
  ArmManagedRegister src = msrc.AsArm();
  CHECK(src.IsCoreRegister()) << src;
  __ StoreToOffset(kStoreWord, src.AsCoreRegister(), SP, dest.Int32Value());
}

void ArmJNIMacroAssembler::StoreRawPtr(FrameOffset dest, ManagedRegister msrc) {
  ArmManagedRegister src = msrc.AsArm();
  CHECK(src.IsCoreRegister()) << src;
  __ StoreToOffset(kStoreWord, src.AsCoreRegister(), SP, dest.Int32Value());
}

void ArmJNIMacroAssembler::StoreSpanning(FrameOffset dest,
                                         ManagedRegister msrc,
                                         FrameOffset in_off,
                                         ManagedRegister mscratch) {
  ArmManagedRegister src = msrc.AsArm();
  ArmManagedRegister scratch = mscratch.AsArm();
  __ StoreToOffset(kStoreWord, src.AsCoreRegister(), SP, dest.Int32Value());
  __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, in_off.Int32Value());
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value() + sizeof(uint32_t));
}

void ArmJNIMacroAssembler::CopyRef(FrameOffset dest, FrameOffset src, ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, src.Int32Value());
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value());
}

void ArmJNIMacroAssembler::LoadRef(ManagedRegister mdest,
                                   ManagedRegister mbase,
                                   MemberOffset offs,
                                   bool unpoison_reference) {
  ArmManagedRegister base = mbase.AsArm();
  ArmManagedRegister dst = mdest.AsArm();
  CHECK(base.IsCoreRegister()) << base;
  CHECK(dst.IsCoreRegister()) << dst;
  __ LoadFromOffset(kLoadWord,
                    dst.AsCoreRegister(),
                    base.AsCoreRegister(),
                    offs.Int32Value());
  if (unpoison_reference) {
    __ MaybeUnpoisonHeapReference(dst.AsCoreRegister());
  }
}

void ArmJNIMacroAssembler::LoadRef(ManagedRegister mdest, FrameOffset  src) {
  ArmManagedRegister dst = mdest.AsArm();
  CHECK(dst.IsCoreRegister()) << dst;
  __ LoadFromOffset(kLoadWord, dst.AsCoreRegister(), SP, src.Int32Value());
}

void ArmJNIMacroAssembler::LoadRawPtr(ManagedRegister mdest,
                                      ManagedRegister mbase,
                                      Offset offs) {
  ArmManagedRegister base = mbase.AsArm();
  ArmManagedRegister dst = mdest.AsArm();
  CHECK(base.IsCoreRegister()) << base;
  CHECK(dst.IsCoreRegister()) << dst;
  __ LoadFromOffset(kLoadWord,
                    dst.AsCoreRegister(),
                    base.AsCoreRegister(),
                    offs.Int32Value());
}

void ArmJNIMacroAssembler::StoreImmediateToFrame(FrameOffset dest,
                                                 uint32_t imm,
                                                 ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  __ LoadImmediate(scratch.AsCoreRegister(), imm);
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value());
}

static void EmitLoad(ArmAssembler* assembler,
                     ManagedRegister m_dst,
                     Register src_register,
                     int32_t src_offset,
                     size_t size) {
  ArmManagedRegister dst = m_dst.AsArm();
  if (dst.IsNoRegister()) {
    CHECK_EQ(0u, size) << dst;
  } else if (dst.IsCoreRegister()) {
    CHECK_EQ(4u, size) << dst;
    assembler->LoadFromOffset(kLoadWord, dst.AsCoreRegister(), src_register, src_offset);
  } else if (dst.IsRegisterPair()) {
    CHECK_EQ(8u, size) << dst;
    assembler->LoadFromOffset(kLoadWord, dst.AsRegisterPairLow(), src_register, src_offset);
    assembler->LoadFromOffset(kLoadWord, dst.AsRegisterPairHigh(), src_register, src_offset + 4);
  } else if (dst.IsSRegister()) {
    assembler->LoadSFromOffset(dst.AsSRegister(), src_register, src_offset);
  } else {
    CHECK(dst.IsDRegister()) << dst;
    assembler->LoadDFromOffset(dst.AsDRegister(), src_register, src_offset);
  }
}

void ArmJNIMacroAssembler::Load(ManagedRegister m_dst, FrameOffset src, size_t size) {
  EmitLoad(asm_.get(), m_dst, SP, src.Int32Value(), size);
}

void ArmJNIMacroAssembler::LoadFromThread(ManagedRegister m_dst, ThreadOffset32 src, size_t size) {
  EmitLoad(asm_.get(), m_dst, TR, src.Int32Value(), size);
}

void ArmJNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister m_dst, ThreadOffset32 offs) {
  ArmManagedRegister dst = m_dst.AsArm();
  CHECK(dst.IsCoreRegister()) << dst;
  __ LoadFromOffset(kLoadWord, dst.AsCoreRegister(), TR, offs.Int32Value());
}

void ArmJNIMacroAssembler::CopyRawPtrFromThread(FrameOffset fr_offs,
                                                ThreadOffset32 thr_offs,
                                                ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), TR, thr_offs.Int32Value());
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, fr_offs.Int32Value());
}

void ArmJNIMacroAssembler::CopyRawPtrToThread(ThreadOffset32 thr_offs,
                                              FrameOffset fr_offs,
                                              ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, fr_offs.Int32Value());
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), TR, thr_offs.Int32Value());
}

void ArmJNIMacroAssembler::StoreStackOffsetToThread(ThreadOffset32 thr_offs,
                                                    FrameOffset fr_offs,
                                                    ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  __ AddConstant(scratch.AsCoreRegister(), SP, fr_offs.Int32Value(), AL);
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), TR, thr_offs.Int32Value());
}

void ArmJNIMacroAssembler::StoreStackPointerToThread(ThreadOffset32 thr_offs) {
  __ StoreToOffset(kStoreWord, SP, TR, thr_offs.Int32Value());
}

void ArmJNIMacroAssembler::SignExtend(ManagedRegister /*mreg*/, size_t /*size*/) {
  UNIMPLEMENTED(FATAL) << "no sign extension necessary for arm";
}

void ArmJNIMacroAssembler::ZeroExtend(ManagedRegister /*mreg*/, size_t /*size*/) {
  UNIMPLEMENTED(FATAL) << "no zero extension necessary for arm";
}

void ArmJNIMacroAssembler::Move(ManagedRegister m_dst, ManagedRegister m_src, size_t /*size*/) {
  ArmManagedRegister dst = m_dst.AsArm();
  ArmManagedRegister src = m_src.AsArm();
  if (!dst.Equals(src)) {
    if (dst.IsCoreRegister()) {
      CHECK(src.IsCoreRegister()) << src;
      __ mov(dst.AsCoreRegister(), ShifterOperand(src.AsCoreRegister()));
    } else if (dst.IsDRegister()) {
      if (src.IsDRegister()) {
        __ vmovd(dst.AsDRegister(), src.AsDRegister());
      } else {
        // VMOV Dn, Rlo, Rhi (Dn = {Rlo, Rhi})
        CHECK(src.IsRegisterPair()) << src;
        __ vmovdrr(dst.AsDRegister(), src.AsRegisterPairLow(), src.AsRegisterPairHigh());
      }
    } else if (dst.IsSRegister()) {
      if (src.IsSRegister()) {
        __ vmovs(dst.AsSRegister(), src.AsSRegister());
      } else {
        // VMOV Sn, Rn  (Sn = Rn)
        CHECK(src.IsCoreRegister()) << src;
        __ vmovsr(dst.AsSRegister(), src.AsCoreRegister());
      }
    } else {
      CHECK(dst.IsRegisterPair()) << dst;
      CHECK(src.IsRegisterPair()) << src;
      // Ensure that the first move doesn't clobber the input of the second.
      if (src.AsRegisterPairHigh() != dst.AsRegisterPairLow()) {
        __ mov(dst.AsRegisterPairLow(), ShifterOperand(src.AsRegisterPairLow()));
        __ mov(dst.AsRegisterPairHigh(), ShifterOperand(src.AsRegisterPairHigh()));
      } else {
        __ mov(dst.AsRegisterPairHigh(), ShifterOperand(src.AsRegisterPairHigh()));
        __ mov(dst.AsRegisterPairLow(), ShifterOperand(src.AsRegisterPairLow()));
      }
    }
  }
}

void ArmJNIMacroAssembler::Copy(FrameOffset dest,
                                FrameOffset src,
                                ManagedRegister mscratch,
                                size_t size) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  CHECK(size == 4 || size == 8) << size;
  if (size == 4) {
    __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, src.Int32Value());
    __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value());
  } else if (size == 8) {
    __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, src.Int32Value());
    __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value());
    __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, src.Int32Value() + 4);
    __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, dest.Int32Value() + 4);
  }
}

void ArmJNIMacroAssembler::Copy(FrameOffset dest,
                                ManagedRegister src_base,
                                Offset src_offset,
                                ManagedRegister mscratch,
                                size_t size) {
  Register scratch = mscratch.AsArm().AsCoreRegister();
  CHECK_EQ(size, 4u);
  __ LoadFromOffset(kLoadWord, scratch, src_base.AsArm().AsCoreRegister(), src_offset.Int32Value());
  __ StoreToOffset(kStoreWord, scratch, SP, dest.Int32Value());
}

void ArmJNIMacroAssembler::Copy(ManagedRegister dest_base,
                                Offset dest_offset,
                                FrameOffset src,
                                ManagedRegister mscratch,
                                size_t size) {
  Register scratch = mscratch.AsArm().AsCoreRegister();
  CHECK_EQ(size, 4u);
  __ LoadFromOffset(kLoadWord, scratch, SP, src.Int32Value());
  __ StoreToOffset(kStoreWord,
                   scratch,
                   dest_base.AsArm().AsCoreRegister(),
                   dest_offset.Int32Value());
}

void ArmJNIMacroAssembler::Copy(FrameOffset /*dst*/,
                                FrameOffset /*src_base*/,
                                Offset /*src_offset*/,
                                ManagedRegister /*mscratch*/,
                                size_t /*size*/) {
  UNIMPLEMENTED(FATAL);
}

void ArmJNIMacroAssembler::Copy(ManagedRegister dest,
                                Offset dest_offset,
                                ManagedRegister src,
                                Offset src_offset,
                                ManagedRegister mscratch,
                                size_t size) {
  CHECK_EQ(size, 4u);
  Register scratch = mscratch.AsArm().AsCoreRegister();
  __ LoadFromOffset(kLoadWord, scratch, src.AsArm().AsCoreRegister(), src_offset.Int32Value());
  __ StoreToOffset(kStoreWord, scratch, dest.AsArm().AsCoreRegister(), dest_offset.Int32Value());
}

void ArmJNIMacroAssembler::Copy(FrameOffset /*dst*/,
                                Offset /*dest_offset*/,
                                FrameOffset /*src*/,
                                Offset /*src_offset*/,
                                ManagedRegister /*scratch*/,
                                size_t /*size*/) {
  UNIMPLEMENTED(FATAL);
}

void ArmJNIMacroAssembler::CreateHandleScopeEntry(ManagedRegister mout_reg,
                                                  FrameOffset handle_scope_offset,
                                                  ManagedRegister min_reg,
                                                  bool null_allowed) {
  ArmManagedRegister out_reg = mout_reg.AsArm();
  ArmManagedRegister in_reg = min_reg.AsArm();
  CHECK(in_reg.IsNoRegister() || in_reg.IsCoreRegister()) << in_reg;
  CHECK(out_reg.IsCoreRegister()) << out_reg;
  if (null_allowed) {
    // Null values get a handle scope entry value of 0.  Otherwise, the handle scope entry is
    // the address in the handle scope holding the reference.
    // e.g. out_reg = (handle == 0) ? 0 : (SP+handle_offset)
    if (in_reg.IsNoRegister()) {
      __ LoadFromOffset(kLoadWord, out_reg.AsCoreRegister(), SP, handle_scope_offset.Int32Value());
      in_reg = out_reg;
    }
    __ cmp(in_reg.AsCoreRegister(), ShifterOperand(0));
    if (!out_reg.Equals(in_reg)) {
      __ it(EQ, kItElse);
      __ LoadImmediate(out_reg.AsCoreRegister(), 0, EQ);
    } else {
      __ it(NE);
    }
    __ AddConstant(out_reg.AsCoreRegister(), SP, handle_scope_offset.Int32Value(), NE);
  } else {
    __ AddConstant(out_reg.AsCoreRegister(), SP, handle_scope_offset.Int32Value(), AL);
  }
}

void ArmJNIMacroAssembler::CreateHandleScopeEntry(FrameOffset out_off,
                                                  FrameOffset handle_scope_offset,
                                                  ManagedRegister mscratch,
                                                  bool null_allowed) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  if (null_allowed) {
    __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, handle_scope_offset.Int32Value());
    // Null values get a handle scope entry value of 0.  Otherwise, the handle scope entry is
    // the address in the handle scope holding the reference.
    // e.g. scratch = (scratch == 0) ? 0 : (SP+handle_scope_offset)
    __ cmp(scratch.AsCoreRegister(), ShifterOperand(0));
    __ it(NE);
    __ AddConstant(scratch.AsCoreRegister(), SP, handle_scope_offset.Int32Value(), NE);
  } else {
    __ AddConstant(scratch.AsCoreRegister(), SP, handle_scope_offset.Int32Value(), AL);
  }
  __ StoreToOffset(kStoreWord, scratch.AsCoreRegister(), SP, out_off.Int32Value());
}

void ArmJNIMacroAssembler::LoadReferenceFromHandleScope(ManagedRegister mout_reg,
                                                        ManagedRegister min_reg) {
  ArmManagedRegister out_reg = mout_reg.AsArm();
  ArmManagedRegister in_reg = min_reg.AsArm();
  CHECK(out_reg.IsCoreRegister()) << out_reg;
  CHECK(in_reg.IsCoreRegister()) << in_reg;
  Label null_arg;
  if (!out_reg.Equals(in_reg)) {
    __ LoadImmediate(out_reg.AsCoreRegister(), 0, EQ);     // TODO: why EQ?
  }
  __ cmp(in_reg.AsCoreRegister(), ShifterOperand(0));
  __ it(NE);
  __ LoadFromOffset(kLoadWord, out_reg.AsCoreRegister(), in_reg.AsCoreRegister(), 0, NE);
}

void ArmJNIMacroAssembler::VerifyObject(ManagedRegister /*src*/, bool /*could_be_null*/) {
  // TODO: not validating references.
}

void ArmJNIMacroAssembler::VerifyObject(FrameOffset /*src*/, bool /*could_be_null*/) {
  // TODO: not validating references.
}

void ArmJNIMacroAssembler::Call(ManagedRegister mbase,
                                Offset offset,
                                ManagedRegister mscratch) {
  ArmManagedRegister base = mbase.AsArm();
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(base.IsCoreRegister()) << base;
  CHECK(scratch.IsCoreRegister()) << scratch;
  __ LoadFromOffset(kLoadWord,
                    scratch.AsCoreRegister(),
                    base.AsCoreRegister(),
                    offset.Int32Value());
  __ blx(scratch.AsCoreRegister());
  // TODO: place reference map on call.
}

void ArmJNIMacroAssembler::Call(FrameOffset base, Offset offset, ManagedRegister mscratch) {
  ArmManagedRegister scratch = mscratch.AsArm();
  CHECK(scratch.IsCoreRegister()) << scratch;
  // Call *(*(SP + base) + offset)
  __ LoadFromOffset(kLoadWord, scratch.AsCoreRegister(), SP, base.Int32Value());
  __ LoadFromOffset(kLoadWord,
                    scratch.AsCoreRegister(),
                    scratch.AsCoreRegister(),
                    offset.Int32Value());
  __ blx(scratch.AsCoreRegister());
  // TODO: place reference map on call
}

void ArmJNIMacroAssembler::CallFromThread(ThreadOffset32 offset ATTRIBUTE_UNUSED,
                                          ManagedRegister scratch ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL);
}

void ArmJNIMacroAssembler::GetCurrentThread(ManagedRegister tr) {
  __ mov(tr.AsArm().AsCoreRegister(), ShifterOperand(TR));
}

void ArmJNIMacroAssembler::GetCurrentThread(FrameOffset offset, ManagedRegister /*scratch*/) {
  __ StoreToOffset(kStoreWord, TR, SP, offset.Int32Value(), AL);
}

void ArmJNIMacroAssembler::ExceptionPoll(ManagedRegister mscratch, size_t stack_adjust) {
  ArmManagedRegister scratch = mscratch.AsArm();
  ArmExceptionSlowPath* slow = new (__ GetArena()) ArmExceptionSlowPath(scratch, stack_adjust);
  __ GetBuffer()->EnqueueSlowPath(slow);
  __ LoadFromOffset(kLoadWord,
                    scratch.AsCoreRegister(),
                    TR,
                    Thread::ExceptionOffset<kArmPointerSize>().Int32Value());
  __ cmp(scratch.AsCoreRegister(), ShifterOperand(0));
  __ b(slow->Entry(), NE);
}

std::unique_ptr<JNIMacroLabel> ArmJNIMacroAssembler::CreateLabel() {
  return std::unique_ptr<JNIMacroLabel>(new ArmJNIMacroLabel());
}

void ArmJNIMacroAssembler::Jump(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ b(ArmJNIMacroLabel::Cast(label)->AsArm());
}

void ArmJNIMacroAssembler::Jump(JNIMacroLabel* label,
                                JNIMacroUnaryCondition condition,
                                ManagedRegister test) {
  CHECK(label != nullptr);

  arm::Condition arm_cond;
  switch (condition) {
    case JNIMacroUnaryCondition::kZero:
      arm_cond = EQ;
      break;
    case JNIMacroUnaryCondition::kNotZero:
      arm_cond = NE;
      break;
    default:
      LOG(FATAL) << "Not implemented condition: " << static_cast<int>(condition);
      UNREACHABLE();
  }
  __ cmp(test.AsArm().AsCoreRegister(), ShifterOperand(0));
  __ b(ArmJNIMacroLabel::Cast(label)->AsArm(), arm_cond);
}

void ArmJNIMacroAssembler::Bind(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ Bind(ArmJNIMacroLabel::Cast(label)->AsArm());
}

#undef __

void ArmExceptionSlowPath::Emit(Assembler* sasm) {
  ArmAssembler* sp_asm = down_cast<ArmAssembler*>(sasm);
#define __ sp_asm->
  __ Bind(&entry_);
  if (stack_adjust_ != 0) {  // Fix up the frame.
    DecreaseFrameSizeImpl(sp_asm, stack_adjust_);
  }
  // Pass exception object as argument.
  // Don't care about preserving R0 as this call won't return.
  __ mov(R0, ShifterOperand(scratch_.AsCoreRegister()));
  // Set up call to Thread::Current()->pDeliverException.
  __ LoadFromOffset(kLoadWord,
                    R12,
                    TR,
                    QUICK_ENTRYPOINT_OFFSET(kArmPointerSize, pDeliverException).Int32Value());
  __ blx(R12);
#undef __
}

void ArmJNIMacroAssembler::MemoryBarrier(ManagedRegister mscratch) {
  CHECK_EQ(mscratch.AsArm().AsCoreRegister(), R12);
  asm_->dmb(SY);
}

}  // namespace arm
}  // namespace art
