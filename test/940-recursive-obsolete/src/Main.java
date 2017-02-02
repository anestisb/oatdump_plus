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
  //     System.out.println("Hello" + recur + " - transformed");
  //     if (recur == 1) {
  //       r.run();
  //       sayHi(recur - 1, r);
  //     } else if (recur != 0) {
  //       sayHi(recur - 1, r);
  //     }
  //     System.out.println("Goodbye" + recur + " - transformed");
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQANwoADwAZCQAaABsHABwKAAMAGQgAHQoAAwAeCgADAB8IACAKAAMAIQoAIgAjCwAk" +
    "ACUKAA4AJggAJwcAKAcAKQEABjxpbml0PgEAAygpVgEABENvZGUBAA9MaW5lTnVtYmVyVGFibGUB" +
    "AAVzYXlIaQEAGChJTGphdmEvbGFuZy9SdW5uYWJsZTspVgEADVN0YWNrTWFwVGFibGUBAApTb3Vy" +
    "Y2VGaWxlAQAOVHJhbnNmb3JtLmphdmEMABAAEQcAKgwAKwAsAQAXamF2YS9sYW5nL1N0cmluZ0J1" +
    "aWxkZXIBAAVIZWxsbwwALQAuDAAtAC8BAA4gLSB0cmFuc2Zvcm1lZAwAMAAxBwAyDAAzADQHADUM" +
    "ADYAEQwAFAAVAQAHR29vZGJ5ZQEACVRyYW5zZm9ybQEAEGphdmEvbGFuZy9PYmplY3QBABBqYXZh" +
    "L2xhbmcvU3lzdGVtAQADb3V0AQAVTGphdmEvaW8vUHJpbnRTdHJlYW07AQAGYXBwZW5kAQAtKExq" +
    "YXZhL2xhbmcvU3RyaW5nOylMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7AQAcKEkpTGphdmEvbGFu" +
    "Zy9TdHJpbmdCdWlsZGVyOwEACHRvU3RyaW5nAQAUKClMamF2YS9sYW5nL1N0cmluZzsBABNqYXZh" +
    "L2lvL1ByaW50U3RyZWFtAQAHcHJpbnRsbgEAFShMamF2YS9sYW5nL1N0cmluZzspVgEAEmphdmEv" +
    "bGFuZy9SdW5uYWJsZQEAA3J1bgAgAA4ADwAAAAAAAgAAABAAEQABABIAAAAdAAEAAQAAAAUqtwAB" +
    "sQAAAAEAEwAAAAYAAQAAAAEAAQAUABUAAQASAAAAnQADAAMAAABfsgACuwADWbcABBIFtgAGG7YA" +
    "BxIItgAGtgAJtgAKGwSgABQsuQALAQAqGwRkLLYADKcADxuZAAsqGwRkLLYADLIAArsAA1m3AAQS" +
    "DbYABhu2AAcSCLYABrYACbYACrEAAAACABMAAAAiAAgAAAADAB4ABAAjAAUAKQAGADQABwA4AAgA" +
    "QAAKAF4ACwAWAAAABAACNAsAAQAXAAAAAgAY");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQA3pkIgnymz2/eri+mp2dyZo3jolQmaRPKEBAAAcAAAAHhWNBIAAAAAAAAAAOQDAAAa" +
    "AAAAcAAAAAkAAADYAAAABgAAAPwAAAABAAAARAEAAAkAAABMAQAAAQAAAJQBAADQAgAAtAEAAJwC" +
    "AACsAgAAtAIAAL0CAADEAgAAxwIAAMoCAADOAgAA0gIAAN8CAAD2AgAACgMAACADAAA0AwAATwMA" +
    "AGMDAABzAwAAdgMAAHsDAAB/AwAAhwMAAJsDAACgAwAAqQMAAK4DAAC1AwAABAAAAAgAAAAJAAAA" +
    "CgAAAAsAAAAMAAAADQAAAA4AAAAQAAAABQAAAAUAAAAAAAAABgAAAAYAAACEAgAABwAAAAYAAACM" +
    "AgAAEAAAAAgAAAAAAAAAEQAAAAgAAACUAgAAEgAAAAgAAACMAgAABwACABUAAAABAAMAAQAAAAEA" +
    "BAAYAAAAAgAFABYAAAADAAMAAQAAAAQAAwAXAAAABgADAAEAAAAGAAEAEwAAAAYAAgATAAAABgAA" +
    "ABkAAAABAAAAAAAAAAMAAAAAAAAADwAAAAAAAADWAwAAAAAAAAEAAQABAAAAvwMAAAQAAABwEAMA" +
    "AAAOAAYAAwADAAAAxAMAAFQAAABiAAAAIgEGAHAQBQABABsCAwAAAG4gBwAhAAwBbiAGAEEADAEb" +
    "AgAAAABuIAcAIQAMAW4QCAABAAwBbiACABAAEhAzBCsAchAEAAUA2AAE/24wAQADBWIAAAAiAQYA" +
    "cBAFAAEAGwICAAAAbiAHACEADAFuIAYAQQAMARsCAAAAAG4gBwAhAAwBbhAIAAEADAFuIAIAEAAO" +
    "ADgE3//YAAT/bjABAAMFKNgBAAAAAAAAAAEAAAAFAAAAAgAAAAAABAAOIC0gdHJhbnNmb3JtZWQA" +
    "Bjxpbml0PgAHR29vZGJ5ZQAFSGVsbG8AAUkAAUwAAkxJAAJMTAALTFRyYW5zZm9ybTsAFUxqYXZh" +
    "L2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxl" +
    "OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABJMamF2YS9s" +
    "YW5nL1N5c3RlbTsADlRyYW5zZm9ybS5qYXZhAAFWAANWSUwAAlZMAAZhcHBlbmQAEmVtaXR0ZXI6" +
    "IGphY2stNC4yNAADb3V0AAdwcmludGxuAANydW4ABXNheUhpAAh0b1N0cmluZwABAAcOAAMCAAAH" +
    "DgEgDzw8XQEgDxktAAAAAQEAgIAEtAMBAcwDDQAAAAAAAAABAAAAAAAAAAEAAAAaAAAAcAAAAAIA" +
    "AAAJAAAA2AAAAAMAAAAGAAAA/AAAAAQAAAABAAAARAEAAAUAAAAJAAAATAEAAAYAAAABAAAAlAEA" +
    "AAEgAAACAAAAtAEAAAEQAAADAAAAhAIAAAIgAAAaAAAAnAIAAAMgAAACAAAAvwMAAAAgAAABAAAA" +
    "1gMAAAAQAAABAAAA5AMAAA==");

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
