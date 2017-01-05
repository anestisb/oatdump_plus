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

#include "ti_class.h"

#include "art_jvmti.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

namespace openjdkjvmti {

jvmtiError ClassUtil::GetClassSignature(jvmtiEnv* env,
                                         jclass jklass,
                                         char** signature_ptr,
                                         char** generic_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  JvmtiUniquePtr sig_copy;
  if (signature_ptr != nullptr) {
    std::string storage;
    const char* descriptor = klass->GetDescriptor(&storage);

    unsigned char* tmp;
    jvmtiError ret = CopyString(env, descriptor, &tmp);
    if (ret != ERR(NONE)) {
      return ret;
    }
    sig_copy = MakeJvmtiUniquePtr(env, tmp);
    *signature_ptr = reinterpret_cast<char*>(tmp);
  }

  // TODO: Support generic signature.
  *generic_ptr = nullptr;

  // Everything is fine, release the buffers.
  sig_copy.release();

  return ERR(NONE);
}

template <typename T>
static jvmtiError ClassIsT(jclass jklass, T test, jboolean* is_t_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (is_t_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  *is_t_ptr = test(klass) ? JNI_TRUE : JNI_FALSE;
  return ERR(NONE);
}

jvmtiError ClassUtil::IsInterface(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                  jclass jklass,
                                  jboolean* is_interface_ptr) {
  auto test = [](art::ObjPtr<art::mirror::Class> klass) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return klass->IsInterface();
  };
  return ClassIsT(jklass, test, is_interface_ptr);
}

jvmtiError ClassUtil::IsArrayClass(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                   jclass jklass,
                                   jboolean* is_array_class_ptr) {
  auto test = [](art::ObjPtr<art::mirror::Class> klass) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return klass->IsArrayClass();
  };
  return ClassIsT(jklass, test, is_array_class_ptr);
}

}  // namespace openjdkjvmti
