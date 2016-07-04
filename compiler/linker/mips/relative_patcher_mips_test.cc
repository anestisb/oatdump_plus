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

#include "linker/relative_patcher_test.h"
#include "linker/mips/relative_patcher_mips.h"

namespace art {
namespace linker {

// We'll maximize the range of a single load instruction for dex cache array accesses
// by aligning offset -32768 with the offset of the first used element.
static constexpr uint32_t kDexCacheArrayLwOffset = 0x8000;

class MipsRelativePatcherTest : public RelativePatcherTest {
 public:
  MipsRelativePatcherTest() : RelativePatcherTest(kMips, "mips32r2") {}

 protected:
  uint32_t GetMethodOffset(uint32_t method_idx) {
    auto result = method_offset_map_.FindMethodOffset(MethodRef(method_idx));
    CHECK(result.first);
    return result.second;
  }
};

TEST_F(MipsRelativePatcherTest, DexCacheReference) {
  dex_cache_arrays_begin_ = 0x12345678;
  constexpr size_t kElementOffset = 0x1234;
  static const uint8_t raw_code[] = {
      0x00, 0x00, 0x10, 0x04,  // nal
      0x34, 0x12, 0x12, 0x3C,  // lui  s2, high(diff); placeholder = 0x1234
      0x78, 0x56, 0x52, 0x36,  // ori  s2, s2, low(diff); placeholder = 0x5678
      0x21, 0x90, 0x5F, 0x02,  // addu s2, s2, ra
  };
  constexpr uint32_t literal_offset = 4;  // At lui (where patching starts).
  constexpr uint32_t anchor_offset = 8;  // At ori (where PC+0 points).
  ArrayRef<const uint8_t> code(raw_code);
  LinkerPatch patches[] = {
      LinkerPatch::DexCacheArrayPatch(literal_offset, nullptr, anchor_offset, kElementOffset),
  };
  AddCompiledMethod(MethodRef(1u), code, ArrayRef<const LinkerPatch>(patches));
  Link();

  auto result = method_offset_map_.FindMethodOffset(MethodRef(1u));
  ASSERT_TRUE(result.first);
  uint32_t diff = dex_cache_arrays_begin_ + kElementOffset - (result.second + anchor_offset) +
      kDexCacheArrayLwOffset;
  static const uint8_t expected_code[] = {
      0x00, 0x00, 0x10, 0x04,
      static_cast<uint8_t>(diff >> 16), static_cast<uint8_t>(diff >> 24), 0x12, 0x3C,
      static_cast<uint8_t>(diff), static_cast<uint8_t>(diff >> 8), 0x52, 0x36,
      0x21, 0x90, 0x5F, 0x02,
  };
  EXPECT_TRUE(CheckLinkedMethod(MethodRef(1u), ArrayRef<const uint8_t>(expected_code)));
}

}  // namespace linker
}  // namespace art
