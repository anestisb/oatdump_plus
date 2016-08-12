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

#ifndef ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_GRAPH_COLOR_H_
#define ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_GRAPH_COLOR_H_

#include "arch/instruction_set.h"
#include "base/arena_containers.h"
#include "base/arena_object.h"
#include "base/macros.h"
#include "primitive.h"
#include "register_allocator.h"

namespace art {

class CodeGenerator;
class HBasicBlock;
class HGraph;
class HInstruction;
class HParallelMove;
class Location;
class SsaLivenessAnalysis;
class InterferenceNode;

/**
 * A graph coloring register allocator.
 *
 * The algorithm proceeds as follows:
 * (1) Build an interference graph, where nodes represent live intervals, and edges represent
 *     interferences between two intervals. Coloring this graph with k colors is isomorphic to
 *     finding a valid register assignment with k registers.
 * (2) To color the graph, first prune all nodes with degree less than k, since these nodes are
 *     guaranteed a color. (No matter how we color their adjacent nodes, we can give them a
 *     different color.) As we prune nodes from the graph, more nodes may drop below degree k,
 *     enabling further pruning. The key is to maintain the pruning order in a stack, so that we
 *     can color the nodes in the reverse order.
 *     When there are no more nodes with degree less than k, we start pruning alternate nodes based
 *     on heuristics. Since these nodes are not guaranteed a color, we are careful to
 *     prioritize nodes that require a register. We also prioritize short intervals, because
 *     short intervals cannot be split very much if coloring fails (see below). "Prioritizing"
 *     a node amounts to pruning it later, since it will have fewer interferences if we prune other
 *     nodes first.
 * (3) We color nodes in the reverse order in which we pruned them. If we cannot assign
 *     a node a color, we do one of two things:
 *     - If the node requires a register, we consider the current coloring attempt a failure.
 *       However, we split the node's live interval in order to make the interference graph
 *       sparser, so that future coloring attempts may succeed.
 *     - If the node does not require a register, we simply assign it a location on the stack.
 *
 * A good reference for graph coloring register allocation is
 * "Modern Compiler Implementation in Java" (Andrew W. Appel, 2nd Edition).
 */
class RegisterAllocatorGraphColor : public RegisterAllocator {
 public:
  RegisterAllocatorGraphColor(ArenaAllocator* allocator,
                              CodeGenerator* codegen,
                              const SsaLivenessAnalysis& analysis);
  ~RegisterAllocatorGraphColor() OVERRIDE {}

  void AllocateRegisters() OVERRIDE;

  bool Validate(bool log_fatal_on_failure);

 private:
  // Collect all intervals and prepare for register allocation.
  void ProcessInstructions();
  void ProcessInstruction(HInstruction* instruction);

  // If any inputs require specific registers, block those registers
  // at the position of this instruction.
  void CheckForFixedInputs(HInstruction* instruction);

  // If the output of an instruction requires a specific register, split
  // the interval and assign the register to the first part.
  void CheckForFixedOutput(HInstruction* instruction);

  // Add all applicable safepoints to a live interval.
  // Currently depends on instruction processing order.
  void AddSafepointsFor(HInstruction* instruction);

  // Collect all live intervals associated with the temporary locations
  // needed by an instruction.
  void CheckForTempLiveIntervals(HInstruction* instruction);

  // If a safe point is needed, add a synthesized interval to later record
  // the number of live registers at this point.
  void CheckForSafepoint(HInstruction* instruction);

  // Split an interval, but only if `position` is inside of `interval`.
  // Return either the new interval, or the original interval if not split.
  static LiveInterval* TrySplit(LiveInterval* interval, size_t position);

  // To ensure every graph can be colored, split live intervals
  // at their register defs and uses. This creates short intervals with low
  // degree in the interference graph, which are prioritized during graph
  // coloring.
  void SplitAtRegisterUses(LiveInterval* interval);

  // If the given instruction is a catch phi, give it a spill slot.
  void AllocateSpillSlotForCatchPhi(HInstruction* instruction);

  // Ensure that the given register cannot be allocated for a given range.
  void BlockRegister(Location location, size_t start, size_t end);
  void BlockRegisters(size_t start, size_t end, bool caller_save_only = false);

  // Use the intervals collected from instructions to construct an
  // interference graph mapping intervals to adjacency lists.
  // Also, collect synthesized safepoint nodes, used to keep
  // track of live intervals across safepoints.
  void BuildInterferenceGraph(const ArenaVector<LiveInterval*>& intervals,
                              ArenaVector<InterferenceNode*>* prunable_nodes,
                              ArenaVector<InterferenceNode*>* safepoints);

  // Prune nodes from the interference graph to be colored later. Build
  // a stack (pruned_nodes) containing these intervals in an order determined
  // by various heuristics.
  void PruneInterferenceGraph(const ArenaVector<InterferenceNode*>& prunable_nodes,
                              size_t num_registers,
                              ArenaStdStack<InterferenceNode*>* pruned_nodes);

  // Process pruned_intervals to color the interference graph, spilling when
  // necessary. Return true if successful. Else, split some intervals to make
  // the interference graph sparser.
  bool ColorInterferenceGraph(ArenaStdStack<InterferenceNode*>* pruned_nodes,
                              size_t num_registers);

  // Return the maximum number of registers live at safepoints,
  // based on the outgoing interference edges of safepoint nodes.
  size_t ComputeMaxSafepointLiveRegisters(const ArenaVector<InterferenceNode*>& safepoints);

  // If necessary, add the given interval to the list of spilled intervals,
  // and make sure it's ready to be spilled to the stack.
  void AllocateSpillSlotFor(LiveInterval* interval);

  // Live intervals, split by kind (core and floating point).
  // These should not contain high intervals, as those are represented by
  // the corresponding low interval throughout register allocation.
  ArenaVector<LiveInterval*> core_intervals_;
  ArenaVector<LiveInterval*> fp_intervals_;

  // Intervals for temporaries, saved for special handling in the resolution phase.
  ArenaVector<LiveInterval*> temp_intervals_;

  // Safepoints, saved for special handling while processing instructions.
  ArenaVector<HInstruction*> safepoints_;

  // Live intervals for specific registers. These become pre-colored nodes
  // in the interference graph.
  ArenaVector<LiveInterval*> physical_core_intervals_;
  ArenaVector<LiveInterval*> physical_fp_intervals_;

  // Allocated stack slot counters.
  size_t int_spill_slot_counter_;
  size_t double_spill_slot_counter_;
  size_t float_spill_slot_counter_;
  size_t long_spill_slot_counter_;
  size_t catch_phi_spill_slot_counter_;

  // Number of stack slots needed for the pointer to the current method.
  // This is 1 for 32-bit architectures, and 2 for 64-bit architectures.
  const size_t reserved_art_method_slots_;

  // Number of stack slots needed for outgoing arguments.
  const size_t reserved_out_slots_;

  // The number of globally blocked core and floating point registers, such as the stack pointer.
  size_t number_of_globally_blocked_core_regs_;
  size_t number_of_globally_blocked_fp_regs_;

  // The maximum number of registers live at safe points. Needed by the code generator.
  size_t max_safepoint_live_core_regs_;
  size_t max_safepoint_live_fp_regs_;

  // An arena allocator used for a single graph coloring attempt.
  // Many data structures are cleared between graph coloring attempts, so we reduce
  // total memory usage by using a new arena allocator for each attempt.
  ArenaAllocator* coloring_attempt_allocator_;

  DISALLOW_COPY_AND_ASSIGN(RegisterAllocatorGraphColor);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_GRAPH_COLOR_H_
