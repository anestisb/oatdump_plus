/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "dex_file.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <memory>
#include <sstream>

#include "base/enums.h"
#include "base/file_magic.h"
#include "base/hash_map.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/stringprintf.h"
#include "base/systrace.h"
#include "base/unix_file/fd_file.h"
#include "dex_file-inl.h"
#include "dex_file_verifier.h"
#include "globals.h"
#include "jvalue.h"
#include "leb128.h"
#include "oat_file.h"
#include "os.h"
#include "safe_map.h"
#include "thread.h"
#include "type_lookup_table.h"
#include "utf-inl.h"
#include "utils.h"
#include "well_known_classes.h"
#include "zip_archive.h"

namespace art {

static constexpr OatDexFile* kNoOatDexFile = nullptr;

const char* DexFile::kClassesDex = "classes.dex";

const uint8_t DexFile::kDexMagic[] = { 'd', 'e', 'x', '\n' };
const uint8_t DexFile::kDexMagicVersions[DexFile::kNumDexVersions][DexFile::kDexVersionLen] = {
  {'0', '3', '5', '\0'},
  // Dex version 036 skipped because of an old dalvik bug on some versions of android where dex
  // files with that version number would erroneously be accepted and run.
  {'0', '3', '7', '\0'},
  // Dex version 038: Android "O" and beyond.
  {'0', '3', '8', '\0'}
};

struct DexFile::AnnotationValue {
  JValue value_;
  uint8_t type_;
};

bool DexFile::GetChecksum(const char* filename, uint32_t* checksum, std::string* error_msg) {
  CHECK(checksum != nullptr);
  uint32_t magic;

  // Strip ":...", which is the location
  const char* zip_entry_name = kClassesDex;
  const char* file_part = filename;
  std::string file_part_storage;

  if (DexFile::IsMultiDexLocation(filename)) {
    file_part_storage = GetBaseLocation(filename);
    file_part = file_part_storage.c_str();
    zip_entry_name = filename + file_part_storage.size() + 1;
    DCHECK_EQ(zip_entry_name[-1], kMultiDexSeparator);
  }

  File fd = OpenAndReadMagic(file_part, &magic, error_msg);
  if (fd.Fd() == -1) {
    DCHECK(!error_msg->empty());
    return false;
  }
  if (IsZipMagic(magic)) {
    std::unique_ptr<ZipArchive> zip_archive(
        ZipArchive::OpenFromFd(fd.Release(), filename, error_msg));
    if (zip_archive.get() == nullptr) {
      *error_msg = StringPrintf("Failed to open zip archive '%s' (error msg: %s)", file_part,
                                error_msg->c_str());
      return false;
    }
    std::unique_ptr<ZipEntry> zip_entry(zip_archive->Find(zip_entry_name, error_msg));
    if (zip_entry.get() == nullptr) {
      *error_msg = StringPrintf("Zip archive '%s' doesn't contain %s (error msg: %s)", file_part,
                                zip_entry_name, error_msg->c_str());
      return false;
    }
    *checksum = zip_entry->GetCrc32();
    return true;
  }
  if (IsDexMagic(magic)) {
    std::unique_ptr<const DexFile> dex_file(
        DexFile::OpenFile(fd.Release(), filename, false, false, error_msg));
    if (dex_file.get() == nullptr) {
      return false;
    }
    *checksum = dex_file->GetHeader().checksum_;
    return true;
  }
  *error_msg = StringPrintf("Expected valid zip or dex file: '%s'", filename);
  return false;
}

int DexFile::GetPermissions() const {
  if (mem_map_.get() == nullptr) {
    return 0;
  } else {
    return mem_map_->GetProtect();
  }
}

bool DexFile::IsReadOnly() const {
  return GetPermissions() == PROT_READ;
}

bool DexFile::EnableWrite() const {
  CHECK(IsReadOnly());
  if (mem_map_.get() == nullptr) {
    return false;
  } else {
    return mem_map_->Protect(PROT_READ | PROT_WRITE);
  }
}

bool DexFile::DisableWrite() const {
  CHECK(!IsReadOnly());
  if (mem_map_.get() == nullptr) {
    return false;
  } else {
    return mem_map_->Protect(PROT_READ);
  }
}


std::unique_ptr<const DexFile> DexFile::Open(const uint8_t* base,
                                             size_t size,
                                             const std::string& location,
                                             uint32_t location_checksum,
                                             const OatDexFile* oat_dex_file,
                                             bool verify,
                                             bool verify_checksum,
                                             std::string* error_msg) {
  ScopedTrace trace(std::string("Open dex file from RAM ") + location);
  return OpenCommon(base,
                    size,
                    location,
                    location_checksum,
                    oat_dex_file,
                    verify,
                    verify_checksum,
                    error_msg);
}

std::unique_ptr<const DexFile> DexFile::Open(const std::string& location,
                                             uint32_t location_checksum,
                                             std::unique_ptr<MemMap> map,
                                             bool verify,
                                             bool verify_checksum,
                                             std::string* error_msg) {
  ScopedTrace trace(std::string("Open dex file from mapped-memory ") + location);
  CHECK(map.get() != nullptr);
  std::unique_ptr<DexFile> dex_file = OpenCommon(map->Begin(),
                                                 map->Size(),
                                                 location,
                                                 location_checksum,
                                                 kNoOatDexFile,
                                                 verify,
                                                 verify_checksum,
                                                 error_msg);
  if (dex_file != nullptr) {
    dex_file->mem_map_.reset(map.release());
  }
  return dex_file;
}

bool DexFile::Open(const char* filename,
                   const std::string& location,
                   bool verify_checksum,
                   std::string* error_msg,
                   std::vector<std::unique_ptr<const DexFile>>* dex_files) {
  ScopedTrace trace(std::string("Open dex file ") + std::string(location));
  DCHECK(dex_files != nullptr) << "DexFile::Open: out-param is nullptr";
  uint32_t magic;
  File fd = OpenAndReadMagic(filename, &magic, error_msg);
  if (fd.Fd() == -1) {
    DCHECK(!error_msg->empty());
    return false;
  }
  if (IsZipMagic(magic)) {
    return DexFile::OpenZip(fd.Release(), location, verify_checksum, error_msg, dex_files);
  }
  if (IsDexMagic(magic)) {
    std::unique_ptr<const DexFile> dex_file(DexFile::OpenFile(fd.Release(),
                                                              location,
                                                              /* verify */ true,
                                                              verify_checksum,
                                                              error_msg));
    if (dex_file.get() != nullptr) {
      dex_files->push_back(std::move(dex_file));
      return true;
    } else {
      return false;
    }
  }
  *error_msg = StringPrintf("Expected valid zip or dex file: '%s'", filename);
  return false;
}

std::unique_ptr<const DexFile> DexFile::OpenDex(int fd,
                                                const std::string& location,
                                                bool verify_checksum,
                                                std::string* error_msg) {
  ScopedTrace trace("Open dex file " + std::string(location));
  return OpenFile(fd, location, true /* verify */, verify_checksum, error_msg);
}

bool DexFile::OpenZip(int fd,
                      const std::string& location,
                      bool verify_checksum,
                      std::string* error_msg,
                      std::vector<std::unique_ptr<const DexFile>>* dex_files) {
  ScopedTrace trace("Dex file open Zip " + std::string(location));
  DCHECK(dex_files != nullptr) << "DexFile::OpenZip: out-param is nullptr";
  std::unique_ptr<ZipArchive> zip_archive(ZipArchive::OpenFromFd(fd, location.c_str(), error_msg));
  if (zip_archive.get() == nullptr) {
    DCHECK(!error_msg->empty());
    return false;
  }
  return DexFile::OpenAllDexFilesFromZip(*zip_archive,
                                         location,
                                         verify_checksum,
                                         error_msg,
                                         dex_files);
}

std::unique_ptr<const DexFile> DexFile::OpenFile(int fd,
                                                 const std::string& location,
                                                 bool verify,
                                                 bool verify_checksum,
                                                 std::string* error_msg) {
  ScopedTrace trace(std::string("Open dex file ") + std::string(location));
  CHECK(!location.empty());
  std::unique_ptr<MemMap> map;
  {
    File delayed_close(fd, /* check_usage */ false);
    struct stat sbuf;
    memset(&sbuf, 0, sizeof(sbuf));
    if (fstat(fd, &sbuf) == -1) {
      *error_msg = StringPrintf("DexFile: fstat '%s' failed: %s", location.c_str(),
                                strerror(errno));
      return nullptr;
    }
    if (S_ISDIR(sbuf.st_mode)) {
      *error_msg = StringPrintf("Attempt to mmap directory '%s'", location.c_str());
      return nullptr;
    }
    size_t length = sbuf.st_size;
    map.reset(MemMap::MapFile(length,
                              PROT_READ,
                              MAP_PRIVATE,
                              fd,
                              0,
                              /*low_4gb*/false,
                              location.c_str(),
                              error_msg));
    if (map == nullptr) {
      DCHECK(!error_msg->empty());
      return nullptr;
    }
  }

  if (map->Size() < sizeof(DexFile::Header)) {
    *error_msg = StringPrintf(
        "DexFile: failed to open dex file '%s' that is too short to have a header",
        location.c_str());
    return nullptr;
  }

  const Header* dex_header = reinterpret_cast<const Header*>(map->Begin());

  std::unique_ptr<DexFile> dex_file = OpenCommon(map->Begin(),
                                                 map->Size(),
                                                 location,
                                                 dex_header->checksum_,
                                                 kNoOatDexFile,
                                                 verify,
                                                 verify_checksum,
                                                 error_msg);
  if (dex_file != nullptr) {
    dex_file->mem_map_.reset(map.release());
  }

  return dex_file;
}

std::unique_ptr<const DexFile> DexFile::OpenOneDexFileFromZip(const ZipArchive& zip_archive,
                                                              const char* entry_name,
                                                              const std::string& location,
                                                              bool verify_checksum,
                                                              std::string* error_msg,
                                                              ZipOpenErrorCode* error_code) {
  ScopedTrace trace("Dex file open from Zip Archive " + std::string(location));
  CHECK(!location.empty());
  std::unique_ptr<ZipEntry> zip_entry(zip_archive.Find(entry_name, error_msg));
  if (zip_entry == nullptr) {
    *error_code = ZipOpenErrorCode::kEntryNotFound;
    return nullptr;
  }
  if (zip_entry->GetUncompressedLength() == 0) {
    *error_msg = StringPrintf("Dex file '%s' has zero length", location.c_str());
    *error_code = ZipOpenErrorCode::kDexFileError;
    return nullptr;
  }
  std::unique_ptr<MemMap> map(zip_entry->ExtractToMemMap(location.c_str(), entry_name, error_msg));
  if (map == nullptr) {
    *error_msg = StringPrintf("Failed to extract '%s' from '%s': %s", entry_name, location.c_str(),
                              error_msg->c_str());
    *error_code = ZipOpenErrorCode::kExtractToMemoryError;
    return nullptr;
  }
  VerifyResult verify_result;
  std::unique_ptr<DexFile> dex_file = OpenCommon(map->Begin(),
                                                 map->Size(),
                                                 location,
                                                 zip_entry->GetCrc32(),
                                                 kNoOatDexFile,
                                                 /* verify */ true,
                                                 verify_checksum,
                                                 error_msg,
                                                 &verify_result);
  if (dex_file == nullptr) {
    if (verify_result == VerifyResult::kVerifyNotAttempted) {
      *error_code = ZipOpenErrorCode::kDexFileError;
    } else {
      *error_code = ZipOpenErrorCode::kVerifyError;
    }
    return nullptr;
  }
  dex_file->mem_map_.reset(map.release());
  if (!dex_file->DisableWrite()) {
    *error_msg = StringPrintf("Failed to make dex file '%s' read only", location.c_str());
    *error_code = ZipOpenErrorCode::kMakeReadOnlyError;
    return nullptr;
  }
  CHECK(dex_file->IsReadOnly()) << location;
  if (verify_result != VerifyResult::kVerifySucceeded) {
    *error_code = ZipOpenErrorCode::kVerifyError;
    return nullptr;
  }
  *error_code = ZipOpenErrorCode::kNoError;
  return dex_file;
}

// Technically we do not have a limitation with respect to the number of dex files that can be in a
// multidex APK. However, it's bad practice, as each dex file requires its own tables for symbols
// (types, classes, methods, ...) and dex caches. So warn the user that we open a zip with what
// seems an excessive number.
static constexpr size_t kWarnOnManyDexFilesThreshold = 100;

bool DexFile::OpenAllDexFilesFromZip(const ZipArchive& zip_archive,
                                     const std::string& location,
                                     bool verify_checksum,
                                     std::string* error_msg,
                                     std::vector<std::unique_ptr<const DexFile>>* dex_files) {
  ScopedTrace trace("Dex file open from Zip " + std::string(location));
  DCHECK(dex_files != nullptr) << "DexFile::OpenFromZip: out-param is nullptr";
  ZipOpenErrorCode error_code;
  std::unique_ptr<const DexFile> dex_file(OpenOneDexFileFromZip(zip_archive,
                                                                kClassesDex,
                                                                location,
                                                                verify_checksum,
                                                                error_msg,
                                                                &error_code));
  if (dex_file.get() == nullptr) {
    return false;
  } else {
    // Had at least classes.dex.
    dex_files->push_back(std::move(dex_file));

    // Now try some more.

    // We could try to avoid std::string allocations by working on a char array directly. As we
    // do not expect a lot of iterations, this seems too involved and brittle.

    for (size_t i = 1; ; ++i) {
      std::string name = GetMultiDexClassesDexName(i);
      std::string fake_location = GetMultiDexLocation(i, location.c_str());
      std::unique_ptr<const DexFile> next_dex_file(OpenOneDexFileFromZip(zip_archive,
                                                                         name.c_str(),
                                                                         fake_location,
                                                                         verify_checksum,
                                                                         error_msg,
                                                                         &error_code));
      if (next_dex_file.get() == nullptr) {
        if (error_code != ZipOpenErrorCode::kEntryNotFound) {
          LOG(WARNING) << error_msg;
        }
        break;
      } else {
        dex_files->push_back(std::move(next_dex_file));
      }

      if (i == kWarnOnManyDexFilesThreshold) {
        LOG(WARNING) << location << " has in excess of " << kWarnOnManyDexFilesThreshold
                     << " dex files. Please consider coalescing and shrinking the number to "
                        " avoid runtime overhead.";
      }

      if (i == std::numeric_limits<size_t>::max()) {
        LOG(ERROR) << "Overflow in number of dex files!";
        break;
      }
    }

    return true;
  }
}

std::unique_ptr<DexFile> DexFile::OpenCommon(const uint8_t* base,
                                             size_t size,
                                             const std::string& location,
                                             uint32_t location_checksum,
                                             const OatDexFile* oat_dex_file,
                                             bool verify,
                                             bool verify_checksum,
                                             std::string* error_msg,
                                             VerifyResult* verify_result) {
  if (verify_result != nullptr) {
    *verify_result = VerifyResult::kVerifyNotAttempted;
  }
  std::unique_ptr<DexFile> dex_file(new DexFile(base,
                                                size,
                                                location,
                                                location_checksum,
                                                oat_dex_file));
  if (dex_file == nullptr) {
    *error_msg = StringPrintf("Failed to open dex file '%s' from memory: %s", location.c_str(),
                              error_msg->c_str());
    return nullptr;
  }
  if (!dex_file->Init(error_msg)) {
    dex_file.reset();
    return nullptr;
  }
  if (verify && !DexFileVerifier::Verify(dex_file.get(),
                                         dex_file->Begin(),
                                         dex_file->Size(),
                                         location.c_str(),
                                         verify_checksum,
                                         error_msg)) {
    if (verify_result != nullptr) {
      *verify_result = VerifyResult::kVerifyFailed;
    }
    return nullptr;
  }
  if (verify_result != nullptr) {
    *verify_result = VerifyResult::kVerifySucceeded;
  }
  return dex_file;
}

DexFile::DexFile(const uint8_t* base,
                 size_t size,
                 const std::string& location,
                 uint32_t location_checksum,
                 const OatDexFile* oat_dex_file)
    : begin_(base),
      size_(size),
      location_(location),
      location_checksum_(location_checksum),
      header_(reinterpret_cast<const Header*>(base)),
      string_ids_(reinterpret_cast<const StringId*>(base + header_->string_ids_off_)),
      type_ids_(reinterpret_cast<const TypeId*>(base + header_->type_ids_off_)),
      field_ids_(reinterpret_cast<const FieldId*>(base + header_->field_ids_off_)),
      method_ids_(reinterpret_cast<const MethodId*>(base + header_->method_ids_off_)),
      proto_ids_(reinterpret_cast<const ProtoId*>(base + header_->proto_ids_off_)),
      class_defs_(reinterpret_cast<const ClassDef*>(base + header_->class_defs_off_)),
      oat_dex_file_(oat_dex_file) {
  CHECK(begin_ != nullptr) << GetLocation();
  CHECK_GT(size_, 0U) << GetLocation();
}

DexFile::~DexFile() {
  // We don't call DeleteGlobalRef on dex_object_ because we're only called by DestroyJavaVM, and
  // that's only called after DetachCurrentThread, which means there's no JNIEnv. We could
  // re-attach, but cleaning up these global references is not obviously useful. It's not as if
  // the global reference table is otherwise empty!
}

bool DexFile::Init(std::string* error_msg) {
  if (!CheckMagicAndVersion(error_msg)) {
    return false;
  }
  return true;
}

bool DexFile::CheckMagicAndVersion(std::string* error_msg) const {
  if (!IsMagicValid(header_->magic_)) {
    std::ostringstream oss;
    oss << "Unrecognized magic number in "  << GetLocation() << ":"
            << " " << header_->magic_[0]
            << " " << header_->magic_[1]
            << " " << header_->magic_[2]
            << " " << header_->magic_[3];
    *error_msg = oss.str();
    return false;
  }
  if (!IsVersionValid(header_->magic_)) {
    std::ostringstream oss;
    oss << "Unrecognized version number in "  << GetLocation() << ":"
            << " " << header_->magic_[4]
            << " " << header_->magic_[5]
            << " " << header_->magic_[6]
            << " " << header_->magic_[7];
    *error_msg = oss.str();
    return false;
  }
  return true;
}

bool DexFile::IsMagicValid(const uint8_t* magic) {
  return (memcmp(magic, kDexMagic, sizeof(kDexMagic)) == 0);
}

bool DexFile::IsVersionValid(const uint8_t* magic) {
  const uint8_t* version = &magic[sizeof(kDexMagic)];
  for (uint32_t i = 0; i < kNumDexVersions; i++) {
    if (memcmp(version, kDexMagicVersions[i], kDexVersionLen) == 0) {
      return true;
    }
  }
  return false;
}

uint32_t DexFile::Header::GetVersion() const {
  const char* version = reinterpret_cast<const char*>(&magic_[sizeof(kDexMagic)]);
  return atoi(version);
}

const DexFile::ClassDef* DexFile::FindClassDef(uint16_t type_idx) const {
  size_t num_class_defs = NumClassDefs();
  // Fast path for rare no class defs case.
  if (num_class_defs == 0) {
    return nullptr;
  }
  for (size_t i = 0; i < num_class_defs; ++i) {
    const ClassDef& class_def = GetClassDef(i);
    if (class_def.class_idx_ == type_idx) {
      return &class_def;
    }
  }
  return nullptr;
}

uint32_t DexFile::FindCodeItemOffset(const DexFile::ClassDef& class_def,
                                     uint32_t method_idx) const {
  const uint8_t* class_data = GetClassData(class_def);
  CHECK(class_data != nullptr);
  ClassDataItemIterator it(*this, class_data);
  // Skip fields
  while (it.HasNextStaticField()) {
    it.Next();
  }
  while (it.HasNextInstanceField()) {
    it.Next();
  }
  while (it.HasNextDirectMethod()) {
    if (it.GetMemberIndex() == method_idx) {
      return it.GetMethodCodeItemOffset();
    }
    it.Next();
  }
  while (it.HasNextVirtualMethod()) {
    if (it.GetMemberIndex() == method_idx) {
      return it.GetMethodCodeItemOffset();
    }
    it.Next();
  }
  LOG(FATAL) << "Unable to find method " << method_idx;
  UNREACHABLE();
}

const DexFile::FieldId* DexFile::FindFieldId(const DexFile::TypeId& declaring_klass,
                                             const DexFile::StringId& name,
                                             const DexFile::TypeId& type) const {
  // Binary search MethodIds knowing that they are sorted by class_idx, name_idx then proto_idx
  const uint16_t class_idx = GetIndexForTypeId(declaring_klass);
  const uint32_t name_idx = GetIndexForStringId(name);
  const uint16_t type_idx = GetIndexForTypeId(type);
  int32_t lo = 0;
  int32_t hi = NumFieldIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const DexFile::FieldId& field = GetFieldId(mid);
    if (class_idx > field.class_idx_) {
      lo = mid + 1;
    } else if (class_idx < field.class_idx_) {
      hi = mid - 1;
    } else {
      if (name_idx > field.name_idx_) {
        lo = mid + 1;
      } else if (name_idx < field.name_idx_) {
        hi = mid - 1;
      } else {
        if (type_idx > field.type_idx_) {
          lo = mid + 1;
        } else if (type_idx < field.type_idx_) {
          hi = mid - 1;
        } else {
          return &field;
        }
      }
    }
  }
  return nullptr;
}

const DexFile::MethodId* DexFile::FindMethodId(const DexFile::TypeId& declaring_klass,
                                               const DexFile::StringId& name,
                                               const DexFile::ProtoId& signature) const {
  // Binary search MethodIds knowing that they are sorted by class_idx, name_idx then proto_idx
  const uint16_t class_idx = GetIndexForTypeId(declaring_klass);
  const uint32_t name_idx = GetIndexForStringId(name);
  const uint16_t proto_idx = GetIndexForProtoId(signature);
  int32_t lo = 0;
  int32_t hi = NumMethodIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const DexFile::MethodId& method = GetMethodId(mid);
    if (class_idx > method.class_idx_) {
      lo = mid + 1;
    } else if (class_idx < method.class_idx_) {
      hi = mid - 1;
    } else {
      if (name_idx > method.name_idx_) {
        lo = mid + 1;
      } else if (name_idx < method.name_idx_) {
        hi = mid - 1;
      } else {
        if (proto_idx > method.proto_idx_) {
          lo = mid + 1;
        } else if (proto_idx < method.proto_idx_) {
          hi = mid - 1;
        } else {
          return &method;
        }
      }
    }
  }
  return nullptr;
}

const DexFile::StringId* DexFile::FindStringId(const char* string) const {
  int32_t lo = 0;
  int32_t hi = NumStringIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const DexFile::StringId& str_id = GetStringId(mid);
    const char* str = GetStringData(str_id);
    int compare = CompareModifiedUtf8ToModifiedUtf8AsUtf16CodePointValues(string, str);
    if (compare > 0) {
      lo = mid + 1;
    } else if (compare < 0) {
      hi = mid - 1;
    } else {
      return &str_id;
    }
  }
  return nullptr;
}

const DexFile::TypeId* DexFile::FindTypeId(const char* string) const {
  int32_t lo = 0;
  int32_t hi = NumTypeIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const TypeId& type_id = GetTypeId(mid);
    const DexFile::StringId& str_id = GetStringId(type_id.descriptor_idx_);
    const char* str = GetStringData(str_id);
    int compare = CompareModifiedUtf8ToModifiedUtf8AsUtf16CodePointValues(string, str);
    if (compare > 0) {
      lo = mid + 1;
    } else if (compare < 0) {
      hi = mid - 1;
    } else {
      return &type_id;
    }
  }
  return nullptr;
}

const DexFile::StringId* DexFile::FindStringId(const uint16_t* string, size_t length) const {
  int32_t lo = 0;
  int32_t hi = NumStringIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const DexFile::StringId& str_id = GetStringId(mid);
    const char* str = GetStringData(str_id);
    int compare = CompareModifiedUtf8ToUtf16AsCodePointValues(str, string, length);
    if (compare > 0) {
      lo = mid + 1;
    } else if (compare < 0) {
      hi = mid - 1;
    } else {
      return &str_id;
    }
  }
  return nullptr;
}

const DexFile::TypeId* DexFile::FindTypeId(uint32_t string_idx) const {
  int32_t lo = 0;
  int32_t hi = NumTypeIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const TypeId& type_id = GetTypeId(mid);
    if (string_idx > type_id.descriptor_idx_) {
      lo = mid + 1;
    } else if (string_idx < type_id.descriptor_idx_) {
      hi = mid - 1;
    } else {
      return &type_id;
    }
  }
  return nullptr;
}

const DexFile::ProtoId* DexFile::FindProtoId(uint16_t return_type_idx,
                                             const uint16_t* signature_type_idxs,
                                             uint32_t signature_length) const {
  int32_t lo = 0;
  int32_t hi = NumProtoIds() - 1;
  while (hi >= lo) {
    int32_t mid = (hi + lo) / 2;
    const DexFile::ProtoId& proto = GetProtoId(mid);
    int compare = return_type_idx - proto.return_type_idx_;
    if (compare == 0) {
      DexFileParameterIterator it(*this, proto);
      size_t i = 0;
      while (it.HasNext() && i < signature_length && compare == 0) {
        compare = signature_type_idxs[i] - it.GetTypeIdx();
        it.Next();
        i++;
      }
      if (compare == 0) {
        if (it.HasNext()) {
          compare = -1;
        } else if (i < signature_length) {
          compare = 1;
        }
      }
    }
    if (compare > 0) {
      lo = mid + 1;
    } else if (compare < 0) {
      hi = mid - 1;
    } else {
      return &proto;
    }
  }
  return nullptr;
}

// Given a signature place the type ids into the given vector
bool DexFile::CreateTypeList(const StringPiece& signature, uint16_t* return_type_idx,
                             std::vector<uint16_t>* param_type_idxs) const {
  if (signature[0] != '(') {
    return false;
  }
  size_t offset = 1;
  size_t end = signature.size();
  bool process_return = false;
  while (offset < end) {
    size_t start_offset = offset;
    char c = signature[offset];
    offset++;
    if (c == ')') {
      process_return = true;
      continue;
    }
    while (c == '[') {  // process array prefix
      if (offset >= end) {  // expect some descriptor following [
        return false;
      }
      c = signature[offset];
      offset++;
    }
    if (c == 'L') {  // process type descriptors
      do {
        if (offset >= end) {  // unexpected early termination of descriptor
          return false;
        }
        c = signature[offset];
        offset++;
      } while (c != ';');
    }
    // TODO: avoid creating a std::string just to get a 0-terminated char array
    std::string descriptor(signature.data() + start_offset, offset - start_offset);
    const DexFile::TypeId* type_id = FindTypeId(descriptor.c_str());
    if (type_id == nullptr) {
      return false;
    }
    uint16_t type_idx = GetIndexForTypeId(*type_id);
    if (!process_return) {
      param_type_idxs->push_back(type_idx);
    } else {
      *return_type_idx = type_idx;
      return offset == end;  // return true if the signature had reached a sensible end
    }
  }
  return false;  // failed to correctly parse return type
}

const Signature DexFile::CreateSignature(const StringPiece& signature) const {
  uint16_t return_type_idx;
  std::vector<uint16_t> param_type_indices;
  bool success = CreateTypeList(signature, &return_type_idx, &param_type_indices);
  if (!success) {
    return Signature::NoSignature();
  }
  const ProtoId* proto_id = FindProtoId(return_type_idx, param_type_indices);
  if (proto_id == nullptr) {
    return Signature::NoSignature();
  }
  return Signature(this, *proto_id);
}

int32_t DexFile::FindTryItem(const CodeItem &code_item, uint32_t address) {
  // Note: Signed type is important for max and min.
  int32_t min = 0;
  int32_t max = code_item.tries_size_ - 1;

  while (min <= max) {
    int32_t mid = min + ((max - min) / 2);

    const art::DexFile::TryItem* ti = GetTryItems(code_item, mid);
    uint32_t start = ti->start_addr_;
    uint32_t end = start + ti->insn_count_;

    if (address < start) {
      max = mid - 1;
    } else if (address >= end) {
      min = mid + 1;
    } else {  // We have a winner!
      return mid;
    }
  }
  // No match.
  return -1;
}

int32_t DexFile::FindCatchHandlerOffset(const CodeItem &code_item, uint32_t address) {
  int32_t try_item = FindTryItem(code_item, address);
  if (try_item == -1) {
    return -1;
  } else {
    return DexFile::GetTryItems(code_item, try_item)->handler_off_;
  }
}

bool DexFile::DecodeDebugLocalInfo(const CodeItem* code_item, bool is_static, uint32_t method_idx,
                                   DexDebugNewLocalCb local_cb, void* context) const {
  DCHECK(local_cb != nullptr);
  if (code_item == nullptr) {
    return false;
  }
  const uint8_t* stream = GetDebugInfoStream(code_item);
  if (stream == nullptr) {
    return false;
  }
  std::vector<LocalInfo> local_in_reg(code_item->registers_size_);

  uint16_t arg_reg = code_item->registers_size_ - code_item->ins_size_;
  if (!is_static) {
    const char* descriptor = GetMethodDeclaringClassDescriptor(GetMethodId(method_idx));
    local_in_reg[arg_reg].name_ = "this";
    local_in_reg[arg_reg].descriptor_ = descriptor;
    local_in_reg[arg_reg].signature_ = nullptr;
    local_in_reg[arg_reg].start_address_ = 0;
    local_in_reg[arg_reg].reg_ = arg_reg;
    local_in_reg[arg_reg].is_live_ = true;
    arg_reg++;
  }

  DexFileParameterIterator it(*this, GetMethodPrototype(GetMethodId(method_idx)));
  DecodeUnsignedLeb128(&stream);  // Line.
  uint32_t parameters_size = DecodeUnsignedLeb128(&stream);
  uint32_t i;
  for (i = 0; i < parameters_size && it.HasNext(); ++i, it.Next()) {
    if (arg_reg >= code_item->registers_size_) {
      LOG(ERROR) << "invalid stream - arg reg >= reg size (" << arg_reg
                 << " >= " << code_item->registers_size_ << ") in " << GetLocation();
      return false;
    }
    uint32_t name_idx = DecodeUnsignedLeb128P1(&stream);
    const char* descriptor = it.GetDescriptor();
    local_in_reg[arg_reg].name_ = StringDataByIdx(name_idx);
    local_in_reg[arg_reg].descriptor_ = descriptor;
    local_in_reg[arg_reg].signature_ = nullptr;
    local_in_reg[arg_reg].start_address_ = 0;
    local_in_reg[arg_reg].reg_ = arg_reg;
    local_in_reg[arg_reg].is_live_ = true;
    switch (*descriptor) {
      case 'D':
      case 'J':
        arg_reg += 2;
        break;
      default:
        arg_reg += 1;
        break;
    }
  }
  if (i != parameters_size || it.HasNext()) {
    LOG(ERROR) << "invalid stream - problem with parameter iterator in " << GetLocation()
               << " for method " << PrettyMethod(method_idx, *this);
    return false;
  }

  uint32_t address = 0;
  for (;;)  {
    uint8_t opcode = *stream++;
    switch (opcode) {
      case DBG_END_SEQUENCE:
        // Emit all variables which are still alive at the end of the method.
        for (uint16_t reg = 0; reg < code_item->registers_size_; reg++) {
          if (local_in_reg[reg].is_live_) {
            local_in_reg[reg].end_address_ = code_item->insns_size_in_code_units_;
            local_cb(context, local_in_reg[reg]);
          }
        }
        return true;
      case DBG_ADVANCE_PC:
        address += DecodeUnsignedLeb128(&stream);
        break;
      case DBG_ADVANCE_LINE:
        DecodeSignedLeb128(&stream);  // Line.
        break;
      case DBG_START_LOCAL:
      case DBG_START_LOCAL_EXTENDED: {
        uint16_t reg = DecodeUnsignedLeb128(&stream);
        if (reg >= code_item->registers_size_) {
          LOG(ERROR) << "invalid stream - reg >= reg size (" << reg << " >= "
                     << code_item->registers_size_ << ") in " << GetLocation();
          return false;
        }

        uint32_t name_idx = DecodeUnsignedLeb128P1(&stream);
        uint32_t descriptor_idx = DecodeUnsignedLeb128P1(&stream);
        uint32_t signature_idx = kDexNoIndex;
        if (opcode == DBG_START_LOCAL_EXTENDED) {
          signature_idx = DecodeUnsignedLeb128P1(&stream);
        }

        // Emit what was previously there, if anything
        if (local_in_reg[reg].is_live_) {
          local_in_reg[reg].end_address_ = address;
          local_cb(context, local_in_reg[reg]);
        }

        local_in_reg[reg].name_ = StringDataByIdx(name_idx);
        local_in_reg[reg].descriptor_ = StringByTypeIdx(descriptor_idx);
        local_in_reg[reg].signature_ = StringDataByIdx(signature_idx);
        local_in_reg[reg].start_address_ = address;
        local_in_reg[reg].reg_ = reg;
        local_in_reg[reg].is_live_ = true;
        break;
      }
      case DBG_END_LOCAL: {
        uint16_t reg = DecodeUnsignedLeb128(&stream);
        if (reg >= code_item->registers_size_) {
          LOG(ERROR) << "invalid stream - reg >= reg size (" << reg << " >= "
                     << code_item->registers_size_ << ") in " << GetLocation();
          return false;
        }
        if (!local_in_reg[reg].is_live_) {
          LOG(ERROR) << "invalid stream - end without start in " << GetLocation();
          return false;
        }
        local_in_reg[reg].end_address_ = address;
        local_cb(context, local_in_reg[reg]);
        local_in_reg[reg].is_live_ = false;
        break;
      }
      case DBG_RESTART_LOCAL: {
        uint16_t reg = DecodeUnsignedLeb128(&stream);
        if (reg >= code_item->registers_size_) {
          LOG(ERROR) << "invalid stream - reg >= reg size (" << reg << " >= "
                     << code_item->registers_size_ << ") in " << GetLocation();
          return false;
        }
        // If the register is live, the "restart" is superfluous,
        // and we don't want to mess with the existing start address.
        if (!local_in_reg[reg].is_live_) {
          local_in_reg[reg].start_address_ = address;
          local_in_reg[reg].is_live_ = true;
        }
        break;
      }
      case DBG_SET_PROLOGUE_END:
      case DBG_SET_EPILOGUE_BEGIN:
        break;
      case DBG_SET_FILE:
        DecodeUnsignedLeb128P1(&stream);  // name.
        break;
      default:
        address += (opcode - DBG_FIRST_SPECIAL) / DBG_LINE_RANGE;
        break;
    }
  }
}

bool DexFile::DecodeDebugPositionInfo(const CodeItem* code_item, DexDebugNewPositionCb position_cb,
                                      void* context) const {
  DCHECK(position_cb != nullptr);
  if (code_item == nullptr) {
    return false;
  }
  const uint8_t* stream = GetDebugInfoStream(code_item);
  if (stream == nullptr) {
    return false;
  }

  PositionInfo entry = PositionInfo();
  entry.line_ = DecodeUnsignedLeb128(&stream);
  uint32_t parameters_size = DecodeUnsignedLeb128(&stream);
  for (uint32_t i = 0; i < parameters_size; ++i) {
    DecodeUnsignedLeb128P1(&stream);  // Parameter name.
  }

  for (;;)  {
    uint8_t opcode = *stream++;
    switch (opcode) {
      case DBG_END_SEQUENCE:
        return true;  // end of stream.
      case DBG_ADVANCE_PC:
        entry.address_ += DecodeUnsignedLeb128(&stream);
        break;
      case DBG_ADVANCE_LINE:
        entry.line_ += DecodeSignedLeb128(&stream);
        break;
      case DBG_START_LOCAL:
        DecodeUnsignedLeb128(&stream);  // reg.
        DecodeUnsignedLeb128P1(&stream);  // name.
        DecodeUnsignedLeb128P1(&stream);  // descriptor.
        break;
      case DBG_START_LOCAL_EXTENDED:
        DecodeUnsignedLeb128(&stream);  // reg.
        DecodeUnsignedLeb128P1(&stream);  // name.
        DecodeUnsignedLeb128P1(&stream);  // descriptor.
        DecodeUnsignedLeb128P1(&stream);  // signature.
        break;
      case DBG_END_LOCAL:
      case DBG_RESTART_LOCAL:
        DecodeUnsignedLeb128(&stream);  // reg.
        break;
      case DBG_SET_PROLOGUE_END:
        entry.prologue_end_ = true;
        break;
      case DBG_SET_EPILOGUE_BEGIN:
        entry.epilogue_begin_ = true;
        break;
      case DBG_SET_FILE: {
        uint32_t name_idx = DecodeUnsignedLeb128P1(&stream);
        entry.source_file_ = StringDataByIdx(name_idx);
        break;
      }
      default: {
        int adjopcode = opcode - DBG_FIRST_SPECIAL;
        entry.address_ += adjopcode / DBG_LINE_RANGE;
        entry.line_ += DBG_LINE_BASE + (adjopcode % DBG_LINE_RANGE);
        if (position_cb(context, entry)) {
          return true;  // early exit.
        }
        entry.prologue_end_ = false;
        entry.epilogue_begin_ = false;
        break;
      }
    }
  }
}

bool DexFile::LineNumForPcCb(void* raw_context, const PositionInfo& entry) {
  LineNumFromPcContext* context = reinterpret_cast<LineNumFromPcContext*>(raw_context);

  // We know that this callback will be called in
  // ascending address order, so keep going until we find
  // a match or we've just gone past it.
  if (entry.address_ > context->address_) {
    // The line number from the previous positions callback
    // wil be the final result.
    return true;
  } else {
    context->line_num_ = entry.line_;
    return entry.address_ == context->address_;
  }
}

bool DexFile::IsMultiDexLocation(const char* location) {
  return strrchr(location, kMultiDexSeparator) != nullptr;
}

std::string DexFile::GetMultiDexClassesDexName(size_t index) {
  if (index == 0) {
    return "classes.dex";
  } else {
    return StringPrintf("classes%zu.dex", index + 1);
  }
}

std::string DexFile::GetMultiDexLocation(size_t index, const char* dex_location) {
  if (index == 0) {
    return dex_location;
  } else {
    return StringPrintf("%s" kMultiDexSeparatorString "classes%zu.dex", dex_location, index + 1);
  }
}

std::string DexFile::GetDexCanonicalLocation(const char* dex_location) {
  CHECK_NE(dex_location, static_cast<const char*>(nullptr));
  std::string base_location = GetBaseLocation(dex_location);
  const char* suffix = dex_location + base_location.size();
  DCHECK(suffix[0] == 0 || suffix[0] == kMultiDexSeparator);
  UniqueCPtr<const char[]> path(realpath(base_location.c_str(), nullptr));
  if (path != nullptr && path.get() != base_location) {
    return std::string(path.get()) + suffix;
  } else if (suffix[0] == 0) {
    return base_location;
  } else {
    return dex_location;
  }
}

// Read a signed integer.  "zwidth" is the zero-based byte count.
int32_t DexFile::ReadSignedInt(const uint8_t* ptr, int zwidth) {
  int32_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = ((uint32_t)val >> 8) | (((int32_t)*ptr++) << 24);
  }
  val >>= (3 - zwidth) * 8;
  return val;
}

// Read an unsigned integer.  "zwidth" is the zero-based byte count,
// "fill_on_right" indicates which side we want to zero-fill from.
uint32_t DexFile::ReadUnsignedInt(const uint8_t* ptr, int zwidth, bool fill_on_right) {
  uint32_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = (val >> 8) | (((uint32_t)*ptr++) << 24);
  }
  if (!fill_on_right) {
    val >>= (3 - zwidth) * 8;
  }
  return val;
}

// Read a signed long.  "zwidth" is the zero-based byte count.
int64_t DexFile::ReadSignedLong(const uint8_t* ptr, int zwidth) {
  int64_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = ((uint64_t)val >> 8) | (((int64_t)*ptr++) << 56);
  }
  val >>= (7 - zwidth) * 8;
  return val;
}

// Read an unsigned long.  "zwidth" is the zero-based byte count,
// "fill_on_right" indicates which side we want to zero-fill from.
uint64_t DexFile::ReadUnsignedLong(const uint8_t* ptr, int zwidth, bool fill_on_right) {
  uint64_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = (val >> 8) | (((uint64_t)*ptr++) << 56);
  }
  if (!fill_on_right) {
    val >>= (7 - zwidth) * 8;
  }
  return val;
}

// Checks that visibility is as expected. Includes special behavior for M and
// before to allow runtime and build visibility when expecting runtime.
std::ostream& operator<<(std::ostream& os, const DexFile& dex_file) {
  os << StringPrintf("[DexFile: %s dex-checksum=%08x location-checksum=%08x %p-%p]",
                     dex_file.GetLocation().c_str(),
                     dex_file.GetHeader().checksum_, dex_file.GetLocationChecksum(),
                     dex_file.Begin(), dex_file.Begin() + dex_file.Size());
  return os;
}

std::string Signature::ToString() const {
  if (dex_file_ == nullptr) {
    CHECK(proto_id_ == nullptr);
    return "<no signature>";
  }
  const DexFile::TypeList* params = dex_file_->GetProtoParameters(*proto_id_);
  std::string result;
  if (params == nullptr) {
    result += "()";
  } else {
    result += "(";
    for (uint32_t i = 0; i < params->Size(); ++i) {
      result += dex_file_->StringByTypeIdx(params->GetTypeItem(i).type_idx_);
    }
    result += ")";
  }
  result += dex_file_->StringByTypeIdx(proto_id_->return_type_idx_);
  return result;
}

bool Signature::operator==(const StringPiece& rhs) const {
  if (dex_file_ == nullptr) {
    return false;
  }
  StringPiece tail(rhs);
  if (!tail.starts_with("(")) {
    return false;  // Invalid signature
  }
  tail.remove_prefix(1);  // "(";
  const DexFile::TypeList* params = dex_file_->GetProtoParameters(*proto_id_);
  if (params != nullptr) {
    for (uint32_t i = 0; i < params->Size(); ++i) {
      StringPiece param(dex_file_->StringByTypeIdx(params->GetTypeItem(i).type_idx_));
      if (!tail.starts_with(param)) {
        return false;
      }
      tail.remove_prefix(param.length());
    }
  }
  if (!tail.starts_with(")")) {
    return false;
  }
  tail.remove_prefix(1);  // ")";
  return tail == dex_file_->StringByTypeIdx(proto_id_->return_type_idx_);
}

std::ostream& operator<<(std::ostream& os, const Signature& sig) {
  return os << sig.ToString();
}

// Decodes the header section from the class data bytes.
void ClassDataItemIterator::ReadClassDataHeader() {
  CHECK(ptr_pos_ != nullptr);
  header_.static_fields_size_ = DecodeUnsignedLeb128(&ptr_pos_);
  header_.instance_fields_size_ = DecodeUnsignedLeb128(&ptr_pos_);
  header_.direct_methods_size_ = DecodeUnsignedLeb128(&ptr_pos_);
  header_.virtual_methods_size_ = DecodeUnsignedLeb128(&ptr_pos_);
}

void ClassDataItemIterator::ReadClassDataField() {
  field_.field_idx_delta_ = DecodeUnsignedLeb128(&ptr_pos_);
  field_.access_flags_ = DecodeUnsignedLeb128(&ptr_pos_);
  // The user of the iterator is responsible for checking if there
  // are unordered or duplicate indexes.
}

void ClassDataItemIterator::ReadClassDataMethod() {
  method_.method_idx_delta_ = DecodeUnsignedLeb128(&ptr_pos_);
  method_.access_flags_ = DecodeUnsignedLeb128(&ptr_pos_);
  method_.code_off_ = DecodeUnsignedLeb128(&ptr_pos_);
  if (last_idx_ != 0 && method_.method_idx_delta_ == 0) {
    LOG(WARNING) << "Duplicate method in " << dex_file_.GetLocation();
  }
}

EncodedStaticFieldValueIterator::EncodedStaticFieldValueIterator(const DexFile& dex_file,
                                                                 const DexFile::ClassDef& class_def)
    : dex_file_(dex_file),
      array_size_(),
      pos_(-1),
      type_(kByte) {
  ptr_ = dex_file_.GetEncodedStaticFieldValuesArray(class_def);
  if (ptr_ == nullptr) {
    array_size_ = 0;
  } else {
    array_size_ = DecodeUnsignedLeb128(&ptr_);
  }
  if (array_size_ > 0) {
    Next();
  }
}

void EncodedStaticFieldValueIterator::Next() {
  pos_++;
  if (pos_ >= array_size_) {
    return;
  }
  uint8_t value_type = *ptr_++;
  uint8_t value_arg = value_type >> kEncodedValueArgShift;
  size_t width = value_arg + 1;  // assume and correct later
  type_ = static_cast<ValueType>(value_type & kEncodedValueTypeMask);
  switch (type_) {
  case kBoolean:
    jval_.i = (value_arg != 0) ? 1 : 0;
    width = 0;
    break;
  case kByte:
    jval_.i = DexFile::ReadSignedInt(ptr_, value_arg);
    CHECK(IsInt<8>(jval_.i));
    break;
  case kShort:
    jval_.i = DexFile::ReadSignedInt(ptr_, value_arg);
    CHECK(IsInt<16>(jval_.i));
    break;
  case kChar:
    jval_.i = DexFile::ReadUnsignedInt(ptr_, value_arg, false);
    CHECK(IsUint<16>(jval_.i));
    break;
  case kInt:
    jval_.i = DexFile::ReadSignedInt(ptr_, value_arg);
    break;
  case kLong:
    jval_.j = DexFile::ReadSignedLong(ptr_, value_arg);
    break;
  case kFloat:
    jval_.i = DexFile::ReadUnsignedInt(ptr_, value_arg, true);
    break;
  case kDouble:
    jval_.j = DexFile::ReadUnsignedLong(ptr_, value_arg, true);
    break;
  case kString:
  case kType:
    jval_.i = DexFile::ReadUnsignedInt(ptr_, value_arg, false);
    break;
  case kField:
  case kMethod:
  case kEnum:
  case kArray:
  case kAnnotation:
    UNIMPLEMENTED(FATAL) << ": type " << type_;
    UNREACHABLE();
  case kNull:
    jval_.l = nullptr;
    width = 0;
    break;
  default:
    LOG(FATAL) << "Unreached";
    UNREACHABLE();
  }
  ptr_ += width;
}

CatchHandlerIterator::CatchHandlerIterator(const DexFile::CodeItem& code_item, uint32_t address) {
  handler_.address_ = -1;
  int32_t offset = -1;

  // Short-circuit the overwhelmingly common cases.
  switch (code_item.tries_size_) {
    case 0:
      break;
    case 1: {
      const DexFile::TryItem* tries = DexFile::GetTryItems(code_item, 0);
      uint32_t start = tries->start_addr_;
      if (address >= start) {
        uint32_t end = start + tries->insn_count_;
        if (address < end) {
          offset = tries->handler_off_;
        }
      }
      break;
    }
    default:
      offset = DexFile::FindCatchHandlerOffset(code_item, address);
  }
  Init(code_item, offset);
}

CatchHandlerIterator::CatchHandlerIterator(const DexFile::CodeItem& code_item,
                                           const DexFile::TryItem& try_item) {
  handler_.address_ = -1;
  Init(code_item, try_item.handler_off_);
}

void CatchHandlerIterator::Init(const DexFile::CodeItem& code_item,
                                int32_t offset) {
  if (offset >= 0) {
    Init(DexFile::GetCatchHandlerData(code_item, offset));
  } else {
    // Not found, initialize as empty
    current_data_ = nullptr;
    remaining_count_ = -1;
    catch_all_ = false;
    DCHECK(!HasNext());
  }
}

void CatchHandlerIterator::Init(const uint8_t* handler_data) {
  current_data_ = handler_data;
  remaining_count_ = DecodeSignedLeb128(&current_data_);

  // If remaining_count_ is non-positive, then it is the negative of
  // the number of catch types, and the catches are followed by a
  // catch-all handler.
  if (remaining_count_ <= 0) {
    catch_all_ = true;
    remaining_count_ = -remaining_count_;
  } else {
    catch_all_ = false;
  }
  Next();
}

void CatchHandlerIterator::Next() {
  if (remaining_count_ > 0) {
    handler_.type_idx_ = DecodeUnsignedLeb128(&current_data_);
    handler_.address_  = DecodeUnsignedLeb128(&current_data_);
    remaining_count_--;
    return;
  }

  if (catch_all_) {
    handler_.type_idx_ = DexFile::kDexNoIndex16;
    handler_.address_  = DecodeUnsignedLeb128(&current_data_);
    catch_all_ = false;
    return;
  }

  // no more handler
  remaining_count_ = -1;
}

}  // namespace art
