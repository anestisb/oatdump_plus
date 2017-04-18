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

#include "safe_copy.h"

#include <unistd.h>
#include <sys/uio.h>

#include <android-base/macros.h>

namespace art {

ssize_t SafeCopy(void *dst, const void *src, size_t len) {
#if defined(__linux__)
  struct iovec dst_iov = {
    .iov_base = dst,
    .iov_len = len,
  };
  struct iovec src_iov = {
    .iov_base = const_cast<void*>(src),
    .iov_len = len,
  };

  ssize_t rc = process_vm_readv(getpid(), &dst_iov, 1, &src_iov, 1, 0);
  if (rc == -1) {
    return 0;
  }
  return rc;
#else
  UNUSED(dst, src, len);
  return -1;
#endif
}

}  // namespace art
