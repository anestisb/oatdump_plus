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

#include "linear_order.h"

namespace art {

// TODO: Generalize to cycles, as found by induction analysis?
static bool IsPhiInduction(HPhi* phi, ArenaSet<HInstruction*>* iset) {
  DCHECK(iset->empty());
  HInputsRef inputs = phi->GetInputs();
  if (inputs.size() == 2 && (inputs[1]->IsAdd() || inputs[1]->IsSub())) {
    HInstruction* addsub = inputs[1];
    if (addsub->InputAt(0) == phi || addsub->InputAt(1) == phi) {
      if (addsub->GetUses().HasExactlyOneElement()) {
        iset->insert(phi);
        iset->insert(addsub);
        return true;
      }
    }
  }
  return false;
}

// Find: phi: Phi(init, addsub)
//       s:   SuspendCheck
//       c:   Condition(phi, bound)
//       i:   If(c)
// TODO: Find a less pattern matching approach?
static bool IsEmptyHeader(HBasicBlock* block, ArenaSet<HInstruction*>* iset) {
  DCHECK(iset->empty());
  HInstruction* phi = block->GetFirstPhi();
  if (phi != nullptr && phi->GetNext() == nullptr && IsPhiInduction(phi->AsPhi(), iset)) {
    HInstruction* s = block->GetFirstInstruction();
    if (s != nullptr && s->IsSuspendCheck()) {
      HInstruction* c = s->GetNext();
      if (c != nullptr && c->IsCondition() && c->GetUses().HasExactlyOneElement()) {
        HInstruction* i = c->GetNext();
        if (i != nullptr && i->IsIf() && i->InputAt(0) == c) {
          iset->insert(c);
          iset->insert(s);
          return true;
        }
      }
    }
  }
  return false;
}

static bool IsEmptyBody(HBasicBlock* block, ArenaSet<HInstruction*>* iset) {
  HInstruction* phi = block->GetFirstPhi();
  HInstruction* i = block->GetFirstInstruction();
  return phi == nullptr && iset->find(i) != iset->end() &&
      i->GetNext() != nullptr && i->GetNext()->IsGoto();
}

static HBasicBlock* TryRemovePreHeader(HBasicBlock* preheader, HBasicBlock* entry_block) {
  if (preheader->GetPredecessors().size() == 1) {
    HBasicBlock* entry = preheader->GetSinglePredecessor();
    HInstruction* anchor = entry->GetLastInstruction();
    // If the pre-header has a single predecessor we can remove it too if
    // either the pre-header just contains a goto, or if the predecessor
    // is not the entry block so we can push instructions backward
    // (moving computation into the entry block is too dangerous!).
    if (preheader->GetFirstInstruction() == nullptr ||
        preheader->GetFirstInstruction()->IsGoto() ||
        (entry != entry_block && anchor->IsGoto())) {
      // Push non-goto statements backward to empty the pre-header.
      for (HInstructionIterator it(preheader->GetInstructions()); !it.Done(); it.Advance()) {
        HInstruction* instruction = it.Current();
        if (!instruction->IsGoto()) {
          if (!instruction->CanBeMoved()) {
            return nullptr;  // pushing failed to move all
          }
          it.Current()->MoveBefore(anchor);
        }
      }
      return entry;
    }
  }
  return nullptr;
}

static void RemoveFromCycle(HInstruction* instruction) {
  // A bit more elaborate than the usual instruction removal,
  // since there may be a cycle in the use structure.
  instruction->RemoveAsUserOfAllInputs();
  instruction->RemoveEnvironmentUsers();
  instruction->GetBlock()->RemoveInstructionOrPhi(instruction, /*ensure_safety=*/ false);
}

//
// Class methods.
//

HLoopOptimization::HLoopOptimization(HGraph* graph,
                                     HInductionVarAnalysis* induction_analysis)
    : HOptimization(graph, kLoopOptimizationPassName),
      induction_range_(induction_analysis),
      loop_allocator_(nullptr),
      top_loop_(nullptr),
      last_loop_(nullptr),
      iset_(nullptr) {
}

void HLoopOptimization::Run() {
  // Well-behaved loops only.
  // TODO: make this less of a sledgehammer.
  if (graph_->HasTryCatch() || graph_->HasIrreducibleLoops()) {
    return;
  }

  // Phase-local allocator that draws from the global pool. Since the allocator
  // itself resides on the stack, it is destructed on exiting Run(), which
  // implies its underlying memory is released immediately.
  ArenaAllocator allocator(graph_->GetArena()->GetArenaPool());
  loop_allocator_ = &allocator;

  // Perform loop optimizations.
  LocalRun();

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
    if (node->inner != nullptr) {
      TraverseLoopsInnerToOuter(node->inner);
    }
    // Visit loop after its inner loops have been visited.
    SimplifyInduction(node);
    RemoveIfEmptyLoop(node);
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
    if (IsPhiInduction(phi, iset_) &&
        IsOnlyUsedAfterLoop(*node->loop_info, phi, &use_count) &&
        TryReplaceWithLastValue(phi, use_count, preheader)) {
      for (HInstruction* i : *iset_) {
        RemoveFromCycle(i);
      }
    }
  }
}

void HLoopOptimization::RemoveIfEmptyLoop(LoopNode* node) {
  HBasicBlock* header = node->loop_info->GetHeader();
  HBasicBlock* preheader = node->loop_info->GetPreHeader();
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
  // Detect an empty loop: no side effects other than plain iteration. Replace
  // subsequent index uses, if any, with the last value and remove the loop.
  iset_->clear();
  int32_t use_count = 0;
  if (IsEmptyHeader(header, iset_) &&
      IsEmptyBody(body, iset_) &&
      IsOnlyUsedAfterLoop(*node->loop_info, header->GetFirstPhi(), &use_count) &&
      TryReplaceWithLastValue(header->GetFirstPhi(), use_count, preheader)) {
    HBasicBlock* entry = TryRemovePreHeader(preheader, graph_->GetEntryBlock());
    body->DisconnectAndDelete();
    exit->RemovePredecessor(header);
    header->RemoveSuccessor(exit);
    header->ClearDominanceInformation();
    header->SetDominator(preheader);  // needed by next disconnect.
    header->DisconnectAndDelete();
    // If allowed, remove preheader too, which may expose next outer empty loop
    // Otherwise, link preheader directly to exit to restore the flow graph.
    if (entry != nullptr) {
      entry->ReplaceSuccessor(preheader, exit);
      entry->AddDominatedBlock(exit);
      exit->SetDominator(entry);
      preheader->DisconnectAndDelete();
    } else {
      preheader->AddSuccessor(exit);
      preheader->AddInstruction(new (graph_->GetArena()) HGoto());  // global allocator
      preheader->AddDominatedBlock(exit);
      exit->SetDominator(preheader);
    }
    // Update hierarchy.
    RemoveLoop(node);
  }
}

bool HLoopOptimization::IsOnlyUsedAfterLoop(const HLoopInformation& loop_info,
                                            HInstruction* instruction,
                                            /*out*/ int32_t* use_count) {
  for (const HUseListNode<HInstruction*>& use : instruction->GetUses()) {
    HInstruction* user = use.GetUser();
    if (iset_->find(user) == iset_->end()) {  // not excluded?
      HLoopInformation* other_loop_info = user->GetBlock()->GetLoopInformation();
      if (other_loop_info != nullptr && other_loop_info->IsIn(loop_info)) {
        return false;
      }
      ++*use_count;
    }
  }
  return true;
}

void HLoopOptimization::ReplaceAllUses(HInstruction* instruction, HInstruction* replacement) {
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
}

bool HLoopOptimization::TryReplaceWithLastValue(HInstruction* instruction,
                                                int32_t use_count,
                                                HBasicBlock* block) {
  // If true uses appear after the loop, replace these uses with the last value. Environment
  // uses can consume this value too, since any first true use is outside the loop (although
  // this may imply that de-opting may look "ahead" a bit on the phi value). If there are only
  // environment uses, the value is dropped altogether, since the computations have no effect.
  if (use_count > 0) {
    if (!induction_range_.CanGenerateLastValue(instruction)) {
      return false;
    }
    ReplaceAllUses(instruction, induction_range_.GenerateLastValue(instruction, graph_, block));
  }
  return true;
}

}  // namespace art
