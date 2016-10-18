/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tracking.h"

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"
#include "ScopedUtfChars.h"
#include "ti-agent/common_load.h"
#include "utils.h"

namespace art {
namespace Test904ObjectAllocation {

static std::string GetClassName(JNIEnv* jni_env, jclass cls) {
  ScopedLocalRef<jclass> class_class(jni_env, jni_env->GetObjectClass(cls));
  jmethodID mid = jni_env->GetMethodID(class_class.get(), "getName", "()Ljava/lang/String;");
  ScopedLocalRef<jstring> str(
      jni_env, reinterpret_cast<jstring>(jni_env->CallObjectMethod(cls, mid)));
  ScopedUtfChars utf_chars(jni_env, str.get());
  return utf_chars.c_str();
}

static void JNICALL ObjectAllocated(jvmtiEnv* ti_env ATTRIBUTE_UNUSED,
                                    JNIEnv* jni_env,
                                    jthread thread ATTRIBUTE_UNUSED,
                                    jobject object,
                                    jclass object_klass,
                                    jlong size) {
  std::string object_klass_descriptor = GetClassName(jni_env, object_klass);
  ScopedLocalRef<jclass> object_klass2(jni_env, jni_env->GetObjectClass(object));
  std::string object_klass_descriptor2 = GetClassName(jni_env, object_klass2.get());

  printf("ObjectAllocated type %s/%s size %zu\n",
         object_klass_descriptor.c_str(),
         object_klass_descriptor2.c_str(),
         static_cast<size_t>(size));
}

extern "C" JNIEXPORT void JNICALL Java_Main_setupObjectAllocCallback(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass klass ATTRIBUTE_UNUSED, jboolean enable) {
  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.VMObjectAlloc = enable ? ObjectAllocated : nullptr;

  jvmtiError ret = jvmti_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error setting callbacks: %s\n", err);
  }
}

extern "C" JNIEXPORT void JNICALL Java_Main_enableAllocationTracking(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                     jclass,
                                                                     jthread thread,
                                                                     jboolean enable) {
  jvmtiError ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_VM_OBJECT_ALLOC,
      thread);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error enabling/disabling allocation tracking: %s\n", err);
  }
}

// Don't do anything
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  jvmti_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_OBJECT_ALLOC, nullptr);
  return 0;
}

}  // namespace Test904ObjectAllocation
}  // namespace art

