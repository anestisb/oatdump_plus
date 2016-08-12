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


LOCAL_PATH := $(call my-dir)

include art/build/Android.common_build.mk

LIBARTAGENT_COMMON_SRC_FILES := \
    900-hello-plugin/load_unload.cc

# $(1): target or host
# $(2): debug or <empty>
define build-libartagent
  ifneq ($(1),target)
    ifneq ($(1),host)
      $$(error expected target or host for argument 1, received $(1))
    endif
  endif
  ifneq ($(2),debug)
    ifneq ($(2),)
      $$(error d or empty for argument 2, received $(2))
    endif
    suffix := d
  else
    suffix :=
  endif

  art_target_or_host := $(1)

  include $(CLEAR_VARS)
  LOCAL_CPP_EXTENSION := $(ART_CPP_EXTENSION)
  LOCAL_MODULE := libartagent$$(suffix)
  ifeq ($$(art_target_or_host),target)
    LOCAL_MODULE_TAGS := tests
  endif
  LOCAL_SRC_FILES := $(LIBARTAGENT_COMMON_SRC_FILES)
  LOCAL_SHARED_LIBRARIES += libart$$(suffix) libbacktrace libnativehelper
  LOCAL_C_INCLUDES += $(ART_C_INCLUDES) art/runtime
  LOCAL_ADDITIONAL_DEPENDENCIES := art/build/Android.common_build.mk
  LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.libartagent.mk
  ifeq ($$(art_target_or_host),target)
    $(call set-target-local-clang-vars)
    ifeq ($$(suffix),d)
      $(call set-target-local-cflags-vars,debug)
    else
      $(call set-target-local-cflags-vars,ndebug)
    endif
    LOCAL_SHARED_LIBRARIES += libdl
    LOCAL_MULTILIB := both
    LOCAL_MODULE_PATH_32 := $(ART_TARGET_TEST_OUT)/$(ART_TARGET_ARCH_32)
    LOCAL_MODULE_PATH_64 := $(ART_TARGET_TEST_OUT)/$(ART_TARGET_ARCH_64)
    LOCAL_MODULE_TARGET_ARCH := $(ART_SUPPORTED_ARCH)
    include $(BUILD_SHARED_LIBRARY)
  else # host
    LOCAL_CLANG := $(ART_HOST_CLANG)
    LOCAL_CFLAGS := $(ART_HOST_CFLAGS)
    LOCAL_ASFLAGS := $(ART_HOST_ASFLAGS)
    ifeq ($$(suffix),d)
      LOCAL_CFLAGS += $(ART_HOST_DEBUG_CFLAGS)
      LOCAL_ASFLAGS += $(ART_HOST_DEBUG_ASFLAGS)
    else
      LOCAL_CFLAGS += $(ART_HOST_NON_DEBUG_CFLAGS)
      LOCAL_ASFLAGS += $(ART_HOST_NON_DEBUG_ASFLAGS)
    endif
    LOCAL_LDLIBS := $(ART_HOST_LDLIBS) -ldl -lpthread
    LOCAL_IS_HOST_MODULE := true
    LOCAL_MULTILIB := both
    include $(BUILD_HOST_SHARED_LIBRARY)
  endif

  # Clear locally used variables.
  art_target_or_host :=
  suffix :=
endef

ifeq ($(ART_BUILD_TARGET),true)
  $(eval $(call build-libartagent,target,))
  $(eval $(call build-libartagent,target,debug))
endif
ifeq ($(ART_BUILD_HOST),true)
  $(eval $(call build-libartagent,host,))
  $(eval $(call build-libartagent,host,debug))
endif

# Clear locally used variables.
LOCAL_PATH :=
LIBARTAGENT_COMMON_SRC_FILES :=
