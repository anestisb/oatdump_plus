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

#include "jvmti_helper.h"

#include <algorithm>
#include <dlfcn.h>
#include <stdio.h>
#include <sstream>
#include <string.h>

#include "android-base/logging.h"
#include "scoped_local_ref.h"

namespace art {

void CheckJvmtiError(jvmtiEnv* env, jvmtiError error) {
  if (error != JVMTI_ERROR_NONE) {
    char* error_name;
    jvmtiError name_error = env->GetErrorName(error, &error_name);
    if (name_error != JVMTI_ERROR_NONE) {
      LOG(FATAL) << "Unable to get error name for " << error;
    }
    LOG(FATAL) << "Unexpected error: " << error_name;
  }
}

void SetAllCapabilities(jvmtiEnv* env) {
  jvmtiCapabilities caps;
  jvmtiError error1 = env->GetPotentialCapabilities(&caps);
  CheckJvmtiError(env, error1);
  jvmtiError error2 = env->AddCapabilities(&caps);
  CheckJvmtiError(env, error2);
}

bool JvmtiErrorToException(JNIEnv* env, jvmtiEnv* jvmti_env, jvmtiError error) {
  if (error == JVMTI_ERROR_NONE) {
    return false;
  }

  ScopedLocalRef<jclass> rt_exception(env, env->FindClass("java/lang/RuntimeException"));
  if (rt_exception.get() == nullptr) {
    // CNFE should be pending.
    return true;
  }

  char* err;
  CheckJvmtiError(jvmti_env, jvmti_env->GetErrorName(error, &err));

  env->ThrowNew(rt_exception.get(), err);

  Deallocate(jvmti_env, err);
  return true;
}

std::ostream& operator<<(std::ostream& os, const jvmtiError& rhs) {
  switch (rhs) {
    case JVMTI_ERROR_NONE:
      return os << "NONE";
    case JVMTI_ERROR_INVALID_THREAD:
      return os << "INVALID_THREAD";
    case JVMTI_ERROR_INVALID_THREAD_GROUP:
      return os << "INVALID_THREAD_GROUP";
    case JVMTI_ERROR_INVALID_PRIORITY:
      return os << "INVALID_PRIORITY";
    case JVMTI_ERROR_THREAD_NOT_SUSPENDED:
      return os << "THREAD_NOT_SUSPENDED";
    case JVMTI_ERROR_THREAD_SUSPENDED:
      return os << "THREAD_SUSPENDED";
    case JVMTI_ERROR_THREAD_NOT_ALIVE:
      return os << "THREAD_NOT_ALIVE";
    case JVMTI_ERROR_INVALID_OBJECT:
      return os << "INVALID_OBJECT";
    case JVMTI_ERROR_INVALID_CLASS:
      return os << "INVALID_CLASS";
    case JVMTI_ERROR_CLASS_NOT_PREPARED:
      return os << "CLASS_NOT_PREPARED";
    case JVMTI_ERROR_INVALID_METHODID:
      return os << "INVALID_METHODID";
    case JVMTI_ERROR_INVALID_LOCATION:
      return os << "INVALID_LOCATION";
    case JVMTI_ERROR_INVALID_FIELDID:
      return os << "INVALID_FIELDID";
    case JVMTI_ERROR_NO_MORE_FRAMES:
      return os << "NO_MORE_FRAMES";
    case JVMTI_ERROR_OPAQUE_FRAME:
      return os << "OPAQUE_FRAME";
    case JVMTI_ERROR_TYPE_MISMATCH:
      return os << "TYPE_MISMATCH";
    case JVMTI_ERROR_INVALID_SLOT:
      return os << "INVALID_SLOT";
    case JVMTI_ERROR_DUPLICATE:
      return os << "DUPLICATE";
    case JVMTI_ERROR_NOT_FOUND:
      return os << "NOT_FOUND";
    case JVMTI_ERROR_INVALID_MONITOR:
      return os << "INVALID_MONITOR";
    case JVMTI_ERROR_NOT_MONITOR_OWNER:
      return os << "NOT_MONITOR_OWNER";
    case JVMTI_ERROR_INTERRUPT:
      return os << "INTERRUPT";
    case JVMTI_ERROR_INVALID_CLASS_FORMAT:
      return os << "INVALID_CLASS_FORMAT";
    case JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION:
      return os << "CIRCULAR_CLASS_DEFINITION";
    case JVMTI_ERROR_FAILS_VERIFICATION:
      return os << "FAILS_VERIFICATION";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED:
      return os << "UNSUPPORTED_REDEFINITION_METHOD_ADDED";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED:
      return os << "UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED";
    case JVMTI_ERROR_INVALID_TYPESTATE:
      return os << "INVALID_TYPESTATE";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED:
      return os << "UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_DELETED:
      return os << "UNSUPPORTED_REDEFINITION_METHOD_DELETED";
    case JVMTI_ERROR_UNSUPPORTED_VERSION:
      return os << "UNSUPPORTED_VERSION";
    case JVMTI_ERROR_NAMES_DONT_MATCH:
      return os << "NAMES_DONT_MATCH";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED:
      return os << "UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED";
    case JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED:
      return os << "UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED";
    case JVMTI_ERROR_UNMODIFIABLE_CLASS:
      return os << "JVMTI_ERROR_UNMODIFIABLE_CLASS";
    case JVMTI_ERROR_NOT_AVAILABLE:
      return os << "NOT_AVAILABLE";
    case JVMTI_ERROR_MUST_POSSESS_CAPABILITY:
      return os << "MUST_POSSESS_CAPABILITY";
    case JVMTI_ERROR_NULL_POINTER:
      return os << "NULL_POINTER";
    case JVMTI_ERROR_ABSENT_INFORMATION:
      return os << "ABSENT_INFORMATION";
    case JVMTI_ERROR_INVALID_EVENT_TYPE:
      return os << "INVALID_EVENT_TYPE";
    case JVMTI_ERROR_ILLEGAL_ARGUMENT:
      return os << "ILLEGAL_ARGUMENT";
    case JVMTI_ERROR_NATIVE_METHOD:
      return os << "NATIVE_METHOD";
    case JVMTI_ERROR_CLASS_LOADER_UNSUPPORTED:
      return os << "CLASS_LOADER_UNSUPPORTED";
    case JVMTI_ERROR_OUT_OF_MEMORY:
      return os << "OUT_OF_MEMORY";
    case JVMTI_ERROR_ACCESS_DENIED:
      return os << "ACCESS_DENIED";
    case JVMTI_ERROR_WRONG_PHASE:
      return os << "WRONG_PHASE";
    case JVMTI_ERROR_INTERNAL:
      return os << "INTERNAL";
    case JVMTI_ERROR_UNATTACHED_THREAD:
      return os << "UNATTACHED_THREAD";
    case JVMTI_ERROR_INVALID_ENVIRONMENT:
      return os << "INVALID_ENVIRONMENT";
  }
  LOG(FATAL) << "Unexpected error type " << static_cast<int>(rhs);
  __builtin_unreachable();
}

}  // namespace art
