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

#include <gtest/gtest.h>

#include "base/unix_file/fd_file.h"
#include "art_method-inl.h"
#include "class_linker-inl.h"
#include "common_runtime_test.h"
#include "dex_file.h"
#include "method_reference.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"
#include "handle_scope-inl.h"
#include "linear_alloc.h"
#include "jit/profile_compilation_info.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

class ProfileCompilationInfoTest : public CommonRuntimeTest {
 public:
  void PostRuntimeCreate() OVERRIDE {
    arena_.reset(new ArenaAllocator(Runtime::Current()->GetArenaPool()));
  }

 protected:
  std::vector<ArtMethod*> GetVirtualMethods(jobject class_loader,
                                            const std::string& clazz) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    StackHandleScope<1> hs(self);
    Handle<mirror::ClassLoader> h_loader(
        hs.NewHandle(self->DecodeJObject(class_loader)->AsClassLoader()));
    mirror::Class* klass = class_linker->FindClass(self, clazz.c_str(), h_loader);

    const auto pointer_size = class_linker->GetImagePointerSize();
    std::vector<ArtMethod*> methods;
    for (auto& m : klass->GetVirtualMethods(pointer_size)) {
      methods.push_back(&m);
    }
    return methods;
  }

  bool AddMethod(const std::string& dex_location,
                 uint32_t checksum,
                 uint16_t method_index,
                 ProfileCompilationInfo* info) {
    return info->AddMethodIndex(dex_location, checksum, method_index);
  }

  bool AddMethod(const std::string& dex_location,
                 uint32_t checksum,
                 uint16_t method_index,
                 const ProfileCompilationInfo::OfflineProfileMethodInfo& pmi,
                 ProfileCompilationInfo* info) {
    return info->AddMethod(dex_location, checksum, method_index, pmi);
  }

  bool AddClass(const std::string& dex_location,
                uint32_t checksum,
                uint16_t class_index,
                ProfileCompilationInfo* info) {
    return info->AddMethodIndex(dex_location, checksum, class_index);
  }

  uint32_t GetFd(const ScratchFile& file) {
    return static_cast<uint32_t>(file.GetFd());
  }

  bool SaveProfilingInfo(
      const std::string& filename,
      const std::vector<ArtMethod*>& methods,
      const std::set<DexCacheResolvedClasses>& resolved_classes) {
    ProfileCompilationInfo info;
    std::vector<ProfileMethodInfo> profile_methods;
    ScopedObjectAccess soa(Thread::Current());
    for (ArtMethod* method : methods) {
      profile_methods.emplace_back(method->GetDexFile(), method->GetDexMethodIndex());
    }
    if (!info.AddMethodsAndClasses(profile_methods, resolved_classes)) {
      return false;
    }
    if (info.GetNumberOfMethods() != profile_methods.size()) {
      return false;
    }
    ProfileCompilationInfo file_profile;
    if (!file_profile.Load(filename, false)) {
      return false;
    }
    if (!info.MergeWith(file_profile)) {
      return false;
    }

    return info.Save(filename, nullptr);
  }

  // Saves the given art methods to a profile backed by 'filename' and adds
  // some fake inline caches to it. The added inline caches are returned in
  // the out map `profile_methods_map`.
  bool SaveProfilingInfoWithFakeInlineCaches(
      const std::string& filename,
      const std::vector<ArtMethod*>& methods,
      /*out*/ SafeMap<ArtMethod*, ProfileMethodInfo>* profile_methods_map) {
    ProfileCompilationInfo info;
    std::vector<ProfileMethodInfo> profile_methods;
    ScopedObjectAccess soa(Thread::Current());
    for (ArtMethod* method : methods) {
      std::vector<ProfileMethodInfo::ProfileInlineCache> caches;
      // Monomorphic
      for (uint16_t dex_pc = 0; dex_pc < 11; dex_pc++) {
        std::vector<ProfileMethodInfo::ProfileClassReference> classes;
        classes.emplace_back(method->GetDexFile(), dex::TypeIndex(0));
        caches.emplace_back(dex_pc, /*is_missing_types*/false, classes);
      }
      // Polymorphic
      for (uint16_t dex_pc = 11; dex_pc < 22; dex_pc++) {
        std::vector<ProfileMethodInfo::ProfileClassReference> classes;
        for (uint16_t k = 0; k < InlineCache::kIndividualCacheSize / 2; k++) {
          classes.emplace_back(method->GetDexFile(), dex::TypeIndex(k));
        }
        caches.emplace_back(dex_pc, /*is_missing_types*/false, classes);
      }
      // Megamorphic
      for (uint16_t dex_pc = 22; dex_pc < 33; dex_pc++) {
        std::vector<ProfileMethodInfo::ProfileClassReference> classes;
        for (uint16_t k = 0; k < 2 * InlineCache::kIndividualCacheSize; k++) {
          classes.emplace_back(method->GetDexFile(), dex::TypeIndex(k));
        }
        caches.emplace_back(dex_pc, /*is_missing_types*/false, classes);
      }
      // Missing types
      for (uint16_t dex_pc = 33; dex_pc < 44; dex_pc++) {
        std::vector<ProfileMethodInfo::ProfileClassReference> classes;
        caches.emplace_back(dex_pc, /*is_missing_types*/true, classes);
      }
      ProfileMethodInfo pmi(method->GetDexFile(), method->GetDexMethodIndex(), caches);
      profile_methods.push_back(pmi);
      profile_methods_map->Put(method, pmi);
    }

    if (!info.AddMethodsAndClasses(profile_methods, std::set<DexCacheResolvedClasses>())) {
      return false;
    }
    if (info.GetNumberOfMethods() != profile_methods.size()) {
      return false;
    }
    return info.Save(filename, nullptr);
  }

  // Creates an inline cache which will be destructed at the end of the test.
  ProfileCompilationInfo::InlineCacheMap* CreateInlineCacheMap() {
    used_inline_caches.emplace_back(new ProfileCompilationInfo::InlineCacheMap(
        std::less<uint16_t>(), arena_->Adapter(kArenaAllocProfile)));
    return used_inline_caches.back().get();
  }

  ProfileCompilationInfo::OfflineProfileMethodInfo ConvertProfileMethodInfo(
        const ProfileMethodInfo& pmi) {
    ProfileCompilationInfo::InlineCacheMap* ic_map = CreateInlineCacheMap();
    ProfileCompilationInfo::OfflineProfileMethodInfo offline_pmi(ic_map);
    SafeMap<DexFile*, uint8_t> dex_map;  // dex files to profile index
    for (const auto& inline_cache : pmi.inline_caches) {
      ProfileCompilationInfo::DexPcData& dex_pc_data =
          ic_map->FindOrAdd(
              inline_cache.dex_pc, ProfileCompilationInfo::DexPcData(arena_.get()))->second;
      if (inline_cache.is_missing_types) {
        dex_pc_data.SetIsMissingTypes();
      }
      for (const auto& class_ref : inline_cache.classes) {
        uint8_t dex_profile_index = dex_map.FindOrAdd(const_cast<DexFile*>(class_ref.dex_file),
                                                      static_cast<uint8_t>(dex_map.size()))->second;
        dex_pc_data.AddClass(dex_profile_index, class_ref.type_index);
        if (dex_profile_index >= offline_pmi.dex_references.size()) {
          // This is a new dex.
          const std::string& dex_key = ProfileCompilationInfo::GetProfileDexFileKey(
              class_ref.dex_file->GetLocation());
          offline_pmi.dex_references.emplace_back(dex_key,
                                                  class_ref.dex_file->GetLocationChecksum());
        }
      }
    }
    return offline_pmi;
  }

  // Creates an offline profile used for testing inline caches.
  ProfileCompilationInfo::OfflineProfileMethodInfo GetOfflineProfileMethodInfo() {
    ProfileCompilationInfo::InlineCacheMap* ic_map = CreateInlineCacheMap();
    // Monomorphic
    for (uint16_t dex_pc = 0; dex_pc < 11; dex_pc++) {
      ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
      dex_pc_data.AddClass(0, dex::TypeIndex(0));
      ic_map->Put(dex_pc, dex_pc_data);
    }
    // Polymorphic
    for (uint16_t dex_pc = 11; dex_pc < 22; dex_pc++) {
      ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
      dex_pc_data.AddClass(0, dex::TypeIndex(0));
      dex_pc_data.AddClass(1, dex::TypeIndex(1));
      dex_pc_data.AddClass(2, dex::TypeIndex(2));

      ic_map->Put(dex_pc, dex_pc_data);
    }
    // Megamorphic
    for (uint16_t dex_pc = 22; dex_pc < 33; dex_pc++) {
      ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
      dex_pc_data.SetIsMegamorphic();
      ic_map->Put(dex_pc, dex_pc_data);
    }
    // Missing types
    for (uint16_t dex_pc = 33; dex_pc < 44; dex_pc++) {
      ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
      dex_pc_data.SetIsMissingTypes();
      ic_map->Put(dex_pc, dex_pc_data);
    }

    ProfileCompilationInfo::OfflineProfileMethodInfo pmi(ic_map);

    pmi.dex_references.emplace_back("dex_location1", /* checksum */1);
    pmi.dex_references.emplace_back("dex_location2", /* checksum */2);
    pmi.dex_references.emplace_back("dex_location3", /* checksum */3);

    return pmi;
  }

  void MakeMegamorphic(/*out*/ProfileCompilationInfo::OfflineProfileMethodInfo* pmi) {
    ProfileCompilationInfo::InlineCacheMap* ic_map =
        const_cast<ProfileCompilationInfo::InlineCacheMap*>(pmi->inline_caches);
    for (auto it : *ic_map) {
      for (uint16_t k = 0; k <= 2 * InlineCache::kIndividualCacheSize; k++) {
        it.second.AddClass(0, dex::TypeIndex(k));
      }
    }
  }

  void SetIsMissingTypes(/*out*/ProfileCompilationInfo::OfflineProfileMethodInfo* pmi) {
    ProfileCompilationInfo::InlineCacheMap* ic_map =
        const_cast<ProfileCompilationInfo::InlineCacheMap*>(pmi->inline_caches);
    for (auto it : *ic_map) {
      it.second.SetIsMissingTypes();
    }
  }

  // Cannot sizeof the actual arrays so hard code the values here.
  // They should not change anyway.
  static constexpr int kProfileMagicSize = 4;
  static constexpr int kProfileVersionSize = 4;

  std::unique_ptr<ArenaAllocator> arena_;

  // Cache of inline caches generated during tests.
  // This makes it easier to pass data between different utilities and ensure that
  // caches are destructed at the end of the test.
  std::vector<std::unique_ptr<ProfileCompilationInfo::InlineCacheMap>> used_inline_caches;
};

TEST_F(ProfileCompilationInfoTest, SaveArtMethods) {
  ScratchFile profile;

  Thread* self = Thread::Current();
  jobject class_loader;
  {
    ScopedObjectAccess soa(self);
    class_loader = LoadDex("ProfileTestMultiDex");
  }
  ASSERT_NE(class_loader, nullptr);

  // Save virtual methods from Main.
  std::set<DexCacheResolvedClasses> resolved_classes;
  std::vector<ArtMethod*> main_methods = GetVirtualMethods(class_loader, "LMain;");
  ASSERT_TRUE(SaveProfilingInfo(profile.GetFilename(), main_methods, resolved_classes));

  // Check that what we saved is in the profile.
  ProfileCompilationInfo info1;
  ASSERT_TRUE(info1.Load(GetFd(profile)));
  ASSERT_EQ(info1.GetNumberOfMethods(), main_methods.size());
  {
    ScopedObjectAccess soa(self);
    for (ArtMethod* m : main_methods) {
      ASSERT_TRUE(info1.ContainsMethod(MethodReference(m->GetDexFile(), m->GetDexMethodIndex())));
    }
  }

  // Save virtual methods from Second.
  std::vector<ArtMethod*> second_methods = GetVirtualMethods(class_loader, "LSecond;");
  ASSERT_TRUE(SaveProfilingInfo(profile.GetFilename(), second_methods, resolved_classes));

  // Check that what we saved is in the profile (methods form Main and Second).
  ProfileCompilationInfo info2;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(info2.Load(GetFd(profile)));
  ASSERT_EQ(info2.GetNumberOfMethods(), main_methods.size() + second_methods.size());
  {
    ScopedObjectAccess soa(self);
    for (ArtMethod* m : main_methods) {
      ASSERT_TRUE(info2.ContainsMethod(MethodReference(m->GetDexFile(), m->GetDexMethodIndex())));
    }
    for (ArtMethod* m : second_methods) {
      ASSERT_TRUE(info2.ContainsMethod(MethodReference(m->GetDexFile(), m->GetDexMethodIndex())));
    }
  }
}

TEST_F(ProfileCompilationInfoTest, SaveFd) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  // Save a few methods.
  for (uint16_t i = 0; i < 10; i++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /* method_idx */ i, &saved_info));
    ASSERT_TRUE(AddMethod("dex_location2", /* checksum */ 2, /* method_idx */ i, &saved_info));
  }
  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));
  ASSERT_TRUE(loaded_info.Equals(saved_info));

  // Save more methods.
  for (uint16_t i = 0; i < 100; i++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /* method_idx */ i, &saved_info));
    ASSERT_TRUE(AddMethod("dex_location2", /* checksum */ 2, /* method_idx */ i, &saved_info));
    ASSERT_TRUE(AddMethod("dex_location3", /* checksum */ 3, /* method_idx */ i, &saved_info));
  }
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we get back everything we saved.
  ProfileCompilationInfo loaded_info2;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info2.Load(GetFd(profile)));
  ASSERT_TRUE(loaded_info2.Equals(saved_info));
}

TEST_F(ProfileCompilationInfoTest, AddMethodsAndClassesFail) {
  ScratchFile profile;

  ProfileCompilationInfo info;
  ASSERT_TRUE(AddMethod("dex_location", /* checksum */ 1, /* method_idx */ 1, &info));
  // Trying to add info for an existing file but with a different checksum.
  ASSERT_FALSE(AddMethod("dex_location", /* checksum */ 2, /* method_idx */ 2, &info));
}

TEST_F(ProfileCompilationInfoTest, MergeFail) {
  ScratchFile profile;

  ProfileCompilationInfo info1;
  ASSERT_TRUE(AddMethod("dex_location", /* checksum */ 1, /* method_idx */ 1, &info1));
  // Use the same file, change the checksum.
  ProfileCompilationInfo info2;
  ASSERT_TRUE(AddMethod("dex_location", /* checksum */ 2, /* method_idx */ 2, &info2));

  ASSERT_FALSE(info1.MergeWith(info2));
}

TEST_F(ProfileCompilationInfoTest, SaveMaxMethods) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  // Save the maximum number of methods
  for (uint16_t i = 0; i < std::numeric_limits<uint16_t>::max(); i++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /* method_idx */ i, &saved_info));
    ASSERT_TRUE(AddMethod("dex_location2", /* checksum */ 2, /* method_idx */ i, &saved_info));
  }
  // Save the maximum number of classes
  for (uint16_t i = 0; i < std::numeric_limits<uint16_t>::max(); i++) {
    ASSERT_TRUE(AddClass("dex_location1", /* checksum */ 1, /* class_idx */ i, &saved_info));
    ASSERT_TRUE(AddClass("dex_location2", /* checksum */ 2, /* class_idx */ i, &saved_info));
  }

  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));
  ASSERT_TRUE(loaded_info.Equals(saved_info));
}

TEST_F(ProfileCompilationInfoTest, SaveEmpty) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));
  ASSERT_TRUE(loaded_info.Equals(saved_info));
}

TEST_F(ProfileCompilationInfoTest, LoadEmpty) {
  ScratchFile profile;

  ProfileCompilationInfo empty_info;

  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));
  ASSERT_TRUE(loaded_info.Equals(empty_info));
}

TEST_F(ProfileCompilationInfoTest, BadMagic) {
  ScratchFile profile;
  uint8_t buffer[] = { 1, 2, 3, 4 };
  ASSERT_TRUE(profile.GetFile()->WriteFully(buffer, sizeof(buffer)));
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_FALSE(loaded_info.Load(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, BadVersion) {
  ScratchFile profile;

  ASSERT_TRUE(profile.GetFile()->WriteFully(
      ProfileCompilationInfo::kProfileMagic, kProfileMagicSize));
  uint8_t version[] = { 'v', 'e', 'r', 's', 'i', 'o', 'n' };
  ASSERT_TRUE(profile.GetFile()->WriteFully(version, sizeof(version)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_FALSE(loaded_info.Load(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, Incomplete) {
  ScratchFile profile;
  ASSERT_TRUE(profile.GetFile()->WriteFully(
      ProfileCompilationInfo::kProfileMagic, kProfileMagicSize));
  ASSERT_TRUE(profile.GetFile()->WriteFully(
      ProfileCompilationInfo::kProfileVersion, kProfileVersionSize));
  // Write that we have at least one line.
  uint8_t line_number[] = { 0, 1 };
  ASSERT_TRUE(profile.GetFile()->WriteFully(line_number, sizeof(line_number)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_FALSE(loaded_info.Load(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, TooLongDexLocation) {
  ScratchFile profile;
  ASSERT_TRUE(profile.GetFile()->WriteFully(
      ProfileCompilationInfo::kProfileMagic, kProfileMagicSize));
  ASSERT_TRUE(profile.GetFile()->WriteFully(
      ProfileCompilationInfo::kProfileVersion, kProfileVersionSize));
  // Write that we have at least one line.
  uint8_t line_number[] = { 0, 1 };
  ASSERT_TRUE(profile.GetFile()->WriteFully(line_number, sizeof(line_number)));

  // dex_location_size, methods_size, classes_size, checksum.
  // Dex location size is too big and should be rejected.
  uint8_t line[] = { 255, 255, 0, 1, 0, 1, 0, 0, 0, 0 };
  ASSERT_TRUE(profile.GetFile()->WriteFully(line, sizeof(line)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_FALSE(loaded_info.Load(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, UnexpectedContent) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  // Save the maximum number of methods
  for (uint16_t i = 0; i < 10; i++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /* method_idx */ i, &saved_info));
  }
  ASSERT_TRUE(saved_info.Save(GetFd(profile)));

  uint8_t random_data[] = { 1, 2, 3};
  ASSERT_TRUE(profile.GetFile()->WriteFully(random_data, sizeof(random_data)));

  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we fail because of unexpected data at the end of the file.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_FALSE(loaded_info.Load(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, SaveInlineCaches) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi = GetOfflineProfileMethodInfo();

  // Add methods with inline caches.
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    // Add a method which is part of the same dex file as one of the
    // class from the inline caches.
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info));
    // Add a method which is outside the set of dex files.
    ASSERT_TRUE(AddMethod("dex_location4", /* checksum */ 4, method_idx, pmi, &saved_info));
  }

  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));

  ASSERT_TRUE(loaded_info.Equals(saved_info));

  std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi1 =
      loaded_info.GetMethod("dex_location1", /* checksum */ 1, /* method_idx */ 3);
  ASSERT_TRUE(loaded_pmi1 != nullptr);
  ASSERT_TRUE(*loaded_pmi1 == pmi);
  std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi2 =
      loaded_info.GetMethod("dex_location4", /* checksum */ 4, /* method_idx */ 3);
  ASSERT_TRUE(loaded_pmi2 != nullptr);
  ASSERT_TRUE(*loaded_pmi2 == pmi);
}

TEST_F(ProfileCompilationInfoTest, MegamorphicInlineCaches) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi = GetOfflineProfileMethodInfo();

  // Add methods with inline caches.
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info));
  }

  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Make the inline caches megamorphic and add them to the profile again.
  ProfileCompilationInfo saved_info_extra;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi_extra = GetOfflineProfileMethodInfo();
  MakeMegamorphic(&pmi_extra);
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info_extra));
  }

  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(saved_info_extra.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Merge the profiles so that we have the same view as the file.
  ASSERT_TRUE(saved_info.MergeWith(saved_info_extra));

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));

  ASSERT_TRUE(loaded_info.Equals(saved_info));

  std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi1 =
      loaded_info.GetMethod("dex_location1", /* checksum */ 1, /* method_idx */ 3);

  ASSERT_TRUE(loaded_pmi1 != nullptr);
  ASSERT_TRUE(*loaded_pmi1 == pmi_extra);
}

TEST_F(ProfileCompilationInfoTest, MissingTypesInlineCaches) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi = GetOfflineProfileMethodInfo();

  // Add methods with inline caches.
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info));
  }

  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Make some inline caches megamorphic and add them to the profile again.
  ProfileCompilationInfo saved_info_extra;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi_extra = GetOfflineProfileMethodInfo();
  MakeMegamorphic(&pmi_extra);
  for (uint16_t method_idx = 5; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info_extra));
  }

  // Mark all inline caches with missing types and add them to the profile again.
  // This will verify that all inline caches (megamorphic or not) should be marked as missing types.
  ProfileCompilationInfo::OfflineProfileMethodInfo missing_types = GetOfflineProfileMethodInfo();
  SetIsMissingTypes(&missing_types);
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &saved_info_extra));
  }

  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(saved_info_extra.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());

  // Merge the profiles so that we have the same view as the file.
  ASSERT_TRUE(saved_info.MergeWith(saved_info_extra));

  // Check that we get back what we saved.
  ProfileCompilationInfo loaded_info;
  ASSERT_TRUE(profile.GetFile()->ResetOffset());
  ASSERT_TRUE(loaded_info.Load(GetFd(profile)));

  ASSERT_TRUE(loaded_info.Equals(saved_info));

  std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi1 =
      loaded_info.GetMethod("dex_location1", /* checksum */ 1, /* method_idx */ 3);
  ASSERT_TRUE(loaded_pmi1 != nullptr);
  ASSERT_TRUE(*loaded_pmi1 == pmi_extra);
}

TEST_F(ProfileCompilationInfoTest, SaveArtMethodsWithInlineCaches) {
  ScratchFile profile;

  Thread* self = Thread::Current();
  jobject class_loader;
  {
    ScopedObjectAccess soa(self);
    class_loader = LoadDex("ProfileTestMultiDex");
  }
  ASSERT_NE(class_loader, nullptr);

  // Save virtual methods from Main.
  std::set<DexCacheResolvedClasses> resolved_classes;
  std::vector<ArtMethod*> main_methods = GetVirtualMethods(class_loader, "LMain;");

  SafeMap<ArtMethod*, ProfileMethodInfo> profile_methods_map;
  ASSERT_TRUE(SaveProfilingInfoWithFakeInlineCaches(
      profile.GetFilename(), main_methods,  &profile_methods_map));

  // Check that what we saved is in the profile.
  ProfileCompilationInfo info;
  ASSERT_TRUE(info.Load(GetFd(profile)));
  ASSERT_EQ(info.GetNumberOfMethods(), main_methods.size());
  {
    ScopedObjectAccess soa(self);
    for (ArtMethod* m : main_methods) {
      ASSERT_TRUE(info.ContainsMethod(MethodReference(m->GetDexFile(), m->GetDexMethodIndex())));
      const ProfileMethodInfo& pmi = profile_methods_map.find(m)->second;
      std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> offline_pmi =
          info.GetMethod(m->GetDexFile()->GetLocation(),
                         m->GetDexFile()->GetLocationChecksum(),
                         m->GetDexMethodIndex());
      ASSERT_TRUE(offline_pmi != nullptr);
      ProfileCompilationInfo::OfflineProfileMethodInfo converted_pmi =
          ConvertProfileMethodInfo(pmi);
      ASSERT_EQ(converted_pmi, *offline_pmi);
    }
  }
}

TEST_F(ProfileCompilationInfoTest, InvalidChecksumInInlineCache) {
  ScratchFile profile;

  ProfileCompilationInfo info;
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi1 = GetOfflineProfileMethodInfo();
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi2 = GetOfflineProfileMethodInfo();
  // Modify the checksum to trigger a mismatch.
  pmi2.dex_references[0].dex_checksum++;

  ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /*method_idx*/ 0, pmi1, &info));
  ASSERT_FALSE(AddMethod("dex_location2", /* checksum */ 2, /*method_idx*/ 0, pmi2, &info));
}

// Verify that profiles behave correctly even if the methods are added in a different
// order and with a different dex profile indices for the dex files.
TEST_F(ProfileCompilationInfoTest, MergeInlineCacheTriggerReindex) {
  ScratchFile profile;

  ProfileCompilationInfo info;
  ProfileCompilationInfo info_reindexed;

  ProfileCompilationInfo::InlineCacheMap* ic_map = CreateInlineCacheMap();
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi(ic_map);
  pmi.dex_references.emplace_back("dex_location1", /* checksum */ 1);
  pmi.dex_references.emplace_back("dex_location2", /* checksum */ 2);
  for (uint16_t dex_pc = 1; dex_pc < 5; dex_pc++) {
    ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
    dex_pc_data.AddClass(0, dex::TypeIndex(0));
    dex_pc_data.AddClass(1, dex::TypeIndex(1));
    ic_map->Put(dex_pc, dex_pc_data);
  }

  ProfileCompilationInfo::InlineCacheMap* ic_map_reindexed = CreateInlineCacheMap();
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi_reindexed(ic_map_reindexed);
  pmi_reindexed.dex_references.emplace_back("dex_location2", /* checksum */ 2);
  pmi_reindexed.dex_references.emplace_back("dex_location1", /* checksum */ 1);
  for (uint16_t dex_pc = 1; dex_pc < 5; dex_pc++) {
    ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
    dex_pc_data.AddClass(1, dex::TypeIndex(0));
    dex_pc_data.AddClass(0, dex::TypeIndex(1));
    ic_map_reindexed->Put(dex_pc, dex_pc_data);
  }

  // Profile 1 and Profile 2 get the same methods but in different order.
  // This will trigger a different dex numbers.
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, method_idx, pmi, &info));
    ASSERT_TRUE(AddMethod("dex_location2", /* checksum */ 2, method_idx, pmi, &info));
  }

  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    ASSERT_TRUE(AddMethod(
      "dex_location2", /* checksum */ 2, method_idx, pmi_reindexed, &info_reindexed));
    ASSERT_TRUE(AddMethod(
      "dex_location1", /* checksum */ 1, method_idx, pmi_reindexed, &info_reindexed));
  }

  ProfileCompilationInfo info_backup;
  info_backup.MergeWith(info);
  ASSERT_TRUE(info.MergeWith(info_reindexed));
  // Merging should have no effect as we're adding the exact same stuff.
  ASSERT_TRUE(info.Equals(info_backup));
  for (uint16_t method_idx = 0; method_idx < 10; method_idx++) {
    std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi1 =
        info.GetMethod("dex_location1", /* checksum */ 1, method_idx);
    ASSERT_TRUE(loaded_pmi1 != nullptr);
    ASSERT_TRUE(*loaded_pmi1 == pmi);
    std::unique_ptr<ProfileCompilationInfo::OfflineProfileMethodInfo> loaded_pmi2 =
        info.GetMethod("dex_location2", /* checksum */ 2, method_idx);
    ASSERT_TRUE(loaded_pmi2 != nullptr);
    ASSERT_TRUE(*loaded_pmi2 == pmi);
  }
}

TEST_F(ProfileCompilationInfoTest, AddMoreDexFileThanLimit) {
  ProfileCompilationInfo info;
  // Save a few methods.
  for (uint16_t i = 0; i < std::numeric_limits<uint8_t>::max(); i++) {
    std::string dex_location = std::to_string(i);
    ASSERT_TRUE(AddMethod(dex_location, /* checksum */ 1, /* method_idx */ i, &info));
  }
  // We only support at most 255 dex files.
  ASSERT_FALSE(AddMethod(
      /*dex_location*/ "256", /* checksum */ 1, /* method_idx */ 0, &info));
}

TEST_F(ProfileCompilationInfoTest, MegamorphicInlineCachesMerge) {
  // Create a megamorphic inline cache.
  ProfileCompilationInfo::InlineCacheMap* ic_map = CreateInlineCacheMap();
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi(ic_map);
  pmi.dex_references.emplace_back("dex_location1", /* checksum */ 1);
  ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
  dex_pc_data.SetIsMegamorphic();
  ic_map->Put(/*dex_pc*/ 0, dex_pc_data);

  ProfileCompilationInfo info_megamorphic;
  ASSERT_TRUE(AddMethod("dex_location1",
                        /*checksum*/ 1,
                        /*method_idx*/ 0,
                        pmi,
                        &info_megamorphic));

  // Create a profile with no inline caches (for the same method).
  ProfileCompilationInfo info_no_inline_cache;
  ASSERT_TRUE(AddMethod("dex_location1",
                        /*checksum*/ 1,
                        /*method_idx*/ 0,
                        &info_no_inline_cache));

  // Merge the megamorphic cache into the empty one.
  ASSERT_TRUE(info_no_inline_cache.MergeWith(info_megamorphic));
  ScratchFile profile;
  // Saving profile should work without crashing (b/35644850).
  ASSERT_TRUE(info_no_inline_cache.Save(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, MissingTypesInlineCachesMerge) {
  // Create an inline cache with missing types
  ProfileCompilationInfo::InlineCacheMap* ic_map = CreateInlineCacheMap();
  ProfileCompilationInfo::OfflineProfileMethodInfo pmi(ic_map);
  pmi.dex_references.emplace_back("dex_location1", /* checksum */ 1);
  ProfileCompilationInfo::DexPcData dex_pc_data(arena_.get());
  dex_pc_data.SetIsMissingTypes();
  ic_map->Put(/*dex_pc*/ 0, dex_pc_data);

  ProfileCompilationInfo info_megamorphic;
  ASSERT_TRUE(AddMethod("dex_location1",
                        /*checksum*/ 1,
                        /*method_idx*/ 0,
                        pmi,
                        &info_megamorphic));

  // Create a profile with no inline caches (for the same method).
  ProfileCompilationInfo info_no_inline_cache;
  ASSERT_TRUE(AddMethod("dex_location1",
                        /*checksum*/ 1,
                        /*method_idx*/ 0,
                        &info_no_inline_cache));

  // Merge the missing type cache into the empty one.
  // Everything should be saved without errors.
  ASSERT_TRUE(info_no_inline_cache.MergeWith(info_megamorphic));
  ScratchFile profile;
  ASSERT_TRUE(info_no_inline_cache.Save(GetFd(profile)));
}

TEST_F(ProfileCompilationInfoTest, LoadShouldClearExistingDataFromProfiles) {
  ScratchFile profile;

  ProfileCompilationInfo saved_info;
  // Save a few methods.
  for (uint16_t i = 0; i < 10; i++) {
    ASSERT_TRUE(AddMethod("dex_location1", /* checksum */ 1, /* method_idx */ i, &saved_info));
  }
  ASSERT_TRUE(saved_info.Save(GetFd(profile)));
  ASSERT_EQ(0, profile.GetFile()->Flush());
  ASSERT_TRUE(profile.GetFile()->ResetOffset());

  // Add a bunch of methods to test_info.
  ProfileCompilationInfo test_info;
  for (uint16_t i = 0; i < 10; i++) {
    ASSERT_TRUE(AddMethod("dex_location2", /* checksum */ 2, /* method_idx */ i, &test_info));
  }

  // Attempt to load the saved profile into test_info.
  // This should fail since the test_info already contains data and the load would overwrite it.
  ASSERT_FALSE(test_info.Load(GetFd(profile)));
}
}  // namespace art
