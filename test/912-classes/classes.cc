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

#include "classes.h"

#include <stdio.h>

#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test912Classes {

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassSignature(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  char* sig;
  char* gen;
  jvmtiError result = jvmti_env->GetClassSignature(klass, &sig, &gen);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassSignature: %s\n", err);
    return nullptr;
  }

  auto callback = [&](jint i) {
    if (i == 0) {
      return sig == nullptr ? nullptr : env->NewStringUTF(sig);
    } else {
      return gen == nullptr ? nullptr : env->NewStringUTF(gen);
    }
  };
  jobjectArray ret = CreateObjectArray(env, 2, "java/lang/String", callback);

  // Need to deallocate the strings.
  if (sig != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
  }
  if (gen != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(gen));
  }

  return ret;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isInterface(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jboolean is_interface = JNI_FALSE;
  jvmtiError result = jvmti_env->IsInterface(klass, &is_interface);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running IsInterface: %s\n", err);
    return JNI_FALSE;
  }
  return is_interface;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isArrayClass(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jboolean is_array_class = JNI_FALSE;
  jvmtiError result = jvmti_env->IsArrayClass(klass, &is_array_class);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running IsArrayClass: %s\n", err);
    return JNI_FALSE;
  }
  return is_array_class;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassFields(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint count = 0;
  jfieldID* fields = nullptr;
  jvmtiError result = jvmti_env->GetClassFields(klass, &count, &fields);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassFields: %s\n", err);
    return nullptr;
  }

  auto callback = [&](jint i) {
    jint modifiers;
    // Ignore any errors for simplicity.
    jvmti_env->GetFieldModifiers(klass, fields[i], &modifiers);
    constexpr jint kStatic = 0x8;
    return env->ToReflectedField(klass,
                                 fields[i],
                                 (modifiers & kStatic) != 0 ? JNI_TRUE : JNI_FALSE);
  };
  return CreateObjectArray(env, count, "java/lang/Object", callback);
}

// Don't do anything
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetAllCapabilities(jvmti_env);
  return 0;
}

}  // namespace Test912Classes
}  // namespace art
