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
  private static boolean do_print = false;

  public static boolean doPrint() {
    return do_print;
  }

  // Since the transformed code isn't really an issue this is the same as test 915.
  // class Transform {
  //   private void Start() {
  //     System.out.println("Hello - private - Transformed");
  //   }
  //
  //   private void Finish() {
  //     System.out.println("Goodbye - private - Transformed");
  //   }
  //
  //   public void sayHi(Runnable r) {
  //     System.out.println("Pre Start private method call - Transformed");
  //     Start();
  //     System.out.println("Post Start private method call - Transformed");
  //     r.run();
  //     System.out.println("Pre Finish private method call - Transformed");
  //     Finish();
  //     System.out.println("Post Finish private method call - Transformed");
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQAMgoADgAZCQAaABsIABwKAB0AHggAHwgAIAoADQAhCAAiCwAjACQIACUKAA0AJggA" +
    "JwcAKAcAKQEABjxpbml0PgEAAygpVgEABENvZGUBAA9MaW5lTnVtYmVyVGFibGUBAAVTdGFydAEA" +
    "BkZpbmlzaAEABXNheUhpAQAXKExqYXZhL2xhbmcvUnVubmFibGU7KVYBAApTb3VyY2VGaWxlAQAO" +
    "VHJhbnNmb3JtLmphdmEMAA8AEAcAKgwAKwAsAQAdSGVsbG8gLSBwcml2YXRlIC0gVHJhbnNmb3Jt" +
    "ZWQHAC0MAC4ALwEAH0dvb2RieWUgLSBwcml2YXRlIC0gVHJhbnNmb3JtZWQBACtQcmUgU3RhcnQg" +
    "cHJpdmF0ZSBtZXRob2QgY2FsbCAtIFRyYW5zZm9ybWVkDAATABABACxQb3N0IFN0YXJ0IHByaXZh" +
    "dGUgbWV0aG9kIGNhbGwgLSBUcmFuc2Zvcm1lZAcAMAwAMQAQAQAsUHJlIEZpbmlzaCBwcml2YXRl" +
    "IG1ldGhvZCBjYWxsIC0gVHJhbnNmb3JtZWQMABQAEAEALVBvc3QgRmluaXNoIHByaXZhdGUgbWV0" +
    "aG9kIGNhbGwgLSBUcmFuc2Zvcm1lZAEACVRyYW5zZm9ybQEAEGphdmEvbGFuZy9PYmplY3QBABBq" +
    "YXZhL2xhbmcvU3lzdGVtAQADb3V0AQAVTGphdmEvaW8vUHJpbnRTdHJlYW07AQATamF2YS9pby9Q" +
    "cmludFN0cmVhbQEAB3ByaW50bG4BABUoTGphdmEvbGFuZy9TdHJpbmc7KVYBABJqYXZhL2xhbmcv" +
    "UnVubmFibGUBAANydW4AIAANAA4AAAAAAAQAAAAPABAAAQARAAAAHQABAAEAAAAFKrcAAbEAAAAB" +
    "ABIAAAAGAAEAAAABAAIAEwAQAAEAEQAAACUAAgABAAAACbIAAhIDtgAEsQAAAAEAEgAAAAoAAgAA" +
    "AAMACAAEAAIAFAAQAAEAEQAAACUAAgABAAAACbIAAhIFtgAEsQAAAAEAEgAAAAoAAgAAAAcACAAI" +
    "AAEAFQAWAAEAEQAAAGMAAgACAAAAL7IAAhIGtgAEKrcAB7IAAhIItgAEK7kACQEAsgACEgq2AAQq" +
    "twALsgACEgy2AASxAAAAAQASAAAAIgAIAAAACwAIAAwADAANABQADgAaAA8AIgAQACYAEQAuABIA" +
    "AQAXAAAAAgAY");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQCM0QYTJmX+NsZXkImojgSkJtXyuew3oaXcBAAAcAAAAHhWNBIAAAAAAAAAADwEAAAX" +
    "AAAAcAAAAAcAAADMAAAAAwAAAOgAAAABAAAADAEAAAcAAAAUAQAAAQAAAEwBAABwAwAAbAEAAD4C" +
    "AABGAgAATgIAAG8CAACOAgAAmwIAALICAADGAgAA3AIAAPACAAAEAwAAMwMAAGEDAACPAwAAvAMA" +
    "AMMDAADTAwAA1gMAANoDAADuAwAA8wMAAPwDAAABBAAABAAAAAUAAAAGAAAABwAAAAgAAAAJAAAA" +
    "EAAAABAAAAAGAAAAAAAAABEAAAAGAAAAMAIAABEAAAAGAAAAOAIAAAUAAQATAAAAAAAAAAAAAAAA" +
    "AAAAAQAAAAAAAAAOAAAAAAABABYAAAABAAIAFAAAAAIAAAAAAAAAAwAAABUAAAAAAAAAAAAAAAIA" +
    "AAAAAAAADwAAAAAAAAAmBAAAAAAAAAEAAQABAAAACAQAAAQAAABwEAUAAAAOAAMAAQACAAAADQQA" +
    "AAkAAABiAAAAGwECAAAAbiAEABAADgAAAAMAAQACAAAAEwQAAAkAAABiAAAAGwEDAAAAbiAEABAA" +
    "DgAAAAQAAgACAAAAGQQAACoAAABiAAAAGwENAAAAbiAEABAAcBACAAIAYgAAABsBCwAAAG4gBAAQ" +
    "AHIQBgADAGIAAAAbAQwAAABuIAQAEABwEAEAAgBiAAAAGwEKAAAAbiAEABAADgABAAAAAwAAAAEA" +
    "AAAEAAY8aW5pdD4ABkZpbmlzaAAfR29vZGJ5ZSAtIHByaXZhdGUgLSBUcmFuc2Zvcm1lZAAdSGVs" +
    "bG8gLSBwcml2YXRlIC0gVHJhbnNmb3JtZWQAC0xUcmFuc2Zvcm07ABVMamF2YS9pby9QcmludFN0" +
    "cmVhbTsAEkxqYXZhL2xhbmcvT2JqZWN0OwAUTGphdmEvbGFuZy9SdW5uYWJsZTsAEkxqYXZhL2xh" +
    "bmcvU3RyaW5nOwASTGphdmEvbGFuZy9TeXN0ZW07AC1Qb3N0IEZpbmlzaCBwcml2YXRlIG1ldGhv" +
    "ZCBjYWxsIC0gVHJhbnNmb3JtZWQALFBvc3QgU3RhcnQgcHJpdmF0ZSBtZXRob2QgY2FsbCAtIFRy" +
    "YW5zZm9ybWVkACxQcmUgRmluaXNoIHByaXZhdGUgbWV0aG9kIGNhbGwgLSBUcmFuc2Zvcm1lZAAr" +
    "UHJlIFN0YXJ0IHByaXZhdGUgbWV0aG9kIGNhbGwgLSBUcmFuc2Zvcm1lZAAFU3RhcnQADlRyYW5z" +
    "Zm9ybS5qYXZhAAFWAAJWTAASZW1pdHRlcjogamFjay00LjEzAANvdXQAB3ByaW50bG4AA3J1bgAF" +
    "c2F5SGkAAQAHDgAHAAcOhwADAAcOhwALAQAHDoc8hzyHPIcAAAADAQCAgATsAgEChAMBAqgDAwHM" +
    "Aw0AAAAAAAAAAQAAAAAAAAABAAAAFwAAAHAAAAACAAAABwAAAMwAAAADAAAAAwAAAOgAAAAEAAAA" +
    "AQAAAAwBAAAFAAAABwAAABQBAAAGAAAAAQAAAEwBAAABIAAABAAAAGwBAAABEAAAAgAAADACAAAC" +
    "IAAAFwAAAD4CAAADIAAABAAAAAgEAAAAIAAAAQAAACYEAAAAEAAAAQAAADwEAAA=");

  public static void main(String[] args) {
    System.loadLibrary(args[1]);
    doTest(new Transform());
  }

  // TODO Workaround to (1) inability to ensure that current_method is not put into a register by
  // the JIT and/or (2) inability to deoptimize frames near runtime functions.
  // TODO Fix one/both of these issues.
  public static void doCall(Runnable r) {
      r.run();
  }

  private static boolean interpreting = true;
  public static void doTest(Transform t) {
    do_print = false;
    Runnable noop = () -> {};
    long j = 0;
    do {
      for (int i = 0; i < 10000; i++) {
        t.sayHi(noop);
        j++;
      }
      t.sayHi(() -> {
        // TODO 2 is wrong It is checking a piece of this lambda method but should be fine due to
        // how we implement lambdas. Be sure to fix before really submitting.
        interpreting = Main.isInterpretedFunction("LTransform;->sayHi()V");
      });
      if (j >= 1000000) {
        System.out.println("FAIL: Could not make sayHi be Jitted!");
        return;
      }
      j++;
    } while(interpreting);
    // Start printing.
    do_print = true;
    t.sayHi(() -> {
      System.out.println("Not doing anything here");
    });
    t.sayHi(() -> {
      if (Main.isInterpretedFunction("LTransform;->sayHi()V")) {
        System.out.println("FAIL: sayHi is being interpreted!");
      }
      System.out.println("transforming calling function");
      doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    });
    t.sayHi(() -> { System.out.println("Not doing anything here"); });
  }

  private static native boolean isInterpretedFunction(String name);

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);
}
