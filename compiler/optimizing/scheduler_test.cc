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

#include "base/arena_allocator.h"
#include "builder.h"
#include "codegen_test_utils.h"
#include "common_compiler_test.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "pc_relative_fixups_x86.h"
#include "register_allocator.h"
#include "scheduler.h"

#ifdef ART_ENABLE_CODEGEN_arm64
#include "scheduler_arm64.h"
#endif

#ifdef ART_ENABLE_CODEGEN_arm
#include "scheduler_arm.h"
#endif

namespace art {

// Return all combinations of ISA and code generator that are executable on
// hardware, or on simulator, and that we'd like to test.
static ::std::vector<CodegenTargetConfig> GetTargetConfigs() {
  ::std::vector<CodegenTargetConfig> v;
  ::std::vector<CodegenTargetConfig> test_config_candidates = {
#ifdef ART_ENABLE_CODEGEN_arm
    CodegenTargetConfig(kArm, create_codegen_arm),
    CodegenTargetConfig(kThumb2, create_codegen_arm),
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

class SchedulerTest : public CommonCompilerTest {
 public:
  SchedulerTest() : pool_(), allocator_(&pool_) {
    graph_ = CreateGraph(&allocator_);
  }

  // Build scheduling graph, and run target specific scheduling on it.
  void TestBuildDependencyGraphAndSchedule(HScheduler* scheduler) {
    HBasicBlock* entry = new (&allocator_) HBasicBlock(graph_);
    HBasicBlock* block1 = new (&allocator_) HBasicBlock(graph_);
    graph_->AddBlock(entry);
    graph_->AddBlock(block1);
    graph_->SetEntryBlock(entry);

    // entry:
    // array         ParameterValue
    // c1            IntConstant
    // c2            IntConstant
    // block1:
    // add1          Add [c1, c2]
    // add2          Add [add1, c2]
    // mul           Mul [add1, add2]
    // div_check     DivZeroCheck [add2] (env: add2, mul)
    // div           Div [add1, div_check]
    // array_get1    ArrayGet [array, add1]
    // array_set1    ArraySet [array, add1, add2]
    // array_get2    ArrayGet [array, add1]
    // array_set2    ArraySet [array, add1, add2]

    HInstruction* array = new (&allocator_) HParameterValue(graph_->GetDexFile(),
                                                            dex::TypeIndex(0),
                                                            0,
                                                            Primitive::kPrimNot);
    HInstruction* c1 = graph_->GetIntConstant(1);
    HInstruction* c2 = graph_->GetIntConstant(10);
    HInstruction* add1 = new (&allocator_) HAdd(Primitive::kPrimInt, c1, c2);
    HInstruction* add2 = new (&allocator_) HAdd(Primitive::kPrimInt, add1, c2);
    HInstruction* mul = new (&allocator_) HMul(Primitive::kPrimInt, add1, add2);
    HInstruction* div_check = new (&allocator_) HDivZeroCheck(add2, 0);
    HInstruction* div = new (&allocator_) HDiv(Primitive::kPrimInt, add1, div_check, 0);
    HInstruction* array_get1 = new (&allocator_) HArrayGet(array, add1, Primitive::kPrimInt, 0);
    HInstruction* array_set1 = new (&allocator_) HArraySet(array, add1, add2, Primitive::kPrimInt, 0);
    HInstruction* array_get2 = new (&allocator_) HArrayGet(array, add1, Primitive::kPrimInt, 0);
    HInstruction* array_set2 = new (&allocator_) HArraySet(array, add1, add2, Primitive::kPrimInt, 0);

    DCHECK(div_check->CanThrow());

    entry->AddInstruction(array);

    HInstruction* block_instructions[] = {add1,
                                          add2,
                                          mul,
                                          div_check,
                                          div,
                                          array_get1,
                                          array_set1,
                                          array_get2,
                                          array_set2};
    for (auto instr : block_instructions) {
      block1->AddInstruction(instr);
    }

    HEnvironment* environment = new (&allocator_) HEnvironment(&allocator_,
                                                               2,
                                                               graph_->GetArtMethod(),
                                                               0,
                                                               div_check);
    div_check->SetRawEnvironment(environment);
    environment->SetRawEnvAt(0, add2);
    add2->AddEnvUseAt(div_check->GetEnvironment(), 0);
    environment->SetRawEnvAt(1, mul);
    mul->AddEnvUseAt(div_check->GetEnvironment(), 1);

    SchedulingGraph scheduling_graph(scheduler, graph_->GetArena());
    // Instructions must be inserted in reverse order into the scheduling graph.
    for (auto instr : ReverseRange(block_instructions)) {
      scheduling_graph.AddNode(instr);
    }

    // Should not have dependencies cross basic blocks.
    ASSERT_FALSE(scheduling_graph.HasImmediateDataDependency(add1, c1));
    ASSERT_FALSE(scheduling_graph.HasImmediateDataDependency(add2, c2));

    // Define-use dependency.
    ASSERT_TRUE(scheduling_graph.HasImmediateDataDependency(add2, add1));
    ASSERT_FALSE(scheduling_graph.HasImmediateDataDependency(add1, add2));
    ASSERT_TRUE(scheduling_graph.HasImmediateDataDependency(div_check, add2));
    ASSERT_FALSE(scheduling_graph.HasImmediateDataDependency(div_check, add1));
    ASSERT_TRUE(scheduling_graph.HasImmediateDataDependency(div, div_check));
    ASSERT_TRUE(scheduling_graph.HasImmediateDataDependency(array_set1, add1));
    ASSERT_TRUE(scheduling_graph.HasImmediateDataDependency(array_set1, add2));

    // Read and write dependencies
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(array_set1, array_get1));
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(array_set2, array_get2));
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(array_get2, array_set1));
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(array_set2, array_set1));

    // Env dependency.
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(div_check, mul));
    ASSERT_FALSE(scheduling_graph.HasImmediateOtherDependency(mul, div_check));

    // CanThrow.
    ASSERT_TRUE(scheduling_graph.HasImmediateOtherDependency(array_set1, div_check));

    // Exercise the code path of target specific scheduler and SchedulingLatencyVisitor.
    scheduler->Schedule(graph_);
  }

  void CompileWithRandomSchedulerAndRun(const uint16_t* data, bool has_result, int expected) {
    for (CodegenTargetConfig target_config : GetTargetConfigs()) {
      HGraph* graph = CreateCFG(&allocator_, data);

      // Schedule the graph randomly.
      HInstructionScheduling scheduling(graph, target_config.GetInstructionSet());
      scheduling.Run(/*only_optimize_loop_blocks*/ false, /*schedule_randomly*/ true);

      RunCode(target_config,
              graph,
              [](HGraph* graph_arg) { RemoveSuspendChecks(graph_arg); },
              has_result, expected);
    }
  }

  ArenaPool pool_;
  ArenaAllocator allocator_;
  HGraph* graph_;
};

#if defined(ART_ENABLE_CODEGEN_arm64)
TEST_F(SchedulerTest, DependencyGraphAndSchedulerARM64) {
  CriticalPathSchedulingNodeSelector critical_path_selector;
  arm64::HSchedulerARM64 scheduler(&allocator_, &critical_path_selector);
  TestBuildDependencyGraphAndSchedule(&scheduler);
}
#endif

#if defined(ART_ENABLE_CODEGEN_arm)
TEST_F(SchedulerTest, DependencyGrapAndSchedulerARM) {
  CriticalPathSchedulingNodeSelector critical_path_selector;
  arm::SchedulingLatencyVisitorARM arm_latency_visitor(/*CodeGenerator*/ nullptr);
  arm::HSchedulerARM scheduler(&allocator_, &critical_path_selector, &arm_latency_visitor);
  TestBuildDependencyGraphAndSchedule(&scheduler);
}
#endif

TEST_F(SchedulerTest, RandomScheduling) {
  //
  // Java source: crafted code to make sure (random) scheduling should get correct result.
  //
  //  int result = 0;
  //  float fr = 10.0f;
  //  for (int i = 1; i < 10; i++) {
  //    fr ++;
  //    int t1 = result >> i;
  //    int t2 = result * i;
  //    result = result + t1 - t2;
  //    fr = fr / i;
  //    result += (int)fr;
  //  }
  //  return result;
  //
  const uint16_t data[] = SIX_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 << 12 | 2 << 8,          // const/4 v2, #int 0
    Instruction::CONST_HIGH16 | 0 << 8, 0x4120,       // const/high16 v0, #float 10.0 // #41200000
    Instruction::CONST_4 | 1 << 12 | 1 << 8,          // const/4 v1, #int 1
    Instruction::CONST_16 | 5 << 8, 0x000a,           // const/16 v5, #int 10
    Instruction::IF_GE | 5 << 12 | 1 << 8, 0x0014,    // if-ge v1, v5, 001a // +0014
    Instruction::CONST_HIGH16 | 5 << 8, 0x3f80,       // const/high16 v5, #float 1.0 // #3f800000
    Instruction::ADD_FLOAT_2ADDR | 5 << 12 | 0 << 8,  // add-float/2addr v0, v5
    Instruction::SHR_INT | 3 << 8, 1 << 8 | 2 ,       // shr-int v3, v2, v1
    Instruction::MUL_INT | 4 << 8, 1 << 8 | 2,        // mul-int v4, v2, v1
    Instruction::ADD_INT | 5 << 8, 3 << 8 | 2,        // add-int v5, v2, v3
    Instruction::SUB_INT | 2 << 8, 4 << 8 | 5,        // sub-int v2, v5, v4
    Instruction::INT_TO_FLOAT | 1 << 12 | 5 << 8,     // int-to-float v5, v1
    Instruction::DIV_FLOAT_2ADDR | 5 << 12 | 0 << 8,  // div-float/2addr v0, v5
    Instruction::FLOAT_TO_INT | 0 << 12 | 5 << 8,     // float-to-int v5, v0
    Instruction::ADD_INT_2ADDR | 5 << 12 | 2 << 8,    // add-int/2addr v2, v5
    Instruction::ADD_INT_LIT8 | 1 << 8, 1 << 8 | 1,   // add-int/lit8 v1, v1, #int 1 // #01
    Instruction::GOTO | 0xeb << 8,                    // goto 0004 // -0015
    Instruction::RETURN | 2 << 8);                    // return v2

  constexpr int kNumberOfRuns = 10;
  for (int i = 0; i < kNumberOfRuns; ++i) {
    CompileWithRandomSchedulerAndRun(data, true, 138774);
  }
}

}  // namespace art
