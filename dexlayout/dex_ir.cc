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
#include "dex_instruction-inl.h"
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
  const char* descriptor = entry.descriptor_ != nullptr ? entry.descriptor_ : "";
  const char* signature = entry.signature_ != nullptr ? entry.signature_ : "";
  locals.push_back(std::unique_ptr<LocalInfo>(
      new LocalInfo(name, descriptor, signature, entry.start_address_, entry.end_address_,
                    entry.reg_)));
}

static uint32_t GetCodeItemSize(const DexFile::CodeItem& disk_code_item) {
  uintptr_t code_item_start = reinterpret_cast<uintptr_t>(&disk_code_item);
  uint32_t insns_size = disk_code_item.insns_size_in_code_units_;
  uint32_t tries_size = disk_code_item.tries_size_;
  if (tries_size == 0) {
    uintptr_t insns_end = reinterpret_cast<uintptr_t>(&disk_code_item.insns_[insns_size]);
    return insns_end - code_item_start;
  } else {
    // Get the start of the handler data.
    const uint8_t* handler_data = DexFile::GetCatchHandlerData(disk_code_item, 0);
    uint32_t handlers_size = DecodeUnsignedLeb128(&handler_data);
    // Manually read each handler.
    for (uint32_t i = 0; i < handlers_size; ++i) {
      int32_t uleb128_count = DecodeSignedLeb128(&handler_data) * 2;
      if (uleb128_count <= 0) {
        uleb128_count = -uleb128_count + 1;
      }
      for (int32_t j = 0; j < uleb128_count; ++j) {
        DecodeUnsignedLeb128(&handler_data);
      }
    }
    return reinterpret_cast<uintptr_t>(handler_data) - code_item_start;
  }
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

static bool GetIdFromInstruction(Collections& collections,
                                 const Instruction* dec_insn,
                                 std::vector<TypeId*>* type_ids,
                                 std::vector<StringId*>* string_ids,
                                 std::vector<MethodId*>* method_ids,
                                 std::vector<FieldId*>* field_ids) {
  // Determine index and width of the string.
  uint32_t index = 0;
  switch (Instruction::FormatOf(dec_insn->Opcode())) {
    // SOME NOT SUPPORTED:
    // case Instruction::k20bc:
    case Instruction::k21c:
    case Instruction::k35c:
    // case Instruction::k35ms:
    case Instruction::k3rc:
    // case Instruction::k3rms:
    // case Instruction::k35mi:
    // case Instruction::k3rmi:
    case Instruction::k45cc:
    case Instruction::k4rcc:
      index = dec_insn->VRegB();
      break;
    case Instruction::k31c:
      index = dec_insn->VRegB();
      break;
    case Instruction::k22c:
    // case Instruction::k22cs:
      index = dec_insn->VRegC();
      break;
    default:
      break;
  }  // switch

  // Determine index type, and add reference to the appropriate collection.
  switch (Instruction::IndexTypeOf(dec_insn->Opcode())) {
    case Instruction::kIndexTypeRef:
      if (index < collections.TypeIdsSize()) {
        type_ids->push_back(collections.GetTypeId(index));
        return true;
      }
      break;
    case Instruction::kIndexStringRef:
      if (index < collections.StringIdsSize()) {
        string_ids->push_back(collections.GetStringId(index));
        return true;
      }
      break;
    case Instruction::kIndexMethodRef:
    case Instruction::kIndexMethodAndProtoRef:
      if (index < collections.MethodIdsSize()) {
        method_ids->push_back(collections.GetMethodId(index));
        return true;
      }
      break;
    case Instruction::kIndexFieldRef:
      if (index < collections.FieldIdsSize()) {
        field_ids->push_back(collections.GetFieldId(index));
        return true;
      }
      break;
    case Instruction::kIndexUnknown:
    case Instruction::kIndexNone:
    case Instruction::kIndexVtableOffset:
    case Instruction::kIndexFieldOffset:
    default:
      break;
  }  // switch
  return false;
}

/*
 * Get all the types, strings, methods, and fields referred to from bytecode.
 */
static bool GetIdsFromByteCode(Collections& collections,
                               const CodeItem* code,
                               std::vector<TypeId*>* type_ids,
                               std::vector<StringId*>* string_ids,
                               std::vector<MethodId*>* method_ids,
                               std::vector<FieldId*>* field_ids) {
  bool has_id = false;
  // Iterate over all instructions.
  const uint16_t* insns = code->Insns();
  for (uint32_t insn_idx = 0; insn_idx < code->InsnsSize();) {
    const Instruction* instruction = Instruction::At(&insns[insn_idx]);
    const uint32_t insn_width = instruction->SizeInCodeUnits();
    if (insn_width == 0) {
      break;
    }
    has_id |= GetIdFromInstruction(collections,
                                   instruction,
                                   type_ids,
                                   string_ids,
                                   method_ids,
                                   field_ids);
    insn_idx += insn_width;
  }  // for
  return has_id;
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
    case DexFile::kDexAnnotationMethodType: {
      const uint32_t proto_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetProtoId(GetProtoId(proto_index));
      break;
    }
    case DexFile::kDexAnnotationMethodHandle: {
      const uint32_t method_handle_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item->SetMethodHandle(GetMethodHandle(method_handle_index));
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
  const DexFile::StringId& disk_string_id = dex_file.GetStringId(dex::StringIndex(i));
  StringData* string_data = new StringData(dex_file.GetStringData(disk_string_id));
  string_datas_.AddItem(string_data, disk_string_id.string_data_off_);

  StringId* string_id = new StringId(string_data);
  string_ids_.AddIndexedItem(string_id, StringIdsOffset() + i * StringId::ItemSize(), i);
}

void Collections::CreateTypeId(const DexFile& dex_file, uint32_t i) {
  const DexFile::TypeId& disk_type_id = dex_file.GetTypeId(dex::TypeIndex(i));
  TypeId* type_id = new TypeId(GetStringId(disk_type_id.descriptor_idx_.index_));
  type_ids_.AddIndexedItem(type_id, TypeIdsOffset() + i * TypeId::ItemSize(), i);
}

void Collections::CreateProtoId(const DexFile& dex_file, uint32_t i) {
  const DexFile::ProtoId& disk_proto_id = dex_file.GetProtoId(i);
  const DexFile::TypeList* type_list = dex_file.GetProtoParameters(disk_proto_id);
  TypeList* parameter_type_list = CreateTypeList(type_list, disk_proto_id.parameters_off_);

  ProtoId* proto_id = new ProtoId(GetStringId(disk_proto_id.shorty_idx_.index_),
                                  GetTypeId(disk_proto_id.return_type_idx_.index_),
                                  parameter_type_list);
  proto_ids_.AddIndexedItem(proto_id, ProtoIdsOffset() + i * ProtoId::ItemSize(), i);
}

void Collections::CreateFieldId(const DexFile& dex_file, uint32_t i) {
  const DexFile::FieldId& disk_field_id = dex_file.GetFieldId(i);
  FieldId* field_id = new FieldId(GetTypeId(disk_field_id.class_idx_.index_),
                                  GetTypeId(disk_field_id.type_idx_.index_),
                                  GetStringId(disk_field_id.name_idx_.index_));
  field_ids_.AddIndexedItem(field_id, FieldIdsOffset() + i * FieldId::ItemSize(), i);
}

void Collections::CreateMethodId(const DexFile& dex_file, uint32_t i) {
  const DexFile::MethodId& disk_method_id = dex_file.GetMethodId(i);
  MethodId* method_id = new MethodId(GetTypeId(disk_method_id.class_idx_.index_),
                                     GetProtoId(disk_method_id.proto_idx_),
                                     GetStringId(disk_method_id.name_idx_.index_));
  method_ids_.AddIndexedItem(method_id, MethodIdsOffset() + i * MethodId::ItemSize(), i);
}

void Collections::CreateClassDef(const DexFile& dex_file, uint32_t i) {
  const DexFile::ClassDef& disk_class_def = dex_file.GetClassDef(i);
  const TypeId* class_type = GetTypeId(disk_class_def.class_idx_.index_);
  uint32_t access_flags = disk_class_def.access_flags_;
  const TypeId* superclass = GetTypeIdOrNullPtr(disk_class_def.superclass_idx_.index_);

  const DexFile::TypeList* type_list = dex_file.GetInterfacesList(disk_class_def);
  TypeList* interfaces_type_list = CreateTypeList(type_list, disk_class_def.interfaces_off_);

  const StringId* source_file = GetStringIdOrNullPtr(disk_class_def.source_file_idx_.index_);
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
  auto found_type_list = TypeLists().find(offset);
  if (found_type_list != TypeLists().end()) {
    return found_type_list->second.get();
  }
  TypeIdVector* type_vector = new TypeIdVector();
  uint32_t size = dex_type_list->Size();
  for (uint32_t index = 0; index < size; ++index) {
    type_vector->push_back(GetTypeId(dex_type_list->GetTypeItem(index).type_idx_.index_));
  }
  TypeList* new_type_list = new TypeList(type_vector);
  type_lists_.AddItem(new_type_list, offset);
  return new_type_list;
}

EncodedArrayItem* Collections::CreateEncodedArrayItem(const uint8_t* static_data, uint32_t offset) {
  if (static_data == nullptr) {
    return nullptr;
  }
  auto found_encoded_array_item = EncodedArrayItems().find(offset);
  if (found_encoded_array_item != EncodedArrayItems().end()) {
    return found_encoded_array_item->second.get();
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
  auto found_annotation_item = AnnotationItems().find(offset);
  if (found_annotation_item != AnnotationItems().end()) {
    return found_annotation_item->second.get();
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
    const DexFile::AnnotationSetItem* disk_annotations_item, uint32_t offset) {
  if (disk_annotations_item == nullptr || (disk_annotations_item->size_ == 0 && offset == 0)) {
    return nullptr;
  }
  auto found_anno_set_item = AnnotationSetItems().find(offset);
  if (found_anno_set_item != AnnotationSetItems().end()) {
    return found_anno_set_item->second.get();
  }
  std::vector<AnnotationItem*>* items = new std::vector<AnnotationItem*>();
  for (uint32_t i = 0; i < disk_annotations_item->size_; ++i) {
    const DexFile::AnnotationItem* annotation =
        dex_file.GetAnnotationItem(disk_annotations_item, i);
    if (annotation == nullptr) {
      continue;
    }
    AnnotationItem* annotation_item =
        CreateAnnotationItem(annotation, disk_annotations_item->entries_[i]);
    items->push_back(annotation_item);
  }
  AnnotationSetItem* annotation_set_item = new AnnotationSetItem(items);
  annotation_set_items_.AddItem(annotation_set_item, offset);
  return annotation_set_item;
}

AnnotationsDirectoryItem* Collections::CreateAnnotationsDirectoryItem(const DexFile& dex_file,
    const DexFile::AnnotationsDirectoryItem* disk_annotations_item, uint32_t offset) {
  auto found_anno_dir_item = AnnotationsDirectoryItems().find(offset);
  if (found_anno_dir_item != AnnotationsDirectoryItems().end()) {
    return found_anno_dir_item->second.get();
  }
  const DexFile::AnnotationSetItem* class_set_item =
      dex_file.GetClassAnnotationSet(disk_annotations_item);
  AnnotationSetItem* class_annotation = nullptr;
  if (class_set_item != nullptr) {
    uint32_t item_offset = disk_annotations_item->class_annotations_off_;
    class_annotation = CreateAnnotationSetItem(dex_file, class_set_item, item_offset);
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
          CreateAnnotationSetItem(dex_file, field_set_item, annotation_set_offset);
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
          CreateAnnotationSetItem(dex_file, method_set_item, annotation_set_offset);
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
  auto found_set_ref_list = AnnotationSetRefLists().find(offset);
  if (found_set_ref_list != AnnotationSetRefLists().end()) {
    set_ref_list = found_set_ref_list->second.get();
  }
  if (set_ref_list == nullptr) {
    std::vector<AnnotationSetItem*>* annotations = new std::vector<AnnotationSetItem*>();
    for (uint32_t i = 0; i < annotation_set_ref_list->size_; ++i) {
      const DexFile::AnnotationSetItem* annotation_set_item =
          dex_file.GetSetRefItemItem(&annotation_set_ref_list->list_[i]);
      uint32_t set_offset = annotation_set_ref_list->list_[i].annotations_off_;
      annotations->push_back(CreateAnnotationSetItem(dex_file, annotation_set_item, set_offset));
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
    debug_info = debug_info_items_.GetExistingObject(disk_code_item.debug_info_off_);
    if (debug_info == nullptr) {
      uint32_t debug_info_size = GetDebugInfoStreamSize(debug_info_stream);
      uint8_t* debug_info_buffer = new uint8_t[debug_info_size];
      memcpy(debug_info_buffer, debug_info_stream, debug_info_size);
      debug_info = new DebugInfoItem(debug_info_size, debug_info_buffer);
      debug_info_items_.AddItem(debug_info, disk_code_item.debug_info_off_);
    }
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
          break;
        }
      }
      if (handlers == nullptr) {
        bool catch_all = false;
        TypeAddrPairVector* addr_pairs = new TypeAddrPairVector();
        for (CatchHandlerIterator it(disk_code_item, *disk_try_item); it.HasNext(); it.Next()) {
          const dex::TypeIndex type_index = it.GetHandlerTypeIndex();
          const TypeId* type_id = GetTypeIdOrNullPtr(type_index.index_);
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
    // Manually walk catch handlers list and add any missing handlers unreferenced by try items.
    const uint8_t* handlers_base = DexFile::GetCatchHandlerData(disk_code_item, 0);
    const uint8_t* handlers_data = handlers_base;
    uint32_t handlers_size = DecodeUnsignedLeb128(&handlers_data);
    while (handlers_size > handler_list->size()) {
      bool already_added = false;
      uint16_t handler_off = handlers_data - handlers_base;
      for (std::unique_ptr<const CatchHandler>& existing_handlers : *handler_list) {
        if (handler_off == existing_handlers->GetListOffset()) {
          already_added = true;
          break;
        }
      }
      int32_t size = DecodeSignedLeb128(&handlers_data);
      bool has_catch_all = size <= 0;
      if (has_catch_all) {
        size = -size;
      }
      if (already_added) {
        for (int32_t i = 0; i < size; i++) {
          DecodeUnsignedLeb128(&handlers_data);
          DecodeUnsignedLeb128(&handlers_data);
        }
        if (has_catch_all) {
          DecodeUnsignedLeb128(&handlers_data);
        }
        continue;
      }
      TypeAddrPairVector* addr_pairs = new TypeAddrPairVector();
      for (int32_t i = 0; i < size; i++) {
        const TypeId* type_id = GetTypeIdOrNullPtr(DecodeUnsignedLeb128(&handlers_data));
        uint32_t addr = DecodeUnsignedLeb128(&handlers_data);
        addr_pairs->push_back(
            std::unique_ptr<const TypeAddrPair>(new TypeAddrPair(type_id, addr)));
      }
      if (has_catch_all) {
        uint32_t addr = DecodeUnsignedLeb128(&handlers_data);
        addr_pairs->push_back(
            std::unique_ptr<const TypeAddrPair>(new TypeAddrPair(nullptr, addr)));
      }
      const CatchHandler* handler = new CatchHandler(has_catch_all, handler_off, addr_pairs);
      handler_list->push_back(std::unique_ptr<const CatchHandler>(handler));
    }
  }

  uint32_t size = GetCodeItemSize(disk_code_item);
  CodeItem* code_item = new CodeItem(
      registers_size, ins_size, outs_size, debug_info, insns_size, insns, tries, handler_list);
  code_item->SetSize(size);
  code_items_.AddItem(code_item, offset);
  // Add "fixup" references to types, strings, methods, and fields.
  // This is temporary, as we will probably want more detailed parsing of the
  // instructions here.
  std::unique_ptr<std::vector<TypeId*>> type_ids(new std::vector<TypeId*>());
  std::unique_ptr<std::vector<StringId*>> string_ids(new std::vector<StringId*>());
  std::unique_ptr<std::vector<MethodId*>> method_ids(new std::vector<MethodId*>());
  std::unique_ptr<std::vector<FieldId*>> field_ids(new std::vector<FieldId*>());
  if (GetIdsFromByteCode(*this,
                         code_item,
                         type_ids.get(),
                         string_ids.get(),
                         method_ids.get(),
                         field_ids.get())) {
    CodeFixups* fixups = new CodeFixups(type_ids.release(),
                                        string_ids.release(),
                                        method_ids.release(),
                                        field_ids.release());
    code_item->SetCodeFixups(fixups);
  }

  return code_item;
}

MethodItem* Collections::GenerateMethodItem(const DexFile& dex_file, ClassDataItemIterator& cdii) {
  MethodId* method_id = GetMethodId(cdii.GetMemberIndex());
  uint32_t access_flags = cdii.GetRawMemberAccessFlags();
  const DexFile::CodeItem* disk_code_item = cdii.GetMethodCodeItem();
  CodeItem* code_item = code_items_.GetExistingObject(cdii.GetMethodCodeItemOffset());
  DebugInfoItem* debug_info = nullptr;
  if (disk_code_item != nullptr) {
    if (code_item == nullptr) {
      code_item = CreateCodeItem(dex_file, *disk_code_item, cdii.GetMethodCodeItemOffset());
    }
    debug_info = code_item->DebugInfo();
  }
  if (debug_info != nullptr) {
    bool is_static = (access_flags & kAccStatic) != 0;
    dex_file.DecodeDebugLocalInfo(
        disk_code_item, is_static, cdii.GetMemberIndex(), GetLocalsCb, debug_info);
    dex_file.DecodeDebugPositionInfo(disk_code_item, GetPositionsCb, debug_info);
  }
  return new MethodItem(access_flags, method_id, code_item);
}

ClassData* Collections::CreateClassData(
    const DexFile& dex_file, const uint8_t* encoded_data, uint32_t offset) {
  // Read the fields and methods defined by the class, resolving the circular reference from those
  // to classes by setting class at the same time.
  ClassData* class_data = class_datas_.GetExistingObject(offset);
  if (class_data == nullptr && encoded_data != nullptr) {
    ClassDataItemIterator cdii(dex_file, encoded_data);
    // Static fields.
    FieldItemVector* static_fields = new FieldItemVector();
    for (; cdii.HasNextStaticField(); cdii.Next()) {
      FieldId* field_item = GetFieldId(cdii.GetMemberIndex());
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      static_fields->push_back(std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Instance fields.
    FieldItemVector* instance_fields = new FieldItemVector();
    for (; cdii.HasNextInstanceField(); cdii.Next()) {
      FieldId* field_item = GetFieldId(cdii.GetMemberIndex());
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      instance_fields->push_back(
          std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Direct methods.
    MethodItemVector* direct_methods = new MethodItemVector();
    for (; cdii.HasNextDirectMethod(); cdii.Next()) {
      direct_methods->push_back(std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, cdii)));
    }
    // Virtual methods.
    MethodItemVector* virtual_methods = new MethodItemVector();
    for (; cdii.HasNextVirtualMethod(); cdii.Next()) {
      virtual_methods->push_back(std::unique_ptr<MethodItem>(GenerateMethodItem(dex_file, cdii)));
    }
    class_data = new ClassData(static_fields, instance_fields, direct_methods, virtual_methods);
    class_data->SetSize(cdii.EndDataPointer() - encoded_data);
    class_datas_.AddItem(class_data, offset);
  }
  return class_data;
}

void Collections::CreateCallSitesAndMethodHandles(const DexFile& dex_file) {
  // Iterate through the map list and set the offset of the CallSiteIds and MethodHandleItems.
  const DexFile::MapList* map =
      reinterpret_cast<const DexFile::MapList*>(dex_file.Begin() + MapListOffset());
  for (uint32_t i = 0; i < map->size_; ++i) {
    const DexFile::MapItem* item = map->list_ + i;
    switch (item->type_) {
      case DexFile::kDexTypeCallSiteIdItem:
        SetCallSiteIdsOffset(item->offset_);
        break;
      case DexFile::kDexTypeMethodHandleItem:
        SetMethodHandleItemsOffset(item->offset_);
        break;
      default:
        break;
    }
  }
  // Populate MethodHandleItems first (CallSiteIds may depend on them).
  for (uint32_t i = 0; i < dex_file.NumMethodHandles(); i++) {
    CreateMethodHandleItem(dex_file, i);
  }
  // Populate CallSiteIds.
  for (uint32_t i = 0; i < dex_file.NumCallSiteIds(); i++) {
    CreateCallSiteId(dex_file, i);
  }
}

void Collections::CreateCallSiteId(const DexFile& dex_file, uint32_t i) {
  const DexFile::CallSiteIdItem& disk_call_site_id = dex_file.GetCallSiteId(i);
  const uint8_t* disk_call_item_ptr = dex_file.Begin() + disk_call_site_id.data_off_;
  EncodedArrayItem* call_site_item =
      CreateEncodedArrayItem(disk_call_item_ptr, disk_call_site_id.data_off_);

  CallSiteId* call_site_id = new CallSiteId(call_site_item);
  call_site_ids_.AddIndexedItem(call_site_id, CallSiteIdsOffset() + i * CallSiteId::ItemSize(), i);
}

void Collections::CreateMethodHandleItem(const DexFile& dex_file, uint32_t i) {
  const DexFile::MethodHandleItem& disk_method_handle = dex_file.GetMethodHandle(i);
  uint16_t index = disk_method_handle.field_or_method_idx_;
  DexFile::MethodHandleType type =
      static_cast<DexFile::MethodHandleType>(disk_method_handle.method_handle_type_);
  bool is_invoke = type == DexFile::MethodHandleType::kInvokeStatic ||
                   type == DexFile::MethodHandleType::kInvokeInstance ||
                   type == DexFile::MethodHandleType::kInvokeConstructor;
  static_assert(DexFile::MethodHandleType::kLast == DexFile::MethodHandleType::kInvokeConstructor,
                "Unexpected method handle types.");
  IndexedItem* field_or_method_id;
  if (is_invoke) {
    field_or_method_id = GetMethodId(index);
  } else {
    field_or_method_id = GetFieldId(index);
  }
  MethodHandleItem* method_handle = new MethodHandleItem(type, field_or_method_id);
  method_handle_items_.AddIndexedItem(
      method_handle, MethodHandleItemsOffset() + i * MethodHandleItem::ItemSize(), i);
}

static uint32_t HeaderOffset(const dex_ir::Collections& collections ATTRIBUTE_UNUSED) {
  return 0;
}

static uint32_t HeaderSize(const dex_ir::Collections& collections ATTRIBUTE_UNUSED) {
  // Size is in elements, so there is only one header.
  return 1;
}

// The description of each dex file section type.
struct FileSectionDescriptor {
 public:
  std::string name;
  uint16_t type;
  // A function that when applied to a collection object, gives the size of the section.
  std::function<uint32_t(const dex_ir::Collections&)> size_fn;
  // A function that when applied to a collection object, gives the offset of the section.
  std::function<uint32_t(const dex_ir::Collections&)> offset_fn;
};

static const FileSectionDescriptor kFileSectionDescriptors[] = {
  {
    "Header",
    DexFile::kDexTypeHeaderItem,
    &HeaderSize,
    &HeaderOffset,
  }, {
    "StringId",
    DexFile::kDexTypeStringIdItem,
    &dex_ir::Collections::StringIdsSize,
    &dex_ir::Collections::StringIdsOffset
  }, {
    "TypeId",
    DexFile::kDexTypeTypeIdItem,
    &dex_ir::Collections::TypeIdsSize,
    &dex_ir::Collections::TypeIdsOffset
  }, {
    "ProtoId",
    DexFile::kDexTypeProtoIdItem,
    &dex_ir::Collections::ProtoIdsSize,
    &dex_ir::Collections::ProtoIdsOffset
  }, {
    "FieldId",
    DexFile::kDexTypeFieldIdItem,
    &dex_ir::Collections::FieldIdsSize,
    &dex_ir::Collections::FieldIdsOffset
  }, {
    "MethodId",
    DexFile::kDexTypeMethodIdItem,
    &dex_ir::Collections::MethodIdsSize,
    &dex_ir::Collections::MethodIdsOffset
  }, {
    "ClassDef",
    DexFile::kDexTypeClassDefItem,
    &dex_ir::Collections::ClassDefsSize,
    &dex_ir::Collections::ClassDefsOffset
  }, {
    "CallSiteId",
    DexFile::kDexTypeCallSiteIdItem,
    &dex_ir::Collections::CallSiteIdsSize,
    &dex_ir::Collections::CallSiteIdsOffset
  }, {
    "MethodHandle",
    DexFile::kDexTypeMethodHandleItem,
    &dex_ir::Collections::MethodHandleItemsSize,
    &dex_ir::Collections::MethodHandleItemsOffset
  }, {
    "StringData",
    DexFile::kDexTypeStringDataItem,
    &dex_ir::Collections::StringDatasSize,
    &dex_ir::Collections::StringDatasOffset
  }, {
    "TypeList",
    DexFile::kDexTypeTypeList,
    &dex_ir::Collections::TypeListsSize,
    &dex_ir::Collections::TypeListsOffset
  }, {
    "EncArr",
    DexFile::kDexTypeEncodedArrayItem,
    &dex_ir::Collections::EncodedArrayItemsSize,
    &dex_ir::Collections::EncodedArrayItemsOffset
  }, {
    "Annotation",
    DexFile::kDexTypeAnnotationItem,
    &dex_ir::Collections::AnnotationItemsSize,
    &dex_ir::Collections::AnnotationItemsOffset
  }, {
    "AnnoSet",
    DexFile::kDexTypeAnnotationSetItem,
    &dex_ir::Collections::AnnotationSetItemsSize,
    &dex_ir::Collections::AnnotationSetItemsOffset
  }, {
    "AnnoSetRL",
    DexFile::kDexTypeAnnotationSetRefList,
    &dex_ir::Collections::AnnotationSetRefListsSize,
    &dex_ir::Collections::AnnotationSetRefListsOffset
  }, {
    "AnnoDir",
    DexFile::kDexTypeAnnotationsDirectoryItem,
    &dex_ir::Collections::AnnotationsDirectoryItemsSize,
    &dex_ir::Collections::AnnotationsDirectoryItemsOffset
  }, {
    "DebugInfo",
    DexFile::kDexTypeDebugInfoItem,
    &dex_ir::Collections::DebugInfoItemsSize,
    &dex_ir::Collections::DebugInfoItemsOffset
  }, {
    "CodeItem",
    DexFile::kDexTypeCodeItem,
    &dex_ir::Collections::CodeItemsSize,
    &dex_ir::Collections::CodeItemsOffset
  }, {
    "ClassData",
    DexFile::kDexTypeClassDataItem,
    &dex_ir::Collections::ClassDatasSize,
    &dex_ir::Collections::ClassDatasOffset
  }
};

std::vector<dex_ir::DexFileSection> GetSortedDexFileSections(dex_ir::Header* header,
                                                             dex_ir::SortDirection direction) {
  const dex_ir::Collections& collections = header->GetCollections();
  std::vector<dex_ir::DexFileSection> sorted_sections;
  // Build the table that will map from offset to color
  for (const FileSectionDescriptor& s : kFileSectionDescriptors) {
    sorted_sections.push_back(dex_ir::DexFileSection(s.name,
                                                     s.type,
                                                     s.size_fn(collections),
                                                     s.offset_fn(collections)));
  }
  // Sort by offset.
  std::sort(sorted_sections.begin(),
            sorted_sections.end(),
            [=](dex_ir::DexFileSection& a, dex_ir::DexFileSection& b) {
              if (direction == SortDirection::kSortDescending) {
                return a.offset > b.offset;
              } else {
                return a.offset < b.offset;
              }
            });
  return sorted_sections;
}

}  // namespace dex_ir
}  // namespace art
