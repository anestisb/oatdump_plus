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

"""Module containing common logic from python testing tools."""

import abc
import os
import signal
import shlex
import shutil

from subprocess import check_call
from subprocess import PIPE
from subprocess import Popen
from subprocess import STDOUT
from subprocess import TimeoutExpired

from tempfile import mkdtemp
from tempfile import NamedTemporaryFile

from enum import Enum
from enum import unique

# Temporary directory path on device.
DEVICE_TMP_PATH = '/data/local/tmp'

# Architectures supported in dalvik cache.
DALVIK_CACHE_ARCHS = ['arm', 'arm64', 'x86', 'x86_64']


@unique
class RetCode(Enum):
  """Enum representing normalized return codes."""
  SUCCESS = 0
  TIMEOUT = 1
  ERROR = 2
  NOTCOMPILED = 3
  NOTRUN = 4


def GetEnvVariableOrError(variable_name):
  """Gets value of an environmental variable.

  If the variable is not set raises FatalError.

  Args:
    variable_name: string, name of variable to get.

  Returns:
    string, value of requested variable.

  Raises:
    FatalError: Requested variable is not set.
  """
  top = os.environ.get(variable_name)
  if top is None:
    raise FatalError('{0} environmental variable not set.'.format(
        variable_name))
  return top


def _DexArchCachePaths(android_data_path):
  """Returns paths to architecture specific caches.

  Args:
    android_data_path: string, path dalvik-cache resides in.

  Returns:
    Iterable paths to architecture specific caches.
  """
  return ('{0}/dalvik-cache/{1}'.format(android_data_path, arch)
          for arch in DALVIK_CACHE_ARCHS)


def RunCommandForOutput(cmd, env, stdout, stderr, timeout=60):
  """Runs command piping output to files, stderr or stdout.

  Args:
    cmd: list of strings, command to run.
    env: shell environment to run the command with.
    stdout: file handle or one of Subprocess.PIPE, Subprocess.STDOUT,
      Subprocess.DEVNULL, see Popen.
    stderr: file handle or one of Subprocess.PIPE, Subprocess.STDOUT,
      Subprocess.DEVNULL, see Popen.
    timeout: int, timeout in seconds.

  Returns:
    tuple (string, string, RetCode) stdout output, stderr output, normalized
      return code.
  """
  proc = Popen(cmd, stdout=stdout, stderr=stderr, env=env,
               universal_newlines=True, start_new_session=True)
  try:
    (output, stderr_output) = proc.communicate(timeout=timeout)
    if proc.returncode == 0:
      retcode = RetCode.SUCCESS
    else:
      retcode = RetCode.ERROR
  except TimeoutExpired:
    os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    (output, stderr_output) = proc.communicate()
    retcode = RetCode.TIMEOUT
  return (output, stderr_output, retcode)


def _RunCommandForOutputAndLog(cmd, env, logfile, timeout=60):
  """Runs command and logs its output. Returns the output.

  Args:
    cmd: list of strings, command to run.
    env: shell environment to run the command with.
    logfile: file handle to logfile.
    timeout: int, timeout in seconds.

  Returns:
    tuple (string, string, RetCode) stdout output, stderr output, normalized
      return code.
  """
  (output, _, retcode) = RunCommandForOutput(cmd, env, PIPE, STDOUT, timeout)
  logfile.write('Command:\n{0}\n{1}\nReturn code: {2}\n'.format(
      CommandListToCommandString(cmd), output, retcode))
  return (output, retcode)


def CommandListToCommandString(cmd):
  """Converts shell command represented as list of strings to a single string.

  Each element of the list is wrapped in double quotes.

  Args:
    cmd: list of strings, shell command.

  Returns:
    string, shell command.
  """
  return ' '.join([shlex.quote(segment) for segment in cmd])


class FatalError(Exception):
  """Fatal error in script."""


class ITestEnv(object):
  """Test environment abstraction.

  Provides unified interface for interacting with host and device test
  environments. Creates a test directory and expose methods to modify test files
  and run commands.
  """
  __meta_class__ = abc.ABCMeta

  @abc.abstractmethod
  def CreateFile(self, name=None):
    """Creates a file in test directory.

    Returned path to file can be used in commands run in the environment.

    Args:
      name: string, file name. If None file is named arbitrarily.

    Returns:
      string, environment specific path to file.
    """

  @abc.abstractmethod
  def WriteLines(self, file_path, lines):
    """Writes lines to a file in test directory.

    If file exists it gets overwritten. If file doest not exist it is created.

    Args:
      file_path: string, environment specific path to file.
      lines: list of strings to write.
    """

  @abc.abstractmethod
  def RunCommand(self, cmd, env_updates=None):
    """Runs command in environment with updated environmental variables.

    Args:
      cmd: list of strings, command to run.
      env_updates: dict, string to string, maps names of variables to their
        updated values.
    Returns:
      tuple (string, string, int) stdout output, stderr output, return code.
    """

  @abc.abstractproperty
  def logfile(self):
    """Gets file handle to logfile residing on host."""


class HostTestEnv(ITestEnv):
  """Host test environment. Concrete implementation of ITestEnv.

  Maintains a test directory in /tmp/. Runs commands on the host in modified
  shell environment. Mimics art script behavior.

  For methods documentation see base class.
  """

  def __init__(self, directory_prefix, cleanup=True, logfile_path=None,
               timeout=60, x64=False):
    """Constructor.

    Args:
      directory_prefix: string, prefix for environment directory name.
      cleanup: boolean, if True remove test directory in destructor.
      logfile_path: string, can be used to specify custom logfile location.
      timeout: int, seconds, time to wait for single test run to finish.
      x64: boolean, whether to setup in x64 mode.
    """
    self._cleanup = cleanup
    self._timeout = timeout
    self._env_path = mkdtemp(dir='/tmp/', prefix=directory_prefix)
    if logfile_path is None:
      self._logfile = open('{0}/log'.format(self._env_path), 'w+')
    else:
      self._logfile = open(logfile_path, 'w+')
    os.mkdir('{0}/dalvik-cache'.format(self._env_path))
    for arch_cache_path in _DexArchCachePaths(self._env_path):
      os.mkdir(arch_cache_path)
    lib = 'lib64' if x64 else 'lib'
    android_root = GetEnvVariableOrError('ANDROID_HOST_OUT')
    library_path = android_root + '/' + lib
    path = android_root + '/bin'
    self._shell_env = os.environ.copy()
    self._shell_env['ANDROID_DATA'] = self._env_path
    self._shell_env['ANDROID_ROOT'] = android_root
    self._shell_env['LD_LIBRARY_PATH'] = library_path
    self._shell_env['DYLD_LIBRARY_PATH'] = library_path
    self._shell_env['PATH'] = (path + ':' + self._shell_env['PATH'])
    # Using dlopen requires load bias on the host.
    self._shell_env['LD_USE_LOAD_BIAS'] = '1'

  def __del__(self):
    if self._cleanup:
      shutil.rmtree(self._env_path)

  def CreateFile(self, name=None):
    if name is None:
      f = NamedTemporaryFile(dir=self._env_path, delete=False)
    else:
      f = open('{0}/{1}'.format(self._env_path, name), 'w+')
    return f.name

  def WriteLines(self, file_path, lines):
    with open(file_path, 'w') as f:
      f.writelines('{0}\n'.format(line) for line in lines)
    return

  def RunCommand(self, cmd, env_updates=None):
    if not env_updates:
      env_updates = {}
    self._EmptyDexCache()
    env = self._shell_env.copy()
    env.update(env_updates)
    return _RunCommandForOutputAndLog(cmd, env, self._logfile, self._timeout)

  @property
  def logfile(self):
    return self._logfile

  def _EmptyDexCache(self):
    """Empties dex cache.

    Iterate over files in architecture specific cache directories and remove
    them.
    """
    for arch_cache_path in _DexArchCachePaths(self._env_path):
      for file_path in os.listdir(arch_cache_path):
        file_path = '{0}/{1}'.format(arch_cache_path, file_path)
        if os.path.isfile(file_path):
          os.unlink(file_path)


class DeviceTestEnv(ITestEnv):
  """Device test environment. Concrete implementation of ITestEnv.

  For methods documentation see base class.
  """

  def __init__(self, directory_prefix, cleanup=True, logfile_path=None,
               timeout=60, specific_device=None):
    """Constructor.

    Args:
      directory_prefix: string, prefix for environment directory name.
      cleanup: boolean, if True remove test directory in destructor.
      logfile_path: string, can be used to specify custom logfile location.
      timeout: int, seconds, time to wait for single test run to finish.
      specific_device: string, serial number of device to use.
    """
    self._cleanup = cleanup
    self._timeout = timeout
    self._specific_device = specific_device
    self._host_env_path = mkdtemp(dir='/tmp/', prefix=directory_prefix)
    if logfile_path is None:
      self._logfile = open('{0}/log'.format(self._host_env_path), 'w+')
    else:
      self._logfile = open(logfile_path, 'w+')
    self._device_env_path = '{0}/{1}'.format(
        DEVICE_TMP_PATH, os.path.basename(self._host_env_path))
    self._shell_env = os.environ.copy()

    self._AdbMkdir('{0}/dalvik-cache'.format(self._device_env_path))
    for arch_cache_path in _DexArchCachePaths(self._device_env_path):
      self._AdbMkdir(arch_cache_path)

  def __del__(self):
    if self._cleanup:
      shutil.rmtree(self._host_env_path)
      check_call(shlex.split(
          'adb shell if [ -d "{0}" ]; then rm -rf "{0}"; fi'
          .format(self._device_env_path)))

  def CreateFile(self, name=None):
    with NamedTemporaryFile(mode='w') as temp_file:
      self._AdbPush(temp_file.name, self._device_env_path)
      if name is None:
        name = os.path.basename(temp_file.name)
      return '{0}/{1}'.format(self._device_env_path, name)

  def WriteLines(self, file_path, lines):
    with NamedTemporaryFile(mode='w') as temp_file:
      temp_file.writelines('{0}\n'.format(line) for line in lines)
      temp_file.flush()
      self._AdbPush(temp_file.name, file_path)
    return

  def RunCommand(self, cmd, env_updates=None):
    if not env_updates:
      env_updates = {}
    self._EmptyDexCache()
    if 'ANDROID_DATA' not in env_updates:
      env_updates['ANDROID_DATA'] = self._device_env_path
    env_updates_cmd = ' '.join(['{0}={1}'.format(var, val) for var, val
                                in env_updates.items()])
    cmd = CommandListToCommandString(cmd)
    adb = 'adb'
    if self._specific_device:
      adb += ' -s ' + self._specific_device
    cmd = '{0} shell "logcat -c && {1} {2}"'.format(
        adb, env_updates_cmd, cmd)
    (output, retcode) = _RunCommandForOutputAndLog(
        shlex.split(cmd), self._shell_env, self._logfile, self._timeout)
    logcat_cmd = 'adb shell "logcat -d -s -b main dex2oat:* dex2oatd:*"'
    (err_output, _) = _RunCommandForOutputAndLog(
        shlex.split(logcat_cmd), self._shell_env, self._logfile)
    return (output + err_output, retcode)

  @property
  def logfile(self):
    return self._logfile

  def PushClasspath(self, classpath):
    """Push classpath to on-device test directory.

    Classpath can contain multiple colon separated file paths, each file is
    pushed. Returns analogous classpath with paths valid on device.

    Args:
      classpath: string, classpath in format 'a/b/c:d/e/f'.
    Returns:
      string, classpath valid on device.
    """
    paths = classpath.split(':')
    device_paths = []
    for path in paths:
      device_paths.append('{0}/{1}'.format(
          self._device_env_path, os.path.basename(path)))
      self._AdbPush(path, self._device_env_path)
    return ':'.join(device_paths)

  def _AdbPush(self, what, where):
    check_call(shlex.split('adb push "{0}" "{1}"'.format(what, where)),
               stdout=self._logfile, stderr=self._logfile)

  def _AdbMkdir(self, path):
    check_call(shlex.split('adb shell mkdir "{0}" -p'.format(path)),
               stdout=self._logfile, stderr=self._logfile)

  def _EmptyDexCache(self):
    """Empties dex cache."""
    for arch_cache_path in _DexArchCachePaths(self._device_env_path):
      cmd = 'adb shell if [ -d "{0}" ]; then rm -f "{0}"/*; fi'.format(
          arch_cache_path)
      check_call(shlex.split(cmd), stdout=self._logfile, stderr=self._logfile)
