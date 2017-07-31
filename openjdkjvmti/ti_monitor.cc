/* Copyright (C) 2017 The Android Open Source Project
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

#include "ti_monitor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "art_jvmti.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace openjdkjvmti {

// We cannot use ART monitors, as they require the mutator lock for contention locking. We
// also cannot use pthread mutexes and condition variables (or C++11 abstractions) directly,
// as the do not have the right semantics for recursive mutexes and waiting (wait only unlocks
// the mutex once).
// So go ahead and use a wrapper that does the counting explicitly.

class JvmtiMonitor {
 public:
  JvmtiMonitor() : owner_(nullptr), count_(0) {
  }

  static bool Destroy(art::Thread* self, JvmtiMonitor* monitor) NO_THREAD_SAFETY_ANALYSIS {
    // Check whether this thread holds the monitor, or nobody does.
    art::Thread* owner_thread = monitor->owner_.load(std::memory_order_relaxed);
    if (owner_thread != nullptr && self != owner_thread) {
      return false;
    }

    if (monitor->count_ > 0) {
      monitor->count_ = 0;
      monitor->owner_.store(nullptr, std::memory_order_relaxed);
      monitor->mutex_.unlock();
    }

    delete monitor;
    return true;
  }

  void MonitorEnter(art::Thread* self) NO_THREAD_SAFETY_ANALYSIS {
    // Check for recursive enter.
    if (IsOwner(self)) {
      count_++;
      return;
    }

    mutex_.lock();

    DCHECK(owner_.load(std::memory_order_relaxed) == nullptr);
    owner_.store(self, std::memory_order_relaxed);
    DCHECK_EQ(0u, count_);
    count_ = 1;
  }

  bool MonitorExit(art::Thread* self) NO_THREAD_SAFETY_ANALYSIS {
    if (!IsOwner(self)) {
      return false;
    }

    --count_;
    if (count_ == 0u) {
      owner_.store(nullptr, std::memory_order_relaxed);
      mutex_.unlock();
    }

    return true;
  }

  bool Wait(art::Thread* self) {
    auto wait_without_timeout = [&](std::unique_lock<std::mutex>& lk) {
      cond_.wait(lk);
    };
    return Wait(self, wait_without_timeout);
  }

  bool Wait(art::Thread* self, uint64_t timeout_in_ms) {
    auto wait_with_timeout = [&](std::unique_lock<std::mutex>& lk) {
      cond_.wait_for(lk, std::chrono::milliseconds(timeout_in_ms));
    };
    return Wait(self, wait_with_timeout);
  }

  bool Notify(art::Thread* self) {
    return Notify(self, [&]() { cond_.notify_one(); });
  }

  bool NotifyAll(art::Thread* self) {
    return Notify(self, [&]() { cond_.notify_all(); });
  }

 private:
  bool IsOwner(art::Thread* self) {
    // There's a subtle correctness argument here for a relaxed load outside the critical section.
    // A thread is guaranteed to see either its own latest store or another thread's store. If a
    // thread sees another thread's store than it cannot be holding the lock.
    art::Thread* owner_thread = owner_.load(std::memory_order_relaxed);
    return self == owner_thread;
  }

  template <typename T>
  bool Wait(art::Thread* self, T how_to_wait) {
    if (!IsOwner(self)) {
      return false;
    }

    size_t old_count = count_;

    count_ = 0;
    owner_.store(nullptr, std::memory_order_relaxed);

    {
      std::unique_lock<std::mutex> lk(mutex_, std::adopt_lock);
      how_to_wait(lk);
      lk.release();  // Do not unlock the mutex.
    }

    DCHECK(owner_.load(std::memory_order_relaxed) == nullptr);
    owner_.store(self, std::memory_order_relaxed);
    DCHECK_EQ(0u, count_);
    count_ = old_count;

    return true;
  }

  template <typename T>
  bool Notify(art::Thread* self, T how_to_notify) {
    if (!IsOwner(self)) {
      return false;
    }

    how_to_notify();

    return true;
  }

  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<art::Thread*> owner_;
  size_t count_;
};

static jrawMonitorID EncodeMonitor(JvmtiMonitor* monitor) {
  return reinterpret_cast<jrawMonitorID>(monitor);
}

static JvmtiMonitor* DecodeMonitor(jrawMonitorID id) {
  return reinterpret_cast<JvmtiMonitor*>(id);
}

jvmtiError MonitorUtil::CreateRawMonitor(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                         const char* name,
                                         jrawMonitorID* monitor_ptr) {
  if (name == nullptr || monitor_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  JvmtiMonitor* monitor = new JvmtiMonitor();
  *monitor_ptr = EncodeMonitor(monitor);

  return ERR(NONE);
}

jvmtiError MonitorUtil::DestroyRawMonitor(jvmtiEnv* env ATTRIBUTE_UNUSED, jrawMonitorID id) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  if (!JvmtiMonitor::Destroy(self, monitor)) {
    return ERR(NOT_MONITOR_OWNER);
  }

  return ERR(NONE);
}

jvmtiError MonitorUtil::RawMonitorEnter(jvmtiEnv* env ATTRIBUTE_UNUSED, jrawMonitorID id) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  monitor->MonitorEnter(self);

  return ERR(NONE);
}

jvmtiError MonitorUtil::RawMonitorExit(jvmtiEnv* env ATTRIBUTE_UNUSED, jrawMonitorID id) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  if (!monitor->MonitorExit(self)) {
    return ERR(NOT_MONITOR_OWNER);
  }

  return ERR(NONE);
}

jvmtiError MonitorUtil::RawMonitorWait(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                       jrawMonitorID id,
                                       jlong millis) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  // This is not in the spec, but it's the only thing that makes sense (and agrees with
  // Object.wait).
  if (millis < 0) {
    return ERR(ILLEGAL_ARGUMENT);
  }

  bool result = (millis > 0)
      ? monitor->Wait(self, static_cast<uint64_t>(millis))
      : monitor->Wait(self);

  if (!result) {
    return ERR(NOT_MONITOR_OWNER);
  }

  // TODO: Make sure that is really what we should be checking here.
  if (self->IsInterrupted()) {
    return ERR(INTERRUPT);
  }

  return ERR(NONE);
}

jvmtiError MonitorUtil::RawMonitorNotify(jvmtiEnv* env ATTRIBUTE_UNUSED, jrawMonitorID id) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  if (!monitor->Notify(self)) {
    return ERR(NOT_MONITOR_OWNER);
  }

  return ERR(NONE);
}

jvmtiError MonitorUtil::RawMonitorNotifyAll(jvmtiEnv* env ATTRIBUTE_UNUSED, jrawMonitorID id) {
  if (id == nullptr) {
    return ERR(INVALID_MONITOR);
  }

  JvmtiMonitor* monitor = DecodeMonitor(id);
  art::Thread* self = art::Thread::Current();

  if (!monitor->NotifyAll(self)) {
    return ERR(NOT_MONITOR_OWNER);
  }

  return ERR(NONE);
}

}  // namespace openjdkjvmti
