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
 */

#include "dex_file_annotations.h"

#include <stdlib.h>

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "dex_file-inl.h"
#include "jvalue-inl.h"
#include "mirror/field.h"
#include "mirror/method.h"
#include "reflection.h"
#include "thread.h"

namespace art {

struct DexFile::AnnotationValue {
  JValue value_;
  uint8_t type_;
};

namespace {
mirror::Object* CreateAnnotationMember(Handle<mirror::Class> klass,
                                       Handle<mirror::Class> annotation_class,
                                       const uint8_t** annotation)
    REQUIRES_SHARED(Locks::mutator_lock_);

bool IsVisibilityCompatible(uint32_t actual, uint32_t expected) {
  if (expected == DexFile::kDexVisibilityRuntime) {
    int32_t sdk_version = Runtime::Current()->GetTargetSdkVersion();
    if (sdk_version > 0 && sdk_version <= 23) {
      return actual == DexFile::kDexVisibilityRuntime || actual == DexFile::kDexVisibilityBuild;
    }
  }
  return actual == expected;
}

const DexFile::AnnotationSetItem* FindAnnotationSetForField(ArtField* field)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile* dex_file = field->GetDexFile();
  ObjPtr<mirror::Class> klass = field->GetDeclaringClass();
  const DexFile::AnnotationsDirectoryItem* annotations_dir =
      dex_file->GetAnnotationsDirectory(*klass->GetClassDef());
  if (annotations_dir == nullptr) {
    return nullptr;
  }
  const DexFile::FieldAnnotationsItem* field_annotations =
      dex_file->GetFieldAnnotations(annotations_dir);
  if (field_annotations == nullptr) {
    return nullptr;
  }
  uint32_t field_index = field->GetDexFieldIndex();
  uint32_t field_count = annotations_dir->fields_size_;
  for (uint32_t i = 0; i < field_count; ++i) {
    if (field_annotations[i].field_idx_ == field_index) {
      return dex_file->GetFieldAnnotationSetItem(field_annotations[i]);
    }
  }
  return nullptr;
}

const DexFile::AnnotationItem* SearchAnnotationSet(const DexFile& dex_file,
                                                   const DexFile::AnnotationSetItem* annotation_set,
                                                   const char* descriptor,
                                                   uint32_t visibility)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile::AnnotationItem* result = nullptr;
  for (uint32_t i = 0; i < annotation_set->size_; ++i) {
    const DexFile::AnnotationItem* annotation_item = dex_file.GetAnnotationItem(annotation_set, i);
    if (!IsVisibilityCompatible(annotation_item->visibility_, visibility)) {
      continue;
    }
    const uint8_t* annotation = annotation_item->annotation_;
    uint32_t type_index = DecodeUnsignedLeb128(&annotation);

    if (strcmp(descriptor, dex_file.StringByTypeIdx(type_index)) == 0) {
      result = annotation_item;
      break;
    }
  }
  return result;
}

bool SkipAnnotationValue(const DexFile& dex_file, const uint8_t** annotation_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const uint8_t* annotation = *annotation_ptr;
  uint8_t header_byte = *(annotation++);
  uint8_t value_type = header_byte & DexFile::kDexAnnotationValueTypeMask;
  uint8_t value_arg = header_byte >> DexFile::kDexAnnotationValueArgShift;
  int32_t width = value_arg + 1;

  switch (value_type) {
    case DexFile::kDexAnnotationByte:
    case DexFile::kDexAnnotationShort:
    case DexFile::kDexAnnotationChar:
    case DexFile::kDexAnnotationInt:
    case DexFile::kDexAnnotationLong:
    case DexFile::kDexAnnotationFloat:
    case DexFile::kDexAnnotationDouble:
    case DexFile::kDexAnnotationString:
    case DexFile::kDexAnnotationType:
    case DexFile::kDexAnnotationMethod:
    case DexFile::kDexAnnotationField:
    case DexFile::kDexAnnotationEnum:
      break;
    case DexFile::kDexAnnotationArray:
    {
      uint32_t size = DecodeUnsignedLeb128(&annotation);
      while (size--) {
        if (!SkipAnnotationValue(dex_file, &annotation)) {
          return false;
        }
      }
      width = 0;
      break;
    }
    case DexFile::kDexAnnotationAnnotation:
    {
      DecodeUnsignedLeb128(&annotation);  // unused type_index
      uint32_t size = DecodeUnsignedLeb128(&annotation);
      while (size--) {
        DecodeUnsignedLeb128(&annotation);  // unused element_name_index
        if (!SkipAnnotationValue(dex_file, &annotation)) {
          return false;
        }
      }
      width = 0;
      break;
    }
    case DexFile::kDexAnnotationBoolean:
    case DexFile::kDexAnnotationNull:
      width = 0;
      break;
    default:
      LOG(FATAL) << StringPrintf("Bad annotation element value byte 0x%02x", value_type);
      return false;
  }

  annotation += width;
  *annotation_ptr = annotation;
  return true;
}

const uint8_t* SearchEncodedAnnotation(const DexFile& dex_file,
                                       const uint8_t* annotation,
                                       const char* name)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DecodeUnsignedLeb128(&annotation);  // unused type_index
  uint32_t size = DecodeUnsignedLeb128(&annotation);

  while (size != 0) {
    uint32_t element_name_index = DecodeUnsignedLeb128(&annotation);
    const char* element_name = dex_file.GetStringData(dex_file.GetStringId(element_name_index));
    if (strcmp(name, element_name) == 0) {
      return annotation;
    }
    SkipAnnotationValue(dex_file, &annotation);
    size--;
  }
  return nullptr;
}

const DexFile::AnnotationSetItem* FindAnnotationSetForMethod(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile* dex_file = method->GetDexFile();
  mirror::Class* klass = method->GetDeclaringClass();
  const DexFile::AnnotationsDirectoryItem* annotations_dir =
      dex_file->GetAnnotationsDirectory(*klass->GetClassDef());
  if (annotations_dir == nullptr) {
    return nullptr;
  }
  const DexFile::MethodAnnotationsItem* method_annotations =
      dex_file->GetMethodAnnotations(annotations_dir);
  if (method_annotations == nullptr) {
    return nullptr;
  }
  uint32_t method_index = method->GetDexMethodIndex();
  uint32_t method_count = annotations_dir->methods_size_;
  for (uint32_t i = 0; i < method_count; ++i) {
    if (method_annotations[i].method_idx_ == method_index) {
      return dex_file->GetMethodAnnotationSetItem(method_annotations[i]);
    }
  }
  return nullptr;
}

const DexFile::ParameterAnnotationsItem* FindAnnotationsItemForMethod(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile* dex_file = method->GetDexFile();
  mirror::Class* klass = method->GetDeclaringClass();
  const DexFile::AnnotationsDirectoryItem* annotations_dir =
      dex_file->GetAnnotationsDirectory(*klass->GetClassDef());
  if (annotations_dir == nullptr) {
    return nullptr;
  }
  const DexFile::ParameterAnnotationsItem* parameter_annotations =
      dex_file->GetParameterAnnotations(annotations_dir);
  if (parameter_annotations == nullptr) {
    return nullptr;
  }
  uint32_t method_index = method->GetDexMethodIndex();
  uint32_t parameter_count = annotations_dir->parameters_size_;
  for (uint32_t i = 0; i < parameter_count; ++i) {
    if (parameter_annotations[i].method_idx_ == method_index) {
      return &parameter_annotations[i];
    }
  }
  return nullptr;
}

const DexFile::AnnotationSetItem* FindAnnotationSetForClass(Handle<mirror::Class> klass)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationsDirectoryItem* annotations_dir =
      dex_file.GetAnnotationsDirectory(*klass->GetClassDef());
  if (annotations_dir == nullptr) {
    return nullptr;
  }
  return dex_file.GetClassAnnotationSet(annotations_dir);
}

mirror::Object* ProcessEncodedAnnotation(Handle<mirror::Class> klass, const uint8_t** annotation)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  uint32_t type_index = DecodeUnsignedLeb128(annotation);
  uint32_t size = DecodeUnsignedLeb128(annotation);

  Thread* self = Thread::Current();
  ScopedObjectAccessUnchecked soa(self);
  StackHandleScope<2> hs(self);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  Handle<mirror::Class> annotation_class(hs.NewHandle(
      class_linker->ResolveType(klass->GetDexFile(), type_index, klass.Get())));
  if (annotation_class.Get() == nullptr) {
    LOG(INFO) << "Unable to resolve " << klass->PrettyClass() << " annotation class " << type_index;
    DCHECK(Thread::Current()->IsExceptionPending());
    Thread::Current()->ClearException();
    return nullptr;
  }

  ObjPtr<mirror::Class> annotation_member_class =
      soa.Decode<mirror::Class>(WellKnownClasses::libcore_reflect_AnnotationMember).Ptr();
  mirror::Class* annotation_member_array_class =
      class_linker->FindArrayClass(self, &annotation_member_class);
  if (annotation_member_array_class == nullptr) {
    return nullptr;
  }
  mirror::ObjectArray<mirror::Object>* element_array = nullptr;
  if (size > 0) {
    element_array =
        mirror::ObjectArray<mirror::Object>::Alloc(self, annotation_member_array_class, size);
    if (element_array == nullptr) {
      LOG(ERROR) << "Failed to allocate annotation member array (" << size << " elements)";
      return nullptr;
    }
  }

  Handle<mirror::ObjectArray<mirror::Object>> h_element_array(hs.NewHandle(element_array));
  for (uint32_t i = 0; i < size; ++i) {
    mirror::Object* new_member = CreateAnnotationMember(klass, annotation_class, annotation);
    if (new_member == nullptr) {
      return nullptr;
    }
    h_element_array->SetWithoutChecks<false>(i, new_member);
  }

  JValue result;
  ArtMethod* create_annotation_method =
      soa.DecodeMethod(WellKnownClasses::libcore_reflect_AnnotationFactory_createAnnotation);
  uint32_t args[2] = { static_cast<uint32_t>(reinterpret_cast<uintptr_t>(annotation_class.Get())),
                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h_element_array.Get())) };
  create_annotation_method->Invoke(self, args, sizeof(args), &result, "LLL");
  if (self->IsExceptionPending()) {
    LOG(INFO) << "Exception in AnnotationFactory.createAnnotation";
    return nullptr;
  }

  return result.GetL();
}

bool ProcessAnnotationValue(Handle<mirror::Class> klass,
                            const uint8_t** annotation_ptr,
                            DexFile::AnnotationValue* annotation_value,
                            Handle<mirror::Class> array_class,
                            DexFile::AnnotationResultStyle result_style)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  Thread* self = Thread::Current();
  ObjPtr<mirror::Object> element_object = nullptr;
  bool set_object = false;
  Primitive::Type primitive_type = Primitive::kPrimVoid;
  const uint8_t* annotation = *annotation_ptr;
  uint8_t header_byte = *(annotation++);
  uint8_t value_type = header_byte & DexFile::kDexAnnotationValueTypeMask;
  uint8_t value_arg = header_byte >> DexFile::kDexAnnotationValueArgShift;
  int32_t width = value_arg + 1;
  annotation_value->type_ = value_type;

  switch (value_type) {
    case DexFile::kDexAnnotationByte:
      annotation_value->value_.SetB(
          static_cast<int8_t>(DexFile::ReadSignedInt(annotation, value_arg)));
      primitive_type = Primitive::kPrimByte;
      break;
    case DexFile::kDexAnnotationShort:
      annotation_value->value_.SetS(
          static_cast<int16_t>(DexFile::ReadSignedInt(annotation, value_arg)));
      primitive_type = Primitive::kPrimShort;
      break;
    case DexFile::kDexAnnotationChar:
      annotation_value->value_.SetC(
          static_cast<uint16_t>(DexFile::ReadUnsignedInt(annotation, value_arg, false)));
      primitive_type = Primitive::kPrimChar;
      break;
    case DexFile::kDexAnnotationInt:
      annotation_value->value_.SetI(DexFile::ReadSignedInt(annotation, value_arg));
      primitive_type = Primitive::kPrimInt;
      break;
    case DexFile::kDexAnnotationLong:
      annotation_value->value_.SetJ(DexFile::ReadSignedLong(annotation, value_arg));
      primitive_type = Primitive::kPrimLong;
      break;
    case DexFile::kDexAnnotationFloat:
      annotation_value->value_.SetI(DexFile::ReadUnsignedInt(annotation, value_arg, true));
      primitive_type = Primitive::kPrimFloat;
      break;
    case DexFile::kDexAnnotationDouble:
      annotation_value->value_.SetJ(DexFile::ReadUnsignedLong(annotation, value_arg, true));
      primitive_type = Primitive::kPrimDouble;
      break;
    case DexFile::kDexAnnotationBoolean:
      annotation_value->value_.SetZ(value_arg != 0);
      primitive_type = Primitive::kPrimBoolean;
      width = 0;
      break;
    case DexFile::kDexAnnotationString: {
      uint32_t index = DexFile::ReadUnsignedInt(annotation, value_arg, false);
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(index);
      } else {
        StackHandleScope<1> hs(self);
        Handle<mirror::DexCache> dex_cache(hs.NewHandle(klass->GetDexCache()));
        element_object = Runtime::Current()->GetClassLinker()->ResolveString(
            klass->GetDexFile(), index, dex_cache);
        set_object = true;
        if (element_object == nullptr) {
          return false;
        }
      }
      break;
    }
    case DexFile::kDexAnnotationType: {
      uint32_t index = DexFile::ReadUnsignedInt(annotation, value_arg, false);
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(index);
      } else {
        element_object = Runtime::Current()->GetClassLinker()->ResolveType(
            klass->GetDexFile(), index, klass.Get());
        set_object = true;
        if (element_object == nullptr) {
          CHECK(self->IsExceptionPending());
          if (result_style == DexFile::kAllObjects) {
            const char* msg = dex_file.StringByTypeIdx(index);
            self->ThrowNewWrappedException("Ljava/lang/TypeNotPresentException;", msg);
            element_object = self->GetException();
            self->ClearException();
          } else {
            return false;
          }
        }
      }
      break;
    }
    case DexFile::kDexAnnotationMethod: {
      uint32_t index = DexFile::ReadUnsignedInt(annotation, value_arg, false);
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(index);
      } else {
        StackHandleScope<2> hs(self);
        Handle<mirror::DexCache> dex_cache(hs.NewHandle(klass->GetDexCache()));
        Handle<mirror::ClassLoader> class_loader(hs.NewHandle(klass->GetClassLoader()));
        ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
        ArtMethod* method = class_linker->ResolveMethodWithoutInvokeType(
            klass->GetDexFile(), index, dex_cache, class_loader);
        if (method == nullptr) {
          return false;
        }
        PointerSize pointer_size = class_linker->GetImagePointerSize();
        set_object = true;
        DCHECK(!Runtime::Current()->IsActiveTransaction());
        if (method->IsConstructor()) {
          if (pointer_size == PointerSize::k64) {
            element_object = mirror::Constructor::CreateFromArtMethod<PointerSize::k64,
                                                                      false>(self, method);
          } else {
            element_object = mirror::Constructor::CreateFromArtMethod<PointerSize::k32,
                                                                      false>(self, method);
          }
        } else {
          if (pointer_size == PointerSize::k64) {
            element_object = mirror::Method::CreateFromArtMethod<PointerSize::k64,
                                                                 false>(self, method);
          } else {
            element_object = mirror::Method::CreateFromArtMethod<PointerSize::k32,
                                                                 false>(self, method);
          }
        }
        if (element_object == nullptr) {
          return false;
        }
      }
      break;
    }
    case DexFile::kDexAnnotationField: {
      uint32_t index = DexFile::ReadUnsignedInt(annotation, value_arg, false);
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(index);
      } else {
        StackHandleScope<2> hs(self);
        Handle<mirror::DexCache> dex_cache(hs.NewHandle(klass->GetDexCache()));
        Handle<mirror::ClassLoader> class_loader(hs.NewHandle(klass->GetClassLoader()));
        ArtField* field = Runtime::Current()->GetClassLinker()->ResolveFieldJLS(
            klass->GetDexFile(), index, dex_cache, class_loader);
        if (field == nullptr) {
          return false;
        }
        set_object = true;
        PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
        if (pointer_size == PointerSize::k64) {
          element_object = mirror::Field::CreateFromArtField<PointerSize::k64>(self, field, true);
        } else {
          element_object = mirror::Field::CreateFromArtField<PointerSize::k32>(self, field, true);
        }
        if (element_object == nullptr) {
          return false;
        }
      }
      break;
    }
    case DexFile::kDexAnnotationEnum: {
      uint32_t index = DexFile::ReadUnsignedInt(annotation, value_arg, false);
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(index);
      } else {
        StackHandleScope<3> hs(self);
        Handle<mirror::DexCache> dex_cache(hs.NewHandle(klass->GetDexCache()));
        Handle<mirror::ClassLoader> class_loader(hs.NewHandle(klass->GetClassLoader()));
        ArtField* enum_field = Runtime::Current()->GetClassLinker()->ResolveField(
            klass->GetDexFile(), index, dex_cache, class_loader, true);
        if (enum_field == nullptr) {
          return false;
        } else {
          Handle<mirror::Class> field_class(hs.NewHandle(enum_field->GetDeclaringClass()));
          Runtime::Current()->GetClassLinker()->EnsureInitialized(self, field_class, true, true);
          element_object = enum_field->GetObject(field_class.Get());
          set_object = true;
        }
      }
      break;
    }
    case DexFile::kDexAnnotationArray:
      if (result_style == DexFile::kAllRaw || array_class.Get() == nullptr) {
        return false;
      } else {
        ScopedObjectAccessUnchecked soa(self);
        StackHandleScope<2> hs(self);
        uint32_t size = DecodeUnsignedLeb128(&annotation);
        Handle<mirror::Class> component_type(hs.NewHandle(array_class->GetComponentType()));
        Handle<mirror::Array> new_array(hs.NewHandle(mirror::Array::Alloc<true>(
            self, array_class.Get(), size, array_class->GetComponentSizeShift(),
            Runtime::Current()->GetHeap()->GetCurrentAllocator())));
        if (new_array.Get() == nullptr) {
          LOG(ERROR) << "Annotation element array allocation failed with size " << size;
          return false;
        }
        DexFile::AnnotationValue new_annotation_value;
        for (uint32_t i = 0; i < size; ++i) {
          if (!ProcessAnnotationValue(klass, &annotation, &new_annotation_value,
                                      component_type, DexFile::kPrimitivesOrObjects)) {
            return false;
          }
          if (!component_type->IsPrimitive()) {
            mirror::Object* obj = new_annotation_value.value_.GetL();
            new_array->AsObjectArray<mirror::Object>()->SetWithoutChecks<false>(i, obj);
          } else {
            switch (new_annotation_value.type_) {
              case DexFile::kDexAnnotationByte:
                new_array->AsByteArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetB());
                break;
              case DexFile::kDexAnnotationShort:
                new_array->AsShortArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetS());
                break;
              case DexFile::kDexAnnotationChar:
                new_array->AsCharArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetC());
                break;
              case DexFile::kDexAnnotationInt:
                new_array->AsIntArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetI());
                break;
              case DexFile::kDexAnnotationLong:
                new_array->AsLongArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetJ());
                break;
              case DexFile::kDexAnnotationFloat:
                new_array->AsFloatArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetF());
                break;
              case DexFile::kDexAnnotationDouble:
                new_array->AsDoubleArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetD());
                break;
              case DexFile::kDexAnnotationBoolean:
                new_array->AsBooleanArray()->SetWithoutChecks<false>(
                    i, new_annotation_value.value_.GetZ());
                break;
              default:
                LOG(FATAL) << "Found invalid annotation value type while building annotation array";
                return false;
            }
          }
        }
        element_object = new_array.Get();
        set_object = true;
        width = 0;
      }
      break;
    case DexFile::kDexAnnotationAnnotation:
      if (result_style == DexFile::kAllRaw) {
        return false;
      }
      element_object = ProcessEncodedAnnotation(klass, &annotation);
      if (element_object == nullptr) {
        return false;
      }
      set_object = true;
      width = 0;
      break;
    case DexFile::kDexAnnotationNull:
      if (result_style == DexFile::kAllRaw) {
        annotation_value->value_.SetI(0);
      } else {
        CHECK(element_object == nullptr);
        set_object = true;
      }
      width = 0;
      break;
    default:
      LOG(ERROR) << StringPrintf("Bad annotation element value type 0x%02x", value_type);
      return false;
  }

  annotation += width;
  *annotation_ptr = annotation;

  if (result_style == DexFile::kAllObjects && primitive_type != Primitive::kPrimVoid) {
    element_object = BoxPrimitive(primitive_type, annotation_value->value_).Ptr();
    set_object = true;
  }

  if (set_object) {
    annotation_value->value_.SetL(element_object.Ptr());
  }

  return true;
}

mirror::Object* CreateAnnotationMember(Handle<mirror::Class> klass,
                                       Handle<mirror::Class> annotation_class,
                                       const uint8_t** annotation) {
  const DexFile& dex_file = klass->GetDexFile();
  Thread* self = Thread::Current();
  ScopedObjectAccessUnchecked soa(self);
  StackHandleScope<5> hs(self);
  uint32_t element_name_index = DecodeUnsignedLeb128(annotation);
  const char* name = dex_file.StringDataByIdx(element_name_index);
  Handle<mirror::String> string_name(
      hs.NewHandle(mirror::String::AllocFromModifiedUtf8(self, name)));

  PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
  ArtMethod* annotation_method =
      annotation_class->FindDeclaredVirtualMethodByName(name, pointer_size);
  if (annotation_method == nullptr) {
    return nullptr;
  }
  Handle<mirror::Class> method_return(hs.NewHandle(
      annotation_method->GetReturnType(true /* resolve */, pointer_size)));

  DexFile::AnnotationValue annotation_value;
  if (!ProcessAnnotationValue(klass, annotation, &annotation_value, method_return,
                              DexFile::kAllObjects)) {
    return nullptr;
  }
  Handle<mirror::Object> value_object(hs.NewHandle(annotation_value.value_.GetL()));

  ObjPtr<mirror::Class> annotation_member_class =
      WellKnownClasses::ToClass(WellKnownClasses::libcore_reflect_AnnotationMember);
  Handle<mirror::Object> new_member(hs.NewHandle(annotation_member_class->AllocObject(self)));
  mirror::Method* method_obj_ptr;
  DCHECK(!Runtime::Current()->IsActiveTransaction());
  if (pointer_size == PointerSize::k64) {
    method_obj_ptr = mirror::Method::CreateFromArtMethod<PointerSize::k64, false>(
        self, annotation_method);
  } else {
    method_obj_ptr = mirror::Method::CreateFromArtMethod<PointerSize::k32, false>(
        self, annotation_method);
  }
  Handle<mirror::Method> method_object(hs.NewHandle(method_obj_ptr));

  if (new_member.Get() == nullptr || string_name.Get() == nullptr ||
      method_object.Get() == nullptr || method_return.Get() == nullptr) {
    LOG(ERROR) << StringPrintf("Failed creating annotation element (m=%p n=%p a=%p r=%p",
        new_member.Get(), string_name.Get(), method_object.Get(), method_return.Get());
    return nullptr;
  }

  JValue result;
  ArtMethod* annotation_member_init =
      soa.DecodeMethod(WellKnownClasses::libcore_reflect_AnnotationMember_init);
  uint32_t args[5] = { static_cast<uint32_t>(reinterpret_cast<uintptr_t>(new_member.Get())),
                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(string_name.Get())),
                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value_object.Get())),
                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(method_return.Get())),
                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(method_object.Get()))
  };
  annotation_member_init->Invoke(self, args, sizeof(args), &result, "VLLLL");
  if (self->IsExceptionPending()) {
    LOG(INFO) << "Exception in AnnotationMember.<init>";
    return nullptr;
  }

  return new_member.Get();
}

const DexFile::AnnotationItem* GetAnnotationItemFromAnnotationSet(
    Handle<mirror::Class> klass,
    const DexFile::AnnotationSetItem* annotation_set,
    uint32_t visibility,
    Handle<mirror::Class> annotation_class)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  for (uint32_t i = 0; i < annotation_set->size_; ++i) {
    const DexFile::AnnotationItem* annotation_item = dex_file.GetAnnotationItem(annotation_set, i);
    if (!IsVisibilityCompatible(annotation_item->visibility_, visibility)) {
      continue;
    }
    const uint8_t* annotation = annotation_item->annotation_;
    uint32_t type_index = DecodeUnsignedLeb128(&annotation);
    mirror::Class* resolved_class = Runtime::Current()->GetClassLinker()->ResolveType(
        klass->GetDexFile(), type_index, klass.Get());
    if (resolved_class == nullptr) {
      std::string temp;
      LOG(WARNING) << StringPrintf("Unable to resolve %s annotation class %d",
                                   klass->GetDescriptor(&temp), type_index);
      CHECK(Thread::Current()->IsExceptionPending());
      Thread::Current()->ClearException();
      continue;
    }
    if (resolved_class == annotation_class.Get()) {
      return annotation_item;
    }
  }

  return nullptr;
}

mirror::Object* GetAnnotationObjectFromAnnotationSet(
    Handle<mirror::Class> klass,
    const DexFile::AnnotationSetItem* annotation_set,
    uint32_t visibility,
    Handle<mirror::Class> annotation_class)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile::AnnotationItem* annotation_item =
      GetAnnotationItemFromAnnotationSet(klass, annotation_set, visibility, annotation_class);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  const uint8_t* annotation = annotation_item->annotation_;
  return ProcessEncodedAnnotation(klass, &annotation);
}

mirror::Object* GetAnnotationValue(Handle<mirror::Class> klass,
                                   const DexFile::AnnotationItem* annotation_item,
                                   const char* annotation_name,
                                   Handle<mirror::Class> array_class,
                                   uint32_t expected_type)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  const uint8_t* annotation =
      SearchEncodedAnnotation(dex_file, annotation_item->annotation_, annotation_name);
  if (annotation == nullptr) {
    return nullptr;
  }
  DexFile::AnnotationValue annotation_value;
  if (!ProcessAnnotationValue(klass, &annotation, &annotation_value, array_class,
                              DexFile::kAllObjects)) {
    return nullptr;
  }
  if (annotation_value.type_ != expected_type) {
    return nullptr;
  }
  return annotation_value.value_.GetL();
}

mirror::ObjectArray<mirror::String>* GetSignatureValue(Handle<mirror::Class> klass,
    const DexFile::AnnotationSetItem* annotation_set)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  StackHandleScope<1> hs(Thread::Current());
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/Signature;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  ObjPtr<mirror::Class> string_class = mirror::String::GetJavaLangString();
  Handle<mirror::Class> string_array_class(hs.NewHandle(
      Runtime::Current()->GetClassLinker()->FindArrayClass(Thread::Current(), &string_class)));
  if (string_array_class.Get() == nullptr) {
    return nullptr;
  }
  mirror::Object* obj =
      GetAnnotationValue(klass, annotation_item, "value", string_array_class,
                         DexFile::kDexAnnotationArray);
  if (obj == nullptr) {
    return nullptr;
  }
  return obj->AsObjectArray<mirror::String>();
}

mirror::ObjectArray<mirror::Class>* GetThrowsValue(Handle<mirror::Class> klass,
                                                   const DexFile::AnnotationSetItem* annotation_set)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  StackHandleScope<1> hs(Thread::Current());
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/Throws;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  ObjPtr<mirror::Class> class_class = mirror::Class::GetJavaLangClass();
  Handle<mirror::Class> class_array_class(hs.NewHandle(
      Runtime::Current()->GetClassLinker()->FindArrayClass(Thread::Current(), &class_class)));
  if (class_array_class.Get() == nullptr) {
    return nullptr;
  }
  mirror::Object* obj =
      GetAnnotationValue(klass, annotation_item, "value", class_array_class,
                         DexFile::kDexAnnotationArray);
  if (obj == nullptr) {
    return nullptr;
  }
  return obj->AsObjectArray<mirror::Class>();
}

mirror::ObjectArray<mirror::Object>* ProcessAnnotationSet(
    Handle<mirror::Class> klass,
    const DexFile::AnnotationSetItem* annotation_set,
    uint32_t visibility)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  Thread* self = Thread::Current();
  ScopedObjectAccessUnchecked soa(self);
  StackHandleScope<2> hs(self);
  Handle<mirror::Class> annotation_array_class(hs.NewHandle(
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_annotation_Annotation__array)));
  if (annotation_set == nullptr) {
    return mirror::ObjectArray<mirror::Object>::Alloc(self, annotation_array_class.Get(), 0);
  }

  uint32_t size = annotation_set->size_;
  Handle<mirror::ObjectArray<mirror::Object>> result(hs.NewHandle(
      mirror::ObjectArray<mirror::Object>::Alloc(self, annotation_array_class.Get(), size)));
  if (result.Get() == nullptr) {
    return nullptr;
  }

  uint32_t dest_index = 0;
  for (uint32_t i = 0; i < size; ++i) {
    const DexFile::AnnotationItem* annotation_item = dex_file.GetAnnotationItem(annotation_set, i);
    // Note that we do not use IsVisibilityCompatible here because older code
    // was correct for this case.
    if (annotation_item->visibility_ != visibility) {
      continue;
    }
    const uint8_t* annotation = annotation_item->annotation_;
    mirror::Object* annotation_obj = ProcessEncodedAnnotation(klass, &annotation);
    if (annotation_obj != nullptr) {
      result->SetWithoutChecks<false>(dest_index, annotation_obj);
      ++dest_index;
    } else if (self->IsExceptionPending()) {
      return nullptr;
    }
  }

  if (dest_index == size) {
    return result.Get();
  }

  mirror::ObjectArray<mirror::Object>* trimmed_result =
      mirror::ObjectArray<mirror::Object>::Alloc(self, annotation_array_class.Get(), dest_index);
  if (trimmed_result == nullptr) {
    return nullptr;
  }

  for (uint32_t i = 0; i < dest_index; ++i) {
    mirror::Object* obj = result->GetWithoutChecks(i);
    trimmed_result->SetWithoutChecks<false>(i, obj);
  }

  return trimmed_result;
}

mirror::ObjectArray<mirror::Object>* ProcessAnnotationSetRefList(
    Handle<mirror::Class> klass,
    const DexFile::AnnotationSetRefList* set_ref_list,
    uint32_t size)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const DexFile& dex_file = klass->GetDexFile();
  Thread* self = Thread::Current();
  ScopedObjectAccessUnchecked soa(self);
  StackHandleScope<1> hs(self);
  ObjPtr<mirror::Class> annotation_array_class =
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_annotation_Annotation__array);
  mirror::Class* annotation_array_array_class =
      Runtime::Current()->GetClassLinker()->FindArrayClass(self, &annotation_array_class);
  if (annotation_array_array_class == nullptr) {
    return nullptr;
  }
  Handle<mirror::ObjectArray<mirror::Object>> annotation_array_array(hs.NewHandle(
      mirror::ObjectArray<mirror::Object>::Alloc(self, annotation_array_array_class, size)));
  if (annotation_array_array.Get() == nullptr) {
    LOG(ERROR) << "Annotation set ref array allocation failed";
    return nullptr;
  }
  for (uint32_t index = 0; index < size; ++index) {
    const DexFile::AnnotationSetRefItem* set_ref_item = &set_ref_list->list_[index];
    const DexFile::AnnotationSetItem* set_item = dex_file.GetSetRefItemItem(set_ref_item);
    mirror::Object* annotation_set = ProcessAnnotationSet(klass, set_item,
                                                          DexFile::kDexVisibilityRuntime);
    if (annotation_set == nullptr) {
      return nullptr;
    }
    annotation_array_array->SetWithoutChecks<false>(index, annotation_set);
  }
  return annotation_array_array.Get();
}
}  // namespace

namespace annotations {

mirror::Object* GetAnnotationForField(ArtField* field, Handle<mirror::Class> annotation_class) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForField(field);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> field_class(hs.NewHandle(field->GetDeclaringClass()));
  return GetAnnotationObjectFromAnnotationSet(field_class, annotation_set,
                                              DexFile::kDexVisibilityRuntime, annotation_class);
}

mirror::ObjectArray<mirror::Object>* GetAnnotationsForField(ArtField* field) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForField(field);
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> field_class(hs.NewHandle(field->GetDeclaringClass()));
  return ProcessAnnotationSet(field_class, annotation_set, DexFile::kDexVisibilityRuntime);
}

mirror::ObjectArray<mirror::String>* GetSignatureAnnotationForField(ArtField* field) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForField(field);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> field_class(hs.NewHandle(field->GetDeclaringClass()));
  return GetSignatureValue(field_class, annotation_set);
}

bool IsFieldAnnotationPresent(ArtField* field, Handle<mirror::Class> annotation_class) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForField(field);
  if (annotation_set == nullptr) {
    return false;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> field_class(hs.NewHandle(field->GetDeclaringClass()));
  const DexFile::AnnotationItem* annotation_item = GetAnnotationItemFromAnnotationSet(
      field_class, annotation_set, DexFile::kDexVisibilityRuntime, annotation_class);
  return annotation_item != nullptr;
}

mirror::Object* GetAnnotationDefaultValue(ArtMethod* method) {
  const DexFile* dex_file = method->GetDexFile();
  mirror::Class* klass = method->GetDeclaringClass();
  const DexFile::AnnotationsDirectoryItem* annotations_dir =
      dex_file->GetAnnotationsDirectory(*klass->GetClassDef());
  if (annotations_dir == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationSetItem* annotation_set =
      dex_file->GetClassAnnotationSet(annotations_dir);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationItem* annotation_item = SearchAnnotationSet(*dex_file, annotation_set,
      "Ldalvik/annotation/AnnotationDefault;", DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  const uint8_t* annotation =
      SearchEncodedAnnotation(*dex_file, annotation_item->annotation_, "value");
  if (annotation == nullptr) {
    return nullptr;
  }
  uint8_t header_byte = *(annotation++);
  if ((header_byte & DexFile::kDexAnnotationValueTypeMask) != DexFile::kDexAnnotationAnnotation) {
    return nullptr;
  }
  annotation = SearchEncodedAnnotation(*dex_file, annotation, method->GetName());
  if (annotation == nullptr) {
    return nullptr;
  }
  DexFile::AnnotationValue annotation_value;
  StackHandleScope<2> hs(Thread::Current());
  Handle<mirror::Class> h_klass(hs.NewHandle(klass));
  PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
  Handle<mirror::Class> return_type(hs.NewHandle(
      method->GetReturnType(true /* resolve */, pointer_size)));
  if (!ProcessAnnotationValue(h_klass, &annotation, &annotation_value, return_type,
                              DexFile::kAllObjects)) {
    return nullptr;
  }
  return annotation_value.value_.GetL();
}

mirror::Object* GetAnnotationForMethod(ArtMethod* method, Handle<mirror::Class> annotation_class) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForMethod(method);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return GetAnnotationObjectFromAnnotationSet(method_class, annotation_set,
                                              DexFile::kDexVisibilityRuntime, annotation_class);
}

mirror::ObjectArray<mirror::Object>* GetAnnotationsForMethod(ArtMethod* method) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForMethod(method);
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return ProcessAnnotationSet(method_class, annotation_set, DexFile::kDexVisibilityRuntime);
}

mirror::ObjectArray<mirror::Class>* GetExceptionTypesForMethod(ArtMethod* method) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForMethod(method);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return GetThrowsValue(method_class, annotation_set);
}

mirror::ObjectArray<mirror::Object>* GetParameterAnnotations(ArtMethod* method) {
  const DexFile* dex_file = method->GetDexFile();
  const DexFile::ParameterAnnotationsItem* parameter_annotations =
      FindAnnotationsItemForMethod(method);
  if (parameter_annotations == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationSetRefList* set_ref_list =
      dex_file->GetParameterAnnotationSetRefList(parameter_annotations);
  if (set_ref_list == nullptr) {
    return nullptr;
  }
  uint32_t size = set_ref_list->size_;
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return ProcessAnnotationSetRefList(method_class, set_ref_list, size);
}

mirror::Object* GetAnnotationForMethodParameter(ArtMethod* method,
                                                uint32_t parameter_idx,
                                                Handle<mirror::Class> annotation_class) {
  const DexFile* dex_file = method->GetDexFile();
  const DexFile::ParameterAnnotationsItem* parameter_annotations =
      FindAnnotationsItemForMethod(method);
  if (parameter_annotations == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationSetRefList* set_ref_list =
      dex_file->GetParameterAnnotationSetRefList(parameter_annotations);
  if (set_ref_list == nullptr) {
    return nullptr;
  }
  if (parameter_idx >= set_ref_list->size_) {
    return nullptr;
  }
  const DexFile::AnnotationSetRefItem* annotation_set_ref = &set_ref_list->list_[parameter_idx];
  const DexFile::AnnotationSetItem* annotation_set =
     dex_file->GetSetRefItemItem(annotation_set_ref);

  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return GetAnnotationObjectFromAnnotationSet(method_class,
                                              annotation_set,
                                              DexFile::kDexVisibilityRuntime,
                                              annotation_class);
}

bool GetParametersMetadataForMethod(ArtMethod* method,
                                    MutableHandle<mirror::ObjectArray<mirror::String>>* names,
                                    MutableHandle<mirror::IntArray>* access_flags) {
  const DexFile::AnnotationSetItem::AnnotationSetItem* annotation_set =
      FindAnnotationSetForMethod(method);
  if (annotation_set == nullptr) {
    return false;
  }

  const DexFile* dex_file = method->GetDexFile();
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(*dex_file,
                          annotation_set,
                          "Ldalvik/annotation/MethodParameters;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return false;
  }

  StackHandleScope<5> hs(Thread::Current());

  // Extract the parameters' names String[].
  ObjPtr<mirror::Class> string_class = mirror::String::GetJavaLangString();
  Handle<mirror::Class> string_array_class(hs.NewHandle(
      Runtime::Current()->GetClassLinker()->FindArrayClass(Thread::Current(), &string_class)));
  if (UNLIKELY(string_array_class.Get() == nullptr)) {
    return false;
  }

  Handle<mirror::Class> klass = hs.NewHandle(method->GetDeclaringClass());
  Handle<mirror::Object> names_obj =
      hs.NewHandle(GetAnnotationValue(klass,
                                      annotation_item,
                                      "names",
                                      string_array_class,
                                      DexFile::kDexAnnotationArray));
  if (names_obj.Get() == nullptr) {
    return false;
  }

  // Extract the parameters' access flags int[].
  Handle<mirror::Class> int_array_class(hs.NewHandle(mirror::IntArray::GetArrayClass()));
  if (UNLIKELY(int_array_class.Get() == nullptr)) {
    return false;
  }
  Handle<mirror::Object> access_flags_obj =
      hs.NewHandle(GetAnnotationValue(klass,
                                      annotation_item,
                                      "accessFlags",
                                      int_array_class,
                                      DexFile::kDexAnnotationArray));
  if (access_flags_obj.Get() == nullptr) {
    return false;
  }

  names->Assign(names_obj.Get()->AsObjectArray<mirror::String>());
  access_flags->Assign(access_flags_obj.Get()->AsIntArray());
  return true;
}

mirror::ObjectArray<mirror::String>* GetSignatureAnnotationForMethod(ArtMethod* method) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForMethod(method);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  return GetSignatureValue(method_class, annotation_set);
}

bool IsMethodAnnotationPresent(ArtMethod* method, Handle<mirror::Class> annotation_class,
                               uint32_t visibility /* = DexFile::kDexVisibilityRuntime */) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForMethod(method);
  if (annotation_set == nullptr) {
    return false;
  }
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::Class> method_class(hs.NewHandle(method->GetDeclaringClass()));
  const DexFile::AnnotationItem* annotation_item =
      GetAnnotationItemFromAnnotationSet(method_class, annotation_set, visibility,
                                         annotation_class);
  return annotation_item != nullptr;
}

mirror::Object* GetAnnotationForClass(Handle<mirror::Class> klass,
                                      Handle<mirror::Class> annotation_class) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  return GetAnnotationObjectFromAnnotationSet(klass, annotation_set, DexFile::kDexVisibilityRuntime,
                                              annotation_class);
}

mirror::ObjectArray<mirror::Object>* GetAnnotationsForClass(Handle<mirror::Class> klass) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  return ProcessAnnotationSet(klass, annotation_set, DexFile::kDexVisibilityRuntime);
}

mirror::ObjectArray<mirror::Class>* GetDeclaredClasses(Handle<mirror::Class> klass) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/MemberClasses;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  StackHandleScope<1> hs(Thread::Current());
  ObjPtr<mirror::Class> class_class = mirror::Class::GetJavaLangClass();
  Handle<mirror::Class> class_array_class(hs.NewHandle(
      Runtime::Current()->GetClassLinker()->FindArrayClass(hs.Self(), &class_class)));
  if (class_array_class.Get() == nullptr) {
    return nullptr;
  }
  mirror::Object* obj =
      GetAnnotationValue(klass, annotation_item, "value", class_array_class,
                         DexFile::kDexAnnotationArray);
  if (obj == nullptr) {
    return nullptr;
  }
  return obj->AsObjectArray<mirror::Class>();
}

mirror::Class* GetDeclaringClass(Handle<mirror::Class> klass) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/EnclosingClass;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  mirror::Object* obj = GetAnnotationValue(klass, annotation_item, "value",
                                           ScopedNullHandle<mirror::Class>(),
                                           DexFile::kDexAnnotationType);
  if (obj == nullptr) {
    return nullptr;
  }
  return obj->AsClass();
}

mirror::Class* GetEnclosingClass(Handle<mirror::Class> klass) {
  const DexFile& dex_file = klass->GetDexFile();
  mirror::Class* declaring_class = GetDeclaringClass(klass);
  if (declaring_class != nullptr) {
    return declaring_class;
  }
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/EnclosingMethod;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  const uint8_t* annotation =
      SearchEncodedAnnotation(dex_file, annotation_item->annotation_, "value");
  if (annotation == nullptr) {
    return nullptr;
  }
  DexFile::AnnotationValue annotation_value;
  if (!ProcessAnnotationValue(klass, &annotation, &annotation_value,
                              ScopedNullHandle<mirror::Class>(), DexFile::kAllRaw)) {
    return nullptr;
  }
  if (annotation_value.type_ != DexFile::kDexAnnotationMethod) {
    return nullptr;
  }
  StackHandleScope<2> hs(Thread::Current());
  Handle<mirror::DexCache> dex_cache(hs.NewHandle(klass->GetDexCache()));
  Handle<mirror::ClassLoader> class_loader(hs.NewHandle(klass->GetClassLoader()));
  ArtMethod* method = Runtime::Current()->GetClassLinker()->ResolveMethodWithoutInvokeType(
      klass->GetDexFile(), annotation_value.value_.GetI(), dex_cache, class_loader);
  if (method == nullptr) {
    return nullptr;
  }
  return method->GetDeclaringClass();
}

mirror::Object* GetEnclosingMethod(Handle<mirror::Class> klass) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/EnclosingMethod;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return nullptr;
  }
  return GetAnnotationValue(klass, annotation_item, "value", ScopedNullHandle<mirror::Class>(),
      DexFile::kDexAnnotationMethod);
}

bool GetInnerClass(Handle<mirror::Class> klass, mirror::String** name) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return false;
  }
  const DexFile::AnnotationItem* annotation_item = SearchAnnotationSet(
      dex_file, annotation_set, "Ldalvik/annotation/InnerClass;", DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return false;
  }
  const uint8_t* annotation =
      SearchEncodedAnnotation(dex_file, annotation_item->annotation_, "name");
  if (annotation == nullptr) {
    return false;
  }
  DexFile::AnnotationValue annotation_value;
  if (!ProcessAnnotationValue(klass, &annotation, &annotation_value,
                              ScopedNullHandle<mirror::Class>(),
                                           DexFile::kAllObjects)) {
    return false;
  }
  if (annotation_value.type_ != DexFile::kDexAnnotationNull &&
      annotation_value.type_ != DexFile::kDexAnnotationString) {
    return false;
  }
  *name = down_cast<mirror::String*>(annotation_value.value_.GetL());
  return true;
}

bool GetInnerClassFlags(Handle<mirror::Class> klass, uint32_t* flags) {
  const DexFile& dex_file = klass->GetDexFile();
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return false;
  }
  const DexFile::AnnotationItem* annotation_item =
      SearchAnnotationSet(dex_file, annotation_set, "Ldalvik/annotation/InnerClass;",
                          DexFile::kDexVisibilitySystem);
  if (annotation_item == nullptr) {
    return false;
  }
  const uint8_t* annotation =
      SearchEncodedAnnotation(dex_file, annotation_item->annotation_, "accessFlags");
  if (annotation == nullptr) {
    return false;
  }
  DexFile::AnnotationValue annotation_value;
  if (!ProcessAnnotationValue(klass, &annotation, &annotation_value,
                              ScopedNullHandle<mirror::Class>(), DexFile::kAllRaw)) {
    return false;
  }
  if (annotation_value.type_ != DexFile::kDexAnnotationInt) {
    return false;
  }
  *flags = annotation_value.value_.GetI();
  return true;
}

mirror::ObjectArray<mirror::String>* GetSignatureAnnotationForClass(Handle<mirror::Class> klass) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return nullptr;
  }
  return GetSignatureValue(klass, annotation_set);
}

bool IsClassAnnotationPresent(Handle<mirror::Class> klass, Handle<mirror::Class> annotation_class) {
  const DexFile::AnnotationSetItem* annotation_set = FindAnnotationSetForClass(klass);
  if (annotation_set == nullptr) {
    return false;
  }
  const DexFile::AnnotationItem* annotation_item = GetAnnotationItemFromAnnotationSet(
      klass, annotation_set, DexFile::kDexVisibilityRuntime, annotation_class);
  return annotation_item != nullptr;
}

int32_t GetLineNumFromPC(const DexFile* dex_file, ArtMethod* method, uint32_t rel_pc) {
  // For native method, lineno should be -2 to indicate it is native. Note that
  // "line number == -2" is how libcore tells from StackTraceElement.
  if (method->GetCodeItemOffset() == 0) {
    return -2;
  }

  const DexFile::CodeItem* code_item = dex_file->GetCodeItem(method->GetCodeItemOffset());
  DCHECK(code_item != nullptr) << method->PrettyMethod() << " " << dex_file->GetLocation();

  // A method with no line number info should return -1
  DexFile::LineNumFromPcContext context(rel_pc, -1);
  dex_file->DecodeDebugPositionInfo(code_item, DexFile::LineNumForPcCb, &context);
  return context.line_num_;
}

template<bool kTransactionActive>
void RuntimeEncodedStaticFieldValueIterator::ReadValueToField(ArtField* field) const {
  DCHECK(dex_cache_ != nullptr);
  DCHECK(class_loader_ != nullptr);
  switch (type_) {
    case kBoolean: field->SetBoolean<kTransactionActive>(field->GetDeclaringClass(), jval_.z);
        break;
    case kByte:    field->SetByte<kTransactionActive>(field->GetDeclaringClass(), jval_.b); break;
    case kShort:   field->SetShort<kTransactionActive>(field->GetDeclaringClass(), jval_.s); break;
    case kChar:    field->SetChar<kTransactionActive>(field->GetDeclaringClass(), jval_.c); break;
    case kInt:     field->SetInt<kTransactionActive>(field->GetDeclaringClass(), jval_.i); break;
    case kLong:    field->SetLong<kTransactionActive>(field->GetDeclaringClass(), jval_.j); break;
    case kFloat:   field->SetFloat<kTransactionActive>(field->GetDeclaringClass(), jval_.f); break;
    case kDouble:  field->SetDouble<kTransactionActive>(field->GetDeclaringClass(), jval_.d); break;
    case kNull:    field->SetObject<kTransactionActive>(field->GetDeclaringClass(), nullptr); break;
    case kString: {
      mirror::String* resolved = linker_->ResolveString(dex_file_, jval_.i, *dex_cache_);
      field->SetObject<kTransactionActive>(field->GetDeclaringClass(), resolved);
      break;
    }
    case kType: {
      mirror::Class* resolved = linker_->ResolveType(dex_file_, jval_.i, *dex_cache_,
                                                     *class_loader_);
      field->SetObject<kTransactionActive>(field->GetDeclaringClass(), resolved);
      break;
    }
    default: UNIMPLEMENTED(FATAL) << ": type " << type_;
  }
}
template
void RuntimeEncodedStaticFieldValueIterator::ReadValueToField<true>(ArtField* field) const;
template
void RuntimeEncodedStaticFieldValueIterator::ReadValueToField<false>(ArtField* field) const;

}  // namespace annotations

}  // namespace art
