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
#include "instruction_simplifier_arm.h"
#include "instruction_simplifier_shared.h"
#include "mirror/array-inl.h"

namespace art {
namespace arm {

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

void InstructionSimplifierArmVisitor::VisitAnd(HAnd* instruction) {
  if (TryMergeNegatedInput(instruction)) {
    RecordSimplification();
  }
}

void InstructionSimplifierArmVisitor::VisitArrayGet(HArrayGet* instruction) {
  size_t data_offset = CodeGenerator::GetArrayDataOffset(instruction);
  Primitive::Type type = instruction->GetType();

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

}  // namespace arm
}  // namespace art
