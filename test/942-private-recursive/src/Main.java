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
  //   public void sayHi(int recur, Runnable r) {
  //     privateSayHi(recur, r);
  //   }
  //   private void privateSayHi(int recur, Runnable r) {
  //     System.out.println("Hello" + recur + " - transformed");
  //     if (recur == 1) {
  //       r.run();
  //       privateSayHi(recur - 1, r);
  //     } else if (recur != 0) {
  //       privateSayHi(recur - 1, r);
  //     }
  //     System.out.println("Goodbye" + recur + " - transformed");
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAOAoADwAaCgAOABsJABwAHQcAHgoABAAaCAAfCgAEACAKAAQAIQgAIgoABAAjCgAk" +
    "ACULACYAJwgAKAcAKQcAKgEABjxpbml0PgEAAygpVgEABENvZGUBAA9MaW5lTnVtYmVyVGFibGUB" +
    "AAVzYXlIaQEAGChJTGphdmEvbGFuZy9SdW5uYWJsZTspVgEADHByaXZhdGVTYXlIaQEADVN0YWNr" +
    "TWFwVGFibGUBAApTb3VyY2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMABAAEQwAFgAVBwArDAAsAC0B" +
    "ABdqYXZhL2xhbmcvU3RyaW5nQnVpbGRlcgEABUhlbGxvDAAuAC8MAC4AMAEADiAtIHRyYW5zZm9y" +
    "bWVkDAAxADIHADMMADQANQcANgwANwARAQAHR29vZGJ5ZQEACVRyYW5zZm9ybQEAEGphdmEvbGFu" +
    "Zy9PYmplY3QBABBqYXZhL2xhbmcvU3lzdGVtAQADb3V0AQAVTGphdmEvaW8vUHJpbnRTdHJlYW07" +
    "AQAGYXBwZW5kAQAtKExqYXZhL2xhbmcvU3RyaW5nOylMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7" +
    "AQAcKEkpTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwEACHRvU3RyaW5nAQAUKClMamF2YS9sYW5n" +
    "L1N0cmluZzsBABNqYXZhL2lvL1ByaW50U3RyZWFtAQAHcHJpbnRsbgEAFShMamF2YS9sYW5nL1N0" +
    "cmluZzspVgEAEmphdmEvbGFuZy9SdW5uYWJsZQEAA3J1bgAgAA4ADwAAAAAAAwAAABAAEQABABIA" +
    "AAAdAAEAAQAAAAUqtwABsQAAAAEAEwAAAAYAAQAAAAEAAQAUABUAAQASAAAAIwADAAMAAAAHKhss" +
    "twACsQAAAAEAEwAAAAoAAgAAAAMABgAEAAIAFgAVAAEAEgAAAJ0AAwADAAAAX7IAA7sABFm3AAUS" +
    "BrYABxu2AAgSCbYAB7YACrYACxsEoAAULLkADAEAKhsEZCy3AAKnAA8bmQALKhsEZCy3AAKyAAO7" +
    "AARZtwAFEg22AAcbtgAIEgm2AAe2AAq2AAuxAAAAAgATAAAAIgAIAAAABgAeAAcAIwAIACkACQA0" +
    "AAoAOAALAEAADQBeAA4AFwAAAAQAAjQLAAEAGAAAAAIAGQ==");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQBQqwVIiZvIuS8j1HDurKbXZEV62Mnug5PEBAAAcAAAAHhWNBIAAAAAAAAAACQEAAAb" +
    "AAAAcAAAAAkAAADcAAAABgAAAAABAAABAAAASAEAAAoAAABQAQAAAQAAAKABAAAEAwAAwAEAAMAC" +
    "AADQAgAA2AIAAOECAADoAgAA6wIAAO4CAADyAgAA9gIAAAMDAAAaAwAALgMAAEQDAABYAwAAcwMA" +
    "AIcDAACXAwAAmgMAAJ8DAACjAwAAqwMAAL8DAADEAwAAzQMAANsDAADgAwAA5wMAAAQAAAAIAAAA" +
    "CQAAAAoAAAALAAAADAAAAA0AAAAOAAAAEAAAAAUAAAAFAAAAAAAAAAYAAAAGAAAAqAIAAAcAAAAG" +
    "AAAAsAIAABAAAAAIAAAAAAAAABEAAAAIAAAAuAIAABIAAAAIAAAAsAIAAAcAAgAVAAAAAQADAAEA" +
    "AAABAAQAFwAAAAEABAAZAAAAAgAFABYAAAADAAMAAQAAAAQAAwAYAAAABgADAAEAAAAGAAEAEwAA" +
    "AAYAAgATAAAABgAAABoAAAABAAAAAAAAAAMAAAAAAAAADwAAAAAAAAAQBAAAAAAAAAEAAQABAAAA" +
    "8QMAAAQAAABwEAQAAAAOAAYAAwADAAAA9gMAAFQAAABiAAAAIgEGAHAQBgABABsCAwAAAG4gCAAh" +
    "AAwBbiAHAEEADAEbAgAAAABuIAgAIQAMAW4QCQABAAwBbiADABAAEhAzBCsAchAFAAUA2AAE/3Aw" +
    "AQADBWIAAAAiAQYAcBAGAAEAGwICAAAAbiAIACEADAFuIAcAQQAMARsCAAAAAG4gCAAhAAwBbhAJ" +
    "AAEADAFuIAMAEAAOADgE3//YAAT/cDABAAMFKNgDAAMAAwAAAAgEAAAEAAAAcDABABACDgABAAAA" +
    "AAAAAAEAAAAFAAAAAgAAAAAABAAOIC0gdHJhbnNmb3JtZWQABjxpbml0PgAHR29vZGJ5ZQAFSGVs" +
    "bG8AAUkAAUwAAkxJAAJMTAALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGph" +
    "dmEvbGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7" +
    "ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9sYW5nL1N5c3RlbTsADlRyYW5zZm9y" +
    "bS5qYXZhAAFWAANWSUwAAlZMAAZhcHBlbmQAEmVtaXR0ZXI6IGphY2stNC4yNAADb3V0AAdwcmlu" +
    "dGxuAAxwcml2YXRlU2F5SGkAA3J1bgAFc2F5SGkACHRvU3RyaW5nAAEABw4ABgIAAAcOASAPPDxd" +
    "ASAPGS0AAwIAAAcOPAAAAAIBAICABMADAQLYAwIBkAUAAA0AAAAAAAAAAQAAAAAAAAABAAAAGwAA" +
    "AHAAAAACAAAACQAAANwAAAADAAAABgAAAAABAAAEAAAAAQAAAEgBAAAFAAAACgAAAFABAAAGAAAA" +
    "AQAAAKABAAABIAAAAwAAAMABAAABEAAAAwAAAKgCAAACIAAAGwAAAMACAAADIAAAAwAAAPEDAAAA" +
    "IAAAAQAAABAEAAAAEAAAAQAAACQEAAA=");

  public static void main(String[] args) {
    doTest(new Transform());
  }

  public static void doTest(Transform t) {
    t.sayHi(2, () -> { System.out.println("Not doing anything here"); });
    t.sayHi(2, () -> {
      System.out.println("transforming calling function");
      doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    });
    t.sayHi(2, () -> { System.out.println("Not doing anything here"); });
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);
}
