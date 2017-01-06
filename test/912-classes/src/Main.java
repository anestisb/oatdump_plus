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
  }

  private static void testClassType(Class<?> c) throws Exception {
    boolean isInterface = isInterface(c);
    boolean isArray = isArrayClass(c);
    System.out.println(c.getName() + " interface=" + isInterface + " array=" + isArray);
  }

  private static native String[] getClassSignature(Class<?> c);

  private static native boolean isInterface(Class<?> c);
  private static native boolean isArrayClass(Class<?> c);
}
