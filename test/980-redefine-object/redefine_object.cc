/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <inttypes.h>
#include <iostream>

#include "android-base/stringprintf.h"
#include "base/logging.h"
#include "base/macros.h"
#include "jni.h"
#include "jvmti.h"
#include "ScopedUtfChars.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test980RedefineObjects {

extern "C" JNIEXPORT void JNICALL Java_Main_bindFunctionsForClass(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass target) {
  BindFunctionsOnClass(jvmti_env, env, target);
}

extern "C" JNIEXPORT void JNICALL Java_art_test_TestWatcher_NotifyConstructed(
    JNIEnv* env, jclass TestWatcherClass ATTRIBUTE_UNUSED, jobject constructed) {
  char* sig = nullptr;
  char* generic_sig = nullptr;
  if (JvmtiErrorToException(env, jvmti_env->GetClassSignature(env->GetObjectClass(constructed),
                                                              &sig,
                                                              &generic_sig))) {
    // Exception.
    return;
  }
  std::cout << "Object allocated of type '" << sig << "'" << std::endl;
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(generic_sig));
}

}  // namespace Test980RedefineObjects
}  // namespace art
