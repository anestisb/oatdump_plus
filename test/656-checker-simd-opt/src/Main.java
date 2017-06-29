/*
 * Copyright (C) 2017 The Android Open Source Project
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

/**
 * Tests for SIMD related optimizations.
 */
public class Main {

  /// CHECK-START: void Main.unroll(float[], float[]) loop_optimization (before)
  /// CHECK-DAG: <<Cons:f\d+>> FloatConstant 2.5                   loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get:f\d+>>  ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Mul:f\d+>>  Mul [<<Get>>,<<Cons>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Mul>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.unroll(float[], float[]) loop_optimization (after)
  /// CHECK-DAG: <<Cons:f\d+>> FloatConstant 2.5                    loop:none
  /// CHECK-DAG: <<Incr:i\d+>> IntConstant 4                        loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<Cons>>]        loop:none
  /// CHECK-NOT:               VecReplicateScalar
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad [{{l\d+}},<<Phi>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Mul1:d\d+>> VecMul [<<Get1>>,<<Repl>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Mul1>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add:i\d+>>  Add [<<Phi>>,<<Incr>>]               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad [{{l\d+}},<<Add>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Mul2:d\d+>> VecMul [<<Get2>>,<<Repl>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Add>>,<<Mul2>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               Add [<<Add>>,<<Incr>>]               loop:<<Loop>>      outer_loop:none
  private static void unroll(float[] x, float[] y) {
    for (int i = 0; i < 100; i++) {
      x[i] = y[i] * 2.5f;
    }
  }

  /// CHECK-START: void Main.stencil(int[], int[], int) loop_optimization (before)
  /// CHECK-DAG: <<CP1:i\d+>>   IntConstant 1                        loop:none
  /// CHECK-DAG: <<CM1:i\d+>>   IntConstant -1                       loop:none
  /// CHECK-DAG: <<Phi:i\d+>>   Phi                                  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Add1:i\d+>>  Add [<<Phi>>,<<CM1>>]                loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:i\d+>>  ArrayGet [{{l\d+}},<<Add1>>]         loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:i\d+>>  ArrayGet [{{l\d+}},<<Phi>>]          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add2:i\d+>>  Add [<<Get1>>,<<Get2>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add3:i\d+>>  Add [<<Phi>>,<<CP1>>]                loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get3:i\d+>>  ArrayGet [{{l\d+}},<<Add3>>]         loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add4:i\d+>>  Add [<<Add2>>,<<Get3>>]              loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                ArraySet [{{l\d+}},<<Phi>>,<<Add4>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.stencil(int[], int[], int) loop_optimization (after)
  /// CHECK-DAG: <<CP1:i\d+>>   IntConstant 1                         loop:none
  /// CHECK-DAG: <<CP2:i\d+>>   IntConstant 2                         loop:none
  /// CHECK-DAG: <<Phi:i\d+>>   Phi                                   loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Add1:i\d+>>  Add [<<Phi>>,<<CP1>>]                 loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>>  VecLoad [{{l\d+}},<<Phi>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>>  VecLoad [{{l\d+}},<<Add1>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add2:d\d+>>  VecAdd [<<Get1>>,<<Get2>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add3:i\d+>>  Add [<<Phi>>,<<CP2>>]                 loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get3:d\d+>>  VecLoad [{{l\d+}},<<Add3>>]           loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Add4:d\d+>>  VecAdd [<<Add2>>,<<Get3>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:                VecStore [{{l\d+}},<<Add1>>,<<Add4>>] loop:<<Loop>>      outer_loop:none
  private static void stencil(int[] a, int[] b, int n) {
    for (int i = 1; i < n - 1; i++) {
      a[i] = b[i - 1] + b[i] + b[i + 1];
    }
  }

  public static void main(String[] args) {
    float[] x = new float[100];
    float[] y = new float[100];
    for (int i = 0; i < 100; i++) {
      x[i] = 0.0f;
      y[i] = 2.0f;
    }
    unroll(x, y);
    for (int i = 0; i < 100; i++) {
      expectEquals(5.0f, x[i]);
      expectEquals(2.0f, y[i]);
    }
    int[] a = new int[100];
    int[] b = new int[100];
    for (int i = 0; i < 100; i++) {
      a[i] = 0;
      b[i] = i;
    }
    stencil(a, b, 100);
    for (int i = 1; i < 99; i++) {
      int e = i + i + i;
      expectEquals(e, a[i]);
      expectEquals(i, b[i]);
    }
    System.out.println("passed");
  }

  private static void expectEquals(float expected, float result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
