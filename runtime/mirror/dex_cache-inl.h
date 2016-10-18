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

#ifndef ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_
#define ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_

#include "dex_cache.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/casts.h"
#include "base/enums.h"
#include "base/logging.h"
#include "mirror/class.h"
#include "mirror/method_type.h"
#include "runtime.h"
#include "obj_ptr.h"

#include <atomic>

namespace art {
namespace mirror {

inline uint32_t DexCache::ClassSize(PointerSize pointer_size) {
  uint32_t vtable_entries = Object::kVTableLength + 5;
  return Class::ComputeClassSize(true, vtable_entries, 0, 0, 0, 0, 0, pointer_size);
}

inline mirror::String* DexCache::GetResolvedString(uint32_t string_idx) {
  DCHECK_LT(string_idx, GetDexFile()->NumStringIds());
  return StringDexCachePair::Lookup(GetStrings(), string_idx, NumStrings()).Read();
}

inline void DexCache::SetResolvedString(uint32_t string_idx, mirror::String* resolved) {
  StringDexCachePair::Assign(GetStrings(), string_idx, resolved, NumStrings());
  Runtime* const runtime = Runtime::Current();
  if (UNLIKELY(runtime->IsActiveTransaction())) {
    DCHECK(runtime->IsAotCompiler());
    runtime->RecordResolveString(this, string_idx);
  }
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  runtime->GetHeap()->WriteBarrierEveryFieldOf(this);
}

inline void DexCache::ClearString(uint32_t string_idx) {
  const uint32_t slot_idx = string_idx % NumStrings();
  DCHECK(Runtime::Current()->IsAotCompiler());
  StringDexCacheType* slot = &GetStrings()[slot_idx];
  // This is racy but should only be called from the transactional interpreter.
  if (slot->load(std::memory_order_relaxed).index == string_idx) {
    StringDexCachePair cleared(
        nullptr,
        StringDexCachePair::InvalidIndexForSlot(slot_idx));
    slot->store(cleared, std::memory_order_relaxed);
  }
}

inline Class* DexCache::GetResolvedType(uint32_t type_idx) {
  DCHECK_LT(type_idx, NumResolvedTypes());
  return GetResolvedTypes()[type_idx].Read();
}

inline void DexCache::SetResolvedType(uint32_t type_idx, ObjPtr<Class> resolved) {
  DCHECK_LT(type_idx, NumResolvedTypes());  // NOTE: Unchecked, i.e. not throwing AIOOB.
  // TODO default transaction support.
  GetResolvedTypes()[type_idx] = GcRoot<Class>(resolved);
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  Runtime::Current()->GetHeap()->WriteBarrierEveryFieldOf(this);
}

inline MethodType* DexCache::GetResolvedMethodType(uint32_t proto_idx) {
  DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
  DCHECK_LT(proto_idx, GetDexFile()->NumProtoIds());
  return MethodTypeDexCachePair::Lookup(
      GetResolvedMethodTypes(), proto_idx, NumResolvedMethodTypes()).Read();
}

inline void DexCache::SetResolvedMethodType(uint32_t proto_idx, MethodType* resolved) {
  DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
  DCHECK_LT(proto_idx, GetDexFile()->NumProtoIds());

  MethodTypeDexCachePair::Assign(GetResolvedMethodTypes(), proto_idx, resolved,
                                 NumResolvedMethodTypes());
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  Runtime::Current()->GetHeap()->WriteBarrierEveryFieldOf(this);
}

inline ArtField* DexCache::GetResolvedField(uint32_t field_idx, PointerSize ptr_size) {
  DCHECK_EQ(Runtime::Current()->GetClassLinker()->GetImagePointerSize(), ptr_size);
  DCHECK_LT(field_idx, NumResolvedFields());  // NOTE: Unchecked, i.e. not throwing AIOOB.
  ArtField* field = GetElementPtrSize(GetResolvedFields(), field_idx, ptr_size);
  if (field == nullptr || field->GetDeclaringClass()->IsErroneous()) {
    return nullptr;
  }
  return field;
}

inline void DexCache::SetResolvedField(uint32_t field_idx, ArtField* field, PointerSize ptr_size) {
  DCHECK_EQ(Runtime::Current()->GetClassLinker()->GetImagePointerSize(), ptr_size);
  DCHECK_LT(field_idx, NumResolvedFields());  // NOTE: Unchecked, i.e. not throwing AIOOB.
  SetElementPtrSize(GetResolvedFields(), field_idx, field, ptr_size);
}

inline ArtMethod* DexCache::GetResolvedMethod(uint32_t method_idx, PointerSize ptr_size) {
  DCHECK_EQ(Runtime::Current()->GetClassLinker()->GetImagePointerSize(), ptr_size);
  DCHECK_LT(method_idx, NumResolvedMethods());  // NOTE: Unchecked, i.e. not throwing AIOOB.
  ArtMethod* method = GetElementPtrSize<ArtMethod*>(GetResolvedMethods(), method_idx, ptr_size);
  // Hide resolution trampoline methods from the caller
  if (method != nullptr && method->IsRuntimeMethod()) {
    DCHECK_EQ(method, Runtime::Current()->GetResolutionMethod());
    return nullptr;
  }
  return method;
}

inline void DexCache::SetResolvedMethod(uint32_t method_idx,
                                        ArtMethod* method,
                                        PointerSize ptr_size) {
  DCHECK_EQ(Runtime::Current()->GetClassLinker()->GetImagePointerSize(), ptr_size);
  DCHECK_LT(method_idx, NumResolvedMethods());  // NOTE: Unchecked, i.e. not throwing AIOOB.
  SetElementPtrSize(GetResolvedMethods(), method_idx, method, ptr_size);
}

template <typename PtrType>
inline PtrType DexCache::GetElementPtrSize(PtrType* ptr_array, size_t idx, PointerSize ptr_size) {
  if (ptr_size == PointerSize::k64) {
    uint64_t element = reinterpret_cast<const uint64_t*>(ptr_array)[idx];
    return reinterpret_cast<PtrType>(dchecked_integral_cast<uintptr_t>(element));
  } else {
    uint32_t element = reinterpret_cast<const uint32_t*>(ptr_array)[idx];
    return reinterpret_cast<PtrType>(dchecked_integral_cast<uintptr_t>(element));
  }
}

template <typename PtrType>
inline void DexCache::SetElementPtrSize(PtrType* ptr_array,
                                        size_t idx,
                                        PtrType ptr,
                                        PointerSize ptr_size) {
  if (ptr_size == PointerSize::k64) {
    reinterpret_cast<uint64_t*>(ptr_array)[idx] =
        dchecked_integral_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
  } else {
    reinterpret_cast<uint32_t*>(ptr_array)[idx] =
        dchecked_integral_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr));
  }
}

template <bool kVisitNativeRoots,
          VerifyObjectFlags kVerifyFlags,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor>
inline void DexCache::VisitReferences(ObjPtr<Class> klass, const Visitor& visitor) {
  // Visit instance fields first.
  VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
  // Visit arrays after.
  if (kVisitNativeRoots) {
    mirror::StringDexCacheType* strings = GetStrings();
    for (size_t i = 0, num_strings = NumStrings(); i != num_strings; ++i) {
      StringDexCachePair source = strings[i].load(std::memory_order_relaxed);
      mirror::String* before = source.object.Read<kReadBarrierOption>();
      GcRoot<mirror::String> root(before);
      visitor.VisitRootIfNonNull(root.AddressWithoutBarrier());
      if (root.Read() != before) {
        source.object = GcRoot<String>(root.Read());
        strings[i].store(source, std::memory_order_relaxed);
      }
    }
    GcRoot<mirror::Class>* resolved_types = GetResolvedTypes();
    for (size_t i = 0, num_types = NumResolvedTypes(); i != num_types; ++i) {
      visitor.VisitRootIfNonNull(resolved_types[i].AddressWithoutBarrier());
    }
  }
}

template <ReadBarrierOption kReadBarrierOption, typename Visitor>
inline void DexCache::FixupStrings(mirror::StringDexCacheType* dest, const Visitor& visitor) {
  mirror::StringDexCacheType* src = GetStrings();
  for (size_t i = 0, count = NumStrings(); i < count; ++i) {
    StringDexCachePair source = src[i].load(std::memory_order_relaxed);
    mirror::String* ptr = source.object.Read<kReadBarrierOption>();
    mirror::String* new_source = visitor(ptr);
    source.object = GcRoot<String>(new_source);
    dest[i].store(source, std::memory_order_relaxed);
  }
}

template <ReadBarrierOption kReadBarrierOption, typename Visitor>
inline void DexCache::FixupResolvedTypes(GcRoot<mirror::Class>* dest, const Visitor& visitor) {
  GcRoot<mirror::Class>* src = GetResolvedTypes();
  for (size_t i = 0, count = NumResolvedTypes(); i < count; ++i) {
    mirror::Class* source = src[i].Read<kReadBarrierOption>();
    mirror::Class* new_source = visitor(source);
    dest[i] = GcRoot<mirror::Class>(new_source);
  }
}

template <ReadBarrierOption kReadBarrierOption, typename Visitor>
inline void DexCache::FixupResolvedMethodTypes(mirror::MethodTypeDexCacheType* dest,
                                               const Visitor& visitor) {
  mirror::MethodTypeDexCacheType* src = GetResolvedMethodTypes();
  for (size_t i = 0, count = NumResolvedMethodTypes(); i < count; ++i) {
    MethodTypeDexCachePair source = src[i].load(std::memory_order_relaxed);
    mirror::MethodType* ptr = source.object.Read<kReadBarrierOption>();
    mirror::MethodType* new_source = visitor(ptr);
    source.object = GcRoot<MethodType>(new_source);
    dest[i].store(source, std::memory_order_relaxed);
  }
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_
