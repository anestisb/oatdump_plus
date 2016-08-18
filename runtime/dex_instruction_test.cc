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

#include "dex_instruction-inl.h"
#include "gtest/gtest.h"

namespace art {

TEST(StaticGetters, PropertiesOfNopTest) {
  Instruction::Code nop = Instruction::NOP;
  EXPECT_STREQ("nop", Instruction::Name(nop));
  EXPECT_EQ(Instruction::k10x, Instruction::FormatOf(nop));
  EXPECT_EQ(Instruction::kIndexNone, Instruction::IndexTypeOf(nop));
  EXPECT_EQ(Instruction::kContinue, Instruction::FlagsOf(nop));
  EXPECT_EQ(Instruction::kVerifyNone, Instruction::VerifyFlagsOf(nop));
}

static void Build45cc(uint8_t num_args, uint16_t method_idx, uint16_t proto_idx,
                      uint16_t arg_regs, uint16_t* out) {
  // A = num argument registers
  // B = method_idx
  // C - G = argument registers
  // H = proto_idx
  //
  // op = 0xFA
  //
  // format:
  // AG op BBBB FEDC HHHH
  out[0] = 0;
  out[0] |= (num_args << 12);
  out[0] |= 0x00FA;

  out[1] = method_idx;
  out[2] = arg_regs;
  out[3] = proto_idx;
}

static void Build4rcc(uint16_t num_args, uint16_t method_idx, uint16_t proto_idx,
                      uint16_t arg_regs_start, uint16_t* out) {
  // A = num argument registers
  // B = method_idx
  // C = first argument register
  // H = proto_idx
  //
  // op = 0xFB
  //
  // format:
  // AA op BBBB CCCC HHHH
  out[0] = 0;
  out[0] |= (num_args << 8);
  out[0] |= 0x00FB;

  out[1] = method_idx;
  out[2] = arg_regs_start;
  out[3] = proto_idx;
}

TEST(Instruction, PropertiesOf45cc) {
  uint16_t instruction[4];
  Build45cc(4u /* num_vregs */, 16u /* method_idx */, 32u /* proto_idx */,
            0xcafe /* arg_regs */, instruction);

  const Instruction* ins = Instruction::At(instruction);
  ASSERT_EQ(4u, ins->SizeInCodeUnits());

  ASSERT_TRUE(ins->HasVRegA());
  ASSERT_EQ(4, ins->VRegA());
  ASSERT_EQ(4u, ins->VRegA_45cc());
  ASSERT_EQ(4u, ins->VRegA_45cc(instruction[0]));

  ASSERT_TRUE(ins->HasVRegB());
  ASSERT_EQ(16, ins->VRegB());
  ASSERT_EQ(16u, ins->VRegB_45cc());

  ASSERT_TRUE(ins->HasVRegC());
  ASSERT_EQ(0xe, ins->VRegC());
  ASSERT_EQ(0xe, ins->VRegC_45cc());

  ASSERT_TRUE(ins->HasVRegH());
  ASSERT_EQ(32, ins->VRegH());
  ASSERT_EQ(32, ins->VRegH_45cc());

  ASSERT_TRUE(ins->HasVarArgs());

  uint32_t arg_regs[Instruction::kMaxVarArgRegs];
  ins->GetVarArgs(arg_regs);
  ASSERT_EQ(0xeu, arg_regs[0]);
  ASSERT_EQ(0xfu, arg_regs[1]);
  ASSERT_EQ(0xau, arg_regs[2]);
  ASSERT_EQ(0xcu, arg_regs[3]);
}

TEST(Instruction, PropertiesOf4rcc) {
  uint16_t instruction[4];
  Build4rcc(4u /* num_vregs */, 16u /* method_idx */, 32u /* proto_idx */,
            0xcafe /* arg_regs */, instruction);

  const Instruction* ins = Instruction::At(instruction);
  ASSERT_EQ(4u, ins->SizeInCodeUnits());

  ASSERT_TRUE(ins->HasVRegA());
  ASSERT_EQ(4, ins->VRegA());
  ASSERT_EQ(4u, ins->VRegA_4rcc());
  ASSERT_EQ(4u, ins->VRegA_4rcc(instruction[0]));

  ASSERT_TRUE(ins->HasVRegB());
  ASSERT_EQ(16, ins->VRegB());
  ASSERT_EQ(16u, ins->VRegB_4rcc());

  ASSERT_TRUE(ins->HasVRegC());
  ASSERT_EQ(0xcafe, ins->VRegC());
  ASSERT_EQ(0xcafe, ins->VRegC_4rcc());

  ASSERT_TRUE(ins->HasVRegH());
  ASSERT_EQ(32, ins->VRegH());
  ASSERT_EQ(32, ins->VRegH_4rcc());

  ASSERT_FALSE(ins->HasVarArgs());
}

}  // namespace art
