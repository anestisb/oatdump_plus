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

#include <jni.h>
#include <stdio.h>
// TODO I don't know?
#include "openjdkjvmti/jvmti.h"

#include "art_method-inl.h"
#include "base/logging.h"
#include "base/macros.h"
#include "common_load.h"

#include "901-hello-ti-agent/basics.h"
#include "902-hello-transformation/transform.h"
#include "903-hello-tagging/tagging.h"
#include "904-object-allocation/tracking.h"
#include "905-object-free/tracking_free.h"
#include "906-iterate-heap/iterate_heap.h"
#include "907-get-loaded-classes/get_loaded_classes.h"
#include "908-gc-start-finish/gc_callbacks.h"
#include "909-attach-agent/attach.h"

namespace art {

jvmtiEnv* jvmti_env;

using OnLoad   = jint (*)(JavaVM* vm, char* options, void* reserved);
using OnAttach = jint (*)(JavaVM* vm, char* options, void* reserved);

struct AgentLib {
  const char* name;
  OnLoad load;
  OnAttach attach;
};

// A list of all the agents we have for testing.
AgentLib agents[] = {
  { "901-hello-ti-agent", Test901HelloTi::OnLoad, nullptr },
  { "902-hello-transformation", Test902HelloTransformation::OnLoad, nullptr },
  { "903-hello-tagging", Test903HelloTagging::OnLoad, nullptr },
  { "904-object-allocation", Test904ObjectAllocation::OnLoad, nullptr },
  { "905-object-free", Test905ObjectFree::OnLoad, nullptr },
  { "906-iterate-heap", Test906IterateHeap::OnLoad, nullptr },
  { "907-get-loaded-classes", Test907GetLoadedClasses::OnLoad, nullptr },
  { "908-gc-start-finish", Test908GcStartFinish::OnLoad, nullptr },
  { "909-attach-agent", nullptr, Test909AttachAgent::OnAttach },
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

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
  char* remaining_options = nullptr;
  char* name_option = nullptr;
  if (!FindAgentNameAndOptions(options, &name_option, &remaining_options)) {
    printf("Unable to find agent name in options: %s\n", options);
    return -1;
  }
  AgentLib* lib = FindAgent(name_option);
  if (lib == nullptr) {
    printf("Unable to find agent named: %s, add it to the list in test/ti-agent/common_load.cc\n",
           name_option);
    return -2;
  }
  if (lib->load == nullptr) {
    printf("agent: %s does not include an OnLoad method.\n", name_option);
    return -3;
  }
  return lib->load(vm, remaining_options, reserved);
}

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
  char* remaining_options = nullptr;
  char* name_option = nullptr;
  if (!FindAgentNameAndOptions(options, &name_option, &remaining_options)) {
    printf("Unable to find agent name in options: %s\n", options);
    return -1;
  }
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
  return lib->attach(vm, remaining_options, reserved);
}

}  // namespace art
