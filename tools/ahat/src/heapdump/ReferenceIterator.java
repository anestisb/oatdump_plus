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

import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;

class ReferenceIterator implements Iterator<Reference>,
                                   Iterable<Reference> {
  private List<Reference> mRefs;
  private int mLength;
  private int mNextIndex;
  private Reference mNext;

  /**
   * Construct a ReferenceIterator that iterators over the given list of
   * references. Elements of the given list of references may be null, in
   * which case the ReferenceIterator will skip over them.
   */
  public ReferenceIterator(List<Reference> refs) {
    mRefs = refs;
    mLength = refs.size();
    mNextIndex = 0;
    mNext = null;
  }

  @Override
  public boolean hasNext() {
    while (mNext == null && mNextIndex < mLength) {
      mNext = mRefs.get(mNextIndex);
      mNextIndex++;
    }
    return mNext != null;
  }

  @Override
  public Reference next() {
    if (!hasNext()) {
      throw new NoSuchElementException();
    }
    Reference next = mNext;
    mNext = null;
    return next;
  }

  @Override
  public Iterator<Reference> iterator() {
    return this;
  }
}
