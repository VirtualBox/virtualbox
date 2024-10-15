/* $Id: RTStrPrintHexBytes.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTStrPrintHexBytes.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/string.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTStrPrintHexBytes(char *pszBuf, size_t cbBuf, void const *pv, size_t cb, uint32_t fFlags)
{
    AssertReturn(   !(fFlags & ~(RTSTRPRINTHEXBYTES_F_UPPER | RTSTRPRINTHEXBYTES_F_SEP_SPACE | RTSTRPRINTHEXBYTES_F_SEP_COLON))
                 &&    (fFlags & (RTSTRPRINTHEXBYTES_F_SEP_SPACE | RTSTRPRINTHEXBYTES_F_SEP_COLON))
                    != (RTSTRPRINTHEXBYTES_F_SEP_SPACE | RTSTRPRINTHEXBYTES_F_SEP_COLON),
                 VERR_INVALID_FLAGS);
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cb * 2 >= cb, VERR_BUFFER_OVERFLOW);
    char const chSep = fFlags & RTSTRPRINTHEXBYTES_F_SEP_SPACE ? ' '
                     : fFlags & RTSTRPRINTHEXBYTES_F_SEP_COLON ? ':' : '\0';
    AssertReturn(cbBuf >= cb * (2 + (chSep != '\0')) - (chSep != '\0') + 1, VERR_BUFFER_OVERFLOW);
    if (cb)
        AssertPtrReturn(pv, VERR_INVALID_POINTER);

    static char const s_szHexDigitsLower[17] = "0123456789abcdef";
    static char const s_szHexDigitsUpper[17] = "0123456789ABCDEF";
    const char *pszHexDigits = !(fFlags & RTSTRPRINTHEXBYTES_F_UPPER) ? s_szHexDigitsLower : s_szHexDigitsUpper;

    uint8_t const *pb = (uint8_t const *)pv;

    if (!chSep)
    {
        while (cb-- > 0)
        {
            uint8_t b = *pb++;
            *pszBuf++ = pszHexDigits[b >> 4];
            *pszBuf++ = pszHexDigits[b & 0xf];
        }
    }
    else if (cb-- > 0)
    {
        uint8_t b = *pb++;
        *pszBuf++ = pszHexDigits[b >> 4];
        *pszBuf++ = pszHexDigits[b & 0xf];

        while (cb-- > 0)
        {
            b = *pb++;
            *pszBuf++ = chSep;
            *pszBuf++ = pszHexDigits[b >> 4];
            *pszBuf++ = pszHexDigits[b & 0xf];
        }
    }

    *pszBuf = '\0';
    return VINF_SUCCESS;
}

