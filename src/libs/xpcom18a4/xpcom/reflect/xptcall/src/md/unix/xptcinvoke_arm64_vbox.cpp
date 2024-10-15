/* $Id: xptcinvoke_arm64_vbox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM - Implementation XPTC_InvokeByIndex for arm64.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "xptcprivate.h"
#include <iprt/cdefs.h>
#include <iprt/alloca.h>
#include <iprt/assert.h>

#include "xptc_arm64_vbox.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MY_MAX_ARGS         64  /**< Limit ourselves to 64 arguments. */

#define NSXPTC_VARIANT_SIZE    (8 + 8 + 8)
#define NSXPTC_VARIANT_PTR_OFF 8

AssertCompileSize(nsXPTCVariant, NSXPTC_VARIANT_SIZE);
AssertCompileMemberOffset(nsXPTCVariant, ptr, NSXPTC_VARIANT_PTR_OFF);
AssertCompileMemberOffset(nsXPTCVariant, val, 0);


/*
 * clang supports the naked attribute on arm64 whereas gcc does not, so we have to resort to this ugliness
 * in order to support both and not require a separate assembly file so we need only one set of defines...
 * Luckily this abormination is contained in this file.
 */
extern "C" nsresult
arm64AsmInvoker(uintptr_t pfnMethod /*x0*/, uint32_t cParams /*w1*/, nsXPTCVariant *paParams /*x2*/, uint64_t cbStack /*x3*/,
                uint8_t *acbStackArgs /*x4*/, uint64_t *pauGprArgs /*x5*/, uint64_t *pauFprArgs /*x6*/, uint32_t cFprArgs /*x7*/);

__asm__ (
"BEGINCODE\n"
"BEGINPROC_HIDDEN arm64AsmInvoker\n"
        ".cfi_startproc\n"
        /* Prologue - create the frame. */ "\
        sub     sp, sp, 16 \n\
        stp     x29, x30, [sp] \n\
        mov     x29, sp \n\
        .cfi_def_cfa        x29, 16 \n\
        .cfi_rel_offset     x30, -8 \n\
        .cfi_rel_offset     x29, -16 \n\
\n\
"       /* Move pfnMethod to x16 and pauGprArgs to x7 free up x0 and x5: */ "\
        mov     x16, x0 \n\
        mov     x17, x5 \n\
\n\
"       /* Load FPU registers first so we free up x6 & x7 early: */ "\
        cmp     w7, #0 \n\
        b.eq    Lno_fprs\n\
        ldp     d0, d1, [x6] \n\
        ldp     d2, d3, [x6, #16] \n\
        ldp     d4, d5, [x6, #32] \n\
        ldp     d6, d7, [x6, #48] \n\
Lno_fprs:\n\
\n\
"       /* Do argument passing by stack (if any).  We align the stack to 16 bytes.  */ "\
        cmp     x3, #0 \n\
        beq     Lno_stack_args \n\
        sub     x3, sp, x3 \n\
        bic     x3, x3, #15 \n\
        mov     sp, x3 \n\
Lnext_parameter: \n\
        ldrb    w7, [x4] \n\
        cmp     w7, #0 \n\
        beq     Ladvance\n\
\n\
        cmp     w7, #4 \n\
        bgt     Lstore_64bits\n\
        cmp     w7, #1 \n\
        beq     Lstore_8bits\n\
        cmp     w7, #2 \n\
        beq     Lstore_16bits\n\
\n\
Lstore_32bits:\n\
        ldr     w0, [x2] \n\
        add     x3, x3, #3 \n\
        bic     x3, x3, #3 \n\
        str     w0, [x3] \n"
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) /* macOS compacts stack usage. */
"       add     x3, x3, #4 \n"
#endif
"       b       Ladvance \n\
\n\
Lstore_8bits:\n\
        ldrb    w0, [x2] \n\
        strb    w0, [x3] \n"
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) /* macOS compacts stack usage. */
"       add     x3, x3, #1 \n"
#endif
"       b       Ladvance \n\
\n\
Lstore_16bits:\n\
        ldrh    w0, [x2] \n\
        add     x3, x3, #1 \n\
        bic     x3, x3, #1 \n\
        strh    w0, [x3] \n"
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) /* macOS compacts stack usage. */
"       add     x3, x3, #2 \n"
#endif
"       b       Ladvance \n\
\n\
Lstore_64bits_ptr:\n\
        ldr     x0, [x2, #" RT_XSTR(NSXPTC_VARIANT_PTR_OFF) "] \n\
        b       Lstore_64bits_common \n\
Lstore_64bits:\n\
        tst     w7, #0x80 \n\
        bne     Lstore_64bits_ptr \n\
        ldr     x0, [x2] \n\
Lstore_64bits_common:\n\
        add     x3, x3, #7 \n\
        bic     x3, x3, #7 \n\
        str     x0, [x3] \n"
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) /* macOS compacts stack usage. */
"       add     x3, x3, #8 \n"
#endif
"\n\
Ladvance:\n"
#if !defined(RT_OS_DARWIN) && !defined(RT_OS_LINUX) /* macOS compacts stack usage. */
"       add     x3, x3, #8 \n"
#endif
"       add     x4, x4, #1 \n\
        add     x2, x2, # " RT_XSTR(NSXPTC_VARIANT_SIZE) " \n\
        sub     w1, w1, #1 \n\
        cmp     w1, #0 \n\
        bne     Lnext_parameter \n\
\n\
"       /* reserve stack space for the integer and floating point registers and save them: */ "\
Lno_stack_args: \n\
\n\
"       /* Load general purpose argument registers: */ "\
        ldp     x0, x1, [x17] \n\
        ldp     x2, x3, [x17, #16] \n\
        ldp     x4, x5, [x17, #32] \n\
        ldp     x6, x7, [x17, #48] \n\
\n\
"       /* Make the call: */ "\
        blr     x16 \n\
\n\
"       /* Epilogue (clang does not emit the .cfi's here, so drop them too?): */ "\
        mov     sp, x29 \n\
        ldp     x29, x30, [sp] \n\
        add     sp, sp, #16 \n\
        .cfi_def_cfa sp, 0 \n\
        .cfi_restore x29 \n\
        .cfi_restore x30 \n\
        ret \n"
        ".cfi_endproc\n"
);


XPTC_PUBLIC_API(nsresult)
XPTC_InvokeByIndex(nsISupports *pThis, PRUint32 idxMethod, PRUint32 cParams, nsXPTCVariant *paParams)
{
    AssertMsgReturn(cParams <= MY_MAX_ARGS, ("cParams=%#x idxMethod=%#x\n", cParams, idxMethod), NS_ERROR_UNEXPECTED);

    /*
     * Prepare
     */
    uint64_t auGprArgs[NUM_ARGS_IN_GPRS] = {0};
    uint64_t auFprArgs[NUM_ARGS_IN_GPRS] = {0};
    uint8_t  acbStackArgs[MY_MAX_ARGS]; /* The number of value bytes to copy onto the stack. Zero if in register. */
    uint32_t cbStackArgs = 0;
    uint32_t cFprArgs    = 0;
    uint32_t cGprArgs    = 0;

    /* First argument is always 'pThis'. The 'pThis' argument is not accounted
       for in cParams or acbStackArgs. */
    auGprArgs[cGprArgs++] = (uintptr_t)pThis;

    /* Do the other arguments. */
    for (PRUint32 i = 0; i < cParams; i++)
    {
        if (paParams[i].IsPtrData())
        {
            if (cGprArgs < NUM_ARGS_IN_GPRS)
            {
                auGprArgs[cGprArgs++] = (uintptr_t)paParams[i].ptr;
                acbStackArgs[i] = 0;
            }
            else
            {
                acbStackArgs[i] = sizeof(paParams[i].ptr) | UINT8_C(0x80);
#ifdef RT_OS_DARWIN /* macOS compacts stack usage. */
                cbStackArgs     = RT_ALIGN_32(cbStackArgs, sizeof(paParams[i].ptr)) + sizeof(paParams[i].ptr);
#else
                cbStackArgs    += sizeof(uint64_t);
#endif
            }
        }
        else
        {
            if (   paParams[i].type != nsXPTType::T_FLOAT
                 && paParams[i].type != nsXPTType::T_DOUBLE)
            {
                if (cGprArgs < NUM_ARGS_IN_GPRS)
                {
                    switch (paParams[i].type)
                    {
                        case nsXPTType::T_I8:       auGprArgs[cGprArgs++] = paParams[i].val.i8; break;
                        case nsXPTType::T_I16:      auGprArgs[cGprArgs++] = paParams[i].val.i16; break;
                        case nsXPTType::T_I32:      auGprArgs[cGprArgs++] = paParams[i].val.i32; break;
                        case nsXPTType::T_I64:      auGprArgs[cGprArgs++] = paParams[i].val.i64; break;
                        case nsXPTType::T_U8:       auGprArgs[cGprArgs++] = paParams[i].val.u8; break;
                        case nsXPTType::T_U16:      auGprArgs[cGprArgs++] = paParams[i].val.u16; break;
                        case nsXPTType::T_U32:      auGprArgs[cGprArgs++] = paParams[i].val.u32; break;
                        default:
                        case nsXPTType::T_U64:      auGprArgs[cGprArgs++] = paParams[i].val.u64; break;
                        case nsXPTType::T_BOOL:     auGprArgs[cGprArgs++] = paParams[i].val.b; break;
                        case nsXPTType::T_CHAR:     auGprArgs[cGprArgs++] = paParams[i].val.c; break;
                        case nsXPTType::T_WCHAR:    auGprArgs[cGprArgs++] = paParams[i].val.wc; break;
                    }
                    acbStackArgs[i] = 0;
                }
                else
                {
                    uint8_t cbStack;
                    switch (paParams[i].type)
                    {
                        case nsXPTType::T_I8:       cbStack = sizeof(paParams[i].val.i8); break;
                        case nsXPTType::T_I16:      cbStack = sizeof(paParams[i].val.i16); break;
                        case nsXPTType::T_I32:      cbStack = sizeof(paParams[i].val.i32); break;
                        case nsXPTType::T_I64:      cbStack = sizeof(paParams[i].val.i64); break;
                        case nsXPTType::T_U8:       cbStack = sizeof(paParams[i].val.u8); break;
                        case nsXPTType::T_U16:      cbStack = sizeof(paParams[i].val.u16); break;
                        case nsXPTType::T_U32:      cbStack = sizeof(paParams[i].val.u32); break;
                        default:
                        case nsXPTType::T_U64:      cbStack = sizeof(paParams[i].val.u64); break;
                        case nsXPTType::T_BOOL:     cbStack = sizeof(paParams[i].val.b); break;
                        case nsXPTType::T_CHAR:     cbStack = sizeof(paParams[i].val.c); break;
                        case nsXPTType::T_WCHAR:    cbStack = sizeof(paParams[i].val.wc); break;
                    }
                    acbStackArgs[i] = cbStack;
#ifdef RT_OS_DARWIN /* macOS compacts stack usage. */
                    cbStackArgs     = RT_ALIGN_32(cbStackArgs, cbStack) + cbStack;
#else
                    cbStackArgs    += sizeof(uint64_t);
#endif
                }
            }
            else if (cFprArgs < NUM_ARGS_IN_FPRS)
            {
                AssertCompile(sizeof(paParams[i].val.f) == 4);
                AssertCompile(sizeof(paParams[i].val.d) == 8);
                if (paParams[i].type == nsXPTType::T_FLOAT)
                    auFprArgs[cFprArgs++] = paParams[i].val.u32;
                else
                    auFprArgs[cFprArgs++] = paParams[i].val.u64;
                acbStackArgs[i] = 0;
            }
            else
            {
                uint8_t cbStack;
                if (paParams[i].type == nsXPTType::T_FLOAT)
                    cbStack = sizeof(paParams[i].val.f);
                else
                    cbStack = sizeof(paParams[i].val.d);
                acbStackArgs[i] = cbStack;
#ifdef RT_OS_DARWIN /* macOS compacts stack usage. */
                cbStackArgs     = RT_ALIGN_32(cbStackArgs, cbStack) + cbStack;
#else
                cbStackArgs    += sizeof(uint64_t);
#endif
            }
        }
    }

    /*
     * Pass it on to a naked wrapper function that does the nitty gritty work.
     */
    uintptr_t *pauVtable = *(uintptr_t **)pThis;
    return arm64AsmInvoker(pauVtable[idxMethod], cParams, paParams, cbStackArgs, acbStackArgs, auGprArgs, auFprArgs, cFprArgs);
}

