/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "base/arena_allocator.h"
#include "base/arena_bit_vector.h"
#include "base/memory_tool.h"
#include "gtest/gtest.h"

namespace art {

class ArenaAllocatorTest : public testing::Test {
 protected:
  size_t NumberOfArenas(ArenaAllocator* arena) {
    size_t result = 0u;
    for (Arena* a = arena->arena_head_; a != nullptr; a = a->next_) {
      ++result;
    }
    return result;
  }
};

TEST_F(ArenaAllocatorTest, Test) {
  ArenaPool pool;
  ArenaAllocator arena(&pool);
  ArenaBitVector bv(&arena, 10, true);
  bv.SetBit(5);
  EXPECT_EQ(1U, bv.GetStorageSize());
  bv.SetBit(35);
  EXPECT_EQ(2U, bv.GetStorageSize());
}

TEST_F(ArenaAllocatorTest, MakeDefined) {
  // Regression test to make sure we mark the allocated area defined.
  ArenaPool pool;
  static constexpr size_t kSmallArraySize = 10;
  static constexpr size_t kLargeArraySize = 50;
  uint32_t* small_array;
  {
    // Allocate a small array from an arena and release it.
    ArenaAllocator arena(&pool);
    small_array = arena.AllocArray<uint32_t>(kSmallArraySize);
    ASSERT_EQ(0u, small_array[kSmallArraySize - 1u]);
  }
  {
    // Reuse the previous arena and allocate more than previous allocation including red zone.
    ArenaAllocator arena(&pool);
    uint32_t* large_array = arena.AllocArray<uint32_t>(kLargeArraySize);
    ASSERT_EQ(0u, large_array[kLargeArraySize - 1u]);
    // Verify that the allocation was made on the same arena.
    ASSERT_EQ(small_array, large_array);
  }
}

TEST_F(ArenaAllocatorTest, LargeAllocations) {
  {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    // Note: Leaving some space for memory tool red zones.
    void* alloc1 = arena.Alloc(Arena::kDefaultSize * 5 / 8);
    void* alloc2 = arena.Alloc(Arena::kDefaultSize * 2 / 8);
    ASSERT_NE(alloc1, alloc2);
    ASSERT_EQ(1u, NumberOfArenas(&arena));
  }
  {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    void* alloc1 = arena.Alloc(Arena::kDefaultSize * 13 / 16);
    void* alloc2 = arena.Alloc(Arena::kDefaultSize * 11 / 16);
    ASSERT_NE(alloc1, alloc2);
    ASSERT_EQ(2u, NumberOfArenas(&arena));
    void* alloc3 = arena.Alloc(Arena::kDefaultSize * 7 / 16);
    ASSERT_NE(alloc1, alloc3);
    ASSERT_NE(alloc2, alloc3);
    ASSERT_EQ(3u, NumberOfArenas(&arena));
  }
  {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    void* alloc1 = arena.Alloc(Arena::kDefaultSize * 13 / 16);
    void* alloc2 = arena.Alloc(Arena::kDefaultSize * 9 / 16);
    ASSERT_NE(alloc1, alloc2);
    ASSERT_EQ(2u, NumberOfArenas(&arena));
    // Note: Leaving some space for memory tool red zones.
    void* alloc3 = arena.Alloc(Arena::kDefaultSize * 5 / 16);
    ASSERT_NE(alloc1, alloc3);
    ASSERT_NE(alloc2, alloc3);
    ASSERT_EQ(2u, NumberOfArenas(&arena));
  }
  {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    void* alloc1 = arena.Alloc(Arena::kDefaultSize * 9 / 16);
    void* alloc2 = arena.Alloc(Arena::kDefaultSize * 13 / 16);
    ASSERT_NE(alloc1, alloc2);
    ASSERT_EQ(2u, NumberOfArenas(&arena));
    // Note: Leaving some space for memory tool red zones.
    void* alloc3 = arena.Alloc(Arena::kDefaultSize * 5 / 16);
    ASSERT_NE(alloc1, alloc3);
    ASSERT_NE(alloc2, alloc3);
    ASSERT_EQ(2u, NumberOfArenas(&arena));
  }
  {
    ArenaPool pool;
    ArenaAllocator arena(&pool);
    // Note: Leaving some space for memory tool red zones.
    for (size_t i = 0; i != 15; ++i) {
      arena.Alloc(Arena::kDefaultSize * 1 / 16);    // Allocate 15 times from the same arena.
      ASSERT_EQ(i + 1u, NumberOfArenas(&arena));
      arena.Alloc(Arena::kDefaultSize * 17 / 16);   // Allocate a separate arena.
      ASSERT_EQ(i + 2u, NumberOfArenas(&arena));
    }
  }
}

TEST_F(ArenaAllocatorTest, AllocAlignment) {
  ArenaPool pool;
  ArenaAllocator arena(&pool);
  for (size_t iterations = 0; iterations <= 10; ++iterations) {
    for (size_t size = 1; size <= ArenaAllocator::kAlignment + 1; ++size) {
      void* allocation = arena.Alloc(size);
      EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(allocation))
          << reinterpret_cast<uintptr_t>(allocation);
    }
  }
}

TEST_F(ArenaAllocatorTest, ReallocReuse) {
  // Realloc does not reuse arenas when running under sanitization. So we cannot do those
  if (RUNNING_ON_MEMORY_TOOL != 0) {
    printf("WARNING: TEST DISABLED FOR MEMORY_TOOL\n");
    return;
  }

  {
    // Case 1: small aligned allocation, aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2;
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = ArenaAllocator::kAlignment * 3;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_EQ(original_allocation, realloc_allocation);
  }

  {
    // Case 2: small aligned allocation, non-aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2;
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_EQ(original_allocation, realloc_allocation);
  }

  {
    // Case 3: small non-aligned allocation, aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = ArenaAllocator::kAlignment * 4;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_EQ(original_allocation, realloc_allocation);
  }

  {
    // Case 4: small non-aligned allocation, aligned non-extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = ArenaAllocator::kAlignment * 3;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_EQ(original_allocation, realloc_allocation);
  }

  // The next part is brittle, as the default size for an arena is variable, and we don't know about
  // sanitization.

  {
    // Case 5: large allocation, aligned extend into next arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = Arena::kDefaultSize - ArenaAllocator::kAlignment * 5;
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = Arena::kDefaultSize + ArenaAllocator::kAlignment * 2;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_NE(original_allocation, realloc_allocation);
  }

  {
    // Case 6: large allocation, non-aligned extend into next arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = Arena::kDefaultSize -
        ArenaAllocator::kAlignment * 4 -
        ArenaAllocator::kAlignment / 2;
    void* original_allocation = arena.Alloc(original_size);

    const size_t new_size = Arena::kDefaultSize +
        ArenaAllocator::kAlignment * 2 +
        ArenaAllocator::kAlignment / 2;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_NE(original_allocation, realloc_allocation);
  }
}

TEST_F(ArenaAllocatorTest, ReallocAlignment) {
  {
    // Case 1: small aligned allocation, aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2;
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = ArenaAllocator::kAlignment * 3;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }

  {
    // Case 2: small aligned allocation, non-aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2;
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }

  {
    // Case 3: small non-aligned allocation, aligned extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = ArenaAllocator::kAlignment * 4;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }

  {
    // Case 4: small non-aligned allocation, aligned non-extend inside arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = ArenaAllocator::kAlignment * 2 + (ArenaAllocator::kAlignment / 2);
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = ArenaAllocator::kAlignment * 3;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }

  // The next part is brittle, as the default size for an arena is variable, and we don't know about
  // sanitization.

  {
    // Case 5: large allocation, aligned extend into next arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = Arena::kDefaultSize - ArenaAllocator::kAlignment * 5;
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = Arena::kDefaultSize + ArenaAllocator::kAlignment * 2;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }

  {
    // Case 6: large allocation, non-aligned extend into next arena.
    ArenaPool pool;
    ArenaAllocator arena(&pool);

    const size_t original_size = Arena::kDefaultSize -
        ArenaAllocator::kAlignment * 4 -
        ArenaAllocator::kAlignment / 2;
    void* original_allocation = arena.Alloc(original_size);
    ASSERT_TRUE(IsAligned<ArenaAllocator::kAlignment>(original_allocation));

    const size_t new_size = Arena::kDefaultSize +
        ArenaAllocator::kAlignment * 2 +
        ArenaAllocator::kAlignment / 2;
    void* realloc_allocation = arena.Realloc(original_allocation, original_size, new_size);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(realloc_allocation));

    void* after_alloc = arena.Alloc(1);
    EXPECT_TRUE(IsAligned<ArenaAllocator::kAlignment>(after_alloc));
  }
}


}  // namespace art
