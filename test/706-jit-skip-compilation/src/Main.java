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

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Class<?> c = Class.forName("ErrClass");
    Method m = c.getMethod("errMethod");

    // Print the counter before invokes. The golden file expects this to be 0.
    int hotnessCounter = getHotnessCounter(c, "errMethod");
    if (hotnessCounter != 0) {
      throw new RuntimeException("Unexpected hotnessCounter: " + hotnessCounter);
    }

    // Loop enough to make sure the interpreter reports invocations count.
    long result = 0;
    for (int i = 0; i < 10000; i++) {
      try {
        result += (Long)m.invoke(null);
        hotnessCounter = getHotnessCounter(c, "errMethod");
        if (hotnessCounter != 0) {
          throw new RuntimeException(
            "Unexpected hotnessCounter: " + hotnessCounter);
        }

      } catch (InvocationTargetException e) {
        if (!(e.getCause() instanceof NullPointerException)) {
          throw e;
        }
      }
    }

    // Not compilable methods should not increase their hotness counter.
    if (hotnessCounter != 0) {
      throw new RuntimeException("Unexpected hotnessCounter: " + hotnessCounter);
    }
  }

  public static native int getHotnessCounter(Class cls, String method_name);
}
