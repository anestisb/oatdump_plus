/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "ti_stack.h"

#include "art_jvmti.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "dex_file.h"
#include "dex_file_annotations.h"
#include "jni_env_ext.h"
#include "jni_internal.h"
#include "mirror/class.h"
#include "mirror/dex_cache.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread.h"
#include "thread_pool.h"

namespace openjdkjvmti {

struct GetStackTraceVisitor : public art::StackVisitor {
  GetStackTraceVisitor(art::Thread* thread_in,
                       art::ScopedObjectAccessAlreadyRunnable& soa_,
                       size_t start_,
                       size_t stop_)
      : StackVisitor(thread_in, nullptr, StackVisitor::StackWalkKind::kIncludeInlinedFrames),
        soa(soa_),
        start(start_),
        stop(stop_) {}

  bool VisitFrame() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::ArtMethod* m = GetMethod();
    if (m->IsRuntimeMethod()) {
      return true;
    }

    if (start == 0) {
      m = m->GetInterfaceMethodIfProxy(art::kRuntimePointerSize);
      jmethodID id = art::jni::EncodeArtMethod(m);

      uint32_t dex_pc = GetDexPc(false);
      jlong dex_location = (dex_pc == art::DexFile::kDexNoIndex) ? -1 : static_cast<jlong>(dex_pc);

      jvmtiFrameInfo info = { id, dex_location };
      frames.push_back(info);

      if (stop == 1) {
        return false;  // We're done.
      } else if (stop > 0) {
        stop--;
      }
    } else {
      start--;
    }

    return true;
  }

  art::ScopedObjectAccessAlreadyRunnable& soa;
  std::vector<jvmtiFrameInfo> frames;
  size_t start;
  size_t stop;
};

struct GetStackTraceClosure : public art::Closure {
 public:
  GetStackTraceClosure(size_t start, size_t stop)
      : start_input(start),
        stop_input(stop),
        start_result(0),
        stop_result(0) {}

  void Run(art::Thread* self) OVERRIDE {
    art::ScopedObjectAccess soa(art::Thread::Current());

    GetStackTraceVisitor visitor(self, soa, start_input, stop_input);
    visitor.WalkStack(false);

    frames.swap(visitor.frames);
    start_result = visitor.start;
    stop_result = visitor.stop;
  }

  const size_t start_input;
  const size_t stop_input;

  std::vector<jvmtiFrameInfo> frames;
  size_t start_result;
  size_t stop_result;
};

jvmtiError StackUtil::GetStackTrace(jvmtiEnv* jvmti_env ATTRIBUTE_UNUSED,
                                    jthread java_thread,
                                    jint start_depth,
                                    jint max_frame_count,
                                    jvmtiFrameInfo* frame_buffer,
                                    jint* count_ptr) {
  if (java_thread == nullptr) {
    return ERR(INVALID_THREAD);
  }

  art::Thread* thread;
  {
    // TODO: Need non-aborting call here, to return JVMTI_ERROR_INVALID_THREAD.
    art::ScopedObjectAccess soa(art::Thread::Current());
    art::MutexLock mu(soa.Self(), *art::Locks::thread_list_lock_);
    thread = art::Thread::FromManagedThread(soa, java_thread);
    DCHECK(thread != nullptr);
  }

  art::ThreadState state = thread->GetState();
  if (state == art::ThreadState::kStarting ||
      state == art::ThreadState::kTerminated ||
      thread->IsStillStarting()) {
    return ERR(THREAD_NOT_ALIVE);
  }

  if (max_frame_count < 0) {
    return ERR(ILLEGAL_ARGUMENT);
  }
  if (frame_buffer == nullptr || count_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  if (max_frame_count == 0) {
    *count_ptr = 0;
    return ERR(NONE);
  }

  GetStackTraceClosure closure(start_depth >= 0 ? static_cast<size_t>(start_depth) : 0,
                               start_depth >= 0 ?static_cast<size_t>(max_frame_count) : 0);
  thread->RequestSynchronousCheckpoint(&closure);

  size_t collected_frames = closure.frames.size();

  // Frames from the top.
  if (start_depth >= 0) {
    if (closure.start_result != 0) {
      // Not enough frames.
      return ERR(ILLEGAL_ARGUMENT);
    }
    DCHECK_LE(collected_frames, static_cast<size_t>(max_frame_count));
    if (closure.frames.size() > 0) {
      memcpy(frame_buffer, closure.frames.data(), collected_frames * sizeof(jvmtiFrameInfo));
    }
    *count_ptr = static_cast<jint>(closure.frames.size());
    return ERR(NONE);
  }

  // Frames from the bottom.
  if (collected_frames < static_cast<size_t>(-start_depth)) {
    return ERR(ILLEGAL_ARGUMENT);
  }

  size_t count = std::min(static_cast<size_t>(-start_depth), static_cast<size_t>(max_frame_count));
  memcpy(frame_buffer,
         &closure.frames.data()[collected_frames + start_depth],
         count * sizeof(jvmtiFrameInfo));
  *count_ptr = static_cast<jint>(count);
  return ERR(NONE);
}

}  // namespace openjdkjvmti
