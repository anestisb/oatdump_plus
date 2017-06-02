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

#include "ti_object.h"

#include "art_jvmti.h"
#include "mirror/object-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace openjdkjvmti {

jvmtiError ObjectUtil::GetObjectSize(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                     jobject jobject,
                                     jlong* size_ptr) {
  if (jobject == nullptr) {
    return ERR(INVALID_OBJECT);
  }
  if (size_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Object> object = soa.Decode<art::mirror::Object>(jobject);

  *size_ptr = object->SizeOf();
  return ERR(NONE);
}

jvmtiError ObjectUtil::GetObjectHashCode(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                         jobject jobject,
                                         jint* hash_code_ptr) {
  if (jobject == nullptr) {
    return ERR(INVALID_OBJECT);
  }
  if (hash_code_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Object> object = soa.Decode<art::mirror::Object>(jobject);

  *hash_code_ptr = object->IdentityHashCode();

  return ERR(NONE);
}

}  // namespace openjdkjvmti
