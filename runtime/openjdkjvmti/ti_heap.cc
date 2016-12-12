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

#include "ti_heap.h"

#include "art_field-inl.h"
#include "art_jvmti.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "class_linker.h"
#include "gc/heap.h"
#include "gc_root-inl.h"
#include "jni_env_ext.h"
#include "jni_internal.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "object_callbacks.h"
#include "object_tagging.h"
#include "obj_ptr-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "thread_list.h"

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

class FollowReferencesHelper FINAL {
 public:
  FollowReferencesHelper(HeapUtil* h,
                         art::ObjPtr<art::mirror::Object> initial_object,
                         const jvmtiHeapCallbacks* callbacks,
                         const void* user_data)
      : tag_table_(h->GetTags()),
        initial_object_(initial_object),
        callbacks_(callbacks),
        user_data_(user_data),
        start_(0),
        stop_reports_(false) {
  }

  void Init()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (initial_object_.IsNull()) {
      CollectAndReportRootsVisitor carrv(this, tag_table_, &worklist_, &visited_);

      // We need precise info (e.g., vregs).
      constexpr art::VisitRootFlags kRootFlags = static_cast<art::VisitRootFlags>(
          art::VisitRootFlags::kVisitRootFlagAllRoots | art::VisitRootFlags::kVisitRootFlagPrecise);
      art::Runtime::Current()->VisitRoots(&carrv, kRootFlags);

      art::Runtime::Current()->VisitImageRoots(&carrv);
      stop_reports_ = carrv.IsStopReports();

      if (stop_reports_) {
        worklist_.clear();
      }
    } else {
      visited_.insert(initial_object_.Ptr());
      worklist_.push_back(initial_object_.Ptr());
    }
  }

  void Work()
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    // Currently implemented as a BFS. To lower overhead, we don't erase elements immediately
    // from the head of the work list, instead postponing until there's a gap that's "large."
    //
    // Alternatively, we can implement a DFS and use the work list as a stack.
    while (start_ < worklist_.size()) {
      art::mirror::Object* cur_obj = worklist_[start_];
      start_++;

      if (start_ >= kMaxStart) {
        worklist_.erase(worklist_.begin(), worklist_.begin() + start_);
        start_ = 0;
      }

      VisitObject(cur_obj);

      if (stop_reports_) {
        break;
      }
    }
  }

 private:
  class CollectAndReportRootsVisitor FINAL : public art::RootVisitor {
   public:
    CollectAndReportRootsVisitor(FollowReferencesHelper* helper,
                                 ObjectTagTable* tag_table,
                                 std::vector<art::mirror::Object*>* worklist,
                                 std::unordered_set<art::mirror::Object*>* visited)
        : helper_(helper),
          tag_table_(tag_table),
          worklist_(worklist),
          visited_(visited),
          stop_reports_(false) {}

    void VisitRoots(art::mirror::Object*** roots, size_t count, const art::RootInfo& info)
        OVERRIDE
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*helper_->tag_table_->GetAllowDisallowLock()) {
      for (size_t i = 0; i != count; ++i) {
        AddRoot(*roots[i], info);
      }
    }

    void VisitRoots(art::mirror::CompressedReference<art::mirror::Object>** roots,
                    size_t count,
                    const art::RootInfo& info)
        OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*helper_->tag_table_->GetAllowDisallowLock()) {
      for (size_t i = 0; i != count; ++i) {
        AddRoot(roots[i]->AsMirrorPtr(), info);
      }
    }

    bool IsStopReports() {
      return stop_reports_;
    }

   private:
    void AddRoot(art::mirror::Object* root_obj, const art::RootInfo& info)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      // We use visited_ to mark roots already so we do not need another set.
      if (visited_->find(root_obj) == visited_->end()) {
        visited_->insert(root_obj);
        worklist_->push_back(root_obj);
      }
      ReportRoot(root_obj, info);
    }

    // Remove NO_THREAD_SAFETY_ANALYSIS once ASSERT_CAPABILITY works correctly.
    art::Thread* FindThread(const art::RootInfo& info) NO_THREAD_SAFETY_ANALYSIS {
      art::Locks::thread_list_lock_->AssertExclusiveHeld(art::Thread::Current());
      return art::Runtime::Current()->GetThreadList()->FindThreadByThreadId(info.GetThreadId());
    }

    jvmtiHeapReferenceKind GetReferenceKind(const art::RootInfo& info,
                                            jvmtiHeapReferenceInfo* ref_info)
        REQUIRES_SHARED(art::Locks::mutator_lock_) {
      // TODO: Fill in ref_info.
      memset(ref_info, 0, sizeof(jvmtiHeapReferenceInfo));

      switch (info.GetType()) {
        case art::RootType::kRootJNIGlobal:
          return JVMTI_HEAP_REFERENCE_JNI_GLOBAL;

        case art::RootType::kRootJNILocal:
        {
          uint32_t thread_id = info.GetThreadId();
          ref_info->jni_local.thread_id = thread_id;

          art::Thread* thread = FindThread(info);
          if (thread != nullptr) {
            art::mirror::Object* thread_obj = thread->GetPeer();
            if (thread->IsStillStarting()) {
              thread_obj = nullptr;
            } else {
              thread_obj = thread->GetPeer();
            }
            if (thread_obj != nullptr) {
              ref_info->jni_local.thread_tag = tag_table_->GetTagOrZero(thread_obj);
            }
          }

          // TODO: We don't have this info.
          if (thread != nullptr) {
            ref_info->jni_local.depth = 0;
            art::ArtMethod* method = thread->GetCurrentMethod(nullptr, false /* abort_on_error */);
            if (method != nullptr) {
              ref_info->jni_local.method = art::jni::EncodeArtMethod(method);
            }
          }

          return JVMTI_HEAP_REFERENCE_JNI_LOCAL;
        }

        case art::RootType::kRootJavaFrame:
        {
          uint32_t thread_id = info.GetThreadId();
          ref_info->stack_local.thread_id = thread_id;

          art::Thread* thread = FindThread(info);
          if (thread != nullptr) {
            art::mirror::Object* thread_obj = thread->GetPeer();
            if (thread->IsStillStarting()) {
              thread_obj = nullptr;
            } else {
              thread_obj = thread->GetPeer();
            }
            if (thread_obj != nullptr) {
              ref_info->stack_local.thread_tag = tag_table_->GetTagOrZero(thread_obj);
            }
          }

          auto& java_info = static_cast<const art::JavaFrameRootInfo&>(info);
          ref_info->stack_local.slot = static_cast<jint>(java_info.GetVReg());
          const art::StackVisitor* visitor = java_info.GetVisitor();
          ref_info->stack_local.location =
              static_cast<jlocation>(visitor->GetDexPc(false /* abort_on_failure */));
          ref_info->stack_local.depth = static_cast<jint>(visitor->GetFrameDepth());
          art::ArtMethod* method = visitor->GetMethod();
          if (method != nullptr) {
            ref_info->stack_local.method = art::jni::EncodeArtMethod(method);
          }

          return JVMTI_HEAP_REFERENCE_STACK_LOCAL;
        }

        case art::RootType::kRootNativeStack:
        case art::RootType::kRootThreadBlock:
        case art::RootType::kRootThreadObject:
          return JVMTI_HEAP_REFERENCE_THREAD;

        case art::RootType::kRootStickyClass:
        case art::RootType::kRootInternedString:
          // Note: this isn't a root in the RI.
          return JVMTI_HEAP_REFERENCE_SYSTEM_CLASS;

        case art::RootType::kRootMonitorUsed:
        case art::RootType::kRootJNIMonitor:
          return JVMTI_HEAP_REFERENCE_MONITOR;

        case art::RootType::kRootFinalizing:
        case art::RootType::kRootDebugger:
        case art::RootType::kRootReferenceCleanup:
        case art::RootType::kRootVMInternal:
        case art::RootType::kRootUnknown:
          return JVMTI_HEAP_REFERENCE_OTHER;
      }
      LOG(FATAL) << "Unreachable";
      UNREACHABLE();
    }

    void ReportRoot(art::mirror::Object* root_obj, const art::RootInfo& info)
        REQUIRES_SHARED(art::Locks::mutator_lock_)
        REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
      jvmtiHeapReferenceInfo ref_info;
      jvmtiHeapReferenceKind kind = GetReferenceKind(info, &ref_info);
      jint result = helper_->ReportReference(kind, &ref_info, nullptr, root_obj);
      if ((result & JVMTI_VISIT_ABORT) != 0) {
        stop_reports_ = true;
      }
    }

   private:
    FollowReferencesHelper* helper_;
    ObjectTagTable* tag_table_;
    std::vector<art::mirror::Object*>* worklist_;
    std::unordered_set<art::mirror::Object*>* visited_;
    bool stop_reports_;
  };

  void VisitObject(art::mirror::Object* obj)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (obj->IsClass()) {
      VisitClass(obj->AsClass());
      return;
    }
    if (obj->IsArrayInstance()) {
      VisitArray(obj);
      return;
    }

    // TODO: We'll probably have to rewrite this completely with our own visiting logic, if we
    //       want to have a chance of getting the field indices computed halfway efficiently. For
    //       now, ignore them altogether.

    struct InstanceReferenceVisitor {
      explicit InstanceReferenceVisitor(FollowReferencesHelper* helper_)
          : helper(helper_), stop_reports(false) {}

      void operator()(art::mirror::Object* src,
                      art::MemberOffset field_offset,
                      bool is_static ATTRIBUTE_UNUSED) const
          REQUIRES_SHARED(art::Locks::mutator_lock_)
          REQUIRES(!*helper->tag_table_->GetAllowDisallowLock()) {
        if (stop_reports) {
          return;
        }

        art::mirror::Object* trg = src->GetFieldObjectReferenceAddr(field_offset)->AsMirrorPtr();
        jvmtiHeapReferenceInfo reference_info;
        memset(&reference_info, 0, sizeof(reference_info));

        // TODO: Implement spec-compliant numbering.
        reference_info.field.index = field_offset.Int32Value();

        jvmtiHeapReferenceKind kind =
            field_offset.Int32Value() == art::mirror::Object::ClassOffset().Int32Value()
                ? JVMTI_HEAP_REFERENCE_CLASS
                : JVMTI_HEAP_REFERENCE_FIELD;
        const jvmtiHeapReferenceInfo* reference_info_ptr =
            kind == JVMTI_HEAP_REFERENCE_CLASS ? nullptr : &reference_info;

        stop_reports = !helper->ReportReferenceMaybeEnqueue(kind, reference_info_ptr, src, trg);
      }

      void VisitRoot(art::mirror::CompressedReference<art::mirror::Object>* root ATTRIBUTE_UNUSED)
          const {
        LOG(FATAL) << "Unreachable";
      }
      void VisitRootIfNonNull(
          art::mirror::CompressedReference<art::mirror::Object>* root ATTRIBUTE_UNUSED) const {
        LOG(FATAL) << "Unreachable";
      }

      // "mutable" required by the visitor API.
      mutable FollowReferencesHelper* helper;
      mutable bool stop_reports;
    };

    InstanceReferenceVisitor visitor(this);
    // Visit references, not native roots.
    obj->VisitReferences<false>(visitor, art::VoidFunctor());

    stop_reports_ = visitor.stop_reports;
  }

  void VisitArray(art::mirror::Object* array)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_CLASS,
                                                 nullptr,
                                                 array,
                                                 array->GetClass());
    if (stop_reports_) {
      return;
    }

    if (array->IsObjectArray()) {
      art::mirror::ObjectArray<art::mirror::Object>* obj_array =
          array->AsObjectArray<art::mirror::Object>();
      int32_t length = obj_array->GetLength();
      for (int32_t i = 0; i != length; ++i) {
        art::mirror::Object* elem = obj_array->GetWithoutChecks(i);
        if (elem != nullptr) {
          jvmtiHeapReferenceInfo reference_info;
          reference_info.array.index = i;
          stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT,
                                                       &reference_info,
                                                       array,
                                                       elem);
          if (stop_reports_) {
            break;
          }
        }
      }
    }
  }

  void VisitClass(art::mirror::Class* klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    // TODO: Are erroneous classes reported? Are non-prepared ones? For now, just use resolved ones.
    if (!klass->IsResolved()) {
      return;
    }

    // Superclass.
    stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_SUPERCLASS,
                                                 nullptr,
                                                 klass,
                                                 klass->GetSuperClass());
    if (stop_reports_) {
      return;
    }

    // Directly implemented or extended interfaces.
    art::Thread* self = art::Thread::Current();
    art::StackHandleScope<1> hs(self);
    art::Handle<art::mirror::Class> h_klass(hs.NewHandle<art::mirror::Class>(klass));
    for (size_t i = 0; i < h_klass->NumDirectInterfaces(); ++i) {
      art::ObjPtr<art::mirror::Class> inf_klass =
          art::mirror::Class::ResolveDirectInterface(self, h_klass, i);
      if (inf_klass == nullptr) {
        // TODO: With a resolved class this should not happen...
        self->ClearException();
        break;
      }

      stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_INTERFACE,
                                                   nullptr,
                                                   klass,
                                                   inf_klass.Ptr());
      if (stop_reports_) {
        return;
      }
    }

    // Classloader.
    // TODO: What about the boot classpath loader? We'll skip for now, but do we have to find the
    //       fake BootClassLoader?
    if (klass->GetClassLoader() != nullptr) {
      stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_CLASS_LOADER,
                                                   nullptr,
                                                   klass,
                                                   klass->GetClassLoader());
      if (stop_reports_) {
        return;
      }
    }
    DCHECK_EQ(h_klass.Get(), klass);

    // Declared static fields.
    for (auto& field : klass->GetSFields()) {
      if (!field.IsPrimitiveType()) {
        art::ObjPtr<art::mirror::Object> field_value = field.GetObject(klass);
        if (field_value != nullptr) {
          jvmtiHeapReferenceInfo reference_info;
          memset(&reference_info, 0, sizeof(reference_info));

          // TODO: Implement spec-compliant numbering.
          reference_info.field.index = field.GetOffset().Int32Value();

          stop_reports_ = !ReportReferenceMaybeEnqueue(JVMTI_HEAP_REFERENCE_STATIC_FIELD,
                                                       &reference_info,
                                                       klass,
                                                       field_value.Ptr());
          if (stop_reports_) {
            return;
          }
        }
      }
    }
  }

  void MaybeEnqueue(art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (visited_.find(obj) == visited_.end()) {
      worklist_.push_back(obj);
      visited_.insert(obj);
    }
  }

  bool ReportReferenceMaybeEnqueue(jvmtiHeapReferenceKind kind,
                                   const jvmtiHeapReferenceInfo* reference_info,
                                   art::mirror::Object* referree,
                                   art::mirror::Object* referrer)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    jint result = ReportReference(kind, reference_info, referree, referrer);
    if ((result & JVMTI_VISIT_ABORT) == 0) {
      if ((result & JVMTI_VISIT_OBJECTS) != 0) {
        MaybeEnqueue(referrer);
      }
      return true;
    } else {
      return false;
    }
  }

  jint ReportReference(jvmtiHeapReferenceKind kind,
                       const jvmtiHeapReferenceInfo* reference_info,
                       art::mirror::Object* referrer,
                       art::mirror::Object* referree)
      REQUIRES_SHARED(art::Locks::mutator_lock_)
      REQUIRES(!*tag_table_->GetAllowDisallowLock()) {
    if (referree == nullptr || stop_reports_) {
      return 0;
    }

    const jlong class_tag = tag_table_->GetTagOrZero(referree->GetClass());
    const jlong referrer_class_tag =
        referrer == nullptr ? 0 : tag_table_->GetTagOrZero(referrer->GetClass());
    const jlong size = static_cast<jlong>(referree->SizeOf());
    jlong tag = tag_table_->GetTagOrZero(referree);
    jlong saved_tag = tag;
    jlong referrer_tag = 0;
    jlong saved_referrer_tag = 0;
    jlong* referrer_tag_ptr;
    if (referrer == nullptr) {
      referrer_tag_ptr = nullptr;
    } else {
      if (referrer == referree) {
        referrer_tag_ptr = &tag;
      } else {
        referrer_tag = saved_referrer_tag = tag_table_->GetTagOrZero(referrer);
        referrer_tag_ptr = &referrer_tag;
      }
    }
    jint length = -1;
    if (referree->IsArrayInstance()) {
      length = referree->AsArray()->GetLength();
    }

    jint result = callbacks_->heap_reference_callback(kind,
                                                      reference_info,
                                                      class_tag,
                                                      referrer_class_tag,
                                                      size,
                                                      &tag,
                                                      referrer_tag_ptr,
                                                      length,
                                                      const_cast<void*>(user_data_));

    if (tag != saved_tag) {
      tag_table_->Set(referree, tag);
    }
    if (referrer_tag != saved_referrer_tag) {
      tag_table_->Set(referrer, referrer_tag);
    }

    return result;
  }

  ObjectTagTable* tag_table_;
  art::ObjPtr<art::mirror::Object> initial_object_;
  const jvmtiHeapCallbacks* callbacks_;
  const void* user_data_;

  std::vector<art::mirror::Object*> worklist_;
  size_t start_;
  static constexpr size_t kMaxStart = 1000000U;

  std::unordered_set<art::mirror::Object*> visited_;

  bool stop_reports_;

  friend class CollectAndReportRootsVisitor;
};

jvmtiError HeapUtil::FollowReferences(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                      jint heap_filter ATTRIBUTE_UNUSED,
                                      jclass klass ATTRIBUTE_UNUSED,
                                      jobject initial_object,
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

  art::gc::Heap* heap = art::Runtime::Current()->GetHeap();
  if (heap->IsGcConcurrentAndMoving()) {
    // Need to take a heap dump while GC isn't running. See the
    // comment in Heap::VisitObjects().
    heap->IncrementDisableMovingGC(self);
  }
  {
    art::ScopedObjectAccess soa(self);      // Now we know we have the shared lock.
    art::ScopedThreadSuspension sts(self, art::kWaitingForVisitObjects);
    art::ScopedSuspendAll ssa("FollowReferences");

    FollowReferencesHelper frh(this,
                               self->DecodeJObject(initial_object),
                               callbacks,
                               user_data);
    frh.Init();
    frh.Work();
  }
  if (heap->IsGcConcurrentAndMoving()) {
    heap->DecrementDisableMovingGC(self);
  }

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

    bool operator()(art::ObjPtr<art::mirror::Class> klass)
        OVERRIDE REQUIRES_SHARED(art::Locks::mutator_lock_) {
      classes_.push_back(self_->GetJniEnv()->AddLocalReference<jclass>(klass));
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

jvmtiError HeapUtil::ForceGarbageCollection(jvmtiEnv* env ATTRIBUTE_UNUSED) {
  art::Runtime::Current()->GetHeap()->CollectGarbage(false);

  return ERR(NONE);
}
}  // namespace openjdkjvmti
