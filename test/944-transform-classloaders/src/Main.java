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

import java.util.Arrays;
import java.util.ArrayList;
import java.util.Base64;
import java.lang.reflect.*;
public class Main {

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static CommonClassDefinition TRANSFORM_DEFINITION = new CommonClassDefinition(
      Transform.class,
      Base64.getDecoder().decode(
        "yv66vgAAADQAHAoABgAOCQAPABAIABEKABIAEwcAFAcAFQEABjxpbml0PgEAAygpVgEABENvZGUB" +
        "AA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwA" +
        "BwAIBwAWDAAXABgBAAdHb29kYnllBwAZDAAaABsBAAlUcmFuc2Zvcm0BABBqYXZhL2xhbmcvT2Jq" +
        "ZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwEAE2ph" +
        "dmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZhL2xhbmcvU3RyaW5nOylWACAABQAG" +
        "AAAAAAACAAAABwAIAAEACQAAAB0AAQABAAAABSq3AAGxAAAAAQAKAAAABgABAAAAEQABAAsACAAB" +
        "AAkAAAAlAAIAAQAAAAmyAAISA7YABLEAAAABAAoAAAAKAAIAAAATAAgAFAABAAwAAAACAA0="),
      Base64.getDecoder().decode(
        "ZGV4CjAzNQCLXSBQ5FiS3f16krSYZFF8xYZtFVp0GRXMAgAAcAAAAHhWNBIAAAAAAAAAACwCAAAO" +
        "AAAAcAAAAAYAAACoAAAAAgAAAMAAAAABAAAA2AAAAAQAAADgAAAAAQAAAAABAACsAQAAIAEAAGIB" +
        "AABqAQAAcwEAAIABAACXAQAAqwEAAL8BAADTAQAA4wEAAOYBAADqAQAA/gEAAAMCAAAMAgAAAgAA" +
        "AAMAAAAEAAAABQAAAAYAAAAIAAAACAAAAAUAAAAAAAAACQAAAAUAAABcAQAABAABAAsAAAAAAAAA" +
        "AAAAAAAAAAANAAAAAQABAAwAAAACAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAHAAAAAAAAAB4CAAAA" +
        "AAAAAQABAAEAAAATAgAABAAAAHAQAwAAAA4AAwABAAIAAAAYAgAACQAAAGIAAAAbAQEAAABuIAIA" +
        "EAAOAAAAAQAAAAMABjxpbml0PgAHR29vZGJ5ZQALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50" +
        "U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAEkxqYXZhL2xh" +
        "bmcvU3lzdGVtOwAOVHJhbnNmb3JtLmphdmEAAVYAAlZMABJlbWl0dGVyOiBqYWNrLTMuMzYAA291" +
        "dAAHcHJpbnRsbgAFc2F5SGkAEQAHDgATAAcOhQAAAAEBAICABKACAQG4Ag0AAAAAAAAAAQAAAAAA" +
        "AAABAAAADgAAAHAAAAACAAAABgAAAKgAAAADAAAAAgAAAMAAAAAEAAAAAQAAANgAAAAFAAAABAAA" +
        "AOAAAAAGAAAAAQAAAAABAAABIAAAAgAAACABAAABEAAAAQAAAFwBAAACIAAADgAAAGIBAAADIAAA" +
        "AgAAABMCAAAAIAAAAQAAAB4CAAAAEAAAAQAAACwCAAA="));

  /**
   * base64 encoded class/dex file for
   * class Transform2 {
   *   public void sayHi() {
   *    System.out.println("Goodbye2");
   *   }
   * }
   */
  private static CommonClassDefinition TRANSFORM2_DEFINITION = new CommonClassDefinition(
      Transform2.class,
      Base64.getDecoder().decode(
        "yv66vgAAADQAHAoABgAOCQAPABAIABEKABIAEwcAFAcAFQEABjxpbml0PgEAAygpVgEABENvZGUB" +
        "AA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEAClNvdXJjZUZpbGUBAA9UcmFuc2Zvcm0yLmphdmEM" +
        "AAcACAcAFgwAFwAYAQAIR29vZGJ5ZTIHABkMABoAGwEAClRyYW5zZm9ybTIBABBqYXZhL2xhbmcv" +
        "T2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwEA" +
        "E2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZhL2xhbmcvU3RyaW5nOylWACAA" +
        "BQAGAAAAAAACAAAABwAIAAEACQAAAB0AAQABAAAABSq3AAGxAAAAAQAKAAAABgABAAAAAQABAAsA" +
        "CAABAAkAAAAlAAIAAQAAAAmyAAISA7YABLEAAAABAAoAAAAKAAIAAAADAAgABAABAAwAAAACAA0="),
      Base64.getDecoder().decode(
        "ZGV4CjAzNQABX6vL8OT7aGLjbzFBEfCM9Aaz+zzGzVnQAgAAcAAAAHhWNBIAAAAAAAAAADACAAAO" +
        "AAAAcAAAAAYAAACoAAAAAgAAAMAAAAABAAAA2AAAAAQAAADgAAAAAQAAAAABAACwAQAAIAEAAGIB" +
        "AABqAQAAdAEAAIIBAACZAQAArQEAAMEBAADVAQAA5gEAAOkBAADtAQAAAQIAAAYCAAAPAgAAAgAA" +
        "AAMAAAAEAAAABQAAAAYAAAAIAAAACAAAAAUAAAAAAAAACQAAAAUAAABcAQAABAABAAsAAAAAAAAA" +
        "AAAAAAAAAAANAAAAAQABAAwAAAACAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAHAAAAAAAAACECAAAA" +
        "AAAAAQABAAEAAAAWAgAABAAAAHAQAwAAAA4AAwABAAIAAAAbAgAACQAAAGIAAAAbAQEAAABuIAIA" +
        "EAAOAAAAAQAAAAMABjxpbml0PgAIR29vZGJ5ZTIADExUcmFuc2Zvcm0yOwAVTGphdmEvaW8vUHJp" +
        "bnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZhL2xhbmcvU3RyaW5nOwASTGphdmEv" +
        "bGFuZy9TeXN0ZW07AA9UcmFuc2Zvcm0yLmphdmEAAVYAAlZMABJlbWl0dGVyOiBqYWNrLTQuMjQA" +
        "A291dAAHcHJpbnRsbgAFc2F5SGkAAQAHDgADAAcOhwAAAAEBAICABKACAQG4AgANAAAAAAAAAAEA" +
        "AAAAAAAAAQAAAA4AAABwAAAAAgAAAAYAAACoAAAAAwAAAAIAAADAAAAABAAAAAEAAADYAAAABQAA" +
        "AAQAAADgAAAABgAAAAEAAAAAAQAAASAAAAIAAAAgAQAAARAAAAEAAABcAQAAAiAAAA4AAABiAQAA" +
        "AyAAAAIAAAAWAgAAACAAAAEAAAAhAgAAABAAAAEAAAAwAgAA"));

  public static void main(String[] args) throws Exception {
    doTest();
    System.out.println("Passed");
  }

  private static void checkIsInstance(Class<?> klass, Object o) throws Exception {
    if (!klass.isInstance(o)) {
      throw new Exception(klass + " is not the class of " + o);
    }
  }

  private static boolean arrayContains(long[] arr, long value) {
    if (arr == null) {
      return false;
    }
    for (int i = 0; i < arr.length; i++) {
      if (arr[i] == value) {
        return true;
      }
    }
    return false;
  }

  /**
   * Checks that we can find the dex-file for the given class in its classloader.
   *
   * Throws if it fails.
   */
  private static void checkDexFileInClassLoader(Class<?> klass) throws Exception {
    // If all the android BCP classes were availible when compiling this test and access checks
    // weren't a thing this function would be written as follows:
    //
    // long dexFilePtr = getDexFilePointer(klass);
    // dalvik.system.BaseDexClassLoader loader =
    //     (dalvik.system.BaseDexClassLoader)klass.getClassLoader();
    // dalvik.system.DexPathList pathListValue = loader.pathList;
    // dalvik.system.DexPathList.Element[] elementArrayValue = pathListValue.dexElements;
    // int array_length = elementArrayValue.length;
    // for (int i = 0; i < array_length; i++) {
    //   dalvik.system.DexPathList.Element curElement = elementArrayValue[i];
    //   dalvik.system.DexFile curDexFile = curElement.dexFile;
    //   if (curDexFile == null) {
    //     continue;
    //   }
    //   long[] curCookie = (long[])curDexFile.mCookie;
    //   long[] curInternalCookie = (long[])curDexFile.mInternalCookie;
    //   if (arrayContains(curCookie, dexFilePtr) || arrayContains(curInternalCookie, dexFilePtr)) {
    //     return;
    //   }
    // }
    // throw new Exception(
    //     "Unable to find dex file pointer " + dexFilePtr + " in class loader for " + klass);

    // Get all the fields and classes we need by reflection.
    Class<?> baseDexClassLoaderClass = Class.forName("dalvik.system.BaseDexClassLoader");
    Field pathListField = baseDexClassLoaderClass.getDeclaredField("pathList");

    Class<?> dexPathListClass = Class.forName("dalvik.system.DexPathList");
    Field elementArrayField = dexPathListClass.getDeclaredField("dexElements");

    Class<?> dexPathListElementClass = Class.forName("dalvik.system.DexPathList$Element");
    Field dexFileField = dexPathListElementClass.getDeclaredField("dexFile");

    Class<?> dexFileClass = Class.forName("dalvik.system.DexFile");
    Field dexFileCookieField = dexFileClass.getDeclaredField("mCookie");
    Field dexFileInternalCookieField = dexFileClass.getDeclaredField("mInternalCookie");

    // Make all the fields accessible
    AccessibleObject.setAccessible(new AccessibleObject[] { pathListField,
                                                            elementArrayField,
                                                            dexFileField,
                                                            dexFileCookieField,
                                                            dexFileInternalCookieField }, true);

    long dexFilePtr = getDexFilePointer(klass);

    ClassLoader loader = klass.getClassLoader();
    checkIsInstance(baseDexClassLoaderClass, loader);
    // DexPathList pathListValue = ((BaseDexClassLoader) loader).pathList;
    Object pathListValue = pathListField.get(loader);

    checkIsInstance(dexPathListClass, pathListValue);

    // DexPathList.Element[] elementArrayValue = pathListValue.dexElements;
    Object elementArrayValue = elementArrayField.get(pathListValue);
    if (!elementArrayValue.getClass().isArray() ||
        elementArrayValue.getClass().getComponentType() != dexPathListElementClass) {
      throw new Exception("elementArrayValue is not an " + dexPathListElementClass + " array!");
    }
    // int array_length = elementArrayValue.length;
    int array_length = Array.getLength(elementArrayValue);
    for (int i = 0; i < array_length; i++) {
      // DexPathList.Element curElement = elementArrayValue[i];
      Object curElement = Array.get(elementArrayValue, i);
      checkIsInstance(dexPathListElementClass, curElement);

      // DexFile curDexFile = curElement.dexFile;
      Object curDexFile = dexFileField.get(curElement);
      if (curDexFile == null) {
        continue;
      }
      checkIsInstance(dexFileClass, curDexFile);

      // long[] curCookie = (long[])curDexFile.mCookie;
      long[] curCookie = (long[])dexFileCookieField.get(curDexFile);
      // long[] curInternalCookie = (long[])curDexFile.mInternalCookie;
      long[] curInternalCookie = (long[])dexFileInternalCookieField.get(curDexFile);

      if (arrayContains(curCookie, dexFilePtr) || arrayContains(curInternalCookie, dexFilePtr)) {
        return;
      }
    }
    throw new Exception(
        "Unable to find dex file pointer " + dexFilePtr + " in class loader for " + klass);
  }

  private static void doTest() throws Exception {
    Transform t = new Transform();
    Transform2 t2 = new Transform2();

    long initial_t1_dex = getDexFilePointer(Transform.class);
    long initial_t2_dex = getDexFilePointer(Transform2.class);
    if (initial_t2_dex != initial_t1_dex) {
      throw new Exception("The classes " + Transform.class + " and " + Transform2.class + " " +
                          "have different initial dex files!");
    }
    checkDexFileInClassLoader(Transform.class);
    checkDexFileInClassLoader(Transform2.class);

    // Make sure they are loaded
    t.sayHi();
    t2.sayHi();
    // Redefine both of the classes.
    doMultiClassRedefinition(TRANSFORM_DEFINITION, TRANSFORM2_DEFINITION);
    // Make sure we actually transformed them!
    t.sayHi();
    t2.sayHi();

    long final_t1_dex = getDexFilePointer(Transform.class);
    long final_t2_dex = getDexFilePointer(Transform2.class);
    if (final_t2_dex == final_t1_dex) {
      throw new Exception("The classes " + Transform.class + " and " + Transform2.class + " " +
                          "have the same initial dex files!");
    } else if (final_t1_dex == initial_t1_dex) {
      throw new Exception("The class " + Transform.class + " did not get a new dex file!");
    } else if (final_t2_dex == initial_t2_dex) {
      throw new Exception("The class " + Transform2.class + " did not get a new dex file!");
    }
    // Check to make sure the new dex files are in the class loader.
    checkDexFileInClassLoader(Transform.class);
    checkDexFileInClassLoader(Transform2.class);
  }

  private static void doMultiClassRedefinition(CommonClassDefinition... defs) {
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

  // Gets the 'long' (really a native pointer) that is stored in the ClassLoader representing the
  // DexFile a class is loaded from. This is converted from the DexFile* in the same way it is done
  // in runtime/native/dalvik_system_DexFile.cc
  private static native long getDexFilePointer(Class<?> target);
  // Transforms the classes
  private static native void doCommonMultiClassRedefinition(Class<?>[] targets,
                                                            byte[][] classfiles,
                                                            byte[][] dexfiles);
}
