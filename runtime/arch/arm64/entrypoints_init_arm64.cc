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

#include "entrypoints/jni/jni_entrypoints.h"
#include "entrypoints/quick/quick_alloc_entrypoints.h"
#include "entrypoints/quick/quick_default_externs.h"
#include "entrypoints/quick/quick_default_init_entrypoints.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "entrypoints/entrypoint_utils.h"
#include "entrypoints/math_entrypoints.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "interpreter/interpreter.h"

namespace art {

// Cast entrypoints.
extern "C" size_t artIsAssignableFromCode(const mirror::Class* klass,
                                          const mirror::Class* ref_class);

// Read barrier entrypoints.
// art_quick_read_barrier_mark_regX uses an non-standard calling
// convention: it expects its input in register X and returns its
// result in that same register, and saves and restores all
// caller-save registers.
extern "C" mirror::Object* art_quick_read_barrier_mark_reg00(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg01(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg02(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg03(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg04(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg05(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg06(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg07(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg08(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg09(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg10(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg11(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg12(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg12(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg13(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg14(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg15(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg16(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg17(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg18(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg19(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg20(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg21(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg22(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg22(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg23(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg24(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg25(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg26(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg27(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg28(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg29(mirror::Object*);

void InitEntryPoints(JniEntryPoints* jpoints, QuickEntryPoints* qpoints) {
  DefaultInitEntryPoints(jpoints, qpoints);

  // Cast
  qpoints->pInstanceofNonTrivial = artIsAssignableFromCode;
  qpoints->pCheckCast = art_quick_check_cast;

  // Math
  // TODO null entrypoints not needed for ARM64 - generate inline.
  qpoints->pCmpgDouble = nullptr;
  qpoints->pCmpgFloat = nullptr;
  qpoints->pCmplDouble = nullptr;
  qpoints->pCmplFloat = nullptr;
  qpoints->pFmod = fmod;
  qpoints->pL2d = nullptr;
  qpoints->pFmodf = fmodf;
  qpoints->pL2f = nullptr;
  qpoints->pD2iz = nullptr;
  qpoints->pF2iz = nullptr;
  qpoints->pIdivmod = nullptr;
  qpoints->pD2l = nullptr;
  qpoints->pF2l = nullptr;
  qpoints->pLdiv = nullptr;
  qpoints->pLmod = nullptr;
  qpoints->pLmul = nullptr;
  qpoints->pShlLong = nullptr;
  qpoints->pShrLong = nullptr;
  qpoints->pUshrLong = nullptr;

  // More math.
  qpoints->pCos = cos;
  qpoints->pSin = sin;
  qpoints->pAcos = acos;
  qpoints->pAsin = asin;
  qpoints->pAtan = atan;
  qpoints->pAtan2 = atan2;
  qpoints->pCbrt = cbrt;
  qpoints->pCosh = cosh;
  qpoints->pExp = exp;
  qpoints->pExpm1 = expm1;
  qpoints->pHypot = hypot;
  qpoints->pLog = log;
  qpoints->pLog10 = log10;
  qpoints->pNextAfter = nextafter;
  qpoints->pSinh = sinh;
  qpoints->pTan = tan;
  qpoints->pTanh = tanh;

  // Intrinsics
  qpoints->pIndexOf = art_quick_indexof;
  // The ARM64 StringCompareTo intrinsic does not call the runtime.
  qpoints->pStringCompareTo = nullptr;
  qpoints->pMemcpy = memcpy;

  // Read barrier.
  qpoints->pReadBarrierJni = ReadBarrierJni;
  // ARM64 is the architecture with the largest number of core
  // registers (32) that supports the read barrier configuration.
  // Because registers 30 (LR) and 31 (SP/XZR) cannot be used to pass
  // arguments, only define ReadBarrierMarkRegX entrypoints for the
  // first 30 registers.  This limitation is not a problem on other
  // supported architectures (ARM, x86 and x86-64) either, as they
  // have less core registers (resp. 16, 8 and 16).  (We may have to
  // revise that design choice if read barrier support is added for
  // MIPS and/or MIPS64.)
  qpoints->pReadBarrierMarkReg00 = art_quick_read_barrier_mark_reg00;
  qpoints->pReadBarrierMarkReg01 = art_quick_read_barrier_mark_reg01;
  qpoints->pReadBarrierMarkReg02 = art_quick_read_barrier_mark_reg02;
  qpoints->pReadBarrierMarkReg03 = art_quick_read_barrier_mark_reg03;
  qpoints->pReadBarrierMarkReg04 = art_quick_read_barrier_mark_reg04;
  qpoints->pReadBarrierMarkReg05 = art_quick_read_barrier_mark_reg05;
  qpoints->pReadBarrierMarkReg06 = art_quick_read_barrier_mark_reg06;
  qpoints->pReadBarrierMarkReg07 = art_quick_read_barrier_mark_reg07;
  qpoints->pReadBarrierMarkReg08 = art_quick_read_barrier_mark_reg08;
  qpoints->pReadBarrierMarkReg09 = art_quick_read_barrier_mark_reg09;
  qpoints->pReadBarrierMarkReg10 = art_quick_read_barrier_mark_reg10;
  qpoints->pReadBarrierMarkReg11 = art_quick_read_barrier_mark_reg11;
  qpoints->pReadBarrierMarkReg12 = art_quick_read_barrier_mark_reg12;
  qpoints->pReadBarrierMarkReg13 = art_quick_read_barrier_mark_reg13;
  qpoints->pReadBarrierMarkReg14 = art_quick_read_barrier_mark_reg14;
  qpoints->pReadBarrierMarkReg15 = art_quick_read_barrier_mark_reg15;
  qpoints->pReadBarrierMarkReg16 = art_quick_read_barrier_mark_reg16;
  qpoints->pReadBarrierMarkReg17 = art_quick_read_barrier_mark_reg17;
  qpoints->pReadBarrierMarkReg18 = art_quick_read_barrier_mark_reg18;
  qpoints->pReadBarrierMarkReg19 = art_quick_read_barrier_mark_reg19;
  qpoints->pReadBarrierMarkReg20 = art_quick_read_barrier_mark_reg20;
  qpoints->pReadBarrierMarkReg21 = art_quick_read_barrier_mark_reg21;
  qpoints->pReadBarrierMarkReg22 = art_quick_read_barrier_mark_reg22;
  qpoints->pReadBarrierMarkReg23 = art_quick_read_barrier_mark_reg23;
  qpoints->pReadBarrierMarkReg24 = art_quick_read_barrier_mark_reg24;
  qpoints->pReadBarrierMarkReg25 = art_quick_read_barrier_mark_reg25;
  qpoints->pReadBarrierMarkReg26 = art_quick_read_barrier_mark_reg26;
  qpoints->pReadBarrierMarkReg27 = art_quick_read_barrier_mark_reg27;
  qpoints->pReadBarrierMarkReg28 = art_quick_read_barrier_mark_reg28;
  qpoints->pReadBarrierMarkReg29 = art_quick_read_barrier_mark_reg29;
  qpoints->pReadBarrierSlow = artReadBarrierSlow;
  qpoints->pReadBarrierForRootSlow = artReadBarrierForRootSlow;
};

}  // namespace art
