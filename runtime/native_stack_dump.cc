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

#include "native_stack_dump.h"

#include <ostream>

#include <stdio.h>

#include "art_method.h"

// For DumpNativeStack.
#include <backtrace/Backtrace.h>
#include <backtrace/BacktraceMap.h>

#if defined(__linux__)

#include <memory>
#include <vector>

#include <linux/unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

#include "arch/instruction_set.h"
#include "base/memory_tool.h"
#include "base/mutex.h"
#include "base/stringprintf.h"
#include "oat_quick_method_header.h"
#include "thread-inl.h"
#include "utils.h"

#endif

namespace art {

#if defined(__linux__)

static constexpr bool kUseAddr2line = !kIsTargetBuild;

ALWAYS_INLINE
static inline void WritePrefix(std::ostream* os, const char* prefix, bool odd) {
  if (prefix != nullptr) {
    *os << prefix;
  }
  *os << "  ";
  if (!odd) {
    *os << " ";
  }
}

static bool RunCommand(std::string cmd, std::ostream* os, const char* prefix) {
  FILE* stream = popen(cmd.c_str(), "r");
  if (stream) {
    if (os != nullptr) {
      bool odd_line = true;               // We indent them differently.
      bool wrote_prefix = false;          // Have we already written a prefix?
      constexpr size_t kMaxBuffer = 128;  // Relatively small buffer. Should be OK as we're on an
                                          // alt stack, but just to be sure...
      char buffer[kMaxBuffer];
      while (!feof(stream)) {
        if (fgets(buffer, kMaxBuffer, stream) != nullptr) {
          // Split on newlines.
          char* tmp = buffer;
          for (;;) {
            char* new_line = strchr(tmp, '\n');
            if (new_line == nullptr) {
              // Print the rest.
              if (*tmp != 0) {
                if (!wrote_prefix) {
                  WritePrefix(os, prefix, odd_line);
                }
                wrote_prefix = true;
                *os << tmp;
              }
              break;
            }
            if (!wrote_prefix) {
              WritePrefix(os, prefix, odd_line);
            }
            char saved = *(new_line + 1);
            *(new_line + 1) = 0;
            *os << tmp;
            *(new_line + 1) = saved;
            tmp = new_line + 1;
            odd_line = !odd_line;
            wrote_prefix = false;
          }
        }
      }
    }
    pclose(stream);
    return true;
  } else {
    return false;
  }
}

static void Addr2line(const std::string& map_src, uintptr_t offset, std::ostream& os,
                      const char* prefix) {
  std::string cmdline(StringPrintf("addr2line --functions --inlines --demangle -e %s %zx",
                                   map_src.c_str(), offset));
  RunCommand(cmdline.c_str(), &os, prefix);
}

static bool PcIsWithinQuickCode(ArtMethod* method, uintptr_t pc) NO_THREAD_SAFETY_ANALYSIS {
  uintptr_t code = reinterpret_cast<uintptr_t>(EntryPointToCodePointer(
      method->GetEntryPointFromQuickCompiledCode()));
  if (code == 0) {
    return pc == 0;
  }
  uintptr_t code_size = reinterpret_cast<const OatQuickMethodHeader*>(code)[-1].code_size_;
  return code <= pc && pc <= (code + code_size);
}

void DumpNativeStack(std::ostream& os, pid_t tid, BacktraceMap* existing_map, const char* prefix,
    ArtMethod* current_method, void* ucontext_ptr) {
  // b/18119146
  if (RUNNING_ON_MEMORY_TOOL != 0) {
    return;
  }

  BacktraceMap* map = existing_map;
  std::unique_ptr<BacktraceMap> tmp_map;
  if (map == nullptr) {
    tmp_map.reset(BacktraceMap::Create(getpid()));
    map = tmp_map.get();
  }
  std::unique_ptr<Backtrace> backtrace(Backtrace::Create(BACKTRACE_CURRENT_PROCESS, tid, map));
  if (!backtrace->Unwind(0, reinterpret_cast<ucontext*>(ucontext_ptr))) {
    os << prefix << "(backtrace::Unwind failed for thread " << tid
       << ": " <<  backtrace->GetErrorString(backtrace->GetError()) << ")\n";
    return;
  } else if (backtrace->NumFrames() == 0) {
    os << prefix << "(no native stack frames for thread " << tid << ")\n";
    return;
  }

  // Check whether we have and should use addr2line.
  bool use_addr2line;
  if (kUseAddr2line) {
    // Try to run it to see whether we have it. Push an argument so that it doesn't assume a.out
    // and print to stderr.
    use_addr2line = (gAborting > 0) && RunCommand("addr2line -h", nullptr, nullptr);
  } else {
    use_addr2line = false;
  }

  for (Backtrace::const_iterator it = backtrace->begin();
       it != backtrace->end(); ++it) {
    // We produce output like this:
    // ]    #00 pc 000075bb8  /system/lib/libc.so (unwind_backtrace_thread+536)
    // In order for parsing tools to continue to function, the stack dump
    // format must at least adhere to this format:
    //  #XX pc <RELATIVE_ADDR>  <FULL_PATH_TO_SHARED_LIBRARY> ...
    // The parsers require a single space before and after pc, and two spaces
    // after the <RELATIVE_ADDR>. There can be any prefix data before the
    // #XX. <RELATIVE_ADDR> has to be a hex number but with no 0x prefix.
    os << prefix << StringPrintf("#%02zu pc ", it->num);
    bool try_addr2line = false;
    if (!BacktraceMap::IsValid(it->map)) {
      os << StringPrintf(Is64BitInstructionSet(kRuntimeISA) ? "%016" PRIxPTR "  ???"
                                                            : "%08" PRIxPTR "  ???",
                         it->pc);
    } else {
      os << StringPrintf(Is64BitInstructionSet(kRuntimeISA) ? "%016" PRIxPTR "  "
                                                            : "%08" PRIxPTR "  ",
                         BacktraceMap::GetRelativePc(it->map, it->pc));
      os << it->map.name;
      os << " (";
      if (!it->func_name.empty()) {
        os << it->func_name;
        if (it->func_offset != 0) {
          os << "+" << it->func_offset;
        }
        try_addr2line = true;
      } else if (current_method != nullptr &&
          Locks::mutator_lock_->IsSharedHeld(Thread::Current()) &&
          PcIsWithinQuickCode(current_method, it->pc)) {
        const void* start_of_code = current_method->GetEntryPointFromQuickCompiledCode();
        os << JniLongName(current_method) << "+"
           << (it->pc - reinterpret_cast<uintptr_t>(start_of_code));
      } else {
        os << "???";
      }
      os << ")";
    }
    os << "\n";
    if (try_addr2line && use_addr2line) {
      Addr2line(it->map.name, it->pc - it->map.start, os, prefix);
    }
  }
}

void DumpKernelStack(std::ostream& os, pid_t tid, const char* prefix, bool include_count) {
  if (tid == GetTid()) {
    // There's no point showing that we're reading our stack out of /proc!
    return;
  }

  std::string kernel_stack_filename(StringPrintf("/proc/self/task/%d/stack", tid));
  std::string kernel_stack;
  if (!ReadFileToString(kernel_stack_filename, &kernel_stack)) {
    os << prefix << "(couldn't read " << kernel_stack_filename << ")\n";
    return;
  }

  std::vector<std::string> kernel_stack_frames;
  Split(kernel_stack, '\n', &kernel_stack_frames);
  // We skip the last stack frame because it's always equivalent to "[<ffffffff>] 0xffffffff",
  // which looking at the source appears to be the kernel's way of saying "that's all, folks!".
  kernel_stack_frames.pop_back();
  for (size_t i = 0; i < kernel_stack_frames.size(); ++i) {
    // Turn "[<ffffffff8109156d>] futex_wait_queue_me+0xcd/0x110"
    // into "futex_wait_queue_me+0xcd/0x110".
    const char* text = kernel_stack_frames[i].c_str();
    const char* close_bracket = strchr(text, ']');
    if (close_bracket != nullptr) {
      text = close_bracket + 2;
    }
    os << prefix;
    if (include_count) {
      os << StringPrintf("#%02zd ", i);
    }
    os << text << "\n";
  }
}

#elif defined(__APPLE__)

void DumpNativeStack(std::ostream& os ATTRIBUTE_UNUSED,
                     pid_t tid ATTRIBUTE_UNUSED,
                     BacktraceMap* existing_map ATTRIBUTE_UNUSED,
                     const char* prefix ATTRIBUTE_UNUSED,
                     ArtMethod* current_method ATTRIBUTE_UNUSED,
                     void* ucontext_ptr ATTRIBUTE_UNUSED) {
}

void DumpKernelStack(std::ostream& os ATTRIBUTE_UNUSED,
                     pid_t tid ATTRIBUTE_UNUSED,
                     const char* prefix ATTRIBUTE_UNUSED,
                     bool include_count ATTRIBUTE_UNUSED) {
}

#else
#error "Unsupported architecture for native stack dumps."
#endif

}  // namespace art
