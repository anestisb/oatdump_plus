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

#include <sstream>
#include <string>
#include <vector>

#include "android-base/strings.h"

#include "common_runtime_test.h"

#include "base/unix_file/fd_file.h"
#include "runtime/arch/instruction_set.h"
#include "runtime/exec_utils.h"
#include "runtime/gc/heap.h"
#include "runtime/gc/space/image_space.h"
#include "runtime/os.h"
#include "runtime/utils.h"
#include "utils.h"

#include <sys/types.h>
#include <unistd.h>

namespace art {

class OatDumpTest : public CommonRuntimeTest {
 protected:
  virtual void SetUp() {
    CommonRuntimeTest::SetUp();
    core_art_location_ = GetCoreArtLocation();
    core_oat_location_ = GetSystemImageFilename(GetCoreOatLocation().c_str(), kRuntimeISA);
  }

  // Linking flavor.
  enum Flavor {
    kDynamic,  // oatdump(d)
    kStatic,   // oatdump(d)s
  };

  // Returns path to the oatdump binary.
  std::string GetOatDumpFilePath(Flavor flavor) {
    std::string root = GetTestAndroidRoot();
    root += "/bin/oatdump";
    if (kIsDebugBuild) {
      root += "d";
    }
    if (flavor == kStatic) {
      root += "s";
    }
    return root;
  }

  enum Mode {
    kModeOat,
    kModeArt,
    kModeSymbolize,
  };

  // Display style.
  enum Display {
    kListOnly,
    kListAndCode
  };

  // Run the test with custom arguments.
  bool Exec(Flavor flavor,
            Mode mode,
            const std::vector<std::string>& args,
            Display display,
            std::string* error_msg) {
    std::string file_path = GetOatDumpFilePath(flavor);

    EXPECT_TRUE(OS::FileExists(file_path.c_str())) << file_path << " should be a valid file path";

    // ScratchFile scratch;
    std::vector<std::string> exec_argv = { file_path };
    std::vector<std::string> expected_prefixes;
    if (mode == kModeSymbolize) {
      exec_argv.push_back("--symbolize=" + core_oat_location_);
      exec_argv.push_back("--output=" + core_oat_location_ + ".symbolize");
    } else {
      expected_prefixes.push_back("Dex file data for");
      expected_prefixes.push_back("Num string ids:");
      expected_prefixes.push_back("Num field ids:");
      expected_prefixes.push_back("Num method ids:");
      expected_prefixes.push_back("LOCATION:");
      expected_prefixes.push_back("MAGIC:");
      expected_prefixes.push_back("DEX FILE COUNT:");
      if (display == kListAndCode) {
        // Code and dex code do not show up if list only.
        expected_prefixes.push_back("DEX CODE:");
        expected_prefixes.push_back("CODE:");
        expected_prefixes.push_back("CodeInfoEncoding");
        expected_prefixes.push_back("CodeInfoInlineInfo");
      }
      if (mode == kModeArt) {
        exec_argv.push_back("--image=" + core_art_location_);
        exec_argv.push_back("--instruction-set=" + std::string(
            GetInstructionSetString(kRuntimeISA)));
        expected_prefixes.push_back("IMAGE LOCATION:");
        expected_prefixes.push_back("IMAGE BEGIN:");
        expected_prefixes.push_back("kDexCaches:");
      } else {
        CHECK_EQ(static_cast<size_t>(mode), static_cast<size_t>(kModeOat));
        exec_argv.push_back("--oat-file=" + core_oat_location_);
      }
    }
    exec_argv.insert(exec_argv.end(), args.begin(), args.end());

    bool result = true;
    // We must set --android-root.
    int link[2];
    if (pipe(link) == -1) {
      *error_msg = strerror(errno);
      return false;
    }

    const pid_t pid = fork();
    if (pid == -1) {
      *error_msg = strerror(errno);
      return false;
    }

    if (pid == 0) {
      dup2(link[1], STDOUT_FILENO);
      close(link[0]);
      close(link[1]);
      // change process groups, so we don't get reaped by ProcessManager
      setpgid(0, 0);
      // Use execv here rather than art::Exec to avoid blocking on waitpid here.
      std::vector<char*> argv;
      for (size_t i = 0; i < exec_argv.size(); ++i) {
        argv.push_back(const_cast<char*>(exec_argv[i].c_str()));
      }
      argv.push_back(nullptr);
      UNUSED(execv(argv[0], &argv[0]));
      const std::string command_line(android::base::Join(exec_argv, ' '));
      PLOG(ERROR) << "Failed to execv(" << command_line << ")";
      // _exit to avoid atexit handlers in child.
      _exit(1);
    } else {
      close(link[1]);
      static const size_t kLineMax = 256;
      char line[kLineMax] = {};
      size_t line_len = 0;
      size_t total = 0;
      std::vector<bool> found(expected_prefixes.size(), false);
      while (true) {
        while (true) {
          size_t spaces = 0;
          // Trim spaces at the start of the line.
          for (; spaces < line_len && isspace(line[spaces]); ++spaces) {}
          if (spaces > 0) {
            line_len -= spaces;
            memmove(&line[0], &line[spaces], line_len);
          }
          ssize_t bytes_read =
              TEMP_FAILURE_RETRY(read(link[0], &line[line_len], kLineMax - line_len));
          if (bytes_read <= 0) {
            break;
          }
          line_len += bytes_read;
          total += bytes_read;
        }
        if (line_len == 0) {
          break;
        }
        // Check contents.
        for (size_t i = 0; i < expected_prefixes.size(); ++i) {
          const std::string& expected = expected_prefixes[i];
          if (!found[i] &&
              line_len >= expected.length() &&
              memcmp(line, expected.c_str(), expected.length()) == 0) {
            found[i] = true;
          }
        }
        // Skip to next line.
        size_t next_line = 0;
        for (; next_line + 1 < line_len && line[next_line] != '\n'; ++next_line) {}
        line_len -= next_line + 1;
        memmove(&line[0], &line[next_line + 1], line_len);
      }
      if (mode == kModeSymbolize) {
        EXPECT_EQ(total, 0u);
      } else {
        EXPECT_GT(total, 0u);
      }
      LOG(INFO) << "Processed bytes " << total;
      close(link[0]);
      int status = 0;
      if (waitpid(pid, &status, 0) != -1) {
        result = (status == 0);
      }

      for (size_t i = 0; i < expected_prefixes.size(); ++i) {
        if (!found[i]) {
          LOG(ERROR) << "Did not find prefix " << expected_prefixes[i];
          result = false;
        }
      }
    }

    return result;
  }

 private:
  std::string core_art_location_;
  std::string core_oat_location_;
};

// Disable tests on arm and mips as they are taking too long to run. b/27824283.
#if !defined(__arm__) && !defined(__mips__)
TEST_F(OatDumpTest, TestImage) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeArt, {}, kListAndCode, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestImageStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeArt, {}, kListAndCode, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestOatImage) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeOat, {}, kListAndCode, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestOatImageStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeOat, {}, kListAndCode, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestNoDumpVmap) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeArt, {"--no-dump:vmap"}, kListAndCode, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestNoDumpVmapStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeArt, {"--no-dump:vmap"}, kListAndCode, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestNoDisassemble) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeArt, {"--no-disassemble"}, kListAndCode, &error_msg))
      << error_msg;
}
TEST_F(OatDumpTest, TestNoDisassembleStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeArt, {"--no-disassemble"}, kListAndCode, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestListClasses) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeArt, {"--list-classes"}, kListOnly, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestListClassesStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeArt, {"--list-classes"}, kListOnly, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestListMethods) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeArt, {"--list-methods"}, kListOnly, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestListMethodsStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeArt, {"--list-methods"}, kListOnly, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestSymbolize) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kDynamic, kModeSymbolize, {}, kListOnly, &error_msg)) << error_msg;
}
TEST_F(OatDumpTest, TestSymbolizeStatic) {
  TEST_DISABLED_FOR_NON_STATIC_HOST_BUILDS();
  std::string error_msg;
  ASSERT_TRUE(Exec(kStatic, kModeSymbolize, {}, kListOnly, &error_msg)) << error_msg;
}
#endif
}  // namespace art
