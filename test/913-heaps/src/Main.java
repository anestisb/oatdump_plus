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

public class Main {
  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[1]);

    doTest();
    doFollowReferencesTest();
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

  public static void doFollowReferencesTest() throws Exception {
    // Force GCs to clean up dirt.
    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();

    tagClasses();
    setTag(Thread.currentThread(), 3000);

    {
      ArrayList<Object> tmpStorage = new ArrayList<>();
      doFollowReferencesTestNonRoot(tmpStorage);
      tmpStorage = null;
    }

    // Force GCs to clean up dirt.
    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();

    doFollowReferencesTestRoot();

    // Force GCs to clean up dirt.
    Runtime.getRuntime().gc();
    Runtime.getRuntime().gc();
  }

  private static void doFollowReferencesTestNonRoot(ArrayList<Object> tmpStorage) {
    A a = createTree();
    tmpStorage.add(a);
    doFollowReferencesTestImpl(null, Integer.MAX_VALUE, -1, null);
    doFollowReferencesTestImpl(a, Integer.MAX_VALUE, -1, null);
    tmpStorage.clear();
  }

  private static void doFollowReferencesTestRoot() {
    A a = createTree();
    doFollowReferencesTestImpl(null, Integer.MAX_VALUE, -1, a);
    doFollowReferencesTestImpl(a, Integer.MAX_VALUE, -1, a);
  }

  private static void doFollowReferencesTestImpl(A root, int stopAfter, int followSet,
      Object asRoot) {
    String[] lines =
        followReferences(0, null, root == null ? null : root.foo, stopAfter, followSet, asRoot);
    // Note: sort the roots, as stack locals visit order isn't defined, so may depend on compiled
    //       code. Do not sort non-roots, as the order here needs to be verified (elements are
    //       finished before a reference is followed). The test setup (and root visit order)
    //       luckily ensures that this is deterministic.

    int i = 0;
    ArrayList<String> rootLines = new ArrayList<>();
    while (i < lines.length) {
      if (lines[i].startsWith("root")) {
        rootLines.add(lines[i]);
      } else {
        break;
      }
      i++;
    }
    Collections.sort(rootLines);
    for (String l : rootLines) {
      System.out.println(l);
    }

    // Print the non-root lines in order.
    while (i < lines.length) {
      System.out.println(lines[i]);
      i++;
    }

    System.out.println("---");

    // TODO: Test filters.
  }

  private static void tagClasses() {
    setTag(A.class, 1000);
    setTag(B.class, 1001);
    setTag(C.class, 1002);
    setTag(I1.class, 2000);
    setTag(I2.class, 2001);
  }

  private static A createTree() {
    A root = new A();
    setTag(root, 1);

    A foo = new A();
    setTag(foo, 2);
    root.foo = foo;

    B foo2 = new B();
    setTag(foo2, 3);
    root.foo2 = foo2;

    A bar = new A();
    setTag(bar, 4);
    foo2.bar = bar;

    C bar2 = new C();
    setTag(bar2, 5);
    foo2.bar2 = bar2;

    A baz = new A();
    setTag(baz, 6);
    bar2.baz = baz;
    bar2.baz2 = root;

    return root;
  }

  public static class A {
    public A foo;
    public A foo2;

    public A() {}
    public A(A a, A b) {
      foo = a;
      foo2 = b;
    }
  }

  public static class B extends A {
    public A bar;
    public A bar2;

    public B() {}
    public B(A a, A b) {
      bar = a;
      bar2 = b;
    }
  }

  public static interface I1 {
    public final static int i1Field = 1;
  }

  public static interface I2 extends I1 {
    public final static int i2Field = 2;
  }

  public static class C extends B implements I2 {
    public A baz;
    public A baz2;

    public C() {}
    public C(A a, A b) {
      baz = a;
      baz2 = b;
    }
  }

  private static native void setupGcCallback();
  private static native void enableGcTracking(boolean enable);
  private static native int getGcStarts();
  private static native int getGcFinishes();
  private static native void forceGarbageCollection();

  private static native void setTag(Object o, long tag);
  private static native long getTag(Object o);

  private static native String[] followReferences(int heapFilter, Class<?> klassFilter,
      Object initialObject, int stopAfter, int followSet, Object jniRef);
}
