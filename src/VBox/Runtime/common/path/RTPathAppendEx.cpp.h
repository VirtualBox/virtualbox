/* $Id: RTPathAppendEx.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtPathAppendEx - Code Template.
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


/**
 * Figures the length of the root part of the path.
 *
 * @returns length of the root specifier.
 * @retval  0 if none.
 *
 * @param   pszPath         The path to investigate.
 *
 * @remarks Unnecessary root slashes will not be counted. The caller will have
 *          to deal with it where it matters.  (Unlike rtPathRootSpecLen which
 *          counts them.)
 */
DECLINLINE(size_t) RTPATH_STYLE_FN(rtPathRootSpecLen2)(const char *pszPath)
{
    /* fend of wildlife. */
    if (!pszPath)
        return 0;

    /* Root slash? */
    if (RTPATH_IS_SLASH(pszPath[0]))
    {
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
        /* UNC? */
        if (    RTPATH_IS_SLASH(pszPath[1])
            &&  pszPath[2] != '\0'
            &&  !RTPATH_IS_SLASH(pszPath[2]))
        {
            /* Find the end of the server name. */
            const char *pszEnd = pszPath + 2;
            pszEnd += 2;
            while (   *pszEnd != '\0'
                   && !RTPATH_IS_SLASH(*pszEnd))
                pszEnd++;
            if (RTPATH_IS_SLASH(*pszEnd))
            {
                pszEnd++;
                while (RTPATH_IS_SLASH(*pszEnd))
                    pszEnd++;

                /* Find the end of the share name */
                while (   *pszEnd != '\0'
                       && !RTPATH_IS_SLASH(*pszEnd))
                    pszEnd++;
                if (RTPATH_IS_SLASH(*pszEnd))
                    pszEnd++;
                return pszPath - pszEnd;
            }
        }
#endif
        return 1;
    }

#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    /* Drive specifier? */
    if (   pszPath[0] != '\0'
        && pszPath[1] == ':'
        && RT_C_IS_ALPHA(pszPath[0]))
    {
        if (RTPATH_IS_SLASH(pszPath[2]))
            return 3;
        return 2;
    }
#endif
    return 0;
}


/** Internal worker for RTPathAppendEx. */
DECLINLINE(int) RTPATH_STYLE_FN(rtPathAppendEx)(char *pszPath, size_t cbPathDst, char *pszPathEnd,
                                                const char *pszAppend, size_t cchAppend)
{
    /*
     * Balance slashes and check for buffer overflow.
     */
    if (!RTPATH_IS_SLASH(pszPathEnd[-1]))
    {
        if (!RTPATH_IS_SLASH(pszAppend[0]))
        {
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
            if (    (size_t)(pszPathEnd - pszPath) == 2
                &&  pszPath[1] == ':'
                &&  RT_C_IS_ALPHA(pszPath[0]))
            {
                if ((size_t)(pszPathEnd - pszPath) + cchAppend >= cbPathDst)
                    return VERR_BUFFER_OVERFLOW;
            }
            else
#endif
            {
                if ((size_t)(pszPathEnd - pszPath) + 1 + cchAppend >= cbPathDst)
                    return VERR_BUFFER_OVERFLOW;
                *pszPathEnd++ = RTPATH_SLASH;
            }
        }
        else
        {
            /* One slash is sufficient at this point. */
            while (cchAppend > 1 && RTPATH_IS_SLASH(pszAppend[1]))
                pszAppend++, cchAppend--;

            if ((size_t)(pszPathEnd - pszPath) + cchAppend >= cbPathDst)
                return VERR_BUFFER_OVERFLOW;
        }
    }
    else
    {
        /* No slashes needed in the appended bit. */
        while (cchAppend && RTPATH_IS_SLASH(*pszAppend))
            pszAppend++, cchAppend--;

        /* In the leading path we can skip unnecessary trailing slashes, but
           be sure to leave one. */
        size_t const cchRoot = RTPATH_STYLE_FN(rtPathRootSpecLen2)(pszPath);
        while (     (size_t)(pszPathEnd - pszPath) > RT_MAX(1, cchRoot)
               &&   RTPATH_IS_SLASH(pszPathEnd[-2]))
            pszPathEnd--;

        if ((size_t)(pszPathEnd - pszPath) + cchAppend >= cbPathDst)
            return VERR_BUFFER_OVERFLOW;
    }

    /*
     * What remains now is the just the copying.
     */
    memcpy(pszPathEnd, pszAppend, cchAppend);
    pszPathEnd[cchAppend] = '\0';
    return VINF_SUCCESS;
}

