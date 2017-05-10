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
 * Tests for MIN/MAX vectorization.
 */
public class Main {

  /// CHECK-START: void Main.doitMin(long[], long[], long[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:j\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:j\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:j\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMinLongLong loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Min>>] loop:<<Loop>>      outer_loop:none
  //
  // Not directly supported for longs.
  //
  /// CHECK-START: void Main.doitMin(long[], long[], long[]) loop_optimization (after)
  /// CHECK-NOT: VecMin
  private static void doitMin(long[] x, long[] y, long[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = Math.min(y[i], z[i]);
    }
  }

  /// CHECK-START: void Main.doitMax(long[], long[], long[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:j\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:j\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:j\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMaxLongLong loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Max>>] loop:<<Loop>>      outer_loop:none
  //
  // Not directly supported for longs.
  //
  /// CHECK-START: void Main.doitMax(long[], long[], long[]) loop_optimization (after)
  /// CHECK-NOT: VecMax
  private static void doitMax(long[] x, long[] y, long[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = Math.max(y[i], z[i]);
    }
  }

  public static void main(String[] args) {
    long[] interesting = {
      0x0000000000000000L, 0x0000000000000001L, 0x000000007fffffffL,
      0x0000000080000000L, 0x0000000080000001L, 0x00000000ffffffffL,
      0x0000000100000000L, 0x0000000100000001L, 0x000000017fffffffL,
      0x0000000180000000L, 0x0000000180000001L, 0x00000001ffffffffL,
      0x7fffffff00000000L, 0x7fffffff00000001L, 0x7fffffff7fffffffL,
      0x7fffffff80000000L, 0x7fffffff80000001L, 0x7fffffffffffffffL,
      0x8000000000000000L, 0x8000000000000001L, 0x800000007fffffffL,
      0x8000000080000000L, 0x8000000080000001L, 0x80000000ffffffffL,
      0x8000000100000000L, 0x8000000100000001L, 0x800000017fffffffL,
      0x8000000180000000L, 0x8000000180000001L, 0x80000001ffffffffL,
      0xffffffff00000000L, 0xffffffff00000001L, 0xffffffff7fffffffL,
      0xffffffff80000000L, 0xffffffff80000001L, 0xffffffffffffffffL
    };
    // Initialize cross-values for the interesting values.
    int total = interesting.length * interesting.length;
    long[] x = new long[total];
    long[] y = new long[total];
    long[] z = new long[total];
    int k = 0;
    for (int i = 0; i < interesting.length; i++) {
      for (int j = 0; j < interesting.length; j++) {
        x[k] = 0;
        y[k] = interesting[i];
        z[k] = interesting[j];
        k++;
      }
    }

    // And test.
    doitMin(x, y, z);
    for (int i = 0; i < total; i++) {
      long expected = Math.min(y[i], z[i]);
      expectEquals(expected, x[i]);
    }
    doitMax(x, y, z);
    for (int i = 0; i < total; i++) {
      long expected = Math.max(y[i], z[i]);
      expectEquals(expected, x[i]);
    }

    System.out.println("passed");
  }

  private static void expectEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
