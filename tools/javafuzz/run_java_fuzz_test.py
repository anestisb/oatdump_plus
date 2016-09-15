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

import abc
import argparse
import filecmp

from glob import glob

import os
import shlex
import shutil
import subprocess
import sys

from tempfile import mkdtemp


sys.path.append(os.path.dirname(os.path.dirname(__file__)))

from bisection_search.common import RetCode
from bisection_search.common import CommandListToCommandString
from bisection_search.common import FatalError
from bisection_search.common import GetEnvVariableOrError
from bisection_search.common import RunCommandForOutput
from bisection_search.common import DeviceTestEnv

# Return codes supported by bisection bug search.
BISECTABLE_RET_CODES = (RetCode.SUCCESS, RetCode.ERROR, RetCode.TIMEOUT)

#
# Utility methods.
#


def RunCommand(cmd, out, err, timeout=5):
  """Executes a command, and returns its return code.

  Args:
    cmd: list of strings, a command to execute
    out: string, file name to open for stdout (or None)
    err: string, file name to open for stderr (or None)
    timeout: int, time out in seconds
  Returns:
    RetCode, return code of running command (forced RetCode.TIMEOUT
    on timeout)
  """
  devnull = subprocess.DEVNULL
  outf = devnull
  if out is not None:
    outf = open(out, mode='w')
  errf = devnull
  if err is not None:
    errf = open(err, mode='w')
  (_, _, retcode) = RunCommandForOutput(cmd, None, outf, errf, timeout)
  if outf != devnull:
    outf.close()
  if errf != devnull:
    errf.close()
  return retcode


def GetJackClassPath():
  """Returns Jack's classpath."""
  top = GetEnvVariableOrError('ANDROID_BUILD_TOP')
  libdir = top + '/out/host/common/obj/JAVA_LIBRARIES'
  return libdir + '/core-libart-hostdex_intermediates/classes.jack:' \
       + libdir + '/core-oj-hostdex_intermediates/classes.jack'


def GetExecutionModeRunner(device, mode):
  """Returns a runner for the given execution mode.

  Args:
    device: string, target device serial number (or None)
    mode: string, execution mode
  Returns:
    TestRunner with given execution mode
  Raises:
    FatalError: error for unknown execution mode
  """
  if mode == 'ri':
    return TestRunnerRIOnHost()
  if mode == 'hint':
    return TestRunnerArtIntOnHost()
  if mode == 'hopt':
    return TestRunnerArtOptOnHost()
  if mode == 'tint':
    return TestRunnerArtIntOnTarget(device)
  if mode == 'topt':
    return TestRunnerArtOptOnTarget(device)
  raise FatalError('Unknown execution mode')

#
# Execution mode classes.
#


class TestRunner(object):
  """Abstraction for running a test in a particular execution mode."""
  __meta_class__ = abc.ABCMeta

  @abc.abstractproperty
  def description(self):
    """Returns a description string of the execution mode."""

  @abc.abstractproperty
  def id(self):
    """Returns a short string that uniquely identifies the execution mode."""

  @property
  def output_file(self):
    return self.id + '_out.txt'

  @abc.abstractmethod
  def GetBisectionSearchArgs(self):
    """Get arguments to pass to bisection search tool.

    Returns:
      list of strings - arguments for bisection search tool, or None if
      runner is not bisectable
    """

  @abc.abstractmethod
  def CompileAndRunTest(self):
    """Compile and run the generated test.

    Ensures that the current Test.java in the temporary directory is compiled
    and executed under the current execution mode. On success, transfers the
    generated output to the file self.output_file in the temporary directory.

    Most nonzero return codes are assumed non-divergent, since systems may
    exit in different ways. This is enforced by normalizing return codes.

    Returns:
      normalized return code
    """


class TestRunnerRIOnHost(TestRunner):
  """Concrete test runner of the reference implementation on host."""

  @property
  def description(self):
    return 'RI on host'

  @property
  def id(self):
    return 'RI'

  def CompileAndRunTest(self):
    if RunCommand(['javac', 'Test.java'],
                  out=None, err=None, timeout=30) == RetCode.SUCCESS:
      retc = RunCommand(['java', 'Test'], self.output_file, err=None)
    else:
      retc = RetCode.NOTCOMPILED
    return retc

  def GetBisectionSearchArgs(self):
    return None


class TestRunnerArtOnHost(TestRunner):
  """Abstract test runner of Art on host."""

  def  __init__(self, extra_args=None):
    """Constructor for the Art on host tester.

    Args:
      extra_args: list of strings, extra arguments for dalvikvm
    """
    self._art_cmd = ['/bin/bash', 'art', '-cp', 'classes.dex']
    if extra_args is not None:
      self._art_cmd += extra_args
    self._art_cmd.append('Test')
    self._jack_args = ['-cp', GetJackClassPath(), '--output-dex', '.',
                       'Test.java']

  def CompileAndRunTest(self):
    if RunCommand(['jack'] + self._jack_args, out=None, err='jackerr.txt',
                  timeout=30) == RetCode.SUCCESS:
      retc = RunCommand(self._art_cmd, self.output_file, 'arterr.txt')
    else:
      retc = RetCode.NOTCOMPILED
    return retc


class TestRunnerArtIntOnHost(TestRunnerArtOnHost):
  """Concrete test runner of interpreter mode Art on host."""

  def  __init__(self):
    """Constructor."""
    super().__init__(['-Xint'])

  @property
  def description(self):
    return 'Art interpreter on host'

  @property
  def id(self):
    return 'HInt'

  def GetBisectionSearchArgs(self):
    return None


class TestRunnerArtOptOnHost(TestRunnerArtOnHost):
  """Concrete test runner of optimizing compiler mode Art on host."""

  def  __init__(self):
    """Constructor."""
    super().__init__(None)

  @property
  def description(self):
    return 'Art optimizing on host'

  @property
  def id(self):
    return 'HOpt'

  def GetBisectionSearchArgs(self):
    cmd_str = CommandListToCommandString(
        self._art_cmd[0:2] + ['{ARGS}'] + self._art_cmd[2:])
    return ['--raw-cmd={0}'.format(cmd_str), '--timeout', str(30)]


class TestRunnerArtOnTarget(TestRunner):
  """Abstract test runner of Art on target."""

  def  __init__(self, device, extra_args=None):
    """Constructor for the Art on target tester.

    Args:
      device: string, target device serial number (or None)
      extra_args: list of strings, extra arguments for dalvikvm
    """
    self._test_env = DeviceTestEnv('javafuzz_', specific_device=device)
    self._dalvik_cmd = ['dalvikvm']
    if extra_args is not None:
      self._dalvik_cmd += extra_args
    self._device = device
    self._jack_args = ['-cp', GetJackClassPath(), '--output-dex', '.',
                       'Test.java']
    self._device_classpath = None

  def CompileAndRunTest(self):
    if RunCommand(['jack'] + self._jack_args, out=None, err='jackerr.txt',
                   timeout=30) == RetCode.SUCCESS:
      self._device_classpath = self._test_env.PushClasspath('classes.dex')
      cmd = self._dalvik_cmd + ['-cp', self._device_classpath, 'Test']
      (output, retc) = self._test_env.RunCommand(
          cmd, {'ANDROID_LOG_TAGS': '*:s'})
      with open(self.output_file, 'w') as run_out:
        run_out.write(output)
    else:
      retc = RetCode.NOTCOMPILED
    return retc

  def GetBisectionSearchArgs(self):
    cmd_str = CommandListToCommandString(
        self._dalvik_cmd + ['-cp',self._device_classpath, 'Test'])
    cmd = ['--raw-cmd={0}'.format(cmd_str), '--timeout', str(30)]
    if self._device:
      cmd += ['--device-serial', self._device]
    else:
      cmd.append('--device')
    return cmd


class TestRunnerArtIntOnTarget(TestRunnerArtOnTarget):
  """Concrete test runner of interpreter mode Art on target."""

  def  __init__(self, device):
    """Constructor.

    Args:
      device: string, target device serial number (or None)
    """
    super().__init__(device, ['-Xint'])

  @property
  def description(self):
    return 'Art interpreter on target'

  @property
  def id(self):
    return 'TInt'

  def GetBisectionSearchArgs(self):
    return None


class TestRunnerArtOptOnTarget(TestRunnerArtOnTarget):
  """Concrete test runner of optimizing compiler mode Art on target."""

  def  __init__(self, device):
    """Constructor.

    Args:
      device: string, target device serial number (or None)
    """
    super().__init__(device, None)

  @property
  def description(self):
    return 'Art optimizing on target'

  @property
  def id(self):
    return 'TOpt'

  def GetBisectionSearchArgs(self):
    cmd_str = CommandListToCommandString(
        self._dalvik_cmd + ['-cp', self._device_classpath, 'Test'])
    cmd = ['--raw-cmd={0}'.format(cmd_str), '--timeout', str(30)]
    if self._device:
      cmd += ['--device-serial', self._device]
    else:
      cmd.append('--device')
    return cmd


#
# Tester classes.
#


class JavaFuzzTester(object):
  """Tester that runs JavaFuzz many times and report divergences."""

  def  __init__(self, num_tests, device, mode1, mode2):
    """Constructor for the tester.

    Args:
      num_tests: int, number of tests to run
      device: string, target device serial number (or None)
      mode1: string, execution mode for first runner
      mode2: string, execution mode for second runner
    """
    self._num_tests = num_tests
    self._device = device
    self._runner1 = GetExecutionModeRunner(device, mode1)
    self._runner2 = GetExecutionModeRunner(device, mode2)
    self._save_dir = None
    self._tmp_dir = None
    # Statistics.
    self._test = 0
    self._num_success = 0
    self._num_not_compiled = 0
    self._num_not_run = 0
    self._num_timed_out = 0
    self._num_divergences = 0

  def __enter__(self):
    """On entry, enters new temp directory after saving current directory.

    Raises:
      FatalError: error when temp directory cannot be constructed
    """
    self._save_dir = os.getcwd()
    self._results_dir = mkdtemp(dir='/tmp/')
    self._tmp_dir = mkdtemp(dir=self._results_dir)
    if self._tmp_dir is None or self._results_dir is None:
      raise FatalError('Cannot obtain temp directory')
    os.chdir(self._tmp_dir)
    return self

  def __exit__(self, etype, evalue, etraceback):
    """On exit, re-enters previously saved current directory and cleans up."""
    os.chdir(self._save_dir)
    shutil.rmtree(self._tmp_dir)
    if self._num_divergences == 0:
      shutil.rmtree(self._results_dir)

  def Run(self):
    """Runs JavaFuzz many times and report divergences."""
    print()
    print('**\n**** JavaFuzz Testing\n**')
    print()
    print('#Tests    :', self._num_tests)
    print('Device    :', self._device)
    print('Directory :', self._results_dir)
    print('Exec-mode1:', self._runner1.description)
    print('Exec-mode2:', self._runner2.description)
    print()
    self.ShowStats()
    for self._test in range(1, self._num_tests + 1):
      self.RunJavaFuzzTest()
      self.ShowStats()
    if self._num_divergences == 0:
      print('\n\nsuccess (no divergences)\n')
    else:
      print('\n\nfailure (divergences)\n')

  def ShowStats(self):
    """Shows current statistics (on same line) while tester is running."""
    print('\rTests:', self._test, \
          'Success:', self._num_success, \
          'Not-compiled:', self._num_not_compiled, \
          'Not-run:', self._num_not_run, \
          'Timed-out:', self._num_timed_out, \
          'Divergences:', self._num_divergences, end='')
    sys.stdout.flush()

  def RunJavaFuzzTest(self):
    """Runs a single JavaFuzz test, comparing two execution modes."""
    self.ConstructTest()
    retc1 = self._runner1.CompileAndRunTest()
    retc2 = self._runner2.CompileAndRunTest()
    self.CheckForDivergence(retc1, retc2)
    self.CleanupTest()

  def ConstructTest(self):
    """Use JavaFuzz to generate next Test.java test.

    Raises:
      FatalError: error when javafuzz fails
    """
    if RunCommand(['javafuzz'], out='Test.java', err=None) != RetCode.SUCCESS:
      raise FatalError('Unexpected error while running JavaFuzz')

  def CheckForDivergence(self, retc1, retc2):
    """Checks for divergences and updates statistics.

    Args:
      retc1: int, normalized return code of first runner
      retc2: int, normalized return code of second runner
    """
    if retc1 == retc2:
      # Non-divergent in return code.
      if retc1 == RetCode.SUCCESS:
        # Both compilations and runs were successful, inspect generated output.
        runner1_out = self._runner1.output_file
        runner2_out = self._runner2.output_file
        if not filecmp.cmp(runner1_out, runner2_out, shallow=False):
          self.ReportDivergence(retc1, retc2, is_output_divergence=True)
        else:
          self._num_success += 1
      elif retc1 == RetCode.TIMEOUT:
        self._num_timed_out += 1
      elif retc1 == RetCode.NOTCOMPILED:
        self._num_not_compiled += 1
      else:
        self._num_not_run += 1
    else:
      # Divergent in return code.
      self.ReportDivergence(retc1, retc2, is_output_divergence=False)

  def GetCurrentDivergenceDir(self):
    return self._results_dir + '/divergence' + str(self._num_divergences)

  def ReportDivergence(self, retc1, retc2, is_output_divergence):
    """Reports and saves a divergence."""
    self._num_divergences += 1
    print('\n' + str(self._num_divergences), end='')
    if is_output_divergence:
      print(' divergence in output')
    else:
      print(' divergence in return code: ' + retc1.name + ' vs. ' +
            retc2.name)
    # Save.
    ddir = self.GetCurrentDivergenceDir()
    os.mkdir(ddir)
    for f in glob('*.txt') + ['Test.java']:
      shutil.copy(f, ddir)
    # Maybe run bisection bug search.
    if retc1 in BISECTABLE_RET_CODES and retc2 in BISECTABLE_RET_CODES:
      self.MaybeBisectDivergence(retc1, retc2, is_output_divergence)

  def RunBisectionSearch(self, args, expected_retcode, expected_output,
                         runner_id):
    ddir = self.GetCurrentDivergenceDir()
    outfile_path = ddir + '/' + runner_id + '_bisection_out.txt'
    logfile_path = ddir + '/' + runner_id + '_bisection_log.txt'
    errfile_path = ddir + '/' + runner_id + '_bisection_err.txt'
    args = list(args) + ['--logfile', logfile_path, '--cleanup']
    args += ['--expected-retcode', expected_retcode.name]
    if expected_output:
      args += ['--expected-output', expected_output]
    bisection_search_path = os.path.join(
        GetEnvVariableOrError('ANDROID_BUILD_TOP'),
        'art/tools/bisection_search/bisection_search.py')
    if RunCommand([bisection_search_path] + args, out=outfile_path,
                  err=errfile_path, timeout=300) == RetCode.TIMEOUT:
      print('Bisection search TIMEOUT')

  def MaybeBisectDivergence(self, retc1, retc2, is_output_divergence):
    bisection_args1 = self._runner1.GetBisectionSearchArgs()
    bisection_args2 = self._runner2.GetBisectionSearchArgs()
    if is_output_divergence:
      maybe_output1 = self._runner1.output_file
      maybe_output2 = self._runner2.output_file
    else:
      maybe_output1 = maybe_output2 = None
    if bisection_args1 is not None:
      self.RunBisectionSearch(bisection_args1, retc2, maybe_output2,
                              self._runner1.id)
    if bisection_args2 is not None:
      self.RunBisectionSearch(bisection_args2, retc1, maybe_output1,
                              self._runner2.id)

  def CleanupTest(self):
    """Cleans up after a single test run."""
    for file_name in os.listdir(self._tmp_dir):
        file_path = os.path.join(self._tmp_dir, file_name)
        if os.path.isfile(file_path):
          os.unlink(file_path)
        elif os.path.isdir(file_path):
          shutil.rmtree(file_path)


def main():
  # Handle arguments.
  parser = argparse.ArgumentParser()
  parser.add_argument('--num_tests', default=10000,
                      type=int, help='number of tests to run')
  parser.add_argument('--device', help='target device serial number')
  parser.add_argument('--mode1', default='ri',
                      help='execution mode 1 (default: ri)')
  parser.add_argument('--mode2', default='hopt',
                      help='execution mode 2 (default: hopt)')
  args = parser.parse_args()
  if args.mode1 == args.mode2:
    raise FatalError('Identical execution modes given')
  # Run the JavaFuzz tester.
  with JavaFuzzTester(args.num_tests, args.device,
                      args.mode1, args.mode2) as fuzzer:
    fuzzer.Run()

if __name__ == '__main__':
  main()
