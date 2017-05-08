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

#include "asm_support_mips.S"

#include "arch/quick_alloc_entrypoints.S"

    .set noreorder
    .balign 4

    /* Deliver the given exception */
    .extern artDeliverExceptionFromCode
    /* Deliver an exception pending on a thread */
    .extern artDeliverPendingExceptionFromCode

#define ARG_SLOT_SIZE   32    // space for a0-a3 plus 4 more words

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAllCalleeSaves)
     * Callee-save: $s0-$s8 + $gp + $ra, 11 total + 1 word for Method*
     * Clobbers $t0 and $sp
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_ALL_CALLEE_SAVES + ARG_SLOT_SIZE bytes on the stack
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    addiu  $sp, $sp, -96
    .cfi_adjust_cfa_offset 96

     // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 96)
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(MIPS) size not as expected."
#endif

    sw     $ra, 92($sp)
    .cfi_rel_offset 31, 92
    sw     $s8, 88($sp)
    .cfi_rel_offset 30, 88
    sw     $gp, 84($sp)
    .cfi_rel_offset 28, 84
    sw     $s7, 80($sp)
    .cfi_rel_offset 23, 80
    sw     $s6, 76($sp)
    .cfi_rel_offset 22, 76
    sw     $s5, 72($sp)
    .cfi_rel_offset 21, 72
    sw     $s4, 68($sp)
    .cfi_rel_offset 20, 68
    sw     $s3, 64($sp)
    .cfi_rel_offset 19, 64
    sw     $s2, 60($sp)
    .cfi_rel_offset 18, 60
    sw     $s1, 56($sp)
    .cfi_rel_offset 17, 56
    sw     $s0, 52($sp)
    .cfi_rel_offset 16, 52

    SDu $f30, $f31, 44, $sp, $t1
    SDu $f28, $f29, 36, $sp, $t1
    SDu $f26, $f27, 28, $sp, $t1
    SDu $f24, $f25, 20, $sp, $t1
    SDu $f22, $f23, 12, $sp, $t1
    SDu $f20, $f21, 4,  $sp, $t1

    # 1 word for holding Method*

    lw $t0, %got(_ZN3art7Runtime9instance_E)($gp)
    lw $t0, 0($t0)
    lw $t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET($t0)
    sw $t0, 0($sp)                                # Place Method* at bottom of stack.
    sw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)  # Place sp in Thread::Current()->top_quick_frame.
    addiu  $sp, $sp, -ARG_SLOT_SIZE               # reserve argument slots on the stack
    .cfi_adjust_cfa_offset ARG_SLOT_SIZE
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsOnly). Restoration assumes non-moving GC.
     * Does not include rSUSPEND or rSELF
     * callee-save: $s2-$s8 + $gp + $ra, 9 total + 2 words padding + 1 word to hold Method*
     * Clobbers $t0 and $sp
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_REFS_ONLY + ARG_SLOT_SIZE bytes on the stack
     */
.macro SETUP_SAVE_REFS_ONLY_FRAME
    addiu  $sp, $sp, -48
    .cfi_adjust_cfa_offset 48

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_ONLY != 48)
#error "FRAME_SIZE_SAVE_REFS_ONLY(MIPS) size not as expected."
#endif

    sw     $ra, 44($sp)
    .cfi_rel_offset 31, 44
    sw     $s8, 40($sp)
    .cfi_rel_offset 30, 40
    sw     $gp, 36($sp)
    .cfi_rel_offset 28, 36
    sw     $s7, 32($sp)
    .cfi_rel_offset 23, 32
    sw     $s6, 28($sp)
    .cfi_rel_offset 22, 28
    sw     $s5, 24($sp)
    .cfi_rel_offset 21, 24
    sw     $s4, 20($sp)
    .cfi_rel_offset 20, 20
    sw     $s3, 16($sp)
    .cfi_rel_offset 19, 16
    sw     $s2, 12($sp)
    .cfi_rel_offset 18, 12
    # 2 words for alignment and bottom word will hold Method*

    lw $t0, %got(_ZN3art7Runtime9instance_E)($gp)
    lw $t0, 0($t0)
    lw $t0, RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET($t0)
    sw $t0, 0($sp)                                # Place Method* at bottom of stack.
    sw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)  # Place sp in Thread::Current()->top_quick_frame.
    addiu  $sp, $sp, -ARG_SLOT_SIZE               # reserve argument slots on the stack
    .cfi_adjust_cfa_offset ARG_SLOT_SIZE
.endm

.macro RESTORE_SAVE_REFS_ONLY_FRAME
    addiu  $sp, $sp, ARG_SLOT_SIZE                # remove argument slots on the stack
    .cfi_adjust_cfa_offset -ARG_SLOT_SIZE
    lw     $ra, 44($sp)
    .cfi_restore 31
    lw     $s8, 40($sp)
    .cfi_restore 30
    lw     $gp, 36($sp)
    .cfi_restore 28
    lw     $s7, 32($sp)
    .cfi_restore 23
    lw     $s6, 28($sp)
    .cfi_restore 22
    lw     $s5, 24($sp)
    .cfi_restore 21
    lw     $s4, 20($sp)
    .cfi_restore 20
    lw     $s3, 16($sp)
    .cfi_restore 19
    lw     $s2, 12($sp)
    .cfi_restore 18
    addiu  $sp, $sp, 48
    .cfi_adjust_cfa_offset -48
.endm

.macro RESTORE_SAVE_REFS_ONLY_FRAME_AND_RETURN
    RESTORE_SAVE_REFS_ONLY_FRAME
    jalr   $zero, $ra
    nop
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs).
     * callee-save: $a1-$a3, $t0-$t1, $s2-$s8, $gp, $ra, $f8-$f19
     *              (26 total + 1 word padding + method*)
     */
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    addiu  $sp, $sp, -112
    .cfi_adjust_cfa_offset 112

    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 112)
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(MIPS) size not as expected."
#endif

    sw     $ra, 108($sp)
    .cfi_rel_offset 31, 108
    sw     $s8, 104($sp)
    .cfi_rel_offset 30, 104
    sw     $gp, 100($sp)
    .cfi_rel_offset 28, 100
    sw     $s7, 96($sp)
    .cfi_rel_offset 23, 96
    sw     $s6, 92($sp)
    .cfi_rel_offset 22, 92
    sw     $s5, 88($sp)
    .cfi_rel_offset 21, 88
    sw     $s4, 84($sp)
    .cfi_rel_offset 20, 84
    sw     $s3, 80($sp)
    .cfi_rel_offset 19, 80
    sw     $s2, 76($sp)
    .cfi_rel_offset 18, 76
    sw     $t1, 72($sp)
    .cfi_rel_offset 9, 72
    sw     $t0, 68($sp)
    .cfi_rel_offset 8, 68
    sw     $a3, 64($sp)
    .cfi_rel_offset 7, 64
    sw     $a2, 60($sp)
    .cfi_rel_offset 6, 60
    sw     $a1, 56($sp)
    .cfi_rel_offset 5, 56
    SDu $f18, $f19, 48, $sp, $t8
    SDu $f16, $f17, 40, $sp, $t8
    SDu $f14, $f15, 32, $sp, $t8
    SDu $f12, $f13, 24, $sp, $t8
    SDu $f10, $f11, 16, $sp, $t8
    SDu $f8, $f9, 8, $sp, $t8
    # bottom will hold Method*
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs). Restoration assumes non-moving GC.
     * callee-save: $a1-$a3, $t0-$t1, $s2-$s8, $gp, $ra, $f8-$f19
     *              (26 total + 1 word padding + method*)
     * Clobbers $t0 and $sp
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_REFS_AND_ARGS + ARG_SLOT_SIZE bytes on the stack
     */
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME
    SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    lw $t0, %got(_ZN3art7Runtime9instance_E)($gp)
    lw $t0, 0($t0)
    lw $t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET($t0)
    sw $t0, 0($sp)                                # Place Method* at bottom of stack.
    sw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)  # Place sp in Thread::Current()->top_quick_frame.
    addiu  $sp, $sp, -ARG_SLOT_SIZE               # reserve argument slots on the stack
    .cfi_adjust_cfa_offset ARG_SLOT_SIZE
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveRefsAndArgs). Restoration assumes non-moving GC.
     * callee-save: $a1-$a3, $t0-$t1, $s2-$s8, $gp, $ra, $f8-$f19
     *              (26 total + 1 word padding + method*)
     * Clobbers $sp
     * Use $a0 as the Method* and loads it into bottom of stack.
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_REFS_AND_ARGS + ARG_SLOT_SIZE bytes on the stack
     */
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    SETUP_SAVE_REFS_AND_ARGS_FRAME_REGISTERS_ONLY
    sw $a0, 0($sp)                                # Place Method* at bottom of stack.
    sw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)  # Place sp in Thread::Current()->top_quick_frame.
    addiu  $sp, $sp, -ARG_SLOT_SIZE               # reserve argument slots on the stack
    .cfi_adjust_cfa_offset ARG_SLOT_SIZE
.endm

.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    addiu  $sp, $sp, ARG_SLOT_SIZE                # remove argument slots on the stack
    .cfi_adjust_cfa_offset -ARG_SLOT_SIZE
    lw     $ra, 108($sp)
    .cfi_restore 31
    lw     $s8, 104($sp)
    .cfi_restore 30
    lw     $gp, 100($sp)
    .cfi_restore 28
    lw     $s7, 96($sp)
    .cfi_restore 23
    lw     $s6, 92($sp)
    .cfi_restore 22
    lw     $s5, 88($sp)
    .cfi_restore 21
    lw     $s4, 84($sp)
    .cfi_restore 20
    lw     $s3, 80($sp)
    .cfi_restore 19
    lw     $s2, 76($sp)
    .cfi_restore 18
    lw     $t1, 72($sp)
    .cfi_restore 9
    lw     $t0, 68($sp)
    .cfi_restore 8
    lw     $a3, 64($sp)
    .cfi_restore 7
    lw     $a2, 60($sp)
    .cfi_restore 6
    lw     $a1, 56($sp)
    .cfi_restore 5
    LDu $f18, $f19, 48, $sp, $t8
    LDu $f16, $f17, 40, $sp, $t8
    LDu $f14, $f15, 32, $sp, $t8
    LDu $f12, $f13, 24, $sp, $t8
    LDu $f10, $f11, 16, $sp, $t8
    LDu $f8, $f9, 8, $sp, $t8
    addiu  $sp, $sp, 112          # pop frame
    .cfi_adjust_cfa_offset -112
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveEverything).
     * when the $sp has already been decremented by FRAME_SIZE_SAVE_EVERYTHING.
     * Callee-save: $at, $v0-$v1, $a0-$a3, $t0-$t7, $s0-$s7, $t8-$t9, $gp, $fp $ra, $f0-$f31;
     *              28(GPR)+ 32(FPR) + 3 words for padding and 1 word for Method*
     * Clobbers $t0 and $t1.
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_EVERYTHING + ARG_SLOT_SIZE bytes on the stack.
     * This macro sets up $gp; entrypoints using it should start with ENTRY_NO_GP.
     */
.macro SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP
     // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_EVERYTHING != 256)
#error "FRAME_SIZE_SAVE_EVERYTHING(MIPS) size not as expected."
#endif

    sw     $ra, 252($sp)
    .cfi_rel_offset 31, 252
    sw     $fp, 248($sp)
    .cfi_rel_offset 30, 248
    sw     $gp, 244($sp)
    .cfi_rel_offset 28, 244
    sw     $t9, 240($sp)
    .cfi_rel_offset 25, 240
    sw     $t8, 236($sp)
    .cfi_rel_offset 24, 236
    sw     $s7, 232($sp)
    .cfi_rel_offset 23, 232
    sw     $s6, 228($sp)
    .cfi_rel_offset 22, 228
    sw     $s5, 224($sp)
    .cfi_rel_offset 21, 224
    sw     $s4, 220($sp)
    .cfi_rel_offset 20, 220
    sw     $s3, 216($sp)
    .cfi_rel_offset 19, 216
    sw     $s2, 212($sp)
    .cfi_rel_offset 18, 212
    sw     $s1, 208($sp)
    .cfi_rel_offset 17, 208
    sw     $s0, 204($sp)
    .cfi_rel_offset 16, 204
    sw     $t7, 200($sp)
    .cfi_rel_offset 15, 200
    sw     $t6, 196($sp)
    .cfi_rel_offset 14, 196
    sw     $t5, 192($sp)
    .cfi_rel_offset 13, 192
    sw     $t4, 188($sp)
    .cfi_rel_offset 12, 188
    sw     $t3, 184($sp)
    .cfi_rel_offset 11, 184
    sw     $t2, 180($sp)
    .cfi_rel_offset 10, 180
    sw     $t1, 176($sp)
    .cfi_rel_offset 9, 176
    sw     $t0, 172($sp)
    .cfi_rel_offset 8, 172
    sw     $a3, 168($sp)
    .cfi_rel_offset 7, 168
    sw     $a2, 164($sp)
    .cfi_rel_offset 6, 164
    sw     $a1, 160($sp)
    .cfi_rel_offset 5, 160
    sw     $a0, 156($sp)
    .cfi_rel_offset 4, 156
    sw     $v1, 152($sp)
    .cfi_rel_offset 3, 152
    sw     $v0, 148($sp)
    .cfi_rel_offset 2, 148

    // Set up $gp, clobbering $ra and using the branch delay slot for a useful instruction.
    bal 1f
    .set push
    .set noat
    sw     $at, 144($sp)
    .cfi_rel_offset 1, 144
    .set pop
1:
    .cpload $ra

    SDu $f30, $f31, 136, $sp, $t1
    SDu $f28, $f29, 128, $sp, $t1
    SDu $f26, $f27, 120, $sp, $t1
    SDu $f24, $f25, 112, $sp, $t1
    SDu $f22, $f23, 104, $sp, $t1
    SDu $f20, $f21, 96,  $sp, $t1
    SDu $f18, $f19, 88,  $sp, $t1
    SDu $f16, $f17, 80,  $sp, $t1
    SDu $f14, $f15, 72,  $sp, $t1
    SDu $f12, $f13, 64,  $sp, $t1
    SDu $f10, $f11, 56,  $sp, $t1
    SDu $f8, $f9, 48,  $sp, $t1
    SDu $f6, $f7, 40,  $sp, $t1
    SDu $f4, $f5, 32,  $sp, $t1
    SDu $f2, $f3, 24,  $sp, $t1
    SDu $f0, $f1, 16,  $sp, $t1

    # 3 words padding and 1 word for holding Method*

    lw $t0, %got(_ZN3art7Runtime9instance_E)($gp)
    lw $t0, 0($t0)
    lw $t0, RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET($t0)
    sw $t0, 0($sp)                                # Place Method* at bottom of stack.
    sw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)  # Place sp in Thread::Current()->top_quick_frame.
    addiu  $sp, $sp, -ARG_SLOT_SIZE               # reserve argument slots on the stack
    .cfi_adjust_cfa_offset ARG_SLOT_SIZE
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveEverything).
     * Callee-save: $at, $v0-$v1, $a0-$a3, $t0-$t7, $s0-$s7, $t8-$t9, $gp, $fp $ra, $f0-$f31;
     *              28(GPR)+ 32(FPR) + 3 words for padding and 1 word for Method*
     * Clobbers $t0 and $t1.
     * Allocates ARG_SLOT_SIZE bytes at the bottom of the stack for arg slots.
     * Reserves FRAME_SIZE_SAVE_EVERYTHING + ARG_SLOT_SIZE bytes on the stack.
     * This macro sets up $gp; entrypoints using it should start with ENTRY_NO_GP.
     */
.macro SETUP_SAVE_EVERYTHING_FRAME
    addiu  $sp, $sp, -(FRAME_SIZE_SAVE_EVERYTHING)
    .cfi_adjust_cfa_offset (FRAME_SIZE_SAVE_EVERYTHING)
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP
.endm

.macro RESTORE_SAVE_EVERYTHING_FRAME
    addiu  $sp, $sp, ARG_SLOT_SIZE                # remove argument slots on the stack
    .cfi_adjust_cfa_offset -ARG_SLOT_SIZE

    LDu $f30, $f31, 136, $sp, $t1
    LDu $f28, $f29, 128, $sp, $t1
    LDu $f26, $f27, 120, $sp, $t1
    LDu $f24, $f25, 112, $sp, $t1
    LDu $f22, $f23, 104, $sp, $t1
    LDu $f20, $f21, 96,  $sp, $t1
    LDu $f18, $f19, 88,  $sp, $t1
    LDu $f16, $f17, 80,  $sp, $t1
    LDu $f14, $f15, 72,  $sp, $t1
    LDu $f12, $f13, 64,  $sp, $t1
    LDu $f10, $f11, 56,  $sp, $t1
    LDu $f8, $f9, 48,  $sp, $t1
    LDu $f6, $f7, 40,  $sp, $t1
    LDu $f4, $f5, 32,  $sp, $t1
    LDu $f2, $f3, 24,  $sp, $t1
    LDu $f0, $f1, 16,  $sp, $t1

    lw     $ra, 252($sp)
    .cfi_restore 31
    lw     $fp, 248($sp)
    .cfi_restore 30
    lw     $gp, 244($sp)
    .cfi_restore 28
    lw     $t9, 240($sp)
    .cfi_restore 25
    lw     $t8, 236($sp)
    .cfi_restore 24
    lw     $s7, 232($sp)
    .cfi_restore 23
    lw     $s6, 228($sp)
    .cfi_restore 22
    lw     $s5, 224($sp)
    .cfi_restore 21
    lw     $s4, 220($sp)
    .cfi_restore 20
    lw     $s3, 216($sp)
    .cfi_restore 19
    lw     $s2, 212($sp)
    .cfi_restore 18
    lw     $s1, 208($sp)
    .cfi_restore 17
    lw     $s0, 204($sp)
    .cfi_restore 16
    lw     $t7, 200($sp)
    .cfi_restore 15
    lw     $t6, 196($sp)
    .cfi_restore 14
    lw     $t5, 192($sp)
    .cfi_restore 13
    lw     $t4, 188($sp)
    .cfi_restore 12
    lw     $t3, 184($sp)
    .cfi_restore 11
    lw     $t2, 180($sp)
    .cfi_restore 10
    lw     $t1, 176($sp)
    .cfi_restore 9
    lw     $t0, 172($sp)
    .cfi_restore 8
    lw     $a3, 168($sp)
    .cfi_restore 7
    lw     $a2, 164($sp)
    .cfi_restore 6
    lw     $a1, 160($sp)
    .cfi_restore 5
    lw     $a0, 156($sp)
    .cfi_restore 4
    lw     $v1, 152($sp)
    .cfi_restore 3
    lw     $v0, 148($sp)
    .cfi_restore 2
    .set push
    .set noat
    lw     $at, 144($sp)
    .cfi_restore 1
    .set pop

    addiu  $sp, $sp, 256            # pop frame
    .cfi_adjust_cfa_offset -256
.endm

    /*
     * Macro that set calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_
     */
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME    # save callee saves for throw
    la      $t9, artDeliverPendingExceptionFromCode
    jalr    $zero, $t9                   # artDeliverPendingExceptionFromCode(Thread*)
    move    $a0, rSELF                   # pass Thread::Current
.endm

.macro RETURN_IF_NO_EXCEPTION
    lw     $t0, THREAD_EXCEPTION_OFFSET(rSELF) # load Thread::Current()->exception_
    RESTORE_SAVE_REFS_ONLY_FRAME
    bnez   $t0, 1f                       # success if no exception is pending
    nop
    jalr   $zero, $ra
    nop
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro RETURN_IF_ZERO
    RESTORE_SAVE_REFS_ONLY_FRAME
    bnez   $v0, 1f                       # success?
    nop
    jalr   $zero, $ra                    # return on success
    nop
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
    RESTORE_SAVE_REFS_ONLY_FRAME
    beqz   $v0, 1f                       # success?
    nop
    jalr   $zero, $ra                    # return on success
    nop
1:
    DELIVER_PENDING_EXCEPTION
.endm

    /*
     * On stack replacement stub.
     * On entry:
     *   a0 = stack to copy
     *   a1 = size of stack
     *   a2 = pc to call
     *   a3 = JValue* result
     *   [sp + 16] = shorty
     *   [sp + 20] = thread
     */
ENTRY art_quick_osr_stub
    // Save callee general purpose registers, RA and GP.
    addiu  $sp, $sp, -48
    .cfi_adjust_cfa_offset 48
    sw     $ra, 44($sp)
    .cfi_rel_offset 31, 44
    sw     $s8, 40($sp)
    .cfi_rel_offset 30, 40
    sw     $gp, 36($sp)
    .cfi_rel_offset 28, 36
    sw     $s7, 32($sp)
    .cfi_rel_offset 23, 32
    sw     $s6, 28($sp)
    .cfi_rel_offset 22, 28
    sw     $s5, 24($sp)
    .cfi_rel_offset 21, 24
    sw     $s4, 20($sp)
    .cfi_rel_offset 20, 20
    sw     $s3, 16($sp)
    .cfi_rel_offset 19, 16
    sw     $s2, 12($sp)
    .cfi_rel_offset 18, 12
    sw     $s1, 8($sp)
    .cfi_rel_offset 17, 8
    sw     $s0, 4($sp)
    .cfi_rel_offset 16, 4

    move   $s8, $sp                        # Save the stack pointer
    move   $s7, $a1                        # Save size of stack
    move   $s6, $a2                        # Save the pc to call
    lw     rSELF, 48+20($sp)               # Save managed thread pointer into rSELF
    addiu  $t0, $sp, -12                   # Reserve space for stack pointer,
                                           #    JValue* result, and ArtMethod* slot.
    srl    $t0, $t0, 4                     # Align stack pointer to 16 bytes
    sll    $sp, $t0, 4                     # Update stack pointer
    sw     $s8, 4($sp)                     # Save old stack pointer
    sw     $a3, 8($sp)                     # Save JValue* result
    sw     $zero, 0($sp)                   # Store null for ArtMethod* at bottom of frame
    subu   $sp, $a1                        # Reserve space for callee stack
    move   $a2, $a1
    move   $a1, $a0
    move   $a0, $sp
    la     $t9, memcpy
    jalr   $t9                             # memcpy (dest a0, src a1, bytes a2)
    addiu  $sp, $sp, -16                   # make space for argument slots for memcpy
    bal    .Losr_entry                     # Call the method
    addiu  $sp, $sp, 16                    # restore stack after memcpy
    lw     $a2, 8($sp)                     # Restore JValue* result
    lw     $sp, 4($sp)                     # Restore saved stack pointer
    lw     $a0, 48+16($sp)                 # load shorty
    lbu    $a0, 0($a0)                     # load return type
    li     $a1, 'D'                        # put char 'D' into a1
    beq    $a0, $a1, .Losr_fp_result       # Test if result type char == 'D'
    li     $a1, 'F'                        # put char 'F' into a1
    beq    $a0, $a1, .Losr_fp_result       # Test if result type char == 'F'
    nop
    sw     $v0, 0($a2)
    b      .Losr_exit
    sw     $v1, 4($a2)                     # store v0/v1 into result
.Losr_fp_result:
    SDu    $f0, $f1, 0, $a2, $t0           # store f0/f1 into result
.Losr_exit:
    lw     $ra, 44($sp)
    .cfi_restore 31
    lw     $s8, 40($sp)
    .cfi_restore 30
    lw     $gp, 36($sp)
    .cfi_restore 28
    lw     $s7, 32($sp)
    .cfi_restore 23
    lw     $s6, 28($sp)
    .cfi_restore 22
    lw     $s5, 24($sp)
    .cfi_restore 21
    lw     $s4, 20($sp)
    .cfi_restore 20
    lw     $s3, 16($sp)
    .cfi_restore 19
    lw     $s2, 12($sp)
    .cfi_restore 18
    lw     $s1, 8($sp)
    .cfi_restore 17
    lw     $s0, 4($sp)
    .cfi_restore 16
    jalr   $zero, $ra
    addiu  $sp, $sp, 48
    .cfi_adjust_cfa_offset -48
.Losr_entry:
    addiu  $s7, $s7, -4
    addu   $t0, $s7, $sp
    move   $t9, $s6
    jalr   $zero, $t9
    sw     $ra, 0($t0)                     # Store RA per the compiler ABI
END art_quick_osr_stub

    /*
     * On entry $a0 is uint32_t* gprs_ and $a1 is uint32_t* fprs_
     * FIXME: just guessing about the shape of the jmpbuf.  Where will pc be?
     */
ENTRY art_quick_do_long_jump
    LDu  $f0,  $f1,   0*8, $a1, $t1
    LDu  $f2,  $f3,   1*8, $a1, $t1
    LDu  $f4,  $f5,   2*8, $a1, $t1
    LDu  $f6,  $f7,   3*8, $a1, $t1
    LDu  $f8,  $f9,   4*8, $a1, $t1
    LDu  $f10, $f11,  5*8, $a1, $t1
    LDu  $f12, $f13,  6*8, $a1, $t1
    LDu  $f14, $f15,  7*8, $a1, $t1
    LDu  $f16, $f17,  8*8, $a1, $t1
    LDu  $f18, $f19,  9*8, $a1, $t1
    LDu  $f20, $f21, 10*8, $a1, $t1
    LDu  $f22, $f23, 11*8, $a1, $t1
    LDu  $f24, $f25, 12*8, $a1, $t1
    LDu  $f26, $f27, 13*8, $a1, $t1
    LDu  $f28, $f29, 14*8, $a1, $t1
    LDu  $f30, $f31, 15*8, $a1, $t1

    .set push
    .set nomacro
    .set noat
    lw      $at, 4($a0)
    .set pop
    lw      $v0, 8($a0)
    lw      $v1, 12($a0)
    lw      $a1, 20($a0)
    lw      $a2, 24($a0)
    lw      $a3, 28($a0)
    lw      $t0, 32($a0)
    lw      $t1, 36($a0)
    lw      $t2, 40($a0)
    lw      $t3, 44($a0)
    lw      $t4, 48($a0)
    lw      $t5, 52($a0)
    lw      $t6, 56($a0)
    lw      $t7, 60($a0)
    lw      $s0, 64($a0)
    lw      $s1, 68($a0)
    lw      $s2, 72($a0)
    lw      $s3, 76($a0)
    lw      $s4, 80($a0)
    lw      $s5, 84($a0)
    lw      $s6, 88($a0)
    lw      $s7, 92($a0)
    lw      $t8, 96($a0)
    lw      $t9, 100($a0)
    lw      $gp, 112($a0)
    lw      $sp, 116($a0)
    lw      $fp, 120($a0)
    lw      $ra, 124($a0)
    lw      $a0, 16($a0)
    move    $v0, $zero          # clear result registers v0 and v1 (in branch delay slot)
    jalr    $zero, $t9          # do long jump
    move    $v1, $zero
END art_quick_do_long_jump

    /*
     * Called by managed code, saves most registers (forms basis of long jump context) and passes
     * the bottom of the stack. artDeliverExceptionFromCode will place the callee save Method* at
     * the bottom of the thread. On entry a0 holds Throwable*
     */
ENTRY art_quick_deliver_exception
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    la   $t9, artDeliverExceptionFromCode
    jalr $zero, $t9                 # artDeliverExceptionFromCode(Throwable*, Thread*)
    move $a1, rSELF                 # pass Thread::Current
END art_quick_deliver_exception

    /*
     * Called by managed code to create and deliver a NullPointerException
     */
    .extern artThrowNullPointerExceptionFromCode
ENTRY_NO_GP art_quick_throw_null_pointer_exception
    // Note that setting up $gp does not rely on $t9 here, so branching here directly is OK,
    // even after clobbering any registers we don't need to preserve, such as $gp or $t0.
    SETUP_SAVE_EVERYTHING_FRAME
    la   $t9, artThrowNullPointerExceptionFromCode
    jalr $zero, $t9                 # artThrowNullPointerExceptionFromCode(Thread*)
    move $a0, rSELF                 # pass Thread::Current
END art_quick_throw_null_pointer_exception


    /*
     * Call installed by a signal handler to create and deliver a NullPointerException.
     */
    .extern artThrowNullPointerExceptionFromSignal
ENTRY_NO_GP_CUSTOM_CFA art_quick_throw_null_pointer_exception_from_signal, FRAME_SIZE_SAVE_EVERYTHING
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP
    # Retrieve the fault address from the padding where the signal handler stores it.
    lw   $a0, (ARG_SLOT_SIZE + __SIZEOF_POINTER__)($sp)
    la   $t9, artThrowNullPointerExceptionFromSignal
    jalr $zero, $t9                 # artThrowNullPointerExceptionFromSignal(uintptr_t, Thread*)
    move $a1, rSELF                 # pass Thread::Current
END art_quick_throw_null_pointer_exception_from_signal

    /*
     * Called by managed code to create and deliver an ArithmeticException
     */
    .extern artThrowDivZeroFromCode
ENTRY_NO_GP art_quick_throw_div_zero
    SETUP_SAVE_EVERYTHING_FRAME
    la   $t9, artThrowDivZeroFromCode
    jalr $zero, $t9                 # artThrowDivZeroFromCode(Thread*)
    move $a0, rSELF                 # pass Thread::Current
END art_quick_throw_div_zero

    /*
     * Called by managed code to create and deliver an ArrayIndexOutOfBoundsException
     */
    .extern artThrowArrayBoundsFromCode
ENTRY_NO_GP art_quick_throw_array_bounds
    // Note that setting up $gp does not rely on $t9 here, so branching here directly is OK,
    // even after clobbering any registers we don't need to preserve, such as $gp or $t0.
    SETUP_SAVE_EVERYTHING_FRAME
    la   $t9, artThrowArrayBoundsFromCode
    jalr $zero, $t9                 # artThrowArrayBoundsFromCode(index, limit, Thread*)
    move $a2, rSELF                 # pass Thread::Current
END art_quick_throw_array_bounds

    /*
     * Called by managed code to create and deliver a StringIndexOutOfBoundsException
     * as if thrown from a call to String.charAt().
     */
    .extern artThrowStringBoundsFromCode
ENTRY_NO_GP art_quick_throw_string_bounds
    SETUP_SAVE_EVERYTHING_FRAME
    la   $t9, artThrowStringBoundsFromCode
    jalr $zero, $t9                 # artThrowStringBoundsFromCode(index, limit, Thread*)
    move $a2, rSELF                 # pass Thread::Current
END art_quick_throw_string_bounds

    /*
     * Called by managed code to create and deliver a StackOverflowError.
     */
    .extern artThrowStackOverflowFromCode
ENTRY art_quick_throw_stack_overflow
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    la   $t9, artThrowStackOverflowFromCode
    jalr $zero, $t9                 # artThrowStackOverflowFromCode(Thread*)
    move $a0, rSELF                 # pass Thread::Current
END art_quick_throw_stack_overflow

    /*
     * All generated callsites for interface invokes and invocation slow paths will load arguments
     * as usual - except instead of loading arg0/$a0 with the target Method*, arg0/$a0 will contain
     * the method_idx.  This wrapper will save arg1-arg3, and call the appropriate C helper.
     * NOTE: "this" is first visable argument of the target, and so can be found in arg1/$a1.
     *
     * The helper will attempt to locate the target and return a 64-bit result in $v0/$v1 consisting
     * of the target Method* in $v0 and method->code_ in $v1.
     *
     * If unsuccessful, the helper will return null/null. There will be a pending exception in the
     * thread and we branch to another stub to deliver it.
     *
     * On success this wrapper will restore arguments and *jump* to the target, leaving the lr
     * pointing back to the original caller.
     */
.macro INVOKE_TRAMPOLINE_BODY cxx_name
    .extern \cxx_name
    SETUP_SAVE_REFS_AND_ARGS_FRAME         # save callee saves in case allocation triggers GC
    move  $a2, rSELF                       # pass Thread::Current
    la    $t9, \cxx_name
    jalr  $t9                              # (method_idx, this, Thread*, $sp)
    addiu $a3, $sp, ARG_SLOT_SIZE          # pass $sp (remove arg slots)
    move  $a0, $v0                         # save target Method*
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    beqz  $v0, 1f
    move  $t9, $v1                         # save $v0->code_
    jalr  $zero, $t9
    nop
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

// Each of the following macros expands into four instructions or 16 bytes.
// They are used to build indexable "tables" of code.

.macro LOAD_WORD_TO_REG reg, next_arg, index_reg, label
    lw    $\reg, -4($\next_arg)   # next_arg points to argument after the current one (offset is 4)
    b     \label
    addiu $\index_reg, 16
    .balign 16
.endm

.macro LOAD_LONG_TO_REG reg1, reg2, next_arg, index_reg, next_index, label
    lw    $\reg1, -8($\next_arg)  # next_arg points to argument after the current one (offset is 8)
    lw    $\reg2, -4($\next_arg)
    b     \label
    li    $\index_reg, \next_index
    .balign 16
.endm

.macro LOAD_FLOAT_TO_REG reg, next_arg, index_reg, label
    lwc1  $\reg, -4($\next_arg)   # next_arg points to argument after the current one (offset is 4)
    b     \label
    addiu $\index_reg, 16
    .balign 16
.endm

#if defined(__mips_isa_rev) && __mips_isa_rev > 2
// LDu expands into 3 instructions for 64-bit FPU, so index_reg cannot be updated here.
.macro LOAD_DOUBLE_TO_REG reg1, reg2, next_arg, index_reg, tmp, label
    .set reorder                                # force use of the branch delay slot
    LDu  $\reg1, $\reg2, -8, $\next_arg, $\tmp  # next_arg points to argument after the current one
                                                # (offset is 8)
    b     \label
    .set noreorder
    .balign 16
.endm
#else
// LDu expands into 2 instructions for 32-bit FPU, so index_reg is updated here.
.macro LOAD_DOUBLE_TO_REG reg1, reg2, next_arg, index_reg, tmp, label
    LDu  $\reg1, $\reg2, -8, $\next_arg, $\tmp  # next_arg points to argument after the current one
                                                # (offset is 8)
    b     \label
    addiu $\index_reg, 16
    .balign 16
.endm
#endif

.macro LOAD_END index_reg, next_index, label
    b     \label
    li    $\index_reg, \next_index
    .balign 16
.endm

#define SPILL_SIZE    32

    /*
     * Invocation stub for quick code.
     * On entry:
     *   a0 = method pointer
     *   a1 = argument array or null for no argument methods
     *   a2 = size of argument array in bytes
     *   a3 = (managed) thread pointer
     *   [sp + 16] = JValue* result
     *   [sp + 20] = shorty
     */
ENTRY art_quick_invoke_stub
    sw    $a0, 0($sp)           # save out a0
    addiu $sp, $sp, -SPILL_SIZE # spill s0, s1, fp, ra and gp
    .cfi_adjust_cfa_offset SPILL_SIZE
    sw    $gp, 16($sp)
    sw    $ra, 12($sp)
    .cfi_rel_offset 31, 12
    sw    $fp, 8($sp)
    .cfi_rel_offset 30, 8
    sw    $s1, 4($sp)
    .cfi_rel_offset 17, 4
    sw    $s0, 0($sp)
    .cfi_rel_offset 16, 0
    move  $fp, $sp              # save sp in fp
    .cfi_def_cfa_register 30
    move  $s1, $a3              # move managed thread pointer into s1
    addiu $s0, $zero, SUSPEND_CHECK_INTERVAL  # reset s0 to suspend check interval
    addiu $t0, $a2, 4           # create space for ArtMethod* in frame.
    subu  $t0, $sp, $t0         # reserve & align *stack* to 16 bytes:
    srl   $t0, $t0, 4           #   native calling convention only aligns to 8B,
    sll   $sp, $t0, 4           #   so we have to ensure ART 16B alignment ourselves.
    addiu $a0, $sp, 4           # pass stack pointer + ArtMethod* as dest for memcpy
    la    $t9, memcpy
    jalr  $t9                   # (dest, src, bytes)
    addiu $sp, $sp, -16         # make space for argument slots for memcpy
    addiu $sp, $sp, 16          # restore stack after memcpy
    lw    $gp, 16($fp)          # restore $gp
    lw    $a0, SPILL_SIZE($fp)  # restore ArtMethod*
    lw    $a1, 4($sp)           # a1 = this*
    addiu $t8, $sp, 8           # t8 = pointer to the current argument (skip ArtMethod* and this*)
    li    $t6, 0                # t6 = gpr_index = 0 (corresponds to A2; A0 and A1 are skipped)
    li    $t7, 0                # t7 = fp_index = 0
    lw    $t9, 20 + SPILL_SIZE($fp)  # get shorty (20 is offset from the $sp on entry + SPILL_SIZE
                                # as the $fp is SPILL_SIZE bytes below the $sp on entry)
    addiu $t9, 1                # t9 = shorty + 1 (skip 1 for return type)

    // Load the base addresses of tabInt ... tabDouble.
    // We will use the register indices (gpr_index, fp_index) to branch.
    // Note that the indices are scaled by 16, so they can be added to the bases directly.
#if defined(__mips_isa_rev) && __mips_isa_rev >= 6
    lapc  $t2, tabInt
    lapc  $t3, tabLong
    lapc  $t4, tabSingle
    lapc  $t5, tabDouble
#else
    bltzal $zero, tabBase       # nal
    addiu $t2, $ra, %lo(tabInt - tabBase)
tabBase:
    addiu $t3, $ra, %lo(tabLong - tabBase)
    addiu $t4, $ra, %lo(tabSingle - tabBase)
    addiu $t5, $ra, %lo(tabDouble - tabBase)
#endif

loop:
    lbu   $ra, 0($t9)           # ra = shorty[i]
    beqz  $ra, loopEnd          # finish getting args when shorty[i] == '\0'
    addiu $t9, 1

    addiu $ra, -'J'
    beqz  $ra, isLong           # branch if result type char == 'J'
    addiu $ra, 'J' - 'D'
    beqz  $ra, isDouble         # branch if result type char == 'D'
    addiu $ra, 'D' - 'F'
    beqz  $ra, isSingle         # branch if result type char == 'F'

    addu  $ra, $t2, $t6
    jalr  $zero, $ra
    addiu $t8, 4                # next_arg = curr_arg + 4

isLong:
    addu  $ra, $t3, $t6
    jalr  $zero, $ra
    addiu $t8, 8                # next_arg = curr_arg + 8

isSingle:
    addu  $ra, $t4, $t7
    jalr  $zero, $ra
    addiu $t8, 4                # next_arg = curr_arg + 4

isDouble:
    addu  $ra, $t5, $t7
#if defined(__mips_isa_rev) && __mips_isa_rev > 2
    addiu $t7, 16               # fp_index += 16 didn't fit into LOAD_DOUBLE_TO_REG
#endif
    jalr  $zero, $ra
    addiu $t8, 8                # next_arg = curr_arg + 8

loopEnd:
    lw    $t9, ART_METHOD_QUICK_CODE_OFFSET_32($a0)  # get pointer to the code
    jalr  $t9                   # call the method
    sw    $zero, 0($sp)         # store null for ArtMethod* at bottom of frame
    move  $sp, $fp              # restore the stack
    lw    $s0, 0($sp)
    .cfi_restore 16
    lw    $s1, 4($sp)
    .cfi_restore 17
    lw    $fp, 8($sp)
    .cfi_restore 30
    lw    $ra, 12($sp)
    .cfi_restore 31
    addiu $sp, $sp, SPILL_SIZE
    .cfi_adjust_cfa_offset -SPILL_SIZE
    lw    $t0, 16($sp)          # get result pointer
    lw    $t1, 20($sp)          # get shorty
    lb    $t1, 0($t1)           # get result type char
    li    $t2, 'D'              # put char 'D' into t2
    beq   $t1, $t2, 5f          # branch if result type char == 'D'
    li    $t3, 'F'              # put char 'F' into t3
    beq   $t1, $t3, 5f          # branch if result type char == 'F'
    sw    $v0, 0($t0)           # store the result
    jalr  $zero, $ra
    sw    $v1, 4($t0)           # store the other half of the result
5:
    SDu   $f0, $f1, 0, $t0, $t1 # store floating point result
    jalr  $zero, $ra
    nop

    // Note that gpr_index is kept within the range of tabInt and tabLong
    // and fp_index is kept within the range of tabSingle and tabDouble.
    .balign 16
tabInt:
    LOAD_WORD_TO_REG a2, t8, t6, loop             # a2 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG a3, t8, t6, loop             # a3 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG t0, t8, t6, loop             # t0 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG t1, t8, t6, loop             # t1 = current argument, gpr_index += 16
    LOAD_END t6, 4*16, loop                       # no more GPR args, gpr_index = 4*16
tabLong:
    LOAD_LONG_TO_REG a2, a3, t8, t6, 2*16, loop   # a2_a3 = curr_arg, gpr_index = 2*16
    LOAD_LONG_TO_REG t0, t1, t8, t6, 4*16, loop   # t0_t1 = curr_arg, gpr_index = 4*16
    LOAD_LONG_TO_REG t0, t1, t8, t6, 4*16, loop   # t0_t1 = curr_arg, gpr_index = 4*16
    LOAD_END t6, 4*16, loop                       # no more GPR args, gpr_index = 4*16
    LOAD_END t6, 4*16, loop                       # no more GPR args, gpr_index = 4*16
tabSingle:
    LOAD_FLOAT_TO_REG f8, t8, t7, loop            # f8 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f10, t8, t7, loop           # f10 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f12, t8, t7, loop           # f12 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f14, t8, t7, loop           # f14 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f16, t8, t7, loop           # f16 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f18, t8, t7, loop           # f18 = curr_arg, fp_index += 16
    LOAD_END t7, 6*16, loop                       # no more FPR args, fp_index = 6*16
tabDouble:
    LOAD_DOUBLE_TO_REG f8, f9, t8, t7, ra, loop   # f8_f9 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f10, f11, t8, t7, ra, loop # f10_f11 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f12, f13, t8, t7, ra, loop # f12_f13 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f14, f15, t8, t7, ra, loop # f14_f15 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f16, f17, t8, t7, ra, loop # f16_f17 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f18, f19, t8, t7, ra, loop # f18_f19 = curr_arg; if FPU32, fp_index += 16
    LOAD_END t7, 6*16, loop                       # no more FPR args, fp_index = 6*16
END art_quick_invoke_stub

    /*
     * Invocation static stub for quick code.
     * On entry:
     *   a0 = method pointer
     *   a1 = argument array or null for no argument methods
     *   a2 = size of argument array in bytes
     *   a3 = (managed) thread pointer
     *   [sp + 16] = JValue* result
     *   [sp + 20] = shorty
     */
ENTRY art_quick_invoke_static_stub
    sw    $a0, 0($sp)           # save out a0
    addiu $sp, $sp, -SPILL_SIZE # spill s0, s1, fp, ra and gp
    .cfi_adjust_cfa_offset SPILL_SIZE
    sw    $gp, 16($sp)
    sw    $ra, 12($sp)
    .cfi_rel_offset 31, 12
    sw    $fp, 8($sp)
    .cfi_rel_offset 30, 8
    sw    $s1, 4($sp)
    .cfi_rel_offset 17, 4
    sw    $s0, 0($sp)
    .cfi_rel_offset 16, 0
    move  $fp, $sp              # save sp in fp
    .cfi_def_cfa_register 30
    move  $s1, $a3              # move managed thread pointer into s1
    addiu $s0, $zero, SUSPEND_CHECK_INTERVAL  # reset s0 to suspend check interval
    addiu $t0, $a2, 4           # create space for ArtMethod* in frame.
    subu  $t0, $sp, $t0         # reserve & align *stack* to 16 bytes:
    srl   $t0, $t0, 4           #   native calling convention only aligns to 8B,
    sll   $sp, $t0, 4           #   so we have to ensure ART 16B alignment ourselves.
    addiu $a0, $sp, 4           # pass stack pointer + ArtMethod* as dest for memcpy
    la    $t9, memcpy
    jalr  $t9                   # (dest, src, bytes)
    addiu $sp, $sp, -16         # make space for argument slots for memcpy
    addiu $sp, $sp, 16          # restore stack after memcpy
    lw    $gp, 16($fp)          # restore $gp
    lw    $a0, SPILL_SIZE($fp)  # restore ArtMethod*
    addiu $t8, $sp, 4           # t8 = pointer to the current argument (skip ArtMethod*)
    li    $t6, 0                # t6 = gpr_index = 0 (corresponds to A1; A0 is skipped)
    li    $t7, 0                # t7 = fp_index = 0
    lw    $t9, 20 + SPILL_SIZE($fp)  # get shorty (20 is offset from the $sp on entry + SPILL_SIZE
                                # as the $fp is SPILL_SIZE bytes below the $sp on entry)
    addiu $t9, 1                # t9 = shorty + 1 (skip 1 for return type)

    // Load the base addresses of tabIntS ... tabDoubleS.
    // We will use the register indices (gpr_index, fp_index) to branch.
    // Note that the indices are scaled by 16, so they can be added to the bases directly.
#if defined(__mips_isa_rev) && __mips_isa_rev >= 6
    lapc  $t2, tabIntS
    lapc  $t3, tabLongS
    lapc  $t4, tabSingleS
    lapc  $t5, tabDoubleS
#else
    bltzal $zero, tabBaseS      # nal
    addiu $t2, $ra, %lo(tabIntS - tabBaseS)
tabBaseS:
    addiu $t3, $ra, %lo(tabLongS - tabBaseS)
    addiu $t4, $ra, %lo(tabSingleS - tabBaseS)
    addiu $t5, $ra, %lo(tabDoubleS - tabBaseS)
#endif

loopS:
    lbu   $ra, 0($t9)           # ra = shorty[i]
    beqz  $ra, loopEndS         # finish getting args when shorty[i] == '\0'
    addiu $t9, 1

    addiu $ra, -'J'
    beqz  $ra, isLongS          # branch if result type char == 'J'
    addiu $ra, 'J' - 'D'
    beqz  $ra, isDoubleS        # branch if result type char == 'D'
    addiu $ra, 'D' - 'F'
    beqz  $ra, isSingleS        # branch if result type char == 'F'

    addu  $ra, $t2, $t6
    jalr  $zero, $ra
    addiu $t8, 4                # next_arg = curr_arg + 4

isLongS:
    addu  $ra, $t3, $t6
    jalr  $zero, $ra
    addiu $t8, 8                # next_arg = curr_arg + 8

isSingleS:
    addu  $ra, $t4, $t7
    jalr  $zero, $ra
    addiu $t8, 4                # next_arg = curr_arg + 4

isDoubleS:
    addu  $ra, $t5, $t7
#if defined(__mips_isa_rev) && __mips_isa_rev > 2
    addiu $t7, 16               # fp_index += 16 didn't fit into LOAD_DOUBLE_TO_REG
#endif
    jalr  $zero, $ra
    addiu $t8, 8                # next_arg = curr_arg + 8

loopEndS:
    lw    $t9, ART_METHOD_QUICK_CODE_OFFSET_32($a0)  # get pointer to the code
    jalr  $t9                   # call the method
    sw    $zero, 0($sp)         # store null for ArtMethod* at bottom of frame
    move  $sp, $fp              # restore the stack
    lw    $s0, 0($sp)
    .cfi_restore 16
    lw    $s1, 4($sp)
    .cfi_restore 17
    lw    $fp, 8($sp)
    .cfi_restore 30
    lw    $ra, 12($sp)
    .cfi_restore 31
    addiu $sp, $sp, SPILL_SIZE
    .cfi_adjust_cfa_offset -SPILL_SIZE
    lw    $t0, 16($sp)          # get result pointer
    lw    $t1, 20($sp)          # get shorty
    lb    $t1, 0($t1)           # get result type char
    li    $t2, 'D'              # put char 'D' into t2
    beq   $t1, $t2, 6f          # branch if result type char == 'D'
    li    $t3, 'F'              # put char 'F' into t3
    beq   $t1, $t3, 6f          # branch if result type char == 'F'
    sw    $v0, 0($t0)           # store the result
    jalr  $zero, $ra
    sw    $v1, 4($t0)           # store the other half of the result
6:
    SDu   $f0, $f1, 0, $t0, $t1 # store floating point result
    jalr  $zero, $ra
    nop

    // Note that gpr_index is kept within the range of tabIntS and tabLongS
    // and fp_index is kept within the range of tabSingleS and tabDoubleS.
    .balign 16
tabIntS:
    LOAD_WORD_TO_REG a1, t8, t6, loopS             # a1 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG a2, t8, t6, loopS             # a2 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG a3, t8, t6, loopS             # a3 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG t0, t8, t6, loopS             # t0 = current argument, gpr_index += 16
    LOAD_WORD_TO_REG t1, t8, t6, loopS             # t1 = current argument, gpr_index += 16
    LOAD_END t6, 5*16, loopS                       # no more GPR args, gpr_index = 5*16
tabLongS:
    LOAD_LONG_TO_REG a2, a3, t8, t6, 3*16, loopS   # a2_a3 = curr_arg, gpr_index = 3*16
    LOAD_LONG_TO_REG a2, a3, t8, t6, 3*16, loopS   # a2_a3 = curr_arg, gpr_index = 3*16
    LOAD_LONG_TO_REG t0, t1, t8, t6, 5*16, loopS   # t0_t1 = curr_arg, gpr_index = 5*16
    LOAD_LONG_TO_REG t0, t1, t8, t6, 5*16, loopS   # t0_t1 = curr_arg, gpr_index = 5*16
    LOAD_END t6, 5*16, loopS                       # no more GPR args, gpr_index = 5*16
    LOAD_END t6, 5*16, loopS                       # no more GPR args, gpr_index = 5*16
tabSingleS:
    LOAD_FLOAT_TO_REG f8, t8, t7, loopS            # f8 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f10, t8, t7, loopS           # f10 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f12, t8, t7, loopS           # f12 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f14, t8, t7, loopS           # f14 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f16, t8, t7, loopS           # f16 = curr_arg, fp_index += 16
    LOAD_FLOAT_TO_REG f18, t8, t7, loopS           # f18 = curr_arg, fp_index += 16
    LOAD_END t7, 6*16, loopS                       # no more FPR args, fp_index = 6*16
tabDoubleS:
    LOAD_DOUBLE_TO_REG f8, f9, t8, t7, ra, loopS   # f8_f9 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f10, f11, t8, t7, ra, loopS # f10_f11 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f12, f13, t8, t7, ra, loopS # f12_f13 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f14, f15, t8, t7, ra, loopS # f14_f15 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f16, f17, t8, t7, ra, loopS # f16_f17 = curr_arg; if FPU32, fp_index += 16
    LOAD_DOUBLE_TO_REG f18, f19, t8, t7, ra, loopS # f18_f19 = curr_arg; if FPU32, fp_index += 16
    LOAD_END t7, 6*16, loopS                       # no more FPR args, fp_index = 6*16
END art_quick_invoke_static_stub

#undef SPILL_SIZE

    /*
     * Entry from managed code that calls artHandleFillArrayDataFromCode and delivers exception on
     * failure.
     */
    .extern artHandleFillArrayDataFromCode
ENTRY art_quick_handle_fill_data
    lw     $a2, 0($sp)                # pass referrer's Method*
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case exception allocation triggers GC
    la     $t9, artHandleFillArrayDataFromCode
    jalr   $t9                        # (payload offset, Array*, method, Thread*)
    move   $a3, rSELF                 # pass Thread::Current
    RETURN_IF_ZERO
END art_quick_handle_fill_data

    /*
     * Entry from managed code that calls artLockObjectFromCode, may block for GC.
     */
    .extern artLockObjectFromCode
ENTRY art_quick_lock_object
    beqz    $a0, art_quick_throw_null_pointer_exception
    nop
    SETUP_SAVE_REFS_ONLY_FRAME            # save callee saves in case we block
    la      $t9, artLockObjectFromCode
    jalr    $t9                           # (Object* obj, Thread*)
    move    $a1, rSELF                    # pass Thread::Current
    RETURN_IF_ZERO
END art_quick_lock_object

ENTRY art_quick_lock_object_no_inline
    beqz    $a0, art_quick_throw_null_pointer_exception
    nop
    SETUP_SAVE_REFS_ONLY_FRAME            # save callee saves in case we block
    la      $t9, artLockObjectFromCode
    jalr    $t9                           # (Object* obj, Thread*)
    move    $a1, rSELF                    # pass Thread::Current
    RETURN_IF_ZERO
END art_quick_lock_object_no_inline

    /*
     * Entry from managed code that calls artUnlockObjectFromCode and delivers exception on failure.
     */
    .extern artUnlockObjectFromCode
ENTRY art_quick_unlock_object
    beqz    $a0, art_quick_throw_null_pointer_exception
    nop
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case exception allocation triggers GC
    la      $t9, artUnlockObjectFromCode
    jalr    $t9                       # (Object* obj, Thread*)
    move    $a1, rSELF                # pass Thread::Current
    RETURN_IF_ZERO
END art_quick_unlock_object

ENTRY art_quick_unlock_object_no_inline
    beqz    $a0, art_quick_throw_null_pointer_exception
    nop
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case exception allocation triggers GC
    la      $t9, artUnlockObjectFromCode
    jalr    $t9                       # (Object* obj, Thread*)
    move    $a1, rSELF                # pass Thread::Current
    RETURN_IF_ZERO
END art_quick_unlock_object_no_inline

    /*
     * Entry from managed code that calls artInstanceOfFromCode and delivers exception on failure.
     */
    .extern artInstanceOfFromCode
    .extern artThrowClassCastExceptionForObject
ENTRY art_quick_check_instance_of
    addiu  $sp, $sp, -32
    .cfi_adjust_cfa_offset 32
    sw     $gp, 16($sp)
    sw     $ra, 12($sp)
    .cfi_rel_offset 31, 12
    sw     $t9, 8($sp)
    sw     $a1, 4($sp)
    sw     $a0, 0($sp)
    la     $t9, artInstanceOfFromCode
    jalr   $t9
    addiu  $sp, $sp, -16             # reserve argument slots on the stack
    addiu  $sp, $sp, 16
    lw     $gp, 16($sp)
    beqz   $v0, .Lthrow_class_cast_exception
    lw     $ra, 12($sp)
    jalr   $zero, $ra
    addiu  $sp, $sp, 32
    .cfi_adjust_cfa_offset -32
.Lthrow_class_cast_exception:
    lw     $t9, 8($sp)
    lw     $a1, 4($sp)
    lw     $a0, 0($sp)
    addiu  $sp, $sp, 32
    .cfi_adjust_cfa_offset -32
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    la   $t9, artThrowClassCastExceptionForObject
    jalr $zero, $t9                 # artThrowClassCastException (Object*, Class*, Thread*)
    move $a2, rSELF                 # pass Thread::Current
END art_quick_check_instance_of

    /*
     * Restore rReg's value from offset($sp) if rReg is not the same as rExclude.
     * nReg is the register number for rReg.
     */
.macro POP_REG_NE rReg, nReg, offset, rExclude
    .ifnc \rReg, \rExclude
        lw \rReg, \offset($sp)      # restore rReg
        .cfi_restore \nReg
    .endif
.endm

    /*
     * Macro to insert read barrier, only used in art_quick_aput_obj.
     * rObj and rDest are registers, offset is a defined literal such as MIRROR_OBJECT_CLASS_OFFSET.
     * TODO: When read barrier has a fast path, add heap unpoisoning support for the fast path.
     */
.macro READ_BARRIER rDest, rObj, offset
#ifdef USE_READ_BARRIER
    # saved registers used in art_quick_aput_obj: a0-a2, t0-t1, t9, ra. 8 words for 16B alignment.
    addiu  $sp, $sp, -32
    .cfi_adjust_cfa_offset 32
    sw     $ra, 28($sp)
    .cfi_rel_offset 31, 28
    sw     $t9, 24($sp)
    .cfi_rel_offset 25, 24
    sw     $t1, 20($sp)
    .cfi_rel_offset 9, 20
    sw     $t0, 16($sp)
    .cfi_rel_offset 8, 16
    sw     $a2, 8($sp)              # padding slot at offset 12 (padding can be any slot in the 32B)
    .cfi_rel_offset 6, 8
    sw     $a1, 4($sp)
    .cfi_rel_offset 5, 4
    sw     $a0, 0($sp)
    .cfi_rel_offset 4, 0

    # move $a0, \rRef               # pass ref in a0 (no-op for now since parameter ref is unused)
    .ifnc \rObj, $a1
        move $a1, \rObj             # pass rObj
    .endif
    addiu  $a2, $zero, \offset      # pass offset
    la     $t9, artReadBarrierSlow
    jalr   $t9                      # artReadBarrierSlow(ref, rObj, offset)
    addiu  $sp, $sp, -16            # Use branch delay slot to reserve argument slots on the stack
                                    # before the call to artReadBarrierSlow.
    addiu  $sp, $sp, 16             # restore stack after call to artReadBarrierSlow
    # No need to unpoison return value in v0, artReadBarrierSlow() would do the unpoisoning.
    move \rDest, $v0                # save return value in rDest
                                    # (rDest cannot be v0 in art_quick_aput_obj)

    lw     $a0, 0($sp)              # restore registers except rDest
                                    # (rDest can only be t0 or t1 in art_quick_aput_obj)
    .cfi_restore 4
    lw     $a1, 4($sp)
    .cfi_restore 5
    lw     $a2, 8($sp)
    .cfi_restore 6
    POP_REG_NE $t0, 8, 16, \rDest
    POP_REG_NE $t1, 9, 20, \rDest
    lw     $t9, 24($sp)
    .cfi_restore 25
    lw     $ra, 28($sp)             # restore $ra
    .cfi_restore 31
    addiu  $sp, $sp, 32
    .cfi_adjust_cfa_offset -32
#else
    lw     \rDest, \offset(\rObj)
    UNPOISON_HEAP_REF \rDest
#endif  // USE_READ_BARRIER
.endm

#ifdef USE_READ_BARRIER
    .extern artReadBarrierSlow
#endif
ENTRY art_quick_aput_obj
    beqz $a2, .Ldo_aput_null
    nop
    READ_BARRIER $t0, $a0, MIRROR_OBJECT_CLASS_OFFSET
    READ_BARRIER $t1, $a2, MIRROR_OBJECT_CLASS_OFFSET
    READ_BARRIER $t0, $t0, MIRROR_CLASS_COMPONENT_TYPE_OFFSET
    bne $t1, $t0, .Lcheck_assignability  # value's type == array's component type - trivial assignability
    nop
.Ldo_aput:
    sll $a1, $a1, 2
    add $t0, $a0, $a1
    POISON_HEAP_REF $a2
    sw  $a2, MIRROR_OBJECT_ARRAY_DATA_OFFSET($t0)
    lw  $t0, THREAD_CARD_TABLE_OFFSET(rSELF)
    srl $t1, $a0, 7
    add $t1, $t1, $t0
    sb  $t0, ($t1)
    jalr $zero, $ra
    nop
.Ldo_aput_null:
    sll $a1, $a1, 2
    add $t0, $a0, $a1
    sw  $a2, MIRROR_OBJECT_ARRAY_DATA_OFFSET($t0)
    jalr $zero, $ra
    nop
.Lcheck_assignability:
    addiu  $sp, $sp, -32
    .cfi_adjust_cfa_offset 32
    sw     $ra, 28($sp)
    .cfi_rel_offset 31, 28
    sw     $gp, 16($sp)
    sw     $t9, 12($sp)
    sw     $a2, 8($sp)
    sw     $a1, 4($sp)
    sw     $a0, 0($sp)
    move   $a1, $t1
    move   $a0, $t0
    la     $t9, artIsAssignableFromCode
    jalr   $t9               # (Class*, Class*)
    addiu  $sp, $sp, -16     # reserve argument slots on the stack
    addiu  $sp, $sp, 16
    lw     $ra, 28($sp)
    lw     $gp, 16($sp)
    lw     $t9, 12($sp)
    lw     $a2, 8($sp)
    lw     $a1, 4($sp)
    lw     $a0, 0($sp)
    addiu  $sp, 32
    .cfi_adjust_cfa_offset -32
    bnez   $v0, .Ldo_aput
    nop
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    move $a1, $a2
    la   $t9, artThrowArrayStoreException
    jalr $zero, $t9                 # artThrowArrayStoreException(Class*, Class*, Thread*)
    move $a2, rSELF                 # pass Thread::Current
END art_quick_aput_obj

// Macros taking opportunity of code similarities for downcalls.
.macro ONE_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9                       # (field_idx, Thread*)
    move    $a1, rSELF                # pass Thread::Current
    \return                           # RETURN_IF_NO_EXCEPTION or RETURN_IF_ZERO
END \name
.endm

.macro TWO_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9                       # (field_idx, Object*, Thread*) or
                                      # (field_idx, new_val, Thread*)
    move    $a2, rSELF                # pass Thread::Current
    \return                           # RETURN_IF_NO_EXCEPTION or RETURN_IF_ZERO
END \name
.endm

.macro THREE_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9                       # (field_idx, Object*, new_val, Thread*)
    move    $a3, rSELF                # pass Thread::Current
    \return                           # RETURN_IF_NO_EXCEPTION or RETURN_IF_ZERO
END \name
.endm

.macro FOUR_ARG_REF_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9                       # (field_idx, Object*, 64-bit new_val, Thread*) or
                                      # (field_idx, 64-bit new_val, Thread*)
                                      # Note that a 64-bit new_val needs to be aligned with
                                      # an even-numbered register, hence A1 may be skipped
                                      # for new_val to reside in A2-A3.
    sw      rSELF, 16($sp)            # pass Thread::Current
    \return                           # RETURN_IF_NO_EXCEPTION or RETURN_IF_ZERO
END \name
.endm

    /*
     * Called by managed code to resolve a static/instance field and load/store a value.
     */
ONE_ARG_REF_DOWNCALL art_quick_get_byte_static, artGetByteStaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get_boolean_static, artGetBooleanStaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get_short_static, artGetShortStaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get_char_static, artGetCharStaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get32_static, artGet32StaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get_obj_static, artGetObjStaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
ONE_ARG_REF_DOWNCALL art_quick_get64_static, artGet64StaticFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get_byte_instance, artGetByteInstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get_boolean_instance, artGetBooleanInstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get_short_instance, artGetShortInstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get_char_instance, artGetCharInstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get32_instance, artGet32InstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get_obj_instance, artGetObjInstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_get64_instance, artGet64InstanceFromCompiledCode, RETURN_IF_NO_EXCEPTION
TWO_ARG_REF_DOWNCALL art_quick_set8_static, artSet8StaticFromCompiledCode, RETURN_IF_ZERO
TWO_ARG_REF_DOWNCALL art_quick_set16_static, artSet16StaticFromCompiledCode, RETURN_IF_ZERO
TWO_ARG_REF_DOWNCALL art_quick_set32_static, artSet32StaticFromCompiledCode, RETURN_IF_ZERO
TWO_ARG_REF_DOWNCALL art_quick_set_obj_static, artSetObjStaticFromCompiledCode, RETURN_IF_ZERO
FOUR_ARG_REF_DOWNCALL art_quick_set64_static, artSet64StaticFromCompiledCode, RETURN_IF_ZERO
THREE_ARG_REF_DOWNCALL art_quick_set8_instance, artSet8InstanceFromCompiledCode, RETURN_IF_ZERO
THREE_ARG_REF_DOWNCALL art_quick_set16_instance, artSet16InstanceFromCompiledCode, RETURN_IF_ZERO
THREE_ARG_REF_DOWNCALL art_quick_set32_instance, artSet32InstanceFromCompiledCode, RETURN_IF_ZERO
THREE_ARG_REF_DOWNCALL art_quick_set_obj_instance, artSetObjInstanceFromCompiledCode, RETURN_IF_ZERO
FOUR_ARG_REF_DOWNCALL art_quick_set64_instance, artSet64InstanceFromCompiledCode, RETURN_IF_ZERO

// Macro to facilitate adding new allocation entrypoints.
.macro ONE_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9
    move    $a1, rSELF                # pass Thread::Current
    \return
END \name
.endm

.macro TWO_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9
    move    $a2, rSELF                # pass Thread::Current
    \return
END \name
.endm

.macro THREE_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9
    move    $a3, rSELF                # pass Thread::Current
    \return
END \name
.endm

.macro FOUR_ARG_DOWNCALL name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        # save callee saves in case of GC
    la      $t9, \entrypoint
    jalr    $t9
    sw      rSELF, 16($sp)            # pass Thread::Current
    \return
END \name
.endm

// Generate the allocation entrypoints for each allocator.
GENERATE_ALLOC_ENTRYPOINTS_FOR_EACH_ALLOCATOR

// A hand-written override for:
//   GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_RESOLVED(_rosalloc, RosAlloc)
//   GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_INITIALIZED(_rosalloc, RosAlloc)
.macro ART_QUICK_ALLOC_OBJECT_ROSALLOC c_name, cxx_name
ENTRY \c_name
    # Fast path rosalloc allocation
    # a0: type
    # s1: Thread::Current
    # -----------------------------
    # t1: object size
    # t2: rosalloc run
    # t3: thread stack top offset
    # t4: thread stack bottom offset
    # v0: free list head
    #
    # t5, t6 : temps
    lw    $t3, THREAD_LOCAL_ALLOC_STACK_TOP_OFFSET($s1)        # Check if thread local allocation
    lw    $t4, THREAD_LOCAL_ALLOC_STACK_END_OFFSET($s1)        # stack has any room left.
    bgeu  $t3, $t4, .Lslow_path_\c_name

    lw    $t1, MIRROR_CLASS_OBJECT_SIZE_ALLOC_FAST_PATH_OFFSET($a0)  # Load object size (t1).
    li    $t5, ROSALLOC_MAX_THREAD_LOCAL_BRACKET_SIZE          # Check if size is for a thread local
                                                               # allocation. Also does the
                                                               # initialized and finalizable checks.
    bgtu  $t1, $t5, .Lslow_path_\c_name

    # Compute the rosalloc bracket index from the size. Since the size is already aligned we can
    # combine the two shifts together.
    srl   $t1, $t1, (ROSALLOC_BRACKET_QUANTUM_SIZE_SHIFT - POINTER_SIZE_SHIFT)

    addu  $t2, $t1, $s1
    lw    $t2, (THREAD_ROSALLOC_RUNS_OFFSET - __SIZEOF_POINTER__)($t2)  # Load rosalloc run (t2).

    # Load the free list head (v0).
    # NOTE: this will be the return val.
    lw    $v0, (ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_HEAD_OFFSET)($t2)
    beqz  $v0, .Lslow_path_\c_name
    nop

    # Load the next pointer of the head and update the list head with the next pointer.
    lw    $t5, ROSALLOC_SLOT_NEXT_OFFSET($v0)
    sw    $t5, (ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_HEAD_OFFSET)($t2)

    # Store the class pointer in the header. This also overwrites the first pointer. The offsets are
    # asserted to match.

#if ROSALLOC_SLOT_NEXT_OFFSET != MIRROR_OBJECT_CLASS_OFFSET
#error "Class pointer needs to overwrite next pointer."
#endif

    POISON_HEAP_REF $a0
    sw    $a0, MIRROR_OBJECT_CLASS_OFFSET($v0)

    # Push the new object onto the thread local allocation stack and increment the thread local
    # allocation stack top.
    sw    $v0, 0($t3)
    addiu $t3, $t3, COMPRESSED_REFERENCE_SIZE
    sw    $t3, THREAD_LOCAL_ALLOC_STACK_TOP_OFFSET($s1)

    # Decrement the size of the free list.
    lw    $t5, (ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_SIZE_OFFSET)($t2)
    addiu $t5, $t5, -1
    sw    $t5, (ROSALLOC_RUN_FREE_LIST_OFFSET + ROSALLOC_RUN_FREE_LIST_SIZE_OFFSET)($t2)

    sync                                                          # Fence.

    jalr  $zero, $ra
    nop

  .Lslow_path_\c_name:
    SETUP_SAVE_REFS_ONLY_FRAME
    la    $t9, \cxx_name
    jalr  $t9
    move  $a1, $s1                                                # Pass self as argument.
    RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER
END \c_name
.endm

ART_QUICK_ALLOC_OBJECT_ROSALLOC art_quick_alloc_object_resolved_rosalloc, artAllocObjectFromCodeResolvedRosAlloc
ART_QUICK_ALLOC_OBJECT_ROSALLOC art_quick_alloc_object_initialized_rosalloc, artAllocObjectFromCodeInitializedRosAlloc

GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_RESOLVED(_tlab, TLAB)
GENERATE_ALLOC_ENTRYPOINTS_ALLOC_OBJECT_RESOLVED(_region_tlab, RegionTLAB)

    /*
     * Entry from managed code to resolve a string, this stub will allocate a String and deliver an
     * exception on error. On success the String is returned. A0 holds the string index. The fast
     * path check for hit in strings cache has already been performed.
     */
ONE_ARG_DOWNCALL art_quick_resolve_string, artResolveStringFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

    /*
     * Entry from managed code when uninitialized static storage, this stub will run the class
     * initializer and deliver the exception on error. On success the static storage base is
     * returned.
     */
ONE_ARG_DOWNCALL art_quick_initialize_static_storage, artInitializeStaticStorageFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

    /*
     * Entry from managed code when dex cache misses for a type_idx.
     */
ONE_ARG_DOWNCALL art_quick_initialize_type, artInitializeTypeFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

    /*
     * Entry from managed code when type_idx needs to be checked for access and dex cache may also
     * miss.
     */
ONE_ARG_DOWNCALL art_quick_initialize_type_and_verify_access, artInitializeTypeAndVerifyAccessFromCode, RETURN_IF_RESULT_IS_NON_ZERO_OR_DELIVER

    /*
     * Called by managed code when the value in rSUSPEND has been decremented to 0.
     */
    .extern artTestSuspendFromCode
ENTRY_NO_GP art_quick_test_suspend
    lh     rSUSPEND, THREAD_FLAGS_OFFSET(rSELF)
    bnez   rSUSPEND, 1f
    addiu  rSUSPEND, $zero, SUSPEND_CHECK_INTERVAL   # reset rSUSPEND to SUSPEND_CHECK_INTERVAL
    jalr   $zero, $ra
    nop
1:
    SETUP_SAVE_EVERYTHING_FRAME                      # save everything for stack crawl
    la     $t9, artTestSuspendFromCode
    jalr   $t9                                       # (Thread*)
    move   $a0, rSELF
    RESTORE_SAVE_EVERYTHING_FRAME
    jalr   $zero, $ra
    nop
END art_quick_test_suspend

    /*
     * Called by managed code that is attempting to call a method on a proxy class. On entry
     * a0 holds the proxy method; a1, a2 and a3 may contain arguments.
     */
    .extern artQuickProxyInvokeHandler
ENTRY art_quick_proxy_invoke_handler
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    move    $a2, rSELF                  # pass Thread::Current
    la      $t9, artQuickProxyInvokeHandler
    jalr    $t9                         # (Method* proxy method, receiver, Thread*, SP)
    addiu   $a3, $sp, ARG_SLOT_SIZE     # pass $sp (remove arg slots)
    lw      $t7, THREAD_EXCEPTION_OFFSET(rSELF) # load Thread::Current()->exception_
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    bnez    $t7, 1f
    # don't care if $v0 and/or $v1 are modified, when exception branch taken
    MTD     $v0, $v1, $f0, $f1          # move float value to return value
    jalr    $zero, $ra
    nop
1:
    DELIVER_PENDING_EXCEPTION
END art_quick_proxy_invoke_handler

    /*
     * Called to resolve an imt conflict.
     * a0 is the conflict ArtMethod.
     * t7 is a hidden argument that holds the target interface method's dex method index.
     *
     * Note that this stub writes to a0, t7 and t8.
     */
ENTRY art_quick_imt_conflict_trampoline
    lw      $t8, 0($sp)                                      # Load referrer.
    lw      $t8, ART_METHOD_DEX_CACHE_METHODS_OFFSET_32($t8) # Load dex cache methods array.
    sll     $t7, $t7, POINTER_SIZE_SHIFT                     # Calculate offset.
    addu    $t7, $t8, $t7                                    # Add offset to base.
    lw      $t7, 0($t7)                                      # Load interface method.
    lw      $a0, ART_METHOD_JNI_OFFSET_32($a0)               # Load ImtConflictTable.

.Limt_table_iterate:
    lw      $t8, 0($a0)                                      # Load next entry in ImtConflictTable.
    # Branch if found.
    beq     $t8, $t7, .Limt_table_found
    nop
    # If the entry is null, the interface method is not in the ImtConflictTable.
    beqz    $t8, .Lconflict_trampoline
    nop
    # Iterate over the entries of the ImtConflictTable.
    b       .Limt_table_iterate
    addiu   $a0, $a0, 2 * __SIZEOF_POINTER__                 # Iterate to the next entry.

.Limt_table_found:
    # We successfully hit an entry in the table. Load the target method and jump to it.
    lw      $a0, __SIZEOF_POINTER__($a0)
    lw      $t9, ART_METHOD_QUICK_CODE_OFFSET_32($a0)
    jalr    $zero, $t9
    nop

.Lconflict_trampoline:
    # Call the runtime stub to populate the ImtConflictTable and jump to the resolved method.
    move    $a0, $t7                                         # Load interface method.
    INVOKE_TRAMPOLINE_BODY artInvokeInterfaceTrampoline
END art_quick_imt_conflict_trampoline

    .extern artQuickResolutionTrampoline
ENTRY art_quick_resolution_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move    $a2, rSELF                    # pass Thread::Current
    la      $t9, artQuickResolutionTrampoline
    jalr    $t9                           # (Method* called, receiver, Thread*, SP)
    addiu   $a3, $sp, ARG_SLOT_SIZE       # pass $sp (remove arg slots)
    beqz    $v0, 1f
    lw      $a0, ARG_SLOT_SIZE($sp)       # load resolved method to $a0
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    move    $t9, $v0               # code pointer must be in $t9 to generate the global pointer
    jalr    $zero, $t9             # tail call to method
    nop
1:
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_resolution_trampoline

    .extern artQuickGenericJniTrampoline
    .extern artQuickGenericJniEndTrampoline
ENTRY art_quick_generic_jni_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    move    $s8, $sp               # save $sp to $s8
    move    $s3, $gp               # save $gp to $s3

    # prepare for call to artQuickGenericJniTrampoline(Thread*, SP)
    move    $a0, rSELF                     # pass Thread::Current
    addiu   $a1, $sp, ARG_SLOT_SIZE        # save $sp (remove arg slots)
    la      $t9, artQuickGenericJniTrampoline
    jalr    $t9                            # (Thread*, SP)
    addiu   $sp, $sp, -5120                # reserve space on the stack

    # The C call will have registered the complete save-frame on success.
    # The result of the call is:
    # v0: ptr to native code, 0 on error.
    # v1: ptr to the bottom of the used area of the alloca, can restore stack till here.
    beq     $v0, $zero, 2f         # check entry error
    move    $t9, $v0               # save the code ptr
    move    $sp, $v1               # release part of the alloca

    # Load parameters from stack into registers
    lw      $a0,   0($sp)
    lw      $a1,   4($sp)
    lw      $a2,   8($sp)
    lw      $a3,  12($sp)

    # artQuickGenericJniTrampoline sets bit 0 of the native code address to 1
    # when the first two arguments are both single precision floats. This lets
    # us extract them properly from the stack and load into floating point
    # registers.
    MTD     $a0, $a1, $f12, $f13
    andi    $t0, $t9, 1
    xor     $t9, $t9, $t0
    bnez    $t0, 1f
    mtc1    $a1, $f14
    MTD     $a2, $a3, $f14, $f15

1:
    jalr    $t9                    # native call
    nop
    addiu   $sp, $sp, 16           # remove arg slots

    move    $gp, $s3               # restore $gp from $s3

    # result sign extension is handled in C code
    # prepare for call to artQuickGenericJniEndTrampoline(Thread*, result, result_f)
    move    $a0, rSELF             # pass Thread::Current
    move    $a2, $v0               # pass result
    move    $a3, $v1
    addiu   $sp, $sp, -24          # reserve arg slots
    la      $t9, artQuickGenericJniEndTrampoline
    jalr    $t9
    s.d     $f0, 16($sp)           # pass result_f

    lw      $t0, THREAD_EXCEPTION_OFFSET(rSELF) # load Thread::Current()->exception_
    bne     $t0, $zero, 2f         # check for pending exceptions

    move    $sp, $s8               # tear down the alloca

    # tear down the callee-save frame
    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    MTD     $v0, $v1, $f0, $f1     # move float value to return value
    jalr    $zero, $ra
    nop

2:
    lw $sp, THREAD_TOP_QUICK_FRAME_OFFSET(rSELF)
    # This will create a new save-all frame, required by the runtime.
    DELIVER_PENDING_EXCEPTION
END art_quick_generic_jni_trampoline

    .extern artQuickToInterpreterBridge
ENTRY art_quick_to_interpreter_bridge
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move    $a1, rSELF                          # pass Thread::Current
    la      $t9, artQuickToInterpreterBridge
    jalr    $t9                                 # (Method* method, Thread*, SP)
    addiu   $a2, $sp, ARG_SLOT_SIZE             # pass $sp (remove arg slots)
    lw      $t7, THREAD_EXCEPTION_OFFSET(rSELF) # load Thread::Current()->exception_
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    bnez    $t7, 1f
    # don't care if $v0 and/or $v1 are modified, when exception branch taken
    MTD     $v0, $v1, $f0, $f1                  # move float value to return value
    jalr    $zero, $ra
    nop
1:
    DELIVER_PENDING_EXCEPTION
END art_quick_to_interpreter_bridge

    .extern artInvokeObsoleteMethod
ENTRY art_invoke_obsolete_method_stub
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    la      $t9, artInvokeObsoleteMethod
    jalr    $t9                                 # (Method* method, Thread* self)
    move    $a1, rSELF                          # pass Thread::Current
END art_invoke_obsolete_method_stub

    /*
     * Routine that intercepts method calls and returns.
     */
    .extern artInstrumentationMethodEntryFromCode
    .extern artInstrumentationMethodExitFromCode
ENTRY art_quick_instrumentation_entry
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    sw       $a0, 28($sp)   # save arg0 in free arg slot
    move     $a3, $ra       # pass $ra
    la       $t9, artInstrumentationMethodEntryFromCode
    jalr     $t9            # (Method*, Object*, Thread*, LR)
    move     $a2, rSELF     # pass Thread::Current
    move     $t9, $v0       # $t9 holds reference to code
    lw       $a0, 28($sp)   # restore arg0 from free arg slot
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    jalr     $t9            # call method
    nop
END art_quick_instrumentation_entry
    /* intentional fallthrough */
    .global art_quick_instrumentation_exit
art_quick_instrumentation_exit:
    .cfi_startproc
    addiu    $t9, $ra, 4    # put current address into $t9 to rebuild $gp
    .cpload  $t9
    move     $ra, $zero     # link register is to here, so clobber with 0 for later checks

    SETUP_SAVE_REFS_ONLY_FRAME
    addiu    $sp, $sp, -16  # allocate temp storage on the stack
    .cfi_adjust_cfa_offset 16
    sw       $v0, ARG_SLOT_SIZE+12($sp)
    .cfi_rel_offset 2, ARG_SLOT_SIZE+12
    sw       $v1, ARG_SLOT_SIZE+8($sp)
    .cfi_rel_offset 3, ARG_SLOT_SIZE+8
    s.d      $f0, ARG_SLOT_SIZE($sp)
    s.d      $f0, 16($sp)   # pass fpr result
    move     $a2, $v0       # pass gpr result
    move     $a3, $v1
    addiu    $a1, $sp, ARG_SLOT_SIZE+16   # pass $sp (remove arg slots and temp storage)
    la       $t9, artInstrumentationMethodExitFromCode
    jalr     $t9            # (Thread*, SP, gpr_res, fpr_res)
    move     $a0, rSELF     # pass Thread::Current
    move     $t9, $v0       # set aside returned link register
    move     $ra, $v1       # set link register for deoptimization
    lw       $v0, ARG_SLOT_SIZE+12($sp)   # restore return values
    lw       $v1, ARG_SLOT_SIZE+8($sp)
    l.d      $f0, ARG_SLOT_SIZE($sp)
    jalr     $zero, $t9     # return
    addiu    $sp, $sp, ARG_SLOT_SIZE+FRAME_SIZE_SAVE_REFS_ONLY+16  # restore stack
    .cfi_adjust_cfa_offset -(ARG_SLOT_SIZE+FRAME_SIZE_SAVE_REFS_ONLY+16)
END art_quick_instrumentation_exit

    /*
     * Instrumentation has requested that we deoptimize into the interpreter. The deoptimization
     * will long jump to the upcall with a special exception of -1.
     */
    .extern artDeoptimize
ENTRY art_quick_deoptimize
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    la       $t9, artDeoptimize
    jalr     $t9            # (Thread*)
    move     $a0, rSELF     # pass Thread::current
END art_quick_deoptimize

    /*
     * Compiled code has requested that we deoptimize into the interpreter. The deoptimization
     * will long jump to the upcall with a special exception of -1.
     */
    .extern artDeoptimizeFromCompiledCode
ENTRY art_quick_deoptimize_from_compiled_code
    SETUP_SAVE_EVERYTHING_FRAME
    la       $t9, artDeoptimizeFromCompiledCode
    jalr     $t9                            # (DeoptimizationKind, Thread*)
    move     $a1, rSELF                     # pass Thread::current
END art_quick_deoptimize_from_compiled_code

    /*
     * Long integer shift.  This is different from the generic 32/64-bit
     * binary operations because vAA/vBB are 64-bit but vCC (the shift
     * distance) is 32-bit.  Also, Dalvik requires us to ignore all but the low
     * 6 bits.
     * On entry:
     *   $a0: low word
     *   $a1: high word
     *   $a2: shift count
     */
ENTRY_NO_GP art_quick_shl_long
    /* shl-long vAA, vBB, vCC */
    sll     $v0, $a0, $a2                    #  rlo<- alo << (shift&31)
    not     $v1, $a2                         #  rhi<- 31-shift  (shift is 5b)
    srl     $a0, 1
    srl     $a0, $v1                         #  alo<- alo >> (32-(shift&31))
    sll     $v1, $a1, $a2                    #  rhi<- ahi << (shift&31)
    andi    $a2, 0x20                        #  shift< shift & 0x20
    beqz    $a2, 1f
    or      $v1, $a0                         #  rhi<- rhi | alo

    move    $v1, $v0                         #  rhi<- rlo (if shift&0x20)
    move    $v0, $zero                       #  rlo<- 0 (if shift&0x20)

1:  jalr    $zero, $ra
    nop
END art_quick_shl_long

    /*
     * Long integer shift.  This is different from the generic 32/64-bit
     * binary operations because vAA/vBB are 64-bit but vCC (the shift
     * distance) is 32-bit.  Also, Dalvik requires us to ignore all but the low
     * 6 bits.
     * On entry:
     *   $a0: low word
     *   $a1: high word
     *   $a2: shift count
     */
ENTRY_NO_GP art_quick_shr_long
    sra     $v1, $a1, $a2                    #  rhi<- ahi >> (shift&31)
    srl     $v0, $a0, $a2                    #  rlo<- alo >> (shift&31)
    sra     $a3, $a1, 31                     #  $a3<- sign(ah)
    not     $a0, $a2                         #  alo<- 31-shift (shift is 5b)
    sll     $a1, 1
    sll     $a1, $a0                         #  ahi<- ahi << (32-(shift&31))
    andi    $a2, 0x20                        #  shift & 0x20
    beqz    $a2, 1f
    or      $v0, $a1                         #  rlo<- rlo | ahi

    move    $v0, $v1                         #  rlo<- rhi (if shift&0x20)
    move    $v1, $a3                         #  rhi<- sign(ahi) (if shift&0x20)

1:  jalr    $zero, $ra
    nop
END art_quick_shr_long

    /*
     * Long integer shift.  This is different from the generic 32/64-bit
     * binary operations because vAA/vBB are 64-bit but vCC (the shift
     * distance) is 32-bit.  Also, Dalvik requires us to ignore all but the low
     * 6 bits.
     * On entry:
     *   $a0: low word
     *   $a1: high word
     *   $a2: shift count
     */
    /* ushr-long vAA, vBB, vCC */
ENTRY_NO_GP art_quick_ushr_long
    srl     $v1, $a1, $a2                    #  rhi<- ahi >> (shift&31)
    srl     $v0, $a0, $a2                    #  rlo<- alo >> (shift&31)
    not     $a0, $a2                         #  alo<- 31-shift (shift is 5b)
    sll     $a1, 1
    sll     $a1, $a0                         #  ahi<- ahi << (32-(shift&31))
    andi    $a2, 0x20                        #  shift & 0x20
    beqz    $a2, 1f
    or      $v0, $a1                         #  rlo<- rlo | ahi

    move    $v0, $v1                         #  rlo<- rhi (if shift&0x20)
    move    $v1, $zero                       #  rhi<- 0 (if shift&0x20)

1:  jalr    $zero, $ra
    nop
END art_quick_ushr_long

/* java.lang.String.indexOf(int ch, int fromIndex=0) */
ENTRY_NO_GP art_quick_indexof
/* $a0 holds address of "this" */
/* $a1 holds "ch" */
/* $a2 holds "fromIndex" */
#if (STRING_COMPRESSION_FEATURE)
    lw    $a3, MIRROR_STRING_COUNT_OFFSET($a0)    # 'count' field of this
#else
    lw    $t0, MIRROR_STRING_COUNT_OFFSET($a0)    # this.length()
#endif
    slt   $t1, $a2, $zero # if fromIndex < 0
#if defined(_MIPS_ARCH_MIPS32R6)
    seleqz $a2, $a2, $t1  #     fromIndex = 0;
#else
    movn   $a2, $zero, $t1 #    fromIndex = 0;
#endif

#if (STRING_COMPRESSION_FEATURE)
    srl   $t0, $a3, 1     # $a3 holds count (with flag) and $t0 holds actual length
#endif
    subu  $t0, $t0, $a2   # this.length() - fromIndex
    blez  $t0, 6f         # if this.length()-fromIndex <= 0
    li    $v0, -1         #     return -1;

#if (STRING_COMPRESSION_FEATURE)
    sll   $a3, $a3, 31    # Extract compression flag.
    beqz  $a3, .Lstring_indexof_compressed
    move  $t2, $a0        # Save a copy in $t2 to later compute result (in branch delay slot).
#endif
    sll   $v0, $a2, 1     # $a0 += $a2 * 2
    addu  $a0, $a0, $v0   #  "  ditto  "
    move  $v0, $a2        # Set i to fromIndex.

1:
    lhu   $t3, MIRROR_STRING_VALUE_OFFSET($a0)    # if this.charAt(i) == ch
    beq   $t3, $a1, 6f                            #     return i;
    addu  $a0, $a0, 2     # i++
    subu  $t0, $t0, 1     # this.length() - i
    bnez  $t0, 1b         # while this.length() - i > 0
    addu  $v0, $v0, 1     # i++

    li    $v0, -1         # if this.length() - i <= 0
                          #     return -1;

6:
    j     $ra
    nop

#if (STRING_COMPRESSION_FEATURE)
.Lstring_indexof_compressed:
    addu  $a0, $a0, $a2   # $a0 += $a2

.Lstring_indexof_compressed_loop:
    lbu   $t3, MIRROR_STRING_VALUE_OFFSET($a0)
    beq   $t3, $a1, .Lstring_indexof_compressed_matched
    subu  $t0, $t0, 1
    bgtz  $t0, .Lstring_indexof_compressed_loop
    addu  $a0, $a0, 1

.Lstring_indexof_nomatch:
    jalr  $zero, $ra
    li    $v0, -1         # return -1;

.Lstring_indexof_compressed_matched:
    jalr  $zero, $ra
    subu  $v0, $a0, $t2   # return (current - start);
#endif
END art_quick_indexof

/* java.lang.String.compareTo(String anotherString) */
ENTRY_NO_GP art_quick_string_compareto
/* $a0 holds address of "this" */
/* $a1 holds address of "anotherString" */
    beq    $a0, $a1, .Lstring_compareto_length_diff   # this and anotherString are the same object
    move   $a3, $a2                                   # trick to return 0 (it returns a2 - a3)

#if (STRING_COMPRESSION_FEATURE)
    lw     $t0, MIRROR_STRING_COUNT_OFFSET($a0)   # 'count' field of this
    lw     $t1, MIRROR_STRING_COUNT_OFFSET($a1)   # 'count' field of anotherString
    sra    $a2, $t0, 1                            # this.length()
    sra    $a3, $t1, 1                            # anotherString.length()
#else
    lw     $a2, MIRROR_STRING_COUNT_OFFSET($a0)   # this.length()
    lw     $a3, MIRROR_STRING_COUNT_OFFSET($a1)   # anotherString.length()
#endif

    MINu   $t2, $a2, $a3
    # $t2 now holds min(this.length(),anotherString.length())

    # while min(this.length(),anotherString.length())-i != 0
    beqz   $t2, .Lstring_compareto_length_diff # if $t2==0
    nop                                        #     return (this.length() - anotherString.length())

#if (STRING_COMPRESSION_FEATURE)
    # Differ cases:
    sll    $t3, $t0, 31
    beqz   $t3, .Lstring_compareto_this_is_compressed
    sll    $t3, $t1, 31                           # In branch delay slot.
    beqz   $t3, .Lstring_compareto_that_is_compressed
    nop
    b      .Lstring_compareto_both_not_compressed
    nop

.Lstring_compareto_this_is_compressed:
    beqz   $t3, .Lstring_compareto_both_compressed
    nop
    /* If (this->IsCompressed() && that->IsCompressed() == false) */
.Lstring_compareto_loop_comparison_this_compressed:
    lbu    $t0, MIRROR_STRING_VALUE_OFFSET($a0)
    lhu    $t1, MIRROR_STRING_VALUE_OFFSET($a1)
    bne    $t0, $t1, .Lstring_compareto_char_diff
    addiu  $a0, $a0, 1    # point at this.charAt(i++) - compressed
    subu   $t2, $t2, 1    # new value of min(this.length(),anotherString.length())-i
    bnez   $t2, .Lstring_compareto_loop_comparison_this_compressed
    addiu  $a1, $a1, 2    # point at anotherString.charAt(i++) - uncompressed
    jalr   $zero, $ra
    subu   $v0, $a2, $a3  # return (this.length() - anotherString.length())

.Lstring_compareto_that_is_compressed:
    lhu    $t0, MIRROR_STRING_VALUE_OFFSET($a0)
    lbu    $t1, MIRROR_STRING_VALUE_OFFSET($a1)
    bne    $t0, $t1, .Lstring_compareto_char_diff
    addiu  $a0, $a0, 2    # point at this.charAt(i++) - uncompressed
    subu   $t2, $t2, 1    # new value of min(this.length(),anotherString.length())-i
    bnez   $t2, .Lstring_compareto_that_is_compressed
    addiu  $a1, $a1, 1    # point at anotherString.charAt(i++) - compressed
    jalr   $zero, $ra
    subu   $v0, $a2, $a3  # return (this.length() - anotherString.length())

.Lstring_compareto_both_compressed:
    lbu    $t0, MIRROR_STRING_VALUE_OFFSET($a0)
    lbu    $t1, MIRROR_STRING_VALUE_OFFSET($a1)
    bne    $t0, $t1, .Lstring_compareto_char_diff
    addiu  $a0, $a0, 1    # point at this.charAt(i++) - compressed
    subu   $t2, $t2, 1    # new value of min(this.length(),anotherString.length())-i
    bnez   $t2, .Lstring_compareto_both_compressed
    addiu  $a1, $a1, 1    # point at anotherString.charAt(i++) - compressed
    jalr   $zero, $ra
    subu   $v0, $a2, $a3  # return (this.length() - anotherString.length())
#endif

.Lstring_compareto_both_not_compressed:
    lhu    $t0, MIRROR_STRING_VALUE_OFFSET($a0)   # while this.charAt(i) == anotherString.charAt(i)
    lhu    $t1, MIRROR_STRING_VALUE_OFFSET($a1)
    bne    $t0, $t1, .Lstring_compareto_char_diff # if this.charAt(i) != anotherString.charAt(i)
                          #     return (this.charAt(i) - anotherString.charAt(i))
    addiu  $a0, $a0, 2    # point at this.charAt(i++)
    subu   $t2, $t2, 1    # new value of min(this.length(),anotherString.length())-i
    bnez   $t2, .Lstring_compareto_both_not_compressed
    addiu  $a1, $a1, 2    # point at anotherString.charAt(i++)

.Lstring_compareto_length_diff:
    jalr   $zero, $ra
    subu   $v0, $a2, $a3  # return (this.length() - anotherString.length())

.Lstring_compareto_char_diff:
    jalr   $zero, $ra
    subu   $v0, $t0, $t1  # return (this.charAt(i) - anotherString.charAt(i))
END art_quick_string_compareto

    /*
     * Create a function `name` calling the ReadBarrier::Mark routine,
     * getting its argument and returning its result through register
     * `reg`, saving and restoring all caller-save registers.
     */
.macro READ_BARRIER_MARK_REG name, reg
ENTRY \name
    /* TODO: optimizations: mark bit, forwarding. */
    addiu   $sp, $sp, -160      # includes 16 bytes of space for argument registers a0-a3
    .cfi_adjust_cfa_offset 160

    sw      $ra, 156($sp)
    .cfi_rel_offset 31, 156
    sw      $t8, 152($sp)
    .cfi_rel_offset 24, 152
    sw      $t7, 148($sp)
    .cfi_rel_offset 15, 148
    sw      $t6, 144($sp)
    .cfi_rel_offset 14, 144
    sw      $t5, 140($sp)
    .cfi_rel_offset 13, 140
    sw      $t4, 136($sp)
    .cfi_rel_offset 12, 136
    sw      $t3, 132($sp)
    .cfi_rel_offset 11, 132
    sw      $t2, 128($sp)
    .cfi_rel_offset 10, 128
    sw      $t1, 124($sp)
    .cfi_rel_offset 9, 124
    sw      $t0, 120($sp)
    .cfi_rel_offset 8, 120
    sw      $a3, 116($sp)
    .cfi_rel_offset 7, 116
    sw      $a2, 112($sp)
    .cfi_rel_offset 6, 112
    sw      $a1, 108($sp)
    .cfi_rel_offset 5, 108
    sw      $a0, 104($sp)
    .cfi_rel_offset 4, 104
    sw      $v1, 100($sp)
    .cfi_rel_offset 3, 100
    sw      $v0, 96($sp)
    .cfi_rel_offset 2, 96

    la      $t9, artReadBarrierMark

    sdc1    $f18, 88($sp)
    sdc1    $f16, 80($sp)
    sdc1    $f14, 72($sp)
    sdc1    $f12, 64($sp)
    sdc1    $f10, 56($sp)
    sdc1    $f8,  48($sp)
    sdc1    $f6,  40($sp)
    sdc1    $f4,  32($sp)
    sdc1    $f2,  24($sp)

    .ifnc \reg, $a0
      move  $a0, \reg           # pass obj from `reg` in a0
    .endif
    jalr    $t9                 # v0 <- artReadBarrierMark(obj)
    sdc1    $f0,  16($sp)       # in delay slot

    lw      $ra, 156($sp)
    .cfi_restore 31
    lw      $t8, 152($sp)
    .cfi_restore 24
    lw      $t7, 148($sp)
    .cfi_restore 15
    lw      $t6, 144($sp)
    .cfi_restore 14
    lw      $t5, 140($sp)
    .cfi_restore 13
    lw      $t4, 136($sp)
    .cfi_restore 12
    lw      $t3, 132($sp)
    .cfi_restore 11
    lw      $t2, 128($sp)
    .cfi_restore 10
    lw      $t1, 124($sp)
    .cfi_restore 9
    lw      $t0, 120($sp)
    .cfi_restore 8
    lw      $a3, 116($sp)
    .cfi_restore 7
    lw      $a2, 112($sp)
    .cfi_restore 6
    lw      $a1, 108($sp)
    .cfi_restore 5
    lw      $a0, 104($sp)
    .cfi_restore 4
    lw      $v1, 100($sp)
    .cfi_restore 3

    .ifnc \reg, $v0
      move  \reg, $v0           # `reg` <- v0
      lw    $v0, 96($sp)
      .cfi_restore 2
    .endif

    ldc1    $f18, 88($sp)
    ldc1    $f16, 80($sp)
    ldc1    $f14, 72($sp)
    ldc1    $f12, 64($sp)
    ldc1    $f10, 56($sp)
    ldc1    $f8,  48($sp)
    ldc1    $f6,  40($sp)
    ldc1    $f4,  32($sp)
    ldc1    $f2,  24($sp)
    ldc1    $f0,  16($sp)

    jalr    $zero, $ra
    addiu   $sp, $sp, 160
    .cfi_adjust_cfa_offset -160
END \name
.endm

// Note that art_quick_read_barrier_mark_regXX corresponds to register XX+1.
// ZERO (register 0) is reserved.
// AT (register 1) is reserved as a temporary/scratch register.
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg01, $v0
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg02, $v1
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg03, $a0
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg04, $a1
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg05, $a2
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg06, $a3
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg07, $t0
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg08, $t1
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg09, $t2
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg10, $t3
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg11, $t4
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg12, $t5
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg13, $t6
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg14, $t7
// S0 and S1 (registers 16 and 17) are reserved as suspended and thread registers.
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg17, $s2
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg18, $s3
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg19, $s4
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg20, $s5
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg21, $s6
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg22, $s7
// T8 and T9 (registers 24 and 25) are reserved as temporary/scratch registers.
// K0, K1, GP, SP (registers 26 - 29) are reserved.
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg29, $s8
// RA (register 31) is reserved.

.extern artInvokePolymorphic
ENTRY art_quick_invoke_polymorphic
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move  $a2, rSELF                          # Make $a2 an alias for the current Thread.
    addiu $a3, $sp, ARG_SLOT_SIZE             # Make $a3 a pointer to the saved frame context.
    sw    $zero, 20($sp)                      # Initialize JValue result.
    sw    $zero, 16($sp)
    la    $t9, artInvokePolymorphic
    jalr  $t9                                 # (result, receiver, Thread*, context)
    addiu $a0, $sp, 16                        # Make $a0 a pointer to the JValue result
.macro MATCH_RETURN_TYPE c, handler
    li    $t0, \c
    beq   $v0, $t0, \handler
.endm
    MATCH_RETURN_TYPE 'V', .Lcleanup_and_return
    MATCH_RETURN_TYPE 'L', .Lstore_int_result
    MATCH_RETURN_TYPE 'I', .Lstore_int_result
    MATCH_RETURN_TYPE 'J', .Lstore_long_result
    MATCH_RETURN_TYPE 'B', .Lstore_int_result
    MATCH_RETURN_TYPE 'C', .Lstore_char_result
    MATCH_RETURN_TYPE 'D', .Lstore_double_result
    MATCH_RETURN_TYPE 'F', .Lstore_float_result
    MATCH_RETURN_TYPE 'S', .Lstore_int_result
    MATCH_RETURN_TYPE 'Z', .Lstore_boolean_result
.purgem MATCH_RETURN_TYPE
    nop
    b .Lcleanup_and_return
    nop
.Lstore_boolean_result:
    b .Lcleanup_and_return
    lbu   $v0, 16($sp)                        # Move byte from JValue result to return value register.
.Lstore_char_result:
    b .Lcleanup_and_return
    lhu   $v0, 16($sp)                        # Move char from JValue result to return value register.
.Lstore_double_result:
.Lstore_float_result:
    LDu   $f0, $f1, 16, $sp, $t0              # Move double/float from JValue result to return value register.
    b .Lcleanup_and_return
    nop
.Lstore_long_result:
    lw    $v1, 20($sp)                        # Move upper bits from JValue result to return value register.
    // Fall-through for lower bits.
.Lstore_int_result:
    lw    $v0, 16($sp)                        # Move lower bits from JValue result to return value register.
    // Fall-through to clean up and return.
.Lcleanup_and_return:
    lw    $t7, THREAD_EXCEPTION_OFFSET(rSELF) # Load Thread::Current()->exception_
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    bnez  $t7, 1f                             # Success if no exception is pending.
    nop
    jalr  $zero, $ra
    nop
1:
    DELIVER_PENDING_EXCEPTION
END art_quick_invoke_polymorphic
