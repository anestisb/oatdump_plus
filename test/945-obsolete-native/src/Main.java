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
  // class Transform {
  //   public void sayHi(Runnable r) {
  //     System.out.println("Hello - Transformed");
  //     doExecute(r);
  //     System.out.println("Goodbye - Transformed");
  //   }
  //
  //   private static native void doExecute(Runnable r);
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAIgoACAASCQATABQIABUKABYAFwoABwAYCAAZBwAaBwAbAQAGPGluaXQ+AQADKClW" +
    "AQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7" +
    "KVYBAAlkb0V4ZWN1dGUBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMAAkACgcAHAwAHQAe" +
    "AQATSGVsbG8gLSBUcmFuc2Zvcm1lZAcAHwwAIAAhDAAPAA4BABVHb29kYnllIC0gVHJhbnNmb3Jt" +
    "ZWQBAAlUcmFuc2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291" +
    "dAEAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxu" +
    "AQAVKExqYXZhL2xhbmcvU3RyaW5nOylWACAABwAIAAAAAAADAAAACQAKAAEACwAAAB0AAQABAAAA" +
    "BSq3AAGxAAAAAQAMAAAABgABAAAAEQABAA0ADgABAAsAAAA5AAIAAgAAABWyAAISA7YABCu4AAWy" +
    "AAISBrYABLEAAAABAAwAAAASAAQAAAATAAgAFAAMABUAFAAWAQoADwAOAAAAAQAQAAAAAgAR");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQB1fZcJR/opPuXacK8mIla5shH0LSg72qJYAwAAcAAAAHhWNBIAAAAAAAAAALgCAAAR" +
    "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAUAAAD8AAAAAQAAACQBAAAUAgAARAEAAKIB" +
    "AACqAQAAwQEAANYBAADjAQAA+gEAAA4CAAAkAgAAOAIAAEwCAABcAgAAXwIAAGMCAABuAgAAggIA" +
    "AIcCAACQAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACgAAAAoAAAAGAAAAAAAAAAsAAAAGAAAA" +
    "lAEAAAsAAAAGAAAAnAEAAAUAAQAOAAAAAAAAAAAAAAAAAAEADAAAAAAAAQAQAAAAAQACAA8AAAAC" +
    "AAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAJAAAAAAAAAKUCAAAAAAAAAQABAAEAAACXAgAABAAAAHAQ" +
    "BAAAAA4ABAACAAIAAACcAgAAFAAAAGIAAAAbAQIAAABuIAMAEABxEAEAAwBiAAAAGwEBAAAAbiAD" +
    "ABAADgABAAAAAwAAAAEAAAAEAAY8aW5pdD4AFUdvb2RieWUgLSBUcmFuc2Zvcm1lZAATSGVsbG8g" +
    "LSBUcmFuc2Zvcm1lZAALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEv" +
    "bGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABJM" +
    "amF2YS9sYW5nL1N5c3RlbTsADlRyYW5zZm9ybS5qYXZhAAFWAAJWTAAJZG9FeGVjdXRlABJlbWl0" +
    "dGVyOiBqYWNrLTQuMjUAA291dAAHcHJpbnRsbgAFc2F5SGkAEQAHDgATAQAHDoc8hwAAAAIBAICA" +
    "BMQCAYoCAAIB3AIADQAAAAAAAAABAAAAAAAAAAEAAAARAAAAcAAAAAIAAAAHAAAAtAAAAAMAAAAD" +
    "AAAA0AAAAAQAAAABAAAA9AAAAAUAAAAFAAAA/AAAAAYAAAABAAAAJAEAAAEgAAACAAAARAEAAAEQ" +
    "AAACAAAAlAEAAAIgAAARAAAAogEAAAMgAAACAAAAlwIAAAAgAAABAAAApQIAAAAQAAABAAAAuAIA" +
    "AA==");

  public static void main(String[] args) {
    bindTest945ObsoleteNative();
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    t.sayHi(() -> { System.out.println("Not doing anything here"); });
    t.sayHi(() -> {
      System.out.println("transforming calling function");
      doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    });
    t.sayHi(() -> { System.out.println("Not doing anything here"); });
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);

  private static native void bindTest945ObsoleteNative();
}
