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
  //     r.run();
  //     System.out.println("Goodbye - Transformed");
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAJAoACAARCQASABMIABQKABUAFgsAFwAYCAAZBwAaBwAbAQAGPGluaXQ+AQADKClW" +
    "AQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7" +
    "KVYBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMAAkACgcAHAwAHQAeAQATSGVsbG8gLSBU" +
    "cmFuc2Zvcm1lZAcAHwwAIAAhBwAiDAAjAAoBABVHb29kYnllIC0gVHJhbnNmb3JtZWQBAAlUcmFu" +
    "c2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZh" +
    "L2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZh" +
    "L2xhbmcvU3RyaW5nOylWAQASamF2YS9sYW5nL1J1bm5hYmxlAQADcnVuACAABwAIAAAAAAACAAAA" +
    "CQAKAAEACwAAAB0AAQABAAAABSq3AAGxAAAAAQAMAAAABgABAAAAAQABAA0ADgABAAsAAAA7AAIA" +
    "AgAAABeyAAISA7YABCu5AAUBALIAAhIGtgAEsQAAAAEADAAAABIABAAAAAMACAAEAA4ABQAWAAYA" +
    "AQAPAAAAAgAQ");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQAYeAMMXgYWxoeSHAS9EWKCCtVRSAGpqZVQAwAAcAAAAHhWNBIAAAAAAAAAALACAAAR" +
    "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAUAAAD8AAAAAQAAACQBAAAMAgAARAEAAKIB" +
    "AACqAQAAwQEAANYBAADjAQAA+gEAAA4CAAAkAgAAOAIAAEwCAABcAgAAXwIAAGMCAAB3AgAAfAIA" +
    "AIUCAACKAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACgAAAAoAAAAGAAAAAAAAAAsAAAAGAAAA" +
    "lAEAAAsAAAAGAAAAnAEAAAUAAQANAAAAAAAAAAAAAAAAAAEAEAAAAAEAAgAOAAAAAgAAAAAAAAAD" +
    "AAAADwAAAAAAAAAAAAAAAgAAAAAAAAAJAAAAAAAAAJ8CAAAAAAAAAQABAAEAAACRAgAABAAAAHAQ" +
    "AwAAAA4ABAACAAIAAACWAgAAFAAAAGIAAAAbAQIAAABuIAIAEAByEAQAAwBiAAAAGwEBAAAAbiAC" +
    "ABAADgABAAAAAwAAAAEAAAAEAAY8aW5pdD4AFUdvb2RieWUgLSBUcmFuc2Zvcm1lZAATSGVsbG8g" +
    "LSBUcmFuc2Zvcm1lZAALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEv" +
    "bGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABJM" +
    "amF2YS9sYW5nL1N5c3RlbTsADlRyYW5zZm9ybS5qYXZhAAFWAAJWTAASZW1pdHRlcjogamFjay00" +
    "LjEzAANvdXQAB3ByaW50bG4AA3J1bgAFc2F5SGkAAQAHDgADAQAHDoc8hwAAAAEBAICABMQCAQHc" +
    "AgAAAA0AAAAAAAAAAQAAAAAAAAABAAAAEQAAAHAAAAACAAAABwAAALQAAAADAAAAAwAAANAAAAAE" +
    "AAAAAQAAAPQAAAAFAAAABQAAAPwAAAAGAAAAAQAAACQBAAABIAAAAgAAAEQBAAABEAAAAgAAAJQB" +
    "AAACIAAAEQAAAKIBAAADIAAAAgAAAJECAAAAIAAAAQAAAJ8CAAAAEAAAAQAAALACAAA=");

  public static void main(String[] args) {
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
}
