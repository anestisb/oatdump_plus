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

#include "ti-agent/common_helper.h"

#include <stdio.h>
#include <sstream>

#include "art_method.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "ti-agent/common_load.h"
#include "utils.h"

namespace art {
bool RuntimeIsJVM;

bool IsJVM() {
  return RuntimeIsJVM;
}

void SetAllCapabilities(jvmtiEnv* env) {
  jvmtiCapabilities caps;
  env->GetPotentialCapabilities(&caps);
  env->AddCapabilities(&caps);
}

namespace common_redefine {

static void throwRedefinitionError(jvmtiEnv* jvmti, JNIEnv* env, jclass target, jvmtiError res) {
  std::stringstream err;
  char* signature = nullptr;
  char* generic = nullptr;
  jvmti->GetClassSignature(target, &signature, &generic);
  char* error = nullptr;
  jvmti->GetErrorName(res, &error);
  err << "Failed to redefine class <" << signature << "> due to " << error;
  std::string message = err.str();
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(generic));
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(error));
  env->ThrowNew(env->FindClass("java/lang/Exception"), message.c_str());
}

using RedefineDirectFunction = jvmtiError (*)(jvmtiEnv*, jclass, jint, const unsigned char*);
static void DoClassTransformation(jvmtiEnv* jvmti_env,
                                  JNIEnv* env,
                                  jclass target,
                                  jbyteArray class_file_bytes,
                                  jbyteArray dex_file_bytes) {
  jbyteArray desired_array = IsJVM() ? class_file_bytes : dex_file_bytes;
  jint len = static_cast<jint>(env->GetArrayLength(desired_array));
  const unsigned char* redef_bytes = reinterpret_cast<const unsigned char*>(
      env->GetByteArrayElements(desired_array, nullptr));
  jvmtiError res;
  if (IsJVM()) {
    jvmtiClassDefinition def;
    def.klass = target;
    def.class_byte_count = static_cast<jint>(len);
    def.class_bytes = redef_bytes;
    res = jvmti_env->RedefineClasses(1, &def);
  } else {
    RedefineDirectFunction f =
        reinterpret_cast<RedefineDirectFunction>(jvmti_env->functions->reserved3);
    res = f(jvmti_env, target, len, redef_bytes);
  }
  if (res != JVMTI_ERROR_NONE) {
    throwRedefinitionError(jvmti_env, env, target, res);
  }
}

// Magic JNI export that classes can use for redefining classes.
// To use classes should declare this as a native function with signature (Ljava/lang/Class;[B[B)V
extern "C" JNIEXPORT void JNICALL Java_Main_doCommonClassRedefinition(JNIEnv* env,
                                                                      jclass,
                                                                      jclass target,
                                                                      jbyteArray class_file_bytes,
                                                                      jbyteArray dex_file_bytes) {
  DoClassTransformation(jvmti_env, env, target, class_file_bytes, dex_file_bytes);
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

}  // namespace common_redefine

}  // namespace art
