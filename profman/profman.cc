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

#include "errno.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/dumpable.h"
#include "base/scoped_flock.h"
#include "base/stringpiece.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "bytecode_utils.h"
#include "dex_file.h"
#include "jit/profile_compilation_info.h"
#include "runtime.h"
#include "utils.h"
#include "zip_archive.h"
#include "profile_assistant.h"

namespace art {

static int original_argc;
static char** original_argv;

static std::string CommandLine() {
  std::vector<std::string> command;
  for (int i = 0; i < original_argc; ++i) {
    command.push_back(original_argv[i]);
  }
  return android::base::Join(command, ' ');
}

static constexpr int kInvalidFd = -1;

static bool FdIsValid(int fd) {
  return fd != kInvalidFd;
}

static void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  LOG(ERROR) << error;
}

static void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN static void Usage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);

  UsageError("Command: %s", CommandLine().c_str());
  UsageError("Usage: profman [options]...");
  UsageError("");
  UsageError("  --dump-only: dumps the content of the specified profile files");
  UsageError("      to standard output (default) in a human readable form.");
  UsageError("");
  UsageError("  --dump-output-to-fd=<number>: redirects --dump-only output to a file descriptor.");
  UsageError("");
  UsageError("  --dump-classes-and-methods: dumps a sorted list of classes and methods that are");
  UsageError("      in the specified profile file to standard output (default) in a human");
  UsageError("      readable form. The output is valid input for --create-profile-from");
  UsageError("");
  UsageError("  --profile-file=<filename>: specify profiler output file to use for compilation.");
  UsageError("      Can be specified multiple time, in which case the data from the different");
  UsageError("      profiles will be aggregated.");
  UsageError("");
  UsageError("  --profile-file-fd=<number>: same as --profile-file but accepts a file descriptor.");
  UsageError("      Cannot be used together with --profile-file.");
  UsageError("");
  UsageError("  --reference-profile-file=<filename>: specify a reference profile.");
  UsageError("      The data in this file will be compared with the data obtained by merging");
  UsageError("      all the files specified with --profile-file or --profile-file-fd.");
  UsageError("      If the exit code is EXIT_COMPILE then all --profile-file will be merged into");
  UsageError("      --reference-profile-file. ");
  UsageError("");
  UsageError("  --reference-profile-file-fd=<number>: same as --reference-profile-file but");
  UsageError("      accepts a file descriptor. Cannot be used together with");
  UsageError("      --reference-profile-file.");
  UsageError("");
  UsageError("  --generate-test-profile=<filename>: generates a random profile file for testing.");
  UsageError("  --generate-test-profile-num-dex=<number>: number of dex files that should be");
  UsageError("      included in the generated profile. Defaults to 20.");
  UsageError("  --generate-test-profile-method-ratio=<number>: the percentage from the maximum");
  UsageError("      number of methods that should be generated. Defaults to 5.");
  UsageError("  --generate-test-profile-class-ratio=<number>: the percentage from the maximum");
  UsageError("      number of classes that should be generated. Defaults to 5.");
  UsageError("  --generate-test-profile-seed=<number>: seed for random number generator used when");
  UsageError("      generating random test profiles. Defaults to using NanoTime.");
  UsageError("");
  UsageError("  --create-profile-from=<filename>: creates a profile from a list of classes and");
  UsageError("      methods.");
  UsageError("");
  UsageError("  --dex-location=<string>: location string to use with corresponding");
  UsageError("      apk-fd to find dex files");
  UsageError("");
  UsageError("  --apk-fd=<number>: file descriptor containing an open APK to");
  UsageError("      search for dex files");
  UsageError("  --apk-=<filename>: an APK to search for dex files");
  UsageError("");

  exit(EXIT_FAILURE);
}

// Note: make sure you update the Usage if you change these values.
static constexpr uint16_t kDefaultTestProfileNumDex = 20;
static constexpr uint16_t kDefaultTestProfileMethodRatio = 5;
static constexpr uint16_t kDefaultTestProfileClassRatio = 5;

// Separators used when parsing human friendly representation of profiles.
static const std::string kMethodSep = "->";
static const std::string kMissingTypesMarker = "missing_types";
static const std::string kInvalidClassDescriptor = "invalid_class";
static const std::string kInvalidMethod = "invalid_method";
static const std::string kClassAllMethods = "*";
static constexpr char kProfileParsingInlineChacheSep = '+';
static constexpr char kProfileParsingTypeSep = ',';
static constexpr char kProfileParsingFirstCharInSignature = '(';

// TODO(calin): This class has grown too much from its initial design. Split the functionality
// into smaller, more contained pieces.
class ProfMan FINAL {
 public:
  ProfMan() :
      reference_profile_file_fd_(kInvalidFd),
      dump_only_(false),
      dump_classes_and_methods_(false),
      dump_output_to_fd_(kInvalidFd),
      test_profile_num_dex_(kDefaultTestProfileNumDex),
      test_profile_method_ratio_(kDefaultTestProfileMethodRatio),
      test_profile_class_ratio_(kDefaultTestProfileClassRatio),
      test_profile_seed_(NanoTime()),
      start_ns_(NanoTime()) {}

  ~ProfMan() {
    LogCompletionTime();
  }

  void ParseArgs(int argc, char **argv) {
    original_argc = argc;
    original_argv = argv;

    InitLogging(argv, Runtime::Aborter);

    // Skip over the command name.
    argv++;
    argc--;

    if (argc == 0) {
      Usage("No arguments specified");
    }

    for (int i = 0; i < argc; ++i) {
      const StringPiece option(argv[i]);
      const bool log_options = false;
      if (log_options) {
        LOG(INFO) << "profman: option[" << i << "]=" << argv[i];
      }
      if (option == "--dump-only") {
        dump_only_ = true;
      } else if (option == "--dump-classes-and-methods") {
        dump_classes_and_methods_ = true;
      } else if (option.starts_with("--create-profile-from=")) {
        create_profile_from_file_ = option.substr(strlen("--create-profile-from=")).ToString();
      } else if (option.starts_with("--dump-output-to-fd=")) {
        ParseUintOption(option, "--dump-output-to-fd", &dump_output_to_fd_, Usage);
      } else if (option.starts_with("--profile-file=")) {
        profile_files_.push_back(option.substr(strlen("--profile-file=")).ToString());
      } else if (option.starts_with("--profile-file-fd=")) {
        ParseFdForCollection(option, "--profile-file-fd", &profile_files_fd_);
      } else if (option.starts_with("--reference-profile-file=")) {
        reference_profile_file_ = option.substr(strlen("--reference-profile-file=")).ToString();
      } else if (option.starts_with("--reference-profile-file-fd=")) {
        ParseUintOption(option, "--reference-profile-file-fd", &reference_profile_file_fd_, Usage);
      } else if (option.starts_with("--dex-location=")) {
        dex_locations_.push_back(option.substr(strlen("--dex-location=")).ToString());
      } else if (option.starts_with("--apk-fd=")) {
        ParseFdForCollection(option, "--apk-fd", &apks_fd_);
      } else if (option.starts_with("--apk=")) {
        apk_files_.push_back(option.substr(strlen("--apk=")).ToString());
      } else if (option.starts_with("--generate-test-profile=")) {
        test_profile_ = option.substr(strlen("--generate-test-profile=")).ToString();
      } else if (option.starts_with("--generate-test-profile-num-dex=")) {
        ParseUintOption(option,
                        "--generate-test-profile-num-dex",
                        &test_profile_num_dex_,
                        Usage);
      } else if (option.starts_with("--generate-test-profile-method-ratio")) {
        ParseUintOption(option,
                        "--generate-test-profile-method-ratio",
                        &test_profile_method_ratio_,
                        Usage);
      } else if (option.starts_with("--generate-test-profile-class-ratio")) {
        ParseUintOption(option,
                        "--generate-test-profile-class-ratio",
                        &test_profile_class_ratio_,
                        Usage);
      } else if (option.starts_with("--generate-test-profile-seed=")) {
        ParseUintOption(option, "--generate-test-profile-seed", &test_profile_seed_, Usage);
      } else {
        Usage("Unknown argument '%s'", option.data());
      }
    }

    // Validate global consistency between file/fd options.
    if (!profile_files_.empty() && !profile_files_fd_.empty()) {
      Usage("Profile files should not be specified with both --profile-file-fd and --profile-file");
    }
    if (!reference_profile_file_.empty() && FdIsValid(reference_profile_file_fd_)) {
      Usage("Reference profile should not be specified with both "
            "--reference-profile-file-fd and --reference-profile-file");
    }
    if (!apk_files_.empty() && !apks_fd_.empty()) {
      Usage("APK files should not be specified with both --apk-fd and --apk");
    }
  }

  ProfileAssistant::ProcessingResult ProcessProfiles() {
    // Validate that at least one profile file was passed, as well as a reference profile.
    if (profile_files_.empty() && profile_files_fd_.empty()) {
      Usage("No profile files specified.");
    }
    if (reference_profile_file_.empty() && !FdIsValid(reference_profile_file_fd_)) {
      Usage("No reference profile file specified.");
    }
    if ((!profile_files_.empty() && FdIsValid(reference_profile_file_fd_)) ||
        (!profile_files_fd_.empty() && !FdIsValid(reference_profile_file_fd_))) {
      Usage("Options --profile-file-fd and --reference-profile-file-fd "
            "should only be used together");
    }
    ProfileAssistant::ProcessingResult result;
    if (profile_files_.empty()) {
      // The file doesn't need to be flushed here (ProcessProfiles will do it)
      // so don't check the usage.
      File file(reference_profile_file_fd_, false);
      result = ProfileAssistant::ProcessProfiles(profile_files_fd_, reference_profile_file_fd_);
      CloseAllFds(profile_files_fd_, "profile_files_fd_");
    } else {
      result = ProfileAssistant::ProcessProfiles(profile_files_, reference_profile_file_);
    }
    return result;
  }

  void OpenApkFilesFromLocations(std::vector<std::unique_ptr<const DexFile>>* dex_files) {
    bool use_apk_fd_list = !apks_fd_.empty();
    if (use_apk_fd_list) {
      // Get the APKs from the collection of FDs.
      CHECK_EQ(dex_locations_.size(), apks_fd_.size());
    } else if (!apk_files_.empty()) {
      // Get the APKs from the collection of filenames.
      CHECK_EQ(dex_locations_.size(), apk_files_.size());
    } else {
      // No APKs were specified.
      CHECK(dex_locations_.empty());
      return;
    }
    static constexpr bool kVerifyChecksum = true;
    for (size_t i = 0; i < dex_locations_.size(); ++i) {
      std::string error_msg;
      std::vector<std::unique_ptr<const DexFile>> dex_files_for_location;
      if (use_apk_fd_list) {
        if (DexFile::OpenZip(apks_fd_[i],
                             dex_locations_[i],
                             kVerifyChecksum,
                             &error_msg,
                             &dex_files_for_location)) {
        } else {
          LOG(WARNING) << "OpenZip failed for '" << dex_locations_[i] << "' " << error_msg;
          continue;
        }
      } else {
        if (DexFile::Open(apk_files_[i].c_str(),
                          dex_locations_[i],
                          kVerifyChecksum,
                          &error_msg,
                          &dex_files_for_location)) {
        } else {
          LOG(WARNING) << "Open failed for '" << dex_locations_[i] << "' " << error_msg;
          continue;
        }
      }
      for (std::unique_ptr<const DexFile>& dex_file : dex_files_for_location) {
        dex_files->emplace_back(std::move(dex_file));
      }
    }
  }

  int DumpOneProfile(const std::string& banner,
                     const std::string& filename,
                     int fd,
                     const std::vector<std::unique_ptr<const DexFile>>* dex_files,
                     std::string* dump) {
    if (!filename.empty()) {
      fd = open(filename.c_str(), O_RDWR);
      if (fd < 0) {
        LOG(ERROR) << "Cannot open " << filename << strerror(errno);
        return -1;
      }
    }
    ProfileCompilationInfo info;
    if (!info.Load(fd)) {
      LOG(ERROR) << "Cannot load profile info from fd=" << fd << "\n";
      return -1;
    }
    std::string this_dump = banner + "\n" + info.DumpInfo(dex_files) + "\n";
    *dump += this_dump;
    if (close(fd) < 0) {
      PLOG(WARNING) << "Failed to close descriptor";
    }
    return 0;
  }

  int DumpProfileInfo() {
    // Validate that at least one profile file or reference was specified.
    if (profile_files_.empty() && profile_files_fd_.empty() &&
        reference_profile_file_.empty() && !FdIsValid(reference_profile_file_fd_)) {
      Usage("No profile files or reference profile specified.");
    }
    static const char* kEmptyString = "";
    static const char* kOrdinaryProfile = "=== profile ===";
    static const char* kReferenceProfile = "=== reference profile ===";

    // Open apk/zip files and and read dex files.
    MemMap::Init();  // for ZipArchive::OpenFromFd
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    OpenApkFilesFromLocations(&dex_files);
    std::string dump;
    // Dump individual profile files.
    if (!profile_files_fd_.empty()) {
      for (int profile_file_fd : profile_files_fd_) {
        int ret = DumpOneProfile(kOrdinaryProfile,
                                 kEmptyString,
                                 profile_file_fd,
                                 &dex_files,
                                 &dump);
        if (ret != 0) {
          return ret;
        }
      }
    }
    if (!profile_files_.empty()) {
      for (const std::string& profile_file : profile_files_) {
        int ret = DumpOneProfile(kOrdinaryProfile, profile_file, kInvalidFd, &dex_files, &dump);
        if (ret != 0) {
          return ret;
        }
      }
    }
    // Dump reference profile file.
    if (FdIsValid(reference_profile_file_fd_)) {
      int ret = DumpOneProfile(kReferenceProfile,
                               kEmptyString,
                               reference_profile_file_fd_,
                               &dex_files,
                               &dump);
      if (ret != 0) {
        return ret;
      }
    }
    if (!reference_profile_file_.empty()) {
      int ret = DumpOneProfile(kReferenceProfile,
                               reference_profile_file_,
                               kInvalidFd,
                               &dex_files,
                               &dump);
      if (ret != 0) {
        return ret;
      }
    }
    if (!FdIsValid(dump_output_to_fd_)) {
      std::cout << dump;
    } else {
      unix_file::FdFile out_fd(dump_output_to_fd_, false /*check_usage*/);
      if (!out_fd.WriteFully(dump.c_str(), dump.length())) {
        return -1;
      }
    }
    return 0;
  }

  bool ShouldOnlyDumpProfile() {
    return dump_only_;
  }

  bool GetClassNamesAndMethods(int fd,
                               std::vector<std::unique_ptr<const DexFile>>* dex_files,
                               std::set<std::string>* out_lines) {
    ProfileCompilationInfo profile_info;
    if (!profile_info.Load(fd)) {
      LOG(ERROR) << "Cannot load profile info";
      return false;
    }
    for (const std::unique_ptr<const DexFile>& dex_file : *dex_files) {
      std::set<dex::TypeIndex> class_types;
      std::set<uint16_t> methods;
      if (profile_info.GetClassesAndMethods(*dex_file.get(), &class_types, &methods)) {
        for (const dex::TypeIndex& type_index : class_types) {
          const DexFile::TypeId& type_id = dex_file->GetTypeId(type_index);
          out_lines->insert(std::string(dex_file->GetTypeDescriptor(type_id)));
        }
        for (uint16_t dex_method_idx : methods) {
          const DexFile::MethodId& id = dex_file->GetMethodId(dex_method_idx);
          std::string signature_string(dex_file->GetMethodSignature(id).ToString());
          std::string type_string(dex_file->GetTypeDescriptor(dex_file->GetTypeId(id.class_idx_)));
          std::string method_name(dex_file->GetMethodName(id));
          out_lines->insert(type_string + kMethodSep + method_name + signature_string);
        }
      }
    }
    return true;
  }

  bool GetClassNamesAndMethods(const std::string& profile_file,
                               std::vector<std::unique_ptr<const DexFile>>* dex_files,
                               std::set<std::string>* out_lines) {
    int fd = open(profile_file.c_str(), O_RDONLY);
    if (!FdIsValid(fd)) {
      LOG(ERROR) << "Cannot open " << profile_file << strerror(errno);
      return false;
    }
    if (!GetClassNamesAndMethods(fd, dex_files, out_lines)) {
      return false;
    }
    if (close(fd) < 0) {
      PLOG(WARNING) << "Failed to close descriptor";
    }
    return true;
  }

  int DumpClasses() {
    // Validate that at least one profile file or reference was specified.
    if (profile_files_.empty() && profile_files_fd_.empty() &&
        reference_profile_file_.empty() && !FdIsValid(reference_profile_file_fd_)) {
      Usage("No profile files or reference profile specified.");
    }
    // Open apk/zip files and and read dex files.
    MemMap::Init();  // for ZipArchive::OpenFromFd
    // Open the dex files to get the names for classes.
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    OpenApkFilesFromLocations(&dex_files);
    // Build a vector of class names from individual profile files.
    std::set<std::string> class_names;
    if (!profile_files_fd_.empty()) {
      for (int profile_file_fd : profile_files_fd_) {
        if (!GetClassNamesAndMethods(profile_file_fd, &dex_files, &class_names)) {
          return -1;
        }
      }
    }
    if (!profile_files_.empty()) {
      for (const std::string& profile_file : profile_files_) {
        if (!GetClassNamesAndMethods(profile_file, &dex_files, &class_names)) {
          return -1;
        }
      }
    }
    // Concatenate class names from reference profile file.
    if (FdIsValid(reference_profile_file_fd_)) {
      if (!GetClassNamesAndMethods(reference_profile_file_fd_, &dex_files, &class_names)) {
        return -1;
      }
    }
    if (!reference_profile_file_.empty()) {
      if (!GetClassNamesAndMethods(reference_profile_file_, &dex_files, &class_names)) {
        return -1;
      }
    }
    // Dump the class names.
    std::string dump;
    for (const std::string& class_name : class_names) {
      dump += class_name + std::string("\n");
    }
    if (!FdIsValid(dump_output_to_fd_)) {
      std::cout << dump;
    } else {
      unix_file::FdFile out_fd(dump_output_to_fd_, false /*check_usage*/);
      if (!out_fd.WriteFully(dump.c_str(), dump.length())) {
        return -1;
      }
    }
    return 0;
  }

  bool ShouldOnlyDumpClassesAndMethods() {
    return dump_classes_and_methods_;
  }

  // Read lines from the given file, dropping comments and empty lines. Post-process each line with
  // the given function.
  template <typename T>
  static T* ReadCommentedInputFromFile(
      const char* input_filename, std::function<std::string(const char*)>* process) {
    std::unique_ptr<std::ifstream> input_file(new std::ifstream(input_filename, std::ifstream::in));
    if (input_file.get() == nullptr) {
      LOG(ERROR) << "Failed to open input file " << input_filename;
      return nullptr;
    }
    std::unique_ptr<T> result(
        ReadCommentedInputStream<T>(*input_file, process));
    input_file->close();
    return result.release();
  }

  // Read lines from the given stream, dropping comments and empty lines. Post-process each line
  // with the given function.
  template <typename T>
  static T* ReadCommentedInputStream(
      std::istream& in_stream,
      std::function<std::string(const char*)>* process) {
    std::unique_ptr<T> output(new T());
    while (in_stream.good()) {
      std::string dot;
      std::getline(in_stream, dot);
      if (android::base::StartsWith(dot, "#") || dot.empty()) {
        continue;
      }
      if (process != nullptr) {
        std::string descriptor((*process)(dot.c_str()));
        output->insert(output->end(), descriptor);
      } else {
        output->insert(output->end(), dot);
      }
    }
    return output.release();
  }

  // Find class klass_descriptor in the given dex_files and store its reference
  // in the out parameter class_ref.
  // Return true if the definition of the class was found in any of the dex_files.
  bool FindClass(const std::vector<std::unique_ptr<const DexFile>>& dex_files,
                 const std::string& klass_descriptor,
                 /*out*/ProfileMethodInfo::ProfileClassReference* class_ref) {
    constexpr uint16_t kInvalidTypeIndex = std::numeric_limits<uint16_t>::max() - 1;
    for (const std::unique_ptr<const DexFile>& dex_file_ptr : dex_files) {
      const DexFile* dex_file = dex_file_ptr.get();
      if (klass_descriptor == kInvalidClassDescriptor) {
        if (kInvalidTypeIndex >= dex_file->NumTypeIds()) {
          // The dex file does not contain all possible type ids which leaves us room
          // to add an "invalid" type id.
          class_ref->dex_file = dex_file;
          class_ref->type_index = dex::TypeIndex(kInvalidTypeIndex);
          return true;
        } else {
          // The dex file contains all possible type ids. We don't have any free type id
          // that we can use as invalid.
          continue;
        }
      }

      const DexFile::TypeId* type_id = dex_file->FindTypeId(klass_descriptor.c_str());
      if (type_id == nullptr) {
        continue;
      }
      dex::TypeIndex type_index = dex_file->GetIndexForTypeId(*type_id);
      if (dex_file->FindClassDef(type_index) == nullptr) {
        // Class is only referenced in the current dex file but not defined in it.
        continue;
      }
      class_ref->dex_file = dex_file;
      class_ref->type_index = type_index;
      return true;
    }
    return false;
  }

  // Find the method specified by method_spec in the class class_ref.
  uint32_t FindMethodIndex(const ProfileMethodInfo::ProfileClassReference& class_ref,
                           const std::string& method_spec) {
    const DexFile* dex_file = class_ref.dex_file;
    if (method_spec == kInvalidMethod) {
      constexpr uint16_t kInvalidMethodIndex = std::numeric_limits<uint16_t>::max() - 1;
      return kInvalidMethodIndex >= dex_file->NumMethodIds()
             ? kInvalidMethodIndex
             : DexFile::kDexNoIndex;
    }

    std::vector<std::string> name_and_signature;
    Split(method_spec, kProfileParsingFirstCharInSignature, &name_and_signature);
    if (name_and_signature.size() != 2) {
      LOG(ERROR) << "Invalid method name and signature " << method_spec;
      return DexFile::kDexNoIndex;
    }

    const std::string& name = name_and_signature[0];
    const std::string& signature = kProfileParsingFirstCharInSignature + name_and_signature[1];

    const DexFile::StringId* name_id = dex_file->FindStringId(name.c_str());
    if (name_id == nullptr) {
      LOG(ERROR) << "Could not find name: "  << name;
      return DexFile::kDexNoIndex;
    }
    dex::TypeIndex return_type_idx;
    std::vector<dex::TypeIndex> param_type_idxs;
    if (!dex_file->CreateTypeList(signature, &return_type_idx, &param_type_idxs)) {
      LOG(ERROR) << "Could not create type list" << signature;
      return DexFile::kDexNoIndex;
    }
    const DexFile::ProtoId* proto_id = dex_file->FindProtoId(return_type_idx, param_type_idxs);
    if (proto_id == nullptr) {
      LOG(ERROR) << "Could not find proto_id: " << name;
      return DexFile::kDexNoIndex;
    }
    const DexFile::MethodId* method_id = dex_file->FindMethodId(
        dex_file->GetTypeId(class_ref.type_index), *name_id, *proto_id);
    if (method_id == nullptr) {
      LOG(ERROR) << "Could not find method_id: " << name;
      return DexFile::kDexNoIndex;
    }

    return dex_file->GetIndexForMethodId(*method_id);
  }

  // Given a method, return true if the method has a single INVOKE_VIRTUAL in its byte code.
  // Upon success it returns true and stores the method index and the invoke dex pc
  // in the output parameters.
  // The format of the method spec is "inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;".
  //
  // TODO(calin): support INVOKE_INTERFACE and the range variants.
  bool HasSingleInvoke(const ProfileMethodInfo::ProfileClassReference& class_ref,
                       uint16_t method_index,
                       /*out*/uint32_t* dex_pc) {
    const DexFile* dex_file = class_ref.dex_file;
    uint32_t offset = dex_file->FindCodeItemOffset(
        *dex_file->FindClassDef(class_ref.type_index),
        method_index);
    const DexFile::CodeItem* code_item = dex_file->GetCodeItem(offset);

    bool found_invoke = false;
    for (CodeItemIterator it(*code_item); !it.Done(); it.Advance()) {
      if (it.CurrentInstruction().Opcode() == Instruction::INVOKE_VIRTUAL) {
        if (found_invoke) {
          LOG(ERROR) << "Multiple invoke INVOKE_VIRTUAL found: "
                     << dex_file->PrettyMethod(method_index);
          return false;
        }
        found_invoke = true;
        *dex_pc = it.CurrentDexPc();
      }
    }
    if (!found_invoke) {
      LOG(ERROR) << "Could not find any INVOKE_VIRTUAL: " << dex_file->PrettyMethod(method_index);
    }
    return found_invoke;
  }

  // Process a line defining a class or a method and its inline caches.
  // Upon success return true and add the class or the method info to profile.
  // The possible line formats are:
  // "LJustTheCass;".
  // "LTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;".
  // "LTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,invalid_class".
  // "LTestInline;->inlineMissingTypes(LSuper;)I+missing_types".
  // "LTestInline;->inlineNoInlineCaches(LSuper;)I".
  // "LTestInline;->*".
  // "invalid_class".
  // "LTestInline;->invalid_method".
  // The method and classes are searched only in the given dex files.
  bool ProcessLine(const std::vector<std::unique_ptr<const DexFile>>& dex_files,
                   const std::string& line,
                   /*out*/ProfileCompilationInfo* profile) {
    std::string klass;
    std::string method_str;
    size_t method_sep_index = line.find(kMethodSep);
    if (method_sep_index == std::string::npos) {
      klass = line;
    } else {
      klass = line.substr(0, method_sep_index);
      method_str = line.substr(method_sep_index + kMethodSep.size());
    }

    ProfileMethodInfo::ProfileClassReference class_ref;
    if (!FindClass(dex_files, klass, &class_ref)) {
      LOG(WARNING) << "Could not find class: " << klass;
      return false;
    }

    if (method_str.empty() || method_str == kClassAllMethods) {
      // Start by adding the class.
      std::set<DexCacheResolvedClasses> resolved_class_set;
      const DexFile* dex_file = class_ref.dex_file;
      const auto& dex_resolved_classes = resolved_class_set.emplace(
            dex_file->GetLocation(),
            dex_file->GetBaseLocation(),
            dex_file->GetLocationChecksum());
      dex_resolved_classes.first->AddClass(class_ref.type_index);
      std::vector<ProfileMethodInfo> methods;
      if (method_str == kClassAllMethods) {
        // Add all of the methods.
        const DexFile::ClassDef* class_def = dex_file->FindClassDef(class_ref.type_index);
        const uint8_t* class_data = dex_file->GetClassData(*class_def);
        if (class_data != nullptr) {
          ClassDataItemIterator it(*dex_file, class_data);
          while (it.HasNextStaticField() || it.HasNextInstanceField()) {
            it.Next();
          }
          while (it.HasNextDirectMethod() || it.HasNextVirtualMethod()) {
            if (it.GetMethodCodeItemOffset() != 0) {
              // Add all of the methods that have code to the profile.
              const uint32_t method_idx = it.GetMemberIndex();
              methods.push_back(ProfileMethodInfo(dex_file, method_idx));
            }
            it.Next();
          }
        }
      }
      profile->AddMethodsAndClasses(methods, resolved_class_set);
      return true;
    }

    // Process the method.
    std::string method_spec;
    std::vector<std::string> inline_cache_elems;

    std::vector<std::string> method_elems;
    bool is_missing_types = false;
    Split(method_str, kProfileParsingInlineChacheSep, &method_elems);
    if (method_elems.size() == 2) {
      method_spec = method_elems[0];
      is_missing_types = method_elems[1] == kMissingTypesMarker;
      if (!is_missing_types) {
        Split(method_elems[1], kProfileParsingTypeSep, &inline_cache_elems);
      }
    } else if (method_elems.size() == 1) {
      method_spec = method_elems[0];
    } else {
      LOG(ERROR) << "Invalid method line: " << line;
      return false;
    }

    const uint32_t method_index = FindMethodIndex(class_ref, method_spec);
    if (method_index == DexFile::kDexNoIndex) {
      return false;
    }

    std::vector<ProfileMethodInfo> pmi;
    std::vector<ProfileMethodInfo::ProfileInlineCache> inline_caches;
    if (is_missing_types || !inline_cache_elems.empty()) {
      uint32_t dex_pc;
      if (!HasSingleInvoke(class_ref, method_index, &dex_pc)) {
        return false;
      }
      std::vector<ProfileMethodInfo::ProfileClassReference> classes(inline_cache_elems.size());
      size_t class_it = 0;
      for (const std::string& ic_class : inline_cache_elems) {
        if (!FindClass(dex_files, ic_class, &(classes[class_it++]))) {
          LOG(ERROR) << "Could not find class: " << ic_class;
          return false;
        }
      }
      inline_caches.emplace_back(dex_pc, is_missing_types, classes);
    }
    pmi.emplace_back(class_ref.dex_file, method_index, inline_caches);
    profile->AddMethodsAndClasses(pmi, std::set<DexCacheResolvedClasses>());
    return true;
  }

  // Creates a profile from a human friendly textual representation.
  // The expected input format is:
  //   # Classes
  //   Ljava/lang/Comparable;
  //   Ljava/lang/Math;
  //   # Methods with inline caches
  //   LTestInline;->inlinePolymorphic(LSuper;)I+LSubA;,LSubB;,LSubC;
  //   LTestInline;->noInlineCache(LSuper;)I
  int CreateProfile() {
    // Validate parameters for this command.
    if (apk_files_.empty() && apks_fd_.empty()) {
      Usage("APK files must be specified");
    }
    if (dex_locations_.empty()) {
      Usage("DEX locations must be specified");
    }
    if (reference_profile_file_.empty() && !FdIsValid(reference_profile_file_fd_)) {
      Usage("Reference profile must be specified with --reference-profile-file or "
            "--reference-profile-file-fd");
    }
    if (!profile_files_.empty() || !profile_files_fd_.empty()) {
      Usage("Profile must be specified with --reference-profile-file or "
            "--reference-profile-file-fd");
    }
    // for ZipArchive::OpenFromFd
    MemMap::Init();
    // Open the profile output file if needed.
    int fd = reference_profile_file_fd_;
    if (!FdIsValid(fd)) {
      CHECK(!reference_profile_file_.empty());
      fd = open(reference_profile_file_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
      if (fd < 0) {
        LOG(ERROR) << "Cannot open " << reference_profile_file_ << strerror(errno);
        return -1;
      }
    }
    // Read the user-specified list of classes and methods.
    std::unique_ptr<std::unordered_set<std::string>>
        user_lines(ReadCommentedInputFromFile<std::unordered_set<std::string>>(
            create_profile_from_file_.c_str(), nullptr));  // No post-processing.

    // Open the dex files to look up classes and methods.
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    OpenApkFilesFromLocations(&dex_files);

    // Process the lines one by one and add the successful ones to the profile.
    ProfileCompilationInfo info;

    for (const auto& line : *user_lines) {
      ProcessLine(dex_files, line, &info);
    }

    // Write the profile file.
    CHECK(info.Save(fd));
    if (close(fd) < 0) {
      PLOG(WARNING) << "Failed to close descriptor";
    }
    return 0;
  }

  bool ShouldCreateProfile() {
    return !create_profile_from_file_.empty();
  }

  int GenerateTestProfile() {
    // Validate parameters for this command.
    if (test_profile_method_ratio_ > 100) {
      Usage("Invalid ratio for --generate-test-profile-method-ratio");
    }
    if (test_profile_class_ratio_ > 100) {
      Usage("Invalid ratio for --generate-test-profile-class-ratio");
    }
    // If given APK files or DEX locations, check that they're ok.
    if (!apk_files_.empty() || !apks_fd_.empty() || !dex_locations_.empty()) {
      if (apk_files_.empty() && apks_fd_.empty()) {
        Usage("APK files must be specified when passing DEX locations to --generate-test-profile");
      }
      if (dex_locations_.empty()) {
        Usage("DEX locations must be specified when passing APK files to --generate-test-profile");
      }
    }
    // ShouldGenerateTestProfile confirms !test_profile_.empty().
    int profile_test_fd = open(test_profile_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (profile_test_fd < 0) {
      LOG(ERROR) << "Cannot open " << test_profile_ << strerror(errno);
      return -1;
    }
    bool result;
    if (apk_files_.empty() && apks_fd_.empty() && dex_locations_.empty()) {
      result = ProfileCompilationInfo::GenerateTestProfile(profile_test_fd,
                                                           test_profile_num_dex_,
                                                           test_profile_method_ratio_,
                                                           test_profile_class_ratio_,
                                                           test_profile_seed_);
    } else {
      // Initialize MemMap for ZipArchive::OpenFromFd.
      MemMap::Init();
      // Open the dex files to look up classes and methods.
      std::vector<std::unique_ptr<const DexFile>> dex_files;
      OpenApkFilesFromLocations(&dex_files);
      // Create a random profile file based on the set of dex files.
      result = ProfileCompilationInfo::GenerateTestProfile(profile_test_fd,
                                                           dex_files,
                                                           test_profile_seed_);
    }
    close(profile_test_fd);  // ignore close result.
    return result ? 0 : -1;
  }

  bool ShouldGenerateTestProfile() {
    return !test_profile_.empty();
  }

 private:
  static void ParseFdForCollection(const StringPiece& option,
                                   const char* arg_name,
                                   std::vector<int>* fds) {
    int fd;
    ParseUintOption(option, arg_name, &fd, Usage);
    fds->push_back(fd);
  }

  static void CloseAllFds(const std::vector<int>& fds, const char* descriptor) {
    for (size_t i = 0; i < fds.size(); i++) {
      if (close(fds[i]) < 0) {
        PLOG(WARNING) << "Failed to close descriptor for " << descriptor << " at index " << i;
      }
    }
  }

  void LogCompletionTime() {
    static constexpr uint64_t kLogThresholdTime = MsToNs(100);  // 100ms
    uint64_t time_taken = NanoTime() - start_ns_;
    if (time_taken > kLogThresholdTime) {
      LOG(WARNING) << "profman took " << PrettyDuration(time_taken);
    }
  }

  std::vector<std::string> profile_files_;
  std::vector<int> profile_files_fd_;
  std::vector<std::string> dex_locations_;
  std::vector<std::string> apk_files_;
  std::vector<int> apks_fd_;
  std::string reference_profile_file_;
  int reference_profile_file_fd_;
  bool dump_only_;
  bool dump_classes_and_methods_;
  int dump_output_to_fd_;
  std::string test_profile_;
  std::string create_profile_from_file_;
  uint16_t test_profile_num_dex_;
  uint16_t test_profile_method_ratio_;
  uint16_t test_profile_class_ratio_;
  uint32_t test_profile_seed_;
  uint64_t start_ns_;
};

// See ProfileAssistant::ProcessingResult for return codes.
static int profman(int argc, char** argv) {
  ProfMan profman;

  // Parse arguments. Argument mistakes will lead to exit(EXIT_FAILURE) in UsageError.
  profman.ParseArgs(argc, argv);

  if (profman.ShouldGenerateTestProfile()) {
    return profman.GenerateTestProfile();
  }
  if (profman.ShouldOnlyDumpProfile()) {
    return profman.DumpProfileInfo();
  }
  if (profman.ShouldOnlyDumpClassesAndMethods()) {
    return profman.DumpClasses();
  }
  if (profman.ShouldCreateProfile()) {
    return profman.CreateProfile();
  }
  // Process profile information and assess if we need to do a profile guided compilation.
  // This operation involves I/O.
  return profman.ProcessProfiles();
}

}  // namespace art

int main(int argc, char **argv) {
  return art::profman(argc, argv);
}

