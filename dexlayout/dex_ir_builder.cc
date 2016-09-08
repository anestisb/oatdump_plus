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

namespace {

static uint64_t ReadVarWidth(const uint8_t** data, uint8_t length, bool sign_extend) {
  uint64_t value = 0;
  for (uint32_t i = 0; i <= length; i++) {
    value |= static_cast<uint64_t>(*(*data)++) << (i * 8);
  }
  if (sign_extend) {
    int shift = (7 - length) * 8;
    return (static_cast<int64_t>(value) << shift) >> shift;
  }
  return value;
}

// Prototype to break cyclic dependency.
void ReadArrayItemVariant(Header& header,
                          const uint8_t** data,
                          uint8_t type,
                          uint8_t length,
                          ArrayItem::ArrayItemVariant* item);

ArrayItem* ReadArrayItem(Header& header, const uint8_t** data, uint8_t type, uint8_t length) {
  ArrayItem* item = new ArrayItem(type);
  ReadArrayItemVariant(header, data, type, length, item->GetArrayItemVariant());
  return item;
}

ArrayItem* ReadArrayItem(Header& header, const uint8_t** data) {
  const uint8_t encoded_value = *(*data)++;
  const uint8_t type = encoded_value & 0x1f;
  ArrayItem* item = new ArrayItem(type);
  ReadArrayItemVariant(header, data, type, encoded_value >> 5, item->GetArrayItemVariant());
  return item;
}

void ReadArrayItemVariant(Header& header,
                          const uint8_t** data,
                          uint8_t type,
                          uint8_t length,
                          ArrayItem::ArrayItemVariant* item) {
  switch (type) {
    case DexFile::kDexAnnotationByte:
      item->u_.byte_val_ = static_cast<int8_t>(ReadVarWidth(data, length, false));
      break;
    case DexFile::kDexAnnotationShort:
      item->u_.short_val_ = static_cast<int16_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationChar:
      item->u_.char_val_ = static_cast<uint16_t>(ReadVarWidth(data, length, false));
      break;
    case DexFile::kDexAnnotationInt:
      item->u_.int_val_ = static_cast<int32_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationLong:
      item->u_.long_val_ = static_cast<int64_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationFloat: {
      // Fill on right.
      union {
        float f;
        uint32_t data;
      } conv;
      conv.data = static_cast<uint32_t>(ReadVarWidth(data, length, false)) << (3 - length) * 8;
      item->u_.float_val_ = conv.f;
      break;
    }
    case DexFile::kDexAnnotationDouble: {
      // Fill on right.
      union {
        double d;
        uint64_t data;
      } conv;
      conv.data = ReadVarWidth(data, length, false) << (7 - length) * 8;
      item->u_.double_val_ = conv.d;
      break;
    }
    case DexFile::kDexAnnotationString: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->u_.string_val_ = header.StringIds()[string_index].get();
      break;
    }
    case DexFile::kDexAnnotationType: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->u_.string_val_ = header.TypeIds()[string_index]->GetStringId();
      break;
    }
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum: {
      const uint32_t field_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->u_.field_val_ = header.FieldIds()[field_index].get();
      break;
    }
    case DexFile::kDexAnnotationMethod: {
      const uint32_t method_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->u_.method_val_ = header.MethodIds()[method_index].get();
      break;
    }
    case DexFile::kDexAnnotationArray: {
      item->annotation_array_val_.reset(new ArrayItemVector());
      // Decode all elements.
      const uint32_t size = DecodeUnsignedLeb128(data);
      for (uint32_t i = 0; i < size; i++) {
        item->annotation_array_val_->push_back(
            std::unique_ptr<ArrayItem>(ReadArrayItem(header, data)));
      }
      break;
    }
    case DexFile::kDexAnnotationAnnotation: {
      const uint32_t type_idx = DecodeUnsignedLeb128(data);
      item->annotation_annotation_val_.string_ = header.TypeIds()[type_idx]->GetStringId();
      item->annotation_annotation_val_.array_.reset(
          new std::vector<std::unique_ptr<ArrayItem::NameValuePair>>());
      // Decode all name=value pairs.
      const uint32_t size = DecodeUnsignedLeb128(data);
      for (uint32_t i = 0; i < size; i++) {
        const uint32_t name_index = DecodeUnsignedLeb128(data);
        item->annotation_annotation_val_.array_->push_back(
            std::unique_ptr<ArrayItem::NameValuePair>(
                new ArrayItem::NameValuePair(header.StringIds()[name_index].get(),
                                             ReadArrayItem(header, data))));
      }
      break;
    }
    case DexFile::kDexAnnotationNull:
      break;
    case DexFile::kDexAnnotationBoolean:
      item->u_.bool_val_ = (length != 0);
      break;
    default:
      break;
  }
}

static bool GetPositionsCb(void* context, const DexFile::PositionInfo& entry) {
  DebugInfoItem* debug_info = reinterpret_cast<DebugInfoItem*>(context);
  PositionInfoVector& positions = debug_info->GetPositionInfo();
  positions.push_back(std::unique_ptr<PositionInfo>(new PositionInfo(entry.address_, entry.line_)));
  return false;
}

static void GetLocalsCb(void* context, const DexFile::LocalInfo& entry) {
  DebugInfoItem* debug_info = reinterpret_cast<DebugInfoItem*>(context);
  LocalInfoVector& locals = debug_info->GetLocalInfo();
  const char* name = entry.name_ != nullptr ? entry.name_ : "(null)";
  const char* signature = entry.signature_ != nullptr ? entry.signature_ : "";
  locals.push_back(std::unique_ptr<LocalInfo>(
      new LocalInfo(name, entry.descriptor_, signature, entry.start_address_,
                    entry.end_address_, entry.reg_)));
}

CodeItem* ReadCodeItem(const DexFile& dex_file,
                       const DexFile::CodeItem& disk_code_item,
                       Header& header) {
  uint16_t registers_size = disk_code_item.registers_size_;
  uint16_t ins_size = disk_code_item.ins_size_;
  uint16_t outs_size = disk_code_item.outs_size_;
  uint32_t tries_size = disk_code_item.tries_size_;

  const uint8_t* debug_info_stream = dex_file.GetDebugInfoStream(&disk_code_item);
  DebugInfoItem* debug_info = nullptr;
  if (debug_info_stream != nullptr) {
    debug_info = new DebugInfoItem();
  }

  uint32_t insns_size = disk_code_item.insns_size_in_code_units_;
  uint16_t* insns = new uint16_t[insns_size];
  memcpy(insns, disk_code_item.insns_, insns_size * sizeof(uint16_t));

  TryItemVector* tries = nullptr;
  if (tries_size > 0) {
    tries = new TryItemVector();
    for (uint32_t i = 0; i < tries_size; ++i) {
      const DexFile::TryItem* disk_try_item = dex_file.GetTryItems(disk_code_item, i);
      uint32_t start_addr = disk_try_item->start_addr_;
      uint16_t insn_count = disk_try_item->insn_count_;
      CatchHandlerVector* handlers = new CatchHandlerVector();
      for (CatchHandlerIterator it(disk_code_item, *disk_try_item); it.HasNext(); it.Next()) {
        const uint16_t type_index = it.GetHandlerTypeIndex();
        const TypeId* type_id = header.GetTypeIdOrNullPtr(type_index);
        handlers->push_back(std::unique_ptr<const CatchHandler>(
            new CatchHandler(type_id, it.GetHandlerAddress())));
      }
      TryItem* try_item = new TryItem(start_addr, insn_count, handlers);
      tries->push_back(std::unique_ptr<const TryItem>(try_item));
    }
  }
  return new CodeItem(registers_size, ins_size, outs_size, debug_info, insns_size, insns, tries);
}

MethodItem* GenerateMethodItem(const DexFile& dex_file,
                               dex_ir::Header& header,
                               ClassDataItemIterator& cdii) {
  MethodId* method_item = header.MethodIds()[cdii.GetMemberIndex()].get();
  uint32_t access_flags = cdii.GetRawMemberAccessFlags();
  const DexFile::CodeItem* disk_code_item = cdii.GetMethodCodeItem();
  CodeItem* code_item = nullptr;
  DebugInfoItem* debug_info = nullptr;
  if (disk_code_item != nullptr) {
    code_item = ReadCodeItem(dex_file, *disk_code_item, header);
    code_item->SetOffset(cdii.GetMethodCodeItemOffset());
    debug_info = code_item->DebugInfo();
  }
  if (debug_info != nullptr) {
    bool is_static = (access_flags & kAccStatic) != 0;
    dex_file.DecodeDebugLocalInfo(
        disk_code_item, is_static, cdii.GetMemberIndex(), GetLocalsCb, debug_info);
    dex_file.DecodeDebugPositionInfo(disk_code_item, GetPositionsCb, debug_info);
  }
  return new MethodItem(access_flags, method_item, code_item);
}

AnnotationSetItem* ReadAnnotationSetItem(const DexFile& dex_file,
                                         const DexFile::AnnotationSetItem& disk_annotations_item,
                                         Header& header) {
  if (disk_annotations_item.size_ == 0) {
    return nullptr;
  }
  AnnotationItemVector* items = new AnnotationItemVector();
  for (uint32_t i = 0; i < disk_annotations_item.size_; ++i) {
    const DexFile::AnnotationItem* annotation =
        dex_file.GetAnnotationItem(&disk_annotations_item, i);
    if (annotation == nullptr) {
      continue;
    }
    uint8_t visibility = annotation->visibility_;
    const uint8_t* annotation_data = annotation->annotation_;
    ArrayItem* array_item =
        ReadArrayItem(header, &annotation_data, DexFile::kDexAnnotationAnnotation, 0);
    items->push_back(std::unique_ptr<AnnotationItem>(new AnnotationItem(visibility, array_item)));
  }
  return new AnnotationSetItem(items);
}

ParameterAnnotation* ReadParameterAnnotation(
    const DexFile& dex_file,
    MethodId* method_id,
    const DexFile::AnnotationSetRefList* annotation_set_ref_list,
    Header& header) {
  AnnotationSetItemVector* annotations = new AnnotationSetItemVector();
  for (uint32_t i = 0; i < annotation_set_ref_list->size_; ++i) {
    const DexFile::AnnotationSetItem* annotation_set_item =
        dex_file.GetSetRefItemItem(&annotation_set_ref_list->list_[i]);
    annotations->push_back(std::unique_ptr<AnnotationSetItem>(
        ReadAnnotationSetItem(dex_file, *annotation_set_item, header)));
  }
  return new ParameterAnnotation(method_id, annotations);
}

AnnotationsDirectoryItem* ReadAnnotationsDirectoryItem(
    const DexFile& dex_file,
    const DexFile::AnnotationsDirectoryItem* disk_annotations_item,
    Header& header) {
  const DexFile::AnnotationSetItem* class_set_item =
      dex_file.GetClassAnnotationSet(disk_annotations_item);
  AnnotationSetItem* class_annotation = nullptr;
  if (class_set_item != nullptr) {
    class_annotation = ReadAnnotationSetItem(dex_file, *class_set_item, header);
  }
  const DexFile::FieldAnnotationsItem* fields =
      dex_file.GetFieldAnnotations(disk_annotations_item);
  FieldAnnotationVector* field_annotations = nullptr;
  if (fields != nullptr) {
    field_annotations = new FieldAnnotationVector();
    for (uint32_t i = 0; i < disk_annotations_item->fields_size_; ++i) {
      FieldId* field_id = header.FieldIds()[fields[i].field_idx_].get();
      const DexFile::AnnotationSetItem* field_set_item =
          dex_file.GetFieldAnnotationSetItem(fields[i]);
      AnnotationSetItem* annotation_set_item =
          ReadAnnotationSetItem(dex_file, *field_set_item, header);
      field_annotations->push_back(std::unique_ptr<FieldAnnotation>(
          new FieldAnnotation(field_id, annotation_set_item)));
    }
  }
  const DexFile::MethodAnnotationsItem* methods =
      dex_file.GetMethodAnnotations(disk_annotations_item);
  MethodAnnotationVector* method_annotations = nullptr;
  if (methods != nullptr) {
    method_annotations = new MethodAnnotationVector();
    for (uint32_t i = 0; i < disk_annotations_item->methods_size_; ++i) {
      MethodId* method_id = header.MethodIds()[methods[i].method_idx_].get();
      const DexFile::AnnotationSetItem* method_set_item =
          dex_file.GetMethodAnnotationSetItem(methods[i]);
      AnnotationSetItem* annotation_set_item =
          ReadAnnotationSetItem(dex_file, *method_set_item, header);
      method_annotations->push_back(std::unique_ptr<MethodAnnotation>(
          new MethodAnnotation(method_id, annotation_set_item)));
    }
  }
  const DexFile::ParameterAnnotationsItem* parameters =
      dex_file.GetParameterAnnotations(disk_annotations_item);
  ParameterAnnotationVector* parameter_annotations = nullptr;
  if (parameters != nullptr) {
    parameter_annotations = new ParameterAnnotationVector();
    for (uint32_t i = 0; i < disk_annotations_item->parameters_size_; ++i) {
      MethodId* method_id = header.MethodIds()[parameters[i].method_idx_].get();
      const DexFile::AnnotationSetRefList* list =
          dex_file.GetParameterAnnotationSetRefList(&parameters[i]);
      parameter_annotations->push_back(std::unique_ptr<ParameterAnnotation>(
          ReadParameterAnnotation(dex_file, method_id, list, header)));
    }
  }

  return new AnnotationsDirectoryItem(class_annotation,
                                      field_annotations,
                                      method_annotations,
                                      parameter_annotations);
}

ClassDef* ReadClassDef(const DexFile& dex_file,
                       const DexFile::ClassDef& disk_class_def,
                       Header& header) {
  const TypeId* class_type = header.TypeIds()[disk_class_def.class_idx_].get();
  uint32_t access_flags = disk_class_def.access_flags_;
  const TypeId* superclass = header.GetTypeIdOrNullPtr(disk_class_def.superclass_idx_);

  TypeIdVector* interfaces = nullptr;
  const DexFile::TypeList* type_list = dex_file.GetInterfacesList(disk_class_def);
  uint32_t interfaces_offset = disk_class_def.interfaces_off_;
  if (type_list != nullptr) {
    interfaces = new TypeIdVector();
    for (uint32_t index = 0; index < type_list->Size(); ++index) {
      interfaces->push_back(header.TypeIds()[type_list->GetTypeItem(index).type_idx_].get());
    }
  }
  const StringId* source_file = header.GetStringIdOrNullPtr(disk_class_def.source_file_idx_);
  // Annotations.
  AnnotationsDirectoryItem* annotations = nullptr;
  const DexFile::AnnotationsDirectoryItem* disk_annotations_directory_item =
      dex_file.GetAnnotationsDirectory(disk_class_def);
  if (disk_annotations_directory_item != nullptr) {
    annotations = ReadAnnotationsDirectoryItem(dex_file, disk_annotations_directory_item, header);
    annotations->SetOffset(disk_class_def.annotations_off_);
  }
  // Static field initializers.
  ArrayItemVector* static_values = nullptr;
  const uint8_t* static_data = dex_file.GetEncodedStaticFieldValuesArray(disk_class_def);
  if (static_data != nullptr) {
    uint32_t static_value_count = static_data == nullptr ? 0 : DecodeUnsignedLeb128(&static_data);
    if (static_value_count > 0) {
      static_values = new ArrayItemVector();
      for (uint32_t i = 0; i < static_value_count; ++i) {
        static_values->push_back(std::unique_ptr<ArrayItem>(ReadArrayItem(header, &static_data)));
      }
    }
  }
  // Read the fields and methods defined by the class, resolving the circular reference from those
  // to classes by setting class at the same time.
  const uint8_t* encoded_data = dex_file.GetClassData(disk_class_def);
  ClassData* class_data = nullptr;
  if (encoded_data != nullptr) {
    uint32_t offset = disk_class_def.class_data_off_;
    ClassDataItemIterator cdii(dex_file, encoded_data);
    // Static fields.
    FieldItemVector* static_fields = new FieldItemVector();
    for (uint32_t i = 0; cdii.HasNextStaticField(); i++, cdii.Next()) {
      FieldId* field_item = header.FieldIds()[cdii.GetMemberIndex()].get();
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      static_fields->push_back(std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Instance fields.
    FieldItemVector* instance_fields = new FieldItemVector();
    for (uint32_t i = 0; cdii.HasNextInstanceField(); i++, cdii.Next()) {
      FieldId* field_item = header.FieldIds()[cdii.GetMemberIndex()].get();
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      instance_fields->push_back(
          std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Direct methods.
    MethodItemVector* direct_methods = new MethodItemVector();
    for (uint32_t i = 0; cdii.HasNextDirectMethod(); i++, cdii.Next()) {
      direct_methods->push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, header, cdii)));
    }
    // Virtual methods.
    MethodItemVector* virtual_methods = new MethodItemVector();
    for (uint32_t i = 0; cdii.HasNextVirtualMethod(); i++, cdii.Next()) {
      virtual_methods->push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, header, cdii)));
    }
    class_data = new ClassData(static_fields, instance_fields, direct_methods, virtual_methods);
    class_data->SetOffset(offset);
  }
  return new ClassDef(class_type,
                      access_flags,
                      superclass,
                      interfaces,
                      interfaces_offset,
                      source_file,
                      annotations,
                      static_values,
                      class_data);
}

}  // namespace

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
  // Walk the rest of the header fields.
  // StringId table.
  std::vector<std::unique_ptr<StringId>>& string_ids = header->StringIds();
  header->SetStringIdsOffset(disk_header.string_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumStringIds(); ++i) {
    const DexFile::StringId& disk_string_id = dex_file.GetStringId(i);
    StringId* string_id = new StringId(dex_file.GetStringData(disk_string_id));
    string_id->SetOffset(i);
    string_ids.push_back(std::unique_ptr<StringId>(string_id));
  }
  // TypeId table.
  std::vector<std::unique_ptr<TypeId>>& type_ids = header->TypeIds();
  header->SetTypeIdsOffset(disk_header.type_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumTypeIds(); ++i) {
    const DexFile::TypeId& disk_type_id = dex_file.GetTypeId(i);
    TypeId* type_id = new TypeId(header->StringIds()[disk_type_id.descriptor_idx_].get());
    type_id->SetOffset(i);
    type_ids.push_back(std::unique_ptr<TypeId>(type_id));
  }
  // ProtoId table.
  std::vector<std::unique_ptr<ProtoId>>& proto_ids = header->ProtoIds();
  header->SetProtoIdsOffset(disk_header.proto_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumProtoIds(); ++i) {
    const DexFile::ProtoId& disk_proto_id = dex_file.GetProtoId(i);
    // Build the parameter type vector.
    TypeIdVector* parameters = new TypeIdVector();
    DexFileParameterIterator dfpi(dex_file, disk_proto_id);
    while (dfpi.HasNext()) {
      parameters->push_back(header->TypeIds()[dfpi.GetTypeIdx()].get());
      dfpi.Next();
    }
    ProtoId* proto_id = new ProtoId(header->StringIds()[disk_proto_id.shorty_idx_].get(),
                                    header->TypeIds()[disk_proto_id.return_type_idx_].get(),
                                    parameters);
    proto_id->SetOffset(i);
    proto_ids.push_back(std::unique_ptr<ProtoId>(proto_id));
  }
  // FieldId table.
  std::vector<std::unique_ptr<FieldId>>& field_ids = header->FieldIds();
  header->SetFieldIdsOffset(disk_header.field_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumFieldIds(); ++i) {
    const DexFile::FieldId& disk_field_id = dex_file.GetFieldId(i);
    FieldId* field_id = new FieldId(header->TypeIds()[disk_field_id.class_idx_].get(),
                                    header->TypeIds()[disk_field_id.type_idx_].get(),
                                    header->StringIds()[disk_field_id.name_idx_].get());
    field_id->SetOffset(i);
    field_ids.push_back(std::unique_ptr<FieldId>(field_id));
  }
  // MethodId table.
  std::vector<std::unique_ptr<MethodId>>& method_ids = header->MethodIds();
  header->SetMethodIdsOffset(disk_header.method_ids_off_);
  for (uint32_t i = 0; i < dex_file.NumMethodIds(); ++i) {
    const DexFile::MethodId& disk_method_id = dex_file.GetMethodId(i);
    MethodId* method_id = new MethodId(header->TypeIds()[disk_method_id.class_idx_].get(),
                                       header->ProtoIds()[disk_method_id.proto_idx_].get(),
                                       header->StringIds()[disk_method_id.name_idx_].get());
    method_id->SetOffset(i);
    method_ids.push_back(std::unique_ptr<MethodId>(method_id));
  }
  // ClassDef table.
  std::vector<std::unique_ptr<ClassDef>>& class_defs = header->ClassDefs();
  header->SetClassDefsOffset(disk_header.class_defs_off_);
  for (uint32_t i = 0; i < dex_file.NumClassDefs(); ++i) {
    const DexFile::ClassDef& disk_class_def = dex_file.GetClassDef(i);
    ClassDef* class_def = ReadClassDef(dex_file, disk_class_def, *header);
    class_def->SetOffset(i);
    class_defs.push_back(std::unique_ptr<ClassDef>(class_def));
  }

  return header;
}

}  // namespace dex_ir
}  // namespace art
