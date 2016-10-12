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

#include "iterate_heap.h"

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <vector>

#include "base/logging.h"
#include "jni.h"
#include "openjdkjvmti/jvmti.h"
#include "ScopedPrimitiveArray.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test906IterateHeap {

class IterationConfig {
 public:
  IterationConfig() {}
  virtual ~IterationConfig() {}

  virtual jint Handle(jlong class_tag, jlong size, jlong* tag_ptr, jint length) = 0;
};

static jint JNICALL HeapIterationCallback(jlong class_tag,
                                          jlong size,
                                          jlong* tag_ptr,
                                          jint length,
                                          void* user_data) {
  IterationConfig* config = reinterpret_cast<IterationConfig*>(user_data);
  return config->Handle(class_tag, size, tag_ptr, length);
}

static bool Run(jint heap_filter, jclass klass_filter, IterationConfig* config) {
  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_iteration_callback = HeapIterationCallback;

  jvmtiError ret = jvmti_env->IterateThroughHeap(heap_filter,
                                                 klass_filter,
                                                 &callbacks,
                                                 config);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Failure running IterateThroughHeap: %s\n", err);
    return false;
  }
  return true;
}

extern "C" JNIEXPORT jint JNICALL Java_Main_iterateThroughHeapCount(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                    jclass klass ATTRIBUTE_UNUSED,
                                                                    jint heap_filter,
                                                                    jclass klass_filter,
                                                                    jint stop_after) {
  class CountIterationConfig : public IterationConfig {
   public:
    CountIterationConfig(jint _counter, jint _stop_after)
        : counter(_counter),
          stop_after(_stop_after) {
    }

    jint Handle(jlong class_tag ATTRIBUTE_UNUSED,
                jlong size ATTRIBUTE_UNUSED,
                jlong* tag_ptr ATTRIBUTE_UNUSED,
                jint length ATTRIBUTE_UNUSED) OVERRIDE {
      counter++;
      if (counter == stop_after) {
        return JVMTI_VISIT_ABORT;
      }
      return 0;
    }

    jint counter;
    const jint stop_after;
  };

  CountIterationConfig config(0, stop_after);
  Run(heap_filter, klass_filter, &config);

  if (config.counter > config.stop_after) {
    printf("Error: more objects visited than signaled.");
  }

  return config.counter;
}


extern "C" JNIEXPORT jint JNICALL Java_Main_iterateThroughHeapData(JNIEnv* env,
                                                                   jclass klass ATTRIBUTE_UNUSED,
                                                                   jint heap_filter,
                                                                   jclass klass_filter,
                                                                   jlongArray class_tags,
                                                                   jlongArray sizes,
                                                                   jlongArray tags,
                                                                   jintArray lengths) {
  class DataIterationConfig : public IterationConfig {
   public:
    jint Handle(jlong class_tag, jlong size, jlong* tag_ptr, jint length) OVERRIDE {
      class_tags_.push_back(class_tag);
      sizes_.push_back(size);
      tags_.push_back(*tag_ptr);
      lengths_.push_back(length);

      return 0;  // Continue.
    }

    std::vector<jlong> class_tags_;
    std::vector<jlong> sizes_;
    std::vector<jlong> tags_;
    std::vector<jint> lengths_;
  };

  DataIterationConfig config;
  if (!Run(heap_filter, klass_filter, &config)) {
    return -1;
  }

  ScopedLongArrayRW s_class_tags(env, class_tags);
  ScopedLongArrayRW s_sizes(env, sizes);
  ScopedLongArrayRW s_tags(env, tags);
  ScopedIntArrayRW s_lengths(env, lengths);

  for (size_t i = 0; i != config.class_tags_.size(); ++i) {
    s_class_tags[i] = config.class_tags_[i];
    s_sizes[i] = config.sizes_[i];
    s_tags[i] = config.tags_[i];
    s_lengths[i] = config.lengths_[i];
  }

  return static_cast<jint>(config.class_tags_.size());
}

extern "C" JNIEXPORT void JNICALL Java_Main_iterateThroughHeapAdd(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                  jclass klass ATTRIBUTE_UNUSED,
                                                                  jint heap_filter,
                                                                  jclass klass_filter) {
  class AddIterationConfig : public IterationConfig {
   public:
    AddIterationConfig() {}

    jint Handle(jlong class_tag ATTRIBUTE_UNUSED,
                jlong size ATTRIBUTE_UNUSED,
                jlong* tag_ptr,
                jint length ATTRIBUTE_UNUSED) OVERRIDE {
      jlong current_tag = *tag_ptr;
      if (current_tag != 0) {
        *tag_ptr = current_tag + 10;
      }
      return 0;
    }
  };

  AddIterationConfig config;
  Run(heap_filter, klass_filter, &config);
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

}  // namespace Test906IterateHeap
}  // namespace art
