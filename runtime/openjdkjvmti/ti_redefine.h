/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_
#define ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_

#include <string>

#include <jni.h>

#include "art_jvmti.h"
#include "art_method.h"
#include "class_linker.h"
#include "dex_file.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "jvmti.h"
#include "linear_alloc.h"
#include "mem_map.h"
#include "mirror/array-inl.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"
#include "mirror/class_loader-inl.h"
#include "mirror/string-inl.h"
#include "oat_file.h"
#include "obj_ptr.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread_list.h"
#include "transform.h"
#include "utf.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace openjdkjvmti {

class RedefinitionDataHolder;

// Class that can redefine a single class's methods.
// TODO We should really make this be driven by an outside class so we can do multiple classes at
// the same time and have less required cleanup.
class Redefiner {
 public:
  // Redefine the given classes with the given dex data. Note this function does not take ownership
  // of the dex_data pointers. It is not used after this call however and may be freed if desired.
  // The caller is responsible for freeing it. The runtime makes its own copy of the data. This
  // function does not call the transformation events.
  // TODO Check modified flag of the definitions.
  static jvmtiError RedefineClassesDirect(ArtJvmTiEnv* env,
                                          art::Runtime* runtime,
                                          art::Thread* self,
                                          const std::vector<ArtClassDefinition>& definitions,
                                          /*out*/std::string* error_msg);

  // Redefine the given classes with the given dex data. Note this function does not take ownership
  // of the dex_data pointers. It is not used after this call however and may be freed if desired.
  // The caller is responsible for freeing it. The runtime makes its own copy of the data.
  // TODO This function should call the transformation events.
  static jvmtiError RedefineClasses(ArtJvmTiEnv* env,
                                    art::Runtime* runtime,
                                    art::Thread* self,
                                    jint class_count,
                                    const jvmtiClassDefinition* definitions,
                                    /*out*/std::string* error_msg);

  static jvmtiError IsModifiableClass(jvmtiEnv* env, jclass klass, jboolean* is_redefinable);

 private:
  class ClassRedefinition {
   public:
    ClassRedefinition(Redefiner* driver,
                      jclass klass,
                      const art::DexFile* redefined_dex_file,
                      const char* class_sig)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

    // NO_THREAD_SAFETY_ANALYSIS so we can unlock the class in the destructor.
    ~ClassRedefinition() NO_THREAD_SAFETY_ANALYSIS;

    // Move constructor so we can put these into a vector.
    ClassRedefinition(ClassRedefinition&& other)
        : driver_(other.driver_),
          klass_(other.klass_),
          dex_file_(std::move(other.dex_file_)),
          class_sig_(std::move(other.class_sig_)) {
      other.driver_ = nullptr;
    }

    art::mirror::Class* GetMirrorClass() REQUIRES_SHARED(art::Locks::mutator_lock_);
    art::mirror::ClassLoader* GetClassLoader() REQUIRES_SHARED(art::Locks::mutator_lock_);

    // This finds the java.lang.DexFile we will add the native DexFile to as part of the classpath.
    // TODO Make sure the DexFile object returned is the one that the klass_ actually comes from.
    art::mirror::Object* FindSourceDexFileObject(art::Handle<art::mirror::ClassLoader> loader)
        REQUIRES_SHARED(art::Locks::mutator_lock_);

    art::mirror::DexCache* CreateNewDexCache(art::Handle<art::mirror::ClassLoader> loader)
        REQUIRES_SHARED(art::Locks::mutator_lock_);

    // Allocates and fills the new DexFileCookie
    art::mirror::LongArray* AllocateDexFileCookie(art::Handle<art::mirror::Object> j_dex_file_obj)
        REQUIRES_SHARED(art::Locks::mutator_lock_);

    void RecordFailure(jvmtiError e, const std::string& err) {
      driver_->RecordFailure(e, class_sig_, err);
    }

    bool FinishRemainingAllocations(
          /*out*/art::MutableHandle<art::mirror::ClassLoader>* source_class_loader,
          /*out*/art::MutableHandle<art::mirror::Object>* source_dex_file_obj,
          /*out*/art::MutableHandle<art::mirror::LongArray>* new_dex_file_cookie,
          /*out*/art::MutableHandle<art::mirror::DexCache>* new_dex_cache)
        REQUIRES_SHARED(art::Locks::mutator_lock_);

    void FindAndAllocateObsoleteMethods(art::mirror::Class* art_klass)
        REQUIRES(art::Locks::mutator_lock_);

    void FillObsoleteMethodMap(
        art::mirror::Class* art_klass,
        const std::unordered_map<art::ArtMethod*, art::ArtMethod*>& obsoletes)
          REQUIRES(art::Locks::mutator_lock_);


    // Checks that the dex file contains only the single expected class and that the top-level class
    // data has not been modified in an incompatible manner.
    bool CheckClass() REQUIRES_SHARED(art::Locks::mutator_lock_);

    // Preallocates all needed allocations in klass so that we can pause execution safely.
    // TODO We should be able to free the arrays if they end up not being used. Investigate doing
    // this in the future. For now we will just take the memory hit.
    bool EnsureClassAllocationsFinished() REQUIRES_SHARED(art::Locks::mutator_lock_);

    // This will check that no constraints are violated (more than 1 class in dex file, any changes
    // in number/declaration of methods & fields, changes in access flags, etc.)
    bool CheckRedefinitionIsValid() REQUIRES_SHARED(art::Locks::mutator_lock_);

    // Checks that the class can even be redefined.
    bool CheckRedefinable() REQUIRES_SHARED(art::Locks::mutator_lock_);

    // Checks that the dex file does not add/remove methods.
    bool CheckSameMethods() REQUIRES_SHARED(art::Locks::mutator_lock_) {
      LOG(WARNING) << "methods are not checked for modification currently";
      return true;
    }

    // Checks that the dex file does not modify fields
    bool CheckSameFields() REQUIRES_SHARED(art::Locks::mutator_lock_) {
      LOG(WARNING) << "Fields are not checked for modification currently";
      return true;
    }

    void UpdateJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                           art::ObjPtr<art::mirror::LongArray> new_cookie)
        REQUIRES(art::Locks::mutator_lock_);

    void UpdateFields(art::ObjPtr<art::mirror::Class> mclass)
        REQUIRES(art::Locks::mutator_lock_);

    void UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                       art::ObjPtr<art::mirror::DexCache> new_dex_cache,
                       const art::DexFile::ClassDef& class_def)
        REQUIRES(art::Locks::mutator_lock_);

    void UpdateClass(art::ObjPtr<art::mirror::Class> mclass,
                     art::ObjPtr<art::mirror::DexCache> new_dex_cache)
        REQUIRES(art::Locks::mutator_lock_);

    void ReleaseDexFile() REQUIRES_SHARED(art::Locks::mutator_lock_);

   private:
    Redefiner* driver_;
    jclass klass_;
    std::unique_ptr<const art::DexFile> dex_file_;
    std::string class_sig_;
  };

  jvmtiError result_;
  art::Runtime* runtime_;
  art::Thread* self_;
  std::vector<ClassRedefinition> redefinitions_;
  // Kept as a jclass since we have weird run-state changes that make keeping it around as a
  // mirror::Class difficult and confusing.
  std::string* error_msg_;

  // TODO Maybe change jclass to a mirror::Class
  Redefiner(art::Runtime* runtime,
            art::Thread* self,
            std::string* error_msg)
      : result_(ERR(INTERNAL)),
        runtime_(runtime),
        self_(self),
        redefinitions_(),
        error_msg_(error_msg) { }

  jvmtiError AddRedefinition(ArtJvmTiEnv* env, const ArtClassDefinition& def)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  static jvmtiError GetClassRedefinitionError(art::Handle<art::mirror::Class> klass,
                                              /*out*/std::string* error_msg)
      REQUIRES_SHARED(art::Locks::mutator_lock_);

  static std::unique_ptr<art::MemMap> MoveDataToMemMap(const std::string& original_location,
                                                       jint data_len,
                                                       const unsigned char* dex_data,
                                                       std::string* error_msg);

  // TODO Put on all the lock qualifiers.
  jvmtiError Run() REQUIRES_SHARED(art::Locks::mutator_lock_);

  bool CheckAllRedefinitionAreValid() REQUIRES_SHARED(art::Locks::mutator_lock_);
  bool EnsureAllClassAllocationsFinished() REQUIRES_SHARED(art::Locks::mutator_lock_);
  bool FinishAllRemainingAllocations(RedefinitionDataHolder& holder)
      REQUIRES_SHARED(art::Locks::mutator_lock_);
  void ReleaseAllDexFiles() REQUIRES_SHARED(art::Locks::mutator_lock_);

  // Ensure that obsolete methods are deoptimized. This is needed since optimized methods may have
  // pointers to their ArtMethods stashed in registers that they then use to attempt to hit the
  // DexCache.
  void EnsureObsoleteMethodsAreDeoptimized()
      REQUIRES(art::Locks::mutator_lock_)
      REQUIRES(!art::Locks::thread_list_lock_,
               !art::Locks::classlinker_classes_lock_);

  void RecordFailure(jvmtiError result, const std::string& class_sig, const std::string& error_msg);
  void RecordFailure(jvmtiError result, const std::string& error_msg) {
    RecordFailure(result, "NO CLASS", error_msg);
  }

  friend struct CallbackCtx;
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_TI_REDEFINE_H_
