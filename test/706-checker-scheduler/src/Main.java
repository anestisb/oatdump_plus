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

  static int static_variable = 0;

  /// CHECK-START-ARM64: int Main.arrayAccess() scheduler (before)
  /// CHECK:    <<Const1:i\d+>>       IntConstant 1
  /// CHECK:    <<i0:i\d+>>           Phi
  /// CHECK:    <<res0:i\d+>>         Phi
  /// CHECK:    <<Array:i\d+>>        IntermediateAddress
  /// CHECK:    <<ArrayGet1:i\d+>>    ArrayGet [<<Array>>,<<i0>>]
  /// CHECK:    <<res1:i\d+>>         Add [<<res0>>,<<ArrayGet1>>]
  /// CHECK:    <<i1:i\d+>>           Add [<<i0>>,<<Const1>>]
  /// CHECK:    <<ArrayGet2:i\d+>>    ArrayGet [<<Array>>,<<i1>>]
  /// CHECK:                          Add [<<res1>>,<<ArrayGet2>>]

  /// CHECK-START-ARM64: int Main.arrayAccess() scheduler (after)
  /// CHECK:    <<Const1:i\d+>>       IntConstant 1
  /// CHECK:    <<i0:i\d+>>           Phi
  /// CHECK:    <<res0:i\d+>>         Phi
  /// CHECK:    <<Array:i\d+>>        IntermediateAddress
  /// CHECK:    <<ArrayGet1:i\d+>>    ArrayGet [<<Array>>,<<i0>>]
  /// CHECK:    <<i1:i\d+>>           Add [<<i0>>,<<Const1>>]
  /// CHECK:    <<ArrayGet2:i\d+>>    ArrayGet [<<Array>>,<<i1>>]
  /// CHECK:    <<res1:i\d+>>         Add [<<res0>>,<<ArrayGet1>>]
  /// CHECK:                          Add [<<res1>>,<<ArrayGet2>>]

  public static int arrayAccess() {
    int res = 0;
    int [] array = new int[10];
    for (int i = 0; i < 9; i++) {
      res += array[i];
      res += array[i + 1];
    }
    return res;
  }

  /// CHECK-START-ARM64: int Main.intDiv(int) scheduler (before)
  /// CHECK:               Sub
  /// CHECK:               DivZeroCheck
  /// CHECK:               Div
  /// CHECK:               StaticFieldSet

  /// CHECK-START-ARM64: int Main.intDiv(int) scheduler (after)
  /// CHECK:               Sub
  /// CHECK-NOT:           StaticFieldSet
  /// CHECK:               DivZeroCheck
  /// CHECK-NOT:           Sub
  /// CHECK:               Div
  public static int intDiv(int arg) {
    int res = 0;
    int tmp = arg;
    for (int i = 1; i < arg; i++) {
      tmp -= i;
      res = res / i;  // div-zero check barrier.
      static_variable++;
    }
    res += tmp;
    return res;
  }

  public static void main(String[] args) {
    if ((arrayAccess() + intDiv(10)) != -35) {
      System.out.println("FAIL");
    }
  }
}
