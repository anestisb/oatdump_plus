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

#include "code_generator_mips.h"
#include "dex_cache_array_fixups_mips.h"

#include "base/arena_containers.h"
#include "utils/dex_cache_arrays_layout-inl.h"

namespace art {
namespace mips {

/**
 * Finds instructions that need the dex cache arrays base as an input.
 */
class DexCacheArrayFixupsVisitor : public HGraphVisitor {
 public:
  explicit DexCacheArrayFixupsVisitor(HGraph* graph, CodeGenerator* codegen)
      : HGraphVisitor(graph),
        codegen_(down_cast<CodeGeneratorMIPS*>(codegen)),
        dex_cache_array_bases_(std::less<const DexFile*>(),
                               // Attribute memory use to code generator.
                               graph->GetArena()->Adapter(kArenaAllocCodeGenerator)) {}

  void MoveBasesIfNeeded() {
    for (const auto& entry : dex_cache_array_bases_) {
      // Bring the base closer to the first use (previously, it was in the
      // entry block) and relieve some pressure on the register allocator
      // while avoiding recalculation of the base in a loop.
      HMipsDexCacheArraysBase* base = entry.second;
      base->MoveBeforeFirstUserAndOutOfLoops();
    }
    // Computing the dex cache base for PC-relative accesses will clobber RA with
    // the NAL instruction on R2. Take a note of this before generating the method
    // entry.
    if (!dex_cache_array_bases_.empty() && !codegen_->GetInstructionSetFeatures().IsR6()) {
      codegen_->ClobberRA();
    }
  }

 private:
  void VisitLoadClass(HLoadClass* load_class) OVERRIDE {
    // If this is a load with PC-relative access to the dex cache types array,
    // we need to add the dex cache arrays base as the special input.
    if (load_class->GetLoadKind() == HLoadClass::LoadKind::kDexCachePcRelative) {
      // Initialize base for target dex file if needed.
      const DexFile& dex_file = load_class->GetDexFile();
      HMipsDexCacheArraysBase* base = GetOrCreateDexCacheArrayBase(dex_file);
      // Update the element offset in base.
      DexCacheArraysLayout layout(kMipsPointerSize, &dex_file);
      base->UpdateElementOffset(layout.TypeOffset(load_class->GetTypeIndex()));
      // Add the special argument base to the load.
      load_class->AddSpecialInput(base);
    }
  }

  void VisitLoadString(HLoadString* load_string) OVERRIDE {
    // If this is a load with PC-relative access to the dex cache strings array,
    // we need to add the dex cache arrays base as the special input.
    if (load_string->GetLoadKind() == HLoadString::LoadKind::kDexCachePcRelative) {
      // Initialize base for target dex file if needed.
      const DexFile& dex_file = load_string->GetDexFile();
      HMipsDexCacheArraysBase* base = GetOrCreateDexCacheArrayBase(dex_file);
      // Update the element offset in base.
      DexCacheArraysLayout layout(kMipsPointerSize, &dex_file);
      base->UpdateElementOffset(layout.StringOffset(load_string->GetStringIndex()));
      // Add the special argument base to the load.
      load_string->AddSpecialInput(base);
    }
  }

  void VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke) OVERRIDE {
    // If this is an invoke with PC-relative access to the dex cache methods array,
    // we need to add the dex cache arrays base as the special input.
    if (invoke->HasPcRelativeDexCache()) {
      // Initialize base for target method dex file if needed.
      MethodReference target_method = invoke->GetTargetMethod();
      HMipsDexCacheArraysBase* base = GetOrCreateDexCacheArrayBase(*target_method.dex_file);
      // Update the element offset in base.
      DexCacheArraysLayout layout(kMipsPointerSize, target_method.dex_file);
      base->UpdateElementOffset(layout.MethodOffset(target_method.dex_method_index));
      // Add the special argument base to the method.
      DCHECK(!invoke->HasCurrentMethodInput());
      invoke->AddSpecialInput(base);
    }
  }

  HMipsDexCacheArraysBase* GetOrCreateDexCacheArrayBase(const DexFile& dex_file) {
    return dex_cache_array_bases_.GetOrCreate(
        &dex_file,
        [this, &dex_file]() {
          HMipsDexCacheArraysBase* base =
              new (GetGraph()->GetArena()) HMipsDexCacheArraysBase(dex_file);
          HBasicBlock* entry_block = GetGraph()->GetEntryBlock();
          // Insert the base at the start of the entry block, move it to a better
          // position later in MoveBaseIfNeeded().
          entry_block->InsertInstructionBefore(base, entry_block->GetFirstInstruction());
          return base;
        });
  }

  CodeGeneratorMIPS* codegen_;

  using DexCacheArraysBaseMap =
      ArenaSafeMap<const DexFile*, HMipsDexCacheArraysBase*, std::less<const DexFile*>>;
  DexCacheArraysBaseMap dex_cache_array_bases_;
};

void DexCacheArrayFixups::Run() {
  if (graph_->HasIrreducibleLoops()) {
    // Do not run this optimization, as irreducible loops do not work with an instruction
    // that can be live-in at the irreducible loop header.
    return;
  }
  DexCacheArrayFixupsVisitor visitor(graph_, codegen_);
  visitor.VisitInsertionOrder();
  visitor.MoveBasesIfNeeded();
}

}  // namespace mips
}  // namespace art
