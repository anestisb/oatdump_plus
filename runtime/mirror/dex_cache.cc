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

#include "dex_cache-inl.h"

#include "art_method-inl.h"
#include "base/logging.h"
#include "class_linker.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/heap.h"
#include "globals.h"
#include "linear_alloc.h"
#include "object.h"
#include "object-inl.h"
#include "object_array-inl.h"
#include "runtime.h"
#include "string.h"
#include "thread.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace art {
namespace mirror {

void DexCache::InitializeDexCache(Thread* self,
                                  ObjPtr<mirror::DexCache> dex_cache,
                                  ObjPtr<mirror::String> location,
                                  const DexFile* dex_file,
                                  LinearAlloc* linear_alloc,
                                  PointerSize image_pointer_size) {
  DCHECK(dex_file != nullptr);
  ScopedAssertNoThreadSuspension sants(__FUNCTION__);
  DexCacheArraysLayout layout(image_pointer_size, dex_file);
  uint8_t* raw_arrays = nullptr;

  const OatDexFile* const oat_dex = dex_file->GetOatDexFile();
  if (oat_dex != nullptr && oat_dex->GetDexCacheArrays() != nullptr) {
    raw_arrays = oat_dex->GetDexCacheArrays();
  } else if (dex_file->NumStringIds() != 0u ||
             dex_file->NumTypeIds() != 0u ||
             dex_file->NumMethodIds() != 0u ||
             dex_file->NumFieldIds() != 0u) {
    // Zero-initialized.
    raw_arrays = reinterpret_cast<uint8_t*>(linear_alloc->Alloc(self, layout.Size()));
  }

  mirror::StringDexCacheType* strings = (dex_file->NumStringIds() == 0u) ? nullptr :
      reinterpret_cast<mirror::StringDexCacheType*>(raw_arrays + layout.StringsOffset());
  mirror::TypeDexCacheType* types = (dex_file->NumTypeIds() == 0u) ? nullptr :
      reinterpret_cast<mirror::TypeDexCacheType*>(raw_arrays + layout.TypesOffset());
  ArtMethod** methods = (dex_file->NumMethodIds() == 0u) ? nullptr :
      reinterpret_cast<ArtMethod**>(raw_arrays + layout.MethodsOffset());
  ArtField** fields = (dex_file->NumFieldIds() == 0u) ? nullptr :
      reinterpret_cast<ArtField**>(raw_arrays + layout.FieldsOffset());

  size_t num_strings = mirror::DexCache::kDexCacheStringCacheSize;
  if (dex_file->NumStringIds() < num_strings) {
    num_strings = dex_file->NumStringIds();
  }
  size_t num_types = mirror::DexCache::kDexCacheTypeCacheSize;
  if (dex_file->NumTypeIds() < num_types) {
    num_types = dex_file->NumTypeIds();
  }

  // Note that we allocate the method type dex caches regardless of this flag,
  // and we make sure here that they're not used by the runtime. This is in the
  // interest of simplicity and to avoid extensive compiler and layout class changes.
  //
  // If this needs to be mitigated in a production system running this code,
  // DexCache::kDexCacheMethodTypeCacheSize can be set to zero.
  mirror::MethodTypeDexCacheType* method_types = nullptr;
  size_t num_method_types = 0;

  if (dex_file->NumProtoIds() < mirror::DexCache::kDexCacheMethodTypeCacheSize) {
    num_method_types = dex_file->NumProtoIds();
  } else {
    num_method_types = mirror::DexCache::kDexCacheMethodTypeCacheSize;
  }

  if (num_method_types > 0) {
    method_types = reinterpret_cast<mirror::MethodTypeDexCacheType*>(
        raw_arrays + layout.MethodTypesOffset());
  }

  DCHECK_ALIGNED(raw_arrays, alignof(mirror::StringDexCacheType)) <<
                 "Expected raw_arrays to align to StringDexCacheType.";
  DCHECK_ALIGNED(layout.StringsOffset(), alignof(mirror::StringDexCacheType)) <<
                 "Expected StringsOffset() to align to StringDexCacheType.";
  DCHECK_ALIGNED(strings, alignof(mirror::StringDexCacheType)) <<
                 "Expected strings to align to StringDexCacheType.";
  static_assert(alignof(mirror::StringDexCacheType) == 8u,
                "Expected StringDexCacheType to have align of 8.");
  if (kIsDebugBuild) {
    // Sanity check to make sure all the dex cache arrays are empty. b/28992179
    for (size_t i = 0; i < num_strings; ++i) {
      CHECK_EQ(strings[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(strings[i].load(std::memory_order_relaxed).object.IsNull());
    }
    for (size_t i = 0; i < num_types; ++i) {
      CHECK_EQ(types[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(types[i].load(std::memory_order_relaxed).object.IsNull());
    }
    for (size_t i = 0; i < dex_file->NumMethodIds(); ++i) {
      CHECK(mirror::DexCache::GetElementPtrSize(methods, i, image_pointer_size) == nullptr);
    }
    for (size_t i = 0; i < dex_file->NumFieldIds(); ++i) {
      CHECK(mirror::DexCache::GetElementPtrSize(fields, i, image_pointer_size) == nullptr);
    }
    for (size_t i = 0; i < num_method_types; ++i) {
      CHECK_EQ(method_types[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(method_types[i].load(std::memory_order_relaxed).object.IsNull());
    }
  }
  if (strings != nullptr) {
    mirror::StringDexCachePair::Initialize(strings);
  }
  if (types != nullptr) {
    mirror::TypeDexCachePair::Initialize(types);
  }
  if (method_types != nullptr) {
    mirror::MethodTypeDexCachePair::Initialize(method_types);
  }
  dex_cache->Init(dex_file,
                  location,
                  strings,
                  num_strings,
                  types,
                  num_types,
                  methods,
                  dex_file->NumMethodIds(),
                  fields,
                  dex_file->NumFieldIds(),
                  method_types,
                  num_method_types,
                  image_pointer_size);
}

void DexCache::Init(const DexFile* dex_file,
                    ObjPtr<String> location,
                    StringDexCacheType* strings,
                    uint32_t num_strings,
                    TypeDexCacheType* resolved_types,
                    uint32_t num_resolved_types,
                    ArtMethod** resolved_methods,
                    uint32_t num_resolved_methods,
                    ArtField** resolved_fields,
                    uint32_t num_resolved_fields,
                    MethodTypeDexCacheType* resolved_method_types,
                    uint32_t num_resolved_method_types,
                    PointerSize pointer_size) {
  CHECK(dex_file != nullptr);
  CHECK(location != nullptr);
  CHECK_EQ(num_strings != 0u, strings != nullptr);
  CHECK_EQ(num_resolved_types != 0u, resolved_types != nullptr);
  CHECK_EQ(num_resolved_methods != 0u, resolved_methods != nullptr);
  CHECK_EQ(num_resolved_fields != 0u, resolved_fields != nullptr);
  CHECK_EQ(num_resolved_method_types != 0u, resolved_method_types != nullptr);

  SetDexFile(dex_file);
  SetLocation(location);
  SetStrings(strings);
  SetResolvedTypes(resolved_types);
  SetResolvedMethods(resolved_methods);
  SetResolvedFields(resolved_fields);
  SetResolvedMethodTypes(resolved_method_types);
  SetField32<false>(NumStringsOffset(), num_strings);
  SetField32<false>(NumResolvedTypesOffset(), num_resolved_types);
  SetField32<false>(NumResolvedMethodsOffset(), num_resolved_methods);
  SetField32<false>(NumResolvedFieldsOffset(), num_resolved_fields);
  SetField32<false>(NumResolvedMethodTypesOffset(), num_resolved_method_types);

  Runtime* const runtime = Runtime::Current();
  if (runtime->HasResolutionMethod()) {
    // Initialize the resolve methods array to contain trampolines for resolution.
    Fixup(runtime->GetResolutionMethod(), pointer_size);
  }
}

void DexCache::Fixup(ArtMethod* trampoline, PointerSize pointer_size) {
  // Fixup the resolve methods array to contain trampoline for resolution.
  CHECK(trampoline != nullptr);
  CHECK(trampoline->IsRuntimeMethod());
  auto* resolved_methods = GetResolvedMethods();
  for (size_t i = 0, length = NumResolvedMethods(); i < length; i++) {
    if (GetElementPtrSize<ArtMethod*>(resolved_methods, i, pointer_size) == nullptr) {
      SetElementPtrSize(resolved_methods, i, trampoline, pointer_size);
    }
  }
}

void DexCache::SetLocation(ObjPtr<mirror::String> location) {
  SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, location_), location);
}

}  // namespace mirror
}  // namespace art
