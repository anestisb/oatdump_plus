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

import com.android.ahat.dominators.DominatorsComputation;
import java.util.AbstractList;
import java.util.ArrayList;
import java.util.List;

public class SuperRoot extends AhatInstance implements DominatorsComputation.Node {
  private List<AhatInstance> mRoots = new ArrayList<AhatInstance>();
  private Object mDominatorsComputationState;

  public SuperRoot() {
    super(0);
  }

  public void addRoot(AhatInstance root) {
    mRoots.add(root);
  }

  @Override
  public String toString() {
    return "SUPER_ROOT";
  }

  @Override
  ReferenceIterator getReferences() {
    List<Reference> refs = new AbstractList<Reference>() {
      @Override
      public int size() {
        return mRoots.size();
      }

      @Override
      public Reference get(int index) {
        String field = ".roots[" + Integer.toString(index) + "]";
        return new Reference(null, field, mRoots.get(index), true);
      }
    };
    return new ReferenceIterator(refs);
  }
}
