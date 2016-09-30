/* Copyright (C) 2016 The Android Open Source Project
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This file implements interfaces from the file jvmti.h. This implementation
 * is licensed under the same terms as the file jvmti.h.  The
 * copyright and license information for the file jvmti.h follows.
 *
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "transform.h"

#include "class_linker.h"
#include "dex_file.h"
#include "gc_root-inl.h"
#include "globals.h"
#include "jni_env_ext-inl.h"
#include "jvmti.h"
#include "linear_alloc.h"
#include "mem_map.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader-inl.h"
#include "mirror/string-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread_list.h"
#include "transform.h"
#include "utf.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace openjdkjvmti {

static bool ReadChecksum(jint data_len, const unsigned char* dex, /*out*/uint32_t* res) {
  if (data_len < static_cast<jint>(sizeof(art::DexFile::Header))) {
    return false;
  }
  *res = reinterpret_cast<const art::DexFile::Header*>(dex)->checksum_;
  return true;
}

static std::unique_ptr<art::MemMap> MoveDataToMemMap(const std::string& original_location,
                                                      jint data_len,
                                                      unsigned char* dex_data) {
  std::string error_msg;
  std::unique_ptr<art::MemMap> map(art::MemMap::MapAnonymous(
      art::StringPrintf("%s-transformed", original_location.c_str()).c_str(),
      nullptr,
      data_len,
      PROT_READ|PROT_WRITE,
      /*low_4gb*/false,
      /*reuse*/false,
      &error_msg));
  if (map == nullptr) {
    return map;
  }
  memcpy(map->Begin(), dex_data, data_len);
  map->Protect(PROT_READ);
  return map;
}

static void InvalidateExistingMethods(art::Thread* self,
                                      art::Handle<art::mirror::Class> klass,
                                      art::Handle<art::mirror::DexCache> cache,
                                      const art::DexFile* dex_file)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  // Create new DexCache with new DexFile.
  // reset dex_class_def_idx_
  // for each method reset entry_point_from_quick_compiled_code_ to bridge
  // for each method reset dex_code_item_offset_
  // for each method reset dex_method_index_
  // for each method set dex_cache_resolved_methods_ to new DexCache
  // for each method set dex_cache_resolved_types_ to new DexCache
  auto* runtime = art::Runtime::Current();
  art::ClassLinker* linker = runtime->GetClassLinker();
  art::PointerSize image_pointer_size = linker->GetImagePointerSize();
  std::string descriptor_storage;
  const char* descriptor = klass->GetDescriptor(&descriptor_storage);
  // Get the new class def
  const art::DexFile::ClassDef* class_def = art::OatFile::OatDexFile::FindClassDef(
      *dex_file, descriptor, art::ComputeModifiedUtf8Hash(descriptor));
  CHECK(class_def != nullptr);
  const art::DexFile::TypeId& declaring_class_id = dex_file->GetTypeId(class_def->class_idx_);
  art::StackHandleScope<6> hs(self);
  const art::DexFile& old_dex_file = klass->GetDexFile();
  for (art::ArtMethod& method : klass->GetMethods(image_pointer_size)) {
    // Find the code_item for the method then find the dex_method_index and dex_code_item_offset to
    // set.
    const art::DexFile::StringId* new_name_id = dex_file->FindStringId(method.GetName());
    uint16_t method_return_idx =
        dex_file->GetIndexForTypeId(*dex_file->FindTypeId(method.GetReturnTypeDescriptor()));
    const auto* old_type_list = method.GetParameterTypeList();
    std::vector<uint16_t> new_type_list;
    for (uint32_t i = 0; old_type_list != nullptr && i < old_type_list->Size(); i++) {
      new_type_list.push_back(
          dex_file->GetIndexForTypeId(
              *dex_file->FindTypeId(
                  old_dex_file.GetTypeDescriptor(
                      old_dex_file.GetTypeId(
                          old_type_list->GetTypeItem(i).type_idx_)))));
    }
    const art::DexFile::ProtoId* proto_id = dex_file->FindProtoId(method_return_idx,
                                                                  new_type_list);
    CHECK(proto_id != nullptr || old_type_list == nullptr);
    const art::DexFile::MethodId* method_id = dex_file->FindMethodId(declaring_class_id,
                                                                      *new_name_id,
                                                                      *proto_id);
    CHECK(method_id != nullptr);
    uint32_t dex_method_idx = dex_file->GetIndexForMethodId(*method_id);
    method.SetDexMethodIndex(dex_method_idx);
    linker->SetEntryPointsToInterpreter(&method);
    method.SetCodeItemOffset(dex_file->FindCodeItemOffset(*class_def, dex_method_idx));
    method.SetDexCacheResolvedMethods(cache->GetResolvedMethods(), image_pointer_size);
    method.SetDexCacheResolvedTypes(cache->GetResolvedTypes(), image_pointer_size);
  }

  // Update the class fields.
  // Need to update class last since the ArtMethod gets its DexFile from the class (which is needed
  // to call GetReturnTypeDescriptor and GetParameterTypeList above).
  klass->SetDexCache(cache.Get());
  klass->SetDexCacheStrings(cache->GetStrings());
  klass->SetDexClassDefIndex(dex_file->GetIndexForClassDef(*class_def));
  klass->SetDexTypeIndex(dex_file->GetIndexForTypeId(*dex_file->FindTypeId(descriptor)));
}

// Adds the dex file.
static art::mirror::LongArray* InsertDexFileIntoArray(art::Thread* self,
                                                      const art::DexFile* dex,
                                                      art::Handle<art::mirror::LongArray>& orig)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  art::StackHandleScope<1> hs(self);
  CHECK_GE(orig->GetLength(), 1);
  art::Handle<art::mirror::LongArray> ret(
      hs.NewHandle(art::mirror::LongArray::Alloc(self, orig->GetLength() + 1)));
  CHECK(ret.Get() != nullptr);
  // Copy the oat-dex.
  // TODO Should I clear the oatdex element?
  ret->SetWithoutChecks<false>(0, orig->GetWithoutChecks(0));
  ret->SetWithoutChecks<false>(1, static_cast<int64_t>(reinterpret_cast<intptr_t>(dex)));
  ret->Memcpy(2, orig.Get(), 1, orig->GetLength() - 1);
  return ret.Get();
}

// TODO Handle all types of class loaders.
static bool FindDalvikSystemDexFileAndLoaderForClass(
    art::Handle<art::mirror::Class> klass,
    /*out*/art::mirror::Object** dex_file,
    /*out*/art::mirror::ClassLoader** loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
  const char* dex_path_list_element_array_name = "[Ldalvik/system/DexPathList$Element;";
  const char* dex_path_list_element_name = "Ldalvik/system/DexPathList$Element;";
  const char* dex_file_name = "Ldalvik/system/DexFile;";
  const char* dex_path_list_name = "Ldalvik/system/DexPathList;";
  const char* dex_class_loader_name = "Ldalvik/system/BaseDexClassLoader;";

  art::Thread* self = art::Thread::Current();
  CHECK(!self->IsExceptionPending());
  art::StackHandleScope<11> hs(self);
  art::ClassLinker* class_linker = art::Runtime::Current()->GetClassLinker();

  art::Handle<art::mirror::ClassLoader> null_loader(hs.NewHandle<art::mirror::ClassLoader>(
      nullptr));
  art::Handle<art::mirror::Class> base_dex_loader_class(hs.NewHandle(class_linker->FindClass(
      self, dex_class_loader_name, null_loader)));

  art::ArtField* path_list_field = base_dex_loader_class->FindDeclaredInstanceField(
      "pathList", dex_path_list_name);
  CHECK(path_list_field != nullptr);

  art::ArtField* dex_path_list_element_field =
      class_linker->FindClass(self, dex_path_list_name, null_loader)
        ->FindDeclaredInstanceField("dexElements", dex_path_list_element_array_name);
  CHECK(dex_path_list_element_field != nullptr);

  art::ArtField* element_dex_file_field =
      class_linker->FindClass(self, dex_path_list_element_name, null_loader)
        ->FindDeclaredInstanceField("dexFile", dex_file_name);
  CHECK(element_dex_file_field != nullptr);

  art::Handle<art::mirror::ClassLoader> h_class_loader(hs.NewHandle(klass->GetClassLoader()));
  art::Handle<art::mirror::Class> loader_class(hs.NewHandle(h_class_loader->GetClass()));
  // Check if loader is a BaseDexClassLoader
  if (!loader_class->IsSubClass(base_dex_loader_class.Get())) {
    LOG(ERROR) << "The classloader is not a BaseDexClassLoader which is currently the only "
               << "supported class loader type!";
    return false;
  }
  art::Handle<art::mirror::Object> path_list(
      hs.NewHandle(path_list_field->GetObject(h_class_loader.Get())));
  CHECK(path_list.Get() != nullptr);
  CHECK(!self->IsExceptionPending());
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> dex_elements_list(
      hs.NewHandle(art::down_cast<art::mirror::ObjectArray<art::mirror::Object>*>(
          dex_path_list_element_field->GetObject(path_list.Get()))));
  CHECK(!self->IsExceptionPending());
  CHECK(dex_elements_list.Get() != nullptr);
  size_t num_elements = dex_elements_list->GetLength();
  art::MutableHandle<art::mirror::Object> current_element(
      hs.NewHandle<art::mirror::Object>(nullptr));
  art::MutableHandle<art::mirror::Object> first_dex_file(
      hs.NewHandle<art::mirror::Object>(nullptr));
  for (size_t i = 0; i < num_elements; i++) {
    current_element.Assign(dex_elements_list->Get(i));
    CHECK(current_element.Get() != nullptr);
    CHECK(!self->IsExceptionPending());
    CHECK(dex_elements_list.Get() != nullptr);
    CHECK_EQ(current_element->GetClass(), class_linker->FindClass(self,
                                                                  dex_path_list_element_name,
                                                                  null_loader));
    // TODO It would be cleaner to put the art::DexFile into the dalvik.system.DexFile the class
    // comes from but it is more annoying because we would need to find this class. It is not
    // necessary for proper function since we just need to be in front of the classes old dex file
    // in the path.
    first_dex_file.Assign(element_dex_file_field->GetObject(current_element.Get()));
    if (first_dex_file.Get() != nullptr) {
      *dex_file = first_dex_file.Get();
      *loader = h_class_loader.Get();
      return true;
    }
  }
  return false;
}

// Gets the data surrounding the given class.
jvmtiError GetTransformationData(ArtJvmTiEnv* env,
                                 jclass klass,
                                 /*out*/std::string* location,
                                 /*out*/JNIEnv** jni_env_ptr,
                                 /*out*/jobject* loader,
                                 /*out*/std::string* name,
                                 /*out*/jobject* protection_domain,
                                 /*out*/jint* data_len,
                                 /*out*/unsigned char** dex_data) {
  jint ret = env->art_vm->GetEnv(reinterpret_cast<void**>(jni_env_ptr), JNI_VERSION_1_1);
  if (ret != JNI_OK) {
    // TODO Different error might be better?
    return ERR(INTERNAL);
  }
  JNIEnv* jni_env = *jni_env_ptr;
  art::ScopedObjectAccess soa(jni_env);
  art::StackHandleScope<3> hs(art::Thread::Current());
  art::Handle<art::mirror::Class> hs_klass(hs.NewHandle(soa.Decode<art::mirror::Class>(klass)));
  *loader = soa.AddLocalReference<jobject>(hs_klass->GetClassLoader());
  *name = art::mirror::Class::ComputeName(hs_klass)->ToModifiedUtf8();
  // TODO is this always null?
  *protection_domain = nullptr;
  const art::DexFile& dex = hs_klass->GetDexFile();
  *location = dex.GetLocation();
  *data_len = static_cast<jint>(dex.Size());
  // TODO We should maybe change env->Allocate to allow us to mprotect this memory and stop writes.
  jvmtiError alloc_error = env->Allocate(*data_len, dex_data);
  if (alloc_error != OK) {
    return alloc_error;
  }
  // Copy the data into a temporary buffer.
  memcpy(reinterpret_cast<void*>(*dex_data),
          reinterpret_cast<const void*>(dex.Begin()),
          *data_len);
  return OK;
}

// Install the new dex file.
// TODO do error checks for bad state (method in a stack, changes to number of methods/fields/etc).
jvmtiError MoveTransformedFileIntoRuntime(jclass jklass,
                                          std::string original_location,
                                          jint data_len,
                                          unsigned char* dex_data) {
  const char* dex_file_name = "Ldalvik/system/DexFile;";
  art::Thread* self = art::Thread::Current();
  art::Runtime* runtime = art::Runtime::Current();
  art::ThreadList* threads = runtime->GetThreadList();
  art::ClassLinker* class_linker = runtime->GetClassLinker();
  uint32_t checksum = 0;
  if (!ReadChecksum(data_len, dex_data, &checksum)) {
    return ERR(INVALID_CLASS_FORMAT);
  }

  std::unique_ptr<art::MemMap> map(MoveDataToMemMap(original_location, data_len, dex_data));
  if (map.get() == nullptr) {
    return ERR(INTERNAL);
  }
  std::string error_msg;
  // Load the new dex_data in memory (mmap it, etc)
  std::unique_ptr<const art::DexFile> new_dex_file = art::DexFile::Open(map->GetName(),
                                                                        checksum,
                                                                        std::move(map),
                                                                        /*verify*/ true,
                                                                        /*verify_checksum*/ true,
                                                                        &error_msg);
  CHECK(new_dex_file.get() != nullptr) << "Unable to load dex file! " << error_msg;

  // Get mutator lock. We need the lifetimes of these variables (hs, the classes, etc.) to be longer
  // then current lock (since there isn't upgrading of the lock) so we don't use soa.
  art::ThreadState old_state = self->TransitionFromSuspendedToRunnable();
  // This scope is needed to make sure that the HandleScope dies with mutator_lock_ since we need to
  // upgrade the mutator_lock during the execution.
  {
    art::StackHandleScope<11> hs(self);
    art::Handle<art::mirror::ClassLoader> null_loader(
        hs.NewHandle<art::mirror::ClassLoader>(nullptr));
    CHECK(null_loader.Get() == nullptr);
    art::ArtField* dex_file_cookie_field = class_linker->
        FindClass(self, dex_file_name, null_loader)->
        FindDeclaredInstanceField("mCookie", "Ljava/lang/Object;");
    art::ArtField* dex_file_internal_cookie_field =
        class_linker->FindClass(self, dex_file_name, null_loader)
          ->FindDeclaredInstanceField("mInternalCookie", "Ljava/lang/Object;");
    CHECK(dex_file_cookie_field != nullptr);
    art::Handle<art::mirror::Class> klass(
        hs.NewHandle(art::down_cast<art::mirror::Class*>(self->DecodeJObject(jklass))));
    art::mirror::Object* dex_file_ptr = nullptr;
    art::mirror::ClassLoader* class_loader_ptr = nullptr;
    // Find dalvik.system.DexFile that represents the dex file we are changing.
    if (!FindDalvikSystemDexFileAndLoaderForClass(klass, &dex_file_ptr, &class_loader_ptr)) {
      self->TransitionFromRunnableToSuspended(old_state);
      LOG(ERROR) << "Could not find DexFile.";
      return ERR(INTERNAL);
    }
    art::Handle<art::mirror::Object> dex_file_obj(hs.NewHandle(dex_file_ptr));
    art::Handle<art::mirror::ClassLoader> class_loader(hs.NewHandle(class_loader_ptr));
    art::Handle<art::mirror::LongArray> art_dex_array(
        hs.NewHandle<art::mirror::LongArray>(
            dex_file_cookie_field->GetObject(dex_file_obj.Get())->AsLongArray()));
    art::Handle<art::mirror::LongArray> new_art_dex_array(
        hs.NewHandle<art::mirror::LongArray>(
            InsertDexFileIntoArray(self, new_dex_file.get(), art_dex_array)));
    art::Handle<art::mirror::DexCache> cache(
        hs.NewHandle(class_linker->RegisterDexFile(*new_dex_file.get(), class_loader.Get())));
    self->TransitionFromRunnableToSuspended(old_state);

    threads->SuspendAll("moving dex file into runtime", /*long_suspend*/true);
    // Change the mCookie field. Old value will be GC'd as normal.
    dex_file_cookie_field->SetObject<false>(dex_file_obj.Get(), new_art_dex_array.Get());
    dex_file_internal_cookie_field->SetObject<false>(dex_file_obj.Get(), new_art_dex_array.Get());
    // Invalidate existing methods.
    InvalidateExistingMethods(self, klass, cache, new_dex_file.release());
  }
  threads->ResumeAll();
  return OK;
}

}  // namespace openjdkjvmti
