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

  // base64 encoded class/dex file for
  // class Transform {
  //   public String take1;
  //   public String take2;
  //
  //   public Transform(String a, String b) {
  //     take1 = a;
  //     take2 = b;
  //   }
  //
  //   public String getResult() {
  //     return take2;
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAFwoABQARCQAEABIJAAQAEwcAFAcAFQEABXRha2UxAQASTGphdmEvbGFuZy9TdHJp" +
    "bmc7AQAFdGFrZTIBAAY8aW5pdD4BACcoTGphdmEvbGFuZy9TdHJpbmc7TGphdmEvbGFuZy9TdHJp" +
    "bmc7KVYBAARDb2RlAQAPTGluZU51bWJlclRhYmxlAQAJZ2V0UmVzdWx0AQAUKClMamF2YS9sYW5n" +
    "L1N0cmluZzsBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMAAkAFgwABgAHDAAIAAcBAAlU" +
    "cmFuc2Zvcm0BABBqYXZhL2xhbmcvT2JqZWN0AQADKClWACAABAAFAAAAAgABAAYABwAAAAEACAAH" +
    "AAAAAgABAAkACgABAAsAAAAzAAIAAwAAAA8qtwABKiu1AAIqLLUAA7EAAAABAAwAAAASAAQAAAAU" +
    "AAQAFQAJABYADgAXAAEADQAOAAEACwAAAB0AAQABAAAABSq0AAOwAAAAAQAMAAAABgABAAAAGgAB" +
    "AA8AAAACABA=");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQAGUTBb4jIABRlaI9rejdk7RCfyqR2kmNSkAgAAcAAAAHhWNBIAAAAAAAAAAAQCAAAM" +
    "AAAAcAAAAAQAAACgAAAAAwAAALAAAAACAAAA1AAAAAMAAADkAAAAAQAAAPwAAACIAQAAHAEAAFwB" +
    "AABkAQAAZwEAAHQBAACIAQAAnAEAAKwBAACvAQAAtAEAAMgBAADTAQAA2gEAAAIAAAADAAAABAAA" +
    "AAYAAAABAAAAAgAAAAAAAAAGAAAAAwAAAAAAAAAHAAAAAwAAAFQBAAAAAAIACgAAAAAAAgALAAAA" +
    "AAACAAAAAAAAAAAACQAAAAEAAQAAAAAAAAAAAAAAAAABAAAAAAAAAAUAAAAAAAAA8AEAAAAAAAAD" +
    "AAMAAQAAAOEBAAAIAAAAcBACAAAAWwEAAFsCAQAOAAIAAQAAAAAA6wEAAAMAAABUEAEAEQAAAAIA" +
    "AAACAAIABjxpbml0PgABTAALTFRyYW5zZm9ybTsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEv" +
    "bGFuZy9TdHJpbmc7AA5UcmFuc2Zvcm0uamF2YQABVgADVkxMABJlbWl0dGVyOiBqYWNrLTQuMTkA" +
    "CWdldFJlc3VsdAAFdGFrZTEABXRha2UyABQCAAAHDjwtLQAaAAcOAAACAQEAAQEBAIGABJwCAQG8" +
    "AgAADQAAAAAAAAABAAAAAAAAAAEAAAAMAAAAcAAAAAIAAAAEAAAAoAAAAAMAAAADAAAAsAAAAAQA" +
    "AAACAAAA1AAAAAUAAAADAAAA5AAAAAYAAAABAAAA/AAAAAEgAAACAAAAHAEAAAEQAAABAAAAVAEA" +
    "AAIgAAAMAAAAXAEAAAMgAAACAAAA4QEAAAAgAAABAAAA8AEAAAAQAAABAAAABAIAAA==");

  public static void main(String[] args) {
    doTest(new Transform("Hello", "Goodbye"),
           new Transform("start", "end"));
  }

  private static void printTransform(Transform t) {
    System.out.println("Result is " + t.getResult());
    System.out.println("take1 is " + t.take1);
    System.out.println("take2 is " + t.take2);
  }
  public static void doTest(Transform t1, Transform t2) {
    printTransform(t1);
    printTransform(t2);
    doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    printTransform(t1);
    printTransform(t2);
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] class_file,
                                                       byte[] dex_file);
}
