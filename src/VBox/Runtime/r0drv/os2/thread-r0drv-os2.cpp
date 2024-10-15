/* $Id: thread-r0drv-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads (Part 1), Ring-0 Driver, OS/2.
 */

/*
 * Contributed by knut st. osmundsen.
 *
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-os2-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Per-cpu preemption counters. */
static int32_t volatile g_acPreemptDisabled[256];



RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    PLINFOSEG pLIS = (PLINFOSEG)RTR0Os2Virt2Flat(g_fpLIS);
    AssertMsgReturn(pLIS, ("g_fpLIS=%04x:%04x - logging too early again?\n", g_fpLIS.sel, g_fpLIS.off), NIL_RTNATIVETHREAD);
    return pLIS->tidCurrent | (pLIS->pidCurrent << 16);
}


static int rtR0ThreadOs2SleepCommon(RTMSINTERVAL cMillies)
{
    int rc = KernBlock((ULONG)RTThreadSleep,
                       cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies,
                       0, NULL, NULL);
    switch (rc)
    {
        case NO_ERROR:
            return VINF_SUCCESS;
        case ERROR_TIMEOUT:
            return VERR_TIMEOUT;
        case ERROR_INTERRUPT:
            return VERR_INTERRUPTED;
        default:
            AssertMsgFailed(("%d\n", rc));
            return VERR_NO_TRANSLATION;
    }
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadOs2SleepCommon(cMillies);
}


RTDECL(int) RTThreadSleepNoBlock(RTMSINTERVAL cMillies)
{
    return rtR0ThreadOs2SleepCommon(cMillies);
}


RTDECL(bool) RTThreadYield(void)
{
    /** @todo implement me (requires a devhelp) */
    return false;
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);
    int32_t c = g_acPreemptDisabled[ASMGetApicId()];
    AssertMsg(c >= 0 && c < 32, ("%d\n", c));
    return c == 0
        && ASMIntAreEnabled();
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);

    union
    {
        RTFAR16 fp;
        uint8_t fResched;
    } u;
    int rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_YIELDFLAG, 0, &u.fp);
    AssertReturn(rc == 0, false);
    if (u.fResched)
        return true;

    /** @todo Check if DHGETDOSV_YIELDFLAG includes TCYIELDFLAG. */
    rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_TCYIELDFLAG, 0, &u.fp);
    AssertReturn(rc == 0, false);
    if (u.fResched)
        return true;
    return false;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* no kernel preemption on OS/2. */
    return false;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);

    /* No preemption on OS/2, so do our own accounting. */
    int32_t c = ASMAtomicIncS32(&g_acPreemptDisabled[ASMGetApicId()]);
    AssertMsg(c > 0 && c < 32, ("%d\n", c));
    pState->u32Reserved = c;
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    AssertMsg(pState->u32Reserved > 0 && pState->u32Reserved < 32, ("%d\n", pState->u32Reserved));
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);

    /* No preemption on OS/2, so do our own accounting. */
    int32_t volatile *pc = &g_acPreemptDisabled[ASMGetApicId()];
    AssertMsg(pState->u32Reserved == (uint32_t)*pc, ("uchDummy=%d *pc=%d \n", pState->u32Reserved, *pc));
    ASMAtomicUoWriteS32(pc, pState->u32Reserved - 1);
    pState->u32Reserved = 0;
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);

    union
    {
        RTFAR16 fp;
        uint8_t cInterruptLevel;
    } u;
    /** @todo OS/2: verify the usage of DHGETDOSV_INTERRUPTLEV. */
    int rc = RTR0Os2DHQueryDOSVar(DHGETDOSV_INTERRUPTLEV, 0, &u.fp);
    AssertReturn(rc == 0, true);

    return u.cInterruptLevel > 0;
}

