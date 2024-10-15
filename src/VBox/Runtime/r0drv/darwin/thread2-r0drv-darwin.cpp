/* $Id: thread2-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads (Part 2), Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/thread.h"


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    /* No TLS in Ring-0. :-/ */
    return VINF_SUCCESS;
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    return rtThreadGetByNative((RTNATIVETHREAD)current_thread());
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    /*
     * Convert the priority type to scheduling policies.
     * (This is really just guess work.)
     */
    bool                            fSetExtended = false;
    thread_extended_policy          Extended = { true };
    bool                            fSetTimeContstraint = false;
    thread_time_constraint_policy   TimeConstraint = { 0, 0, 0, true };
    thread_precedence_policy        Precedence = { 0 };
    switch (enmType)
    {
        case RTTHREADTYPE_INFREQUENT_POLLER:
            Precedence.importance = 1;
            break;

        case RTTHREADTYPE_EMULATION:
            Precedence.importance = 30;
            break;

        case RTTHREADTYPE_DEFAULT:
            Precedence.importance = 31;
            break;

        case RTTHREADTYPE_MSG_PUMP:
            Precedence.importance = 34;
            break;

        case RTTHREADTYPE_IO:
            Precedence.importance = 98;
            break;

        case RTTHREADTYPE_TIMER:
            Precedence.importance = 0x7fffffff;

            fSetExtended = true;
            Extended.timeshare = FALSE;

            fSetTimeContstraint = true;
            TimeConstraint.period = 0; /* not really true for a real timer thread, but we've really no idea. */
            TimeConstraint.computation = rtDarwinAbsTimeFromNano(100000); /* 100 us*/
            TimeConstraint.constraint = rtDarwinAbsTimeFromNano(500000);  /* 500 us */
            TimeConstraint.preemptible = FALSE;
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }
    RT_ASSERT_INTS_ON();

    /*
     * Do the actual modification.
     */
    kern_return_t kr = thread_policy_set((thread_t)pThread->Core.Key, THREAD_PRECEDENCE_POLICY,
                                         (thread_policy_t)&Precedence, THREAD_PRECEDENCE_POLICY_COUNT);
    AssertMsg(kr == KERN_SUCCESS, ("%rc\n", kr)); NOREF(kr);

    if (fSetExtended)
    {
        kr = thread_policy_set((thread_t)pThread->Core.Key, THREAD_EXTENDED_POLICY,
                               (thread_policy_t)&Extended, THREAD_EXTENDED_POLICY_COUNT);
        AssertMsg(kr == KERN_SUCCESS, ("%rc\n", kr));
    }

    if (fSetTimeContstraint)
    {
        kr = thread_policy_set((thread_t)pThread->Core.Key, THREAD_TIME_CONSTRAINT_POLICY,
                               (thread_policy_t)&TimeConstraint, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        AssertMsg(kr == KERN_SUCCESS, ("%rc\n", kr));
    }

    return VINF_SUCCESS; /* ignore any errors for now */
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    RT_NOREF(pThread);
    return VERR_NOT_IMPLEMENTED;
}


DECLHIDDEN(void) rtThreadNativeWaitKludge(PRTTHREADINT pThread)
{
    RT_NOREF(pThread);
    /** @todo fix RTThreadWait/RTR0Term race on darwin. */
    RTThreadSleep(1);
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    RT_NOREF(pThread);
}


/**
 * Native kernel thread wrapper function.
 *
 * This will forward to rtThreadMain and do termination upon return.
 *
 * @param pvArg         Pointer to the argument package.
 * @param Ignored       Wait result, which we ignore.
 */
static void rtThreadNativeMain(void *pvArg, wait_result_t Ignored)
{
    RT_NOREF(Ignored);
    const thread_t Self = current_thread();
    PRTTHREADINT pThread = (PRTTHREADINT)pvArg;

    rtThreadMain(pThread, (RTNATIVETHREAD)Self, &pThread->szName[0]);

    kern_return_t kr = thread_terminate(Self);
    AssertFatalMsgFailed(("kr=%d\n", kr));
}


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread)
{
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    thread_t NativeThread;
    kern_return_t kr = kernel_thread_start(rtThreadNativeMain, pThreadInt, &NativeThread);
    if (kr == KERN_SUCCESS)
    {
        *pNativeThread = (RTNATIVETHREAD)NativeThread;
        thread_deallocate(NativeThread);
        IPRT_DARWIN_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
    return RTErrConvertFromMachKernReturn(kr);
}

