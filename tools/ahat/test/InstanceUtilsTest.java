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

import com.android.tools.perflib.heap.ArrayInstance;
import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Instance;
import java.io.IOException;
import java.util.List;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import org.junit.Test;

public class InstanceUtilsTest {
  @Test
  public void asStringBasic() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("basicString");
    assertEquals("hello, world", InstanceUtils.asString(str));
  }

  @Test
  public void asStringCharArray() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("charArray");
    assertEquals("char thing", InstanceUtils.asString(str));
  }

  @Test
  public void asStringTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("basicString");
    assertEquals("hello", InstanceUtils.asString(str, 5));
  }

  @Test
  public void asStringCharArrayTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("charArray");
    assertEquals("char ", InstanceUtils.asString(str, 5));
  }

  @Test
  public void asStringExactMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("basicString");
    assertEquals("hello, world", InstanceUtils.asString(str, 12));
  }

  @Test
  public void asStringCharArrayExactMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("charArray");
    assertEquals("char thing", InstanceUtils.asString(str, 10));
  }

  @Test
  public void asStringNotTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("basicString");
    assertEquals("hello, world", InstanceUtils.asString(str, 50));
  }

  @Test
  public void asStringCharArrayNotTruncated() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("charArray");
    assertEquals("char thing", InstanceUtils.asString(str, 50));
  }

  @Test
  public void asStringNegativeMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("basicString");
    assertEquals("hello, world", InstanceUtils.asString(str, -3));
  }

  @Test
  public void asStringCharArrayNegativeMax() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance str = (Instance)dump.getDumpedThing("charArray");
    assertEquals("char thing", InstanceUtils.asString(str, -3));
  }

  @Test
  public void asStringNull() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance obj = (Instance)dump.getDumpedThing("nullString");
    assertNull(InstanceUtils.asString(obj));
  }

  @Test
  public void asStringNotString() throws IOException {
    TestDump dump = TestDump.getTestDump();
    Instance obj = (Instance)dump.getDumpedThing("anObject");
    assertNotNull(obj);
    assertNull(InstanceUtils.asString(obj));
  }

  @Test
  public void basicReference() throws IOException {
    TestDump dump = TestDump.getTestDump();

    Instance pref = (Instance)dump.getDumpedThing("aPhantomReference");
    Instance wref = (Instance)dump.getDumpedThing("aWeakReference");
    Instance referent = (Instance)dump.getDumpedThing("anObject");
    assertNotNull(pref);
    assertNotNull(wref);
    assertNotNull(referent);
    assertEquals(referent, InstanceUtils.getReferent(pref));
    assertEquals(referent, InstanceUtils.getReferent(wref));
    assertNull(InstanceUtils.getReferent(referent));
  }

  @Test
  public void gcRootPath() throws IOException {
    TestDump dump = TestDump.getTestDump();

    ClassObj main = dump.getAhatSnapshot().findClass("Main");
    ArrayInstance gcPathArray = (ArrayInstance)dump.getDumpedThing("gcPathArray");
    Object[] values = gcPathArray.getValues();
    Instance base = (Instance)values[2];
    Instance left = InstanceUtils.getRefField(base, "left");
    Instance right = InstanceUtils.getRefField(base, "right");
    Instance target = InstanceUtils.getRefField(left, "right");

    List<InstanceUtils.PathElement> path = InstanceUtils.getPathFromGcRoot(target);
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
}
