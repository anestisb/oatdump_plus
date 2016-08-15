/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "oat_file_assistant.h"

#include <sys/stat.h>
#include "base/logging.h"
#include "base/stringprintf.h"
#include "compiler_filter.h"
#include "class_linker.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "image.h"
#include "oat.h"
#include "os.h"
#include "runtime.h"
#include "scoped_thread_state_change.h"
#include "utils.h"

namespace art {

std::ostream& operator << (std::ostream& stream, const OatFileAssistant::OatStatus status) {
  switch (status) {
    case OatFileAssistant::kOatOutOfDate:
      stream << "kOatOutOfDate";
      break;
    case OatFileAssistant::kOatUpToDate:
      stream << "kOatUpToDate";
      break;
    case OatFileAssistant::kOatNeedsRelocation:
      stream << "kOatNeedsRelocation";
      break;
    default:
      UNREACHABLE();
  }

  return stream;
}

OatFileAssistant::OatFileAssistant(const char* dex_location,
                                   const InstructionSet isa,
                                   bool load_executable)
    : OatFileAssistant(dex_location, nullptr, isa, load_executable)
{ }

OatFileAssistant::OatFileAssistant(const char* dex_location,
                                   const char* oat_location,
                                   const InstructionSet isa,
                                   bool load_executable)
    : isa_(isa), load_executable_(load_executable), odex_(this), oat_(this) {
  CHECK(dex_location != nullptr) << "OatFileAssistant: null dex location";
  dex_location_.assign(dex_location);

  if (load_executable_ && isa != kRuntimeISA) {
    LOG(WARNING) << "OatFileAssistant: Load executable specified, "
      << "but isa is not kRuntimeISA. Will not attempt to load executable.";
    load_executable_ = false;
  }

  // Get the odex filename.
  std::string error_msg;
  std::string odex_file_name;
  if (DexLocationToOdexFilename(dex_location_, isa_, &odex_file_name, &error_msg)) {
    odex_.Reset(odex_file_name);
  } else {
    LOG(WARNING) << "Failed to determine odex file name: " << error_msg;
  }

  // Get the oat filename.
  if (oat_location != nullptr) {
    oat_.Reset(oat_location);
  } else {
    std::string oat_file_name;
    if (DexLocationToOatFilename(dex_location_, isa_, &oat_file_name, &error_msg)) {
      oat_.Reset(oat_file_name);
    } else {
      LOG(WARNING) << "Failed to determine oat file name for dex location "
        << dex_location_ << ": " << error_msg;
    }
  }
}

OatFileAssistant::~OatFileAssistant() {
  // Clean up the lock file.
  if (flock_.HasFile()) {
    unlink(flock_.GetFile()->GetPath().c_str());
  }
}

bool OatFileAssistant::IsInBootClassPath() {
  // Note: We check the current boot class path, regardless of the ISA
  // specified by the user. This is okay, because the boot class path should
  // be the same for all ISAs.
  // TODO: Can we verify the boot class path is the same for all ISAs?
  Runtime* runtime = Runtime::Current();
  ClassLinker* class_linker = runtime->GetClassLinker();
  const auto& boot_class_path = class_linker->GetBootClassPath();
  for (size_t i = 0; i < boot_class_path.size(); i++) {
    if (boot_class_path[i]->GetLocation() == dex_location_) {
      VLOG(oat) << "Dex location " << dex_location_ << " is in boot class path";
      return true;
    }
  }
  return false;
}

bool OatFileAssistant::Lock(std::string* error_msg) {
  CHECK(error_msg != nullptr);
  CHECK(!flock_.HasFile()) << "OatFileAssistant::Lock already acquired";

  const std::string* oat_file_name = oat_.Filename();
  if (oat_file_name == nullptr) {
    *error_msg = "Failed to determine lock file";
    return false;
  }
  std::string lock_file_name = *oat_file_name + ".flock";

  if (!flock_.Init(lock_file_name.c_str(), error_msg)) {
    unlink(lock_file_name.c_str());
    return false;
  }
  return true;
}

OatFileAssistant::DexOptNeeded
OatFileAssistant::GetDexOptNeeded(CompilerFilter::Filter target, bool profile_changed) {
  bool compilation_desired = CompilerFilter::IsBytecodeCompilationEnabled(target);

  // See if the oat file is in good shape as is.
  bool oat_okay = oat_.CompilerFilterIsOkay(target, profile_changed);
  if (oat_okay) {
    if (compilation_desired) {
      if (oat_.IsUpToDate()) {
        return kNoDexOptNeeded;
      }
    } else {
      if (!oat_.IsOutOfDate()) {
        return kNoDexOptNeeded;
      }
    }
  }

  // See if the odex file is in good shape as is.
  bool odex_okay = odex_.CompilerFilterIsOkay(target, profile_changed);
  if (odex_okay) {
    if (compilation_desired) {
      if (odex_.IsUpToDate()) {
        return kNoDexOptNeeded;
      }
    } else {
      if (!odex_.IsOutOfDate()) {
        return kNoDexOptNeeded;
      }
    }
  }

  // See if we can get an up-to-date file by running patchoat.
  if (compilation_desired) {
    if (odex_okay && odex_.NeedsRelocation() && odex_.HasPatchInfo()) {
      return kPatchOatNeeded;
    }

    if (oat_okay && oat_.NeedsRelocation() && oat_.HasPatchInfo()) {
      return kSelfPatchOatNeeded;
    }
  }

  // We can only run dex2oat if there are original dex files.
  return HasOriginalDexFiles() ? kDex2OatNeeded : kNoDexOptNeeded;
}

// Figure out the currently specified compile filter option in the runtime.
// Returns true on success, false if the compiler filter is invalid, in which
// case error_msg describes the problem.
static bool GetRuntimeCompilerFilterOption(CompilerFilter::Filter* filter,
                                           std::string* error_msg) {
  CHECK(filter != nullptr);
  CHECK(error_msg != nullptr);

  *filter = CompilerFilter::kDefaultCompilerFilter;
  for (StringPiece option : Runtime::Current()->GetCompilerOptions()) {
    if (option.starts_with("--compiler-filter=")) {
      const char* compiler_filter_string = option.substr(strlen("--compiler-filter=")).data();
      if (!CompilerFilter::ParseCompilerFilter(compiler_filter_string, filter)) {
        *error_msg = std::string("Unknown --compiler-filter value: ")
                   + std::string(compiler_filter_string);
        return false;
      }
    }
  }
  return true;
}

bool OatFileAssistant::IsUpToDate() {
  return OatFileIsUpToDate() || OdexFileIsUpToDate();
}

OatFileAssistant::ResultOfAttemptToUpdate
OatFileAssistant::MakeUpToDate(bool profile_changed, std::string* error_msg) {
  CompilerFilter::Filter target;
  if (!GetRuntimeCompilerFilterOption(&target, error_msg)) {
    return kUpdateNotAttempted;
  }

  switch (GetDexOptNeeded(target, profile_changed)) {
    case kNoDexOptNeeded: return kUpdateSucceeded;
    case kDex2OatNeeded: return GenerateOatFile(error_msg);
    case kPatchOatNeeded: return RelocateOatFile(odex_.Filename(), error_msg);
    case kSelfPatchOatNeeded: return RelocateOatFile(oat_.Filename(), error_msg);
  }
  UNREACHABLE();
}

std::unique_ptr<OatFile> OatFileAssistant::GetBestOatFile() {
  // The best oat files are, in descending order of bestness:
  // 1. Properly relocated files. These may be opened executable.
  // 2. Not out-of-date files that are already opened non-executable.
  // 3. Not out-of-date files that we must reopen non-executable.

  if (oat_.IsUpToDate()) {
    return oat_.ReleaseFile();
  }

  if (odex_.IsUpToDate()) {
    return odex_.ReleaseFile();
  }

  VLOG(oat) << "Oat File Assistant: No relocated oat file found,"
    << " attempting to fall back to interpreting oat file instead.";

  if (!oat_.IsOutOfDate() && !oat_.IsExecutable()) {
    return oat_.ReleaseFile();
  }

  if (!odex_.IsOutOfDate() && !odex_.IsExecutable()) {
    return odex_.ReleaseFile();
  }

  if (!oat_.IsOutOfDate()) {
    load_executable_ = false;
    oat_.Reset();
    if (!oat_.IsOutOfDate()) {
      CHECK(!oat_.IsExecutable());
      return oat_.ReleaseFile();
    }
  }

  if (!odex_.IsOutOfDate()) {
    load_executable_ = false;
    odex_.Reset();
    if (!odex_.IsOutOfDate()) {
      CHECK(!odex_.IsExecutable());
      return odex_.ReleaseFile();
    }
  }

  return std::unique_ptr<OatFile>();
}

std::vector<std::unique_ptr<const DexFile>> OatFileAssistant::LoadDexFiles(
    const OatFile& oat_file, const char* dex_location) {
  std::vector<std::unique_ptr<const DexFile>> dex_files;

  // Load the primary dex file.
  std::string error_msg;
  const OatFile::OatDexFile* oat_dex_file = oat_file.GetOatDexFile(
      dex_location, nullptr, &error_msg);
  if (oat_dex_file == nullptr) {
    LOG(WARNING) << error_msg;
    return std::vector<std::unique_ptr<const DexFile>>();
  }

  std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
  if (dex_file.get() == nullptr) {
    LOG(WARNING) << "Failed to open dex file from oat dex file: " << error_msg;
    return std::vector<std::unique_ptr<const DexFile>>();
  }
  dex_files.push_back(std::move(dex_file));

  // Load secondary multidex files
  for (size_t i = 1; ; i++) {
    std::string secondary_dex_location = DexFile::GetMultiDexLocation(i, dex_location);
    oat_dex_file = oat_file.GetOatDexFile(secondary_dex_location.c_str(), nullptr);
    if (oat_dex_file == nullptr) {
      // There are no more secondary dex files to load.
      break;
    }

    dex_file = oat_dex_file->OpenDexFile(&error_msg);
    if (dex_file.get() == nullptr) {
      LOG(WARNING) << "Failed to open dex file from oat dex file: " << error_msg;
      return std::vector<std::unique_ptr<const DexFile>>();
    }
    dex_files.push_back(std::move(dex_file));
  }
  return dex_files;
}

bool OatFileAssistant::HasOriginalDexFiles() {
  // Ensure GetRequiredDexChecksum has been run so that
  // has_original_dex_files_ is initialized. We don't care about the result of
  // GetRequiredDexChecksum.
  GetRequiredDexChecksum();
  return has_original_dex_files_;
}

const std::string* OatFileAssistant::OdexFileName() {
  return odex_.Filename();
}

bool OatFileAssistant::OdexFileExists() {
  return odex_.Exists();
}

OatFileAssistant::OatStatus OatFileAssistant::OdexFileStatus() {
  return odex_.Status();
}

bool OatFileAssistant::OdexFileIsOutOfDate() {
  return odex_.IsOutOfDate();
}

bool OatFileAssistant::OdexFileNeedsRelocation() {
  return odex_.NeedsRelocation();
}

bool OatFileAssistant::OdexFileIsUpToDate() {
  return odex_.IsUpToDate();
}

CompilerFilter::Filter OatFileAssistant::OdexFileCompilerFilter() {
  return odex_.CompilerFilter();
}

static std::string ArtFileName(const OatFile* oat_file) {
  const std::string oat_file_location = oat_file->GetLocation();
  // Replace extension with .art
  const size_t last_ext = oat_file_location.find_last_of('.');
  if (last_ext == std::string::npos) {
    LOG(ERROR) << "No extension in oat file " << oat_file_location;
    return std::string();
  }
  return oat_file_location.substr(0, last_ext) + ".art";
}

const std::string* OatFileAssistant::OatFileName() {
  return oat_.Filename();
}

bool OatFileAssistant::OatFileExists() {
  return oat_.Exists();
}

OatFileAssistant::OatStatus OatFileAssistant::OatFileStatus() {
  return oat_.Status();
}

bool OatFileAssistant::OatFileIsOutOfDate() {
  return oat_.IsOutOfDate();
}

bool OatFileAssistant::OatFileNeedsRelocation() {
  return oat_.NeedsRelocation();
}

bool OatFileAssistant::OatFileIsUpToDate() {
  return oat_.IsUpToDate();
}

CompilerFilter::Filter OatFileAssistant::OatFileCompilerFilter() {
  return oat_.CompilerFilter();
}

OatFileAssistant::OatStatus OatFileAssistant::GivenOatFileStatus(const OatFile& file) {
  // Verify the dex checksum.
  // Note: GetOatDexFile will return null if the dex checksum doesn't match
  // what we provide, which verifies the primary dex checksum for us.
  std::string error_msg;
  const uint32_t* dex_checksum_pointer = GetRequiredDexChecksum();
  const OatFile::OatDexFile* oat_dex_file = file.GetOatDexFile(
      dex_location_.c_str(), dex_checksum_pointer, &error_msg);
  if (oat_dex_file == nullptr) {
    VLOG(oat) << error_msg;
    return kOatOutOfDate;
  }

  // Verify the dex checksums for any secondary multidex files
  for (size_t i = 1; ; i++) {
    std::string secondary_dex_location = DexFile::GetMultiDexLocation(i, dex_location_.c_str());
    const OatFile::OatDexFile* secondary_oat_dex_file
      = file.GetOatDexFile(secondary_dex_location.c_str(), nullptr);
    if (secondary_oat_dex_file == nullptr) {
      // There are no more secondary dex files to check.
      break;
    }

    uint32_t expected_secondary_checksum = 0;
    if (DexFile::GetChecksum(secondary_dex_location.c_str(),
          &expected_secondary_checksum, &error_msg)) {
      uint32_t actual_secondary_checksum
        = secondary_oat_dex_file->GetDexFileLocationChecksum();
      if (expected_secondary_checksum != actual_secondary_checksum) {
        VLOG(oat) << "Dex checksum does not match for secondary dex: "
          << secondary_dex_location
          << ". Expected: " << expected_secondary_checksum
          << ", Actual: " << actual_secondary_checksum;
        return kOatOutOfDate;
      }
    } else {
      // If we can't get the checksum for the secondary location, we assume
      // the dex checksum is up to date for this and all other secondary dex
      // files.
      break;
    }
  }

  CompilerFilter::Filter current_compiler_filter = file.GetCompilerFilter();

  // Verify the image checksum
  if (CompilerFilter::DependsOnImageChecksum(current_compiler_filter)) {
    const ImageInfo* image_info = GetImageInfo();
    if (image_info == nullptr) {
      VLOG(oat) << "No image for oat image checksum to match against.";

      if (HasOriginalDexFiles()) {
        return kOatOutOfDate;
      }

      // If there is no original dex file to fall back to, grudgingly accept
      // the oat file. This could technically lead to crashes, but there's no
      // way we could find a better oat file to use for this dex location,
      // and it's better than being stuck in a boot loop with no way out.
      // The problem will hopefully resolve itself the next time the runtime
      // starts up.
      LOG(WARNING) << "Dex location " << dex_location_ << " does not seem to include dex file. "
        << "Allow oat file use. This is potentially dangerous.";
    } else if (file.GetOatHeader().GetImageFileLocationOatChecksum()
        != GetCombinedImageChecksum()) {
      VLOG(oat) << "Oat image checksum does not match image checksum.";
      return kOatOutOfDate;
    }
  } else {
    VLOG(oat) << "Image checksum test skipped for compiler filter " << current_compiler_filter;
  }

  if (CompilerFilter::IsBytecodeCompilationEnabled(current_compiler_filter)) {
    if (!file.IsPic()) {
      const ImageInfo* image_info = GetImageInfo();
      if (image_info == nullptr) {
        VLOG(oat) << "No image to check oat relocation against.";
        return kOatNeedsRelocation;
      }

      // Verify the oat_data_begin recorded for the image in the oat file matches
      // the actual oat_data_begin for boot.oat in the image.
      const OatHeader& oat_header = file.GetOatHeader();
      uintptr_t oat_data_begin = oat_header.GetImageFileLocationOatDataBegin();
      if (oat_data_begin != image_info->oat_data_begin) {
        VLOG(oat) << file.GetLocation() <<
          ": Oat file image oat_data_begin (" << oat_data_begin << ")"
          << " does not match actual image oat_data_begin ("
          << image_info->oat_data_begin << ")";
        return kOatNeedsRelocation;
      }

      // Verify the oat_patch_delta recorded for the image in the oat file matches
      // the actual oat_patch_delta for the image.
      int32_t oat_patch_delta = oat_header.GetImagePatchDelta();
      if (oat_patch_delta != image_info->patch_delta) {
        VLOG(oat) << file.GetLocation() <<
          ": Oat file image patch delta (" << oat_patch_delta << ")"
          << " does not match actual image patch delta ("
          << image_info->patch_delta << ")";
        return kOatNeedsRelocation;
      }
    } else {
      // Oat files compiled in PIC mode do not require relocation.
      VLOG(oat) << "Oat relocation test skipped for PIC oat file";
    }
  } else {
    VLOG(oat) << "Oat relocation test skipped for compiler filter " << current_compiler_filter;
  }
  return kOatUpToDate;
}

OatFileAssistant::ResultOfAttemptToUpdate
OatFileAssistant::RelocateOatFile(const std::string* input_file, std::string* error_msg) {
  CHECK(error_msg != nullptr);

  if (input_file == nullptr) {
    *error_msg = "Patching of oat file for dex location " + dex_location_
      + " not attempted because the input file name could not be determined.";
    return kUpdateNotAttempted;
  }
  const std::string& input_file_name = *input_file;

  if (oat_.Filename() == nullptr) {
    *error_msg = "Patching of oat file for dex location " + dex_location_
      + " not attempted because the oat file name could not be determined.";
    return kUpdateNotAttempted;
  }
  const std::string& oat_file_name = *oat_.Filename();

  const ImageInfo* image_info = GetImageInfo();
  Runtime* runtime = Runtime::Current();
  if (image_info == nullptr) {
    *error_msg = "Patching of oat file " + oat_file_name
      + " not attempted because no image location was found.";
    return kUpdateNotAttempted;
  }

  if (!runtime->IsDex2OatEnabled()) {
    *error_msg = "Patching of oat file " + oat_file_name
      + " not attempted because dex2oat is disabled";
    return kUpdateNotAttempted;
  }

  std::vector<std::string> argv;
  argv.push_back(runtime->GetPatchoatExecutable());
  argv.push_back("--instruction-set=" + std::string(GetInstructionSetString(isa_)));
  argv.push_back("--input-oat-file=" + input_file_name);
  argv.push_back("--output-oat-file=" + oat_file_name);
  argv.push_back("--patched-image-location=" + image_info->location);

  std::string command_line(Join(argv, ' '));
  if (!Exec(argv, error_msg)) {
    // Manually delete the file. This ensures there is no garbage left over if
    // the process unexpectedly died.
    unlink(oat_file_name.c_str());
    return kUpdateFailed;
  }

  // Mark that the oat file has changed and we should try to reload.
  oat_.Reset();
  return kUpdateSucceeded;
}

OatFileAssistant::ResultOfAttemptToUpdate
OatFileAssistant::GenerateOatFile(std::string* error_msg) {
  CHECK(error_msg != nullptr);

  Runtime* runtime = Runtime::Current();
  if (!runtime->IsDex2OatEnabled()) {
    *error_msg = "Generation of oat file for dex location " + dex_location_
      + " not attempted because dex2oat is disabled.";
    return kUpdateNotAttempted;
  }

  if (oat_.Filename() == nullptr) {
    *error_msg = "Generation of oat file for dex location " + dex_location_
      + " not attempted because the oat file name could not be determined.";
    return kUpdateNotAttempted;
  }
  const std::string& oat_file_name = *oat_.Filename();

  // dex2oat ignores missing dex files and doesn't report an error.
  // Check explicitly here so we can detect the error properly.
  // TODO: Why does dex2oat behave that way?
  if (!OS::FileExists(dex_location_.c_str())) {
    *error_msg = "Dex location " + dex_location_ + " does not exists.";
    return kUpdateNotAttempted;
  }

  std::unique_ptr<File> oat_file;
  oat_file.reset(OS::CreateEmptyFile(oat_file_name.c_str()));
  if (oat_file.get() == nullptr) {
    *error_msg = "Generation of oat file " + oat_file_name
      + " not attempted because the oat file could not be created.";
    return kUpdateNotAttempted;
  }

  if (fchmod(oat_file->Fd(), 0644) != 0) {
    *error_msg = "Generation of oat file " + oat_file_name
      + " not attempted because the oat file could not be made world readable.";
    oat_file->Erase();
    return kUpdateNotAttempted;
  }

  std::vector<std::string> args;
  args.push_back("--dex-file=" + dex_location_);
  args.push_back("--oat-fd=" + std::to_string(oat_file->Fd()));
  args.push_back("--oat-location=" + oat_file_name);

  if (!Dex2Oat(args, error_msg)) {
    // Manually delete the file. This ensures there is no garbage left over if
    // the process unexpectedly died.
    oat_file->Erase();
    unlink(oat_file_name.c_str());
    return kUpdateFailed;
  }

  if (oat_file->FlushCloseOrErase() != 0) {
    *error_msg = "Unable to close oat file " + oat_file_name;
    unlink(oat_file_name.c_str());
    return kUpdateFailed;
  }

  // Mark that the oat file has changed and we should try to reload.
  oat_.Reset();
  return kUpdateSucceeded;
}

bool OatFileAssistant::Dex2Oat(const std::vector<std::string>& args,
                               std::string* error_msg) {
  Runtime* runtime = Runtime::Current();
  std::string image_location = ImageLocation();
  if (image_location.empty()) {
    *error_msg = "No image location found for Dex2Oat.";
    return false;
  }

  std::vector<std::string> argv;
  argv.push_back(runtime->GetCompilerExecutable());
  argv.push_back("--runtime-arg");
  argv.push_back("-classpath");
  argv.push_back("--runtime-arg");
  std::string class_path = runtime->GetClassPathString();
  if (class_path == "") {
    class_path = OatFile::kSpecialSharedLibrary;
  }
  argv.push_back(class_path);
  if (runtime->IsDebuggable()) {
    argv.push_back("--debuggable");
  }
  runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

  if (!runtime->IsVerificationEnabled()) {
    argv.push_back("--compiler-filter=verify-none");
  }

  if (runtime->MustRelocateIfPossible()) {
    argv.push_back("--runtime-arg");
    argv.push_back("-Xrelocate");
  } else {
    argv.push_back("--runtime-arg");
    argv.push_back("-Xnorelocate");
  }

  if (!kIsTargetBuild) {
    argv.push_back("--host");
  }

  argv.push_back("--boot-image=" + image_location);

  std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
  argv.insert(argv.end(), compiler_options.begin(), compiler_options.end());

  argv.insert(argv.end(), args.begin(), args.end());

  std::string command_line(Join(argv, ' '));
  return Exec(argv, error_msg);
}

bool OatFileAssistant::DexLocationToOdexFilename(const std::string& location,
                                                 InstructionSet isa,
                                                 std::string* odex_filename,
                                                 std::string* error_msg) {
  CHECK(odex_filename != nullptr);
  CHECK(error_msg != nullptr);

  // The odex file name is formed by replacing the dex_location extension with
  // .odex and inserting an oat/<isa> directory. For example:
  //   location = /foo/bar/baz.jar
  //   odex_location = /foo/bar/oat/<isa>/baz.odex

  // Find the directory portion of the dex location and add the oat/<isa>
  // directory.
  size_t pos = location.rfind('/');
  if (pos == std::string::npos) {
    *error_msg = "Dex location " + location + " has no directory.";
    return false;
  }
  std::string dir = location.substr(0, pos+1);
  dir += "oat/" + std::string(GetInstructionSetString(isa));

  // Find the file portion of the dex location.
  std::string file;
  if (pos == std::string::npos) {
    file = location;
  } else {
    file = location.substr(pos+1);
  }

  // Get the base part of the file without the extension.
  pos = file.rfind('.');
  if (pos == std::string::npos) {
    *error_msg = "Dex location " + location + " has no extension.";
    return false;
  }
  std::string base = file.substr(0, pos);

  *odex_filename = dir + "/" + base + ".odex";
  return true;
}

bool OatFileAssistant::DexLocationToOatFilename(const std::string& location,
                                                InstructionSet isa,
                                                std::string* oat_filename,
                                                std::string* error_msg) {
  CHECK(oat_filename != nullptr);
  CHECK(error_msg != nullptr);

  std::string cache_dir = GetDalvikCache(GetInstructionSetString(isa));
  if (cache_dir.empty()) {
    *error_msg = "Dalvik cache directory does not exist";
    return false;
  }

  // TODO: The oat file assistant should be the definitive place for
  // determining the oat file name from the dex location, not
  // GetDalvikCacheFilename.
  return GetDalvikCacheFilename(location.c_str(), cache_dir.c_str(), oat_filename, error_msg);
}

std::string OatFileAssistant::ImageLocation() {
  Runtime* runtime = Runtime::Current();
  const std::vector<gc::space::ImageSpace*>& image_spaces =
      runtime->GetHeap()->GetBootImageSpaces();
  if (image_spaces.empty()) {
    return "";
  }
  return image_spaces[0]->GetImageLocation();
}

const uint32_t* OatFileAssistant::GetRequiredDexChecksum() {
  if (!required_dex_checksum_attempted_) {
    required_dex_checksum_attempted_ = true;
    required_dex_checksum_found_ = false;
    std::string error_msg;
    if (DexFile::GetChecksum(dex_location_.c_str(), &cached_required_dex_checksum_, &error_msg)) {
      required_dex_checksum_found_ = true;
      has_original_dex_files_ = true;
    } else {
      // This can happen if the original dex file has been stripped from the
      // apk.
      VLOG(oat) << "OatFileAssistant: " << error_msg;
      has_original_dex_files_ = false;

      // Get the checksum from the odex if we can.
      const OatFile* odex_file = odex_.GetFile();
      if (odex_file != nullptr) {
        const OatFile::OatDexFile* odex_dex_file
            = odex_file->GetOatDexFile(dex_location_.c_str(), nullptr);
        if (odex_dex_file != nullptr) {
          cached_required_dex_checksum_ = odex_dex_file->GetDexFileLocationChecksum();
          required_dex_checksum_found_ = true;
        }
      }
    }
  }
  return required_dex_checksum_found_ ? &cached_required_dex_checksum_ : nullptr;
}

const OatFileAssistant::ImageInfo* OatFileAssistant::GetImageInfo() {
  if (!image_info_load_attempted_) {
    image_info_load_attempted_ = true;

    Runtime* runtime = Runtime::Current();
    std::vector<gc::space::ImageSpace*> image_spaces = runtime->GetHeap()->GetBootImageSpaces();
    if (!image_spaces.empty()) {
      cached_image_info_.location = image_spaces[0]->GetImageLocation();

      if (isa_ == kRuntimeISA) {
        const ImageHeader& image_header = image_spaces[0]->GetImageHeader();
        cached_image_info_.oat_checksum = image_header.GetOatChecksum();
        cached_image_info_.oat_data_begin = reinterpret_cast<uintptr_t>(
            image_header.GetOatDataBegin());
        cached_image_info_.patch_delta = image_header.GetPatchDelta();
      } else {
        std::string error_msg;
        std::unique_ptr<ImageHeader> image_header(
            gc::space::ImageSpace::ReadImageHeader(cached_image_info_.location.c_str(),
                                                   isa_,
                                                   &error_msg));
        CHECK(image_header != nullptr) << error_msg;
        cached_image_info_.oat_checksum = image_header->GetOatChecksum();
        cached_image_info_.oat_data_begin = reinterpret_cast<uintptr_t>(
            image_header->GetOatDataBegin());
        cached_image_info_.patch_delta = image_header->GetPatchDelta();
      }
    }
    image_info_load_succeeded_ = (!image_spaces.empty());

    combined_image_checksum_ = CalculateCombinedImageChecksum(isa_);
  }
  return image_info_load_succeeded_ ? &cached_image_info_ : nullptr;
}

// TODO: Use something better than xor.
uint32_t OatFileAssistant::CalculateCombinedImageChecksum(InstructionSet isa) {
  uint32_t checksum = 0;
  std::vector<gc::space::ImageSpace*> image_spaces =
      Runtime::Current()->GetHeap()->GetBootImageSpaces();
  if (isa == kRuntimeISA) {
    for (gc::space::ImageSpace* image_space : image_spaces) {
      checksum ^= image_space->GetImageHeader().GetOatChecksum();
    }
  } else {
    for (gc::space::ImageSpace* image_space : image_spaces) {
      std::string location = image_space->GetImageLocation();
      std::string error_msg;
      std::unique_ptr<ImageHeader> image_header(
          gc::space::ImageSpace::ReadImageHeader(location.c_str(), isa, &error_msg));
      CHECK(image_header != nullptr) << error_msg;
      checksum ^= image_header->GetOatChecksum();
    }
  }
  return checksum;
}

uint32_t OatFileAssistant::GetCombinedImageChecksum() {
  if (!image_info_load_attempted_) {
    GetImageInfo();
  }
  return combined_image_checksum_;
}

std::unique_ptr<gc::space::ImageSpace> OatFileAssistant::OpenImageSpace(const OatFile* oat_file) {
  DCHECK(oat_file != nullptr);
  std::string art_file = ArtFileName(oat_file);
  if (art_file.empty()) {
    return nullptr;
  }
  std::string error_msg;
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<gc::space::ImageSpace> ret =
      gc::space::ImageSpace::CreateFromAppImage(art_file.c_str(), oat_file, &error_msg);
  if (ret == nullptr && (VLOG_IS_ON(image) || OS::FileExists(art_file.c_str()))) {
    LOG(INFO) << "Failed to open app image " << art_file.c_str() << " " << error_msg;
  }
  return ret;
}

OatFileAssistant::OatFileInfo::OatFileInfo(OatFileAssistant* oat_file_assistant)
  : oat_file_assistant_(oat_file_assistant)
{}

const std::string* OatFileAssistant::OatFileInfo::Filename() {
  return filename_provided_ ? &filename_ : nullptr;
}

bool OatFileAssistant::OatFileInfo::Exists() {
  return GetFile() != nullptr;
}

OatFileAssistant::OatStatus OatFileAssistant::OatFileInfo::Status() {
  if (!status_attempted_) {
    status_attempted_ = true;
    const OatFile* file = GetFile();
    if (file == nullptr) {
      status_ = kOatOutOfDate;
    } else {
      status_ = oat_file_assistant_->GivenOatFileStatus(*file);
      VLOG(oat) << file->GetLocation() << " is " << status_
          << " with filter " << file->GetCompilerFilter();
    }
  }
  return status_;
}

bool OatFileAssistant::OatFileInfo::IsOutOfDate() {
  return Status() == kOatOutOfDate;
}

bool OatFileAssistant::OatFileInfo::NeedsRelocation() {
  return Status() == kOatNeedsRelocation;
}

bool OatFileAssistant::OatFileInfo::IsUpToDate() {
  return Status() == kOatUpToDate;
}

CompilerFilter::Filter OatFileAssistant::OatFileInfo::CompilerFilter() {
  const OatFile* file = GetFile();
  CHECK(file != nullptr);
  return file->GetCompilerFilter();
}

const OatFile* OatFileAssistant::OatFileInfo::GetFile() {
  CHECK(!file_released_) << "GetFile called after oat file released.";
  if (!load_attempted_) {
    load_attempted_ = true;
    if (filename_provided_) {
      std::string error_msg;
      file_.reset(OatFile::Open(filename_.c_str(),
                                filename_.c_str(),
                                nullptr,
                                nullptr,
                                oat_file_assistant_->load_executable_,
                                /*low_4gb*/false,
                                oat_file_assistant_->dex_location_.c_str(),
                                &error_msg));
      if (file_.get() == nullptr) {
        VLOG(oat) << "OatFileAssistant test for existing oat file "
          << filename_ << ": " << error_msg;
      }
    }
  }
  return file_.get();
}

bool OatFileAssistant::OatFileInfo::CompilerFilterIsOkay(
    CompilerFilter::Filter target, bool profile_changed) {
  const OatFile* file = GetFile();
  if (file == nullptr) {
    return false;
  }

  CompilerFilter::Filter current = file->GetCompilerFilter();
  if (profile_changed && CompilerFilter::DependsOnProfile(current)) {
    VLOG(oat) << "Compiler filter not okay because Profile changed";
    return false;
  }
  return CompilerFilter::IsAsGoodAs(current, target);
}

bool OatFileAssistant::OatFileInfo::IsExecutable() {
  const OatFile* file = GetFile();
  return (file != nullptr && file->IsExecutable());
}

bool OatFileAssistant::OatFileInfo::HasPatchInfo() {
  const OatFile* file = GetFile();
  return (file != nullptr && file->HasPatchInfo());
}

void OatFileAssistant::OatFileInfo::Reset() {
  load_attempted_ = false;
  file_.reset();
  status_attempted_ = false;
}

void OatFileAssistant::OatFileInfo::Reset(const std::string& filename) {
  filename_provided_ = true;
  filename_ = filename;
  Reset();
}

std::unique_ptr<OatFile> OatFileAssistant::OatFileInfo::ReleaseFile() {
  file_released_ = true;
  return std::move(file_);
}

}  // namespace art

