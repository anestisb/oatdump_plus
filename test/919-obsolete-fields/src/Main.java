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

import java.util.function.Consumer;
import java.util.Base64;

public class Main {

  // What follows is the base64 encoded representation of the following class:
  //
  // import java.util.function.Consumer;
  //
  // class Transform {
  //   private Consumer<String> reporter;
  //   public Transform(Consumer<String> reporter) {
  //     this.reporter = reporter;
  //   }
  //
  //   private void Start() {
  //     reporter.accept("Hello - private - Transformed");
  //   }
  //
  //   private void Finish() {
  //     reporter.accept("Goodbye - private - Transformed");
  //   }
  //
  //   public void sayHi(Runnable r) {
  //     reporter.accept("pre Start private method call - Transformed");
  //     Start();
  //     reporter.accept("post Start private method call - Transformed");
  //     r.run();
  //     reporter.accept("pre Finish private method call - Transformed");
  //     Finish();
  //     reporter.accept("post Finish private method call - Transformed");
  //   }
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "yv66vgAAADQANAoADgAfCQANACAIACELACIAIwgAJAgAJQoADQAmCAAnCwAoACkIACoKAA0AKwgA" +
    "LAcALQcALgEACHJlcG9ydGVyAQAdTGphdmEvdXRpbC9mdW5jdGlvbi9Db25zdW1lcjsBAAlTaWdu" +
    "YXR1cmUBADFMamF2YS91dGlsL2Z1bmN0aW9uL0NvbnN1bWVyPExqYXZhL2xhbmcvU3RyaW5nOz47" +
    "AQAGPGluaXQ+AQAgKExqYXZhL3V0aWwvZnVuY3Rpb24vQ29uc3VtZXI7KVYBAARDb2RlAQAPTGlu" +
    "ZU51bWJlclRhYmxlAQA0KExqYXZhL3V0aWwvZnVuY3Rpb24vQ29uc3VtZXI8TGphdmEvbGFuZy9T" +
    "dHJpbmc7PjspVgEABVN0YXJ0AQADKClWAQAGRmluaXNoAQAFc2F5SGkBABcoTGphdmEvbGFuZy9S" +
    "dW5uYWJsZTspVgEAClNvdXJjZUZpbGUBAA5UcmFuc2Zvcm0uamF2YQwAEwAZDAAPABABAB1IZWxs" +
    "byAtIHByaXZhdGUgLSBUcmFuc2Zvcm1lZAcALwwAMAAxAQAfR29vZGJ5ZSAtIHByaXZhdGUgLSBU" +
    "cmFuc2Zvcm1lZAEAK3ByZSBTdGFydCBwcml2YXRlIG1ldGhvZCBjYWxsIC0gVHJhbnNmb3JtZWQM" +
    "ABgAGQEALHBvc3QgU3RhcnQgcHJpdmF0ZSBtZXRob2QgY2FsbCAtIFRyYW5zZm9ybWVkBwAyDAAz" +
    "ABkBACxwcmUgRmluaXNoIHByaXZhdGUgbWV0aG9kIGNhbGwgLSBUcmFuc2Zvcm1lZAwAGgAZAQAt" +
    "cG9zdCBGaW5pc2ggcHJpdmF0ZSBtZXRob2QgY2FsbCAtIFRyYW5zZm9ybWVkAQAJVHJhbnNmb3Jt" +
    "AQAQamF2YS9sYW5nL09iamVjdAEAG2phdmEvdXRpbC9mdW5jdGlvbi9Db25zdW1lcgEABmFjY2Vw" +
    "dAEAFShMamF2YS9sYW5nL09iamVjdDspVgEAEmphdmEvbGFuZy9SdW5uYWJsZQEAA3J1bgAgAA0A" +
    "DgAAAAEAAgAPABAAAQARAAAAAgASAAQAAQATABQAAgAVAAAAKgACAAIAAAAKKrcAASortQACsQAA" +
    "AAEAFgAAAA4AAwAAABUABAAWAAkAFwARAAAAAgAXAAIAGAAZAAEAFQAAACgAAgABAAAADCq0AAIS" +
    "A7kABAIAsQAAAAEAFgAAAAoAAgAAABoACwAbAAIAGgAZAAEAFQAAACgAAgABAAAADCq0AAISBbkA" +
    "BAIAsQAAAAEAFgAAAAoAAgAAAB4ACwAfAAEAGwAcAAEAFQAAAG8AAgACAAAAOyq0AAISBrkABAIA" +
    "KrcAByq0AAISCLkABAIAK7kACQEAKrQAAhIKuQAEAgAqtwALKrQAAhIMuQAEAgCxAAAAAQAWAAAA" +
    "IgAIAAAAIgALACMADwAkABoAJQAgACYAKwAnAC8AKAA6ACkAAQAdAAAAAgAe");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQAw/b59wCwTlSVDmuhPEezuK3oe0rtT4ujMBQAAcAAAAHhWNBIAAAAAAAAAAAgFAAAd" +
    "AAAAcAAAAAYAAADkAAAABAAAAPwAAAABAAAALAEAAAcAAAA0AQAAAQAAAGwBAABABAAAjAEAAJoC" +
    "AACdAgAAoAIAAKgCAACsAgAAsgIAALoCAADbAgAA+gIAAAcDAAAmAwAAOgMAAFADAABkAwAAggMA" +
    "AKEDAACoAwAAuAMAALsDAAC/AwAAxwMAANsDAAAKBAAAOAQAAGYEAACTBAAAnQQAAKIEAACpBAAA" +
    "CAAAAAkAAAAKAAAACwAAAA4AAAARAAAAEQAAAAUAAAAAAAAAEgAAAAUAAACEAgAAEgAAAAUAAACM" +
    "AgAAEgAAAAUAAACUAgAAAAAEABkAAAAAAAMAAgAAAAAAAAAFAAAAAAAAAA8AAAAAAAIAGwAAAAIA" +
    "AAACAAAAAwAAABoAAAAEAAEAEwAAAAAAAAAAAAAAAgAAAAAAAAAQAAAAZAIAAO8EAAAAAAAAAQAA" +
    "ANEEAAABAAAA3wQAAAIAAgABAAAAsAQAAAYAAABwEAQAAABbAQAADgADAAEAAgAAALgEAAAJAAAA" +
    "VCAAABsBBgAAAHIgBgAQAA4AAAADAAEAAgAAAL4EAAAJAAAAVCAAABsBBwAAAHIgBgAQAA4AAAAE" +
    "AAIAAgAAAMQEAAAqAAAAVCAAABsBGAAAAHIgBgAQAHAQAgACAFQgAAAbARYAAAByIAYAEAByEAUA" +
    "AwBUIAAAGwEXAAAAciAGABAAcBABAAIAVCAAABsBFQAAAHIgBgAQAA4AAAAAAAEAAAABAAAAAAAA" +
    "AAAAAACMAQAAAAAAAJQBAAABAAAAAgAAAAEAAAADAAAAAQAAAAQAASgAATwABjxpbml0PgACPjsA" +
    "BD47KVYABkZpbmlzaAAfR29vZGJ5ZSAtIHByaXZhdGUgLSBUcmFuc2Zvcm1lZAAdSGVsbG8gLSBw" +
    "cml2YXRlIC0gVHJhbnNmb3JtZWQAC0xUcmFuc2Zvcm07AB1MZGFsdmlrL2Fubm90YXRpb24vU2ln" +
    "bmF0dXJlOwASTGphdmEvbGFuZy9PYmplY3Q7ABRMamF2YS9sYW5nL1J1bm5hYmxlOwASTGphdmEv" +
    "bGFuZy9TdHJpbmc7ABxMamF2YS91dGlsL2Z1bmN0aW9uL0NvbnN1bWVyAB1MamF2YS91dGlsL2Z1" +
    "bmN0aW9uL0NvbnN1bWVyOwAFU3RhcnQADlRyYW5zZm9ybS5qYXZhAAFWAAJWTAAGYWNjZXB0ABJl" +
    "bWl0dGVyOiBqYWNrLTQuMTkALXBvc3QgRmluaXNoIHByaXZhdGUgbWV0aG9kIGNhbGwgLSBUcmFu" +
    "c2Zvcm1lZAAscG9zdCBTdGFydCBwcml2YXRlIG1ldGhvZCBjYWxsIC0gVHJhbnNmb3JtZWQALHBy" +
    "ZSBGaW5pc2ggcHJpdmF0ZSBtZXRob2QgY2FsbCAtIFRyYW5zZm9ybWVkACtwcmUgU3RhcnQgcHJp" +
    "dmF0ZSBtZXRob2QgY2FsbCAtIFRyYW5zZm9ybWVkAAhyZXBvcnRlcgADcnVuAAVzYXlIaQAFdmFs" +
    "dWUAFQEABw48LQAeAAcOhwAaAAcOhwAiAQAHDoc8hzyHPIcAAgEBHBwEFw0XARcMFwMCAQEcHAUX" +
    "ABcNFwEXDBcEAAEDAQACAIGABJwDAQK4AwEC3AMDAYAEABAAAAAAAAAAAQAAAAAAAAABAAAAHQAA" +
    "AHAAAAACAAAABgAAAOQAAAADAAAABAAAAPwAAAAEAAAAAQAAACwBAAAFAAAABwAAADQBAAAGAAAA" +
    "AQAAAGwBAAADEAAAAgAAAIwBAAABIAAABAAAAJwBAAAGIAAAAQAAAGQCAAABEAAAAwAAAIQCAAAC" +
    "IAAAHQAAAJoCAAADIAAABAAAALAEAAAEIAAAAgAAANEEAAAAIAAAAQAAAO8EAAAAEAAAAQAAAAgF" +
    "AAA=");

  // A class that we can use to keep track of the output of this test.
  private static class TestWatcher implements Consumer<String> {
    private StringBuilder sb;
    public TestWatcher() {
      sb = new StringBuilder();
    }

    @Override
    public void accept(String s) {
      sb.append(s);
      sb.append('\n');
    }

    public String getOutput() {
      return sb.toString();
    }
  }

  public static void main(String[] args) {
    TestWatcher w = new TestWatcher();
    doTest(new Transform(w), w);
  }

  // TODO Workaround to (1) inability to ensure that current_method is not put into a register by
  // the JIT and/or (2) inability to deoptimize frames near runtime functions.
  // TODO Fix one/both of these issues.
  public static void doCall(Runnable r) {
      r.run();
  }

  private static boolean interpreting = true;
  private static boolean retry = false;

  public static void doTest(Transform t, TestWatcher w) {
    Runnable do_redefinition = () -> {
      w.accept("transforming calling function");
      doCommonClassRedefinition(Transform.class, CLASS_BYTES, DEX_BYTES);
    };
    // This just prints something out to show we are running the Runnable.
    Runnable say_nothing = () -> { w.accept("Not doing anything here"); };

    // Try and redefine.
    t.sayHi(say_nothing);
    t.sayHi(do_redefinition);
    t.sayHi(say_nothing);

    // Print output of last run.
    System.out.print(w.getOutput());
  }

  // Transforms the class
  private static native void doCommonClassRedefinition(Class<?> target,
                                                       byte[] classfile,
                                                       byte[] dexfile);
}
