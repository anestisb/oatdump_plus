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

#include "file_magic.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "android-base/stringprintf.h"

#include "base/logging.h"
#include "base/unix_file/fd_file.h"
#include "dex_file.h"

namespace art {

using android::base::StringPrintf;

File OpenAndReadMagic(const char* filename, uint32_t* magic, std::string* error_msg) {
  CHECK(magic != nullptr);
  File fd(filename, O_RDONLY, /* check_usage */ false);
  if (fd.Fd() == -1) {
    *error_msg = StringPrintf("Unable to open '%s' : %s", filename, strerror(errno));
    return File();
  }
  int n = TEMP_FAILURE_RETRY(read(fd.Fd(), magic, sizeof(*magic)));
  if (n != sizeof(*magic)) {
    *error_msg = StringPrintf("Failed to find magic in '%s'", filename);
    return File();
  }
  if (lseek(fd.Fd(), 0, SEEK_SET) != 0) {
    *error_msg = StringPrintf("Failed to seek to beginning of file '%s' : %s", filename,
                              strerror(errno));
    return File();
  }
  return fd;
}

bool IsZipMagic(uint32_t magic) {
  return (('P' == ((magic >> 0) & 0xff)) &&
          ('K' == ((magic >> 8) & 0xff)));
}

bool IsDexMagic(uint32_t magic) {
  return DexFile::IsMagicValid(reinterpret_cast<const uint8_t*>(&magic));
}

}  // namespace art
