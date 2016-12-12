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

import com.android.tools.perflib.heap.ClassInstance;
import com.android.tools.perflib.heap.Instance;
import java.awt.image.BufferedImage;
import java.util.Arrays;
import java.util.List;

public class AhatClassInstance extends AhatInstance {
  private FieldValue[] mFieldValues;

  public AhatClassInstance(long id) {
    super(id);
  }

  @Override void initialize(AhatSnapshot snapshot, Instance inst) {
    super.initialize(snapshot, inst);

    ClassInstance classInst = (ClassInstance)inst;
    List<ClassInstance.FieldValue> fieldValues = classInst.getValues();
    mFieldValues = new FieldValue[fieldValues.size()];
    for (int i = 0; i < mFieldValues.length; i++) {
      ClassInstance.FieldValue field = fieldValues.get(i);
      String name = field.getField().getName();
      String type = field.getField().getType().toString();
      Value value = snapshot.getValue(field.getValue());

      mFieldValues[i] = new FieldValue(name, type, value);

      if (field.getValue() instanceof Instance) {
        Instance ref = (Instance)field.getValue();
        if (ref.getNextInstanceToGcRoot() == inst) {
          value.asAhatInstance().setNextInstanceToGcRoot(this, "." + name);
        }
      }
    }
  }

  @Override public Value getField(String fieldName) {
    for (FieldValue field : mFieldValues) {
      if (fieldName.equals(field.getName())) {
        return field.getValue();
      }
    }
    return null;
  }

  @Override public AhatInstance getRefField(String fieldName) {
    Value value = getField(fieldName);
    return value == null ? null : value.asAhatInstance();
  }

  /**
   * Read an int field of an instance.
   * The field is assumed to be an int type.
   * Returns <code>def</code> if the field value is not an int or could not be
   * read.
   */
  private Integer getIntField(String fieldName, Integer def) {
    Value value = getField(fieldName);
    if (value == null || !value.isInteger()) {
      return def;
    }
    return value.asInteger();
  }

  /**
   * Read a long field of this instance.
   * The field is assumed to be a long type.
   * Returns <code>def</code> if the field value is not an long or could not
   * be read.
   */
  private Long getLongField(String fieldName, Long def) {
    Value value = getField(fieldName);
    if (value == null || !value.isLong()) {
      return def;
    }
    return value.asLong();
  }

  /**
   * Returns the list of class instance fields for this instance.
   */
  public List<FieldValue> getInstanceFields() {
    return Arrays.asList(mFieldValues);
  }

  /**
   * Returns true if this is an instance of a class with the given name.
   */
  private boolean isInstanceOfClass(String className) {
    AhatClassObj cls = getClassObj();
    while (cls != null) {
      if (className.equals(cls.getName())) {
        return true;
      }
      cls = cls.getSuperClassObj();
    }
    return false;
  }

  @Override public String asString(int maxChars) {
    if (!isInstanceOfClass("java.lang.String")) {
      return null;
    }

    Value value = getField("value");
    if (!value.isAhatInstance()) {
      return null;
    }

    AhatInstance inst = value.asAhatInstance();
    if (inst.isArrayInstance()) {
      AhatArrayInstance chars = inst.asArrayInstance();
      int numChars = chars.getLength();
      int count = getIntField("count", numChars);
      int offset = getIntField("offset", 0);
      return chars.asMaybeCompressedString(offset, count, maxChars);
    }
    return null;
  }

  @Override public AhatInstance getReferent() {
    if (isInstanceOfClass("java.lang.ref.Reference")) {
      return getRefField("referent");
    }
    return null;
  }

  @Override public String getDexCacheLocation(int maxChars) {
    if (isInstanceOfClass("java.lang.DexCache")) {
      AhatInstance location = getRefField("location");
      if (location != null) {
        return location.asString(maxChars);
      }
    }
    return null;
  }

  @Override public AhatInstance getAssociatedBitmapInstance() {
    if (isInstanceOfClass("android.graphics.Bitmap")) {
      return this;
    }
    return null;
  }

  @Override public boolean isClassInstance() {
    return true;
  }

  @Override public AhatClassInstance asClassInstance() {
    return this;
  }

  @Override public String toString() {
    return String.format("%s@%08x", getClassName(), getId());
  }

  /**
   * Read the given field from the given instance.
   * The field is assumed to be a byte[] field.
   * Returns null if the field value is null, not a byte[] or could not be read.
   */
  private byte[] getByteArrayField(String fieldName) {
    Value value = getField(fieldName);
    if (!value.isAhatInstance()) {
      return null;
    }
    return value.asAhatInstance().asByteArray();
  }

  public BufferedImage asBitmap() {
    if (!isInstanceOfClass("android.graphics.Bitmap")) {
      return null;
    }

    Integer width = getIntField("mWidth", null);
    if (width == null) {
      return null;
    }

    Integer height = getIntField("mHeight", null);
    if (height == null) {
      return null;
    }

    byte[] buffer = getByteArrayField("mBuffer");
    if (buffer == null) {
      return null;
    }

    // Convert the raw data to an image
    // Convert BGRA to ABGR
    int[] abgr = new int[height * width];
    for (int i = 0; i < abgr.length; i++) {
      abgr[i] = (
          (((int) buffer[i * 4 + 3] & 0xFF) << 24)
          + (((int) buffer[i * 4 + 0] & 0xFF) << 16)
          + (((int) buffer[i * 4 + 1] & 0xFF) << 8)
          + ((int) buffer[i * 4 + 2] & 0xFF));
    }

    BufferedImage bitmap = new BufferedImage(
        width, height, BufferedImage.TYPE_4BYTE_ABGR);
    bitmap.setRGB(0, 0, width, height, abgr, 0, width);
    return bitmap;
  }
}
