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

import com.android.jack.annotations.CalledByInvokeCustom;
import com.android.jack.annotations.Constant;
import com.android.jack.annotations.LinkerMethodHandle;
import com.android.jack.annotations.MethodHandleKind;

import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public class TestLinkerMethodMultipleArgumentTypes {

  private static int bootstrapRunCount = 0;

  @CalledByInvokeCustom(
      invokeMethodHandle = @LinkerMethodHandle(kind = MethodHandleKind.INVOKE_STATIC,
          enclosingType = TestLinkerMethodMultipleArgumentTypes.class,
          name = "linkerMethod",
          argumentTypes = {MethodHandles.Lookup.class, String.class, MethodType.class,
                           boolean.class, byte.class, char.class, short.class, int.class,
                           float.class, double.class, String.class, Class.class, long.class}),
      methodHandleExtraArgs = {@Constant(booleanValue = true), @Constant(byteValue = 1),
                         @Constant(charValue = 'a'), @Constant(shortValue = 1024),
                         @Constant(intValue = 1), @Constant(floatValue = 11.1f),
                         @Constant(doubleValue = 2.2), @Constant(stringValue = "Hello"),
                         @Constant(classValue = TestLinkerMethodMultipleArgumentTypes.class),
                         @Constant(longValue = 123456789L)},
      name = "add",
      returnType = int.class,
      argumentTypes = {int.class, int.class})
  private static int add(int a, int b) {
    return a + b;
  }

  @SuppressWarnings("unused")
  private static CallSite linkerMethod(MethodHandles.Lookup caller, String name,
                                       MethodType methodType, boolean v1, byte v2, char v3,
                                       short v4, int v5, float v6, double v7,
                                       String v8, Class<?> v9, long v10) throws Throwable {
    System.out.println("Linking " + name + " " + methodType);
    assertTrue(v1);
    assertEquals(1, v2);
    assertEquals('a', v3);
    assertEquals(1024, v4);
    assertEquals(1, v5);
    assertEquals(11.1f, v6);
    assertEquals(2.2, v7);
    assertEquals("Hello", v8);
    assertEquals(TestLinkerMethodMultipleArgumentTypes.class, v9);
    assertEquals(123456789L, v10);
    MethodHandle mh_add =
        caller.findStatic(TestLinkerMethodMultipleArgumentTypes.class, name, methodType);
    return new ConstantCallSite(mh_add);
  }

  public int GetBootstrapRunCount() {
    return bootstrapRunCount;
  }

  public static void test(int x, int y) throws Throwable {
    assertEquals(x + y, add(x, y));
    System.out.println(x + y);
  }

  public static void assertTrue(boolean value) {
    if (!value) {
      throw new AssertionError("assertTrue value: " + value);
    }
  }

  public static void assertEquals(byte b1, byte b2) {
    if (b1 == b2) { return; }
    throw new AssertionError("assertEquals b1: " + b1 + ", b2: " + b2);
  }

  public static void assertEquals(char c1, char c2) {
    if (c1 == c2) { return; }
    throw new AssertionError("assertEquals c1: " + c1 + ", c2: " + c2);
  }

  public static void assertEquals(short s1, short s2) {
    if (s1 == s2) { return; }
    throw new AssertionError("assertEquals s1: " + s1 + ", s2: " + s2);
  }

  public static void assertEquals(int i1, int i2) {
    if (i1 == i2) { return; }
    throw new AssertionError("assertEquals i1: " + i1 + ", i2: " + i2);
  }

  public static void assertEquals(long l1, long l2) {
    if (l1 == l2) { return; }
    throw new AssertionError("assertEquals l1: " + l1 + ", l2: " + l2);
  }

  public static void assertEquals(float f1, float f2) {
    if (f1 == f2) { return; }
    throw new AssertionError("assertEquals f1: " + f1 + ", f2: " + f2);
  }

  public static void assertEquals(double d1, double d2) {
    if (d1 == d2) { return; }
    throw new AssertionError("assertEquals d1: " + d1 + ", d2: " + d2);
  }

  public static void assertEquals(Object o, Object p) {
    if (o == p) { return; }
    if (o != null && p != null && o.equals(p)) { return; }
    throw new AssertionError("assertEquals: o1: " + o + ", o2: " + p);
  }

  public static void assertEquals(String s1, String s2) {
    if (s1 == s2) {
      return;
    }

    if (s1 != null && s2 != null && s1.equals(s2)) {
      return;
    }

    throw new AssertionError("assertEquals s1: " + s1 + ", s2: " + s2);
  }
}
