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

#include <memory>

#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "common_runtime_test.h"
#include "dex_file-inl.h"
#include "mem_map.h"
#include "os.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "utils.h"

namespace art {

class DexFileTest : public CommonRuntimeTest {};

TEST_F(DexFileTest, Open) {
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<const DexFile> dex(OpenTestDexFile("Nested"));
  ASSERT_TRUE(dex.get() != nullptr);
}

static inline std::vector<uint8_t> DecodeBase64Vec(const char* src) {
  std::vector<uint8_t> res;
  size_t size;
  std::unique_ptr<uint8_t[]> data(DecodeBase64(src, &size));
  res.resize(size);
  memcpy(res.data(), data.get(), size);
  return res;
}

// Although this is the same content logically as the Nested test dex,
// the DexFileHeader test is sensitive to subtle changes in the
// contents due to the checksum etc, so we embed the exact input here.
//
// class Nested {
//     class Inner {
//     }
// }
static const char kRawDex[] =
  "ZGV4CjAzNQAQedgAe7gM1B/WHsWJ6L7lGAISGC7yjD2IAwAAcAAAAHhWNBIAAAAAAAAAAMQCAAAP"
  "AAAAcAAAAAcAAACsAAAAAgAAAMgAAAABAAAA4AAAAAMAAADoAAAAAgAAAAABAABIAgAAQAEAAK4B"
  "AAC2AQAAvQEAAM0BAADXAQAA+wEAABsCAAA+AgAAUgIAAF8CAABiAgAAZgIAAHMCAAB5AgAAgQIA"
  "AAIAAAADAAAABAAAAAUAAAAGAAAABwAAAAkAAAAJAAAABgAAAAAAAAAKAAAABgAAAKgBAAAAAAEA"
  "DQAAAAAAAQAAAAAAAQAAAAAAAAAFAAAAAAAAAAAAAAAAAAAABQAAAAAAAAAIAAAAiAEAAKsCAAAA"
  "AAAAAQAAAAAAAAAFAAAAAAAAAAgAAACYAQAAuAIAAAAAAAACAAAAlAIAAJoCAAABAAAAowIAAAIA"
  "AgABAAAAiAIAAAYAAABbAQAAcBACAAAADgABAAEAAQAAAI4CAAAEAAAAcBACAAAADgBAAQAAAAAA"
  "AAAAAAAAAAAATAEAAAAAAAAAAAAAAAAAAAEAAAABAAY8aW5pdD4ABUlubmVyAA5MTmVzdGVkJElu"
  "bmVyOwAITE5lc3RlZDsAIkxkYWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2"
  "aWsvYW5ub3RhdGlvbi9Jbm5lckNsYXNzOwAhTGRhbHZpay9hbm5vdGF0aW9uL01lbWJlckNsYXNz"
  "ZXM7ABJMamF2YS9sYW5nL09iamVjdDsAC05lc3RlZC5qYXZhAAFWAAJWTAALYWNjZXNzRmxhZ3MA"
  "BG5hbWUABnRoaXMkMAAFdmFsdWUAAgEABw4AAQAHDjwAAgIBDhgBAgMCCwQADBcBAgQBDhwBGAAA"
  "AQEAAJAgAICABNQCAAABAAGAgATwAgAAEAAAAAAAAAABAAAAAAAAAAEAAAAPAAAAcAAAAAIAAAAH"
  "AAAArAAAAAMAAAACAAAAyAAAAAQAAAABAAAA4AAAAAUAAAADAAAA6AAAAAYAAAACAAAAAAEAAAMQ"
  "AAACAAAAQAEAAAEgAAACAAAAVAEAAAYgAAACAAAAiAEAAAEQAAABAAAAqAEAAAIgAAAPAAAArgEA"
  "AAMgAAACAAAAiAIAAAQgAAADAAAAlAIAAAAgAAACAAAAqwIAAAAQAAABAAAAxAIAAA==";

// kRawDex38 and 39 are dex'ed versions of the following Java source :
//
// public class Main {
//     public static void main(String[] foo) {
//     }
// }
//
// The dex file was manually edited to change its dex version code to 38
// or 39, respectively.
static const char kRawDex38[] =
  "ZGV4CjAzOAC4OovJlJ1089ikzK6asMf/f8qp3Kve5VsgAgAAcAAAAHhWNBIAAAAAAAAAAIwBAAAI"
  "AAAAcAAAAAQAAACQAAAAAgAAAKAAAAAAAAAAAAAAAAMAAAC4AAAAAQAAANAAAAAwAQAA8AAAACIB"
  "AAAqAQAAMgEAAEYBAABRAQAAVAEAAFgBAABtAQAAAQAAAAIAAAAEAAAABgAAAAQAAAACAAAAAAAA"
  "AAUAAAACAAAAHAEAAAAAAAAAAAAAAAABAAcAAAABAAAAAAAAAAAAAAABAAAAAQAAAAAAAAADAAAA"
  "AAAAAH4BAAAAAAAAAQABAAEAAABzAQAABAAAAHAQAgAAAA4AAQABAAAAAAB4AQAAAQAAAA4AAAAB"
  "AAAAAwAGPGluaXQ+AAZMTWFpbjsAEkxqYXZhL2xhbmcvT2JqZWN0OwAJTWFpbi5qYXZhAAFWAAJW"
  "TAATW0xqYXZhL2xhbmcvU3RyaW5nOwAEbWFpbgABAAcOAAMBAAcOAAAAAgAAgYAE8AEBCYgCDAAA"
  "AAAAAAABAAAAAAAAAAEAAAAIAAAAcAAAAAIAAAAEAAAAkAAAAAMAAAACAAAAoAAAAAUAAAADAAAA"
  "uAAAAAYAAAABAAAA0AAAAAEgAAACAAAA8AAAAAEQAAABAAAAHAEAAAIgAAAIAAAAIgEAAAMgAAAC"
  "AAAAcwEAAAAgAAABAAAAfgEAAAAQAAABAAAAjAEAAA==";

static const char kRawDex39[] =
  "ZGV4CjAzOQC4OovJlJ1089ikzK6asMf/f8qp3Kve5VsgAgAAcAAAAHhWNBIAAAAAAAAAAIwBAAAI"
  "AAAAcAAAAAQAAACQAAAAAgAAAKAAAAAAAAAAAAAAAAMAAAC4AAAAAQAAANAAAAAwAQAA8AAAACIB"
  "AAAqAQAAMgEAAEYBAABRAQAAVAEAAFgBAABtAQAAAQAAAAIAAAAEAAAABgAAAAQAAAACAAAAAAAA"
  "AAUAAAACAAAAHAEAAAAAAAAAAAAAAAABAAcAAAABAAAAAAAAAAAAAAABAAAAAQAAAAAAAAADAAAA"
  "AAAAAH4BAAAAAAAAAQABAAEAAABzAQAABAAAAHAQAgAAAA4AAQABAAAAAAB4AQAAAQAAAA4AAAAB"
  "AAAAAwAGPGluaXQ+AAZMTWFpbjsAEkxqYXZhL2xhbmcvT2JqZWN0OwAJTWFpbi5qYXZhAAFWAAJW"
  "TAATW0xqYXZhL2xhbmcvU3RyaW5nOwAEbWFpbgABAAcOAAMBAAcOAAAAAgAAgYAE8AEBCYgCDAAA"
  "AAAAAAABAAAAAAAAAAEAAAAIAAAAcAAAAAIAAAAEAAAAkAAAAAMAAAACAAAAoAAAAAUAAAADAAAA"
  "uAAAAAYAAAABAAAA0AAAAAEgAAACAAAA8AAAAAEQAAABAAAAHAEAAAIgAAAIAAAAIgEAAAMgAAAC"
  "AAAAcwEAAAAgAAABAAAAfgEAAAAQAAABAAAAjAEAAA==";

static const char kRawDexZeroLength[] =
  "UEsDBAoAAAAAAOhxAkkAAAAAAAAAAAAAAAALABwAY2xhc3Nlcy5kZXhVVAkAA2QNoVdnDaFXdXgL"
  "AAEE5AMBAASIEwAAUEsBAh4DCgAAAAAA6HECSQAAAAAAAAAAAAAAAAsAGAAAAAAAAAAAAKCBAAAA"
  "AGNsYXNzZXMuZGV4VVQFAANkDaFXdXgLAAEE5AMBAASIEwAAUEsFBgAAAAABAAEAUQAAAEUAAAAA"
  "AA==";

static const char kRawZipClassesDexPresent[] =
  "UEsDBBQAAAAIANVRN0ms99lIMQEAACACAAALABwAY2xhc3Nlcy5kZXhVVAkAAwFj5VcUY+VXdXgL"
  "AAEE5AMBAASIEwAAS0mt4DIwtmDYYdV9csrcks83lpxZN2vD8f/1p1beWX3vabQCEwNDAQMDQ0WY"
  "iRADFPQwMjBwMEDEWYB4AhADlTEsYEAAZiDeAcRApQwXgNgAyPgApJWAtBYQGwGxGxAHAnEIEEcA"
  "cS4jRD0T1Fw2KM0ENZMVypZhRLIIqIMdag9CBMFnhtJ1jDA5RrBcMSPE7AIBkIl8UFGgP6Fu4IOa"
  "wczAZpOZl1lix8Dm45uYmWfNIOSTlViWqJ+TmJeu75+UlZpcYs3ACZLSA4kzMIYxMIX5MAhHIykL"
  "LinKzEu3ZmDJBSoDOZiPgRlMgv3T2MDygZGRs4OJB8n9MBoWzrAwmQD1Eyy8WZHCmg0pvBkVIGpA"
  "Yc4oABEHhRuTAsRMUDwwQ9WAwoJBAaIGHE5Q9aB4BgBQSwECHgMUAAAACADVUTdJrPfZSDEBAAAg"
  "AgAACwAYAAAAAAAAAAAAoIEAAAAAY2xhc3Nlcy5kZXhVVAUAAwFj5Vd1eAsAAQTkAwEABIgTAABQ"
  "SwUGAAAAAAEAAQBRAAAAdgEAAAAA";

static const char kRawZipClassesDexAbsent[] =
  "UEsDBBQAAAAIANVRN0ms99lIMQEAACACAAAOABwAbm90Y2xhc3Nlcy5kZXhVVAkAAwFj5VcUY+VX"
  "dXgLAAEE5AMBAASIEwAAS0mt4DIwtmDYYdV9csrcks83lpxZN2vD8f/1p1beWX3vabQCEwNDAQMD"
  "Q0WYiRADFPQwMjBwMEDEWYB4AhADlTEsYEAAZiDeAcRApQwXgNgAyPgApJWAtBYQGwGxGxAHAnEI"
  "EEcAcS4jRD0T1Fw2KM0ENZMVypZhRLIIqIMdag9CBMFnhtJ1jDA5RrBcMSPE7AIBkIl8UFGgP6Fu"
  "4IOawczAZpOZl1lix8Dm45uYmWfNIOSTlViWqJ+TmJeu75+UlZpcYs3ACZLSA4kzMIYxMIX5MAhH"
  "IykLLinKzEu3ZmDJBSoDOZiPgRlMgv3T2MDygZGRs4OJB8n9MBoWzrAwmQD1Eyy8WZHCmg0pvBkV"
  "IGpAYc4oABEHhRuTAsRMUDwwQ9WAwoJBAaIGHE5Q9aB4BgBQSwECHgMUAAAACADVUTdJrPfZSDEB"
  "AAAgAgAADgAYAAAAAAAAAAAAoIEAAAAAbm90Y2xhc3Nlcy5kZXhVVAUAAwFj5Vd1eAsAAQTkAwEA"
  "BIgTAABQSwUGAAAAAAEAAQBUAAAAeQEAAAAA";

static const char kRawZipThreeDexFiles[] =
  "UEsDBBQAAAAIAP1WN0ms99lIMQEAACACAAAMABwAY2xhc3NlczIuZGV4VVQJAAOtbOVXrWzlV3V4"
  "CwABBOQDAQAEiBMAAEtJreAyMLZg2GHVfXLK3JLPN5acWTdrw/H/9adW3ll972m0AhMDQwEDA0NF"
  "mIkQAxT0MDIwcDBAxFmAeAIQA5UxLGBAAGYg3gHEQKUMF4DYAMj4AKSVgLQWEBsBsRsQBwJxCBBH"
  "AHEuI0Q9E9RcNijNBDWTFcqWYUSyCKiDHWoPQgTBZ4bSdYwwOUawXDEjxOwCAZCJfFBRoD+hbuCD"
  "msHMwGaTmZdZYsfA5uObmJlnzSDkk5VYlqifk5iXru+flJWaXGLNwAmS0gOJMzCGMTCF+TAIRyMp"
  "Cy4pysxLt2ZgyQUqAzmYj4EZTIL909jA8oGRkbODiQfJ/TAaFs6wMJkA9RMsvFmRwpoNKbwZFSBq"
  "QGHOKAARB4UbkwLETFA8MEPVgMKCQQGiBhxOUPWgeAYAUEsDBBQAAAAIAABXN0ms99lIMQEAACAC"
  "AAAMABwAY2xhc3NlczMuZGV4VVQJAAOvbOVXr2zlV3V4CwABBOQDAQAEiBMAAEtJreAyMLZg2GHV"
  "fXLK3JLPN5acWTdrw/H/9adW3ll972m0AhMDQwEDA0NFmIkQAxT0MDIwcDBAxFmAeAIQA5UxLGBA"
  "AGYg3gHEQKUMF4DYAMj4AKSVgLQWEBsBsRsQBwJxCBBHAHEuI0Q9E9RcNijNBDWTFcqWYUSyCKiD"
  "HWoPQgTBZ4bSdYwwOUawXDEjxOwCAZCJfFBRoD+hbuCDmsHMwGaTmZdZYsfA5uObmJlnzSDkk5VY"
  "lqifk5iXru+flJWaXGLNwAmS0gOJMzCGMTCF+TAIRyMpCy4pysxLt2ZgyQUqAzmYj4EZTIL909jA"
  "8oGRkbODiQfJ/TAaFs6wMJkA9RMsvFmRwpoNKbwZFSBqQGHOKAARB4UbkwLETFA8MEPVgMKCQQGi"
  "BhxOUPWgeAYAUEsDBBQAAAAIANVRN0ms99lIMQEAACACAAALABwAY2xhc3Nlcy5kZXhVVAkAAwFj"
  "5VetbOVXdXgLAAEE5AMBAASIEwAAS0mt4DIwtmDYYdV9csrcks83lpxZN2vD8f/1p1beWX3vabQC"
  "EwNDAQMDQ0WYiRADFPQwMjBwMEDEWYB4AhADlTEsYEAAZiDeAcRApQwXgNgAyPgApJWAtBYQGwGx"
  "GxAHAnEIEEcAcS4jRD0T1Fw2KM0ENZMVypZhRLIIqIMdag9CBMFnhtJ1jDA5RrBcMSPE7AIBkIl8"
  "UFGgP6Fu4IOawczAZpOZl1lix8Dm45uYmWfNIOSTlViWqJ+TmJeu75+UlZpcYs3ACZLSA4kzMIYx"
  "MIX5MAhHIykLLinKzEu3ZmDJBSoDOZiPgRlMgv3T2MDygZGRs4OJB8n9MBoWzrAwmQD1Eyy8WZHC"
  "mg0pvBkVIGpAYc4oABEHhRuTAsRMUDwwQ9WAwoJBAaIGHE5Q9aB4BgBQSwECHgMUAAAACAD9VjdJ"
  "rPfZSDEBAAAgAgAADAAYAAAAAAAAAAAAoIEAAAAAY2xhc3NlczIuZGV4VVQFAAOtbOVXdXgLAAEE"
  "5AMBAASIEwAAUEsBAh4DFAAAAAgAAFc3Saz32UgxAQAAIAIAAAwAGAAAAAAAAAAAAKCBdwEAAGNs"
  "YXNzZXMzLmRleFVUBQADr2zlV3V4CwABBOQDAQAEiBMAAFBLAQIeAxQAAAAIANVRN0ms99lIMQEA"
  "ACACAAALABgAAAAAAAAAAACgge4CAABjbGFzc2VzLmRleFVUBQADAWPlV3V4CwABBOQDAQAEiBMA"
  "AFBLBQYAAAAAAwADAPUAAABkBAAAAAA=";

static const char kRawDexBadMapOffset[] =
  "ZGV4CjAzNQAZKGSz85r+tXJ1I24FYi+FpQtWbXtelAmoAQAAcAAAAHhWNBIAAAAAAAAAAEAwIBAF"
  "AAAAcAAAAAMAAACEAAAAAQAAAJAAAAAAAAAAAAAAAAIAAACcAAAAAQAAAKwAAADcAAAAzAAAAOQA"
  "AADsAAAA9AAAAPkAAAANAQAAAgAAAAMAAAAEAAAABAAAAAIAAAAAAAAAAAAAAAAAAAABAAAAAAAA"
  "AAAAAAABAAAAAQAAAAAAAAABAAAAAAAAABUBAAAAAAAAAQABAAEAAAAQAQAABAAAAHAQAQAAAA4A"
  "Bjxpbml0PgAGQS5qYXZhAANMQTsAEkxqYXZhL2xhbmcvT2JqZWN0OwABVgABAAcOAAAAAQAAgYAE"
  "zAEACwAAAAAAAAABAAAAAAAAAAEAAAAFAAAAcAAAAAIAAAADAAAAhAAAAAMAAAABAAAAkAAAAAUA"
  "AAACAAAAnAAAAAYAAAABAAAArAAAAAEgAAABAAAAzAAAAAIgAAAFAAAA5AAAAAMgAAABAAAAEAEA"
  "AAAgAAABAAAAFQEAAAAQAAABAAAAIAEAAA==";

static const char kRawDexDebugInfoLocalNullType[] =
    "ZGV4CjAzNQA+Kwj2g6OZMH88OvK9Ey6ycdIsFCt18ED8AQAAcAAAAHhWNBIAAAAAAAAAAHQBAAAI"
    "AAAAcAAAAAQAAACQAAAAAgAAAKAAAAAAAAAAAAAAAAMAAAC4AAAAAQAAANAAAAAMAQAA8AAAABwB"
    "AAAkAQAALAEAAC8BAAA0AQAASAEAAEsBAABOAQAAAgAAAAMAAAAEAAAABQAAAAIAAAAAAAAAAAAA"
    "AAUAAAADAAAAAAAAAAEAAQAAAAAAAQAAAAYAAAACAAEAAAAAAAEAAAABAAAAAgAAAAAAAAABAAAA"
    "AAAAAGMBAAAAAAAAAQABAAEAAABUAQAABAAAAHAQAgAAAA4AAgABAAAAAABZAQAAAgAAABIQDwAG"
    "PGluaXQ+AAZBLmphdmEAAUkAA0xBOwASTGphdmEvbGFuZy9PYmplY3Q7AAFWAAFhAAR0aGlzAAEA"
    "Bw4AAwAHDh4DAAcAAAAAAQEAgYAE8AEBAIgCAAAACwAAAAAAAAABAAAAAAAAAAEAAAAIAAAAcAAA"
    "AAIAAAAEAAAAkAAAAAMAAAACAAAAoAAAAAUAAAADAAAAuAAAAAYAAAABAAAA0AAAAAEgAAACAAAA"
    "8AAAAAIgAAAIAAAAHAEAAAMgAAACAAAAVAEAAAAgAAABAAAAYwEAAAAQAAABAAAAdAEAAA==";

static void DecodeAndWriteDexFile(const char* base64, const char* location) {
  // decode base64
  CHECK(base64 != nullptr);
  std::vector<uint8_t> dex_bytes = DecodeBase64Vec(base64);
  CHECK_NE(dex_bytes.size(), 0u);

  // write to provided file
  std::unique_ptr<File> file(OS::CreateEmptyFile(location));
  CHECK(file.get() != nullptr);
  if (!file->WriteFully(dex_bytes.data(), dex_bytes.size())) {
    PLOG(FATAL) << "Failed to write base64 as dex file";
  }
  if (file->FlushCloseOrErase() != 0) {
    PLOG(FATAL) << "Could not flush and close test file.";
  }
}

static bool OpenDexFilesBase64(const char* base64,
                               const char* location,
                               std::vector<std::unique_ptr<const DexFile>>* dex_files,
                               std::string* error_msg) {
  DecodeAndWriteDexFile(base64, location);

  // read dex file(s)
  ScopedObjectAccess soa(Thread::Current());
  static constexpr bool kVerifyChecksum = true;
  std::vector<std::unique_ptr<const DexFile>> tmp;
  bool success = DexFile::Open(location, location, kVerifyChecksum, error_msg, &tmp);
  if (success) {
    for (std::unique_ptr<const DexFile>& dex_file : tmp) {
      EXPECT_EQ(PROT_READ, dex_file->GetPermissions());
      EXPECT_TRUE(dex_file->IsReadOnly());
    }
    *dex_files = std::move(tmp);
  }
  return success;
}

static std::unique_ptr<const DexFile> OpenDexFileBase64(const char* base64,
                                                        const char* location) {
  // read dex files.
  std::string error_msg;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  bool success = OpenDexFilesBase64(base64, location, &dex_files, &error_msg);
  CHECK(success) << error_msg;
  EXPECT_EQ(1U, dex_files.size());
  return std::move(dex_files[0]);
}

static std::unique_ptr<const DexFile> OpenDexFileInMemoryBase64(const char* base64,
                                                                const char* location,
                                                                uint32_t location_checksum,
                                                                bool expect_success) {
  CHECK(base64 != nullptr);
  std::vector<uint8_t> dex_bytes = DecodeBase64Vec(base64);
  CHECK_NE(dex_bytes.size(), 0u);

  std::string error_message;
  std::unique_ptr<MemMap> region(MemMap::MapAnonymous("test-region",
                                                      nullptr,
                                                      dex_bytes.size(),
                                                      PROT_READ | PROT_WRITE,
                                                      /* low_4gb */ false,
                                                      /* reuse */ false,
                                                      &error_message));
  memcpy(region->Begin(), dex_bytes.data(), dex_bytes.size());
  std::unique_ptr<const DexFile> dex_file(DexFile::Open(location,
                                                        location_checksum,
                                                        std::move(region),
                                                        /* verify */ true,
                                                        /* verify_checksum */ true,
                                                        &error_message));
  if (expect_success) {
    CHECK(dex_file != nullptr) << error_message;
  } else {
    CHECK(dex_file == nullptr) << "Expected dex file open to fail.";
  }
  return dex_file;
}

static void ValidateDexFileHeader(std::unique_ptr<const DexFile> dex_file) {
  static const uint8_t kExpectedDexFileMagic[8] = {
    /* d */ 0x64, /* e */ 0x64, /* x */ 0x78, /* \n */ 0x0d,
    /* 0 */ 0x30, /* 3 */ 0x33, /* 5 */ 0x35, /* \0 */ 0x00
  };
  static const uint8_t kExpectedSha1[DexFile::kSha1DigestSize] = {
    0x7b, 0xb8, 0x0c, 0xd4, 0x1f, 0xd6, 0x1e, 0xc5,
    0x89, 0xe8, 0xbe, 0xe5, 0x18, 0x02, 0x12, 0x18,
    0x2e, 0xf2, 0x8c, 0x3d,
  };

  const DexFile::Header& header = dex_file->GetHeader();
  EXPECT_EQ(*kExpectedDexFileMagic, *header.magic_);
  EXPECT_EQ(0x00d87910U, header.checksum_);
  EXPECT_EQ(*kExpectedSha1, *header.signature_);
  EXPECT_EQ(904U, header.file_size_);
  EXPECT_EQ(112U, header.header_size_);
  EXPECT_EQ(0U, header.link_size_);
  EXPECT_EQ(0U, header.link_off_);
  EXPECT_EQ(15U, header.string_ids_size_);
  EXPECT_EQ(112U, header.string_ids_off_);
  EXPECT_EQ(7U, header.type_ids_size_);
  EXPECT_EQ(172U, header.type_ids_off_);
  EXPECT_EQ(2U, header.proto_ids_size_);
  EXPECT_EQ(200U, header.proto_ids_off_);
  EXPECT_EQ(1U, header.field_ids_size_);
  EXPECT_EQ(224U, header.field_ids_off_);
  EXPECT_EQ(3U, header.method_ids_size_);
  EXPECT_EQ(232U, header.method_ids_off_);
  EXPECT_EQ(2U, header.class_defs_size_);
  EXPECT_EQ(256U, header.class_defs_off_);
  EXPECT_EQ(584U, header.data_size_);
  EXPECT_EQ(320U, header.data_off_);

  EXPECT_EQ(header.checksum_, dex_file->GetLocationChecksum());
}

TEST_F(DexFileTest, Header) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw(OpenDexFileBase64(kRawDex, tmp.GetFilename().c_str()));
  ValidateDexFileHeader(std::move(raw));
}

TEST_F(DexFileTest, HeaderInMemory) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw =
      OpenDexFileInMemoryBase64(kRawDex, tmp.GetFilename().c_str(), 0x00d87910U, true);
  ValidateDexFileHeader(std::move(raw));
}

TEST_F(DexFileTest, Version38Accepted) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw(OpenDexFileBase64(kRawDex38, tmp.GetFilename().c_str()));
  ASSERT_TRUE(raw.get() != nullptr);

  const DexFile::Header& header = raw->GetHeader();
  EXPECT_EQ(38u, header.GetVersion());
}

TEST_F(DexFileTest, Version39Rejected) {
  ScratchFile tmp;
  const char* location = tmp.GetFilename().c_str();
  DecodeAndWriteDexFile(kRawDex39, location);

  ScopedObjectAccess soa(Thread::Current());
  static constexpr bool kVerifyChecksum = true;
  std::string error_msg;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  ASSERT_FALSE(DexFile::Open(location, location, kVerifyChecksum, &error_msg, &dex_files));
}

TEST_F(DexFileTest, ZeroLengthDexRejected) {
  ScratchFile tmp;
  const char* location = tmp.GetFilename().c_str();
  DecodeAndWriteDexFile(kRawDexZeroLength, location);

  ScopedObjectAccess soa(Thread::Current());
  static constexpr bool kVerifyChecksum = true;
  std::string error_msg;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  ASSERT_FALSE(DexFile::Open(location, location, kVerifyChecksum, &error_msg, &dex_files));
}

TEST_F(DexFileTest, GetLocationChecksum) {
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<const DexFile> raw(OpenTestDexFile("Main"));
  EXPECT_NE(raw->GetHeader().checksum_, raw->GetLocationChecksum());
}

TEST_F(DexFileTest, GetChecksum) {
  std::vector<uint32_t> checksums;
  ScopedObjectAccess soa(Thread::Current());
  std::string error_msg;
  EXPECT_TRUE(DexFile::GetMultiDexChecksums(GetLibCoreDexFileNames()[0].c_str(), &checksums, &error_msg))
      << error_msg;
  ASSERT_EQ(1U, checksums.size());
  EXPECT_EQ(java_lang_dex_file_->GetLocationChecksum(), checksums[0]);
}

TEST_F(DexFileTest, GetMultiDexChecksums) {
  std::string error_msg;
  std::vector<uint32_t> checksums;
  std::string multidex_file = GetTestDexFileName("MultiDex");
  EXPECT_TRUE(DexFile::GetMultiDexChecksums(multidex_file.c_str(),
                                            &checksums,
                                            &error_msg)) << error_msg;

  std::vector<std::unique_ptr<const DexFile>> dexes = OpenTestDexFiles("MultiDex");
  ASSERT_EQ(2U, dexes.size());
  ASSERT_EQ(2U, checksums.size());

  EXPECT_EQ(dexes[0]->GetLocation(), DexFile::GetMultiDexLocation(0, multidex_file.c_str()));
  EXPECT_EQ(dexes[0]->GetLocationChecksum(), checksums[0]);

  EXPECT_EQ(dexes[1]->GetLocation(), DexFile::GetMultiDexLocation(1, multidex_file.c_str()));
  EXPECT_EQ(dexes[1]->GetLocationChecksum(), checksums[1]);
}

TEST_F(DexFileTest, ClassDefs) {
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<const DexFile> raw(OpenTestDexFile("Nested"));
  ASSERT_TRUE(raw.get() != nullptr);
  EXPECT_EQ(3U, raw->NumClassDefs());

  const DexFile::ClassDef& c0 = raw->GetClassDef(0);
  EXPECT_STREQ("LNested$1;", raw->GetClassDescriptor(c0));

  const DexFile::ClassDef& c1 = raw->GetClassDef(1);
  EXPECT_STREQ("LNested$Inner;", raw->GetClassDescriptor(c1));

  const DexFile::ClassDef& c2 = raw->GetClassDef(2);
  EXPECT_STREQ("LNested;", raw->GetClassDescriptor(c2));
}

TEST_F(DexFileTest, GetMethodSignature) {
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<const DexFile> raw(OpenTestDexFile("GetMethodSignature"));
  ASSERT_TRUE(raw.get() != nullptr);
  EXPECT_EQ(1U, raw->NumClassDefs());

  const DexFile::ClassDef& class_def = raw->GetClassDef(0);
  ASSERT_STREQ("LGetMethodSignature;", raw->GetClassDescriptor(class_def));

  const uint8_t* class_data = raw->GetClassData(class_def);
  ASSERT_TRUE(class_data != nullptr);
  ClassDataItemIterator it(*raw, class_data);

  EXPECT_EQ(1u, it.NumDirectMethods());

  // Check the signature for the static initializer.
  {
    ASSERT_EQ(1U, it.NumDirectMethods());
    const DexFile::MethodId& method_id = raw->GetMethodId(it.GetMemberIndex());
    const char* name = raw->StringDataByIdx(method_id.name_idx_);
    ASSERT_STREQ("<init>", name);
    std::string signature(raw->GetMethodSignature(method_id).ToString());
    ASSERT_EQ("()V", signature);
  }

  // Check both virtual methods.
  ASSERT_EQ(2U, it.NumVirtualMethods());
  {
    it.Next();
    const DexFile::MethodId& method_id = raw->GetMethodId(it.GetMemberIndex());

    const char* name = raw->StringDataByIdx(method_id.name_idx_);
    ASSERT_STREQ("m1", name);

    std::string signature(raw->GetMethodSignature(method_id).ToString());
    ASSERT_EQ("(IDJLjava/lang/Object;)Ljava/lang/Float;", signature);
  }

  {
    it.Next();
    const DexFile::MethodId& method_id = raw->GetMethodId(it.GetMemberIndex());

    const char* name = raw->StringDataByIdx(method_id.name_idx_);
    ASSERT_STREQ("m2", name);

    std::string signature(raw->GetMethodSignature(method_id).ToString());
    ASSERT_EQ("(ZSC)LGetMethodSignature;", signature);
  }
}

TEST_F(DexFileTest, FindStringId) {
  ScopedObjectAccess soa(Thread::Current());
  std::unique_ptr<const DexFile> raw(OpenTestDexFile("GetMethodSignature"));
  ASSERT_TRUE(raw.get() != nullptr);
  EXPECT_EQ(1U, raw->NumClassDefs());

  const char* strings[] = { "LGetMethodSignature;", "Ljava/lang/Float;", "Ljava/lang/Object;",
      "D", "I", "J", nullptr };
  for (size_t i = 0; strings[i] != nullptr; i++) {
    const char* str = strings[i];
    const DexFile::StringId* str_id = raw->FindStringId(str);
    const char* dex_str = raw->GetStringData(*str_id);
    EXPECT_STREQ(dex_str, str);
  }
}

TEST_F(DexFileTest, FindTypeId) {
  for (size_t i = 0; i < java_lang_dex_file_->NumTypeIds(); i++) {
    const char* type_str = java_lang_dex_file_->StringByTypeIdx(dex::TypeIndex(i));
    const DexFile::StringId* type_str_id = java_lang_dex_file_->FindStringId(type_str);
    ASSERT_TRUE(type_str_id != nullptr);
    dex::StringIndex type_str_idx = java_lang_dex_file_->GetIndexForStringId(*type_str_id);
    const DexFile::TypeId* type_id = java_lang_dex_file_->FindTypeId(type_str_idx);
    ASSERT_EQ(type_id, java_lang_dex_file_->FindTypeId(type_str));
    ASSERT_TRUE(type_id != nullptr);
    EXPECT_EQ(java_lang_dex_file_->GetIndexForTypeId(*type_id).index_, i);
  }
}

TEST_F(DexFileTest, FindProtoId) {
  for (size_t i = 0; i < java_lang_dex_file_->NumProtoIds(); i++) {
    const DexFile::ProtoId& to_find = java_lang_dex_file_->GetProtoId(i);
    const DexFile::TypeList* to_find_tl = java_lang_dex_file_->GetProtoParameters(to_find);
    std::vector<dex::TypeIndex> to_find_types;
    if (to_find_tl != nullptr) {
      for (size_t j = 0; j < to_find_tl->Size(); j++) {
        to_find_types.push_back(to_find_tl->GetTypeItem(j).type_idx_);
      }
    }
    const DexFile::ProtoId* found =
        java_lang_dex_file_->FindProtoId(to_find.return_type_idx_, to_find_types);
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(java_lang_dex_file_->GetIndexForProtoId(*found), i);
  }
}

TEST_F(DexFileTest, FindMethodId) {
  for (size_t i = 0; i < java_lang_dex_file_->NumMethodIds(); i++) {
    const DexFile::MethodId& to_find = java_lang_dex_file_->GetMethodId(i);
    const DexFile::TypeId& klass = java_lang_dex_file_->GetTypeId(to_find.class_idx_);
    const DexFile::StringId& name = java_lang_dex_file_->GetStringId(to_find.name_idx_);
    const DexFile::ProtoId& signature = java_lang_dex_file_->GetProtoId(to_find.proto_idx_);
    const DexFile::MethodId* found = java_lang_dex_file_->FindMethodId(klass, name, signature);
    ASSERT_TRUE(found != nullptr) << "Didn't find method " << i << ": "
        << java_lang_dex_file_->StringByTypeIdx(to_find.class_idx_) << "."
        << java_lang_dex_file_->GetStringData(name)
        << java_lang_dex_file_->GetMethodSignature(to_find);
    EXPECT_EQ(java_lang_dex_file_->GetIndexForMethodId(*found), i);
  }
}

TEST_F(DexFileTest, FindFieldId) {
  for (size_t i = 0; i < java_lang_dex_file_->NumFieldIds(); i++) {
    const DexFile::FieldId& to_find = java_lang_dex_file_->GetFieldId(i);
    const DexFile::TypeId& klass = java_lang_dex_file_->GetTypeId(to_find.class_idx_);
    const DexFile::StringId& name = java_lang_dex_file_->GetStringId(to_find.name_idx_);
    const DexFile::TypeId& type = java_lang_dex_file_->GetTypeId(to_find.type_idx_);
    const DexFile::FieldId* found = java_lang_dex_file_->FindFieldId(klass, name, type);
    ASSERT_TRUE(found != nullptr) << "Didn't find field " << i << ": "
        << java_lang_dex_file_->StringByTypeIdx(to_find.type_idx_) << " "
        << java_lang_dex_file_->StringByTypeIdx(to_find.class_idx_) << "."
        << java_lang_dex_file_->GetStringData(name);
    EXPECT_EQ(java_lang_dex_file_->GetIndexForFieldId(*found), i);
  }
}

TEST_F(DexFileTest, GetMultiDexClassesDexName) {
  ASSERT_EQ("classes.dex", DexFile::GetMultiDexClassesDexName(0));
  ASSERT_EQ("classes2.dex", DexFile::GetMultiDexClassesDexName(1));
  ASSERT_EQ("classes3.dex", DexFile::GetMultiDexClassesDexName(2));
  ASSERT_EQ("classes100.dex", DexFile::GetMultiDexClassesDexName(99));
}

TEST_F(DexFileTest, GetMultiDexLocation) {
  std::string dex_location_str = "/system/app/framework.jar";
  const char* dex_location = dex_location_str.c_str();
  ASSERT_EQ("/system/app/framework.jar", DexFile::GetMultiDexLocation(0, dex_location));
  ASSERT_EQ("/system/app/framework.jar:classes2.dex",
            DexFile::GetMultiDexLocation(1, dex_location));
  ASSERT_EQ("/system/app/framework.jar:classes101.dex",
            DexFile::GetMultiDexLocation(100, dex_location));
}

TEST_F(DexFileTest, GetDexCanonicalLocation) {
  ScratchFile file;
  UniqueCPtr<const char[]> dex_location_real(realpath(file.GetFilename().c_str(), nullptr));
  std::string dex_location(dex_location_real.get());

  ASSERT_EQ(dex_location, DexFile::GetDexCanonicalLocation(dex_location.c_str()));
  std::string multidex_location = DexFile::GetMultiDexLocation(1, dex_location.c_str());
  ASSERT_EQ(multidex_location, DexFile::GetDexCanonicalLocation(multidex_location.c_str()));

  std::string dex_location_sym = dex_location + "symlink";
  ASSERT_EQ(0, symlink(dex_location.c_str(), dex_location_sym.c_str()));

  ASSERT_EQ(dex_location, DexFile::GetDexCanonicalLocation(dex_location_sym.c_str()));

  std::string multidex_location_sym = DexFile::GetMultiDexLocation(1, dex_location_sym.c_str());
  ASSERT_EQ(multidex_location, DexFile::GetDexCanonicalLocation(multidex_location_sym.c_str()));

  ASSERT_EQ(0, unlink(dex_location_sym.c_str()));
}

TEST(DexFileUtilsTest, GetBaseLocationAndMultiDexSuffix) {
  EXPECT_EQ("/foo/bar/baz.jar", DexFile::GetBaseLocation("/foo/bar/baz.jar"));
  EXPECT_EQ("/foo/bar/baz.jar", DexFile::GetBaseLocation("/foo/bar/baz.jar:classes2.dex"));
  EXPECT_EQ("/foo/bar/baz.jar", DexFile::GetBaseLocation("/foo/bar/baz.jar:classes8.dex"));
  EXPECT_EQ("", DexFile::GetMultiDexSuffix("/foo/bar/baz.jar"));
  EXPECT_EQ(":classes2.dex", DexFile::GetMultiDexSuffix("/foo/bar/baz.jar:classes2.dex"));
  EXPECT_EQ(":classes8.dex", DexFile::GetMultiDexSuffix("/foo/bar/baz.jar:classes8.dex"));
}

TEST_F(DexFileTest, ZipOpenClassesPresent) {
  ScratchFile tmp;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  std::string error_msg;
  ASSERT_TRUE(OpenDexFilesBase64(kRawZipClassesDexPresent, tmp.GetFilename().c_str(), &dex_files,
                                 &error_msg));
  EXPECT_EQ(dex_files.size(), 1u);
}

TEST_F(DexFileTest, ZipOpenClassesAbsent) {
  ScratchFile tmp;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  std::string error_msg;
  ASSERT_FALSE(OpenDexFilesBase64(kRawZipClassesDexAbsent, tmp.GetFilename().c_str(), &dex_files,
                                  &error_msg));
  EXPECT_EQ(dex_files.size(), 0u);
}

TEST_F(DexFileTest, ZipOpenThreeDexFiles) {
  ScratchFile tmp;
  std::vector<std::unique_ptr<const DexFile>> dex_files;
  std::string error_msg;
  ASSERT_TRUE(OpenDexFilesBase64(kRawZipThreeDexFiles, tmp.GetFilename().c_str(), &dex_files,
                                 &error_msg));
  EXPECT_EQ(dex_files.size(), 3u);
}

TEST_F(DexFileTest, OpenDexBadMapOffset) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw =
      OpenDexFileInMemoryBase64(kRawDexBadMapOffset, tmp.GetFilename().c_str(), 0xb3642819U, false);
  EXPECT_EQ(raw, nullptr);
}

TEST_F(DexFileTest, GetStringWithNoIndex) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw(OpenDexFileBase64(kRawDex, tmp.GetFilename().c_str()));
  dex::TypeIndex idx;
  EXPECT_EQ(raw->StringByTypeIdx(idx), nullptr);
}

static void Callback(void* context ATTRIBUTE_UNUSED,
                     const DexFile::LocalInfo& entry ATTRIBUTE_UNUSED) {
}

TEST_F(DexFileTest, OpenDexDebugInfoLocalNullType) {
  ScratchFile tmp;
  std::unique_ptr<const DexFile> raw = OpenDexFileInMemoryBase64(
      kRawDexDebugInfoLocalNullType, tmp.GetFilename().c_str(), 0xf25f2b38U, true);
  const DexFile::ClassDef& class_def = raw->GetClassDef(0);
  const DexFile::CodeItem* code_item = raw->GetCodeItem(raw->FindCodeItemOffset(class_def, 1));
  ASSERT_TRUE(raw->DecodeDebugLocalInfo(code_item, true, 1, Callback, nullptr));
}

}  // namespace art
