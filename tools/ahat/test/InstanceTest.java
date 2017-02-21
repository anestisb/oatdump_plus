/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.android.ahat;

import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatHeap;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.PathElement;
import com.android.ahat.heapdump.Value;
import com.android.tools.perflib.heap.hprof.HprofClassDump;
import com.android.tools.perflib.heap.hprof.HprofConstant;
import com.android.tools.perflib.heap.hprof.HprofDumpRecord;
import com.android.tools.perflib.heap.hprof.HprofHeapDump;
import com.android.tools.perflib.heap.hprof.HprofInstanceDump;
import com.android.tools.perflib.heap.hprof.HprofInstanceField;
import com.android.tools.perflib.heap.hprof.HprofLoadClass;
import com.android.tools.perflib.heap.hprof.HprofPrimitiveArrayDump;
import com.android.tools.perflib.heap.hprof.HprofRecord;
import com.android.tools.perflib.heap.hprof.HprofRootDebugger;
import com.android.tools.perflib.heap.hprof.HprofStaticField;
import com.android.tools.perflib.heap.hprof.HprofStringBuilder;
import com.android.tools.perflib.heap.hprof.HprofType;
import com.google.common.io.ByteArrayDataOutput;
import com.google.common.io.ByteStreams;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class InstanceTest {
  @Test
  public void asStringBasic() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("basicString");
    assertEquals("hello, world", str.asString());
  }

  @Test
  public void asStringNonAscii() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("nonAscii");
    assertEquals("Sigma (Ʃ) is not ASCII", str.asString());
  }

  @Test
  public void asStringEmbeddedZero() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("embeddedZero");
    assertEquals("embedded\0...", str.asString());
  }

  @Test
  public void asStringCharArray() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("charArray");
    assertEquals("char thing", str.asString());
  }

  @Test
  public void asStringTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("basicString");
    assertEquals("hello", str.asString(5));
  }

  @Test
  public void asStringTruncatedNonAscii() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("nonAscii");
    assertEquals("Sigma (Ʃ)", str.asString(9));
  }

  @Test
  public void asStringTruncatedEmbeddedZero() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("embeddedZero");
    assertEquals("embed", str.asString(5));
  }

  @Test
  public void asStringCharArrayTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("charArray");
    assertEquals("char ", str.asString(5));
  }

  @Test
  public void asStringExactMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("basicString");
    assertEquals("hello, world", str.asString(12));
  }

  @Test
  public void asStringExactMaxNonAscii() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("nonAscii");
    assertEquals("Sigma (Ʃ) is not ASCII", str.asString(22));
  }

  @Test
  public void asStringExactMaxEmbeddedZero() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("embeddedZero");
    assertEquals("embedded\0...", str.asString(12));
  }

  @Test
  public void asStringCharArrayExactMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("charArray");
    assertEquals("char thing", str.asString(10));
  }

  @Test
  public void asStringNotTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("basicString");
    assertEquals("hello, world", str.asString(50));
  }

  @Test
  public void asStringNotTruncatedNonAscii() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("nonAscii");
    assertEquals("Sigma (Ʃ) is not ASCII", str.asString(50));
  }

  @Test
  public void asStringNotTruncatedEmbeddedZero() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("embeddedZero");
    assertEquals("embedded\0...", str.asString(50));
  }

  @Test
  public void asStringCharArrayNotTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("charArray");
    assertEquals("char thing", str.asString(50));
  }

  @Test
  public void asStringNegativeMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("basicString");
    assertEquals("hello, world", str.asString(-3));
  }

  @Test
  public void asStringNegativeMaxNonAscii() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("nonAscii");
    assertEquals("Sigma (Ʃ) is not ASCII", str.asString(-3));
  }

  @Test
  public void asStringNegativeMaxEmbeddedZero() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("embeddedZero");
    assertEquals("embedded\0...", str.asString(-3));
  }

  @Test
  public void asStringCharArrayNegativeMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance str = dump.getDumpedAhatInstance("charArray");
    assertEquals("char thing", str.asString(-3));
  }

  @Test
  public void asStringNull() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("nullString");
    assertNull(obj);
  }

  @Test
  public void asStringNotString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("anObject");
    assertNotNull(obj);
    assertNull(obj.asString());
  }

  @Test
  public void basicReference() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatInstance pref = dump.getDumpedAhatInstance("aPhantomReference");
    AhatInstance wref = dump.getDumpedAhatInstance("aWeakReference");
    AhatInstance nref = dump.getDumpedAhatInstance("aNullReferentReference");
    AhatInstance referent = dump.getDumpedAhatInstance("anObject");
    assertNotNull(pref);
    assertNotNull(wref);
    assertNotNull(nref);
    assertNotNull(referent);
    assertEquals(referent, pref.getReferent());
    assertEquals(referent, wref.getReferent());
    assertNull(nref.getReferent());
    assertNull(referent.getReferent());
  }

  @Test
  public void unreachableReferent() throws IOException {
    // The test dump program should never be under enough GC pressure for the
    // soft reference to be cleared. Ensure that ahat will show the soft
    // reference as having a non-null referent.
    TestDump dump = TestDump.getTestDump();
    AhatInstance ref = dump.getDumpedAhatInstance("aSoftReference");
    assertNotNull(ref.getReferent());
  }

  @Test
  public void gcRootPath() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatClassObj main = dump.getAhatSnapshot().findClass("Main");
    AhatInstance gcPathArray = dump.getDumpedAhatInstance("gcPathArray");
    Value value = gcPathArray.asArrayInstance().getValue(2);
    AhatInstance base = value.asAhatInstance();
    AhatInstance left = base.getRefField("left");
    AhatInstance right = base.getRefField("right");
    AhatInstance target = left.getRefField("right");

    List<PathElement> path = target.getPathFromGcRoot();
    assertEquals(6, path.size());

    assertEquals(main, path.get(0).instance);
    assertEquals(".stuff", path.get(0).field);
    assertTrue(path.get(0).isDominator);

    assertEquals(".gcPathArray", path.get(1).field);
    assertTrue(path.get(1).isDominator);

    assertEquals(gcPathArray, path.get(2).instance);
    assertEquals("[2]", path.get(2).field);
    assertTrue(path.get(2).isDominator);

    assertEquals(base, path.get(3).instance);
    assertTrue(path.get(3).isDominator);

    // There are two possible paths. Either it can go through the 'left' node,
    // or the 'right' node.
    if (path.get(3).field.equals(".left")) {
      assertEquals(".left", path.get(3).field);

      assertEquals(left, path.get(4).instance);
      assertEquals(".right", path.get(4).field);
      assertFalse(path.get(4).isDominator);

    } else {
      assertEquals(".right", path.get(3).field);

      assertEquals(right, path.get(4).instance);
      assertEquals(".left", path.get(4).field);
      assertFalse(path.get(4).isDominator);
    }

    assertEquals(target, path.get(5).instance);
    assertEquals("", path.get(5).field);
    assertTrue(path.get(5).isDominator);
  }

  @Test
  public void retainedSize() throws IOException {
    TestDump dump = TestDump.getTestDump();

    // anObject should not be an immediate dominator of any other object. This
    // means its retained size should be equal to its size for the heap it was
    // allocated on, and should be 0 for all other heaps.
    AhatInstance anObject = dump.getDumpedAhatInstance("anObject");
    AhatSnapshot snapshot = dump.getAhatSnapshot();
    long size = anObject.getSize();
    assertEquals(size, anObject.getTotalRetainedSize());
    assertEquals(size, anObject.getRetainedSize(anObject.getHeap()));
    for (AhatHeap heap : snapshot.getHeaps()) {
      if (!heap.equals(anObject.getHeap())) {
        assertEquals(String.format("For heap '%s'", heap.getName()),
            0, anObject.getRetainedSize(heap));
      }
    }
  }

  @Test
  public void objectNotABitmap() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("anObject");
    assertNull(obj.asBitmap());
  }

  @Test
  public void arrayNotABitmap() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("gcPathArray");
    assertNull(obj.asBitmap());
  }

  @Test
  public void classObjNotABitmap() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getAhatSnapshot().findClass("Main");
    assertNull(obj.asBitmap());
  }

  @Test
  public void classInstanceToString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("aPhantomReference");
    long id = obj.getId();
    assertEquals(String.format("java.lang.ref.PhantomReference@%08x", id), obj.toString());
  }

  @Test
  public void classObjToString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getAhatSnapshot().findClass("Main");
    assertEquals("Main", obj.toString());
  }

  @Test
  public void arrayInstanceToString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("gcPathArray");
    long id = obj.getId();

    // There's a bug in perfib's proguard deobfuscation for arrays.
    // To work around that bug for the time being, only test the suffix of
    // the toString result. Ideally we test for string equality against
    // "Main$ObjectTree[4]@%08x", id.
    assertTrue(obj.toString().endsWith(String.format("[4]@%08x", id)));
  }

  @Test
  public void primArrayInstanceToString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("bigArray");
    long id = obj.getId();
    assertEquals(String.format("byte[1000000]@%08x", id), obj.toString());
  }

  @Test
  public void isNotRoot() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatInstance obj = dump.getDumpedAhatInstance("anObject");
    assertFalse(obj.isRoot());
    assertNull(obj.getRootTypes());
  }

  @Test
  public void asStringEmbedded() throws IOException {
    // Set up a heap dump with an instance of java.lang.String of
    // "hello" with instance id 0x42 that is backed by a char array that is
    // bigger. This is how ART used to represent strings, and we should still
    // support it in case the heap dump is from a previous platform version.
    HprofStringBuilder strings = new HprofStringBuilder(0);
    List<HprofRecord> records = new ArrayList<HprofRecord>();
    List<HprofDumpRecord> dump = new ArrayList<HprofDumpRecord>();

    final int stringClassObjectId = 1;
    records.add(new HprofLoadClass(0, 0, stringClassObjectId, 0, strings.get("java.lang.String")));
    dump.add(new HprofClassDump(stringClassObjectId, 0, 0, 0, 0, 0, 0, 0, 0,
          new HprofConstant[0], new HprofStaticField[0],
          new HprofInstanceField[]{
            new HprofInstanceField(strings.get("count"), HprofType.TYPE_INT),
            new HprofInstanceField(strings.get("hashCode"), HprofType.TYPE_INT),
            new HprofInstanceField(strings.get("offset"), HprofType.TYPE_INT),
            new HprofInstanceField(strings.get("value"), HprofType.TYPE_OBJECT)}));

    dump.add(new HprofPrimitiveArrayDump(0x41, 0, HprofType.TYPE_CHAR,
          new long[]{'n', 'o', 't', ' ', 'h', 'e', 'l', 'l', 'o', 'o', 'p'}));

    ByteArrayDataOutput values = ByteStreams.newDataOutput();
    values.writeInt(5);     // count
    values.writeInt(0);     // hashCode
    values.writeInt(4);     // offset
    values.writeInt(0x41);  // value
    dump.add(new HprofInstanceDump(0x42, 0, stringClassObjectId, values.toByteArray()));
    dump.add(new HprofRootDebugger(stringClassObjectId));
    dump.add(new HprofRootDebugger(0x42));

    records.add(new HprofHeapDump(0, dump.toArray(new HprofDumpRecord[0])));
    AhatSnapshot snapshot = SnapshotBuilder.makeSnapshot(strings, records);
    AhatInstance chars = snapshot.findInstance(0x41);
    assertNotNull(chars);
    assertEquals("not helloop", chars.asString());

    AhatInstance stringInstance = snapshot.findInstance(0x42);
    assertNotNull(stringInstance);
    assertEquals("hello", stringInstance.asString());
  }
}
