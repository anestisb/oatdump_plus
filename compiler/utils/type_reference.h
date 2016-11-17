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

#ifndef ART_COMPILER_UTILS_TYPE_REFERENCE_H_
#define ART_COMPILER_UTILS_TYPE_REFERENCE_H_

#include <stdint.h>

#include "base/logging.h"
#include "dex_file_types.h"
#include "string_reference.h"

namespace art {

class DexFile;

// A type is located by its DexFile and the string_ids_ table index into that DexFile.
struct TypeReference {
  TypeReference(const DexFile* file, dex::TypeIndex index) : dex_file(file), type_index(index) { }

  const DexFile* dex_file;
  dex::TypeIndex type_index;
};

// Compare the actual referenced type names. Used for type reference deduplication.
struct TypeReferenceValueComparator {
  bool operator()(TypeReference tr1, TypeReference tr2) const {
    // Note that we want to deduplicate identical boot image types even if they are
    // referenced by different dex files, so we simply compare the descriptors.
    StringReference sr1(tr1.dex_file, tr1.dex_file->GetTypeId(tr1.type_index).descriptor_idx_);
    StringReference sr2(tr2.dex_file, tr2.dex_file->GetTypeId(tr2.type_index).descriptor_idx_);
    return StringReferenceValueComparator()(sr1, sr2);
  }
};

}  // namespace art

#endif  // ART_COMPILER_UTILS_TYPE_REFERENCE_H_
