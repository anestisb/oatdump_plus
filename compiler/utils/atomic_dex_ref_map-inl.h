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

#ifndef ART_COMPILER_UTILS_ATOMIC_DEX_REF_MAP_INL_H_
#define ART_COMPILER_UTILS_ATOMIC_DEX_REF_MAP_INL_H_

#include "atomic_dex_ref_map.h"

#include "dex_file-inl.h"

namespace art {

template <typename T>
inline typename AtomicDexRefMap<T>::InsertResult AtomicDexRefMap<T>::Insert(
    DexFileReference ref,
    const T& expected,
    const T& desired) {
  ElementArray* const array = GetArray(ref.dex_file);
  if (array == nullptr) {
    return kInsertResultInvalidDexFile;
  }
  DCHECK_LT(ref.index, array->size());
  return (*array)[ref.index].CompareExchangeStrongSequentiallyConsistent(expected, desired)
      ? kInsertResultSuccess
      : kInsertResultCASFailure;
}

template <typename T>
inline bool AtomicDexRefMap<T>::Get(DexFileReference ref, T* out) const {
  const ElementArray* const array = GetArray(ref.dex_file);
  if (array == nullptr) {
    return false;
  }
  *out = (*array)[ref.index].LoadRelaxed();
  return true;
}

template <typename T>
inline void AtomicDexRefMap<T>::AddDexFile(const DexFile* dex_file, size_t max_index) {
  arrays_.Put(dex_file, std::move(ElementArray(max_index)));
}

template <typename T>
inline typename AtomicDexRefMap<T>::ElementArray* AtomicDexRefMap<T>::GetArray(
    const DexFile* dex_file) {
  auto it = arrays_.find(dex_file);
  return (it != arrays_.end()) ? &it->second : nullptr;
}

template <typename T>
inline const typename AtomicDexRefMap<T>::ElementArray* AtomicDexRefMap<T>::GetArray(
    const DexFile* dex_file) const {
  auto it = arrays_.find(dex_file);
  return (it != arrays_.end()) ? &it->second : nullptr;
}

template <typename T> template <typename Visitor>
inline void AtomicDexRefMap<T>::Visit(const Visitor& visitor) {
  for (auto& pair : arrays_) {
    const DexFile* dex_file = pair.first;
    const ElementArray& elements = pair.second;
    for (size_t i = 0; i < elements.size(); ++i) {
      visitor(DexFileReference(dex_file, i), elements[i].LoadRelaxed());
    }
  }
}

template <typename T>
inline void AtomicDexRefMap<T>::ClearEntries() {
  for (auto& it : arrays_) {
    for (auto& element : it.second) {
      element.StoreRelaxed(nullptr);
    }
  }
}

}  // namespace art

#endif  // ART_COMPILER_UTILS_ATOMIC_DEX_REF_MAP_INL_H_
