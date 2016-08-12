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

#ifndef ART_RUNTIME_TI_AGENT_H_
#define ART_RUNTIME_TI_AGENT_H_

#include <dlfcn.h>
#include <jni.h>  // for jint, JavaVM* etc declarations

#include "base/stringprintf.h"
#include "runtime.h"
#include "utils.h"

namespace art {
namespace ti {

using AgentOnLoadFunction = jint (*)(JavaVM*, const char*, void*);
using AgentOnAttachFunction = jint (*)(JavaVM*, const char*, void*);
using AgentOnUnloadFunction = void (*)(JavaVM*);

class Agent {
 public:
  enum LoadError {
    kNoError,              // No error occurred..
    kAlreadyStarted,       // The agent has already been loaded.
    kLoadingError,         // dlopen or dlsym returned an error.
    kInitializationError,  // The entrypoint did not return 0. This might require an abort.
  };

  bool IsStarted() const {
    return dlopen_handle_ != nullptr;
  }

  const std::string& GetName() const {
    return name_;
  }

  const std::string& GetArgs() const {
    return args_;
  }

  bool HasArgs() const {
    return !GetArgs().empty();
  }

  // TODO We need to acquire some locks probably.
  LoadError Load(/*out*/jint* call_res, /*out*/std::string* error_msg);

  // TODO We need to acquire some locks probably.
  void Unload();

  // Tries to attach the agent using its OnAttach method. Returns true on success.
  // TODO We need to acquire some locks probably.
  LoadError Attach(std::string* error_msg) {
    // TODO
    *error_msg = "Attach has not yet been implemented!";
    return kLoadingError;
  }

  static Agent Create(std::string arg);

  static Agent Create(std::string name, std::string args) {
    return Agent(name, args);
  }

  ~Agent();

  // We need move constructor and copy for vectors
  Agent(const Agent& other);

  Agent(Agent&& other)
      : name_(other.name_),
        args_(other.args_),
        dlopen_handle_(nullptr),
        onload_(nullptr),
        onattach_(nullptr),
        onunload_(nullptr) {
    other.dlopen_handle_ = nullptr;
    other.onload_ = nullptr;
    other.onattach_ = nullptr;
    other.onunload_ = nullptr;
  }

  // We don't need an operator=
  void operator=(const Agent&) = delete;

 private:
  Agent(std::string name, std::string args)
      : name_(name),
        args_(args),
        dlopen_handle_(nullptr),
        onload_(nullptr),
        onattach_(nullptr),
        onunload_(nullptr) { }

  LoadError DoDlOpen(/*out*/std::string* error_msg);

  const std::string name_;
  const std::string args_;
  void* dlopen_handle_;

  // The entrypoints.
  AgentOnLoadFunction onload_;
  AgentOnAttachFunction onattach_;
  AgentOnUnloadFunction onunload_;

  friend std::ostream& operator<<(std::ostream &os, Agent const& m);
};

std::ostream& operator<<(std::ostream &os, Agent const& m);
std::ostream& operator<<(std::ostream &os, const Agent* m);

}  // namespace ti
}  // namespace art

#endif  // ART_RUNTIME_TI_AGENT_H_

