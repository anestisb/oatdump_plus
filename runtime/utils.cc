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

#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <memory>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "dex_file-inl.h"
#include "dex_instruction.h"
#include "oat_quick_method_header.h"
#include "os.h"
#include "scoped_thread_state_change-inl.h"
#include "utf-inl.h"

#if defined(__APPLE__)
#include "AvailabilityMacros.h"  // For MAC_OS_X_VERSION_MAX_ALLOWED
#include <sys/syscall.h>
#include <crt_externs.h>
#endif

#if defined(__linux__)
#include <linux/unistd.h>
#endif

namespace art {

using android::base::StringAppendF;
using android::base::StringPrintf;

pid_t GetTid() {
#if defined(__APPLE__)
  uint64_t owner;
  CHECK_PTHREAD_CALL(pthread_threadid_np, (nullptr, &owner), __FUNCTION__);  // Requires Mac OS 10.6
  return owner;
#elif defined(__BIONIC__)
  return gettid();
#else
  return syscall(__NR_gettid);
#endif
}

std::string GetThreadName(pid_t tid) {
  std::string result;
  if (ReadFileToString(StringPrintf("/proc/self/task/%d/comm", tid), &result)) {
    result.resize(result.size() - 1);  // Lose the trailing '\n'.
  } else {
    result = "<unknown>";
  }
  return result;
}

bool ReadFileToString(const std::string& file_name, std::string* result) {
  File file(file_name, O_RDONLY, false);
  if (!file.IsOpened()) {
    return false;
  }

  std::vector<char> buf(8 * KB);
  while (true) {
    int64_t n = TEMP_FAILURE_RETRY(read(file.Fd(), &buf[0], buf.size()));
    if (n == -1) {
      return false;
    }
    if (n == 0) {
      return true;
    }
    result->append(&buf[0], n);
  }
}

bool PrintFileToLog(const std::string& file_name, LogSeverity level) {
  File file(file_name, O_RDONLY, false);
  if (!file.IsOpened()) {
    return false;
  }

  constexpr size_t kBufSize = 256;  // Small buffer. Avoid stack overflow and stack size warnings.
  char buf[kBufSize + 1];           // +1 for terminator.
  size_t filled_to = 0;
  while (true) {
    DCHECK_LT(filled_to, kBufSize);
    int64_t n = TEMP_FAILURE_RETRY(read(file.Fd(), &buf[filled_to], kBufSize - filled_to));
    if (n <= 0) {
      // Print the rest of the buffer, if it exists.
      if (filled_to > 0) {
        buf[filled_to] = 0;
        LOG(level) << buf;
      }
      return n == 0;
    }
    // Scan for '\n'.
    size_t i = filled_to;
    bool found_newline = false;
    for (; i < filled_to + n; ++i) {
      if (buf[i] == '\n') {
        // Found a line break, that's something to print now.
        buf[i] = 0;
        LOG(level) << buf;
        // Copy the rest to the front.
        if (i + 1 < filled_to + n) {
          memmove(&buf[0], &buf[i + 1], filled_to + n - i - 1);
          filled_to = filled_to + n - i - 1;
        } else {
          filled_to = 0;
        }
        found_newline = true;
        break;
      }
    }
    if (found_newline) {
      continue;
    } else {
      filled_to += n;
      // Check if we must flush now.
      if (filled_to == kBufSize) {
        buf[kBufSize] = 0;
        LOG(level) << buf;
        filled_to = 0;
      }
    }
  }
}

std::string PrettyDescriptor(const char* descriptor) {
  // Count the number of '['s to get the dimensionality.
  const char* c = descriptor;
  size_t dim = 0;
  while (*c == '[') {
    dim++;
    c++;
  }

  // Reference or primitive?
  if (*c == 'L') {
    // "[[La/b/C;" -> "a.b.C[][]".
    c++;  // Skip the 'L'.
  } else {
    // "[[B" -> "byte[][]".
    // To make life easier, we make primitives look like unqualified
    // reference types.
    switch (*c) {
    case 'B': c = "byte;"; break;
    case 'C': c = "char;"; break;
    case 'D': c = "double;"; break;
    case 'F': c = "float;"; break;
    case 'I': c = "int;"; break;
    case 'J': c = "long;"; break;
    case 'S': c = "short;"; break;
    case 'Z': c = "boolean;"; break;
    case 'V': c = "void;"; break;  // Used when decoding return types.
    default: return descriptor;
    }
  }

  // At this point, 'c' is a string of the form "fully/qualified/Type;"
  // or "primitive;". Rewrite the type with '.' instead of '/':
  std::string result;
  const char* p = c;
  while (*p != ';') {
    char ch = *p++;
    if (ch == '/') {
      ch = '.';
    }
    result.push_back(ch);
  }
  // ...and replace the semicolon with 'dim' "[]" pairs:
  for (size_t i = 0; i < dim; ++i) {
    result += "[]";
  }
  return result;
}

std::string PrettyArguments(const char* signature) {
  std::string result;
  result += '(';
  CHECK_EQ(*signature, '(');
  ++signature;  // Skip the '('.
  while (*signature != ')') {
    size_t argument_length = 0;
    while (signature[argument_length] == '[') {
      ++argument_length;
    }
    if (signature[argument_length] == 'L') {
      argument_length = (strchr(signature, ';') - signature + 1);
    } else {
      ++argument_length;
    }
    {
      std::string argument_descriptor(signature, argument_length);
      result += PrettyDescriptor(argument_descriptor.c_str());
    }
    if (signature[argument_length] != ')') {
      result += ", ";
    }
    signature += argument_length;
  }
  CHECK_EQ(*signature, ')');
  ++signature;  // Skip the ')'.
  result += ')';
  return result;
}

std::string PrettyReturnType(const char* signature) {
  const char* return_type = strchr(signature, ')');
  CHECK(return_type != nullptr);
  ++return_type;  // Skip ')'.
  return PrettyDescriptor(return_type);
}

std::string PrettyJavaAccessFlags(uint32_t access_flags) {
  std::string result;
  if ((access_flags & kAccPublic) != 0) {
    result += "public ";
  }
  if ((access_flags & kAccProtected) != 0) {
    result += "protected ";
  }
  if ((access_flags & kAccPrivate) != 0) {
    result += "private ";
  }
  if ((access_flags & kAccFinal) != 0) {
    result += "final ";
  }
  if ((access_flags & kAccStatic) != 0) {
    result += "static ";
  }
  if ((access_flags & kAccAbstract) != 0) {
    result += "abstract ";
  }
  if ((access_flags & kAccInterface) != 0) {
    result += "interface ";
  }
  if ((access_flags & kAccTransient) != 0) {
    result += "transient ";
  }
  if ((access_flags & kAccVolatile) != 0) {
    result += "volatile ";
  }
  if ((access_flags & kAccSynchronized) != 0) {
    result += "synchronized ";
  }
  return result;
}

std::string PrettySize(int64_t byte_count) {
  // The byte thresholds at which we display amounts.  A byte count is displayed
  // in unit U when kUnitThresholds[U] <= bytes < kUnitThresholds[U+1].
  static const int64_t kUnitThresholds[] = {
    0,              // B up to...
    3*1024,         // KB up to...
    2*1024*1024,    // MB up to...
    1024*1024*1024  // GB from here.
  };
  static const int64_t kBytesPerUnit[] = { 1, KB, MB, GB };
  static const char* const kUnitStrings[] = { "B", "KB", "MB", "GB" };
  const char* negative_str = "";
  if (byte_count < 0) {
    negative_str = "-";
    byte_count = -byte_count;
  }
  int i = arraysize(kUnitThresholds);
  while (--i > 0) {
    if (byte_count >= kUnitThresholds[i]) {
      break;
    }
  }
  return StringPrintf("%s%" PRId64 "%s",
                      negative_str, byte_count / kBytesPerUnit[i], kUnitStrings[i]);
}

static inline constexpr bool NeedsEscaping(uint16_t ch) {
  return (ch < ' ' || ch > '~');
}

std::string PrintableChar(uint16_t ch) {
  std::string result;
  result += '\'';
  if (NeedsEscaping(ch)) {
    StringAppendF(&result, "\\u%04x", ch);
  } else {
    result += ch;
  }
  result += '\'';
  return result;
}

std::string PrintableString(const char* utf) {
  std::string result;
  result += '"';
  const char* p = utf;
  size_t char_count = CountModifiedUtf8Chars(p);
  for (size_t i = 0; i < char_count; ++i) {
    uint32_t ch = GetUtf16FromUtf8(&p);
    if (ch == '\\') {
      result += "\\\\";
    } else if (ch == '\n') {
      result += "\\n";
    } else if (ch == '\r') {
      result += "\\r";
    } else if (ch == '\t') {
      result += "\\t";
    } else {
      const uint16_t leading = GetLeadingUtf16Char(ch);

      if (NeedsEscaping(leading)) {
        StringAppendF(&result, "\\u%04x", leading);
      } else {
        result += leading;
      }

      const uint32_t trailing = GetTrailingUtf16Char(ch);
      if (trailing != 0) {
        // All high surrogates will need escaping.
        StringAppendF(&result, "\\u%04x", trailing);
      }
    }
  }
  result += '"';
  return result;
}

std::string GetJniShortName(const std::string& class_descriptor, const std::string& method) {
  // Remove the leading 'L' and trailing ';'...
  std::string class_name(class_descriptor);
  CHECK_EQ(class_name[0], 'L') << class_name;
  CHECK_EQ(class_name[class_name.size() - 1], ';') << class_name;
  class_name.erase(0, 1);
  class_name.erase(class_name.size() - 1, 1);

  std::string short_name;
  short_name += "Java_";
  short_name += MangleForJni(class_name);
  short_name += "_";
  short_name += MangleForJni(method);
  return short_name;
}

// See http://java.sun.com/j2se/1.5.0/docs/guide/jni/spec/design.html#wp615 for the full rules.
std::string MangleForJni(const std::string& s) {
  std::string result;
  size_t char_count = CountModifiedUtf8Chars(s.c_str());
  const char* cp = &s[0];
  for (size_t i = 0; i < char_count; ++i) {
    uint32_t ch = GetUtf16FromUtf8(&cp);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      result.push_back(ch);
    } else if (ch == '.' || ch == '/') {
      result += "_";
    } else if (ch == '_') {
      result += "_1";
    } else if (ch == ';') {
      result += "_2";
    } else if (ch == '[') {
      result += "_3";
    } else {
      const uint16_t leading = GetLeadingUtf16Char(ch);
      const uint32_t trailing = GetTrailingUtf16Char(ch);

      StringAppendF(&result, "_0%04x", leading);
      if (trailing != 0) {
        StringAppendF(&result, "_0%04x", trailing);
      }
    }
  }
  return result;
}

std::string DotToDescriptor(const char* class_name) {
  std::string descriptor(class_name);
  std::replace(descriptor.begin(), descriptor.end(), '.', '/');
  if (descriptor.length() > 0 && descriptor[0] != '[') {
    descriptor = "L" + descriptor + ";";
  }
  return descriptor;
}

std::string DescriptorToDot(const char* descriptor) {
  size_t length = strlen(descriptor);
  if (length > 1) {
    if (descriptor[0] == 'L' && descriptor[length - 1] == ';') {
      // Descriptors have the leading 'L' and trailing ';' stripped.
      std::string result(descriptor + 1, length - 2);
      std::replace(result.begin(), result.end(), '/', '.');
      return result;
    } else {
      // For arrays the 'L' and ';' remain intact.
      std::string result(descriptor);
      std::replace(result.begin(), result.end(), '/', '.');
      return result;
    }
  }
  // Do nothing for non-class/array descriptors.
  return descriptor;
}

std::string DescriptorToName(const char* descriptor) {
  size_t length = strlen(descriptor);
  if (descriptor[0] == 'L' && descriptor[length - 1] == ';') {
    std::string result(descriptor + 1, length - 2);
    return result;
  }
  return descriptor;
}

// Helper for IsValidPartOfMemberNameUtf8(), a bit vector indicating valid low ascii.
uint32_t DEX_MEMBER_VALID_LOW_ASCII[4] = {
  0x00000000,  // 00..1f low control characters; nothing valid
  0x03ff2010,  // 20..3f digits and symbols; valid: '0'..'9', '$', '-'
  0x87fffffe,  // 40..5f uppercase etc.; valid: 'A'..'Z', '_'
  0x07fffffe   // 60..7f lowercase etc.; valid: 'a'..'z'
};

// Helper for IsValidPartOfMemberNameUtf8(); do not call directly.
bool IsValidPartOfMemberNameUtf8Slow(const char** pUtf8Ptr) {
  /*
   * It's a multibyte encoded character. Decode it and analyze. We
   * accept anything that isn't (a) an improperly encoded low value,
   * (b) an improper surrogate pair, (c) an encoded '\0', (d) a high
   * control character, or (e) a high space, layout, or special
   * character (U+00a0, U+2000..U+200f, U+2028..U+202f,
   * U+fff0..U+ffff). This is all specified in the dex format
   * document.
   */

  const uint32_t pair = GetUtf16FromUtf8(pUtf8Ptr);
  const uint16_t leading = GetLeadingUtf16Char(pair);

  // We have a surrogate pair resulting from a valid 4 byte UTF sequence.
  // No further checks are necessary because 4 byte sequences span code
  // points [U+10000, U+1FFFFF], which are valid codepoints in a dex
  // identifier. Furthermore, GetUtf16FromUtf8 guarantees that each of
  // the surrogate halves are valid and well formed in this instance.
  if (GetTrailingUtf16Char(pair) != 0) {
    return true;
  }


  // We've encountered a one, two or three byte UTF-8 sequence. The
  // three byte UTF-8 sequence could be one half of a surrogate pair.
  switch (leading >> 8) {
    case 0x00:
      // It's only valid if it's above the ISO-8859-1 high space (0xa0).
      return (leading > 0x00a0);
    case 0xd8:
    case 0xd9:
    case 0xda:
    case 0xdb:
      {
        // We found a three byte sequence encoding one half of a surrogate.
        // Look for the other half.
        const uint32_t pair2 = GetUtf16FromUtf8(pUtf8Ptr);
        const uint16_t trailing = GetLeadingUtf16Char(pair2);

        return (GetTrailingUtf16Char(pair2) == 0) && (0xdc00 <= trailing && trailing <= 0xdfff);
      }
    case 0xdc:
    case 0xdd:
    case 0xde:
    case 0xdf:
      // It's a trailing surrogate, which is not valid at this point.
      return false;
    case 0x20:
    case 0xff:
      // It's in the range that has spaces, controls, and specials.
      switch (leading & 0xfff8) {
        case 0x2000:
        case 0x2008:
        case 0x2028:
        case 0xfff0:
        case 0xfff8:
          return false;
      }
      return true;
    default:
      return true;
  }

  UNREACHABLE();
}

/* Return whether the pointed-at modified-UTF-8 encoded character is
 * valid as part of a member name, updating the pointer to point past
 * the consumed character. This will consume two encoded UTF-16 code
 * points if the character is encoded as a surrogate pair. Also, if
 * this function returns false, then the given pointer may only have
 * been partially advanced.
 */
static bool IsValidPartOfMemberNameUtf8(const char** pUtf8Ptr) {
  uint8_t c = (uint8_t) **pUtf8Ptr;
  if (LIKELY(c <= 0x7f)) {
    // It's low-ascii, so check the table.
    uint32_t wordIdx = c >> 5;
    uint32_t bitIdx = c & 0x1f;
    (*pUtf8Ptr)++;
    return (DEX_MEMBER_VALID_LOW_ASCII[wordIdx] & (1 << bitIdx)) != 0;
  }

  // It's a multibyte encoded character. Call a non-inline function
  // for the heavy lifting.
  return IsValidPartOfMemberNameUtf8Slow(pUtf8Ptr);
}

bool IsValidMemberName(const char* s) {
  bool angle_name = false;

  switch (*s) {
    case '\0':
      // The empty string is not a valid name.
      return false;
    case '<':
      angle_name = true;
      s++;
      break;
  }

  while (true) {
    switch (*s) {
      case '\0':
        return !angle_name;
      case '>':
        return angle_name && s[1] == '\0';
    }

    if (!IsValidPartOfMemberNameUtf8(&s)) {
      return false;
    }
  }
}

enum ClassNameType { kName, kDescriptor };
template<ClassNameType kType, char kSeparator>
static bool IsValidClassName(const char* s) {
  int arrayCount = 0;
  while (*s == '[') {
    arrayCount++;
    s++;
  }

  if (arrayCount > 255) {
    // Arrays may have no more than 255 dimensions.
    return false;
  }

  ClassNameType type = kType;
  if (type != kDescriptor && arrayCount != 0) {
    /*
     * If we're looking at an array of some sort, then it doesn't
     * matter if what is being asked for is a class name; the
     * format looks the same as a type descriptor in that case, so
     * treat it as such.
     */
    type = kDescriptor;
  }

  if (type == kDescriptor) {
    /*
     * We are looking for a descriptor. Either validate it as a
     * single-character primitive type, or continue on to check the
     * embedded class name (bracketed by "L" and ";").
     */
    switch (*(s++)) {
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'I':
    case 'J':
    case 'S':
    case 'Z':
      // These are all single-character descriptors for primitive types.
      return (*s == '\0');
    case 'V':
      // Non-array void is valid, but you can't have an array of void.
      return (arrayCount == 0) && (*s == '\0');
    case 'L':
      // Class name: Break out and continue below.
      break;
    default:
      // Oddball descriptor character.
      return false;
    }
  }

  /*
   * We just consumed the 'L' that introduces a class name as part
   * of a type descriptor, or we are looking for an unadorned class
   * name.
   */

  bool sepOrFirst = true;  // first character or just encountered a separator.
  for (;;) {
    uint8_t c = (uint8_t) *s;
    switch (c) {
    case '\0':
      /*
       * Premature end for a type descriptor, but valid for
       * a class name as long as we haven't encountered an
       * empty component (including the degenerate case of
       * the empty string "").
       */
      return (type == kName) && !sepOrFirst;
    case ';':
      /*
       * Invalid character for a class name, but the
       * legitimate end of a type descriptor. In the latter
       * case, make sure that this is the end of the string
       * and that it doesn't end with an empty component
       * (including the degenerate case of "L;").
       */
      return (type == kDescriptor) && !sepOrFirst && (s[1] == '\0');
    case '/':
    case '.':
      if (c != kSeparator) {
        // The wrong separator character.
        return false;
      }
      if (sepOrFirst) {
        // Separator at start or two separators in a row.
        return false;
      }
      sepOrFirst = true;
      s++;
      break;
    default:
      if (!IsValidPartOfMemberNameUtf8(&s)) {
        return false;
      }
      sepOrFirst = false;
      break;
    }
  }
}

bool IsValidBinaryClassName(const char* s) {
  return IsValidClassName<kName, '.'>(s);
}

bool IsValidJniClassName(const char* s) {
  return IsValidClassName<kName, '/'>(s);
}

bool IsValidDescriptor(const char* s) {
  return IsValidClassName<kDescriptor, '/'>(s);
}

void Split(const std::string& s, char separator, std::vector<std::string>* result) {
  const char* p = s.data();
  const char* end = p + s.size();
  while (p != end) {
    if (*p == separator) {
      ++p;
    } else {
      const char* start = p;
      while (++p != end && *p != separator) {
        // Skip to the next occurrence of the separator.
      }
      result->push_back(std::string(start, p - start));
    }
  }
}

void SetThreadName(const char* thread_name) {
  int hasAt = 0;
  int hasDot = 0;
  const char* s = thread_name;
  while (*s) {
    if (*s == '.') {
      hasDot = 1;
    } else if (*s == '@') {
      hasAt = 1;
    }
    s++;
  }
  int len = s - thread_name;
  if (len < 15 || hasAt || !hasDot) {
    s = thread_name;
  } else {
    s = thread_name + len - 15;
  }
#if defined(__linux__)
  // pthread_setname_np fails rather than truncating long strings.
  char buf[16];       // MAX_TASK_COMM_LEN=16 is hard-coded in the kernel.
  strncpy(buf, s, sizeof(buf)-1);
  buf[sizeof(buf)-1] = '\0';
  errno = pthread_setname_np(pthread_self(), buf);
  if (errno != 0) {
    PLOG(WARNING) << "Unable to set the name of current thread to '" << buf << "'";
  }
#else  // __APPLE__
  pthread_setname_np(thread_name);
#endif
}

void GetTaskStats(pid_t tid, char* state, int* utime, int* stime, int* task_cpu) {
  *utime = *stime = *task_cpu = 0;
  std::string stats;
  if (!ReadFileToString(StringPrintf("/proc/self/task/%d/stat", tid), &stats)) {
    return;
  }
  // Skip the command, which may contain spaces.
  stats = stats.substr(stats.find(')') + 2);
  // Extract the three fields we care about.
  std::vector<std::string> fields;
  Split(stats, ' ', &fields);
  *state = fields[0][0];
  *utime = strtoull(fields[11].c_str(), nullptr, 10);
  *stime = strtoull(fields[12].c_str(), nullptr, 10);
  *task_cpu = strtoull(fields[36].c_str(), nullptr, 10);
}

static const char* GetAndroidDirSafe(const char* env_var,
                                     const char* default_dir,
                                     std::string* error_msg) {
  const char* android_dir = getenv(env_var);
  if (android_dir == nullptr) {
    if (OS::DirectoryExists(default_dir)) {
      android_dir = default_dir;
    } else {
      *error_msg = StringPrintf("%s not set and %s does not exist", env_var, default_dir);
      return nullptr;
    }
  }
  if (!OS::DirectoryExists(android_dir)) {
    *error_msg = StringPrintf("Failed to find %s directory %s", env_var, android_dir);
    return nullptr;
  }
  return android_dir;
}

const char* GetAndroidDir(const char* env_var, const char* default_dir) {
  std::string error_msg;
  const char* dir = GetAndroidDirSafe(env_var, default_dir, &error_msg);
  if (dir != nullptr) {
    return dir;
  } else {
    LOG(FATAL) << error_msg;
    return nullptr;
  }
}

const char* GetAndroidRoot() {
  return GetAndroidDir("ANDROID_ROOT", "/system");
}

const char* GetAndroidRootSafe(std::string* error_msg) {
  return GetAndroidDirSafe("ANDROID_ROOT", "/system", error_msg);
}

const char* GetAndroidData() {
  return GetAndroidDir("ANDROID_DATA", "/data");
}

const char* GetAndroidDataSafe(std::string* error_msg) {
  return GetAndroidDirSafe("ANDROID_DATA", "/data", error_msg);
}

std::string GetDefaultBootImageLocation(std::string* error_msg) {
  const char* android_root = GetAndroidRootSafe(error_msg);
  if (android_root == nullptr) {
    return "";
  }
  return StringPrintf("%s/framework/boot.art", android_root);
}

void GetDalvikCache(const char* subdir, const bool create_if_absent, std::string* dalvik_cache,
                    bool* have_android_data, bool* dalvik_cache_exists, bool* is_global_cache) {
  CHECK(subdir != nullptr);
  std::string error_msg;
  const char* android_data = GetAndroidDataSafe(&error_msg);
  if (android_data == nullptr) {
    *have_android_data = false;
    *dalvik_cache_exists = false;
    *is_global_cache = false;
    return;
  } else {
    *have_android_data = true;
  }
  const std::string dalvik_cache_root(StringPrintf("%s/dalvik-cache/", android_data));
  *dalvik_cache = dalvik_cache_root + subdir;
  *dalvik_cache_exists = OS::DirectoryExists(dalvik_cache->c_str());
  *is_global_cache = strcmp(android_data, "/data") == 0;
  if (create_if_absent && !*dalvik_cache_exists && !*is_global_cache) {
    // Don't create the system's /data/dalvik-cache/... because it needs special permissions.
    *dalvik_cache_exists = ((mkdir(dalvik_cache_root.c_str(), 0700) == 0 || errno == EEXIST) &&
                            (mkdir(dalvik_cache->c_str(), 0700) == 0 || errno == EEXIST));
  }
}

std::string GetDalvikCache(const char* subdir) {
  CHECK(subdir != nullptr);
  const char* android_data = GetAndroidData();
  const std::string dalvik_cache_root(StringPrintf("%s/dalvik-cache/", android_data));
  const std::string dalvik_cache = dalvik_cache_root + subdir;
  if (!OS::DirectoryExists(dalvik_cache.c_str())) {
    // TODO: Check callers. Traditional behavior is to not abort.
    return "";
  }
  return dalvik_cache;
}

bool GetDalvikCacheFilename(const char* location, const char* cache_location,
                            std::string* filename, std::string* error_msg) {
  if (location[0] != '/') {
    *error_msg = StringPrintf("Expected path in location to be absolute: %s", location);
    return false;
  }
  std::string cache_file(&location[1]);  // skip leading slash
  if (!android::base::EndsWith(location, ".dex") &&
      !android::base::EndsWith(location, ".art") &&
      !android::base::EndsWith(location, ".oat")) {
    cache_file += "/";
    cache_file += DexFile::kClassesDex;
  }
  std::replace(cache_file.begin(), cache_file.end(), '/', '@');
  *filename = StringPrintf("%s/%s", cache_location, cache_file.c_str());
  return true;
}

std::string GetVdexFilename(const std::string& oat_location) {
  return ReplaceFileExtension(oat_location, "vdex");
}

static void InsertIsaDirectory(const InstructionSet isa, std::string* filename) {
  // in = /foo/bar/baz
  // out = /foo/bar/<isa>/baz
  size_t pos = filename->rfind('/');
  CHECK_NE(pos, std::string::npos) << *filename << " " << isa;
  filename->insert(pos, "/", 1);
  filename->insert(pos + 1, GetInstructionSetString(isa));
}

std::string GetSystemImageFilename(const char* location, const InstructionSet isa) {
  // location = /system/framework/boot.art
  // filename = /system/framework/<isa>/boot.art
  std::string filename(location);
  InsertIsaDirectory(isa, &filename);
  return filename;
}

bool FileExists(const std::string& filename) {
  struct stat buffer;
  return stat(filename.c_str(), &buffer) == 0;
}

bool FileExistsAndNotEmpty(const std::string& filename) {
  struct stat buffer;
  if (stat(filename.c_str(), &buffer) != 0) {
    return false;
  }
  return buffer.st_size > 0;
}

std::string ReplaceFileExtension(const std::string& filename, const std::string& new_extension) {
  const size_t last_ext = filename.find_last_of('.');
  if (last_ext == std::string::npos) {
    return filename + "." + new_extension;
  } else {
    return filename.substr(0, last_ext + 1) + new_extension;
  }
}

std::string PrettyDescriptor(Primitive::Type type) {
  return PrettyDescriptor(Primitive::Descriptor(type));
}

static void ParseStringAfterChar(const std::string& s,
                                 char c,
                                 std::string* parsed_value,
                                 UsageFn Usage) {
  std::string::size_type colon = s.find(c);
  if (colon == std::string::npos) {
    Usage("Missing char %c in option %s\n", c, s.c_str());
  }
  // Add one to remove the char we were trimming until.
  *parsed_value = s.substr(colon + 1);
}

void ParseDouble(const std::string& option,
                 char after_char,
                 double min,
                 double max,
                 double* parsed_value,
                 UsageFn Usage) {
  std::string substring;
  ParseStringAfterChar(option, after_char, &substring, Usage);
  bool sane_val = true;
  double value;
  if ((false)) {
    // TODO: this doesn't seem to work on the emulator.  b/15114595
    std::stringstream iss(substring);
    iss >> value;
    // Ensure that we have a value, there was no cruft after it and it satisfies a sensible range.
    sane_val = iss.eof() && (value >= min) && (value <= max);
  } else {
    char* end = nullptr;
    value = strtod(substring.c_str(), &end);
    sane_val = *end == '\0' && value >= min && value <= max;
  }
  if (!sane_val) {
    Usage("Invalid double value %s for option %s\n", substring.c_str(), option.c_str());
  }
  *parsed_value = value;
}

int64_t GetFileSizeBytes(const std::string& filename) {
  struct stat stat_buf;
  int rc = stat(filename.c_str(), &stat_buf);
  return rc == 0 ? stat_buf.st_size : -1;
}

void SleepForever() {
  while (true) {
    usleep(1000000);
  }
}

}  // namespace art
