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

#include "pc_relative_fixups_mips.h"
#include "code_generator_mips.h"
#include "intrinsics_mips.h"

namespace art {
namespace mips {

/**
 * Finds instructions that need the constant area base as an input.
 */
class PCRelativeHandlerVisitor : public HGraphVisitor {
 public:
  PCRelativeHandlerVisitor(HGraph* graph, CodeGenerator* codegen)
      : HGraphVisitor(graph),
        codegen_(down_cast<CodeGeneratorMIPS*>(codegen)),
        base_(nullptr) {}

  void MoveBaseIfNeeded() {
    if (base_ != nullptr) {
      // Bring the base closer to the first use (previously, it was in the
      // entry block) and relieve some pressure on the register allocator
      // while avoiding recalculation of the base in a loop.
      base_->MoveBeforeFirstUserAndOutOfLoops();
      // Computing the base for PC-relative literals will clobber RA with
      // the NAL instruction on R2. Take a note of this before generating
      // the method entry.
      codegen_->ClobberRA();
    }
  }

 private:
  void VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) OVERRIDE {
    HandleInvoke(invoke);
  }

  void InitializePCRelativeBasePointer() {
    // Ensure we only initialize the pointer once.
    if (base_ != nullptr) {
      return;
    }
    // Insert the base at the start of the entry block, move it to a better
    // position later in MoveBaseIfNeeded().
    base_ = new (GetGraph()->GetArena()) HMipsComputeBaseMethodAddress();
    HBasicBlock* entry_block = GetGraph()->GetEntryBlock();
    entry_block->InsertInstructionBefore(base_, entry_block->GetFirstInstruction());
    DCHECK(base_ != nullptr);
  }

  void VisitLoadClass(HLoadClass* load_class) OVERRIDE {
    HLoadClass::LoadKind load_kind = load_class->GetLoadKind();
    switch (load_kind) {
      case HLoadClass::LoadKind::kBootImageLinkTimeAddress:
      case HLoadClass::LoadKind::kBootImageAddress:
      case HLoadClass::LoadKind::kBootImageLinkTimePcRelative:
        // Add a base register for PC-relative literals on R2.
        InitializePCRelativeBasePointer();
        load_class->AddSpecialInput(base_);
        break;
      default:
        break;
    }
  }

  void VisitLoadString(HLoadString* load_string) OVERRIDE {
    HLoadString::LoadKind load_kind = load_string->GetLoadKind();
    switch (load_kind) {
      case HLoadString::LoadKind::kBootImageLinkTimeAddress:
      case HLoadString::LoadKind::kBootImageAddress:
      case HLoadString::LoadKind::kBootImageLinkTimePcRelative:
        // Add a base register for PC-relative literals on R2.
        InitializePCRelativeBasePointer();
        load_string->AddSpecialInput(base_);
        break;
      default:
        break;
    }
  }

  void HandleInvoke(HInvoke* invoke) {
    // If this is an invoke-static/-direct with PC-relative dex cache array
    // addressing, we need the PC-relative address base.
    HInvokeStaticOrDirect* invoke_static_or_direct = invoke->AsInvokeStaticOrDirect();
    if (invoke_static_or_direct != nullptr) {
      HInvokeStaticOrDirect::MethodLoadKind method_load_kind =
          invoke_static_or_direct->GetMethodLoadKind();
      HInvokeStaticOrDirect::CodePtrLocation code_ptr_location =
          invoke_static_or_direct->GetCodePtrLocation();

      bool has_extra_input =
          (method_load_kind == HInvokeStaticOrDirect::MethodLoadKind::kDirectAddressWithFixup) ||
          (code_ptr_location == HInvokeStaticOrDirect::CodePtrLocation::kCallDirectWithFixup);

      // We can't add a pointer to the constant area if we already have a current
      // method pointer. This may arise when sharpening doesn't remove the current
      // method pointer from the invoke.
      if (invoke_static_or_direct->HasCurrentMethodInput()) {
        DCHECK(!invoke_static_or_direct->HasPcRelativeDexCache());
        CHECK(!has_extra_input);
        return;
      }

      if (has_extra_input &&
          !IsCallFreeIntrinsic<IntrinsicLocationsBuilderMIPS>(invoke, codegen_)) {
        InitializePCRelativeBasePointer();
        // Add the extra parameter base_.
        invoke_static_or_direct->AddSpecialInput(base_);
      }
    }
  }

  CodeGeneratorMIPS* codegen_;

  // The generated HMipsComputeBaseMethodAddress in the entry block needed as an
  // input to the HMipsLoadFromConstantTable instructions.
  HMipsComputeBaseMethodAddress* base_;
};

void PcRelativeFixups::Run() {
  CodeGeneratorMIPS* mips_codegen = down_cast<CodeGeneratorMIPS*>(codegen_);
  if (mips_codegen->GetInstructionSetFeatures().IsR6()) {
    // Do nothing for R6 because it has PC-relative addressing.
    return;
  }
  if (graph_->HasIrreducibleLoops()) {
    // Do not run this optimization, as irreducible loops do not work with an instruction
    // that can be live-in at the irreducible loop header.
    return;
  }
  PCRelativeHandlerVisitor visitor(graph_, codegen_);
  visitor.VisitInsertionOrder();
  visitor.MoveBaseIfNeeded();
}

}  // namespace mips
}  // namespace art
