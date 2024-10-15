/* $Id: strncmp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CRT Strings, strncmp().
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
#include <iprt/string.h>

#ifdef IPRT_NO_CRT
# undef strncmp
int RT_NOCRT(strncmp)(const char *pszStr1, const char *pszStr2, size_t cb)
#elif defined( _MSC_VER)
_CRTIMP int __cdecl strncmp(const char *pszStr1, const char *pszStr2, size_t cb)
#elif defined(__WATCOMC__) && !defined(IPRT_NO_CRT)
_WCRTLINK int std::strncmp(const char *pszStr1, const char *pszStr2, size_t cb)
#else
int strncmp(const char *pszStr1, const char *pszStr2, size_t cb)
# if defined(__THROW) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    __THROW
# endif
#endif
{
    while (cb-- > 0)
    {
        char const ch1   = *pszStr1++;
        int  const iDiff = ch1 - *pszStr2++;
        if (iDiff)
            return iDiff;
        if (!ch1)
            break;
    }
    return 0;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(strncmp);

