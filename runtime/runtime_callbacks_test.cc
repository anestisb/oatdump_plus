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

#include "runtime_callbacks.h"

#include "jni.h"
#include <memory>
#include <string>

#include "art_method-inl.h"
#include "base/mutex.h"
#include "mirror/class-inl.h"
#include "common_runtime_test.h"
#include "mem_map.h"
#include "obj_ptr.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "well_known_classes.h"

namespace art {

class RuntimeCallbacksTest : public CommonRuntimeTest {
 protected:
  void SetUp() OVERRIDE {
    CommonRuntimeTest::SetUp();

    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    ScopedThreadSuspension sts(self, kWaitingForDebuggerToAttach);
    ScopedSuspendAll ssa("RuntimeCallbacksTest SetUp");
    AddListener();
  }

  void TearDown() OVERRIDE {
    {
      Thread* self = Thread::Current();
      ScopedObjectAccess soa(self);
      ScopedThreadSuspension sts(self, kWaitingForDebuggerToAttach);
      ScopedSuspendAll ssa("RuntimeCallbacksTest TearDown");
      AddListener();
      RemoveListener();
    }

    CommonRuntimeTest::TearDown();
  }

  virtual void AddListener() REQUIRES(Locks::mutator_lock_) = 0;
  virtual void RemoveListener() REQUIRES(Locks::mutator_lock_) = 0;

  void MakeExecutable(ObjPtr<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_) {
    CHECK(klass != nullptr);
    PointerSize pointer_size = class_linker_->GetImagePointerSize();
    for (auto& m : klass->GetMethods(pointer_size)) {
      if (!m.IsAbstract()) {
        class_linker_->SetEntryPointsToInterpreter(&m);
      }
    }
  }
};

class ThreadLifecycleCallbackRuntimeCallbacksTest : public RuntimeCallbacksTest {
 public:
  static void* PthreadsCallback(void* arg ATTRIBUTE_UNUSED) {
    // Attach.
    Runtime* runtime = Runtime::Current();
    CHECK(runtime->AttachCurrentThread("ThreadLifecycle test thread", true, nullptr, false));

    // Detach.
    runtime->DetachCurrentThread();

    // Die...
    return nullptr;
  }

 protected:
  void AddListener() OVERRIDE REQUIRES(Locks::mutator_lock_) {
    Runtime::Current()->GetRuntimeCallbacks().AddThreadLifecycleCallback(&cb_);
  }
  void RemoveListener() OVERRIDE REQUIRES(Locks::mutator_lock_) {
    Runtime::Current()->GetRuntimeCallbacks().RemoveThreadLifecycleCallback(&cb_);
  }

  enum CallbackState {
    kBase,
    kStarted,
    kDied,
    kWrongStart,
    kWrongDeath,
  };

  struct Callback : public ThreadLifecycleCallback {
    void ThreadStart(Thread* self) OVERRIDE {
      if (state == CallbackState::kBase) {
        state = CallbackState::kStarted;
        stored_self = self;
      } else {
        state = CallbackState::kWrongStart;
      }
    }

    void ThreadDeath(Thread* self) OVERRIDE {
      if (state == CallbackState::kStarted && self == stored_self) {
        state = CallbackState::kDied;
      } else {
        state = CallbackState::kWrongDeath;
      }
    }

    Thread* stored_self;
    CallbackState state = CallbackState::kBase;
  };

  Callback cb_;
};


TEST_F(ThreadLifecycleCallbackRuntimeCallbacksTest, ThreadLifecycleCallbackJava) {
  Thread* self = Thread::Current();

  self->TransitionFromSuspendedToRunnable();
  bool started = runtime_->Start();
  ASSERT_TRUE(started);

  cb_.state = CallbackState::kBase;  // Ignore main thread attach.

  {
    ScopedObjectAccess soa(self);
    MakeExecutable(soa.Decode<mirror::Class>(WellKnownClasses::java_lang_Thread));
  }

  JNIEnv* env = self->GetJniEnv();

  ScopedLocalRef<jobject> thread_name(env,
                                      env->NewStringUTF("ThreadLifecycleCallback test thread"));
  ASSERT_TRUE(thread_name.get() != nullptr);

  ScopedLocalRef<jobject> thread(env, env->AllocObject(WellKnownClasses::java_lang_Thread));
  ASSERT_TRUE(thread.get() != nullptr);

  env->CallNonvirtualVoidMethod(thread.get(),
                                WellKnownClasses::java_lang_Thread,
                                WellKnownClasses::java_lang_Thread_init,
                                runtime_->GetMainThreadGroup(),
                                thread_name.get(),
                                kMinThreadPriority,
                                JNI_FALSE);
  ASSERT_FALSE(env->ExceptionCheck());

  jmethodID start_id = env->GetMethodID(WellKnownClasses::java_lang_Thread, "start", "()V");
  ASSERT_TRUE(start_id != nullptr);

  env->CallVoidMethod(thread.get(), start_id);
  ASSERT_FALSE(env->ExceptionCheck());

  jmethodID join_id = env->GetMethodID(WellKnownClasses::java_lang_Thread, "join", "()V");
  ASSERT_TRUE(join_id != nullptr);

  env->CallVoidMethod(thread.get(), join_id);
  ASSERT_FALSE(env->ExceptionCheck());

  EXPECT_TRUE(cb_.state == CallbackState::kDied) << static_cast<int>(cb_.state);
}

TEST_F(ThreadLifecycleCallbackRuntimeCallbacksTest, ThreadLifecycleCallbackAttach) {
  std::string error_msg;
  std::unique_ptr<MemMap> stack(MemMap::MapAnonymous("ThreadLifecycleCallback Thread",
                                                     nullptr,
                                                     128 * kPageSize,  // Just some small stack.
                                                     PROT_READ | PROT_WRITE,
                                                     false,
                                                     false,
                                                     &error_msg));
  ASSERT_FALSE(stack == nullptr) << error_msg;

  const char* reason = "ThreadLifecycleCallback test thread";
  pthread_attr_t attr;
  CHECK_PTHREAD_CALL(pthread_attr_init, (&attr), reason);
  CHECK_PTHREAD_CALL(pthread_attr_setstack, (&attr, stack->Begin(), stack->Size()), reason);
  pthread_t pthread;
  CHECK_PTHREAD_CALL(pthread_create,
                     (&pthread,
                         &attr,
                         &ThreadLifecycleCallbackRuntimeCallbacksTest::PthreadsCallback,
                         this),
                         reason);
  CHECK_PTHREAD_CALL(pthread_attr_destroy, (&attr), reason);

  CHECK_PTHREAD_CALL(pthread_join, (pthread, nullptr), "ThreadLifecycleCallback test shutdown");

  // Detach is not a ThreadDeath event, so we expect to be in state Started.
  EXPECT_TRUE(cb_.state == CallbackState::kStarted) << static_cast<int>(cb_.state);
}

}  // namespace art
