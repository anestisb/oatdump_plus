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

#define LOG_TAG "NativeMethods"

#define CLASS_NAME "benchmarks/MicroNative/java/NativeMethods"

#include "JNIHelp.h"
#include "JniConstants.h"

static void NativeMethods_emptyJniStaticSynchronizedMethod0(JNIEnv*, jclass) { }
static void NativeMethods_emptyJniSynchronizedMethod0(JNIEnv*, jclass) { }

static JNINativeMethod gMethods_NormalOnly[] = {
  NATIVE_METHOD(NativeMethods, emptyJniStaticSynchronizedMethod0, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniSynchronizedMethod0, "()V"),
};

static void NativeMethods_emptyJniMethod0(JNIEnv*, jobject) { }
static void NativeMethods_emptyJniMethod6(JNIEnv*, jobject, int, int, int, int, int, int) { }
static void NativeMethods_emptyJniMethod6L(JNIEnv*, jobject, jobject, jarray, jarray, jobject,
                                           jarray, jarray) { }
static void NativeMethods_emptyJniStaticMethod6L(JNIEnv*, jclass, jobject, jarray, jarray, jobject,
                                                 jarray, jarray) { }

static void NativeMethods_emptyJniStaticMethod0(JNIEnv*, jclass) { }
static void NativeMethods_emptyJniStaticMethod6(JNIEnv*, jclass, int, int, int, int, int, int) { }

static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(NativeMethods, emptyJniMethod0, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniMethod6, "(IIIIII)V"),
  NATIVE_METHOD(NativeMethods, emptyJniMethod6L, "(Ljava/lang/String;[Ljava/lang/String;[[ILjava/lang/Object;[Ljava/lang/Object;[[[[Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod6L, "(Ljava/lang/String;[Ljava/lang/String;[[ILjava/lang/Object;[Ljava/lang/Object;[[[[Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod0, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod6, "(IIIIII)V"),
};

static void NativeMethods_emptyJniMethod0_Fast(JNIEnv*, jobject) { }
static void NativeMethods_emptyJniMethod6_Fast(JNIEnv*, jobject, int, int, int, int, int, int) { }
static void NativeMethods_emptyJniMethod6L_Fast(JNIEnv*, jobject, jobject, jarray, jarray, jobject,
                                                jarray, jarray) { }
static void NativeMethods_emptyJniStaticMethod6L_Fast(JNIEnv*, jclass, jobject, jarray, jarray,
                                                      jobject, jarray, jarray) { }

static void NativeMethods_emptyJniStaticMethod0_Fast(JNIEnv*, jclass) { }
static void NativeMethods_emptyJniStaticMethod6_Fast(JNIEnv*, jclass, int, int, int, int, int, int) { }

static JNINativeMethod gMethods_Fast[] = {
  NATIVE_METHOD(NativeMethods, emptyJniMethod0_Fast, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniMethod6_Fast, "(IIIIII)V"),
  NATIVE_METHOD(NativeMethods, emptyJniMethod6L_Fast, "(Ljava/lang/String;[Ljava/lang/String;[[ILjava/lang/Object;[Ljava/lang/Object;[[[[Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod6L_Fast, "(Ljava/lang/String;[Ljava/lang/String;[[ILjava/lang/Object;[Ljava/lang/Object;[[[[Ljava/lang/Object;)V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod0_Fast, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod6_Fast, "(IIIIII)V"),
};

static void NativeMethods_emptyJniStaticMethod0_Critical() { }
static void NativeMethods_emptyJniStaticMethod6_Critical(int, int, int, int, int, int) { }

static JNINativeMethod gMethods_Critical[] = {
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod0_Critical, "()V"),
  NATIVE_METHOD(NativeMethods, emptyJniStaticMethod6_Critical, "(IIIIII)V"),
};

void register_micro_native_methods(JNIEnv* env) {
  jniRegisterNativeMethods(env, CLASS_NAME, gMethods_NormalOnly, NELEM(gMethods_NormalOnly));
  jniRegisterNativeMethods(env, CLASS_NAME, gMethods, NELEM(gMethods));
  jniRegisterNativeMethods(env, CLASS_NAME, gMethods_Fast, NELEM(gMethods_Fast));
  jniRegisterNativeMethods(env, CLASS_NAME, gMethods_Critical, NELEM(gMethods_Critical));
}
