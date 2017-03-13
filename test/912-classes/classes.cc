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

#include <stdio.h>

#include "base/macros.h"
#include "class_linker.h"
#include "jni.h"
#include "mirror/class_loader.h"
#include "jvmti.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "ScopedUtfChars.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test912Classes {

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isModifiableClass(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jboolean res = JNI_FALSE;
  jvmtiError result = jvmti_env->IsModifiableClass(klass, &res);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running IsModifiableClass: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return JNI_FALSE;
  }
  return res;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassSignature(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  char* sig;
  char* gen;
  jvmtiError result = jvmti_env->GetClassSignature(klass, &sig, &gen);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassSignature: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return nullptr;
  }

  auto callback = [&](jint i) {
    if (i == 0) {
      return sig == nullptr ? nullptr : env->NewStringUTF(sig);
    } else {
      return gen == nullptr ? nullptr : env->NewStringUTF(gen);
    }
  };
  jobjectArray ret = CreateObjectArray(env, 2, "java/lang/String", callback);

  // Need to deallocate the strings.
  if (sig != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(sig));
  }
  if (gen != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(gen));
  }

  return ret;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isInterface(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jboolean is_interface = JNI_FALSE;
  jvmtiError result = jvmti_env->IsInterface(klass, &is_interface);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running IsInterface: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return JNI_FALSE;
  }
  return is_interface;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isArrayClass(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jboolean is_array_class = JNI_FALSE;
  jvmtiError result = jvmti_env->IsArrayClass(klass, &is_array_class);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running IsArrayClass: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return JNI_FALSE;
  }
  return is_array_class;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getClassModifiers(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint mod;
  jvmtiError result = jvmti_env->GetClassModifiers(klass, &mod);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassModifiers: %s\n", err);
    return JNI_FALSE;
  }
  return mod;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassFields(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint count = 0;
  jfieldID* fields = nullptr;
  jvmtiError result = jvmti_env->GetClassFields(klass, &count, &fields);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassFields: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return nullptr;
  }

  auto callback = [&](jint i) {
    jint modifiers;
    // Ignore any errors for simplicity.
    jvmti_env->GetFieldModifiers(klass, fields[i], &modifiers);
    constexpr jint kStatic = 0x8;
    return env->ToReflectedField(klass,
                                 fields[i],
                                 (modifiers & kStatic) != 0 ? JNI_TRUE : JNI_FALSE);
  };
  jobjectArray ret = CreateObjectArray(env, count, "java/lang/Object", callback);
  if (fields != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(fields));
  }
  return ret;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassMethods(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint count = 0;
  jmethodID* methods = nullptr;
  jvmtiError result = jvmti_env->GetClassMethods(klass, &count, &methods);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassMethods: %s\n", err);
    return nullptr;
  }

  auto callback = [&](jint i) {
    jint modifiers;
    // Ignore any errors for simplicity.
    jvmti_env->GetMethodModifiers(methods[i], &modifiers);
    constexpr jint kStatic = 0x8;
    return env->ToReflectedMethod(klass,
                                  methods[i],
                                  (modifiers & kStatic) != 0 ? JNI_TRUE : JNI_FALSE);
  };
  jobjectArray ret = CreateObjectArray(env, count, "java/lang/Object", callback);
  if (methods != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(methods));
  }
  return ret;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getImplementedInterfaces(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint count = 0;
  jclass* classes = nullptr;
  jvmtiError result = jvmti_env->GetImplementedInterfaces(klass, &count, &classes);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetImplementedInterfaces: %s\n", err);
    return nullptr;
  }

  auto callback = [&](jint i) {
    return classes[i];
  };
  jobjectArray ret = CreateObjectArray(env, count, "java/lang/Class", callback);
  if (classes != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  return ret;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_getClassStatus(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint status;
  jvmtiError result = jvmti_env->GetClassStatus(klass, &status);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassStatus: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return JNI_FALSE;
  }
  return status;
}

extern "C" JNIEXPORT jobject JNICALL Java_Main_getClassLoader(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jobject classloader;
  jvmtiError result = jvmti_env->GetClassLoader(klass, &classloader);
  if (result != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(result, &err);
    printf("Failure running GetClassLoader: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return nullptr;
  }
  return classloader;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_getClassLoaderClasses(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jobject jclassloader) {
  jint count = 0;
  jclass* classes = nullptr;
  jvmtiError result = jvmti_env->GetClassLoaderClasses(jclassloader, &count, &classes);
  if (JvmtiErrorToException(env, result)) {
    return nullptr;
  }

  auto callback = [&](jint i) {
    return classes[i];
  };
  jobjectArray ret = CreateObjectArray(env, count, "java/lang/Class", callback);
  if (classes != nullptr) {
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  return ret;
}

extern "C" JNIEXPORT jintArray JNICALL Java_Main_getClassVersion(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  jint major, minor;
  jvmtiError result = jvmti_env->GetClassVersionNumbers(klass, &minor, &major);
  if (JvmtiErrorToException(env, result)) {
    return nullptr;
  }

  jintArray int_array = env->NewIntArray(2);
  if (int_array == nullptr) {
    return nullptr;
  }
  jint buf[2] = { major, minor };
  env->SetIntArrayRegion(int_array, 0, 2, buf);

  return int_array;
}

static std::string GetClassName(jvmtiEnv* jenv, JNIEnv* jni_env, jclass klass) {
  char* name;
  jvmtiError result = jenv->GetClassSignature(klass, &name, nullptr);
  if (result != JVMTI_ERROR_NONE) {
    if (jni_env != nullptr) {
      JvmtiErrorToException(jni_env, result);
    } else {
      printf("Failed to get class signature.\n");
    }
    return "";
  }

  std::string tmp(name);
  jenv->Deallocate(reinterpret_cast<unsigned char*>(name));

  return tmp;
}

static void EnableEvents(JNIEnv* env,
                         jboolean enable,
                         decltype(jvmtiEventCallbacks().ClassLoad) class_load,
                         decltype(jvmtiEventCallbacks().ClassPrepare) class_prepare) {
  if (enable == JNI_FALSE) {
    jvmtiError ret = jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                                         JVMTI_EVENT_CLASS_LOAD,
                                                         nullptr);
    if (JvmtiErrorToException(env, ret)) {
      return;
    }
    ret = jvmti_env->SetEventNotificationMode(JVMTI_DISABLE,
                                              JVMTI_EVENT_CLASS_PREPARE,
                                              nullptr);
    JvmtiErrorToException(env, ret);
    return;
  }

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.ClassLoad = class_load;
  callbacks.ClassPrepare = class_prepare;
  jvmtiError ret = jvmti_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
  if (JvmtiErrorToException(env, ret)) {
    return;
  }

  ret = jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                            JVMTI_EVENT_CLASS_LOAD,
                                            nullptr);
  if (JvmtiErrorToException(env, ret)) {
    return;
  }
  ret = jvmti_env->SetEventNotificationMode(JVMTI_ENABLE,
                                            JVMTI_EVENT_CLASS_PREPARE,
                                            nullptr);
  JvmtiErrorToException(env, ret);
}

class ClassLoadPreparePrinter {
 public:
  static void JNICALL ClassLoadCallback(jvmtiEnv* jenv,
                                        JNIEnv* jni_env,
                                        jthread thread,
                                        jclass klass) {
    std::string name = GetClassName(jenv, jni_env, klass);
    if (name == "") {
      return;
    }
    std::string thread_name = GetThreadName(jenv, jni_env, thread);
    if (thread_name == "") {
      return;
    }
    printf("Load: %s on %s\n", name.c_str(), thread_name.c_str());
  }

  static void JNICALL ClassPrepareCallback(jvmtiEnv* jenv,
                                           JNIEnv* jni_env,
                                           jthread thread,
                                           jclass klass) {
    std::string name = GetClassName(jenv, jni_env, klass);
    if (name == "") {
      return;
    }
    std::string thread_name = GetThreadName(jenv, jni_env, thread);
    if (thread_name == "") {
      return;
    }
    std::string cur_thread_name = GetThreadName(Thread::Current());
    printf("Prepare: %s on %s (cur=%s)\n",
           name.c_str(),
           thread_name.c_str(),
           cur_thread_name.c_str());
  }

 private:
  static std::string GetThreadName(jvmtiEnv* jenv, JNIEnv* jni_env, jthread thread) {
    jvmtiThreadInfo info;
    jvmtiError result = jenv->GetThreadInfo(thread, &info);
    if (result != JVMTI_ERROR_NONE) {
      if (jni_env != nullptr) {
        JvmtiErrorToException(jni_env, result);
      } else {
        printf("Failed to get thread name.\n");
      }
      return "";
    }

    std::string tmp(info.name);
    jenv->Deallocate(reinterpret_cast<unsigned char*>(info.name));
    jni_env->DeleteLocalRef(info.context_class_loader);
    jni_env->DeleteLocalRef(info.thread_group);

    return tmp;
  }

  static std::string GetThreadName(Thread* thread) {
    std::string tmp;
    thread->GetThreadName(tmp);
    return tmp;
  }
};

extern "C" JNIEXPORT void JNICALL Java_Main_enableClassLoadPreparePrintEvents(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jboolean enable) {
  EnableEvents(env,
               enable,
               ClassLoadPreparePrinter::ClassLoadCallback,
               ClassLoadPreparePrinter::ClassPrepareCallback);
}

struct ClassLoadSeen {
  static void JNICALL ClassLoadSeenCallback(jvmtiEnv* jenv ATTRIBUTE_UNUSED,
                                            JNIEnv* jni_env ATTRIBUTE_UNUSED,
                                            jthread thread ATTRIBUTE_UNUSED,
                                            jclass klass ATTRIBUTE_UNUSED) {
    saw_event = true;
  }

  static bool saw_event;
};
bool ClassLoadSeen::saw_event = false;

extern "C" JNIEXPORT void JNICALL Java_Main_enableClassLoadSeenEvents(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jboolean b) {
  EnableEvents(env, b, ClassLoadSeen::ClassLoadSeenCallback, nullptr);
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_hadLoadEvent(
    JNIEnv* env ATTRIBUTE_UNUSED, jclass Main_klass ATTRIBUTE_UNUSED) {
  return ClassLoadSeen::saw_event ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_isLoadedClass(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jstring class_name) {
  ScopedUtfChars name(env, class_name);
  ScopedObjectAccess soa(Thread::Current());
  Runtime* current = Runtime::Current();
  ClassLinker* class_linker = current->GetClassLinker();
  bool found =
      class_linker->LookupClass(
          soa.Self(),
          name.c_str(),
          soa.Decode<mirror::ClassLoader>(current->GetSystemClassLoader())) != nullptr;
  return found ? JNI_TRUE : JNI_FALSE;
}

class ClassLoadPrepareEquality {
 public:
  static constexpr const char* kClassName = "LMain$ClassE;";
  static constexpr const char* kStorageFieldName = "STATIC";
  static constexpr const char* kStorageFieldSig = "Ljava/lang/Object;";
  static constexpr const char* kStorageWeakFieldName = "WEAK";
  static constexpr const char* kStorageWeakFieldSig = "Ljava/lang/ref/Reference;";
  static constexpr const char* kWeakClassName = "java/lang/ref/WeakReference";
  static constexpr const char* kWeakInitSig = "(Ljava/lang/Object;)V";
  static constexpr const char* kWeakGetSig = "()Ljava/lang/Object;";

  static void JNICALL ClassLoadCallback(jvmtiEnv* jenv,
                                        JNIEnv* jni_env,
                                        jthread thread ATTRIBUTE_UNUSED,
                                        jclass klass) {
    std::string name = GetClassName(jenv, jni_env, klass);
    if (name == kClassName) {
      found_ = true;
      stored_class_ = jni_env->NewGlobalRef(klass);
      weakly_stored_class_ = jni_env->NewWeakGlobalRef(klass);
      // The following is bad and relies on implementation details. But otherwise a test would be
      // a lot more complicated.
      local_stored_class_ = jni_env->NewLocalRef(klass);
      // Store the value into a field in the heap.
      SetOrCompare(jni_env, klass, true);
    }
  }

  static void JNICALL ClassPrepareCallback(jvmtiEnv* jenv,
                                           JNIEnv* jni_env,
                                           jthread thread ATTRIBUTE_UNUSED,
                                           jclass klass) {
    std::string name = GetClassName(jenv, jni_env, klass);
    if (name == kClassName) {
      CHECK(stored_class_ != nullptr);
      CHECK(jni_env->IsSameObject(stored_class_, klass));
      CHECK(jni_env->IsSameObject(weakly_stored_class_, klass));
      CHECK(jni_env->IsSameObject(local_stored_class_, klass));
      // Look up the value in a field in the heap.
      SetOrCompare(jni_env, klass, false);
      compared_ = true;
    }
  }

  static void SetOrCompare(JNIEnv* jni_env, jobject value, bool set) {
    CHECK(storage_class_ != nullptr);

    // Simple direct storage.
    jfieldID field = jni_env->GetStaticFieldID(storage_class_, kStorageFieldName, kStorageFieldSig);
    CHECK(field != nullptr);

    if (set) {
      jni_env->SetStaticObjectField(storage_class_, field, value);
      CHECK(!jni_env->ExceptionCheck());
    } else {
      ScopedLocalRef<jobject> stored(jni_env, jni_env->GetStaticObjectField(storage_class_, field));
      CHECK(jni_env->IsSameObject(value, stored.get()));
    }

    // Storage as a reference.
    ScopedLocalRef<jclass> weak_ref_class(jni_env, jni_env->FindClass(kWeakClassName));
    CHECK(weak_ref_class.get() != nullptr);
    jfieldID weak_field = jni_env->GetStaticFieldID(storage_class_,
                                                    kStorageWeakFieldName,
                                                    kStorageWeakFieldSig);
    CHECK(weak_field != nullptr);
    if (set) {
      // Create a WeakReference.
      jmethodID weak_init = jni_env->GetMethodID(weak_ref_class.get(), "<init>", kWeakInitSig);
      CHECK(weak_init != nullptr);
      ScopedLocalRef<jobject> weak_obj(jni_env, jni_env->NewObject(weak_ref_class.get(),
                                                                   weak_init,
                                                                   value));
      CHECK(weak_obj.get() != nullptr);
      jni_env->SetStaticObjectField(storage_class_, weak_field, weak_obj.get());
      CHECK(!jni_env->ExceptionCheck());
    } else {
      // Check the reference value.
      jmethodID get_referent = jni_env->GetMethodID(weak_ref_class.get(), "get", kWeakGetSig);
      CHECK(get_referent != nullptr);
      ScopedLocalRef<jobject> weak_obj(jni_env, jni_env->GetStaticObjectField(storage_class_,
                                                                              weak_field));
      CHECK(weak_obj.get() != nullptr);
      ScopedLocalRef<jobject> weak_referent(jni_env, jni_env->CallObjectMethod(weak_obj.get(),
                                                                               get_referent));
      CHECK(weak_referent.get() != nullptr);
      CHECK(jni_env->IsSameObject(value, weak_referent.get()));
    }
  }

  static void CheckFound() {
    CHECK(found_);
    CHECK(compared_);
  }

  static void Free(JNIEnv* env) {
    if (stored_class_ != nullptr) {
      env->DeleteGlobalRef(stored_class_);
      DCHECK(weakly_stored_class_ != nullptr);
      env->DeleteWeakGlobalRef(weakly_stored_class_);
      // Do not attempt to delete the local ref. It will be out of date by now.
    }
  }

  static jclass storage_class_;

 private:
  static jobject stored_class_;
  static jweak weakly_stored_class_;
  static jobject local_stored_class_;
  static bool found_;
  static bool compared_;
};
jclass ClassLoadPrepareEquality::storage_class_ = nullptr;
jobject ClassLoadPrepareEquality::stored_class_ = nullptr;
jweak ClassLoadPrepareEquality::weakly_stored_class_ = nullptr;
jobject ClassLoadPrepareEquality::local_stored_class_ = nullptr;
bool ClassLoadPrepareEquality::found_ = false;
bool ClassLoadPrepareEquality::compared_ = false;

extern "C" JNIEXPORT void JNICALL Java_Main_setEqualityEventStorageClass(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jclass klass) {
  ClassLoadPrepareEquality::storage_class_ =
      reinterpret_cast<jclass>(env->NewGlobalRef(klass));
}

extern "C" JNIEXPORT void JNICALL Java_Main_enableClassLoadPrepareEqualityEvents(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED, jboolean b) {
  EnableEvents(env,
               b,
               ClassLoadPrepareEquality::ClassLoadCallback,
               ClassLoadPrepareEquality::ClassPrepareCallback);
  if (b == JNI_FALSE) {
    ClassLoadPrepareEquality::Free(env);
    ClassLoadPrepareEquality::CheckFound();
    env->DeleteGlobalRef(ClassLoadPrepareEquality::storage_class_);
    ClassLoadPrepareEquality::storage_class_ = nullptr;
  }
}

}  // namespace Test912Classes
}  // namespace art
