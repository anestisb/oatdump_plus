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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;
import java.lang.invoke.MethodType;
import java.lang.invoke.WrongMethodTypeException;

public class Main {

  public static class A {
    public void foo() {
      System.out.println("foo_A");
    }

    public static final Lookup lookup = MethodHandles.lookup();
  }

  public static class B extends A {
    public void foo() {
      System.out.println("foo_B");
    }

    public static final Lookup lookup = MethodHandles.lookup();
  }

  public static class C extends B {
    public static final Lookup lookup = MethodHandles.lookup();
  }

  public static class D {
    private final void privateRyan() {
      System.out.println("privateRyan_D");
    }

    public static final Lookup lookup = MethodHandles.lookup();
  }

  public static class E extends D {
    public static final Lookup lookup = MethodHandles.lookup();
  }

  public static void main(String[] args) throws Throwable {
    testfindSpecial_invokeSuperBehaviour();
    testfindSpecial_invokeDirectBehaviour();
  }

  public static void testfindSpecial_invokeSuperBehaviour() throws Throwable {
    // This is equivalent to an invoke-super instruction where the referrer
    // is B.class.
    MethodHandle mh1 = B.lookup.findSpecial(A.class /* refC */, "foo",
        MethodType.methodType(void.class), B.class /* specialCaller */);

    A aInstance = new A();
    B bInstance = new B();
    C cInstance = new C();

    // This should be as if an invoke-super was called from one of B's methods.
    mh1.invokeExact(bInstance);
    mh1.invoke(bInstance);

    // This should not work. The receiver type in the handle will be suitably
    // restricted to B and subclasses.
    try {
      mh1.invoke(aInstance);
      System.out.println("mh1.invoke(aInstance) should not succeeed");
    } catch (ClassCastException expected) {
    }

    try {
      mh1.invokeExact(aInstance);
      System.out.println("mh1.invoke(aInstance) should not succeeed");
    } catch (WrongMethodTypeException expected) {
    } catch (ClassCastException workaround) {
      // TODO(narayan): ART treats all invokes as if they were non-exact. We
      // should throw a WMTE if we execute an invoke-polymorphic instruction whose
      // target method is MethodHandle.invokeExact.
    }

    // This should *still* be as if an invoke-super was called from one of C's
    // methods, despite the fact that we're operating on a C.
    mh1.invoke(cInstance);

    // Now that C is the special caller, the next invoke will call B.foo.
    MethodHandle mh2 = C.lookup.findSpecial(A.class /* refC */, "foo",
        MethodType.methodType(void.class), C.class /* specialCaller */);
    mh2.invokeExact(cInstance);

    // Shouldn't allow invoke-super semantics from an unrelated special caller.
    try {
      C.lookup.findSpecial(A.class, "foo",
        MethodType.methodType(void.class), D.class /* specialCaller */);
      System.out.println("findSpecial(A.class, foo, .. D.class) unexpectedly succeeded.");
    } catch (IllegalAccessException expected) {
    }
  }

  public static void testfindSpecial_invokeDirectBehaviour() throws Throwable {
    D dInstance = new D();

    MethodHandle mh3 = D.lookup.findSpecial(D.class, "privateRyan",
        MethodType.methodType(void.class), D.class /* specialCaller */);
    mh3.invoke(dInstance);

    // The private method shouldn't be accessible from any special caller except
    // itself...
    try {
      D.lookup.findSpecial(D.class, "privateRyan", MethodType.methodType(void.class), C.class);
      System.out.println("findSpecial(privateRyan, C.class) unexpectedly succeeded");
    } catch (IllegalAccessException expected) {
    }

    // ... or from any lookup context except its own.
    try {
      E.lookup.findSpecial(D.class, "privateRyan", MethodType.methodType(void.class), E.class);
      System.out.println("findSpecial(privateRyan, E.class) unexpectedly succeeded");
    } catch (IllegalAccessException expected) {
    }
  }
}


