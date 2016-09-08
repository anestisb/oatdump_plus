#
# Copyright (C) 2012 The Android Open Source Project
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

include art/build/Android.common_build.mk

LIBART_DISASSEMBLER_SRC_FILES := \
	disassembler.cc \
	disassembler_arm.cc \
	disassembler_arm64.cc \
	disassembler_mips.cc \
	disassembler_x86.cc

# $(1): target or host
# $(2): ndebug or debug
# $(3): static or shared (static is only valid for host)
define build-libart-disassembler
  ifneq ($(1),target)
    ifneq ($(1),host)
      $$(error expected target or host for argument 1, received $(1))
    endif
  endif
  ifneq ($(2),ndebug)
    ifneq ($(2),debug)
      $$(error expected ndebug or debug for argument 2, received $(2))
    endif
  endif
  ifeq ($(3),static)
    ifneq ($(1),host)
      $$(error received static for argument 3, but argument 1 is not host)
    endif
  else
    ifneq ($(3),shared)
      $$(error expected static or shared for argument 3, received $(3))
    endif
  endif

  art_target_or_host := $(1)
  art_ndebug_or_debug := $(2)
  art_static_or_shared := $(3)

  include $(CLEAR_VARS)
  ifeq ($$(art_target_or_host),host)
     LOCAL_IS_HOST_MODULE := true
  endif
  LOCAL_CPP_EXTENSION := $(ART_CPP_EXTENSION)
  ifeq ($$(art_ndebug_or_debug),ndebug)
    LOCAL_MODULE := libart-disassembler
  else # debug
    LOCAL_MODULE := libartd-disassembler
  endif

  LOCAL_MODULE_TAGS := optional
  ifeq ($$(art_static_or_shared),static)
    LOCAL_MODULE_CLASS := STATIC_LIBRARIES
  else # shared
    LOCAL_MODULE_CLASS := SHARED_LIBRARIES
  endif

  LOCAL_SRC_FILES := $$(LIBART_DISASSEMBLER_SRC_FILES)

  ifeq ($$(art_target_or_host),target)
    LOCAL_CLANG := $(ART_TARGET_CLANG)
    $(call set-target-local-cflags-vars,$(2))
  else # host
    LOCAL_CLANG := $(ART_HOST_CLANG)
    LOCAL_CFLAGS += $(ART_HOST_CFLAGS)
    LOCAL_ASFLAGS += $(ART_HOST_ASFLAGS)
    ifeq ($$(art_ndebug_or_debug),debug)
      LOCAL_CFLAGS += $(ART_HOST_DEBUG_CFLAGS)
      LOCAL_ASFLAGS += $(ART_HOST_DEBUG_ASFLAGS)
    else
      LOCAL_CFLAGS += $(ART_HOST_NON_DEBUG_CFLAGS)
      LOCAL_ASFLAGS += $(ART_HOST_NON_DEBUG_ASFLAGS)
    endif
  endif

  ifeq ($$(art_static_or_shared),static)
    LOCAL_STATIC_LIBRARIES += liblog libbase
    ifeq ($$(art_ndebug_or_debug),debug)
      LOCAL_STATIC_LIBRARIES += libartd
    else
      LOCAL_STATIC_LIBRARIES += libart
    endif
  else # shared
    LOCAL_SHARED_LIBRARIES += liblog libbase
    ifeq ($$(art_ndebug_or_debug),debug)
      LOCAL_SHARED_LIBRARIES += libartd
    else
      LOCAL_SHARED_LIBRARIES += libart
    endif
  endif

  LOCAL_C_INCLUDES += $(ART_C_INCLUDES) art/runtime
  LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)
  LOCAL_MULTILIB := both

  LOCAL_ADDITIONAL_DEPENDENCIES := art/build/Android.common_build.mk
  LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.mk
  LOCAL_NATIVE_COVERAGE := $(ART_COVERAGE)
  # For disassembler_arm64.
  ifeq ($$(art_static_or_shared),static)
    ifeq ($$(art_ndebug_or_debug),debug)
      LOCAL_STATIC_LIBRARIES += libvixld-arm64
    else
      LOCAL_STATIC_LIBRARIES += libvixl-arm64
    endif
    ifeq ($$(art_target_or_host),target)
      $$(error libart-disassembler static builds for target are not supported)
    else # host
      include $(BUILD_HOST_STATIC_LIBRARY)
    endif
  else # shared
    ifeq ($$(art_ndebug_or_debug),debug)
      LOCAL_SHARED_LIBRARIES += libvixld-arm64
    else
      LOCAL_SHARED_LIBRARIES += libvixl-arm64
    endif
    ifeq ($$(art_target_or_host),target)
      include $(BUILD_SHARED_LIBRARY)
    else # host
      include $(BUILD_HOST_SHARED_LIBRARY)
    endif
  endif

  # Clear out local variables now that we're done with them.
  art_target_or_host :=
  art_ndebug_or_debug :=
  art_static_or_shared :=
endef

ifeq ($(ART_BUILD_TARGET_NDEBUG),true)
  $(eval $(call build-libart-disassembler,target,ndebug,shared))
endif
ifeq ($(ART_BUILD_TARGET_DEBUG),true)
  $(eval $(call build-libart-disassembler,target,debug,shared))
endif
# We always build dex2oat and dependencies, even if the host build is
# otherwise disabled, since they are used to cross compile for the target.
ifeq ($(ART_BUILD_HOST_NDEBUG),true)
  $(eval $(call build-libart-disassembler,host,ndebug,shared))
  ifeq ($(ART_BUILD_HOST_STATIC),true)
    $(eval $(call build-libart-disassembler,host,ndebug,static))
  endif
endif
ifeq ($(ART_BUILD_HOST_DEBUG),true)
  $(eval $(call build-libart-disassembler,host,debug,shared))
  ifeq ($(ART_BUILD_HOST_STATIC),true)
    $(eval $(call build-libart-disassembler,host,debug,static))
  endif
endif
