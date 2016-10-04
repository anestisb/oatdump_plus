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
 * Tests on loop optimizations related to induction.
 */
public class Main {

  static int[] a = new int[10];

  /// CHECK-START: void Main.deadSingleLoop() loop_optimization (before)
  /// CHECK-DAG: Phi loop:{{B\d+}} outer_loop:none
  //
  /// CHECK-START: void Main.deadSingleLoop() loop_optimization (after)
  /// CHECK-NOT: Phi loop:{{B\d+}} outer_loop:none
  static void deadSingleLoop() {
    for (int i = 0; i < 4; i++) {
    }
  }

  /// CHECK-START: void Main.deadNestedLoops() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:{{B\d+}}      outer_loop:<<Loop>>
  //
  /// CHECK-START: void Main.deadNestedLoops() loop_optimization (after)
  /// CHECK-NOT: Phi loop:{{B\d+}}
  static void deadNestedLoops() {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
      }
    }
  }

  /// CHECK-START: void Main.deadNestedAndFollowingLoops() loop_optimization (before)
  /// CHECK-DAG: Phi loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi loop:<<Loop2:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: Phi loop:{{B\d+}}       outer_loop:<<Loop2>>
  /// CHECK-DAG: Phi loop:{{B\d+}}       outer_loop:<<Loop2>>
  /// CHECK-DAG: Phi loop:<<Loop3:B\d+>> outer_loop:<<Loop1>>
  /// CHECK-DAG: Phi loop:{{B\d+}}       outer_loop:<<Loop3>>
  /// CHECK-DAG: Phi loop:{{B\d+}}       outer_loop:none
  //
  /// CHECK-START: void Main.deadNestedAndFollowingLoops() loop_optimization (after)
  /// CHECK-NOT: Phi loop:{{B\d+}}
  static void deadNestedAndFollowingLoops() {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        for (int k = 0; k < 4; k++) {
        }
        for (int k = 0; k < 4; k++) {
        }
      }
      for (int j = 0; j < 4; j++) {
        for (int k = 0; k < 4; k++) {
        }
      }
    }
    for (int i = 0; i < 4; i++) {
    }
  }

  /// CHECK-START: void Main.deadInduction() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.deadInduction() loop_optimization (after)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-NOT: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  static void deadInduction() {
    int dead = 0;
    for (int i = 0; i < a.length; i++) {
      a[i] = 1;
      dead += 5;
    }
  }

  /// CHECK-START: void Main.deadManyInduction() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.deadManyInduction() loop_optimization (after)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-NOT: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  static void deadManyInduction() {
    int dead1 = 0, dead2 = 1, dead3 = 3;
    for (int i = 0; i < a.length; i++) {
      dead1 += 5;
      a[i] = 2;
      dead2 += 10;
      dead3 += 100;
    }
  }

  /// CHECK-START: void Main.deadSequence() loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.deadSequence() loop_optimization (after)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-NOT: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  static void deadSequence() {
    int dead = 0;
    for (int i = 0; i < a.length; i++) {
      a[i] = 3;
      // Increment value defined inside loop,
      // but sequence itself not used anywhere.
      dead += i;
    }
  }

  /// CHECK-START: void Main.deadCycleWithException(int) loop_optimization (before)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  //
  /// CHECK-START: void Main.deadCycleWithException(int) loop_optimization (after)
  /// CHECK-DAG: Phi      loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-NOT: Phi      loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: ArrayGet loop:<<Loop>>      outer_loop:none
  static void deadCycleWithException(int k) {
    int dead = 0;
    for (int i = 0; i < a.length; i++) {
      a[i] = 4;
      // Increment value of dead cycle may throw exception.
      dead += a[k];
    }
  }

  /// CHECK-START: int Main.closedFormInductionUp() loop_optimization (before)
  /// CHECK-DAG: <<Phi1:i\d+>> Phi               loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>> Phi               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               Return [<<Phi1>>] loop:none
  //
  /// CHECK-START: int Main.closedFormInductionUp() loop_optimization (after)
  /// CHECK-NOT:               Phi    loop:B\d+ outer_loop:none
  /// CHECK-DAG:               Return loop:none
  static int closedFormInductionUp() {
    int closed = 12345;
    for (int i = 0; i < 10; i++) {
      closed += 5;
    }
    return closed;  // only needs last value
  }

  /// CHECK-START: int Main.closedFormInductionInAndDown(int) loop_optimization (before)
  /// CHECK-DAG: <<Phi1:i\d+>> Phi               loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>> Phi               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               Return [<<Phi2>>] loop:none
  //
  /// CHECK-START: int Main.closedFormInductionInAndDown(int) loop_optimization (after)
  /// CHECK-NOT:               Phi    loop:B\d+ outer_loop:none
  /// CHECK-DAG:               Return loop:none
  static int closedFormInductionInAndDown(int closed) {
    for (int i = 0; i < 10; i++) {
      closed -= 5;
    }
    return closed;  // only needs last value
  }

  // TODO: taken test around closed form?
  static int closedFormInductionUpN(int n) {
    int closed = 12345;
    for (int i = 0; i < n; i++) {
      closed += 5;
    }
    return closed;  // only needs last value
  }

  // TODO: taken test around closed form?
  static int closedFormInductionInAndDownN(int closed, int n) {
    for (int i = 0; i < n; i++) {
      closed -= 5;
    }
    return closed;  // only needs last value
  }

  // TODO: move closed form even further out?
  static int closedFormNested(int n) {
    int closed = 0;
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < 10; j++) {
        closed++;
      }
    }
    return closed;  // only needs last-value
  }

  // TODO: handle as closed/empty eventually?
  static int mainIndexReturned(int n) {
    int i;
    for (i = 0; i < n; i++);
    return i;
  }

  // If ever replaced by closed form, last value should be correct!
  static int periodicReturned(int n) {
    int k = 0;
    for (int i = 0; i < n; i++) {
      k = 1 - k;
    }
    return k;
  }

  // Same here.
  private static int getSum(int n) {
    int k = 0;
    int sum = 0;
    for (int i = 0; i < n; i++) {
      k++;
      sum += k;
    }
    return sum;
  }

  // Same here.
  private static int getSum21() {
    int k = 0;
    int sum = 0;
    for (int i = 0; i < 6; i++) {
      k++;
      sum += k;
    }
    return sum;
  }

  // Same here.
  private static int closedTwice() {
    int closed = 0;
    for (int i = 0; i < 10; i++) {
      closed++;
    }
    // Closed form of first loop defines trip count of second loop.
    int other_closed = 0;
    for (int i = 0; i < closed; i++) {
      other_closed++;
    }
    return other_closed;
  }

  /// CHECK-START: int Main.closedFeed() loop_optimization (before)
  /// CHECK-DAG: <<Phi1:i\d+>> Phi               loop:<<Loop1:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>> Phi               loop:<<Loop1>>      outer_loop:none
  /// CHECK-DAG: <<Phi3:i\d+>> Phi               loop:<<Loop2:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi4:i\d+>> Phi               loop:<<Loop2>>      outer_loop:none
  /// CHECK-DAG:               Return [<<Phi3>>] loop:none
  /// CHECK-EVAL: "<<Loop1>>" != "<<Loop2>>"
  //
  /// CHECK-START: int Main.closedFeed() loop_optimization (after)
  /// CHECK-NOT:               Phi    loop:B\d+ outer_loop:none
  /// CHECK-DAG:               Return loop:none
  private static int closedFeed() {
    int closed = 0;
    for (int i = 0; i < 10; i++) {
      closed++;
    }
    // Closed form of first loop feeds into initial value of second loop,
    // used when generating closed form for the latter.
    for (int i = 0; i < 10; i++) {
      closed++;
    }
    return closed;
  }

  /// CHECK-START: int Main.closedLargeUp() loop_optimization (before)
  /// CHECK-DAG: <<Phi1:i\d+>> Phi               loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>> Phi               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               Return [<<Phi1>>] loop:none
  //
  /// CHECK-START: int Main.closedLargeUp() loop_optimization (after)
  /// CHECK-NOT:               Phi    loop:B\d+ outer_loop:none
  /// CHECK-DAG:               Return loop:none
  private static int closedLargeUp() {
    int closed = 0;
    for (int i = 0; i < 10; i++) {
      closed += 0x7fffffff;
    }
    return closed;
  }

  /// CHECK-START: int Main.closedLargeDown() loop_optimization (before)
  /// CHECK-DAG: <<Phi1:i\d+>> Phi               loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: <<Phi2:i\d+>> Phi               loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG:               Return [<<Phi1>>] loop:none
  //
  /// CHECK-START: int Main.closedLargeDown() loop_optimization (after)
  /// CHECK-NOT:               Phi    loop:B\d+ outer_loop:none
  /// CHECK-DAG:               Return loop:none
  private static int closedLargeDown() {
    int closed = 0;
    for (int i = 0; i < 10; i++) {
      closed -= 0x7fffffff;
    }
    return closed;
  }

  private static int exceptionExitBeforeAdd() {
    int k = 0;
    try {
      for (int i = 0; i < 10; i++) {
        a[i] = 0;
        k += 10;  // increment last
      }
    } catch(Exception e) {
      // Flag error by returning current
      // value of k negated.
      return -k-1;
    }
    return k;
  }

  private static int exceptionExitAfterAdd() {
    int k = 0;
    try {
      for (int i = 0; i < 10; i++) {
        k += 10;  // increment first
        a[i] = 0;
      }
    } catch(Exception e) {
      // Flag error by returning current
      // value of k negated.
      return -k-1;
    }
    return k;
  }

  public static void main(String[] args) {
    deadSingleLoop();
    deadNestedLoops();
    deadNestedAndFollowingLoops();

    deadInduction();
    for (int i = 0; i < a.length; i++) {
      expectEquals(1, a[i]);
    }
    deadManyInduction();
    for (int i = 0; i < a.length; i++) {
      expectEquals(2, a[i]);
    }
    deadSequence();
    for (int i = 0; i < a.length; i++) {
      expectEquals(3, a[i]);
    }
    try {
      deadCycleWithException(-1);
      throw new Error("Expected: IOOB exception");
    } catch (IndexOutOfBoundsException e) {
    }
    for (int i = 0; i < a.length; i++) {
      expectEquals(i == 0 ? 4 : 3, a[i]);
    }
    deadCycleWithException(0);
    for (int i = 0; i < a.length; i++) {
      expectEquals(4, a[i]);
    }

    int c = closedFormInductionUp();
    expectEquals(12395, c);
    c = closedFormInductionInAndDown(12345);
    expectEquals(12295, c);
    for (int n = -4; n < 10; n++) {
      int tc = (n <= 0) ? 0 : n;
      c = closedFormInductionUpN(n);
      expectEquals(12345 + tc * 5, c);
      c = closedFormInductionInAndDownN(12345, n);
      expectEquals(12345 - tc * 5, c);
      c = closedFormNested(n);
      expectEquals(tc * 10, c);
    }

    for (int n = -4; n < 4; n++) {
      int tc = (n <= 0) ? 0 : n;
      expectEquals(tc, mainIndexReturned(n));
      expectEquals(tc & 1, periodicReturned(n));
      expectEquals((tc * (tc + 1)) / 2, getSum(n));
    }
    expectEquals(21, getSum21());
    expectEquals(10, closedTwice());
    expectEquals(20, closedFeed());
    expectEquals(-10, closedLargeUp());
    expectEquals(10, closedLargeDown());

    expectEquals(100, exceptionExitBeforeAdd());
    expectEquals(100, exceptionExitAfterAdd());
    a = null;
    expectEquals(-1, exceptionExitBeforeAdd());
    expectEquals(-11, exceptionExitAfterAdd());
    a = new int[4];
    expectEquals(-41, exceptionExitBeforeAdd());
    expectEquals(-51, exceptionExitAfterAdd());

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
