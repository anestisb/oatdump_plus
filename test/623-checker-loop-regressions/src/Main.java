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

/**
 * Regression tests for loop optimizations.
 */
public class Main {

  /// CHECK-START: int Main.earlyExitFirst(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitFirst(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int earlyExitFirst(int m) {
    int k = 0;
    for (int i = 0; i < 10; i++) {
      if (i == m) {
        return k;
      }
      k++;
    }
    return k;
  }

  /// CHECK-START: int Main.earlyExitLast(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitLast(int) loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int earlyExitLast(int m) {
    int k = 0;
    for (int i = 0; i < 10; i++) {
      k++;
      if (i == m) {
        return k;
      }
    }
    return k;
  }

  /// CHECK-START: int Main.earlyExitNested() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: Phi loop:<<Loop2>>      outer_loop:<<Loop1>>
  //
  /// CHECK-START: int Main.earlyExitNested() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop1>>      outer_loop:none
  //
  /// CHECK-START: int Main.earlyExitNested() loop_optimization (after)
  /// CHECK-NOT: Phi loop:{{B\d+}} outer_loop:{{B\d+}}
  static int earlyExitNested() {
    int offset = 0;
    for (int i = 0; i < 2; i++) {
      int start = offset;
      // This loop can be removed.
      for (int j = 0; j < 2; j++) {
        offset++;
      }
      if (i == 1) {
        return start;
      }
    }
    return 0;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly.
  //
  /// CHECK-START: int Main.transferNarrowWrap() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.transferNarrowWrap() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int transferNarrowWrap() {
    short x = 0;
    int w = 10;
    int v = 3;
    for (int i = 0; i < 10; i++) {
      v = w + 1;    // transfer on wrap-around
      w = x;   // wrap-around
      x += 2;  // narrowing linear
    }
    return v;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly
  // (currently rejected, could be improved).
  //
  /// CHECK-START: int Main.polynomialShort() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialShort() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int polynomialShort() {
    int x = 0;
    for (short i = 0; i < 10; i++) {
      x = x - i;  // polynomial on narrowing linear
    }
    return x;
  }

  // Regression test for b/33774618: transfer operations involving
  // narrowing linear induction should be done correctly
  // (currently rejected, could be improved).
  //
  /// CHECK-START: int Main.polynomialIntFromLong() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialIntFromLong() loop_optimization (after)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  static int polynomialIntFromLong() {
    int x = 0;
    for (long i = 0; i < 10; i++) {
      x = x - (int) i;  // polynomial on narrowing linear
    }
    return x;
  }

  /// CHECK-START: int Main.polynomialInt() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.polynomialInt() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.polynomialInt() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>>  IntConstant -45  loop:none
  /// CHECK-DAG:               Return [<<Int>>] loop:none
  static int polynomialInt() {
    int x = 0;
    for (int i = 0; i < 10; i++) {
      x = x - i;
    }
    return x;
  }

  // Regression test for b/34779592 (found with fuzz testing): overflow for last value
  // of division truncates to zero, for multiplication it simply truncates.
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.geoIntDivLastValue(int) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Int:i\d+>> IntConstant 0    loop:none
  /// CHECK-DAG:              Return [<<Int>>] loop:none
  static int geoIntDivLastValue(int x) {
    for (int i = 0; i < 2; i++) {
      x /= 1081788608;
    }
    return x;
  }

  /// CHECK-START: int Main.geoIntMulLastValue(int) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: int Main.geoIntMulLastValue(int) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: int Main.geoIntMulLastValue(int) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Par:i\d+>> ParameterValue         loop:none
  /// CHECK-DAG: <<Int:i\d+>> IntConstant -194211840 loop:none
  /// CHECK-DAG: <<Mul:i\d+>> Mul [<<Par>>,<<Int>>]  loop:none
  /// CHECK-DAG:              Return [<<Mul>>]       loop:none
  static int geoIntMulLastValue(int x) {
    for (int i = 0; i < 2; i++) {
      x *= 1081788608;
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongDivLastValue(long) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongDivLastValue(long) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongDivLastValue(long) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Long:j\d+>> LongConstant 0    loop:none
  /// CHECK-DAG:               Return [<<Long>>] loop:none
  //
  // Tests overflow in the divisor (while updating intermediate result).
  static long geoLongDivLastValue(long x) {
    for (int i = 0; i < 10; i++) {
      x /= 1081788608;
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongDivLastValue() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongDivLastValue() loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongDivLastValue() instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Long:j\d+>> LongConstant 0    loop:none
  /// CHECK-DAG:               Return [<<Long>>] loop:none
  //
  // Tests overflow in the divisor (while updating base).
  static long geoLongDivLastValue() {
    long x = -1;
    for (int i2 = 0; i2 < 2; i2++) {
      x /= (Long.MAX_VALUE);
    }
    return x;
  }

  /// CHECK-START: long Main.geoLongMulLastValue(long) loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: long Main.geoLongMulLastValue(long) loop_optimization (after)
  /// CHECK-NOT: Phi
  //
  /// CHECK-START: long Main.geoLongMulLastValue(long) instruction_simplifier$after_bce (after)
  /// CHECK-DAG: <<Par:j\d+>>  ParameterValue                    loop:none
  /// CHECK-DAG: <<Long:j\d+>> LongConstant -8070450532247928832 loop:none
  /// CHECK-DAG: <<Mul:j\d+>>  Mul [<<Par>>,<<Long>>]            loop:none
  /// CHECK-DAG:               Return [<<Mul>>]                  loop:none
  static long geoLongMulLastValue(long x) {
    for (int i = 0; i < 10; i++) {
      x *= 1081788608;
    }
    return x;
  }

  // If vectorized, the narrowing subscript should not cause
  // type inconsistencies in the synthesized code.
  static void narrowingSubscript(float[] a) {
    float val = 2.0f;
    for (long i = 0; i < a.length; i++) {
      a[(int) i] += val;
    }
  }

  // If vectorized, invariant stride should be recognized
  // as a reduction, not a unit stride in outer loop.
  static void reduc(int[] xx, int[] yy) {
    for (int i0 = 0; i0 < 2; i0++) {
      for (int i1 = 0; i1 < 469; i1++) {
        xx[i0] -= (++yy[i1]);
      }
    }
  }

  /// CHECK-START: void Main.string2Bytes(char[], java.lang.String) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.string2Bytes(char[], java.lang.String) loop_optimization (after)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecLoad  loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  //
  // NOTE: should correctly deal with compressed and uncompressed cases.
  private static void string2Bytes(char[] a, String b) {
    int min = Math.min(a.length, b.length());
    for (int i = 0; i < min; i++) {
      a[i] = b.charAt(i);
    }
  }

  // A strange function that does not inline.
  private static void $noinline$foo(boolean x, int n) {
    if (n < 0)
      throw new Error("oh no");
    if (n > 100) {
      $noinline$foo(!x, n - 1);
      $noinline$foo(!x, n - 2);
      $noinline$foo(!x, n - 3);
      $noinline$foo(!x, n - 4);
    }
  }

  // A loop with environment uses of x (the terminating condition). As exposed by bug
  // b/37247891, the loop can be unrolled, but should handle the (unlikely, but clearly
  // not impossible) environment uses of the terminating condition in a correct manner.
  private static void envUsesInCond() {
    boolean x = false;
    for (int i = 0; !(x = i >= 1); i++) {
      $noinline$foo(true, i);
    }
  }

  /// CHECK-START: void Main.oneBoth(short[], char[]) loop_optimization (before)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                       loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                 loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<One>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               ArraySet [{{l\d+}},<<Phi>>,<<One>>] loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START-ARM64: void Main.oneBoth(short[], char[]) loop_optimization (after)
  /// CHECK-DAG: <<One:i\d+>>  IntConstant 1                        loop:none
  /// CHECK-DAG: <<Repl:d\d+>> VecReplicateScalar [<<One>>]         loop:none
  /// CHECK-DAG: <<Phi:i\d+>>  Phi                                  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Repl>>] loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               VecStore [{{l\d+}},<<Phi>>,<<Repl>>] loop:<<Loop>>      outer_loop:none
  //
  // Bug b/37764324: integral same-length packed types can be mixed freely.
  private static void oneBoth(short[] a, char[] b) {
    for (int i = 0; i < Math.min(a.length, b.length); i++) {
      a[i] = 1;
      b[i] = 1;
    }
  }

  // Bug b/37768917: potential dynamic BCE vs. loop optimizations
  // case should be deal with correctly (used to DCHECK fail).
  private static void arrayInTripCount(int[] a, byte[] b, int n) {
    for (int k = 0; k < n; k++) {
      for (int i = 0, u = a[0]; i < u; i++) {
        b[i] += 2;
      }
    }
  }

  public static void main(String[] args) {
    expectEquals(10, earlyExitFirst(-1));
    for (int i = 0; i <= 10; i++) {
      expectEquals(i, earlyExitFirst(i));
    }
    expectEquals(10, earlyExitFirst(11));

    expectEquals(10, earlyExitLast(-1));
    for (int i = 0; i < 10; i++) {
      expectEquals(i + 1, earlyExitLast(i));
    }
    expectEquals(10, earlyExitLast(10));
    expectEquals(10, earlyExitLast(11));

    expectEquals(2, earlyExitNested());

    expectEquals(17, transferNarrowWrap());
    expectEquals(-45, polynomialShort());
    expectEquals(-45, polynomialIntFromLong());
    expectEquals(-45, polynomialInt());

    expectEquals(0, geoIntDivLastValue(0));
    expectEquals(0, geoIntDivLastValue(1));
    expectEquals(0, geoIntDivLastValue(2));
    expectEquals(0, geoIntDivLastValue(1081788608));
    expectEquals(0, geoIntDivLastValue(-1081788608));
    expectEquals(0, geoIntDivLastValue(2147483647));
    expectEquals(0, geoIntDivLastValue(-2147483648));

    expectEquals(          0, geoIntMulLastValue(0));
    expectEquals( -194211840, geoIntMulLastValue(1));
    expectEquals( -388423680, geoIntMulLastValue(2));
    expectEquals(-1041498112, geoIntMulLastValue(1081788608));
    expectEquals( 1041498112, geoIntMulLastValue(-1081788608));
    expectEquals(  194211840, geoIntMulLastValue(2147483647));
    expectEquals(          0, geoIntMulLastValue(-2147483648));

    expectEquals(0L, geoLongDivLastValue(0L));
    expectEquals(0L, geoLongDivLastValue(1L));
    expectEquals(0L, geoLongDivLastValue(2L));
    expectEquals(0L, geoLongDivLastValue(1081788608L));
    expectEquals(0L, geoLongDivLastValue(-1081788608L));
    expectEquals(0L, geoLongDivLastValue(2147483647L));
    expectEquals(0L, geoLongDivLastValue(-2147483648L));
    expectEquals(0L, geoLongDivLastValue(9223372036854775807L));
    expectEquals(0L, geoLongDivLastValue(-9223372036854775808L));

    expectEquals(0L, geoLongDivLastValue());

    expectEquals(                   0L, geoLongMulLastValue(0L));
    expectEquals(-8070450532247928832L, geoLongMulLastValue(1L));
    expectEquals( 2305843009213693952L, geoLongMulLastValue(2L));
    expectEquals(                   0L, geoLongMulLastValue(1081788608L));
    expectEquals(                   0L, geoLongMulLastValue(-1081788608L));
    expectEquals( 8070450532247928832L, geoLongMulLastValue(2147483647L));
    expectEquals(                   0L, geoLongMulLastValue(-2147483648L));
    expectEquals( 8070450532247928832L, geoLongMulLastValue(9223372036854775807L));
    expectEquals(                   0L, geoLongMulLastValue(-9223372036854775808L));

    float[] a = new float[16];
    narrowingSubscript(a);
    for (int i = 0; i < 16; i++) {
      expectEquals(2.0f, a[i]);
    }

    int[] xx = new int[2];
    int[] yy = new int[469];
    reduc(xx, yy);
    expectEquals(-469, xx[0]);
    expectEquals(-938, xx[1]);
    for (int i = 0; i < 469; i++) {
      expectEquals(2, yy[i]);
    }

    char[] aa = new char[23];
    String bb = "hello world how are you";
    string2Bytes(aa, bb);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], bb.charAt(i));
    }
    String cc = "\u1010\u2020llo world how are y\u3030\u4040";
    string2Bytes(aa, cc);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], cc.charAt(i));
    }

    envUsesInCond();

    short[] dd = new short[23];
    oneBoth(dd, aa);
    for (int i = 0; i < aa.length; i++) {
      expectEquals(aa[i], 1);
      expectEquals(dd[i], 1);
    }

    xx[0] = 10;
    byte[] bt = new byte[10];
    arrayInTripCount(xx, bt, 20);
    for (int i = 0; i < bt.length; i++) {
      expectEquals(40, bt[i]);
    }

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  private static void expectEquals(float expected, float result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
