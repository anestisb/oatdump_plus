/*
 * Copyright (C) 2012 The Android Open Source Project
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
#if !defined(__APPLE__)
#include "entrypoints/quick/quick_default_init_entrypoints.h"
#endif
#include "entrypoints/quick/quick_entrypoints.h"
#include "entrypoints/math_entrypoints.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "interpreter/interpreter.h"

namespace art {

// Cast entrypoints.
extern "C" size_t art_quick_assignable_from_code(const mirror::Class* klass,
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
extern "C" mirror::Object* art_quick_read_barrier_mark_reg05(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg06(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg07(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg08(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg09(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg10(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg11(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg12(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg13(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg14(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_mark_reg15(mirror::Object*);
extern "C" mirror::Object* art_quick_read_barrier_slow(mirror::Object*, mirror::Object*, uint32_t);
extern "C" mirror::Object* art_quick_read_barrier_for_root_slow(GcRoot<mirror::Object>*);

void InitEntryPoints(JniEntryPoints* jpoints, QuickEntryPoints* qpoints) {
#if defined(__APPLE__)
  UNUSED(jpoints, qpoints);
  UNIMPLEMENTED(FATAL);
#else
  DefaultInitEntryPoints(jpoints, qpoints);

  // Cast
  qpoints->pInstanceofNonTrivial = art_quick_assignable_from_code;
  qpoints->pCheckCast = art_quick_check_cast;

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

  // Math
  qpoints->pD2l = art_d2l;
  qpoints->pF2l = art_f2l;
  qpoints->pLdiv = art_quick_ldiv;
  qpoints->pLmod = art_quick_lmod;
  qpoints->pLmul = art_quick_lmul;
  qpoints->pShlLong = art_quick_lshl;
  qpoints->pShrLong = art_quick_lshr;
  qpoints->pUshrLong = art_quick_lushr;

  // Intrinsics
  qpoints->pStringCompareTo = art_quick_string_compareto;
  qpoints->pMemcpy = art_quick_memcpy;

  // Read barrier.
  qpoints->pReadBarrierJni = ReadBarrierJni;
  qpoints->pReadBarrierMarkReg00 = art_quick_read_barrier_mark_reg00;
  qpoints->pReadBarrierMarkReg01 = art_quick_read_barrier_mark_reg01;
  qpoints->pReadBarrierMarkReg02 = art_quick_read_barrier_mark_reg02;
  qpoints->pReadBarrierMarkReg03 = art_quick_read_barrier_mark_reg03;
  qpoints->pReadBarrierMarkReg04 = nullptr;  // Cannot use register 4 (RSP) to pass arguments.
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
  // x86-64 has only 16 core registers.
  qpoints->pReadBarrierMarkReg16 = nullptr;
  qpoints->pReadBarrierMarkReg17 = nullptr;
  qpoints->pReadBarrierMarkReg18 = nullptr;
  qpoints->pReadBarrierMarkReg19 = nullptr;
  qpoints->pReadBarrierMarkReg20 = nullptr;
  qpoints->pReadBarrierMarkReg21 = nullptr;
  qpoints->pReadBarrierMarkReg22 = nullptr;
  qpoints->pReadBarrierMarkReg23 = nullptr;
  qpoints->pReadBarrierMarkReg24 = nullptr;
  qpoints->pReadBarrierMarkReg25 = nullptr;
  qpoints->pReadBarrierMarkReg26 = nullptr;
  qpoints->pReadBarrierMarkReg27 = nullptr;
  qpoints->pReadBarrierMarkReg28 = nullptr;
  qpoints->pReadBarrierMarkReg29 = nullptr;
  qpoints->pReadBarrierMarkReg30 = nullptr;
  qpoints->pReadBarrierMarkReg31 = nullptr;
  qpoints->pReadBarrierSlow = art_quick_read_barrier_slow;
  qpoints->pReadBarrierForRootSlow = art_quick_read_barrier_for_root_slow;
#endif  // __APPLE__
};

}  // namespace art
