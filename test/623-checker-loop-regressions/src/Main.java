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

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
