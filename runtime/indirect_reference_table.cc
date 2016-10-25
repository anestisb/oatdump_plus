/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include "indirect_reference_table-inl.h"

#include "base/dumpable-inl.h"
#include "base/systrace.h"
#include "jni_internal.h"
#include "nth_caller_visitor.h"
#include "reference_table.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "utils.h"
#include "verify_object-inl.h"

#include <cstdlib>

namespace art {

static constexpr bool kDumpStackOnNonLocalReference = false;

const char* GetIndirectRefKindString(const IndirectRefKind& kind) {
  switch (kind) {
    case kHandleScopeOrInvalid:
      return "HandleScopeOrInvalid";
    case kLocal:
      return "Local";
    case kGlobal:
      return "Global";
    case kWeakGlobal:
      return "WeakGlobal";
  }
  return "IndirectRefKind Error";
}

void IndirectReferenceTable::AbortIfNoCheckJNI(const std::string& msg) {
  // If -Xcheck:jni is on, it'll give a more detailed error before aborting.
  JavaVMExt* vm = Runtime::Current()->GetJavaVM();
  if (!vm->IsCheckJniEnabled()) {
    // Otherwise, we want to abort rather than hand back a bad reference.
    LOG(FATAL) << msg;
  } else {
    LOG(ERROR) << msg;
  }
}

IndirectReferenceTable::IndirectReferenceTable(size_t max_count,
                                               IndirectRefKind desired_kind,
                                               std::string* error_msg)
    : kind_(desired_kind),
      max_entries_(max_count) {
  CHECK(error_msg != nullptr);
  CHECK_NE(desired_kind, kHandleScopeOrInvalid);

  const size_t table_bytes = max_count * sizeof(IrtEntry);
  table_mem_map_.reset(MemMap::MapAnonymous("indirect ref table", nullptr, table_bytes,
                                            PROT_READ | PROT_WRITE, false, false, error_msg));
  if (table_mem_map_.get() == nullptr && error_msg->empty()) {
    *error_msg = "Unable to map memory for indirect ref table";
  }

  if (table_mem_map_.get() != nullptr) {
    table_ = reinterpret_cast<IrtEntry*>(table_mem_map_->Begin());
  } else {
    table_ = nullptr;
  }
  segment_state_.all = IRT_FIRST_SEGMENT;
}

IndirectReferenceTable::~IndirectReferenceTable() {
}

void IndirectReferenceTable::ConstexprChecks() {
  // Use this for some assertions. They can't be put into the header as C++ wants the class
  // to be complete.

  // Check kind.
  static_assert((EncodeIndirectRefKind(kLocal) & (~kKindMask)) == 0, "Kind encoding error");
  static_assert((EncodeIndirectRefKind(kGlobal) & (~kKindMask)) == 0, "Kind encoding error");
  static_assert((EncodeIndirectRefKind(kWeakGlobal) & (~kKindMask)) == 0, "Kind encoding error");
  static_assert(DecodeIndirectRefKind(EncodeIndirectRefKind(kLocal)) == kLocal,
                "Kind encoding error");
  static_assert(DecodeIndirectRefKind(EncodeIndirectRefKind(kGlobal)) == kGlobal,
                "Kind encoding error");
  static_assert(DecodeIndirectRefKind(EncodeIndirectRefKind(kWeakGlobal)) == kWeakGlobal,
                "Kind encoding error");

  // Check serial.
  static_assert(DecodeSerial(EncodeSerial(0u)) == 0u, "Serial encoding error");
  static_assert(DecodeSerial(EncodeSerial(1u)) == 1u, "Serial encoding error");
  static_assert(DecodeSerial(EncodeSerial(2u)) == 2u, "Serial encoding error");
  static_assert(DecodeSerial(EncodeSerial(3u)) == 3u, "Serial encoding error");

  // Table index.
  static_assert(DecodeIndex(EncodeIndex(0u)) == 0u, "Index encoding error");
  static_assert(DecodeIndex(EncodeIndex(1u)) == 1u, "Index encoding error");
  static_assert(DecodeIndex(EncodeIndex(2u)) == 2u, "Index encoding error");
  static_assert(DecodeIndex(EncodeIndex(3u)) == 3u, "Index encoding error");
}

bool IndirectReferenceTable::IsValid() const {
  return table_mem_map_.get() != nullptr;
}

IndirectRef IndirectReferenceTable::Add(uint32_t cookie, ObjPtr<mirror::Object> obj) {
  IRTSegmentState prevState;
  prevState.all = cookie;
  size_t topIndex = segment_state_.parts.topIndex;

  CHECK(obj != nullptr);
  VerifyObject(obj);
  DCHECK(table_ != nullptr);
  DCHECK_GE(segment_state_.parts.numHoles, prevState.parts.numHoles);

  if (topIndex == max_entries_) {
    LOG(FATAL) << "JNI ERROR (app bug): " << kind_ << " table overflow "
               << "(max=" << max_entries_ << ")\n"
               << MutatorLockedDumpable<IndirectReferenceTable>(*this);
  }

  // We know there's enough room in the table.  Now we just need to find
  // the right spot.  If there's a hole, find it and fill it; otherwise,
  // add to the end of the list.
  IndirectRef result;
  int numHoles = segment_state_.parts.numHoles - prevState.parts.numHoles;
  size_t index;
  if (numHoles > 0) {
    DCHECK_GT(topIndex, 1U);
    // Find the first hole; likely to be near the end of the list.
    IrtEntry* pScan = &table_[topIndex - 1];
    DCHECK(!pScan->GetReference()->IsNull());
    --pScan;
    while (!pScan->GetReference()->IsNull()) {
      DCHECK_GE(pScan, table_ + prevState.parts.topIndex);
      --pScan;
    }
    index = pScan - table_;
    segment_state_.parts.numHoles--;
  } else {
    // Add to the end.
    index = topIndex++;
    segment_state_.parts.topIndex = topIndex;
  }
  table_[index].Add(obj);
  result = ToIndirectRef(index);
  if ((false)) {
    LOG(INFO) << "+++ added at " << ExtractIndex(result) << " top=" << segment_state_.parts.topIndex
              << " holes=" << segment_state_.parts.numHoles;
  }

  DCHECK(result != nullptr);
  return result;
}

void IndirectReferenceTable::AssertEmpty() {
  for (size_t i = 0; i < Capacity(); ++i) {
    if (!table_[i].GetReference()->IsNull()) {
      LOG(FATAL) << "Internal Error: non-empty local reference table\n"
                 << MutatorLockedDumpable<IndirectReferenceTable>(*this);
      UNREACHABLE();
    }
  }
}

// Removes an object. We extract the table offset bits from "iref"
// and zap the corresponding entry, leaving a hole if it's not at the top.
// If the entry is not between the current top index and the bottom index
// specified by the cookie, we don't remove anything. This is the behavior
// required by JNI's DeleteLocalRef function.
// This method is not called when a local frame is popped; this is only used
// for explicit single removals.
// Returns "false" if nothing was removed.
bool IndirectReferenceTable::Remove(uint32_t cookie, IndirectRef iref) {
  IRTSegmentState prevState;
  prevState.all = cookie;
  int topIndex = segment_state_.parts.topIndex;
  int bottomIndex = prevState.parts.topIndex;

  DCHECK(table_ != nullptr);
  DCHECK_GE(segment_state_.parts.numHoles, prevState.parts.numHoles);

  if (GetIndirectRefKind(iref) == kHandleScopeOrInvalid) {
    auto* self = Thread::Current();
    if (self->HandleScopeContains(reinterpret_cast<jobject>(iref))) {
      auto* env = self->GetJniEnv();
      DCHECK(env != nullptr);
      if (env->check_jni) {
        ScopedObjectAccess soa(self);
        LOG(WARNING) << "Attempt to remove non-JNI local reference, dumping thread";
        if (kDumpStackOnNonLocalReference) {
          self->Dump(LOG_STREAM(WARNING));
        }
      }
      return true;
    }
  }
  const int idx = ExtractIndex(iref);
  if (idx < bottomIndex) {
    // Wrong segment.
    LOG(WARNING) << "Attempt to remove index outside index area (" << idx
                 << " vs " << bottomIndex << "-" << topIndex << ")";
    return false;
  }
  if (idx >= topIndex) {
    // Bad --- stale reference?
    LOG(WARNING) << "Attempt to remove invalid index " << idx
                 << " (bottom=" << bottomIndex << " top=" << topIndex << ")";
    return false;
  }

  if (idx == topIndex - 1) {
    // Top-most entry.  Scan up and consume holes.

    if (!CheckEntry("remove", iref, idx)) {
      return false;
    }

    *table_[idx].GetReference() = GcRoot<mirror::Object>(nullptr);
    int numHoles = segment_state_.parts.numHoles - prevState.parts.numHoles;
    if (numHoles != 0) {
      while (--topIndex > bottomIndex && numHoles != 0) {
        if ((false)) {
          LOG(INFO) << "+++ checking for hole at " << topIndex - 1
                    << " (cookie=" << cookie << ") val="
                    << table_[topIndex - 1].GetReference()->Read<kWithoutReadBarrier>();
        }
        if (!table_[topIndex - 1].GetReference()->IsNull()) {
          break;
        }
        if ((false)) {
          LOG(INFO) << "+++ ate hole at " << (topIndex - 1);
        }
        numHoles--;
      }
      segment_state_.parts.numHoles = numHoles + prevState.parts.numHoles;
      segment_state_.parts.topIndex = topIndex;
    } else {
      segment_state_.parts.topIndex = topIndex-1;
      if ((false)) {
        LOG(INFO) << "+++ ate last entry " << topIndex - 1;
      }
    }
  } else {
    // Not the top-most entry.  This creates a hole.  We null out the entry to prevent somebody
    // from deleting it twice and screwing up the hole count.
    if (table_[idx].GetReference()->IsNull()) {
      LOG(INFO) << "--- WEIRD: removing null entry " << idx;
      return false;
    }
    if (!CheckEntry("remove", iref, idx)) {
      return false;
    }

    *table_[idx].GetReference() = GcRoot<mirror::Object>(nullptr);
    segment_state_.parts.numHoles++;
    if ((false)) {
      LOG(INFO) << "+++ left hole at " << idx << ", holes=" << segment_state_.parts.numHoles;
    }
  }

  return true;
}

void IndirectReferenceTable::Trim() {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  const size_t top_index = Capacity();
  auto* release_start = AlignUp(reinterpret_cast<uint8_t*>(&table_[top_index]), kPageSize);
  uint8_t* release_end = table_mem_map_->End();
  madvise(release_start, release_end - release_start, MADV_DONTNEED);
}

void IndirectReferenceTable::VisitRoots(RootVisitor* visitor, const RootInfo& root_info) {
  BufferedRootVisitor<kDefaultBufferedRootCount> root_visitor(visitor, root_info);
  for (auto ref : *this) {
    if (!ref->IsNull()) {
      root_visitor.VisitRoot(*ref);
      DCHECK(!ref->IsNull());
    }
  }
}

void IndirectReferenceTable::Dump(std::ostream& os) const {
  os << kind_ << " table dump:\n";
  ReferenceTable::Table entries;
  for (size_t i = 0; i < Capacity(); ++i) {
    ObjPtr<mirror::Object> obj = table_[i].GetReference()->Read<kWithoutReadBarrier>();
    if (obj != nullptr) {
      obj = table_[i].GetReference()->Read();
      entries.push_back(GcRoot<mirror::Object>(obj));
    }
  }
  ReferenceTable::Dump(os, entries);
}

}  // namespace art
