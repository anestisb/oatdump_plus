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

#include <algorithm>

#include "base/macros.h"
#include "class_linker.h"
#include "thread.h"

namespace art {

void RuntimeCallbacks::AddThreadLifecycleCallback(ThreadLifecycleCallback* cb) {
  thread_callbacks_.push_back(cb);
}

template <typename T>
ALWAYS_INLINE
static inline void Remove(T* cb, std::vector<T*>* data) {
  auto it = std::find(data->begin(), data->end(), cb);
  if (it != data->end()) {
    data->erase(it);
  }
}

void RuntimeCallbacks::RemoveThreadLifecycleCallback(ThreadLifecycleCallback* cb) {
  Remove(cb, &thread_callbacks_);
}

void RuntimeCallbacks::ThreadStart(Thread* self) {
  for (ThreadLifecycleCallback* cb : thread_callbacks_) {
    cb->ThreadStart(self);
  }
}

void RuntimeCallbacks::ThreadDeath(Thread* self) {
  for (ThreadLifecycleCallback* cb : thread_callbacks_) {
    cb->ThreadDeath(self);
  }
}

void RuntimeCallbacks::AddClassLoadCallback(ClassLoadCallback* cb) {
  class_callbacks_.push_back(cb);
}

void RuntimeCallbacks::RemoveClassLoadCallback(ClassLoadCallback* cb) {
  Remove(cb, &class_callbacks_);
}

void RuntimeCallbacks::ClassLoad(Handle<mirror::Class> klass) {
  for (ClassLoadCallback* cb : class_callbacks_) {
    cb->ClassLoad(klass);
  }
}

void RuntimeCallbacks::ClassPrepare(Handle<mirror::Class> temp_klass, Handle<mirror::Class> klass) {
  for (ClassLoadCallback* cb : class_callbacks_) {
    cb->ClassPrepare(temp_klass, klass);
  }
}

void RuntimeCallbacks::AddRuntimeSigQuitCallback(RuntimeSigQuitCallback* cb) {
  sigquit_callbacks_.push_back(cb);
}

void RuntimeCallbacks::RemoveRuntimeSigQuitCallback(RuntimeSigQuitCallback* cb) {
  Remove(cb, &sigquit_callbacks_);
}

void RuntimeCallbacks::SigQuit() {
  for (RuntimeSigQuitCallback* cb : sigquit_callbacks_) {
    cb->SigQuit();
  }
}

}  // namespace art
