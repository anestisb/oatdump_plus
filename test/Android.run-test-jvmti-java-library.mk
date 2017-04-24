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

# shim classes. We use one that exposes the common functionality.
LOCAL_SHIM_CLASSES := \
  902-hello-transformation/src/art/Redefinition.java \
  903-hello-tagging/src/art/Main.java \

LOCAL_SRC_FILES := $(LOCAL_SHIM_CLASSES)

# Actual test classes.
LOCAL_SRC_FILES += \
  901-hello-ti-agent/src/art/Test901.java \
  902-hello-transformation/src/art/Test902.java \
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
  912-classes/src/art/Test912.java \
    912-classes/src/art/DexData.java \
  913-heaps/src/art/Test913.java \
  914-hello-obsolescence/src/art/Test914.java \
  915-obsolete-2/src/art/Test915.java \
  917-fields-transformation/src/art/Test917.java \
  918-fields/src/art/Test918.java \
  919-obsolete-fields/src/art/Test919.java \
  920-objects/src/art/Test920.java \
  922-properties/src/art/Test922.java \
  923-monitors/src/art/Test923.java \
  924-threads/src/art/Test924.java \
  925-threadgroups/src/art/Test925.java \
  926-multi-obsolescence/src/art/Test926.java \
  927-timers/src/art/Test927.java \
  928-jni-table/src/art/Test928.java \
  930-hello-retransform/src/art/Test930.java \
  931-agent-thread/src/art/Test931.java \
  932-transform-saves/src/art/Test932.java \
  933-misc-events/src/art/Test933.java \
  940-recursive-obsolete/src/art/Test940.java \
  942-private-recursive/src/art/Test942.java \
  944-transform-classloaders/src/art/Test944.java \
  945-obsolete-native/src/art/Test945.java \
  947-reflect-method/src/art/Test947.java \
  951-threaded-obsolete/src/art/Test951.java \
  981-dedup-original-dex/src/art/Test981.java \
  982-ok-no-retransform/src/art/Test982.java \
  984-obsolete-invoke/src/art/Test984.java \
  985-re-obsolete/src/art/Test985.java \
  986-native-method-bind/src/art/Test986.java \

JVMTI_RUN_TEST_GENERATED_NUMBERS := \
  901 \
  902 \
  903 \
  904 \
  905 \
  906 \
  907 \
  908 \
  910 \
  911 \
  912 \
  913 \
  914 \
  915 \
  917 \
  918 \
  919 \
  920 \
  922 \
  923 \
  924 \
  925 \
  926 \
  927 \
  928 \
  930 \
  931 \
  932 \
  933 \
  940 \
  942 \
  944 \
  945 \
  947 \
  951 \
  981 \
  982 \
  984 \
  985 \
  986 \

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
