#!/bin/sh
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
# Note: Requires $ANDROID_BUILD_TOP/build/envsetup.sh to have been run.
#
# This script takes in a logcat containing Sanitizer traces and outputs several
# files, prints information regarding the traces, and plots information as well.
ALL_PIDS=false
USE_TEMP=true
DO_REDO=false
PACKAGE_NAME=""
# EXACT_ARG and MIN_ARG are passed to prune_sanitizer_output.py
EXACT_ARG=""
MIN_ARG=""
OFFSET_ARGS=""
TIME_ARGS=""
usage() {
  echo "Usage: $0 [options] [LOGCAT_FILE] [CATEGORIES...]"
  echo "    -a"
  echo "        Forces all pids associated with registered dex"
  echo "        files in the logcat to be processed."
  echo "        default: only the last pid is processed"
  echo
  echo "    -d  OUT_DIRECTORY"
  echo "        Puts all output in specified directory."
  echo "        If not given, output will be put in a local"
  echo "        temp folder which will be deleted after"
  echo "        execution."
  echo
  echo "    -e"
  echo "        All traces will have exactly the same number"
  echo "        of categories which is specified by either"
  echo "        the -m argument or by prune_sanitizer_output.py"
  echo
  echo "    -f"
  echo "        Forces redo of all commands even if output"
  echo "        files exist. Steps are skipped if their output"
  echo "        exist already and this is not enabled."
  echo
  echo "    -m  [MINIMUM_CALLS_PER_TRACE]"
  echo "        Filters out all traces that do not have"
  echo "        at least MINIMUM_CALLS_PER_TRACE lines."
  echo "        default: specified by prune_sanitizer_output.py"
  echo
  echo "    -o  [OFFSET],[OFFSET]"
  echo "        Filters out all Dex File offsets outside the"
  echo "        range between provided offsets. 'inf' can be"
  echo "        provided for infinity."
  echo "        default: 0,inf"
  echo
  echo "    -p  [PACKAGE_NAME]"
  echo "        Using the package name, uses baksmali to get"
  echo "        a dump of the Dex File format for the package."
  echo
  echo "    -t  [TIME_OFFSET],[TIME_OFFSET]"
  echo "        Filters out all time offsets outside the"
  echo "        range between provided offsets. 'inf' can be"
  echo "        provided for infinity."
  echo "        default: 0,inf"
  echo
  echo "    CATEGORIES are words that are expected to show in"
  echo "       a large subset of symbolized traces. Splits"
  echo "       output based on each word."
  echo
  echo "    LOGCAT_FILE is the piped output from adb logcat."
  echo
}


while getopts ":ad:efm:o:p:t:" opt ; do
case ${opt} in
  a)
    ALL_PIDS=true
    ;;
  d)
    USE_TEMP=false
    OUT_DIR=$OPTARG
    ;;
  e)
    EXACT_ARG='-e'
    ;;
  f)
    DO_REDO=true
    ;;
  m)
    if ! [ "$OPTARG" -eq "$OPTARG" ]; then
      usage
      exit
    fi
    MIN_ARG='-m '"$OPTARG"
    ;;
  o)
    set -f
    OLD_IFS=$IFS
    IFS=","
    OFFSET_ARGS=( $OPTARG )
    if [ "${#OFFSET_ARGS[@]}" -ne 2 ]; then
      usage
      exit
    fi
    OFFSET_ARGS=( "--offsets" "${OFFSET_ARGS[@]}" )
    IFS=$OLD_IFS
    ;;
  t)
    set -f
    OLD_IFS=$IFS
    IFS=","
    TIME_ARGS=( $OPTARG )
    if [ "${#TIME_ARGS[@]}" -ne 2 ]; then
      usage
      exit
    fi
    TIME_ARGS=( "--times" "${TIME_ARGS[@]}" )
    IFS=$OLD_IFS
    ;;
  p)
    PACKAGE_NAME=$OPTARG
    ;;
  \?)
    usage
    exit
esac
done
shift $((OPTIND -1))

if [ $# -lt 1 ]; then
  usage
  exit
fi

LOGCAT_FILE=$1
NUM_CAT=$(($# - 1))

# Use a temp directory that will be deleted
if [ $USE_TEMP = true ]; then
  OUT_DIR=$(mktemp -d --tmpdir=$PWD)
  DO_REDO=true
fi

if [ ! -d "$OUT_DIR" ]; then
  mkdir $OUT_DIR
  DO_REDO=true
fi

# Note: Steps are skipped if their output exists until -f flag is enabled
echo "Output folder: $OUT_DIR"
unique_pids=( $(grep "RegisterDexFile" "$LOGCAT_FILE" | grep -v "zygote64" | tr -s ' ' | cut -f3 -d' ' | awk '!a[$0]++') )
echo "List of pids: ${unique_pids[@]}"
if [ $ALL_PIDS = false ]; then
  unique_pids=( ${unique_pids[-1]} )
fi

for pid in "${unique_pids[@]}"
do
  echo
  echo "Current pid: $pid"
  echo
  PID_DIR=$OUT_DIR/$pid
  if [ ! -d "$PID_DIR" ]; then
    mkdir $PID_DIR
    DO_REDO[$pid]=true
  fi

  INTERMEDIATES_DIR=$PID_DIR/intermediates
  RESULTS_DIR=$PID_DIR/results
  LOGCAT_PID_FILE=$PID_DIR/logcat

  if [ ! -f "$PID_DIR/logcat" ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    awk '{if($3 == '$pid') print $0}' $LOGCAT_FILE > $LOGCAT_PID_FILE
  fi

  if [ ! -d "$INTERMEDIATES_DIR" ]; then
    mkdir $INTERMEDIATES_DIR
    DO_REDO[$pid]=true
  fi

  # Step 1 - Only output lines related to Sanitizer
  # Folder that holds all file output
  ASAN_OUT=$INTERMEDIATES_DIR/asan_output
  if [ ! -f $ASAN_OUT ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    echo "Extracting ASAN output"
    grep "app_process64" $LOGCAT_PID_FILE > $ASAN_OUT
  else
    echo "Skipped: Extracting ASAN output"
  fi

  # Step 2 - Only output lines containing Dex File Start Addresses
  DEX_START=$INTERMEDIATES_DIR/dex_start
  if [ ! -f $DEX_START ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    echo "Extracting Start of Dex File(s)"
    grep "RegisterDexFile" $LOGCAT_PID_FILE > $DEX_START
  else
    echo "Skipped: Extracting Start of Dex File(s)"
  fi

  # Step 3 - Clean Sanitizer output from Step 2 since logcat cannot
  # handle large amounts of output.
  ASAN_OUT_FILTERED=$INTERMEDIATES_DIR/asan_output_filtered
  if [ ! -f $ASAN_OUT_FILTERED ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    echo "Filtering/Cleaning ASAN output"
    python $ANDROID_BUILD_TOP/art/tools/runtime_memusage/prune_sanitizer_output.py \
    $EXACT_ARG $MIN_ARG -d $INTERMEDIATES_DIR $ASAN_OUT
  else
    echo "Skipped: Filtering/Cleaning ASAN output"
  fi

  # Step 4 - Retrieve symbolized stack traces from Step 3 output
  SYM_FILTERED=$INTERMEDIATES_DIR/sym_filtered
  if [ ! -f $SYM_FILTERED ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    echo "Retrieving symbolized traces"
    $ANDROID_BUILD_TOP/development/scripts/stack $ASAN_OUT_FILTERED > $SYM_FILTERED
  else
    echo "Skipped: Retrieving symbolized traces"
  fi

  # Step 4.5 - Obtain Dex File Format of dex file related to package
  BAKSMALI_DMP_OUT="$INTERMEDIATES_DIR""/baksmali_dex_file"
  BAKSMALI_DMP_ARG="--dex-file="$BAKSMALI_DMP_OUT
  if [ ! -f $BAKSMALI_DMP_OUT ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    if [ $PACKAGE_NAME != "" ]; then
      # Extracting Dex File path on device from Dex File related to package
      apk_directory=$(dirname $(grep $PACKAGE_NAME $DEX_START | tail -n1 | awk '{print $8}'))
      apk_dex_files=$(adb shell find $apk_directory -name "*.?dex" -type f 2> /dev/null)
      for apk_file in $apk_dex_files; do
        base_name=$(basename $apk_file)
        adb pull $apk_file $INTERMEDIATES_DIR/base."${base_name#*.}"
      done
      oatdump --oat-file=$INTERMEDIATES_DIR/base.odex --export-dex-to=$INTERMEDIATES_DIR --output=/dev/null
      export_dex=( $INTERMEDIATES_DIR/*apk_export* )
      baksmali -JXmx1024M dump $export_dex > $BAKSMALI_DMP_OUT 2> /dev/null
      if ! [ -s $BAKSMALI_DMP_OUT ]; then
        rm $BAKSMALI_DMP_OUT
        BAKSMALI_DMP_ARG=""
        echo "Failed to retrieve Dex File format"
      fi
    else
      BAKSMALI_DMP_ARG=""
      echo "Failed to retrieve Dex File format"
    fi
  else
    echo "Skipped: Retrieving Dex File format from baksmali"
  fi

  if [ ! -d "$RESULTS_DIR" ]; then
    mkdir $RESULTS_DIR
    DO_REDO[$pid]=true
  fi

  # Step 5 - Using Steps 2, 3, 4 outputs in order to output graph data
  # and trace data
  # Only the category names are needed for the commands giving final output
  shift
  TIME_OUTPUT=($RESULTS_DIR/time_output_*.dat)
  if [ ! -e ${TIME_OUTPUT[0]} ] || [ "${DO_REDO[$pid]}" = true ] || [ $DO_REDO = true ]; then
    DO_REDO[$pid]=true
    echo "Creating Categorized Time Table"
    python $ANDROID_BUILD_TOP/art/tools/runtime_memusage/symbol_trace_info.py \
      -d $RESULTS_DIR ${OFFSET_ARGS[@]} ${TIME_ARGS[@]} $BAKSMALI_DMP_ARG $ASAN_OUT_FILTERED $SYM_FILTERED $DEX_START $@
  else
    echo "Skipped: Creating Categorized Time Table"
  fi

  # Step 6 - Use graph data from Step 5 to plot graph
  # Contains the category names used for legend of gnuplot
  PLOT_CATS=`echo \"Uncategorized $@\"`
  PACKAGE_STRING=""
  if [ $PACKAGE_NAME != "" ]; then
    PACKAGE_STRING="Package name: "$PACKAGE_NAME" "
  fi
  echo "Plotting Categorized Time Table"
  # Plots the information from logcat
  gnuplot --persist -e \
    'filename(n) = sprintf("'"$RESULTS_DIR"'/time_output_%d.dat", n);
     catnames = '"$PLOT_CATS"';
     set title "'"$PACKAGE_STRING"'PID: '"$pid"'";
     set xlabel "Time (milliseconds)";
     set ylabel "Dex File Offset (bytes)";
     plot for [i=0:'"$NUM_CAT"'] filename(i) using 1:2 title word(catnames, i + 1);'

  if [ $USE_TEMP = true ]; then
    echo "Removing temp directory and files"
    rm -rf $OUT_DIR
  fi
done
