/* $Id $ */
/** @file
 * IPRT - Local Time, Posix.
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
#include <iprt/types.h>
#include <iprt/assert.h>

#include <sys/time.h>
#include <time.h>

#include <iprt/time.h>


/**
 * This tries to find the UTC offset for a given timespec.
 *
 * It does probably not take into account changes in daylight
 * saving over the years or similar stuff.
 *
 * @returns UTC offset in nanoseconds.
 * @param   pTime           The time.
 * @param   fCurrentTime    Whether the input is current time or not.
 *                          This is for avoid infinit recursion on errors in the fallback path.
 */
static int64_t rtTimeLocalUTCOffset(PCRTTIMESPEC pTime, bool fCurrentTime)
{
    RTTIMESPEC Fallback;

    /*
     * Convert to time_t.
     */
    int64_t i64UnixTime = RTTimeSpecGetSeconds(pTime);
    time_t UnixTime = i64UnixTime;
    if (UnixTime != i64UnixTime)
        return fCurrentTime ? 0 : rtTimeLocalUTCOffset(RTTimeNow(&Fallback), true);

    /*
     * Explode it as both local and UTC time.
     */
    struct tm TmLocal;
    if (    !localtime_r(&UnixTime, &TmLocal)
        ||  !TmLocal.tm_year)
        return fCurrentTime ? 0 : rtTimeLocalUTCOffset(RTTimeNow(&Fallback), true);
    struct tm TmUtc;
    if (!gmtime_r(&UnixTime, &TmUtc))
        return fCurrentTime ? 0 : rtTimeLocalUTCOffset(RTTimeNow(&Fallback), true);

    /*
     * Calc the difference (if any).
     * We ASSUME that the difference is less that 24 hours.
     */
    if (    TmLocal.tm_hour == TmUtc.tm_hour
        &&  TmLocal.tm_min  == TmUtc.tm_min
        &&  TmLocal.tm_sec  == TmUtc.tm_sec
        &&  TmLocal.tm_mday == TmUtc.tm_mday)
        return 0;

    int cLocalSecs = TmLocal.tm_hour * 3600
                   + TmLocal.tm_min * 60
                   + TmLocal.tm_sec;
    int cUtcSecs   = TmUtc.tm_hour * 3600
                   + TmUtc.tm_min * 60
                   + TmUtc.tm_sec;
    if (TmLocal.tm_mday != TmUtc.tm_mday)
    {
        /*
         * Must add 24 hours to the value that is ahead of the other.
         *
         * To determine which is ahead was busted for a long long time (bugref:9078),
         * so here are some examples and two different approaches.
         *
         *  TmLocal              TmUtc              => Add 24:00 to     => Diff
         *  2007-04-02 01:00     2007-04-01 23:00   => TmLocal          => +02:00
         *  2007-04-01 01:00     2007-03-31 23:00   => TmLocal          => +02:00
         *  2007-03-31 01:00     2007-03-30 23:00   => TmLocal          => +02:00
         *
         *  2007-04-01 01:00     2007-04-02 23:00   => TmUtc            => -02:00
         *  2007-03-31 23:00     2007-04-01 01:00   => TmUtc            => -02:00
         *  2007-03-30 23:00     2007-03-31 01:00   => TmUtc            => -02:00
         *
         */
#if 0
        /* Using day of month turned out to be a little complicated. */
        if (   (   TmLocal.tm_mday > TmUtc.tm_mday
                && (TmUtc.tm_mday != 1 || TmLocal.tm_mday < 28) )
            || (TmLocal.tm_mday == 1 && TmUtc.tm_mday >= 28) )
        {
            cLocalSecs += 24*60*60;
            Assert(   TmLocal.tm_yday - TmUtc.tm_yday == 1
                   || (TmLocal.tm_yday == 0 && TmUtc.tm_yday >= 364 && TmLocal.tm_year == TmUtc.tm_year + 1));
        }
        else
        {
            cUtcSecs   += 24*60*60;
            Assert(   TmUtc.tm_yday - TmLocal.tm_yday == 1
                   || (TmUtc.tm_yday == 0 && TmLocal.tm_yday >= 364 && TmUtc.tm_year == TmLocal.tm_year + 1));
        }
#else
        /* Using day of year and year is simpler. */
        if (   (   TmLocal.tm_year == TmUtc.tm_year
                && TmLocal.tm_yday > TmUtc.tm_yday)
            || TmLocal.tm_year > TmUtc.tm_year)
        {
            cLocalSecs += 24*60*60;
            Assert(   TmLocal.tm_yday - TmUtc.tm_yday == 1
                   || (TmLocal.tm_yday == 0 && TmUtc.tm_yday >= 364 && TmLocal.tm_year == TmUtc.tm_year + 1));
        }
        else
        {
            cUtcSecs   += 24*60*60;
            Assert(   TmUtc.tm_yday - TmLocal.tm_yday == 1
                   || (TmUtc.tm_yday == 0 && TmLocal.tm_yday >= 364 && TmUtc.tm_year == TmLocal.tm_year + 1));
        }
#endif
    }

    return (cLocalSecs - cUtcSecs) * INT64_C(1000000000);
}


/**
 * Gets the current delta between UTC and local time.
 *
 * @code
 *      RTTIMESPEC LocalTime;
 *      RTTimeSpecAddNano(RTTimeNow(&LocalTime), RTTimeLocalDeltaNano());
 * @endcode
 *
 * @returns Returns the nanosecond delta between UTC and local time.
 */
RTDECL(int64_t) RTTimeLocalDeltaNano(void)
{
    RTTIMESPEC Time;
    return rtTimeLocalUTCOffset(RTTimeNow(&Time), true /* current time, skip fallback */);
}


/**
 * Gets the delta between UTC and local time at the given time.
 *
 * @code
 *      RTTIMESPEC LocalTime;
 *      RTTimeNow(&LocalTime);
 *      RTTimeSpecAddNano(&LocalTime, RTTimeLocalDeltaNanoFor(&LocalTime));
 * @endcode
 *
 * @param   pTimeSpec   The time spec giving the time to get the delta for.
 * @returns Returns the nanosecond delta between UTC and local time.
 */
RTDECL(int64_t) RTTimeLocalDeltaNanoFor(PCRTTIMESPEC pTimeSpec)
{
    AssertPtr(pTimeSpec);
    return rtTimeLocalUTCOffset(pTimeSpec, false /* current time, skip fallback */);
}


/**
 * Explodes a time spec to the localized timezone.
 *
 * @returns pTime.
 * @param   pTime       Where to store the exploded time.
 * @param   pTimeSpec   The time spec to exploded. (UTC)
 */
RTDECL(PRTTIME) RTTimeLocalExplode(PRTTIME pTime, PCRTTIMESPEC pTimeSpec)
{
    RTTIMESPEC LocalTime = *pTimeSpec;
    int64_t cNsUtcOffset = rtTimeLocalUTCOffset(&LocalTime, true /* current time, skip fallback */);
    RTTimeSpecAddNano(&LocalTime, cNsUtcOffset);
    pTime = RTTimeExplode(pTime, &LocalTime);
    if (pTime)
    {
        pTime->fFlags = (pTime->fFlags & ~RTTIME_FLAGS_TYPE_MASK) | RTTIME_FLAGS_TYPE_LOCAL;
        pTime->offUTC = cNsUtcOffset / (int64_t)RT_NS_1MIN;
    }
    return pTime;
}

