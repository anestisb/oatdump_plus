# Copyright 2017, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import re
import tempfile
import subprocess

env = dict(os.environ)

def getEnvBoolean(var, default):
  val = env.get(var)
  if val:
    if val == "True" or val == "true":
      return True
    if val == "False" or val == "false":
      return False
  return default

_DUMP_MANY_VARS_LIST = ['HOST_2ND_ARCH_PREFIX',
                        'TARGET_2ND_ARCH',
                        'TARGET_ARCH',
                        'HOST_PREFER_32_BIT',
                        'HOST_OUT_EXECUTABLES']
_DUMP_MANY_VARS = None  # To be set to a dictionary with above list being the keys,
                        # and the build variable being the value.
def dump_many_vars(var_name):
  """
  Reach into the Android build system to dump many build vars simultaneously.
  Since the make system is so slow, we want to avoid calling into build frequently.
  """
  global _DUMP_MANY_VARS
  global _DUMP_MANY_VARS_LIST

  # Look up var from cache.
  if _DUMP_MANY_VARS:
    return _DUMP_MANY_VARS[var_name]

  all_vars=" ".join(_DUMP_MANY_VARS_LIST)

  # The command is taken from build/envsetup.sh to fetch build variables.
  command = ("CALLED_FROM_SETUP=true "  # Enable the 'dump-many-vars' make target.
             "BUILD_SYSTEM=build/core " # Set up lookup path for make includes.
             "make --no-print-directory -C \"%s\" -f build/core/config.mk "
             "dump-many-vars DUMP_MANY_VARS=\"%s\"") % (ANDROID_BUILD_TOP, all_vars)

  config = subprocess.Popen(command,
                            stdout=subprocess.PIPE,
                            universal_newlines=True,
                            shell=True).communicate()[0] # read until EOF, select stdin
  # Prints out something like:
  # TARGET_ARCH='arm64'
  # HOST_ARCH='x86_64'
  _DUMP_MANY_VARS = {}
  for line in config.split("\n"):
    # Split out "$key='$value'" via regex.
    match = re.search("([^=]+)='([^']*)", line)
    if not match:
      continue
    key = match.group(1)
    value = match.group(2)
    _DUMP_MANY_VARS[key] = value

  return _DUMP_MANY_VARS[var_name]

def get_build_var(var_name):
  return dump_many_vars(var_name)

def get_env(key):
  return env.get(key)

ANDROID_BUILD_TOP = env.get('ANDROID_BUILD_TOP', os.getcwd())

# Directory used for temporary test files on the host.
ART_HOST_TEST_DIR = tempfile.mkdtemp(prefix = 'test-art-')

# Keep going after encountering a test failure?
ART_TEST_KEEP_GOING = getEnvBoolean('ART_TEST_KEEP_GOING', True)

# Do you want all tests, even those that are time consuming?
ART_TEST_FULL = getEnvBoolean('ART_TEST_FULL', False)

# Do you want interpreter tests run?
ART_TEST_INTERPRETER = getEnvBoolean('ART_TEST_INTERPRETER', ART_TEST_FULL)
ART_TEST_INTERPRETER_ACCESS_CHECKS = getEnvBoolean('ART_TEST_INTERPRETER_ACCESS_CHECKS',
                                                   ART_TEST_FULL)

# Do you want JIT tests run?
ART_TEST_JIT = getEnvBoolean('ART_TEST_JIT', ART_TEST_FULL)

# Do you want optimizing compiler tests run?
ART_TEST_OPTIMIZING = getEnvBoolean('ART_TEST_OPTIMIZING', ART_TEST_FULL)

# Do you want to test the optimizing compiler with graph coloring register allocation?
ART_TEST_OPTIMIZING_GRAPH_COLOR = getEnvBoolean('ART_TEST_OPTIMIZING_GRAPH_COLOR', ART_TEST_FULL)

# Do we want to test PIC-compiled tests ("apps")?
ART_TEST_PIC_TEST = getEnvBoolean('ART_TEST_PIC_TEST', ART_TEST_FULL)
# Do you want tracing tests run?
ART_TEST_TRACE = getEnvBoolean('ART_TEST_TRACE', ART_TEST_FULL)

# Do you want tracing tests (streaming mode) run?
ART_TEST_TRACE_STREAM = getEnvBoolean('ART_TEST_TRACE_STREAM', ART_TEST_FULL)

# Do you want tests with GC verification enabled run?
ART_TEST_GC_VERIFY = getEnvBoolean('ART_TEST_GC_VERIFY', ART_TEST_FULL)

# Do you want tests with the GC stress mode enabled run?
ART_TEST_GC_STRESS = getEnvBoolean('ART_TEST_GC_STRESS', ART_TEST_FULL)

# Do you want tests with the JNI forcecopy mode enabled run?
ART_TEST_JNI_FORCECOPY = getEnvBoolean('ART_TEST_JNI_FORCECOPY', ART_TEST_FULL)

# Do you want run-tests with relocation disabled run?
ART_TEST_RUN_TEST_RELOCATE = getEnvBoolean('ART_TEST_RUN_TEST_RELOCATE', ART_TEST_FULL)

# Do you want run-tests with prebuilding?
ART_TEST_RUN_TEST_PREBUILD = getEnvBoolean('ART_TEST_RUN_TEST_PREBUILD', ART_TEST_FULL)

# Do you want run-tests with no prebuilding enabled run?
ART_TEST_RUN_TEST_NO_PREBUILD = getEnvBoolean('ART_TEST_RUN_TEST_NO_PREBUILD', ART_TEST_FULL)

# Do you want run-tests with a pregenerated core.art?
ART_TEST_RUN_TEST_IMAGE = getEnvBoolean('ART_TEST_RUN_TEST_IMAGE', ART_TEST_FULL)

# Do you want run-tests without a pregenerated core.art?
ART_TEST_RUN_TEST_NO_IMAGE = getEnvBoolean('ART_TEST_RUN_TEST_NO_IMAGE', ART_TEST_FULL)

# Do you want run-tests with relocation enabled but patchoat failing?
ART_TEST_RUN_TEST_RELOCATE_NO_PATCHOAT = getEnvBoolean('ART_TEST_RUN_TEST_RELOCATE_NO_PATCHOAT',
                                                       ART_TEST_FULL)

# Do you want run-tests without a dex2oat?
ART_TEST_RUN_TEST_NO_DEX2OAT = getEnvBoolean('ART_TEST_RUN_TEST_NO_DEX2OAT', ART_TEST_FULL)

# Do you want run-tests with libartd.so?
ART_TEST_RUN_TEST_DEBUG = getEnvBoolean('ART_TEST_RUN_TEST_DEBUG', ART_TEST_FULL)

# Do you want run-tests with libart.so?
ART_TEST_RUN_TEST_NDEBUG = getEnvBoolean('ART_TEST_RUN_TEST_NDEBUG', ART_TEST_FULL)

# Do you want failed tests to have their artifacts cleaned up?
ART_TEST_RUN_TEST_ALWAYS_CLEAN = getEnvBoolean('ART_TEST_RUN_TEST_ALWAYS_CLEAN', True)

# Do you want run-tests with the --debuggable flag
ART_TEST_RUN_TEST_DEBUGGABLE = getEnvBoolean('ART_TEST_RUN_TEST_DEBUGGABLE', ART_TEST_FULL)

# Do you want to test multi-part boot-image functionality?
ART_TEST_RUN_TEST_MULTI_IMAGE = getEnvBoolean('ART_TEST_RUN_TEST_MULTI_IMAGE', ART_TEST_FULL)

ART_TEST_DEBUG_GC = getEnvBoolean('ART_TEST_DEBUG_GC', False)

ART_TEST_BISECTION = getEnvBoolean('ART_TEST_BISECTION', False)

DEX2OAT_HOST_INSTRUCTION_SET_FEATURES = env.get('DEX2OAT_HOST_INSTRUCTION_SET_FEATURES')

# Do you want run-tests with the host/target's second arch?
ART_TEST_RUN_TEST_2ND_ARCH = getEnvBoolean('ART_TEST_RUN_TEST_2ND_ARCH', True)

HOST_2ND_ARCH_PREFIX = get_build_var('HOST_2ND_ARCH_PREFIX')
HOST_2ND_ARCH_PREFIX_DEX2OAT_HOST_INSTRUCTION_SET_FEATURES = env.get(
  HOST_2ND_ARCH_PREFIX + 'DEX2OAT_HOST_INSTRUCTION_SET_FEATURES')

ART_TEST_ANDROID_ROOT = env.get('ART_TEST_ANDROID_ROOT')

ART_TEST_WITH_STRACE = getEnvBoolean('ART_TEST_DEBUG_GC', False)

EXTRA_DISABLED_TESTS = set(env.get("ART_TEST_RUN_TEST_SKIP", "").split())

ART_TEST_RUN_TEST_BUILD = getEnvBoolean('ART_TEST_RUN_TEST_BUILD', False)

TARGET_2ND_ARCH = get_build_var('TARGET_2ND_ARCH')
TARGET_ARCH = get_build_var('TARGET_ARCH')
if TARGET_2ND_ARCH:
  if "64" in TARGET_ARCH:
    ART_PHONY_TEST_TARGET_SUFFIX = "64"
    _2ND_ART_PHONY_TEST_TARGET_SUFFIX = "32"
  else:
    ART_PHONY_TEST_TARGET_SUFFIX = "32"
    _2ND_ART_PHONY_TEST_TARGET_SUFFIX = ""
else:
  if "64" in TARGET_ARCH:
    ART_PHONY_TEST_TARGET_SUFFIX = "64"
    _2ND_ART_PHONY_TEST_TARGET_SUFFIX = ""
  else:
    ART_PHONY_TEST_TARGET_SUFFIX = "32"
    _2ND_ART_PHONY_TEST_TARGET_SUFFIX = ""

HOST_PREFER_32_BIT = get_build_var('HOST_PREFER_32_BIT')
if HOST_PREFER_32_BIT == "true":
  ART_PHONY_TEST_HOST_SUFFIX = "32"
  _2ND_ART_PHONY_TEST_HOST_SUFFIX = ""
else:
  ART_PHONY_TEST_HOST_SUFFIX = "64"
  _2ND_ART_PHONY_TEST_HOST_SUFFIX = "32"

HOST_OUT_EXECUTABLES = os.path.join(ANDROID_BUILD_TOP,
                                    get_build_var("HOST_OUT_EXECUTABLES"))
os.environ['JACK'] = HOST_OUT_EXECUTABLES + '/jack'
os.environ['DX'] = HOST_OUT_EXECUTABLES + '/dx'
os.environ['SMALI'] = HOST_OUT_EXECUTABLES + '/smali'
os.environ['JASMIN'] = HOST_OUT_EXECUTABLES + '/jasmin'
os.environ['DXMERGER'] = HOST_OUT_EXECUTABLES + '/dexmerger'
