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

#include <inttypes.h>
#include <sched.h>

#include "barrier.h"
#include "base/logging.h"
#include "base/macros.h"
#include "jni.h"
#include "jvmti.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "thread-inl.h"
#include "well_known_classes.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test930AgentThread {

struct AgentData {
  AgentData() : main_thread(nullptr),
                jvmti_env(nullptr),
                b(2) {
  }

  jthread main_thread;
  jvmtiEnv* jvmti_env;
  Barrier b;
  jint priority;
};

static void AgentMain(jvmtiEnv* jenv, JNIEnv* env, void* arg) {
  AgentData* data = reinterpret_cast<AgentData*>(arg);

  // Check some basics.
  // This thread is not the main thread.
  jthread this_thread;
  jvmtiError this_thread_result = jenv->GetCurrentThread(&this_thread);
  CHECK(!JvmtiErrorToException(env, this_thread_result));
  CHECK(!env->IsSameObject(this_thread, data->main_thread));

  // The thread is a daemon.
  jvmtiThreadInfo info;
  jvmtiError info_result = jenv->GetThreadInfo(this_thread, &info);
  CHECK(!JvmtiErrorToException(env, info_result));
  CHECK(info.is_daemon);

  // The thread has the requested priority.
  // TODO: Our thread priorities do not work on the host.
  // CHECK_EQ(info.priority, data->priority);

  // Check further parts of the thread:
  jint thread_count;
  jthread* threads;
  jvmtiError threads_result = jenv->GetAllThreads(&thread_count, &threads);
  CHECK(!JvmtiErrorToException(env, threads_result));
  bool found = false;
  for (jint i = 0; i != thread_count; ++i) {
    if (env->IsSameObject(threads[i], this_thread)) {
      found = true;
      break;
    }
  }
  CHECK(found);

  // Done, let the main thread progress.
  data->b.Pass(Thread::Current());
}

extern "C" JNIEXPORT void JNICALL Java_Main_testAgentThread(
    JNIEnv* env, jclass Main_klass ATTRIBUTE_UNUSED) {
  // Create a Thread object.
  ScopedLocalRef<jobject> thread_name(env,
                                      env->NewStringUTF("Agent Thread"));
  if (thread_name.get() == nullptr) {
    return;
  }

  ScopedLocalRef<jobject> thread(env, env->AllocObject(WellKnownClasses::java_lang_Thread));
  if (thread.get() == nullptr) {
    return;
  }

  env->CallNonvirtualVoidMethod(thread.get(),
                                WellKnownClasses::java_lang_Thread,
                                WellKnownClasses::java_lang_Thread_init,
                                Runtime::Current()->GetMainThreadGroup(),
                                thread_name.get(),
                                kMinThreadPriority,
                                JNI_FALSE);
  if (env->ExceptionCheck()) {
    return;
  }

  jthread main_thread;
  jvmtiError main_thread_result = jvmti_env->GetCurrentThread(&main_thread);
  if (JvmtiErrorToException(env, main_thread_result)) {
    return;
  }

  AgentData data;
  data.main_thread = env->NewGlobalRef(main_thread);
  data.jvmti_env = jvmti_env;
  data.priority = JVMTI_THREAD_MIN_PRIORITY;

  jvmtiError result = jvmti_env->RunAgentThread(thread.get(), AgentMain, &data, data.priority);
  if (JvmtiErrorToException(env, result)) {
    return;
  }

  data.b.Wait(Thread::Current());

  // Scheduling may mean that the agent thread is put to sleep. Wait until it's dead in an effort
  // to not unload the plugin and crash.
  for (;;) {
    NanoSleep(1000 * 1000);
    jint thread_state;
    jvmtiError state_result = jvmti_env->GetThreadState(thread.get(), &thread_state);
    if (JvmtiErrorToException(env, state_result)) {
      return;
    }
    if (thread_state == 0 ||                                    // Was never alive.
        (thread_state & JVMTI_THREAD_STATE_TERMINATED) != 0) {  // Was alive and died.
      break;
    }
  }
  // Yield and sleep a bit more, to give the plugin time to tear down the native thread structure.
  sched_yield();
  NanoSleep(100 * 1000 * 1000);

  env->DeleteGlobalRef(data.main_thread);
}

}  // namespace Test930AgentThread
}  // namespace art
