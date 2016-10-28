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

import java.lang.reflect.Method;
import java.util.Arrays;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
  }

  public static void doTest() throws Exception {
    testMethod("java.lang.Object", "toString");
    testMethod("java.lang.String", "charAt", int.class);
    testMethod("java.lang.Math", "sqrt", double.class);
    testMethod("java.util.List", "add", Object.class);
  }

  private static void testMethod(String className, String methodName, Class<?>... types)
      throws Exception {
    Class<?> base = Class.forName(className);
    Method m = base.getDeclaredMethod(methodName, types);
    String[] result = getMethodName(m);
    System.out.println(Arrays.toString(result));
  }

  private static native String[] getMethodName(Method m);
}
