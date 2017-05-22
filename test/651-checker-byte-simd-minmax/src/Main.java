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
  /// CHECK-START-ARM64: void Main.doitMin(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:d\d+>>  VecMin [<<Get1>>,<<Get2>>] unsigned:false loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Min>>] loop:<<Loop>>      outer_loop:none
  private static void doitMin(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.min(y[i], z[i]);
    }
  }

  /// CHECK-START-ARM64: void Main.doitMinUnsigned(byte[], byte[], byte[]) loop_optimization (before)
  /// CHECK-DAG: <<I255:i\d+>> IntConstant 255                     loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<And1:i\d+>> And [<<Get1>>,<<I255>>]             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<And2:i\d+>> And [<<Get2>>,<<I255>>]             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:i\d+>>  InvokeStaticOrDirect [<<And1>>,<<And2>>] intrinsic:MathMinIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Min>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.doitMinUnsigned(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Min:d\d+>>  VecMin [<<Get1>>,<<Get2>>] unsigned:true loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Min>>] loop:<<Loop>>      outer_loop:none
  private static void doitMinUnsigned(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.min(y[i] & 0xff, z[i] & 0xff);
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
  /// CHECK-START-ARM64: void Main.doitMax(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:d\d+>>  VecMax [<<Get1>>,<<Get2>>] unsigned:false loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Max>>] loop:<<Loop>>      outer_loop:none
  private static void doitMax(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.max(y[i], z[i]);
    }
  }

  /// CHECK-START-ARM64: void Main.doitMaxUnsigned(byte[], byte[], byte[]) loop_optimization (before)
  /// CHECK-DAG: <<I255:i\d+>> IntConstant 255                     loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:b\d+>> ArrayGet                            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<And1:i\d+>> And [<<Get1>>,<<I255>>]             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<And2:i\d+>> And [<<Get2>>,<<I255>>]             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:i\d+>>  InvokeStaticOrDirect [<<And1>>,<<And2>>] intrinsic:MathMaxIntInt loop:<<Loop>> outer_loop:none
  /// CHECK-DAG: <<Cnv:b\d+>>  TypeConversion [<<Max>>]            loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<Cnv>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.doitMaxUnsigned(byte[], byte[], byte[]) loop_optimization (after)
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Get1:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Get2:d\d+>> VecLoad                             loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: <<Max:d\d+>>  VecMax [<<Get1>>,<<Get2>>] unsigned:true loop:<<Loop>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Max>>] loop:<<Loop>>      outer_loop:none
  private static void doitMaxUnsigned(byte[] x, byte[] y, byte[] z) {
    int min = Math.min(x.length, Math.min(y.length, z.length));
    for (int i = 0; i < min; i++) {
      x[i] = (byte) Math.max(y[i] & 0xff, z[i] & 0xff);
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
    doitMinUnsigned(x, y, z);
    for (int i = 0; i < total; i++) {
      byte expected = (byte) Math.min(y[i] & 0xff, z[i] & 0xff);
      expectEquals(expected, x[i]);
    }
    doitMax(x, y, z);
    for (int i = 0; i < total; i++) {
      byte expected = (byte) Math.max(y[i], z[i]);
      expectEquals(expected, x[i]);
    }
    doitMaxUnsigned(x, y, z);
    for (int i = 0; i < total; i++) {
      byte expected = (byte) Math.max(y[i] & 0xff, z[i] & 0xff);
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
