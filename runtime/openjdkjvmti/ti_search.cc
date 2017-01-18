/* Copyright (C) 2017 The Android Open Source Project
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

#include "ti_search.h"

#include "jni.h"

#include "art_jvmti.h"
#include "base/macros.h"
#include "class_linker.h"
#include "dex_file.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"

namespace openjdkjvmti {

jvmtiError SearchUtil::AddToBootstrapClassLoaderSearch(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                                       const char* segment) {
  art::Runtime* current = art::Runtime::Current();
  if (current == nullptr) {
    return ERR(WRONG_PHASE);
  }
  if (current->GetClassLinker() == nullptr) {
    // TODO: Support boot classpath change in OnLoad.
    return ERR(WRONG_PHASE);
  }
  if (segment == nullptr) {
    return ERR(NULL_POINTER);
  }

  std::string error_msg;
  std::vector<std::unique_ptr<const art::DexFile>> dex_files;
  if (!art::DexFile::Open(segment, segment, true, &error_msg, &dex_files)) {
    LOG(WARNING) << "Could not open " << segment << " for boot classpath extension: " << error_msg;
    return ERR(ILLEGAL_ARGUMENT);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  for (std::unique_ptr<const art::DexFile>& dex_file : dex_files) {
    current->GetClassLinker()->AppendToBootClassPath(art::Thread::Current(), *dex_file.release());
  }

  return ERR(NONE);
}

jvmtiError SearchUtil::AddToSystemClassLoaderSearch(jvmtiEnv* jvmti_env ATTRIBUTE_UNUSED,
                                                    const char* segment) {
  if (segment == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::Runtime* current = art::Runtime::Current();
  if (current == nullptr) {
    return ERR(WRONG_PHASE);
  }
  jobject sys_class_loader = current->GetSystemClassLoader();
  if (sys_class_loader == nullptr) {
    // TODO: Support classpath change in OnLoad.
    return ERR(WRONG_PHASE);
  }

  // We'll use BaseDexClassLoader.addDexPath, as it takes care of array resizing etc. As a downside,
  // exceptions are swallowed.

  art::Thread* self = art::Thread::Current();
  JNIEnv* env = self->GetJniEnv();
  if (!env->IsInstanceOf(sys_class_loader,
                         art::WellKnownClasses::dalvik_system_BaseDexClassLoader)) {
    return ERR(INTERNAL);
  }

  jmethodID add_dex_path_id = env->GetMethodID(
      art::WellKnownClasses::dalvik_system_BaseDexClassLoader,
      "addDexPath",
      "(Ljava/lang/String;)V");
  if (add_dex_path_id == nullptr) {
    return ERR(INTERNAL);
  }

  ScopedLocalRef<jstring> dex_path(env, env->NewStringUTF(segment));
  if (dex_path.get() == nullptr) {
    return ERR(INTERNAL);
  }
  env->CallVoidMethod(sys_class_loader, add_dex_path_id, dex_path.get());

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return ERR(ILLEGAL_ARGUMENT);
  }
  return ERR(NONE);
}

}  // namespace openjdkjvmti
