/* $Id: strchr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CRT Strings, strchr().
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
# undef strchr
char *RT_NOCRT(strchr)(const char *pszStr, int ch)
#elif defined(_MSC_VER)
_CRTIMP char * __cdecl strchr(const char *pszStr, int ch)
#elif defined(__WATCOMC__) && !defined(IPRT_NO_CRT)
_WCRTLINK char *std::strchr(const char *pszStr, int ch)
#else
char *strchr(const char *pszStr, int ch)
# if defined(__THROW) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    __THROW
# endif
#endif
{
    char chCur;
    while ((chCur = *pszStr) != '\0')
        if (chCur != ch)
            pszStr++;
        else
            return (char *)pszStr;
    return NULL;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(strchr);

