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

#include "get_loaded_classes.h"

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"
#include "ScopedUtfChars.h"

#include "ti-agent/common_load.h"

namespace art {
namespace Test907GetLoadedClasses {

static jstring GetClassName(JNIEnv* jni_env, jclass cls) {
  ScopedLocalRef<jclass> class_class(jni_env, jni_env->GetObjectClass(cls));
  jmethodID mid = jni_env->GetMethodID(class_class.get(), "getName", "()Ljava/lang/String;");
  return reinterpret_cast<jstring>(jni_env->CallObjectMethod(cls, mid));
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getLoadedClasses(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED) {
  jint count = -1;
  jclass* classes = nullptr;
  jvmtiError result = jvmti_env->GetLoadedClasses(&count, &classes);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetLoadedClasses: %s\n", err);
    return nullptr;
  }

  ScopedLocalRef<jclass> obj_class(env, env->FindClass("java/lang/String"));
  if (obj_class.get() == nullptr) {
    return nullptr;
  }

  jobjectArray ret = env->NewObjectArray(count, obj_class.get(), nullptr);
  if (ret == nullptr) {
    return ret;
  }

  for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
    jstring class_name = GetClassName(env, classes[i]);
    env->SetObjectArrayElement(ret, static_cast<jint>(i), class_name);
    env->DeleteLocalRef(class_name);
  }

  // Need to:
  // 1) Free the local references.
  // 2) Deallocate.
  for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
    env->DeleteLocalRef(classes[i]);
  }
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(classes));

  return ret;
}

}  // namespace Test907GetLoadedClasses
}  // namespace art
