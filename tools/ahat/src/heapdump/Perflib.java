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

import com.android.tools.perflib.heap.ClassInstance;
import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Instance;
import com.android.tools.perflib.heap.Snapshot;
import java.util.HashMap;
import java.util.Map;

/**
 * Collection of utilities that may be suitable to have in perflib instead of
 * ahat.
 */
public class Perflib {
  /**
   * Return a collection of instances in the given snapshot that are tied to
   * registered native allocations and their corresponding registered native
   * sizes.
   */
  public static Map<Instance, Long> getRegisteredNativeAllocations(Snapshot snapshot) {
    Map<Instance, Long> allocs = new HashMap<Instance, Long>();
    ClassObj cleanerClass = snapshot.findClass("sun.misc.Cleaner");
    if (cleanerClass != null) {
      for (Instance cleanerInst : cleanerClass.getInstancesList()) {
        ClassInstance cleaner = (ClassInstance)cleanerInst;
        Object referent = getField(cleaner, "referent");
        if (referent instanceof Instance) {
          Instance inst = (Instance)referent;
          Object thunkValue = getField(cleaner, "thunk");
          if (thunkValue instanceof ClassInstance) {
            ClassInstance thunk = (ClassInstance)thunkValue;
            ClassObj thunkClass = thunk.getClassObj();
            String cleanerThunkClassName = "libcore.util.NativeAllocationRegistry$CleanerThunk";
            if (thunkClass != null && cleanerThunkClassName.equals(thunkClass.getClassName())) {
              for (ClassInstance.FieldValue thunkField : thunk.getValues()) {
                if (thunkField.getValue() instanceof ClassInstance) {
                  ClassInstance registry = (ClassInstance)thunkField.getValue();
                  ClassObj registryClass = registry.getClassObj();
                  String registryClassName = "libcore.util.NativeAllocationRegistry";
                  if (registryClass != null
                      && registryClassName.equals(registryClass.getClassName())) {
                    Object sizeValue = getField(registry, "size");
                    if (sizeValue instanceof Long) {
                      long size = (Long)sizeValue;
                      if (size > 0) {
                        Long old = allocs.get(inst);
                        allocs.put(inst, old == null ? size : old + size);
                      }
                    }
                    break;
                  }
                }
              }
            }
          }
        }
      }
    }
    return allocs;
  }

  /**
   * Helper function to read a single field from a perflib class instance.
   * Returns null if field not found. Note there is no way to distinguish
   * between field not found an a field value of null.
   */
  private static Object getField(ClassInstance cls, String name) {
    for (ClassInstance.FieldValue field : cls.getValues()) {
      if (name.equals(field.getField().getName())) {
        return field.getValue();
      }
    }
    return null;
  }
}
