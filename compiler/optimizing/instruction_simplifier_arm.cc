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

#include "code_generator.h"
#include "common_arm.h"
#include "instruction_simplifier_arm.h"
#include "instruction_simplifier_shared.h"
#include "mirror/array-inl.h"
#include "mirror/string.h"
#include "nodes.h"

namespace art {

using helpers::CanFitInShifterOperand;
using helpers::HasShifterOperand;

namespace arm {

using helpers::ShifterOperandSupportsExtension;

bool InstructionSimplifierArmVisitor::TryMergeIntoShifterOperand(HInstruction* use,
                                                                 HInstruction* bitfield_op,
                                                                 bool do_merge) {
  DCHECK(HasShifterOperand(use, kArm));
  DCHECK(use->IsBinaryOperation());
  DCHECK(CanFitInShifterOperand(bitfield_op));
  DCHECK(!bitfield_op->HasEnvironmentUses());

  Primitive::Type type = use->GetType();
  if (type != Primitive::kPrimInt && type != Primitive::kPrimLong) {
    return false;
  }

  HInstruction* left = use->InputAt(0);
  HInstruction* right = use->InputAt(1);
  DCHECK(left == bitfield_op || right == bitfield_op);

  if (left == right) {
    // TODO: Handle special transformations in this situation?
    // For example should we transform `(x << 1) + (x << 1)` into `(x << 2)`?
    // Or should this be part of a separate transformation logic?
    return false;
  }

  bool is_commutative = use->AsBinaryOperation()->IsCommutative();
  HInstruction* other_input;
  if (bitfield_op == right) {
    other_input = left;
  } else {
    if (is_commutative) {
      other_input = right;
    } else {
      return false;
    }
  }

  HDataProcWithShifterOp::OpKind op_kind;
  int shift_amount = 0;

  HDataProcWithShifterOp::GetOpInfoFromInstruction(bitfield_op, &op_kind, &shift_amount);
  shift_amount &= use->GetType() == Primitive::kPrimInt
      ? kMaxIntShiftDistance
      : kMaxLongShiftDistance;

  if (HDataProcWithShifterOp::IsExtensionOp(op_kind)) {
    if (!ShifterOperandSupportsExtension(use)) {
      return false;
    }
  // Shift by 1 is a special case that results in the same number and type of instructions
  // as this simplification, but potentially shorter code.
  } else if (type == Primitive::kPrimLong && shift_amount == 1) {
    return false;
  }

  if (do_merge) {
    HDataProcWithShifterOp* alu_with_op =
        new (GetGraph()->GetArena()) HDataProcWithShifterOp(use,
                                                            other_input,
                                                            bitfield_op->InputAt(0),
                                                            op_kind,
                                                            shift_amount,
                                                            use->GetDexPc());
    use->GetBlock()->ReplaceAndRemoveInstructionWith(use, alu_with_op);
    if (bitfield_op->GetUses().empty()) {
      bitfield_op->GetBlock()->RemoveInstruction(bitfield_op);
    }
    RecordSimplification();
  }

  return true;
}

// Merge a bitfield move instruction into its uses if it can be merged in all of them.
bool InstructionSimplifierArmVisitor::TryMergeIntoUsersShifterOperand(HInstruction* bitfield_op) {
  DCHECK(CanFitInShifterOperand(bitfield_op));

  if (bitfield_op->HasEnvironmentUses()) {
    return false;
  }

  const HUseList<HInstruction*>& uses = bitfield_op->GetUses();

  // Check whether we can merge the instruction in all its users' shifter operand.
  for (const HUseListNode<HInstruction*>& use : uses) {
    HInstruction* user = use.GetUser();
    if (!HasShifterOperand(user, kArm)) {
      return false;
    }
    if (!CanMergeIntoShifterOperand(user, bitfield_op)) {
      return false;
    }
  }

  // Merge the instruction into its uses.
  for (auto it = uses.begin(), end = uses.end(); it != end; /* ++it below */) {
    HInstruction* user = it->GetUser();
    // Increment `it` now because `*it` will disappear thanks to MergeIntoShifterOperand().
    ++it;
    bool merged = MergeIntoShifterOperand(user, bitfield_op);
    DCHECK(merged);
  }

  return true;
}

void InstructionSimplifierArmVisitor::VisitAnd(HAnd* instruction) {
  if (TryMergeNegatedInput(instruction)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitArrayGet(HArrayGet* instruction) {
  size_t data_offset = CodeGenerator::GetArrayDataOffset(instruction);
  Primitive::Type type = instruction->GetType();

  // TODO: Implement reading (length + compression) for String compression feature from
  // negative offset (count_offset - data_offset). Thumb2Assembler does not support T4
  // encoding of "LDR (immediate)" at the moment.
  // Don't move array pointer if it is charAt because we need to take the count first.
  if (mirror::kUseStringCompression && instruction->IsStringCharAt()) {
    return;
  }

  if (type == Primitive::kPrimLong
      || type == Primitive::kPrimFloat
      || type == Primitive::kPrimDouble) {
    // T32 doesn't support ShiftedRegOffset mem address mode for these types
    // to enable optimization.
    return;
  }

  if (TryExtractArrayAccessAddress(instruction,
                                   instruction->GetArray(),
                                   instruction->GetIndex(),
                                   data_offset)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitArraySet(HArraySet* instruction) {
  size_t access_size = Primitive::ComponentSize(instruction->GetComponentType());
  size_t data_offset = mirror::Array::DataOffset(access_size).Uint32Value();
  Primitive::Type type = instruction->GetComponentType();

  if (type == Primitive::kPrimLong
      || type == Primitive::kPrimFloat
      || type == Primitive::kPrimDouble) {
    // T32 doesn't support ShiftedRegOffset mem address mode for these types
    // to enable optimization.
    return;
  }

  if (TryExtractArrayAccessAddress(instruction,
                                   instruction->GetArray(),
                                   instruction->GetIndex(),
                                   data_offset)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitMul(HMul* instruction) {
  if (TryCombineMultiplyAccumulate(instruction, kArm)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitOr(HOr* instruction) {
  if (TryMergeNegatedInput(instruction)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitShl(HShl* instruction) {
  if (instruction->InputAt(1)->IsConstant()) {
    TryMergeIntoUsersShifterOperand(instruction);
  }
}

void InstructionSimplifierArmVisitor::VisitShr(HShr* instruction) {
  if (instruction->InputAt(1)->IsConstant()) {
    TryMergeIntoUsersShifterOperand(instruction);
  }
}

void InstructionSimplifierArmVisitor::VisitTypeConversion(HTypeConversion* instruction) {
  Primitive::Type result_type = instruction->GetResultType();
  Primitive::Type input_type = instruction->GetInputType();

  if (input_type == result_type) {
    // We let the arch-independent code handle this.
    return;
  }

  if (Primitive::IsIntegralType(result_type) && Primitive::IsIntegralType(input_type)) {
    TryMergeIntoUsersShifterOperand(instruction);
  }
}

void InstructionSimplifierArmVisitor::VisitUShr(HUShr* instruction) {
  if (instruction->InputAt(1)->IsConstant()) {
    TryMergeIntoUsersShifterOperand(instruction);
  }
}

}  // namespace arm
}  // namespace art
