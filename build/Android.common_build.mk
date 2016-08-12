#
# Copyright (C) 2011 The Android Open Source Project
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

ifndef ART_ANDROID_COMMON_BUILD_MK
ART_ANDROID_COMMON_BUILD_MK = true

include art/build/Android.common.mk
include art/build/Android.common_utils.mk

# These can be overridden via the environment or by editing to
# enable/disable certain build configuration.
#
# For example, to disable everything but the host debug build you use:
#
# (export ART_BUILD_TARGET_NDEBUG=false && export ART_BUILD_TARGET_DEBUG=false && export ART_BUILD_HOST_NDEBUG=false && ...)
#
# Beware that tests may use the non-debug build for performance, notable 055-enum-performance
#
ART_BUILD_TARGET_NDEBUG ?= true
ART_BUILD_TARGET_DEBUG ?= true
ART_BUILD_HOST_NDEBUG ?= true
ART_BUILD_HOST_DEBUG ?= true

# Set this to change what opt level ART is built at.
ART_DEBUG_OPT_FLAG ?= -O2
ART_NDEBUG_OPT_FLAG ?= -O3

# Enable the static builds only for checkbuilds.
ifneq (,$(filter checkbuild,$(MAKECMDGOALS)))
  ART_BUILD_HOST_STATIC ?= true
else
  ART_BUILD_HOST_STATIC ?= false
endif

# Asan does not support static linkage
ifdef SANITIZE_HOST
  ART_BUILD_HOST_STATIC := false
endif

ifneq ($(HOST_OS),linux)
  ART_BUILD_HOST_STATIC := false
endif

ifeq ($(ART_BUILD_TARGET_NDEBUG),false)
$(info Disabling ART_BUILD_TARGET_NDEBUG)
endif
ifeq ($(ART_BUILD_TARGET_DEBUG),false)
$(info Disabling ART_BUILD_TARGET_DEBUG)
endif
ifeq ($(ART_BUILD_HOST_NDEBUG),false)
$(info Disabling ART_BUILD_HOST_NDEBUG)
endif
ifeq ($(ART_BUILD_HOST_DEBUG),false)
$(info Disabling ART_BUILD_HOST_DEBUG)
endif
ifeq ($(ART_BUILD_HOST_STATIC),true)
$(info Enabling ART_BUILD_HOST_STATIC)
endif

ifeq ($(ART_TEST_DEBUG_GC),true)
  ART_DEFAULT_GC_TYPE := SS
  ART_USE_TLAB := true
endif

#
# Used to change the default GC. Valid values are CMS, SS, GSS. The default is CMS.
#
ART_DEFAULT_GC_TYPE ?= CMS
art_default_gc_type_cflags := -DART_DEFAULT_GC_TYPE_IS_$(ART_DEFAULT_GC_TYPE)

ART_HOST_CLANG := true
ART_TARGET_CLANG := true

ART_CPP_EXTENSION := .cc

ART_C_INCLUDES := \
  external/gtest/include \
  external/icu/icu4c/source/common \
  external/lz4/lib \
  external/valgrind/include \
  external/valgrind \
  external/vixl/src \
  external/zlib \

# We optimize Thread::Current() with a direct TLS access. This requires access to a private
# Bionic header.
# Note: technically we only need this on device, but this avoids the duplication of the includes.
ART_C_INCLUDES += bionic/libc/private

art_cflags :=

# Warn about thread safety violations with clang.
art_cflags += -Wthread-safety -Wthread-safety-negative

# Warn if switch fallthroughs aren't annotated.
art_cflags += -Wimplicit-fallthrough

# Enable float equality warnings.
art_cflags += -Wfloat-equal

# Enable warning of converting ints to void*.
art_cflags += -Wint-to-void-pointer-cast

# Enable warning of wrong unused annotations.
art_cflags += -Wused-but-marked-unused

# Enable warning for deprecated language features.
art_cflags += -Wdeprecated

# Enable warning for unreachable break & return.
art_cflags += -Wunreachable-code-break -Wunreachable-code-return

# Bug: http://b/29823425  Disable -Wconstant-conversion and
# -Wundefined-var-template for Clang update to r271374
art_cflags += -Wno-constant-conversion -Wno-undefined-var-template

# Enable missing-noreturn only on non-Mac. As lots of things are not implemented for Apple, it's
# a pain.
ifneq ($(HOST_OS),darwin)
  art_cflags += -Wmissing-noreturn
endif

# Base set of cflags used by all things ART.
art_cflags += \
  -fno-rtti \
  -std=gnu++11 \
  -ggdb3 \
  -Wall \
  -Werror \
  -Wextra \
  -Wstrict-aliasing \
  -fstrict-aliasing \
  -Wunreachable-code \
  -Wredundant-decls \
  -Wshadow \
  -Wunused \
  -fvisibility=protected \
  $(art_default_gc_type_cflags)

# The architectures the compiled tools are able to run on. Setting this to 'all' will cause all
# architectures to be included.
ART_TARGET_CODEGEN_ARCHS ?= all
ART_HOST_CODEGEN_ARCHS ?= all

ifeq ($(ART_TARGET_CODEGEN_ARCHS),all)
  ART_TARGET_CODEGEN_ARCHS := $(sort $(ART_TARGET_SUPPORTED_ARCH) $(ART_HOST_SUPPORTED_ARCH))
  # We need to handle the fact that some compiler tests mix code from different architectures.
  ART_TARGET_COMPILER_TESTS ?= true
else
  ART_TARGET_COMPILER_TESTS := false
  ifeq ($(ART_TARGET_CODEGEN_ARCHS),svelte)
    ART_TARGET_CODEGEN_ARCHS := $(sort $(ART_TARGET_ARCH_64) $(ART_TARGET_ARCH_32))
  endif
endif
ifeq ($(ART_HOST_CODEGEN_ARCHS),all)
  ART_HOST_CODEGEN_ARCHS := $(sort $(ART_TARGET_SUPPORTED_ARCH) $(ART_HOST_SUPPORTED_ARCH))
  ART_HOST_COMPILER_TESTS ?= true
else
  ART_HOST_COMPILER_TESTS := false
  ifeq ($(ART_HOST_CODEGEN_ARCHS),svelte)
    ART_HOST_CODEGEN_ARCHS := $(sort $(ART_TARGET_CODEGEN_ARCHS) $(ART_HOST_ARCH_64) $(ART_HOST_ARCH_32))
  endif
endif

ifneq (,$(filter arm64,$(ART_TARGET_CODEGEN_ARCHS)))
  ART_TARGET_CODEGEN_ARCHS += arm
endif
ifneq (,$(filter mips64,$(ART_TARGET_CODEGEN_ARCHS)))
  ART_TARGET_CODEGEN_ARCHS += mips
endif
ifneq (,$(filter x86_64,$(ART_TARGET_CODEGEN_ARCHS)))
  ART_TARGET_CODEGEN_ARCHS += x86
endif
ART_TARGET_CODEGEN_ARCHS := $(sort $(ART_TARGET_CODEGEN_ARCHS))
ifneq (,$(filter arm64,$(ART_HOST_CODEGEN_ARCHS)))
  ART_HOST_CODEGEN_ARCHS += arm
endif
ifneq (,$(filter mips64,$(ART_HOST_CODEGEN_ARCHS)))
  ART_HOST_CODEGEN_ARCHS += mips
endif
ifneq (,$(filter x86_64,$(ART_HOST_CODEGEN_ARCHS)))
  ART_HOST_CODEGEN_ARCHS += x86
endif
ART_HOST_CODEGEN_ARCHS := $(sort $(ART_HOST_CODEGEN_ARCHS))

# Base set of cflags used by target build only
art_target_cflags := \
  $(foreach target_arch,$(strip $(ART_TARGET_CODEGEN_ARCHS)), -DART_ENABLE_CODEGEN_$(target_arch))
# Base set of cflags used by host build only
art_host_cflags := \
  $(foreach host_arch,$(strip $(ART_HOST_CODEGEN_ARCHS)), -DART_ENABLE_CODEGEN_$(host_arch))

# Base set of asflags used by all things ART.
art_asflags :=

# Missing declarations: too many at the moment, as we use "extern" quite a bit.
#  -Wmissing-declarations \



ifdef ART_IMT_SIZE
  art_cflags += -DIMT_SIZE=$(ART_IMT_SIZE)
else
  # Default is 43
  art_cflags += -DIMT_SIZE=43
endif

ifeq ($(ART_HEAP_POISONING),true)
  art_cflags += -DART_HEAP_POISONING=1
  art_asflags += -DART_HEAP_POISONING=1
endif

#
# Used to change the read barrier type. Valid values are BAKER, BROOKS, TABLELOOKUP.
# The default is BAKER.
#
ART_READ_BARRIER_TYPE ?= BAKER

ifeq ($(ART_USE_READ_BARRIER),true)
  art_cflags += -DART_USE_READ_BARRIER=1
  art_cflags += -DART_READ_BARRIER_TYPE_IS_$(ART_READ_BARRIER_TYPE)=1
  art_asflags += -DART_USE_READ_BARRIER=1
  art_asflags += -DART_READ_BARRIER_TYPE_IS_$(ART_READ_BARRIER_TYPE)=1

  # Temporarily override -fstack-protector-strong with -fstack-protector to avoid a major
  # slowdown with the read barrier config. b/26744236.
  art_cflags += -fstack-protector
endif

ifeq ($(ART_USE_TLAB),true)
  art_cflags += -DART_USE_TLAB=1
endif

# Cflags for non-debug ART and ART tools.
art_non_debug_cflags := \
  $(ART_NDEBUG_OPT_FLAG)

# Cflags for debug ART and ART tools.
art_debug_cflags := \
  $(ART_DEBUG_OPT_FLAG) \
  -DDYNAMIC_ANNOTATIONS_ENABLED=1 \
  -DVIXL_DEBUG \
  -UNDEBUG

# Assembler flags for non-debug ART and ART tools.
art_non_debug_asflags :=

# Assembler flags for debug ART and ART tools.
art_debug_asflags := -UNDEBUG

art_host_non_debug_cflags := $(art_non_debug_cflags)
art_target_non_debug_cflags := $(art_non_debug_cflags)

###
# Frame size
###

# Size of the stack-overflow gap.
ART_STACK_OVERFLOW_GAP_arm := 8192
ART_STACK_OVERFLOW_GAP_arm64 := 8192
ART_STACK_OVERFLOW_GAP_mips := 16384
ART_STACK_OVERFLOW_GAP_mips64 := 16384
ART_STACK_OVERFLOW_GAP_x86 := 8192
ART_STACK_OVERFLOW_GAP_x86_64 := 8192
ART_COMMON_STACK_OVERFLOW_DEFINES := \
  -DART_STACK_OVERFLOW_GAP_arm=$(ART_STACK_OVERFLOW_GAP_arm) \
  -DART_STACK_OVERFLOW_GAP_arm64=$(ART_STACK_OVERFLOW_GAP_arm64) \
  -DART_STACK_OVERFLOW_GAP_mips=$(ART_STACK_OVERFLOW_GAP_mips) \
  -DART_STACK_OVERFLOW_GAP_mips64=$(ART_STACK_OVERFLOW_GAP_mips64) \
  -DART_STACK_OVERFLOW_GAP_x86=$(ART_STACK_OVERFLOW_GAP_x86) \
  -DART_STACK_OVERFLOW_GAP_x86_64=$(ART_STACK_OVERFLOW_GAP_x86_64) \

# Keep these as small as possible. We have separate values as we have some host vs target
# specific code (and previously GCC vs Clang).
ART_HOST_FRAME_SIZE_LIMIT := 1736
ART_TARGET_FRAME_SIZE_LIMIT := 1736

# Frame size adaptations for instrumented builds.
ifdef SANITIZE_TARGET
  ART_TARGET_FRAME_SIZE_LIMIT := 6400
endif

# Add frame-size checks for non-debug builds.
ifeq ($(HOST_OS),linux)
  ifneq ($(ART_COVERAGE),true)
    ifneq ($(NATIVE_COVERAGE),true)
      art_host_non_debug_cflags += -Wframe-larger-than=$(ART_HOST_FRAME_SIZE_LIMIT)
      art_target_non_debug_cflags += -Wframe-larger-than=$(ART_TARGET_FRAME_SIZE_LIMIT)
    endif
  endif
endif


ART_HOST_CFLAGS := $(art_cflags)
ART_TARGET_CFLAGS := $(art_cflags)

ART_HOST_ASFLAGS := $(art_asflags)
ART_TARGET_ASFLAGS := $(art_asflags)

# Bug: 15446488. We don't omit the frame pointer to work around
# clang/libunwind bugs that cause SEGVs in run-test-004-ThreadStress.
ART_HOST_CFLAGS += -fno-omit-frame-pointer

ifndef LIBART_IMG_HOST_BASE_ADDRESS
  $(error LIBART_IMG_HOST_BASE_ADDRESS unset)
endif
ART_HOST_CFLAGS += -DART_BASE_ADDRESS=$(LIBART_IMG_HOST_BASE_ADDRESS)
ART_HOST_CFLAGS += -DART_DEFAULT_INSTRUCTION_SET_FEATURES=default $(art_host_cflags)

ART_HOST_CFLAGS += -DART_FRAME_SIZE_LIMIT=$(ART_HOST_FRAME_SIZE_LIMIT) \
                   $(ART_COMMON_STACK_OVERFLOW_DEFINES)


ifndef LIBART_IMG_TARGET_BASE_ADDRESS
  $(error LIBART_IMG_TARGET_BASE_ADDRESS unset)
endif

ART_TARGET_CFLAGS += -DART_TARGET \
                     -DART_BASE_ADDRESS=$(LIBART_IMG_TARGET_BASE_ADDRESS) \

ART_TARGET_CFLAGS += -DART_FRAME_SIZE_LIMIT=$(ART_TARGET_FRAME_SIZE_LIMIT) \
                     $(ART_COMMON_STACK_OVERFLOW_DEFINES)

ifeq ($(ART_TARGET_LINUX),true)
# Setting ART_TARGET_LINUX to true compiles art/ assuming that the target device
# will be running linux rather than android.
ART_TARGET_CFLAGS += -DART_TARGET_LINUX
else
# The ART_TARGET_ANDROID macro is passed to target builds, which check
# against it instead of against __ANDROID__ (which is provided by target
# toolchains).
ART_TARGET_CFLAGS += -DART_TARGET_ANDROID
endif

ART_TARGET_CFLAGS += $(art_target_cflags)

ART_HOST_NON_DEBUG_CFLAGS := $(art_host_non_debug_cflags)
ART_TARGET_NON_DEBUG_CFLAGS := $(art_target_non_debug_cflags)
ART_HOST_DEBUG_CFLAGS := $(art_debug_cflags)
ART_TARGET_DEBUG_CFLAGS := $(art_debug_cflags)

ART_HOST_NON_DEBUG_ASFLAGS := $(art_non_debug_asflags)
ART_TARGET_NON_DEBUG_ASFLAGS := $(art_non_debug_asflags)
ART_HOST_DEBUG_ASFLAGS := $(art_debug_asflags)
ART_TARGET_DEBUG_ASFLAGS := $(art_debug_asflags)

ifndef LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA
  LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA=-0x1000000
endif
ifndef LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA
  LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA=0x1000000
endif
ART_HOST_CFLAGS += -DART_BASE_ADDRESS_MIN_DELTA=$(LIBART_IMG_HOST_MIN_BASE_ADDRESS_DELTA)
ART_HOST_CFLAGS += -DART_BASE_ADDRESS_MAX_DELTA=$(LIBART_IMG_HOST_MAX_BASE_ADDRESS_DELTA)

ifndef LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA
  LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA=-0x1000000
endif
ifndef LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA
  LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA=0x1000000
endif
ART_TARGET_CFLAGS += -DART_BASE_ADDRESS_MIN_DELTA=$(LIBART_IMG_TARGET_MIN_BASE_ADDRESS_DELTA)
ART_TARGET_CFLAGS += -DART_BASE_ADDRESS_MAX_DELTA=$(LIBART_IMG_TARGET_MAX_BASE_ADDRESS_DELTA)

# To use oprofile_android --callgraph, uncomment this and recompile with "mmm art -B -j16"
# ART_TARGET_CFLAGS += -fno-omit-frame-pointer -marm -mapcs

# Clear locals now they've served their purpose.
art_cflags :=
art_asflags :=
art_host_cflags :=
art_target_cflags :=
art_debug_cflags :=
art_non_debug_cflags :=
art_debug_asflags :=
art_non_debug_asflags :=
art_host_non_debug_cflags :=
art_target_non_debug_cflags :=
art_default_gc_type_cflags :=

ART_TARGET_LDFLAGS :=

# $(1): ndebug_or_debug
define set-target-local-cflags-vars
  LOCAL_CFLAGS += $(ART_TARGET_CFLAGS)
  LOCAL_ASFLAGS += $(ART_TARGET_ASFLAGS)
  LOCAL_LDFLAGS += $(ART_TARGET_LDFLAGS)
  art_target_cflags_ndebug_or_debug := $(1)
  ifeq ($$(art_target_cflags_ndebug_or_debug),debug)
    LOCAL_CFLAGS += $(ART_TARGET_DEBUG_CFLAGS)
    LOCAL_ASFLAGS += $(ART_TARGET_DEBUG_ASFLAGS)
  else
    LOCAL_CFLAGS += $(ART_TARGET_NON_DEBUG_CFLAGS)
    LOCAL_ASFLAGS += $(ART_TARGET_NON_DEBUG_ASFLAGS)
  endif

  # Clear locally used variables.
  art_target_cflags_ndebug_or_debug :=
endef

# Support for disabling certain builds.
ART_BUILD_TARGET := false
ART_BUILD_HOST := false
ART_BUILD_NDEBUG := false
ART_BUILD_DEBUG := false
ifeq ($(ART_BUILD_TARGET_NDEBUG),true)
  ART_BUILD_TARGET := true
  ART_BUILD_NDEBUG := true
endif
ifeq ($(ART_BUILD_TARGET_DEBUG),true)
  ART_BUILD_TARGET := true
  ART_BUILD_DEBUG := true
endif
ifeq ($(ART_BUILD_HOST_NDEBUG),true)
  ART_BUILD_HOST := true
  ART_BUILD_NDEBUG := true
endif
ifeq ($(ART_BUILD_HOST_DEBUG),true)
  ART_BUILD_HOST := true
  ART_BUILD_DEBUG := true
endif

endif # ART_ANDROID_COMMON_BUILD_MK
