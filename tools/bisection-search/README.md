Bisection Bug Search
====================

Bisection Bug Search is a tool for finding compiler optimizations bugs. It
accepts a program which exposes a bug by producing incorrect output and expected
output for the program. It then attempts to narrow down the issue to a single
method and optimization pass under the assumption that interpreter is correct.

Given methods in order M0..Mn finds smallest i such that compiling Mi and
interpreting all other methods produces incorrect output. Then, given ordered
optimization passes P0..Pl, finds smallest j such that compiling Mi with passes
P0..Pj-1 produces expected output and compiling Mi with passes P0..Pj produces
incorrect output. Prints Mi and Pj.

How to run Bisection Bug Search
===============================

    bisection_search.py [-h] [-cp CLASSPATH] [--class CLASSNAME] [--lib LIB]
                               [--dalvikvm-option [OPT [OPT ...]]] [--arg [ARG [ARG ...]]]
                               [--image IMAGE] [--raw-cmd RAW_CMD]
                               [--64] [--device] [--expected-output EXPECTED_OUTPUT]
                               [--check-script CHECK_SCRIPT] [--verbose]

    Tool for finding compiler bugs. Either --raw-cmd or both -cp and --class are required.

    optional arguments:
      -h, --help                            show this help message and exit

    dalvikvm command options:
      -cp CLASSPATH, --classpath CLASSPATH  classpath
      --class CLASSNAME                     name of main class
      --lib LIB                             lib to use, default: libart.so
      --dalvikvm-option [OPT [OPT ...]]     additional dalvikvm option
      --arg [ARG [ARG ...]]                 argument passed to test
      --image IMAGE                         path to image
      --raw-cmd RAW_CMD                     bisect with this command, ignore other command options

    bisection options:
      --64                                  x64 mode
      --device                              run on device
      --expected-output EXPECTED_OUTPUT     file containing expected output
      --check-script CHECK_SCRIPT           script comparing output and expected output
      --verbose                             enable verbose output
