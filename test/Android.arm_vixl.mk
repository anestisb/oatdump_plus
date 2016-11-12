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
  004-ThreadStress \
  028-array-write \
  037-inherit \
  042-new-instance \
  044-proxy \
  080-oom-throw \
  082-inline-execute \
  083-compiler-regressions \
  096-array-copy-concurrent-gc \
  099-vmdebug \
  103-string-append \
  114-ParallelGC \
  122-npe \
  123-inline-execute2 \
  129-ThreadGetId \
  137-cfi \
  144-static-field-sigquit \
  201-built-in-except-detail-messages \
  412-new-array \
  422-type-conversion \
  437-inline \
  439-npe \
  442-checker-constant-folding \
  450-checker-types \
  458-checker-instruct-simplification \
  458-long-to-fpu \
  488-checker-inline-recursive-calls \
  510-checker-try-catch \
  515-dce-dominator \
  520-equivalent-phi \
  525-checker-arrays-fields1 \
  525-checker-arrays-fields2 \
  527-checker-array-access-split \
  530-checker-loops2 \
  530-checker-lse \
  530-checker-lse2 \
  535-regression-const-val \
  536-checker-intrinsic-optimization \
  538-checker-embed-constants \
  550-checker-multiply-accumulate \
  552-checker-primitive-typeprop \
  552-checker-sharpening \
  555-UnsafeGetLong-regression \
  562-checker-no-intermediate \
  564-checker-negbitwise \
  570-checker-osr \
  570-checker-select \
  574-irreducible-and-constant-area \
  580-checker-round \
  594-checker-array-alias \
  602-deoptimizeable \
  700-LoadArgRegs \
  800-smali \
