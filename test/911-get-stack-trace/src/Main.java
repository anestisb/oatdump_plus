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
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
    doTestOtherThreadWait();
    doTestOtherThreadBusyLoop();

    doTestAllStackTraces();

    System.out.println("Done");
  }

  public static void doTest() throws Exception {
    System.out.println("###################");
    System.out.println("### Same thread ###");
    System.out.println("###################");
    System.out.println("From top");
    Recurse.foo(4, 0, 25, null);
    Recurse.foo(4, 1, 25, null);
    Recurse.foo(4, 0, 5, null);
    Recurse.foo(4, 2, 5, null);

    System.out.println("From bottom");
    Recurse.foo(4, -1, 25, null);
    Recurse.foo(4, -5, 5, null);
    Recurse.foo(4, -7, 5, null);
  }

  public static void doTestOtherThreadWait() throws Exception {
    System.out.println();
    System.out.println("################################");
    System.out.println("### Other thread (suspended) ###");
    System.out.println("################################");
    final ControlData data = new ControlData();
    data.waitFor = new Object();
    Thread t = new Thread() {
      public void run() {
        Recurse.foo(4, 0, 0, data);
      }
    };
    t.start();
    data.reached.await();
    Thread.yield();
    Thread.sleep(500);  // A little bit of time...

    System.out.println("From top");
    print(t, 0, 25);
    print(t, 1, 25);
    print(t, 0, 5);
    print(t, 2, 5);

    System.out.println("From bottom");
    print(t, -1, 25);
    print(t, -5, 5);
    print(t, -7, 5);

    // Let the thread make progress and die.
    synchronized(data.waitFor) {
      data.waitFor.notifyAll();
    }
    t.join();
  }

  public static void doTestOtherThreadBusyLoop() throws Exception {
    System.out.println();
    System.out.println("###########################");
    System.out.println("### Other thread (live) ###");
    System.out.println("###########################");
    final ControlData data = new ControlData();
    Thread t = new Thread() {
      public void run() {
        Recurse.foo(4, 0, 0, data);
      }
    };
    t.start();
    data.reached.await();
    Thread.yield();
    Thread.sleep(500);  // A little bit of time...

    System.out.println("From top");
    print(t, 0, 25);
    print(t, 1, 25);
    print(t, 0, 5);
    print(t, 2, 5);

    System.out.println("From bottom");
    print(t, -1, 25);
    print(t, -5, 5);
    print(t, -7, 5);

    // Let the thread stop looping and die.
    data.stop = true;
    t.join();
  }

  public static void doTestAllStackTraces() throws Exception {
    System.out.println();
    System.out.println("################################");
    System.out.println("### Other threads (suspended) ###");
    System.out.println("################################");

    final int N = 10;

    final ControlData data = new ControlData(N);
    data.waitFor = new Object();

    Thread threads[] = new Thread[N];

    for (int i = 0; i < N; i++) {
      Thread t = new Thread() {
        public void run() {
          Recurse.foo(4, 0, 0, data);
        }
      };
      t.start();
      threads[i] = t;
    }
    data.reached.await();
    Thread.yield();
    Thread.sleep(500);  // A little bit of time...

    printAll(0);

    printAll(5);

    printAll(25);

    // Let the thread make progress and die.
    synchronized(data.waitFor) {
      data.waitFor.notifyAll();
    }
    for (int i = 0; i < N; i++) {
      threads[i].join();
    }
  }

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

  public static void printAll(int max) {
    printAll(getAllStackTraces(max));
  }

  // Wrap generated stack traces into a class to separate them nicely.
  public static class Recurse {

    public static int foo(int x, int start, int max, ControlData data) {
      bar(x, start, max, data);
      return 0;
    }

    private static long bar(int x, int start, int max, ControlData data) {
      baz(x, start, max, data);
      return 0;
    }

    private static Object baz(int x, int start, int max, ControlData data) {
      if (x == 0) {
        printOrWait(start, max, data);
      } else {
        foo(x - 1, start, max, data);
      }
      return null;
    }

    private static void printOrWait(int start, int max, ControlData data) {
      if (data == null) {
        print(Thread.currentThread(), start, max);
      } else {
        if (data.waitFor != null) {
          synchronized (data.waitFor) {
            data.reached.countDown();
            try {
              data.waitFor.wait();  // Use wait() as it doesn't have a "hidden" Java call-graph.
            } catch (Throwable t) {
              throw new RuntimeException(t);
            }
          }
        } else {
          data.reached.countDown();
          while (!data.stop) {
            // Busy-loop.
          }
        }
      }
    }
  }

  public static class ControlData {
    CountDownLatch reached;
    Object waitFor = null;
    volatile boolean stop = false;

    public ControlData() {
      this(1);
    }

    public ControlData(int latchCount) {
      reached = new CountDownLatch(latchCount);
    }
  }

  public static native String[][] getStackTrace(Thread thread, int start, int max);
  // Get all stack traces. This will return an array with an element for each thread. The element
  // is an array itself with the first element being the thread, and the second element a nested
  // String array as in getStackTrace.
  public static native Object[][] getAllStackTraces(int max);
}
