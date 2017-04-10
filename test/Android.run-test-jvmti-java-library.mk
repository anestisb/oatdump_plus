#
# Copyright (C) 2017 The Android Open Source Project
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

include $(CLEAR_VARS)

# Main shim classes. We use one that exposes the tagging common functionality.
LOCAL_MAIN_SHIM := 903-hello-tagging/src/art/Main.java
LOCAL_SRC_FILES := $(LOCAL_MAIN_SHIM)

# Actual test classes.
LOCAL_SRC_FILES += \
  901-hello-ti-agent/src/art/Test901.java \
  903-hello-tagging/src/art/Test903.java \
  904-object-allocation/src/art/Test904.java \
  905-object-free/src/art/Test905.java \
  906-iterate-heap/src/art/Test906.java \
  907-get-loaded-classes/src/art/Test907.java \
  908-gc-start-finish/src/art/Test908.java \
  910-methods/src/art/Test910.java \
  911-get-stack-trace/src/art/Test911.java \
    911-get-stack-trace/src/art/AllTraces.java \
    911-get-stack-trace/src/art/ControlData.java \
    911-get-stack-trace/src/art/Frames.java \
    911-get-stack-trace/src/art/OtherThread.java \
    911-get-stack-trace/src/art/PrintThread.java \
    911-get-stack-trace/src/art/Recurse.java \
    911-get-stack-trace/src/art/SameThread.java \
    911-get-stack-trace/src/art/ThreadListTraces.java \
  913-heaps/src/art/Test913.java \
  918-fields/src/art/Test918.java \
  920-objects/src/art/Test920.java \
  922-properties/src/art/Test922.java \
  923-monitors/src/art/Test923.java \
  924-threads/src/art/Test924.java \
  925-threadgroups/src/art/Test925.java \
  927-timers/src/art/Test927.java \
  928-jni-table/src/art/Test928.java \
  931-agent-thread/src/art/Test931.java \
  933-misc-events/src/art/Test933.java \

JVMTI_RUN_TEST_GENERATED_NUMBERS := \
  901 \
  903 \
  904 \
  905 \
  906 \
  907 \
  908 \
  910 \
  911 \
  913 \
  918 \
  920 \
  922 \
  923 \
  924 \
  925 \
  927 \
  928 \
  931 \
  933 \

# Try to enforce that the directories correspond to the Java files we pull in.
JVMTI_RUN_TEST_DIR_CHECK := $(sort $(foreach DIR,$(JVMTI_RUN_TEST_GENERATED_NUMBERS), \
  $(filter $(DIR)%,$(LOCAL_SRC_FILES))))
ifneq ($(sort $(LOCAL_SRC_FILES)),$(JVMTI_RUN_TEST_DIR_CHECK))
  $(error Missing file, compare $(sort $(LOCAL_SRC_FILES)) with $(JVMTI_RUN_TEST_DIR_CHECK))
endif

LOCAL_MODULE_CLASS := JAVA_LIBRARIES
LOCAL_MODULE_TAGS := optional
LOCAL_JAVA_LANGUAGE_VERSION := 1.8
LOCAL_MODULE := run-test-jvmti-java

GENERATED_SRC_DIR := $(call local-generated-sources-dir)
JVMTI_RUN_TEST_GENERATED_FILES := \
  $(foreach NR,$(JVMTI_RUN_TEST_GENERATED_NUMBERS),$(GENERATED_SRC_DIR)/results.$(NR).expected.txt)

define GEN_JVMTI_RUN_TEST_GENERATED_FILE

GEN_INPUT := $(wildcard $(LOCAL_PATH)/$(1)*/expected.txt)
GEN_OUTPUT := $(GENERATED_SRC_DIR)/results.$(1).expected.txt
$$(GEN_OUTPUT): $$(GEN_INPUT)
	cp $$< $$@

GEN_INPUT :=
GEN_OUTPUT :=

endef

$(foreach NR,$(JVMTI_RUN_TEST_GENERATED_NUMBERS),\
  $(eval $(call GEN_JVMTI_RUN_TEST_GENERATED_FILE,$(NR))))
LOCAL_JAVA_RESOURCE_FILES := $(JVMTI_RUN_TEST_GENERATED_FILES)

include $(BUILD_JAVA_LIBRARY)
