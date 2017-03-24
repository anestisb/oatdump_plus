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

#include "sharpening.h"

#include "base/casts.h"
#include "base/enums.h"
#include "class_linker.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "driver/dex_compilation_unit.h"
#include "utils/dex_cache_arrays_layout-inl.h"
#include "driver/compiler_driver.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "handle_scope-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/string.h"
#include "nodes.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

void HSharpening::Run() {
  // We don't care about the order of the blocks here.
  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (instruction->IsInvokeStaticOrDirect()) {
        SharpenInvokeStaticOrDirect(instruction->AsInvokeStaticOrDirect(), codegen_);
      } else if (instruction->IsLoadString()) {
        ProcessLoadString(instruction->AsLoadString());
      }
      // TODO: Move the sharpening of invoke-virtual/-interface/-super from HGraphBuilder
      //       here. Rewrite it to avoid the CompilerDriver's reliance on verifier data
      //       because we know the type better when inlining.
    }
  }
}

static bool IsInBootImage(ArtMethod* method) {
  const std::vector<gc::space::ImageSpace*>& image_spaces =
      Runtime::Current()->GetHeap()->GetBootImageSpaces();
  for (gc::space::ImageSpace* image_space : image_spaces) {
    const auto& method_section = image_space->GetImageHeader().GetMethodsSection();
    if (method_section.Contains(reinterpret_cast<uint8_t*>(method) - image_space->Begin())) {
      return true;
    }
  }
  return false;
}

static bool AOTCanEmbedMethod(ArtMethod* method, const CompilerOptions& options) {
  return IsInBootImage(method) && !options.GetCompilePic();
}


void HSharpening::SharpenInvokeStaticOrDirect(HInvokeStaticOrDirect* invoke,
                                              CodeGenerator* codegen) {
  if (invoke->IsStringInit()) {
    // Not using the dex cache arrays. But we could still try to use a better dispatch...
    // TODO: Use direct_method and direct_code for the appropriate StringFactory method.
    return;
  }

  ArtMethod* callee = invoke->GetResolvedMethod();
  DCHECK(callee != nullptr);

  HInvokeStaticOrDirect::MethodLoadKind method_load_kind;
  HInvokeStaticOrDirect::CodePtrLocation code_ptr_location;
  uint64_t method_load_data = 0u;

  // Note: we never call an ArtMethod through a known code pointer, as
  // we do not want to keep on invoking it if it gets deoptimized. This
  // applies to both AOT and JIT.
  // This also avoids having to find out if the code pointer of an ArtMethod
  // is the resolution trampoline (for ensuring the class is initialized), or
  // the interpreter entrypoint. Such code pointers we do not want to call
  // directly.
  // Only in the case of a recursive call can we call directly, as we know the
  // class is initialized already or being initialized, and the call will not
  // be invoked once the method is deoptimized.

  // We don't optimize for debuggable as it would prevent us from obsoleting the method in some
  // situations.
  if (callee == codegen->GetGraph()->GetArtMethod() && !codegen->GetGraph()->IsDebuggable()) {
    // Recursive call.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kRecursive;
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallSelf;
  } else if (Runtime::Current()->UseJitCompilation() ||
      AOTCanEmbedMethod(callee, codegen->GetCompilerOptions())) {
    // JIT or on-device AOT compilation referencing a boot image method.
    // Use the method address directly.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kDirectAddress;
    method_load_data = reinterpret_cast<uintptr_t>(callee);
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  } else {
    // Use PC-relative access to the dex cache arrays.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kDexCachePcRelative;
    // Note: we use the invoke's graph instead of the codegen graph, which are
    // different when inlining (the codegen graph is the most outer graph). The
    // invoke's dex method index is relative to the dex file where the invoke's graph
    // was built from.
    DexCacheArraysLayout layout(GetInstructionSetPointerSize(codegen->GetInstructionSet()),
                                &invoke->GetBlock()->GetGraph()->GetDexFile());
    method_load_data = layout.MethodOffset(invoke->GetDexMethodIndex());
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  }

  if (codegen->GetGraph()->IsDebuggable()) {
    // For debuggable apps always use the code pointer from ArtMethod
    // so that we don't circumvent instrumentation stubs if installed.
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  }

  HInvokeStaticOrDirect::DispatchInfo desired_dispatch_info = {
      method_load_kind, code_ptr_location, method_load_data
  };
  HInvokeStaticOrDirect::DispatchInfo dispatch_info =
      codegen->GetSupportedInvokeStaticOrDirectDispatch(desired_dispatch_info, invoke);
  invoke->SetDispatchInfo(dispatch_info);
}

HLoadClass::LoadKind HSharpening::ComputeLoadClassKind(HLoadClass* load_class,
                                                       CodeGenerator* codegen,
                                                       CompilerDriver* compiler_driver,
                                                       const DexCompilationUnit& dex_compilation_unit) {
  Handle<mirror::Class> klass = load_class->GetClass();
  DCHECK(load_class->GetLoadKind() == HLoadClass::LoadKind::kDexCacheViaMethod ||
         load_class->GetLoadKind() == HLoadClass::LoadKind::kReferrersClass)
      << load_class->GetLoadKind();
  DCHECK(!load_class->IsInBootImage()) << "HLoadClass should not be optimized before sharpening.";

  HLoadClass::LoadKind load_kind = load_class->GetLoadKind();

  if (load_class->NeedsAccessCheck()) {
    // We need to call the runtime anyway, so we simply get the class as that call's return value.
  } else if (load_kind == HLoadClass::LoadKind::kReferrersClass) {
    // Loading from the ArtMethod* is the most efficient retrieval in code size.
    // TODO: This may not actually be true for all architectures and
    // locations of target classes. The additional register pressure
    // for using the ArtMethod* should be considered.
  } else {
    const DexFile& dex_file = load_class->GetDexFile();
    dex::TypeIndex type_index = load_class->GetTypeIndex();

    bool is_in_boot_image = false;
    HLoadClass::LoadKind desired_load_kind = HLoadClass::LoadKind::kInvalid;
    Runtime* runtime = Runtime::Current();
    if (codegen->GetCompilerOptions().IsBootImage()) {
      // Compiling boot image. Check if the class is a boot image class.
      DCHECK(!runtime->UseJitCompilation());
      if (!compiler_driver->GetSupportBootImageFixup()) {
        // compiler_driver_test. Do not sharpen.
        desired_load_kind = HLoadClass::LoadKind::kDexCacheViaMethod;
      } else if ((klass != nullptr) && compiler_driver->IsImageClass(
          dex_file.StringDataByIdx(dex_file.GetTypeId(type_index).descriptor_idx_))) {
        is_in_boot_image = true;
        desired_load_kind = codegen->GetCompilerOptions().GetCompilePic()
            ? HLoadClass::LoadKind::kBootImageLinkTimePcRelative
            : HLoadClass::LoadKind::kBootImageLinkTimeAddress;
      } else {
        // Not a boot image class.
        DCHECK(ContainsElement(compiler_driver->GetDexFilesForOatFile(), &dex_file));
        desired_load_kind = HLoadClass::LoadKind::kBssEntry;
      }
    } else {
      is_in_boot_image = (klass != nullptr) &&
          runtime->GetHeap()->ObjectIsInBootImageSpace(klass.Get());
      if (runtime->UseJitCompilation()) {
        // TODO: Make sure we don't set the "compile PIC" flag for JIT as that's bogus.
        // DCHECK(!codegen_->GetCompilerOptions().GetCompilePic());
        if (is_in_boot_image) {
          // TODO: Use direct pointers for all non-moving spaces, not just boot image. Bug: 29530787
          desired_load_kind = HLoadClass::LoadKind::kBootImageAddress;
        } else if (klass != nullptr) {
          desired_load_kind = HLoadClass::LoadKind::kJitTableAddress;
        } else {
          // Class not loaded yet. This happens when the dex code requesting
          // this `HLoadClass` hasn't been executed in the interpreter.
          // Fallback to the dex cache.
          // TODO(ngeoffray): Generate HDeoptimize instead.
          desired_load_kind = HLoadClass::LoadKind::kDexCacheViaMethod;
        }
      } else if (is_in_boot_image && !codegen->GetCompilerOptions().GetCompilePic()) {
        // AOT app compilation. Check if the class is in the boot image.
        desired_load_kind = HLoadClass::LoadKind::kBootImageAddress;
      } else {
        // Not JIT and either the klass is not in boot image or we are compiling in PIC mode.
        desired_load_kind = HLoadClass::LoadKind::kBssEntry;
      }
    }
    DCHECK_NE(desired_load_kind, HLoadClass::LoadKind::kInvalid);

    if (is_in_boot_image) {
      load_class->MarkInBootImage();
    }
    load_kind = codegen->GetSupportedLoadClassKind(desired_load_kind);
  }

  if (!IsSameDexFile(load_class->GetDexFile(), *dex_compilation_unit.GetDexFile())) {
    if ((load_kind == HLoadClass::LoadKind::kDexCacheViaMethod) ||
        (load_kind == HLoadClass::LoadKind::kBssEntry)) {
      // We actually cannot reference this class, we're forced to bail.
      // We cannot reference this class with Bss, as the entrypoint will lookup the class
      // in the caller's dex file, but that dex file does not reference the class.
      return HLoadClass::LoadKind::kInvalid;
    }
  }
  return load_kind;
}

void HSharpening::ProcessLoadString(HLoadString* load_string) {
  DCHECK_EQ(load_string->GetLoadKind(), HLoadString::LoadKind::kDexCacheViaMethod);

  const DexFile& dex_file = load_string->GetDexFile();
  dex::StringIndex string_index = load_string->GetStringIndex();

  HLoadString::LoadKind desired_load_kind = static_cast<HLoadString::LoadKind>(-1);
  {
    Runtime* runtime = Runtime::Current();
    ClassLinker* class_linker = runtime->GetClassLinker();
    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::DexCache> dex_cache = IsSameDexFile(dex_file, *compilation_unit_.GetDexFile())
        ? compilation_unit_.GetDexCache()
        : hs.NewHandle(class_linker->FindDexCache(soa.Self(), dex_file));
    mirror::String* string = nullptr;

    if (codegen_->GetCompilerOptions().IsBootImage()) {
      // Compiling boot image. Resolve the string and allocate it if needed, to ensure
      // the string will be added to the boot image.
      DCHECK(!runtime->UseJitCompilation());
      string = class_linker->ResolveString(dex_file, string_index, dex_cache);
      CHECK(string != nullptr);
      if (compiler_driver_->GetSupportBootImageFixup()) {
        DCHECK(ContainsElement(compiler_driver_->GetDexFilesForOatFile(), &dex_file));
        desired_load_kind = codegen_->GetCompilerOptions().GetCompilePic()
            ? HLoadString::LoadKind::kBootImageLinkTimePcRelative
            : HLoadString::LoadKind::kBootImageLinkTimeAddress;
      } else {
        // compiler_driver_test. Do not sharpen.
        desired_load_kind = HLoadString::LoadKind::kDexCacheViaMethod;
      }
    } else if (runtime->UseJitCompilation()) {
      // TODO: Make sure we don't set the "compile PIC" flag for JIT as that's bogus.
      // DCHECK(!codegen_->GetCompilerOptions().GetCompilePic());
      string = class_linker->LookupString(dex_file, string_index, dex_cache.Get());
      if (string != nullptr) {
        if (runtime->GetHeap()->ObjectIsInBootImageSpace(string)) {
          desired_load_kind = HLoadString::LoadKind::kBootImageAddress;
        } else {
          desired_load_kind = HLoadString::LoadKind::kJitTableAddress;
        }
      } else {
        desired_load_kind = HLoadString::LoadKind::kDexCacheViaMethod;
      }
    } else {
      // AOT app compilation. Try to lookup the string without allocating if not found.
      string = class_linker->LookupString(dex_file, string_index, dex_cache.Get());
      if (string != nullptr &&
          runtime->GetHeap()->ObjectIsInBootImageSpace(string) &&
          !codegen_->GetCompilerOptions().GetCompilePic()) {
        desired_load_kind = HLoadString::LoadKind::kBootImageAddress;
      } else {
        desired_load_kind = HLoadString::LoadKind::kBssEntry;
      }
    }
    if (string != nullptr) {
      load_string->SetString(handles_->NewHandle(string));
    }
  }
  DCHECK_NE(desired_load_kind, static_cast<HLoadString::LoadKind>(-1));

  HLoadString::LoadKind load_kind = codegen_->GetSupportedLoadStringKind(desired_load_kind);
  load_string->SetLoadKind(load_kind);
}

}  // namespace art
