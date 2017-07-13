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
#include "bytecode_utils.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "quicken_info.h"

namespace art {
namespace optimizer {

class DexDecompiler {
 public:
  DexDecompiler(const DexFile::CodeItem& code_item,
                const ArrayRef<const uint8_t>& quickened_info,
                bool decompile_return_instruction)
    : code_item_(code_item),
      quicken_info_(quickened_info.data()),
      quicken_info_number_of_indices_(QuickenInfoTable::NumberOfIndices(quickened_info.size())),
      decompile_return_instruction_(decompile_return_instruction) {}

  bool Decompile();

 private:
  void DecompileInstanceFieldAccess(Instruction* inst, Instruction::Code new_opcode) {
    uint16_t index = NextIndex();
    inst->SetOpcode(new_opcode);
    inst->SetVRegC_22c(index);
  }

  void DecompileInvokeVirtual(Instruction* inst, Instruction::Code new_opcode, bool is_range) {
    const uint16_t index = NextIndex();
    inst->SetOpcode(new_opcode);
    if (is_range) {
      inst->SetVRegB_3rc(index);
    } else {
      inst->SetVRegB_35c(index);
    }
  }

  void DecompileNop(Instruction* inst) {
    const uint16_t reference_index = NextIndex();
    if (reference_index == DexFile::kDexNoIndex16) {
      // This means it was a normal nop and not a check-cast.
      return;
    }
    const uint16_t type_index = NextIndex();
    inst->SetOpcode(Instruction::CHECK_CAST);
    inst->SetVRegA_21c(reference_index);
    inst->SetVRegB_21c(type_index);
  }

  uint16_t NextIndex() {
    DCHECK_LT(quicken_index_, quicken_info_number_of_indices_);
    const uint16_t ret = quicken_info_.GetData(quicken_index_);
    quicken_index_++;
    return ret;
  }

  const DexFile::CodeItem& code_item_;
  const QuickenInfoTable quicken_info_;
  const size_t quicken_info_number_of_indices_;
  const bool decompile_return_instruction_;

  size_t quicken_index_ = 0u;

  DISALLOW_COPY_AND_ASSIGN(DexDecompiler);
};

bool DexDecompiler::Decompile() {
  // We need to iterate over the code item, and not over the quickening data,
  // because the RETURN_VOID quickening is not encoded in the quickening data. Because
  // unquickening is a rare need and not performance sensitive, it is not worth the
  // added storage to also add the RETURN_VOID quickening in the quickened data.
  for (CodeItemIterator it(code_item_); !it.Done(); it.Advance()) {
    Instruction* inst = const_cast<Instruction*>(&it.CurrentInstruction());

    switch (inst->Opcode()) {
      case Instruction::RETURN_VOID_NO_BARRIER:
        if (decompile_return_instruction_) {
          inst->SetOpcode(Instruction::RETURN_VOID);
        }
        break;

      case Instruction::NOP:
        if (quicken_info_number_of_indices_ > 0) {
          // Only try to decompile NOP if there are more than 0 indices. Not having
          // any index happens when we unquicken a code item that only has
          // RETURN_VOID_NO_BARRIER as quickened instruction.
          DecompileNop(inst);
        }
        break;

      case Instruction::IGET_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET);
        break;

      case Instruction::IGET_WIDE_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_WIDE);
        break;

      case Instruction::IGET_OBJECT_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_OBJECT);
        break;

      case Instruction::IGET_BOOLEAN_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_BOOLEAN);
        break;

      case Instruction::IGET_BYTE_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_BYTE);
        break;

      case Instruction::IGET_CHAR_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_CHAR);
        break;

      case Instruction::IGET_SHORT_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IGET_SHORT);
        break;

      case Instruction::IPUT_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT);
        break;

      case Instruction::IPUT_BOOLEAN_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_BOOLEAN);
        break;

      case Instruction::IPUT_BYTE_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_BYTE);
        break;

      case Instruction::IPUT_CHAR_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_CHAR);
        break;

      case Instruction::IPUT_SHORT_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_SHORT);
        break;

      case Instruction::IPUT_WIDE_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_WIDE);
        break;

      case Instruction::IPUT_OBJECT_QUICK:
        DecompileInstanceFieldAccess(inst, Instruction::IPUT_OBJECT);
        break;

      case Instruction::INVOKE_VIRTUAL_QUICK:
        DecompileInvokeVirtual(inst, Instruction::INVOKE_VIRTUAL, false);
        break;

      case Instruction::INVOKE_VIRTUAL_RANGE_QUICK:
        DecompileInvokeVirtual(inst, Instruction::INVOKE_VIRTUAL_RANGE, true);
        break;

      default:
        break;
    }
  }

  if (quicken_index_ != quicken_info_number_of_indices_) {
    if (quicken_index_ == 0) {
      LOG(WARNING) << "Failed to use any value in quickening info,"
                   << " potentially due to duplicate methods.";
    } else {
      LOG(FATAL) << "Failed to use all values in quickening info."
                 << " Actual: " << std::hex << quicken_index_
                 << " Expected: " << quicken_info_number_of_indices_;
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
