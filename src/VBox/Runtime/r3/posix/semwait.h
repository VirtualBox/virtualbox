/* $Id: semwait.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Common semaphore wait code.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r3_posix_semwait_h
#define IPRT_INCLUDED_SRC_r3_posix_semwait_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @def IPRT_HAVE_PTHREAD_CONDATTR_SETCLOCK
 * Set if the platform implements pthread_condattr_setclock().
 * Enables the use of the monotonic clock for waiting on condition variables. */
#ifndef IPRT_HAVE_PTHREAD_CONDATTR_SETCLOCK
/* Linux detection */
# if defined(RT_OS_LINUX) && defined(__USE_XOPEN2K)
#  include <features.h>
#  if __GLIBC_PREREQ(2,6) /** @todo figure the exact version where this was added */
#   define IPRT_HAVE_PTHREAD_CONDATTR_SETCLOCK
#  endif
# endif
/** @todo check other platforms */
#endif


/**
 * Converts a extended wait timeout specification to an absolute timespec and a
 * relative nanosecond count.
 *
 * @note    This does not check for RTSEMWAIT_FLAGS_INDEFINITE, caller should've
 *          done that already.
 *
 * @returns The relative wait in nanoseconds.  0 for a poll call, UINT64_MAX for
 *          an effectively indefinite wait.
 * @param   fFlags          RTSEMWAIT_FLAGS_XXX.
 * @param   fMonotonicClock Whether the timeout is in monotonic (true) or real
 *                          (false) time.
 * @param   uTimeout        The timeout.
 * @param   pAbsDeadline    Where to return the absolute deadline.
 */
DECLINLINE(uint64_t) rtSemPosixCalcDeadline(uint32_t fFlags, uint64_t uTimeout, bool fMonotonicClock,
                                            struct timespec *pAbsDeadline)
{
    Assert(!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE));

    /*
     * Convert uTimeout to a relative value in nanoseconds.
     */
    if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
    {
        if (uTimeout < UINT64_MAX / RT_NS_1MS)
            uTimeout = uTimeout * RT_NS_1MS;
        else
            return UINT64_MAX;
    }
    else if (uTimeout == UINT64_MAX) /* unofficial way of indicating an indefinite wait */
        return UINT64_MAX;

    /*
     * Make uTimeout relative and check for polling (zero timeout) calls.
     */
    uint64_t uAbsTimeout = uTimeout;
    if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
    {
        uint64_t const u64Now = RTTimeSystemNanoTS();
        if (uTimeout > u64Now)
            uTimeout -= u64Now;
        else
            return 0;
    }
    else if (uTimeout == 0)
        return 0;

    /*
     * Calculate the deadline according to the clock we're using.
     */
    if (!fMonotonicClock)
    {
#if defined(RT_OS_DARWIN) || defined(RT_OS_HAIKU)
        struct timeval  tv = {0,0};
        gettimeofday(&tv, NULL);
        pAbsDeadline->tv_sec  = tv.tv_sec;
        pAbsDeadline->tv_nsec = tv.tv_usec * 1000;
#else
        clock_gettime(CLOCK_REALTIME, pAbsDeadline);
#endif
        struct timespec TsAdd;
        TsAdd.tv_nsec = uTimeout % RT_NS_1SEC;
        TsAdd.tv_sec  = uTimeout / RT_NS_1SEC;

        /* Check for 32-bit tv_sec overflows: */
        if (   sizeof(pAbsDeadline->tv_sec) < sizeof(uint64_t)
            && (   uTimeout >= (uint64_t)RT_NS_1SEC * UINT32_MAX
                || (uint64_t)pAbsDeadline->tv_sec + pAbsDeadline->tv_sec >= UINT32_MAX) )
            return UINT64_MAX;

        pAbsDeadline->tv_sec  += TsAdd.tv_sec;
        pAbsDeadline->tv_nsec += TsAdd.tv_nsec;
        if ((uint32_t)pAbsDeadline->tv_nsec >= RT_NS_1SEC)
        {
            pAbsDeadline->tv_nsec -= RT_NS_1SEC;
            pAbsDeadline->tv_sec++;
        }
    }
    else
    {
        /* ASSUMES RTTimeSystemNanoTS() == RTTimeNanoTS() == clock_gettime(CLOCK_MONOTONIC). */
        if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
        {
            uint64_t const nsNow = RTTimeSystemNanoTS();
            uAbsTimeout += nsNow;
            if (uAbsTimeout < nsNow)
                return UINT64_MAX;
        }

        /* Check for 32-bit tv_sec overflows: */
        if (   sizeof(pAbsDeadline->tv_sec) < sizeof(uint64_t)
            && uAbsTimeout >= (uint64_t)RT_NS_1SEC * UINT32_MAX)
            return UINT64_MAX;

        pAbsDeadline->tv_nsec = uAbsTimeout % RT_NS_1SEC;
        pAbsDeadline->tv_sec  = uAbsTimeout / RT_NS_1SEC;
    }

    return uTimeout;
}

#endif /* !IPRT_INCLUDED_SRC_r3_posix_semwait_h */

