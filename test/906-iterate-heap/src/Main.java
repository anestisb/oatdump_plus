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
  }

  public static void doTest() throws Exception {
    A a = new A();
    B b = new B();
    B b2 = new B();
    C c = new C();
    A[] aArray = new A[5];

    setTag(a, 1);
    setTag(b, 2);
    setTag(b2, 3);
    setTag(aArray, 4);
    setTag(B.class, 100);

    int all = iterateThroughHeapCount(0, null, Integer.MAX_VALUE);
    int tagged = iterateThroughHeapCount(HEAP_FILTER_OUT_UNTAGGED, null, Integer.MAX_VALUE);
    int untagged = iterateThroughHeapCount(HEAP_FILTER_OUT_TAGGED, null, Integer.MAX_VALUE);
    int taggedClass = iterateThroughHeapCount(HEAP_FILTER_OUT_CLASS_UNTAGGED, null,
        Integer.MAX_VALUE);
    int untaggedClass = iterateThroughHeapCount(HEAP_FILTER_OUT_CLASS_TAGGED, null,
        Integer.MAX_VALUE);

    if (all != tagged + untagged) {
      throw new IllegalStateException("Instances: " + all + " != " + tagged + " + " + untagged);
    }
    if (all != taggedClass + untaggedClass) {
      throw new IllegalStateException("By class: " + all + " != " + taggedClass + " + " +
          untaggedClass);
    }
    if (tagged != 5) {
      throw new IllegalStateException(tagged + " tagged objects");
    }
    if (taggedClass != 2) {
      throw new IllegalStateException(tagged + " objects with tagged class");
    }
    if (all == tagged) {
      throw new IllegalStateException("All objects tagged");
    }
    if (all == taggedClass) {
      throw new IllegalStateException("All objects have tagged class");
    }

    long classTags[] = new long[100];
    long sizes[] = new long[100];
    long tags[] = new long[100];
    int lengths[] = new int[100];

    int n = iterateThroughHeapData(HEAP_FILTER_OUT_UNTAGGED, null, classTags, sizes, tags, lengths);
    System.out.println(sort(n, classTags, sizes, tags, lengths));

    iterateThroughHeapAdd(HEAP_FILTER_OUT_UNTAGGED, null);
    n = iterateThroughHeapData(HEAP_FILTER_OUT_UNTAGGED, null, classTags, sizes, tags, lengths);
    System.out.println(sort(n, classTags, sizes, tags, lengths));
  }

  static class A {
  }

  static class B {
  }

  static class C {
  }

  static class HeapElem implements Comparable<HeapElem> {
    long classTag;
    long size;
    long tag;
    int length;

    public int compareTo(HeapElem other) {
      if (tag != other.tag) {
        return Long.compare(tag, other.tag);
      }
      if (classTag != other.classTag) {
        return Long.compare(classTag, other.classTag);
      }
      if (size != other.size) {
        return Long.compare(size, other.size);
      }
      return Integer.compare(length, other.length);
    }

    public String toString() {
      return "{tag=" + tag + ", class-tag=" + classTag + ", size=" +
          (tag >= 100 ? "<class>" : size)  // Class size is dependent on 32-bit vs 64-bit,
                                           // so strip it.
          + ", length=" + length + "}";
    }
  }

  private static ArrayList<HeapElem> sort(int n, long classTags[], long sizes[], long tags[],
      int lengths[]) {
    ArrayList<HeapElem> ret = new ArrayList<HeapElem>(n);
    for (int i = 0; i < n; i++) {
      HeapElem elem = new HeapElem();
      elem.classTag = classTags[i];
      elem.size = sizes[i];
      elem.tag = tags[i];
      elem.length = lengths[i];
      ret.add(elem);
    }
    Collections.sort(ret);
    return ret;
  }

  private static native void setTag(Object o, long tag);
  private static native long getTag(Object o);

  private final static int HEAP_FILTER_OUT_TAGGED = 0x4;
  private final static int HEAP_FILTER_OUT_UNTAGGED = 0x8;
  private final static int HEAP_FILTER_OUT_CLASS_TAGGED = 0x10;
  private final static int HEAP_FILTER_OUT_CLASS_UNTAGGED = 0x20;

  private static native int iterateThroughHeapCount(int heapFilter,
      Class<?> klassFilter, int stopAfter);
  private static native int iterateThroughHeapData(int heapFilter,
      Class<?> klassFilter, long classTags[], long sizes[], long tags[], int lengths[]);
  private static native int iterateThroughHeapAdd(int heapFilter,
      Class<?> klassFilter);
}
