#!/usr/bin/env python3.4
#
# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import os
import shutil
import sys

from subprocess import call
from tempfile import mkdtemp

sys.path.append(os.path.dirname(os.path.dirname(
        os.path.realpath(__file__))))

from common.common import FatalError
from common.common import GetEnvVariableOrError
from common.common import GetJackClassPath
from common.common import RetCode
from common.common import RunCommand


#
# Tester class.
#


class DexFuzzTester(object):
  """Tester that feeds JFuzz programs into DexFuzz testing."""

  def  __init__(self, num_tests, num_inputs, device):
    """Constructor for the tester.

    Args:
      num_tests: int, number of tests to run
      num_inputs: int, number of JFuzz programs to generate
      device: string, target device serial number (or None)
    """
    self._num_tests = num_tests
    self._num_inputs = num_inputs
    self._device = device
    self._save_dir = None
    self._results_dir = None
    self._dexfuzz_dir = None
    self._inputs_dir = None
    self._dexfuzz_env = None

  def __enter__(self):
    """On entry, enters new temp directory after saving current directory.

    Raises:
      FatalError: error when temp directory cannot be constructed
    """
    self._save_dir = os.getcwd()
    self._results_dir = mkdtemp(dir='/tmp/')
    self._dexfuzz_dir = mkdtemp(dir=self._results_dir)
    self._inputs_dir = mkdtemp(dir=self._dexfuzz_dir)
    if self._results_dir is None or self._dexfuzz_dir is None or \
        self._inputs_dir is None:
      raise FatalError('Cannot obtain temp directory')
    self._dexfuzz_env = os.environ.copy()
    self._dexfuzz_env['ANDROID_DATA'] = self._dexfuzz_dir
    top = GetEnvVariableOrError('ANDROID_BUILD_TOP')
    self._dexfuzz_env['PATH'] = (top + '/art/tools/bisection_search:' +
                                 self._dexfuzz_env['PATH'])
    android_root = GetEnvVariableOrError('ANDROID_HOST_OUT')
    self._dexfuzz_env['ANDROID_ROOT'] = android_root
    self._dexfuzz_env['LD_LIBRARY_PATH'] = android_root + '/lib'
    os.chdir(self._dexfuzz_dir)
    os.mkdir('divergent_programs')
    os.mkdir('bisection_outputs')
    return self

  def __exit__(self, etype, evalue, etraceback):
    """On exit, re-enters previously saved current directory and cleans up."""
    os.chdir(self._save_dir)
    # TODO: detect divergences or shutil.rmtree(self._results_dir)

  def Run(self):
    """Feeds JFuzz programs into DexFuzz testing."""
    print()
    print('**\n**** JFuzz Testing\n**')
    print()
    print('#Tests    :', self._num_tests)
    print('Device    :', self._device)
    print('Directory :', self._results_dir)
    print()
    self.GenerateJFuzzPrograms()
    self.RunDexFuzz()


  def GenerateJFuzzPrograms(self):
    """Generates JFuzz programs.

    Raises:
      FatalError: error when generation fails
    """
    os.chdir(self._inputs_dir)
    for i in range(1, self._num_inputs + 1):
      jack_args = ['-cp', GetJackClassPath(), '--output-dex', '.', 'Test.java']
      if RunCommand(['jfuzz'], out='Test.java', err=None) != RetCode.SUCCESS:
        raise FatalError('Unexpected error while running JFuzz')
      if RunCommand(['jack'] + jack_args, out=None, err='jackerr.txt',
                    timeout=30) != RetCode.SUCCESS:
        raise FatalError('Unexpected error while running Jack')
      shutil.move('Test.java', '../Test' + str(i) + '.java')
      shutil.move('classes.dex', 'classes' + str(i) + '.dex')
    os.unlink('jackerr.txt')

  def RunDexFuzz(self):
    """Starts the DexFuzz testing."""
    os.chdir(self._dexfuzz_dir)
    dexfuzz_args = ['--inputs=' + self._inputs_dir,
                    '--execute',
                    '--execute-class=Test',
                    '--repeat=' + str(self._num_tests),
                    '--dump-output', '--dump-verify',
                    '--interpreter', '--optimizing',
                    '--bisection-search']
    if self._device is not None:
      dexfuzz_args += ['--device=' + self._device, '--allarm']
    else:
      dexfuzz_args += ['--host']  # Assume host otherwise.
    cmd = ['dexfuzz'] + dexfuzz_args
    print('**** Running ****\n\n', cmd, '\n')
    call(cmd, env=self._dexfuzz_env)
    print('\n**** Results (report.log) ****\n')
    call(['tail', '-n 24', 'report.log'])


def main():
  # Handle arguments.
  parser = argparse.ArgumentParser()
  parser.add_argument('--num_tests', default=1000,
                      type=int, help='number of tests to run')
  parser.add_argument('--num_inputs', default=10,
                      type=int, help='number of JFuzz program to generate')
  parser.add_argument('--device', help='target device serial number')
  args = parser.parse_args()
  # Run the DexFuzz tester.
  with DexFuzzTester(args.num_tests, args.num_inputs, args.device) as fuzzer:
    fuzzer.Run()

if __name__ == '__main__':
  main()
