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
  usleep(100 * 1000);  // Leave some time for threads to get in here before we start suspending.
  for (size_t i = 0; i < 500; ++i) {
    Runtime::Current()->GetThreadList()->SuspendAll(__FUNCTION__);
    usleep(500);
    Runtime::Current()->GetThreadList()->ResumeAll();
  }
}

}  // namespace art
