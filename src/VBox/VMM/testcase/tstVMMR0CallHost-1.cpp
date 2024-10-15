/* $Id: tstVMMR0CallHost-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for the VMMR0JMPBUF operations.
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
#include <iprt/errcore.h>
#include <VBox/param.h>
#include <iprt/alloca.h>
#include <iprt/initterm.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/test.h>

#define IN_VMM_R0
#define IN_RING0 /* pretent we're in Ring-0 to get the prototypes. */
#include <VBox/vmm/vmm.h>
#include "VMMInternal.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The jump buffer. */
static VMMR0JMPBUF          g_Jmp;
/** The mirror jump buffer. */
static VMMR0JMPBUF          g_JmpMirror;
/** The number of jumps we've done. */
static unsigned volatile    g_cJmps;
/** Number of bytes allocated last time we called foo(). */
static size_t volatile      g_cbFoo;
/** Number of bytes used last time we called foo(). */
static intptr_t volatile    g_cbFooUsed;
/** Set if we're in a long jump. */
static bool                 g_fInLongJmp;


static int foo(int i, int iZero, int iMinusOne)
{
    NOREF(iZero);

    /* allocate a buffer which we fill up to the end. */
    size_t cb = (i % 1555) + 32;
    g_cbFoo = cb;
    char  *pv = (char *)alloca(cb);
    RTStrPrintf(pv, cb, "i=%d%*s\n", i, cb, "");
#if defined(RT_ARCH_AMD64)
    g_cbFooUsed = (uintptr_t)g_Jmp.rsp - (uintptr_t)pv;
    RTTESTI_CHECK_MSG_RET(g_cbFooUsed < VMM_STACK_SIZE - 128, ("%p - %p -> %#x; cb=%#x i=%d\n", g_Jmp.rsp, pv, g_cbFooUsed, cb, i), -15);
#elif defined(RT_ARCH_X86)
    g_cbFooUsed = (uintptr_t)g_Jmp.esp - (uintptr_t)pv;
    RTTESTI_CHECK_MSG_RET(g_cbFooUsed < (intptr_t)VMM_STACK_SIZE - 128, ("%p - %p -> %#x; cb=%#x i=%d\n", g_Jmp.esp, pv, g_cbFooUsed, cb, i), -15);
#endif

    /* Twice in a row, every 7th time. */
    if ((i % 7) <= 1)
    {
        g_cJmps++;
        g_fInLongJmp = true;
        int rc = vmmR0CallRing3LongJmp(&g_Jmp, 42);
        g_fInLongJmp = false;
        if (!rc)
            return i + 10000;
        return -1;
    }
    NOREF(iMinusOne);
    return i;
}


static DECLCALLBACK(int) tst2(intptr_t i, intptr_t i2)
{
    RTTESTI_CHECK_MSG_RET(i >= 0 && i <= 8192, ("i=%d is out of range [0..8192]\n", i),      1);
    RTTESTI_CHECK_MSG_RET(i2 == 0,             ("i2=%d is out of range [0]\n", i2),          1);
    int iExpect = (i % 7) <= 1 ? i + 10000 : i;
    int rc = foo(i, 0, -1);
    RTTESTI_CHECK_MSG_RET(rc == iExpect,       ("i=%d rc=%d expected=%d\n", i, rc, iExpect), 1);
    return 0;
}


static DECLCALLBACK(DECL_NO_INLINE(RT_NOTHING, int)) stackRandom(PVMMR0JMPBUF pJmpBuf, PFNVMMR0SETJMP pfn, PVM pVM, PVMCPU pVCpu)
{
#ifdef RT_ARCH_AMD64
    uint32_t            cbRand  = RTRandU32Ex(1, 96);
#else
    uint32_t            cbRand  = 1;
#endif
    uint8_t volatile   *pabFuzz = (uint8_t volatile *)alloca(cbRand);
    memset((void *)pabFuzz, 0xfa, cbRand);
    int rc = vmmR0CallRing3SetJmp(pJmpBuf, pfn, pVM, pVCpu);
    memset((void *)pabFuzz, 0xaf, cbRand);
    return rc;
}


static void tst(int iFrom, int iTo, int iInc)
{
    RT_BZERO(&g_Jmp, RT_UOFFSETOF(VMMR0JMPBUF, cbStackBuf));
    g_Jmp.cbStackValid = _1M;
    memset((void *)g_Jmp.pvStackBuf, '\0', g_Jmp.cbStackBuf);
    g_cbFoo = 0;
    g_cJmps = 0;
    g_cbFooUsed = 0;
    g_fInLongJmp = false;

    for (int i = iFrom, iItr = 0; i != iTo; i += iInc, iItr++)
    {
        g_fInLongJmp = false;
        int rc = stackRandom(&g_Jmp, (PFNVMMR0SETJMP)(uintptr_t)tst2, (PVM)(uintptr_t)i, 0);
        RTTESTI_CHECK_MSG_RETV(rc == (g_fInLongJmp ? 42 : 0),
                               ("i=%d rc=%d setjmp; cbFoo=%#x cbFooUsed=%#x fInLongJmp=%d\n",
                                i, rc, g_cbFoo, g_cbFooUsed, g_fInLongJmp));

    }
    RTTESTI_CHECK_MSG_RETV(g_cJmps, ("No jumps!"));
}


int main()
{
    /*
     * Init.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVMMR0CallHost-1", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    g_Jmp.cbStackBuf = HOST_PAGE_SIZE;
    g_Jmp.pvStackBuf = (uintptr_t)RTTestGuardedAllocTail(hTest, g_Jmp.cbStackBuf);
    g_Jmp.pMirrorBuf = (uintptr_t)&g_JmpMirror;

    /*
     * Run two test with about 1000 long jumps each.
     */
    RTTestSub(hTest, "Increasing stack usage");
    tst(0, 7000, 1);
    RTTestSub(hTest, "Decreasing stack usage");
    tst(7599, 0, -1);

    return RTTestSummaryAndDestroy(hTest);
}
