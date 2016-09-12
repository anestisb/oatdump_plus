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

#include "asm_support_arm64.S"

#include "arch/quick_alloc_entrypoints.S"


.macro INCREASE_FRAME frame_adjustment
    sub sp, sp, #(\frame_adjustment)
    .cfi_adjust_cfa_offset (\frame_adjustment)
.endm

.macro DECREASE_FRAME frame_adjustment
    add sp, sp, #(\frame_adjustment)
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm

.macro SAVE_REG reg, offset
    str \reg, [sp, #(\offset)]
    .cfi_rel_offset \reg, (\offset)
.endm

.macro RESTORE_REG reg, offset
    ldr \reg, [sp, #(\offset)]
    .cfi_restore \reg
.endm

.macro SAVE_TWO_REGS reg1, reg2, offset
    stp \reg1, \reg2, [sp, #(\offset)]
    .cfi_rel_offset \reg1, (\offset)
    .cfi_rel_offset \reg2, (\offset) + 8
.endm

.macro RESTORE_TWO_REGS reg1, reg2, offset
    ldp \reg1, \reg2, [sp, #(\offset)]
    .cfi_restore \reg1
    .cfi_restore \reg2
.endm

.macro SAVE_TWO_REGS_INCREASE_FRAME reg1, reg2, frame_adjustment
    stp \reg1, \reg2, [sp, #-(\frame_adjustment)]!
    .cfi_adjust_cfa_offset (\frame_adjustment)
    .cfi_rel_offset \reg1, 0
    .cfi_rel_offset \reg2, 8
.endm

.macro RESTORE_TWO_REGS_DECREASE_FRAME reg1, reg2, frame_adjustment
    ldp \reg1, \reg2, [sp], #(\frame_adjustment)
    .cfi_restore \reg1
    .cfi_restore \reg2
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    // art::Runtime** xIP0 = &art::Runtime::instance_
    adrp xIP0, :got:_ZN3art7Runtime9instance_E
    ldr xIP0, [xIP0, #:got_lo12:_ZN3art7Runtime9instance_E]

    // Our registers aren't intermixed - just spill in order.
    ldr xIP0, [xIP0]  // art::Runtime* xIP0 = art::Runtime::instance_;

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveAllCalleeSaves];
    ldr xIP0, [xIP0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET]

    INCREASE_FRAME 176

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 176)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(ARM64) size not as expected."
#endif

    // Stack alignment filler [sp, #8].
    // FP callee-saves.
    stp d8, d9,   [sp, #16]
    stp d10, d11, [sp, #32]
    stp d12, d13, [sp, #48]
    stp d14, d15, [sp, #64]

    // GP callee-saves
    SAVE_TWO_REGS x19, x20, 80
    SAVE_TWO_REGS x21, x22, 96
    SAVE_TWO_REGS x23, x24, 112
    SAVE_TWO_REGS x25, x26, 128
    SAVE_TWO_REGS x27, x28, 144
    SAVE_TWO_REGS x29, xLR, 160

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves].
    str xIP0, [sp]
    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsOnly).
     */
.macro SETUP_SAVE_REFS_ONLY_FRAME
    // art::Runtime** xIP0 = &art::Runtime::instance_
    adrp xIP0, :got:_ZN3art7Runtime9instance_E
    ldr xIP0, [xIP0, #:got_lo12:_ZN3art7Runtime9instance_E]

    // Our registers aren't intermixed - just spill in order.
    ldr xIP0, [xIP0]  // art::Runtime* xIP0 = art::Runtime::instance_;

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveRefOnly];
    ldr xIP0, [xIP0, RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET]

    INCREASE_FRAME 96

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_ONLY != 96)
#error "FRAME_SIZE_SAVE_REFS_ONLY(ARM64) size not as expected."
#endif

    // GP callee-saves.
    // x20 paired with ArtMethod* - see below.
    SAVE_TWO_REGS x21, x22, 16
    SAVE_TWO_REGS x23, x24, 32
    SAVE_TWO_REGS x25, x26, 48
    SAVE_TWO_REGS x27, x28, 64
    SAVE_TWO_REGS x29, xLR, 80

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsOnly].
    stp xIP0, x20, [sp]
    .cfi_rel_offset x20, 8

    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

// TODO: Probably no need to restore registers preserved by aapcs64.
.macro RESTORE_SAVE_REFS_ONLY_FRAME
    // Callee-saves.
    RESTORE_REG x20, 8
    RESTORE_TWO_REGS x21, x22, 16
    RESTORE_TWO_REGS x23, x24, 32
    RESTORE_TWO_REGS x25, x26, 48
    RESTORE_TWO_REGS x27, x28, 64
    RESTORE_TWO_REGS x29, xLR, 80

    DECREASE_FRAME 96
.endm

.macro POP_SAVE_REFS_ONLY_FRAME
    DECREASE_FRAME 96
.endm

.macro RESTORE_SAVE_REFS_ONLY_FRAME_AND_RETURN
    RESTORE_SAVE_REFS_ONLY_FRAME
    ret
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    INCREASE_FRAME 224

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 224)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(ARM64) size not as expected."
#endif

    // Stack alignment filler [sp, #8].
    // FP args.
    stp d0, d1, [sp, #16]
    stp d2, d3, [sp, #32]
    stp d4, d5, [sp, #48]
    stp d6, d7, [sp, #64]

    // Core args.
    SAVE_TWO_REGS x1, x2, 80
    SAVE_TWO_REGS x3, x4, 96
    SAVE_TWO_REGS x5, x6, 112

    // x7, Callee-saves.
    SAVE_TWO_REGS x7, x20, 128
    SAVE_TWO_REGS x21, x22, 144
    SAVE_TWO_REGS x23, x24, 160
    SAVE_TWO_REGS x25, x26, 176
    SAVE_TWO_REGS x27, x28, 192

    // x29(callee-save) and LR.
    SAVE_TWO_REGS x29, xLR, 208

.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs).
     *
     * TODO This is probably too conservative - saving FP & LR.
     */
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME
    // art::Runtime** xIP0 = &art::Runtime::instance_
    adrp xIP0, :got:_ZN3art7Runtime9instance_E
    ldr xIP0, [xIP0, #:got_lo12:_ZN3art7Runtime9instance_E]

    // Our registers aren't intermixed - just spill in order.
    ldr xIP0, [xIP0]  // art::Runtime* xIP0 = art::Runtime::instance_;

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveRefAndArgs];
    ldr xIP0, [xIP0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET]

    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL

    str xIP0, [sp]    // Store ArtMethod* Runtime::callee_save_methods_[kSaveRefsAndArgs].
    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    str x0, [sp, #0]  // Store ArtMethod* to bottom of stack.
    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

// TODO: Probably no need to restore registers preserved by aapcs64.
.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    // FP args.
    ldp d0, d1, [sp, #16]
    ldp d2, d3, [sp, #32]
    ldp d4, d5, [sp, #48]
    ldp d6, d7, [sp, #64]

    // Core args.
    RESTORE_TWO_REGS x1, x2, 80
    RESTORE_TWO_REGS x3, x4, 96
    RESTORE_TWO_REGS x5, x6, 112

    // x7, Callee-saves.
    RESTORE_TWO_REGS x7, x20, 128
    RESTORE_TWO_REGS x21, x22, 144
    RESTORE_TWO_REGS x23, x24, 160
    RESTORE_TWO_REGS x25, x26, 176
    RESTORE_TWO_REGS x27, x28, 192

    // x29(callee-save) and LR.
    RESTORE_TWO_REGS x29, xLR, 208

    DECREASE_FRAME 224
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveEverything)
     * when the SP has already been decremented by FRAME_SIZE_SAVE_EVERYTHING
     * and saving registers x29 and LR is handled elsewhere.
     */
.macro SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_X29_LR
    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_EVERYTHING != 512)
#error "FRAME_SIZE_SAVE_EVERYTHING(ARM64) size not as expected."
#endif

    // Save FP registers.
    // For better performance, store d0 and d31 separately, so that all STPs are 16-byte aligned.
    str d0,       [sp, #8]
    stp d1, d2,   [sp, #16]
    stp d3, d4,   [sp, #32]
    stp d5, d6,   [sp, #48]
    stp d7, d8,   [sp, #64]
    stp d9, d10,  [sp, #80]
    stp d11, d12, [sp, #96]
    stp d13, d14, [sp, #112]
    stp d15, d16, [sp, #128]
    stp d17, d18, [sp, #144]
    stp d19, d20, [sp, #160]
    stp d21, d22, [sp, #176]
    stp d23, d24, [sp, #192]
    stp d25, d26, [sp, #208]
    stp d27, d28, [sp, #224]
    stp d29, d30, [sp, #240]
    str d31,      [sp, #256]

    // Save core registers.
    SAVE_REG            x0, 264
    SAVE_TWO_REGS  x1,  x2, 272
    SAVE_TWO_REGS  x3,  x4, 288
    SAVE_TWO_REGS  x5,  x6, 304
    SAVE_TWO_REGS  x7,  x8, 320
    SAVE_TWO_REGS  x9, x10, 336
    SAVE_TWO_REGS x11, x12, 352
    SAVE_TWO_REGS x13, x14, 368
    SAVE_TWO_REGS x15, x16, 384
    SAVE_TWO_REGS x17, x18, 400
    SAVE_TWO_REGS x19, x20, 416
    SAVE_TWO_REGS x21, x22, 432
    SAVE_TWO_REGS x23, x24, 448
    SAVE_TWO_REGS x25, x26, 464
    SAVE_TWO_REGS x27, x28, 480

    // art::Runtime** xIP0 = &art::Runtime::instance_
    adrp xIP0, :got:_ZN3art7Runtime9instance_E
    ldr xIP0, [xIP0, #:got_lo12:_ZN3art7Runtime9instance_E]

    ldr xIP0, [xIP0]  // art::Runtime* xIP0 = art::Runtime::instance_;

    // ArtMethod* xIP0 = Runtime::instance_->callee_save_methods_[kSaveEverything];
    ldr xIP0, [xIP0, RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET]

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveEverything].
    str xIP0, [sp]
    // Place sp in Thread::Current()->top_quick_frame.
    mov xIP0, sp
    str xIP0, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveEverything)
     */
.macro SETUP_SAVE_EVERYTHING_FRAME
    INCREASE_FRAME 512
    SAVE_TWO_REGS x29, xLR, 496
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_X29_LR
.endm

.macro RESTORE_SAVE_EVERYTHING_FRAME
    // Restore FP registers.
    // For better performance, load d0 and d31 separately, so that all LDPs are 16-byte aligned.
    ldr d0,       [sp, #8]
    ldp d1, d2,   [sp, #16]
    ldp d3, d4,   [sp, #32]
    ldp d5, d6,   [sp, #48]
    ldp d7, d8,   [sp, #64]
    ldp d9, d10,  [sp, #80]
    ldp d11, d12, [sp, #96]
    ldp d13, d14, [sp, #112]
    ldp d15, d16, [sp, #128]
    ldp d17, d18, [sp, #144]
    ldp d19, d20, [sp, #160]
    ldp d21, d22, [sp, #176]
    ldp d23, d24, [sp, #192]
    ldp d25, d26, [sp, #208]
    ldp d27, d28, [sp, #224]
    ldp d29, d30, [sp, #240]
    ldr d31,      [sp, #256]

    // Restore core registers.
    RESTORE_REG            x0, 264
    RESTORE_TWO_REGS  x1,  x2, 272
    RESTORE_TWO_REGS  x3,  x4, 288
    RESTORE_TWO_REGS  x5,  x6, 304
    RESTORE_TWO_REGS  x7,  x8, 320
    RESTORE_TWO_REGS  x9, x10, 336
    RESTORE_TWO_REGS x11, x12, 352
    RESTORE_TWO_REGS x13, x14, 368
    RESTORE_TWO_REGS x15, x16, 384
    RESTORE_TWO_REGS x17, x18, 400
    RESTORE_TWO_REGS x19, x20, 416
    RESTORE_TWO_REGS x21, x22, 432
    RESTORE_TWO_REGS x23, x24, 448
    RESTORE_TWO_REGS x25, x26, 464
    RESTORE_TWO_REGS x27, x28, 480
    RESTORE_TWO_REGS x29, xLR, 496

    DECREASE_FRAME 512
.endm

.macro RETURN_IF_RESULT_IS_ZERO
    cbnz x0, 1f                // result non-zero branch over
    ret                        // return
1:
.endm

.macro RETURN_IF_RESULT_IS_NON_ZERO
    cbz x0, 1f                 // result zero branch over
    ret                        // return
1:
.endm

    /*
     * Macro that set calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_
     */
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    mov x0, xSELF

    // Point of no return.
    b artDeliverPendingExceptionFromCode  // artDeliverPendingExceptionFromCode(Thread*)
    brk 0  // Unreached
.endm

.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_REG reg
    ldr \reg, [xSELF, # THREAD_EXCEPTION_OFFSET]   // Get exception field.
    cbnz \reg, 1f
    ret
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro RETURN_OR_DELIVER_PENDING_EXCEPTION
    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG xIP0
.endm

// Same as above with x1. This is helpful in stubs that want to avoid clobbering another register.
.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG x1
.endm

.macro RETURN_IF_W0_IS_ZERO_OR_DELIVER
    cbnz w0, 1f                // result non-zero branch over
    ret                        // return
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro NO_ARG_RUNTIME_EXCEPTION c_name, cxx_name
    .extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context
    mov x0, xSELF                     // pass Thread::Current
    b   \cxx_name                     // \cxx_name(Thread*)
END \c_name
.endm

.macro ONE_ARG_RUNTIME_EXCEPTION c_name, cxx_name
    .extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context.
    mov x1, xSELF                     // pass Thread::Current.
    b   \cxx_name                     // \cxx_name(arg, Thread*).
    brk 0
END \c_name
.endm

.macro TWO_ARG_RUNTIME_EXCEPTION c_name, cxx_name
    .extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context
    mov x2, xSELF                     // pass Thread::Current
    b   \cxx_name                     // \cxx_name(arg1, arg2, Thread*)
    brk 0
END \c_name
.endm

    /*
     * Called by managed code, saves callee saves and then calls artThrowException
     * that will place a mock Method* at the bottom of the stack. Arg1 holds the exception.
     */
ONE_ARG_RUNTIME_EXCEPTION art_quick_deliver_exception, artDeliverExceptionFromCode

    /*
     * Called by managed code to create and deliver a NullPointerException.
     */
NO_ARG_RUNTIME_EXCEPTION art_quick_throw_null_pointer_exception, artThrowNullPointerExceptionFromCode

    /*
     * Call installed by a signal handler to create and deliver a NullPointerException.
     */
    .extern art_quick_throw_null_pointer_exception_from_signal
ENTRY art_quick_throw_null_pointer_exception_from_signal
    // The fault handler pushes the gc map address, i.e. "return address", to stack
    // and passes the fault address in LR. So we need to set up the CFI info accordingly.
    .cfi_def_cfa_offset __SIZEOF_POINTER__
    .cfi_rel_offset lr, 0
    // Save all registers as basis for long jump context.
    INCREASE_FRAME (FRAME_SIZE_SAVE_EVERYTHING - __SIZEOF_POINTER__)
    SAVE_REG x29, (FRAME_SIZE_SAVE_EVERYTHING - 2 * __SIZEOF_POINTER__)  // LR already saved.
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_X29_LR
    mov x0, lr                        // pass the fault address stored in LR by the fault handler.
    mov x1, xSELF                     // pass Thread::Current.
    b   artThrowNullPointerExceptionFromSignal  // (arg, Thread*).
    brk 0
END art_quick_throw_null_pointer_exception_from_signal

    /*
     * Called by managed code to create and deliver an ArithmeticException.
     */
NO_ARG_RUNTIME_EXCEPTION art_quick_throw_div_zero, artThrowDivZeroFromCode

    /*
     * Called by managed code to create and deliver an ArrayIndexOutOfBoundsException. Arg1 holds
     * index, arg2 holds limit.
     */
TWO_ARG_RUNTIME_EXCEPTION art_quick_throw_array_bounds, artThrowArrayBoundsFromCode

    /*
     * Called by managed code to create and deliver a StringIndexOutOfBoundsException
     * as if thrown from a call to String.charAt(). Arg1 holds index, arg2 holds limit.
     */
TWO_ARG_RUNTIME_EXCEPTION art_quick_throw_string_bounds, artThrowStringBoundsFromCode

    /*
     * Called by managed code to create and deliver a StackOverflowError.
     */
NO_ARG_RUNTIME_EXCEPTION art_quick_throw_stack_overflow, artThrowStackOverflowFromCode

    /*
     * Called by managed code to create and deliver a NoSuchMethodError.
     */
ONE_ARG_RUNTIME_EXCEPTION art_quick_throw_no_such_method, artThrowNoSuchMethodFromCode

    /*
     * All generated callsites for interface invokes and invocation slow paths will load arguments
     * as usual - except instead of loading arg0/x0 with the target Method*, arg0/x0 will contain
     * the method_idx.  This wrapper will save arg1-arg3, and call the appropriate C helper.
     * NOTE: "this" is first visible argument of the target, and so can be found in arg1/x1.
     *
     * The helper will attempt to locate the target and return a 128-bit result in x0/x1 consisting
     * of the target Method* in x0 and method->code_ in x1.
     *
     * If unsuccessful, the helper will return null/????. There will be a pending exception in the
     * thread and we branch to another stub to deliver it.
     *
     * On success this wrapper will restore arguments and *jump* to the target, leaving the lr
     * pointing back to the original caller.
     *
     * Adapted from ARM32 code.
     *
     * Clobbers xIP0.
     */
.macro INVOKE_TRAMPOLINE_BODY cxx_name
    .extern \cxx_name
    SETUP_SAVE_REFS_AND_ARGS_FRAME        // save callee saves in case allocation triggers GC
    // Helper signature is always
    // (method_idx, *this_object, *caller_method, *self, sp)

    mov    x2, xSELF                      // pass Thread::Current
    mov    x3, sp
    bl     \cxx_name                      // (method_idx, this, Thread*, SP)
    mov    xIP0, x1                       // save Method*->code_
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    cbz    x0, 1f                         // did we find the target? if not go to exception delivery
    br     xIP0                           // tail call to target
1:
    DELIVER_PENDING_EXCEPTION
.endm
.macro INVOKE_TRAMPOLINE c_name, cxx_name
ENTRY \c_name
    INVOKE_TRAMPOLINE_BODY \cxx_name
END \c_name
.endm

INVOKE_TRAMPOLINE art_quick_invoke_interface_trampoline_with_access_check, artInvokeInterfaceTrampolineWithAccessCheck

INVOKE_TRAMPOLINE art_quick_invoke_static_trampoline_with_access_check, artInvokeStaticTrampolineWithAccessCheck
INVOKE_TRAMPOLINE art_quick_invoke_direct_trampoline_with_access_check, artInvokeDirectTrampolineWithAccessCheck
INVOKE_TRAMPOLINE art_quick_invoke_super_trampoline_with_access_check, artInvokeSuperTrampolineWithAccessCheck
INVOKE_TRAMPOLINE art_quick_invoke_virtual_trampoline_with_access_check, artInvokeVirtualTrampolineWithAccessCheck


.macro INVOKE_STUB_CREATE_FRAME

SAVE_SIZE=15*8   // x4, x5, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, SP, LR, FP saved.
SAVE_SIZE_AND_METHOD=SAVE_SIZE+8


    mov x9, sp                             // Save stack pointer.
    .cfi_register sp,x9

    add x10, x2, # SAVE_SIZE_AND_METHOD    // calculate size of frame.
    sub x10, sp, x10                       // Calculate SP position - saves + ArtMethod* + args
    and x10, x10, # ~0xf                   // Enforce 16 byte stack alignment.
    mov sp, x10                            // Set new SP.

    sub x10, x9, #SAVE_SIZE                // Calculate new FP (later). Done here as we must move SP
    .cfi_def_cfa_register x10              // before this.
    .cfi_adjust_cfa_offset SAVE_SIZE

    str x28, [x10, #112]
    .cfi_rel_offset x28, 112

    stp x26, x27, [x10, #96]
    .cfi_rel_offset x26, 96
    .cfi_rel_offset x27, 104

    stp x24, x25, [x10, #80]
    .cfi_rel_offset x24, 80
    .cfi_rel_offset x25, 88

    stp x22, x23, [x10, #64]
    .cfi_rel_offset x22, 64
    .cfi_rel_offset x23, 72

    stp x20, x21, [x10, #48]
    .cfi_rel_offset x20, 48
    .cfi_rel_offset x21, 56

    stp x9, x19, [x10, #32]                // Save old stack pointer and x19.
    .cfi_rel_offset sp, 32
    .cfi_rel_offset x19, 40

    stp x4, x5, [x10, #16]                 // Save result and shorty addresses.
    .cfi_rel_offset x4, 16
    .cfi_rel_offset x5, 24

    stp xFP, xLR, [x10]                    // Store LR & FP.
    .cfi_rel_offset x29, 0
    .cfi_rel_offset x30, 8

    mov xFP, x10                           // Use xFP now, as it's callee-saved.
    .cfi_def_cfa_register x29
    mov xSELF, x3                          // Move thread pointer into SELF register.

    // Copy arguments into stack frame.
    // Use simple copy routine for now.
    // 4 bytes per slot.
    // X1 - source address
    // W2 - args length
    // X9 - destination address.
    // W10 - temporary
    add x9, sp, #8                         // Destination address is bottom of stack + null.

    // Copy parameters into the stack. Use numeric label as this is a macro and Clang's assembler
    // does not have unique-id variables.
1:
    cmp w2, #0
    beq 2f
    sub w2, w2, #4      // Need 65536 bytes of range.
    ldr w10, [x1, x2]
    str w10, [x9, x2]

    b 1b

2:
    // Store null into ArtMethod* at bottom of frame.
    str xzr, [sp]
.endm

.macro INVOKE_STUB_CALL_AND_RETURN

    // load method-> METHOD_QUICK_CODE_OFFSET
    ldr x9, [x0, #ART_METHOD_QUICK_CODE_OFFSET_64]
    // Branch to method.
    blr x9

    // Restore return value address and shorty address.
    ldp x4,x5, [xFP, #16]
    .cfi_restore x4
    .cfi_restore x5

    ldr x28, [xFP, #112]
    .cfi_restore x28

    ldp x26, x27, [xFP, #96]
    .cfi_restore x26
    .cfi_restore x27

    ldp x24, x25, [xFP, #80]
    .cfi_restore x24
    .cfi_restore x25

    ldp x22, x23, [xFP, #64]
    .cfi_restore x22
    .cfi_restore x23

    ldp x20, x21, [xFP, #48]
    .cfi_restore x20
    .cfi_restore x21

    // Store result (w0/x0/s0/d0) appropriately, depending on resultType.
    ldrb w10, [x5]

    // Check the return type and store the correct register into the jvalue in memory.
    // Use numeric label as this is a macro and Clang's assembler does not have unique-id variables.

    // Don't set anything for a void type.
    cmp w10, #'V'
    beq 3f

    // Is it a double?
    cmp w10, #'D'
    bne 1f
    str d0, [x4]
    b 3f

1:  // Is it a float?
    cmp w10, #'F'
    bne 2f
    str s0, [x4]
    b 3f

2:  // Just store x0. Doesn't matter if it is 64 or 32 bits.
    str x0, [x4]

3:  // Finish up.
    ldp x2, x19, [xFP, #32]   // Restore stack pointer and x19.
    .cfi_restore x19
    mov sp, x2
    .cfi_restore sp

    ldp xFP, xLR, [xFP]    // Restore old frame pointer and link register.
    .cfi_restore x29
    .cfi_restore x30

    ret

.endm


/*
 *  extern"C" void art_quick_invoke_stub(ArtMethod *method,   x0
 *                                       uint32_t  *args,     x1
 *                                       uint32_t argsize,    w2
 *                                       Thread *self,        x3
 *                                       JValue *result,      x4
 *                                       char   *shorty);     x5
 *  +----------------------+
 *  |                      |
 *  |  C/C++ frame         |
 *  |       LR''           |
 *  |       FP''           | <- SP'
 *  +----------------------+
 *  +----------------------+
 *  |        x28           | <- TODO: Remove callee-saves.
 *  |         :            |
 *  |        x19           |
 *  |        SP'           |
 *  |        X5            |
 *  |        X4            |        Saved registers
 *  |        LR'           |
 *  |        FP'           | <- FP
 *  +----------------------+
 *  | uint32_t out[n-1]    |
 *  |    :      :          |        Outs
 *  | uint32_t out[0]      |
 *  | ArtMethod*           | <- SP  value=null
 *  +----------------------+
 *
 * Outgoing registers:
 *  x0    - Method*
 *  x1-x7 - integer parameters.
 *  d0-d7 - Floating point parameters.
 *  xSELF = self
 *  SP = & of ArtMethod*
 *  x1 = "this" pointer.
 *
 */
ENTRY art_quick_invoke_stub
    // Spill registers as per AACPS64 calling convention.
    INVOKE_STUB_CREATE_FRAME

    // Fill registers x/w1 to x/w7 and s/d0 to s/d7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load WXSD registers.
    adr  x11, .LstoreW2
    adr  x12, .LstoreX2
    adr  x13, .LstoreS0
    adr  x14, .LstoreD0

    // Initialize routine offsets to 0 for integers and floats.
    // x8 for integers, x15 for floating point.
    mov x8, #0
    mov x15, #0

    add x10, x5, #1         // Load shorty address, plus one to skip return value.
    ldr w1, [x9],#4         // Load "this" parameter, and increment arg pointer.

    // Loop to fill registers.
.LfillRegisters:
    ldrb w17, [x10], #1       // Load next character in signature, and increment.
    cbz w17, .LcallFunction   // Exit at end of signature. Shorty 0 terminated.

    cmp  w17, #'F' // is this a float?
    bne .LisDouble

    cmp x15, # 8*12         // Skip this load if all registers full.
    beq .Ladvance4

    add x17, x13, x15       // Calculate subroutine to jump to.
    br  x17

.LisDouble:
    cmp w17, #'D'           // is this a double?
    bne .LisLong

    cmp x15, # 8*12         // Skip this load if all registers full.
    beq .Ladvance8

    add x17, x14, x15       // Calculate subroutine to jump to.
    br x17

.LisLong:
    cmp w17, #'J'           // is this a long?
    bne .LisOther

    cmp x8, # 6*12          // Skip this load if all registers full.
    beq .Ladvance8

    add x17, x12, x8        // Calculate subroutine to jump to.
    br x17

.LisOther:                  // Everything else takes one vReg.
    cmp x8, # 6*12          // Skip this load if all registers full.
    beq .Ladvance4

    add x17, x11, x8        // Calculate subroutine to jump to.
    br x17

.Ladvance4:
    add x9, x9, #4
    b .LfillRegisters

.Ladvance8:
    add x9, x9, #8
    b .LfillRegisters

// Macro for loading a parameter into a register.
//  counter - the register with offset into these tables
//  size - the size of the register - 4 or 8 bytes.
//  register - the name of the register to be loaded.
.macro LOADREG counter size register return
    ldr \register , [x9], #\size
    add \counter, \counter, 12
    b \return
.endm

// Store ints.
.LstoreW2:
    LOADREG x8 4 w2 .LfillRegisters
    LOADREG x8 4 w3 .LfillRegisters
    LOADREG x8 4 w4 .LfillRegisters
    LOADREG x8 4 w5 .LfillRegisters
    LOADREG x8 4 w6 .LfillRegisters
    LOADREG x8 4 w7 .LfillRegisters

// Store longs.
.LstoreX2:
    LOADREG x8 8 x2 .LfillRegisters
    LOADREG x8 8 x3 .LfillRegisters
    LOADREG x8 8 x4 .LfillRegisters
    LOADREG x8 8 x5 .LfillRegisters
    LOADREG x8 8 x6 .LfillRegisters
    LOADREG x8 8 x7 .LfillRegisters

// Store singles.
.LstoreS0:
    LOADREG x15 4 s0 .LfillRegisters
    LOADREG x15 4 s1 .LfillRegisters
    LOADREG x15 4 s2 .LfillRegisters
    LOADREG x15 4 s3 .LfillRegisters
    LOADREG x15 4 s4 .LfillRegisters
    LOADREG x15 4 s5 .LfillRegisters
    LOADREG x15 4 s6 .LfillRegisters
    LOADREG x15 4 s7 .LfillRegisters

// Store doubles.
.LstoreD0:
    LOADREG x15 8 d0 .LfillRegisters
    LOADREG x15 8 d1 .LfillRegisters
    LOADREG x15 8 d2 .LfillRegisters
    LOADREG x15 8 d3 .LfillRegisters
    LOADREG x15 8 d4 .LfillRegisters
    LOADREG x15 8 d5 .LfillRegisters
    LOADREG x15 8 d6 .LfillRegisters
    LOADREG x15 8 d7 .LfillRegisters


.LcallFunction:

    INVOKE_STUB_CALL_AND_RETURN

END art_quick_invoke_stub

/*  extern"C"
 *     void art_quick_invoke_static_stub(ArtMethod *method,   x0
 *                                       uint32_t  *args,     x1
 *                                       uint32_t argsize,    w2
 *                                       Thread *self,        x3
 *                                       JValue *result,      x4
 *                                       char   *shorty);     x5
 */
ENTRY art_quick_invoke_static_stub
    // Spill registers as per AACPS64 calling convention.
    INVOKE_STUB_CREATE_FRAME

    // Fill registers x/w1 to x/w7 and s/d0 to s/d7 with parameters.
    // Parse the passed shorty to determine which register to load.
    // Load addresses for routines that load WXSD registers.
    adr  x11, .LstoreW1_2
    adr  x12, .LstoreX1_2
    adr  x13, .LstoreS0_2
    adr  x14, .LstoreD0_2

    // Initialize routine offsets to 0 for integers and floats.
    // x8 for integers, x15 for floating point.
    mov x8, #0
    mov x15, #0

    add x10, x5, #1     // Load shorty address, plus one to skip return value.

    // Loop to fill registers.
.LfillRegisters2:
    ldrb w17, [x10], #1         // Load next character in signature, and increment.
    cbz w17, .LcallFunction2    // Exit at end of signature. Shorty 0 terminated.

    cmp  w17, #'F'          // is this a float?
    bne .LisDouble2

    cmp x15, # 8*12         // Skip this load if all registers full.
    beq .Ladvance4_2

    add x17, x13, x15       // Calculate subroutine to jump to.
    br  x17

.LisDouble2:
    cmp w17, #'D'           // is this a double?
    bne .LisLong2

    cmp x15, # 8*12         // Skip this load if all registers full.
    beq .Ladvance8_2

    add x17, x14, x15       // Calculate subroutine to jump to.
    br x17

.LisLong2:
    cmp w17, #'J'           // is this a long?
    bne .LisOther2

    cmp x8, # 7*12          // Skip this load if all registers full.
    beq .Ladvance8_2

    add x17, x12, x8        // Calculate subroutine to jump to.
    br x17

.LisOther2:                 // Everything else takes one vReg.
    cmp x8, # 7*12          // Skip this load if all registers full.
    beq .Ladvance4_2

    add x17, x11, x8        // Calculate subroutine to jump to.
    br x17

.Ladvance4_2:
    add x9, x9, #4
    b .LfillRegisters2

.Ladvance8_2:
    add x9, x9, #8
    b .LfillRegisters2

// Store ints.
.LstoreW1_2:
    LOADREG x8 4 w1 .LfillRegisters2
    LOADREG x8 4 w2 .LfillRegisters2
    LOADREG x8 4 w3 .LfillRegisters2
    LOADREG x8 4 w4 .LfillRegisters2
    LOADREG x8 4 w5 .LfillRegisters2
    LOADREG x8 4 w6 .LfillRegisters2
    LOADREG x8 4 w7 .LfillRegisters2

// Store longs.
.LstoreX1_2:
    LOADREG x8 8 x1 .LfillRegisters2
    LOADREG x8 8 x2 .LfillRegisters2
    LOADREG x8 8 x3 .LfillRegisters2
    LOADREG x8 8 x4 .LfillRegisters2
    LOADREG x8 8 x5 .LfillRegisters2
    LOADREG x8 8 x6 .LfillRegisters2
    LOADREG x8 8 x7 .LfillRegisters2

// Store singles.
.LstoreS0_2:
    LOADREG x15 4 s0 .LfillRegisters2
    LOADREG x15 4 s1 .LfillRegisters2
    LOADREG x15 4 s2 .LfillRegisters2
    LOADREG x15 4 s3 .LfillRegisters2
    LOADREG x15 4 s4 .LfillRegisters2
    LOADREG x15 4 s5 .LfillRegisters2
    LOADREG x15 4 s6 .LfillRegisters2
    LOADREG x15 4 s7 .LfillRegisters2

// Store doubles.
.LstoreD0_2:
    LOADREG x15 8 d0 .LfillRegisters2
    LOADREG x15 8 d1 .LfillRegisters2
    LOADREG x15 8 d2 .LfillRegisters2
    LOADREG x15 8 d3 .LfillRegisters2
    LOADREG x15 8 d4 .LfillRegisters2
    LOADREG x15 8 d5 .LfillRegisters2
    LOADREG x15 8 d6 .LfillRegisters2
    LOADREG x15 8 d7 .LfillRegisters2


.LcallFunction2:

    INVOKE_STUB_CALL_AND_RETURN

END art_quick_invoke_static_stub



/*  extern"C" void art_quick_osr_stub(void** stack,                x0
 *                                    size_t stack_size_in_bytes,  x1
 *                                    const uin8_t* native_pc,     x2
 *                                    JValue *result,              x3
 *                                    char   *shorty,              x4
 *                                    Thread *self)                x5
 */
ENTRY art_quick_osr_stub
SAVE_SIZE=15*8   // x3, x4, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, SP, LR, FP saved.
    mov x9, sp                             // Save stack pointer.
    .cfi_register sp,x9

    sub x10, sp, # SAVE_SIZE
    and x10, x10, # ~0xf                   // Enforce 16 byte stack alignment.
    mov sp, x10                            // Set new SP.

    str x28, [sp, #112]
    stp x26, x27, [sp, #96]
    stp x24, x25, [sp, #80]
    stp x22, x23, [sp, #64]
    stp x20, x21, [sp, #48]
    stp x9, x19, [sp, #32]                // Save old stack pointer and x19.
    stp x3, x4, [sp, #16]                 // Save result and shorty addresses.
    stp xFP, xLR, [sp]                    // Store LR & FP.
    mov xSELF, x5                         // Move thread pointer into SELF register.

    sub sp, sp, #16
    str xzr, [sp]                         // Store null for ArtMethod* slot
    // Branch to stub.
    bl .Losr_entry
    add sp, sp, #16

    // Restore return value address and shorty address.
    ldp x3,x4, [sp, #16]
    ldr x28, [sp, #112]
    ldp x26, x27, [sp, #96]
    ldp x24, x25, [sp, #80]
    ldp x22, x23, [sp, #64]
    ldp x20, x21, [sp, #48]

    // Store result (w0/x0/s0/d0) appropriately, depending on resultType.
    ldrb w10, [x4]

    // Check the return type and store the correct register into the jvalue in memory.

    // Don't set anything for a void type.
    cmp w10, #'V'
    beq .Losr_exit

    // Is it a double?
    cmp w10, #'D'
    bne .Lno_double
    str d0, [x3]
    b .Losr_exit

.Lno_double:  // Is it a float?
    cmp w10, #'F'
    bne .Lno_float
    str s0, [x3]
    b .Losr_exit

.Lno_float:  // Just store x0. Doesn't matter if it is 64 or 32 bits.
    str x0, [x3]

.Losr_exit:  // Finish up.
    ldp x2, x19, [sp, #32]   // Restore stack pointer and x19.
    ldp xFP, xLR, [sp]    // Restore old frame pointer and link register.
    mov sp, x2
    ret

.Losr_entry:
    // Update stack pointer for the callee
    sub sp, sp, x1

    // Update link register slot expected by the callee.
    sub w1, w1, #8
    str lr, [sp, x1]

    // Copy arguments into stack frame.
    // Use simple copy routine for now.
    // 4 bytes per slot.
    // X0 - source address
    // W1 - args length
    // SP - destination address.
    // W10 - temporary
.Losr_loop_entry:
    cmp w1, #0
    beq .Losr_loop_exit
    sub w1, w1, #4
    ldr w10, [x0, x1]
    str w10, [sp, x1]
    b .Losr_loop_entry

.Losr_loop_exit:
    // Branch to the OSR entry point.
    br x2

END art_quick_osr_stub

    /*
     * On entry x0 is uintptr_t* gprs_ and x1 is uint64_t* fprs_
     */

ENTRY art_quick_do_long_jump
    // Load FPRs
    ldp d0, d1, [x1], #16
    ldp d2, d3, [x1], #16
    ldp d4, d5, [x1], #16
    ldp d6, d7, [x1], #16
    ldp d8, d9, [x1], #16
    ldp d10, d11, [x1], #16
    ldp d12, d13, [x1], #16
    ldp d14, d15, [x1], #16
    ldp d16, d17, [x1], #16
    ldp d18, d19, [x1], #16
    ldp d20, d21, [x1], #16
    ldp d22, d23, [x1], #16
    ldp d24, d25, [x1], #16
    ldp d26, d27, [x1], #16
    ldp d28, d29, [x1], #16
    ldp d30, d31, [x1]

    // Load GPRs
    // TODO: lots of those are smashed, could optimize.
    add x0, x0, #30*8
    ldp x30, x1, [x0], #-16          // LR & SP
    ldp x28, x29, [x0], #-16
    ldp x26, x27, [x0], #-16
    ldp x24, x25, [x0], #-16
    ldp x22, x23, [x0], #-16
    ldp x20, x21, [x0], #-16
    ldp x18, x19, [x0], #-16
    ldp x16, x17, [x0], #-16
    ldp x14, x15, [x0], #-16
    ldp x12, x13, [x0], #-16
    ldp x10, x11, [x0], #-16
    ldp x8, x9, [x0], #-16
    ldp x6, x7, [x0], #-16
    ldp x4, x5, [x0], #-16
    ldp x2, x3, [x0], #-16
    mov sp, x1

    // Need to load PC, it's at the end (after the space for the unused XZR). Use x1.
    ldr x1, [x0, #33*8]
    // And the value of x0.
    ldr x0, [x0]

    br  x1
END art_quick_do_long_jump

    /*
     * Entry from managed code that calls artLockObjectFromCode, may block for GC. x0 holds the
     * possibly null object to lock.
     *
     * Derived from arm32 code.
     */
    .extern artLockObjectFromCode
ENTRY art_quick_lock_object
    cbz    w0, .Lslow_lock
    add    x4, x0, #MIRROR_OBJECT_LOCK_WORD_OFFSET  // exclusive load/store has no immediate anymore
.Lretry_lock:
    ldr    w2, [xSELF, #THREAD_ID_OFFSET] // TODO: Can the thread ID really change during the loop?
    ldxr   w1, [x4]
    mov    x3, x1
    and    w3, w3, #LOCK_WORD_GC_STATE_MASK_SHIFTED_TOGGLED  // zero the gc bits
    cbnz   w3, .Lnot_unlocked         // already thin locked
    // unlocked case - x1: original lock word that's zero except for the read barrier bits.
    orr    x2, x1, x2                 // x2 holds thread id with count of 0 with preserved read barrier bits
    stxr   w3, w2, [x4]
    cbnz   w3, .Llock_stxr_fail       // store failed, retry
    dmb    ishld                      // full (LoadLoad|LoadStore) memory barrier
    ret
.Lnot_unlocked:  // x1: original lock word
    lsr    w3, w1, LOCK_WORD_STATE_SHIFT
    cbnz   w3, .Lslow_lock            // if either of the top two bits are set, go slow path
    eor    w2, w1, w2                 // lock_word.ThreadId() ^ self->ThreadId()
    uxth   w2, w2                     // zero top 16 bits
    cbnz   w2, .Lslow_lock            // lock word and self thread id's match -> recursive lock
                                      // else contention, go to slow path
    mov    x3, x1                     // copy the lock word to check count overflow.
    and    w3, w3, #LOCK_WORD_GC_STATE_MASK_SHIFTED_TOGGLED  // zero the gc bits.
    add    w2, w3, #LOCK_WORD_THIN_LOCK_COUNT_ONE  // increment count in lock word placing in w2 to check overflow
    lsr    w3, w2, #LOCK_WORD_GC_STATE_SHIFT     // if the first gc state bit is set, we overflowed.
    cbnz   w3, .Lslow_lock            // if we overflow the count go slow path
    add    w2, w1, #LOCK_WORD_THIN_LOCK_COUNT_ONE  // increment count for real
    stxr   w3, w2, [x4]
    cbnz   w3, .Llock_stxr_fail       // store failed, retry
    ret
.Llock_stxr_fail:
    b      .Lretry_lock               // retry
.Lslow_lock:
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case we block
    mov    x1, xSELF                  // pass Thread::Current
    bl     artLockObjectFromCode      // (Object* obj, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_W0_IS_ZERO_OR_DELIVER
END art_quick_lock_object

ENTRY art_quick_lock_object_no_inline
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case we block
    mov    x1, xSELF                  // pass Thread::Current
    bl     artLockObjectFromCode      // (Object* obj, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_W0_IS_ZERO_OR_DELIVER
END art_quick_lock_object_no_inline

    /*
     * Entry from managed code that calls artUnlockObjectFromCode and delivers exception on failure.
     * x0 holds the possibly null object to lock.
     *
     * Derived from arm32 code.
     */
    .extern artUnlockObjectFromCode
ENTRY art_quick_unlock_object
    cbz    x0, .Lslow_unlock
    add    x4, x0, #MIRROR_OBJECT_LOCK_WORD_OFFSET  // exclusive load/store has no immediate anymore
.Lretry_unlock:
#ifndef USE_READ_BARRIER
    ldr    w1, [x4]
#else
    ldxr   w1, [x4]                   // Need to use atomic instructions for read barrier
#endif
    lsr    w2, w1, LOCK_WORD_STATE_SHIFT
    cbnz   w2, .Lslow_unlock          // if either of the top two bits are set, go slow path
    ldr    w2, [xSELF, #THREAD_ID_OFFSET]
    mov    x3, x1                     // copy lock word to check thread id equality
    and    w3, w3, #LOCK_WORD_GC_STATE_MASK_SHIFTED_TOGGLED  // zero the gc bits
    eor    w3, w3, w2                 // lock_word.ThreadId() ^ self->ThreadId()
    uxth   w3, w3                     // zero top 16 bits
    cbnz   w3, .Lslow_unlock          // do lock word and self thread id's match?
    mov    x3, x1                     // copy lock word to detect transition to unlocked
    and    w3, w3, #LOCK_WORD_GC_STATE_MASK_SHIFTED_TOGGLED  // zero the gc bits
    cmp    w3, #LOCK_WORD_THIN_LOCK_COUNT_ONE
    bpl    .Lrecursive_thin_unlock
    // transition to unlocked
    mov    x3, x1
    and    w3, w3, #LOCK_WORD_GC_STATE_MASK_SHIFTED  // w3: zero except for the preserved read barrier bits
    dmb    ish                        // full (LoadStore|StoreStore) memory barrier
#ifndef USE_READ_BARRIER
    str    w3, [x4]
#else
    stxr   w2, w3, [x4]               // Need to use atomic instructions for read barrier
    cbnz   w2, .Lunlock_stxr_fail     // store failed, retry
#endif
    ret
.Lrecursive_thin_unlock:  // w1: original lock word
    sub    w1, w1, #LOCK_WORD_THIN_LOCK_COUNT_ONE  // decrement count
#ifndef USE_READ_BARRIER
    str    w1, [x4]
#else
    stxr   w2, w1, [x4]               // Need to use atomic instructions for read barrier
    cbnz   w2, .Lunlock_stxr_fail     // store failed, retry
#endif
    ret
.Lunlock_stxr_fail:
    b      .Lretry_unlock               // retry
.Lslow_unlock:
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case exception allocation triggers GC
    mov    x1, xSELF                  // pass Thread::Current
    bl     artUnlockObjectFromCode    // (Object* obj, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_W0_IS_ZERO_OR_DELIVER
END art_quick_unlock_object

ENTRY art_quick_unlock_object_no_inline
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case exception allocation triggers GC
    mov    x1, xSELF                  // pass Thread::Current
    bl     artUnlockObjectFromCode    // (Object* obj, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_W0_IS_ZERO_OR_DELIVER
END art_quick_unlock_object_no_inline

    /*
     * Entry from managed code that calls artIsAssignableFromCode and on failure calls
     * artThrowClassCastException.
     */
    .extern artThrowClassCastException
ENTRY art_quick_check_cast
    // Store arguments and link register
    // Stack needs to be 16B aligned on calls.
    SAVE_TWO_REGS_INCREASE_FRAME x0, x1, 32
    SAVE_REG xLR, 24

    // Call runtime code
    bl artIsAssignableFromCode

    // Check for exception
    cbz x0, .Lthrow_class_cast_exception

    // Restore and return
    .cfi_remember_state
    RESTORE_REG xLR, 24
    RESTORE_TWO_REGS_DECREASE_FRAME x0, x1, 32
    ret
    .cfi_restore_state                // Reset unwind info so following code unwinds.

.Lthrow_class_cast_exception:
    // Restore
    RESTORE_REG xLR, 24
    RESTORE_TWO_REGS_DECREASE_FRAME x0, x1, 32

    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context
    mov x2, xSELF                     // pass Thread::Current
    b artThrowClassCastException      // (Class*, Class*, Thread*)
    brk 0                             // We should not return here...
END art_quick_check_cast

// Restore xReg's value from [sp, #offset] if xReg is not the same as xExclude.
.macro POP_REG_NE xReg, offset, xExclude
    .ifnc \xReg, \xExclude
        ldr \xReg, [sp, #\offset]     // restore xReg
        .cfi_restore \xReg
    .endif
.endm

// Restore xReg1's value from [sp, #offset] if xReg1 is not the same as xExclude.
// Restore xReg2's value from [sp, #(offset + 8)] if xReg2 is not the same as xExclude.
.macro POP_REGS_NE xReg1, xReg2, offset, xExclude
    .ifc \xReg1, \xExclude
        ldr \xReg2, [sp, #(\offset + 8)]        // restore xReg2
    .else
        .ifc \xReg2, \xExclude
            ldr \xReg1, [sp, #\offset]          // restore xReg1
        .else
            ldp \xReg1, \xReg2, [sp, #\offset]  // restore xReg1 and xReg2
        .endif
    .endif
    .cfi_restore \xReg1
    .cfi_restore \xReg2
.endm

    /*
     * Macro to insert read barrier, only used in art_quick_aput_obj.
     * xDest, wDest and xObj are registers, offset is a defined literal such as
     * MIRROR_OBJECT_CLASS_OFFSET. Dest needs both x and w versions of the same register to handle
     * name mismatch between instructions. This macro uses the lower 32b of register when possible.
     * TODO: When read barrier has a fast path, add heap unpoisoning support for the fast path.
     */
.macro READ_BARRIER xDest, wDest, xObj, xTemp, wTemp, offset, number
#ifdef USE_READ_BARRIER
#ifdef USE_BAKER_READ_BARRIER
    ldr \wTemp, [\xObj, #MIRROR_OBJECT_LOCK_WORD_OFFSET]
    tbnz \wTemp, #LOCK_WORD_READ_BARRIER_STATE_SHIFT, .Lrb_slowpath\number
    // False dependency to avoid needing load/load fence.
    add \xObj, \xObj, \xTemp, lsr #32
    ldr \wDest, [\xObj, #\offset]   // Heap reference = 32b. This also zero-extends to \xDest.
    UNPOISON_HEAP_REF \wDest
    b .Lrb_exit\number
#endif
.Lrb_slowpath\number:
    // Store registers used in art_quick_aput_obj (x0-x4, LR), stack is 16B aligned.
    SAVE_TWO_REGS_INCREASE_FRAME x0, x1, 48
    SAVE_TWO_REGS x2, x3, 16
    SAVE_TWO_REGS x4, xLR, 32

    // mov x0, \xRef                // pass ref in x0 (no-op for now since parameter ref is unused)
    .ifnc \xObj, x1
        mov x1, \xObj               // pass xObj
    .endif
    mov w2, #\offset                // pass offset
    bl artReadBarrierSlow           // artReadBarrierSlow(ref, xObj, offset)
    // No need to unpoison return value in w0, artReadBarrierSlow() would do the unpoisoning.
    .ifnc \wDest, w0
        mov \wDest, w0              // save return value in wDest
    .endif

    // Conditionally restore saved registers
    POP_REG_NE x0, 0, \xDest
    POP_REG_NE x1, 8, \xDest
    POP_REG_NE x2, 16, \xDest
    POP_REG_NE x3, 24, \xDest
    POP_REG_NE x4, 32, \xDest
    RESTORE_REG xLR, 40
    DECREASE_FRAME 48
.Lrb_exit\number:
#else
    ldr \wDest, [\xObj, #\offset]   // Heap reference = 32b. This also zero-extends to \xDest.
    UNPOISON_HEAP_REF \wDest
#endif  // USE_READ_BARRIER
.endm

    /*
     * Entry from managed code for array put operations of objects where the value being stored
     * needs to be checked for compatibility.
     * x0 = array, x1 = index, x2 = value
     *
     * Currently all values should fit into w0/w1/w2, and w1 always will as indices are 32b. We
     * assume, though, that the upper 32b are zeroed out. At least for x1/w1 we can do better by
     * using index-zero-extension in load/stores.
     *
     * Temporaries: x3, x4
     * TODO: x4 OK? ip seems wrong here.
     */
ENTRY art_quick_aput_obj_with_null_and_bound_check
    tst x0, x0
    bne art_quick_aput_obj_with_bound_check
    b art_quick_throw_null_pointer_exception
END art_quick_aput_obj_with_null_and_bound_check

ENTRY art_quick_aput_obj_with_bound_check
    ldr w3, [x0, #MIRROR_ARRAY_LENGTH_OFFSET]
    cmp w3, w1
    bhi art_quick_aput_obj
    mov x0, x1
    mov x1, x3
    b art_quick_throw_array_bounds
END art_quick_aput_obj_with_bound_check

#ifdef USE_READ_BARRIER
    .extern artReadBarrierSlow
#endif
ENTRY art_quick_aput_obj
    cbz x2, .Ldo_aput_null
    READ_BARRIER x3, w3, x0, x3, w3, MIRROR_OBJECT_CLASS_OFFSET, 0  // Heap reference = 32b
                                                                    // This also zero-extends to x3
    READ_BARRIER x3, w3, x3, x4, w4, MIRROR_CLASS_COMPONENT_TYPE_OFFSET, 1 // Heap reference = 32b
    // This also zero-extends to x3
    READ_BARRIER x4, w4, x2, x4, w4, MIRROR_OBJECT_CLASS_OFFSET, 2  // Heap reference = 32b
                                                                    // This also zero-extends to x4
    cmp w3, w4  // value's type == array's component type - trivial assignability
    bne .Lcheck_assignability
.Ldo_aput:
    add x3, x0, #MIRROR_OBJECT_ARRAY_DATA_OFFSET
                                                         // "Compress" = do nothing
    POISON_HEAP_REF w2
    str w2, [x3, x1, lsl #2]                             // Heap reference = 32b
    ldr x3, [xSELF, #THREAD_CARD_TABLE_OFFSET]
    lsr x0, x0, #7
    strb w3, [x3, x0]
    ret
.Ldo_aput_null:
    add x3, x0, #MIRROR_OBJECT_ARRAY_DATA_OFFSET
                                                         // "Compress" = do nothing
    str w2, [x3, x1, lsl #2]                             // Heap reference = 32b
    ret
.Lcheck_assignability:
    // Store arguments and link register
    SAVE_TWO_REGS_INCREASE_FRAME x0, x1, 32
    SAVE_TWO_REGS x2, xLR, 16

    // Call runtime code
    mov x0, x3              // Heap reference, 32b, "uncompress" = do nothing, already zero-extended
    mov x1, x4              // Heap reference, 32b, "uncompress" = do nothing, already zero-extended
    bl artIsAssignableFromCode

    // Check for exception
    cbz x0, .Lthrow_array_store_exception

    // Restore
    .cfi_remember_state
    RESTORE_TWO_REGS x2, xLR, 16
    RESTORE_TWO_REGS_DECREASE_FRAME x0, x1, 32

    add x3, x0, #MIRROR_OBJECT_ARRAY_DATA_OFFSET
                                                          // "Compress" = do nothing
    POISON_HEAP_REF w2
    str w2, [x3, x1, lsl #2]                              // Heap reference = 32b
    ldr x3, [xSELF, #THREAD_CARD_TABLE_OFFSET]
    lsr x0, x0, #7
    strb w3, [x3, x0]
    ret
    .cfi_restore_state            // Reset unwind info so following code unwinds.
.Lthrow_array_store_exception:
    RESTORE_TWO_REGS x2, xLR, 16
    RESTORE_TWO_REGS_DECREASE_FRAME x0, x1, 32

    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    mov x1, x2                    // Pass value.
    mov x2, xSELF                 // Pass Thread::Current.
    b artThrowArrayStoreException // (Object*, Object*, Thread*).
    brk 0                         // Unreached.
END art_quick_aput_obj

// Macro to facilitate adding new allocation entrypoints.
.macro ONE_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    mov    x1, xSELF                  // pass Thread::Current
    bl     \entrypoint                // (uint32_t type_idx, Method* method, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

// Macro to facilitate adding new allocation entrypoints.
.macro TWO_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    mov    x2, xSELF                  // pass Thread::Current
    bl     \entrypoint                // (uint32_t type_idx, Method* method, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

// Macro to facilitate adding new allocation entrypoints.
.macro THREE_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    mov    x3, xSELF                  // pass Thread::Current
    bl     \entrypoint
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

// Macro to facilitate adding new allocation entrypoints.
.macro FOUR_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    mov    x4, xSELF                  // pass Thread::Current
    bl     \entrypoint                //
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
    DELIVER_PENDING_EXCEPTION
END \name
.endm

// Macros taking opportunity of code similarities for downcalls with referrer.
.macro ONE_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    ldr    x1, [sp, #FRAME_SIZE_SAVE_REFS_ONLY] // Load referrer
    mov    x2, xSELF                  // pass Thread::Current
    bl     \entrypoint                // (uint32_t type_idx, Method* method, Thread*, SP)
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

.macro TWO_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    ldr    x2, [sp, #FRAME_SIZE_SAVE_REFS_ONLY] // Load referrer
    mov    x3, xSELF                  // pass Thread::Current
    bl     \entrypoint
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

.macro THREE_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    ldr    x3, [sp, #FRAME_SIZE_SAVE_REFS_ONLY] // Load referrer
    mov    x4, xSELF                  // pass Thread::Current
    bl     \entrypoint
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm

.macro RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
    cbz w0, 1f                 // result zero branch over
    ret                        // return
1:
    DELIVER_PENDING_EXCEPTION
.endm

    /*
     * Entry from managed code that calls artHandleFillArrayDataFromCode and delivers exception on
     * failure.
     */
TWO_ARG_REF_DOWNCALL art_quick_handle_fill_data, artHandleFillArrayDataFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER

    /*
     * Entry from managed code when uninitialized static storage, this stub will run the class
     * initializer and deliver the exception on error. On success the static storage base is
     * returned.
     */
ONE_ARG_DOWNCALL art_quick_initialize_static_storage, artInitializeStaticStorageFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

ONE_ARG_DOWNCALL art_quick_initialize_type, artInitializeTypeFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
ONE_ARG_DOWNCALL art_quick_initialize_type_and_verify_access, artInitializeTypeAndVerifyAccessFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

ONE_ARG_REF_DOWNCALL art_quick_get_boolean_static, artGetBooleanStaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get_byte_static, artGetByteStaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get_char_static, artGetCharStaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get_short_static, artGetShortStaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get32_static, artGet32StaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get64_static, artGet64StaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
ONE_ARG_REF_DOWNCALL art_quick_get_obj_static, artGetObjStaticFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1

TWO_ARG_REF_DOWNCALL art_quick_get_boolean_instance, artGetBooleanInstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get_byte_instance, artGetByteInstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get_char_instance, artGetCharInstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get_short_instance, artGetShortInstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get32_instance, artGet32InstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get64_instance, artGet64InstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1
TWO_ARG_REF_DOWNCALL art_quick_get_obj_instance, artGetObjInstanceFromCode, RETURN_OR_DELIVER_PENDING_EXCEPTION_X1

TWO_ARG_REF_DOWNCALL art_quick_set8_static, artSet8StaticFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
TWO_ARG_REF_DOWNCALL art_quick_set16_static, artSet16StaticFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
TWO_ARG_REF_DOWNCALL art_quick_set32_static, artSet32StaticFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
TWO_ARG_REF_DOWNCALL art_quick_set_obj_static, artSetObjStaticFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER

THREE_ARG_REF_DOWNCALL art_quick_set8_instance, artSet8InstanceFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
THREE_ARG_REF_DOWNCALL art_quick_set16_instance, artSet16InstanceFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
THREE_ARG_REF_DOWNCALL art_quick_set32_instance, artSet32InstanceFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
THREE_ARG_REF_DOWNCALL art_quick_set64_instance, artSet64InstanceFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER
THREE_ARG_REF_DOWNCALL art_quick_set_obj_instance, artSetObjInstanceFromCode, RETURN_IF_W0_IS_ZERO_OR_DELIVER

// This is separated out as the argument order is different.
    .extern artSet64StaticFromCode
ENTRY art_quick_set64_static
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    ldr    x1, [sp, #FRAME_SIZE_SAVE_REFS_ONLY] // Load referrer
                                      // x2 contains the parameter
    mov    x3, xSELF                  // pass Thread::Current
    bl     artSet64StaticFromCode
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_W0_IS_ZERO_OR_DELIVER
END art_quick_set64_static

    /*
     * Entry from managed code to resolve a string, this stub will
     * check the dex cache for a matching string (the fast path), and if not found,
     * it will allocate a String and deliver an exception on error.
     * On success the String is returned. R0 holds the string index.
     */

ENTRY art_quick_resolve_string
    ldr   x1, [sp]                                               // load referrer
    ldr   w2, [x1, #ART_METHOD_DECLARING_CLASS_OFFSET]           // load declaring class
    ldr   x1, [x2, #DECLARING_CLASS_DEX_CACHE_STRINGS_OFFSET]    // load string dex cache
    ubfx  x2, x0, #0, #STRING_DEX_CACHE_HASH_BITS                // get masked string index into x2
    ldr   x2, [x1, x2, lsl #STRING_DEX_CACHE_ELEMENT_SIZE_SHIFT]  // load dex cache pair into x2
    cmp   x0, x2, lsr #32                                         // compare against upper 32 bits
    bne   .Lart_quick_resolve_string_slow_path
    ubfx  x0, x2, #0, #32                                        // extract lower 32 bits into x0
#ifdef USE_READ_BARRIER
    // Most common case: GC is not marking.
    ldr    w3, [xSELF, #THREAD_IS_GC_MARKING_OFFSET]
    cbnz   x3, .Lart_quick_resolve_string_marking
#endif
    ret

// Slow path case, the index did not match.
.Lart_quick_resolve_string_slow_path:
    SETUP_SAVE_REFS_ONLY_FRAME                      // save callee saves in case of GC
    mov   x1, xSELF                                 // pass Thread::Current
    bl    artResolveStringFromCode                  // (int32_t string_idx, Thread* self)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

// GC is marking case, need to check the mark bit.
.Lart_quick_resolve_string_marking:
    ldr   x3, [x0, #MIRROR_OBJECT_LOCK_WORD_OFFSET]
    tbnz  x3, #LOCK_WORD_MARK_BIT_SHIFT, .Lart_quick_resolve_string_no_rb
    // Save LR so that we can return, also x1 for alignment purposes.
    SAVE_TWO_REGS_INCREASE_FRAME x1, xLR, 16        // Save x1, LR.
    bl     artReadBarrierMark                       // Get the marked string back.
    RESTORE_TWO_REGS_DECREASE_FRAME x1, xLR, 16     // Restore registers.
.Lart_quick_resolve_string_no_rb:
    ret

END art_quick_resolve_string

// Generate the allocation entrypoints for each allocator.
GENERATE_ALLOC_ENTRYPOINTS_FOR_NON_REGION_TLAB_ALLOCATORS
// Comment out allocators that have arm64 specific asm.
// GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT(_region_tlab, RegionTLAB) implemented in asm
// GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_RESOLVED(_region_tlab, RegionTLAB)
// GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_INITIALIZED(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_WITH_ACCESS_CHECK(_region_tlab, RegionTLAB)
// GENERATE_ALLOC_ENTRYPOINTS_ALLOC_ARRAY(_region_tlab, RegionTLAB) implemented in asm
// GENERATE_ALLOC_ENTRYPOINTS_ALLOC_ARRAY_RESOLVED(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_ARRAY_WITH_ACCESS_CHECK(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_CHECK_AND_ALLOC_ARRAY(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_CHECK_AND_ALLOC_ARRAY_WITH_ACCESS_CHECK(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_STRING_FROM_BYTES(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_STRING_FROM_CHARS(_region_tlab, RegionTLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_STRING_FROM_STRING(_region_tlab, RegionTLAB)

// A hand-written override for GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT(_rosalloc, RosAlloc).
ENTRY art_quick_alloc_object_rosalloc
    // Fast path rosalloc allocation.
    // x0: type_idx/return value, x1: ArtMethod*, xSELF(x19): Thread::Current
    // x2-x7: free.
    ldr    x2, [x1, #ART_METHOD_DEX_CACHE_TYPES_OFFSET_64]    // Load dex cache resolved types array
                                                              // Load the class (x2)
    ldr    w2, [x2, x0, lsl #COMPRESSED_REFERENCE_SIZE_SHIFT]
    cbz    x2, .Lart_quick_alloc_object_rosalloc_slow_path    // Check null class
    ldr    x3, [xSELF, #THREAD_LOCAL_ALLOC_STACK_TOP_OFFSET]  // Check if the thread local
                                                              // allocation stack has room.
                                                              // ldp won't work due to large offset.
    ldr    x4, [xSELF, #THREAD_LOCAL_ALLOC_STACK_END_OFFSET]
    cmp    x3, x4
    bhs    .Lart_quick_alloc_object_rosalloc_slow_path
    ldr    w3, [x2, #MIRROR_CLASS_OBJECT_SIZE_ALLOC_FAST_PATH_OFFSET]  // Load the object size (x3)
    cmp    x3, #ROSALLOC_MAX_THREAD_LOCAL_BRACKET_SIZE        // Check if the size is for a thread
                                                              // local allocation. Also does the
                                                              // finalizable and initialization
                                                              // checks.
    bhs    .Lart_quick_alloc_object_rosalloc_slow_path
                                                              // Compute the rosalloc bracket index
                                                              // from the size. Since the size is
                                                              // already aligned we can combine the
                                                              // two shifts together.
    add    x4, xSELF, x3, lsr #(ROSALLOC_BRACKET_QUANTUM_SIZE_SHIFT - POINTER_SIZE_SHIFT)
                                                              // Subtract pointer size since ther
                                                              // are no runs for 0 byte allocations
                                                              // and the size is already aligned.
    ldr    x4, [x4, #(THREAD_ROSALLOC_RUNS_OFFSET - __SIZEOF_POINTER__)]
                                                              // Load the free list head (x3). This
                                                              // will be the return val.
    ldr    x3, [x4, #(ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_HEAD_OFFSET)]
    cbz    x3, .Lart_quick_alloc_object_rosalloc_slow_path
    // "Point of no slow path". Won't go to the slow path from here on. OK to clobber x0 and x1.
    ldr    x1, [x3, #ROSALLOC_SLOT_NEXT_OFFSET]               // Load the next pointer of the head
                                                              // and update the list head with the
                                                              // next pointer.
    str    x1, [x4, #(ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_HEAD_OFFSET)]
                                                              // Store the class pointer in the
                                                              // header. This also overwrites the
                                                              // next pointer. The offsets are
                                                              // asserted to match.
#if ROSALLOC_SLOT_NEXT_OFFSET != MIRROR_OBJECT_CLASS_OFFSET
#error "Class pointer needs to overwrite next pointer."
#endif
    POISON_HEAP_REF w2
    str    w2, [x3, #MIRROR_OBJECT_CLASS_OFFSET]
                                                              // Fence. This is "ish" not "ishst" so
                                                              // that it also ensures ordering of
                                                              // the object size load with respect
                                                              // to later accesses to the class
                                                              // object. Alternatively we could use
                                                              // "ishst" if we use load-acquire for
                                                              // the class status load.
                                                              // Needs to be done before pushing on
                                                              // allocation since Heap::VisitObjects
                                                              // relies on seeing the class pointer.
                                                              // b/28790624
    dmb    ish
                                                              // Push the new object onto the thread
                                                              // local allocation stack and
                                                              // increment the thread local
                                                              // allocation stack top.
    ldr    x1, [xSELF, #THREAD_LOCAL_ALLOC_STACK_TOP_OFFSET]
    str    w3, [x1], #COMPRESSED_REFERENCE_SIZE               // (Increment x1 as a side effect.)
    str    x1, [xSELF, #THREAD_LOCAL_ALLOC_STACK_TOP_OFFSET]
                                                              // Decrement the size of the free list
    ldr    w1, [x4, #(ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_SIZE_OFFSET)]
    sub    x1, x1, #1
                                                              // TODO: consider combining this store
                                                              // and the list head store above using
                                                              // strd.
    str    w1, [x4, #(ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_SIZE_OFFSET)]

    mov    x0, x3                                             // Set the return value and return.
    ret
.Lart_quick_alloc_object_rosalloc_slow_path:
    SETUP_SAVE_REFS_ONLY_FRAME             // save callee saves in case of GC
    mov    x2, xSELF                       // pass Thread::Current
    bl     artAllocObjectFromCodeRosAlloc  // (uint32_t type_idx, Method* method, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
END art_quick_alloc_object_rosalloc


// The common fast path code for art_quick_alloc_array_region_tlab.
.macro ALLOC_ARRAY_TLAB_FAST_PATH slowPathLabel, xClass, wClass, xCount, wCount, xTemp0, wTemp0, xTemp1, wTemp1, xTemp2, wTemp2
    // Check null class
    cbz    \wClass, \slowPathLabel
    ALLOC_ARRAY_TLAB_FAST_PATH_RESOLVED \slowPathLabel, \xClass, \wClass, \xCount, \wCount, \xTemp0, \wTemp0, \xTemp1, \wTemp1, \xTemp2, \wTemp2
.endm

// The common fast path code for art_quick_alloc_array_region_tlab.
.macro ALLOC_ARRAY_TLAB_FAST_PATH_RESOLVED slowPathLabel, xClass, wClass, xCount, wCount, xTemp0, wTemp0, xTemp1, wTemp1, xTemp2, wTemp2
    // Array classes are never finalizable or uninitialized, no need to check.
    ldr    \wTemp0, [\xClass, #MIRROR_CLASS_COMPONENT_TYPE_OFFSET] // Load component type
    UNPOISON_HEAP_REF \wTemp0
    ldr    \wTemp0, [\xTemp0, #MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET]
    lsr    \xTemp0, \xTemp0, #PRIMITIVE_TYPE_SIZE_SHIFT_SHIFT // Component size shift is in high 16
                                                              // bits.
                                                              // xCount is holding a 32 bit value,
                                                              // it can not overflow.
    lsl    \xTemp1, \xCount, \xTemp0                          // Calculate data size
    // Add array data offset and alignment.
    add    \xTemp1, \xTemp1, #(MIRROR_INT_ARRAY_DATA_OFFSET + OBJECT_ALIGNMENT_MASK)
#if MIRROR_LONG_ARRAY_DATA_OFFSET != MIRROR_INT_ARRAY_DATA_OFFSET + 4
#error Long array data offset must be 4 greater than int array data offset.
#endif

    add    \xTemp0, \xTemp0, #1                               // Add 4 to the length only if the
                                                              // component size shift is 3
                                                              // (for 64 bit alignment).
    and    \xTemp0, \xTemp0, #4
    add    \xTemp1, \xTemp1, \xTemp0
    and    \xTemp1, \xTemp1, #OBJECT_ALIGNMENT_MASK_TOGGLED64 // Apply alignemnt mask
                                                              // (addr + 7) & ~7. The mask must
                                                              // be 64 bits to keep high bits in
                                                              // case of overflow.
    // Negative sized arrays are handled here since xCount holds a zero extended 32 bit value.
    // Negative ints become large 64 bit unsigned ints which will always be larger than max signed
    // 32 bit int. Since the max shift for arrays is 3, it can not become a negative 64 bit int.
    cmp    \xTemp1, #MIN_LARGE_OBJECT_THRESHOLD               // Possibly a large object, go slow
    bhs    \slowPathLabel                                     // path.

    ldr    \xTemp0, [xSELF, #THREAD_LOCAL_POS_OFFSET]         // Check tlab for space, note that
                                                              // we use (end - begin) to handle
                                                              // negative size arrays. It is
                                                              // assumed that a negative size will
                                                              // always be greater unsigned than
                                                              // region size.
    ldr    \xTemp2, [xSELF, #THREAD_LOCAL_END_OFFSET]
    sub    \xTemp2, \xTemp2, \xTemp0
    cmp    \xTemp1, \xTemp2
    bhi    \slowPathLabel
    // "Point of no slow path". Won't go to the slow path from here on. OK to clobber x0 and x1.
                                                              // Move old thread_local_pos to x0
                                                              // for the return value.
    mov    x0, \xTemp0
    add    \xTemp0, \xTemp0, \xTemp1
    str    \xTemp0, [xSELF, #THREAD_LOCAL_POS_OFFSET]         // Store new thread_local_pos.
    ldr    \xTemp0, [xSELF, #THREAD_LOCAL_OBJECTS_OFFSET]     // Increment thread_local_objects.
    add    \xTemp0, \xTemp0, #1
    str    \xTemp0, [xSELF, #THREAD_LOCAL_OBJECTS_OFFSET]
    POISON_HEAP_REF \wClass
    str    \wClass, [x0, #MIRROR_OBJECT_CLASS_OFFSET]         // Store the class pointer.
    str    \wCount, [x0, #MIRROR_ARRAY_LENGTH_OFFSET]         // Store the array length.
                                                              // Fence.
    dmb    ishst
    ret
.endm

// The common fast path code for art_quick_alloc_object_tlab and art_quick_alloc_object_region_tlab.
//
// x0: type_idx/return value, x1: ArtMethod*, x2: Class*, xSELF(x19): Thread::Current
// x3-x7: free.
// Need to preserve x0 and x1 to the slow path.
.macro ALLOC_OBJECT_TLAB_FAST_PATH slowPathLabel
    cbz    x2, \slowPathLabel                                 // Check null class
    ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED \slowPathLabel
.endm

// TODO: delete ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED since it is the same as
// ALLOC_OBJECT_TLAB_FAST_PATH_INITIALIZED.
.macro ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED slowPathLabel
    ALLOC_OBJECT_TLAB_FAST_PATH_INITIALIZED \slowPathLabel
.endm

.macro ALLOC_OBJECT_TLAB_FAST_PATH_INITIALIZED slowPathLabel
    ldr    x4, [xSELF, #THREAD_LOCAL_POS_OFFSET]
    ldr    x5, [xSELF, #THREAD_LOCAL_END_OFFSET]
    ldr    w7, [x2, #MIRROR_CLASS_OBJECT_SIZE_ALLOC_FAST_PATH_OFFSET]  // Load the object size (x7).
    add    x6, x4, x7                                         // Add object size to tlab pos.
    cmp    x6, x5                                             // Check if it fits, overflow works
                                                              // since the tlab pos and end are 32
                                                              // bit values.
    bhi    \slowPathLabel
    // "Point of no slow path". Won't go to the slow path from here on. OK to clobber x0 and x1.
    mov    x0, x4
    str    x6, [xSELF, #THREAD_LOCAL_POS_OFFSET]              // Store new thread_local_pos.
    ldr    x5, [xSELF, #THREAD_LOCAL_OBJECTS_OFFSET]          // Increment thread_local_objects.
    add    x5, x5, #1
    str    x5, [xSELF, #THREAD_LOCAL_OBJECTS_OFFSET]
    POISON_HEAP_REF w2
    str    w2, [x0, #MIRROR_OBJECT_CLASS_OFFSET]              // Store the class pointer.
                                                              // Fence. This is "ish" not "ishst" so
                                                              // that the code after this allocation
                                                              // site will see the right values in
                                                              // the fields of the class.
                                                              // Alternatively we could use "ishst"
                                                              // if we use load-acquire for the
                                                              // object size load.)
    dmb    ish
    ret
.endm

// A hand-written override for GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT(_tlab, TLAB).
ENTRY art_quick_alloc_object_tlab
    // Fast path tlab allocation.
    // x0: type_idx/return value, x1: ArtMethod*, xSELF(x19): Thread::Current
    // x2-x7: free.
#if defined(USE_READ_BARRIER)
    mvn    x0, xzr                                            // Read barrier not supported here.
    ret                                                       // Return -1.
#endif
    ldr    x2, [x1, #ART_METHOD_DEX_CACHE_TYPES_OFFSET_64]    // Load dex cache resolved types array
                                                              // Load the class (x2)
    ldr    w2, [x2, x0, lsl #COMPRESSED_REFERENCE_SIZE_SHIFT]
    ALLOC_OBJECT_TLAB_FAST_PATH .Lart_quick_alloc_object_tlab_slow_path
.Lart_quick_alloc_object_tlab_slow_path:
    SETUP_SAVE_REFS_ONLY_FRAME           // Save callee saves in case of GC.
    mov    x2, xSELF                     // Pass Thread::Current.
    bl     artAllocObjectFromCodeTLAB    // (uint32_t type_idx, Method* method, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
END art_quick_alloc_object_tlab

// The common code for art_quick_alloc_object_*region_tlab
.macro GENERATE_ALLOC_OBJECT_REGION_TLAB name, entrypoint, fast_path, is_resolved, read_barrier
ENTRY \name
    // Fast path region tlab allocation.
    // x0: type_idx/resolved class/return value, x1: ArtMethod*, xSELF(x19): Thread::Current
    // If is_resolved is 1 then x0 is the resolved type, otherwise it is the index.
    // x2-x7: free.
#if !defined(USE_READ_BARRIER)
    mvn    x0, xzr                                            // Read barrier must be enabled here.
    ret                                                       // Return -1.
#endif
.if \is_resolved
    mov    x2, x0 // class is actually stored in x0 already
.else
    ldr    x2, [x1, #ART_METHOD_DEX_CACHE_TYPES_OFFSET_64]    // Load dex cache resolved types array
                                                              // Load the class (x2)
    ldr    w2, [x2, x0, lsl #COMPRESSED_REFERENCE_SIZE_SHIFT]
    // If the class is null, go slow path. The check is required to read the lock word.
    cbz    w2, .Lslow_path\name
.endif
.if \read_barrier
    // Most common case: GC is not marking.
    ldr    w3, [xSELF, #THREAD_IS_GC_MARKING_OFFSET]
    cbnz   x3, .Lmarking\name
.endif
.Ldo_allocation\name:
    \fast_path .Lslow_path\name
.Lmarking\name:
.if \read_barrier
    // GC is marking, check the lock word of the class for the mark bit.
    // Class is not null, check mark bit in lock word.
    ldr    w3, [x2, #MIRROR_OBJECT_LOCK_WORD_OFFSET]
    // If the bit is not zero, do the allocation.
    tbnz    w3, #LOCK_WORD_MARK_BIT_SHIFT, .Ldo_allocation\name
                                                              // The read barrier slow path. Mark
                                                              // the class.
    SAVE_TWO_REGS_INCREASE_FRAME x0, x1, 32                   // Save registers (x0, x1, lr).
    SAVE_REG xLR, 24                                          // Align sp by 16 bytes.
    mov    x0, x2                                             // Pass the class as the first param.
    bl     artReadBarrierMark
    mov    x2, x0                                             // Get the (marked) class back.
    RESTORE_REG xLR, 24
    RESTORE_TWO_REGS_DECREASE_FRAME x0, x1, 32                // Restore registers.
    b      .Ldo_allocation\name
.endif
.Lslow_path\name:
    SETUP_SAVE_REFS_ONLY_FRAME                 // Save callee saves in case of GC.
    mov    x2, xSELF                           // Pass Thread::Current.
    bl     \entrypoint                         // (uint32_t type_idx, Method* method, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
END \name
.endm

// Use ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED since the null check is already done in GENERATE_ALLOC_OBJECT_TLAB.
GENERATE_ALLOC_OBJECT_REGION_TLAB art_quick_alloc_object_region_tlab, artAllocObjectFromCodeRegionTLAB, ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED, 0, 1
// No read barrier for the resolved or initialized cases since the caller is responsible for the
// read barrier due to the to-space invariant.
GENERATE_ALLOC_OBJECT_REGION_TLAB art_quick_alloc_object_resolved_region_tlab, artAllocObjectFromCodeResolvedRegionTLAB, ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED, 1, 0
GENERATE_ALLOC_OBJECT_REGION_TLAB art_quick_alloc_object_initialized_region_tlab, artAllocObjectFromCodeInitializedRegionTLAB, ALLOC_OBJECT_TLAB_FAST_PATH_INITIALIZED, 1, 0

// TODO: We could use this macro for the normal tlab allocator too.

// The common code for art_quick_alloc_array_*region_tlab
.macro GENERATE_ALLOC_ARRAY_REGION_TLAB name, entrypoint, fast_path, is_resolved
ENTRY \name
    // Fast path array allocation for region tlab allocation.
    // x0: uint32_t type_idx
    // x1: int32_t component_count
    // x2: ArtMethod* method
    // x3-x7: free.
#if !defined(USE_READ_BARRIER)
    mvn    x0, xzr                                            // Read barrier must be enabled here.
    ret                                                       // Return -1.
#endif
.if \is_resolved
    mov    x3, x0
    // If already resolved, class is stored in x0
.else
    ldr    x3, [x2, #ART_METHOD_DEX_CACHE_TYPES_OFFSET_64]    // Load dex cache resolved types array
                                                              // Load the class (x2)
    ldr    w3, [x3, x0, lsl #COMPRESSED_REFERENCE_SIZE_SHIFT]
.endif
    // Most common case: GC is not marking.
    ldr    w4, [xSELF, #THREAD_IS_GC_MARKING_OFFSET]
    cbnz   x4, .Lmarking\name
.Ldo_allocation\name:
    \fast_path .Lslow_path\name, x3, w3, x1, w1, x4, w4, x5, w5, x6, w6
.Lmarking\name:
    // GC is marking, check the lock word of the class for the mark bit.
    // If the class is null, go slow path. The check is required to read the lock word.
    cbz    w3, .Lslow_path\name
    // Class is not null, check mark bit in lock word.
    ldr    w4, [x3, #MIRROR_OBJECT_LOCK_WORD_OFFSET]
    // If the bit is not zero, do the allocation.
    tbnz   w4, #LOCK_WORD_MARK_BIT_SHIFT, .Ldo_allocation\name
                                                              // The read barrier slow path. Mark
                                                              // the class.
    stp    x0, x1, [sp, #-32]!                                // Save registers (x0, x1, x2, lr).
    stp    x2, xLR, [sp, #16]
    mov    x0, x3                                             // Pass the class as the first param.
    bl     artReadBarrierMark
    mov    x3, x0                                             // Get the (marked) class back.
    ldp    x2, xLR, [sp, #16]
    ldp    x0, x1, [sp], #32                                  // Restore registers.
    b      .Ldo_allocation\name
.Lslow_path\name:
    // x0: uint32_t type_idx / mirror::Class* klass (if resolved)
    // x1: int32_t component_count
    // x2: ArtMethod* method
    // x3: Thread* self
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case of GC
    mov    x3, xSELF                  // pass Thread::Current
    bl     \entrypoint
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
END \name
.endm

GENERATE_ALLOC_ARRAY_REGION_TLAB art_quick_alloc_array_region_tlab, artAllocArrayFromCodeRegionTLAB, ALLOC_ARRAY_TLAB_FAST_PATH, 0
// TODO: art_quick_alloc_array_resolved_region_tlab seems to not get called. Investigate compiler.
GENERATE_ALLOC_ARRAY_REGION_TLAB art_quick_alloc_array_resolved_region_tlab, artAllocArrayFromCodeResolvedRegionTLAB, ALLOC_ARRAY_TLAB_FAST_PATH_RESOLVED, 1

    /*
     * Called by managed code when the thread has been asked to suspend.
     */
    .extern artTestSuspendFromCode
ENTRY art_quick_test_suspend
    SETUP_SAVE_EVERYTHING_FRAME               // save callee saves for stack crawl
    mov    x0, xSELF
    bl     artTestSuspendFromCode             // (Thread*)
    RESTORE_SAVE_EVERYTHING_FRAME
    ret
END art_quick_test_suspend

ENTRY art_quick_implicit_suspend
    mov    x0, xSELF
    SETUP_SAVE_REFS_ONLY_FRAME                // save callee saves for stack crawl
    bl     artTestSuspendFromCode             // (Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME_AND_RETURN
END art_quick_implicit_suspend

     /*
     * Called by managed code that is attempting to call a method on a proxy class. On entry
     * x0 holds the proxy method and x1 holds the receiver; The frame size of the invoked proxy
     * method agrees with a ref and args callee save frame.
     */
     .extern artQuickProxyInvokeHandler
ENTRY art_quick_proxy_invoke_handler
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0
    mov     x2, xSELF                   // pass Thread::Current
    mov     x3, sp                      // pass SP
    bl      artQuickProxyInvokeHandler  // (Method* proxy method, receiver, Thread*, SP)
    ldr     x2, [xSELF, THREAD_EXCEPTION_OFFSET]
    cbnz    x2, .Lexception_in_proxy    // success if no exception is pending
    RESTORE_SAVE_REFS_AND_ARGS_FRAME    // Restore frame
    fmov    d0, x0                      // Store result in d0 in case it was float or double
    ret                                 // return on success
.Lexception_in_proxy:
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_proxy_invoke_handler

    /*
     * Called to resolve an imt conflict.
     * x0 is the conflict ArtMethod.
     * xIP1 is a hidden argument that holds the target interface method's dex method index.
     *
     * Note that this stub writes to xIP0, xIP1, and x0.
     */
    .extern artInvokeInterfaceTrampoline
ENTRY art_quick_imt_conflict_trampoline
    ldr xIP0, [sp, #0]  // Load referrer
    ldr xIP0, [xIP0, #ART_METHOD_DEX_CACHE_METHODS_OFFSET_64]   // Load dex cache methods array
    ldr xIP0, [xIP0, xIP1, lsl #POINTER_SIZE_SHIFT]  // Load interface method
    ldr xIP1, [x0, #ART_METHOD_JNI_OFFSET_64]  // Load ImtConflictTable
    ldr x0, [xIP1]  // Load first entry in ImtConflictTable.
.Limt_table_iterate:
    cmp x0, xIP0
    // Branch if found. Benchmarks have shown doing a branch here is better.
    beq .Limt_table_found
    // If the entry is null, the interface method is not in the ImtConflictTable.
    cbz x0, .Lconflict_trampoline
    // Iterate over the entries of the ImtConflictTable.
    ldr x0, [xIP1, #(2 * __SIZEOF_POINTER__)]!
    b .Limt_table_iterate
.Limt_table_found:
    // We successfully hit an entry in the table. Load the target method
    // and jump to it.
    ldr x0, [xIP1, #__SIZEOF_POINTER__]
    ldr xIP0, [x0, #ART_METHOD_QUICK_CODE_OFFSET_64]
    br xIP0
.Lconflict_trampoline:
    // Call the runtime stub to populate the ImtConflictTable and jump to the
    // resolved method.
    INVOKE_TRAMPOLINE_BODY artInvokeInterfaceTrampoline
END art_quick_imt_conflict_trampoline

ENTRY art_quick_resolution_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    mov x2, xSELF
    mov x3, sp
    bl artQuickResolutionTrampoline  // (called, receiver, Thread*, SP)
    cbz x0, 1f
    mov xIP0, x0            // Remember returned code pointer in xIP0.
    ldr x0, [sp, #0]        // artQuickResolutionTrampoline puts called method in *SP.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    br xIP0
1:
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_resolution_trampoline

/*
 * Generic JNI frame layout:
 *
 * #-------------------#
 * |                   |
 * | caller method...  |
 * #-------------------#    <--- SP on entry
 * | Return X30/LR     |
 * | X29/FP            |    callee save
 * | X28               |    callee save
 * | X27               |    callee save
 * | X26               |    callee save
 * | X25               |    callee save
 * | X24               |    callee save
 * | X23               |    callee save
 * | X22               |    callee save
 * | X21               |    callee save
 * | X20               |    callee save
 * | X19               |    callee save
 * | X7                |    arg7
 * | X6                |    arg6
 * | X5                |    arg5
 * | X4                |    arg4
 * | X3                |    arg3
 * | X2                |    arg2
 * | X1                |    arg1
 * | D7                |    float arg 8
 * | D6                |    float arg 7
 * | D5                |    float arg 6
 * | D4                |    float arg 5
 * | D3                |    float arg 4
 * | D2                |    float arg 3
 * | D1                |    float arg 2
 * | D0                |    float arg 1
 * | Method*           | <- X0
 * #-------------------#
 * | local ref cookie  | // 4B
 * | handle scope size | // 4B
 * #-------------------#
 * | JNI Call Stack    |
 * #-------------------#    <--- SP on native call
 * |                   |
 * | Stack for Regs    |    The trampoline assembly will pop these values
 * |                   |    into registers for native call
 * #-------------------#
 * | Native code ptr   |
 * #-------------------#
 * | Free scratch      |
 * #-------------------#
 * | Ptr to (1)        |    <--- SP
 * #-------------------#
 */
    /*
     * Called to do a generic JNI down-call
     */
ENTRY art_quick_generic_jni_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_X0

    // Save SP , so we can have static CFI info.
    mov x28, sp
    .cfi_def_cfa_register x28

    // This looks the same, but is different: this will be updated to point to the bottom
    // of the frame when the handle scope is inserted.
    mov xFP, sp

    mov xIP0, #5120
    sub sp, sp, xIP0

    // prepare for artQuickGenericJniTrampoline call
    // (Thread*,  SP)
    //    x0      x1   <= C calling convention
    //   xSELF    xFP  <= where they are

    mov x0, xSELF   // Thread*
    mov x1, xFP
    bl artQuickGenericJniTrampoline  // (Thread*, sp)

    // The C call will have registered the complete save-frame on success.
    // The result of the call is:
    // x0: pointer to native code, 0 on error.
    // x1: pointer to the bottom of the used area of the alloca, can restore stack till there.

    // Check for error = 0.
    cbz x0, .Lexception_in_native

    // Release part of the alloca.
    mov sp, x1

    // Save the code pointer
    mov xIP0, x0

    // Load parameters from frame into registers.
    // TODO Check with artQuickGenericJniTrampoline.
    //      Also, check again APPCS64 - the stack arguments are interleaved.
    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]

    ldp d0, d1, [sp, #64]
    ldp d2, d3, [sp, #80]
    ldp d4, d5, [sp, #96]
    ldp d6, d7, [sp, #112]

    add sp, sp, #128

    blr xIP0        // native call.

    // result sign extension is handled in C code
    // prepare for artQuickGenericJniEndTrampoline call
    // (Thread*, result, result_f)
    //    x0       x1       x2        <= C calling convention
    mov x1, x0      // Result (from saved).
    mov x0, xSELF   // Thread register.
    fmov x2, d0     // d0 will contain floating point result, but needs to go into x2

    bl artQuickGenericJniEndTrampoline

    // Pending exceptions possible.
    ldr x2, [xSELF, THREAD_EXCEPTION_OFFSET]
    cbnz x2, .Lexception_in_native

    // Tear down the alloca.
    mov sp, x28
    .cfi_def_cfa_register sp

    // Tear down the callee-save frame.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    // store into fpr, for when it's a fpr return...
    fmov d0, x0
    ret

.Lexception_in_native:
    // Move to x1 then sp to please assembler.
    ldr x1, [xSELF, # THREAD_TOP_QUICK_FRAME_OFFSET]
    mov sp, x1
    .cfi_def_cfa_register sp
    # This will create a new save-all frame, required by the runtime.
    DELIVER_PENDING_EXCEPTION
END art_quick_generic_jni_trampoline

/*
 * Called to bridge from the quick to interpreter ABI. On entry the arguments match those
 * of a quick call:
 * x0 = method being called/to bridge to.
 * x1..x7, d0..d7 = arguments to that method.
 */
ENTRY art_quick_to_interpreter_bridge
    SETUP_SAVE_REFS_AND_ARGS_FRAME         // Set up frame and save arguments.

    //  x0 will contain mirror::ArtMethod* method.
    mov x1, xSELF                          // How to get Thread::Current() ???
    mov x2, sp

    // uint64_t artQuickToInterpreterBridge(mirror::ArtMethod* method, Thread* self,
    //                                      mirror::ArtMethod** sp)
    bl   artQuickToInterpreterBridge

    RESTORE_SAVE_REFS_AND_ARGS_FRAME       // TODO: no need to restore arguments in this case.

    fmov d0, x0

    RETURN_OR_DELIVER_PENDING_EXCEPTION
END art_quick_to_interpreter_bridge


//
// Instrumentation-related stubs
//
    .extern artInstrumentationMethodEntryFromCode
ENTRY art_quick_instrumentation_entry
    SETUP_SAVE_REFS_AND_ARGS_FRAME

    mov   x20, x0             // Preserve method reference in a callee-save.

    mov   x2, xSELF
    mov   x3, xLR
    bl    artInstrumentationMethodEntryFromCode  // (Method*, Object*, Thread*, LR)

    mov   xIP0, x0            // x0 = result of call.
    mov   x0, x20             // Reload method reference.

    RESTORE_SAVE_REFS_AND_ARGS_FRAME  // Note: will restore xSELF
    adr   xLR, art_quick_instrumentation_exit
    br    xIP0                // Tail-call method with lr set to art_quick_instrumentation_exit.
END art_quick_instrumentation_entry

    .extern artInstrumentationMethodExitFromCode
ENTRY art_quick_instrumentation_exit
    mov   xLR, #0             // Clobber LR for later checks.

    SETUP_SAVE_REFS_ONLY_FRAME

    // We need to save x0 and d0. We could use a callee-save from SETUP_REF_ONLY, but then
    // we would need to fully restore it. As there are a lot of callee-save registers, it seems
    // easier to have an extra small stack area.

    str x0, [sp, #-16]!       // Save integer result.
    .cfi_adjust_cfa_offset 16
    str d0,  [sp, #8]         // Save floating-point result.

    add   x1, sp, #16         // Pass SP.
    mov   x2, x0              // Pass integer result.
    fmov  x3, d0              // Pass floating-point result.
    mov   x0, xSELF           // Pass Thread.
    bl   artInstrumentationMethodExitFromCode    // (Thread*, SP, gpr_res, fpr_res)

    mov   xIP0, x0            // Return address from instrumentation call.
    mov   xLR, x1             // r1 is holding link register if we're to bounce to deoptimize

    ldr   d0, [sp, #8]        // Restore floating-point result.
    ldr   x0, [sp], #16       // Restore integer result, and drop stack area.
    .cfi_adjust_cfa_offset 16

    POP_SAVE_REFS_ONLY_FRAME

    br    xIP0                // Tail-call out.
END art_quick_instrumentation_exit

    /*
     * Instrumentation has requested that we deoptimize into the interpreter. The deoptimization
     * will long jump to the upcall with a special exception of -1.
     */
    .extern artDeoptimize
ENTRY art_quick_deoptimize
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    mov    x0, xSELF          // Pass thread.
    bl     artDeoptimize      // artDeoptimize(Thread*)
    brk 0
END art_quick_deoptimize

    /*
     * Compiled code has requested that we deoptimize into the interpreter. The deoptimization
     * will long jump to the upcall with a special exception of -1.
     */
    .extern artDeoptimizeFromCompiledCode
ENTRY art_quick_deoptimize_from_compiled_code
    SETUP_SAVE_EVERYTHING_FRAME
    mov    x0, xSELF                      // Pass thread.
    bl     artDeoptimizeFromCompiledCode  // artDeoptimizeFromCompiledCode(Thread*)
    brk 0
END art_quick_deoptimize_from_compiled_code


    /*
     * String's indexOf.
     *
     * TODO: Not very optimized.
     * On entry:
     *    x0:   string object (known non-null)
     *    w1:   char to match (known <= 0xFFFF)
     *    w2:   Starting offset in string data
     */
ENTRY art_quick_indexof
    ldr   w3, [x0, #MIRROR_STRING_COUNT_OFFSET]
    add   x0, x0, #MIRROR_STRING_VALUE_OFFSET

    /* Clamp start to [0..count] */
    cmp   w2, #0
    csel  w2, wzr, w2, lt
    cmp   w2, w3
    csel  w2, w3, w2, gt

    /* Save a copy to compute result */
    mov   x5, x0

    /* Build pointer to start of data to compare and pre-bias */
    add   x0, x0, x2, lsl #1
    sub   x0, x0, #2

    /* Compute iteration count */
    sub   w2, w3, w2

    /*
     * At this point we have:
     *  x0: start of the data to test
     *  w1: char to compare
     *  w2: iteration count
     *  x5: original start of string data
     */

    subs  w2, w2, #4
    b.lt  .Lindexof_remainder

.Lindexof_loop4:
    ldrh  w6, [x0, #2]!
    ldrh  w7, [x0, #2]!
    ldrh  wIP0, [x0, #2]!
    ldrh  wIP1, [x0, #2]!
    cmp   w6, w1
    b.eq  .Lmatch_0
    cmp   w7, w1
    b.eq  .Lmatch_1
    cmp   wIP0, w1
    b.eq  .Lmatch_2
    cmp   wIP1, w1
    b.eq  .Lmatch_3
    subs  w2, w2, #4
    b.ge  .Lindexof_loop4

.Lindexof_remainder:
    adds  w2, w2, #4
    b.eq  .Lindexof_nomatch

.Lindexof_loop1:
    ldrh  w6, [x0, #2]!
    cmp   w6, w1
    b.eq  .Lmatch_3
    subs  w2, w2, #1
    b.ne  .Lindexof_loop1

.Lindexof_nomatch:
    mov   x0, #-1
    ret

.Lmatch_0:
    sub   x0, x0, #6
    sub   x0, x0, x5
    asr   x0, x0, #1
    ret
.Lmatch_1:
    sub   x0, x0, #4
    sub   x0, x0, x5
    asr   x0, x0, #1
    ret
.Lmatch_2:
    sub   x0, x0, #2
    sub   x0, x0, x5
    asr   x0, x0, #1
    ret
.Lmatch_3:
    sub   x0, x0, x5
    asr   x0, x0, #1
    ret
END art_quick_indexof

    /*
     * Create a function `name` calling the ReadBarrier::Mark routine,
     * getting its argument and returning its result through W register
     * `wreg` (corresponding to X register `xreg`), saving and restoring
     * all caller-save registers.
     *
     * If `wreg` is different from `w0`, the generated function follows a
     * non-standard runtime calling convention:
     * - register `wreg` is used to pass the (sole) argument of this
     *   function (instead of W0);
     * - register `wreg` is used to return the result of this function
     *   (instead of W0);
     * - W0 is treated like a normal (non-argument) caller-save register;
     * - everything else is the same as in the standard runtime calling
     *   convention (e.g. standard callee-save registers are preserved).
     */
.macro READ_BARRIER_MARK_REG name, wreg, xreg
ENTRY \name
    // Reference is null, no work to do at all.
    cbz \wreg, .Lret_rb_\name
    /*
     * Allocate 46 stack slots * 8 = 368 bytes:
     * - 20 slots for core registers X0-X19
     * - 24 slots for floating-point registers D0-D7 and D16-D31
     * -  1 slot for return address register XLR
     * -  1 padding slot for 16-byte stack alignment
     */
    // Use wIP0 as temp and check the mark bit of the reference. wIP0 is not used by the compiler.
    ldr   wIP0, [\xreg, #MIRROR_OBJECT_LOCK_WORD_OFFSET]
    tbz   wIP0, #LOCK_WORD_MARK_BIT_SHIFT, .Lslow_path_rb_\name
    ret
.Lslow_path_rb_\name:
    // Save all potentially live caller-save core registers.
    SAVE_TWO_REGS_INCREASE_FRAME x0, x1, 368
    SAVE_TWO_REGS  x2,  x3, 16
    SAVE_TWO_REGS  x4,  x5, 32
    SAVE_TWO_REGS  x6,  x7, 48
    SAVE_TWO_REGS  x8,  x9, 64
    SAVE_TWO_REGS x10, x11, 80
    SAVE_TWO_REGS x12, x13, 96
    SAVE_TWO_REGS x14, x15, 112
    SAVE_TWO_REGS x16, x17, 128
    SAVE_TWO_REGS x18, x19, 144
    // Save all potentially live caller-save floating-point registers.
    stp   d0, d1,   [sp, #160]
    stp   d2, d3,   [sp, #176]
    stp   d4, d5,   [sp, #192]
    stp   d6, d7,   [sp, #208]
    stp   d16, d17, [sp, #224]
    stp   d18, d19, [sp, #240]
    stp   d20, d21, [sp, #256]
    stp   d22, d23, [sp, #272]
    stp   d24, d25, [sp, #288]
    stp   d26, d27, [sp, #304]
    stp   d28, d29, [sp, #320]
    stp   d30, d31, [sp, #336]
    // Save return address.
    // (sp + #352 is a padding slot)
    SAVE_REG xLR, 360

    .ifnc \wreg, w0
      mov   w0, \wreg                   // Pass arg1 - obj from `wreg`
    .endif
    bl    artReadBarrierMark            // artReadBarrierMark(obj)
    .ifnc \wreg, w0
      mov   \wreg, w0                   // Return result into `wreg`
    .endif

    // Restore core regs, except `xreg`, as `wreg` is used to return the
    // result of this function (simply remove it from the stack instead).
    POP_REGS_NE x0, x1,   0,   \xreg
    POP_REGS_NE x2, x3,   16,  \xreg
    POP_REGS_NE x4, x5,   32,  \xreg
    POP_REGS_NE x6, x7,   48,  \xreg
    POP_REGS_NE x8, x9,   64,  \xreg
    POP_REGS_NE x10, x11, 80,  \xreg
    POP_REGS_NE x12, x13, 96,  \xreg
    POP_REGS_NE x14, x15, 112, \xreg
    POP_REGS_NE x16, x17, 128, \xreg
    POP_REGS_NE x18, x19, 144, \xreg
    // Restore floating-point registers.
    ldp   d0, d1,   [sp, #160]
    ldp   d2, d3,   [sp, #176]
    ldp   d4, d5,   [sp, #192]
    ldp   d6, d7,   [sp, #208]
    ldp   d16, d17, [sp, #224]
    ldp   d18, d19, [sp, #240]
    ldp   d20, d21, [sp, #256]
    ldp   d22, d23, [sp, #272]
    ldp   d24, d25, [sp, #288]
    ldp   d26, d27, [sp, #304]
    ldp   d28, d29, [sp, #320]
    ldp   d30, d31, [sp, #336]
    // Restore return address and remove padding.
    RESTORE_REG xLR, 360
    DECREASE_FRAME 368
.Lret_rb_\name:
    ret
END \name
.endm

READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg00, w0,  x0
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg01, w1,  x1
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg02, w2,  x2
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg03, w3,  x3
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg04, w4,  x4
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg05, w5,  x5
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg06, w6,  x6
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg07, w7,  x7
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg08, w8,  x8
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg09, w9,  x9
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg10, w10, x10
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg11, w11, x11
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg12, w12, x12
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg13, w13, x13
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg14, w14, x14
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg15, w15, x15
// READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg16, w16, x16 ip0 is blocked
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg17, w17, x17
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg18, w18, x18
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg19, w19, x19
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg20, w20, x20
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg21, w21, x21
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg22, w22, x22
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg23, w23, x23
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg24, w24, x24
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg25, w25, x25
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg26, w26, x26
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg27, w27, x27
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg28, w28, x28
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg29, w29, x29
