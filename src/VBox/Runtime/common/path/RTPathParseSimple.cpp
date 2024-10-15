/* $Id: RTPathParseSimple.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathParseSimple
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

#include <iprt/assert.h>
#include <iprt/ctype.h>


RTDECL(size_t) RTPathParseSimple(const char *pszPath, size_t *pcchDir, ssize_t *poffName, ssize_t *poffSuff)
{
    /*
     * First deal with the root as it is always more fun that you'd think.
     */
    const char *psz     = pszPath;
    size_t      cchRoot = 0;

#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    if (RT_C_IS_ALPHA(*psz) && RTPATH_IS_VOLSEP(psz[1]))
    {
        /* Volume specifier. */
        cchRoot = 2;
        psz    += 2;
    }
    else if (RTPATH_IS_SLASH(*psz) && RTPATH_IS_SLASH(psz[1]))
    {
        /* UNC - there are exactly two prefix slashes followed by a namespace
           or computer name, which can be empty on windows.  */
        cchRoot = 2;
        psz += 2;
        while (!RTPATH_IS_SLASH(*psz) && *psz)
        {
            cchRoot++;
            psz++;
        }
    }
#endif
    while (RTPATH_IS_SLASH(*psz))
    {
        cchRoot++;
        psz++;
    }

    /*
     * Do the remainder.
     */
    const char *pszName = psz;
    const char *pszLastDot = NULL;
    for (;; psz++)
    {
        switch (*psz)
        {
            default:
                break;

            /* handle separators. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
            case '\\':
#endif
            case '/':
                pszName = psz + 1;
                pszLastDot = NULL;
                break;

            case '.':
                pszLastDot = psz;
                break;

            /*
             * The end. Complete the results.
             */
            case '\0':
            {
                ssize_t offName = *pszName != '\0' ? pszName - pszPath : -1;
                if (poffName)
                    *poffName = offName;

                if (poffSuff)
                {
                    ssize_t offSuff = -1;
                    if (   pszLastDot
                        && pszLastDot != pszName
                        && pszLastDot[1] != '\0')
                    {
                        offSuff = pszLastDot - pszPath;
                        Assert(offSuff > offName);
                    }
                    *poffSuff = offSuff;
                }

                if (pcchDir)
                {
                    size_t cch = offName < 0 ? psz - pszPath : offName - 1 < (ssize_t)cchRoot ? cchRoot : offName - 1;
                    while (cch > cchRoot && RTPATH_IS_SLASH(pszPath[cch - 1]))
                        cch--;
                    *pcchDir = cch;
                }

                return psz - pszPath;
            }
        }
    }

    /* will never get here */
}

