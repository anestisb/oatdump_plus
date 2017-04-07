/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_COMPILER_LINKER_ARM_RELATIVE_PATCHER_ARM_BASE_H_
#define ART_COMPILER_LINKER_ARM_RELATIVE_PATCHER_ARM_BASE_H_

#include <deque>
#include <vector>

#include "linker/relative_patcher.h"
#include "method_reference.h"
#include "safe_map.h"

namespace art {
namespace linker {

class ArmBaseRelativePatcher : public RelativePatcher {
 public:
  uint32_t ReserveSpace(uint32_t offset,
                        const CompiledMethod* compiled_method,
                        MethodReference method_ref) OVERRIDE;
  uint32_t ReserveSpaceEnd(uint32_t offset) OVERRIDE;
  uint32_t WriteThunks(OutputStream* out, uint32_t offset) OVERRIDE;

 protected:
  ArmBaseRelativePatcher(RelativePatcherTargetProvider* provider,
                         InstructionSet instruction_set);
  ~ArmBaseRelativePatcher();

  enum class ThunkType {
    kMethodCall,              // Method call thunk.
    kBakerReadBarrierField,   // Baker read barrier, load field or array element at known offset.
    kBakerReadBarrierArray,   // Baker read barrier, array load with index in register.
    kBakerReadBarrierRoot,    // Baker read barrier, GC root load.
  };

  struct BakerReadBarrierFieldParams {
    uint32_t holder_reg;      // Holder object for reading lock word.
    uint32_t base_reg;        // Base register, different from holder for large offset.
                              // If base differs from holder, it should be a pre-defined
                              // register to limit the number of thunks we need to emit.
                              // The offset is retrieved using introspection.
  };

  struct BakerReadBarrierArrayParams {
    uint32_t base_reg;        // Reference to the start of the data.
    uint32_t dummy;           // Dummy field.
                              // The index register is retrieved using introspection
                              // to limit the number of thunks we need to emit.
  };

  struct BakerReadBarrierRootParams {
    uint32_t root_reg;        // The register holding the GC root.
    uint32_t dummy;           // Dummy field.
  };

  struct RawThunkParams {
    uint32_t first;
    uint32_t second;
  };

  union ThunkParams {
    RawThunkParams raw_params;
    BakerReadBarrierFieldParams field_params;
    BakerReadBarrierArrayParams array_params;
    BakerReadBarrierRootParams root_params;
    static_assert(sizeof(raw_params) == sizeof(field_params), "field_params size check");
    static_assert(sizeof(raw_params) == sizeof(array_params), "array_params size check");
    static_assert(sizeof(raw_params) == sizeof(root_params), "root_params size check");
  };

  class ThunkKey {
   public:
    ThunkKey(ThunkType type, ThunkParams params) : type_(type), params_(params) { }

    ThunkType GetType() const {
      return type_;
    }

    BakerReadBarrierFieldParams GetFieldParams() const {
      DCHECK(type_ == ThunkType::kBakerReadBarrierField);
      return params_.field_params;
    }

    BakerReadBarrierArrayParams GetArrayParams() const {
      DCHECK(type_ == ThunkType::kBakerReadBarrierArray);
      return params_.array_params;
    }

    BakerReadBarrierRootParams GetRootParams() const {
      DCHECK(type_ == ThunkType::kBakerReadBarrierRoot);
      return params_.root_params;
    }

    RawThunkParams GetRawParams() const {
      return params_.raw_params;
    }

   private:
    ThunkType type_;
    ThunkParams params_;
  };

  class ThunkKeyCompare {
   public:
    bool operator()(const ThunkKey& lhs, const ThunkKey& rhs) const {
      if (lhs.GetType() != rhs.GetType()) {
        return lhs.GetType() < rhs.GetType();
      }
      if (lhs.GetRawParams().first != rhs.GetRawParams().first) {
        return lhs.GetRawParams().first < rhs.GetRawParams().first;
      }
      return lhs.GetRawParams().second < rhs.GetRawParams().second;
    }
  };

  uint32_t ReserveSpaceInternal(uint32_t offset,
                                const CompiledMethod* compiled_method,
                                MethodReference method_ref,
                                uint32_t max_extra_space);
  uint32_t GetThunkTargetOffset(const ThunkKey& key, uint32_t patch_offset);

  uint32_t CalculateMethodCallDisplacement(uint32_t patch_offset,
                                           uint32_t target_offset);

  virtual ThunkKey GetBakerReadBarrierKey(const LinkerPatch& patch) = 0;
  virtual std::vector<uint8_t> CompileThunk(const ThunkKey& key) = 0;
  virtual uint32_t MaxPositiveDisplacement(ThunkType type) = 0;
  virtual uint32_t MaxNegativeDisplacement(ThunkType type) = 0;

 private:
  class ThunkData;

  void ProcessPatches(const CompiledMethod* compiled_method, uint32_t code_offset);
  void AddUnreservedThunk(ThunkData* data);

  void ResolveMethodCalls(uint32_t quick_code_offset, MethodReference method_ref);

  uint32_t CalculateMaxNextOffset(uint32_t patch_offset, ThunkType type);

  RelativePatcherTargetProvider* const provider_;
  const InstructionSet instruction_set_;

  // The data for all thunks.
  // SafeMap<> nodes don't move after being inserted, so we can use direct pointers to the data.
  using ThunkMap = SafeMap<ThunkKey, ThunkData, ThunkKeyCompare>;
  ThunkMap thunks_;

  // ReserveSpace() tracks unprocessed method call patches. These may be resolved later.
  class UnprocessedMethodCallPatch {
   public:
    UnprocessedMethodCallPatch(uint32_t patch_offset, MethodReference target_method)
        : patch_offset_(patch_offset), target_method_(target_method) { }

    uint32_t GetPatchOffset() const {
      return patch_offset_;
    }

    MethodReference GetTargetMethod() const {
      return target_method_;
    }

   private:
    uint32_t patch_offset_;
    MethodReference target_method_;
  };
  std::deque<UnprocessedMethodCallPatch> unprocessed_method_call_patches_;
  // Once we have compiled a method call thunk, cache pointer to the data.
  ThunkData* method_call_thunk_;

  // Thunks
  std::deque<ThunkData*> unreserved_thunks_;

  class PendingThunkComparator;
  std::vector<ThunkData*> pending_thunks_;  // Heap with the PendingThunkComparator.

  friend class Arm64RelativePatcherTest;
  friend class Thumb2RelativePatcherTest;

  DISALLOW_COPY_AND_ASSIGN(ArmBaseRelativePatcher);
};

}  // namespace linker
}  // namespace art

#endif  // ART_COMPILER_LINKER_ARM_RELATIVE_PATCHER_ARM_BASE_H_
