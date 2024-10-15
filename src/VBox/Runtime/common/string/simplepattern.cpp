/* $Id: simplepattern.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTStrSimplePattern.
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

#include <iprt/assert.h>


RTDECL(bool) RTStrSimplePatternMatch(const char *pszPattern, const char *pszString)
{
#if 0
    return RTStrSimplePatternNMatch(pszPattern, RTSTR_MAX, pszString, RTSTR_MAX);
#else
    /* ASSUMES ASCII / UTF-8 */
    for (;;)
    {
        char chPat = *pszPattern;
        switch (chPat)
        {
            default:
                if (*pszString != chPat)
                    return false;
                break;

            case '*':
            {
                /* collapse '*' and '?', they are superfluous */
                while ((chPat = *++pszPattern) == '*' || chPat == '?')
                    /* nothing */;

                /* if no more pattern, we're done now.  */
                if (!chPat)
                    return true;

                /* find chPat in the string and try get a match on the remaining pattern. */
                for (;;)
                {
                    char chStr = *pszString++;
                    if (    chStr == chPat
                        &&  RTStrSimplePatternMatch(pszPattern + 1, pszString))
                        return true;
                    if (!chStr)
                        return false;
                }
                /* won't ever get here */
                break;
            }

            case '?':
                if (!*pszString)
                    return false;
                break;

            case '\0':
                return !*pszString;
        }
        pszString++;
        pszPattern++;
    }
#endif
}
RT_EXPORT_SYMBOL(RTStrSimplePatternMatch);


RTDECL(bool) RTStrSimplePatternNMatch(const char *pszPattern, size_t cchPattern,
                                      const char *pszString, size_t cchString)
{
    /* ASSUMES ASCII / UTF-8 */
    for (;;)
    {
        char chPat = cchPattern ? *pszPattern : '\0';
        switch (chPat)
        {
            default:
            {
                char chStr = cchString ? *pszString : '\0';
                if (chStr != chPat)
                    return false;
                break;
            }

            case '*':
            {
                /* Collapse '*' and '?', they are superfluous. End of the pattern == match.  */
                do
                {
                    if (!--cchPattern)
                        return true;
                    chPat = *++pszPattern;
                } while (chPat == '*' || chPat == '?');
                if (!chPat)
                    return true;

                /* Find chPat in the string and try get a match on the remaining pattern. */
                for (;;)
                {
                    if (!cchString--)
                        return false;
                    char chStr = *pszString++;
                    if (    chStr == chPat
                        &&  RTStrSimplePatternNMatch(pszPattern + 1, cchPattern - 1, pszString, cchString))
                        return true;
                    if (!chStr)
                        return false;
                }
                /* won't ever get here */
                break;
            }

            case '?':
                if (!cchString || !*pszString)
                    return false;
                break;

            case '\0':
                return cchString == 0 || !*pszString;
        }

        /* advance */
        pszString++;
        cchString--;
        pszPattern++;
        cchPattern--;
    }
}
RT_EXPORT_SYMBOL(RTStrSimplePatternNMatch);


RTDECL(bool) RTStrSimplePatternMultiMatch(const char *pszPatterns, size_t cchPatterns,
                                          const char *pszString, size_t cchString,
                                          size_t *poffMatchingPattern)
{
    const char *pszCur = pszPatterns;
    while (*pszCur && cchPatterns)
    {
        /*
         * Find the end of the current pattern.
         */
        unsigned char ch = '\0';
        const char *pszEnd = pszCur;
        while (cchPatterns && (ch = *pszEnd) != '\0' && ch != '|')
            cchPatterns--, pszEnd++;

        /*
         * Try match it.
         */
        if (RTStrSimplePatternNMatch(pszCur, pszEnd - pszCur, pszString, cchString))
        {
            if (poffMatchingPattern)
                *poffMatchingPattern = pszCur - pszPatterns;
            return true;
        }

        /* advance */
        if (!ch || !cchPatterns)
            break;
        cchPatterns--;
        pszCur = pszEnd + 1;
    }

    if (poffMatchingPattern)
        *poffMatchingPattern = RTSTR_MAX;
    return false;
}
RT_EXPORT_SYMBOL(RTStrSimplePatternMultiMatch);

