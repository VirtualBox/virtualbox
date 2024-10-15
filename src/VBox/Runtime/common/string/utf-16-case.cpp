/* $Id: utf-16-case.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UTF-16, Case Sensitivity.
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
#include <iprt/utf16.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/string.h"


RTDECL(int) RTUtf16ICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    PCRTUTF16 pwsz1Start = pwsz1; /* keep it around in case we have to backtrack on a surrogate pair */
    for (;;)
    {
        RTUTF16 wc1 = *pwsz1;
        RTUTF16 wc2 = *pwsz2;
        int     iDiff = wc1 - wc2;
        if (iDiff)
        {
            /* unless they are *both* surrogate pairs, there is no chance they'll be identical. */
            if (    wc1 < 0xd800
                ||  wc2 < 0xd800
                ||  wc1 > 0xdfff
                ||  wc2 > 0xdfff)
            {
                /* simple UCS-2 char */
                iDiff = RTUniCpToUpper(wc1) - RTUniCpToUpper(wc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(wc1) - RTUniCpToLower(wc2);
            }
            else
            {
                /* a damned pair */
                RTUNICP uc1;
                RTUNICP uc2;
                if (wc1 >= 0xdc00)
                {
                    if (pwsz1Start == pwsz1)
                        return iDiff;
                    uc1 = pwsz1[-1];
                    if (uc1 < 0xd800 || uc1 >= 0xdc00)
                        return iDiff;
                    uc1 = 0x10000 + (((uc1       & 0x3ff) << 10) | (wc1 & 0x3ff));
                    uc2 = 0x10000 + (((pwsz2[-1] & 0x3ff) << 10) | (wc2 & 0x3ff));
                }
                else
                {
                    uc1 = *++pwsz1;
                    if (uc1 < 0xdc00 || uc1 >= 0xe000)
                        return iDiff;
                    uc1 = 0x10000 + (((wc1 & 0x3ff) << 10) | (uc1      & 0x3ff));
                    uc2 = 0x10000 + (((wc2 & 0x3ff) << 10) | (*++pwsz2 & 0x3ff));
                }
                iDiff = RTUniCpToUpper(uc1) - RTUniCpToUpper(uc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* serious paranoia! */
            }
            if (iDiff)
                return iDiff;
        }
        if (!wc1)
            return 0;
        pwsz1++;
        pwsz2++;
    }
}
RT_EXPORT_SYMBOL(RTUtf16ICmp);


RTDECL(int) RTUtf16BigICmp(PCRTUTF16 pwsz1,  PCRTUTF16 pwsz2)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    PCRTUTF16 pwsz1Start = pwsz1; /* keep it around in case we have to backtrack on a surrogate pair */
    for (;;)
    {
        RTUTF16 wc1 = *pwsz1;
        RTUTF16 wc2 = *pwsz2;
        int     iDiff = wc1 - wc2;
        if (iDiff)
        {
            /* unless they are *both* surrogate pairs, there is no chance they'll be identical. */
            wc1 = RT_BE2H_U16(wc1);
            wc2 = RT_BE2H_U16(wc2);
            if (    wc1 < 0xd800
                ||  wc2 < 0xd800
                ||  wc1 > 0xdfff
                ||  wc2 > 0xdfff)
            {
                /* simple UCS-2 char */
                iDiff = RTUniCpToUpper(wc1) - RTUniCpToUpper(wc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(wc1) - RTUniCpToLower(wc2);
            }
            else
            {
                /* a damned pair */
                RTUNICP uc1;
                RTUNICP uc2;
                if (wc1 >= 0xdc00)
                {
                    if (pwsz1Start == pwsz1)
                        return iDiff;
                    uc1 = RT_BE2H_U16(pwsz1[-1]);
                    if (uc1 < 0xd800 || uc1 >= 0xdc00)
                        return iDiff;
                    uc1 = 0x10000 + (((uc1                    & 0x3ff) << 10) | (wc1 & 0x3ff));
                    uc2 = 0x10000 + (((RT_BE2H_U16(pwsz2[-1]) & 0x3ff) << 10) | (wc2 & 0x3ff));
                }
                else
                {
                    RTUTF16 wcTmp = *++pwsz1;
                    uc1 = RT_BE2H_U16(wcTmp);
                    if (uc1 < 0xdc00 || uc1 >= 0xe000)
                        return iDiff;
                    uc1 = 0x10000 + (((wc1 & 0x3ff) << 10) | (uc1                & 0x3ff));
                    wcTmp = *++pwsz2;
                    uc2 = 0x10000 + (((wc2 & 0x3ff) << 10) | (RT_BE2H_U16(wcTmp) & 0x3ff));
                }
                iDiff = RTUniCpToUpper(uc1) - RTUniCpToUpper(uc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* serious paranoia! */
            }
            if (iDiff)
                return iDiff;
        }
        if (!wc1)
            return 0;
        pwsz1++;
        pwsz2++;
    }
}
RT_EXPORT_SYMBOL(RTUtf16BigICmp);


RTDECL(int) RTUtf16ICmpUtf8(PCRTUTF16 pwsz1, const char *psz2)
{
    /*
     * NULL and empty strings are all the same.
     */
    if (!pwsz1)
        return !psz2 || !*psz2 ? 0 : -1;
    if (!psz2)
        return !*pwsz1         ? 0 :  1;

    /*
     * Compare with a UTF-8 string by enumerating them char by char.
     */
    for (;;)
    {
        RTUNICP uc1;
        int rc = RTUtf16GetCpEx(&pwsz1, &uc1);
        AssertRCReturn(rc, 1);

        RTUNICP uc2;
        rc = RTStrGetCpEx(&psz2, &uc2);
        AssertRCReturn(rc, -1);
        if (uc1 == uc2)
        {
            if (uc1)
                continue;
            return 0;
        }

        if (RTUniCpToUpper(uc1) == RTUniCpToUpper(uc2))
            continue;
        if (RTUniCpToLower(uc1) == RTUniCpToLower(uc2))
            continue;
        return uc1 < uc2 ? -1 : 1;
    }
}
RT_EXPORT_SYMBOL(RTUtf16CmpIUtf8);


RTDECL(int) RTUtf16NICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2, size_t cwcMax)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    PCRTUTF16 pwsz1Start = pwsz1; /* keep it around in case we have to backtrack on a surrogate pair */
    while (cwcMax-- > 0)
    {
        RTUTF16 wc1 = *pwsz1;
        RTUTF16 wc2 = *pwsz2;
        int     iDiff = wc1 - wc2;
        if (iDiff)
        {
            /* unless they are *both* surrogate pairs, there is no chance they'll be identical. */
            if (    wc1 < 0xd800
                ||  wc2 < 0xd800
                ||  wc1 > 0xdfff
                ||  wc2 > 0xdfff)
            {
                /* simple UCS-2 char */
                iDiff = RTUniCpToUpper(wc1) - RTUniCpToUpper(wc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(wc1) - RTUniCpToLower(wc2);
            }
            else
            {
                /* a damned pair */
                RTUNICP uc1;
                RTUNICP uc2;
                if (wc1 >= 0xdc00)
                {
                    if (pwsz1Start == pwsz1)
                        return iDiff;
                    uc1 = pwsz1[-1];
                    if (uc1 < 0xd800 || uc1 >= 0xdc00)
                        return iDiff;
                    uc1 = 0x10000 + (((uc1       & 0x3ff) << 10) | (wc1 & 0x3ff));
                    uc2 = 0x10000 + (((pwsz2[-1] & 0x3ff) << 10) | (wc2 & 0x3ff));
                }
                else if (cwcMax-- > 0)
                {
                    uc1 = *++pwsz1;
                    if (uc1 < 0xdc00 || uc1 >= 0xe000)
                        return iDiff;
                    uc1 = 0x10000 + (((wc1 & 0x3ff) << 10) | (uc1      & 0x3ff));
                    uc2 = 0x10000 + (((wc2 & 0x3ff) << 10) | (*++pwsz2 & 0x3ff));
                }
                else
                {
                    iDiff = wc1 - wc2;
                    return iDiff;
                }
                iDiff = RTUniCpToUpper(uc1) - RTUniCpToUpper(uc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* serious paranoia! */
            }
            if (iDiff)
                return iDiff;
        }
        if (!wc1)
            return 0;
        pwsz1++;
        pwsz2++;
    }
    return 0;
}
RT_EXPORT_SYMBOL(RTUtf16NICmp);


RTDECL(int) RTUtf16BigNICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2, size_t cwcMax)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    PCRTUTF16 pwsz1Start = pwsz1; /* keep it around in case we have to backtrack on a surrogate pair */
    while (cwcMax-- > 0)
    {
        RTUTF16 wc1 = *pwsz1;
        RTUTF16 wc2 = *pwsz2;
        int     iDiff = wc1 - wc2;
        if (iDiff)
        {
            /* unless they are *both* surrogate pairs, there is no chance they'll be identical. */
            wc1 = RT_BE2H_U16(wc1);
            wc2 = RT_BE2H_U16(wc2);
            if (    wc1 < 0xd800
                ||  wc2 < 0xd800
                ||  wc1 > 0xdfff
                ||  wc2 > 0xdfff)
            {
                /* simple UCS-2 char */
                iDiff = RTUniCpToUpper(wc1) - RTUniCpToUpper(wc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(wc1) - RTUniCpToLower(wc2);
            }
            else
            {
                /* a damned pair */
                RTUNICP uc1;
                RTUNICP uc2;
                if (wc1 >= 0xdc00)
                {
                    if (pwsz1Start == pwsz1)
                        return iDiff;
                    uc1 = RT_BE2H_U16(pwsz1[-1]);
                    if (uc1 < 0xd800 || uc1 >= 0xdc00)
                        return iDiff;
                    uc1 = 0x10000 + (((uc1                    & 0x3ff) << 10) | (wc1 & 0x3ff));
                    uc2 = 0x10000 + (((RT_BE2H_U16(pwsz2[-1]) & 0x3ff) << 10) | (wc2 & 0x3ff));
                }
                else if (cwcMax > 0)
                {
                    RTUTF16 wcTmp = *++pwsz1;
                    uc1 = RT_BE2H_U16(wcTmp);
                    if (uc1 < 0xdc00 || uc1 >= 0xe000)
                        return iDiff;
                    uc1 = 0x10000 + (((wc1 & 0x3ff) << 10) | (uc1                & 0x3ff));
                    wcTmp = *++pwsz2;
                    uc2 = 0x10000 + (((wc2 & 0x3ff) << 10) | (RT_BE2H_U16(wcTmp) & 0x3ff));
                }
                else
                {
                    iDiff = wc1 - wc2;
                    return iDiff;
                }
                iDiff = RTUniCpToUpper(uc1) - RTUniCpToUpper(uc2);
                if (iDiff)
                    iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* serious paranoia! */
            }
            if (iDiff)
                return iDiff;
        }
        if (!wc1)
            return 0;
        pwsz1++;
        pwsz2++;
    }
    return 0;
}
RT_EXPORT_SYMBOL(RTUtf16BigNICmp);


RTDECL(PRTUTF16) RTUtf16ToLower(PRTUTF16 pwsz)
{
    PRTUTF16 pwc = pwsz;
    for (;;)
    {
        RTUTF16 wc = *pwc;
        if (!wc)
            break;
        if (wc < 0xd800 || wc >= 0xdc00)
        {
            RTUNICP ucFolded = RTUniCpToLower(wc);
            if (ucFolded < 0x10000)
                *pwc++ = RTUniCpToLower(wc);
        }
        else
        {
            /* surrogate */
            RTUTF16 wc2 = pwc[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                RTUNICP ucFolded = RTUniCpToLower(uc);
                if (uc != ucFolded && ucFolded >= 0x10000) /* we don't support shrinking the string */
                {
                    uc -= 0x10000;
                    *pwc++ = 0xd800 | (uc >> 10);
                    *pwc++ = 0xdc00 | (uc & 0x3ff);
                }
            }
            else /* invalid encoding. */
                pwc++;
        }
    }
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16ToLower);


RTDECL(PRTUTF16) RTUtf16ToUpper(PRTUTF16 pwsz)
{
    PRTUTF16 pwc = pwsz;
    for (;;)
    {
        RTUTF16 wc = *pwc;
        if (!wc)
            break;
        if (wc < 0xd800 || wc >= 0xdc00)
            *pwc++ = RTUniCpToUpper(wc);
        else
        {
            /* surrogate */
            RTUTF16 wc2 = pwc[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                RTUNICP ucFolded = RTUniCpToUpper(uc);
                if (uc != ucFolded && ucFolded >= 0x10000) /* we don't support shrinking the string */
                {
                    uc -= 0x10000;
                    *pwc++ = 0xd800 | (uc >> 10);
                    *pwc++ = 0xdc00 | (uc & 0x3ff);
                }
            }
            else /* invalid encoding. */
                pwc++;
        }
    }
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16ToUpper);

