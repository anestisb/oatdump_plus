/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "utils.h"

#include <stdlib.h>

#include "base/enums.h"
#include "class_linker-inl.h"
#include "common_runtime_test.h"
#include "exec_utils.h"
#include "mirror/array.h"
#include "mirror/array-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/string.h"
#include "scoped_thread_state_change-inl.h"
#include "handle_scope-inl.h"

#include "base/memory_tool.h"

namespace art {

std::string PrettyArguments(const char* signature);
std::string PrettyReturnType(const char* signature);

class UtilsTest : public CommonRuntimeTest {};

TEST_F(UtilsTest, PrettyDescriptor_ArrayReferences) {
  EXPECT_EQ("java.lang.Class[]", PrettyDescriptor("[Ljava/lang/Class;"));
  EXPECT_EQ("java.lang.Class[][]", PrettyDescriptor("[[Ljava/lang/Class;"));
}

TEST_F(UtilsTest, PrettyDescriptor_ScalarReferences) {
  EXPECT_EQ("java.lang.String", PrettyDescriptor("Ljava.lang.String;"));
  EXPECT_EQ("java.lang.String", PrettyDescriptor("Ljava/lang/String;"));
}

TEST_F(UtilsTest, PrettyDescriptor_Primitive) {
  EXPECT_EQ("boolean", PrettyDescriptor(Primitive::kPrimBoolean));
  EXPECT_EQ("byte", PrettyDescriptor(Primitive::kPrimByte));
  EXPECT_EQ("char", PrettyDescriptor(Primitive::kPrimChar));
  EXPECT_EQ("short", PrettyDescriptor(Primitive::kPrimShort));
  EXPECT_EQ("int", PrettyDescriptor(Primitive::kPrimInt));
  EXPECT_EQ("float", PrettyDescriptor(Primitive::kPrimFloat));
  EXPECT_EQ("long", PrettyDescriptor(Primitive::kPrimLong));
  EXPECT_EQ("double", PrettyDescriptor(Primitive::kPrimDouble));
  EXPECT_EQ("void", PrettyDescriptor(Primitive::kPrimVoid));
}

TEST_F(UtilsTest, PrettyDescriptor_PrimitiveArrays) {
  EXPECT_EQ("boolean[]", PrettyDescriptor("[Z"));
  EXPECT_EQ("boolean[][]", PrettyDescriptor("[[Z"));
  EXPECT_EQ("byte[]", PrettyDescriptor("[B"));
  EXPECT_EQ("byte[][]", PrettyDescriptor("[[B"));
  EXPECT_EQ("char[]", PrettyDescriptor("[C"));
  EXPECT_EQ("char[][]", PrettyDescriptor("[[C"));
  EXPECT_EQ("double[]", PrettyDescriptor("[D"));
  EXPECT_EQ("double[][]", PrettyDescriptor("[[D"));
  EXPECT_EQ("float[]", PrettyDescriptor("[F"));
  EXPECT_EQ("float[][]", PrettyDescriptor("[[F"));
  EXPECT_EQ("int[]", PrettyDescriptor("[I"));
  EXPECT_EQ("int[][]", PrettyDescriptor("[[I"));
  EXPECT_EQ("long[]", PrettyDescriptor("[J"));
  EXPECT_EQ("long[][]", PrettyDescriptor("[[J"));
  EXPECT_EQ("short[]", PrettyDescriptor("[S"));
  EXPECT_EQ("short[][]", PrettyDescriptor("[[S"));
}

TEST_F(UtilsTest, PrettyDescriptor_PrimitiveScalars) {
  EXPECT_EQ("boolean", PrettyDescriptor("Z"));
  EXPECT_EQ("byte", PrettyDescriptor("B"));
  EXPECT_EQ("char", PrettyDescriptor("C"));
  EXPECT_EQ("double", PrettyDescriptor("D"));
  EXPECT_EQ("float", PrettyDescriptor("F"));
  EXPECT_EQ("int", PrettyDescriptor("I"));
  EXPECT_EQ("long", PrettyDescriptor("J"));
  EXPECT_EQ("short", PrettyDescriptor("S"));
}

TEST_F(UtilsTest, PrettyArguments) {
  EXPECT_EQ("()", PrettyArguments("()V"));
  EXPECT_EQ("(int)", PrettyArguments("(I)V"));
  EXPECT_EQ("(int, int)", PrettyArguments("(II)V"));
  EXPECT_EQ("(int, int, int[][])", PrettyArguments("(II[[I)V"));
  EXPECT_EQ("(int, int, int[][], java.lang.Poop)", PrettyArguments("(II[[ILjava/lang/Poop;)V"));
  EXPECT_EQ("(int, int, int[][], java.lang.Poop, java.lang.Poop[][])", PrettyArguments("(II[[ILjava/lang/Poop;[[Ljava/lang/Poop;)V"));
}

TEST_F(UtilsTest, PrettyReturnType) {
  EXPECT_EQ("void", PrettyReturnType("()V"));
  EXPECT_EQ("int", PrettyReturnType("()I"));
  EXPECT_EQ("int[][]", PrettyReturnType("()[[I"));
  EXPECT_EQ("java.lang.Poop", PrettyReturnType("()Ljava/lang/Poop;"));
  EXPECT_EQ("java.lang.Poop[][]", PrettyReturnType("()[[Ljava/lang/Poop;"));
}

TEST_F(UtilsTest, PrettyTypeOf) {
  ScopedObjectAccess soa(Thread::Current());
  EXPECT_EQ("null", mirror::Object::PrettyTypeOf(nullptr));

  StackHandleScope<2> hs(soa.Self());
  Handle<mirror::String> s(hs.NewHandle(mirror::String::AllocFromModifiedUtf8(soa.Self(), "")));
  EXPECT_EQ("java.lang.String", mirror::Object::PrettyTypeOf(s.Get()));

  Handle<mirror::ShortArray> a(hs.NewHandle(mirror::ShortArray::Alloc(soa.Self(), 2)));
  EXPECT_EQ("short[]", mirror::Object::PrettyTypeOf(a.Get()));

  mirror::Class* c = class_linker_->FindSystemClass(soa.Self(), "[Ljava/lang/String;");
  ASSERT_TRUE(c != nullptr);
  mirror::Object* o = mirror::ObjectArray<mirror::String>::Alloc(soa.Self(), c, 0);
  EXPECT_EQ("java.lang.String[]", mirror::Object::PrettyTypeOf(o));
  EXPECT_EQ("java.lang.Class<java.lang.String[]>", mirror::Object::PrettyTypeOf(o->GetClass()));
}

TEST_F(UtilsTest, PrettyClass) {
  ScopedObjectAccess soa(Thread::Current());
  EXPECT_EQ("null", mirror::Class::PrettyClass(nullptr));
  mirror::Class* c = class_linker_->FindSystemClass(soa.Self(), "[Ljava/lang/String;");
  ASSERT_TRUE(c != nullptr);
  mirror::Object* o = mirror::ObjectArray<mirror::String>::Alloc(soa.Self(), c, 0);
  EXPECT_EQ("java.lang.Class<java.lang.String[]>", mirror::Class::PrettyClass(o->GetClass()));
}

TEST_F(UtilsTest, PrettyClassAndClassLoader) {
  ScopedObjectAccess soa(Thread::Current());
  EXPECT_EQ("null", mirror::Class::PrettyClassAndClassLoader(nullptr));
  mirror::Class* c = class_linker_->FindSystemClass(soa.Self(), "[Ljava/lang/String;");
  ASSERT_TRUE(c != nullptr);
  mirror::Object* o = mirror::ObjectArray<mirror::String>::Alloc(soa.Self(), c, 0);
  EXPECT_EQ("java.lang.Class<java.lang.String[],null>",
            mirror::Class::PrettyClassAndClassLoader(o->GetClass()));
}

TEST_F(UtilsTest, PrettyField) {
  ScopedObjectAccess soa(Thread::Current());
  EXPECT_EQ("null", ArtField::PrettyField(nullptr));

  mirror::Class* java_lang_String = class_linker_->FindSystemClass(soa.Self(),
                                                                   "Ljava/lang/String;");

  ArtField* f;
  f = java_lang_String->FindDeclaredInstanceField("count", "I");
  EXPECT_EQ("int java.lang.String.count", f->PrettyField());
  EXPECT_EQ("java.lang.String.count", f->PrettyField(false));
}

TEST_F(UtilsTest, PrettySize) {
  EXPECT_EQ("1GB", PrettySize(1 * GB));
  EXPECT_EQ("2GB", PrettySize(2 * GB));
  if (sizeof(size_t) > sizeof(uint32_t)) {
    EXPECT_EQ("100GB", PrettySize(100 * GB));
  }
  EXPECT_EQ("1024KB", PrettySize(1 * MB));
  EXPECT_EQ("10MB", PrettySize(10 * MB));
  EXPECT_EQ("100MB", PrettySize(100 * MB));
  EXPECT_EQ("1024B", PrettySize(1 * KB));
  EXPECT_EQ("10KB", PrettySize(10 * KB));
  EXPECT_EQ("100KB", PrettySize(100 * KB));
  EXPECT_EQ("0B", PrettySize(0));
  EXPECT_EQ("1B", PrettySize(1));
  EXPECT_EQ("10B", PrettySize(10));
  EXPECT_EQ("100B", PrettySize(100));
  EXPECT_EQ("512B", PrettySize(512));
}

TEST_F(UtilsTest, MangleForJni) {
  ScopedObjectAccess soa(Thread::Current());
  EXPECT_EQ("hello_00024world", MangleForJni("hello$world"));
  EXPECT_EQ("hello_000a9world", MangleForJni("hello\xc2\xa9world"));
  EXPECT_EQ("hello_1world", MangleForJni("hello_world"));
  EXPECT_EQ("Ljava_lang_String_2", MangleForJni("Ljava/lang/String;"));
  EXPECT_EQ("_3C", MangleForJni("[C"));
}

TEST_F(UtilsTest, JniShortName_JniLongName) {
  ScopedObjectAccess soa(Thread::Current());
  mirror::Class* c = class_linker_->FindSystemClass(soa.Self(), "Ljava/lang/String;");
  ASSERT_TRUE(c != nullptr);
  ArtMethod* m;

  m = c->FindVirtualMethod("charAt", "(I)C", kRuntimePointerSize);
  ASSERT_TRUE(m != nullptr);
  EXPECT_EQ("Java_java_lang_String_charAt", m->JniShortName());
  EXPECT_EQ("Java_java_lang_String_charAt__I", m->JniLongName());

  m = c->FindVirtualMethod("indexOf", "(Ljava/lang/String;I)I", kRuntimePointerSize);
  ASSERT_TRUE(m != nullptr);
  EXPECT_EQ("Java_java_lang_String_indexOf", m->JniShortName());
  EXPECT_EQ("Java_java_lang_String_indexOf__Ljava_lang_String_2I", m->JniLongName());

  m = c->FindDirectMethod("copyValueOf", "([CII)Ljava/lang/String;", kRuntimePointerSize);
  ASSERT_TRUE(m != nullptr);
  EXPECT_EQ("Java_java_lang_String_copyValueOf", m->JniShortName());
  EXPECT_EQ("Java_java_lang_String_copyValueOf___3CII", m->JniLongName());
}

TEST_F(UtilsTest, Split) {
  std::vector<std::string> actual;
  std::vector<std::string> expected;

  expected.clear();

  actual.clear();
  Split("", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":", ':', &actual);
  EXPECT_EQ(expected, actual);

  expected.clear();
  expected.push_back("foo");

  actual.clear();
  Split(":foo", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split("foo:", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":foo:", ':', &actual);
  EXPECT_EQ(expected, actual);

  expected.push_back("bar");

  actual.clear();
  Split("foo:bar", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":foo:bar", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split("foo:bar:", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":foo:bar:", ':', &actual);
  EXPECT_EQ(expected, actual);

  expected.push_back("baz");

  actual.clear();
  Split("foo:bar:baz", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":foo:bar:baz", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split("foo:bar:baz:", ':', &actual);
  EXPECT_EQ(expected, actual);

  actual.clear();
  Split(":foo:bar:baz:", ':', &actual);
  EXPECT_EQ(expected, actual);
}

TEST_F(UtilsTest, GetDalvikCacheFilename) {
  std::string name;
  std::string error;

  EXPECT_TRUE(GetDalvikCacheFilename("/system/app/Foo.apk", "/foo", &name, &error)) << error;
  EXPECT_EQ("/foo/system@app@Foo.apk@classes.dex", name);

  EXPECT_TRUE(GetDalvikCacheFilename("/data/app/foo-1.apk", "/foo", &name, &error)) << error;
  EXPECT_EQ("/foo/data@app@foo-1.apk@classes.dex", name);

  EXPECT_TRUE(GetDalvikCacheFilename("/system/framework/core.jar", "/foo", &name, &error)) << error;
  EXPECT_EQ("/foo/system@framework@core.jar@classes.dex", name);

  EXPECT_TRUE(GetDalvikCacheFilename("/system/framework/boot.art", "/foo", &name, &error)) << error;
  EXPECT_EQ("/foo/system@framework@boot.art", name);

  EXPECT_TRUE(GetDalvikCacheFilename("/system/framework/boot.oat", "/foo", &name, &error)) << error;
  EXPECT_EQ("/foo/system@framework@boot.oat", name);
}

TEST_F(UtilsTest, GetDalvikCache) {
  EXPECT_STREQ("", GetDalvikCache("should-not-exist123").c_str());

  EXPECT_STREQ((android_data_ + "/dalvik-cache/.").c_str(), GetDalvikCache(".").c_str());
}


TEST_F(UtilsTest, GetSystemImageFilename) {
  EXPECT_STREQ("/system/framework/arm/boot.art",
               GetSystemImageFilename("/system/framework/boot.art", kArm).c_str());
}

TEST_F(UtilsTest, ExecSuccess) {
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/id");
  } else {
    command.push_back("/usr/bin/id");
  }
  std::string error_msg;
  if (!(RUNNING_ON_MEMORY_TOOL && kMemoryToolDetectsLeaks)) {
    // Running on valgrind fails due to some memory that leaks in thread alternate signal stacks.
    EXPECT_TRUE(Exec(command, &error_msg));
  }
  EXPECT_EQ(0U, error_msg.size()) << error_msg;
}

TEST_F(UtilsTest, ExecError) {
  // This will lead to error messages in the log.
  ScopedLogSeverity sls(LogSeverity::FATAL);

  std::vector<std::string> command;
  command.push_back("bogus");
  std::string error_msg;
  if (!(RUNNING_ON_MEMORY_TOOL && kMemoryToolDetectsLeaks)) {
    // Running on valgrind fails due to some memory that leaks in thread alternate signal stacks.
    EXPECT_FALSE(Exec(command, &error_msg));
    EXPECT_FALSE(error_msg.empty());
  }
}

TEST_F(UtilsTest, EnvSnapshotAdditionsAreNotVisible) {
  static constexpr const char* kModifiedVariable = "EXEC_SHOULD_NOT_EXPORT_THIS";
  static constexpr int kOverwrite = 1;
  // Set an variable in the current environment.
  EXPECT_EQ(setenv(kModifiedVariable, "NEVER", kOverwrite), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/printenv");
  } else {
    command.push_back("/usr/bin/printenv");
  }
  command.push_back(kModifiedVariable);
  std::string error_msg;
  if (!(RUNNING_ON_MEMORY_TOOL && kMemoryToolDetectsLeaks)) {
    // Running on valgrind fails due to some memory that leaks in thread alternate signal stacks.
    EXPECT_FALSE(Exec(command, &error_msg));
    EXPECT_NE(0U, error_msg.size()) << error_msg;
  }
}

TEST_F(UtilsTest, EnvSnapshotDeletionsAreNotVisible) {
  static constexpr const char* kDeletedVariable = "PATH";
  static constexpr int kOverwrite = 1;
  // Save the variable's value.
  const char* save_value = getenv(kDeletedVariable);
  EXPECT_NE(save_value, nullptr);
  // Delete the variable.
  EXPECT_EQ(unsetenv(kDeletedVariable), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    command.push_back(android_root + "/bin/printenv");
  } else {
    command.push_back("/usr/bin/printenv");
  }
  command.push_back(kDeletedVariable);
  std::string error_msg;
  if (!(RUNNING_ON_MEMORY_TOOL && kMemoryToolDetectsLeaks)) {
    // Running on valgrind fails due to some memory that leaks in thread alternate signal stacks.
    EXPECT_TRUE(Exec(command, &error_msg));
    EXPECT_EQ(0U, error_msg.size()) << error_msg;
  }
  // Restore the variable's value.
  EXPECT_EQ(setenv(kDeletedVariable, save_value, kOverwrite), 0);
}

TEST_F(UtilsTest, IsValidDescriptor) {
  std::vector<uint8_t> descriptor(
      { 'L', 'a', '/', 'b', '$', 0xed, 0xa0, 0x80, 0xed, 0xb0, 0x80, ';', 0x00 });
  EXPECT_TRUE(IsValidDescriptor(reinterpret_cast<char*>(&descriptor[0])));

  std::vector<uint8_t> unpaired_surrogate(
      { 'L', 'a', '/', 'b', '$', 0xed, 0xa0, 0x80, ';', 0x00 });
  EXPECT_FALSE(IsValidDescriptor(reinterpret_cast<char*>(&unpaired_surrogate[0])));

  std::vector<uint8_t> unpaired_surrogate_at_end(
      { 'L', 'a', '/', 'b', '$', 0xed, 0xa0, 0x80, 0x00 });
  EXPECT_FALSE(IsValidDescriptor(reinterpret_cast<char*>(&unpaired_surrogate_at_end[0])));

  std::vector<uint8_t> invalid_surrogate(
      { 'L', 'a', '/', 'b', '$', 0xed, 0xb0, 0x80, ';', 0x00 });
  EXPECT_FALSE(IsValidDescriptor(reinterpret_cast<char*>(&invalid_surrogate[0])));

  std::vector<uint8_t> unpaired_surrogate_with_multibyte_sequence(
      { 'L', 'a', '/', 'b', '$', 0xed, 0xb0, 0x80, 0xf0, 0x9f, 0x8f, 0xa0, ';', 0x00 });
  EXPECT_FALSE(
      IsValidDescriptor(reinterpret_cast<char*>(&unpaired_surrogate_with_multibyte_sequence[0])));
}

TEST_F(UtilsTest, ArrayCount) {
  int i[64];
  EXPECT_EQ(ArrayCount(i), 64u);
  char c[7];
  EXPECT_EQ(ArrayCount(c), 7u);
}

TEST_F(UtilsTest, BoundsCheckedCast) {
  char buffer[64];
  const char* buffer_end = buffer + ArrayCount(buffer);
  EXPECT_EQ(BoundsCheckedCast<const uint64_t*>(nullptr, buffer, buffer_end), nullptr);
  EXPECT_EQ(BoundsCheckedCast<const uint64_t*>(buffer, buffer, buffer_end),
            reinterpret_cast<const uint64_t*>(buffer));
  EXPECT_EQ(BoundsCheckedCast<const uint64_t*>(buffer + 56, buffer, buffer_end),
            reinterpret_cast<const uint64_t*>(buffer + 56));
  EXPECT_EQ(BoundsCheckedCast<const uint64_t*>(buffer - 1, buffer, buffer_end), nullptr);
  EXPECT_EQ(BoundsCheckedCast<const uint64_t*>(buffer + 57, buffer, buffer_end), nullptr);
}

}  // namespace art
