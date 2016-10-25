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

#include "tagging.h"

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "jni.h"
#include "ScopedLocalRef.h"
#include "ScopedPrimitiveArray.h"

#include "art_method-inl.h"
#include "base/logging.h"
#include "openjdkjvmti/jvmti.h"
#include "ti-agent/common_load.h"
#include "utils.h"

namespace art {
namespace Test903HelloTagging {

extern "C" JNIEXPORT void JNICALL Java_Main_setTag(JNIEnv* env ATTRIBUTE_UNUSED,
                                                   jclass,
                                                   jobject obj,
                                                   jlong tag) {
  jvmtiError ret = jvmti_env->SetTag(obj, tag);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error setting tag: %s\n", err);
  }
}

extern "C" JNIEXPORT jlong JNICALL Java_Main_getTag(JNIEnv* env ATTRIBUTE_UNUSED,
                                                    jclass,
                                                    jobject obj) {
  jlong tag = 0;
  jvmtiError ret = jvmti_env->GetTag(obj, &tag);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error getting tag: %s\n", err);
  }
  return tag;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getTaggedObjects(JNIEnv* env,
                                                                     jclass,
                                                                     jlongArray searchTags,
                                                                     jboolean returnObjects,
                                                                     jboolean returnTags) {
  ScopedLongArrayRO scoped_array(env);
  if (searchTags != nullptr) {
    scoped_array.reset(searchTags);
  }
  const jlong* tag_ptr = scoped_array.get();
  if (tag_ptr == nullptr) {
    // Can never pass null.
    tag_ptr = reinterpret_cast<const jlong*>(1);
  }

  jint result_count;
  jobject* result_object_array;
  jobject** result_object_array_ptr = returnObjects == JNI_TRUE ? &result_object_array : nullptr;
  jlong* result_tag_array;
  jlong** result_tag_array_ptr = returnTags == JNI_TRUE ? &result_tag_array : nullptr;

  jvmtiError ret = jvmti_env->GetObjectsWithTags(scoped_array.size(),
                                                 tag_ptr,
                                                 &result_count,
                                                 result_object_array_ptr,
                                                 result_tag_array_ptr);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Failure running GetLoadedClasses: %s\n", err);
    return nullptr;
  }

  CHECK_GE(result_count, 0);

  ScopedLocalRef<jclass> obj_class(env, env->FindClass("java/lang/Object"));
  if (obj_class.get() == nullptr) {
    return nullptr;
  }

  jobjectArray resultObjectArray = nullptr;
  if (returnObjects == JNI_TRUE) {
    resultObjectArray = env->NewObjectArray(result_count, obj_class.get(), nullptr);
    if (resultObjectArray == nullptr) {
      return nullptr;
    }
    for (jint i = 0; i < result_count; ++i) {
      env->SetObjectArrayElement(resultObjectArray, i, result_object_array[i]);
    }
  }

  jlongArray resultTagArray = nullptr;
  if (returnTags == JNI_TRUE) {
    resultTagArray = env->NewLongArray(result_count);
    env->SetLongArrayRegion(resultTagArray, 0, result_count, result_tag_array);
  }

  jobject count_integer;
  {
    ScopedLocalRef<jclass> integer_class(env, env->FindClass("java/lang/Integer"));
    jmethodID methodID = env->GetMethodID(integer_class.get(), "<init>", "(I)V");
    count_integer = env->NewObject(integer_class.get(), methodID, result_count);
    if (count_integer == nullptr) {
      return nullptr;
    }
  }

  jobjectArray resultArray = env->NewObjectArray(3, obj_class.get(), nullptr);
  if (resultArray == nullptr) {
    return nullptr;
  }
  env->SetObjectArrayElement(resultArray, 0, resultObjectArray);
  env->SetObjectArrayElement(resultArray, 1, resultTagArray);
  env->SetObjectArrayElement(resultArray, 2, count_integer);

  return resultArray;
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

}  // namespace Test903HelloTagging
}  // namespace art

