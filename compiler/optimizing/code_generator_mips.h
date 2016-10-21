/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_MIPS_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_MIPS_H_

#include "code_generator.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "parallel_move_resolver.h"
#include "string_reference.h"
#include "utils/mips/assembler_mips.h"
#include "utils/type_reference.h"

namespace art {
namespace mips {

// InvokeDexCallingConvention registers

static constexpr Register kParameterCoreRegisters[] =
    { A1, A2, A3 };
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);

static constexpr FRegister kParameterFpuRegisters[] =
    { F12, F14 };
static constexpr size_t kParameterFpuRegistersLength = arraysize(kParameterFpuRegisters);


// InvokeRuntimeCallingConvention registers

static constexpr Register kRuntimeParameterCoreRegisters[] =
    { A0, A1, A2, A3 };
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);

static constexpr FRegister kRuntimeParameterFpuRegisters[] =
    { F12, F14};
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterFpuRegisters);


static constexpr Register kCoreCalleeSaves[] =
    { S0, S1, S2, S3, S4, S5, S6, S7, FP, RA };
static constexpr FRegister kFpuCalleeSaves[] =
    { F20, F22, F24, F26, F28, F30 };


class CodeGeneratorMIPS;

class InvokeDexCallingConvention : public CallingConvention<Register, FRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFpuRegisters,
                          kParameterFpuRegistersLength,
                          kMipsPointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorMIPS : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorMIPS() {}
  virtual ~InvokeDexCallingConventionVisitorMIPS() {}

  Location GetNextLocation(Primitive::Type type) OVERRIDE;
  Location GetReturnLocation(Primitive::Type type) const OVERRIDE;
  Location GetMethodLocation() const OVERRIDE;

 private:
  InvokeDexCallingConvention calling_convention;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorMIPS);
};

class InvokeRuntimeCallingConvention : public CallingConvention<Register, FRegister> {
 public:
  InvokeRuntimeCallingConvention()
      : CallingConvention(kRuntimeParameterCoreRegisters,
                          kRuntimeParameterCoreRegistersLength,
                          kRuntimeParameterFpuRegisters,
                          kRuntimeParameterFpuRegistersLength,
                          kMipsPointerSize) {}

  Location GetReturnLocation(Primitive::Type return_type);

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConvention);
};

class FieldAccessCallingConventionMIPS : public FieldAccessCallingConvention {
 public:
  FieldAccessCallingConventionMIPS() {}

  Location GetObjectLocation() const OVERRIDE {
    return Location::RegisterLocation(A1);
  }
  Location GetFieldIndexLocation() const OVERRIDE {
    return Location::RegisterLocation(A0);
  }
  Location GetReturnLocation(Primitive::Type type) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? Location::RegisterPairLocation(V0, V1)
        : Location::RegisterLocation(V0);
  }
  Location GetSetValueLocation(Primitive::Type type, bool is_instance) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? Location::RegisterPairLocation(A2, A3)
        : (is_instance ? Location::RegisterLocation(A2) : Location::RegisterLocation(A1));
  }
  Location GetFpuLocation(Primitive::Type type ATTRIBUTE_UNUSED) const OVERRIDE {
    return Location::FpuRegisterLocation(F0);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FieldAccessCallingConventionMIPS);
};

class ParallelMoveResolverMIPS : public ParallelMoveResolverWithSwap {
 public:
  ParallelMoveResolverMIPS(ArenaAllocator* allocator, CodeGeneratorMIPS* codegen)
      : ParallelMoveResolverWithSwap(allocator), codegen_(codegen) {}

  void EmitMove(size_t index) OVERRIDE;
  void EmitSwap(size_t index) OVERRIDE;
  void SpillScratch(int reg) OVERRIDE;
  void RestoreScratch(int reg) OVERRIDE;

  void Exchange(int index1, int index2, bool double_slot);

  MipsAssembler* GetAssembler() const;

 private:
  CodeGeneratorMIPS* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverMIPS);
};

class SlowPathCodeMIPS : public SlowPathCode {
 public:
  explicit SlowPathCodeMIPS(HInstruction* instruction)
      : SlowPathCode(instruction), entry_label_(), exit_label_() {}

  MipsLabel* GetEntryLabel() { return &entry_label_; }
  MipsLabel* GetExitLabel() { return &exit_label_; }

 private:
  MipsLabel entry_label_;
  MipsLabel exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeMIPS);
};

class LocationsBuilderMIPS : public HGraphVisitor {
 public:
  LocationsBuilderMIPS(HGraph* graph, CodeGeneratorMIPS* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super)     \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_MIPS(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

 private:
  void HandleInvoke(HInvoke* invoke);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);
  Location RegisterOrZeroConstant(HInstruction* instruction);
  Location FpuRegisterOrConstantForStore(HInstruction* instruction);

  InvokeDexCallingConventionVisitorMIPS parameter_visitor_;

  CodeGeneratorMIPS* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderMIPS);
};

class InstructionCodeGeneratorMIPS : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorMIPS(HGraph* graph, CodeGeneratorMIPS* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super)     \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_MIPS(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

  MipsAssembler* GetAssembler() const { return assembler_; }

  // Compare-and-jump packed switch generates approx. 3 + 2.5 * N 32-bit
  // instructions for N cases.
  // Table-based packed switch generates approx. 11 32-bit instructions
  // and N 32-bit data words for N cases.
  // At N = 6 they come out as 18 and 17 32-bit words respectively.
  // We switch to the table-based method starting with 7 cases.
  static constexpr uint32_t kPackedSwitchJumpTableThreshold = 6;

 private:
  void GenerateClassInitializationCheck(SlowPathCodeMIPS* slow_path, Register class_reg);
  void GenerateMemoryBarrier(MemBarrierKind kind);
  void GenerateSuspendCheck(HSuspendCheck* check, HBasicBlock* successor);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info, uint32_t dex_pc);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info, uint32_t dex_pc);
  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers (if any).
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               Register obj,
                               uint32_t offset);
  void GenerateIntCompare(IfCondition cond, LocationSummary* locations);
  // When the function returns `false` it means that the condition holds if `dst` is non-zero
  // and doesn't hold if `dst` is zero. If it returns `true`, the roles of zero and non-zero
  // `dst` are exchanged.
  bool MaterializeIntCompare(IfCondition cond,
                             LocationSummary* input_locations,
                             Register dst);
  void GenerateIntCompareAndBranch(IfCondition cond,
                                   LocationSummary* locations,
                                   MipsLabel* label);
  void GenerateLongCompareAndBranch(IfCondition cond,
                                    LocationSummary* locations,
                                    MipsLabel* label);
  void GenerateFpCompare(IfCondition cond,
                         bool gt_bias,
                         Primitive::Type type,
                         LocationSummary* locations);
  // When the function returns `false` it means that the condition holds if the condition
  // code flag `cc` is non-zero and doesn't hold if `cc` is zero. If it returns `true`,
  // the roles of zero and non-zero values of the `cc` flag are exchanged.
  bool MaterializeFpCompareR2(IfCondition cond,
                              bool gt_bias,
                              Primitive::Type type,
                              LocationSummary* input_locations,
                              int cc);
  // When the function returns `false` it means that the condition holds if `dst` is non-zero
  // and doesn't hold if `dst` is zero. If it returns `true`, the roles of zero and non-zero
  // `dst` are exchanged.
  bool MaterializeFpCompareR6(IfCondition cond,
                              bool gt_bias,
                              Primitive::Type type,
                              LocationSummary* input_locations,
                              FRegister dst);
  void GenerateFpCompareAndBranch(IfCondition cond,
                                  bool gt_bias,
                                  Primitive::Type type,
                                  LocationSummary* locations,
                                  MipsLabel* label);
  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             MipsLabel* true_target,
                             MipsLabel* false_target);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemIntegral(HBinaryOperation* instruction);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);
  auto GetImplicitNullChecker(HInstruction* instruction);
  void GenPackedSwitchWithCompares(Register value_reg,
                                   int32_t lower_bound,
                                   uint32_t num_entries,
                                   HBasicBlock* switch_block,
                                   HBasicBlock* default_block);
  void GenTableBasedPackedSwitch(Register value_reg,
                                 Register constant_area,
                                 int32_t lower_bound,
                                 uint32_t num_entries,
                                 HBasicBlock* switch_block,
                                 HBasicBlock* default_block);
  void GenConditionalMoveR2(HSelect* select);
  void GenConditionalMoveR6(HSelect* select);

  MipsAssembler* const assembler_;
  CodeGeneratorMIPS* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorMIPS);
};

class CodeGeneratorMIPS : public CodeGenerator {
 public:
  CodeGeneratorMIPS(HGraph* graph,
                    const MipsInstructionSetFeatures& isa_features,
                    const CompilerOptions& compiler_options,
                    OptimizingCompilerStats* stats = nullptr);
  virtual ~CodeGeneratorMIPS() {}

  void ComputeSpillMask() OVERRIDE;
  bool HasAllocatedCalleeSaveRegisters() const OVERRIDE;
  void GenerateFrameEntry() OVERRIDE;
  void GenerateFrameExit() OVERRIDE;

  void Bind(HBasicBlock* block) OVERRIDE;

  void Move32(Location destination, Location source);
  void Move64(Location destination, Location source);
  void MoveConstant(Location location, HConstant* c);

  size_t GetWordSize() const OVERRIDE { return kMipsWordSize; }

  size_t GetFloatingPointSpillSlotSize() const OVERRIDE { return kMipsDoublewordSize; }

  uintptr_t GetAddressOf(HBasicBlock* block) OVERRIDE {
    return assembler_.GetLabelLocation(GetLabelOf(block));
  }

  HGraphVisitor* GetLocationBuilder() OVERRIDE { return &location_builder_; }
  HGraphVisitor* GetInstructionVisitor() OVERRIDE { return &instruction_visitor_; }
  MipsAssembler* GetAssembler() OVERRIDE { return &assembler_; }
  const MipsAssembler& GetAssembler() const OVERRIDE { return assembler_; }

  // Emit linker patches.
  void EmitLinkerPatches(ArenaVector<LinkerPatch>* linker_patches) OVERRIDE;

  void MarkGCCard(Register object, Register value);

  // Register allocation.

  void SetupBlockedRegisters() const OVERRIDE;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  void ClobberRA() {
    clobbered_ra_ = true;
  }

  void DumpCoreRegister(std::ostream& stream, int reg) const OVERRIDE;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const OVERRIDE;

  InstructionSet GetInstructionSet() const OVERRIDE { return InstructionSet::kMips; }

  const MipsInstructionSetFeatures& GetInstructionSetFeatures() const {
    return isa_features_;
  }

  MipsLabel* GetLabelOf(HBasicBlock* block) const {
    return CommonGetLabelOf<MipsLabel>(block_labels_, block);
  }

  void Initialize() OVERRIDE {
    block_labels_ = CommonInitializeLabels<MipsLabel>();
  }

  void Finalize(CodeAllocator* allocator) OVERRIDE;

  // Code generation helpers.

  void MoveLocation(Location dst, Location src, Primitive::Type dst_type) OVERRIDE;

  void MoveConstant(Location destination, int32_t value) OVERRIDE;

  void AddLocationAsTemp(Location location, LocationSummary* locations) OVERRIDE;

  // Generate code to invoke a runtime entry point.
  void InvokeRuntime(QuickEntrypointEnum entrypoint,
                     HInstruction* instruction,
                     uint32_t dex_pc,
                     SlowPathCode* slow_path = nullptr) OVERRIDE;

  ParallelMoveResolver* GetMoveResolver() OVERRIDE { return &move_resolver_; }

  bool NeedsTwoRegisters(Primitive::Type type) const OVERRIDE {
    return type == Primitive::kPrimLong;
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
      HInvokeStaticOrDirect* invoke) OVERRIDE;

  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp);
  void GenerateVirtualCall(HInvokeVirtual* invoke, Location temp) OVERRIDE;

  void MoveFromReturnRegister(Location trg ATTRIBUTE_UNUSED,
                              Primitive::Type type ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(FATAL) << "Not implemented on MIPS";
  }

  void GenerateNop() OVERRIDE;
  void GenerateImplicitNullCheck(HNullCheck* instruction) OVERRIDE;
  void GenerateExplicitNullCheck(HNullCheck* instruction) OVERRIDE;

  // The PcRelativePatchInfo is used for PC-relative addressing of dex cache arrays
  // and boot image strings. The only difference is the interpretation of the offset_or_index.
  struct PcRelativePatchInfo {
    PcRelativePatchInfo(const DexFile& dex_file, uint32_t off_or_idx)
        : target_dex_file(dex_file), offset_or_index(off_or_idx) { }
    PcRelativePatchInfo(PcRelativePatchInfo&& other) = default;

    const DexFile& target_dex_file;
    // Either the dex cache array element offset or the string/type index.
    uint32_t offset_or_index;
    // Label for the instruction loading the most significant half of the offset that's added to PC
    // to form the base address (the least significant half is loaded with the instruction that
    // follows).
    MipsLabel high_label;
    // Label for the instruction corresponding to PC+0.
    MipsLabel pc_rel_label;
  };

  PcRelativePatchInfo* NewPcRelativeStringPatch(const DexFile& dex_file, uint32_t string_index);
  PcRelativePatchInfo* NewPcRelativeTypePatch(const DexFile& dex_file, uint32_t type_index);
  PcRelativePatchInfo* NewPcRelativeDexCacheArrayPatch(const DexFile& dex_file,
                                                       uint32_t element_offset);
  Literal* DeduplicateBootImageStringLiteral(const DexFile& dex_file, uint32_t string_index);
  Literal* DeduplicateBootImageTypeLiteral(const DexFile& dex_file, uint32_t type_index);
  Literal* DeduplicateBootImageAddressLiteral(uint32_t address);

  void EmitPcRelativeAddressPlaceholder(PcRelativePatchInfo* info, Register out, Register base);

 private:
  Register GetInvokeStaticOrDirectExtraParameter(HInvokeStaticOrDirect* invoke, Register temp);

  using Uint32ToLiteralMap = ArenaSafeMap<uint32_t, Literal*>;
  using MethodToLiteralMap = ArenaSafeMap<MethodReference, Literal*, MethodReferenceComparator>;
  using BootStringToLiteralMap = ArenaSafeMap<StringReference,
                                              Literal*,
                                              StringReferenceValueComparator>;
  using BootTypeToLiteralMap = ArenaSafeMap<TypeReference,
                                            Literal*,
                                            TypeReferenceValueComparator>;

  Literal* DeduplicateUint32Literal(uint32_t value, Uint32ToLiteralMap* map);
  Literal* DeduplicateMethodLiteral(MethodReference target_method, MethodToLiteralMap* map);
  Literal* DeduplicateMethodAddressLiteral(MethodReference target_method);
  Literal* DeduplicateMethodCodeLiteral(MethodReference target_method);
  PcRelativePatchInfo* NewPcRelativePatch(const DexFile& dex_file,
                                          uint32_t offset_or_index,
                                          ArenaDeque<PcRelativePatchInfo>* patches);

  template <LinkerPatch (*Factory)(size_t, const DexFile*, uint32_t, uint32_t)>
  void EmitPcRelativeLinkerPatches(const ArenaDeque<PcRelativePatchInfo>& infos,
                                   ArenaVector<LinkerPatch>* linker_patches);

  // Labels for each block that will be compiled.
  MipsLabel* block_labels_;
  MipsLabel frame_entry_label_;
  LocationsBuilderMIPS location_builder_;
  InstructionCodeGeneratorMIPS instruction_visitor_;
  ParallelMoveResolverMIPS move_resolver_;
  MipsAssembler assembler_;
  const MipsInstructionSetFeatures& isa_features_;

  // Deduplication map for 32-bit literals, used for non-patchable boot image addresses.
  Uint32ToLiteralMap uint32_literals_;
  // Method patch info, map MethodReference to a literal for method address and method code.
  MethodToLiteralMap method_patches_;
  MethodToLiteralMap call_patches_;
  // PC-relative patch info for each HMipsDexCacheArraysBase.
  ArenaDeque<PcRelativePatchInfo> pc_relative_dex_cache_patches_;
  // Deduplication map for boot string literals for kBootImageLinkTimeAddress.
  BootStringToLiteralMap boot_image_string_patches_;
  // PC-relative String patch info; type depends on configuration (app .bss or boot image PIC).
  ArenaDeque<PcRelativePatchInfo> pc_relative_string_patches_;
  // Deduplication map for boot type literals for kBootImageLinkTimeAddress.
  BootTypeToLiteralMap boot_image_type_patches_;
  // PC-relative type patch info.
  ArenaDeque<PcRelativePatchInfo> pc_relative_type_patches_;
  // Deduplication map for patchable boot image addresses.
  Uint32ToLiteralMap boot_image_address_patches_;

  // PC-relative loads on R2 clobber RA, which may need to be preserved explicitly in leaf methods.
  // This is a flag set by pc_relative_fixups_mips and dex_cache_array_fixups_mips optimizations.
  bool clobbered_ra_;

  DISALLOW_COPY_AND_ASSIGN(CodeGeneratorMIPS);
};

}  // namespace mips
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_MIPS_H_
