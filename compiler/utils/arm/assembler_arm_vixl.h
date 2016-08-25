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

#ifndef ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_VIXL_H_
#define ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_VIXL_H_

#include "base/arena_containers.h"
#include "base/logging.h"
#include "constants_arm.h"
#include "offsets.h"
#include "utils/arm/assembler_arm_shared.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/assembler.h"
#include "utils/jni_macro_assembler.h"

// TODO(VIXL): Make VIXL compile with -Wshadow and remove pragmas.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch32/macro-assembler-aarch32.h"
#pragma GCC diagnostic pop

namespace vixl32 = vixl::aarch32;

namespace art {
namespace arm {

class ArmVIXLAssembler FINAL : public Assembler {
 private:
  class ArmException;
 public:
  explicit ArmVIXLAssembler(ArenaAllocator* arena)
      : Assembler(arena) {
    // Use Thumb2 instruction set.
    vixl_masm_.UseT32();
  }

  virtual ~ArmVIXLAssembler() {}
  vixl32::MacroAssembler* GetVIXLAssembler() { return &vixl_masm_; }
  void FinalizeCode() OVERRIDE;

  // Size of generated code.
  size_t CodeSize() const OVERRIDE;
  const uint8_t* CodeBufferBaseAddress() const OVERRIDE;

  // Copy instructions out of assembly buffer into the given region of memory.
  void FinalizeInstructions(const MemoryRegion& region) OVERRIDE;

  void Bind(Label* label ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(FATAL) << "Do not use Bind for ARM";
  }
  void Jump(Label* label ATTRIBUTE_UNUSED) OVERRIDE {
    UNIMPLEMENTED(FATAL) << "Do not use Jump for ARM";
  }

  //
  // Heap poisoning.
  //
  // Poison a heap reference contained in `reg`.
  void PoisonHeapReference(vixl32::Register reg);
  // Unpoison a heap reference contained in `reg`.
  void UnpoisonHeapReference(vixl32::Register reg);
  // Unpoison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybeUnpoisonHeapReference(vixl32::Register reg);

  void StoreToOffset(StoreOperandType type,
                     vixl32::Register reg,
                     vixl32::Register base,
                     int32_t offset);
  void StoreSToOffset(vixl32::SRegister source, vixl32::Register base, int32_t offset);
  void StoreDToOffset(vixl32::DRegister source, vixl32::Register base, int32_t offset);

  void LoadImmediate(vixl32::Register dest, int32_t value);
  void LoadFromOffset(LoadOperandType type,
                      vixl32::Register reg,
                      vixl32::Register base,
                      int32_t offset);
  void LoadSFromOffset(vixl32::SRegister reg, vixl32::Register base, int32_t offset);
  void LoadDFromOffset(vixl32::DRegister reg, vixl32::Register base, int32_t offset);

  bool ShifterOperandCanAlwaysHold(uint32_t immediate);
  bool ShifterOperandCanHold(Opcode opcode, uint32_t immediate, SetCc set_cc);
  bool CanSplitLoadStoreOffset(int32_t allowed_offset_bits,
                               int32_t offset,
                               /*out*/ int32_t* add_to_base,
                               /*out*/ int32_t* offset_for_load_store);
  int32_t AdjustLoadStoreOffset(int32_t allowed_offset_bits,
                                vixl32::Register temp,
                                vixl32::Register base,
                                int32_t offset);
  int32_t GetAllowedLoadOffsetBits(LoadOperandType type);
  int32_t GetAllowedStoreOffsetBits(StoreOperandType type);

  void AddConstant(vixl32::Register rd, int32_t value);
  void AddConstant(vixl32::Register rd, vixl32::Register rn, int32_t value);
  void AddConstantInIt(vixl32::Register rd,
                       vixl32::Register rn,
                       int32_t value,
                       vixl32::Condition cond = vixl32::al);

 private:
  // VIXL assembler.
  vixl32::MacroAssembler vixl_masm_;
};

// Thread register declaration.
extern const vixl32::Register tr;

}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_VIXL_H_
