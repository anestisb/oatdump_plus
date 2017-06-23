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

package com.android.ahat.heapdump;

import java.util.Objects;

/** DiffedFieldValue is used by the DiffedField class to return the result of
 * diffing two collections of fields.
 */
public class DiffedFieldValue {
  public final String name;
  public final String type;
  public final Value current;
  public final Value baseline;

  public final Status status;

  public static enum Status {
    ADDED,      // The current field has no matching baseline value.
    MATCHED,    // The current field has a matching baseline value.
    DELETED     // The baseline field has no matching current value.
  };

  /**
   * Return a DiffedFieldValue where there is both a current and baseline.
   */
  public static DiffedFieldValue matched(FieldValue current, FieldValue baseline) {
    return new DiffedFieldValue(current.name,
                                current.type,
                                current.value,
                                baseline.value,
                                Status.MATCHED);
  }

  /**
   * Return a DiffedFieldValue where there is no baseline.
   */
  public static DiffedFieldValue added(FieldValue current) {
    return new DiffedFieldValue(current.name, current.type, current.value, null, Status.ADDED);
  }

  /**
   * Return a DiffedFieldValue where there is no current.
   */
  public static DiffedFieldValue deleted(FieldValue baseline) {
    return new DiffedFieldValue(baseline.name, baseline.type, null, baseline.value, Status.DELETED);
  }

  private DiffedFieldValue(String name, String type, Value current, Value baseline, Status status) {
    this.name = name;
    this.type = type;
    this.current = current;
    this.baseline = baseline;
    this.status = status;
  }

  @Override
  public boolean equals(Object otherObject) {
    if (otherObject instanceof DiffedFieldValue) {
      DiffedFieldValue other = (DiffedFieldValue)otherObject;
      return name.equals(other.name)
        && type.equals(other.type)
        && Objects.equals(current, other.current)
        && Objects.equals(baseline, other.baseline)
        && Objects.equals(status, other.status);
    }
    return false;
  }

  @Override
  public String toString() {
    switch (status) {
      case ADDED:
        return "(" + name + " " + type + " +" + current + ")";

      case MATCHED:
        return "(" + name + " " + type + " " + current + " " + baseline + ")";

      case DELETED:
        return "(" + name + " " + type + " -" + baseline + ")";

      default:
        // There are no other members.
        throw new AssertionError("unsupported enum member");
    }
  }
}
