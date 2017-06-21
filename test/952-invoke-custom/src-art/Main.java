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

import dalvik.system.InMemoryDexClassLoader;

import java.lang.invoke.CallSite;
import java.lang.invoke.MethodType;
import java.lang.invoke.MutableCallSite;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.Base64;

// This test is a stop-gap until we have support for generating invoke-custom
// in the Android tree.

public class Main {

  private static void TestUninitializedCallSite() throws Throwable {
    CallSite callSite = new MutableCallSite(MethodType.methodType(int.class));
    try {
      callSite.getTarget().invoke();
      fail();
    } catch (IllegalStateException e) {
      System.out.println("Caught exception from uninitialized call site");
    }

    callSite = new MutableCallSite(MethodType.methodType(String.class, int.class, char.class));
    try {
      callSite.getTarget().invoke(1535, 'd');
      fail();
    } catch (IllegalStateException e) {
      System.out.println("Caught exception from uninitialized call site");
    }
  }

  private static void TestLinkerMethodMultipleArgumentTypes() throws Throwable {
    // This is a more comprehensive test of invoke-custom, the linker
    // method takes additional arguments of types boolean, byte, char,
    // short, int, float, double, String, Class, and long (in this order)
    // The test asserts the values passed to the linker method match their
    // expected values.
    byte[] base64Data = TestDataLinkerMethodMultipleArgumentTypes.BASE64_DEX_FILE.getBytes();
    Base64.Decoder decoder = Base64.getDecoder();
    ByteBuffer dexBuffer = ByteBuffer.wrap(decoder.decode(base64Data));

    InMemoryDexClassLoader classLoader =
        new InMemoryDexClassLoader(dexBuffer,
                                   ClassLoader.getSystemClassLoader());
    Class<?> testClass =
        classLoader.loadClass("TestLinkerMethodMultipleArgumentTypes");
    Method testMethod = testClass.getDeclaredMethod("test", int.class, int.class);
    // First invocation should link via the bootstrap method (outputs "Linking add" ...).
    testMethod.invoke(null, 33, 67);
    // Subsequent invocations use the cached value of the CallSite and do not require linking.
    testMethod.invoke(null, -10000, +1000);
    testMethod.invoke(null, -1000, +10000);
  }

  private static void TestLinkerMethodMinimalArguments() throws Throwable {
    // This test checks various failures when running the linker
    // method and during invocation of the method handle.
    byte[] base64Data = TestDataLinkerMethodMinimalArguments.BASE64_DEX_FILE.getBytes();
    Base64.Decoder decoder = Base64.getDecoder();
    ByteBuffer dexBuffer = ByteBuffer.wrap(decoder.decode(base64Data));

    InMemoryDexClassLoader classLoader =
        new InMemoryDexClassLoader(dexBuffer,
                                   ClassLoader.getSystemClassLoader());
    Class<?> testClass =
        classLoader.loadClass("TestLinkerMethodMinimalArguments");
    Method testMethod = testClass.getDeclaredMethod("test", int.class, int.class, int.class);

    try {
      testMethod.invoke(null, 1 /* linker method return null */, 10, 10);
    } catch (InvocationTargetException e) {
      assertEquals(e.getCause().getClass().getName(), "java.lang.BootstrapMethodError");
      assertEquals(
          e.getCause().getCause().getClass().getName(), "java.lang.NullPointerException");
    }

    try {
      testMethod.invoke(null, 2 /* linker method throw InstantiationException */, 10, 11);
    } catch (InvocationTargetException e) {
      assertEquals(e.getCause().getClass().getName(), "java.lang.BootstrapMethodError");
      assertEquals(
          e.getCause().getCause().getClass().getName(), "java.lang.InstantiationException");
    }
    try {
      // Creating the CallSite works here, but fail invoking the method.
      testMethod.invoke(null, 3 /* target throw NPE */, 10, 12);
    } catch (InvocationTargetException e) {
      assertEquals(e.getCause().getClass().getName(), "java.lang.ArithmeticException");
    }

    // This should succeed using already resolved CallSite.
    testMethod.invoke(null, 0 /* no error */, 10, 13);
  }

  private static void TestInvokeCustomWithConcurrentThreads() throws Throwable {
    // This is a concurrency test that attempts to run invoke-custom on the same
    // call site.
    byte[] base64Data = TestDataInvokeCustomWithConcurrentThreads.BASE64_DEX_FILE.getBytes();
    Base64.Decoder decoder = Base64.getDecoder();
    ByteBuffer dexBuffer = ByteBuffer.wrap(decoder.decode(base64Data));

    InMemoryDexClassLoader classLoader =
        new InMemoryDexClassLoader(dexBuffer,
                                   ClassLoader.getSystemClassLoader());
    Class<?> testClass =
        classLoader.loadClass("TestInvokeCustomWithConcurrentThreads");
    Method testMethod = testClass.getDeclaredMethod("test");
    testMethod.invoke(null);
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

  private static void fail() {
    System.out.println("fail");
    Thread.dumpStack();
  }

  public static void main(String[] args) throws Throwable {
    TestUninitializedCallSite();
    TestLinkerMethodMinimalArguments();
    TestLinkerMethodMultipleArgumentTypes();
    TestInvokeCustomWithConcurrentThreads();
  }
}