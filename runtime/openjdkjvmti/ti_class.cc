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

#include "android-base/stringprintf.h"

#include <mutex>
#include <unordered_set>

#include "art_jvmti.h"
#include "base/macros.h"
#include "class_table-inl.h"
#include "class_linker.h"
#include "common_throws.h"
#include "events-inl.h"
#include "handle.h"
#include "jni_env_ext-inl.h"
#include "jni_internal.h"
#include "mirror/array-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class_ext.h"
#include "runtime.h"
#include "runtime_callbacks.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "ti_redefine.h"
#include "utils.h"

namespace openjdkjvmti {

using android::base::StringPrintf;

static std::unique_ptr<const art::DexFile> MakeSingleDexFile(art::Thread* self,
                                                             const char* descriptor,
                                                             const std::string& orig_location,
                                                             jint final_len,
                                                             const unsigned char* final_dex_data)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
  // Make the mmap
  std::string error_msg;
  std::unique_ptr<art::MemMap> map(Redefiner::MoveDataToMemMap(orig_location,
                                                               final_len,
                                                               final_dex_data,
                                                               &error_msg));
  if (map.get() == nullptr) {
    LOG(WARNING) << "Unable to allocate mmap for redefined dex file! Error was: " << error_msg;
    self->ThrowOutOfMemoryError(StringPrintf(
        "Unable to allocate dex file for transformation of %s", descriptor).c_str());
    return nullptr;
  }

  // Make a dex-file
  if (map->Size() < sizeof(art::DexFile::Header)) {
    LOG(WARNING) << "Could not read dex file header because dex_data was too short";
    art::ThrowClassFormatError(nullptr,
                               "Unable to read transformed dex file of %s",
                               descriptor);
    return nullptr;
  }
  uint32_t checksum = reinterpret_cast<const art::DexFile::Header*>(map->Begin())->checksum_;
  std::unique_ptr<const art::DexFile> dex_file(art::DexFile::Open(map->GetName(),
                                                                  checksum,
                                                                  std::move(map),
                                                                  /*verify*/true,
                                                                  /*verify_checksum*/true,
                                                                  &error_msg));
  if (dex_file.get() == nullptr) {
    LOG(WARNING) << "Unable to load modified dex file for " << descriptor << ": " << error_msg;
    art::ThrowClassFormatError(nullptr,
                               "Unable to read transformed dex file of %s because %s",
                               descriptor,
                               error_msg.c_str());
    return nullptr;
  }
  if (dex_file->NumClassDefs() != 1) {
    LOG(WARNING) << "Dex file contains more than 1 class_def. Ignoring.";
    // TODO Throw some other sort of error here maybe?
    art::ThrowClassFormatError(
        nullptr,
        "Unable to use transformed dex file of %s because it contained too many classes",
        descriptor);
    return nullptr;
  }
  return dex_file;
}

struct ClassCallback : public art::ClassLoadCallback {
  void ClassPreDefine(const char* descriptor,
                      art::Handle<art::mirror::Class> klass,
                      art::Handle<art::mirror::ClassLoader> class_loader,
                      const art::DexFile& initial_dex_file,
                      const art::DexFile::ClassDef& initial_class_def ATTRIBUTE_UNUSED,
                      /*out*/art::DexFile const** final_dex_file,
                      /*out*/art::DexFile::ClassDef const** final_class_def)
      OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
    bool is_enabled =
        event_handler->IsEventEnabledAnywhere(ArtJvmtiEvent::kClassFileLoadHookRetransformable) ||
        event_handler->IsEventEnabledAnywhere(ArtJvmtiEvent::kClassFileLoadHookNonRetransformable);
    if (!is_enabled) {
      return;
    }
    if (descriptor[0] != 'L') {
      // It is a primitive or array. Just return
      return;
    }
    std::string name(std::string(descriptor).substr(1, strlen(descriptor) - 2));

    art::Thread* self = art::Thread::Current();
    art::JNIEnvExt* env = self->GetJniEnv();
    ScopedLocalRef<jobject> loader(
        env, class_loader.IsNull() ? nullptr : env->AddLocalReference<jobject>(class_loader.Get()));
    // Go back to native.
    art::ScopedThreadSuspension sts(self, art::ThreadState::kNative);
    // Call all Non-retransformable agents.
    jint post_no_redefine_len = 0;
    unsigned char* post_no_redefine_dex_data = nullptr;
    std::unique_ptr<const unsigned char> post_no_redefine_unique_ptr(nullptr);
    event_handler->DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookNonRetransformable>(
        self,
        static_cast<JNIEnv*>(env),
        static_cast<jclass>(nullptr),  // The class doesn't really exist yet so send null.
        loader.get(),
        name.c_str(),
        static_cast<jobject>(nullptr),  // Android doesn't seem to have protection domains
        static_cast<jint>(initial_dex_file.Size()),
        static_cast<const unsigned char*>(initial_dex_file.Begin()),
        static_cast<jint*>(&post_no_redefine_len),
        static_cast<unsigned char**>(&post_no_redefine_dex_data));
    if (post_no_redefine_dex_data == nullptr) {
      DCHECK_EQ(post_no_redefine_len, 0);
      post_no_redefine_dex_data = const_cast<unsigned char*>(initial_dex_file.Begin());
      post_no_redefine_len = initial_dex_file.Size();
    } else {
      post_no_redefine_unique_ptr = std::unique_ptr<const unsigned char>(post_no_redefine_dex_data);
      DCHECK_GT(post_no_redefine_len, 0);
    }
    // Call all retransformable agents.
    jint final_len = 0;
    unsigned char* final_dex_data = nullptr;
    std::unique_ptr<const unsigned char> final_dex_unique_ptr(nullptr);
    event_handler->DispatchEvent<ArtJvmtiEvent::kClassFileLoadHookRetransformable>(
        self,
        static_cast<JNIEnv*>(env),
        static_cast<jclass>(nullptr),  // The class doesn't really exist yet so send null.
        loader.get(),
        name.c_str(),
        static_cast<jobject>(nullptr),  // Android doesn't seem to have protection domains
        static_cast<jint>(post_no_redefine_len),
        static_cast<const unsigned char*>(post_no_redefine_dex_data),
        static_cast<jint*>(&final_len),
        static_cast<unsigned char**>(&final_dex_data));
    if (final_dex_data == nullptr) {
      DCHECK_EQ(final_len, 0);
      final_dex_data = post_no_redefine_dex_data;
      final_len = post_no_redefine_len;
    } else {
      final_dex_unique_ptr = std::unique_ptr<const unsigned char>(final_dex_data);
      DCHECK_GT(final_len, 0);
    }

    if (final_dex_data != initial_dex_file.Begin()) {
      LOG(WARNING) << "Changing class " << descriptor;
      art::ScopedObjectAccess soa(self);
      art::StackHandleScope<2> hs(self);
      // Save the results of all the non-retransformable agents.
      // First allocate the ClassExt
      art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->EnsureExtDataPresent(self)));
      // Make sure we have a ClassExt. This is fine even though we are a temporary since it will
      // get copied.
      if (ext.IsNull()) {
        // We will just return failure if we fail to allocate
        LOG(WARNING) << "Could not allocate ext-data for class '" << descriptor << "'. "
                     << "Aborting transformation since we will be unable to store it.";
        self->AssertPendingOOMException();
        return;
      }

      // Allocate the byte array to store the dex file bytes in.
      art::Handle<art::mirror::ByteArray> arr(hs.NewHandle(
          art::mirror::ByteArray::AllocateAndFill(
              self,
              reinterpret_cast<const signed char*>(post_no_redefine_dex_data),
              post_no_redefine_len)));
      if (arr.IsNull()) {
        LOG(WARNING) << "Unable to allocate byte array for initial dex-file bytes. Aborting "
                     << "transformation";
        self->AssertPendingOOMException();
        return;
      }

      std::unique_ptr<const art::DexFile> dex_file(MakeSingleDexFile(self,
                                                                     descriptor,
                                                                     initial_dex_file.GetLocation(),
                                                                     final_len,
                                                                     final_dex_data));
      if (dex_file.get() == nullptr) {
        return;
      }

      // TODO Check Redefined dex file for invariants.
      LOG(WARNING) << "Dex file created by class-definition time transformation of "
                   << descriptor << " is not checked for all retransformation invariants.";
      // TODO Put it in classpath
      LOG(WARNING) << "Dex file created for class-definition time transformation of "
                   << descriptor << " was not added to any classpaths!";
      // Actually set the ClassExt's original bytes once we have actually succeeded.
      ext->SetOriginalDexFileBytes(arr.Get());
      // Set the return values
      *final_class_def = &dex_file->GetClassDef(0);
      *final_dex_file = dex_file.release();
    }
  }

  void ClassLoad(art::Handle<art::mirror::Class> klass) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (event_handler->IsEventEnabledAnywhere(ArtJvmtiEvent::kClassLoad)) {
      art::Thread* thread = art::Thread::Current();
      ScopedLocalRef<jclass> jklass(thread->GetJniEnv(),
                                    thread->GetJniEnv()->AddLocalReference<jclass>(klass.Get()));
      ScopedLocalRef<jthread> thread_jni(
          thread->GetJniEnv(), thread->GetJniEnv()->AddLocalReference<jthread>(thread->GetPeer()));
      {
        art::ScopedThreadSuspension sts(thread, art::ThreadState::kNative);
        event_handler->DispatchEvent<ArtJvmtiEvent::kClassLoad>(
            thread,
            static_cast<JNIEnv*>(thread->GetJniEnv()),
            thread_jni.get(),
            jklass.get());
      }
      AddTempClass(thread, jklass.get());
    }
  }

  void ClassPrepare(art::Handle<art::mirror::Class> temp_klass ATTRIBUTE_UNUSED,
                    art::Handle<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (event_handler->IsEventEnabledAnywhere(ArtJvmtiEvent::kClassPrepare)) {
      art::Thread* thread = art::Thread::Current();
      ScopedLocalRef<jclass> jklass(thread->GetJniEnv(),
                                    thread->GetJniEnv()->AddLocalReference<jclass>(klass.Get()));
      ScopedLocalRef<jthread> thread_jni(
          thread->GetJniEnv(), thread->GetJniEnv()->AddLocalReference<jthread>(thread->GetPeer()));
      art::ScopedThreadSuspension sts(thread, art::ThreadState::kNative);
      event_handler->DispatchEvent<ArtJvmtiEvent::kClassPrepare>(
          thread,
          static_cast<JNIEnv*>(thread->GetJniEnv()),
          thread_jni.get(),
          jklass.get());
    }
  }

  void AddTempClass(art::Thread* self, jclass klass) {
    std::unique_lock<std::mutex> mu(temp_classes_lock);
    temp_classes.push_back(reinterpret_cast<jclass>(self->GetJniEnv()->NewGlobalRef(klass)));
  }

  void HandleTempClass(art::Handle<art::mirror::Class> temp_klass,
                       art::Handle<art::mirror::Class> klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    std::unique_lock<std::mutex> mu(temp_classes_lock);
    if (temp_classes.empty()) {
      return;
    }

    art::Thread* self = art::Thread::Current();
    for (auto it = temp_classes.begin(); it != temp_classes.end(); ++it) {
      if (temp_klass.Get() == art::ObjPtr<art::mirror::Class>::DownCast(self->DecodeJObject(*it))) {
        temp_classes.erase(it);
        FixupTempClass(temp_klass, klass);
      }
    }
  }

  void FixupTempClass(art::Handle<art::mirror::Class> temp_klass ATTRIBUTE_UNUSED,
                      art::Handle<art::mirror::Class> klass ATTRIBUTE_UNUSED)
     REQUIRES_SHARED(art::Locks::mutator_lock_) {
    // TODO: Implement.
  }

  // A set of all the temp classes we have handed out. We have to fix up references to these.
  // For simplicity, we store the temp classes as JNI global references in a vector. Normally a
  // Prepare event will closely follow, so the vector should be small.
  std::mutex temp_classes_lock;
  std::vector<jclass> temp_classes;

  EventHandler* event_handler = nullptr;
};

ClassCallback gClassCallback;

void ClassUtil::Register(EventHandler* handler) {
  gClassCallback.event_handler = handler;
  art::ScopedThreadStateChange stsc(art::Thread::Current(),
                                    art::ThreadState::kWaitingForDebuggerToAttach);
  art::ScopedSuspendAll ssa("Add load callback");
  art::Runtime::Current()->GetRuntimeCallbacks()->AddClassLoadCallback(&gClassCallback);
}

void ClassUtil::Unregister() {
  art::ScopedThreadStateChange stsc(art::Thread::Current(),
                                    art::ThreadState::kWaitingForDebuggerToAttach);
  art::ScopedSuspendAll ssa("Remove thread callback");
  art::Runtime* runtime = art::Runtime::Current();
  runtime->GetRuntimeCallbacks()->RemoveClassLoadCallback(&gClassCallback);
}

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
  if (generic_ptr != nullptr) {
    *generic_ptr = nullptr;
  }

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

jvmtiError ClassUtil::GetClassLoaderClasses(jvmtiEnv* env,
                                            jobject initiating_loader,
                                            jint* class_count_ptr,
                                            jclass** classes_ptr) {
  UNUSED(env, initiating_loader, class_count_ptr, classes_ptr);

  if (class_count_ptr == nullptr || classes_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }
  art::Thread* self = art::Thread::Current();
  if (!self->GetJniEnv()->IsInstanceOf(initiating_loader,
                                       art::WellKnownClasses::java_lang_ClassLoader)) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (self->GetJniEnv()->IsInstanceOf(initiating_loader,
                                      art::WellKnownClasses::java_lang_BootClassLoader)) {
    // Need to use null for the BootClassLoader.
    initiating_loader = nullptr;
  }

  art::ScopedObjectAccess soa(self);
  art::ObjPtr<art::mirror::ClassLoader> class_loader =
      soa.Decode<art::mirror::ClassLoader>(initiating_loader);

  art::ClassLinker* class_linker = art::Runtime::Current()->GetClassLinker();

  art::ReaderMutexLock mu(self, *art::Locks::classlinker_classes_lock_);

  art::ClassTable* class_table = class_linker->ClassTableForClassLoader(class_loader);
  if (class_table == nullptr) {
    // Nothing loaded.
    *class_count_ptr = 0;
    *classes_ptr = nullptr;
    return ERR(NONE);
  }

  struct ClassTableCount {
    bool operator()(art::ObjPtr<art::mirror::Class> klass) {
      DCHECK(klass != nullptr);
      ++count;
      return true;
    }

    size_t count = 0;
  };
  ClassTableCount ctc;
  class_table->Visit(ctc);

  if (ctc.count == 0) {
    // Nothing loaded.
    *class_count_ptr = 0;
    *classes_ptr = nullptr;
    return ERR(NONE);
  }

  unsigned char* data;
  jvmtiError data_result = env->Allocate(ctc.count * sizeof(jclass), &data);
  if (data_result != ERR(NONE)) {
    return data_result;
  }
  jclass* class_array = reinterpret_cast<jclass*>(data);

  struct ClassTableFill {
    bool operator()(art::ObjPtr<art::mirror::Class> klass)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      DCHECK(klass != nullptr);
      DCHECK_LT(count, ctc_ref.count);
      local_class_array[count++] = soa_ptr->AddLocalReference<jclass>(klass);
      return true;
    }

    jclass* local_class_array;
    const ClassTableCount& ctc_ref;
    art::ScopedObjectAccess* soa_ptr;
    size_t count;
  };
  ClassTableFill ctf = { class_array, ctc, &soa, 0 };
  class_table->Visit(ctf);
  DCHECK_EQ(ctc.count, ctf.count);

  *class_count_ptr = ctc.count;
  *classes_ptr = class_array;

  return ERR(NONE);
}

jvmtiError ClassUtil::GetClassVersionNumbers(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                             jclass jklass,
                                             jint* minor_version_ptr,
                                             jint* major_version_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  if (jklass == nullptr) {
    return ERR(INVALID_CLASS);
  }
  art::ObjPtr<art::mirror::Object> jklass_obj = soa.Decode<art::mirror::Object>(jklass);
  if (!jklass_obj->IsClass()) {
    return ERR(INVALID_CLASS);
  }
  art::ObjPtr<art::mirror::Class> klass = jklass_obj->AsClass();
  if (klass->IsPrimitive() || klass->IsArrayClass()) {
    return ERR(INVALID_CLASS);
  }

  if (minor_version_ptr == nullptr || major_version_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  // Note: proxies will show the dex file version of java.lang.reflect.Proxy, as that is
  //       what their dex cache copies from.
  uint32_t version = klass->GetDexFile().GetHeader().GetVersion();

  *major_version_ptr = static_cast<jint>(version);
  *minor_version_ptr = 0;

  return ERR(NONE);
}

}  // namespace openjdkjvmti
