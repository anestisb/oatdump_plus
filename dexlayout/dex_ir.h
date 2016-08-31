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

#ifndef ART_DEXLAYOUT_DEX_IR_H_
#define ART_DEXLAYOUT_DEX_IR_H_

#include <iostream>
#include <map>
#include <vector>
#include <stdint.h>

#include "dex_file.h"

namespace art {
namespace dex_ir {

// Forward declarations for classes used in containers or pointed to.
class AnnotationsDirectoryItem;
class AnnotationSetItem;
class ArrayItem;
class ClassData;
class ClassDef;
class CodeItem;
class DebugInfoItem;
class FieldId;
class FieldItem;
class Header;
class MapList;
class MapItem;
class MethodId;
class MethodItem;
class ProtoId;
class StringId;
class TryItem;
class TypeId;

// Visitor support
class AbstractDispatcher {
 public:
  AbstractDispatcher() = default;
  virtual ~AbstractDispatcher() { }

  virtual void Dispatch(Header* header) = 0;
  virtual void Dispatch(const StringId* string_id) = 0;
  virtual void Dispatch(const TypeId* type_id) = 0;
  virtual void Dispatch(const ProtoId* proto_id) = 0;
  virtual void Dispatch(const FieldId* field_id) = 0;
  virtual void Dispatch(const MethodId* method_id) = 0;
  virtual void Dispatch(ClassData* class_data) = 0;
  virtual void Dispatch(ClassDef* class_def) = 0;
  virtual void Dispatch(FieldItem* field_item) = 0;
  virtual void Dispatch(MethodItem* method_item) = 0;
  virtual void Dispatch(ArrayItem* array_item) = 0;
  virtual void Dispatch(CodeItem* code_item) = 0;
  virtual void Dispatch(TryItem* try_item) = 0;
  virtual void Dispatch(DebugInfoItem* debug_info_item) = 0;
  virtual void Dispatch(AnnotationSetItem* annotation_set_item) = 0;
  virtual void Dispatch(AnnotationsDirectoryItem* annotations_directory_item) = 0;
  virtual void Dispatch(MapList* map_list) = 0;
  virtual void Dispatch(MapItem* map_item) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(AbstractDispatcher);
};

// Collections become owners of the objects added by moving them into unique pointers.
template<class T> class CollectionWithOffset {
 public:
  CollectionWithOffset() = default;
  std::vector<std::unique_ptr<T>>& Collection() { return collection_; }
  // Read-time support methods
  void AddWithPosition(uint32_t position, T* object) {
    collection_.push_back(std::unique_ptr<T>(object));
    collection_.back()->SetOffset(position);
  }
  // Ordinary object insertion into collection.
  void Insert(T object ATTRIBUTE_UNUSED) {
    // TODO(sehr): add ordered insertion support.
    UNIMPLEMENTED(FATAL) << "Insertion not ready";
  }
  uint32_t GetOffset() const { return offset_; }
  void SetOffset(uint32_t new_offset) { offset_ = new_offset; }
  uint32_t Size() const { return collection_.size(); }

 private:
  std::vector<std::unique_ptr<T>> collection_;
  uint32_t offset_ = 0;
  DISALLOW_COPY_AND_ASSIGN(CollectionWithOffset);
};

class Item {
 public:
  virtual ~Item() { }
  uint32_t GetOffset() const { return offset_; }
  void SetOffset(uint32_t offset) { offset_ = offset; }
 protected:
  uint32_t offset_ = 0;
};

class Header : public Item {
 public:
  explicit Header(const DexFile& dex_file);
  ~Header() OVERRIDE { }

  const DexFile& GetDexFile() const { return dex_file_; }

  const uint8_t* Magic() const { return magic_; }
  uint32_t Checksum() const { return checksum_; }
  const uint8_t* Signature() const { return signature_; }
  uint32_t EndianTag() const { return endian_tag_; }
  uint32_t FileSize() const { return file_size_; }
  uint32_t HeaderSize() const { return header_size_; }
  uint32_t LinkSize() const { return link_size_; }
  uint32_t LinkOffset() const { return link_offset_; }
  uint32_t DataSize() const { return data_size_; }
  uint32_t DataOffset() const { return data_offset_; }

  void SetChecksum(uint32_t new_checksum) { checksum_ = new_checksum; }
  void SetSignature(const uint8_t* new_signature) {
    memcpy(signature_, new_signature, sizeof(signature_));
  }
  void SetFileSize(uint32_t new_file_size) { file_size_ = new_file_size; }
  void SetHeaderSize(uint32_t new_header_size) { header_size_ = new_header_size; }
  void SetLinkSize(uint32_t new_link_size) { link_size_ = new_link_size; }
  void SetLinkOffset(uint32_t new_link_offset) { link_offset_ = new_link_offset; }
  void SetDataSize(uint32_t new_data_size) { data_size_ = new_data_size; }
  void SetDataOffset(uint32_t new_data_offset) { data_offset_ = new_data_offset; }

  // Collections.
  std::vector<std::unique_ptr<StringId>>& StringIds() { return string_ids_.Collection(); }
  std::vector<std::unique_ptr<TypeId>>& TypeIds() { return type_ids_.Collection(); }
  std::vector<std::unique_ptr<ProtoId>>& ProtoIds() { return proto_ids_.Collection(); }
  std::vector<std::unique_ptr<FieldId>>& FieldIds() { return field_ids_.Collection(); }
  std::vector<std::unique_ptr<MethodId>>& MethodIds() { return method_ids_.Collection(); }
  std::vector<std::unique_ptr<ClassDef>>& ClassDefs() { return class_defs_.Collection(); }
  uint32_t StringIdsOffset() const { return string_ids_.GetOffset(); }
  uint32_t TypeIdsOffset() const { return type_ids_.GetOffset(); }
  uint32_t ProtoIdsOffset() const { return proto_ids_.GetOffset(); }
  uint32_t FieldIdsOffset() const { return field_ids_.GetOffset(); }
  uint32_t MethodIdsOffset() const { return method_ids_.GetOffset(); }
  uint32_t ClassDefsOffset() const { return class_defs_.GetOffset(); }
  void SetStringIdsOffset(uint32_t new_offset) { string_ids_.SetOffset(new_offset); }
  void SetTypeIdsOffset(uint32_t new_offset) { type_ids_.SetOffset(new_offset); }
  void SetProtoIdsOffset(uint32_t new_offset) { proto_ids_.SetOffset(new_offset); }
  void SetFieldIdsOffset(uint32_t new_offset) { field_ids_.SetOffset(new_offset); }
  void SetMethodIdsOffset(uint32_t new_offset) { method_ids_.SetOffset(new_offset); }
  void SetClassDefsOffset(uint32_t new_offset) { class_defs_.SetOffset(new_offset); }
  uint32_t StringIdsSize() const { return string_ids_.Size(); }
  uint32_t TypeIdsSize() const { return type_ids_.Size(); }
  uint32_t ProtoIdsSize() const { return proto_ids_.Size(); }
  uint32_t FieldIdsSize() const { return field_ids_.Size(); }
  uint32_t MethodIdsSize() const { return method_ids_.Size(); }
  uint32_t ClassDefsSize() const { return class_defs_.Size(); }

  TypeId* GetTypeIdOrNullPtr(uint16_t index) {
    return index == DexFile::kDexNoIndex16 ? nullptr : TypeIds()[index].get();
  }

  StringId* GetStringIdOrNullPtr(uint32_t index) {
    return index == DexFile::kDexNoIndex ? nullptr : StringIds()[index].get();
  }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  const DexFile& dex_file_;
  uint8_t magic_[8];
  uint32_t checksum_;
  uint8_t signature_[DexFile::kSha1DigestSize];
  uint32_t endian_tag_;
  uint32_t file_size_;
  uint32_t header_size_;
  uint32_t link_size_;
  uint32_t link_offset_;
  uint32_t data_size_;
  uint32_t data_offset_;

  CollectionWithOffset<StringId> string_ids_;
  CollectionWithOffset<TypeId> type_ids_;
  CollectionWithOffset<ProtoId> proto_ids_;
  CollectionWithOffset<FieldId> field_ids_;
  CollectionWithOffset<MethodId> method_ids_;
  CollectionWithOffset<ClassDef> class_defs_;
  DISALLOW_COPY_AND_ASSIGN(Header);
};

class StringId : public Item {
 public:
  StringId(const DexFile::StringId& disk_string_id, Header& header) :
    data_(strdup(header.GetDexFile().GetStringData(disk_string_id))) {
  }
  ~StringId() OVERRIDE { }

  const char* Data() const { return data_.get(); }

  void Accept(AbstractDispatcher* dispatch) const { dispatch->Dispatch(this); }

 private:
  std::unique_ptr<const char> data_;
  DISALLOW_COPY_AND_ASSIGN(StringId);
};

class TypeId : public Item {
 public:
  TypeId(const DexFile::TypeId& disk_type_id, Header& header) :
    string_id_(header.StringIds()[disk_type_id.descriptor_idx_].get()) {
  }
  ~TypeId() OVERRIDE { }

  StringId* GetStringId() const { return string_id_; }

  void Accept(AbstractDispatcher* dispatch) const { dispatch->Dispatch(this); }

 private:
  StringId* string_id_;
  DISALLOW_COPY_AND_ASSIGN(TypeId);
};

class ProtoId : public Item {
 public:
  ProtoId(const DexFile::ProtoId& disk_proto_id, Header& header) {
    shorty_ = header.StringIds()[disk_proto_id.shorty_idx_].get();
    return_type_ = header.TypeIds()[disk_proto_id.return_type_idx_].get();
    DexFileParameterIterator dfpi(header.GetDexFile(), disk_proto_id);
    while (dfpi.HasNext()) {
      parameters_.push_back(header.TypeIds()[dfpi.GetTypeIdx()].get());
      dfpi.Next();
    }
  }
  ~ProtoId() OVERRIDE { }

  const StringId* Shorty() const { return shorty_; }
  const TypeId* ReturnType() const { return return_type_; }
  const std::vector<const TypeId*>& Parameters() const { return parameters_; }

  void Accept(AbstractDispatcher* dispatch) const { dispatch->Dispatch(this); }

 private:
  const StringId* shorty_;
  const TypeId* return_type_;
  std::vector<const TypeId*> parameters_;
  DISALLOW_COPY_AND_ASSIGN(ProtoId);
};

class FieldId : public Item {
 public:
  FieldId(const DexFile::FieldId& disk_field_id, Header& header) {
    class_ = header.TypeIds()[disk_field_id.class_idx_].get();
    type_ = header.TypeIds()[disk_field_id.type_idx_].get();
    name_ = header.StringIds()[disk_field_id.name_idx_].get();
  }
  ~FieldId() OVERRIDE { }

  const TypeId* Class() const { return class_; }
  const TypeId* Type() const { return type_; }
  const StringId* Name() const { return name_; }

  void Accept(AbstractDispatcher* dispatch) const { dispatch->Dispatch(this); }

 private:
  const TypeId* class_;
  const TypeId* type_;
  const StringId* name_;
  DISALLOW_COPY_AND_ASSIGN(FieldId);
};

class MethodId : public Item {
 public:
  MethodId(const DexFile::MethodId& disk_method_id, Header& header) {
    class_ = header.TypeIds()[disk_method_id.class_idx_].get();
    proto_ = header.ProtoIds()[disk_method_id.proto_idx_].get();
    name_ = header.StringIds()[disk_method_id.name_idx_].get();
  }
  ~MethodId() OVERRIDE { }

  const TypeId* Class() const { return class_; }
  const ProtoId* Proto() const { return proto_; }
  const StringId* Name() const { return name_; }

  void Accept(AbstractDispatcher* dispatch) const { dispatch->Dispatch(this); }

 private:
  const TypeId* class_;
  const ProtoId* proto_;
  const StringId* name_;
  DISALLOW_COPY_AND_ASSIGN(MethodId);
};

class FieldItem : public Item {
 public:
  FieldItem(uint32_t access_flags, const FieldId* field_id) :
    access_flags_(access_flags), field_id_(field_id) { }
  ~FieldItem() OVERRIDE { }

  uint32_t GetAccessFlags() const { return access_flags_; }
  const FieldId* GetFieldId() const { return field_id_; }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  uint32_t access_flags_;
  const FieldId* field_id_;
  DISALLOW_COPY_AND_ASSIGN(FieldItem);
};

class MethodItem : public Item {
 public:
  MethodItem(uint32_t access_flags, const MethodId* method_id, const CodeItem* code) :
    access_flags_(access_flags), method_id_(method_id), code_(code) { }
  ~MethodItem() OVERRIDE { }

  uint32_t GetAccessFlags() const { return access_flags_; }
  const MethodId* GetMethodId() const { return method_id_; }
  const CodeItem* GetCodeItem() const { return code_.get(); }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  uint32_t access_flags_;
  const MethodId* method_id_;
  std::unique_ptr<const CodeItem> code_;
  DISALLOW_COPY_AND_ASSIGN(MethodItem);
};

class ArrayItem : public Item {
 public:
  class NameValuePair {
   public:
    NameValuePair(StringId* name, ArrayItem* value) :
      name_(name), value_(value) { }

    StringId* Name() const { return name_; }
    ArrayItem* Value() const { return value_.get(); }

   private:
    StringId* name_;
    std::unique_ptr<ArrayItem> value_;
    DISALLOW_COPY_AND_ASSIGN(NameValuePair);
  };

  ArrayItem(Header& header, const uint8_t** data, uint8_t type, uint8_t length);
  ArrayItem(Header& header, const uint8_t** data);
  ~ArrayItem() OVERRIDE { }

  int8_t Type() const { return type_; }
  bool GetBoolean() const { return item_.bool_val_; }
  int8_t GetByte() const { return item_.byte_val_; }
  int16_t GetShort() const { return item_.short_val_; }
  uint16_t GetChar() const { return item_.char_val_; }
  int32_t GetInt() const { return item_.int_val_; }
  int64_t GetLong() const { return item_.long_val_; }
  float GetFloat() const { return item_.float_val_; }
  double GetDouble() const { return item_.double_val_; }
  StringId* GetStringId() const { return item_.string_val_; }
  FieldId* GetFieldId() const { return item_.field_val_; }
  MethodId* GetMethodId() const { return item_.method_val_; }
  std::vector<std::unique_ptr<ArrayItem>>* GetAnnotationArray() const {
    return item_.annotation_array_val_;
  }
  StringId* GetAnnotationAnnotationString() const {
    return item_.annotation_annotation_val_.string_;
  }
  std::vector<std::unique_ptr<NameValuePair>>* GetAnnotationAnnotationNameValuePairArray() const {
    return item_.annotation_annotation_val_.array_;
  }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  void Read(Header& header, const uint8_t** data, uint8_t type, uint8_t length);
  uint8_t type_;
  union {
    bool bool_val_;
    int8_t byte_val_;
    int16_t short_val_;
    uint16_t char_val_;
    int32_t int_val_;
    int64_t long_val_;
    float float_val_;
    double double_val_;
    StringId* string_val_;
    FieldId* field_val_;
    MethodId* method_val_;
    std::vector<std::unique_ptr<ArrayItem>>* annotation_array_val_;
    struct {
      StringId* string_;
      std::vector<std::unique_ptr<NameValuePair>>* array_;
    } annotation_annotation_val_;
  } item_;
  DISALLOW_COPY_AND_ASSIGN(ArrayItem);
};

class ClassData : public Item {
 public:
  ClassData() = default;
  ~ClassData() OVERRIDE = default;
  std::vector<std::unique_ptr<FieldItem>>& StaticFields() { return static_fields_; }
  std::vector<std::unique_ptr<FieldItem>>& InstanceFields() { return instance_fields_; }
  std::vector<std::unique_ptr<MethodItem>>& DirectMethods() { return direct_methods_; }
  std::vector<std::unique_ptr<MethodItem>>& VirtualMethods() { return virtual_methods_; }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  std::vector<std::unique_ptr<FieldItem>> static_fields_;
  std::vector<std::unique_ptr<FieldItem>> instance_fields_;
  std::vector<std::unique_ptr<MethodItem>> direct_methods_;
  std::vector<std::unique_ptr<MethodItem>> virtual_methods_;
  DISALLOW_COPY_AND_ASSIGN(ClassData);
};

class ClassDef : public Item {
 public:
  ClassDef(const DexFile::ClassDef& disk_class_def, Header& header);
  ~ClassDef() OVERRIDE { }

  const TypeId* ClassType() const { return class_type_; }
  uint32_t GetAccessFlags() const { return access_flags_; }
  const TypeId* Superclass() const { return superclass_; }
  std::vector<TypeId*>* Interfaces() { return &interfaces_; }
  uint32_t InterfacesOffset() const { return interfaces_offset_; }
  void SetInterfacesOffset(uint32_t new_offset) { interfaces_offset_ = new_offset; }
  const StringId* SourceFile() const { return source_file_; }
  AnnotationsDirectoryItem* Annotations() const { return annotations_.get(); }
  std::vector<std::unique_ptr<ArrayItem>>* StaticValues() { return static_values_; }
  ClassData* GetClassData() { return &class_data_; }

  MethodItem* GenerateMethodItem(Header& header, ClassDataItemIterator& cdii);

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  const TypeId* class_type_;
  uint32_t access_flags_;
  const TypeId* superclass_;
  std::vector<TypeId*> interfaces_;
  uint32_t interfaces_offset_;
  const StringId* source_file_;
  std::unique_ptr<AnnotationsDirectoryItem> annotations_;
  std::vector<std::unique_ptr<ArrayItem>>* static_values_;
  ClassData class_data_;
  DISALLOW_COPY_AND_ASSIGN(ClassDef);
};

class CodeItem : public Item {
 public:
  CodeItem(const DexFile::CodeItem& disk_code_item, Header& header);
  ~CodeItem() OVERRIDE { }

  uint16_t RegistersSize() const { return registers_size_; }
  uint16_t InsSize() const { return ins_size_; }
  uint16_t OutsSize() const { return outs_size_; }
  uint16_t TriesSize() const { return tries_size_; }
  DebugInfoItem* DebugInfo() const { return debug_info_.get(); }
  uint32_t InsnsSize() const { return insns_size_; }
  uint16_t* Insns() const { return insns_.get(); }
  std::vector<std::unique_ptr<const TryItem>>* Tries() const { return tries_; }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  uint16_t registers_size_;
  uint16_t ins_size_;
  uint16_t outs_size_;
  uint16_t tries_size_;
  std::unique_ptr<DebugInfoItem> debug_info_;
  uint32_t insns_size_;
  std::unique_ptr<uint16_t[]> insns_;
  std::vector<std::unique_ptr<const TryItem>>* tries_;
  DISALLOW_COPY_AND_ASSIGN(CodeItem);
};

class TryItem : public Item {
 public:
  class CatchHandler {
   public:
    CatchHandler(const TypeId* type_id, uint32_t address) : type_id_(type_id), address_(address) { }

    const TypeId* GetTypeId() const { return type_id_; }
    uint32_t GetAddress() const { return address_; }

   private:
    const TypeId* type_id_;
    uint32_t address_;
    DISALLOW_COPY_AND_ASSIGN(CatchHandler);
  };

  TryItem(const DexFile::TryItem& disk_try_item,
          const DexFile::CodeItem& disk_code_item,
          Header& header) {
    start_addr_ = disk_try_item.start_addr_;
    insn_count_ = disk_try_item.insn_count_;
    for (CatchHandlerIterator it(disk_code_item, disk_try_item); it.HasNext(); it.Next()) {
      const uint16_t type_index = it.GetHandlerTypeIndex();
      const TypeId* type_id = header.GetTypeIdOrNullPtr(type_index);
      handlers_.push_back(std::unique_ptr<const CatchHandler>(
          new CatchHandler(type_id, it.GetHandlerAddress())));
    }
  }
  ~TryItem() OVERRIDE { }

  uint32_t StartAddr() const { return start_addr_; }
  uint16_t InsnCount() const { return insn_count_; }
  const std::vector<std::unique_ptr<const CatchHandler>>& GetHandlers() const { return handlers_; }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  uint32_t start_addr_;
  uint16_t insn_count_;
  std::vector<std::unique_ptr<const CatchHandler>> handlers_;
  DISALLOW_COPY_AND_ASSIGN(TryItem);
};


struct PositionInfo {
  PositionInfo(uint32_t address, uint32_t line) : address_(address), line_(line) { }

  uint32_t address_;
  uint32_t line_;
};

struct LocalInfo {
  LocalInfo(const char* name, const char* descriptor, const char* signature, uint32_t start_address,
            uint32_t end_address, uint16_t reg) :
    name_(name), descriptor_(descriptor), signature_(signature), start_address_(start_address),
    end_address_(end_address), reg_(reg) { }

  std::string name_;
  std::string descriptor_;
  std::string signature_;
  uint32_t start_address_;
  uint32_t end_address_;
  uint16_t reg_;
};

class DebugInfoItem : public Item {
 public:
  DebugInfoItem() = default;

  std::vector<std::unique_ptr<PositionInfo>>& GetPositionInfo() { return positions_; }
  std::vector<std::unique_ptr<LocalInfo>>& GetLocalInfo() { return locals_; }

 private:
  std::vector<std::unique_ptr<PositionInfo>> positions_;
  std::vector<std::unique_ptr<LocalInfo>> locals_;
  DISALLOW_COPY_AND_ASSIGN(DebugInfoItem);
};

class AnnotationSetItem : public Item {
 public:
  class AnnotationItem {
   public:
    AnnotationItem(uint8_t visibility, ArrayItem* item) :
      visibility_(visibility), item_(item) { }

    uint8_t GetVisibility() const { return visibility_; }
    ArrayItem* GetItem() const { return item_.get(); }

   private:
    uint8_t visibility_;
    std::unique_ptr<ArrayItem> item_;
    DISALLOW_COPY_AND_ASSIGN(AnnotationItem);
  };

  AnnotationSetItem(const DexFile::AnnotationSetItem& disk_annotations_item, Header& header);
  ~AnnotationSetItem() OVERRIDE { }

  std::vector<std::unique_ptr<AnnotationItem>>& GetItems() { return items_; }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  std::vector<std::unique_ptr<AnnotationItem>> items_;
  DISALLOW_COPY_AND_ASSIGN(AnnotationSetItem);
};

class AnnotationsDirectoryItem : public Item {
 public:
  class FieldAnnotation {
   public:
    FieldAnnotation(FieldId* field_id, AnnotationSetItem* annotation_set_item) :
      field_id_(field_id), annotation_set_item_(annotation_set_item) { }

    FieldId* GetFieldId() const { return field_id_; }
    AnnotationSetItem* GetAnnotationSetItem() const { return annotation_set_item_.get(); }

   private:
    FieldId* field_id_;
    std::unique_ptr<AnnotationSetItem> annotation_set_item_;
    DISALLOW_COPY_AND_ASSIGN(FieldAnnotation);
  };

  class MethodAnnotation {
   public:
    MethodAnnotation(MethodId* method_id, AnnotationSetItem* annotation_set_item) :
      method_id_(method_id), annotation_set_item_(annotation_set_item) { }

    MethodId* GetMethodId() const { return method_id_; }
    AnnotationSetItem* GetAnnotationSetItem() const { return annotation_set_item_.get(); }

   private:
    MethodId* method_id_;
    std::unique_ptr<AnnotationSetItem> annotation_set_item_;
    DISALLOW_COPY_AND_ASSIGN(MethodAnnotation);
  };

  class ParameterAnnotation {
   public:
    ParameterAnnotation(MethodId* method_id,
                        const DexFile::AnnotationSetRefList* annotation_set_ref_list,
                        Header& header) :
      method_id_(method_id) {
      for (uint32_t i = 0; i < annotation_set_ref_list->size_; ++i) {
        const DexFile::AnnotationSetItem* annotation_set_item =
            header.GetDexFile().GetSetRefItemItem(&annotation_set_ref_list->list_[i]);
        annotations_.push_back(std::unique_ptr<AnnotationSetItem>(
            new AnnotationSetItem(*annotation_set_item, header)));
      }
    }

    MethodId* GetMethodId() const { return method_id_; }
    std::vector<std::unique_ptr<AnnotationSetItem>>& GetAnnotations() { return annotations_; }

   private:
    MethodId* method_id_;
    std::vector<std::unique_ptr<AnnotationSetItem>> annotations_;
    DISALLOW_COPY_AND_ASSIGN(ParameterAnnotation);
  };

  AnnotationsDirectoryItem(const DexFile::AnnotationsDirectoryItem* disk_annotations_item,
                           Header& header);

  AnnotationSetItem* GetClassAnnotation() const { return class_annotation_.get(); }

  std::vector<std::unique_ptr<FieldAnnotation>>& GetFieldAnnotations() {
    return field_annotations_;
  }

  std::vector<std::unique_ptr<MethodAnnotation>>& GetMethodAnnotations() {
    return method_annotations_;
  }

  std::vector<std::unique_ptr<ParameterAnnotation>>& GetParameterAnnotations() {
    return parameter_annotations_;
  }

  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  std::unique_ptr<AnnotationSetItem> class_annotation_;
  std::vector<std::unique_ptr<FieldAnnotation>> field_annotations_;
  std::vector<std::unique_ptr<MethodAnnotation>> method_annotations_;
  std::vector<std::unique_ptr<ParameterAnnotation>> parameter_annotations_;
  DISALLOW_COPY_AND_ASSIGN(AnnotationsDirectoryItem);
};

// TODO(sehr): implement MapList.
class MapList : public Item {
 public:
  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  DISALLOW_COPY_AND_ASSIGN(MapList);
};

class MapItem : public Item {
 public:
  void Accept(AbstractDispatcher* dispatch) { dispatch->Dispatch(this); }

 private:
  DISALLOW_COPY_AND_ASSIGN(MapItem);
};

}  // namespace dex_ir
}  // namespace art

#endif  // ART_DEXLAYOUT_DEX_IR_H_
