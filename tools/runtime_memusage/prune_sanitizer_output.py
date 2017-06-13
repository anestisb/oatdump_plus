#!/usr/bin/env python
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

"""Cleans up overlapping portions of traces provided by logcat."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import os
import sys

STACK_DIVIDER = 65 * '='


def has_min_lines(trace, stack_min_size):
    """Checks if the trace has a minimum amount of levels in trace."""
    # Line containing 'use-after-poison' contains address accessed, which is
    # useful for extracting Dex File offsets
    string_checks = ['use-after-poison', 'READ']
    required_checks = string_checks + ['#%d ' % line_ctr
                                       for line_ctr in
                                       range(stack_min_size)
                                       ]
    try:
        trace_indices = [trace.index(check) for check in required_checks]
        return all(trace_indices[trace_ind] < trace_indices[trace_ind + 1]
                   for trace_ind in range(len(trace_indices) - 1))
    except ValueError:
        return False
    return True


def prune_exact(trace, stack_min_size):
    """Removes all of trace that comes after the (stack_min_size)th trace."""
    string_checks = ['use-after-poison', 'READ']
    required_checks = string_checks + ['#%d ' % line_ctr
                                       for line_ctr in
                                       range(stack_min_size)
                                       ]
    trace_indices = [trace.index(check) for check in required_checks]
    new_line_index = trace.index("\n", trace_indices[-1])
    return trace[:new_line_index + 1]


def make_unique(trace):
    """Removes overlapping line numbers and lines out of order."""
    string_checks = ['use-after-poison', 'READ']
    hard_checks = string_checks + ['#%d ' % line_ctr
                                   for line_ctr in range(100)
                                   ]
    last_ind = -1
    for str_check in hard_checks:
        try:
            location_ind = trace.index(str_check)
            if last_ind > location_ind:
                trace = trace[:trace[:location_ind].find("\n") + 1]
            last_ind = location_ind
            try:
                next_location_ind = trace.index(str_check, location_ind + 1)
                trace = trace[:next_location_ind]
            except ValueError:
                pass
        except ValueError:
            pass
    return trace


def parse_args(argv):
    """Parses arguments passed in."""
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', action='store',
                        default="", dest="out_dir_name", type=is_directory,
                        help='Output Directory')
    parser.add_argument('-e', action='store_true',
                        default=False, dest='check_exact',
                        help='Forces each trace to be cut to have '
                             'minimum number of lines')
    parser.add_argument('-m', action='store',
                        default=4, dest='stack_min_size', type=int,
                        help='minimum number of lines a trace should have')
    parser.add_argument('trace_file', action='store',
                        type=argparse.FileType('r'),
                        help='File only containing lines that are related to '
                             'Sanitizer traces')
    return parser.parse_args(argv)


def is_directory(path_name):
    """Checks if a path is an actual directory."""
    if not os.path.isdir(path_name):
        dir_error = "%s is not a directory" % (path_name)
        raise argparse.ArgumentTypeError(dir_error)
    return path_name


def main(argv=None):
    """Parses arguments and cleans up traces using other functions."""
    stack_min_size = 4
    check_exact = False

    if argv is None:
        argv = sys.argv

    parsed_argv = parse_args(argv[1:])
    stack_min_size = parsed_argv.stack_min_size
    check_exact = parsed_argv.check_exact
    out_dir_name = parsed_argv.out_dir_name
    trace_file = parsed_argv.trace_file

    trace_split = trace_file.read().split(STACK_DIVIDER)
    trace_file.close()
    # if flag -e is enabled
    if check_exact:
        trace_prune_split = [prune_exact(trace, stack_min_size)
                             for trace in trace_split if
                             has_min_lines(trace, stack_min_size)
                             ]
        trace_unique_split = [make_unique(trace)
                              for trace in trace_prune_split
                              ]
    else:
        trace_unique_split = [make_unique(trace)
                              for trace in trace_split if
                              has_min_lines(trace, stack_min_size)
                              ]
    # has_min_lines is called again because removing lines can prune too much
    trace_clean_split = [trace for trace
                         in trace_unique_split if
                         has_min_lines(trace,
                                       stack_min_size)
                         ]

    outfile = os.path.join(out_dir_name, trace_file.name + '_filtered')
    with open(outfile, "w") as output_file:
        output_file.write(STACK_DIVIDER.join(trace_clean_split))

    filter_percent = 100.0 - (float(len(trace_clean_split)) /
                              len(trace_split) * 100)
    filter_amount = len(trace_split) - len(trace_clean_split)
    print("Filtered out %d (%f%%) of %d."
          % (filter_amount, filter_percent, len(trace_split)))


if __name__ == "__main__":
    main()
