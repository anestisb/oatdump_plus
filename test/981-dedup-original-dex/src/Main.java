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

import java.lang.reflect.Field;
import java.util.Base64;

import dalvik.system.ClassExt;

public class Main {

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static final byte[] DEX_BYTES_1 = Base64.getDecoder().decode(
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
    "AgAAABMCAAAAIAAAAQAAAB4CAAAAEAAAAQAAACwCAAA=");

  /**
   * base64 encoded class/dex file for
   * class Transform2 {
   *   public void sayHi() {
   *    System.out.println("Goodbye2");
   *   }
   * }
   */
  private static final byte[] DEX_BYTES_2 = Base64.getDecoder().decode(
    "ZGV4CjAzNQAjXDED2iflQ3NXbPtBRVjQVMqoDU9nDz/QAgAAcAAAAHhWNBIAAAAAAAAAADACAAAO" +
    "AAAAcAAAAAYAAACoAAAAAgAAAMAAAAABAAAA2AAAAAQAAADgAAAAAQAAAAABAACwAQAAIAEAAGIB" +
    "AABqAQAAdAEAAIIBAACZAQAArQEAAMEBAADVAQAA5gEAAOkBAADtAQAAAQIAAAYCAAAPAgAAAgAA" +
    "AAMAAAAEAAAABQAAAAYAAAAIAAAACAAAAAUAAAAAAAAACQAAAAUAAABcAQAABAABAAsAAAAAAAAA" +
    "AAAAAAAAAAANAAAAAQABAAwAAAACAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAHAAAAAAAAACECAAAA" +
    "AAAAAQABAAEAAAAWAgAABAAAAHAQAwAAAA4AAwABAAIAAAAbAgAACQAAAGIAAAAbAQEAAABuIAIA" +
    "EAAOAAAAAQAAAAMABjxpbml0PgAIR29vZGJ5ZTIADExUcmFuc2Zvcm0yOwAVTGphdmEvaW8vUHJp" +
    "bnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZhL2xhbmcvU3RyaW5nOwASTGphdmEv" +
    "bGFuZy9TeXN0ZW07AA9UcmFuc2Zvcm0yLmphdmEAAVYAAlZMABJlbWl0dGVyOiBqYWNrLTQuMzAA" +
    "A291dAAHcHJpbnRsbgAFc2F5SGkAAQAHDgADAAcOhwAAAAEBAICABKACAQG4AgANAAAAAAAAAAEA" +
    "AAAAAAAAAQAAAA4AAABwAAAAAgAAAAYAAACoAAAAAwAAAAIAAADAAAAABAAAAAEAAADYAAAABQAA" +
    "AAQAAADgAAAABgAAAAEAAAAAAQAAASAAAAIAAAAgAQAAARAAAAEAAABcAQAAAiAAAA4AAABiAQAA" +
    "AyAAAAIAAAAWAgAAACAAAAEAAAAhAgAAABAAAAEAAAAwAgAA");

  public static void main(String[] args) {
    try {
      doTest();
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  private static void assertSame(Object a, Object b) throws Exception {
    if (a != b) {
      throw new AssertionError("'" + (a != null ? a.toString() : "null") + "' is not the same as " +
                               "'" + (b != null ? b.toString() : "null") + "'");
    }
  }

  private static Object getObjectField(Object o, String name) throws Exception {
    return getObjectField(o, o.getClass(), name);
  }

  private static Object getObjectField(Object o, Class<?> type, String name) throws Exception {
    Field f = type.getDeclaredField(name);
    f.setAccessible(true);
    return f.get(o);
  }

  private static Object getOriginalDexFile(Class<?> k) throws Exception {
    ClassExt ext_data_object = (ClassExt) getObjectField(k, "extData");
    if (ext_data_object == null) {
      return null;
    }

    return getObjectField(ext_data_object, "originalDexFile");
  }

  public static void doTest() throws Exception {
    // Make sure both of these are loaded prior to transformations being added so they have the same
    // original dex files.
    Transform t1 = new Transform();
    Transform2 t2 = new Transform2();

    assertSame(null, getOriginalDexFile(t1.getClass()));
    assertSame(null, getOriginalDexFile(t2.getClass()));
    assertSame(null, getOriginalDexFile(Main.class));

    addCommonTransformationResult("Transform", new byte[0], DEX_BYTES_1);
    addCommonTransformationResult("Transform2", new byte[0], DEX_BYTES_2);
    enableCommonRetransformation(true);
    doCommonClassRetransformation(Transform.class, Transform2.class);

    assertSame(getOriginalDexFile(t1.getClass()), getOriginalDexFile(t2.getClass()));
    assertSame(null, getOriginalDexFile(Main.class));
    // Make sure that the original dex file is a DexCache object.
    assertSame(getOriginalDexFile(t1.getClass()).getClass(), Class.forName("java.lang.DexCache"));

    // Check that we end up with a byte[] if we do a direct RedefineClasses
    enableCommonRetransformation(false);
    doCommonClassRedefinition(Transform.class, new byte[0], DEX_BYTES_1);
    assertSame((new byte[0]).getClass(), getOriginalDexFile(t1.getClass()).getClass());
  }

  // Transforms the class
  private static native void doCommonClassRetransformation(Class<?>... target);
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] class_file,
                                                       byte[] dex_file);
  private static native void enableCommonRetransformation(boolean enable);
  private static native void addCommonTransformationResult(String target_name,
                                                           byte[] class_bytes,
                                                           byte[] dex_bytes);
}
