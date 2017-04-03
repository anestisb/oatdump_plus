/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "909-attach-agent/attach.h"

#include <jni.h>
#include <stdio.h>
#include <string.h>

#include "android-base/macros.h"

#include "jvmti.h"

namespace art {
namespace Test909AttachAgent {

jint OnAttach(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  fprintf(stderr, "Attached Agent for test 909-attach-agent\n");
  fsync(1);
  jvmtiEnv* env = nullptr;
  jvmtiEnv* env2 = nullptr;

#define CHECK_CALL_SUCCESS(c) \
  do { \
    if ((c) != JNI_OK) { \
      fprintf(stderr, "call " #c " did not succeed\n"); \
      return -1; \
    } \
  } while (false)

  CHECK_CALL_SUCCESS(vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION_1_0));
  CHECK_CALL_SUCCESS(vm->GetEnv(reinterpret_cast<void**>(&env2), JVMTI_VERSION_1_0));
  if (env == env2) {
    fprintf(stderr, "GetEnv returned same environment twice!\n");
    return -1;
  }
  unsigned char* local_data = nullptr;
  CHECK_CALL_SUCCESS(env->Allocate(8, &local_data));
  strcpy(reinterpret_cast<char*>(local_data), "hello!!");
  CHECK_CALL_SUCCESS(env->SetEnvironmentLocalStorage(local_data));
  unsigned char* get_data = nullptr;
  CHECK_CALL_SUCCESS(env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&get_data)));
  if (get_data != local_data) {
    fprintf(stderr, "Got different data from local storage then what was set!\n");
    return -1;
  }
  CHECK_CALL_SUCCESS(env2->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&get_data)));
  if (get_data != nullptr) {
    fprintf(stderr, "env2 did not have nullptr local storage.\n");
    return -1;
  }
  CHECK_CALL_SUCCESS(env->Deallocate(local_data));
  jint version = 0;
  CHECK_CALL_SUCCESS(env->GetVersionNumber(&version));
  if ((version & JVMTI_VERSION_1) != JVMTI_VERSION_1) {
    fprintf(stderr, "Unexpected version number!\n");
    return -1;
  }
  CHECK_CALL_SUCCESS(env->DisposeEnvironment());
  CHECK_CALL_SUCCESS(env2->DisposeEnvironment());
#undef CHECK_CALL_SUCCESS
  return JNI_OK;
}

}  // namespace Test909AttachAgent
}  // namespace art
