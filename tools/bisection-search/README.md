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

    bisection_search.py [-h] -cp CLASSPATH
                        [--expected-output EXPECTED_OUTPUT] [--device]
                        [--lib LIB] [--64]
                        [--dalvikvm-option [OPTION [OPTION ...]]]
                        [--arg [TEST_ARGS [TEST_ARGS ...]]] [--image IMAGE]
                        [--verbose]
                        classname

    positional arguments:
      classname             name of class to run

    optional arguments:
      -h, --help            show this help message and exit
      -cp CLASSPATH, --classpath CLASSPATH
                            classpath
      --expected-output EXPECTED_OUTPUT
                            file containing expected output
      --device              run on device
      --lib LIB             lib to use, default: libart.so
      --64                  x64 mode
      --dalvikvm-option [OPTION [OPTION ...]]
                            additional dalvikvm option
      --arg [TEST_ARGS [TEST_ARGS ...]]
                            argument to pass to program
      --image IMAGE         path to image
      --verbose             enable verbose output
