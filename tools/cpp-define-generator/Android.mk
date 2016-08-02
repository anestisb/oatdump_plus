#
# Copyright (C) 2014 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include art/build/Android.executable.mk

CPP_DEFINE_GENERATOR_SRC_FILES := \
  main.cc

CPP_DEFINE_GENERATOR_EXTRA_SHARED_LIBRARIES :=
CPP_DEFINE_GENERATOR_EXTRA_INCLUDE :=
CPP_DEFINE_GENERATOR_MULTILIB :=

# Build a "data" binary which will hold all the symbol values that will be parsed by the other scripts.
#
# Builds are for host only, target-specific define generation is possibly but is trickier and would need extra tooling.
#
# In the future we may wish to parameterize this on (32,64)x(read_barrier,no_read_barrier).
$(eval $(call build-art-executable,cpp-define-generator-data,$(CPP_DEFINE_GENERATOR_SRC_FILES),$(CPP_DEFINE_GENERATOR_EXTRA_SHARED_LIBRARIES),$(CPP_DEFINE_GENERATOR_EXTRA_INCLUDE),host,debug,$(CPP_DEFINE_GENERATOR_MULTILIB),shared))

