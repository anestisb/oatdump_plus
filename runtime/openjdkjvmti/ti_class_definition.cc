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

#include "ti_class_definition.h"

#include "base/array_slice.h"
#include "class_linker-inl.h"
#include "dex_file.h"
#include "fixed_up_dex_file.h"
#include "handle_scope-inl.h"
#include "handle.h"
#include "mirror/class_ext.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "reflection.h"
#include "thread.h"

namespace openjdkjvmti {

bool ArtClassDefinition::IsModified() const {
  // RedefineClasses calls always are 'modified' since they need to change the original_dex_file of
  // the class.
  if (redefined_) {
    return true;
  }
  // Check if the dex file we want to set is the same as the current one.
  // Unfortunately we need to do this check even if no modifications have been done since it could
  // be that agents were removed in the mean-time so we still have a different dex file. The dex
  // checksum means this is likely to be fairly fast.
  return static_cast<jint>(original_dex_file_.size()) != dex_len_ ||
      memcmp(&original_dex_file_.At(0), dex_data_.get(), dex_len_) != 0;
}

jvmtiError ArtClassDefinition::InitCommon(ArtJvmTiEnv* env, jclass klass) {
  JNIEnv* jni_env = GetJniEnv(env);
  if (jni_env == nullptr) {
    return ERR(INTERNAL);
  }
  art::ScopedObjectAccess soa(jni_env);
  art::ObjPtr<art::mirror::Class> m_klass(soa.Decode<art::mirror::Class>(klass));
  if (m_klass.IsNull()) {
    return ERR(INVALID_CLASS);
  }
  klass_ = klass;
  loader_ = soa.AddLocalReference<jobject>(m_klass->GetClassLoader());
  std::string descriptor_store;
  std::string descriptor(m_klass->GetDescriptor(&descriptor_store));
  name_ = descriptor.substr(1, descriptor.size() - 2);
  // Android doesn't really have protection domains.
  protection_domain_ = nullptr;
  return OK;
}

// Gets the data surrounding the given class.
static jvmtiError GetDexDataForRetransformation(ArtJvmTiEnv* env,
                                                art::Handle<art::mirror::Class> klass,
                                                /*out*/jint* dex_data_len,
                                                /*out*/unsigned char** dex_data)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  art::StackHandleScope<3> hs(art::Thread::Current());
  art::Handle<art::mirror::ClassExt> ext(hs.NewHandle(klass->GetExtData()));
  const art::DexFile* dex_file = nullptr;
  if (!ext.IsNull()) {
    art::Handle<art::mirror::Object> orig_dex(hs.NewHandle(ext->GetOriginalDexFile()));
    if (!orig_dex.IsNull()) {
      if (orig_dex->IsArrayInstance()) {
        DCHECK(orig_dex->GetClass()->GetComponentType()->IsPrimitiveByte());
        art::Handle<art::mirror::ByteArray> orig_dex_bytes(
            hs.NewHandle(art::down_cast<art::mirror::ByteArray*>(orig_dex->AsArray())));
        *dex_data_len = static_cast<jint>(orig_dex_bytes->GetLength());
        return CopyDataIntoJvmtiBuffer(
            env,
            reinterpret_cast<const unsigned char*>(orig_dex_bytes->GetData()),
            *dex_data_len,
            /*out*/dex_data);
      } else if (orig_dex->IsDexCache()) {
        dex_file = orig_dex->AsDexCache()->GetDexFile();
      } else {
        DCHECK(orig_dex->GetClass()->DescriptorEquals("Ljava/lang/Long;"))
            << "Expected java/lang/Long but found object of type "
            << orig_dex->GetClass()->PrettyClass();
        art::ObjPtr<art::mirror::Class> prim_long_class(
            art::Runtime::Current()->GetClassLinker()->GetClassRoot(
                art::ClassLinker::kPrimitiveLong));
        art::JValue val;
        if (!art::UnboxPrimitiveForResult(orig_dex.Get(), prim_long_class, &val)) {
          // This should never happen.
          return ERR(INTERNAL);
        }
        dex_file = reinterpret_cast<const art::DexFile*>(static_cast<uintptr_t>(val.GetJ()));
      }
    }
  }
  if (dex_file == nullptr) {
    dex_file = &klass->GetDexFile();
  }
  std::unique_ptr<FixedUpDexFile> fixed_dex_file(FixedUpDexFile::Create(*dex_file));
  *dex_data_len = static_cast<jint>(fixed_dex_file->Size());
  return CopyDataIntoJvmtiBuffer(env,
                                 fixed_dex_file->Begin(),
                                 fixed_dex_file->Size(),
                                 /*out*/dex_data);
}

jvmtiError ArtClassDefinition::Init(ArtJvmTiEnv* env, jclass klass) {
  jvmtiError res = InitCommon(env, klass);
  if (res != OK) {
    return res;
  }
  unsigned char* new_data = nullptr;
  art::Thread* self = art::Thread::Current();
  art::ScopedObjectAccess soa(self);
  art::StackHandleScope<1> hs(self);
  art::Handle<art::mirror::Class> m_klass(hs.NewHandle(self->DecodeJObject(klass)->AsClass()));
  res = GetDexDataForRetransformation(env, m_klass, &dex_len_, &new_data);
  if (res != OK) {
    return res;
  }
  dex_data_ = MakeJvmtiUniquePtr(env, new_data);
  if (m_klass->GetExtData() == nullptr || m_klass->GetExtData()->GetOriginalDexFile() == nullptr) {
    // We have never redefined class this yet. Keep track of what the (de-quickened) dex file looks
    // like so we can tell if anything has changed. Really we would like to just always do the
    // 'else' block but the fact that we de-quickened stuff screws us over.
    unsigned char* original_data_memory = nullptr;
    res = CopyDataIntoJvmtiBuffer(env, dex_data_.get(), dex_len_, &original_data_memory);
    original_dex_file_memory_ = MakeJvmtiUniquePtr(env, original_data_memory);
    original_dex_file_ = art::ArraySlice<const unsigned char>(original_data_memory, dex_len_);
  } else {
    // We know that we have been redefined at least once (there is an original_dex_file set in
    // the class) so we can just use the current dex file directly.
    const art::DexFile& dex_file = m_klass->GetDexFile();
    original_dex_file_ = art::ArraySlice<const unsigned char>(dex_file.Begin(), dex_file.Size());
  }
  return res;
}

jvmtiError ArtClassDefinition::Init(ArtJvmTiEnv* env, const jvmtiClassDefinition& def) {
  jvmtiError res = InitCommon(env, def.klass);
  if (res != OK) {
    return res;
  }
  unsigned char* new_data = nullptr;
  original_dex_file_ = art::ArraySlice<const unsigned char>(def.class_bytes, def.class_byte_count);
  redefined_ = true;
  dex_len_ = def.class_byte_count;
  res = CopyDataIntoJvmtiBuffer(env, def.class_bytes, def.class_byte_count, /*out*/ &new_data);
  dex_data_ = MakeJvmtiUniquePtr(env, new_data);
  return res;
}

}  // namespace openjdkjvmti
