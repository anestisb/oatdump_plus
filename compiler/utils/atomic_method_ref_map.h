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

#ifndef ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_H_
#define ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_H_

#include "base/dchecked_vector.h"
#include "method_reference.h"
#include "safe_map.h"

namespace art {

class DexFile;

// Used by CompilerCallbacks to track verification information from the Runtime.
template <typename T>
class AtomicMethodRefMap {
 public:
  explicit AtomicMethodRefMap() {}
  ~AtomicMethodRefMap() {}

  // Atomically swap the element in if the existing value matches expected.
  enum InsertResult {
    kInsertResultInvalidDexFile,
    kInsertResultCASFailure,
    kInsertResultSuccess,
  };
  InsertResult Insert(MethodReference ref, const T& expected, const T& desired);

  // Retreive an item, returns false if the dex file is not added.
  bool Get(MethodReference ref, T* out) const;

  // Dex files must be added before method references belonging to them can be used as keys. Not
  // thread safe.
  void AddDexFile(const DexFile* dex_file);

  bool HaveDexFile(const DexFile* dex_file) const {
    return arrays_.find(dex_file) != arrays_.end();
  }

  // Visit all of the dex files and elements.
  template <typename Visitor>
  void Visit(const Visitor& visitor);

  void ClearEntries();

 private:
  // Verified methods. The method array is fixed to avoid needing a lock to extend it.
  using ElementArray = dchecked_vector<Atomic<T>>;
  using DexFileArrays = SafeMap<const DexFile*, ElementArray>;

  const ElementArray* GetArray(const DexFile* dex_file) const;
  ElementArray* GetArray(const DexFile* dex_file);

  DexFileArrays arrays_;
};

}  // namespace art

#endif  // ART_COMPILER_UTILS_ATOMIC_METHOD_REF_MAP_H_
