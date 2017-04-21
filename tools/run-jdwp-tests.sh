#!/bin/bash
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

if [ ! -d libcore ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

if [ -z "$ANDROID_HOST_OUT" ] ; then
  ANDROID_HOST_OUT=${OUT_DIR-$ANDROID_BUILD_TOP/out}/host/linux-x86
fi

# Jar containing all the tests.
test_jack=${ANDROID_HOST_OUT}/../common/obj/JAVA_LIBRARIES/apache-harmony-jdwp-tests-hostdex_intermediates/classes.jack

if [ ! -f $test_jack ]; then
  echo "Before running, you must build jdwp tests and vogar:" \
       "make apache-harmony-jdwp-tests-hostdex vogar"
  exit 1
fi

art="/data/local/tmp/system/bin/art"
art_debugee="sh /data/local/tmp/system/bin/art"
args=$@
debuggee_args="-Xcompiler-option --debuggable"
device_dir="--device-dir=/data/local/tmp"
# We use the art script on target to ensure the runner and the debuggee share the same
# image.
vm_command="--vm-command=$art"
image_compiler_option=""
debug="no"
verbose="no"
image="-Ximage:/data/art-test/core.art"
vm_args=""
# By default, we run the whole JDWP test suite.
test="org.apache.harmony.jpda.tests.share.AllTests"
host="no"
# Use JIT compiling by default.
use_jit=true
variant_cmdline_parameter="--variant=X32"
# Timeout of JDWP test in ms.
#
# Note: some tests expect a timeout to check that *no* reply/event is received for a specific case.
# A lower timeout can save up several minutes when running the whole test suite, especially for
# continuous testing. This value can be adjusted to fit the configuration of the host machine(s).
jdwp_test_timeout=10000

while true; do
  if [[ "$1" == "--mode=host" ]]; then
    host="yes"
    # Specify bash explicitly since the art script cannot, since it has to run on the device
    # with mksh.
    art="bash ${OUT_DIR-out}/host/linux-x86/bin/art"
    art_debugee="bash ${OUT_DIR-out}/host/linux-x86/bin/art"
    # We force generation of a new image to avoid build-time and run-time classpath differences.
    image="-Ximage:/system/non/existent/vogar.art"
    # We do not need a device directory on host.
    device_dir=""
    # Vogar knows which VM to use on host.
    vm_command=""
    shift
  elif [[ $1 == -Ximage:* ]]; then
    image="$1"
    shift
  elif [[ "$1" == "--no-jit" ]]; then
    use_jit=false
    # Remove the --no-jit from the arguments.
    args=${args/$1}
    shift
  elif [[ $1 == "--debug" ]]; then
    debug="yes"
    # Remove the --debug from the arguments.
    args=${args/$1}
    shift
  elif [[ $1 == "--verbose" ]]; then
    verbose="yes"
    # Remove the --verbose from the arguments.
    args=${args/$1}
    shift
  elif [[ $1 == "--test" ]]; then
    # Remove the --test from the arguments.
    args=${args/$1}
    shift
    test=$1
    # Remove the test from the arguments.
    args=${args/$1}
    shift
  elif [[ "$1" == "" ]]; then
    break
  elif [[ $1 == --variant=* ]]; then
    variant_cmdline_parameter=$1
    shift
  else
    shift
  fi
done

# For the host:
#
# If, on the other hand, there is a variant set, use it to modify the art_debugee parameter to
# force the fork to have the same bitness as the controller. This should be fine and not impact
# testing (cross-bitness), as the protocol is always 64-bit anyways (our implementation).
#
# Note: this isn't necessary for the device as the BOOTCLASSPATH environment variable is set there
#       and used as a fallback.
if [[ $host == "yes" ]]; then
  variant=${variant_cmdline_parameter:10}
  if [[ $variant == "x32" || $variant == "X32" ]]; then
    art_debugee="$art_debugee --32"
  elif [[ $variant == "x64" || $variant == "X64" ]]; then
    art_debugee="$art_debugee --64"
  else
    echo "Error, do not understand variant $variant_cmdline_parameter."
    exit 1
  fi
fi

if [[ "$image" != "" ]]; then
  vm_args="--vm-arg $image"
fi
if $use_jit; then
  vm_args="$vm_args --vm-arg -Xcompiler-option --vm-arg --compiler-filter=quicken"
  debuggee_args="$debuggee_args -Xcompiler-option --compiler-filter=quicken"
fi
vm_args="$vm_args --vm-arg -Xusejit:$use_jit"
debuggee_args="$debuggee_args -Xusejit:$use_jit"
if [[ $debug == "yes" ]]; then
  art="$art -d"
  art_debugee="$art_debugee -d"
  vm_args="$vm_args --vm-arg -XXlib:libartd.so"
fi
if [[ $verbose == "yes" ]]; then
  # Enable JDWP logs in the debuggee.
  art_debugee="$art_debugee -verbose:jdwp"
fi

# Run the tests using vogar.
vogar $vm_command \
      $vm_args \
      --verbose \
      $args \
      $device_dir \
      $image_compiler_option \
      --timeout 800 \
      --vm-arg -Djpda.settings.verbose=true \
      --vm-arg -Djpda.settings.timeout=$jdwp_test_timeout \
      --vm-arg -Djpda.settings.waitingTime=$jdwp_test_timeout \
      --vm-arg -Djpda.settings.transportAddress=127.0.0.1:55107 \
      --vm-arg -Djpda.settings.debuggeeJavaPath="$art_debugee $image $debuggee_args" \
      --classpath $test_jack \
      --toolchain jack --language JN \
      --vm-arg -Xcompiler-option --vm-arg --debuggable \
      --jack-arg -g \
      $test

vogar_exit_status=$?

echo "Killing stalled dalvikvm processes..."
if [[ $host == "yes" ]]; then
  pkill -9 -f /bin/dalvikvm
else
  adb shell pkill -9 -f /bin/dalvikvm
fi
echo "Done."

exit $vogar_exit_status
