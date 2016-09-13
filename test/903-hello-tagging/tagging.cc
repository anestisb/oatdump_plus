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

#include "tagging.h"

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "art_method-inl.h"
#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "utils.h"

namespace art {
namespace Test903HelloTagging {

static jvmtiEnv* jvmti_env;

extern "C" JNIEXPORT void JNICALL Java_Main_setTag(JNIEnv* env ATTRIBUTE_UNUSED,
                                                   jclass,
                                                   jobject obj,
                                                   jlong tag) {
  jvmtiError ret = jvmti_env->SetTag(obj, tag);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error setting tag: %s\n", err);
  }
}

extern "C" JNIEXPORT jlong JNICALL Java_Main_getTag(JNIEnv* env ATTRIBUTE_UNUSED,
                                                    jclass,
                                                    jobject obj) {
  jlong tag = 0;
  jvmtiError ret = jvmti_env->GetTag(obj, &tag);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error getting tag: %s\n", err);
  }
  return tag;
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

}  // namespace Test903HelloTagging
}  // namespace art

