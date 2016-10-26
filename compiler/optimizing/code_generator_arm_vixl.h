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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_VIXL_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_VIXL_H_

#include "code_generator_arm.h"
#include "utils/arm/assembler_arm_vixl.h"

// TODO(VIXL): make vixl clean wrt -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch32/constants-aarch32.h"
#include "aarch32/instructions-aarch32.h"
#include "aarch32/macro-assembler-aarch32.h"
#pragma GCC diagnostic pop

// True if VIXL32 should be used for codegen on ARM.
#ifdef ART_USE_VIXL_ARM_BACKEND
static constexpr bool kArmUseVIXL32 = true;
#else
static constexpr bool kArmUseVIXL32 = false;
#endif

namespace art {
namespace arm {

static const vixl::aarch32::Register kParameterCoreRegistersVIXL[] = {
    vixl::aarch32::r1,
    vixl::aarch32::r2,
    vixl::aarch32::r3
};
static const size_t kParameterCoreRegistersLengthVIXL = arraysize(kParameterCoreRegisters);
static const vixl::aarch32::SRegister kParameterFpuRegistersVIXL[] = {
    vixl::aarch32::s0,
    vixl::aarch32::s1,
    vixl::aarch32::s2,
    vixl::aarch32::s3,
    vixl::aarch32::s4,
    vixl::aarch32::s5,
    vixl::aarch32::s6,
    vixl::aarch32::s7,
    vixl::aarch32::s8,
    vixl::aarch32::s9,
    vixl::aarch32::s10,
    vixl::aarch32::s11,
    vixl::aarch32::s12,
    vixl::aarch32::s13,
    vixl::aarch32::s14,
    vixl::aarch32::s15
};
static const size_t kParameterFpuRegistersLengthVIXL = arraysize(kParameterFpuRegisters);

static const vixl::aarch32::Register kMethodRegister = vixl::aarch32::r0;

static const vixl::aarch32::Register kCoreAlwaysSpillRegister = vixl::aarch32::r5;

// Callee saves core registers r5, r6, r7, r8, r10, r11, and lr.
static const vixl::aarch32::RegisterList kCoreCalleeSaves = vixl::aarch32::RegisterList::Union(
    vixl::aarch32::RegisterList(vixl::aarch32::r5,
                                vixl::aarch32::r6,
                                vixl::aarch32::r7,
                                vixl::aarch32::r8),
    vixl::aarch32::RegisterList(vixl::aarch32::r10,
                                vixl::aarch32::r11,
                                vixl::aarch32::lr));

// Callee saves FP registers s16 to s31 inclusive.
static const vixl::aarch32::SRegisterList kFpuCalleeSaves =
    vixl::aarch32::SRegisterList(vixl::aarch32::s16, 16);

static const vixl::aarch32::Register kRuntimeParameterCoreRegistersVIXL[] = {
    vixl::aarch32::r0,
    vixl::aarch32::r1,
    vixl::aarch32::r2,
    vixl::aarch32::r3
};
static const size_t kRuntimeParameterCoreRegistersLengthVIXL =
    arraysize(kRuntimeParameterCoreRegisters);
static const vixl::aarch32::SRegister kRuntimeParameterFpuRegistersVIXL[] = {
    vixl::aarch32::s0,
    vixl::aarch32::s1,
    vixl::aarch32::s2,
    vixl::aarch32::s3
};
static const size_t kRuntimeParameterFpuRegistersLengthVIXL =
    arraysize(kRuntimeParameterFpuRegisters);

class LoadClassSlowPathARMVIXL;

#define FOR_EACH_IMPLEMENTED_INSTRUCTION(M)     \
  M(Above)                                      \
  M(AboveOrEqual)                               \
  M(Add)                                        \
  M(ArrayLength)                                \
  M(Below)                                      \
  M(BelowOrEqual)                               \
  M(ClearException)                             \
  M(ClinitCheck)                                \
  M(Compare)                                    \
  M(CurrentMethod)                              \
  M(Div)                                        \
  M(DivZeroCheck)                               \
  M(Equal)                                      \
  M(Exit)                                       \
  M(Goto)                                       \
  M(GreaterThan)                                \
  M(GreaterThanOrEqual)                         \
  M(If)                                         \
  M(InstanceFieldGet)                           \
  M(InstanceFieldSet)                           \
  M(IntConstant)                                \
  M(InvokeStaticOrDirect)                       \
  M(InvokeVirtual)                              \
  M(LessThan)                                   \
  M(LessThanOrEqual)                            \
  M(LoadClass)                                  \
  M(LoadException)                              \
  M(LoadString)                                 \
  M(LongConstant)                               \
  M(MemoryBarrier)                              \
  M(Mul)                                        \
  M(NewArray)                                   \
  M(NewInstance)                                \
  M(Not)                                        \
  M(NotEqual)                                   \
  M(NullCheck)                                  \
  M(NullConstant)                               \
  M(ParallelMove)                               \
  M(ParameterValue)                             \
  M(Phi)                                        \
  M(Return)                                     \
  M(ReturnVoid)                                 \
  M(Select)                                     \
  M(StaticFieldGet)                             \
  M(Sub)                                        \
  M(SuspendCheck)                               \
  M(Throw)                                      \
  M(TryBoundary)                                \
  M(TypeConversion)                             \

// TODO: Remove once the VIXL32 backend is implemented completely.
#define FOR_EACH_UNIMPLEMENTED_INSTRUCTION(M)   \
  M(And)                                        \
  M(ArrayGet)                                   \
  M(ArraySet)                                   \
  M(BooleanNot)                                 \
  M(BoundsCheck)                                \
  M(BoundType)                                  \
  M(CheckCast)                                  \
  M(ClassTableGet)                              \
  M(Deoptimize)                                 \
  M(DoubleConstant)                             \
  M(FloatConstant)                              \
  M(InstanceOf)                                 \
  M(InvokeInterface)                            \
  M(InvokeUnresolved)                           \
  M(MonitorOperation)                           \
  M(NativeDebugInfo)                            \
  M(Neg)                                        \
  M(Or)                                         \
  M(PackedSwitch)                               \
  M(Rem)                                        \
  M(Ror)                                        \
  M(Shl)                                        \
  M(Shr)                                        \
  M(StaticFieldSet)                             \
  M(UnresolvedInstanceFieldGet)                 \
  M(UnresolvedInstanceFieldSet)                 \
  M(UnresolvedStaticFieldGet)                   \
  M(UnresolvedStaticFieldSet)                   \
  M(UShr)                                       \
  M(Xor)                                        \

class CodeGeneratorARMVIXL;

class InvokeRuntimeCallingConventionARMVIXL
    : public CallingConvention<vixl::aarch32::Register, vixl::aarch32::SRegister> {
 public:
  InvokeRuntimeCallingConventionARMVIXL()
      : CallingConvention(kRuntimeParameterCoreRegistersVIXL,
                          kRuntimeParameterCoreRegistersLengthVIXL,
                          kRuntimeParameterFpuRegistersVIXL,
                          kRuntimeParameterFpuRegistersLengthVIXL,
                          kArmPointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConventionARMVIXL);
};

class InvokeDexCallingConventionARMVIXL
    : public CallingConvention<vixl::aarch32::Register, vixl::aarch32::SRegister> {
 public:
  InvokeDexCallingConventionARMVIXL()
      : CallingConvention(kParameterCoreRegistersVIXL,
                          kParameterCoreRegistersLengthVIXL,
                          kParameterFpuRegistersVIXL,
                          kParameterFpuRegistersLengthVIXL,
                          kArmPointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionARMVIXL);
};

class SlowPathCodeARMVIXL : public SlowPathCode {
 public:
  explicit SlowPathCodeARMVIXL(HInstruction* instruction)
      : SlowPathCode(instruction), entry_label_(), exit_label_() {}

  vixl::aarch32::Label* GetEntryLabel() { return &entry_label_; }
  vixl::aarch32::Label* GetExitLabel() { return &exit_label_; }

  void SaveLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) OVERRIDE;
  void RestoreLiveRegisters(CodeGenerator* codegen, LocationSummary* locations) OVERRIDE;

 private:
  vixl::aarch32::Label entry_label_;
  vixl::aarch32::Label exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeARMVIXL);
};

class ParallelMoveResolverARMVIXL : public ParallelMoveResolverWithSwap {
 public:
  ParallelMoveResolverARMVIXL(ArenaAllocator* allocator, CodeGeneratorARMVIXL* codegen)
      : ParallelMoveResolverWithSwap(allocator), codegen_(codegen) {}

  void EmitMove(size_t index) OVERRIDE;
  void EmitSwap(size_t index) OVERRIDE;
  void SpillScratch(int reg) OVERRIDE;
  void RestoreScratch(int reg) OVERRIDE;

  ArmVIXLAssembler* GetAssembler() const;

 private:
  void Exchange(vixl32::Register reg, int mem);
  void Exchange(int mem1, int mem2);

  CodeGeneratorARMVIXL* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverARMVIXL);
};

#define DEFINE_IMPLEMENTED_INSTRUCTION_VISITOR(Name)            \
  void Visit##Name(H##Name*) OVERRIDE;

#define DEFINE_UNIMPLEMENTED_INSTRUCTION_VISITOR(Name)          \
  void Visit##Name(H##Name* instr) OVERRIDE {                   \
    VisitUnimplemementedInstruction(instr); }

class LocationsBuilderARMVIXL : public HGraphVisitor {
 public:
  LocationsBuilderARMVIXL(HGraph* graph, CodeGeneratorARMVIXL* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

  FOR_EACH_IMPLEMENTED_INSTRUCTION(DEFINE_IMPLEMENTED_INSTRUCTION_VISITOR)

  FOR_EACH_UNIMPLEMENTED_INSTRUCTION(DEFINE_UNIMPLEMENTED_INSTRUCTION_VISITOR)

 private:
  void VisitUnimplemementedInstruction(HInstruction* instruction) {
    LOG(FATAL) << "Unimplemented Instruction: " << instruction->DebugName();
  }

  void HandleInvoke(HInvoke* invoke);
  void HandleCondition(HCondition* condition);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

  Location ArithmeticZeroOrFpuRegister(HInstruction* input);

  CodeGeneratorARMVIXL* const codegen_;
  InvokeDexCallingConventionVisitorARM parameter_visitor_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderARMVIXL);
};

class InstructionCodeGeneratorARMVIXL : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorARMVIXL(HGraph* graph, CodeGeneratorARMVIXL* codegen);

  FOR_EACH_IMPLEMENTED_INSTRUCTION(DEFINE_IMPLEMENTED_INSTRUCTION_VISITOR)

  FOR_EACH_UNIMPLEMENTED_INSTRUCTION(DEFINE_UNIMPLEMENTED_INSTRUCTION_VISITOR)

  ArmVIXLAssembler* GetAssembler() const { return assembler_; }
  vixl::aarch32::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->GetVIXLAssembler(); }

 private:
  void VisitUnimplemementedInstruction(HInstruction* instruction) {
    LOG(FATAL) << "Unimplemented Instruction: " << instruction->DebugName();
  }

  // Generate code for the given suspend check. If not null, `successor`
  // is the block to branch to if the suspend check is not needed, and after
  // the suspend call.
  void GenerateSuspendCheck(HSuspendCheck* instruction, HBasicBlock* successor);
  void GenerateClassInitializationCheck(LoadClassSlowPathARMVIXL* slow_path,
                                        vixl32::Register class_reg);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);
  void HandleCondition(HCondition* condition);

  void GenerateWideAtomicStore(vixl::aarch32::Register addr,
                               uint32_t offset,
                               vixl::aarch32::Register value_lo,
                               vixl::aarch32::Register value_hi,
                               vixl::aarch32::Register temp1,
                               vixl::aarch32::Register temp2,
                               HInstruction* instruction);
  void GenerateWideAtomicLoad(vixl::aarch32::Register addr,
                              uint32_t offset,
                              vixl::aarch32::Register out_lo,
                              vixl::aarch32::Register out_hi);

  void HandleFieldSet(HInstruction* instruction,
                      const FieldInfo& field_info,
                      bool value_can_be_null);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers if `requires_read_barrier` is true.
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               vixl::aarch32::Register obj,
                               uint32_t offset,
                               bool requires_read_barrier = kEmitCompilerReadBarrier);
  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             vixl::aarch32::Label* true_target,
                             vixl::aarch32::Label* false_target);
  void GenerateCompareTestAndBranch(HCondition* condition,
                                    vixl::aarch32::Label* true_target,
                                    vixl::aarch32::Label* false_target);
  void GenerateVcmp(HInstruction* instruction);
  void GenerateFPJumps(HCondition* cond,
                       vixl::aarch32::Label* true_label,
                       vixl::aarch32::Label* false_label);
  void GenerateLongComparesAndJumps(HCondition* cond,
                                    vixl::aarch32::Label* true_label,
                                    vixl::aarch32::Label* false_label);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemConstantIntegral(HBinaryOperation* instruction);

  ArmVIXLAssembler* const assembler_;
  CodeGeneratorARMVIXL* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorARMVIXL);
};

class CodeGeneratorARMVIXL : public CodeGenerator {
 public:
  CodeGeneratorARMVIXL(HGraph* graph,
                       const ArmInstructionSetFeatures& isa_features,
                       const CompilerOptions& compiler_options,
                       OptimizingCompilerStats* stats = nullptr);

  virtual ~CodeGeneratorARMVIXL() {}

  void Initialize() OVERRIDE {
    block_labels_.resize(GetGraph()->GetBlocks().size());
  }

  void GenerateFrameEntry() OVERRIDE;
  void GenerateFrameExit() OVERRIDE;

  void Bind(HBasicBlock* block) OVERRIDE;

  vixl::aarch32::Label* GetLabelOf(HBasicBlock* block) {
    block = FirstNonEmptyBlock(block);
    return &(block_labels_[block->GetBlockId()]);
  }

  void MoveConstant(Location destination, int32_t value) OVERRIDE;
  void MoveLocation(Location dst, Location src, Primitive::Type dst_type) OVERRIDE;
  void AddLocationAsTemp(Location location, LocationSummary* locations) OVERRIDE;

  ArmVIXLAssembler* GetAssembler() OVERRIDE { return &assembler_; }

  const ArmVIXLAssembler& GetAssembler() const OVERRIDE { return assembler_; }

  vixl::aarch32::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->GetVIXLAssembler(); }

  size_t GetWordSize() const OVERRIDE { return kArmWordSize; }

  size_t GetFloatingPointSpillSlotSize() const OVERRIDE { return vixl::aarch32::kRegSizeInBytes; }

  uintptr_t GetAddressOf(HBasicBlock* block) OVERRIDE {
    vixl::aarch32::Label* block_entry_label = GetLabelOf(block);
    DCHECK(block_entry_label->IsBound());
    return block_entry_label->GetLocation();
  }

  HGraphVisitor* GetLocationBuilder() OVERRIDE { return &location_builder_; }

  HGraphVisitor* GetInstructionVisitor() OVERRIDE { return &instruction_visitor_; }

  void GenerateMemoryBarrier(MemBarrierKind kind);
  void Finalize(CodeAllocator* allocator) OVERRIDE;
  void SetupBlockedRegisters() const OVERRIDE;

  void DumpCoreRegister(std::ostream& stream, int reg) const OVERRIDE;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const OVERRIDE;

  InstructionSet GetInstructionSet() const OVERRIDE { return InstructionSet::kThumb2; }

  // Helper method to move a 32-bit value between two locations.
  void Move32(Location destination, Location source);

  const ArmInstructionSetFeatures& GetInstructionSetFeatures() const { return isa_features_; }

  vixl::aarch32::Label* GetFrameEntryLabel() { return &frame_entry_label_; }

  // Saves the register in the stack. Returns the size taken on stack.
  size_t SaveCoreRegister(size_t stack_index ATTRIBUTE_UNUSED,
                          uint32_t reg_id ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(INFO) << "TODO: SaveCoreRegister";
    return 0;
  }

  // Restores the register from the stack. Returns the size taken on stack.
  size_t RestoreCoreRegister(size_t stack_index ATTRIBUTE_UNUSED,
                             uint32_t reg_id ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(INFO) << "TODO: RestoreCoreRegister";
    return 0;
  }

  size_t SaveFloatingPointRegister(size_t stack_index ATTRIBUTE_UNUSED,
                                   uint32_t reg_id ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(INFO) << "TODO: SaveFloatingPointRegister";
    return 0;
  }

  size_t RestoreFloatingPointRegister(size_t stack_index ATTRIBUTE_UNUSED,
                                      uint32_t reg_id ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(INFO) << "TODO: RestoreFloatingPointRegister";
    return 0;
  }

  bool NeedsTwoRegisters(Primitive::Type type) const OVERRIDE {
    return type == Primitive::kPrimDouble || type == Primitive::kPrimLong;
  }

  void ComputeSpillMask() OVERRIDE;

  void GenerateImplicitNullCheck(HNullCheck* null_check) OVERRIDE;
  void GenerateExplicitNullCheck(HNullCheck* null_check) OVERRIDE;

  ParallelMoveResolver* GetMoveResolver() OVERRIDE {
    return &move_resolver_;
  }

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
  void MarkGCCard(vixl::aarch32::Register temp,
                  vixl::aarch32::Register card,
                  vixl::aarch32::Register object,
                  vixl::aarch32::Register value,
                  bool can_be_null);

  // If read barriers are enabled, generate a read barrier for a heap
  // reference using a slow path. If heap poisoning is enabled, also
  // unpoison the reference in `out`.
  void MaybeGenerateReadBarrierSlow(HInstruction* instruction,
                                    Location out,
                                    Location ref,
                                    Location obj,
                                    uint32_t offset,
                                    Location index = Location::NoLocation());

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

  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, Location temp) OVERRIDE;
  void GenerateVirtualCall(HInvokeVirtual* invoke, Location temp) OVERRIDE;

  void MoveFromReturnRegister(Location trg, Primitive::Type type) OVERRIDE;

  void GenerateNop() OVERRIDE;

 private:
  vixl::aarch32::Register GetInvokeStaticOrDirectExtraParameter(HInvokeStaticOrDirect* invoke,
                                                                vixl::aarch32::Register temp);

  // Labels for each block that will be compiled.
  // We use a deque so that the `vixl::aarch32::Label` objects do not move in memory.
  ArenaDeque<vixl::aarch32::Label> block_labels_;  // Indexed by block id.
  vixl::aarch32::Label frame_entry_label_;

  LocationsBuilderARMVIXL location_builder_;
  InstructionCodeGeneratorARMVIXL instruction_visitor_;
  ParallelMoveResolverARMVIXL move_resolver_;

  ArmVIXLAssembler assembler_;
  const ArmInstructionSetFeatures& isa_features_;

  DISALLOW_COPY_AND_ASSIGN(CodeGeneratorARMVIXL);
};

#undef FOR_EACH_IMPLEMENTED_INSTRUCTION
#undef FOR_EACH_UNIMPLEMENTED_INSTRUCTION
#undef DEFINE_IMPLEMENTED_INSTRUCTION_VISITOR
#undef DEFINE_UNIMPLEMENTED_INSTRUCTION_VISITOR


}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM_VIXL_H_
