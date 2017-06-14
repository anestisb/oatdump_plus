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

#include "class_loader_context.h"

#include "base/dchecked_vector.h"
#include "base/stl_util.h"
#include "class_linker.h"
#include "dex_file.h"
#include "oat_file_assistant.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"

namespace art {

static constexpr char kPathClassLoaderString[] = "PCL";
static constexpr char kDelegateLastClassLoaderString[] = "DLC";
static constexpr char kClassLoaderOpeningMark = '[';
static constexpr char kClassLoaderClosingMark = ']';
static constexpr char kClassLoaderSep = ';';
static constexpr char kClasspathSep = ':';

ClassLoaderContext::ClassLoaderContext()
    : special_shared_library_(false),
      dex_files_open_attempted_(false),
      dex_files_open_result_(false) {}

std::unique_ptr<ClassLoaderContext> ClassLoaderContext::Create(const std::string& spec) {
  std::unique_ptr<ClassLoaderContext> result(new ClassLoaderContext());
  if (result->Parse(spec)) {
    return result;
  } else {
    return nullptr;
  }
}

// The expected format is: "ClassLoaderType1[ClasspathElem1:ClasspathElem2...]".
bool ClassLoaderContext::ParseClassLoaderSpec(const std::string& class_loader_spec,
                                              ClassLoaderType class_loader_type) {
  const char* class_loader_type_str = GetClassLoaderTypeName(class_loader_type);
  size_t type_str_size = strlen(class_loader_type_str);

  CHECK_EQ(0, class_loader_spec.compare(0, type_str_size, class_loader_type_str));

  // Check the opening and closing markers.
  if (class_loader_spec[type_str_size] != kClassLoaderOpeningMark) {
    return false;
  }
  if (class_loader_spec[class_loader_spec.length() - 1] != kClassLoaderClosingMark) {
    return false;
  }

  // At this point we know the format is ok; continue and extract the classpath.
  // Note that class loaders with an empty class path are allowed.
  std::string classpath = class_loader_spec.substr(type_str_size + 1,
                                                   class_loader_spec.length() - type_str_size - 2);

  class_loader_chain_.push_back(ClassLoaderInfo(class_loader_type));
  Split(classpath, kClasspathSep, &class_loader_chain_.back().classpath);

  return true;
}

// Extracts the class loader type from the given spec.
// Return ClassLoaderContext::kInvalidClassLoader if the class loader type is not
// recognized.
ClassLoaderContext::ClassLoaderType
ClassLoaderContext::ExtractClassLoaderType(const std::string& class_loader_spec) {
  const ClassLoaderType kValidTypes[] = {kPathClassLoader, kDelegateLastClassLoader};
  for (const ClassLoaderType& type : kValidTypes) {
    const char* type_str = GetClassLoaderTypeName(type);
    if (class_loader_spec.compare(0, strlen(type_str), type_str) == 0) {
      return type;
    }
  }
  return kInvalidClassLoader;
}

// The format: ClassLoaderType1[ClasspathElem1:ClasspathElem2...];ClassLoaderType2[...]...
// ClassLoaderType is either "PCL" (PathClassLoader) or "DLC" (DelegateLastClassLoader).
// ClasspathElem is the path of dex/jar/apk file.
bool ClassLoaderContext::Parse(const std::string& spec) {
  if (spec.empty()) {
    LOG(ERROR) << "Empty string passed to Parse";
    return false;
  }
  // Stop early if we detect the special shared library, which may be passed as the classpath
  // for dex2oat when we want to skip the shared libraries check.
  if (spec == OatFile::kSpecialSharedLibrary) {
    LOG(INFO) << "The ClassLoaderContext is a special shared library.";
    special_shared_library_ = true;
    return true;
  }

  std::vector<std::string> class_loaders;
  Split(spec, kClassLoaderSep, &class_loaders);

  for (const std::string& class_loader : class_loaders) {
    ClassLoaderType type = ExtractClassLoaderType(class_loader);
    if (type == kInvalidClassLoader) {
      LOG(ERROR) << "Invalid class loader type: " << class_loader;
      return false;
    }
    if (!ParseClassLoaderSpec(class_loader, type)) {
      LOG(ERROR) << "Invalid class loader spec: " << class_loader;
      return false;
    }
  }
  return true;
}

// Opens requested class path files and appends them to opened_dex_files. If the dex files have
// been stripped, this opens them from their oat files (which get added to opened_oat_files).
bool ClassLoaderContext::OpenDexFiles(InstructionSet isa, const std::string& classpath_dir) {
  CHECK(!dex_files_open_attempted_) << "OpenDexFiles should not be called twice";

  dex_files_open_attempted_ = true;
  // Assume we can open all dex files. If not, we will set this to false as we go.
  dex_files_open_result_ = true;

  if (special_shared_library_) {
    // Nothing to open if the context is a special shared library.
    return true;
  }

  // Note that we try to open all dex files even if some fail.
  // We may get resource-only apks which we cannot load.
  // TODO(calin): Refine the dex opening interface to be able to tell if an archive contains
  // no dex files. So that we can distinguish the real failures...
  for (ClassLoaderInfo& info : class_loader_chain_) {
    for (const std::string& cp_elem : info.classpath) {
      // If path is relative, append it to the provided base directory.
      std::string location = cp_elem;
      if (location[0] != '/') {
        location = classpath_dir + '/' + location;
      }
      std::string error_msg;
      // When opening the dex files from the context we expect their checksum to match their
      // contents. So pass true to verify_checksum.
      if (!DexFile::Open(location.c_str(),
                         location.c_str(),
                         /*verify_checksum*/ true,
                         &error_msg,
                         &info.opened_dex_files)) {
        // If we fail to open the dex file because it's been stripped, try to open the dex file
        // from its corresponding oat file.
        // This could happen when we need to recompile a pre-build whose dex code has been stripped.
        // (for example, if the pre-build is only quicken and we want to re-compile it
        // speed-profile).
        // TODO(calin): Use the vdex directly instead of going through the oat file.
        OatFileAssistant oat_file_assistant(location.c_str(), isa, false);
        std::unique_ptr<OatFile> oat_file(oat_file_assistant.GetBestOatFile());
        std::vector<std::unique_ptr<const DexFile>> oat_dex_files;
        if (oat_file != nullptr &&
            OatFileAssistant::LoadDexFiles(*oat_file, location, &oat_dex_files)) {
          info.opened_oat_files.push_back(std::move(oat_file));
          info.opened_dex_files.insert(info.opened_dex_files.end(),
                                       std::make_move_iterator(oat_dex_files.begin()),
                                       std::make_move_iterator(oat_dex_files.end()));
        } else {
          LOG(WARNING) << "Could not open dex files from location: " << location;
          dex_files_open_result_ = false;
        }
      }
    }
  }

  return dex_files_open_result_;
}

bool ClassLoaderContext::RemoveLocationsFromClassPaths(
    const dchecked_vector<std::string>& locations) {
  CHECK(!dex_files_open_attempted_)
      << "RemoveLocationsFromClasspaths cannot be call after OpenDexFiles";

  std::set<std::string> canonical_locations;
  for (const std::string& location : locations) {
    canonical_locations.insert(DexFile::GetDexCanonicalLocation(location.c_str()));
  }
  bool removed_locations = false;
  for (ClassLoaderInfo& info : class_loader_chain_) {
    size_t initial_size = info.classpath.size();
    auto kept_it = std::remove_if(
        info.classpath.begin(),
        info.classpath.end(),
        [canonical_locations](const std::string& location) {
            return ContainsElement(canonical_locations,
                                   DexFile::GetDexCanonicalLocation(location.c_str()));
        });
    info.classpath.erase(kept_it, info.classpath.end());
    if (initial_size != info.classpath.size()) {
      removed_locations = true;
    }
  }
  return removed_locations;
}

std::string ClassLoaderContext::EncodeContextForOatFile(const std::string& base_dir) const {
  CheckDexFilesOpened("EncodeContextForOatFile");
  if (special_shared_library_) {
    return OatFile::kSpecialSharedLibrary;
  }

  if (class_loader_chain_.empty()) {
    return "";
  }

  // TODO(calin): Transition period: assume we only have a classloader until
  // the oat file assistant implements the full class loader check.
  CHECK_EQ(1u, class_loader_chain_.size());

  return OatFile::EncodeDexFileDependencies(MakeNonOwningPointerVector(
      class_loader_chain_[0].opened_dex_files), base_dir);
}

jobject ClassLoaderContext::CreateClassLoader(
    const std::vector<const DexFile*>& compilation_sources) const {
  CheckDexFilesOpened("CreateClassLoader");

  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);

  std::vector<const DexFile*> class_path_files;

  // TODO(calin): Transition period: assume we only have a classloader until
  // the oat file assistant implements the full class loader check.
  if (!class_loader_chain_.empty()) {
    CHECK_EQ(1u, class_loader_chain_.size());
    CHECK_EQ(kPathClassLoader, class_loader_chain_[0].type);
    class_path_files = MakeNonOwningPointerVector(class_loader_chain_[0].opened_dex_files);
  }

  // Classpath: first the class-path given; then the dex files we'll compile.
  // Thus we'll resolve the class-path first.
  class_path_files.insert(class_path_files.end(),
                          compilation_sources.begin(),
                          compilation_sources.end());

  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  return class_linker->CreatePathClassLoader(self, class_path_files);
}

std::vector<const DexFile*> ClassLoaderContext::FlattenOpenedDexFiles() const {
  CheckDexFilesOpened("FlattenOpenedDexFiles");

  std::vector<const DexFile*> result;
  for (const ClassLoaderInfo& info : class_loader_chain_) {
    for (const std::unique_ptr<const DexFile>& dex_file : info.opened_dex_files) {
      result.push_back(dex_file.get());
    }
  }
  return result;
}

const char* ClassLoaderContext::GetClassLoaderTypeName(ClassLoaderType type) {
  switch (type) {
    case kPathClassLoader: return kPathClassLoaderString;
    case kDelegateLastClassLoader: return kDelegateLastClassLoaderString;
    default:
      LOG(FATAL) << "Invalid class loader type " << type;
      UNREACHABLE();
  }
}

void ClassLoaderContext::CheckDexFilesOpened(const std::string& calling_method) const {
  CHECK(dex_files_open_attempted_)
      << "Dex files were not successfully opened before the call to " << calling_method
      << "attempt=" << dex_files_open_attempted_ << ", result=" << dex_files_open_result_;
}
}  // namespace art

