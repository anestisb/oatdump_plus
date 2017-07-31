#!/usr/bin/python3
#
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

# TODO We should unify this with build/Android.cpplint.mk.

import os
import pathlib
import subprocess
import sys

IGNORED_FILES = {"runtime/elf.h", "openjdkjvmti/include/jvmti.h"}

INTERESTING_SUFFIXES = {".h", ".cc"}

CPPLINT_FLAGS = [
    '--filter=-whitespace/line_length,-build/include,-readability/function,-readability/streams,-readability/todo,-runtime/references,-runtime/sizeof,-runtime/threadsafe_fn,-runtime/printf',
    '--quiet',
]

def is_interesting(f):
  """
  Returns true if this is a file we want to run through cpplint before uploading. False otherwise.
  """
  path = pathlib.Path(f)
  return f not in IGNORED_FILES and path.suffix in INTERESTING_SUFFIXES and path.exists()

def get_changed_files(commit):
  """
  Gets the files changed in the given commit.
  """
  return subprocess.check_output(
      ["git", 'diff-tree', '--no-commit-id', '--name-only', '-r', commit],
      stderr=subprocess.STDOUT,
      universal_newlines=True).split()

def run_cpplint(files):
  """
  Runs cpplint on the given files.
  """
  if len(files) == 0:
    return
  sys.exit(subprocess.call(['tools/cpplint.py'] + CPPLINT_FLAGS + files))

def main():
  if 'PREUPLOAD_COMMIT' in os.environ:
    commit = os.environ['PREUPLOAD_COMMIT']
  else:
    print("WARNING: Not running as a pre-upload hook. Assuming commit to check = 'HEAD'")
    commit = "HEAD"
  files_to_check = [f for f in get_changed_files(commit) if is_interesting(f)]
  run_cpplint(files_to_check)

if __name__ == '__main__':
  main()
