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

#include "java_lang_Void.h"

#include "nativehelper/jni_macros.h"

#include "class_linker-inl.h"
#include "jni_internal.h"
#include "native_util.h"
#include "runtime.h"
#include "scoped_fast_native_object_access-inl.h"

namespace art {

static jclass Void_lookupType(JNIEnv* env, jclass) {
  ScopedFastNativeObjectAccess soa(env);
  return soa.AddLocalReference<jclass>(
      Runtime::Current()->GetClassLinker()->GetClassRoot(ClassLinker::kPrimitiveVoid));
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(Void, lookupType, "()Ljava/lang/Class;"),
};

void register_java_lang_Void(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/Void");
}

}  // namespace art
