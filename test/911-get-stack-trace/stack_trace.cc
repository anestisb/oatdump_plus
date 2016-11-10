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

#include "stack_trace.h"

#include <memory>
#include <stdio.h>

#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"
#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test911GetStackTrace {

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getStackTrace(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jthread thread, jint start, jint max) {
  std::unique_ptr<jvmtiFrameInfo[]> frames(new jvmtiFrameInfo[max]);

  jint count;
  {
    jvmtiError result = jvmti_env->GetStackTrace(thread, start, max, frames.get(), &count);
    if (result != JVMTI_ERROR_NONE) {
      char* err;
      jvmti_env->GetErrorName(result, &err);
      printf("Failure running GetStackTrace: %s\n", err);
      return nullptr;
    }
  }

  auto callback = [&](jint i) -> jstring {
    size_t method_index = static_cast<size_t>(i) / 2;
    char* name;
    char* sig;
    char* gen;
    {
      jvmtiError result2 = jvmti_env->GetMethodName(frames[method_index].method, &name, &sig, &gen);
      if (result2 != JVMTI_ERROR_NONE) {
        char* err;
        jvmti_env->GetErrorName(result2, &err);
        printf("Failure running GetMethodName: %s\n", err);
        return nullptr;
      }
    }
    jstring callback_result;
    if (i % 2 == 0) {
      callback_result = name == nullptr ? nullptr : env->NewStringUTF(name);
    } else {
      callback_result = sig == nullptr ? nullptr : env->NewStringUTF(sig);
    }

    if (name != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(name));
    }
    if (sig != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
    }
    if (gen != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(gen));
    }
    return callback_result;
  };
  return CreateObjectArray(env, 2 * count, "java/lang/String", callback);
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

}  // namespace Test911GetStackTrace
}  // namespace art
