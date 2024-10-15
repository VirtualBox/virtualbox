/* $Id: RTTimeSet-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTTimeSet, OS/2.
 */

/*
 * Contributed by knut st. osmundsen.
 *
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
 * Copyright (c) 2018 knut st. osmundsen <bird-src-spam@anduin.net>
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
#define LOG_GROUP RTLOGGROUP_TIME
#define INCL_DOSDATETIME
#define INCL_DOSERRORS
#include <os2.h>

#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTTimeSet(PCRTTIMESPEC pTime)
{
    /*
     * Convert to local time and explode it, keeping the distance
     * between UTC and local.
     */
    int64_t    cNsLocalDelta = RTTimeLocalDeltaNanoFor(pTime);
    RTTIMESPEC TimeLocal     = *pTime;
    RTTIME     Exploded;
    if (RTTimeExplode(&Exploded, RTTimeSpecAddNano(&TimeLocal, cNsLocalDelta)))
    {
        /*
         * Fill in the OS/2 structure and make the call.
         */
        DATETIME DateTime;
        DateTime.hours      = Exploded.u8Hour;
        DateTime.minutes    = Exploded.u8Minute;
        DateTime.seconds    = Exploded.u8Second;
        DateTime.hundredths = (uint8_t)(Exploded.u32Nanosecond / (RT_NS_1SEC_64 / 100));
        DateTime.day        = Exploded.u8MonthDay;
        DateTime.month      = Exploded.u8Month;
        DateTime.year       = (uint16_t)Exploded.i32Year;
        DateTime.weekday    = Exploded.u8WeekDay;

        /* Minutes from UTC.  http://www.edm2.com/os2api/Dos/DosSetDateTime.html says
           that timezones west of UTC should have a positive value.  The kernel fails
           the call if we're more than +/-780 min (13h) distant, so clamp it in
           case of bogus TZ values. */
        DateTime.timezone   = (int16_t)(-cNsLocalDelta / (int64_t)RT_NS_1MIN);
        if (DateTime.timezone > 780)
            DateTime.timezone = 780;
        else if (DateTime.timezone < -780)
            DateTime.timezone = -780;

        APIRET rc = DosSetDateTime(&DateTime);
        if (rc == NO_ERROR)
            return VINF_SUCCESS;
        AssertMsgFailed(("rc=%u\n", rc));
        return RTErrConvertFromOS2(rc);
    }
    return VERR_INVALID_PARAMETER;
}

