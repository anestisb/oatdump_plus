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
  020-string \
  021-string2 \
  042-new-instance \
  044-proxy \
  080-oom-throw \
  082-inline-execute \
  096-array-copy-concurrent-gc \
  099-vmdebug \
  100-reflect2 \
  103-string-append \
  114-ParallelGC \
  122-npe \
  129-ThreadGetId \
  137-cfi \
  144-static-field-sigquit \
  412-new-array \
  439-npe \
  450-checker-types \
  488-checker-inline-recursive-calls \
  515-dce-dominator \
  520-equivalent-phi \
  525-checker-arrays-fields1 \
  525-checker-arrays-fields2 \
  527-checker-array-access-split \
  538-checker-embed-constants \
  552-checker-sharpening \
  562-checker-no-intermediate \
  570-checker-osr \
  602-deoptimizeable \
  700-LoadArgRegs \
  800-smali \

