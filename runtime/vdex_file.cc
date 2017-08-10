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

#include "base/bit_utils.h"
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
    // Dex files are required to be 4 byte aligned. the OatWriter makes sure they are, see
    // OatWriter::SeekToDexFiles.
    data = AlignUp(data, 4);
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

// Utility class to easily iterate over the quickening data.
class QuickeningInfoIterator {
 public:
  QuickeningInfoIterator(uint32_t dex_file_index,
                         uint32_t number_of_dex_files,
                         const ArrayRef<const uint8_t>& quickening_info)
      : quickening_info_(quickening_info) {
    const unaligned_uint32_t* dex_file_indices = reinterpret_cast<const unaligned_uint32_t*>(
            quickening_info.data() +
            quickening_info.size() -
            number_of_dex_files * sizeof(uint32_t));
    current_code_item_end_ = (dex_file_index == number_of_dex_files - 1)
        ? dex_file_indices
        : reinterpret_cast<const unaligned_uint32_t*>(
              quickening_info_.data() + dex_file_indices[dex_file_index + 1]);
    current_code_item_ptr_ = reinterpret_cast<const uint32_t*>(
        quickening_info_.data() + dex_file_indices[dex_file_index]);
  }

  bool Done() const {
    return current_code_item_ptr_ == current_code_item_end_;
  }

  void Advance() {
    current_code_item_ptr_ += 2;
  }

  uint32_t GetCurrentCodeItemOffset() const {
    return current_code_item_ptr_[0];
  }

  const ArrayRef<const uint8_t> GetCurrentQuickeningInfo() const {
    return ArrayRef<const uint8_t>(
        // Add sizeof(uint32_t) to remove the length from the data pointer.
        quickening_info_.data() + current_code_item_ptr_[1] + sizeof(uint32_t),
        *reinterpret_cast<const unaligned_uint32_t*>(
            quickening_info_.data() + current_code_item_ptr_[1]));
  }

 private:
  typedef __attribute__((__aligned__(1))) uint32_t unaligned_uint32_t;
  const ArrayRef<const uint8_t>& quickening_info_;
  const unaligned_uint32_t* current_code_item_ptr_;
  const unaligned_uint32_t* current_code_item_end_;

  DISALLOW_COPY_AND_ASSIGN(QuickeningInfoIterator);
};

void VdexFile::Unquicken(const std::vector<const DexFile*>& dex_files,
                         const ArrayRef<const uint8_t>& quickening_info) {
  if (quickening_info.size() == 0) {
    // Bail early if there is no quickening info.
    return;
  }
  // We do not decompile a RETURN_VOID_NO_BARRIER into a RETURN_VOID, as the quickening
  // optimization does not depend on the boot image (the optimization relies on not
  // having final fields in a class, which does not change for an app).
  constexpr bool kDecompileReturnInstruction = false;
  for (uint32_t i = 0; i < dex_files.size(); ++i) {
    for (QuickeningInfoIterator it(i, dex_files.size(), quickening_info);
         !it.Done();
         it.Advance()) {
      optimizer::ArtDecompileDEX(
          *dex_files[i]->GetCodeItem(it.GetCurrentCodeItemOffset()),
          it.GetCurrentQuickeningInfo(),
          kDecompileReturnInstruction);
    }
  }
}

static constexpr uint32_t kNoDexFile = -1;

uint32_t VdexFile::GetDexFileIndex(const DexFile& dex_file) const {
  uint32_t dex_index = 0;
  for (const uint8_t* dex_file_start = GetNextDexFileData(nullptr);
       dex_file_start != dex_file.Begin();
       dex_file_start = GetNextDexFileData(dex_file_start)) {
    if (dex_file_start == nullptr) {
      return kNoDexFile;
    }
    dex_index++;
  }
  return dex_index;
}

void VdexFile::FullyUnquickenDexFile(const DexFile& target_dex_file,
                                     const DexFile& original_dex_file) const {
  uint32_t dex_index = GetDexFileIndex(original_dex_file);
  if (dex_index == kNoDexFile) {
    return;
  }

  constexpr bool kDecompileReturnInstruction = true;
  QuickeningInfoIterator it(dex_index, GetHeader().GetNumberOfDexFiles(), GetQuickeningInfo());
  // Iterate over the class definitions. Even if there is no quickening info,
  // we want to unquicken RETURN_VOID_NO_BARRIER instruction.
  for (uint32_t i = 0; i < target_dex_file.NumClassDefs(); ++i) {
    const DexFile::ClassDef& class_def = target_dex_file.GetClassDef(i);
    const uint8_t* class_data = target_dex_file.GetClassData(class_def);
    if (class_data != nullptr) {
      for (ClassDataItemIterator class_it(target_dex_file, class_data);
           class_it.HasNext();
           class_it.Next()) {
        if (class_it.IsAtMethod() && class_it.GetMethodCodeItem() != nullptr) {
          uint32_t offset = class_it.GetMethodCodeItemOffset();
          if (!it.Done() && offset == it.GetCurrentCodeItemOffset()) {
            optimizer::ArtDecompileDEX(
                *class_it.GetMethodCodeItem(),
                it.GetCurrentQuickeningInfo(),
                kDecompileReturnInstruction);
            it.Advance();
          } else {
            optimizer::ArtDecompileDEX(*class_it.GetMethodCodeItem(),
                                       ArrayRef<const uint8_t>(nullptr, 0),
                                       kDecompileReturnInstruction);
          }
        }
      }
    }
  }
}

const uint8_t* VdexFile::GetQuickenedInfoOf(const DexFile& dex_file,
                                            uint32_t code_item_offset) const {
  if (GetQuickeningInfo().size() == 0) {
    // Bail early if there is no quickening info.
    return nullptr;
  }

  uint32_t dex_index = GetDexFileIndex(dex_file);
  if (dex_index == kNoDexFile) {
    return nullptr;
  }

  for (QuickeningInfoIterator it(dex_index, GetHeader().GetNumberOfDexFiles(), GetQuickeningInfo());
       !it.Done();
       it.Advance()) {
    if (code_item_offset == it.GetCurrentCodeItemOffset()) {
      return it.GetCurrentQuickeningInfo().data();
    }
  }
  return nullptr;
}

}  // namespace art
