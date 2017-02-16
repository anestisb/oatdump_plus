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

  //
  // Allows combining of returned "this". Also ensures that similar looking append() calls
  // are not combined somehow through returned result.
  //
  /// CHECK-START: int Main.bufferLen2() instruction_simplifier (before)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance
  /// CHECK-DAG: <<String1:l\d+>> LoadString
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>]   intrinsic:StringBufferAppend
  /// CHECK-DAG: <<String2:l\d+>> LoadString
  /// CHECK-DAG: <<Null1:l\d+>>   NullCheck     [<<Append1>>]
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<Null1>>,<<String2>>] intrinsic:StringBufferAppend
  /// CHECK-DAG: <<Null2:l\d+>>   NullCheck     [<<Append2>>]
  /// CHECK-DAG:                  InvokeVirtual [<<Null2>>]             intrinsic:StringBufferLength
  //
  /// CHECK-START: int Main.bufferLen2() instruction_simplifier (after)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance
  /// CHECK-DAG: <<String1:l\d+>> LoadString
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>] intrinsic:StringBufferAppend
  /// CHECK-DAG: <<String2:l\d+>> LoadString
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<New>>,<<String2>>] intrinsic:StringBufferAppend
  /// CHECK-DAG:                  InvokeVirtual [<<New>>]             intrinsic:StringBufferLength
  static int bufferLen2() {
    StringBuffer s = new StringBuffer();
    return s.append("x").append("x").length();
  }

  //
  // Allows combining of returned "this". Also ensures that similar looking append() calls
  // are not combined somehow through returned result.
  //
  /// CHECK-START: int Main.builderLen2() instruction_simplifier (before)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance
  /// CHECK-DAG: <<String1:l\d+>> LoadString
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>]   intrinsic:StringBuilderAppend
  /// CHECK-DAG: <<String2:l\d+>> LoadString
  /// CHECK-DAG: <<Null2:l\d+>>   NullCheck     [<<Append1>>]
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<Null2>>,<<String2>>] intrinsic:StringBuilderAppend
  /// CHECK-DAG: <<Null3:l\d+>>   NullCheck     [<<Append2>>]
  /// CHECK-DAG:                  InvokeVirtual [<<Null3>>]             intrinsic:StringBuilderLength
  //
  /// CHECK-START: int Main.builderLen2() instruction_simplifier (after)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance
  /// CHECK-DAG: <<String1:l\d+>> LoadString
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>] intrinsic:StringBuilderAppend
  /// CHECK-DAG: <<String2:l\d+>> LoadString
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<New>>,<<String2>>] intrinsic:StringBuilderAppend
  /// CHECK-DAG:                  InvokeVirtual [<<New>>]             intrinsic:StringBuilderLength
  static int builderLen2() {
    StringBuilder s = new StringBuilder();
    return s.append("x").append("x").length();
  }

  //
  // Similar situation in a loop.
  //
  /// CHECK-START: int Main.bufferLoopAppender() instruction_simplifier (before)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance                                                         loop:none
  /// CHECK-DAG: <<String1:l\d+>> LoadString                                                          loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Null1:l\d+>>   NullCheck     [<<New>>]                                             loop:<<Loop>>
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<Null1>>,<<String1>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG: <<String2:l\d+>> LoadString                                                          loop:<<Loop>>
  /// CHECK-DAG: <<Null2:l\d+>>   NullCheck     [<<Append1>>]                                         loop:<<Loop>>
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<Null2>>,<<String2>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG: <<String3:l\d+>> LoadString                                                          loop:<<Loop>>
  /// CHECK-DAG: <<Null3:l\d+>>   NullCheck     [<<Append2>>]                                         loop:<<Loop>>
  /// CHECK-DAG: <<Append3:l\d+>> InvokeVirtual [<<Null3>>,<<String3>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG: <<Null4:l\d+>>   NullCheck     [<<New>>]                                             loop:none
  /// CHECK-DAG:                  InvokeVirtual [<<Null4>>]             intrinsic:StringBufferLength  loop:none
  //
  /// CHECK-START: int Main.bufferLoopAppender() instruction_simplifier (after)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance                                                       loop:none
  /// CHECK-DAG: <<String1:l\d+>> LoadString                                                        loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG: <<String2:l\d+>> LoadString                                                        loop:<<Loop>>
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<New>>,<<String2>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG: <<String3:l\d+>> LoadString                                                        loop:<<Loop>>
  /// CHECK-DAG: <<Append3:l\d+>> InvokeVirtual [<<New>>,<<String3>>] intrinsic:StringBufferAppend  loop:<<Loop>>
  /// CHECK-DAG:                  InvokeVirtual [<<New>>]             intrinsic:StringBufferLength  loop:none
  static int bufferLoopAppender() {
    StringBuffer b = new StringBuffer();
    for (int i = 0; i < 10; i++) {
      b.append("x").append("y").append("z");
    }
    return b.length();
  }

  //
  // Similar situation in a loop.
  //
  /// CHECK-START: int Main.builderLoopAppender() instruction_simplifier (before)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance                                                         loop:none
  /// CHECK-DAG: <<String1:l\d+>> LoadString                                                          loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Null1:l\d+>>   NullCheck     [<<New>>]                                             loop:<<Loop>>
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<Null1>>,<<String1>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG: <<String2:l\d+>> LoadString                                                          loop:<<Loop>>
  /// CHECK-DAG: <<Null2:l\d+>>   NullCheck     [<<Append1>>]                                         loop:<<Loop>>
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<Null2>>,<<String2>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG: <<String3:l\d+>> LoadString                                                          loop:<<Loop>>
  /// CHECK-DAG: <<Null3:l\d+>>   NullCheck     [<<Append2>>]                                         loop:<<Loop>>
  /// CHECK-DAG: <<Append3:l\d+>> InvokeVirtual [<<Null3>>,<<String3>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG: <<Null4:l\d+>>   NullCheck     [<<New>>]                                             loop:none
  /// CHECK-DAG:                  InvokeVirtual [<<Null4>>]             intrinsic:StringBuilderLength loop:none
  //
  /// CHECK-START: int Main.builderLoopAppender() instruction_simplifier (after)
  /// CHECK-DAG: <<New:l\d+>>     NewInstance                                                       loop:none
  /// CHECK-DAG: <<String1:l\d+>> LoadString                                                        loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Append1:l\d+>> InvokeVirtual [<<New>>,<<String1>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG: <<String2:l\d+>> LoadString                                                        loop:<<Loop>>
  /// CHECK-DAG: <<Append2:l\d+>> InvokeVirtual [<<New>>,<<String2>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG: <<String3:l\d+>> LoadString                                                        loop:<<Loop>>
  /// CHECK-DAG: <<Append3:l\d+>> InvokeVirtual [<<New>>,<<String3>>] intrinsic:StringBuilderAppend loop:<<Loop>>
  /// CHECK-DAG:                  InvokeVirtual [<<New>>]             intrinsic:StringBuilderLength loop:none
  static int builderLoopAppender() {
    StringBuilder b = new StringBuilder();
    for (int i = 0; i < 10; i++) {
      b.append("x").append("y").append("z");
    }
    return b.length();
  }

  //
  // All calls in the loop-body and thus loop can be eliminated.
  //
  /// CHECK-START: int Main.bufferDeadLoop() instruction_simplifier (before)
  /// CHECK-DAG: Phi                                              loop:<<Loop:B\d+>>
  /// CHECK-DAG: InvokeVirtual intrinsic:StringBufferToString     loop:<<Loop>>
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOfAfter loop:<<Loop>>
  //
  /// CHECK-START: int Main.bufferDeadLoop() loop_optimization (after)
  /// CHECK-NOT: Phi
  /// CHECK-NOT: InvokeVirtual intrinsic:StringBufferToString
  /// CHECK-NOT: InvokeVirtual intrinsic:StringStringIndexOfAfter
  static int bufferDeadLoop() {
    StringBuffer b = new StringBuffer();
    String x = "x";
    for (int i = 0; i < 10; i++) {
      int d = b.toString().indexOf(x, 1);
    }
    return b.length();
  }

  //
  // All calls in the loop-body and thus loop can be eliminated.
  //
  /// CHECK-START: int Main.builderDeadLoop() instruction_simplifier (before)
  /// CHECK-DAG: Phi                                              loop:<<Loop:B\d+>>
  /// CHECK-DAG: InvokeVirtual intrinsic:StringBuilderToString    loop:<<Loop>>
  /// CHECK-DAG: InvokeVirtual intrinsic:StringStringIndexOfAfter loop:<<Loop>>
  //
  /// CHECK-START: int Main.builderDeadLoop() loop_optimization (after)
  /// CHECK-NOT: Phi
  /// CHECK-NOT: InvokeVirtual intrinsic:StringBuilderToString
  /// CHECK-NOT: InvokeVirtual intrinsic:StringStringIndexOfAfter
  static int builderDeadLoop() {
    StringBuilder b = new StringBuilder();
    String x = "x";
    for (int i = 0; i < 10; i++) {
      int d = b.toString().indexOf(x, 1);
    }
    return b.length();
  }

  // Regression b/33656359: StringBuffer x is passed to constructor of String
  // (this caused old code to crash due to missing nullptr check).
  //
  /// CHECK-START: void Main.doesNothing() instruction_simplifier (before)
  /// CHECK-DAG: InvokeVirtual intrinsic:StringBufferToString
  //
  /// CHECK-START: void Main.doesNothing() instruction_simplifier (after)
  /// CHECK-DAG: InvokeVirtual intrinsic:StringBufferToString
  static void doesNothing() {
    StringBuffer x = new StringBuffer();
    String y = new String(x);
    x.toString();
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

    expectEquals(2, bufferLen2());
    expectEquals(2, builderLen2());
    expectEquals(30, bufferLoopAppender());
    expectEquals(30, builderLoopAppender());
    expectEquals(0, bufferDeadLoop());
    expectEquals(0, builderDeadLoop());

    doesNothing();

    System.out.println("passed");
  }

  private static void expectEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }
}
