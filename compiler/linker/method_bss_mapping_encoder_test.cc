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

#include "method_bss_mapping_encoder.h"

#include "gtest/gtest.h"

namespace art {
namespace linker {

TEST(MethodBssMappingEncoder, TryMerge) {
  for (PointerSize pointer_size : {PointerSize::k32, PointerSize::k64}) {
    size_t raw_pointer_size = static_cast<size_t>(pointer_size);
    MethodBssMappingEncoder encoder(pointer_size);
    encoder.Reset(1u, 0u);
    ASSERT_FALSE(encoder.TryMerge(5u, raw_pointer_size + 1));       // Wrong bss_offset difference.
    ASSERT_FALSE(encoder.TryMerge(18u, raw_pointer_size));          // Method index out of range.
    ASSERT_TRUE(encoder.TryMerge(5u, raw_pointer_size));
    ASSERT_TRUE(encoder.GetEntry().CoversIndex(1u));
    ASSERT_TRUE(encoder.GetEntry().CoversIndex(5u));
    ASSERT_FALSE(encoder.GetEntry().CoversIndex(17u));
    ASSERT_FALSE(encoder.TryMerge(17u, 2 * raw_pointer_size + 1));  // Wrong bss_offset difference.
    ASSERT_FALSE(encoder.TryMerge(18u, 2 * raw_pointer_size));      // Method index out of range.
    ASSERT_TRUE(encoder.TryMerge(17u, 2 * raw_pointer_size));
    ASSERT_TRUE(encoder.GetEntry().CoversIndex(1u));
    ASSERT_TRUE(encoder.GetEntry().CoversIndex(5u));
    ASSERT_TRUE(encoder.GetEntry().CoversIndex(17u));
    ASSERT_EQ(0u, encoder.GetEntry().GetBssOffset(1u, raw_pointer_size));
    ASSERT_EQ(raw_pointer_size, encoder.GetEntry().GetBssOffset(5u, raw_pointer_size));
    ASSERT_EQ(2 * raw_pointer_size, encoder.GetEntry().GetBssOffset(17u, raw_pointer_size));
    ASSERT_EQ(0x0011u, encoder.GetEntry().index_mask);
    ASSERT_FALSE(encoder.TryMerge(18u, 2 * raw_pointer_size));      // Method index out of range.
  }
}

}  // namespace linker
}  // namespace art
