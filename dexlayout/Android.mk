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

# TODO(sehr): Art-i-fy this makefile

LOCAL_PATH:= $(call my-dir)

dexlayout_src_files := dexlayout_main.cc dexlayout.cc dex_ir.cc
dexlayout_c_includes := art/runtime
dexlayout_libraries := libart

##
## Build the device command line tool dexlayout.
##

ifneq ($(SDK_ONLY),true)  # SDK_only doesn't need device version
include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := cc
LOCAL_SRC_FILES := $(dexlayout_src_files)
LOCAL_C_INCLUDES := $(dexlayout_c_includes)
LOCAL_CFLAGS += -Wall
LOCAL_SHARED_LIBRARIES += $(dexlayout_libraries)
LOCAL_MODULE := dexlayout
include $(BUILD_EXECUTABLE)
endif # !SDK_ONLY

##
## Build the host command line tool dexlayout.
##

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := cc
LOCAL_SRC_FILES := $(dexlayout_src_files)
LOCAL_C_INCLUDES := $(dexlayout_c_includes)
LOCAL_CFLAGS += -Wall
LOCAL_SHARED_LIBRARIES += $(dexlayout_libraries)
LOCAL_MODULE := dexlayout
LOCAL_MULTILIB := $(ART_MULTILIB_OVERRIDE_host)
include $(BUILD_HOST_EXECUTABLE)
