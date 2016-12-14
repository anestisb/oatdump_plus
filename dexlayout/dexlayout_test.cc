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

#include <string>
#include <vector>
#include <sstream>

#include <sys/types.h>
#include <unistd.h>

#include "base/unix_file/fd_file.h"
#include "common_runtime_test.h"
#include "utils.h"

namespace art {

static const char kDexFileLayoutInputDex[] =
    "ZGV4CjAzNQD1KW3+B8NAB0f2A/ZVIBJ0aHrGIqcpVTAUAgAAcAAAAHhWNBIAAAAAAAAAAIwBAAAH"
    "AAAAcAAAAAQAAACMAAAAAQAAAJwAAAAAAAAAAAAAAAMAAACoAAAAAgAAAMAAAAAUAQAAAAEAADAB"
    "AAA4AQAAQAEAAEgBAABNAQAAUgEAAGYBAAADAAAABAAAAAUAAAAGAAAABgAAAAMAAAAAAAAAAAAA"
    "AAAAAAABAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAACAAAAAAAAAAEAAAAAAAAAdQEAAAAAAAABAAAA"
    "AAAAAAIAAAAAAAAAAgAAAAAAAAB/AQAAAAAAAAEAAQABAAAAaQEAAAQAAABwEAIAAAAOAAEAAQAB"
    "AAAAbwEAAAQAAABwEAIAAAAOAAY8aW5pdD4ABkEuamF2YQAGQi5qYXZhAANMQTsAA0xCOwASTGph"
    "dmEvbGFuZy9PYmplY3Q7AAFWAAQABw48AAQABw48AAAAAQAAgIAEgAIAAAEAAYCABJgCAAAACwAA"
    "AAAAAAABAAAAAAAAAAEAAAAHAAAAcAAAAAIAAAAEAAAAjAAAAAMAAAABAAAAnAAAAAUAAAADAAAA"
    "qAAAAAYAAAACAAAAwAAAAAEgAAACAAAAAAEAAAIgAAAHAAAAMAEAAAMgAAACAAAAaQEAAAAgAAAC"
    "AAAAdQEAAAAQAAABAAAAjAEAAA==";

static const char kDexFileLayoutInputProfile[] =
    "cHJvADAwMgABAAsAAAABAPUpbf5jbGFzc2VzLmRleAEA";

static const char kDexFileLayoutExpectedOutputDex[] =
    "ZGV4CjAzNQD1KW3+B8NAB0f2A/ZVIBJ0aHrGIqcpVTAUAgAAcAAAAHhWNBIAAAAAAAAAAIwBAAAH"
    "AAAAcAAAAAQAAACMAAAAAQAAAJwAAAAAAAAAAAAAAAMAAACoAAAAAgAAAMAAAAAUAQAAAAEAADAB"
    "AAA4AQAAQAEAAEgBAABNAQAAUgEAAGYBAAADAAAABAAAAAUAAAAGAAAABgAAAAMAAAAAAAAAAAAA"
    "AAAAAAABAAAAAAAAAAIAAAAAAAAAAQAAAAAAAAACAAAAAAAAAAIAAAAAAAAAdQEAAAAAAAAAAAAA"
    "AAAAAAIAAAAAAAAAAQAAAAAAAAB/AQAAAAAAAAEAAQABAAAAbwEAAAQAAABwEAIAAAAOAAEAAQAB"
    "AAAAaQEAAAQAAABwEAIAAAAOAAY8aW5pdD4ABkEuamF2YQAGQi5qYXZhAANMQTsAA0xCOwASTGph"
    "dmEvbGFuZy9PYmplY3Q7AAFWAAQABw48AAQABw48AAAAAQABgIAEgAIAAAEAAICABJgCAAAACwAA"
    "AAAAAAABAAAAAAAAAAEAAAAHAAAAcAAAAAIAAAAEAAAAjAAAAAMAAAABAAAAnAAAAAUAAAADAAAA"
    "qAAAAAYAAAACAAAAwAAAAAEgAAACAAAAAAEAAAIgAAAHAAAAMAEAAAMgAAACAAAAaQEAAAAgAAAC"
    "AAAAdQEAAAAQAAABAAAAjAEAAA==";

static void WriteFileBase64(const char* base64, const char* location) {
  // Decode base64.
  CHECK(base64 != nullptr);
  size_t length;
  std::unique_ptr<uint8_t[]> bytes(DecodeBase64(base64, &length));
  CHECK(bytes.get() != nullptr);

  // Write to provided file.
  std::unique_ptr<File> file(OS::CreateEmptyFile(location));
  CHECK(file.get() != nullptr);
  if (!file->WriteFully(bytes.get(), length)) {
    PLOG(FATAL) << "Failed to write base64 as file";
  }
  if (file->FlushCloseOrErase() != 0) {
    PLOG(FATAL) << "Could not flush and close test file.";
  }
}

class DexLayoutTest : public CommonRuntimeTest {
 protected:
  virtual void SetUp() {
    CommonRuntimeTest::SetUp();
  }

  // Runs FullPlainOutput test.
  bool FullPlainOutputExec(std::string* error_msg) {
    // TODO: dexdump2 -> dexdump ?
    ScratchFile dexdump_output;
    const std::string& dexdump_filename = dexdump_output.GetFilename();
    std::string dexdump = GetTestAndroidRoot() + "/bin/dexdump2";
    EXPECT_TRUE(OS::FileExists(dexdump.c_str())) << dexdump << " should be a valid file path";

    ScratchFile dexlayout_output;
    const std::string& dexlayout_filename = dexlayout_output.GetFilename();
    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    for (const std::string &dex_file : GetLibCoreDexFileNames()) {
      std::vector<std::string> dexdump_exec_argv =
          { dexdump, "-d", "-f", "-h", "-l", "plain", "-o", dexdump_filename, dex_file };
      std::vector<std::string> dexlayout_exec_argv =
          { dexlayout, "-d", "-f", "-h", "-l", "plain", "-o", dexlayout_filename, dex_file };
      if (!::art::Exec(dexdump_exec_argv, error_msg)) {
        return false;
      }
      if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> diff_exec_argv =
          { "/usr/bin/diff", dexdump_filename, dexlayout_filename };
      if (!::art::Exec(diff_exec_argv, error_msg)) {
        return false;
      }
    }
    return true;
  }

  // Runs DexFileOutput test.
  bool DexFileOutputExec(std::string* error_msg) {
    ScratchFile tmp_file;
    const std::string& tmp_name = tmp_file.GetFilename();
    size_t tmp_last_slash = tmp_name.rfind('/');
    std::string tmp_dir = tmp_name.substr(0, tmp_last_slash + 1);
    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    for (const std::string &dex_file : GetLibCoreDexFileNames()) {
      std::vector<std::string> dexlayout_exec_argv =
          { dexlayout, "-w", tmp_dir, "-o", tmp_name, dex_file };
      if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
        return false;
      }
      size_t dex_file_last_slash = dex_file.rfind("/");
      std::string dex_file_name = dex_file.substr(dex_file_last_slash + 1);
      std::vector<std::string> unzip_exec_argv =
          { "/usr/bin/unzip", dex_file, "classes.dex", "-d", tmp_dir};
      if (!::art::Exec(unzip_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> diff_exec_argv =
          { "/usr/bin/diff", tmp_dir + "classes.dex" , tmp_dir + dex_file_name };
      if (!::art::Exec(diff_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> rm_zip_exec_argv = { "/bin/rm", tmp_dir + "classes.dex" };
      if (!::art::Exec(rm_zip_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> rm_out_exec_argv = { "/bin/rm", tmp_dir + dex_file_name };
      if (!::art::Exec(rm_out_exec_argv, error_msg)) {
        return false;
      }
    }
    return true;
  }

  // Runs DexFileOutput test.
  bool DexFileLayoutExec(std::string* error_msg) {
    ScratchFile tmp_file;
    std::string tmp_name = tmp_file.GetFilename();
    size_t tmp_last_slash = tmp_name.rfind("/");
    std::string tmp_dir = tmp_name.substr(0, tmp_last_slash + 1);

    // Write inputs and expected outputs.
    std::string dex_file = tmp_dir + "classes.dex";
    WriteFileBase64(kDexFileLayoutInputDex, dex_file.c_str());
    std::string profile_file = tmp_dir + "primary.prof";
    WriteFileBase64(kDexFileLayoutInputProfile, profile_file.c_str());
    std::string expected_output = tmp_dir + "expected.dex";
    WriteFileBase64(kDexFileLayoutExpectedOutputDex, expected_output.c_str());
    std::string output_dex = tmp_dir + "classes.dex.new";

    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    std::vector<std::string> dexlayout_exec_argv =
    { dexlayout, "-w", tmp_dir, "-o", tmp_name, "-p", profile_file, dex_file };
    if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
      return false;
    }
    std::vector<std::string> diff_exec_argv =
        { "/usr/bin/diff", expected_output, output_dex };
    if (!::art::Exec(diff_exec_argv, error_msg)) {
      return false;
    }

    std::vector<std::string> rm_exec_argv =
        { "/bin/rm", dex_file, profile_file, expected_output, output_dex };
    if (!::art::Exec(rm_exec_argv, error_msg)) {
      return false;
    }
    return true;
  }
};


TEST_F(DexLayoutTest, FullPlainOutput) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(FullPlainOutputExec(&error_msg)) << error_msg;
}

TEST_F(DexLayoutTest, DexFileOutput) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(DexFileOutputExec(&error_msg)) << error_msg;
}

TEST_F(DexLayoutTest, DexFileLayout) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(DexFileLayoutExec(&error_msg)) << error_msg;
}

}  // namespace art
