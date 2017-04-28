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

#include <queue>
#include <vector>

#include "dex_writer.h"
#include "utf.h"

namespace art {

size_t EncodeIntValue(int32_t value, uint8_t* buffer) {
  size_t length = 0;
  if (value >= 0) {
    while (value > 0x7f) {
      buffer[length++] = static_cast<uint8_t>(value);
      value >>= 8;
    }
  } else {
    while (value < -0x80) {
      buffer[length++] = static_cast<uint8_t>(value);
      value >>= 8;
    }
  }
  buffer[length++] = static_cast<uint8_t>(value);
  return length;
}

size_t EncodeUIntValue(uint32_t value, uint8_t* buffer) {
  size_t length = 0;
  do {
    buffer[length++] = static_cast<uint8_t>(value);
    value >>= 8;
  } while (value != 0);
  return length;
}

size_t EncodeLongValue(int64_t value, uint8_t* buffer) {
  size_t length = 0;
  if (value >= 0) {
    while (value > 0x7f) {
      buffer[length++] = static_cast<uint8_t>(value);
      value >>= 8;
    }
  } else {
    while (value < -0x80) {
      buffer[length++] = static_cast<uint8_t>(value);
      value >>= 8;
    }
  }
  buffer[length++] = static_cast<uint8_t>(value);
  return length;
}

union FloatUnion {
  float f_;
  uint32_t i_;
};

size_t EncodeFloatValue(float value, uint8_t* buffer) {
  FloatUnion float_union;
  float_union.f_ = value;
  uint32_t int_value = float_union.i_;
  size_t index = 3;
  do {
    buffer[index--] = int_value >> 24;
    int_value <<= 8;
  } while (int_value != 0);
  return 3 - index;
}

union DoubleUnion {
  double d_;
  uint64_t l_;
};

size_t EncodeDoubleValue(double value, uint8_t* buffer) {
  DoubleUnion double_union;
  double_union.d_ = value;
  uint64_t long_value = double_union.l_;
  size_t index = 7;
  do {
    buffer[index--] = long_value >> 56;
    long_value <<= 8;
  } while (long_value != 0);
  return 7 - index;
}

size_t DexWriter::Write(const void* buffer, size_t length, size_t offset) {
  DCHECK_LE(offset + length, mem_map_->Size());
  memcpy(mem_map_->Begin() + offset, buffer, length);
  return length;
}

size_t DexWriter::WriteSleb128(uint32_t value, size_t offset) {
  uint8_t buffer[8];
  EncodeSignedLeb128(buffer, value);
  return Write(buffer, SignedLeb128Size(value), offset);
}

size_t DexWriter::WriteUleb128(uint32_t value, size_t offset) {
  uint8_t buffer[8];
  EncodeUnsignedLeb128(buffer, value);
  return Write(buffer, UnsignedLeb128Size(value), offset);
}

size_t DexWriter::WriteEncodedValue(dex_ir::EncodedValue* encoded_value, size_t offset) {
  size_t original_offset = offset;
  size_t start = 0;
  size_t length;
  uint8_t buffer[8];
  int8_t type = encoded_value->Type();
  switch (type) {
    case DexFile::kDexAnnotationByte:
      length = EncodeIntValue(encoded_value->GetByte(), buffer);
      break;
    case DexFile::kDexAnnotationShort:
      length = EncodeIntValue(encoded_value->GetShort(), buffer);
      break;
    case DexFile::kDexAnnotationChar:
      length = EncodeUIntValue(encoded_value->GetChar(), buffer);
      break;
    case DexFile::kDexAnnotationInt:
      length = EncodeIntValue(encoded_value->GetInt(), buffer);
      break;
    case DexFile::kDexAnnotationLong:
      length = EncodeLongValue(encoded_value->GetLong(), buffer);
      break;
    case DexFile::kDexAnnotationFloat:
      length = EncodeFloatValue(encoded_value->GetFloat(), buffer);
      start = 4 - length;
      break;
    case DexFile::kDexAnnotationDouble:
      length = EncodeDoubleValue(encoded_value->GetDouble(), buffer);
      start = 8 - length;
      break;
    case DexFile::kDexAnnotationMethodType:
      length = EncodeUIntValue(encoded_value->GetProtoId()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationMethodHandle:
      length = EncodeUIntValue(encoded_value->GetMethodHandle()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationString:
      length = EncodeUIntValue(encoded_value->GetStringId()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationType:
      length = EncodeUIntValue(encoded_value->GetTypeId()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum:
      length = EncodeUIntValue(encoded_value->GetFieldId()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationMethod:
      length = EncodeUIntValue(encoded_value->GetMethodId()->GetIndex(), buffer);
      break;
    case DexFile::kDexAnnotationArray:
      offset += WriteEncodedValueHeader(type, 0, offset);
      offset += WriteEncodedArray(encoded_value->GetEncodedArray()->GetEncodedValues(), offset);
      return offset - original_offset;
    case DexFile::kDexAnnotationAnnotation:
      offset += WriteEncodedValueHeader(type, 0, offset);
      offset += WriteEncodedAnnotation(encoded_value->GetEncodedAnnotation(), offset);
      return offset - original_offset;
    case DexFile::kDexAnnotationNull:
      return WriteEncodedValueHeader(type, 0, offset);
    case DexFile::kDexAnnotationBoolean:
      return WriteEncodedValueHeader(type, encoded_value->GetBoolean() ? 1 : 0, offset);
    default:
      return 0;
  }
  offset += WriteEncodedValueHeader(type, length - 1, offset);
  offset += Write(buffer + start, length, offset);
  return offset - original_offset;
}

size_t DexWriter::WriteEncodedValueHeader(int8_t value_type, size_t value_arg, size_t offset) {
  uint8_t buffer[1] = { static_cast<uint8_t>((value_arg << 5) | value_type) };
  return Write(buffer, sizeof(uint8_t), offset);
}

size_t DexWriter::WriteEncodedArray(dex_ir::EncodedValueVector* values, size_t offset) {
  size_t original_offset = offset;
  offset += WriteUleb128(values->size(), offset);
  for (std::unique_ptr<dex_ir::EncodedValue>& value : *values) {
    offset += WriteEncodedValue(value.get(), offset);
  }
  return offset - original_offset;
}

size_t DexWriter::WriteEncodedAnnotation(dex_ir::EncodedAnnotation* annotation, size_t offset) {
  size_t original_offset = offset;
  offset += WriteUleb128(annotation->GetType()->GetIndex(), offset);
  offset += WriteUleb128(annotation->GetAnnotationElements()->size(), offset);
  for (std::unique_ptr<dex_ir::AnnotationElement>& annotation_element :
      *annotation->GetAnnotationElements()) {
    offset += WriteUleb128(annotation_element->GetName()->GetIndex(), offset);
    offset += WriteEncodedValue(annotation_element->GetValue(), offset);
  }
  return offset - original_offset;
}

size_t DexWriter::WriteEncodedFields(dex_ir::FieldItemVector* fields, size_t offset) {
  size_t original_offset = offset;
  uint32_t prev_index = 0;
  for (std::unique_ptr<dex_ir::FieldItem>& field : *fields) {
    uint32_t index = field->GetFieldId()->GetIndex();
    offset += WriteUleb128(index - prev_index, offset);
    offset += WriteUleb128(field->GetAccessFlags(), offset);
    prev_index = index;
  }
  return offset - original_offset;
}

size_t DexWriter::WriteEncodedMethods(dex_ir::MethodItemVector* methods, size_t offset) {
  size_t original_offset = offset;
  uint32_t prev_index = 0;
  for (std::unique_ptr<dex_ir::MethodItem>& method : *methods) {
    uint32_t index = method->GetMethodId()->GetIndex();
    uint32_t code_off = method->GetCodeItem() == nullptr ? 0 : method->GetCodeItem()->GetOffset();
    offset += WriteUleb128(index - prev_index, offset);
    offset += WriteUleb128(method->GetAccessFlags(), offset);
    offset += WriteUleb128(code_off, offset);
    prev_index = index;
  }
  return offset - original_offset;
}

void DexWriter::WriteStrings() {
  uint32_t string_data_off[1];
  for (std::unique_ptr<dex_ir::StringId>& string_id : header_->GetCollections().StringIds()) {
    string_data_off[0] = string_id->DataItem()->GetOffset();
    Write(string_data_off, string_id->GetSize(), string_id->GetOffset());
  }

  for (auto& string_data_pair : header_->GetCollections().StringDatas()) {
    std::unique_ptr<dex_ir::StringData>& string_data = string_data_pair.second;
    uint32_t offset = string_data->GetOffset();
    offset += WriteUleb128(CountModifiedUtf8Chars(string_data->Data()), offset);
    Write(string_data->Data(), strlen(string_data->Data()), offset);
  }
}

void DexWriter::WriteTypes() {
  uint32_t descriptor_idx[1];
  for (std::unique_ptr<dex_ir::TypeId>& type_id : header_->GetCollections().TypeIds()) {
    descriptor_idx[0] = type_id->GetStringId()->GetIndex();
    Write(descriptor_idx, type_id->GetSize(), type_id->GetOffset());
  }
}

void DexWriter::WriteTypeLists() {
  uint32_t size[1];
  uint16_t list[1];
  for (auto& type_list_pair : header_->GetCollections().TypeLists()) {
    std::unique_ptr<dex_ir::TypeList>& type_list = type_list_pair.second;
    size[0] = type_list->GetTypeList()->size();
    uint32_t offset = type_list->GetOffset();
    offset += Write(size, sizeof(uint32_t), offset);
    for (const dex_ir::TypeId* type_id : *type_list->GetTypeList()) {
      list[0] = type_id->GetIndex();
      offset += Write(list, sizeof(uint16_t), offset);
    }
  }
}

void DexWriter::WriteProtos() {
  uint32_t buffer[3];
  for (std::unique_ptr<dex_ir::ProtoId>& proto_id : header_->GetCollections().ProtoIds()) {
    buffer[0] = proto_id->Shorty()->GetIndex();
    buffer[1] = proto_id->ReturnType()->GetIndex();
    buffer[2] = proto_id->Parameters() == nullptr ? 0 : proto_id->Parameters()->GetOffset();
    Write(buffer, proto_id->GetSize(), proto_id->GetOffset());
  }
}

void DexWriter::WriteFields() {
  uint16_t buffer[4];
  for (std::unique_ptr<dex_ir::FieldId>& field_id : header_->GetCollections().FieldIds()) {
    buffer[0] = field_id->Class()->GetIndex();
    buffer[1] = field_id->Type()->GetIndex();
    buffer[2] = field_id->Name()->GetIndex();
    buffer[3] = field_id->Name()->GetIndex() >> 16;
    Write(buffer, field_id->GetSize(), field_id->GetOffset());
  }
}

void DexWriter::WriteMethods() {
  uint16_t buffer[4];
  for (std::unique_ptr<dex_ir::MethodId>& method_id : header_->GetCollections().MethodIds()) {
    buffer[0] = method_id->Class()->GetIndex();
    buffer[1] = method_id->Proto()->GetIndex();
    buffer[2] = method_id->Name()->GetIndex();
    buffer[3] = method_id->Name()->GetIndex() >> 16;
    Write(buffer, method_id->GetSize(), method_id->GetOffset());
  }
}

void DexWriter::WriteEncodedArrays() {
  for (auto& encoded_array_pair : header_->GetCollections().EncodedArrayItems()) {
    std::unique_ptr<dex_ir::EncodedArrayItem>& encoded_array = encoded_array_pair.second;
    WriteEncodedArray(encoded_array->GetEncodedValues(), encoded_array->GetOffset());
  }
}

void DexWriter::WriteAnnotations() {
  uint8_t visibility[1];
  for (auto& annotation_pair : header_->GetCollections().AnnotationItems()) {
    std::unique_ptr<dex_ir::AnnotationItem>& annotation = annotation_pair.second;
    visibility[0] = annotation->GetVisibility();
    size_t offset = annotation->GetOffset();
    offset += Write(visibility, sizeof(uint8_t), offset);
    WriteEncodedAnnotation(annotation->GetAnnotation(), offset);
  }
}

void DexWriter::WriteAnnotationSets() {
  uint32_t size[1];
  uint32_t annotation_off[1];
  for (auto& annotation_set_pair : header_->GetCollections().AnnotationSetItems()) {
    std::unique_ptr<dex_ir::AnnotationSetItem>& annotation_set = annotation_set_pair.second;
    size[0] = annotation_set->GetItems()->size();
    size_t offset = annotation_set->GetOffset();
    offset += Write(size, sizeof(uint32_t), offset);
    for (dex_ir::AnnotationItem* annotation : *annotation_set->GetItems()) {
      annotation_off[0] = annotation->GetOffset();
      offset += Write(annotation_off, sizeof(uint32_t), offset);
    }
  }
}

void DexWriter::WriteAnnotationSetRefs() {
  uint32_t size[1];
  uint32_t annotations_off[1];
  for (auto& anno_set_ref_pair : header_->GetCollections().AnnotationSetRefLists()) {
    std::unique_ptr<dex_ir::AnnotationSetRefList>& annotation_set_ref = anno_set_ref_pair.second;
    size[0] = annotation_set_ref->GetItems()->size();
    size_t offset = annotation_set_ref->GetOffset();
    offset += Write(size, sizeof(uint32_t), offset);
    for (dex_ir::AnnotationSetItem* annotation_set : *annotation_set_ref->GetItems()) {
      annotations_off[0] = annotation_set == nullptr ? 0 : annotation_set->GetOffset();
      offset += Write(annotations_off, sizeof(uint32_t), offset);
    }
  }
}

void DexWriter::WriteAnnotationsDirectories() {
  uint32_t directory_buffer[4];
  uint32_t annotation_buffer[2];
  for (auto& annotations_directory_pair : header_->GetCollections().AnnotationsDirectoryItems()) {
    std::unique_ptr<dex_ir::AnnotationsDirectoryItem>& annotations_directory =
        annotations_directory_pair.second;
    directory_buffer[0] = annotations_directory->GetClassAnnotation() == nullptr ? 0 :
        annotations_directory->GetClassAnnotation()->GetOffset();
    directory_buffer[1] = annotations_directory->GetFieldAnnotations() == nullptr ? 0 :
        annotations_directory->GetFieldAnnotations()->size();
    directory_buffer[2] = annotations_directory->GetMethodAnnotations() == nullptr ? 0 :
        annotations_directory->GetMethodAnnotations()->size();
    directory_buffer[3] = annotations_directory->GetParameterAnnotations() == nullptr ? 0 :
        annotations_directory->GetParameterAnnotations()->size();
    uint32_t offset = annotations_directory->GetOffset();
    offset += Write(directory_buffer, 4 * sizeof(uint32_t), offset);
    if (annotations_directory->GetFieldAnnotations() != nullptr) {
      for (std::unique_ptr<dex_ir::FieldAnnotation>& field :
          *annotations_directory->GetFieldAnnotations()) {
        annotation_buffer[0] = field->GetFieldId()->GetIndex();
        annotation_buffer[1] = field->GetAnnotationSetItem()->GetOffset();
        offset += Write(annotation_buffer, 2 * sizeof(uint32_t), offset);
      }
    }
    if (annotations_directory->GetMethodAnnotations() != nullptr) {
      for (std::unique_ptr<dex_ir::MethodAnnotation>& method :
          *annotations_directory->GetMethodAnnotations()) {
        annotation_buffer[0] = method->GetMethodId()->GetIndex();
        annotation_buffer[1] = method->GetAnnotationSetItem()->GetOffset();
        offset += Write(annotation_buffer, 2 * sizeof(uint32_t), offset);
      }
    }
    if (annotations_directory->GetParameterAnnotations() != nullptr) {
      for (std::unique_ptr<dex_ir::ParameterAnnotation>& parameter :
          *annotations_directory->GetParameterAnnotations()) {
        annotation_buffer[0] = parameter->GetMethodId()->GetIndex();
        annotation_buffer[1] = parameter->GetAnnotations()->GetOffset();
        offset += Write(annotation_buffer, 2 * sizeof(uint32_t), offset);
      }
    }
  }
}

void DexWriter::WriteDebugInfoItems() {
  for (auto& debug_info_pair : header_->GetCollections().DebugInfoItems()) {
    std::unique_ptr<dex_ir::DebugInfoItem>& debug_info = debug_info_pair.second;
    Write(debug_info->GetDebugInfo(), debug_info->GetDebugInfoSize(), debug_info->GetOffset());
  }
}

void DexWriter::WriteCodeItems() {
  uint16_t uint16_buffer[4];
  uint32_t uint32_buffer[2];
  for (auto& code_item_pair : header_->GetCollections().CodeItems()) {
    std::unique_ptr<dex_ir::CodeItem>& code_item = code_item_pair.second;
    uint16_buffer[0] = code_item->RegistersSize();
    uint16_buffer[1] = code_item->InsSize();
    uint16_buffer[2] = code_item->OutsSize();
    uint16_buffer[3] = code_item->TriesSize();
    uint32_buffer[0] = code_item->DebugInfo() == nullptr ? 0 : code_item->DebugInfo()->GetOffset();
    uint32_buffer[1] = code_item->InsnsSize();
    size_t offset = code_item->GetOffset();
    offset += Write(uint16_buffer, 4 * sizeof(uint16_t), offset);
    offset += Write(uint32_buffer, 2 * sizeof(uint32_t), offset);
    offset += Write(code_item->Insns(), code_item->InsnsSize() * sizeof(uint16_t), offset);
    if (code_item->TriesSize() != 0) {
      if (code_item->InsnsSize() % 2 != 0) {
        uint16_t padding[1] = { 0 };
        offset += Write(padding, sizeof(uint16_t), offset);
      }
      uint32_t start_addr[1];
      uint16_t insn_count_and_handler_off[2];
      for (std::unique_ptr<const dex_ir::TryItem>& try_item : *code_item->Tries()) {
        start_addr[0] = try_item->StartAddr();
        insn_count_and_handler_off[0] = try_item->InsnCount();
        insn_count_and_handler_off[1] = try_item->GetHandlers()->GetListOffset();
        offset += Write(start_addr, sizeof(uint32_t), offset);
        offset += Write(insn_count_and_handler_off, 2 * sizeof(uint16_t), offset);
      }
      // Leave offset pointing to the end of the try items.
      WriteUleb128(code_item->Handlers()->size(), offset);
      for (std::unique_ptr<const dex_ir::CatchHandler>& handlers : *code_item->Handlers()) {
        size_t list_offset = offset + handlers->GetListOffset();
        uint32_t size = handlers->HasCatchAll() ? (handlers->GetHandlers()->size() - 1) * -1 :
            handlers->GetHandlers()->size();
        list_offset += WriteSleb128(size, list_offset);
        for (std::unique_ptr<const dex_ir::TypeAddrPair>& handler : *handlers->GetHandlers()) {
          if (handler->GetTypeId() != nullptr) {
            list_offset += WriteUleb128(handler->GetTypeId()->GetIndex(), list_offset);
          }
          list_offset += WriteUleb128(handler->GetAddress(), list_offset);
        }
      }
    }
  }
}

void DexWriter::WriteClasses() {
  uint32_t class_def_buffer[8];
  for (std::unique_ptr<dex_ir::ClassDef>& class_def : header_->GetCollections().ClassDefs()) {
    class_def_buffer[0] = class_def->ClassType()->GetIndex();
    class_def_buffer[1] = class_def->GetAccessFlags();
    class_def_buffer[2] = class_def->Superclass() == nullptr ? DexFile::kDexNoIndex :
        class_def->Superclass()->GetIndex();
    class_def_buffer[3] = class_def->InterfacesOffset();
    class_def_buffer[4] = class_def->SourceFile() == nullptr ? DexFile::kDexNoIndex :
        class_def->SourceFile()->GetIndex();
    class_def_buffer[5] = class_def->Annotations() == nullptr ? 0 :
        class_def->Annotations()->GetOffset();
    class_def_buffer[6] = class_def->GetClassData() == nullptr ? 0 :
        class_def->GetClassData()->GetOffset();
    class_def_buffer[7] = class_def->StaticValues() == nullptr ? 0 :
        class_def->StaticValues()->GetOffset();
    size_t offset = class_def->GetOffset();
    Write(class_def_buffer, class_def->GetSize(), offset);
  }

  for (auto& class_data_pair : header_->GetCollections().ClassDatas()) {
    std::unique_ptr<dex_ir::ClassData>& class_data = class_data_pair.second;
    size_t offset = class_data->GetOffset();
    offset += WriteUleb128(class_data->StaticFields()->size(), offset);
    offset += WriteUleb128(class_data->InstanceFields()->size(), offset);
    offset += WriteUleb128(class_data->DirectMethods()->size(), offset);
    offset += WriteUleb128(class_data->VirtualMethods()->size(), offset);
    offset += WriteEncodedFields(class_data->StaticFields(), offset);
    offset += WriteEncodedFields(class_data->InstanceFields(), offset);
    offset += WriteEncodedMethods(class_data->DirectMethods(), offset);
    offset += WriteEncodedMethods(class_data->VirtualMethods(), offset);
  }
}

void DexWriter::WriteCallSites() {
  uint32_t call_site_off[1];
  for (std::unique_ptr<dex_ir::CallSiteId>& call_site_id :
      header_->GetCollections().CallSiteIds()) {
    call_site_off[0] = call_site_id->CallSiteItem()->GetOffset();
    Write(call_site_off, call_site_id->GetSize(), call_site_id->GetOffset());
  }
}

void DexWriter::WriteMethodHandles() {
  uint16_t method_handle_buff[4];
  for (std::unique_ptr<dex_ir::MethodHandleItem>& method_handle :
      header_->GetCollections().MethodHandleItems()) {
    method_handle_buff[0] = static_cast<uint16_t>(method_handle->GetMethodHandleType());
    method_handle_buff[1] = 0;  // unused.
    method_handle_buff[2] = method_handle->GetFieldOrMethodId()->GetIndex();
    method_handle_buff[3] = 0;  // unused.
    Write(method_handle_buff, method_handle->GetSize(), method_handle->GetOffset());
  }
}

struct MapItemContainer {
  MapItemContainer(uint32_t type, uint32_t size, uint32_t offset)
      : type_(type), size_(size), offset_(offset) { }

  bool operator<(const MapItemContainer& other) const {
    return offset_ > other.offset_;
  }

  uint32_t type_;
  uint32_t size_;
  uint32_t offset_;
};

void DexWriter::WriteMapItem() {
  dex_ir::Collections& collection = header_->GetCollections();
  std::priority_queue<MapItemContainer> queue;

  // Header and index section.
  queue.push(MapItemContainer(DexFile::kDexTypeHeaderItem, 1, 0));
  if (collection.StringIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeStringIdItem, collection.StringIdsSize(),
        collection.StringIdsOffset()));
  }
  if (collection.TypeIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeTypeIdItem, collection.TypeIdsSize(),
        collection.TypeIdsOffset()));
  }
  if (collection.ProtoIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeProtoIdItem, collection.ProtoIdsSize(),
        collection.ProtoIdsOffset()));
  }
  if (collection.FieldIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeFieldIdItem, collection.FieldIdsSize(),
        collection.FieldIdsOffset()));
  }
  if (collection.MethodIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeMethodIdItem, collection.MethodIdsSize(),
        collection.MethodIdsOffset()));
  }
  if (collection.ClassDefsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeClassDefItem, collection.ClassDefsSize(),
        collection.ClassDefsOffset()));
  }
  if (collection.CallSiteIdsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeCallSiteIdItem, collection.CallSiteIdsSize(),
        collection.CallSiteIdsOffset()));
  }
  if (collection.MethodHandleItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeMethodHandleItem,
        collection.MethodHandleItemsSize(), collection.MethodHandleItemsOffset()));
  }

  // Data section.
  queue.push(MapItemContainer(DexFile::kDexTypeMapList, 1, collection.MapListOffset()));
  if (collection.TypeListsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeTypeList, collection.TypeListsSize(),
        collection.TypeListsOffset()));
  }
  if (collection.AnnotationSetRefListsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeAnnotationSetRefList,
        collection.AnnotationSetRefListsSize(), collection.AnnotationSetRefListsOffset()));
  }
  if (collection.AnnotationSetItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeAnnotationSetItem,
        collection.AnnotationSetItemsSize(), collection.AnnotationSetItemsOffset()));
  }
  if (collection.ClassDatasSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeClassDataItem, collection.ClassDatasSize(),
        collection.ClassDatasOffset()));
  }
  if (collection.CodeItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeCodeItem, collection.CodeItemsSize(),
        collection.CodeItemsOffset()));
  }
  if (collection.StringDatasSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeStringDataItem, collection.StringDatasSize(),
        collection.StringDatasOffset()));
  }
  if (collection.DebugInfoItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeDebugInfoItem, collection.DebugInfoItemsSize(),
        collection.DebugInfoItemsOffset()));
  }
  if (collection.AnnotationItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeAnnotationItem, collection.AnnotationItemsSize(),
        collection.AnnotationItemsOffset()));
  }
  if (collection.EncodedArrayItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeEncodedArrayItem,
        collection.EncodedArrayItemsSize(), collection.EncodedArrayItemsOffset()));
  }
  if (collection.AnnotationsDirectoryItemsSize() != 0) {
    queue.push(MapItemContainer(DexFile::kDexTypeAnnotationsDirectoryItem,
        collection.AnnotationsDirectoryItemsSize(), collection.AnnotationsDirectoryItemsOffset()));
  }

  uint32_t offset = collection.MapListOffset();
  uint16_t uint16_buffer[2];
  uint32_t uint32_buffer[2];
  uint16_buffer[1] = 0;
  uint32_buffer[0] = queue.size();
  offset += Write(uint32_buffer, sizeof(uint32_t), offset);
  while (!queue.empty()) {
    const MapItemContainer& map_item = queue.top();
    uint16_buffer[0] = map_item.type_;
    uint32_buffer[0] = map_item.size_;
    uint32_buffer[1] = map_item.offset_;
    offset += Write(uint16_buffer, 2 * sizeof(uint16_t), offset);
    offset += Write(uint32_buffer, 2 * sizeof(uint32_t), offset);
    queue.pop();
  }
}

void DexWriter::WriteHeader() {
  uint32_t buffer[20];
  dex_ir::Collections& collections = header_->GetCollections();
  size_t offset = 0;
  offset += Write(header_->Magic(), 8 * sizeof(uint8_t), offset);
  buffer[0] = header_->Checksum();
  offset += Write(buffer, sizeof(uint32_t), offset);
  offset += Write(header_->Signature(), 20 * sizeof(uint8_t), offset);
  uint32_t file_size = header_->FileSize();
  buffer[0] = file_size;
  buffer[1] = header_->GetSize();
  buffer[2] = header_->EndianTag();
  buffer[3] = header_->LinkSize();
  buffer[4] = header_->LinkOffset();
  buffer[5] = collections.MapListOffset();
  buffer[6] = collections.StringIdsSize();
  buffer[7] = collections.StringIdsOffset();
  buffer[8] = collections.TypeIdsSize();
  buffer[9] = collections.TypeIdsOffset();
  buffer[10] = collections.ProtoIdsSize();
  buffer[11] = collections.ProtoIdsOffset();
  buffer[12] = collections.FieldIdsSize();
  buffer[13] = collections.FieldIdsOffset();
  buffer[14] = collections.MethodIdsSize();
  buffer[15] = collections.MethodIdsOffset();
  uint32_t class_defs_size = collections.ClassDefsSize();
  uint32_t class_defs_off = collections.ClassDefsOffset();
  buffer[16] = class_defs_size;
  buffer[17] = class_defs_off;
  buffer[18] = header_->DataSize();
  buffer[19] = header_->DataOffset();
  Write(buffer, 20 * sizeof(uint32_t), offset);
}

void DexWriter::WriteMemMap() {
  WriteStrings();
  WriteTypes();
  WriteTypeLists();
  WriteProtos();
  WriteFields();
  WriteMethods();
  WriteEncodedArrays();
  WriteAnnotations();
  WriteAnnotationSets();
  WriteAnnotationSetRefs();
  WriteAnnotationsDirectories();
  WriteDebugInfoItems();
  WriteCodeItems();
  WriteClasses();
  WriteCallSites();
  WriteMethodHandles();
  WriteMapItem();
  WriteHeader();
}

void DexWriter::Output(dex_ir::Header* header, MemMap* mem_map) {
  DexWriter dex_writer(header, mem_map);
  dex_writer.WriteMemMap();
}

}  // namespace art
