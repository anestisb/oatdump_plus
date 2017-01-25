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

import java.lang.reflect.Method;
import java.util.Base64;

public class Main {

  /**
   * base64 encoded class/dex file for
   * class Transform {
   *   public void sayHi() {
   *     System.out.println("Hello");
   *   }
   *   public void sayGoodbye() {
   *    System.out.println("Goodbye");
   *   }
   * }
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAHwoABwAQCQARABIIABMKABQAFQgAFgcAFwcAGAEABjxpbml0PgEAAygpVgEABENv" +
    "ZGUBAA9MaW5lTnVtYmVyVGFibGUBAAVzYXlIaQEACnNheUdvb2RieWUBAApTb3VyY2VGaWxlAQAO" +
    "VHJhbnNmb3JtLmphdmEMAAgACQcAGQwAGgAbAQAFSGVsbG8HABwMAB0AHgEAB0dvb2RieWUBAAlU" +
    "cmFuc2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxq" +
    "YXZhL2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExq" +
    "YXZhL2xhbmcvU3RyaW5nOylWACAABgAHAAAAAAADAAAACAAJAAEACgAAAB0AAQABAAAABSq3AAGx" +
    "AAAAAQALAAAABgABAAAAAQABAAwACQABAAoAAAAlAAIAAQAAAAmyAAISA7YABLEAAAABAAsAAAAK" +
    "AAIAAAADAAgABAABAA0ACQABAAoAAAAlAAIAAQAAAAmyAAISBbYABLEAAAABAAsAAAAKAAIAAAAG" +
    "AAgABwABAA4AAAACAA8=");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQDpaN+7jX/ZLl9Jr0HAEV7nqL1YDuakKakgAwAAcAAAAHhWNBIAAAAAAAAAAIACAAAQ" +
    "AAAAcAAAAAYAAACwAAAAAgAAAMgAAAABAAAA4AAAAAUAAADoAAAAAQAAABABAADwAQAAMAEAAJYB" +
    "AACeAQAApwEAAK4BAAC7AQAA0gEAAOYBAAD6AQAADgIAAB4CAAAhAgAAJQIAADkCAAA+AgAARwIA" +
    "AFMCAAADAAAABAAAAAUAAAAGAAAABwAAAAkAAAAJAAAABQAAAAAAAAAKAAAABQAAAJABAAAEAAEA" +
    "DAAAAAAAAAAAAAAAAAAAAA4AAAAAAAAADwAAAAEAAQANAAAAAgAAAAAAAAAAAAAAAAAAAAIAAAAA" +
    "AAAACAAAAAAAAABrAgAAAAAAAAEAAQABAAAAWgIAAAQAAABwEAQAAAAOAAMAAQACAAAAXwIAAAkA" +
    "AABiAAAAGwEBAAAAbiADABAADgAAAAMAAQACAAAAZQIAAAkAAABiAAAAGwECAAAAbiADABAADgAA" +
    "AAEAAAADAAY8aW5pdD4AB0dvb2RieWUABUhlbGxvAAtMVHJhbnNmb3JtOwAVTGphdmEvaW8vUHJp" +
    "bnRTdHJlYW07ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZhL2xhbmcvU3RyaW5nOwASTGphdmEv" +
    "bGFuZy9TeXN0ZW07AA5UcmFuc2Zvcm0uamF2YQABVgACVkwAEmVtaXR0ZXI6IGphY2stNC4yMgAD" +
    "b3V0AAdwcmludGxuAApzYXlHb29kYnllAAVzYXlIaQABAAcOAAYABw6HAAMABw6HAAAAAQIAgIAE" +
    "sAIBAcgCAQHsAgAAAA0AAAAAAAAAAQAAAAAAAAABAAAAEAAAAHAAAAACAAAABgAAALAAAAADAAAA" +
    "AgAAAMgAAAAEAAAAAQAAAOAAAAAFAAAABQAAAOgAAAAGAAAAAQAAABABAAABIAAAAwAAADABAAAB" +
    "EAAAAQAAAJABAAACIAAAEAAAAJYBAAADIAAAAwAAAFoCAAAAIAAAAQAAAGsCAAAAEAAAAQAAAIAC" +
    "AAA=");

  public static void main(String[] args) {
    doTest();
  }

  public static void doTest() {
    addCommonTransformationResult("Transform", CLASS_BYTES, DEX_BYTES);
    enableCommonRetransformation(true);
    // Actually load the class.
    Transform t = new Transform();
    try {
      // Call functions with reflection. Since the sayGoodbye function does not exist in the
      // LTransform; when we compile this for the first time we need to use reflection.
      Method hi = Transform.class.getMethod("sayHi");
      Method bye = Transform.class.getMethod("sayGoodbye");
      hi.invoke(t);
      t.sayHi();
      bye.invoke(t);
      // Make sure we don't get called for transformation again.
      addCommonTransformationResult("Transform", new byte[0], new byte[0]);
      doCommonClassRetransformation(Transform.class);
      hi.invoke(t);
      t.sayHi();
      bye.invoke(t);
    } catch (Exception e) {
      System.out.println("Unexpected error occured! " + e.toString());
      e.printStackTrace();
    }
  }

  // Transforms the class
  private static native void doCommonClassRetransformation(Class<?>... klasses);
  private static native void enableCommonRetransformation(boolean enable);
  private static native void addCommonTransformationResult(String target_name,
                                                           byte[] class_bytes,
                                                           byte[] dex_bytes);
}
