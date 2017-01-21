/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "instruction_set.h"

#include <gtest/gtest.h>

#include "base/enums.h"

namespace art {

TEST(InstructionSetTest, GetInstructionSetFromString) {
  EXPECT_EQ(kArm, GetInstructionSetFromString("arm"));
  EXPECT_EQ(kArm64, GetInstructionSetFromString("arm64"));
  EXPECT_EQ(kX86, GetInstructionSetFromString("x86"));
  EXPECT_EQ(kX86_64, GetInstructionSetFromString("x86_64"));
  EXPECT_EQ(kMips, GetInstructionSetFromString("mips"));
  EXPECT_EQ(kMips64, GetInstructionSetFromString("mips64"));
  EXPECT_EQ(kNone, GetInstructionSetFromString("none"));
  EXPECT_EQ(kNone, GetInstructionSetFromString("random-string"));
}

TEST(InstructionSetTest, GetInstructionSetString) {
  EXPECT_STREQ("arm", GetInstructionSetString(kArm));
  EXPECT_STREQ("arm", GetInstructionSetString(kThumb2));
  EXPECT_STREQ("arm64", GetInstructionSetString(kArm64));
  EXPECT_STREQ("x86", GetInstructionSetString(kX86));
  EXPECT_STREQ("x86_64", GetInstructionSetString(kX86_64));
  EXPECT_STREQ("mips", GetInstructionSetString(kMips));
  EXPECT_STREQ("mips64", GetInstructionSetString(kMips64));
  EXPECT_STREQ("none", GetInstructionSetString(kNone));
}

TEST(InstructionSetTest, GetInstructionSetInstructionAlignment) {
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kThumb2), kThumb2InstructionAlignment);
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kArm64), kArm64InstructionAlignment);
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kX86), kX86InstructionAlignment);
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kX86_64), kX86_64InstructionAlignment);
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kMips), kMipsInstructionAlignment);
  EXPECT_EQ(GetInstructionSetInstructionAlignment(kMips64), kMips64InstructionAlignment);
}

TEST(InstructionSetTest, TestRoundTrip) {
  EXPECT_EQ(kRuntimeISA, GetInstructionSetFromString(GetInstructionSetString(kRuntimeISA)));
}

TEST(InstructionSetTest, PointerSize) {
  EXPECT_EQ(kRuntimePointerSize, GetInstructionSetPointerSize(kRuntimeISA));
}

}  // namespace art
