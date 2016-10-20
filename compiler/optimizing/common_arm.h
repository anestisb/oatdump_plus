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

#ifndef ART_COMPILER_OPTIMIZING_COMMON_ARM_H_
#define ART_COMPILER_OPTIMIZING_COMMON_ARM_H_

// TODO(VIXL): Make VIXL compile with -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch32/macro-assembler-aarch32.h"
#pragma GCC diagnostic pop

namespace art {
namespace arm {
namespace helpers {

static_assert(vixl::aarch32::kSpCode == SP, "vixl::aarch32::kSpCode must equal ART's SP");

inline dwarf::Reg DWARFReg(vixl::aarch32::Register reg) {
  return dwarf::Reg::ArmCore(static_cast<int>(reg.GetCode()));
}

inline dwarf::Reg DWARFReg(vixl::aarch32::SRegister reg) {
  return dwarf::Reg::ArmFp(static_cast<int>(reg.GetCode()));
}

inline vixl::aarch32::DRegister FromLowSToD(vixl::aarch32::SRegister reg) {
  DCHECK_EQ(reg.GetCode() % 2, 0u) << reg;
  return vixl::aarch32::DRegister(reg.GetCode() / 2);
}

inline vixl::aarch32::Register HighRegisterFrom(Location location) {
  DCHECK(location.IsRegisterPair()) << location;
  return vixl::aarch32::Register(location.AsRegisterPairHigh<vixl32::Register>());
}

inline vixl::aarch32::DRegister HighDRegisterFrom(Location location) {
  DCHECK(location.IsFpuRegisterPair()) << location;
  return vixl::aarch32::DRegister(location.AsFpuRegisterPairHigh<vixl32::DRegister>());
}

inline vixl::aarch32::Register LowRegisterFrom(Location location) {
  DCHECK(location.IsRegisterPair()) << location;
  return vixl::aarch32::Register(location.AsRegisterPairLow<vixl32::Register>());
}

inline vixl::aarch32::SRegister LowSRegisterFrom(Location location) {
  DCHECK(location.IsFpuRegisterPair()) << location;
  return vixl::aarch32::SRegister(location.AsFpuRegisterPairLow<vixl32::SRegister>());
}

inline vixl::aarch32::Register RegisterFrom(Location location) {
  DCHECK(location.IsRegister()) << location;
  return vixl::aarch32::Register(location.reg());
}

inline vixl::aarch32::Register RegisterFrom(Location location, Primitive::Type type) {
  DCHECK(type != Primitive::kPrimVoid && !Primitive::IsFloatingPointType(type)) << type;
  return RegisterFrom(location);
}

inline vixl::aarch32::DRegister DRegisterFrom(Location location) {
  DCHECK(location.IsFpuRegisterPair()) << location;
  int reg_code = location.low();
  DCHECK_EQ(reg_code % 2, 0) << reg_code;
  return vixl::aarch32::DRegister(reg_code / 2);
}

inline vixl::aarch32::SRegister SRegisterFrom(Location location) {
  DCHECK(location.IsFpuRegister()) << location;
  return vixl::aarch32::SRegister(location.reg());
}

inline vixl::aarch32::SRegister OutputSRegister(HInstruction* instr) {
  Primitive::Type type = instr->GetType();
  DCHECK_EQ(type, Primitive::kPrimFloat) << type;
  return SRegisterFrom(instr->GetLocations()->Out());
}

inline vixl::aarch32::DRegister OutputDRegister(HInstruction* instr) {
  Primitive::Type type = instr->GetType();
  DCHECK_EQ(type, Primitive::kPrimDouble) << type;
  return DRegisterFrom(instr->GetLocations()->Out());
}

inline vixl::aarch32::VRegister OutputVRegister(HInstruction* instr) {
  Primitive::Type type = instr->GetType();
  if (type == Primitive::kPrimFloat) {
    return OutputSRegister(instr);
  } else {
    return OutputDRegister(instr);
  }
}

inline vixl::aarch32::SRegister InputSRegisterAt(HInstruction* instr, int input_index) {
  Primitive::Type type = instr->InputAt(input_index)->GetType();
  DCHECK_EQ(type, Primitive::kPrimFloat) << type;
  return SRegisterFrom(instr->GetLocations()->InAt(input_index));
}

inline vixl::aarch32::DRegister InputDRegisterAt(HInstruction* instr, int input_index) {
  Primitive::Type type = instr->InputAt(input_index)->GetType();
  DCHECK_EQ(type, Primitive::kPrimDouble) << type;
  return DRegisterFrom(instr->GetLocations()->InAt(input_index));
}

inline vixl::aarch32::VRegister InputVRegisterAt(HInstruction* instr, int input_index) {
  Primitive::Type type = instr->InputAt(input_index)->GetType();
  if (type == Primitive::kPrimFloat) {
    return InputSRegisterAt(instr, input_index);
  } else {
    return InputDRegisterAt(instr, input_index);
  }
}

inline vixl::aarch32::Register OutputRegister(HInstruction* instr) {
  return RegisterFrom(instr->GetLocations()->Out(), instr->GetType());
}

inline vixl::aarch32::Register InputRegisterAt(HInstruction* instr, int input_index) {
  return RegisterFrom(instr->GetLocations()->InAt(input_index),
                      instr->InputAt(input_index)->GetType());
}

inline int64_t Int64ConstantFrom(Location location) {
  HConstant* instr = location.GetConstant();
  if (instr->IsIntConstant()) {
    return instr->AsIntConstant()->GetValue();
  } else if (instr->IsNullConstant()) {
    return 0;
  } else {
    DCHECK(instr->IsLongConstant()) << instr->DebugName();
    return instr->AsLongConstant()->GetValue();
  }
}

inline vixl::aarch32::Operand OperandFrom(Location location, Primitive::Type type) {
  if (location.IsRegister()) {
    return vixl::aarch32::Operand(RegisterFrom(location, type));
  } else {
    return vixl::aarch32::Operand(Int64ConstantFrom(location));
  }
}

inline vixl::aarch32::Operand InputOperandAt(HInstruction* instr, int input_index) {
  return OperandFrom(instr->GetLocations()->InAt(input_index),
                     instr->InputAt(input_index)->GetType());
}

inline Location LocationFrom(const vixl::aarch32::Register& reg) {
  return Location::RegisterLocation(reg.GetCode());
}

inline Location LocationFrom(const vixl::aarch32::SRegister& reg) {
  return Location::FpuRegisterLocation(reg.GetCode());
}

inline Location LocationFrom(const vixl::aarch32::Register& low,
                             const vixl::aarch32::Register& high) {
  return Location::RegisterPairLocation(low.GetCode(), high.GetCode());
}

inline Location LocationFrom(const vixl::aarch32::SRegister& low,
                             const vixl::aarch32::SRegister& high) {
  return Location::FpuRegisterPairLocation(low.GetCode(), high.GetCode());
}

}  // namespace helpers
}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_COMMON_ARM_H_
