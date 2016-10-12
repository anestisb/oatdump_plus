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

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
  }

  public static void doTest() throws Exception {
    // Use a list to ensure objects must be allocated.
    ArrayList<Object> l = new ArrayList<>(100);

    setupObjectFreeCallback();

    enableFreeTracking(true);
    run(l);

    enableFreeTracking(false);
    run(l);
  }

  private static void run(ArrayList<Object> l) {
    allocate(l, 1);
    l.clear();

    Runtime.getRuntime().gc();

    System.out.println("---");

    // Note: the reporting will not depend on the heap layout (which could be unstable). Walking
    //       the tag table should give us a stable output order.
    for (int i = 10; i <= 1000; i *= 10) {
      allocate(l, i);
    }
    l.clear();

    Runtime.getRuntime().gc();

    System.out.println("---");

    Runtime.getRuntime().gc();

    System.out.println("---");
  }

  private static void allocate(ArrayList<Object> l, long tag) {
    Object obj = new Object();
    l.add(obj);
    setTag(obj, tag);
  }

  private static native void setupObjectFreeCallback();
  private static native void enableFreeTracking(boolean enable);
  private static native void setTag(Object o, long tag);
}
