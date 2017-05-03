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

#include "dex_to_dex_decompiler.h"

#include "base/logging.h"
#include "base/mutex.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "bytecode_utils.h"

namespace art {
namespace optimizer {

class DexDecompiler {
 public:
  DexDecompiler(const DexFile::CodeItem& code_item,
                const ArrayRef<const uint8_t>& quickened_info,
                bool decompile_return_instruction)
    : code_item_(code_item),
      quickened_info_ptr_(quickened_info.data()),
      quickened_info_start_(quickened_info.data()),
      quickened_info_end_(quickened_info.data() + quickened_info.size()),
      decompile_return_instruction_(decompile_return_instruction) {}

  bool Decompile();

 private:
  void DecompileInstanceFieldAccess(Instruction* inst,
                                    uint32_t dex_pc,
                                    Instruction::Code new_opcode) {
    uint16_t index = GetIndexAt(dex_pc);
    inst->SetOpcode(new_opcode);
    inst->SetVRegC_22c(index);
  }

  void DecompileInvokeVirtual(Instruction* inst,
                              uint32_t dex_pc,
                              Instruction::Code new_opcode,
                              bool is_range) {
    uint16_t index = GetIndexAt(dex_pc);
    inst->SetOpcode(new_opcode);
    if (is_range) {
      inst->SetVRegB_3rc(index);
    } else {
      inst->SetVRegB_35c(index);
    }
  }

  void DecompileNop(Instruction* inst, uint32_t dex_pc) {
    if (quickened_info_ptr_ == quickened_info_end_) {
      return;
    }
    const uint8_t* temporary_pointer = quickened_info_ptr_;
    uint32_t quickened_pc = DecodeUnsignedLeb128(&temporary_pointer);
    if (quickened_pc != dex_pc) {
      return;
    }
    uint16_t reference_index = GetIndexAt(dex_pc);
    uint16_t type_index = GetIndexAt(dex_pc);
    inst->SetOpcode(Instruction::CHECK_CAST);
    inst->SetVRegA_21c(reference_index);
    inst->SetVRegB_21c(type_index);
  }

  uint16_t GetIndexAt(uint32_t dex_pc) {
    // Note that as a side effect, DecodeUnsignedLeb128 update the given pointer
    // to the new position in the buffer.
    DCHECK_LT(quickened_info_ptr_, quickened_info_end_);
    uint32_t quickened_pc = DecodeUnsignedLeb128(&quickened_info_ptr_);
    DCHECK_LT(quickened_info_ptr_, quickened_info_end_);
    uint16_t index = DecodeUnsignedLeb128(&quickened_info_ptr_);
    DCHECK_LE(quickened_info_ptr_, quickened_info_end_);
    DCHECK_EQ(quickened_pc, dex_pc);
    return index;
  }

  const DexFile::CodeItem& code_item_;
  const uint8_t* quickened_info_ptr_;
  const uint8_t* const quickened_info_start_;
  const uint8_t* const quickened_info_end_;
  const bool decompile_return_instruction_;

  DISALLOW_COPY_AND_ASSIGN(DexDecompiler);
};

bool DexDecompiler::Decompile() {
  // We need to iterate over the code item, and not over the quickening data,
  // because the RETURN_VOID quickening is not encoded in the quickening data. Because
  // unquickening is a rare need and not performance sensitive, it is not worth the
  // added storage to also add the RETURN_VOID quickening in the quickened data.
  for (CodeItemIterator it(code_item_); !it.Done(); it.Advance()) {
    uint32_t dex_pc = it.CurrentDexPc();
    Instruction* inst = const_cast<Instruction*>(&it.CurrentInstruction());

    switch (inst->Opcode()) {
      case Instruction::RETURN_VOID_NO_BARRIER:
        if (decompile_return_instruction_) {
          inst->SetOpcode(Instruction::RETURN_VOID);
        }
        break;

      case Instruction::NOP:
        DecompileNop(inst, dex_pc);
        break;

      case Instruction::IGET_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET);
        break;

      case Instruction::IGET_WIDE_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_WIDE);
        break;

      case Instruction::IGET_OBJECT_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_OBJECT);
        break;

      case Instruction::IGET_BOOLEAN_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_BOOLEAN);
        break;

      case Instruction::IGET_BYTE_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_BYTE);
        break;

      case Instruction::IGET_CHAR_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_CHAR);
        break;

      case Instruction::IGET_SHORT_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IGET_SHORT);
        break;

      case Instruction::IPUT_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT);
        break;

      case Instruction::IPUT_BOOLEAN_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_BOOLEAN);
        break;

      case Instruction::IPUT_BYTE_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_BYTE);
        break;

      case Instruction::IPUT_CHAR_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_CHAR);
        break;

      case Instruction::IPUT_SHORT_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_SHORT);
        break;

      case Instruction::IPUT_WIDE_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_WIDE);
        break;

      case Instruction::IPUT_OBJECT_QUICK:
        DecompileInstanceFieldAccess(inst, dex_pc, Instruction::IPUT_OBJECT);
        break;

      case Instruction::INVOKE_VIRTUAL_QUICK:
        DecompileInvokeVirtual(inst, dex_pc, Instruction::INVOKE_VIRTUAL, false);
        break;

      case Instruction::INVOKE_VIRTUAL_RANGE_QUICK:
        DecompileInvokeVirtual(inst, dex_pc, Instruction::INVOKE_VIRTUAL_RANGE, true);
        break;

      default:
        break;
    }
  }

  if (quickened_info_ptr_ != quickened_info_end_) {
    if (quickened_info_start_ == quickened_info_ptr_) {
      LOG(WARNING) << "Failed to use any value in quickening info,"
                   << " potentially due to duplicate methods.";
    } else {
      LOG(FATAL) << "Failed to use all values in quickening info."
                 << " Actual: " << std::hex << reinterpret_cast<uintptr_t>(quickened_info_ptr_)
                 << " Expected: " << reinterpret_cast<uintptr_t>(quickened_info_end_);
      return false;
    }
  }

  return true;
}

bool ArtDecompileDEX(const DexFile::CodeItem& code_item,
                     const ArrayRef<const uint8_t>& quickened_info,
                     bool decompile_return_instruction) {
  if (quickened_info.size() == 0 && !decompile_return_instruction) {
    return true;
  }
  DexDecompiler decompiler(code_item, quickened_info, decompile_return_instruction);
  return decompiler.Decompile();
}

}  // namespace optimizer
}  // namespace art
