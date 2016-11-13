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

#include "base/arena_allocator.h"
#include "code_generator_mips.h"
#include "optimizing_unit_test.h"
#include "parallel_move_resolver.h"
#include "utils/assembler_test_base.h"
#include "utils/mips/assembler_mips.h"

#include "gtest/gtest.h"

namespace art {

class EmitSwapMipsTest : public ::testing::Test {
 public:
  void SetUp() OVERRIDE {
    allocator_.reset(new ArenaAllocator(&pool_));
    graph_ = CreateGraph(allocator_.get());
    isa_features_ = MipsInstructionSetFeatures::FromCppDefines();
    codegen_ = new (graph_->GetArena()) mips::CodeGeneratorMIPS(graph_,
                                                                *isa_features_.get(),
                                                                CompilerOptions());
    moves_ = new (allocator_.get()) HParallelMove(allocator_.get());
    test_helper_.reset(
        new AssemblerTestInfrastructure(GetArchitectureString(),
                                        GetAssemblerCmdName(),
                                        GetAssemblerParameters(),
                                        GetObjdumpCmdName(),
                                        GetObjdumpParameters(),
                                        GetDisassembleCmdName(),
                                        GetDisassembleParameters(),
                                        GetAssemblyHeader()));
  }

  void TearDown() OVERRIDE {
    allocator_.reset();
    test_helper_.reset();
  }

  // Get the typically used name for this architecture.
  std::string GetArchitectureString() {
    return "mips";
  }

  // Get the name of the assembler.
  std::string GetAssemblerCmdName() {
    return "as";
  }

  // Switches to the assembler command.
  std::string GetAssemblerParameters() {
    return " --no-warn -32 -march=mips32r2";
  }

  // Get the name of the objdump.
  std::string GetObjdumpCmdName() {
    return "objdump";
  }

  // Switches to the objdump command.
  std::string GetObjdumpParameters() {
    return " -h";
  }

  // Get the name of the objdump.
  std::string GetDisassembleCmdName() {
    return "objdump";
  }

  // Switches to the objdump command.
  std::string GetDisassembleParameters() {
    return " -D -bbinary -mmips:isa32r2";
  }

  // No need for assembly header here.
  const char* GetAssemblyHeader() {
    return nullptr;
  }

  void DriverWrapper(HParallelMove* move, std::string assembly_text, std::string test_name) {
    codegen_->GetMoveResolver()->EmitNativeCode(move);
    assembler_ = codegen_->GetAssembler();
    assembler_->FinalizeCode();
    std::unique_ptr<std::vector<uint8_t>> data(new std::vector<uint8_t>(assembler_->CodeSize()));
    MemoryRegion code(&(*data)[0], data->size());
    assembler_->FinalizeInstructions(code);
    test_helper_->Driver(*data, assembly_text, test_name);
  }

 protected:
  ArenaPool pool_;
  HGraph* graph_;
  HParallelMove* moves_;
  mips::CodeGeneratorMIPS* codegen_;
  mips::MipsAssembler* assembler_;
  std::unique_ptr<ArenaAllocator> allocator_;
  std::unique_ptr<AssemblerTestInfrastructure> test_helper_;
  std::unique_ptr<const MipsInstructionSetFeatures> isa_features_;
};

TEST_F(EmitSwapMipsTest, TwoRegisters) {
  moves_->AddMove(
      Location::RegisterLocation(4),
      Location::RegisterLocation(5),
      Primitive::kPrimInt,
      nullptr);
  moves_->AddMove(
      Location::RegisterLocation(5),
      Location::RegisterLocation(4),
      Primitive::kPrimInt,
      nullptr);
  const char* expected =
      "or $t8, $a1, $zero\n"
      "or $a1, $a0, $zero\n"
      "or $a0, $t8, $zero\n";
  DriverWrapper(moves_, expected, "TwoRegisters");
}

TEST_F(EmitSwapMipsTest, TwoRegisterPairs) {
  moves_->AddMove(
      Location::RegisterPairLocation(4, 5),
      Location::RegisterPairLocation(6, 7),
      Primitive::kPrimLong,
      nullptr);
  moves_->AddMove(
      Location::RegisterPairLocation(6, 7),
      Location::RegisterPairLocation(4, 5),
      Primitive::kPrimLong,
      nullptr);
  const char* expected =
      "or $t8, $a2, $zero\n"
      "or $a2, $a0, $zero\n"
      "or $a0, $t8, $zero\n"
      "or $t8, $a3, $zero\n"
      "or $a3, $a1, $zero\n"
      "or $a1, $t8, $zero\n";
  DriverWrapper(moves_, expected, "TwoRegisterPairs");
}

TEST_F(EmitSwapMipsTest, TwoFpuRegistersFloat) {
  moves_->AddMove(
      Location::FpuRegisterLocation(4),
      Location::FpuRegisterLocation(2),
      Primitive::kPrimFloat,
      nullptr);
  moves_->AddMove(
      Location::FpuRegisterLocation(2),
      Location::FpuRegisterLocation(4),
      Primitive::kPrimFloat,
      nullptr);
  const char* expected =
      "mov.s $f6, $f2\n"
      "mov.s $f2, $f4\n"
      "mov.s $f4, $f6\n";
  DriverWrapper(moves_, expected, "TwoFpuRegistersFloat");
}

TEST_F(EmitSwapMipsTest, TwoFpuRegistersDouble) {
  moves_->AddMove(
      Location::FpuRegisterLocation(4),
      Location::FpuRegisterLocation(2),
      Primitive::kPrimDouble,
      nullptr);
  moves_->AddMove(
      Location::FpuRegisterLocation(2),
      Location::FpuRegisterLocation(4),
      Primitive::kPrimDouble,
      nullptr);
  const char* expected =
      "mov.d $f6, $f2\n"
      "mov.d $f2, $f4\n"
      "mov.d $f4, $f6\n";
  DriverWrapper(moves_, expected, "TwoFpuRegistersDouble");
}

TEST_F(EmitSwapMipsTest, RegisterAndFpuRegister) {
  moves_->AddMove(
      Location::RegisterLocation(4),
      Location::FpuRegisterLocation(2),
      Primitive::kPrimFloat,
      nullptr);
  moves_->AddMove(
      Location::FpuRegisterLocation(2),
      Location::RegisterLocation(4),
      Primitive::kPrimFloat,
      nullptr);
  const char* expected =
      "or $t8, $a0, $zero\n"
      "mfc1 $a0, $f2\n"
      "mtc1 $t8, $f2\n";
  DriverWrapper(moves_, expected, "RegisterAndFpuRegister");
}

TEST_F(EmitSwapMipsTest, RegisterPairAndFpuRegister) {
  moves_->AddMove(
      Location::RegisterPairLocation(4, 5),
      Location::FpuRegisterLocation(4),
      Primitive::kPrimDouble,
      nullptr);
  moves_->AddMove(
      Location::FpuRegisterLocation(4),
      Location::RegisterPairLocation(4, 5),
      Primitive::kPrimDouble,
      nullptr);
  const char* expected =
      "mfc1 $t8, $f4\n"
      "mfc1 $at, $f5\n"
      "mtc1 $a0, $f4\n"
      "mtc1 $a1, $f5\n"
      "or $a0, $t8, $zero\n"
      "or $a1, $at, $zero\n";
  DriverWrapper(moves_, expected, "RegisterPairAndFpuRegister");
}

TEST_F(EmitSwapMipsTest, TwoStackSlots) {
  moves_->AddMove(
      Location::StackSlot(52),
      Location::StackSlot(48),
      Primitive::kPrimInt,
      nullptr);
  moves_->AddMove(
      Location::StackSlot(48),
      Location::StackSlot(52),
      Primitive::kPrimInt,
      nullptr);
  const char* expected =
      "addiu $sp, $sp, -4\n"
      "sw $v0, 0($sp)\n"
      "lw $v0, 56($sp)\n"
      "lw $t8, 52($sp)\n"
      "sw $v0, 52($sp)\n"
      "sw $t8, 56($sp)\n"
      "lw $v0, 0($sp)\n"
      "addiu $sp, $sp, 4\n";
  DriverWrapper(moves_, expected, "TwoStackSlots");
}

TEST_F(EmitSwapMipsTest, TwoDoubleStackSlots) {
  moves_->AddMove(
      Location::DoubleStackSlot(56),
      Location::DoubleStackSlot(48),
      Primitive::kPrimLong,
      nullptr);
  moves_->AddMove(
      Location::DoubleStackSlot(48),
      Location::DoubleStackSlot(56),
      Primitive::kPrimLong,
      nullptr);
  const char* expected =
      "addiu $sp, $sp, -4\n"
      "sw $v0, 0($sp)\n"
      "lw $v0, 60($sp)\n"
      "lw $t8, 52($sp)\n"
      "sw $v0, 52($sp)\n"
      "sw $t8, 60($sp)\n"
      "lw $v0, 64($sp)\n"
      "lw $t8, 56($sp)\n"
      "sw $v0, 56($sp)\n"
      "sw $t8, 64($sp)\n"
      "lw $v0, 0($sp)\n"
      "addiu $sp, $sp, 4\n";
  DriverWrapper(moves_, expected, "TwoDoubleStackSlots");
}

TEST_F(EmitSwapMipsTest, RegisterAndStackSlot) {
  moves_->AddMove(
      Location::RegisterLocation(4),
      Location::StackSlot(48),
      Primitive::kPrimInt,
      nullptr);
  moves_->AddMove(
      Location::StackSlot(48),
      Location::RegisterLocation(4),
      Primitive::kPrimInt,
      nullptr);
  const char* expected =
      "or $t8, $a0, $zero\n"
      "lw $a0, 48($sp)\n"
      "sw $t8, 48($sp)\n";
  DriverWrapper(moves_, expected, "RegisterAndStackSlot");
}

TEST_F(EmitSwapMipsTest, RegisterPairAndDoubleStackSlot) {
  moves_->AddMove(
      Location::RegisterPairLocation(4, 5),
      Location::DoubleStackSlot(32),
      Primitive::kPrimLong,
      nullptr);
  moves_->AddMove(
      Location::DoubleStackSlot(32),
      Location::RegisterPairLocation(4, 5),
      Primitive::kPrimLong,
      nullptr);
  const char* expected =
      "or $t8, $a0, $zero\n"
      "lw $a0, 32($sp)\n"
      "sw $t8, 32($sp)\n"
      "or $t8, $a1, $zero\n"
      "lw $a1, 36($sp)\n"
      "sw $t8, 36($sp)\n";
  DriverWrapper(moves_, expected, "RegisterPairAndDoubleStackSlot");
}

TEST_F(EmitSwapMipsTest, FpuRegisterAndStackSlot) {
  moves_->AddMove(
      Location::FpuRegisterLocation(4),
      Location::StackSlot(48),
      Primitive::kPrimFloat,
      nullptr);
  moves_->AddMove(
      Location::StackSlot(48),
      Location::FpuRegisterLocation(4),
      Primitive::kPrimFloat,
      nullptr);
  const char* expected =
      "mov.s $f6, $f4\n"
      "lwc1 $f4, 48($sp)\n"
      "swc1 $f6, 48($sp)\n";
  DriverWrapper(moves_, expected, "FpuRegisterAndStackSlot");
}

TEST_F(EmitSwapMipsTest, FpuRegisterAndDoubleStackSlot) {
  moves_->AddMove(
      Location::FpuRegisterLocation(4),
      Location::DoubleStackSlot(48),
      Primitive::kPrimDouble,
      nullptr);
  moves_->AddMove(
      Location::DoubleStackSlot(48),
      Location::FpuRegisterLocation(4),
      Primitive::kPrimDouble,
      nullptr);
  const char* expected =
      "mov.d $f6, $f4\n"
      "ldc1 $f4, 48($sp)\n"
      "sdc1 $f6, 48($sp)\n";
  DriverWrapper(moves_, expected, "FpuRegisterAndDoubleStackSlot");
}

}  // namespace art
