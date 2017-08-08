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
 * Tests for simple integral reductions: same type for accumulator and data.
 */
public class Main {

  static final int N = 500;

  //
  // Basic reductions in loops.
  //

  // TODO: vectorize these (second step of b/64091002 plan)

  private static byte reductionByte(byte[] x) {
    byte sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static short reductionShort(short[] x) {
    short sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static char reductionChar(char[] x) {
    char sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static int reductionInt(int[] x) {
    int sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static long reductionLong(long[] x) {
    long sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static byte reductionMinusByte(byte[] x) {
    byte sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static short reductionMinusShort(short[] x) {
    short sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static char reductionMinusChar(char[] x) {
    char sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static int reductionMinusInt(int[] x) {
    int sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static long reductionMinusLong(long[] x) {
    long sum = 0;
    for (int i = 0; i < x.length; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static byte reductionMinByte(byte[] x) {
    byte min = Byte.MAX_VALUE;
    for (int i = 0; i < x.length; i++) {
      min = (byte) Math.min(min, x[i]);
    }
    return min;
  }

  private static short reductionMinShort(short[] x) {
    short min = Short.MAX_VALUE;
    for (int i = 0; i < x.length; i++) {
      min = (short) Math.min(min, x[i]);
    }
    return min;
  }

  private static char reductionMinChar(char[] x) {
    char min = Character.MAX_VALUE;
    for (int i = 0; i < x.length; i++) {
      min = (char) Math.min(min, x[i]);
    }
    return min;
  }

  private static int reductionMinInt(int[] x) {
    int min = Integer.MAX_VALUE;
    for (int i = 0; i < x.length; i++) {
      min = Math.min(min, x[i]);
    }
    return min;
  }

  private static long reductionMinLong(long[] x) {
    long min = Long.MAX_VALUE;
    for (int i = 0; i < x.length; i++) {
      min = Math.min(min, x[i]);
    }
    return min;
  }

  private static byte reductionMaxByte(byte[] x) {
    byte max = Byte.MIN_VALUE;
    for (int i = 0; i < x.length; i++) {
      max = (byte) Math.max(max, x[i]);
    }
    return max;
  }

  private static short reductionMaxShort(short[] x) {
    short max = Short.MIN_VALUE;
    for (int i = 0; i < x.length; i++) {
      max = (short) Math.max(max, x[i]);
    }
    return max;
  }

  private static char reductionMaxChar(char[] x) {
    char max = Character.MIN_VALUE;
    for (int i = 0; i < x.length; i++) {
      max = (char) Math.max(max, x[i]);
    }
    return max;
  }

  private static int reductionMaxInt(int[] x) {
    int max = Integer.MIN_VALUE;
    for (int i = 0; i < x.length; i++) {
      max = Math.max(max, x[i]);
    }
    return max;
  }

  private static long reductionMaxLong(long[] x) {
    long max = Long.MIN_VALUE;
    for (int i = 0; i < x.length; i++) {
      max = Math.max(max, x[i]);
    }
    return max;
  }

  //
  // A few special cases.
  //

  // TODO: consider unrolling

  private static int reductionInt10(int[] x) {
    int sum = 0;
    // Amenable to complete unrolling.
    for (int i = 10; i <= 10; i++) {
      sum += x[i];
    }
    return sum;
  }

  private static int reductionMinusInt10(int[] x) {
    int sum = 0;
    // Amenable to complete unrolling.
    for (int i = 10; i <= 10; i++) {
      sum -= x[i];
    }
    return sum;
  }

  private static int reductionMinInt10(int[] x) {
    int min = Integer.MAX_VALUE;
    // Amenable to complete unrolling.
    for (int i = 10; i <= 10; i++) {
      min = Math.min(min, x[i]);
    }
    return min;
  }

  private static int reductionMaxInt10(int[] x) {
    int max = Integer.MIN_VALUE;
    // Amenable to complete unrolling.
    for (int i = 10; i <= 10; i++) {
      max = Math.max(max, x[i]);
    }
    return max;
  }

  //
  // Main driver.
  //

  public static void main(String[] args) {
    byte[] xb = new byte[N];
    short[] xs = new short[N];
    char[] xc = new char[N];
    int[] xi = new int[N];
    long[] xl = new long[N];
    for (int i = 0, k = -17; i < N; i++, k += 3) {
      xb[i] = (byte) k;
      xs[i] = (short) k;
      xc[i] = (char) k;
      xi[i] = k;
      xl[i] = k;
    }

    // Test various reductions in loops.
    expectEquals(-74, reductionByte(xb));
    expectEquals(-27466, reductionShort(xs));
    expectEquals(38070, reductionChar(xc));
    expectEquals(365750, reductionInt(xi));
    expectEquals(365750L, reductionLong(xl));
    expectEquals(74, reductionMinusByte(xb));
    expectEquals(27466, reductionMinusShort(xs));
    expectEquals(27466, reductionMinusChar(xc));
    expectEquals(-365750, reductionMinusInt(xi));
    expectEquals(-365750L, reductionMinusLong(xl));
    expectEquals(-128, reductionMinByte(xb));
    expectEquals(-17, reductionMinShort(xs));
    expectEquals(1, reductionMinChar(xc));
    expectEquals(-17, reductionMinInt(xi));
    expectEquals(-17L, reductionMinLong(xl));
    expectEquals(127, reductionMaxByte(xb));
    expectEquals(1480, reductionMaxShort(xs));
    expectEquals(65534, reductionMaxChar(xc));
    expectEquals(1480, reductionMaxInt(xi));
    expectEquals(1480L, reductionMaxLong(xl));

    // Test special cases.
    expectEquals(13, reductionInt10(xi));
    expectEquals(-13, reductionMinusInt10(xi));
    expectEquals(13, reductionMinInt10(xi));
    expectEquals(13, reductionMaxInt10(xi));

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
}
