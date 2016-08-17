JavaFuzz
========

JavaFuzz is tool for generating random Java programs with the objective of
fuzz testing the ART infrastructure. Each randomly generated Java program
can be run under various modes of execution, such as using the interpreter,
using the optimizing compiler, using an external reference implementation,
or using various target architectures. Any difference between the outputs
(a divergence) may indicate a bug in one of the execution modes.

JavaFuzz can be combined with dexfuzz to get multilayered fuzz testing.

How to run JavaFuzz
===================

    javafuzz [-s seed] [-d expr-depth] [-l stmt-length]
             [-i if-nest] [-n loop-nest]

where

    -s : defines a deterministic random seed
         (randomized using time by default)
    -d : defines a fuzzing depth for expressions
         (higher values yield deeper expressions)
    -l : defines a fuzzing length for statement lists
         (higher values yield longer statement sequences)
    -i : defines a fuzzing nest for if/switch statements
         (higher values yield deeper nested conditionals)
    -n : defines a fuzzing nest for for/while/do-while loops
         (higher values yield deeper nested loops)

The current version of JavaFuzz sends all output to stdout, and uses
a fixed testing class named Test. So a typical test run looks as follows.

    javafuzz > Test.java
    jack -cp ${JACK_CLASSPATH} --output-dex . Test.java
    art -classpath classes.dex Test

Background
==========

Although test suites are extremely useful to validate the correctness of a
system and to ensure that no regressions occur, any test suite is necessarily
finite in size and scope. Tests typically focus on validating particular
features by means of code sequences most programmers would expect. Regression
tests often use slightly less idiomatic code sequences, since they reflect
problems that were not anticipated originally, but occurred “in the field”.
Still, any test suite leaves the developer wondering whether undetected bugs
and flaws still linger in the system.

Over the years, fuzz testing has gained popularity as a testing technique for
discovering such lingering bugs, including bugs that can bring down a system in
an unexpected way. Fuzzing refers to feeding a large amount of random data as
input to a system in an attempt to find bugs or make it crash. Mutation-based
fuzz testing is a special form of fuzzing that applies small random changes to
existing inputs in order to detect shortcomings in a system. Profile-guided or
coverage-guided fuzzing adds a direction to the way these random changes are
applied. Multilayer approaches generate random inputs that are subsequently
mutated at various stages of execution.

The randomness of fuzz testing implies that the size and scope of testing is no
longer bounded. Every new run can potentially discover bugs and crashes that were
hereto undetected.
