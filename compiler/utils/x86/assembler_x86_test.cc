/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "assembler_x86.h"

#include "base/arena_allocator.h"
#include "base/stl_util.h"
#include "utils/assembler_test.h"

namespace art {

TEST(AssemblerX86, CreateBuffer) {
  ArenaPool pool;
  ArenaAllocator arena(&pool);
  AssemblerBuffer buffer(&arena);
  AssemblerBuffer::EnsureCapacity ensured(&buffer);
  buffer.Emit<uint8_t>(0x42);
  ASSERT_EQ(static_cast<size_t>(1), buffer.Size());
  buffer.Emit<int32_t>(42);
  ASSERT_EQ(static_cast<size_t>(5), buffer.Size());
}

class AssemblerX86Test : public AssemblerTest<x86::X86Assembler, x86::Register,
                                              x86::XmmRegister, x86::Immediate> {
 public:
  typedef AssemblerTest<x86::X86Assembler, x86::Register,
                         x86::XmmRegister, x86::Immediate> Base;

 protected:
  std::string GetArchitectureString() OVERRIDE {
    return "x86";
  }

  std::string GetAssemblerParameters() OVERRIDE {
    return " --32";
  }

  std::string GetDisassembleParameters() OVERRIDE {
    return " -D -bbinary -mi386 --no-show-raw-insn";
  }

  void SetUpHelpers() OVERRIDE {
    if (registers_.size() == 0) {
      registers_.insert(end(registers_),
                        {  // NOLINT(whitespace/braces)
                          new x86::Register(x86::EAX),
                          new x86::Register(x86::EBX),
                          new x86::Register(x86::ECX),
                          new x86::Register(x86::EDX),
                          new x86::Register(x86::EBP),
                          new x86::Register(x86::ESP),
                          new x86::Register(x86::ESI),
                          new x86::Register(x86::EDI)
                        });
    }

    if (fp_registers_.size() == 0) {
      fp_registers_.insert(end(fp_registers_),
                           {  // NOLINT(whitespace/braces)
                             new x86::XmmRegister(x86::XMM0),
                             new x86::XmmRegister(x86::XMM1),
                             new x86::XmmRegister(x86::XMM2),
                             new x86::XmmRegister(x86::XMM3),
                             new x86::XmmRegister(x86::XMM4),
                             new x86::XmmRegister(x86::XMM5),
                             new x86::XmmRegister(x86::XMM6),
                             new x86::XmmRegister(x86::XMM7)
                           });
    }
  }

  void TearDown() OVERRIDE {
    AssemblerTest::TearDown();
    STLDeleteElements(&registers_);
    STLDeleteElements(&fp_registers_);
  }

  std::vector<x86::Register*> GetRegisters() OVERRIDE {
    return registers_;
  }

  std::vector<x86::XmmRegister*> GetFPRegisters() OVERRIDE {
    return fp_registers_;
  }

  x86::Immediate CreateImmediate(int64_t imm_value) OVERRIDE {
    return x86::Immediate(imm_value);
  }

 private:
  std::vector<x86::Register*> registers_;
  std::vector<x86::XmmRegister*> fp_registers_;
};


TEST_F(AssemblerX86Test, Movl) {
  GetAssembler()->movl(x86::EAX, x86::EBX);
  const char* expected = "mov %ebx, %eax\n";
  DriverStr(expected, "movl");
}

TEST_F(AssemblerX86Test, Movntl) {
  GetAssembler()->movntl(x86::Address(x86::EDI, x86::EBX, x86::TIMES_4, 12), x86::EAX);
  GetAssembler()->movntl(x86::Address(x86::EDI, 0), x86::EAX);
  const char* expected =
    "movntil %EAX, 0xc(%EDI,%EBX,4)\n"
    "movntil %EAX, (%EDI)\n";

  DriverStr(expected, "movntl");
}

TEST_F(AssemblerX86Test, LoadLongConstant) {
  GetAssembler()->LoadLongConstant(x86::XMM0, 51);
  const char* expected =
      "push $0x0\n"
      "push $0x33\n"
      "movsd 0(%esp), %xmm0\n"
      "add $8, %esp\n";
  DriverStr(expected, "LoadLongConstant");
}

TEST_F(AssemblerX86Test, LockCmpxchgl) {
  GetAssembler()->LockCmpxchgl(x86::Address(
        x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12),
      x86::Register(x86::ESI));
  GetAssembler()->LockCmpxchgl(x86::Address(
        x86::Register(x86::EDI), x86::Register(x86::ESI), x86::TIMES_4, 12),
      x86::Register(x86::ESI));
  GetAssembler()->LockCmpxchgl(x86::Address(
        x86::Register(x86::EDI), x86::Register(x86::ESI), x86::TIMES_4, 12),
      x86::Register(x86::EDI));
  GetAssembler()->LockCmpxchgl(x86::Address(
      x86::Register(x86::EBP), 0), x86::Register(x86::ESI));
  GetAssembler()->LockCmpxchgl(x86::Address(
        x86::Register(x86::EBP), x86::Register(x86::ESI), x86::TIMES_1, 0),
      x86::Register(x86::ESI));
  const char* expected =
    "lock cmpxchgl %ESI, 0xc(%EDI,%EBX,4)\n"
    "lock cmpxchgl %ESI, 0xc(%EDI,%ESI,4)\n"
    "lock cmpxchgl %EDI, 0xc(%EDI,%ESI,4)\n"
    "lock cmpxchgl %ESI, (%EBP)\n"
    "lock cmpxchgl %ESI, (%EBP,%ESI,1)\n";

  DriverStr(expected, "lock_cmpxchgl");
}

TEST_F(AssemblerX86Test, LockCmpxchg8b) {
  GetAssembler()->LockCmpxchg8b(x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  GetAssembler()->LockCmpxchg8b(x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::ESI), x86::TIMES_4, 12));
  GetAssembler()->LockCmpxchg8b(x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::ESI), x86::TIMES_4, 12));
  GetAssembler()->LockCmpxchg8b(x86::Address(x86::Register(x86::EBP), 0));
  GetAssembler()->LockCmpxchg8b(x86::Address(
      x86::Register(x86::EBP), x86::Register(x86::ESI), x86::TIMES_1, 0));
  const char* expected =
    "lock cmpxchg8b 0xc(%EDI,%EBX,4)\n"
    "lock cmpxchg8b 0xc(%EDI,%ESI,4)\n"
    "lock cmpxchg8b 0xc(%EDI,%ESI,4)\n"
    "lock cmpxchg8b (%EBP)\n"
    "lock cmpxchg8b (%EBP,%ESI,1)\n";

  DriverStr(expected, "lock_cmpxchg8b");
}

TEST_F(AssemblerX86Test, FPUIntegerLoad) {
  GetAssembler()->filds(x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->fildl(x86::Address(x86::Register(x86::ESP), 12));
  const char* expected =
      "fildl 0x4(%ESP)\n"
      "fildll 0xc(%ESP)\n";
  DriverStr(expected, "FPUIntegerLoad");
}

TEST_F(AssemblerX86Test, FPUIntegerStore) {
  GetAssembler()->fistps(x86::Address(x86::Register(x86::ESP), 16));
  GetAssembler()->fistpl(x86::Address(x86::Register(x86::ESP), 24));
  const char* expected =
      "fistpl 0x10(%ESP)\n"
      "fistpll 0x18(%ESP)\n";
  DriverStr(expected, "FPUIntegerStore");
}

TEST_F(AssemblerX86Test, Repnescasb) {
  GetAssembler()->repne_scasb();
  const char* expected = "repne scasb\n";
  DriverStr(expected, "Repnescasb");
}

TEST_F(AssemblerX86Test, Repnescasw) {
  GetAssembler()->repne_scasw();
  const char* expected = "repne scasw\n";
  DriverStr(expected, "Repnescasw");
}

TEST_F(AssemblerX86Test, Repecmpsb) {
  GetAssembler()->repe_cmpsb();
  const char* expected = "repe cmpsb\n";
  DriverStr(expected, "Repecmpsb");
}

TEST_F(AssemblerX86Test, Repecmpsw) {
  GetAssembler()->repe_cmpsw();
  const char* expected = "repe cmpsw\n";
  DriverStr(expected, "Repecmpsw");
}

TEST_F(AssemblerX86Test, Repecmpsl) {
  GetAssembler()->repe_cmpsl();
  const char* expected = "repe cmpsl\n";
  DriverStr(expected, "Repecmpsl");
}

TEST_F(AssemblerX86Test, RepMovsb) {
  GetAssembler()->rep_movsb();
  const char* expected = "rep movsb\n";
  DriverStr(expected, "rep_movsb");
}

TEST_F(AssemblerX86Test, RepMovsw) {
  GetAssembler()->rep_movsw();
  const char* expected = "rep movsw\n";
  DriverStr(expected, "rep_movsw");
}

TEST_F(AssemblerX86Test, Bsfl) {
  DriverStr(RepeatRR(&x86::X86Assembler::bsfl, "bsfl %{reg2}, %{reg1}"), "bsfl");
}

TEST_F(AssemblerX86Test, BsflAddress) {
  GetAssembler()->bsfl(x86::Register(x86::EDI), x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  const char* expected =
    "bsfl 0xc(%EDI,%EBX,4), %EDI\n";

  DriverStr(expected, "bsfl_address");
}

TEST_F(AssemblerX86Test, Bsrl) {
  DriverStr(RepeatRR(&x86::X86Assembler::bsrl, "bsrl %{reg2}, %{reg1}"), "bsrl");
}

TEST_F(AssemblerX86Test, BsrlAddress) {
  GetAssembler()->bsrl(x86::Register(x86::EDI), x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  const char* expected =
    "bsrl 0xc(%EDI,%EBX,4), %EDI\n";

  DriverStr(expected, "bsrl_address");
}

TEST_F(AssemblerX86Test, Popcntl) {
  DriverStr(RepeatRR(&x86::X86Assembler::popcntl, "popcntl %{reg2}, %{reg1}"), "popcntl");
}

TEST_F(AssemblerX86Test, PopcntlAddress) {
  GetAssembler()->popcntl(x86::Register(x86::EDI), x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  const char* expected =
    "popcntl 0xc(%EDI,%EBX,4), %EDI\n";

  DriverStr(expected, "popcntl_address");
}

// Rorl only allows CL as the shift count.
std::string rorl_fn(AssemblerX86Test::Base* assembler_test, x86::X86Assembler* assembler) {
  std::ostringstream str;

  std::vector<x86::Register*> registers = assembler_test->GetRegisters();

  x86::Register shifter(x86::ECX);
  for (auto reg : registers) {
    assembler->rorl(*reg, shifter);
    str << "rorl %cl, %" << assembler_test->GetRegisterName(*reg) << "\n";
  }

  return str.str();
}

TEST_F(AssemblerX86Test, RorlReg) {
  DriverFn(&rorl_fn, "rorl");
}

TEST_F(AssemblerX86Test, RorlImm) {
  DriverStr(RepeatRI(&x86::X86Assembler::rorl, 1U, "rorl ${imm}, %{reg}"), "rorli");
}

// Roll only allows CL as the shift count.
std::string roll_fn(AssemblerX86Test::Base* assembler_test, x86::X86Assembler* assembler) {
  std::ostringstream str;

  std::vector<x86::Register*> registers = assembler_test->GetRegisters();

  x86::Register shifter(x86::ECX);
  for (auto reg : registers) {
    assembler->roll(*reg, shifter);
    str << "roll %cl, %" << assembler_test->GetRegisterName(*reg) << "\n";
  }

  return str.str();
}

TEST_F(AssemblerX86Test, RollReg) {
  DriverFn(&roll_fn, "roll");
}

TEST_F(AssemblerX86Test, RollImm) {
  DriverStr(RepeatRI(&x86::X86Assembler::roll, 1U, "roll ${imm}, %{reg}"), "rolli");
}

TEST_F(AssemblerX86Test, Cvtdq2ps) {
  DriverStr(RepeatFF(&x86::X86Assembler::cvtdq2ps, "cvtdq2ps %{reg2}, %{reg1}"), "cvtdq2ps");
}

TEST_F(AssemblerX86Test, Cvtdq2pd) {
  DriverStr(RepeatFF(&x86::X86Assembler::cvtdq2pd, "cvtdq2pd %{reg2}, %{reg1}"), "cvtdq2pd");
}

TEST_F(AssemblerX86Test, ComissAddr) {
  GetAssembler()->comiss(x86::XmmRegister(x86::XMM0), x86::Address(x86::EAX, 0));
  const char* expected = "comiss 0(%EAX), %xmm0\n";
  DriverStr(expected, "comiss");
}

TEST_F(AssemblerX86Test, UComissAddr) {
  GetAssembler()->ucomiss(x86::XmmRegister(x86::XMM0), x86::Address(x86::EAX, 0));
  const char* expected = "ucomiss 0(%EAX), %xmm0\n";
  DriverStr(expected, "ucomiss");
}

TEST_F(AssemblerX86Test, ComisdAddr) {
  GetAssembler()->comisd(x86::XmmRegister(x86::XMM0), x86::Address(x86::EAX, 0));
  const char* expected = "comisd 0(%EAX), %xmm0\n";
  DriverStr(expected, "comisd");
}

TEST_F(AssemblerX86Test, UComisdAddr) {
  GetAssembler()->ucomisd(x86::XmmRegister(x86::XMM0), x86::Address(x86::EAX, 0));
  const char* expected = "ucomisd 0(%EAX), %xmm0\n";
  DriverStr(expected, "ucomisd");
}

TEST_F(AssemblerX86Test, RoundSS) {
  GetAssembler()->roundss(
      x86::XmmRegister(x86::XMM0), x86::XmmRegister(x86::XMM1), x86::Immediate(1));
  const char* expected = "roundss $1, %xmm1, %xmm0\n";
  DriverStr(expected, "roundss");
}

TEST_F(AssemblerX86Test, RoundSD) {
  GetAssembler()->roundsd(
      x86::XmmRegister(x86::XMM0), x86::XmmRegister(x86::XMM1), x86::Immediate(1));
  const char* expected = "roundsd $1, %xmm1, %xmm0\n";
  DriverStr(expected, "roundsd");
}

TEST_F(AssemblerX86Test, CmovlAddress) {
  GetAssembler()->cmovl(x86::kEqual, x86::Register(x86::EAX), x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  GetAssembler()->cmovl(x86::kNotEqual, x86::Register(x86::EDI), x86::Address(
      x86::Register(x86::ESI), x86::Register(x86::EBX), x86::TIMES_4, 12));
  GetAssembler()->cmovl(x86::kEqual, x86::Register(x86::EDI), x86::Address(
      x86::Register(x86::EDI), x86::Register(x86::EAX), x86::TIMES_4, 12));
  const char* expected =
    "cmovzl 0xc(%EDI,%EBX,4), %eax\n"
    "cmovnzl 0xc(%ESI,%EBX,4), %edi\n"
    "cmovzl 0xc(%EDI,%EAX,4), %edi\n";

  DriverStr(expected, "cmovl_address");
}

TEST_F(AssemblerX86Test, TestbAddressImmediate) {
  GetAssembler()->testb(
      x86::Address(x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12),
      x86::Immediate(1));
  GetAssembler()->testb(
      x86::Address(x86::Register(x86::ESP), FrameOffset(7)),
      x86::Immediate(-128));
  GetAssembler()->testb(
      x86::Address(x86::Register(x86::EBX), MemberOffset(130)),
      x86::Immediate(127));
  const char* expected =
      "testb $1, 0xc(%EDI,%EBX,4)\n"
      "testb $-128, 0x7(%ESP)\n"
      "testb $127, 0x82(%EBX)\n";

  DriverStr(expected, "TestbAddressImmediate");
}

TEST_F(AssemblerX86Test, TestlAddressImmediate) {
  GetAssembler()->testl(
      x86::Address(x86::Register(x86::EDI), x86::Register(x86::EBX), x86::TIMES_4, 12),
      x86::Immediate(1));
  GetAssembler()->testl(
      x86::Address(x86::Register(x86::ESP), FrameOffset(7)),
      x86::Immediate(-100000));
  GetAssembler()->testl(
      x86::Address(x86::Register(x86::EBX), MemberOffset(130)),
      x86::Immediate(77777777));
  const char* expected =
      "testl $1, 0xc(%EDI,%EBX,4)\n"
      "testl $-100000, 0x7(%ESP)\n"
      "testl $77777777, 0x82(%EBX)\n";

  DriverStr(expected, "TestlAddressImmediate");
}

TEST_F(AssemblerX86Test, Movaps) {
  DriverStr(RepeatFF(&x86::X86Assembler::movaps, "movaps %{reg2}, %{reg1}"), "movaps");
}

TEST_F(AssemblerX86Test, MovapsAddr) {
  GetAssembler()->movaps(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movaps(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movaps 0x4(%ESP), %xmm0\n"
    "movaps %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movaps_address");
}

TEST_F(AssemblerX86Test, MovupsAddr) {
  GetAssembler()->movups(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movups(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movups 0x4(%ESP), %xmm0\n"
    "movups %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movups_address");
}

TEST_F(AssemblerX86Test, Movapd) {
  DriverStr(RepeatFF(&x86::X86Assembler::movapd, "movapd %{reg2}, %{reg1}"), "movapd");
}

TEST_F(AssemblerX86Test, MovapdAddr) {
  GetAssembler()->movapd(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movapd(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movapd 0x4(%ESP), %xmm0\n"
    "movapd %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movapd_address");
}

TEST_F(AssemblerX86Test, MovupdAddr) {
  GetAssembler()->movupd(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movupd(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movupd 0x4(%ESP), %xmm0\n"
    "movupd %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movupd_address");
}

TEST_F(AssemblerX86Test, Movdqa) {
  DriverStr(RepeatFF(&x86::X86Assembler::movdqa, "movdqa %{reg2}, %{reg1}"), "movdqa");
}

TEST_F(AssemblerX86Test, MovdqaAddr) {
  GetAssembler()->movdqa(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movdqa(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movdqa 0x4(%ESP), %xmm0\n"
    "movdqa %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movdqa_address");
}

TEST_F(AssemblerX86Test, MovdquAddr) {
  GetAssembler()->movdqu(x86::XmmRegister(x86::XMM0), x86::Address(x86::Register(x86::ESP), 4));
  GetAssembler()->movdqu(x86::Address(x86::Register(x86::ESP), 2), x86::XmmRegister(x86::XMM1));
  const char* expected =
    "movdqu 0x4(%ESP), %xmm0\n"
    "movdqu %xmm1, 0x2(%ESP)\n";
  DriverStr(expected, "movdqu_address");
}

TEST_F(AssemblerX86Test, AddPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::addps, "addps %{reg2}, %{reg1}"), "addps");
}

TEST_F(AssemblerX86Test, AddPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::addpd, "addpd %{reg2}, %{reg1}"), "addpd");
}

TEST_F(AssemblerX86Test, SubPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::subps, "subps %{reg2}, %{reg1}"), "subps");
}

TEST_F(AssemblerX86Test, SubPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::subpd, "subpd %{reg2}, %{reg1}"), "subpd");
}

TEST_F(AssemblerX86Test, MulPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::mulps, "mulps %{reg2}, %{reg1}"), "mulps");
}

TEST_F(AssemblerX86Test, MulPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::mulpd, "mulpd %{reg2}, %{reg1}"), "mulpd");
}

TEST_F(AssemblerX86Test, DivPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::divps, "divps %{reg2}, %{reg1}"), "divps");
}

TEST_F(AssemblerX86Test, DivPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::divpd, "divpd %{reg2}, %{reg1}"), "divpd");
}

TEST_F(AssemblerX86Test, PAddB) {
  DriverStr(RepeatFF(&x86::X86Assembler::paddb, "paddb %{reg2}, %{reg1}"), "paddb");
}

TEST_F(AssemblerX86Test, PSubB) {
  DriverStr(RepeatFF(&x86::X86Assembler::psubb, "psubb %{reg2}, %{reg1}"), "psubb");
}

TEST_F(AssemblerX86Test, PAddW) {
  DriverStr(RepeatFF(&x86::X86Assembler::paddw, "paddw %{reg2}, %{reg1}"), "paddw");
}

TEST_F(AssemblerX86Test, PSubW) {
  DriverStr(RepeatFF(&x86::X86Assembler::psubw, "psubw %{reg2}, %{reg1}"), "psubw");
}

TEST_F(AssemblerX86Test, PMullW) {
  DriverStr(RepeatFF(&x86::X86Assembler::pmullw, "pmullw %{reg2}, %{reg1}"), "pmullw");
}

TEST_F(AssemblerX86Test, PAddD) {
  DriverStr(RepeatFF(&x86::X86Assembler::paddd, "paddd %{reg2}, %{reg1}"), "paddd");
}

TEST_F(AssemblerX86Test, PSubD) {
  DriverStr(RepeatFF(&x86::X86Assembler::psubd, "psubd %{reg2}, %{reg1}"), "psubd");
}

TEST_F(AssemblerX86Test, PMullD) {
  DriverStr(RepeatFF(&x86::X86Assembler::pmulld, "pmulld %{reg2}, %{reg1}"), "pmulld");
}

TEST_F(AssemblerX86Test, PAddQ) {
  DriverStr(RepeatFF(&x86::X86Assembler::paddq, "paddq %{reg2}, %{reg1}"), "paddq");
}

TEST_F(AssemblerX86Test, PSubQ) {
  DriverStr(RepeatFF(&x86::X86Assembler::psubq, "psubq %{reg2}, %{reg1}"), "psubq");
}

TEST_F(AssemblerX86Test, XorPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::xorpd, "xorpd %{reg2}, %{reg1}"), "xorpd");
}

TEST_F(AssemblerX86Test, XorPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::xorps, "xorps %{reg2}, %{reg1}"), "xorps");
}

TEST_F(AssemblerX86Test, PXor) {
  DriverStr(RepeatFF(&x86::X86Assembler::pxor, "pxor %{reg2}, %{reg1}"), "pxor");
}

TEST_F(AssemblerX86Test, AndPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::andpd, "andpd %{reg2}, %{reg1}"), "andpd");
}

TEST_F(AssemblerX86Test, AndPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::andps, "andps %{reg2}, %{reg1}"), "andps");
}

TEST_F(AssemblerX86Test, PAnd) {
  DriverStr(RepeatFF(&x86::X86Assembler::pand, "pand %{reg2}, %{reg1}"), "pand");
}

TEST_F(AssemblerX86Test, AndnPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::andnpd, "andnpd %{reg2}, %{reg1}"), "andnpd");
}

TEST_F(AssemblerX86Test, AndnPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::andnps, "andnps %{reg2}, %{reg1}"), "andnps");
}

TEST_F(AssemblerX86Test, PAndn) {
  DriverStr(RepeatFF(&x86::X86Assembler::pandn, "pandn %{reg2}, %{reg1}"), "pandn");
}

TEST_F(AssemblerX86Test, OrPD) {
  DriverStr(RepeatFF(&x86::X86Assembler::orpd, "orpd %{reg2}, %{reg1}"), "orpd");
}

TEST_F(AssemblerX86Test, OrPS) {
  DriverStr(RepeatFF(&x86::X86Assembler::orps, "orps %{reg2}, %{reg1}"), "orps");
}

TEST_F(AssemblerX86Test, POr) {
  DriverStr(RepeatFF(&x86::X86Assembler::por, "por %{reg2}, %{reg1}"), "por");
}

TEST_F(AssemblerX86Test, PAvgB) {
  DriverStr(RepeatFF(&x86::X86Assembler::pavgb, "pavgb %{reg2}, %{reg1}"), "pavgb");
}

TEST_F(AssemblerX86Test, PAvgW) {
  DriverStr(RepeatFF(&x86::X86Assembler::pavgw, "pavgw %{reg2}, %{reg1}"), "pavgw");
}

TEST_F(AssemblerX86Test, PCmpeqB) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpeqb, "pcmpeqb %{reg2}, %{reg1}"), "cmpeqb");
}

TEST_F(AssemblerX86Test, PCmpeqW) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpeqw, "pcmpeqw %{reg2}, %{reg1}"), "cmpeqw");
}

TEST_F(AssemblerX86Test, PCmpeqD) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpeqd, "pcmpeqd %{reg2}, %{reg1}"), "cmpeqd");
}

TEST_F(AssemblerX86Test, PCmpeqQ) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpeqq, "pcmpeqq %{reg2}, %{reg1}"), "cmpeqq");
}

TEST_F(AssemblerX86Test, PCmpgtB) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpgtb, "pcmpgtb %{reg2}, %{reg1}"), "cmpgtb");
}

TEST_F(AssemblerX86Test, PCmpgtW) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpgtw, "pcmpgtw %{reg2}, %{reg1}"), "cmpgtw");
}

TEST_F(AssemblerX86Test, PCmpgtD) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpgtd, "pcmpgtd %{reg2}, %{reg1}"), "cmpgtd");
}

TEST_F(AssemblerX86Test, PCmpgtQ) {
  DriverStr(RepeatFF(&x86::X86Assembler::pcmpgtq, "pcmpgtq %{reg2}, %{reg1}"), "cmpgtq");
}

TEST_F(AssemblerX86Test, ShufPS) {
  DriverStr(RepeatFFI(&x86::X86Assembler::shufps, 1, "shufps ${imm}, %{reg2}, %{reg1}"), "shufps");
}

TEST_F(AssemblerX86Test, ShufPD) {
  DriverStr(RepeatFFI(&x86::X86Assembler::shufpd, 1, "shufpd ${imm}, %{reg2}, %{reg1}"), "shufpd");
}

TEST_F(AssemblerX86Test, PShufD) {
  DriverStr(RepeatFFI(&x86::X86Assembler::pshufd, 1, "pshufd ${imm}, %{reg2}, %{reg1}"), "pshufd");
}

TEST_F(AssemblerX86Test, Punpcklbw) {
  DriverStr(RepeatFF(&x86::X86Assembler::punpcklbw, "punpcklbw %{reg2}, %{reg1}"), "punpcklbw");
}

TEST_F(AssemblerX86Test, Punpcklwd) {
  DriverStr(RepeatFF(&x86::X86Assembler::punpcklwd, "punpcklwd %{reg2}, %{reg1}"), "punpcklwd");
}

TEST_F(AssemblerX86Test, Punpckldq) {
  DriverStr(RepeatFF(&x86::X86Assembler::punpckldq, "punpckldq %{reg2}, %{reg1}"), "punpckldq");
}

TEST_F(AssemblerX86Test, Punpcklqdq) {
  DriverStr(RepeatFF(&x86::X86Assembler::punpcklqdq, "punpcklqdq %{reg2}, %{reg1}"), "punpcklqdq");
}

TEST_F(AssemblerX86Test, psllw) {
  GetAssembler()->psllw(x86::XMM0, CreateImmediate(16));
  DriverStr("psllw $0x10, %xmm0\n", "psllwi");
}

TEST_F(AssemblerX86Test, pslld) {
  GetAssembler()->pslld(x86::XMM0, CreateImmediate(16));
  DriverStr("pslld $0x10, %xmm0\n", "pslldi");
}

TEST_F(AssemblerX86Test, psllq) {
  GetAssembler()->psllq(x86::XMM0, CreateImmediate(16));
  DriverStr("psllq $0x10, %xmm0\n", "psllqi");
}

TEST_F(AssemblerX86Test, psraw) {
  GetAssembler()->psraw(x86::XMM0, CreateImmediate(16));
  DriverStr("psraw $0x10, %xmm0\n", "psrawi");
}

TEST_F(AssemblerX86Test, psrad) {
  GetAssembler()->psrad(x86::XMM0, CreateImmediate(16));
  DriverStr("psrad $0x10, %xmm0\n", "psradi");
}

TEST_F(AssemblerX86Test, psrlw) {
  GetAssembler()->psrlw(x86::XMM0, CreateImmediate(16));
  DriverStr("psrlw $0x10, %xmm0\n", "psrlwi");
}

TEST_F(AssemblerX86Test, psrld) {
  GetAssembler()->psrld(x86::XMM0, CreateImmediate(16));
  DriverStr("psrld $0x10, %xmm0\n", "psrldi");
}

TEST_F(AssemblerX86Test, psrlq) {
  GetAssembler()->psrlq(x86::XMM0, CreateImmediate(16));
  DriverStr("psrlq $0x10, %xmm0\n", "psrlqi");
}

TEST_F(AssemblerX86Test, psrldq) {
  GetAssembler()->psrldq(x86::XMM0, CreateImmediate(16));
  DriverStr("psrldq $0x10, %xmm0\n", "psrldqi");
}

/////////////////
// Near labels //
/////////////////

TEST_F(AssemblerX86Test, Jecxz) {
  x86::NearLabel target;
  GetAssembler()->jecxz(&target);
  GetAssembler()->addl(x86::EDI, x86::Address(x86::ESP, 4));
  GetAssembler()->Bind(&target);
  const char* expected =
    "jecxz 1f\n"
    "addl 4(%ESP),%EDI\n"
    "1:\n";

  DriverStr(expected, "jecxz");
}

TEST_F(AssemblerX86Test, NearLabel) {
  // Test both forward and backward branches.
  x86::NearLabel start, target;
  GetAssembler()->Bind(&start);
  GetAssembler()->j(x86::kEqual, &target);
  GetAssembler()->jmp(&target);
  GetAssembler()->jecxz(&target);
  GetAssembler()->addl(x86::EDI, x86::Address(x86::ESP, 4));
  GetAssembler()->Bind(&target);
  GetAssembler()->j(x86::kNotEqual, &start);
  GetAssembler()->jmp(&start);
  const char* expected =
    "1: je 2f\n"
    "jmp 2f\n"
    "jecxz 2f\n"
    "addl 4(%ESP),%EDI\n"
    "2: jne 1b\n"
    "jmp 1b\n";

  DriverStr(expected, "near_label");
}

TEST_F(AssemblerX86Test, Cmpb) {
  GetAssembler()->cmpb(x86::Address(x86::EDI, 128), x86::Immediate(0));
  const char* expected = "cmpb $0, 128(%EDI)\n";
  DriverStr(expected, "cmpb");
}

}  // namespace art
