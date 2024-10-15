/* $Id: strpbrk.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - strpbrk().
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


/**
 * Find the first occurrence of a character in pszChars in pszStr.
 *
 * @returns
 */
#ifdef IPRT_NO_CRT
# undef strpbrk
char *RT_NOCRT(strpbrk)(const char *pszStr, const char *pszBreakChars)
#elif defined(_MSC_VER)
# if _MSC_VER >= 1400
_CRTIMP __checkReturn _CONST_RETURN char *  __cdecl strpbrk(__in_z const char *pszStr, __in_z const char *pszBreakChars)
# else
_CRTIMP char * __cdecl strpbrk(const char *pszStr, const char *pszBreakChars)
# endif
#elif defined(__WATCOMC__)
_WCRTLINK char *std::strpbrk(const char *pszStr, const char *pszBreakChars)
#else
char *strpbrk(const char *pszStr, const char *pszBreakChars)
# if defined(__THROW) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    __THROW
# endif
#endif
{
    int chCur;
    while ((chCur = *pszStr++) != '\0')
    {
        int chBreak;
        const char *psz = pszBreakChars;
        while ((chBreak = *psz++) != '\0')
            if (chBreak == chCur)
                return (char *)(pszStr - 1);

    }
    return NULL;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(strpbrk);

