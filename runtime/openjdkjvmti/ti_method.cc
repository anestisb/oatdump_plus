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
#include "dex_file_annotations.h"
#include "events-inl.h"
#include "jni_internal.h"
#include "mirror/object_array-inl.h"
#include "modifiers.h"
#include "runtime_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "ti_phase.h"

namespace openjdkjvmti {

struct TiMethodCallback : public art::MethodCallback {
  void RegisterNativeMethod(art::ArtMethod* method,
                            const void* cur_method,
                            /*out*/void** new_method)
      OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (event_handler->IsEventEnabledAnywhere(ArtJvmtiEvent::kNativeMethodBind)) {
      art::Thread* thread = art::Thread::Current();
      art::JNIEnvExt* jnienv = thread->GetJniEnv();
      ScopedLocalRef<jthread> thread_jni(
          jnienv, PhaseUtil::IsLivePhase() ? jnienv->AddLocalReference<jthread>(thread->GetPeer())
                                           : nullptr);
      art::ScopedThreadSuspension sts(thread, art::ThreadState::kNative);
      event_handler->DispatchEvent<ArtJvmtiEvent::kNativeMethodBind>(
          thread,
          static_cast<JNIEnv*>(jnienv),
          thread_jni.get(),
          art::jni::EncodeArtMethod(method),
          const_cast<void*>(cur_method),
          new_method);
    }
  }

  EventHandler* event_handler = nullptr;
};

TiMethodCallback gMethodCallback;

void MethodUtil::Register(EventHandler* handler) {
  gMethodCallback.event_handler = handler;
  art::ScopedThreadStateChange stsc(art::Thread::Current(),
                                    art::ThreadState::kWaitingForDebuggerToAttach);
  art::ScopedSuspendAll ssa("Add method callback");
  art::Runtime::Current()->GetRuntimeCallbacks()->AddMethodCallback(&gMethodCallback);
}

void MethodUtil::Unregister() {
  art::ScopedThreadStateChange stsc(art::Thread::Current(),
                                    art::ThreadState::kWaitingForDebuggerToAttach);
  art::ScopedSuspendAll ssa("Remove method callback");
  art::Runtime* runtime = art::Runtime::Current();
  runtime->GetRuntimeCallbacks()->RemoveMethodCallback(&gMethodCallback);
}

jvmtiError MethodUtil::GetArgumentsSize(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                        jmethodID method,
                                        jint* size_ptr) {
  if (method == nullptr) {
    return ERR(INVALID_METHODID);
  }
  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);

  if (art_method->IsNative()) {
    return ERR(NATIVE_METHOD);
  }

  if (size_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  if (art_method->IsProxyMethod() || art_method->IsAbstract()) {
    // Use the shorty.
    art::ArtMethod* base_method = art_method->GetInterfaceMethodIfProxy(art::kRuntimePointerSize);
    size_t arg_count = art::ArtMethod::NumArgRegisters(base_method->GetShorty());
    if (!base_method->IsStatic()) {
      arg_count++;
    }
    *size_ptr = static_cast<jint>(arg_count);
    return ERR(NONE);
  }

  DCHECK_NE(art_method->GetCodeItemOffset(), 0u);
  *size_ptr = art_method->GetCodeItem()->ins_size_;

  return ERR(NONE);
}

jvmtiError MethodUtil::GetMaxLocals(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                    jmethodID method,
                                    jint* max_ptr) {
  if (method == nullptr) {
    return ERR(INVALID_METHODID);
  }
  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);

  if (art_method->IsNative()) {
    return ERR(NATIVE_METHOD);
  }

  if (max_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  if (art_method->IsProxyMethod() || art_method->IsAbstract()) {
    // This isn't specified as an error case, so return 0.
    *max_ptr = 0;
    return ERR(NONE);
  }

  DCHECK_NE(art_method->GetCodeItemOffset(), 0u);
  *max_ptr = art_method->GetCodeItem()->registers_size_;

  return ERR(NONE);
}

jvmtiError MethodUtil::GetMethodName(jvmtiEnv* env,
                                     jmethodID method,
                                     char** name_ptr,
                                     char** signature_ptr,
                                     char** generic_ptr) {
  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);
  art_method = art_method->GetInterfaceMethodIfProxy(art::kRuntimePointerSize);

  JvmtiUniquePtr<char[]> name_copy;
  if (name_ptr != nullptr) {
    const char* method_name = art_method->GetName();
    if (method_name == nullptr) {
      method_name = "<error>";
    }
    jvmtiError ret;
    name_copy = CopyString(env, method_name, &ret);
    if (name_copy == nullptr) {
      return ret;
    }
    *name_ptr = name_copy.get();
  }

  JvmtiUniquePtr<char[]> signature_copy;
  if (signature_ptr != nullptr) {
    const art::Signature sig = art_method->GetSignature();
    std::string str = sig.ToString();
    jvmtiError ret;
    signature_copy = CopyString(env, str.c_str(), &ret);
    if (signature_copy == nullptr) {
      return ret;
    }
    *signature_ptr = signature_copy.get();
  }

  if (generic_ptr != nullptr) {
    *generic_ptr = nullptr;
    if (!art_method->GetDeclaringClass()->IsProxyClass()) {
      art::mirror::ObjectArray<art::mirror::String>* str_array =
          art::annotations::GetSignatureAnnotationForMethod(art_method);
      if (str_array != nullptr) {
        std::ostringstream oss;
        for (int32_t i = 0; i != str_array->GetLength(); ++i) {
          oss << str_array->Get(i)->ToModifiedUtf8();
        }
        std::string output_string = oss.str();
        jvmtiError ret;
        JvmtiUniquePtr<char[]> generic_copy = CopyString(env, output_string.c_str(), &ret);
        if (generic_copy == nullptr) {
          return ret;
        }
        *generic_ptr = generic_copy.release();
      } else if (soa.Self()->IsExceptionPending()) {
        // TODO: Should we report an error here?
        soa.Self()->ClearException();
      }
    }
  }

  // Everything is fine, release the buffers.
  name_copy.release();
  signature_copy.release();

  return ERR(NONE);
}

jvmtiError MethodUtil::GetMethodDeclaringClass(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                               jmethodID method,
                                               jclass* declaring_class_ptr) {
  if (declaring_class_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);
  // Note: No GetInterfaceMethodIfProxy, we want to actual class.

  art::ScopedObjectAccess soa(art::Thread::Current());
  art::mirror::Class* klass = art_method->GetDeclaringClass();
  *declaring_class_ptr = soa.AddLocalReference<jclass>(klass);

  return ERR(NONE);
}

jvmtiError MethodUtil::GetMethodLocation(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                         jmethodID method,
                                         jlocation* start_location_ptr,
                                         jlocation* end_location_ptr) {
  if (method == nullptr) {
    return ERR(INVALID_METHODID);
  }
  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);

  if (art_method->IsNative()) {
    return ERR(NATIVE_METHOD);
  }

  if (start_location_ptr == nullptr || end_location_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ScopedObjectAccess soa(art::Thread::Current());
  if (art_method->IsProxyMethod() || art_method->IsAbstract()) {
    // This isn't specified as an error case, so return -1/-1 as the RI does.
    *start_location_ptr = -1;
    *end_location_ptr = -1;
    return ERR(NONE);
  }

  DCHECK_NE(art_method->GetCodeItemOffset(), 0u);
  *start_location_ptr = 0;
  *end_location_ptr = art_method->GetCodeItem()->insns_size_in_code_units_ - 1;

  return ERR(NONE);
}

jvmtiError MethodUtil::GetMethodModifiers(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                          jmethodID method,
                                          jint* modifiers_ptr) {
  if (modifiers_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);
  uint32_t modifiers = art_method->GetAccessFlags();

  // Note: Keep this code in sync with Executable.fixMethodFlags.
  if ((modifiers & art::kAccAbstract) != 0) {
    modifiers &= ~art::kAccNative;
  }
  modifiers &= ~art::kAccSynchronized;
  if ((modifiers & art::kAccDeclaredSynchronized) != 0) {
    modifiers |= art::kAccSynchronized;
  }
  modifiers &= art::kAccJavaFlagsMask;

  *modifiers_ptr = modifiers;
  return ERR(NONE);
}

using LineNumberContext = std::vector<jvmtiLineNumberEntry>;

static bool CollectLineNumbers(void* void_context, const art::DexFile::PositionInfo& entry) {
  LineNumberContext* context = reinterpret_cast<LineNumberContext*>(void_context);
  jvmtiLineNumberEntry jvmti_entry = { static_cast<jlocation>(entry.address_),
                                       static_cast<jint>(entry.line_) };
  context->push_back(jvmti_entry);
  return false;  // Collect all, no early exit.
}

jvmtiError MethodUtil::GetLineNumberTable(jvmtiEnv* env,
                                          jmethodID method,
                                          jint* entry_count_ptr,
                                          jvmtiLineNumberEntry** table_ptr) {
  if (method == nullptr) {
    return ERR(NULL_POINTER);
  }
  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);
  DCHECK(!art_method->IsRuntimeMethod());

  const art::DexFile::CodeItem* code_item;
  const art::DexFile* dex_file;
  {
    art::ScopedObjectAccess soa(art::Thread::Current());

    if (art_method->IsProxyMethod()) {
      return ERR(ABSENT_INFORMATION);
    }
    if (art_method->IsNative()) {
      return ERR(NATIVE_METHOD);
    }
    if (entry_count_ptr == nullptr || table_ptr == nullptr) {
      return ERR(NULL_POINTER);
    }

    code_item = art_method->GetCodeItem();
    dex_file = art_method->GetDexFile();
    DCHECK(code_item != nullptr) << art_method->PrettyMethod() << " " << dex_file->GetLocation();
  }

  LineNumberContext context;
  bool success = dex_file->DecodeDebugPositionInfo(code_item, CollectLineNumbers, &context);
  if (!success) {
    return ERR(ABSENT_INFORMATION);
  }

  unsigned char* data;
  jlong mem_size = context.size() * sizeof(jvmtiLineNumberEntry);
  jvmtiError alloc_error = env->Allocate(mem_size, &data);
  if (alloc_error != ERR(NONE)) {
    return alloc_error;
  }
  *table_ptr = reinterpret_cast<jvmtiLineNumberEntry*>(data);
  memcpy(*table_ptr, context.data(), mem_size);
  *entry_count_ptr = static_cast<jint>(context.size());

  return ERR(NONE);
}

template <typename T>
static jvmtiError IsMethodT(jvmtiEnv* env ATTRIBUTE_UNUSED,
                            jmethodID method,
                            T test,
                            jboolean* is_t_ptr) {
  if (method == nullptr) {
    return ERR(INVALID_METHODID);
  }
  if (is_t_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  art::ArtMethod* art_method = art::jni::DecodeArtMethod(method);
  *is_t_ptr = test(art_method) ? JNI_TRUE : JNI_FALSE;

  return ERR(NONE);
}

jvmtiError MethodUtil::IsMethodNative(jvmtiEnv* env, jmethodID m, jboolean* is_native_ptr) {
  auto test = [](art::ArtMethod* method) {
    return method->IsNative();
  };
  return IsMethodT(env, m, test, is_native_ptr);
}

jvmtiError MethodUtil::IsMethodObsolete(jvmtiEnv* env, jmethodID m, jboolean* is_obsolete_ptr) {
  auto test = [](art::ArtMethod* method) {
    return method->IsObsolete();
  };
  return IsMethodT(env, m, test, is_obsolete_ptr);
}

jvmtiError MethodUtil::IsMethodSynthetic(jvmtiEnv* env, jmethodID m, jboolean* is_synthetic_ptr) {
  auto test = [](art::ArtMethod* method) {
    return method->IsSynthetic();
  };
  return IsMethodT(env, m, test, is_synthetic_ptr);
}

}  // namespace openjdkjvmti
