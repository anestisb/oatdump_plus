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

public class Main {
  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    loop();
    ensureJitCompiled(Main.class, "$noinline$doCall");
    loop();
  }

  public static void loop() {
    Main m = new Main();
    for (int i = 0; i < 5000; i++) {
      $noinline$doCall("foo");
      $noinline$doCall(m);
      if (numberOfDeoptimizations() != 0) {
        throw new Error("Unexpected deoptimizations");
      }
    }
  }

  public static boolean $noinline$doCall(Object foo) {
    return foo.equals(Main.class);
  }

  public static native int numberOfDeoptimizations();
  public static native void ensureJitCompiled(Class<?> cls, String methodName);
}
