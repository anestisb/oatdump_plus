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

#include "instruction_set_features_arm.h"

#if defined(ART_TARGET_ANDROID) && defined(__arm__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

#include "signal.h"
#include <fstream>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"

#include "base/logging.h"

#if defined(__arm__)
extern "C" bool artCheckForArmSdivInstruction();
#endif

namespace art {

using android::base::StringPrintf;

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromVariant(
    const std::string& variant, std::string* error_msg) {
  // Look for variants that have divide support.
  static const char* arm_variants_with_div[] = {
      "cortex-a7",
      "cortex-a12",
      "cortex-a15",
      "cortex-a17",
      "cortex-a53",
      "cortex-a53.a57",
      "cortex-a57",
      "denver",
      "krait",
  };

  bool has_div = FindVariantInArray(arm_variants_with_div,
                                    arraysize(arm_variants_with_div),
                                    variant);

  // Look for variants that have LPAE support.
  static const char* arm_variants_with_lpae[] = {
      "cortex-a7",
      "cortex-a12",
      "cortex-a15",
      "cortex-a17",
      "cortex-a53",
      "cortex-a53.a57",
      "cortex-a57",
      "denver",
      "krait",
  };
  bool has_lpae = FindVariantInArray(arm_variants_with_lpae,
                                     arraysize(arm_variants_with_lpae),
                                     variant);

  if (has_div == false && has_lpae == false) {
    static const char* arm_variants_with_default_features[] = {
        "cortex-a5",
        "cortex-a8",
        "cortex-a9",
        "cortex-a9-mp",
        "default",
        "generic"
    };
    if (!FindVariantInArray(arm_variants_with_default_features,
                            arraysize(arm_variants_with_default_features),
                            variant)) {
      *error_msg = StringPrintf("Attempt to use unsupported ARM variant: %s", variant.c_str());
      return nullptr;
    } else {
      // Warn if we use the default features.
      LOG(WARNING) << "Using default instruction set features for ARM CPU variant (" << variant
          << ") using conservative defaults";
    }
  }
  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_lpae));
}

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromBitmap(uint32_t bitmap) {
  bool has_div = (bitmap & kDivBitfield) != 0;
  bool has_atomic_ldrd_strd = (bitmap & kAtomicLdrdStrdBitfield) != 0;
  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_atomic_ldrd_strd));
}

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromCppDefines() {
#if defined(__ARM_ARCH_EXT_IDIV__)
  const bool has_div = true;
#else
  const bool has_div = false;
#endif
#if defined(__ARM_FEATURE_LPAE)
  const bool has_lpae = true;
#else
  const bool has_lpae = false;
#endif
  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_lpae));
}

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromCpuInfo() {
  // Look in /proc/cpuinfo for features we need.  Only use this when we can guarantee that
  // the kernel puts the appropriate feature flags in here.  Sometimes it doesn't.
  bool has_lpae = false;
  bool has_div = false;

  std::ifstream in("/proc/cpuinfo");
  if (!in.fail()) {
    while (!in.eof()) {
      std::string line;
      std::getline(in, line);
      if (!in.eof()) {
        LOG(INFO) << "cpuinfo line: " << line;
        if (line.find("Features") != std::string::npos) {
          LOG(INFO) << "found features";
          if (line.find("idivt") != std::string::npos) {
            // We always expect both ARM and Thumb divide instructions to be available or not
            // available.
            CHECK_NE(line.find("idiva"), std::string::npos);
            has_div = true;
          }
          if (line.find("lpae") != std::string::npos) {
            has_lpae = true;
          }
        }
      }
    }
    in.close();
  } else {
    LOG(ERROR) << "Failed to open /proc/cpuinfo";
  }
  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_lpae));
}

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromHwcap() {
  bool has_div = false;
  bool has_lpae = false;

#if defined(ART_TARGET_ANDROID) && defined(__arm__)
  uint64_t hwcaps = getauxval(AT_HWCAP);
  LOG(INFO) << "hwcaps=" << hwcaps;
  if ((hwcaps & HWCAP_IDIVT) != 0) {
    // We always expect both ARM and Thumb divide instructions to be available or not
    // available.
    CHECK_NE(hwcaps & HWCAP_IDIVA, 0U);
    has_div = true;
  }
  if ((hwcaps & HWCAP_LPAE) != 0) {
    has_lpae = true;
  }
#endif

  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_lpae));
}

// A signal handler called by a fault for an illegal instruction.  We record the fact in r0
// and then increment the PC in the signal context to return to the next instruction.  We know the
// instruction is an sdiv (4 bytes long).
static void bad_divide_inst_handle(int signo ATTRIBUTE_UNUSED, siginfo_t* si ATTRIBUTE_UNUSED,
                                   void* data) {
#if defined(__arm__)
  struct ucontext *uc = (struct ucontext *)data;
  struct sigcontext *sc = &uc->uc_mcontext;
  sc->arm_r0 = 0;     // Set R0 to #0 to signal error.
  sc->arm_pc += 4;    // Skip offending instruction.
#else
  UNUSED(data);
#endif
}

ArmFeaturesUniquePtr ArmInstructionSetFeatures::FromAssembly() {
  // See if have a sdiv instruction.  Register a signal handler and try to execute an sdiv
  // instruction.  If we get a SIGILL then it's not supported.
  struct sigaction sa, osa;
  sa.sa_flags = SA_ONSTACK | SA_RESTART | SA_SIGINFO;
  sa.sa_sigaction = bad_divide_inst_handle;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGILL, &sa, &osa);

  bool has_div = false;
#if defined(__arm__)
  if (artCheckForArmSdivInstruction()) {
    has_div = true;
  }
#endif

  // Restore the signal handler.
  sigaction(SIGILL, &osa, nullptr);

  // Use compile time features to "detect" LPAE support.
  // TODO: write an assembly LPAE support test.
#if defined(__ARM_FEATURE_LPAE)
  const bool has_lpae = true;
#else
  const bool has_lpae = false;
#endif
  return ArmFeaturesUniquePtr(new ArmInstructionSetFeatures(has_div, has_lpae));
}

bool ArmInstructionSetFeatures::Equals(const InstructionSetFeatures* other) const {
  if (kArm != other->GetInstructionSet()) {
    return false;
  }
  const ArmInstructionSetFeatures* other_as_arm = other->AsArmInstructionSetFeatures();
  return has_div_ == other_as_arm->has_div_ &&
      has_atomic_ldrd_strd_ == other_as_arm->has_atomic_ldrd_strd_;
}

uint32_t ArmInstructionSetFeatures::AsBitmap() const {
  return (has_div_ ? kDivBitfield : 0) |
      (has_atomic_ldrd_strd_ ? kAtomicLdrdStrdBitfield : 0);
}

std::string ArmInstructionSetFeatures::GetFeatureString() const {
  std::string result;
  if (has_div_) {
    result += "div";
  } else {
    result += "-div";
  }
  if (has_atomic_ldrd_strd_) {
    result += ",atomic_ldrd_strd";
  } else {
    result += ",-atomic_ldrd_strd";
  }
  return result;
}

std::unique_ptr<const InstructionSetFeatures>
ArmInstructionSetFeatures::AddFeaturesFromSplitString(
    const std::vector<std::string>& features, std::string* error_msg) const {
  bool has_atomic_ldrd_strd = has_atomic_ldrd_strd_;
  bool has_div = has_div_;
  for (auto i = features.begin(); i != features.end(); i++) {
    std::string feature = android::base::Trim(*i);
    if (feature == "div") {
      has_div = true;
    } else if (feature == "-div") {
      has_div = false;
    } else if (feature == "atomic_ldrd_strd") {
      has_atomic_ldrd_strd = true;
    } else if (feature == "-atomic_ldrd_strd") {
      has_atomic_ldrd_strd = false;
    } else {
      *error_msg = StringPrintf("Unknown instruction set feature: '%s'", feature.c_str());
      return nullptr;
    }
  }
  return std::unique_ptr<const InstructionSetFeatures>(
      new ArmInstructionSetFeatures(has_div, has_atomic_ldrd_strd));
}

}  // namespace art
