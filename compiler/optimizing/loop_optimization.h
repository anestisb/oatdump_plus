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

class CompilerDriver;

/**
 * Loop optimizations. Builds a loop hierarchy and applies optimizations to
 * the detected nested loops, such as removal of dead induction and empty loops
 * and inner loop vectorization.
 */
class HLoopOptimization : public HOptimization {
 public:
  HLoopOptimization(HGraph* graph,
                    CompilerDriver* compiler_driver,
                    HInductionVarAnalysis* induction_analysis);

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
    HLoopInformation* loop_info;
    LoopNode* outer;
    LoopNode* inner;
    LoopNode* previous;
    LoopNode* next;
  };

  /*
   * Vectorization restrictions (bit mask).
   */
  enum VectorRestrictions {
    kNone            = 0,    // no restrictions
    kNoMul           = 1,    // no multiplication
    kNoDiv           = 2,    // no division
    kNoShift         = 4,    // no shift
    kNoShr           = 8,    // no arithmetic shift right
    kNoHiBits        = 16,   // "wider" operations cannot bring in higher order bits
    kNoSignedHAdd    = 32,   // no signed halving add
    kNoUnroundedHAdd = 64,   // no unrounded halving add
    kNoAbs           = 128,  // no absolute value
  };

  /*
   * Vectorization mode during synthesis
   * (sequential peeling/cleanup loop or vector loop).
   */
  enum VectorMode {
    kSequential,
    kVector
  };

  /*
   * Representation of a unit-stride array reference.
   */
  struct ArrayReference {
    ArrayReference(HInstruction* b, HInstruction* o, Primitive::Type t, bool l)
        : base(b), offset(o), type(t), lhs(l) { }
    bool operator<(const ArrayReference& other) const {
      return
          (base < other.base) ||
          (base == other.base &&
           (offset < other.offset || (offset == other.offset &&
                                      (type < other.type ||
                                       (type == other.type && lhs < other.lhs)))));
    }
    HInstruction* base;    // base address
    HInstruction* offset;  // offset + i
    Primitive::Type type;  // component type
    bool lhs;              // def/use
  };

  // Loop setup and traversal.
  void LocalRun();
  void AddLoop(HLoopInformation* loop_info);
  void RemoveLoop(LoopNode* node);
  void TraverseLoopsInnerToOuter(LoopNode* node);

  // Optimization.
  void SimplifyInduction(LoopNode* node);
  void SimplifyBlocks(LoopNode* node);
  void OptimizeInnerLoop(LoopNode* node);

  // Vectorization analysis and synthesis.
  bool CanVectorize(LoopNode* node, HBasicBlock* block, int64_t trip_count);
  void Vectorize(LoopNode* node, HBasicBlock* block, HBasicBlock* exit, int64_t trip_count);
  void GenerateNewLoop(LoopNode* node,
                       HBasicBlock* block,
                       HBasicBlock* new_preheader,
                       HInstruction* lo,
                       HInstruction* hi,
                       HInstruction* step);
  bool VectorizeDef(LoopNode* node, HInstruction* instruction, bool generate_code);
  bool VectorizeUse(LoopNode* node,
                    HInstruction* instruction,
                    bool generate_code,
                    Primitive::Type type,
                    uint64_t restrictions);
  bool TrySetVectorType(Primitive::Type type, /*out*/ uint64_t* restrictions);
  bool TrySetVectorLength(uint32_t length);
  void GenerateVecInv(HInstruction* org, Primitive::Type type);
  void GenerateVecSub(HInstruction* org, HInstruction* off);
  void GenerateVecMem(HInstruction* org,
                      HInstruction* opa,
                      HInstruction* opb,
                      Primitive::Type type);
  void GenerateVecOp(HInstruction* org, HInstruction* opa, HInstruction* opb, Primitive::Type type);

  // Vectorization idioms.
  bool VectorizeHalvingAddIdiom(LoopNode* node,
                                HInstruction* instruction,
                                bool generate_code,
                                Primitive::Type type,
                                uint64_t restrictions);

  // Helpers.
  bool TrySetPhiInduction(HPhi* phi, bool restrict_uses);
  bool TrySetSimpleLoopHeader(HBasicBlock* block);
  bool IsEmptyBody(HBasicBlock* block);
  bool IsOnlyUsedAfterLoop(HLoopInformation* loop_info,
                           HInstruction* instruction,
                           bool collect_loop_uses,
                           /*out*/ int32_t* use_count);
  bool IsUsedOutsideLoop(HLoopInformation* loop_info,
                         HInstruction* instruction);
  bool TryReplaceWithLastValue(HLoopInformation* loop_info,
                               HInstruction* instruction,
                               HBasicBlock* block);
  bool TryAssignLastValue(HLoopInformation* loop_info,
                          HInstruction* instruction,
                          HBasicBlock* block,
                          bool collect_loop_uses);
  void RemoveDeadInstructions(const HInstructionList& list);
  bool CanRemoveCycle();  // Whether the current 'iset_' is removable.

  // Compiler driver (to query ISA features).
  const CompilerDriver* compiler_driver_;

  // Range information based on prior induction variable analysis.
  InductionVarRange induction_range_;

  // Phase-local heap memory allocator for the loop optimizer. Storage obtained
  // through this allocator is immediately released when the loop optimizer is done.
  ArenaAllocator* loop_allocator_;

  // Global heap memory allocator. Used to build HIR.
  ArenaAllocator* global_allocator_;

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
  uint32_t induction_simplication_count_;

  // Flag that tracks if any simplifications have occurred.
  bool simplified_;

  // Number of "lanes" for selected packed type.
  uint32_t vector_length_;

  // Set of array references in the vector loop.
  // Contents reside in phase-local heap memory.
  ArenaSet<ArrayReference>* vector_refs_;

  // Mapping used during vectorization synthesis for both the scalar peeling/cleanup
  // loop (simd_ is false) and the actual vector loop (simd_ is true). The data
  // structure maps original instructions into the new instructions.
  // Contents reside in phase-local heap memory.
  ArenaSafeMap<HInstruction*, HInstruction*>* vector_map_;

  // Temporary vectorization bookkeeping.
  HBasicBlock* vector_preheader_;  // preheader of the new loop
  HBasicBlock* vector_header_;  // header of the new loop
  HBasicBlock* vector_body_;  // body of the new loop
  HInstruction* vector_runtime_test_a_;
  HInstruction* vector_runtime_test_b_;  // defines a != b runtime test
  HPhi* vector_phi_;  // the Phi representing the normalized loop index
  VectorMode vector_mode_;  // selects synthesis mode

  friend class LoopOptimizationTest;

  DISALLOW_COPY_AND_ASSIGN(HLoopOptimization);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOOP_OPTIMIZATION_H_
