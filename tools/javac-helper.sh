#!/bin/bash
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
# Calls javac with the -bootclasspath values passed in automatically.
# (This avoids having to manually set a boot class path).
#
#
# Script-specific args:
#   --mode=[host|target]: Select between host or target bootclasspath (default target).
#   --core-only:          Use only "core" bootclasspath (e.g. do not include framework).
#   --show-commands:      Print the desugar command being executed.
#   --help:               Print above list of args.
#
# All other args are forwarded to javac
#

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TOP=$DIR/../..

if [[ -z $JAVAC ]]; then
  JAVAC=javac
fi

bootjars_args=
mode=target
showcommands=n
while true; do
  case $1 in
    --help)
      echo "Usage: $0 [--mode=host|target] [--core-only] [--show-commands] <javac args>"
      exit 0
      ;;
    --mode=host)
      bootjars_args="$bootjars_args --host"
      ;;
    --mode=target)
      bootjars_args="$bootjars_args --target"
      ;;
    --core-only)
      bootjars_args="$bootjars_args --core"
      ;;
    --show-commands)
      showcommands=y
      ;;
    *)
      break
      ;;
  esac
  shift
done

javac_bootclasspath=()
boot_class_path_list=$($TOP/art/tools/bootjars.sh $bootjars_args --path)


for path in $boot_class_path_list; do
  javac_bootclasspath+=("$path")
done

if [[ ${#javac_bootclasspath[@]} -eq 0 ]]; then
  echo "FATAL: Missing bootjars.sh file path list" >&2
  exit 1
fi

function join_by { local IFS="$1"; shift; echo "$*"; }
bcp_arg="$(join_by ":" "${javac_bootclasspath[@]}")"
javac_args=(-bootclasspath "$bcp_arg")

if [[ $showcommands == y ]]; then
  echo ${JAVAC} "${javac_args[@]}" "$@"
fi

${JAVAC} "${javac_args[@]}" "$@"
