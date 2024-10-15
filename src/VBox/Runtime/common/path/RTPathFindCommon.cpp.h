/* $Id: RTPathFindCommon.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathFindCommon - Code Template.
 *
 * This file included multiple times with different path style macros.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#include "rtpath-root-length-template.cpp.h"


/** Helper for skipping slashes, given a pointer to the first one.   */
DECLINLINE(const char *) RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(const char *pszSlash)
{
    for (;;)
    {
        char ch;
        do
            ch = *++pszSlash;
        while (RTPATH_IS_SLASH(ch));

        /* Also skip '/./' sequences.  */
        if (   ch != '.'
            || !RTPATH_IS_SLASH(pszSlash[1]))
            break;
        pszSlash++;
    }
    return pszSlash;
}


static size_t RTPATH_STYLE_FN(rtPathFindCommon)(size_t cPaths, const char **papszPaths, uint32_t fFlags)
{
    /*
     * Check for '..' elements before we start doing anything.
     *
     * They are currently not supported at all (lazy) and we shun them for
     * security reasons.  Iff we want to support them properly, we'd have to:
     *  1. Note down exactly where the root specification ends for each of
     *     the paths so we can prevent '..' from messing with it.
     *  2. When encountering '..', we'd have to ascend all paths.
     *  3. When encountering a difference, we'd have to see if it's eliminated
     *     by a following '..' sequence.
     *  4. When returning anything, we'd have to see if it could be affected by
     *     a '..' sequence later in any of the paths.
     *
     * We could kind of RTAbsPath the secondary paths, however it wouldn't work
     * for the primary path we use as reference.
     *
     * Summa summarum: Annoyingly tedious, so just forget it.
     */
    if (!(fFlags & RTPATHFINDCOMMON_F_IGNORE_DOTDOT))
        for (size_t i = 0; i < cPaths; i++)
        {
            const char * const psz = papszPaths[i];
            const char *pszDot = strchr(psz, '.');
            while (pszDot)
            {
                if (   pszDot[1] == '.'
                    && (RTPATH_IS_SLASH(pszDot[2]) || pszDot[2] == '\0')
                    && (   pszDot == psz
                        || RTPATH_IS_SLASH(pszDot[-1])
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
                    || (pszDot[-1] == ':' && psz + 2 == pszDot && !(fFlags & RTPATH_STR_F_NO_START))
#endif
                       )
                   )
                    return 0;
                pszDot = strchr(pszDot + 1, '.');
            }
        }

    /*
     * We use the first path as the reference for the return length.
     */
    const char *       pszPath0            = papszPaths[0];
    const char *       pszPath0EndLastComp = pszPath0;
    const char * const pszPath0Start       = pszPath0;

    /*
     * Deal with root stuff as appropriate.
     */
    if (fFlags & RTPATH_STR_F_NO_START)
    {
        /* We ignore leading slashes when RTPATH_STR_F_NO_START is specified:  */
        for (size_t i = 0; i < cPaths; i++)
        {
            const char *psz = papszPaths[i];
            papszPaths[i] = RTPATH_IS_SLASH(*psz) ? RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(psz) : psz;
        }
        pszPath0EndLastComp = pszPath0 = papszPaths[0];
    }
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    else if (RTPATH_IS_SLASH(pszPath0[0]))
    {
        /* UNC requires a little bit of special magic to make sure we have
           exactly two slashes in each path and don't mix things up. */
        char ch;
        if (   RTPATH_IS_SLASH(pszPath0[1])
            && (ch = pszPath0[2]) != '\0'
            && !RTPATH_IS_SLASH(ch))
        {
            pszPath0 += 2;
            for (size_t i = 1; i < cPaths; i++)
            {
                const char *psz = papszPaths[i];
                if (   RTPATH_IS_SLASH(psz[0])
                    && RTPATH_IS_SLASH(psz[1])
                    && (ch = psz[2]) != '\0'
                    && !RTPATH_IS_SLASH(ch))
                    papszPaths[i] = psz + 2;
                else
                    return 0;
            }
        }
        else
        {
            for (size_t i = 1; i < cPaths; i++)
            {
                const char *psz = papszPaths[i];
                if (   RTPATH_IS_SLASH(psz[0])
                    && RTPATH_IS_SLASH(psz[1])
                    && (ch = psz[2]) != '\0'
                    && !RTPATH_IS_SLASH(ch))
                    return 0;
                if (!RTPATH_IS_SLASH(psz[0]))
                    return 0;
                papszPaths[i] = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(psz);
            }
            pszPath0EndLastComp = pszPath0 = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(pszPath0);
        }
    }
    /* Skip past the drive letter if there is one, as that eliminates the need
       to handle ':' in the main loop below. */
    else if (   RT_C_IS_ALPHA(pszPath0[0])
             && pszPath0[1] == ':')
    {
        /* Drive letter part first: */
        char const chDrv = RT_C_TO_UPPER(pszPath0[0]);
        pszPath0 += 2;
        pszPath0EndLastComp = pszPath0;

        for (size_t i = 1; i < cPaths; i++)
        {
            const char *psz = papszPaths[i];
            if (   (   psz[0] != chDrv
                    && RT_C_TO_UPPER(psz[0]) != chDrv)
                || psz[1] != ':')
                return 0;
            papszPaths[i] = psz + 2;
        }

        /* Subsequent slashes or lack thereof. */
        if (RTPATH_IS_SLASH(*pszPath0))
        {
            for (size_t i = 1; i < cPaths; i++)
            {
                const char *psz = papszPaths[i];
                if (!RTPATH_IS_SLASH(*psz))
                    return pszPath0EndLastComp - pszPath0Start;
                papszPaths[i] = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(psz);
            }
            pszPath0EndLastComp = pszPath0 = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(pszPath0);
        }
        else
            for (size_t i = 1; i < cPaths; i++)
                if (RTPATH_IS_SLASH(*papszPaths[i]))
                    return pszPath0EndLastComp - pszPath0Start;
    }
#endif

    /*
     * Main compare loop.
     */
    for (;;)
    {
        RTUNICP uc0;
        int rc = RTStrGetCpEx(&pszPath0, &uc0);
        AssertRCReturn(rc, 0);
        if (!RTPATH_IS_SLASH(uc0))
        {
            if (uc0 != 0)
            {
                for (size_t i = 1; i < cPaths; i++)
                {
                    RTUNICP uc;
                    rc = RTStrGetCpEx(&papszPaths[i], &uc);
                    AssertRCReturn(rc, 0);
                    if (uc == uc0)
                    { /* likely */}
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
                    else if (   RTUniCpToUpper(uc) == RTUniCpToUpper(uc0)
                             || RTUniCpToLower(uc) == RTUniCpToLower(uc0))
                    { /* less likely */}
#endif
                    else
                        return pszPath0EndLastComp - pszPath0Start;
                }
            }
            else
            {
                /* pszPath0 is at an end.  Check the state of the others as we must
                   return the whole pszPath0 length if their are also at the end of
                   at a slash. */
                for (size_t i = 1; i < cPaths; i++)
                {
                    char ch = *papszPaths[i];
                    if (   ch != '\0'
                        && !RTPATH_IS_SLASH(ch))
                        return pszPath0EndLastComp - pszPath0Start;
                }
                return pszPath0 - 1 - pszPath0Start;
            }
        }
        else
        {
            /* pszPath0 is at a slash.  Check whether all the other are too or are at
               the end of the string.  If any other string ends here, we can return
               the length up to but not including the slash. */
            bool fDone = false;
            for (size_t i = 1; i < cPaths; i++)
            {
                char ch = *papszPaths[i];
                if (ch == '\0')
                    fDone = true;
                else if (RTPATH_IS_SLASH(ch))
                    papszPaths[i] = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(papszPaths[i]);
                else
                    return pszPath0EndLastComp - pszPath0Start;
            }
            if (fDone)
                return pszPath0 - pszPath0Start;
            pszPath0EndLastComp = pszPath0 = RTPATH_STYLE_FN(rtPathHlpSkipSlashes)(pszPath0 - 1);
        }
    }
}

