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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_

#include "arch/arm64/quick_method_frame_info_arm64.h"
#include "code_generator.h"
#include "common_arm64.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "parallel_move_resolver.h"
#include "string_reference.h"
#include "utils/arm64/assembler_arm64.h"
#include "utils/type_reference.h"

// TODO(VIXL): Make VIXL compile with -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch64/disasm-aarch64.h"
#include "aarch64/macro-assembler-aarch64.h"
#pragma GCC diagnostic pop

namespace art {
namespace arm64 {

class CodeGeneratorARM64;

// Use a local definition to prevent copying mistakes.
static constexpr size_t kArm64WordSize = static_cast<size_t>(kArm64PointerSize);

static const vixl::aarch64::Register kParameterCoreRegisters[] = {
  vixl::aarch64::x1,
  vixl::aarch64::x2,
  vixl::aarch64::x3,
  vixl::aarch64::x4,
  vixl::aarch64::x5,
  vixl::aarch64::x6,
  vixl::aarch64::x7
};
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);
static const vixl::aarch64::FPRegister kParameterFPRegisters[] = {
  vixl::aarch64::d0,
  vixl::aarch64::d1,
  vixl::aarch64::d2,
  vixl::aarch64::d3,
  vixl::aarch64::d4,
  vixl::aarch64::d5,
  vixl::aarch64::d6,
  vixl::aarch64::d7
};
static constexpr size_t kParameterFPRegistersLength = arraysize(kParameterFPRegisters);

// Thread Register
const vixl::aarch64::Register tr = vixl::aarch64::x19;
// Method register on invoke.
static const vixl::aarch64::Register kArtMethodRegister = vixl::aarch64::x0;
const vixl::aarch64::CPURegList vixl_reserved_core_registers(vixl::aarch64::ip0,
                                                             vixl::aarch64::ip1);
const vixl::aarch64::CPURegList vixl_reserved_fp_registers(vixl::aarch64::d31);

const vixl::aarch64::CPURegList runtime_reserved_core_registers(tr, vixl::aarch64::lr);

// Callee-saved registers AAPCS64 (without x19 - Thread Register)
const vixl::aarch64::CPURegList callee_saved_core_registers(vixl::aarch64::CPURegister::kRegister,
                                                            vixl::aarch64::kXRegSize,
                                                            vixl::aarch64::x20.GetCode(),
                                                            vixl::aarch64::x30.GetCode());
const vixl::aarch64::CPURegList callee_saved_fp_registers(vixl::aarch64::CPURegister::kFPRegister,
                                                          vixl::aarch64::kDRegSize,
                                                          vixl::aarch64::d8.GetCode(),
                                                          vixl::aarch64::d15.GetCode());
Location ARM64ReturnLocation(Primitive::Type return_type);

class SlowPathCodeARM64 : public SlowPathCode {
 public:
  explicit SlowPathCodeARM64(HInstruction* instruction)
      : SlowPathCode(instruction), entry_label_(), exit_label_() {}

  vixl::aarch64::Label* GetEntryLabel() { return &entry_label_; }
  vixl::aarch64::Label* GetExitLabel() { return &exit_label_; }

  void SaveLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) OVERRIDE;
  void RestoreLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) OVERRIDE;

 private:
  vixl::aarch64::Label entry_label_;
  vixl::aarch64::Label exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeARM64);
};

class JumpTableARM64 : public DeletableArenaObject<kArenaAllocSwitchTable> {
 public:
  explicit JumpTableARM64(HPackedSwitch* switch_instr)
    : switch_instr_(switch_instr), table_start_() {}

  vixl::aarch64::Label* GetTableStartLabel() { return &table_start_; }

  void EmitTable(CodeGeneratorARM64* codegen);

 private:
  HPackedSwitch* const switch_instr_;
  vixl::aarch64::Label table_start_;

  DISALLOW_COPY_AND_ASSIGN(JumpTableARM64);
};

static const vixl::aarch64::Register kRuntimeParameterCoreRegisters[] =
    { vixl::aarch64::x0,
      vixl::aarch64::x1,
      vixl::aarch64::x2,
      vixl::aarch64::x3,
      vixl::aarch64::x4,
      vixl::aarch64::x5,
      vixl::aarch64::x6,
      vixl::aarch64::x7 };
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);
static const vixl::aarch64::FPRegister kRuntimeParameterFpuRegisters[] =
    { vixl::aarch64::d0,
      vixl::aarch64::d1,
      vixl::aarch64::d2,
      vixl::aarch64::d3,
      vixl::aarch64::d4,
      vixl::aarch64::d5,
      vixl::aarch64::d6,
      vixl::aarch64::d7 };
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);

class InvokeRuntimeCallingConvention : public CallingConvention<vixl::aarch64::Register,
                                                                vixl::aarch64::FPRegister> {
 public:
  static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);

  InvokeRuntimeCallingConvention()
      : CallingConvention(kRuntimeParameterCoreRegisters,
                          kRuntimeParameterCoreRegistersLength,
                          kRuntimeParameterFpuRegisters,
                          kRuntimeParameterFpuRegistersLength,
                          kArm64PointerSize) {}

  Location GetReturnLocation(Primitive::Type return_type);

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConvention);
};

class InvokeDexCallingConvention : public CallingConvention<vixl::aarch64::Register,
                                                            vixl::aarch64::FPRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFPRegisters,
                          kParameterFPRegistersLength,
                          kArm64PointerSize) {}

  Location GetReturnLocation(Primitive::Type return_type) const {
    return ARM64ReturnLocation(return_type);
  }


 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorARM64 : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorARM64() {}
  virtual ~InvokeDexCallingConventionVisitorARM64() {}

  Location GetNextLocation(Primitive::Type type) OVERRIDE;
  Location GetReturnLocation(Primitive::Type return_type) const OVERRIDE {
    return calling_convention.GetReturnLocation(return_type);
  }
  Location GetMethodLocation() const OVERRIDE;

 private:
  InvokeDexCallingConvention calling_convention;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorARM64);
};

class FieldAccessCallingConventionARM64 : public FieldAccessCallingConvention {
 public:
  FieldAccessCallingConventionARM64() {}

  Location GetObjectLocation() const OVERRIDE {
    return helpers::LocationFrom(vixl::aarch64::x1);
  }
  Location GetFieldIndexLocation() const OVERRIDE {
    return helpers::LocationFrom(vixl::aarch64::x0);
  }
  Location GetReturnLocation(Primitive::Type type ATTRIBUTE_UNUSED) const OVERRIDE {
    return helpers::LocationFrom(vixl::aarch64::x0);
  }
  Location GetSetValueLocation(Primitive::Type type, bool is_instance) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? helpers::LocationFrom(vixl::aarch64::x2)
        : (is_instance
            ? helpers::LocationFrom(vixl::aarch64::x2)
            : helpers::LocationFrom(vixl::aarch64::x1));
  }
  Location GetFpuLocation(Primitive::Type type ATTRIBUTE_UNUSED) const OVERRIDE {
    return helpers::LocationFrom(vixl::aarch64::d0);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FieldAccessCallingConventionARM64);
};

class InstructionCodeGeneratorARM64 : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorARM64(HGraph* graph, CodeGeneratorARM64* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super) \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_ARM64(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_SHARED(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

  Arm64Assembler* GetAssembler() const { return assembler_; }
  vixl::aarch64::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->GetVIXLAssembler(); }

 private:
  void GenerateClassInitializationCheck(SlowPathCodeARM64* slow_path,
                                        vixl::aarch64::Register class_reg);
  void GenerateSuspendCheck(HSuspendCheck* instruction, HBasicBlock* successor);
  void HandleBinaryOp(HBinaryOperation* instr);

  void HandleFieldSet(HInstruction* instruction,
                      const FieldInfo& field_info,
                      bool value_can_be_null);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleCondition(HCondition* instruction);

  // Generate a heap reference load using one register `out`:
  //
  //   out <- *(out + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a read barrier and
  // shall be a register in that case; it may be an invalid location
  // otherwise.
  void GenerateReferenceLoadOneRegister(HInstruction* instruction,
                                        Location out,
                                        uint32_t offset,
                                        Location maybe_temp);
  // Generate a heap reference load using two different registers
  // `out` and `obj`:
  //
  //   out <- *(obj + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a Baker's (fast
  // path) read barrier and shall be a register in that case; it may
  // be an invalid location otherwise.
  void GenerateReferenceLoadTwoRegisters(HInstruction* instruction,
                                         Location out,
                                         Location obj,
                                         uint32_t offset,
                                         Location maybe_temp);
  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers (if any).
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               vixl::aarch64::Register obj,
                               uint32_t offset,
                               vixl::aarch64::Label* fixup_label = nullptr);

  // Generate a floating-point comparison.
  void GenerateFcmp(HInstruction* instruction);

  void HandleShift(HBinaryOperation* instr);
  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             vixl::aarch64::Label* true_target,
                             vixl::aarch64::Label* false_target);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemIntegral(HBinaryOperation* instruction);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);

  Arm64Assembler* const assembler_;
  CodeGeneratorARM64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorARM64);
};

class LocationsBuilderARM64 : public HGraphVisitor {
 public:
  LocationsBuilderARM64(HGraph* graph, CodeGeneratorARM64* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super) \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_ARM64(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_SHARED(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

 private:
  void HandleBinaryOp(HBinaryOperation* instr);
  void HandleFieldSet(HInstruction* instruction);
  void HandleFieldGet(HInstruction* instruction);
  void HandleInvoke(HInvoke* instr);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* instr);

  CodeGeneratorARM64* const codegen_;
  InvokeDexCallingConventionVisitorARM64 parameter_visitor_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderARM64);
};

class ParallelMoveResolverARM64 : public ParallelMoveResolverNoSwap {
 public:
  ParallelMoveResolverARM64(ArenaAllocator* allocator, CodeGeneratorARM64* codegen)
      : ParallelMoveResolverNoSwap(allocator), codegen_(codegen), vixl_temps_() {}

 protected:
  void PrepareForEmitNativeCode() OVERRIDE;
  void FinishEmitNativeCode() OVERRIDE;
  Location AllocateScratchLocationFor(Location::Kind kind) OVERRIDE;
  void FreeScratchLocation(Location loc) OVERRIDE;
  void EmitMove(size_t index) OVERRIDE;

 private:
  Arm64Assembler* GetAssembler() const;
  vixl::aarch64::MacroAssembler* GetVIXLAssembler() const {
    return GetAssembler()->GetVIXLAssembler();
  }

  CodeGeneratorARM64* const codegen_;
  vixl::aarch64::UseScratchRegisterScope vixl_temps_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverARM64);
};

class CodeGeneratorARM64 : public CodeGenerator {
 public:
  CodeGeneratorARM64(HGraph* graph,
                     const Arm64InstructionSetFeatures& isa_features,
                     const CompilerOptions& compiler_options,
                     OptimizingCompilerStats* stats = nullptr);
  virtual ~CodeGeneratorARM64() {}

  void GenerateFrameEntry() OVERRIDE;
  void GenerateFrameExit() OVERRIDE;

  vixl::aarch64::CPURegList GetFramePreservedCoreRegisters() const;
  vixl::aarch64::CPURegList GetFramePreservedFPRegisters() const;

  void Bind(HBasicBlock* block) OVERRIDE;

  vixl::aarch64::Label* GetLabelOf(HBasicBlock* block) {
    block = FirstNonEmptyBlock(block);
    return &(block_labels_[block->GetBlockId()]);
  }

  size_t GetWordSize() const OVERRIDE {
    return kArm64WordSize;
  }

  size_t GetFloatingPointSpillSlotSize() const OVERRIDE {
    // Allocated in D registers, which are word sized.
    return kArm64WordSize;
  }

  uintptr_t GetAddressOf(HBasicBlock* block) OVERRIDE {
    vixl::aarch64::Label* block_entry_label = GetLabelOf(block);
    DCHECK(block_entry_label->IsBound());
    return block_entry_label->GetLocation();
  }

  HGraphVisitor* GetLocationBuilder() OVERRIDE { return &location_builder_; }
  HGraphVisitor* GetInstructionVisitor() OVERRIDE { return &instruction_visitor_; }
  Arm64Assembler* GetAssembler() OVERRIDE { return &assembler_; }
  const Arm64Assembler& GetAssembler() const OVERRIDE { return assembler_; }
  vixl::aarch64::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->GetVIXLAssembler(); }

  // Emit a write barrier.
  void MarkGCCard(vixl::aarch64::Register object,
                  vixl::aarch64::Register value,
                  bool value_can_be_null);

  void GenerateMemoryBarrier(MemBarrierKind kind);

  // Register allocation.

  void SetupBlockedRegisters() const OVERRIDE;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;

  // The number of registers that can be allocated. The register allocator may
  // decide to reserve and not use a few of them.
  // We do not consider registers sp, xzr, wzr. They are either not allocatable
  // (xzr, wzr), or make for poor allocatable registers (sp alignment
  // requirements, etc.). This also facilitates our task as all other registers
  // can easily be mapped via to or from their type and index or code.
  static const int kNumberOfAllocatableRegisters = vixl::aarch64::kNumberOfRegisters - 1;
  static const int kNumberOfAllocatableFPRegisters = vixl::aarch64::kNumberOfFPRegisters;
  static constexpr int kNumberOfAllocatableRegisterPairs = 0;

  void DumpCoreRegister(std::ostream& stream, int reg) const OVERRIDE;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const OVERRIDE;

  InstructionSet GetInstructionSet() const OVERRIDE {
    return InstructionSet::kArm64;
  }

  const Arm64InstructionSetFeatures& GetInstructionSetFeatures() const {
    return isa_features_;
  }

  void Initialize() OVERRIDE {
    block_labels_.resize(GetGraph()->GetBlocks().size());
  }

  // We want to use the STP and LDP instructions to spill and restore registers for slow paths.
  // These instructions can only encode offsets that are multiples of the register size accessed.
  uint32_t GetPreferredSlotsAlignment() const OVERRIDE { return vixl::aarch64::kXRegSizeInBytes; }

  JumpTableARM64* CreateJumpTable(HPackedSwitch* switch_instr) {
    jump_tables_.emplace_back(new (GetGraph()->GetArena()) JumpTableARM64(switch_instr));
    return jump_tables_.back().get();
  }

  void Finalize(CodeAllocator* allocator) OVERRIDE;

  // Code generation helpers.
  void MoveConstant(vixl::aarch64::CPURegister destination, HConstant* constant);
  void MoveConstant(Location destination, int32_t value) OVERRIDE;
  void MoveLocation(Location dst, Location src, Primitive::Type dst_type) OVERRIDE;
  void AddLocationAsTemp(Location location, LocationSummary* locations) OVERRIDE;

  void Load(Primitive::Type type,
            vixl::aarch64::CPURegister dst,
            const vixl::aarch64::MemOperand& src);
  void Store(Primitive::Type type,
             vixl::aarch64::CPURegister src,
             const vixl::aarch64::MemOperand& dst);
  void LoadAcquire(HInstruction* instruction,
                   vixl::aarch64::CPURegister dst,
                   const vixl::aarch64::MemOperand& src,
                   bool needs_null_check);
  void StoreRelease(Primitive::Type type,
                    vixl::aarch64::CPURegister src,
                    const vixl::aarch64::MemOperand& dst);

  // Generate code to invoke a runtime entry point.
  void InvokeRuntime(QuickEntrypointEnum entrypoint,
                     HInstruction* instruction,
                     uint32_t dex_pc,
                     SlowPathCode* slow_path = nullptr) OVERRIDE;

  // Generate code to invoke a runtime entry point, but do not record
  // PC-related information in a stack map.
  void InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                           HInstruction* instruction,
                                           SlowPathCode* slow_path);

  void GenerateInvokeRuntime(int32_t entry_point_offset);

  ParallelMoveResolverARM64* GetMoveResolver() OVERRIDE { return &move_resolver_; }

  bool NeedsTwoRegisters(Primitive::Type type ATTRIBUTE_UNUSED) const OVERRIDE {
    return false;
  }

  // Check if the desired_string_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadString::LoadKind GetSupportedLoadStringKind(
      HLoadString::LoadKind desired_string_load_kind) OVERRIDE;

  // Check if the desired_class_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadClass::LoadKind GetSupportedLoadClassKind(
      HLoadClass::LoadKind desired_class_load_kind) OVERRIDE;

  // Check if the desired_dispatch_info is supported. If it is, return it,
  // otherwise return a fall-back info that should be used instead.
  HInvokeStaticOrDirect::DispatchInfo GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info,
      MethodReference target_method) OVERRIDE;

  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp) OVERRIDE;
  void GenerateVirtualCall(HInvokeVirtual* invoke, Location temp) OVERRIDE;

  void MoveFromReturnRegister(Location trg ATTRIBUTE_UNUSED,
                              Primitive::Type type ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(FATAL);
  }

  // Add a new PC-relative string patch for an instruction and return the label
  // to be bound before the instruction. The instruction will be either the
  // ADRP (pass `adrp_label = null`) or the ADD (pass `adrp_label` pointing
  // to the associated ADRP patch label).
  vixl::aarch64::Label* NewPcRelativeStringPatch(const DexFile& dex_file,
                                                 uint32_t string_index,
                                                 vixl::aarch64::Label* adrp_label = nullptr);

  // Add a new PC-relative type patch for an instruction and return the label
  // to be bound before the instruction. The instruction will be either the
  // ADRP (pass `adrp_label = null`) or the ADD (pass `adrp_label` pointing
  // to the associated ADRP patch label).
  vixl::aarch64::Label* NewPcRelativeTypePatch(const DexFile& dex_file,
                                               uint32_t type_index,
                                               vixl::aarch64::Label* adrp_label = nullptr);

  // Add a new PC-relative dex cache array patch for an instruction and return
  // the label to be bound before the instruction. The instruction will be
  // either the ADRP (pass `adrp_label = null`) or the LDR (pass `adrp_label`
  // pointing to the associated ADRP patch label).
  vixl::aarch64::Label* NewPcRelativeDexCacheArrayPatch(
      const DexFile& dex_file,
      uint32_t element_offset,
      vixl::aarch64::Label* adrp_label = nullptr);

  vixl::aarch64::Literal<uint32_t>* DeduplicateBootImageStringLiteral(const DexFile& dex_file,
                                                                      uint32_t string_index);
  vixl::aarch64::Literal<uint32_t>* DeduplicateBootImageTypeLiteral(const DexFile& dex_file,
                                                                    uint32_t type_index);
  vixl::aarch64::Literal<uint32_t>* DeduplicateBootImageAddressLiteral(uint64_t address);
  vixl::aarch64::Literal<uint64_t>* DeduplicateDexCacheAddressLiteral(uint64_t address);

  void EmitLinkerPatches(ArenaVector<LinkerPatch>* linker_patches) OVERRIDE;

  // Fast path implementation of ReadBarrier::Barrier for a heap
  // reference field load when Baker's read barriers are used.
  void GenerateFieldLoadWithBakerReadBarrier(HInstruction* instruction,
                                             Location ref,
                                             vixl::aarch64::Register obj,
                                             uint32_t offset,
                                             vixl::aarch64::Register temp,
                                             bool needs_null_check,
                                             bool use_load_acquire);
  // Fast path implementation of ReadBarrier::Barrier for a heap
  // reference array load when Baker's read barriers are used.
  void GenerateArrayLoadWithBakerReadBarrier(HInstruction* instruction,
                                             Location ref,
                                             vixl::aarch64::Register obj,
                                             uint32_t data_offset,
                                             Location index,
                                             vixl::aarch64::Register temp,
                                             bool needs_null_check);
  // Factored implementation used by GenerateFieldLoadWithBakerReadBarrier
  // and GenerateArrayLoadWithBakerReadBarrier.
  void GenerateReferenceLoadWithBakerReadBarrier(HInstruction* instruction,
                                                 Location ref,
                                                 vixl::aarch64::Register obj,
                                                 uint32_t offset,
                                                 Location index,
                                                 size_t scale_factor,
                                                 vixl::aarch64::Register temp,
                                                 bool needs_null_check,
                                                 bool use_load_acquire);

  // Generate a read barrier for a heap reference within `instruction`
  // using a slow path.
  //
  // A read barrier for an object reference read from the heap is
  // implemented as a call to the artReadBarrierSlow runtime entry
  // point, which is passed the values in locations `ref`, `obj`, and
  // `offset`:
  //
  //   mirror::Object* artReadBarrierSlow(mirror::Object* ref,
  //                                      mirror::Object* obj,
  //                                      uint32_t offset);
  //
  // The `out` location contains the value returned by
  // artReadBarrierSlow.
  //
  // When `index` is provided (i.e. for array accesses), the offset
  // value passed to artReadBarrierSlow is adjusted to take `index`
  // into account.
  void GenerateReadBarrierSlow(HInstruction* instruction,
                               Location out,
                               Location ref,
                               Location obj,
                               uint32_t offset,
                               Location index = Location::NoLocation());

  // If read barriers are enabled, generate a read barrier for a heap
  // reference using a slow path. If heap poisoning is enabled, also
  // unpoison the reference in `out`.
  void MaybeGenerateReadBarrierSlow(HInstruction* instruction,
                                    Location out,
                                    Location ref,
                                    Location obj,
                                    uint32_t offset,
                                    Location index = Location::NoLocation());

  // Generate a read barrier for a GC root within `instruction` using
  // a slow path.
  //
  // A read barrier for an object reference GC root is implemented as
  // a call to the artReadBarrierForRootSlow runtime entry point,
  // which is passed the value in location `root`:
  //
  //   mirror::Object* artReadBarrierForRootSlow(GcRoot<mirror::Object>* root);
  //
  // The `out` location contains the value returned by
  // artReadBarrierForRootSlow.
  void GenerateReadBarrierForRootSlow(HInstruction* instruction, Location out, Location root);

  void GenerateNop();

  void GenerateImplicitNullCheck(HNullCheck* instruction);
  void GenerateExplicitNullCheck(HNullCheck* instruction);

 private:
  using Uint64ToLiteralMap = ArenaSafeMap<uint64_t, vixl::aarch64::Literal<uint64_t>*>;
  using Uint32ToLiteralMap = ArenaSafeMap<uint32_t, vixl::aarch64::Literal<uint32_t>*>;
  using MethodToLiteralMap = ArenaSafeMap<MethodReference,
                                          vixl::aarch64::Literal<uint64_t>*,
                                          MethodReferenceComparator>;
  using BootStringToLiteralMap = ArenaSafeMap<StringReference,
                                              vixl::aarch64::Literal<uint32_t>*,
                                              StringReferenceValueComparator>;
  using BootTypeToLiteralMap = ArenaSafeMap<TypeReference,
                                            vixl::aarch64::Literal<uint32_t>*,
                                            TypeReferenceValueComparator>;

  vixl::aarch64::Literal<uint32_t>* DeduplicateUint32Literal(uint32_t value,
                                                             Uint32ToLiteralMap* map);
  vixl::aarch64::Literal<uint64_t>* DeduplicateUint64Literal(uint64_t value);
  vixl::aarch64::Literal<uint64_t>* DeduplicateMethodLiteral(MethodReference target_method,
                                                             MethodToLiteralMap* map);
  vixl::aarch64::Literal<uint64_t>* DeduplicateMethodAddressLiteral(MethodReference target_method);
  vixl::aarch64::Literal<uint64_t>* DeduplicateMethodCodeLiteral(MethodReference target_method);

  // The PcRelativePatchInfo is used for PC-relative addressing of dex cache arrays
  // and boot image strings/types. The only difference is the interpretation of the
  // offset_or_index.
  struct PcRelativePatchInfo {
    PcRelativePatchInfo(const DexFile& dex_file, uint32_t off_or_idx)
        : target_dex_file(dex_file), offset_or_index(off_or_idx), label(), pc_insn_label() { }

    const DexFile& target_dex_file;
    // Either the dex cache array element offset or the string/type index.
    uint32_t offset_or_index;
    vixl::aarch64::Label label;
    vixl::aarch64::Label* pc_insn_label;
  };

  vixl::aarch64::Label* NewPcRelativePatch(const DexFile& dex_file,
                                           uint32_t offset_or_index,
                                           vixl::aarch64::Label* adrp_label,
                                           ArenaDeque<PcRelativePatchInfo>* patches);

  void EmitJumpTables();

  // Labels for each block that will be compiled.
  // We use a deque so that the `vixl::aarch64::Label` objects do not move in memory.
  ArenaDeque<vixl::aarch64::Label> block_labels_;  // Indexed by block id.
  vixl::aarch64::Label frame_entry_label_;
  ArenaVector<std::unique_ptr<JumpTableARM64>> jump_tables_;

  LocationsBuilderARM64 location_builder_;
  InstructionCodeGeneratorARM64 instruction_visitor_;
  ParallelMoveResolverARM64 move_resolver_;
  Arm64Assembler assembler_;
  const Arm64InstructionSetFeatures& isa_features_;

  // Deduplication map for 32-bit literals, used for non-patchable boot image addresses.
  Uint32ToLiteralMap uint32_literals_;
  // Deduplication map for 64-bit literals, used for non-patchable method address, method code
  // or string dex cache address.
  Uint64ToLiteralMap uint64_literals_;
  // Method patch info, map MethodReference to a literal for method address and method code.
  MethodToLiteralMap method_patches_;
  MethodToLiteralMap call_patches_;
  // Relative call patch info.
  // Using ArenaDeque<> which retains element addresses on push/emplace_back().
  ArenaDeque<MethodPatchInfo<vixl::aarch64::Label>> relative_call_patches_;
  // PC-relative DexCache access info.
  ArenaDeque<PcRelativePatchInfo> pc_relative_dex_cache_patches_;
  // Deduplication map for boot string literals for kBootImageLinkTimeAddress.
  BootStringToLiteralMap boot_image_string_patches_;
  // PC-relative String patch info.
  ArenaDeque<PcRelativePatchInfo> pc_relative_string_patches_;
  // Deduplication map for boot type literals for kBootImageLinkTimeAddress.
  BootTypeToLiteralMap boot_image_type_patches_;
  // PC-relative type patch info.
  ArenaDeque<PcRelativePatchInfo> pc_relative_type_patches_;
  // Deduplication map for patchable boot image addresses.
  Uint32ToLiteralMap boot_image_address_patches_;

  DISALLOW_COPY_AND_ASSIGN(CodeGeneratorARM64);
};

inline Arm64Assembler* ParallelMoveResolverARM64::GetAssembler() const {
  return codegen_->GetAssembler();
}

}  // namespace arm64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_
