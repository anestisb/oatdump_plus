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

#include "android-base/macros.h"
#include "jni.h"
#include "jvmti.h"
#include "mirror/class-inl.h"
#include "scoped_local_ref.h"

// Test infrastructure
#include "test_env.h"

namespace art {
namespace Test944TransformClassloaders {

extern "C" JNIEXPORT jlong JNICALL Java_art_Test944_getDexFilePointer(JNIEnv* env, jclass, jclass klass) {
  if (Runtime::Current() == nullptr) {
    env->ThrowNew(env->FindClass("java/lang/Exception"),
                  "We do not seem to be running in ART! Unable to get dex file.");
    return 0;
  }
  ScopedObjectAccess soa(env);
  // This sequence of casts must be the same as those done in
  // runtime/native/dalvik_system_DexFile.cc in order to ensure that we get the same results.
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(
      &soa.Decode<mirror::Class>(klass)->GetDexFile()));
}

}  // namespace Test944TransformClassloaders
}  // namespace art
