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

package art;

import java.util.Base64;

public class Test992 {

  static class Target1 { }

  public static void run() {
    doTest(Test992.class);
    doTest(Target1.class);
    doTest(Target2.class);
    doTest(Integer.TYPE);
    doTest(Integer.class);
    doTest(Object.class);
    doTest(Runnable.class);
    doTest(new Object[0].getClass());
    doTest(new int[0].getClass());
    doTest(null);
  }

  public static void doTest(Class<?> k) {
    try {
      System.out.println(k + " is defined in file \"" + getSourceFileName(k) + "\"");
    } catch (Exception e) {
      System.out.println(k + " does not have a known source file because " + e);
    }
  }

  public static native String getSourceFileName(Class<?> k) throws Exception;
}
