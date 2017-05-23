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

  /// CHECK-START: void Main.doitMin(byte[], byte[], byte[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:i\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMinIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Min>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO: narrow type vectorization.
  /// CHECK-START: void Main.doitMin(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-NOT: VecMin
  private static void doitMin(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.min(y[i], z[i]);
    }
  }

  /// CHECK-START: void Main.doitMax(byte[], byte[], byte[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:i\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMaxIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Max>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO: narrow type vectorization.
  /// CHECK-START: void Main.doitMax(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-NOT: VecMax
  private static void doitMax(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.max(y[i], z[i]);
    }
  }

  public static void main(String[] args) {
    // Initialize cross-values for all possible values.
    int total = 256 * 256;
    byte[] x = new byte[total];
    byte[] y = new byte[total];
    byte[] z = new byte[total];
    int k = 0;
    for (int i = 0; i < 256; i++) {
      for (int j = 0; j < 256; j++) {
        x[k] = 0;
        y[k] = (byte) i;
        z[k] = (byte) j;
        k++;
      }
    }

    // And test.
    doitMin(x, y, z);
    for (int i = 0; i < total; i++) {
      byte expected = (byte) Math.min(y[i], z[i]);
      expectEquals(expected, x[i]);
    }
    doitMax(x, y, z);
    for (int i = 0; i < total; i++) {
      byte expected = (byte) Math.max(y[i], z[i]);
      expectEquals(expected, x[i]);
    }

    System.out.println("passed");
  }

  private static void expectEquals(byte expected, byte result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
