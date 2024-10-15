/* $Id: time-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time, Darwin.
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
#define LOG_GROUP RTLOGGROUP_TIME
#define RTTIME_INCL_TIMEVAL
#include <mach/mach_time.h>
#include <mach/kern_return.h>
#include <sys/time.h>
#include <time.h>

#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/asm-math.h>
#include "internal/time.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct mach_timebase_info    g_Info = { 0, 0 };
static bool                         g_fFailedToGetTimeBaseInfo = false;


/**
 * Perform lazy init (pray we're not racing anyone in a bad way).
 */
static void rtTimeDarwinLazyInit(void)
{
    struct mach_timebase_info Info;
    if (mach_timebase_info(&Info) == KERN_SUCCESS)
        g_Info = Info;
    else
    {
        g_fFailedToGetTimeBaseInfo = true;
        Assert(g_Info.denom == 0 && g_Info.numer == 0);
    }
}


/**
 * Internal worker.
 * @returns Nanosecond timestamp.
 */
DECLINLINE(uint64_t) rtTimeGetSystemNanoTS(void)
{
    /* Lazy init. */
    if (RT_UNLIKELY(g_Info.denom == 0 && !g_fFailedToGetTimeBaseInfo))
        rtTimeDarwinLazyInit();

    /* special case: absolute time is in nanoseconds. */
    if (g_Info.denom == 1 && g_Info.numer == 1)
        return mach_absolute_time();

    /* general case: multiply by factor to get nanoseconds. */
    if (g_Info.denom > 0 && g_Info.numer > 0)
    {
        /*
         * We could use clock_gettime_nsec_np(CLOCK_UPTIME_RAW) but internally
         * it might be calling mach_timebase_info() for each call. This is most
         * likely faster since we avoid calling mach_timebase_info() here.
         */
        uint64_t const cTicks = mach_absolute_time();
        return ASMMultU64ByU32DivByU32(cTicks, g_Info.numer, g_Info.denom);
    }

    /* worst case: fallback to gettimeofday(). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec  * RT_NS_1SEC_64
         + (uint64_t)(tv.tv_usec * RT_NS_1US);
}


RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}


RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
}


RTDECL(PRTTIMESPEC) RTTimeNow(PRTTIMESPEC pTime)
{
    /** @todo find nanosecond API for getting time of day. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return RTTimeSpecSetTimeval(pTime, &tv);
}

