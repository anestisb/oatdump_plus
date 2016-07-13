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

#include "common_runtime_test.h"

#include "base/stringprintf.h"
#include "base/unix_file/fd_file.h"
#include "runtime/arch/instruction_set.h"
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

  // Returns path to the oatdump binary.
  std::string GetOatDumpFilePath() {
    std::string root = GetTestAndroidRoot();
    root += "/bin/oatdump";
    if (kIsDebugBuild) {
      root += "d";
    }
    return root;
  }

  enum Mode {
    kModeOat,
    kModeArt,
    kModeSymbolize,
  };

  // Run the test with custom arguments.
  bool Exec(Mode mode,
            const std::vector<std::string>& args,
            bool list_only,
            std::string* error_msg) {
    std::string file_path = GetOatDumpFilePath();

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
      if (!list_only) {
        // Code and dex code do not show up if list only.
        expected_prefixes.push_back("DEX CODE:");
        expected_prefixes.push_back("CODE:");
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
      return false;
    }

    const pid_t pid = fork();
    if (pid == -1) {
      return false;
    }

    if (pid == 0) {
      dup2(link[1], STDOUT_FILENO);
      close(link[0]);
      close(link[1]);
      exit(::art::Exec(exec_argv, error_msg) ? 0 : 1);
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
  ASSERT_TRUE(Exec(kModeArt, {}, /*list_only*/ false, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestOatImage) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeOat, {}, /*list_only*/ false, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestNoDumpVmap) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeArt, {"--no-dump:vmap"}, /*list_only*/ false, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestNoDisassemble) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeArt, {"--no-disassemble"}, /*list_only*/ false, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestListClasses) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeArt, {"--list-classes"}, /*list_only*/ true, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestListMethods) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeArt, {"--list-methods"}, /*list_only*/ true, &error_msg)) << error_msg;
}

TEST_F(OatDumpTest, TestSymbolize) {
  std::string error_msg;
  ASSERT_TRUE(Exec(kModeSymbolize, {}, /*list_only*/ true, &error_msg)) << error_msg;
}
#endif
}  // namespace art
