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

package com.android.ahat.heapdump;

import com.android.tools.perflib.heap.ArrayInstance;
import com.android.tools.perflib.heap.Instance;
import java.nio.charset.StandardCharsets;
import java.util.AbstractList;
import java.util.List;

public class AhatArrayInstance extends AhatInstance {
  // To save space, we store byte, character, and object arrays directly as
  // byte, character, and AhatInstance arrays respectively. This is especially
  // important for large byte arrays, such as bitmaps. All other array types
  // are stored as an array of objects, though we could potentially save space
  // by specializing those too. mValues is a list view of the underlying
  // array.
  private List<Value> mValues;
  private byte[] mByteArray;    // null if not a byte array.
  private char[] mCharArray;    // null if not a char array.

  public AhatArrayInstance(long id) {
    super(id);
  }

  @Override void initialize(AhatSnapshot snapshot, Instance inst) {
    super.initialize(snapshot, inst);

    ArrayInstance array = (ArrayInstance)inst;
    switch (array.getArrayType()) {
      case OBJECT:
        Object[] objects = array.getValues();
        final AhatInstance[] insts = new AhatInstance[objects.length];
        for (int i = 0; i < objects.length; i++) {
          if (objects[i] != null) {
            Instance ref = (Instance)objects[i];
            insts[i] = snapshot.findInstance(ref.getId());
            if (ref.getNextInstanceToGcRoot() == inst) {
              String field = "[" + Integer.toString(i) + "]";
              insts[i].setNextInstanceToGcRoot(this, field);
            }
          }
        }
        mValues = new AbstractList<Value>() {
          @Override public int size() {
            return insts.length;
          }

          @Override public Value get(int index) {
            AhatInstance obj = insts[index];
            return obj == null ? null : new Value(insts[index]);
          }
        };
        break;

      case CHAR:
        final char[] chars = array.asCharArray(0, array.getLength());
        mCharArray = chars;
        mValues = new AbstractList<Value>() {
          @Override public int size() {
            return chars.length;
          }

          @Override public Value get(int index) {
            return new Value(chars[index]);
          }
        };
        break;

      case BYTE:
        final byte[] bytes = array.asRawByteArray(0, array.getLength());
        mByteArray = bytes;
        mValues = new AbstractList<Value>() {
          @Override public int size() {
            return bytes.length;
          }

          @Override public Value get(int index) {
            return new Value(bytes[index]);
          }
        };
        break;

      default:
        final Object[] values = array.getValues();
        mValues = new AbstractList<Value>() {
          @Override public int size() {
            return values.length;
          }

          @Override public Value get(int index) {
            Object obj = values[index];
            return obj == null ? null : new Value(obj);
          }
        };
        break;
    }
  }

  /**
   * Returns the length of the array.
   */
  public int getLength() {
    return mValues.size();
  }

  /**
   * Returns the array's values.
   */
  public List<Value> getValues() {
    return mValues;
  }

  /**
   * Returns the object at the given index of this array.
   */
  public Value getValue(int index) {
    return mValues.get(index);
  }

  @Override public boolean isArrayInstance() {
    return true;
  }

  @Override public AhatArrayInstance asArrayInstance() {
    return this;
  }

  @Override public String asString(int maxChars) {
    return asString(0, getLength(), maxChars);
  }

  /**
   * Returns the String value associated with this array.
   * Only char arrays are considered as having an associated String value.
   */
  String asString(int offset, int count, int maxChars) {
    if (mCharArray == null) {
      return null;
    }

    if (count == 0) {
      return "";
    }
    int numChars = mCharArray.length;
    if (0 <= maxChars && maxChars < count) {
      count = maxChars;
    }

    int end = offset + count - 1;
    if (offset >= 0 && offset < numChars && end >= 0 && end < numChars) {
      return new String(mCharArray, offset, count);
    }
    return null;
  }

  /**
   * Returns the ascii String value associated with this array.
   * Only byte arrays are considered as having an associated ascii String value.
   */
  String asAsciiString(int offset, int count, int maxChars) {
    if (mByteArray == null) {
      return null;
    }

    if (count == 0) {
      return "";
    }
    int numChars = mByteArray.length;
    if (0 <= maxChars && maxChars < count) {
      count = maxChars;
    }

    int end = offset + count - 1;
    if (offset >= 0 && offset < numChars && end >= 0 && end < numChars) {
      return new String(mByteArray, offset, count, StandardCharsets.US_ASCII);
    }
    return null;
  }

  /**
   * Returns the String value associated with this array. Byte arrays are
   * considered as ascii encoded strings.
   */
  String asMaybeCompressedString(int offset, int count, int maxChars) {
    String str = asString(offset, count, maxChars);
    if (str == null) {
      str = asAsciiString(offset, count, maxChars);
    }
    return str;
  }

  @Override public AhatInstance getAssociatedBitmapInstance() {
    if (mByteArray != null) {
      List<AhatInstance> refs = getHardReverseReferences();
      if (refs.size() == 1) {
        AhatInstance ref = refs.get(0);
        return ref.getAssociatedBitmapInstance();
      }
    }
    return null;
  }

  @Override public String toString() {
    String className = getClassName();
    if (className.endsWith("[]")) {
      className = className.substring(0, className.length() - 2);
    }
    return String.format("%s[%d]@%08x", className, mValues.size(), getId());
  }

  byte[] asByteArray() {
    return mByteArray;
  }
}
