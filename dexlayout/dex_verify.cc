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
 * Implementation file of dex ir verifier.
 *
 * Compares two dex files at the IR level, allowing differences in layout, but not in data.
 */

#include "dex_verify.h"

#include "android-base/stringprintf.h"

namespace art {

using android::base::StringPrintf;

bool VerifyOutputDexFile(dex_ir::Header* orig_header,
                         dex_ir::Header* output_header,
                         std::string* error_msg) {
  dex_ir::Collections& orig = orig_header->GetCollections();
  dex_ir::Collections& output = output_header->GetCollections();

  // Compare all id sections.
  if (!VerifyIds(orig.StringIds(), output.StringIds(), "string ids", error_msg) ||
      !VerifyIds(orig.TypeIds(), output.TypeIds(), "type ids", error_msg) ||
      !VerifyIds(orig.ProtoIds(), output.ProtoIds(), "proto ids", error_msg) ||
      !VerifyIds(orig.FieldIds(), output.FieldIds(), "field ids", error_msg) ||
      !VerifyIds(orig.MethodIds(), output.MethodIds(), "method ids", error_msg)) {
    return false;
  }
  return true;
}

template<class T> bool VerifyIds(std::vector<std::unique_ptr<T>>& orig,
                                 std::vector<std::unique_ptr<T>>& output,
                                 const char* section_name,
                                 std::string* error_msg) {
  if (orig.size() != output.size()) {
    *error_msg = StringPrintf(
        "Mismatched size for %s section, %zu vs %zu.", section_name, orig.size(), output.size());
    return false;
  }
  for (size_t i = 0; i < orig.size(); ++i) {
    if (!VerifyId(orig[i].get(), output[i].get(), error_msg)) {
      return false;
    }
  }
  return true;
}

bool VerifyId(dex_ir::StringId* orig, dex_ir::StringId* output, std::string* error_msg) {
  if (strcmp(orig->Data(), output->Data()) != 0) {
    *error_msg = StringPrintf("Mismatched string data for string id %u @ orig offset %x, %s vs %s.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Data(),
                              output->Data());
    return false;
  }
  return true;
}

bool VerifyId(dex_ir::TypeId* orig, dex_ir::TypeId* output, std::string* error_msg) {
  if (orig->GetStringId()->GetIndex() != output->GetStringId()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched string index for type id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->GetStringId()->GetIndex(),
                              output->GetStringId()->GetIndex());
    return false;
  }
  return true;
}

bool VerifyId(dex_ir::ProtoId* orig, dex_ir::ProtoId* output, std::string* error_msg) {
  if (orig->Shorty()->GetIndex() != output->Shorty()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched string index for proto id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Shorty()->GetIndex(),
                              output->Shorty()->GetIndex());
    return false;
  }
  if (orig->ReturnType()->GetIndex() != output->ReturnType()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched type index for proto id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->ReturnType()->GetIndex(),
                              output->ReturnType()->GetIndex());
    return false;
  }
  if (!VerifyTypeList(orig->Parameters(), output->Parameters())) {
    *error_msg = StringPrintf("Mismatched type list for proto id %u @ orig offset %x.",
                              orig->GetIndex(),
                              orig->GetOffset());
  }
  return true;
}

bool VerifyId(dex_ir::FieldId* orig, dex_ir::FieldId* output, std::string* error_msg) {
  if (orig->Class()->GetIndex() != output->Class()->GetIndex()) {
    *error_msg =
        StringPrintf("Mismatched class type index for field id %u @ orig offset %x, %u vs %u.",
                     orig->GetIndex(),
                     orig->GetOffset(),
                     orig->Class()->GetIndex(),
                     output->Class()->GetIndex());
    return false;
  }
  if (orig->Type()->GetIndex() != output->Type()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched type index for field id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Class()->GetIndex(),
                              output->Class()->GetIndex());
    return false;
  }
  if (orig->Name()->GetIndex() != output->Name()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched string index for field id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Name()->GetIndex(),
                              output->Name()->GetIndex());
    return false;
  }
  return true;
}

bool VerifyId(dex_ir::MethodId* orig, dex_ir::MethodId* output, std::string* error_msg) {
  if (orig->Class()->GetIndex() != output->Class()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched type index for method id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Class()->GetIndex(),
                              output->Class()->GetIndex());
    return false;
  }
  if (orig->Proto()->GetIndex() != output->Proto()->GetIndex()) {
    *error_msg = StringPrintf("Mismatched proto index for method id %u @ orig offset %x, %u vs %u.",
                              orig->GetIndex(),
                              orig->GetOffset(),
                              orig->Class()->GetIndex(),
                              output->Class()->GetIndex());
    return false;
  }
  if (orig->Name()->GetIndex() != output->Name()->GetIndex()) {
    *error_msg =
        StringPrintf("Mismatched string index for method id %u @ orig offset %x, %u vs %u.",
                     orig->GetIndex(),
                     orig->GetOffset(),
                     orig->Name()->GetIndex(),
                     output->Name()->GetIndex());
    return false;
  }
  return true;
}

bool VerifyTypeList(const dex_ir::TypeList* orig, const dex_ir::TypeList* output) {
  if (orig == nullptr || output == nullptr) {
    return orig == output;
  }
  const dex_ir::TypeIdVector* orig_list = orig->GetTypeList();
  const dex_ir::TypeIdVector* output_list = output->GetTypeList();
  if (orig_list->size() != output_list->size()) {
    return false;
  }
  for (size_t i = 0; i < orig_list->size(); ++i) {
    if ((*orig_list)[i]->GetIndex() != (*output_list)[i]->GetIndex()) {
      return false;
    }
  }
  return true;
}

}  // namespace art
