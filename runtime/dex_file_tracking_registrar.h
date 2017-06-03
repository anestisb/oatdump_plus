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

#ifndef ART_RUNTIME_DEX_FILE_TRACKING_REGISTRAR_H_
#define ART_RUNTIME_DEX_FILE_TRACKING_REGISTRAR_H_

#include "dex_file.h"

namespace art {
namespace dex {
namespace tracking {

void RegisterDexFile(const DexFile* const dex_file);

}  // namespace tracking
}  // namespace dex
}  // namespace art

#endif  // ART_RUNTIME_DEX_FILE_TRACKING_REGISTRAR_H_
