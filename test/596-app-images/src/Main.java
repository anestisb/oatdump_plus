/*
 * Copyright 2016 The Android Open Source Project
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

class Main {
  static class Inner {
    final public static int abc = 10;
  }

  static class Nested {

  }

  public static void main(String[] args) {
    System.loadLibrary(args[0]);
    if (!checkAppImageLoaded()) {
      System.out.println("App image is not loaded!");
    } else if (!checkAppImageContains(Inner.class)) {
      System.out.println("App image does not contain Inner!");
    }

    if (!checkInitialized(Inner.class))
      System.out.println("Inner class is not initialized!");

    if (!checkInitialized(Nested.class))
      System.out.println("Nested class is not initialized!");

    if (!checkInitialized(StaticFields.class))
      System.out.println("StaticFields class is not initialized!");

    if (!checkInitialized(StaticFieldsInitSub.class))
      System.out.println("StaticFieldsInitSub class is not initialized!");

    if (!checkInitialized(StaticFieldsInit.class))
      System.out.println("StaticFieldsInit class is not initialized!");

    if (checkInitialized(StaticInternString.class))
      System.out.println("StaticInternString class is initialized!");
  }

  public static native boolean checkAppImageLoaded();
  public static native boolean checkAppImageContains(Class<?> klass);
  public static native boolean checkInitialized(Class<?> klass);
}

class StaticFields{
  public static int abc;
}

class StaticFieldsInitSub extends StaticFieldsInit {
  final public static int def = 10;
}

class StaticFieldsInit{
  final public static int abc = 10;
}

class StaticInternString {
  final public static String intern = "java.abc.Action";
}

