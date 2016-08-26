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

"""Performs bisection bug search on methods and optimizations.

See README.md.

Example usage:
./bisection-search.py -cp classes.dex --expected-output output Test
"""

import argparse
import re
import sys

from common import DeviceTestEnv
from common import FatalError
from common import GetEnvVariableOrError
from common import HostTestEnv

# Passes that are never disabled during search process because disabling them
# would compromise correctness.
MANDATORY_PASSES = ['dex_cache_array_fixups_arm',
                    'dex_cache_array_fixups_mips',
                    'instruction_simplifier$before_codegen',
                    'pc_relative_fixups_x86',
                    'pc_relative_fixups_mips',
                    'x86_memory_operand_generation']

# Passes that show up as optimizations in compiler verbose output but aren't
# driven by run-passes mechanism. They are mandatory and will always run, we
# never pass them to --run-passes.
NON_PASSES = ['builder', 'prepare_for_register_allocation',
              'liveness', 'register']


class Dex2OatWrapperTestable(object):
  """Class representing a testable compilation.

  Accepts filters on compiled methods and optimization passes.
  """

  def __init__(self, base_cmd, test_env, class_name, args,
               expected_output=None, verbose=False):
    """Constructor.

    Args:
      base_cmd: list of strings, base command to run.
      test_env: ITestEnv.
      class_name: string, name of class to run.
      args: list of strings, program arguments to pass.
      expected_output: string, expected output to compare against or None.
      verbose: bool, enable verbose output.
    """
    self._base_cmd = base_cmd
    self._test_env = test_env
    self._class_name = class_name
    self._args = args
    self._expected_output = expected_output
    self._compiled_methods_path = self._test_env.CreateFile('compiled_methods')
    self._passes_to_run_path = self._test_env.CreateFile('run_passes')
    self._verbose = verbose

  def Test(self, compiled_methods, passes_to_run=None):
    """Tests compilation with compiled_methods and run_passes switches active.

    If compiled_methods is None then compiles all methods.
    If passes_to_run is None then runs default passes.

    Args:
      compiled_methods: list of strings representing methods to compile or None.
      passes_to_run: list of strings representing passes to run or None.

    Returns:
      True if test passes with given settings. False otherwise.
    """
    if self._verbose:
      print('Testing methods: {0} passes:{1}.'.format(
          compiled_methods, passes_to_run))
    cmd = self._PrepareCmd(compiled_methods=compiled_methods,
                           passes_to_run=passes_to_run,
                           verbose_compiler=True)
    (output, _, ret_code) = self._test_env.RunCommand(cmd)
    res = ret_code == 0 and (self._expected_output is None
                             or output == self._expected_output)
    if self._verbose:
      print('Test passed: {0}.'.format(res))
    return res

  def GetAllMethods(self):
    """Get methods compiled during the test.

    Returns:
      List of strings representing methods compiled during the test.

    Raises:
      FatalError: An error occurred when retrieving methods list.
    """
    cmd = self._PrepareCmd(verbose_compiler=True)
    (_, err_output, _) = self._test_env.RunCommand(cmd)
    match_methods = re.findall(r'Building ([^\n]+)\n', err_output)
    if not match_methods:
      raise FatalError('Failed to retrieve methods list. '
                       'Not recognized output format.')
    return match_methods

  def GetAllPassesForMethod(self, compiled_method):
    """Get all optimization passes ran for a method during the test.

    Args:
      compiled_method: string representing method to compile.

    Returns:
      List of strings representing passes ran for compiled_method during test.

    Raises:
      FatalError: An error occurred when retrieving passes list.
    """
    cmd = self._PrepareCmd(compiled_methods=[compiled_method],
                           verbose_compiler=True)
    (_, err_output, _) = self._test_env.RunCommand(cmd)
    match_passes = re.findall(r'Starting pass: ([^\n]+)\n', err_output)
    if not match_passes:
      raise FatalError('Failed to retrieve passes list. '
                       'Not recognized output format.')
    return [p for p in match_passes if p not in NON_PASSES]

  def _PrepareCmd(self, compiled_methods=None, passes_to_run=None,
                  verbose_compiler=False):
    """Prepare command to run."""
    cmd = list(self._base_cmd)
    if compiled_methods is not None:
      self._test_env.WriteLines(self._compiled_methods_path, compiled_methods)
      cmd += ['-Xcompiler-option', '--compiled-methods={0}'.format(
          self._compiled_methods_path)]
    if passes_to_run is not None:
      self._test_env.WriteLines(self._passes_to_run_path, passes_to_run)
      cmd += ['-Xcompiler-option', '--run-passes={0}'.format(
          self._passes_to_run_path)]
    if verbose_compiler:
      cmd += ['-Xcompiler-option', '--runtime-arg', '-Xcompiler-option',
              '-verbose:compiler']
    cmd += ['-classpath', self._test_env.classpath, self._class_name]
    cmd += self._args
    return cmd


def BinarySearch(start, end, test):
  """Binary search integers using test function to guide the process."""
  while start < end:
    mid = (start + end) // 2
    if test(mid):
      start = mid + 1
    else:
      end = mid
  return start


def FilterPasses(passes, cutoff_idx):
  """Filters passes list according to cutoff_idx but keeps mandatory passes."""
  return [opt_pass for idx, opt_pass in enumerate(passes)
          if opt_pass in MANDATORY_PASSES or idx < cutoff_idx]


def BugSearch(testable):
  """Find buggy (method, optimization pass) pair for a given testable.

  Args:
    testable: Dex2OatWrapperTestable.

  Returns:
    (string, string) tuple. First element is name of method which when compiled
    exposes test failure. Second element is name of optimization pass such that
    for aforementioned method running all passes up to and excluding the pass
    results in test passing but running all passes up to and including the pass
    results in test failing.

    (None, None) if test passes when compiling all methods.
    (string, None) if a method is found which exposes the failure, but the
      failure happens even when running just mandatory passes.

  Raises:
    FatalError: Testable fails with no methods compiled.
    AssertionError: Method failed for all passes when bisecting methods, but
    passed when bisecting passes. Possible sporadic failure.
  """
  all_methods = testable.GetAllMethods()
  faulty_method_idx = BinarySearch(
      0,
      len(all_methods),
      lambda mid: testable.Test(all_methods[0:mid]))
  if faulty_method_idx == len(all_methods):
    return (None, None)
  if faulty_method_idx == 0:
    raise FatalError('Testable fails with no methods compiled. '
                     'Perhaps issue lies outside of compiler.')
  faulty_method = all_methods[faulty_method_idx - 1]
  all_passes = testable.GetAllPassesForMethod(faulty_method)
  faulty_pass_idx = BinarySearch(
      0,
      len(all_passes),
      lambda mid: testable.Test([faulty_method],
                                FilterPasses(all_passes, mid)))
  if faulty_pass_idx == 0:
    return (faulty_method, None)
  assert faulty_pass_idx != len(all_passes), 'Method must fail for some passes.'
  faulty_pass = all_passes[faulty_pass_idx - 1]
  return (faulty_method, faulty_pass)


def PrepareParser():
  """Prepares argument parser."""
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '-cp', '--classpath', required=True, type=str, help='classpath')
  parser.add_argument('--expected-output', type=str,
                      help='file containing expected output')
  parser.add_argument(
      '--device', action='store_true', default=False, help='run on device')
  parser.add_argument('classname', type=str, help='name of class to run')
  parser.add_argument('--lib', dest='lib', type=str, default='libart.so',
                      help='lib to use, default: libart.so')
  parser.add_argument('--64', dest='x64', action='store_true',
                      default=False, help='x64 mode')
  parser.add_argument('--dalvikvm-option', dest='dalvikvm_opts',
                      metavar='OPTION', nargs='*', default=[],
                      help='additional dalvikvm option')
  parser.add_argument('--arg', dest='test_args', nargs='*', default=[],
                      help='argument to pass to program')
  parser.add_argument('--image', type=str, help='path to image')
  parser.add_argument('--verbose', action='store_true',
                      default=False, help='enable verbose output')
  return parser


def main():
  # Parse arguments
  parser = PrepareParser()
  args = parser.parse_args()

  # Prepare environment
  if args.expected_output is not None:
    with open(args.expected_output, 'r') as f:
      expected_output = f.read()
  else:
    expected_output = None
  if args.device:
    run_cmd = ['dalvikvm64'] if args.x64 else ['dalvikvm32']
    test_env = DeviceTestEnv(args.classpath)
  else:
    run_cmd = ['dalvikvm64'] if args.x64 else ['dalvikvm32']
    run_cmd += ['-XXlib:{0}'.format(args.lib)]
    if not args.image:
      image_path = '{0}/framework/core-optimizing-pic.art'.format(
          GetEnvVariableOrError('ANDROID_HOST_OUT'))
    else:
      image_path = args.image
    run_cmd += ['-Ximage:{0}'.format(image_path)]
    if args.dalvikvm_opts:
      run_cmd += args.dalvikvm_opts
    test_env = HostTestEnv(args.classpath, args.x64)

  # Perform the search
  try:
    testable = Dex2OatWrapperTestable(run_cmd, test_env, args.classname,
                                      args.test_args, expected_output,
                                      args.verbose)
    (method, opt_pass) = BugSearch(testable)
  except Exception as e:
    print('Error. Refer to logfile: {0}'.format(test_env.logfile.name))
    test_env.logfile.write('Exception: {0}\n'.format(e))
    raise

  # Report results
  if method is None:
    print('Couldn\'t find any bugs.')
  elif opt_pass is None:
    print('Faulty method: {0}. Fails with just mandatory passes.'.format(
        method))
  else:
    print('Faulty method and pass: {0}, {1}.'.format(method, opt_pass))
  print('Logfile: {0}'.format(test_env.logfile.name))
  sys.exit(0)


if __name__ == '__main__':
  main()
