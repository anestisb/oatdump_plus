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

#include "common_helper.h"

#include <dlfcn.h>
#include <map>
#include <stdio.h>
#include <sstream>
#include <deque>
#include <vector>

#include "android-base/stringprintf.h"
#include "jni.h"
#include "jvmti.h"

#include "jni_binder.h"
#include "jvmti_helper.h"
#include "scoped_local_ref.h"
#include "test_env.h"

namespace art {

static void SetupCommonRetransform();
static void SetupCommonRedefine();
static void SetupCommonTransform();

template <bool is_redefine>
static void throwCommonRedefinitionError(jvmtiEnv* jvmti,
                                         JNIEnv* env,
                                         jint num_targets,
                                         jclass* target,
                                         jvmtiError res) {
  std::stringstream err;
  char* error = nullptr;
  jvmti->GetErrorName(res, &error);
  err << "Failed to " << (is_redefine ? "redefine" : "retransform") << " class";
  if (num_targets > 1) {
    err << "es";
  }
  err << " <";
  for (jint i = 0; i < num_targets; i++) {
    char* signature = nullptr;
    char* generic = nullptr;
    jvmti->GetClassSignature(target[i], &signature, &generic);
    if (i != 0) {
      err << ", ";
    }
    err << signature;
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(signature));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(generic));
  }
  err << "> due to " << error;
  std::string message = err.str();
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(error));
  env->ThrowNew(env->FindClass("java/lang/Exception"), message.c_str());
}

namespace common_trace {

// Taken from art/runtime/modifiers.h
static constexpr uint32_t kAccStatic =       0x0008;  // field, method, ic

struct TraceData {
  jclass test_klass;
  jmethodID enter_method;
  jmethodID exit_method;
  jmethodID field_access;
  jmethodID field_modify;
  bool in_callback;
  bool access_watch_on_load;
  bool modify_watch_on_load;
};

static jobject GetJavaField(jvmtiEnv* jvmti, JNIEnv* env, jclass field_klass, jfieldID f) {
  jint mods = 0;
  if (JvmtiErrorToException(env, jvmti, jvmti->GetFieldModifiers(field_klass, f, &mods))) {
    return nullptr;
  }

  bool is_static = (mods & kAccStatic) != 0;
  return env->ToReflectedField(field_klass, f, is_static);
}

static jobject GetJavaMethod(jvmtiEnv* jvmti, JNIEnv* env, jmethodID m) {
  jint mods = 0;
  if (JvmtiErrorToException(env, jvmti, jvmti->GetMethodModifiers(m, &mods))) {
    return nullptr;
  }

  bool is_static = (mods & kAccStatic) != 0;
  jclass method_klass = nullptr;
  if (JvmtiErrorToException(env, jvmti, jvmti->GetMethodDeclaringClass(m, &method_klass))) {
    return nullptr;
  }
  jobject res = env->ToReflectedMethod(method_klass, m, is_static);
  env->DeleteLocalRef(method_klass);
  return res;
}

static jobject GetJavaValueByType(JNIEnv* env, char type, jvalue value) {
  std::string name;
  switch (type) {
    case 'V':
      return nullptr;
    case '[':
    case 'L':
      return value.l;
    case 'Z':
      name = "java/lang/Boolean";
      break;
    case 'B':
      name = "java/lang/Byte";
      break;
    case 'C':
      name = "java/lang/Character";
      break;
    case 'S':
      name = "java/lang/Short";
      break;
    case 'I':
      name = "java/lang/Integer";
      break;
    case 'J':
      name = "java/lang/Long";
      break;
    case 'F':
      name = "java/lang/Float";
      break;
    case 'D':
      name = "java/lang/Double";
      break;
    default:
      LOG(FATAL) << "Unable to figure out type!";
      return nullptr;
  }
  std::ostringstream oss;
  oss << "(" << type << ")L" << name << ";";
  std::string args = oss.str();
  jclass target = env->FindClass(name.c_str());
  jmethodID valueOfMethod = env->GetStaticMethodID(target, "valueOf", args.c_str());

  CHECK(valueOfMethod != nullptr) << args;
  jobject res = env->CallStaticObjectMethodA(target, valueOfMethod, &value);
  env->DeleteLocalRef(target);
  return res;
}

static jobject GetJavaValue(jvmtiEnv* jvmtienv,
                            JNIEnv* env,
                            jmethodID m,
                            jvalue value) {
  char *fname, *fsig, *fgen;
  if (JvmtiErrorToException(env, jvmtienv, jvmtienv->GetMethodName(m, &fname, &fsig, &fgen))) {
    return nullptr;
  }
  std::string type(fsig);
  type = type.substr(type.find(")") + 1);
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fsig));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fname));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fgen));
  return GetJavaValueByType(env, type[0], value);
}

static void fieldAccessCB(jvmtiEnv* jvmti,
                          JNIEnv* jnienv,
                          jthread thr ATTRIBUTE_UNUSED,
                          jmethodID method,
                          jlocation location,
                          jclass field_klass,
                          jobject object,
                          jfieldID field) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(jnienv, jvmti,
                            jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  if (data->in_callback) {
    // Don't do callback for either of these to prevent an infinite loop.
    return;
  }
  CHECK(data->field_access != nullptr);
  data->in_callback = true;
  jobject method_arg = GetJavaMethod(jvmti, jnienv, method);
  jobject field_arg = GetJavaField(jvmti, jnienv, field_klass, field);
  jnienv->CallStaticVoidMethod(data->test_klass,
                               data->field_access,
                               method_arg,
                               static_cast<jlong>(location),
                               field_klass,
                               object,
                               field_arg);
  jnienv->DeleteLocalRef(method_arg);
  jnienv->DeleteLocalRef(field_arg);
  data->in_callback = false;
}

static void fieldModificationCB(jvmtiEnv* jvmti,
                                JNIEnv* jnienv,
                                jthread thr ATTRIBUTE_UNUSED,
                                jmethodID method,
                                jlocation location,
                                jclass field_klass,
                                jobject object,
                                jfieldID field,
                                char type_char,
                                jvalue new_value) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(jnienv, jvmti,
                            jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  if (data->in_callback) {
    // Don't do callback recursively to prevent an infinite loop.
    return;
  }
  CHECK(data->field_modify != nullptr);
  data->in_callback = true;
  jobject method_arg = GetJavaMethod(jvmti, jnienv, method);
  jobject field_arg = GetJavaField(jvmti, jnienv, field_klass, field);
  jobject value = GetJavaValueByType(jnienv, type_char, new_value);
  if (jnienv->ExceptionCheck()) {
    data->in_callback = false;
    jnienv->DeleteLocalRef(method_arg);
    jnienv->DeleteLocalRef(field_arg);
    return;
  }
  jnienv->CallStaticVoidMethod(data->test_klass,
                               data->field_modify,
                               method_arg,
                               static_cast<jlong>(location),
                               field_klass,
                               object,
                               field_arg,
                               value);
  jnienv->DeleteLocalRef(method_arg);
  jnienv->DeleteLocalRef(field_arg);
  data->in_callback = false;
}

static void methodExitCB(jvmtiEnv* jvmti,
                         JNIEnv* jnienv,
                         jthread thr ATTRIBUTE_UNUSED,
                         jmethodID method,
                         jboolean was_popped_by_exception,
                         jvalue return_value) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(jnienv, jvmti,
                            jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  if (method == data->exit_method || method == data->enter_method || data->in_callback) {
    // Don't do callback for either of these to prevent an infinite loop.
    return;
  }
  CHECK(data->exit_method != nullptr);
  data->in_callback = true;
  jobject method_arg = GetJavaMethod(jvmti, jnienv, method);
  jobject result =
      was_popped_by_exception ? nullptr : GetJavaValue(jvmti, jnienv, method, return_value);
  if (jnienv->ExceptionCheck()) {
    data->in_callback = false;
    return;
  }
  jnienv->CallStaticVoidMethod(data->test_klass,
                               data->exit_method,
                               method_arg,
                               was_popped_by_exception,
                               result);
  jnienv->DeleteLocalRef(method_arg);
  data->in_callback = false;
}

static void methodEntryCB(jvmtiEnv* jvmti,
                          JNIEnv* jnienv,
                          jthread thr ATTRIBUTE_UNUSED,
                          jmethodID method) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(jnienv, jvmti,
                            jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  CHECK(data->enter_method != nullptr);
  if (method == data->exit_method || method == data->enter_method || data->in_callback) {
    // Don't do callback for either of these to prevent an infinite loop.
    return;
  }
  data->in_callback = true;
  jobject method_arg = GetJavaMethod(jvmti, jnienv, method);
  if (jnienv->ExceptionCheck()) {
    return;
  }
  jnienv->CallStaticVoidMethod(data->test_klass, data->enter_method, method_arg);
  jnienv->DeleteLocalRef(method_arg);
  data->in_callback = false;
}

static void classPrepareCB(jvmtiEnv* jvmti,
                           JNIEnv* jnienv,
                           jthread thr ATTRIBUTE_UNUSED,
                           jclass klass) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(jnienv, jvmti,
                            jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  if (data->access_watch_on_load || data->modify_watch_on_load) {
    jint nfields;
    jfieldID* fields;
    if (JvmtiErrorToException(jnienv, jvmti, jvmti->GetClassFields(klass, &nfields, &fields))) {
      return;
    }
    for (jint i = 0; i < nfields; i++) {
      jfieldID f = fields[i];
      // Ignore errors
      if (data->access_watch_on_load) {
        jvmti->SetFieldAccessWatch(klass, f);
      }

      if (data->modify_watch_on_load) {
        jvmti->SetFieldModificationWatch(klass, f);
      }
    }
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
  }
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_watchAllFieldAccesses(JNIEnv* env) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(
      env, jvmti_env, jvmti_env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  data->access_watch_on_load = true;
  // We need the classPrepareCB to watch new fields as the classes are loaded/prepared.
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_CLASS_PREPARE,
                                                                nullptr))) {
    return;
  }
  jint nklasses;
  jclass* klasses;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetLoadedClasses(&nklasses, &klasses))) {
    return;
  }
  for (jint i = 0; i < nklasses; i++) {
    jclass k = klasses[i];

    jint nfields;
    jfieldID* fields;
    jvmtiError err = jvmti_env->GetClassFields(k, &nfields, &fields);
    if (err == JVMTI_ERROR_CLASS_NOT_PREPARED) {
      continue;
    } else if (JvmtiErrorToException(env, jvmti_env, err)) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(klasses));
      return;
    }
    for (jint j = 0; j < nfields; j++) {
      jvmti_env->SetFieldAccessWatch(k, fields[j]);
    }
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(fields));
  }
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(klasses));
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_watchAllFieldModifications(JNIEnv* env) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(
      env, jvmti_env, jvmti_env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)))) {
    return;
  }
  data->modify_watch_on_load = true;
  // We need the classPrepareCB to watch new fields as the classes are loaded/prepared.
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_CLASS_PREPARE,
                                                                nullptr))) {
    return;
  }
  jint nklasses;
  jclass* klasses;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->GetLoadedClasses(&nklasses, &klasses))) {
    return;
  }
  for (jint i = 0; i < nklasses; i++) {
    jclass k = klasses[i];

    jint nfields;
    jfieldID* fields;
    jvmtiError err = jvmti_env->GetClassFields(k, &nfields, &fields);
    if (err == JVMTI_ERROR_CLASS_NOT_PREPARED) {
      continue;
    } else if (JvmtiErrorToException(env, jvmti_env, err)) {
      jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(klasses));
      return;
    }
    for (jint j = 0; j < nfields; j++) {
      jvmti_env->SetFieldModificationWatch(k, fields[j]);
    }
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(fields));
  }
  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(klasses));
}

static bool GetFieldAndClass(JNIEnv* env,
                             jobject ref_field,
                             jclass* out_klass,
                             jfieldID* out_field) {
  *out_field = env->FromReflectedField(ref_field);
  if (env->ExceptionCheck()) {
    return false;
  }
  jclass field_klass = env->FindClass("java/lang/reflect/Field");
  if (env->ExceptionCheck()) {
    return false;
  }
  jmethodID get_declaring_class_method =
      env->GetMethodID(field_klass, "getDeclaringClass", "()Ljava/lang/Class;");
  if (env->ExceptionCheck()) {
    env->DeleteLocalRef(field_klass);
    return false;
  }
  *out_klass = static_cast<jclass>(env->CallObjectMethod(ref_field, get_declaring_class_method));
  if (env->ExceptionCheck()) {
    *out_klass = nullptr;
    env->DeleteLocalRef(field_klass);
    return false;
  }
  env->DeleteLocalRef(field_klass);
  return true;
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_watchFieldModification(
    JNIEnv* env,
    jclass trace ATTRIBUTE_UNUSED,
    jobject field_obj) {
  jfieldID field;
  jclass klass;
  if (!GetFieldAndClass(env, field_obj, &klass, &field)) {
    return;
  }

  JvmtiErrorToException(env, jvmti_env, jvmti_env->SetFieldModificationWatch(klass, field));
  env->DeleteLocalRef(klass);
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_watchFieldAccess(
    JNIEnv* env,
    jclass trace ATTRIBUTE_UNUSED,
    jobject field_obj) {
  jfieldID field;
  jclass klass;
  if (!GetFieldAndClass(env, field_obj, &klass, &field)) {
    return;
  }
  JvmtiErrorToException(env, jvmti_env, jvmti_env->SetFieldAccessWatch(klass, field));
  env->DeleteLocalRef(klass);
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_enableTracing(
    JNIEnv* env,
    jclass trace ATTRIBUTE_UNUSED,
    jclass klass,
    jobject enter,
    jobject exit,
    jobject field_access,
    jobject field_modify,
    jthread thr) {
  TraceData* data = nullptr;
  if (JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->Allocate(sizeof(TraceData),
                                                reinterpret_cast<unsigned char**>(&data)))) {
    return;
  }
  memset(data, 0, sizeof(TraceData));
  data->test_klass = reinterpret_cast<jclass>(env->NewGlobalRef(klass));
  data->enter_method = enter != nullptr ? env->FromReflectedMethod(enter) : nullptr;
  data->exit_method = exit != nullptr ? env->FromReflectedMethod(exit) : nullptr;
  data->field_access = field_access != nullptr ? env->FromReflectedMethod(field_access) : nullptr;
  data->field_modify = field_modify != nullptr ? env->FromReflectedMethod(field_modify) : nullptr;
  data->in_callback = false;

  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->SetEnvironmentLocalStorage(data))) {
    return;
  }

  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.MethodEntry = methodEntryCB;
  cb.MethodExit = methodExitCB;
  cb.FieldAccess = fieldAccessCB;
  cb.FieldModification = fieldModificationCB;
  cb.ClassPrepare = classPrepareCB;
  if (JvmtiErrorToException(env, jvmti_env, jvmti_env->SetEventCallbacks(&cb, sizeof(cb)))) {
    return;
  }
  if (enter != nullptr &&
      JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_METHOD_ENTRY,
                                                                thr))) {
    return;
  }
  if (exit != nullptr &&
      JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_METHOD_EXIT,
                                                                thr))) {
    return;
  }
  if (field_access != nullptr &&
      JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_FIELD_ACCESS,
                                                                thr))) {
    return;
  }
  if (field_modify != nullptr &&
      JvmtiErrorToException(env,
                            jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                JVMTI_EVENT_FIELD_MODIFICATION,
                                                                thr))) {
    return;
  }
}

extern "C" JNIEXPORT void JNICALL Java_art_Trace_disableTracing(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jthread thr) {
  if (JvmtiErrorToException(env, jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                                                JVMTI_EVENT_FIELD_ACCESS,
                                                                thr))) {
    return;
  }
  if (JvmtiErrorToException(env, jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                                                JVMTI_EVENT_FIELD_MODIFICATION,
                                                                thr))) {
    return;
  }
  if (JvmtiErrorToException(env, jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                                                JVMTI_EVENT_METHOD_ENTRY,
                                                                thr))) {
    return;
  }
  if (JvmtiErrorToException(env, jvmti_env,
                            jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                                                JVMTI_EVENT_METHOD_EXIT,
                                                                thr))) {
    return;
  }
}

}  // namespace common_trace

namespace common_redefine {

static void throwRedefinitionError(jvmtiEnv* jvmti,
                                   JNIEnv* env,
                                   jint num_targets,
                                   jclass* target,
                                   jvmtiError res) {
  return throwCommonRedefinitionError<true>(jvmti, env, num_targets, target, res);
}

static void DoMultiClassRedefine(jvmtiEnv* jvmti_env,
                                 JNIEnv* env,
                                 jint num_redefines,
                                 jclass* targets,
                                 jbyteArray* class_file_bytes,
                                 jbyteArray* dex_file_bytes) {
  std::vector<jvmtiClassDefinition> defs;
  for (jint i = 0; i < num_redefines; i++) {
    jbyteArray desired_array = IsJVM() ? class_file_bytes[i] : dex_file_bytes[i];
    jint len = static_cast<jint>(env->GetArrayLength(desired_array));
    const unsigned char* redef_bytes = reinterpret_cast<const unsigned char*>(
        env->GetByteArrayElements(desired_array, nullptr));
    defs.push_back({targets[i], static_cast<jint>(len), redef_bytes});
  }
  jvmtiError res = jvmti_env->RedefineClasses(num_redefines, defs.data());
  if (res != JVMTI_ERROR_NONE) {
    throwRedefinitionError(jvmti_env, env, num_redefines, targets, res);
  }
}

static void DoClassRedefine(jvmtiEnv* jvmti_env,
                            JNIEnv* env,
                            jclass target,
                            jbyteArray class_file_bytes,
                            jbyteArray dex_file_bytes) {
  return DoMultiClassRedefine(jvmti_env, env, 1, &target, &class_file_bytes, &dex_file_bytes);
}

// Magic JNI export that classes can use for redefining classes.
// To use classes should declare this as a native function with signature (Ljava/lang/Class;[B[B)V
extern "C" JNIEXPORT void JNICALL Java_art_Redefinition_doCommonClassRedefinition(
    JNIEnv* env, jclass, jclass target, jbyteArray class_file_bytes, jbyteArray dex_file_bytes) {
  DoClassRedefine(jvmti_env, env, target, class_file_bytes, dex_file_bytes);
}

// Magic JNI export that classes can use for redefining classes.
// To use classes should declare this as a native function with signature
// ([Ljava/lang/Class;[[B[[B)V
extern "C" JNIEXPORT void JNICALL Java_art_Redefinition_doCommonMultiClassRedefinition(
    JNIEnv* env,
    jclass,
    jobjectArray targets,
    jobjectArray class_file_bytes,
    jobjectArray dex_file_bytes) {
  std::vector<jclass> classes;
  std::vector<jbyteArray> class_files;
  std::vector<jbyteArray> dex_files;
  jint len = env->GetArrayLength(targets);
  if (len != env->GetArrayLength(class_file_bytes) || len != env->GetArrayLength(dex_file_bytes)) {
    env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
                  "the three array arguments passed to this function have different lengths!");
    return;
  }
  for (jint i = 0; i < len; i++) {
    classes.push_back(static_cast<jclass>(env->GetObjectArrayElement(targets, i)));
    dex_files.push_back(static_cast<jbyteArray>(env->GetObjectArrayElement(dex_file_bytes, i)));
    class_files.push_back(static_cast<jbyteArray>(env->GetObjectArrayElement(class_file_bytes, i)));
  }
  return DoMultiClassRedefine(jvmti_env,
                              env,
                              len,
                              classes.data(),
                              class_files.data(),
                              dex_files.data());
}

// Get all capabilities except those related to retransformation.
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetupCommonRedefine();
  return 0;
}

}  // namespace common_redefine

namespace common_retransform {

struct CommonTransformationResult {
  std::vector<unsigned char> class_bytes;
  std::vector<unsigned char> dex_bytes;

  CommonTransformationResult(size_t class_size, size_t dex_size)
      : class_bytes(class_size), dex_bytes(dex_size) {}

  CommonTransformationResult() = default;
  CommonTransformationResult(CommonTransformationResult&&) = default;
  CommonTransformationResult(CommonTransformationResult&) = default;
};

// Map from class name to transformation result.
std::map<std::string, std::deque<CommonTransformationResult>> gTransformations;
bool gPopTransformations = true;

extern "C" JNIEXPORT void JNICALL Java_art_Redefinition_addCommonTransformationResult(
    JNIEnv* env, jclass, jstring class_name, jbyteArray class_array, jbyteArray dex_array) {
  const char* name_chrs = env->GetStringUTFChars(class_name, nullptr);
  std::string name_str(name_chrs);
  env->ReleaseStringUTFChars(class_name, name_chrs);
  CommonTransformationResult trans(env->GetArrayLength(class_array),
                                   env->GetArrayLength(dex_array));
  if (env->ExceptionOccurred()) {
    return;
  }
  env->GetByteArrayRegion(class_array,
                          0,
                          env->GetArrayLength(class_array),
                          reinterpret_cast<jbyte*>(trans.class_bytes.data()));
  if (env->ExceptionOccurred()) {
    return;
  }
  env->GetByteArrayRegion(dex_array,
                          0,
                          env->GetArrayLength(dex_array),
                          reinterpret_cast<jbyte*>(trans.dex_bytes.data()));
  if (env->ExceptionOccurred()) {
    return;
  }
  if (gTransformations.find(name_str) == gTransformations.end()) {
    std::deque<CommonTransformationResult> list;
    gTransformations[name_str] = std::move(list);
  }
  gTransformations[name_str].push_back(std::move(trans));
}

// The hook we are using.
void JNICALL CommonClassFileLoadHookRetransformable(jvmtiEnv* jvmti_env,
                                                    JNIEnv* jni_env ATTRIBUTE_UNUSED,
                                                    jclass class_being_redefined ATTRIBUTE_UNUSED,
                                                    jobject loader ATTRIBUTE_UNUSED,
                                                    const char* name,
                                                    jobject protection_domain ATTRIBUTE_UNUSED,
                                                    jint class_data_len ATTRIBUTE_UNUSED,
                                                    const unsigned char* class_dat ATTRIBUTE_UNUSED,
                                                    jint* new_class_data_len,
                                                    unsigned char** new_class_data) {
  std::string name_str(name);
  if (gTransformations.find(name_str) != gTransformations.end() &&
      gTransformations[name_str].size() > 0) {
    CommonTransformationResult& res = gTransformations[name_str][0];
    const std::vector<unsigned char>& desired_array = IsJVM() ? res.class_bytes : res.dex_bytes;
    unsigned char* new_data;
    CHECK_EQ(JVMTI_ERROR_NONE, jvmti_env->Allocate(desired_array.size(), &new_data));
    memcpy(new_data, desired_array.data(), desired_array.size());
    *new_class_data = new_data;
    *new_class_data_len = desired_array.size();
    if (gPopTransformations) {
      gTransformations[name_str].pop_front();
    }
  }
}

extern "C" JNIEXPORT void Java_art_Redefinition_setPopRetransformations(JNIEnv*,
                                                                        jclass,
                                                                        jboolean enable) {
  gPopTransformations = enable;
}

extern "C" JNIEXPORT void Java_art_Redefinition_popTransformationFor(JNIEnv* env,
                                                                         jclass,
                                                                         jstring class_name) {
  const char* name_chrs = env->GetStringUTFChars(class_name, nullptr);
  std::string name_str(name_chrs);
  env->ReleaseStringUTFChars(class_name, name_chrs);
  if (gTransformations.find(name_str) != gTransformations.end() &&
      gTransformations[name_str].size() > 0) {
    gTransformations[name_str].pop_front();
  } else {
    std::stringstream err;
    err << "No transformations found for class " << name_str;
    std::string message = err.str();
    env->ThrowNew(env->FindClass("java/lang/Exception"), message.c_str());
  }
}

extern "C" JNIEXPORT void Java_art_Redefinition_enableCommonRetransformation(JNIEnv* env,
                                                                                 jclass,
                                                                                 jboolean enable) {
  jvmtiError res = jvmti_env->SetEventNotificationMode(enable ? JVMTI_ENABLE : JVMTI_DISABLE,
                                                       JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                                                       nullptr);
  if (res != JVMTI_ERROR_NONE) {
    JvmtiErrorToException(env, jvmti_env, res);
  }
}

static void throwRetransformationError(jvmtiEnv* jvmti,
                                       JNIEnv* env,
                                       jint num_targets,
                                       jclass* targets,
                                       jvmtiError res) {
  return throwCommonRedefinitionError<false>(jvmti, env, num_targets, targets, res);
}

static void DoClassRetransformation(jvmtiEnv* jvmti_env, JNIEnv* env, jobjectArray targets) {
  std::vector<jclass> classes;
  jint len = env->GetArrayLength(targets);
  for (jint i = 0; i < len; i++) {
    classes.push_back(static_cast<jclass>(env->GetObjectArrayElement(targets, i)));
  }
  jvmtiError res = jvmti_env->RetransformClasses(len, classes.data());
  if (res != JVMTI_ERROR_NONE) {
    throwRetransformationError(jvmti_env, env, len, classes.data(), res);
  }
}

extern "C" JNIEXPORT void JNICALL Java_art_Redefinition_doCommonClassRetransformation(
    JNIEnv* env, jclass, jobjectArray targets) {
  jvmtiCapabilities caps;
  jvmtiError caps_err = jvmti_env->GetCapabilities(&caps);
  if (caps_err != JVMTI_ERROR_NONE) {
    env->ThrowNew(env->FindClass("java/lang/Exception"),
                  "Unable to get current jvmtiEnv capabilities");
    return;
  }

  // Allocate a new environment if we don't have the can_retransform_classes capability needed to
  // call the RetransformClasses function.
  jvmtiEnv* real_env = nullptr;
  if (caps.can_retransform_classes != 1) {
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != 0 ||
        vm->GetEnv(reinterpret_cast<void**>(&real_env), JVMTI_VERSION_1_0) != 0) {
      env->ThrowNew(env->FindClass("java/lang/Exception"),
                    "Unable to create temporary jvmtiEnv for RetransformClasses call.");
      return;
    }
    SetAllCapabilities(real_env);
  } else {
    real_env = jvmti_env;
  }
  DoClassRetransformation(real_env, env, targets);
  if (caps.can_retransform_classes != 1) {
    real_env->DisposeEnvironment();
  }
}

// Get all capabilities except those related to retransformation.
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetupCommonRetransform();
  return 0;
}

}  // namespace common_retransform

namespace common_transform {

// Get all capabilities except those related to retransformation.
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetupCommonTransform();
  return 0;
}

}  // namespace common_transform

#define CONFIGURATION_COMMON_REDEFINE 0
#define CONFIGURATION_COMMON_RETRANSFORM 1
#define CONFIGURATION_COMMON_TRANSFORM 2

static void SetupCommonRedefine() {
  jvmtiCapabilities caps;
  jvmti_env->GetPotentialCapabilities(&caps);
  caps.can_retransform_classes = 0;
  caps.can_retransform_any_class = 0;
  jvmti_env->AddCapabilities(&caps);
}

static void SetupCommonRetransform() {
  SetAllCapabilities(jvmti_env);
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = common_retransform::CommonClassFileLoadHookRetransformable;
  jvmtiError res = jvmti_env->SetEventCallbacks(&cb, sizeof(cb));
  CHECK_EQ(res, JVMTI_ERROR_NONE);
  common_retransform::gTransformations.clear();
}

static void SetupCommonTransform() {
  // Don't set the retransform caps
  jvmtiCapabilities caps;
  jvmti_env->GetPotentialCapabilities(&caps);
  caps.can_retransform_classes = 0;
  caps.can_retransform_any_class = 0;
  jvmti_env->AddCapabilities(&caps);

  // Use the same callback as the retransform test.
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = common_retransform::CommonClassFileLoadHookRetransformable;
  jvmtiError res = jvmti_env->SetEventCallbacks(&cb, sizeof(cb));
  CHECK_EQ(res, JVMTI_ERROR_NONE);
  common_retransform::gTransformations.clear();
}

extern "C" JNIEXPORT void JNICALL Java_art_Redefinition_nativeSetTestConfiguration(JNIEnv*,
                                                                                   jclass,
                                                                                   jint type) {
  switch (type) {
    case CONFIGURATION_COMMON_REDEFINE: {
      SetupCommonRedefine();
      return;
    }
    case CONFIGURATION_COMMON_RETRANSFORM: {
      SetupCommonRetransform();
      return;
    }
    case CONFIGURATION_COMMON_TRANSFORM: {
      SetupCommonTransform();
      return;
    }
    default: {
      LOG(FATAL) << "Unknown test configuration: " << type;
    }
  }
}
}  // namespace art
