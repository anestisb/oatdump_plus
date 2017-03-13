/*
 * Copyright 2016 The Android Open Source Project
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

#include "common_load.h"

#include <jni.h>
#include <stdio.h>

#include "art_method-inl.h"
#include "base/logging.h"
#include "base/macros.h"
#include "common_helper.h"

#include "901-hello-ti-agent/basics.h"
#include "909-attach-agent/attach.h"
#include "936-search-onload/search_onload.h"

namespace art {

jvmtiEnv* jvmti_env;

namespace {

using OnLoad   = jint (*)(JavaVM* vm, char* options, void* reserved);
using OnAttach = jint (*)(JavaVM* vm, char* options, void* reserved);

struct AgentLib {
  const char* name;
  OnLoad load;
  OnAttach attach;
};

static void JNICALL VMInitCallback(jvmtiEnv *jvmti_env,
                                   JNIEnv* jni_env,
                                   jthread thread ATTRIBUTE_UNUSED) {
  // Bind Main native methods.
  BindFunctions(jvmti_env, jni_env, "Main");
}

// Install a phase callback that will bind JNI functions on VMInit.
bool InstallBindCallback(JavaVM* vm) {
  // Use a new jvmtiEnv. Otherwise we might collide with table changes.
  jvmtiEnv* install_env;
  if (vm->GetEnv(reinterpret_cast<void**>(&install_env), JVMTI_VERSION_1_0) != 0) {
    return false;
  }
  SetAllCapabilities(install_env);

  {
    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
    callbacks.VMInit = VMInitCallback;

    jvmtiError install_error = install_env->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (install_error != JVMTI_ERROR_NONE) {
      return false;
    }
  }

  {
    jvmtiError enable_error = install_env->SetEventNotificationMode(JVMTI_ENABLE,
                                                                    JVMTI_EVENT_VM_INIT,
                                                                    nullptr);
    if (enable_error != JVMTI_ERROR_NONE) {
      return false;
    }
  }

  return true;
}

// A trivial OnLoad implementation that only initializes the global jvmti_env.
static jint MinimalOnLoad(JavaVM* vm,
                          char* options ATTRIBUTE_UNUSED,
                          void* reserved ATTRIBUTE_UNUSED) {
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti_env), JVMTI_VERSION_1_0) != 0) {
    printf("Unable to get jvmti env!\n");
    return 1;
  }
  SetAllCapabilities(jvmti_env);
  return 0;
}

// A list of all non-standard the agents we have for testing. All other agents will use
// MinimalOnLoad.
static AgentLib agents[] = {
  { "901-hello-ti-agent", Test901HelloTi::OnLoad, nullptr },
  { "902-hello-transformation", common_redefine::OnLoad, nullptr },
  { "909-attach-agent", nullptr, Test909AttachAgent::OnAttach },
  { "914-hello-obsolescence", common_redefine::OnLoad, nullptr },
  { "915-obsolete-2", common_redefine::OnLoad, nullptr },
  { "916-obsolete-jit", common_redefine::OnLoad, nullptr },
  { "917-fields-transformation", common_redefine::OnLoad, nullptr },
  { "919-obsolete-fields", common_redefine::OnLoad, nullptr },
  { "921-hello-failure", common_retransform::OnLoad, nullptr },
  { "926-multi-obsolescence", common_redefine::OnLoad, nullptr },
  { "930-hello-retransform", common_retransform::OnLoad, nullptr },
  { "932-transform-saves", common_retransform::OnLoad, nullptr },
  { "934-load-transform", common_retransform::OnLoad, nullptr },
  { "935-non-retransformable", common_transform::OnLoad, nullptr },
  { "936-search-onload", Test936SearchOnload::OnLoad, nullptr },
  { "937-hello-retransform-package", common_retransform::OnLoad, nullptr },
  { "938-load-transform-bcp", common_retransform::OnLoad, nullptr },
  { "939-hello-transformation-bcp", common_redefine::OnLoad, nullptr },
  { "940-recursive-obsolete", common_redefine::OnLoad, nullptr },
  { "941-recursive-obsolete-jit", common_redefine::OnLoad, nullptr },
  { "942-private-recursive", common_redefine::OnLoad, nullptr },
  { "943-private-recursive-jit", common_redefine::OnLoad, nullptr },
  { "944-transform-classloaders", common_redefine::OnLoad, nullptr },
  { "945-obsolete-native", common_redefine::OnLoad, nullptr },
};

static AgentLib* FindAgent(char* name) {
  for (AgentLib& l : agents) {
    if (strncmp(l.name, name, strlen(l.name)) == 0) {
      return &l;
    }
  }
  return nullptr;
}

static bool FindAgentNameAndOptions(char* options,
                                    /*out*/char** name,
                                    /*out*/char** other_options) {
  // Name is the first element.
  *name = options;
  char* rest = options;
  // name is the first thing in the options
  while (*rest != '\0' && *rest != ',') {
    rest++;
  }
  if (*rest == ',') {
    *rest = '\0';
    rest++;
  }
  *other_options = rest;
  return true;
}

static void SetIsJVM(char* options) {
  RuntimeIsJVM = strncmp(options, "jvm", 3) == 0;
}

static bool BindFunctionsAttached(JavaVM* vm, const char* class_name) {
  // Get a JNIEnv. As the thread is attached, we must not destroy it.
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != 0) {
    printf("Unable to get JNI env!\n");
    return false;
  }

  jvmtiEnv* jenv;
  if (vm->GetEnv(reinterpret_cast<void**>(&jenv), JVMTI_VERSION_1_0) != 0) {
    printf("Unable to get jvmti env!\n");
    return false;
  }
  SetAllCapabilities(jenv);

  BindFunctions(jenv, env, class_name);

  return true;
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
  char* remaining_options = nullptr;
  char* name_option = nullptr;
  if (!FindAgentNameAndOptions(options, &name_option, &remaining_options)) {
    printf("Unable to find agent name in options: %s\n", options);
    return -1;
  }

  SetIsJVM(remaining_options);

  if (!InstallBindCallback(vm)) {
    return 1;
  }

  AgentLib* lib = FindAgent(name_option);
  OnLoad fn = nullptr;
  if (lib == nullptr) {
    fn = &MinimalOnLoad;
  } else {
    if (lib->load == nullptr) {
      printf("agent: %s does not include an OnLoad method.\n", name_option);
      return -3;
    }
    fn = lib->load;
  }
  return fn(vm, remaining_options, reserved);
}

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  char* remaining_options = nullptr;
  char* name_option = nullptr;
  if (!FindAgentNameAndOptions(options, &name_option, &remaining_options)) {
    printf("Unable to find agent name in options: %s\n", options);
    return -1;
  }

  BindFunctionsAttached(vm, "Main");

  AgentLib* lib = FindAgent(name_option);
  if (lib == nullptr) {
    printf("Unable to find agent named: %s, add it to the list in test/ti-agent/common_load.cc\n",
           name_option);
    return -2;
  }
  if (lib->attach == nullptr) {
    printf("agent: %s does not include an OnAttach method.\n", name_option);
    return -3;
  }
  SetIsJVM(remaining_options);
  return lib->attach(vm, remaining_options, reserved);
}

}  // namespace art
