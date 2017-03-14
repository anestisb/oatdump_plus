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
 *
 * Header file of dex ir verifier.
 *
 * Compares two dex files at the IR level, allowing differences in layout, but not in data.
 */

#ifndef ART_DEXLAYOUT_DEX_VERIFY_H_
#define ART_DEXLAYOUT_DEX_VERIFY_H_

#include "dex_ir.h"

namespace art {

// Check that the output dex file contains the same data as the original.
// Compares the dex IR of both dex files. Allows the dex files to have different layouts.
bool VerifyOutputDexFile(dex_ir::Header* orig_header,
                         dex_ir::Header* output_header,
                         std::string* error_msg);
template<class T> bool VerifyIds(std::vector<std::unique_ptr<T>>& orig,
                                 std::vector<std::unique_ptr<T>>& output,
                                 const char* section_name,
                                 std::string* error_msg);
bool VerifyId(dex_ir::StringId* orig, dex_ir::StringId* output, std::string* error_msg);
bool VerifyId(dex_ir::TypeId* orig, dex_ir::TypeId* output, std::string* error_msg);
bool VerifyId(dex_ir::ProtoId* orig, dex_ir::ProtoId* output, std::string* error_msg);
bool VerifyId(dex_ir::FieldId* orig, dex_ir::FieldId* output, std::string* error_msg);
bool VerifyId(dex_ir::MethodId* orig, dex_ir::MethodId* output, std::string* error_msg);
bool VerifyTypeList(const dex_ir::TypeList* orig, const dex_ir::TypeList* output);

}  // namespace art

#endif  // ART_DEXLAYOUT_DEX_VERIFY_H_
