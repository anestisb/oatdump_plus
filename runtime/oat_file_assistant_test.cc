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

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <sys/param.h>

#include "android-base/strings.h"
#include <backtrace/BacktraceMap.h>
#include <gtest/gtest.h>

#include "art_field-inl.h"
#include "class_linker-inl.h"
#include "common_runtime_test.h"
#include "compiler_callbacks.h"
#include "dex2oat_environment_test.h"
#include "gc/space/image_space.h"
#include "mem_map.h"
#include "oat_file_assistant.h"
#include "oat_file_manager.h"
#include "os.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "utils.h"

namespace art {

class OatFileAssistantTest : public Dex2oatEnvironmentTest {
 public:
  virtual void SetUp() OVERRIDE {
    ReserveImageSpace();
    Dex2oatEnvironmentTest::SetUp();
  }

  // Pre-Relocate the image to a known non-zero offset so we don't have to
  // deal with the runtime randomly relocating the image by 0 and messing up
  // the expected results of the tests.
  bool PreRelocateImage(const std::string& image_location, std::string* error_msg) {
    std::string image;
    if (!GetCachedImageFile(image_location, &image, error_msg)) {
      return false;
    }

    std::string patchoat = GetAndroidRoot();
    patchoat += kIsDebugBuild ? "/bin/patchoatd" : "/bin/patchoat";

    std::vector<std::string> argv;
    argv.push_back(patchoat);
    argv.push_back("--input-image-location=" + image_location);
    argv.push_back("--output-image-file=" + image);
    argv.push_back("--instruction-set=" + std::string(GetInstructionSetString(kRuntimeISA)));
    argv.push_back("--base-offset-delta=0x00008000");
    return Exec(argv, error_msg);
  }

  virtual void PreRuntimeCreate() {
    std::string error_msg;
    ASSERT_TRUE(PreRelocateImage(GetImageLocation(), &error_msg)) << error_msg;
    ASSERT_TRUE(PreRelocateImage(GetImageLocation2(), &error_msg)) << error_msg;
    UnreserveImageSpace();
  }

  virtual void PostRuntimeCreate() OVERRIDE {
    ReserveImageSpace();
  }

  // Generate an oat file for the purposes of test.
  void GenerateOatForTest(const std::string& dex_location,
                          const std::string& oat_location,
                          CompilerFilter::Filter filter,
                          bool relocate,
                          bool pic,
                          bool with_patch_info,
                          bool with_alternate_image) {
    std::string dalvik_cache = GetDalvikCache(GetInstructionSetString(kRuntimeISA));
    std::string dalvik_cache_tmp = dalvik_cache + ".redirected";

    if (!relocate) {
      // Temporarily redirect the dalvik cache so dex2oat doesn't find the
      // relocated image file.
      ASSERT_EQ(0, rename(dalvik_cache.c_str(), dalvik_cache_tmp.c_str())) << strerror(errno);
    }

    std::vector<std::string> args;
    args.push_back("--dex-file=" + dex_location);
    args.push_back("--oat-file=" + oat_location);
    args.push_back("--compiler-filter=" + CompilerFilter::NameOfFilter(filter));
    args.push_back("--runtime-arg");

    // Use -Xnorelocate regardless of the relocate argument.
    // We control relocation by redirecting the dalvik cache when needed
    // rather than use this flag.
    args.push_back("-Xnorelocate");

    if (pic) {
      args.push_back("--compile-pic");
    }

    if (with_patch_info) {
      args.push_back("--include-patch-information");
    }

    std::string image_location = GetImageLocation();
    if (with_alternate_image) {
      args.push_back("--boot-image=" + GetImageLocation2());
    }

    std::string error_msg;
    ASSERT_TRUE(OatFileAssistant::Dex2Oat(args, &error_msg)) << error_msg;

    if (!relocate) {
      // Restore the dalvik cache if needed.
      ASSERT_EQ(0, rename(dalvik_cache_tmp.c_str(), dalvik_cache.c_str())) << strerror(errno);
    }

    // Verify the odex file was generated as expected.
    std::unique_ptr<OatFile> odex_file(OatFile::Open(oat_location.c_str(),
                                                     oat_location.c_str(),
                                                     nullptr,
                                                     nullptr,
                                                     false,
                                                     /*low_4gb*/false,
                                                     dex_location.c_str(),
                                                     &error_msg));
    ASSERT_TRUE(odex_file.get() != nullptr) << error_msg;
    EXPECT_EQ(pic, odex_file->IsPic());
    EXPECT_EQ(with_patch_info, odex_file->HasPatchInfo());
    EXPECT_EQ(filter, odex_file->GetCompilerFilter());

    std::unique_ptr<ImageHeader> image_header(
            gc::space::ImageSpace::ReadImageHeader(image_location.c_str(),
                                                   kRuntimeISA,
                                                   &error_msg));
    ASSERT_TRUE(image_header != nullptr) << error_msg;
    const OatHeader& oat_header = odex_file->GetOatHeader();
    uint32_t combined_checksum = OatFileAssistant::CalculateCombinedImageChecksum();

    if (CompilerFilter::DependsOnImageChecksum(filter)) {
      if (with_alternate_image) {
        EXPECT_NE(combined_checksum, oat_header.GetImageFileLocationOatChecksum());
      } else {
        EXPECT_EQ(combined_checksum, oat_header.GetImageFileLocationOatChecksum());
      }
    }

    if (CompilerFilter::IsBytecodeCompilationEnabled(filter)) {
      if (relocate) {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(image_header->GetOatDataBegin()),
            oat_header.GetImageFileLocationOatDataBegin());
        EXPECT_EQ(image_header->GetPatchDelta(), oat_header.GetImagePatchDelta());
      } else {
        EXPECT_NE(reinterpret_cast<uintptr_t>(image_header->GetOatDataBegin()),
            oat_header.GetImageFileLocationOatDataBegin());
        EXPECT_NE(image_header->GetPatchDelta(), oat_header.GetImagePatchDelta());
      }
    }
  }

  // Generate a non-PIC odex file for the purposes of test.
  // The generated odex file will be un-relocated.
  void GenerateOdexForTest(const std::string& dex_location,
                           const std::string& odex_location,
                           CompilerFilter::Filter filter) {
    GenerateOatForTest(dex_location,
                       odex_location,
                       filter,
                       /*relocate*/false,
                       /*pic*/false,
                       /*with_patch_info*/true,
                       /*with_alternate_image*/false);
  }

  void GeneratePicOdexForTest(const std::string& dex_location,
                              const std::string& odex_location,
                              CompilerFilter::Filter filter) {
    GenerateOatForTest(dex_location,
                       odex_location,
                       filter,
                       /*relocate*/false,
                       /*pic*/true,
                       /*with_patch_info*/false,
                       /*with_alternate_image*/false);
  }

  // Generate a non-PIC odex file without patch information for the purposes
  // of test.  The generated odex file will be un-relocated.
  void GenerateNoPatchOdexForTest(const std::string& dex_location,
                                  const std::string& odex_location,
                                  CompilerFilter::Filter filter) {
    GenerateOatForTest(dex_location,
                       odex_location,
                       filter,
                       /*relocate*/false,
                       /*pic*/false,
                       /*with_patch_info*/false,
                       /*with_alternate_image*/false);
  }

  // Generate an oat file in the oat location.
  void GenerateOatForTest(const char* dex_location,
                          CompilerFilter::Filter filter,
                          bool relocate,
                          bool pic,
                          bool with_patch_info,
                          bool with_alternate_image) {
    std::string oat_location;
    std::string error_msg;
    ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
          dex_location, kRuntimeISA, &oat_location, &error_msg)) << error_msg;
    GenerateOatForTest(dex_location,
                       oat_location,
                       filter,
                       relocate,
                       pic,
                       with_patch_info,
                       with_alternate_image);
  }

  // Generate a standard oat file in the oat location.
  void GenerateOatForTest(const char* dex_location, CompilerFilter::Filter filter) {
    GenerateOatForTest(dex_location,
                       filter,
                       /*relocate*/true,
                       /*pic*/false,
                       /*with_patch_info*/false,
                       /*with_alternate_image*/false);
  }

 private:
  // Reserve memory around where the image will be loaded so other memory
  // won't conflict when it comes time to load the image.
  // This can be called with an already loaded image to reserve the space
  // around it.
  void ReserveImageSpace() {
    MemMap::Init();

    // Ensure a chunk of memory is reserved for the image space.
    // The reservation_end includes room for the main space that has to come
    // right after the image in case of the GSS collector.
    uintptr_t reservation_start = ART_BASE_ADDRESS;
    uintptr_t reservation_end = ART_BASE_ADDRESS + 384 * MB;

    std::unique_ptr<BacktraceMap> map(BacktraceMap::Create(getpid(), true));
    ASSERT_TRUE(map.get() != nullptr) << "Failed to build process map";
    for (BacktraceMap::const_iterator it = map->begin();
        reservation_start < reservation_end && it != map->end(); ++it) {
      ReserveImageSpaceChunk(reservation_start, std::min(it->start, reservation_end));
      reservation_start = std::max(reservation_start, it->end);
    }
    ReserveImageSpaceChunk(reservation_start, reservation_end);
  }

  // Reserve a chunk of memory for the image space in the given range.
  // Only has effect for chunks with a positive number of bytes.
  void ReserveImageSpaceChunk(uintptr_t start, uintptr_t end) {
    if (start < end) {
      std::string error_msg;
      image_reservation_.push_back(std::unique_ptr<MemMap>(
          MemMap::MapAnonymous("image reservation",
              reinterpret_cast<uint8_t*>(start), end - start,
              PROT_NONE, false, false, &error_msg)));
      ASSERT_TRUE(image_reservation_.back().get() != nullptr) << error_msg;
      LOG(INFO) << "Reserved space for image " <<
        reinterpret_cast<void*>(image_reservation_.back()->Begin()) << "-" <<
        reinterpret_cast<void*>(image_reservation_.back()->End());
    }
  }


  // Unreserve any memory reserved by ReserveImageSpace. This should be called
  // before the image is loaded.
  void UnreserveImageSpace() {
    image_reservation_.clear();
  }

  std::vector<std::unique_ptr<MemMap>> image_reservation_;
};

class OatFileAssistantNoDex2OatTest : public OatFileAssistantTest {
 public:
  virtual void SetUpRuntimeOptions(RuntimeOptions* options) {
    OatFileAssistantTest::SetUpRuntimeOptions(options);
    options->push_back(std::make_pair("-Xnodex2oat", nullptr));
  }
};

// Case: We have a DEX file, but no OAT file for it.
// Expect: The status is kDex2OatNeeded.
TEST_F(OatFileAssistantTest, DexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexNoOat.jar";
  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeedProfile));
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have no DEX file and no OAT file.
// Expect: Status is kNoDexOptNeeded. Loading should fail, but not crash.
TEST_F(OatFileAssistantTest, NoDexNoOat) {
  std::string dex_location = GetScratchDir() + "/NoDexNoOat.jar";

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Trying to make the oat file up to date should not fail or crash.
  std::string error_msg;
  EXPECT_EQ(OatFileAssistant::kUpdateSucceeded, oat_file_assistant.MakeUpToDate(false, &error_msg));

  // Trying to get the best oat file should fail, but not crash.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  EXPECT_EQ(nullptr, oat_file.get());
}

// Case: We have a DEX file and up-to-date OAT file for it.
// Expect: The status is kNoDexOptNeeded.
TEST_F(OatFileAssistantTest, OatUpToDate) {
  std::string dex_location = GetScratchDir() + "/OatUpToDate.jar";
  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kEverything));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and ODEX file for a different dex location.
// Expect: The status is kDex2OatNeeded.
TEST_F(OatFileAssistantTest, OatForDifferentDex) {
  // Generate an odex file for OatForDifferentDex_A.jar
  std::string dex_location_a = GetScratchDir() + "/OatForDifferentDex_A.jar";
  std::string odex_location = GetOdexDir() + "/OatForDifferentDex.odex";
  Copy(GetDexSrc1(), dex_location_a);
  GenerateOdexForTest(dex_location_a, odex_location, CompilerFilter::kSpeed);

  // Try to use that odex file for OatForDifferentDex.jar
  std::string dex_location = GetScratchDir() + "/OatForDifferentDex.jar";
  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatDexOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
}

// Case: We have a DEX file and speed-profile OAT file for it.
// Expect: The status is kNoDexOptNeeded if the profile hasn't changed, but
// kDex2Oat if the profile has changed.
TEST_F(OatFileAssistantTest, ProfileOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/ProfileOatUpToDate.jar";
  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeedProfile);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeedProfile, false));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly, false));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeedProfile, true));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly, true));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a MultiDEX file and up-to-date OAT file for it.
// Expect: The status is kNoDexOptNeeded and we load all dex files.
TEST_F(OatFileAssistantTest, MultiDexOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/MultiDexOatUpToDate.jar";
  Copy(GetMultiDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed, false));
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());

  // Verify we can load both dex files.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(2u, dex_files.size());
}

// Case: We have a MultiDEX file where the secondary dex file is out of date.
// Expect: The status is kDex2OatNeeded.
TEST_F(OatFileAssistantTest, MultiDexSecondaryOutOfDate) {
  std::string dex_location = GetScratchDir() + "/MultiDexSecondaryOutOfDate.jar";

  // Compile code for GetMultiDexSrc1.
  Copy(GetMultiDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  // Now overwrite the dex file with GetMultiDexSrc2 so the secondary checksum
  // is out of date.
  Copy(GetMultiDexSrc2(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed, false));
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a MultiDEX file and up-to-date OAT file for it with relative
// encoded dex locations.
// Expect: The oat file status is kNoDexOptNeeded.
TEST_F(OatFileAssistantTest, RelativeEncodedDexLocation) {
  std::string dex_location = GetScratchDir() + "/RelativeEncodedDexLocation.jar";
  std::string oat_location = GetOdexDir() + "/RelativeEncodedDexLocation.oat";

  // Create the dex file
  Copy(GetMultiDexSrc1(), dex_location);

  // Create the oat file with relative encoded dex location.
  std::vector<std::string> args;
  args.push_back("--dex-file=" + dex_location);
  args.push_back("--dex-location=" + std::string("RelativeEncodedDexLocation.jar"));
  args.push_back("--oat-file=" + oat_location);
  args.push_back("--compiler-filter=speed");

  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::Dex2Oat(args, &error_msg)) << error_msg;

  // Verify we can load both dex files.
  OatFileAssistant oat_file_assistant(dex_location.c_str(),
                                      oat_location.c_str(),
                                      kRuntimeISA, true);
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(2u, dex_files.size());
}

// Case: We have a DEX file and an OAT file out of date with respect to the
// dex checksum.
TEST_F(OatFileAssistantTest, OatDexOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatDexOutOfDate.jar";

  // We create a dex, generate an oat for it, then overwrite the dex with a
  // different dex to make the oat out of date.
  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);
  Copy(GetDexSrc2(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatDexOutOfDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and an OAT file out of date with respect to the
// boot image.
TEST_F(OatFileAssistantTest, OatImageOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatImageOutOfDate.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(),
                     CompilerFilter::kSpeed,
                     /*relocate*/true,
                     /*pic*/false,
                     /*with_patch_info*/false,
                     /*with_alternate_image*/true);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);
  EXPECT_EQ(OatFileAssistant::kDex2OatForBootImage,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kDex2OatForBootImage,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kDex2OatForBootImage,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatBootImageOutOfDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and a verify-at-runtime OAT file out of date with
// respect to the boot image.
// It shouldn't matter that the OAT file is out of date, because it is
// verify-at-runtime.
TEST_F(OatFileAssistantTest, OatVerifyAtRuntimeImageOutOfDate) {
  std::string dex_location = GetScratchDir() + "/OatVerifyAtRuntimeImageOutOfDate.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(),
                     CompilerFilter::kVerifyAtRuntime,
                     /*relocate*/true,
                     /*pic*/false,
                     /*with_patch_info*/false,
                     /*with_alternate_image*/true);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and an ODEX file, but no OAT file.
// Expect: The status is kPatchOatNeeded.
TEST_F(OatFileAssistantTest, DexOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/DexOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(-OatFileAssistant::kPatchoatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());

  // We should still be able to get the non-executable odex file to run from.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
}

// Case: We have a stripped DEX file and an ODEX file, but no OAT file.
// Expect: The status is kPatchOatNeeded
TEST_F(OatFileAssistantTest, StrippedDexOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/StrippedDexOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/StrippedDexOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Strip the dex file
  Copy(GetStrippedDexSrc1(), dex_location);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(-OatFileAssistant::kPatchoatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Make the oat file up to date.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Verify we can load the dex files from it.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a stripped DEX file, an ODEX file, and an out-of-date OAT file.
// Expect: The status is kPatchOatNeeded.
TEST_F(OatFileAssistantTest, StrippedDexOdexOat) {
  std::string dex_location = GetScratchDir() + "/StrippedDexOdexOat.jar";
  std::string odex_location = GetOdexDir() + "/StrippedDexOdexOat.odex";

  // Create the oat file from a different dex file so it looks out of date.
  Copy(GetDexSrc2(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  // Create the odex file
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Strip the dex file.
  Copy(GetStrippedDexSrc1(), dex_location);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(-OatFileAssistant::kPatchoatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,  // Can't run dex2oat because dex file is stripped.
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kEverything));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatDexOutOfDate, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Make the oat file up to date.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,  // Can't run dex2oat because dex file is stripped.
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kEverything));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Verify we can load the dex files from it.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a stripped (or resource-only) DEX file, no ODEX file and no
// OAT file. Expect: The status is kNoDexOptNeeded.
TEST_F(OatFileAssistantTest, ResourceOnlyDex) {
  std::string dex_location = GetScratchDir() + "/ResourceOnlyDex.jar";

  Copy(GetStrippedDexSrc1(), dex_location);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Make the oat file up to date. This should have no effect.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  EXPECT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file, no ODEX file and an OAT file that needs
// relocation.
// Expect: The status is kSelfPatchOatNeeded.
TEST_F(OatFileAssistantTest, SelfRelocation) {
  std::string dex_location = GetScratchDir() + "/SelfRelocation.jar";
  std::string oat_location = GetOdexDir() + "/SelfRelocation.oat";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, oat_location, CompilerFilter::kSpeed);

  OatFileAssistant oat_file_assistant(dex_location.c_str(),
      oat_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kPatchoatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kEverything));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());

  // Make the oat file up to date.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file, no ODEX file and an OAT file that needs
// relocation but doesn't have patch info.
// Expect: The status is kDex2OatNeeded, because we can't run patchoat.
TEST_F(OatFileAssistantTest, NoSelfRelocation) {
  std::string dex_location = GetScratchDir() + "/NoSelfRelocation.jar";
  std::string oat_location = GetOdexDir() + "/NoSelfRelocation.oat";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateNoPatchOdexForTest(dex_location, oat_location, CompilerFilter::kSpeed);

  OatFileAssistant oat_file_assistant(dex_location.c_str(),
      oat_location.c_str(), kRuntimeISA, true);

  EXPECT_EQ(OatFileAssistant::kDex2OatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  // Make the oat file up to date.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file, an ODEX file and an OAT file, where the ODEX and
// OAT files both have patch delta of 0.
// Expect: It shouldn't crash, and status is kSelfPatchOatNeeded.
TEST_F(OatFileAssistantTest, OdexOatOverlap) {
  std::string dex_location = GetScratchDir() + "/OdexOatOverlap.jar";
  std::string odex_location = GetOdexDir() + "/OdexOatOverlap.odex";
  std::string oat_location = GetOdexDir() + "/OdexOatOverlap.oat";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Create the oat file by copying the odex so they are located in the same
  // place in memory.
  Copy(odex_location, oat_location);

  // Verify things don't go bad.
  OatFileAssistant oat_file_assistant(dex_location.c_str(),
      oat_location.c_str(), kRuntimeISA, true);

  // kPatchoatForRelocation is expected rather than -kPatchoatForRelocation
  // based on the assumption that the oat location is more up-to-date than the odex
  // location, even if they both need relocation.
  EXPECT_EQ(OatFileAssistant::kPatchoatForRelocation,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatRelocationOutOfDate, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());

  // Things aren't relocated, so it should fall back to interpreted.
  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);

  EXPECT_FALSE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file and a PIC ODEX file, but no OAT file.
// Expect: The status is kNoDexOptNeeded, because PIC needs no relocation.
TEST_F(OatFileAssistantTest, DexPicOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexPicOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/DexPicOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GeneratePicOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(-OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kEverything));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and a VerifyAtRuntime ODEX file, but no OAT file.
// Expect: The status is kNoDexOptNeeded, because VerifyAtRuntime contains no code.
TEST_F(OatFileAssistantTest, DexVerifyAtRuntimeOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/DexVerifyAtRuntimeOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/DexVerifyAtRuntimeOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kVerifyAtRuntime);

  // Verify the status.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kVerifyAtRuntime));
  EXPECT_EQ(-OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatUpToDate, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_TRUE(oat_file_assistant.HasOriginalDexFiles());
}

// Case: We have a DEX file and up-to-date OAT file for it.
// Expect: We should load an executable dex file.
TEST_F(OatFileAssistantTest, LoadOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/LoadOatUpToDate.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  // Load the oat using an oat file assistant.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file and up-to-date interpret-only OAT file for it.
// Expect: We should still load the oat file as executable.
TEST_F(OatFileAssistantTest, LoadExecInterpretOnlyOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/LoadExecInterpretOnlyOatUpToDate.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kInterpretOnly);

  // Load the oat using an oat file assistant.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file and up-to-date OAT file for it.
// Expect: Loading non-executable should load the oat non-executable.
TEST_F(OatFileAssistantTest, LoadNoExecOatUpToDate) {
  std::string dex_location = GetScratchDir() + "/LoadNoExecOatUpToDate.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  // Load the oat using an oat file assistant.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_FALSE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a DEX file.
// Expect: We should load an executable dex file from an alternative oat
// location.
TEST_F(OatFileAssistantTest, LoadDexNoAlternateOat) {
  std::string dex_location = GetScratchDir() + "/LoadDexNoAlternateOat.jar";
  std::string oat_location = GetScratchDir() + "/LoadDexNoAlternateOat.oat";

  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(
      dex_location.c_str(), oat_location.c_str(), kRuntimeISA, true);
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_TRUE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());

  EXPECT_TRUE(OS::FileExists(oat_location.c_str()));

  // Verify it didn't create an oat in the default location.
  OatFileAssistant ofm(dex_location.c_str(), kRuntimeISA, false);
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, ofm.OatFileStatus());
}

// Case: We have a DEX file but can't write the oat file.
// Expect: We should fail to make the oat file up to date.
TEST_F(OatFileAssistantTest, LoadDexUnwriteableAlternateOat) {
  std::string dex_location = GetScratchDir() + "/LoadDexUnwriteableAlternateOat.jar";

  // Make the oat location unwritable by inserting some non-existent
  // intermediate directories.
  std::string oat_location = GetScratchDir() + "/foo/bar/LoadDexUnwriteableAlternateOat.oat";

  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(
      dex_location.c_str(), oat_location.c_str(), kRuntimeISA, true);
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  ASSERT_EQ(OatFileAssistant::kUpdateNotAttempted,
      oat_file_assistant.MakeUpToDate(false, &error_msg));

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() == nullptr);
}

// Case: We don't have a DEX file and can't write the oat file.
// Expect: We should fail to generate the oat file without crashing.
TEST_F(OatFileAssistantTest, GenNoDex) {
  std::string dex_location = GetScratchDir() + "/GenNoDex.jar";
  std::string oat_location = GetScratchDir() + "/GenNoDex.oat";

  OatFileAssistant oat_file_assistant(
      dex_location.c_str(), oat_location.c_str(), kRuntimeISA, true);
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  EXPECT_EQ(OatFileAssistant::kUpdateNotAttempted,
      oat_file_assistant.GenerateOatFile(&error_msg));
}

// Turn an absolute path into a path relative to the current working
// directory.
static std::string MakePathRelative(const std::string& target) {
  char buf[MAXPATHLEN];
  std::string cwd = getcwd(buf, MAXPATHLEN);

  // Split the target and cwd paths into components.
  std::vector<std::string> target_path;
  std::vector<std::string> cwd_path;
  Split(target, '/', &target_path);
  Split(cwd, '/', &cwd_path);

  // Reverse the path components, so we can use pop_back().
  std::reverse(target_path.begin(), target_path.end());
  std::reverse(cwd_path.begin(), cwd_path.end());

  // Drop the common prefix of the paths. Because we reversed the path
  // components, this becomes the common suffix of target_path and cwd_path.
  while (!target_path.empty() && !cwd_path.empty()
      && target_path.back() == cwd_path.back()) {
    target_path.pop_back();
    cwd_path.pop_back();
  }

  // For each element of the remaining cwd_path, add '..' to the beginning
  // of the target path. Because we reversed the path components, we add to
  // the end of target_path.
  for (unsigned int i = 0; i < cwd_path.size(); i++) {
    target_path.push_back("..");
  }

  // Reverse again to get the right path order, and join to get the result.
  std::reverse(target_path.begin(), target_path.end());
  return android::base::Join(target_path, '/');
}

// Case: Non-absolute path to Dex location.
// Expect: Not sure, but it shouldn't crash.
TEST_F(OatFileAssistantTest, NonAbsoluteDexLocation) {
  std::string abs_dex_location = GetScratchDir() + "/NonAbsoluteDexLocation.jar";
  Copy(GetDexSrc1(), abs_dex_location);

  std::string dex_location = MakePathRelative(abs_dex_location);
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
}

// Case: Very short, non-existent Dex location.
// Expect: kNoDexOptNeeded.
TEST_F(OatFileAssistantTest, ShortDexLocation) {
  std::string dex_location = "/xx";

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
  EXPECT_FALSE(oat_file_assistant.HasOriginalDexFiles());

  // Trying to make it up to date should have no effect.
  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  EXPECT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg));
  EXPECT_TRUE(error_msg.empty());
}

// Case: Non-standard extension for dex file.
// Expect: The status is kDex2OatNeeded.
TEST_F(OatFileAssistantTest, LongDexExtension) {
  std::string dex_location = GetScratchDir() + "/LongDexExtension.jarx";
  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  EXPECT_EQ(OatFileAssistant::kDex2OatFromScratch,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  EXPECT_FALSE(oat_file_assistant.IsInBootClassPath());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OdexFileStatus());
  EXPECT_EQ(OatFileAssistant::kOatCannotOpen, oat_file_assistant.OatFileStatus());
}

// A task to generate a dex location. Used by the RaceToGenerate test.
class RaceGenerateTask : public Task {
 public:
  explicit RaceGenerateTask(const std::string& dex_location, const std::string& oat_location)
    : dex_location_(dex_location), oat_location_(oat_location), loaded_oat_file_(nullptr)
  {}

  void Run(Thread* self ATTRIBUTE_UNUSED) {
    // Load the dex files, and save a pointer to the loaded oat file, so that
    // we can verify only one oat file was loaded for the dex location.
    std::vector<std::unique_ptr<const DexFile>> dex_files;
    std::vector<std::string> error_msgs;
    const OatFile* oat_file = nullptr;
    dex_files = Runtime::Current()->GetOatFileManager().OpenDexFilesFromOat(
        dex_location_.c_str(),
        oat_location_.c_str(),
        /*class_loader*/nullptr,
        /*dex_elements*/nullptr,
        &oat_file,
        &error_msgs);
    CHECK(!dex_files.empty()) << android::base::Join(error_msgs, '\n');
    CHECK(dex_files[0]->GetOatDexFile() != nullptr) << dex_files[0]->GetLocation();
    loaded_oat_file_ = dex_files[0]->GetOatDexFile()->GetOatFile();
    CHECK_EQ(loaded_oat_file_, oat_file);
  }

  const OatFile* GetLoadedOatFile() const {
    return loaded_oat_file_;
  }

 private:
  std::string dex_location_;
  std::string oat_location_;
  const OatFile* loaded_oat_file_;
};

// Test the case where multiple processes race to generate an oat file.
// This simulates multiple processes using multiple threads.
//
// We want unique Oat files to be loaded even when there is a race to load.
// TODO: The test case no longer tests locking the way it was intended since we now get multiple
// copies of the same Oat files mapped at different locations.
TEST_F(OatFileAssistantTest, RaceToGenerate) {
  std::string dex_location = GetScratchDir() + "/RaceToGenerate.jar";
  std::string oat_location = GetOdexDir() + "/RaceToGenerate.oat";

  // We use the lib core dex file, because it's large, and hopefully should
  // take a while to generate.
  Copy(GetLibCoreDexFileNames()[0], dex_location);

  const int kNumThreads = 32;
  Thread* self = Thread::Current();
  ThreadPool thread_pool("Oat file assistant test thread pool", kNumThreads);
  std::vector<std::unique_ptr<RaceGenerateTask>> tasks;
  for (int i = 0; i < kNumThreads; i++) {
    std::unique_ptr<RaceGenerateTask> task(new RaceGenerateTask(dex_location, oat_location));
    thread_pool.AddTask(self, task.get());
    tasks.push_back(std::move(task));
  }
  thread_pool.StartWorkers(self);
  thread_pool.Wait(self, true, false);

  // Verify every task got a unique oat file.
  std::set<const OatFile*> oat_files;
  for (auto& task : tasks) {
    const OatFile* oat_file = task->GetLoadedOatFile();
    EXPECT_TRUE(oat_files.find(oat_file) == oat_files.end());
    oat_files.insert(oat_file);
  }
}

// Case: We have a DEX file and an ODEX file, no OAT file, and dex2oat is
// disabled.
// Expect: We should load the odex file non-executable.
TEST_F(OatFileAssistantNoDex2OatTest, LoadDexOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/LoadDexOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/LoadDexOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Load the oat using an executable oat file assistant.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_FALSE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(1u, dex_files.size());
}

// Case: We have a MultiDEX file and an ODEX file, no OAT file, and dex2oat is
// disabled.
// Expect: We should load the odex file non-executable.
TEST_F(OatFileAssistantNoDex2OatTest, LoadMultiDexOdexNoOat) {
  std::string dex_location = GetScratchDir() + "/LoadMultiDexOdexNoOat.jar";
  std::string odex_location = GetOdexDir() + "/LoadMultiDexOdexNoOat.odex";

  // Create the dex and odex files
  Copy(GetMultiDexSrc1(), dex_location);
  GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed);

  // Load the oat using an executable oat file assistant.
  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, true);

  std::unique_ptr<OatFile> oat_file = oat_file_assistant.GetBestOatFile();
  ASSERT_TRUE(oat_file.get() != nullptr);
  EXPECT_FALSE(oat_file->IsExecutable());
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  dex_files = oat_file_assistant.LoadDexFiles(*oat_file, dex_location.c_str());
  EXPECT_EQ(2u, dex_files.size());
}

TEST_F(OatFileAssistantTest, RuntimeCompilerFilterOptionUsed) {
  std::string dex_location = GetScratchDir() + "/RuntimeCompilerFilterOptionUsed.jar";
  Copy(GetDexSrc1(), dex_location);

  OatFileAssistant oat_file_assistant(dex_location.c_str(), kRuntimeISA, false);

  std::string error_msg;
  Runtime::Current()->AddCompilerOption("--compiler-filter=interpret-only");
  EXPECT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kDex2OatForFilter,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  Runtime::Current()->AddCompilerOption("--compiler-filter=speed");
  EXPECT_EQ(OatFileAssistant::kUpdateSucceeded,
      oat_file_assistant.MakeUpToDate(false, &error_msg)) << error_msg;
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kInterpretOnly));
  EXPECT_EQ(OatFileAssistant::kNoDexOptNeeded,
      oat_file_assistant.GetDexOptNeeded(CompilerFilter::kSpeed));

  Runtime::Current()->AddCompilerOption("--compiler-filter=bogus");
  EXPECT_EQ(OatFileAssistant::kUpdateNotAttempted,
      oat_file_assistant.MakeUpToDate(false, &error_msg));
}

TEST(OatFileAssistantUtilsTest, DexLocationToOdexFilename) {
  std::string error_msg;
  std::string odex_file;

  EXPECT_TRUE(OatFileAssistant::DexLocationToOdexFilename(
        "/foo/bar/baz.jar", kArm, &odex_file, &error_msg)) << error_msg;
  EXPECT_EQ("/foo/bar/oat/arm/baz.odex", odex_file);

  EXPECT_TRUE(OatFileAssistant::DexLocationToOdexFilename(
        "/foo/bar/baz.funnyext", kArm, &odex_file, &error_msg)) << error_msg;
  EXPECT_EQ("/foo/bar/oat/arm/baz.odex", odex_file);

  EXPECT_FALSE(OatFileAssistant::DexLocationToOdexFilename(
        "nopath.jar", kArm, &odex_file, &error_msg));
  EXPECT_FALSE(OatFileAssistant::DexLocationToOdexFilename(
        "/foo/bar/baz_noext", kArm, &odex_file, &error_msg));
}

// Verify the dexopt status values from dalvik.system.DexFile
// match the OatFileAssistant::DexOptStatus values.
TEST_F(OatFileAssistantTest, DexOptStatusValues) {
  std::pair<OatFileAssistant::DexOptNeeded, const char*> mapping[] = {
    {OatFileAssistant::kNoDexOptNeeded, "NO_DEXOPT_NEEDED"},
    {OatFileAssistant::kDex2OatFromScratch, "DEX2OAT_FROM_SCRATCH"},
    {OatFileAssistant::kDex2OatForBootImage, "DEX2OAT_FOR_BOOT_IMAGE"},
    {OatFileAssistant::kDex2OatForFilter, "DEX2OAT_FOR_FILTER"},
    {OatFileAssistant::kDex2OatForRelocation, "DEX2OAT_FOR_RELOCATION"},
    {OatFileAssistant::kPatchoatForRelocation, "PATCHOAT_FOR_RELOCATION"}
  };

  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<1> hs(soa.Self());
  ClassLinker* linker = Runtime::Current()->GetClassLinker();
  Handle<mirror::Class> dexfile(
      hs.NewHandle(linker->FindSystemClass(soa.Self(), "Ldalvik/system/DexFile;")));
  ASSERT_FALSE(dexfile.Get() == nullptr);
  linker->EnsureInitialized(soa.Self(), dexfile, true, true);

  for (std::pair<OatFileAssistant::DexOptNeeded, const char*> field : mapping) {
    ArtField* art_field = mirror::Class::FindStaticField(
        soa.Self(), dexfile.Get(), field.second, "I");
    ASSERT_FALSE(art_field == nullptr);
    EXPECT_EQ(art_field->GetTypeAsPrimitiveType(), Primitive::kPrimInt);
    EXPECT_EQ(field.first, art_field->GetInt(dexfile.Get()));
  }
}

// TODO: More Tests:
//  * Test class linker falls back to unquickened dex for DexNoOat
//  * Test class linker falls back to unquickened dex for MultiDexNoOat
//  * Test using secondary isa
//  * Test for status of oat while oat is being generated (how?)
//  * Test case where 32 and 64 bit boot class paths differ,
//      and we ask IsInBootClassPath for a class in exactly one of the 32 or
//      64 bit boot class paths.
//  * Test unexpected scenarios (?):
//    - Dex is stripped, don't have odex.
//    - Oat file corrupted after status check, before reload unexecutable
//    because it's unrelocated and no dex2oat
//  * Test unrelocated specific target compilation type can be relocated to
//    make it up to date.
}  // namespace art
