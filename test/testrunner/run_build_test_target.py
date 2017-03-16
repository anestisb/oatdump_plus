#!/usr/bin/env python
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

"""Build and run go/ab/git_master-art-host target

Provided with a target name, the script setup the environment for
building the test target by taking config information from
from target_config.py.

If the target field is defined in the configuration for the target, it
invokes `make` to build the target, otherwise, it assumes
that the its is a run-test target, and invokes testrunner.py
script for building and running the run-tests.
"""

import argparse
import os
import subprocess
import sys

from target_config import target_config
import env

parser = argparse.ArgumentParser()
parser.add_argument('build_target')
parser.add_argument('-j', default='1', dest='n_threads')
options = parser.parse_args()

target = target_config[options.build_target]
n_threads = options.n_threads
custom_env = target.get('env', {})
custom_env['SOONG_ALLOW_MISSING_DEPENDENCIES'] = 'true'
print custom_env
os.environ.update(custom_env)

if target.get('target'):
  build_command = 'make'
  build_command += ' -j' + str(n_threads)
  build_command += ' -C ' + env.ANDROID_BUILD_TOP
  build_command += ' ' + target.get('target')
  print build_command.split()
  if subprocess.call(build_command.split()):
    sys.exit(1)

if target.get('run-tests'):
  run_test_command = [os.path.join(env.ANDROID_BUILD_TOP,
                                   'art/test/testrunner/testrunner.py')]
  run_test_command += target.get('flags', [])
  run_test_command += ['-j', str(n_threads)]
  run_test_command += ['-b']
  run_test_command += ['--host']
  run_test_command += ['--verbose']

  print run_test_command
  if subprocess.call(run_test_command):
    sys.exit(1)

sys.exit(0)
