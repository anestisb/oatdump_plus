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
 *
 * Header file of an in-memory representation of DEX files.
 */

#include <stdint.h>
#include <vector>

#include "dex_ir_builder.h"

namespace art {
namespace dex_ir {

Header* DexIrBuilder(const DexFile& dex_file) {
  const DexFile::Header& disk_header = dex_file.GetHeader();
  Header* header = new Header(disk_header.magic_,
                              disk_header.checksum_,
                              disk_header.signature_,
                              disk_header.endian_tag_,
                              disk_header.file_size_,
                              disk_header.header_size_,
                              disk_header.link_size_,
                              disk_header.link_off_,
                              disk_header.data_size_,
                              disk_header.data_off_);
  Collections& collections = header->GetCollections();
  // Walk the rest of the header fields.
  // StringId table.
  collections.SetStringIdsOffset(disk_header.string_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumStringIds(); ++i) {
    collections.CreateStringId(dex_file, i);
  }
  // TypeId table.
  collections.SetTypeIdsOffset(disk_header.type_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumTypeIds(); ++i) {
    collections.CreateTypeId(dex_file, i);
  }
  // ProtoId table.
  collections.SetProtoIdsOffset(disk_header.proto_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumProtoIds(); ++i) {
    collections.CreateProtoId(dex_file, i);
  }
  // FieldId table.
  collections.SetFieldIdsOffset(disk_header.field_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumFieldIds(); ++i) {
    collections.CreateFieldId(dex_file, i);
  }
  // MethodId table.
  collections.SetMethodIdsOffset(disk_header.method_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumMethodIds(); ++i) {
    collections.CreateMethodId(dex_file, i);
  }
  // ClassDef table.
  collections.SetClassDefsOffset(disk_header.class_defs_off_);
  for (uint32_t i = 0; i < dex_file.NumClassDefs(); ++i) {
    collections.CreateClassDef(dex_file, i);
  }

  return header;
}

}  // namespace dex_ir
}  // namespace art
