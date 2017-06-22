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
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    $noinline$intUpdate(new Main());
    ensureJitCompiled(Main.class, "$noinline$intUpdate");
    $noinline$intUpdate(new SubMain());
    if (myIntStatic != 5000) {
      throw new Error("Expected 5000, got " + myIntStatic);
    }

    $noinline$objectUpdate(new Main());
    ensureJitCompiled(Main.class, "$noinline$objectUpdate");
    $noinline$objectUpdate(new SubMain());

    $noinline$loopIncrement(new Main());
    ensureJitCompiled(Main.class, "$noinline$loopIncrement");
    $noinline$loopIncrement(new SubMain());
  }

  public boolean doCheck() {
    return false;
  }

  public static void $noinline$intUpdate(Main m) {
    int a = 0;
    // We used to kill 'a' when the inline cache of 'doCheck' only
    // contains 'Main' (which makes the only branch using 'a' dead).
    // So the deoptimization at the inline cache was incorrectly assuming
    // 'a' was dead.
    for (int i = 0; i < 5000; i++) {
      if (m.doCheck()) {
        a++;
        // We make this branch the only true user of the 'a' phi. All other uses
        // of 'a' are phi updates.
        myIntStatic = a;
      } else if (myIntStatic == 42) {
        a = 1;
      }
    }
  }

  public static void $noinline$objectUpdate(Main m) {
    Object o = new Object();
    // We used to kill 'o' when the inline cache of 'doCheck' only
    // contains 'Main' (which makes the only branch using 'a' dead).
    // So the deoptimization at the inline cache was incorrectly assuming
    // 'o' was dead.
    // This lead to a NPE on the 'toString' call just after deoptimizing.
    for (int i = 0; i < 5000; i++) {
      if (m.doCheck()) {
        // We make this branch the only true user of the 'o' phi. All other uses
        // of 'o' are phi updates.
        o.toString();
      } else if (myIntStatic == 42) {
        o = m;
      }
    }
  }

  public static void $noinline$loopIncrement(Main m) {
    int k = 0;
    // We used to kill 'k' and replace it with 5000 when the inline cache
    // of 'doCheck' only contains 'Main'.
    // So the deoptimization at the inline cache was incorrectly assuming
    // 'k' was 5000.
    for (int i = 0; i < 5000; i++, k++) {
      if (m.doCheck()) {
        // We make this branch the only true user of the 'a' phi. All other uses
        // of 'a' are phi updates.
        myIntStatic = k;
      }
    }
    if (k != 5000) {
      throw new Error("Expected 5000, got " + k);
    }
  }

  public static int myIntStatic = 0;

  public static native void ensureJitCompiled(Class<?> itf, String name);
}

class SubMain extends Main {
  public boolean doCheck() {
    return true;
  }
}
