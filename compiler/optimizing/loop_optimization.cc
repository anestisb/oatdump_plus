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

#include "loop_optimization.h"

#include "arch/instruction_set.h"
#include "arch/arm/instruction_set_features_arm.h"
#include "arch/arm64/instruction_set_features_arm64.h"
#include "arch/mips/instruction_set_features_mips.h"
#include "arch/mips64/instruction_set_features_mips64.h"
#include "arch/x86/instruction_set_features_x86.h"
#include "arch/x86_64/instruction_set_features_x86_64.h"
#include "driver/compiler_driver.h"
#include "linear_order.h"

namespace art {

// Enables vectorization (SIMDization) in the loop optimizer.
static constexpr bool kEnableVectorization = true;

// Remove the instruction from the graph. A bit more elaborate than the usual
// instruction removal, since there may be a cycle in the use structure.
static void RemoveFromCycle(HInstruction* instruction) {
  instruction->RemoveAsUserOfAllInputs();
  instruction->RemoveEnvironmentUsers();
  instruction->GetBlock()->RemoveInstructionOrPhi(instruction, /*ensure_safety=*/ false);
}

// Detect a goto block and sets succ to the single successor.
static bool IsGotoBlock(HBasicBlock* block, /*out*/ HBasicBlock** succ) {
  if (block->GetPredecessors().size() == 1 &&
      block->GetSuccessors().size() == 1 &&
      block->IsSingleGoto()) {
    *succ = block->GetSingleSuccessor();
    return true;
  }
  return false;
}

// Detect an early exit loop.
static bool IsEarlyExit(HLoopInformation* loop_info) {
  HBlocksInLoopReversePostOrderIterator it_loop(*loop_info);
  for (it_loop.Advance(); !it_loop.Done(); it_loop.Advance()) {
    for (HBasicBlock* successor : it_loop.Current()->GetSuccessors()) {
      if (!loop_info->Contains(*successor)) {
        return true;
      }
    }
  }
  return false;
}

// Detect a sign extension from the given type. Returns the promoted operand on success.
static bool IsSignExtensionAndGet(HInstruction* instruction,
                                  Primitive::Type type,
                                  /*out*/ HInstruction** operand) {
  // Accept any already wider constant that would be handled properly by sign
  // extension when represented in the *width* of the given narrower data type
  // (the fact that char normally zero extends does not matter here).
  int64_t value = 0;
  if (IsInt64AndGet(instruction, &value)) {
    switch (type) {
      case Primitive::kPrimByte:
        if (std::numeric_limits<int8_t>::min() <= value &&
            std::numeric_limits<int8_t>::max() >= value) {
          *operand = instruction;
          return true;
        }
        return false;
      case Primitive::kPrimChar:
      case Primitive::kPrimShort:
        if (std::numeric_limits<int16_t>::min() <= value &&
            std::numeric_limits<int16_t>::max() <= value) {
          *operand = instruction;
          return true;
        }
        return false;
      default:
        return false;
    }
  }
  // An implicit widening conversion of a signed integer to an integral type sign-extends
  // the two's-complement representation of the integer value to fill the wider format.
  if (instruction->GetType() == type && (instruction->IsArrayGet() ||
                                         instruction->IsStaticFieldGet() ||
                                         instruction->IsInstanceFieldGet())) {
    switch (type) {
      case Primitive::kPrimByte:
      case Primitive::kPrimShort:
        *operand = instruction;
        return true;
      default:
        return false;
    }
  }
  // TODO: perhaps explicit conversions later too?
  //       (this may return something different from instruction)
  return false;
}

// Detect a zero extension from the given type. Returns the promoted operand on success.
static bool IsZeroExtensionAndGet(HInstruction* instruction,
                                  Primitive::Type type,
                                  /*out*/ HInstruction** operand) {
  // Accept any already wider constant that would be handled properly by zero
  // extension when represented in the *width* of the given narrower data type
  // (the fact that byte/short normally sign extend does not matter here).
  int64_t value = 0;
  if (IsInt64AndGet(instruction, &value)) {
    switch (type) {
      case Primitive::kPrimByte:
        if (std::numeric_limits<uint8_t>::min() <= value &&
            std::numeric_limits<uint8_t>::max() >= value) {
          *operand = instruction;
          return true;
        }
        return false;
      case Primitive::kPrimChar:
      case Primitive::kPrimShort:
        if (std::numeric_limits<uint16_t>::min() <= value &&
            std::numeric_limits<uint16_t>::max() <= value) {
          *operand = instruction;
          return true;
        }
        return false;
      default:
        return false;
    }
  }
  // An implicit widening conversion of a char to an integral type zero-extends
  // the representation of the char value to fill the wider format.
  if (instruction->GetType() == type && (instruction->IsArrayGet() ||
                                         instruction->IsStaticFieldGet() ||
                                         instruction->IsInstanceFieldGet())) {
    if (type == Primitive::kPrimChar) {
      *operand = instruction;
      return true;
    }
  }
  // A sign (or zero) extension followed by an explicit removal of just the
  // higher sign bits is equivalent to a zero extension of the underlying operand.
  if (instruction->IsAnd()) {
    int64_t mask = 0;
    HInstruction* a = instruction->InputAt(0);
    HInstruction* b = instruction->InputAt(1);
    // In (a & b) find (mask & b) or (a & mask) with sign or zero extension on the non-mask.
    if ((IsInt64AndGet(a, /*out*/ &mask) && (IsSignExtensionAndGet(b, type, /*out*/ operand) ||
                                             IsZeroExtensionAndGet(b, type, /*out*/ operand))) ||
        (IsInt64AndGet(b, /*out*/ &mask) && (IsSignExtensionAndGet(a, type, /*out*/ operand) ||
                                             IsZeroExtensionAndGet(a, type, /*out*/ operand)))) {
      switch ((*operand)->GetType()) {
        case Primitive::kPrimByte:  return mask == std::numeric_limits<uint8_t>::max();
        case Primitive::kPrimChar:
        case Primitive::kPrimShort: return mask == std::numeric_limits<uint16_t>::max();
        default: return false;
      }
    }
  }
  // TODO: perhaps explicit conversions later too?
  return false;
}

// Test vector restrictions.
static bool HasVectorRestrictions(uint64_t restrictions, uint64_t tested) {
  return (restrictions & tested) != 0;
}

// Insert an instruction.
static HInstruction* Insert(HBasicBlock* block, HInstruction* instruction) {
  DCHECK(block != nullptr);
  DCHECK(instruction != nullptr);
  block->InsertInstructionBefore(instruction, block->GetLastInstruction());
  return instruction;
}

//
// Class methods.
//

HLoopOptimization::HLoopOptimization(HGraph* graph,
                                     CompilerDriver* compiler_driver,
                                     HInductionVarAnalysis* induction_analysis)
    : HOptimization(graph, kLoopOptimizationPassName),
      compiler_driver_(compiler_driver),
      induction_range_(induction_analysis),
      loop_allocator_(nullptr),
      global_allocator_(graph_->GetArena()),
      top_loop_(nullptr),
      last_loop_(nullptr),
      iset_(nullptr),
      induction_simplication_count_(0),
      simplified_(false),
      vector_length_(0),
      vector_refs_(nullptr),
      vector_map_(nullptr) {
}

void HLoopOptimization::Run() {
  // Skip if there is no loop or the graph has try-catch/irreducible loops.
  // TODO: make this less of a sledgehammer.
  if (!graph_->HasLoops() || graph_->HasTryCatch() || graph_->HasIrreducibleLoops()) {
    return;
  }

  // Phase-local allocator that draws from the global pool. Since the allocator
  // itself resides on the stack, it is destructed on exiting Run(), which
  // implies its underlying memory is released immediately.
  ArenaAllocator allocator(global_allocator_->GetArenaPool());
  loop_allocator_ = &allocator;

  // Perform loop optimizations.
  LocalRun();
  if (top_loop_ == nullptr) {
    graph_->SetHasLoops(false);  // no more loops
  }

  // Detach.
  loop_allocator_ = nullptr;
  last_loop_ = top_loop_ = nullptr;
}

void HLoopOptimization::LocalRun() {
  // Build the linear order using the phase-local allocator. This step enables building
  // a loop hierarchy that properly reflects the outer-inner and previous-next relation.
  ArenaVector<HBasicBlock*> linear_order(loop_allocator_->Adapter(kArenaAllocLinearOrder));
  LinearizeGraph(graph_, loop_allocator_, &linear_order);

  // Build the loop hierarchy.
  for (HBasicBlock* block : linear_order) {
    if (block->IsLoopHeader()) {
      AddLoop(block->GetLoopInformation());
    }
  }

  // Traverse the loop hierarchy inner-to-outer and optimize. Traversal can use
  // temporary data structures using the phase-local allocator. All new HIR
  // should use the global allocator.
  if (top_loop_ != nullptr) {
    ArenaSet<HInstruction*> iset(loop_allocator_->Adapter(kArenaAllocLoopOptimization));
    ArenaSet<ArrayReference> refs(loop_allocator_->Adapter(kArenaAllocLoopOptimization));
    ArenaSafeMap<HInstruction*, HInstruction*> map(
        std::less<HInstruction*>(), loop_allocator_->Adapter(kArenaAllocLoopOptimization));
    // Attach.
    iset_ = &iset;
    vector_refs_ = &refs;
    vector_map_ = &map;
    // Traverse.
    TraverseLoopsInnerToOuter(top_loop_);
    // Detach.
    iset_ = nullptr;
    vector_refs_ = nullptr;
    vector_map_ = nullptr;
  }
}

void HLoopOptimization::AddLoop(HLoopInformation* loop_info) {
  DCHECK(loop_info != nullptr);
  LoopNode* node = new (loop_allocator_) LoopNode(loop_info);
  if (last_loop_ == nullptr) {
    // First loop.
    DCHECK(top_loop_ == nullptr);
    last_loop_ = top_loop_ = node;
  } else if (loop_info->IsIn(*last_loop_->loop_info)) {
    // Inner loop.
    node->outer = last_loop_;
    DCHECK(last_loop_->inner == nullptr);
    last_loop_ = last_loop_->inner = node;
  } else {
    // Subsequent loop.
    while (last_loop_->outer != nullptr && !loop_info->IsIn(*last_loop_->outer->loop_info)) {
      last_loop_ = last_loop_->outer;
    }
    node->outer = last_loop_->outer;
    node->previous = last_loop_;
    DCHECK(last_loop_->next == nullptr);
    last_loop_ = last_loop_->next = node;
  }
}

void HLoopOptimization::RemoveLoop(LoopNode* node) {
  DCHECK(node != nullptr);
  DCHECK(node->inner == nullptr);
  if (node->previous != nullptr) {
    // Within sequence.
    node->previous->next = node->next;
    if (node->next != nullptr) {
      node->next->previous = node->previous;
    }
  } else {
    // First of sequence.
    if (node->outer != nullptr) {
      node->outer->inner = node->next;
    } else {
      top_loop_ = node->next;
    }
    if (node->next != nullptr) {
      node->next->outer = node->outer;
      node->next->previous = nullptr;
    }
  }
}

void HLoopOptimization::TraverseLoopsInnerToOuter(LoopNode* node) {
  for ( ; node != nullptr; node = node->next) {
    // Visit inner loops first.
    uint32_t current_induction_simplification_count = induction_simplication_count_;
    if (node->inner != nullptr) {
      TraverseLoopsInnerToOuter(node->inner);
    }
    // Recompute induction information of this loop if the induction
    // of any inner loop has been simplified.
    if (current_induction_simplification_count != induction_simplication_count_) {
      induction_range_.ReVisit(node->loop_info);
    }
    // Repeat simplifications in the loop-body until no more changes occur.
    // Note that since each simplification consists of eliminating code (without
    // introducing new code), this process is always finite.
    do {
      simplified_ = false;
      SimplifyInduction(node);
      SimplifyBlocks(node);
    } while (simplified_);
    // Optimize inner loop.
    if (node->inner == nullptr) {
      OptimizeInnerLoop(node);
    }
  }
}

//
// Optimization.
//

bool HLoopOptimization::CanRemoveCycle() {
  for (HInstruction* i : *iset_) {
    // We can never remove instructions that have environment
    // uses when we compile 'debuggable'.
    if (i->HasEnvironmentUses() && graph_->IsDebuggable()) {
      return false;
    }
    // A deoptimization should never have an environment input removed.
    for (const HUseListNode<HEnvironment*>& use : i->GetEnvUses()) {
      if (use.GetUser()->GetHolder()->IsDeoptimize()) {
        return false;
      }
    }
  }
  return true;
}

void HLoopOptimization::SimplifyInduction(LoopNode* node) {
  HBasicBlock* header = node->loop_info->GetHeader();
  HBasicBlock* preheader = node->loop_info->GetPreHeader();
  // Scan the phis in the header to find opportunities to simplify an induction
  // cycle that is only used outside the loop. Replace these uses, if any, with
  // the last value and remove the induction cycle.
  // Examples: for (int i = 0; x != null;   i++) { .... no i .... }
  //           for (int i = 0; i < 10; i++, k++) { .... no k .... } return k;
  for (HInstructionIterator it(header->GetPhis()); !it.Done(); it.Advance()) {
    HPhi* phi = it.Current()->AsPhi();
    iset_->clear();  // prepare phi induction
    if (TrySetPhiInduction(phi, /*restrict_uses*/ true) &&
        TryAssignLastValue(node->loop_info, phi, preheader, /*collect_loop_uses*/ false)) {
      // Note that it's ok to have replaced uses after the loop with the last value, without
      // being able to remove the cycle. Environment uses (which are the reason we may not be
      // able to remove the cycle) within the loop will still hold the right value.
      if (CanRemoveCycle()) {
        for (HInstruction* i : *iset_) {
          RemoveFromCycle(i);
        }
        simplified_ = true;
      }
    }
  }
}

void HLoopOptimization::SimplifyBlocks(LoopNode* node) {
  // Iterate over all basic blocks in the loop-body.
  for (HBlocksInLoopIterator it(*node->loop_info); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();
    // Remove dead instructions from the loop-body.
    RemoveDeadInstructions(block->GetPhis());
    RemoveDeadInstructions(block->GetInstructions());
    // Remove trivial control flow blocks from the loop-body.
    if (block->GetPredecessors().size() == 1 &&
        block->GetSuccessors().size() == 1 &&
        block->GetSingleSuccessor()->GetPredecessors().size() == 1) {
      simplified_ = true;
      block->MergeWith(block->GetSingleSuccessor());
    } else if (block->GetSuccessors().size() == 2) {
      // Trivial if block can be bypassed to either branch.
      HBasicBlock* succ0 = block->GetSuccessors()[0];
      HBasicBlock* succ1 = block->GetSuccessors()[1];
      HBasicBlock* meet0 = nullptr;
      HBasicBlock* meet1 = nullptr;
      if (succ0 != succ1 &&
          IsGotoBlock(succ0, &meet0) &&
          IsGotoBlock(succ1, &meet1) &&
          meet0 == meet1 &&  // meets again
          meet0 != block &&  // no self-loop
          meet0->GetPhis().IsEmpty()) {  // not used for merging
        simplified_ = true;
        succ0->DisconnectAndDelete();
        if (block->Dominates(meet0)) {
          block->RemoveDominatedBlock(meet0);
          succ1->AddDominatedBlock(meet0);
          meet0->SetDominator(succ1);
        }
      }
    }
  }
}

void HLoopOptimization::OptimizeInnerLoop(LoopNode* node) {
  HBasicBlock* header = node->loop_info->GetHeader();
  HBasicBlock* preheader = node->loop_info->GetPreHeader();
  // Ensure loop header logic is finite.
  int64_t trip_count = 0;
  if (!induction_range_.IsFinite(node->loop_info, &trip_count)) {
    return;
  }

  // Ensure there is only a single loop-body (besides the header).
  HBasicBlock* body = nullptr;
  for (HBlocksInLoopIterator it(*node->loop_info); !it.Done(); it.Advance()) {
    if (it.Current() != header) {
      if (body != nullptr) {
        return;
      }
      body = it.Current();
    }
  }
  // Ensure there is only a single exit point.
  if (header->GetSuccessors().size() != 2) {
    return;
  }
  HBasicBlock* exit = (header->GetSuccessors()[0] == body)
      ? header->GetSuccessors()[1]
      : header->GetSuccessors()[0];
  // Ensure exit can only be reached by exiting loop.
  if (exit->GetPredecessors().size() != 1) {
    return;
  }
  // Detect either an empty loop (no side effects other than plain iteration) or
  // a trivial loop (just iterating once). Replace subsequent index uses, if any,
  // with the last value and remove the loop, possibly after unrolling its body.
  HInstruction* phi = header->GetFirstPhi();
  iset_->clear();  // prepare phi induction
  if (TrySetSimpleLoopHeader(header)) {
    bool is_empty = IsEmptyBody(body);
    if ((is_empty || trip_count == 1) &&
        TryAssignLastValue(node->loop_info, phi, preheader, /*collect_loop_uses*/ true)) {
      if (!is_empty) {
        // Unroll the loop-body, which sees initial value of the index.
        phi->ReplaceWith(phi->InputAt(0));
        preheader->MergeInstructionsWith(body);
      }
      body->DisconnectAndDelete();
      exit->RemovePredecessor(header);
      header->RemoveSuccessor(exit);
      header->RemoveDominatedBlock(exit);
      header->DisconnectAndDelete();
      preheader->AddSuccessor(exit);
      preheader->AddInstruction(new (global_allocator_) HGoto());
      preheader->AddDominatedBlock(exit);
      exit->SetDominator(preheader);
      RemoveLoop(node);  // update hierarchy
      return;
    }
  }

  // Vectorize loop, if possible and valid.
  if (kEnableVectorization) {
    iset_->clear();  // prepare phi induction
    if (TrySetSimpleLoopHeader(header) &&
        CanVectorize(node, body, trip_count) &&
        TryAssignLastValue(node->loop_info, phi, preheader, /*collect_loop_uses*/ true)) {
      Vectorize(node, body, exit, trip_count);
      graph_->SetHasSIMD(true);  // flag SIMD usage
      return;
    }
  }
}

//
// Loop vectorization. The implementation is based on the book by Aart J.C. Bik:
// "The Software Vectorization Handbook. Applying Multimedia Extensions for Maximum Performance."
// Intel Press, June, 2004 (http://www.aartbik.com/).
//

bool HLoopOptimization::CanVectorize(LoopNode* node, HBasicBlock* block, int64_t trip_count) {
  // Reset vector bookkeeping.
  vector_length_ = 0;
  vector_refs_->clear();
  vector_runtime_test_a_ =
  vector_runtime_test_b_= nullptr;

  // Phis in the loop-body prevent vectorization.
  if (!block->GetPhis().IsEmpty()) {
    return false;
  }

  // Scan the loop-body, starting a right-hand-side tree traversal at each left-hand-side
  // occurrence, which allows passing down attributes down the use tree.
  for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
    if (!VectorizeDef(node, it.Current(), /*generate_code*/ false)) {
      return false;  // failure to vectorize a left-hand-side
    }
  }

  // Heuristics. Does vectorization seem profitable?
  // TODO: refine
  if (vector_length_ == 0) {
    return false;  // nothing found
  } else if (0 < trip_count && trip_count < vector_length_) {
    return false;  // insufficient iterations
  }

  // Data dependence analysis. Find each pair of references with same type, where
  // at least one is a write. Each such pair denotes a possible data dependence.
  // This analysis exploits the property that differently typed arrays cannot be
  // aliased, as well as the property that references either point to the same
  // array or to two completely disjoint arrays, i.e., no partial aliasing.
  // Other than a few simply heuristics, no detailed subscript analysis is done.
  for (auto i = vector_refs_->begin(); i != vector_refs_->end(); ++i) {
    for (auto j = i; ++j != vector_refs_->end(); ) {
      if (i->type == j->type && (i->lhs || j->lhs)) {
        // Found same-typed a[i+x] vs. b[i+y], where at least one is a write.
        HInstruction* a = i->base;
        HInstruction* b = j->base;
        HInstruction* x = i->offset;
        HInstruction* y = j->offset;
        if (a == b) {
          // Found a[i+x] vs. a[i+y]. Accept if x == y (loop-independent data dependence).
          // Conservatively assume a loop-carried data dependence otherwise, and reject.
          if (x != y) {
            return false;
          }
        } else {
          // Found a[i+x] vs. b[i+y]. Accept if x == y (at worst loop-independent data dependence).
          // Conservatively assume a potential loop-carried data dependence otherwise, avoided by
          // generating an explicit a != b disambiguation runtime test on the two references.
          if (x != y) {
            // For now, we reject after one test to avoid excessive overhead.
            if (vector_runtime_test_a_ != nullptr) {
              return false;
            }
            vector_runtime_test_a_ = a;
            vector_runtime_test_b_ = b;
          }
        }
      }
    }
  }

  // Success!
  return true;
}

void HLoopOptimization::Vectorize(LoopNode* node,
                                  HBasicBlock* block,
                                  HBasicBlock* exit,
                                  int64_t trip_count) {
  Primitive::Type induc_type = Primitive::kPrimInt;
  HBasicBlock* header = node->loop_info->GetHeader();
  HBasicBlock* preheader = node->loop_info->GetPreHeader();

  // A cleanup is needed for any unknown trip count or for a known trip count
  // with remainder iterations after vectorization.
  bool needs_cleanup = trip_count == 0 || (trip_count % vector_length_) != 0;

  // Adjust vector bookkeeping.
  iset_->clear();  // prepare phi induction
  bool is_simple_loop_header = TrySetSimpleLoopHeader(header);  // fills iset_
  DCHECK(is_simple_loop_header);

  // Generate preheader:
  // stc = <trip-count>;
  // vtc = stc - stc % VL;
  HInstruction* stc = induction_range_.GenerateTripCount(node->loop_info, graph_, preheader);
  HInstruction* vtc = stc;
  if (needs_cleanup) {
    DCHECK(IsPowerOfTwo(vector_length_));
    HInstruction* rem = Insert(
        preheader, new (global_allocator_) HAnd(induc_type,
                                                stc,
                                                graph_->GetIntConstant(vector_length_ - 1)));
    vtc = Insert(preheader, new (global_allocator_) HSub(induc_type, stc, rem));
  }

  // Generate runtime disambiguation test:
  // vtc = a != b ? vtc : 0;
  if (vector_runtime_test_a_ != nullptr) {
    HInstruction* rt = Insert(
        preheader,
        new (global_allocator_) HNotEqual(vector_runtime_test_a_, vector_runtime_test_b_));
    vtc = Insert(preheader,
                 new (global_allocator_) HSelect(rt, vtc, graph_->GetIntConstant(0), kNoDexPc));
    needs_cleanup = true;
  }

  // Generate vector loop:
  // for (i = 0; i < vtc; i += VL)
  //    <vectorized-loop-body>
  vector_mode_ = kVector;
  GenerateNewLoop(node,
                  block,
                  graph_->TransformLoopForVectorization(header, block, exit),
                  graph_->GetIntConstant(0),
                  vtc,
                  graph_->GetIntConstant(vector_length_));
  HLoopInformation* vloop = vector_header_->GetLoopInformation();

  // Generate cleanup loop, if needed:
  // for ( ; i < stc; i += 1)
  //    <loop-body>
  if (needs_cleanup) {
    vector_mode_ = kSequential;
    GenerateNewLoop(node,
                    block,
                    graph_->TransformLoopForVectorization(vector_header_, vector_body_, exit),
                    vector_phi_,
                    stc,
                    graph_->GetIntConstant(1));
  }

  // Remove the original loop by disconnecting the body block
  // and removing all instructions from the header.
  block->DisconnectAndDelete();
  while (!header->GetFirstInstruction()->IsGoto()) {
    header->RemoveInstruction(header->GetFirstInstruction());
  }
  // Update loop hierarchy: the old header now resides in the
  // same outer loop as the old preheader.
  header->SetLoopInformation(preheader->GetLoopInformation());  // outward
  node->loop_info = vloop;
}

void HLoopOptimization::GenerateNewLoop(LoopNode* node,
                                        HBasicBlock* block,
                                        HBasicBlock* new_preheader,
                                        HInstruction* lo,
                                        HInstruction* hi,
                                        HInstruction* step) {
  Primitive::Type induc_type = Primitive::kPrimInt;
  // Prepare new loop.
  vector_map_->clear();
  vector_preheader_ = new_preheader,
  vector_header_ = vector_preheader_->GetSingleSuccessor();
  vector_body_ = vector_header_->GetSuccessors()[1];
  vector_phi_ = new (global_allocator_) HPhi(global_allocator_,
                                             kNoRegNumber,
                                             0,
                                             HPhi::ToPhiType(induc_type));
  // Generate header and prepare body.
  // for (i = lo; i < hi; i += step)
  //    <loop-body>
  HInstruction* cond = new (global_allocator_) HAboveOrEqual(vector_phi_, hi);
  vector_header_->AddPhi(vector_phi_);
  vector_header_->AddInstruction(cond);
  vector_header_->AddInstruction(new (global_allocator_) HIf(cond));
  for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
    bool vectorized_def = VectorizeDef(node, it.Current(), /*generate_code*/ true);
    DCHECK(vectorized_def);
  }
  // Generate body from the instruction map, but in original program order.
  HEnvironment* env = vector_header_->GetFirstInstruction()->GetEnvironment();
  for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
    auto i = vector_map_->find(it.Current());
    if (i != vector_map_->end() && !i->second->IsInBlock()) {
      Insert(vector_body_, i->second);
      // Deal with instructions that need an environment, such as the scalar intrinsics.
      if (i->second->NeedsEnvironment()) {
        i->second->CopyEnvironmentFromWithLoopPhiAdjustment(env, vector_header_);
      }
    }
  }
  // Finalize increment and phi.
  HInstruction* inc = new (global_allocator_) HAdd(induc_type, vector_phi_, step);
  vector_phi_->AddInput(lo);
  vector_phi_->AddInput(Insert(vector_body_, inc));
}

// TODO: accept reductions at left-hand-side, mixed-type store idioms, etc.
bool HLoopOptimization::VectorizeDef(LoopNode* node,
                                     HInstruction* instruction,
                                     bool generate_code) {
  // Accept a left-hand-side array base[index] for
  // (1) supported vector type,
  // (2) loop-invariant base,
  // (3) unit stride index,
  // (4) vectorizable right-hand-side value.
  uint64_t restrictions = kNone;
  if (instruction->IsArraySet()) {
    Primitive::Type type = instruction->AsArraySet()->GetComponentType();
    HInstruction* base = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    HInstruction* value = instruction->InputAt(2);
    HInstruction* offset = nullptr;
    if (TrySetVectorType(type, &restrictions) &&
        node->loop_info->IsDefinedOutOfTheLoop(base) &&
        induction_range_.IsUnitStride(instruction, index, &offset) &&
        VectorizeUse(node, value, generate_code, type, restrictions)) {
      if (generate_code) {
        GenerateVecSub(index, offset);
        GenerateVecMem(instruction, vector_map_->Get(index), vector_map_->Get(value), type);
      } else {
        vector_refs_->insert(ArrayReference(base, offset, type, /*lhs*/ true));
      }
      return true;
    }
    return false;
  }
  // Branch back okay.
  if (instruction->IsGoto()) {
    return true;
  }
  // Otherwise accept only expressions with no effects outside the immediate loop-body.
  // Note that actual uses are inspected during right-hand-side tree traversal.
  return !IsUsedOutsideLoop(node->loop_info, instruction) && !instruction->DoesAnyWrite();
}

// TODO: more operations and intrinsics, detect saturation arithmetic, etc.
bool HLoopOptimization::VectorizeUse(LoopNode* node,
                                     HInstruction* instruction,
                                     bool generate_code,
                                     Primitive::Type type,
                                     uint64_t restrictions) {
  // Accept anything for which code has already been generated.
  if (generate_code) {
    if (vector_map_->find(instruction) != vector_map_->end()) {
      return true;
    }
  }
  // Continue the right-hand-side tree traversal, passing in proper
  // types and vector restrictions along the way. During code generation,
  // all new nodes are drawn from the global allocator.
  if (node->loop_info->IsDefinedOutOfTheLoop(instruction)) {
    // Accept invariant use, using scalar expansion.
    if (generate_code) {
      GenerateVecInv(instruction, type);
    }
    return true;
  } else if (instruction->IsArrayGet()) {
    // Strings are different, with a different offset to the actual data
    // and some compressed to save memory. For now, all cases are rejected
    // to avoid the complexity.
    if (instruction->AsArrayGet()->IsStringCharAt()) {
      return false;
    }
    // Accept a right-hand-side array base[index] for
    // (1) exact matching vector type,
    // (2) loop-invariant base,
    // (3) unit stride index,
    // (4) vectorizable right-hand-side value.
    HInstruction* base = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    HInstruction* offset = nullptr;
    if (type == instruction->GetType() &&
        node->loop_info->IsDefinedOutOfTheLoop(base) &&
        induction_range_.IsUnitStride(instruction, index, &offset)) {
      if (generate_code) {
        GenerateVecSub(index, offset);
        GenerateVecMem(instruction, vector_map_->Get(index), nullptr, type);
      } else {
        vector_refs_->insert(ArrayReference(base, offset, type, /*lhs*/ false));
      }
      return true;
    }
  } else if (instruction->IsTypeConversion()) {
    // Accept particular type conversions.
    HTypeConversion* conversion = instruction->AsTypeConversion();
    HInstruction* opa = conversion->InputAt(0);
    Primitive::Type from = conversion->GetInputType();
    Primitive::Type to = conversion->GetResultType();
    if ((to == Primitive::kPrimByte ||
         to == Primitive::kPrimChar ||
         to == Primitive::kPrimShort) && from == Primitive::kPrimInt) {
      // Accept a "narrowing" type conversion from a "wider" computation for
      // (1) conversion into final required type,
      // (2) vectorizable operand,
      // (3) "wider" operations cannot bring in higher order bits.
      if (to == type && VectorizeUse(node, opa, generate_code, type, restrictions | kNoHiBits)) {
        if (generate_code) {
          if (vector_mode_ == kVector) {
            vector_map_->Put(instruction, vector_map_->Get(opa));  // operand pass-through
          } else {
            GenerateVecOp(instruction, vector_map_->Get(opa), nullptr, type);
          }
        }
        return true;
      }
    } else if (to == Primitive::kPrimFloat && from == Primitive::kPrimInt) {
      DCHECK_EQ(to, type);
      // Accept int to float conversion for
      // (1) supported int,
      // (2) vectorizable operand.
      if (TrySetVectorType(from, &restrictions) &&
          VectorizeUse(node, opa, generate_code, from, restrictions)) {
        if (generate_code) {
          GenerateVecOp(instruction, vector_map_->Get(opa), nullptr, type);
        }
        return true;
      }
    }
    return false;
  } else if (instruction->IsNeg() || instruction->IsNot() || instruction->IsBooleanNot()) {
    // Accept unary operator for vectorizable operand.
    HInstruction* opa = instruction->InputAt(0);
    if (VectorizeUse(node, opa, generate_code, type, restrictions)) {
      if (generate_code) {
        GenerateVecOp(instruction, vector_map_->Get(opa), nullptr, type);
      }
      return true;
    }
  } else if (instruction->IsAdd() || instruction->IsSub() ||
             instruction->IsMul() || instruction->IsDiv() ||
             instruction->IsAnd() || instruction->IsOr()  || instruction->IsXor()) {
    // Deal with vector restrictions.
    if ((instruction->IsMul() && HasVectorRestrictions(restrictions, kNoMul)) ||
        (instruction->IsDiv() && HasVectorRestrictions(restrictions, kNoDiv))) {
      return false;
    }
    // Accept binary operator for vectorizable operands.
    HInstruction* opa = instruction->InputAt(0);
    HInstruction* opb = instruction->InputAt(1);
    if (VectorizeUse(node, opa, generate_code, type, restrictions) &&
        VectorizeUse(node, opb, generate_code, type, restrictions)) {
      if (generate_code) {
        GenerateVecOp(instruction, vector_map_->Get(opa), vector_map_->Get(opb), type);
      }
      return true;
    }
  } else if (instruction->IsShl() || instruction->IsShr() || instruction->IsUShr()) {
    // Recognize vectorization idioms.
    if (VectorizeHalvingAddIdiom(node, instruction, generate_code, type, restrictions)) {
      return true;
    }
    // Deal with vector restrictions.
    if ((HasVectorRestrictions(restrictions, kNoShift)) ||
        (instruction->IsShr() && HasVectorRestrictions(restrictions, kNoShr))) {
      return false;  // unsupported instruction
    } else if ((instruction->IsShr() || instruction->IsUShr()) &&
               HasVectorRestrictions(restrictions, kNoHiBits)) {
      return false;  // hibits may impact lobits; TODO: we can do better!
    }
    // Accept shift operator for vectorizable/invariant operands.
    // TODO: accept symbolic, albeit loop invariant shift factors.
    HInstruction* opa = instruction->InputAt(0);
    HInstruction* opb = instruction->InputAt(1);
    int64_t value = 0;
    if (VectorizeUse(node, opa, generate_code, type, restrictions) && IsInt64AndGet(opb, &value)) {
      // Make sure shift distance only looks at lower bits, as defined for sequential shifts.
      int64_t mask = (instruction->GetType() == Primitive::kPrimLong)
          ? kMaxLongShiftDistance
          : kMaxIntShiftDistance;
      int64_t distance = value & mask;
      // Restrict shift distance to packed data type width.
      int64_t max_distance = Primitive::ComponentSize(type) * 8;
      if (0 <= distance && distance < max_distance) {
        if (generate_code) {
          HInstruction* s = graph_->GetIntConstant(distance);
          GenerateVecOp(instruction, vector_map_->Get(opa), s, type);
        }
        return true;
      }
    }
  } else if (instruction->IsInvokeStaticOrDirect()) {
    // Accept particular intrinsics.
    HInvokeStaticOrDirect* invoke = instruction->AsInvokeStaticOrDirect();
    switch (invoke->GetIntrinsic()) {
      case Intrinsics::kMathAbsInt:
      case Intrinsics::kMathAbsLong:
      case Intrinsics::kMathAbsFloat:
      case Intrinsics::kMathAbsDouble: {
        // Deal with vector restrictions.
        if (HasVectorRestrictions(restrictions, kNoAbs) ||
            HasVectorRestrictions(restrictions, kNoHiBits)) {
          // TODO: we can do better for some hibits cases.
          return false;
        }
        // Accept ABS(x) for vectorizable operand.
        HInstruction* opa = instruction->InputAt(0);
        if (VectorizeUse(node, opa, generate_code, type, restrictions)) {
          if (generate_code) {
            GenerateVecOp(instruction, vector_map_->Get(opa), nullptr, type);
          }
          return true;
        }
        return false;
      }
      default:
        return false;
    }  // switch
  }
  return false;
}

bool HLoopOptimization::TrySetVectorType(Primitive::Type type, uint64_t* restrictions) {
  const InstructionSetFeatures* features = compiler_driver_->GetInstructionSetFeatures();
  switch (compiler_driver_->GetInstructionSet()) {
    case kArm:
    case kThumb2:
      return false;
    case kArm64:
      // Allow vectorization for all ARM devices, because Android assumes that
      // ARMv8 AArch64 always supports advanced SIMD.
      switch (type) {
        case Primitive::kPrimBoolean:
        case Primitive::kPrimByte:
          *restrictions |= kNoDiv | kNoAbs;
          return TrySetVectorLength(16);
        case Primitive::kPrimChar:
        case Primitive::kPrimShort:
          *restrictions |= kNoDiv | kNoAbs;
          return TrySetVectorLength(8);
        case Primitive::kPrimInt:
          *restrictions |= kNoDiv;
          return TrySetVectorLength(4);
        case Primitive::kPrimLong:
          *restrictions |= kNoDiv | kNoMul;
          return TrySetVectorLength(2);
        case Primitive::kPrimFloat:
          return TrySetVectorLength(4);
        case Primitive::kPrimDouble:
          return TrySetVectorLength(2);
        default:
          return false;
      }
    case kX86:
    case kX86_64:
      // Allow vectorization for SSE4-enabled X86 devices only (128-bit vectors).
      if (features->AsX86InstructionSetFeatures()->HasSSE4_1()) {
        switch (type) {
          case Primitive::kPrimBoolean:
          case Primitive::kPrimByte:
            *restrictions |= kNoMul | kNoDiv | kNoShift | kNoAbs | kNoSignedHAdd | kNoUnroundedHAdd;
            return TrySetVectorLength(16);
          case Primitive::kPrimChar:
          case Primitive::kPrimShort:
            *restrictions |= kNoDiv | kNoAbs | kNoSignedHAdd | kNoUnroundedHAdd;
            return TrySetVectorLength(8);
          case Primitive::kPrimInt:
            *restrictions |= kNoDiv;
            return TrySetVectorLength(4);
          case Primitive::kPrimLong:
            *restrictions |= kNoMul | kNoDiv | kNoShr | kNoAbs;
            return TrySetVectorLength(2);
          case Primitive::kPrimFloat:
            return TrySetVectorLength(4);
          case Primitive::kPrimDouble:
            return TrySetVectorLength(2);
          default:
            break;
        }  // switch type
      }
      return false;
    case kMips:
    case kMips64:
      // TODO: implement MIPS SIMD.
      return false;
    default:
      return false;
  }  // switch instruction set
}

bool HLoopOptimization::TrySetVectorLength(uint32_t length) {
  DCHECK(IsPowerOfTwo(length) && length >= 2u);
  // First time set?
  if (vector_length_ == 0) {
    vector_length_ = length;
  }
  // Different types are acceptable within a loop-body, as long as all the corresponding vector
  // lengths match exactly to obtain a uniform traversal through the vector iteration space
  // (idiomatic exceptions to this rule can be handled by further unrolling sub-expressions).
  return vector_length_ == length;
}

void HLoopOptimization::GenerateVecInv(HInstruction* org, Primitive::Type type) {
  if (vector_map_->find(org) == vector_map_->end()) {
    // In scalar code, just use a self pass-through for scalar invariants
    // (viz. expression remains itself).
    if (vector_mode_ == kSequential) {
      vector_map_->Put(org, org);
      return;
    }
    // In vector code, explicit scalar expansion is needed.
    HInstruction* vector = new (global_allocator_) HVecReplicateScalar(
        global_allocator_, org, type, vector_length_);
    vector_map_->Put(org, Insert(vector_preheader_, vector));
  }
}

void HLoopOptimization::GenerateVecSub(HInstruction* org, HInstruction* offset) {
  if (vector_map_->find(org) == vector_map_->end()) {
    HInstruction* subscript = vector_phi_;
    if (offset != nullptr) {
      subscript = new (global_allocator_) HAdd(Primitive::kPrimInt, subscript, offset);
      if (org->IsPhi()) {
        Insert(vector_body_, subscript);  // lacks layout placeholder
      }
    }
    vector_map_->Put(org, subscript);
  }
}

void HLoopOptimization::GenerateVecMem(HInstruction* org,
                                       HInstruction* opa,
                                       HInstruction* opb,
                                       Primitive::Type type) {
  HInstruction* vector = nullptr;
  if (vector_mode_ == kVector) {
    // Vector store or load.
    if (opb != nullptr) {
      vector = new (global_allocator_) HVecStore(
          global_allocator_, org->InputAt(0), opa, opb, type, vector_length_);
    } else  {
      bool is_string_char_at = org->AsArrayGet()->IsStringCharAt();
      vector = new (global_allocator_) HVecLoad(
          global_allocator_, org->InputAt(0), opa, type, vector_length_, is_string_char_at);
    }
  } else {
    // Scalar store or load.
    DCHECK(vector_mode_ == kSequential);
    if (opb != nullptr) {
      vector = new (global_allocator_) HArraySet(org->InputAt(0), opa, opb, type, kNoDexPc);
    } else  {
      bool is_string_char_at = org->AsArrayGet()->IsStringCharAt();
      vector = new (global_allocator_) HArrayGet(
          org->InputAt(0), opa, type, kNoDexPc, is_string_char_at);
    }
  }
  vector_map_->Put(org, vector);
}

#define GENERATE_VEC(x, y) \
  if (vector_mode_ == kVector) { \
    vector = (x); \
  } else { \
    DCHECK(vector_mode_ == kSequential); \
    vector = (y); \
  } \
  break;

void HLoopOptimization::GenerateVecOp(HInstruction* org,
                                      HInstruction* opa,
                                      HInstruction* opb,
                                      Primitive::Type type) {
  if (vector_mode_ == kSequential) {
    // Scalar code follows implicit integral promotion.
    if (type == Primitive::kPrimBoolean ||
        type == Primitive::kPrimByte ||
        type == Primitive::kPrimChar ||
        type == Primitive::kPrimShort) {
      type = Primitive::kPrimInt;
    }
  }
  HInstruction* vector = nullptr;
  switch (org->GetKind()) {
    case HInstruction::kNeg:
      DCHECK(opb == nullptr);
      GENERATE_VEC(
          new (global_allocator_) HVecNeg(global_allocator_, opa, type, vector_length_),
          new (global_allocator_) HNeg(type, opa));
    case HInstruction::kNot:
      DCHECK(opb == nullptr);
      GENERATE_VEC(
          new (global_allocator_) HVecNot(global_allocator_, opa, type, vector_length_),
          new (global_allocator_) HNot(type, opa));
    case HInstruction::kBooleanNot:
      DCHECK(opb == nullptr);
      GENERATE_VEC(
          new (global_allocator_) HVecNot(global_allocator_, opa, type, vector_length_),
          new (global_allocator_) HBooleanNot(opa));
    case HInstruction::kTypeConversion:
      DCHECK(opb == nullptr);
      GENERATE_VEC(
          new (global_allocator_) HVecCnv(global_allocator_, opa, type, vector_length_),
          new (global_allocator_) HTypeConversion(type, opa, kNoDexPc));
    case HInstruction::kAdd:
      GENERATE_VEC(
          new (global_allocator_) HVecAdd(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HAdd(type, opa, opb));
    case HInstruction::kSub:
      GENERATE_VEC(
          new (global_allocator_) HVecSub(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HSub(type, opa, opb));
    case HInstruction::kMul:
      GENERATE_VEC(
          new (global_allocator_) HVecMul(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HMul(type, opa, opb));
    case HInstruction::kDiv:
      GENERATE_VEC(
          new (global_allocator_) HVecDiv(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HDiv(type, opa, opb, kNoDexPc));
    case HInstruction::kAnd:
      GENERATE_VEC(
          new (global_allocator_) HVecAnd(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HAnd(type, opa, opb));
    case HInstruction::kOr:
      GENERATE_VEC(
          new (global_allocator_) HVecOr(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HOr(type, opa, opb));
    case HInstruction::kXor:
      GENERATE_VEC(
          new (global_allocator_) HVecXor(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HXor(type, opa, opb));
    case HInstruction::kShl:
      GENERATE_VEC(
          new (global_allocator_) HVecShl(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HShl(type, opa, opb));
    case HInstruction::kShr:
      GENERATE_VEC(
          new (global_allocator_) HVecShr(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HShr(type, opa, opb));
    case HInstruction::kUShr:
      GENERATE_VEC(
          new (global_allocator_) HVecUShr(global_allocator_, opa, opb, type, vector_length_),
          new (global_allocator_) HUShr(type, opa, opb));
    case HInstruction::kInvokeStaticOrDirect: {
      HInvokeStaticOrDirect* invoke = org->AsInvokeStaticOrDirect();
      if (vector_mode_ == kVector) {
        switch (invoke->GetIntrinsic()) {
          case Intrinsics::kMathAbsInt:
          case Intrinsics::kMathAbsLong:
          case Intrinsics::kMathAbsFloat:
          case Intrinsics::kMathAbsDouble:
            DCHECK(opb == nullptr);
            vector = new (global_allocator_) HVecAbs(global_allocator_, opa, type, vector_length_);
            break;
          default:
            LOG(FATAL) << "Unsupported SIMD intrinsic";
            UNREACHABLE();
        }  // switch invoke
      } else {
        // In scalar code, simply clone the method invoke, and replace its operands with the
        // corresponding new scalar instructions in the loop. The instruction will get an
        // environment while being inserted from the instruction map in original program order.
        DCHECK(vector_mode_ == kSequential);
        HInvokeStaticOrDirect* new_invoke = new (global_allocator_) HInvokeStaticOrDirect(
            global_allocator_,
            invoke->GetNumberOfArguments(),
            invoke->GetType(),
            invoke->GetDexPc(),
            invoke->GetDexMethodIndex(),
            invoke->GetResolvedMethod(),
            invoke->GetDispatchInfo(),
            invoke->GetInvokeType(),
            invoke->GetTargetMethod(),
            invoke->GetClinitCheckRequirement());
        HInputsRef inputs = invoke->GetInputs();
        for (size_t index = 0; index < inputs.size(); ++index) {
          new_invoke->SetArgumentAt(index, vector_map_->Get(inputs[index]));
        }
        new_invoke->SetIntrinsic(invoke->GetIntrinsic(),
                                 kNeedsEnvironmentOrCache,
                                 kNoSideEffects,
                                 kNoThrow);
        vector = new_invoke;
      }
      break;
    }
    default:
      break;
  }  // switch
  CHECK(vector != nullptr) << "Unsupported SIMD operator";
  vector_map_->Put(org, vector);
}

#undef GENERATE_VEC

//
// Vectorization idioms.
//

// Method recognizes the following idioms:
//   rounding halving add (a + b + 1) >> 1 for unsigned/signed operands a, b
//   regular  halving add (a + b)     >> 1 for unsigned/signed operands a, b
// Provided that the operands are promoted to a wider form to do the arithmetic and
// then cast back to narrower form, the idioms can be mapped into efficient SIMD
// implementation that operates directly in narrower form (plus one extra bit).
// TODO: current version recognizes implicit byte/short/char widening only;
//       explicit widening from int to long could be added later.
bool HLoopOptimization::VectorizeHalvingAddIdiom(LoopNode* node,
                                                 HInstruction* instruction,
                                                 bool generate_code,
                                                 Primitive::Type type,
                                                 uint64_t restrictions) {
  // Test for top level arithmetic shift right x >> 1 or logical shift right x >>> 1
  // (note whether the sign bit in higher precision is shifted in has no effect
  // on the narrow precision computed by the idiom).
  int64_t value = 0;
  if ((instruction->IsShr() ||
       instruction->IsUShr()) &&
      IsInt64AndGet(instruction->InputAt(1), &value) && value == 1) {
    //
    // TODO: make following code less sensitive to associativity and commutativity differences.
    //
    HInstruction* x = instruction->InputAt(0);
    // Test for an optional rounding part (x + 1) >> 1.
    bool is_rounded = false;
    if (x->IsAdd() && IsInt64AndGet(x->InputAt(1), &value) && value == 1) {
      x = x->InputAt(0);
      is_rounded = true;
    }
    // Test for a core addition (a + b) >> 1 (possibly rounded), either unsigned or signed.
    if (x->IsAdd()) {
      HInstruction* a = x->InputAt(0);
      HInstruction* b = x->InputAt(1);
      HInstruction* r = nullptr;
      HInstruction* s = nullptr;
      bool is_unsigned = false;
      if (IsZeroExtensionAndGet(a, type, &r) && IsZeroExtensionAndGet(b, type, &s)) {
        is_unsigned = true;
      } else if (IsSignExtensionAndGet(a, type, &r) && IsSignExtensionAndGet(b, type, &s)) {
        is_unsigned = false;
      } else {
        return false;
      }
      // Deal with vector restrictions.
      if ((!is_unsigned && HasVectorRestrictions(restrictions, kNoSignedHAdd)) ||
          (!is_rounded && HasVectorRestrictions(restrictions, kNoUnroundedHAdd))) {
        return false;
      }
      // Accept recognized halving add for vectorizable operands. Vectorized code uses the
      // shorthand idiomatic operation. Sequential code uses the original scalar expressions.
      DCHECK(r != nullptr && s != nullptr);
      if (VectorizeUse(node, r, generate_code, type, restrictions) &&
          VectorizeUse(node, s, generate_code, type, restrictions)) {
        if (generate_code) {
          if (vector_mode_ == kVector) {
            vector_map_->Put(instruction, new (global_allocator_) HVecHalvingAdd(
                global_allocator_,
                vector_map_->Get(r),
                vector_map_->Get(s),
                type,
                vector_length_,
                is_unsigned,
                is_rounded));
          } else {
            VectorizeUse(node, instruction->InputAt(0), generate_code, type, restrictions);
            VectorizeUse(node, instruction->InputAt(1), generate_code, type, restrictions);
            GenerateVecOp(instruction,
                          vector_map_->Get(instruction->InputAt(0)),
                          vector_map_->Get(instruction->InputAt(1)),
                          type);
          }
        }
        return true;
      }
    }
  }
  return false;
}

//
// Helpers.
//

bool HLoopOptimization::TrySetPhiInduction(HPhi* phi, bool restrict_uses) {
  DCHECK(iset_->empty());
  ArenaSet<HInstruction*>* set = induction_range_.LookupCycle(phi);
  if (set != nullptr) {
    for (HInstruction* i : *set) {
      // Check that, other than instructions that are no longer in the graph (removed earlier)
      // each instruction is removable and, when restrict uses are requested, other than for phi,
      // all uses are contained within the cycle.
      if (!i->IsInBlock()) {
        continue;
      } else if (!i->IsRemovable()) {
        return false;
      } else if (i != phi && restrict_uses) {
        for (const HUseListNode<HInstruction*>& use : i->GetUses()) {
          if (set->find(use.GetUser()) == set->end()) {
            return false;
          }
        }
      }
      iset_->insert(i);  // copy
    }
    return true;
  }
  return false;
}

// Find: phi: Phi(init, addsub)
//       s:   SuspendCheck
//       c:   Condition(phi, bound)
//       i:   If(c)
// TODO: Find a less pattern matching approach?
bool HLoopOptimization::TrySetSimpleLoopHeader(HBasicBlock* block) {
  DCHECK(iset_->empty());
  HInstruction* phi = block->GetFirstPhi();
  if (phi != nullptr &&
      phi->GetNext() == nullptr &&
      TrySetPhiInduction(phi->AsPhi(), /*restrict_uses*/ false)) {
    HInstruction* s = block->GetFirstInstruction();
    if (s != nullptr && s->IsSuspendCheck()) {
      HInstruction* c = s->GetNext();
      if (c != nullptr &&
          c->IsCondition() &&
          c->GetUses().HasExactlyOneElement() &&  // only used for termination
          !c->HasEnvironmentUses()) {  // unlikely, but not impossible
        HInstruction* i = c->GetNext();
        if (i != nullptr && i->IsIf() && i->InputAt(0) == c) {
          iset_->insert(c);
          iset_->insert(s);
          return true;
        }
      }
    }
  }
  return false;
}

bool HLoopOptimization::IsEmptyBody(HBasicBlock* block) {
  if (!block->GetPhis().IsEmpty()) {
    return false;
  }
  for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
    HInstruction* instruction = it.Current();
    if (!instruction->IsGoto() && iset_->find(instruction) == iset_->end()) {
      return false;
    }
  }
  return true;
}

bool HLoopOptimization::IsUsedOutsideLoop(HLoopInformation* loop_info,
                                          HInstruction* instruction) {
  for (const HUseListNode<HInstruction*>& use : instruction->GetUses()) {
    if (use.GetUser()->GetBlock()->GetLoopInformation() != loop_info) {
      return true;
    }
  }
  return false;
}

bool HLoopOptimization::IsOnlyUsedAfterLoop(HLoopInformation* loop_info,
                                            HInstruction* instruction,
                                            bool collect_loop_uses,
                                            /*out*/ int32_t* use_count) {
  for (const HUseListNode<HInstruction*>& use : instruction->GetUses()) {
    HInstruction* user = use.GetUser();
    if (iset_->find(user) == iset_->end()) {  // not excluded?
      HLoopInformation* other_loop_info = user->GetBlock()->GetLoopInformation();
      if (other_loop_info != nullptr && other_loop_info->IsIn(*loop_info)) {
        // If collect_loop_uses is set, simply keep adding those uses to the set.
        // Otherwise, reject uses inside the loop that were not already in the set.
        if (collect_loop_uses) {
          iset_->insert(user);
          continue;
        }
        return false;
      }
      ++*use_count;
    }
  }
  return true;
}

bool HLoopOptimization::TryReplaceWithLastValue(HLoopInformation* loop_info,
                                                HInstruction* instruction,
                                                HBasicBlock* block) {
  // Try to replace outside uses with the last value.
  if (induction_range_.CanGenerateLastValue(instruction)) {
    HInstruction* replacement = induction_range_.GenerateLastValue(instruction, graph_, block);
    const HUseList<HInstruction*>& uses = instruction->GetUses();
    for (auto it = uses.begin(), end = uses.end(); it != end;) {
      HInstruction* user = it->GetUser();
      size_t index = it->GetIndex();
      ++it;  // increment before replacing
      if (iset_->find(user) == iset_->end()) {  // not excluded?
        if (kIsDebugBuild) {
          // We have checked earlier in 'IsOnlyUsedAfterLoop' that the use is after the loop.
          HLoopInformation* other_loop_info = user->GetBlock()->GetLoopInformation();
          CHECK(other_loop_info == nullptr || !other_loop_info->IsIn(*loop_info));
        }
        user->ReplaceInput(replacement, index);
        induction_range_.Replace(user, instruction, replacement);  // update induction
      }
    }
    const HUseList<HEnvironment*>& env_uses = instruction->GetEnvUses();
    for (auto it = env_uses.begin(), end = env_uses.end(); it != end;) {
      HEnvironment* user = it->GetUser();
      size_t index = it->GetIndex();
      ++it;  // increment before replacing
      if (iset_->find(user->GetHolder()) == iset_->end()) {  // not excluded?
        HLoopInformation* other_loop_info = user->GetHolder()->GetBlock()->GetLoopInformation();
        // Only update environment uses after the loop.
        if (other_loop_info == nullptr || !other_loop_info->IsIn(*loop_info)) {
          user->RemoveAsUserOfInput(index);
          user->SetRawEnvAt(index, replacement);
          replacement->AddEnvUseAt(user, index);
        }
      }
    }
    induction_simplication_count_++;
    return true;
  }
  return false;
}

bool HLoopOptimization::TryAssignLastValue(HLoopInformation* loop_info,
                                           HInstruction* instruction,
                                           HBasicBlock* block,
                                           bool collect_loop_uses) {
  // Assigning the last value is always successful if there are no uses.
  // Otherwise, it succeeds in a no early-exit loop by generating the
  // proper last value assignment.
  int32_t use_count = 0;
  return IsOnlyUsedAfterLoop(loop_info, instruction, collect_loop_uses, &use_count) &&
      (use_count == 0 ||
       (!IsEarlyExit(loop_info) && TryReplaceWithLastValue(loop_info, instruction, block)));
}

void HLoopOptimization::RemoveDeadInstructions(const HInstructionList& list) {
  for (HBackwardInstructionIterator i(list); !i.Done(); i.Advance()) {
    HInstruction* instruction = i.Current();
    if (instruction->IsDeadAndRemovable()) {
      simplified_ = true;
      instruction->GetBlock()->RemoveInstructionOrPhi(instruction);
    }
  }
}

}  // namespace art
