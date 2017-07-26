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
                            const std::string& classpath) {
    VerifyClassLoaderInfo(
        context, index, ClassLoaderContext::kPathClassLoader, classpath);
  }

  void VerifyClassLoaderDLC(ClassLoaderContext* context,
                            size_t index,
                            const std::string& classpath) {
    VerifyClassLoaderInfo(
        context, index, ClassLoaderContext::kDelegateLastClassLoader, classpath);
  }

  void VerifyClassLoaderPCLFromTestDex(ClassLoaderContext* context,
                                       size_t index,
                                       const std::string& test_name) {
    VerifyClassLoaderFromTestDex(
        context, index, ClassLoaderContext::kPathClassLoader, test_name);
  }

  void VerifyClassLoaderDLCFromTestDex(ClassLoaderContext* context,
                                       size_t index,
                                       const std::string& test_name) {
    VerifyClassLoaderFromTestDex(
        context, index, ClassLoaderContext::kDelegateLastClassLoader, test_name);
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

  std::unique_ptr<ClassLoaderContext> CreateContextForClassLoader(jobject class_loader) {
    return ClassLoaderContext::CreateContextForClassLoader(class_loader, nullptr);
  }

  std::unique_ptr<ClassLoaderContext> ParseContextWithChecksums(const std::string& context_spec) {
    std::unique_ptr<ClassLoaderContext> context(new ClassLoaderContext());
    if (!context->Parse(context_spec, /*parse_checksums*/ true)) {
      return nullptr;
    }
    return context;
  }

  void VerifyContextForClassLoader(ClassLoaderContext* context) {
    ASSERT_TRUE(context != nullptr);
    ASSERT_TRUE(context->dex_files_open_attempted_);
    ASSERT_TRUE(context->dex_files_open_result_);
    ASSERT_FALSE(context->owns_the_dex_files_);
    ASSERT_FALSE(context->special_shared_library_);
  }

  void VerifyClassLoaderDexFiles(ScopedObjectAccess& soa,
                                 Handle<mirror::ClassLoader> class_loader,
                                 jclass type,
                                 std::vector<const DexFile*>& expected_dex_files)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ASSERT_TRUE(class_loader->GetClass() == soa.Decode<mirror::Class>(type));

    std::vector<const DexFile*> class_loader_dex_files = GetDexFiles(soa, class_loader);
    ASSERT_EQ(expected_dex_files.size(), class_loader_dex_files.size());

    for (size_t i = 0; i < expected_dex_files.size(); i++) {
      ASSERT_EQ(expected_dex_files[i]->GetLocation(),
                class_loader_dex_files[i]->GetLocation());
      ASSERT_EQ(expected_dex_files[i]->GetLocationChecksum(),
                class_loader_dex_files[i]->GetLocationChecksum());
    }
  }

 private:
  void VerifyClassLoaderInfo(ClassLoaderContext* context,
                             size_t index,
                             ClassLoaderContext::ClassLoaderType type,
                             const std::string& classpath) {
    ASSERT_TRUE(context != nullptr);
    ASSERT_GT(context->class_loader_chain_.size(), index);
    ClassLoaderContext::ClassLoaderInfo& info = context->class_loader_chain_[index];
    ASSERT_EQ(type, info.type);
    std::vector<std::string> expected_classpath;
    Split(classpath, ':', &expected_classpath);
    ASSERT_EQ(expected_classpath, info.classpath);
  }

  void VerifyClassLoaderFromTestDex(ClassLoaderContext* context,
                                    size_t index,
                                    ClassLoaderContext::ClassLoaderType type,
                                    const std::string& test_name) {
    std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles(test_name.c_str());
    std::vector<std::vector<std::unique_ptr<const DexFile>>*> all_dex_files;
    all_dex_files.push_back(&dex_files);

    VerifyClassLoaderInfo(context, index, type, GetTestDexFileName(test_name.c_str()));
    VerifyOpenDexFiles(context, index, all_dex_files);
  }
};

TEST_F(ClassLoaderContextTest, ParseValidEmptyContext) {
  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::Create("");
  // An empty context should create a single empty PathClassLoader.
  VerifyContextSize(context.get(), 1);
  VerifyClassLoaderPCL(context.get(), 0, "");
}

TEST_F(ClassLoaderContextTest, ParseValidSharedLibraryContext) {
  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::Create("&");
  // An shared library context should have no class loader in the chain.
  VerifyContextSize(context.get(), 0);
}

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

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));

  ASSERT_TRUE(class_loader->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_PathClassLoader));
  ASSERT_TRUE(class_loader->GetParent()->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_BootClassLoader));

  // For the first class loader the class path dex files must come first and then the
  // compilation sources.
  std::vector<const DexFile*> expected_classpath = MakeNonOwningPointerVector(classpath_dex);
  for (auto& dex : compilation_sources_raw) {
    expected_classpath.push_back(dex);
  }

  VerifyClassLoaderDexFiles(soa,
                            class_loader,
                            WellKnownClasses::dalvik_system_PathClassLoader,
                            expected_classpath);
}

TEST_F(ClassLoaderContextTest, CreateClassLoaderWithEmptyContext) {
  std::unique_ptr<ClassLoaderContext> context =
      ClassLoaderContext::Create("");
  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, ""));

  std::vector<std::unique_ptr<const DexFile>> compilation_sources = OpenTestDexFiles("MultiDex");

  std::vector<const DexFile*> compilation_sources_raw =
      MakeNonOwningPointerVector(compilation_sources);
  jobject jclass_loader = context->CreateClassLoader(compilation_sources_raw);
  ASSERT_TRUE(jclass_loader != nullptr);

  ScopedObjectAccess soa(Thread::Current());

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));

  // An empty context should create a single PathClassLoader with only the compilation sources.
  VerifyClassLoaderDexFiles(soa,
                            class_loader,
                            WellKnownClasses::dalvik_system_PathClassLoader,
                            compilation_sources_raw);
  ASSERT_TRUE(class_loader->GetParent()->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_BootClassLoader));
}

TEST_F(ClassLoaderContextTest, CreateClassLoaderWithSharedLibraryContext) {
  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::Create("&");

  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, ""));

  std::vector<std::unique_ptr<const DexFile>> compilation_sources = OpenTestDexFiles("MultiDex");

  std::vector<const DexFile*> compilation_sources_raw =
      MakeNonOwningPointerVector(compilation_sources);
  jobject jclass_loader = context->CreateClassLoader(compilation_sources_raw);
  ASSERT_TRUE(jclass_loader != nullptr);

  ScopedObjectAccess soa(Thread::Current());

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));

  // A shared library context should create a single PathClassLoader with only the compilation
  // sources.
  VerifyClassLoaderDexFiles(soa,
      class_loader,
      WellKnownClasses::dalvik_system_PathClassLoader,
      compilation_sources_raw);
  ASSERT_TRUE(class_loader->GetParent()->GetClass() ==
  soa.Decode<mirror::Class>(WellKnownClasses::java_lang_BootClassLoader));
}

TEST_F(ClassLoaderContextTest, CreateClassLoaderWithComplexChain) {
  // Setup the context.
  std::vector<std::unique_ptr<const DexFile>> classpath_dex_a = OpenTestDexFiles("ForClassLoaderA");
  std::vector<std::unique_ptr<const DexFile>> classpath_dex_b = OpenTestDexFiles("ForClassLoaderB");
  std::vector<std::unique_ptr<const DexFile>> classpath_dex_c = OpenTestDexFiles("ForClassLoaderC");
  std::vector<std::unique_ptr<const DexFile>> classpath_dex_d = OpenTestDexFiles("ForClassLoaderD");

  std::string context_spec =
      "PCL[" + CreateClassPath(classpath_dex_a) + ":" + CreateClassPath(classpath_dex_b) + "];" +
      "DLC[" + CreateClassPath(classpath_dex_c) + "];" +
      "PCL[" + CreateClassPath(classpath_dex_d) + "]";

  std::unique_ptr<ClassLoaderContext> context = ClassLoaderContext::Create(context_spec);
  ASSERT_TRUE(context->OpenDexFiles(InstructionSet::kArm, ""));

  // Setup the compilation sources.
  std::vector<std::unique_ptr<const DexFile>> compilation_sources = OpenTestDexFiles("MultiDex");
  std::vector<const DexFile*> compilation_sources_raw =
      MakeNonOwningPointerVector(compilation_sources);

  // Create the class loader.
  jobject jclass_loader = context->CreateClassLoader(compilation_sources_raw);
  ASSERT_TRUE(jclass_loader != nullptr);

  // Verify the class loader.
  ScopedObjectAccess soa(Thread::Current());

  StackHandleScope<3> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader_1 = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));

  // Verify the first class loader

  // For the first class loader the class path dex files must come first and then the
  // compilation sources.
  std::vector<const DexFile*> class_loader_1_dex_files =
      MakeNonOwningPointerVector(classpath_dex_a);
  for (auto& dex : classpath_dex_b) {
    class_loader_1_dex_files.push_back(dex.get());
  }
  for (auto& dex : compilation_sources_raw) {
    class_loader_1_dex_files.push_back(dex);
  }
  VerifyClassLoaderDexFiles(soa,
                            class_loader_1,
                            WellKnownClasses::dalvik_system_PathClassLoader,
                            class_loader_1_dex_files);

  // Verify the second class loader
  Handle<mirror::ClassLoader> class_loader_2 = hs.NewHandle(class_loader_1->GetParent());
  std::vector<const DexFile*> class_loader_2_dex_files =
      MakeNonOwningPointerVector(classpath_dex_c);
  VerifyClassLoaderDexFiles(soa,
                            class_loader_2,
                            WellKnownClasses::dalvik_system_DelegateLastClassLoader,
                            class_loader_2_dex_files);

  // Verify the third class loader
  Handle<mirror::ClassLoader> class_loader_3 = hs.NewHandle(class_loader_2->GetParent());
  std::vector<const DexFile*> class_loader_3_dex_files =
      MakeNonOwningPointerVector(classpath_dex_d);
  VerifyClassLoaderDexFiles(soa,
                            class_loader_3,
                            WellKnownClasses::dalvik_system_PathClassLoader,
                            class_loader_3_dex_files);
  // The last class loader should have the BootClassLoader as a parent.
  ASSERT_TRUE(class_loader_3->GetParent()->GetClass() ==
      soa.Decode<mirror::Class>(WellKnownClasses::java_lang_BootClassLoader));
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
  std::string expected_encoding = "PCL[" + CreateClassPathWithChecksums(dex1) + ":" +
      CreateClassPathWithChecksums(dex2) + "]";
  ASSERT_EQ(expected_encoding, context->EncodeContextForOatFile(""));
}

// TODO(calin) add a test which creates the context for a class loader together with dex_elements.
TEST_F(ClassLoaderContextTest, CreateContextForClassLoader) {
  // The chain is
  //    ClassLoaderA (PathClassLoader)
  //       ^
  //       |
  //    ClassLoaderB (DelegateLastClassLoader)
  //       ^
  //       |
  //    ClassLoaderC (PathClassLoader)
  //       ^
  //       |
  //    ClassLoaderD (DelegateLastClassLoader)

  jobject class_loader_a = LoadDexInPathClassLoader("ForClassLoaderA", nullptr);
  jobject class_loader_b = LoadDexInDelegateLastClassLoader("ForClassLoaderB", class_loader_a);
  jobject class_loader_c = LoadDexInPathClassLoader("ForClassLoaderC", class_loader_b);
  jobject class_loader_d = LoadDexInDelegateLastClassLoader("ForClassLoaderD", class_loader_c);

  std::unique_ptr<ClassLoaderContext> context = CreateContextForClassLoader(class_loader_d);

  VerifyContextForClassLoader(context.get());
  VerifyContextSize(context.get(), 4);

  VerifyClassLoaderDLCFromTestDex(context.get(), 0, "ForClassLoaderD");
  VerifyClassLoaderPCLFromTestDex(context.get(), 1, "ForClassLoaderC");
  VerifyClassLoaderDLCFromTestDex(context.get(), 2, "ForClassLoaderB");
  VerifyClassLoaderPCLFromTestDex(context.get(), 3, "ForClassLoaderA");
}

TEST_F(ClassLoaderContextTest, VerifyClassLoaderContextMatch) {
  std::string context_spec = "PCL[a.dex*123:b.dex*456];DLC[c.dex*890]";
  std::unique_ptr<ClassLoaderContext> context = ParseContextWithChecksums(context_spec);

  VerifyContextSize(context.get(), 2);
  VerifyClassLoaderPCL(context.get(), 0, "a.dex:b.dex");
  VerifyClassLoaderDLC(context.get(), 1, "c.dex");

  ASSERT_TRUE(context->VerifyClassLoaderContextMatch(context_spec));

  std::string wrong_class_loader_type = "PCL[a.dex*123:b.dex*456];PCL[c.dex*890]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_class_loader_type));

  std::string wrong_class_loader_order = "DLC[c.dex*890];PCL[a.dex*123:b.dex*456]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_class_loader_order));

  std::string wrong_classpath_order = "PCL[b.dex*456:a.dex*123];DLC[c.dex*890]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_classpath_order));

  std::string wrong_checksum = "PCL[a.dex*999:b.dex*456];DLC[c.dex*890]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_checksum));

  std::string wrong_extra_class_loader = "PCL[a.dex*123:b.dex*456];DLC[c.dex*890];PCL[d.dex*321]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_extra_class_loader));

  std::string wrong_extra_classpath = "PCL[a.dex*123:b.dex*456];DLC[c.dex*890:d.dex*321]";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_extra_classpath));

  std::string wrong_spec = "PCL[a.dex*999:b.dex*456];DLC[";
  ASSERT_FALSE(context->VerifyClassLoaderContextMatch(wrong_spec));
}

TEST_F(ClassLoaderContextTest, VerifyClassLoaderContextMatchAfterEncoding) {
  jobject class_loader_a = LoadDexInPathClassLoader("ForClassLoaderA", nullptr);
  jobject class_loader_b = LoadDexInDelegateLastClassLoader("ForClassLoaderB", class_loader_a);
  jobject class_loader_c = LoadDexInPathClassLoader("ForClassLoaderC", class_loader_b);
  jobject class_loader_d = LoadDexInDelegateLastClassLoader("ForClassLoaderD", class_loader_c);

  std::unique_ptr<ClassLoaderContext> context = CreateContextForClassLoader(class_loader_d);

  ASSERT_TRUE(context->VerifyClassLoaderContextMatch(context->EncodeContextForOatFile("")));
}

TEST_F(ClassLoaderContextTest, VerifyClassLoaderContextMatchAfterEncodingMultidex) {
  jobject class_loader = LoadDexInPathClassLoader("MultiDex", nullptr);

  std::unique_ptr<ClassLoaderContext> context = CreateContextForClassLoader(class_loader);

  ASSERT_TRUE(context->VerifyClassLoaderContextMatch(context->EncodeContextForOatFile("")));
}

}  // namespace art
