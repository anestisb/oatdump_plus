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

#include "load_store_analysis.h"
#include "load_store_elimination.h"

#include "escape.h"
#include "side_effects_analysis.h"

#include <iostream>

namespace art {

// An unknown heap value. Loads with such a value in the heap location cannot be eliminated.
// A heap location can be set to kUnknownHeapValue when:
// - initially set a value.
// - killed due to aliasing, merging, invocation, or loop side effects.
static HInstruction* const kUnknownHeapValue =
    reinterpret_cast<HInstruction*>(static_cast<uintptr_t>(-1));

// Default heap value after an allocation.
// A heap location can be set to that value right after an allocation.
static HInstruction* const kDefaultHeapValue =
    reinterpret_cast<HInstruction*>(static_cast<uintptr_t>(-2));

class LSEVisitor : public HGraphVisitor {
 public:
  LSEVisitor(HGraph* graph,
             const HeapLocationCollector& heap_locations_collector,
             const SideEffectsAnalysis& side_effects)
      : HGraphVisitor(graph),
        heap_location_collector_(heap_locations_collector),
        side_effects_(side_effects),
        heap_values_for_(graph->GetBlocks().size(),
                         ArenaVector<HInstruction*>(heap_locations_collector.
                                                    GetNumberOfHeapLocations(),
                                                    kUnknownHeapValue,
                                                    graph->GetArena()->Adapter(kArenaAllocLSE)),
                         graph->GetArena()->Adapter(kArenaAllocLSE)),
        removed_loads_(graph->GetArena()->Adapter(kArenaAllocLSE)),
        substitute_instructions_for_loads_(graph->GetArena()->Adapter(kArenaAllocLSE)),
        possibly_removed_stores_(graph->GetArena()->Adapter(kArenaAllocLSE)),
        singleton_new_instances_(graph->GetArena()->Adapter(kArenaAllocLSE)),
        singleton_new_arrays_(graph->GetArena()->Adapter(kArenaAllocLSE)) {
  }

  void VisitBasicBlock(HBasicBlock* block) OVERRIDE {
    // Populate the heap_values array for this block.
    // TODO: try to reuse the heap_values array from one predecessor if possible.
    if (block->IsLoopHeader()) {
      HandleLoopSideEffects(block);
    } else {
      MergePredecessorValues(block);
    }
    HGraphVisitor::VisitBasicBlock(block);
  }

  // Remove recorded instructions that should be eliminated.
  void RemoveInstructions() {
    size_t size = removed_loads_.size();
    DCHECK_EQ(size, substitute_instructions_for_loads_.size());
    for (size_t i = 0; i < size; i++) {
      HInstruction* load = removed_loads_[i];
      DCHECK(load != nullptr);
      DCHECK(load->IsInstanceFieldGet() ||
             load->IsStaticFieldGet() ||
             load->IsArrayGet());
      HInstruction* substitute = substitute_instructions_for_loads_[i];
      DCHECK(substitute != nullptr);
      // Keep tracing substitute till one that's not removed.
      HInstruction* sub_sub = FindSubstitute(substitute);
      while (sub_sub != substitute) {
        substitute = sub_sub;
        sub_sub = FindSubstitute(substitute);
      }
      load->ReplaceWith(substitute);
      load->GetBlock()->RemoveInstruction(load);
    }

    // At this point, stores in possibly_removed_stores_ can be safely removed.
    for (HInstruction* store : possibly_removed_stores_) {
      DCHECK(store->IsInstanceFieldSet() || store->IsStaticFieldSet() || store->IsArraySet());
      store->GetBlock()->RemoveInstruction(store);
    }

    // Eliminate singleton-classified instructions:
    //   * - Constructor fences (they never escape this thread).
    //   * - Allocations (if they are unused).
    for (HInstruction* new_instance : singleton_new_instances_) {
      HConstructorFence::RemoveConstructorFences(new_instance);

      if (!new_instance->HasNonEnvironmentUses()) {
        new_instance->RemoveEnvironmentUsers();
        new_instance->GetBlock()->RemoveInstruction(new_instance);
      }
    }
    for (HInstruction* new_array : singleton_new_arrays_) {
      HConstructorFence::RemoveConstructorFences(new_array);

      if (!new_array->HasNonEnvironmentUses()) {
        new_array->RemoveEnvironmentUsers();
        new_array->GetBlock()->RemoveInstruction(new_array);
      }
    }
  }

 private:
  // If heap_values[index] is an instance field store, need to keep the store.
  // This is necessary if a heap value is killed due to merging, or loop side
  // effects (which is essentially merging also), since a load later from the
  // location won't be eliminated.
  void KeepIfIsStore(HInstruction* heap_value) {
    if (heap_value == kDefaultHeapValue ||
        heap_value == kUnknownHeapValue ||
        !(heap_value->IsInstanceFieldSet() || heap_value->IsArraySet())) {
      return;
    }
    auto idx = std::find(possibly_removed_stores_.begin(),
        possibly_removed_stores_.end(), heap_value);
    if (idx != possibly_removed_stores_.end()) {
      // Make sure the store is kept.
      possibly_removed_stores_.erase(idx);
    }
  }

  void HandleLoopSideEffects(HBasicBlock* block) {
    DCHECK(block->IsLoopHeader());
    int block_id = block->GetBlockId();
    ArenaVector<HInstruction*>& heap_values = heap_values_for_[block_id];

    // Don't eliminate loads in irreducible loops. This is safe for singletons, because
    // they are always used by the non-eliminated loop-phi.
    if (block->GetLoopInformation()->IsIrreducible()) {
      if (kIsDebugBuild) {
        for (size_t i = 0; i < heap_values.size(); i++) {
          DCHECK_EQ(heap_values[i], kUnknownHeapValue);
        }
      }
      return;
    }

    HBasicBlock* pre_header = block->GetLoopInformation()->GetPreHeader();
    ArenaVector<HInstruction*>& pre_header_heap_values =
        heap_values_for_[pre_header->GetBlockId()];

    // Inherit the values from pre-header.
    for (size_t i = 0; i < heap_values.size(); i++) {
      heap_values[i] = pre_header_heap_values[i];
    }

    // We do a single pass in reverse post order. For loops, use the side effects as a hint
    // to see if the heap values should be killed.
    if (side_effects_.GetLoopEffects(block).DoesAnyWrite()) {
      for (size_t i = 0; i < heap_values.size(); i++) {
        HeapLocation* location = heap_location_collector_.GetHeapLocation(i);
        ReferenceInfo* ref_info = location->GetReferenceInfo();
        if (ref_info->IsSingletonAndRemovable() &&
            !location->IsValueKilledByLoopSideEffects()) {
          // A removable singleton's field that's not stored into inside a loop is
          // invariant throughout the loop. Nothing to do.
          DCHECK(ref_info->IsSingletonAndRemovable());
        } else {
          // heap value is killed by loop side effects (stored into directly, or
          // due to aliasing). Or the heap value may be needed after method return
          // or deoptimization.
          KeepIfIsStore(pre_header_heap_values[i]);
          heap_values[i] = kUnknownHeapValue;
        }
      }
    }
  }

  void MergePredecessorValues(HBasicBlock* block) {
    const ArenaVector<HBasicBlock*>& predecessors = block->GetPredecessors();
    if (predecessors.size() == 0) {
      return;
    }

    ArenaVector<HInstruction*>& heap_values = heap_values_for_[block->GetBlockId()];
    for (size_t i = 0; i < heap_values.size(); i++) {
      HInstruction* merged_value = nullptr;
      // Whether merged_value is a result that's merged from all predecessors.
      bool from_all_predecessors = true;
      ReferenceInfo* ref_info = heap_location_collector_.GetHeapLocation(i)->GetReferenceInfo();
      HInstruction* singleton_ref = nullptr;
      if (ref_info->IsSingleton()) {
        // We do more analysis of liveness when merging heap values for such
        // cases since stores into such references may potentially be eliminated.
        singleton_ref = ref_info->GetReference();
      }

      for (HBasicBlock* predecessor : predecessors) {
        HInstruction* pred_value = heap_values_for_[predecessor->GetBlockId()][i];
        if ((singleton_ref != nullptr) &&
            !singleton_ref->GetBlock()->Dominates(predecessor)) {
          // singleton_ref is not live in this predecessor. Skip this predecessor since
          // it does not really have the location.
          DCHECK_EQ(pred_value, kUnknownHeapValue);
          from_all_predecessors = false;
          continue;
        }
        if (merged_value == nullptr) {
          // First seen heap value.
          merged_value = pred_value;
        } else if (pred_value != merged_value) {
          // There are conflicting values.
          merged_value = kUnknownHeapValue;
          break;
        }
      }

      if (merged_value == kUnknownHeapValue || ref_info->IsSingletonAndNonRemovable()) {
        // There are conflicting heap values from different predecessors,
        // or the heap value may be needed after method return or deoptimization.
        // Keep the last store in each predecessor since future loads cannot be eliminated.
        for (HBasicBlock* predecessor : predecessors) {
          ArenaVector<HInstruction*>& pred_values = heap_values_for_[predecessor->GetBlockId()];
          KeepIfIsStore(pred_values[i]);
        }
      }

      if ((merged_value == nullptr) || !from_all_predecessors) {
        DCHECK(singleton_ref != nullptr);
        DCHECK((singleton_ref->GetBlock() == block) ||
               !singleton_ref->GetBlock()->Dominates(block));
        // singleton_ref is not defined before block or defined only in some of its
        // predecessors, so block doesn't really have the location at its entry.
        heap_values[i] = kUnknownHeapValue;
      } else {
        heap_values[i] = merged_value;
      }
    }
  }

  // `instruction` is being removed. Try to see if the null check on it
  // can be removed. This can happen if the same value is set in two branches
  // but not in dominators. Such as:
  //   int[] a = foo();
  //   if () {
  //     a[0] = 2;
  //   } else {
  //     a[0] = 2;
  //   }
  //   // a[0] can now be replaced with constant 2, and the null check on it can be removed.
  void TryRemovingNullCheck(HInstruction* instruction) {
    HInstruction* prev = instruction->GetPrevious();
    if ((prev != nullptr) && prev->IsNullCheck() && (prev == instruction->InputAt(0))) {
      // Previous instruction is a null check for this instruction. Remove the null check.
      prev->ReplaceWith(prev->InputAt(0));
      prev->GetBlock()->RemoveInstruction(prev);
    }
  }

  HInstruction* GetDefaultValue(Primitive::Type type) {
    switch (type) {
      case Primitive::kPrimNot:
        return GetGraph()->GetNullConstant();
      case Primitive::kPrimBoolean:
      case Primitive::kPrimByte:
      case Primitive::kPrimChar:
      case Primitive::kPrimShort:
      case Primitive::kPrimInt:
        return GetGraph()->GetIntConstant(0);
      case Primitive::kPrimLong:
        return GetGraph()->GetLongConstant(0);
      case Primitive::kPrimFloat:
        return GetGraph()->GetFloatConstant(0);
      case Primitive::kPrimDouble:
        return GetGraph()->GetDoubleConstant(0);
      default:
        UNREACHABLE();
    }
  }

  void VisitGetLocation(HInstruction* instruction,
                        HInstruction* ref,
                        size_t offset,
                        HInstruction* index,
                        int16_t declaring_class_def_index) {
    HInstruction* original_ref = heap_location_collector_.HuntForOriginalReference(ref);
    ReferenceInfo* ref_info = heap_location_collector_.FindReferenceInfoOf(original_ref);
    size_t idx = heap_location_collector_.FindHeapLocationIndex(
        ref_info, offset, index, declaring_class_def_index);
    DCHECK_NE(idx, HeapLocationCollector::kHeapLocationNotFound);
    ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[instruction->GetBlock()->GetBlockId()];
    HInstruction* heap_value = heap_values[idx];
    if (heap_value == kDefaultHeapValue) {
      HInstruction* constant = GetDefaultValue(instruction->GetType());
      removed_loads_.push_back(instruction);
      substitute_instructions_for_loads_.push_back(constant);
      heap_values[idx] = constant;
      return;
    }
    if (heap_value != kUnknownHeapValue) {
      if (heap_value->IsInstanceFieldSet() || heap_value->IsArraySet()) {
        HInstruction* store = heap_value;
        // This load must be from a singleton since it's from the same
        // field/element that a "removed" store puts the value. That store
        // must be to a singleton's field/element.
        DCHECK(ref_info->IsSingleton());
        // Get the real heap value of the store.
        heap_value = heap_value->IsInstanceFieldSet() ? store->InputAt(1) : store->InputAt(2);
      }
    }
    if (heap_value == kUnknownHeapValue) {
      // Load isn't eliminated. Put the load as the value into the HeapLocation.
      // This acts like GVN but with better aliasing analysis.
      heap_values[idx] = instruction;
    } else {
      if (Primitive::PrimitiveKind(heap_value->GetType())
              != Primitive::PrimitiveKind(instruction->GetType())) {
        // The only situation where the same heap location has different type is when
        // we do an array get on an instruction that originates from the null constant
        // (the null could be behind a field access, an array access, a null check or
        // a bound type).
        // In order to stay properly typed on primitive types, we do not eliminate
        // the array gets.
        if (kIsDebugBuild) {
          DCHECK(heap_value->IsArrayGet()) << heap_value->DebugName();
          DCHECK(instruction->IsArrayGet()) << instruction->DebugName();
        }
        return;
      }
      removed_loads_.push_back(instruction);
      substitute_instructions_for_loads_.push_back(heap_value);
      TryRemovingNullCheck(instruction);
    }
  }

  bool Equal(HInstruction* heap_value, HInstruction* value) {
    if (heap_value == value) {
      return true;
    }
    if (heap_value == kDefaultHeapValue && GetDefaultValue(value->GetType()) == value) {
      return true;
    }
    return false;
  }

  void VisitSetLocation(HInstruction* instruction,
                        HInstruction* ref,
                        size_t offset,
                        HInstruction* index,
                        int16_t declaring_class_def_index,
                        HInstruction* value) {
    HInstruction* original_ref = heap_location_collector_.HuntForOriginalReference(ref);
    ReferenceInfo* ref_info = heap_location_collector_.FindReferenceInfoOf(original_ref);
    size_t idx = heap_location_collector_.FindHeapLocationIndex(
        ref_info, offset, index, declaring_class_def_index);
    DCHECK_NE(idx, HeapLocationCollector::kHeapLocationNotFound);
    ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[instruction->GetBlock()->GetBlockId()];
    HInstruction* heap_value = heap_values[idx];
    bool same_value = false;
    bool possibly_redundant = false;
    if (Equal(heap_value, value)) {
      // Store into the heap location with the same value.
      same_value = true;
    } else if (index != nullptr && ref_info->HasIndexAliasing()) {
      // For array element, don't eliminate stores if the index can be aliased.
    } else if (ref_info->IsSingleton()) {
      // Store into a field of a singleton. The value cannot be killed due to
      // aliasing/invocation. It can be redundant since future loads can
      // directly get the value set by this instruction. The value can still be killed due to
      // merging or loop side effects. Stores whose values are killed due to merging/loop side
      // effects later will be removed from possibly_removed_stores_ when that is detected.
      // Stores whose values may be needed after method return or deoptimization
      // are also removed from possibly_removed_stores_ when that is detected.
      possibly_redundant = true;
      HNewInstance* new_instance = ref_info->GetReference()->AsNewInstance();
      if (new_instance != nullptr && new_instance->IsFinalizable()) {
        // Finalizable objects escape globally. Need to keep the store.
        possibly_redundant = false;
      } else {
        HLoopInformation* loop_info = instruction->GetBlock()->GetLoopInformation();
        if (loop_info != nullptr) {
          // instruction is a store in the loop so the loop must does write.
          DCHECK(side_effects_.GetLoopEffects(loop_info->GetHeader()).DoesAnyWrite());

          if (loop_info->IsDefinedOutOfTheLoop(original_ref)) {
            DCHECK(original_ref->GetBlock()->Dominates(loop_info->GetPreHeader()));
            // Keep the store since its value may be needed at the loop header.
            possibly_redundant = false;
          } else {
            // The singleton is created inside the loop. Value stored to it isn't needed at
            // the loop header. This is true for outer loops also.
          }
        }
      }
    }
    if (same_value || possibly_redundant) {
      possibly_removed_stores_.push_back(instruction);
    }

    if (!same_value) {
      if (possibly_redundant) {
        DCHECK(instruction->IsInstanceFieldSet() || instruction->IsArraySet());
        // Put the store as the heap value. If the value is loaded from heap
        // by a load later, this store isn't really redundant.
        heap_values[idx] = instruction;
      } else {
        heap_values[idx] = value;
      }
    }
    // This store may kill values in other heap locations due to aliasing.
    for (size_t i = 0; i < heap_values.size(); i++) {
      if (i == idx) {
        continue;
      }
      if (heap_values[i] == value) {
        // Same value should be kept even if aliasing happens.
        continue;
      }
      if (heap_values[i] == kUnknownHeapValue) {
        // Value is already unknown, no need for aliasing check.
        continue;
      }
      if (heap_location_collector_.MayAlias(i, idx)) {
        // Kill heap locations that may alias.
        heap_values[i] = kUnknownHeapValue;
      }
    }
  }

  void VisitInstanceFieldGet(HInstanceFieldGet* instruction) OVERRIDE {
    HInstruction* obj = instruction->InputAt(0);
    size_t offset = instruction->GetFieldInfo().GetFieldOffset().SizeValue();
    int16_t declaring_class_def_index = instruction->GetFieldInfo().GetDeclaringClassDefIndex();
    VisitGetLocation(instruction, obj, offset, nullptr, declaring_class_def_index);
  }

  void VisitInstanceFieldSet(HInstanceFieldSet* instruction) OVERRIDE {
    HInstruction* obj = instruction->InputAt(0);
    size_t offset = instruction->GetFieldInfo().GetFieldOffset().SizeValue();
    int16_t declaring_class_def_index = instruction->GetFieldInfo().GetDeclaringClassDefIndex();
    HInstruction* value = instruction->InputAt(1);
    VisitSetLocation(instruction, obj, offset, nullptr, declaring_class_def_index, value);
  }

  void VisitStaticFieldGet(HStaticFieldGet* instruction) OVERRIDE {
    HInstruction* cls = instruction->InputAt(0);
    size_t offset = instruction->GetFieldInfo().GetFieldOffset().SizeValue();
    int16_t declaring_class_def_index = instruction->GetFieldInfo().GetDeclaringClassDefIndex();
    VisitGetLocation(instruction, cls, offset, nullptr, declaring_class_def_index);
  }

  void VisitStaticFieldSet(HStaticFieldSet* instruction) OVERRIDE {
    HInstruction* cls = instruction->InputAt(0);
    size_t offset = instruction->GetFieldInfo().GetFieldOffset().SizeValue();
    int16_t declaring_class_def_index = instruction->GetFieldInfo().GetDeclaringClassDefIndex();
    HInstruction* value = instruction->InputAt(1);
    VisitSetLocation(instruction, cls, offset, nullptr, declaring_class_def_index, value);
  }

  void VisitArrayGet(HArrayGet* instruction) OVERRIDE {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    VisitGetLocation(instruction,
                     array,
                     HeapLocation::kInvalidFieldOffset,
                     index,
                     HeapLocation::kDeclaringClassDefIndexForArrays);
  }

  void VisitArraySet(HArraySet* instruction) OVERRIDE {
    HInstruction* array = instruction->InputAt(0);
    HInstruction* index = instruction->InputAt(1);
    HInstruction* value = instruction->InputAt(2);
    VisitSetLocation(instruction,
                     array,
                     HeapLocation::kInvalidFieldOffset,
                     index,
                     HeapLocation::kDeclaringClassDefIndexForArrays,
                     value);
  }

  void VisitDeoptimize(HDeoptimize* instruction) {
    const ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[instruction->GetBlock()->GetBlockId()];
    for (HInstruction* heap_value : heap_values) {
      // Filter out fake instructions before checking instruction kind below.
      if (heap_value == kUnknownHeapValue || heap_value == kDefaultHeapValue) {
        continue;
      }
      // A store is kept as the heap value for possibly removed stores.
      if (heap_value->IsInstanceFieldSet() || heap_value->IsArraySet()) {
        // Check whether the reference for a store is used by an environment local of
        // HDeoptimize.
        HInstruction* reference = heap_value->InputAt(0);
        DCHECK(heap_location_collector_.FindReferenceInfoOf(reference)->IsSingleton());
        for (const HUseListNode<HEnvironment*>& use : reference->GetEnvUses()) {
          HEnvironment* user = use.GetUser();
          if (user->GetHolder() == instruction) {
            // The singleton for the store is visible at this deoptimization
            // point. Need to keep the store so that the heap value is
            // seen by the interpreter.
            KeepIfIsStore(heap_value);
          }
        }
      }
    }
  }

  void HandleInvoke(HInstruction* invoke) {
    ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[invoke->GetBlock()->GetBlockId()];
    for (size_t i = 0; i < heap_values.size(); i++) {
      ReferenceInfo* ref_info = heap_location_collector_.GetHeapLocation(i)->GetReferenceInfo();
      if (ref_info->IsSingleton()) {
        // Singleton references cannot be seen by the callee.
      } else {
        heap_values[i] = kUnknownHeapValue;
      }
    }
  }

  void VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void VisitInvokeVirtual(HInvokeVirtual* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void VisitInvokeInterface(HInvokeInterface* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void VisitInvokeUnresolved(HInvokeUnresolved* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void VisitInvokePolymorphic(HInvokePolymorphic* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void VisitClinitCheck(HClinitCheck* clinit) OVERRIDE {
    HandleInvoke(clinit);
  }

  void VisitUnresolvedInstanceFieldGet(HUnresolvedInstanceFieldGet* instruction) OVERRIDE {
    // Conservatively treat it as an invocation.
    HandleInvoke(instruction);
  }

  void VisitUnresolvedInstanceFieldSet(HUnresolvedInstanceFieldSet* instruction) OVERRIDE {
    // Conservatively treat it as an invocation.
    HandleInvoke(instruction);
  }

  void VisitUnresolvedStaticFieldGet(HUnresolvedStaticFieldGet* instruction) OVERRIDE {
    // Conservatively treat it as an invocation.
    HandleInvoke(instruction);
  }

  void VisitUnresolvedStaticFieldSet(HUnresolvedStaticFieldSet* instruction) OVERRIDE {
    // Conservatively treat it as an invocation.
    HandleInvoke(instruction);
  }

  void VisitNewInstance(HNewInstance* new_instance) OVERRIDE {
    ReferenceInfo* ref_info = heap_location_collector_.FindReferenceInfoOf(new_instance);
    if (ref_info == nullptr) {
      // new_instance isn't used for field accesses. No need to process it.
      return;
    }
    if (ref_info->IsSingletonAndRemovable() &&
        !new_instance->IsFinalizable() &&
        !new_instance->NeedsChecks()) {
      singleton_new_instances_.push_back(new_instance);
    }
    ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[new_instance->GetBlock()->GetBlockId()];
    for (size_t i = 0; i < heap_values.size(); i++) {
      HInstruction* ref =
          heap_location_collector_.GetHeapLocation(i)->GetReferenceInfo()->GetReference();
      size_t offset = heap_location_collector_.GetHeapLocation(i)->GetOffset();
      if (ref == new_instance && offset >= mirror::kObjectHeaderSize) {
        // Instance fields except the header fields are set to default heap values.
        heap_values[i] = kDefaultHeapValue;
      }
    }
  }

  void VisitNewArray(HNewArray* new_array) OVERRIDE {
    ReferenceInfo* ref_info = heap_location_collector_.FindReferenceInfoOf(new_array);
    if (ref_info == nullptr) {
      // new_array isn't used for array accesses. No need to process it.
      return;
    }
    if (ref_info->IsSingletonAndRemovable()) {
      singleton_new_arrays_.push_back(new_array);
    }
    ArenaVector<HInstruction*>& heap_values =
        heap_values_for_[new_array->GetBlock()->GetBlockId()];
    for (size_t i = 0; i < heap_values.size(); i++) {
      HeapLocation* location = heap_location_collector_.GetHeapLocation(i);
      HInstruction* ref = location->GetReferenceInfo()->GetReference();
      if (ref == new_array && location->GetIndex() != nullptr) {
        // Array elements are set to default heap values.
        heap_values[i] = kDefaultHeapValue;
      }
    }
  }

  // Find an instruction's substitute if it should be removed.
  // Return the same instruction if it should not be removed.
  HInstruction* FindSubstitute(HInstruction* instruction) {
    size_t size = removed_loads_.size();
    for (size_t i = 0; i < size; i++) {
      if (removed_loads_[i] == instruction) {
        return substitute_instructions_for_loads_[i];
      }
    }
    return instruction;
  }

  const HeapLocationCollector& heap_location_collector_;
  const SideEffectsAnalysis& side_effects_;

  // One array of heap values for each block.
  ArenaVector<ArenaVector<HInstruction*>> heap_values_for_;

  // We record the instructions that should be eliminated but may be
  // used by heap locations. They'll be removed in the end.
  ArenaVector<HInstruction*> removed_loads_;
  ArenaVector<HInstruction*> substitute_instructions_for_loads_;

  // Stores in this list may be removed from the list later when it's
  // found that the store cannot be eliminated.
  ArenaVector<HInstruction*> possibly_removed_stores_;

  ArenaVector<HInstruction*> singleton_new_instances_;
  ArenaVector<HInstruction*> singleton_new_arrays_;

  DISALLOW_COPY_AND_ASSIGN(LSEVisitor);
};

void LoadStoreElimination::Run() {
  if (graph_->IsDebuggable() || graph_->HasTryCatch()) {
    // Debugger may set heap values or trigger deoptimization of callers.
    // Try/catch support not implemented yet.
    // Skip this optimization.
    return;
  }
  const HeapLocationCollector& heap_location_collector = lsa_.GetHeapLocationCollector();
  if (heap_location_collector.GetNumberOfHeapLocations() == 0) {
    // No HeapLocation information from LSA, skip this optimization.
    return;
  }

  LSEVisitor lse_visitor(graph_, heap_location_collector, side_effects_);
  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    lse_visitor.VisitBasicBlock(block);
  }
  lse_visitor.RemoveInstructions();
}

}  // namespace art
