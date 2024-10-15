/* $Id: strversion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Version String Parsing.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTSTRVER_IS_PUNCTUACTION(ch)    \
    ( (ch) == '_' || (ch) == '-' || (ch) == '+' || RT_C_IS_PUNCT(ch) )


/**
 * Parses a out the next block from a version string.
 *
 * @returns true if numeric, false if not.
 * @param   ppszVer             The string cursor, IN/OUT.
 * @param   pi32Value           Where to return the value if numeric.
 * @param   pcchBlock           Where to return the block length.
 */
static bool rtStrVersionParseBlock(const char **ppszVer, int32_t *pi32Value, size_t *pcchBlock)
{
    const char *psz = *ppszVer;

    /*
     * Check for end-of-string.
     */
    if (!*psz)
    {
        *pi32Value = 0;
        *pcchBlock = 0;
        return false;
    }

    /*
     * Try convert the block to a number the simple way.
     */
    char ch;
    bool fNumeric = RT_C_IS_DIGIT(*psz);
    if (fNumeric)
    {
        do
            ch = *++psz;
        while (ch && RT_C_IS_DIGIT(ch));

        int rc = RTStrToInt32Ex(*ppszVer, NULL, 10, pi32Value);
        if (RT_FAILURE(rc) || rc == VWRN_NUMBER_TOO_BIG)
        {
            AssertRC(rc);
            fNumeric = false;
            *pi32Value = 0;
        }
    }
    else
    {
        /*
         * Find the end of the current string.  Make a special case for SVN
         * revision numbers that immediately follows a release tag string.
         */
        do
            ch = *++psz;
        while (    ch
               && !RT_C_IS_DIGIT(ch)
               && !RTSTRVER_IS_PUNCTUACTION(ch));

        size_t cchBlock = psz - *ppszVer;
        if (   cchBlock > 1
            && psz[-1] == 'r'
            && RT_C_IS_DIGIT(*psz))
        {
            psz--;
            cchBlock--;
        }


        /*
         * Translate standard pre release terms to negative values.
         */
        static const struct
        {
            size_t      cch;
            const char *psz;
            int32_t     iValue;
        } s_aTerms[] =
        {
            { 2, "RC",      -100000 },
            { 3, "PRE",     -200000 },
            { 5, "GAMMA",   -300000 },
            { 4, "BETA",    -400000 },
            { 5, "ALPHA",   -500000 }
        };

        int32_t iVal1 = 0;
        for (unsigned i = 0; i < RT_ELEMENTS(s_aTerms); i++)
            if (   cchBlock == s_aTerms[i].cch
                && !RTStrNCmp(s_aTerms[i].psz, *ppszVer, cchBlock))
            {
                iVal1 = s_aTerms[i].iValue;
                break;
            }
        if (iVal1 != 0)
        {
            /*
             * Does the prelease term have a trailing number?
             * Add it assuming BETA == BETA1.
             */
            if (RT_C_IS_DIGIT(*psz))
            {
                const char *psz2 = psz;
                do
                    ch = *++psz;
                while (   ch
                       && RT_C_IS_DIGIT(ch)
                       && !RTSTRVER_IS_PUNCTUACTION(ch));

                int rc = RTStrToInt32Ex(psz2, NULL, 10, pi32Value);
                if (RT_SUCCESS(rc) && rc != VWRN_NUMBER_TOO_BIG && *pi32Value)
                    iVal1 += *pi32Value - 1;
                else
                {
                    AssertRC(rc);
                    psz = psz2;
                }
            }
            fNumeric = true;
        }
        *pi32Value = iVal1;
    }
    *pcchBlock = psz - *ppszVer;

    /*
     * Skip trailing punctuation.
     */
    if (RTSTRVER_IS_PUNCTUACTION(*psz))
        psz++;
    *ppszVer = psz;

    return fNumeric;
}


RTDECL(int) RTStrVersionCompare(const char *pszVer1, const char *pszVer2)
{
    AssertPtr(pszVer1);
    AssertPtr(pszVer2);

    /*
     * Do a parallel parse of the strings.
     */
    while (*pszVer1 || *pszVer2)
    {
        const char *pszBlock1 = pszVer1;
        size_t      cchBlock1;
        int32_t     iVal1;
        bool        fNumeric1 = rtStrVersionParseBlock(&pszVer1, &iVal1, &cchBlock1);

        const char *pszBlock2 = pszVer2;
        size_t      cchBlock2;
        int32_t     iVal2;
        bool        fNumeric2 = rtStrVersionParseBlock(&pszVer2, &iVal2, &cchBlock2);

        if (fNumeric1 && fNumeric2)
        {
            if (iVal1 != iVal2)
                return iVal1 < iVal2 ? -1 : 1;
        }
        else if (   fNumeric1 != fNumeric2
                 && (  fNumeric1
                     ? iVal1 == 0 && cchBlock2 == 0
                     : iVal2 == 0 && cchBlock1 == 0)
                )
        {
            /*else: 1.0 == 1.0.0.0.0. */;
        }
        else if (   fNumeric1 != fNumeric2
                 && (fNumeric1 ? iVal1 : iVal2) < 0)
        {
            /* Pre-release indicators are smaller than all other strings. */
            return fNumeric1 ? -1 : 1;
        }
        else
        {
            int iDiff = RTStrNICmp(pszBlock1, pszBlock2, RT_MIN(cchBlock1, cchBlock2));
            if (!iDiff && cchBlock1 != cchBlock2)
                iDiff = cchBlock1 < cchBlock2 ? -1 : 1;
            if (iDiff)
                return iDiff < 0 ? -1 : 1;
        }
    }
    return 0;
}
RT_EXPORT_SYMBOL(RTStrVersionCompare);
