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
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAHAoABgAOCQAPABAIABEKABIAEwcAFAcAFQEABjxpbml0PgEAAygpVgEABENvZGUB" +
    "AA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwA" +
    "BwAIBwAWDAAXABgBAAdHb29kYnllBwAZDAAaABsBAAlUcmFuc2Zvcm0BABBqYXZhL2xhbmcvT2Jq" +
    "ZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwEAE2ph" +
    "dmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZhL2xhbmcvU3RyaW5nOylWACAABQAG" +
    "AAAAAAACAAAABwAIAAEACQAAAB0AAQABAAAABSq3AAGxAAAAAQAKAAAABgABAAAAEQABAAsACAAB" +
    "AAkAAAAlAAIAAQAAAAmyAAISA7YABLEAAAABAAoAAAAKAAIAAAATAAgAFAABAAwAAAACAA0=");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
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
    System.loadLibrary(args[1]);
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    t.sayHi();
    addCommonTransformationResult("Transform", CLASS_BYTES, DEX_BYTES);
    enableCommonRetransformation(true);
    doCommonClassRetransformation(Transform.class);
    t.sayHi();
  }

  // Transforms the class
  private static native void doCommonClassRetransformation(Class<?>... target);
  private static native void enableCommonRetransformation(boolean enable);
  private static native void addCommonTransformationResult(String target_name,
                                                           byte[] class_bytes,
                                                           byte[] dex_bytes);
}
