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
#include "linker/mips64/relative_patcher_mips64.h"

namespace art {
namespace linker {

class Mips64RelativePatcherTest : public RelativePatcherTest {
 public:
  Mips64RelativePatcherTest() : RelativePatcherTest(kMips64, "default") {}

 protected:
  static const uint8_t kUnpatchedPcRelativeRawCode[];
  static const uint8_t kUnpatchedPcRelativeCallRawCode[];
  static const uint32_t kLiteralOffset;
  static const uint32_t kAnchorOffset;
  static const ArrayRef<const uint8_t> kUnpatchedPcRelativeCode;
  static const ArrayRef<const uint8_t> kUnpatchedPcRelativeCallCode;

  uint32_t GetMethodOffset(uint32_t method_idx) {
    auto result = method_offset_map_.FindMethodOffset(MethodRef(method_idx));
    CHECK(result.first);
    return result.second;
  }

  void CheckPcRelativePatch(const ArrayRef<const LinkerPatch>& patches, uint32_t target_offset);
  void TestDexCacheReference(uint32_t dex_cache_arrays_begin, uint32_t element_offset);
  void TestStringReference(uint32_t string_offset);
};

const uint8_t Mips64RelativePatcherTest::kUnpatchedPcRelativeRawCode[] = {
    0x34, 0x12, 0x5E, 0xEE,  // auipc s2, high(diff); placeholder = 0x1234
    0x78, 0x56, 0x52, 0x66,  // daddiu s2, s2, low(diff); placeholder = 0x5678
};
const uint8_t Mips64RelativePatcherTest::kUnpatchedPcRelativeCallRawCode[] = {
    0x34, 0x12, 0x3E, 0xEC,  // auipc at, high(diff); placeholder = 0x1234
    0x78, 0x56, 0x01, 0xF8,  // jialc at, low(diff); placeholder = 0x5678
};
const uint32_t Mips64RelativePatcherTest::kLiteralOffset = 0;  // At auipc (where patching starts).
const uint32_t Mips64RelativePatcherTest::kAnchorOffset = 0;  // At auipc (where PC+0 points).
const ArrayRef<const uint8_t> Mips64RelativePatcherTest::kUnpatchedPcRelativeCode(
    kUnpatchedPcRelativeRawCode);
const ArrayRef<const uint8_t> Mips64RelativePatcherTest::kUnpatchedPcRelativeCallCode(
    kUnpatchedPcRelativeCallRawCode);

void Mips64RelativePatcherTest::CheckPcRelativePatch(const ArrayRef<const LinkerPatch>& patches,
                                                     uint32_t target_offset) {
  AddCompiledMethod(MethodRef(1u), kUnpatchedPcRelativeCode, ArrayRef<const LinkerPatch>(patches));
  Link();

  auto result = method_offset_map_.FindMethodOffset(MethodRef(1u));
  ASSERT_TRUE(result.first);

  uint32_t diff = target_offset - (result.second + kAnchorOffset);
  diff += (diff & 0x8000) << 1;  // Account for sign extension in instruction following auipc.

  const uint8_t expected_code[] = {
      static_cast<uint8_t>(diff >> 16), static_cast<uint8_t>(diff >> 24), 0x5E, 0xEE,
      static_cast<uint8_t>(diff), static_cast<uint8_t>(diff >> 8), 0x52, 0x66,
  };
  EXPECT_TRUE(CheckLinkedMethod(MethodRef(1u), ArrayRef<const uint8_t>(expected_code)));
}

void Mips64RelativePatcherTest::TestDexCacheReference(uint32_t dex_cache_arrays_begin,
                                                      uint32_t element_offset) {
  dex_cache_arrays_begin_ = dex_cache_arrays_begin;
  LinkerPatch patches[] = {
      LinkerPatch::DexCacheArrayPatch(kLiteralOffset, nullptr, kAnchorOffset, element_offset)
  };
  CheckPcRelativePatch(ArrayRef<const LinkerPatch>(patches),
                       dex_cache_arrays_begin_ + element_offset);
}

TEST_F(Mips64RelativePatcherTest, DexCacheReference) {
  TestDexCacheReference(/* dex_cache_arrays_begin */ 0x12345678, /* element_offset */ 0x1234);
}

TEST_F(Mips64RelativePatcherTest, CallOther) {
  LinkerPatch method1_patches[] = {
      LinkerPatch::RelativeCodePatch(kLiteralOffset, nullptr, 2u),
  };
  AddCompiledMethod(MethodRef(1u),
                    kUnpatchedPcRelativeCallCode,
                    ArrayRef<const LinkerPatch>(method1_patches));
  LinkerPatch method2_patches[] = {
      LinkerPatch::RelativeCodePatch(kLiteralOffset, nullptr, 1u),
  };
  AddCompiledMethod(MethodRef(2u),
                    kUnpatchedPcRelativeCallCode,
                    ArrayRef<const LinkerPatch>(method2_patches));
  Link();

  uint32_t method1_offset = GetMethodOffset(1u);
  uint32_t method2_offset = GetMethodOffset(2u);
  uint32_t diff_after = method2_offset - (method1_offset + kAnchorOffset /* PC adjustment */);
  diff_after += (diff_after & 0x8000) << 1;  // Account for sign extension in jialc.
  static const uint8_t method1_expected_code[] = {
      static_cast<uint8_t>(diff_after >> 16), static_cast<uint8_t>(diff_after >> 24), 0x3E, 0xEC,
      static_cast<uint8_t>(diff_after), static_cast<uint8_t>(diff_after >> 8), 0x01, 0xF8,
  };
  EXPECT_TRUE(CheckLinkedMethod(MethodRef(1u), ArrayRef<const uint8_t>(method1_expected_code)));
  uint32_t diff_before = method1_offset - (method2_offset + kAnchorOffset /* PC adjustment */);
  diff_before += (diff_before & 0x8000) << 1;  // Account for sign extension in jialc.
  static const uint8_t method2_expected_code[] = {
      static_cast<uint8_t>(diff_before >> 16), static_cast<uint8_t>(diff_before >> 24), 0x3E, 0xEC,
      static_cast<uint8_t>(diff_before), static_cast<uint8_t>(diff_before >> 8), 0x01, 0xF8,
  };
  EXPECT_TRUE(CheckLinkedMethod(MethodRef(2u), ArrayRef<const uint8_t>(method2_expected_code)));
}

}  // namespace linker
}  // namespace art
