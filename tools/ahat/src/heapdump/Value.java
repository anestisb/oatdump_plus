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

/**
 * Value represents a field value in a heap dump. The field value is either a
 * subclass of AhatInstance or a primitive Java type.
 */
public class Value {
  private Object mObject;

  /**
   * Constructs a value from a generic Java Object.
   * The Object must either be a boxed Java primitive type or a subclass of
   * AhatInstance. The object must not be null.
   */
  Value(Object object) {
    // TODO: Check that the Object is either an AhatSnapshot or boxed Java
    // primitive type?
    assert object != null;
    mObject = object;
  }

  /**
   * Returns true if the Value is an AhatInstance, as opposed to a Java
   * primitive value.
   */
  public boolean isAhatInstance() {
    return mObject instanceof AhatInstance;
  }

  /**
   * Return the Value as an AhatInstance if it is one.
   * Returns null if the Value represents a Java primitive value.
   */
  public AhatInstance asAhatInstance() {
    if (isAhatInstance()) {
      return (AhatInstance)mObject;
    }
    return null;
  }

  /**
   * Returns true if the Value is an Integer.
   */
  public boolean isInteger() {
    return mObject instanceof Integer;
  }

  /**
   * Return the Value as an Integer if it is one.
   * Returns null if the Value does not represent an Integer.
   */
  public Integer asInteger() {
    if (isInteger()) {
      return (Integer)mObject;
    }
    return null;
  }

  /**
   * Returns true if the Value is an Long.
   */
  public boolean isLong() {
    return mObject instanceof Long;
  }

  /**
   * Return the Value as an Long if it is one.
   * Returns null if the Value does not represent an Long.
   */
  public Long asLong() {
    if (isLong()) {
      return (Long)mObject;
    }
    return null;
  }

  /**
   * Return the Value as a Byte if it is one.
   * Returns null if the Value does not represent a Byte.
   */
  public Byte asByte() {
    if (mObject instanceof Byte) {
      return (Byte)mObject;
    }
    return null;
  }

  /**
   * Return the Value as a Char if it is one.
   * Returns null if the Value does not represent a Char.
   */
  public Character asChar() {
    if (mObject instanceof Character) {
      return (Character)mObject;
    }
    return null;
  }

  public String toString() {
    return mObject.toString();
  }

  public static Value getBaseline(Value value) {
    if (value == null || !value.isAhatInstance()) {
      return value;
    }
    return new Value(value.asAhatInstance().getBaseline());
  }

  @Override public boolean equals(Object other) {
    if (other instanceof Value) {
      Value value = (Value)other;
      return mObject.equals(value.mObject);
    }
    return false;
  }
}
