/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "profile_compilation_info.h"

#include "errno.h"
#include <limits.h>
#include <vector>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "base/mutex.h"
#include "base/scoped_flock.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/unix_file/fd_file.h"
#include "jit/profiling_info.h"
#include "os.h"
#include "safe_map.h"
#include "utils.h"

namespace art {

const uint8_t ProfileCompilationInfo::kProfileMagic[] = { 'p', 'r', 'o', '\0' };
// Last profile version: fix profman merges. Update profile version to force
// regeneration of possibly faulty profiles.
const uint8_t ProfileCompilationInfo::kProfileVersion[] = { '0', '0', '5', '\0' };

static constexpr uint16_t kMaxDexFileKeyLength = PATH_MAX;

// Debug flag to ignore checksums when testing if a method or a class is present in the profile.
// Used to facilitate testing profile guided compilation across a large number of apps
// using the same test profile.
static constexpr bool kDebugIgnoreChecksum = false;

static constexpr uint8_t kIsMissingTypesEncoding = 6;
static constexpr uint8_t kIsMegamorphicEncoding = 7;

static_assert(sizeof(InlineCache::kIndividualCacheSize) == sizeof(uint8_t),
              "InlineCache::kIndividualCacheSize does not have the expect type size");
static_assert(InlineCache::kIndividualCacheSize < kIsMegamorphicEncoding,
              "InlineCache::kIndividualCacheSize is larger than expected");
static_assert(InlineCache::kIndividualCacheSize < kIsMissingTypesEncoding,
              "InlineCache::kIndividualCacheSize is larger than expected");

ProfileCompilationInfo::ProfileCompilationInfo(const ProfileCompilationInfo& pci) {
  MergeWith(pci);
}

ProfileCompilationInfo::~ProfileCompilationInfo() {
  ClearProfile();
}

void ProfileCompilationInfo::DexPcData::AddClass(uint16_t dex_profile_idx,
                                                 const dex::TypeIndex& type_idx) {
  if (is_megamorphic || is_missing_types) {
    return;
  }
  classes.emplace(dex_profile_idx, type_idx);
  if (classes.size() >= InlineCache::kIndividualCacheSize) {
    is_megamorphic = true;
    classes.clear();
  }
}

// Transform the actual dex location into relative paths.
// Note: this is OK because we don't store profiles of different apps into the same file.
// Apps with split apks don't cause trouble because each split has a different name and will not
// collide with other entries.
std::string ProfileCompilationInfo::GetProfileDexFileKey(const std::string& dex_location) {
  DCHECK(!dex_location.empty());
  size_t last_sep_index = dex_location.find_last_of('/');
  if (last_sep_index == std::string::npos) {
    return dex_location;
  } else {
    DCHECK(last_sep_index < dex_location.size());
    return dex_location.substr(last_sep_index + 1);
  }
}

bool ProfileCompilationInfo::AddMethodsAndClasses(
    const std::vector<ProfileMethodInfo>& methods,
    const std::set<DexCacheResolvedClasses>& resolved_classes) {
  for (const ProfileMethodInfo& method : methods) {
    if (!AddMethod(method)) {
      return false;
    }
  }
  for (const DexCacheResolvedClasses& dex_cache : resolved_classes) {
    if (!AddResolvedClasses(dex_cache)) {
      return false;
    }
  }
  return true;
}

bool ProfileCompilationInfo::Load(const std::string& filename, bool clear_if_invalid) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  ScopedFlock flock;
  std::string error;
  int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC;
  // There's no need to fsync profile data right away. We get many chances
  // to write it again in case something goes wrong. We can rely on a simple
  // close(), no sync, and let to the kernel decide when to write to disk.
  if (!flock.Init(filename.c_str(), flags, /*block*/false, /*flush_on_close*/false, &error)) {
    LOG(WARNING) << "Couldn't lock the profile file " << filename << ": " << error;
    return false;
  }

  int fd = flock.GetFile()->Fd();

  ProfileLoadSatus status = LoadInternal(fd, &error);
  if (status == kProfileLoadSuccess) {
    return true;
  }

  if (clear_if_invalid &&
      ((status == kProfileLoadVersionMismatch) || (status == kProfileLoadBadData))) {
    LOG(WARNING) << "Clearing bad or obsolete profile data from file "
                 << filename << ": " << error;
    if (flock.GetFile()->ClearContent()) {
      return true;
    } else {
      PLOG(WARNING) << "Could not clear profile file: " << filename;
      return false;
    }
  }

  LOG(WARNING) << "Could not load profile data from file " << filename << ": " << error;
  return false;
}

bool ProfileCompilationInfo::Save(const std::string& filename, uint64_t* bytes_written) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  ScopedFlock flock;
  std::string error;
  int flags = O_WRONLY | O_NOFOLLOW | O_CLOEXEC;
  // There's no need to fsync profile data right away. We get many chances
  // to write it again in case something goes wrong. We can rely on a simple
  // close(), no sync, and let to the kernel decide when to write to disk.
  if (!flock.Init(filename.c_str(), flags, /*block*/false, /*flush_on_close*/false, &error)) {
    LOG(WARNING) << "Couldn't lock the profile file " << filename << ": " << error;
    return false;
  }

  int fd = flock.GetFile()->Fd();

  // We need to clear the data because we don't support appending to the profiles yet.
  if (!flock.GetFile()->ClearContent()) {
    PLOG(WARNING) << "Could not clear profile file: " << filename;
    return false;
  }

  // This doesn't need locking because we are trying to lock the file for exclusive
  // access and fail immediately if we can't.
  bool result = Save(fd);
  if (result) {
    int64_t size = GetFileSizeBytes(filename);
    if (size != -1) {
      VLOG(profiler)
        << "Successfully saved profile info to " << filename << " Size: "
        << size;
      if (bytes_written != nullptr) {
        *bytes_written = static_cast<uint64_t>(size);
      }
    }
  } else {
    VLOG(profiler) << "Failed to save profile info to " << filename;
  }
  return result;
}

// Returns true if all the bytes were successfully written to the file descriptor.
static bool WriteBuffer(int fd, const uint8_t* buffer, size_t byte_count) {
  while (byte_count > 0) {
    int bytes_written = TEMP_FAILURE_RETRY(write(fd, buffer, byte_count));
    if (bytes_written == -1) {
      return false;
    }
    byte_count -= bytes_written;  // Reduce the number of remaining bytes.
    buffer += bytes_written;  // Move the buffer forward.
  }
  return true;
}

// Add the string bytes to the buffer.
static void AddStringToBuffer(std::vector<uint8_t>* buffer, const std::string& value) {
  buffer->insert(buffer->end(), value.begin(), value.end());
}

// Insert each byte, from low to high into the buffer.
template <typename T>
static void AddUintToBuffer(std::vector<uint8_t>* buffer, T value) {
  for (size_t i = 0; i < sizeof(T); i++) {
    buffer->push_back((value >> (i * kBitsPerByte)) & 0xff);
  }
}

static constexpr size_t kLineHeaderSize =
    2 * sizeof(uint16_t) +  // class_set.size + dex_location.size
    2 * sizeof(uint32_t);   // method_map.size + checksum

/**
 * Serialization format:
 *    magic,version,number_of_dex_files
 *    dex_location1,number_of_classes1,methods_region_size,dex_location_checksum1, \
 *        method_encoding_11,method_encoding_12...,class_id1,class_id2...
 *    dex_location2,number_of_classes2,methods_region_size,dex_location_checksum2, \
 *        method_encoding_21,method_encoding_22...,,class_id1,class_id2...
 *    .....
 * The method_encoding is:
 *    method_id,number_of_inline_caches,inline_cache1,inline_cache2...
 * The inline_cache is:
 *    dex_pc,[M|dex_map_size], dex_profile_index,class_id1,class_id2...,dex_profile_index2,...
 *    dex_map_size is the number of dex_indeces that follows.
 *       Classes are grouped per their dex files and the line
 *       `dex_profile_index,class_id1,class_id2...,dex_profile_index2,...` encodes the
 *       mapping from `dex_profile_index` to the set of classes `class_id1,class_id2...`
 *    M stands for megamorphic or missing types and it's encoded as either
 *    the byte kIsMegamorphicEncoding or kIsMissingTypesEncoding.
 *    When present, there will be no class ids following.
 **/
bool ProfileCompilationInfo::Save(int fd) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_GE(fd, 0);

  // Cache at most 50KB before writing.
  static constexpr size_t kMaxSizeToKeepBeforeWriting = 50 * KB;
  // Use a vector wrapper to avoid keeping track of offsets when we add elements.
  std::vector<uint8_t> buffer;
  WriteBuffer(fd, kProfileMagic, sizeof(kProfileMagic));
  WriteBuffer(fd, kProfileVersion, sizeof(kProfileVersion));
  DCHECK_LE(info_.size(), std::numeric_limits<uint8_t>::max());
  AddUintToBuffer(&buffer, static_cast<uint8_t>(info_.size()));

  // Dex files must be written in the order of their profile index. This
  // avoids writing the index in the output file and simplifies the parsing logic.
  for (const DexFileData* dex_data_ptr : info_) {
    const DexFileData& dex_data = *dex_data_ptr;
    if (buffer.size() > kMaxSizeToKeepBeforeWriting) {
      if (!WriteBuffer(fd, buffer.data(), buffer.size())) {
        return false;
      }
      buffer.clear();
    }

    // Note that we allow dex files without any methods or classes, so that
    // inline caches can refer valid dex files.

    if (dex_data.profile_key.size() >= kMaxDexFileKeyLength) {
      LOG(WARNING) << "DexFileKey exceeds allocated limit";
      return false;
    }

    // Make sure that the buffer has enough capacity to avoid repeated resizings
    // while we add data.
    uint32_t methods_region_size = GetMethodsRegionSize(dex_data);
    size_t required_capacity = buffer.size() +
        kLineHeaderSize +
        dex_data.profile_key.size() +
        sizeof(uint16_t) * dex_data.class_set.size() +
        methods_region_size;

    buffer.reserve(required_capacity);
    DCHECK_LE(dex_data.profile_key.size(), std::numeric_limits<uint16_t>::max());
    DCHECK_LE(dex_data.class_set.size(), std::numeric_limits<uint16_t>::max());
    AddUintToBuffer(&buffer, static_cast<uint16_t>(dex_data.profile_key.size()));
    AddUintToBuffer(&buffer, static_cast<uint16_t>(dex_data.class_set.size()));
    AddUintToBuffer(&buffer, methods_region_size);  // uint32_t
    AddUintToBuffer(&buffer, dex_data.checksum);  // uint32_t

    AddStringToBuffer(&buffer, dex_data.profile_key);

    for (const auto& method_it : dex_data.method_map) {
      AddUintToBuffer(&buffer, method_it.first);
      AddInlineCacheToBuffer(&buffer, method_it.second);
    }
    for (const auto& class_id : dex_data.class_set) {
      AddUintToBuffer(&buffer, class_id.index_);
    }

    DCHECK_LE(required_capacity, buffer.size())
        << "Failed to add the expected number of bytes in the buffer";
  }

  return WriteBuffer(fd, buffer.data(), buffer.size());
}

void ProfileCompilationInfo::AddInlineCacheToBuffer(std::vector<uint8_t>* buffer,
                                                    const InlineCacheMap& inline_cache_map) {
  // Add inline cache map size.
  AddUintToBuffer(buffer, static_cast<uint16_t>(inline_cache_map.size()));
  if (inline_cache_map.size() == 0) {
    return;
  }
  for (const auto& inline_cache_it : inline_cache_map) {
    uint16_t dex_pc = inline_cache_it.first;
    const DexPcData dex_pc_data = inline_cache_it.second;
    const ClassSet& classes = dex_pc_data.classes;

    // Add the dex pc.
    AddUintToBuffer(buffer, dex_pc);

    // Add the megamorphic/missing_types encoding if needed and continue.
    // In either cases we don't add any classes to the profiles and so there's
    // no point to continue.
    // TODO(calin): in case we miss types there is still value to add the
    // rest of the classes. They can be added without bumping the profile version.
    if (dex_pc_data.is_missing_types) {
      DCHECK(!dex_pc_data.is_megamorphic);  // at this point the megamorphic flag should not be set.
      DCHECK_EQ(classes.size(), 0u);
      AddUintToBuffer(buffer, kIsMissingTypesEncoding);
      continue;
    } else if (dex_pc_data.is_megamorphic) {
      DCHECK_EQ(classes.size(), 0u);
      AddUintToBuffer(buffer, kIsMegamorphicEncoding);
      continue;
    }

    DCHECK_LT(classes.size(), InlineCache::kIndividualCacheSize);
    DCHECK_NE(classes.size(), 0u) << "InlineCache contains a dex_pc with 0 classes";

    SafeMap<uint8_t, std::vector<dex::TypeIndex>> dex_to_classes_map;
    // Group the classes by dex. We expect that most of the classes will come from
    // the same dex, so this will be more efficient than encoding the dex index
    // for each class reference.
    GroupClassesByDex(classes, &dex_to_classes_map);
    // Add the dex map size.
    AddUintToBuffer(buffer, static_cast<uint8_t>(dex_to_classes_map.size()));
    for (const auto& dex_it : dex_to_classes_map) {
      uint8_t dex_profile_index = dex_it.first;
      const std::vector<dex::TypeIndex>& dex_classes = dex_it.second;
      // Add the dex profile index.
      AddUintToBuffer(buffer, dex_profile_index);
      // Add the the number of classes for each dex profile index.
      AddUintToBuffer(buffer, static_cast<uint8_t>(dex_classes.size()));
      for (size_t i = 0; i < dex_classes.size(); i++) {
        // Add the type index of the classes.
        AddUintToBuffer(buffer, dex_classes[i].index_);
      }
    }
  }
}

uint32_t ProfileCompilationInfo::GetMethodsRegionSize(const DexFileData& dex_data) {
  // ((uint16_t)method index + (uint16_t)inline cache size) * number of methods
  uint32_t size = 2 * sizeof(uint16_t) * dex_data.method_map.size();
  for (const auto& method_it : dex_data.method_map) {
    const InlineCacheMap& inline_cache = method_it.second;
    size += sizeof(uint16_t) * inline_cache.size();  // dex_pc
    for (const auto& inline_cache_it : inline_cache) {
      const ClassSet& classes = inline_cache_it.second.classes;
      SafeMap<uint8_t, std::vector<dex::TypeIndex>> dex_to_classes_map;
      GroupClassesByDex(classes, &dex_to_classes_map);
      size += sizeof(uint8_t);  // dex_to_classes_map size
      for (const auto& dex_it : dex_to_classes_map) {
        size += sizeof(uint8_t);  // dex profile index
        size += sizeof(uint8_t);  // number of classes
        const std::vector<dex::TypeIndex>& dex_classes = dex_it.second;
        size += sizeof(uint16_t) * dex_classes.size();  // the actual classes
      }
    }
  }
  return size;
}

void ProfileCompilationInfo::GroupClassesByDex(
    const ClassSet& classes,
    /*out*/SafeMap<uint8_t, std::vector<dex::TypeIndex>>* dex_to_classes_map) {
  for (const auto& classes_it : classes) {
    auto dex_it = dex_to_classes_map->FindOrAdd(classes_it.dex_profile_index);
    dex_it->second.push_back(classes_it.type_index);
  }
}

ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::GetOrAddDexFileData(
    const std::string& profile_key,
    uint32_t checksum) {
  const auto& profile_index_it = profile_key_map_.FindOrAdd(profile_key, profile_key_map_.size());
  if (profile_key_map_.size() > std::numeric_limits<uint8_t>::max()) {
    // Allow only 255 dex files to be profiled. This allows us to save bytes
    // when encoding. The number is well above what we expect for normal applications.
    if (kIsDebugBuild) {
      LOG(ERROR) << "Exceeded the maximum number of dex files (255). Something went wrong";
    }
    profile_key_map_.erase(profile_key);
    return nullptr;
  }

  uint8_t profile_index = profile_index_it->second;
  if (info_.size() <= profile_index) {
    // This is a new addition. Add it to the info_ array.
    info_.emplace_back(new DexFileData(profile_key, checksum, profile_index));
  }
  DexFileData* result = info_[profile_index];
  // DCHECK that profile info map key is consistent with the one stored in the dex file data.
  // This should always be the case since since the cache map is managed by ProfileCompilationInfo.
  DCHECK_EQ(profile_key, result->profile_key);
  DCHECK_EQ(profile_index, result->profile_index);

  // Check that the checksum matches.
  // This may different if for example the dex file was updated and
  // we had a record of the old one.
  if (result->checksum != checksum) {
    LOG(WARNING) << "Checksum mismatch for dex " << profile_key;
    return nullptr;
  }
  return result;
}

const ProfileCompilationInfo::DexFileData* ProfileCompilationInfo::FindDexData(
      const std::string& profile_key) const {
  const auto& profile_index_it = profile_key_map_.find(profile_key);
  if (profile_index_it == profile_key_map_.end()) {
    return nullptr;
  }

  uint8_t profile_index = profile_index_it->second;
  const DexFileData* result = info_[profile_index];
  DCHECK_EQ(profile_key, result->profile_key);
  DCHECK_EQ(profile_index, result->profile_index);
  return result;
}

bool ProfileCompilationInfo::AddResolvedClasses(const DexCacheResolvedClasses& classes) {
  const std::string dex_location = GetProfileDexFileKey(classes.GetDexLocation());
  const uint32_t checksum = classes.GetLocationChecksum();
  DexFileData* const data = GetOrAddDexFileData(dex_location, checksum);
  if (data == nullptr) {
    return false;
  }
  data->class_set.insert(classes.GetClasses().begin(), classes.GetClasses().end());
  return true;
}

bool ProfileCompilationInfo::AddMethodIndex(const std::string& dex_location,
                                            uint32_t dex_checksum,
                                            uint16_t method_index) {
  return AddMethod(dex_location, dex_checksum, method_index, OfflineProfileMethodInfo());
}

bool ProfileCompilationInfo::AddMethod(const std::string& dex_location,
                                       uint32_t dex_checksum,
                                       uint16_t method_index,
                                       const OfflineProfileMethodInfo& pmi) {
  DexFileData* const data = GetOrAddDexFileData(GetProfileDexFileKey(dex_location), dex_checksum);
  if (data == nullptr) {  // checksum mismatch
    return false;
  }
  auto inline_cache_it = data->method_map.FindOrAdd(method_index);
  for (const auto& pmi_inline_cache_it : pmi.inline_caches) {
    uint16_t pmi_ic_dex_pc = pmi_inline_cache_it.first;
    const DexPcData& pmi_ic_dex_pc_data = pmi_inline_cache_it.second;
    DexPcData& dex_pc_data = inline_cache_it->second.FindOrAdd(pmi_ic_dex_pc)->second;
    if (dex_pc_data.is_missing_types || dex_pc_data.is_megamorphic) {
      // We are already megamorphic or we are missing types; no point in going forward.
      continue;
    }

    if (pmi_ic_dex_pc_data.is_missing_types) {
      dex_pc_data.SetIsMissingTypes();
      continue;
    }
    if (pmi_ic_dex_pc_data.is_megamorphic) {
      dex_pc_data.SetIsMegamorphic();
      continue;
    }

    for (const ClassReference& class_ref : pmi_ic_dex_pc_data.classes) {
      const DexReference& dex_ref = pmi.dex_references[class_ref.dex_profile_index];
      DexFileData* class_dex_data = GetOrAddDexFileData(
          GetProfileDexFileKey(dex_ref.dex_location),
          dex_ref.dex_checksum);
      if (class_dex_data == nullptr) {  // checksum mismatch
        return false;
      }
      dex_pc_data.AddClass(class_dex_data->profile_index, class_ref.type_index);
    }
  }
  return true;
}

bool ProfileCompilationInfo::AddMethod(const ProfileMethodInfo& pmi) {
  DexFileData* const data = GetOrAddDexFileData(
      GetProfileDexFileKey(pmi.dex_file->GetLocation()),
      pmi.dex_file->GetLocationChecksum());
  if (data == nullptr) {  // checksum mismatch
    return false;
  }
  auto inline_cache_it = data->method_map.FindOrAdd(pmi.dex_method_index);

  for (const ProfileMethodInfo::ProfileInlineCache& cache : pmi.inline_caches) {
    if (cache.is_missing_types) {
      auto dex_pc_data_it = inline_cache_it->second.FindOrAdd(cache.dex_pc);
      dex_pc_data_it->second.SetIsMissingTypes();
      continue;
    }
    for (const ProfileMethodInfo::ProfileClassReference& class_ref : cache.classes) {
      DexFileData* class_dex_data = GetOrAddDexFileData(
          GetProfileDexFileKey(class_ref.dex_file->GetLocation()),
          class_ref.dex_file->GetLocationChecksum());
      if (class_dex_data == nullptr) {  // checksum mismatch
        return false;
      }
      auto dex_pc_data_it = inline_cache_it->second.FindOrAdd(cache.dex_pc);
      if (dex_pc_data_it->second.is_missing_types) {
        // Don't bother adding classes if we are missing types.
        break;
      }
      dex_pc_data_it->second.AddClass(class_dex_data->profile_index, class_ref.type_index);
    }
  }
  return true;
}

bool ProfileCompilationInfo::AddClassIndex(const std::string& dex_location,
                                           uint32_t checksum,
                                           dex::TypeIndex type_idx) {
  DexFileData* const data = GetOrAddDexFileData(dex_location, checksum);
  if (data == nullptr) {
    return false;
  }
  data->class_set.insert(type_idx);
  return true;
}

#define READ_UINT(type, buffer, dest, error)            \
  do {                                                  \
    if (!(buffer).ReadUintAndAdvance<type>(&(dest))) {  \
      *(error) = "Could not read "#dest;                \
      return false;                                     \
    }                                                   \
  }                                                     \
  while (false)

bool ProfileCompilationInfo::ReadInlineCache(SafeBuffer& buffer,
                                             uint8_t number_of_dex_files,
                                             /*out*/ InlineCacheMap* inline_cache,
                                             /*out*/ std::string* error) {
  uint16_t inline_cache_size;
  READ_UINT(uint16_t, buffer, inline_cache_size, error);
  for (; inline_cache_size > 0; inline_cache_size--) {
    uint16_t dex_pc;
    uint8_t dex_to_classes_map_size;
    READ_UINT(uint16_t, buffer, dex_pc, error);
    READ_UINT(uint8_t, buffer, dex_to_classes_map_size, error);
    auto dex_pc_data_it = inline_cache->FindOrAdd(dex_pc);
    if (dex_to_classes_map_size == kIsMissingTypesEncoding) {
      dex_pc_data_it->second.SetIsMissingTypes();
      continue;
    }
    if (dex_to_classes_map_size == kIsMegamorphicEncoding) {
      dex_pc_data_it->second.SetIsMegamorphic();
      continue;
    }
    for (; dex_to_classes_map_size > 0; dex_to_classes_map_size--) {
      uint8_t dex_profile_index;
      uint8_t dex_classes_size;
      READ_UINT(uint8_t, buffer, dex_profile_index, error);
      READ_UINT(uint8_t, buffer, dex_classes_size, error);
      if (dex_profile_index >= number_of_dex_files) {
        *error = "dex_profile_index out of bounds ";
        *error += std::to_string(dex_profile_index) + " " + std::to_string(number_of_dex_files);
        return false;
      }
      for (; dex_classes_size > 0; dex_classes_size--) {
        uint16_t type_index;
        READ_UINT(uint16_t, buffer, type_index, error);
        dex_pc_data_it->second.AddClass(dex_profile_index, dex::TypeIndex(type_index));
      }
    }
  }
  return true;
}

bool ProfileCompilationInfo::ReadMethods(SafeBuffer& buffer,
                                         uint8_t number_of_dex_files,
                                         const ProfileLineHeader& line_header,
                                         /*out*/std::string* error) {
  while (buffer.HasMoreData()) {
    DexFileData* const data = GetOrAddDexFileData(line_header.dex_location, line_header.checksum);
    uint16_t method_index;
    READ_UINT(uint16_t, buffer, method_index, error);

    auto it = data->method_map.FindOrAdd(method_index);
    if (!ReadInlineCache(buffer, number_of_dex_files, &(it->second), error)) {
      return false;
    }
  }

  return true;
}

bool ProfileCompilationInfo::ReadClasses(SafeBuffer& buffer,
                                         uint16_t classes_to_read,
                                         const ProfileLineHeader& line_header,
                                         /*out*/std::string* error) {
  for (uint16_t i = 0; i < classes_to_read; i++) {
    uint16_t type_index;
    READ_UINT(uint16_t, buffer, type_index, error);
    if (!AddClassIndex(line_header.dex_location,
                       line_header.checksum,
                       dex::TypeIndex(type_index))) {
      return false;
    }
  }
  return true;
}

// Tests for EOF by trying to read 1 byte from the descriptor.
// Returns:
//   0 if the descriptor is at the EOF,
//  -1 if there was an IO error
//   1 if the descriptor has more content to read
static int testEOF(int fd) {
  uint8_t buffer[1];
  return TEMP_FAILURE_RETRY(read(fd, buffer, 1));
}

// Reads an uint value previously written with AddUintToBuffer.
template <typename T>
bool ProfileCompilationInfo::SafeBuffer::ReadUintAndAdvance(/*out*/T* value) {
  static_assert(std::is_unsigned<T>::value, "Type is not unsigned");
  if (ptr_current_ + sizeof(T) > ptr_end_) {
    return false;
  }
  *value = 0;
  for (size_t i = 0; i < sizeof(T); i++) {
    *value += ptr_current_[i] << (i * kBitsPerByte);
  }
  ptr_current_ += sizeof(T);
  return true;
}

bool ProfileCompilationInfo::SafeBuffer::CompareAndAdvance(const uint8_t* data, size_t data_size) {
  if (ptr_current_ + data_size > ptr_end_) {
    return false;
  }
  if (memcmp(ptr_current_, data, data_size) == 0) {
    ptr_current_ += data_size;
    return true;
  }
  return false;
}

bool ProfileCompilationInfo::SafeBuffer::HasMoreData() {
  return ptr_current_ < ptr_end_;
}

ProfileCompilationInfo::ProfileLoadSatus ProfileCompilationInfo::SafeBuffer::FillFromFd(
      int fd,
      const std::string& source,
      /*out*/std::string* error) {
  size_t byte_count = ptr_end_ - ptr_current_;
  uint8_t* buffer = ptr_current_;
  while (byte_count > 0) {
    int bytes_read = TEMP_FAILURE_RETRY(read(fd, buffer, byte_count));
    if (bytes_read == 0) {
      *error += "Profile EOF reached prematurely for " + source;
      return kProfileLoadBadData;
    } else if (bytes_read < 0) {
      *error += "Profile IO error for " + source + strerror(errno);
      return kProfileLoadIOError;
    }
    byte_count -= bytes_read;
    buffer += bytes_read;
  }
  return kProfileLoadSuccess;
}

ProfileCompilationInfo::ProfileLoadSatus ProfileCompilationInfo::ReadProfileHeader(
      int fd,
      /*out*/uint8_t* number_of_dex_files,
      /*out*/std::string* error) {
  // Read magic and version
  const size_t kMagicVersionSize =
    sizeof(kProfileMagic) +
    sizeof(kProfileVersion) +
    sizeof(uint8_t);  // number of dex files

  SafeBuffer safe_buffer(kMagicVersionSize);

  ProfileLoadSatus status = safe_buffer.FillFromFd(fd, "ReadProfileHeader", error);
  if (status != kProfileLoadSuccess) {
    return status;
  }

  if (!safe_buffer.CompareAndAdvance(kProfileMagic, sizeof(kProfileMagic))) {
    *error = "Profile missing magic";
    return kProfileLoadVersionMismatch;
  }
  if (!safe_buffer.CompareAndAdvance(kProfileVersion, sizeof(kProfileVersion))) {
    *error = "Profile version mismatch";
    return kProfileLoadVersionMismatch;
  }
  if (!safe_buffer.ReadUintAndAdvance<uint8_t>(number_of_dex_files)) {
    *error = "Cannot read the number of dex files";
    return kProfileLoadBadData;
  }
  return kProfileLoadSuccess;
}

bool ProfileCompilationInfo::ReadProfileLineHeaderElements(SafeBuffer& buffer,
                                                           /*out*/uint16_t* dex_location_size,
                                                           /*out*/ProfileLineHeader* line_header,
                                                           /*out*/std::string* error) {
  READ_UINT(uint16_t, buffer, *dex_location_size, error);
  READ_UINT(uint16_t, buffer, line_header->class_set_size, error);
  READ_UINT(uint32_t, buffer, line_header->method_region_size_bytes, error);
  READ_UINT(uint32_t, buffer, line_header->checksum, error);
  return true;
}

ProfileCompilationInfo::ProfileLoadSatus ProfileCompilationInfo::ReadProfileLineHeader(
      int fd,
      /*out*/ProfileLineHeader* line_header,
      /*out*/std::string* error) {
  SafeBuffer header_buffer(kLineHeaderSize);
  ProfileLoadSatus status = header_buffer.FillFromFd(fd, "ReadProfileLineHeader", error);
  if (status != kProfileLoadSuccess) {
    return status;
  }

  uint16_t dex_location_size;
  if (!ReadProfileLineHeaderElements(header_buffer, &dex_location_size, line_header, error)) {
    return kProfileLoadBadData;
  }

  if (dex_location_size == 0 || dex_location_size > kMaxDexFileKeyLength) {
    *error = "DexFileKey has an invalid size: " +
        std::to_string(static_cast<uint32_t>(dex_location_size));
    return kProfileLoadBadData;
  }

  SafeBuffer location_buffer(dex_location_size);
  status = location_buffer.FillFromFd(fd, "ReadProfileHeaderDexLocation", error);
  if (status != kProfileLoadSuccess) {
    return status;
  }
  line_header->dex_location.assign(
      reinterpret_cast<char*>(location_buffer.Get()), dex_location_size);
  return kProfileLoadSuccess;
}

ProfileCompilationInfo::ProfileLoadSatus ProfileCompilationInfo::ReadProfileLine(
      int fd,
      uint8_t number_of_dex_files,
      const ProfileLineHeader& line_header,
      /*out*/std::string* error) {
  if (GetOrAddDexFileData(line_header.dex_location, line_header.checksum) == nullptr) {
    *error = "Error when reading profile file line header: checksum mismatch for "
        + line_header.dex_location;
    return kProfileLoadBadData;
  }

  {
    SafeBuffer buffer(line_header.method_region_size_bytes);
    ProfileLoadSatus status = buffer.FillFromFd(fd, "ReadProfileLineMethods", error);
    if (status != kProfileLoadSuccess) {
      return status;
    }

    if (!ReadMethods(buffer, number_of_dex_files, line_header, error)) {
      return kProfileLoadBadData;
    }
  }

  {
    SafeBuffer buffer(sizeof(uint16_t) * line_header.class_set_size);
    ProfileLoadSatus status = buffer.FillFromFd(fd, "ReadProfileLineClasses", error);
    if (status != kProfileLoadSuccess) {
      return status;
    }
    if (!ReadClasses(buffer, line_header.class_set_size, line_header, error)) {
      return kProfileLoadBadData;
    }
  }

  return kProfileLoadSuccess;
}

// TODO(calin): Fix this API. ProfileCompilationInfo::Load should be static and
// return a unique pointer to a ProfileCompilationInfo upon success.
bool ProfileCompilationInfo::Load(int fd) {
  std::string error;
  ProfileLoadSatus status = LoadInternal(fd, &error);

  if (status == kProfileLoadSuccess) {
    return true;
  } else {
    LOG(WARNING) << "Error when reading profile: " << error;
    return false;
  }
}

// TODO(calin): fail fast if the dex checksums don't match.
ProfileCompilationInfo::ProfileLoadSatus ProfileCompilationInfo::LoadInternal(
      int fd, std::string* error) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_GE(fd, 0);

  if (!IsEmpty()) {
    return kProfileLoadWouldOverwiteData;
  }

  struct stat stat_buffer;
  if (fstat(fd, &stat_buffer) != 0) {
    return kProfileLoadIOError;
  }
  // We allow empty profile files.
  // Profiles may be created by ActivityManager or installd before we manage to
  // process them in the runtime or profman.
  if (stat_buffer.st_size == 0) {
    return kProfileLoadSuccess;
  }
  // Read profile header: magic + version + number_of_dex_files.
  uint8_t number_of_dex_files;
  ProfileLoadSatus status = ReadProfileHeader(fd, &number_of_dex_files, error);
  if (status != kProfileLoadSuccess) {
    return status;
  }

  for (uint8_t k = 0; k < number_of_dex_files; k++) {
    ProfileLineHeader line_header;

    // First, read the line header to get the amount of data we need to read.
    status = ReadProfileLineHeader(fd, &line_header, error);
    if (status != kProfileLoadSuccess) {
      return status;
    }

    // Now read the actual profile line.
    status = ReadProfileLine(fd, number_of_dex_files, line_header, error);
    if (status != kProfileLoadSuccess) {
      return status;
    }
  }

  // Check that we read everything and that profiles don't contain junk data.
  int result = testEOF(fd);
  if (result == 0) {
    return kProfileLoadSuccess;
  } else if (result < 0) {
    return kProfileLoadIOError;
  } else {
    *error = "Unexpected content in the profile file";
    return kProfileLoadBadData;
  }
}

bool ProfileCompilationInfo::MergeWith(const ProfileCompilationInfo& other) {
  // First verify that all checksums match. This will avoid adding garbage to
  // the current profile info.
  // Note that the number of elements should be very small, so this should not
  // be a performance issue.
  for (const DexFileData* other_dex_data : other.info_) {
    const DexFileData* dex_data = FindDexData(other_dex_data->profile_key);
    if ((dex_data != nullptr) && (dex_data->checksum != other_dex_data->checksum)) {
      LOG(WARNING) << "Checksum mismatch for dex " << other_dex_data->profile_key;
      return false;
    }
  }
  // All checksums match. Import the data.

  // The other profile might have a different indexing of dex files.
  // That is because each dex files gets a 'dex_profile_index' on a first come first served basis.
  // That means that the order in with the methods are added to the profile matters for the
  // actual indices.
  // The reason we cannot rely on the actual multidex index is that a single profile may store
  // data from multiple splits. This means that a profile may contain a classes2.dex from split-A
  // and one from split-B.

  // First, build a mapping from other_dex_profile_index to this_dex_profile_index.
  // This will make sure that the ClassReferences  will point to the correct dex file.
  SafeMap<uint8_t, uint8_t> dex_profile_index_remap;
  for (const DexFileData* other_dex_data : other.info_) {
    const DexFileData* dex_data = GetOrAddDexFileData(other_dex_data->profile_key,
                                                      other_dex_data->checksum);
    if (dex_data == nullptr) {
      return false;  // Could happen if we exceed the number of allowed dex files.
    }
    dex_profile_index_remap.Put(other_dex_data->profile_index, dex_data->profile_index);
  }

  // Merge the actual profile data.
  for (const DexFileData* other_dex_data : other.info_) {
    DexFileData* dex_data = const_cast<DexFileData*>(FindDexData(other_dex_data->profile_key));
    DCHECK(dex_data != nullptr);

    // Merge the classes.
    dex_data->class_set.insert(other_dex_data->class_set.begin(),
                               other_dex_data->class_set.end());

    // Merge the methods and the inline caches.
    for (const auto& other_method_it : other_dex_data->method_map) {
      uint16_t other_method_index = other_method_it.first;
      auto method_it = dex_data->method_map.FindOrAdd(other_method_index);
      const auto& other_inline_cache = other_method_it.second;
      for (const auto& other_ic_it : other_inline_cache) {
        uint16_t other_dex_pc = other_ic_it.first;
        const ClassSet& other_class_set = other_ic_it.second.classes;
        auto class_set = method_it->second.FindOrAdd(other_dex_pc);
        if (other_ic_it.second.is_missing_types) {
          class_set->second.SetIsMissingTypes();
        } else if (other_ic_it.second.is_megamorphic) {
          class_set->second.SetIsMegamorphic();
        } else {
          for (const auto& class_it : other_class_set) {
            class_set->second.AddClass(dex_profile_index_remap.Get(
                class_it.dex_profile_index), class_it.type_index);
          }
        }
      }
    }
  }
  return true;
}

static bool ChecksumMatch(uint32_t dex_file_checksum, uint32_t checksum) {
  return kDebugIgnoreChecksum || dex_file_checksum == checksum;
}

static bool ChecksumMatch(const DexFile& dex_file, uint32_t checksum) {
  return ChecksumMatch(dex_file.GetLocationChecksum(), checksum);
}

bool ProfileCompilationInfo::ContainsMethod(const MethodReference& method_ref) const {
  return FindMethod(method_ref.dex_file->GetLocation(),
                    method_ref.dex_file->GetLocationChecksum(),
                    method_ref.dex_method_index) != nullptr;
}

const ProfileCompilationInfo::InlineCacheMap*
ProfileCompilationInfo::FindMethod(const std::string& dex_location,
                                   uint32_t dex_checksum,
                                   uint16_t dex_method_index) const {
  const DexFileData* dex_data = FindDexData(GetProfileDexFileKey(dex_location));
  if (dex_data != nullptr) {
    if (!ChecksumMatch(dex_checksum, dex_data->checksum)) {
      return nullptr;
    }
    const MethodMap& methods = dex_data->method_map;
    const auto method_it = methods.find(dex_method_index);
    return method_it == methods.end() ? nullptr : &(method_it->second);
  }
  return nullptr;
}

bool ProfileCompilationInfo::GetMethod(const std::string& dex_location,
                                       uint32_t dex_checksum,
                                       uint16_t dex_method_index,
                                       /*out*/OfflineProfileMethodInfo* pmi) const {
  const InlineCacheMap* inline_caches = FindMethod(dex_location, dex_checksum, dex_method_index);
  if (inline_caches == nullptr) {
    return false;
  }

  pmi->dex_references.resize(info_.size());
  for (const DexFileData* dex_data : info_) {
    pmi->dex_references[dex_data->profile_index].dex_location = dex_data->profile_key;
    pmi->dex_references[dex_data->profile_index].dex_checksum = dex_data->checksum;
  }

  // TODO(calin): maybe expose a direct pointer to avoid copying
  pmi->inline_caches = *inline_caches;
  return true;
}


bool ProfileCompilationInfo::ContainsClass(const DexFile& dex_file, dex::TypeIndex type_idx) const {
  const DexFileData* dex_data = FindDexData(GetProfileDexFileKey(dex_file.GetLocation()));
  if (dex_data != nullptr) {
    if (!ChecksumMatch(dex_file, dex_data->checksum)) {
      return false;
    }
    const std::set<dex::TypeIndex>& classes = dex_data->class_set;
    return classes.find(type_idx) != classes.end();
  }
  return false;
}

uint32_t ProfileCompilationInfo::GetNumberOfMethods() const {
  uint32_t total = 0;
  for (const DexFileData* dex_data : info_) {
    total += dex_data->method_map.size();
  }
  return total;
}

uint32_t ProfileCompilationInfo::GetNumberOfResolvedClasses() const {
  uint32_t total = 0;
  for (const DexFileData* dex_data : info_) {
    total += dex_data->class_set.size();
  }
  return total;
}

// Produce a non-owning vector from a vector.
template<typename T>
const std::vector<T*>* MakeNonOwningVector(const std::vector<std::unique_ptr<T>>* owning_vector) {
  auto non_owning_vector = new std::vector<T*>();
  for (auto& element : *owning_vector) {
    non_owning_vector->push_back(element.get());
  }
  return non_owning_vector;
}

std::string ProfileCompilationInfo::DumpInfo(
    const std::vector<std::unique_ptr<const DexFile>>* dex_files,
    bool print_full_dex_location) const {
  std::unique_ptr<const std::vector<const DexFile*>> non_owning_dex_files(
      MakeNonOwningVector(dex_files));
  return DumpInfo(non_owning_dex_files.get(), print_full_dex_location);
}

std::string ProfileCompilationInfo::DumpInfo(const std::vector<const DexFile*>* dex_files,
                                             bool print_full_dex_location) const {
  std::ostringstream os;
  if (info_.empty()) {
    return "ProfileInfo: empty";
  }

  os << "ProfileInfo:";

  const std::string kFirstDexFileKeySubstitute = ":classes.dex";

  for (const DexFileData* dex_data : info_) {
    os << "\n";
    if (print_full_dex_location) {
      os << dex_data->profile_key;
    } else {
      // Replace the (empty) multidex suffix of the first key with a substitute for easier reading.
      std::string multidex_suffix = DexFile::GetMultiDexSuffix(dex_data->profile_key);
      os << (multidex_suffix.empty() ? kFirstDexFileKeySubstitute : multidex_suffix);
    }
    os << " [index=" << static_cast<uint32_t>(dex_data->profile_index) << "]";
    const DexFile* dex_file = nullptr;
    if (dex_files != nullptr) {
      for (size_t i = 0; i < dex_files->size(); i++) {
        if (dex_data->profile_key == (*dex_files)[i]->GetLocation()) {
          dex_file = (*dex_files)[i];
        }
      }
    }
    os << "\n\tmethods: ";
    for (const auto& method_it : dex_data->method_map) {
      if (dex_file != nullptr) {
        os << "\n\t\t" << dex_file->PrettyMethod(method_it.first, true);
      } else {
        os << method_it.first;
      }

      os << "[";
      for (const auto& inline_cache_it : method_it.second) {
        os << "{" << std::hex << inline_cache_it.first << std::dec << ":";
        if (inline_cache_it.second.is_missing_types) {
          os << "MT";
        } else if (inline_cache_it.second.is_megamorphic) {
          os << "MM";
        } else {
          for (const ClassReference& class_ref : inline_cache_it.second.classes) {
            os << "(" << static_cast<uint32_t>(class_ref.dex_profile_index)
               << "," << class_ref.type_index.index_ << ")";
          }
        }
        os << "}";
      }
      os << "], ";
    }
    os << "\n\tclasses: ";
    for (const auto class_it : dex_data->class_set) {
      if (dex_file != nullptr) {
        os << "\n\t\t" << dex_file->PrettyType(class_it);
      } else {
        os << class_it.index_ << ",";
      }
    }
  }
  return os.str();
}

bool ProfileCompilationInfo::GetClassesAndMethods(const DexFile* dex_file,
                                                  std::set<dex::TypeIndex>* class_set,
                                                  MethodMap* method_map) const {
  std::set<std::string> ret;
  std::string profile_key = GetProfileDexFileKey(dex_file->GetLocation());
  const DexFileData* dex_data = FindDexData(profile_key);
  if (dex_data == nullptr || dex_data->checksum != dex_file->GetLocationChecksum()) {
    return false;
  }
  *method_map = dex_data->method_map;
  *class_set = dex_data->class_set;
  return true;
}

bool ProfileCompilationInfo::Equals(const ProfileCompilationInfo& other) {
  // No need to compare profile_key_map_. That's only a cache for fast search.
  // All the information is already in the info_ vector.
  if (info_.size() != other.info_.size()) {
    return false;
  }
  for (size_t i = 0; i < info_.size(); i++) {
    const DexFileData& dex_data = *info_[i];
    const DexFileData& other_dex_data = *other.info_[i];
    if (!(dex_data == other_dex_data)) {
      return false;
    }
  }
  return true;
}

std::set<DexCacheResolvedClasses> ProfileCompilationInfo::GetResolvedClasses(
    const std::unordered_set<std::string>& dex_files_locations) const {
  std::unordered_map<std::string, std::string> key_to_location_map;
  for (const std::string& location : dex_files_locations) {
    key_to_location_map.emplace(GetProfileDexFileKey(location), location);
  }
  std::set<DexCacheResolvedClasses> ret;
  for (const DexFileData* dex_data : info_) {
    const auto& it = key_to_location_map.find(dex_data->profile_key);
    if (it != key_to_location_map.end()) {
      DexCacheResolvedClasses classes(it->second, it->second, dex_data->checksum);
      classes.AddClasses(dex_data->class_set.begin(), dex_data->class_set.end());
      ret.insert(classes);
    }
  }
  return ret;
}

void ProfileCompilationInfo::ClearResolvedClasses() {
  for (DexFileData* dex_data : info_) {
    dex_data->class_set.clear();
  }
}

// Naive implementation to generate a random profile file suitable for testing.
bool ProfileCompilationInfo::GenerateTestProfile(int fd,
                                                 uint16_t number_of_dex_files,
                                                 uint16_t method_ratio,
                                                 uint16_t class_ratio,
                                                 uint32_t random_seed) {
  const std::string base_dex_location = "base.apk";
  ProfileCompilationInfo info;
  // The limits are defined by the dex specification.
  uint16_t max_method = std::numeric_limits<uint16_t>::max();
  uint16_t max_classes = std::numeric_limits<uint16_t>::max();
  uint16_t number_of_methods = max_method * method_ratio / 100;
  uint16_t number_of_classes = max_classes * class_ratio / 100;

  std::srand(random_seed);

  // Make sure we generate more samples with a low index value.
  // This makes it more likely to hit valid method/class indices in small apps.
  const uint16_t kFavorFirstN = 10000;
  const uint16_t kFavorSplit = 2;

  for (uint16_t i = 0; i < number_of_dex_files; i++) {
    std::string dex_location = DexFile::GetMultiDexLocation(i, base_dex_location.c_str());
    std::string profile_key = GetProfileDexFileKey(dex_location);

    for (uint16_t m = 0; m < number_of_methods; m++) {
      uint16_t method_idx = rand() % max_method;
      if (m < (number_of_methods / kFavorSplit)) {
        method_idx %= kFavorFirstN;
      }
      info.AddMethodIndex(profile_key, 0, method_idx);
    }

    for (uint16_t c = 0; c < number_of_classes; c++) {
      uint16_t type_idx = rand() % max_classes;
      if (c < (number_of_classes / kFavorSplit)) {
        type_idx %= kFavorFirstN;
      }
      info.AddClassIndex(profile_key, 0, dex::TypeIndex(type_idx));
    }
  }
  return info.Save(fd);
}

// Naive implementation to generate a random profile file suitable for testing.
bool ProfileCompilationInfo::GenerateTestProfile(
    int fd,
    std::vector<std::unique_ptr<const DexFile>>& dex_files,
    uint32_t random_seed) {
  std::srand(random_seed);
  ProfileCompilationInfo info;
  for (std::unique_ptr<const DexFile>& dex_file : dex_files) {
    const std::string& location = dex_file->GetLocation();
    uint32_t checksum = dex_file->GetLocationChecksum();
    for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
      // Randomly add a class from the dex file (with 50% chance).
      if (std::rand() % 2 != 0) {
        info.AddClassIndex(location, checksum, dex::TypeIndex(dex_file->GetClassDef(i).class_idx_));
      }
    }
    for (uint32_t i = 0; i < dex_file->NumMethodIds(); ++i) {
      // Randomly add a method from the dex file (with 50% chance).
      if (std::rand() % 2 != 0) {
        info.AddMethodIndex(location, checksum, i);
      }
    }
  }
  return info.Save(fd);
}

bool ProfileCompilationInfo::OfflineProfileMethodInfo::operator==(
      const OfflineProfileMethodInfo& other) const {
  if (inline_caches.size() != other.inline_caches.size()) {
    return false;
  }

  // We can't use a simple equality test because we need to match the dex files
  // of the inline caches which might have different profile indexes.
  for (const auto& inline_cache_it : inline_caches) {
    uint16_t dex_pc = inline_cache_it.first;
    const DexPcData dex_pc_data = inline_cache_it.second;
    const auto other_it = other.inline_caches.find(dex_pc);
    if (other_it == other.inline_caches.end()) {
      return false;
    }
    const DexPcData& other_dex_pc_data = other_it->second;
    if (dex_pc_data.is_megamorphic != other_dex_pc_data.is_megamorphic ||
        dex_pc_data.is_missing_types != other_dex_pc_data.is_missing_types) {
      return false;
    }
    for (const ClassReference& class_ref : dex_pc_data.classes) {
      bool found = false;
      for (const ClassReference& other_class_ref : other_dex_pc_data.classes) {
        CHECK_LE(class_ref.dex_profile_index, dex_references.size());
        CHECK_LE(other_class_ref.dex_profile_index, other.dex_references.size());
        const DexReference& dex_ref = dex_references[class_ref.dex_profile_index];
        const DexReference& other_dex_ref = other.dex_references[other_class_ref.dex_profile_index];
        if (class_ref.type_index == other_class_ref.type_index &&
            dex_ref == other_dex_ref) {
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
  }
  return true;
}

void ProfileCompilationInfo::ClearProfile() {
  for (DexFileData* dex_data : info_) {
    delete dex_data;
  }
  info_.clear();
  profile_key_map_.clear();
}

bool ProfileCompilationInfo::IsEmpty() const {
  DCHECK_EQ(info_.empty(), profile_key_map_.empty());
  return info_.empty();
}

}  // namespace art
