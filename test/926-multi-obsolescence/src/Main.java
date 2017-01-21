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

import java.util.ArrayList;
import java.util.Base64;

public class Main {
  // class Transform {
  //   public void sayHi(Runnable r) {
  //     System.out.println("Hello - Transformed");
  //     r.run();
  //     System.out.println("Goodbye - Transformed");
  //   }
  // }
  private static CommonClassDefinition VALID_DEFINITION_T1 = new CommonClassDefinition(
      Transform.class,
      Base64.getDecoder().decode(
        "yv66vgAAADQAJAoACAARCQASABMIABQKABUAFgsAFwAYCAAZBwAaBwAbAQAGPGluaXQ+AQADKClW" +
        "AQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7" +
        "KVYBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMAAkACgcAHAwAHQAeAQATSGVsbG8gLSBU" +
        "cmFuc2Zvcm1lZAcAHwwAIAAhBwAiDAAjAAoBABVHb29kYnllIC0gVHJhbnNmb3JtZWQBAAlUcmFu" +
        "c2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEAFUxqYXZh" +
        "L2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAVKExqYXZh" +
        "L2xhbmcvU3RyaW5nOylWAQASamF2YS9sYW5nL1J1bm5hYmxlAQADcnVuACAABwAIAAAAAAACAAAA" +
        "CQAKAAEACwAAAB0AAQABAAAABSq3AAGxAAAAAQAMAAAABgABAAAAAQABAA0ADgABAAsAAAA7AAIA" +
        "AgAAABeyAAISA7YABCu5AAUBALIAAhIGtgAEsQAAAAEADAAAABIABAAAAAMACAAEAA4ABQAWAAYA" +
        "AQAPAAAAAgAQ"),
      Base64.getDecoder().decode(
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
        "AAACIAAAEQAAAKIBAAADIAAAAgAAAJECAAAAIAAAAQAAAJ8CAAAAEAAAAQAAALACAAA="));
  // class Transform2 {
  //   public void sayHi(Runnable r) {
  //     System.out.println("Hello 2 - Transformed");
  //     r.run();
  //     System.out.println("Goodbye 2 - Transformed");
  //   }
  // }
  private static CommonClassDefinition VALID_DEFINITION_T2 = new CommonClassDefinition(
      Transform2.class,
      Base64.getDecoder().decode(
        "yv66vgAAADQAJAoACAARCQASABMIABQKABUAFgsAFwAYCAAZBwAaBwAbAQAGPGluaXQ+AQADKClW" +
        "AQAEQ29kZQEAD0xpbmVOdW1iZXJUYWJsZQEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7" +
        "KVYBAApTb3VyY2VGaWxlAQAPVHJhbnNmb3JtMi5qYXZhDAAJAAoHABwMAB0AHgEAFUhlbGxvIDIg" +
        "LSBUcmFuc2Zvcm1lZAcAHwwAIAAhBwAiDAAjAAoBABdHb29kYnllIDIgLSBUcmFuc2Zvcm1lZAEA" +
        "ClRyYW5zZm9ybTIBABBqYXZhL2xhbmcvT2JqZWN0AQAQamF2YS9sYW5nL1N5c3RlbQEAA291dAEA" +
        "FUxqYXZhL2lvL1ByaW50U3RyZWFtOwEAE2phdmEvaW8vUHJpbnRTdHJlYW0BAAdwcmludGxuAQAV" +
        "KExqYXZhL2xhbmcvU3RyaW5nOylWAQASamF2YS9sYW5nL1J1bm5hYmxlAQADcnVuACAABwAIAAAA" +
        "AAACAAAACQAKAAEACwAAAB0AAQABAAAABSq3AAGxAAAAAQAMAAAABgABAAAAAQABAA0ADgABAAsA" +
        "AAA7AAIAAgAAABeyAAISA7YABCu5AAUBALIAAhIGtgAEsQAAAAEADAAAABIABAAAAAMACAAEAA4A" +
        "BQAWAAYAAQAPAAAAAgAQ"),
      Base64.getDecoder().decode(
        "ZGV4CjAzNQCee5Z6+AuFcjnPjjn7QYgZmKSmFQCO4nxUAwAAcAAAAHhWNBIAAAAAAAAAALQCAAAR" +
        "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAUAAAD8AAAAAQAAACQBAAAQAgAARAEAAKIB" +
        "AACqAQAAwwEAANoBAADoAQAA/wEAABMCAAApAgAAPQIAAFECAABiAgAAZQIAAGkCAAB9AgAAggIA" +
        "AIsCAACQAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACgAAAAoAAAAGAAAAAAAAAAsAAAAGAAAA" +
        "lAEAAAsAAAAGAAAAnAEAAAUAAQANAAAAAAAAAAAAAAAAAAEAEAAAAAEAAgAOAAAAAgAAAAAAAAAD" +
        "AAAADwAAAAAAAAAAAAAAAgAAAAAAAAAJAAAAAAAAAKUCAAAAAAAAAQABAAEAAACXAgAABAAAAHAQ" +
        "AwAAAA4ABAACAAIAAACcAgAAFAAAAGIAAAAbAQIAAABuIAIAEAByEAQAAwBiAAAAGwEBAAAAbiAC" +
        "ABAADgABAAAAAwAAAAEAAAAEAAY8aW5pdD4AF0dvb2RieWUgMiAtIFRyYW5zZm9ybWVkABVIZWxs" +
        "byAyIC0gVHJhbnNmb3JtZWQADExUcmFuc2Zvcm0yOwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABJM" +
        "amF2YS9sYW5nL09iamVjdDsAFExqYXZhL2xhbmcvUnVubmFibGU7ABJMamF2YS9sYW5nL1N0cmlu" +
        "ZzsAEkxqYXZhL2xhbmcvU3lzdGVtOwAPVHJhbnNmb3JtMi5qYXZhAAFWAAJWTAASZW1pdHRlcjog" +
        "amFjay00LjIwAANvdXQAB3ByaW50bG4AA3J1bgAFc2F5SGkAAQAHDgADAQAHDoc8hwAAAAEBAICA" +
        "BMQCAQHcAgANAAAAAAAAAAEAAAAAAAAAAQAAABEAAABwAAAAAgAAAAcAAAC0AAAAAwAAAAMAAADQ" +
        "AAAABAAAAAEAAAD0AAAABQAAAAUAAAD8AAAABgAAAAEAAAAkAQAAASAAAAIAAABEAQAAARAAAAIA" +
        "AACUAQAAAiAAABEAAACiAQAAAyAAAAIAAACXAgAAACAAAAEAAAClAgAAABAAAAEAAAC0AgAA"));

  public static void main(String[] args) {
    doTest(new Transform(), new Transform2());
  }

  public static void doTest(final Transform t1, final Transform2 t2) {
    t1.sayHi(() -> { t2.sayHi(() -> { System.out.println("Not doing anything here"); }); });
    t1.sayHi(() -> {
      t2.sayHi(() -> {
        System.out.println("transforming calling functions");
        doMultiClassRedefinition(VALID_DEFINITION_T1, VALID_DEFINITION_T2);
      });
    });
    t1.sayHi(() -> { t2.sayHi(() -> { System.out.println("Not doing anything here"); }); });
  }

  public static void doMultiClassRedefinition(CommonClassDefinition... defs) {
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

  public static native void doCommonMultiClassRedefinition(Class<?>[] targets,
                                                           byte[][] classfiles,
                                                           byte[][] dexfiles);
}
