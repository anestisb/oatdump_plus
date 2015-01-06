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

#include "instruction_set_features.h"

#include "base/casts.h"
#include "utils.h"


#include "arm/instruction_set_features_arm.h"
#include "arm64/instruction_set_features_arm64.h"
#include "mips/instruction_set_features_mips.h"
#include "mips64/instruction_set_features_mips64.h"
#include "x86/instruction_set_features_x86.h"
#include "x86_64/instruction_set_features_x86_64.h"

namespace art {

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromVariant(
    InstructionSet isa, const std::string& variant, std::string* error_msg) {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (isa) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromVariant(variant, error_msg).release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromVariant(variant, error_msg).release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromVariant(variant, error_msg).release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromVariant(variant, error_msg);
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromVariant(variant, error_msg).release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromVariant(variant, error_msg).release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << isa;
      UNREACHABLE();
  }
  CHECK_EQ(result == nullptr, error_msg->size() != 0);
  return result;
}

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromBitmap(InstructionSet isa,
                                                                                 uint32_t bitmap) {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (isa) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromBitmap(bitmap).release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromBitmap(bitmap).release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromBitmap(bitmap).release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromBitmap(bitmap);
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromBitmap(bitmap).release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromBitmap(bitmap).release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << isa;
      UNREACHABLE();
  }
  CHECK_EQ(bitmap, result->AsBitmap());
  return result;
}

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromCppDefines() {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (kRuntimeISA) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromCppDefines().release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromCppDefines().release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromCppDefines().release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromCppDefines();
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromCppDefines().release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromCppDefines().release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << kRuntimeISA;
      UNREACHABLE();
  }
  return result;
}


std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromCpuInfo() {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (kRuntimeISA) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromCpuInfo().release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromCpuInfo().release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromCpuInfo().release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromCpuInfo();
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromCpuInfo().release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromCpuInfo().release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << kRuntimeISA;
      UNREACHABLE();
  }
  return result;
}

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromHwcap() {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (kRuntimeISA) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromHwcap().release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromHwcap().release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromHwcap().release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromHwcap();
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromHwcap().release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromHwcap().release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << kRuntimeISA;
      UNREACHABLE();
  }
  return result;
}

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::FromAssembly() {
  std::unique_ptr<const InstructionSetFeatures> result;
  switch (kRuntimeISA) {
    case kArm:
    case kThumb2:
      result.reset(ArmInstructionSetFeatures::FromAssembly().release());
      break;
    case kArm64:
      result.reset(Arm64InstructionSetFeatures::FromAssembly().release());
      break;
    case kMips:
      result.reset(MipsInstructionSetFeatures::FromAssembly().release());
      break;
    case kMips64:
      result = Mips64InstructionSetFeatures::FromAssembly();
      break;
    case kX86:
      result.reset(X86InstructionSetFeatures::FromAssembly().release());
      break;
    case kX86_64:
      result.reset(X86_64InstructionSetFeatures::FromAssembly().release());
      break;
    default:
      UNIMPLEMENTED(FATAL) << kRuntimeISA;
      UNREACHABLE();
  }
  return result;
}

std::unique_ptr<const InstructionSetFeatures> InstructionSetFeatures::AddFeaturesFromString(
    const std::string& feature_list, std::string* error_msg) const {
  if (feature_list.empty()) {
    *error_msg = "No instruction set features specified";
    return std::unique_ptr<const InstructionSetFeatures>();
  }
  std::vector<std::string> features;
  Split(feature_list, ',', &features);
  bool smp = smp_;
  bool use_default = false;  // Have we seen the 'default' feature?
  bool first = false;  // Is this first feature?
  for (auto it = features.begin(); it != features.end();) {
    if (use_default) {
      *error_msg = "Unexpected instruction set features after 'default'";
      return std::unique_ptr<const InstructionSetFeatures>();
    }
    std::string feature = Trim(*it);
    bool erase = false;
    if (feature == "default") {
      if (!first) {
        use_default = true;
        erase = true;
      } else {
        *error_msg = "Unexpected instruction set features before 'default'";
        return std::unique_ptr<const InstructionSetFeatures>();
      }
    } else if (feature == "smp") {
      smp = true;
      erase = true;
    } else if (feature == "-smp") {
      smp = false;
      erase = true;
    }
    // Erase the smp feature once processed.
    if (!erase) {
      ++it;
    } else {
      it = features.erase(it);
    }
    first = true;
  }
  // Expectation: "default" is standalone, no other flags. But an empty features vector after
  // processing can also come along if the handled flags (at the moment only smp) are the only
  // ones in the list. So logically, we check "default -> features.empty."
  DCHECK(!use_default || features.empty());

  return AddFeaturesFromSplitString(smp, features, error_msg);
}

const ArmInstructionSetFeatures* InstructionSetFeatures::AsArmInstructionSetFeatures() const {
  DCHECK_EQ(kArm, GetInstructionSet());
  return down_cast<const ArmInstructionSetFeatures*>(this);
}

const Arm64InstructionSetFeatures* InstructionSetFeatures::AsArm64InstructionSetFeatures() const {
  DCHECK_EQ(kArm64, GetInstructionSet());
  return down_cast<const Arm64InstructionSetFeatures*>(this);
}

const MipsInstructionSetFeatures* InstructionSetFeatures::AsMipsInstructionSetFeatures() const {
  DCHECK_EQ(kMips, GetInstructionSet());
  return down_cast<const MipsInstructionSetFeatures*>(this);
}

const Mips64InstructionSetFeatures* InstructionSetFeatures::AsMips64InstructionSetFeatures() const {
  DCHECK_EQ(kMips64, GetInstructionSet());
  return down_cast<const Mips64InstructionSetFeatures*>(this);
}

const X86InstructionSetFeatures* InstructionSetFeatures::AsX86InstructionSetFeatures() const {
  DCHECK(kX86 == GetInstructionSet() || kX86_64 == GetInstructionSet());
  return down_cast<const X86InstructionSetFeatures*>(this);
}

const X86_64InstructionSetFeatures* InstructionSetFeatures::AsX86_64InstructionSetFeatures() const {
  DCHECK_EQ(kX86_64, GetInstructionSet());
  return down_cast<const X86_64InstructionSetFeatures*>(this);
}

bool InstructionSetFeatures::FindVariantInArray(const char* const variants[], size_t num_variants,
                                                const std::string& variant) {
  const char* const * begin = variants;
  const char* const * end = begin + num_variants;
  return std::find(begin, end, variant) != end;
}

std::ostream& operator<<(std::ostream& os, const InstructionSetFeatures& rhs) {
  os << "ISA: " << rhs.GetInstructionSet() << " Feature string: " << rhs.GetFeatureString();
  return os;
}

}  // namespace art
