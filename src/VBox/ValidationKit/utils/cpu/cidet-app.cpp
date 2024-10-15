/* $Id: cidet-app.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU Instruction Decoding & Execution Tests - Ring-3 Driver Application.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "cidet.h"

#include <iprt/asm-amd64-x86.h>
#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#else
# define USE_SIGNALS
# include <signal.h>
# include <unistd.h>
# include <sys/ucontext.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def CIDET_LEAVE_GS_ALONE
 * Leave GS alone on 64-bit darwin (gs is 0, no ldt or gdt entry to load that'll
 * restore the lower 32-bits of the base when saving and restoring the register).
 */
#if (defined(RT_OS_DARWIN) && defined(RT_ARCH_AMD64)) || defined(DOXYGEN_RUNNING)
# define CIDET_LEAVE_GS_ALONE
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * CIDET driver app buffer.
 */
typedef struct CIDETAPPBUF
{
    /** The buffer size. */
    size_t          cb;
    /** The normal allocation.
     * There is a fence page before this as well as at pbNormal+cb.  */
    uint8_t        *pbNormal;
    /** The low memory allocation (32-bit addressable if 64-bit host, 16-bit
     * addressable if 32-bit host). */
    uint8_t        *pbLow;
    /** Set if we're using the normal buffer, clear if it's the low one. */
    bool            fUsingNormal : 1;
    /** Set if the buffer is armed, clear if mostly accessible. */
    bool            fArmed : 1;
    /** Set if this is a code buffer. */
    bool            fIsCode : 1;
    /** The memory protection for the pages (RTMEM_PROT_XXX). */
    uint8_t         fDefaultProt : 3;
    /** The memory protection for the last page (RTMEM_PROT_XXX). */
    uint8_t         fLastPageProt : 3;
    /** The buffer index. */
    uint16_t        idxCfg;
} CIDETAPPBUF;
/** Pointer to a CIDET driver app buffer. */
typedef CIDETAPPBUF *PCIDETAPPBUF;

/** Number of code buffers. */
#define CIDETAPP_CODE_BUF_COUNT     1
/** Number of data buffers. */
#define CIDETAPP_DATA_BUF_COUNT     1


/**
 * CIDET driver app instance.
 */
typedef struct CIDETAPP
{
    /** The core structure. */
    CIDETCORE       Core;
    /** The execute return context. */
    CIDETCPUCTX     ExecuteCtx;
    /** Code buffers (runs parallel to g_aCodeBufCfgs). */
    CIDETAPPBUF     aCodeBuffers[CIDETAPP_CODE_BUF_COUNT];
    /** Data buffers (runs parallel to g_aDataBufCfgs). */
    CIDETAPPBUF     aDataBuffers[CIDETAPP_DATA_BUF_COUNT];

    /** The lowest stack address. */
    uint8_t        *pbStackLow;
    /** The end of the stack allocation (highest address). */
    uint8_t        *pbStackEnd;
    /** Stack size (= pbStackEnd - pbStackLow). */
    uint32_t        cbStack;
    /** Whether we're currently using the 'lock int3' to deal with tricky stack. */
    bool            fUsingLockedInt3;
} CIDETAPP;
/** Pointer to a CIDET driver app instance. */
typedef CIDETAPP *PCIDETAPP;
/** Pointer to a pointer to a CIDET driver app instance. */
typedef PCIDETAPP *PPCIDETAPP;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test instance handle. */
static RTTEST               g_hTest;
/** Points to the instance data while executing, NULL if not executing or if
 * we've already handled the first exception while executing. */
static PCIDETAPP volatile   g_pExecutingThis;
#ifdef USE_SIGNALS
/** The default signal mask. */
static sigset_t             g_ProcSigMask;
/** The alternative signal stack. */
static stack_t              g_AltStack;
#endif


/** Code buffer configurations (parallel to CIDETAPP::aCodeBuffers). */
static CIDETBUFCFG g_aCodeBufCfgs[CIDETAPP_CODE_BUF_COUNT] =
{
    {
        "Normal",
        CIDETBUF_PROT_RWX | CIDETBUF_DPL_3 | CIDETBUF_DPL_SAME | CIDETBUF_SEG_ER | CIDETBUF_KIND_CODE,
    },
};

/** Data buffer configurations (parallel to CIDETAPP::aDataBuffers). */
static CIDETBUFCFG g_aDataBufCfgs[CIDETAPP_DATA_BUF_COUNT] =
{
    {
        "Normal",
        CIDETBUF_PROT_RWX | CIDETBUF_DPL_3 | CIDETBUF_DPL_SAME | CIDETBUF_SEG_RW | CIDETBUF_KIND_DATA,
    },
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLASM(void) CidetAppSaveAndRestoreCtx(void);
DECLASM(void) CidetAppRestoreCtx(PCCIDETCPUCTX pRestoreCtx);
DECLASM(void) CidetAppExecute(PCIDETCPUCTX pSaveCtx, PCCIDETCPUCTX pRestoreCtx);


/*
 *
 *
 * Exception and signal handling.
 * Exception and signal handling.
 * Exception and signal handling.
 *
 *
 */

#ifdef RT_OS_WINDOWS
static int CidetAppXcptFilter(EXCEPTION_POINTERS *pXcptPtrs) RT_NOTHROW_DEF
{
    /*
     * Grab the this point. We expect at most one signal.
     */
    PCIDETAPP pThis = g_pExecutingThis;
    g_pExecutingThis = NULL;
    if (pThis == NULL)
    {
        /* we're up the infamous creek... */
        for (;;) ExitProcess(2);
    }

    /*
     * Gather CPU state information from the context structure.
     */
    CONTEXT *pSrcCtx = pXcptPtrs->ContextRecord;
# ifdef RT_ARCH_AMD64
    if (   (pSrcCtx->ContextFlags & (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS))
        !=                          (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS))
        __debugbreak();
    pThis->Core.ActualCtx.rip                   = pSrcCtx->Rip;
    pThis->Core.ActualCtx.rfl                   = pSrcCtx->EFlags;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xAX]  = pSrcCtx->Rax;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xCX]  = pSrcCtx->Rcx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDX]  = pSrcCtx->Rdx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBX]  = pSrcCtx->Rbx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSP]  = pSrcCtx->Rsp;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBP]  = pSrcCtx->Rbp;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSI]  = pSrcCtx->Rsi;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDI]  = pSrcCtx->Rdi;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x8]   = pSrcCtx->R8;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x9]   = pSrcCtx->R9;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x10]  = pSrcCtx->R10;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x11]  = pSrcCtx->R11;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x12]  = pSrcCtx->R12;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x13]  = pSrcCtx->R13;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x14]  = pSrcCtx->R14;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x15]  = pSrcCtx->R15;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_ES]   = pSrcCtx->SegEs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_CS]   = pSrcCtx->SegCs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_SS]   = pSrcCtx->SegSs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_DS]   = pSrcCtx->SegDs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_FS]   = pSrcCtx->SegFs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_GS]   = pSrcCtx->SegGs;
    if (pSrcCtx->ContextFlags & CONTEXT_FLOATING_POINT)
    {
        /* ... */
    }
    if (pSrcCtx->ContextFlags & CONTEXT_DEBUG_REGISTERS)
    {
        /* ... */
    }

# elif defined(RT_ARCH_X86)
    if (   (pSrcCtx->ContextFlags & (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS))
        !=                          (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS))
        __debugbreak();
    pThis->Core.ActualCtx.rip                   = pSrcCtx->Eip;
    pThis->Core.ActualCtx.rfl                   = pSrcCtx->EFlags;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xAX]  = pSrcCtx->Eax;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xCX]  = pSrcCtx->Ecx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDX]  = pSrcCtx->Edx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBX]  = pSrcCtx->Ebx;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSP]  = pSrcCtx->Esp;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBP]  = pSrcCtx->Ebp;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSI]  = pSrcCtx->Esi;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDI]  = pSrcCtx->Edi;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x8]   = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x9]   = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x10]  = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x11]  = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x12]  = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x13]  = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x14]  = 0;
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x15]  = 0;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_ES]   = pSrcCtx->SegEs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_CS]   = pSrcCtx->SegCs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_SS]   = pSrcCtx->SegSs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_DS]   = pSrcCtx->SegDs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_FS]   = pSrcCtx->SegFs;
    pThis->Core.ActualCtx.aSRegs[X86_SREG_GS]   = pSrcCtx->SegGs;
    if (pSrcCtx->ContextFlags & CONTEXT_FLOATING_POINT)
    {
        /* ... */
    }
    if (pSrcCtx->ContextFlags & CONTEXT_EXTENDED_REGISTERS)
    {
        /* ... */
    }
    if (pSrcCtx->ContextFlags & CONTEXT_DEBUG_REGISTERS)
    {
        /* ... */
    }
# else
#  error "Not supported"
# endif

    /*
     * Add/Adjust CPU state information according to the exception code.
     */
    pThis->Core.ActualCtx.uErr = UINT64_MAX;
    switch (pXcptPtrs->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_DE;
            break;
        case EXCEPTION_SINGLE_STEP:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_DB;
            break;
        case EXCEPTION_BREAKPOINT:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_BP;
            break;
        case EXCEPTION_INT_OVERFLOW:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_OF;
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_BR;
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_UD;
            break;

        case EXCEPTION_PRIV_INSTRUCTION:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_GP;
            pThis->Core.ActualCtx.uErr  = 0;
            break;

        case EXCEPTION_ACCESS_VIOLATION:
        {
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_PF;
            pThis->Core.ActualCtx.cr2   = pXcptPtrs->ExceptionRecord->ExceptionInformation[1];
            pThis->Core.ActualCtx.uErr  = 0;
            if (pXcptPtrs->ExceptionRecord->ExceptionInformation[0] == EXCEPTION_WRITE_FAULT)
                pThis->Core.ActualCtx.uErr = X86_TRAP_PF_RW;
            else if (pXcptPtrs->ExceptionRecord->ExceptionInformation[0] == EXCEPTION_EXECUTE_FAULT)
                pThis->Core.ActualCtx.uErr = X86_TRAP_PF_ID;
            else if (pXcptPtrs->ExceptionRecord->ExceptionInformation[0] != EXCEPTION_READ_FAULT)
                AssertFatalFailed();

            MEMORY_BASIC_INFORMATION MemInfo = {0};
            if (VirtualQuery((PVOID)pXcptPtrs->ExceptionRecord->ExceptionInformation[1], &MemInfo, sizeof(MemInfo)) > 0)
                switch (MemInfo.Protect & 0xff)
                {
                    case PAGE_NOACCESS:
                        break;
                    case PAGE_READONLY:
                    case PAGE_READWRITE:
                    case PAGE_WRITECOPY:
                    case PAGE_EXECUTE:
                    case PAGE_EXECUTE_READ:
                    case PAGE_EXECUTE_READWRITE:
                    case PAGE_EXECUTE_WRITECOPY:
                        pThis->Core.ActualCtx.uErr |= X86_TRAP_PF_P;
                        break;
                    default:
                        AssertFatalFailed();
                }
            break;
        }

        case EXCEPTION_FLT_DENORMAL_OPERAND:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INEXACT_RESULT:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_STACK_CHECK:
        case EXCEPTION_FLT_UNDERFLOW:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_MF;
            break;

        case EXCEPTION_DATATYPE_MISALIGNMENT:
            pThis->Core.ActualCtx.uXcpt = X86_XCPT_AC;
            break;

        default:
            pThis->Core.ActualCtx.uXcpt = pXcptPtrs->ExceptionRecord->ExceptionCode;
            break;
    }

    /*
     * Our own personal long jump implementation.
     */
    CidetAppRestoreCtx(&pThis->ExecuteCtx);

    /* Won't return...*/
    return EXCEPTION_EXECUTE_HANDLER;
}


/**
 * Vectored exception handler.
 *
 * @returns Long jumps or terminates the process.
 * @param   pXcptPtrs   The exception record.
 */
static LONG CALLBACK CidetAppVectoredXcptHandler(EXCEPTION_POINTERS *pXcptPtrs) RT_NOTHROW_DEF
{
    RTStrmPrintf(g_pStdErr, "CidetAppVectoredXcptHandler!\n");
    CidetAppXcptFilter(pXcptPtrs);

    /* won't get here. */
    return EXCEPTION_CONTINUE_SEARCH;
}


/**
 * Unhandled exception filter.
 *
 * @returns Long jumps or terminates the process.
 * @param   pXcptPtrs   The exception record.
 */
static LONG CALLBACK CidetAppUnhandledXcptFilter(EXCEPTION_POINTERS *pXcptPtrs) RT_NOTHROW_DEF
{
    RTStrmPrintf(g_pStdErr, "CidetAppUnhandledXcptFilter!\n");
    CidetAppXcptFilter(pXcptPtrs);

    /* won't get here. */
    return EXCEPTION_CONTINUE_SEARCH;
}


#elif defined(USE_SIGNALS)
/**
 * Signal handler.
 */
static void CidetAppSigHandler(int iSignal, siginfo_t *pSigInfo, void *pvCtx)
{
# if 1
    if (   !g_pExecutingThis
        || !g_pExecutingThis->fUsingLockedInt3
        || iSignal != SIGILL)
    {
        RTStrmPrintf(g_pStdErr, "signal %d pSigInfo=%p pvCtx=%p", iSignal, pSigInfo, pvCtx);
        if (pSigInfo)
            RTStrmPrintf(g_pStdErr, " si_addr=%p si_code=%#x sival_ptr=%p sival_int=%d",
                         pSigInfo->si_addr, pSigInfo->si_code, pSigInfo->si_value.sival_ptr, pSigInfo->si_value.sival_int);
        RTStrmPrintf(g_pStdErr, "\n");
    }
# endif

    /*
     * Grab the this point. We expect at most one signal.
     */
    PCIDETAPP pThis = g_pExecutingThis;
    g_pExecutingThis = NULL;
    if (pThis == NULL)
    {
        /* we're up the infamous creek... */
        RTStrmPrintf(g_pStdErr, "Creek time!\n");
        for (;;) _exit(2);
    }

    /*
     * Gather all the CPU state information available.
     */
# ifdef RT_OS_LINUX
    ucontext_t const *pCtx = (ucontext_t const *)pvCtx;
#  ifdef RT_ARCH_AMD64

    pThis->Core.ActualCtx.aGRegs[X86_GREG_xAX] = pCtx->uc_mcontext.gregs[REG_RAX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xCX] = pCtx->uc_mcontext.gregs[REG_RCX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDX] = pCtx->uc_mcontext.gregs[REG_RDX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBX] = pCtx->uc_mcontext.gregs[REG_RBX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSP] = pCtx->uc_mcontext.gregs[REG_RSP];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBP] = pCtx->uc_mcontext.gregs[REG_RBP];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSI] = pCtx->uc_mcontext.gregs[REG_RSI];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDI] = pCtx->uc_mcontext.gregs[REG_RDI];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x8 ] = pCtx->uc_mcontext.gregs[REG_R8];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x9 ] = pCtx->uc_mcontext.gregs[REG_R9];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x10] = pCtx->uc_mcontext.gregs[REG_R10];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x11] = pCtx->uc_mcontext.gregs[REG_R11];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x12] = pCtx->uc_mcontext.gregs[REG_R12];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x13] = pCtx->uc_mcontext.gregs[REG_R13];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x14] = pCtx->uc_mcontext.gregs[REG_R14];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_x15] = pCtx->uc_mcontext.gregs[REG_R15];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_CS]  = RT_LO_U16((uint32_t)pCtx->uc_mcontext.gregs[REG_CSGSFS]);
    pThis->Core.ActualCtx.aSRegs[X86_SREG_GS]  = RT_HI_U16((uint32_t)pCtx->uc_mcontext.gregs[REG_CSGSFS]);
    pThis->Core.ActualCtx.aSRegs[X86_SREG_FS]  = (uint16_t)RT_HI_U32(pCtx->uc_mcontext.gregs[REG_CSGSFS]);
    pThis->Core.ActualCtx.aSRegs[X86_SREG_DS]  = ASMGetDS();
    pThis->Core.ActualCtx.aSRegs[X86_SREG_ES]  = ASMGetES();
    pThis->Core.ActualCtx.aSRegs[X86_SREG_SS]  = ASMGetSS();
    pThis->Core.ActualCtx.rip                  = pCtx->uc_mcontext.gregs[REG_RIP];
    pThis->Core.ActualCtx.rfl                  = pCtx->uc_mcontext.gregs[REG_EFL];
    pThis->Core.ActualCtx.cr2                  = pCtx->uc_mcontext.gregs[REG_CR2];
    pThis->Core.ActualCtx.uXcpt                = pCtx->uc_mcontext.gregs[REG_TRAPNO];
    pThis->Core.ActualCtx.uErr                 = pCtx->uc_mcontext.gregs[REG_ERR];

    /* Fudge the FS and GS registers as setup_sigcontext returns 0. */
    if (pThis->Core.ActualCtx.aSRegs[X86_SREG_FS] == 0)
       pThis->Core.ActualCtx.aSRegs[X86_SREG_FS] = pThis->Core.ExpectedCtx.aSRegs[X86_SREG_FS];
    if (pThis->Core.ActualCtx.aSRegs[X86_SREG_GS] == 0)
       pThis->Core.ActualCtx.aSRegs[X86_SREG_GS] = pThis->Core.ExpectedCtx.aSRegs[X86_SREG_GS];

#  elif defined(RT_ARCH_X86)
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xAX] = pCtx->uc_mcontext.gregs[REG_EAX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xCX] = pCtx->uc_mcontext.gregs[REG_ECX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDX] = pCtx->uc_mcontext.gregs[REG_EDX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBX] = pCtx->uc_mcontext.gregs[REG_EBX];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSP] = pCtx->uc_mcontext.gregs[REG_ESP];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xBP] = pCtx->uc_mcontext.gregs[REG_EBP];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xSI] = pCtx->uc_mcontext.gregs[REG_ESI];
    pThis->Core.ActualCtx.aGRegs[X86_GREG_xDI] = pCtx->uc_mcontext.gregs[REG_EDI];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_CS]  = pCtx->uc_mcontext.gregs[REG_CS];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_DS]  = pCtx->uc_mcontext.gregs[REG_DS];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_ES]  = pCtx->uc_mcontext.gregs[REG_ES];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_FS]  = pCtx->uc_mcontext.gregs[REG_FS];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_GS]  = pCtx->uc_mcontext.gregs[REG_GS];
    pThis->Core.ActualCtx.aSRegs[X86_SREG_SS]  = pCtx->uc_mcontext.gregs[REG_SS];
    pThis->Core.ActualCtx.rip                  = pCtx->uc_mcontext.gregs[REG_EIP];
    pThis->Core.ActualCtx.rfl                  = pCtx->uc_mcontext.gregs[REG_EFL];
    pThis->Core.ActualCtx.cr2                  = pCtx->uc_mcontext.cr2;
    pThis->Core.ActualCtx.uXcpt                = pCtx->uc_mcontext.gregs[REG_TRAPNO];
    pThis->Core.ActualCtx.uErr                 = pCtx->uc_mcontext.gregs[REG_ERR];

#  else
#   error "Unsupported arch."
#  endif

    /* Adjust uErr. */
    switch (pThis->Core.ActualCtx.uXcpt)
    {
        case X86_XCPT_TS:
        case X86_XCPT_NP:
        case X86_XCPT_SS:
        case X86_XCPT_GP:
        case X86_XCPT_PF:
        case X86_XCPT_AC:
        case X86_XCPT_DF:
            break;
        default:
            pThis->Core.ActualCtx.uErr = UINT64_MAX;
            break;
    }

#  if 0
    /* Fudge the resume flag (it's probably always set here). */
    if (   (pThis->Core.ActualCtx.rfl & X86_EFL_RF)
        && !(pThis->Core.ExpectedCtx.rfl & X86_EFL_RF))
        pThis->Core.ActualCtx.rfl &= ~X86_EFL_RF;
#  endif

# else
    /** @todo    */
# endif


    /*
     * Check for the 'lock int3' instruction used for tricky stacks.
     */
    if (   pThis->fUsingLockedInt3
        && pThis->Core.ActualCtx.uXcpt == X86_XCPT_UD
        && pThis->Core.ActualCtx.rip == pThis->Core.CodeBuf.uEffBufAddr - pThis->Core.CodeBuf.offSegBase
                                        + pThis->Core.CodeBuf.offActive + pThis->Core.CodeBuf.cbActive )
    {
        pThis->Core.ActualCtx.uXcpt = UINT32_MAX;
        Assert(pThis->Core.ActualCtx.uErr == UINT64_MAX);
        pThis->Core.ActualCtx.rfl &= ~X86_EFL_RF;
    }

    /*
     * Jump back to CidetAppCbExecute.
     */
    CidetAppRestoreCtx(&pThis->ExecuteCtx);
}
#endif



/*
 *
 * Buffer handling
 * Buffer handling
 * Buffer handling
 *
 *
 */

static int cidetAppAllocateAndConfigureOneBuffer(PCIDETAPP pThis, PCIDETAPPBUF pBuf, uint16_t idxBuf, bool fIsCode,
                                                 uint32_t fFlags)
{
    RT_NOREF_PV(pThis);
    static uint8_t const s_afBufProtToDefaultMemProt[] =
    {
        /* [0]  = */ RTMEM_PROT_NONE,
        /* [1]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC,
        /* [2]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE,
        /* [3]  = */ RTMEM_PROT_READ | RTMEM_PROT_EXEC,
        /* [4]  = */ RTMEM_PROT_READ,
        /* [5]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC,
        /* [6]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC,
        /* [7]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC,
        /* [8]  = */ RTMEM_PROT_NONE,
        /* [9]  = */ RTMEM_PROT_NONE,
        /* [10] = */ RTMEM_PROT_NONE,
        /* [11] = */ RTMEM_PROT_NONE,
        /* [12] = */ RTMEM_PROT_NONE,
        /* [13] = */ RTMEM_PROT_NONE,
        /* [14] = */ RTMEM_PROT_NONE,
        /* [15] = */ RTMEM_PROT_NONE,
    };
    static uint8_t const s_afBufProtToLastPageMemProt[] =
    {
        /* [0]  = */ RTMEM_PROT_NONE,
        /* [1]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC,
        /* [2]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE,
        /* [3]  = */ RTMEM_PROT_READ | RTMEM_PROT_EXEC,
        /* [4]  = */ RTMEM_PROT_READ,
        /* [5]  = */ RTMEM_PROT_NONE,
        /* [6]  = */ RTMEM_PROT_READ | RTMEM_PROT_WRITE,
        /* [7]  = */ RTMEM_PROT_READ,
        /* [8]  = */ RTMEM_PROT_NONE,
        /* [9]  = */ RTMEM_PROT_NONE,
        /* [10] = */ RTMEM_PROT_NONE,
        /* [11] = */ RTMEM_PROT_NONE,
        /* [12] = */ RTMEM_PROT_NONE,
        /* [13] = */ RTMEM_PROT_NONE,
        /* [14] = */ RTMEM_PROT_NONE,
        /* [15] = */ RTMEM_PROT_NONE,
    };

    int rc;
    Assert(CIDETBUF_IS_CODE(fFlags) == fIsCode);
    pBuf->fIsCode       = fIsCode;
    pBuf->idxCfg        = idxBuf;
    pBuf->fUsingNormal  = true;
    pBuf->fDefaultProt  = s_afBufProtToDefaultMemProt[fFlags & CIDETBUF_PROT_MASK];
    pBuf->fLastPageProt = s_afBufProtToLastPageMemProt[fFlags & CIDETBUF_PROT_MASK];
    if (pBuf->fDefaultProt != RTMEM_PROT_NONE)
    {
        /*
         * Allocate a 3 page buffer plus two fence pages.
         */
        pBuf->cb        = fIsCode ? CIDET_CODE_BUF_SIZE : CIDET_DATA_BUF_SIZE;
        pBuf->pbNormal  = (uint8_t *)RTMemPageAlloc(PAGE_SIZE + pBuf->cb + PAGE_SIZE);
        if (pBuf->pbNormal)
        {
            memset(pBuf->pbNormal, 0x55, PAGE_SIZE);
            memset(pBuf->pbNormal + PAGE_SIZE, 0xcc, pBuf->cb);
            memset(pBuf->pbNormal + PAGE_SIZE + pBuf->cb, 0x77, PAGE_SIZE);

            /* Set up fence pages. */
            rc = RTMemProtect(pBuf->pbNormal, PAGE_SIZE, RTMEM_PROT_NONE); /* fence */
            if (RT_SUCCESS(rc))
                rc = RTMemProtect(pBuf->pbNormal + PAGE_SIZE + pBuf->cb, PAGE_SIZE, RTMEM_PROT_NONE); /* fence */
            pBuf->pbNormal += PAGE_SIZE;

            /* Default protection + read + write. */
            if (RT_SUCCESS(rc))
                rc = RTMemProtect(pBuf->pbNormal, pBuf->cb, pBuf->fDefaultProt | RTMEM_PROT_READ | RTMEM_PROT_WRITE);

            /*
             * Allocate a low memory buffer or LDT if necessary.
             */
            if (   RT_SUCCESS(rc)
                && (uintptr_t)pBuf->pbNormal + pBuf->cb > RT_BIT_64(sizeof(uintptr_t) / 2 * 8))
            {
                /** @todo Buffers for the other addressing mode. */
                pBuf->pbLow = NULL;
            }
            else
                pBuf->pbLow = pBuf->pbNormal;
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

        }
        else
            rc = RTTestIFailedRc(VERR_NO_PAGE_MEMORY, "Error allocating three pages.");
    }
    else
        rc = RTTestIFailedRc(VERR_NO_PAGE_MEMORY, "Unsupported buffer config: fFlags=%#x, idxBuf=%u", fFlags, idxBuf);
    return rc;
}


static void CidetAppDeleteBuffer(PCIDETAPPBUF pBuf)
{
    RTMemProtect(pBuf->pbNormal - PAGE_SIZE, PAGE_SIZE + pBuf->cb + PAGE_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    RTMemPageFree(pBuf->pbNormal - PAGE_SIZE, PAGE_SIZE + pBuf->cb + PAGE_SIZE);
    if (pBuf->pbLow != pBuf->pbNormal && pBuf->pbLow)
    {
        RTMemProtect(pBuf->pbLow, pBuf->cb, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        RTMemFreeEx(pBuf->pbLow, pBuf->cb);
    }
}


static bool CidetAppArmBuf(PCIDETAPP pThis, PCIDETAPPBUF pAppBuf)
{
    RT_NOREF_PV(pThis);
    uint8_t *pbUsingBuf = (pAppBuf->fUsingNormal ? pAppBuf->pbNormal : pAppBuf->pbLow);
    if (pAppBuf->fLastPageProt == pAppBuf->fDefaultProt)
    {
        if ((pAppBuf->fDefaultProt & (RTMEM_PROT_READ | RTMEM_PROT_WRITE)) != (RTMEM_PROT_READ | RTMEM_PROT_WRITE))
            RTTESTI_CHECK_RC_RET(RTMemProtect(pbUsingBuf, pAppBuf->cb, pAppBuf->fDefaultProt), VINF_SUCCESS, false);
    }
    else
    {
        if ((pAppBuf->fDefaultProt & (RTMEM_PROT_READ | RTMEM_PROT_WRITE)) != (RTMEM_PROT_READ | RTMEM_PROT_WRITE))
            RTTESTI_CHECK_RC_RET(RTMemProtect(pbUsingBuf, pAppBuf->cb - PAGE_SIZE, pAppBuf->fDefaultProt), VINF_SUCCESS, false);
        RTTESTI_CHECK_RC_RET(RTMemProtect(pbUsingBuf + pAppBuf->cb - PAGE_SIZE, PAGE_SIZE, pAppBuf->fLastPageProt),
                             VINF_SUCCESS, false);
    }
    pAppBuf->fArmed = true;
    return true;
}


static bool CidetAppDearmBuf(PCIDETAPP pThis, PCIDETAPPBUF pAppBuf)
{
    RT_NOREF_PV(pThis);
    uint8_t *pbUsingBuf = (pAppBuf->fUsingNormal ? pAppBuf->pbNormal : pAppBuf->pbLow);
    int rc = RTMemProtect(pbUsingBuf, pAppBuf->cb, pAppBuf->fDefaultProt | RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("RTMemProtect failed on %s buf #%u: %Rrc", pAppBuf->fIsCode ? "code" : "data", pAppBuf->idxCfg, rc);
        return false;
    }
    pAppBuf->fArmed = false;
    return true;
}


/**
 * @interface_method_impl{CIDETCORE,pfnReInitDataBuf}
 */
static DECLCALLBACK(bool) CidetAppCbReInitDataBuf(PCIDETCORE pThis, PCIDETBUF pBuf)
{
    PCIDETAPP    pThisApp = (PCIDETAPP)pThis;
    PCIDETAPPBUF pAppBuf  = &pThisApp->aDataBuffers[pBuf->idxCfg];
    Assert(CIDETBUF_IS_DATA(pBuf->pCfg->fFlags));

    /*
     * De-arm the buffer.
     */
    if (pAppBuf->fArmed)
        if (RT_UNLIKELY(!CidetAppDearmBuf(pThisApp, pAppBuf)))
            return false;

    /*
     * Check the allocation requirements.
     */
    if (RT_UNLIKELY((size_t)pBuf->off + pBuf->cb > pAppBuf->cb))
    {
        RTTestIFailed("Buffer too small; off=%#x cb=%#x pAppBuf->cb=%#x (%s)",
                      pBuf->off, pBuf->cb, pAppBuf->cb, pBuf->pCfg->pszName);
        return false;
    }

    /*
     * Do we need to use the low buffer?  Check that we have one, if we need it.
     */
    bool fUseNormal = pThis->cbAddrMode == ARCH_BITS / 8;
    if (!fUseNormal && !pAppBuf->pbLow)
        return false;

    /*
     * Update the state.
     */
    pAppBuf->fUsingNormal   = fUseNormal;

    pBuf->offActive         = pBuf->off;
    pBuf->cbActive          = pBuf->cb;
    pBuf->cbPrologue        = 0;
    pBuf->cbEpilogue        = 0;
    pBuf->uSeg              = UINT32_MAX;
    pBuf->cbActiveSegLimit  = UINT64_MAX;
    pBuf->uSegBase          = 0;
    if (fUseNormal)
        pBuf->uEffBufAddr   = (uintptr_t)pAppBuf->pbNormal;
    else
        pBuf->uEffBufAddr   = (uintptr_t)pAppBuf->pbLow;

    return true;
}


/**
 * @interface_method_impl{CIDETCORE,pfnSetupDataBuf}
 */
static DECLCALLBACK(bool) CidetAppCbSetupDataBuf(PCIDETCORE pThis, PCIDETBUF pBuf, void const *pvSrc)
{
    PCIDETAPP    pThisApp = (PCIDETAPP)pThis;
    PCIDETAPPBUF pAppBuf  = &pThisApp->aDataBuffers[pBuf->idxCfg];
    Assert(CIDETBUF_IS_DATA(pBuf->pCfg->fFlags));
    Assert(!pAppBuf->fArmed);


    /*
     * Copy over the data.
     */
    uint8_t *pbUsingBuf = (pAppBuf->fUsingNormal ? pAppBuf->pbNormal : pAppBuf->pbLow);
    memcpy(pbUsingBuf + pBuf->offActive, pvSrc, pBuf->cbActive);

    /*
     * Arm the buffer.
     */
    return CidetAppArmBuf(pThisApp, pAppBuf);
}


/**
 * @interface_method_impl{CIDETCORE,pfnIsBufEqual}
 */
static DECLCALLBACK(bool) CidetAppCbIsBufEqual(PCIDETCORE pThis, struct CIDETBUF *pBuf, void const *pvExpected)
{
    PCIDETAPP    pThisApp = (PCIDETAPP)pThis;
    PCIDETAPPBUF pAppBuf  = CIDETBUF_IS_CODE(pBuf->pCfg->fFlags)
                          ? &pThisApp->aCodeBuffers[pBuf->idxCfg]
                          : &pThisApp->aDataBuffers[pBuf->idxCfg];

    /*
     * Disarm the buffer if we can't read it all.
     */
    if (   pAppBuf->fArmed
        && (   !(pAppBuf->fLastPageProt & RTMEM_PROT_READ)
            || !(pAppBuf->fDefaultProt  & RTMEM_PROT_READ)) )
        if (RT_UNLIKELY(!CidetAppDearmBuf(pThisApp, pAppBuf)))
            return false;

    /*
     * Do the comparing.
     */
    uint8_t *pbUsingBuf = (pAppBuf->fUsingNormal ? pAppBuf->pbNormal : pAppBuf->pbLow);
    if (memcmp(pbUsingBuf + pBuf->offActive, pvExpected, pBuf->cbActive) != 0)
    {
        /** @todo RTMEM_PROT_NONE may kill content on some hosts... */
        return false;
    }

    /** @todo check padding. */
    return true;
}


/*
 *
 * Code buffer, prologue, epilogue, and execution.
 * Code buffer, prologue, epilogue, and execution.
 * Code buffer, prologue, epilogue, and execution.
 *
 *
 */


/**
 * @interface_method_impl{CIDETCORE,pfnReInitCodeBuf}
 */
static DECLCALLBACK(bool) CidetAppCbReInitCodeBuf(PCIDETCORE pThis, PCIDETBUF pBuf)
{
    PCIDETAPP    pThisApp = (PCIDETAPP)pThis;
    PCIDETAPPBUF pAppBuf  = &pThisApp->aCodeBuffers[pBuf->idxCfg];
    Assert(CIDETBUF_IS_CODE(pBuf->pCfg->fFlags));
    Assert(pAppBuf->fUsingNormal);

    /*
     * De-arm the buffer.
     */
    if (pAppBuf->fArmed)
        if (RT_UNLIKELY(!CidetAppDearmBuf(pThisApp, pAppBuf)))
            return false;

    /*
     * Determin the prologue and epilogue sizes.
     */
    uint16_t cbPrologue = 0;
    uint16_t cbEpilogue = ARCH_BITS == 64 ? 0x56 : 0x4e;
    if (pThis->InCtx.fTrickyStack)
        cbEpilogue = 16;

    /*
     * Check the allocation requirements.
     */
    if (RT_UNLIKELY(   cbPrologue > pBuf->off
                    || (size_t)pBuf->off + pBuf->cb + cbEpilogue > pAppBuf->cb))
    {
        RTTestIFailed("Buffer too small; off=%#x cb=%#x cbPro=%#x cbEpi=%#x pAppBuf->cb=%#x (%s)",
                      pBuf->off, pBuf->cb, cbPrologue, cbEpilogue, pAppBuf->cb, pBuf->pCfg->pszName);
        return false;
    }

    /*
     * Update the state.
     */
    pAppBuf->fUsingNormal   = true;

    pBuf->cbActive          = pBuf->cb;
    pBuf->offActive         = pBuf->off;
    pBuf->cbPrologue        = cbPrologue;
    pBuf->cbEpilogue        = cbEpilogue;
    pBuf->uSeg              = UINT32_MAX;
    pBuf->cbActiveSegLimit  = UINT64_MAX;
    pBuf->uSegBase          = 0;
    pBuf->uEffBufAddr       = (uintptr_t)pAppBuf->pbNormal;

    return true;
}


/**
 * @interface_method_impl{CIDETCORE,pfnSetupCodeBuf}
 */
static DECLCALLBACK(bool) CidetAppCbSetupCodeBuf(PCIDETCORE pThis, PCIDETBUF pBuf, void const *pvInstr)
{
    PCIDETAPP    pThisApp = (PCIDETAPP)pThis;
    PCIDETAPPBUF pAppBuf  =&pThisApp->aCodeBuffers[pBuf->idxCfg];
    Assert(CIDETBUF_IS_CODE(pBuf->pCfg->fFlags));
    Assert(pAppBuf->fUsingNormal);
    Assert(!pAppBuf->fArmed);

    /*
     * Emit prologue code.
     */
    uint8_t *pbDst = pAppBuf->pbNormal + pBuf->offActive - pBuf->cbPrologue;

    /*
     * Copy over the code.
     */
    Assert(pbDst == &pAppBuf->pbNormal[pBuf->offActive]);
    memcpy(pbDst, pvInstr, pBuf->cbActive);
    pbDst += pBuf->cbActive;

    /*
     * Emit epilogue code.
     */
    if (!pThis->InCtx.fTrickyStack)
    {
        /*
         * The stack is reasonably good, do minimal work.
         *
         * Note! Ideally, we would just fill in 16 int3s here and check that
         *       we hit the first right one.  However, if we wish to run this
         *       code with IEM, we better skip unnecessary trips to ring-0.
         */
        uint8_t * const pbStartEpilogue = pbDst;

        /* jmp      $+6 */
        *pbDst++ = 0xeb;
        *pbDst++ = 0x06;        /* This is a push es, so if the decoder is one off, we'll hit the int 3 below. */

        /* Six int3s for trapping incorrectly decoded instructions. */
        *pbDst++ = 0xcc;
        *pbDst++ = 0xcc;
        *pbDst++ = 0xcc;
        *pbDst++ = 0xcc;
        *pbDst++ = 0xcc;
        *pbDst++ = 0xcc;

        /* push     rip / call $+0 */
        *pbDst++ = 0xe8;
        *pbDst++ = 0x00;
        *pbDst++ = 0x00;
        *pbDst++ = 0x00;
        *pbDst++ = 0x00;
        uint8_t offRipAdjust = (uint8_t)(uintptr_t)(pbStartEpilogue - pbDst);

        /* push     xCX */
        *pbDst++ = 0x51;

        /* mov      xCX, [xSP + xCB] */
        *pbDst++ = 0x48;
        *pbDst++ = 0x8b;
        *pbDst++ = 0x4c;
        *pbDst++ = 0x24;
        *pbDst++ = sizeof(uintptr_t);

        /* lea      xCX, [xCX - 24] */
        *pbDst++ = 0x48;
        *pbDst++ = 0x8d;
        *pbDst++ = 0x49;
        *pbDst++ = offRipAdjust;

        /* mov      xCX, [xSP + xCB] */
        *pbDst++ = 0x48;
        *pbDst++ = 0x89;
        *pbDst++ = 0x4c;
        *pbDst++ = 0x24;
        *pbDst++ = sizeof(uintptr_t);

        /* mov      xCX, &pThis->ActualCtx */
#ifdef RT_ARCH_AMD64
        *pbDst++ = 0x48;
#endif
        *pbDst++ = 0xb9;
        *(uintptr_t *)pbDst = (uintptr_t)&pThisApp->Core.ActualCtx;
        pbDst  += sizeof(uintptr_t);

        /* pop      [ss:rcx + ActualCtx.aGRegs[X86_GREG_xCX]] */
        *pbDst++ = 0x36;
        *pbDst++ = 0x8f;
        *pbDst++ = 0x41;
        *pbDst++ = RT_UOFFSETOF(CIDETCPUCTX, aGRegs[X86_GREG_xCX]);
        Assert(RT_UOFFSETOF(CIDETCPUCTX, aGRegs[X86_GREG_xCX]) < 0x7f);

        /* mov      [ss:rcx + ActualCtx.aGRegs[X86_GREG_xDX]], rdx */
        *pbDst++ = 0x36;
#ifdef RT_ARCH_AMD64
        *pbDst++ = 0x48;
#endif
        *pbDst++ = 0x89;
        *pbDst++ = 0x51;
        *pbDst++ = RT_UOFFSETOF(CIDETCPUCTX, aGRegs[X86_GREG_xDX]);
        Assert(RT_UOFFSETOF(CIDETCPUCTX, aGRegs[X86_GREG_xDX]) < 0x7f);

        /* mov      [ss:rcx + ActualCtx.aSRegs[X86_GREG_DS]], ds */
        *pbDst++ = 0x36;
        *pbDst++ = 0x8c;
        *pbDst++ = 0x99;
        *(uint32_t *)pbDst = RT_UOFFSETOF(CIDETCPUCTX, aSRegs[X86_SREG_DS]);
        pbDst += sizeof(uint32_t);

        /* mov      edx, 0XXYYh */
        *pbDst++ = 0xba;
        *(uint32_t *)pbDst = pThisApp->Core.InTemplateCtx.aSRegs[X86_SREG_DS];
        pbDst   += sizeof(uint32_t);

        /* mov      ds, dx */
        *pbDst++ = 0x8e;
        *pbDst++ = 0xda;

        /* mov      xDX, &pThisApp->ExecuteCtx */
#ifdef RT_ARCH_AMD64
        *pbDst++ = 0x48;
#endif
        *pbDst++ = 0xba;
        *(uintptr_t *)pbDst = (uintptr_t)&pThisApp->ExecuteCtx;
        pbDst  += sizeof(uintptr_t);

#ifdef RT_ARCH_AMD64
        /* jmp      [cs:$ wrt rip] */
        *pbDst++ = 0xff;
        *pbDst++ = 0x25;
        *(uint32_t *)pbDst = 0;
        pbDst   += sizeof(uint32_t);
#else
        /* jmp      NAME(CidetAppSaveAndRestoreCtx) */
        *pbDst++ = 0xb9;
#endif
        *(uintptr_t *)pbDst = (uintptr_t)CidetAppSaveAndRestoreCtx;
        pbDst  += sizeof(uintptr_t);

        /* int3 */
        *pbDst++ = 0xcc;

        pThisApp->fUsingLockedInt3 = false;

    }
    else
    {
        /*
         * Tricky stack, so just make it raise #UD after a successful run.
         */
        *pbDst++ = 0xf0;         /* lock prefix */
        memset(pbDst, 0xcc, 15); /* int3 */
        pbDst += 15;

        pThisApp->fUsingLockedInt3 = true;
    }

    AssertMsg(pbDst == &pAppBuf->pbNormal[pBuf->offActive + pBuf->cb + pBuf->cbEpilogue],
              ("cbEpilogue=%#x, actual %#x\n", pBuf->cbEpilogue, pbDst - &pAppBuf->pbNormal[pBuf->offActive + pBuf->cb]));

    /*
     * Arm the buffer.
     */
    return CidetAppArmBuf(pThisApp, pAppBuf);
}


/**
 * @interface_method_impl{CIDETCORE,pfnExecute}
 */
static DECLCALLBACK(bool) CidetAppCbExecute(PCIDETCORE pThis)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    /* Skip tricky stack because windows cannot dispatch exception if RSP/ESP is bad. */
    if (pThis->InCtx.fTrickyStack)
        return false;
#endif

    g_pExecutingThis = (PCIDETAPP)pThis;
#ifdef RT_OS_WINDOWS
    __try
    {
        CidetAppExecute(&((PCIDETAPP)pThis)->ExecuteCtx, &pThis->InCtx);
    }
    __except (CidetAppXcptFilter(GetExceptionInformation()))
    {
        /* Won't end up here... */
    }
    g_pExecutingThis = NULL;
#else
    CidetAppExecute(&((PCIDETAPP)pThis)->ExecuteCtx, &pThis->InCtx);
    if (g_pExecutingThis)
        g_pExecutingThis = NULL;
    else
    {
        RTTESTI_CHECK_RC(sigprocmask(SIG_SETMASK, &g_ProcSigMask, NULL), 0);
        RTTESTI_CHECK_RC(sigaltstack(&g_AltStack, NULL), 0);
    }
#endif

    return true;
}




/*
 *
 *
 * CIDET Application.
 * CIDET Application.
 * CIDET Application.
 *
 *
 */


/**
 * @interface_method_impl{CIDETCORE,pfnFailure}
 */
static DECLCALLBACK(void) CidetAppCbFailureV(PCIDETCORE pThis, const char *pszFormat, va_list va)
{
    RT_NOREF_PV(pThis);
    RTTestIFailedV(pszFormat, va);
}


static int cidetAppAllocateAndConfigureBuffers(PCIDETAPP pThis)
{
    /*
     * Code buffers.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCodeBuffers); i++)
    {
        int rc = cidetAppAllocateAndConfigureOneBuffer(pThis, &pThis->aCodeBuffers[i], i, true /*fCode*/,
                                                       g_aCodeBufCfgs[i].fFlags);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Data buffers.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aDataBuffers); i++)
    {
        int rc = cidetAppAllocateAndConfigureOneBuffer(pThis, &pThis->aDataBuffers[i], i, false /*fCode*/,
                                                       g_aDataBufCfgs[i].fFlags);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Stack.
     */
    pThis->cbStack    = _32K;
    pThis->pbStackLow = (uint8_t *)RTMemPageAlloc(pThis->cbStack);
    if (!pThis->pbStackLow)
    {
        RTTestIFailed("Failed to allocate %u bytes for stack\n", pThis->cbStack);
        return false;
    }
    pThis->pbStackEnd = pThis->pbStackLow + pThis->cbStack;

    return true;
}


static int CidetAppCreate(PPCIDETAPP ppThis)
{
    *ppThis = NULL;

    PCIDETAPP pThis = (PCIDETAPP)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Error allocating %zu bytes.", sizeof(*pThis));

    /* Create a random source. */
    RTRAND hRand;
    int rc = RTRandAdvCreateParkMiller(&hRand);
    if (RT_SUCCESS(rc))
    {
        uint64_t uSeed = ASMReadTSC();
        rc = RTRandAdvSeed(hRand, uSeed);
        if (RT_SUCCESS(rc))
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Random seed %#llx\n", uSeed);

        /* Initialize the CIDET structure. */
        rc = CidetCoreInit(&pThis->Core, hRand);
        if (RT_SUCCESS(rc))
        {
            pThis->Core.pfnReInitDataBuf    = CidetAppCbReInitDataBuf;
            pThis->Core.pfnSetupDataBuf     = CidetAppCbSetupDataBuf;
            pThis->Core.pfnIsBufEqual       = CidetAppCbIsBufEqual;
            pThis->Core.pfnReInitCodeBuf    = CidetAppCbReInitCodeBuf;
            pThis->Core.pfnSetupCodeBuf     = CidetAppCbSetupCodeBuf;
            pThis->Core.pfnExecute          = CidetAppCbExecute;
            pThis->Core.pfnFailure          = CidetAppCbFailureV;

            pThis->Core.paCodeBufConfigs    = g_aCodeBufCfgs;
            pThis->Core.cCodeBufConfigs     = CIDETAPP_CODE_BUF_COUNT;
            pThis->Core.paDataBufConfigs    = g_aDataBufCfgs;
            pThis->Core.cDataBufConfigs     = CIDETAPP_DATA_BUF_COUNT;

            rc = cidetAppAllocateAndConfigureBuffers(pThis);
            if (RT_SUCCESS(rc))
            {
                rc = CidetCoreSetTargetMode(&pThis->Core, ARCH_BITS == 32 ? CIDETMODE_PP_32 : CIDETMODE_LM_64);
                if (RT_SUCCESS(rc))
                {
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_CS] = ASMGetCS();
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_DS] = ASMGetDS();
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_ES] = ASMGetES();
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_FS] = ASMGetFS();
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_GS] = ASMGetGS();
                    pThis->Core.InTemplateCtx.aSRegs[X86_SREG_SS] = ASMGetSS();
                    pThis->Core.InTemplateCtx.aGRegs[X86_GREG_xSP] = (uintptr_t)pThis->pbStackEnd - 64;

                    pThis->Core.fTestCfg |= CIDET_TESTCFG_SEG_PRF_CS;
                    pThis->Core.fTestCfg |= CIDET_TESTCFG_SEG_PRF_DS;
                    pThis->Core.fTestCfg |= CIDET_TESTCFG_SEG_PRF_ES;
#if !defined(RT_OS_WINDOWS)
                    pThis->Core.fTestCfg |= CIDET_TESTCFG_SEG_PRF_FS;
#endif
#if !defined(CIDET_LEAVE_GS_ALONE)
                    pThis->Core.fTestCfg |= CIDET_TESTCFG_SEG_PRF_GS;
#endif

                    *ppThis = pThis;
                    return VINF_SUCCESS;
                }
                rc = RTTestIFailedRc(rc, "Error setting target mode: %Rrc", rc);
            }
            CidetCoreDelete(&pThis->Core);
        }
        else
        {
            rc = RTTestIFailedRc(rc, "CidetCoreInit failed: %Rrc", rc);
            RTRandAdvDestroy(hRand);
        }
    }
    else
        rc = RTTestIFailedRc(rc, "RTRandAdvCreate failed: %Rrc", rc);
    RTMemFree(pThis);
    return rc;
}


static void CidetAppDestroy(PCIDETAPP pThis)
{
    CidetCoreDelete(&pThis->Core);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCodeBuffers); i++)
        CidetAppDeleteBuffer(&pThis->aCodeBuffers[i]);
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aDataBuffers); i++)
        CidetAppDeleteBuffer(&pThis->aDataBuffers[i]);
    RTMemPageFree(pThis->pbStackLow, pThis->cbStack);

    RTMemFree(pThis);
}


static void CidetAppTestBunch(PCIDETAPP pThis, PCCIDETINSTR paInstructions, uint32_t cInstructions, const char *pszBunchName)
{
    for (uint32_t iInstr = 0; iInstr < cInstructions; iInstr++)
    {
        RTTestSubF(g_hTest, "%s - %s", pszBunchName, paInstructions[iInstr].pszMnemonic);
        CidetCoreTestInstruction(&pThis->Core, &paInstructions[iInstr]);
    }
}


int main(int argc, char **argv)
{
    /*
     * Initialize the runtime.
     */
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, 0, "cidet-app", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--noop", 'n', RTGETOPT_REQ_NOTHING },
    };

    int           chOpt;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((chOpt = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (chOpt)
        {
            case 'n':
                break;

            case 'h':
                RTPrintf("usage: %s\n", argv[0]);
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

#ifdef USE_SIGNALS
    /*
     * Set up signal handlers with alternate stack.
     */
    /* Get the default signal mask. */
    RTTESTI_CHECK_RC_RET(sigprocmask(SIG_BLOCK, NULL, &g_ProcSigMask), 0, RTEXITCODE_FAILURE);

    /* Alternative stack so we can play with esp/rsp. */
    RT_ZERO(g_AltStack);
    g_AltStack.ss_flags = 0;
# ifdef SIGSTKSZ
    g_AltStack.ss_size  = RT_MAX(SIGSTKSZ, _128K);
# else
    g_AltStack.ss_size  = _128K;
# endif
#ifdef RT_OS_FREEBSD
    g_AltStack.ss_sp    = (char *)RTMemPageAlloc(g_AltStack.ss_size);
#else
    g_AltStack.ss_sp    = RTMemPageAlloc(g_AltStack.ss_size);
#endif
    RTTESTI_CHECK_RET(g_AltStack.ss_sp != NULL, RTEXITCODE_FAILURE);
    RTTESTI_CHECK_RC_RET(sigaltstack(&g_AltStack, NULL), 0, RTEXITCODE_FAILURE);

    /* Default signal action config. */
    struct sigaction Act;
    RT_ZERO(Act);
    Act.sa_sigaction  = CidetAppSigHandler;
    Act.sa_flags      = SA_SIGINFO | SA_ONSTACK;
    sigfillset(&Act.sa_mask);

    /* Hook the signals we might need. */
    sigaction(SIGILL,  &Act, NULL);
    sigaction(SIGTRAP, &Act, NULL);
# ifdef SIGEMT
    sigaction(SIGEMT,  &Act, NULL);
# endif
    sigaction(SIGFPE,  &Act, NULL);
    sigaction(SIGBUS,  &Act, NULL);
    sigaction(SIGSEGV, &Act, NULL);

#elif defined(RT_OS_WINDOWS)
    /*
     * Register vectored exception handler and override the default unhandled
     * exception filter, just to be on the safe side...
     */
    RTTESTI_CHECK(AddVectoredExceptionHandler(1 /* first */, CidetAppVectoredXcptHandler) != NULL);
    SetUnhandledExceptionFilter(CidetAppUnhandledXcptFilter);
#endif

    /*
     * Do the work.
     */
    RTTestBanner(g_hTest);

    PCIDETAPP pThis;
    int rc = CidetAppCreate(&pThis);
    if (RT_SUCCESS(rc))
    {
        CidetAppTestBunch(pThis, g_aCidetInstructions1, g_cCidetInstructions1, "First Bunch");

        CidetAppDestroy(pThis);
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

