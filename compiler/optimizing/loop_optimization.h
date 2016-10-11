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

#ifndef ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_
#define ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_

#include "induction_var_range.h"
#include "nodes.h"
#include "optimization.h"

namespace art {

/**
 * Loop optimizations. Builds a loop hierarchy and applies optimizations to
 * the detected nested loops, such as removal of dead induction and empty loops.
 */
class HLoopOptimization : public HOptimization {
 public:
  HLoopOptimization(HGraph* graph, HInductionVarAnalysis* induction_analysis);

  void Run() OVERRIDE;

  static constexpr const char* kLoopOptimizationPassName = "loop_optimization";

 private:
  /**
   * A single loop inside the loop hierarchy representation.
   */
  struct LoopNode : public ArenaObject<kArenaAllocLoopOptimization> {
    explicit LoopNode(HLoopInformation* lp_info)
        : loop_info(lp_info),
          outer(nullptr),
          inner(nullptr),
          previous(nullptr),
          next(nullptr) {}
    HLoopInformation* const loop_info;
    LoopNode* outer;
    LoopNode* inner;
    LoopNode* previous;
    LoopNode* next;
  };

  void LocalRun();

  void AddLoop(HLoopInformation* loop_info);
  void RemoveLoop(LoopNode* node);

  void TraverseLoopsInnerToOuter(LoopNode* node);

  void SimplifyInduction(LoopNode* node);
  void SimplifyBlocks(LoopNode* node);
  void RemoveIfEmptyLoop(LoopNode* node);

  bool IsOnlyUsedAfterLoop(HLoopInformation* loop_info,
                           HInstruction* instruction,
                           /*out*/ int32_t* use_count);
  void ReplaceAllUses(HInstruction* instruction, HInstruction* replacement);
  bool TryReplaceWithLastValue(HInstruction* instruction,
                               int32_t use_count,
                               HBasicBlock* block);

  // Range information based on prior induction variable analysis.
  InductionVarRange induction_range_;

  // Phase-local heap memory allocator for the loop optimizer. Storage obtained
  // through this allocator is immediately released when the loop optimizer is done.
  ArenaAllocator* loop_allocator_;

  // Entries into the loop hierarchy representation. The hierarchy resides
  // in phase-local heap memory.
  LoopNode* top_loop_;
  LoopNode* last_loop_;

  // Temporary bookkeeping of a set of instructions.
  // Contents reside in phase-local heap memory.
  ArenaSet<HInstruction*>* iset_;

  // Counter that tracks how many induction cycles have been simplified. Useful
  // to trigger incremental updates of induction variable analysis of outer loops
  // when the induction of inner loops has changed.
  int32_t induction_simplication_count_;

  friend class LoopOptimizationTest;

  DISALLOW_COPY_AND_ASSIGN(HLoopOptimization);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_
