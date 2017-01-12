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

import java.util.Arrays;

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
  }

  private static void doTest() throws Exception {
    Thread t1 = Thread.currentThread();
    Thread t2 = getCurrentThread();

    if (t1 != t2) {
      throw new RuntimeException("Expected " + t1 + " but got " + t2);
    }
    System.out.println("currentThread OK");

    printThreadInfo(t1);
    printThreadInfo(null);

    Thread t3 = new Thread("Daemon Thread");
    t3.setDaemon(true);
    // Do not start this thread, yet.
    printThreadInfo(t3);
    // Start, and wait for it to die.
    t3.start();
    t3.join();
    Thread.currentThread().sleep(500);  // Wait a little bit.
    // Thread has died, check that we can still get info.
    printThreadInfo(t3);
  }

  private static void printThreadInfo(Thread t) {
    Object[] threadInfo = getThreadInfo(t);
    if (threadInfo == null || threadInfo.length != 5) {
      System.out.println(Arrays.toString(threadInfo));
      throw new RuntimeException("threadInfo length wrong");
    }

    System.out.println(threadInfo[0]);  // Name
    System.out.println(threadInfo[1]);  // Priority
    System.out.println(threadInfo[2]);  // Daemon
    System.out.println(threadInfo[3]);  // Threadgroup
    System.out.println(threadInfo[4] == null ? "null" : threadInfo[4].getClass());  // Context CL.
  }

  private static native Thread getCurrentThread();
  private static native Object[] getThreadInfo(Thread t);
}
