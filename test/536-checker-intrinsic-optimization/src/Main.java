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


public class Main {
  public static boolean doThrow = false;

  public static void assertIntEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void assertBooleanEquals(boolean expected, boolean result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  public static void main(String[] args) {
    stringEqualsSame();
    stringArgumentNotNull("Foo");

    assertIntEquals(0, $opt$noinline$getStringLength(""));
    assertIntEquals(3, $opt$noinline$getStringLength("abc"));
    assertIntEquals(10, $opt$noinline$getStringLength("0123456789"));

    assertBooleanEquals(true, $opt$noinline$isStringEmpty(""));
    assertBooleanEquals(false, $opt$noinline$isStringEmpty("abc"));
    assertBooleanEquals(false, $opt$noinline$isStringEmpty("0123456789"));
  }

  /// CHECK-START: int Main.$opt$noinline$getStringLength(java.lang.String) instruction_simplifier (before)
  /// CHECK-DAG:  <<Length:i\d+>>   InvokeVirtual intrinsic:StringLength
  /// CHECK-DAG:                    Return [<<Length>>]

  /// CHECK-START: int Main.$opt$noinline$getStringLength(java.lang.String) instruction_simplifier (after)
  /// CHECK-DAG:  <<String:l\d+>>   ParameterValue
  /// CHECK-DAG:  <<NullCk:l\d+>>   NullCheck [<<String>>]
  /// CHECK-DAG:  <<Length:i\d+>>   ArrayLength [<<NullCk>>] is_string_length:true
  /// CHECK-DAG:                    Return [<<Length>>]

  /// CHECK-START: int Main.$opt$noinline$getStringLength(java.lang.String) instruction_simplifier (after)
  /// CHECK-NOT:                    InvokeVirtual intrinsic:StringLength

  static public int $opt$noinline$getStringLength(String s) {
    if (doThrow) { throw new Error(); }
    return s.length();
  }

  /// CHECK-START: boolean Main.$opt$noinline$isStringEmpty(java.lang.String) instruction_simplifier (before)
  /// CHECK-DAG:  <<IsEmpty:z\d+>>  InvokeVirtual intrinsic:StringIsEmpty
  /// CHECK-DAG:                    Return [<<IsEmpty>>]

  /// CHECK-START: boolean Main.$opt$noinline$isStringEmpty(java.lang.String) instruction_simplifier (after)
  /// CHECK-DAG:  <<String:l\d+>>   ParameterValue
  /// CHECK-DAG:  <<Const0:i\d+>>   IntConstant 0
  /// CHECK-DAG:  <<NullCk:l\d+>>   NullCheck [<<String>>]
  /// CHECK-DAG:  <<Length:i\d+>>   ArrayLength [<<NullCk>>] is_string_length:true
  /// CHECK-DAG:  <<IsEmpty:z\d+>>  Equal [<<Length>>,<<Const0>>]
  /// CHECK-DAG:                    Return [<<IsEmpty>>]

  /// CHECK-START: boolean Main.$opt$noinline$isStringEmpty(java.lang.String) instruction_simplifier (after)
  /// CHECK-NOT:                    InvokeVirtual intrinsic:StringIsEmpty

  static public boolean $opt$noinline$isStringEmpty(String s) {
    if (doThrow) { throw new Error(); }
    return s.isEmpty();
  }

  /// CHECK-START: boolean Main.stringEqualsSame() instruction_simplifier (before)
  /// CHECK:      InvokeStaticOrDirect

  /// CHECK-START: boolean Main.stringEqualsSame() register (before)
  /// CHECK:      <<Const1:i\d+>> IntConstant 1
  /// CHECK:      Return [<<Const1>>]

  /// CHECK-START: boolean Main.stringEqualsSame() register (before)
  /// CHECK-NOT:  InvokeStaticOrDirect
  public static boolean stringEqualsSame() {
    return $inline$callStringEquals("obj", "obj");
  }

  /// CHECK-START: boolean Main.stringEqualsNull() register (after)
  /// CHECK:      <<Invoke:z\d+>> InvokeVirtual
  /// CHECK:      Return [<<Invoke>>]
  public static boolean stringEqualsNull() {
    String o = (String)myObject;
    return $inline$callStringEquals(o, o);
  }

  public static boolean $inline$callStringEquals(String a, String b) {
    return a.equals(b);
  }

  /// CHECK-START-X86: boolean Main.stringArgumentNotNull(java.lang.Object) disassembly (after)
  /// CHECK:          InvokeVirtual {{.*\.equals.*}}
  /// CHECK-NOT:      test
  public static boolean stringArgumentNotNull(Object obj) {
    obj.getClass();
    return "foo".equals(obj);
  }

  // Test is very brittle as it depends on the order we emit instructions.
  /// CHECK-START-X86: boolean Main.stringArgumentIsString() disassembly (after)
  /// CHECK:      InvokeVirtual
  /// CHECK:      test
  /// CHECK:      jz/eq
  // Check that we don't try to compare the classes.
  /// CHECK-NOT:  mov
  /// CHECK:      cmp
  public static boolean stringArgumentIsString() {
    return "foo".equals(myString);
  }

  static String myString;
  static Object myObject;
}
