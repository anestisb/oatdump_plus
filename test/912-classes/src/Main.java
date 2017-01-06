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

import java.lang.reflect.Proxy;
import java.util.Arrays;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
  }

  public static void doTest() throws Exception {
    testClass("java.lang.Object");
    testClass("java.lang.String");
    testClass("java.lang.Math");
    testClass("java.util.List");

    testClass(getProxyClass());

    testClass(int.class);
    testClass(double[].class);

    testClassType(int.class);
    testClassType(getProxyClass());
    testClassType(Runnable.class);
    testClassType(String.class);

    testClassType(int[].class);
    testClassType(Runnable[].class);
    testClassType(String[].class);

    testClassFields(Integer.class);
    testClassFields(int.class);
    testClassFields(String[].class);

    testClassMethods(Integer.class);
    testClassMethods(int.class);
    testClassMethods(String[].class);

    testClassStatus(int.class);
    testClassStatus(String[].class);
    testClassStatus(Object.class);
    testClassStatus(TestForNonInit.class);
    try {
      System.out.println(TestForInitFail.dummy);
    } catch (ExceptionInInitializerError e) {
    }
    testClassStatus(TestForInitFail.class);
  }

  private static Class<?> proxyClass = null;

  private static Class<?> getProxyClass() throws Exception {
    if (proxyClass != null) {
      return proxyClass;
    }

    proxyClass = Proxy.getProxyClass(Main.class.getClassLoader(), new Class[] { Runnable.class });
    return proxyClass;
  }

  private static void testClass(String className) throws Exception {
    Class<?> base = Class.forName(className);
    testClass(base);
  }

  private static void testClass(Class<?> base) throws Exception {
    String[] result = getClassSignature(base);
    System.out.println(Arrays.toString(result));
    int mod = getClassModifiers(base);
    if (mod != base.getModifiers()) {
      throw new RuntimeException("Unexpected modifiers: " + base.getModifiers() + " vs " + mod);
    }
    System.out.println(Integer.toHexString(mod));
  }

  private static void testClassType(Class<?> c) throws Exception {
    boolean isInterface = isInterface(c);
    boolean isArray = isArrayClass(c);
    System.out.println(c.getName() + " interface=" + isInterface + " array=" + isArray);
  }

  private static void testClassFields(Class<?> c) throws Exception {
    System.out.println(Arrays.toString(getClassFields(c)));
  }

  private static void testClassMethods(Class<?> c) throws Exception {
    System.out.println(Arrays.toString(getClassMethods(c)));
  }

  private static void testClassStatus(Class<?> c) {
    System.out.println(c + " " + Integer.toBinaryString(getClassStatus(c)));
  }

  private static native String[] getClassSignature(Class<?> c);

  private static native boolean isInterface(Class<?> c);
  private static native boolean isArrayClass(Class<?> c);

  private static native int getClassModifiers(Class<?> c);

  private static native Object[] getClassFields(Class<?> c);
  private static native Object[] getClassMethods(Class<?> c);

  private static native int getClassStatus(Class<?> c);

  private static class TestForNonInit {
    public static double dummy = Math.random();  // So it can't be compile-time initialized.
  }

  private static class TestForInitFail {
    public static int dummy = ((int)Math.random())/0;  // So it throws when initializing.
  }
}
