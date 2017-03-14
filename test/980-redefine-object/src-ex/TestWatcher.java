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

package art.test;

public class TestWatcher {
  // NB This function is native since it is called in the Object.<init> method and so cannot cause
  // any java allocations at all. The normal System.out.print* functions will cause allocations to
  // occur so we cannot use them. This means the easiest way to report the object as being created
  // is to go into native code and do it there.
  public static native void NotifyConstructed(Object o);
}
