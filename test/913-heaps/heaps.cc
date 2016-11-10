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

#include "heaps.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "base/stringprintf.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test913Heaps {

extern "C" JNIEXPORT void JNICALL Java_Main_forceGarbageCollection(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                   jclass klass ATTRIBUTE_UNUSED) {
  jvmtiError ret = jvmti_env->ForceGarbageCollection();
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error forcing a garbage collection: %s\n", err);
  }
}

class IterationConfig {
 public:
  IterationConfig() {}
  virtual ~IterationConfig() {}

  virtual jint Handle(jvmtiHeapReferenceKind reference_kind,
                      const jvmtiHeapReferenceInfo* reference_info,
                      jlong class_tag,
                      jlong referrer_class_tag,
                      jlong size,
                      jlong* tag_ptr,
                      jlong* referrer_tag_ptr,
                      jint length,
                      void* user_data) = 0;
};

static jint JNICALL HeapReferenceCallback(jvmtiHeapReferenceKind reference_kind,
                                          const jvmtiHeapReferenceInfo* reference_info,
                                          jlong class_tag,
                                          jlong referrer_class_tag,
                                          jlong size,
                                          jlong* tag_ptr,
                                          jlong* referrer_tag_ptr,
                                          jint length,
                                          void* user_data) {
  IterationConfig* config = reinterpret_cast<IterationConfig*>(user_data);
  return config->Handle(reference_kind,
                        reference_info,
                        class_tag,
                        referrer_class_tag,
                        size,
                        tag_ptr,
                        referrer_tag_ptr,
                        length,
                        user_data);
}

static bool Run(jint heap_filter,
                jclass klass_filter,
                jobject initial_object,
                IterationConfig* config) {
  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_reference_callback = HeapReferenceCallback;

  jvmtiError ret = jvmti_env->FollowReferences(heap_filter,
                                               klass_filter,
                                               initial_object,
                                               &callbacks,
                                               config);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Failure running FollowReferences: %s\n", err);
    return false;
  }
  return true;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_followReferences(JNIEnv* env,
                                                                     jclass klass ATTRIBUTE_UNUSED,
                                                                     jint heap_filter,
                                                                     jclass klass_filter,
                                                                     jobject initial_object,
                                                                     jint stop_after,
                                                                     jint follow_set,
                                                                     jobject jniRef) {
  class PrintIterationConfig FINAL : public IterationConfig {
   public:
    PrintIterationConfig(jint _stop_after, jint _follow_set)
        : counter_(0),
          stop_after_(_stop_after),
          follow_set_(_follow_set) {
    }

    jint Handle(jvmtiHeapReferenceKind reference_kind,
                const jvmtiHeapReferenceInfo* reference_info,
                jlong class_tag,
                jlong referrer_class_tag,
                jlong size,
                jlong* tag_ptr,
                jlong* referrer_tag_ptr,
                jint length,
                void* user_data ATTRIBUTE_UNUSED) OVERRIDE {
      jlong tag = *tag_ptr;
      // Only check tagged objects.
      if (tag == 0) {
        return JVMTI_VISIT_OBJECTS;
      }

      Print(reference_kind,
            reference_info,
            class_tag,
            referrer_class_tag,
            size,
            tag_ptr,
            referrer_tag_ptr,
            length);

      counter_++;
      if (counter_ == stop_after_) {
        return JVMTI_VISIT_ABORT;
      }

      if (tag > 0 && tag < 32) {
        bool should_visit_references = (follow_set_ & (1 << static_cast<int32_t>(tag))) != 0;
        return should_visit_references ? JVMTI_VISIT_OBJECTS : 0;
      }

      return JVMTI_VISIT_OBJECTS;
    }

    void Print(jvmtiHeapReferenceKind reference_kind,
               const jvmtiHeapReferenceInfo* reference_info,
               jlong class_tag,
               jlong referrer_class_tag,
               jlong size,
               jlong* tag_ptr,
               jlong* referrer_tag_ptr,
               jint length) {
      std::string referrer_str;
      if (referrer_tag_ptr == nullptr) {
        referrer_str = "root@root";
      } else {
        referrer_str = StringPrintf("%" PRId64 "@%" PRId64, *referrer_tag_ptr, referrer_class_tag);
      }

      jlong adapted_size = size;
      if (*tag_ptr >= 1000) {
        // This is a class or interface, the size of which will be dependent on the architecture.
        // Do not print the size, but detect known values and "normalize" for the golden file.
        if ((sizeof(void*) == 4 && size == 180) || (sizeof(void*) == 8 && size == 232)) {
          adapted_size = 123;
        }
      }

      lines_.push_back(
          StringPrintf("%s --(%s)--> %" PRId64 "@%" PRId64 " [size=%" PRId64 ", length=%d]",
                       referrer_str.c_str(),
                       GetReferenceTypeStr(reference_kind, reference_info).c_str(),
                       *tag_ptr,
                       class_tag,
                       adapted_size,
                       length));
    }

    static std::string GetReferenceTypeStr(jvmtiHeapReferenceKind reference_kind,
                                           const jvmtiHeapReferenceInfo* reference_info) {
      switch (reference_kind) {
        case JVMTI_HEAP_REFERENCE_CLASS:
          return "class";
        case JVMTI_HEAP_REFERENCE_FIELD:
          return StringPrintf("field@%d", reference_info->field.index);
        case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT:
          return StringPrintf("array-element@%d", reference_info->array.index);
        case JVMTI_HEAP_REFERENCE_CLASS_LOADER:
          return "classloader";
        case JVMTI_HEAP_REFERENCE_SIGNERS:
          return "signers";
        case JVMTI_HEAP_REFERENCE_PROTECTION_DOMAIN:
          return "protection-domain";
        case JVMTI_HEAP_REFERENCE_INTERFACE:
          return "interface";
        case JVMTI_HEAP_REFERENCE_STATIC_FIELD:
          return StringPrintf("static-field@%d", reference_info->field.index);
        case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
          return "constant-pool";
        case JVMTI_HEAP_REFERENCE_SUPERCLASS:
          return "superclass";
        case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
          return "jni-global";
        case JVMTI_HEAP_REFERENCE_SYSTEM_CLASS:
          return "system-class";
        case JVMTI_HEAP_REFERENCE_MONITOR:
          return "monitor";
        case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
          return "stack-local";
        case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
          return "jni-local";
        case JVMTI_HEAP_REFERENCE_THREAD:
          return "thread";
        case JVMTI_HEAP_REFERENCE_OTHER:
          return "other";
      }
      return "unknown";
    }

    const std::vector<std::string>& GetLines() const {
      return lines_;
    }

   private:
    jint counter_;
    const jint stop_after_;
    const jint follow_set_;
    std::vector<std::string> lines_;
  };

  // If jniRef isn't null, add a local and a global ref.
  ScopedLocalRef<jobject> jni_local_ref(env, nullptr);
  jobject jni_global_ref = nullptr;
  if (jniRef != nullptr) {
    jni_local_ref.reset(env->NewLocalRef(jniRef));
    jni_global_ref = env->NewGlobalRef(jniRef);
  }

  PrintIterationConfig config(stop_after, follow_set);
  Run(heap_filter, klass_filter, initial_object, &config);

  const std::vector<std::string>& lines = config.GetLines();
  jobjectArray ret = CreateObjectArray(env,
                                       static_cast<jint>(lines.size()),
                                       "java/lang/String",
                                       [&](jint i) {
                                         return env->NewStringUTF(lines[i].c_str());
                                       });

  if (jni_global_ref != nullptr) {
    env->DeleteGlobalRef(jni_global_ref);
  }

  return ret;
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

}  // namespace Test913Heaps
}  // namespace art
