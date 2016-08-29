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
 * Implementation file of the dex file intermediate representation.
 *
 * Utilities for reading dex files into an internal representation,
 * manipulating them, and writing them out.
 */

#include "dex_ir.h"

#include <map>
#include <vector>

#include "dex_file.h"
#include "dex_file-inl.h"
#include "utils.h"

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

static bool GetPositionsCb(void* context, const DexFile::PositionInfo& entry) {
  DebugInfoItem* debug_info = reinterpret_cast<DebugInfoItem*>(context);
  std::vector<std::unique_ptr<PositionInfo>>& positions = debug_info->GetPositionInfo();
  positions.push_back(std::unique_ptr<PositionInfo>(new PositionInfo(entry.address_, entry.line_)));
  return false;
}

static void GetLocalsCb(void* context, const DexFile::LocalInfo& entry) {
  DebugInfoItem* debug_info = reinterpret_cast<DebugInfoItem*>(context);
  std::vector<std::unique_ptr<LocalInfo>>& locals = debug_info->GetLocalInfo();
  const char* name = entry.name_ != nullptr ? entry.name_ : "(null)";
  const char* signature = entry.signature_ != nullptr ? entry.signature_ : "";
  locals.push_back(std::unique_ptr<LocalInfo>(
      new LocalInfo(name, entry.descriptor_, signature, entry.start_address_,
                    entry.end_address_, entry.reg_)));
}
}  // namespace

Header::Header(const DexFile& dex_file) : dex_file_(dex_file) {
  const DexFile::Header& disk_header = dex_file.GetHeader();
  memcpy(magic_, disk_header.magic_, sizeof(magic_));
  checksum_ = disk_header.checksum_;
  // TODO(sehr): clearly the signature will need to be recomputed before dumping.
  memcpy(signature_, disk_header.signature_, sizeof(signature_));
  endian_tag_ = disk_header.endian_tag_;
  file_size_ = disk_header.file_size_;
  header_size_ = disk_header.header_size_;
  link_size_ = disk_header.link_size_;
  link_offset_ = disk_header.link_off_;
  data_size_ = disk_header.data_size_;
  data_offset_ = disk_header.data_off_;
  // Walk the rest of the header fields.
  string_ids_.SetOffset(disk_header.string_ids_off_);
  for (uint32_t i = 0; i < dex_file_.NumStringIds(); ++i) {
    string_ids_.AddWithPosition(i, new StringId(dex_file_.GetStringId(i), *this));
  }
  type_ids_.SetOffset(disk_header.type_ids_off_);
  for (uint32_t i = 0; i < dex_file_.NumTypeIds(); ++i) {
    type_ids_.AddWithPosition(i, new TypeId(dex_file_.GetTypeId(i), *this));
  }
  proto_ids_.SetOffset(disk_header.proto_ids_off_);
  for (uint32_t i = 0; i < dex_file_.NumProtoIds(); ++i) {
    proto_ids_.AddWithPosition(i, new ProtoId(dex_file_.GetProtoId(i), *this));
  }
  field_ids_.SetOffset(disk_header.field_ids_off_);
  for (uint32_t i = 0; i < dex_file_.NumFieldIds(); ++i) {
    field_ids_.AddWithPosition(i, new FieldId(dex_file_.GetFieldId(i), *this));
  }
  method_ids_.SetOffset(disk_header.method_ids_off_);
  for (uint32_t i = 0; i < dex_file_.NumMethodIds(); ++i) {
    method_ids_.AddWithPosition(i, new MethodId(dex_file_.GetMethodId(i), *this));
  }
  class_defs_.SetOffset(disk_header.class_defs_off_);
  for (uint32_t i = 0; i < dex_file_.NumClassDefs(); ++i) {
    class_defs_.AddWithPosition(i, new ClassDef(dex_file_.GetClassDef(i), *this));
  }
}

ArrayItem::ArrayItem(Header& header, const uint8_t** data, uint8_t type, uint8_t length) {
  Read(header, data, type, length);
}

ArrayItem::ArrayItem(Header& header, const uint8_t** data) {
  const uint8_t encoded_value = *(*data)++;
  Read(header, data, encoded_value & 0x1f, encoded_value >> 5);
}

void ArrayItem::Read(Header& header, const uint8_t** data, uint8_t type, uint8_t length) {
  type_ = type;
  switch (type_) {
    case DexFile::kDexAnnotationByte:
      item_.byte_val_ = static_cast<int8_t>(ReadVarWidth(data, length, false));
      break;
    case DexFile::kDexAnnotationShort:
      item_.short_val_ = static_cast<int16_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationChar:
      item_.char_val_ = static_cast<uint16_t>(ReadVarWidth(data, length, false));
      break;
    case DexFile::kDexAnnotationInt:
      item_.int_val_ = static_cast<int32_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationLong:
      item_.long_val_ = static_cast<int64_t>(ReadVarWidth(data, length, true));
      break;
    case DexFile::kDexAnnotationFloat: {
      // Fill on right.
      union {
        float f;
        uint32_t data;
      } conv;
      conv.data = static_cast<uint32_t>(ReadVarWidth(data, length, false)) << (3 - length) * 8;
      item_.float_val_ = conv.f;
      break;
    }
    case DexFile::kDexAnnotationDouble: {
      // Fill on right.
      union {
        double d;
        uint64_t data;
      } conv;
      conv.data = ReadVarWidth(data, length, false) << (7 - length) * 8;
      item_.double_val_ = conv.d;
      break;
    }
    case DexFile::kDexAnnotationString: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item_.string_val_ = header.StringIds()[string_index].get();
      break;
    }
    case DexFile::kDexAnnotationType: {
      const uint32_t string_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item_.string_val_ = header.TypeIds()[string_index]->GetStringId();
      break;
    }
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum: {
      const uint32_t field_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item_.field_val_ = header.FieldIds()[field_index].get();
      break;
    }
    case DexFile::kDexAnnotationMethod: {
      const uint32_t method_index = static_cast<uint32_t>(ReadVarWidth(data, length, false));
      item_.method_val_ = header.MethodIds()[method_index].get();
      break;
    }
    case DexFile::kDexAnnotationArray: {
      item_.annotation_array_val_ = new std::vector<std::unique_ptr<ArrayItem>>();
      // Decode all elements.
      const uint32_t size = DecodeUnsignedLeb128(data);
      for (uint32_t i = 0; i < size; i++) {
        item_.annotation_array_val_->push_back(
            std::unique_ptr<ArrayItem>(new ArrayItem(header, data)));
      }
      break;
    }
    case DexFile::kDexAnnotationAnnotation: {
      const uint32_t type_idx = DecodeUnsignedLeb128(data);
      item_.annotation_annotation_val_.string_ = header.TypeIds()[type_idx]->GetStringId();
      item_.annotation_annotation_val_.array_ = new std::vector<std::unique_ptr<NameValuePair>>();
      // Decode all name=value pairs.
      const uint32_t size = DecodeUnsignedLeb128(data);
      for (uint32_t i = 0; i < size; i++) {
        const uint32_t name_index = DecodeUnsignedLeb128(data);
        item_.annotation_annotation_val_.array_->push_back(std::unique_ptr<NameValuePair>(
            new NameValuePair(header.StringIds()[name_index].get(), new ArrayItem(header, data))));
      }
      break;
    }
    case DexFile::kDexAnnotationNull:
      break;
    case DexFile::kDexAnnotationBoolean:
      item_.bool_val_ = (length != 0);
      break;
    default:
      break;
  }
}

ClassDef::ClassDef(const DexFile::ClassDef& disk_class_def, Header& header) {
  class_type_ = header.TypeIds()[disk_class_def.class_idx_].get();
  access_flags_ = disk_class_def.access_flags_;
  superclass_ = header.GetTypeIdOrNullPtr(disk_class_def.superclass_idx_);

  const DexFile::TypeList* type_list = header.GetDexFile().GetInterfacesList(disk_class_def);
  interfaces_offset_ = disk_class_def.interfaces_off_;
  if (type_list != nullptr) {
    for (uint32_t index = 0; index < type_list->Size(); ++index) {
      interfaces_.push_back(header.TypeIds()[type_list->GetTypeItem(index).type_idx_].get());
    }
  }
  source_file_ = header.GetStringIdOrNullPtr(disk_class_def.source_file_idx_);
  // Annotations.
  const DexFile::AnnotationsDirectoryItem* disk_annotations_directory_item =
      header.GetDexFile().GetAnnotationsDirectory(disk_class_def);
  if (disk_annotations_directory_item == nullptr) {
    annotations_.reset(nullptr);
  } else {
    annotations_.reset(new AnnotationsDirectoryItem(disk_annotations_directory_item, header));
    annotations_->SetOffset(disk_class_def.annotations_off_);
  }
  // Static field initializers.
  static_values_ = nullptr;
  const uint8_t* static_data = header.GetDexFile().GetEncodedStaticFieldValuesArray(disk_class_def);
  if (static_data != nullptr) {
    uint32_t static_value_count = static_data == nullptr ? 0 : DecodeUnsignedLeb128(&static_data);
    if (static_value_count > 0) {
      static_values_ = new std::vector<std::unique_ptr<ArrayItem>>();
      for (uint32_t i = 0; i < static_value_count; ++i) {
        static_values_->push_back(std::unique_ptr<ArrayItem>(new ArrayItem(header, &static_data)));
      }
    }
  }
  // Read the fields and methods defined by the class, resolving the circular reference from those
  // to classes by setting class at the same time.
  const uint8_t* encoded_data = header.GetDexFile().GetClassData(disk_class_def);
  class_data_.SetOffset(disk_class_def.class_data_off_);
  if (encoded_data != nullptr) {
    ClassDataItemIterator cdii(header.GetDexFile(), encoded_data);
    // Static fields.
    for (uint32_t i = 0; cdii.HasNextStaticField(); i++, cdii.Next()) {
      FieldId* field_item = header.FieldIds()[cdii.GetMemberIndex()].get();
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      class_data_.StaticFields().push_back(
          std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Instance fields.
    for (uint32_t i = 0; cdii.HasNextInstanceField(); i++, cdii.Next()) {
      FieldId* field_item = header.FieldIds()[cdii.GetMemberIndex()].get();
      uint32_t access_flags = cdii.GetRawMemberAccessFlags();
      class_data_.InstanceFields().push_back(
          std::unique_ptr<FieldItem>(new FieldItem(access_flags, field_item)));
    }
    // Direct methods.
    for (uint32_t i = 0; cdii.HasNextDirectMethod(); i++, cdii.Next()) {
      class_data_.DirectMethods().push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(header, cdii)));
    }
    // Virtual methods.
    for (uint32_t i = 0; cdii.HasNextVirtualMethod(); i++, cdii.Next()) {
      class_data_.VirtualMethods().push_back(
          std::unique_ptr<MethodItem>(GenerateMethodItem(header, cdii)));
    }
  }
}

MethodItem* ClassDef::GenerateMethodItem(Header& header, ClassDataItemIterator& cdii) {
  MethodId* method_item = header.MethodIds()[cdii.GetMemberIndex()].get();
  uint32_t access_flags = cdii.GetRawMemberAccessFlags();
  const DexFile::CodeItem* disk_code_item = cdii.GetMethodCodeItem();
  CodeItem* code_item = nullptr;
  DebugInfoItem* debug_info = nullptr;
  if (disk_code_item != nullptr) {
    code_item = new CodeItem(*disk_code_item, header);
    code_item->SetOffset(cdii.GetMethodCodeItemOffset());
    debug_info = code_item->DebugInfo();
  }
  if (debug_info != nullptr) {
    bool is_static = (access_flags & kAccStatic) != 0;
    header.GetDexFile().DecodeDebugLocalInfo(
        disk_code_item, is_static, cdii.GetMemberIndex(), GetLocalsCb, debug_info);
    header.GetDexFile().DecodeDebugPositionInfo(disk_code_item, GetPositionsCb, debug_info);
  }
  return new MethodItem(access_flags, method_item, code_item);
}

CodeItem::CodeItem(const DexFile::CodeItem& disk_code_item, Header& header) {
  registers_size_ = disk_code_item.registers_size_;
  ins_size_ = disk_code_item.ins_size_;
  outs_size_ = disk_code_item.outs_size_;
  tries_size_ = disk_code_item.tries_size_;

  const uint8_t* debug_info_stream = header.GetDexFile().GetDebugInfoStream(&disk_code_item);
  if (debug_info_stream != nullptr) {
    debug_info_.reset(new DebugInfoItem());
  } else {
    debug_info_.reset(nullptr);
  }

  insns_size_ = disk_code_item.insns_size_in_code_units_;
  insns_.reset(new uint16_t[insns_size_]);
  memcpy(insns_.get(), disk_code_item.insns_, insns_size_ * sizeof(uint16_t));

  if (tries_size_ > 0) {
    tries_ = new std::vector<std::unique_ptr<const TryItem>>();
    for (uint32_t i = 0; i < tries_size_; ++i) {
      const DexFile::TryItem* disk_try_item = header.GetDexFile().GetTryItems(disk_code_item, i);
      tries_->push_back(std::unique_ptr<const TryItem>(
          new TryItem(*disk_try_item, disk_code_item, header)));
    }
  } else {
    tries_ = nullptr;
  }
}

AnnotationSetItem::AnnotationSetItem(const DexFile::AnnotationSetItem& disk_annotations_item,
                                     Header& header) {
  if (disk_annotations_item.size_ == 0) {
    return;
  }
  for (uint32_t i = 0; i < disk_annotations_item.size_; ++i) {
    const DexFile::AnnotationItem* annotation =
        header.GetDexFile().GetAnnotationItem(&disk_annotations_item, i);
    if (annotation == nullptr) {
      continue;
    }
    uint8_t visibility = annotation->visibility_;
    const uint8_t* annotation_data = annotation->annotation_;
    ArrayItem* array_item =
        new ArrayItem(header, &annotation_data, DexFile::kDexAnnotationAnnotation, 0);
    items_.push_back(std::unique_ptr<AnnotationItem>(new AnnotationItem(visibility, array_item)));
  }
}

AnnotationsDirectoryItem::AnnotationsDirectoryItem(
    const DexFile::AnnotationsDirectoryItem* disk_annotations_item, Header& header) {
  const DexFile::AnnotationSetItem* class_set_item =
      header.GetDexFile().GetClassAnnotationSet(disk_annotations_item);
  if (class_set_item == nullptr) {
    class_annotation_.reset(nullptr);
  } else {
    class_annotation_.reset(new AnnotationSetItem(*class_set_item, header));
  }
  const DexFile::FieldAnnotationsItem* fields =
      header.GetDexFile().GetFieldAnnotations(disk_annotations_item);
  if (fields != nullptr) {
    for (uint32_t i = 0; i < disk_annotations_item->fields_size_; ++i) {
      FieldId* field_id = header.FieldIds()[fields[i].field_idx_].get();
      const DexFile::AnnotationSetItem* field_set_item =
          header.GetDexFile().GetFieldAnnotationSetItem(fields[i]);
      dex_ir::AnnotationSetItem* annotation_set_item =
          new AnnotationSetItem(*field_set_item, header);
      field_annotations_.push_back(std::unique_ptr<FieldAnnotation>(
          new FieldAnnotation(field_id, annotation_set_item)));
    }
  }
  const DexFile::MethodAnnotationsItem* methods =
      header.GetDexFile().GetMethodAnnotations(disk_annotations_item);
  if (methods != nullptr) {
    for (uint32_t i = 0; i < disk_annotations_item->methods_size_; ++i) {
      MethodId* method_id = header.MethodIds()[methods[i].method_idx_].get();
      const DexFile::AnnotationSetItem* method_set_item =
          header.GetDexFile().GetMethodAnnotationSetItem(methods[i]);
      dex_ir::AnnotationSetItem* annotation_set_item =
          new AnnotationSetItem(*method_set_item, header);
      method_annotations_.push_back(std::unique_ptr<MethodAnnotation>(
          new MethodAnnotation(method_id, annotation_set_item)));
    }
  }
  const DexFile::ParameterAnnotationsItem* parameters =
      header.GetDexFile().GetParameterAnnotations(disk_annotations_item);
  if (parameters != nullptr) {
    for (uint32_t i = 0; i < disk_annotations_item->parameters_size_; ++i) {
      MethodId* method_id = header.MethodIds()[parameters[i].method_idx_].get();
      const DexFile::AnnotationSetRefList* list =
          header.GetDexFile().GetParameterAnnotationSetRefList(&parameters[i]);
      parameter_annotations_.push_back(std::unique_ptr<ParameterAnnotation>(
          new ParameterAnnotation(method_id, list, header)));
    }
  }
}

}  // namespace dex_ir
}  // namespace art
