/* $Id: RTPathCalcRelative.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathCreateRelative.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
# include <iprt/uni.h>
#endif
#include "internal/path.h"


#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
/** Helper for doing case insensitive comparison of a mismatching codepoint. */
DECLINLINE(bool) rtPathCalcRelativeEqualICaseCodepoint(const char *pszPathFromStart, const char *pszPathFrom,
                                                       const char *pszPathToStart,   const char *pszPathTo)
{
    RTUNICP ucFrom = RTStrGetCp(RTStrPrevCp(pszPathFromStart, pszPathFrom));
    RTUNICP ucTo   = RTStrGetCp(RTStrPrevCp(pszPathToStart,   pszPathTo));
    return ucFrom == ucTo
        || RTUniCpToLower(ucFrom) == RTUniCpToLower(ucTo)
        || RTUniCpToUpper(ucFrom) == RTUniCpToUpper(ucTo);
}
#endif


RTDECL(int) RTPathCalcRelative(char *pszPathDst, size_t cbPathDst,
                               const char *pszPathFrom, bool fFromFile,
                               const char *pszPathTo)
{
    AssertPtrReturn(pszPathDst, VERR_INVALID_POINTER);
    AssertReturn(cbPathDst, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszPathFrom, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPathTo, VERR_INVALID_POINTER);
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    const char * const pszPathFromStart = pszPathFrom;
    const char * const pszPathToStart   = pszPathTo;
#endif

    /*
     * Check for different root specifiers (drive letters), creating a relative path doesn't work here.
     */
    size_t offRootFrom = rtPathRootSpecLen(pszPathFrom);
    AssertReturn(offRootFrom > 0, VERR_INVALID_PARAMETER);

    size_t offRootTo   = rtPathRootSpecLen(pszPathTo);
    AssertReturn(offRootTo > 0, VERR_INVALID_PARAMETER);


    /** @todo correctly deal with extra root slashes! */
    if (offRootFrom != offRootTo)
        return VERR_NOT_SUPPORTED;

#if RTPATH_STYLE != RTPATH_STR_F_STYLE_DOS
    if (RTStrNCmp(pszPathFrom, pszPathTo, offRootFrom))
        return VERR_NOT_SUPPORTED;
#else
    if (RTStrNICmp(pszPathFrom, pszPathTo, offRootFrom))
    {
        const char *pszFromCursor = pszPathFrom;
        const char *pszToCursor   = pszPathTo;
        while ((size_t)(pszFromCursor - pszPathFrom) < offRootFrom)
        {
            RTUNICP ucFrom;
            int rc = RTStrGetCpEx(&pszFromCursor, &ucFrom);
            AssertRCReturn(rc, rc);

            RTUNICP ucTo;
            rc = RTStrGetCpEx(&pszToCursor, &ucTo);
            AssertRCReturn(rc, rc);
            if (   ucFrom != ucTo
                && RTUniCpToLower(ucFrom) != RTUniCpToLower(ucTo)
                && RTUniCpToUpper(ucFrom) != RTUniCpToUpper(ucTo)
                && (!RTPATH_IS_SLASH(ucFrom) || !RTPATH_IS_SLASH(ucTo)) )
                return VERR_NOT_SUPPORTED;
        }
    }
#endif

    pszPathFrom += offRootFrom;
    pszPathTo   += offRootTo;

    /*
     * Skip out the part of the path which is equal to both.
     */
    const char *pszStartOfFromComp = pszPathFrom;
    for (;;)
    {
        char const chFrom = *pszPathFrom;
        char const chTo   = *pszPathTo;
        if (!RTPATH_IS_SLASH(chFrom))
        {
            if (chFrom == chTo)
            {
                if (chFrom)
                { /* likely */ }
                else
                {
                    /* Special case: The two paths are equal. */
                    if (fFromFile)
                    {
                        size_t cchComp = pszPathFrom - pszStartOfFromComp;
                        if (cchComp < cbPathDst)
                        {
                            memcpy(pszPathDst, pszStartOfFromComp, cchComp);
                            pszPathDst[cchComp] = '\0';
                            return VINF_SUCCESS;
                        }
                    }
                    else if (sizeof(".") <= cbPathDst)
                    {
                        memcpy(pszPathDst, ".", sizeof("."));
                        return VINF_SUCCESS;
                    }
                    return VERR_BUFFER_OVERFLOW;
                }
            }
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
            else if (rtPathCalcRelativeEqualICaseCodepoint(pszPathFromStart, pszPathFrom + 1, pszPathToStart, pszPathTo + 1))
            { /* if not likely, then simpler code structure wise. */ }
#endif
            else if (chFrom != '\0' || !RTPATH_IS_SLASH(chTo) || fFromFile)
                break;
            else
            {
                pszStartOfFromComp = pszPathFrom;
                do
                    pszPathTo++;
                while (RTPATH_IS_SLASH(*pszPathTo));
                break;
            }
            pszPathFrom++;
            pszPathTo++;
        }
        else if (RTPATH_IS_SLASH(chTo))
        {
            /* Both have slashes.  Skip any additional ones before taking down
               the start of the component for rewinding purposes. */
            do
                pszPathTo++;
            while (RTPATH_IS_SLASH(*pszPathTo));
            do
                pszPathFrom++;
            while (RTPATH_IS_SLASH(*pszPathFrom));
            pszStartOfFromComp = pszPathFrom;
        }
        else
            break;
    }

    /* Rewind to the start of the current component. */
    pszPathTo  -= pszPathFrom - pszStartOfFromComp;
    pszPathFrom = pszStartOfFromComp;

    /* Paths point to the first non equal component now. */

    /*
     * Constructure the relative path.
     */

    /* Create the part to go up from pszPathFrom. */
    unsigned offDst = 0;

    if (!fFromFile && *pszPathFrom != '\0')
    {
        if (offDst + 3 < cbPathDst)
        {
            pszPathDst[offDst++] = '.';
            pszPathDst[offDst++] = '.';
            pszPathDst[offDst++] = RTPATH_SLASH;
        }
        else
            return VERR_BUFFER_OVERFLOW;
    }

    while (*pszPathFrom != '\0')
    {
        char ch;
        while (   (ch = *pszPathFrom) != '\0'
               && !RTPATH_IS_SLASH(*pszPathFrom))
            pszPathFrom++;
        while (   (ch = *pszPathFrom) != '\0'
               && RTPATH_IS_SLASH(ch))
            pszPathFrom++;
        if (!ch)
            break;

        if (offDst + 3 < cbPathDst)
        {
            pszPathDst[offDst++] = '.';
            pszPathDst[offDst++] = '.';
            pszPathDst[offDst++] = RTPATH_SLASH;
        }
        else
            return VERR_BUFFER_OVERFLOW;
    }

    /* Now append the rest of pszPathTo to the final path. */
    size_t cchTo = strlen(pszPathTo);
    if (offDst + cchTo <= cbPathDst)
    {
        memcpy(&pszPathDst[offDst], pszPathTo, cchTo);
        pszPathDst[offDst + cchTo] = '\0';
        return VINF_SUCCESS;
    }
    return VERR_BUFFER_OVERFLOW;
}

