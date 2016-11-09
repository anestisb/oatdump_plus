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
 * Tests properties of some string operations represented by intrinsics.
 */
public class Main {

  static final String ABC = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static final String XYZ = "XYZ";

  //
  // Variant intrinsics remain in the loop, but invariant references are hoisted out of the loop.
  //
  /// CHECK-START: int Main.liveIndexOf() licm (before)
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOf            loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOfAfter       loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOf      loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOfAfter loop:{{B\d+}} outer_loop:none
  //
  /// CHECK-START: int Main.liveIndexOf() licm (after)
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOf            loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOfAfter       loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOf      loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOfAfter loop:none
  static int liveIndexOf() {
    int k = ABC.length() + XYZ.length();  // does LoadString before loops
    for (char c = 'A'; c <= 'Z'; c++) {
      k += ABC.indexOf(c);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      k += ABC.indexOf(c, 4);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      k += ABC.indexOf(XYZ);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      k += ABC.indexOf(XYZ, 2);
    }
    return k;
  }

  //
  // All dead intrinsics can be removed completely.
  //
  /// CHECK-START: int Main.deadIndexOf() dead_code_elimination$initial (before)
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOf            loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringIndexOfAfter       loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOf      loop:{{B\d+}} outer_loop:none
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOfAfter loop:{{B\d+}} outer_loop:none
  //
  /// CHECK-START: int Main.deadIndexOf() dead_code_elimination$initial (after)
  /// CHECK-NOT: InvokeVirtual intrinsic:StringIndexOf
  /// CHECK-NOT: InvokeVirtual intrinsic:StringIndexOfAfter
  /// CHECK-NOT: InvokeVirtual intrinsic:StringStringIndexOf
  /// CHECK-NOT: InvokeVirtual intrinsic:StringStringIndexOfAfter
  static int deadIndexOf() {
    int k = ABC.length() + XYZ.length();  // does LoadString before loops
    for (char c = 'A'; c <= 'Z'; c++) {
      int d = ABC.indexOf(c);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      int d = ABC.indexOf(c, 4);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      int d = ABC.indexOf(XYZ);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
      int d = ABC.indexOf(XYZ, 2);
    }
    return k;
  }

  //
  // Explicit null check on receiver, implicit null check on argument prevents hoisting.
  //
  /// CHECK-START: int Main.indexOfExceptions(java.lang.String, java.lang.String) licm (after)
  /// CHECK-DAG: <<String:l\d+>> NullCheck                                                         loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG:                 InvokeVirtual [<<String>>,{{l\d+}}] intrinsic:StringStringIndexOf loop:<<Loop>>      outer_loop:none
  static int indexOfExceptions(String s, String t) {
    int k = 0;
    for (char c = 'A'; c <= 'Z'; c++) {
      k += s.indexOf(t);
    }
    return k;
  }

  public static void main(String[] args) {
    expectEquals(1865, liveIndexOf());
    expectEquals(29, deadIndexOf());
    try {
      indexOfExceptions(null, XYZ);
      throw new Error("Expected: NPE");
    } catch (NullPointerException e) {
    }
    try {
      indexOfExceptions(ABC, null);
      throw new Error("Expected: NPE");
    } catch (NullPointerException e) {
    }
    expectEquals(598, indexOfExceptions(ABC, XYZ));

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
