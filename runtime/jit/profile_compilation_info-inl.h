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

#ifndef ART_RUNTIME_JIT_PROFILE_COMPILATION_INFO_INL_H_
#define ART_RUNTIME_JIT_PROFILE_COMPILATION_INFO_INL_H_

#include "profile_compilation_info.h"

namespace art {

template <class Iterator>
inline bool ProfileCompilationInfo::AddMethodsForDex(bool startup,
                                                     bool hot,
                                                     const DexFile* dex_file,
                                                     Iterator index_begin,
                                                     Iterator index_end) {
  DexFileData* data = GetOrAddDexFileData(dex_file);
  if (data == nullptr) {
    return false;
  }
  for (auto it = index_begin; it != index_end; ++it) {
    DCHECK_LT(*it, data->num_method_ids);
    data->AddSampledMethod(startup, *it);
    if (hot) {
      data->FindOrAddMethod(*it);
    }
  }
  return true;
}

template <class Iterator>
inline bool ProfileCompilationInfo::AddClassesForDex(const DexFile* dex_file,
                                                     Iterator index_begin,
                                                     Iterator index_end) {
  DexFileData* data = GetOrAddDexFileData(dex_file);
  if (data == nullptr) {
    return false;
  }
  data->class_set.insert(index_begin, index_end);
  return true;
}

}  // namespace art

#endif  // ART_RUNTIME_JIT_PROFILE_COMPILATION_INFO_INL_H_
