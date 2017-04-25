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

#include "linker/mips64/relative_patcher_mips64.h"

#include "compiled_method.h"

namespace art {
namespace linker {

uint32_t Mips64RelativePatcher::ReserveSpace(
    uint32_t offset,
    const CompiledMethod* compiled_method ATTRIBUTE_UNUSED,
    MethodReference method_ref ATTRIBUTE_UNUSED) {
  return offset;  // No space reserved; no limit on relative call distance.
}

uint32_t Mips64RelativePatcher::ReserveSpaceEnd(uint32_t offset) {
  return offset;  // No space reserved; no limit on relative call distance.
}

uint32_t Mips64RelativePatcher::WriteThunks(OutputStream* out ATTRIBUTE_UNUSED, uint32_t offset) {
  return offset;  // No thunks added; no limit on relative call distance.
}

void Mips64RelativePatcher::PatchCall(std::vector<uint8_t>* code,
                                      uint32_t literal_offset,
                                      uint32_t patch_offset,
                                      uint32_t target_offset) {
  // Basic sanity checks.
  DCHECK_GE(code->size(), 8u);
  DCHECK_LE(literal_offset, code->size() - 8u);
  // auipc reg, offset_high
  DCHECK_EQ((*code)[literal_offset + 0], 0x34);
  DCHECK_EQ((*code)[literal_offset + 1], 0x12);
  DCHECK_EQ(((*code)[literal_offset + 2] & 0x1F), 0x1E);
  DCHECK_EQ(((*code)[literal_offset + 3] & 0xFC), 0xEC);
  // jialc reg, offset_low
  DCHECK_EQ((*code)[literal_offset + 4], 0x78);
  DCHECK_EQ((*code)[literal_offset + 5], 0x56);
  DCHECK_EQ(((*code)[literal_offset + 6] & 0xE0), 0x00);
  DCHECK_EQ((*code)[literal_offset + 7], 0xF8);

  // Apply patch.
  uint32_t diff = target_offset - patch_offset;
  // Note that a combination of auipc with an instruction that adds a sign-extended
  // 16-bit immediate operand (e.g. jialc) provides a PC-relative range of
  // PC-0x80000000 to PC+0x7FFF7FFF on MIPS64, that is, short of 2GB on one end
  // by 32KB.
  diff += (diff & 0x8000) << 1;  // Account for sign extension in jialc.

  // auipc reg, offset_high
  (*code)[literal_offset + 0] = static_cast<uint8_t>(diff >> 16);
  (*code)[literal_offset + 1] = static_cast<uint8_t>(diff >> 24);
  // jialc reg, offset_low
  (*code)[literal_offset + 4] = static_cast<uint8_t>(diff >> 0);
  (*code)[literal_offset + 5] = static_cast<uint8_t>(diff >> 8);
}

void Mips64RelativePatcher::PatchPcRelativeReference(std::vector<uint8_t>* code,
                                                     const LinkerPatch& patch,
                                                     uint32_t patch_offset,
                                                     uint32_t target_offset) {
  uint32_t anchor_literal_offset = patch.PcInsnOffset();
  uint32_t literal_offset = patch.LiteralOffset();

  // Basic sanity checks.
  DCHECK_GE(code->size(), 8u);
  DCHECK_LE(literal_offset, code->size() - 8u);
  DCHECK_EQ(literal_offset, anchor_literal_offset);
  // auipc reg, offset_high
  DCHECK_EQ((*code)[literal_offset + 0], 0x34);
  DCHECK_EQ((*code)[literal_offset + 1], 0x12);
  DCHECK_EQ(((*code)[literal_offset + 2] & 0x1F), 0x1E);
  DCHECK_EQ(((*code)[literal_offset + 3] & 0xFC), 0xEC);
  // instr reg(s), offset_low
  DCHECK_EQ((*code)[literal_offset + 4], 0x78);
  DCHECK_EQ((*code)[literal_offset + 5], 0x56);

  // Apply patch.
  uint32_t anchor_offset = patch_offset - literal_offset + anchor_literal_offset;
  uint32_t diff = target_offset - anchor_offset;
  // Note that a combination of auipc with an instruction that adds a sign-extended
  // 16-bit immediate operand (e.g. ld) provides a PC-relative range of
  // PC-0x80000000 to PC+0x7FFF7FFF on MIPS64, that is, short of 2GB on one end
  // by 32KB.
  diff += (diff & 0x8000) << 1;  // Account for sign extension in instruction following auipc.

  // auipc reg, offset_high
  (*code)[literal_offset + 0] = static_cast<uint8_t>(diff >> 16);
  (*code)[literal_offset + 1] = static_cast<uint8_t>(diff >> 24);
  // instr reg(s), offset_low
  (*code)[literal_offset + 4] = static_cast<uint8_t>(diff >> 0);
  (*code)[literal_offset + 5] = static_cast<uint8_t>(diff >> 8);
}

void Mips64RelativePatcher::PatchBakerReadBarrierBranch(std::vector<uint8_t>* code ATTRIBUTE_UNUSED,
                                                        const LinkerPatch& patch ATTRIBUTE_UNUSED,
                                                        uint32_t patch_offset ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "UNIMPLEMENTED";
}

}  // namespace linker
}  // namespace art
