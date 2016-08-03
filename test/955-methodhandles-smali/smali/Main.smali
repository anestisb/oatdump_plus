# Copyright 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class LMain;
.super Ljava/lang/Object;

# MethodHandle Main.getHandleForVirtual(Class<?> defc, String name, MethodType type);
#
# Returns a handle to a virtual method on |defc| named name with type |type| using
# the public lookup object.
.method public static getHandleForVirtual(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
.registers 5

    # Get a reference to the public lookup object (MethodHandles.publicLookup()).
    invoke-static {}, Ljava/lang/invoke/MethodHandles;->publicLookup()Ljava/lang/invoke/MethodHandles$Lookup;
    move-result-object v0

    # Call Lookup.findVirtual(defc, name, type);
    invoke-virtual {v0, p0, p1, p2}, Ljava/lang/invoke/MethodHandles$Lookup;->findVirtual(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
    move-result-object v1
    return-object v1
.end method

# MethodHandle Main.getHandleForStatic(Class<?> defc, String name, MethodType type);
#
# Returns a handle to a static method on |defc| named name with type |type| using
# the public lookup object.
.method public static getHandleForStatic(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
.registers 5

    # Get a reference to the public lookup object (MethodHandles.publicLookup()).
    invoke-static {}, Ljava/lang/invoke/MethodHandles;->publicLookup()Ljava/lang/invoke/MethodHandles$Lookup;
    move-result-object v0

    # Call Lookup.findStatic(defc, name, type);
    invoke-virtual {v0, p0, p1, p2}, Ljava/lang/invoke/MethodHandles$Lookup;->findStatic(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
    move-result-object v1
    return-object v1
.end method

# Returns a method handle to String java.lang.String.concat(String);
.method public static getStringConcatHandle()Ljava/lang/invoke/MethodHandle;
.registers 3
    const-string v0, "concat"
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;
    move-result-object v1

    # Call MethodType.methodType(rtype=String.class, ptype[0] = String.class)
    invoke-static {v1, v1}, Ljava/lang/invoke/MethodType;->methodType(Ljava/lang/Class;Ljava/lang/Class;)Ljava/lang/invoke/MethodType;
    move-result-object v2

    # Call Main.getHandleForVirtual(String.class, "concat", methodType);
    invoke-static {v1, v0, v2}, LMain;->getHandleForVirtual(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
    move-result-object v0
    return-object v0
.end method

# Returns a method handle to static String java.lang.String.valueOf(String);
.method public static getStringValueOfHandle()Ljava/lang/invoke/MethodHandle;
.registers 4
    # set v0 to java.lang.Object.class
    new-instance v0, Ljava/lang/Object;
    invoke-direct {v0}, Ljava/lang/Object;-><init>()V
    invoke-virtual {v0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;
    move-result-object v0

    # set v1 to the name of the method ("valueOf") and v2 to java.lang.String.class;
    const-string v1, "valueOf"
    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;
    move-result-object v2

    # Call MethodType.methodType(rtype=String.class, ptype[0]=Object.class)
    invoke-static {v2, v0}, Ljava/lang/invoke/MethodType;->methodType(Ljava/lang/Class;Ljava/lang/Class;)Ljava/lang/invoke/MethodType;
    move-result-object v3

    # Call Main.getHandleForStatic(String.class, "valueOf", methodType);
    invoke-static {v2, v1, v3}, LMain;->getHandleForStatic(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;
    move-result-object v0
    return-object v0
.end method


.method public static main([Ljava/lang/String;)V
.registers 5

    # Test case 1: Exercise String.concat(String, String) which is a virtual method.
    invoke-static {}, LMain;->getStringConcatHandle()Ljava/lang/invoke/MethodHandle;
    move-result-object v0
    const-string v1, "[String1]"
    const-string v2, "+[String2]"
    invoke-polymorphic {v0, v1, v2}, Ljava/lang/invoke/MethodHandle;->invokeExact([Ljava/lang/Object;)Ljava/lang/Object;, (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
    move-result-object v3
    sget-object v4, Ljava/lang/System;->out:Ljava/io/PrintStream;
    invoke-virtual {v4, v3}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V

    # Test case 2: Exercise String.valueOf(Object);
    invoke-static {}, LMain;->getStringValueOfHandle()Ljava/lang/invoke/MethodHandle;
    move-result-object v0
    const-string v1, "[String1]"
    invoke-polymorphic {v0, v1}, Ljava/lang/invoke/MethodHandle;->invokeExact([Ljava/lang/Object;)Ljava/lang/Object;, (Ljava/lang/Object;)Ljava/lang/String;
    move-result-object v3
    sget-object v4, Ljava/lang/System;->out:Ljava/io/PrintStream;
    invoke-virtual {v4, v3}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V

    return-void
.end method
