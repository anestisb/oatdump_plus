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

#ifndef ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_
#define ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_

#include <jni.h>

#include "java_vm_ext.h"
#include "jni_env_ext.h"
#include "jvmti.h"

namespace openjdkjvmti {

extern const jvmtiInterface_1 gJvmtiInterface;

// A structure that is a jvmtiEnv with additional information for the runtime.
struct ArtJvmTiEnv : public jvmtiEnv {
  art::JavaVMExt* art_vm;
  void* local_data;

  explicit ArtJvmTiEnv(art::JavaVMExt* runtime) : art_vm(runtime), local_data(nullptr) {
    functions = &gJvmtiInterface;
  }
};

// Macro and constexpr to make error values less annoying to write.
#define ERR(e) JVMTI_ERROR_ ## e
static constexpr jvmtiError OK = JVMTI_ERROR_NONE;

// Special error code for unimplemented functions in JVMTI
static constexpr jvmtiError ERR(NOT_IMPLEMENTED) = JVMTI_ERROR_NOT_AVAILABLE;

static inline JNIEnv* GetJniEnv(jvmtiEnv* env) {
  JNIEnv* ret_value = nullptr;
  jint res = reinterpret_cast<ArtJvmTiEnv*>(env)->art_vm->GetEnv(
      reinterpret_cast<void**>(&ret_value), JNI_VERSION_1_1);
  if (res != JNI_OK) {
    return nullptr;
  }
  return ret_value;
}

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_
