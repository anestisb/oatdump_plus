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

#include "tracking_free.h"

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
namespace Test905ObjectFree {

static std::vector<jlong> collected_tags;

static void JNICALL ObjectFree(jvmtiEnv* ti_env ATTRIBUTE_UNUSED, jlong tag) {
  collected_tags.push_back(tag);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setupObjectFreeCallback(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass klass ATTRIBUTE_UNUSED) {
  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.ObjectFree = ObjectFree;

  jvmtiError ret = jvmti_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error setting callbacks: %s\n", err);
  }
}

extern "C" JNIEXPORT void JNICALL Java_Main_enableFreeTracking(JNIEnv* env ATTRIBUTE_UNUSED,
                                                               jclass klass ATTRIBUTE_UNUSED,
                                                               jboolean enable) {
  jvmtiError ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_OBJECT_FREE,
      nullptr);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error enabling/disabling object-free callbacks: %s\n", err);
  }
}

extern "C" JNIEXPORT jlongArray JNICALL Java_Main_getCollectedTags(JNIEnv* env,
                                                                   jclass klass ATTRIBUTE_UNUSED) {
  jlongArray ret = env->NewLongArray(collected_tags.size());
  if (ret == nullptr) {
    return ret;
  }

  env->SetLongArrayRegion(ret, 0, collected_tags.size(), collected_tags.data());
  collected_tags.clear();

  return ret;
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

}  // namespace Test905ObjectFree
}  // namespace art
