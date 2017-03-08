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

#include "driver/compiler_driver.h"
#include "linear_order.h"

namespace art {

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
      top_loop_(nullptr),
      last_loop_(nullptr),
      iset_(nullptr),
      induction_simplication_count_(0),
      simplified_(false) {
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
  ArenaAllocator allocator(graph_->GetArena()->GetArenaPool());
  loop_allocator_ = &allocator;

  // Perform loop optimizations.
  LocalRun();

  if (top_loop_ == nullptr) {
    // All loops have been eliminated.
    graph_->SetHasLoops(false);
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
  // a temporary set that stores instructions using the phase-local allocator.
  if (top_loop_ != nullptr) {
    ArenaSet<HInstruction*> iset(loop_allocator_->Adapter(kArenaAllocLoopOptimization));
    iset_ = &iset;
    TraverseLoopsInnerToOuter(top_loop_);
    iset_ = nullptr;  // detach
  }
}

void HLoopOptimization::AddLoop(HLoopInformation* loop_info) {
  DCHECK(loop_info != nullptr);
  LoopNode* node = new (loop_allocator_) LoopNode(loop_info);  // phase-local allocator
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
    int current_induction_simplification_count = induction_simplication_count_;
    if (node->inner != nullptr) {
      TraverseLoopsInnerToOuter(node->inner);
    }
    // Recompute induction information of this loop if the induction
    // of any inner loop has been simplified.
    if (current_induction_simplification_count != induction_simplication_count_) {
      induction_range_.ReVisit(node->loop_info);
    }
    // Repeat simplifications in the body of this loop until no more changes occur.
    // Note that since each simplification consists of eliminating code (without
    // introducing new code), this process is always finite.
    do {
      simplified_ = false;
      SimplifyInduction(node);
      SimplifyBlocks(node);
    } while (simplified_);
    // Simplify inner loop.
    if (node->inner == nullptr) {
      SimplifyInnerLoop(node);
    }
  }
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
    iset_->clear();
    int32_t use_count = 0;
    if (IsPhiInduction(phi) &&
        IsOnlyUsedAfterLoop(node->loop_info, phi, /*collect_loop_uses*/ false, &use_count) &&
        // No uses, or no early-exit with proper replacement.
        (use_count == 0 ||
         (!IsEarlyExit(node->loop_info) && TryReplaceWithLastValue(phi, preheader)))) {
      for (HInstruction* i : *iset_) {
        RemoveFromCycle(i);
      }
      simplified_ = true;
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

bool HLoopOptimization::SimplifyInnerLoop(LoopNode* node) {
  HBasicBlock* header = node->loop_info->GetHeader();
  HBasicBlock* preheader = node->loop_info->GetPreHeader();
  // Ensure loop header logic is finite.
  int64_t tc = 0;
  if (!induction_range_.IsFinite(node->loop_info, &tc)) {
    return false;
  }
  // Ensure there is only a single loop-body (besides the header).
  HBasicBlock* body = nullptr;
  for (HBlocksInLoopIterator it(*node->loop_info); !it.Done(); it.Advance()) {
    if (it.Current() != header) {
      if (body != nullptr) {
        return false;
      }
      body = it.Current();
    }
  }
  // Ensure there is only a single exit point.
  if (header->GetSuccessors().size() != 2) {
    return false;
  }
  HBasicBlock* exit = (header->GetSuccessors()[0] == body)
      ? header->GetSuccessors()[1]
      : header->GetSuccessors()[0];
  // Ensure exit can only be reached by exiting loop.
  if (exit->GetPredecessors().size() != 1) {
    return false;
  }
  // Detect either an empty loop (no side effects other than plain iteration) or
  // a trivial loop (just iterating once). Replace subsequent index uses, if any,
  // with the last value and remove the loop, possibly after unrolling its body.
  HInstruction* phi = header->GetFirstPhi();
  iset_->clear();
  int32_t use_count = 0;
  if (IsEmptyHeader(header)) {
    bool is_empty = IsEmptyBody(body);
    if ((is_empty || tc == 1) &&
        IsOnlyUsedAfterLoop(node->loop_info, phi, /*collect_loop_uses*/ true, &use_count) &&
        // No uses, or proper replacement.
        (use_count == 0 || TryReplaceWithLastValue(phi, preheader))) {
      if (!is_empty) {
        // Unroll the loop body, which sees initial value of the index.
        phi->ReplaceWith(phi->InputAt(0));
        preheader->MergeInstructionsWith(body);
      }
      body->DisconnectAndDelete();
      exit->RemovePredecessor(header);
      header->RemoveSuccessor(exit);
      header->RemoveDominatedBlock(exit);
      header->DisconnectAndDelete();
      preheader->AddSuccessor(exit);
      preheader->AddInstruction(new (graph_->GetArena()) HGoto());  // global allocator
      preheader->AddDominatedBlock(exit);
      exit->SetDominator(preheader);
      RemoveLoop(node);  // update hierarchy
      return true;
    }
  }
  return false;
}

bool HLoopOptimization::IsPhiInduction(HPhi* phi) {
  ArenaSet<HInstruction*>* set = induction_range_.LookupCycle(phi);
  if (set != nullptr) {
    DCHECK(iset_->empty());
    for (HInstruction* i : *set) {
      // Check that, other than instructions that are no longer in the graph (removed earlier)
      // each instruction is removable and, other than the phi, uses are contained in the cycle.
      if (!i->IsInBlock()) {
        continue;
      } else if (!i->IsRemovable()) {
        return false;
      } else if (i != phi) {
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
bool HLoopOptimization::IsEmptyHeader(HBasicBlock* block) {
  DCHECK(iset_->empty());
  HInstruction* phi = block->GetFirstPhi();
  if (phi != nullptr && phi->GetNext() == nullptr && IsPhiInduction(phi->AsPhi())) {
    HInstruction* s = block->GetFirstInstruction();
    if (s != nullptr && s->IsSuspendCheck()) {
      HInstruction* c = s->GetNext();
      if (c != nullptr && c->IsCondition() && c->GetUses().HasExactlyOneElement()) {
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
  if (block->GetFirstPhi() == nullptr) {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (!instruction->IsGoto() && iset_->find(instruction) == iset_->end()) {
        return false;
      }
    }
    return true;
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

bool HLoopOptimization::TryReplaceWithLastValue(HInstruction* instruction, HBasicBlock* block) {
  // Try to replace outside uses with the last value. Environment uses can consume this
  // value too, since any first true use is outside the loop (although this may imply
  // that de-opting may look "ahead" a bit on the phi value). If there are only environment
  // uses, the value is dropped altogether, since the computations have no effect.
  if (induction_range_.CanGenerateLastValue(instruction)) {
    HInstruction* replacement = induction_range_.GenerateLastValue(instruction, graph_, block);
    const HUseList<HInstruction*>& uses = instruction->GetUses();
    for (auto it = uses.begin(), end = uses.end(); it != end;) {
      HInstruction* user = it->GetUser();
      size_t index = it->GetIndex();
      ++it;  // increment before replacing
      if (iset_->find(user) == iset_->end()) {  // not excluded?
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
        user->RemoveAsUserOfInput(index);
        user->SetRawEnvAt(index, replacement);
        replacement->AddEnvUseAt(user, index);
      }
    }
    induction_simplication_count_++;
    return true;
  }
  return false;
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
