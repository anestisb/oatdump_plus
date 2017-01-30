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

class Mips32r6RelativePatcherTest : public RelativePatcherTest {
 public:
  Mips32r6RelativePatcherTest() : RelativePatcherTest(kMips, "mips32r6") {}

 protected:
  static const uint8_t kUnpatchedPcRelativeRawCode[];
  static const uint32_t kLiteralOffset;
  static const uint32_t kAnchorOffset;
  static const ArrayRef<const uint8_t> kUnpatchedPcRelativeCode;

  uint32_t GetMethodOffset(uint32_t method_idx) {
    auto result = method_offset_map_.FindMethodOffset(MethodRef(method_idx));
    CHECK(result.first);
    return result.second;
  }

  void CheckPcRelativePatch(const ArrayRef<const LinkerPatch>& patches, uint32_t target_offset);
  void TestDexCacheReference(uint32_t dex_cache_arrays_begin, uint32_t element_offset);
  void TestStringReference(uint32_t string_offset);
};

const uint8_t Mips32r6RelativePatcherTest::kUnpatchedPcRelativeRawCode[] = {
    0x34, 0x12, 0x5E, 0xEE,  // auipc s2, high(diff); placeholder = 0x1234
    0x78, 0x56, 0x52, 0x26,  // addiu s2, s2, low(diff); placeholder = 0x5678
};
const uint32_t Mips32r6RelativePatcherTest::kLiteralOffset = 0;  // At auipc (where
                                                                 // patching starts).
const uint32_t Mips32r6RelativePatcherTest::kAnchorOffset = 0;  // At auipc (where PC+0 points).
const ArrayRef<const uint8_t> Mips32r6RelativePatcherTest::kUnpatchedPcRelativeCode(
    kUnpatchedPcRelativeRawCode);

void Mips32r6RelativePatcherTest::CheckPcRelativePatch(const ArrayRef<const LinkerPatch>& patches,
                                                       uint32_t target_offset) {
  AddCompiledMethod(MethodRef(1u), kUnpatchedPcRelativeCode, ArrayRef<const LinkerPatch>(patches));
  Link();

  auto result = method_offset_map_.FindMethodOffset(MethodRef(1u));
  ASSERT_TRUE(result.first);

  uint32_t diff = target_offset - (result.second + kAnchorOffset);
  diff += (diff & 0x8000) << 1;  // Account for sign extension in addiu.

  const uint8_t expected_code[] = {
      static_cast<uint8_t>(diff >> 16), static_cast<uint8_t>(diff >> 24), 0x5E, 0xEE,
      static_cast<uint8_t>(diff), static_cast<uint8_t>(diff >> 8), 0x52, 0x26,
  };
  EXPECT_TRUE(CheckLinkedMethod(MethodRef(1u), ArrayRef<const uint8_t>(expected_code)));
}

void Mips32r6RelativePatcherTest::TestDexCacheReference(uint32_t dex_cache_arrays_begin,
                                                        uint32_t element_offset) {
  dex_cache_arrays_begin_ = dex_cache_arrays_begin;
  LinkerPatch patches[] = {
      LinkerPatch::DexCacheArrayPatch(kLiteralOffset, nullptr, kAnchorOffset, element_offset)
  };
  CheckPcRelativePatch(ArrayRef<const LinkerPatch>(patches),
                       dex_cache_arrays_begin_ + element_offset);
}

void Mips32r6RelativePatcherTest::TestStringReference(uint32_t string_offset) {
  constexpr uint32_t kStringIndex = 1u;
  string_index_to_offset_map_.Put(kStringIndex, string_offset);
  LinkerPatch patches[] = {
      LinkerPatch::RelativeStringPatch(kLiteralOffset, nullptr, kAnchorOffset, kStringIndex)
  };
  CheckPcRelativePatch(ArrayRef<const LinkerPatch>(patches), string_offset);
}

TEST_F(Mips32r6RelativePatcherTest, DexCacheReference) {
  TestDexCacheReference(/* dex_cache_arrays_begin */ 0x12345678, /* element_offset */ 0x1234);
}

TEST_F(Mips32r6RelativePatcherTest, StringReference) {
  TestStringReference(/* string_offset*/ 0x87651234);
}

}  // namespace linker
}  // namespace art
