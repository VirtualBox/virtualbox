/* $Id: thread2-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads part 2, POSIX.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/errcore.h>
#include "internal/thread.h"


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)pthread_self();
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    LogFlow(("RTThreadSleep: cMillies=%d\n", cMillies));
    if (!cMillies)
    {
        if (!sched_yield())
        {
            LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", VINF_SUCCESS, cMillies));
            return VINF_SUCCESS;
        }
    }
    else
    {
        struct timespec ts;
        struct timespec tsrem = {0,0};

        ts.tv_nsec = (cMillies % 1000) * 1000000;
        ts.tv_sec  = cMillies / 1000;
        if (!nanosleep(&ts, &tsrem))
        {
            LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", VINF_SUCCESS, cMillies));
            return VINF_SUCCESS;
        }
    }

    int rc = RTErrConvertFromErrno(errno);
    LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", rc, cMillies));
    return rc;
}


RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    if (!cMillies)
    {
        if (!sched_yield())
            return VINF_SUCCESS;
    }
    else
    {
        struct timespec ts;
        struct timespec tsrem = {0,0};

        ts.tv_nsec = (cMillies % 1000) * 1000000;
        ts.tv_sec  = cMillies / 1000;
        if (!nanosleep(&ts, &tsrem))
            return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(errno);
}


RTDECL(bool) RTThreadYield(void)
{
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint64_t u64TS = ASMReadTSC();
#endif

    sched_yield();

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    u64TS = ASMReadTSC() - u64TS;
    bool fRc = u64TS > 1500;
    LogFlow(("RTThreadYield: returning %d (%llu ticks)\n", fRc, u64TS));
#else
    bool fRc = true; /* PORTME: Add heuristics for determining whether the cpus was yielded. */
#endif
    return fRc;
}

