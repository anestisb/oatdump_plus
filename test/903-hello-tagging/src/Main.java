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

import java.lang.ref.WeakReference;

public class Main {
  public static void main(String[] args) {
    System.loadLibrary(args[1]);
    doTest();
  }

  public static void doTest() {
    WeakReference<Object> weak = test();

    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();

    if (weak.get() != null) {
      throw new RuntimeException("WeakReference not cleared");
    }
  }

  private static WeakReference<Object> test() {
    Object o1 = new Object();
    setTag(o1, 1);

    Object o2 = new Object();
    setTag(o2, 2);

    checkTag(o1, 1);
    checkTag(o2, 2);

    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();

    checkTag(o1, 1);
    checkTag(o2, 2);

    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();

    setTag(o1, 10);
    setTag(o2, 20);

    checkTag(o1, 10);
    checkTag(o2, 20);

    return new WeakReference<Object>(o1);
  }

  private static void checkTag(Object o, long expectedTag) {
    long tag = getTag(o);
    if (expectedTag != tag) {
      throw new RuntimeException("Unexpected tag " + tag + ", expected " + expectedTag);
    }
  }

  private static native void setTag(Object o, long tag);
  private static native long getTag(Object o);
}
