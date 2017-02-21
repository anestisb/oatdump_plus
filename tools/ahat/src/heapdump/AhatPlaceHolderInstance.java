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
 * Generic PlaceHolder instance to take the place of a real AhatInstance for
 * the purposes of displaying diffs.
 *
 * This should be created through a call to AhatInstance.newPlaceHolder();
 */
public class AhatPlaceHolderInstance extends AhatInstance {
  AhatPlaceHolderInstance(AhatInstance baseline) {
    super(-1);
    setBaseline(baseline);
    baseline.setBaseline(this);
  }

  @Override public long getSize() {
    return 0;
  }

  @Override public long getRetainedSize(AhatHeap heap) {
    return 0;
  }

  @Override public long getTotalRetainedSize() {
    return 0;
  }

  @Override public AhatHeap getHeap() {
    return getBaseline().getHeap().getBaseline();
  }

  @Override public String getClassName() {
    return getBaseline().getClassName();
  }

  @Override public String asString(int maxChars) {
    return getBaseline().asString(maxChars);
  }

  @Override public String toString() {
    return getBaseline().toString();
  }

  @Override public boolean isPlaceHolder() {
    return true;
  }
}
