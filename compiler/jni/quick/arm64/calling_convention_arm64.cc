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

#include "base/logging.h"
#include "calling_convention_arm64.h"
#include "handle_scope-inl.h"
#include "utils/arm64/managed_register_arm64.h"

namespace art {
namespace arm64 {

static const XRegister kXArgumentRegisters[] = {
  X0, X1, X2, X3, X4, X5, X6, X7
};

static const WRegister kWArgumentRegisters[] = {
  W0, W1, W2, W3, W4, W5, W6, W7
};

static const DRegister kDArgumentRegisters[] = {
  D0, D1, D2, D3, D4, D5, D6, D7
};

static const SRegister kSArgumentRegisters[] = {
  S0, S1, S2, S3, S4, S5, S6, S7
};

static constexpr ManagedRegister kCalleeSaveRegisters[] = {
    // Core registers.
    // Note: The native jni function may call to some VM runtime functions which may suspend
    // or trigger GC. And the jni method frame will become top quick frame in those cases.
    // So we need to satisfy GC to save LR and callee-save registers which is similar to
    // CalleeSaveMethod(RefOnly) frame.
    // Jni function is the native function which the java code wants to call.
    // Jni method is the method that is compiled by jni compiler.
    // Call chain: managed code(java) --> jni method --> jni function.
    // Thread register(X19) is saved on stack.
    Arm64ManagedRegister::FromXRegister(X19),
    Arm64ManagedRegister::FromXRegister(X20),
    Arm64ManagedRegister::FromXRegister(X21),
    Arm64ManagedRegister::FromXRegister(X22),
    Arm64ManagedRegister::FromXRegister(X23),
    Arm64ManagedRegister::FromXRegister(X24),
    Arm64ManagedRegister::FromXRegister(X25),
    Arm64ManagedRegister::FromXRegister(X26),
    Arm64ManagedRegister::FromXRegister(X27),
    Arm64ManagedRegister::FromXRegister(X28),
    Arm64ManagedRegister::FromXRegister(X29),
    Arm64ManagedRegister::FromXRegister(LR),
    // Hard float registers.
    // Considering the case, java_method_1 --> jni method --> jni function --> java_method_2,
    // we may break on java_method_2 and we still need to find out the values of DEX registers
    // in java_method_1. So all callee-saves(in managed code) need to be saved.
    Arm64ManagedRegister::FromDRegister(D8),
    Arm64ManagedRegister::FromDRegister(D9),
    Arm64ManagedRegister::FromDRegister(D10),
    Arm64ManagedRegister::FromDRegister(D11),
    Arm64ManagedRegister::FromDRegister(D12),
    Arm64ManagedRegister::FromDRegister(D13),
    Arm64ManagedRegister::FromDRegister(D14),
    Arm64ManagedRegister::FromDRegister(D15),
};

static constexpr uint32_t CalculateCoreCalleeSpillMask() {
  uint32_t result = 0u;
  for (auto&& r : kCalleeSaveRegisters) {
    if (r.AsArm64().IsXRegister()) {
      result |= (1 << r.AsArm64().AsXRegister());
    }
  }
  return result;
}

static constexpr uint32_t CalculateFpCalleeSpillMask() {
  uint32_t result = 0;
  for (auto&& r : kCalleeSaveRegisters) {
    if (r.AsArm64().IsDRegister()) {
      result |= (1 << r.AsArm64().AsDRegister());
    }
  }
  return result;
}

static constexpr uint32_t kCoreCalleeSpillMask = CalculateCoreCalleeSpillMask();
static constexpr uint32_t kFpCalleeSpillMask = CalculateFpCalleeSpillMask();

// Calling convention
ManagedRegister Arm64ManagedRuntimeCallingConvention::InterproceduralScratchRegister() {
  return Arm64ManagedRegister::FromXRegister(X20);  // saved on entry restored on exit
}

ManagedRegister Arm64JniCallingConvention::InterproceduralScratchRegister() {
  return Arm64ManagedRegister::FromXRegister(X20);  // saved on entry restored on exit
}

static ManagedRegister ReturnRegisterForShorty(const char* shorty) {
  if (shorty[0] == 'F') {
    return Arm64ManagedRegister::FromSRegister(S0);
  } else if (shorty[0] == 'D') {
    return Arm64ManagedRegister::FromDRegister(D0);
  } else if (shorty[0] == 'J') {
    return Arm64ManagedRegister::FromXRegister(X0);
  } else if (shorty[0] == 'V') {
    return Arm64ManagedRegister::NoRegister();
  } else {
    return Arm64ManagedRegister::FromWRegister(W0);
  }
}

ManagedRegister Arm64ManagedRuntimeCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty());
}

ManagedRegister Arm64JniCallingConvention::ReturnRegister() {
  return ReturnRegisterForShorty(GetShorty());
}

ManagedRegister Arm64JniCallingConvention::IntReturnRegister() {
  return Arm64ManagedRegister::FromWRegister(W0);
}

// Managed runtime calling convention

ManagedRegister Arm64ManagedRuntimeCallingConvention::MethodRegister() {
  return Arm64ManagedRegister::FromXRegister(X0);
}

bool Arm64ManagedRuntimeCallingConvention::IsCurrentParamInRegister() {
  return false;  // Everything moved to stack on entry.
}

bool Arm64ManagedRuntimeCallingConvention::IsCurrentParamOnStack() {
  return true;
}

ManagedRegister Arm64ManagedRuntimeCallingConvention::CurrentParamRegister() {
  LOG(FATAL) << "Should not reach here";
  return ManagedRegister::NoRegister();
}

FrameOffset Arm64ManagedRuntimeCallingConvention::CurrentParamStackOffset() {
  CHECK(IsCurrentParamOnStack());
  FrameOffset result =
      FrameOffset(displacement_.Int32Value() +  // displacement
                  kFramePointerSize +  // Method ref
                  (itr_slots_ * sizeof(uint32_t)));  // offset into in args
  return result;
}

const ManagedRegisterEntrySpills& Arm64ManagedRuntimeCallingConvention::EntrySpills() {
  // We spill the argument registers on ARM64 to free them up for scratch use, we then assume
  // all arguments are on the stack.
  if ((entry_spills_.size() == 0) && (NumArgs() > 0)) {
    int gp_reg_index = 1;   // we start from X1/W1, X0 holds ArtMethod*.
    int fp_reg_index = 0;   // D0/S0.

    // We need to choose the correct register (D/S or X/W) since the managed
    // stack uses 32bit stack slots.
    ResetIterator(FrameOffset(0));
    while (HasNext()) {
      if (IsCurrentParamAFloatOrDouble()) {  // FP regs.
          if (fp_reg_index < 8) {
            if (!IsCurrentParamADouble()) {
              entry_spills_.push_back(Arm64ManagedRegister::FromSRegister(kSArgumentRegisters[fp_reg_index]));
            } else {
              entry_spills_.push_back(Arm64ManagedRegister::FromDRegister(kDArgumentRegisters[fp_reg_index]));
            }
            fp_reg_index++;
          } else {  // just increase the stack offset.
            if (!IsCurrentParamADouble()) {
              entry_spills_.push_back(ManagedRegister::NoRegister(), 4);
            } else {
              entry_spills_.push_back(ManagedRegister::NoRegister(), 8);
            }
          }
      } else {  // GP regs.
        if (gp_reg_index < 8) {
          if (IsCurrentParamALong() && (!IsCurrentParamAReference())) {
            entry_spills_.push_back(Arm64ManagedRegister::FromXRegister(kXArgumentRegisters[gp_reg_index]));
          } else {
            entry_spills_.push_back(Arm64ManagedRegister::FromWRegister(kWArgumentRegisters[gp_reg_index]));
          }
          gp_reg_index++;
        } else {  // just increase the stack offset.
          if (IsCurrentParamALong() && (!IsCurrentParamAReference())) {
              entry_spills_.push_back(ManagedRegister::NoRegister(), 8);
          } else {
              entry_spills_.push_back(ManagedRegister::NoRegister(), 4);
          }
        }
      }
      Next();
    }
  }
  return entry_spills_;
}

// JNI calling convention
Arm64JniCallingConvention::Arm64JniCallingConvention(bool is_static, bool is_synchronized,
                                                     const char* shorty)
    : JniCallingConvention(is_static, is_synchronized, shorty, kFramePointerSize) {
}

uint32_t Arm64JniCallingConvention::CoreSpillMask() const {
  return kCoreCalleeSpillMask;
}

uint32_t Arm64JniCallingConvention::FpSpillMask() const {
  return kFpCalleeSpillMask;
}

ManagedRegister Arm64JniCallingConvention::ReturnScratchRegister() const {
  return ManagedRegister::NoRegister();
}

size_t Arm64JniCallingConvention::FrameSize() {
  // Method*, callee save area size, local reference segment state
  size_t frame_data_size = kFramePointerSize +
      CalleeSaveRegisters().size() * kFramePointerSize + sizeof(uint32_t);
  // References plus 2 words for HandleScope header
  size_t handle_scope_size = HandleScope::SizeOf(kFramePointerSize, ReferenceCount());
  // Plus return value spill area size
  return RoundUp(frame_data_size + handle_scope_size + SizeOfReturnValue(), kStackAlignment);
}

size_t Arm64JniCallingConvention::OutArgSize() {
  return RoundUp(NumberOfOutgoingStackArgs() * kFramePointerSize, kStackAlignment);
}

ArrayRef<const ManagedRegister> Arm64JniCallingConvention::CalleeSaveRegisters() const {
  return ArrayRef<const ManagedRegister>(kCalleeSaveRegisters);
}

bool Arm64JniCallingConvention::IsCurrentParamInRegister() {
  if (IsCurrentParamAFloatOrDouble()) {
    return (itr_float_and_doubles_ < 8);
  } else {
    return ((itr_args_ - itr_float_and_doubles_) < 8);
  }
}

bool Arm64JniCallingConvention::IsCurrentParamOnStack() {
  return !IsCurrentParamInRegister();
}

ManagedRegister Arm64JniCallingConvention::CurrentParamRegister() {
  CHECK(IsCurrentParamInRegister());
  if (IsCurrentParamAFloatOrDouble()) {
    CHECK_LT(itr_float_and_doubles_, 8u);
    if (IsCurrentParamADouble()) {
      return Arm64ManagedRegister::FromDRegister(kDArgumentRegisters[itr_float_and_doubles_]);
    } else {
      return Arm64ManagedRegister::FromSRegister(kSArgumentRegisters[itr_float_and_doubles_]);
    }
  } else {
    int gp_reg = itr_args_ - itr_float_and_doubles_;
    CHECK_LT(static_cast<unsigned int>(gp_reg), 8u);
    if (IsCurrentParamALong() || IsCurrentParamAReference() || IsCurrentParamJniEnv())  {
      return Arm64ManagedRegister::FromXRegister(kXArgumentRegisters[gp_reg]);
    } else {
      return Arm64ManagedRegister::FromWRegister(kWArgumentRegisters[gp_reg]);
    }
  }
}

FrameOffset Arm64JniCallingConvention::CurrentParamStackOffset() {
  CHECK(IsCurrentParamOnStack());
  size_t args_on_stack = itr_args_
                  - std::min(8u, itr_float_and_doubles_)
                  - std::min(8u, (itr_args_ - itr_float_and_doubles_));
  size_t offset = displacement_.Int32Value() - OutArgSize() + (args_on_stack * kFramePointerSize);
  CHECK_LT(offset, OutArgSize());
  return FrameOffset(offset);
}

size_t Arm64JniCallingConvention::NumberOfOutgoingStackArgs() {
  // all arguments including JNI args
  size_t all_args = NumArgs() + NumberOfExtraArgumentsForJni();

  size_t all_stack_args = all_args -
            std::min(8u, static_cast<unsigned int>(NumFloatOrDoubleArgs())) -
            std::min(8u, static_cast<unsigned int>((all_args - NumFloatOrDoubleArgs())));

  return all_stack_args;
}

}  // namespace arm64
}  // namespace art
