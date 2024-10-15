/* $Id: tstRTNoCrt-3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Testcase for the No-CRT SEH bits on Windows.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <iprt/win/windows.h>
#include <iprt/string.h>
#include <iprt/test.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RTTEST  g_hTest;


/*
 * Simple access violation with a filter and handler that's called.
 */

static int tstSeh1Filter(uint32_t uStatus, PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh1Filter: uStatus=%#x pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 uStatus, pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(uStatus == STATUS_ACCESS_VIOLATION,                 ("uStatus=%#x\n", uStatus));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",           pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n",        pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x42,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    return EXCEPTION_EXECUTE_HANDLER;
}

static void tstSeh1(void)
{
    RTTestSub(g_hTest, "SEH #1");
    uint8_t volatile cHandlerCalled = 0;
    __try
    {
        uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x42;
        *pu8 = 0x42;
    }
    __except(tstSeh1Filter(GetExceptionCode(), GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh1/1: __except\n");
        cHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cHandlerCalled == 1, ("cHandlerCalled=%d\n", cHandlerCalled));
}


/*
 * Same but handler not called (continue search).  We need to use a 2nd
 * wrapper here to avoid terminating the app.
 */

static int tstSeh2Filter(uint32_t uStatus, PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh2Filter: uStatus=%#x pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 uStatus, pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(uStatus == STATUS_ACCESS_VIOLATION,                 ("uStatus=%#x\n", uStatus));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",           pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n",        pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x42,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    return EXCEPTION_CONTINUE_SEARCH;
}

static void tstSeh2(void)
{
    RTTestSub(g_hTest, "SEH #2");
    uint8_t volatile cInnerHandlerCalled = 0;
    uint8_t volatile cOuterHandlerCalled = 0;
    __try
    {
        __try
        {
            uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x42;
            *pu8 = 0x42;
        }
        __except(tstSeh2Filter(GetExceptionCode(), GetExceptionInformation()))
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh2/inner: __except\n");
            cInnerHandlerCalled++;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh2/outer: __except\n");
        cOuterHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cInnerHandlerCalled == 0, ("cHandlerCalled=%d\n", cInnerHandlerCalled));
    RTTESTI_CHECK_MSG(cOuterHandlerCalled == 1, ("cOuterHandlerCalled=%d\n", cOuterHandlerCalled));
}


/*
 * Simple finally construct.
 */
static void tstSeh3(void)
{
    RTTestSub(g_hTest, "SEH #3");
    uint8_t volatile cFinallyHandlerCalled = 0;
    uint8_t volatile cOuterHandlerCalled   = 0;
    __try
    {
        __try
        {
            uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x42;
            *pu8 = 0x42;
        }
        __finally
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh3/inner: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
            RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
            cFinallyHandlerCalled++;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh3/outer: __except\n");
        cOuterHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cFinallyHandlerCalled == 1, ("cFinallyHandlerCalled=%d\n", cFinallyHandlerCalled));
    RTTESTI_CHECK_MSG(cOuterHandlerCalled == 1, ("cOuterHandlerCalled=%d\n", cOuterHandlerCalled));
}


/*
 * Continue execution.
 */
static volatile unsigned g_cSeh4FilterCalls = 0;

static int tstSeh4Filter(PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh4Filter: pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters);
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == UINT32_C(0xc0c1c2c3), ("ExceptionCode=%#x\n",    pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 0,                 ("NumberParameters=%#x\n", pXcptRec->NumberParameters));
    g_cSeh4FilterCalls++;
    return EXCEPTION_CONTINUE_EXECUTION;
}


static void tstSeh4(void)
{
    RTTestSub(g_hTest, "SEH #4");
    uint8_t volatile cHandlerCalled   = 0;
    uint8_t volatile cContinued       = 0;
    g_cSeh4FilterCalls = 0;
    __try
    {
        RaiseException(UINT32_C(0xc0c1c2c3), 0, 0, NULL);
        cContinued++;
    }
    __except(tstSeh4Filter(GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh4/outer: __except\n");
        cHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cContinued     == 1, ("cContinued=%d\n", cContinued));
    RTTESTI_CHECK_MSG(cHandlerCalled == 0, ("cHandlerCalled=%d\n", cHandlerCalled));
    RTTESTI_CHECK_MSG(g_cSeh4FilterCalls == 1, ("g_cSeh4FilterCalls=%d\n", g_cSeh4FilterCalls));
}


/*
 * Catching exception in sub function.
 */
unsigned volatile g_cSeh5InnerCalls  = 0;
unsigned volatile g_cSeh5FilterCalls = 0;

DECL_NO_INLINE(extern, void) tstSeh5Inner(void)
{
    uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x22;
    g_cSeh5InnerCalls++;
    *pu8 = 0x22;
}


static int tstSeh5Filter(PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh5Filter: pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",    pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n", pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x22,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    g_cSeh5FilterCalls++;
    return EXCEPTION_EXECUTE_HANDLER;
}


static void tstSeh5(void)
{
    RTTestSub(g_hTest, "SEH #5");
    uint8_t volatile cHandlerCalled = 0;
    g_cSeh5InnerCalls  = 0;
    g_cSeh5FilterCalls = 0;
    __try
    {
        tstSeh5Inner();
        RTTestIFailed("tstSeh5Inner returned");
    }
    __except(tstSeh5Filter(GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh5: __except\n");
        cHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cHandlerCalled == 1, ("cHandlerCalled=%d\n", cHandlerCalled));
    RTTESTI_CHECK_MSG(g_cSeh5InnerCalls == 1, ("g_cSeh5FilterCalls=%d\n", g_cSeh5FilterCalls));
    RTTESTI_CHECK_MSG(g_cSeh5FilterCalls == 1, ("g_cSeh5FilterCalls=%d\n", g_cSeh5FilterCalls));
}


/*
 * Catching exception in sub function with a __try/__finally block in it.
 */
unsigned volatile g_cSeh6InnerCalls        = 0;
unsigned volatile g_cSeh6InnerFinallyCalls = 0;
unsigned volatile g_cSeh6FilterCalls       = 0;

DECL_NO_INLINE(extern, void) tstSeh6Inner(void)
{
    __try
    {
        uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x22;
        g_cSeh6InnerCalls++;
        *pu8 = 0x22;
    }
    __finally
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh6Inner: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
        RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
        g_cSeh6InnerFinallyCalls++;
    }
}


static int tstSeh6Filter(PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh6Filter: pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",    pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n", pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x22,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    g_cSeh6FilterCalls++;
    return EXCEPTION_EXECUTE_HANDLER;
}


static void tstSeh6(void)
{
    RTTestSub(g_hTest, "SEH #6");
    uint8_t volatile cHandlerCalled = 0;
    g_cSeh6InnerCalls        = 0;
    g_cSeh6FilterCalls       = 0;
    g_cSeh6InnerFinallyCalls = 0;
    __try
    {
        tstSeh6Inner();
        RTTestIFailed("tstSeh6Inner returned");
    }
    __except(tstSeh6Filter(GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh6: __except\n");
        cHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cHandlerCalled == 1, ("cHandlerCalled=%d\n", cHandlerCalled));
    RTTESTI_CHECK_MSG(g_cSeh6InnerCalls == 1, ("g_cSeh6FilterCalls=%d\n", g_cSeh6FilterCalls));
    RTTESTI_CHECK_MSG(g_cSeh6FilterCalls == 1, ("g_cSeh6FilterCalls=%d\n", g_cSeh6FilterCalls));
    RTTESTI_CHECK_MSG(g_cSeh6InnerFinallyCalls == 1, ("g_cSeh6InnerFinallyCalls=%d\n", g_cSeh6InnerFinallyCalls));
}


/*
 * Catching exception in sub function with a __try/__finally block in it as well as the caller.
 */
unsigned volatile g_cSeh7InnerCalls        = 0;
unsigned volatile g_cSeh7InnerFinallyCalls = 0;
unsigned volatile g_cSeh7FilterCalls       = 0;

DECL_NO_INLINE(extern, void) tstSeh7Inner(void)
{
    __try
    {
        uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x22;
        g_cSeh7InnerCalls++;
        *pu8 = 0x22;
    }
    __finally
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh7Inner: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
        RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
        g_cSeh7InnerFinallyCalls++;
    }
}


static int tstSeh7Filter(PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh7Filter: pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",    pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n", pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x22,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    g_cSeh7FilterCalls++;
    return EXCEPTION_EXECUTE_HANDLER;
}


static void tstSeh7(void)
{
    RTTestSub(g_hTest, "SEH #7");
    uint8_t volatile cHandlerCalled = 0;
    uint8_t volatile cOuterFinallyCalls = 0;
    g_cSeh7InnerCalls        = 0;
    g_cSeh7FilterCalls       = 0;
    g_cSeh7InnerFinallyCalls = 0;
    __try
    {
        __try
        {
            tstSeh7Inner();
        }
        __finally
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh7: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
            RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
            cOuterFinallyCalls++;
        }
        RTTestIFailed("tstSeh7Inner returned");
    }
    __except(tstSeh7Filter(GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh7: __except\n");
        cHandlerCalled++;
    }
    RTTESTI_CHECK_MSG(cHandlerCalled == 1, ("cHandlerCalled=%d\n", cHandlerCalled));
    RTTESTI_CHECK_MSG(cOuterFinallyCalls == 1, ("ccOuterFinallyCalls=%d\n", cOuterFinallyCalls));
    RTTESTI_CHECK_MSG(g_cSeh7InnerCalls == 1, ("g_cSeh7FilterCalls=%d\n", g_cSeh7FilterCalls));
    RTTESTI_CHECK_MSG(g_cSeh7FilterCalls == 1, ("g_cSeh7FilterCalls=%d\n", g_cSeh7FilterCalls));
    RTTESTI_CHECK_MSG(g_cSeh7InnerFinallyCalls == 1, ("g_cSeh7InnerFinallyCalls=%d\n", g_cSeh7InnerFinallyCalls));
}


/*
 * Much nested setup.
 */
unsigned volatile g_acSeh8Calls[6]        = {0};
unsigned volatile g_acSeh8FilterCalls[6]  = {0};
unsigned volatile g_acSeh8FinallyCalls[6] = {0};


DECL_NO_INLINE(extern, void) tstSeh8Inner5(void)
{
    __try
    {
        uint8_t volatile *pu8 = (uint8_t volatile *)(uintptr_t)0x22;
        g_acSeh8Calls[5] += 1;
        *pu8 = 0x22;
    }
    __finally
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh8Inner5: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
        RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
        g_acSeh8FinallyCalls[5] += 1;
    }
}


DECL_NO_INLINE(extern, void) tstSeh8Inner4(void)
{
    __try
    {
        g_acSeh8Calls[4] += 1;
        tstSeh8Inner5();
    }
    __finally
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh8Inner4: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
        RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
        g_acSeh8FinallyCalls[4] += 1;
    }
}


DECL_NO_INLINE(extern, void) tstSeh8Inner3(void)
{
    __try
    {
        __try
        {
            g_acSeh8Calls[3] += 1;
            tstSeh8Inner4();
        }
        __finally
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh8Inner3: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
            RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
            g_acSeh8FinallyCalls[3] += 1;
        }
    }
    __except(  g_acSeh8FilterCalls[3]++ == 0 && GetExceptionCode() == STATUS_ACCESS_VIOLATION
             ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
    {
        RTTestIFailed("tstSeh3Inner3: Unexpected __except");
    }
}


DECL_NO_INLINE(extern, void) tstSeh8Inner2(void)
{
    g_acSeh8Calls[2] += 1;
    tstSeh8Inner3();
}


DECL_NO_INLINE(extern, void) tstSeh8Inner1(void)
{
    __try
    {
        g_acSeh8Calls[1] += 1;
        tstSeh8Inner2();
    }
    __except(  g_acSeh8FilterCalls[1]++ == 0 && GetExceptionCode() == STATUS_ACCESS_VIOLATION
             ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
    {
        RTTestIFailed("tstSeh3Inner1: Unexpected __except");
    }
}


static int tstSeh8Filter(PEXCEPTION_POINTERS pXcptPtrs)
{
    PEXCEPTION_RECORD const pXcptRec = pXcptPtrs->ExceptionRecord;
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "tstSeh8Filter: pXcptPtrs=%p: Code=%#x Flags=%#x Rec=%p Addr=%#x cParms=%#x %p %p\n",
                 pXcptPtrs,
                 pXcptRec->ExceptionCode,
                 pXcptRec->ExceptionFlags,
                 pXcptRec->ExceptionRecord,
                 pXcptRec->ExceptionAddress,
                 pXcptRec->NumberParameters,
                 pXcptRec->ExceptionInformation[0],
                 pXcptRec->ExceptionInformation[1]);
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionCode == STATUS_ACCESS_VIOLATION, ("ExceptionCode=%#x\n",    pXcptRec->ExceptionCode));
    RTTESTI_CHECK_MSG(pXcptRec->NumberParameters == 2,                    ("NumberParameters=%#x\n", pXcptRec->NumberParameters));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[0] == 1,             ("ExceptionInformation[0]=%#x\n", pXcptRec->ExceptionInformation[0]));
    RTTESTI_CHECK_MSG(pXcptRec->ExceptionInformation[1] == 0x22,          ("ExceptionInformation[1]=%#x\n", pXcptRec->ExceptionInformation[1]));
    g_acSeh8FilterCalls[0] += 1;
    return EXCEPTION_EXECUTE_HANDLER;
}


static void tstSeh8(void)
{
    RTTestSub(g_hTest, "SEH #8");
    for (unsigned i = 0; i < RT_ELEMENTS(g_acSeh8FinallyCalls); i++)
    {
        g_acSeh8Calls[i]        = 0;
        g_acSeh8FilterCalls[i]  = 0;
        g_acSeh8FinallyCalls[i] = 0;
    }
    uint8_t volatile cHandlerCalled = 0;
    __try
    {
        __try
        {
            g_acSeh8Calls[0] += 1;
            tstSeh8Inner1();
        }
        __finally
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh8: __finally: AbnormalTermination()=>%d\n", AbnormalTermination());
            RTTESTI_CHECK_MSG(AbnormalTermination() == 1, ("AbnormalTermination()=>%d\n", AbnormalTermination()));
            g_acSeh8FinallyCalls[0] += 1;
        }
        RTTestIFailed("tstSeh8Inner returned");
    }
    __except(tstSeh8Filter(GetExceptionInformation()))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstSeh8: __except\n");
        cHandlerCalled++;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(g_acSeh8Calls); i++)
        RTTESTI_CHECK(g_acSeh8Calls[i] == 1);
    RTTESTI_CHECK_MSG(cHandlerCalled == 1, ("cHandlerCalled=%d\n", cHandlerCalled));
    RTTESTI_CHECK(g_acSeh8FilterCalls[0] == 1);
    RTTESTI_CHECK(g_acSeh8FilterCalls[1] == 1);
    RTTESTI_CHECK(g_acSeh8FilterCalls[2] == 0);
    RTTESTI_CHECK(g_acSeh8FilterCalls[3] == 1);
    RTTESTI_CHECK(g_acSeh8FilterCalls[4] == 0);
    RTTESTI_CHECK(g_acSeh8FilterCalls[5] == 0);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[0] == 1);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[1] == 0);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[2] == 0);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[3] == 1);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[4] == 1);
    RTTESTI_CHECK(g_acSeh8FinallyCalls[5] == 1);
}



int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTNoCrt-3", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    tstSeh1();
    tstSeh2();
    tstSeh3();
    tstSeh4();
    tstSeh5();
    tstSeh6();
    tstSeh7();
    tstSeh8();

    return RTTestSummaryAndDestroy(g_hTest);
}

