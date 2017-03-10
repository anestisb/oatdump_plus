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
import java.util.concurrent.Semaphore;

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
    // Semaphores to let each thread know where the other is. We could use barriers but semaphores
    // mean we don't need to have the worker thread be waiting around.
    final Semaphore sem_redefine_start = new Semaphore(0);
    final Semaphore sem_redefine_end = new Semaphore(0);
    // Create a thread to do the actual redefinition. We will just communicate through an
    // atomic-integer.
    new Thread(() -> {
      try {
        // Wait for the other thread to ask for redefinition.
        sem_redefine_start.acquire();
        // Do the redefinition.
        doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
        // Allow the other thread to wake up if it is waiting.
        sem_redefine_end.release();
      } catch (InterruptedException e) {
        throw new Error("unable to do redefinition", e);
      }
    }).start();

    Transform t = new Transform();
    t.sayHi(() -> { System.out.println("Not doing anything here"); });
    t.sayHi(() -> {
      try {
        System.out.println("transforming calling function");
        // Wake up the waiting thread.
        sem_redefine_start.release();
        // Wait for the other thread to finish with redefinition.
        sem_redefine_end.acquire();
      } catch (InterruptedException e) {
        throw new Error("unable to do redefinition", e);
      }
    });
    t.sayHi(() -> { System.out.println("Not doing anything here"); });
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);
}
