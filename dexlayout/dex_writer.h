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

#ifndef ART_DEXLAYOUT_DEX_WRITER_H_
#define ART_DEXLAYOUT_DEX_WRITER_H_

#include "base/unix_file/fd_file.h"
#include "dex_ir.h"
#include "mem_map.h"
#include "os.h"

namespace art {

class DexWriter {
 public:
  DexWriter(dex_ir::Header* header, MemMap* mem_map) : header_(header), mem_map_(mem_map) { }

  static void Output(dex_ir::Header* header, MemMap* mem_map);

 private:
  void WriteMemMap();

  size_t Write(const void* buffer, size_t length, size_t offset);
  size_t WriteSleb128(uint32_t value, size_t offset);
  size_t WriteUleb128(uint32_t value, size_t offset);
  size_t WriteEncodedValue(dex_ir::EncodedValue* encoded_value, size_t offset);
  size_t WriteEncodedValueHeader(int8_t value_type, size_t value_arg, size_t offset);
  size_t WriteEncodedArray(dex_ir::EncodedValueVector* values, size_t offset);
  size_t WriteEncodedAnnotation(dex_ir::EncodedAnnotation* annotation, size_t offset);
  size_t WriteEncodedFields(dex_ir::FieldItemVector* fields, size_t offset);
  size_t WriteEncodedMethods(dex_ir::MethodItemVector* methods, size_t offset);

  void WriteStrings();
  void WriteTypes();
  void WriteTypeLists();
  void WriteProtos();
  void WriteFields();
  void WriteMethods();
  void WriteEncodedArrays();
  void WriteAnnotations();
  void WriteAnnotationSets();
  void WriteAnnotationSetRefs();
  void WriteAnnotationsDirectories();
  void WriteDebugInfoItems();
  void WriteCodeItems();
  void WriteClasses();
  void WriteCallSites();
  void WriteMethodHandles();
  void WriteMapItem();
  void WriteHeader();

  dex_ir::Header* const header_;
  MemMap* const mem_map_;

  DISALLOW_COPY_AND_ASSIGN(DexWriter);
};

}  // namespace art

#endif  // ART_DEXLAYOUT_DEX_WRITER_H_
