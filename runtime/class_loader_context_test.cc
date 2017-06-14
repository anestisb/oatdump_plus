/*
 * Copyright (C) 2017 The Android Open Source Project
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


#include "class_loader_context.h"
#include "common_runtime_test.h"

#include "base/dchecked_vector.h"
#include "base/stl_util.h"
#include "class_linker.h"
#include "dex_file.h"
#include "handle_scope-inl.h"
#include "mirror/class.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "oat_file_assistant.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "well_known_classes.h"

namespace art {

class ClassLoaderContextTest : public CommonRuntimeTest {
 public:
  void VerifyContextSize(ClassLoaderContext* context, size_t expected_size) {
    ASSERT_TRUE(context != nullptr);
    ASSERT_EQ(expected_size, context->class_loader_chain_.size());
  }

  void VerifyClassLoaderPCL(ClassLoaderContext* context,
                            size_t index,
                            std::string classpath) {
    VerifyClassLoaderInfo(
        context, index, ClassLoaderContext::kPathClassLoader, classpath);
  }

  void VerifyClassLoaderDLC(ClassLoaderContext* context,
                            size_t index,
                            std::string classpath) {
    VerifyClassLoaderInfo(
        context, index, ClassLoaderContext::kDelegateLastClassLoader, classpath);
  }

  void VerifyOpenDexFiles(
      ClassLoaderContext* context,
      size_t index,
      std::vector<std::vector<std::unique_ptr<const DexFile>>*>& all_dex_files) {
    ASSERT_TRUE(context != nullptr);
    ASSERT_TRUE(context->dex_files_open_attempted_);
    ASSERT_TRUE(context->dex_files_open_result_);
    ClassLoaderContext::ClassLoaderInfo& info = context->class_loader_chain_[index];
    ASSERT_EQ(all_dex_files.size(), info.classpath.size());
    size_t cur_open_dex_index = 0;
    for (size_t k = 0; k < all_dex_files.size(); k++) {
      std::vector<std::unique_ptr<const DexFile>>& dex_files_for_cp_elem = *(all_dex_files[k]);
      for (size_t i = 0; i < dex_files_for_cp_elem.size(); i++) {
        ASSERT_LT(cur_open_dex_index, info.opened_dex_files.size());

        std::unique_ptr<const DexFile>& opened_dex_file =
            info.opened_dex_files[cur_open_dex_index++];
        std::unique_ptr<const DexFile>& expected_dex_file = dex_files_for_cp_elem[i];

        ASSERT_EQ(expected_dex_file->GetLocation(), opened_dex_file->GetLocation());
        ASSERT_EQ(expected_dex_file->GetLocationChecksum(), opened_dex_file->GetLocationChecksum());
        ASSERT_EQ(info.classpath[k], opened_dex_file->GetBaseLocation());
      }
    }
  }

 private:
  void VerifyClassLoaderInfo(ClassLoaderContext* context,
                             size_t index,
                             ClassLoaderContext::ClassLoaderType type,
                             std::string classpath) {
    ASSERT_TRUE(context != nullptr);
    ASSERT_GT(context->class_loader_chain_.size(), index);
    ClassLoaderContext::ClassLoaderInfo& info = context->class_loader_chain_[index];
    ASSERT_EQ(type, info.type);
    std::vector<std::string> expected_classpath;
    Split(classpath, ':', &expected_classpath);
    ASSERT_EQ(expected_classpath, info.classpath);
  }
};

TEST_F(ClassLoaderContextTest, ParseValidContextPCL) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[a.dex]");
  VerifyContextSize(context.get(), 1);
  VerifyClassLoaderPCL(context.get(), 0, "a.dex");
}

TEST_F(ClassLoaderContextTest, ParseValidContextDLC) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("DLC[a.dex]");
  VerifyContextSize(context.get(), 1);
  VerifyClassLoaderDLC(context.get(), 0, "a.dex");
}

TEST_F(ClassLoaderContextTest, ParseValidContextChain) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[a.dex:b.dex];DLC[c.dex:d.dex];PCL[e.dex]");
  VerifyContextSize(context.get(), 3);
  VerifyClassLoaderPCL(context.get(), 0, "a.dex:b.dex");
  VerifyClassLoaderDLC(context.get(), 1, "c.dex:d.dex");
  VerifyClassLoaderPCL(context.get(), 2, "e.dex");
}

TEST_F(ClassLoaderContextTest, ParseValidEmptyContextDLC) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("DLC[]");
  VerifyContextSize(context.get(), 1);
  VerifyClassLoaderDLC(context.get(), 0, "");
}

TEST_F(ClassLoaderContextTest, ParseValidContextSpecialSymbol) {
  std::unique_ptr<ClassLoaderContext> context =
    ClassLoaderContext::Create(OatFile::kSpecialSharedLibrary);
  VerifyContextSize(context.get(), 0);
}

TEST_F(ClassLoaderContextTest, ParseInvalidValidContexts) {
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("ABC[a.dex]"));
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("PCL"));
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("PCL[a.dex"));
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("PCLa.dex]"));
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("PCL{a.dex}"));
  ASSERT_TRUE(nullptr == ClassLoaderContext::Create("PCL[a.dex];DLC[b.dex"));
}

TEST_F(ClassLoaderContextTest, OpenInvalidDexFiles) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[does_not_exist.dex]");
  VerifyContextSize(context.get(), 1);
  ASSERT_FALSE(context->OpenDexFiles(InstructionSet::kArm, "."));
}

TEST_F(ClassLoaderContextTest, OpenValidDexFiles) {
  std::string multidex_name = GetTestDexFileName("MultiDex");
  std::vector<std::unique_ptr<const DexFile>> multidex_files = OpenTestDexFiles("MultiDex");
  std::string myclass_dex_name = GetTestDexFileName("MyClass");
  std::vector<std::unique_ptr<const DexFile>> myclass_dex_files = OpenTestDexFiles("MyClass");
  std::string dex_name = GetTestDexFileName("Main");
  std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles("Main");


  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create(
          "PCL[" + multidex_name + ":" + myclass_dex_name + "];" +
          "DLC[" + dex_name + "]");

  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, /*classpath_dir*/ ""));

  VerifyContextSize(context.get(), 2);
  std::vector<std::vector<std::unique_ptr<const DexFile>>*> all_dex_files0;
  all_dex_files0.push_back(&multidex_files);
  all_dex_files0.push_back(&myclass_dex_files);
  std::vector<std::vector<std::unique_ptr<const DexFile>>*> all_dex_files1;
  all_dex_files1.push_back(&dex_files);

  VerifyOpenDexFiles(context.get(), 0, all_dex_files0);
  VerifyOpenDexFiles(context.get(), 1, all_dex_files1);
}

TEST_F(ClassLoaderContextTest, OpenInvalidDexFilesMix) {
  std::string dex_name = GetTestDexFileName("Main");
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[does_not_exist.dex];DLC[" + dex_name + "]");
  ASSERT_FALSE(context->OpenDexFiles(InstructionSet::kArm, ""));
}

TEST_F(ClassLoaderContextTest, CreateClassLoader) {
  std::string dex_name = GetTestDexFileName("Main");
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[" + dex_name + "]");
  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, ""));

  std::vector<std::unique_ptr<const DexFile>> classpath_dex = OpenTestDexFiles("Main");
  std::vector<std::unique_ptr<const DexFile>> compilation_sources = OpenTestDexFiles("MultiDex");

  std::vector<const DexFile*> compilation_sources_raw =
      MakeNonOwningPointerVector(compilation_sources);
  jobject jclass_loader = context->CreateClassLoader(compilation_sources_raw);
  ASSERT_TRUE(jclass_loader != nullptr);

  ScopedObjectAccess soa(Thread::Current());

  StackHandleScope<2> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));

  ASSERT_TRUE(class_loader->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_PathClassLoader));
  ASSERT_TRUE(class_loader->GetParent()->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_BootClassLoader));


  std::vector<const DexFile*> class_loader_dex_files = GetDexFiles(jclass_loader);
  ASSERT_EQ(classpath_dex.size() + compilation_sources.size(), class_loader_dex_files.size());

  // The classpath dex files must come first.
  for (size_t i = 0; i < classpath_dex.size(); i++) {
    ASSERT_EQ(classpath_dex[i]->GetLocation(),
              class_loader_dex_files[i]->GetLocation());
    ASSERT_EQ(classpath_dex[i]->GetLocationChecksum(),
              class_loader_dex_files[i]->GetLocationChecksum());
  }

  // The compilation dex files must come second.
  for (size_t i = 0, k = classpath_dex.size(); i < compilation_sources.size(); i++, k++) {
    ASSERT_EQ(compilation_sources[i]->GetLocation(),
              class_loader_dex_files[k]->GetLocation());
    ASSERT_EQ(compilation_sources[i]->GetLocationChecksum(),
              class_loader_dex_files[k]->GetLocationChecksum());
  }
}

TEST_F(ClassLoaderContextTest, RemoveSourceLocations) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[a.dex]");
  dchecked_vector<std::string> classpath_dex;
  classpath_dex.push_back("a.dex");
  dchecked_vector<std::string> compilation_sources;
  compilation_sources.push_back("src.dex");

  // Nothing should be removed.
  ASSERT_FALSE(context->RemoveLocationsFromClassPaths(compilation_sources));
  VerifyClassLoaderPCL(context.get(), 0, "a.dex");
  // Classes should be removed.
  ASSERT_TRUE(context->RemoveLocationsFromClassPaths(classpath_dex));
  VerifyClassLoaderPCL(context.get(), 0, "");
}

TEST_F(ClassLoaderContextTest, EncodeInOatFile) {
  std::string dex1_name = GetTestDexFileName("Main");
  std::string dex2_name = GetTestDexFileName("MyClass");
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("PCL[" + dex1_name + ":" + dex2_name + "]");
  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, ""));

  std::vector<std::unique_ptr<const DexFile>> dex1 = OpenTestDexFiles("Main");
  std::vector<std::unique_ptr<const DexFile>> dex2 = OpenTestDexFiles("MyClass");
  std::string encoding = context->EncodeContextForOatFile("");
  std::string expected_encoding =
      dex1[0]->GetLocation() + "*" + std::to_string(dex1[0]->GetLocationChecksum()) + "*" +
      dex2[0]->GetLocation() + "*" + std::to_string(dex2[0]->GetLocationChecksum()) + "*";
  ASSERT_EQ(expected_encoding, context->EncodeContextForOatFile(""));
}

}  // namespace art
