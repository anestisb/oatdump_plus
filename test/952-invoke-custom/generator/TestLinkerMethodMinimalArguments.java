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

import com.android.jack.annotations.CalledByInvokeCustom;
import com.android.jack.annotations.Constant;
import com.android.jack.annotations.LinkerMethodHandle;
import com.android.jack.annotations.MethodHandleKind;

import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public class TestLinkerMethodMinimalArguments {

  private static int forceFailureType = 0;

  private static int FAILURE_TYPE_NONE = 0;
  private static int FAILURE_TYPE_LINKER_METHOD_RETURNS_NULL = 1;
  private static int FAILURE_TYPE_LINKER_METHOD_THROWS = 2;
  private static int FAILURE_TYPE_TARGET_METHOD_THROWS = 3;

  @CalledByInvokeCustom(
      invokeMethodHandle = @LinkerMethodHandle(
          kind = MethodHandleKind.INVOKE_STATIC,
          enclosingType = TestLinkerMethodMinimalArguments.class,
          argumentTypes = {MethodHandles.Lookup.class, String.class, MethodType.class},
          name = "linkerMethod"),
      name = "add",
      returnType = int.class,
      argumentTypes = {int.class, int.class})
  private static int add(int a, int b) {
    if (forceFailureType == FAILURE_TYPE_TARGET_METHOD_THROWS) {
      System.out.println("Throwing ArithmeticException in add()");
      throw new ArithmeticException("add");
    }
    return a + b;
  }

  @SuppressWarnings("unused")
  private static CallSite linkerMethod(MethodHandles.Lookup caller, String name,
                                       MethodType methodType) throws Throwable {
    System.out.println("linkerMethod failure type " + forceFailureType);
    MethodHandle mh_add =
        caller.findStatic(TestLinkerMethodMinimalArguments.class, name, methodType);
    if (forceFailureType == FAILURE_TYPE_LINKER_METHOD_RETURNS_NULL) {
      System.out.println("Returning null instead of CallSite for " + name + " " + methodType);
      return null;
    } else if (forceFailureType == FAILURE_TYPE_LINKER_METHOD_THROWS) {
      System.out.println("Throwing InstantiationException in linkerMethod()");
      throw new InstantiationException("linkerMethod");
    } else {
      return new ConstantCallSite(mh_add);
    }
  }

  public static void test(int failureType, int x, int y) throws Throwable {
    assertTrue(failureType >= FAILURE_TYPE_NONE);
    assertTrue(failureType <= FAILURE_TYPE_TARGET_METHOD_THROWS);
    forceFailureType = failureType;
    assertEquals(x + y, add(x, y));
    System.out.println("Failure Type + " + failureType + " (" + x + y+ ")");
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
