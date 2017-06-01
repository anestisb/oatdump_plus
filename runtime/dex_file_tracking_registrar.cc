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

#include "dex_file_tracking_registrar.h"

// For dex tracking through poisoning. Note: Requires forcing sanitization. This is the reason for
// the ifdefs and early include.
#ifdef ART_DEX_FILE_ACCESS_TRACKING
#ifndef ART_ENABLE_ADDRESS_SANITIZER
#define ART_ENABLE_ADDRESS_SANITIZER
#endif
#endif
#include "base/memory_tool.h"

#include "base/logging.h"

namespace art {
namespace dex {
namespace tracking {

// If true, poison dex files to track accesses.
static constexpr bool kDexFileAccessTracking =
#ifdef ART_DEX_FILE_ACCESS_TRACKING
    true;
#else
    false;
#endif

void RegisterDexFile(const DexFile* const dex_file) {
  if (kDexFileAccessTracking && dex_file != nullptr) {
    LOG(ERROR) << dex_file->GetLocation() + " @ " << std::hex
               << reinterpret_cast<uintptr_t>(dex_file->Begin());
    MEMORY_TOOL_MAKE_NOACCESS(dex_file->Begin(), dex_file->Size());
  }
}

}  // namespace tracking
}  // namespace dex
}  // namespace art
