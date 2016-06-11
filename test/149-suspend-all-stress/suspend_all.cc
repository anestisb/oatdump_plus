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

#include "jni.h"
#include "runtime.h"
#include "thread_list.h"

namespace art {

extern "C" JNIEXPORT void JNICALL Java_Main_suspendAndResume(JNIEnv*, jclass) {
  static constexpr size_t kInitialSleepUS = 100 * 1000;  // 100ms.
  static constexpr size_t kIterations = 500;
  usleep(kInitialSleepUS);  // Leave some time for threads to get in here before we start suspending.
  enum Operation {
    kOPSuspendAll,
    kOPDumpStack,
    kOPSuspendAllDumpStack,
    // Total number of operations.
    kOPNumber,
  };
  for (size_t i = 0; i < kIterations; ++i) {
    switch (static_cast<Operation>(i % kOPNumber)) {
      case kOPSuspendAll: {
        ScopedSuspendAll ssa(__FUNCTION__);
        usleep(500);
        break;
      }
      case kOPDumpStack: {
        Runtime::Current()->GetThreadList()->Dump(LOG(INFO));
        usleep(500);
        break;
      }
      case kOPSuspendAllDumpStack: {
        // Not yet supported.
        // ScopedSuspendAll ssa(__FUNCTION__);
        // Runtime::Current()->GetThreadList()->Dump(LOG(INFO));
        break;
      }
      case kOPNumber:
        break;
    }
  }
}

}  // namespace art
