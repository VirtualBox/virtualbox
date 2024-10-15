/* $Id: utf-8-case.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UTF-8 Case Sensitivity and Folding, Part 1.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/string.h"



RTDECL(int) RTStrICmp(const char *psz1, const char *psz2)
{
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    const char *pszStart1 = psz1;
    for (;;)
    {
        /* Get the codepoints */
        RTUNICP uc1;
        int rc = RTStrGetCpEx(&psz1, &uc1);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz1--;
            break;
        }

        RTUNICP uc2;
        rc = RTStrGetCpEx(&psz2, &uc2);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz2--;
            psz1 = RTStrPrevCp(pszStart1, psz1);
            break;
        }

        /* compare */
        int iDiff = uc1 - uc2;
        if (iDiff)
        {
            iDiff = RTUniCpToUpper(uc1) != RTUniCpToUpper(uc2);
            if (iDiff)
            {
                iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* lower case diff last! */
                if (iDiff)
                    return iDiff;
            }
        }

        /* hit the terminator? */
        if (!uc1)
            return 0;
    }

    /* Hit some bad encoding, continue in case sensitive mode. */
    return RTStrCmp(psz1, psz2);
}
RT_EXPORT_SYMBOL(RTStrICmp);


RTDECL(int) RTStrNICmp(const char *psz1, const char *psz2, size_t cchMax)
{
    if (cchMax == 0)
        return 0;
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    for (;;)
    {
        /* Get the codepoints */
        RTUNICP uc1;
        size_t cchMax2 = cchMax;
        int rc = RTStrGetCpNEx(&psz1, &cchMax, &uc1);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz1--;
            cchMax++;
            break;
        }

        RTUNICP uc2;
        rc = RTStrGetCpNEx(&psz2, &cchMax2, &uc2);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz2--;
            psz1 -= (cchMax - cchMax2 + 1);  /* This can't overflow, can it? */
            cchMax = cchMax2 + 1;
            break;
        }

        /* compare */
        int iDiff = uc1 - uc2;
        if (iDiff)
        {
            iDiff = RTUniCpToUpper(uc1) != RTUniCpToUpper(uc2);
            if (iDiff)
            {
                iDiff = RTUniCpToLower(uc1) - RTUniCpToLower(uc2); /* lower case diff last! */
                if (iDiff)
                    return iDiff;
            }
        }

        /* hit the terminator? */
        if (!uc1 || cchMax == 0)
            return 0;
    }

    /* Hit some bad encoding, continue in case insensitive mode. */
    return RTStrNCmp(psz1, psz2, cchMax);
}
RT_EXPORT_SYMBOL(RTStrNICmp);


RTDECL(char *) RTStrIStr(const char *pszHaystack, const char *pszNeedle)
{
    /* Any NULL strings means NULL return. (In the RTStrCmp tradition.) */
    if (!pszHaystack)
        return NULL;
    if (!pszNeedle)
        return NULL;

    /* The empty string matches everything. */
    if (!*pszNeedle)
        return (char *)pszHaystack;

    /*
     * The search strategy is to pick out the first char of the needle, fold it,
     * and match it against the haystack code point by code point. When encountering
     * a matching code point we use RTStrNICmp for the remainder (if any) of the needle.
     */
    const char * const pszNeedleStart = pszNeedle;
    RTUNICP Cp0;
    RTStrGetCpEx(&pszNeedle, &Cp0);     /* pszNeedle is advanced one code point. */
    size_t const    cchNeedle   = strlen(pszNeedle);
    size_t const    cchNeedleCp0= pszNeedle - pszNeedleStart;
    RTUNICP const   Cp0Lower    = RTUniCpToLower(Cp0);
    RTUNICP const   Cp0Upper    = RTUniCpToUpper(Cp0);
    if (    Cp0Lower == Cp0Upper
        &&  Cp0Lower == Cp0)
    {
        /* Cp0 is not a case sensitive char. */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    Cp == Cp0
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }
    else if (   Cp0Lower == Cp0
             || Cp0Upper != Cp0)
    {
        /* Cp0 is case sensitive */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    (   Cp == Cp0Upper
                     || Cp == Cp0Lower)
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }
    else
    {
        /* Cp0 is case sensitive and folds to two difference chars. (paranoia) */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    (   Cp == Cp0
                     || Cp == Cp0Upper
                     || Cp == Cp0Lower)
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }


    return NULL;
}
RT_EXPORT_SYMBOL(RTStrIStr);


RTDECL(char *) RTStrToLower(char *psz)
{
    /*
     * Loop the code points in the string, converting them one by one.
     *
     * ASSUMES that the folded code points have an encoding that is equal or
     *         shorter than the original (this is presently correct).
     */
    const char *pszSrc = psz;
    char       *pszDst = psz;
    RTUNICP     uc;
    do
    {
        int rc = RTStrGetCpEx(&pszSrc, &uc);
        if (RT_SUCCESS(rc))
        {
            RTUNICP uc2 = RTUniCpToLower(uc);
            if (RT_LIKELY(   uc2 == uc
                          || RTUniCpCalcUtf8Len(uc2) == RTUniCpCalcUtf8Len(uc)))
                pszDst = RTStrPutCp(pszDst, uc2);
            else
                pszDst = RTStrPutCp(pszDst, uc);
        }
        else
        {
            /* bad encoding, just copy it quietly (uc == RTUNICP_INVALID (!= 0)). */
            AssertRC(rc);
            *pszDst++ = pszSrc[-1];
        }
        Assert((uintptr_t)pszDst <= (uintptr_t)pszSrc);
    } while (uc != 0);

    return psz;
}
RT_EXPORT_SYMBOL(RTStrToLower);


RTDECL(char *) RTStrToUpper(char *psz)
{
    /*
     * Loop the code points in the string, converting them one by one.
     *
     * ASSUMES that the folded code points have an encoding that is equal or
     *         shorter than the original (this is presently correct).
     */
    const char *pszSrc = psz;
    char       *pszDst = psz;
    RTUNICP     uc;
    do
    {
        int rc = RTStrGetCpEx(&pszSrc, &uc);
        if (RT_SUCCESS(rc))
        {
            RTUNICP uc2 = RTUniCpToUpper(uc);
            if (RT_LIKELY(   uc2 == uc
                          || RTUniCpCalcUtf8Len(uc2) == RTUniCpCalcUtf8Len(uc)))
                pszDst = RTStrPutCp(pszDst, uc2);
            else
                pszDst = RTStrPutCp(pszDst, uc);
        }
        else
        {
            /* bad encoding, just copy it quietly (uc == RTUNICP_INVALID (!= 0)). */
            AssertRC(rc);
            *pszDst++ = pszSrc[-1];
        }
        Assert((uintptr_t)pszDst <= (uintptr_t)pszSrc);
    } while (uc != 0);

    return psz;
}
RT_EXPORT_SYMBOL(RTStrToUpper);

