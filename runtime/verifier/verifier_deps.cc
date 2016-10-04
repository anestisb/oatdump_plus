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

#include "verifier_deps.h"

#include "compiler_callbacks.h"
#include "leb128.h"
#include "mirror/class-inl.h"
#include "obj_ptr-inl.h"
#include "runtime.h"

namespace art {
namespace verifier {

VerifierDeps::VerifierDeps(const std::vector<const DexFile*>& dex_files) {
  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
  for (const DexFile* dex_file : dex_files) {
    DCHECK(GetDexFileDeps(*dex_file) == nullptr);
    std::unique_ptr<DexFileDeps> deps(new DexFileDeps());
    dex_deps_.emplace(dex_file, std::move(deps));
  }
}

VerifierDeps::DexFileDeps* VerifierDeps::GetDexFileDeps(const DexFile& dex_file) {
  auto it = dex_deps_.find(&dex_file);
  return (it == dex_deps_.end()) ? nullptr : it->second.get();
}

template <typename T>
uint16_t VerifierDeps::GetAccessFlags(T* element) {
  static_assert(kAccJavaFlagsMask == 0xFFFF, "Unexpected value of a constant");
  if (element == nullptr) {
    return VerifierDeps::kUnresolvedMarker;
  } else {
    uint16_t access_flags = Low16Bits(element->GetAccessFlags());
    CHECK_NE(access_flags, VerifierDeps::kUnresolvedMarker);
    return access_flags;
  }
}

template <typename T>
uint32_t VerifierDeps::GetDeclaringClassStringId(const DexFile& dex_file, T* element) {
  static_assert(kAccJavaFlagsMask == 0xFFFF, "Unexpected value of a constant");
  if (element == nullptr) {
    return VerifierDeps::kUnresolvedMarker;
  } else {
    std::string temp;
    uint32_t string_id = GetIdFromString(
        dex_file, element->GetDeclaringClass()->GetDescriptor(&temp));
    return string_id;
  }
}

uint32_t VerifierDeps::GetIdFromString(const DexFile& dex_file, const std::string& str) {
  const DexFile::StringId* string_id = dex_file.FindStringId(str.c_str());
  if (string_id != nullptr) {
    // String is in the DEX file. Return its ID.
    return dex_file.GetIndexForStringId(*string_id);
  }

  // String is not in the DEX file. Assign a new ID to it which is higher than
  // the number of strings in the DEX file.

  DexFileDeps* deps = GetDexFileDeps(dex_file);
  DCHECK(deps != nullptr);

  uint32_t num_ids_in_dex = dex_file.NumStringIds();
  uint32_t num_extra_ids = deps->strings_.size();

  for (size_t i = 0; i < num_extra_ids; ++i) {
    if (deps->strings_[i] == str) {
      return num_ids_in_dex + i;
    }
  }

  deps->strings_.push_back(str);

  uint32_t new_id = num_ids_in_dex + num_extra_ids;
  CHECK_GE(new_id, num_ids_in_dex);  // check for overflows
  DCHECK_EQ(str, GetStringFromId(dex_file, new_id));

  return new_id;
}

std::string VerifierDeps::GetStringFromId(const DexFile& dex_file, uint32_t string_id) {
  uint32_t num_ids_in_dex = dex_file.NumStringIds();
  if (string_id < num_ids_in_dex) {
    return std::string(dex_file.StringDataByIdx(string_id));
  } else {
    DexFileDeps* deps = GetDexFileDeps(dex_file);
    DCHECK(deps != nullptr);
    string_id -= num_ids_in_dex;
    CHECK_LT(string_id, deps->strings_.size());
    return deps->strings_[string_id];
  }
}

bool VerifierDeps::IsInClassPath(ObjPtr<mirror::Class> klass) {
  DCHECK(klass != nullptr);

  ObjPtr<mirror::DexCache> dex_cache = klass->GetDexCache();
  if (dex_cache == nullptr) {
    // This is a synthesized class, in this case always an array. They are not
    // defined in the compiled DEX files and therefore are part of the classpath.
    // We could avoid recording dependencies on arrays with component types in
    // the compiled DEX files but we choose to record them anyway so as to
    // record the access flags VM sets for array classes.
    DCHECK(klass->IsArrayClass()) << PrettyDescriptor(klass);
    return true;
  }

  const DexFile* dex_file = dex_cache->GetDexFile();
  DCHECK(dex_file != nullptr);

  // Test if the `dex_deps_` contains an entry for `dex_file`. If not, the dex
  // file was not registered as being compiled and we assume `klass` is in the
  // classpath.
  return (GetDexFileDeps(*dex_file) == nullptr);
}

void VerifierDeps::AddClassResolution(const DexFile& dex_file,
                                      uint16_t type_idx,
                                      mirror::Class* klass) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a dex file which is not being compiled.
    return;
  }

  if (klass != nullptr && !IsInClassPath(klass)) {
    // Class resolved into one of the DEX files which are being compiled.
    // This is not a classpath dependency.
    return;
  }

  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
  dex_deps->classes_.emplace(ClassResolution(type_idx, GetAccessFlags(klass)));
}

void VerifierDeps::AddFieldResolution(const DexFile& dex_file,
                                      uint32_t field_idx,
                                      ArtField* field) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a dex file which is not being compiled.
    return;
  }

  if (field != nullptr && !IsInClassPath(field->GetDeclaringClass())) {
    // Field resolved into one of the DEX files which are being compiled.
    // This is not a classpath dependency.
    return;
  }

  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
  dex_deps->fields_.emplace(FieldResolution(
      field_idx, GetAccessFlags(field), GetDeclaringClassStringId(dex_file, field)));
}

void VerifierDeps::AddMethodResolution(const DexFile& dex_file,
                                       uint32_t method_idx,
                                       MethodResolutionKind resolution_kind,
                                       ArtMethod* method) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a dex file which is not being compiled.
    return;
  }

  if (method != nullptr && !IsInClassPath(method->GetDeclaringClass())) {
    // Method resolved into one of the DEX files which are being compiled.
    // This is not a classpath dependency.
    return;
  }

  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
  MethodResolution method_tuple(method_idx,
                                GetAccessFlags(method),
                                GetDeclaringClassStringId(dex_file, method));
  if (resolution_kind == kDirectMethodResolution) {
    dex_deps->direct_methods_.emplace(method_tuple);
  } else if (resolution_kind == kVirtualMethodResolution) {
    dex_deps->virtual_methods_.emplace(method_tuple);
  } else {
    DCHECK_EQ(resolution_kind, kInterfaceMethodResolution);
    dex_deps->interface_methods_.emplace(method_tuple);
  }
}

void VerifierDeps::AddAssignability(const DexFile& dex_file,
                                    mirror::Class* destination,
                                    mirror::Class* source,
                                    bool is_strict,
                                    bool is_assignable) {
  // Test that the method is only called on reference types.
  // Note that concurrent verification of `destination` and `source` may have
  // set their status to erroneous. However, the tests performed below rely
  // merely on no issues with linking (valid access flags, superclass and
  // implemented interfaces). If the class at any point reached the IsResolved
  // status, the requirement holds. This is guaranteed by RegTypeCache::ResolveClass.
  DCHECK(destination != nullptr && !destination->IsPrimitive());
  DCHECK(source != nullptr && !source->IsPrimitive());

  if (destination == source ||
      destination->IsObjectClass() ||
      (!is_strict && destination->IsInterface())) {
    // Cases when `destination` is trivially assignable from `source`.
    DCHECK(is_assignable);
    return;
  }

  DCHECK_EQ(is_assignable, destination->IsAssignableFrom(source));

  if (destination->IsArrayClass() && source->IsArrayClass()) {
    // Both types are arrays. Break down to component types and add recursively.
    // This helps filter out destinations from compiled DEX files (see below)
    // and deduplicate entries with the same canonical component type.
    mirror::Class* destination_component = destination->GetComponentType();
    mirror::Class* source_component = source->GetComponentType();

    // Only perform the optimization if both types are resolved which guarantees
    // that they linked successfully, as required at the top of this method.
    if (destination_component->IsResolved() && source_component->IsResolved()) {
      AddAssignability(dex_file,
                       destination_component,
                       source_component,
                       /* is_strict */ true,
                       is_assignable);
      return;
    }
  }

  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a DEX file which is not being compiled.
    return;
  }

  if (!IsInClassPath(destination) && !IsInClassPath(source)) {
    // Both `destination` and `source` are defined in the compiled DEX files.
    // No need to record a dependency.
    return;
  }

  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);

  // Get string IDs for both descriptors and store in the appropriate set.

  std::string temp1, temp2;
  std::string destination_desc(destination->GetDescriptor(&temp1));
  std::string source_desc(source->GetDescriptor(&temp2));
  uint32_t destination_id = GetIdFromString(dex_file, destination_desc);
  uint32_t source_id = GetIdFromString(dex_file, source_desc);

  if (is_assignable) {
    dex_deps->assignable_types_.emplace(TypeAssignability(destination_id, source_id));
  } else {
    dex_deps->unassignable_types_.emplace(TypeAssignability(destination_id, source_id));
  }
}

static inline VerifierDeps* GetVerifierDepsSingleton() {
  CompilerCallbacks* callbacks = Runtime::Current()->GetCompilerCallbacks();
  if (callbacks == nullptr) {
    return nullptr;
  }
  return callbacks->GetVerifierDeps();
}

void VerifierDeps::MaybeRecordClassResolution(const DexFile& dex_file,
                                              uint16_t type_idx,
                                              mirror::Class* klass) {
  VerifierDeps* singleton = GetVerifierDepsSingleton();
  if (singleton != nullptr) {
    singleton->AddClassResolution(dex_file, type_idx, klass);
  }
}

void VerifierDeps::MaybeRecordFieldResolution(const DexFile& dex_file,
                                              uint32_t field_idx,
                                              ArtField* field) {
  VerifierDeps* singleton = GetVerifierDepsSingleton();
  if (singleton != nullptr) {
    singleton->AddFieldResolution(dex_file, field_idx, field);
  }
}

void VerifierDeps::MaybeRecordMethodResolution(const DexFile& dex_file,
                                               uint32_t method_idx,
                                               MethodResolutionKind resolution_kind,
                                               ArtMethod* method) {
  VerifierDeps* singleton = GetVerifierDepsSingleton();
  if (singleton != nullptr) {
    singleton->AddMethodResolution(dex_file, method_idx, resolution_kind, method);
  }
}

void VerifierDeps::MaybeRecordAssignability(const DexFile& dex_file,
                                            mirror::Class* destination,
                                            mirror::Class* source,
                                            bool is_strict,
                                            bool is_assignable) {
  VerifierDeps* singleton = GetVerifierDepsSingleton();
  if (singleton != nullptr) {
    singleton->AddAssignability(dex_file, destination, source, is_strict, is_assignable);
  }
}

static inline uint32_t DecodeUint32WithOverflowCheck(const uint8_t** in, const uint8_t* end) {
  CHECK_LT(*in, end);
  return DecodeUnsignedLeb128(in);
}

template<typename T1, typename T2>
static inline void EncodeTuple(std::vector<uint8_t>* out, const std::tuple<T1, T2>& t) {
  EncodeUnsignedLeb128(out, std::get<0>(t));
  EncodeUnsignedLeb128(out, std::get<1>(t));
}

template<typename T1, typename T2>
static inline void DecodeTuple(const uint8_t** in, const uint8_t* end, std::tuple<T1, T2>* t) {
  T1 v1 = static_cast<T1>(DecodeUint32WithOverflowCheck(in, end));
  T2 v2 = static_cast<T2>(DecodeUint32WithOverflowCheck(in, end));
  *t = std::make_tuple(v1, v2);
}

template<typename T1, typename T2, typename T3>
static inline void EncodeTuple(std::vector<uint8_t>* out, const std::tuple<T1, T2, T3>& t) {
  EncodeUnsignedLeb128(out, std::get<0>(t));
  EncodeUnsignedLeb128(out, std::get<1>(t));
  EncodeUnsignedLeb128(out, std::get<2>(t));
}

template<typename T1, typename T2, typename T3>
static inline void DecodeTuple(const uint8_t** in, const uint8_t* end, std::tuple<T1, T2, T3>* t) {
  T1 v1 = static_cast<T1>(DecodeUint32WithOverflowCheck(in, end));
  T2 v2 = static_cast<T2>(DecodeUint32WithOverflowCheck(in, end));
  T3 v3 = static_cast<T2>(DecodeUint32WithOverflowCheck(in, end));
  *t = std::make_tuple(v1, v2, v3);
}

template<typename T>
static inline void EncodeSet(std::vector<uint8_t>* out, const std::set<T>& set) {
  EncodeUnsignedLeb128(out, set.size());
  for (const T& entry : set) {
    EncodeTuple(out, entry);
  }
}

template<typename T>
static inline void DecodeSet(const uint8_t** in, const uint8_t* end, std::set<T>* set) {
  DCHECK(set->empty());
  size_t num_entries = DecodeUint32WithOverflowCheck(in, end);
  for (size_t i = 0; i < num_entries; ++i) {
    T tuple;
    DecodeTuple(in, end, &tuple);
    set->emplace(tuple);
  }
}

static inline void EncodeStringVector(std::vector<uint8_t>* out,
                                      const std::vector<std::string>& strings) {
  EncodeUnsignedLeb128(out, strings.size());
  for (const std::string& str : strings) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(str.c_str());
    size_t length = str.length() + 1;
    out->insert(out->end(), data, data + length);
    DCHECK_EQ(0u, out->back());
  }
}

static inline void DecodeStringVector(const uint8_t** in,
                                      const uint8_t* end,
                                      std::vector<std::string>* strings) {
  DCHECK(strings->empty());
  size_t num_strings = DecodeUint32WithOverflowCheck(in, end);
  strings->reserve(num_strings);
  for (size_t i = 0; i < num_strings; ++i) {
    CHECK_LT(*in, end);
    const char* string_start = reinterpret_cast<const char*>(*in);
    strings->emplace_back(std::string(string_start));
    *in += strings->back().length() + 1;
  }
}

void VerifierDeps::Encode(std::vector<uint8_t>* buffer) const {
  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
  for (auto& entry : dex_deps_) {
    EncodeStringVector(buffer, entry.second->strings_);
    EncodeSet(buffer, entry.second->assignable_types_);
    EncodeSet(buffer, entry.second->unassignable_types_);
    EncodeSet(buffer, entry.second->classes_);
    EncodeSet(buffer, entry.second->fields_);
    EncodeSet(buffer, entry.second->direct_methods_);
    EncodeSet(buffer, entry.second->virtual_methods_);
    EncodeSet(buffer, entry.second->interface_methods_);
  }
}

VerifierDeps::VerifierDeps(const std::vector<const DexFile*>& dex_files, ArrayRef<uint8_t> data)
    : VerifierDeps(dex_files) {
  const uint8_t* data_start = data.data();
  const uint8_t* data_end = data_start + data.size();
  for (auto& entry : dex_deps_) {
    DecodeStringVector(&data_start, data_end, &entry.second->strings_);
    DecodeSet(&data_start, data_end, &entry.second->assignable_types_);
    DecodeSet(&data_start, data_end, &entry.second->unassignable_types_);
    DecodeSet(&data_start, data_end, &entry.second->classes_);
    DecodeSet(&data_start, data_end, &entry.second->fields_);
    DecodeSet(&data_start, data_end, &entry.second->direct_methods_);
    DecodeSet(&data_start, data_end, &entry.second->virtual_methods_);
    DecodeSet(&data_start, data_end, &entry.second->interface_methods_);
  }
  CHECK_LE(data_start, data_end);
}

bool VerifierDeps::Equals(const VerifierDeps& rhs) const {
  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);

  if (dex_deps_.size() != rhs.dex_deps_.size()) {
    return false;
  }

  auto lhs_it = dex_deps_.begin();
  auto rhs_it = rhs.dex_deps_.begin();

  for (; (lhs_it != dex_deps_.end()) && (rhs_it != rhs.dex_deps_.end()); lhs_it++, rhs_it++) {
    const DexFile* lhs_dex_file = lhs_it->first;
    const DexFile* rhs_dex_file = rhs_it->first;
    if (lhs_dex_file != rhs_dex_file) {
      return false;
    }

    DexFileDeps* lhs_deps = lhs_it->second.get();
    DexFileDeps* rhs_deps = rhs_it->second.get();
    if (!lhs_deps->Equals(*rhs_deps)) {
      return false;
    }
  }

  DCHECK((lhs_it == dex_deps_.end()) && (rhs_it == rhs.dex_deps_.end()));
  return true;
}

bool VerifierDeps::DexFileDeps::Equals(const VerifierDeps::DexFileDeps& rhs) const {
  return (strings_ == rhs.strings_) &&
         (assignable_types_ == rhs.assignable_types_) &&
         (unassignable_types_ == rhs.unassignable_types_) &&
         (classes_ == rhs.classes_) &&
         (fields_ == rhs.fields_) &&
         (direct_methods_ == rhs.direct_methods_) &&
         (virtual_methods_ == rhs.virtual_methods_) &&
         (interface_methods_ == rhs.interface_methods_);
}

}  // namespace verifier
}  // namespace art
