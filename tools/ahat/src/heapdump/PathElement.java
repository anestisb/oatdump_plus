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

public class PathElement implements Diffable<PathElement> {
  public final AhatInstance instance;
  public final String field;
  public boolean isDominator;

  public PathElement(AhatInstance instance, String field) {
    this.instance = instance;
    this.field = field;
    this.isDominator = false;
  }

  @Override public PathElement getBaseline() {
    return this;
  }

  @Override public boolean isPlaceHolder() {
    return false;
  }
}
