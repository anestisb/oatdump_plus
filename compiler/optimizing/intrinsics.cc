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

#include "intrinsics.h"

#include "art_method.h"
#include "class_linker.h"
#include "driver/compiler_driver.h"
#include "invoke_type.h"
#include "mirror/dex_cache-inl.h"
#include "nodes.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "utils.h"

namespace art {

// Function that returns whether an intrinsic is static/direct or virtual.
static inline InvokeType GetIntrinsicInvokeType(Intrinsics i) {
  switch (i) {
    case Intrinsics::kNone:
      return kInterface;  // Non-sensical for intrinsic.
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      return IsStatic;
#include "intrinsics_list.h"
INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef INTRINSICS_LIST
#undef OPTIMIZING_INTRINSICS
  }
  return kInterface;
}

// Function that returns whether an intrinsic needs an environment or not.
static inline IntrinsicNeedsEnvironmentOrCache NeedsEnvironmentOrCache(Intrinsics i) {
  switch (i) {
    case Intrinsics::kNone:
      return kNeedsEnvironmentOrCache;  // Non-sensical for intrinsic.
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      return NeedsEnvironmentOrCache;
#include "intrinsics_list.h"
INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef INTRINSICS_LIST
#undef OPTIMIZING_INTRINSICS
  }
  return kNeedsEnvironmentOrCache;
}

// Function that returns whether an intrinsic has side effects.
static inline IntrinsicSideEffects GetSideEffects(Intrinsics i) {
  switch (i) {
    case Intrinsics::kNone:
      return kAllSideEffects;
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      return SideEffects;
#include "intrinsics_list.h"
INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef INTRINSICS_LIST
#undef OPTIMIZING_INTRINSICS
  }
  return kAllSideEffects;
}

// Function that returns whether an intrinsic can throw exceptions.
static inline IntrinsicExceptions GetExceptions(Intrinsics i) {
  switch (i) {
    case Intrinsics::kNone:
      return kCanThrow;
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      return Exceptions;
#include "intrinsics_list.h"
INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef INTRINSICS_LIST
#undef OPTIMIZING_INTRINSICS
  }
  return kCanThrow;
}

static bool CheckInvokeType(Intrinsics intrinsic, HInvoke* invoke) {
  // Whenever the intrinsic is marked as static, report an error if we find an InvokeVirtual.
  //
  // Whenever the intrinsic is marked as direct and we find an InvokeVirtual, a devirtualization
  // failure occured. We might be in a situation where we have inlined a method that calls an
  // intrinsic, but that method is in a different dex file on which we do not have a
  // verified_method that would have helped the compiler driver sharpen the call. In that case,
  // make sure that the intrinsic is actually for some final method (or in a final class), as
  // otherwise the intrinsics setup is broken.
  //
  // For the last direction, we have intrinsics for virtual functions that will perform a check
  // inline. If the precise type is known, however, the instruction will be sharpened to an
  // InvokeStaticOrDirect.
  InvokeType intrinsic_type = GetIntrinsicInvokeType(intrinsic);
  InvokeType invoke_type = invoke->GetInvokeType();
  switch (intrinsic_type) {
    case kStatic:
      return (invoke_type == kStatic);

    case kDirect:
      if (invoke_type == kDirect) {
        return true;
      }
      if (invoke_type == kVirtual) {
        ArtMethod* art_method = invoke->GetResolvedMethod();
        ScopedObjectAccess soa(Thread::Current());
        return (art_method->IsFinal() || art_method->GetDeclaringClass()->IsFinal());
      }
      return false;

    case kVirtual:
      // Call might be devirtualized.
      return (invoke_type == kVirtual || invoke_type == kDirect);

    default:
      return false;
  }
}

void IntrinsicsRecognizer::Run() {
  ScopedObjectAccess soa(Thread::Current());
  for (HReversePostOrderIterator it(*graph_); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();
    for (HInstructionIterator inst_it(block->GetInstructions()); !inst_it.Done();
         inst_it.Advance()) {
      HInstruction* inst = inst_it.Current();
      if (inst->IsInvoke()) {
        HInvoke* invoke = inst->AsInvoke();
        ArtMethod* art_method = invoke->GetResolvedMethod();
        if (art_method != nullptr && art_method->IsIntrinsic()) {
          Intrinsics intrinsic = static_cast<Intrinsics>(art_method->GetIntrinsic());
          if (!CheckInvokeType(intrinsic, invoke)) {
            LOG(WARNING) << "Found an intrinsic with unexpected invoke type: "
                << intrinsic << " for "
                << PrettyMethod(invoke->GetDexMethodIndex(), invoke->GetDexFile())
                << invoke->DebugName();
          } else {
            invoke->SetIntrinsic(intrinsic,
                                 NeedsEnvironmentOrCache(intrinsic),
                                 GetSideEffects(intrinsic),
                                 GetExceptions(intrinsic));
            MaybeRecordStat(MethodCompilationStat::kIntrinsicRecognized);
          }
        }
      }
    }
  }
}

std::ostream& operator<<(std::ostream& os, const Intrinsics& intrinsic) {
  switch (intrinsic) {
    case Intrinsics::kNone:
      os << "None";
      break;
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      os << # Name; \
      break;
#include "intrinsics_list.h"
INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef STATIC_INTRINSICS_LIST
#undef VIRTUAL_INTRINSICS_LIST
#undef OPTIMIZING_INTRINSICS
  }
  return os;
}

}  // namespace art
