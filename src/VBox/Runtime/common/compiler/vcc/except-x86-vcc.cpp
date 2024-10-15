/* $Id: except-x86-vcc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Visual C++ Compiler - x86 Exception Handler Filter.
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
#include <iprt/nt/nt-and-windows.h>

#include "internal/compiler-vcc.h"
#include "except-vcc.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Extended exception registration record used by rtVccEh4DoLocalUnwind
 * and rtVccEh4DoLocalUnwindHandler.
 */
typedef struct EH4_LOCAL_UNWIND_XCPT_REG
{
    /** Security cookie. */
    uintptr_t                       uEHCookieFront;
    /** The actual registration record.   */
    EXCEPTION_REGISTRATION_RECORD   XcptRegRec;
    /** @name rtVccEh4DoLocalUnwind parameters
     * @{ */
    PEH4_XCPT_REG_REC_T             pEh4XcptRegRec;
    uint32_t                        uTargetTryLevel;
    uint8_t const                  *pbFrame;
    /** @} */
    /** Security cookie. */
    uintptr_t                       uEHCookieBack;
} EH4_LOCAL_UNWIND_XCPT_REG;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern "C" uintptr_t __security_cookie;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLASM(LONG)                   rtVccEh4DoFiltering(PFN_EH4_XCPT_FILTER_T pfnFilter, uint8_t const *pbFrame);
DECLASM(DECL_NO_RETURN(void))   rtVccEh4JumpToHandler(PFN_EH4_XCPT_HANDLER_T pfnHandler, uint8_t const *pbFrame);
DECLASM(void)                   rtVccEh4DoGlobalUnwind(PEXCEPTION_RECORD pXcptRec, PEXCEPTION_REGISTRATION_RECORD pXcptRegRec);
DECLASM(void)                   rtVccEh4DoFinally(PFN_EH4_FINALLY_T pfnFinally, bool fAbend, uint8_t const *pbFrame);
extern "C" EXCEPTION_DISPOSITION __stdcall
rtVccEh4DoLocalUnwindHandler(PEXCEPTION_RECORD pXcptRec, PVOID pvEstFrame, PCONTEXT pCpuCtx, PVOID pvDispCtx);


#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4733)  /* warning C4733: Inline asm assigning to 'FS:0': handler not registered as safe handler */
#endif

/**
 * Calls the __finally blocks up to @a uTargetTryLevel is reached, starting with
 * @a pEh4XcptRegRec->uTryLevel.
 *
 * @param   pEh4XcptRegRec  The EH4 exception registration record.
 * @param   uTargetTryLevel The target __try level to stop unwinding at.
 * @param   pbFrame         The frame pointer (EBP).
 */
static void rtVccEh4DoLocalUnwind(PEH4_XCPT_REG_REC_T pEh4XcptRegRec, uint32_t uTargetTryLevel, uint8_t const *pbFrame)
{
    /*
     * Manually set up exception handler.
     */
    EH4_LOCAL_UNWIND_XCPT_REG RegRec =
    {
        __security_cookie ^ (uintptr_t)&RegRec,
        {
            (EXCEPTION_REGISTRATION_RECORD *)__readfsdword(RT_UOFFSETOF(NT_TIB, ExceptionList)),
            rtVccEh4DoLocalUnwindHandler /* safeseh (.sxdata) entry emitted by except-x86-vcc-asm.asm */
        },
        pEh4XcptRegRec,
        uTargetTryLevel,
        pbFrame,
        __security_cookie ^ (uintptr_t)&RegRec
    };
    __writefsdword(RT_UOFFSETOF(NT_TIB, ExceptionList), (uintptr_t)&RegRec.XcptRegRec);

    /*
     * Do the unwinding.
     */
    uint32_t uCurTryLevel = pEh4XcptRegRec->uTryLevel;
    while (   uCurTryLevel != EH4_TOPMOST_TRY_LEVEL
           && (   uCurTryLevel > uTargetTryLevel
               || uTargetTryLevel == EH4_TOPMOST_TRY_LEVEL /* if we knew what 0xffffffff meant, this could probably be omitted */ ))
    {
        PCEH4_SCOPE_TAB_T const pScopeTable = (PCEH4_SCOPE_TAB_T)(pEh4XcptRegRec->uEncodedScopeTable ^ __security_cookie);
        PCEH4_SCOPE_TAB_REC_T const pEntry  = &pScopeTable->aScopeRecords[uCurTryLevel];

        pEh4XcptRegRec->uTryLevel = uCurTryLevel = pEntry->uEnclosingLevel;

        /* __finally scope table entries have no filter sub-function. */
        if (!pEntry->pfnFilter)
        {
            //RTAssertMsg2("rtVccEh4DoLocalUnwind: Calling %p (level %#x)\n", pEntry->pfnFinally, uCurTryLevel);
            rtVccEh4DoFinally(pEntry->pfnFinally, true /*fAbend*/, pbFrame);

            /* Read the try level again in case it changed... */
            uCurTryLevel = pEh4XcptRegRec->uTryLevel;
        }
    }

    /*
     * Deregister exception handler.
     */
    __writefsdword(RT_UOFFSETOF(NT_TIB, ExceptionList), (uintptr_t)RegRec.XcptRegRec.Next);
}

#ifdef _MSC_VER
# pragma warning(pop)
#endif

/**
 * Exception handler for rtVccEh4DoLocalUnwind.
 */
EXCEPTION_DISPOSITION __stdcall
rtVccEh4DoLocalUnwindHandler(PEXCEPTION_RECORD pXcptRec, PVOID pvEstFrame, PCONTEXT pCpuCtx, PVOID pvDispCtx)
{
    EH4_LOCAL_UNWIND_XCPT_REG *pMyRegRec = RT_FROM_MEMBER(pvEstFrame, EH4_LOCAL_UNWIND_XCPT_REG, XcptRegRec);
    __security_check_cookie(pMyRegRec->uEHCookieFront ^ (uintptr_t)pMyRegRec);
    __security_check_cookie(pMyRegRec->uEHCookieBack  ^ (uintptr_t)pMyRegRec);

    /*
     * This is a little sketchy as it isn't all that well documented by the OS
     * vendor, but if invoked while unwinding, we return ExceptionCollidedUnwind
     * and update the *ppDispCtx value to point to the colliding one.
     */
    if (pXcptRec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        rtVccEh4DoLocalUnwind(pMyRegRec->pEh4XcptRegRec, pMyRegRec->uTargetTryLevel, pMyRegRec->pbFrame);

        PEXCEPTION_REGISTRATION_RECORD *ppDispCtx = (PEXCEPTION_REGISTRATION_RECORD *)pvDispCtx;
        *ppDispCtx = &pMyRegRec->XcptRegRec;
        return ExceptionCollidedUnwind;
    }

    /*
     * In all other cases we do nothing special.
     */
    RT_NOREF(pCpuCtx);
    return ExceptionContinueSearch;
}


/**
 * This validates the CPU context, may terminate the application if invalid.
 */
DECLINLINE(void) rtVccValidateExceptionContextRecord(PCONTEXT pCpuCtx)
{
    if (RT_LIKELY(   !rtVccIsGuardICallChecksActive()
                  || rtVccIsPointerOnTheStack(pCpuCtx->Esp)))
    { /* likely */ }
    else
        rtVccCheckContextFailed(pCpuCtx);
}


/**
 * Helper that validates stack cookies.
 */
DECLINLINE(void) rtVccEh4ValidateCookies(PCEH4_SCOPE_TAB_T pScopeTable, uint8_t const *pbFrame)
{
    if (pScopeTable->offGSCookie != EH4_NO_GS_COOKIE)
    {
        uintptr_t uGsCookie = *(uintptr_t const *)&pbFrame[pScopeTable->offGSCookie];
        uGsCookie          ^=          (uintptr_t)&pbFrame[pScopeTable->offGSCookieXor];
        __security_check_cookie(uGsCookie);
    }

    uintptr_t uEhCookie = *(uintptr_t const *)&pbFrame[pScopeTable->offEHCookie];
    uEhCookie          ^=          (uintptr_t)&pbFrame[pScopeTable->offEHCookieXor];
    __security_check_cookie(uEhCookie);
}


/**
 * Call exception filters, handlers and unwind code for x86 code.
 *
 * This is called for windows' structured exception handling (SEH) in x86 32-bit
 * code, i.e. the __try/__except/__finally stuff in Visual C++.  The compiler
 * generate scope records for the __try/__except blocks as well as unwind
 * records for __finally and probably C++ stack object destructors.
 *
 * @returns Exception disposition.
 * @param   pXcptRec    The exception record.
 * @param   pXcptRegRec The exception registration record, taken to be the frame
 *                      address.
 * @param   pCpuCtx     The CPU context for the exception.
 * @param   pDispCtx    Dispatcher context.
 */
extern "C" __declspec(guard(suppress))
DWORD _except_handler4(PEXCEPTION_RECORD pXcptRec, PEXCEPTION_REGISTRATION_RECORD pXcptRegRec, PCONTEXT pCpuCtx, PVOID pvCtx)
{
    /*
     * The registration record (probably chained on FS:[0] like in the OS/2 days)
     * points to a larger structure specific to _except_handler4.  The structure
     * is planted right after the saved caller EBP value when establishing the
     * stack frame, so EBP = pXcptRegRec + 1;
     */
    PEH4_XCPT_REG_REC_T const pEh4XcptRegRec = RT_FROM_MEMBER(pXcptRegRec, EH4_XCPT_REG_REC_T, XcptRec);
    uint8_t * const           pbFrame        = (uint8_t *)&pEh4XcptRegRec[1];
    PCEH4_SCOPE_TAB_T const   pScopeTable    = (PCEH4_SCOPE_TAB_T)(pEh4XcptRegRec->uEncodedScopeTable ^ __security_cookie);

    /*
     * Validate the stack cookie and exception context.
     */
    rtVccEh4ValidateCookies(pScopeTable, pbFrame);
    rtVccValidateExceptionContextRecord(pCpuCtx);

    /*
     * If dispatching an exception, call the exception filter functions and jump
     * to the __except blocks if so directed.
     */
    if (IS_DISPATCHING(pXcptRec->ExceptionFlags))
    {
        uint32_t uTryLevel = pEh4XcptRegRec->uTryLevel;
        //RTAssertMsg2("_except_handler4: dispatch: uTryLevel=%#x\n", uTryLevel);
        while (uTryLevel != EH4_TOPMOST_TRY_LEVEL)
        {
            PCEH4_SCOPE_TAB_REC_T const pEntry    = &pScopeTable->aScopeRecords[uTryLevel];
            PFN_EH4_XCPT_FILTER_T const pfnFilter = pEntry->pfnFilter;
            if (pfnFilter)
            {
                /* Call the __except filtering expression: */
                //RTAssertMsg2("_except_handler4: Calling pfnFilter=%p\n", pfnFilter);
                EXCEPTION_POINTERS XcptPtrs = { pXcptRec, pCpuCtx };
                pEh4XcptRegRec->pXctpPtrs = &XcptPtrs;
                LONG lRet = rtVccEh4DoFiltering(pfnFilter, pbFrame);
                pEh4XcptRegRec->pXctpPtrs = NULL;
                //RTAssertMsg2("_except_handler4: pfnFilter=%p -> %ld\n", pfnFilter, lRet);
                rtVccEh4ValidateCookies(pScopeTable, pbFrame);

                /* Return if we're supposed to continue execution (the convention
                   it to match negative values rather than the exact defined value):  */
                AssertCompile(EXCEPTION_CONTINUE_EXECUTION == -1);
                if (lRet <= EXCEPTION_CONTINUE_EXECUTION)
                    return ExceptionContinueExecution;

                /* Similarly, the handler is executed for any positive value. */
                AssertCompile(EXCEPTION_CONTINUE_SEARCH == 0);
                AssertCompile(EXCEPTION_EXECUTE_HANDLER == 1);
                if (lRet >= EXCEPTION_EXECUTE_HANDLER)
                {
                    /* We're about to resume execution in the __except block, so unwind
                       up to it first. */
                    //RTAssertMsg2("_except_handler4: global unwind\n");
                    rtVccEh4DoGlobalUnwind(pXcptRec, &pEh4XcptRegRec->XcptRec);
                    if (pEh4XcptRegRec->uTryLevel != EH4_TOPMOST_TRY_LEVEL)
                    {
                        //RTAssertMsg2("_except_handler4: local unwind\n");
                        rtVccEh4DoLocalUnwind(pEh4XcptRegRec, uTryLevel, pbFrame);
                    }
                    rtVccEh4ValidateCookies(pScopeTable, pbFrame);

                    /* Now jump to the __except block.  This will _not_ return. */
                    //RTAssertMsg2("_except_handler4: jumping to __except block %p (level %#x)\n", pEntry->pfnHandler, pEntry->uEnclosingLevel);
                    pEh4XcptRegRec->uTryLevel = pEntry->uEnclosingLevel;
                    rtVccEh4ValidateCookies(pScopeTable, pbFrame); /* paranoia^2 */

                    rtVccEh4JumpToHandler(pEntry->pfnHandler, pbFrame);
                    /* (not reached) */
                }
            }

            /*
             * Next try level.
             */
            uTryLevel = pEntry->uEnclosingLevel;
        }
    }
    /*
     * If not dispatching we're unwinding, so we call any __finally blocks.
     */
    else
    {
        //RTAssertMsg2("_except_handler4: unwind: uTryLevel=%#x\n", pEh4XcptRegRec->uTryLevel);
        if (pEh4XcptRegRec->uTryLevel != EH4_TOPMOST_TRY_LEVEL)
        {
            rtVccEh4DoLocalUnwind(pEh4XcptRegRec, EH4_TOPMOST_TRY_LEVEL, pbFrame);
            rtVccEh4ValidateCookies(pScopeTable, pbFrame);
        }
    }

    RT_NOREF(pvCtx);
    return ExceptionContinueSearch;
}

