/* $Id: time-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time, Ring-0 Driver, Nt.
 */

/*
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
#define LOG_GROUP RTLOGGROUP_TIME
#include "the-nt-kernel.h"
#include "internal-r0drv-nt.h"
#include <iprt/time.h>


/*
 * The KeQueryTickCount macro isn't compatible with NT 3.1, use the
 * exported KPI instead.
 */
#ifdef RT_ARCH_X86
# undef KeQueryTickCount
extern "C" NTKERNELAPI void NTAPI KeQueryTickCount(PLARGE_INTEGER);
#endif


DECLINLINE(uint64_t) rtTimeGetSystemNanoTS(void)
{
    /*
     * Note! The time source we use here must be exactly the same as in
     *       the ring-3 code!
     *
     * Using interrupt time is the simplest and requires the least calculation.
     * It is also accounting for suspended time. Unfortuantely, there is no
     * ring-3 for reading it... but that won't stop us.
     *
     * Using the tick count is problematic in ring-3 on older windows version
     * as we can only get the 32-bit tick value, i.e. we'll roll over sooner or
     * later.
     */
#if 1
    /* Interrupt time. */
    LARGE_INTEGER InterruptTime;
    if (g_pfnrtKeQueryInterruptTimePrecise)
    {
        ULONG64 QpcTsIgnored;
        InterruptTime.QuadPart = g_pfnrtKeQueryInterruptTimePrecise(&QpcTsIgnored);
    }
# ifdef RT_ARCH_X86
    else if (g_pfnrtKeQueryInterruptTime) /* W2K+ */
        InterruptTime.QuadPart = g_pfnrtKeQueryInterruptTime();
    else if (g_uRtNtVersion >= RTNT_MAKE_VERSION(3, 50))
    {
        /* NT 3.50 and later, also pre-init: Use the user shared data. */
        do
        {
            InterruptTime.HighPart = ((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.High1Time;
            InterruptTime.LowPart  = ((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.LowPart;
        } while (((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.High2Time != InterruptTime.HighPart);
    }
    else
    {
        /*
         * There is no KUSER_SHARED_DATA structure on NT 3.1, so we have no choice
         * but to use the tick count.  We must also avoid the KeQueryTickCount macro
         * in the WDK, since NT 3.1 doesn't have the KeTickCount data export either (see above).
         */
        static ULONG volatile s_uTimeIncrement = 0;
        ULONG uTimeIncrement = s_uTimeIncrement;
        if (!uTimeIncrement)
        {
            uTimeIncrement = KeQueryTimeIncrement();
            Assert(uTimeIncrement != 0);
            Assert(uTimeIncrement * 100 / 100 == uTimeIncrement);
            uTimeIncrement *= 100;
            s_uTimeIncrement = uTimeIncrement;
        }

        KeQueryTickCount(&InterruptTime);
        return (uint64_t)InterruptTime.QuadPart * uTimeIncrement;
    }
# else
    else
        InterruptTime.QuadPart = KeQueryInterruptTime(); /* Macro on AMD64. */
# endif
    return (uint64_t)InterruptTime.QuadPart * 100;
#else
    /* Tick count.  Works all the way back to NT 3.1 with #undef above.  */
    LARGE_INTEGER Tick;
    KeQueryTickCount(&Tick);
    return (uint64_t)Tick.QuadPart * KeQueryTimeIncrement() * 100;
#endif
}


RTDECL(uint64_t) RTTimeNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}


RTDECL(uint64_t) RTTimeMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
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
    LARGE_INTEGER SystemTime;
    if (g_pfnrtKeQuerySystemTimePrecise)
        g_pfnrtKeQuerySystemTimePrecise(&SystemTime);
    else
        KeQuerySystemTime(&SystemTime); /* Macro on AMD64, export on X86.  */
    return RTTimeSpecSetNtTime(pTime, SystemTime.QuadPart);
}

