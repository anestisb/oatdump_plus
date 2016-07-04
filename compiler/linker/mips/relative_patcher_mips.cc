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

#include "linker/mips/relative_patcher_mips.h"

#include "compiled_method.h"

namespace art {
namespace linker {

uint32_t MipsRelativePatcher::ReserveSpace(
    uint32_t offset,
    const CompiledMethod* compiled_method ATTRIBUTE_UNUSED,
    MethodReference method_ref ATTRIBUTE_UNUSED) {
  return offset;  // No space reserved; no limit on relative call distance.
}

uint32_t MipsRelativePatcher::ReserveSpaceEnd(uint32_t offset) {
  return offset;  // No space reserved; no limit on relative call distance.
}

uint32_t MipsRelativePatcher::WriteThunks(OutputStream* out ATTRIBUTE_UNUSED, uint32_t offset) {
  return offset;  // No thunks added; no limit on relative call distance.
}

void MipsRelativePatcher::PatchCall(std::vector<uint8_t>* code ATTRIBUTE_UNUSED,
                                    uint32_t literal_offset ATTRIBUTE_UNUSED,
                                    uint32_t patch_offset ATTRIBUTE_UNUSED,
                                    uint32_t target_offset ATTRIBUTE_UNUSED) {
  UNIMPLEMENTED(FATAL) << "PatchCall unimplemented on MIPS";
}

void MipsRelativePatcher::PatchPcRelativeReference(std::vector<uint8_t>* code,
                                                   const LinkerPatch& patch,
                                                   uint32_t patch_offset,
                                                   uint32_t target_offset) {
  uint32_t anchor_literal_offset = patch.PcInsnOffset();
  uint32_t literal_offset = patch.LiteralOffset();

  // Basic sanity checks.
  if (is_r6) {
    DCHECK_GE(code->size(), 8u);
    DCHECK_LE(literal_offset, code->size() - 8u);
    DCHECK_EQ(literal_offset, anchor_literal_offset);
    // AUIPC reg, offset_high
    DCHECK_EQ((*code)[literal_offset + 0], 0x34);
    DCHECK_EQ((*code)[literal_offset + 1], 0x12);
    DCHECK_EQ(((*code)[literal_offset + 2] & 0x1F), 0x1E);
    DCHECK_EQ(((*code)[literal_offset + 3] & 0xFC), 0xEC);
    // ADDIU reg, reg, offset_low
    DCHECK_EQ((*code)[literal_offset + 4], 0x78);
    DCHECK_EQ((*code)[literal_offset + 5], 0x56);
    DCHECK_EQ(((*code)[literal_offset + 7] & 0xFC), 0x24);
  } else {
    DCHECK_GE(code->size(), 16u);
    DCHECK_LE(literal_offset, code->size() - 12u);
    DCHECK_GE(literal_offset, 4u);
    DCHECK_EQ(literal_offset + 4u, anchor_literal_offset);
    // NAL
    DCHECK_EQ((*code)[literal_offset - 4], 0x00);
    DCHECK_EQ((*code)[literal_offset - 3], 0x00);
    DCHECK_EQ((*code)[literal_offset - 2], 0x10);
    DCHECK_EQ((*code)[literal_offset - 1], 0x04);
    // LUI reg, offset_high
    DCHECK_EQ((*code)[literal_offset + 0], 0x34);
    DCHECK_EQ((*code)[literal_offset + 1], 0x12);
    DCHECK_EQ(((*code)[literal_offset + 2] & 0xE0), 0x00);
    DCHECK_EQ((*code)[literal_offset + 3], 0x3C);
    // ORI reg, reg, offset_low
    DCHECK_EQ((*code)[literal_offset + 4], 0x78);
    DCHECK_EQ((*code)[literal_offset + 5], 0x56);
    DCHECK_EQ(((*code)[literal_offset + 7] & 0xFC), 0x34);
    // ADDU reg, reg, RA
    DCHECK_EQ((*code)[literal_offset + 8], 0x21);
    DCHECK_EQ(((*code)[literal_offset + 9] & 0x07), 0x00);
    DCHECK_EQ(((*code)[literal_offset + 10] & 0x1F), 0x1F);
    DCHECK_EQ(((*code)[literal_offset + 11] & 0xFC), 0x00);
  }

  // Apply patch.
  uint32_t anchor_offset = patch_offset - literal_offset + anchor_literal_offset;
  uint32_t diff = target_offset - anchor_offset + kDexCacheArrayLwOffset;
  if (is_r6) {
    diff += (diff & 0x8000) << 1;  // Account for sign extension in ADDIU.
  }

  // LUI reg, offset_high / AUIPC reg, offset_high
  (*code)[literal_offset + 0] = static_cast<uint8_t>(diff >> 16);
  (*code)[literal_offset + 1] = static_cast<uint8_t>(diff >> 24);
  // ORI reg, reg, offset_low / ADDIU reg, reg, offset_low
  (*code)[literal_offset + 4] = static_cast<uint8_t>(diff >> 0);
  (*code)[literal_offset + 5] = static_cast<uint8_t>(diff >> 8);
}

}  // namespace linker
}  // namespace art
