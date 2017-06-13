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

"""Outputs quantitative information about Address Sanitizer traces."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from collections import Counter
from datetime import datetime
import argparse
import bisect
import os
import sys


def find_match(list_substrings, big_string):
    """Returns the category a trace belongs to by searching substrings."""
    for ind, substr in enumerate(list_substrings):
        if big_string.find(substr) != -1:
            return ind
    return list_substrings.index("Uncategorized")


def absolute_to_relative(plot_list, dex_start_list, cat_list):
    """Address changed to Dex File offset and shifting time to 0 min in ms."""
    time_format_str = "%H:%M:%S.%f"
    first_access_time = datetime.strptime(plot_list[0][0],
                                          time_format_str)
    for ind, elem in enumerate(plot_list):
        elem_date_time = datetime.strptime(elem[0], time_format_str)
        # Shift time values so that first access is at time 0 milliseconds
        elem[0] = int((elem_date_time - first_access_time).total_seconds() *
                      1000)
        address_access = int(elem[1], 16)
        # For each poisoned address, find highest Dex File starting address less
        # than address_access
        dex_file_start = dex_start_list[bisect.bisect(dex_start_list,
                                                      address_access) - 1
                                        ]
        elem.insert(1, address_access - dex_file_start)
        # Category that a data point belongs to
        elem.insert(2, cat_list[ind])


def print_category_info(cat_split, outname, out_dir_name, title):
    """Prints information of category and puts related traces in a files."""
    trace_counts_dict = Counter(cat_split)
    trace_counts_list_ordered = trace_counts_dict.most_common()
    print(53 * "-")
    print(title)
    print("\tNumber of distinct traces: " +
          str(len(trace_counts_list_ordered)))
    print("\tSum of trace counts: " +
          str(sum([trace[1] for trace in trace_counts_list_ordered])))
    print("\n\tCount: How many traces appeared with count\n\t")
    print(Counter([trace[1] for trace in trace_counts_list_ordered]))
    with open(os.path.join(out_dir_name, outname), "w") as output_file:
        for trace in trace_counts_list_ordered:
            output_file.write("\n\nNumber of times appeared: " +
                              str(trace[1]) +
                              "\n")
            output_file.write(trace[0].strip())


def print_categories(categories, symbol_file_split, out_dir_name):
    """Prints details of all categories."""
    # Info of traces containing a call to current category
    for cat_num, cat_name in enumerate(categories[1:]):
        print("\nCategory #%d" % (cat_num + 1))
        cat_split = [trace for trace in symbol_file_split
                     if cat_name in trace]
        cat_file_name = cat_name.lower() + "cat_output"
        print_category_info(cat_split, cat_file_name, out_dir_name,
                            "Traces containing: " + cat_name)
        noncat_split = [trace for trace in symbol_file_split
                        if cat_name not in trace]
        print_category_info(noncat_split, "non" + cat_file_name,
                            out_dir_name,
                            "Traces not containing: " +
                            cat_name)

    # All traces (including uncategorized) together
    print_category_info(symbol_file_split, "allcat_output",
                        out_dir_name,
                        "All traces together:")
    # Traces containing none of keywords
    # Only used if categories are passed in
    if len(categories) > 1:
        noncat_split = [trace for trace in symbol_file_split if
                        all(cat_name not in trace
                            for cat_name in categories)]
        print_category_info(noncat_split, "noncat_output",
                            out_dir_name,
                            "Uncategorized calls")


def is_directory(path_name):
    """Checks if a path is an actual directory."""
    if not os.path.isdir(path_name):
        dir_error = "%s is not a directory" % (path_name)
        raise argparse.ArgumentTypeError(dir_error)
    return path_name


def parse_args(argv):
    """Parses arguments passed in."""
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', action='store',
                        default="", dest="out_dir_name", type=is_directory,
                        help='Output Directory')
    parser.add_argument('sanitizer_trace', action='store',
                        type=argparse.FileType('r'),
                        help='File containing sanitizer traces filtered by '
                             'prune_sanitizer_output.py')
    parser.add_argument('symbol_trace', action='store',
                        type=argparse.FileType('r'),
                        help='File containing symbolized traces that match '
                             'sanitizer_trace')
    parser.add_argument('dex_starts', action='store',
                        type=argparse.FileType('r'),
                        help='File containing starting addresses of Dex Files')
    parser.add_argument('categories', action='store', nargs='*',
                        help='Keywords expected to show in large amounts of'
                             ' symbolized traces')

    return parser.parse_args(argv)


def read_data(parsed_argv):
    """Reads data from filepath arguments and parses them into lists."""
    # Using a dictionary to establish relation between lists added
    data_lists = {}
    categories = parsed_argv.categories
    # Makes sure each trace maps to some category
    categories.insert(0, "Uncategorized")

    logcat_file_data = parsed_argv.sanitizer_trace.readlines()
    parsed_argv.sanitizer_trace.close()

    symbol_file_split = parsed_argv.symbol_trace.read().split("Stack Trace")[
        1:]
    parsed_argv.symbol_trace.close()

    dex_start_file_data = parsed_argv.dex_starts.readlines()
    parsed_argv.dex_starts.close()

    # Each element is a tuple of time and address accessed
    data_lists["plot_list"] = [[elem[1] for elem in enumerate(line.split())
                                if elem[0] in (1, 11)
                                ]
                               for line in logcat_file_data
                               if "use-after-poison" in line
                               ]
    # Contains a mapping between traces and the category they belong to
    # based on arguments
    data_lists["cat_list"] = [categories[find_match(categories, trace)]
                              for trace in symbol_file_split]

    # Contains a list of starting address of all dex files to calculate dex
    # offsets
    data_lists["dex_start_list"] = [int(line.split("@")[1], 16)
                                    for line in dex_start_file_data
                                    if "RegisterDexFile" in line
                                    ]
    return data_lists, categories, symbol_file_split


def main(argv=None):
    """Takes in trace information and outputs details about them."""
    if argv is None:
        argv = sys.argv
    parsed_argv = parse_args(argv[1:])

    data_lists, categories, symbol_file_split = read_data(parsed_argv)
    # Formats plot_list such that each element is a data point
    absolute_to_relative(data_lists["plot_list"], data_lists["dex_start_list"],
                         data_lists["cat_list"])
    for file_ext, cat_name in enumerate(categories):
        out_file_name = os.path.join(parsed_argv.out_dir_name, "time_output_" +
                                     str(file_ext) +
                                     ".dat")
        with open(out_file_name, "w") as output_file:
            output_file.write("# Category: " + cat_name + "\n")
            output_file.write("# Time, Dex File Offset, Address \n")
            for time, dex_offset, category, address in data_lists["plot_list"]:
                if category == cat_name:
                    output_file.write(
                        str(time) +
                        " " +
                        str(dex_offset) +
                        " #" +
                        str(address) +
                        "\n")

    print_categories(categories, symbol_file_split, parsed_argv.out_dir_name)


if __name__ == '__main__':
    main()
