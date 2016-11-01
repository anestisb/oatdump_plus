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

#include "heaps.h"

#include <stdio.h>
#include <string.h>

#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"

#include "ti-agent/common_load.h"

namespace art {
namespace Test913Heaps {

extern "C" JNIEXPORT void JNICALL Java_Main_forceGarbageCollection(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                   jclass klass ATTRIBUTE_UNUSED) {
  jvmtiError ret = jvmti_env->ForceGarbageCollection();
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error forcing a garbage collection: %s\n", err);
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
  return 0;
}

}  // namespace Test913Heaps
}  // namespace art
