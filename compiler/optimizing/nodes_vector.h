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

#ifndef ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_
#define ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_

// This #include should never be used by compilation, because this header file (nodes_vector.h)
// is included in the header file nodes.h itself. However it gives editing tools better context.
#include "nodes.h"

namespace art {

// Memory alignment, represented as an offset relative to a base, where 0 <= offset < base,
// and base is a power of two. For example, the value Alignment(16, 0) means memory is
// perfectly aligned at a 16-byte boundary, whereas the value Alignment(16, 4) means
// memory is always exactly 4 bytes above such a boundary.
class Alignment {
 public:
  Alignment(size_t base, size_t offset) : base_(base), offset_(offset) {
    DCHECK_LT(offset, base);
    DCHECK(IsPowerOfTwo(base));
  }

  // Returns true if memory is "at least" aligned at the given boundary.
  // Assumes requested base is power of two.
  bool IsAlignedAt(size_t base) const {
    DCHECK_NE(0u, base);
    DCHECK(IsPowerOfTwo(base));
    return ((offset_ | base_) & (base - 1u)) == 0;
  }

  std::string ToString() const {
    return "ALIGN(" + std::to_string(base_) + "," + std::to_string(offset_) + ")";
  }

 private:
  size_t base_;
  size_t offset_;
};

//
// Definitions of abstract vector operations in HIR.
//

// Abstraction of a vector operation, i.e., an operation that performs
// GetVectorLength() x GetPackedType() operations simultaneously.
class HVecOperation : public HVariableInputSizeInstruction {
 public:
  HVecOperation(ArenaAllocator* arena,
                Primitive::Type packed_type,
                SideEffects side_effects,
                size_t number_of_inputs,
                size_t vector_length,
                uint32_t dex_pc)
      : HVariableInputSizeInstruction(side_effects,
                                      dex_pc,
                                      arena,
                                      number_of_inputs,
                                      kArenaAllocVectorNode),
        vector_length_(vector_length) {
    SetPackedField<TypeField>(packed_type);
    DCHECK_LT(1u, vector_length);
  }

  // Returns the number of elements packed in a vector.
  size_t GetVectorLength() const {
    return vector_length_;
  }

  // Returns the number of bytes in a full vector.
  size_t GetVectorNumberOfBytes() const {
    return vector_length_ * Primitive::ComponentSize(GetPackedType());
  }

  // Returns the type of the vector operation: a SIMD operation looks like a FPU location.
  // TODO: we could introduce SIMD types in HIR.
  Primitive::Type GetType() const OVERRIDE {
    return Primitive::kPrimDouble;
  }

  // Returns the true component type packed in a vector.
  Primitive::Type GetPackedType() const {
    return GetPackedField<TypeField>();
  }

  DECLARE_ABSTRACT_INSTRUCTION(VecOperation);

 protected:
  // Additional packed bits.
  static constexpr size_t kFieldType = HInstruction::kNumberOfGenericPackedBits;
  static constexpr size_t kFieldTypeSize =
      MinimumBitsToStore(static_cast<size_t>(Primitive::kPrimLast));
  static constexpr size_t kNumberOfVectorOpPackedBits = kFieldType + kFieldTypeSize;
  static_assert(kNumberOfVectorOpPackedBits <= kMaxNumberOfPackedBits, "Too many packed fields.");
  using TypeField = BitField<Primitive::Type, kFieldType, kFieldTypeSize>;

 private:
  const size_t vector_length_;

  DISALLOW_COPY_AND_ASSIGN(HVecOperation);
};

// Abstraction of a unary vector operation.
class HVecUnaryOperation : public HVecOperation {
 public:
  HVecUnaryOperation(ArenaAllocator* arena,
                     HInstruction* input,
                     Primitive::Type packed_type,
                     size_t vector_length,
                     uint32_t dex_pc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /* number_of_inputs */ 1,
                      vector_length,
                      dex_pc) {
    SetRawInputAt(0, input);
  }

  HInstruction* GetInput() const { return InputAt(0); }

  DECLARE_ABSTRACT_INSTRUCTION(VecUnaryOperation);

 private:
  DISALLOW_COPY_AND_ASSIGN(HVecUnaryOperation);
};

// Abstraction of a binary vector operation.
class HVecBinaryOperation : public HVecOperation {
 public:
  HVecBinaryOperation(ArenaAllocator* arena,
                      HInstruction* left,
                      HInstruction* right,
                      Primitive::Type packed_type,
                      size_t vector_length,
                      uint32_t dex_pc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /* number_of_inputs */ 2,
                      vector_length,
                      dex_pc) {
    SetRawInputAt(0, left);
    SetRawInputAt(1, right);
  }

  HInstruction* GetLeft() const { return InputAt(0); }
  HInstruction* GetRight() const { return InputAt(1); }

  DECLARE_ABSTRACT_INSTRUCTION(VecBinaryOperation);

 private:
  DISALLOW_COPY_AND_ASSIGN(HVecBinaryOperation);
};

// Abstraction of a vector operation that references memory, with an alignment.
// The Android runtime guarantees at least "component size" alignment for array
// elements and, thus, vectors.
class HVecMemoryOperation : public HVecOperation {
 public:
  HVecMemoryOperation(ArenaAllocator* arena,
                      Primitive::Type packed_type,
                      SideEffects side_effects,
                      size_t number_of_inputs,
                      size_t vector_length,
                      uint32_t dex_pc)
      : HVecOperation(arena, packed_type, side_effects, number_of_inputs, vector_length, dex_pc),
        alignment_(Primitive::ComponentSize(packed_type), 0) {
    DCHECK_GE(number_of_inputs, 2u);
  }

  void SetAlignment(Alignment alignment) { alignment_ = alignment; }

  Alignment GetAlignment() const { return alignment_; }

  HInstruction* GetArray() const { return InputAt(0); }
  HInstruction* GetIndex() const { return InputAt(1); }

  DECLARE_ABSTRACT_INSTRUCTION(VecMemoryOperation);

 private:
  Alignment alignment_;

  DISALLOW_COPY_AND_ASSIGN(HVecMemoryOperation);
};

// Packed type consistency checker (same vector length integral types may mix freely).
inline static bool HasConsistentPackedTypes(HInstruction* input, Primitive::Type type) {
  DCHECK(input->IsVecOperation());
  Primitive::Type input_type = input->AsVecOperation()->GetPackedType();
  switch (input_type) {
    case Primitive::kPrimBoolean:
    case Primitive::kPrimByte:
      return type == Primitive::kPrimBoolean ||
             type == Primitive::kPrimByte;
    case Primitive::kPrimChar:
    case Primitive::kPrimShort:
      return type == Primitive::kPrimChar ||
             type == Primitive::kPrimShort;
    default:
      return type == input_type;
  }
}

//
// Definitions of concrete unary vector operations in HIR.
//

// Replicates the given scalar into a vector,
// viz. replicate(x) = [ x, .. , x ].
class HVecReplicateScalar FINAL : public HVecUnaryOperation {
 public:
  HVecReplicateScalar(ArenaAllocator* arena,
                      HInstruction* scalar,
                      Primitive::Type packed_type,
                      size_t vector_length,
                      uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, scalar, packed_type, vector_length, dex_pc) {
    DCHECK(!scalar->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecReplicateScalar);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecReplicateScalar);
};

// Sum-reduces the given vector into a shorter vector (m < n) or scalar (m = 1),
// viz. sum-reduce[ x1, .. , xn ] = [ y1, .., ym ], where yi = sum_j x_j.
class HVecSumReduce FINAL : public HVecUnaryOperation {
  HVecSumReduce(ArenaAllocator* arena,
                HInstruction* input,
                Primitive::Type packed_type,
                size_t vector_length,
                uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, input, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(input, packed_type));
  }

  // TODO: probably integral promotion
  Primitive::Type GetType() const OVERRIDE { return GetPackedType(); }

  DECLARE_INSTRUCTION(VecSumReduce);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSumReduce);
};

// Converts every component in the vector,
// viz. cnv[ x1, .. , xn ]  = [ cnv(x1), .. , cnv(xn) ].
class HVecCnv FINAL : public HVecUnaryOperation {
 public:
  HVecCnv(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, input, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
    DCHECK_NE(GetInputType(), GetResultType());  // actual convert
  }

  Primitive::Type GetInputType() const { return InputAt(0)->AsVecOperation()->GetPackedType(); }
  Primitive::Type GetResultType() const { return GetPackedType(); }

  DECLARE_INSTRUCTION(VecCnv);

 private:
  DISALLOW_COPY_AND_ASSIGN(HVecCnv);
};

// Negates every component in the vector,
// viz. neg[ x1, .. , xn ]  = [ -x1, .. , -xn ].
class HVecNeg FINAL : public HVecUnaryOperation {
 public:
  HVecNeg(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, input, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(input, packed_type));
  }
  DECLARE_INSTRUCTION(VecNeg);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecNeg);
};

// Takes absolute value of every component in the vector,
// viz. abs[ x1, .. , xn ]  = [ |x1|, .. , |xn| ].
class HVecAbs FINAL : public HVecUnaryOperation {
 public:
  HVecAbs(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, input, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(input, packed_type));
  }
  DECLARE_INSTRUCTION(VecAbs);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAbs);
};

// Bitwise- or boolean-nots every component in the vector,
// viz. not[ x1, .. , xn ]  = [ ~x1, .. , ~xn ], or
//      not[ x1, .. , xn ]  = [ !x1, .. , !xn ] for boolean.
class HVecNot FINAL : public HVecUnaryOperation {
 public:
  HVecNot(ArenaAllocator* arena,
          HInstruction* input,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecUnaryOperation(arena, input, packed_type, vector_length, dex_pc) {
    DCHECK(input->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecNot);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecNot);
};

//
// Definitions of concrete binary vector operations in HIR.
//

// Adds every component in the two vectors,
// viz. [ x1, .. , xn ] + [ y1, .. , yn ] = [ x1 + y1, .. , xn + yn ].
class HVecAdd FINAL : public HVecBinaryOperation {
 public:
  HVecAdd(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecAdd);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAdd);
};

// Performs halving add on every component in the two vectors, viz.
// rounded [ x1, .. , xn ] hradd [ y1, .. , yn ] = [ (x1 + y1 + 1) >> 1, .. , (xn + yn + 1) >> 1 ]
// or      [ x1, .. , xn ] hadd  [ y1, .. , yn ] = [ (x1 + y1)     >> 1, .. , (xn + yn )    >> 1 ]
// for signed operands x, y (sign extension) or unsigned operands x, y (zero extension).
class HVecHalvingAdd FINAL : public HVecBinaryOperation {
 public:
  HVecHalvingAdd(ArenaAllocator* arena,
                 HInstruction* left,
                 HInstruction* right,
                 Primitive::Type packed_type,
                 size_t vector_length,
                 bool is_unsigned,
                 bool is_rounded,
                 uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
    SetPackedFlag<kFieldHAddIsUnsigned>(is_unsigned);
    SetPackedFlag<kFieldHAddIsRounded>(is_rounded);
  }

  bool IsUnsigned() const { return GetPackedFlag<kFieldHAddIsUnsigned>(); }
  bool IsRounded() const { return GetPackedFlag<kFieldHAddIsRounded>(); }

  DECLARE_INSTRUCTION(VecHalvingAdd);

 private:
  // Additional packed bits.
  static constexpr size_t kFieldHAddIsUnsigned = HVecOperation::kNumberOfVectorOpPackedBits;
  static constexpr size_t kFieldHAddIsRounded = kFieldHAddIsUnsigned + 1;
  static constexpr size_t kNumberOfHAddPackedBits = kFieldHAddIsRounded + 1;
  static_assert(kNumberOfHAddPackedBits <= kMaxNumberOfPackedBits, "Too many packed fields.");

  DISALLOW_COPY_AND_ASSIGN(HVecHalvingAdd);
};

// Subtracts every component in the two vectors,
// viz. [ x1, .. , xn ] - [ y1, .. , yn ] = [ x1 - y1, .. , xn - yn ].
class HVecSub FINAL : public HVecBinaryOperation {
 public:
  HVecSub(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecSub);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSub);
};

// Multiplies every component in the two vectors,
// viz. [ x1, .. , xn ] * [ y1, .. , yn ] = [ x1 * y1, .. , xn * yn ].
class HVecMul FINAL : public HVecBinaryOperation {
 public:
  HVecMul(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecMul);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMul);
};

// Divides every component in the two vectors,
// viz. [ x1, .. , xn ] / [ y1, .. , yn ] = [ x1 / y1, .. , xn / yn ].
class HVecDiv FINAL : public HVecBinaryOperation {
 public:
  HVecDiv(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecDiv);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecDiv);
};

// Takes minimum of every component in the two vectors,
// viz. MIN( [ x1, .. , xn ] , [ y1, .. , yn ]) = [ min(x1, y1), .. , min(xn, yn) ].
class HVecMin FINAL : public HVecBinaryOperation {
 public:
  HVecMin(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecMin);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMin);
};

// Takes maximum of every component in the two vectors,
// viz. MAX( [ x1, .. , xn ] , [ y1, .. , yn ]) = [ max(x1, y1), .. , max(xn, yn) ].
class HVecMax FINAL : public HVecBinaryOperation {
 public:
  HVecMax(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
    DCHECK(HasConsistentPackedTypes(right, packed_type));
  }
  DECLARE_INSTRUCTION(VecMax);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecMax);
};

// Bitwise-ands every component in the two vectors,
// viz. [ x1, .. , xn ] & [ y1, .. , yn ] = [ x1 & y1, .. , xn & yn ].
class HVecAnd FINAL : public HVecBinaryOperation {
 public:
  HVecAnd(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecAnd);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAnd);
};

// Bitwise-and-nots every component in the two vectors,
// viz. [ x1, .. , xn ] and-not [ y1, .. , yn ] = [ ~x1 & y1, .. , ~xn & yn ].
class HVecAndNot FINAL : public HVecBinaryOperation {
 public:
  HVecAndNot(ArenaAllocator* arena,
             HInstruction* left,
             HInstruction* right,
             Primitive::Type packed_type,
             size_t vector_length,
             uint32_t dex_pc = kNoDexPc)
         : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecAndNot);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecAndNot);
};

// Bitwise-ors every component in the two vectors,
// viz. [ x1, .. , xn ] | [ y1, .. , yn ] = [ x1 | y1, .. , xn | yn ].
class HVecOr FINAL : public HVecBinaryOperation {
 public:
  HVecOr(ArenaAllocator* arena,
         HInstruction* left,
         HInstruction* right,
         Primitive::Type packed_type,
         size_t vector_length,
         uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecOr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecOr);
};

// Bitwise-xors every component in the two vectors,
// viz. [ x1, .. , xn ] ^ [ y1, .. , yn ] = [ x1 ^ y1, .. , xn ^ yn ].
class HVecXor FINAL : public HVecBinaryOperation {
 public:
  HVecXor(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(left->IsVecOperation() && right->IsVecOperation());
  }
  DECLARE_INSTRUCTION(VecXor);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecXor);
};

// Logically shifts every component in the vector left by the given distance,
// viz. [ x1, .. , xn ] << d = [ x1 << d, .. , xn << d ].
class HVecShl FINAL : public HVecBinaryOperation {
 public:
  HVecShl(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
  }
  DECLARE_INSTRUCTION(VecShl);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecShl);
};

// Arithmetically shifts every component in the vector right by the given distance,
// viz. [ x1, .. , xn ] >> d = [ x1 >> d, .. , xn >> d ].
class HVecShr FINAL : public HVecBinaryOperation {
 public:
  HVecShr(ArenaAllocator* arena,
          HInstruction* left,
          HInstruction* right,
          Primitive::Type packed_type,
          size_t vector_length,
          uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
  }
  DECLARE_INSTRUCTION(VecShr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecShr);
};

// Logically shifts every component in the vector right by the given distance,
// viz. [ x1, .. , xn ] >>> d = [ x1 >>> d, .. , xn >>> d ].
class HVecUShr FINAL : public HVecBinaryOperation {
 public:
  HVecUShr(ArenaAllocator* arena,
           HInstruction* left,
           HInstruction* right,
           Primitive::Type packed_type,
           size_t vector_length,
           uint32_t dex_pc = kNoDexPc)
      : HVecBinaryOperation(arena, left, right, packed_type, vector_length, dex_pc) {
    DCHECK(HasConsistentPackedTypes(left, packed_type));
  }
  DECLARE_INSTRUCTION(VecUShr);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecUShr);
};

//
// Definitions of concrete miscellaneous vector operations in HIR.
//

// Assigns the given scalar elements to a vector,
// viz. set( array(x1, .., xn) ) = [ x1, .. , xn ].
class HVecSetScalars FINAL : public HVecOperation {
  HVecSetScalars(ArenaAllocator* arena,
                 HInstruction** scalars,  // array
                 Primitive::Type packed_type,
                 size_t vector_length,
                 uint32_t dex_pc = kNoDexPc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /* number_of_inputs */ vector_length,
                      vector_length,
                      dex_pc) {
    for (size_t i = 0; i < vector_length; i++) {
      DCHECK(!scalars[i]->IsVecOperation());
      SetRawInputAt(0, scalars[i]);
    }
  }
  DECLARE_INSTRUCTION(VecSetScalars);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecSetScalars);
};

// Multiplies every component in the two vectors, adds the result vector to the accumulator vector.
// viz. [ acc1, .., accn ] + [ x1, .. , xn ] * [ y1, .. , yn ] =
//     [ acc1 + x1 * y1, .. , accn + xn * yn ].
class HVecMultiplyAccumulate FINAL : public HVecOperation {
 public:
  HVecMultiplyAccumulate(ArenaAllocator* arena,
                         InstructionKind op,
                         HInstruction* accumulator,
                         HInstruction* mul_left,
                         HInstruction* mul_right,
                         Primitive::Type packed_type,
                         size_t vector_length,
                         uint32_t dex_pc = kNoDexPc)
      : HVecOperation(arena,
                      packed_type,
                      SideEffects::None(),
                      /* number_of_inputs */ 3,
                      vector_length,
                      dex_pc),
        op_kind_(op) {
    DCHECK(op == InstructionKind::kAdd || op == InstructionKind::kSub);
    DCHECK(HasConsistentPackedTypes(accumulator, packed_type));
    DCHECK(HasConsistentPackedTypes(mul_left, packed_type));
    DCHECK(HasConsistentPackedTypes(mul_right, packed_type));
    SetRawInputAt(kInputAccumulatorIndex, accumulator);
    SetRawInputAt(kInputMulLeftIndex, mul_left);
    SetRawInputAt(kInputMulRightIndex, mul_right);
  }

  static constexpr int kInputAccumulatorIndex = 0;
  static constexpr int kInputMulLeftIndex = 1;
  static constexpr int kInputMulRightIndex = 2;

  bool CanBeMoved() const OVERRIDE { return true; }

  bool InstructionDataEquals(const HInstruction* other) const OVERRIDE {
    return op_kind_ == other->AsVecMultiplyAccumulate()->op_kind_;
  }

  InstructionKind GetOpKind() const { return op_kind_; }

  DECLARE_INSTRUCTION(VecMultiplyAccumulate);

 private:
  // Indicates if this is a MADD or MSUB.
  const InstructionKind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(HVecMultiplyAccumulate);
};

// Loads a vector from memory, viz. load(mem, 1)
// yield the vector [ mem(1), .. , mem(n) ].
class HVecLoad FINAL : public HVecMemoryOperation {
 public:
  HVecLoad(ArenaAllocator* arena,
           HInstruction* base,
           HInstruction* index,
           Primitive::Type packed_type,
           size_t vector_length,
           bool is_string_char_at,
           uint32_t dex_pc = kNoDexPc)
      : HVecMemoryOperation(arena,
                            packed_type,
                            SideEffects::ArrayReadOfType(packed_type),
                            /* number_of_inputs */ 2,
                            vector_length,
                            dex_pc) {
    SetRawInputAt(0, base);
    SetRawInputAt(1, index);
    SetPackedFlag<kFieldIsStringCharAt>(is_string_char_at);
  }
  DECLARE_INSTRUCTION(VecLoad);

  bool IsStringCharAt() const { return GetPackedFlag<kFieldIsStringCharAt>(); }

 private:
  // Additional packed bits.
  static constexpr size_t kFieldIsStringCharAt = HVecOperation::kNumberOfVectorOpPackedBits;
  static constexpr size_t kNumberOfVecLoadPackedBits = kFieldIsStringCharAt + 1;
  static_assert(kNumberOfVecLoadPackedBits <= kMaxNumberOfPackedBits, "Too many packed fields.");

  DISALLOW_COPY_AND_ASSIGN(HVecLoad);
};

// Stores a vector to memory, viz. store(m, 1, [x1, .. , xn] )
// sets mem(1) = x1, .. , mem(n) = xn.
class HVecStore FINAL : public HVecMemoryOperation {
 public:
  HVecStore(ArenaAllocator* arena,
            HInstruction* base,
            HInstruction* index,
            HInstruction* value,
            Primitive::Type packed_type,
            size_t vector_length,
            uint32_t dex_pc = kNoDexPc)
      : HVecMemoryOperation(arena,
                            packed_type,
                            SideEffects::ArrayWriteOfType(packed_type),
                            /* number_of_inputs */ 3,
                            vector_length,
                            dex_pc) {
    DCHECK(HasConsistentPackedTypes(value, packed_type));
    SetRawInputAt(0, base);
    SetRawInputAt(1, index);
    SetRawInputAt(2, value);
  }
  DECLARE_INSTRUCTION(VecStore);
 private:
  DISALLOW_COPY_AND_ASSIGN(HVecStore);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_NODES_VECTOR_H_
