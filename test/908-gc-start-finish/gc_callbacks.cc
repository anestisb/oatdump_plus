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

#include "gc_callbacks.h"

#include <stdio.h>
#include <string.h>

#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test908GcStartFinish {

static size_t starts = 0;
static size_t finishes = 0;

static void JNICALL GarbageCollectionFinish(jvmtiEnv* ti_env ATTRIBUTE_UNUSED) {
  finishes++;
}

static void JNICALL GarbageCollectionStart(jvmtiEnv* ti_env ATTRIBUTE_UNUSED) {
  starts++;
}

extern "C" JNIEXPORT void JNICALL Java_Main_setupGcCallback(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass klass ATTRIBUTE_UNUSED) {
  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.GarbageCollectionFinish = GarbageCollectionFinish;
  callbacks.GarbageCollectionStart = GarbageCollectionStart;

  jvmtiError ret = jvmti_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error setting callbacks: %s\n", err);
  }
}

extern "C" JNIEXPORT void JNICALL Java_Main_enableGcTracking(JNIEnv* env ATTRIBUTE_UNUSED,
                                                             jclass klass ATTRIBUTE_UNUSED,
                                                             jboolean enable) {
  jvmtiError ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_GARBAGE_COLLECTION_START,
      nullptr);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error enabling/disabling gc callbacks: %s\n", err);
  }
  ret = jvmti_env->SetEventNotificationMode(
      enable ? JVMTI_ENABLE : JVMTI_DISABLE,
      JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
      nullptr);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error enabling/disabling gc callbacks: %s\n", err);
  }
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getGcStarts(JNIEnv* env ATTRIBUTE_UNUSED,
                                                        jclass klass ATTRIBUTE_UNUSED) {
  jint result = static_cast<jint>(starts);
  starts = 0;
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getGcFinishes(JNIEnv* env ATTRIBUTE_UNUSED,
                                                          jclass klass ATTRIBUTE_UNUSED) {
  jint result = static_cast<jint>(finishes);
  finishes = 0;
  return result;
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

}  // namespace Test908GcStartFinish
}  // namespace art
