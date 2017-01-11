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

#include "ti_properties.h"

#include <string.h>
#include <vector>

#include "art_jvmti.h"
#include "runtime.h"

namespace openjdkjvmti {

// Hardcoded properties. Tests ensure that these are consistent with libcore's view, as seen
// in System.java and AndroidHardcodedSystemProperties.java.
static constexpr const char* kProperties[][2] = {
    // Recommended by the spec.
    { "java.vm.vendor", "The Android Project" },
    { "java.vm.version", "2.1.0" },  // This is Runtime::GetVersion().
    { "java.vm.name", "Dalvik" },
    // Android does not provide java.vm.info.
    //
    // These are other values provided by AndroidHardcodedSystemProperties.
    { "java.class.version", "50.0" },
    { "java.version", "0" },
    { "java.compiler", "" },
    { "java.ext.dirs", "" },

    { "java.specification.name", "Dalvik Core Library" },
    { "java.specification.vendor", "The Android Project" },
    { "java.specification.version", "0.9" },

    { "java.vendor", "The Android Project" },
    { "java.vendor.url", "http://www.android.com/" },
    { "java.vm.name", "Dalvik" },
    { "java.vm.specification.name", "Dalvik Virtual Machine Specification" },
    { "java.vm.specification.vendor", "The Android Project" },
    { "java.vm.specification.version", "0.9" },
    { "java.vm.vendor", "The Android Project" },

    { "java.vm.vendor.url", "http://www.android.com/" },

    { "java.net.preferIPv6Addresses", "false" },

    { "file.encoding", "UTF-8" },

    { "file.separator", "/" },
    { "line.separator", "\n" },
    { "path.separator", ":" },

    { "os.name", "Linux" },
};
static constexpr size_t kPropertiesSize = arraysize(kProperties);
static constexpr const char* kPropertyLibraryPath = "java.library.path";
static constexpr const char* kPropertyClassPath = "java.class.path";

static jvmtiError Copy(jvmtiEnv* env, const char* in, char** out) {
  unsigned char* data = nullptr;
  jvmtiError result = CopyString(env, in, &data);
  *out = reinterpret_cast<char*>(data);
  return result;
}

jvmtiError PropertiesUtil::GetSystemProperties(jvmtiEnv* env,
                                               jint* count_ptr,
                                               char*** property_ptr) {
  if (count_ptr == nullptr || property_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }
  unsigned char* array_data;
  jvmtiError array_alloc_result = env->Allocate((kPropertiesSize + 2) * sizeof(char*), &array_data);
  if (array_alloc_result != ERR(NONE)) {
    return array_alloc_result;
  }
  JvmtiUniquePtr array_data_ptr = MakeJvmtiUniquePtr(env, array_data);
  char** array = reinterpret_cast<char**>(array_data);

  std::vector<JvmtiUniquePtr> property_copies;

  {
    char* libpath_data;
    jvmtiError libpath_result = Copy(env, kPropertyLibraryPath, &libpath_data);
    if (libpath_result != ERR(NONE)) {
      return libpath_result;
    }
    array[0] = libpath_data;
    property_copies.push_back(MakeJvmtiUniquePtr(env, libpath_data));
  }

  {
    char* classpath_data;
    jvmtiError classpath_result = Copy(env, kPropertyClassPath, &classpath_data);
    if (classpath_result != ERR(NONE)) {
      return classpath_result;
    }
    array[1] = classpath_data;
    property_copies.push_back(MakeJvmtiUniquePtr(env, classpath_data));
  }

  for (size_t i = 0; i != kPropertiesSize; ++i) {
    char* data;
    jvmtiError data_result = Copy(env, kProperties[i][0], &data);
    if (data_result != ERR(NONE)) {
      return data_result;
    }
    array[i + 2] = data;
    property_copies.push_back(MakeJvmtiUniquePtr(env, data));
  }

  // Everything is OK, release the data.
  array_data_ptr.release();
  for (auto& uptr : property_copies) {
    uptr.release();
  }

  *count_ptr = kPropertiesSize + 2;
  *property_ptr = array;

  return ERR(NONE);
}

jvmtiError PropertiesUtil::GetSystemProperty(jvmtiEnv* env,
                                             const char* property,
                                             char** value_ptr) {
  if (property == nullptr || value_ptr == nullptr) {
    return ERR(NULL_POINTER);
  }

  if (strcmp(property, kPropertyLibraryPath) == 0) {
    // TODO: In the live phase, we should probably compare to System.getProperty. java.library.path
    //       may not be set initially, and is then freely modifiable.
    const std::vector<std::string>& runtime_props = art::Runtime::Current()->GetProperties();
    for (const std::string& prop_assignment : runtime_props) {
      size_t assign_pos = prop_assignment.find('=');
      if (assign_pos != std::string::npos && assign_pos > 0) {
        if (prop_assignment.substr(0, assign_pos) == kPropertyLibraryPath) {
          return Copy(env, prop_assignment.substr(assign_pos + 1).c_str(), value_ptr);
        }
      }
    }
    return ERR(NOT_AVAILABLE);
  }

  if (strcmp(property, kPropertyClassPath) == 0) {
    return Copy(env, art::Runtime::Current()->GetClassPathString().c_str(), value_ptr);
  }

  for (size_t i = 0; i != kPropertiesSize; ++i) {
    if (strcmp(property, kProperties[i][0]) == 0) {
      return Copy(env, kProperties[i][1], value_ptr);
    }
  }

  return ERR(NOT_AVAILABLE);
}

jvmtiError PropertiesUtil::SetSystemProperty(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                             const char* property ATTRIBUTE_UNUSED,
                                             const char* value ATTRIBUTE_UNUSED) {
  // We do not allow manipulation of any property here.
  return ERR(NOT_AVAILABLE);
}

}  // namespace openjdkjvmti
