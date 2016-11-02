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
    setupGcCallback();

    enableGcTracking(true);
    run();
    enableGcTracking(false);
  }

  private static void run() {
    clearStats();
    forceGarbageCollection();
    printStats();
  }

  private static void clearStats() {
    getGcStarts();
    getGcFinishes();
  }

  private static void printStats() {
      System.out.println("---");
      int s = getGcStarts();
      int f = getGcFinishes();
      System.out.println((s > 0) + " " + (f > 0));
  }

  private static native void setupGcCallback();
  private static native void enableGcTracking(boolean enable);
  private static native int getGcStarts();
  private static native int getGcFinishes();
  private static native void forceGarbageCollection();
}
