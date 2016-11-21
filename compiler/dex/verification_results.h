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

#ifndef ART_COMPILER_DEX_VERIFICATION_RESULTS_H_
#define ART_COMPILER_DEX_VERIFICATION_RESULTS_H_

#include <stdint.h>
#include <set>

#include "base/dchecked_vector.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "class_reference.h"
#include "method_reference.h"
#include "safe_map.h"

namespace art {

namespace verifier {
class MethodVerifier;
}  // namespace verifier

class CompilerOptions;
class VerifiedMethod;

// Used by CompilerCallbacks to track verification information from the Runtime.
class VerificationResults {
 public:
  explicit VerificationResults(const CompilerOptions* compiler_options);
  ~VerificationResults();

  void ProcessVerifiedMethod(verifier::MethodVerifier* method_verifier)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!verified_methods_lock_);

  const VerifiedMethod* GetVerifiedMethod(MethodReference ref)
      REQUIRES(!verified_methods_lock_);

  void AddRejectedClass(ClassReference ref) REQUIRES(!rejected_classes_lock_);
  bool IsClassRejected(ClassReference ref) REQUIRES(!rejected_classes_lock_);

  bool IsCandidateForCompilation(MethodReference& method_ref, const uint32_t access_flags);

  // Add a dex file array to the preregistered_dex_files_ array. These dex files require no locks to
  // access. It is not safe to call if other callers are calling GetVerifiedMethod concurrently.
  void PreRegisterDexFile(const DexFile* dex_file) REQUIRES(!verified_methods_lock_);

 private:
  // Verified methods. The method array is fixed to avoid needing a lock to extend it.
  using DexFileMethodArray = dchecked_vector<Atomic<const VerifiedMethod*>>;
  using DexFileResults = std::map<const DexFile*, DexFileMethodArray>;
  using VerifiedMethodMap = SafeMap<MethodReference,
                                    const VerifiedMethod*,
                                    MethodReferenceComparator>;

  static void DeleteResults(DexFileResults& array);

  DexFileMethodArray* GetMethodArray(const DexFile* dex_file) REQUIRES(!verified_methods_lock_);
  VerifiedMethodMap verified_methods_ GUARDED_BY(verified_methods_lock_);
  const CompilerOptions* const compiler_options_;

  // Dex2oat can preregister dex files to avoid locking when calling GetVerifiedMethod.
  DexFileResults preregistered_dex_files_;

  ReaderWriterMutex verified_methods_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // Rejected classes.
  ReaderWriterMutex rejected_classes_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  std::set<ClassReference> rejected_classes_ GUARDED_BY(rejected_classes_lock_);
};

}  // namespace art

#endif  // ART_COMPILER_DEX_VERIFICATION_RESULTS_H_
