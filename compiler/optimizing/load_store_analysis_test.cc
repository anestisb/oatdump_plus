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

TEST_F(LoadStoreAnalysisTest, ArrayIndexAliasingTest) {
  HBasicBlock* entry = new (&allocator_) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  graph_->BuildDominatorTree();

  HInstruction* array = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, Primitive::kPrimNot);
  HInstruction* index = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, Primitive::kPrimInt);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c_neg1 = graph_->GetIntConstant(-1);
  HInstruction* add0 = new (&allocator_) HAdd(Primitive::kPrimInt, index, c0);
  HInstruction* add1 = new (&allocator_) HAdd(Primitive::kPrimInt, index, c1);
  HInstruction* sub0 = new (&allocator_) HSub(Primitive::kPrimInt, index, c0);
  HInstruction* sub1 = new (&allocator_) HSub(Primitive::kPrimInt, index, c1);
  HInstruction* sub_neg1 = new (&allocator_) HSub(Primitive::kPrimInt, index, c_neg1);
  HInstruction* rev_sub1 = new (&allocator_) HSub(Primitive::kPrimInt, c1, index);
  HInstruction* arr_set1 = new (&allocator_) HArraySet(array, c0, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set2 = new (&allocator_) HArraySet(array, c1, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set3 = new (&allocator_) HArraySet(array, add0, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set4 = new (&allocator_) HArraySet(array, add1, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set5 = new (&allocator_) HArraySet(array, sub0, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set6 = new (&allocator_) HArraySet(array, sub1, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set7 = new (&allocator_) HArraySet(array, rev_sub1, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set8 = new (&allocator_) HArraySet(array, sub_neg1, c0, Primitive::kPrimInt, 0);

  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(add0);
  entry->AddInstruction(add1);
  entry->AddInstruction(sub0);
  entry->AddInstruction(sub1);
  entry->AddInstruction(sub_neg1);
  entry->AddInstruction(rev_sub1);

  entry->AddInstruction(arr_set1);  // array[0] = c0
  entry->AddInstruction(arr_set2);  // array[1] = c0
  entry->AddInstruction(arr_set3);  // array[i+0] = c0
  entry->AddInstruction(arr_set4);  // array[i+1] = c0
  entry->AddInstruction(arr_set5);  // array[i-0] = c0
  entry->AddInstruction(arr_set6);  // array[i-1] = c0
  entry->AddInstruction(arr_set7);  // array[1-i] = c0
  entry->AddInstruction(arr_set8);  // array[i-(-1)] = c0

  LoadStoreAnalysis lsa(graph_);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[0] and array[1]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, c0);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, c1);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add0);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-1]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add1);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub1);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[1-i]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add1);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, rev_sub1);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+1] and array[i-(-1)]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add1);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_neg1);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));
}

TEST_F(LoadStoreAnalysisTest, ArrayIndexCalculationOverflowTest) {
  HBasicBlock* entry = new (&allocator_) HBasicBlock(graph_);
  graph_->AddBlock(entry);
  graph_->SetEntryBlock(entry);
  graph_->BuildDominatorTree();

  HInstruction* array = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(0), 0, Primitive::kPrimNot);
  HInstruction* index = new (&allocator_) HParameterValue(
      graph_->GetDexFile(), dex::TypeIndex(1), 1, Primitive::kPrimInt);

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c_0x80000000 = graph_->GetIntConstant(0x80000000);
  HInstruction* c_0x10 = graph_->GetIntConstant(0x10);
  HInstruction* c_0xFFFFFFF0 = graph_->GetIntConstant(0xFFFFFFF0);
  HInstruction* c_0x7FFFFFFF = graph_->GetIntConstant(0x7FFFFFFF);
  HInstruction* c_0x80000001 = graph_->GetIntConstant(0x80000001);

  // `index+0x80000000` and `index-0x80000000` array indices MAY alias.
  HInstruction* add_0x80000000 = new (&allocator_) HAdd(Primitive::kPrimInt, index, c_0x80000000);
  HInstruction* sub_0x80000000 = new (&allocator_) HSub(Primitive::kPrimInt, index, c_0x80000000);
  HInstruction* arr_set_1 = new (&allocator_) HArraySet(
      array, add_0x80000000, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set_2 = new (&allocator_) HArraySet(
      array, sub_0x80000000, c0, Primitive::kPrimInt, 0);

  // `index+0x10` and `index-0xFFFFFFF0` array indices MAY alias.
  HInstruction* add_0x10 = new (&allocator_) HAdd(Primitive::kPrimInt, index, c_0x10);
  HInstruction* sub_0xFFFFFFF0 = new (&allocator_) HSub(Primitive::kPrimInt, index, c_0xFFFFFFF0);
  HInstruction* arr_set_3 = new (&allocator_) HArraySet(
      array, add_0x10, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set_4 = new (&allocator_) HArraySet(
      array, sub_0xFFFFFFF0, c0, Primitive::kPrimInt, 0);

  // `index+0x7FFFFFFF` and `index-0x80000001` array indices MAY alias.
  HInstruction* add_0x7FFFFFFF = new (&allocator_) HAdd(Primitive::kPrimInt, index, c_0x7FFFFFFF);
  HInstruction* sub_0x80000001 = new (&allocator_) HSub(Primitive::kPrimInt, index, c_0x80000001);
  HInstruction* arr_set_5 = new (&allocator_) HArraySet(
      array, add_0x7FFFFFFF, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set_6 = new (&allocator_) HArraySet(
      array, sub_0x80000001, c0, Primitive::kPrimInt, 0);

  // `index+0` and `index-0` array indices MAY alias.
  HInstruction* add_0 = new (&allocator_) HAdd(Primitive::kPrimInt, index, c0);
  HInstruction* sub_0 = new (&allocator_) HSub(Primitive::kPrimInt, index, c0);
  HInstruction* arr_set_7 = new (&allocator_) HArraySet(array, add_0, c0, Primitive::kPrimInt, 0);
  HInstruction* arr_set_8 = new (&allocator_) HArraySet(array, sub_0, c0, Primitive::kPrimInt, 0);

  entry->AddInstruction(array);
  entry->AddInstruction(index);
  entry->AddInstruction(add_0x80000000);
  entry->AddInstruction(sub_0x80000000);
  entry->AddInstruction(add_0x10);
  entry->AddInstruction(sub_0xFFFFFFF0);
  entry->AddInstruction(add_0x7FFFFFFF);
  entry->AddInstruction(sub_0x80000001);
  entry->AddInstruction(add_0);
  entry->AddInstruction(sub_0);
  entry->AddInstruction(arr_set_1);
  entry->AddInstruction(arr_set_2);
  entry->AddInstruction(arr_set_3);
  entry->AddInstruction(arr_set_4);
  entry->AddInstruction(arr_set_5);
  entry->AddInstruction(arr_set_6);
  entry->AddInstruction(arr_set_7);
  entry->AddInstruction(arr_set_8);

  LoadStoreAnalysis lsa(graph_);
  lsa.Run();
  const HeapLocationCollector& heap_location_collector = lsa.GetHeapLocationCollector();

  // LSA/HeapLocationCollector should see those ArrayGet instructions.
  ASSERT_EQ(heap_location_collector.GetNumberOfHeapLocations(), 8U);
  ASSERT_TRUE(heap_location_collector.HasHeapStores());

  // Test queries on HeapLocationCollector's aliasing matrix after load store analysis.
  size_t loc1 = HeapLocationCollector::kHeapLocationNotFound;
  size_t loc2 = HeapLocationCollector::kHeapLocationNotFound;

  // Test alias: array[i+0x80000000] and array[i-0x80000000]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add_0x80000000);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0x80000000);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x10] and array[i-0xFFFFFFF0]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add_0x10);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0xFFFFFFF0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0x7FFFFFFF] and array[i-0x80000001]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add_0x7FFFFFFF);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0x80000001);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Test alias: array[i+0] and array[i-0]
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add_0);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0);
  ASSERT_TRUE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0x80000000);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0x80000001);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));

  // Should not alias:
  loc1 = heap_location_collector.GetArrayAccessHeapLocation(array, add_0);
  loc2 = heap_location_collector.GetArrayAccessHeapLocation(array, sub_0x80000000);
  ASSERT_FALSE(heap_location_collector.MayAlias(loc1, loc2));
}

}  // namespace art
