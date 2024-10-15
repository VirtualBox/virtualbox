/* $Id: stacksup-vcc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Visual C++ Compiler - Stack Checking C/C++ Support.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include "internal/nocrt.h"

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#ifndef IPRT_NOCRT_WITHOUT_FATAL_WRITE
# include <iprt/assert.h>
#endif

#include "internal/compiler-vcc.h"
#ifdef IN_RING3
# include <iprt/win/windows.h>
# include "../../../r3/win/internal-r3-win.h" /* ugly, but need some windows API function pointers */
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Gets the program counter member of Windows' CONTEXT structure. */
#if   defined(RT_ARCH_AMD64)
# define MY_GET_PC_FROM_CONTEXT(a_pCtx)  ((a_pCtx)->Rip)
#elif defined(RT_ARCH_X86)
# define MY_GET_PC_FROM_CONTEXT(a_pCtx)  ((a_pCtx)->Eip)
#else
# error "Port Me!"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Variable descriptor. */
typedef struct RTC_VAR_DESC_T
{
    int32_t     offFrame;
    uint32_t    cbVar;
    const char *pszName;
} RTC_VAR_DESC_T;

/** Frame descriptor. */
typedef struct RTC_FRAME_DESC_T
{
    uint32_t                cVars;
    RTC_VAR_DESC_T const   *paVars;
} RTC_FRAME_DESC_T;

#define VARIABLE_MARKER_PRE     0xcccccccc
#define VARIABLE_MARKER_POST    0xcccccccc


/**
 * Alloca allocation entry.
 * @note For whatever reason the pNext and cb members are misaligned on 64-bit
 *       targets.  32-bit targets OTOH adds padding to keep the structure size
 *       and pNext + cb offsets the same.
 */
#pragma pack(4)
typedef struct RTC_ALLOC_ENTRY
{
    uint32_t            uGuard1;
    RTC_ALLOC_ENTRY    *pNext;
#if ARCH_BITS == 32
    uint32_t            pNextPad;
#endif
    size_t              cb;
#if ARCH_BITS == 32
    uint32_t            cbPad;
#endif
    uint32_t            auGuard2[3];
} RTC_ALLOC_ENTRY;
#pragma pack()

#define ALLOCA_FILLER_BYTE      0xcc
#define ALLOCA_FILLER_32        0xcccccccc


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern "C" void __fastcall _RTC_CheckStackVars(uint8_t *pbFrame, RTC_VAR_DESC_T const *pVar); /* nocrt-stack.asm */
extern "C" uintptr_t __security_cookie;


/**
 * Initializes the security cookie value.
 *
 * This must be called as the first thing by the startup code.  We must also no
 * do anything fancy here.
 */
void rtVccInitSecurityCookie(void) RT_NOEXCEPT
{
    __security_cookie = (uintptr_t)ASMReadTSC() ^ (uintptr_t)&__security_cookie;
}


/**
 * Reports a security error.
 *
 * @param   uFastFailCode   The fast fail code.
 * @param   pCpuCtx         The CPU context at the failure location.
 */
static DECL_NO_RETURN(void) rtVccFatalSecurityErrorWithCtx(uint32_t uFastFailCode, PCONTEXT pCpuCtx)
{
#ifdef IN_RING3
    /*
     * Use the __fastfail() approach if available, it is more secure than the stuff below:
     */
    if (g_pfnIsProcessorFeaturePresent && g_pfnIsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE))
        __fastfail(uFastFailCode);

    /*
     * Fallback for legacy systems.
     */
    if (g_pfnIsDebuggerPresent && g_pfnIsDebuggerPresent())
        __debugbreak();

    /* If we can, clear the unhandled exception filter and report and unhandled exception. */
    if (g_pfnSetUnhandledExceptionFilter && g_pfnUnhandledExceptionFilter)
    {
        g_pfnSetUnhandledExceptionFilter(NULL);

        EXCEPTION_RECORD   XcptRec  =
        {
            /* .ExceptionCode = */          STATUS_STACK_BUFFER_OVERRUN,
            /* .ExceptionFlags = */         EXCEPTION_NONCONTINUABLE,
            /* .ExceptionRecord = */        NULL,
# ifdef RT_ARCH_AMD64
            /* .ExceptionAddress = */       (void *)pCpuCtx->Rip,
# elif defined(RT_ARCH_X86)
            /* .ExceptionAddress = */       (void *)pCpuCtx->Eip,
# else
#  error "Port me!"
# endif
            /* .NumberParameters = */       1,
            /* .ExceptionInformation = */   { uFastFailCode, }
        };

        EXCEPTION_POINTERS XcptPtrs = { &XcptRec, pCpuCtx };
        g_pfnUnhandledExceptionFilter(&XcptPtrs);
    }

    for (;;)
        TerminateProcess(GetCurrentProcess(), STATUS_STACK_BUFFER_OVERRUN);

#else
# error "Port ME!"
#endif
}


DECLASM(void) rtVccStackVarCorrupted(uint8_t *pbFrame, RTC_VAR_DESC_T const *pVar, PCONTEXT pCpuCtx)
{
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!Stack corruption!!\n\n"
                 "%p LB %#x - %s\n",
                 pbFrame + pVar->offFrame, pVar->cbVar, pVar->pszName);
#else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Stack corruption!!\r\n\r\n"));
    rtNoCrtFatalWritePtr(pbFrame + pVar->offFrame);
    rtNoCrtFatalWrite(RT_STR_TUPLE(" LB "));
    rtNoCrtFatalWriteX32(pVar->cbVar);
    rtNoCrtFatalWrite(RT_STR_TUPLE(" - "));
    rtNoCrtFatalWriteStr(pVar->pszName);
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
#endif
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_INCORRECT_STACK, pCpuCtx);
}


DECLASM(void) rtVccSecurityCookieMismatch(uintptr_t uCookie, PCONTEXT pCpuCtx)
{
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!Stack cookie corruption!!\n\n"
                 "expected %p, found %p\n",
                 __security_cookie, uCookie);
#else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Stack cookie corruption!!\r\n\r\n"
                                        "expected"));
    rtNoCrtFatalWritePtr((void *)__security_cookie);
    rtNoCrtFatalWrite(RT_STR_TUPLE(", found "));
    rtNoCrtFatalWritePtr((void *)uCookie);
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
#endif
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE, pCpuCtx);
}


#ifdef RT_ARCH_X86
DECLASM(void) rtVccCheckEspFailed(PCONTEXT pCpuCtx)
{
# ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!ESP check failed!!\n\n"
                 "eip=%p esp=%p ebp=%p\n",
                 pCpuCtx->Eip, pCpuCtx->Esp, pCpuCtx->Ebp);
# else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!ESP check failed!!\r\n\r\n"
                                       "eip="));
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Eip);
    rtNoCrtFatalWrite(RT_STR_TUPLE(" esp="));
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Esp);
    rtNoCrtFatalWrite(RT_STR_TUPLE(" ebp="));
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Ebp);
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
# endif
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_INCORRECT_STACK, pCpuCtx);
}
#endif


/** @todo reimplement in assembly (feeling too lazy right now). */
extern "C" void __fastcall _RTC_CheckStackVars2(uint8_t *pbFrame, RTC_VAR_DESC_T const *pVar, RTC_ALLOC_ENTRY *pHead)
{
    while (pHead)
    {
        if (   pHead->uGuard1     == ALLOCA_FILLER_32
#if 1 && ARCH_BITS == 32
            && pHead->pNextPad    == ALLOCA_FILLER_32
            && pHead->cbPad       == ALLOCA_FILLER_32
#endif
            && pHead->auGuard2[0] == ALLOCA_FILLER_32
            && pHead->auGuard2[1] == ALLOCA_FILLER_32
            && pHead->auGuard2[2] == ALLOCA_FILLER_32
            && *(uint32_t const *)((uint8_t const *)pHead + pHead->cb - sizeof(uint32_t)) == ALLOCA_FILLER_32)
        { /* likely */ }
        else
        {
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
            RTAssertMsg2("\n\n!!Stack corruption (alloca)!!\n\n"
                         "%p LB %#x\n",
                         pHead, pHead->cb);
#else
            rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Stack corruption (alloca)!!\r\n\r\n"));
            rtNoCrtFatalWritePtr(pHead);
            rtNoCrtFatalWrite(RT_STR_TUPLE(" LB "));
            rtNoCrtFatalWriteX64(pHead->cb);
            rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
#endif
#ifdef IN_RING3
            if (g_pfnIsDebuggerPresent && g_pfnIsDebuggerPresent())
#endif
                RT_BREAKPOINT();
        }
        pHead = pHead->pNext;
    }

    _RTC_CheckStackVars(pbFrame, pVar);
}


DECLASM(void) rtVccRangeCheckFailed(PCONTEXT pCpuCtx)
{
# ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!Range check failed at %p!!\n\n", MY_GET_PC_FROM_CONTEXT(pCpuCtx));
# else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Range check failed at "));
    rtNoCrtFatalWritePtr((void *)MY_GET_PC_FROM_CONTEXT(pCpuCtx));
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("!!\r\n"));
# endif
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_RANGE_CHECK_FAILURE, pCpuCtx);
}


/** Whether or not this should be a fatal issue remains to be seen. See
 *  explanation in stack-vcc.asm.  */
#if 0
DECLASM(void) rtVccUninitializedVariableUse(const char *pszVar, PCONTEXT pCpuCtx)
#else
extern "C" void __cdecl _RTC_UninitUse(const char *pszVar)
#endif
{
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!Used uninitialized variable %s at %p!!\n\n",
                 pszVar ? pszVar : "", ASMReturnAddress());
#else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Used uninitialized variable "));
    rtNoCrtFatalWriteStr(pszVar);
    rtNoCrtFatalWrite(RT_STR_TUPLE(" at "));
    rtNoCrtFatalWritePtr(ASMReturnAddress());
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("!!\r\n\r\n"));
#endif
#if 0
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_FATAL_APP_EXIT, pCpuCtx);
#else
# ifdef IN_RING3
    if (g_pfnIsDebuggerPresent && g_pfnIsDebuggerPresent())
# endif
        RT_BREAKPOINT();
#endif
}


void rtVccCheckContextFailed(PCONTEXT pCpuCtx)
{
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
    RTAssertMsg2("\n\n!!Context (stack) check failed!!\n\n"
                 "PC=%p SP=%p BP=%p\n",
# ifdef RT_ARCH_AMD64
                 pCpuCtx->Rip, pCpuCtx->Rsp, pCpuCtx->Rbp
# elif defined(RT_ARCH_X86)
                 pCpuCtx->Eip, pCpuCtx->Esp, pCpuCtx->Ebp
# else
#  error "unsupported arch"
# endif
                 );
#else
    rtNoCrtFatalWriteBegin(RT_STR_TUPLE("\r\n\r\n!!Context (stack) check failed!!\r\n\r\n"
                                       "PC="));
# ifdef RT_ARCH_AMD64
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Rip);
# elif defined(RT_ARCH_X86)
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Eip);
# else
#  error "unsupported arch"
# endif
    rtNoCrtFatalWrite(RT_STR_TUPLE(" SP="));
# ifdef RT_ARCH_AMD64
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Rsp);
# elif defined(RT_ARCH_X86)
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Esp);
# endif
    rtNoCrtFatalWrite(RT_STR_TUPLE(" BP="));
# ifdef RT_ARCH_AMD64
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Rbp);
# elif defined(RT_ARCH_X86)
    rtNoCrtFatalWritePtr((void *)pCpuCtx->Ebp);
# endif
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
#endif
    rtVccFatalSecurityErrorWithCtx(FAST_FAIL_INVALID_SET_OF_CONTEXT, pCpuCtx);
}

