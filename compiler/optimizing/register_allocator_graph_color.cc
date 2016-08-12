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

#include "register_allocator_graph_color.h"

#include "code_generator.h"
#include "register_allocation_resolver.h"
#include "ssa_liveness_analysis.h"
#include "thread-inl.h"

namespace art {

// Highest number of registers that we support for any platform. This can be used for std::bitset,
// for example, which needs to know its size at compile time.
static constexpr size_t kMaxNumRegs = 32;

// The maximum number of graph coloring attempts before triggering a DCHECK.
// This is meant to catch changes to the graph coloring algorithm that undermine its forward
// progress guarantees. Forward progress for the algorithm means splitting live intervals on
// every graph coloring attempt so that eventually the interference graph will be sparse enough
// to color. The main threat to forward progress is trying to split short intervals which cannot be
// split further; this could cause infinite looping because the interference graph would never
// change. This is avoided by prioritizing short intervals before long ones, so that long
// intervals are split when coloring fails.
static constexpr size_t kMaxGraphColoringAttemptsDebug = 100;

// Interference nodes make up the interference graph, which is the primary data structure in
// graph coloring register allocation. Each node represents a single live interval, and contains
// a set of adjacent nodes corresponding to intervals overlapping with its own. To save memory,
// pre-colored nodes never contain outgoing edges (only incoming ones).
//
// As nodes are pruned from the interference graph, incoming edges of the pruned node are removed,
// but outgoing edges remain in order to later color the node based on the colors of its neighbors.
//
// Note that a pair interval is represented by a single node in the interference graph, which
// essentially requires two colors. One consequence of this is that the degree of a node is not
// necessarily equal to the number of adjacent nodes--instead, the degree reflects the maximum
// number of colors with which a node could interfere. We model this by giving edges different
// weights (1 or 2) to control how much it increases the degree of adjacent nodes.
// For example, the edge between two single nodes will have weight 1. On the other hand,
// the edge between a single node and a pair node will have weight 2. This is because the pair
// node could block up to two colors for the single node, and because the single node could
// block an entire two-register aligned slot for the pair node.
// The degree is defined this way because we use it to decide whether a node is guaranteed a color,
// and thus whether it is safe to prune it from the interference graph early on.
class InterferenceNode : public ArenaObject<kArenaAllocRegisterAllocator> {
 public:
  InterferenceNode(ArenaAllocator* allocator, LiveInterval* interval, size_t id)
        : interval_(interval),
          adjacent_nodes_(CmpPtr, allocator->Adapter(kArenaAllocRegisterAllocator)),
          out_degree_(0),
          id_(id) {}

  // Used to maintain determinism when storing InterferenceNode pointers in sets.
  static bool CmpPtr(const InterferenceNode* lhs, const InterferenceNode* rhs) {
    return lhs->id_ < rhs->id_;
  }

  void AddInterference(InterferenceNode* other) {
    if (adjacent_nodes_.insert(other).second) {
      out_degree_ += EdgeWeightWith(other);
    }
  }

  void RemoveInterference(InterferenceNode* other) {
    if (adjacent_nodes_.erase(other) > 0) {
      out_degree_ -= EdgeWeightWith(other);
    }
  }

  bool ContainsInterference(InterferenceNode* other) const {
    return adjacent_nodes_.count(other) > 0;
  }

  LiveInterval* GetInterval() const {
    return interval_;
  }

  const ArenaSet<InterferenceNode*, decltype(&CmpPtr)>& GetAdjacentNodes() const {
    return adjacent_nodes_;
  }

  size_t GetOutDegree() const {
    return out_degree_;
  }

  size_t GetId() const {
    return id_;
  }

 private:
  // We give extra weight to edges adjacent to pair nodes. See the general comment on the
  // interference graph above.
  size_t EdgeWeightWith(InterferenceNode* other) const {
    return (interval_->HasHighInterval() || other->interval_->HasHighInterval()) ? 2 : 1;
  }

  // The live interval that this node represents.
  LiveInterval* const interval_;

  // All nodes interfering with this one.
  // TODO: There is potential to use a cheaper data structure here, especially since
  //       adjacency sets will usually be small.
  ArenaSet<InterferenceNode*, decltype(&CmpPtr)> adjacent_nodes_;

  // The maximum number of colors with which this node could interfere. This could be more than
  // the number of adjacent nodes if this is a pair node, or if some adjacent nodes are pair nodes.
  // We use "out" degree because incoming edges come from nodes already pruned from the graph,
  // and do not affect the coloring of this node.
  size_t out_degree_;

  // A unique identifier for this node, used to maintain determinism when storing
  // interference nodes in sets.
  const size_t id_;

  // TODO: We could cache the result of interval_->RequiresRegister(), since it
  //       will not change for the lifetime of this node. (Currently, RequiresRegister() requires
  //       iterating through all uses of a live interval.)

  DISALLOW_COPY_AND_ASSIGN(InterferenceNode);
};

static bool IsCoreInterval(LiveInterval* interval) {
  return interval->GetType() != Primitive::kPrimFloat
      && interval->GetType() != Primitive::kPrimDouble;
}

static size_t ComputeReservedArtMethodSlots(const CodeGenerator& codegen) {
  return static_cast<size_t>(InstructionSetPointerSize(codegen.GetInstructionSet())) / kVRegSize;
}

RegisterAllocatorGraphColor::RegisterAllocatorGraphColor(ArenaAllocator* allocator,
                                                         CodeGenerator* codegen,
                                                         const SsaLivenessAnalysis& liveness)
      : RegisterAllocator(allocator, codegen, liveness),
        core_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        fp_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        temp_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        safepoints_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        physical_core_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        physical_fp_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        int_spill_slot_counter_(0),
        double_spill_slot_counter_(0),
        float_spill_slot_counter_(0),
        long_spill_slot_counter_(0),
        catch_phi_spill_slot_counter_(0),
        reserved_art_method_slots_(ComputeReservedArtMethodSlots(*codegen)),
        reserved_out_slots_(codegen->GetGraph()->GetMaximumNumberOfOutVRegs()),
        number_of_globally_blocked_core_regs_(0),
        number_of_globally_blocked_fp_regs_(0),
        max_safepoint_live_core_regs_(0),
        max_safepoint_live_fp_regs_(0),
        coloring_attempt_allocator_(nullptr) {
  // Before we ask for blocked registers, set them up in the code generator.
  codegen->SetupBlockedRegisters();

  // Initialize physical core register live intervals and blocked registers.
  // This includes globally blocked registers, such as the stack pointer.
  physical_core_intervals_.resize(codegen->GetNumberOfCoreRegisters(), nullptr);
  for (size_t i = 0; i < codegen->GetNumberOfCoreRegisters(); ++i) {
    LiveInterval* interval = LiveInterval::MakeFixedInterval(allocator_, i, Primitive::kPrimInt);
    physical_core_intervals_[i] = interval;
    core_intervals_.push_back(interval);
    if (codegen_->IsBlockedCoreRegister(i)) {
      ++number_of_globally_blocked_core_regs_;
      interval->AddRange(0, liveness.GetMaxLifetimePosition());
    }
  }
  // Initialize physical floating point register live intervals and blocked registers.
  physical_fp_intervals_.resize(codegen->GetNumberOfFloatingPointRegisters(), nullptr);
  for (size_t i = 0; i < codegen->GetNumberOfFloatingPointRegisters(); ++i) {
    LiveInterval* interval = LiveInterval::MakeFixedInterval(allocator_, i, Primitive::kPrimFloat);
    physical_fp_intervals_[i] = interval;
    fp_intervals_.push_back(interval);
    if (codegen_->IsBlockedFloatingPointRegister(i)) {
      ++number_of_globally_blocked_fp_regs_;
      interval->AddRange(0, liveness.GetMaxLifetimePosition());
    }
  }
}

void RegisterAllocatorGraphColor::AllocateRegisters() {
  // (1) Collect and prepare live intervals.
  ProcessInstructions();

  for (bool processing_core_regs : {true, false}) {
    ArenaVector<LiveInterval*>& intervals = processing_core_regs
        ? core_intervals_
        : fp_intervals_;
    size_t num_registers = processing_core_regs
        ? codegen_->GetNumberOfCoreRegisters()
        : codegen_->GetNumberOfFloatingPointRegisters();

    size_t attempt = 0;
    while (true) {
      ++attempt;
      DCHECK(attempt <= kMaxGraphColoringAttemptsDebug)
          << "Exceeded debug max graph coloring register allocation attempts. "
          << "This could indicate that the register allocator is not making forward progress, "
          << "which could be caused by prioritizing the wrong live intervals. (Short intervals "
          << "should be prioritized over long ones, because they cannot be split further.)";

      // Reset the allocator for the next coloring attempt.
      ArenaAllocator coloring_attempt_allocator(allocator_->GetArenaPool());
      coloring_attempt_allocator_ = &coloring_attempt_allocator;

      // (2) Build the interference graph.
      ArenaVector<InterferenceNode*> prunable_nodes(
          coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));
      ArenaVector<InterferenceNode*> safepoints(
          coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));
      BuildInterferenceGraph(intervals, &prunable_nodes, &safepoints);

      // (3) Prune all uncolored nodes from interference graph.
      ArenaStdStack<InterferenceNode*> pruned_nodes(
          coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));
      PruneInterferenceGraph(prunable_nodes, num_registers, &pruned_nodes);

      // (4) Color pruned nodes based on interferences.
      bool successful = ColorInterferenceGraph(&pruned_nodes, num_registers);

      if (successful) {
        // Compute the maximum number of live registers across safepoints.
        // Notice that we do not count globally blocked registers, such as the stack pointer.
        if (safepoints.size() > 0) {
          size_t max_safepoint_live_regs = ComputeMaxSafepointLiveRegisters(safepoints);
          if (processing_core_regs) {
            max_safepoint_live_core_regs_ =
                max_safepoint_live_regs - number_of_globally_blocked_core_regs_;
          } else {
            max_safepoint_live_fp_regs_=
                max_safepoint_live_regs - number_of_globally_blocked_fp_regs_;
          }
        }

        // Tell the code generator which registers were allocated.
        // We only look at prunable_nodes because we already told the code generator about
        // fixed intervals while processing instructions. We also ignore the fixed intervals
        // placed at the top of catch blocks.
        for (InterferenceNode* node : prunable_nodes) {
          LiveInterval* interval = node->GetInterval();
          if (interval->HasRegister()) {
            Location low_reg = processing_core_regs
                ? Location::RegisterLocation(interval->GetRegister())
                : Location::FpuRegisterLocation(interval->GetRegister());
            codegen_->AddAllocatedRegister(low_reg);
            if (interval->HasHighInterval()) {
              LiveInterval* high = interval->GetHighInterval();
              DCHECK(high->HasRegister());
              Location high_reg = processing_core_regs
                  ? Location::RegisterLocation(high->GetRegister())
                  : Location::FpuRegisterLocation(high->GetRegister());
              codegen_->AddAllocatedRegister(high_reg);
            }
          } else {
            DCHECK(!interval->HasHighInterval() || !interval->GetHighInterval()->HasRegister());
          }
        }

        break;
      }
    }  // while unsuccessful
  }  // for processing_core_instructions

  // (5) Resolve locations and deconstruct SSA form.
  RegisterAllocationResolver(allocator_, codegen_, liveness_)
      .Resolve(max_safepoint_live_core_regs_,
               max_safepoint_live_fp_regs_,
               reserved_art_method_slots_ + reserved_out_slots_,
               int_spill_slot_counter_,
               long_spill_slot_counter_,
               float_spill_slot_counter_,
               double_spill_slot_counter_,
               catch_phi_spill_slot_counter_,
               temp_intervals_);

  if (kIsDebugBuild) {
    Validate(/*log_fatal_on_failure*/ true);
  }
}

bool RegisterAllocatorGraphColor::Validate(bool log_fatal_on_failure) {
  for (bool processing_core_regs : {true, false}) {
    ArenaVector<LiveInterval*> intervals(
        allocator_->Adapter(kArenaAllocRegisterAllocatorValidate));
    for (size_t i = 0; i < liveness_.GetNumberOfSsaValues(); ++i) {
      HInstruction* instruction = liveness_.GetInstructionFromSsaIndex(i);
      LiveInterval* interval = instruction->GetLiveInterval();
      if (interval != nullptr && IsCoreInterval(interval) == processing_core_regs) {
        intervals.push_back(instruction->GetLiveInterval());
      }
    }

    ArenaVector<LiveInterval*>& physical_intervals = processing_core_regs
        ? physical_core_intervals_
        : physical_fp_intervals_;
    for (LiveInterval* fixed : physical_intervals) {
      if (fixed->GetFirstRange() != nullptr) {
        // Ideally we would check fixed ranges as well, but currently there are times when
        // two fixed intervals for the same register will overlap. For example, a fixed input
        // and a fixed output may sometimes share the same register, in which there will be two
        // fixed intervals for the same place.
      }
    }

    for (LiveInterval* temp : temp_intervals_) {
      if (IsCoreInterval(temp) == processing_core_regs) {
        intervals.push_back(temp);
      }
    }

    size_t spill_slots = int_spill_slot_counter_
                       + long_spill_slot_counter_
                       + float_spill_slot_counter_
                       + double_spill_slot_counter_
                       + catch_phi_spill_slot_counter_;
    bool ok = ValidateIntervals(intervals,
                                spill_slots,
                                reserved_art_method_slots_ + reserved_out_slots_,
                                *codegen_,
                                allocator_,
                                processing_core_regs,
                                log_fatal_on_failure);
    if (!ok) {
      return false;
    }
  }  // for processing_core_regs

  return true;
}

void RegisterAllocatorGraphColor::ProcessInstructions() {
  for (HLinearPostOrderIterator it(*codegen_->GetGraph()); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();

    // Note that we currently depend on this ordering, since some helper
    // code is designed for linear scan register allocation.
    for (HBackwardInstructionIterator instr_it(block->GetInstructions());
          !instr_it.Done();
          instr_it.Advance()) {
      ProcessInstruction(instr_it.Current());
    }

    for (HInstructionIterator phi_it(block->GetPhis()); !phi_it.Done(); phi_it.Advance()) {
      ProcessInstruction(phi_it.Current());
    }

    if (block->IsCatchBlock() || (block->IsLoopHeader() && block->GetLoopInformation()->IsIrreducible())) {
      // By blocking all registers at the top of each catch block or irreducible loop, we force
      // intervals belonging to the live-in set of the catch/header block to be spilled.
      // TODO(ngeoffray): Phis in this block could be allocated in register.
      size_t position = block->GetLifetimeStart();
      BlockRegisters(position, position + 1);
    }
  }
}

void RegisterAllocatorGraphColor::ProcessInstruction(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  if (locations == nullptr) {
    return;
  }
  if (locations->NeedsSafepoint() && codegen_->IsLeafMethod()) {
    // We do this here because we do not want the suspend check to artificially
    // create live registers.
    DCHECK(instruction->IsSuspendCheckEntry());
    DCHECK_EQ(locations->GetTempCount(), 0u);
    instruction->GetBlock()->RemoveInstruction(instruction);
    return;
  }

  CheckForTempLiveIntervals(instruction);
  CheckForSafepoint(instruction);
  if (instruction->GetLocations()->WillCall()) {
    // If a call will happen, create fixed intervals for caller-save registers.
    // TODO: Note that it may be beneficial to later split intervals at this point,
    //       so that we allow last-minute moves from a caller-save register
    //       to a callee-save register.
    BlockRegisters(instruction->GetLifetimePosition(),
                   instruction->GetLifetimePosition() + 1,
                   /*caller_save_only*/ true);
  }
  CheckForFixedInputs(instruction);

  LiveInterval* interval = instruction->GetLiveInterval();
  if (interval == nullptr) {
    // Instructions lacking a valid output location do not have a live interval.
    DCHECK(!locations->Out().IsValid());
    return;
  }

  // Low intervals act as representatives for their corresponding high interval.
  DCHECK(!interval->IsHighInterval());
  if (codegen_->NeedsTwoRegisters(interval->GetType())) {
    interval->AddHighInterval();
  }
  AddSafepointsFor(instruction);
  CheckForFixedOutput(instruction);
  AllocateSpillSlotForCatchPhi(instruction);

  ArenaVector<LiveInterval*>& intervals = IsCoreInterval(interval)
      ? core_intervals_
      : fp_intervals_;
  if (interval->HasSpillSlot() || instruction->IsConstant()) {
    // Note that if an interval already has a spill slot, then its value currently resides
    // in the stack (e.g., parameters). Thus we do not have to allocate a register until its first
    // register use. This is also true for constants, which can be materialized at any point.
    size_t first_register_use = interval->FirstRegisterUse();
    if (first_register_use != kNoLifetime) {
      LiveInterval* split = SplitBetween(interval, interval->GetStart(), first_register_use - 1);
      intervals.push_back(split);
    } else {
      // We won't allocate a register for this value.
    }
  } else {
    intervals.push_back(interval);
  }
}

void RegisterAllocatorGraphColor::CheckForFixedInputs(HInstruction* instruction) {
  // We simply block physical registers where necessary.
  // TODO: Ideally we would coalesce the physical register with the register
  //       allocated to the input value, but this can be tricky if, e.g., there
  //       could be multiple physical register uses of the same value at the
  //       same instruction. Need to think about it more.
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();
  for (size_t i = 0; i < locations->GetInputCount(); ++i) {
    Location input = locations->InAt(i);
    if (input.IsRegister() || input.IsFpuRegister()) {
      BlockRegister(input, position, position + 1);
      codegen_->AddAllocatedRegister(input);
    } else if (input.IsPair()) {
      BlockRegister(input.ToLow(), position, position + 1);
      BlockRegister(input.ToHigh(), position, position + 1);
      codegen_->AddAllocatedRegister(input.ToLow());
      codegen_->AddAllocatedRegister(input.ToHigh());
    }
  }
}

void RegisterAllocatorGraphColor::CheckForFixedOutput(HInstruction* instruction) {
  // If an instruction has a fixed output location, we give the live interval a register and then
  // proactively split it just after the definition point to avoid creating too many interferences
  // with a fixed node.
  LiveInterval* interval = instruction->GetLiveInterval();
  Location out = interval->GetDefinedBy()->GetLocations()->Out();
  size_t position = instruction->GetLifetimePosition();
  DCHECK_GE(interval->GetEnd() - position, 2u);

  if (out.IsUnallocated() && out.GetPolicy() == Location::kSameAsFirstInput) {
    out = instruction->GetLocations()->InAt(0);
  }

  if (out.IsRegister() || out.IsFpuRegister()) {
    interval->SetRegister(out.reg());
    codegen_->AddAllocatedRegister(out);
    Split(interval, position + 1);
  } else if (out.IsPair()) {
    interval->SetRegister(out.low());
    interval->GetHighInterval()->SetRegister(out.high());
    codegen_->AddAllocatedRegister(out.ToLow());
    codegen_->AddAllocatedRegister(out.ToHigh());
    Split(interval, position + 1);
  } else if (out.IsStackSlot() || out.IsDoubleStackSlot()) {
    interval->SetSpillSlot(out.GetStackIndex());
  } else {
    DCHECK(out.IsUnallocated() || out.IsConstant());
  }
}

void RegisterAllocatorGraphColor::AddSafepointsFor(HInstruction* instruction) {
  LiveInterval* interval = instruction->GetLiveInterval();
  for (size_t safepoint_index = safepoints_.size(); safepoint_index > 0; --safepoint_index) {
    HInstruction* safepoint = safepoints_[safepoint_index - 1u];
    size_t safepoint_position = safepoint->GetLifetimePosition();

    // Test that safepoints_ are ordered in the optimal way.
    DCHECK(safepoint_index == safepoints_.size() ||
           safepoints_[safepoint_index]->GetLifetimePosition() < safepoint_position);

    if (safepoint_position == interval->GetStart()) {
      // The safepoint is for this instruction, so the location of the instruction
      // does not need to be saved.
      DCHECK_EQ(safepoint_index, safepoints_.size());
      DCHECK_EQ(safepoint, instruction);
      continue;
    } else if (interval->IsDeadAt(safepoint_position)) {
      break;
    } else if (!interval->Covers(safepoint_position)) {
      // Hole in the interval.
      continue;
    }
    interval->AddSafepoint(safepoint);
  }
}

void RegisterAllocatorGraphColor::CheckForTempLiveIntervals(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();
  for (size_t i = 0; i < locations->GetTempCount(); ++i) {
    Location temp = locations->GetTemp(i);
    if (temp.IsRegister() || temp.IsFpuRegister()) {
      BlockRegister(temp, position, position + 1);
      codegen_->AddAllocatedRegister(temp);
    } else {
      DCHECK(temp.IsUnallocated());
      switch (temp.GetPolicy()) {
        case Location::kRequiresRegister: {
          LiveInterval* interval =
              LiveInterval::MakeTempInterval(allocator_, Primitive::kPrimInt);
          interval->AddTempUse(instruction, i);
          core_intervals_.push_back(interval);
          temp_intervals_.push_back(interval);
          break;
        }

        case Location::kRequiresFpuRegister: {
          LiveInterval* interval =
              LiveInterval::MakeTempInterval(allocator_, Primitive::kPrimDouble);
          interval->AddTempUse(instruction, i);
          fp_intervals_.push_back(interval);
          temp_intervals_.push_back(interval);
          if (codegen_->NeedsTwoRegisters(Primitive::kPrimDouble)) {
            interval->AddHighInterval(/*is_temp*/ true);
            temp_intervals_.push_back(interval->GetHighInterval());
          }
          break;
        }

        default:
          LOG(FATAL) << "Unexpected policy for temporary location "
                     << temp.GetPolicy();
      }
    }
  }
}

void RegisterAllocatorGraphColor::CheckForSafepoint(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();

  if (locations->NeedsSafepoint()) {
    safepoints_.push_back(instruction);
    if (locations->OnlyCallsOnSlowPath()) {
      // We add a synthesized range at this position to record the live registers
      // at this position. Ideally, we could just update the safepoints when locations
      // are updated, but we currently need to know the full stack size before updating
      // locations (because of parameters and the fact that we don't have a frame pointer).
      // And knowing the full stack size requires to know the maximum number of live
      // registers at calls in slow paths.
      // By adding the following interval in the algorithm, we can compute this
      // maximum before updating locations.
      LiveInterval* interval = LiveInterval::MakeSlowPathInterval(allocator_, instruction);
      interval->AddRange(position, position + 1);
      core_intervals_.push_back(interval);
      fp_intervals_.push_back(interval);
    }
  }
}

LiveInterval* RegisterAllocatorGraphColor::TrySplit(LiveInterval* interval, size_t position) {
  if (interval->GetStart() < position && position < interval->GetEnd()) {
    return Split(interval, position);
  } else {
    return interval;
  }
}

void RegisterAllocatorGraphColor::SplitAtRegisterUses(LiveInterval* interval) {
  DCHECK(!interval->IsHighInterval());

  // Split just after a register definition.
  if (interval->IsParent() && interval->DefinitionRequiresRegister()) {
    interval = TrySplit(interval, interval->GetStart() + 1);
  }

  UsePosition* use = interval->GetFirstUse();
  while (use != nullptr && use->GetPosition() < interval->GetStart()) {
    use = use->GetNext();
  }

  // Split around register uses.
  size_t end = interval->GetEnd();
  while (use != nullptr && use->GetPosition() <= end) {
    if (use->RequiresRegister()) {
      size_t position = use->GetPosition();
      interval = TrySplit(interval, position - 1);
      if (liveness_.GetInstructionFromPosition(position / 2)->IsControlFlow()) {
        // If we are at the very end of a basic block, we cannot split right
        // at the use. Split just after instead.
        interval = TrySplit(interval, position + 1);
      } else {
        interval = TrySplit(interval, position);
      }
    }
    use = use->GetNext();
  }
}

void RegisterAllocatorGraphColor::AllocateSpillSlotForCatchPhi(HInstruction* instruction) {
  if (instruction->IsPhi() && instruction->AsPhi()->IsCatchPhi()) {
    HPhi* phi = instruction->AsPhi();
    LiveInterval* interval = phi->GetLiveInterval();

    HInstruction* previous_phi = phi->GetPrevious();
    DCHECK(previous_phi == nullptr ||
           previous_phi->AsPhi()->GetRegNumber() <= phi->GetRegNumber())
        << "Phis expected to be sorted by vreg number, "
        << "so that equivalent phis are adjacent.";

    if (phi->IsVRegEquivalentOf(previous_phi)) {
      // Assign the same spill slot.
      DCHECK(previous_phi->GetLiveInterval()->HasSpillSlot());
      interval->SetSpillSlot(previous_phi->GetLiveInterval()->GetSpillSlot());
    } else {
      interval->SetSpillSlot(catch_phi_spill_slot_counter_);
      catch_phi_spill_slot_counter_ += interval->NeedsTwoSpillSlots() ? 2 : 1;
    }
  }
}

void RegisterAllocatorGraphColor::BlockRegister(Location location,
                                                size_t start,
                                                size_t end) {
  DCHECK(location.IsRegister() || location.IsFpuRegister());
  int reg = location.reg();
  LiveInterval* interval = location.IsRegister()
      ? physical_core_intervals_[reg]
      : physical_fp_intervals_[reg];
  DCHECK(interval->GetRegister() == reg);
  bool blocked_by_codegen = location.IsRegister()
      ? codegen_->IsBlockedCoreRegister(reg)
      : codegen_->IsBlockedFloatingPointRegister(reg);
  if (blocked_by_codegen) {
    // We've already blocked this register for the entire method. (And adding a
    // range inside another range violates the preconditions of AddRange).
  } else {
    interval->AddRange(start, end);
  }
}

void RegisterAllocatorGraphColor::BlockRegisters(size_t start, size_t end, bool caller_save_only) {
  for (size_t i = 0; i < codegen_->GetNumberOfCoreRegisters(); ++i) {
    if (!caller_save_only || !codegen_->IsCoreCalleeSaveRegister(i)) {
      BlockRegister(Location::RegisterLocation(i), start, end);
    }
  }
  for (size_t i = 0; i < codegen_->GetNumberOfFloatingPointRegisters(); ++i) {
    if (!caller_save_only || !codegen_->IsFloatingPointCalleeSaveRegister(i)) {
      BlockRegister(Location::FpuRegisterLocation(i), start, end);
    }
  }
}

// Add an interference edge, but only if necessary.
static void AddPotentialInterference(InterferenceNode* from, InterferenceNode* to) {
  if (from->GetInterval()->HasRegister()) {
    // We save space by ignoring outgoing edges from fixed nodes.
  } else if (to->GetInterval()->IsSlowPathSafepoint()) {
    // Safepoint intervals are only there to count max live registers,
    // so no need to give them incoming interference edges.
    // This is also necessary for correctness, because we don't want nodes
    // to remove themselves from safepoint adjacency sets when they're pruned.
  } else {
    from->AddInterference(to);
  }
}

// TODO: See locations->OutputCanOverlapWithInputs(); we may want to consider
//       this when building the interference graph.
void RegisterAllocatorGraphColor::BuildInterferenceGraph(
    const ArenaVector<LiveInterval*>& intervals,
    ArenaVector<InterferenceNode*>* prunable_nodes,
    ArenaVector<InterferenceNode*>* safepoints) {
  size_t interval_id_counter = 0;

  // Build the interference graph efficiently by ordering range endpoints
  // by position and doing a linear sweep to find interferences. (That is, we
  // jump from endpoint to endpoint, maintaining a set of intervals live at each
  // point. If two nodes are ever in the live set at the same time, then they
  // interfere with each other.)
  //
  // We order by both position and (secondarily) by whether the endpoint
  // begins or ends a range; we want to process range endings before range
  // beginnings at the same position because they should not conflict.
  //
  // For simplicity, we create a tuple for each endpoint, and then sort the tuples.
  // Tuple contents: (position, is_range_beginning, node).
  ArenaVector<std::tuple<size_t, bool, InterferenceNode*>> range_endpoints(
      coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));
  for (LiveInterval* parent : intervals) {
    for (LiveInterval* sibling = parent; sibling != nullptr; sibling = sibling->GetNextSibling()) {
      LiveRange* range = sibling->GetFirstRange();
      if (range != nullptr) {
        InterferenceNode* node = new (coloring_attempt_allocator_) InterferenceNode(
            coloring_attempt_allocator_, sibling, interval_id_counter++);
        if (sibling->HasRegister()) {
          // Fixed nodes will never be pruned, so no need to keep track of them.
        } else if (sibling->IsSlowPathSafepoint()) {
          // Safepoint intervals are synthesized to count max live registers.
          // They will be processed separately after coloring.
          safepoints->push_back(node);
        } else {
          prunable_nodes->push_back(node);
        }

        while (range != nullptr) {
          range_endpoints.push_back(std::make_tuple(range->GetStart(), true, node));
          range_endpoints.push_back(std::make_tuple(range->GetEnd(), false, node));
          range = range->GetNext();
        }
      }
    }
  }

  // Sort the endpoints.
  std::sort(range_endpoints.begin(), range_endpoints.end());

  // Nodes live at the current position in the linear sweep.
  ArenaSet<InterferenceNode*, decltype(&InterferenceNode::CmpPtr)> live(
      InterferenceNode::CmpPtr, coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));

  // Linear sweep. When we encounter the beginning of a range, we add the corresponding node to the
  // live set. When we encounter the end of a range, we remove the corresponding node
  // from the live set. Nodes interfere if they are in the live set at the same time.
  for (auto it = range_endpoints.begin(); it != range_endpoints.end(); ++it) {
    bool is_range_beginning;
    InterferenceNode* node;
    // Extract information from the tuple, including the node this tuple represents.
    std::tie(std::ignore, is_range_beginning, node) = *it;

    if (is_range_beginning) {
      for (InterferenceNode* conflicting : live) {
        DCHECK_NE(node, conflicting);
        AddPotentialInterference(node, conflicting);
        AddPotentialInterference(conflicting, node);
      }
      DCHECK_EQ(live.count(node), 0u);
      live.insert(node);
    } else {
      // End of range.
      DCHECK_EQ(live.count(node), 1u);
      live.erase(node);
    }
  }
  DCHECK(live.empty());
}

// The order in which we color nodes is vital to both correctness (forward
// progress) and code quality. Specifically, we must prioritize intervals
// that require registers, and after that we must prioritize short intervals.
// That way, if we fail to color a node, it either won't require a register,
// or it will be a long interval that can be split in order to make the
// interference graph sparser.
// TODO: May also want to consider:
// - Loop depth
// - Constants (since they can be rematerialized)
// - Allocated spill slots
static bool GreaterNodePriority(const InterferenceNode* lhs,
                                const InterferenceNode* rhs) {
  LiveInterval* lhs_interval = lhs->GetInterval();
  LiveInterval* rhs_interval = rhs->GetInterval();

  // (1) Choose the interval that requires a register.
  if (lhs_interval->RequiresRegister() != rhs_interval->RequiresRegister()) {
    return lhs_interval->RequiresRegister();
  }

  // (2) Choose the interval that has a shorter life span.
  if (lhs_interval->GetLength() != rhs_interval->GetLength()) {
    return lhs_interval->GetLength() < rhs_interval->GetLength();
  }

  // (3) Just choose the interval based on a deterministic ordering.
  return InterferenceNode::CmpPtr(lhs, rhs);
}

void RegisterAllocatorGraphColor::PruneInterferenceGraph(
      const ArenaVector<InterferenceNode*>& prunable_nodes,
      size_t num_regs,
      ArenaStdStack<InterferenceNode*>* pruned_nodes) {
  // When pruning the graph, we refer to nodes with degree less than num_regs as low degree nodes,
  // and all others as high degree nodes. The distinction is important: low degree nodes are
  // guaranteed a color, while high degree nodes are not.

  // Low-degree nodes are guaranteed a color, so worklist order does not matter.
  ArenaDeque<InterferenceNode*> low_degree_worklist(
      coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));

  // If we have to prune from the high-degree worklist, we cannot guarantee
  // the pruned node a color. So, we order the worklist by priority.
  ArenaSet<InterferenceNode*, decltype(&GreaterNodePriority)> high_degree_worklist(
      GreaterNodePriority, coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));

  // Build worklists.
  for (InterferenceNode* node : prunable_nodes) {
    DCHECK(!node->GetInterval()->HasRegister())
        << "Fixed nodes should never be pruned";
    DCHECK(!node->GetInterval()->IsSlowPathSafepoint())
        << "Safepoint nodes should never be pruned";
    if (node->GetOutDegree() < num_regs) {
      low_degree_worklist.push_back(node);
    } else {
      high_degree_worklist.insert(node);
    }
  }

  // Helper function to prune an interval from the interference graph,
  // which includes updating the worklists.
  auto prune_node = [this,
                     num_regs,
                     &pruned_nodes,
                     &low_degree_worklist,
                     &high_degree_worklist] (InterferenceNode* node) {
    DCHECK(!node->GetInterval()->HasRegister());
    pruned_nodes->push(node);
    for (InterferenceNode* adjacent : node->GetAdjacentNodes()) {
      DCHECK(!adjacent->GetInterval()->IsSlowPathSafepoint())
          << "Nodes should never interfere with synthesized safepoint nodes";
      if (adjacent->GetInterval()->HasRegister()) {
        // No effect on pre-colored nodes; they're never pruned.
      } else {
        bool was_high_degree = adjacent->GetOutDegree() >= num_regs;
        DCHECK(adjacent->ContainsInterference(node))
            << "Missing incoming interference edge from non-fixed node";
        adjacent->RemoveInterference(node);
        if (was_high_degree && adjacent->GetOutDegree() < num_regs) {
          // This is a transition from high degree to low degree.
          DCHECK_EQ(high_degree_worklist.count(adjacent), 1u);
          high_degree_worklist.erase(adjacent);
          low_degree_worklist.push_back(adjacent);
        }
      }
    }
  };

  // Prune graph.
  while (!low_degree_worklist.empty() || !high_degree_worklist.empty()) {
    while (!low_degree_worklist.empty()) {
      InterferenceNode* node = low_degree_worklist.front();
      // TODO: pop_back() should work as well, but it doesn't; we get a
      //       failed check while pruning. We should look into this.
      low_degree_worklist.pop_front();
      prune_node(node);
    }
    if (!high_degree_worklist.empty()) {
      // We prune the lowest-priority node, because pruning a node earlier
      // gives it a higher chance of being spilled.
      InterferenceNode* node = *high_degree_worklist.rbegin();
      high_degree_worklist.erase(node);
      prune_node(node);
    }
  }
}

// Build a mask with a bit set for each register assigned to some
// interval in `intervals`.
template <typename Container>
static std::bitset<kMaxNumRegs> BuildConflictMask(Container& intervals) {
  std::bitset<kMaxNumRegs> conflict_mask;
  for (InterferenceNode* adjacent : intervals) {
    LiveInterval* conflicting = adjacent->GetInterval();
    if (conflicting->HasRegister()) {
      conflict_mask.set(conflicting->GetRegister());
      if (conflicting->HasHighInterval()) {
        DCHECK(conflicting->GetHighInterval()->HasRegister());
        conflict_mask.set(conflicting->GetHighInterval()->GetRegister());
      }
    } else {
      DCHECK(!conflicting->HasHighInterval()
          || !conflicting->GetHighInterval()->HasRegister());
    }
  }
  return conflict_mask;
}

bool RegisterAllocatorGraphColor::ColorInterferenceGraph(
      ArenaStdStack<InterferenceNode*>* pruned_nodes,
      size_t num_regs) {
  DCHECK_LE(num_regs, kMaxNumRegs) << "kMaxNumRegs is too small";
  ArenaVector<LiveInterval*> colored_intervals(
      coloring_attempt_allocator_->Adapter(kArenaAllocRegisterAllocator));
  bool successful = true;

  while (!pruned_nodes->empty()) {
    InterferenceNode* node = pruned_nodes->top();
    pruned_nodes->pop();
    LiveInterval* interval = node->GetInterval();

    // Search for free register(s).
    // Note that the graph coloring allocator assumes that pair intervals are aligned here,
    // excluding pre-colored pair intervals (which can currently be unaligned on x86).
    std::bitset<kMaxNumRegs> conflict_mask = BuildConflictMask(node->GetAdjacentNodes());
    size_t reg = 0;
    if (interval->HasHighInterval()) {
      while (reg < num_regs - 1 && (conflict_mask[reg] || conflict_mask[reg + 1])) {
        reg += 2;
      }
    } else {
      // We use CTZ (count trailing zeros) to quickly find the lowest available register.
      // Note that CTZ is undefined for 0, so we special-case it.
      reg = conflict_mask.all() ? conflict_mask.size() : CTZ(~conflict_mask.to_ulong());
    }

    if (reg < (interval->HasHighInterval() ? num_regs - 1 : num_regs)) {
      // Assign register.
      DCHECK(!interval->HasRegister());
      interval->SetRegister(reg);
      colored_intervals.push_back(interval);
      if (interval->HasHighInterval()) {
        DCHECK(!interval->GetHighInterval()->HasRegister());
        interval->GetHighInterval()->SetRegister(reg + 1);
        colored_intervals.push_back(interval->GetHighInterval());
      }
    } else if (interval->RequiresRegister()) {
      // The interference graph is too dense to color. Make it sparser by
      // splitting this live interval.
      successful = false;
      SplitAtRegisterUses(interval);
      // We continue coloring, because there may be additional intervals that cannot
      // be colored, and that we should split.
    } else {
      // Spill.
      AllocateSpillSlotFor(interval);
    }
  }

  // If unsuccessful, reset all register assignments.
  if (!successful) {
    for (LiveInterval* interval : colored_intervals) {
      interval->ClearRegister();
    }
  }

  return successful;
}

size_t RegisterAllocatorGraphColor::ComputeMaxSafepointLiveRegisters(
    const ArenaVector<InterferenceNode*>& safepoints) {
  size_t max_safepoint_live_regs = 0;
  for (InterferenceNode* safepoint : safepoints) {
    DCHECK(safepoint->GetInterval()->IsSlowPathSafepoint());
    std::bitset<kMaxNumRegs> conflict_mask = BuildConflictMask(safepoint->GetAdjacentNodes());
    size_t live_regs = conflict_mask.count();
    max_safepoint_live_regs = std::max(max_safepoint_live_regs, live_regs);
  }
  return max_safepoint_live_regs;
}

void RegisterAllocatorGraphColor::AllocateSpillSlotFor(LiveInterval* interval) {
  LiveInterval* parent = interval->GetParent();
  HInstruction* defined_by = parent->GetDefinedBy();
  if (parent->HasSpillSlot()) {
    // We already have a spill slot for this value that we can reuse.
  } else if (defined_by->IsParameterValue()) {
    // Parameters already have a stack slot.
    parent->SetSpillSlot(codegen_->GetStackSlotOfParameter(defined_by->AsParameterValue()));
  } else if (defined_by->IsCurrentMethod()) {
    // The current method is always at spill slot 0.
    parent->SetSpillSlot(0);
  } else if (defined_by->IsConstant()) {
    // Constants don't need a spill slot.
  } else {
    // Allocate a spill slot based on type.
    size_t* spill_slot_counter;
    switch (interval->GetType()) {
      case Primitive::kPrimDouble:
        spill_slot_counter = &double_spill_slot_counter_;
        break;
      case Primitive::kPrimLong:
        spill_slot_counter = &long_spill_slot_counter_;
        break;
      case Primitive::kPrimFloat:
        spill_slot_counter = &float_spill_slot_counter_;
        break;
      case Primitive::kPrimNot:
      case Primitive::kPrimInt:
      case Primitive::kPrimChar:
      case Primitive::kPrimByte:
      case Primitive::kPrimBoolean:
      case Primitive::kPrimShort:
        spill_slot_counter = &int_spill_slot_counter_;
        break;
      case Primitive::kPrimVoid:
        LOG(FATAL) << "Unexpected type for interval " << interval->GetType();
        UNREACHABLE();
    }

    parent->SetSpillSlot(*spill_slot_counter);
    *spill_slot_counter += parent->NeedsTwoSpillSlots() ? 2 : 1;
    // TODO: Could color stack slots if we wanted to, even if
    //       it's just a trivial coloring. See the linear scan implementation,
    //       which simply reuses spill slots for values whose live intervals
    //       have already ended.
  }
}

}  // namespace art
