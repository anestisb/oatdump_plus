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

#include "heap.h"

#include "art_jvmti.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "class_linker.h"
#include "gc/heap.h"
#include "java_vm_ext.h"
#include "jni_env_ext.h"
#include "mirror/class.h"
#include "object_callbacks.h"
#include "object_tagging.h"
#include "obj_ptr-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

namespace openjdkjvmti {

struct IterateThroughHeapData {
  IterateThroughHeapData(HeapUtil* _heap_util,
                         jint heap_filter,
                         art::ObjPtr<art::mirror::Class> klass,
                         const jvmtiHeapCallbacks* _callbacks,
                         const void* _user_data)
      : heap_util(_heap_util),
        filter_klass(klass),
        callbacks(_callbacks),
        user_data(_user_data),
        filter_out_tagged((heap_filter & JVMTI_HEAP_FILTER_TAGGED) != 0),
        filter_out_untagged((heap_filter & JVMTI_HEAP_FILTER_UNTAGGED) != 0),
        filter_out_class_tagged((heap_filter & JVMTI_HEAP_FILTER_CLASS_TAGGED) != 0),
        filter_out_class_untagged((heap_filter & JVMTI_HEAP_FILTER_CLASS_UNTAGGED) != 0),
        any_filter(filter_out_tagged ||
                   filter_out_untagged ||
                   filter_out_class_tagged ||
                   filter_out_class_untagged),
        stop_reports(false) {
  }

  bool ShouldReportByHeapFilter(jlong tag, jlong class_tag) {
    if (!any_filter) {
      return true;
    }

    if ((tag == 0 && filter_out_untagged) || (tag != 0 && filter_out_tagged)) {
      return false;
    }

    if ((class_tag == 0 && filter_out_class_untagged) ||
        (class_tag != 0 && filter_out_class_tagged)) {
      return false;
    }

    return true;
  }

  HeapUtil* heap_util;
  art::ObjPtr<art::mirror::Class> filter_klass;
  const jvmtiHeapCallbacks* callbacks;
  const void* user_data;
  const bool filter_out_tagged;
  const bool filter_out_untagged;
  const bool filter_out_class_tagged;
  const bool filter_out_class_untagged;
  const bool any_filter;

  bool stop_reports;
};

static void IterateThroughHeapObjectCallback(art::mirror::Object* obj, void* arg)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  IterateThroughHeapData* ithd = reinterpret_cast<IterateThroughHeapData*>(arg);
  // Early return, as we can't really stop visiting.
  if (ithd->stop_reports) {
    return;
  }

  art::ScopedAssertNoThreadSuspension no_suspension("IterateThroughHeapCallback");

  jlong tag = 0;
  ithd->heap_util->GetTags()->GetTag(obj, &tag);

  jlong class_tag = 0;
  art::ObjPtr<art::mirror::Class> klass = obj->GetClass();
  ithd->heap_util->GetTags()->GetTag(klass.Ptr(), &class_tag);
  // For simplicity, even if we find a tag = 0, assume 0 = not tagged.

  if (!ithd->ShouldReportByHeapFilter(tag, class_tag)) {
    return;
  }

  // TODO: Handle array_primitive_value_callback.

  if (ithd->filter_klass != nullptr) {
    if (ithd->filter_klass != klass) {
      return;
    }
  }

  jlong size = obj->SizeOf();

  jint length = -1;
  if (obj->IsArrayInstance()) {
    length = obj->AsArray()->GetLength();
  }

  jlong saved_tag = tag;
  jint ret = ithd->callbacks->heap_iteration_callback(class_tag,
                                                      size,
                                                      &tag,
                                                      length,
                                                      const_cast<void*>(ithd->user_data));

  if (tag != saved_tag) {
    ithd->heap_util->GetTags()->Set(obj, tag);
  }

  ithd->stop_reports = (ret & JVMTI_VISIT_ABORT) != 0;

  // TODO Implement array primitive and string primitive callback.
  // TODO Implement primitive field callback.
}

jvmtiError HeapUtil::IterateThroughHeap(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                        jint heap_filter,
                                        jclass klass,
                                        const jvmtiHeapCallbacks* callbacks,
                                        const void* user_data) {
  if (callbacks == nullptr) {
    return ERR(NULL_POINTER);
  }

  if (callbacks->array_primitive_value_callback != nullptr) {
    // TODO: Implement.
    return ERR(NOT_IMPLEMENTED);
  }

  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);      // Now we know we have the shared lock.

  IterateThroughHeapData ithd(this,
                              heap_filter,
                              soa.Decode<art::mirror::Class>(klass),
                              callbacks,
                              user_data);

  art::Runtime::Current()->GetHeap()->VisitObjects(IterateThroughHeapObjectCallback, &ithd);

  return ERR(NONE);
}

jvmtiError HeapUtil::GetLoadedClasses(jvmtiEnv* env,
                                      jint* class_count_ptr,
                                      jclass** classes_ptr) {
  if (class_count_ptr == nullptr || classes_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  class ReportClassVisitor : public art::ClassVisitor {
   public:
    explicit ReportClassVisitor(art::Thread* self) : self_(self) {}

    bool operator()(art::mirror::Class* klass) OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
      art::JNIEnvExt* jni_env = self_->GetJniEnv();
      classes_.push_back(reinterpret_cast<jclass>(jni_env->vm->AddGlobalRef(self_, klass)));
      return true;
    }

    art::Thread* self_;
    std::vector<jclass> classes_;
  };

  art::Thread* self = art::Thread::Current();
  ReportClassVisitor rcv(self);
  {
    art::ScopedObjectAccess soa(self);
    art::Runtime::Current()->GetClassLinker()->VisitClasses(&rcv);
  }

  size_t size = rcv.classes_.size();
  jclass* classes = nullptr;
  jvmtiError alloc_ret = env->Allocate(static_cast<jlong>(size * sizeof(jclass)),
                                       reinterpret_cast<unsigned char**>(&classes));
  if (alloc_ret != ERR(NONE)) {
    return alloc_ret;
  }

  for (size_t i = 0; i < size; ++i) {
    classes[i] = rcv.classes_[i];
  }
  *classes_ptr = classes;
  *class_count_ptr = static_cast<jint>(size);

  return ERR(NONE);
}

}  // namespace openjdkjvmti
