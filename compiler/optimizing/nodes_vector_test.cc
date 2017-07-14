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

#include "base/arena_allocator.h"
#include "nodes.h"
#include "optimizing_unit_test.h"

namespace art {

/**
 * Fixture class for testing vector nodes.
 */
class NodesVectorTest : public CommonCompilerTest {
 public:
  NodesVectorTest()
      : pool_(),
        allocator_(&pool_),
        graph_(CreateGraph(&allocator_)) {
    BuildGraph();
  }

  ~NodesVectorTest() { }

  void BuildGraph() {
    graph_->SetNumberOfVRegs(1);
    entry_block_ = new (&allocator_) HBasicBlock(graph_);
    exit_block_ = new (&allocator_) HBasicBlock(graph_);
    graph_->AddBlock(entry_block_);
    graph_->AddBlock(exit_block_);
    graph_->SetEntryBlock(entry_block_);
    graph_->SetExitBlock(exit_block_);
    parameter_ = new (&allocator_) HParameterValue(graph_->GetDexFile(),
                                                   dex::TypeIndex(0),
                                                   0,
                                                   Primitive::kPrimInt);
    entry_block_->AddInstruction(parameter_);
  }

  // General building fields.
  ArenaPool pool_;
  ArenaAllocator allocator_;
  HGraph* graph_;

  HBasicBlock* entry_block_;
  HBasicBlock* exit_block_;

  HInstruction* parameter_;
};

//
// The actual vector nodes tests.
//

TEST(NodesVector, Alignment) {
  EXPECT_TRUE(Alignment(1, 0).IsAlignedAt(1));
  EXPECT_FALSE(Alignment(1, 0).IsAlignedAt(2));

  EXPECT_TRUE(Alignment(2, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(2, 1).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(2, 0).IsAlignedAt(2));
  EXPECT_FALSE(Alignment(2, 1).IsAlignedAt(2));
  EXPECT_FALSE(Alignment(2, 0).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(2, 1).IsAlignedAt(4));

  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(4, 2).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(4, 2).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(4, 2).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(4, 0).IsAlignedAt(8));
  EXPECT_FALSE(Alignment(4, 2).IsAlignedAt(8));

  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(4));
  EXPECT_TRUE(Alignment(16, 8).IsAlignedAt(8));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 1).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 7).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 0).IsAlignedAt(32));
}

TEST(NodesVector, AlignmentEQ) {
  EXPECT_TRUE(Alignment(2, 0) == Alignment(2, 0));
  EXPECT_TRUE(Alignment(2, 1) == Alignment(2, 1));
  EXPECT_TRUE(Alignment(4, 0) == Alignment(4, 0));
  EXPECT_TRUE(Alignment(4, 2) == Alignment(4, 2));

  EXPECT_FALSE(Alignment(4, 0) == Alignment(2, 0));
  EXPECT_FALSE(Alignment(4, 0) == Alignment(4, 1));
  EXPECT_FALSE(Alignment(4, 0) == Alignment(8, 0));
}

TEST(NodesVector, AlignmentString) {
  EXPECT_STREQ("ALIGN(1,0)", Alignment(1, 0).ToString().c_str());

  EXPECT_STREQ("ALIGN(2,0)", Alignment(2, 0).ToString().c_str());
  EXPECT_STREQ("ALIGN(2,1)", Alignment(2, 1).ToString().c_str());

  EXPECT_STREQ("ALIGN(16,0)", Alignment(16, 0).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,1)", Alignment(16, 1).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,8)", Alignment(16, 8).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,9)", Alignment(16, 9).ToString().c_str());
}

TEST_F(NodesVectorTest, VectorOperationProperties) {
  HVecOperation* v0 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);
  HVecOperation* v1 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);
  HVecOperation* v2 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 2);
  HVecOperation* v3 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimShort, 4);
  HVecOperation* v4 = new (&allocator_)
      HVecStore(&allocator_, parameter_, parameter_, v0, Primitive::kPrimInt, 4);

  EXPECT_TRUE(v0->Equals(v0));
  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));
  EXPECT_TRUE(v4->Equals(v4));

  EXPECT_TRUE(v0->Equals(v1));
  EXPECT_FALSE(v0->Equals(v2));  // different vector lengths
  EXPECT_FALSE(v0->Equals(v3));  // different packed types
  EXPECT_FALSE(v0->Equals(v4));  // different kinds

  EXPECT_TRUE(v1->Equals(v0));  // switch operands
  EXPECT_FALSE(v4->Equals(v0));

  EXPECT_EQ(4u, v0->GetVectorLength());
  EXPECT_EQ(4u, v1->GetVectorLength());
  EXPECT_EQ(2u, v2->GetVectorLength());
  EXPECT_EQ(4u, v3->GetVectorLength());
  EXPECT_EQ(4u, v4->GetVectorLength());

  EXPECT_EQ(Primitive::kPrimDouble, v0->GetType());
  EXPECT_EQ(Primitive::kPrimDouble, v1->GetType());
  EXPECT_EQ(Primitive::kPrimDouble, v2->GetType());
  EXPECT_EQ(Primitive::kPrimDouble, v3->GetType());
  EXPECT_EQ(Primitive::kPrimDouble, v4->GetType());

  EXPECT_EQ(Primitive::kPrimInt, v0->GetPackedType());
  EXPECT_EQ(Primitive::kPrimInt, v1->GetPackedType());
  EXPECT_EQ(Primitive::kPrimInt, v2->GetPackedType());
  EXPECT_EQ(Primitive::kPrimShort, v3->GetPackedType());
  EXPECT_EQ(Primitive::kPrimInt, v4->GetPackedType());

  EXPECT_EQ(16u, v0->GetVectorNumberOfBytes());
  EXPECT_EQ(16u, v1->GetVectorNumberOfBytes());
  EXPECT_EQ(8u, v2->GetVectorNumberOfBytes());
  EXPECT_EQ(8u, v3->GetVectorNumberOfBytes());
  EXPECT_EQ(16u, v4->GetVectorNumberOfBytes());

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_FALSE(v1->CanBeMoved());
  EXPECT_FALSE(v2->CanBeMoved());
  EXPECT_FALSE(v3->CanBeMoved());
  EXPECT_FALSE(v4->CanBeMoved());
}

TEST_F(NodesVectorTest, VectorAlignmentAndStringCharAtMatterOnLoad) {
  HVecLoad* v0 = new (&allocator_)
      HVecLoad(&allocator_, parameter_, parameter_, Primitive::kPrimInt, 4, /*is_string_char_at*/ false);
  HVecLoad* v1 = new (&allocator_)
      HVecLoad(&allocator_, parameter_, parameter_, Primitive::kPrimInt, 4, /*is_string_char_at*/ false);
  HVecLoad* v2 = new (&allocator_)
      HVecLoad(&allocator_, parameter_, parameter_, Primitive::kPrimInt, 4, /*is_string_char_at*/ true);

  EXPECT_TRUE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());

  EXPECT_FALSE(v0->IsStringCharAt());
  EXPECT_FALSE(v1->IsStringCharAt());
  EXPECT_TRUE(v2->IsStringCharAt());

  EXPECT_TRUE(v0->Equals(v0));
  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));

  EXPECT_TRUE(v0->Equals(v1));
  EXPECT_FALSE(v0->Equals(v2));

  EXPECT_TRUE(v0->GetAlignment() == Alignment(4, 0));
  EXPECT_TRUE(v1->GetAlignment() == Alignment(4, 0));
  EXPECT_TRUE(v2->GetAlignment() == Alignment(4, 0));

  v1->SetAlignment(Alignment(8, 0));

  EXPECT_TRUE(v1->GetAlignment() == Alignment(8, 0));

  EXPECT_FALSE(v0->Equals(v1));  // no longer equal
}

TEST_F(NodesVectorTest, VectorSignMattersOnMin) {
  HVecOperation* v0 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);

  HVecMin* v1 = new (&allocator_)
      HVecMin(&allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ true);
  HVecMin* v2 = new (&allocator_)
      HVecMin(&allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ false);
  HVecMin* v3 = new (&allocator_)
      HVecMin(&allocator_, v0, v0, Primitive::kPrimInt, 2, /*is_unsigned*/ true);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());

  EXPECT_TRUE(v1->IsUnsigned());
  EXPECT_FALSE(v2->IsUnsigned());
  EXPECT_TRUE(v3->IsUnsigned());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));

  EXPECT_FALSE(v1->Equals(v2));  // different signs
  EXPECT_FALSE(v1->Equals(v3));  // different vector lengths
}

TEST_F(NodesVectorTest, VectorSignMattersOnMax) {
  HVecOperation* v0 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);

  HVecMax* v1 = new (&allocator_)
      HVecMax(&allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ true);
  HVecMax* v2 = new (&allocator_)
      HVecMax(&allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ false);
  HVecMax* v3 = new (&allocator_)
      HVecMax(&allocator_, v0, v0, Primitive::kPrimInt, 2, /*is_unsigned*/ true);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());

  EXPECT_TRUE(v1->IsUnsigned());
  EXPECT_FALSE(v2->IsUnsigned());
  EXPECT_TRUE(v3->IsUnsigned());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));

  EXPECT_FALSE(v1->Equals(v2));  // different signs
  EXPECT_FALSE(v1->Equals(v3));  // different vector lengths
}

TEST_F(NodesVectorTest, VectorAttributesMatterOnHalvingAdd) {
  HVecOperation* v0 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);

  HVecHalvingAdd* v1 = new (&allocator_) HVecHalvingAdd(
      &allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ true, /*is_rounded*/ true);
  HVecHalvingAdd* v2 = new (&allocator_) HVecHalvingAdd(
      &allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ true, /*is_rounded*/ false);
  HVecHalvingAdd* v3 = new (&allocator_) HVecHalvingAdd(
      &allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ false, /*is_rounded*/ true);
  HVecHalvingAdd* v4 = new (&allocator_) HVecHalvingAdd(
      &allocator_, v0, v0, Primitive::kPrimInt, 4, /*is_unsigned*/ false, /*is_rounded*/ false);
  HVecHalvingAdd* v5 = new (&allocator_) HVecHalvingAdd(
      &allocator_, v0, v0, Primitive::kPrimInt, 2, /*is_unsigned*/ true, /*is_rounded*/ true);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());
  EXPECT_TRUE(v4->CanBeMoved());
  EXPECT_TRUE(v5->CanBeMoved());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));
  EXPECT_TRUE(v4->Equals(v4));
  EXPECT_TRUE(v5->Equals(v5));

  EXPECT_TRUE(v1->IsUnsigned() && v1->IsRounded());
  EXPECT_TRUE(v2->IsUnsigned() && !v2->IsRounded());
  EXPECT_TRUE(!v3->IsUnsigned() && v3->IsRounded());
  EXPECT_TRUE(!v4->IsUnsigned() && !v4->IsRounded());
  EXPECT_TRUE(v5->IsUnsigned() && v5->IsRounded());

  EXPECT_FALSE(v1->Equals(v2));  // different attributes
  EXPECT_FALSE(v1->Equals(v3));  // different attributes
  EXPECT_FALSE(v1->Equals(v4));  // different attributes
  EXPECT_FALSE(v1->Equals(v5));  // different vector lengths
}

TEST_F(NodesVectorTest, VectorOperationMattersOnMultiplyAccumulate) {
  HVecOperation* v0 = new (&allocator_)
      HVecReplicateScalar(&allocator_, parameter_, Primitive::kPrimInt, 4);

  HVecMultiplyAccumulate* v1 = new (&allocator_)
      HVecMultiplyAccumulate(&allocator_, HInstruction::kAdd, v0, v0, v0, Primitive::kPrimInt, 4);
  HVecMultiplyAccumulate* v2 = new (&allocator_)
      HVecMultiplyAccumulate(&allocator_, HInstruction::kSub, v0, v0, v0, Primitive::kPrimInt, 4);
  HVecMultiplyAccumulate* v3 = new (&allocator_)
      HVecMultiplyAccumulate(&allocator_, HInstruction::kAdd, v0, v0, v0, Primitive::kPrimInt, 2);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());

  EXPECT_EQ(HInstruction::kAdd, v1->GetOpKind());
  EXPECT_EQ(HInstruction::kSub, v2->GetOpKind());
  EXPECT_EQ(HInstruction::kAdd, v3->GetOpKind());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));

  EXPECT_FALSE(v1->Equals(v2));  // different operators
  EXPECT_FALSE(v1->Equals(v3));  // different vector lengths
}

}  // namespace art
