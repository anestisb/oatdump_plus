/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_COMMON_THROWS_H_
#define ART_RUNTIME_COMMON_THROWS_H_

#include "base/mutex.h"
#include "invoke_type.h"

namespace art {
namespace mirror {
  class Class;
  class Object;
}  // namespace mirror
class ArtField;
class ArtMethod;
class DexFile;
class Signature;
class StringPiece;

// AbstractMethodError

void ThrowAbstractMethodError(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowAbstractMethodError(uint32_t method_idx, const DexFile& dex_file)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ArithmeticException

void ThrowArithmeticExceptionDivideByZero() REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ArrayIndexOutOfBoundsException

void ThrowArrayIndexOutOfBoundsException(int index, int length)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ArrayStoreException

void ThrowArrayStoreException(mirror::Class* element_class, mirror::Class* array_class)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ClassCircularityError

void ThrowClassCircularityError(mirror::Class* c)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowClassCircularityError(mirror::Class* c, const char* fmt, ...)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ClassCastException

void ThrowClassCastException(mirror::Class* dest_type, mirror::Class* src_type)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowClassCastException(const char* msg)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// ClassFormatError

void ThrowClassFormatError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// IllegalAccessError

void ThrowIllegalAccessErrorClass(mirror::Class* referrer, mirror::Class* accessed)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorClassForMethodDispatch(mirror::Class* referrer, mirror::Class* accessed,
                                                   ArtMethod* called,
                                                   InvokeType type)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorMethod(mirror::Class* referrer, ArtMethod* accessed)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorField(mirror::Class* referrer, ArtField* accessed)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorFinalField(ArtMethod* referrer, ArtField* accessed)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// IllegalAccessException

void ThrowIllegalAccessException(const char* msg)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// IllegalArgumentException

void ThrowIllegalArgumentException(const char* msg)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// IncompatibleClassChangeError

void ThrowIncompatibleClassChangeError(InvokeType expected_type, InvokeType found_type,
                                       ArtMethod* method, ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorClassForInterfaceSuper(ArtMethod* method,
                                                             mirror::Class* target_class,
                                                             mirror::Object* this_object,
                                                             ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorClassForInterfaceDispatch(ArtMethod* interface_method,
                                                                mirror::Object* this_object,
                                                                ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorField(ArtField* resolved_field, bool is_static,
                                            ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorForMethodConflict(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// IOException

void ThrowIOException(const char* fmt, ...) __attribute__((__format__(__printf__, 1, 2)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowWrappedIOException(const char* fmt, ...) __attribute__((__format__(__printf__, 1, 2)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// LinkageError

void ThrowLinkageError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowWrappedLinkageError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// NegativeArraySizeException

void ThrowNegativeArraySizeException(int size)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNegativeArraySizeException(const char* msg)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;


// NoSuchFieldError

void ThrowNoSuchFieldError(const StringPiece& scope, mirror::Class* c,
                           const StringPiece& type, const StringPiece& name)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNoSuchFieldException(mirror::Class* c, const StringPiece& name)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// NoSuchMethodError

void ThrowNoSuchMethodError(InvokeType type, mirror::Class* c, const StringPiece& name,
                            const Signature& signature)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNoSuchMethodError(uint32_t method_idx)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// NullPointerException

void ThrowNullPointerExceptionForFieldAccess(ArtField* field,
                                             bool is_read)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionForMethodAccess(uint32_t method_idx,
                                              InvokeType type)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionForMethodAccess(ArtMethod* method,
                                              InvokeType type)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionFromDexPC(bool check_address = false, uintptr_t addr = 0)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerException(const char* msg)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// RuntimeException

void ThrowRuntimeException(const char* fmt, ...)
    __attribute__((__format__(__printf__, 1, 2)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// Stack overflow.

void ThrowStackOverflowError(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// StringIndexOutOfBoundsException

void ThrowStringIndexOutOfBoundsException(int index, int length)
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

// VerifyError

void ThrowVerifyError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_) COLD_ATTR;

}  // namespace art

#endif  // ART_RUNTIME_COMMON_THROWS_H_
