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

import java.lang.ref.Reference;
import java.lang.reflect.Constructor;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;

public class Main {
  public static void main(String[] args) throws Exception {
    doTest();
  }

  public static void doTest() throws Exception {
    testClass("java.lang.Object");
    testClass("java.lang.String");
    testClass("java.lang.Math");
    testClass("java.util.List");

    testClass(getProxyClass());

    testClass(int.class);
    testClass(double[].class);

    testClassType(int.class);
    testClassType(getProxyClass());
    testClassType(Runnable.class);
    testClassType(String.class);
    testClassType(ArrayList.class);

    testClassType(int[].class);
    testClassType(Runnable[].class);
    testClassType(String[].class);

    testClassFields(Integer.class);
    testClassFields(int.class);
    testClassFields(String[].class);

    testClassMethods(Integer.class);
    testClassMethods(int.class);
    testClassMethods(String[].class);

    testClassStatus(int.class);
    testClassStatus(String[].class);
    testClassStatus(Object.class);
    testClassStatus(TestForNonInit.class);
    try {
      System.out.println(TestForInitFail.dummy);
    } catch (ExceptionInInitializerError e) {
    }
    testClassStatus(TestForInitFail.class);

    testInterfaces(int.class);
    testInterfaces(String[].class);
    testInterfaces(Object.class);
    testInterfaces(InfA.class);
    testInterfaces(InfB.class);
    testInterfaces(InfC.class);
    testInterfaces(ClassA.class);
    testInterfaces(ClassB.class);
    testInterfaces(ClassC.class);

    testClassLoader(String.class);
    testClassLoader(String[].class);
    testClassLoader(InfA.class);
    testClassLoader(getProxyClass());

    testClassLoaderClasses();

    System.out.println();

    testClassVersion();

    System.out.println();

    testClassEvents();
  }

  private static Class<?> proxyClass = null;

  private static Class<?> getProxyClass() throws Exception {
    if (proxyClass != null) {
      return proxyClass;
    }

    proxyClass = Proxy.getProxyClass(Main.class.getClassLoader(), new Class[] { Runnable.class });
    return proxyClass;
  }

  private static void testClass(String className) throws Exception {
    Class<?> base = Class.forName(className);
    testClass(base);
  }

  private static void testClass(Class<?> base) throws Exception {
    String[] result = getClassSignature(base);
    System.out.println(Arrays.toString(result));
    int mod = getClassModifiers(base);
    if (mod != base.getModifiers()) {
      throw new RuntimeException("Unexpected modifiers: " + base.getModifiers() + " vs " + mod);
    }
    System.out.println(Integer.toHexString(mod));
  }

  private static void testClassType(Class<?> c) throws Exception {
    boolean isInterface = isInterface(c);
    boolean isArray = isArrayClass(c);
    boolean isModifiable = isModifiableClass(c);
    System.out.println(c.getName() + " interface=" + isInterface + " array=" + isArray +
        " modifiable=" + isModifiable);
  }

  private static void testClassFields(Class<?> c) throws Exception {
    System.out.println(Arrays.toString(getClassFields(c)));
  }

  private static void testClassMethods(Class<?> c) throws Exception {
    System.out.println(Arrays.toString(getClassMethods(c)));
  }

  private static void testClassStatus(Class<?> c) {
    System.out.println(c + " " + Integer.toBinaryString(getClassStatus(c)));
  }

  private static void testInterfaces(Class<?> c) {
    System.out.println(c + " " + Arrays.toString(getImplementedInterfaces(c)));
  }

  private static boolean IsBootClassLoader(ClassLoader l) {
    // Hacky check for Android's fake boot classloader.
    return l.getClass().getName().equals("java.lang.BootClassLoader");
  }

  private static void testClassLoader(Class<?> c) {
    Object cl = getClassLoader(c);
    System.out.println(c + " " + (cl != null ? cl.getClass().getName() : "null"));
    if (cl == null) {
      if (c.getClassLoader() != null && !IsBootClassLoader(c.getClassLoader())) {
        throw new RuntimeException("Expected " + c.getClassLoader() + ", but got null.");
      }
    } else {
      if (!(cl instanceof ClassLoader)) {
        throw new RuntimeException("Unexpected \"classloader\": " + cl + " (" + cl.getClass() +
            ")");
      }
      if (cl != c.getClassLoader()) {
        throw new RuntimeException("Unexpected classloader: " + c.getClassLoader() + " vs " + cl);
      }
    }
  }

  private static void testClassLoaderClasses() throws Exception {
    ClassLoader boot = ClassLoader.getSystemClassLoader().getParent();
    while (boot.getParent() != null) {
      boot = boot.getParent();
    }

    System.out.println();
    System.out.println("boot <- src <- src-ex (A,B)");
    ClassLoader cl1 = create(create(boot, DEX1), DEX2);
    Class.forName("B", false, cl1);
    Class.forName("A", false, cl1);
    printClassLoaderClasses(cl1);

    System.out.println();
    System.out.println("boot <- src (B) <- src-ex (A, List)");
    ClassLoader cl2 = create(create(boot, DEX1), DEX2);
    Class.forName("A", false, cl2);
    Class.forName("java.util.List", false, cl2);
    Class.forName("B", false, cl2.getParent());
    printClassLoaderClasses(cl2);

    System.out.println();
    System.out.println("boot <- src+src-ex (A,B)");
    ClassLoader cl3 = create(boot, DEX1, DEX2);
    Class.forName("B", false, cl3);
    Class.forName("A", false, cl3);
    printClassLoaderClasses(cl3);

    // Check that the boot classloader dumps something non-empty.
    Class<?>[] bootClasses = getClassLoaderClasses(boot);
    if (bootClasses.length == 0) {
      throw new RuntimeException("No classes initiated by boot classloader.");
    }
    // Check that at least java.util.List is loaded.
    boolean foundList = false;
    for (Class<?> c : bootClasses) {
      if (c == java.util.List.class) {
        foundList = true;
        break;
      }
    }
    if (!foundList) {
      System.out.println(Arrays.toString(bootClasses));
      throw new RuntimeException("Could not find class java.util.List.");
    }
  }

  private static void testClassVersion() {
    System.out.println(Arrays.toString(getClassVersion(Main.class)));
  }

  private static void testClassEvents() throws Exception {
    ClassLoader cl = Main.class.getClassLoader();
    while (cl.getParent() != null) {
      cl = cl.getParent();
    }
    final ClassLoader boot = cl;

    // The JIT may deeply inline and load some classes. Preload these for test determinism.
    final String PRELOAD_FOR_JIT[] = {
        "java.nio.charset.CoderMalfunctionError",
        "java.util.NoSuchElementException"
    };
    for (String s : PRELOAD_FOR_JIT) {
      Class.forName(s);
    }

    Runnable r = new Runnable() {
      @Override
      public void run() {
        try {
          ClassLoader cl6 = create(boot, DEX1, DEX2);
          System.out.println("C, true");
          Class.forName("C", true, cl6);
        } catch (Exception e) {
          throw new RuntimeException(e);
        }
      }
    };

    Thread dummyThread = new Thread();
    dummyThread.start();
    dummyThread.join();

    ensureJitCompiled(Main.class, "testClassEvents");

    enableClassLoadPreparePrintEvents(true);

    ClassLoader cl1 = create(boot, DEX1, DEX2);
    System.out.println("B, false");
    Class.forName("B", false, cl1);

    ClassLoader cl2 = create(boot, DEX1, DEX2);
    System.out.println("B, true");
    Class.forName("B", true, cl2);

    ClassLoader cl3 = create(boot, DEX1, DEX2);
    System.out.println("C, false");
    Class.forName("C", false, cl3);
    System.out.println("A, false");
    Class.forName("A", false, cl3);

    ClassLoader cl4 = create(boot, DEX1, DEX2);
    System.out.println("C, true");
    Class.forName("C", true, cl4);
    System.out.println("A, true");
    Class.forName("A", true, cl4);

    ClassLoader cl5 = create(boot, DEX1, DEX2);
    System.out.println("A, true");
    Class.forName("A", true, cl5);
    System.out.println("C, true");
    Class.forName("C", true, cl5);

    Thread t = new Thread(r, "TestRunner");
    t.start();
    t.join();

    enableClassLoadPreparePrintEvents(false);

    // Note: the JIT part of this test is about the JIT pulling in a class not yet touched by
    //       anything else in the system. This could be the verifier or the interpreter. We
    //       block the interpreter by calling ensureJitCompiled. The verifier, however, must
    //       run in configurations where dex2oat didn't verify the class itself. So explicitly
    //       check whether the class has been already loaded, and skip then.
    // TODO: Add multiple configurations to the run script once that becomes easier to do.
    if (hasJit() && !isLoadedClass("Main$ClassD")) {
      testClassEventsJit();
    }

    testClassLoadPrepareEquality();
  }

  private static void testClassEventsJit() throws Exception {
    enableClassLoadSeenEvents(true);

    testClassEventsJitImpl();

    enableClassLoadSeenEvents(false);

    if (!hadLoadEvent()) {
      throw new RuntimeException("Did not get expected load event.");
    }
  }

  private static void testClassEventsJitImpl() throws Exception {
    ensureJitCompiled(Main.class, "testClassEventsJitImpl");

    if (ClassD.x != 1) {
      throw new RuntimeException("Unexpected value");
    }
  }

  private static void testClassLoadPrepareEquality() throws Exception {
    setEqualityEventStorageClass(ClassF.class);

    enableClassLoadPrepareEqualityEvents(true);

    Class.forName("Main$ClassE");

    enableClassLoadPrepareEqualityEvents(false);
  }

  private static void printClassLoaderClasses(ClassLoader cl) {
    for (;;) {
      if (cl == null || !cl.getClass().getName().startsWith("dalvik.system")) {
        break;
      }

      ClassLoader saved = cl;
      for (;;) {
        if (cl == null || !cl.getClass().getName().startsWith("dalvik.system")) {
          break;
        }
        String s = cl.toString();
        int index1 = s.indexOf("zip file");
        int index2 = s.indexOf(']', index1);
        if (index2 < 0) {
          throw new RuntimeException("Unexpected classloader " + s);
        }
        String zip_file = s.substring(index1, index2);
        int index3 = zip_file.indexOf('"');
        int index4 = zip_file.indexOf('"', index3 + 1);
        if (index4 < 0) {
          throw new RuntimeException("Unexpected classloader " + s);
        }
        String paths = zip_file.substring(index3 + 1, index4);
        String pathArray[] = paths.split(":");
        for (String path : pathArray) {
          int index5 = path.lastIndexOf('/');
          System.out.print(path.substring(index5 + 1));
          System.out.print('+');
        }
        System.out.print(" -> ");
        cl = cl.getParent();
      }
      System.out.println();
      Class<?> classes[] = getClassLoaderClasses(saved);
      Arrays.sort(classes, new ClassNameComparator());
      System.out.println(Arrays.toString(classes));

      cl = saved.getParent();
    }
  }

  private static native boolean isModifiableClass(Class<?> c);
  private static native String[] getClassSignature(Class<?> c);

  private static native boolean isInterface(Class<?> c);
  private static native boolean isArrayClass(Class<?> c);

  private static native int getClassModifiers(Class<?> c);

  private static native Object[] getClassFields(Class<?> c);
  private static native Object[] getClassMethods(Class<?> c);
  private static native Class<?>[] getImplementedInterfaces(Class<?> c);

  private static native int getClassStatus(Class<?> c);

  private static native Object getClassLoader(Class<?> c);

  private static native Class<?>[] getClassLoaderClasses(ClassLoader cl);

  private static native int[] getClassVersion(Class<?> c);

  private static native void enableClassLoadPreparePrintEvents(boolean b);

  private static native void ensureJitCompiled(Class<?> c, String name);

  private static native boolean hasJit();
  private static native boolean isLoadedClass(String name);
  private static native void enableClassLoadSeenEvents(boolean b);
  private static native boolean hadLoadEvent();

  private static native void setEqualityEventStorageClass(Class<?> c);
  private static native void enableClassLoadPrepareEqualityEvents(boolean b);

  private static class TestForNonInit {
    public static double dummy = Math.random();  // So it can't be compile-time initialized.
  }

  private static class TestForInitFail {
    public static int dummy = ((int)Math.random())/0;  // So it throws when initializing.
  }

  public static interface InfA {
  }
  public static interface InfB extends InfA {
  }
  public static interface InfC extends InfB {
  }

  public abstract static class ClassA implements InfA {
  }
  public abstract static class ClassB extends ClassA implements InfB {
  }
  public abstract static class ClassC implements InfA, InfC {
  }

  public static class ClassD {
    static int x = 1;
  }

  public static class ClassE {
    public void foo() {
    }
    public void bar() {
    }
  }

  public static class ClassF {
    public static Object STATIC = null;
    public static Reference<Object> WEAK = null;
  }

  private static final String DEX1 = System.getenv("DEX_LOCATION") + "/912-classes.jar";
  private static final String DEX2 = System.getenv("DEX_LOCATION") + "/912-classes-ex.jar";

  private static ClassLoader create(ClassLoader parent, String... elements) throws Exception {
    // Note: We use a PathClassLoader, as we do not care about code performance. We only load
    //       the classes, and they're empty.
    Class<?> pathClassLoaderClass = Class.forName("dalvik.system.PathClassLoader");
    Constructor<?> pathClassLoaderInit = pathClassLoaderClass.getConstructor(String.class,
                                                                             ClassLoader.class);
    String path = String.join(":", elements);
    return (ClassLoader) pathClassLoaderInit.newInstance(path, parent);
  }

  private static class ClassNameComparator implements Comparator<Class<?>> {
    public int compare(Class<?> c1, Class<?> c2) {
      return c1.getName().compareTo(c2.getName());
    }
  }
}
