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

import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Instance;
import com.android.tools.perflib.heap.RootObj;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

public abstract class AhatInstance implements Diffable<AhatInstance> {
  private long mId;
  private long mSize;
  private long mTotalRetainedSize;
  private long mRetainedSizes[];      // Retained size indexed by heap index
  private boolean mIsReachable;
  private AhatHeap mHeap;
  private AhatInstance mImmediateDominator;
  private AhatInstance mNextInstanceToGcRoot;
  private String mNextInstanceToGcRootField = "???";
  private AhatClassObj mClassObj;
  private AhatInstance[] mHardReverseReferences;
  private AhatInstance[] mSoftReverseReferences;
  private Site mSite;

  // If this instance is a root, mRootTypes contains a set of the root types.
  // If this instance is not a root, mRootTypes is null.
  private List<String> mRootTypes;

  // List of instances this instance immediately dominates.
  private List<AhatInstance> mDominated = new ArrayList<AhatInstance>();

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
  void initialize(AhatSnapshot snapshot, Instance inst) {
    mId = inst.getId();
    mSize = inst.getSize();
    mTotalRetainedSize = inst.getTotalRetainedSize();
    mIsReachable = inst.isReachable();

    List<AhatHeap> heaps = snapshot.getHeaps();
    mRetainedSizes = new long[heaps.size()];
    for (AhatHeap heap : heaps) {
      mRetainedSizes[heap.getIndex()] = inst.getRetainedSize(heap.getIndex());
    }

    mHeap = snapshot.getHeap(inst.getHeap().getName());

    Instance dom = inst.getImmediateDominator();
    if (dom == null || dom instanceof RootObj) {
      mImmediateDominator = null;
    } else {
      mImmediateDominator = snapshot.findInstance(dom.getId());
      mImmediateDominator.mDominated.add(this);
    }

    ClassObj clsObj = inst.getClassObj();
    if (clsObj != null) {
      mClassObj = snapshot.findClassObj(clsObj.getId());
    }

    // A couple notes about reverse references:
    // * perflib sometimes returns unreachable reverse references. If
    //   snapshot.findInstance returns null, it means the reverse reference is
    //   not reachable, so we filter it out.
    // * We store the references as AhatInstance[] instead of
    //   ArrayList<AhatInstance> because it saves a lot of space and helps
    //   with performance when there are a lot of AhatInstances.
    ArrayList<AhatInstance> ahatRefs = new ArrayList<AhatInstance>();
    ahatRefs = new ArrayList<AhatInstance>();
    for (Instance ref : inst.getHardReverseReferences()) {
      AhatInstance ahat = snapshot.findInstance(ref.getId());
      if (ahat != null) {
        ahatRefs.add(ahat);
      }
    }
    mHardReverseReferences = new AhatInstance[ahatRefs.size()];
    ahatRefs.toArray(mHardReverseReferences);

    List<Instance> refs = inst.getSoftReverseReferences();
    ahatRefs.clear();
    if (refs != null) {
      for (Instance ref : refs) {
        AhatInstance ahat = snapshot.findInstance(ref.getId());
        if (ahat != null) {
          ahatRefs.add(ahat);
        }
      }
    }
    mSoftReverseReferences = new AhatInstance[ahatRefs.size()];
    ahatRefs.toArray(mSoftReverseReferences);
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
  public long getSize() {
    return mSize;
  }

  /**
   * Returns the number of bytes belonging to the given heap that this instance
   * retains.
   */
  public long getRetainedSize(AhatHeap heap) {
    int index = heap.getIndex();
    return 0 <= index && index < mRetainedSizes.length ? mRetainedSizes[heap.getIndex()] : 0;
  }

  /**
   * Returns the total number of bytes this instance retains.
   */
  public long getTotalRetainedSize() {
    return mTotalRetainedSize;
  }

  /**
   * Returns whether this object is strongly-reachable.
   */
  public boolean isReachable() {
    return mIsReachable;
  }

  /**
   * Returns the heap that this instance is allocated on.
   */
  public AhatHeap getHeap() {
    return mHeap;
  }

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
   * Sets the allocation site of this instance.
   */
  void setSite(Site site) {
    mSite = site;
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
    return Arrays.asList(mHardReverseReferences);
  }

  /**
   * Returns a list of objects with soft references to this object.
   */
  public List<AhatInstance> getSoftReverseReferences() {
    return Arrays.asList(mSoftReverseReferences);
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
    mNextInstanceToGcRoot = inst;
    mNextInstanceToGcRootField = field;
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
}
