/* $Id: RTTimeFormatDurationEx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTTimeFormatInterval.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/time.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


static size_t rtTimeFormatDurationNumberEx(char *psz, uint32_t uValue, size_t cchValue)
{
    switch (cchValue)
    {
        case 10:
            *psz++ = (uint8_t)(uValue / 1000000000) + '0';
            uValue %=                   1000000000;
            RT_FALL_THROUGH();
        case 9:
            *psz++ = (uint8_t)(uValue / 100000000) + '0';
            uValue %=                   100000000;
            RT_FALL_THROUGH();
        case 8:
            *psz++ = (uint8_t)(uValue / 10000000) + '0';
            uValue %=                   10000000;
            RT_FALL_THROUGH();
        case 7:
            *psz++ = (uint8_t)(uValue / 1000000) + '0';
            uValue %=                   1000000;
            RT_FALL_THROUGH();
        case 6:
            *psz++ = (uint8_t)(uValue / 100000) + '0';
            uValue %=                   100000;
            RT_FALL_THROUGH();
        case 5:
            *psz++ = (uint8_t)(uValue / 10000) + '0';
            uValue %=                   10000;
            RT_FALL_THROUGH();
        case 4:
            *psz++ = (uint8_t)(uValue / 1000) + '0';
            uValue %=                   1000;
            RT_FALL_THROUGH();
        case 3:
            *psz++ = (uint8_t)(uValue / 100) + '0';
            uValue %=                   100;
            RT_FALL_THROUGH();
        case 2:
            *psz++ = (uint8_t)(uValue / 10) + '0';
            uValue %=                   10;
            RT_FALL_THROUGH();
        case 1:
            *psz++ = (uint8_t)uValue + '0';
            break;
    }
    return cchValue;
}


static size_t rtTimeFormatDurationNumber(char *psz, uint32_t uValue)
{
    size_t cchValue;
    if (uValue < 10)
        cchValue = 1;
    else if (uValue < 100)
        cchValue = 2;
    else if (uValue < 1000)
        cchValue = 3;
    else if (uValue < 10000)
        cchValue = 4;
    else if (uValue < 100000)
        cchValue = 5;
    else if (uValue < 1000000)
        cchValue = 6;
    else if (uValue < 10000000)
        cchValue = 7;
    else if (uValue < 100000000)
        cchValue = 8;
    else if (uValue < 1000000000)
        cchValue = 9;
    else
        cchValue = 10;
    return rtTimeFormatDurationNumberEx(psz, uValue, cchValue);
}


static ssize_t rtTimeFormatDurationCopyOutResult(char *pszDst, size_t cbDst, const char *pszValue, size_t cchValue)
{
    if (cbDst > cchValue)
    {
        memcpy(pszDst, pszValue, cchValue);
        pszDst[cchValue] = '\0';
        return cchValue;
    }
    if (cbDst)
    {
        memcpy(pszDst, pszValue, cbDst);
        pszDst[cbDst - 1] = '\0';
    }
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Formats duration as best we can according to ISO-8601.
 *
 * The returned value is on the form "[-]PnnnnnWnDTnnHnnMnn.fffffffffS", where a
 * sequence of 'n' can be between 1 and the given lenght, and all but the
 * "nn.fffffffffS" part is optional and will only be outputted when the duration
 * is sufficiently large.  The code currently does not omit any inbetween
 * elements other than the day count (D), so an exactly 7 day duration is
 * formatted as "P1WT0H0M0.000000000S" when @a cFractionDigits is 9.
 *
 * @returns Number of characters in the output on success. VERR_BUFFER_OVEFLOW
 *          on failure.
 * @retval  VERR_OUT_OF_RANGE if @a cFractionDigits is too large.
 * @param   pszDst          Pointer to the output buffer.  In case of overflow,
 *                          the max number of characters will be written and
 *                          zero terminated, provided @a cbDst isn't zero.
 * @param   cbDst           The size of the output buffer.
 * @param   pDuration       The duration to format.
 * @param   cFractionDigits Number of digits in the second fraction part. Zero
 *                          for whole no fraction. Max is 9 (nano seconds).
 */
RTDECL(ssize_t) RTTimeFormatDurationEx(char *pszDst, size_t cbDst, PCRTTIMESPEC pDuration, uint32_t cFractionDigits)
{
    AssertReturn(cFractionDigits <= 9, VERR_OUT_OF_RANGE);
    AssertReturn(cbDst != 0, VERR_BUFFER_OVERFLOW);

    /*
     * Get the seconds and .
     */
    int64_t cNanoSecsSigned = RTTimeSpecGetNano(pDuration);

    /* Special case: zero interval */
    if (cNanoSecsSigned == 0)
        return rtTimeFormatDurationCopyOutResult(pszDst, cbDst, RT_STR_TUPLE("PT0S"));

    char  szTmp[64];
    size_t offTmp = 0;

    /* Negative intervals aren't really allowed by the standard, but we slap a
       minus in from of the 'P' and get on with it. */
    if (cNanoSecsSigned < 0)
    {
        szTmp[offTmp++] = '-';
        cNanoSecsSigned = -cNanoSecsSigned;
    }
    uint64_t cNanoSecs = (uint64_t)cNanoSecsSigned;

    /* Emit the duration indicator: */
    szTmp[offTmp++] = 'P';
    size_t const offPostP = offTmp;

    /* Any full weeks? */
    if (cNanoSecs >= RT_NS_1WEEK)
    {
        uint64_t const cWeeks = cNanoSecs / RT_NS_1WEEK; /* (the max value here is 15250) */
        cNanoSecs %= RT_NS_1WEEK;
        offTmp += rtTimeFormatDurationNumber(&szTmp[offTmp], (uint32_t)cWeeks);
        szTmp[offTmp++] = 'W';
    }

    /* Any full days?*/
    if (cNanoSecs >= RT_NS_1DAY)
    {
        uint8_t const cDays = (uint8_t)(cNanoSecs / RT_NS_1DAY);
        cNanoSecs %= RT_NS_1DAY;
        szTmp[offTmp++] = '0' + cDays;
        szTmp[offTmp++] = 'D';
    }

    szTmp[offTmp++] = 'T';

    /* Hours: */
    if (cNanoSecs >= RT_NS_1HOUR || offTmp > offPostP + 1)
    {
        uint8_t const cHours = (uint8_t)(cNanoSecs / RT_NS_1HOUR);
        cNanoSecs %= RT_NS_1HOUR;
        offTmp += rtTimeFormatDurationNumber(&szTmp[offTmp], cHours);
        szTmp[offTmp++] = 'H';
    }

    /* Minutes: */
    if (cNanoSecs >= RT_NS_1MIN || offTmp > offPostP + 1)
    {
        uint8_t const cMins = (uint8_t)(cNanoSecs / RT_NS_1MIN);
        cNanoSecs %= RT_NS_1MIN;
        offTmp += rtTimeFormatDurationNumber(&szTmp[offTmp], cMins);
        szTmp[offTmp++] = 'M';
    }

    /* Seconds: */
    uint8_t const cSecs = (uint8_t)(cNanoSecs / RT_NS_1SEC);
    cNanoSecs %= RT_NS_1SEC;
    offTmp += rtTimeFormatDurationNumber(&szTmp[offTmp], cSecs);
    if (cFractionDigits > 0)
    {
        szTmp[offTmp++] = '.';
        static uint32_t const s_auFactors[9] = { 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };
        offTmp += rtTimeFormatDurationNumberEx(&szTmp[offTmp], (uint32_t)(cNanoSecs / s_auFactors[cFractionDigits - 1]),
                                               cFractionDigits);
    }
    szTmp[offTmp++] = 'S';
    szTmp[offTmp] = '\0';

    return rtTimeFormatDurationCopyOutResult(pszDst, cbDst, szTmp, offTmp);
}
RT_EXPORT_SYMBOL(RTTimeFormatDurationEx);


/**
 * Formats duration as best we can according to ISO-8601, with no fraction.
 *
 * See RTTimeFormatDurationEx for details.
 *
 * @returns Number of characters in the output on success. VERR_BUFFER_OVEFLOW
 *          on failure.
 * @param   pszDst          Pointer to the output buffer.  In case of overflow,
 *                          the max number of characters will be written and
 *                          zero terminated, provided @a cbDst isn't zero.
 * @param   cbDst           The size of the output buffer.
 * @param   pDuration       The duration to format.
 */
RTDECL(int) RTTimeFormatDuration(char *pszDst, size_t cbDst, PCRTTIMESPEC pDuration)
{
    return RTTimeFormatDurationEx(pszDst, cbDst, pDuration, 0);
}
RT_EXPORT_SYMBOL(RTTimeFormatDuration);

