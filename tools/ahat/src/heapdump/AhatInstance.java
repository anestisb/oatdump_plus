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

import com.android.ahat.dominators.DominatorsComputation;
import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Instance;
import java.awt.image.BufferedImage;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Queue;

public abstract class AhatInstance implements Diffable<AhatInstance>,
                                              DominatorsComputation.Node {
  // The id of this instance from the heap dump.
  private final long mId;

  // Fields initialized in initialize().
  private Size mSize;
  private AhatHeap mHeap;
  private AhatClassObj mClassObj;
  private Site mSite;

  // If this instance is a root, mRootTypes contains a set of the root types.
  // If this instance is not a root, mRootTypes is null.
  private List<String> mRootTypes;

  // Fields initialized in computeReverseReferences().
  private AhatInstance mNextInstanceToGcRoot;
  private String mNextInstanceToGcRootField;
  private ArrayList<AhatInstance> mHardReverseReferences;
  private ArrayList<AhatInstance> mSoftReverseReferences;

  // Fields initialized in DominatorsComputation.computeDominators().
  // mDominated - the list of instances immediately dominated by this instance.
  // mRetainedSizes - retained size indexed by heap index.
  private AhatInstance mImmediateDominator;
  private List<AhatInstance> mDominated = new ArrayList<AhatInstance>();
  private Size[] mRetainedSizes;
  private Object mDominatorsComputationState;

  // The baseline instance for purposes of diff.
  private AhatInstance mBaseline;

  public AhatInstance(long id) {
    mId = id;
    mBaseline = this;
  }

  /**
   * Initializes this AhatInstance based on the given perflib instance.
   * The AhatSnapshot should be used to look up AhatInstances and AhatHeaps.
   * There is no guarantee that the AhatInstances returned by
   * snapshot.findInstance have been initialized yet.
   */
  void initialize(AhatSnapshot snapshot, Instance inst, Site site) {
    mSize = new Size(inst.getSize(), 0);
    mHeap = snapshot.getHeap(inst.getHeap().getName());

    ClassObj clsObj = inst.getClassObj();
    if (clsObj != null) {
      mClassObj = snapshot.findClassObj(clsObj.getId());
    }

    mSite = site;
  }

  /**
   * Returns a unique identifier for the instance.
   */
  public long getId() {
    return mId;
  }

  /**
   * Returns the shallow number of bytes this object takes up.
   */
  public Size getSize() {
    return mSize;
  }

  /**
   * Returns the number of bytes belonging to the given heap that this instance
   * retains.
   */
  public Size getRetainedSize(AhatHeap heap) {
    int index = heap.getIndex();
    if (mRetainedSizes != null && 0 <= index && index < mRetainedSizes.length) {
      return mRetainedSizes[heap.getIndex()];
    }
    return Size.ZERO;
  }

  /**
   * Returns the total number of bytes this instance retains.
   */
  public Size getTotalRetainedSize() {
    Size size = Size.ZERO;
    if (mRetainedSizes != null) {
      for (int i = 0; i < mRetainedSizes.length; i++) {
        size = size.plus(mRetainedSizes[i]);
      }
    }
    return size;
  }

  /**
   * Increment the number of registered native bytes tied to this object.
   */
  void addRegisteredNativeSize(long size) {
    mSize = mSize.plusRegisteredNativeSize(size);
  }

  /**
   * Returns whether this object is strongly-reachable.
   */
  public boolean isReachable() {
    return mImmediateDominator != null;
  }

  /**
   * Returns the heap that this instance is allocated on.
   */
  public AhatHeap getHeap() {
    return mHeap;
  }

  /**
   * Returns an iterator over the references this AhatInstance has to other
   * AhatInstances.
   */
  abstract ReferenceIterator getReferences();

  /**
   * Returns true if this instance is marked as a root instance.
   */
  public boolean isRoot() {
    return mRootTypes != null;
  }

  /**
   * Marks this instance as being a root of the given type.
   */
  void addRootType(String type) {
    if (mRootTypes == null) {
      mRootTypes = new ArrayList<String>();
      mRootTypes.add(type);
    } else if (!mRootTypes.contains(type)) {
      mRootTypes.add(type);
    }
  }

  /**
   * Returns a list of string descriptions of the root types of this object.
   * Returns null if this object is not a root.
   */
  public Collection<String> getRootTypes() {
    return mRootTypes;
  }

  /**
   * Returns the immediate dominator of this instance.
   * Returns null if this is a root instance.
   */
  public AhatInstance getImmediateDominator() {
    return mImmediateDominator;
  }

  /**
   * Returns a list of those objects immediately dominated by the given
   * instance.
   */
  public List<AhatInstance> getDominated() {
    return mDominated;
  }

  /**
   * Returns the site where this instance was allocated.
   */
  public Site getSite() {
    return mSite;
  }

  /**
   * Returns true if the given instance is a class object
   */
  public boolean isClassObj() {
    // Overridden by AhatClassObj.
    return false;
  }

  /**
   * Returns this as an AhatClassObj if this is an AhatClassObj.
   * Returns null if this is not an AhatClassObj.
   */
  public AhatClassObj asClassObj() {
    // Overridden by AhatClassObj.
    return null;
  }

  /**
   * Returns the class object instance for the class of this object.
   */
  public AhatClassObj getClassObj() {
    return mClassObj;
  }

  /**
   * Returns the name of the class this object belongs to.
   */
  public String getClassName() {
    AhatClassObj classObj = getClassObj();
    return classObj == null ? "???" : classObj.getName();
  }

  /**
   * Returns true if the given instance is an array instance
   */
  public boolean isArrayInstance() {
    // Overridden by AhatArrayInstance.
    return false;
  }

  /**
   * Returns this as an AhatArrayInstance if this is an AhatArrayInstance.
   * Returns null if this is not an AhatArrayInstance.
   */
  public AhatArrayInstance asArrayInstance() {
    // Overridden by AhatArrayInstance.
    return null;
  }

  /**
   * Returns true if the given instance is a class instance
   */
  public boolean isClassInstance() {
    return false;
  }

  /**
   * Returns this as an AhatClassInstance if this is an AhatClassInstance.
   * Returns null if this is not an AhatClassInstance.
   */
  public AhatClassInstance asClassInstance() {
    return null;
  }

  /**
   * Return the referent associated with this instance.
   * This is relevent for instances of java.lang.ref.Reference.
   * Returns null if the instance has no referent associated with it.
   */
  public AhatInstance getReferent() {
    // Overridden by AhatClassInstance.
    return null;
  }

  /**
   * Returns a list of objects with hard references to this object.
   */
  public List<AhatInstance> getHardReverseReferences() {
    if (mHardReverseReferences != null) {
      return mHardReverseReferences;
    }
    return Collections.emptyList();
  }

  /**
   * Returns a list of objects with soft references to this object.
   */
  public List<AhatInstance> getSoftReverseReferences() {
    if (mSoftReverseReferences != null) {
      return mSoftReverseReferences;
    }
    return Collections.emptyList();
  }

  /**
   * Returns the value of a field of an instance.
   * Returns null if the field value is null, the field couldn't be read, or
   * there are multiple fields with the same name.
   */
  public Value getField(String fieldName) {
    // Overridden by AhatClassInstance.
    return null;
  }

  /**
   * Reads a reference field of this instance.
   * Returns null if the field value is null, or if the field couldn't be read.
   */
  public AhatInstance getRefField(String fieldName) {
    // Overridden by AhatClassInstance.
    return null;
  }

  /**
   * Assuming inst represents a DexCache object, return the dex location for
   * that dex cache. Returns null if the given instance doesn't represent a
   * DexCache object or the location could not be found.
   * If maxChars is non-negative, the returned location is truncated to
   * maxChars in length.
   */
  public String getDexCacheLocation(int maxChars) {
    return null;
  }

  /**
   * Return the bitmap instance associated with this object, or null if there
   * is none. This works for android.graphics.Bitmap instances and their
   * underlying Byte[] instances.
   */
  public AhatInstance getAssociatedBitmapInstance() {
    return null;
  }

  /**
   * Read the string value from this instance.
   * Returns null if this object can't be interpreted as a string.
   * The returned string is truncated to maxChars characters.
   * If maxChars is negative, the returned string is not truncated.
   */
  public String asString(int maxChars) {
    // By default instances can't be interpreted as a string. This method is
    // overridden by AhatClassInstance and AhatArrayInstance for those cases
    // when an instance can be interpreted as a string.
    return null;
  }

  /**
   * Reads the string value from an hprof Instance.
   * Returns null if the object can't be interpreted as a string.
   */
  public String asString() {
    return asString(-1);
  }

  /**
   * Return the bitmap associated with the given instance, if any.
   * This is relevant for instances of android.graphics.Bitmap and byte[].
   * Returns null if there is no bitmap associated with the given instance.
   */
  public BufferedImage asBitmap() {
    return null;
  }

  /**
   * Returns a sample path from a GC root to this instance.
   * This instance is included as the last element of the path with an empty
   * field description.
   */
  public List<PathElement> getPathFromGcRoot() {
    List<PathElement> path = new ArrayList<PathElement>();

    AhatInstance dom = this;
    for (PathElement elem = new PathElement(this, ""); elem != null;
        elem = getNextPathElementToGcRoot(elem.instance)) {
      if (elem.instance.equals(dom)) {
        elem.isDominator = true;
        dom = dom.getImmediateDominator();
      }
      path.add(elem);
    }
    Collections.reverse(path);
    return path;
  }

  /**
   * Returns the next instance to GC root from this object and a string
   * description of which field of that object refers to the given instance.
   * Returns null if the given instance has no next instance to the gc root.
   */
  private static PathElement getNextPathElementToGcRoot(AhatInstance inst) {
    AhatInstance parent = inst.mNextInstanceToGcRoot;
    if (parent == null) {
      return null;
    }
    return new PathElement(inst.mNextInstanceToGcRoot, inst.mNextInstanceToGcRootField);
  }

  void setNextInstanceToGcRoot(AhatInstance inst, String field) {
    if (mNextInstanceToGcRoot == null && !isRoot()) {
      mNextInstanceToGcRoot = inst;
      mNextInstanceToGcRootField = field;
    }
  }

  /** Returns a human-readable identifier for this object.
   * For class objects, the string is the class name.
   * For class instances, the string is the class name followed by '@' and the
   * hex id of the instance.
   * For array instances, the string is the array type followed by the size in
   * square brackets, followed by '@' and the hex id of the instance.
   */
  @Override public abstract String toString();

  /**
   * Read the byte[] value from an hprof Instance.
   * Returns null if the instance is not a byte array.
   */
  byte[] asByteArray() {
    return null;
  }

  public void setBaseline(AhatInstance baseline) {
    mBaseline = baseline;
  }

  @Override public AhatInstance getBaseline() {
    return mBaseline;
  }

  @Override public boolean isPlaceHolder() {
    return false;
  }

  /**
   * Returns a new place holder instance corresponding to this instance.
   */
  AhatInstance newPlaceHolderInstance() {
    return new AhatPlaceHolderInstance(this);
  }

  /**
   * Initialize the reverse reference fields of this instance and all other
   * instances reachable from it. Initializes the following fields:
   *   mNextInstanceToGcRoot
   *   mNextInstanceToGcRootField
   *   mHardReverseReferences
   *   mSoftReverseReferences
   */
  static void computeReverseReferences(AhatInstance root) {
    // Do a breadth first search to visit the nodes.
    Queue<Reference> bfs = new ArrayDeque<Reference>();
    for (Reference ref : root.getReferences()) {
      bfs.add(ref);
    }
    while (!bfs.isEmpty()) {
      Reference ref = bfs.poll();

      if (ref.ref.mHardReverseReferences == null) {
        // This is the first time we are seeing ref.ref.
        ref.ref.mNextInstanceToGcRoot = ref.src;
        ref.ref.mNextInstanceToGcRootField = ref.field;
        ref.ref.mHardReverseReferences = new ArrayList<AhatInstance>();
        for (Reference childRef : ref.ref.getReferences()) {
          bfs.add(childRef);
        }
      }

      // Note: ref.src is null when the src is the SuperRoot.
      if (ref.src != null) {
        if (ref.strong) {
          ref.ref.mHardReverseReferences.add(ref.src);
        } else {
          if (ref.ref.mSoftReverseReferences == null) {
            ref.ref.mSoftReverseReferences = new ArrayList<AhatInstance>();
          }
          ref.ref.mSoftReverseReferences.add(ref.src);
        }
      }
    }
  }

  /**
   * Recursively compute the retained size of the given instance and all
   * other instances it dominates.
   */
  static void computeRetainedSize(AhatInstance inst, int numHeaps) {
    // Note: We can't use a recursive implementation because it can lead to
    // stack overflow. Use an iterative implementation instead.
    //
    // Objects not yet processed will have mRetainedSizes set to null.
    // Once prepared, an object will have mRetaiedSizes set to an array of 0
    // sizes.
    Deque<AhatInstance> deque = new ArrayDeque<AhatInstance>();
    deque.push(inst);

    while (!deque.isEmpty()) {
      inst = deque.pop();
      if (inst.mRetainedSizes == null) {
        inst.mRetainedSizes = new Size[numHeaps];
        for (int i = 0; i < numHeaps; i++) {
          inst.mRetainedSizes[i] = Size.ZERO;
        }
        if (!(inst instanceof SuperRoot)) {
          inst.mRetainedSizes[inst.mHeap.getIndex()] =
            inst.mRetainedSizes[inst.mHeap.getIndex()].plus(inst.mSize);
        }
        deque.push(inst);
        for (AhatInstance dominated : inst.mDominated) {
          deque.push(dominated);
        }
      } else {
        for (AhatInstance dominated : inst.mDominated) {
          for (int i = 0; i < numHeaps; i++) {
            inst.mRetainedSizes[i] = inst.mRetainedSizes[i].plus(dominated.mRetainedSizes[i]);
          }
        }
      }
    }
  }

  @Override
  public void setDominatorsComputationState(Object state) {
    mDominatorsComputationState = state;
  }

  @Override
  public Object getDominatorsComputationState() {
    return mDominatorsComputationState;
  }

  @Override
  public Iterable<? extends DominatorsComputation.Node> getReferencesForDominators() {
    return new DominatorReferenceIterator(getReferences());
  }

  @Override
  public void setDominator(DominatorsComputation.Node dominator) {
    mImmediateDominator = (AhatInstance)dominator;
    mImmediateDominator.mDominated.add(this);
  }
}
