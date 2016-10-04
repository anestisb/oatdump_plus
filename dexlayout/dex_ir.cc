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
 * Implementation file of the dexlayout utility.
 *
 * This is a tool to read dex files into an internal representation,
 * reorganize the representation, and emit dex files with a better
 * file layout.
 */

#include "dex_ir.h"
#include "dex_ir_builder.h"

namespace art {
namespace dex_ir {

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

static uint32_t GetDebugInfoStreamSize(const uint8_t* debug_info_stream) {
  const uint8_t* stream = debug_info_stream;
  DecodeUnsignedLeb128(&stream);  // line_start
  uint32_t parameters_size = DecodeUnsignedLeb128(&stream);
  for (uint32_t i = 0; i < parameters_size; ++i) {
    DecodeUnsignedLeb128P1(&stream);  // Parameter name.
  }

  for (;;)  {
    uint8_t opcode = *stream++;
    switch (opcode) {
      case DexFile::DBG_END_SEQUENCE:
        return stream - debug_info_stream;  // end of stream.
      case DexFile::DBG_ADVANCE_PC:
        DecodeUnsignedLeb128(&stream);  // addr_diff
        break;
      case DexFile::DBG_ADVANCE_LINE:
        DecodeSignedLeb128(&stream);  // line_diff
        break;
      case DexFile::DBG_START_LOCAL:
        DecodeUnsignedLeb128(&stream);  // register_num
        DecodeUnsignedLeb128P1(&stream);  // name_idx
        DecodeUnsignedLeb128P1(&stream);  // type_idx
        break;
      case DexFile::DBG_START_LOCAL_EXTENDED:
        DecodeUnsignedLeb128(&stream);  // register_num
        DecodeUnsignedLeb128P1(&stream);  // name_idx
        DecodeUnsignedLeb128P1(&stream);  // type_idx
        DecodeUnsignedLeb128P1(&stream);  // sig_idx
        break;
      case DexFile::DBG_END_LOCAL:
      case DexFile::DBG_RESTART_LOCAL:
        DecodeUnsignedLeb128(&stream);  // register_num
        break;
      case DexFile::DBG_SET_PROLOGUE_END:
      case DexFile::DBG_SET_EPILOGUE_BEGIN:
        break;
      case DexFile::DBG_SET_FILE: {
        DecodeUnsignedLeb128P1(&stream);  // name_idx
        break;
      }
      default: {
        break;
      }
    }
  }
}

EncodedValue* Collections::ReadEncodedValue(const uint8_t** data) {
  const uint8_t encoded_value = *(*data)++;
  const uint8_t type = encoded_value & 0x1f;
  EncodedValue* item = new EncodedValue(type);
  ReadEncodedValue(data, type, encoded_value >> 5, item);
  return item;
}

EncodedValue* Collections::ReadEncodedValue(const uint8_t** data, uint8_t type, uint8_t length) {
  EncodedValue* item = new EncodedValue(type);
  ReadEncodedValue(data, type, length, item);
  return item;
}

void Collections::ReadEncodedValue(
    const uint8_t** data, uint8_t type, uint8_t length, EncodedValue* item) {
  switch (type) {
    case DexFile::kDexAnnotationByte:
      item->SetByte(static_cast<int8_t>(ReadVarWidth(data, length, false)));
      break;
    case DexFile::kDexAnnotationShort:
      item->SetShort(static_cast<int16_t>(ReadVarWidth(data, length, true)));
      break;
    case DexFile::kDexAnnotationChar:
      item->SetChar(static_cast<uint16_t>(ReadVarWidth(data, length, false)));
      break;
    case DexFile::kDexAnnotationInt:
      item->SetInt(static_cast<int32_t>(ReadVarWidth(data, length, true)));
      break;
    case DexFile::kDexAnnotationLong:
      item->SetLong(static_cast<int64_t>(ReadVarWidth(data, length, true)));
      break;
    case DexFile::kDexAnnotationFloat: {
      // Fill on right.
      union {
        float f;
        uint32_t data;
      } conv;
      conv.data = static_cast<uint32_t>(ReadVarWidth(data, length, false)) << (3 - length) * 8;
      item->SetFloat(conv.f);
      break;
    }
    case DexFile::kDexAnnotationDouble: {
      // Fill on right.
      union {
        double d;
        uint64_t data;
      } conv;
      conv.data = ReadVarWidth(data, length, false) << (7 - length) * 8;
      item->SetDouble(conv.d);
      break;
    }
    case DexFile::kDexAnnotationString: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetStringId(GetStringId(string_index));
      break;
    }
    case DexFile::kDexAnnotationType: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetTypeId(GetTypeId(string_index));
      break;
    }
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum: {
      const uint32_t field_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetFieldId(GetFieldId(field_index));
      break;
    }
    case DexFile::kDexAnnotationMethod: {
      const uint32_t method_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetMethodId(GetMethodId(method_index));
      break;
    }
    case DexFile::kDexAnnotationArray: {
      EncodedValueVector* values = new EncodedValueVector();
      const uint32_t size = DecodeUnsignedLeb128(data);
      // Decode all elements.
      for (uint32_t i = 0; i < size; i++) {
        values->push_back(std::unique_ptr<EncodedValue>(ReadEncodedValue(data)));
      }
      item->SetEncodedArray(new EncodedArrayItem(values));
      break;
    }
    case DexFile::kDexAnnotationAnnotation: {
      AnnotationElementVector* elements = new AnnotationElementVector();
      const uint32_t type_idx = DecodeUnsignedLeb128(data);
      const uint32_t size = DecodeUnsignedLeb128(data);
      // Decode all name=value pairs.
      for (uint32_t i = 0; i < size; i++) {
        const uint32_t name_index = DecodeUnsignedLeb128(data);
        elements->push_back(std::unique_ptr<AnnotationElement>(
            new AnnotationElement(GetStringId(name_index), ReadEncodedValue(data))));
      }
      item->SetEncodedAnnotation(new EncodedAnnotation(GetTypeId(type_idx), elements));
      break;
    }
    case DexFile::kDexAnnotationNull:
      break;
    case DexFile::kDexAnnotationBoolean:
      item->SetBoolean(length != 0);
      break;
    default:
      break;
  }
}

void Collections::CreateStringId(const DexFile& dex_file, uint32_t i) {
  const DexFile::StringId& disk_string_id = dex_file.GetStringId(i);
  StringData* string_data = new StringData(dex_file.GetStringData(disk_string_id));
  string_datas_.AddItem(string_data, disk_string_id.string_data_off_);

  StringId* string_id = new StringId(string_data);
  string_ids_.AddIndexedItem(string_id, StringIdsOffset() + i * StringId::ItemSize(), i);
}

void Collections::CreateTypeId(const DexFile& dex_file, uint32_t i) {
  const DexFile::TypeId& disk_type_id = dex_file.GetTypeId(i);
  TypeId* type_id = new TypeId(GetStringId(disk_type_id.descriptor_idx_));
  type_ids_.AddIndexedItem(type_id, TypeIdsOffset() + i * TypeId::ItemSize(), i);
}

void Collections::CreateProtoId(const DexFile& dex_file, uint32_t i) {
  const DexFile::ProtoId& disk_proto_id = dex_file.GetProtoId(i);
  const DexFile::TypeList* type_list = dex_file.GetProtoParameters(disk_proto_id);
  TypeList* parameter_type_list = CreateTypeList(type_list, disk_proto_id.parameters_off_);

  ProtoId* proto_id = new ProtoId(GetStringId(disk_proto_id.shorty_idx_),
                                  GetTypeId(disk_proto_id.return_type_idx_),
                                  parameter_type_list);
  proto_ids_.AddIndexedItem(proto_id, ProtoIdsOffset() + i * ProtoId::ItemSize(), i);
}

void Collections::CreateFieldId(const DexFile& dex_file, uint32_t i) {
  const DexFile::FieldId& disk_field_id = dex_file.GetFieldId(i);
  FieldId* field_id = new FieldId(GetTypeId(disk_field_id.class_idx_),
                                  GetTypeId(disk_field_id.type_idx_),
                                  GetStringId(disk_field_id.name_idx_));
  field_ids_.AddIndexedItem(field_id, FieldIdsOffset() + i * FieldId::ItemSize(), i);
}

void Collections::CreateMethodId(const DexFile& dex_file, uint32_t i) {
  const DexFile::MethodId& disk_method_id = dex_file.GetMethodId(i);
  MethodId* method_id = new MethodId(GetTypeId(disk_method_id.class_idx_),
                                     GetProtoId(disk_method_id.proto_idx_),
                                     GetStringId(disk_method_id.name_idx_));
  method_ids_.AddIndexedItem(method_id, MethodIdsOffset() + i * MethodId::ItemSize(), i);
}

void Collections::CreateClassDef(const DexFile& dex_file, uint32_t i) {
  const DexFile::ClassDef& disk_class_def = dex_file.GetClassDef(i);
  const TypeId* class_type = GetTypeId(disk_class_def.class_idx_);
  uint32_t access_flags = disk_class_def.access_flags_;
  const TypeId* superclass = GetTypeIdOrNullPtr(disk_class_def.superclass_idx_);

  const DexFile::TypeList* type_list = dex_file.GetInterfacesList(disk_class_def);
  TypeList* interfaces_type_list = CreateTypeList(type_list, disk_class_def.interfaces_off_);

  const StringId* source_file = GetStringIdOrNullPtr(disk_class_def.source_file_idx_);
  // Annotations.
  AnnotationsDirectoryItem* annotations = nullptr;
  const DexFile::AnnotationsDirectoryItem* disk_annotations_directory_item =
      dex_file.GetAnnotationsDirectory(disk_class_def);
  if (disk_annotations_directory_item != nullptr) {
    annotations = CreateAnnotationsDirectoryItem(
        dex_file, disk_annotations_directory_item, disk_class_def.annotations_off_);
  }
  // Static field initializers.
  const uint8_t* static_data = dex_file.GetEncodedStaticFieldValuesArray(disk_class_def);
  EncodedArrayItem* static_values =
      CreateEncodedArrayItem(static_data, disk_class_def.static_values_off_);
  ClassData* class_data = CreateClassData(
      dex_file, dex_file.GetClassData(disk_class_def), disk_class_def.class_data_off_);
  ClassDef* class_def = new ClassDef(class_type, access_flags, superclass, interfaces_type_list,
                                     source_file, annotations, static_values, class_data);
  class_defs_.AddIndexedItem(class_def, ClassDefsOffset() + i * ClassDef::ItemSize(), i);
}

TypeList* Collections::CreateTypeList(const DexFile::TypeList* dex_type_list, uint32_t offset) {
  if (dex_type_list == nullptr) {
    return nullptr;
  }
  // TODO: Create more efficient lookup for existing type lists.
  for (std::unique_ptr<TypeList>& type_list : TypeLists()) {
    if (type_list->GetOffset() == offset) {
      return type_list.get();
    }
  }
  TypeIdVector* type_vector = new TypeIdVector();
  uint32_t size = dex_type_list->Size();
  for (uint32_t index = 0; index < size; ++index) {
    type_vector->push_back(GetTypeId(dex_type_list->GetTypeItem(index).type_idx_));
  }
  TypeList* new_type_list = new TypeList(type_vector);
  type_lists_.AddItem(new_type_list, offset);
  return new_type_list;
}

EncodedArrayItem* Collections::CreateEncodedArrayItem(const uint8_t* static_data, uint32_t offset) {
  if (static_data == nullptr) {
    return nullptr;
  }
  for (std::unique_ptr<EncodedArrayItem>& existing_array_item : EncodedArrayItems()) {
    if (existing_array_item->GetOffset() == offset) {
      return existing_array_item.get();
    }
  }
  uint32_t size = DecodeUnsignedLeb128(&static_data);
  EncodedValueVector* values = new EncodedValueVector();
  for (uint32_t i = 0; i < size; ++i) {
    values->push_back(std::unique_ptr<EncodedValue>(ReadEncodedValue(&static_data)));
  }
  // TODO: Calculate the size of the encoded array.
  EncodedArrayItem* encoded_array_item = new EncodedArrayItem(values);
  encoded_array_items_.AddItem(encoded_array_item, offset);
  return encoded_array_item;
}

AnnotationItem* Collections::CreateAnnotationItem(const DexFile::AnnotationItem* annotation,
                                                  uint32_t offset) {
  for (std::unique_ptr<AnnotationItem>& existing_annotation_item : AnnotationItems()) {
    if (existing_annotation_item->GetOffset() == offset) {
      return existing_annotation_item.get();
    }
  }
  uint8_t visibility = annotation->visibility_;
  const uint8_t* annotation_data = annotation->annotation_;
  EncodedValue* encoded_value =
      ReadEncodedValue(&annotation_data, DexFile::kDexAnnotationAnnotation, 0);
  // TODO: Calculate the size of the annotation.
  AnnotationItem* annotation_item =
      new AnnotationItem(visibility, encoded_value->ReleaseEncodedAnnotation());
  annotation_items_.AddItem(annotation_item, offset);
  return annotation_item;
}


AnnotationSetItem* Collections::CreateAnnotationSetItem(const DexFile& dex_file,
    const DexFile::AnnotationSetItem& disk_annotations_item, uint32_t offset) {
  if (disk_annotations_item.size_ == 0 && offset == 0) {
    return nullptr;
  }
  for (std::unique_ptr<AnnotationSetItem>& existing_anno_set_item : AnnotationSetItems()) {
    if (existing_anno_set_item->GetOffset() == offset) {
      return existing_anno_set_item.get();
    }
  }
  std::vector<AnnotationItem*>* items = new std::vector<AnnotationItem*>();
  for (uint32_t i = 0; i < disk_annotations_item.size_; ++i) {
    const DexFile::AnnotationItem* annotation =
        dex_file.GetAnnotationItem(&disk_annotations_item, i);
    if (annotation == nullptr) {
      continue;
    }
    AnnotationItem* annotation_item =
        CreateAnnotationItem(annotation, disk_annotations_item.entries_[i]);
    items->push_back(annotation_item);
  }
  AnnotationSetItem* annotation_set_item = new AnnotationSetItem(items);
  annotation_set_items_.AddItem(annotation_set_item, offset);
  return annotation_set_item;
}

AnnotationsDirectoryItem* Collections::CreateAnnotationsDirectoryItem(const DexFile& dex_file,
    const DexFile::AnnotationsDirectoryItem* disk_annotations_item, uint32_t offset) {
  for (std::unique_ptr<AnnotationsDirectoryItem>& anno_dir_item : AnnotationsDirectoryItems()) {
    if (anno_dir_item->GetOffset() == offset) {
      return anno_dir_item.get();
    }
  }
  const DexFile::AnnotationSetItem* class_set_item =
      dex_file.GetClassAnnotationSet(disk_annotations_item);
  AnnotationSetItem* class_annotation = nullptr;
  if (class_set_item != nullptr) {
    uint32_t offset = disk_annotations_item->class_annotations_off_;
    class_annotation = CreateAnnotationSetItem(dex_file, *class_set_item, offset);
  }
  const DexFile::FieldAnnotationsItem* fields =
      dex_file.GetFieldAnnotations(disk_annotations_item);
  FieldAnnotationVector* field_annotations = nullptr;
  if (fields != nullptr) {
    field_annotations = new FieldAnnotationVector();
    for (uint32_t i = 0; i < disk_annotations_item->fields_size_; ++i) {
      FieldId* field_id = GetFieldId(fields[i].field_idx_);
      const DexFile::AnnotationSetItem* field_set_item =
          dex_file.GetFieldAnnotationSetItem(fields[i]);
      uint32_t annotation_set_offset = fields[i].annotations_off_;
      AnnotationSetItem* annotation_set_item =
          CreateAnnotationSetItem(dex_file, *field_set_item, annotation_set_offset);
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
      MethodId* method_id = GetMethodId(methods[i].method_idx_);
      const DexFile::AnnotationSetItem* method_set_item =
          dex_file.GetMethodAnnotationSetItem(methods[i]);
      uint32_t annotation_set_offset = methods[i].annotations_off_;
      AnnotationSetItem* annotation_set_item =
          CreateAnnotationSetItem(dex_file, *method_set_item, annotation_set_offset);
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
      MethodId* method_id = GetMethodId(parameters[i].method_idx_);
      const DexFile::AnnotationSetRefList* list =
          dex_file.GetParameterAnnotationSetRefList(&parameters[i]);
      parameter_annotations->push_back(std::unique_ptr<ParameterAnnotation>(
          GenerateParameterAnnotation(dex_file, method_id, list, parameters[i].annotations_off_)));
    }
  }
  // TODO: Calculate the size of the annotations directory.
  AnnotationsDirectoryItem* annotations_directory_item = new AnnotationsDirectoryItem(
      class_annotation, field_annotations, method_annotations, parameter_annotations);
  annotations_directory_items_.AddItem(annotations_directory_item, offset);
  return annotations_directory_item;
}

ParameterAnnotation* Collections::GenerateParameterAnnotation(
    const DexFile& dex_file, MethodId* method_id,
    const DexFile::AnnotationSetRefList* annotation_set_ref_list, uint32_t offset) {
  AnnotationSetRefList* set_ref_list = nullptr;
  for (std::unique_ptr<AnnotationSetRefList>& existing_set_ref_list : AnnotationSetRefLists()) {
    if (existing_set_ref_list->GetOffset() == offset) {
      set_ref_list = existing_set_ref_list.get();
      break;
    }
  }
  if (set_ref_list == nullptr) {
    std::vector<AnnotationSetItem*>* annotations = new std::vector<AnnotationSetItem*>();
    for (uint32_t i = 0; i < annotation_set_ref_list->size_; ++i) {
      const DexFile::AnnotationSetItem* annotation_set_item =
          dex_file.GetSetRefItemItem(&annotation_set_ref_list->list_[i]);
      uint32_t set_offset = annotation_set_ref_list->list_[i].annotations_off_;
      annotations->push_back(CreateAnnotationSetItem(dex_file, *annotation_set_item, set_offset));
    }
    set_ref_list = new AnnotationSetRefList(annotations);
    annotation_set_ref_lists_.AddItem(set_ref_list, offset);
  }
  return new ParameterAnnotation(method_id, set_ref_list);
}

CodeItem* Collections::CreateCodeItem(const DexFile& dex_file,
                                      const DexFile::CodeItem& disk_code_item, uint32_t offset) {
  uint16_t registers_size = disk_code_item.registers_size_;
  uint16_t ins_size = disk_code_item.ins_size_;
  uint16_t outs_size = disk_code_item.outs_size_;
  uint32_t tries_size = disk_code_item.tries_size_;

  // TODO: Calculate the size of the debug info.
  const uint8_t* debug_info_stream = dex_file.GetDebugInfoStream(&disk_code_item);
  DebugInfoItem* debug_info = nullptr;
  if (debug_info_stream != nullptr) {
    uint32_t debug_info_size = GetDebugInfoStreamSize(debug_info_stream);
    uint8_t* debug_info_buffer = new uint8_t[debug_info_size];
    memcpy(debug_info_buffer, debug_info_stream, debug_info_size);
    debug_info = new DebugInfoItem(debug_info_size, debug_info_buffer);
    debug_info_items_.AddItem(debug_info, disk_code_item.debug_info_off_);
  }

  uint32_t insns_size = disk_code_item.insns_size_in_code_units_;
  uint16_t* insns = new uint16_t[insns_size];
  memcpy(insns, disk_code_item.insns_, insns_size * sizeof(uint16_t));

  TryItemVector* tries = nullptr;
  CatchHandlerVector* handler_list = nullptr;
  if (tries_size > 0) {
    tries = new TryItemVector();
    handler_list = new CatchHandlerVector();
    for (uint32_t i = 0; i < tries_size; ++i) {
      const DexFile::TryItem* disk_try_item = dex_file.GetTryItems(disk_code_item, i);
      uint32_t start_addr = disk_try_item->start_addr_;
      uint16_t insn_count = disk_try_item->insn_count_;
      uint16_t handler_off = disk_try_item->handler_off_;
      const CatchHandler* handlers = nullptr;
      for (std::unique_ptr<const CatchHandler>& existing_handlers : *handler_list) {
        if (handler_off == existing_handlers->GetListOffset()) {
          handlers = existing_handlers.get();
        }
      }
      if (handlers == nullptr) {
        bool catch_all = false;
        TypeAddrPairVector* addr_pairs = new TypeAddrPairVector();
        for (CatchHandlerIterator it(disk_code_item, *disk_try_item); it.HasNext(); it.Next()) {
          const uint16_t type_index = it.GetHandlerTypeIndex();
          const TypeId* type_id = GetTypeIdOrNullPtr(type_index);
          catch_all |= type_id == nullptr;
          addr_pairs->push_back(std::unique_ptr<const TypeAddrPair>(
              new TypeAddrPair(type_id, it.GetHandlerAddress())));
        }
        handlers = new CatchHandler(catch_all, handler_off, addr_pairs);
        handler_list->push_back(std::unique_ptr<const CatchHandler>(handlers));
      }
      TryItem* try_item = new TryItem(start_addr, insn_count, handlers);
      tries->push_back(std::unique_ptr<const TryItem>(try_item));
    }
  }
  // TODO: Calculate the size of the code item.
  CodeItem* code_item = new CodeItem(
      registers_size, ins_size, outs_size, debug_info, insns_size, insns, tries, handler_list);
  code_items_.AddItem(code_item, offset);
  return code_item;
}

MethodItem* Collections::GenerateMethodItem(const DexFile& dex_file, ClassDataItemIterator& cdii) {
  MethodId* method_item = GetMethodId(cdii.GetMemberIndex());
  uint32_t access_flags = cdii.GetRawMemberAccessFlags();
  const DexFile::CodeItem* disk_code_item = cdii.GetMethodCodeItem();
  CodeItem* code_item = nullptr;
  DebugInfoItem* debug_info = nullptr;
  if (disk_code_item != nullptr) {
    code_item = CreateCodeItem(dex_file, *disk_code_item, cdii.GetMethodCodeItemOffset());
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

ClassData* Collections::CreateClassData(
    const DexFile& dex_file, const uint8_t* encoded_data, uint32_t offset) {
  // Read the fields and methods defined by the class, resolving the circular reference from those
  // to classes by setting class at the same time.
  ClassData* class_data = nullptr;
  if (encoded_data != nullptr) {
    ClassDataItemIterator cdii(dex_file, encoded_data);
    // Static fields.
    FieldItemVector* static_fields = new FieldItemVector();
    for (uint32_t i = 0; cdii.HasNextStaticField(); i++, cdii.Next()) {
      FieldId* field_item = GetFieldId(cdii.GetMemberIndex());
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      static_fields->push_back(std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Instance fields.
    FieldItemVector* instance_fields = new FieldItemVector();
    for (uint32_t i = 0; cdii.HasNextInstanceField(); i++, cdii.Next()) {
      FieldId* field_item = GetFieldId(cdii.GetMemberIndex());
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      instance_fields->push_back(
          std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Direct methods.
    MethodItemVector* direct_methods = new MethodItemVector();
    for (uint32_t i = 0; cdii.HasNextDirectMethod(); i++, cdii.Next()) {
      direct_methods->push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, cdii)));
    }
    // Virtual methods.
    MethodItemVector* virtual_methods = new MethodItemVector();
    for (uint32_t i = 0; cdii.HasNextVirtualMethod(); i++, cdii.Next()) {
      virtual_methods->push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, cdii)));
    }
    // TODO: Calculate the size of the class data.
    class_data = new ClassData(static_fields, instance_fields, direct_methods, virtual_methods);
    class_datas_.AddItem(class_data, offset);
  }
  return class_data;
}

template <class T> static uint32_t GetMinOffset(std::vector<std::unique_ptr<T>>& items) {
  uint32_t offset = 0xffffffff;
  for (std::unique_ptr<T>& item : items) {
    if (item->GetOffset() != 0 && item->GetOffset() < offset) {
      offset = item->GetOffset();
    }
  }
  return offset;
}

void Collections::CalculateSectionOffsets() {
  SetStringDatasOffset(GetMinOffset(StringDatas()));
  SetTypeListsOffset(GetMinOffset(TypeLists()));
  SetEncodedArrayItemsOffset(GetMinOffset(EncodedArrayItems()));
  SetAnnotationItemsOffset(GetMinOffset(AnnotationItems()));
  SetAnnotationSetItemsOffset(GetMinOffset(AnnotationSetItems()));
  SetAnnotationSetRefListsOffset(GetMinOffset(AnnotationSetRefLists()));
  SetAnnotationsDirectoryItemsOffset(GetMinOffset(AnnotationsDirectoryItems()));
  SetDebugInfoItemsOffset(GetMinOffset(DebugInfoItems()));
  SetCodeItemsOffset(GetMinOffset(CodeItems()));
  SetClassDatasOffset(GetMinOffset(ClassDatas()));
}

}  // namespace dex_ir
}  // namespace art
