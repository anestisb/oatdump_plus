/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "art_method-inl.h"
#include "base/callee_save_type.h"
#include "callee_save_frame.h"
#include "class_linker-inl.h"
#include "class_table-inl.h"
#include "dex_file-inl.h"
#include "dex_file_types.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "gc/heap.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "oat_file.h"
#include "runtime.h"

namespace art {

static inline void BssWriteBarrier(ArtMethod* outer_method) REQUIRES_SHARED(Locks::mutator_lock_) {
  // For AOT code, we need a write barrier for the class loader that holds the
  // GC roots in the .bss.
  const DexFile* dex_file = outer_method->GetDexFile();
  if (dex_file != nullptr &&
      dex_file->GetOatDexFile() != nullptr &&
      !dex_file->GetOatDexFile()->GetOatFile()->GetBssGcRoots().empty()) {
    ObjPtr<mirror::ClassLoader> class_loader = outer_method->GetClassLoader();
    if (kIsDebugBuild) {
      ClassTable* class_table =
          Runtime::Current()->GetClassLinker()->ClassTableForClassLoader(class_loader);
      CHECK(class_table != nullptr &&
            !class_table->InsertOatFile(dex_file->GetOatDexFile()->GetOatFile()))
          << "Oat file with .bss GC roots was not registered in class table: "
          << dex_file->GetOatDexFile()->GetOatFile()->GetLocation();
    }
    if (class_loader != nullptr) {
      // Note that we emit the barrier before the compiled code stores the String or Class
      // as a GC root. This is OK as there is no suspend point point in between.
      Runtime::Current()->GetHeap()->WriteBarrierEveryFieldOf(class_loader);
    } else {
      Runtime::Current()->GetClassLinker()->WriteBarrierForBootOatFileBssRoots(
          dex_file->GetOatDexFile()->GetOatFile());
    }
  }
}

extern "C" mirror::Class* artInitializeStaticStorageFromCode(uint32_t type_idx, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Called to ensure static storage base is initialized for direct static field reads and writes.
  // A class may be accessing another class' fields when it doesn't have access, as access has been
  // given by inheritance.
  ScopedQuickEntrypointChecks sqec(self);
  auto caller_and_outer = GetCalleeSaveMethodCallerAndOuterMethod(
      self, CalleeSaveType::kSaveEverythingForClinit);
  ArtMethod* caller = caller_and_outer.caller;
  mirror::Class* result =
      ResolveVerifyAndClinit(dex::TypeIndex(type_idx), caller, self, true, false);
  if (LIKELY(result != nullptr)) {
    BssWriteBarrier(caller_and_outer.outer_method);
  }
  return result;
}

extern "C" mirror::Class* artInitializeTypeFromCode(uint32_t type_idx, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Called when method->dex_cache_resolved_types_[] misses.
  ScopedQuickEntrypointChecks sqec(self);
  auto caller_and_outer = GetCalleeSaveMethodCallerAndOuterMethod(
      self, CalleeSaveType::kSaveEverythingForClinit);
  ArtMethod* caller = caller_and_outer.caller;
  mirror::Class* result =
      ResolveVerifyAndClinit(dex::TypeIndex(type_idx), caller, self, false, false);
  if (LIKELY(result != nullptr)) {
    BssWriteBarrier(caller_and_outer.outer_method);
  }
  return result;
}

extern "C" mirror::Class* artInitializeTypeAndVerifyAccessFromCode(uint32_t type_idx, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Called when caller isn't guaranteed to have access to a type and the dex cache may be
  // unpopulated.
  ScopedQuickEntrypointChecks sqec(self);
  auto caller_and_outer = GetCalleeSaveMethodCallerAndOuterMethod(self,
                                                                  CalleeSaveType::kSaveEverything);
  ArtMethod* caller = caller_and_outer.caller;
  mirror::Class* result =
      ResolveVerifyAndClinit(dex::TypeIndex(type_idx), caller, self, false, true);
  if (LIKELY(result != nullptr)) {
    BssWriteBarrier(caller_and_outer.outer_method);
  }
  return result;
}

extern "C" mirror::String* artResolveStringFromCode(int32_t string_idx, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedQuickEntrypointChecks sqec(self);
  auto caller_and_outer = GetCalleeSaveMethodCallerAndOuterMethod(self,
                                                                  CalleeSaveType::kSaveEverything);
  ArtMethod* caller = caller_and_outer.caller;
  mirror::String* result = ResolveStringFromCode(caller, dex::StringIndex(string_idx));
  if (LIKELY(result != nullptr)) {
    BssWriteBarrier(caller_and_outer.outer_method);
  }
  return result;
}

}  // namespace art
