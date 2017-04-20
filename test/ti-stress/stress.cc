/*
 * Copyright 2017 The Android Open Source Project
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
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sstream>

#include "jvmti.h"
#include "exec_utils.h"
#include "utils.h"

namespace art {

// Should we do a 'full_rewrite' with this test?
static constexpr bool kDoFullRewrite = true;

struct StressData {
  std::string dexter_cmd;
  std::string out_temp_dex;
  std::string in_temp_dex;
  bool vm_class_loader_initialized;
};

static void WriteToFile(const std::string& fname, jint data_len, const unsigned char* data) {
  std::ofstream file(fname, std::ios::binary | std::ios::out | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data), data_len);
  file.flush();
}

static bool ReadIntoBuffer(const std::string& fname, /*out*/std::vector<unsigned char>* data) {
  std::ifstream file(fname, std::ios::binary | std::ios::in);
  file.seekg(0, std::ios::end);
  size_t len = file.tellg();
  data->resize(len);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(data->data()), len);
  return len != 0;
}

// TODO rewrite later.
static bool DoExtractClassFromData(StressData* data,
                                   const std::string& class_name,
                                   jint in_len,
                                   const unsigned char* in_data,
                                   /*out*/std::vector<unsigned char>* dex) {
  // Write the dex file into a temporary file.
  WriteToFile(data->in_temp_dex, in_len, in_data);
  // Clear out file so even if something suppresses the exit value we will still detect dexter
  // failure.
  WriteToFile(data->out_temp_dex, 0, nullptr);
  // Have dexter do the extraction.
  std::vector<std::string> args;
  args.push_back(data->dexter_cmd);
  if (kDoFullRewrite) {
    args.push_back("-x");
    args.push_back("full_rewrite");
  }
  args.push_back("-e");
  args.push_back(class_name);
  args.push_back("-o");
  args.push_back(data->out_temp_dex);
  args.push_back(data->in_temp_dex);
  std::string error;
  if (ExecAndReturnCode(args, &error) != 0) {
    LOG(ERROR) << "unable to execute dexter: " << error;
    return false;
  }
  return ReadIntoBuffer(data->out_temp_dex, dex);
}

static void doJvmtiMethodBind(jvmtiEnv* jvmtienv,
                              JNIEnv* env,
                              jthread thread,
                              jmethodID m,
                              void* address,
                              /*out*/void** out_address) {
  *out_address = address;
  jvmtiThreadInfo info;
  if (thread == nullptr) {
    info.name = const_cast<char*>("<NULLPTR>");
  } else if (jvmtienv->GetThreadInfo(thread, &info) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Unable to get thread info!";
    info.name = const_cast<char*>("<UNKNOWN THREAD>");
  }
  char *fname, *fsig, *fgen;
  char *cname, *cgen;
  jclass klass = nullptr;
  if (jvmtienv->GetMethodDeclaringClass(m, &klass) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to get method declaring class!";
    return;
  }
  if (jvmtienv->GetMethodName(m, &fname, &fsig, &fgen) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to get method name!";
    env->DeleteLocalRef(klass);
    return;
  }
  if (jvmtienv->GetClassSignature(klass, &cname, &cgen) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to get class name!";
    env->DeleteLocalRef(klass);
    return;
  }
  LOG(INFO) << "Loading native method \"" << cname << "->" << fname << fsig << "\". Thread is "
            << info.name;
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(cname));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(cgen));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fname));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fsig));
  jvmtienv->Deallocate(reinterpret_cast<unsigned char*>(fgen));
  env->DeleteLocalRef(klass);
  return;
}

// The hook we are using.
void JNICALL ClassFileLoadHookSecretNoOp(jvmtiEnv* jvmti,
                                         JNIEnv* jni_env ATTRIBUTE_UNUSED,
                                         jclass class_being_redefined ATTRIBUTE_UNUSED,
                                         jobject loader ATTRIBUTE_UNUSED,
                                         const char* name,
                                         jobject protection_domain ATTRIBUTE_UNUSED,
                                         jint class_data_len,
                                         const unsigned char* class_data,
                                         jint* new_class_data_len,
                                         unsigned char** new_class_data) {
  std::vector<unsigned char> out;
  std::string name_str(name);
  // Make the jvmti semi-descriptor into the java style descriptor (though with $ for inner
  // classes).
  std::replace(name_str.begin(), name_str.end(), '/', '.');
  StressData* data = nullptr;
  CHECK_EQ(jvmti->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)),
           JVMTI_ERROR_NONE);
  if (!data->vm_class_loader_initialized) {
    LOG(WARNING) << "Ignoring load of class " << name << " because VMClassLoader is not yet "
                 << "initialized. Transforming this class could cause spurious test failures.";
    return;
  } else if (DoExtractClassFromData(data, name_str, class_data_len, class_data, /*out*/ &out)) {
    LOG(INFO) << "Extracted class: " << name;
    unsigned char* new_data;
    CHECK_EQ(JVMTI_ERROR_NONE, jvmti->Allocate(out.size(), &new_data));
    memcpy(new_data, out.data(), out.size());
    *new_class_data_len = static_cast<jint>(out.size());
    *new_class_data = new_data;
  } else {
    std::cerr << "Unable to extract class " << name_str << std::endl;
    *new_class_data_len = 0;
    *new_class_data = nullptr;
  }
}

// Options are ${DEXTER_BINARY},${TEMP_FILE_1},${TEMP_FILE_2}
static void ReadOptions(StressData* data, char* options) {
  std::string ops(options);
  data->dexter_cmd = ops.substr(0, ops.find(','));
  ops = ops.substr(ops.find(',') + 1);
  data->in_temp_dex = ops.substr(0, ops.find(','));
  ops = ops.substr(ops.find(',') + 1);
  data->out_temp_dex = ops;
}

// We need to make sure that VMClassLoader is initialized before we start redefining anything since
// it can give (non-fatal) error messages if it's initialized after we've redefined BCP classes.
// These error messages are expected and no problem but they will mess up our testing
// infrastructure.
static void JNICALL EnsureVMClassloaderInitializedCB(jvmtiEnv *jvmti_env,
                                                     JNIEnv* jni_env,
                                                     jthread thread ATTRIBUTE_UNUSED) {
  // Load the VMClassLoader class. We will get a ClassNotFound exception because we don't have
  // visibility but the class will be loaded behind the scenes.
  LOG(INFO) << "manual load & initialization of class java/lang/VMClassLoader!";
  jclass klass = jni_env->FindClass("java/lang/VMClassLoader");
  if (klass == nullptr) {
    // Probably on RI. Clear the exception so we can continue but don't mark vmclassloader as
    // initialized.
    LOG(WARNING) << "Unable to find VMClassLoader class!";
    jni_env->ExceptionClear();
  } else {
    // GetMethodID is spec'd to cause the class to be initialized.
    jni_env->GetMethodID(klass, "hashCode", "()I");
    jni_env->DeleteLocalRef(klass);
    StressData* data = nullptr;
    CHECK_EQ(jvmti_env->GetEnvironmentLocalStorage(reinterpret_cast<void**>(&data)),
             JVMTI_ERROR_NONE);
    data->vm_class_loader_initialized = true;
  }
}

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm,
                                               char* options,
                                               void* reserved ATTRIBUTE_UNUSED) {
  jvmtiEnv* jvmti = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_0)) {
    LOG(ERROR) << "Unable to get jvmti env.";
    return 1;
  }
  StressData* data = nullptr;
  if (JVMTI_ERROR_NONE != jvmti->Allocate(sizeof(StressData),
                                          reinterpret_cast<unsigned char**>(&data))) {
    LOG(ERROR) << "Unable to allocate data for stress test.";
    return 1;
  }
  memset(data, 0, sizeof(StressData));
  // Read the options into the static variables that hold them.
  ReadOptions(data, options);
  // Save the data
  if (JVMTI_ERROR_NONE != jvmti->SetEnvironmentLocalStorage(data)) {
    LOG(ERROR) << "Unable to save stress test data.";
    return 1;
  }

  // Just get all capabilities.
  jvmtiCapabilities caps;
  jvmti->GetPotentialCapabilities(&caps);
  jvmti->AddCapabilities(&caps);

  // Set callbacks.
  jvmtiEventCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.ClassFileLoadHook = ClassFileLoadHookSecretNoOp;
  cb.NativeMethodBind = doJvmtiMethodBind;
  cb.VMInit = EnsureVMClassloaderInitializedCB;
  if (jvmti->SetEventCallbacks(&cb, sizeof(cb)) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to set class file load hook cb!";
    return 1;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_NATIVE_METHOD_BIND,
                                      nullptr) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to enable JVMTI_EVENT_NATIVE_METHOD_BIND event!";
    return 1;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_VM_INIT,
                                      nullptr) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to enable JVMTI_EVENT_VM_INIT event!";
    return 1;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                                      nullptr) != JVMTI_ERROR_NONE) {
    LOG(ERROR) << "Unable to enable CLASS_FILE_LOAD_HOOK event!";
    return 1;
  }
  return 0;
}

}  // namespace art
