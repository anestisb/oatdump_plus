# The goal of this proguard configuration is to obfuscate the test-dump
# program so that the heap dump it generates is an obfuscated heap dump.
# This allows us to test that deobfuscation of the generated heap dump is
# working properly.

# All we care about is obfuscation. Don't do any other optimizations.
-dontpreverify
-dontoptimize
-dontshrink

-keep public class Main {
  public static void main(java.lang.String[]);
}

-printmapping proguard.map
