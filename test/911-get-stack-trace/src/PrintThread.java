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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class PrintThread {
  public static void print(String[][] stack) {
    System.out.println("---------");
    for (String[] stackElement : stack) {
      for (String part : stackElement) {
        System.out.print(' ');
        System.out.print(part);
      }
      System.out.println();
    }
  }

  public static void print(Thread t, int start, int max) {
    print(getStackTrace(t, start, max));
  }

  public static void printAll(Object[][] stacks) {
    List<String> stringified = new ArrayList<String>(stacks.length);

    for (Object[] stackInfo : stacks) {
      Thread t = (Thread)stackInfo[0];
      String name = (t != null) ? t.getName() : "null";
      String stackSerialization;
      if (name.contains("Daemon")) {
        // Do not print daemon stacks, as they're non-deterministic.
        stackSerialization = "<not printed>";
      } else if (name.startsWith("Jit thread pool worker")) {
        // Skip JIT thread pool. It may or may not be there depending on configuration.
        continue;
      } else {
        StringBuilder sb = new StringBuilder();
        for (String[] stackElement : (String[][])stackInfo[1]) {
          for (String part : stackElement) {
            sb.append(' ');
            sb.append(part);
          }
          sb.append('\n');
        }
        stackSerialization = sb.toString();
      }
      stringified.add(name + "\n" + stackSerialization);
    }

    Collections.sort(stringified);

    for (String s : stringified) {
      System.out.println("---------");
      System.out.println(s);
    }
  }

  public static native String[][] getStackTrace(Thread thread, int start, int max);
}