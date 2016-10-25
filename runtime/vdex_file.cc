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

#include "vdex_file.h"

#include <memory>

#include "base/logging.h"
#include "base/unix_file/fd_file.h"

namespace art {

constexpr uint8_t VdexFile::Header::kVdexMagic[4];
constexpr uint8_t VdexFile::Header::kVdexVersion[4];

bool VdexFile::Header::IsMagicValid() const {
  return (memcmp(magic_, kVdexMagic, sizeof(kVdexMagic)) == 0);
}

bool VdexFile::Header::IsVersionValid() const {
  return (memcmp(version_, kVdexVersion, sizeof(kVdexVersion)) == 0);
}

VdexFile::Header::Header(uint32_t dex_size,
                         uint32_t verifier_deps_size,
                         uint32_t quickening_info_size)
    : dex_size_(dex_size),
      verifier_deps_size_(verifier_deps_size),
      quickening_info_size_(quickening_info_size) {
  memcpy(magic_, kVdexMagic, sizeof(kVdexMagic));
  memcpy(version_, kVdexVersion, sizeof(kVdexVersion));
  DCHECK(IsMagicValid());
  DCHECK(IsVersionValid());
}

VdexFile* VdexFile::Open(const std::string& vdex_filename,
                         bool writable,
                         bool low_4gb,
                         std::string* error_msg) {
  if (!OS::FileExists(vdex_filename.c_str())) {
    *error_msg = "File " + vdex_filename + " does not exist.";
    return nullptr;
  }

  std::unique_ptr<File> vdex_file;
  if (writable) {
    vdex_file.reset(OS::OpenFileReadWrite(vdex_filename.c_str()));
  } else {
    vdex_file.reset(OS::OpenFileForReading(vdex_filename.c_str()));
  }
  if (vdex_file == nullptr) {
    *error_msg = "Could not open file " + vdex_filename +
                 (writable ? " for read/write" : "for reading");
    return nullptr;
  }

  int64_t vdex_length = vdex_file->GetLength();
  if (vdex_length == -1) {
    *error_msg = "Could not read the length of file " + vdex_filename;
    return nullptr;
  }

  std::unique_ptr<MemMap> mmap(MemMap::MapFile(vdex_length,
                                               writable ? PROT_READ | PROT_WRITE : PROT_READ,
                                               MAP_SHARED,
                                               vdex_file->Fd(),
                                               0 /* start offset */,
                                               low_4gb,
                                               vdex_filename.c_str(),
                                               error_msg));
  if (mmap == nullptr) {
    *error_msg = "Failed to mmap file " + vdex_filename + " : " + *error_msg;
    return nullptr;
  }

  *error_msg = "Success";
  return new VdexFile(mmap.release());
}

}  // namespace art
