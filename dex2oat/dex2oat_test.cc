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

#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "android-base/stringprintf.h"

#include "common_runtime_test.h"

#include "base/logging.h"
#include "base/macros.h"
#include "base/mutex-inl.h"
#include "bytecode_utils.h"
#include "dex2oat_environment_test.h"
#include "dex2oat_return_codes.h"
#include "dex_file-inl.h"
#include "jit/profile_compilation_info.h"
#include "oat.h"
#include "oat_file.h"
#include "utils.h"

namespace art {

static constexpr size_t kMaxMethodIds = 65535;
static constexpr bool kDebugArgs = false;

using android::base::StringPrintf;

class Dex2oatTest : public Dex2oatEnvironmentTest {
 public:
  virtual void TearDown() OVERRIDE {
    Dex2oatEnvironmentTest::TearDown();

    output_ = "";
    error_msg_ = "";
    success_ = false;
  }

 protected:
  int GenerateOdexForTestWithStatus(const std::vector<std::string>& dex_locations,
                                    const std::string& odex_location,
                                    CompilerFilter::Filter filter,
                                    std::string* error_msg,
                                    const std::vector<std::string>& extra_args = {},
                                    bool use_fd = false) {
    std::unique_ptr<File> oat_file;
    std::vector<std::string> args;
    // Add dex file args.
    for (const std::string& dex_location : dex_locations) {
      args.push_back("--dex-file=" + dex_location);
    }
    if (use_fd) {
      oat_file.reset(OS::CreateEmptyFile(odex_location.c_str()));
      CHECK(oat_file != nullptr) << odex_location;
      args.push_back("--oat-fd=" + std::to_string(oat_file->Fd()));
      args.push_back("--oat-location=" + odex_location);
    } else {
      args.push_back("--oat-file=" + odex_location);
    }
    args.push_back("--compiler-filter=" + CompilerFilter::NameOfFilter(filter));
    args.push_back("--runtime-arg");
    args.push_back("-Xnorelocate");

    args.insert(args.end(), extra_args.begin(), extra_args.end());

    int status = Dex2Oat(args, error_msg);
    if (oat_file != nullptr) {
      CHECK_EQ(oat_file->FlushClose(), 0) << "Could not flush and close oat file";
    }
    return status;
  }

  void GenerateOdexForTest(const std::string& dex_location,
                           const std::string& odex_location,
                           CompilerFilter::Filter filter,
                           const std::vector<std::string>& extra_args = {},
                           bool expect_success = true,
                           bool use_fd = false,
                           std::function<void(const OatFile&)> check_oat = [](const OatFile&) {}) {
    std::string error_msg;
    int status = GenerateOdexForTestWithStatus({dex_location},
                                               odex_location,
                                               filter,
                                               &error_msg,
                                               extra_args,
                                               use_fd);
    bool success = (status == 0);
    if (expect_success) {
      ASSERT_TRUE(success) << error_msg << std::endl << output_;

      // Verify the odex file was generated as expected.
      std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                       odex_location.c_str(),
                                                       nullptr,
                                                       nullptr,
                                                       false,
                                                       /*low_4gb*/false,
                                                       dex_location.c_str(),
                                                       &error_msg));
      ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;

      CheckFilter(filter, odex_file->GetCompilerFilter());
      check_oat(*(odex_file.get()));
    } else {
      ASSERT_FALSE(success) << output_;

      error_msg_ = error_msg;

      // Verify there's no loadable odex file.
      std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                       odex_location.c_str(),
                                                       nullptr,
                                                       nullptr,
                                                       false,
                                                       /*low_4gb*/false,
                                                       dex_location.c_str(),
                                                       &error_msg));
      ASSERT_TRUE(odex_file.get() == nullptr);
    }
  }

  // Check the input compiler filter against the generated oat file's filter. May be overridden
  // in subclasses when equality is not expected.
  virtual void CheckFilter(CompilerFilter::Filter expected, CompilerFilter::Filter actual) {
    EXPECT_EQ(expected, actual);
  }

  int Dex2Oat(const std::vector<std::string>& dex2oat_args, std::string* error_msg) {
    Runtime* runtime = Runtime::Current();

    const std::vector<gc::space::ImageSpace*>& image_spaces =
        runtime->GetHeap()->GetBootImageSpaces();
    if (image_spaces.empty()) {
      *error_msg = "No image location found for Dex2Oat.";
      return false;
    }
    std::string image_location = image_spaces[0]->GetImageLocation();

    std::vector<std::string> argv;
    argv.push_back(runtime->GetCompilerExecutable());

    if (runtime->IsJavaDebuggable()) {
      argv.push_back("--debuggable");
    }
    runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

    if (!runtime->IsVerificationEnabled()) {
      argv.push_back("--compiler-filter=assume-verified");
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

    argv.insert(argv.end(), dex2oat_args.begin(), dex2oat_args.end());

    // We must set --android-root.
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    argv.push_back("--android-root=" + std::string(android_root));

    if (kDebugArgs) {
      std::string all_args;
      for (const std::string& arg : argv) {
        all_args += arg + " ";
      }
      LOG(ERROR) << all_args;
    }

    int link[2];

    if (pipe(link) == -1) {
      return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
      return false;
    }

    if (pid == 0) {
      // We need dex2oat to actually log things.
      setenv("ANDROID_LOG_TAGS", "*:d", 1);
      dup2(link[1], STDERR_FILENO);
      close(link[0]);
      close(link[1]);
      std::vector<const char*> c_args;
      for (const std::string& str : argv) {
        c_args.push_back(str.c_str());
      }
      c_args.push_back(nullptr);
      execv(c_args[0], const_cast<char* const*>(c_args.data()));
      exit(1);
      UNREACHABLE();
    } else {
      close(link[1]);
      char buffer[128];
      memset(buffer, 0, 128);
      ssize_t bytes_read = 0;

      while (TEMP_FAILURE_RETRY(bytes_read = read(link[0], buffer, 128)) > 0) {
        output_ += std::string(buffer, bytes_read);
      }
      close(link[0]);
      int status = -1;
      if (waitpid(pid, &status, 0) != -1) {
        success_ = (status == 0);
      }
      return status;
    }
  }

  std::string output_ = "";
  std::string error_msg_ = "";
  bool success_ = false;
};

class Dex2oatSwapTest : public Dex2oatTest {
 protected:
  void RunTest(bool use_fd, bool expect_use, const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/Dex2OatSwapTest.jar";
    std::string odex_location = GetOdexDir() + "/Dex2OatSwapTest.odex";

    Copy(GetTestDexFileName(), dex_location);

    std::vector<std::string> copy(extra_args);

    std::unique_ptr<ScratchFile> sf;
    if (use_fd) {
      sf.reset(new ScratchFile());
      copy.push_back(android::base::StringPrintf("--swap-fd=%d", sf->GetFd()));
    } else {
      std::string swap_location = GetOdexDir() + "/Dex2OatSwapTest.odex.swap";
      copy.push_back("--swap-file=" + swap_location);
    }
    GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed, copy);

    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(expect_use);
  }

  virtual std::string GetTestDexFileName() {
    return Dex2oatEnvironmentTest::GetTestDexFileName("VerifierDeps");
  }

  virtual void CheckResult(bool expect_use) {
    if (kIsTargetBuild) {
      CheckTargetResult(expect_use);
    } else {
      CheckHostResult(expect_use);
    }
  }

  virtual void CheckTargetResult(bool expect_use ATTRIBUTE_UNUSED) {
    // TODO: Ignore for now, as we won't capture any output (it goes to the logcat). We may do
    //       something for variants with file descriptor where we can control the lifetime of
    //       the swap file and thus take a look at it.
  }

  virtual void CheckHostResult(bool expect_use) {
    if (!kIsTargetBuild) {
      if (expect_use) {
        EXPECT_NE(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      }
    }
  }

  // Check whether the dex2oat run was really successful.
  virtual void CheckValidity() {
    if (kIsTargetBuild) {
      CheckTargetValidity();
    } else {
      CheckHostValidity();
    }
  }

  virtual void CheckTargetValidity() {
    // TODO: Ignore for now, as we won't capture any output (it goes to the logcat). We may do
    //       something for variants with file descriptor where we can control the lifetime of
    //       the swap file and thus take a look at it.
  }

  // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
  virtual void CheckHostValidity() {
    EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
  }
};

TEST_F(Dex2oatSwapTest, DoNotUseSwapDefaultSingleSmall) {
  RunTest(false /* use_fd */, false /* expect_use */);
  RunTest(true /* use_fd */, false /* expect_use */);
}

TEST_F(Dex2oatSwapTest, DoNotUseSwapSingle) {
  RunTest(false /* use_fd */, false /* expect_use */, { "--swap-dex-size-threshold=0" });
  RunTest(true /* use_fd */, false /* expect_use */, { "--swap-dex-size-threshold=0" });
}

TEST_F(Dex2oatSwapTest, DoNotUseSwapSmall) {
  RunTest(false /* use_fd */, false /* expect_use */, { "--swap-dex-count-threshold=0" });
  RunTest(true /* use_fd */, false /* expect_use */, { "--swap-dex-count-threshold=0" });
}

TEST_F(Dex2oatSwapTest, DoUseSwapSingleSmall) {
  RunTest(false /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
  RunTest(true /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
}

class Dex2oatSwapUseTest : public Dex2oatSwapTest {
 protected:
  void CheckHostResult(bool expect_use) OVERRIDE {
    if (!kIsTargetBuild) {
      if (expect_use) {
        EXPECT_NE(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Large app, accepted running with swap."), std::string::npos)
            << output_;
      }
    }
  }

  std::string GetTestDexFileName() OVERRIDE {
    // Use Statics as it has a handful of functions.
    return CommonRuntimeTest::GetTestDexFileName("Statics");
  }

  void GrabResult1() {
    if (!kIsTargetBuild) {
      native_alloc_1_ = ParseNativeAlloc();
      swap_1_ = ParseSwap(false /* expected */);
    } else {
      native_alloc_1_ = std::numeric_limits<size_t>::max();
      swap_1_ = 0;
    }
  }

  void GrabResult2() {
    if (!kIsTargetBuild) {
      native_alloc_2_ = ParseNativeAlloc();
      swap_2_ = ParseSwap(true /* expected */);
    } else {
      native_alloc_2_ = 0;
      swap_2_ = std::numeric_limits<size_t>::max();
    }
  }

 private:
  size_t ParseNativeAlloc() {
    std::regex native_alloc_regex("dex2oat took.*native alloc=[^ ]+ \\(([0-9]+)B\\)");
    std::smatch native_alloc_match;
    bool found = std::regex_search(output_, native_alloc_match, native_alloc_regex);
    if (!found) {
      EXPECT_TRUE(found);
      return 0;
    }
    if (native_alloc_match.size() != 2U) {
      EXPECT_EQ(native_alloc_match.size(), 2U);
      return 0;
    }

    std::istringstream stream(native_alloc_match[1].str());
    size_t value;
    stream >> value;

    return value;
  }

  size_t ParseSwap(bool expected) {
    std::regex swap_regex("dex2oat took[^\\n]+swap=[^ ]+ \\(([0-9]+)B\\)");
    std::smatch swap_match;
    bool found = std::regex_search(output_, swap_match, swap_regex);
    if (found != expected) {
      EXPECT_EQ(expected, found);
      return 0;
    }

    if (!found) {
      return 0;
    }

    if (swap_match.size() != 2U) {
      EXPECT_EQ(swap_match.size(), 2U);
      return 0;
    }

    std::istringstream stream(swap_match[1].str());
    size_t value;
    stream >> value;

    return value;
  }

 protected:
  size_t native_alloc_1_;
  size_t native_alloc_2_;

  size_t swap_1_;
  size_t swap_2_;
};

TEST_F(Dex2oatSwapUseTest, CheckSwapUsage) {
  // Native memory usage isn't correctly tracked under sanitization.
  TEST_DISABLED_FOR_MEMORY_TOOL_ASAN();

  // The `native_alloc_2_ >= native_alloc_1_` assertion below may not
  // hold true on some x86 systems; disable this test while we
  // investigate (b/29259363).
  TEST_DISABLED_FOR_X86();

  RunTest(false /* use_fd */,
          false /* expect_use */);
  GrabResult1();
  std::string output_1 = output_;

  output_ = "";

  RunTest(false /* use_fd */,
          true /* expect_use */,
          { "--swap-dex-size-threshold=0", "--swap-dex-count-threshold=0" });
  GrabResult2();
  std::string output_2 = output_;

  if (native_alloc_2_ >= native_alloc_1_ || swap_1_ >= swap_2_) {
    EXPECT_LT(native_alloc_2_, native_alloc_1_);
    EXPECT_LT(swap_1_, swap_2_);

    LOG(ERROR) << output_1;
    LOG(ERROR) << output_2;
  }
}

class Dex2oatVeryLargeTest : public Dex2oatTest {
 protected:
  void CheckFilter(CompilerFilter::Filter input ATTRIBUTE_UNUSED,
                   CompilerFilter::Filter result ATTRIBUTE_UNUSED) OVERRIDE {
    // Ignore, we'll do our own checks.
  }

  void RunTest(CompilerFilter::Filter filter,
               bool expect_large,
               const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
    std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";

    Copy(GetDexSrc1(), dex_location);

    GenerateOdexForTest(dex_location, odex_location, filter, extra_args);

    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(dex_location, odex_location, filter, expect_large);
  }

  void CheckResult(const std::string& dex_location,
                   const std::string& odex_location,
                   CompilerFilter::Filter filter,
                   bool expect_large) {
    // Host/target independent checks.
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                     odex_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;
    if (expect_large) {
      // Note: we cannot check the following:
      //   EXPECT_TRUE(CompilerFilter::IsAsGoodAs(CompilerFilter::kVerifyAtRuntime,
      //                                          odex_file->GetCompilerFilter()));
      // The reason is that the filter override currently happens when the dex files are
      // loaded in dex2oat, which is after the oat file has been started. Thus, the header
      // store cannot be changed, and the original filter is set in stone.

      for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
        std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
        ASSERT_TRUE(dex_file != nullptr);
        uint32_t class_def_count = dex_file->NumClassDefs();
        ASSERT_LT(class_def_count, std::numeric_limits<uint16_t>::max());
        for (uint16_t class_def_index = 0; class_def_index < class_def_count; ++class_def_index) {
          OatFile::OatClass oat_class = oat_dex_file->GetOatClass(class_def_index);
          EXPECT_EQ(oat_class.GetType(), OatClassType::kOatClassNoneCompiled);
        }
      }

      // If the input filter was "below," it should have been used.
      if (!CompilerFilter::IsAsGoodAs(CompilerFilter::kExtract, filter)) {
        EXPECT_EQ(odex_file->GetCompilerFilter(), filter);
      }
    } else {
      EXPECT_EQ(odex_file->GetCompilerFilter(), filter);
    }

    // Host/target dependent checks.
    if (kIsTargetBuild) {
      CheckTargetResult(expect_large);
    } else {
      CheckHostResult(expect_large);
    }
  }

  void CheckTargetResult(bool expect_large ATTRIBUTE_UNUSED) {
    // TODO: Ignore for now. May do something for fd things.
  }

  void CheckHostResult(bool expect_large) {
    if (!kIsTargetBuild) {
      if (expect_large) {
        EXPECT_NE(output_.find("Very large app, downgrading to"),
                  std::string::npos)
            << output_;
      } else {
        EXPECT_EQ(output_.find("Very large app, downgrading to"),
                  std::string::npos)
            << output_;
      }
    }
  }

  // Check whether the dex2oat run was really successful.
  void CheckValidity() {
    if (kIsTargetBuild) {
      CheckTargetValidity();
    } else {
      CheckHostValidity();
    }
  }

  void CheckTargetValidity() {
    // TODO: Ignore for now.
  }

  // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
  void CheckHostValidity() {
    EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
  }
};

TEST_F(Dex2oatVeryLargeTest, DontUseVeryLarge) {
  RunTest(CompilerFilter::kAssumeVerified, false);
  RunTest(CompilerFilter::kExtract, false);
  RunTest(CompilerFilter::kQuicken, false);
  RunTest(CompilerFilter::kSpeed, false);

  RunTest(CompilerFilter::kAssumeVerified, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kExtract, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kQuicken, false, { "--very-large-app-threshold=1000000" });
  RunTest(CompilerFilter::kSpeed, false, { "--very-large-app-threshold=1000000" });
}

TEST_F(Dex2oatVeryLargeTest, UseVeryLarge) {
  RunTest(CompilerFilter::kAssumeVerified, false, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kExtract, false, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kQuicken, true, { "--very-large-app-threshold=100" });
  RunTest(CompilerFilter::kSpeed, true, { "--very-large-app-threshold=100" });
}

// Regressin test for b/35665292.
TEST_F(Dex2oatVeryLargeTest, SpeedProfileNoProfile) {
  // Test that dex2oat doesn't crash with speed-profile but no input profile.
  RunTest(CompilerFilter::kSpeedProfile, false);
}

class Dex2oatLayoutTest : public Dex2oatTest {
 protected:
  void CheckFilter(CompilerFilter::Filter input ATTRIBUTE_UNUSED,
                   CompilerFilter::Filter result ATTRIBUTE_UNUSED) OVERRIDE {
    // Ignore, we'll do our own checks.
  }

  // Emits a profile with a single dex file with the given location and a single class index of 1.
  void GenerateProfile(const std::string& test_profile,
                       const std::string& dex_location,
                       size_t num_classes,
                       uint32_t checksum) {
    int profile_test_fd = open(test_profile.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    CHECK_GE(profile_test_fd, 0);

    ProfileCompilationInfo info;
    std::string profile_key = ProfileCompilationInfo::GetProfileDexFileKey(dex_location);
    for (size_t i = 0; i < num_classes; ++i) {
      info.AddClassIndex(profile_key, checksum, dex::TypeIndex(1 + i), kMaxMethodIds);
    }
    bool result = info.Save(profile_test_fd);
    close(profile_test_fd);
    ASSERT_TRUE(result);
  }

  void CompileProfileOdex(const std::string& dex_location,
                          const std::string& odex_location,
                          const std::string& app_image_file_name,
                          bool use_fd,
                          size_t num_profile_classes,
                          const std::vector<std::string>& extra_args = {},
                          bool expect_success = true) {
    const std::string profile_location = GetScratchDir() + "/primary.prof";
    const char* location = dex_location.c_str();
    std::string error_msg;
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    ASSERT_TRUE(DexFile::Open(location, location, true, &error_msg, &dex_files));
    EXPECT_EQ(dex_files.size(), 1U);
    std::unique_ptr<const DexFile>& dex_file = dex_files[0];
    GenerateProfile(profile_location,
                    dex_location,
                    num_profile_classes,
                    dex_file->GetLocationChecksum());
    std::vector<std::string> copy(extra_args);
    copy.push_back("--profile-file=" + profile_location);
    std::unique_ptr<File> app_image_file;
    if (!app_image_file_name.empty()) {
      if (use_fd) {
        app_image_file.reset(OS::CreateEmptyFile(app_image_file_name.c_str()));
        copy.push_back("--app-image-fd=" + std::to_string(app_image_file->Fd()));
      } else {
        copy.push_back("--app-image-file=" + app_image_file_name);
      }
    }
    GenerateOdexForTest(dex_location,
                        odex_location,
                        CompilerFilter::kSpeedProfile,
                        copy,
                        expect_success,
                        use_fd);
    if (app_image_file != nullptr) {
      ASSERT_EQ(app_image_file->FlushCloseOrErase(), 0) << "Could not flush and close art file";
    }
  }

  uint64_t GetImageSize(const std::string& image_file_name) {
    EXPECT_FALSE(image_file_name.empty());
    std::unique_ptr<File> file(OS::OpenFileForReading(image_file_name.c_str()));
    CHECK(file != nullptr);
    ImageHeader image_header;
    const bool success = file->ReadFully(&image_header, sizeof(image_header));
    CHECK(success);
    CHECK(image_header.IsValid());
    ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
    return image_header.GetImageSize();
  }

  void RunTest(bool app_image) {
    std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
    std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";
    std::string app_image_file = app_image ? (GetOdexDir() + "/DexOdexNoOat.art"): "";
    Copy(GetDexSrc2(), dex_location);

    uint64_t image_file_empty_profile = 0;
    if (app_image) {
      CompileProfileOdex(dex_location,
                         odex_location,
                         app_image_file,
                         /* use_fd */ false,
                         /* num_profile_classes */ 0);
      CheckValidity();
      ASSERT_TRUE(success_);
      // Don't check the result since CheckResult relies on the class being in the profile.
      image_file_empty_profile = GetImageSize(app_image_file);
      EXPECT_GT(image_file_empty_profile, 0u);
    }

    // Small profile.
    CompileProfileOdex(dex_location,
                       odex_location,
                       app_image_file,
                       /* use_fd */ false,
                       /* num_profile_classes */ 1);
    CheckValidity();
    ASSERT_TRUE(success_);
    CheckResult(dex_location, odex_location, app_image_file);

    if (app_image) {
      // Test that the profile made a difference by adding more classes.
      const uint64_t image_file_small_profile = GetImageSize(app_image_file);
      CHECK_LT(image_file_empty_profile, image_file_small_profile);
    }
  }

  void RunTestVDex() {
    std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
    std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";
    std::string vdex_location = GetOdexDir() + "/DexOdexNoOat.vdex";
    std::string app_image_file_name = GetOdexDir() + "/DexOdexNoOat.art";
    Copy(GetDexSrc2(), dex_location);

    std::unique_ptr<File> vdex_file1(OS::CreateEmptyFile(vdex_location.c_str()));
    CHECK(vdex_file1 != nullptr) << vdex_location;
    ScratchFile vdex_file2;
    {
      std::string input_vdex = "--input-vdex-fd=-1";
      std::string output_vdex = StringPrintf("--output-vdex-fd=%d", vdex_file1->Fd());
      CompileProfileOdex(dex_location,
                         odex_location,
                         app_image_file_name,
                         /* use_fd */ true,
                         /* num_profile_classes */ 1,
                         { input_vdex, output_vdex });
      EXPECT_GT(vdex_file1->GetLength(), 0u);
    }
    {
      // Test that vdex and dexlayout fail gracefully.
      std::string input_vdex = StringPrintf("--input-vdex-fd=%d", vdex_file1->Fd());
      std::string output_vdex = StringPrintf("--output-vdex-fd=%d", vdex_file2.GetFd());
      CompileProfileOdex(dex_location,
                         odex_location,
                         app_image_file_name,
                         /* use_fd */ true,
                         /* num_profile_classes */ 1,
                         { input_vdex, output_vdex },
                         /* expect_success */ true);
      EXPECT_GT(vdex_file2.GetFile()->GetLength(), 0u);
    }
    ASSERT_EQ(vdex_file1->FlushCloseOrErase(), 0) << "Could not flush and close vdex file";
    CheckValidity();
    ASSERT_TRUE(success_);
  }

  void CheckResult(const std::string& dex_location,
                   const std::string& odex_location,
                   const std::string& app_image_file_name) {
    // Host/target independent checks.
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                     odex_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;

    const char* location = dex_location.c_str();
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    ASSERT_TRUE(DexFile::Open(location, location, true, &error_msg, &dex_files));
    EXPECT_EQ(dex_files.size(), 1U);
    std::unique_ptr<const DexFile>& old_dex_file = dex_files[0];

    for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
      std::unique_ptr<const DexFile> new_dex_file = oat_dex_file->OpenDexFile(&error_msg);
      ASSERT_TRUE(new_dex_file != nullptr);
      uint32_t class_def_count = new_dex_file->NumClassDefs();
      ASSERT_LT(class_def_count, std::numeric_limits<uint16_t>::max());
      ASSERT_GE(class_def_count, 2U);

      // The new layout swaps the classes at indexes 0 and 1.
      std::string old_class0 = old_dex_file->PrettyType(old_dex_file->GetClassDef(0).class_idx_);
      std::string old_class1 = old_dex_file->PrettyType(old_dex_file->GetClassDef(1).class_idx_);
      std::string new_class0 = new_dex_file->PrettyType(new_dex_file->GetClassDef(0).class_idx_);
      std::string new_class1 = new_dex_file->PrettyType(new_dex_file->GetClassDef(1).class_idx_);
      EXPECT_EQ(old_class0, new_class1);
      EXPECT_EQ(old_class1, new_class0);
    }

    EXPECT_EQ(odex_file->GetCompilerFilter(), CompilerFilter::kSpeedProfile);

    if (!app_image_file_name.empty()) {
      // Go peek at the image header to make sure it was large enough to contain the class.
      std::unique_ptr<File> file(OS::OpenFileForReading(app_image_file_name.c_str()));
      ImageHeader image_header;
      bool success = file->ReadFully(&image_header, sizeof(image_header));
      ASSERT_TRUE(success);
      ASSERT_TRUE(image_header.IsValid());
      EXPECT_GT(image_header.GetImageSection(ImageHeader::kSectionObjects).Size(), 0u);
    }
  }

  // Check whether the dex2oat run was really successful.
  void CheckValidity() {
    if (kIsTargetBuild) {
      CheckTargetValidity();
    } else {
      CheckHostValidity();
    }
  }

  void CheckTargetValidity() {
    // TODO: Ignore for now.
  }

  // On the host, we can get the dex2oat output. Here, look for "dex2oat took."
  void CheckHostValidity() {
    EXPECT_NE(output_.find("dex2oat took"), std::string::npos) << output_;
  }
};

TEST_F(Dex2oatLayoutTest, TestLayout) {
  RunTest(/* app-image */ false);
}

TEST_F(Dex2oatLayoutTest, TestLayoutAppImage) {
  RunTest(/* app-image */ true);
}

TEST_F(Dex2oatLayoutTest, TestVdexLayout) {
  RunTestVDex();
}

class Dex2oatUnquickenTest : public Dex2oatTest {
 protected:
  void RunUnquickenMultiDex() {
    std::string dex_location = GetScratchDir() + "/UnquickenMultiDex.jar";
    std::string odex_location = GetOdexDir() + "/UnquickenMultiDex.odex";
    std::string vdex_location = GetOdexDir() + "/UnquickenMultiDex.vdex";
    Copy(GetTestDexFileName("MultiDex"), dex_location);

    std::unique_ptr<File> vdex_file1(OS::CreateEmptyFile(vdex_location.c_str()));
    CHECK(vdex_file1 != nullptr) << vdex_location;
    // Quicken the dex file into a vdex file.
    {
      std::string input_vdex = "--input-vdex-fd=-1";
      std::string output_vdex = StringPrintf("--output-vdex-fd=%d", vdex_file1->Fd());
      GenerateOdexForTest(dex_location,
                          odex_location,
                          CompilerFilter::kQuicken,
                          { input_vdex, output_vdex },
                          /* expect_success */ true,
                          /* use_fd */ true);
      EXPECT_GT(vdex_file1->GetLength(), 0u);
    }
    // Unquicken by running the verify compiler filter on the vdex file.
    {
      std::string input_vdex = StringPrintf("--input-vdex-fd=%d", vdex_file1->Fd());
      std::string output_vdex = StringPrintf("--output-vdex-fd=%d", vdex_file1->Fd());
      GenerateOdexForTest(dex_location,
                          odex_location,
                          CompilerFilter::kVerify,
                          { input_vdex, output_vdex },
                          /* expect_success */ true,
                          /* use_fd */ true);
    }
    ASSERT_EQ(vdex_file1->FlushCloseOrErase(), 0) << "Could not flush and close vdex file";
    CheckResult(dex_location, odex_location);
    ASSERT_TRUE(success_);
  }

  void CheckResult(const std::string& dex_location, const std::string& odex_location) {
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(odex_location.c_str(),
                                                     odex_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;
    ASSERT_GE(odex_file->GetOatDexFiles().size(), 1u);

    // Iterate over the dex files and ensure there is no quickened instruction.
    for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
      std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
      for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
        const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
        const uint8_t* class_data = dex_file->GetClassData(class_def);
        if (class_data != nullptr) {
          for (ClassDataItemIterator class_it(*dex_file, class_data);
               class_it.HasNext();
               class_it.Next()) {
            if (class_it.IsAtMethod() && class_it.GetMethodCodeItem() != nullptr) {
              for (CodeItemIterator it(*class_it.GetMethodCodeItem()); !it.Done(); it.Advance()) {
                Instruction* inst = const_cast<Instruction*>(&it.CurrentInstruction());
                ASSERT_FALSE(inst->IsQuickened());
              }
            }
          }
        }
      }
    }
  }
};

TEST_F(Dex2oatUnquickenTest, UnquickenMultiDex) {
  RunUnquickenMultiDex();
}

class Dex2oatWatchdogTest : public Dex2oatTest {
 protected:
  void RunTest(bool expect_success, const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/Dex2OatSwapTest.jar";
    std::string odex_location = GetOdexDir() + "/Dex2OatSwapTest.odex";

    Copy(GetTestDexFileName(), dex_location);

    std::vector<std::string> copy(extra_args);

    std::string swap_location = GetOdexDir() + "/Dex2OatSwapTest.odex.swap";
    copy.push_back("--swap-file=" + swap_location);
    GenerateOdexForTest(dex_location,
                        odex_location,
                        CompilerFilter::kSpeed,
                        copy,
                        expect_success);
  }

  std::string GetTestDexFileName() {
    return GetDexSrc1();
  }
};

TEST_F(Dex2oatWatchdogTest, TestWatchdogOK) {
  // Check with default.
  RunTest(true);

  // Check with ten minutes.
  RunTest(true, { "--watchdog-timeout=600000" });
}

TEST_F(Dex2oatWatchdogTest, TestWatchdogTrigger) {
  TEST_DISABLED_FOR_MEMORY_TOOL_VALGRIND();  // b/63052624
  // Check with ten milliseconds.
  RunTest(false, { "--watchdog-timeout=10" });
}

class Dex2oatReturnCodeTest : public Dex2oatTest {
 protected:
  int RunTest(const std::vector<std::string>& extra_args = {}) {
    std::string dex_location = GetScratchDir() + "/Dex2OatSwapTest.jar";
    std::string odex_location = GetOdexDir() + "/Dex2OatSwapTest.odex";

    Copy(GetTestDexFileName(), dex_location);

    std::string error_msg;
    return GenerateOdexForTestWithStatus({dex_location},
                                         odex_location,
                                         CompilerFilter::kSpeed,
                                         &error_msg,
                                         extra_args);
  }

  std::string GetTestDexFileName() {
    return GetDexSrc1();
  }
};

TEST_F(Dex2oatReturnCodeTest, TestCreateRuntime) {
  TEST_DISABLED_FOR_MEMORY_TOOL();  // b/19100793
  int status = RunTest({ "--boot-image=/this/does/not/exist/yolo.oat" });
  EXPECT_EQ(static_cast<int>(dex2oat::ReturnCode::kCreateRuntime), WEXITSTATUS(status)) << output_;
}

class Dex2oatClassLoaderContextTest : public Dex2oatTest {
 protected:
  void RunTest(const char* class_loader_context,
               const char* expected_classpath_key,
               bool expected_success,
               bool use_second_source = false) {
    std::string dex_location = GetUsedDexLocation();
    std::string odex_location = GetUsedOatLocation();

    Copy(use_second_source ? GetDexSrc2() : GetDexSrc1(), dex_location);

    std::string error_msg;
    std::vector<std::string> extra_args;
    if (class_loader_context != nullptr) {
      extra_args.push_back(std::string("--class-loader-context=") + class_loader_context);
    }
    auto check_oat = [expected_classpath_key](const OatFile& oat_file) {
      ASSERT_TRUE(expected_classpath_key != nullptr);
      const char* classpath = oat_file.GetOatHeader().GetStoreValueByKey(OatHeader::kClassPathKey);
      ASSERT_TRUE(classpath != nullptr);
      ASSERT_STREQ(expected_classpath_key, classpath);
    };

    GenerateOdexForTest(dex_location,
                        odex_location,
                        CompilerFilter::kQuicken,
                        extra_args,
                        expected_success,
                        /*use_fd*/ false,
                        check_oat);
  }

  std::string GetUsedDexLocation() {
    return GetScratchDir() + "/Context.jar";
  }

  std::string GetUsedOatLocation() {
    return GetOdexDir() + "/Context.odex";
  }

  const char* kEmptyClassPathKey = "PCL[]";
};

TEST_F(Dex2oatClassLoaderContextTest, InvalidContext) {
  RunTest("Invalid[]", /*expected_classpath_key*/ nullptr, /*expected_success*/ false);
}

TEST_F(Dex2oatClassLoaderContextTest, EmptyContext) {
  RunTest("PCL[]", kEmptyClassPathKey, /*expected_success*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, SpecialContext) {
  RunTest(OatFile::kSpecialSharedLibrary,
          OatFile::kSpecialSharedLibrary,
          /*expected_success*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, ContextWithTheSourceDexFiles) {
  std::string context = "PCL[" + GetUsedDexLocation() + "]";
  RunTest(context.c_str(), kEmptyClassPathKey, /*expected_success*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, ContextWithOtherDexFiles) {
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("Nested");

  std::string context = "PCL[" + dex_files[0]->GetLocation() + "]";
  std::string expected_classpath_key = "PCL[" +
      dex_files[0]->GetLocation() + "*" + std::to_string(dex_files[0]->GetLocationChecksum()) + "]";
  RunTest(context.c_str(), expected_classpath_key.c_str(), true);
}

TEST_F(Dex2oatClassLoaderContextTest, ContextWithStrippedDexFiles) {
  std::string stripped_classpath = GetScratchDir() + "/stripped_classpath.jar";
  Copy(GetStrippedDexSrc1(), stripped_classpath);

  std::string context = "PCL[" + stripped_classpath + "]";
  // Expect an empty context because stripped dex files cannot be open.
  RunTest(context.c_str(), kEmptyClassPathKey , /*expected_success*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, ContextWithStrippedDexFilesBackedByOdex) {
  std::string stripped_classpath = GetScratchDir() + "/stripped_classpath.jar";
  std::string odex_for_classpath = GetOdexDir() + "/stripped_classpath.odex";

  Copy(GetDexSrc1(), stripped_classpath);

  GenerateOdexForTest(stripped_classpath,
                      odex_for_classpath,
                      CompilerFilter::kQuicken,
                      {},
                      true);

  // Strip the dex file
  Copy(GetStrippedDexSrc1(), stripped_classpath);

  std::string context = "PCL[" + stripped_classpath + "]";
  std::string expected_classpath_key;
  {
    // Open the oat file to get the expected classpath.
    OatFileAssistant oat_file_assistant(stripped_classpath.c_str(), kRuntimeISA, false);
    std::unique_ptr<OatFile> oat_file(oat_file_assistant.GetBestOatFile());
    std::vector<std::unique_ptr<const DexFile>> oat_dex_files =
        OatFileAssistant::LoadDexFiles(*oat_file, stripped_classpath.c_str());
    expected_classpath_key = "PCL[";
    for (size_t i = 0; i < oat_dex_files.size(); i++) {
      if (i > 0) {
        expected_classpath_key + ":";
      }
      expected_classpath_key += oat_dex_files[i]->GetLocation() + "*" +
          std::to_string(oat_dex_files[i]->GetLocationChecksum());
    }
    expected_classpath_key += "]";
  }

  RunTest(context.c_str(),
          expected_classpath_key.c_str(),
          /*expected_success*/ true,
          /*use_second_source*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, ContextWithNotExistentDexFiles) {
  std::string context = "PCL[does_not_exists.dex]";
  // Expect an empty context because stripped dex files cannot be open.
  RunTest(context.c_str(), kEmptyClassPathKey, /*expected_success*/ true);
}

TEST_F(Dex2oatClassLoaderContextTest, ChainContext) {
  std::vector<std::unique_ptr<const DexFile>> dex_files1 = OpenTestDexFiles("Nested");
  std::vector<std::unique_ptr<const DexFile>> dex_files2 = OpenTestDexFiles("MultiDex");

  std::string context = "PCL[" + GetTestDexFileName("Nested") + "];" +
      "DLC[" + GetTestDexFileName("MultiDex") + "]";
  std::string expected_classpath_key = "PCL[" + CreateClassPathWithChecksums(dex_files1) + "];" +
      "DLC[" + CreateClassPathWithChecksums(dex_files2) + "]";

  RunTest(context.c_str(), expected_classpath_key.c_str(), true);
}

class Dex2oatDeterminism : public Dex2oatTest {};

TEST_F(Dex2oatDeterminism, UnloadCompile) {
  if (!kUseReadBarrier &&
      gc::kCollectorTypeDefault != gc::kCollectorTypeCMS &&
      gc::kCollectorTypeDefault != gc::kCollectorTypeMS) {
    LOG(INFO) << "Test requires determinism support.";
    return;
  }
  Runtime* const runtime = Runtime::Current();
  std::string out_dir = GetScratchDir();
  const std::string base_oat_name = out_dir + "/base.oat";
  const std::string base_vdex_name = out_dir + "/base.vdex";
  const std::string unload_oat_name = out_dir + "/unload.oat";
  const std::string unload_vdex_name = out_dir + "/unload.vdex";
  const std::string no_unload_oat_name = out_dir + "/nounload.oat";
  const std::string no_unload_vdex_name = out_dir + "/nounload.vdex";
  const std::string app_image_name = out_dir + "/unload.art";
  std::string error_msg;
  const std::vector<gc::space::ImageSpace*>& spaces = runtime->GetHeap()->GetBootImageSpaces();
  ASSERT_GT(spaces.size(), 0u);
  const std::string image_location = spaces[0]->GetImageLocation();
  // Without passing in an app image, it will unload in between compilations.
  const int res = GenerateOdexForTestWithStatus(
      GetLibCoreDexFileNames(),
      base_oat_name,
      CompilerFilter::Filter::kQuicken,
      &error_msg,
      {"--force-determinism", "--avoid-storing-invocation"});
  EXPECT_EQ(res, 0);
  Copy(base_oat_name, unload_oat_name);
  Copy(base_vdex_name, unload_vdex_name);
  std::unique_ptr<File> unload_oat(OS::OpenFileForReading(unload_oat_name.c_str()));
  std::unique_ptr<File> unload_vdex(OS::OpenFileForReading(unload_vdex_name.c_str()));
  ASSERT_TRUE(unload_oat != nullptr);
  ASSERT_TRUE(unload_vdex != nullptr);
  EXPECT_GT(unload_oat->GetLength(), 0u);
  EXPECT_GT(unload_vdex->GetLength(), 0u);
  // Regenerate with an app image to disable the dex2oat unloading and verify that the output is
  // the same.
  const int res2 = GenerateOdexForTestWithStatus(
      GetLibCoreDexFileNames(),
      base_oat_name,
      CompilerFilter::Filter::kQuicken,
      &error_msg,
      {"--force-determinism", "--avoid-storing-invocation", "--app-image-file=" + app_image_name});
  EXPECT_EQ(res2, 0);
  Copy(base_oat_name, no_unload_oat_name);
  Copy(base_vdex_name, no_unload_vdex_name);
  std::unique_ptr<File> no_unload_oat(OS::OpenFileForReading(no_unload_oat_name.c_str()));
  std::unique_ptr<File> no_unload_vdex(OS::OpenFileForReading(no_unload_vdex_name.c_str()));
  ASSERT_TRUE(no_unload_oat != nullptr);
  ASSERT_TRUE(no_unload_vdex != nullptr);
  EXPECT_GT(no_unload_oat->GetLength(), 0u);
  EXPECT_GT(no_unload_vdex->GetLength(), 0u);
  // Verify that both of the files are the same (odex and vdex).
  EXPECT_EQ(unload_oat->GetLength(), no_unload_oat->GetLength());
  EXPECT_EQ(unload_vdex->GetLength(), no_unload_vdex->GetLength());
  EXPECT_EQ(unload_oat->Compare(no_unload_oat.get()), 0)
      << unload_oat_name << " " << no_unload_oat_name;
  EXPECT_EQ(unload_vdex->Compare(no_unload_vdex.get()), 0)
      << unload_vdex_name << " " << no_unload_vdex_name;
  // App image file.
  std::unique_ptr<File> app_image_file(OS::OpenFileForReading(app_image_name.c_str()));
  ASSERT_TRUE(app_image_file != nullptr);
  EXPECT_GT(app_image_file->GetLength(), 0u);
}

// Test that dexlayout section info is correctly written to the oat file for profile based
// compilation.
TEST_F(Dex2oatTest, LayoutSections) {
  using Hotness = ProfileCompilationInfo::MethodHotness;
  std::unique_ptr<const DexFile> dex(OpenTestDexFile("ManyMethods"));
  ScratchFile profile_file;
  // We can only layout method indices with code items, figure out which ones have this property
  // first.
  std::vector<uint16_t> methods;
  {
    const DexFile::TypeId* type_id = dex->FindTypeId("LManyMethods;");
    dex::TypeIndex type_idx = dex->GetIndexForTypeId(*type_id);
    const DexFile::ClassDef* class_def = dex->FindClassDef(type_idx);
    ClassDataItemIterator it(*dex, dex->GetClassData(*class_def));
    it.SkipAllFields();
    std::set<size_t> code_item_offsets;
    for (; it.HasNextDirectMethod() || it.HasNextVirtualMethod(); it.Next()) {
      const uint16_t method_idx = it.GetMemberIndex();
      const size_t code_item_offset = it.GetMethodCodeItemOffset();
      if (code_item_offsets.insert(code_item_offset).second) {
        // Unique code item, add the method index.
        methods.push_back(method_idx);
      }
    }
    DCHECK(!it.HasNext());
  }
  ASSERT_GE(methods.size(), 8u);
  std::vector<uint16_t> hot_methods = {methods[1], methods[3], methods[5]};
  std::vector<uint16_t> startup_methods = {methods[1], methods[2], methods[7]};
  std::vector<uint16_t> post_methods = {methods[0], methods[2], methods[6]};
  // Here, we build the profile from the method lists.
  ProfileCompilationInfo info;
  info.AddMethodsForDex(
      static_cast<Hotness::Flag>(Hotness::kFlagHot | Hotness::kFlagStartup),
      dex.get(),
      hot_methods.begin(),
      hot_methods.end());
  info.AddMethodsForDex(
      Hotness::kFlagStartup,
      dex.get(),
      startup_methods.begin(),
      startup_methods.end());
  info.AddMethodsForDex(
      Hotness::kFlagPostStartup,
      dex.get(),
      post_methods.begin(),
      post_methods.end());
  for (uint16_t id : hot_methods) {
    EXPECT_TRUE(info.GetMethodHotness(MethodReference(dex.get(), id)).IsHot());
    EXPECT_TRUE(info.GetMethodHotness(MethodReference(dex.get(), id)).IsStartup());
  }
  for (uint16_t id : startup_methods) {
    EXPECT_TRUE(info.GetMethodHotness(MethodReference(dex.get(), id)).IsStartup());
  }
  for (uint16_t id : post_methods) {
    EXPECT_TRUE(info.GetMethodHotness(MethodReference(dex.get(), id)).IsPostStartup());
  }
  // Save the profile since we want to use it with dex2oat to produce an oat file.
  ASSERT_TRUE(info.Save(profile_file.GetFd()));
  // Generate a profile based odex.
  const std::string dir = GetScratchDir();
  const std::string oat_filename = dir + "/base.oat";
  const std::string vdex_filename = dir + "/base.vdex";
  std::string error_msg;
  const int res = GenerateOdexForTestWithStatus(
      {dex->GetLocation()},
      oat_filename,
      CompilerFilter::Filter::kQuicken,
      &error_msg,
      {"--profile-file=" + profile_file.GetFilename()});
  EXPECT_EQ(res, 0);

  // Open our generated oat file.
  std::unique_ptr<OatFile> odex_file(OatFile::Open(oat_filename.c_str(),
                                                   oat_filename.c_str(),
                                                   nullptr,
                                                   nullptr,
                                                   false,
                                                   /*low_4gb*/false,
                                                   dex->GetLocation().c_str(),
                                                   &error_msg));
  ASSERT_TRUE(odex_file != nullptr);
  std::vector<const OatDexFile*> oat_dex_files = odex_file->GetOatDexFiles();
  ASSERT_EQ(oat_dex_files.size(), 1u);
  // Check that the code sections match what we expect.
  for (const OatDexFile* oat_dex : oat_dex_files) {
    const DexLayoutSections* const sections = oat_dex->GetDexLayoutSections();
    // Testing of logging the sections.
    ASSERT_TRUE(sections != nullptr);
    LOG(INFO) << *sections;

    // Load the sections into temporary variables for convenience.
    const DexLayoutSection& code_section =
        sections->sections_[static_cast<size_t>(DexLayoutSections::SectionType::kSectionTypeCode)];
    const DexLayoutSection::Subsection& section_hot_code =
        code_section.parts_[static_cast<size_t>(LayoutType::kLayoutTypeHot)];
    const DexLayoutSection::Subsection& section_sometimes_used =
        code_section.parts_[static_cast<size_t>(LayoutType::kLayoutTypeSometimesUsed)];
    const DexLayoutSection::Subsection& section_startup_only =
        code_section.parts_[static_cast<size_t>(LayoutType::kLayoutTypeStartupOnly)];
    const DexLayoutSection::Subsection& section_unused =
        code_section.parts_[static_cast<size_t>(LayoutType::kLayoutTypeUnused)];

    // All the sections should be non-empty.
    EXPECT_GT(section_hot_code.size_, 0u);
    EXPECT_GT(section_sometimes_used.size_, 0u);
    EXPECT_GT(section_startup_only.size_, 0u);
    EXPECT_GT(section_unused.size_, 0u);

    // Open the dex file since we need to peek at the code items to verify the layout matches what
    // we expect.
    std::unique_ptr<const DexFile> dex_file(oat_dex->OpenDexFile(&error_msg));
    ASSERT_TRUE(dex_file != nullptr) << error_msg;
    const DexFile::TypeId* type_id = dex_file->FindTypeId("LManyMethods;");
    ASSERT_TRUE(type_id != nullptr);
    dex::TypeIndex type_idx = dex_file->GetIndexForTypeId(*type_id);
    const DexFile::ClassDef* class_def = dex_file->FindClassDef(type_idx);
    ASSERT_TRUE(class_def != nullptr);

    // Count how many code items are for each category, there should be at least one per category.
    size_t hot_count = 0;
    size_t post_startup_count = 0;
    size_t startup_count = 0;
    size_t unused_count = 0;
    // Visit all of the methdos of the main class and cross reference the method indices to their
    // corresponding code item offsets to verify the layout.
    ClassDataItemIterator it(*dex_file, dex_file->GetClassData(*class_def));
    it.SkipAllFields();
    for (; it.HasNextDirectMethod() || it.HasNextVirtualMethod(); it.Next()) {
      const size_t method_idx = it.GetMemberIndex();
      const size_t code_item_offset = it.GetMethodCodeItemOffset();
      const bool is_hot = ContainsElement(hot_methods, method_idx);
      const bool is_startup = ContainsElement(startup_methods, method_idx);
      const bool is_post_startup = ContainsElement(post_methods, method_idx);
      if (is_hot) {
        // Hot is highest precedence, check that the hot methods are in the hot section.
        EXPECT_LT(code_item_offset - section_hot_code.offset_, section_hot_code.size_);
        ++hot_count;
      } else if (is_post_startup) {
        // Post startup is sometimes used section.
        EXPECT_LT(code_item_offset - section_sometimes_used.offset_, section_sometimes_used.size_);
        ++post_startup_count;
      } else if (is_startup) {
        // Startup at this point means not hot or post startup, these must be startup only then.
        EXPECT_LT(code_item_offset - section_startup_only.offset_, section_startup_only.size_);
        ++startup_count;
      } else {
        // If no flags are set, the method should be unused.
        EXPECT_LT(code_item_offset - section_unused.offset_, section_unused.size_);
        ++unused_count;
      }
    }
    DCHECK(!it.HasNext());
    EXPECT_GT(hot_count, 0u);
    EXPECT_GT(post_startup_count, 0u);
    EXPECT_GT(startup_count, 0u);
    EXPECT_GT(unused_count, 0u);
  }
}

}  // namespace art
