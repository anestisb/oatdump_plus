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

class Transform {
  private void Start() {
    if (Main.doPrint()) {
      System.out.println("hello - private");
    }
  }

  private void Finish() {
    if (Main.doPrint()) {
      System.out.println("goodbye - private");
    }
  }

  public void sayHi(Runnable r) {
    if (Main.doPrint()) {
      System.out.println("Pre Start private method call");
    }
    Start();
    if (Main.doPrint()) {
      System.out.println("Post Start private method call");
    }
    // TODO Revist with b/33616143
    // TODO Uncomment this
    // r.run();
    // TODO This is a very temporary fix until we get either deoptimization near runtime frames
    // working, forcing current method to be always read from the stack or both working.
    Main.doCall(r);
    if (Main.doPrint()) {
      System.out.println("Pre Finish private method call");
    }
    Finish();
    if (Main.doPrint()) {
      System.out.println("Post Finish private method call");
    }
  }
}
