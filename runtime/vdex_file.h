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

#ifndef ART_RUNTIME_VDEX_FILE_H_
#define ART_RUNTIME_VDEX_FILE_H_

#include <stdint.h>
#include <string>

#include "base/macros.h"
#include "mem_map.h"
#include "os.h"

namespace art {

// VDEX files contain extracted DEX files. The VdexFile class maps the file to
// memory and provides tools for accessing its individual sections.
//
// File format:
//   VdexFile::Header    fixed-length header
//
//   DEX[0]              array of the input DEX files
//   DEX[1]              the bytecode may have been quickened
//   ...
//   DEX[D]
//

class VdexFile {
 public:
  struct Header {
   public:
    Header(uint32_t dex_size, uint32_t verifier_deps_size);

    bool IsMagicValid() const;
    bool IsVersionValid() const;

    uint32_t GetDexSize() const { return dex_size_; }
    uint32_t GetVerifierDepsSize() const { return verifier_deps_size_; }

   private:
    static constexpr uint8_t kVdexMagic[] = { 'v', 'd', 'e', 'x' };
    static constexpr uint8_t kVdexVersion[] = { '0', '0', '0', '\0' };

    uint8_t magic_[4];
    uint8_t version_[4];
    uint32_t dex_size_;
    uint32_t verifier_deps_size_;
  };

  static VdexFile* Open(const std::string& vdex_filename,
                        bool writable,
                        bool low_4gb,
                        std::string* error_msg);

  const uint8_t* Begin() const { return mmap_->Begin(); }
  const uint8_t* End() const { return mmap_->End(); }
  size_t Size() const { return mmap_->Size(); }

 private:
  explicit VdexFile(MemMap* mmap) : mmap_(mmap) {}

  std::unique_ptr<MemMap> mmap_;

  DISALLOW_COPY_AND_ASSIGN(VdexFile);
};

}  // namespace art

#endif  // ART_RUNTIME_VDEX_FILE_H_
