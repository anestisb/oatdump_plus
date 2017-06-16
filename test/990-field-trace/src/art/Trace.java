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

package art;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

public class Trace {
  public static native void enableTracing(Class<?> methodClass,
                                          Method entryMethod,
                                          Method exitMethod,
                                          Method fieldAccess,
                                          Method fieldModify,
                                          Thread thr);
  public static native void disableTracing(Thread thr);

  public static void enableFieldTracing(Class<?> methodClass,
                                        Method fieldAccess,
                                        Method fieldModify,
                                        Thread thr) {
    enableTracing(methodClass, null, null, fieldAccess, fieldModify, thr);
  }

  public static void enableMethodTracing(Class<?> methodClass,
                                         Method entryMethod,
                                         Method exitMethod,
                                         Thread thr) {
    enableTracing(methodClass, entryMethod, exitMethod, null, null, thr);
  }

  public static native void watchFieldAccess(Field f);
  public static native void watchFieldModification(Field f);
  public static native void watchAllFieldAccesses();
  public static native void watchAllFieldModifications();
}
