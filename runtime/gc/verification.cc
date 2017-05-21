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

#include "verification.h"

#include <iomanip>
#include <sstream>

#include "art_field-inl.h"
#include "mirror/class-inl.h"

namespace art {
namespace gc {

std::string Verification::DumpObjectInfo(const void* addr, const char* tag) const {
  std::ostringstream oss;
  oss << tag << "=" << addr;
  if (IsValidHeapObjectAddress(addr)) {
    mirror::Object* obj = reinterpret_cast<mirror::Object*>(const_cast<void*>(addr));
    mirror::Class* klass = obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
    oss << " klass=" << klass;
    if (IsValidClass(klass)) {
      oss << "(" << klass->PrettyClass() << ")";
      if (klass->IsArrayClass<kVerifyNone, kWithoutReadBarrier>()) {
        oss << " length=" << obj->AsArray<kVerifyNone, kWithoutReadBarrier>()->GetLength();
      }
    } else {
      oss << " <invalid address>";
    }
    space::Space* const space = heap_->FindSpaceFromAddress(addr);
    if (space != nullptr) {
      oss << " space=" << *space;
    }
    accounting::CardTable* card_table = heap_->GetCardTable();
    if (card_table->AddrIsInCardTable(addr)) {
      oss << " card=" << static_cast<size_t>(
          card_table->GetCard(reinterpret_cast<const mirror::Object*>(addr)));
    }
    // Dump adjacent RAM.
    const uintptr_t uint_addr = reinterpret_cast<uintptr_t>(addr);
    static constexpr size_t kBytesBeforeAfter = 2 * kObjectAlignment;
    const uintptr_t dump_start = uint_addr - kBytesBeforeAfter;
    const uintptr_t dump_end = uint_addr + kBytesBeforeAfter;
    if (dump_start < dump_end &&
        IsValidHeapObjectAddress(reinterpret_cast<const void*>(dump_start)) &&
        IsValidHeapObjectAddress(reinterpret_cast<const void*>(dump_end - kObjectAlignment))) {
      oss << " adjacent_ram=";
      for (uintptr_t p = dump_start; p < dump_end; ++p) {
        if (p == uint_addr) {
          // Marker of where the object is.
          oss << "|";
        }
        uint8_t* ptr = reinterpret_cast<uint8_t*>(p);
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<uintptr_t>(*ptr);
      }
    }
  } else {
    oss << " <invalid address>";
  }
  return oss.str();
}

void Verification::LogHeapCorruption(ObjPtr<mirror::Object> holder,
                                     MemberOffset offset,
                                     mirror::Object* ref,
                                     bool fatal) const {
  // Lowest priority logging first:
  PrintFileToLog("/proc/self/maps", LogSeverity::FATAL_WITHOUT_ABORT);
  MemMap::DumpMaps(LOG_STREAM(FATAL_WITHOUT_ABORT), true);
  // Buffer the output in the string stream since it is more important than the stack traces
  // and we want it to have log priority. The stack traces are printed from Runtime::Abort
  // which is called from LOG(FATAL) but before the abort message.
  std::ostringstream oss;
  oss << "GC tried to mark invalid reference " << ref << std::endl;
  oss << DumpObjectInfo(ref, "ref") << "\n";
  oss << DumpObjectInfo(holder.Ptr(), "holder");
  if (holder != nullptr) {
    mirror::Class* holder_klass = holder->GetClass<kVerifyNone, kWithoutReadBarrier>();
    if (IsValidClass(holder_klass)) {
      oss << "field_offset=" << offset.Uint32Value();
      ArtField* field = holder->FindFieldByOffset(offset);
      if (field != nullptr) {
        oss << " name=" << field->GetName();
      }
    }
  }

  if (fatal) {
    LOG(FATAL) << oss.str();
  } else {
    LOG(FATAL_WITHOUT_ABORT) << oss.str();
  }
}

bool Verification::IsValidHeapObjectAddress(const void* addr, space::Space** out_space) const {
  if (!IsAligned<kObjectAlignment>(addr)) {
    return false;
  }
  space::Space* const space = heap_->FindSpaceFromAddress(addr);
  if (space != nullptr) {
    if (out_space != nullptr) {
      *out_space = space;
    }
    return true;
  }
  return false;
}

bool Verification::IsValidClass(const void* addr) const {
  if (!IsValidHeapObjectAddress(addr)) {
    return false;
  }
  mirror::Class* klass = reinterpret_cast<mirror::Class*>(const_cast<void*>(addr));
  mirror::Class* k1 = klass->GetClass<kVerifyNone, kWithoutReadBarrier>();
  if (!IsValidHeapObjectAddress(k1)) {
    return false;
  }
  // k should be class class, take the class again to verify.
  // Note that this check may not be valid for the no image space since the class class might move
  // around from moving GC.
  mirror::Class* k2 = k1->GetClass<kVerifyNone, kWithoutReadBarrier>();
  if (!IsValidHeapObjectAddress(k2)) {
    return false;
  }
  return k1 == k2;
}

}  // namespace gc
}  // namespace art
