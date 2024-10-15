/* $Id: time-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time, POSIX.
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
#include <sys/time.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef __NR_clock_gettime
# define __NR_timer_create      259
# define __NR_clock_gettime     (__NR_timer_create+6)
#endif

#include <iprt/time.h>
#include "internal/time.h"


DECLINLINE(int) sys_clock_gettime(clockid_t id,  struct timespec *ts)
{
    int rc = syscall(__NR_clock_gettime, id, ts);
    if (rc >= 0)
        return rc;
    return -1;
}


/**
 * Wrapper around various monotone time sources.
 */
DECLINLINE(int) mono_clock(struct timespec *ts)
{
    static int iWorking = -1;
    switch (iWorking)
    {
#ifdef CLOCK_MONOTONIC
        /*
         * Standard clock_gettime()
         */
        case 0:
            return clock_gettime(CLOCK_MONOTONIC, ts);

        /*
         * Syscall clock_gettime().
         */
        case 1:
            return sys_clock_gettime(CLOCK_MONOTONIC, ts);

#endif /* CLOCK_MONOTONIC */


        /*
         * Figure out what's working.
         */
        case -1:
        {
#ifdef CLOCK_MONOTONIC
            /*
             * Real-Time API.
             */
            int rc = clock_gettime(CLOCK_MONOTONIC, ts);
            if (!rc)
            {
                iWorking = 0;
                return 0;
            }

            rc = sys_clock_gettime(CLOCK_MONOTONIC, ts);
            if (!rc)
            {
                iWorking = 1;
                return 0;
            }
#endif /* CLOCK_MONOTONIC */

            /* give up */
            iWorking = -2;
            break;
        }
    }
    return -1;
}


DECLINLINE(uint64_t) rtTimeGetSystemNanoTS(void)
{
    /* check monotonic clock first. */
    static bool fMonoClock = true;
    if (fMonoClock)
    {
        struct timespec ts;
        if (!mono_clock(&ts))
            return (uint64_t)ts.tv_sec * RT_NS_1SEC_64
                 + ts.tv_nsec;
        fMonoClock = false;
    }

    /* fallback to gettimeofday(). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec  * RT_NS_1SEC_64
         + (uint64_t)(tv.tv_usec * RT_NS_1US);
}


/**
 * Gets the current nanosecond timestamp.
 *
 * This differs from RTTimeNanoTS in that it will use system APIs and not do any
 * resolution or performance optimizations.
 *
 * @returns nanosecond timestamp.
 */
RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}


/**
 * Gets the current millisecond timestamp.
 *
 * This differs from RTTimeNanoTS in that it will use system APIs and not do any
 * resolution or performance optimizations.
 *
 * @returns millisecond timestamp.
 */
RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
}

