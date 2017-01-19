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

#include "thread.h"

namespace art {

void RuntimeCallbacks::AddThreadLifecycleCallback(ThreadLifecycleCallback* cb) {
  thread_callbacks_.push_back(cb);
}

void RuntimeCallbacks::RemoveThreadLifecycleCallback(ThreadLifecycleCallback* cb) {
  auto it = std::find(thread_callbacks_.begin(), thread_callbacks_.end(), cb);
  if (it != thread_callbacks_.end()) {
    thread_callbacks_.erase(it);
  }
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

}  // namespace art
