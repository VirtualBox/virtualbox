/* $Id: IEMAllN8veHlpA-arm64.S 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Native Recompiler Assembly Helpers, ARM64 variant.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*       Header Files                                                                                                             *
*********************************************************************************************************************************/
#include <iprt/asmdefs-arm.h>
#include <iprt/armv8.h>

#include "IEMInternal.h"
#include "IEMN8veRecompiler.h"
#include "IEMAssemblyOffsets.h"


/*********************************************************************************************************************************
*       Defined Constants And Macros                                                                                             *
*********************************************************************************************************************************/
#define IEM_HLP_FUNCTION_ALIGNMENT 0x20


/*********************************************************************************************************************************
*       External Functions                                                                                                       *
*********************************************************************************************************************************/
.extern NAME(iemThreadedFunc_BltIn_LogCpuStateWorker)


BEGINCODE

/**
 * This is the common prologue of a TB, saving all volatile registers
 * and creating the stack frame for saving temporary values.
 *
 * @param    pVCpu    (x0)      The cross-context vCPU structure pointer.
 * @param    pCpumCtx (x1)      The cross-context CPUM context structure pointer.
 * @param    pTbStart (x2)      The TB instruction start pointer.
 */
ALIGNCODE(IEM_HLP_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemNativeTbEntry
#ifdef RT_OS_DARWIN
        pacibsp
#endif
        /*
         * We set up a stack frame exactly like on x86, only we have to push the
         * return address our selves here.  We save all non-volatile registers.
         */
        /* Allocate space for saving registers and place x19+x20 at the bottom. */
        stp     x19, x20, [sp, #-IEMNATIVE_FRAME_SAVE_REG_SIZE]!
        /* Save x21 thru x28 (SP remains unchanged). */
        stp     x21, x22, [sp, #0x10]
        stp     x23, x24, [sp, #0x20]
        stp     x25, x26, [sp, #0x30]
        stp     x27, x28, [sp, #0x40]
        /* Save the BP (x29) and LR (x30) (ret address) registers at the top of the frame. */
        stp     x29, x30, [sp, #0x50]
        /* Set BP to point to the old BP stack address */
        add     x29, sp, #(IEMNATIVE_FRAME_SAVE_REG_SIZE - 16)
        /* Allocate the variable area from SP. */
        sub     sp, sp, #IEMNATIVE_FRAME_VAR_SIZE
        /* Load the fixed register values from parameters. */
        mov     IEMNATIVE_REG_FIXED_PVMCPU_ASM,   x0
        mov     IEMNATIVE_REG_FIXED_PCPUMCTX_ASM, x1
#ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
        /* Save the frame pointer to pvTbFramePointerR3 */
        str     x29, [IEMNATIVE_REG_FIXED_PVMCPU_ASM, #(VMCPU_OFF_iem + IEMCPU_OFF_pvTbFramePointerR3)]
#endif

        /*
         * Everything is done, jump to the start of the TB.
         */
        br      x2


/**
 * This does the epilogue of a TB, given the RBP for the frame and eax value to return.
 *
 * @param    pFrame  (x0)       The frame pointer.
 * @param    rc      (w1)       The return value.
 *
 * @note This doesn't really work for MSC since xmm6 thru xmm15 are non-volatile
 *       and since we don't save them in the TB prolog we'll potentially return
 *       with different values if any functions on the calling stack uses them
 *       as they're unlikely to restore them till they return.
 *
 *       For the GCC calling convention all xmm registers are volatile and the
 *       only worry would be someone fiddling the control bits of MXCSR or FCW
 *       without restoring them.  This is highly unlikely, unless we're doing
 *       it ourselves, I think.
 */
ALIGNCODE(IEM_HLP_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemNativeTbLongJmp
        /*
         * This must exactly match what iemNativeEmitEpilog does.
         */
        sub     sp, x0, #0x50
        ldp     x19, x20, [sp, #0x00]
        ldp     x21, x22, [sp, #0x10]
        ldp     x23, x24, [sp, #0x20]
        ldp     x25, x26, [sp, #0x30]
        ldp     x27, x28, [sp, #0x40]
        ldp     x29, x30, [sp, #0x50]       /* the pFrame address points to this entry */
        add     sp, sp, #0x60
        mov     w0, w1                      /* The return value */
#ifdef RT_OS_DARWIN
        retab
#else
        ret
#endif
        brk #1



#define IEMNATIVE_HLP_FRAME_SIZE (11 * 16)

/**
 * This is wrapper function that saves and restores all volatile registers
 * so the impact of inserting LogCpuState is minimal to the other TB code.
 */
ALIGNCODE(IEM_HLP_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemNativeHlpAsmSafeWrapLogCpuState
#ifdef RT_OS_DARWIN
        pacibsp
#endif

        /*
         * Save all volatile registers.
         */
        stp     x29, x30, [sp, #-IEMNATIVE_HLP_FRAME_SIZE]!
        stp      x0,  x1, [sp, #( 1 * 16)]
        stp      x2,  x3, [sp, #( 2 * 16)]
        stp      x4,  x5, [sp, #( 3 * 16)]
        stp      x5,  x6, [sp, #( 4 * 16)]
        stp      x7,  x8, [sp, #( 5 * 16)]
        stp      x9, x10, [sp, #( 6 * 16)]
        stp     x11, x12, [sp, #( 7 * 16)]
        stp     x13, x14, [sp, #( 8 * 16)]
        stp     x15, x16, [sp, #( 9 * 16)]
        stp     x17, x18, [sp, #(10 * 16)]

        /*
         * Move the pVCpu pointer from the fixed register to the first argument.
         */
        mov      x0, IEMNATIVE_REG_FIXED_PVMCPU_ASM

        /*
         * Call C function to do the actual work.
         */
        bl      NAME(iemThreadedFunc_BltIn_LogCpuStateWorker)

        /*
         * Restore volatile registers and return to the TB code.
         */
        ldp    x29, x30, [sp, #( 0 * 16)]
        ldp     x0,  x1, [sp, #( 1 * 16)]
        ldp     x2,  x3, [sp, #( 2 * 16)]
        ldp     x4,  x5, [sp, #( 3 * 16)]
        ldp     x5,  x6, [sp, #( 4 * 16)]
        ldp     x7,  x8, [sp, #( 5 * 16)]
        ldp     x9, x10, [sp, #( 6 * 16)]
        ldp    x11, x12, [sp, #( 7 * 16)]
        ldp    x13, x14, [sp, #( 8 * 16)]
        ldp    x15, x16, [sp, #( 9 * 16)]
        ldp    x17, x18, [sp, #(10 * 16)]
        add      sp, sp, #IEMNATIVE_HLP_FRAME_SIZE

#ifdef RT_OS_DARWIN
        retab
#else
        ret
#endif
        brk #1


/**
 * This is wrapper function that saves and restores all volatile registers
 * so the impact of inserting CheckTlbLookup is minimal to the other TB code.
 */
ALIGNCODE(IEM_HLP_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemNativeHlpAsmSafeWrapCheckTlbLookup
#ifdef RT_OS_DARWIN
        ; pacibsp - see retab comment below.
#endif

        /*
         * Save all volatile registers.
         */
        stp      x0,  x1, [sp, #-IEMNATIVE_HLP_FRAME_SIZE]!
        stp      x2,  x3, [sp, #( 1 * 16)]
        stp      x4,  x5, [sp, #( 2 * 16)]
        stp      x5,  x6, [sp, #( 3 * 16)]
        stp      x7,  x8, [sp, #( 4 * 16)]
        stp      x9, x10, [sp, #( 5 * 16)]
        stp     x11, x12, [sp, #( 6 * 16)]
        stp     x13, x14, [sp, #( 7 * 16)]
        stp     x15, x16, [sp, #( 8 * 16)]
        stp     x17, x18, [sp, #( 9 * 16)]
        stp     x29, x30, [sp, #(10 * 16)]                  /* At the top, save BP followed by LR. */
        add     x29, sp, #(IEMNATIVE_HLP_FRAME_SIZE - 16)   /* x29 (BP) = &SavedBP; Creates a traditional call frame. */

        /*
         * Load parameters off the stack.
         */
        ldp     x0, x1, [x29, #16]
        ldp     x2, x3, [x29, #32]

        /*
         * Call C function to do the actual work.
         */
        bl      NAME(iemNativeHlpCheckTlbLookup)

        /*
         * Restore volatile registers and return to the TB code.
         */
        ldp     x0,  x1, [sp, #( 0 * 16)]
        ldp     x2,  x3, [sp, #( 1 * 16)]
        ldp     x4,  x5, [sp, #( 2 * 16)]
        ldp     x5,  x6, [sp, #( 3 * 16)]
        ldp     x7,  x8, [sp, #( 4 * 16)]
        ldp     x9, x10, [sp, #( 5 * 16)]
        ldp    x11, x12, [sp, #( 6 * 16)]
        ldp    x13, x14, [sp, #( 7 * 16)]
        ldp    x15, x16, [sp, #( 8 * 16)]
        ldp    x17, x18, [sp, #( 9 * 16)]
        ldp    x29, x30, [sp, #(10 * 16)]
        add      sp, sp, #(IEMNATIVE_HLP_FRAME_SIZE + 32)

#ifdef RT_OS_DARWIN
        ; retab - doesn't work with the wrong SP (+32 compared to the original).
        ret
#else
        ret
#endif
        brk #1


/**
 * Restores the FPCR register from the given argument.
 *
 * @param    u64RegFpCtrl    (x0)      The value to restore FPCR with.
 */
ALIGNCODE(IEM_HLP_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemNativeFpCtrlRegRestore
#ifdef RT_OS_DARWIN
        pacibsp
#endif

        msr FPCR, x0

#ifdef RT_OS_DARWIN
        retab
#else
        ret
#endif
