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

#include <dlfcn.h>
#include <stdio.h>
#include <sstream>
#include <deque>

#include "android-base/stringprintf.h"
#include "art_method.h"
#include "jni.h"
#include "jni_internal.h"
#include "jvmti.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"
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

bool JvmtiErrorToException(JNIEnv* env, jvmtiError error) {
  if (error == JVMTI_ERROR_NONE) {
    return false;
  }

  ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
  if (rt_exception.get() == nullptr) {
    // CNFE should be pending.
    return true;
  }

  char* err;
  jvmti_env->GetErrorName(error, &err);

  env->ThrowNew(rt_exception.get(), err);

  jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
  return true;
}


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
extern "C" JNIEXPORT void JNICALL Java_Main_doCommonClassRedefinition(JNIEnv* env,
                                                                      jclass,
                                                                      jclass target,
                                                                      jbyteArray class_file_bytes,
                                                                      jbyteArray dex_file_bytes) {
  DoClassRedefine(jvmti_env, env, target, class_file_bytes, dex_file_bytes);
}

// Magic JNI export that classes can use for redefining classes.
// To use classes should declare this as a native function with signature
// ([Ljava/lang/Class;[[B[[B)V
extern "C" JNIEXPORT void JNICALL Java_Main_doCommonMultiClassRedefinition(
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
  jvmtiCapabilities caps;
  jvmti_env->GetPotentialCapabilities(&caps);
  caps.can_retransform_classes = 0;
  caps.can_retransform_any_class = 0;
  jvmti_env->AddCapabilities(&caps);
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

extern "C" JNIEXPORT void JNICALL Java_Main_addCommonTransformationResult(JNIEnv* env,
                                                                          jclass,
                                                                          jstring class_name,
                                                                          jbyteArray class_array,
                                                                          jbyteArray dex_array) {
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

extern "C" JNIEXPORT void Java_Main_setPopRetransformations(JNIEnv*,
                                                            jclass,
                                                            jboolean enable) {
  gPopTransformations = enable;
}

extern "C" JNIEXPORT void Java_Main_popTransformationFor(JNIEnv* env,
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

extern "C" JNIEXPORT void Java_Main_enableCommonRetransformation(JNIEnv* env,
                                                                 jclass,
                                                                 jboolean enable) {
  jvmtiError res = jvmti_env->SetEventNotificationMode(enable ? JVMTI_ENABLE : JVMTI_DISABLE,
                                                       JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                                                       nullptr);
  if (res != JVMTI_ERROR_NONE) {
    JvmtiErrorToException(env, res);
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

extern "C" JNIEXPORT void JNICALL Java_Main_doCommonClassRetransformation(JNIEnv* env,
                                                                          jclass,
                                                                          jobjectArray targets) {
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
  SetAllCapabilities(jvmti_env);
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = CommonClassFileLoadHookRetransformable;
  if (jvmti_env->SetEventCallbacks(&cb, sizeof(cb)) != JVMTI_ERROR_NONE) {
    printf("Unable to set class file load hook cb!\n");
    return 1;
  }
  return 0;
}

}  // namespace common_retransform

namespace common_transform {

using art::common_retransform::CommonClassFileLoadHookRetransformable;

// Get all capabilities except those related to retransformation.
jint OnLoad(JavaVM* vm,
            char* options ATTRIBUTE_UNUSED,
            void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0)) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  // Don't set the retransform caps
  jvmtiCapabilities caps;
  jvmti_env->GetPotentialCapabilities(&caps);
  caps.can_retransform_classes = 0;
  caps.can_retransform_any_class = 0;
  jvmti_env->AddCapabilities(&caps);

  // Use the same callback as the retransform test.
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = CommonClassFileLoadHookRetransformable;
  if (jvmti_env->SetEventCallbacks(&cb, sizeof(cb)) != JVMTI_ERROR_NONE) {
    printf("Unable to set class file load hook cb!\n");
    return 1;
  }
  return 0;
}

}  // namespace common_transform

static void BindMethod(jvmtiEnv* jenv,
                       JNIEnv* env,
                       jclass klass,
                       jmethodID method) {
  char* name;
  char* signature;
  jvmtiError name_result = jenv->GetMethodName(method, &name, &signature, nullptr);
  if (name_result != JVMTI_ERROR_NONE) {
    LOG(FATAL) << "Could not get methods";
  }

  std::string names[2];
  if (IsJVM()) {
    // TODO Get the JNI long name
    char* klass_name;
    jvmtiError klass_result = jenv->GetClassSignature(klass, &klass_name, nullptr);
    if (klass_result == JVMTI_ERROR_NONE) {
      std::string name_str(name);
      std::string klass_str(klass_name);
      names[0] = GetJniShortName(klass_str, name_str);
      jenv->Deallocate(reinterpret_cast<unsigned char*>(klass_name));
    } else {
      LOG(FATAL) << "Could not get class name!";
    }
  } else {
    ScopedObjectAccess soa(Thread::Current());
    ArtMethod* m = jni::DecodeArtMethod(method);
    names[0] = m->JniShortName();
    names[1] = m->JniLongName();
  }
  for (const std::string& mangled_name : names) {
    if (mangled_name == "") {
      continue;
    }
    void* sym = dlsym(RTLD_DEFAULT, mangled_name.c_str());
    if (sym == nullptr) {
      continue;
    }

    JNINativeMethod native_method;
    native_method.fnPtr = sym;
    native_method.name = name;
    native_method.signature = signature;

    env->RegisterNatives(klass, &native_method, 1);

    jenv->Deallocate(reinterpret_cast<unsigned char*>(name));
    jenv->Deallocate(reinterpret_cast<unsigned char*>(signature));
    return;
  }

  LOG(FATAL) << "Could not find " << names[0];
}

static jclass FindClassWithSystemClassLoader(JNIEnv* env, const char* class_name) {
  // Find the system classloader.
  ScopedLocalRef<jclass> cl_klass(env, env->FindClass("java/lang/ClassLoader"));
  if (cl_klass.get() == nullptr) {
    return nullptr;
  }
  jmethodID getsystemclassloader_method = env->GetStaticMethodID(cl_klass.get(),
                                                                 "getSystemClassLoader",
                                                                 "()Ljava/lang/ClassLoader;");
  if (getsystemclassloader_method == nullptr) {
    return nullptr;
  }
  ScopedLocalRef<jobject> cl(env, env->CallStaticObjectMethod(cl_klass.get(),
                                                              getsystemclassloader_method));
  if (cl.get() == nullptr) {
    return nullptr;
  }

  // Create a String of the name.
  std::string descriptor = android::base::StringPrintf("L%s;", class_name);
  std::string dot_name = DescriptorToDot(descriptor.c_str());
  ScopedLocalRef<jstring> name_str(env, env->NewStringUTF(dot_name.c_str()));

  // Call Class.forName with it.
  ScopedLocalRef<jclass> c_klass(env, env->FindClass("java/lang/Class"));
  if (c_klass.get() == nullptr) {
    return nullptr;
  }
  jmethodID forname_method = env->GetStaticMethodID(
      c_klass.get(),
      "forName",
      "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
  if (forname_method == nullptr) {
    return nullptr;
  }

  return reinterpret_cast<jclass>(env->CallStaticObjectMethod(c_klass.get(),
                                                              forname_method,
                                                              name_str.get(),
                                                              JNI_FALSE,
                                                              cl.get()));
}

void BindFunctions(jvmtiEnv* jenv, JNIEnv* env, const char* class_name) {
  // Use JNI to load the class.
  ScopedLocalRef<jclass> klass(env, env->FindClass(class_name));
  if (klass.get() == nullptr) {
    // We may be called with the wrong classloader. Try explicitly using the system classloader.
    env->ExceptionClear();
    klass.reset(FindClassWithSystemClassLoader(env, class_name));
    if (klass.get() == nullptr) {
      LOG(FATAL) << "Could not load " << class_name;
    }
  }
  BindFunctionsOnClass(jenv, env, klass.get());
}

void BindFunctionsOnClass(jvmtiEnv* jenv, JNIEnv* env, jclass klass) {
  // Use JVMTI to get the methods.
  jint method_count;
  jmethodID* methods;
  jvmtiError methods_result = jenv->GetClassMethods(klass, &method_count, &methods);
  if (methods_result != JVMTI_ERROR_NONE) {
    LOG(FATAL) << "Could not get methods";
  }

  // Check each method.
  for (jint i = 0; i < method_count; ++i) {
    jint modifiers;
    jvmtiError mod_result = jenv->GetMethodModifiers(methods[i], &modifiers);
    if (mod_result != JVMTI_ERROR_NONE) {
      LOG(FATAL) << "Could not get methods";
    }
    constexpr jint kNative = static_cast<jint>(kAccNative);
    if ((modifiers & kNative) != 0) {
      BindMethod(jenv, env, klass, methods[i]);
    }
  }

  jenv->Deallocate(reinterpret_cast<unsigned char*>(methods));
}

}  // namespace art
