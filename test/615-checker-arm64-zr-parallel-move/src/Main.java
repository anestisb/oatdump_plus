/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

public class Main {

  public static boolean doThrow = false;

  public void $noinline$foo(int in_w1,
                            int in_w2,
                            int in_w3,
                            int in_w4,
                            int in_w5,
                            int in_w6,
                            int in_w7,
                            int on_stack_int,
                            long on_stack_long,
                            float in_s0,
                            float in_s1,
                            float in_s2,
                            float in_s3,
                            float in_s4,
                            float in_s5,
                            float in_s6,
                            float in_s7,
                            float on_stack_float,
                            double on_stack_double) {
    if (doThrow) throw new Error();
  }

  // We expect a parallel move that moves four times the zero constant to stack locations.
  /// CHECK-START-ARM64: void Main.bar() register (after)
  /// CHECK:             ParallelMove {{.*#0->[0-9x]+\(sp\).*#0->[0-9x]+\(sp\).*#0->[0-9x]+\(sp\).*#0->[0-9x]+\(sp\).*}}

  // Those four moves should generate four 'store' instructions using directly the zero register.
  /// CHECK-START-ARM64: void Main.bar() disassembly (after)
  /// CHECK-DAG:         {{(str|stur)}} wzr, [sp, #{{[0-9]+}}]
  /// CHECK-DAG:         {{(str|stur)}} xzr, [sp, #{{[0-9]+}}]
  /// CHECK-DAG:         {{(str|stur)}} wzr, [sp, #{{[0-9]+}}]
  /// CHECK-DAG:         {{(str|stur)}} xzr, [sp, #{{[0-9]+}}]

  public void bar() {
    $noinline$foo(1, 2, 3, 4, 5, 6, 7,     // Integral values in registers.
                  0, 0L,                   // Integral values on the stack.
                  1, 2, 3, 4, 5, 6, 7, 8,  // Floating-point values in registers.
                  0.0f, 0.0);              // Floating-point values on the stack.
  }

  public static void main(String args[]) {}
}
