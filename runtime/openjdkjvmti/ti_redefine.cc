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

#include "ti_redefine.h"

#include <limits>

#include "android-base/stringprintf.h"

#include "art_jvmti.h"
#include "base/logging.h"
#include "dex_file.h"
#include "dex_file_types.h"
#include "events-inl.h"
#include "gc/allocation_listener.h"
#include "gc/heap.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni_env_ext-inl.h"
#include "jvmti_allocator.h"
#include "mirror/class.h"
#include "mirror/class_ext.h"
#include "mirror/object.h"
#include "object_lock.h"
#include "runtime.h"
#include "ScopedLocalRef.h"

namespace openjdkjvmti {

using android::base::StringPrintf;

// This visitor walks thread stacks and allocates and sets up the obsolete methods. It also does
// some basic sanity checks that the obsolete method is sane.
class ObsoleteMethodStackVisitor : public art::StackVisitor {
 protected:
  ObsoleteMethodStackVisitor(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps,
      /*out*/bool* success,
      /*out*/std::string* error_msg)
        : StackVisitor(thread,
                       /*context*/nullptr,
                       StackVisitor::StackWalkKind::kIncludeInlinedFrames),
          allocator_(allocator),
          obsoleted_methods_(obsoleted_methods),
          obsolete_maps_(obsolete_maps),
          success_(success),
          is_runtime_frame_(false),
          error_msg_(error_msg) {
    *success_ = true;
  }

  ~ObsoleteMethodStackVisitor() OVERRIDE {}

 public:
  // Returns true if we successfully installed obsolete methods on this thread, filling
  // obsolete_maps_ with the translations if needed. Returns false and fills error_msg if we fail.
  // The stack is cleaned up when we fail.
  static bool UpdateObsoleteFrames(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps,
      /*out*/std::string* error_msg) REQUIRES(art::Locks::mutator_lock_) {
    bool success = true;
    ObsoleteMethodStackVisitor visitor(thread,
                                       allocator,
                                       obsoleted_methods,
                                       obsolete_maps,
                                       &success,
                                       error_msg);
    visitor.WalkStack();
    if (!success) {
      RestoreFrames(thread, *obsolete_maps, error_msg);
      return false;
    } else {
      return true;
    }
  }

  static void RestoreFrames(
      art::Thread* thread ATTRIBUTE_UNUSED,
      const std::unordered_map<art::ArtMethod*, art::ArtMethod*>& obsolete_maps ATTRIBUTE_UNUSED,
      std::string* error_msg)
        REQUIRES(art::Locks::mutator_lock_) {
    LOG(FATAL) << "Restoring stack frames is not yet supported. Error was: " << *error_msg;
  }

  bool VisitFrame() OVERRIDE REQUIRES(art::Locks::mutator_lock_) {
    art::ArtMethod* old_method = GetMethod();
    // TODO REMOVE once either current_method doesn't stick around through suspend points or deopt
    // works through runtime methods.
    bool prev_was_runtime_frame_ = is_runtime_frame_;
    is_runtime_frame_ = old_method->IsRuntimeMethod();
    if (obsoleted_methods_.find(old_method) != obsoleted_methods_.end()) {
      // The check below works since when we deoptimize we set shadow frames for all frames until a
      // native/runtime transition and for those set the return PC to a function that will complete
      // the deoptimization. This does leave us with the unfortunate side-effect that frames just
      // below runtime frames cannot be deoptimized at the moment.
      // TODO REMOVE once either current_method doesn't stick around through suspend points or deopt
      // works through runtime methods.
      // TODO b/33616143
      if (!IsShadowFrame() && prev_was_runtime_frame_) {
        *error_msg_ = StringPrintf("Deoptimization failed due to runtime method in stack.");
        *success_ = false;
        return false;
      }
      // We cannot ensure that the right dex file is used in inlined frames so we don't support
      // redefining them.
      DCHECK(!IsInInlinedFrame()) << "Inlined frames are not supported when using redefinition";
      // TODO We should really support intrinsic obsolete methods.
      // TODO We should really support redefining intrinsics.
      // We don't support intrinsics so check for them here.
      DCHECK(!old_method->IsIntrinsic());
      art::ArtMethod* new_obsolete_method = nullptr;
      auto obsolete_method_pair = obsolete_maps_->find(old_method);
      if (obsolete_method_pair == obsolete_maps_->end()) {
        // Create a new Obsolete Method and put it in the list.
        art::Runtime* runtime = art::Runtime::Current();
        art::ClassLinker* cl = runtime->GetClassLinker();
        auto ptr_size = cl->GetImagePointerSize();
        const size_t method_size = art::ArtMethod::Size(ptr_size);
        auto* method_storage = allocator_->Alloc(GetThread(), method_size);
        if (method_storage == nullptr) {
          *success_ = false;
          *error_msg_ = StringPrintf("Unable to allocate storage for obsolete version of '%s'",
                                     old_method->PrettyMethod().c_str());
          return false;
        }
        new_obsolete_method = new (method_storage) art::ArtMethod();
        new_obsolete_method->CopyFrom(old_method, ptr_size);
        DCHECK_EQ(new_obsolete_method->GetDeclaringClass(), old_method->GetDeclaringClass());
        new_obsolete_method->SetIsObsolete();
        obsolete_maps_->insert({old_method, new_obsolete_method});
        // Update JIT Data structures to point to the new method.
        art::jit::Jit* jit = art::Runtime::Current()->GetJit();
        if (jit != nullptr) {
          // Notify the JIT we are making this obsolete method. It will update the jit's internal
          // structures to keep track of the new obsolete method.
          jit->GetCodeCache()->MoveObsoleteMethod(old_method, new_obsolete_method);
        }
      } else {
        new_obsolete_method = obsolete_method_pair->second;
      }
      DCHECK(new_obsolete_method != nullptr);
      SetMethod(new_obsolete_method);
    }
    return true;
  }

 private:
  // The linear allocator we should use to make new methods.
  art::LinearAlloc* allocator_;
  // The set of all methods which could be obsoleted.
  const std::unordered_set<art::ArtMethod*>& obsoleted_methods_;
  // A map from the original to the newly allocated obsolete method for frames on this thread. The
  // values in this map must be added to the obsolete_methods_ (and obsolete_dex_caches_) fields of
  // the redefined classes ClassExt by the caller.
  std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps_;
  bool* success_;
  // TODO REMOVE once either current_method doesn't stick around through suspend points or deopt
  // works through runtime methods.
  bool is_runtime_frame_;
  std::string* error_msg_;
};

jvmtiError Redefiner::IsModifiableClass(jvmtiEnv* env ATTRIBUTE_UNUSED,
                                        jclass klass,
                                        jboolean* is_redefinable) {
  // TODO Check for the appropriate feature flags once we have enabled them.
  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  art::ObjPtr<art::mirror::Object> obj(self->DecodeJObject(klass));
  if (obj.IsNull()) {
    return ERR(INVALID_CLASS);
  }
  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(obj->AsClass()));
  std::string err_unused;
  *is_redefinable =
      Redefiner::GetClassRedefinitionError(h_klass, &err_unused) == OK ? JNI_TRUE : JNI_FALSE;
  return OK;
}

jvmtiError Redefiner::GetClassRedefinitionError(art::Handle<art::mirror::Class> klass,
                                                /*out*/std::string* error_msg) {
  if (klass->IsPrimitive()) {
    *error_msg = "Modification of primitive classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsInterface()) {
    *error_msg = "Modification of Interface classes is currently not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsArrayClass()) {
    *error_msg = "Modification of Array classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  } else if (klass->IsProxyClass()) {
    *error_msg = "Modification of proxy classes is not supported";
    return ERR(UNMODIFIABLE_CLASS);
  }

  // TODO We should check if the class has non-obsoletable methods on the stack
  LOG(WARNING) << "presence of non-obsoletable methods on stacks is not currently checked";
  return OK;
}

// Moves dex data to an anonymous, read-only mmap'd region.
std::unique_ptr<art::MemMap> Redefiner::MoveDataToMemMap(const std::string& original_location,
                                                         jint data_len,
                                                         unsigned char* dex_data,
                                                         std::string* error_msg) {
  std::unique_ptr<art::MemMap> map(art::MemMap::MapAnonymous(
      StringPrintf("%s-transformed", original_location.c_str()).c_str(),
      nullptr,
      data_len,
      PROT_READ|PROT_WRITE,
      /*low_4gb*/false,
      /*reuse*/false,
      error_msg));
  if (map == nullptr) {
    return map;
  }
  memcpy(map->Begin(), dex_data, data_len);
  // Make the dex files mmap read only. This matches how other DexFiles are mmaped and prevents
  // programs from corrupting it.
  map->Protect(PROT_READ);
  return map;
}

// TODO This should handle doing multiple classes at once so we need to do less cleanup when things
// go wrong.
jvmtiError Redefiner::RedefineClass(ArtJvmTiEnv* env,
                                    art::Runtime* runtime,
                                    art::Thread* self,
                                    jclass klass,
                                    const std::string& original_dex_location,
                                    jint data_len,
                                    unsigned char* dex_data,
                                    std::string* error_msg) {
  std::unique_ptr<art::MemMap> map(MoveDataToMemMap(original_dex_location,
                                                    data_len,
                                                    dex_data,
                                                    error_msg));
  std::ostringstream os;
  char* generic_ptr_unused = nullptr;
  char* signature_ptr = nullptr;
  if (env->GetClassSignature(klass, &signature_ptr, &generic_ptr_unused) != OK) {
    signature_ptr = const_cast<char*>("<UNKNOWN CLASS>");
  }
  if (map.get() == nullptr) {
    os << "Failed to create anonymous mmap for modified dex file of class " << signature_ptr
       << "in dex file " << original_dex_location << " because: " << *error_msg;
    *error_msg = os.str();
    return ERR(OUT_OF_MEMORY);
  }
  if (map->Size() < sizeof(art::DexFile::Header)) {
    *error_msg = "Could not read dex file header because dex_data was too short";
    return ERR(INVALID_CLASS_FORMAT);
  }
  uint32_t checksum = reinterpret_cast<const art::DexFile::Header*>(map->Begin())->checksum_;
  std::unique_ptr<const art::DexFile> dex_file(art::DexFile::Open(map->GetName(),
                                                                  checksum,
                                                                  std::move(map),
                                                                  /*verify*/true,
                                                                  /*verify_checksum*/true,
                                                                  error_msg));
  if (dex_file.get() == nullptr) {
    os << "Unable to load modified dex file for " << signature_ptr << ": " << *error_msg;
    *error_msg = os.str();
    return ERR(INVALID_CLASS_FORMAT);
  }
  // Stop JIT for the duration of this redefine since the JIT might concurrently compile a method we
  // are going to redefine.
  art::jit::ScopedJitSuspend suspend_jit;
  // Get shared mutator lock.
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  Redefiner r(runtime, self, klass, signature_ptr, dex_file, error_msg);
  // Lock around this class to avoid races.
  art::ObjectLock<art::mirror::Class> lock(self, hs.NewHandle(r.GetMirrorClass()));
  return r.Run();
}

// TODO *MAJOR* This should return the actual source java.lang.DexFile object for the klass.
// TODO Make mirror of DexFile and associated types to make this less hellish.
// TODO Make mirror of BaseDexClassLoader and associated types to make this less hellish.
art::mirror::Object* Redefiner::FindSourceDexFileObject(
    art::Handle<art::mirror::ClassLoader> loader) {
  const char* dex_path_list_element_array_name = "[Ldalvik/system/DexPathList$Element;";
  const char* dex_path_list_element_name = "Ldalvik/system/DexPathList$Element;";
  const char* dex_file_name = "Ldalvik/system/DexFile;";
  const char* dex_path_list_name = "Ldalvik/system/DexPathList;";
  const char* dex_class_loader_name = "Ldalvik/system/BaseDexClassLoader;";

  CHECK(!self_->IsExceptionPending());
  art::StackHandleScope<11> hs(self_);
  art::ClassLinker* class_linker = runtime_->GetClassLinker();

  art::Handle<art::mirror::ClassLoader> null_loader(hs.NewHandle<art::mirror::ClassLoader>(
      nullptr));
  art::Handle<art::mirror::Class> base_dex_loader_class(hs.NewHandle(class_linker->FindClass(
      self_, dex_class_loader_name, null_loader)));

  // Get all the ArtFields so we can look in the BaseDexClassLoader
  art::ArtField* path_list_field = base_dex_loader_class->FindDeclaredInstanceField(
      "pathList", dex_path_list_name);
  CHECK(path_list_field != nullptr);

  art::ArtField* dex_path_list_element_field =
      class_linker->FindClass(self_, dex_path_list_name, null_loader)
        ->FindDeclaredInstanceField("dexElements", dex_path_list_element_array_name);
  CHECK(dex_path_list_element_field != nullptr);

  art::ArtField* element_dex_file_field =
      class_linker->FindClass(self_, dex_path_list_element_name, null_loader)
        ->FindDeclaredInstanceField("dexFile", dex_file_name);
  CHECK(element_dex_file_field != nullptr);

  // Check if loader is a BaseDexClassLoader
  art::Handle<art::mirror::Class> loader_class(hs.NewHandle(loader->GetClass()));
  if (!loader_class->IsSubClass(base_dex_loader_class.Get())) {
    LOG(ERROR) << "The classloader is not a BaseDexClassLoader which is currently the only "
               << "supported class loader type!";
    return nullptr;
  }
  // Start navigating the fields of the loader (now known to be a BaseDexClassLoader derivative)
  art::Handle<art::mirror::Object> path_list(
      hs.NewHandle(path_list_field->GetObject(loader.Get())));
  CHECK(path_list.Get() != nullptr);
  CHECK(!self_->IsExceptionPending());
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> dex_elements_list(hs.NewHandle(
      dex_path_list_element_field->GetObject(path_list.Get())->
      AsObjectArray<art::mirror::Object>()));
  CHECK(!self_->IsExceptionPending());
  CHECK(dex_elements_list.Get() != nullptr);
  size_t num_elements = dex_elements_list->GetLength();
  art::MutableHandle<art::mirror::Object> current_element(
      hs.NewHandle<art::mirror::Object>(nullptr));
  art::MutableHandle<art::mirror::Object> first_dex_file(
      hs.NewHandle<art::mirror::Object>(nullptr));
  // Iterate over the DexPathList$Element to find the right one
  // TODO Or not ATM just return the first one.
  for (size_t i = 0; i < num_elements; i++) {
    current_element.Assign(dex_elements_list->Get(i));
    CHECK(current_element.Get() != nullptr);
    CHECK(!self_->IsExceptionPending());
    CHECK(dex_elements_list.Get() != nullptr);
    CHECK_EQ(current_element->GetClass(), class_linker->FindClass(self_,
                                                                  dex_path_list_element_name,
                                                                  null_loader));
    // TODO It would be cleaner to put the art::DexFile into the dalvik.system.DexFile the class
    // comes from but it is more annoying because we would need to find this class. It is not
    // necessary for proper function since we just need to be in front of the classes old dex file
    // in the path.
    first_dex_file.Assign(element_dex_file_field->GetObject(current_element.Get()));
    if (first_dex_file.Get() != nullptr) {
      return first_dex_file.Get();
    }
  }
  return nullptr;
}

art::mirror::Class* Redefiner::GetMirrorClass() {
  return self_->DecodeJObject(klass_)->AsClass();
}

art::mirror::ClassLoader* Redefiner::GetClassLoader() {
  return GetMirrorClass()->GetClassLoader();
}

art::mirror::DexCache* Redefiner::CreateNewDexCache(art::Handle<art::mirror::ClassLoader> loader) {
  return runtime_->GetClassLinker()->RegisterDexFile(*dex_file_, loader.Get());
}

// TODO Really wishing I had that mirror of java.lang.DexFile now.
art::mirror::LongArray* Redefiner::AllocateDexFileCookie(
    art::Handle<art::mirror::Object> java_dex_file_obj) {
  art::StackHandleScope<2> hs(self_);
  // mCookie is nulled out if the DexFile has been closed but mInternalCookie sticks around until
  // the object is finalized. Since they always point to the same array if mCookie is not null we
  // just use the mInternalCookie field. We will update one or both of these fields later.
  // TODO Should I get the class from the classloader or directly?
  art::ArtField* internal_cookie_field = java_dex_file_obj->GetClass()->FindDeclaredInstanceField(
      "mInternalCookie", "Ljava/lang/Object;");
  // TODO Add check that mCookie is either null or same as mInternalCookie
  CHECK(internal_cookie_field != nullptr);
  art::Handle<art::mirror::LongArray> cookie(
      hs.NewHandle(internal_cookie_field->GetObject(java_dex_file_obj.Get())->AsLongArray()));
  // TODO Maybe make these non-fatal.
  CHECK(cookie.Get() != nullptr);
  CHECK_GE(cookie->GetLength(), 1);
  art::Handle<art::mirror::LongArray> new_cookie(
      hs.NewHandle(art::mirror::LongArray::Alloc(self_, cookie->GetLength() + 1)));
  if (new_cookie.Get() == nullptr) {
    self_->AssertPendingOOMException();
    return nullptr;
  }
  // Copy the oat-dex field at the start.
  // TODO Should I clear this field?
  // TODO This is a really crappy thing here with the first element being different.
  new_cookie->SetWithoutChecks<false>(0, cookie->GetWithoutChecks(0));
  new_cookie->SetWithoutChecks<false>(
      1, static_cast<int64_t>(reinterpret_cast<intptr_t>(dex_file_.get())));
  new_cookie->Memcpy(2, cookie.Get(), 1, cookie->GetLength() - 1);
  return new_cookie.Get();
}

void Redefiner::RecordFailure(jvmtiError result, const std::string& error_msg) {
  *error_msg_ = StringPrintf("Unable to perform redefinition of '%s': %s",
                             class_sig_,
                             error_msg.c_str());
  result_ = result;
}

bool Redefiner::FinishRemainingAllocations(
    /*out*/art::MutableHandle<art::mirror::ClassLoader>* source_class_loader,
    /*out*/art::MutableHandle<art::mirror::Object>* java_dex_file_obj,
    /*out*/art::MutableHandle<art::mirror::LongArray>* new_dex_file_cookie,
    /*out*/art::MutableHandle<art::mirror::DexCache>* new_dex_cache) {
  art::StackHandleScope<4> hs(self_);
  // This shouldn't allocate
  art::Handle<art::mirror::ClassLoader> loader(hs.NewHandle(GetClassLoader()));
  if (loader.Get() == nullptr) {
    // TODO Better error msg.
    RecordFailure(ERR(INTERNAL), "Unable to find class loader!");
    return false;
  }
  art::Handle<art::mirror::Object> dex_file_obj(hs.NewHandle(FindSourceDexFileObject(loader)));
  if (dex_file_obj.Get() == nullptr) {
    // TODO Better error msg.
    RecordFailure(ERR(INTERNAL), "Unable to find class loader!");
    return false;
  }
  art::Handle<art::mirror::LongArray> new_cookie(hs.NewHandle(AllocateDexFileCookie(dex_file_obj)));
  if (new_cookie.Get() == nullptr) {
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate dex file array for class loader");
    return false;
  }
  art::Handle<art::mirror::DexCache> dex_cache(hs.NewHandle(CreateNewDexCache(loader)));
  if (dex_cache.Get() == nullptr) {
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate DexCache");
    return false;
  }
  source_class_loader->Assign(loader.Get());
  java_dex_file_obj->Assign(dex_file_obj.Get());
  new_dex_file_cookie->Assign(new_cookie.Get());
  new_dex_cache->Assign(dex_cache.Get());
  return true;
}

struct CallbackCtx {
  Redefiner* const r;
  art::LinearAlloc* allocator;
  std::unordered_map<art::ArtMethod*, art::ArtMethod*> obsolete_map;
  std::unordered_set<art::ArtMethod*> obsolete_methods;
  bool success;
  std::string* error_msg;

  CallbackCtx(Redefiner* self, art::LinearAlloc* alloc, std::string* error)
      : r(self), allocator(alloc), success(true), error_msg(error) {}
};

void DoRestoreObsoleteMethodsCallback(art::Thread* t, void* vdata) NO_THREAD_SAFETY_ANALYSIS {
  CallbackCtx* data = reinterpret_cast<CallbackCtx*>(vdata);
  ObsoleteMethodStackVisitor::RestoreFrames(t, data->obsolete_map, data->error_msg);
}

void DoAllocateObsoleteMethodsCallback(art::Thread* t, void* vdata) NO_THREAD_SAFETY_ANALYSIS {
  CallbackCtx* data = reinterpret_cast<CallbackCtx*>(vdata);
  if (data->success) {
    // Don't do anything if we already failed once.
    data->success = ObsoleteMethodStackVisitor::UpdateObsoleteFrames(t,
                                                                     data->allocator,
                                                                     data->obsolete_methods,
                                                                     &data->obsolete_map,
                                                                     data->error_msg);
  }
}

// This creates any ArtMethod* structures needed for obsolete methods and ensures that the stack is
// updated so they will be run.
bool Redefiner::FindAndAllocateObsoleteMethods(art::mirror::Class* art_klass) {
  art::ScopedAssertNoThreadSuspension ns("No thread suspension during thread stack walking");
  art::mirror::ClassExt* ext = art_klass->GetExtData();
  CHECK(ext->GetObsoleteMethods() != nullptr);
  CallbackCtx ctx(this, art_klass->GetClassLoader()->GetAllocator(), error_msg_);
  // Add all the declared methods to the map
  for (auto& m : art_klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
    ctx.obsolete_methods.insert(&m);
  }
  for (art::ArtMethod* old_method : ctx.obsolete_methods) {
    if (old_method->IsIntrinsic()) {
      *error_msg_ = StringPrintf("Method '%s' is intrinsic and cannot be made obsolete!",
                                 old_method->PrettyMethod().c_str());
      return false;
    }
  }
  {
    art::MutexLock mu(self_, *art::Locks::thread_list_lock_);
    art::ThreadList* list = art::Runtime::Current()->GetThreadList();
    list->ForEach(DoAllocateObsoleteMethodsCallback, static_cast<void*>(&ctx));
    if (!ctx.success) {
      list->ForEach(DoRestoreObsoleteMethodsCallback, static_cast<void*>(&ctx));
      return false;
    }
  }
  FillObsoleteMethodMap(art_klass, ctx.obsolete_map);
  return true;
}

// Fills the obsolete method map in the art_klass's extData. This is so obsolete methods are able to
// figure out their DexCaches.
void Redefiner::FillObsoleteMethodMap(
    art::mirror::Class* art_klass,
    const std::unordered_map<art::ArtMethod*, art::ArtMethod*>& obsoletes) {
  int32_t index = 0;
  art::mirror::ClassExt* ext_data = art_klass->GetExtData();
  art::mirror::PointerArray* obsolete_methods = ext_data->GetObsoleteMethods();
  art::mirror::ObjectArray<art::mirror::DexCache>* obsolete_dex_caches =
      ext_data->GetObsoleteDexCaches();
  int32_t num_method_slots = obsolete_methods->GetLength();
  // Find the first empty index.
  for (; index < num_method_slots; index++) {
    if (obsolete_methods->GetElementPtrSize<art::ArtMethod*>(
          index, art::kRuntimePointerSize) == nullptr) {
      break;
    }
  }
  // Make sure we have enough space.
  CHECK_GT(num_method_slots, static_cast<int32_t>(obsoletes.size() + index));
  CHECK(obsolete_dex_caches->Get(index) == nullptr);
  // Fill in the map.
  for (auto& obs : obsoletes) {
    obsolete_methods->SetElementPtrSize(index, obs.second, art::kRuntimePointerSize);
    obsolete_dex_caches->Set(index, art_klass->GetDexCache());
    index++;
  }
}

// TODO It should be possible to only deoptimize the specific obsolete methods.
// TODO ReJitEverything can (sort of) fail. In certain cases it will skip deoptimizing some frames.
// If one of these frames is an obsolete method we have a problem. b/33616143
// TODO This shouldn't be necessary once we can ensure that the current method is not kept in
// registers across suspend points.
// TODO Pending b/33630159
void Redefiner::EnsureObsoleteMethodsAreDeoptimized() {
  art::ScopedAssertNoThreadSuspension nts("Deoptimizing everything!");
  art::instrumentation::Instrumentation* i = runtime_->GetInstrumentation();
  i->ReJitEverything("libOpenJkdJvmti - Class Redefinition");
}

bool Redefiner::CheckClass() {
  // TODO Might just want to put it in a ObjPtr and NoSuspend assert.
  art::StackHandleScope<1> hs(self_);
  // Easy check that only 1 class def is present.
  if (dex_file_->NumClassDefs() != 1) {
    RecordFailure(ERR(ILLEGAL_ARGUMENT),
                  StringPrintf("Expected 1 class def in dex file but found %d",
                               dex_file_->NumClassDefs()));
    return false;
  }
  // Get the ClassDef from the new DexFile.
  // Since the dex file has only a single class def the index is always 0.
  const art::DexFile::ClassDef& def = dex_file_->GetClassDef(0);
  // Get the class as it is now.
  art::Handle<art::mirror::Class> current_class(hs.NewHandle(GetMirrorClass()));

  // Check the access flags didn't change.
  if (def.GetJavaAccessFlags() != (current_class->GetAccessFlags() & art::kAccValidClassFlags)) {
    RecordFailure(ERR(UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED),
                  "Cannot change modifiers of class by redefinition");
    return false;
  }

  // Check class name.
  // These should have been checked by the dexfile verifier on load.
  DCHECK_NE(def.class_idx_, art::dex::TypeIndex::Invalid()) << "Invalid type index";
  const char* descriptor = dex_file_->StringByTypeIdx(def.class_idx_);
  DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
  if (!current_class->DescriptorEquals(descriptor)) {
    std::string storage;
    RecordFailure(ERR(NAMES_DONT_MATCH),
                  StringPrintf("expected file to contain class called '%s' but found '%s'!",
                               current_class->GetDescriptor(&storage),
                               descriptor));
    return false;
  }
  if (current_class->IsObjectClass()) {
    if (def.superclass_idx_ != art::dex::TypeIndex::Invalid()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass added!");
      return false;
    }
  } else {
    const char* super_descriptor = dex_file_->StringByTypeIdx(def.superclass_idx_);
    DCHECK(descriptor != nullptr) << "Invalid dex file structure!";
    if (!current_class->GetSuperClass()->DescriptorEquals(super_descriptor)) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Superclass changed");
      return false;
    }
  }
  const art::DexFile::TypeList* interfaces = dex_file_->GetInterfacesList(def);
  if (interfaces == nullptr) {
    if (current_class->NumDirectInterfaces() != 0) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added");
      return false;
    }
  } else {
    DCHECK(!current_class->IsProxyClass());
    const art::DexFile::TypeList* current_interfaces = current_class->GetInterfaceTypeList();
    if (current_interfaces == nullptr || current_interfaces->Size() != interfaces->Size()) {
      RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED), "Interfaces added or removed");
      return false;
    }
    // The order of interfaces is (barely) meaningful so we error if it changes.
    const art::DexFile& orig_dex_file = current_class->GetDexFile();
    for (uint32_t i = 0; i < interfaces->Size(); i++) {
      if (strcmp(
            dex_file_->StringByTypeIdx(interfaces->GetTypeItem(i).type_idx_),
            orig_dex_file.StringByTypeIdx(current_interfaces->GetTypeItem(i).type_idx_)) != 0) {
        RecordFailure(ERR(UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED),
                      "Interfaces changed or re-ordered");
        return false;
      }
    }
  }
  LOG(WARNING) << "No verification is done on annotations of redefined classes.";

  return true;
}

// TODO Move this to use IsRedefinable when that function is made.
bool Redefiner::CheckRedefinable() {
  std::string err;
  art::StackHandleScope<1> hs(self_);

  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  jvmtiError res = Redefiner::GetClassRedefinitionError(h_klass, &err);
  if (res != OK) {
    RecordFailure(res, err);
    return false;
  } else {
    return true;
  }
}

bool Redefiner::CheckRedefinitionIsValid() {
  return CheckRedefinable() &&
      CheckClass() &&
      CheckSameFields() &&
      CheckSameMethods();
}

jvmtiError Redefiner::Run() {
  art::StackHandleScope<5> hs(self_);
  // TODO We might want to have a global lock (or one based on the class being redefined at least)
  // in order to make cleanup easier. Not a huge deal though.
  //
  // First we just allocate the ClassExt and its fields that we need. These can be updated
  // atomically without any issues (since we allocate the map arrays as empty) so we don't bother
  // doing a try loop. The other allocations we need to ensure that nothing has changed in the time
  // between allocating them and pausing all threads before we can update them so we need to do a
  // try loop.
  if (!CheckRedefinitionIsValid() || !EnsureClassAllocationsFinished()) {
    return result_;
  }
  art::MutableHandle<art::mirror::ClassLoader> source_class_loader(
      hs.NewHandle<art::mirror::ClassLoader>(nullptr));
  art::MutableHandle<art::mirror::Object> java_dex_file(
      hs.NewHandle<art::mirror::Object>(nullptr));
  art::MutableHandle<art::mirror::LongArray> new_dex_file_cookie(
      hs.NewHandle<art::mirror::LongArray>(nullptr));
  art::MutableHandle<art::mirror::DexCache> new_dex_cache(
      hs.NewHandle<art::mirror::DexCache>(nullptr));
  if (!FinishRemainingAllocations(&source_class_loader,
                                  &java_dex_file,
                                  &new_dex_file_cookie,
                                  &new_dex_cache)) {
    // TODO Null out the ClassExt fields we allocated (if possible, might be racing with another
    // redefineclass call which made it even bigger. Leak shouldn't be huge (2x array of size
    // declared_methods_.length) but would be good to get rid of.
    // new_dex_file_cookie & new_dex_cache should be cleaned up by the GC.
    return result_;
  }
  // Get the mirror class now that we aren't allocating anymore.
  art::Handle<art::mirror::Class> art_class(hs.NewHandle(GetMirrorClass()));
  // Disable GC and wait for it to be done if we are a moving GC.  This is fine since we are done
  // allocating so no deadlocks.
  art::gc::Heap* heap = runtime_->GetHeap();
  if (heap->IsGcConcurrentAndMoving()) {
    // GC moving objects can cause deadlocks as we are deoptimizing the stack.
    heap->IncrementDisableMovingGC(self_);
  }
  // Enable assertion that this thread isn't interrupted during this installation.
  // After this we will need to do real cleanup in case of failure. Prior to this we could simply
  // return and would let everything get cleaned up or harmlessly leaked.
  // Do transition to final suspension
  // TODO We might want to give this its own suspended state!
  // TODO This isn't right. We need to change state without any chance of suspend ideally!
  self_->TransitionFromRunnableToSuspended(art::ThreadState::kNative);
  runtime_->GetThreadList()->SuspendAll(
      "Final installation of redefined Class!", /*long_suspend*/true);
  // TODO We need to invalidate all breakpoints in the redefined class with the debugger.
  // TODO We need to deal with any instrumentation/debugger deoptimized_methods_.
  // TODO We need to update all debugger MethodIDs so they note the method they point to is
  // obsolete or implement some other well defined semantics.
  // TODO We need to decide on & implement semantics for JNI jmethodids when we redefine methods.
  // TODO Might want to move this into a different type.
  // Now we reach the part where we must do active cleanup if something fails.
  // TODO We should really Retry if this fails instead of simply aborting.
  // Set the new DexFileCookie returns the original so we can fix it back up if redefinition fails
  art::ObjPtr<art::mirror::LongArray> original_dex_file_cookie(nullptr);
  if (!UpdateJavaDexFile(java_dex_file.Get(),
                         new_dex_file_cookie.Get(),
                         &original_dex_file_cookie) ||
      !FindAndAllocateObsoleteMethods(art_class.Get())) {
    // Release suspendAll
    runtime_->GetThreadList()->ResumeAll();
    // Get back shared mutator lock as expected for return.
    self_->TransitionFromSuspendedToRunnable();
    if (heap->IsGcConcurrentAndMoving()) {
      heap->DecrementDisableMovingGC(self_);
    }
    return result_;
  }
  if (!UpdateClass(art_class.Get(), new_dex_cache.Get())) {
    // TODO Should have some form of scope to do this.
    RestoreJavaDexFile(java_dex_file.Get(), original_dex_file_cookie);
    // Release suspendAll
    runtime_->GetThreadList()->ResumeAll();
    // Get back shared mutator lock as expected for return.
    self_->TransitionFromSuspendedToRunnable();
    if (heap->IsGcConcurrentAndMoving()) {
      heap->DecrementDisableMovingGC(self_);
    }
    return result_;
  }
  // Ensure that obsolete methods are deoptimized. This is needed since optimized methods may have
  // pointers to their ArtMethod's stashed in registers that they then use to attempt to hit the
  // DexCache.
  // TODO This can fail (leave some methods optimized) near runtime methods (including
  // quick-to-interpreter transition function).
  // TODO We probably don't need this at all once we have a way to ensure that the
  // current_art_method is never stashed in a (physical) register by the JIT and lost to the
  // stack-walker.
  EnsureObsoleteMethodsAreDeoptimized();
  // TODO Verify the new Class.
  // TODO   Failure then undo updates to class
  // TODO Shrink the obsolete method maps if possible?
  // TODO find appropriate class loader.
  // TODO Put this into a scoped thing.
  runtime_->GetThreadList()->ResumeAll();
  // Get back shared mutator lock as expected for return.
  self_->TransitionFromSuspendedToRunnable();
  // TODO Do the dex_file_ release at a more reasonable place. This works but it muddles who really
  // owns the DexFile.
  dex_file_.release();
  if (heap->IsGcConcurrentAndMoving()) {
    heap->DecrementDisableMovingGC(self_);
  }
  return OK;
}

void Redefiner::RestoreJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                                   art::ObjPtr<art::mirror::LongArray> orig_cookie) {
  art::ArtField* internal_cookie_field = java_dex_file->GetClass()->FindDeclaredInstanceField(
      "mInternalCookie", "Ljava/lang/Object;");
  art::ArtField* cookie_field = java_dex_file->GetClass()->FindDeclaredInstanceField(
      "mCookie", "Ljava/lang/Object;");
  art::ObjPtr<art::mirror::LongArray> new_cookie(
      cookie_field->GetObject(java_dex_file)->AsLongArray());
  internal_cookie_field->SetObject<false>(java_dex_file, orig_cookie);
  if (!new_cookie.IsNull()) {
    cookie_field->SetObject<false>(java_dex_file, orig_cookie);
  }
}

bool Redefiner::UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                              art::ObjPtr<art::mirror::DexCache> new_dex_cache,
                              const art::DexFile::ClassDef& class_def) {
  art::ClassLinker* linker = runtime_->GetClassLinker();
  art::PointerSize image_pointer_size = linker->GetImagePointerSize();
  const art::DexFile::TypeId& declaring_class_id = dex_file_->GetTypeId(class_def.class_idx_);
  const art::DexFile& old_dex_file = mclass->GetDexFile();
  // Update methods.
  for (art::ArtMethod& method : mclass->GetMethods(image_pointer_size)) {
    const art::DexFile::StringId* new_name_id = dex_file_->FindStringId(method.GetName());
    art::dex::TypeIndex method_return_idx =
        dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(method.GetReturnTypeDescriptor()));
    const auto* old_type_list = method.GetParameterTypeList();
    std::vector<art::dex::TypeIndex> new_type_list;
    for (uint32_t i = 0; old_type_list != nullptr && i < old_type_list->Size(); i++) {
      new_type_list.push_back(
          dex_file_->GetIndexForTypeId(
              *dex_file_->FindTypeId(
                  old_dex_file.GetTypeDescriptor(
                      old_dex_file.GetTypeId(
                          old_type_list->GetTypeItem(i).type_idx_)))));
    }
    const art::DexFile::ProtoId* proto_id = dex_file_->FindProtoId(method_return_idx,
                                                                   new_type_list);
    // TODO Return false, cleanup.
    CHECK(proto_id != nullptr || old_type_list == nullptr);
    const art::DexFile::MethodId* method_id = dex_file_->FindMethodId(declaring_class_id,
                                                                      *new_name_id,
                                                                      *proto_id);
    // TODO Return false, cleanup.
    CHECK(method_id != nullptr);
    uint32_t dex_method_idx = dex_file_->GetIndexForMethodId(*method_id);
    method.SetDexMethodIndex(dex_method_idx);
    linker->SetEntryPointsToInterpreter(&method);
    method.SetCodeItemOffset(dex_file_->FindCodeItemOffset(class_def, dex_method_idx));
    method.SetDexCacheResolvedMethods(new_dex_cache->GetResolvedMethods(), image_pointer_size);
    method.SetDexCacheResolvedTypes(new_dex_cache->GetResolvedTypes(), image_pointer_size);
    // Notify the jit that this method is redefined.
    art::jit::Jit* jit = runtime_->GetJit();
    if (jit != nullptr) {
      jit->GetCodeCache()->NotifyMethodRedefined(&method);
    }
  }
  return true;
}

bool Redefiner::UpdateFields(art::ObjPtr<art::mirror::Class> mclass) {
  // TODO The IFields & SFields pointers should be combined like the methods_ arrays were.
  for (auto fields_iter : {mclass->GetIFields(), mclass->GetSFields()}) {
    for (art::ArtField& field : fields_iter) {
      std::string declaring_class_name;
      const art::DexFile::TypeId* new_declaring_id =
          dex_file_->FindTypeId(field.GetDeclaringClass()->GetDescriptor(&declaring_class_name));
      const art::DexFile::StringId* new_name_id = dex_file_->FindStringId(field.GetName());
      const art::DexFile::TypeId* new_type_id = dex_file_->FindTypeId(field.GetTypeDescriptor());
      // TODO Handle error, cleanup.
      CHECK(new_name_id != nullptr && new_type_id != nullptr && new_declaring_id != nullptr);
      const art::DexFile::FieldId* new_field_id =
          dex_file_->FindFieldId(*new_declaring_id, *new_name_id, *new_type_id);
      CHECK(new_field_id != nullptr);
      // We only need to update the index since the other data in the ArtField cannot be updated.
      field.SetDexFieldIndex(dex_file_->GetIndexForFieldId(*new_field_id));
    }
  }
  return true;
}

// Performs updates to class that will allow us to verify it.
bool Redefiner::UpdateClass(art::ObjPtr<art::mirror::Class> mclass,
                            art::ObjPtr<art::mirror::DexCache> new_dex_cache) {
  const art::DexFile::ClassDef* class_def = art::OatFile::OatDexFile::FindClassDef(
      *dex_file_, class_sig_, art::ComputeModifiedUtf8Hash(class_sig_));
  if (class_def == nullptr) {
    RecordFailure(ERR(INVALID_CLASS_FORMAT), "Unable to find ClassDef!");
    return false;
  }
  if (!UpdateMethods(mclass, new_dex_cache, *class_def)) {
    // TODO Investigate appropriate error types.
    RecordFailure(ERR(INTERNAL), "Unable to update class methods.");
    return false;
  }
  if (!UpdateFields(mclass)) {
    // TODO Investigate appropriate error types.
    RecordFailure(ERR(INTERNAL), "Unable to update class fields.");
    return false;
  }

  // Update the class fields.
  // Need to update class last since the ArtMethod gets its DexFile from the class (which is needed
  // to call GetReturnTypeDescriptor and GetParameterTypeList above).
  mclass->SetDexCache(new_dex_cache.Ptr());
  mclass->SetDexClassDefIndex(dex_file_->GetIndexForClassDef(*class_def));
  mclass->SetDexTypeIndex(dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(class_sig_)));
  return true;
}

bool Redefiner::UpdateJavaDexFile(art::ObjPtr<art::mirror::Object> java_dex_file,
                                  art::ObjPtr<art::mirror::LongArray> new_cookie,
                                  /*out*/art::ObjPtr<art::mirror::LongArray>* original_cookie) {
  art::ArtField* internal_cookie_field = java_dex_file->GetClass()->FindDeclaredInstanceField(
      "mInternalCookie", "Ljava/lang/Object;");
  art::ArtField* cookie_field = java_dex_file->GetClass()->FindDeclaredInstanceField(
      "mCookie", "Ljava/lang/Object;");
  CHECK(internal_cookie_field != nullptr);
  art::ObjPtr<art::mirror::LongArray> orig_internal_cookie(
      internal_cookie_field->GetObject(java_dex_file)->AsLongArray());
  art::ObjPtr<art::mirror::LongArray> orig_cookie(
      cookie_field->GetObject(java_dex_file)->AsLongArray());
  internal_cookie_field->SetObject<false>(java_dex_file, new_cookie);
  *original_cookie = orig_internal_cookie;
  if (!orig_cookie.IsNull()) {
    cookie_field->SetObject<false>(java_dex_file, new_cookie);
  }
  return true;
}

// This function does all (java) allocations we need to do for the Class being redefined.
// TODO Change this name maybe?
bool Redefiner::EnsureClassAllocationsFinished() {
  art::StackHandleScope<2> hs(self_);
  art::Handle<art::mirror::Class> klass(hs.NewHandle(self_->DecodeJObject(klass_)->AsClass()));
  if (klass.Get() == nullptr) {
    RecordFailure(ERR(INVALID_CLASS), "Unable to decode class argument!");
    return false;
  }
  // Allocate the classExt
  art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->EnsureExtDataPresent(self_)));
  if (ext.Get() == nullptr) {
    // No memory. Clear exception (it's not useful) and return error.
    // TODO This doesn't need to be fatal. We could just not support obsolete methods after hitting
    // this case.
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate ClassExt");
    return false;
  }
  // Allocate the 2 arrays that make up the obsolete methods map.  Since the contents of the arrays
  // are only modified when all threads (other than the modifying one) are suspended we don't need
  // to worry about missing the unsyncronized writes to the array. We do synchronize when setting it
  // however, since that can happen at any time.
  // TODO Clear these after we walk the stacks in order to free them in the (likely?) event there
  // are no obsolete methods.
  {
    art::ObjectLock<art::mirror::ClassExt> lock(self_, ext);
    if (!ext->ExtendObsoleteArrays(
          self_, klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size())) {
      // OOM. Clear exception and return error.
      self_->AssertPendingOOMException();
      self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate/extend obsolete methods map");
      return false;
    }
  }
  return true;
}

}  // namespace openjdkjvmti
