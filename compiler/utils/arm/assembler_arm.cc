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

#include "assembler_arm.h"

#include <algorithm>

#include "base/bit_utils.h"
#include "base/logging.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "offsets.h"
#include "thread.h"

namespace art {
namespace arm {

const char* kRegisterNames[] = {
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
  "fp", "ip", "sp", "lr", "pc"
};

const char* kConditionNames[] = {
  "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT",
  "LE", "AL",
};

std::ostream& operator<<(std::ostream& os, const Register& rhs) {
  if (rhs >= R0 && rhs <= PC) {
    os << kRegisterNames[rhs];
  } else {
    os << "Register[" << static_cast<int>(rhs) << "]";
  }
  return os;
}


std::ostream& operator<<(std::ostream& os, const SRegister& rhs) {
  if (rhs >= S0 && rhs < kNumberOfSRegisters) {
    os << "s" << static_cast<int>(rhs);
  } else {
    os << "SRegister[" << static_cast<int>(rhs) << "]";
  }
  return os;
}


std::ostream& operator<<(std::ostream& os, const DRegister& rhs) {
  if (rhs >= D0 && rhs < kNumberOfDRegisters) {
    os << "d" << static_cast<int>(rhs);
  } else {
    os << "DRegister[" << static_cast<int>(rhs) << "]";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Condition& rhs) {
  if (rhs >= EQ && rhs <= AL) {
    os << kConditionNames[rhs];
  } else {
    os << "Condition[" << static_cast<int>(rhs) << "]";
  }
  return os;
}

ShifterOperand::ShifterOperand(uint32_t immed)
    : type_(kImmediate), rm_(kNoRegister), rs_(kNoRegister),
      is_rotate_(false), is_shift_(false), shift_(kNoShift), rotate_(0), immed_(immed) {
  CHECK(immed < (1u << 12) || ArmAssembler::ModifiedImmediate(immed) != kInvalidModifiedImmediate);
}


uint32_t ShifterOperand::encodingArm() const {
  CHECK(is_valid());
  switch (type_) {
    case kImmediate:
      if (is_rotate_) {
        return (rotate_ << kRotateShift) | (immed_ << kImmed8Shift);
      } else {
        return immed_;
      }
    case kRegister:
      if (is_shift_) {
        uint32_t shift_type;
        switch (shift_) {
          case arm::Shift::ROR:
            shift_type = static_cast<uint32_t>(shift_);
            CHECK_NE(immed_, 0U);
            break;
          case arm::Shift::RRX:
            shift_type = static_cast<uint32_t>(arm::Shift::ROR);  // Same encoding as ROR.
            CHECK_EQ(immed_, 0U);
            break;
          default:
            shift_type = static_cast<uint32_t>(shift_);
        }
        // Shifted immediate or register.
        if (rs_ == kNoRegister) {
          // Immediate shift.
          return immed_ << kShiftImmShift |
                          shift_type << kShiftShift |
                          static_cast<uint32_t>(rm_);
        } else {
          // Register shift.
          return static_cast<uint32_t>(rs_) << kShiftRegisterShift |
              shift_type << kShiftShift | (1 << 4) |
              static_cast<uint32_t>(rm_);
        }
      } else {
        // Simple register
        return static_cast<uint32_t>(rm_);
      }
    default:
      // Can't get here.
      LOG(FATAL) << "Invalid shifter operand for ARM";
      return 0;
  }
}

uint32_t ShifterOperand::encodingThumb() const {
  switch (type_) {
    case kImmediate:
      return immed_;
    case kRegister:
      if (is_shift_) {
        // Shifted immediate or register.
        if (rs_ == kNoRegister) {
          // Immediate shift.
          if (shift_ == RRX) {
            DCHECK_EQ(immed_, 0u);
            // RRX is encoded as an ROR with imm 0.
            return ROR << 4 | static_cast<uint32_t>(rm_);
          } else {
            DCHECK((1 <= immed_ && immed_ <= 31) ||
                   (immed_ == 0u && shift_ == LSL) ||
                   (immed_ == 32u && (shift_ == ASR || shift_ == LSR)));
            uint32_t imm3 = (immed_ >> 2) & 7 /* 0b111*/;
            uint32_t imm2 = immed_ & 3U /* 0b11 */;

            return imm3 << 12 | imm2 << 6 | shift_ << 4 |
                static_cast<uint32_t>(rm_);
          }
        } else {
          LOG(FATAL) << "No register-shifted register instruction available in thumb";
          return 0;
        }
      } else {
        // Simple register
        return static_cast<uint32_t>(rm_);
      }
    default:
      // Can't get here.
      LOG(FATAL) << "Invalid shifter operand for thumb";
      UNREACHABLE();
  }
}

uint32_t Address::encodingArm() const {
  CHECK(IsAbsoluteUint<12>(offset_));
  uint32_t encoding;
  if (is_immed_offset_) {
    if (offset_ < 0) {
      encoding = (am_ ^ (1 << kUShift)) | -offset_;  // Flip U to adjust sign.
    } else {
      encoding =  am_ | offset_;
    }
  } else {
    uint32_t shift = shift_;
    if (shift == RRX) {
      CHECK_EQ(offset_, 0);
      shift = ROR;
    }
    encoding = am_ | static_cast<uint32_t>(rm_) | shift << 5 | offset_ << 7 | B25;
  }
  encoding |= static_cast<uint32_t>(rn_) << kRnShift;
  return encoding;
}


uint32_t Address::encodingThumb(bool is_32bit) const {
  uint32_t encoding = 0;
  if (is_immed_offset_) {
    encoding = static_cast<uint32_t>(rn_) << 16;
    // Check for the T3/T4 encoding.
    // PUW must Offset for T3
    // Convert ARM PU0W to PUW
    // The Mode is in ARM encoding format which is:
    // |P|U|0|W|
    // we need this in thumb2 mode:
    // |P|U|W|

    uint32_t am = am_;
    int32_t offset = offset_;
    if (offset < 0) {
      am ^= 1 << kUShift;
      offset = -offset;
    }
    if (offset_ < 0 || (offset >= 0 && offset < 256 &&
        am_ != Mode::Offset)) {
      // T4 encoding.
      uint32_t PUW = am >> 21;   // Move down to bottom of word.
      PUW = (PUW >> 1) | (PUW & 1);   // Bits 3, 2 and 0.
      // If P is 0 then W must be 1 (Different from ARM).
      if ((PUW & 4U /* 0b100 */) == 0) {
        PUW |= 1U /* 0b1 */;
      }
      encoding |= B11 | PUW << 8 | offset;
    } else {
      // T3 encoding (also sets op1 to 0b01).
      encoding |= B23 | offset_;
    }
  } else {
    // Register offset, possibly shifted.
    // Need to choose between encoding T1 (16 bit) or T2.
    // Only Offset mode is supported.  Shift must be LSL and the count
    // is only 2 bits.
    CHECK_EQ(shift_, LSL);
    CHECK_LE(offset_, 4);
    CHECK_EQ(am_, Offset);
    bool is_t2 = is_32bit;
    if (ArmAssembler::IsHighRegister(rn_) || ArmAssembler::IsHighRegister(rm_)) {
      is_t2 = true;
    } else if (offset_ != 0) {
      is_t2 = true;
    }
    if (is_t2) {
      encoding = static_cast<uint32_t>(rn_) << 16 | static_cast<uint32_t>(rm_) |
          offset_ << 4;
    } else {
      encoding = static_cast<uint32_t>(rn_) << 3 | static_cast<uint32_t>(rm_) << 6;
    }
  }
  return encoding;
}

// This is very like the ARM encoding except the offset is 10 bits.
uint32_t Address::encodingThumbLdrdStrd() const {
  DCHECK(IsImmediate());
  uint32_t encoding;
  uint32_t am = am_;
  // If P is 0 then W must be 1 (Different from ARM).
  uint32_t PU1W = am_ >> 21;   // Move down to bottom of word.
  if ((PU1W & 8U /* 0b1000 */) == 0) {
    am |= 1 << 21;      // Set W bit.
  }
  if (offset_ < 0) {
    int32_t off = -offset_;
    CHECK_LT(off, 1024);
    CHECK_ALIGNED(off, 4);
    encoding = (am ^ (1 << kUShift)) | off >> 2;  // Flip U to adjust sign.
  } else {
    CHECK_LT(offset_, 1024);
    CHECK_ALIGNED(offset_, 4);
    encoding =  am | offset_ >> 2;
  }
  encoding |= static_cast<uint32_t>(rn_) << 16;
  return encoding;
}

// Encoding for ARM addressing mode 3.
uint32_t Address::encoding3() const {
  const uint32_t offset_mask = (1 << 12) - 1;
  uint32_t encoding = encodingArm();
  uint32_t offset = encoding & offset_mask;
  CHECK_LT(offset, 256u);
  return (encoding & ~offset_mask) | ((offset & 0xf0) << 4) | (offset & 0xf);
}

// Encoding for vfp load/store addressing.
uint32_t Address::vencoding() const {
  CHECK(IsAbsoluteUint<10>(offset_));  // In the range -1020 to +1020.
  CHECK_ALIGNED(offset_, 2);  // Multiple of 4.

  const uint32_t offset_mask = (1 << 12) - 1;
  uint32_t encoding = encodingArm();
  uint32_t offset = encoding & offset_mask;
  CHECK((am_ == Offset) || (am_ == NegOffset));
  uint32_t vencoding_value = (encoding & (0xf << kRnShift)) | (offset >> 2);
  if (am_ == Offset) {
    vencoding_value |= 1 << 23;
  }
  return vencoding_value;
}


bool Address::CanHoldLoadOffsetArm(LoadOperandType type, int offset) {
  switch (type) {
    case kLoadSignedByte:
    case kLoadSignedHalfword:
    case kLoadUnsignedHalfword:
    case kLoadWordPair:
      return IsAbsoluteUint<8>(offset);  // Addressing mode 3.
    case kLoadUnsignedByte:
    case kLoadWord:
      return IsAbsoluteUint<12>(offset);  // Addressing mode 2.
    case kLoadSWord:
    case kLoadDWord:
      return IsAbsoluteUint<10>(offset);  // VFP addressing mode.
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}


bool Address::CanHoldStoreOffsetArm(StoreOperandType type, int offset) {
  switch (type) {
    case kStoreHalfword:
    case kStoreWordPair:
      return IsAbsoluteUint<8>(offset);  // Addressing mode 3.
    case kStoreByte:
    case kStoreWord:
      return IsAbsoluteUint<12>(offset);  // Addressing mode 2.
    case kStoreSWord:
    case kStoreDWord:
      return IsAbsoluteUint<10>(offset);  // VFP addressing mode.
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}

bool Address::CanHoldLoadOffsetThumb(LoadOperandType type, int offset) {
  switch (type) {
    case kLoadSignedByte:
    case kLoadSignedHalfword:
    case kLoadUnsignedHalfword:
    case kLoadUnsignedByte:
    case kLoadWord:
      return IsAbsoluteUint<12>(offset);
    case kLoadSWord:
    case kLoadDWord:
      return IsAbsoluteUint<10>(offset) && (offset & 3) == 0;  // VFP addressing mode.
    case kLoadWordPair:
      return IsAbsoluteUint<10>(offset) && (offset & 3) == 0;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}


bool Address::CanHoldStoreOffsetThumb(StoreOperandType type, int offset) {
  switch (type) {
    case kStoreHalfword:
    case kStoreByte:
    case kStoreWord:
      return IsAbsoluteUint<12>(offset);
    case kStoreSWord:
    case kStoreDWord:
      return IsAbsoluteUint<10>(offset) && (offset & 3) == 0;  // VFP addressing mode.
    case kStoreWordPair:
      return IsAbsoluteUint<10>(offset) && (offset & 3) == 0;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}

void ArmAssembler::Pad(uint32_t bytes) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  for (uint32_t i = 0; i < bytes; ++i) {
    buffer_.Emit<uint8_t>(0);
  }
}

static int LeadingZeros(uint32_t val) {
  uint32_t alt;
  int32_t n;
  int32_t count;

  count = 16;
  n = 32;
  do {
    alt = val >> count;
    if (alt != 0) {
      n = n - count;
      val = alt;
    }
    count >>= 1;
  } while (count);
  return n - val;
}


uint32_t ArmAssembler::ModifiedImmediate(uint32_t value) {
  int32_t z_leading;
  int32_t z_trailing;
  uint32_t b0 = value & 0xff;

  /* Note: case of value==0 must use 0:000:0:0000000 encoding */
  if (value <= 0xFF)
    return b0;  // 0:000:a:bcdefgh.
  if (value == ((b0 << 16) | b0))
    return (0x1 << 12) | b0; /* 0:001:a:bcdefgh */
  if (value == ((b0 << 24) | (b0 << 16) | (b0 << 8) | b0))
    return (0x3 << 12) | b0; /* 0:011:a:bcdefgh */
  b0 = (value >> 8) & 0xff;
  if (value == ((b0 << 24) | (b0 << 8)))
    return (0x2 << 12) | b0; /* 0:010:a:bcdefgh */
  /* Can we do it with rotation? */
  z_leading = LeadingZeros(value);
  z_trailing = 32 - LeadingZeros(~value & (value - 1));
  /* A run of eight or fewer active bits? */
  if ((z_leading + z_trailing) < 24)
    return kInvalidModifiedImmediate;  /* No - bail */
  /* left-justify the constant, discarding msb (known to be 1) */
  value <<= z_leading + 1;
  /* Create bcdefgh */
  value >>= 25;

  /* Put it all together */
  uint32_t v = 8 + z_leading;

  uint32_t i = (v & 16U /* 0b10000 */) >> 4;
  uint32_t imm3 = (v >> 1) & 7U /* 0b111 */;
  uint32_t a = v & 1;
  return value | i << 26 | imm3 << 12 | a << 7;
}

void ArmAssembler::FinalizeTrackedLabels() {
  if (!tracked_labels_.empty()) {
    // This array should be sorted, as assembly is generated in linearized order. It isn't
    // technically required, but GetAdjustedPosition() used in AdjustLabelPosition() can take
    // advantage of it. So ensure that it's actually the case.
    DCHECK(std::is_sorted(
        tracked_labels_.begin(),
        tracked_labels_.end(),
        [](const Label* lhs, const Label* rhs) { return lhs->Position() < rhs->Position(); }));

    Label* last_label = nullptr;  // Track duplicates, we must not adjust twice.
    for (Label* label : tracked_labels_) {
      DCHECK_NE(label, last_label);
      AdjustLabelPosition(label);
      last_label = label;
    }
  }
}

}  // namespace arm
}  // namespace art
