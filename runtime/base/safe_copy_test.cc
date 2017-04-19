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

#include "common_runtime_test.h"

#include <sys/mman.h>
#include <sys/user.h>

namespace art {

#if defined(__linux__)

TEST(SafeCopyTest, smoke) {
  // Map two pages, and mark the second one as PROT_NONE.
  void* map = mmap(nullptr, PAGE_SIZE * 2, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(MAP_FAILED, map);
  char* page1 = static_cast<char*>(map);
  ASSERT_EQ(0, mprotect(page1 + PAGE_SIZE, PAGE_SIZE, PROT_NONE));

  page1[0] = 'a';
  page1[PAGE_SIZE - 1] = 'z';

  char buf[PAGE_SIZE];

  // Completely valid read.
  memset(buf, 0xCC, sizeof(buf));
  EXPECT_EQ(static_cast<ssize_t>(PAGE_SIZE), SafeCopy(buf, page1, PAGE_SIZE));
  EXPECT_EQ(0, memcmp(buf, page1, PAGE_SIZE));

  // Reading off of the end.
  memset(buf, 0xCC, sizeof(buf));
  EXPECT_EQ(static_cast<ssize_t>(PAGE_SIZE - 1), SafeCopy(buf, page1 + 1, PAGE_SIZE));
  EXPECT_EQ(0, memcmp(buf, page1 + 1, PAGE_SIZE - 1));

  // Completely invalid.
  EXPECT_EQ(0, SafeCopy(buf, page1 + PAGE_SIZE, PAGE_SIZE));
  ASSERT_EQ(0, munmap(map, PAGE_SIZE * 2));
}

#endif  // defined(__linux__)

}  // namespace art
