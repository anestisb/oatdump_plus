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

import com.android.jack.annotations.CalledByInvokeCustom;
import com.android.jack.annotations.Constant;
import com.android.jack.annotations.LinkerMethodHandle;
import com.android.jack.annotations.MethodHandleKind;

import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

import java.lang.Thread;
import java.lang.ThreadLocal;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.CyclicBarrier;

public class TestInvokeCustomWithConcurrentThreads extends Thread {
  private static final int NUMBER_OF_THREADS = 16;

  private static final AtomicInteger nextIndex = new AtomicInteger(0);

  private static final ThreadLocal<Integer> threadIndex =
      new ThreadLocal<Integer>() {
        @Override
        protected Integer initialValue() {
          return nextIndex.getAndIncrement();
        }
      };

  // Array of call sites instantiated, one per thread
  private static final CallSite[] instantiated = new CallSite[NUMBER_OF_THREADS];

  // Array of counters for how many times each instantiated call site is called
  private static final AtomicInteger[] called = new AtomicInteger[NUMBER_OF_THREADS];

  // Array of call site indicies of which call site a thread invoked
  private static final AtomicInteger[] targetted = new AtomicInteger[NUMBER_OF_THREADS];

  // Synchronization barrier all threads will wait on in the bootstrap method.
  private static final CyclicBarrier barrier = new CyclicBarrier(NUMBER_OF_THREADS);

  private TestInvokeCustomWithConcurrentThreads() {}

  private static int getThreadIndex() {
    return threadIndex.get().intValue();
  }

  public static int notUsed(int x) {
    return x;
  }

  @Override
  public void run() {
    int x = setCalled(-1 /* argument dropped */);
    notUsed(x);
  }

  @CalledByInvokeCustom(
      invokeMethodHandle = @LinkerMethodHandle(kind = MethodHandleKind.INVOKE_STATIC,
          enclosingType = TestInvokeCustomWithConcurrentThreads.class,
          name = "linkerMethod",
          argumentTypes = {MethodHandles.Lookup.class, String.class, MethodType.class}),
      name = "setCalled",
      returnType = int.class,
      argumentTypes = {int.class})
  private static int setCalled(int index) {
    called[index].getAndIncrement();
    targetted[getThreadIndex()].set(index);
    return 0;
  }

  @SuppressWarnings("unused")
  private static CallSite linkerMethod(MethodHandles.Lookup caller,
                                       String name,
                                       MethodType methodType) throws Throwable {
    int threadIndex = getThreadIndex();
    MethodHandle mh =
        caller.findStatic(TestInvokeCustomWithConcurrentThreads.class, name, methodType);
    assertEquals(methodType, mh.type());
    assertEquals(mh.type().parameterCount(), 1);
    mh = MethodHandles.insertArguments(mh, 0, threadIndex);
    mh = MethodHandles.dropArguments(mh, 0, int.class);
    assertEquals(mh.type().parameterCount(), 1);
    assertEquals(methodType, mh.type());

    // Wait for all threads to be in this method.
    // Multiple call sites should be created, but only one
    // invoked.
    barrier.await();

    instantiated[getThreadIndex()] = new ConstantCallSite(mh);
    return instantiated[getThreadIndex()];
  }

  public static void test() throws Throwable {
    // Initialize counters for which call site gets invoked
    for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
      called[i] = new AtomicInteger(0);
      targetted[i] = new AtomicInteger(0);
    }

    // Run threads that each invoke-custom the call site
    Thread [] threads = new Thread[NUMBER_OF_THREADS];
    for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
      threads[i] = new TestInvokeCustomWithConcurrentThreads();
      threads[i].start();
    }

    // Wait for all threads to complete
    for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
      threads[i].join();
    }

    // Check one call site instance won
    int winners = 0;
    int votes = 0;
    for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
      assertNotEquals(instantiated[i], null);
      if (called[i].get() != 0) {
        winners++;
        votes += called[i].get();
      }
    }

    System.out.println("Winners " + winners + " Votes " + votes);

    // We assert this below but output details when there's an error as
    // it's non-deterministic.
    if (winners != 1) {
      System.out.println("Threads did not the same call-sites:");
      for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
        System.out.format(" Thread % 2d invoked call site instance #%02d\n",
                          i, targetted[i].get());
      }
    }

    // We assert this below but output details when there's an error as
    // it's non-deterministic.
    if (votes != NUMBER_OF_THREADS) {
      System.out.println("Call-sites invocations :");
      for (int i = 0; i < NUMBER_OF_THREADS; ++i) {
        System.out.format(" Call site instance #%02d was invoked % 2d times\n",
                          i, called[i].get());
      }
    }

    assertEquals(winners, 1);
    assertEquals(votes, NUMBER_OF_THREADS);
  }

  public static void assertTrue(boolean value) {
    if (!value) {
      throw new AssertionError("assertTrue value: " + value);
    }
  }

  public static void assertEquals(byte b1, byte b2) {
    if (b1 == b2) { return; }
    throw new AssertionError("assertEquals b1: " + b1 + ", b2: " + b2);
  }

  public static void assertEquals(char c1, char c2) {
    if (c1 == c2) { return; }
    throw new AssertionError("assertEquals c1: " + c1 + ", c2: " + c2);
  }

  public static void assertEquals(short s1, short s2) {
    if (s1 == s2) { return; }
    throw new AssertionError("assertEquals s1: " + s1 + ", s2: " + s2);
  }

  public static void assertEquals(int i1, int i2) {
    if (i1 == i2) { return; }
    throw new AssertionError("assertEquals i1: " + i1 + ", i2: " + i2);
  }

  public static void assertEquals(long l1, long l2) {
    if (l1 == l2) { return; }
    throw new AssertionError("assertEquals l1: " + l1 + ", l2: " + l2);
  }

  public static void assertEquals(float f1, float f2) {
    if (f1 == f2) { return; }
    throw new AssertionError("assertEquals f1: " + f1 + ", f2: " + f2);
  }

  public static void assertEquals(double d1, double d2) {
    if (d1 == d2) { return; }
    throw new AssertionError("assertEquals d1: " + d1 + ", d2: " + d2);
  }

  public static void assertEquals(Object o, Object p) {
    if (o == p) { return; }
    if (o != null && p != null && o.equals(p)) { return; }
    throw new AssertionError("assertEquals: o1: " + o + ", o2: " + p);
  }

  public static void assertNotEquals(Object o, Object p) {
    if (o != p) { return; }
    if (o != null && p != null && !o.equals(p)) { return; }
    throw new AssertionError("assertNotEquals: o1: " + o + ", o2: " + p);
  }

  public static void assertEquals(String s1, String s2) {
    if (s1 == s2) {
      return;
    }

    if (s1 != null && s2 != null && s1.equals(s2)) {
      return;
    }

    throw new AssertionError("assertEquals s1: " + s1 + ", s2: " + s2);
  }
}
