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

// Test is in compiler, as it uses compiler related code.
#include "verifier/verifier_deps.h"

#include "class_linker.h"
#include "compiler/common_compiler_test.h"
#include "compiler/driver/compiler_options.h"
#include "compiler/driver/compiler_driver.h"
#include "compiler_callbacks.h"
#include "dex_file.h"
#include "handle_scope-inl.h"
#include "verifier/method_verifier-inl.h"
#include "mirror/class_loader.h"
#include "runtime.h"
#include "thread.h"
#include "scoped_thread_state_change-inl.h"

namespace art {
namespace verifier {

class VerifierDepsCompilerCallbacks : public CompilerCallbacks {
 public:
  explicit VerifierDepsCompilerCallbacks()
      : CompilerCallbacks(CompilerCallbacks::CallbackMode::kCompileApp),
        deps_(nullptr) {}

  void MethodVerified(verifier::MethodVerifier* verifier ATTRIBUTE_UNUSED) OVERRIDE {}
  void ClassRejected(ClassReference ref ATTRIBUTE_UNUSED) OVERRIDE {}
  bool IsRelocationPossible() OVERRIDE { return false; }

  verifier::VerifierDeps* GetVerifierDeps() const OVERRIDE { return deps_; }
  void SetVerifierDeps(verifier::VerifierDeps* deps) { deps_ = deps; }

 private:
  verifier::VerifierDeps* deps_;
};

class VerifierDepsTest : public CommonCompilerTest {
 public:
  void SetUpRuntimeOptions(RuntimeOptions* options) {
    CommonCompilerTest::SetUpRuntimeOptions(options);
    callbacks_.reset(new VerifierDepsCompilerCallbacks());
  }

  mirror::Class* FindClassByName(const std::string& name, ScopedObjectAccess* soa)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    StackHandleScope<1> hs(Thread::Current());
    Handle<mirror::ClassLoader> class_loader_handle(
        hs.NewHandle(soa->Decode<mirror::ClassLoader>(class_loader_)));
    mirror::Class* klass = class_linker_->FindClass(Thread::Current(),
                                                    name.c_str(),
                                                    class_loader_handle);
    if (klass == nullptr) {
      DCHECK(Thread::Current()->IsExceptionPending());
      Thread::Current()->ClearException();
    }
    return klass;
  }

  void SetVerifierDeps(const std::vector<const DexFile*>& dex_files) {
    verifier_deps_.reset(new verifier::VerifierDeps(dex_files));
    VerifierDepsCompilerCallbacks* callbacks =
        reinterpret_cast<VerifierDepsCompilerCallbacks*>(callbacks_.get());
    callbacks->SetVerifierDeps(verifier_deps_.get());
  }

  void LoadDexFile(ScopedObjectAccess* soa) REQUIRES_SHARED(Locks::mutator_lock_) {
    class_loader_ = LoadDex("VerifierDeps");
    std::vector<const DexFile*> dex_files = GetDexFiles(class_loader_);
    CHECK_EQ(dex_files.size(), 1u);
    dex_file_ = dex_files.front();

    SetVerifierDeps(dex_files);

    ObjPtr<mirror::ClassLoader> loader = soa->Decode<mirror::ClassLoader>(class_loader_);
    class_linker_->RegisterDexFile(*dex_file_, loader.Ptr());

    klass_Main_ = FindClassByName("LMain;", soa);
    CHECK(klass_Main_ != nullptr);
  }

  bool VerifyMethod(const std::string& method_name) {
    ScopedObjectAccess soa(Thread::Current());
    LoadDexFile(&soa);

    StackHandleScope<2> hs(Thread::Current());
    Handle<mirror::ClassLoader> class_loader_handle(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader_)));
    Handle<mirror::DexCache> dex_cache_handle(hs.NewHandle(klass_Main_->GetDexCache()));

    const DexFile::ClassDef* class_def = klass_Main_->GetClassDef();
    const uint8_t* class_data = dex_file_->GetClassData(*class_def);
    CHECK(class_data != nullptr);

    ClassDataItemIterator it(*dex_file_, class_data);
    while (it.HasNextStaticField() || it.HasNextInstanceField()) {
      it.Next();
    }

    ArtMethod* method = nullptr;
    while (it.HasNextDirectMethod()) {
      ArtMethod* resolved_method = class_linker_->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
          *dex_file_,
          it.GetMemberIndex(),
          dex_cache_handle,
          class_loader_handle,
          nullptr,
          it.GetMethodInvokeType(*class_def));
      CHECK(resolved_method != nullptr);
      if (method_name == resolved_method->GetName()) {
        method = resolved_method;
        break;
      }
      it.Next();
    }
    CHECK(method != nullptr);

    MethodVerifier verifier(Thread::Current(),
                            dex_file_,
                            dex_cache_handle,
                            class_loader_handle,
                            *class_def,
                            it.GetMethodCodeItem(),
                            it.GetMemberIndex(),
                            method,
                            it.GetMethodAccessFlags(),
                            true /* can_load_classes */,
                            true /* allow_soft_failures */,
                            true /* need_precise_constants */,
                            false /* verify to dump */,
                            true /* allow_thread_suspension */);
    verifier.Verify();
    return !verifier.HasFailures();
  }

  void VerifyDexFile() {
    std::string error_msg;
    {
      ScopedObjectAccess soa(Thread::Current());
      LoadDexFile(&soa);
    }
    SetVerifierDeps({ dex_file_ });
    TimingLogger timings("Verify", false, false);
    std::vector<const DexFile*> dex_files;
    dex_files.push_back(dex_file_);
    compiler_options_->boot_image_ = false;
    compiler_driver_->InitializeThreadPools();
    compiler_driver_->Verify(class_loader_, dex_files, &timings);
  }

  bool TestAssignabilityRecording(const std::string& dst,
                                  const std::string& src,
                                  bool is_strict,
                                  bool is_assignable) {
    ScopedObjectAccess soa(Thread::Current());
    LoadDexFile(&soa);
    mirror::Class* klass_dst = FindClassByName(dst, &soa);
    DCHECK(klass_dst != nullptr);
    mirror::Class* klass_src = FindClassByName(src, &soa);
    DCHECK(klass_src != nullptr);
    verifier_deps_->AddAssignability(*dex_file_,
                                     klass_dst,
                                     klass_src,
                                     is_strict,
                                     is_assignable);
    return true;
  }

  bool HasUnverifiedClass(const std::string& cls) {
    const DexFile::TypeId* type_id = dex_file_->FindTypeId(cls.c_str());
    DCHECK(type_id != nullptr);
    uint16_t index = dex_file_->GetIndexForTypeId(*type_id);
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    for (const auto& dex_dep : verifier_deps_->dex_deps_) {
      for (uint16_t entry : dex_dep.second->unverified_classes_) {
        if (index == entry) {
          return true;
        }
      }
    }
    return false;
  }

  // Iterates over all assignability records and tries to find an entry which
  // matches the expected destination/source pair.
  bool HasAssignable(const std::string& expected_destination,
                     const std::string& expected_source,
                     bool expected_is_assignable) {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    for (auto& dex_dep : verifier_deps_->dex_deps_) {
      const DexFile& dex_file = *dex_dep.first;
      auto& storage = expected_is_assignable ? dex_dep.second->assignable_types_
                                             : dex_dep.second->unassignable_types_;
      for (auto& entry : storage) {
        std::string actual_destination =
            verifier_deps_->GetStringFromId(dex_file, entry.GetDestination());
        std::string actual_source = verifier_deps_->GetStringFromId(dex_file, entry.GetSource());
        if ((expected_destination == actual_destination) && (expected_source == actual_source)) {
          return true;
        }
      }
    }
    return false;
  }

  // Iterates over all class resolution records, finds an entry which matches
  // the given class descriptor and tests its properties.
  bool HasClass(const std::string& expected_klass,
                bool expected_resolved,
                const std::string& expected_access_flags = "") {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    for (auto& dex_dep : verifier_deps_->dex_deps_) {
      for (auto& entry : dex_dep.second->classes_) {
        if (expected_resolved != entry.IsResolved()) {
          continue;
        }

        std::string actual_klass = dex_dep.first->StringByTypeIdx(entry.GetDexTypeIndex());
        if (expected_klass != actual_klass) {
          continue;
        }

        if (expected_resolved) {
          // Test access flags. Note that PrettyJavaAccessFlags always appends
          // a space after the modifiers. Add it to the expected access flags.
          std::string actual_access_flags = PrettyJavaAccessFlags(entry.GetAccessFlags());
          if (expected_access_flags + " " != actual_access_flags) {
            continue;
          }
        }

        return true;
      }
    }
    return false;
  }

  // Iterates over all field resolution records, finds an entry which matches
  // the given field class+name+type and tests its properties.
  bool HasField(const std::string& expected_klass,
                const std::string& expected_name,
                const std::string& expected_type,
                bool expected_resolved,
                const std::string& expected_access_flags = "",
                const std::string& expected_decl_klass = "") {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    for (auto& dex_dep : verifier_deps_->dex_deps_) {
      for (auto& entry : dex_dep.second->fields_) {
        if (expected_resolved != entry.IsResolved()) {
          continue;
        }

        const DexFile::FieldId& field_id = dex_dep.first->GetFieldId(entry.GetDexFieldIndex());

        std::string actual_klass = dex_dep.first->StringByTypeIdx(field_id.class_idx_);
        if (expected_klass != actual_klass) {
          continue;
        }

        std::string actual_name = dex_dep.first->StringDataByIdx(field_id.name_idx_);
        if (expected_name != actual_name) {
          continue;
        }

        std::string actual_type = dex_dep.first->StringByTypeIdx(field_id.type_idx_);
        if (expected_type != actual_type) {
          continue;
        }

        if (expected_resolved) {
          // Test access flags. Note that PrettyJavaAccessFlags always appends
          // a space after the modifiers. Add it to the expected access flags.
          std::string actual_access_flags = PrettyJavaAccessFlags(entry.GetAccessFlags());
          if (expected_access_flags + " " != actual_access_flags) {
            continue;
          }

          std::string actual_decl_klass = verifier_deps_->GetStringFromId(
              *dex_dep.first, entry.GetDeclaringClassIndex());
          if (expected_decl_klass != actual_decl_klass) {
            continue;
          }
        }

        return true;
      }
    }
    return false;
  }

  // Iterates over all method resolution records, finds an entry which matches
  // the given field kind+class+name+signature and tests its properties.
  bool HasMethod(const std::string& expected_kind,
                 const std::string& expected_klass,
                 const std::string& expected_name,
                 const std::string& expected_signature,
                 bool expected_resolved,
                 const std::string& expected_access_flags = "",
                 const std::string& expected_decl_klass = "") {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    for (auto& dex_dep : verifier_deps_->dex_deps_) {
      auto& storage = (expected_kind == "direct") ? dex_dep.second->direct_methods_
                          : (expected_kind == "virtual") ? dex_dep.second->virtual_methods_
                              : dex_dep.second->interface_methods_;
      for (auto& entry : storage) {
        if (expected_resolved != entry.IsResolved()) {
          continue;
        }

        const DexFile::MethodId& method_id = dex_dep.first->GetMethodId(entry.GetDexMethodIndex());

        std::string actual_klass = dex_dep.first->StringByTypeIdx(method_id.class_idx_);
        if (expected_klass != actual_klass) {
          continue;
        }

        std::string actual_name = dex_dep.first->StringDataByIdx(method_id.name_idx_);
        if (expected_name != actual_name) {
          continue;
        }

        std::string actual_signature = dex_dep.first->GetMethodSignature(method_id).ToString();
        if (expected_signature != actual_signature) {
          continue;
        }

        if (expected_resolved) {
          // Test access flags. Note that PrettyJavaAccessFlags always appends
          // a space after the modifiers. Add it to the expected access flags.
          std::string actual_access_flags = PrettyJavaAccessFlags(entry.GetAccessFlags());
          if (expected_access_flags + " " != actual_access_flags) {
            continue;
          }

          std::string actual_decl_klass = verifier_deps_->GetStringFromId(
              *dex_dep.first, entry.GetDeclaringClassIndex());
          if (expected_decl_klass != actual_decl_klass) {
            continue;
          }
        }

        return true;
      }
    }
    return false;
  }

  size_t NumberOfCompiledDexFiles() {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    return verifier_deps_->dex_deps_.size();
  }

  size_t HasEachKindOfRecord() {
    MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);

    bool has_strings = false;
    bool has_assignability = false;
    bool has_classes = false;
    bool has_fields = false;
    bool has_methods = false;
    bool has_unverified_classes = false;

    for (auto& entry : verifier_deps_->dex_deps_) {
      has_strings |= !entry.second->strings_.empty();
      has_assignability |= !entry.second->assignable_types_.empty();
      has_assignability |= !entry.second->unassignable_types_.empty();
      has_classes |= !entry.second->classes_.empty();
      has_fields |= !entry.second->fields_.empty();
      has_methods |= !entry.second->direct_methods_.empty();
      has_methods |= !entry.second->virtual_methods_.empty();
      has_methods |= !entry.second->interface_methods_.empty();
      has_unverified_classes |= !entry.second->unverified_classes_.empty();
    }

    return has_strings &&
           has_assignability &&
           has_classes &&
           has_fields &&
           has_methods &&
           has_unverified_classes;
  }

  std::unique_ptr<verifier::VerifierDeps> verifier_deps_;
  const DexFile* dex_file_;
  jobject class_loader_;
  mirror::Class* klass_Main_;
};

TEST_F(VerifierDepsTest, StringToId) {
  ScopedObjectAccess soa(Thread::Current());
  LoadDexFile(&soa);

  MutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);

  uint32_t id_Main1 = verifier_deps_->GetIdFromString(*dex_file_, "LMain;");
  ASSERT_LT(id_Main1, dex_file_->NumStringIds());
  ASSERT_EQ("LMain;", verifier_deps_->GetStringFromId(*dex_file_, id_Main1));

  uint32_t id_Main2 = verifier_deps_->GetIdFromString(*dex_file_, "LMain;");
  ASSERT_LT(id_Main2, dex_file_->NumStringIds());
  ASSERT_EQ("LMain;", verifier_deps_->GetStringFromId(*dex_file_, id_Main2));

  uint32_t id_Lorem1 = verifier_deps_->GetIdFromString(*dex_file_, "Lorem ipsum");
  ASSERT_GE(id_Lorem1, dex_file_->NumStringIds());
  ASSERT_EQ("Lorem ipsum", verifier_deps_->GetStringFromId(*dex_file_, id_Lorem1));

  uint32_t id_Lorem2 = verifier_deps_->GetIdFromString(*dex_file_, "Lorem ipsum");
  ASSERT_GE(id_Lorem2, dex_file_->NumStringIds());
  ASSERT_EQ("Lorem ipsum", verifier_deps_->GetStringFromId(*dex_file_, id_Lorem2));

  ASSERT_EQ(id_Main1, id_Main2);
  ASSERT_EQ(id_Lorem1, id_Lorem2);
  ASSERT_NE(id_Main1, id_Lorem1);
}

TEST_F(VerifierDepsTest, Assignable_BothInBoot) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/util/TimeZone;",
                                         /* src */ "Ljava/util/SimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;", true));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot1) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/net/Socket;",
                                         /* src */ "LMySSLSocket;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/net/Socket;", "LMySSLSocket;", true));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot2) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/util/TimeZone;",
                                         /* src */ "LMySimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "LMySimpleTimeZone;", true));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot3) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/util/Collection;",
                                         /* src */ "LMyThreadSet;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/util/Collection;", "LMyThreadSet;", true));
}

TEST_F(VerifierDepsTest, Assignable_BothArrays_Resolved) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "[[Ljava/util/TimeZone;",
                                         /* src */ "[[Ljava/util/SimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  // If the component types of both arrays are resolved, we optimize the list of
  // dependencies by recording a dependency on the component types.
  ASSERT_FALSE(HasAssignable("[[Ljava/util/TimeZone;", "[[Ljava/util/SimpleTimeZone;", true));
  ASSERT_FALSE(HasAssignable("[Ljava/util/TimeZone;", "[Ljava/util/SimpleTimeZone;", true));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;", true));
}

TEST_F(VerifierDepsTest, Assignable_BothArrays_Erroneous) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "[[Ljava/util/TimeZone;",
                                         /* src */ "[[LMyErroneousTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  // If the component type of an array is erroneous, we record the dependency on
  // the array type.
  ASSERT_FALSE(HasAssignable("[[Ljava/util/TimeZone;", "[[LMyErroneousTimeZone;", true));
  ASSERT_TRUE(HasAssignable("[Ljava/util/TimeZone;", "[LMyErroneousTimeZone;", true));
  ASSERT_FALSE(HasAssignable("Ljava/util/TimeZone;", "LMyErroneousTimeZone;", true));
}

  // We test that VerifierDeps does not try to optimize by storing assignability
  // of the component types. This is due to the fact that the component type may
  // be an erroneous class, even though the array type has resolved status.

TEST_F(VerifierDepsTest, Assignable_ArrayToInterface1) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/io/Serializable;",
                                         /* src */ "[Ljava/util/TimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/io/Serializable;", "[Ljava/util/TimeZone;", true));
}

TEST_F(VerifierDepsTest, Assignable_ArrayToInterface2) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/io/Serializable;",
                                         /* src */ "[LMyThreadSet;",
                                         /* is_strict */ true,
                                         /* is_assignable */ true));
  ASSERT_TRUE(HasAssignable("Ljava/io/Serializable;", "[LMyThreadSet;", true));
}

TEST_F(VerifierDepsTest, NotAssignable_BothInBoot) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/lang/Exception;",
                                         /* src */ "Ljava/util/SimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ false));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/util/SimpleTimeZone;", false));
}

TEST_F(VerifierDepsTest, NotAssignable_DestinationInBoot1) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/lang/Exception;",
                                         /* src */ "LMySSLSocket;",
                                         /* is_strict */ true,
                                         /* is_assignable */ false));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "LMySSLSocket;", false));
}

TEST_F(VerifierDepsTest, NotAssignable_DestinationInBoot2) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "Ljava/lang/Exception;",
                                         /* src */ "LMySimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ false));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "LMySimpleTimeZone;", false));
}

TEST_F(VerifierDepsTest, NotAssignable_BothArrays) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst */ "[Ljava/lang/Exception;",
                                         /* src */ "[Ljava/util/SimpleTimeZone;",
                                         /* is_strict */ true,
                                         /* is_assignable */ false));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/util/SimpleTimeZone;", false));
}

TEST_F(VerifierDepsTest, ArgumentType_ResolvedClass) {
  ASSERT_TRUE(VerifyMethod("ArgumentType_ResolvedClass"));
  ASSERT_TRUE(HasClass("Ljava/lang/Thread;", true, "public"));
}

TEST_F(VerifierDepsTest, ArgumentType_ResolvedReferenceArray) {
  ASSERT_TRUE(VerifyMethod("ArgumentType_ResolvedReferenceArray"));
  ASSERT_TRUE(HasClass("[Ljava/lang/Thread;", true, "public final abstract"));
}

TEST_F(VerifierDepsTest, ArgumentType_ResolvedPrimitiveArray) {
  ASSERT_TRUE(VerifyMethod("ArgumentType_ResolvedPrimitiveArray"));
  ASSERT_TRUE(HasClass("[B", true, "public final abstract"));
}

TEST_F(VerifierDepsTest, ArgumentType_UnresolvedClass) {
  ASSERT_TRUE(VerifyMethod("ArgumentType_UnresolvedClass"));
  ASSERT_TRUE(HasClass("LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, ArgumentType_UnresolvedSuper) {
  ASSERT_TRUE(VerifyMethod("ArgumentType_UnresolvedSuper"));
  ASSERT_TRUE(HasClass("LMySetWithUnresolvedSuper;", false));
}

TEST_F(VerifierDepsTest, ReturnType_Reference) {
  ASSERT_TRUE(VerifyMethod("ReturnType_Reference"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/lang/IllegalStateException;", true));
}

TEST_F(VerifierDepsTest, ReturnType_Array) {
  ASSERT_FALSE(VerifyMethod("ReturnType_Array"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Integer;", "Ljava/lang/IllegalStateException;", false));
}

TEST_F(VerifierDepsTest, InvokeArgumentType) {
  ASSERT_TRUE(VerifyMethod("InvokeArgumentType"));
  ASSERT_TRUE(HasClass("Ljava/text/SimpleDateFormat;", true, "public"));
  ASSERT_TRUE(HasClass("Ljava/util/SimpleTimeZone;", true, "public"));
  ASSERT_TRUE(HasMethod("virtual",
                        "Ljava/text/SimpleDateFormat;",
                        "setTimeZone",
                        "(Ljava/util/TimeZone;)V",
                        true,
                        "public",
                        "Ljava/text/DateFormat;"));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;", true));
}

TEST_F(VerifierDepsTest, MergeTypes_RegisterLines) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_RegisterLines"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "LMySocketTimeoutException;", true));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;", true));
}

TEST_F(VerifierDepsTest, MergeTypes_IfInstanceOf) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_IfInstanceOf"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/net/SocketTimeoutException;", true));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;", true));
  ASSERT_TRUE(HasAssignable("Ljava/net/SocketTimeoutException;", "Ljava/lang/Exception;", false));
}

TEST_F(VerifierDepsTest, MergeTypes_Unresolved) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_Unresolved"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/net/SocketTimeoutException;", true));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;", true));
}

TEST_F(VerifierDepsTest, ConstClass_Resolved) {
  ASSERT_TRUE(VerifyMethod("ConstClass_Resolved"));
  ASSERT_TRUE(HasClass("Ljava/lang/IllegalStateException;", true, "public"));
}

TEST_F(VerifierDepsTest, ConstClass_Unresolved) {
  ASSERT_TRUE(VerifyMethod("ConstClass_Unresolved"));
  ASSERT_TRUE(HasClass("LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, CheckCast_Resolved) {
  ASSERT_TRUE(VerifyMethod("CheckCast_Resolved"));
  ASSERT_TRUE(HasClass("Ljava/lang/IllegalStateException;", true, "public"));
}

TEST_F(VerifierDepsTest, CheckCast_Unresolved) {
  ASSERT_TRUE(VerifyMethod("CheckCast_Unresolved"));
  ASSERT_TRUE(HasClass("LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, InstanceOf_Resolved) {
  ASSERT_TRUE(VerifyMethod("InstanceOf_Resolved"));
  ASSERT_TRUE(HasClass("Ljava/lang/IllegalStateException;", true, "public"));
}

TEST_F(VerifierDepsTest, InstanceOf_Unresolved) {
  ASSERT_TRUE(VerifyMethod("InstanceOf_Unresolved"));
  ASSERT_TRUE(HasClass("LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, NewInstance_Resolved) {
  ASSERT_TRUE(VerifyMethod("NewInstance_Resolved"));
  ASSERT_TRUE(HasClass("Ljava/lang/IllegalStateException;", true, "public"));
}

TEST_F(VerifierDepsTest, NewInstance_Unresolved) {
  ASSERT_TRUE(VerifyMethod("NewInstance_Unresolved"));
  ASSERT_TRUE(HasClass("LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, NewArray_Resolved) {
  ASSERT_TRUE(VerifyMethod("NewArray_Resolved"));
  ASSERT_TRUE(HasClass("[Ljava/lang/IllegalStateException;", true, "public final abstract"));
}

TEST_F(VerifierDepsTest, NewArray_Unresolved) {
  ASSERT_TRUE(VerifyMethod("NewArray_Unresolved"));
  ASSERT_TRUE(HasClass("[LUnresolvedClass;", false));
}

TEST_F(VerifierDepsTest, Throw) {
  ASSERT_TRUE(VerifyMethod("Throw"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/lang/IllegalStateException;", true));
}

TEST_F(VerifierDepsTest, MoveException_Resolved) {
  ASSERT_TRUE(VerifyMethod("MoveException_Resolved"));
  ASSERT_TRUE(HasClass("Ljava/io/InterruptedIOException;", true, "public"));
  ASSERT_TRUE(HasClass("Ljava/net/SocketTimeoutException;", true, "public"));
  ASSERT_TRUE(HasClass("Ljava/util/zip/ZipException;", true, "public"));

  // Testing that all exception types are assignable to Throwable.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/io/InterruptedIOException;", true));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/net/SocketTimeoutException;", true));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/util/zip/ZipException;", true));

  // Testing that the merge type is assignable to Throwable.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/io/IOException;", true));

  // Merging of exception types.
  ASSERT_TRUE(HasAssignable("Ljava/io/IOException;", "Ljava/io/InterruptedIOException;", true));
  ASSERT_TRUE(HasAssignable("Ljava/io/IOException;", "Ljava/util/zip/ZipException;", true));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "Ljava/net/SocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, MoveException_Unresolved) {
  ASSERT_FALSE(VerifyMethod("MoveException_Unresolved"));
  ASSERT_TRUE(HasClass("LUnresolvedException;", false));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/lang/System;", true, "public final"));
  ASSERT_TRUE(HasField("Ljava/lang/System;",
                       "out",
                       "Ljava/io/PrintStream;",
                       true,
                       "public final static",
                       "Ljava/lang/System;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasClass("Ljava/util/SimpleTimeZone;", true, "public"));
  ASSERT_TRUE(HasField(
      "Ljava/util/SimpleTimeZone;", "LONG", "I", true, "public final static", "Ljava/util/TimeZone;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInSuperclass2) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasField(
      "LMySimpleTimeZone;", "SHORT", "I", true, "public final static", "Ljava/util/TimeZone;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInInterface1) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInInterface1"));
  ASSERT_TRUE(HasClass("Ljavax/xml/transform/dom/DOMResult;", true, "public"));
  ASSERT_TRUE(HasField("Ljavax/xml/transform/dom/DOMResult;",
                       "PI_ENABLE_OUTPUT_ESCAPING",
                       "Ljava/lang/String;",
                       true,
                       "public final static",
                       "Ljavax/xml/transform/Result;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInInterface2) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInInterface2"));
  ASSERT_TRUE(HasField("LMyDOMResult;",
                       "PI_ENABLE_OUTPUT_ESCAPING",
                       "Ljava/lang/String;",
                       true,
                       "public final static",
                       "Ljavax/xml/transform/Result;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInInterface3) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInInterface3"));
  ASSERT_TRUE(HasField("LMyResult;",
                       "PI_ENABLE_OUTPUT_ESCAPING",
                       "Ljava/lang/String;",
                       true,
                       "public final static",
                       "Ljavax/xml/transform/Result;"));
}

TEST_F(VerifierDepsTest, StaticField_Resolved_DeclaredInInterface4) {
  ASSERT_TRUE(VerifyMethod("StaticField_Resolved_DeclaredInInterface4"));
  ASSERT_TRUE(HasField("LMyDocument;",
                       "ELEMENT_NODE",
                       "S",
                       true,
                       "public final static",
                       "Lorg/w3c/dom/Node;"));
}

TEST_F(VerifierDepsTest, StaticField_Unresolved_ReferrerInBoot) {
  ASSERT_TRUE(VerifyMethod("StaticField_Unresolved_ReferrerInBoot"));
  ASSERT_TRUE(HasClass("Ljava/util/TimeZone;", true, "public abstract"));
  ASSERT_TRUE(HasField("Ljava/util/TimeZone;", "x", "I", false));
}

TEST_F(VerifierDepsTest, StaticField_Unresolved_ReferrerInDex) {
  ASSERT_TRUE(VerifyMethod("StaticField_Unresolved_ReferrerInDex"));
  ASSERT_TRUE(HasField("LMyThreadSet;", "x", "I", false));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/io/InterruptedIOException;", true, "public"));
  ASSERT_TRUE(HasField("Ljava/io/InterruptedIOException;",
                       "bytesTransferred",
                       "I",
                       true,
                       "public",
                       "Ljava/io/InterruptedIOException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "LMySocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasClass("Ljava/net/SocketTimeoutException;", true, "public"));
  ASSERT_TRUE(HasField("Ljava/net/SocketTimeoutException;",
                       "bytesTransferred",
                       "I",
                       true,
                       "public",
                       "Ljava/io/InterruptedIOException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "LMySocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInSuperclass2) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasField("LMySocketTimeoutException;",
                       "bytesTransferred",
                       "I",
                       true,
                       "public",
                       "Ljava/io/InterruptedIOException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "LMySocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, InstanceField_Unresolved_ReferrerInBoot) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Unresolved_ReferrerInBoot"));
  ASSERT_TRUE(HasClass("Ljava/io/InterruptedIOException;", true, "public"));
  ASSERT_TRUE(HasField("Ljava/io/InterruptedIOException;", "x", "I", false));
}

TEST_F(VerifierDepsTest, InstanceField_Unresolved_ReferrerInDex) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Unresolved_ReferrerInDex"));
  ASSERT_TRUE(HasField("LMyThreadSet;", "x", "I", false));
}

TEST_F(VerifierDepsTest, InvokeStatic_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InvokeStatic_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/net/Socket;", true, "public"));
  ASSERT_TRUE(HasMethod("direct",
                        "Ljava/net/Socket;",
                        "setSocketImplFactory",
                        "(Ljava/net/SocketImplFactory;)V",
                        true,
                        "public static",
                        "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeStatic_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("InvokeStatic_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasClass("Ljavax/net/ssl/SSLSocket;", true, "public abstract"));
  ASSERT_TRUE(HasMethod("direct",
                        "Ljavax/net/ssl/SSLSocket;",
                        "setSocketImplFactory",
                        "(Ljava/net/SocketImplFactory;)V",
                        true,
                        "public static",
                        "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeStatic_Resolved_DeclaredInSuperclass2) {
  ASSERT_TRUE(VerifyMethod("InvokeStatic_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasMethod("direct",
                        "LMySSLSocket;",
                        "setSocketImplFactory",
                        "(Ljava/net/SocketImplFactory;)V",
                        true,
                        "public static",
                        "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeStatic_DeclaredInInterface1) {
  ASSERT_TRUE(VerifyMethod("InvokeStatic_DeclaredInInterface1"));
  ASSERT_TRUE(HasClass("Ljava/util/Map$Entry;", true, "public abstract interface"));
  ASSERT_TRUE(HasMethod("direct",
                        "Ljava/util/Map$Entry;",
                        "comparingByKey",
                        "()Ljava/util/Comparator;",
                        true,
                        "public static",
                        "Ljava/util/Map$Entry;"));
}

TEST_F(VerifierDepsTest, InvokeStatic_DeclaredInInterface2) {
  ASSERT_FALSE(VerifyMethod("InvokeStatic_DeclaredInInterface2"));
  ASSERT_TRUE(HasClass("Ljava/util/AbstractMap$SimpleEntry;", true, "public"));
  ASSERT_TRUE(HasMethod("direct",
                        "Ljava/util/AbstractMap$SimpleEntry;",
                        "comparingByKey",
                        "()Ljava/util/Comparator;",
                        false));
}

TEST_F(VerifierDepsTest, InvokeStatic_Unresolved1) {
  ASSERT_FALSE(VerifyMethod("InvokeStatic_Unresolved1"));
  ASSERT_TRUE(HasClass("Ljavax/net/ssl/SSLSocket;", true, "public abstract"));
  ASSERT_TRUE(HasMethod("direct", "Ljavax/net/ssl/SSLSocket;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeStatic_Unresolved2) {
  ASSERT_FALSE(VerifyMethod("InvokeStatic_Unresolved2"));
  ASSERT_TRUE(HasMethod("direct", "LMySSLSocket;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeDirect_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InvokeDirect_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/net/Socket;", true, "public"));
  ASSERT_TRUE(HasMethod(
      "direct", "Ljava/net/Socket;", "<init>", "()V", true, "public", "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeDirect_Resolved_DeclaredInSuperclass1) {
  ASSERT_FALSE(VerifyMethod("InvokeDirect_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasClass("Ljavax/net/ssl/SSLSocket;", true, "public abstract"));
  ASSERT_TRUE(HasMethod("direct",
                        "Ljavax/net/ssl/SSLSocket;",
                        "checkOldImpl",
                        "()V",
                        true,
                        "private",
                        "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeDirect_Resolved_DeclaredInSuperclass2) {
  ASSERT_FALSE(VerifyMethod("InvokeDirect_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasMethod(
      "direct", "LMySSLSocket;", "checkOldImpl", "()V", true, "private", "Ljava/net/Socket;"));
}

TEST_F(VerifierDepsTest, InvokeDirect_Unresolved1) {
  ASSERT_FALSE(VerifyMethod("InvokeDirect_Unresolved1"));
  ASSERT_TRUE(HasClass("Ljavax/net/ssl/SSLSocket;", true, "public abstract"));
  ASSERT_TRUE(HasMethod("direct", "Ljavax/net/ssl/SSLSocket;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeDirect_Unresolved2) {
  ASSERT_FALSE(VerifyMethod("InvokeDirect_Unresolved2"));
  ASSERT_TRUE(HasMethod("direct", "LMySSLSocket;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/lang/Throwable;", true, "public"));
  ASSERT_TRUE(HasMethod("virtual",
                        "Ljava/lang/Throwable;",
                        "getMessage",
                        "()Ljava/lang/String;",
                        true,
                        "public",
                        "Ljava/lang/Throwable;"));
  // Type dependency on `this` argument.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "LMySocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasClass("Ljava/io/InterruptedIOException;", true, "public"));
  ASSERT_TRUE(HasMethod("virtual",
                        "Ljava/io/InterruptedIOException;",
                        "getMessage",
                        "()Ljava/lang/String;",
                        true,
                        "public",
                        "Ljava/lang/Throwable;"));
  // Type dependency on `this` argument.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "LMySocketTimeoutException;", true));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInSuperclass2) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasMethod("virtual",
                        "LMySocketTimeoutException;",
                        "getMessage",
                        "()Ljava/lang/String;",
                        true,
                        "public",
                        "Ljava/lang/Throwable;"));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInSuperinterface) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInSuperinterface"));
  ASSERT_TRUE(HasMethod("virtual",
                        "LMyThreadSet;",
                        "size",
                        "()I",
                        true,
                        "public abstract",
                        "Ljava/util/Set;"));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Unresolved1) {
  ASSERT_FALSE(VerifyMethod("InvokeVirtual_Unresolved1"));
  ASSERT_TRUE(HasClass("Ljava/io/InterruptedIOException;", true, "public"));
  ASSERT_TRUE(HasMethod("virtual", "Ljava/io/InterruptedIOException;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Unresolved2) {
  ASSERT_FALSE(VerifyMethod("InvokeVirtual_Unresolved2"));
  ASSERT_TRUE(HasMethod("virtual", "LMySocketTimeoutException;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeVirtual_ActuallyDirect) {
  ASSERT_FALSE(VerifyMethod("InvokeVirtual_ActuallyDirect"));
  ASSERT_TRUE(HasMethod("virtual", "LMyThread;", "activeCount", "()I", false));
  ASSERT_TRUE(HasMethod("direct",
                        "LMyThread;",
                        "activeCount",
                        "()I",
                        true,
                        "public static",
                        "Ljava/lang/Thread;"));
}

TEST_F(VerifierDepsTest, InvokeInterface_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InvokeInterface_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasClass("Ljava/lang/Runnable;", true, "public abstract interface"));
  ASSERT_TRUE(HasMethod("interface",
                        "Ljava/lang/Runnable;",
                        "run",
                        "()V",
                        true,
                        "public abstract",
                        "Ljava/lang/Runnable;"));
}

TEST_F(VerifierDepsTest, InvokeInterface_Resolved_DeclaredInSuperclass) {
  ASSERT_FALSE(VerifyMethod("InvokeInterface_Resolved_DeclaredInSuperclass"));
  ASSERT_TRUE(HasMethod("interface", "LMyThread;", "join", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeInterface_Resolved_DeclaredInSuperinterface1) {
  ASSERT_FALSE(VerifyMethod("InvokeInterface_Resolved_DeclaredInSuperinterface1"));
  ASSERT_TRUE(HasMethod("interface",
                        "LMyThreadSet;",
                        "run",
                        "()V",
                        true,
                        "public abstract",
                        "Ljava/lang/Runnable;"));
}

TEST_F(VerifierDepsTest, InvokeInterface_Resolved_DeclaredInSuperinterface2) {
  ASSERT_FALSE(VerifyMethod("InvokeInterface_Resolved_DeclaredInSuperinterface2"));
  ASSERT_TRUE(HasMethod("interface",
                        "LMyThreadSet;",
                        "isEmpty",
                        "()Z",
                        true,
                        "public abstract",
                        "Ljava/util/Set;"));
}

TEST_F(VerifierDepsTest, InvokeInterface_Unresolved1) {
  ASSERT_FALSE(VerifyMethod("InvokeInterface_Unresolved1"));
  ASSERT_TRUE(HasClass("Ljava/lang/Runnable;", true, "public abstract interface"));
  ASSERT_TRUE(HasMethod("interface", "Ljava/lang/Runnable;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeInterface_Unresolved2) {
  ASSERT_FALSE(VerifyMethod("InvokeInterface_Unresolved2"));
  ASSERT_TRUE(HasMethod("interface", "LMyThreadSet;", "x", "()V", false));
}

TEST_F(VerifierDepsTest, InvokeSuper_ThisAssignable) {
  ASSERT_TRUE(VerifyMethod("InvokeSuper_ThisAssignable"));
  ASSERT_TRUE(HasClass("Ljava/lang/Runnable;", true, "public abstract interface"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Runnable;", "LMain;", true));
  ASSERT_TRUE(HasMethod("interface",
                        "Ljava/lang/Runnable;",
                        "run",
                        "()V",
                        true,
                        "public abstract",
                        "Ljava/lang/Runnable;"));
}

TEST_F(VerifierDepsTest, InvokeSuper_ThisNotAssignable) {
  ASSERT_FALSE(VerifyMethod("InvokeSuper_ThisNotAssignable"));
  ASSERT_TRUE(HasClass("Ljava/lang/Integer;", true, "public final"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Integer;", "LMain;", false));
  ASSERT_TRUE(HasMethod(
      "virtual", "Ljava/lang/Integer;", "intValue", "()I", true, "public", "Ljava/lang/Integer;"));
}

TEST_F(VerifierDepsTest, EncodeDecode) {
  VerifyDexFile();

  ASSERT_EQ(1u, NumberOfCompiledDexFiles());
  ASSERT_TRUE(HasEachKindOfRecord());

  std::vector<uint8_t> buffer;
  verifier_deps_->Encode(&buffer);
  ASSERT_FALSE(buffer.empty());

  VerifierDeps decoded_deps({ dex_file_ }, ArrayRef<uint8_t>(buffer));
  ASSERT_TRUE(verifier_deps_->Equals(decoded_deps));
}

TEST_F(VerifierDepsTest, UnverifiedClasses) {
  VerifyDexFile();
  ASSERT_FALSE(HasUnverifiedClass("LMyThread;"));
  // Test that a class with a soft failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMain;"));
  // Test that a class with hard failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMyVerificationFailure;"));
  // Test that a class with unresolved super is recorded.
  ASSERT_FALSE(HasUnverifiedClass("LMyClassWithNoSuper;"));
  // Test that a class with unresolved super and hard failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMyClassWithNoSuperButFailures;"));
}

}  // namespace verifier
}  // namespace art
