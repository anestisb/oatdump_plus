/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "instruction_set_features_mips64.h"

#include <fstream>
#include <sstream>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/logging.h"

namespace art {

using android::base::StringPrintf;

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromVariant(
    const std::string& variant, std::string* error_msg ATTRIBUTE_UNUSED) {
  if (variant != "default" && variant != "mips64r6") {
    LOG(WARNING) << "Unexpected CPU variant for Mips64 using defaults: " << variant;
  }
  return Mips64FeaturesUniquePtr(new Mips64InstructionSetFeatures());
}

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromBitmap(uint32_t bitmap ATTRIBUTE_UNUSED) {
  return Mips64FeaturesUniquePtr(new Mips64InstructionSetFeatures());
}

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromCppDefines() {
  return Mips64FeaturesUniquePtr(new Mips64InstructionSetFeatures());
}

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromCpuInfo() {
  return Mips64FeaturesUniquePtr(new Mips64InstructionSetFeatures());
}

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromHwcap() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Mips64FeaturesUniquePtr Mips64InstructionSetFeatures::FromAssembly() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

bool Mips64InstructionSetFeatures::Equals(const InstructionSetFeatures* other) const {
  if (kMips64 != other->GetInstructionSet()) {
    return false;
  }
  return true;
}

uint32_t Mips64InstructionSetFeatures::AsBitmap() const {
  return 0;
}

std::string Mips64InstructionSetFeatures::GetFeatureString() const {
  return "default";
}

std::unique_ptr<const InstructionSetFeatures>
Mips64InstructionSetFeatures::AddFeaturesFromSplitString(
    const std::vector<std::string>& features, std::string* error_msg) const {
  auto i = features.begin();
  if (i != features.end()) {
    // We don't have any features.
    std::string feature = android::base::Trim(*i);
    *error_msg = StringPrintf("Unknown instruction set feature: '%s'", feature.c_str());
    return nullptr;
  }
  return std::unique_ptr<const InstructionSetFeatures>(new Mips64InstructionSetFeatures());
}

}  // namespace art
