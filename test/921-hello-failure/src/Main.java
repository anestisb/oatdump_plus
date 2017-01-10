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

public class Main {

  public static void main(String[] args) {
    System.loadLibrary(args[1]);
    NewName.doTest(new Transform());
    DifferentAccess.doTest(new Transform());
    NewInterface.doTest(new Transform2());
    MissingInterface.doTest(new Transform2());
    ReorderInterface.doTest(new Transform2());
  }

  // Transforms the class. This throws an exception if something goes wrong.
  public static native void doCommonClassRedefinition(Class<?> target,
                                                      byte[] classfile,
                                                      byte[] dexfile) throws Exception;
}
