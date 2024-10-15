/* $Id: tstVMREQ.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM Testcase.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TESTCASE    "tstVMREQ"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RTTEST g_hTest;


/**
 * Testings va_list passing in VMSetRuntimeError.
 */
static DECLCALLBACK(void) MyAtRuntimeError(PUVM pUVM, void *pvUser, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    NOREF(pUVM);
    if (strcmp((const char *)pvUser, "user argument"))
        RTTestFailed(g_hTest, "pvUser=%p:{%s}!\n", pvUser, (const char *)pvUser);
    if (fFlags)
        RTTestFailed(g_hTest, "fFlags=%#x!\n", fFlags);
    if (strcmp(pszErrorId, "enum"))
        RTTestFailed(g_hTest, "pszErrorId=%p:{%s}!\n", pszErrorId, pszErrorId);
    if (strcmp(pszFormat, "some %s string"))
        RTTestFailed(g_hTest, "pszFormat=%p:{%s}!\n", pszFormat, pszFormat);

    char szBuf[1024];
    RTStrPrintfV(szBuf, sizeof(szBuf), pszFormat, va);
    if (strcmp(szBuf, "some error string"))
        RTTestFailed(g_hTest, "RTStrPrintfV -> '%s'!\n", szBuf);
}


/**
 * The function PassVA and PassVA2 calls.
 */
static DECLCALLBACK(int) PassVACallback(PUVM pUVM, unsigned u4K, unsigned u1G, const char *pszFormat, va_list *pva)
{
    NOREF(pUVM);
    if (u4K != _4K)
        RTTestFailed(g_hTest, "u4K=%#x!\n", u4K);
    if (u1G != _1G)
        RTTestFailed(g_hTest, "u1G=%#x!\n", u1G);

    if (strcmp(pszFormat, "hello %s"))
        RTTestFailed(g_hTest, "pszFormat=%p:{%s}!\n", pszFormat, pszFormat);

    char szBuf[1024];
    RTStrPrintfV(szBuf, sizeof(szBuf), pszFormat, *pva);
    if (strcmp(szBuf, "hello world"))
        RTTestFailed(g_hTest, "RTStrPrintfV -> '%s'!\n", szBuf);

    return VINF_SUCCESS;
}


/**
 * Functions that tests passing a va_list * argument in a request,
 * similar to VMSetRuntimeError.
 */
static void PassVA2(PUVM pUVM, const char *pszFormat, va_list va)
{
#if 0 /** @todo test if this is a GCC problem only or also happens with AMD64+VCC80... */
    void *pvVA = &va;
#else
    va_list va2;
    va_copy(va2, va);
    void *pvVA = &va2;
#endif

    int rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)PassVACallback, 5, pUVM, _4K, _1G, pszFormat, pvVA);
    NOREF(rc);

#if 1
    va_end(va2);
#endif
}


/**
 * Functions that tests passing a va_list * argument in a request,
 * similar to VMSetRuntimeError.
 */
static void PassVA(PUVM pUVM, const char *pszFormat, ...)
{
    /* 1st test */
    va_list va1;
    va_start(va1, pszFormat);
    int rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)PassVACallback, 5, pUVM, _4K, _1G, pszFormat, &va1);
    va_end(va1);
    NOREF(rc);

    /* 2nd test */
    va_list va2;
    va_start(va2, pszFormat);
    PassVA2(pUVM, pszFormat, va2);
    va_end(va2);
}


/**
 * Thread function which allocates and frees requests like wildfire.
 */
static DECLCALLBACK(int) Thread(RTTHREAD hThreadSelf, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PUVM pUVM = (PUVM)pvUser;
    NOREF(hThreadSelf);

    for (unsigned i = 0; i < 100000; i++)
    {
        PVMREQ          apReq[17];
        const unsigned  cReqs = i % RT_ELEMENTS(apReq);
        unsigned        iReq;
        for (iReq = 0; iReq < cReqs; iReq++)
        {
            rc = VMR3ReqAlloc(pUVM, &apReq[iReq], VMREQTYPE_INTERNAL, VMCPUID_ANY);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "i=%d iReq=%d cReqs=%d rc=%Rrc (alloc)\n", i, iReq, cReqs, rc);
                return rc;
            }
            apReq[iReq]->iStatus = iReq + i;
        }

        for (iReq = 0; iReq < cReqs; iReq++)
        {
            if (apReq[iReq]->iStatus != (int)(iReq + i))
            {
                RTTestFailed(g_hTest, "i=%d iReq=%d cReqs=%d: iStatus=%d != %d\n", i, iReq, cReqs, apReq[iReq]->iStatus, iReq + i);
                return VERR_GENERAL_FAILURE;
            }
            rc = VMR3ReqFree(apReq[iReq]);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "i=%d iReq=%d cReqs=%d rc=%Rrc (free)\n", i, iReq, cReqs, rc);
                return rc;
            }
        }
        //if (!(i % 10000))
        //    RTPrintf(TESTCASE ": i=%d\n", i);
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   The VMR3ReqCallWaitU test                                                                                                    *
*********************************************************************************************************************************/
static uintptr_t         g_auArgs[32];
static uint32_t volatile g_cCalled = 0;

#define CHECK_ARG(a_iArg, a_ArgValue) do { \
        if ((uintptr_t)(a_ArgValue) != g_auArgs[a_iArg]) \
            RTTestFailed(g_hTest, "%s/#%u: %#zx, expected %#zx!", \
                         __FUNCTION__, a_iArg, (uintptr_t)(a_ArgValue), g_auArgs[a_iArg]); \
    } while (0)

#define CHECK_ARG_EX(a_iArg, a_ArgValue, a_Type) do { \
        if ((a_ArgValue) != (a_Type)g_auArgs[a_iArg]) \
            RTTestFailed(g_hTest, "%s/#%u: %#zx, expected %#zx!", \
                         __FUNCTION__, a_iArg, (uintptr_t)(a_ArgValue), (uintptr_t)(a_Type)g_auArgs[a_iArg]); \
    } while (0)


static DECLCALLBACK(int) CallbackNoArgs(void)
{
    g_cCalled++;
    return VINF_SUCCESS;
}


#define CALLBACKS_WITH_TYPE(a_Type, a_BaseName) \
static DECLCALLBACK(int) a_BaseName##_1Args(a_Type uArg0) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_2Args(a_Type uArg0, a_Type uArg1) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_3Args(a_Type uArg0, a_Type uArg1, a_Type uArg2) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_4Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_5Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_6Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                           a_Type uArg5) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_7Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                            a_Type uArg5, a_Type uArg6) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_8Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                            a_Type uArg5, a_Type uArg6, a_Type uArg7) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_9Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                            a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    return VINF_SUCCESS; \
}

/* The max is now 9. See @bugref{10725}.
static DECLCALLBACK(int) a_BaseName##_10Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                             a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_11Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                             a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9, \
                                             a_Type uArg10) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    CHECK_ARG_EX(10, uArg10, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_12Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                            a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9, \
                                            a_Type uArg10, a_Type uArg11) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    CHECK_ARG_EX(10, uArg10, a_Type); \
    CHECK_ARG_EX(11, uArg11, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_13Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                             a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9, \
                                             a_Type uArg10, a_Type uArg11, a_Type uArg12) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    CHECK_ARG_EX(10, uArg10, a_Type); \
    CHECK_ARG_EX(11, uArg11, a_Type); \
    CHECK_ARG_EX(12, uArg12, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_14Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                             a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9, \
                                             a_Type uArg10, a_Type uArg11, a_Type uArg12, a_Type uArg13) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    CHECK_ARG_EX(10, uArg10, a_Type); \
    CHECK_ARG_EX(11, uArg11, a_Type); \
    CHECK_ARG_EX(12, uArg12, a_Type); \
    CHECK_ARG_EX(13, uArg13, a_Type); \
    return VINF_SUCCESS; \
} \
\
\
static DECLCALLBACK(int) a_BaseName##_15Args(a_Type uArg0, a_Type uArg1, a_Type uArg2, a_Type uArg3, a_Type uArg4, \
                                             a_Type uArg5, a_Type uArg6, a_Type uArg7, a_Type uArg8, a_Type uArg9, \
                                             a_Type uArg10, a_Type uArg11, a_Type uArg12, a_Type uArg13, a_Type uArg14) \
{ \
    g_cCalled++; \
    CHECK_ARG_EX(0, uArg0, a_Type); \
    CHECK_ARG_EX(1, uArg1, a_Type); \
    CHECK_ARG_EX(2, uArg2, a_Type); \
    CHECK_ARG_EX(3, uArg3, a_Type); \
    CHECK_ARG_EX(4, uArg4, a_Type); \
    CHECK_ARG_EX(5, uArg5, a_Type); \
    CHECK_ARG_EX(6, uArg6, a_Type); \
    CHECK_ARG_EX(7, uArg7, a_Type); \
    CHECK_ARG_EX(8, uArg8, a_Type); \
    CHECK_ARG_EX(9, uArg9, a_Type); \
    CHECK_ARG_EX(10, uArg10, a_Type); \
    CHECK_ARG_EX(11, uArg11, a_Type); \
    CHECK_ARG_EX(12, uArg12, a_Type); \
    CHECK_ARG_EX(13, uArg13, a_Type); \
    CHECK_ARG_EX(14, uArg14, a_Type); \
    return VINF_SUCCESS; \
} */

#if ARCH_BITS == 64
# define RAND_ARG_UINTPTR(a_Type, a_iArg)   (g_auArgs[a_iArg] = RTRandU64())
#elif ARCH_BITS == 32
# define RAND_ARG_UINTPTR(a_Type, a_iArg)   (g_auArgs[a_iArg] = RTRandU32())
#else
# error "Unsupported ARCH_BITS value!"
#endif
#define RAND_ARG_UINT32(a_Type, a_iArg)     (uint32_t)(g_auArgs[a_iArg] =           RTRandU32())
#define RAND_ARG_UINT16(a_Type, a_iArg)     (uint16_t)(g_auArgs[a_iArg] = (uint16_t)RTRandU32())
#define RAND_ARG_UINT8(a_Type, a_iArg)      (uint8_t)( g_auArgs[a_iArg] =  (uint8_t)RTRandU32())
#if 1
# define RAND_ARG_BOOL(a_Type, a_iArg)      (bool)(    g_auArgs[a_iArg] =    (bool)(RTRandU32() & 1))
#else
# define RAND_ARG_BOOL(a_Type, a_iArg)      ((bool)(g_auArgs[a_iArg] = a_iArg % 2 ? true : false), a_iArg % 2 ? true : false)
#endif


CALLBACKS_WITH_TYPE(uintptr_t, CallbackUintPtr)
CALLBACKS_WITH_TYPE(uint32_t,  CallbackUint32)
CALLBACKS_WITH_TYPE(uint16_t,  CallbackUint16)
CALLBACKS_WITH_TYPE(uint8_t,   CallbackUint8)
CALLBACKS_WITH_TYPE(bool,      CallbackBool)


static void doCallWaitTests(PUVM pUVM)
{
    RTTestSub(g_hTest, "VMR3ReqCallWaitU");

    uint32_t cCalled = g_cCalled = 0;

    /*
     * Basic check w/o params.
     */
    int rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)CallbackNoArgs, 0);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    cCalled += 1;
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled);

    /*
     * Macro for testing passing 1 thru 15 arguments of a single type.
     */
#define TEST_CALLBACKS_WITH_TYPES(a_Type, a_CallbackBaseName, a_fnRandArg) \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_1Args, 1, \
                          a_fnRandArg(a_Type, 0)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_2Args, 2, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_3Args, 3, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_4Args, 4, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_5Args, 5, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_6Args, 6, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_7Args, 7, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_8Args, 8, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_9Args, 9, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
/* The max is now 9. See @bugref{10725}.
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_10Args, 10, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_11Args, 11, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9), a_fnRandArg(a_Type, 10)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_12Args, 12, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9), a_fnRandArg(a_Type, 10), a_fnRandArg(a_Type, 11)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_13Args, 13, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9), a_fnRandArg(a_Type, 10), a_fnRandArg(a_Type, 11), \
                          a_fnRandArg(a_Type, 12)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_14Args, 14, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9), a_fnRandArg(a_Type, 10), a_fnRandArg(a_Type, 11), \
                          a_fnRandArg(a_Type, 12), a_fnRandArg(a_Type, 13)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled); \
    \
    rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)a_CallbackBaseName##_15Args, 15, \
                          a_fnRandArg(a_Type, 0), a_fnRandArg(a_Type, 1), a_fnRandArg(a_Type, 2), a_fnRandArg(a_Type, 3), \
                          a_fnRandArg(a_Type, 4), a_fnRandArg(a_Type, 5), a_fnRandArg(a_Type, 6), a_fnRandArg(a_Type, 7), \
                          a_fnRandArg(a_Type, 8), a_fnRandArg(a_Type, 9), a_fnRandArg(a_Type, 10), a_fnRandArg(a_Type, 11), \
                          a_fnRandArg(a_Type, 12), a_fnRandArg(a_Type, 13), a_fnRandArg(a_Type, 14)); \
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS); \
    cCalled += 1; \
    RTTEST_CHECK(g_hTest, g_cCalled == cCalled) */


    /*
     * Test passing various types.
     */
    TEST_CALLBACKS_WITH_TYPES(uintptr_t, CallbackUintPtr, RAND_ARG_UINTPTR);
    TEST_CALLBACKS_WITH_TYPES(uint32_t,  CallbackUint32,  RAND_ARG_UINT32);
    TEST_CALLBACKS_WITH_TYPES(uint16_t,  CallbackUint16,  RAND_ARG_UINT16);
    TEST_CALLBACKS_WITH_TYPES(uint8_t,   CallbackUint8,   RAND_ARG_UINT8);
    TEST_CALLBACKS_WITH_TYPES(bool,      CallbackBool,    RAND_ARG_BOOL);
}



/*********************************************************************************************************************************
*   VM construction stuff.                                                                                                       *
*********************************************************************************************************************************/


static DECLCALLBACK(int)
tstVMREQConfigConstructor(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    RT_NOREF(pUVM, pVMM, pvUser);
    return CFGMR3ConstructDefaultTree(pVM);
}

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);
    RTTestCreate(TESTCASE, &g_hTest);
    RTTestSub(g_hTest, "Setup...");

    /*
     * Create empty VM.
     */
    PUVM pUVM;
    int rc = VMR3Create(1 /*cCpus*/, NULL, 0 /*fFlags*/, NULL, NULL, tstVMREQConfigConstructor, NULL, NULL, &pUVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do testing.
         */
        RTTestSub(g_hTest, "Alloc+Free Benchmark");
        uint64_t const u64StartTS = RTTimeNanoTS();
        RTTHREAD       Thread0;
        rc = RTThreadCreate(&Thread0, Thread, pUVM, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "REQ0");
        if (RT_SUCCESS(rc))
        {
            RTTHREAD Thread1;
            rc = RTThreadCreate(&Thread1, Thread, pUVM, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "REQ1");
            if (RT_SUCCESS(rc))
            {
                int rcThread1;
                rc = RTThreadWait(Thread1, RT_INDEFINITE_WAIT, &rcThread1);
                if (RT_FAILURE(rc))
                    RTTestFailed(g_hTest, "RTThreadWait(Thread1,,) failed, rc=%Rrc\n", rc);
                if (RT_FAILURE(rcThread1))
                    RTTestFailed(g_hTest, "rcThread1=%Rrc", rcThread1);
            }
            else
                RTTestFailed(g_hTest, "RTThreadCreate(&Thread1,,,,) failed, rc=%Rrc\n", rc);

            int rcThread0;
            rc = RTThreadWait(Thread0, RT_INDEFINITE_WAIT, &rcThread0);
            if (RT_FAILURE(rc))
                RTTestFailed(g_hTest, "RTThreadWait(Thread1,,) failed, rc=%Rrc\n", rc);
            if (RT_FAILURE(rcThread0))
                RTTestFailed(g_hTest, "rcThread0=%Rrc", rcThread0);
        }
        else
            RTTestFailed(g_hTest, "RTThreadCreate(&Thread0,,,,) failed, rc=%Rrc\n", rc);
        uint64_t const u64ElapsedTS = RTTimeNanoTS() - u64StartTS;
        RTTestValue(g_hTest, "runtime", u64ElapsedTS, RTTESTUNIT_NS);

        /*
         * Print stats.
         */
        STAMR3Print(pUVM, "/VM/Req/*");

        /*
         * Testing va_list fun.
         */
        RTTestSub(g_hTest, "va_list argument test");
        PassVA(pUVM, "hello %s", "world");
        VMR3AtRuntimeErrorRegister(pUVM, MyAtRuntimeError, (void *)"user argument");
        VMSetRuntimeError(VMR3GetVM(pUVM), 0 /*fFlags*/, "enum", "some %s string", "error");

        /*
         * Do various VMR3ReqCallWait tests.
         */
        doCallWaitTests(pUVM);

        /*
         * Cleanup.
         */
        rc = VMR3PowerOff(pUVM);
        if (!RT_SUCCESS(rc))
            RTTestFailed(g_hTest, "failed to power off vm! rc=%Rrc\n", rc);
        rc = VMR3Destroy(pUVM);
        if (!RT_SUCCESS(rc))
            RTTestFailed(g_hTest, "failed to destroy vm! rc=%Rrc\n", rc);
        VMR3ReleaseUVM(pUVM);
    }
    else if (rc == VERR_SVM_NO_SVM || rc == VERR_VMX_NO_VMX)
        return RTTestSkipAndDestroy(g_hTest, "%Rrc", rc);
    else
        RTTestFailed(g_hTest, "fatal error: failed to create vm! rc=%Rrc\n", rc);

    /*
     * Summary and return.
     */
    return RTTestSummaryAndDestroy(g_hTest);
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

