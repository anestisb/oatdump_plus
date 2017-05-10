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

#include <sys/mman.h>  // For the PROT_* and MAP_* constants.

#include <memory>

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "dex_file.h"
#include "dex_to_dex_decompiler.h"

namespace art {

constexpr uint8_t VdexFile::Header::kVdexInvalidMagic[4];
constexpr uint8_t VdexFile::Header::kVdexMagic[4];
constexpr uint8_t VdexFile::Header::kVdexVersion[4];

bool VdexFile::Header::IsMagicValid() const {
  return (memcmp(magic_, kVdexMagic, sizeof(kVdexMagic)) == 0);
}

bool VdexFile::Header::IsVersionValid() const {
  return (memcmp(version_, kVdexVersion, sizeof(kVdexVersion)) == 0);
}

VdexFile::Header::Header(uint32_t number_of_dex_files,
                         uint32_t dex_size,
                         uint32_t verifier_deps_size,
                         uint32_t quickening_info_size)
    : number_of_dex_files_(number_of_dex_files),
      dex_size_(dex_size),
      verifier_deps_size_(verifier_deps_size),
      quickening_info_size_(quickening_info_size) {
  memcpy(magic_, kVdexMagic, sizeof(kVdexMagic));
  memcpy(version_, kVdexVersion, sizeof(kVdexVersion));
  DCHECK(IsMagicValid());
  DCHECK(IsVersionValid());
}

std::unique_ptr<VdexFile> VdexFile::Open(const std::string& vdex_filename,
                                         bool writable,
                                         bool low_4gb,
                                         bool unquicken,
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

  return Open(vdex_file->Fd(), vdex_length, vdex_filename, writable, low_4gb, unquicken, error_msg);
}

std::unique_ptr<VdexFile> VdexFile::Open(int file_fd,
                                         size_t vdex_length,
                                         const std::string& vdex_filename,
                                         bool writable,
                                         bool low_4gb,
                                         bool unquicken,
                                         std::string* error_msg) {
  std::unique_ptr<MemMap> mmap(MemMap::MapFile(
      vdex_length,
      (writable || unquicken) ? PROT_READ | PROT_WRITE : PROT_READ,
      unquicken ? MAP_PRIVATE : MAP_SHARED,
      file_fd,
      0 /* start offset */,
      low_4gb,
      vdex_filename.c_str(),
      error_msg));
  if (mmap == nullptr) {
    *error_msg = "Failed to mmap file " + vdex_filename + " : " + *error_msg;
    return nullptr;
  }

  std::unique_ptr<VdexFile> vdex(new VdexFile(mmap.release()));
  if (!vdex->IsValid()) {
    *error_msg = "Vdex file is not valid";
    return nullptr;
  }

  if (unquicken) {
    std::vector<std::unique_ptr<const DexFile>> unique_ptr_dex_files;
    if (!vdex->OpenAllDexFiles(&unique_ptr_dex_files, error_msg)) {
      return nullptr;
    }
    Unquicken(MakeNonOwningPointerVector(unique_ptr_dex_files), vdex->GetQuickeningInfo());
    // Update the quickening info size to pretend there isn't any.
    reinterpret_cast<Header*>(vdex->mmap_->Begin())->quickening_info_size_ = 0;
  }

  *error_msg = "Success";
  return vdex;
}

const uint8_t* VdexFile::GetNextDexFileData(const uint8_t* cursor) const {
  DCHECK(cursor == nullptr || (cursor > Begin() && cursor <= End()));
  if (cursor == nullptr) {
    // Beginning of the iteration, return the first dex file if there is one.
    return HasDexSection() ? DexBegin() : nullptr;
  } else {
    // Fetch the next dex file. Return null if there is none.
    const uint8_t* data = cursor + reinterpret_cast<const DexFile::Header*>(cursor)->file_size_;
    return (data == DexEnd()) ? nullptr : data;
  }
}

bool VdexFile::OpenAllDexFiles(std::vector<std::unique_ptr<const DexFile>>* dex_files,
                               std::string* error_msg) {
  size_t i = 0;
  for (const uint8_t* dex_file_start = GetNextDexFileData(nullptr);
       dex_file_start != nullptr;
       dex_file_start = GetNextDexFileData(dex_file_start), ++i) {
    size_t size = reinterpret_cast<const DexFile::Header*>(dex_file_start)->file_size_;
    // TODO: Supply the location information for a vdex file.
    static constexpr char kVdexLocation[] = "";
    std::string location = DexFile::GetMultiDexLocation(i, kVdexLocation);
    std::unique_ptr<const DexFile> dex(DexFile::Open(dex_file_start,
                                                     size,
                                                     location,
                                                     GetLocationChecksum(i),
                                                     nullptr /*oat_dex_file*/,
                                                     false /*verify*/,
                                                     false /*verify_checksum*/,
                                                     error_msg));
    if (dex == nullptr) {
      return false;
    }
    dex_files->push_back(std::move(dex));
  }
  return true;
}

void VdexFile::Unquicken(const std::vector<const DexFile*>& dex_files,
                         const ArrayRef<const uint8_t>& quickening_info) {
  if (quickening_info.size() == 0) {
    // If there is no quickening info, we bail early, as the code below expects at
    // least the size of quickening data for each method that has a code item.
    return;
  }
  const uint8_t* quickening_info_ptr = quickening_info.data();
  const uint8_t* const quickening_info_end = quickening_info.data() + quickening_info.size();
  for (const DexFile* dex_file : dex_files) {
    for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
      const uint8_t* class_data = dex_file->GetClassData(class_def);
      if (class_data == nullptr) {
        continue;
      }
      ClassDataItemIterator it(*dex_file, class_data);
      // Skip fields
      while (it.HasNextStaticField()) {
        it.Next();
      }
      while (it.HasNextInstanceField()) {
        it.Next();
      }

      while (it.HasNextDirectMethod()) {
        const DexFile::CodeItem* code_item = it.GetMethodCodeItem();
        if (code_item != nullptr) {
          uint32_t quickening_size = *reinterpret_cast<const uint32_t*>(quickening_info_ptr);
          quickening_info_ptr += sizeof(uint32_t);
          optimizer::ArtDecompileDEX(*code_item,
                                     ArrayRef<const uint8_t>(quickening_info_ptr, quickening_size),
                                     /* decompile_return_instruction */ false);
          quickening_info_ptr += quickening_size;
        }
        it.Next();
      }

      while (it.HasNextVirtualMethod()) {
        const DexFile::CodeItem* code_item = it.GetMethodCodeItem();
        if (code_item != nullptr) {
          uint32_t quickening_size = *reinterpret_cast<const uint32_t*>(quickening_info_ptr);
          quickening_info_ptr += sizeof(uint32_t);
          optimizer::ArtDecompileDEX(*code_item,
                                     ArrayRef<const uint8_t>(quickening_info_ptr, quickening_size),
                                     /* decompile_return_instruction */ false);
          quickening_info_ptr += quickening_size;
        }
        it.Next();
      }
      DCHECK(!it.HasNext());
    }
  }
  if (quickening_info_ptr != quickening_info_end) {
    LOG(FATAL) << "Failed to use all quickening info";
  }
}

}  // namespace art
