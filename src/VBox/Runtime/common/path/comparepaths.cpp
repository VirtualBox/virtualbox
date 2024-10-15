/* $Id: comparepaths.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Comparison.
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
#include "internal/iprt.h"
#include <iprt/path.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/uni.h>


/**
 * Helper for RTPathCompare() and RTPathStartsWith().
 *
 * @returns similar to strcmp.
 * @param   pszPath1        Path to compare.
 * @param   pszPath2        Path to compare.
 * @param   fLimit          Limit the comparison to the length of \a pszPath2
 * @internal
 */
static int rtPathCompare(const char *pszPath1, const char *pszPath2, bool fLimit)
{
    if (pszPath1 == pszPath2)
        return 0;
    if (!pszPath1)
        return -1;
    if (!pszPath2)
        return 1;

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    for (;;)
    {
        RTUNICP uc1;
        int rc = RTStrGetCpEx(&pszPath1, &uc1);
        if (RT_SUCCESS(rc))
        {
            RTUNICP uc2;
            rc = RTStrGetCpEx(&pszPath2, &uc2);
            if (RT_SUCCESS(rc))
            {
                if (uc1 == uc2)
                {
                    if (uc1)
                    { /* likely */ }
                    else
                        return 0;
                }
                else
                {
                    if (uc1 == '\\')
                        uc1 = '/';
                    else
                        uc1 = RTUniCpToUpper(uc1);
                    if (uc2 == '\\')
                        uc2 = '/';
                    else
                        uc2 = RTUniCpToUpper(uc2);
                    if (uc1 != uc2)
                    {
                        if (fLimit && uc2 == '\0')
                            return 0;
                        return uc1 > uc2 ? 1 : -1; /* (overflow/underflow paranoia) */
                    }
                }
            }
            else
                return 1;
        }
        else
            return -1;
    }
#else
    if (!fLimit)
        return strcmp(pszPath1, pszPath2);
    return strncmp(pszPath1, pszPath2, strlen(pszPath2));
#endif
}


RTDECL(int) RTPathCompare(const char *pszPath1, const char *pszPath2)
{
    return rtPathCompare(pszPath1, pszPath2, false /* full path lengths */);
}


RTDECL(bool) RTPathStartsWith(const char *pszPath, const char *pszParentPath)
{
    if (pszPath == pszParentPath)
        return true;
    if (!pszPath || !pszParentPath)
        return false;

    if (rtPathCompare(pszPath, pszParentPath, true /* limited by path 2 */) != 0)
        return false;

    const size_t cchParentPath = strlen(pszParentPath);
    if (RTPATH_IS_SLASH(pszPath[cchParentPath]))
        return true;
    if (pszPath[cchParentPath] == '\0')
        return true;

    /* Deal with pszParentPath = root (or having a trailing slash). */
    if (   cchParentPath > 0
        && RTPATH_IS_SLASH(pszParentPath[cchParentPath - 1])
        && RTPATH_IS_SLASH(pszPath[cchParentPath - 1]))
        return true;

    return false;
}

