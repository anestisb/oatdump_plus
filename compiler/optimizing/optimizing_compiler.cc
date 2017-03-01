/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "optimizing_compiler.h"

#include <fstream>
#include <memory>
#include <sstream>

#include <stdint.h>

#include "android-base/strings.h"

#ifdef ART_ENABLE_CODEGEN_arm
#include "dex_cache_array_fixups_arm.h"
#endif

#ifdef ART_ENABLE_CODEGEN_arm64
#include "instruction_simplifier_arm64.h"
#endif

#ifdef ART_ENABLE_CODEGEN_mips
#include "dex_cache_array_fixups_mips.h"
#include "pc_relative_fixups_mips.h"
#endif

#ifdef ART_ENABLE_CODEGEN_x86
#include "pc_relative_fixups_x86.h"
#endif

#if defined(ART_ENABLE_CODEGEN_x86) || defined(ART_ENABLE_CODEGEN_x86_64)
#include "x86_memory_gen.h"
#endif

#include "art_method-inl.h"
#include "base/arena_allocator.h"
#include "base/arena_containers.h"
#include "base/dumpable.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/timing_logger.h"
#include "bounds_check_elimination.h"
#include "builder.h"
#include "cha_guard_optimization.h"
#include "code_generator.h"
#include "code_sinking.h"
#include "compiled_method.h"
#include "compiler.h"
#include "constant_folding.h"
#include "dead_code_elimination.h"
#include "debug/elf_debug_writer.h"
#include "debug/method_debug_info.h"
#include "dex/verification_results.h"
#include "dex/verified_method.h"
#include "dex_file_types.h"
#include "driver/compiler_driver-inl.h"
#include "driver/compiler_options.h"
#include "driver/dex_compilation_unit.h"
#include "elf_writer_quick.h"
#include "graph_checker.h"
#include "graph_visualizer.h"
#include "gvn.h"
#include "induction_var_analysis.h"
#include "inliner.h"
#include "instruction_simplifier.h"
#include "instruction_simplifier_arm.h"
#include "intrinsics.h"
#include "jit/debugger_interface.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni/quick/jni_compiler.h"
#include "licm.h"
#include "load_store_elimination.h"
#include "loop_optimization.h"
#include "nodes.h"
#include "oat_quick_method_header.h"
#include "prepare_for_register_allocation.h"
#include "reference_type_propagation.h"
#include "register_allocator_linear_scan.h"
#include "select_generator.h"
#include "scheduler.h"
#include "sharpening.h"
#include "side_effects_analysis.h"
#include "ssa_builder.h"
#include "ssa_liveness_analysis.h"
#include "ssa_phi_elimination.h"
#include "utils/assembler.h"
#include "verifier/verifier_compiler_binding.h"

namespace art {

static constexpr size_t kArenaAllocatorMemoryReportThreshold = 8 * MB;

static constexpr const char* kPassNameSeparator = "$";

/**
 * Used by the code generator, to allocate the code in a vector.
 */
class CodeVectorAllocator FINAL : public CodeAllocator {
 public:
  explicit CodeVectorAllocator(ArenaAllocator* arena)
      : memory_(arena->Adapter(kArenaAllocCodeBuffer)),
        size_(0) {}

  virtual uint8_t* Allocate(size_t size) {
    size_ = size;
    memory_.resize(size);
    return &memory_[0];
  }

  size_t GetSize() const { return size_; }
  const ArenaVector<uint8_t>& GetMemory() const { return memory_; }
  uint8_t* GetData() { return memory_.data(); }

 private:
  ArenaVector<uint8_t> memory_;
  size_t size_;

  DISALLOW_COPY_AND_ASSIGN(CodeVectorAllocator);
};

/**
 * Filter to apply to the visualizer. Methods whose name contain that filter will
 * be dumped.
 */
static constexpr const char kStringFilter[] = "";

class PassScope;

class PassObserver : public ValueObject {
 public:
  PassObserver(HGraph* graph,
               CodeGenerator* codegen,
               std::ostream* visualizer_output,
               CompilerDriver* compiler_driver,
               Mutex& dump_mutex)
      : graph_(graph),
        cached_method_name_(),
        timing_logger_enabled_(compiler_driver->GetDumpPasses()),
        timing_logger_(timing_logger_enabled_ ? GetMethodName() : "", true, true),
        disasm_info_(graph->GetArena()),
        visualizer_oss_(),
        visualizer_output_(visualizer_output),
        visualizer_enabled_(!compiler_driver->GetCompilerOptions().GetDumpCfgFileName().empty()),
        visualizer_(&visualizer_oss_, graph, *codegen),
        visualizer_dump_mutex_(dump_mutex),
        graph_in_bad_state_(false) {
    if (timing_logger_enabled_ || visualizer_enabled_) {
      if (!IsVerboseMethod(compiler_driver, GetMethodName())) {
        timing_logger_enabled_ = visualizer_enabled_ = false;
      }
      if (visualizer_enabled_) {
        visualizer_.PrintHeader(GetMethodName());
        codegen->SetDisassemblyInformation(&disasm_info_);
      }
    }
  }

  ~PassObserver() {
    if (timing_logger_enabled_) {
      LOG(INFO) << "TIMINGS " << GetMethodName();
      LOG(INFO) << Dumpable<TimingLogger>(timing_logger_);
    }
    DCHECK(visualizer_oss_.str().empty());
  }

  void DumpDisassembly() REQUIRES(!visualizer_dump_mutex_) {
    if (visualizer_enabled_) {
      visualizer_.DumpGraphWithDisassembly();
      FlushVisualizer();
    }
  }

  void SetGraphInBadState() { graph_in_bad_state_ = true; }

  const char* GetMethodName() {
    // PrettyMethod() is expensive, so we delay calling it until we actually have to.
    if (cached_method_name_.empty()) {
      cached_method_name_ = graph_->GetDexFile().PrettyMethod(graph_->GetMethodIdx());
    }
    return cached_method_name_.c_str();
  }

 private:
  void StartPass(const char* pass_name) REQUIRES(!visualizer_dump_mutex_) {
    VLOG(compiler) << "Starting pass: " << pass_name;
    // Dump graph first, then start timer.
    if (visualizer_enabled_) {
      visualizer_.DumpGraph(pass_name, /* is_after_pass */ false, graph_in_bad_state_);
      FlushVisualizer();
    }
    if (timing_logger_enabled_) {
      timing_logger_.StartTiming(pass_name);
    }
  }

  void FlushVisualizer() REQUIRES(!visualizer_dump_mutex_) {
    MutexLock mu(Thread::Current(), visualizer_dump_mutex_);
    *visualizer_output_ << visualizer_oss_.str();
    visualizer_output_->flush();
    visualizer_oss_.str("");
    visualizer_oss_.clear();
  }

  void EndPass(const char* pass_name) REQUIRES(!visualizer_dump_mutex_) {
    // Pause timer first, then dump graph.
    if (timing_logger_enabled_) {
      timing_logger_.EndTiming();
    }
    if (visualizer_enabled_) {
      visualizer_.DumpGraph(pass_name, /* is_after_pass */ true, graph_in_bad_state_);
      FlushVisualizer();
    }

    // Validate the HGraph if running in debug mode.
    if (kIsDebugBuild) {
      if (!graph_in_bad_state_) {
        GraphChecker checker(graph_);
        checker.Run();
        if (!checker.IsValid()) {
          LOG(FATAL) << "Error after " << pass_name << ": " << Dumpable<GraphChecker>(checker);
        }
      }
    }
  }

  static bool IsVerboseMethod(CompilerDriver* compiler_driver, const char* method_name) {
    // Test an exact match to --verbose-methods. If verbose-methods is set, this overrides an
    // empty kStringFilter matching all methods.
    if (compiler_driver->GetCompilerOptions().HasVerboseMethods()) {
      return compiler_driver->GetCompilerOptions().IsVerboseMethod(method_name);
    }

    // Test the kStringFilter sub-string. constexpr helper variable to silence unreachable-code
    // warning when the string is empty.
    constexpr bool kStringFilterEmpty = arraysize(kStringFilter) <= 1;
    if (kStringFilterEmpty || strstr(method_name, kStringFilter) != nullptr) {
      return true;
    }

    return false;
  }

  HGraph* const graph_;

  std::string cached_method_name_;

  bool timing_logger_enabled_;
  TimingLogger timing_logger_;

  DisassemblyInformation disasm_info_;

  std::ostringstream visualizer_oss_;
  std::ostream* visualizer_output_;
  bool visualizer_enabled_;
  HGraphVisualizer visualizer_;
  Mutex& visualizer_dump_mutex_;

  // Flag to be set by the compiler if the pass failed and the graph is not
  // expected to validate.
  bool graph_in_bad_state_;

  friend PassScope;

  DISALLOW_COPY_AND_ASSIGN(PassObserver);
};

class PassScope : public ValueObject {
 public:
  PassScope(const char *pass_name, PassObserver* pass_observer)
      : pass_name_(pass_name),
        pass_observer_(pass_observer) {
    pass_observer_->StartPass(pass_name_);
  }

  ~PassScope() {
    pass_observer_->EndPass(pass_name_);
  }

 private:
  const char* const pass_name_;
  PassObserver* const pass_observer_;
};

class OptimizingCompiler FINAL : public Compiler {
 public:
  explicit OptimizingCompiler(CompilerDriver* driver);
  ~OptimizingCompiler() OVERRIDE;

  bool CanCompileMethod(uint32_t method_idx, const DexFile& dex_file) const OVERRIDE;

  CompiledMethod* Compile(const DexFile::CodeItem* code_item,
                          uint32_t access_flags,
                          InvokeType invoke_type,
                          uint16_t class_def_idx,
                          uint32_t method_idx,
                          Handle<mirror::ClassLoader> class_loader,
                          const DexFile& dex_file,
                          Handle<mirror::DexCache> dex_cache) const OVERRIDE;

  CompiledMethod* JniCompile(uint32_t access_flags,
                             uint32_t method_idx,
                             const DexFile& dex_file,
                             JniOptimizationFlags optimization_flags) const OVERRIDE {
    return ArtQuickJniCompileMethod(GetCompilerDriver(),
                                    access_flags,
                                    method_idx,
                                    dex_file,
                                    optimization_flags);
  }

  uintptr_t GetEntryPointOf(ArtMethod* method) const OVERRIDE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return reinterpret_cast<uintptr_t>(method->GetEntryPointFromQuickCompiledCodePtrSize(
        InstructionSetPointerSize(GetCompilerDriver()->GetInstructionSet())));
  }

  void Init() OVERRIDE;

  void UnInit() const OVERRIDE;

  void MaybeRecordStat(MethodCompilationStat compilation_stat) const {
    if (compilation_stats_.get() != nullptr) {
      compilation_stats_->RecordStat(compilation_stat);
    }
  }

  bool JitCompile(Thread* self, jit::JitCodeCache* code_cache, ArtMethod* method, bool osr)
      OVERRIDE
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  void RunOptimizations(HGraph* graph,
                        CodeGenerator* codegen,
                        CompilerDriver* driver,
                        const DexCompilationUnit& dex_compilation_unit,
                        PassObserver* pass_observer,
                        VariableSizedHandleScope* handles) const;

  void RunOptimizations(HOptimization* optimizations[],
                        size_t length,
                        PassObserver* pass_observer) const;

 private:
  // Create a 'CompiledMethod' for an optimized graph.
  CompiledMethod* Emit(ArenaAllocator* arena,
                       CodeVectorAllocator* code_allocator,
                       CodeGenerator* codegen,
                       CompilerDriver* driver,
                       const DexFile::CodeItem* item) const;

  // Try compiling a method and return the code generator used for
  // compiling it.
  // This method:
  // 1) Builds the graph. Returns null if it failed to build it.
  // 2) Transforms the graph to SSA. Returns null if it failed.
  // 3) Runs optimizations on the graph, including register allocator.
  // 4) Generates code with the `code_allocator` provided.
  CodeGenerator* TryCompile(ArenaAllocator* arena,
                            CodeVectorAllocator* code_allocator,
                            const DexFile::CodeItem* code_item,
                            uint32_t access_flags,
                            InvokeType invoke_type,
                            uint16_t class_def_idx,
                            uint32_t method_idx,
                            Handle<mirror::ClassLoader> class_loader,
                            const DexFile& dex_file,
                            Handle<mirror::DexCache> dex_cache,
                            ArtMethod* method,
                            bool osr,
                            VariableSizedHandleScope* handles) const;

  void MaybeRunInliner(HGraph* graph,
                       CodeGenerator* codegen,
                       CompilerDriver* driver,
                       const DexCompilationUnit& dex_compilation_unit,
                       PassObserver* pass_observer,
                       VariableSizedHandleScope* handles) const;

  void RunArchOptimizations(InstructionSet instruction_set,
                            HGraph* graph,
                            CodeGenerator* codegen,
                            PassObserver* pass_observer) const;

  std::unique_ptr<OptimizingCompilerStats> compilation_stats_;

  std::unique_ptr<std::ostream> visualizer_output_;

  mutable Mutex dump_mutex_;  // To synchronize visualizer writing.

  DISALLOW_COPY_AND_ASSIGN(OptimizingCompiler);
};

static const int kMaximumCompilationTimeBeforeWarning = 100; /* ms */

OptimizingCompiler::OptimizingCompiler(CompilerDriver* driver)
    : Compiler(driver, kMaximumCompilationTimeBeforeWarning),
      dump_mutex_("Visualizer dump lock") {}

void OptimizingCompiler::Init() {
  // Enable C1visualizer output. Must be done in Init() because the compiler
  // driver is not fully initialized when passed to the compiler's constructor.
  CompilerDriver* driver = GetCompilerDriver();
  const std::string cfg_file_name = driver->GetCompilerOptions().GetDumpCfgFileName();
  if (!cfg_file_name.empty()) {
    std::ios_base::openmode cfg_file_mode =
        driver->GetCompilerOptions().GetDumpCfgAppend() ? std::ofstream::app : std::ofstream::out;
    visualizer_output_.reset(new std::ofstream(cfg_file_name, cfg_file_mode));
  }
  if (driver->GetDumpStats()) {
    compilation_stats_.reset(new OptimizingCompilerStats());
  }
}

void OptimizingCompiler::UnInit() const {
}

OptimizingCompiler::~OptimizingCompiler() {
  if (compilation_stats_.get() != nullptr) {
    compilation_stats_->Log();
  }
}

bool OptimizingCompiler::CanCompileMethod(uint32_t method_idx ATTRIBUTE_UNUSED,
                                          const DexFile& dex_file ATTRIBUTE_UNUSED) const {
  return true;
}

static bool IsInstructionSetSupported(InstructionSet instruction_set) {
  return (instruction_set == kArm && !kArm32QuickCodeUseSoftFloat)
      || instruction_set == kArm64
      || (instruction_set == kThumb2 && !kArm32QuickCodeUseSoftFloat)
      || instruction_set == kMips
      || instruction_set == kMips64
      || instruction_set == kX86
      || instruction_set == kX86_64;
}

// Strip pass name suffix to get optimization name.
static std::string ConvertPassNameToOptimizationName(const std::string& pass_name) {
  size_t pos = pass_name.find(kPassNameSeparator);
  return pos == std::string::npos ? pass_name : pass_name.substr(0, pos);
}

static HOptimization* BuildOptimization(
    const std::string& pass_name,
    ArenaAllocator* arena,
    HGraph* graph,
    OptimizingCompilerStats* stats,
    CodeGenerator* codegen,
    CompilerDriver* driver,
    const DexCompilationUnit& dex_compilation_unit,
    VariableSizedHandleScope* handles,
    SideEffectsAnalysis* most_recent_side_effects,
    HInductionVarAnalysis* most_recent_induction) {
  std::string opt_name = ConvertPassNameToOptimizationName(pass_name);
  if (opt_name == BoundsCheckElimination::kBoundsCheckEliminationPassName) {
    CHECK(most_recent_side_effects != nullptr && most_recent_induction != nullptr);
    return new (arena) BoundsCheckElimination(graph,
                                              *most_recent_side_effects,
                                              most_recent_induction);
  } else if (opt_name == GVNOptimization::kGlobalValueNumberingPassName) {
    CHECK(most_recent_side_effects != nullptr);
    return new (arena) GVNOptimization(graph, *most_recent_side_effects, pass_name.c_str());
  } else if (opt_name == HConstantFolding::kConstantFoldingPassName) {
    return new (arena) HConstantFolding(graph, pass_name.c_str());
  } else if (opt_name == HDeadCodeElimination::kDeadCodeEliminationPassName) {
    return new (arena) HDeadCodeElimination(graph, stats, pass_name.c_str());
  } else if (opt_name == HInliner::kInlinerPassName) {
    size_t number_of_dex_registers = dex_compilation_unit.GetCodeItem()->registers_size_;
    return new (arena) HInliner(graph,                   // outer_graph
                                graph,                   // outermost_graph
                                codegen,
                                dex_compilation_unit,    // outer_compilation_unit
                                dex_compilation_unit,    // outermost_compilation_unit
                                driver,
                                handles,
                                stats,
                                number_of_dex_registers,
                                /* total_number_of_instructions */ 0,
                                /* parent */ nullptr);
  } else if (opt_name == HSharpening::kSharpeningPassName) {
    return new (arena) HSharpening(graph, codegen, dex_compilation_unit, driver, handles);
  } else if (opt_name == HSelectGenerator::kSelectGeneratorPassName) {
    return new (arena) HSelectGenerator(graph, stats);
  } else if (opt_name == HInductionVarAnalysis::kInductionPassName) {
    return new (arena) HInductionVarAnalysis(graph);
  } else if (opt_name == InstructionSimplifier::kInstructionSimplifierPassName) {
    return new (arena) InstructionSimplifier(graph, codegen, stats, pass_name.c_str());
  } else if (opt_name == IntrinsicsRecognizer::kIntrinsicsRecognizerPassName) {
    return new (arena) IntrinsicsRecognizer(graph, stats);
  } else if (opt_name == LICM::kLoopInvariantCodeMotionPassName) {
    CHECK(most_recent_side_effects != nullptr);
    return new (arena) LICM(graph, *most_recent_side_effects, stats);
  } else if (opt_name == LoadStoreElimination::kLoadStoreEliminationPassName) {
    CHECK(most_recent_side_effects != nullptr);
    return new (arena) LoadStoreElimination(graph, *most_recent_side_effects);
  } else if (opt_name == SideEffectsAnalysis::kSideEffectsAnalysisPassName) {
    return new (arena) SideEffectsAnalysis(graph);
  } else if (opt_name == HLoopOptimization::kLoopOptimizationPassName) {
    return new (arena) HLoopOptimization(graph, driver, most_recent_induction);
  } else if (opt_name == CHAGuardOptimization::kCHAGuardOptimizationPassName) {
    return new (arena) CHAGuardOptimization(graph);
  } else if (opt_name == CodeSinking::kCodeSinkingPassName) {
    return new (arena) CodeSinking(graph, stats);
#ifdef ART_ENABLE_CODEGEN_arm
  } else if (opt_name == arm::DexCacheArrayFixups::kDexCacheArrayFixupsArmPassName) {
    return new (arena) arm::DexCacheArrayFixups(graph, codegen, stats);
  } else if (opt_name == arm::InstructionSimplifierArm::kInstructionSimplifierArmPassName) {
    return new (arena) arm::InstructionSimplifierArm(graph, stats);
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
  } else if (opt_name == arm64::InstructionSimplifierArm64::kInstructionSimplifierArm64PassName) {
    return new (arena) arm64::InstructionSimplifierArm64(graph, stats);
#endif
#ifdef ART_ENABLE_CODEGEN_mips
  } else if (opt_name == mips::DexCacheArrayFixups::kDexCacheArrayFixupsMipsPassName) {
    return new (arena) mips::DexCacheArrayFixups(graph, codegen, stats);
  } else if (opt_name == mips::PcRelativeFixups::kPcRelativeFixupsMipsPassName) {
    return new (arena) mips::PcRelativeFixups(graph, codegen, stats);
#endif
#ifdef ART_ENABLE_CODEGEN_x86
  } else if (opt_name == x86::PcRelativeFixups::kPcRelativeFixupsX86PassName) {
    return new (arena) x86::PcRelativeFixups(graph, codegen, stats);
  } else if (opt_name == x86::X86MemoryOperandGeneration::kX86MemoryOperandGenerationPassName) {
    return new (arena) x86::X86MemoryOperandGeneration(graph, codegen, stats);
#endif
  }
  return nullptr;
}

static ArenaVector<HOptimization*> BuildOptimizations(
    const std::vector<std::string>& pass_names,
    ArenaAllocator* arena,
    HGraph* graph,
    OptimizingCompilerStats* stats,
    CodeGenerator* codegen,
    CompilerDriver* driver,
    const DexCompilationUnit& dex_compilation_unit,
    VariableSizedHandleScope* handles) {
  // Few HOptimizations constructors require SideEffectsAnalysis or HInductionVarAnalysis
  // instances. This method assumes that each of them expects the nearest instance preceeding it
  // in the pass name list.
  SideEffectsAnalysis* most_recent_side_effects = nullptr;
  HInductionVarAnalysis* most_recent_induction = nullptr;
  ArenaVector<HOptimization*> ret(arena->Adapter());
  for (const std::string& pass_name : pass_names) {
    HOptimization* opt = BuildOptimization(
        pass_name,
        arena,
        graph,
        stats,
        codegen,
        driver,
        dex_compilation_unit,
        handles,
        most_recent_side_effects,
        most_recent_induction);
    CHECK(opt != nullptr) << "Couldn't build optimization: \"" << pass_name << "\"";
    ret.push_back(opt);

    std::string opt_name = ConvertPassNameToOptimizationName(pass_name);
    if (opt_name == SideEffectsAnalysis::kSideEffectsAnalysisPassName) {
      most_recent_side_effects = down_cast<SideEffectsAnalysis*>(opt);
    } else if (opt_name == HInductionVarAnalysis::kInductionPassName) {
      most_recent_induction = down_cast<HInductionVarAnalysis*>(opt);
    }
  }
  return ret;
}

void OptimizingCompiler::RunOptimizations(HOptimization* optimizations[],
                                          size_t length,
                                          PassObserver* pass_observer) const {
  for (size_t i = 0; i < length; ++i) {
    PassScope scope(optimizations[i]->GetPassName(), pass_observer);
    optimizations[i]->Run();
  }
}

void OptimizingCompiler::MaybeRunInliner(HGraph* graph,
                                         CodeGenerator* codegen,
                                         CompilerDriver* driver,
                                         const DexCompilationUnit& dex_compilation_unit,
                                         PassObserver* pass_observer,
                                         VariableSizedHandleScope* handles) const {
  OptimizingCompilerStats* stats = compilation_stats_.get();
  const CompilerOptions& compiler_options = driver->GetCompilerOptions();
  bool should_inline = (compiler_options.GetInlineMaxCodeUnits() > 0);
  if (!should_inline) {
    return;
  }
  size_t number_of_dex_registers = dex_compilation_unit.GetCodeItem()->registers_size_;
  HInliner* inliner = new (graph->GetArena()) HInliner(
      graph,                   // outer_graph
      graph,                   // outermost_graph
      codegen,
      dex_compilation_unit,    // outer_compilation_unit
      dex_compilation_unit,    // outermost_compilation_unit
      driver,
      handles,
      stats,
      number_of_dex_registers,
      /* total_number_of_instructions */ 0,
      /* parent */ nullptr);
  HOptimization* optimizations[] = { inliner };

  RunOptimizations(optimizations, arraysize(optimizations), pass_observer);
}

void OptimizingCompiler::RunArchOptimizations(InstructionSet instruction_set,
                                              HGraph* graph,
                                              CodeGenerator* codegen,
                                              PassObserver* pass_observer) const {
  UNUSED(codegen);  // To avoid compilation error when compiling for svelte
  OptimizingCompilerStats* stats = compilation_stats_.get();
  ArenaAllocator* arena = graph->GetArena();
  switch (instruction_set) {
#if defined(ART_ENABLE_CODEGEN_arm)
    case kThumb2:
    case kArm: {
      arm::DexCacheArrayFixups* fixups =
          new (arena) arm::DexCacheArrayFixups(graph, codegen, stats);
      arm::InstructionSimplifierArm* simplifier =
          new (arena) arm::InstructionSimplifierArm(graph, stats);
      SideEffectsAnalysis* side_effects = new (arena) SideEffectsAnalysis(graph);
      GVNOptimization* gvn = new (arena) GVNOptimization(graph, *side_effects, "GVN$after_arch");
      HInstructionScheduling* scheduling =
          new (arena) HInstructionScheduling(graph, instruction_set, codegen);
      HOptimization* arm_optimizations[] = {
        simplifier,
        side_effects,
        gvn,
        fixups,
        scheduling,
      };
      RunOptimizations(arm_optimizations, arraysize(arm_optimizations), pass_observer);
      break;
    }
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
    case kArm64: {
      arm64::InstructionSimplifierArm64* simplifier =
          new (arena) arm64::InstructionSimplifierArm64(graph, stats);
      SideEffectsAnalysis* side_effects = new (arena) SideEffectsAnalysis(graph);
      GVNOptimization* gvn = new (arena) GVNOptimization(graph, *side_effects, "GVN$after_arch");
      HInstructionScheduling* scheduling =
          new (arena) HInstructionScheduling(graph, instruction_set);
      HOptimization* arm64_optimizations[] = {
        simplifier,
        side_effects,
        gvn,
        scheduling,
      };
      RunOptimizations(arm64_optimizations, arraysize(arm64_optimizations), pass_observer);
      break;
    }
#endif
#ifdef ART_ENABLE_CODEGEN_mips
    case kMips: {
      mips::PcRelativeFixups* pc_relative_fixups =
          new (arena) mips::PcRelativeFixups(graph, codegen, stats);
      mips::DexCacheArrayFixups* dex_cache_array_fixups =
          new (arena) mips::DexCacheArrayFixups(graph, codegen, stats);
      HOptimization* mips_optimizations[] = {
          pc_relative_fixups,
          dex_cache_array_fixups
      };
      RunOptimizations(mips_optimizations, arraysize(mips_optimizations), pass_observer);
      break;
    }
#endif
#ifdef ART_ENABLE_CODEGEN_x86
    case kX86: {
      x86::PcRelativeFixups* pc_relative_fixups =
          new (arena) x86::PcRelativeFixups(graph, codegen, stats);
      x86::X86MemoryOperandGeneration* memory_gen =
          new (arena) x86::X86MemoryOperandGeneration(graph, codegen, stats);
      HOptimization* x86_optimizations[] = {
          pc_relative_fixups,
          memory_gen
      };
      RunOptimizations(x86_optimizations, arraysize(x86_optimizations), pass_observer);
      break;
    }
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
    case kX86_64: {
      x86::X86MemoryOperandGeneration* memory_gen =
          new (arena) x86::X86MemoryOperandGeneration(graph, codegen, stats);
      HOptimization* x86_64_optimizations[] = {
          memory_gen
      };
      RunOptimizations(x86_64_optimizations, arraysize(x86_64_optimizations), pass_observer);
      break;
    }
#endif
    default:
      break;
  }
}

NO_INLINE  // Avoid increasing caller's frame size by large stack-allocated objects.
static void AllocateRegisters(HGraph* graph,
                              CodeGenerator* codegen,
                              PassObserver* pass_observer,
                              RegisterAllocator::Strategy strategy) {
  {
    PassScope scope(PrepareForRegisterAllocation::kPrepareForRegisterAllocationPassName,
                    pass_observer);
    PrepareForRegisterAllocation(graph).Run();
  }
  SsaLivenessAnalysis liveness(graph, codegen);
  {
    PassScope scope(SsaLivenessAnalysis::kLivenessPassName, pass_observer);
    liveness.Analyze();
  }
  {
    PassScope scope(RegisterAllocator::kRegisterAllocatorPassName, pass_observer);
    RegisterAllocator::Create(graph->GetArena(), codegen, liveness, strategy)->AllocateRegisters();
  }
}

void OptimizingCompiler::RunOptimizations(HGraph* graph,
                                          CodeGenerator* codegen,
                                          CompilerDriver* driver,
                                          const DexCompilationUnit& dex_compilation_unit,
                                          PassObserver* pass_observer,
                                          VariableSizedHandleScope* handles) const {
  OptimizingCompilerStats* stats = compilation_stats_.get();
  ArenaAllocator* arena = graph->GetArena();
  if (driver->GetCompilerOptions().GetPassesToRun() != nullptr) {
    ArenaVector<HOptimization*> optimizations = BuildOptimizations(
        *driver->GetCompilerOptions().GetPassesToRun(),
        arena,
        graph,
        stats,
        codegen,
        driver,
        dex_compilation_unit,
        handles);
    RunOptimizations(&optimizations[0], optimizations.size(), pass_observer);
    return;
  }

  HDeadCodeElimination* dce1 = new (arena) HDeadCodeElimination(
      graph, stats, "dead_code_elimination$initial");
  HDeadCodeElimination* dce2 = new (arena) HDeadCodeElimination(
      graph, stats, "dead_code_elimination$after_inlining");
  HDeadCodeElimination* dce3 = new (arena) HDeadCodeElimination(
      graph, stats, "dead_code_elimination$final");
  HConstantFolding* fold1 = new (arena) HConstantFolding(graph, "constant_folding");
  InstructionSimplifier* simplify1 = new (arena) InstructionSimplifier(graph, codegen, stats);
  HSelectGenerator* select_generator = new (arena) HSelectGenerator(graph, stats);
  HConstantFolding* fold2 = new (arena) HConstantFolding(
      graph, "constant_folding$after_inlining");
  HConstantFolding* fold3 = new (arena) HConstantFolding(graph, "constant_folding$after_bce");
  SideEffectsAnalysis* side_effects1 = new (arena) SideEffectsAnalysis(
      graph, "side_effects$before_gvn");
  SideEffectsAnalysis* side_effects2 = new (arena) SideEffectsAnalysis(
      graph, "side_effects$before_lse");
  GVNOptimization* gvn = new (arena) GVNOptimization(graph, *side_effects1);
  LICM* licm = new (arena) LICM(graph, *side_effects1, stats);
  HInductionVarAnalysis* induction = new (arena) HInductionVarAnalysis(graph);
  BoundsCheckElimination* bce = new (arena) BoundsCheckElimination(graph, *side_effects1, induction);
  HLoopOptimization* loop = new (arena) HLoopOptimization(graph, driver, induction);
  LoadStoreElimination* lse = new (arena) LoadStoreElimination(graph, *side_effects2);
  HSharpening* sharpening = new (arena) HSharpening(
      graph, codegen, dex_compilation_unit, driver, handles);
  InstructionSimplifier* simplify2 = new (arena) InstructionSimplifier(
      graph, codegen, stats, "instruction_simplifier$after_inlining");
  InstructionSimplifier* simplify3 = new (arena) InstructionSimplifier(
      graph, codegen, stats, "instruction_simplifier$after_bce");
  InstructionSimplifier* simplify4 = new (arena) InstructionSimplifier(
      graph, codegen, stats, "instruction_simplifier$before_codegen");
  IntrinsicsRecognizer* intrinsics = new (arena) IntrinsicsRecognizer(graph, stats);
  CHAGuardOptimization* cha_guard = new (arena) CHAGuardOptimization(graph);
  CodeSinking* code_sinking = new (arena) CodeSinking(graph, stats);

  HOptimization* optimizations1[] = {
    intrinsics,
    sharpening,
    fold1,
    simplify1,
    dce1,
  };
  RunOptimizations(optimizations1, arraysize(optimizations1), pass_observer);

  MaybeRunInliner(graph, codegen, driver, dex_compilation_unit, pass_observer, handles);

  HOptimization* optimizations2[] = {
    // SelectGenerator depends on the InstructionSimplifier removing
    // redundant suspend checks to recognize empty blocks.
    select_generator,
    fold2,  // TODO: if we don't inline we can also skip fold2.
    simplify2,
    dce2,
    side_effects1,
    gvn,
    licm,
    induction,
    bce,
    loop,
    fold3,  // evaluates code generated by dynamic bce
    simplify3,
    side_effects2,
    lse,
    cha_guard,
    dce3,
    code_sinking,
    // The codegen has a few assumptions that only the instruction simplifier
    // can satisfy. For example, the code generator does not expect to see a
    // HTypeConversion from a type to the same type.
    simplify4,
  };
  RunOptimizations(optimizations2, arraysize(optimizations2), pass_observer);

  RunArchOptimizations(driver->GetInstructionSet(), graph, codegen, pass_observer);
}

static ArenaVector<LinkerPatch> EmitAndSortLinkerPatches(CodeGenerator* codegen) {
  ArenaVector<LinkerPatch> linker_patches(codegen->GetGraph()->GetArena()->Adapter());
  codegen->EmitLinkerPatches(&linker_patches);

  // Sort patches by literal offset. Required for .oat_patches encoding.
  std::sort(linker_patches.begin(), linker_patches.end(),
            [](const LinkerPatch& lhs, const LinkerPatch& rhs) {
    return lhs.LiteralOffset() < rhs.LiteralOffset();
  });

  return linker_patches;
}

CompiledMethod* OptimizingCompiler::Emit(ArenaAllocator* arena,
                                         CodeVectorAllocator* code_allocator,
                                         CodeGenerator* codegen,
                                         CompilerDriver* compiler_driver,
                                         const DexFile::CodeItem* code_item) const {
  ArenaVector<LinkerPatch> linker_patches = EmitAndSortLinkerPatches(codegen);
  ArenaVector<uint8_t> stack_map(arena->Adapter(kArenaAllocStackMaps));
  ArenaVector<uint8_t> method_info(arena->Adapter(kArenaAllocStackMaps));
  size_t stack_map_size = 0;
  size_t method_info_size = 0;
  codegen->ComputeStackMapAndMethodInfoSize(&stack_map_size, &method_info_size);
  stack_map.resize(stack_map_size);
  method_info.resize(method_info_size);
  codegen->BuildStackMaps(MemoryRegion(stack_map.data(), stack_map.size()),
                          MemoryRegion(method_info.data(), method_info.size()),
                          *code_item);

  CompiledMethod* compiled_method = CompiledMethod::SwapAllocCompiledMethod(
      compiler_driver,
      codegen->GetInstructionSet(),
      ArrayRef<const uint8_t>(code_allocator->GetMemory()),
      // Follow Quick's behavior and set the frame size to zero if it is
      // considered "empty" (see the definition of
      // art::CodeGenerator::HasEmptyFrame).
      codegen->HasEmptyFrame() ? 0 : codegen->GetFrameSize(),
      codegen->GetCoreSpillMask(),
      codegen->GetFpuSpillMask(),
      ArrayRef<const uint8_t>(method_info),
      ArrayRef<const uint8_t>(stack_map),
      ArrayRef<const uint8_t>(*codegen->GetAssembler()->cfi().data()),
      ArrayRef<const LinkerPatch>(linker_patches));

  return compiled_method;
}

CodeGenerator* OptimizingCompiler::TryCompile(ArenaAllocator* arena,
                                              CodeVectorAllocator* code_allocator,
                                              const DexFile::CodeItem* code_item,
                                              uint32_t access_flags,
                                              InvokeType invoke_type,
                                              uint16_t class_def_idx,
                                              uint32_t method_idx,
                                              Handle<mirror::ClassLoader> class_loader,
                                              const DexFile& dex_file,
                                              Handle<mirror::DexCache> dex_cache,
                                              ArtMethod* method,
                                              bool osr,
                                              VariableSizedHandleScope* handles) const {
  MaybeRecordStat(MethodCompilationStat::kAttemptCompilation);
  CompilerDriver* compiler_driver = GetCompilerDriver();
  InstructionSet instruction_set = compiler_driver->GetInstructionSet();

  // Always use the Thumb-2 assembler: some runtime functionality
  // (like implicit stack overflow checks) assume Thumb-2.
  DCHECK_NE(instruction_set, kArm);

  // Do not attempt to compile on architectures we do not support.
  if (!IsInstructionSetSupported(instruction_set)) {
    MaybeRecordStat(MethodCompilationStat::kNotCompiledUnsupportedIsa);
    return nullptr;
  }

  if (Compiler::IsPathologicalCase(*code_item, method_idx, dex_file)) {
    MaybeRecordStat(MethodCompilationStat::kNotCompiledPathological);
    return nullptr;
  }

  // Implementation of the space filter: do not compile a code item whose size in
  // code units is bigger than 128.
  static constexpr size_t kSpaceFilterOptimizingThreshold = 128;
  const CompilerOptions& compiler_options = compiler_driver->GetCompilerOptions();
  if ((compiler_options.GetCompilerFilter() == CompilerFilter::kSpace)
      && (code_item->insns_size_in_code_units_ > kSpaceFilterOptimizingThreshold)) {
    MaybeRecordStat(MethodCompilationStat::kNotCompiledSpaceFilter);
    return nullptr;
  }

  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  DexCompilationUnit dex_compilation_unit(
      class_loader,
      class_linker,
      dex_file,
      code_item,
      class_def_idx,
      method_idx,
      access_flags,
      /* verified_method */ nullptr,
      dex_cache);

  HGraph* graph = new (arena) HGraph(
      arena,
      dex_file,
      method_idx,
      compiler_driver->GetInstructionSet(),
      kInvalidInvokeType,
      compiler_driver->GetCompilerOptions().GetDebuggable(),
      osr);

  const uint8_t* interpreter_metadata = nullptr;
  if (method == nullptr) {
    ScopedObjectAccess soa(Thread::Current());
    method = compiler_driver->ResolveMethod(
        soa, dex_cache, class_loader, &dex_compilation_unit, method_idx, invoke_type);
  }
  // For AOT compilation, we may not get a method, for example if its class is erroneous.
  // JIT should always have a method.
  DCHECK(Runtime::Current()->IsAotCompiler() || method != nullptr);
  if (method != nullptr) {
    graph->SetArtMethod(method);
    ScopedObjectAccess soa(Thread::Current());
    interpreter_metadata = method->GetQuickenedInfo(class_linker->GetImagePointerSize());
  }

  std::unique_ptr<CodeGenerator> codegen(
      CodeGenerator::Create(graph,
                            instruction_set,
                            *compiler_driver->GetInstructionSetFeatures(),
                            compiler_driver->GetCompilerOptions(),
                            compilation_stats_.get()));
  if (codegen.get() == nullptr) {
    MaybeRecordStat(MethodCompilationStat::kNotCompiledNoCodegen);
    return nullptr;
  }
  codegen->GetAssembler()->cfi().SetEnabled(
      compiler_driver->GetCompilerOptions().GenerateAnyDebugInfo());

  PassObserver pass_observer(graph,
                             codegen.get(),
                             visualizer_output_.get(),
                             compiler_driver,
                             dump_mutex_);

  {
    VLOG(compiler) << "Building " << pass_observer.GetMethodName();
    PassScope scope(HGraphBuilder::kBuilderPassName, &pass_observer);
    HGraphBuilder builder(graph,
                          &dex_compilation_unit,
                          &dex_compilation_unit,
                          &dex_file,
                          *code_item,
                          compiler_driver,
                          codegen.get(),
                          compilation_stats_.get(),
                          interpreter_metadata,
                          dex_cache,
                          handles);
    GraphAnalysisResult result = builder.BuildGraph();
    if (result != kAnalysisSuccess) {
      switch (result) {
        case kAnalysisSkipped:
          MaybeRecordStat(MethodCompilationStat::kNotCompiledSkipped);
          break;
        case kAnalysisInvalidBytecode:
          MaybeRecordStat(MethodCompilationStat::kNotCompiledInvalidBytecode);
          break;
        case kAnalysisFailThrowCatchLoop:
          MaybeRecordStat(MethodCompilationStat::kNotCompiledThrowCatchLoop);
          break;
        case kAnalysisFailAmbiguousArrayOp:
          MaybeRecordStat(MethodCompilationStat::kNotCompiledAmbiguousArrayOp);
          break;
        case kAnalysisSuccess:
          UNREACHABLE();
      }
      pass_observer.SetGraphInBadState();
      return nullptr;
    }
  }

  RunOptimizations(graph,
                   codegen.get(),
                   compiler_driver,
                   dex_compilation_unit,
                   &pass_observer,
                   handles);

  RegisterAllocator::Strategy regalloc_strategy =
    compiler_options.GetRegisterAllocationStrategy();
  AllocateRegisters(graph, codegen.get(), &pass_observer, regalloc_strategy);

  codegen->Compile(code_allocator);
  pass_observer.DumpDisassembly();

  return codegen.release();
}

CompiledMethod* OptimizingCompiler::Compile(const DexFile::CodeItem* code_item,
                                            uint32_t access_flags,
                                            InvokeType invoke_type,
                                            uint16_t class_def_idx,
                                            uint32_t method_idx,
                                            Handle<mirror::ClassLoader> jclass_loader,
                                            const DexFile& dex_file,
                                            Handle<mirror::DexCache> dex_cache) const {
  CompilerDriver* compiler_driver = GetCompilerDriver();
  CompiledMethod* method = nullptr;
  DCHECK(Runtime::Current()->IsAotCompiler());
  const VerifiedMethod* verified_method = compiler_driver->GetVerifiedMethod(&dex_file, method_idx);
  DCHECK(!verified_method->HasRuntimeThrow());
  if (compiler_driver->IsMethodVerifiedWithoutFailures(method_idx, class_def_idx, dex_file)
      || verifier::CanCompilerHandleVerificationFailure(
            verified_method->GetEncounteredVerificationFailures())) {
    ArenaAllocator arena(Runtime::Current()->GetArenaPool());
    CodeVectorAllocator code_allocator(&arena);
    std::unique_ptr<CodeGenerator> codegen;
    {
      ScopedObjectAccess soa(Thread::Current());
      VariableSizedHandleScope handles(soa.Self());
      // Go to native so that we don't block GC during compilation.
      ScopedThreadSuspension sts(soa.Self(), kNative);
      codegen.reset(
          TryCompile(&arena,
                     &code_allocator,
                     code_item,
                     access_flags,
                     invoke_type,
                     class_def_idx,
                     method_idx,
                     jclass_loader,
                     dex_file,
                     dex_cache,
                     nullptr,
                     /* osr */ false,
                     &handles));
    }
    if (codegen.get() != nullptr) {
      MaybeRecordStat(MethodCompilationStat::kCompiled);
      method = Emit(&arena, &code_allocator, codegen.get(), compiler_driver, code_item);

      if (kArenaAllocatorCountAllocations) {
        if (arena.BytesAllocated() > kArenaAllocatorMemoryReportThreshold) {
          MemStats mem_stats(arena.GetMemStats());
          LOG(INFO) << dex_file.PrettyMethod(method_idx) << " " << Dumpable<MemStats>(mem_stats);
        }
      }
    }
  } else {
    if (compiler_driver->GetCompilerOptions().VerifyAtRuntime()) {
      MaybeRecordStat(MethodCompilationStat::kNotCompiledVerifyAtRuntime);
    } else {
      MaybeRecordStat(MethodCompilationStat::kNotCompiledVerificationError);
    }
  }

  if (kIsDebugBuild &&
      IsCompilingWithCoreImage() &&
      IsInstructionSetSupported(compiler_driver->GetInstructionSet())) {
    // For testing purposes, we put a special marker on method names
    // that should be compiled with this compiler (when the
    // instruction set is supported). This makes sure we're not
    // regressing.
    std::string method_name = dex_file.PrettyMethod(method_idx);
    bool shouldCompile = method_name.find("$opt$") != std::string::npos;
    DCHECK((method != nullptr) || !shouldCompile) << "Didn't compile " << method_name;
  }

  return method;
}

Compiler* CreateOptimizingCompiler(CompilerDriver* driver) {
  return new OptimizingCompiler(driver);
}

bool IsCompilingWithCoreImage() {
  const std::string& image = Runtime::Current()->GetImageLocation();
  // TODO: This is under-approximating...
  if (android::base::EndsWith(image, "core.art") ||
      android::base::EndsWith(image, "core-optimizing.art")) {
    return true;
  }
  return false;
}

bool EncodeArtMethodInInlineInfo(ArtMethod* method ATTRIBUTE_UNUSED) {
  // Note: the runtime is null only for unit testing.
  return Runtime::Current() == nullptr || !Runtime::Current()->IsAotCompiler();
}

bool CanEncodeInlinedMethodInStackMap(const DexFile& caller_dex_file, ArtMethod* callee) {
  if (!Runtime::Current()->IsAotCompiler()) {
    // JIT can always encode methods in stack maps.
    return true;
  }
  if (IsSameDexFile(caller_dex_file, *callee->GetDexFile())) {
    return true;
  }
  // TODO(ngeoffray): Support more AOT cases for inlining:
  // - methods in multidex
  // - methods in boot image for on-device non-PIC compilation.
  return false;
}

bool OptimizingCompiler::JitCompile(Thread* self,
                                    jit::JitCodeCache* code_cache,
                                    ArtMethod* method,
                                    bool osr) {
  StackHandleScope<3> hs(self);
  Handle<mirror::ClassLoader> class_loader(hs.NewHandle(
      method->GetDeclaringClass()->GetClassLoader()));
  Handle<mirror::DexCache> dex_cache(hs.NewHandle(method->GetDexCache()));
  DCHECK(method->IsCompilable());

  const DexFile* dex_file = method->GetDexFile();
  const uint16_t class_def_idx = method->GetClassDefIndex();
  const DexFile::CodeItem* code_item = dex_file->GetCodeItem(method->GetCodeItemOffset());
  const uint32_t method_idx = method->GetDexMethodIndex();
  const uint32_t access_flags = method->GetAccessFlags();
  const InvokeType invoke_type = method->GetInvokeType();

  ArenaAllocator arena(Runtime::Current()->GetJitArenaPool());
  CodeVectorAllocator code_allocator(&arena);
  VariableSizedHandleScope handles(self);

  std::unique_ptr<CodeGenerator> codegen;
  {
    // Go to native so that we don't block GC during compilation.
    ScopedThreadSuspension sts(self, kNative);
    codegen.reset(
        TryCompile(&arena,
                   &code_allocator,
                   code_item,
                   access_flags,
                   invoke_type,
                   class_def_idx,
                   method_idx,
                   class_loader,
                   *dex_file,
                   dex_cache,
                   method,
                   osr,
                   &handles));
    if (codegen.get() == nullptr) {
      return false;
    }

    if (kArenaAllocatorCountAllocations) {
      if (arena.BytesAllocated() > kArenaAllocatorMemoryReportThreshold) {
        MemStats mem_stats(arena.GetMemStats());
        LOG(INFO) << dex_file->PrettyMethod(method_idx) << " " << Dumpable<MemStats>(mem_stats);
      }
    }
  }

  size_t stack_map_size = 0;
  size_t method_info_size = 0;
  codegen->ComputeStackMapAndMethodInfoSize(&stack_map_size, &method_info_size);
  size_t number_of_roots = codegen->GetNumberOfJitRoots();
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  // We allocate an object array to ensure the JIT roots that we will collect in EmitJitRoots
  // will be visible by the GC between EmitLiterals and CommitCode. Once CommitCode is
  // executed, this array is not needed.
  Handle<mirror::ObjectArray<mirror::Object>> roots(
      hs.NewHandle(mirror::ObjectArray<mirror::Object>::Alloc(
          self, class_linker->GetClassRoot(ClassLinker::kObjectArrayClass), number_of_roots)));
  if (roots == nullptr) {
    // Out of memory, just clear the exception to avoid any Java exception uncaught problems.
    DCHECK(self->IsExceptionPending());
    self->ClearException();
    return false;
  }
  uint8_t* stack_map_data = nullptr;
  uint8_t* method_info_data = nullptr;
  uint8_t* roots_data = nullptr;
  uint32_t data_size = code_cache->ReserveData(self,
                                               stack_map_size,
                                               method_info_size,
                                               number_of_roots,
                                               method,
                                               &stack_map_data,
                                               &method_info_data,
                                               &roots_data);
  if (stack_map_data == nullptr || roots_data == nullptr) {
    return false;
  }
  MaybeRecordStat(MethodCompilationStat::kCompiled);
  codegen->BuildStackMaps(MemoryRegion(stack_map_data, stack_map_size),
                          MemoryRegion(method_info_data, method_info_size),
                          *code_item);
  codegen->EmitJitRoots(code_allocator.GetData(), roots, roots_data);

  const void* code = code_cache->CommitCode(
      self,
      method,
      stack_map_data,
      method_info_data,
      roots_data,
      codegen->HasEmptyFrame() ? 0 : codegen->GetFrameSize(),
      codegen->GetCoreSpillMask(),
      codegen->GetFpuSpillMask(),
      code_allocator.GetMemory().data(),
      code_allocator.GetSize(),
      data_size,
      osr,
      roots,
      codegen->GetGraph()->HasShouldDeoptimizeFlag(),
      codegen->GetGraph()->GetCHASingleImplementationList());

  if (code == nullptr) {
    code_cache->ClearData(self, stack_map_data, roots_data);
    return false;
  }

  const CompilerOptions& compiler_options = GetCompilerDriver()->GetCompilerOptions();
  if (compiler_options.GetGenerateDebugInfo()) {
    const auto* method_header = reinterpret_cast<const OatQuickMethodHeader*>(code);
    const uintptr_t code_address = reinterpret_cast<uintptr_t>(method_header->GetCode());
    debug::MethodDebugInfo info = debug::MethodDebugInfo();
    info.trampoline_name = nullptr;
    info.dex_file = dex_file;
    info.class_def_index = class_def_idx;
    info.dex_method_index = method_idx;
    info.access_flags = access_flags;
    info.code_item = code_item;
    info.isa = codegen->GetInstructionSet();
    info.deduped = false;
    info.is_native_debuggable = compiler_options.GetNativeDebuggable();
    info.is_optimized = true;
    info.is_code_address_text_relative = false;
    info.code_address = code_address;
    info.code_size = code_allocator.GetSize();
    info.frame_size_in_bytes = method_header->GetFrameSizeInBytes();
    info.code_info = stack_map_size == 0 ? nullptr : stack_map_data;
    info.cfi = ArrayRef<const uint8_t>(*codegen->GetAssembler()->cfi().data());
    std::vector<uint8_t> elf_file = debug::WriteDebugElfFileForMethods(
        GetCompilerDriver()->GetInstructionSet(),
        GetCompilerDriver()->GetInstructionSetFeatures(),
        ArrayRef<const debug::MethodDebugInfo>(&info, 1));
    CreateJITCodeEntryForAddress(code_address, std::move(elf_file));
  }

  Runtime::Current()->GetJit()->AddMemoryUsage(method, arena.BytesUsed());

  return true;
}

}  // namespace art
