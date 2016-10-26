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

#ifndef ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_
#define ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_

#include <map>
#include <set>
#include <vector>

#include "art_field.h"
#include "art_method.h"
#include "base/array_ref.h"
#include "base/mutex.h"
#include "method_resolution_kind.h"
#include "method_verifier.h"  // For MethodVerifier::FailureKind.
#include "obj_ptr.h"
#include "os.h"

namespace art {
namespace verifier {

// Verification dependencies collector class used by the MethodVerifier to record
// resolution outcomes and type assignability tests of classes/methods/fields
// not present in the set of compiled DEX files, that is classes/methods/fields
// defined in the classpath.
// The compilation driver initializes the class and registers all DEX files
// which are being compiled. Classes defined in DEX files outside of this set
// (or synthesized classes without associated DEX files) are considered being
// in the classpath.
// During code-flow verification, the MethodVerifier informs the VerifierDeps
// singleton about the outcome of every resolution and assignability test, and
// the singleton records them if their outcome may change with changes in the
// classpath.
class VerifierDeps {
 public:
  explicit VerifierDeps(const std::vector<const DexFile*>& dex_files)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the verification status of the class at `type_idx`.
  static void MaybeRecordVerificationStatus(const DexFile& dex_file,
                                            uint16_t type_idx,
                                            MethodVerifier::FailureKind failure_kind)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the outcome `klass` of resolving type `type_idx` from `dex_file`.
  // If `klass` is null, the class is assumed unresolved.
  static void MaybeRecordClassResolution(const DexFile& dex_file,
                                         uint16_t type_idx,
                                         mirror::Class* klass)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the outcome `field` of resolving field `field_idx` from `dex_file`.
  // If `field` is null, the field is assumed unresolved.
  static void MaybeRecordFieldResolution(const DexFile& dex_file,
                                         uint32_t field_idx,
                                         ArtField* field)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the outcome `method` of resolving method `method_idx` from `dex_file`
  // using `res_kind` kind of method resolution algorithm. If `method` is null,
  // the method is assumed unresolved.
  static void MaybeRecordMethodResolution(const DexFile& dex_file,
                                          uint32_t method_idx,
                                          MethodResolutionKind res_kind,
                                          ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the outcome `is_assignable` of type assignability test from `source`
  // to `destination` as defined by RegType::AssignableFrom. `dex_file` is the
  // owner of the method for which MethodVerifier performed the assignability test.
  static void MaybeRecordAssignability(const DexFile& dex_file,
                                       mirror::Class* destination,
                                       mirror::Class* source,
                                       bool is_strict,
                                       bool is_assignable)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Serialize the recorded dependencies and store the data into `buffer`.
  void Encode(std::vector<uint8_t>* buffer) const
      REQUIRES(!Locks::verifier_deps_lock_);

 private:
  static constexpr uint16_t kUnresolvedMarker = static_cast<uint16_t>(-1);

  // Only used in tests to reconstruct the data structure from serialized data.
  VerifierDeps(const std::vector<const DexFile*>& dex_files, ArrayRef<uint8_t> data)
      REQUIRES(!Locks::verifier_deps_lock_);

  using ClassResolutionBase = std::tuple<uint32_t, uint16_t>;
  struct ClassResolution : public ClassResolutionBase {
    ClassResolution() = default;
    ClassResolution(const ClassResolution&) = default;
    ClassResolution(uint32_t type_idx, uint16_t access_flags)
        : ClassResolutionBase(type_idx, access_flags) {}

    bool IsResolved() const { return GetAccessFlags() != kUnresolvedMarker; }
    uint32_t GetDexTypeIndex() const { return std::get<0>(*this); }
    uint16_t GetAccessFlags() const { return std::get<1>(*this); }
  };

  using FieldResolutionBase = std::tuple<uint32_t, uint16_t, uint32_t>;
  struct FieldResolution : public FieldResolutionBase {
    FieldResolution() = default;
    FieldResolution(const FieldResolution&) = default;
    FieldResolution(uint32_t field_idx, uint16_t access_flags, uint32_t declaring_class_idx)
        : FieldResolutionBase(field_idx, access_flags, declaring_class_idx) {}

    bool IsResolved() const { return GetAccessFlags() != kUnresolvedMarker; }
    uint32_t GetDexFieldIndex() const { return std::get<0>(*this); }
    uint16_t GetAccessFlags() const { return std::get<1>(*this); }
    uint32_t GetDeclaringClassIndex() const { return std::get<2>(*this); }
  };

  using MethodResolutionBase = std::tuple<uint32_t, uint16_t, uint32_t>;
  struct MethodResolution : public MethodResolutionBase {
    MethodResolution() = default;
    MethodResolution(const MethodResolution&) = default;
    MethodResolution(uint32_t method_idx, uint16_t access_flags, uint32_t declaring_class_idx)
        : MethodResolutionBase(method_idx, access_flags, declaring_class_idx) {}

    bool IsResolved() const { return GetAccessFlags() != kUnresolvedMarker; }
    uint32_t GetDexMethodIndex() const { return std::get<0>(*this); }
    uint16_t GetAccessFlags() const { return std::get<1>(*this); }
    uint32_t GetDeclaringClassIndex() const { return std::get<2>(*this); }
  };

  using TypeAssignabilityBase = std::tuple<uint32_t, uint32_t>;
  struct TypeAssignability : public TypeAssignabilityBase {
    TypeAssignability() = default;
    TypeAssignability(const TypeAssignability&) = default;
    TypeAssignability(uint32_t destination_idx, uint32_t source_idx)
        : TypeAssignabilityBase(destination_idx, source_idx) {}

    uint32_t GetDestination() const { return std::get<0>(*this); }
    uint32_t GetSource() const { return std::get<1>(*this); }
  };

  // Data structure representing dependencies collected during verification of
  // methods inside one DexFile.
  struct DexFileDeps {
    // Vector of strings which are not present in the corresponding DEX file.
    // These are referred to with ids starting with `NumStringIds()` of that DexFile.
    std::vector<std::string> strings_;

    // Set of class pairs recording the outcome of assignability test from one
    // of the two types to the other.
    std::set<TypeAssignability> assignable_types_;
    std::set<TypeAssignability> unassignable_types_;

    // Sets of recorded class/field/method resolutions.
    std::set<ClassResolution> classes_;
    std::set<FieldResolution> fields_;
    std::set<MethodResolution> direct_methods_;
    std::set<MethodResolution> virtual_methods_;
    std::set<MethodResolution> interface_methods_;

    // List of classes that were not fully verified in that dex file.
    std::vector<uint16_t> unverified_classes_;

    bool Equals(const DexFileDeps& rhs) const;
  };

  // Finds the DexFileDep instance associated with `dex_file`, or nullptr if
  // `dex_file` is not reported as being compiled.
  // We disable thread safety analysis. The method only reads the key set of
  // `dex_deps_` which stays constant after initialization.
  DexFileDeps* GetDexFileDeps(const DexFile& dex_file)
      NO_THREAD_SAFETY_ANALYSIS;

  // Returns true if `klass` is null or not defined in any of dex files which
  // were reported as being compiled.
  bool IsInClassPath(ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns the index of `str`. If it is defined in `dex_file_`, this is the dex
  // string ID. If not, an ID is assigned to the string and cached in `strings_`
  // of the corresponding DexFileDeps structure (either provided or inferred from
  // `dex_file`).
  uint32_t GetIdFromString(const DexFile& dex_file, const std::string& str)
      REQUIRES(Locks::verifier_deps_lock_);

  // Returns the string represented by `id`.
  std::string GetStringFromId(const DexFile& dex_file, uint32_t string_id)
      REQUIRES(Locks::verifier_deps_lock_);

  // Returns the bytecode access flags of `element` (bottom 16 bits), or
  // `kUnresolvedMarker` if `element` is null.
  template <typename T>
  uint16_t GetAccessFlags(T* element)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns a string ID of the descriptor of the declaring class of `element`,
  // or `kUnresolvedMarker` if `element` is null.
  template <typename T>
  uint32_t GetDeclaringClassStringId(const DexFile& dex_file, T* element)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::verifier_deps_lock_);

  void AddClassResolution(const DexFile& dex_file,
                          uint16_t type_idx,
                          mirror::Class* klass)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  void AddFieldResolution(const DexFile& dex_file,
                          uint32_t field_idx,
                          ArtField* field)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  void AddMethodResolution(const DexFile& dex_file,
                           uint32_t method_idx,
                           MethodResolutionKind res_kind,
                           ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  void AddAssignability(const DexFile& dex_file,
                        mirror::Class* destination,
                        mirror::Class* source,
                        bool is_strict,
                        bool is_assignable)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  bool Equals(const VerifierDeps& rhs) const
      REQUIRES(!Locks::verifier_deps_lock_);

  // Map from DexFiles into dependencies collected from verification of their methods.
  std::map<const DexFile*, std::unique_ptr<DexFileDeps>> dex_deps_
      GUARDED_BY(Locks::verifier_deps_lock_);

  friend class VerifierDepsTest;
  ART_FRIEND_TEST(VerifierDepsTest, StringToId);
  ART_FRIEND_TEST(VerifierDepsTest, EncodeDecode);
};

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_
