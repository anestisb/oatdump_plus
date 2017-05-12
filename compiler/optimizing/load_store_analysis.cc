/*
 * Copyright (C) 2017 The Android Open Source Project
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

namespace art {

// A cap for the number of heap locations to prevent pathological time/space consumption.
// The number of heap locations for most of the methods stays below this threshold.
constexpr size_t kMaxNumberOfHeapLocations = 32;

// Check if array indices array[idx1 +/- CONST] and array[idx2] MAY alias.
static bool BinaryOpAndIndexMayAlias(const HBinaryOperation* idx1, const HInstruction* idx2) {
  DCHECK(idx1 != nullptr);
  DCHECK(idx2 != nullptr);

  if (!idx1->IsAdd() && !idx1->IsSub()) {
    // We currently only support Add and Sub operations.
    return true;
  }

  HConstant* cst = idx1->GetConstantRight();
  if (cst == nullptr || cst->IsArithmeticZero()) {
    return true;
  }

  if (idx1->GetLeastConstantLeft() == idx2) {
    // for example, array[idx1 + 1] and array[idx1]
    return false;
  }

  return true;
}

// Check if Add and Sub MAY alias when used as indices in arrays.
static bool BinaryOpsMayAlias(const HBinaryOperation* idx1, const HBinaryOperation* idx2) {
  DCHECK(idx1!= nullptr);
  DCHECK(idx2 != nullptr);

  HConstant* idx1_cst = idx1->GetConstantRight();
  HInstruction* idx1_other = idx1->GetLeastConstantLeft();
  HConstant* idx2_cst = idx2->GetConstantRight();
  HInstruction* idx2_other = idx2->GetLeastConstantLeft();

  if (idx1_cst == nullptr || idx1_other == nullptr ||
      idx2_cst == nullptr || idx2_other == nullptr) {
    // We only analyze patterns like [i +/- CONST].
    return true;
  }

  if (idx1_other != idx2_other) {
    // For example, [j+1] and [k+1] MAY alias.
    return true;
  }

  if ((idx1->IsAdd() && idx2->IsAdd()) ||
      (idx1->IsSub() && idx2->IsSub())) {
    return idx1_cst->AsIntConstant()->GetValue() == idx2_cst->AsIntConstant()->GetValue();
  }

  if ((idx1->IsAdd() && idx2->IsSub()) ||
      (idx1->IsSub() && idx2->IsAdd())) {
    // [i + CONST1] and [i - CONST2] MAY alias iff CONST1 == -CONST2.
    // By checking CONST1 == -CONST2, following cases are handled:
    // - Zero constants case [i+0] and [i-0] is handled.
    // - Overflow cases are handled, for example:
    //   [i+0x80000000] and [i-0x80000000];
    //   [i+0x10] and [i-0xFFFFFFF0].
    // - Other cases [i+CONST1] and [i-CONST2] without any overflow is handled.
    return idx1_cst->AsIntConstant()->GetValue() == -(idx2_cst->AsIntConstant()->GetValue());
  }

  // All other cases, MAY alias.
  return true;
}

// The following array index cases are handled:
//   [i] and [i]
//   [CONST1] and [CONST2]
//   [i] and [i+CONST]
//   [i] and [i-CONST]
//   [i+CONST1] and [i+CONST2]
//   [i-CONST1] and [i-CONST2]
//   [i+CONST1] and [i-CONST2]
//   [i-CONST1] and [i+CONST2]
// For other complicated cases, we rely on other passes like GVN and simpilfier
// to optimize these cases before this pass.
// For example: [i+j+k+10] and [i+k+10+j] shall be optimized to [i7+10] and [i7+10].
bool HeapLocationCollector::CanArrayIndicesAlias(const HInstruction* idx1,
                                                 const HInstruction* idx2) const {
  DCHECK(idx1 != nullptr);
  DCHECK(idx2 != nullptr);

  if (idx1 == idx2) {
    // [i] and [i]
    return true;
  }
  if (idx1->IsIntConstant() && idx2->IsIntConstant()) {
    // [CONST1] and [CONST2]
    return idx1->AsIntConstant()->GetValue() == idx2->AsIntConstant()->GetValue();
  }

  if (idx1->IsBinaryOperation() && !BinaryOpAndIndexMayAlias(idx1->AsBinaryOperation(), idx2)) {
    // [i] and [i+/-CONST]
    return false;
  }
  if (idx2->IsBinaryOperation() && !BinaryOpAndIndexMayAlias(idx2->AsBinaryOperation(), idx1)) {
    // [i+/-CONST] and [i]
    return false;
  }

  if (idx1->IsBinaryOperation() && idx2->IsBinaryOperation()) {
    // [i+/-CONST1] and [i+/-CONST2]
    if (!BinaryOpsMayAlias(idx1->AsBinaryOperation(), idx2->AsBinaryOperation())) {
      return false;
    }
  }

  // By default, MAY alias.
  return true;
}

void LoadStoreAnalysis::Run() {
  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    heap_location_collector_.VisitBasicBlock(block);
  }

  if (heap_location_collector_.GetNumberOfHeapLocations() > kMaxNumberOfHeapLocations) {
    // Bail out if there are too many heap locations to deal with.
    heap_location_collector_.CleanUp();
    return;
  }
  if (!heap_location_collector_.HasHeapStores()) {
    // Without heap stores, this pass would act mostly as GVN on heap accesses.
    heap_location_collector_.CleanUp();
    return;
  }
  if (heap_location_collector_.HasVolatile() || heap_location_collector_.HasMonitorOps()) {
    // Don't do load/store elimination if the method has volatile field accesses or
    // monitor operations, for now.
    // TODO: do it right.
    heap_location_collector_.CleanUp();
    return;
  }

  heap_location_collector_.BuildAliasingMatrix();
}

}  // namespace art
