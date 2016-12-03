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
#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "jvmti.h"
#include "linear_alloc.h"
#include "mem_map.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader-inl.h"
#include "mirror/string-inl.h"
#include "oat_file.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread_list.h"
#include "transform.h"
#include "utf.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace openjdkjvmti {

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

// TODO Move this function somewhere more appropriate.
// Gets the data surrounding the given class.
jvmtiError GetTransformationData(ArtJvmTiEnv* env,
                                 jclass klass,
                                 /*out*/std::string* location,
                                 /*out*/JNIEnv** jni_env_ptr,
                                 /*out*/jobject* loader,
                                 /*out*/std::string* name,
                                 /*out*/jobject* protection_domain,
                                 /*out*/jint* data_len,
                                 /*out*/unsigned char** dex_data) {
  jint ret = env->art_vm->GetEnv(reinterpret_cast<void**>(jni_env_ptr), JNI_VERSION_1_1);
  if (ret != JNI_OK) {
    // TODO Different error might be better?
    return ERR(INTERNAL);
  }
  JNIEnv* jni_env = *jni_env_ptr;
  art::ScopedObjectAccess soa(jni_env);
  art::StackHandleScope<3> hs(art::Thread::Current());
  art::Handle<art::mirror::Class> hs_klass(hs.NewHandle(soa.Decode<art::mirror::Class>(klass)));
  *loader = soa.AddLocalReference<jobject>(hs_klass->GetClassLoader());
  *name = art::mirror::Class::ComputeName(hs_klass)->ToModifiedUtf8();
  // TODO is this always null?
  *protection_domain = nullptr;
  const art::DexFile& dex = hs_klass->GetDexFile();
  *location = dex.GetLocation();
  *data_len = static_cast<jint>(dex.Size());
  // TODO We should maybe change env->Allocate to allow us to mprotect this memory and stop writes.
  jvmtiError alloc_error = env->Allocate(*data_len, dex_data);
  if (alloc_error != OK) {
    return alloc_error;
  }
  // Copy the data into a temporary buffer.
  memcpy(reinterpret_cast<void*>(*dex_data),
          reinterpret_cast<const void*>(dex.Begin()),
          *data_len);
  return OK;
}

}  // namespace openjdkjvmti
