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
#include "nodes.h"
#include "optimizing_unit_test.h"

#include "gtest/gtest.h"

namespace art {

class LoadStoreAnalysisTest : public CommonCompilerTest {
 public:
  LoadStoreAnalysisTest() : pool_(), allocator_(&pool_) {
    graph_ = CreateGraph(&allocator_);
  }

  ArenaPool pool_;
  ArenaAllocator allocator_;
  HGraph* graph_;
};

TEST_F(LoadStoreAnalysisTest, ArrayHeapLocations) {
  HBasicBlock* entry = new (&allocator_) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  // entry:
  // array         ParameterValue
  // index         ParameterValue
  // c1            IntConstant
  // c2            IntConstant
  // c3            IntConstant
  // array_get1    ArrayGet [array, c1]
  // array_get2    ArrayGet [array, c2]
  // array_set1    ArraySet [array, c1, c3]
  // array_set2    ArraySet [array, index, c3]
  HInstruction* array = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, Primitive::kPrimNot);
  HInstruction* index = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, Primitive::kPrimInt);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* array_get1 = new (&allocator_) HArrayGet(array, c1, Primitive::kPrimInt, 0);
  HInstruction* array_get2 = new (&allocator_) HArrayGet(array, c2, Primitive::kPrimInt, 0);
  HInstruction* array_set1 = new (&allocator_) HArraySet(array, c1, c3, Primitive::kPrimInt, 0);
  HInstruction* array_set2 = new (&allocator_) HArraySet(array, index, c3, Primitive::kPrimInt, 0);
  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(array_get1);
  entry->AddInstruction(array_get2);
  entry->AddInstruction(array_set1);
  entry->AddInstruction(array_set2);

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  HeapLocationCollector heap_location_collector(graph_);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph_, it must see following heap locations
  // array[c1], array[c2], array[index]; and it should see heap stores.
  heap_location_collector.VisitBasicBlock(entry);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 3U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(array);
  size_t field_off = HeapLocation::kInvalidFieldOffset;
  size_t class_def = HeapLocation::kDeclaringClassDefIndexForArrays;
  size_t loc1 = heap_location_collector.FindHeapLocationIndex(ref, field_off, c1, class_def);
  size_t loc2 = heap_location_collector.FindHeapLocationIndex(ref, field_off, c2, class_def);
  size_t loc3 = heap_location_collector.FindHeapLocationIndex(ref, field_off, index, class_def);
  // must find this reference info for array in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations;
  // and array[1], array[2], array[3] should be different heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc3 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc1 != loc2);
  ASSERT_TRUE(loc2 != loc3);
  ASSERT_TRUE(loc1 != loc3);

  // Test alias relationships after building aliasing matrix.
  // array[1] and array[2] clearly should not alias;
  // array[index] should alias with the others, because index is an unknow value.
  heap_location_collector.BuildAliasingMatrix();
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc3));
}

TEST_F(LoadStoreAnalysisTest, FieldHeapLocations) {
  HBasicBlock* entry = new (&allocator_) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);

  // entry:
  // object              ParameterValue
  // c1                  IntConstant
  // set_field10         InstanceFieldSet [object, c1, 10]
  // get_field10         InstanceFieldGet [object, 10]
  // get_field20         InstanceFieldGet [object, 20]

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* object = new (&allocator_) HParameterValue(graph_->GetDexFile(),
                                                           dex::TypeIndex(0),
                                                           0,
                                                           Primitive::kPrimNot);
  HInstanceFieldSet* set_field10 = new (&allocator_) HInstanceFieldSet(object,
                                                                       c1,
                                                                       nullptr,
                                                                       Primitive::kPrimInt,
                                                                       MemberOffset(10),
                                                                       false,
                                                                       kUnknownFieldIndex,
                                                                       kUnknownClassDefIndex,
                                                                       graph_->GetDexFile(),
                                                                       0);
  HInstanceFieldGet* get_field10 = new (&allocator_) HInstanceFieldGet(object,
                                                                       nullptr,
                                                                       Primitive::kPrimInt,
                                                                       MemberOffset(10),
                                                                       false,
                                                                       kUnknownFieldIndex,
                                                                       kUnknownClassDefIndex,
                                                                       graph_->GetDexFile(),
                                                                       0);
  HInstanceFieldGet* get_field20 = new (&allocator_) HInstanceFieldGet(object,
                                                                       nullptr,
                                                                       Primitive::kPrimInt,
                                                                       MemberOffset(20),
                                                                       false,
                                                                       kUnknownFieldIndex,
                                                                       kUnknownClassDefIndex,
                                                                       graph_->GetDexFile(),
                                                                       0);
  entry->AddInstruction(object);
  entry->AddInstruction(set_field10);
  entry->AddInstruction(get_field10);
  entry->AddInstruction(get_field20);

  // Test HeapLocationCollector initialization.
  // Should be no heap locations, no operations on the heap.
  HeapLocationCollector heap_location_collector(graph_);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 0U);
  ASSERT_FALSE(heap_location_collector.HasHeapStores());

  // Test that after visiting the graph, it must see following heap locations
  // object.field10, object.field20 and it should see heap stores.
  heap_location_collector.VisitBasicBlock(entry);
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 2U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's ref info and index records.
  ReferenceInfo* ref = heap_location_collector.FindReferenceInfoOf(object);
  size_t loc1 = heap_location_collector.FindHeapLocationIndex(
      ref, 10, nullptr, kUnknownClassDefIndex);
  size_t loc2 = heap_location_collector.FindHeapLocationIndex(
      ref, 20, nullptr, kUnknownClassDefIndex);
  // must find references info for object and in HeapLocationCollector.
  ASSERT_TRUE(ref != nullptr);
  // must find these heap locations.
  ASSERT_TRUE(loc1 != HeapLocationCollector::kHeapLocationNotFound);
  ASSERT_TRUE(loc2 != HeapLocationCollector::kHeapLocationNotFound);
  // different fields of same object.
  ASSERT_TRUE(loc1 != loc2);
  // accesses to different fields of the same object should not alias.
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

}  // namespace art
