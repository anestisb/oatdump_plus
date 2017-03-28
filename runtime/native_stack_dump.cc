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

#include "android-base/stringprintf.h"

#include "arch/instruction_set.h"
#include "base/memory_tool.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "oat_quick_method_header.h"
#include "os.h"
#include "thread-inl.h"
#include "utils.h"

#endif

namespace art {

#if defined(__linux__)

using android::base::StringPrintf;

static constexpr bool kUseAddr2line = !kIsTargetBuild;

ALWAYS_INLINE
static inline void WritePrefix(std::ostream& os, const char* prefix, bool odd) {
  if (prefix != nullptr) {
    os << prefix;
  }
  os << "  ";
  if (!odd) {
    os << " ";
  }
}

// The state of an open pipe to addr2line. In "server" mode, addr2line takes input on stdin
// and prints the result to stdout. This struct keeps the state of the open connection.
struct Addr2linePipe {
  Addr2linePipe(int in_fd, int out_fd, const std::string& file_name, pid_t pid)
      : in(in_fd, false), out(out_fd, false), file(file_name), child_pid(pid), odd(true) {}

  ~Addr2linePipe() {
    kill(child_pid, SIGKILL);
  }

  File in;      // The file descriptor that is connected to the output of addr2line.
  File out;     // The file descriptor that is connected to the input of addr2line.

  const std::string file;     // The file addr2line is working on, so that we know when to close
                              // and restart.
  const pid_t child_pid;      // The pid of the child, which we should kill when we're done.
  bool odd;                   // Print state for indentation of lines.
};

static std::unique_ptr<Addr2linePipe> Connect(const std::string& name, const char* args[]) {
  int caller_to_addr2line[2];
  int addr2line_to_caller[2];

  if (pipe(caller_to_addr2line) == -1) {
    return nullptr;
  }
  if (pipe(addr2line_to_caller) == -1) {
    close(caller_to_addr2line[0]);
    close(caller_to_addr2line[1]);
    return nullptr;
  }

  pid_t pid = fork();
  if (pid == -1) {
    close(caller_to_addr2line[0]);
    close(caller_to_addr2line[1]);
    close(addr2line_to_caller[0]);
    close(addr2line_to_caller[1]);
    return nullptr;
  }

  if (pid == 0) {
    dup2(caller_to_addr2line[0], STDIN_FILENO);
    dup2(addr2line_to_caller[1], STDOUT_FILENO);

    close(caller_to_addr2line[0]);
    close(caller_to_addr2line[1]);
    close(addr2line_to_caller[0]);
    close(addr2line_to_caller[1]);

    execv(args[0], const_cast<char* const*>(args));
    exit(1);
  } else {
    close(caller_to_addr2line[0]);
    close(addr2line_to_caller[1]);
    return std::unique_ptr<Addr2linePipe>(new Addr2linePipe(addr2line_to_caller[0],
                                                            caller_to_addr2line[1],
                                                            name,
                                                            pid));
  }
}

static void Drain(size_t expected,
                  const char* prefix,
                  std::unique_ptr<Addr2linePipe>* pipe /* inout */,
                  std::ostream& os) {
  DCHECK(pipe != nullptr);
  DCHECK(pipe->get() != nullptr);
  int in = pipe->get()->in.Fd();
  DCHECK_GE(in, 0);

  bool prefix_written = false;

  for (;;) {
    constexpr uint32_t kWaitTimeExpectedMicros = 500 * 1000;
    constexpr uint32_t kWaitTimeUnexpectedMicros = 50 * 1000;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = expected > 0 ? kWaitTimeExpectedMicros : kWaitTimeUnexpectedMicros;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(in, &rfds);

    int retval = TEMP_FAILURE_RETRY(select(in + 1, &rfds, nullptr, nullptr, &tv));

    if (retval < 0) {
      // Other side may have crashed or other errors.
      pipe->reset();
      return;
    }

    if (retval == 0) {
      // Timeout.
      return;
    }

    DCHECK_EQ(retval, 1);

    constexpr size_t kMaxBuffer = 128;  // Relatively small buffer. Should be OK as we're on an
    // alt stack, but just to be sure...
    char buffer[kMaxBuffer];
    memset(buffer, 0, kMaxBuffer);
    int bytes_read = TEMP_FAILURE_RETRY(read(in, buffer, kMaxBuffer - 1));

    if (bytes_read < 0) {
      // This should not really happen...
      pipe->reset();
      return;
    }

    char* tmp = buffer;
    while (*tmp != 0) {
      if (!prefix_written) {
        WritePrefix(os, prefix, (*pipe)->odd);
        prefix_written = true;
      }
      char* new_line = strchr(tmp, '\n');
      if (new_line == nullptr) {
        os << tmp;

        break;
      } else {
        char saved = *(new_line + 1);
        *(new_line + 1) = 0;
        os << tmp;
        *(new_line + 1) = saved;

        tmp = new_line + 1;
        prefix_written = false;
        (*pipe)->odd = !(*pipe)->odd;

        if (expected > 0) {
          expected--;
        }
      }
    }
  }
}

static void Addr2line(const std::string& map_src,
                      uintptr_t offset,
                      std::ostream& os,
                      const char* prefix,
                      std::unique_ptr<Addr2linePipe>* pipe /* inout */) {
  DCHECK(pipe != nullptr);

  if (map_src == "[vdso]") {
    // Special-case this, our setup has problems with this.
    return;
  }

  if (*pipe == nullptr || (*pipe)->file != map_src) {
    if (*pipe != nullptr) {
      Drain(0, prefix, pipe, os);
    }
    pipe->reset();  // Close early.

    const char* args[7] = {
        "/usr/bin/addr2line",
        "--functions",
        "--inlines",
        "--demangle",
        "-e",
        map_src.c_str(),
        nullptr
    };
    *pipe = Connect(map_src, args);
  }

  Addr2linePipe* pipe_ptr = pipe->get();
  if (pipe_ptr == nullptr) {
    // Failed...
    return;
  }

  // Send the offset.
  const std::string hex_offset = StringPrintf("%zx\n", offset);

  if (!pipe_ptr->out.WriteFully(hex_offset.data(), hex_offset.length())) {
    // Error. :-(
    pipe->reset();
    return;
  }

  // Now drain (expecting two lines).
  Drain(2U, prefix, pipe, os);
}

static bool RunCommand(const std::string& cmd) {
  FILE* stream = popen(cmd.c_str(), "r");
  if (stream) {
    pclose(stream);
    return true;
  } else {
    return false;
  }
}

static bool PcIsWithinQuickCode(ArtMethod* method, uintptr_t pc) NO_THREAD_SAFETY_ANALYSIS {
  uintptr_t code = reinterpret_cast<uintptr_t>(EntryPointToCodePointer(
      method->GetEntryPointFromQuickCompiledCode()));
  if (code == 0) {
    return pc == 0;
  }
  uintptr_t code_size = reinterpret_cast<const OatQuickMethodHeader*>(code)[-1].GetCodeSize();
  return code <= pc && pc <= (code + code_size);
}

void DumpNativeStack(std::ostream& os,
                     pid_t tid,
                     BacktraceMap* existing_map,
                     const char* prefix,
                     ArtMethod* current_method,
                     void* ucontext_ptr) {
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
       << ": " <<  backtrace->GetErrorString(backtrace->GetError()) << ")" << std::endl;
    return;
  } else if (backtrace->NumFrames() == 0) {
    os << prefix << "(no native stack frames for thread " << tid << ")" << std::endl;
    return;
  }

  // Check whether we have and should use addr2line.
  bool use_addr2line;
  if (kUseAddr2line) {
    // Try to run it to see whether we have it. Push an argument so that it doesn't assume a.out
    // and print to stderr.
    use_addr2line = (gAborting > 0) && RunCommand("addr2line -h");
  } else {
    use_addr2line = false;
  }

  std::unique_ptr<Addr2linePipe> addr2line_state;

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
        os << current_method->JniLongName() << "+"
           << (it->pc - reinterpret_cast<uintptr_t>(start_of_code));
      } else {
        os << "???";
      }
      os << ")";
    }
    os << std::endl;
    if (try_addr2line && use_addr2line) {
      Addr2line(it->map.name, it->pc - it->map.start, os, prefix, &addr2line_state);
    }
  }

  if (addr2line_state != nullptr) {
    Drain(0, prefix, &addr2line_state, os);
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
    os << text << std::endl;
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
