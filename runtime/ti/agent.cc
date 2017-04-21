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

#include "agent.h"

#include "android-base/stringprintf.h"

#include "java_vm_ext.h"
#include "runtime.h"

namespace art {
namespace ti {

using android::base::StringPrintf;

const char* AGENT_ON_LOAD_FUNCTION_NAME = "Agent_OnLoad";
const char* AGENT_ON_ATTACH_FUNCTION_NAME = "Agent_OnAttach";
const char* AGENT_ON_UNLOAD_FUNCTION_NAME = "Agent_OnUnload";

// TODO We need to acquire some locks probably.
Agent::LoadError Agent::DoLoadHelper(bool attaching,
                                     /*out*/jint* call_res,
                                     /*out*/std::string* error_msg) {
  DCHECK(call_res != nullptr);
  DCHECK(error_msg != nullptr);

  if (IsStarted()) {
    *error_msg = StringPrintf("the agent at %s has already been started!", name_.c_str());
    VLOG(agents) << "err: " << *error_msg;
    return kAlreadyStarted;
  }
  LoadError err = DoDlOpen(error_msg);
  if (err != kNoError) {
    VLOG(agents) << "err: " << *error_msg;
    return err;
  }
  AgentOnLoadFunction callback = attaching ? onattach_ : onload_;
  if (callback == nullptr) {
    *error_msg = StringPrintf("Unable to start agent %s: No %s callback found",
                              (attaching ? "attach" : "load"),
                              name_.c_str());
    VLOG(agents) << "err: " << *error_msg;
    return kLoadingError;
  }
  // Need to let the function fiddle with the array.
  std::unique_ptr<char[]> copied_args(new char[args_.size() + 1]);
  strcpy(copied_args.get(), args_.c_str());
  // TODO Need to do some checks that we are at a good spot etc.
  *call_res = callback(Runtime::Current()->GetJavaVM(),
                       copied_args.get(),
                       nullptr);
  if (*call_res != 0) {
    *error_msg = StringPrintf("Initialization of %s returned non-zero value of %d",
                              name_.c_str(), *call_res);
    VLOG(agents) << "err: " << *error_msg;
    return kInitializationError;
  } else {
    return kNoError;
  }
}

void* Agent::FindSymbol(const std::string& name) const {
  CHECK(IsStarted()) << "Cannot find symbols in an unloaded agent library " << this;
  return dlsym(dlopen_handle_, name.c_str());
}

Agent::LoadError Agent::DoDlOpen(/*out*/std::string* error_msg) {
  DCHECK(error_msg != nullptr);

  DCHECK(dlopen_handle_ == nullptr);
  DCHECK(onload_ == nullptr);
  DCHECK(onattach_ == nullptr);
  DCHECK(onunload_ == nullptr);

  dlopen_handle_ = dlopen(name_.c_str(), RTLD_LAZY);
  if (dlopen_handle_ == nullptr) {
    *error_msg = StringPrintf("Unable to dlopen %s: %s", name_.c_str(), dlerror());
    return kLoadingError;
  }

  onload_ = reinterpret_cast<AgentOnLoadFunction>(FindSymbol(AGENT_ON_LOAD_FUNCTION_NAME));
  if (onload_ == nullptr) {
    VLOG(agents) << "Unable to find 'Agent_OnLoad' symbol in " << this;
  }
  onattach_ = reinterpret_cast<AgentOnLoadFunction>(FindSymbol(AGENT_ON_ATTACH_FUNCTION_NAME));
  if (onattach_ == nullptr) {
    VLOG(agents) << "Unable to find 'Agent_OnAttach' symbol in " << this;
  }
  onunload_= reinterpret_cast<AgentOnUnloadFunction>(FindSymbol(AGENT_ON_UNLOAD_FUNCTION_NAME));
  if (onunload_ == nullptr) {
    VLOG(agents) << "Unable to find 'Agent_OnUnload' symbol in " << this;
  }
  return kNoError;
}

// TODO Lock some stuff probably.
void Agent::Unload() {
  if (dlopen_handle_ != nullptr) {
    if (onunload_ != nullptr) {
      onunload_(Runtime::Current()->GetJavaVM());
    }
    dlclose(dlopen_handle_);
    dlopen_handle_ = nullptr;
    onload_ = nullptr;
    onattach_ = nullptr;
    onunload_ = nullptr;
  } else {
    VLOG(agents) << this << " is not currently loaded!";
  }
}

Agent::Agent(std::string arg)
    : dlopen_handle_(nullptr),
      onload_(nullptr),
      onattach_(nullptr),
      onunload_(nullptr) {
  size_t eq = arg.find_first_of('=');
  if (eq == std::string::npos) {
    name_ = arg;
  } else {
    name_ = arg.substr(0, eq);
    args_ = arg.substr(eq + 1, arg.length());
  }
}

Agent::Agent(const Agent& other)
    : dlopen_handle_(nullptr),
      onload_(nullptr),
      onattach_(nullptr),
      onunload_(nullptr) {
  *this = other;
}

// Attempting to copy to/from loaded/started agents is a fatal error
Agent& Agent::operator=(const Agent& other) {
  if (this != &other) {
    if (other.dlopen_handle_ != nullptr) {
      LOG(FATAL) << "Attempting to copy a loaded agent!";
    }

    if (dlopen_handle_ != nullptr) {
      LOG(FATAL) << "Attempting to assign into a loaded agent!";
    }

    DCHECK(other.onload_ == nullptr);
    DCHECK(other.onattach_ == nullptr);
    DCHECK(other.onunload_ == nullptr);

    DCHECK(onload_ == nullptr);
    DCHECK(onattach_ == nullptr);
    DCHECK(onunload_ == nullptr);

    name_ = other.name_;
    args_ = other.args_;

    dlopen_handle_ = nullptr;
    onload_ = nullptr;
    onattach_ = nullptr;
    onunload_ = nullptr;
  }
  return *this;
}

Agent::Agent(Agent&& other)
    : dlopen_handle_(nullptr),
      onload_(nullptr),
      onattach_(nullptr),
      onunload_(nullptr) {
  *this = std::move(other);
}

Agent& Agent::operator=(Agent&& other) {
  if (this != &other) {
    if (dlopen_handle_ != nullptr) {
      dlclose(dlopen_handle_);
    }
    name_ = std::move(other.name_);
    args_ = std::move(other.args_);
    dlopen_handle_ = other.dlopen_handle_;
    onload_ = other.onload_;
    onattach_ = other.onattach_;
    onunload_ = other.onunload_;
    other.dlopen_handle_ = nullptr;
    other.onload_ = nullptr;
    other.onattach_ = nullptr;
    other.onunload_ = nullptr;
  }
  return *this;
}

Agent::~Agent() {
  if (dlopen_handle_ != nullptr) {
    dlclose(dlopen_handle_);
  }
}

std::ostream& operator<<(std::ostream &os, const Agent* m) {
  return os << *m;
}

std::ostream& operator<<(std::ostream &os, Agent const& m) {
  return os << "Agent { name=\"" << m.name_ << "\", args=\"" << m.args_ << "\", handle="
            << m.dlopen_handle_ << " }";
}

}  // namespace ti
}  // namespace art
