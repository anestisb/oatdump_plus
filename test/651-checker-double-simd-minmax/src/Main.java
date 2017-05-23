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

  /// CHECK-START: void Main.doitMin(double[], double[], double[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:d\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMinDoubleDouble loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Min>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO x86: 0.0 vs -0.0?
  //
  /// CHECK-START-ARM64: void Main.doitMin(double[], double[], double[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:d\d+>>  VecMin [<<Get1>>,<<Get2>>]          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Min>>] loop:<<Loop>>      outer_loop:none
  private static void doitMin(double[] x, double[] y, double[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = Math.min(y[i], z[i]);
    }
  }

  /// CHECK-START: void Main.doitMax(double[], double[], double[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:d\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMaxDoubleDouble loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Max>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO x86: 0.0 vs -0.0?
  //
  /// CHECK-START-ARM64: void Main.doitMax(double[], double[], double[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:d\d+>>  VecMax [<<Get1>>,<<Get2>>]          loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Max>>] loop:<<Loop>>      outer_loop:none
  private static void doitMax(double[] x, double[] y, double[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = Math.max(y[i], z[i]);
    }
  }

  public static void main(String[] args) {
    double[] interesting = {
      -0.0f,
      +0.0f,
      -1.0f,
      +1.0f,
      -3.14f,
      +3.14f,
      -100.0f,
      +100.0f,
      -4444.44f,
      +4444.44f,
      Double.MIN_NORMAL,
      Double.MIN_VALUE,
      Double.MAX_VALUE,
      Double.NEGATIVE_INFINITY,
      Double.POSITIVE_INFINITY,
      Double.NaN
    };
    // Initialize cross-values for the interesting values.
    int total = interesting.length * interesting.length;
    double[] x = new double[total];
    double[] y = new double[total];
    double[] z = new double[total];
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
      double expected = Math.min(y[i], z[i]);
      expectEquals(expected, x[i]);
    }
    doitMax(x, y, z);
    for (int i = 0; i < total; i++) {
      double expected = Math.max(y[i], z[i]);
      expectEquals(expected, x[i]);
    }

    System.out.println("passed");
  }

  private static void expectEquals(double expected, double result) {
    // Tests the bits directly. This distinguishes correctly between +0.0
    // and -0.0 and returns a canonical representation for all NaN.
    long expected_bits = Double.doubleToLongBits(expected);
    long result_bits = Double.doubleToLongBits(result);
    if (expected_bits != result_bits) {
      throw new Error("Expected: " + expected +
          "(0x" + Long.toHexString(expected_bits) + "), found: " + result +
          "(0x" + Long.toHexString(result_bits) + ")");
    }
  }
}
