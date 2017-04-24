/*
 * Copyright (C) 2011 The Android Open Source Project
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

public class Test901 {
  public static void run() {
    System.out.println("Hello, world!");

    if (checkLivePhase()) {
      System.out.println("Agent in live phase.");
    }
    if (checkUnattached()) {
      System.out.println("Received expected error for unattached JVMTI calls");
    }

    set(0);  // OTHER
    set(1);  // GC
    set(2);  // CLASS
    set(4);  // JNI
    set(8);  // Error.
  }

  private static void set(int i) {
    System.out.println(i);
    try {
      setVerboseFlag(i, true);
      setVerboseFlag(i, false);
    } catch (RuntimeException e) {
      System.out.println(e.getMessage());
    }
  }

  private static native boolean checkLivePhase();
  private static native void setVerboseFlag(int flag, boolean value);
  private static native boolean checkUnattached();
}
