/* $Id: RTUtf16PrintHexBytes.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTUtf16PrintHexBytes.
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
#include <iprt/utf16.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTUtf16PrintHexBytes(PRTUTF16 pwszBuf, size_t cwcBuf, void const *pv, size_t cb, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~RTSTRPRINTHEXBYTES_F_UPPER), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pwszBuf, VERR_INVALID_POINTER);
    AssertReturn(cb * 2 >= cb, VERR_BUFFER_OVERFLOW);
    AssertReturn(cwcBuf >= cb * 2 + 1, VERR_BUFFER_OVERFLOW);
    if (cb)
        AssertPtrReturn(pv, VERR_INVALID_POINTER);

    static char const s_szHexDigitsLower[17] = "0123456789abcdef";
    static char const s_szHexDigitsUpper[17] = "0123456789ABCDEF";
    const char *pszHexDigits = !(fFlags & RTSTRPRINTHEXBYTES_F_UPPER) ? s_szHexDigitsLower : s_szHexDigitsUpper;

    uint8_t const *pb = (uint8_t const *)pv;
    while (cb-- > 0)
    {
        uint8_t b = *pb++;
        *pwszBuf++ = pszHexDigits[b >> 4];
        *pwszBuf++ = pszHexDigits[b & 0xf];
    }
    *pwszBuf = '\0';
    return VINF_SUCCESS;
}

