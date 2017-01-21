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

import java.util.Base64;
public class Main {
  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *    System.out.println("hello");
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES_A = Base64.getDecoder().decode(
      "yv66vgAAADQAHAoABgAOCQAPABAIABEKABIAEwcAFAcAFQEABjxpbml0PgEAAygpVgEABENvZGUB" +
      "AA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwA" +
      "BwAIBwAWDAAXABgBAAVoZWxsbwcAGQwAGgAbAQAJVHJhbnNmb3JtAQAQamF2YS9sYW5nL09iamVj" +
      "dAEAEGphdmEvbGFuZy9TeXN0ZW0BAANvdXQBABVMamF2YS9pby9QcmludFN0cmVhbTsBABNqYXZh" +
      "L2lvL1ByaW50U3RyZWFtAQAHcHJpbnRsbgEAFShMamF2YS9sYW5nL1N0cmluZzspVgAgAAUABgAA" +
      "AAAAAgAAAAcACAABAAkAAAAdAAEAAQAAAAUqtwABsQAAAAEACgAAAAYAAQAAABEAAQALAAgAAQAJ" +
      "AAAAJQACAAEAAAAJsgACEgO2AASxAAAAAQAKAAAACgACAAAAGgAIABsAAQAMAAAAAgAN");
  private static final byte[] DEX_BYTES_A = Base64.getDecoder().decode(
      "ZGV4CjAzNQC6XWInnnDd1H4NdQ3P3inH8eCVmQI6W7LMAgAAcAAAAHhWNBIAAAAAAAAAACwCAAAO" +
      "AAAAcAAAAAYAAACoAAAAAgAAAMAAAAABAAAA2AAAAAQAAADgAAAAAQAAAAABAACsAQAAIAEAAGIB" +
      "AABqAQAAdwEAAI4BAACiAQAAtgEAAMoBAADaAQAA3QEAAOEBAAD1AQAA/AEAAAECAAAKAgAAAQAA" +
      "AAIAAAADAAAABAAAAAUAAAAHAAAABwAAAAUAAAAAAAAACAAAAAUAAABcAQAABAABAAsAAAAAAAAA" +
      "AAAAAAAAAAANAAAAAQABAAwAAAACAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAGAAAAAAAAABwCAAAA" +
      "AAAAAQABAAEAAAARAgAABAAAAHAQAwAAAA4AAwABAAIAAAAWAgAACQAAAGIAAAAbAQoAAABuIAIA" +
      "EAAOAAAAAQAAAAMABjxpbml0PgALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwAS" +
      "TGphdmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAEkxqYXZhL2xhbmcvU3lzdGVt" +
      "OwAOVHJhbnNmb3JtLmphdmEAAVYAAlZMABJlbWl0dGVyOiBqYWNrLTQuMjIABWhlbGxvAANvdXQA" +
      "B3ByaW50bG4ABXNheUhpABEABw4AGgAHDocAAAABAQCAgASgAgEBuAIAAA0AAAAAAAAAAQAAAAAA" +
      "AAABAAAADgAAAHAAAAACAAAABgAAAKgAAAADAAAAAgAAAMAAAAAEAAAAAQAAANgAAAAFAAAABAAA" +
      "AOAAAAAGAAAAAQAAAAABAAABIAAAAgAAACABAAABEAAAAQAAAFwBAAACIAAADgAAAGIBAAADIAAA" +
      "AgAAABECAAAAIAAAAQAAABwCAAAAEAAAAQAAACwCAAA=");

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES_B = Base64.getDecoder().decode(
    "yv66vgAAADQAHAoABgAOCQAPABAIABEKABIAEwcAFAcAFQEABjxpbml0PgEAAygpVgEABENvZGUB" +
    "AA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwA" +
    "BwAIBwAWDAAXABgBAAdHb29kYnllBwAZDAAaABsBAAlUcmFuc2Zvcm0BABBqYXZhL2xhbmcvT2Jq" +
    "ZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwEAE2ph" +
    "dmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZhL2xhbmcvU3RyaW5nOylWACAABQAG" +
    "AAAAAAACAAAABwAIAAEACQAAAB0AAQABAAAABSq3AAGxAAAAAQAKAAAABgABAAAAEQABAAsACAAB" +
    "AAkAAAAlAAIAAQAAAAmyAAISA7YABLEAAAABAAoAAAAKAAIAAAATAAgAFAABAAwAAAACAA0=");
  private static final byte[] DEX_BYTES_B = Base64.getDecoder().decode(
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

  public static void main(String[] args) {
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    // TODO We currently need to do this transform call since we don't have any way to make the
    // original-dex-file a single-class dex-file letting us restore it easily. We should use the
    // manipulation library that is being made when we store the original dex file.
    // TODO REMOVE this theoretically does nothing but it ensures the original-dex-file we have set
    // is one we can return to unaltered.
    doCommonClassRedefinition(Transform.class, CLASS_BYTES_A, DEX_BYTES_A);
    t.sayHi();

    // Now turn it into DEX_BYTES_B so it says 'Goodbye'
    addCommonTransformationResult("Transform", CLASS_BYTES_B, DEX_BYTES_B);
    enableCommonRetransformation(true);
    doCommonClassRetransformation(Transform.class);
    t.sayHi();

    // Now turn it back to normal by removing the load-hook and transforming again.
    enableCommonRetransformation(false);
    doCommonClassRetransformation(Transform.class);
    t.sayHi();
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] class_bytes,
                                                       byte[] dex_bytes);
  private static native void doCommonClassRetransformation(Class<?>... target);
  private static native void enableCommonRetransformation(boolean enable);
  private static native void addCommonTransformationResult(String target_name,
                                                           byte[] class_bytes,
                                                           byte[] dex_bytes);
}
