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
#include "jni_internal.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

namespace openjdkjvmti {

jvmtiError ClassUtil::GetClassFields(jvmtiEnv* env,
                                     jclass jklass,
                                     jint* field_count_ptr,
                                     jfieldID** fields_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (field_count_ptr == nullptr || fields_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::IterationRange<art::StrideIterator<art::ArtField>> ifields = klass->GetIFields();
  art::IterationRange<art::StrideIterator<art::ArtField>> sfields = klass->GetSFields();
  size_t array_size = klass->NumInstanceFields() + klass->NumStaticFields();

  unsigned char* out_ptr;
  jvmtiError allocError = env->Allocate(array_size * sizeof(jfieldID), &out_ptr);
  if (allocError != ERR(NONE)) {
    return allocError;
  }
  jfieldID* field_array = reinterpret_cast<jfieldID*>(out_ptr);

  size_t array_idx = 0;
  for (art::ArtField& field : sfields) {
    field_array[array_idx] = art::jni::EncodeArtField(&field);
    ++array_idx;
  }
  for (art::ArtField& field : ifields) {
    field_array[array_idx] = art::jni::EncodeArtField(&field);
    ++array_idx;
  }

  *field_count_ptr = static_cast<jint>(array_size);
  *fields_ptr = field_array;

  return ERR(NONE);
}

jvmtiError ClassUtil::GetClassMethods(jvmtiEnv* env,
                                      jclass jklass,
                                      jint* method_count_ptr,
                                      jmethodID** methods_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (method_count_ptr == nullptr || methods_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  size_t array_size = klass->NumDeclaredVirtualMethods() + klass->NumDirectMethods();
  unsigned char* out_ptr;
  jvmtiError allocError = env->Allocate(array_size * sizeof(jmethodID), &out_ptr);
  if (allocError != ERR(NONE)) {
    return allocError;
  }
  jmethodID* method_array = reinterpret_cast<jmethodID*>(out_ptr);

  if (art::kIsDebugBuild) {
    size_t count = 0;
    for (auto& m ATTRIBUTE_UNUSED : klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
      count++;
    }
    CHECK_EQ(count, klass->NumDirectMethods() + klass->NumDeclaredVirtualMethods());
  }

  size_t array_idx = 0;
  for (auto& m : klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
    method_array[array_idx] = art::jni::EncodeArtMethod(&m);
    ++array_idx;
  }

  *method_count_ptr = static_cast<jint>(array_size);
  *methods_ptr = method_array;

  return ERR(NONE);
}

jvmtiError ClassUtil::GetImplementedInterfaces(jvmtiEnv* env,
                                               jclass jklass,
                                               jint* interface_count_ptr,
                                               jclass** interfaces_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (interface_count_ptr == nullptr || interfaces_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  // Need to handle array specifically. Arrays implement Serializable and Cloneable, but the
  // spec says these should not be reported.
  if (klass->IsArrayClass()) {
    *interface_count_ptr = 0;
    *interfaces_ptr = nullptr;  // TODO: Should we allocate a dummy here?
    return ERR(NONE);
  }

  size_t array_size = klass->NumDirectInterfaces();
  unsigned char* out_ptr;
  jvmtiError allocError = env->Allocate(array_size * sizeof(jclass), &out_ptr);
  if (allocError != ERR(NONE)) {
    return allocError;
  }
  jclass* interface_array = reinterpret_cast<jclass*>(out_ptr);

  art::StackHandleScope<1> hs(soa.Self());
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(klass));

  for (uint32_t idx = 0; idx != array_size; ++idx) {
    art::ObjPtr<art::mirror::Class> inf_klass =
        art::mirror::Class::ResolveDirectInterface(soa.Self(), h_klass, idx);
    if (inf_klass == nullptr) {
      soa.Self()->ClearException();
      env->Deallocate(out_ptr);
      // TODO: What is the right error code here?
      return ERR(INTERNAL);
    }
    interface_array[idx] = soa.AddLocalReference<jclass>(inf_klass);
  }

  *interface_count_ptr = static_cast<jint>(array_size);
  *interfaces_ptr = interface_array;

  return ERR(NONE);
}

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

jvmtiError ClassUtil::GetClassStatus(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                     jclass jklass,
                                     jint* status_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (status_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  if (klass->IsArrayClass()) {
    *status_ptr = JVMTI_CLASS_STATUS_ARRAY;
  } else if (klass->IsPrimitive()) {
    *status_ptr = JVMTI_CLASS_STATUS_PRIMITIVE;
  } else {
    *status_ptr = JVMTI_CLASS_STATUS_VERIFIED;  // All loaded classes are structurally verified.
    // This is finicky. If there's an error, we'll say it wasn't prepared.
    if (klass->IsResolved()) {
      *status_ptr |= JVMTI_CLASS_STATUS_PREPARED;
    }
    if (klass->IsInitialized()) {
      *status_ptr |= JVMTI_CLASS_STATUS_INITIALIZED;
    }
    // Technically the class may be erroneous for other reasons, but we do not have enough info.
    if (klass->IsErroneous()) {
      *status_ptr |= JVMTI_CLASS_STATUS_ERROR;
    }
  }

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

// Keep this in sync with Class.getModifiers().
static uint32_t ClassGetModifiers(art::Thread* self, art::ObjPtr<art::mirror::Class> klass)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  if (klass->IsArrayClass()) {
    uint32_t component_modifiers = ClassGetModifiers(self, klass->GetComponentType());
    if ((component_modifiers & art::kAccInterface) != 0) {
      component_modifiers &= ~(art::kAccInterface | art::kAccStatic);
    }
    return art::kAccAbstract | art::kAccFinal | component_modifiers;
  }

  uint32_t modifiers = klass->GetAccessFlags() & art::kAccJavaFlagsMask;

  art::StackHandleScope<1> hs(self);
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(klass));
  return art::mirror::Class::GetInnerClassFlags(h_klass, modifiers);
}

jvmtiError ClassUtil::GetClassModifiers(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                        jclass jklass,
                                        jint* modifiers_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (modifiers_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  *modifiers_ptr = ClassGetModifiers(soa.Self(), klass);

  return ERR(NONE);
}

jvmtiError ClassUtil::GetClassLoader(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                     jclass jklass,
                                     jobject* classloader_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ObjPtr<art::mirror::Class> klass = soa.Decode<art::mirror::Class>(jklass);
  if (klass == nullptr) {
    return ERR(INVALID_CLASS);
  }

  if (classloader_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  *classloader_ptr = soa.AddLocalReference<jobject>(klass->GetClassLoader());

  return ERR(NONE);
}

}  // namespace openjdkjvmti
