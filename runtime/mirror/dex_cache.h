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

#ifndef ART_RUNTIME_MIRROR_DEX_CACHE_H_
#define ART_RUNTIME_MIRROR_DEX_CACHE_H_

#include "array.h"
#include "art_field.h"
#include "art_method.h"
#include "class.h"
#include "object.h"
#include "object_array.h"

namespace art {

struct DexCacheOffsets;
class DexFile;
class ImageWriter;
union JValue;

namespace mirror {

class String;

struct StringDexCachePair {
  GcRoot<String> string_pointer;
  uint32_t string_index;
  // The array is initially [ {0,0}, {0,0}, {0,0} ... ]
  // We maintain the invariant that once a dex cache entry is populated,
  // the pointer is always non-0
  // Any given entry would thus be:
  // {non-0, non-0} OR {0,0}
  //
  // It's generally sufficiently enough then to check if the
  // lookup string index matches the stored string index (for a >0 string index)
  // because if it's true the pointer is also non-null.
  //
  // For the 0th entry which is a special case, the value is either
  // {0,0} (initial state) or {non-0, 0} which indicates
  // that a valid string is stored at that index for a dex string id of 0.
  //
  // As an optimization, we want to avoid branching on the string pointer since
  // it's always non-null if the string id branch succeeds (except for the 0th string id).
  // Set the initial state for the 0th entry to be {0,1} which is guaranteed to fail
  // the lookup string id == stored id branch.
  static void Initialize(StringDexCacheType* strings) {
    DCHECK(StringDexCacheType().is_lock_free());
    mirror::StringDexCachePair first_elem;
    first_elem.string_pointer = GcRoot<String>(nullptr);
    first_elem.string_index = 1;
    strings[0].store(first_elem, std::memory_order_relaxed);
  }
  static GcRoot<String> LookupString(StringDexCacheType* dex_cache,
                                     uint32_t string_idx,
                                     uint32_t cache_size) {
    StringDexCachePair index_string = dex_cache[string_idx % cache_size]
        .load(std::memory_order_relaxed);
    if (string_idx != index_string.string_index) return GcRoot<String>(nullptr);
    DCHECK(!index_string.string_pointer.IsNull());
    return index_string.string_pointer;
  }
};
using StringDexCacheType = std::atomic<StringDexCachePair>;


// C++ mirror of java.lang.DexCache.
class MANAGED DexCache FINAL : public Object {
 public:
  // Size of java.lang.DexCache.class.
  static uint32_t ClassSize(PointerSize pointer_size);

  // Size of string dex cache. Needs to be a power of 2 for entrypoint assumptions to hold.
  static constexpr size_t kDexCacheStringCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheStringCacheSize),
                "String dex cache size is not a power of 2.");

  static constexpr size_t StaticStringSize() {
    return kDexCacheStringCacheSize;
  }

  // Size of an instance of java.lang.DexCache not including referenced values.
  static constexpr uint32_t InstanceSize() {
    return sizeof(DexCache);
  }

  void Init(const DexFile* dex_file,
            String* location,
            StringDexCacheType* strings,
            uint32_t num_strings,
            GcRoot<Class>* resolved_types,
            uint32_t num_resolved_types,
            ArtMethod** resolved_methods,
            uint32_t num_resolved_methods,
            ArtField** resolved_fields,
            uint32_t num_resolved_fields,
            PointerSize pointer_size) SHARED_REQUIRES(Locks::mutator_lock_);

  void Fixup(ArtMethod* trampoline, PointerSize pointer_size)
      SHARED_REQUIRES(Locks::mutator_lock_);

  template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier, typename Visitor>
  void FixupStrings(StringDexCacheType* dest, const Visitor& visitor)
      SHARED_REQUIRES(Locks::mutator_lock_);

  template <ReadBarrierOption kReadBarrierOption = kWithReadBarrier, typename Visitor>
  void FixupResolvedTypes(GcRoot<mirror::Class>* dest, const Visitor& visitor)
      SHARED_REQUIRES(Locks::mutator_lock_);

  String* GetLocation() SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldObject<String>(OFFSET_OF_OBJECT_MEMBER(DexCache, location_));
  }

  static MemberOffset DexOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, dex_);
  }

  static MemberOffset StringsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, strings_);
  }

  static MemberOffset ResolvedTypesOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, resolved_types_);
  }

  static MemberOffset ResolvedFieldsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, resolved_fields_);
  }

  static MemberOffset ResolvedMethodsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, resolved_methods_);
  }

  static MemberOffset NumStringsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, num_strings_);
  }

  static MemberOffset NumResolvedTypesOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, num_resolved_types_);
  }

  static MemberOffset NumResolvedFieldsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, num_resolved_fields_);
  }

  static MemberOffset NumResolvedMethodsOffset() {
    return OFFSET_OF_OBJECT_MEMBER(DexCache, num_resolved_methods_);
  }

  mirror::String* GetResolvedString(uint32_t string_idx) ALWAYS_INLINE
      SHARED_REQUIRES(Locks::mutator_lock_);

  void SetResolvedString(uint32_t string_idx, mirror::String* resolved) ALWAYS_INLINE
      SHARED_REQUIRES(Locks::mutator_lock_);

  Class* GetResolvedType(uint32_t type_idx) SHARED_REQUIRES(Locks::mutator_lock_);

  void SetResolvedType(uint32_t type_idx, Class* resolved) SHARED_REQUIRES(Locks::mutator_lock_);

  ALWAYS_INLINE ArtMethod* GetResolvedMethod(uint32_t method_idx, PointerSize ptr_size)
      SHARED_REQUIRES(Locks::mutator_lock_);

  ALWAYS_INLINE void SetResolvedMethod(uint32_t method_idx,
                                       ArtMethod* resolved,
                                       PointerSize ptr_size)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Pointer sized variant, used for patching.
  ALWAYS_INLINE ArtField* GetResolvedField(uint32_t idx, PointerSize ptr_size)
      SHARED_REQUIRES(Locks::mutator_lock_);

  // Pointer sized variant, used for patching.
  ALWAYS_INLINE void SetResolvedField(uint32_t idx, ArtField* field, PointerSize ptr_size)
      SHARED_REQUIRES(Locks::mutator_lock_);

  StringDexCacheType* GetStrings() ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldPtr64<StringDexCacheType*>(StringsOffset());
  }

  void SetStrings(StringDexCacheType* strings) ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    SetFieldPtr<false>(StringsOffset(), strings);
  }

  GcRoot<Class>* GetResolvedTypes() ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldPtr<GcRoot<Class>*>(ResolvedTypesOffset());
  }

  void SetResolvedTypes(GcRoot<Class>* resolved_types)
      ALWAYS_INLINE
      SHARED_REQUIRES(Locks::mutator_lock_) {
    SetFieldPtr<false>(ResolvedTypesOffset(), resolved_types);
  }

  ArtMethod** GetResolvedMethods() ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldPtr<ArtMethod**>(ResolvedMethodsOffset());
  }

  void SetResolvedMethods(ArtMethod** resolved_methods)
      ALWAYS_INLINE
      SHARED_REQUIRES(Locks::mutator_lock_) {
    SetFieldPtr<false>(ResolvedMethodsOffset(), resolved_methods);
  }

  ArtField** GetResolvedFields() ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldPtr<ArtField**>(ResolvedFieldsOffset());
  }

  void SetResolvedFields(ArtField** resolved_fields)
      ALWAYS_INLINE
      SHARED_REQUIRES(Locks::mutator_lock_) {
    SetFieldPtr<false>(ResolvedFieldsOffset(), resolved_fields);
  }

  size_t NumStrings() SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetField32(NumStringsOffset());
  }

  size_t NumResolvedTypes() SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetField32(NumResolvedTypesOffset());
  }

  size_t NumResolvedMethods() SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetField32(NumResolvedMethodsOffset());
  }

  size_t NumResolvedFields() SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetField32(NumResolvedFieldsOffset());
  }

  const DexFile* GetDexFile() ALWAYS_INLINE SHARED_REQUIRES(Locks::mutator_lock_) {
    return GetFieldPtr<const DexFile*>(OFFSET_OF_OBJECT_MEMBER(DexCache, dex_file_));
  }

  void SetDexFile(const DexFile* dex_file) SHARED_REQUIRES(Locks::mutator_lock_) {
    SetFieldPtr<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, dex_file_), dex_file);
  }

  void SetLocation(mirror::String* location) SHARED_REQUIRES(Locks::mutator_lock_);

  // NOTE: Get/SetElementPtrSize() are intended for working with ArtMethod** and ArtField**
  // provided by GetResolvedMethods/Fields() and ArtMethod::GetDexCacheResolvedMethods(),
  // so they need to be public.

  template <typename PtrType>
  static PtrType GetElementPtrSize(PtrType* ptr_array, size_t idx, PointerSize ptr_size);

  template <typename PtrType>
  static void SetElementPtrSize(PtrType* ptr_array, size_t idx, PtrType ptr, PointerSize ptr_size);

 private:
  // Visit instance fields of the dex cache as well as its associated arrays.
  template <bool kVisitNativeRoots,
            VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
            ReadBarrierOption kReadBarrierOption = kWithReadBarrier,
            typename Visitor>
  void VisitReferences(mirror::Class* klass, const Visitor& visitor)
      SHARED_REQUIRES(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  HeapReference<Object> dex_;
  HeapReference<String> location_;
  uint64_t dex_file_;           // const DexFile*
  uint64_t resolved_fields_;    // ArtField*, array with num_resolved_fields_ elements.
  uint64_t resolved_methods_;   // ArtMethod*, array with num_resolved_methods_ elements.
  uint64_t resolved_types_;     // GcRoot<Class>*, array with num_resolved_types_ elements.
  uint64_t strings_;            // std::atomic<StringDexCachePair>*,
                                // array with num_strings_ elements.
  uint32_t num_resolved_fields_;    // Number of elements in the resolved_fields_ array.
  uint32_t num_resolved_methods_;   // Number of elements in the resolved_methods_ array.
  uint32_t num_resolved_types_;     // Number of elements in the resolved_types_ array.
  uint32_t num_strings_;            // Number of elements in the strings_ array.

  friend struct art::DexCacheOffsets;  // for verifying offset information
  friend class Object;  // For VisitReferences
  DISALLOW_IMPLICIT_CONSTRUCTORS(DexCache);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_DEX_CACHE_H_
