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

  /// CHECK-START: void Main.doitMin(char[], char[], char[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:c\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:c\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:i\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMinIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:c\d+>>  TypeConversion [<<Min>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO: narrow type vectorization.
  /// CHECK-START: void Main.doitMin(char[], char[], char[]) loop_optimization (after)
  /// CHECK-NOT: VecMin
  private static void doitMin(char[] x, char[] y, char[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (char) Math.min(y[i], z[i]);
    }
  }

  /// CHECK-START: void Main.doitMax(char[], char[], char[]) loop_optimization (before)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:c\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:c\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:i\d+>>  InvokeStaticOrDirect [<<Get1>>,<<Get2>>] intrinsic:MathMaxIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:c\d+>>  TypeConversion [<<Max>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  // TODO: narrow type vectorization.
  /// CHECK-START: void Main.doitMax(char[], char[], char[]) loop_optimization (after)
  /// CHECK-NOT: VecMax
  private static void doitMax(char[] x, char[] y, char[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (char) Math.max(y[i], z[i]);
    }
  }

  public static void main(String[] args) {
    char[] interesting = {
      0x0000, 0x0001, 0x007f, 0x0080, 0x0081, 0x00ff,
      0x0100, 0x0101, 0x017f, 0x0180, 0x0181, 0x01ff,
      0x7f00, 0x7f01, 0x7f7f, 0x7f80, 0x7f81, 0x7fff,
      0x8000, 0x8001, 0x807f, 0x8080, 0x8081, 0x80ff,
      0x8100, 0x8101, 0x817f, 0x8180, 0x8181, 0x81ff,
      0xff00, 0xff01, 0xff7f, 0xff80, 0xff81, 0xffff
    };
    // Initialize cross-values for the interesting values.
    int total = interesting.length * interesting.length;
    char[] x = new char[total];
    char[] y = new char[total];
    char[] z = new char[total];
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
      char expected = (char) Math.min(y[i], z[i]);
      expectEquals(expected, x[i]);
    }
    doitMax(x, y, z);
    for (int i = 0; i < total; i++) {
      char expected = (char) Math.max(y[i], z[i]);
      expectEquals(expected, x[i]);
    }

    System.out.println("passed");
  }

  private static void expectEquals(char expected, char result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
