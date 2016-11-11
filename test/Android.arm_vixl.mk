#
# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Known broken tests for the ARM VIXL backend.
TEST_ART_BROKEN_OPTIMIZING_ARM_VIXL_RUN_TESTS := \
  003-omnibus-opcodes \
  004-NativeAllocations \
  004-ThreadStress \
  012-math \
  015-switch \
  021-string2 \
  028-array-write \
  036-finalizer \
  037-inherit \
  042-new-instance \
  044-proxy \
  050-sync-test \
  051-thread \
  068-classloader \
  074-gc-thrash \
  079-phantom \
  080-oom-throw \
  082-inline-execute \
  083-compiler-regressions \
  088-monitor-verification \
  096-array-copy-concurrent-gc \
  099-vmdebug \
  103-string-append \
  106-exceptions2 \
  107-int-math2 \
  114-ParallelGC \
  120-hashcode \
  121-modifiers \
  122-npe \
  123-compiler-regressions-mt \
  123-inline-execute2 \
  129-ThreadGetId \
  132-daemon-locks-shutdown \
  137-cfi \
  138-duplicate-classes-check2 \
  141-class-unload \
  144-static-field-sigquit \
  201-built-in-except-detail-messages \
  412-new-array \
  417-optimizing-arith-div \
  422-type-conversion \
  426-monitor \
  428-optimizing-arith-rem \
  436-rem-float \
  437-inline \
  439-npe \
  442-checker-constant-folding \
  444-checker-nce \
  445-checker-licm \
  447-checker-inliner3 \
  449-checker-bce \
  450-checker-types \
  458-checker-instruct-simplification \
  458-long-to-fpu \
  485-checker-dce-switch \
  488-checker-inline-recursive-calls \
  508-checker-disassembly \
  510-checker-try-catch \
  515-dce-dominator \
  520-equivalent-phi \
  522-checker-regression-monitor-exit \
  523-checker-can-throw-regression \
  525-checker-arrays-fields1 \
  525-checker-arrays-fields2 \
  526-checker-caller-callee-regs \
  527-checker-array-access-split \
  530-checker-loops1 \
  530-checker-loops2 \
  530-checker-lse \
  530-checker-lse2 \
  535-regression-const-val \
  536-checker-intrinsic-optimization \
  538-checker-embed-constants \
  543-checker-dce-trycatch \
  546-regression-simplify-catch \
  550-checker-multiply-accumulate \
  552-checker-primitive-typeprop \
  552-checker-sharpening \
  555-UnsafeGetLong-regression \
  558-switch \
  560-packed-switch \
  561-divrem \
  562-checker-no-intermediate \
  564-checker-negbitwise \
  570-checker-osr \
  570-checker-select \
  573-checker-checkcast-regression \
  574-irreducible-and-constant-area \
  575-checker-string-init-alias \
  580-checker-round \
  584-checker-div-bool \
  588-checker-irreducib-lifetime-hole \
  594-checker-array-alias \
  597-deopt-new-string \
  602-deoptimizeable \
  700-LoadArgRegs \
  701-easy-div-rem \
  702-LargeBranchOffset \
  800-smali \
