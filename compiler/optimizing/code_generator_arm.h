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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_H_

#include "base/enums.h"
#include "code_generator.h"
#include "dex_file_types.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "string_reference.h"
#include "parallel_move_resolver.h"
#include "utils/arm/assembler_thumb2.h"
#include "utils/type_reference.h"

namespace art {
namespace arm {

class CodeGeneratorARM;

// Use a local definition to prevent copying mistakes.
static constexpr size_t kArmWordSize = static_cast<size_t>(kArmPointerSize);
static constexpr size_t kArmBitsPerWord = kArmWordSize * kBitsPerByte;

static constexpr Register kParameterCoreRegisters[] = { R1, R2, R3 };
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);
static constexpr SRegister kParameterFpuRegisters[] =
    { S0, S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15 };
static constexpr size_t kParameterFpuRegistersLength = arraysize(kParameterFpuRegisters);

static constexpr Register kArtMethodRegister = R0;

static constexpr Register kRuntimeParameterCoreRegisters[] = { R0, R1, R2, R3 };
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);
static constexpr SRegister kRuntimeParameterFpuRegisters[] = { S0, S1, S2, S3 };
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterFpuRegisters);

class SlowPathCodeARM : public SlowPathCode {
 public:
  explicit SlowPathCodeARM(HInstruction* instruction) : SlowPathCode(instruction) {}

  void SaveLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) FINAL;
  void RestoreLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) FINAL;

 private:
  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeARM);
};


class InvokeRuntimeCallingConvention : public CallingConvention<Register, SRegister> {
 public:
  InvokeRuntimeCallingConvention()
      : CallingConvention(kRuntimeParameterCoreRegisters,
                          kRuntimeParameterCoreRegistersLength,
                          kRuntimeParameterFpuRegisters,
                          kRuntimeParameterFpuRegistersLength,
                          kArmPointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConvention);
};

constexpr DRegister FromLowSToD(SRegister reg) {
  DCHECK_EQ(reg % 2, 0);
  return static_cast<DRegister>(reg / 2);
}


class InvokeDexCallingConvention : public CallingConvention<Register, SRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFpuRegisters,
                          kParameterFpuRegistersLength,
                          kArmPointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorARM : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorARM() {}
  virtual ~InvokeDexCallingConventionVisitorARM() {}

  Location GetNextLocation(Primitive::Type type) OVERRIDE;
  Location GetReturnLocation(Primitive::Type type) const OVERRIDE;
  Location GetMethodLocation() const OVERRIDE;

 private:
  InvokeDexCallingConvention calling_convention;
  uint32_t double_index_ = 0;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorARM);
};

class FieldAccessCallingConventionARM : public FieldAccessCallingConvention {
 public:
  FieldAccessCallingConventionARM() {}

  Location GetObjectLocation() const OVERRIDE {
    return Location::RegisterLocation(R1);
  }
  Location GetFieldIndexLocation() const OVERRIDE {
    return Location::RegisterLocation(R0);
  }
  Location GetReturnLocation(Primitive::Type type) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? Location::RegisterPairLocation(R0, R1)
        : Location::RegisterLocation(R0);
  }
  Location GetSetValueLocation(Primitive::Type type, bool is_instance) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? Location::RegisterPairLocation(R2, R3)
        : (is_instance
            ? Location::RegisterLocation(R2)
            : Location::RegisterLocation(R1));
  }
  Location GetFpuLocation(Primitive::Type type) const OVERRIDE {
    return Primitive::Is64BitType(type)
        ? Location::FpuRegisterPairLocation(S0, S1)
        : Location::FpuRegisterLocation(S0);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FieldAccessCallingConventionARM);
};

class ParallelMoveResolverARM : public ParallelMoveResolverWithSwap {
 public:
  ParallelMoveResolverARM(ArenaAllocator* allocator, CodeGeneratorARM* codegen)
      : ParallelMoveResolverWithSwap(allocator), codegen_(codegen) {}

  void EmitMove(size_t index) OVERRIDE;
  void EmitSwap(size_t index) OVERRIDE;
  void SpillScratch(int reg) OVERRIDE;
  void RestoreScratch(int reg) OVERRIDE;

  ArmAssembler* GetAssembler() const;

 private:
  void Exchange(Register reg, int mem);
  void Exchange(int mem1, int mem2);

  CodeGeneratorARM* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverARM);
};

class LocationsBuilderARM : public HGraphVisitor {
 public:
  LocationsBuilderARM(HGraph* graph, CodeGeneratorARM* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super)     \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_ARM(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_SHARED(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

 private:
  void HandleInvoke(HInvoke* invoke);
  void HandleBitwiseOperation(HBinaryOperation* operation, Opcode opcode);
  void HandleCondition(HCondition* condition);
  void HandleIntegerRotate(LocationSummary* locations);
  void HandleLongRotate(LocationSummary* locations);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

  Location ArithmeticZeroOrFpuRegister(HInstruction* input);
  Location ArmEncodableConstantOrRegister(HInstruction* constant, Opcode opcode);
  bool CanEncodeConstantAsImmediate(HConstant* input_cst, Opcode opcode);
  bool CanEncodeConstantAsImmediate(uint32_t value, Opcode opcode, SetCc set_cc = kCcDontCare);

  CodeGeneratorARM* const codegen_;
  InvokeDexCallingConventionVisitorARM parameter_visitor_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderARM);
};

class InstructionCodeGeneratorARM : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorARM(HGraph* graph, CodeGeneratorARM* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super)     \
  void Visit##name(H##name* instr) OVERRIDE;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_ARM(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_SHARED(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) OVERRIDE {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName()
               << " (id " << instruction->GetId() << ")";
  }

  ArmAssembler* GetAssembler() const { return assembler_; }

 private:
  // Generate code for the given suspend check. If not null, `successor`
  // is the block to branch to if the suspend check is not needed, and after
  // the suspend call.
  void GenerateSuspendCheck(HSuspendCheck* check, HBasicBlock* successor);
  void GenerateClassInitializationCheck(SlowPathCodeARM* slow_path, Register class_reg);
  void GenerateAndConst(Register out, Register first, uint32_t value);
  void GenerateOrrConst(Register out, Register first, uint32_t value);
  void GenerateEorConst(Register out, Register first, uint32_t value);
  void GenerateAddLongConst(Location out, Location first, uint64_t value);
  void HandleBitwiseOperation(HBinaryOperation* operation);
  void HandleCondition(HCondition* condition);
  void HandleIntegerRotate(LocationSummary* locations);
  void HandleLongRotate(HRor* ror);
  void HandleShift(HBinaryOperation* operation);

  void GenerateWideAtomicStore(Register addr, uint32_t offset,
                               Register value_lo, Register value_hi,
                               Register temp1, Register temp2,
                               HInstruction* instruction);
  void GenerateWideAtomicLoad(Register addr, uint32_t offset,
                              Register out_lo, Register out_hi);

  void HandleFieldSet(HInstruction* instruction,
                      const FieldInfo& field_info,
                      bool value_can_be_null);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

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
                                        Location maybe_temp,
                                        ReadBarrierOption read_barrier_option);
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
                                         Location maybe_temp,
                                         ReadBarrierOption read_barrier_option);
  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers based on read_barrier_option.
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               Register obj,
                               uint32_t offset,
                               ReadBarrierOption read_barrier_option);
  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             Label* true_target,
                             Label* false_target);
  void GenerateCompareTestAndBranch(HCondition* condition,
                                    Label* true_target,
                                    Label* false_target);
  void GenerateLongComparesAndJumps(HCondition* cond, Label* true_label, Label* false_label);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemConstantIntegral(HBinaryOperation* instruction);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);

  ArmAssembler* const assembler_;
  CodeGeneratorARM* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorARM);
};

class CodeGeneratorARM : public CodeGenerator {
 public:
  CodeGeneratorARM(HGraph* graph,
                   const ArmInstructionSetFeatures& isa_features,
                   const CompilerOptions& compiler_options,
                   OptimizingCompilerStats* stats = nullptr);
  virtual ~CodeGeneratorARM() {}

  void GenerateFrameEntry() OVERRIDE;
  void GenerateFrameExit() OVERRIDE;
  void Bind(HBasicBlock* block) OVERRIDE;
  void MoveConstant(Location destination, int32_t value) OVERRIDE;
  void MoveLocation(Location dst, Location src, Primitive::Type dst_type) OVERRIDE;
  void AddLocationAsTemp(Location location, LocationSummary* locations) OVERRIDE;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) OVERRIDE;

  size_t GetWordSize() const OVERRIDE {
    return kArmWordSize;
  }

  size_t GetFloatingPointSpillSlotSize() const OVERRIDE {
    // Allocated in S registers, which are word sized.
    return kArmWordSize;
  }

  HGraphVisitor* GetLocationBuilder() OVERRIDE {
    return &location_builder_;
  }

  HGraphVisitor* GetInstructionVisitor() OVERRIDE {
    return &instruction_visitor_;
  }

  ArmAssembler* GetAssembler() OVERRIDE {
    return &assembler_;
  }

  const ArmAssembler& GetAssembler() const OVERRIDE {
    return assembler_;
  }

  uintptr_t GetAddressOf(HBasicBlock* block) OVERRIDE {
    return GetLabelOf(block)->Position();
  }

  void SetupBlockedRegisters() const OVERRIDE;

  void DumpCoreRegister(std::ostream& stream, int reg) const OVERRIDE;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const OVERRIDE;

  ParallelMoveResolverARM* GetMoveResolver() OVERRIDE {
    return &move_resolver_;
  }

  InstructionSet GetInstructionSet() const OVERRIDE {
    return InstructionSet::kThumb2;
  }

  // Helper method to move a 32bits value between two locations.
  void Move32(Location destination, Location source);
  // Helper method to move a 64bits value between two locations.
  void Move64(Location destination, Location source);

  void LoadOrStoreToOffset(Primitive::Type type,
                           Location loc,
                           Register base,
                           int32_t offset,
                           bool is_load,
                           Condition cond = AL);

  void LoadFromShiftedRegOffset(Primitive::Type type,
                                Location out_loc,
                                Register base,
                                Register reg_offset,
                                Condition cond = AL);
  void StoreToShiftedRegOffset(Primitive::Type type,
                               Location out_loc,
                               Register base,
                               Register reg_offset,
                               Condition cond = AL);

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

  // Emit a write barrier.
  void MarkGCCard(Register temp, Register card, Register object, Register value, bool can_be_null);

  void GenerateMemoryBarrier(MemBarrierKind kind);

  Label* GetLabelOf(HBasicBlock* block) const {
    return CommonGetLabelOf<Label>(block_labels_, block);
  }

  Label* GetFinalLabel(HInstruction* instruction, Label* final_label);

  void Initialize() OVERRIDE {
    block_labels_ = CommonInitializeLabels<Label>();
  }

  void Finalize(CodeAllocator* allocator) OVERRIDE;

  const ArmInstructionSetFeatures& GetInstructionSetFeatures() const {
    return isa_features_;
  }

  bool NeedsTwoRegisters(Primitive::Type type) const OVERRIDE {
    return type == Primitive::kPrimDouble || type == Primitive::kPrimLong;
  }

  void ComputeSpillMask() OVERRIDE;

  Label* GetFrameEntryLabel() { return &frame_entry_label_; }

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

  Location GenerateCalleeMethodStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp);
  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp) OVERRIDE;
  void GenerateVirtualCall(HInvokeVirtual* invoke, Location temp) OVERRIDE;

  void MoveFromReturnRegister(Location trg, Primitive::Type type) OVERRIDE;

  // The PcRelativePatchInfo is used for PC-relative addressing of dex cache arrays
  // and boot image strings/types. The only difference is the interpretation of the
  // offset_or_index. The PC-relative address is loaded with three instructions,
  // MOVW+MOVT to load the offset to base_reg and then ADD base_reg, PC. The offset
  // is calculated from the ADD's effective PC, i.e. PC+4 on Thumb2. Though we
  // currently emit these 3 instructions together, instruction scheduling could
  // split this sequence apart, so we keep separate labels for each of them.
  struct PcRelativePatchInfo {
    PcRelativePatchInfo(const DexFile& dex_file, uint32_t off_or_idx)
        : target_dex_file(dex_file), offset_or_index(off_or_idx) { }
    PcRelativePatchInfo(PcRelativePatchInfo&& other) = default;

    const DexFile& target_dex_file;
    // Either the dex cache array element offset or the string/type index.
    uint32_t offset_or_index;
    Label movw_label;
    Label movt_label;
    Label add_pc_label;
  };

  PcRelativePatchInfo* NewPcRelativeStringPatch(const DexFile& dex_file,
                                                dex::StringIndex string_index);
  PcRelativePatchInfo* NewPcRelativeTypePatch(const DexFile& dex_file, dex::TypeIndex type_index);
  PcRelativePatchInfo* NewTypeBssEntryPatch(const DexFile& dex_file, dex::TypeIndex type_index);
  PcRelativePatchInfo* NewPcRelativeDexCacheArrayPatch(const DexFile& dex_file,
                                                       uint32_t element_offset);
  Literal* DeduplicateBootImageStringLiteral(const DexFile& dex_file,
                                             dex::StringIndex string_index);
  Literal* DeduplicateBootImageTypeLiteral(const DexFile& dex_file, dex::TypeIndex type_index);
  Literal* DeduplicateBootImageAddressLiteral(uint32_t address);
  Literal* DeduplicateJitStringLiteral(const DexFile& dex_file,
                                       dex::StringIndex string_index,
                                       Handle<mirror::String> handle);
  Literal* DeduplicateJitClassLiteral(const DexFile& dex_file,
                                      dex::TypeIndex type_index,
                                      Handle<mirror::Class> handle);

  void EmitLinkerPatches(ArenaVector<LinkerPatch>* linker_patches) OVERRIDE;

  void EmitJitRootPatches(uint8_t* code, const uint8_t* roots_data) OVERRIDE;

  // Fast path implementation of ReadBarrier::Barrier for a heap
  // reference field load when Baker's read barriers are used.
  void GenerateFieldLoadWithBakerReadBarrier(HInstruction* instruction,
                                             Location ref,
                                             Register obj,
                                             uint32_t offset,
                                             Location temp,
                                             bool needs_null_check);
  // Fast path implementation of ReadBarrier::Barrier for a heap
  // reference array load when Baker's read barriers are used.
  void GenerateArrayLoadWithBakerReadBarrier(HInstruction* instruction,
                                             Location ref,
                                             Register obj,
                                             uint32_t data_offset,
                                             Location index,
                                             Location temp,
                                             bool needs_null_check);
  // Factored implementation, used by GenerateFieldLoadWithBakerReadBarrier,
  // GenerateArrayLoadWithBakerReadBarrier and some intrinsics.
  //
  // Load the object reference located at the address
  // `obj + offset + (index << scale_factor)`, held by object `obj`, into
  // `ref`, and mark it if needed.
  //
  // If `always_update_field` is true, the value of the reference is
  // atomically updated in the holder (`obj`).  This operation
  // requires an extra temporary register, which must be provided as a
  // non-null pointer (`temp2`).
  void GenerateReferenceLoadWithBakerReadBarrier(HInstruction* instruction,
                                                 Location ref,
                                                 Register obj,
                                                 uint32_t offset,
                                                 Location index,
                                                 ScaleFactor scale_factor,
                                                 Location temp,
                                                 bool needs_null_check,
                                                 bool always_update_field = false,
                                                 Register* temp2 = nullptr);

  // Generate a heap reference load (with no read barrier).
  void GenerateRawReferenceLoad(HInstruction* instruction,
                                Location ref,
                                Register obj,
                                uint32_t offset,
                                Location index,
                                ScaleFactor scale_factor,
                                bool needs_null_check);

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

  void GenerateNop() OVERRIDE;

  void GenerateImplicitNullCheck(HNullCheck* instruction) OVERRIDE;
  void GenerateExplicitNullCheck(HNullCheck* instruction) OVERRIDE;

 private:
  Register GetInvokeStaticOrDirectExtraParameter(HInvokeStaticOrDirect* invoke, Register temp);

  using Uint32ToLiteralMap = ArenaSafeMap<uint32_t, Literal*>;
  using MethodToLiteralMap = ArenaSafeMap<MethodReference, Literal*, MethodReferenceComparator>;
  using StringToLiteralMap = ArenaSafeMap<StringReference,
                                          Literal*,
                                          StringReferenceValueComparator>;
  using TypeToLiteralMap = ArenaSafeMap<TypeReference,
                                        Literal*,
                                        TypeReferenceValueComparator>;

  Literal* DeduplicateUint32Literal(uint32_t value, Uint32ToLiteralMap* map);
  Literal* DeduplicateMethodLiteral(MethodReference target_method, MethodToLiteralMap* map);
  PcRelativePatchInfo* NewPcRelativePatch(const DexFile& dex_file,
                                          uint32_t offset_or_index,
                                          ArenaDeque<PcRelativePatchInfo>* patches);
  template <LinkerPatch (*Factory)(size_t, const DexFile*, uint32_t, uint32_t)>
  static void EmitPcRelativeLinkerPatches(const ArenaDeque<PcRelativePatchInfo>& infos,
                                          ArenaVector<LinkerPatch>* linker_patches);

  // Labels for each block that will be compiled.
  Label* block_labels_;  // Indexed by block id.
  Label frame_entry_label_;
  LocationsBuilderARM location_builder_;
  InstructionCodeGeneratorARM instruction_visitor_;
  ParallelMoveResolverARM move_resolver_;
  Thumb2Assembler assembler_;
  const ArmInstructionSetFeatures& isa_features_;

  // Deduplication map for 32-bit literals, used for non-patchable boot image addresses.
  Uint32ToLiteralMap uint32_literals_;
  // PC-relative patch info for each HArmDexCacheArraysBase.
  ArenaDeque<PcRelativePatchInfo> pc_relative_dex_cache_patches_;
  // Deduplication map for boot string literals for kBootImageLinkTimeAddress.
  StringToLiteralMap boot_image_string_patches_;
  // PC-relative String patch info; type depends on configuration (app .bss or boot image PIC).
  ArenaDeque<PcRelativePatchInfo> pc_relative_string_patches_;
  // Deduplication map for boot type literals for kBootImageLinkTimeAddress.
  TypeToLiteralMap boot_image_type_patches_;
  // PC-relative type patch info for kBootImageLinkTimePcRelative.
  ArenaDeque<PcRelativePatchInfo> pc_relative_type_patches_;
  // PC-relative type patch info for kBssEntry.
  ArenaDeque<PcRelativePatchInfo> type_bss_entry_patches_;

  // Patches for string literals in JIT compiled code.
  StringToLiteralMap jit_string_patches_;
  // Patches for class literals in JIT compiled code.
  TypeToLiteralMap jit_class_patches_;

  DISALLOW_COPY_AND_ASSIGN(CodeGeneratorARM);
};

}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_H_
