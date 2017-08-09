/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "aot_class_linker.h"

#include "handle_scope-inl.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "runtime.h"

namespace art {

AotClassLinker::AotClassLinker(InternTable *intern_table) : ClassLinker(intern_table) {}

AotClassLinker::~AotClassLinker() {}

// Wrap the original InitializeClass with creation of transaction when in strict mode.
bool AotClassLinker::InitializeClass(Thread* self, Handle<mirror::Class> klass,
                                  bool can_init_statics, bool can_init_parents) {
  Runtime* const runtime = Runtime::Current();
  bool strict_mode_ = runtime->IsActiveStrictTransactionMode();

  DCHECK(klass != nullptr);
  if (klass->IsInitialized() || klass->IsInitializing()) {
    return ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);
  }

  // Don't initialize klass if it's superclass is not initialized, because superclass might abort
  // the transaction and rolled back after klass's change is commited.
  if (strict_mode_ && !klass->IsInterface() && klass->HasSuperClass()) {
    if (klass->GetSuperClass()->GetStatus() == mirror::Class::kStatusInitializing) {
      runtime->AbortTransactionAndThrowAbortError(self, "Can't resolve "
          + klass->PrettyTypeOf() + " because it's superclass is not initialized.");
      return false;
    }
  }

  if (strict_mode_) {
    runtime->EnterTransactionMode(true, klass.Get()->AsClass());
  }
  bool success = ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);

  if (strict_mode_) {
    if (success) {
      // Exit Transaction if success.
      runtime->ExitTransactionMode();
    } else {
      // If not successfully initialized, the last transaction must abort. Don't rollback
      // immediately, leave the cleanup to compiler driver which needs abort message and exception.
      DCHECK(runtime->IsTransactionAborted());
      DCHECK(self->IsExceptionPending());
    }
  }
  return success;
}
}  // namespace art
