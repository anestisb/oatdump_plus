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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;
import java.lang.invoke.MethodType;
import java.lang.invoke.WrongMethodTypeException;

public class Main {
  public static void main(String[] args) throws Throwable {
    testThrowException();
    testDropArguments();
    testCatchException();
    testGuardWithTest();
  }

  public static void testThrowException() throws Throwable {
    MethodHandle handle = MethodHandles.throwException(String.class,
        IllegalArgumentException.class);

    if (handle.type().returnType() != String.class) {
      System.out.println("Unexpected return type for handle: " + handle +
          " [ " + handle.type() + "]");
    }

    final IllegalArgumentException iae = new IllegalArgumentException("boo!");
    try {
      handle.invoke(iae);
      System.out.println("Expected an exception of type: java.lang.IllegalArgumentException");
    } catch (IllegalArgumentException expected) {
      if (expected != iae) {
        System.out.println("Wrong exception: expected " + iae + " but was " + expected);
      }
    }
  }

  public static void dropArguments_delegate(String message, long message2) {
    System.out.println("Message: " + message + ", Message2: " + message2);
  }

  public static void testDropArguments() throws Throwable {
    MethodHandle delegate = MethodHandles.lookup().findStatic(Main.class,
        "dropArguments_delegate",
        MethodType.methodType(void.class, new Class<?>[] { String.class, long.class }));

    MethodHandle transform = MethodHandles.dropArguments(delegate, 0, int.class, Object.class);

    // The transformer will accept two additional arguments at position zero.
    try {
      transform.invokeExact("foo", 42l);
      fail();
    } catch (WrongMethodTypeException expected) {
    }

    transform.invokeExact(45, new Object(), "foo", 42l);
    transform.invoke(45, new Object(), "foo", 42l);

    // Additional arguments at position 1.
    transform = MethodHandles.dropArguments(delegate, 1, int.class, Object.class);
    transform.invokeExact("foo", 45, new Object(), 42l);
    transform.invoke("foo", 45, new Object(), 42l);

    // Additional arguments at position 2.
    transform = MethodHandles.dropArguments(delegate, 2, int.class, Object.class);
    transform.invokeExact("foo", 42l, 45, new Object());
    transform.invoke("foo", 42l, 45, new Object());

    // Note that we still perform argument conversions even for the arguments that
    // are subsequently dropped.
    try {
      transform.invoke("foo", 42l, 45l, new Object());
      fail();
    } catch (WrongMethodTypeException expected) {
    } catch (IllegalArgumentException expected) {
      // TODO(narayan): We currently throw the wrong type of exception here,
      // it's IAE and should be WMTE instead.
    }

    // Invalid argument location, should not be allowed.
    try {
      MethodHandles.dropArguments(delegate, -1, int.class, Object.class);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Invalid argument location, should not be allowed.
    try {
      MethodHandles.dropArguments(delegate, 3, int.class, Object.class);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    try {
      MethodHandles.dropArguments(delegate, 1, void.class);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  public static String testCatchException_target(String arg1, long arg2, String exceptionMessage)
      throws Throwable {
    if (exceptionMessage != null) {
      throw new IllegalArgumentException(exceptionMessage);
    }

    System.out.println("Target: Arg1: " + arg1 + ", Arg2: " + arg2);
    return "target";
  }

  public static String testCatchException_handler(IllegalArgumentException iae, String arg1, long arg2,
      String exMsg) {
    System.out.println("Handler: " + iae + ", Arg1: " + arg1 + ", Arg2: " + arg2 + ", ExMsg: " + exMsg);
    return "handler1";
  }

  public static String testCatchException_handler2(IllegalArgumentException iae, String arg1) {
    System.out.println("Handler: " + iae + ", Arg1: " + arg1);
    return "handler2";
  }

  public static void testCatchException() throws Throwable {
    MethodHandle target = MethodHandles.lookup().findStatic(Main.class,
        "testCatchException_target",
        MethodType.methodType(String.class, new Class<?>[] { String.class, long.class, String.class }));

    MethodHandle handler = MethodHandles.lookup().findStatic(Main.class,
        "testCatchException_handler",
        MethodType.methodType(String.class, new Class<?>[] { IllegalArgumentException.class,
            String.class, long.class, String.class }));

    MethodHandle adapter = MethodHandles.catchException(target, IllegalArgumentException.class,
        handler);

    String returnVal = null;

    // These two should end up calling the target always. We're passing a null exception
    // message here, which means the target will not throw.
    returnVal = (String) adapter.invoke("foo", 42, null);
    assertEquals("target", returnVal);
    returnVal = (String) adapter.invokeExact("foo", 42l, (String) null);
    assertEquals("target", returnVal);

    // We're passing a non-null exception message here, which means the target will throw,
    // which in turn means that the handler must be called for the next two invokes.
    returnVal = (String) adapter.invoke("foo", 42, "exceptionMessage");
    assertEquals("handler1", returnVal);
    returnVal = (String) adapter.invokeExact("foo", 42l, "exceptionMessage");
    assertEquals("handler1", returnVal);

    handler = MethodHandles.lookup().findStatic(Main.class,
        "testCatchException_handler2",
        MethodType.methodType(String.class, new Class<?>[] { IllegalArgumentException.class,
            String.class }));
    adapter = MethodHandles.catchException(target, IllegalArgumentException.class, handler);

    returnVal = (String) adapter.invoke("foo", 42, "exceptionMessage");
    assertEquals("handler2", returnVal);
    returnVal = (String) adapter.invokeExact("foo", 42l, "exceptionMessage");
    assertEquals("handler2", returnVal);

    // Test that the type of the invoke doesn't matter. Here we call
    // IllegalArgumentException.toString() on the exception that was thrown by
    // the target.
    handler = MethodHandles.lookup().findVirtual(IllegalArgumentException.class,
        "toString", MethodType.methodType(String.class));
    adapter = MethodHandles.catchException(target, IllegalArgumentException.class, handler);

    returnVal = (String) adapter.invoke("foo", 42, "exceptionMessage");
    assertEquals("java.lang.IllegalArgumentException: exceptionMessage", returnVal);
    returnVal = (String) adapter.invokeExact("foo", 42l, "exceptionMessage2");
    assertEquals("java.lang.IllegalArgumentException: exceptionMessage2", returnVal);
  }

  public static boolean testGuardWithTest_test(String arg1, long arg2) {
    return "target".equals(arg1) && 42 == arg2;
  }

  public static String testGuardWithTest_target(String arg1, long arg2, int arg3) {
    System.out.println("target: " + arg1 + ", " + arg2  + ", " + arg3);
    return "target";
  }

  public static String testGuardWithTest_fallback(String arg1, long arg2, int arg3) {
    System.out.println("fallback: " + arg1 + ", " + arg2  + ", " + arg3);
    return "fallback";
  }

  public static void testGuardWithTest() throws Throwable {
    MethodHandle test = MethodHandles.lookup().findStatic(Main.class,
        "testGuardWithTest_test",
        MethodType.methodType(boolean.class, new Class<?>[] { String.class, long.class }));

    final MethodType type = MethodType.methodType(String.class,
        new Class<?>[] { String.class, long.class, int.class });

    final MethodHandle target = MethodHandles.lookup().findStatic(Main.class,
        "testGuardWithTest_target", type);
    final MethodHandle fallback = MethodHandles.lookup().findStatic(Main.class,
        "testGuardWithTest_fallback", type);

    MethodHandle adapter = MethodHandles.guardWithTest(test, target, fallback);

    String returnVal = null;

    returnVal = (String) adapter.invoke("target", 42, 56);
    assertEquals("target", returnVal);
    returnVal = (String) adapter.invokeExact("target", 42l, 56);
    assertEquals("target", returnVal);

    returnVal = (String) adapter.invoke("fallback", 42l, 56);
    assertEquals("fallback", returnVal);
    returnVal = (String) adapter.invokeExact("target", 42l, 56);
    assertEquals("target", returnVal);
  }

  public static void fail() {
    System.out.println("FAIL");
    Thread.dumpStack();
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


