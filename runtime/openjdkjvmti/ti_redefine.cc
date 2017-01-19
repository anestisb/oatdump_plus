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
#include "transform.h"

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
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps)
        : StackVisitor(thread,
                       /*context*/nullptr,
                       StackVisitor::StackWalkKind::kIncludeInlinedFrames),
          allocator_(allocator),
          obsoleted_methods_(obsoleted_methods),
          obsolete_maps_(obsolete_maps),
          is_runtime_frame_(false) {
  }

  ~ObsoleteMethodStackVisitor() OVERRIDE {}

 public:
  // Returns true if we successfully installed obsolete methods on this thread, filling
  // obsolete_maps_ with the translations if needed. Returns false and fills error_msg if we fail.
  // The stack is cleaned up when we fail.
  static void UpdateObsoleteFrames(
      art::Thread* thread,
      art::LinearAlloc* allocator,
      const std::unordered_set<art::ArtMethod*>& obsoleted_methods,
      /*out*/std::unordered_map<art::ArtMethod*, art::ArtMethod*>* obsolete_maps)
        REQUIRES(art::Locks::mutator_lock_) {
    ObsoleteMethodStackVisitor visitor(thread,
                                       allocator,
                                       obsoleted_methods,
                                       obsolete_maps);
    visitor.WalkStack();
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
        LOG(FATAL) << "Deoptimization failed due to runtime method in stack. See b/33616143";
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
        CHECK(method_storage != nullptr) << "Unable to allocate storage for obsolete version of '"
                                         << old_method->PrettyMethod() << "'";
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
  // TODO REMOVE once either current_method doesn't stick around through suspend points or deopt
  // works through runtime methods.
  bool is_runtime_frame_;
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
                                                         const unsigned char* dex_data,
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

Redefiner::ClassRedefinition::ClassRedefinition(Redefiner* driver,
                                                jclass klass,
                                                const art::DexFile* redefined_dex_file,
                                                const char* class_sig) :
      driver_(driver), klass_(klass), dex_file_(redefined_dex_file), class_sig_(class_sig) {
  GetMirrorClass()->MonitorEnter(driver_->self_);
}

Redefiner::ClassRedefinition::~ClassRedefinition() {
  if (driver_ != nullptr) {
    GetMirrorClass()->MonitorExit(driver_->self_);
  }
}

jvmtiError Redefiner::RedefineClasses(ArtJvmTiEnv* env,
                                      art::Runtime* runtime,
                                      art::Thread* self,
                                      jint class_count,
                                      const jvmtiClassDefinition* definitions,
                                      /*out*/std::string* error_msg) {
  if (env == nullptr) {
    *error_msg = "env was null!";
    return ERR(INVALID_ENVIRONMENT);
  } else if (class_count < 0) {
    *error_msg = "class_count was less then 0";
    return ERR(ILLEGAL_ARGUMENT);
  } else if (class_count == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  } else if (definitions == nullptr) {
    *error_msg = "null definitions!";
    return ERR(NULL_POINTER);
  }
  std::vector<ArtClassDefinition> def_vector;
  def_vector.reserve(class_count);
  for (jint i = 0; i < class_count; i++) {
    ArtClassDefinition def;
    def.dex_len = definitions[i].class_byte_count;
    def.dex_data = MakeJvmtiUniquePtr(env, const_cast<unsigned char*>(definitions[i].class_bytes));
    // We are definitely modified.
    def.modified = true;
    jvmtiError res = Transformer::FillInTransformationData(env, definitions[i].klass, &def);
    if (res != OK) {
      return res;
    }
    def_vector.push_back(std::move(def));
  }
  // Call all the transformation events.
  jvmtiError res = Transformer::RetransformClassesDirect(env,
                                                         self,
                                                         &def_vector);
  if (res != OK) {
    // Something went wrong with transformation!
    return res;
  }
  return RedefineClassesDirect(env, runtime, self, def_vector, error_msg);
}

jvmtiError Redefiner::RedefineClassesDirect(ArtJvmTiEnv* env,
                                            art::Runtime* runtime,
                                            art::Thread* self,
                                            const std::vector<ArtClassDefinition>& definitions,
                                            std::string* error_msg) {
  DCHECK(env != nullptr);
  if (definitions.size() == 0) {
    // We don't actually need to do anything. Just return OK.
    return OK;
  }
  // Stop JIT for the duration of this redefine since the JIT might concurrently compile a method we
  // are going to redefine.
  art::jit::ScopedJitSuspend suspend_jit;
  // Get shared mutator lock so we can lock all the classes.
  art::ScopedObjectAccess soa(self);
  std::vector<Redefiner::ClassRedefinition> redefinitions;
  redefinitions.reserve(definitions.size());
  Redefiner r(runtime, self, error_msg);
  for (const ArtClassDefinition& def : definitions) {
    // Only try to transform classes that have been modified.
    if (def.modified) {
      jvmtiError res = r.AddRedefinition(env, def);
      if (res != OK) {
        return res;
      }
    }
  }
  return r.Run();
}

jvmtiError Redefiner::AddRedefinition(ArtJvmTiEnv* env, const ArtClassDefinition& def) {
  std::string original_dex_location;
  jvmtiError ret = OK;
  if ((ret = GetClassLocation(env, def.klass, &original_dex_location))) {
    *error_msg_ = "Unable to get original dex file location!";
    return ret;
  }
  char* generic_ptr_unused = nullptr;
  char* signature_ptr = nullptr;
  if ((ret = env->GetClassSignature(def.klass, &signature_ptr, &generic_ptr_unused)) != OK) {
    *error_msg_ = "Unable to get class signature!";
    return ret;
  }
  JvmtiUniquePtr generic_unique_ptr(MakeJvmtiUniquePtr(env, generic_ptr_unused));
  JvmtiUniquePtr signature_unique_ptr(MakeJvmtiUniquePtr(env, signature_ptr));
  std::unique_ptr<art::MemMap> map(MoveDataToMemMap(original_dex_location,
                                                    def.dex_len,
                                                    def.dex_data.get(),
                                                    error_msg_));
  std::ostringstream os;
  if (map.get() == nullptr) {
    os << "Failed to create anonymous mmap for modified dex file of class " << def.name
       << "in dex file " << original_dex_location << " because: " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(OUT_OF_MEMORY);
  }
  if (map->Size() < sizeof(art::DexFile::Header)) {
    *error_msg_ = "Could not read dex file header because dex_data was too short";
    return ERR(INVALID_CLASS_FORMAT);
  }
  uint32_t checksum = reinterpret_cast<const art::DexFile::Header*>(map->Begin())->checksum_;
  std::unique_ptr<const art::DexFile> dex_file(art::DexFile::Open(map->GetName(),
                                                                  checksum,
                                                                  std::move(map),
                                                                  /*verify*/true,
                                                                  /*verify_checksum*/true,
                                                                  error_msg_));
  if (dex_file.get() == nullptr) {
    os << "Unable to load modified dex file for " << def.name << ": " << *error_msg_;
    *error_msg_ = os.str();
    return ERR(INVALID_CLASS_FORMAT);
  }
  redefinitions_.push_back(
      Redefiner::ClassRedefinition(this, def.klass, dex_file.release(), signature_ptr));
  return OK;
}

// TODO *MAJOR* This should return the actual source java.lang.DexFile object for the klass.
// TODO Make mirror of DexFile and associated types to make this less hellish.
// TODO Make mirror of BaseDexClassLoader and associated types to make this less hellish.
art::mirror::Object* Redefiner::ClassRedefinition::FindSourceDexFileObject(
    art::Handle<art::mirror::ClassLoader> loader) {
  const char* dex_path_list_element_array_name = "[Ldalvik/system/DexPathList$Element;";
  const char* dex_path_list_element_name = "Ldalvik/system/DexPathList$Element;";
  const char* dex_file_name = "Ldalvik/system/DexFile;";
  const char* dex_path_list_name = "Ldalvik/system/DexPathList;";
  const char* dex_class_loader_name = "Ldalvik/system/BaseDexClassLoader;";

  CHECK(!driver_->self_->IsExceptionPending());
  art::StackHandleScope<11> hs(driver_->self_);
  art::ClassLinker* class_linker = driver_->runtime_->GetClassLinker();

  art::Handle<art::mirror::ClassLoader> null_loader(hs.NewHandle<art::mirror::ClassLoader>(
      nullptr));
  art::Handle<art::mirror::Class> base_dex_loader_class(hs.NewHandle(class_linker->FindClass(
      driver_->self_, dex_class_loader_name, null_loader)));

  // Get all the ArtFields so we can look in the BaseDexClassLoader
  art::ArtField* path_list_field = base_dex_loader_class->FindDeclaredInstanceField(
      "pathList", dex_path_list_name);
  CHECK(path_list_field != nullptr);

  art::ArtField* dex_path_list_element_field =
      class_linker->FindClass(driver_->self_, dex_path_list_name, null_loader)
        ->FindDeclaredInstanceField("dexElements", dex_path_list_element_array_name);
  CHECK(dex_path_list_element_field != nullptr);

  art::ArtField* element_dex_file_field =
      class_linker->FindClass(driver_->self_, dex_path_list_element_name, null_loader)
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
  CHECK(!driver_->self_->IsExceptionPending());
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> dex_elements_list(hs.NewHandle(
      dex_path_list_element_field->GetObject(path_list.Get())->
      AsObjectArray<art::mirror::Object>()));
  CHECK(!driver_->self_->IsExceptionPending());
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
    CHECK(!driver_->self_->IsExceptionPending());
    CHECK(dex_elements_list.Get() != nullptr);
    CHECK_EQ(current_element->GetClass(), class_linker->FindClass(driver_->self_,
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

art::mirror::Class* Redefiner::ClassRedefinition::GetMirrorClass() {
  return driver_->self_->DecodeJObject(klass_)->AsClass();
}

art::mirror::ClassLoader* Redefiner::ClassRedefinition::GetClassLoader() {
  return GetMirrorClass()->GetClassLoader();
}

art::mirror::DexCache* Redefiner::ClassRedefinition::CreateNewDexCache(
    art::Handle<art::mirror::ClassLoader> loader) {
  return driver_->runtime_->GetClassLinker()->RegisterDexFile(*dex_file_, loader.Get());
}

// TODO Really wishing I had that mirror of java.lang.DexFile now.
art::mirror::LongArray* Redefiner::ClassRedefinition::AllocateDexFileCookie(
    art::Handle<art::mirror::Object> java_dex_file_obj) {
  art::StackHandleScope<2> hs(driver_->self_);
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
      hs.NewHandle(art::mirror::LongArray::Alloc(driver_->self_, cookie->GetLength() + 1)));
  if (new_cookie.Get() == nullptr) {
    driver_->self_->AssertPendingOOMException();
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

void Redefiner::RecordFailure(jvmtiError result,
                              const std::string& class_sig,
                              const std::string& error_msg) {
  *error_msg_ = StringPrintf("Unable to perform redefinition of '%s': %s",
                             class_sig.c_str(),
                             error_msg.c_str());
  result_ = result;
}

bool Redefiner::ClassRedefinition::FinishRemainingAllocations(
    /*out*/art::MutableHandle<art::mirror::ClassLoader>* source_class_loader,
    /*out*/art::MutableHandle<art::mirror::Object>* java_dex_file_obj,
    /*out*/art::MutableHandle<art::mirror::LongArray>* new_dex_file_cookie,
    /*out*/art::MutableHandle<art::mirror::DexCache>* new_dex_cache) {
  art::StackHandleScope<4> hs(driver_->self_);
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
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate dex file array for class loader");
    return false;
  }
  art::Handle<art::mirror::DexCache> dex_cache(hs.NewHandle(CreateNewDexCache(loader)));
  if (dex_cache.Get() == nullptr) {
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
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
  art::LinearAlloc* allocator;
  std::unordered_map<art::ArtMethod*, art::ArtMethod*> obsolete_map;
  std::unordered_set<art::ArtMethod*> obsolete_methods;

  explicit CallbackCtx(art::LinearAlloc* alloc) : allocator(alloc) {}
};

void DoAllocateObsoleteMethodsCallback(art::Thread* t, void* vdata) NO_THREAD_SAFETY_ANALYSIS {
  CallbackCtx* data = reinterpret_cast<CallbackCtx*>(vdata);
  ObsoleteMethodStackVisitor::UpdateObsoleteFrames(t,
                                                   data->allocator,
                                                   data->obsolete_methods,
                                                   &data->obsolete_map);
}

// This creates any ArtMethod* structures needed for obsolete methods and ensures that the stack is
// updated so they will be run.
// TODO Rewrite so we can do this only once regardless of how many redefinitions there are.
void Redefiner::ClassRedefinition::FindAndAllocateObsoleteMethods(art::mirror::Class* art_klass) {
  art::ScopedAssertNoThreadSuspension ns("No thread suspension during thread stack walking");
  art::mirror::ClassExt* ext = art_klass->GetExtData();
  CHECK(ext->GetObsoleteMethods() != nullptr);
  CallbackCtx ctx(art_klass->GetClassLoader()->GetAllocator());
  // Add all the declared methods to the map
  for (auto& m : art_klass->GetDeclaredMethods(art::kRuntimePointerSize)) {
    ctx.obsolete_methods.insert(&m);
    // TODO Allow this or check in IsModifiableClass.
    DCHECK(!m.IsIntrinsic());
  }
  {
    art::MutexLock mu(driver_->self_, *art::Locks::thread_list_lock_);
    art::ThreadList* list = art::Runtime::Current()->GetThreadList();
    list->ForEach(DoAllocateObsoleteMethodsCallback, static_cast<void*>(&ctx));
  }
  FillObsoleteMethodMap(art_klass, ctx.obsolete_map);
}

// Fills the obsolete method map in the art_klass's extData. This is so obsolete methods are able to
// figure out their DexCaches.
void Redefiner::ClassRedefinition::FillObsoleteMethodMap(
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

bool Redefiner::ClassRedefinition::CheckClass() {
  // TODO Might just want to put it in a ObjPtr and NoSuspend assert.
  art::StackHandleScope<1> hs(driver_->self_);
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
  LOG(WARNING) << "Bytecodes of redefinitions are not verified.";

  return true;
}

// TODO Move this to use IsRedefinable when that function is made.
bool Redefiner::ClassRedefinition::CheckRedefinable() {
  std::string err;
  art::StackHandleScope<1> hs(driver_->self_);

  art::Handle<art::mirror::Class> h_klass(hs.NewHandle(GetMirrorClass()));
  jvmtiError res = Redefiner::GetClassRedefinitionError(h_klass, &err);
  if (res != OK) {
    RecordFailure(res, err);
    return false;
  } else {
    return true;
  }
}

bool Redefiner::ClassRedefinition::CheckRedefinitionIsValid() {
  return CheckRedefinable() &&
      CheckClass() &&
      CheckSameFields() &&
      CheckSameMethods();
}

// A wrapper that lets us hold onto the arbitrary sized data needed for redefinitions in a
// reasonably sane way. This adds no fields to the normal ObjectArray. By doing this we can avoid
// having to deal with the fact that we need to hold an arbitrary number of references live.
class RedefinitionDataHolder {
 public:
  enum DataSlot : int32_t {
    kSlotSourceClassLoader = 0,
    kSlotJavaDexFile = 1,
    kSlotNewDexFileCookie = 2,
    kSlotNewDexCache = 3,
    kSlotMirrorClass = 4,

    // Must be last one.
    kNumSlots = 5,
  };

  // This needs to have a HandleScope passed in that is capable of creating a new Handle without
  // overflowing. Only one handle will be created. This object has a lifetime identical to that of
  // the passed in handle-scope.
  RedefinitionDataHolder(art::StackHandleScope<1>* hs,
                         art::Runtime* runtime,
                         art::Thread* self,
                         int32_t num_redefinitions) REQUIRES_SHARED(art::Locks::mutator_lock_) :
    arr_(
      hs->NewHandle(
        art::mirror::ObjectArray<art::mirror::Object>::Alloc(
            self,
            runtime->GetClassLinker()->GetClassRoot(art::ClassLinker::kObjectArrayClass),
            num_redefinitions * kNumSlots))) {}

  bool IsNull() const REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_.IsNull();
  }

  // TODO Maybe make an iterable view type to simplify using this.
  art::mirror::ClassLoader* GetSourceClassLoader(jint klass_index)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::ClassLoader*>(GetSlot(klass_index, kSlotSourceClassLoader));
  }
  art::mirror::Object* GetJavaDexFile(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return GetSlot(klass_index, kSlotJavaDexFile);
  }
  art::mirror::LongArray* GetNewDexFileCookie(jint klass_index)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::LongArray*>(GetSlot(klass_index, kSlotNewDexFileCookie));
  }
  art::mirror::DexCache* GetNewDexCache(jint klass_index)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::DexCache*>(GetSlot(klass_index, kSlotNewDexCache));
  }
  art::mirror::Class* GetMirrorClass(jint klass_index) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return art::down_cast<art::mirror::Class*>(GetSlot(klass_index, kSlotMirrorClass));
  }

  void SetSourceClassLoader(jint klass_index, art::mirror::ClassLoader* loader)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotSourceClassLoader, loader);
  }
  void SetJavaDexFile(jint klass_index, art::mirror::Object* dexfile)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotJavaDexFile, dexfile);
  }
  void SetNewDexFileCookie(jint klass_index, art::mirror::LongArray* cookie)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexFileCookie, cookie);
  }
  void SetNewDexCache(jint klass_index, art::mirror::DexCache* cache)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotNewDexCache, cache);
  }
  void SetMirrorClass(jint klass_index, art::mirror::Class* klass)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    SetSlot(klass_index, kSlotMirrorClass, klass);
  }

  int32_t Length() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    return arr_->GetLength() / kNumSlots;
  }

 private:
  art::Handle<art::mirror::ObjectArray<art::mirror::Object>> arr_;

  art::mirror::Object* GetSlot(jint klass_index,
                               DataSlot slot) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK_LT(klass_index, Length());
    return arr_->Get((kNumSlots * klass_index) + slot);
  }

  void SetSlot(jint klass_index,
               DataSlot slot,
               art::ObjPtr<art::mirror::Object> obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    DCHECK(!art::Runtime::Current()->IsActiveTransaction());
    DCHECK_LT(klass_index, Length());
    arr_->Set<false>((kNumSlots * klass_index) + slot, obj);
  }

  DISALLOW_COPY_AND_ASSIGN(RedefinitionDataHolder);
};

bool Redefiner::CheckAllRedefinitionAreValid() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.CheckRedefinitionIsValid()) {
      return false;
    }
  }
  return true;
}

bool Redefiner::EnsureAllClassAllocationsFinished() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    if (!redef.EnsureClassAllocationsFinished()) {
      return false;
    }
  }
  return true;
}

bool Redefiner::FinishAllRemainingAllocations(RedefinitionDataHolder& holder) {
  int32_t cnt = 0;
  art::StackHandleScope<4> hs(self_);
  art::MutableHandle<art::mirror::Object> java_dex_file(hs.NewHandle<art::mirror::Object>(nullptr));
  art::MutableHandle<art::mirror::ClassLoader> source_class_loader(
      hs.NewHandle<art::mirror::ClassLoader>(nullptr));
  art::MutableHandle<art::mirror::LongArray> new_dex_file_cookie(
      hs.NewHandle<art::mirror::LongArray>(nullptr));
  art::MutableHandle<art::mirror::DexCache> new_dex_cache(
      hs.NewHandle<art::mirror::DexCache>(nullptr));
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    // Reset the out pointers to null
    source_class_loader.Assign(nullptr);
    java_dex_file.Assign(nullptr);
    new_dex_file_cookie.Assign(nullptr);
    new_dex_cache.Assign(nullptr);
    // Allocate the data this redefinition requires.
    if (!redef.FinishRemainingAllocations(&source_class_loader,
                                          &java_dex_file,
                                          &new_dex_file_cookie,
                                          &new_dex_cache)) {
      return false;
    }
    // Save the allocated data into the holder.
    holder.SetSourceClassLoader(cnt, source_class_loader.Get());
    holder.SetJavaDexFile(cnt, java_dex_file.Get());
    holder.SetNewDexFileCookie(cnt, new_dex_file_cookie.Get());
    holder.SetNewDexCache(cnt, new_dex_cache.Get());
    holder.SetMirrorClass(cnt, redef.GetMirrorClass());
    cnt++;
  }
  return true;
}

void Redefiner::ClassRedefinition::ReleaseDexFile() {
  dex_file_.release();
}

void Redefiner::ReleaseAllDexFiles() {
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    redef.ReleaseDexFile();
  }
}

jvmtiError Redefiner::Run() {
  art::StackHandleScope<1> hs(self_);
  // Allocate an array to hold onto all java temporary objects associated with this redefinition.
  // We will let this be collected after the end of this function.
  RedefinitionDataHolder holder(&hs, runtime_, self_, redefinitions_.size());
  if (holder.IsNull()) {
    self_->AssertPendingOOMException();
    self_->ClearException();
    RecordFailure(ERR(OUT_OF_MEMORY), "Could not allocate storage for temporaries");
    return result_;
  }

  // First we just allocate the ClassExt and its fields that we need. These can be updated
  // atomically without any issues (since we allocate the map arrays as empty) so we don't bother
  // doing a try loop. The other allocations we need to ensure that nothing has changed in the time
  // between allocating them and pausing all threads before we can update them so we need to do a
  // try loop.
  if (!CheckAllRedefinitionAreValid() ||
      !EnsureAllClassAllocationsFinished() ||
      !FinishAllRemainingAllocations(holder)) {
    // TODO Null out the ClassExt fields we allocated (if possible, might be racing with another
    // redefineclass call which made it even bigger. Leak shouldn't be huge (2x array of size
    // declared_methods_.length) but would be good to get rid of. All other allocations should be
    // cleaned up by the GC eventually.
    return result_;
  }
  // Disable GC and wait for it to be done if we are a moving GC.  This is fine since we are done
  // allocating so no deadlocks.
  art::gc::Heap* heap = runtime_->GetHeap();
  if (heap->IsGcConcurrentAndMoving()) {
    // GC moving objects can cause deadlocks as we are deoptimizing the stack.
    heap->IncrementDisableMovingGC(self_);
  }
  // Do transition to final suspension
  // TODO We might want to give this its own suspended state!
  // TODO This isn't right. We need to change state without any chance of suspend ideally!
  self_->TransitionFromRunnableToSuspended(art::ThreadState::kNative);
  runtime_->GetThreadList()->SuspendAll(
      "Final installation of redefined Classes!", /*long_suspend*/true);
  // TODO We need to invalidate all breakpoints in the redefined class with the debugger.
  // TODO We need to deal with any instrumentation/debugger deoptimized_methods_.
  // TODO We need to update all debugger MethodIDs so they note the method they point to is
  // obsolete or implement some other well defined semantics.
  // TODO We need to decide on & implement semantics for JNI jmethodids when we redefine methods.
  int32_t cnt = 0;
  for (Redefiner::ClassRedefinition& redef : redefinitions_) {
    art::mirror::Class* klass = holder.GetMirrorClass(cnt);
    redef.UpdateJavaDexFile(holder.GetJavaDexFile(cnt), holder.GetNewDexFileCookie(cnt));
    // TODO Rewrite so we don't do a stack walk for each and every class.
    redef.FindAndAllocateObsoleteMethods(klass);
    redef.UpdateClass(klass, holder.GetNewDexCache(cnt));
    cnt++;
  }
  // Ensure that obsolete methods are deoptimized. This is needed since optimized methods may have
  // pointers to their ArtMethod's stashed in registers that they then use to attempt to hit the
  // DexCache. (b/33630159)
  // TODO This can fail (leave some methods optimized) near runtime methods (including
  // quick-to-interpreter transition function).
  // TODO We probably don't need this at all once we have a way to ensure that the
  // current_art_method is never stashed in a (physical) register by the JIT and lost to the
  // stack-walker.
  EnsureObsoleteMethodsAreDeoptimized();
  // TODO Verify the new Class.
  // TODO Shrink the obsolete method maps if possible?
  // TODO find appropriate class loader.
  // TODO Put this into a scoped thing.
  runtime_->GetThreadList()->ResumeAll();
  // Get back shared mutator lock as expected for return.
  self_->TransitionFromSuspendedToRunnable();
  // TODO Do the dex_file release at a more reasonable place. This works but it muddles who really
  // owns the DexFile and when ownership is transferred.
  ReleaseAllDexFiles();
  if (heap->IsGcConcurrentAndMoving()) {
    heap->DecrementDisableMovingGC(self_);
  }
  return OK;
}

void Redefiner::ClassRedefinition::UpdateMethods(art::ObjPtr<art::mirror::Class> mclass,
                                                 art::ObjPtr<art::mirror::DexCache> new_dex_cache,
                                                 const art::DexFile::ClassDef& class_def) {
  art::ClassLinker* linker = driver_->runtime_->GetClassLinker();
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
    // Notify the jit that this method is redefined.
    art::jit::Jit* jit = driver_->runtime_->GetJit();
    if (jit != nullptr) {
      jit->GetCodeCache()->NotifyMethodRedefined(&method);
    }
  }
}

void Redefiner::ClassRedefinition::UpdateFields(art::ObjPtr<art::mirror::Class> mclass) {
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
}

// Performs updates to class that will allow us to verify it.
void Redefiner::ClassRedefinition::UpdateClass(art::ObjPtr<art::mirror::Class> mclass,
                                               art::ObjPtr<art::mirror::DexCache> new_dex_cache) {
  DCHECK_EQ(dex_file_->NumClassDefs(), 1u);
  const art::DexFile::ClassDef& class_def = dex_file_->GetClassDef(0);
  UpdateMethods(mclass, new_dex_cache, class_def);
  UpdateFields(mclass);

  // Update the class fields.
  // Need to update class last since the ArtMethod gets its DexFile from the class (which is needed
  // to call GetReturnTypeDescriptor and GetParameterTypeList above).
  mclass->SetDexCache(new_dex_cache.Ptr());
  mclass->SetDexClassDefIndex(dex_file_->GetIndexForClassDef(class_def));
  mclass->SetDexTypeIndex(dex_file_->GetIndexForTypeId(*dex_file_->FindTypeId(class_sig_.c_str())));
}

void Redefiner::ClassRedefinition::UpdateJavaDexFile(
    art::ObjPtr<art::mirror::Object> java_dex_file,
    art::ObjPtr<art::mirror::LongArray> new_cookie) {
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
  if (!orig_cookie.IsNull()) {
    cookie_field->SetObject<false>(java_dex_file, new_cookie);
  }
}

// This function does all (java) allocations we need to do for the Class being redefined.
// TODO Change this name maybe?
bool Redefiner::ClassRedefinition::EnsureClassAllocationsFinished() {
  art::StackHandleScope<2> hs(driver_->self_);
  art::Handle<art::mirror::Class> klass(hs.NewHandle(
      driver_->self_->DecodeJObject(klass_)->AsClass()));
  if (klass.Get() == nullptr) {
    RecordFailure(ERR(INVALID_CLASS), "Unable to decode class argument!");
    return false;
  }
  // Allocate the classExt
  art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->EnsureExtDataPresent(driver_->self_)));
  if (ext.Get() == nullptr) {
    // No memory. Clear exception (it's not useful) and return error.
    // TODO This doesn't need to be fatal. We could just not support obsolete methods after hitting
    // this case.
    driver_->self_->AssertPendingOOMException();
    driver_->self_->ClearException();
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
    art::ObjectLock<art::mirror::ClassExt> lock(driver_->self_, ext);
    if (!ext->ExtendObsoleteArrays(
          driver_->self_, klass->GetDeclaredMethodsSlice(art::kRuntimePointerSize).size())) {
      // OOM. Clear exception and return error.
      driver_->self_->AssertPendingOOMException();
      driver_->self_->ClearException();
      RecordFailure(ERR(OUT_OF_MEMORY), "Unable to allocate/extend obsolete methods map");
      return false;
    }
  }
  return true;
}

}  // namespace openjdkjvmti
