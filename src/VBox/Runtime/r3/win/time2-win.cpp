/* $Id: time2-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time, Windows.
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
#include <iprt/win/windows.h>

#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/time.h"

#include "internal-r3-win.h"


RTDECL(int) RTTimeSet(PCRTTIMESPEC pTime)
{
    FILETIME    FileTime;
    SYSTEMTIME  SysTime;
    if (FileTimeToSystemTime(RTTimeSpecGetNtFileTime(pTime, &FileTime), &SysTime))
    {
        if (SetSystemTime(&SysTime))
            return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(GetLastError());
}


RTDECL(PRTTIME) RTTimeLocalExplode(PRTTIME pTime, PCRTTIMESPEC pTimeSpec)
{
    RTTIMESPEC LocalTime;
    if (g_pfnSystemTimeToTzSpecificLocalTime)
    {
        /*
         * FileTimeToLocalFileTime does not do the right thing, so we'll have
         * to convert to system time and SystemTimeToTzSpecificLocalTime instead.
         *
         * Note! FileTimeToSystemTime drops resoultion down to milliseconds, thus
         *       we have to do the offUTC calculation using milliseconds and adjust
         *       u32Nanosecons by sub milliseconds digits.
         */
        SYSTEMTIME SystemTimeIn;
        FILETIME FileTime;
        if (FileTimeToSystemTime(RTTimeSpecGetNtFileTime(pTimeSpec, &FileTime), &SystemTimeIn))
        {
            SYSTEMTIME SystemTimeOut;
            if (g_pfnSystemTimeToTzSpecificLocalTime(NULL /* use current TZI */, &SystemTimeIn, &SystemTimeOut))
            {
                if (SystemTimeToFileTime(&SystemTimeOut, &FileTime))
                {
                    RTTimeSpecSetNtFileTime(&LocalTime, &FileTime);
                    pTime = RTTimeExplode(pTime, &LocalTime);
                    if (pTime)
                    {
                        pTime->fFlags = (pTime->fFlags & ~RTTIME_FLAGS_TYPE_MASK) | RTTIME_FLAGS_TYPE_LOCAL;
                        pTime->offUTC = (RTTimeSpecGetMilli(&LocalTime) - RTTimeSpecGetMilli(pTimeSpec)) / RT_MS_1MIN;
                        pTime->u32Nanosecond += RTTimeSpecGetNano(pTimeSpec) % RT_NS_1MS;
                    }
                    return pTime;
                }
            }
        }
    }

    /*
     * The fallback is to use the current offset.
     * (A better fallback would be to use the offset of the same time of the year.)
     */
    LocalTime = *pTimeSpec;
    int64_t cNsUtcOffset = RTTimeLocalDeltaNano();
    RTTimeSpecAddNano(&LocalTime, cNsUtcOffset);
    pTime = RTTimeExplode(pTime, &LocalTime);
    if (pTime)
    {
        pTime->fFlags = (pTime->fFlags & ~RTTIME_FLAGS_TYPE_MASK) | RTTIME_FLAGS_TYPE_LOCAL;
        pTime->offUTC = cNsUtcOffset / RT_NS_1MIN;
    }
    return pTime;
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
    RTTIMESPEC LocalTime;
    if (g_pfnSystemTimeToTzSpecificLocalTime)
    {
        /*
         * FileTimeToLocalFileTime does not do the right thing, so we'll have
         * to convert to system time and SystemTimeToTzSpecificLocalTime instead.
         *
         * Note! FileTimeToSystemTime drops resoultion down to milliseconds, thus
         *       we have to do the offUTC calculation using milliseconds and adjust
         *       u32Nanosecons by sub milliseconds digits.
         */
        SYSTEMTIME SystemTimeIn;
        FILETIME FileTime;
        if (FileTimeToSystemTime(RTTimeSpecGetNtFileTime(pTimeSpec, &FileTime), &SystemTimeIn))
        {
            SYSTEMTIME SystemTimeOut;
            if (g_pfnSystemTimeToTzSpecificLocalTime(NULL /* use current TZI */, &SystemTimeIn, &SystemTimeOut))
            {
                if (SystemTimeToFileTime(&SystemTimeOut, &FileTime))
                {
                    RTTimeSpecSetNtFileTime(&LocalTime, &FileTime);

                    return (RTTimeSpecGetMilli(&LocalTime) - RTTimeSpecGetMilli(pTimeSpec)) * RT_NS_1MS;
                }
            }
        }
    }

    return RTTimeLocalDeltaNano();
}

