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

class Transform {
  private void Start(Consumer<String> reporter) {
    reporter.accept("hello - private");
  }

  private void Finish(Consumer<String> reporter) {
    reporter.accept("goodbye - private");
  }

  public void sayHi(Runnable r, Consumer<String> reporter) {
    reporter.accept("Pre Start private method call");
    Start(reporter);
    reporter.accept("Post Start private method call");
    // TODO Revisit with b/33616143
    // TODO Uncomment this once either b/33630159 or b/33616143 are resolved.
    // r.run();
    // TODO This doCall function is a very temporary fix until we get either deoptimization near
    // runtime frames working, forcing current method to be always read from the stack or both
    // working.
    Main.doCall(r);
    reporter.accept("Pre Finish private method call");
    Finish(reporter);
    reporter.accept("Post Finish private method call");
  }
}
