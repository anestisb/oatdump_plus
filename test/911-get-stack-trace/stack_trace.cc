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

#include <inttypes.h>
#include <memory>
#include <stdio.h>

#include "android-base/stringprintf.h"

#include "base/logging.h"
#include "base/macros.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedLocalRef.h"
#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test911GetStackTrace {

using android::base::StringPrintf;

static jint FindLineNumber(jint line_number_count,
                           jvmtiLineNumberEntry* line_number_table,
                           jlocation location) {
  if (line_number_table == nullptr) {
    return -2;
  }

  jint line_number = -1;
  for (jint i = 0; i != line_number_count; ++i) {
    if (line_number_table[i].start_location > location) {
      return line_number;
    }
    line_number = line_number_table[i].line_number;
  }
  return line_number;
}

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
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
      return nullptr;
    }
  }

  auto callback = [&](jint method_index) -> jobjectArray {
    char* name;
    char* sig;
    char* gen;
    {
      jvmtiError result2 = jvmti_env->GetMethodName(frames[method_index].method, &name, &sig, &gen);
      if (result2 != JVMTI_ERROR_NONE) {
        char* err;
        jvmti_env->GetErrorName(result2, &err);
        printf("Failure running GetMethodName: %s\n", err);
        jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
        return nullptr;
      }
    }

    jint line_number_count;
    jvmtiLineNumberEntry* line_number_table;
    {
      jvmtiError line_result = jvmti_env->GetLineNumberTable(frames[method_index].method,
                                                             &line_number_count,
                                                             &line_number_table);
      if (line_result != JVMTI_ERROR_NONE) {
        // Accept absent info and native method errors.
        if (line_result != JVMTI_ERROR_ABSENT_INFORMATION &&
            line_result != JVMTI_ERROR_NATIVE_METHOD) {
          char* err;
          jvmti_env->GetErrorName(line_result, &err);
          printf("Failure running GetLineNumberTable: %s\n", err);
          jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
          return nullptr;
        }
        line_number_table = nullptr;
        line_number_count = 0;
      }
    }

    auto inner_callback = [&](jint component_index) -> jstring {
      switch (component_index) {
        case 0:
          return (name == nullptr) ? nullptr : env->NewStringUTF(name);
        case 1:
          return (sig == nullptr) ? nullptr : env->NewStringUTF(sig);
        case 2:
          return env->NewStringUTF(StringPrintf("%" PRId64, frames[method_index].location).c_str());
        case 3: {
          jint line_number = FindLineNumber(line_number_count,
                                            line_number_table,
                                            frames[method_index].location);
          return env->NewStringUTF(StringPrintf("%d", line_number).c_str());
        }
      }
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
    };
    jobjectArray inner_array = CreateObjectArray(env, 4, "java/lang/String", inner_callback);

    if (name != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(name));
    }
    if (sig != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
    }
    if (gen != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(gen));
    }
    if (line_number_table != nullptr) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(line_number_table));
    }

    return inner_array;
  };
  return CreateObjectArray(env, count, "[Ljava/lang/String;", callback);
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
