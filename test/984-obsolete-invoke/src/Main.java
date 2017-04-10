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
  // class Transform {
  //   public static void sayHi(Runnable r) {
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
    "CQAKAAEACwAAAB0AAQABAAAABSq3AAGxAAAAAQAMAAAABgABAAAAAQAJAA0ADgABAAsAAAA7AAIA" +
    "AQAAABeyAAISA7YABCq5AAUBALIAAhIGtgAEsQAAAAEADAAAABIABAAAAAMACAAEAA4ABQAWAAYA" +
    "AQAPAAAAAgAQ");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQCMekj2NPwzrEp/v+2yzzSg8xZvBtU1bC1QAwAAcAAAAHhWNBIAAAAAAAAAALACAAAR" +
    "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAUAAAD8AAAAAQAAACQBAAAMAgAARAEAAKIB" +
    "AACqAQAAwQEAANYBAADjAQAA+gEAAA4CAAAkAgAAOAIAAEwCAABcAgAAXwIAAGMCAAB3AgAAfAIA" +
    "AIUCAACKAgAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACgAAAAoAAAAGAAAAAAAAAAsAAAAGAAAA" +
    "lAEAAAsAAAAGAAAAnAEAAAUAAQANAAAAAAAAAAAAAAAAAAEAEAAAAAEAAgAOAAAAAgAAAAAAAAAD" +
    "AAAADwAAAAAAAAAAAAAAAgAAAAAAAAAJAAAAAAAAAJ8CAAAAAAAAAQABAAEAAACRAgAABAAAAHAQ" +
    "AwAAAA4AAwABAAIAAACWAgAAFAAAAGIAAAAbAQIAAABuIAIAEAByEAQAAgBiAAAAGwEBAAAAbiAC" +
    "ABAADgABAAAAAwAAAAEAAAAEAAY8aW5pdD4AFUdvb2RieWUgLSBUcmFuc2Zvcm1lZAATSGVsbG8g" +
    "LSBUcmFuc2Zvcm1lZAALTFRyYW5zZm9ybTsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEv" +
    "bGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEvbGFuZy9TdHJpbmc7ABJM" +
    "amF2YS9sYW5nL1N5c3RlbTsADlRyYW5zZm9ybS5qYXZhAAFWAAJWTAASZW1pdHRlcjogamFjay00" +
    "LjMxAANvdXQAB3ByaW50bG4AA3J1bgAFc2F5SGkAAQAHDgADAQAHDoc8hwAAAAIAAICABMQCAQnc" +
    "AgAAAA0AAAAAAAAAAQAAAAAAAAABAAAAEQAAAHAAAAACAAAABwAAALQAAAADAAAAAwAAANAAAAAE" +
    "AAAAAQAAAPQAAAAFAAAABQAAAPwAAAAGAAAAAQAAACQBAAABIAAAAgAAAEQBAAABEAAAAgAAAJQB" +
    "AAACIAAAEQAAAKIBAAADIAAAAgAAAJECAAAAIAAAAQAAAJ8CAAAAEAAAAQAAALACAAA=");

  public static void main(String[] args) {
    art.Main.bindAgentJNIForClass(Main.class);
    doTest();
  }

  // The Method that holds an obsolete method pointer. We will fill it in by getting a jmethodID
  // from a stack with an obsolete method in it. There should be no other ways to obtain an obsolete
  // jmethodID in ART without unsafe casts.
  public static Method obsolete_method = null;

  public static void doTest() {
    // Capture the obsolete method.
    //
    // NB The obsolete method must be direct so that we will not look in the receiver type to get
    // the actual method.
    Transform.sayHi(() -> {
      System.out.println("transforming calling function");
      doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
      System.out.println("Retrieving obsolete method from current stack");
      // This should get the obsolete sayHi method (as the only obsolete method on the current
      // threads stack).
      Main.obsolete_method = getFirstObsoleteMethod984();
    });

    // Prove we did actually redefine something.
    System.out.println("Invoking redefined version of method.");
    Transform.sayHi(() -> { System.out.println("Not doing anything here"); });

    System.out.println("invoking obsolete method");
    try {
      obsolete_method.invoke(null, (Runnable)() -> {
        throw new Error("Unexpected code running from invoke of obsolete method!");
      });
      throw new Error("Running obsolete method did not throw exception");
    } catch (Throwable e) {
      if (e instanceof InternalError || e.getCause() instanceof InternalError) {
        System.out.println("Caught expected error from attempting to invoke an obsolete method.");
      } else {
        System.out.println("Unexpected error type for calling obsolete method! Expected either "
            + "an InternalError or something that is caused by an InternalError.");
        throw new Error("Unexpected error caught: ", e);
      }
    }
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);

  // Gets the first obsolete method on the current threads stack (NB only looks through the first 30
  // stack frames).
  private static native Method getFirstObsoleteMethod984();
}
