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

#ifndef ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_
#define ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_

#include <memory>

#include <jni.h>

#include "base/casts.h"
#include "base/logging.h"
#include "base/macros.h"
#include "events.h"
#include "java_vm_ext.h"
#include "jni_env_ext.h"
#include "jvmti.h"

namespace openjdkjvmti {

extern const jvmtiInterface_1 gJvmtiInterface;
extern EventHandler gEventHandler;

// A structure that is a jvmtiEnv with additional information for the runtime.
struct ArtJvmTiEnv : public jvmtiEnv {
  art::JavaVMExt* art_vm;
  void* local_data;
  jvmtiCapabilities capabilities;

  EventMasks event_masks;
  std::unique_ptr<jvmtiEventCallbacks> event_callbacks;

  explicit ArtJvmTiEnv(art::JavaVMExt* runtime)
      : art_vm(runtime), local_data(nullptr), capabilities() {
    functions = &gJvmtiInterface;
  }

  static ArtJvmTiEnv* AsArtJvmTiEnv(jvmtiEnv* env) {
    return art::down_cast<ArtJvmTiEnv*>(env);
  }
};

// Macro and constexpr to make error values less annoying to write.
#define ERR(e) JVMTI_ERROR_ ## e
static constexpr jvmtiError OK = JVMTI_ERROR_NONE;

// Special error code for unimplemented functions in JVMTI
static constexpr jvmtiError ERR(NOT_IMPLEMENTED) = JVMTI_ERROR_NOT_AVAILABLE;

static inline JNIEnv* GetJniEnv(jvmtiEnv* env) {
  JNIEnv* ret_value = nullptr;
  jint res = reinterpret_cast<ArtJvmTiEnv*>(env)->art_vm->GetEnv(
      reinterpret_cast<void**>(&ret_value), JNI_VERSION_1_1);
  if (res != JNI_OK) {
    return nullptr;
  }
  return ret_value;
}

class JvmtiDeleter {
 public:
  JvmtiDeleter() : env_(nullptr) {}
  explicit JvmtiDeleter(jvmtiEnv* env) : env_(env) {}

  JvmtiDeleter(JvmtiDeleter&) = default;
  JvmtiDeleter(JvmtiDeleter&&) = default;
  JvmtiDeleter& operator=(const JvmtiDeleter&) = default;

  void operator()(unsigned char* ptr) const {
    CHECK(env_ != nullptr);
    jvmtiError ret = env_->Deallocate(ptr);
    CHECK(ret == ERR(NONE));
  }

 private:
  mutable jvmtiEnv* env_;
};

using JvmtiUniquePtr = std::unique_ptr<unsigned char, JvmtiDeleter>;

template <typename T>
ALWAYS_INLINE
static inline JvmtiUniquePtr MakeJvmtiUniquePtr(jvmtiEnv* env, T* mem) {
  return JvmtiUniquePtr(reinterpret_cast<unsigned char*>(mem), JvmtiDeleter(env));
}

ALWAYS_INLINE
static inline jvmtiError CopyString(jvmtiEnv* env, const char* src, unsigned char** copy) {
  size_t len = strlen(src) + 1;
  unsigned char* buf;
  jvmtiError ret = env->Allocate(len, &buf);
  if (ret != ERR(NONE)) {
    return ret;
  }
  strcpy(reinterpret_cast<char*>(buf), src);
  *copy = buf;
  return ret;
}

struct ArtClassDefinition {
  jclass klass;
  jobject loader;
  std::string name;
  jobject protection_domain;
  jint dex_len;
  JvmtiUniquePtr dex_data;
  bool modified;

  ArtClassDefinition() = default;
  ArtClassDefinition(ArtClassDefinition&& o) = default;

  void SetNewDexData(ArtJvmTiEnv* env, jint new_dex_len, unsigned char* new_dex_data) {
    if (new_dex_data == nullptr) {
      return;
    } else if (new_dex_data != dex_data.get() || new_dex_len != dex_len) {
      modified = true;
      dex_len = new_dex_len;
      dex_data = MakeJvmtiUniquePtr(env, new_dex_data);
    }
  }
};

const jvmtiCapabilities kPotentialCapabilities = {
    .can_tag_objects                                 = 1,
    .can_generate_field_modification_events          = 0,
    .can_generate_field_access_events                = 0,
    .can_get_bytecodes                               = 0,
    .can_get_synthetic_attribute                     = 0,
    .can_get_owned_monitor_info                      = 0,
    .can_get_current_contended_monitor               = 0,
    .can_get_monitor_info                            = 0,
    .can_pop_frame                                   = 0,
    .can_redefine_classes                            = 1,
    .can_signal_thread                               = 0,
    .can_get_source_file_name                        = 0,
    .can_get_line_numbers                            = 0,
    .can_get_source_debug_extension                  = 0,
    .can_access_local_variables                      = 0,
    .can_maintain_original_method_order              = 0,
    .can_generate_single_step_events                 = 0,
    .can_generate_exception_events                   = 0,
    .can_generate_frame_pop_events                   = 0,
    .can_generate_breakpoint_events                  = 0,
    .can_suspend                                     = 0,
    .can_redefine_any_class                          = 0,
    .can_get_current_thread_cpu_time                 = 0,
    .can_get_thread_cpu_time                         = 0,
    .can_generate_method_entry_events                = 0,
    .can_generate_method_exit_events                 = 0,
    .can_generate_all_class_hook_events              = 0,
    .can_generate_compiled_method_load_events        = 0,
    .can_generate_monitor_events                     = 0,
    .can_generate_vm_object_alloc_events             = 0,
    .can_generate_native_method_bind_events          = 0,
    .can_generate_garbage_collection_events          = 0,
    .can_generate_object_free_events                 = 0,
    .can_force_early_return                          = 0,
    .can_get_owned_monitor_stack_depth_info          = 0,
    .can_get_constant_pool                           = 0,
    .can_set_native_method_prefix                    = 0,
    .can_retransform_classes                         = 1,
    .can_retransform_any_class                       = 0,
    .can_generate_resource_exhaustion_heap_events    = 0,
    .can_generate_resource_exhaustion_threads_events = 0,
};

}  // namespace openjdkjvmti

#endif  // ART_RUNTIME_OPENJDKJVMTI_ART_JVMTI_H_
