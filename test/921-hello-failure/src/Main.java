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

import java.util.ArrayList;
public class Main {

  public static void main(String[] args) {
    NewName.doTest(new Transform());
    DifferentAccess.doTest(new Transform());
    NewInterface.doTest(new Transform2());
    MissingInterface.doTest(new Transform2());
    ReorderInterface.doTest(new Transform2());
    MultiRedef.doTest(new Transform(), new Transform2());
    MultiRetrans.doTest(new Transform(), new Transform2());
  }

  // Transforms the class. This throws an exception if something goes wrong.
  public static native void doCommonClassRedefinition(Class<?> target,
                                                      byte[] classfile,
                                                      byte[] dexfile) throws Exception;

  public static void doMultiClassRedefinition(CommonClassDefinition... defs) throws Exception {
    ArrayList<Class<?>> classes = new ArrayList<>();
    ArrayList<byte[]> class_files = new ArrayList<>();
    ArrayList<byte[]> dex_files = new ArrayList<>();

    for (CommonClassDefinition d : defs) {
      classes.add(d.target);
      class_files.add(d.class_file_bytes);
      dex_files.add(d.dex_file_bytes);
    }
    doCommonMultiClassRedefinition(classes.toArray(new Class<?>[0]),
                                   class_files.toArray(new byte[0][]),
                                   dex_files.toArray(new byte[0][]));
  }

  public static void addMultiTransformationResults(CommonClassDefinition... defs) throws Exception {
    for (CommonClassDefinition d : defs) {
      addCommonTransformationResult(d.target.getCanonicalName(),
                                    d.class_file_bytes,
                                    d.dex_file_bytes);
    }
  }

  public static native void doCommonMultiClassRedefinition(Class<?>[] targets,
                                                           byte[][] classfiles,
                                                           byte[][] dexfiles) throws Exception;
  public static native void doCommonClassRetransformation(Class<?>... target) throws Exception;
  public static native void enableCommonRetransformation(boolean enable);
  public static native void addCommonTransformationResult(String target_name,
                                                          byte[] class_bytes,
                                                          byte[] dex_bytes);
}
