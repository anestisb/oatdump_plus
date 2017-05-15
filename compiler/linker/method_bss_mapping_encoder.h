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

#ifndef ART_COMPILER_LINKER_METHOD_BSS_MAPPING_ENCODER_H_
#define ART_COMPILER_LINKER_METHOD_BSS_MAPPING_ENCODER_H_

#include "base/enums.h"
#include "base/logging.h"
#include "dex_file.h"
#include "method_bss_mapping.h"

namespace art {
namespace linker {

// Helper class for encoding compressed MethodBssMapping.
class MethodBssMappingEncoder {
 public:
  explicit MethodBssMappingEncoder(PointerSize pointer_size)
      : pointer_size_(static_cast<size_t>(pointer_size)) {
    entry_.method_index = DexFile::kDexNoIndex16;
    entry_.index_mask = 0u;
    entry_.bss_offset = static_cast<uint32_t>(-1);
  }

  // Try to merge the next method_index -> bss_offset mapping into the current entry.
  // Return true on success, false on failure.
  bool TryMerge(uint32_t method_index, uint32_t bss_offset) {
    DCHECK_NE(method_index, entry_.method_index);
    if (entry_.bss_offset + pointer_size_ != bss_offset) {
      return false;
    }
    uint32_t diff = method_index - entry_.method_index;
    if (diff > 16u) {
      return false;
    }
    if ((entry_.index_mask & ~(static_cast<uint32_t>(-1) << diff)) != 0u) {
      return false;
    }
    entry_.method_index = method_index;
    // Insert the bit indicating the method index we've just overwritten
    // and shift bits indicating method indexes before that.
    entry_.index_mask = dchecked_integral_cast<uint16_t>(
        (static_cast<uint32_t>(entry_.index_mask) | 0x10000u) >> diff);
    entry_.bss_offset = bss_offset;
    return true;
  }

  void Reset(uint32_t method_index, uint32_t bss_offset) {
    entry_.method_index = method_index;
    entry_.index_mask = 0u;
    entry_.bss_offset = bss_offset;
  }

  MethodBssMappingEntry GetEntry() {
    return entry_;
  }

 private:
  size_t pointer_size_;
  MethodBssMappingEntry entry_;
};

}  // namespace linker
}  // namespace art

#endif  // ART_COMPILER_LINKER_METHOD_BSS_MAPPING_ENCODER_H_
