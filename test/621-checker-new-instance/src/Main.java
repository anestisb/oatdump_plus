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

public class Main {
  /// CHECK-START: java.lang.Object Main.newObject() prepare_for_register_allocation (before)
  /// CHECK: LoadClass
  /// CHECK: NewInstance

  /// CHECK-START: java.lang.Object Main.newObject() prepare_for_register_allocation (after)
  /// CHECK-NOT: LoadClass
  /// CHECK: NewInstance
  public static Object newObject() {
      return new Object();
  }

  /// CHECK-START: java.lang.Object Main.newFinalizableMayThrow() prepare_for_register_allocation (after)
  /// CHECK: LoadClass
  /// CHECK: NewInstance
  public static Object newFinalizableMayThrow() {
      return $inline$newFinalizableMayThrow();
  }

  public static Object $inline$newFinalizableMayThrow() {
      return new FinalizableMayThrow();
  }

  public static void main(String[] args) {
      newFinalizableMayThrow();
      newObject();
  }
}

class FinalizableMayThrow {
    // clinit may throw OOME.
    static Object o = new Object();
    static String s;
    public void finalize() {
        s = "Test";
    }
}
