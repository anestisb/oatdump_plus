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

#include <unordered_map>
#include <unordered_set>

#include "transform.h"

#include "art_method.h"
#include "class_linker.h"
#include "dex_file.h"
#include "dex_file_types.h"
#include "events-inl.h"
#include "fixed_up_dex_file.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "jvalue.h"
#include "jvmti.h"
#include "linear_alloc.h"
#include "mem_map.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/class_ext.h"
#include "mirror/class_loader-inl.h"
#include "mirror/string-inl.h"
#include "oat_file.h"
#include "reflection.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread_list.h"
#include "ti_redefine.h"
#include "transform.h"
#include "utf.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace openjdkjvmti {

jvmtiError Transformer::RetransformClassesDirect(
      ArtJvmTiEnv* env,
      EventHandler* event_handler,
      art::Thread* self,
      /*in-out*/std::vector<ArtClassDefinition>* definitions) {
  for (ArtClassDefinition& def : *definitions) {
    jint new_len = -1;
    unsigned char* new_data = nullptr;
    event_handler->DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookRetransformable>(
        self,
        GetJniEnv(env),
        def.klass,
        def.loader,
        def.name.c_str(),
        def.protection_domain,
        def.dex_len,
        static_cast<const unsigned char*>(def.dex_data.get()),
        &new_len,
        &new_data);
    def.SetNewDexData(env, new_len, new_data);
  }
  return OK;
}

jvmtiError Transformer::RetransformClasses(ArtJvmTiEnv* env,
                                           EventHandler* event_handler,
                                           art::Runtime* runtime,
                                           art::Thread* self,
                                           jint class_count,
                                           const jclass* classes,
                                           /*out*/std::string* error_msg) {
  if (env == nullptr) {
    *error_msg = "env was null!";
    return ERR(INVALID_ENVIRONMENT);
  } else if (class_count < 0) {
    *error_msg = "class_count was less then 0";
    return ERR(ILLEGAL_ARGUMENT);
  } else if (class_count == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  } else if (classes == nullptr) {
    *error_msg = "null classes!";
    return ERR(NULL_POINTER);
  }
  // A holder that will Deallocate all the class bytes buffers on destruction.
  std::vector<ArtClassDefinition> definitions;
  jvmtiError res = OK;
  for (jint i = 0; i < class_count; i++) {
    jboolean is_modifiable = JNI_FALSE;
    res = env->IsModifiableClass(classes[i], &is_modifiable);
    if (res != OK) {
      return res;
    } else if (!is_modifiable) {
      return ERR(UNMODIFIABLE_CLASS);
    }
    ArtClassDefinition def;
    res = FillInTransformationData(env, classes[i], &def);
    if (res != OK) {
      return res;
    }
    definitions.push_back(std::move(def));
  }
  res = RetransformClassesDirect(env, event_handler, self, &definitions);
  if (res != OK) {
    return res;
  }
  return Redefiner::RedefineClassesDirect(env, runtime, self, definitions, error_msg);
}

// TODO Move this somewhere else, ti_class?
jvmtiError GetClassLocation(ArtJvmTiEnv* env, jclass klass, /*out*/std::string* location) {
  JNIEnv* jni_env = nullptr;
  jint ret = env->art_vm->GetEnv(reinterpret_cast<void**>(&jni_env), JNI_VERSION_1_1);
  if (ret != JNI_OK) {
    // TODO Different error might be better?
    return ERR(INTERNAL);
  }
  art::ScopedObjectAccess soa(jni_env);
  art::StackHandleScope<1> hs(art::Thread::Current());
  art::Handle<art::mirror::Class> hs_klass(hs.NewHandle(soa.Decode<art::mirror::Class>(klass)));
  const art::DexFile& dex = hs_klass->GetDexFile();
  *location = dex.GetLocation();
  return OK;
}

jvmtiError Transformer::GetDexDataForRetransformation(ArtJvmTiEnv* env,
                                                      art::Handle<art::mirror::Class> klass,
                                                      /*out*/jint* dex_data_len,
                                                      /*out*/unsigned char** dex_data) {
  art::StackHandleScope<3> hs(art::Thread::Current());
  art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->GetExtData()));
  const art::DexFile* dex_file = nullptr;
  if (!ext.IsNull()) {
    art::Handle<art::mirror::Object> orig_dex(hs.NewHandle(ext->GetOriginalDexFile()));
    if (!orig_dex.IsNull()) {
      if (orig_dex->IsArrayInstance()) {
        DCHECK(orig_dex->GetClass()->GetComponentType()->IsPrimitiveByte());
        art::Handle<art::mirror::ByteArray> orig_dex_bytes(
            hs.NewHandle(art::down_cast<art::mirror::ByteArray*>(orig_dex->AsArray())));
        *dex_data_len = static_cast<jint>(orig_dex_bytes->GetLength());
        return CopyDataIntoJvmtiBuffer(
            env,
            reinterpret_cast<const unsigned char*>(orig_dex_bytes->GetData()),
            *dex_data_len,
            /*out*/dex_data);
      } else if (orig_dex->IsDexCache()) {
        dex_file = orig_dex->AsDexCache()->GetDexFile();
      } else {
        DCHECK_EQ(orig_dex->GetClass()->GetPrimitiveType(), art::Primitive::kPrimLong);
        art::ObjPtr<art::mirror::Class> prim_long_class(
            art::Runtime::Current()->GetClassLinker()->GetClassRoot(
                art::ClassLinker::kPrimitiveLong));
        art::JValue val;
        if (!art::UnboxPrimitiveForResult(orig_dex.Get(), prim_long_class, &val)) {
          // This should never happen.
          return ERR(INTERNAL);
        }
        dex_file = reinterpret_cast<const art::DexFile*>(static_cast<uintptr_t>(val.GetJ()));
      }
    }
  }
  if (dex_file == nullptr) {
    dex_file = &klass->GetDexFile();
  }
  std::unique_ptr<FixedUpDexFile> fixed_dex_file(FixedUpDexFile::Create(*dex_file));
  *dex_data_len = static_cast<jint>(fixed_dex_file->Size());
  return CopyDataIntoJvmtiBuffer(env,
                                 fixed_dex_file->Begin(),
                                 fixed_dex_file->Size(),
                                 /*out*/dex_data);
}

// TODO Move this function somewhere more appropriate.
// Gets the data surrounding the given class.
// TODO Make this less magical.
jvmtiError Transformer::FillInTransformationData(ArtJvmTiEnv* env,
                                                 jclass klass,
                                                 ArtClassDefinition* def) {
  JNIEnv* jni_env = GetJniEnv(env);
  if (jni_env == nullptr) {
    // TODO Different error might be better?
    return ERR(INTERNAL);
  }
  art::ScopedObjectAccess soa(jni_env);
  art::StackHandleScope<3> hs(art::Thread::Current());
  art::Handle<art::mirror::Class> hs_klass(hs.NewHandle(soa.Decode<art::mirror::Class>(klass)));
  if (hs_klass.IsNull()) {
    return ERR(INVALID_CLASS);
  }
  def->klass = klass;
  def->loader = soa.AddLocalReference<jobject>(hs_klass->GetClassLoader());
  std::string descriptor_store;
  std::string descriptor(hs_klass->GetDescriptor(&descriptor_store));
  def->name = descriptor.substr(1, descriptor.size() - 2);
  // TODO is this always null?
  def->protection_domain = nullptr;
  if (def->dex_data.get() == nullptr) {
    unsigned char* new_data;
    jvmtiError res = GetDexDataForRetransformation(env, hs_klass, &def->dex_len, &new_data);
    if (res == OK) {
      def->dex_data = MakeJvmtiUniquePtr(env, new_data);
      // TODO This whole thing is a bit of a mess.
      // We need to keep track of what the runtime should think an unmodified dex file is since
      // we need to be able to tell if anything changes. This might be different then the currently
      // loaded dex file since we need to un-quicken stuff.
      if (hs_klass->GetExtData() == nullptr ||
          hs_klass->GetExtData()->GetOriginalDexFile() == nullptr) {
        // We have never redefined this yet. Keep track of what the (de-quickened) dex file looks
        // like so we can tell if anything has changed.
        // Really we would like to just always do the 'else' block but the fact that we de-quickened
        // stuff screws us over.
        unsigned char* original_data_memory = nullptr;
        res = env->Allocate(def->dex_len, &original_data_memory);
        if (res != OK) {
          return res;
        }
        memcpy(original_data_memory, new_data, def->dex_len);
        def->original_dex_file_memory = MakeJvmtiUniquePtr(env, original_data_memory);
        def->original_dex_file = art::ArraySlice<const unsigned char>(original_data_memory,
                                                                      def->dex_len);
      } else {
        // We know that we have been redefined at least once (there is an original_dex_file set in
        // the class) so we can just use the current dex file directly.
        def->original_dex_file = art::ArraySlice<const unsigned char>(
            hs_klass->GetDexFile().Begin(), hs_klass->GetDexFile().Size());
      }
    } else {
      return res;
    }
  }
  return OK;
}

}  // namespace openjdkjvmti
