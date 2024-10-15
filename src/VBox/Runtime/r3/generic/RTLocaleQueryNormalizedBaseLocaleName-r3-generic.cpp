/* $Id: RTLocaleQueryNormalizedBaseLocaleName-r3-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTLocaleQueryNormalizedBaseLocaleName, ring-3 generic.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/locale.h>
#include "internal/iprt.h"

#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


/*
 * Note! Code duplicated in r3/win/RTLocaleQueryNormalizedBaseLocaleName-win.cpp (adds fallback).
 */
RTDECL(int) RTLocaleQueryNormalizedBaseLocaleName(char *pszName, size_t cbName)
{
    char szLocale[_1K];
    int rc = RTLocaleQueryLocaleName(szLocale, sizeof(szLocale));
    if (RT_SUCCESS(rc))
    {
        /*
         * May return some complicated "LC_XXX=yyy;LC.." sequence if
         * partially set (like IPRT does).  Try get xx_YY sequence first
         * because 'C' or 'POSIX' may be LC_xxx variants that haven't been
         * set yet.
         *
         * ASSUMES complicated locale mangling is done in a certain way...
         */
        const char *pszLocale = strchr(szLocale, '=');
        if (!pszLocale)
            pszLocale = szLocale;
        else
            pszLocale++;
        bool fSeenC = false;
        bool fSeenPOSIX = false;
        do
        {
            const char *pszEnd = strchr(pszLocale, ';');

            if (   RTLOCALE_IS_LANGUAGE2_UNDERSCORE_COUNTRY2(pszLocale)
                && (   pszLocale[5] == '\0'
                    || RT_C_IS_PUNCT(pszLocale[5])) )
                return RTStrCopyEx(pszName, cbName, pszLocale, 5);

            if (   pszLocale[0] == 'C'
                && (   pszLocale[1] == '\0'
                    || RT_C_IS_PUNCT(pszLocale[1])) )
                fSeenC = true;
            else if (   strncmp(pszLocale, "POSIX", 5) == 0
                     && (   pszLocale[5] == '\0'
                        || RT_C_IS_PUNCT(pszLocale[5])) )
                fSeenPOSIX = true;

            /* advance */
            pszLocale = pszEnd ? strchr(pszEnd + 1, '=') : NULL;
        } while (pszLocale++);

        if (fSeenC || fSeenPOSIX)
            return RTStrCopy(pszName, cbName, "C"); /* C and POSIX should be identical IIRC, so keep it simple. */

        rc = VERR_NOT_AVAILABLE;
    }
    return rc;
}

