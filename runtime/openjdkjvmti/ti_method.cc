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

#include "ti_method.h"

#include "art_jvmti.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "scoped_thread_state_change-inl.h"

namespace openjdkjvmti {

static jvmtiError CopyString(jvmtiEnv* env, const char* src, unsigned char** copy) {
  size_t len = strlen(src) + 1;
  unsigned char* buf;
  jvmtiError ret = env->Allocate(len, &buf);
  if (ret != ERR(NONE)) {
    return ret;
  }
  strcpy(reinterpret_cast<char*>(buf), src);
  *copy = buf;
  return ret;
}

jvmtiError MethodUtil::GetMethodName(jvmtiEnv* env,
                                     jmethodID method,
                                     char** name_ptr,
                                     char** signature_ptr,
                                     char** generic_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ArtMethod* art_method = soa.DecodeMethod(method);
  art_method = art_method->GetInterfaceMethodIfProxy(art::kRuntimePointerSize);

  JvmtiUniquePtr name_copy;
  if (name_ptr != nullptr) {
    const char* method_name = art_method->GetName();
    if (method_name == nullptr) {
      method_name = "<error>";
    }
    unsigned char* tmp;
    jvmtiError ret = CopyString(env, method_name, &tmp);
    if (ret != ERR(NONE)) {
      return ret;
    }
    name_copy = MakeJvmtiUniquePtr(env, tmp);
    *name_ptr = reinterpret_cast<char*>(tmp);
  }

  JvmtiUniquePtr signature_copy;
  if (signature_ptr != nullptr) {
    const art::Signature sig = art_method->GetSignature();
    std::string str = sig.ToString();
    unsigned char* tmp;
    jvmtiError ret = CopyString(env, str.c_str(), &tmp);
    if (ret != ERR(NONE)) {
      return ret;
    }
    signature_copy = MakeJvmtiUniquePtr(env, tmp);
    *signature_ptr = reinterpret_cast<char*>(tmp);
  }

  // TODO: Support generic signature.
  *generic_ptr = nullptr;

  // Everything is fine, release the buffers.
  name_copy.release();
  signature_copy.release();

  return ERR(NONE);
}

}  // namespace openjdkjvmti
