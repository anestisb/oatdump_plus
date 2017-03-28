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

#ifndef ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_INL_H_
#define ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_INL_H_

#include "atomic_method_ref_map.h"

#include "dex_file-inl.h"

namespace art {

template <typename T>
inline typename AtomicMethodRefMap<T>::InsertResult AtomicMethodRefMap<T>::Insert(
    MethodReference ref,
    const T& expected,
    const T& desired) {
  ElementArray* const array = GetArray(ref.dex_file);
  if (array == nullptr) {
    return kInsertResultInvalidDexFile;
  }
  return (*array)[ref.dex_method_index].CompareExchangeStrongSequentiallyConsistent(
      expected, desired)
      ? kInsertResultSuccess
      : kInsertResultCASFailure;
}

template <typename T>
inline bool AtomicMethodRefMap<T>::Get(MethodReference ref, T* out) const {
  const ElementArray* const array = GetArray(ref.dex_file);
  if (array == nullptr) {
    return false;
  }
  *out = (*array)[ref.dex_method_index].LoadRelaxed();
  return true;
}

template <typename T>
inline void AtomicMethodRefMap<T>::AddDexFile(const DexFile* dex_file) {
  arrays_.Put(dex_file, std::move(ElementArray(dex_file->NumMethodIds())));
}

template <typename T>
inline typename AtomicMethodRefMap<T>::ElementArray* AtomicMethodRefMap<T>::GetArray(
    const DexFile* dex_file) {
  auto it = arrays_.find(dex_file);
  return (it != arrays_.end()) ? &it->second : nullptr;
}

template <typename T>
inline const typename AtomicMethodRefMap<T>::ElementArray* AtomicMethodRefMap<T>::GetArray(
    const DexFile* dex_file) const {
  auto it = arrays_.find(dex_file);
  return (it != arrays_.end()) ? &it->second : nullptr;
}

template <typename T> template <typename Visitor>
inline void AtomicMethodRefMap<T>::Visit(const Visitor& visitor) {
  for (auto& pair : arrays_) {
    const DexFile* dex_file = pair.first;
    const ElementArray& elements = pair.second;
    for (size_t i = 0; i < elements.size(); ++i) {
      visitor(MethodReference(dex_file, i), elements[i].LoadRelaxed());
    }
  }
}

template <typename T>
inline void AtomicMethodRefMap<T>::ClearEntries() {
  for (auto& it : arrays_) {
    for (auto& element : it.second) {
      element.StoreRelaxed(nullptr);
    }
  }
}

}  // namespace art

#endif  // ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_INL_H_
