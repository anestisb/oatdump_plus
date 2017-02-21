#
# Copyright (C) 2015 The Android Open Source Project
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

include art/build/Android.common_path.mk

# --- ahat.jar ----------------
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, src)
LOCAL_JAR_MANIFEST := src/manifest.txt
LOCAL_JAVA_RESOURCE_FILES := \
  $(LOCAL_PATH)/src/style.css

LOCAL_STATIC_JAVA_LIBRARIES := perflib-prebuilt guavalib trove-prebuilt
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := ahat

# Let users with Java 7 run ahat (b/28303627)
LOCAL_JAVA_LANGUAGE_VERSION := 1.7

include $(BUILD_HOST_JAVA_LIBRARY)

# --- ahat script ----------------
include $(CLEAR_VARS)
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE := ahat
LOCAL_SRC_FILES := ahat
include $(BUILD_PREBUILT)

# --- ahat-tests.jar --------------
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, test)
LOCAL_JAR_MANIFEST := test/manifest.txt
LOCAL_STATIC_JAVA_LIBRARIES := ahat junit-host
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := ahat-tests
include $(BUILD_HOST_JAVA_LIBRARY)
AHAT_TEST_JAR := $(LOCAL_BUILT_MODULE)

# Rule to generate the proguard configuration for the test-dump program.
# We copy the configuration to the intermediates directory because jack will
# output the proguard map in that same directory.
AHAT_TEST_DUMP_PROGUARD_CONFIG := $(intermediates.COMMON)/config.pro
AHAT_TEST_DUMP_PROGUARD_MAP := $(intermediates.COMMON)/proguard.map
$(AHAT_TEST_DUMP_PROGUARD_CONFIG): PRIVATE_AHAT_PROGUARD_CONFIG_IN := $(LOCAL_PATH)/test-dump/config.pro
$(AHAT_TEST_DUMP_PROGUARD_CONFIG): PRIVATE_AHAT_PROGUARD_CONFIG := $(AHAT_TEST_DUMP_PROGUARD_CONFIG)
$(AHAT_TEST_DUMP_PROGUARD_CONFIG): $(LOCAL_PATH)/test-dump/config.pro
	cp $(PRIVATE_AHAT_PROGUARD_CONFIG_IN) $(PRIVATE_AHAT_PROGUARD_CONFIG)

# --- ahat-test-dump.jar --------------
include $(CLEAR_VARS)
LOCAL_MODULE := ahat-test-dump
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := $(call all-java-files-under, test-dump)
LOCAL_ADDITIONAL_DEPENDENCIES := $(AHAT_TEST_DUMP_PROGUARD_CONFIG)
LOCAL_JACK_FLAGS := --config-proguard $(AHAT_TEST_DUMP_PROGUARD_CONFIG)
include $(BUILD_HOST_DALVIK_JAVA_LIBRARY)

# Determine the location of the test-dump.jar and test-dump.hprof files.
# These use variables set implicitly by the include of
# BUILD_HOST_DALVIK_JAVA_LIBRARY above.
AHAT_TEST_DUMP_JAR := $(LOCAL_BUILT_MODULE)
AHAT_TEST_DUMP_HPROF := $(intermediates.COMMON)/test-dump.hprof
AHAT_TEST_DUMP_BASE_HPROF := $(intermediates.COMMON)/test-dump-base.hprof

# Run ahat-test-dump.jar to generate test-dump.hprof and test-dump-base.hprof
AHAT_TEST_DUMP_DEPENDENCIES := \
  $(ART_HOST_EXECUTABLES) \
  $(ART_HOST_SHARED_LIBRARY_DEPENDENCIES) \
  $(HOST_OUT_EXECUTABLES)/art \
  $(HOST_CORE_IMG_OUT_BASE)$(CORE_IMG_SUFFIX)

$(AHAT_TEST_DUMP_HPROF): PRIVATE_AHAT_TEST_ART := $(HOST_OUT_EXECUTABLES)/art
$(AHAT_TEST_DUMP_HPROF): PRIVATE_AHAT_TEST_DUMP_JAR := $(AHAT_TEST_DUMP_JAR)
$(AHAT_TEST_DUMP_HPROF): PRIVATE_AHAT_TEST_DUMP_DEPENDENCIES := $(AHAT_TEST_DUMP_DEPENDENCIES)
$(AHAT_TEST_DUMP_HPROF): $(AHAT_TEST_DUMP_JAR) $(AHAT_TEST_DUMP_DEPENDENCIES)
	$(PRIVATE_AHAT_TEST_ART) -cp $(PRIVATE_AHAT_TEST_DUMP_JAR) Main $@

$(AHAT_TEST_DUMP_BASE_HPROF): PRIVATE_AHAT_TEST_ART := $(HOST_OUT_EXECUTABLES)/art
$(AHAT_TEST_DUMP_BASE_HPROF): PRIVATE_AHAT_TEST_DUMP_JAR := $(AHAT_TEST_DUMP_JAR)
$(AHAT_TEST_DUMP_BASE_HPROF): PRIVATE_AHAT_TEST_DUMP_DEPENDENCIES := $(AHAT_TEST_DUMP_DEPENDENCIES)
$(AHAT_TEST_DUMP_BASE_HPROF): $(AHAT_TEST_DUMP_JAR) $(AHAT_TEST_DUMP_DEPENDENCIES)
	$(PRIVATE_AHAT_TEST_ART) -cp $(PRIVATE_AHAT_TEST_DUMP_JAR) Main $@ --base

.PHONY: ahat-test
ahat-test: PRIVATE_AHAT_TEST_DUMP_HPROF := $(AHAT_TEST_DUMP_HPROF)
ahat-test: PRIVATE_AHAT_TEST_DUMP_BASE_HPROF := $(AHAT_TEST_DUMP_BASE_HPROF)
ahat-test: PRIVATE_AHAT_TEST_JAR := $(AHAT_TEST_JAR)
ahat-test: PRIVATE_AHAT_PROGUARD_MAP := $(AHAT_TEST_DUMP_PROGUARD_MAP)
ahat-test: $(AHAT_TEST_JAR) $(AHAT_TEST_DUMP_HPROF) $(AHAT_TEST_DUMP_BASE_HPROF)
	java -enableassertions -Dahat.test.dump.hprof=$(PRIVATE_AHAT_TEST_DUMP_HPROF) -Dahat.test.dump.base.hprof=$(PRIVATE_AHAT_TEST_DUMP_BASE_HPROF) -Dahat.test.dump.map=$(PRIVATE_AHAT_PROGUARD_MAP) -jar $(PRIVATE_AHAT_TEST_JAR)

# Clean up local variables.
AHAT_TEST_DUMP_DEPENDENCIES :=
AHAT_TEST_DUMP_HPROF :=
AHAT_TEST_DUMP_JAR :=
AHAT_TEST_DUMP_PROGUARD_CONFIG :=
AHAT_TEST_DUMP_PROGUARD_MAP :=
AHAT_TEST_JAR :=

