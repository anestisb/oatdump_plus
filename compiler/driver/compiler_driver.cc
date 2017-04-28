/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "compiler_driver.h"

#include <unordered_set>
#include <vector>
#include <unistd.h>

#ifndef __APPLE__
#include <malloc.h>  // For mallinfo
#endif

#include "android-base/strings.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/array_ref.h"
#include "base/bit_vector.h"
#include "base/enums.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/timing_logger.h"
#include "class_linker-inl.h"
#include "compiled_class.h"
#include "compiled_method.h"
#include "compiler.h"
#include "compiler_callbacks.h"
#include "compiler_driver-inl.h"
#include "dex_compilation_unit.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "dex/dex_to_dex_compiler.h"
#include "dex/verification_results.h"
#include "dex/verified_method.h"
#include "driver/compiler_options.h"
#include "intrinsics_enum.h"
#include "jni_internal.h"
#include "object_lock.h"
#include "runtime.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc/space/image_space.h"
#include "gc/space/space.h"
#include "mirror/class_loader.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/throwable.h"
#include "scoped_thread_state_change-inl.h"
#include "ScopedLocalRef.h"
#include "handle_scope-inl.h"
#include "thread.h"
#include "thread_list.h"
#include "thread_pool.h"
#include "trampolines/trampoline_compiler.h"
#include "transaction.h"
#include "utils/atomic_method_ref_map-inl.h"
#include "utils/dex_cache_arrays_layout-inl.h"
#include "utils/swap_space.h"
#include "vdex_file.h"
#include "verifier/method_verifier.h"
#include "verifier/method_verifier-inl.h"
#include "verifier/verifier_deps.h"
#include "verifier/verifier_enums.h"

namespace art {

static constexpr bool kTimeCompileMethod = !kIsDebugBuild;

// Print additional info during profile guided compilation.
static constexpr bool kDebugProfileGuidedCompilation = false;

static double Percentage(size_t x, size_t y) {
  return 100.0 * (static_cast<double>(x)) / (static_cast<double>(x + y));
}

static void DumpStat(size_t x, size_t y, const char* str) {
  if (x == 0 && y == 0) {
    return;
  }
  LOG(INFO) << Percentage(x, y) << "% of " << str << " for " << (x + y) << " cases";
}

class CompilerDriver::AOTCompilationStats {
 public:
  AOTCompilationStats()
      : stats_lock_("AOT compilation statistics lock"),
        resolved_types_(0), unresolved_types_(0),
        resolved_instance_fields_(0), unresolved_instance_fields_(0),
        resolved_local_static_fields_(0), resolved_static_fields_(0), unresolved_static_fields_(0),
        type_based_devirtualization_(0),
        safe_casts_(0), not_safe_casts_(0) {
    for (size_t i = 0; i <= kMaxInvokeType; i++) {
      resolved_methods_[i] = 0;
      unresolved_methods_[i] = 0;
      virtual_made_direct_[i] = 0;
      direct_calls_to_boot_[i] = 0;
      direct_methods_to_boot_[i] = 0;
    }
  }

  void Dump() {
    DumpStat(resolved_types_, unresolved_types_, "types resolved");
    DumpStat(resolved_instance_fields_, unresolved_instance_fields_, "instance fields resolved");
    DumpStat(resolved_local_static_fields_ + resolved_static_fields_, unresolved_static_fields_,
             "static fields resolved");
    DumpStat(resolved_local_static_fields_, resolved_static_fields_ + unresolved_static_fields_,
             "static fields local to a class");
    DumpStat(safe_casts_, not_safe_casts_, "check-casts removed based on type information");
    // Note, the code below subtracts the stat value so that when added to the stat value we have
    // 100% of samples. TODO: clean this up.
    DumpStat(type_based_devirtualization_,
             resolved_methods_[kVirtual] + unresolved_methods_[kVirtual] +
             resolved_methods_[kInterface] + unresolved_methods_[kInterface] -
             type_based_devirtualization_,
             "virtual/interface calls made direct based on type information");

    for (size_t i = 0; i <= kMaxInvokeType; i++) {
      std::ostringstream oss;
      oss << static_cast<InvokeType>(i) << " methods were AOT resolved";
      DumpStat(resolved_methods_[i], unresolved_methods_[i], oss.str().c_str());
      if (virtual_made_direct_[i] > 0) {
        std::ostringstream oss2;
        oss2 << static_cast<InvokeType>(i) << " methods made direct";
        DumpStat(virtual_made_direct_[i],
                 resolved_methods_[i] + unresolved_methods_[i] - virtual_made_direct_[i],
                 oss2.str().c_str());
      }
      if (direct_calls_to_boot_[i] > 0) {
        std::ostringstream oss2;
        oss2 << static_cast<InvokeType>(i) << " method calls are direct into boot";
        DumpStat(direct_calls_to_boot_[i],
                 resolved_methods_[i] + unresolved_methods_[i] - direct_calls_to_boot_[i],
                 oss2.str().c_str());
      }
      if (direct_methods_to_boot_[i] > 0) {
        std::ostringstream oss2;
        oss2 << static_cast<InvokeType>(i) << " method calls have methods in boot";
        DumpStat(direct_methods_to_boot_[i],
                 resolved_methods_[i] + unresolved_methods_[i] - direct_methods_to_boot_[i],
                 oss2.str().c_str());
      }
    }
  }

// Allow lossy statistics in non-debug builds.
#ifndef NDEBUG
#define STATS_LOCK() MutexLock mu(Thread::Current(), stats_lock_)
#else
#define STATS_LOCK()
#endif

  void TypeDoesntNeedAccessCheck() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    resolved_types_++;
  }

  void TypeNeedsAccessCheck() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    unresolved_types_++;
  }

  void ResolvedInstanceField() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    resolved_instance_fields_++;
  }

  void UnresolvedInstanceField() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    unresolved_instance_fields_++;
  }

  void ResolvedLocalStaticField() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    resolved_local_static_fields_++;
  }

  void ResolvedStaticField() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    resolved_static_fields_++;
  }

  void UnresolvedStaticField() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    unresolved_static_fields_++;
  }

  // Indicate that type information from the verifier led to devirtualization.
  void PreciseTypeDevirtualization() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    type_based_devirtualization_++;
  }

  // A check-cast could be eliminated due to verifier type analysis.
  void SafeCast() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    safe_casts_++;
  }

  // A check-cast couldn't be eliminated due to verifier type analysis.
  void NotASafeCast() REQUIRES(!stats_lock_) {
    STATS_LOCK();
    not_safe_casts_++;
  }

 private:
  Mutex stats_lock_;

  size_t resolved_types_;
  size_t unresolved_types_;

  size_t resolved_instance_fields_;
  size_t unresolved_instance_fields_;

  size_t resolved_local_static_fields_;
  size_t resolved_static_fields_;
  size_t unresolved_static_fields_;
  // Type based devirtualization for invoke interface and virtual.
  size_t type_based_devirtualization_;

  size_t resolved_methods_[kMaxInvokeType + 1];
  size_t unresolved_methods_[kMaxInvokeType + 1];
  size_t virtual_made_direct_[kMaxInvokeType + 1];
  size_t direct_calls_to_boot_[kMaxInvokeType + 1];
  size_t direct_methods_to_boot_[kMaxInvokeType + 1];

  size_t safe_casts_;
  size_t not_safe_casts_;

  DISALLOW_COPY_AND_ASSIGN(AOTCompilationStats);
};

class CompilerDriver::DexFileMethodSet {
 public:
  explicit DexFileMethodSet(const DexFile& dex_file)
    : dex_file_(dex_file),
      method_indexes_(dex_file.NumMethodIds(), false, Allocator::GetMallocAllocator()) {
  }
  DexFileMethodSet(DexFileMethodSet&& other) = default;

  const DexFile& GetDexFile() const { return dex_file_; }

  BitVector& GetMethodIndexes() { return method_indexes_; }
  const BitVector& GetMethodIndexes() const { return method_indexes_; }

 private:
  const DexFile& dex_file_;
  BitVector method_indexes_;
};

CompilerDriver::CompilerDriver(
    const CompilerOptions* compiler_options,
    VerificationResults* verification_results,
    Compiler::Kind compiler_kind,
    InstructionSet instruction_set,
    const InstructionSetFeatures* instruction_set_features,
    std::unordered_set<std::string>* image_classes,
    std::unordered_set<std::string>* compiled_classes,
    std::unordered_set<std::string>* compiled_methods,
    size_t thread_count,
    bool dump_stats,
    bool dump_passes,
    CumulativeLogger* timer,
    int swap_fd,
    const ProfileCompilationInfo* profile_compilation_info)
    : compiler_options_(compiler_options),
      verification_results_(verification_results),
      compiler_(Compiler::Create(this, compiler_kind)),
      compiler_kind_(compiler_kind),
      instruction_set_(instruction_set == kArm ? kThumb2 : instruction_set),
      instruction_set_features_(instruction_set_features),
      requires_constructor_barrier_lock_("constructor barrier lock"),
      compiled_classes_lock_("compiled classes lock"),
      non_relative_linker_patch_count_(0u),
      image_classes_(image_classes),
      classes_to_compile_(compiled_classes),
      methods_to_compile_(compiled_methods),
      had_hard_verifier_failure_(false),
      parallel_thread_count_(thread_count),
      stats_(new AOTCompilationStats),
      dump_stats_(dump_stats),
      dump_passes_(dump_passes),
      timings_logger_(timer),
      compiler_context_(nullptr),
      support_boot_image_fixup_(true),
      dex_files_for_oat_file_(nullptr),
      compiled_method_storage_(swap_fd),
      profile_compilation_info_(profile_compilation_info),
      max_arena_alloc_(0),
      dex_to_dex_references_lock_("dex-to-dex references lock"),
      dex_to_dex_references_(),
      current_dex_to_dex_methods_(nullptr) {
  DCHECK(compiler_options_ != nullptr);

  compiler_->Init();

  if (GetCompilerOptions().IsBootImage()) {
    CHECK(image_classes_.get() != nullptr) << "Expected image classes for boot image";
  }
}

CompilerDriver::~CompilerDriver() {
  Thread* self = Thread::Current();
  {
    MutexLock mu(self, compiled_classes_lock_);
    STLDeleteValues(&compiled_classes_);
  }
  compiled_methods_.Visit([this](const MethodReference& ref ATTRIBUTE_UNUSED,
                                 CompiledMethod* method) {
    if (method != nullptr) {
      CompiledMethod::ReleaseSwapAllocatedCompiledMethod(this, method);
    }
  });
  compiler_->UnInit();
}


#define CREATE_TRAMPOLINE(type, abi, offset) \
    if (Is64BitInstructionSet(instruction_set_)) { \
      return CreateTrampoline64(instruction_set_, abi, \
                                type ## _ENTRYPOINT_OFFSET(PointerSize::k64, offset)); \
    } else { \
      return CreateTrampoline32(instruction_set_, abi, \
                                type ## _ENTRYPOINT_OFFSET(PointerSize::k32, offset)); \
    }

std::unique_ptr<const std::vector<uint8_t>> CompilerDriver::CreateJniDlsymLookup() const {
  CREATE_TRAMPOLINE(JNI, kJniAbi, pDlsymLookup)
}

std::unique_ptr<const std::vector<uint8_t>> CompilerDriver::CreateQuickGenericJniTrampoline()
    const {
  CREATE_TRAMPOLINE(QUICK, kQuickAbi, pQuickGenericJniTrampoline)
}

std::unique_ptr<const std::vector<uint8_t>> CompilerDriver::CreateQuickImtConflictTrampoline()
    const {
  CREATE_TRAMPOLINE(QUICK, kQuickAbi, pQuickImtConflictTrampoline)
}

std::unique_ptr<const std::vector<uint8_t>> CompilerDriver::CreateQuickResolutionTrampoline()
    const {
  CREATE_TRAMPOLINE(QUICK, kQuickAbi, pQuickResolutionTrampoline)
}

std::unique_ptr<const std::vector<uint8_t>> CompilerDriver::CreateQuickToInterpreterBridge()
    const {
  CREATE_TRAMPOLINE(QUICK, kQuickAbi, pQuickToInterpreterBridge)
}
#undef CREATE_TRAMPOLINE

static void SetupIntrinsic(Thread* self,
                           Intrinsics intrinsic,
                           InvokeType invoke_type,
                           const char* class_name,
                           const char* method_name,
                           const char* signature)
      REQUIRES_SHARED(Locks::mutator_lock_) {
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  PointerSize image_size = class_linker->GetImagePointerSize();
  mirror::Class* cls = class_linker->FindSystemClass(self, class_name);
  if (cls == nullptr) {
    LOG(FATAL) << "Could not find class of intrinsic " << class_name;
  }
  ArtMethod* method = (invoke_type == kStatic || invoke_type == kDirect)
      ? cls->FindDeclaredDirectMethod(method_name, signature, image_size)
      : cls->FindDeclaredVirtualMethod(method_name, signature, image_size);
  if (method == nullptr) {
    LOG(FATAL) << "Could not find method of intrinsic "
               << class_name << " " << method_name << " " << signature;
  }
  DCHECK_EQ(method->GetInvokeType(), invoke_type);
  method->SetIntrinsic(static_cast<uint32_t>(intrinsic));
}

void CompilerDriver::CompileAll(jobject class_loader,
                                const std::vector<const DexFile*>& dex_files,
                                TimingLogger* timings) {
  DCHECK(!Runtime::Current()->IsStarted());

  InitializeThreadPools();

  VLOG(compiler) << "Before precompile " << GetMemoryUsageString(false);
  // Precompile:
  // 1) Load image classes
  // 2) Resolve all classes
  // 3) Attempt to verify all classes
  // 4) Attempt to initialize image classes, and trivially initialized classes
  PreCompile(class_loader, dex_files, timings);
  if (GetCompilerOptions().IsBootImage()) {
    // We don't need to setup the intrinsics for non boot image compilation, as
    // those compilations will pick up a boot image that have the ArtMethod already
    // set with the intrinsics flag.
    ScopedObjectAccess soa(Thread::Current());
#define SETUP_INTRINSICS(Name, InvokeType, NeedsEnvironmentOrCache, SideEffects, Exceptions, \
                         ClassName, MethodName, Signature) \
  SetupIntrinsic(soa.Self(), Intrinsics::k##Name, InvokeType, ClassName, MethodName, Signature);
#include "intrinsics_list.h"
INTRINSICS_LIST(SETUP_INTRINSICS)
#undef INTRINSICS_LIST
#undef SETUP_INTRINSICS
  }
  // Compile:
  // 1) Compile all classes and methods enabled for compilation. May fall back to dex-to-dex
  //    compilation.
  if (GetCompilerOptions().IsAnyCompilationEnabled()) {
    Compile(class_loader, dex_files, timings);
  }
  if (dump_stats_) {
    stats_->Dump();
  }

  FreeThreadPools();
}

void CompilerDriver::CompileAll(jobject class_loader,
                                const std::vector<const DexFile*>& dex_files,
                                VdexFile* vdex_file,
                                TimingLogger* timings) {
  if (vdex_file != nullptr) {
    // TODO: we unquicken unconditionnally, as we don't know
    // if the boot image has changed. How exactly we'll know is under
    // experimentation.
    TimingLogger::ScopedTiming t("Unquicken", timings);
    // We do not decompile a RETURN_VOID_NO_BARRIER into a RETURN_VOID, as the quickening
    // optimization does not depend on the boot image (the optimization relies on not
    // having final fields in a class, which does not change for an app).
    VdexFile::Unquicken(dex_files, vdex_file->GetQuickeningInfo());

    Runtime::Current()->GetCompilerCallbacks()->SetVerifierDeps(
        new verifier::VerifierDeps(dex_files, vdex_file->GetVerifierDepsData()));
  }
  CompileAll(class_loader, dex_files, timings);
}

static optimizer::DexToDexCompilationLevel GetDexToDexCompilationLevel(
    Thread* self, const CompilerDriver& driver, Handle<mirror::ClassLoader> class_loader,
    const DexFile& dex_file, const DexFile::ClassDef& class_def)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  auto* const runtime = Runtime::Current();
  DCHECK(driver.GetCompilerOptions().IsQuickeningCompilationEnabled());
  const char* descriptor = dex_file.GetClassDescriptor(class_def);
  ClassLinker* class_linker = runtime->GetClassLinker();
  mirror::Class* klass = class_linker->FindClass(self, descriptor, class_loader);
  if (klass == nullptr) {
    CHECK(self->IsExceptionPending());
    self->ClearException();
    return optimizer::DexToDexCompilationLevel::kDontDexToDexCompile;
  }
  // DexToDex at the kOptimize level may introduce quickened opcodes, which replace symbolic
  // references with actual offsets. We cannot re-verify such instructions.
  //
  // We store the verification information in the class status in the oat file, which the linker
  // can validate (checksums) and use to skip load-time verification. It is thus safe to
  // optimize when a class has been fully verified before.
  optimizer::DexToDexCompilationLevel max_level = optimizer::DexToDexCompilationLevel::kOptimize;
  if (driver.GetCompilerOptions().GetDebuggable()) {
    // We are debuggable so definitions of classes might be changed. We don't want to do any
    // optimizations that could break that.
    max_level = optimizer::DexToDexCompilationLevel::kDontDexToDexCompile;
  }
  if (klass->IsVerified()) {
    // Class is verified so we can enable DEX-to-DEX compilation for performance.
    return max_level;
  } else {
    // Class verification has failed: do not run DEX-to-DEX optimizations.
    return optimizer::DexToDexCompilationLevel::kDontDexToDexCompile;
  }
}

static optimizer::DexToDexCompilationLevel GetDexToDexCompilationLevel(
    Thread* self,
    const CompilerDriver& driver,
    jobject jclass_loader,
    const DexFile& dex_file,
    const DexFile::ClassDef& class_def) {
  ScopedObjectAccess soa(self);
  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
  return GetDexToDexCompilationLevel(self, driver, class_loader, dex_file, class_def);
}

// Does the runtime for the InstructionSet provide an implementation returned by
// GetQuickGenericJniStub allowing down calls that aren't compiled using a JNI compiler?
static bool InstructionSetHasGenericJniStub(InstructionSet isa) {
  switch (isa) {
    case kArm:
    case kArm64:
    case kThumb2:
    case kMips:
    case kMips64:
    case kX86:
    case kX86_64: return true;
    default: return false;
  }
}

static void CompileMethod(Thread* self,
                          CompilerDriver* driver,
                          const DexFile::CodeItem* code_item,
                          uint32_t access_flags,
                          InvokeType invoke_type,
                          uint16_t class_def_idx,
                          uint32_t method_idx,
                          Handle<mirror::ClassLoader> class_loader,
                          const DexFile& dex_file,
                          optimizer::DexToDexCompilationLevel dex_to_dex_compilation_level,
                          bool compilation_enabled,
                          Handle<mirror::DexCache> dex_cache) {
  DCHECK(driver != nullptr);
  CompiledMethod* compiled_method = nullptr;
  uint64_t start_ns = kTimeCompileMethod ? NanoTime() : 0;
  MethodReference method_ref(&dex_file, method_idx);

  if (driver->GetCurrentDexToDexMethods() != nullptr) {
    // This is the second pass when we dex-to-dex compile previously marked methods.
    // TODO: Refactor the compilation to avoid having to distinguish the two passes
    // here. That should be done on a higher level. http://b/29089975
    if (driver->GetCurrentDexToDexMethods()->IsBitSet(method_idx)) {
      const VerifiedMethod* verified_method =
          driver->GetVerificationResults()->GetVerifiedMethod(method_ref);
      // Do not optimize if a VerifiedMethod is missing. SafeCast elision,
      // for example, relies on it.
      compiled_method = optimizer::ArtCompileDEX(
          driver,
          code_item,
          access_flags,
          invoke_type,
          class_def_idx,
          method_idx,
          class_loader,
          dex_file,
          (verified_method != nullptr)
              ? dex_to_dex_compilation_level
              : optimizer::DexToDexCompilationLevel::kDontDexToDexCompile);
    }
  } else if ((access_flags & kAccNative) != 0) {
    // Are we extracting only and have support for generic JNI down calls?
    if (!driver->GetCompilerOptions().IsJniCompilationEnabled() &&
        InstructionSetHasGenericJniStub(driver->GetInstructionSet())) {
      // Leaving this empty will trigger the generic JNI version
    } else {
      // Look-up the ArtMethod associated with this code_item (if any)
      // -- It is later used to lookup any [optimization] annotations for this method.
      ScopedObjectAccess soa(self);

      // TODO: Lookup annotation from DexFile directly without resolving method.
      ArtMethod* method =
          Runtime::Current()->GetClassLinker()->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
              dex_file,
              method_idx,
              dex_cache,
              class_loader,
              /* referrer */ nullptr,
              invoke_type);

      // Query any JNI optimization annotations such as @FastNative or @CriticalNative.
      Compiler::JniOptimizationFlags optimization_flags = Compiler::kNone;
      if (UNLIKELY(method == nullptr)) {
        // Failed method resolutions happen very rarely, e.g. ancestor class cannot be resolved.
        DCHECK(self->IsExceptionPending());
        self->ClearException();
      } else if (method->IsAnnotatedWithFastNative()) {
        // TODO: Will no longer need this CHECK once we have verifier checking this.
        CHECK(!method->IsAnnotatedWithCriticalNative());
        optimization_flags = Compiler::kFastNative;
      } else if (method->IsAnnotatedWithCriticalNative()) {
        // TODO: Will no longer need this CHECK once we have verifier checking this.
        CHECK(!method->IsAnnotatedWithFastNative());
        optimization_flags = Compiler::kCriticalNative;
      }

      compiled_method = driver->GetCompiler()->JniCompile(access_flags,
                                                          method_idx,
                                                          dex_file,
                                                          optimization_flags);
      CHECK(compiled_method != nullptr);
    }
  } else if ((access_flags & kAccAbstract) != 0) {
    // Abstract methods don't have code.
  } else {
    const VerifiedMethod* verified_method =
        driver->GetVerificationResults()->GetVerifiedMethod(method_ref);
    bool compile = compilation_enabled &&
        // Basic checks, e.g., not <clinit>.
        driver->GetVerificationResults()
            ->IsCandidateForCompilation(method_ref, access_flags) &&
        // Did not fail to create VerifiedMethod metadata.
        verified_method != nullptr &&
        // Do not have failures that should punt to the interpreter.
        !verified_method->HasRuntimeThrow() &&
        (verified_method->GetEncounteredVerificationFailures() &
            (verifier::VERIFY_ERROR_FORCE_INTERPRETER | verifier::VERIFY_ERROR_LOCKING)) == 0 &&
        // Is eligable for compilation by methods-to-compile filter.
        driver->IsMethodToCompile(method_ref) &&
        driver->ShouldCompileBasedOnProfile(method_ref);

    if (compile) {
      // NOTE: if compiler declines to compile this method, it will return null.
      compiled_method = driver->GetCompiler()->Compile(code_item,
                                                       access_flags,
                                                       invoke_type,
                                                       class_def_idx,
                                                       method_idx,
                                                       class_loader,
                                                       dex_file,
                                                       dex_cache);
    }
    if (compiled_method == nullptr &&
        dex_to_dex_compilation_level != optimizer::DexToDexCompilationLevel::kDontDexToDexCompile) {
      DCHECK(!Runtime::Current()->UseJitCompilation());
      // TODO: add a command-line option to disable DEX-to-DEX compilation ?
      driver->MarkForDexToDexCompilation(self, method_ref);
    }
  }
  if (kTimeCompileMethod) {
    uint64_t duration_ns = NanoTime() - start_ns;
    if (duration_ns > MsToNs(driver->GetCompiler()->GetMaximumCompilationTimeBeforeWarning())) {
      LOG(WARNING) << "Compilation of " << dex_file.PrettyMethod(method_idx)
                   << " took " << PrettyDuration(duration_ns);
    }
  }

  if (compiled_method != nullptr) {
    // Count non-relative linker patches.
    size_t non_relative_linker_patch_count = 0u;
    for (const LinkerPatch& patch : compiled_method->GetPatches()) {
      if (!patch.IsPcRelative()) {
        ++non_relative_linker_patch_count;
      }
    }
    bool compile_pic = driver->GetCompilerOptions().GetCompilePic();  // Off by default
    // When compiling with PIC, there should be zero non-relative linker patches
    CHECK(!compile_pic || non_relative_linker_patch_count == 0u);

    driver->AddCompiledMethod(method_ref, compiled_method, non_relative_linker_patch_count);
  }

  if (self->IsExceptionPending()) {
    ScopedObjectAccess soa(self);
    LOG(FATAL) << "Unexpected exception compiling: " << dex_file.PrettyMethod(method_idx) << "\n"
        << self->GetException()->Dump();
  }
}

void CompilerDriver::CompileOne(Thread* self, ArtMethod* method, TimingLogger* timings) {
  DCHECK(!Runtime::Current()->IsStarted());
  jobject jclass_loader;
  const DexFile* dex_file;
  uint16_t class_def_idx;
  uint32_t method_idx = method->GetDexMethodIndex();
  uint32_t access_flags = method->GetAccessFlags();
  InvokeType invoke_type = method->GetInvokeType();
  StackHandleScope<2> hs(self);
  Handle<mirror::DexCache> dex_cache(hs.NewHandle(method->GetDexCache()));
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(method->GetDeclaringClass()->GetClassLoader()));
  {
    ScopedObjectAccessUnchecked soa(self);
    ScopedLocalRef<jobject> local_class_loader(
        soa.Env(), soa.AddLocalReference<jobject>(class_loader.Get()));
    jclass_loader = soa.Env()->NewGlobalRef(local_class_loader.get());
    // Find the dex_file
    dex_file = method->GetDexFile();
    class_def_idx = method->GetClassDefIndex();
  }
  const DexFile::CodeItem* code_item = dex_file->GetCodeItem(method->GetCodeItemOffset());

  // Go to native so that we don't block GC during compilation.
  ScopedThreadSuspension sts(self, kNative);

  std::vector<const DexFile*> dex_files;
  dex_files.push_back(dex_file);

  InitializeThreadPools();

  PreCompile(jclass_loader, dex_files, timings);

  // Can we run DEX-to-DEX compiler on this class ?
  optimizer::DexToDexCompilationLevel dex_to_dex_compilation_level =
      GetDexToDexCompilationLevel(self,
                                  *this,
                                  jclass_loader,
                                  *dex_file,
                                  dex_file->GetClassDef(class_def_idx));

  DCHECK(current_dex_to_dex_methods_ == nullptr);
  CompileMethod(self,
                this,
                code_item,
                access_flags,
                invoke_type,
                class_def_idx,
                method_idx,
                class_loader,
                *dex_file,
                dex_to_dex_compilation_level,
                true,
                dex_cache);

  ArrayRef<DexFileMethodSet> dex_to_dex_references;
  {
    // From this point on, we shall not modify dex_to_dex_references_, so
    // just grab a reference to it that we use without holding the mutex.
    MutexLock lock(Thread::Current(), dex_to_dex_references_lock_);
    dex_to_dex_references = ArrayRef<DexFileMethodSet>(dex_to_dex_references_);
  }
  if (!dex_to_dex_references.empty()) {
    DCHECK_EQ(dex_to_dex_references.size(), 1u);
    DCHECK(&dex_to_dex_references[0].GetDexFile() == dex_file);
    current_dex_to_dex_methods_ = &dex_to_dex_references.front().GetMethodIndexes();
    DCHECK(current_dex_to_dex_methods_->IsBitSet(method_idx));
    DCHECK_EQ(current_dex_to_dex_methods_->NumSetBits(), 1u);
    CompileMethod(self,
                  this,
                  code_item,
                  access_flags,
                  invoke_type,
                  class_def_idx,
                  method_idx,
                  class_loader,
                  *dex_file,
                  dex_to_dex_compilation_level,
                  true,
                  dex_cache);
    current_dex_to_dex_methods_ = nullptr;
  }

  FreeThreadPools();

  self->GetJniEnv()->DeleteGlobalRef(jclass_loader);
}

void CompilerDriver::Resolve(jobject class_loader,
                             const std::vector<const DexFile*>& dex_files,
                             TimingLogger* timings) {
  // Resolution allocates classes and needs to run single-threaded to be deterministic.
  bool force_determinism = GetCompilerOptions().IsForceDeterminism();
  ThreadPool* resolve_thread_pool = force_determinism
                                     ? single_thread_pool_.get()
                                     : parallel_thread_pool_.get();
  size_t resolve_thread_count = force_determinism ? 1U : parallel_thread_count_;

  for (size_t i = 0; i != dex_files.size(); ++i) {
    const DexFile* dex_file = dex_files[i];
    CHECK(dex_file != nullptr);
    ResolveDexFile(class_loader,
                   *dex_file,
                   dex_files,
                   resolve_thread_pool,
                   resolve_thread_count,
                   timings);
  }
}

// Resolve const-strings in the code. Done to have deterministic allocation behavior. Right now
// this is single-threaded for simplicity.
// TODO: Collect the relevant string indices in parallel, then allocate them sequentially in a
//       stable order.

static void ResolveConstStrings(Handle<mirror::DexCache> dex_cache,
                                const DexFile& dex_file,
                                const DexFile::CodeItem* code_item)
      REQUIRES_SHARED(Locks::mutator_lock_) {
  if (code_item == nullptr) {
    // Abstract or native method.
    return;
  }

  const uint16_t* code_ptr = code_item->insns_;
  const uint16_t* code_end = code_item->insns_ + code_item->insns_size_in_code_units_;
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();

  while (code_ptr < code_end) {
    const Instruction* inst = Instruction::At(code_ptr);
    switch (inst->Opcode()) {
      case Instruction::CONST_STRING:
      case Instruction::CONST_STRING_JUMBO: {
        dex::StringIndex string_index((inst->Opcode() == Instruction::CONST_STRING)
            ? inst->VRegB_21c()
            : inst->VRegB_31c());
        mirror::String* string = class_linker->ResolveString(dex_file, string_index, dex_cache);
        CHECK(string != nullptr) << "Could not allocate a string when forcing determinism";
        break;
      }

      default:
        break;
    }

    code_ptr += inst->SizeInCodeUnits();
  }
}

static void ResolveConstStrings(CompilerDriver* driver,
                                const std::vector<const DexFile*>& dex_files,
                                TimingLogger* timings) {
  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<1> hs(soa.Self());
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  MutableHandle<mirror::DexCache> dex_cache(hs.NewHandle<mirror::DexCache>(nullptr));

  for (const DexFile* dex_file : dex_files) {
    dex_cache.Assign(class_linker->FindDexCache(soa.Self(), *dex_file));
    TimingLogger::ScopedTiming t("Resolve const-string Strings", timings);

    size_t class_def_count = dex_file->NumClassDefs();
    for (size_t class_def_index = 0; class_def_index < class_def_count; ++class_def_index) {
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(class_def_index);

      const uint8_t* class_data = dex_file->GetClassData(class_def);
      if (class_data == nullptr) {
        // empty class, probably a marker interface
        continue;
      }

      ClassDataItemIterator it(*dex_file, class_data);
      // Skip fields
      while (it.HasNextStaticField()) {
        it.Next();
      }
      while (it.HasNextInstanceField()) {
        it.Next();
      }

      bool compilation_enabled = driver->IsClassToCompile(
          dex_file->StringByTypeIdx(class_def.class_idx_));
      if (!compilation_enabled) {
        // Compilation is skipped, do not resolve const-string in code of this class.
        // TODO: Make sure that inlining honors this.
        continue;
      }

      // Direct methods.
      int64_t previous_direct_method_idx = -1;
      while (it.HasNextDirectMethod()) {
        uint32_t method_idx = it.GetMemberIndex();
        if (method_idx == previous_direct_method_idx) {
          // smali can create dex files with two encoded_methods sharing the same method_idx
          // http://code.google.com/p/smali/issues/detail?id=119
          it.Next();
          continue;
        }
        previous_direct_method_idx = method_idx;
        ResolveConstStrings(dex_cache, *dex_file, it.GetMethodCodeItem());
        it.Next();
      }
      // Virtual methods.
      int64_t previous_virtual_method_idx = -1;
      while (it.HasNextVirtualMethod()) {
        uint32_t method_idx = it.GetMemberIndex();
        if (method_idx == previous_virtual_method_idx) {
          // smali can create dex files with two encoded_methods sharing the same method_idx
          // http://code.google.com/p/smali/issues/detail?id=119
          it.Next();
          continue;
        }
        previous_virtual_method_idx = method_idx;
        ResolveConstStrings(dex_cache, *dex_file, it.GetMethodCodeItem());
        it.Next();
      }
      DCHECK(!it.HasNext());
    }
  }
}

inline void CompilerDriver::CheckThreadPools() {
  DCHECK(parallel_thread_pool_ != nullptr);
  DCHECK(single_thread_pool_ != nullptr);
}

static void EnsureVerifiedOrVerifyAtRuntime(jobject jclass_loader,
                                            const std::vector<const DexFile*>& dex_files) {
  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<2> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
  MutableHandle<mirror::Class> cls(hs.NewHandle<mirror::Class>(nullptr));
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();

  for (const DexFile* dex_file : dex_files) {
    for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
      const char* descriptor = dex_file->GetClassDescriptor(class_def);
      cls.Assign(class_linker->FindClass(soa.Self(), descriptor, class_loader));
      if (cls == nullptr) {
        soa.Self()->ClearException();
      } else if (&cls->GetDexFile() == dex_file) {
        DCHECK(cls->IsErroneous() || cls->IsVerified() || cls->ShouldVerifyAtRuntime())
            << cls->PrettyClass()
            << " " << cls->GetStatus();
      }
    }
  }
}

void CompilerDriver::PreCompile(jobject class_loader,
                                const std::vector<const DexFile*>& dex_files,
                                TimingLogger* timings) {
  CheckThreadPools();

  for (const DexFile* dex_file : dex_files) {
    // Can be already inserted if the caller is CompileOne. This happens for gtests.
    if (!compiled_methods_.HaveDexFile(dex_file)) {
      compiled_methods_.AddDexFile(dex_file);
    }
  }

  LoadImageClasses(timings);
  VLOG(compiler) << "LoadImageClasses: " << GetMemoryUsageString(false);

  if (compiler_options_->IsAnyCompilationEnabled()) {
    // Resolve eagerly to prepare for compilation.
    Resolve(class_loader, dex_files, timings);
    VLOG(compiler) << "Resolve: " << GetMemoryUsageString(false);
  }

  if (compiler_options_->AssumeClassesAreVerified()) {
    VLOG(compiler) << "Verify none mode specified, skipping verification.";
    SetVerified(class_loader, dex_files, timings);
  }

  if (!compiler_options_->IsVerificationEnabled()) {
    return;
  }

  if (GetCompilerOptions().IsForceDeterminism() && GetCompilerOptions().IsBootImage()) {
    // Resolve strings from const-string. Do this now to have a deterministic image.
    ResolveConstStrings(this, dex_files, timings);
    VLOG(compiler) << "Resolve const-strings: " << GetMemoryUsageString(false);
  }

  Verify(class_loader, dex_files, timings);
  VLOG(compiler) << "Verify: " << GetMemoryUsageString(false);

  if (had_hard_verifier_failure_ && GetCompilerOptions().AbortOnHardVerifierFailure()) {
    LOG(FATAL) << "Had a hard failure verifying all classes, and was asked to abort in such "
               << "situations. Please check the log.";
  }

  if (compiler_options_->IsAnyCompilationEnabled()) {
    if (kIsDebugBuild) {
      EnsureVerifiedOrVerifyAtRuntime(class_loader, dex_files);
    }
    InitializeClasses(class_loader, dex_files, timings);
    VLOG(compiler) << "InitializeClasses: " << GetMemoryUsageString(false);
  }

  UpdateImageClasses(timings);
  VLOG(compiler) << "UpdateImageClasses: " << GetMemoryUsageString(false);
}

bool CompilerDriver::IsImageClass(const char* descriptor) const {
  if (image_classes_ != nullptr) {
    // If we have a set of image classes, use those.
    return image_classes_->find(descriptor) != image_classes_->end();
  }
  // No set of image classes, assume we include all the classes.
  // NOTE: Currently only reachable from InitImageMethodVisitor for the app image case.
  return !GetCompilerOptions().IsBootImage();
}

bool CompilerDriver::IsClassToCompile(const char* descriptor) const {
  if (classes_to_compile_ == nullptr) {
    return true;
  }
  return classes_to_compile_->find(descriptor) != classes_to_compile_->end();
}

bool CompilerDriver::IsMethodToCompile(const MethodReference& method_ref) const {
  if (methods_to_compile_ == nullptr) {
    return true;
  }

  std::string tmp = method_ref.dex_file->PrettyMethod(method_ref.dex_method_index, true);
  return methods_to_compile_->find(tmp.c_str()) != methods_to_compile_->end();
}

bool CompilerDriver::ShouldCompileBasedOnProfile(const MethodReference& method_ref) const {
  // Profile compilation info may be null if no profile is passed.
  if (!CompilerFilter::DependsOnProfile(compiler_options_->GetCompilerFilter())) {
    // Use the compiler filter instead of the presence of profile_compilation_info_ since
    // we may want to have full speed compilation along with profile based layout optimizations.
    return true;
  }
  // If we are using a profile filter but do not have a profile compilation info, compile nothing.
  if (profile_compilation_info_ == nullptr) {
    return false;
  }
  bool result = profile_compilation_info_->ContainsMethod(method_ref);

  if (kDebugProfileGuidedCompilation) {
    LOG(INFO) << "[ProfileGuidedCompilation] "
        << (result ? "Compiled" : "Skipped") << " method:"
        << method_ref.dex_file->PrettyMethod(method_ref.dex_method_index, true);
  }
  return result;
}

class ResolveCatchBlockExceptionsClassVisitor : public ClassVisitor {
 public:
  ResolveCatchBlockExceptionsClassVisitor() : classes_() {}

  virtual bool operator()(ObjPtr<mirror::Class> c) OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    classes_.push_back(c);
    return true;
  }

  void FindExceptionTypesToResolve(
      std::set<std::pair<dex::TypeIndex, const DexFile*>>* exceptions_to_resolve)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    const auto pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
    for (ObjPtr<mirror::Class> klass : classes_) {
      for (ArtMethod& method : klass->GetMethods(pointer_size)) {
        FindExceptionTypesToResolveForMethod(&method, exceptions_to_resolve);
      }
    }
  }

 private:
  void FindExceptionTypesToResolveForMethod(
      ArtMethod* method,
      std::set<std::pair<dex::TypeIndex, const DexFile*>>* exceptions_to_resolve)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    const DexFile::CodeItem* code_item = method->GetCodeItem();
    if (code_item == nullptr) {
      return;  // native or abstract method
    }
    if (code_item->tries_size_ == 0) {
      return;  // nothing to process
    }
    const uint8_t* encoded_catch_handler_list = DexFile::GetCatchHandlerData(*code_item, 0);
    size_t num_encoded_catch_handlers = DecodeUnsignedLeb128(&encoded_catch_handler_list);
    for (size_t i = 0; i < num_encoded_catch_handlers; i++) {
      int32_t encoded_catch_handler_size = DecodeSignedLeb128(&encoded_catch_handler_list);
      bool has_catch_all = false;
      if (encoded_catch_handler_size <= 0) {
        encoded_catch_handler_size = -encoded_catch_handler_size;
        has_catch_all = true;
      }
      for (int32_t j = 0; j < encoded_catch_handler_size; j++) {
        dex::TypeIndex encoded_catch_handler_handlers_type_idx =
            dex::TypeIndex(DecodeUnsignedLeb128(&encoded_catch_handler_list));
        // Add to set of types to resolve if not already in the dex cache resolved types
        if (!method->IsResolvedTypeIdx(encoded_catch_handler_handlers_type_idx)) {
          exceptions_to_resolve->emplace(encoded_catch_handler_handlers_type_idx,
                                         method->GetDexFile());
        }
        // ignore address associated with catch handler
        DecodeUnsignedLeb128(&encoded_catch_handler_list);
      }
      if (has_catch_all) {
        // ignore catch all address
        DecodeUnsignedLeb128(&encoded_catch_handler_list);
      }
    }
  }

  std::vector<ObjPtr<mirror::Class>> classes_;
};

class RecordImageClassesVisitor : public ClassVisitor {
 public:
  explicit RecordImageClassesVisitor(std::unordered_set<std::string>* image_classes)
      : image_classes_(image_classes) {}

  bool operator()(ObjPtr<mirror::Class> klass) OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
    std::string temp;
    image_classes_->insert(klass->GetDescriptor(&temp));
    return true;
  }

 private:
  std::unordered_set<std::string>* const image_classes_;
};

// Make a list of descriptors for classes to include in the image
void CompilerDriver::LoadImageClasses(TimingLogger* timings) {
  CHECK(timings != nullptr);
  if (!GetCompilerOptions().IsBootImage()) {
    return;
  }

  TimingLogger::ScopedTiming t("LoadImageClasses", timings);
  // Make a first class to load all classes explicitly listed in the file
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  CHECK(image_classes_.get() != nullptr);
  for (auto it = image_classes_->begin(), end = image_classes_->end(); it != end;) {
    const std::string& descriptor(*it);
    StackHandleScope<1> hs(self);
    Handle<mirror::Class> klass(
        hs.NewHandle(class_linker->FindSystemClass(self, descriptor.c_str())));
    if (klass == nullptr) {
      VLOG(compiler) << "Failed to find class " << descriptor;
      image_classes_->erase(it++);
      self->ClearException();
    } else {
      ++it;
    }
  }

  // Resolve exception classes referenced by the loaded classes. The catch logic assumes
  // exceptions are resolved by the verifier when there is a catch block in an interested method.
  // Do this here so that exception classes appear to have been specified image classes.
  std::set<std::pair<dex::TypeIndex, const DexFile*>> unresolved_exception_types;
  StackHandleScope<1> hs(self);
  Handle<mirror::Class> java_lang_Throwable(
      hs.NewHandle(class_linker->FindSystemClass(self, "Ljava/lang/Throwable;")));
  do {
    unresolved_exception_types.clear();
    {
      // Thread suspension is not allowed while ResolveCatchBlockExceptionsClassVisitor
      // is using a std::vector<ObjPtr<mirror::Class>>.
      ScopedAssertNoThreadSuspension ants(__FUNCTION__);
      ResolveCatchBlockExceptionsClassVisitor visitor;
      class_linker->VisitClasses(&visitor);
      visitor.FindExceptionTypesToResolve(&unresolved_exception_types);
    }
    for (const auto& exception_type : unresolved_exception_types) {
      dex::TypeIndex exception_type_idx = exception_type.first;
      const DexFile* dex_file = exception_type.second;
      StackHandleScope<2> hs2(self);
      Handle<mirror::DexCache> dex_cache(hs2.NewHandle(class_linker->RegisterDexFile(*dex_file,
                                                                                     nullptr)));
      Handle<mirror::Class> klass(hs2.NewHandle(
          (dex_cache != nullptr)
              ? class_linker->ResolveType(*dex_file,
                                          exception_type_idx,
                                          dex_cache,
                                          ScopedNullHandle<mirror::ClassLoader>())
              : nullptr));
      if (klass == nullptr) {
        const DexFile::TypeId& type_id = dex_file->GetTypeId(exception_type_idx);
        const char* descriptor = dex_file->GetTypeDescriptor(type_id);
        LOG(FATAL) << "Failed to resolve class " << descriptor;
      }
      DCHECK(java_lang_Throwable->IsAssignableFrom(klass.Get()));
    }
    // Resolving exceptions may load classes that reference more exceptions, iterate until no
    // more are found
  } while (!unresolved_exception_types.empty());

  // We walk the roots looking for classes so that we'll pick up the
  // above classes plus any classes them depend on such super
  // classes, interfaces, and the required ClassLinker roots.
  RecordImageClassesVisitor visitor(image_classes_.get());
  class_linker->VisitClasses(&visitor);

  CHECK_NE(image_classes_->size(), 0U);
}

static void MaybeAddToImageClasses(Thread* self,
                                   ObjPtr<mirror::Class> klass,
                                   std::unordered_set<std::string>* image_classes)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK_EQ(self, Thread::Current());
  StackHandleScope<1> hs(self);
  std::string temp;
  const PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
  while (!klass->IsObjectClass()) {
    const char* descriptor = klass->GetDescriptor(&temp);
    std::pair<std::unordered_set<std::string>::iterator, bool> result =
        image_classes->insert(descriptor);
    if (!result.second) {  // Previously inserted.
      break;
    }
    VLOG(compiler) << "Adding " << descriptor << " to image classes";
    for (size_t i = 0, num_interfaces = klass->NumDirectInterfaces(); i != num_interfaces; ++i) {
      ObjPtr<mirror::Class> interface = mirror::Class::GetDirectInterface(self, klass, i);
      DCHECK(interface != nullptr);
      MaybeAddToImageClasses(self, interface, image_classes);
    }
    for (auto& m : klass->GetVirtualMethods(pointer_size)) {
      MaybeAddToImageClasses(self, m.GetDeclaringClass(), image_classes);
    }
    if (klass->IsArrayClass()) {
      MaybeAddToImageClasses(self, klass->GetComponentType(), image_classes);
    }
    klass.Assign(klass->GetSuperClass());
  }
}

// Keeps all the data for the update together. Also doubles as the reference visitor.
// Note: we can use object pointers because we suspend all threads.
class ClinitImageUpdate {
 public:
  static ClinitImageUpdate* Create(VariableSizedHandleScope& hs,
                                   std::unordered_set<std::string>* image_class_descriptors,
                                   Thread* self,
                                   ClassLinker* linker) {
    std::unique_ptr<ClinitImageUpdate> res(new ClinitImageUpdate(hs,
                                                                 image_class_descriptors,
                                                                 self,
                                                                 linker));
    return res.release();
  }

  ~ClinitImageUpdate() {
    // Allow others to suspend again.
    self_->EndAssertNoThreadSuspension(old_cause_);
  }

  // Visitor for VisitReferences.
  void operator()(ObjPtr<mirror::Object> object,
                  MemberOffset field_offset,
                  bool /* is_static */) const
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mirror::Object* ref = object->GetFieldObject<mirror::Object>(field_offset);
    if (ref != nullptr) {
      VisitClinitClassesObject(ref);
    }
  }

  // java.lang.ref.Reference visitor for VisitReferences.
  void operator()(ObjPtr<mirror::Class> klass ATTRIBUTE_UNUSED,
                  ObjPtr<mirror::Reference> ref ATTRIBUTE_UNUSED) const {}

  // Ignore class native roots.
  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED)
      const {}
  void VisitRoot(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED) const {}

  void Walk() REQUIRES_SHARED(Locks::mutator_lock_) {
    // Use the initial classes as roots for a search.
    for (Handle<mirror::Class> klass_root : image_classes_) {
      VisitClinitClassesObject(klass_root.Get());
    }
    Thread* self = Thread::Current();
    ScopedAssertNoThreadSuspension ants(__FUNCTION__);
    for (Handle<mirror::Class> h_klass : to_insert_) {
      MaybeAddToImageClasses(self, h_klass.Get(), image_class_descriptors_);
    }
  }

 private:
  class FindImageClassesVisitor : public ClassVisitor {
   public:
    explicit FindImageClassesVisitor(VariableSizedHandleScope& hs,
                                     ClinitImageUpdate* data)
        : data_(data),
          hs_(hs) {}

    bool operator()(ObjPtr<mirror::Class> klass) OVERRIDE REQUIRES_SHARED(Locks::mutator_lock_) {
      std::string temp;
      const char* name = klass->GetDescriptor(&temp);
      if (data_->image_class_descriptors_->find(name) != data_->image_class_descriptors_->end()) {
        data_->image_classes_.push_back(hs_.NewHandle(klass));
      } else {
        // Check whether it is initialized and has a clinit. They must be kept, too.
        if (klass->IsInitialized() && klass->FindClassInitializer(
            Runtime::Current()->GetClassLinker()->GetImagePointerSize()) != nullptr) {
          data_->image_classes_.push_back(hs_.NewHandle(klass));
        }
      }
      return true;
    }

   private:
    ClinitImageUpdate* const data_;
    VariableSizedHandleScope& hs_;
  };

  ClinitImageUpdate(VariableSizedHandleScope& hs,
                    std::unordered_set<std::string>* image_class_descriptors,
                    Thread* self,
                    ClassLinker* linker) REQUIRES_SHARED(Locks::mutator_lock_)
      : hs_(hs),
        image_class_descriptors_(image_class_descriptors),
        self_(self) {
    CHECK(linker != nullptr);
    CHECK(image_class_descriptors != nullptr);

    // Make sure nobody interferes with us.
    old_cause_ = self->StartAssertNoThreadSuspension("Boot image closure");

    // Find all the already-marked classes.
    WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
    FindImageClassesVisitor visitor(hs_, this);
    linker->VisitClasses(&visitor);
  }

  void VisitClinitClassesObject(mirror::Object* object) const
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(object != nullptr);
    if (marked_objects_.find(object) != marked_objects_.end()) {
      // Already processed.
      return;
    }

    // Mark it.
    marked_objects_.insert(object);

    if (object->IsClass()) {
      // Add to the TODO list since MaybeAddToImageClasses may cause thread suspension. Thread
      // suspensionb is not safe to do in VisitObjects or VisitReferences.
      to_insert_.push_back(hs_.NewHandle(object->AsClass()));
    } else {
      // Else visit the object's class.
      VisitClinitClassesObject(object->GetClass());
    }

    // If it is not a DexCache, visit all references.
    if (!object->IsDexCache()) {
      object->VisitReferences(*this, *this);
    }
  }

  VariableSizedHandleScope& hs_;
  mutable std::vector<Handle<mirror::Class>> to_insert_;
  mutable std::unordered_set<mirror::Object*> marked_objects_;
  std::unordered_set<std::string>* const image_class_descriptors_;
  std::vector<Handle<mirror::Class>> image_classes_;
  Thread* const self_;
  const char* old_cause_;

  DISALLOW_COPY_AND_ASSIGN(ClinitImageUpdate);
};

void CompilerDriver::UpdateImageClasses(TimingLogger* timings) {
  if (GetCompilerOptions().IsBootImage()) {
    TimingLogger::ScopedTiming t("UpdateImageClasses", timings);

    Runtime* runtime = Runtime::Current();

    // Suspend all threads.
    ScopedSuspendAll ssa(__FUNCTION__);

    VariableSizedHandleScope hs(Thread::Current());
    std::string error_msg;
    std::unique_ptr<ClinitImageUpdate> update(ClinitImageUpdate::Create(hs,
                                                                        image_classes_.get(),
                                                                        Thread::Current(),
                                                                        runtime->GetClassLinker()));

    // Do the marking.
    update->Walk();
  }
}

bool CompilerDriver::CanAssumeClassIsLoaded(mirror::Class* klass) {
  Runtime* runtime = Runtime::Current();
  if (!runtime->IsAotCompiler()) {
    DCHECK(runtime->UseJitCompilation());
    // Having the klass reference here implies that the klass is already loaded.
    return true;
  }
  if (!GetCompilerOptions().IsBootImage()) {
    // Assume loaded only if klass is in the boot image. App classes cannot be assumed
    // loaded because we don't even know what class loader will be used to load them.
    bool class_in_image = runtime->GetHeap()->FindSpaceFromObject(klass, false)->IsImageSpace();
    return class_in_image;
  }
  std::string temp;
  const char* descriptor = klass->GetDescriptor(&temp);
  return IsImageClass(descriptor);
}

void CompilerDriver::MarkForDexToDexCompilation(Thread* self, const MethodReference& method_ref) {
  MutexLock lock(self, dex_to_dex_references_lock_);
  // Since we're compiling one dex file at a time, we need to look for the
  // current dex file entry only at the end of dex_to_dex_references_.
  if (dex_to_dex_references_.empty() ||
      &dex_to_dex_references_.back().GetDexFile() != method_ref.dex_file) {
    dex_to_dex_references_.emplace_back(*method_ref.dex_file);
  }
  dex_to_dex_references_.back().GetMethodIndexes().SetBit(method_ref.dex_method_index);
}

bool CompilerDriver::CanAccessTypeWithoutChecks(ObjPtr<mirror::Class> referrer_class,
                                                ObjPtr<mirror::Class> resolved_class) {
  if (resolved_class == nullptr) {
    stats_->TypeNeedsAccessCheck();
    return false;  // Unknown class needs access checks.
  }
  bool is_accessible = resolved_class->IsPublic();  // Public classes are always accessible.
  if (!is_accessible) {
    if (referrer_class == nullptr) {
      stats_->TypeNeedsAccessCheck();
      return false;  // Incomplete referrer knowledge needs access check.
    }
    // Perform access check, will return true if access is ok or false if we're going to have to
    // check this at runtime (for example for class loaders).
    is_accessible = referrer_class->CanAccess(resolved_class);
  }
  if (is_accessible) {
    stats_->TypeDoesntNeedAccessCheck();
  } else {
    stats_->TypeNeedsAccessCheck();
  }
  return is_accessible;
}

bool CompilerDriver::CanAccessInstantiableTypeWithoutChecks(ObjPtr<mirror::Class> referrer_class,
                                                            ObjPtr<mirror::Class> resolved_class,
                                                            bool* finalizable) {
  if (resolved_class == nullptr) {
    stats_->TypeNeedsAccessCheck();
    // Be conservative.
    *finalizable = true;
    return false;  // Unknown class needs access checks.
  }
  *finalizable = resolved_class->IsFinalizable();
  bool is_accessible = resolved_class->IsPublic();  // Public classes are always accessible.
  if (!is_accessible) {
    if (referrer_class == nullptr) {
      stats_->TypeNeedsAccessCheck();
      return false;  // Incomplete referrer knowledge needs access check.
    }
    // Perform access and instantiable checks, will return true if access is ok or false if we're
    // going to have to check this at runtime (for example for class loaders).
    is_accessible = referrer_class->CanAccess(resolved_class);
  }
  bool result = is_accessible && resolved_class->IsInstantiable();
  if (result) {
    stats_->TypeDoesntNeedAccessCheck();
  } else {
    stats_->TypeNeedsAccessCheck();
  }
  return result;
}

void CompilerDriver::ProcessedInstanceField(bool resolved) {
  if (!resolved) {
    stats_->UnresolvedInstanceField();
  } else {
    stats_->ResolvedInstanceField();
  }
}

void CompilerDriver::ProcessedStaticField(bool resolved, bool local) {
  if (!resolved) {
    stats_->UnresolvedStaticField();
  } else if (local) {
    stats_->ResolvedLocalStaticField();
  } else {
    stats_->ResolvedStaticField();
  }
}

ArtField* CompilerDriver::ComputeInstanceFieldInfo(uint32_t field_idx,
                                                   const DexCompilationUnit* mUnit, bool is_put,
                                                   const ScopedObjectAccess& soa) {
  // Try to resolve the field and compiling method's class.
  ArtField* resolved_field;
  mirror::Class* referrer_class;
  Handle<mirror::DexCache> dex_cache(mUnit->GetDexCache());
  {
    Handle<mirror::ClassLoader> class_loader_handle = mUnit->GetClassLoader();
    resolved_field = ResolveField(soa, dex_cache, class_loader_handle, mUnit, field_idx, false);
    referrer_class = resolved_field != nullptr
        ? ResolveCompilingMethodsClass(soa, dex_cache, class_loader_handle, mUnit) : nullptr;
  }
  bool can_link = false;
  if (resolved_field != nullptr && referrer_class != nullptr) {
    std::pair<bool, bool> fast_path = IsFastInstanceField(
        dex_cache.Get(), referrer_class, resolved_field, field_idx);
    can_link = is_put ? fast_path.second : fast_path.first;
  }
  ProcessedInstanceField(can_link);
  return can_link ? resolved_field : nullptr;
}

bool CompilerDriver::ComputeInstanceFieldInfo(uint32_t field_idx, const DexCompilationUnit* mUnit,
                                              bool is_put, MemberOffset* field_offset,
                                              bool* is_volatile) {
  ScopedObjectAccess soa(Thread::Current());
  ArtField* resolved_field = ComputeInstanceFieldInfo(field_idx, mUnit, is_put, soa);

  if (resolved_field == nullptr) {
    // Conservative defaults.
    *is_volatile = true;
    *field_offset = MemberOffset(static_cast<size_t>(-1));
    return false;
  } else {
    *is_volatile = resolved_field->IsVolatile();
    *field_offset = resolved_field->GetOffset();
    return true;
  }
}

const VerifiedMethod* CompilerDriver::GetVerifiedMethod(const DexFile* dex_file,
                                                        uint32_t method_idx) const {
  MethodReference ref(dex_file, method_idx);
  return verification_results_->GetVerifiedMethod(ref);
}

bool CompilerDriver::IsSafeCast(const DexCompilationUnit* mUnit, uint32_t dex_pc) {
  if (!compiler_options_->IsVerificationEnabled()) {
    // If we didn't verify, every cast has to be treated as non-safe.
    return false;
  }
  DCHECK(mUnit->GetVerifiedMethod() != nullptr);
  bool result = mUnit->GetVerifiedMethod()->IsSafeCast(dex_pc);
  if (result) {
    stats_->SafeCast();
  } else {
    stats_->NotASafeCast();
  }
  return result;
}

class CompilationVisitor {
 public:
  virtual ~CompilationVisitor() {}
  virtual void Visit(size_t index) = 0;
};

class ParallelCompilationManager {
 public:
  ParallelCompilationManager(ClassLinker* class_linker,
                             jobject class_loader,
                             CompilerDriver* compiler,
                             const DexFile* dex_file,
                             const std::vector<const DexFile*>& dex_files,
                             ThreadPool* thread_pool)
    : index_(0),
      class_linker_(class_linker),
      class_loader_(class_loader),
      compiler_(compiler),
      dex_file_(dex_file),
      dex_files_(dex_files),
      thread_pool_(thread_pool) {}

  ClassLinker* GetClassLinker() const {
    CHECK(class_linker_ != nullptr);
    return class_linker_;
  }

  jobject GetClassLoader() const {
    return class_loader_;
  }

  CompilerDriver* GetCompiler() const {
    CHECK(compiler_ != nullptr);
    return compiler_;
  }

  const DexFile* GetDexFile() const {
    CHECK(dex_file_ != nullptr);
    return dex_file_;
  }

  const std::vector<const DexFile*>& GetDexFiles() const {
    return dex_files_;
  }

  void ForAll(size_t begin, size_t end, CompilationVisitor* visitor, size_t work_units)
      REQUIRES(!*Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    self->AssertNoPendingException();
    CHECK_GT(work_units, 0U);

    index_.StoreRelaxed(begin);
    for (size_t i = 0; i < work_units; ++i) {
      thread_pool_->AddTask(self, new ForAllClosure(this, end, visitor));
    }
    thread_pool_->StartWorkers(self);

    // Ensure we're suspended while we're blocked waiting for the other threads to finish (worker
    // thread destructor's called below perform join).
    CHECK_NE(self->GetState(), kRunnable);

    // Wait for all the worker threads to finish.
    thread_pool_->Wait(self, true, false);

    // And stop the workers accepting jobs.
    thread_pool_->StopWorkers(self);
  }

  size_t NextIndex() {
    return index_.FetchAndAddSequentiallyConsistent(1);
  }

 private:
  class ForAllClosure : public Task {
   public:
    ForAllClosure(ParallelCompilationManager* manager, size_t end, CompilationVisitor* visitor)
        : manager_(manager),
          end_(end),
          visitor_(visitor) {}

    virtual void Run(Thread* self) {
      while (true) {
        const size_t index = manager_->NextIndex();
        if (UNLIKELY(index >= end_)) {
          break;
        }
        visitor_->Visit(index);
        self->AssertNoPendingException();
      }
    }

    virtual void Finalize() {
      delete this;
    }

   private:
    ParallelCompilationManager* const manager_;
    const size_t end_;
    CompilationVisitor* const visitor_;
  };

  AtomicInteger index_;
  ClassLinker* const class_linker_;
  const jobject class_loader_;
  CompilerDriver* const compiler_;
  const DexFile* const dex_file_;
  const std::vector<const DexFile*>& dex_files_;
  ThreadPool* const thread_pool_;

  DISALLOW_COPY_AND_ASSIGN(ParallelCompilationManager);
};

// A fast version of SkipClass above if the class pointer is available
// that avoids the expensive FindInClassPath search.
static bool SkipClass(jobject class_loader, const DexFile& dex_file, mirror::Class* klass)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(klass != nullptr);
  const DexFile& original_dex_file = *klass->GetDexCache()->GetDexFile();
  if (&dex_file != &original_dex_file) {
    if (class_loader == nullptr) {
      LOG(WARNING) << "Skipping class " << klass->PrettyDescriptor() << " from "
                   << dex_file.GetLocation() << " previously found in "
                   << original_dex_file.GetLocation();
    }
    return true;
  }
  return false;
}

static void CheckAndClearResolveException(Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  CHECK(self->IsExceptionPending());
  mirror::Throwable* exception = self->GetException();
  std::string temp;
  const char* descriptor = exception->GetClass()->GetDescriptor(&temp);
  const char* expected_exceptions[] = {
      "Ljava/lang/IllegalAccessError;",
      "Ljava/lang/IncompatibleClassChangeError;",
      "Ljava/lang/InstantiationError;",
      "Ljava/lang/LinkageError;",
      "Ljava/lang/NoClassDefFoundError;",
      "Ljava/lang/NoSuchFieldError;",
      "Ljava/lang/NoSuchMethodError;"
  };
  bool found = false;
  for (size_t i = 0; (found == false) && (i < arraysize(expected_exceptions)); ++i) {
    if (strcmp(descriptor, expected_exceptions[i]) == 0) {
      found = true;
    }
  }
  if (!found) {
    LOG(FATAL) << "Unexpected exception " << exception->Dump();
  }
  self->ClearException();
}

bool CompilerDriver::RequiresConstructorBarrier(const DexFile& dex_file,
                                                uint16_t class_def_idx) const {
  const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_idx);
  const uint8_t* class_data = dex_file.GetClassData(class_def);
  if (class_data == nullptr) {
    // Empty class such as a marker interface.
    return false;
  }
  ClassDataItemIterator it(dex_file, class_data);
  while (it.HasNextStaticField()) {
    it.Next();
  }
  // We require a constructor barrier if there are final instance fields.
  while (it.HasNextInstanceField()) {
    if (it.MemberIsFinal()) {
      return true;
    }
    it.Next();
  }
  return false;
}

class ResolveClassFieldsAndMethodsVisitor : public CompilationVisitor {
 public:
  explicit ResolveClassFieldsAndMethodsVisitor(const ParallelCompilationManager* manager)
      : manager_(manager) {}

  void Visit(size_t class_def_index) OVERRIDE REQUIRES(!Locks::mutator_lock_) {
    ATRACE_CALL();
    Thread* const self = Thread::Current();
    jobject jclass_loader = manager_->GetClassLoader();
    const DexFile& dex_file = *manager_->GetDexFile();
    ClassLinker* class_linker = manager_->GetClassLinker();

    // If an instance field is final then we need to have a barrier on the return, static final
    // fields are assigned within the lock held for class initialization. Conservatively assume
    // constructor barriers are always required.
    bool requires_constructor_barrier = true;

    // Method and Field are the worst. We can't resolve without either
    // context from the code use (to disambiguate virtual vs direct
    // method and instance vs static field) or from class
    // definitions. While the compiler will resolve what it can as it
    // needs it, here we try to resolve fields and methods used in class
    // definitions, since many of them many never be referenced by
    // generated code.
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    ScopedObjectAccess soa(self);
    StackHandleScope<2> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    Handle<mirror::DexCache> dex_cache(hs.NewHandle(class_linker->FindDexCache(
        soa.Self(), dex_file)));
    // Resolve the class.
    mirror::Class* klass = class_linker->ResolveType(dex_file, class_def.class_idx_, dex_cache,
                                                     class_loader);
    bool resolve_fields_and_methods;
    if (klass == nullptr) {
      // Class couldn't be resolved, for example, super-class is in a different dex file. Don't
      // attempt to resolve methods and fields when there is no declaring class.
      CheckAndClearResolveException(soa.Self());
      resolve_fields_and_methods = false;
    } else {
      // We successfully resolved a class, should we skip it?
      if (SkipClass(jclass_loader, dex_file, klass)) {
        return;
      }
      // We want to resolve the methods and fields eagerly.
      resolve_fields_and_methods = true;
    }
    // Note the class_data pointer advances through the headers,
    // static fields, instance fields, direct methods, and virtual
    // methods.
    const uint8_t* class_data = dex_file.GetClassData(class_def);
    if (class_data == nullptr) {
      // Empty class such as a marker interface.
      requires_constructor_barrier = false;
    } else {
      ClassDataItemIterator it(dex_file, class_data);
      while (it.HasNextStaticField()) {
        if (resolve_fields_and_methods) {
          ArtField* field = class_linker->ResolveField(dex_file, it.GetMemberIndex(),
                                                               dex_cache, class_loader, true);
          if (field == nullptr) {
            CheckAndClearResolveException(soa.Self());
          }
        }
        it.Next();
      }
      // We require a constructor barrier if there are final instance fields.
      requires_constructor_barrier = false;
      while (it.HasNextInstanceField()) {
        if (it.MemberIsFinal()) {
          requires_constructor_barrier = true;
        }
        if (resolve_fields_and_methods) {
          ArtField* field = class_linker->ResolveField(dex_file, it.GetMemberIndex(),
                                                               dex_cache, class_loader, false);
          if (field == nullptr) {
            CheckAndClearResolveException(soa.Self());
          }
        }
        it.Next();
      }
      if (resolve_fields_and_methods) {
        while (it.HasNextDirectMethod()) {
          ArtMethod* method = class_linker->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
              dex_file, it.GetMemberIndex(), dex_cache, class_loader, nullptr,
              it.GetMethodInvokeType(class_def));
          if (method == nullptr) {
            CheckAndClearResolveException(soa.Self());
          }
          it.Next();
        }
        while (it.HasNextVirtualMethod()) {
          ArtMethod* method = class_linker->ResolveMethod<ClassLinker::kNoICCECheckForCache>(
              dex_file, it.GetMemberIndex(), dex_cache, class_loader, nullptr,
              it.GetMethodInvokeType(class_def));
          if (method == nullptr) {
            CheckAndClearResolveException(soa.Self());
          }
          it.Next();
        }
        DCHECK(!it.HasNext());
      }
    }
    manager_->GetCompiler()->SetRequiresConstructorBarrier(self,
                                                           &dex_file,
                                                           class_def_index,
                                                           requires_constructor_barrier);
  }

 private:
  const ParallelCompilationManager* const manager_;
};

class ResolveTypeVisitor : public CompilationVisitor {
 public:
  explicit ResolveTypeVisitor(const ParallelCompilationManager* manager) : manager_(manager) {
  }
  void Visit(size_t type_idx) OVERRIDE REQUIRES(!Locks::mutator_lock_) {
  // Class derived values are more complicated, they require the linker and loader.
    ScopedObjectAccess soa(Thread::Current());
    ClassLinker* class_linker = manager_->GetClassLinker();
    const DexFile& dex_file = *manager_->GetDexFile();
    StackHandleScope<2> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(manager_->GetClassLoader())));
    Handle<mirror::DexCache> dex_cache(hs.NewHandle(class_linker->RegisterDexFile(
        dex_file,
        class_loader.Get())));
    ObjPtr<mirror::Class> klass = (dex_cache != nullptr)
        ? class_linker->ResolveType(dex_file, dex::TypeIndex(type_idx), dex_cache, class_loader)
        : nullptr;

    if (klass == nullptr) {
      soa.Self()->AssertPendingException();
      mirror::Throwable* exception = soa.Self()->GetException();
      VLOG(compiler) << "Exception during type resolution: " << exception->Dump();
      if (exception->GetClass()->DescriptorEquals("Ljava/lang/OutOfMemoryError;")) {
        // There's little point continuing compilation if the heap is exhausted.
        LOG(FATAL) << "Out of memory during type resolution for compilation";
      }
      soa.Self()->ClearException();
    }
  }

 private:
  const ParallelCompilationManager* const manager_;
};

void CompilerDriver::ResolveDexFile(jobject class_loader,
                                    const DexFile& dex_file,
                                    const std::vector<const DexFile*>& dex_files,
                                    ThreadPool* thread_pool,
                                    size_t thread_count,
                                    TimingLogger* timings) {
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();

  // TODO: we could resolve strings here, although the string table is largely filled with class
  //       and method names.

  ParallelCompilationManager context(class_linker, class_loader, this, &dex_file, dex_files,
                                     thread_pool);
  if (GetCompilerOptions().IsBootImage()) {
    // For images we resolve all types, such as array, whereas for applications just those with
    // classdefs are resolved by ResolveClassFieldsAndMethods.
    TimingLogger::ScopedTiming t("Resolve Types", timings);
    ResolveTypeVisitor visitor(&context);
    context.ForAll(0, dex_file.NumTypeIds(), &visitor, thread_count);
  }

  TimingLogger::ScopedTiming t("Resolve MethodsAndFields", timings);
  ResolveClassFieldsAndMethodsVisitor visitor(&context);
  context.ForAll(0, dex_file.NumClassDefs(), &visitor, thread_count);
}

void CompilerDriver::SetVerified(jobject class_loader,
                                 const std::vector<const DexFile*>& dex_files,
                                 TimingLogger* timings) {
  // This can be run in parallel.
  for (const DexFile* dex_file : dex_files) {
    CHECK(dex_file != nullptr);
    SetVerifiedDexFile(class_loader,
                       *dex_file,
                       dex_files,
                       parallel_thread_pool_.get(),
                       parallel_thread_count_,
                       timings);
  }
}

static void PopulateVerifiedMethods(const DexFile& dex_file,
                                    uint32_t class_def_index,
                                    VerificationResults* verification_results) {
  const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
  const uint8_t* class_data = dex_file.GetClassData(class_def);
  if (class_data == nullptr) {
    return;
  }
  ClassDataItemIterator it(dex_file, class_data);
  // Skip fields
  while (it.HasNextStaticField()) {
    it.Next();
  }
  while (it.HasNextInstanceField()) {
    it.Next();
  }

  while (it.HasNextDirectMethod()) {
    verification_results->CreateVerifiedMethodFor(MethodReference(&dex_file, it.GetMemberIndex()));
    it.Next();
  }

  while (it.HasNextVirtualMethod()) {
    verification_results->CreateVerifiedMethodFor(MethodReference(&dex_file, it.GetMemberIndex()));
    it.Next();
  }
  DCHECK(!it.HasNext());
}

static void LoadAndUpdateStatus(const DexFile& dex_file,
                                const DexFile::ClassDef& class_def,
                                mirror::Class::Status status,
                                Handle<mirror::ClassLoader> class_loader,
                                Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  StackHandleScope<1> hs(self);
  const char* descriptor = dex_file.GetClassDescriptor(class_def);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  Handle<mirror::Class> cls(hs.NewHandle<mirror::Class>(
      class_linker->FindClass(self, descriptor, class_loader)));
  if (cls != nullptr) {
    // Check that the class is resolved with the current dex file. We might get
    // a boot image class, or a class in a different dex file for multidex, and
    // we should not update the status in that case.
    if (&cls->GetDexFile() == &dex_file) {
      ObjectLock<mirror::Class> lock(self, cls);
      mirror::Class::SetStatus(cls, status, self);
    }
  } else {
    DCHECK(self->IsExceptionPending());
    self->ClearException();
  }
}

bool CompilerDriver::FastVerify(jobject jclass_loader,
                                const std::vector<const DexFile*>& dex_files,
                                TimingLogger* timings) {
  verifier::VerifierDeps* verifier_deps =
      Runtime::Current()->GetCompilerCallbacks()->GetVerifierDeps();
  // If there is an existing `VerifierDeps`, try to use it for fast verification.
  if (verifier_deps == nullptr) {
    return false;
  }
  TimingLogger::ScopedTiming t("Fast Verify", timings);
  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<2> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
  if (!verifier_deps->ValidateDependencies(class_loader, soa.Self())) {
    return false;
  }

  bool compiler_only_verifies = !GetCompilerOptions().IsAnyCompilationEnabled();

  // We successfully validated the dependencies, now update class status
  // of verified classes. Note that the dependencies also record which classes
  // could not be fully verified; we could try again, but that would hurt verification
  // time. So instead we assume these classes still need to be verified at
  // runtime.
  for (const DexFile* dex_file : dex_files) {
    // Fetch the list of unverified classes and turn it into a set for faster
    // lookups.
    const std::vector<dex::TypeIndex>& unverified_classes =
        verifier_deps->GetUnverifiedClasses(*dex_file);
    std::set<dex::TypeIndex> set(unverified_classes.begin(), unverified_classes.end());
    for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
      if (set.find(class_def.class_idx_) == set.end()) {
        if (compiler_only_verifies) {
          // Just update the compiled_classes_ map. The compiler doesn't need to resolve
          // the type.
          compiled_classes_.Overwrite(
              ClassReference(dex_file, i), new CompiledClass(mirror::Class::kStatusVerified));
        } else {
          // Update the class status, so later compilation stages know they don't need to verify
          // the class.
          LoadAndUpdateStatus(
              *dex_file, class_def, mirror::Class::kStatusVerified, class_loader, soa.Self());
          // Create `VerifiedMethod`s for each methods, the compiler expects one for
          // quickening or compiling.
          // Note that this means:
          // - We're only going to compile methods that did verify.
          // - Quickening will not do checkcast ellision.
          // TODO(ngeoffray): Reconsider this once we refactor compiler filters.
          PopulateVerifiedMethods(*dex_file, i, verification_results_);
        }
      } else if (!compiler_only_verifies) {
        // Make sure later compilation stages know they should not try to verify
        // this class again.
        LoadAndUpdateStatus(*dex_file,
                            class_def,
                            mirror::Class::kStatusRetryVerificationAtRuntime,
                            class_loader,
                            soa.Self());
      }
    }
  }
  return true;
}

void CompilerDriver::Verify(jobject jclass_loader,
                            const std::vector<const DexFile*>& dex_files,
                            TimingLogger* timings) {
  if (FastVerify(jclass_loader, dex_files, timings)) {
    return;
  }

  // If there is no existing `verifier_deps` (because of non-existing vdex), or
  // the existing `verifier_deps` is not valid anymore, create a new one for
  // non boot image compilation. The verifier will need it to record the new dependencies.
  // Then dex2oat can update the vdex file with these new dependencies.
  if (!GetCompilerOptions().IsBootImage()) {
    // Create the main VerifierDeps, and set it to this thread.
    verifier::VerifierDeps* verifier_deps = new verifier::VerifierDeps(dex_files);
    Runtime::Current()->GetCompilerCallbacks()->SetVerifierDeps(verifier_deps);
    Thread::Current()->SetVerifierDeps(verifier_deps);
    // Create per-thread VerifierDeps to avoid contention on the main one.
    // We will merge them after verification.
    for (ThreadPoolWorker* worker : parallel_thread_pool_->GetWorkers()) {
      worker->GetThread()->SetVerifierDeps(new verifier::VerifierDeps(dex_files));
    }
  }

  // Verification updates VerifierDeps and needs to run single-threaded to be deterministic.
  bool force_determinism = GetCompilerOptions().IsForceDeterminism();
  ThreadPool* verify_thread_pool =
      force_determinism ? single_thread_pool_.get() : parallel_thread_pool_.get();
  size_t verify_thread_count = force_determinism ? 1U : parallel_thread_count_;
  for (const DexFile* dex_file : dex_files) {
    CHECK(dex_file != nullptr);
    VerifyDexFile(jclass_loader,
                  *dex_file,
                  dex_files,
                  verify_thread_pool,
                  verify_thread_count,
                  timings);
  }

  if (!GetCompilerOptions().IsBootImage()) {
    // Merge all VerifierDeps into the main one.
    verifier::VerifierDeps* verifier_deps = Thread::Current()->GetVerifierDeps();
    for (ThreadPoolWorker* worker : parallel_thread_pool_->GetWorkers()) {
      verifier::VerifierDeps* thread_deps = worker->GetThread()->GetVerifierDeps();
      worker->GetThread()->SetVerifierDeps(nullptr);
      verifier_deps->MergeWith(*thread_deps, dex_files);;
      delete thread_deps;
    }
    Thread::Current()->SetVerifierDeps(nullptr);
  }
}

class VerifyClassVisitor : public CompilationVisitor {
 public:
  VerifyClassVisitor(const ParallelCompilationManager* manager, verifier::HardFailLogMode log_level)
     : manager_(manager), log_level_(log_level) {}

  virtual void Visit(size_t class_def_index) REQUIRES(!Locks::mutator_lock_) OVERRIDE {
    ATRACE_CALL();
    ScopedObjectAccess soa(Thread::Current());
    const DexFile& dex_file = *manager_->GetDexFile();
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    const char* descriptor = dex_file.GetClassDescriptor(class_def);
    ClassLinker* class_linker = manager_->GetClassLinker();
    jobject jclass_loader = manager_->GetClassLoader();
    StackHandleScope<3> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    Handle<mirror::Class> klass(
        hs.NewHandle(class_linker->FindClass(soa.Self(), descriptor, class_loader)));
    verifier::FailureKind failure_kind;
    if (klass == nullptr) {
      CHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();

      /*
       * At compile time, we can still structurally verify the class even if FindClass fails.
       * This is to ensure the class is structurally sound for compilation. An unsound class
       * will be rejected by the verifier and later skipped during compilation in the compiler.
       */
      Handle<mirror::DexCache> dex_cache(hs.NewHandle(class_linker->FindDexCache(
          soa.Self(), dex_file)));
      std::string error_msg;
      failure_kind =
          verifier::MethodVerifier::VerifyClass(soa.Self(),
                                                &dex_file,
                                                dex_cache,
                                                class_loader,
                                                class_def,
                                                Runtime::Current()->GetCompilerCallbacks(),
                                                true /* allow soft failures */,
                                                log_level_,
                                                &error_msg);
      if (failure_kind == verifier::FailureKind::kHardFailure) {
        LOG(ERROR) << "Verification failed on class " << PrettyDescriptor(descriptor)
                   << " because: " << error_msg;
        manager_->GetCompiler()->SetHadHardVerifierFailure();
      } else {
        // Force a soft failure for the VerifierDeps. This is a sanity measure, as
        // the vdex file already records that the class hasn't been resolved. It avoids
        // trying to do future verification optimizations when processing the vdex file.
        DCHECK(failure_kind == verifier::FailureKind::kSoftFailure ||
               failure_kind == verifier::FailureKind::kNoFailure)
            << failure_kind;
        failure_kind = verifier::FailureKind::kSoftFailure;
      }
    } else if (!SkipClass(jclass_loader, dex_file, klass.Get())) {
      CHECK(klass->IsResolved()) << klass->PrettyClass();
      failure_kind = class_linker->VerifyClass(soa.Self(), klass, log_level_);

      if (klass->IsErroneous()) {
        // ClassLinker::VerifyClass throws, which isn't useful in the compiler.
        CHECK(soa.Self()->IsExceptionPending());
        soa.Self()->ClearException();
        manager_->GetCompiler()->SetHadHardVerifierFailure();
      }

      CHECK(klass->ShouldVerifyAtRuntime() || klass->IsVerified() || klass->IsErroneous())
          << klass->PrettyDescriptor() << ": state=" << klass->GetStatus();

      // Class has a meaningful status for the compiler now, record it.
      ClassReference ref(manager_->GetDexFile(), class_def_index);
      manager_->GetCompiler()->RecordClassStatus(ref, klass->GetStatus());

      // It is *very* problematic if there are verification errors in the boot classpath. For example,
      // we rely on things working OK without verification when the decryption dialog is brought up.
      // So abort in a debug build if we find this violated.
      if (kIsDebugBuild) {
        // TODO(narayan): Remove this special case for signature polymorphic
        // invokes once verifier support is fully implemented.
        if (manager_->GetCompiler()->GetCompilerOptions().IsBootImage() &&
            !android::base::StartsWith(descriptor, "Ljava/lang/invoke/")) {
          DCHECK(klass->IsVerified()) << "Boot classpath class " << klass->PrettyClass()
              << " failed to fully verify: state= " << klass->GetStatus();
        }
        if (klass->IsVerified()) {
          DCHECK_EQ(failure_kind, verifier::FailureKind::kNoFailure);
        } else if (klass->ShouldVerifyAtRuntime()) {
          DCHECK_EQ(failure_kind, verifier::FailureKind::kSoftFailure);
        } else {
          DCHECK_EQ(failure_kind, verifier::FailureKind::kHardFailure);
        }
      }
    } else {
      // Make the skip a soft failure, essentially being considered as verify at runtime.
      failure_kind = verifier::FailureKind::kSoftFailure;
    }
    verifier::VerifierDeps::MaybeRecordVerificationStatus(
        dex_file, class_def.class_idx_, failure_kind);
    soa.Self()->AssertNoPendingException();
  }

 private:
  const ParallelCompilationManager* const manager_;
  const verifier::HardFailLogMode log_level_;
};

void CompilerDriver::VerifyDexFile(jobject class_loader,
                                   const DexFile& dex_file,
                                   const std::vector<const DexFile*>& dex_files,
                                   ThreadPool* thread_pool,
                                   size_t thread_count,
                                   TimingLogger* timings) {
  TimingLogger::ScopedTiming t("Verify Dex File", timings);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  ParallelCompilationManager context(class_linker, class_loader, this, &dex_file, dex_files,
                                     thread_pool);
  verifier::HardFailLogMode log_level = GetCompilerOptions().AbortOnHardVerifierFailure()
                              ? verifier::HardFailLogMode::kLogInternalFatal
                              : verifier::HardFailLogMode::kLogWarning;
  VerifyClassVisitor visitor(&context, log_level);
  context.ForAll(0, dex_file.NumClassDefs(), &visitor, thread_count);
}

class SetVerifiedClassVisitor : public CompilationVisitor {
 public:
  explicit SetVerifiedClassVisitor(const ParallelCompilationManager* manager) : manager_(manager) {}

  virtual void Visit(size_t class_def_index) REQUIRES(!Locks::mutator_lock_) OVERRIDE {
    ATRACE_CALL();
    ScopedObjectAccess soa(Thread::Current());
    const DexFile& dex_file = *manager_->GetDexFile();
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    const char* descriptor = dex_file.GetClassDescriptor(class_def);
    ClassLinker* class_linker = manager_->GetClassLinker();
    jobject jclass_loader = manager_->GetClassLoader();
    StackHandleScope<3> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    Handle<mirror::Class> klass(
        hs.NewHandle(class_linker->FindClass(soa.Self(), descriptor, class_loader)));
    // Class might have failed resolution. Then don't set it to verified.
    if (klass != nullptr) {
      // Only do this if the class is resolved. If even resolution fails, quickening will go very,
      // very wrong.
      if (klass->IsResolved() && !klass->IsErroneousResolved()) {
        if (klass->GetStatus() < mirror::Class::kStatusVerified) {
          ObjectLock<mirror::Class> lock(soa.Self(), klass);
          // Set class status to verified.
          mirror::Class::SetStatus(klass, mirror::Class::kStatusVerified, soa.Self());
          // Mark methods as pre-verified. If we don't do this, the interpreter will run with
          // access checks.
          klass->SetSkipAccessChecksFlagOnAllMethods(
              GetInstructionSetPointerSize(manager_->GetCompiler()->GetInstructionSet()));
          klass->SetVerificationAttempted();
        }
        // Record the final class status if necessary.
        ClassReference ref(manager_->GetDexFile(), class_def_index);
        manager_->GetCompiler()->RecordClassStatus(ref, klass->GetStatus());
      }
    } else {
      Thread* self = soa.Self();
      DCHECK(self->IsExceptionPending());
      self->ClearException();
    }
  }

 private:
  const ParallelCompilationManager* const manager_;
};

void CompilerDriver::SetVerifiedDexFile(jobject class_loader,
                                        const DexFile& dex_file,
                                        const std::vector<const DexFile*>& dex_files,
                                        ThreadPool* thread_pool,
                                        size_t thread_count,
                                        TimingLogger* timings) {
  TimingLogger::ScopedTiming t("Verify Dex File", timings);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  ParallelCompilationManager context(class_linker, class_loader, this, &dex_file, dex_files,
                                     thread_pool);
  SetVerifiedClassVisitor visitor(&context);
  context.ForAll(0, dex_file.NumClassDefs(), &visitor, thread_count);
}

class InitializeClassVisitor : public CompilationVisitor {
 public:
  explicit InitializeClassVisitor(const ParallelCompilationManager* manager) : manager_(manager) {}

  void Visit(size_t class_def_index) REQUIRES(!Locks::mutator_lock_) OVERRIDE {
    ATRACE_CALL();
    jobject jclass_loader = manager_->GetClassLoader();
    const DexFile& dex_file = *manager_->GetDexFile();
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    const DexFile::TypeId& class_type_id = dex_file.GetTypeId(class_def.class_idx_);
    const char* descriptor = dex_file.StringDataByIdx(class_type_id.descriptor_idx_);

    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<3> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    Handle<mirror::Class> klass(
        hs.NewHandle(manager_->GetClassLinker()->FindClass(soa.Self(), descriptor, class_loader)));

    if (klass != nullptr && !SkipClass(jclass_loader, dex_file, klass.Get())) {
      // Only try to initialize classes that were successfully verified.
      if (klass->IsVerified()) {
        // Attempt to initialize the class but bail if we either need to initialize the super-class
        // or static fields.
        manager_->GetClassLinker()->EnsureInitialized(soa.Self(), klass, false, false);
        if (!klass->IsInitialized()) {
          // We don't want non-trivial class initialization occurring on multiple threads due to
          // deadlock problems. For example, a parent class is initialized (holding its lock) that
          // refers to a sub-class in its static/class initializer causing it to try to acquire the
          // sub-class' lock. While on a second thread the sub-class is initialized (holding its lock)
          // after first initializing its parents, whose locks are acquired. This leads to a
          // parent-to-child and a child-to-parent lock ordering and consequent potential deadlock.
          // We need to use an ObjectLock due to potential suspension in the interpreting code. Rather
          // than use a special Object for the purpose we use the Class of java.lang.Class.
          Handle<mirror::Class> h_klass(hs.NewHandle(klass->GetClass()));
          ObjectLock<mirror::Class> lock(soa.Self(), h_klass);
          // Attempt to initialize allowing initialization of parent classes but still not static
          // fields.
          manager_->GetClassLinker()->EnsureInitialized(soa.Self(), klass, false, true);
          if (!klass->IsInitialized()) {
            // We need to initialize static fields, we only do this for image classes that aren't
            // marked with the $NoPreloadHolder (which implies this should not be initialized early).
            bool can_init_static_fields =
                manager_->GetCompiler()->GetCompilerOptions().IsBootImage() &&
                manager_->GetCompiler()->IsImageClass(descriptor) &&
                !StringPiece(descriptor).ends_with("$NoPreloadHolder;");
            if (can_init_static_fields) {
              VLOG(compiler) << "Initializing: " << descriptor;
              // TODO multithreading support. We should ensure the current compilation thread has
              // exclusive access to the runtime and the transaction. To achieve this, we could use
              // a ReaderWriterMutex but we're holding the mutator lock so we fail mutex sanity
              // checks in Thread::AssertThreadSuspensionIsAllowable.
              Runtime* const runtime = Runtime::Current();
              Transaction transaction;

              // Run the class initializer in transaction mode.
              runtime->EnterTransactionMode(&transaction);
              const mirror::Class::Status old_status = klass->GetStatus();
              bool success = manager_->GetClassLinker()->EnsureInitialized(soa.Self(), klass, true,
                                                                           true);
              // TODO we detach transaction from runtime to indicate we quit the transactional
              // mode which prevents the GC from visiting objects modified during the transaction.
              // Ensure GC is not run so don't access freed objects when aborting transaction.

              {
                ScopedAssertNoThreadSuspension ants("Transaction end");
                runtime->ExitTransactionMode();

                if (!success) {
                  CHECK(soa.Self()->IsExceptionPending());
                  mirror::Throwable* exception = soa.Self()->GetException();
                  VLOG(compiler) << "Initialization of " << descriptor << " aborted because of "
                      << exception->Dump();
                  std::ostream* file_log = manager_->GetCompiler()->
                      GetCompilerOptions().GetInitFailureOutput();
                  if (file_log != nullptr) {
                    *file_log << descriptor << "\n";
                    *file_log << exception->Dump() << "\n";
                  }
                  soa.Self()->ClearException();
                  transaction.Rollback();
                  CHECK_EQ(old_status, klass->GetStatus()) << "Previous class status not restored";
                }
              }

              if (!success) {
                // On failure, still intern strings of static fields and seen in <clinit>, as these
                // will be created in the zygote. This is separated from the transaction code just
                // above as we will allocate strings, so must be allowed to suspend.
                InternStrings(klass, class_loader);
              }
            }
          }
          soa.Self()->AssertNoPendingException();
        }
      }
      // Record the final class status if necessary.
      ClassReference ref(manager_->GetDexFile(), class_def_index);
      manager_->GetCompiler()->RecordClassStatus(ref, klass->GetStatus());
    }
    // Clear any class not found or verification exceptions.
    soa.Self()->ClearException();
  }

 private:
  void InternStrings(Handle<mirror::Class> klass, Handle<mirror::ClassLoader> class_loader)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(manager_->GetCompiler()->GetCompilerOptions().IsBootImage());
    DCHECK(klass->IsVerified());
    DCHECK(!klass->IsInitialized());

    StackHandleScope<1> hs(Thread::Current());
    Handle<mirror::DexCache> h_dex_cache = hs.NewHandle(klass->GetDexCache());
    const DexFile* dex_file = manager_->GetDexFile();
    const DexFile::ClassDef* class_def = klass->GetClassDef();
    ClassLinker* class_linker = manager_->GetClassLinker();

    // Check encoded final field values for strings and intern.
    annotations::RuntimeEncodedStaticFieldValueIterator value_it(*dex_file,
                                                                 &h_dex_cache,
                                                                 &class_loader,
                                                                 manager_->GetClassLinker(),
                                                                 *class_def);
    for ( ; value_it.HasNext(); value_it.Next()) {
      if (value_it.GetValueType() == annotations::RuntimeEncodedStaticFieldValueIterator::kString) {
        // Resolve the string. This will intern the string.
        art::ObjPtr<mirror::String> resolved = class_linker->ResolveString(
            *dex_file, dex::StringIndex(value_it.GetJavaValue().i), h_dex_cache);
        CHECK(resolved != nullptr);
      }
    }

    // Intern strings seen in <clinit>.
    ArtMethod* clinit = klass->FindClassInitializer(class_linker->GetImagePointerSize());
    if (clinit != nullptr) {
      const DexFile::CodeItem* code_item = clinit->GetCodeItem();
      DCHECK(code_item != nullptr);
      const Instruction* inst = Instruction::At(code_item->insns_);

      const uint32_t insns_size = code_item->insns_size_in_code_units_;
      for (uint32_t dex_pc = 0; dex_pc < insns_size;) {
        if (inst->Opcode() == Instruction::CONST_STRING) {
          ObjPtr<mirror::String> s = class_linker->ResolveString(
              *dex_file, dex::StringIndex(inst->VRegB_21c()), h_dex_cache);
          CHECK(s != nullptr);
        } else if (inst->Opcode() == Instruction::CONST_STRING_JUMBO) {
          ObjPtr<mirror::String> s = class_linker->ResolveString(
              *dex_file, dex::StringIndex(inst->VRegB_31c()), h_dex_cache);
          CHECK(s != nullptr);
        }
        dex_pc += inst->SizeInCodeUnits();
        inst = inst->Next();
      }
    }
  }

  const ParallelCompilationManager* const manager_;
};

void CompilerDriver::InitializeClasses(jobject jni_class_loader,
                                       const DexFile& dex_file,
                                       const std::vector<const DexFile*>& dex_files,
                                       TimingLogger* timings) {
  TimingLogger::ScopedTiming t("InitializeNoClinit", timings);

  // Initialization allocates objects and needs to run single-threaded to be deterministic.
  bool force_determinism = GetCompilerOptions().IsForceDeterminism();
  ThreadPool* init_thread_pool = force_determinism
                                     ? single_thread_pool_.get()
                                     : parallel_thread_pool_.get();
  size_t init_thread_count = force_determinism ? 1U : parallel_thread_count_;

  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  ParallelCompilationManager context(class_linker, jni_class_loader, this, &dex_file, dex_files,
                                     init_thread_pool);
  if (GetCompilerOptions().IsBootImage()) {
    // TODO: remove this when transactional mode supports multithreading.
    init_thread_count = 1U;
  }
  InitializeClassVisitor visitor(&context);
  context.ForAll(0, dex_file.NumClassDefs(), &visitor, init_thread_count);
}

class InitializeArrayClassesAndCreateConflictTablesVisitor : public ClassVisitor {
 public:
  explicit InitializeArrayClassesAndCreateConflictTablesVisitor(VariableSizedHandleScope& hs)
      : hs_(hs) {}

  virtual bool operator()(ObjPtr<mirror::Class> klass) OVERRIDE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(klass)) {
      return true;
    }
    if (klass->IsArrayClass()) {
      StackHandleScope<1> hs(Thread::Current());
      auto h_klass = hs.NewHandleWrapper(&klass);
      Runtime::Current()->GetClassLinker()->EnsureInitialized(hs.Self(), h_klass, true, true);
    }
    // Collect handles since there may be thread suspension in future EnsureInitialized.
    to_visit_.push_back(hs_.NewHandle(klass));
    return true;
  }

  void FillAllIMTAndConflictTables() REQUIRES_SHARED(Locks::mutator_lock_) {
    for (Handle<mirror::Class> c : to_visit_) {
      // Create the conflict tables.
      FillIMTAndConflictTables(c.Get());
    }
  }

 private:
  void FillIMTAndConflictTables(ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!klass->ShouldHaveImt()) {
      return;
    }
    if (visited_classes_.find(klass) != visited_classes_.end()) {
      return;
    }
    if (klass->HasSuperClass()) {
      FillIMTAndConflictTables(klass->GetSuperClass());
    }
    if (!klass->IsTemp()) {
      Runtime::Current()->GetClassLinker()->FillIMTAndConflictTables(klass);
    }
    visited_classes_.insert(klass);
  }

  VariableSizedHandleScope& hs_;
  std::vector<Handle<mirror::Class>> to_visit_;
  std::unordered_set<ObjPtr<mirror::Class>, HashObjPtr> visited_classes_;
};

void CompilerDriver::InitializeClasses(jobject class_loader,
                                       const std::vector<const DexFile*>& dex_files,
                                       TimingLogger* timings) {
  for (size_t i = 0; i != dex_files.size(); ++i) {
    const DexFile* dex_file = dex_files[i];
    CHECK(dex_file != nullptr);
    InitializeClasses(class_loader, *dex_file, dex_files, timings);
  }
  if (GetCompilerOptions().IsBootImage() || GetCompilerOptions().IsAppImage()) {
    // Make sure that we call EnsureIntiailized on all the array classes to call
    // SetVerificationAttempted so that the access flags are set. If we do not do this they get
    // changed at runtime resulting in more dirty image pages.
    // Also create conflict tables.
    // Only useful if we are compiling an image (image_classes_ is not null).
    ScopedObjectAccess soa(Thread::Current());
    VariableSizedHandleScope hs(soa.Self());
    InitializeArrayClassesAndCreateConflictTablesVisitor visitor(hs);
    Runtime::Current()->GetClassLinker()->VisitClassesWithoutClassesLock(&visitor);
    visitor.FillAllIMTAndConflictTables();
  }
  if (GetCompilerOptions().IsBootImage()) {
    // Prune garbage objects created during aborted transactions.
    Runtime::Current()->GetHeap()->CollectGarbage(true);
  }
}

void CompilerDriver::Compile(jobject class_loader,
                             const std::vector<const DexFile*>& dex_files,
                             TimingLogger* timings) {
  if (kDebugProfileGuidedCompilation) {
    LOG(INFO) << "[ProfileGuidedCompilation] " <<
        ((profile_compilation_info_ == nullptr)
            ? "null"
            : profile_compilation_info_->DumpInfo(&dex_files));
  }

  DCHECK(current_dex_to_dex_methods_ == nullptr);
  for (const DexFile* dex_file : dex_files) {
    CHECK(dex_file != nullptr);
    CompileDexFile(class_loader,
                   *dex_file,
                   dex_files,
                   parallel_thread_pool_.get(),
                   parallel_thread_count_,
                   timings);
    const ArenaPool* const arena_pool = Runtime::Current()->GetArenaPool();
    const size_t arena_alloc = arena_pool->GetBytesAllocated();
    max_arena_alloc_ = std::max(arena_alloc, max_arena_alloc_);
    Runtime::Current()->ReclaimArenaPoolMemory();
  }

  ArrayRef<DexFileMethodSet> dex_to_dex_references;
  {
    // From this point on, we shall not modify dex_to_dex_references_, so
    // just grab a reference to it that we use without holding the mutex.
    MutexLock lock(Thread::Current(), dex_to_dex_references_lock_);
    dex_to_dex_references = ArrayRef<DexFileMethodSet>(dex_to_dex_references_);
  }
  for (const auto& method_set : dex_to_dex_references) {
    current_dex_to_dex_methods_ = &method_set.GetMethodIndexes();
    CompileDexFile(class_loader,
                   method_set.GetDexFile(),
                   dex_files,
                   parallel_thread_pool_.get(),
                   parallel_thread_count_,
                   timings);
  }
  current_dex_to_dex_methods_ = nullptr;

  VLOG(compiler) << "Compile: " << GetMemoryUsageString(false);
}

class CompileClassVisitor : public CompilationVisitor {
 public:
  explicit CompileClassVisitor(const ParallelCompilationManager* manager) : manager_(manager) {}

  virtual void Visit(size_t class_def_index) REQUIRES(!Locks::mutator_lock_) OVERRIDE {
    ATRACE_CALL();
    const DexFile& dex_file = *manager_->GetDexFile();
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    ClassLinker* class_linker = manager_->GetClassLinker();
    jobject jclass_loader = manager_->GetClassLoader();
    ClassReference ref(&dex_file, class_def_index);
    // Skip compiling classes with generic verifier failures since they will still fail at runtime
    if (manager_->GetCompiler()->verification_results_->IsClassRejected(ref)) {
      return;
    }
    // Use a scoped object access to perform to the quick SkipClass check.
    const char* descriptor = dex_file.GetClassDescriptor(class_def);
    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<3> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    Handle<mirror::Class> klass(
        hs.NewHandle(class_linker->FindClass(soa.Self(), descriptor, class_loader)));
    Handle<mirror::DexCache> dex_cache;
    if (klass == nullptr) {
      soa.Self()->AssertPendingException();
      soa.Self()->ClearException();
      dex_cache = hs.NewHandle(class_linker->FindDexCache(soa.Self(), dex_file));
    } else if (SkipClass(jclass_loader, dex_file, klass.Get())) {
      return;
    } else {
      dex_cache = hs.NewHandle(klass->GetDexCache());
    }

    const uint8_t* class_data = dex_file.GetClassData(class_def);
    if (class_data == nullptr) {
      // empty class, probably a marker interface
      return;
    }

    // Go to native so that we don't block GC during compilation.
    ScopedThreadSuspension sts(soa.Self(), kNative);

    CompilerDriver* const driver = manager_->GetCompiler();

    // Can we run DEX-to-DEX compiler on this class ?
    optimizer::DexToDexCompilationLevel dex_to_dex_compilation_level =
        GetDexToDexCompilationLevel(soa.Self(), *driver, jclass_loader, dex_file, class_def);

    ClassDataItemIterator it(dex_file, class_data);
    // Skip fields
    while (it.HasNextStaticField()) {
      it.Next();
    }
    while (it.HasNextInstanceField()) {
      it.Next();
    }

    bool compilation_enabled = driver->IsClassToCompile(
        dex_file.StringByTypeIdx(class_def.class_idx_));

    // Compile direct methods
    int64_t previous_direct_method_idx = -1;
    while (it.HasNextDirectMethod()) {
      uint32_t method_idx = it.GetMemberIndex();
      if (method_idx == previous_direct_method_idx) {
        // smali can create dex files with two encoded_methods sharing the same method_idx
        // http://code.google.com/p/smali/issues/detail?id=119
        it.Next();
        continue;
      }
      previous_direct_method_idx = method_idx;
      CompileMethod(soa.Self(),
                    driver,
                    it.GetMethodCodeItem(),
                    it.GetMethodAccessFlags(),
                    it.GetMethodInvokeType(class_def),
                    class_def_index,
                    method_idx,
                    class_loader,
                    dex_file,
                    dex_to_dex_compilation_level,
                    compilation_enabled,
                    dex_cache);
      it.Next();
    }
    // Compile virtual methods
    int64_t previous_virtual_method_idx = -1;
    while (it.HasNextVirtualMethod()) {
      uint32_t method_idx = it.GetMemberIndex();
      if (method_idx == previous_virtual_method_idx) {
        // smali can create dex files with two encoded_methods sharing the same method_idx
        // http://code.google.com/p/smali/issues/detail?id=119
        it.Next();
        continue;
      }
      previous_virtual_method_idx = method_idx;
      CompileMethod(soa.Self(),
                    driver, it.GetMethodCodeItem(),
                    it.GetMethodAccessFlags(),
                    it.GetMethodInvokeType(class_def),
                    class_def_index,
                    method_idx,
                    class_loader,
                    dex_file,
                    dex_to_dex_compilation_level,
                    compilation_enabled,
                    dex_cache);
      it.Next();
    }
    DCHECK(!it.HasNext());
  }

 private:
  const ParallelCompilationManager* const manager_;
};

void CompilerDriver::CompileDexFile(jobject class_loader,
                                    const DexFile& dex_file,
                                    const std::vector<const DexFile*>& dex_files,
                                    ThreadPool* thread_pool,
                                    size_t thread_count,
                                    TimingLogger* timings) {
  TimingLogger::ScopedTiming t("Compile Dex File", timings);
  ParallelCompilationManager context(Runtime::Current()->GetClassLinker(), class_loader, this,
                                     &dex_file, dex_files, thread_pool);
  CompileClassVisitor visitor(&context);
  context.ForAll(0, dex_file.NumClassDefs(), &visitor, thread_count);
}

void CompilerDriver::AddCompiledMethod(const MethodReference& method_ref,
                                       CompiledMethod* const compiled_method,
                                       size_t non_relative_linker_patch_count) {
  DCHECK(GetCompiledMethod(method_ref) == nullptr)
      << method_ref.dex_file->PrettyMethod(method_ref.dex_method_index);
  MethodTable::InsertResult result = compiled_methods_.Insert(method_ref,
                                                              /*expected*/ nullptr,
                                                              compiled_method);
  CHECK(result == MethodTable::kInsertResultSuccess);
  non_relative_linker_patch_count_.FetchAndAddRelaxed(non_relative_linker_patch_count);
  DCHECK(GetCompiledMethod(method_ref) != nullptr)
      << method_ref.dex_file->PrettyMethod(method_ref.dex_method_index);
}

CompiledClass* CompilerDriver::GetCompiledClass(ClassReference ref) const {
  MutexLock mu(Thread::Current(), compiled_classes_lock_);
  ClassTable::const_iterator it = compiled_classes_.find(ref);
  if (it == compiled_classes_.end()) {
    return nullptr;
  }
  CHECK(it->second != nullptr);
  return it->second;
}

void CompilerDriver::RecordClassStatus(ClassReference ref, mirror::Class::Status status) {
  switch (status) {
    case mirror::Class::kStatusNotReady:
    case mirror::Class::kStatusErrorResolved:
    case mirror::Class::kStatusErrorUnresolved:
    case mirror::Class::kStatusRetryVerificationAtRuntime:
    case mirror::Class::kStatusVerified:
    case mirror::Class::kStatusInitialized:
    case mirror::Class::kStatusResolved:
      break;  // Expected states.
    default:
      LOG(FATAL) << "Unexpected class status for class "
          << PrettyDescriptor(ref.first->GetClassDescriptor(ref.first->GetClassDef(ref.second)))
          << " of " << status;
  }

  MutexLock mu(Thread::Current(), compiled_classes_lock_);
  auto it = compiled_classes_.find(ref);
  if (it == compiled_classes_.end()) {
    CompiledClass* compiled_class = new CompiledClass(status);
    compiled_classes_.Overwrite(ref, compiled_class);
  } else if (status > it->second->GetStatus()) {
    // Update the status if we now have a greater one. This happens with vdex,
    // which records a class is verified, but does not resolve it.
    it->second->SetStatus(status);
  }
}

CompiledMethod* CompilerDriver::GetCompiledMethod(MethodReference ref) const {
  CompiledMethod* compiled_method = nullptr;
  compiled_methods_.Get(ref, &compiled_method);
  return compiled_method;
}

bool CompilerDriver::IsMethodVerifiedWithoutFailures(uint32_t method_idx,
                                                     uint16_t class_def_idx,
                                                     const DexFile& dex_file) const {
  const VerifiedMethod* verified_method = GetVerifiedMethod(&dex_file, method_idx);
  if (verified_method != nullptr) {
    return !verified_method->HasVerificationFailures();
  }

  // If we can't find verification metadata, check if this is a system class (we trust that system
  // classes have their methods verified). If it's not, be conservative and assume the method
  // has not been verified successfully.

  // TODO: When compiling the boot image it should be safe to assume that everything is verified,
  // even if methods are not found in the verification cache.
  const char* descriptor = dex_file.GetClassDescriptor(dex_file.GetClassDef(class_def_idx));
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);
  bool is_system_class = class_linker->FindSystemClass(self, descriptor) != nullptr;
  if (!is_system_class) {
    self->ClearException();
  }
  return is_system_class;
}

size_t CompilerDriver::GetNonRelativeLinkerPatchCount() const {
  return non_relative_linker_patch_count_.LoadRelaxed();
}

void CompilerDriver::SetRequiresConstructorBarrier(Thread* self,
                                                   const DexFile* dex_file,
                                                   uint16_t class_def_index,
                                                   bool requires) {
  WriterMutexLock mu(self, requires_constructor_barrier_lock_);
  requires_constructor_barrier_.emplace(ClassReference(dex_file, class_def_index), requires);
}

bool CompilerDriver::RequiresConstructorBarrier(Thread* self,
                                                const DexFile* dex_file,
                                                uint16_t class_def_index) {
  ClassReference class_ref(dex_file, class_def_index);
  {
    ReaderMutexLock mu(self, requires_constructor_barrier_lock_);
    auto it = requires_constructor_barrier_.find(class_ref);
    if (it != requires_constructor_barrier_.end()) {
      return it->second;
    }
  }
  WriterMutexLock mu(self, requires_constructor_barrier_lock_);
  const bool requires = RequiresConstructorBarrier(*dex_file, class_def_index);
  requires_constructor_barrier_.emplace(class_ref, requires);
  return requires;
}

std::string CompilerDriver::GetMemoryUsageString(bool extended) const {
  std::ostringstream oss;
  const gc::Heap* const heap = Runtime::Current()->GetHeap();
  const size_t java_alloc = heap->GetBytesAllocated();
  oss << "arena alloc=" << PrettySize(max_arena_alloc_) << " (" << max_arena_alloc_ << "B)";
  oss << " java alloc=" << PrettySize(java_alloc) << " (" << java_alloc << "B)";
#if defined(__BIONIC__) || defined(__GLIBC__)
  const struct mallinfo info = mallinfo();
  const size_t allocated_space = static_cast<size_t>(info.uordblks);
  const size_t free_space = static_cast<size_t>(info.fordblks);
  oss << " native alloc=" << PrettySize(allocated_space) << " (" << allocated_space << "B)"
      << " free=" << PrettySize(free_space) << " (" << free_space << "B)";
#endif
  compiled_method_storage_.DumpMemoryUsage(oss, extended);
  return oss.str();
}

bool CompilerDriver::MayInlineInternal(const DexFile* inlined_from,
                                       const DexFile* inlined_into) const {
  // We're not allowed to inline across dex files if we're the no-inline-from dex file.
  if (inlined_from != inlined_into &&
      compiler_options_->GetNoInlineFromDexFile() != nullptr &&
      ContainsElement(*compiler_options_->GetNoInlineFromDexFile(), inlined_from)) {
    return false;
  }

  return true;
}

void CompilerDriver::InitializeThreadPools() {
  size_t parallel_count = parallel_thread_count_ > 0 ? parallel_thread_count_ - 1 : 0;
  parallel_thread_pool_.reset(
      new ThreadPool("Compiler driver thread pool", parallel_count));
  single_thread_pool_.reset(new ThreadPool("Single-threaded Compiler driver thread pool", 0));
}

void CompilerDriver::FreeThreadPools() {
  parallel_thread_pool_.reset();
  single_thread_pool_.reset();
}

}  // namespace art
