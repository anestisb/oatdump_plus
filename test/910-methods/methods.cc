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

#include "methods.h"

#include <stdio.h>

#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"

#include "ti-agent/common_load.h"

namespace art {
namespace Test910Methods {

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getMethodName(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject method) {
  jmethodID id = env->FromReflectedMethod(method);

  char* name;
  char* sig;
  char* gen;
  jvmtiError result = jvmti_env->GetMethodName(id, &name, &sig, &gen);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetMethodName: %s\n", err);
    return nullptr;
  }

  ScopedLocalRef<jclass> obj_class(env, env->FindClass("java/lang/String"));
  if (obj_class.get() == nullptr) {
    return nullptr;
  }

  jobjectArray ret = env->NewObjectArray(3, obj_class.get(), nullptr);
  if (ret == nullptr) {
    return ret;
  }

  ScopedLocalRef<jstring> name_str(env, name == nullptr ? nullptr : env->NewStringUTF(name));
  ScopedLocalRef<jstring> sig_str(env, sig == nullptr ? nullptr : env->NewStringUTF(sig));
  ScopedLocalRef<jstring> gen_str(env, gen == nullptr ? nullptr : env->NewStringUTF(gen));

  env->SetObjectArrayElement(ret, 0, name_str.get());
  env->SetObjectArrayElement(ret, 1, sig_str.get());
  env->SetObjectArrayElement(ret, 2, gen_str.get());

  // Need to deallocate the strings.
  if (name != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(name));
  }
  if (sig != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
  }
  if (gen != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(gen));
  }

  return ret;
}

extern "C" JNIEXPORT jclass JNICALL Java_Main_getMethodDeclaringClass(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject method) {
  jmethodID id = env->FromReflectedMethod(method);

  jclass declaring_class;
  jvmtiError result = jvmti_env->GetMethodDeclaringClass(id, &declaring_class);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetMethodDeclaringClass: %s\n", err);
    return nullptr;
  }

  return declaring_class;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getMethodModifiers(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject method) {
  jmethodID id = env->FromReflectedMethod(method);

  jint modifiers;
  jvmtiError result = jvmti_env->GetMethodModifiers(id, &modifiers);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetMethodModifiers: %s\n", err);
    return 0;
  }

  return modifiers;
}

// Don't do anything
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  return 0;
}

}  // namespace Test910Methods
}  // namespace art
