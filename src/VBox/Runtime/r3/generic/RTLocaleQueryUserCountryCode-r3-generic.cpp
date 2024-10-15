/* $Id: RTLocaleQueryUserCountryCode-r3-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTLocaleQueryLocaleName, ring-3 generic.
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
#include <locale.h>

#include <iprt/locale.h>
#include "internal/iprt.h"

#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>



RTDECL(int) RTLocaleQueryUserCountryCode(char pszCountryCode[3])
{
    static const int s_aiLocales[] =
    {
        LC_ALL,
        LC_CTYPE,
        LC_COLLATE,
        LC_MONETARY,
        LC_NUMERIC,
        LC_TIME
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aiLocales); i++)
    {
        const char *pszLocale = setlocale(s_aiLocales[i], NULL);
        if (   pszLocale
            && strlen(pszLocale) >= 5
            && RT_C_IS_ALPHA(pszLocale[0])
            && RT_C_IS_ALPHA(pszLocale[1])
            && pszLocale[2] == '_'
            && RT_C_IS_ALPHA(pszLocale[3])
            && RT_C_IS_ALPHA(pszLocale[4]))
        {
            pszCountryCode[0] = RT_C_TO_UPPER(pszLocale[3]);
            pszCountryCode[1] = RT_C_TO_UPPER(pszLocale[4]);
            pszCountryCode[2] = '\0';
            return VINF_SUCCESS;
        }
    }

    pszCountryCode[0] = 'Z';
    pszCountryCode[1] = 'Z';
    pszCountryCode[2] = '\0';
    return VERR_NOT_AVAILABLE;
}

