/*
 * Copyright (C) 2015 The Android Open Source Project
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

import java.lang.reflect.Method;

public class Main {

  static boolean doThrow = false;

  public static void assertBooleanEquals(boolean expected, boolean result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertIntEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertLongEquals(long expected, long result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertFloatEquals(float expected, float result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertDoubleEquals(double expected, double result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertStringEquals(String expected, String result) {
    if (expected == null ? result != null : !expected.equals(result)) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  /**
   * Tiny programs exercising optimizations of arithmetic identities.
   */

  /// CHECK-START: long Main.$noinline$Add0(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Const0:j\d+>>  LongConstant 0
  /// CHECK-DAG:     <<Add:j\d+>>     Add [<<Const0>>,<<Arg>>]
  /// CHECK-DAG:                      Return [<<Add>>]

  /// CHECK-START: long Main.$noinline$Add0(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:                      Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Add0(long) instruction_simplifier (after)
  /// CHECK-NOT:                        Add

  public static long $noinline$Add0(long arg) {
    if (doThrow) { throw new Error(); }
    return 0 + arg;
  }

  /// CHECK-START: int Main.$noinline$AndAllOnes(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<ConstF:i\d+>>  IntConstant -1
  /// CHECK-DAG:     <<And:i\d+>>     And [<<Arg>>,<<ConstF>>]
  /// CHECK-DAG:                      Return [<<And>>]

  /// CHECK-START: int Main.$noinline$AndAllOnes(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>     ParameterValue
  /// CHECK-DAG:                      Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$AndAllOnes(int) instruction_simplifier (after)
  /// CHECK-NOT:                      And

  public static int $noinline$AndAllOnes(int arg) {
    if (doThrow) { throw new Error(); }
    return arg & -1;
  }

  /// CHECK-START: int Main.$noinline$UShr28And15(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const28:i\d+>>  IntConstant 28
  /// CHECK-DAG:     <<Const15:i\d+>>  IntConstant 15
  /// CHECK-DAG:     <<UShr:i\d+>>     UShr [<<Arg>>,<<Const28>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<UShr>>,<<Const15>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: int Main.$noinline$UShr28And15(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const28:i\d+>>  IntConstant 28
  /// CHECK-DAG:     <<UShr:i\d+>>     UShr [<<Arg>>,<<Const28>>]
  /// CHECK-DAG:                       Return [<<UShr>>]

  /// CHECK-START: int Main.$noinline$UShr28And15(int) instruction_simplifier (after)
  /// CHECK-NOT:                       And

  public static int $noinline$UShr28And15(int arg) {
    if (doThrow) { throw new Error(); }
    return (arg >>> 28) & 15;
  }

  /// CHECK-START: long Main.$noinline$UShr60And15(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const60:i\d+>>  IntConstant 60
  /// CHECK-DAG:     <<Const15:j\d+>>  LongConstant 15
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const60>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<UShr>>,<<Const15>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: long Main.$noinline$UShr60And15(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const60:i\d+>>  IntConstant 60
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const60>>]
  /// CHECK-DAG:                       Return [<<UShr>>]

  /// CHECK-START: long Main.$noinline$UShr60And15(long) instruction_simplifier (after)
  /// CHECK-NOT:                       And

  public static long $noinline$UShr60And15(long arg) {
    if (doThrow) { throw new Error(); }
    return (arg >>> 60) & 15;
  }

  /// CHECK-START: int Main.$noinline$UShr28And7(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const28:i\d+>>  IntConstant 28
  /// CHECK-DAG:     <<Const7:i\d+>>   IntConstant 7
  /// CHECK-DAG:     <<UShr:i\d+>>     UShr [<<Arg>>,<<Const28>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<UShr>>,<<Const7>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: int Main.$noinline$UShr28And7(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const28:i\d+>>  IntConstant 28
  /// CHECK-DAG:     <<Const7:i\d+>>   IntConstant 7
  /// CHECK-DAG:     <<UShr:i\d+>>     UShr [<<Arg>>,<<Const28>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<UShr>>,<<Const7>>]
  /// CHECK-DAG:                       Return [<<And>>]

  public static int $noinline$UShr28And7(int arg) {
    if (doThrow) { throw new Error(); }
    return (arg >>> 28) & 7;
  }

  /// CHECK-START: long Main.$noinline$UShr60And7(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const60:i\d+>>  IntConstant 60
  /// CHECK-DAG:     <<Const7:j\d+>>   LongConstant 7
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const60>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<UShr>>,<<Const7>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: long Main.$noinline$UShr60And7(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const60:i\d+>>  IntConstant 60
  /// CHECK-DAG:     <<Const7:j\d+>>   LongConstant 7
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const60>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<UShr>>,<<Const7>>]
  /// CHECK-DAG:                       Return [<<And>>]

  public static long $noinline$UShr60And7(long arg) {
    if (doThrow) { throw new Error(); }
    return (arg >>> 60) & 7;
  }

  /// CHECK-START: int Main.$noinline$Shr24And255(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const24:i\d+>>  IntConstant 24
  /// CHECK-DAG:     <<Const255:i\d+>> IntConstant 255
  /// CHECK-DAG:     <<Shr:i\d+>>      Shr [<<Arg>>,<<Const24>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<Shr>>,<<Const255>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: int Main.$noinline$Shr24And255(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const24:i\d+>>  IntConstant 24
  /// CHECK-DAG:     <<UShr:i\d+>>     UShr [<<Arg>>,<<Const24>>]
  /// CHECK-DAG:                       Return [<<UShr>>]

  /// CHECK-START: int Main.$noinline$Shr24And255(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Shr
  /// CHECK-NOT:                       And

  public static int $noinline$Shr24And255(int arg) {
    if (doThrow) { throw new Error(); }
    return (arg >> 24) & 255;
  }

  /// CHECK-START: long Main.$noinline$Shr56And255(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const56:i\d+>>  IntConstant 56
  /// CHECK-DAG:     <<Const255:j\d+>> LongConstant 255
  /// CHECK-DAG:     <<Shr:j\d+>>      Shr [<<Arg>>,<<Const56>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<Shr>>,<<Const255>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: long Main.$noinline$Shr56And255(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const56:i\d+>>  IntConstant 56
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const56>>]
  /// CHECK-DAG:                       Return [<<UShr>>]

  /// CHECK-START: long Main.$noinline$Shr56And255(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Shr
  /// CHECK-NOT:                       And

  public static long $noinline$Shr56And255(long arg) {
    if (doThrow) { throw new Error(); }
    return (arg >> 56) & 255;
  }

  /// CHECK-START: int Main.$noinline$Shr24And127(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const24:i\d+>>  IntConstant 24
  /// CHECK-DAG:     <<Const127:i\d+>> IntConstant 127
  /// CHECK-DAG:     <<Shr:i\d+>>      Shr [<<Arg>>,<<Const24>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<Shr>>,<<Const127>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: int Main.$noinline$Shr24And127(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const24:i\d+>>  IntConstant 24
  /// CHECK-DAG:     <<Const127:i\d+>> IntConstant 127
  /// CHECK-DAG:     <<Shr:i\d+>>      Shr [<<Arg>>,<<Const24>>]
  /// CHECK-DAG:     <<And:i\d+>>      And [<<Shr>>,<<Const127>>]
  /// CHECK-DAG:                       Return [<<And>>]

  public static int $noinline$Shr24And127(int arg) {
    if (doThrow) { throw new Error(); }
    return (arg >> 24) & 127;
  }

  /// CHECK-START: long Main.$noinline$Shr56And127(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const56:i\d+>>  IntConstant 56
  /// CHECK-DAG:     <<Const127:j\d+>> LongConstant 127
  /// CHECK-DAG:     <<Shr:j\d+>>      Shr [<<Arg>>,<<Const56>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<Shr>>,<<Const127>>]
  /// CHECK-DAG:                       Return [<<And>>]

  /// CHECK-START: long Main.$noinline$Shr56And127(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const56:i\d+>>  IntConstant 56
  /// CHECK-DAG:     <<Const127:j\d+>> LongConstant 127
  /// CHECK-DAG:     <<Shr:j\d+>>      Shr [<<Arg>>,<<Const56>>]
  /// CHECK-DAG:     <<And:j\d+>>      And [<<Shr>>,<<Const127>>]
  /// CHECK-DAG:                       Return [<<And>>]

  public static long $noinline$Shr56And127(long arg) {
    if (doThrow) { throw new Error(); }
    return (arg >> 56) & 127;
  }

  /// CHECK-START: long Main.$noinline$Div1(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Const1:j\d+>>  LongConstant 1
  /// CHECK-DAG:     <<Div:j\d+>>     Div [<<Arg>>,<<Const1>>]
  /// CHECK-DAG:                      Return [<<Div>>]

  /// CHECK-START: long Main.$noinline$Div1(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:                      Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Div1(long) instruction_simplifier (after)
  /// CHECK-NOT:                      Div

  public static long $noinline$Div1(long arg) {
    if (doThrow) { throw new Error(); }
    return arg / 1;
  }

  /// CHECK-START: int Main.$noinline$DivN1(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<ConstN1:i\d+>>  IntConstant -1
  /// CHECK-DAG:     <<Div:i\d+>>      Div [<<Arg>>,<<ConstN1>>]
  /// CHECK-DAG:                       Return [<<Div>>]

  /// CHECK-START: int Main.$noinline$DivN1(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  /// CHECK-START: int Main.$noinline$DivN1(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Div

  public static int $noinline$DivN1(int arg) {
    if (doThrow) { throw new Error(); }
    return arg / -1;
  }

  /// CHECK-START: long Main.$noinline$Mul1(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Const1:j\d+>>  LongConstant 1
  /// CHECK-DAG:     <<Mul:j\d+>>     Mul [<<Const1>>,<<Arg>>]
  /// CHECK-DAG:                      Return [<<Mul>>]

  /// CHECK-START: long Main.$noinline$Mul1(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>     ParameterValue
  /// CHECK-DAG:                      Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Mul1(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Mul

  public static long $noinline$Mul1(long arg) {
    if (doThrow) { throw new Error(); }
    return arg * 1;
  }

  /// CHECK-START: int Main.$noinline$MulN1(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<ConstN1:i\d+>>  IntConstant -1
  /// CHECK-DAG:     <<Mul:i\d+>>      Mul [<<Arg>>,<<ConstN1>>]
  /// CHECK-DAG:                       Return [<<Mul>>]

  /// CHECK-START: int Main.$noinline$MulN1(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  /// CHECK-START: int Main.$noinline$MulN1(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Mul

  public static int $noinline$MulN1(int arg) {
    if (doThrow) { throw new Error(); }
    return arg * -1;
  }

  /// CHECK-START: long Main.$noinline$MulPowerOfTwo128(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>       ParameterValue
  /// CHECK-DAG:     <<Const128:j\d+>>  LongConstant 128
  /// CHECK-DAG:     <<Mul:j\d+>>       Mul [<<Const128>>,<<Arg>>]
  /// CHECK-DAG:                        Return [<<Mul>>]

  /// CHECK-START: long Main.$noinline$MulPowerOfTwo128(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>       ParameterValue
  /// CHECK-DAG:     <<Const7:i\d+>>    IntConstant 7
  /// CHECK-DAG:     <<Shl:j\d+>>       Shl [<<Arg>>,<<Const7>>]
  /// CHECK-DAG:                        Return [<<Shl>>]

  /// CHECK-START: long Main.$noinline$MulPowerOfTwo128(long) instruction_simplifier (after)
  /// CHECK-NOT:                        Mul

  public static long $noinline$MulPowerOfTwo128(long arg) {
    if (doThrow) { throw new Error(); }
    return arg * 128;
  }

  /// CHECK-START: int Main.$noinline$Or0(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$Or0(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$Or0(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Or

  public static int $noinline$Or0(int arg) {
    if (doThrow) { throw new Error(); }
    return arg | 0;
  }

  /// CHECK-START: long Main.$noinline$OrSame(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>       ParameterValue
  /// CHECK-DAG:     <<Or:j\d+>>        Or [<<Arg>>,<<Arg>>]
  /// CHECK-DAG:                        Return [<<Or>>]

  /// CHECK-START: long Main.$noinline$OrSame(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>       ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$OrSame(long) instruction_simplifier (after)
  /// CHECK-NOT:                        Or

  public static long $noinline$OrSame(long arg) {
    if (doThrow) { throw new Error(); }
    return arg | arg;
  }

  /// CHECK-START: int Main.$noinline$Shl0(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Shl:i\d+>>      Shl [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<Shl>>]

  /// CHECK-START: int Main.$noinline$Shl0(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$Shl0(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Shl

  public static int $noinline$Shl0(int arg) {
    if (doThrow) { throw new Error(); }
    return arg << 0;
  }

  /// CHECK-START: long Main.$noinline$Shr0(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Shr:j\d+>>      Shr [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<Shr>>]

  /// CHECK-START: long Main.$noinline$Shr0(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Shr0(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Shr

  public static long $noinline$Shr0(long arg) {
    if (doThrow) { throw new Error(); }
    return arg >> 0;
  }

  /// CHECK-START: long Main.$noinline$Shr64(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const64:i\d+>>  IntConstant 64
  /// CHECK-DAG:     <<Shr:j\d+>>      Shr [<<Arg>>,<<Const64>>]
  /// CHECK-DAG:                       Return [<<Shr>>]

  /// CHECK-START: long Main.$noinline$Shr64(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Shr64(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Shr

  public static long $noinline$Shr64(long arg) {
    if (doThrow) { throw new Error(); }
    return arg >> 64;
  }

  /// CHECK-START: long Main.$noinline$Sub0(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:j\d+>>   LongConstant 0
  /// CHECK-DAG:     <<Sub:j\d+>>      Sub [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: long Main.$noinline$Sub0(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$Sub0(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Sub

  public static long $noinline$Sub0(long arg) {
    if (doThrow) { throw new Error(); }
    return arg - 0;
  }

  /// CHECK-START: int Main.$noinline$SubAliasNeg(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Const0>>,<<Arg>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: int Main.$noinline$SubAliasNeg(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  /// CHECK-START: int Main.$noinline$SubAliasNeg(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Sub

  public static int $noinline$SubAliasNeg(int arg) {
    if (doThrow) { throw new Error(); }
    return 0 - arg;
  }

  /// CHECK-START: long Main.$noinline$UShr0(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<UShr:j\d+>>     UShr [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<UShr>>]

  /// CHECK-START: long Main.$noinline$UShr0(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$UShr0(long) instruction_simplifier (after)
  /// CHECK-NOT:                       UShr

  public static long $noinline$UShr0(long arg) {
    if (doThrow) { throw new Error(); }
    return arg >>> 0;
  }

  /// CHECK-START: int Main.$noinline$Xor0(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Xor:i\d+>>      Xor [<<Arg>>,<<Const0>>]
  /// CHECK-DAG:                       Return [<<Xor>>]

  /// CHECK-START: int Main.$noinline$Xor0(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$Xor0(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Xor

  public static int $noinline$Xor0(int arg) {
    if (doThrow) { throw new Error(); }
    return arg ^ 0;
  }

  /// CHECK-START: int Main.$noinline$XorAllOnes(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<ConstF:i\d+>>   IntConstant -1
  /// CHECK-DAG:     <<Xor:i\d+>>      Xor [<<Arg>>,<<ConstF>>]
  /// CHECK-DAG:                       Return [<<Xor>>]

  /// CHECK-START: int Main.$noinline$XorAllOnes(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Not:i\d+>>      Not [<<Arg>>]
  /// CHECK-DAG:                       Return [<<Not>>]

  /// CHECK-START: int Main.$noinline$XorAllOnes(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Xor

  public static int $noinline$XorAllOnes(int arg) {
    if (doThrow) { throw new Error(); }
    return arg ^ -1;
  }

  /**
   * Test that addition or subtraction operation with both inputs negated are
   * optimized to use a single negation after the operation.
   * The transformation tested is implemented in
   * `InstructionSimplifierVisitor::TryMoveNegOnInputsAfterBinop`.
   */

  /// CHECK-START: int Main.$noinline$AddNegs1(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:                       Return [<<Add>>]

  /// CHECK-START: int Main.$noinline$AddNegs1(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-NOT:                       Neg
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Arg1>>,<<Arg2>>]
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Add>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  public static int $noinline$AddNegs1(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    return -arg1 + -arg2;
  }

  /**
   * This is similar to the test-case AddNegs1, but the negations have
   * multiple uses.
   * The transformation tested is implemented in
   * `InstructionSimplifierVisitor::TryMoveNegOnInputsAfterBinop`.
   * The current code won't perform the previous optimization. The
   * transformations do not look at other uses of their inputs. As they don't
   * know what will happen with other uses, they do not take the risk of
   * increasing the register pressure by creating or extending live ranges.
   */

  /// CHECK-START: int Main.$noinline$AddNegs2(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add1:i\d+>>     Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:     <<Add2:i\d+>>     Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Add1>>,<<Add2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$AddNegs2(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add1:i\d+>>     Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:     <<Add2:i\d+>>     Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-NOT:                       Neg
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Add1>>,<<Add2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$AddNegs2(int, int) GVN (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Add>>,<<Add>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  public static int $noinline$AddNegs2(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    int temp1 = -arg1;
    int temp2 = -arg2;
    return (temp1 + temp2) | (temp1 + temp2);
  }

  /**
   * This follows test-cases AddNegs1 and AddNegs2.
   * The transformation tested is implemented in
   * `InstructionSimplifierVisitor::TryMoveNegOnInputsAfterBinop`.
   * The optimization should not happen if it moves an additional instruction in
   * the loop.
   */

  /// CHECK-START: long Main.$noinline$AddNegs3(long, long) instruction_simplifier (before)
  //  -------------- Arguments and initial negation operations.
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:j\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:j\d+>>     Neg [<<Arg2>>]
  /// CHECK:                           Goto
  //  -------------- Loop
  /// CHECK:                           SuspendCheck
  /// CHECK:         <<Add:j\d+>>      Add [<<Neg1>>,<<Neg2>>]
  /// CHECK:                           Goto

  /// CHECK-START: long Main.$noinline$AddNegs3(long, long) instruction_simplifier (after)
  //  -------------- Arguments and initial negation operations.
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg1:j\d+>>     Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Neg2:j\d+>>     Neg [<<Arg2>>]
  /// CHECK:                           Goto
  //  -------------- Loop
  /// CHECK:                           SuspendCheck
  /// CHECK:         <<Add:j\d+>>      Add [<<Neg1>>,<<Neg2>>]
  /// CHECK-NOT:                       Neg
  /// CHECK:                           Goto

  public static long $noinline$AddNegs3(long arg1, long arg2) {
    if (doThrow) { throw new Error(); }
    long res = 0;
    long n_arg1 = -arg1;
    long n_arg2 = -arg2;
    for (long i = 0; i < 1; i++) {
      res += n_arg1 + n_arg2 + i;
    }
    return res;
  }

  /**
   * Test the simplification of an addition with a negated argument into a
   * subtraction.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitAdd`.
   */

  /// CHECK-START: long Main.$noinline$AddNeg1(long, long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Add:j\d+>>      Add [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:                       Return [<<Add>>]

  /// CHECK-START: long Main.$noinline$AddNeg1(long, long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Sub:j\d+>>      Sub [<<Arg2>>,<<Arg1>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: long Main.$noinline$AddNeg1(long, long) instruction_simplifier (after)
  /// CHECK-NOT:                       Neg
  /// CHECK-NOT:                       Add

  public static long $noinline$AddNeg1(long arg1, long arg2) {
    if (doThrow) { throw new Error(); }
    return -arg1 + arg2;
  }

  /**
   * This is similar to the test-case AddNeg1, but the negation has two uses.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitAdd`.
   * The current code won't perform the previous optimization. The
   * transformations do not look at other uses of their inputs. As they don't
   * know what will happen with other uses, they do not take the risk of
   * increasing the register pressure by creating or extending live ranges.
   */

  /// CHECK-START: long Main.$noinline$AddNeg2(long, long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add1:j\d+>>     Add [<<Arg1>>,<<Neg>>]
  /// CHECK-DAG:     <<Add2:j\d+>>     Add [<<Arg1>>,<<Neg>>]
  /// CHECK-DAG:     <<Res:j\d+>>      Or [<<Add1>>,<<Add2>>]
  /// CHECK-DAG:                       Return [<<Res>>]

  /// CHECK-START: long Main.$noinline$AddNeg2(long, long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg2>>]
  /// CHECK-DAG:     <<Add1:j\d+>>     Add [<<Arg1>>,<<Neg>>]
  /// CHECK-DAG:     <<Add2:j\d+>>     Add [<<Arg1>>,<<Neg>>]
  /// CHECK-DAG:     <<Res:j\d+>>      Or [<<Add1>>,<<Add2>>]
  /// CHECK-DAG:                       Return [<<Res>>]

  /// CHECK-START: long Main.$noinline$AddNeg2(long, long) instruction_simplifier (after)
  /// CHECK-NOT:                       Sub

  public static long $noinline$AddNeg2(long arg1, long arg2) {
    if (doThrow) { throw new Error(); }
    long temp = -arg2;
    return (arg1 + temp) | (arg1 + temp);
  }

  /**
   * Test simplification of the `-(-var)` pattern.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitNeg`.
   */

  /// CHECK-START: long Main.$noinline$NegNeg1(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Neg1:j\d+>>     Neg [<<Arg>>]
  /// CHECK-DAG:     <<Neg2:j\d+>>     Neg [<<Neg1>>]
  /// CHECK-DAG:                       Return [<<Neg2>>]

  /// CHECK-START: long Main.$noinline$NegNeg1(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$NegNeg1(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Neg

  public static long $noinline$NegNeg1(long arg) {
    if (doThrow) { throw new Error(); }
    return -(-arg);
  }

  /**
   * Test 'multi-step' simplification, where a first transformation yields a
   * new simplification possibility for the current instruction.
   * The transformations tested are implemented in `InstructionSimplifierVisitor::VisitNeg`
   * and in `InstructionSimplifierVisitor::VisitAdd`.
   */

  /// CHECK-START: int Main.$noinline$NegNeg2(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Arg>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Neg1>>]
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Neg2>>,<<Neg1>>]
  /// CHECK-DAG:                       Return [<<Add>>]

  /// CHECK-START: int Main.$noinline$NegNeg2(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Arg>>,<<Arg>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: int Main.$noinline$NegNeg2(int) instruction_simplifier (after)
  /// CHECK-NOT:                       Neg
  /// CHECK-NOT:                       Add

  /// CHECK-START: int Main.$noinline$NegNeg2(int) constant_folding_after_inlining (after)
  /// CHECK:         <<Const0:i\d+>>   IntConstant 0
  /// CHECK-NOT:                       Neg
  /// CHECK-NOT:                       Add
  /// CHECK:                           Return [<<Const0>>]

  public static int $noinline$NegNeg2(int arg) {
    if (doThrow) { throw new Error(); }
    int temp = -arg;
    return temp + -temp;
  }

  /**
   * Test another 'multi-step' simplification, where a first transformation
   * yields a new simplification possibility for the current instruction.
   * The transformations tested are implemented in `InstructionSimplifierVisitor::VisitNeg`
   * and in `InstructionSimplifierVisitor::VisitSub`.
   */

  /// CHECK-START: long Main.$noinline$NegNeg3(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:j\d+>>   LongConstant 0
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg>>]
  /// CHECK-DAG:     <<Sub:j\d+>>      Sub [<<Const0>>,<<Neg>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: long Main.$noinline$NegNeg3(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$NegNeg3(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Neg
  /// CHECK-NOT:                       Sub

  public static long $noinline$NegNeg3(long arg) {
    if (doThrow) { throw new Error(); }
    return 0 - -arg;
  }

  /**
   * Test that a negated subtraction is simplified to a subtraction with its
   * arguments reversed.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitNeg`.
   */

  /// CHECK-START: int Main.$noinline$NegSub1(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Arg1>>,<<Arg2>>]
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Sub>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  /// CHECK-START: int Main.$noinline$NegSub1(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Arg2>>,<<Arg1>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: int Main.$noinline$NegSub1(int, int) instruction_simplifier (after)
  /// CHECK-NOT:                       Neg

  public static int $noinline$NegSub1(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    return -(arg1 - arg2);
  }

  /**
   * This is similar to the test-case NegSub1, but the subtraction has
   * multiple uses.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitNeg`.
   * The current code won't perform the previous optimization. The
   * transformations do not look at other uses of their inputs. As they don't
   * know what will happen with other uses, they do not take the risk of
   * increasing the register pressure by creating or extending live ranges.
   */

  /// CHECK-START: int Main.$noinline$NegSub2(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Arg1>>,<<Arg2>>]
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Sub>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Sub>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$NegSub2(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Arg1>>,<<Arg2>>]
  /// CHECK-DAG:     <<Neg1:i\d+>>     Neg [<<Sub>>]
  /// CHECK-DAG:     <<Neg2:i\d+>>     Neg [<<Sub>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Neg1>>,<<Neg2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  public static int $noinline$NegSub2(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    int temp = arg1 - arg2;
    return -temp | -temp;
  }

  /**
   * Test simplification of the `~~var` pattern.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitNot`.
   */

  /// CHECK-START: long Main.$noinline$NotNot1(long) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:     <<Not1:j\d+>>     Not [<<Arg>>]
  /// CHECK-DAG:     <<Not2:j\d+>>     Not [<<Not1>>]
  /// CHECK-DAG:                       Return [<<Not2>>]

  /// CHECK-START: long Main.$noinline$NotNot1(long) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:                       Return [<<Arg>>]

  /// CHECK-START: long Main.$noinline$NotNot1(long) instruction_simplifier (after)
  /// CHECK-NOT:                       Not

  public static long $noinline$NotNot1(long arg) {
    if (doThrow) { throw new Error(); }
    return ~~arg;
  }

  /// CHECK-START: int Main.$noinline$NotNot2(int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Not1:i\d+>>     Not [<<Arg>>]
  /// CHECK-DAG:     <<Not2:i\d+>>     Not [<<Not1>>]
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Not2>>,<<Not1>>]
  /// CHECK-DAG:                       Return [<<Add>>]

  /// CHECK-START: int Main.$noinline$NotNot2(int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:     <<Not:i\d+>>      Not [<<Arg>>]
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Arg>>,<<Not>>]
  /// CHECK-DAG:                       Return [<<Add>>]

  /// CHECK-START: int Main.$noinline$NotNot2(int) instruction_simplifier (after)
  /// CHECK:                           Not
  /// CHECK-NOT:                       Not

  public static int $noinline$NotNot2(int arg) {
    if (doThrow) { throw new Error(); }
    int temp = ~arg;
    return temp + ~temp;
  }

  /**
   * Test the simplification of a subtraction with a negated argument.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitSub`.
   */

  /// CHECK-START: int Main.$noinline$SubNeg1(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Sub:i\d+>>      Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:                       Return [<<Sub>>]

  /// CHECK-START: int Main.$noinline$SubNeg1(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Add:i\d+>>      Add [<<Arg1>>,<<Arg2>>]
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Add>>]
  /// CHECK-DAG:                       Return [<<Neg>>]

  /// CHECK-START: int Main.$noinline$SubNeg1(int, int) instruction_simplifier (after)
  /// CHECK-NOT:                       Sub

  public static int $noinline$SubNeg1(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    return -arg1 - arg2;
  }

  /**
   * This is similar to the test-case SubNeg1, but the negation has
   * multiple uses.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitSub`.
   * The current code won't perform the previous optimization. The
   * transformations do not look at other uses of their inputs. As they don't
   * know what will happen with other uses, they do not take the risk of
   * increasing the register pressure by creating or extending live ranges.
   */

  /// CHECK-START: int Main.$noinline$SubNeg2(int, int) instruction_simplifier (before)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Sub1:i\d+>>     Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:     <<Sub2:i\d+>>     Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Sub1>>,<<Sub2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$SubNeg2(int, int) instruction_simplifier (after)
  /// CHECK-DAG:     <<Arg1:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:i\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:i\d+>>      Neg [<<Arg1>>]
  /// CHECK-DAG:     <<Sub1:i\d+>>     Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:     <<Sub2:i\d+>>     Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-DAG:     <<Or:i\d+>>       Or [<<Sub1>>,<<Sub2>>]
  /// CHECK-DAG:                       Return [<<Or>>]

  /// CHECK-START: int Main.$noinline$SubNeg2(int, int) instruction_simplifier (after)
  /// CHECK-NOT:                       Add

  public static int $noinline$SubNeg2(int arg1, int arg2) {
    if (doThrow) { throw new Error(); }
    int temp = -arg1;
    return (temp - arg2) | (temp - arg2);
  }

  /**
   * This follows test-cases SubNeg1 and SubNeg2.
   * The transformation tested is implemented in `InstructionSimplifierVisitor::VisitSub`.
   * The optimization should not happen if it moves an additional instruction in
   * the loop.
   */

  /// CHECK-START: long Main.$noinline$SubNeg3(long, long) instruction_simplifier (before)
  //  -------------- Arguments and initial negation operation.
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg1>>]
  /// CHECK:                           Goto
  //  -------------- Loop
  /// CHECK:                           SuspendCheck
  /// CHECK:         <<Sub:j\d+>>      Sub [<<Neg>>,<<Arg2>>]
  /// CHECK:                           Goto

  /// CHECK-START: long Main.$noinline$SubNeg3(long, long) instruction_simplifier (after)
  //  -------------- Arguments and initial negation operation.
  /// CHECK-DAG:     <<Arg1:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Arg2:j\d+>>     ParameterValue
  /// CHECK-DAG:     <<Neg:j\d+>>      Neg [<<Arg1>>]
  /// CHECK-DAG:                       Goto
  //  -------------- Loop
  /// CHECK:                           SuspendCheck
  /// CHECK:         <<Sub:j\d+>>      Sub [<<Neg>>,<<Arg2>>]
  /// CHECK-NOT:                       Neg
  /// CHECK:                           Goto

  public static long $noinline$SubNeg3(long arg1, long arg2) {
    if (doThrow) { throw new Error(); }
    long res = 0;
    long temp = -arg1;
    for (long i = 0; i < 1; i++) {
      res += temp - arg2 - i;
    }
    return res;
  }

  /// CHECK-START: boolean Main.$noinline$EqualBoolVsIntConst(boolean) instruction_simplifier_after_bce (before)
  /// CHECK-DAG:     <<Arg:z\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Const1:i\d+>>   IntConstant 1
  /// CHECK-DAG:     <<Const2:i\d+>>   IntConstant 2
  /// CHECK-DAG:     <<NotArg:i\d+>>   Select [<<Const1>>,<<Const0>>,<<Arg>>]
  /// CHECK-DAG:     <<Cond:z\d+>>     Equal [<<NotArg>>,<<Const2>>]
  /// CHECK-DAG:     <<NotCond:i\d+>>  Select [<<Const1>>,<<Const0>>,<<Cond>>]
  /// CHECK-DAG:                       Return [<<NotCond>>]

  /// CHECK-START: boolean Main.$noinline$EqualBoolVsIntConst(boolean) instruction_simplifier_after_bce (after)
  /// CHECK-DAG:     <<True:i\d+>>     IntConstant 1
  /// CHECK-DAG:                       Return [<<True>>]

  public static boolean $noinline$EqualBoolVsIntConst(boolean arg) {
    if (doThrow) { throw new Error(); }
    return (arg ? 0 : 1) != 2;
  }

  /// CHECK-START: boolean Main.$noinline$NotEqualBoolVsIntConst(boolean) instruction_simplifier_after_bce (before)
  /// CHECK-DAG:     <<Arg:z\d+>>      ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:     <<Const1:i\d+>>   IntConstant 1
  /// CHECK-DAG:     <<Const2:i\d+>>   IntConstant 2
  /// CHECK-DAG:     <<NotArg:i\d+>>   Select [<<Const1>>,<<Const0>>,<<Arg>>]
  /// CHECK-DAG:     <<Cond:z\d+>>     NotEqual [<<NotArg>>,<<Const2>>]
  /// CHECK-DAG:     <<NotCond:i\d+>>  Select [<<Const1>>,<<Const0>>,<<Cond>>]
  /// CHECK-DAG:                       Return [<<NotCond>>]

  /// CHECK-START: boolean Main.$noinline$NotEqualBoolVsIntConst(boolean) instruction_simplifier_after_bce (after)
  /// CHECK-DAG:     <<False:i\d+>>    IntConstant 0
  /// CHECK-DAG:                       Return [<<False>>]

  public static boolean $noinline$NotEqualBoolVsIntConst(boolean arg) {
    if (doThrow) { throw new Error(); }
    return (arg ? 0 : 1) == 2;
  }

  /*
   * Test simplification of double Boolean negation. Note that sometimes
   * both negations can be removed but we only expect the simplifier to
   * remove the second.
   */

  /// CHECK-START: boolean Main.$noinline$NotNotBool(boolean) instruction_simplifier_after_bce (before)
  /// CHECK-DAG:     <<Arg:z\d+>>       ParameterValue
  /// CHECK-DAG:     <<Const0:i\d+>>    IntConstant 0
  /// CHECK-DAG:     <<Const1:i\d+>>    IntConstant 1
  /// CHECK-DAG:     <<NotArg:i\d+>>    Select [<<Const1>>,<<Const0>>,<<Arg>>]
  /// CHECK-DAG:     <<NotNotArg:i\d+>> Select [<<Const1>>,<<Const0>>,<<NotArg>>]
  /// CHECK-DAG:                        Return [<<NotNotArg>>]

  /// CHECK-START: boolean Main.$noinline$NotNotBool(boolean) instruction_simplifier_after_bce (after)
  /// CHECK-DAG:     <<Arg:z\d+>>       ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  public static boolean NegateValue(boolean arg) {
    return !arg;
  }

  public static boolean $noinline$NotNotBool(boolean arg) {
    if (doThrow) { throw new Error(); }
    return !(NegateValue(arg));
  }

  /// CHECK-START: float Main.$noinline$Div2(float) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const2:f\d+>>   FloatConstant 2
  /// CHECK-DAG:      <<Div:f\d+>>      Div [<<Arg>>,<<Const2>>]
  /// CHECK-DAG:                        Return [<<Div>>]

  /// CHECK-START: float Main.$noinline$Div2(float) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstP5:f\d+>>  FloatConstant 0.5
  /// CHECK-DAG:      <<Mul:f\d+>>      Mul [<<Arg>>,<<ConstP5>>]
  /// CHECK-DAG:                        Return [<<Mul>>]

  /// CHECK-START: float Main.$noinline$Div2(float) instruction_simplifier (after)
  /// CHECK-NOT:                        Div

  public static float $noinline$Div2(float arg) {
    if (doThrow) { throw new Error(); }
    return arg / 2.0f;
  }

  /// CHECK-START: double Main.$noinline$Div2(double) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:d\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const2:d\d+>>   DoubleConstant 2
  /// CHECK-DAG:      <<Div:d\d+>>      Div [<<Arg>>,<<Const2>>]
  /// CHECK-DAG:                        Return [<<Div>>]

  /// CHECK-START: double Main.$noinline$Div2(double) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:d\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstP5:d\d+>>  DoubleConstant 0.5
  /// CHECK-DAG:      <<Mul:d\d+>>      Mul [<<Arg>>,<<ConstP5>>]
  /// CHECK-DAG:                        Return [<<Mul>>]

  /// CHECK-START: double Main.$noinline$Div2(double) instruction_simplifier (after)
  /// CHECK-NOT:                        Div
  public static double $noinline$Div2(double arg) {
    if (doThrow) { throw new Error(); }
    return arg / 2.0;
  }

  /// CHECK-START: float Main.$noinline$DivMP25(float) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstMP25:f\d+>>   FloatConstant -0.25
  /// CHECK-DAG:      <<Div:f\d+>>      Div [<<Arg>>,<<ConstMP25>>]
  /// CHECK-DAG:                        Return [<<Div>>]

  /// CHECK-START: float Main.$noinline$DivMP25(float) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstM4:f\d+>>  FloatConstant -4
  /// CHECK-DAG:      <<Mul:f\d+>>      Mul [<<Arg>>,<<ConstM4>>]
  /// CHECK-DAG:                        Return [<<Mul>>]

  /// CHECK-START: float Main.$noinline$DivMP25(float) instruction_simplifier (after)
  /// CHECK-NOT:                        Div

  public static float $noinline$DivMP25(float arg) {
    if (doThrow) { throw new Error(); }
    return arg / -0.25f;
  }

  /// CHECK-START: double Main.$noinline$DivMP25(double) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:d\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstMP25:d\d+>>   DoubleConstant -0.25
  /// CHECK-DAG:      <<Div:d\d+>>      Div [<<Arg>>,<<ConstMP25>>]
  /// CHECK-DAG:                        Return [<<Div>>]

  /// CHECK-START: double Main.$noinline$DivMP25(double) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:d\d+>>      ParameterValue
  /// CHECK-DAG:      <<ConstM4:d\d+>>  DoubleConstant -4
  /// CHECK-DAG:      <<Mul:d\d+>>      Mul [<<Arg>>,<<ConstM4>>]
  /// CHECK-DAG:                        Return [<<Mul>>]

  /// CHECK-START: double Main.$noinline$DivMP25(double) instruction_simplifier (after)
  /// CHECK-NOT:                        Div
  public static double $noinline$DivMP25(double arg) {
    if (doThrow) { throw new Error(); }
    return arg / -0.25f;
  }

  /**
   * Test strength reduction of factors of the form (2^n + 1).
   */

  /// CHECK-START: int Main.$noinline$mulPow2Plus1(int) instruction_simplifier (before)
  /// CHECK-DAG:   <<Arg:i\d+>>         ParameterValue
  /// CHECK-DAG:   <<Const9:i\d+>>      IntConstant 9
  /// CHECK:                            Mul [<<Arg>>,<<Const9>>]

  /// CHECK-START: int Main.$noinline$mulPow2Plus1(int) instruction_simplifier (after)
  /// CHECK-DAG:   <<Arg:i\d+>>         ParameterValue
  /// CHECK-DAG:   <<Const3:i\d+>>      IntConstant 3
  /// CHECK:       <<Shift:i\d+>>       Shl [<<Arg>>,<<Const3>>]
  /// CHECK-NEXT:                       Add [<<Arg>>,<<Shift>>]

  public static int $noinline$mulPow2Plus1(int arg) {
    if (doThrow) { throw new Error(); }
    return arg * 9;
  }

  /**
   * Test strength reduction of factors of the form (2^n - 1).
   */

  /// CHECK-START: long Main.$noinline$mulPow2Minus1(long) instruction_simplifier (before)
  /// CHECK-DAG:   <<Arg:j\d+>>         ParameterValue
  /// CHECK-DAG:   <<Const31:j\d+>>     LongConstant 31
  /// CHECK:                            Mul [<<Const31>>,<<Arg>>]

  /// CHECK-START: long Main.$noinline$mulPow2Minus1(long) instruction_simplifier (after)
  /// CHECK-DAG:   <<Arg:j\d+>>         ParameterValue
  /// CHECK-DAG:   <<Const5:i\d+>>      IntConstant 5
  /// CHECK:       <<Shift:j\d+>>       Shl [<<Arg>>,<<Const5>>]
  /// CHECK-NEXT:                       Sub [<<Shift>>,<<Arg>>]

  public static long $noinline$mulPow2Minus1(long arg) {
    if (doThrow) { throw new Error(); }
    return arg * 31;
  }

  /// CHECK-START: int Main.$noinline$booleanFieldNotEqualOne() instruction_simplifier_after_bce (before)
  /// CHECK-DAG:      <<Const1:i\d+>>   IntConstant 1
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<doThrow:z\d+>>  StaticFieldGet
  /// CHECK-DAG:      <<Field:z\d+>>    StaticFieldGet
  /// CHECK-DAG:      <<NE:z\d+>>       NotEqual [<<Field>>,<<Const1>>]
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const13>>,<<Const54>>,<<NE>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  /// CHECK-START: int Main.$noinline$booleanFieldNotEqualOne() instruction_simplifier_after_bce (after)
  /// CHECK-DAG:      <<doThrow:z\d+>>  StaticFieldGet
  /// CHECK-DAG:      <<Field:z\d+>>    StaticFieldGet
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const54>>,<<Const13>>,<<Field>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  public static int $noinline$booleanFieldNotEqualOne() {
    if (doThrow) { throw new Error(); }
    return (booleanField == $inline$true()) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$booleanFieldEqualZero() instruction_simplifier_after_bce (before)
  /// CHECK-DAG:      <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<doThrow:z\d+>>  StaticFieldGet
  /// CHECK-DAG:      <<Field:z\d+>>    StaticFieldGet
  /// CHECK-DAG:      <<NE:z\d+>>       Equal [<<Field>>,<<Const0>>]
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const13>>,<<Const54>>,<<NE>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  /// CHECK-START: int Main.$noinline$booleanFieldEqualZero() instruction_simplifier_after_bce (after)
  /// CHECK-DAG:      <<doThrow:z\d+>>  StaticFieldGet
  /// CHECK-DAG:      <<Field:z\d+>>    StaticFieldGet
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const54>>,<<Const13>>,<<Field>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  public static int $noinline$booleanFieldEqualZero() {
    if (doThrow) { throw new Error(); }
    return (booleanField != $inline$false()) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$intConditionNotEqualOne(int) instruction_simplifier_after_bce (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:      <<Const1:i\d+>>   IntConstant 1
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<LE:z\d+>>       LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:      <<GT:i\d+>>       Select [<<Const1>>,<<Const0>>,<<LE>>]
  /// CHECK-DAG:      <<NE:z\d+>>       NotEqual [<<GT>>,<<Const1>>]
  /// CHECK-DAG:      <<Result:i\d+>>   Select [<<Const13>>,<<Const54>>,<<NE>>]
  /// CHECK-DAG:                        Return [<<Result>>]

  /// CHECK-START: int Main.$noinline$intConditionNotEqualOne(int) instruction_simplifier_after_bce (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Result:i\d+>>   Select [<<Const13>>,<<Const54>>,<<LE:z\d+>>]
  /// CHECK-DAG:      <<LE>>            LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:                        Return [<<Result>>]
  // Note that we match `LE` from Select because there are two identical
  // LessThanOrEqual instructions.

  public static int $noinline$intConditionNotEqualOne(int i) {
    if (doThrow) { throw new Error(); }
    return ((i > 42) == $inline$true()) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$intConditionEqualZero(int) instruction_simplifier_after_bce (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:      <<Const1:i\d+>>   IntConstant 1
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<LE:z\d+>>       LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:      <<GT:i\d+>>       Select [<<Const1>>,<<Const0>>,<<LE>>]
  /// CHECK-DAG:      <<NE:z\d+>>       Equal [<<GT>>,<<Const0>>]
  /// CHECK-DAG:      <<Result:i\d+>>   Select [<<Const13>>,<<Const54>>,<<NE>>]
  /// CHECK-DAG:                        Return [<<Result>>]

  /// CHECK-START: int Main.$noinline$intConditionEqualZero(int) instruction_simplifier_after_bce (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Result:i\d+>>   Select [<<Const13>>,<<Const54>>,<<LE:z\d+>>]
  /// CHECK-DAG:      <<LE>>            LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:                        Return [<<Result>>]
  // Note that we match `LE` from Select because there are two identical
  // LessThanOrEqual instructions.

  public static int $noinline$intConditionEqualZero(int i) {
    if (doThrow) { throw new Error(); }
    return ((i > 42) != $inline$false()) ? 13 : 54;
  }

  // Test that conditions on float/double are not flipped.

  /// CHECK-START: int Main.$noinline$floatConditionNotEqualOne(float) builder (after)
  /// CHECK:                            LessThanOrEqual

  /// CHECK-START: int Main.$noinline$floatConditionNotEqualOne(float) instruction_simplifier_before_codegen (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Const42:f\d+>>  FloatConstant 42
  /// CHECK-DAG:      <<LE:z\d+>>       LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const13>>,<<Const54>>,<<LE>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  public static int $noinline$floatConditionNotEqualOne(float f) {
    if (doThrow) { throw new Error(); }
    return ((f > 42.0f) == true) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$doubleConditionEqualZero(double) builder (after)
  /// CHECK:                            LessThanOrEqual

  /// CHECK-START: int Main.$noinline$doubleConditionEqualZero(double) instruction_simplifier_before_codegen (after)
  /// CHECK-DAG:      <<Arg:d\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const13:i\d+>>  IntConstant 13
  /// CHECK-DAG:      <<Const54:i\d+>>  IntConstant 54
  /// CHECK-DAG:      <<Const42:d\d+>>  DoubleConstant 42
  /// CHECK-DAG:      <<LE:z\d+>>       LessThanOrEqual [<<Arg>>,<<Const42>>]
  /// CHECK-DAG:      <<Select:i\d+>>   Select [<<Const13>>,<<Const54>>,<<LE>>]
  /// CHECK-DAG:                        Return [<<Select>>]

  public static int $noinline$doubleConditionEqualZero(double d) {
    if (doThrow) { throw new Error(); }
    return ((d > 42.0) != false) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$intToDoubleToInt(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Double>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$intToDoubleToInt(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$intToDoubleToInt(int) instruction_simplifier (after)
  /// CHECK-NOT:                        TypeConversion

  public static int $noinline$intToDoubleToInt(int value) {
    if (doThrow) { throw new Error(); }
    // Lossless conversion followed by a conversion back.
    return (int) (double) value;
  }

  /// CHECK-START: java.lang.String Main.$noinline$intToDoubleToIntPrint(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      {{i\d+}}          TypeConversion [<<Double>>]

  /// CHECK-START: java.lang.String Main.$noinline$intToDoubleToIntPrint(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      {{d\d+}}          TypeConversion [<<Arg>>]

  /// CHECK-START: java.lang.String Main.$noinline$intToDoubleToIntPrint(int) instruction_simplifier (after)
  /// CHECK-DAG:                        TypeConversion
  /// CHECK-NOT:                        TypeConversion

  public static String $noinline$intToDoubleToIntPrint(int value) {
    if (doThrow) { throw new Error(); }
    // Lossless conversion followed by a conversion back
    // with another use of the intermediate result.
    double d = (double) value;
    int i = (int) d;
    return "d=" + d + ", i=" + i;
  }

  /// CHECK-START: int Main.$noinline$byteToDoubleToInt(byte) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:b\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Double>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$byteToDoubleToInt(byte) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:b\d+>>      ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  /// CHECK-START: int Main.$noinline$byteToDoubleToInt(byte) instruction_simplifier (after)
  /// CHECK-NOT:                        TypeConversion

  public static int $noinline$byteToDoubleToInt(byte value) {
    if (doThrow) { throw new Error(); }
    // Lossless conversion followed by another conversion, use implicit conversion.
    return (int) (double) value;
  }

  /// CHECK-START: int Main.$noinline$floatToDoubleToInt(float) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Double>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$floatToDoubleToInt(float) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$floatToDoubleToInt(float) instruction_simplifier (after)
  /// CHECK-DAG:                        TypeConversion
  /// CHECK-NOT:                        TypeConversion

  public static int $noinline$floatToDoubleToInt(float value) {
    if (doThrow) { throw new Error(); }
    // Lossless conversion followed by another conversion.
    return (int) (double) value;
  }

  /// CHECK-START: java.lang.String Main.$noinline$floatToDoubleToIntPrint(float) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      {{i\d+}}          TypeConversion [<<Double>>]

  /// CHECK-START: java.lang.String Main.$noinline$floatToDoubleToIntPrint(float) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      {{i\d+}}          TypeConversion [<<Double>>]

  public static String $noinline$floatToDoubleToIntPrint(float value) {
    if (doThrow) { throw new Error(); }
    // Lossless conversion followed by another conversion with
    // an extra use of the intermediate result.
    double d = (double) value;
    int i = (int) d;
    return "d=" + d + ", i=" + i;
  }

  /// CHECK-START: short Main.$noinline$byteToDoubleToShort(byte) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:b\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Double>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$byteToDoubleToShort(byte) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:b\d+>>      ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  /// CHECK-START: short Main.$noinline$byteToDoubleToShort(byte) instruction_simplifier (after)
  /// CHECK-NOT:                        TypeConversion

  public static short $noinline$byteToDoubleToShort(byte value) {
    if (doThrow) { throw new Error(); }
    // Originally, this is byte->double->int->short. The first conversion is lossless,
    // so we merge this with the second one to byte->int which we omit as it's an implicit
    // conversion. Then we eliminate the resulting byte->short as an implicit conversion.
    return (short) (double) value;
  }

  /// CHECK-START: short Main.$noinline$charToDoubleToShort(char) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:c\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Double>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$charToDoubleToShort(char) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:c\d+>>      ParameterValue
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$charToDoubleToShort(char) instruction_simplifier (after)
  /// CHECK-DAG:                        TypeConversion
  /// CHECK-NOT:                        TypeConversion

  public static short $noinline$charToDoubleToShort(char value) {
    if (doThrow) { throw new Error(); }
    // Originally, this is char->double->int->short. The first conversion is lossless,
    // so we merge this with the second one to char->int which we omit as it's an implicit
    // conversion. Then we are left with the resulting char->short conversion.
    return (short) (double) value;
  }

  /// CHECK-START: short Main.$noinline$floatToIntToShort(float) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$floatToIntToShort(float) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:f\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  public static short $noinline$floatToIntToShort(float value) {
    if (doThrow) { throw new Error(); }
    // Lossy FP to integral conversion followed by another conversion: no simplification.
    return (short) value;
  }

  /// CHECK-START: int Main.$noinline$intToFloatToInt(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Float:f\d+>>    TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Float>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$intToFloatToInt(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Float:f\d+>>    TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Float>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  public static int $noinline$intToFloatToInt(int value) {
    if (doThrow) { throw new Error(); }
    // Lossy integral to FP conversion followed another conversion: no simplification.
    return (int) (float) value;
  }

  /// CHECK-START: double Main.$noinline$longToIntToDouble(long) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Double>>]

  /// CHECK-START: double Main.$noinline$longToIntToDouble(long) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Double>>]

  public static double $noinline$longToIntToDouble(long value) {
    if (doThrow) { throw new Error(); }
    // Lossy long-to-int conversion followed an integral to FP conversion: no simplification.
    return (double) (int) value;
  }

  /// CHECK-START: long Main.$noinline$longToIntToLong(long) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Long:j\d+>>     TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Long>>]

  /// CHECK-START: long Main.$noinline$longToIntToLong(long) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Long:j\d+>>     TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Long>>]

  public static long $noinline$longToIntToLong(long value) {
    if (doThrow) { throw new Error(); }
    // Lossy long-to-int conversion followed an int-to-long conversion: no simplification.
    return (long) (int) value;
  }

  /// CHECK-START: short Main.$noinline$shortToCharToShort(short) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<Char>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$shortToCharToShort(short) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  public static short $noinline$shortToCharToShort(short value) {
    if (doThrow) { throw new Error(); }
    // Integral conversion followed by non-widening integral conversion to original type.
    return (short) (char) value;
  }

  /// CHECK-START: int Main.$noinline$shortToLongToInt(short) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Long:j\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<Long>>]
  /// CHECK-DAG:                        Return [<<Int>>]

  /// CHECK-START: int Main.$noinline$shortToLongToInt(short) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:                        Return [<<Arg>>]

  public static int $noinline$shortToLongToInt(short value) {
    if (doThrow) { throw new Error(); }
    // Integral conversion followed by non-widening integral conversion, use implicit conversion.
    return (int) (long) value;
  }

  /// CHECK-START: byte Main.$noinline$shortToCharToByte(short) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:      <<Byte:b\d+>>     TypeConversion [<<Char>>]
  /// CHECK-DAG:                        Return [<<Byte>>]

  /// CHECK-START: byte Main.$noinline$shortToCharToByte(short) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Byte:b\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Byte>>]

  public static byte $noinline$shortToCharToByte(short value) {
    if (doThrow) { throw new Error(); }
    // Integral conversion followed by non-widening integral conversion losing bits
    // from the original type. Simplify to use only one conversion.
    return (byte) (char) value;
  }

  /// CHECK-START: java.lang.String Main.$noinline$shortToCharToBytePrint(short) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:      {{b\d+}}          TypeConversion [<<Char>>]

  /// CHECK-START: java.lang.String Main.$noinline$shortToCharToBytePrint(short) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:      {{b\d+}}          TypeConversion [<<Char>>]

  public static String $noinline$shortToCharToBytePrint(short value) {
    if (doThrow) { throw new Error(); }
    // Integral conversion followed by non-widening integral conversion losing bits
    // from the original type with an extra use of the intermediate result.
    char c = (char) value;
    byte b = (byte) c;
    return "c=" + ((int) c) + ", b=" + ((int) b);  // implicit conversions.
  }

  /// CHECK-START: byte Main.$noinline$longAnd0xffToByte(long) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Mask:j\d+>>     LongConstant 255
  /// CHECK-DAG:      <<And:j\d+>>      And [<<Mask>>,<<Arg>>]
  /// CHECK-DAG:      <<Int:i\d+>>      TypeConversion [<<And>>]
  /// CHECK-DAG:      <<Byte:b\d+>>     TypeConversion [<<Int>>]
  /// CHECK-DAG:                        Return [<<Byte>>]

  /// CHECK-START: byte Main.$noinline$longAnd0xffToByte(long) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:j\d+>>      ParameterValue
  /// CHECK-DAG:      <<Byte:b\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Byte>>]

  /// CHECK-START: byte Main.$noinline$longAnd0xffToByte(long) instruction_simplifier (after)
  /// CHECK-NOT:                        And

  public static byte $noinline$longAnd0xffToByte(long value) {
    if (doThrow) { throw new Error(); }
    return (byte) (value & 0xff);
  }

  /// CHECK-START: char Main.$noinline$intAnd0x1ffffToChar(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Mask:i\d+>>     IntConstant 131071
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Mask>>,<<Arg>>]
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<And>>]
  /// CHECK-DAG:                        Return [<<Char>>]

  /// CHECK-START: char Main.$noinline$intAnd0x1ffffToChar(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Char:c\d+>>     TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Char>>]

  /// CHECK-START: char Main.$noinline$intAnd0x1ffffToChar(int) instruction_simplifier (after)
  /// CHECK-NOT:                        And

  public static char $noinline$intAnd0x1ffffToChar(int value) {
    if (doThrow) { throw new Error(); }
    // Keeping all significant bits and one more.
    return (char) (value & 0x1ffff);
  }

  /// CHECK-START: short Main.$noinline$intAnd0x17fffToShort(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Mask:i\d+>>     IntConstant 98303
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Mask>>,<<Arg>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<And>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  /// CHECK-START: short Main.$noinline$intAnd0x17fffToShort(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Mask:i\d+>>     IntConstant 98303
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Mask>>,<<Arg>>]
  /// CHECK-DAG:      <<Short:s\d+>>    TypeConversion [<<And>>]
  /// CHECK-DAG:                        Return [<<Short>>]

  public static short $noinline$intAnd0x17fffToShort(int value) {
    if (doThrow) { throw new Error(); }
    // No simplification: clearing a significant bit.
    return (short) (value & 0x17fff);
  }

  /// CHECK-START: double Main.$noinline$shortAnd0xffffToShortToDouble(short) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Mask:i\d+>>     IntConstant 65535
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Mask>>,<<Arg>>]
  /// CHECK-DAG:      <<Same:s\d+>>     TypeConversion [<<And>>]
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Same>>]
  /// CHECK-DAG:                        Return [<<Double>>]

  /// CHECK-START: double Main.$noinline$shortAnd0xffffToShortToDouble(short) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:s\d+>>      ParameterValue
  /// CHECK-DAG:      <<Double:d\d+>>   TypeConversion [<<Arg>>]
  /// CHECK-DAG:                        Return [<<Double>>]

  public static double $noinline$shortAnd0xffffToShortToDouble(short value) {
    if (doThrow) { throw new Error(); }
    short same = (short) (value & 0xffff);
    return (double) same;
  }

  /// CHECK-START: int Main.$noinline$intReverseCondition(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<LE:z\d+>>       LessThanOrEqual [<<Const42>>,<<Arg>>]

  /// CHECK-START: int Main.$noinline$intReverseCondition(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Arg:i\d+>>      ParameterValue
  /// CHECK-DAG:      <<Const42:i\d+>>  IntConstant 42
  /// CHECK-DAG:      <<GE:z\d+>>       GreaterThanOrEqual [<<Arg>>,<<Const42>>]

  public static int $noinline$intReverseCondition(int i) {
    if (doThrow) { throw new Error(); }
    return (42 > i) ? 13 : 54;
  }

  /// CHECK-START: int Main.$noinline$intReverseConditionNaN(int) instruction_simplifier (before)
  /// CHECK-DAG:      <<Const42:d\d+>>  DoubleConstant 42
  /// CHECK-DAG:      <<Result:d\d+>>   InvokeStaticOrDirect
  /// CHECK-DAG:      <<CMP:i\d+>>      Compare [<<Const42>>,<<Result>>]

  /// CHECK-START: int Main.$noinline$intReverseConditionNaN(int) instruction_simplifier (after)
  /// CHECK-DAG:      <<Const42:d\d+>>  DoubleConstant 42
  /// CHECK-DAG:      <<Result:d\d+>>   InvokeStaticOrDirect
  /// CHECK-DAG:      <<EQ:z\d+>>       Equal [<<Result>>,<<Const42>>]

  public static int $noinline$intReverseConditionNaN(int i) {
    if (doThrow) { throw new Error(); }
    return (42 != Math.sqrt(i)) ? 13 : 54;
  }

  public static int $noinline$runSmaliTest(String name, boolean input) {
    if (doThrow) { throw new Error(); }
    try {
      Class<?> c = Class.forName("SmaliTests");
      Method m = c.getMethod(name, new Class[] { boolean.class });
      return (Integer) m.invoke(null, input);
    } catch (Exception ex) {
      throw new Error(ex);
    }
  }

  /// CHECK-START: int Main.$noinline$intUnnecessaryShiftMasking(int, int) instruction_simplifier (before)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const31:i\d+>>  IntConstant 31
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const31>>]
  /// CHECK-DAG:      <<Shl:i\d+>>      Shl [<<Value>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Shl>>]

  /// CHECK-START: int Main.$noinline$intUnnecessaryShiftMasking(int, int) instruction_simplifier (after)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Shl:i\d+>>      Shl [<<Value>>,<<Shift>>]
  /// CHECK-DAG:                        Return [<<Shl>>]

  public static int $noinline$intUnnecessaryShiftMasking(int value, int shift) {
    if (doThrow) { throw new Error(); }
    return value << (shift & 31);
  }

  /// CHECK-START: long Main.$noinline$longUnnecessaryShiftMasking(long, int) instruction_simplifier (before)
  /// CHECK:          <<Value:j\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const63:i\d+>>  IntConstant 63
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const63>>]
  /// CHECK-DAG:      <<Shr:j\d+>>      Shr [<<Value>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Shr>>]

  /// CHECK-START: long Main.$noinline$longUnnecessaryShiftMasking(long, int) instruction_simplifier (after)
  /// CHECK:          <<Value:j\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Shr:j\d+>>      Shr [<<Value>>,<<Shift>>]
  /// CHECK-DAG:                        Return [<<Shr>>]

  public static long $noinline$longUnnecessaryShiftMasking(long value, int shift) {
    if (doThrow) { throw new Error(); }
    return value >> (shift & 63);
  }

  /// CHECK-START: int Main.$noinline$intUnnecessaryWiderShiftMasking(int, int) instruction_simplifier (before)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const255:i\d+>> IntConstant 255
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const255>>]
  /// CHECK-DAG:      <<UShr:i\d+>>     UShr [<<Value>>,<<And>>]
  /// CHECK-DAG:                        Return [<<UShr>>]

  /// CHECK-START: int Main.$noinline$intUnnecessaryWiderShiftMasking(int, int) instruction_simplifier (after)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<UShr:i\d+>>     UShr [<<Value>>,<<Shift>>]
  /// CHECK-DAG:                        Return [<<UShr>>]

  public static int $noinline$intUnnecessaryWiderShiftMasking(int value, int shift) {
    if (doThrow) { throw new Error(); }
    return value >>> (shift & 0xff);
  }

  /// CHECK-START: long Main.$noinline$longSmallerShiftMasking(long, int) instruction_simplifier (before)
  /// CHECK:          <<Value:j\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const3:i\d+>>   IntConstant 3
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const3>>]
  /// CHECK-DAG:      <<Shl:j\d+>>      Shl [<<Value>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Shl>>]

  /// CHECK-START: long Main.$noinline$longSmallerShiftMasking(long, int) instruction_simplifier (after)
  /// CHECK:          <<Value:j\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const3:i\d+>>   IntConstant 3
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const3>>]
  /// CHECK-DAG:      <<Shl:j\d+>>      Shl [<<Value>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Shl>>]

  public static long $noinline$longSmallerShiftMasking(long value, int shift) {
    if (doThrow) { throw new Error(); }
    return value << (shift & 3);
  }

  /// CHECK-START: int Main.$noinline$otherUseOfUnnecessaryShiftMasking(int, int) instruction_simplifier (before)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const31:i\d+>>  IntConstant 31
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const31>>]
  /// CHECK-DAG:      <<Shr:i\d+>>      Shr [<<Value>>,<<And>>]
  /// CHECK-DAG:      <<Add:i\d+>>      Add [<<Shr>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Add>>]

  /// CHECK-START: int Main.$noinline$otherUseOfUnnecessaryShiftMasking(int, int) instruction_simplifier (after)
  /// CHECK:          <<Value:i\d+>>    ParameterValue
  /// CHECK:          <<Shift:i\d+>>    ParameterValue
  /// CHECK-DAG:      <<Const31:i\d+>>  IntConstant 31
  /// CHECK-DAG:      <<And:i\d+>>      And [<<Shift>>,<<Const31>>]
  /// CHECK-DAG:      <<Shr:i\d+>>      Shr [<<Value>>,<<Shift>>]
  /// CHECK-DAG:      <<Add:i\d+>>      Add [<<Shr>>,<<And>>]
  /// CHECK-DAG:                        Return [<<Add>>]

  public static int $noinline$otherUseOfUnnecessaryShiftMasking(int value, int shift) {
    if (doThrow) { throw new Error(); }
    int temp = shift & 31;
    return (value >> temp) + temp;
  }

public static void main(String[] args) {
    int arg = 123456;

    assertLongEquals(arg, $noinline$Add0(arg));
    assertIntEquals(arg, $noinline$AndAllOnes(arg));
    assertLongEquals(arg, $noinline$Div1(arg));
    assertIntEquals(-arg, $noinline$DivN1(arg));
    assertLongEquals(arg, $noinline$Mul1(arg));
    assertIntEquals(-arg, $noinline$MulN1(arg));
    assertLongEquals((128 * arg), $noinline$MulPowerOfTwo128(arg));
    assertIntEquals(arg, $noinline$Or0(arg));
    assertLongEquals(arg, $noinline$OrSame(arg));
    assertIntEquals(arg, $noinline$Shl0(arg));
    assertLongEquals(arg, $noinline$Shr0(arg));
    assertLongEquals(arg, $noinline$Shr64(arg));
    assertLongEquals(arg, $noinline$Sub0(arg));
    assertIntEquals(-arg, $noinline$SubAliasNeg(arg));
    assertLongEquals(arg, $noinline$UShr0(arg));
    assertIntEquals(arg, $noinline$Xor0(arg));
    assertIntEquals(~arg, $noinline$XorAllOnes(arg));
    assertIntEquals(-(arg + arg + 1), $noinline$AddNegs1(arg, arg + 1));
    assertIntEquals(-(arg + arg + 1), $noinline$AddNegs2(arg, arg + 1));
    assertLongEquals(-(2 * arg + 1), $noinline$AddNegs3(arg, arg + 1));
    assertLongEquals(1, $noinline$AddNeg1(arg, arg + 1));
    assertLongEquals(-1, $noinline$AddNeg2(arg, arg + 1));
    assertLongEquals(arg, $noinline$NegNeg1(arg));
    assertIntEquals(0, $noinline$NegNeg2(arg));
    assertLongEquals(arg, $noinline$NegNeg3(arg));
    assertIntEquals(1, $noinline$NegSub1(arg, arg + 1));
    assertIntEquals(1, $noinline$NegSub2(arg, arg + 1));
    assertLongEquals(arg, $noinline$NotNot1(arg));
    assertIntEquals(-1, $noinline$NotNot2(arg));
    assertIntEquals(-(arg + arg + 1), $noinline$SubNeg1(arg, arg + 1));
    assertIntEquals(-(arg + arg + 1), $noinline$SubNeg2(arg, arg + 1));
    assertLongEquals(-(2 * arg + 1), $noinline$SubNeg3(arg, arg + 1));
    assertBooleanEquals(true, $noinline$EqualBoolVsIntConst(true));
    assertBooleanEquals(true, $noinline$EqualBoolVsIntConst(true));
    assertBooleanEquals(false, $noinline$NotEqualBoolVsIntConst(false));
    assertBooleanEquals(false, $noinline$NotEqualBoolVsIntConst(false));
    assertBooleanEquals(true, $noinline$NotNotBool(true));
    assertBooleanEquals(false, $noinline$NotNotBool(false));
    assertFloatEquals(50.0f, $noinline$Div2(100.0f));
    assertDoubleEquals(75.0, $noinline$Div2(150.0));
    assertFloatEquals(-400.0f, $noinline$DivMP25(100.0f));
    assertDoubleEquals(-600.0, $noinline$DivMP25(150.0));
    assertIntEquals(0xc, $noinline$UShr28And15(0xc1234567));
    assertLongEquals(0xcL, $noinline$UShr60And15(0xc123456787654321L));
    assertIntEquals(0x4, $noinline$UShr28And7(0xc1234567));
    assertLongEquals(0x4L, $noinline$UShr60And7(0xc123456787654321L));
    assertIntEquals(0xc1, $noinline$Shr24And255(0xc1234567));
    assertLongEquals(0xc1L, $noinline$Shr56And255(0xc123456787654321L));
    assertIntEquals(0x41, $noinline$Shr24And127(0xc1234567));
    assertLongEquals(0x41L, $noinline$Shr56And127(0xc123456787654321L));
    assertIntEquals(0, $noinline$mulPow2Plus1(0));
    assertIntEquals(9, $noinline$mulPow2Plus1(1));
    assertIntEquals(18, $noinline$mulPow2Plus1(2));
    assertIntEquals(900, $noinline$mulPow2Plus1(100));
    assertIntEquals(111105, $noinline$mulPow2Plus1(12345));
    assertLongEquals(0, $noinline$mulPow2Minus1(0));
    assertLongEquals(31, $noinline$mulPow2Minus1(1));
    assertLongEquals(62, $noinline$mulPow2Minus1(2));
    assertLongEquals(3100, $noinline$mulPow2Minus1(100));
    assertLongEquals(382695, $noinline$mulPow2Minus1(12345));

    booleanField = false;
    assertIntEquals($noinline$booleanFieldNotEqualOne(), 54);
    assertIntEquals($noinline$booleanFieldEqualZero(), 54);
    booleanField = true;
    assertIntEquals(13, $noinline$booleanFieldNotEqualOne());
    assertIntEquals(13, $noinline$booleanFieldEqualZero());
    assertIntEquals(54, $noinline$intConditionNotEqualOne(6));
    assertIntEquals(13, $noinline$intConditionNotEqualOne(43));
    assertIntEquals(54, $noinline$intConditionEqualZero(6));
    assertIntEquals(13, $noinline$intConditionEqualZero(43));
    assertIntEquals(54, $noinline$floatConditionNotEqualOne(6.0f));
    assertIntEquals(13, $noinline$floatConditionNotEqualOne(43.0f));
    assertIntEquals(54, $noinline$doubleConditionEqualZero(6.0));
    assertIntEquals(13, $noinline$doubleConditionEqualZero(43.0));

    assertIntEquals(1234567, $noinline$intToDoubleToInt(1234567));
    assertIntEquals(Integer.MIN_VALUE, $noinline$intToDoubleToInt(Integer.MIN_VALUE));
    assertIntEquals(Integer.MAX_VALUE, $noinline$intToDoubleToInt(Integer.MAX_VALUE));
    assertStringEquals("d=7654321.0, i=7654321", $noinline$intToDoubleToIntPrint(7654321));
    assertIntEquals(12, $noinline$byteToDoubleToInt((byte) 12));
    assertIntEquals(Byte.MIN_VALUE, $noinline$byteToDoubleToInt(Byte.MIN_VALUE));
    assertIntEquals(Byte.MAX_VALUE, $noinline$byteToDoubleToInt(Byte.MAX_VALUE));
    assertIntEquals(11, $noinline$floatToDoubleToInt(11.3f));
    assertStringEquals("d=12.25, i=12", $noinline$floatToDoubleToIntPrint(12.25f));
    assertIntEquals(123, $noinline$byteToDoubleToShort((byte) 123));
    assertIntEquals(Byte.MIN_VALUE, $noinline$byteToDoubleToShort(Byte.MIN_VALUE));
    assertIntEquals(Byte.MAX_VALUE, $noinline$byteToDoubleToShort(Byte.MAX_VALUE));
    assertIntEquals(1234, $noinline$charToDoubleToShort((char) 1234));
    assertIntEquals(Character.MIN_VALUE, $noinline$charToDoubleToShort(Character.MIN_VALUE));
    assertIntEquals(/* sign-extended */ -1, $noinline$charToDoubleToShort(Character.MAX_VALUE));
    assertIntEquals(12345, $noinline$floatToIntToShort(12345.75f));
    assertIntEquals(Short.MAX_VALUE, $noinline$floatToIntToShort((float)(Short.MIN_VALUE - 1)));
    assertIntEquals(Short.MIN_VALUE, $noinline$floatToIntToShort((float)(Short.MAX_VALUE + 1)));
    assertIntEquals(-54321, $noinline$intToFloatToInt(-54321));
    assertDoubleEquals((double) 0x12345678, $noinline$longToIntToDouble(0x1234567812345678L));
    assertDoubleEquals(0.0, $noinline$longToIntToDouble(Long.MIN_VALUE));
    assertDoubleEquals(-1.0, $noinline$longToIntToDouble(Long.MAX_VALUE));
    assertLongEquals(0x0000000012345678L, $noinline$longToIntToLong(0x1234567812345678L));
    assertLongEquals(0xffffffff87654321L, $noinline$longToIntToLong(0x1234567887654321L));
    assertLongEquals(0L, $noinline$longToIntToLong(Long.MIN_VALUE));
    assertLongEquals(-1L, $noinline$longToIntToLong(Long.MAX_VALUE));
    assertIntEquals((short) -5678, $noinline$shortToCharToShort((short) -5678));
    assertIntEquals(Short.MIN_VALUE, $noinline$shortToCharToShort(Short.MIN_VALUE));
    assertIntEquals(Short.MAX_VALUE, $noinline$shortToCharToShort(Short.MAX_VALUE));
    assertIntEquals(5678, $noinline$shortToLongToInt((short) 5678));
    assertIntEquals(Short.MIN_VALUE, $noinline$shortToLongToInt(Short.MIN_VALUE));
    assertIntEquals(Short.MAX_VALUE, $noinline$shortToLongToInt(Short.MAX_VALUE));
    assertIntEquals(0x34, $noinline$shortToCharToByte((short) 0x1234));
    assertIntEquals(-0x10, $noinline$shortToCharToByte((short) 0x12f0));
    assertIntEquals(0, $noinline$shortToCharToByte(Short.MIN_VALUE));
    assertIntEquals(-1, $noinline$shortToCharToByte(Short.MAX_VALUE));
    assertStringEquals("c=1025, b=1", $noinline$shortToCharToBytePrint((short) 1025));
    assertStringEquals("c=1023, b=-1", $noinline$shortToCharToBytePrint((short) 1023));
    assertStringEquals("c=65535, b=-1", $noinline$shortToCharToBytePrint((short) -1));

    assertIntEquals(0x21, $noinline$longAnd0xffToByte(0x1234432112344321L));
    assertIntEquals(0, $noinline$longAnd0xffToByte(Long.MIN_VALUE));
    assertIntEquals(-1, $noinline$longAnd0xffToByte(Long.MAX_VALUE));
    assertIntEquals(0x1234, $noinline$intAnd0x1ffffToChar(0x43211234));
    assertIntEquals(0, $noinline$intAnd0x1ffffToChar(Integer.MIN_VALUE));
    assertIntEquals(Character.MAX_VALUE, $noinline$intAnd0x1ffffToChar(Integer.MAX_VALUE));
    assertIntEquals(0x4321, $noinline$intAnd0x17fffToShort(0x87654321));
    assertIntEquals(0x0888, $noinline$intAnd0x17fffToShort(0x88888888));
    assertIntEquals(0, $noinline$intAnd0x17fffToShort(Integer.MIN_VALUE));
    assertIntEquals(Short.MAX_VALUE, $noinline$intAnd0x17fffToShort(Integer.MAX_VALUE));

    assertDoubleEquals(0.0, $noinline$shortAnd0xffffToShortToDouble((short) 0));
    assertDoubleEquals(1.0, $noinline$shortAnd0xffffToShortToDouble((short) 1));
    assertDoubleEquals(-2.0, $noinline$shortAnd0xffffToShortToDouble((short) -2));
    assertDoubleEquals(12345.0, $noinline$shortAnd0xffffToShortToDouble((short) 12345));
    assertDoubleEquals((double)Short.MAX_VALUE,
                       $noinline$shortAnd0xffffToShortToDouble(Short.MAX_VALUE));
    assertDoubleEquals((double)Short.MIN_VALUE,
                       $noinline$shortAnd0xffffToShortToDouble(Short.MIN_VALUE));

    assertIntEquals(13, $noinline$intReverseCondition(41));
    assertIntEquals(13, $noinline$intReverseConditionNaN(-5));

    for (String condition : new String[] { "Equal", "NotEqual" }) {
      for (String constant : new String[] { "True", "False" }) {
        for (String side : new String[] { "Rhs", "Lhs" }) {
          String name = condition + constant + side;
          assertIntEquals(5, $noinline$runSmaliTest(name, true));
          assertIntEquals(3, $noinline$runSmaliTest(name, false));
        }
      }
    }

    assertIntEquals(0x5e6f7808, $noinline$intUnnecessaryShiftMasking(0xabcdef01, 3));
    assertIntEquals(0x5e6f7808, $noinline$intUnnecessaryShiftMasking(0xabcdef01, 3 + 32));
    assertLongEquals(0xffffffffffffeaf3L, $noinline$longUnnecessaryShiftMasking(0xabcdef0123456789L, 50));
    assertLongEquals(0xffffffffffffeaf3L, $noinline$longUnnecessaryShiftMasking(0xabcdef0123456789L, 50 + 64));
    assertIntEquals(0x2af37b, $noinline$intUnnecessaryWiderShiftMasking(0xabcdef01, 10));
    assertIntEquals(0x2af37b, $noinline$intUnnecessaryWiderShiftMasking(0xabcdef01, 10 + 128));
    assertLongEquals(0xaf37bc048d159e24L, $noinline$longSmallerShiftMasking(0xabcdef0123456789L, 2));
    assertLongEquals(0xaf37bc048d159e24L, $noinline$longSmallerShiftMasking(0xabcdef0123456789L, 2 + 256));
    assertIntEquals(0xfffd5e7c, $noinline$otherUseOfUnnecessaryShiftMasking(0xabcdef01, 13));
    assertIntEquals(0xfffd5e7c, $noinline$otherUseOfUnnecessaryShiftMasking(0xabcdef01, 13 + 512));
  }

  private static boolean $inline$true() { return true; }
  private static boolean $inline$false() { return false; }

  public static boolean booleanField;
}
