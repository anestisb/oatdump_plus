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

#include "quick_compiler_callbacks.h"

#include "driver/compiler_driver.h"
#include "verification_results.h"
#include "verifier/method_verifier-inl.h"

namespace art {

void QuickCompilerCallbacks::MethodVerified(verifier::MethodVerifier* verifier) {
  if (verification_results_ != nullptr) {
    verification_results_->ProcessVerifiedMethod(verifier);
  }
}

void QuickCompilerCallbacks::ClassRejected(ClassReference ref) {
  if (verification_results_ != nullptr) {
    verification_results_->AddRejectedClass(ref);
  }
}

bool QuickCompilerCallbacks::CanAssumeVerified(ClassReference ref) {
  // If we don't have class unloading enabled in the compiler, we will never see class that were
  // previously verified. Return false to avoid overhead from the lookup in the compiler driver.
  if (!does_class_unloading_) {
    return false;
  }
  DCHECK(compiler_driver_ != nullptr);
  // In the case of the quicken filter: avoiding verification of quickened instructions, which the
  // verifier doesn't currently support.
  // In the case of the verify filter, avoiding verifiying twice.
  return compiler_driver_->CanAssumeVerified(ref);
}

}  // namespace art
