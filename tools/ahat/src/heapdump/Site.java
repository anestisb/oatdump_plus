/*
 * Copyright (C) 2015 The Android Open Source Project
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

import com.android.tools.perflib.heap.StackFrame;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Site implements Diffable<Site> {
  // The site that this site was directly called from.
  // mParent is null for the root site.
  private Site mParent;

  private String mMethodName;
  private String mSignature;
  private String mFilename;
  private int mLineNumber;

  // To identify this site, we pick a stack trace that includes the site.
  // mId is the id of an object allocated at that stack trace, and mDepth
  // is the number of calls between this site and the innermost site of
  // allocation of the object with mId.
  // For the root site, mId is 0 and mDepth is 0.
  private long mId;
  private int mDepth;

  // The total size of objects allocated in this site (including child sites),
  // organized by heap index. Computed as part of computeObjectsInfos.
  private Size[] mSizesByHeap;

  // List of child sites.
  private List<Site> mChildren;

  // List of objects allocated at this site (not including child sites).
  private List<AhatInstance> mObjects;

  private List<ObjectsInfo> mObjectsInfos;
  private Map<AhatHeap, Map<AhatClassObj, ObjectsInfo>> mObjectsInfoMap;

  private Site mBaseline;

  public static class ObjectsInfo implements Diffable<ObjectsInfo> {
    public AhatHeap heap;
    public AhatClassObj classObj;   // May be null.
    public long numInstances;
    public Size numBytes;
    private ObjectsInfo baseline;

    /**
     * Construct a new, empty objects info for the given heap and class
     * combination.
     */
    public ObjectsInfo(AhatHeap heap, AhatClassObj classObj) {
      this.heap = heap;
      this.classObj = classObj;
      this.numInstances = 0;
      this.numBytes = Size.ZERO;
      this.baseline = this;
    }

    /**
     * Returns the name of the class this ObjectsInfo is associated with.
     */
    public String getClassName() {
      return classObj == null ? "???" : classObj.getName();
    }

    public void setBaseline(ObjectsInfo baseline) {
      this.baseline = baseline;
    }

    @Override public ObjectsInfo getBaseline() {
      return baseline;
    }

    @Override public boolean isPlaceHolder() {
      return false;
    }
  }

  /**
   * Construct a root site.
   */
  public Site(String name) {
    this(null, name, "", "", 0, 0, 0);
  }

  public Site(Site parent, String method, String signature, String file,
      int line, long id, int depth) {
    mParent = parent;
    mMethodName = method;
    mSignature = signature;
    mFilename = file;
    mLineNumber = line;
    mId = id;
    mDepth = depth;
    mChildren = new ArrayList<Site>();
    mObjects = new ArrayList<AhatInstance>();
    mObjectsInfos = new ArrayList<ObjectsInfo>();
    mObjectsInfoMap = new HashMap<AhatHeap, Map<AhatClassObj, ObjectsInfo>>();
    mBaseline = this;
  }

  /**
   * Add an instance to this site.
   * Returns the site at which the instance was allocated.
   * @param frames - The list of frames in the stack trace, starting with the inner-most frame.
   * @param depth - The number of frames remaining before the inner-most frame is reached.
   */
  Site add(StackFrame[] frames, int depth, AhatInstance inst) {
    return add(this, frames, depth, inst);
  }

  private static Site add(Site site, StackFrame[] frames, int depth, AhatInstance inst) {
    while (depth > 0) {
      StackFrame next = frames[depth - 1];
      Site child = null;
      for (int i = 0; i < site.mChildren.size(); i++) {
        Site curr = site.mChildren.get(i);
        if (curr.mLineNumber == next.getLineNumber()
            && curr.mMethodName.equals(next.getMethodName())
            && curr.mSignature.equals(next.getSignature())
            && curr.mFilename.equals(next.getFilename())) {
          child = curr;
          break;
        }
      }
      if (child == null) {
        child = new Site(site, next.getMethodName(), next.getSignature(),
            next.getFilename(), next.getLineNumber(), inst.getId(), depth - 1);
        site.mChildren.add(child);
      }
      depth = depth - 1;
      site = child;
    }
    site.mObjects.add(inst);
    return site;
  }

  /**
   * Recompute the ObjectsInfos for this and all child sites.
   * This should be done after the sites tree has been formed. It should also
   * be done after dominators computation has been performed to ensure only
   * reachable objects are included in the ObjectsInfos.
   *
   * @param numHeaps - The number of heaps in the heap dump.
   */
  void computeObjectsInfos(int numHeaps) {
    // Count up the total sizes by heap.
    mSizesByHeap = new Size[numHeaps];
    for (int i = 0; i < numHeaps; ++i) {
      mSizesByHeap[i] = Size.ZERO;
    }

    // Add all reachable objects allocated at this site.
    for (AhatInstance inst : mObjects) {
      if (inst.isReachable()) {
        AhatHeap heap = inst.getHeap();
        Size size = inst.getSize();
        ObjectsInfo info = getObjectsInfo(heap, inst.getClassObj());
        info.numInstances++;
        info.numBytes = info.numBytes.plus(size);
        mSizesByHeap[heap.getIndex()] = mSizesByHeap[heap.getIndex()].plus(size);
      }
    }

    // Add objects allocated in child sites.
    for (Site child : mChildren) {
      child.computeObjectsInfos(numHeaps);
      for (ObjectsInfo childInfo : child.mObjectsInfos) {
        ObjectsInfo info = getObjectsInfo(childInfo.heap, childInfo.classObj);
        info.numInstances += childInfo.numInstances;
        info.numBytes = info.numBytes.plus(childInfo.numBytes);
      }
      for (int i = 0; i < numHeaps; ++i) {
        mSizesByHeap[i] = mSizesByHeap[i].plus(child.mSizesByHeap[i]);
      }
    }
  }

  // Get the size of a site for a specific heap.
  public Size getSize(AhatHeap heap) {
    return mSizesByHeap[heap.getIndex()];
  }

  /**
   * Collect the objects allocated under this site, optionally filtered by
   * heap name or class name. Includes objects allocated in children sites.
   * @param heapName - The name of the heap the collected objects should
   *                   belong to. This may be null to indicate objects of
   *                   every heap should be collected.
   * @param className - The name of the class the collected objects should
   *                    belong to. This may be null to indicate objects of
   *                    every class should be collected.
   * @param objects - Out parameter. A collection of objects that all
   *                  collected objects should be added to.
   */
  public void getObjects(String heapName, String className, Collection<AhatInstance> objects) {
    for (AhatInstance inst : mObjects) {
      if ((heapName == null || inst.getHeap().getName().equals(heapName))
          && (className == null || inst.getClassName().equals(className))) {
        objects.add(inst);
      }
    }

    // Recursively visit children. Recursion should be okay here because the
    // stack depth is limited by a reasonable amount (128 frames or so).
    for (Site child : mChildren) {
      child.getObjects(heapName, className, objects);
    }
  }

  /**
   * Returns the ObjectsInfo at this site for the given heap and class
   * objects. Creates a new empty ObjectsInfo if none existed before.
   */
  ObjectsInfo getObjectsInfo(AhatHeap heap, AhatClassObj classObj) {
    Map<AhatClassObj, ObjectsInfo> classToObjectsInfo = mObjectsInfoMap.get(heap);
    if (classToObjectsInfo == null) {
      classToObjectsInfo = new HashMap<AhatClassObj, ObjectsInfo>();
      mObjectsInfoMap.put(heap, classToObjectsInfo);
    }

    ObjectsInfo info = classToObjectsInfo.get(classObj);
    if (info == null) {
      info = new ObjectsInfo(heap, classObj);
      mObjectsInfos.add(info);
      classToObjectsInfo.put(classObj, info);
    }
    return info;
  }

  public List<ObjectsInfo> getObjectsInfos() {
    return mObjectsInfos;
  }

  // Get the combined size of the site for all heaps.
  public Size getTotalSize() {
    Size total = Size.ZERO;
    for (Size size : mSizesByHeap) {
      total = total.plus(size);
    }
    return total;
  }

  /**
   * Return the site this site was called from.
   * Returns null for the root site.
   */
  public Site getParent() {
    return mParent;
  }

  public String getMethodName() {
    return mMethodName;
  }

  public String getSignature() {
    return mSignature;
  }

  public String getFilename() {
    return mFilename;
  }

  public int getLineNumber() {
    return mLineNumber;
  }

  /**
   * Returns the id of some object allocated in this site.
   */
  public long getId() {
    return mId;
  }

  /**
   * Returns the number of frames between this site and the site where the
   * object with id getId() was allocated.
   */
  public int getDepth() {
    return mDepth;
  }

  public List<Site> getChildren() {
    return mChildren;
  }

  void setBaseline(Site baseline) {
    mBaseline = baseline;
  }

  @Override public Site getBaseline() {
    return mBaseline;
  }

  @Override public boolean isPlaceHolder() {
    return false;
  }

  /**
   * Adds a place holder instance to this site and all parent sites.
   */
  void addPlaceHolderInstance(AhatInstance placeholder) {
    for (Site site = this; site != null; site = site.mParent) {
      site.mObjects.add(placeholder);
    }
  }
}
