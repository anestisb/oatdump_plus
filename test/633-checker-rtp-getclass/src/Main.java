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
  public static void main(String[] args) {
    System.out.println($opt$noinline$foo(new Main()));
    System.out.println($opt$noinline$foo(new SubMain()));
    System.out.println($opt$noinline$foo(new SubSubMain()));
  }


  // Checker test to make sure the only inlined instruction is
  // SubMain.bar.
  /// CHECK-START: int Main.$opt$noinline$foo(Main) inliner (after)
  /// CHECK-DAG:                InvokeVirtual method_name:Main.foo
  /// CHECK-DAG: <<Const:i\d+>> IntConstant 3
  /// CHECK:                    begin_block
  /// CHECK:                    BoundType klass:SubMain
  /// CHECK:                    Return [<<Const>>]
  /// CHECK-NOT:                begin_block
  /// CHECK:                    end_block
  public static int $opt$noinline$foo(Main o) {
    if (doThrow) { throw new Error(); }
    // To exercise the bug on Jack, we need two getClass compares.
    if (o.getClass() == Main.class || o.getClass() != SubMain.class) {
      return o.foo();
    } else {
      // We used to wrongly bound the type of o to `Main` here and then realize that's
      // impossible and mark this branch as dead.
      return o.bar();
    }
  }

  public int bar() {
    return 1;
  }

  public int foo() {
    return 2;
  }

  public static boolean doThrow = false;
}

class SubMain extends Main {
  public int bar() {
    return 3;
  }

  public int foo() {
    return 4;
  }
}

class SubSubMain extends SubMain {
  public int bar() {
    return 5;
  }

  public int foo() {
    return 6;
  }
}
