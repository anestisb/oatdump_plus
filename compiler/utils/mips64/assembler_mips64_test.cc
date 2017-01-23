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

#include "assembler_mips64.h"

#include <inttypes.h>
#include <map>
#include <random>

#include "base/bit_utils.h"
#include "base/stl_util.h"
#include "utils/assembler_test.h"

#define __ GetAssembler()->

namespace art {

struct MIPS64CpuRegisterCompare {
  bool operator()(const mips64::GpuRegister& a, const mips64::GpuRegister& b) const {
    return a < b;
  }
};

class AssemblerMIPS64Test : public AssemblerTest<mips64::Mips64Assembler,
                                                 mips64::GpuRegister,
                                                 mips64::FpuRegister,
                                                 uint32_t> {
 public:
  typedef AssemblerTest<mips64::Mips64Assembler,
                        mips64::GpuRegister,
                        mips64::FpuRegister,
                        uint32_t> Base;

 protected:
  // Get the typically used name for this architecture, e.g., aarch64, x86-64, ...
  std::string GetArchitectureString() OVERRIDE {
    return "mips64";
  }

  std::string GetAssemblerCmdName() OVERRIDE {
    // We assemble and link for MIPS64R6. See GetAssemblerParameters() for details.
    return "gcc";
  }

  std::string GetAssemblerParameters() OVERRIDE {
    // We assemble and link for MIPS64R6. The reason is that object files produced for MIPS64R6
    // (and MIPS32R6) with the GNU assembler don't have correct final offsets in PC-relative
    // branches in the .text section and so they require a relocation pass (there's a relocation
    // section, .rela.text, that has the needed info to fix up the branches).
    return " -march=mips64r6 -Wa,--no-warn -Wl,-Ttext=0 -Wl,-e0 -nostdlib";
  }

  void Pad(std::vector<uint8_t>& data) OVERRIDE {
    // The GNU linker unconditionally pads the code segment with NOPs to a size that is a multiple
    // of 16 and there doesn't appear to be a way to suppress this padding. Our assembler doesn't
    // pad, so, in order for two assembler outputs to match, we need to match the padding as well.
    // NOP is encoded as four zero bytes on MIPS.
    size_t pad_size = RoundUp(data.size(), 16u) - data.size();
    data.insert(data.end(), pad_size, 0);
  }

  std::string GetDisassembleParameters() OVERRIDE {
    return " -D -bbinary -mmips:isa64r6";
  }

  void SetUpHelpers() OVERRIDE {
    if (registers_.size() == 0) {
      registers_.push_back(new mips64::GpuRegister(mips64::ZERO));
      registers_.push_back(new mips64::GpuRegister(mips64::AT));
      registers_.push_back(new mips64::GpuRegister(mips64::V0));
      registers_.push_back(new mips64::GpuRegister(mips64::V1));
      registers_.push_back(new mips64::GpuRegister(mips64::A0));
      registers_.push_back(new mips64::GpuRegister(mips64::A1));
      registers_.push_back(new mips64::GpuRegister(mips64::A2));
      registers_.push_back(new mips64::GpuRegister(mips64::A3));
      registers_.push_back(new mips64::GpuRegister(mips64::A4));
      registers_.push_back(new mips64::GpuRegister(mips64::A5));
      registers_.push_back(new mips64::GpuRegister(mips64::A6));
      registers_.push_back(new mips64::GpuRegister(mips64::A7));
      registers_.push_back(new mips64::GpuRegister(mips64::T0));
      registers_.push_back(new mips64::GpuRegister(mips64::T1));
      registers_.push_back(new mips64::GpuRegister(mips64::T2));
      registers_.push_back(new mips64::GpuRegister(mips64::T3));
      registers_.push_back(new mips64::GpuRegister(mips64::S0));
      registers_.push_back(new mips64::GpuRegister(mips64::S1));
      registers_.push_back(new mips64::GpuRegister(mips64::S2));
      registers_.push_back(new mips64::GpuRegister(mips64::S3));
      registers_.push_back(new mips64::GpuRegister(mips64::S4));
      registers_.push_back(new mips64::GpuRegister(mips64::S5));
      registers_.push_back(new mips64::GpuRegister(mips64::S6));
      registers_.push_back(new mips64::GpuRegister(mips64::S7));
      registers_.push_back(new mips64::GpuRegister(mips64::T8));
      registers_.push_back(new mips64::GpuRegister(mips64::T9));
      registers_.push_back(new mips64::GpuRegister(mips64::K0));
      registers_.push_back(new mips64::GpuRegister(mips64::K1));
      registers_.push_back(new mips64::GpuRegister(mips64::GP));
      registers_.push_back(new mips64::GpuRegister(mips64::SP));
      registers_.push_back(new mips64::GpuRegister(mips64::S8));
      registers_.push_back(new mips64::GpuRegister(mips64::RA));

      secondary_register_names_.emplace(mips64::GpuRegister(mips64::ZERO), "zero");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::AT), "at");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::V0), "v0");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::V1), "v1");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A0), "a0");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A1), "a1");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A2), "a2");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A3), "a3");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A4), "a4");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A5), "a5");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A6), "a6");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::A7), "a7");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T0), "t0");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T1), "t1");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T2), "t2");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T3), "t3");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S0), "s0");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S1), "s1");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S2), "s2");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S3), "s3");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S4), "s4");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S5), "s5");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S6), "s6");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S7), "s7");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T8), "t8");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::T9), "t9");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::K0), "k0");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::K1), "k1");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::GP), "gp");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::SP), "sp");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::S8), "s8");
      secondary_register_names_.emplace(mips64::GpuRegister(mips64::RA), "ra");

      fp_registers_.push_back(new mips64::FpuRegister(mips64::F0));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F1));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F2));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F3));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F4));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F5));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F6));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F7));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F8));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F9));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F10));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F11));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F12));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F13));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F14));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F15));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F16));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F17));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F18));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F19));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F20));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F21));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F22));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F23));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F24));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F25));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F26));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F27));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F28));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F29));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F30));
      fp_registers_.push_back(new mips64::FpuRegister(mips64::F31));
    }
  }

  void TearDown() OVERRIDE {
    AssemblerTest::TearDown();
    STLDeleteElements(&registers_);
    STLDeleteElements(&fp_registers_);
  }

  std::vector<mips64::GpuRegister*> GetRegisters() OVERRIDE {
    return registers_;
  }

  std::vector<mips64::FpuRegister*> GetFPRegisters() OVERRIDE {
    return fp_registers_;
  }

  uint32_t CreateImmediate(int64_t imm_value) OVERRIDE {
    return imm_value;
  }

  std::string GetSecondaryRegisterName(const mips64::GpuRegister& reg) OVERRIDE {
    CHECK(secondary_register_names_.find(reg) != secondary_register_names_.end());
    return secondary_register_names_[reg];
  }

  std::string RepeatInsn(size_t count, const std::string& insn) {
    std::string result;
    for (; count != 0u; --count) {
      result += insn;
    }
    return result;
  }

  void BranchCondOneRegHelper(void (mips64::Mips64Assembler::*f)(mips64::GpuRegister,
                                                                 mips64::Mips64Label*),
                              const std::string& instr_name) {
    mips64::Mips64Label label;
    (Base::GetAssembler()->*f)(mips64::A0, &label);
    constexpr size_t kAdduCount1 = 63;
    for (size_t i = 0; i != kAdduCount1; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bind(&label);
    constexpr size_t kAdduCount2 = 64;
    for (size_t i = 0; i != kAdduCount2; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    (Base::GetAssembler()->*f)(mips64::A1, &label);

    std::string expected =
        ".set noreorder\n" +
        instr_name + " $a0, 1f\n"
        "nop\n" +
        RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
        "1:\n" +
        RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
        instr_name + " $a1, 1b\n"
        "nop\n";
    DriverStr(expected, instr_name);
  }

  void BranchCondTwoRegsHelper(void (mips64::Mips64Assembler::*f)(mips64::GpuRegister,
                                                                  mips64::GpuRegister,
                                                                  mips64::Mips64Label*),
                               const std::string& instr_name) {
    mips64::Mips64Label label;
    (Base::GetAssembler()->*f)(mips64::A0, mips64::A1, &label);
    constexpr size_t kAdduCount1 = 63;
    for (size_t i = 0; i != kAdduCount1; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bind(&label);
    constexpr size_t kAdduCount2 = 64;
    for (size_t i = 0; i != kAdduCount2; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    (Base::GetAssembler()->*f)(mips64::A2, mips64::A3, &label);

    std::string expected =
        ".set noreorder\n" +
        instr_name + " $a0, $a1, 1f\n"
        "nop\n" +
        RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
        "1:\n" +
        RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
        instr_name + " $a2, $a3, 1b\n"
        "nop\n";
    DriverStr(expected, instr_name);
  }

 private:
  std::vector<mips64::GpuRegister*> registers_;
  std::map<mips64::GpuRegister, std::string, MIPS64CpuRegisterCompare> secondary_register_names_;

  std::vector<mips64::FpuRegister*> fp_registers_;
};


TEST_F(AssemblerMIPS64Test, Toolchain) {
  EXPECT_TRUE(CheckTools());
}

///////////////////
// FP Operations //
///////////////////

TEST_F(AssemblerMIPS64Test, AddS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::AddS, "add.s ${reg1}, ${reg2}, ${reg3}"), "add.s");
}

TEST_F(AssemblerMIPS64Test, AddD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::AddD, "add.d ${reg1}, ${reg2}, ${reg3}"), "add.d");
}

TEST_F(AssemblerMIPS64Test, SubS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::SubS, "sub.s ${reg1}, ${reg2}, ${reg3}"), "sub.s");
}

TEST_F(AssemblerMIPS64Test, SubD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::SubD, "sub.d ${reg1}, ${reg2}, ${reg3}"), "sub.d");
}

TEST_F(AssemblerMIPS64Test, MulS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MulS, "mul.s ${reg1}, ${reg2}, ${reg3}"), "mul.s");
}

TEST_F(AssemblerMIPS64Test, MulD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MulD, "mul.d ${reg1}, ${reg2}, ${reg3}"), "mul.d");
}

TEST_F(AssemblerMIPS64Test, DivS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::DivS, "div.s ${reg1}, ${reg2}, ${reg3}"), "div.s");
}

TEST_F(AssemblerMIPS64Test, DivD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::DivD, "div.d ${reg1}, ${reg2}, ${reg3}"), "div.d");
}

TEST_F(AssemblerMIPS64Test, SqrtS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::SqrtS, "sqrt.s ${reg1}, ${reg2}"), "sqrt.s");
}

TEST_F(AssemblerMIPS64Test, SqrtD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::SqrtD, "sqrt.d ${reg1}, ${reg2}"), "sqrt.d");
}

TEST_F(AssemblerMIPS64Test, AbsS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::AbsS, "abs.s ${reg1}, ${reg2}"), "abs.s");
}

TEST_F(AssemblerMIPS64Test, AbsD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::AbsD, "abs.d ${reg1}, ${reg2}"), "abs.d");
}

TEST_F(AssemblerMIPS64Test, MovS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::MovS, "mov.s ${reg1}, ${reg2}"), "mov.s");
}

TEST_F(AssemblerMIPS64Test, MovD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::MovD, "mov.d ${reg1}, ${reg2}"), "mov.d");
}

TEST_F(AssemblerMIPS64Test, NegS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::NegS, "neg.s ${reg1}, ${reg2}"), "neg.s");
}

TEST_F(AssemblerMIPS64Test, NegD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::NegD, "neg.d ${reg1}, ${reg2}"), "neg.d");
}

TEST_F(AssemblerMIPS64Test, RoundLS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RoundLS, "round.l.s ${reg1}, ${reg2}"), "round.l.s");
}

TEST_F(AssemblerMIPS64Test, RoundLD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RoundLD, "round.l.d ${reg1}, ${reg2}"), "round.l.d");
}

TEST_F(AssemblerMIPS64Test, RoundWS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RoundWS, "round.w.s ${reg1}, ${reg2}"), "round.w.s");
}

TEST_F(AssemblerMIPS64Test, RoundWD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RoundWD, "round.w.d ${reg1}, ${reg2}"), "round.w.d");
}

TEST_F(AssemblerMIPS64Test, CeilLS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::CeilLS, "ceil.l.s ${reg1}, ${reg2}"), "ceil.l.s");
}

TEST_F(AssemblerMIPS64Test, CeilLD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::CeilLD, "ceil.l.d ${reg1}, ${reg2}"), "ceil.l.d");
}

TEST_F(AssemblerMIPS64Test, CeilWS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::CeilWS, "ceil.w.s ${reg1}, ${reg2}"), "ceil.w.s");
}

TEST_F(AssemblerMIPS64Test, CeilWD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::CeilWD, "ceil.w.d ${reg1}, ${reg2}"), "ceil.w.d");
}

TEST_F(AssemblerMIPS64Test, FloorLS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::FloorLS, "floor.l.s ${reg1}, ${reg2}"), "floor.l.s");
}

TEST_F(AssemblerMIPS64Test, FloorLD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::FloorLD, "floor.l.d ${reg1}, ${reg2}"), "floor.l.d");
}

TEST_F(AssemblerMIPS64Test, FloorWS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::FloorWS, "floor.w.s ${reg1}, ${reg2}"), "floor.w.s");
}

TEST_F(AssemblerMIPS64Test, FloorWD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::FloorWD, "floor.w.d ${reg1}, ${reg2}"), "floor.w.d");
}

TEST_F(AssemblerMIPS64Test, SelS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::SelS, "sel.s ${reg1}, ${reg2}, ${reg3}"), "sel.s");
}

TEST_F(AssemblerMIPS64Test, SelD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::SelD, "sel.d ${reg1}, ${reg2}, ${reg3}"), "sel.d");
}

TEST_F(AssemblerMIPS64Test, RintS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RintS, "rint.s ${reg1}, ${reg2}"), "rint.s");
}

TEST_F(AssemblerMIPS64Test, RintD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::RintD, "rint.d ${reg1}, ${reg2}"), "rint.d");
}

TEST_F(AssemblerMIPS64Test, ClassS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::ClassS, "class.s ${reg1}, ${reg2}"), "class.s");
}

TEST_F(AssemblerMIPS64Test, ClassD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::ClassD, "class.d ${reg1}, ${reg2}"), "class.d");
}

TEST_F(AssemblerMIPS64Test, MinS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MinS, "min.s ${reg1}, ${reg2}, ${reg3}"), "min.s");
}

TEST_F(AssemblerMIPS64Test, MinD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MinD, "min.d ${reg1}, ${reg2}, ${reg3}"), "min.d");
}

TEST_F(AssemblerMIPS64Test, MaxS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MaxS, "max.s ${reg1}, ${reg2}, ${reg3}"), "max.s");
}

TEST_F(AssemblerMIPS64Test, MaxD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::MaxD, "max.d ${reg1}, ${reg2}, ${reg3}"), "max.d");
}

TEST_F(AssemblerMIPS64Test, CmpUnS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUnS, "cmp.un.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.un.s");
}

TEST_F(AssemblerMIPS64Test, CmpEqS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpEqS, "cmp.eq.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.eq.s");
}

TEST_F(AssemblerMIPS64Test, CmpUeqS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUeqS, "cmp.ueq.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ueq.s");
}

TEST_F(AssemblerMIPS64Test, CmpLtS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpLtS, "cmp.lt.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.lt.s");
}

TEST_F(AssemblerMIPS64Test, CmpUltS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUltS, "cmp.ult.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ult.s");
}

TEST_F(AssemblerMIPS64Test, CmpLeS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpLeS, "cmp.le.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.le.s");
}

TEST_F(AssemblerMIPS64Test, CmpUleS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUleS, "cmp.ule.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ule.s");
}

TEST_F(AssemblerMIPS64Test, CmpOrS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpOrS, "cmp.or.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.or.s");
}

TEST_F(AssemblerMIPS64Test, CmpUneS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUneS, "cmp.une.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.une.s");
}

TEST_F(AssemblerMIPS64Test, CmpNeS) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpNeS, "cmp.ne.s ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ne.s");
}

TEST_F(AssemblerMIPS64Test, CmpUnD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUnD, "cmp.un.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.un.d");
}

TEST_F(AssemblerMIPS64Test, CmpEqD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpEqD, "cmp.eq.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.eq.d");
}

TEST_F(AssemblerMIPS64Test, CmpUeqD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUeqD, "cmp.ueq.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ueq.d");
}

TEST_F(AssemblerMIPS64Test, CmpLtD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpLtD, "cmp.lt.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.lt.d");
}

TEST_F(AssemblerMIPS64Test, CmpUltD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUltD, "cmp.ult.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ult.d");
}

TEST_F(AssemblerMIPS64Test, CmpLeD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpLeD, "cmp.le.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.le.d");
}

TEST_F(AssemblerMIPS64Test, CmpUleD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUleD, "cmp.ule.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ule.d");
}

TEST_F(AssemblerMIPS64Test, CmpOrD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpOrD, "cmp.or.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.or.d");
}

TEST_F(AssemblerMIPS64Test, CmpUneD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpUneD, "cmp.une.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.une.d");
}

TEST_F(AssemblerMIPS64Test, CmpNeD) {
  DriverStr(RepeatFFF(&mips64::Mips64Assembler::CmpNeD, "cmp.ne.d ${reg1}, ${reg2}, ${reg3}"),
            "cmp.ne.d");
}

TEST_F(AssemblerMIPS64Test, CvtDL) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtdl, "cvt.d.l ${reg1}, ${reg2}"), "cvt.d.l");
}

TEST_F(AssemblerMIPS64Test, CvtDS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtds, "cvt.d.s ${reg1}, ${reg2}"), "cvt.d.s");
}

TEST_F(AssemblerMIPS64Test, CvtDW) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtdw, "cvt.d.w ${reg1}, ${reg2}"), "cvt.d.w");
}

TEST_F(AssemblerMIPS64Test, CvtSL) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtsl, "cvt.s.l ${reg1}, ${reg2}"), "cvt.s.l");
}

TEST_F(AssemblerMIPS64Test, CvtSD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtsd, "cvt.s.d ${reg1}, ${reg2}"), "cvt.s.d");
}

TEST_F(AssemblerMIPS64Test, CvtSW) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::Cvtsw, "cvt.s.w ${reg1}, ${reg2}"), "cvt.s.w");
}

TEST_F(AssemblerMIPS64Test, TruncWS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::TruncWS, "trunc.w.s ${reg1}, ${reg2}"), "trunc.w.s");
}

TEST_F(AssemblerMIPS64Test, TruncWD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::TruncWD, "trunc.w.d ${reg1}, ${reg2}"), "trunc.w.d");
}

TEST_F(AssemblerMIPS64Test, TruncLS) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::TruncLS, "trunc.l.s ${reg1}, ${reg2}"), "trunc.l.s");
}

TEST_F(AssemblerMIPS64Test, TruncLD) {
  DriverStr(RepeatFF(&mips64::Mips64Assembler::TruncLD, "trunc.l.d ${reg1}, ${reg2}"), "trunc.l.d");
}

TEST_F(AssemblerMIPS64Test, Mfc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Mfc1, "mfc1 ${reg1}, ${reg2}"), "Mfc1");
}

TEST_F(AssemblerMIPS64Test, Mfhc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Mfhc1, "mfhc1 ${reg1}, ${reg2}"), "Mfhc1");
}

TEST_F(AssemblerMIPS64Test, Mtc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Mtc1, "mtc1 ${reg1}, ${reg2}"), "Mtc1");
}

TEST_F(AssemblerMIPS64Test, Mthc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Mthc1, "mthc1 ${reg1}, ${reg2}"), "Mthc1");
}

TEST_F(AssemblerMIPS64Test, Dmfc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Dmfc1, "dmfc1 ${reg1}, ${reg2}"), "Dmfc1");
}

TEST_F(AssemblerMIPS64Test, Dmtc1) {
  DriverStr(RepeatRF(&mips64::Mips64Assembler::Dmtc1, "dmtc1 ${reg1}, ${reg2}"), "Dmtc1");
}

TEST_F(AssemblerMIPS64Test, Lwc1) {
  DriverStr(RepeatFRIb(&mips64::Mips64Assembler::Lwc1, -16, "lwc1 ${reg1}, {imm}(${reg2})"),
            "lwc1");
}

TEST_F(AssemblerMIPS64Test, Ldc1) {
  DriverStr(RepeatFRIb(&mips64::Mips64Assembler::Ldc1, -16, "ldc1 ${reg1}, {imm}(${reg2})"),
            "ldc1");
}

TEST_F(AssemblerMIPS64Test, Swc1) {
  DriverStr(RepeatFRIb(&mips64::Mips64Assembler::Swc1, -16, "swc1 ${reg1}, {imm}(${reg2})"),
            "swc1");
}

TEST_F(AssemblerMIPS64Test, Sdc1) {
  DriverStr(RepeatFRIb(&mips64::Mips64Assembler::Sdc1, -16, "sdc1 ${reg1}, {imm}(${reg2})"),
            "sdc1");
}

////////////////
// CALL / JMP //
////////////////

TEST_F(AssemblerMIPS64Test, Jalr) {
  DriverStr(".set noreorder\n" +
            RepeatRRNoDupes(&mips64::Mips64Assembler::Jalr, "jalr ${reg1}, ${reg2}"), "jalr");
}

TEST_F(AssemblerMIPS64Test, Balc) {
  mips64::Mips64Label label1, label2;
  __ Balc(&label1);
  constexpr size_t kAdduCount1 = 63;
  for (size_t i = 0; i != kAdduCount1; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label1);
  __ Balc(&label2);
  constexpr size_t kAdduCount2 = 64;
  for (size_t i = 0; i != kAdduCount2; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label2);
  __ Balc(&label1);

  std::string expected =
      ".set noreorder\n"
      "balc 1f\n" +
      RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
      "1:\n"
      "balc 2f\n" +
      RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
      "2:\n"
      "balc 1b\n";
  DriverStr(expected, "Balc");
}

TEST_F(AssemblerMIPS64Test, LongBalc) {
  constexpr uint32_t kNopCount1 = (1u << 25) + 1;
  constexpr uint32_t kNopCount2 = (1u << 25) + 1;
  constexpr uint32_t kRequiredCapacity = (kNopCount1 + kNopCount2 + 6u) * 4u;
  ASSERT_LT(__ GetBuffer()->Capacity(), kRequiredCapacity);
  __ GetBuffer()->ExtendCapacity(kRequiredCapacity);
  mips64::Mips64Label label1, label2;
  __ Balc(&label1);
  for (uint32_t i = 0; i != kNopCount1; ++i) {
    __ Nop();
  }
  __ Bind(&label1);
  __ Balc(&label2);
  for (uint32_t i = 0; i != kNopCount2; ++i) {
    __ Nop();
  }
  __ Bind(&label2);
  __ Balc(&label1);

  uint32_t offset_forward1 = 2 + kNopCount1;  // 2: account for auipc and jialc.
  offset_forward1 <<= 2;
  offset_forward1 += (offset_forward1 & 0x8000) << 1;  // Account for sign extension in jialc.

  uint32_t offset_forward2 = 2 + kNopCount2;  // 2: account for auipc and jialc.
  offset_forward2 <<= 2;
  offset_forward2 += (offset_forward2 & 0x8000) << 1;  // Account for sign extension in jialc.

  uint32_t offset_back = -(2 + kNopCount2);  // 2: account for auipc and jialc.
  offset_back <<= 2;
  offset_back += (offset_back & 0x8000) << 1;  // Account for sign extension in jialc.

  // Note, we're using the ".fill" directive to tell the assembler to generate many NOPs
  // instead of generating them ourselves in the source code. This saves a few minutes
  // of test time.
  std::ostringstream oss;
  oss <<
      ".set noreorder\n"
      "auipc $at, 0x" << std::hex << High16Bits(offset_forward1) << "\n"
      "jialc $at, 0x" << std::hex << Low16Bits(offset_forward1) << "\n"
      ".fill 0x" << std::hex << kNopCount1 << " , 4, 0\n"
      "1:\n"
      "auipc $at, 0x" << std::hex << High16Bits(offset_forward2) << "\n"
      "jialc $at, 0x" << std::hex << Low16Bits(offset_forward2) << "\n"
      ".fill 0x" << std::hex << kNopCount2 << " , 4, 0\n"
      "2:\n"
      "auipc $at, 0x" << std::hex << High16Bits(offset_back) << "\n"
      "jialc $at, 0x" << std::hex << Low16Bits(offset_back) << "\n";
  std::string expected = oss.str();
  DriverStr(expected, "LongBalc");
}

TEST_F(AssemblerMIPS64Test, Bc) {
  mips64::Mips64Label label1, label2;
  __ Bc(&label1);
  constexpr size_t kAdduCount1 = 63;
  for (size_t i = 0; i != kAdduCount1; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label1);
  __ Bc(&label2);
  constexpr size_t kAdduCount2 = 64;
  for (size_t i = 0; i != kAdduCount2; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label2);
  __ Bc(&label1);

  std::string expected =
      ".set noreorder\n"
      "bc 1f\n" +
      RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
      "1:\n"
      "bc 2f\n" +
      RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
      "2:\n"
      "bc 1b\n";
  DriverStr(expected, "Bc");
}

TEST_F(AssemblerMIPS64Test, Beqzc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Beqzc, "Beqzc");
}

TEST_F(AssemblerMIPS64Test, Bnezc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Bnezc, "Bnezc");
}

TEST_F(AssemblerMIPS64Test, Bltzc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Bltzc, "Bltzc");
}

TEST_F(AssemblerMIPS64Test, Bgezc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Bgezc, "Bgezc");
}

TEST_F(AssemblerMIPS64Test, Blezc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Blezc, "Blezc");
}

TEST_F(AssemblerMIPS64Test, Bgtzc) {
  BranchCondOneRegHelper(&mips64::Mips64Assembler::Bgtzc, "Bgtzc");
}

TEST_F(AssemblerMIPS64Test, Beqc) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Beqc, "Beqc");
}

TEST_F(AssemblerMIPS64Test, Bnec) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Bnec, "Bnec");
}

TEST_F(AssemblerMIPS64Test, Bltc) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Bltc, "Bltc");
}

TEST_F(AssemblerMIPS64Test, Bgec) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Bgec, "Bgec");
}

TEST_F(AssemblerMIPS64Test, Bltuc) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Bltuc, "Bltuc");
}

TEST_F(AssemblerMIPS64Test, Bgeuc) {
  BranchCondTwoRegsHelper(&mips64::Mips64Assembler::Bgeuc, "Bgeuc");
}

TEST_F(AssemblerMIPS64Test, Bc1eqz) {
    mips64::Mips64Label label;
    __ Bc1eqz(mips64::F0, &label);
    constexpr size_t kAdduCount1 = 63;
    for (size_t i = 0; i != kAdduCount1; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bind(&label);
    constexpr size_t kAdduCount2 = 64;
    for (size_t i = 0; i != kAdduCount2; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bc1eqz(mips64::F31, &label);

    std::string expected =
        ".set noreorder\n"
        "bc1eqz $f0, 1f\n"
        "nop\n" +
        RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
        "1:\n" +
        RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
        "bc1eqz $f31, 1b\n"
        "nop\n";
    DriverStr(expected, "Bc1eqz");
}

TEST_F(AssemblerMIPS64Test, Bc1nez) {
    mips64::Mips64Label label;
    __ Bc1nez(mips64::F0, &label);
    constexpr size_t kAdduCount1 = 63;
    for (size_t i = 0; i != kAdduCount1; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bind(&label);
    constexpr size_t kAdduCount2 = 64;
    for (size_t i = 0; i != kAdduCount2; ++i) {
      __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
    }
    __ Bc1nez(mips64::F31, &label);

    std::string expected =
        ".set noreorder\n"
        "bc1nez $f0, 1f\n"
        "nop\n" +
        RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") +
        "1:\n" +
        RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") +
        "bc1nez $f31, 1b\n"
        "nop\n";
    DriverStr(expected, "Bc1nez");
}

TEST_F(AssemblerMIPS64Test, LongBeqc) {
  mips64::Mips64Label label;
  __ Beqc(mips64::A0, mips64::A1, &label);
  constexpr uint32_t kAdduCount1 = (1u << 15) + 1;
  for (uint32_t i = 0; i != kAdduCount1; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label);
  constexpr uint32_t kAdduCount2 = (1u << 15) + 1;
  for (uint32_t i = 0; i != kAdduCount2; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Beqc(mips64::A2, mips64::A3, &label);

  uint32_t offset_forward = 2 + kAdduCount1;  // 2: account for auipc and jic.
  offset_forward <<= 2;
  offset_forward += (offset_forward & 0x8000) << 1;  // Account for sign extension in jic.

  uint32_t offset_back = -(kAdduCount2 + 1);  // 1: account for bnec.
  offset_back <<= 2;
  offset_back += (offset_back & 0x8000) << 1;  // Account for sign extension in jic.

  std::ostringstream oss;
  oss <<
      ".set noreorder\n"
      "bnec $a0, $a1, 1f\n"
      "auipc $at, 0x" << std::hex << High16Bits(offset_forward) << "\n"
      "jic $at, 0x" << std::hex << Low16Bits(offset_forward) << "\n"
      "1:\n" <<
      RepeatInsn(kAdduCount1, "addu $zero, $zero, $zero\n") <<
      "2:\n" <<
      RepeatInsn(kAdduCount2, "addu $zero, $zero, $zero\n") <<
      "bnec $a2, $a3, 3f\n"
      "auipc $at, 0x" << std::hex << High16Bits(offset_back) << "\n"
      "jic $at, 0x" << std::hex << Low16Bits(offset_back) << "\n"
      "3:\n";
  std::string expected = oss.str();
  DriverStr(expected, "LongBeqc");
}

//////////
// MISC //
//////////

TEST_F(AssemblerMIPS64Test, Lwpc) {
  // Lwpc() takes an unsigned 19-bit immediate, while the GNU assembler needs a signed offset,
  // hence the sign extension from bit 18 with `imm - ((imm & 0x40000) << 1)`.
  // The GNU assembler also wants the offset to be a multiple of 4, which it will shift right
  // by 2 positions when encoding, hence `<< 2` to compensate for that shift.
  // We capture the value of the immediate with `.set imm, {imm}` because the value is needed
  // twice for the sign extension, but `{imm}` is substituted only once.
  const char* code = ".set imm, {imm}\nlw ${reg}, ((imm - ((imm & 0x40000) << 1)) << 2)($pc)";
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Lwpc, 19, code), "Lwpc");
}

TEST_F(AssemblerMIPS64Test, Lwupc) {
  // The comment for the Lwpc test applies here as well.
  const char* code = ".set imm, {imm}\nlwu ${reg}, ((imm - ((imm & 0x40000) << 1)) << 2)($pc)";
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Lwupc, 19, code), "Lwupc");
}

TEST_F(AssemblerMIPS64Test, Ldpc) {
  // The comment for the Lwpc test applies here as well.
  const char* code = ".set imm, {imm}\nld ${reg}, ((imm - ((imm & 0x20000) << 1)) << 3)($pc)";
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Ldpc, 18, code), "Ldpc");
}

TEST_F(AssemblerMIPS64Test, Auipc) {
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Auipc, 16, "auipc ${reg}, {imm}"), "Auipc");
}

TEST_F(AssemblerMIPS64Test, Addiupc) {
  // The comment from the Lwpc() test applies to this Addiupc() test as well.
  const char* code = ".set imm, {imm}\naddiupc ${reg}, (imm - ((imm & 0x40000) << 1)) << 2";
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Addiupc, 19, code), "Addiupc");
}

TEST_F(AssemblerMIPS64Test, LoadFarthestNearLabelAddress) {
  mips64::Mips64Label label;
  __ LoadLabelAddress(mips64::V0, &label);
  constexpr uint32_t kAdduCount = 0x3FFDE;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label);

  std::string expected =
      "lapc $v0, 1f\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "1:\n";
  DriverStr(expected, "LoadFarthestNearLabelAddress");
  EXPECT_EQ(__ GetLabelLocation(&label), (1 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadNearestFarLabelAddress) {
  mips64::Mips64Label label;
  __ LoadLabelAddress(mips64::V0, &label);
  constexpr uint32_t kAdduCount = 0x3FFDF;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  __ Bind(&label);

  std::string expected =
      "1:\n"
      "auipc $at, %hi(2f - 1b)\n"
      "daddiu $v0, $at, %lo(2f - 1b)\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "2:\n";
  DriverStr(expected, "LoadNearestFarLabelAddress");
  EXPECT_EQ(__ GetLabelLocation(&label), (2 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadFarthestNearLiteral) {
  mips64::Literal* literal = __ NewLiteral<uint32_t>(0x12345678);
  __ LoadLiteral(mips64::V0, mips64::kLoadWord, literal);
  constexpr uint32_t kAdduCount = 0x3FFDE;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "lwpc $v0, 1f\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "1:\n"
      ".word 0x12345678\n";
  DriverStr(expected, "LoadFarthestNearLiteral");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (1 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadNearestFarLiteral) {
  mips64::Literal* literal = __ NewLiteral<uint32_t>(0x12345678);
  __ LoadLiteral(mips64::V0, mips64::kLoadWord, literal);
  constexpr uint32_t kAdduCount = 0x3FFDF;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "1:\n"
      "auipc $at, %hi(2f - 1b)\n"
      "lw $v0, %lo(2f - 1b)($at)\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "2:\n"
      ".word 0x12345678\n";
  DriverStr(expected, "LoadNearestFarLiteral");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (2 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadFarthestNearLiteralUnsigned) {
  mips64::Literal* literal = __ NewLiteral<uint32_t>(0x12345678);
  __ LoadLiteral(mips64::V0, mips64::kLoadUnsignedWord, literal);
  constexpr uint32_t kAdduCount = 0x3FFDE;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "lwupc $v0, 1f\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "1:\n"
      ".word 0x12345678\n";
  DriverStr(expected, "LoadFarthestNearLiteralUnsigned");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (1 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadNearestFarLiteralUnsigned) {
  mips64::Literal* literal = __ NewLiteral<uint32_t>(0x12345678);
  __ LoadLiteral(mips64::V0, mips64::kLoadUnsignedWord, literal);
  constexpr uint32_t kAdduCount = 0x3FFDF;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "1:\n"
      "auipc $at, %hi(2f - 1b)\n"
      "lwu $v0, %lo(2f - 1b)($at)\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "2:\n"
      ".word 0x12345678\n";
  DriverStr(expected, "LoadNearestFarLiteralUnsigned");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (2 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadFarthestNearLiteralLong) {
  mips64::Literal* literal = __ NewLiteral<uint64_t>(UINT64_C(0x0123456789ABCDEF));
  __ LoadLiteral(mips64::V0, mips64::kLoadDoubleword, literal);
  constexpr uint32_t kAdduCount = 0x3FFDD;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "ldpc $v0, 1f\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "1:\n"
      ".dword 0x0123456789ABCDEF\n";
  DriverStr(expected, "LoadFarthestNearLiteralLong");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (1 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LoadNearestFarLiteralLong) {
  mips64::Literal* literal = __ NewLiteral<uint64_t>(UINT64_C(0x0123456789ABCDEF));
  __ LoadLiteral(mips64::V0, mips64::kLoadDoubleword, literal);
  constexpr uint32_t kAdduCount = 0x3FFDE;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }

  std::string expected =
      "1:\n"
      "auipc $at, %hi(2f - 1b)\n"
      "ld $v0, %lo(2f - 1b)($at)\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "2:\n"
      ".dword 0x0123456789ABCDEF\n";
  DriverStr(expected, "LoadNearestFarLiteralLong");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (2 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, LongLiteralAlignmentNop) {
  mips64::Literal* literal1 = __ NewLiteral<uint64_t>(UINT64_C(0x0123456789ABCDEF));
  mips64::Literal* literal2 = __ NewLiteral<uint64_t>(UINT64_C(0x5555555555555555));
  mips64::Literal* literal3 = __ NewLiteral<uint64_t>(UINT64_C(0xAAAAAAAAAAAAAAAA));
  __ LoadLiteral(mips64::A1, mips64::kLoadDoubleword, literal1);
  __ LoadLiteral(mips64::A2, mips64::kLoadDoubleword, literal2);
  __ LoadLiteral(mips64::A3, mips64::kLoadDoubleword, literal3);
  __ LoadLabelAddress(mips64::V0, literal1->GetLabel());
  __ LoadLabelAddress(mips64::V1, literal2->GetLabel());
  // A nop will be inserted here before the 64-bit literals.

  std::string expected =
      "ldpc $a1, 1f\n"
      // The GNU assembler incorrectly requires the ldpc instruction to be located
      // at an address that's a multiple of 8. TODO: Remove this workaround if/when
      // the assembler is fixed.
      // "ldpc $a2, 2f\n"
      ".word 0xECD80004\n"
      "ldpc $a3, 3f\n"
      "lapc $v0, 1f\n"
      "lapc $v1, 2f\n"
      "nop\n"
      "1:\n"
      ".dword 0x0123456789ABCDEF\n"
      "2:\n"
      ".dword 0x5555555555555555\n"
      "3:\n"
      ".dword 0xAAAAAAAAAAAAAAAA\n";
  DriverStr(expected, "LongLiteralAlignmentNop");
  EXPECT_EQ(__ GetLabelLocation(literal1->GetLabel()), 6 * 4u);
  EXPECT_EQ(__ GetLabelLocation(literal2->GetLabel()), 8 * 4u);
  EXPECT_EQ(__ GetLabelLocation(literal3->GetLabel()), 10 * 4u);
}

TEST_F(AssemblerMIPS64Test, LongLiteralAlignmentNoNop) {
  mips64::Literal* literal1 = __ NewLiteral<uint64_t>(UINT64_C(0x0123456789ABCDEF));
  mips64::Literal* literal2 = __ NewLiteral<uint64_t>(UINT64_C(0x5555555555555555));
  __ LoadLiteral(mips64::A1, mips64::kLoadDoubleword, literal1);
  __ LoadLiteral(mips64::A2, mips64::kLoadDoubleword, literal2);
  __ LoadLabelAddress(mips64::V0, literal1->GetLabel());
  __ LoadLabelAddress(mips64::V1, literal2->GetLabel());

  std::string expected =
      "ldpc $a1, 1f\n"
      // The GNU assembler incorrectly requires the ldpc instruction to be located
      // at an address that's a multiple of 8. TODO: Remove this workaround if/when
      // the assembler is fixed.
      // "ldpc $a2, 2f\n"
      ".word 0xECD80003\n"
      "lapc $v0, 1f\n"
      "lapc $v1, 2f\n"
      "1:\n"
      ".dword 0x0123456789ABCDEF\n"
      "2:\n"
      ".dword 0x5555555555555555\n";
  DriverStr(expected, "LongLiteralAlignmentNoNop");
  EXPECT_EQ(__ GetLabelLocation(literal1->GetLabel()), 4 * 4u);
  EXPECT_EQ(__ GetLabelLocation(literal2->GetLabel()), 6 * 4u);
}

TEST_F(AssemblerMIPS64Test, FarLongLiteralAlignmentNop) {
  mips64::Literal* literal = __ NewLiteral<uint64_t>(UINT64_C(0x0123456789ABCDEF));
  __ LoadLiteral(mips64::V0, mips64::kLoadDoubleword, literal);
  __ LoadLabelAddress(mips64::V1, literal->GetLabel());
  constexpr uint32_t kAdduCount = 0x3FFDF;
  for (uint32_t i = 0; i != kAdduCount; ++i) {
    __ Addu(mips64::ZERO, mips64::ZERO, mips64::ZERO);
  }
  // A nop will be inserted here before the 64-bit literal.

  std::string expected =
      "1:\n"
      "auipc $at, %hi(3f - 1b)\n"
      "ld $v0, %lo(3f - 1b)($at)\n"
      "2:\n"
      "auipc $at, %hi(3f - 2b)\n"
      "daddiu $v1, $at, %lo(3f - 2b)\n" +
      RepeatInsn(kAdduCount, "addu $zero, $zero, $zero\n") +
      "nop\n"
      "3:\n"
      ".dword 0x0123456789ABCDEF\n";
  DriverStr(expected, "FarLongLiteralAlignmentNop");
  EXPECT_EQ(__ GetLabelLocation(literal->GetLabel()), (5 + kAdduCount) * 4);
}

TEST_F(AssemblerMIPS64Test, Addu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Addu, "addu ${reg1}, ${reg2}, ${reg3}"), "addu");
}

TEST_F(AssemblerMIPS64Test, Addiu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Addiu, -16, "addiu ${reg1}, ${reg2}, {imm}"),
            "addiu");
}

TEST_F(AssemblerMIPS64Test, Daddu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Daddu, "daddu ${reg1}, ${reg2}, ${reg3}"), "daddu");
}

TEST_F(AssemblerMIPS64Test, Daddiu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Daddiu, -16, "daddiu ${reg1}, ${reg2}, {imm}"),
            "daddiu");
}

TEST_F(AssemblerMIPS64Test, Subu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Subu, "subu ${reg1}, ${reg2}, ${reg3}"), "subu");
}

TEST_F(AssemblerMIPS64Test, Dsubu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dsubu, "dsubu ${reg1}, ${reg2}, ${reg3}"), "dsubu");
}

TEST_F(AssemblerMIPS64Test, MulR6) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::MulR6, "mul ${reg1}, ${reg2}, ${reg3}"), "mulR6");
}

TEST_F(AssemblerMIPS64Test, DivR6) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::DivR6, "div ${reg1}, ${reg2}, ${reg3}"), "divR6");
}

TEST_F(AssemblerMIPS64Test, ModR6) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::ModR6, "mod ${reg1}, ${reg2}, ${reg3}"), "modR6");
}

TEST_F(AssemblerMIPS64Test, DivuR6) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::DivuR6, "divu ${reg1}, ${reg2}, ${reg3}"),
            "divuR6");
}

TEST_F(AssemblerMIPS64Test, ModuR6) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::ModuR6, "modu ${reg1}, ${reg2}, ${reg3}"),
            "moduR6");
}

TEST_F(AssemblerMIPS64Test, Dmul) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dmul, "dmul ${reg1}, ${reg2}, ${reg3}"), "dmul");
}

TEST_F(AssemblerMIPS64Test, Ddiv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Ddiv, "ddiv ${reg1}, ${reg2}, ${reg3}"), "ddiv");
}

TEST_F(AssemblerMIPS64Test, Dmod) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dmod, "dmod ${reg1}, ${reg2}, ${reg3}"), "dmod");
}

TEST_F(AssemblerMIPS64Test, Ddivu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Ddivu, "ddivu ${reg1}, ${reg2}, ${reg3}"), "ddivu");
}

TEST_F(AssemblerMIPS64Test, Dmodu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dmodu, "dmodu ${reg1}, ${reg2}, ${reg3}"), "dmodu");
}

TEST_F(AssemblerMIPS64Test, And) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::And, "and ${reg1}, ${reg2}, ${reg3}"), "and");
}

TEST_F(AssemblerMIPS64Test, Andi) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Andi, 16, "andi ${reg1}, ${reg2}, {imm}"), "andi");
}

TEST_F(AssemblerMIPS64Test, Or) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Or, "or ${reg1}, ${reg2}, ${reg3}"), "or");
}

TEST_F(AssemblerMIPS64Test, Ori) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Ori, 16, "ori ${reg1}, ${reg2}, {imm}"), "ori");
}

TEST_F(AssemblerMIPS64Test, Xor) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Xor, "xor ${reg1}, ${reg2}, ${reg3}"), "xor");
}

TEST_F(AssemblerMIPS64Test, Xori) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Xori, 16, "xori ${reg1}, ${reg2}, {imm}"), "xori");
}

TEST_F(AssemblerMIPS64Test, Nor) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Nor, "nor ${reg1}, ${reg2}, ${reg3}"), "nor");
}

TEST_F(AssemblerMIPS64Test, Lb) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lb, -16, "lb ${reg1}, {imm}(${reg2})"), "lb");
}

TEST_F(AssemblerMIPS64Test, Lh) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lh, -16, "lh ${reg1}, {imm}(${reg2})"), "lh");
}

TEST_F(AssemblerMIPS64Test, Lw) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lw, -16, "lw ${reg1}, {imm}(${reg2})"), "lw");
}

TEST_F(AssemblerMIPS64Test, Ld) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Ld, -16, "ld ${reg1}, {imm}(${reg2})"), "ld");
}

TEST_F(AssemblerMIPS64Test, Lbu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lbu, -16, "lbu ${reg1}, {imm}(${reg2})"), "lbu");
}

TEST_F(AssemblerMIPS64Test, Lhu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lhu, -16, "lhu ${reg1}, {imm}(${reg2})"), "lhu");
}

TEST_F(AssemblerMIPS64Test, Lwu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lwu, -16, "lwu ${reg1}, {imm}(${reg2})"), "lwu");
}

TEST_F(AssemblerMIPS64Test, Lui) {
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Lui, 16, "lui ${reg}, {imm}"), "lui");
}

TEST_F(AssemblerMIPS64Test, Dahi) {
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Dahi, 16, "dahi ${reg}, ${reg}, {imm}"), "dahi");
}

TEST_F(AssemblerMIPS64Test, Dati) {
  DriverStr(RepeatRIb(&mips64::Mips64Assembler::Dati, 16, "dati ${reg}, ${reg}, {imm}"), "dati");
}

TEST_F(AssemblerMIPS64Test, Sb) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sb, -16, "sb ${reg1}, {imm}(${reg2})"), "sb");
}

TEST_F(AssemblerMIPS64Test, Sh) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sh, -16, "sh ${reg1}, {imm}(${reg2})"), "sh");
}

TEST_F(AssemblerMIPS64Test, Sw) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sw, -16, "sw ${reg1}, {imm}(${reg2})"), "sw");
}

TEST_F(AssemblerMIPS64Test, Sd) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sd, -16, "sd ${reg1}, {imm}(${reg2})"), "sd");
}

TEST_F(AssemblerMIPS64Test, Slt) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Slt, "slt ${reg1}, ${reg2}, ${reg3}"), "slt");
}

TEST_F(AssemblerMIPS64Test, Sltu) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Sltu, "sltu ${reg1}, ${reg2}, ${reg3}"), "sltu");
}

TEST_F(AssemblerMIPS64Test, Slti) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Slti, -16, "slti ${reg1}, ${reg2}, {imm}"),
            "slti");
}

TEST_F(AssemblerMIPS64Test, Sltiu) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sltiu, -16, "sltiu ${reg1}, ${reg2}, {imm}"),
            "sltiu");
}

TEST_F(AssemblerMIPS64Test, Move) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Move, "or ${reg1}, ${reg2}, $zero"), "move");
}

TEST_F(AssemblerMIPS64Test, Clear) {
  DriverStr(RepeatR(&mips64::Mips64Assembler::Clear, "or ${reg}, $zero, $zero"), "clear");
}

TEST_F(AssemblerMIPS64Test, Not) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Not, "nor ${reg1}, ${reg2}, $zero"), "not");
}

TEST_F(AssemblerMIPS64Test, Bitswap) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Bitswap, "bitswap ${reg1}, ${reg2}"), "bitswap");
}

TEST_F(AssemblerMIPS64Test, Dbitswap) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Dbitswap, "dbitswap ${reg1}, ${reg2}"), "dbitswap");
}

TEST_F(AssemblerMIPS64Test, Seb) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Seb, "seb ${reg1}, ${reg2}"), "seb");
}

TEST_F(AssemblerMIPS64Test, Seh) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Seh, "seh ${reg1}, ${reg2}"), "seh");
}

TEST_F(AssemblerMIPS64Test, Dsbh) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Dsbh, "dsbh ${reg1}, ${reg2}"), "dsbh");
}

TEST_F(AssemblerMIPS64Test, Dshd) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Dshd, "dshd ${reg1}, ${reg2}"), "dshd");
}

TEST_F(AssemblerMIPS64Test, Dext) {
  std::vector<mips64::GpuRegister*> reg1_registers = GetRegisters();
  std::vector<mips64::GpuRegister*> reg2_registers = GetRegisters();
  WarnOnCombinations(reg1_registers.size() * reg2_registers.size() * 33 * 16);
  std::ostringstream expected;
  for (mips64::GpuRegister* reg1 : reg1_registers) {
    for (mips64::GpuRegister* reg2 : reg2_registers) {
      for (int32_t pos = 0; pos < 32; pos++) {
        for (int32_t size = 1; size <= 32; size++) {
          __ Dext(*reg1, *reg2, pos, size);
          expected << "dext $" << *reg1 << ", $" << *reg2 << ", " << pos << ", " << size << "\n";
        }
      }
    }
  }

  DriverStr(expected.str(), "Dext");
}

TEST_F(AssemblerMIPS64Test, Dinsu) {
  std::vector<mips64::GpuRegister*> reg1_registers = GetRegisters();
  std::vector<mips64::GpuRegister*> reg2_registers = GetRegisters();
  WarnOnCombinations(reg1_registers.size() * reg2_registers.size() * 33 * 16);
  std::ostringstream expected;
  for (mips64::GpuRegister* reg1 : reg1_registers) {
    for (mips64::GpuRegister* reg2 : reg2_registers) {
      for (int32_t pos = 32; pos < 64; pos++) {
        for (int32_t size = 1; pos + size <= 64; size++) {
          __ Dinsu(*reg1, *reg2, pos, size);
          expected << "dinsu $" << *reg1 << ", $" << *reg2 << ", " << pos << ", " << size << "\n";
        }
      }
    }
  }

  DriverStr(expected.str(), "Dinsu");
}

TEST_F(AssemblerMIPS64Test, Lsa) {
  DriverStr(RepeatRRRIb(&mips64::Mips64Assembler::Lsa,
                        2,
                        "lsa ${reg1}, ${reg2}, ${reg3}, {imm}",
                        1),
            "lsa");
}

TEST_F(AssemblerMIPS64Test, Dlsa) {
  DriverStr(RepeatRRRIb(&mips64::Mips64Assembler::Dlsa,
                        2,
                        "dlsa ${reg1}, ${reg2}, ${reg3}, {imm}",
                        1),
            "dlsa");
}

TEST_F(AssemblerMIPS64Test, Wsbh) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Wsbh, "wsbh ${reg1}, ${reg2}"), "wsbh");
}

TEST_F(AssemblerMIPS64Test, Sll) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sll, 5, "sll ${reg1}, ${reg2}, {imm}"), "sll");
}

TEST_F(AssemblerMIPS64Test, Srl) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Srl, 5, "srl ${reg1}, ${reg2}, {imm}"), "srl");
}

TEST_F(AssemblerMIPS64Test, Rotr) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Rotr, 5, "rotr ${reg1}, ${reg2}, {imm}"), "rotr");
}

TEST_F(AssemblerMIPS64Test, Sra) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sra, 5, "sra ${reg1}, ${reg2}, {imm}"), "sra");
}

TEST_F(AssemblerMIPS64Test, Sllv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Sllv, "sllv ${reg1}, ${reg2}, ${reg3}"), "sllv");
}

TEST_F(AssemblerMIPS64Test, Srlv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Srlv, "srlv ${reg1}, ${reg2}, ${reg3}"), "srlv");
}

TEST_F(AssemblerMIPS64Test, Rotrv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Rotrv, "rotrv ${reg1}, ${reg2}, ${reg3}"), "rotrv");
}

TEST_F(AssemblerMIPS64Test, Srav) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Srav, "srav ${reg1}, ${reg2}, ${reg3}"), "srav");
}

TEST_F(AssemblerMIPS64Test, Dsll) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsll, 5, "dsll ${reg1}, ${reg2}, {imm}"), "dsll");
}

TEST_F(AssemblerMIPS64Test, Dsrl) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsrl, 5, "dsrl ${reg1}, ${reg2}, {imm}"), "dsrl");
}

TEST_F(AssemblerMIPS64Test, Drotr) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Drotr, 5, "drotr ${reg1}, ${reg2}, {imm}"),
            "drotr");
}

TEST_F(AssemblerMIPS64Test, Dsra) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsra, 5, "dsra ${reg1}, ${reg2}, {imm}"), "dsra");
}

TEST_F(AssemblerMIPS64Test, Dsll32) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsll32, 5, "dsll32 ${reg1}, ${reg2}, {imm}"),
            "dsll32");
}

TEST_F(AssemblerMIPS64Test, Dsrl32) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsrl32, 5, "dsrl32 ${reg1}, ${reg2}, {imm}"),
            "dsrl32");
}

TEST_F(AssemblerMIPS64Test, Drotr32) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Drotr32, 5, "drotr32 ${reg1}, ${reg2}, {imm}"),
            "drotr32");
}

TEST_F(AssemblerMIPS64Test, Dsra32) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Dsra32, 5, "dsra32 ${reg1}, ${reg2}, {imm}"),
            "dsra32");
}

TEST_F(AssemblerMIPS64Test, Dsllv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dsllv, "dsllv ${reg1}, ${reg2}, ${reg3}"), "dsllv");
}

TEST_F(AssemblerMIPS64Test, Dsrlv) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dsrlv, "dsrlv ${reg1}, ${reg2}, ${reg3}"), "dsrlv");
}

TEST_F(AssemblerMIPS64Test, Dsrav) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Dsrav, "dsrav ${reg1}, ${reg2}, ${reg3}"), "dsrav");
}

TEST_F(AssemblerMIPS64Test, Sc) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Sc, -9, "sc ${reg1}, {imm}(${reg2})"), "sc");
}

TEST_F(AssemblerMIPS64Test, Scd) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Scd, -9, "scd ${reg1}, {imm}(${reg2})"), "scd");
}

TEST_F(AssemblerMIPS64Test, Ll) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Ll, -9, "ll ${reg1}, {imm}(${reg2})"), "ll");
}

TEST_F(AssemblerMIPS64Test, Lld) {
  DriverStr(RepeatRRIb(&mips64::Mips64Assembler::Lld, -9, "lld ${reg1}, {imm}(${reg2})"), "lld");
}

TEST_F(AssemblerMIPS64Test, Seleqz) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Seleqz, "seleqz ${reg1}, ${reg2}, ${reg3}"),
            "seleqz");
}

TEST_F(AssemblerMIPS64Test, Selnez) {
  DriverStr(RepeatRRR(&mips64::Mips64Assembler::Selnez, "selnez ${reg1}, ${reg2}, ${reg3}"),
            "selnez");
}

TEST_F(AssemblerMIPS64Test, Clz) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Clz, "clz ${reg1}, ${reg2}"), "clz");
}

TEST_F(AssemblerMIPS64Test, Clo) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Clo, "clo ${reg1}, ${reg2}"), "clo");
}

TEST_F(AssemblerMIPS64Test, Dclz) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Dclz, "dclz ${reg1}, ${reg2}"), "dclz");
}

TEST_F(AssemblerMIPS64Test, Dclo) {
  DriverStr(RepeatRR(&mips64::Mips64Assembler::Dclo, "dclo ${reg1}, ${reg2}"), "dclo");
}

TEST_F(AssemblerMIPS64Test, LoadFromOffset) {
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 1);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0x7FFF);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0x8001);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadSignedByte, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 1);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0x7FFF);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0x8001);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadUnsignedByte, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 2);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0x7FFE);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0x8002);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadSignedHalfword, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 2);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0x7FFE);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0x8002);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadUnsignedHalfword, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 4);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0x7FFC);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0x8004);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadWord, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 4);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0x7FFC);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0x8004);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadUnsignedWord, mips64::A0, mips64::A1, 0xABCDEF00);

  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A0, 0);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 4);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 256);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 1000);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0x7FFC);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0x8000);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0x8004);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0x10000);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0x12345678);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, -256);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, -32768);
  __ LoadFromOffset(mips64::kLoadDoubleword, mips64::A0, mips64::A1, 0xABCDEF00);

  const char* expected =
      "lb $a0, 0($a0)\n"
      "lb $a0, 0($a1)\n"
      "lb $a0, 1($a1)\n"
      "lb $a0, 256($a1)\n"
      "lb $a0, 1000($a1)\n"
      "lb $a0, 0x7FFF($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lb $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lb $a0, 1($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lb $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lb $a0, 0($at)\n"
      "lb $a0, -256($a1)\n"
      "lb $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lb $a0, 0($at)\n"

      "lbu $a0, 0($a0)\n"
      "lbu $a0, 0($a1)\n"
      "lbu $a0, 1($a1)\n"
      "lbu $a0, 256($a1)\n"
      "lbu $a0, 1000($a1)\n"
      "lbu $a0, 0x7FFF($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lbu $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lbu $a0, 1($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lbu $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lbu $a0, 0($at)\n"
      "lbu $a0, -256($a1)\n"
      "lbu $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lbu $a0, 0($at)\n"

      "lh $a0, 0($a0)\n"
      "lh $a0, 0($a1)\n"
      "lh $a0, 2($a1)\n"
      "lh $a0, 256($a1)\n"
      "lh $a0, 1000($a1)\n"
      "lh $a0, 0x7FFE($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lh $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lh $a0, 2($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lh $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lh $a0, 0($at)\n"
      "lh $a0, -256($a1)\n"
      "lh $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lh $a0, 0($at)\n"

      "lhu $a0, 0($a0)\n"
      "lhu $a0, 0($a1)\n"
      "lhu $a0, 2($a1)\n"
      "lhu $a0, 256($a1)\n"
      "lhu $a0, 1000($a1)\n"
      "lhu $a0, 0x7FFE($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lhu $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lhu $a0, 2($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lhu $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lhu $a0, 0($at)\n"
      "lhu $a0, -256($a1)\n"
      "lhu $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lhu $a0, 0($at)\n"

      "lw $a0, 0($a0)\n"
      "lw $a0, 0($a1)\n"
      "lw $a0, 4($a1)\n"
      "lw $a0, 256($a1)\n"
      "lw $a0, 1000($a1)\n"
      "lw $a0, 0x7FFC($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lw $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lw $a0, 4($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lw $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lw $a0, 0($at)\n"
      "lw $a0, -256($a1)\n"
      "lw $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lw $a0, 0($at)\n"

      "lwu $a0, 0($a0)\n"
      "lwu $a0, 0($a1)\n"
      "lwu $a0, 4($a1)\n"
      "lwu $a0, 256($a1)\n"
      "lwu $a0, 1000($a1)\n"
      "lwu $a0, 0x7FFC($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 4($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 0($at)\n"
      "lwu $a0, -256($a1)\n"
      "lwu $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 0($at)\n"

      "ld $a0, 0($a0)\n"
      "ld $a0, 0($a1)\n"
      "lwu $a0, 4($a1)\n"
      "lwu $t3, 8($a1)\n"
      "dins $a0, $t3, 32, 32\n"
      "ld $a0, 256($a1)\n"
      "ld $a0, 1000($a1)\n"
      "ori $at, $zero, 0x7FF8\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 4($at)\n"
      "lwu $t3, 8($at)\n"
      "dins $a0, $t3, 32, 32\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "ld $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "lwu $a0, 4($at)\n"
      "lwu $t3, 8($at)\n"
      "dins $a0, $t3, 32, 32\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "ld $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "ld $a0, 0($at)\n"
      "ld $a0, -256($a1)\n"
      "ld $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "ld $a0, 0($at)\n";
  DriverStr(expected, "LoadFromOffset");
}

TEST_F(AssemblerMIPS64Test, LoadFpuFromOffset) {
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 4);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 256);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0x7FFC);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0x8000);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0x8004);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0x10000);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0x12345678);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, -256);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, -32768);
  __ LoadFpuFromOffset(mips64::kLoadWord, mips64::F0, mips64::A0, 0xABCDEF00);

  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 4);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 256);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0x7FFC);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0x8000);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0x8004);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0x10000);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0x12345678);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, -256);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, -32768);
  __ LoadFpuFromOffset(mips64::kLoadDoubleword, mips64::F0, mips64::A0, 0xABCDEF00);

  const char* expected =
      "lwc1 $f0, 0($a0)\n"
      "lwc1 $f0, 4($a0)\n"
      "lwc1 $f0, 256($a0)\n"
      "lwc1 $f0, 0x7FFC($a0)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 4($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 0($at)\n"
      "lwc1 $f0, -256($a0)\n"
      "lwc1 $f0, -32768($a0)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 0($at)\n"

      "ldc1 $f0, 0($a0)\n"
      "lwc1 $f0, 4($a0)\n"
      "lw $t3, 8($a0)\n"
      "mthc1 $t3, $f0\n"
      "ldc1 $f0, 256($a0)\n"
      "ori $at, $zero, 0x7FF8\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 4($at)\n"
      "lw $t3, 8($at)\n"
      "mthc1 $t3, $f0\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "ldc1 $f0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "lwc1 $f0, 4($at)\n"
      "lw $t3, 8($at)\n"
      "mthc1 $t3, $f0\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a0\n"
      "ldc1 $f0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a0\n"
      "ldc1 $f0, 0($at)\n"
      "ldc1 $f0, -256($a0)\n"
      "ldc1 $f0, -32768($a0)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a0\n"
      "ldc1 $f0, 0($at)\n";
  DriverStr(expected, "LoadFpuFromOffset");
}

TEST_F(AssemblerMIPS64Test, StoreToOffset) {
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A0, 0);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 1);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 256);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 1000);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0x7FFF);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0x8000);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0x8001);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0x10000);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0x12345678);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, -256);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, -32768);
  __ StoreToOffset(mips64::kStoreByte, mips64::A0, mips64::A1, 0xABCDEF00);

  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A0, 0);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 2);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 256);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 1000);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0x7FFE);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0x8000);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0x8002);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0x10000);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0x12345678);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, -256);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, -32768);
  __ StoreToOffset(mips64::kStoreHalfword, mips64::A0, mips64::A1, 0xABCDEF00);

  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A0, 0);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 4);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 256);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 1000);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0x7FFC);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0x8000);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0x8004);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0x10000);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0x12345678);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, -256);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, -32768);
  __ StoreToOffset(mips64::kStoreWord, mips64::A0, mips64::A1, 0xABCDEF00);

  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A0, 0);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 4);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 256);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 1000);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0x7FFC);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0x8000);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0x8004);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0x10000);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0x12345678);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, -256);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, -32768);
  __ StoreToOffset(mips64::kStoreDoubleword, mips64::A0, mips64::A1, 0xABCDEF00);

  const char* expected =
      "sb $a0, 0($a0)\n"
      "sb $a0, 0($a1)\n"
      "sb $a0, 1($a1)\n"
      "sb $a0, 256($a1)\n"
      "sb $a0, 1000($a1)\n"
      "sb $a0, 0x7FFF($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sb $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sb $a0, 1($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "sb $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "sb $a0, 0($at)\n"
      "sb $a0, -256($a1)\n"
      "sb $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "sb $a0, 0($at)\n"

      "sh $a0, 0($a0)\n"
      "sh $a0, 0($a1)\n"
      "sh $a0, 2($a1)\n"
      "sh $a0, 256($a1)\n"
      "sh $a0, 1000($a1)\n"
      "sh $a0, 0x7FFE($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sh $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sh $a0, 2($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "sh $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "sh $a0, 0($at)\n"
      "sh $a0, -256($a1)\n"
      "sh $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "sh $a0, 0($at)\n"

      "sw $a0, 0($a0)\n"
      "sw $a0, 0($a1)\n"
      "sw $a0, 4($a1)\n"
      "sw $a0, 256($a1)\n"
      "sw $a0, 1000($a1)\n"
      "sw $a0, 0x7FFC($a1)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 4($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 0($at)\n"
      "sw $a0, -256($a1)\n"
      "sw $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 0($at)\n"

      "sd $a0, 0($a0)\n"
      "sd $a0, 0($a1)\n"
      "sw $a0, 4($a1)\n"
      "dsrl32 $t3, $a0, 0\n"
      "sw $t3, 8($a1)\n"
      "sd $a0, 256($a1)\n"
      "sd $a0, 1000($a1)\n"
      "ori $at, $zero, 0x7FF8\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 4($at)\n"
      "dsrl32 $t3, $a0, 0\n"
      "sw $t3, 8($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sd $a0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a1\n"
      "sw $a0, 4($at)\n"
      "dsrl32 $t3, $a0, 0\n"
      "sw $t3, 8($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a1\n"
      "sd $a0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a1\n"
      "sd $a0, 0($at)\n"
      "sd $a0, -256($a1)\n"
      "sd $a0, -32768($a1)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a1\n"
      "sd $a0, 0($at)\n";
  DriverStr(expected, "StoreToOffset");
}

TEST_F(AssemblerMIPS64Test, StoreFpuToOffset) {
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 4);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 256);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0x7FFC);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0x8000);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0x8004);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0x10000);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0x12345678);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, -256);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, -32768);
  __ StoreFpuToOffset(mips64::kStoreWord, mips64::F0, mips64::A0, 0xABCDEF00);

  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 4);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 256);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0x7FFC);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0x8000);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0x8004);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0x10000);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0x12345678);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, -256);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, -32768);
  __ StoreFpuToOffset(mips64::kStoreDoubleword, mips64::F0, mips64::A0, 0xABCDEF00);

  const char* expected =
      "swc1 $f0, 0($a0)\n"
      "swc1 $f0, 4($a0)\n"
      "swc1 $f0, 256($a0)\n"
      "swc1 $f0, 0x7FFC($a0)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "swc1 $f0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "swc1 $f0, 4($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a0\n"
      "swc1 $f0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a0\n"
      "swc1 $f0, 0($at)\n"
      "swc1 $f0, -256($a0)\n"
      "swc1 $f0, -32768($a0)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a0\n"
      "swc1 $f0, 0($at)\n"

      "sdc1 $f0, 0($a0)\n"
      "mfhc1 $t3, $f0\n"
      "swc1 $f0, 4($a0)\n"
      "sw $t3, 8($a0)\n"
      "sdc1 $f0, 256($a0)\n"
      "ori $at, $zero, 0x7FF8\n"
      "daddu $at, $at, $a0\n"
      "mfhc1 $t3, $f0\n"
      "swc1 $f0, 4($at)\n"
      "sw $t3, 8($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "sdc1 $f0, 0($at)\n"
      "ori $at, $zero, 0x8000\n"
      "daddu $at, $at, $a0\n"
      "mfhc1 $t3, $f0\n"
      "swc1 $f0, 4($at)\n"
      "sw $t3, 8($at)\n"
      "lui $at, 1\n"
      "daddu $at, $at, $a0\n"
      "sdc1 $f0, 0($at)\n"
      "lui $at, 0x1234\n"
      "ori $at, 0x5678\n"
      "daddu $at, $at, $a0\n"
      "sdc1 $f0, 0($at)\n"
      "sdc1 $f0, -256($a0)\n"
      "sdc1 $f0, -32768($a0)\n"
      "lui $at, 0xABCD\n"
      "ori $at, 0xEF00\n"
      "daddu $at, $at, $a0\n"
      "sdc1 $f0, 0($at)\n";
  DriverStr(expected, "StoreFpuToOffset");
}

//////////////////////////////
// Loading/adding Constants //
//////////////////////////////

TEST_F(AssemblerMIPS64Test, LoadConst32) {
  // IsUint<16>(value)
  __ LoadConst32(mips64::V0, 0);
  __ LoadConst32(mips64::V0, 65535);
  // IsInt<16>(value)
  __ LoadConst32(mips64::V0, -1);
  __ LoadConst32(mips64::V0, -32768);
  // Everything else
  __ LoadConst32(mips64::V0, 65536);
  __ LoadConst32(mips64::V0, 65537);
  __ LoadConst32(mips64::V0, 2147483647);
  __ LoadConst32(mips64::V0, -32769);
  __ LoadConst32(mips64::V0, -65536);
  __ LoadConst32(mips64::V0, -65537);
  __ LoadConst32(mips64::V0, -2147483647);
  __ LoadConst32(mips64::V0, -2147483648);

  const char* expected =
      // IsUint<16>(value)
      "ori $v0, $zero, 0\n"         // __ LoadConst32(mips64::V0, 0);
      "ori $v0, $zero, 65535\n"     // __ LoadConst32(mips64::V0, 65535);
      // IsInt<16>(value)
      "addiu $v0, $zero, -1\n"      // __ LoadConst32(mips64::V0, -1);
      "addiu $v0, $zero, -32768\n"  // __ LoadConst32(mips64::V0, -32768);
      // Everything else
      "lui $v0, 1\n"                // __ LoadConst32(mips64::V0, 65536);
      "lui $v0, 1\n"                // __ LoadConst32(mips64::V0, 65537);
      "ori $v0, 1\n"                //                 "
      "lui $v0, 32767\n"            // __ LoadConst32(mips64::V0, 2147483647);
      "ori $v0, 65535\n"            //                 "
      "lui $v0, 65535\n"            // __ LoadConst32(mips64::V0, -32769);
      "ori $v0, 32767\n"            //                 "
      "lui $v0, 65535\n"            // __ LoadConst32(mips64::V0, -65536);
      "lui $v0, 65534\n"            // __ LoadConst32(mips64::V0, -65537);
      "ori $v0, 65535\n"            //                 "
      "lui $v0, 32768\n"            // __ LoadConst32(mips64::V0, -2147483647);
      "ori $v0, 1\n"                //                 "
      "lui $v0, 32768\n";           // __ LoadConst32(mips64::V0, -2147483648);
  DriverStr(expected, "LoadConst32");
}

TEST_F(AssemblerMIPS64Test, Addiu32) {
  __ Addiu32(mips64::A1, mips64::A2, -0x8000);
  __ Addiu32(mips64::A1, mips64::A2, +0);
  __ Addiu32(mips64::A1, mips64::A2, +0x7FFF);
  __ Addiu32(mips64::A1, mips64::A2, -0x8001);
  __ Addiu32(mips64::A1, mips64::A2, +0x8000);
  __ Addiu32(mips64::A1, mips64::A2, -0x10000);
  __ Addiu32(mips64::A1, mips64::A2, +0x10000);
  __ Addiu32(mips64::A1, mips64::A2, +0x12345678);

  const char* expected =
      "addiu $a1, $a2, -0x8000\n"
      "addiu $a1, $a2, 0\n"
      "addiu $a1, $a2, 0x7FFF\n"
      "aui $a1, $a2, 0xFFFF\n"
      "addiu $a1, $a1, 0x7FFF\n"
      "aui $a1, $a2, 1\n"
      "addiu $a1, $a1, -0x8000\n"
      "aui $a1, $a2, 0xFFFF\n"
      "aui $a1, $a2, 1\n"
      "aui $a1, $a2, 0x1234\n"
      "addiu $a1, $a1, 0x5678\n";
  DriverStr(expected, "Addiu32");
}

static uint64_t SignExtend16To64(uint16_t n) {
  return static_cast<int16_t>(n);
}

// The art::mips64::Mips64Assembler::LoadConst64() method uses a template
// to minimize the number of instructions needed to load a 64-bit constant
// value into a register. The template calls various methods which emit
// MIPS machine instructions. This struct (class) uses the same template
// but overrides the definitions of the methods which emit MIPS instructions
// to use methods which simulate the operation of the corresponding MIPS
// instructions. After invoking LoadConst64() the target register should
// contain the same 64-bit value as was input to LoadConst64(). If the
// simulated register doesn't contain the correct value then there is probably
// an error in the template function.
struct LoadConst64Tester {
  LoadConst64Tester() {
    // Initialize all of the registers for simulation to zero.
    for (int r = 0; r < 32; r++) {
      regs_[r] = 0;
    }
    // Clear all of the path flags.
    loadconst64_paths_ = art::mips64::kLoadConst64PathZero;
  }
  void Addiu(mips64::GpuRegister rd, mips64::GpuRegister rs, uint16_t c) {
    regs_[rd] = static_cast<int32_t>(regs_[rs] + SignExtend16To64(c));
  }
  void Daddiu(mips64::GpuRegister rd, mips64::GpuRegister rs, uint16_t c) {
    regs_[rd] = regs_[rs] + SignExtend16To64(c);
  }
  void Dahi(mips64::GpuRegister rd, uint16_t c) {
    regs_[rd] += SignExtend16To64(c) << 32;
  }
  void Dati(mips64::GpuRegister rd, uint16_t c) {
    regs_[rd] += SignExtend16To64(c) << 48;
  }
  void Dinsu(mips64::GpuRegister rt, mips64::GpuRegister rs, int pos, int size) {
    CHECK(IsUint<5>(pos - 32)) << pos;
    CHECK(IsUint<5>(size - 1)) << size;
    CHECK(IsUint<5>(pos + size - 33)) << pos << " + " << size;
    uint64_t src_mask = (UINT64_C(1) << size) - 1;
    uint64_t dsk_mask = ~(src_mask << pos);

    regs_[rt] = (regs_[rt] & dsk_mask) | ((regs_[rs] & src_mask) << pos);
  }
  void Dsll(mips64::GpuRegister rd, mips64::GpuRegister rt, int shamt) {
    regs_[rd] = regs_[rt] << (shamt & 0x1f);
  }
  void Dsll32(mips64::GpuRegister rd, mips64::GpuRegister rt, int shamt) {
    regs_[rd] = regs_[rt] << (32 + (shamt & 0x1f));
  }
  void Dsrl(mips64::GpuRegister rd, mips64::GpuRegister rt, int shamt) {
    regs_[rd] = regs_[rt] >> (shamt & 0x1f);
  }
  void Dsrl32(mips64::GpuRegister rd, mips64::GpuRegister rt, int shamt) {
    regs_[rd] = regs_[rt] >> (32 + (shamt & 0x1f));
  }
  void Lui(mips64::GpuRegister rd, uint16_t c) {
    regs_[rd] = SignExtend16To64(c) << 16;
  }
  void Ori(mips64::GpuRegister rd, mips64::GpuRegister rs, uint16_t c) {
    regs_[rd] = regs_[rs] | c;
  }
  void LoadConst32(mips64::GpuRegister rd, int32_t c) {
    CHECK_NE(rd, 0);
    mips64::TemplateLoadConst32<LoadConst64Tester>(this, rd, c);
    CHECK_EQ(regs_[rd], static_cast<uint64_t>(c));
  }
  void LoadConst64(mips64::GpuRegister rd, int64_t c) {
    CHECK_NE(rd, 0);
    mips64::TemplateLoadConst64<LoadConst64Tester>(this, rd, c);
    CHECK_EQ(regs_[rd], static_cast<uint64_t>(c));
  }
  uint64_t regs_[32];

  // Getter function for loadconst64_paths_.
  int GetPathsCovered() {
    return loadconst64_paths_;
  }

  void RecordLoadConst64Path(int value) {
    loadconst64_paths_ |= value;
  }

 private:
  // This variable holds a bitmask to tell us which paths were taken
  // through the template function which loads 64-bit values.
  int loadconst64_paths_;
};

TEST_F(AssemblerMIPS64Test, LoadConst64) {
  const uint16_t imms[] = {
      0, 1, 2, 3, 4, 0x33, 0x66, 0x55, 0x99, 0xaa, 0xcc, 0xff, 0x5500, 0x5555,
      0x7ffc, 0x7ffd, 0x7ffe, 0x7fff, 0x8000, 0x8001, 0x8002, 0x8003, 0x8004,
      0xaaaa, 0xfffc, 0xfffd, 0xfffe, 0xffff
  };
  unsigned d0, d1, d2, d3;
  LoadConst64Tester tester;

  union {
    int64_t v64;
    uint16_t v16[4];
  } u;

  for (d3 = 0; d3 < sizeof imms / sizeof imms[0]; d3++) {
    u.v16[3] = imms[d3];

    for (d2 = 0; d2 < sizeof imms / sizeof imms[0]; d2++) {
      u.v16[2] = imms[d2];

      for (d1 = 0; d1 < sizeof imms / sizeof imms[0]; d1++) {
        u.v16[1] = imms[d1];

        for (d0 = 0; d0 < sizeof imms / sizeof imms[0]; d0++) {
          u.v16[0] = imms[d0];

          tester.LoadConst64(mips64::V0, u.v64);
        }
      }
    }
  }

  // Verify that we tested all paths through the "load 64-bit value"
  // function template.
  EXPECT_EQ(tester.GetPathsCovered(), art::mips64::kLoadConst64PathAllPaths);
}

#undef __

}  // namespace art
