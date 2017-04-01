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
    art::ArraySlice<const unsigned char> dex_data = def.GetDexData();
    event_handler->DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookRetransformable>(
        self,
        GetJniEnv(env),
        def.GetClass(),
        def.GetLoader(),
        def.GetName().c_str(),
        def.GetProtectionDomain(),
        static_cast<jint>(dex_data.size()),
        &dex_data.At(0),
        /*out*/&new_len,
        /*out*/&new_data);
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
    res = def.Init(env, classes[i]);
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

}  // namespace openjdkjvmti
