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

#include <functional>
#include <memory>

#include "base/macros.h"
#include "builder.h"
#include "codegen_test_utils.h"
#include "dex_file.h"
#include "dex_instruction.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "register_allocator_linear_scan.h"
#include "utils.h"
#include "utils/arm/assembler_arm_vixl.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/mips/managed_register_mips.h"
#include "utils/mips64/managed_register_mips64.h"
#include "utils/x86/managed_register_x86.h"

#include "gtest/gtest.h"

namespace art {

// Return all combinations of ISA and code generator that are executable on
// hardware, or on simulator, and that we'd like to test.
static ::std::vector<CodegenTargetConfig> GetTargetConfigs() {
  ::std::vector<CodegenTargetConfig> v;
  ::std::vector<CodegenTargetConfig> test_config_candidates = {
#ifdef ART_ENABLE_CODEGEN_arm
    CodegenTargetConfig(kArm, create_codegen_arm),
    CodegenTargetConfig(kThumb2, create_codegen_arm),
    CodegenTargetConfig(kArm, create_codegen_arm_vixl32),
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
    CodegenTargetConfig(kArm64, create_codegen_arm64),
#endif
#ifdef ART_ENABLE_CODEGEN_x86
    CodegenTargetConfig(kX86, create_codegen_x86),
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
    CodegenTargetConfig(kX86_64, create_codegen_x86_64),
#endif
#ifdef ART_ENABLE_CODEGEN_mips
    CodegenTargetConfig(kMips, create_codegen_mips),
#endif
#ifdef ART_ENABLE_CODEGEN_mips64
    CodegenTargetConfig(kMips64, create_codegen_mips64)
#endif
  };

  for (auto test_config : test_config_candidates) {
    if (CanExecute(test_config.GetInstructionSet())) {
      v.push_back(test_config);
    }
  }

  return v;
}

static void TestCode(const uint16_t* data,
                     bool has_result = false,
                     int32_t expected = 0) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    HGraph* graph = CreateCFG(&arena, data);
    // Remove suspend checks, they cannot be executed in this context.
    RemoveSuspendChecks(graph);
    RunCode(target_config, graph, [](HGraph*) {}, has_result, expected);
  }
}

static void TestCodeLong(const uint16_t* data,
                         bool has_result,
                         int64_t expected) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    HGraph* graph = CreateCFG(&arena, data, Primitive::kPrimLong);
    // Remove suspend checks, they cannot be executed in this context.
    RemoveSuspendChecks(graph);
    RunCode(target_config, graph, [](HGraph*) {}, has_result, expected);
  }
}

class CodegenTest : public CommonCompilerTest {};

TEST_F(CodegenTest, ReturnVoid) {
  const uint16_t data[] = ZERO_REGISTER_CODE_ITEM(Instruction::RETURN_VOID);
  TestCode(data);
}

TEST_F(CodegenTest, CFG1) {
  const uint16_t data[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, CFG2) {
  const uint16_t data[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x100,
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, CFG3) {
  const uint16_t data1[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x200,
    Instruction::RETURN_VOID,
    Instruction::GOTO | 0xFF00);

  TestCode(data1);

  const uint16_t data2[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO_16, 3,
    Instruction::RETURN_VOID,
    Instruction::GOTO_16, 0xFFFF);

  TestCode(data2);

  const uint16_t data3[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO_32, 4, 0,
    Instruction::RETURN_VOID,
    Instruction::GOTO_32, 0xFFFF, 0xFFFF);

  TestCode(data3);
}

TEST_F(CodegenTest, CFG4) {
  const uint16_t data[] = ZERO_REGISTER_CODE_ITEM(
    Instruction::RETURN_VOID,
    Instruction::GOTO | 0x100,
    Instruction::GOTO | 0xFE00);

  TestCode(data);
}

TEST_F(CodegenTest, CFG5) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 3,
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, IntConstant) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, Return1) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN | 0);

  TestCode(data, true, 0);
}

TEST_F(CodegenTest, Return2) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 0 | 1 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 0);
}

TEST_F(CodegenTest, Return3) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnIf1) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::IF_EQ, 3,
    Instruction::RETURN | 0 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnIf2) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::IF_EQ | 0 << 4 | 1 << 8, 3,
    Instruction::RETURN | 0 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 0);
}

// Exercise bit-wise (one's complement) not-int instruction.
#define NOT_INT_TEST(TEST_NAME, INPUT, EXPECTED_OUTPUT) \
TEST_F(CodegenTest, TEST_NAME) {                        \
  const int32_t input = INPUT;                          \
  const uint16_t input_lo = Low16Bits(input);           \
  const uint16_t input_hi = High16Bits(input);          \
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(      \
      Instruction::CONST | 0 << 8, input_lo, input_hi,  \
      Instruction::NOT_INT | 1 << 8 | 0 << 12 ,         \
      Instruction::RETURN | 1 << 8);                    \
                                                        \
  TestCode(data, true, EXPECTED_OUTPUT);                \
}

NOT_INT_TEST(ReturnNotIntMinus2, -2, 1)
NOT_INT_TEST(ReturnNotIntMinus1, -1, 0)
NOT_INT_TEST(ReturnNotInt0, 0, -1)
NOT_INT_TEST(ReturnNotInt1, 1, -2)
NOT_INT_TEST(ReturnNotIntINT32_MIN, -2147483648, 2147483647)  // (2^31) - 1
NOT_INT_TEST(ReturnNotIntINT32_MINPlus1, -2147483647, 2147483646)  // (2^31) - 2
NOT_INT_TEST(ReturnNotIntINT32_MAXMinus1, 2147483646, -2147483647)  // -(2^31) - 1
NOT_INT_TEST(ReturnNotIntINT32_MAX, 2147483647, -2147483648)  // -(2^31)

#undef NOT_INT_TEST

// Exercise bit-wise (one's complement) not-long instruction.
#define NOT_LONG_TEST(TEST_NAME, INPUT, EXPECTED_OUTPUT)                 \
TEST_F(CodegenTest, TEST_NAME) {                                         \
  const int64_t input = INPUT;                                           \
  const uint16_t word0 = Low16Bits(Low32Bits(input));   /* LSW. */       \
  const uint16_t word1 = High16Bits(Low32Bits(input));                   \
  const uint16_t word2 = Low16Bits(High32Bits(input));                   \
  const uint16_t word3 = High16Bits(High32Bits(input)); /* MSW. */       \
  const uint16_t data[] = FOUR_REGISTERS_CODE_ITEM(                      \
      Instruction::CONST_WIDE | 0 << 8, word0, word1, word2, word3,      \
      Instruction::NOT_LONG | 2 << 8 | 0 << 12,                          \
      Instruction::RETURN_WIDE | 2 << 8);                                \
                                                                         \
  TestCodeLong(data, true, EXPECTED_OUTPUT);                             \
}

NOT_LONG_TEST(ReturnNotLongMinus2, INT64_C(-2), INT64_C(1))
NOT_LONG_TEST(ReturnNotLongMinus1, INT64_C(-1), INT64_C(0))
NOT_LONG_TEST(ReturnNotLong0, INT64_C(0), INT64_C(-1))
NOT_LONG_TEST(ReturnNotLong1, INT64_C(1), INT64_C(-2))

NOT_LONG_TEST(ReturnNotLongINT32_MIN,
              INT64_C(-2147483648),
              INT64_C(2147483647))  // (2^31) - 1
NOT_LONG_TEST(ReturnNotLongINT32_MINPlus1,
              INT64_C(-2147483647),
              INT64_C(2147483646))  // (2^31) - 2
NOT_LONG_TEST(ReturnNotLongINT32_MAXMinus1,
              INT64_C(2147483646),
              INT64_C(-2147483647))  // -(2^31) - 1
NOT_LONG_TEST(ReturnNotLongINT32_MAX,
              INT64_C(2147483647),
              INT64_C(-2147483648))  // -(2^31)

// Note that the C++ compiler won't accept
// INT64_C(-9223372036854775808) (that is, INT64_MIN) as a valid
// int64_t literal, so we use INT64_C(-9223372036854775807)-1 instead.
NOT_LONG_TEST(ReturnNotINT64_MIN,
              INT64_C(-9223372036854775807)-1,
              INT64_C(9223372036854775807));  // (2^63) - 1
NOT_LONG_TEST(ReturnNotINT64_MINPlus1,
              INT64_C(-9223372036854775807),
              INT64_C(9223372036854775806));  // (2^63) - 2
NOT_LONG_TEST(ReturnNotLongINT64_MAXMinus1,
              INT64_C(9223372036854775806),
              INT64_C(-9223372036854775807));  // -(2^63) - 1
NOT_LONG_TEST(ReturnNotLongINT64_MAX,
              INT64_C(9223372036854775807),
              INT64_C(-9223372036854775807)-1);  // -(2^63)

#undef NOT_LONG_TEST

TEST_F(CodegenTest, IntToLongOfLongToInt) {
  const int64_t input = INT64_C(4294967296);             // 2^32
  const uint16_t word0 = Low16Bits(Low32Bits(input));    // LSW.
  const uint16_t word1 = High16Bits(Low32Bits(input));
  const uint16_t word2 = Low16Bits(High32Bits(input));
  const uint16_t word3 = High16Bits(High32Bits(input));  // MSW.
  const uint16_t data[] = FIVE_REGISTERS_CODE_ITEM(
      Instruction::CONST_WIDE | 0 << 8, word0, word1, word2, word3,
      Instruction::CONST_WIDE | 2 << 8, 1, 0, 0, 0,
      Instruction::ADD_LONG | 0, 0 << 8 | 2,             // v0 <- 2^32 + 1
      Instruction::LONG_TO_INT | 4 << 8 | 0 << 12,
      Instruction::INT_TO_LONG | 2 << 8 | 4 << 12,
      Instruction::RETURN_WIDE | 2 << 8);

  TestCodeLong(data, true, 1);
}

TEST_F(CodegenTest, ReturnAdd1) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::ADD_INT, 1 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd2) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::ADD_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd3) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::ADD_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd4) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::ADD_INT_LIT16, 3,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnMulInt) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::MUL_INT, 1 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulInt2addr) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::MUL_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulLong) {
  const uint16_t data[] = FOUR_REGISTERS_CODE_ITEM(
    Instruction::CONST_WIDE | 0 << 8, 3, 0, 0, 0,
    Instruction::CONST_WIDE | 2 << 8, 4, 0, 0, 0,
    Instruction::MUL_LONG, 2 << 8 | 0,
    Instruction::RETURN_WIDE);

  TestCodeLong(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulLong2addr) {
  const uint16_t data[] = FOUR_REGISTERS_CODE_ITEM(
    Instruction::CONST_WIDE | 0 << 8, 3, 0, 0, 0,
    Instruction::CONST_WIDE | 2 << 8, 4, 0, 0, 0,
    Instruction::MUL_LONG_2ADDR | 2 << 12,
    Instruction::RETURN_WIDE);

  TestCodeLong(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulIntLit8) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::MUL_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulIntLit16) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::MUL_INT_LIT16, 3,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, NonMaterializedCondition) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    ArenaPool pool;
    ArenaAllocator allocator(&pool);

    HGraph* graph = CreateGraph(&allocator);

    HBasicBlock* entry = new (&allocator) HBasicBlock(graph);
    graph->AddBlock(entry);
    graph->SetEntryBlock(entry);
    entry->AddInstruction(new (&allocator) HGoto());

    HBasicBlock* first_block = new (&allocator) HBasicBlock(graph);
    graph->AddBlock(first_block);
    entry->AddSuccessor(first_block);
    HIntConstant* constant0 = graph->GetIntConstant(0);
    HIntConstant* constant1 = graph->GetIntConstant(1);
    HEqual* equal = new (&allocator) HEqual(constant0, constant0);
    first_block->AddInstruction(equal);
    first_block->AddInstruction(new (&allocator) HIf(equal));

    HBasicBlock* then_block = new (&allocator) HBasicBlock(graph);
    HBasicBlock* else_block = new (&allocator) HBasicBlock(graph);
    HBasicBlock* exit_block = new (&allocator) HBasicBlock(graph);
    graph->SetExitBlock(exit_block);

    graph->AddBlock(then_block);
    graph->AddBlock(else_block);
    graph->AddBlock(exit_block);
    first_block->AddSuccessor(then_block);
    first_block->AddSuccessor(else_block);
    then_block->AddSuccessor(exit_block);
    else_block->AddSuccessor(exit_block);

    exit_block->AddInstruction(new (&allocator) HExit());
    then_block->AddInstruction(new (&allocator) HReturn(constant0));
    else_block->AddInstruction(new (&allocator) HReturn(constant1));

    ASSERT_FALSE(equal->IsEmittedAtUseSite());
    graph->BuildDominatorTree();
    PrepareForRegisterAllocation(graph).Run();
    ASSERT_TRUE(equal->IsEmittedAtUseSite());

    auto hook_before_codegen = [](HGraph* graph_in) {
      HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
      HParallelMove* move = new (graph_in->GetArena()) HParallelMove(graph_in->GetArena());
      block->InsertInstructionBefore(move, block->GetLastInstruction());
    };

    RunCode(target_config, graph, hook_before_codegen, true, 0);
  }
}

TEST_F(CodegenTest, MaterializedCondition1) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    // Check that condition are materialized correctly. A materialized condition
    // should yield `1` if it evaluated to true, and `0` otherwise.
    // We force the materialization of comparisons for different combinations of

    // inputs and check the results.

    int lhs[] = {1, 2, -1, 2, 0xabc};
    int rhs[] = {2, 1, 2, -1, 0xabc};

    for (size_t i = 0; i < arraysize(lhs); i++) {
      ArenaPool pool;
      ArenaAllocator allocator(&pool);
      HGraph* graph = CreateGraph(&allocator);

      HBasicBlock* entry_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(entry_block);
      graph->SetEntryBlock(entry_block);
      entry_block->AddInstruction(new (&allocator) HGoto());
      HBasicBlock* code_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(code_block);
      HBasicBlock* exit_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(exit_block);
      exit_block->AddInstruction(new (&allocator) HExit());

      entry_block->AddSuccessor(code_block);
      code_block->AddSuccessor(exit_block);
      graph->SetExitBlock(exit_block);

      HIntConstant* cst_lhs = graph->GetIntConstant(lhs[i]);
      HIntConstant* cst_rhs = graph->GetIntConstant(rhs[i]);
      HLessThan cmp_lt(cst_lhs, cst_rhs);
      code_block->AddInstruction(&cmp_lt);
      HReturn ret(&cmp_lt);
      code_block->AddInstruction(&ret);

      graph->BuildDominatorTree();
      auto hook_before_codegen = [](HGraph* graph_in) {
        HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
        HParallelMove* move = new (graph_in->GetArena()) HParallelMove(graph_in->GetArena());
        block->InsertInstructionBefore(move, block->GetLastInstruction());
      };
      RunCode(target_config, graph, hook_before_codegen, true, lhs[i] < rhs[i]);
    }
  }
}

TEST_F(CodegenTest, MaterializedCondition2) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    // Check that HIf correctly interprets a materialized condition.
    // We force the materialization of comparisons for different combinations of
    // inputs. An HIf takes the materialized combination as input and returns a
    // value that we verify.

    int lhs[] = {1, 2, -1, 2, 0xabc};
    int rhs[] = {2, 1, 2, -1, 0xabc};


    for (size_t i = 0; i < arraysize(lhs); i++) {
      ArenaPool pool;
      ArenaAllocator allocator(&pool);
      HGraph* graph = CreateGraph(&allocator);

      HBasicBlock* entry_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(entry_block);
      graph->SetEntryBlock(entry_block);
      entry_block->AddInstruction(new (&allocator) HGoto());

      HBasicBlock* if_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(if_block);
      HBasicBlock* if_true_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(if_true_block);
      HBasicBlock* if_false_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(if_false_block);
      HBasicBlock* exit_block = new (&allocator) HBasicBlock(graph);
      graph->AddBlock(exit_block);
      exit_block->AddInstruction(new (&allocator) HExit());

      graph->SetEntryBlock(entry_block);
      entry_block->AddSuccessor(if_block);
      if_block->AddSuccessor(if_true_block);
      if_block->AddSuccessor(if_false_block);
      if_true_block->AddSuccessor(exit_block);
      if_false_block->AddSuccessor(exit_block);
      graph->SetExitBlock(exit_block);

      HIntConstant* cst_lhs = graph->GetIntConstant(lhs[i]);
      HIntConstant* cst_rhs = graph->GetIntConstant(rhs[i]);
      HLessThan cmp_lt(cst_lhs, cst_rhs);
      if_block->AddInstruction(&cmp_lt);
      // We insert a dummy instruction to separate the HIf from the HLessThan
      // and force the materialization of the condition.
      HMemoryBarrier force_materialization(MemBarrierKind::kAnyAny, 0);
      if_block->AddInstruction(&force_materialization);
      HIf if_lt(&cmp_lt);
      if_block->AddInstruction(&if_lt);

      HIntConstant* cst_lt = graph->GetIntConstant(1);
      HReturn ret_lt(cst_lt);
      if_true_block->AddInstruction(&ret_lt);
      HIntConstant* cst_ge = graph->GetIntConstant(0);
      HReturn ret_ge(cst_ge);
      if_false_block->AddInstruction(&ret_ge);

      graph->BuildDominatorTree();
      auto hook_before_codegen = [](HGraph* graph_in) {
        HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
        HParallelMove* move = new (graph_in->GetArena()) HParallelMove(graph_in->GetArena());
        block->InsertInstructionBefore(move, block->GetLastInstruction());
      };
      RunCode(target_config, graph, hook_before_codegen, true, lhs[i] < rhs[i]);
    }
  }
}

TEST_F(CodegenTest, ReturnDivIntLit8) {
  const uint16_t data[] = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::DIV_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnDivInt2Addr) {
  const uint16_t data[] = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0,
    Instruction::CONST_4 | 2 << 12 | 1 << 8,
    Instruction::DIV_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 2);
}

// Helper method.
static void TestComparison(IfCondition condition,
                           int64_t i,
                           int64_t j,
                           Primitive::Type type,
                           const CodegenTargetConfig target_config) {
  ArenaPool pool;
  ArenaAllocator allocator(&pool);
  HGraph* graph = CreateGraph(&allocator);

  HBasicBlock* entry_block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(entry_block);
  graph->SetEntryBlock(entry_block);
  entry_block->AddInstruction(new (&allocator) HGoto());

  HBasicBlock* block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(block);

  HBasicBlock* exit_block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(exit_block);
  graph->SetExitBlock(exit_block);
  exit_block->AddInstruction(new (&allocator) HExit());

  entry_block->AddSuccessor(block);
  block->AddSuccessor(exit_block);

  HInstruction* op1;
  HInstruction* op2;
  if (type == Primitive::kPrimInt) {
    op1 = graph->GetIntConstant(i);
    op2 = graph->GetIntConstant(j);
  } else {
    DCHECK_EQ(type, Primitive::kPrimLong);
    op1 = graph->GetLongConstant(i);
    op2 = graph->GetLongConstant(j);
  }

  HInstruction* comparison = nullptr;
  bool expected_result = false;
  const uint64_t x = i;
  const uint64_t y = j;
  switch (condition) {
    case kCondEQ:
      comparison = new (&allocator) HEqual(op1, op2);
      expected_result = (i == j);
      break;
    case kCondNE:
      comparison = new (&allocator) HNotEqual(op1, op2);
      expected_result = (i != j);
      break;
    case kCondLT:
      comparison = new (&allocator) HLessThan(op1, op2);
      expected_result = (i < j);
      break;
    case kCondLE:
      comparison = new (&allocator) HLessThanOrEqual(op1, op2);
      expected_result = (i <= j);
      break;
    case kCondGT:
      comparison = new (&allocator) HGreaterThan(op1, op2);
      expected_result = (i > j);
      break;
    case kCondGE:
      comparison = new (&allocator) HGreaterThanOrEqual(op1, op2);
      expected_result = (i >= j);
      break;
    case kCondB:
      comparison = new (&allocator) HBelow(op1, op2);
      expected_result = (x < y);
      break;
    case kCondBE:
      comparison = new (&allocator) HBelowOrEqual(op1, op2);
      expected_result = (x <= y);
      break;
    case kCondA:
      comparison = new (&allocator) HAbove(op1, op2);
      expected_result = (x > y);
      break;
    case kCondAE:
      comparison = new (&allocator) HAboveOrEqual(op1, op2);
      expected_result = (x >= y);
      break;
  }
  block->AddInstruction(comparison);
  block->AddInstruction(new (&allocator) HReturn(comparison));

  graph->BuildDominatorTree();
  RunCode(target_config, graph, [](HGraph*) {}, true, expected_result);
}

TEST_F(CodegenTest, ComparisonsInt) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    for (int64_t i = -1; i <= 1; i++) {
      for (int64_t j = -1; j <= 1; j++) {
        for (int cond = kCondFirst; cond <= kCondLast; cond++) {
          TestComparison(static_cast<IfCondition>(cond), i, j, Primitive::kPrimInt, target_config);
        }
      }
    }
  }
}

TEST_F(CodegenTest, ComparisonsLong) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    for (int64_t i = -1; i <= 1; i++) {
      for (int64_t j = -1; j <= 1; j++) {
        for (int cond = kCondFirst; cond <= kCondLast; cond++) {
          TestComparison(static_cast<IfCondition>(cond), i, j, Primitive::kPrimLong, target_config);
        }
      }
    }
  }
}

#ifdef ART_ENABLE_CODEGEN_arm
TEST_F(CodegenTest, ARMVIXLParallelMoveResolver) {
  std::unique_ptr<const ArmInstructionSetFeatures> features(
      ArmInstructionSetFeatures::FromCppDefines());
  ArenaPool pool;
  ArenaAllocator allocator(&pool);
  HGraph* graph = CreateGraph(&allocator);
  arm::CodeGeneratorARMVIXL codegen(graph, *features.get(), CompilerOptions());

  codegen.Initialize();

  // This will result in calling EmitSwap -> void ParallelMoveResolverARMVIXL::Exchange(int mem1,
  // int mem2) which was faulty (before the fix). So previously GPR and FP scratch registers were
  // used as temps; however GPR scratch register is required for big stack offsets which don't fit
  // LDR encoding. So the following code is a regression test for that situation.
  HParallelMove* move = new (graph->GetArena()) HParallelMove(graph->GetArena());
  move->AddMove(Location::StackSlot(0), Location::StackSlot(8192), Primitive::kPrimInt, nullptr);
  move->AddMove(Location::StackSlot(8192), Location::StackSlot(0), Primitive::kPrimInt, nullptr);
  codegen.GetMoveResolver()->EmitNativeCode(move);

  InternalCodeAllocator code_allocator;
  codegen.Finalize(&code_allocator);
}
#endif

#ifdef ART_ENABLE_CODEGEN_arm64
// Regression test for b/34760542.
TEST_F(CodegenTest, ARM64ParallelMoveResolverB34760542) {
  std::unique_ptr<const Arm64InstructionSetFeatures> features(
      Arm64InstructionSetFeatures::FromCppDefines());
  ArenaPool pool;
  ArenaAllocator allocator(&pool);
  HGraph* graph = CreateGraph(&allocator);
  arm64::CodeGeneratorARM64 codegen(graph, *features.get(), CompilerOptions());

  codegen.Initialize();

  // The following ParallelMove used to fail this assertion:
  //
  //   Assertion failed (!available->IsEmpty())
  //
  // in vixl::aarch64::UseScratchRegisterScope::AcquireNextAvailable.
  HParallelMove* move = new (graph->GetArena()) HParallelMove(graph->GetArena());
  move->AddMove(Location::DoubleStackSlot(0),
                Location::DoubleStackSlot(257),
                Primitive::kPrimDouble,
                nullptr);
  move->AddMove(Location::DoubleStackSlot(257),
                Location::DoubleStackSlot(0),
                Primitive::kPrimDouble,
                nullptr);
  codegen.GetMoveResolver()->EmitNativeCode(move);

  InternalCodeAllocator code_allocator;
  codegen.Finalize(&code_allocator);
}

// Check that ParallelMoveResolver works fine for ARM64 for both cases when SIMD is on and off.
TEST_F(CodegenTest, ARM64ParallelMoveResolverSIMD) {
  std::unique_ptr<const Arm64InstructionSetFeatures> features(
      Arm64InstructionSetFeatures::FromCppDefines());
  ArenaPool pool;
  ArenaAllocator allocator(&pool);
  HGraph* graph = CreateGraph(&allocator);
  arm64::CodeGeneratorARM64 codegen(graph, *features.get(), CompilerOptions());

  codegen.Initialize();

  graph->SetHasSIMD(true);
  for (int i = 0; i < 2; i++) {
    HParallelMove* move = new (graph->GetArena()) HParallelMove(graph->GetArena());
    move->AddMove(Location::SIMDStackSlot(0),
                  Location::SIMDStackSlot(257),
                  Primitive::kPrimDouble,
                  nullptr);
    move->AddMove(Location::SIMDStackSlot(257),
                  Location::SIMDStackSlot(0),
                  Primitive::kPrimDouble,
                  nullptr);
    move->AddMove(Location::FpuRegisterLocation(0),
                  Location::FpuRegisterLocation(1),
                  Primitive::kPrimDouble,
                  nullptr);
    move->AddMove(Location::FpuRegisterLocation(1),
                  Location::FpuRegisterLocation(0),
                  Primitive::kPrimDouble,
                  nullptr);
    codegen.GetMoveResolver()->EmitNativeCode(move);
    graph->SetHasSIMD(false);
  }

  InternalCodeAllocator code_allocator;
  codegen.Finalize(&code_allocator);
}

#endif

#ifdef ART_ENABLE_CODEGEN_mips
TEST_F(CodegenTest, MipsClobberRA) {
  std::unique_ptr<const MipsInstructionSetFeatures> features_mips(
      MipsInstructionSetFeatures::FromCppDefines());
  if (!CanExecute(kMips) || features_mips->IsR6()) {
    // HMipsComputeBaseMethodAddress and the NAL instruction behind it
    // should only be generated on non-R6.
    return;
  }

  ArenaPool pool;
  ArenaAllocator allocator(&pool);
  HGraph* graph = CreateGraph(&allocator);

  HBasicBlock* entry_block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(entry_block);
  graph->SetEntryBlock(entry_block);
  entry_block->AddInstruction(new (&allocator) HGoto());

  HBasicBlock* block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(block);

  HBasicBlock* exit_block = new (&allocator) HBasicBlock(graph);
  graph->AddBlock(exit_block);
  graph->SetExitBlock(exit_block);
  exit_block->AddInstruction(new (&allocator) HExit());

  entry_block->AddSuccessor(block);
  block->AddSuccessor(exit_block);

  // To simplify matters, don't create PC-relative HLoadClass or HLoadString.
  // Instead, generate HMipsComputeBaseMethodAddress directly.
  HMipsComputeBaseMethodAddress* base = new (&allocator) HMipsComputeBaseMethodAddress();
  block->AddInstruction(base);
  // HMipsComputeBaseMethodAddress is defined as int, so just make the
  // compiled method return it.
  block->AddInstruction(new (&allocator) HReturn(base));

  graph->BuildDominatorTree();

  mips::CodeGeneratorMIPS codegenMIPS(graph, *features_mips.get(), CompilerOptions());
  // Since there isn't HLoadClass or HLoadString, we need to manually indicate
  // that RA is clobbered and the method entry code should generate a stack frame
  // and preserve RA in it. And this is what we're testing here.
  codegenMIPS.ClobberRA();
  // Without ClobberRA() the code would be:
  //   nal              # Sets RA to point to the jr instruction below
  //   move  v0, ra     # and the CPU falls into an infinite loop.
  //   jr    ra
  //   nop
  // The expected code is:
  //   addiu sp, sp, -16
  //   sw    ra, 12(sp)
  //   sw    a0, 0(sp)
  //   nal              # Sets RA to point to the lw instruction below.
  //   move  v0, ra
  //   lw    ra, 12(sp)
  //   jr    ra
  //   addiu sp, sp, 16
  RunCode(&codegenMIPS, graph, [](HGraph*) {}, false, 0);
}
#endif

}  // namespace art
