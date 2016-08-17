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

#include <jni.h>
#include "openjdkjvmti/jvmti.h"

#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "scoped_thread_state_change.h"
#include "thread_list.h"

// TODO Remove this at some point by annotating all the methods. It was put in to make the skeleton
// easier to create.
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace openjdkjvmti {

extern const jvmtiInterface_1 gJvmtiInterface;

// A structure that is a jvmtiEnv with additional information for the runtime.
struct ArtJvmTiEnv : public jvmtiEnv {
  art::JavaVMExt* art_vm;
  void* local_data;

  explicit ArtJvmTiEnv(art::JavaVMExt* runtime) : art_vm(runtime), local_data(nullptr) {
    functions = &gJvmtiInterface;
  }
};

// Macro and constexpr to make error values less annoying to write.
#define ERR(e) JVMTI_ERROR_ ## e
static constexpr jvmtiError OK = JVMTI_ERROR_NONE;

// Special error code for unimplemented functions in JVMTI
static constexpr jvmtiError ERR(NOT_IMPLEMENTED) = JVMTI_ERROR_NOT_AVAILABLE;

class JvmtiFunctions {
 private:
  static bool IsValidEnv(jvmtiEnv* env) {
    return env != nullptr;
  }

 public:
  static jvmtiError Allocate(jvmtiEnv* env, jlong size, unsigned char** mem_ptr) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    if (mem_ptr == nullptr) {
      return ERR(NULL_POINTER);
    }
    if (size < 0) {
      return ERR(ILLEGAL_ARGUMENT);
    } else if (size == 0) {
      *mem_ptr = nullptr;
      return OK;
    }
    *mem_ptr = static_cast<unsigned char*>(malloc(size));
    return (*mem_ptr != nullptr) ? OK : ERR(OUT_OF_MEMORY);
  }

  static jvmtiError Deallocate(jvmtiEnv* env, unsigned char* mem) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    if (mem != nullptr) {
      free(mem);
    }
    return OK;
  }

  static jvmtiError GetThreadState(jvmtiEnv* env, jthread thread, jint* thread_state_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetCurrentThread(jvmtiEnv* env, jthread* thread_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetAllThreads(jvmtiEnv* env, jint* threads_count_ptr, jthread** threads_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SuspendThread(jvmtiEnv* env, jthread thread) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SuspendThreadList(jvmtiEnv* env,
                                      jint request_count,
                                      const jthread* request_list,
                                      jvmtiError* results) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ResumeThread(jvmtiEnv* env, jthread thread) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ResumeThreadList(jvmtiEnv* env,
                                     jint request_count,
                                     const jthread* request_list,
                                     jvmtiError* results) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError StopThread(jvmtiEnv* env, jthread thread, jobject exception) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError InterruptThread(jvmtiEnv* env, jthread thread) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadInfo(jvmtiEnv* env, jthread thread, jvmtiThreadInfo* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetOwnedMonitorInfo(jvmtiEnv* env,
                                        jthread thread,
                                        jint* owned_monitor_count_ptr,
                                        jobject** owned_monitors_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetOwnedMonitorStackDepthInfo(jvmtiEnv* env,
                                                  jthread thread,
                                                  jint* monitor_info_count_ptr,
                                                  jvmtiMonitorStackDepthInfo** monitor_info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetCurrentContendedMonitor(jvmtiEnv* env,
                                               jthread thread,
                                               jobject* monitor_ptr) {
  return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RunAgentThread(jvmtiEnv* env,
                                   jthread thread,
                                   jvmtiStartFunction proc,
                                   const void* arg,
                                   jint priority) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetThreadLocalStorage(jvmtiEnv* env, jthread thread, const void* data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadLocalStorage(jvmtiEnv* env, jthread thread, void** data_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetTopThreadGroups(jvmtiEnv* env,
                                       jint* group_count_ptr,
                                       jthreadGroup** groups_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadGroupInfo(jvmtiEnv* env,
                                       jthreadGroup group,
                                       jvmtiThreadGroupInfo* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadGroupChildren(jvmtiEnv* env,
                                           jthreadGroup group,
                                           jint* thread_count_ptr,
                                           jthread** threads_ptr,
                                           jint* group_count_ptr,
                                           jthreadGroup** groups_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetStackTrace(jvmtiEnv* env,
                                  jthread thread,
                                  jint start_depth,
                                  jint max_frame_count,
                                  jvmtiFrameInfo* frame_buffer,
                                  jint* count_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetAllStackTraces(jvmtiEnv* env,
                                      jint max_frame_count,
                                      jvmtiStackInfo** stack_info_ptr,
                                      jint* thread_count_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadListStackTraces(jvmtiEnv* env,
                                             jint thread_count,
                                             const jthread* thread_list,
                                             jint max_frame_count,
                                             jvmtiStackInfo** stack_info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetFrameCount(jvmtiEnv* env, jthread thread, jint* count_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError PopFrame(jvmtiEnv* env, jthread thread) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetFrameLocation(jvmtiEnv* env,
                                     jthread thread,
                                     jint depth,
                                     jmethodID* method_ptr,
                                     jlocation* location_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError NotifyFramePop(jvmtiEnv* env, jthread thread, jint depth) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnObject(jvmtiEnv* env, jthread thread, jobject value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnInt(jvmtiEnv* env, jthread thread, jint value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnLong(jvmtiEnv* env, jthread thread, jlong value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnFloat(jvmtiEnv* env, jthread thread, jfloat value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnDouble(jvmtiEnv* env, jthread thread, jdouble value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceEarlyReturnVoid(jvmtiEnv* env, jthread thread) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError FollowReferences(jvmtiEnv* env,
                                     jint heap_filter,
                                     jclass klass,
                                     jobject initial_object,
                                     const jvmtiHeapCallbacks* callbacks,
                                     const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IterateThroughHeap(jvmtiEnv* env,
                                       jint heap_filter,
                                       jclass klass,
                                       const jvmtiHeapCallbacks* callbacks,
                                       const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetTag(jvmtiEnv* env, jobject object, jlong* tag_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetTag(jvmtiEnv* env, jobject object, jlong tag) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetObjectsWithTags(jvmtiEnv* env,
                                       jint tag_count,
                                       const jlong* tags,
                                       jint* count_ptr,
                                       jobject** object_result_ptr,
                                       jlong** tag_result_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ForceGarbageCollection(jvmtiEnv* env) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IterateOverObjectsReachableFromObject(
      jvmtiEnv* env,
      jobject object,
      jvmtiObjectReferenceCallback object_reference_callback,
      const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IterateOverReachableObjects(jvmtiEnv* env,
                                                jvmtiHeapRootCallback heap_root_callback,
                                                jvmtiStackReferenceCallback stack_ref_callback,
                                                jvmtiObjectReferenceCallback object_ref_callback,
                                                const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IterateOverHeap(jvmtiEnv* env,
                                    jvmtiHeapObjectFilter object_filter,
                                    jvmtiHeapObjectCallback heap_object_callback,
                                    const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IterateOverInstancesOfClass(jvmtiEnv* env,
                                                jclass klass,
                                                jvmtiHeapObjectFilter object_filter,
                                                jvmtiHeapObjectCallback heap_object_callback,
                                                const void* user_data) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalObject(jvmtiEnv* env,
                                   jthread thread,
                                   jint depth,
                                   jint slot,
                                   jobject* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalInstance(jvmtiEnv* env,
                                     jthread thread,
                                     jint depth,
                                     jobject* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalInt(jvmtiEnv* env,
                                jthread thread,
                                jint depth,
                                jint slot,
                                jint* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalLong(jvmtiEnv* env,
                                 jthread thread,
                                 jint depth,
                                 jint slot,
                                 jlong* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalFloat(jvmtiEnv* env,
                                  jthread thread,
                                  jint depth,
                                  jint slot,
                                  jfloat* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalDouble(jvmtiEnv* env,
                                   jthread thread,
                                   jint depth,
                                   jint slot,
                                   jdouble* value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetLocalObject(jvmtiEnv* env,
                                   jthread thread,
                                   jint depth,
                                   jint slot,
                                   jobject value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetLocalInt(jvmtiEnv* env,
                                jthread thread,
                                jint depth,
                                jint slot,
                                jint value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetLocalLong(jvmtiEnv* env,
                                 jthread thread,
                                 jint depth,
                                 jint slot,
                                 jlong value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetLocalFloat(jvmtiEnv* env,
                                  jthread thread,
                                  jint depth,
                                  jint slot,
                                  jfloat value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetLocalDouble(jvmtiEnv* env,
                                   jthread thread,
                                   jint depth,
                                   jint slot,
                                   jdouble value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetBreakpoint(jvmtiEnv* env, jmethodID method, jlocation location) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ClearBreakpoint(jvmtiEnv* env, jmethodID method, jlocation location) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetFieldAccessWatch(jvmtiEnv* env, jclass klass, jfieldID field) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ClearFieldAccessWatch(jvmtiEnv* env, jclass klass, jfieldID field) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetFieldModificationWatch(jvmtiEnv* env, jclass klass, jfieldID field) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError ClearFieldModificationWatch(jvmtiEnv* env, jclass klass, jfieldID field) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLoadedClasses(jvmtiEnv* env, jint* class_count_ptr, jclass** classes_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassLoaderClasses(jvmtiEnv* env,
                                          jobject initiating_loader,
                                          jint* class_count_ptr,
                                          jclass** classes_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassSignature(jvmtiEnv* env,
                                      jclass klass,
                                      char** signature_ptr,
                                      char** generic_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassStatus(jvmtiEnv* env, jclass klass, jint* status_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetSourceFileName(jvmtiEnv* env, jclass klass, char** source_name_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassModifiers(jvmtiEnv* env, jclass klass, jint* modifiers_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassMethods(jvmtiEnv* env,
                                    jclass klass,
                                    jint* method_count_ptr,
                                    jmethodID** methods_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassFields(jvmtiEnv* env,
                                   jclass klass,
                                   jint* field_count_ptr,
                                   jfieldID** fields_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetImplementedInterfaces(jvmtiEnv* env,
                                             jclass klass,
                                             jint* interface_count_ptr,
                                             jclass** interfaces_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassVersionNumbers(jvmtiEnv* env,
                                           jclass klass,
                                           jint* minor_version_ptr,
                                           jint* major_version_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetConstantPool(jvmtiEnv* env,
                                    jclass klass,
                                    jint* constant_pool_count_ptr,
                                    jint* constant_pool_byte_count_ptr,
                                    unsigned char** constant_pool_bytes_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsInterface(jvmtiEnv* env, jclass klass, jboolean* is_interface_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsArrayClass(jvmtiEnv* env,
                                 jclass klass,
                                 jboolean* is_array_class_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsModifiableClass(jvmtiEnv* env,
                                      jclass klass,
                                      jboolean* is_modifiable_class_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetClassLoader(jvmtiEnv* env, jclass klass, jobject* classloader_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetSourceDebugExtension(jvmtiEnv* env,
                                            jclass klass,
                                            char** source_debug_extension_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RetransformClasses(jvmtiEnv* env, jint class_count, const jclass* classes) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RedefineClasses(jvmtiEnv* env,
                                    jint class_count,
                                    const jvmtiClassDefinition* class_definitions) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetObjectSize(jvmtiEnv* env, jobject object, jlong* size_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetObjectHashCode(jvmtiEnv* env, jobject object, jint* hash_code_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetObjectMonitorUsage(jvmtiEnv* env,
                                          jobject object,
                                          jvmtiMonitorUsage* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetFieldName(jvmtiEnv* env,
                                 jclass klass,
                                 jfieldID field,
                                 char** name_ptr,
                                 char** signature_ptr,
                                 char** generic_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetFieldDeclaringClass(jvmtiEnv* env,
                                           jclass klass,
                                           jfieldID field,
                                           jclass* declaring_class_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetFieldModifiers(jvmtiEnv* env,
                                      jclass klass,
                                      jfieldID field,
                                      jint* modifiers_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsFieldSynthetic(jvmtiEnv* env,
                                     jclass klass,
                                     jfieldID field,
                                     jboolean* is_synthetic_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetMethodName(jvmtiEnv* env,
                                  jmethodID method,
                                  char** name_ptr,
                                  char** signature_ptr,
                                  char** generic_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetMethodDeclaringClass(jvmtiEnv* env,
                                            jmethodID method,
                                            jclass* declaring_class_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetMethodModifiers(jvmtiEnv* env,
                                       jmethodID method,
                                       jint* modifiers_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetMaxLocals(jvmtiEnv* env,
                                 jmethodID method,
                                 jint* max_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetArgumentsSize(jvmtiEnv* env,
                                     jmethodID method,
                                     jint* size_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLineNumberTable(jvmtiEnv* env,
                                       jmethodID method,
                                       jint* entry_count_ptr,
                                       jvmtiLineNumberEntry** table_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetMethodLocation(jvmtiEnv* env,
                                      jmethodID method,
                                      jlocation* start_location_ptr,
                                      jlocation* end_location_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetLocalVariableTable(jvmtiEnv* env,
                                          jmethodID method,
                                          jint* entry_count_ptr,
                                          jvmtiLocalVariableEntry** table_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetBytecodes(jvmtiEnv* env,
                                 jmethodID method,
                                 jint* bytecode_count_ptr,
                                 unsigned char** bytecodes_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsMethodNative(jvmtiEnv* env, jmethodID method, jboolean* is_native_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsMethodSynthetic(jvmtiEnv* env, jmethodID method, jboolean* is_synthetic_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError IsMethodObsolete(jvmtiEnv* env, jmethodID method, jboolean* is_obsolete_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetNativeMethodPrefix(jvmtiEnv* env, const char* prefix) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetNativeMethodPrefixes(jvmtiEnv* env, jint prefix_count, char** prefixes) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError CreateRawMonitor(jvmtiEnv* env, const char* name, jrawMonitorID* monitor_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError DestroyRawMonitor(jvmtiEnv* env, jrawMonitorID monitor) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RawMonitorEnter(jvmtiEnv* env, jrawMonitorID monitor) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RawMonitorExit(jvmtiEnv* env, jrawMonitorID monitor) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RawMonitorWait(jvmtiEnv* env, jrawMonitorID monitor, jlong millis) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RawMonitorNotify(jvmtiEnv* env, jrawMonitorID monitor) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RawMonitorNotifyAll(jvmtiEnv* env, jrawMonitorID monitor) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetJNIFunctionTable(jvmtiEnv* env, const jniNativeInterface* function_table) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetJNIFunctionTable(jvmtiEnv* env, jniNativeInterface** function_table) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetEventCallbacks(jvmtiEnv* env,
                                      const jvmtiEventCallbacks* callbacks,
                                      jint size_of_callbacks) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetEventNotificationMode(jvmtiEnv* env,
                                             jvmtiEventMode mode,
                                             jvmtiEvent event_type,
                                             jthread event_thread,
                                             ...) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GenerateEvents(jvmtiEnv* env, jvmtiEvent event_type) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetExtensionFunctions(jvmtiEnv* env,
                                          jint* extension_count_ptr,
                                          jvmtiExtensionFunctionInfo** extensions) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetExtensionEvents(jvmtiEnv* env,
                                       jint* extension_count_ptr,
                                       jvmtiExtensionEventInfo** extensions) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetExtensionEventCallback(jvmtiEnv* env,
                                              jint extension_event_index,
                                              jvmtiExtensionEvent callback) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetPotentialCapabilities(jvmtiEnv* env, jvmtiCapabilities* capabilities_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError AddCapabilities(jvmtiEnv* env, const jvmtiCapabilities* capabilities_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError RelinquishCapabilities(jvmtiEnv* env,
                                           const jvmtiCapabilities* capabilities_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetCapabilities(jvmtiEnv* env, jvmtiCapabilities* capabilities_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetCurrentThreadCpuTimerInfo(jvmtiEnv* env, jvmtiTimerInfo* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetCurrentThreadCpuTime(jvmtiEnv* env, jlong* nanos_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadCpuTimerInfo(jvmtiEnv* env, jvmtiTimerInfo* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetThreadCpuTime(jvmtiEnv* env, jthread thread, jlong* nanos_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetTimerInfo(jvmtiEnv* env, jvmtiTimerInfo* info_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetTime(jvmtiEnv* env, jlong* nanos_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetAvailableProcessors(jvmtiEnv* env, jint* processor_count_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError AddToBootstrapClassLoaderSearch(jvmtiEnv* env, const char* segment) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError AddToSystemClassLoaderSearch(jvmtiEnv* env, const char* segment) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetSystemProperties(jvmtiEnv* env, jint* count_ptr, char*** property_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetSystemProperty(jvmtiEnv* env, const char* property, char** value_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError SetSystemProperty(jvmtiEnv* env, const char* property, const char* value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetPhase(jvmtiEnv* env, jvmtiPhase* phase_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError DisposeEnvironment(jvmtiEnv* env) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    delete env;
    return OK;
  }

  static jvmtiError SetEnvironmentLocalStorage(jvmtiEnv* env, const void* data) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    reinterpret_cast<ArtJvmTiEnv*>(env)->local_data = const_cast<void*>(data);
    return OK;
  }

  static jvmtiError GetEnvironmentLocalStorage(jvmtiEnv* env, void** data_ptr) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    *data_ptr = reinterpret_cast<ArtJvmTiEnv*>(env)->local_data;
    return OK;
  }

  static jvmtiError GetVersionNumber(jvmtiEnv* env, jint* version_ptr) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    *version_ptr = JVMTI_VERSION;
    return OK;
  }

  static jvmtiError GetErrorName(jvmtiEnv* env, jvmtiError error,  char** name_ptr) {
    if (!IsValidEnv(env)) {
      return ERR(INVALID_ENVIRONMENT);
    }
    if (name_ptr == nullptr) {
      return ERR(NULL_POINTER);
    }
    switch (error) {
#define ERROR_CASE(e) case (JVMTI_ERROR_ ## e) : do { \
          *name_ptr = const_cast<char*>("JVMTI_ERROR_"#e); \
          return OK; \
        } while (false)
      ERROR_CASE(NONE);
      ERROR_CASE(INVALID_THREAD);
      ERROR_CASE(INVALID_THREAD_GROUP);
      ERROR_CASE(INVALID_PRIORITY);
      ERROR_CASE(THREAD_NOT_SUSPENDED);
      ERROR_CASE(THREAD_NOT_ALIVE);
      ERROR_CASE(INVALID_OBJECT);
      ERROR_CASE(INVALID_CLASS);
      ERROR_CASE(CLASS_NOT_PREPARED);
      ERROR_CASE(INVALID_METHODID);
      ERROR_CASE(INVALID_LOCATION);
      ERROR_CASE(INVALID_FIELDID);
      ERROR_CASE(NO_MORE_FRAMES);
      ERROR_CASE(OPAQUE_FRAME);
      ERROR_CASE(TYPE_MISMATCH);
      ERROR_CASE(INVALID_SLOT);
      ERROR_CASE(DUPLICATE);
      ERROR_CASE(NOT_FOUND);
      ERROR_CASE(INVALID_MONITOR);
      ERROR_CASE(NOT_MONITOR_OWNER);
      ERROR_CASE(INTERRUPT);
      ERROR_CASE(INVALID_CLASS_FORMAT);
      ERROR_CASE(CIRCULAR_CLASS_DEFINITION);
      ERROR_CASE(FAILS_VERIFICATION);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_METHOD_ADDED);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);
      ERROR_CASE(INVALID_TYPESTATE);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_METHOD_DELETED);
      ERROR_CASE(UNSUPPORTED_VERSION);
      ERROR_CASE(NAMES_DONT_MATCH);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
      ERROR_CASE(UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED);
      ERROR_CASE(UNMODIFIABLE_CLASS);
      ERROR_CASE(NOT_AVAILABLE);
      ERROR_CASE(MUST_POSSESS_CAPABILITY);
      ERROR_CASE(NULL_POINTER);
      ERROR_CASE(ABSENT_INFORMATION);
      ERROR_CASE(INVALID_EVENT_TYPE);
      ERROR_CASE(ILLEGAL_ARGUMENT);
      ERROR_CASE(NATIVE_METHOD);
      ERROR_CASE(CLASS_LOADER_UNSUPPORTED);
      ERROR_CASE(OUT_OF_MEMORY);
      ERROR_CASE(ACCESS_DENIED);
      ERROR_CASE(WRONG_PHASE);
      ERROR_CASE(INTERNAL);
      ERROR_CASE(UNATTACHED_THREAD);
      ERROR_CASE(INVALID_ENVIRONMENT);
#undef ERROR_CASE
      default: {
        *name_ptr = const_cast<char*>("JVMTI_ERROR_UNKNOWN");
        return ERR(ILLEGAL_ARGUMENT);
      }
    }
  }

  static jvmtiError SetVerboseFlag(jvmtiEnv* env, jvmtiVerboseFlag flag, jboolean value) {
    return ERR(NOT_IMPLEMENTED);
  }

  static jvmtiError GetJLocationFormat(jvmtiEnv* env, jvmtiJlocationFormat* format_ptr) {
    return ERR(NOT_IMPLEMENTED);
  }
};

static bool IsJvmtiVersion(jint version) {
  return version ==  JVMTI_VERSION_1 ||
         version == JVMTI_VERSION_1_0 ||
         version == JVMTI_VERSION_1_1 ||
         version == JVMTI_VERSION_1_2 ||
         version == JVMTI_VERSION;
}

// Creates a jvmtiEnv and returns it with the art::ti::Env that is associated with it. new_art_ti
// is a pointer to the uninitialized memory for an art::ti::Env.
static void CreateArtJvmTiEnv(art::JavaVMExt* vm, /*out*/void** new_jvmtiEnv) {
  struct ArtJvmTiEnv* env = new ArtJvmTiEnv(vm);
  *new_jvmtiEnv = env;
}

// A hook that the runtime uses to allow plugins to handle GetEnv calls. It returns true and
// places the return value in 'env' if this library can handle the GetEnv request. Otherwise
// returns false and does not modify the 'env' pointer.
static jint GetEnvHandler(art::JavaVMExt* vm, /*out*/void** env, jint version) {
  if (IsJvmtiVersion(version)) {
    CreateArtJvmTiEnv(vm, env);
    return JNI_OK;
  } else {
    printf("version 0x%x is not valid!", version);
    return JNI_EVERSION;
  }
}

// The plugin initialization function. This adds the jvmti environment.
extern "C" bool ArtPlugin_Initialize() {
  art::Runtime::Current()->GetJavaVM()->AddEnvironmentHook(GetEnvHandler);
  return true;
}

// The actual struct holding all of the entrypoints into the jvmti interface.
const jvmtiInterface_1 gJvmtiInterface = {
  nullptr,  // reserved1
  JvmtiFunctions::SetEventNotificationMode,
  nullptr,  // reserved3
  JvmtiFunctions::GetAllThreads,
  JvmtiFunctions::SuspendThread,
  JvmtiFunctions::ResumeThread,
  JvmtiFunctions::StopThread,
  JvmtiFunctions::InterruptThread,
  JvmtiFunctions::GetThreadInfo,
  JvmtiFunctions::GetOwnedMonitorInfo,  // 10
  JvmtiFunctions::GetCurrentContendedMonitor,
  JvmtiFunctions::RunAgentThread,
  JvmtiFunctions::GetTopThreadGroups,
  JvmtiFunctions::GetThreadGroupInfo,
  JvmtiFunctions::GetThreadGroupChildren,
  JvmtiFunctions::GetFrameCount,
  JvmtiFunctions::GetThreadState,
  JvmtiFunctions::GetCurrentThread,
  JvmtiFunctions::GetFrameLocation,
  JvmtiFunctions::NotifyFramePop,  // 20
  JvmtiFunctions::GetLocalObject,
  JvmtiFunctions::GetLocalInt,
  JvmtiFunctions::GetLocalLong,
  JvmtiFunctions::GetLocalFloat,
  JvmtiFunctions::GetLocalDouble,
  JvmtiFunctions::SetLocalObject,
  JvmtiFunctions::SetLocalInt,
  JvmtiFunctions::SetLocalLong,
  JvmtiFunctions::SetLocalFloat,
  JvmtiFunctions::SetLocalDouble,  // 30
  JvmtiFunctions::CreateRawMonitor,
  JvmtiFunctions::DestroyRawMonitor,
  JvmtiFunctions::RawMonitorEnter,
  JvmtiFunctions::RawMonitorExit,
  JvmtiFunctions::RawMonitorWait,
  JvmtiFunctions::RawMonitorNotify,
  JvmtiFunctions::RawMonitorNotifyAll,
  JvmtiFunctions::SetBreakpoint,
  JvmtiFunctions::ClearBreakpoint,
  nullptr,  // reserved40
  JvmtiFunctions::SetFieldAccessWatch,
  JvmtiFunctions::ClearFieldAccessWatch,
  JvmtiFunctions::SetFieldModificationWatch,
  JvmtiFunctions::ClearFieldModificationWatch,
  JvmtiFunctions::IsModifiableClass,
  JvmtiFunctions::Allocate,
  JvmtiFunctions::Deallocate,
  JvmtiFunctions::GetClassSignature,
  JvmtiFunctions::GetClassStatus,
  JvmtiFunctions::GetSourceFileName,  // 50
  JvmtiFunctions::GetClassModifiers,
  JvmtiFunctions::GetClassMethods,
  JvmtiFunctions::GetClassFields,
  JvmtiFunctions::GetImplementedInterfaces,
  JvmtiFunctions::IsInterface,
  JvmtiFunctions::IsArrayClass,
  JvmtiFunctions::GetClassLoader,
  JvmtiFunctions::GetObjectHashCode,
  JvmtiFunctions::GetObjectMonitorUsage,
  JvmtiFunctions::GetFieldName,  // 60
  JvmtiFunctions::GetFieldDeclaringClass,
  JvmtiFunctions::GetFieldModifiers,
  JvmtiFunctions::IsFieldSynthetic,
  JvmtiFunctions::GetMethodName,
  JvmtiFunctions::GetMethodDeclaringClass,
  JvmtiFunctions::GetMethodModifiers,
  nullptr,  // reserved67
  JvmtiFunctions::GetMaxLocals,
  JvmtiFunctions::GetArgumentsSize,
  JvmtiFunctions::GetLineNumberTable,  // 70
  JvmtiFunctions::GetMethodLocation,
  JvmtiFunctions::GetLocalVariableTable,
  JvmtiFunctions::SetNativeMethodPrefix,
  JvmtiFunctions::SetNativeMethodPrefixes,
  JvmtiFunctions::GetBytecodes,
  JvmtiFunctions::IsMethodNative,
  JvmtiFunctions::IsMethodSynthetic,
  JvmtiFunctions::GetLoadedClasses,
  JvmtiFunctions::GetClassLoaderClasses,
  JvmtiFunctions::PopFrame,  // 80
  JvmtiFunctions::ForceEarlyReturnObject,
  JvmtiFunctions::ForceEarlyReturnInt,
  JvmtiFunctions::ForceEarlyReturnLong,
  JvmtiFunctions::ForceEarlyReturnFloat,
  JvmtiFunctions::ForceEarlyReturnDouble,
  JvmtiFunctions::ForceEarlyReturnVoid,
  JvmtiFunctions::RedefineClasses,
  JvmtiFunctions::GetVersionNumber,
  JvmtiFunctions::GetCapabilities,
  JvmtiFunctions::GetSourceDebugExtension,  // 90
  JvmtiFunctions::IsMethodObsolete,
  JvmtiFunctions::SuspendThreadList,
  JvmtiFunctions::ResumeThreadList,
  nullptr,  // reserved94
  nullptr,  // reserved95
  nullptr,  // reserved96
  nullptr,  // reserved97
  nullptr,  // reserved98
  nullptr,  // reserved99
  JvmtiFunctions::GetAllStackTraces,  // 100
  JvmtiFunctions::GetThreadListStackTraces,
  JvmtiFunctions::GetThreadLocalStorage,
  JvmtiFunctions::SetThreadLocalStorage,
  JvmtiFunctions::GetStackTrace,
  nullptr,  // reserved105
  JvmtiFunctions::GetTag,
  JvmtiFunctions::SetTag,
  JvmtiFunctions::ForceGarbageCollection,
  JvmtiFunctions::IterateOverObjectsReachableFromObject,
  JvmtiFunctions::IterateOverReachableObjects,  // 110
  JvmtiFunctions::IterateOverHeap,
  JvmtiFunctions::IterateOverInstancesOfClass,
  nullptr,  // reserved113
  JvmtiFunctions::GetObjectsWithTags,
  JvmtiFunctions::FollowReferences,
  JvmtiFunctions::IterateThroughHeap,
  nullptr,  // reserved117
  nullptr,  // reserved118
  nullptr,  // reserved119
  JvmtiFunctions::SetJNIFunctionTable,  // 120
  JvmtiFunctions::GetJNIFunctionTable,
  JvmtiFunctions::SetEventCallbacks,
  JvmtiFunctions::GenerateEvents,
  JvmtiFunctions::GetExtensionFunctions,
  JvmtiFunctions::GetExtensionEvents,
  JvmtiFunctions::SetExtensionEventCallback,
  JvmtiFunctions::DisposeEnvironment,
  JvmtiFunctions::GetErrorName,
  JvmtiFunctions::GetJLocationFormat,
  JvmtiFunctions::GetSystemProperties,  // 130
  JvmtiFunctions::GetSystemProperty,
  JvmtiFunctions::SetSystemProperty,
  JvmtiFunctions::GetPhase,
  JvmtiFunctions::GetCurrentThreadCpuTimerInfo,
  JvmtiFunctions::GetCurrentThreadCpuTime,
  JvmtiFunctions::GetThreadCpuTimerInfo,
  JvmtiFunctions::GetThreadCpuTime,
  JvmtiFunctions::GetTimerInfo,
  JvmtiFunctions::GetTime,
  JvmtiFunctions::GetPotentialCapabilities,  // 140
  nullptr,  // reserved141
  JvmtiFunctions::AddCapabilities,
  JvmtiFunctions::RelinquishCapabilities,
  JvmtiFunctions::GetAvailableProcessors,
  JvmtiFunctions::GetClassVersionNumbers,
  JvmtiFunctions::GetConstantPool,
  JvmtiFunctions::GetEnvironmentLocalStorage,
  JvmtiFunctions::SetEnvironmentLocalStorage,
  JvmtiFunctions::AddToBootstrapClassLoaderSearch,
  JvmtiFunctions::SetVerboseFlag,  // 150
  JvmtiFunctions::AddToSystemClassLoaderSearch,
  JvmtiFunctions::RetransformClasses,
  JvmtiFunctions::GetOwnedMonitorStackDepthInfo,
  JvmtiFunctions::GetObjectSize,
  JvmtiFunctions::GetLocalInstance,
};

};  // namespace openjdkjvmti
