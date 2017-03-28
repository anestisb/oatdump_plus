/*
 * Copyright (C) 2017 The Android Open Source Project
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

  /// CHECK-START: int Main.inlineMonomorphic(Main) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Main.getValue

  /// CHECK-START: int Main.inlineMonomorphic(Main) inliner (after)
  /// CHECK-NOT:   InvokeVirtual method_name:Main.getValue

  /// CHECK-START: int Main.inlineMonomorphic(Main) licm (before)
  /// CHECK:   <<Deopt:l\d+>> Deoptimize
  /// CHECK:                  InstanceFieldGet [<<Deopt>>] field_name:Main.value

  /// CHECK-START: int Main.inlineMonomorphic(Main) licm (after)
  /// CHECK:   <<Deopt:l\d+>> Deoptimize
  /// CHECK:                  InstanceFieldGet [<<Deopt>>] field_name:Main.value

  public static int inlineMonomorphic(Main a) {
    if (a == null) {
      return 42;
    }
    int i = 0;
    while (i < 100) {
      i += a.getValue();
    }
    return i;
  }

  /// CHECK-START: int Main.inlinePolymorphic(Main) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Main.getValue

  /// CHECK-START: int Main.inlinePolymorphic(Main) inliner (after)
  /// CHECK-NOT:   InvokeVirtual method_name:Main.getValue

  /// CHECK-START: int Main.inlineMonomorphic(Main) licm (before)
  /// CHECK:   <<Deopt:l\d+>> Deoptimize
  /// CHECK:                  InstanceFieldGet [<<Deopt>>] field_name:Main.value

  /// CHECK-START: int Main.inlineMonomorphic(Main) licm (after)
  /// CHECK:   <<Deopt:l\d+>> Deoptimize
  /// CHECK:                  InstanceFieldGet [<<Deopt>>] field_name:Main.value
  public static int inlinePolymorphic(Main a) {
    return a.getValue();
  }

  public int getValue() {
    return value;
  }

  public static void main(String[] args) {
    inlineMonomorphic(new Main());
  }

  int value = 1;
}

// Add a subclass of 'Main' to write the polymorphic inline cache in the profile.
class SubMain extends Main {
}
