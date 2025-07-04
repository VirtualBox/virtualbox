/* $Id: strncpy.cpp 106935 2024-11-10 02:26:41Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - CRT Strings, strncpy().
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
 * Copy a string
 *
 * @returns Pointer to destination string
 * @param   pszDst      Will contain a copy of pszSrc.
 * @param   pszSrc      Zero terminated string.
 * @param   cbDst       Size of the destination buffer.
 */
#ifdef IPRT_NO_CRT
# undef strncpy
char *RT_NOCRT(strncpy)(char *pszDst, const char *pszSrc, size_t cbDst)
#else
char *strncpy(char *pszDst, const char *pszSrc, size_t cbDst)
#endif
{
    size_t off = 0;
    while (off < cbDst)
    {
        char const ch = *pszSrc;
        pszDst[off++] = ch;
        if (ch)
        { /* likely */ }
        else
        {
            /* (this zeroing is not very efficient) */
            while (off < cbDst)
                pszDst[off++] = '\0';
            break;
        }
    }

    return pszDst;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(strncpy);

