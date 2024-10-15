/* $Id: thread-r0drv-netbsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads (Part 1), Ring-0 Driver, NetBSD.
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
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-netbsd-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>
#include "internal/thread.h"


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)curlwp;
}


static int rtR0ThreadNbsdSleepCommon(RTMSINTERVAL cMillies)
{
    int rc;
    int cTicks;

    /*
     * 0 ms sleep -> yield.
     */
    if (!cMillies)
    {
        RTThreadYield();
        return VINF_SUCCESS;
    }

    /*
     * Translate milliseconds into ticks and go to sleep.
     */
    if (cMillies != RT_INDEFINITE_WAIT)
    {
        if (hz == 1000)
            cTicks = cMillies;
        else if (hz == 100)
            cTicks = cMillies / 10;
        else
        {
            int64_t cTicks64 = ((uint64_t)cMillies * hz) / 1000;
            cTicks = (int)cTicks64;
            if (cTicks != cTicks64)
                cTicks = INT_MAX;
        }
    }
    else
        cTicks = 0;     /* requires giant lock! */

    rc = tsleep((void *)RTThreadSleep,
                PZERO | PCATCH,
                "iprtsl",           /* max 6 chars */
                cTicks);
    switch (rc)
    {
        case 0:
            return VINF_SUCCESS;
        case EWOULDBLOCK:
            return VERR_TIMEOUT;
        case EINTR:
        case ERESTART:
            return VERR_INTERRUPTED;
        default:
            AssertMsgFailed(("%d\n", rc));
            return VERR_NO_TRANSLATION;
    }
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadNbsdSleepCommon(cMillies);
}


RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    return rtR0ThreadNbsdSleepCommon(cMillies);
}


RTDECL(bool) RTThreadYield(void)
{
    yield();
    return true;
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);

    return curlwp->l_dopreempt == 0
        && ASMIntAreEnabled(); /** @todo is there a native netbsd function/macro for this? */
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);

    return curlwp->l_dopreempt;
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /* yes, kernel preemption is possible. */
    return true;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);

    curlwp->l_nopreempt++;
    __insn_barrier();
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    __insn_barrier();
    if (--curlwp->l_nopreempt != 0)
        return;
    __insn_barrier();
    if (__predict_false(curlwp->l_dopreempt))
        kpreempt(0);
    __insn_barrier();
}


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);
    /** @todo NetBSD: Implement RTThreadIsInInterrupt. Required for guest
     *        additions! */
    return !ASMIntAreEnabled();
}
