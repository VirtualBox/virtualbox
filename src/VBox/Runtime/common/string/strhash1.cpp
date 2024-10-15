/* $Id: strhash1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Hashing by Algorithm \#1.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#include "internal/strhash.h"


RTDECL(uint32_t)    RTStrHash1(const char *pszString)
{
    size_t cchIgnored;
    return sdbm(pszString, &cchIgnored);
}


RTDECL(uint32_t)    RTStrHash1N(const char *pszString, size_t cchString)
{
    size_t cchIgnored;
    return sdbmN(pszString, cchString, &cchIgnored);
}


RTDECL(uint32_t)    RTStrHash1ExN(size_t cPairs, ...)
{
    va_list va;
    va_start(va, cPairs);
    uint32_t uHash = RTStrHash1ExNV(cPairs, va);
    va_end(va);
    return uHash;
}


RTDECL(uint32_t)    RTStrHash1ExNV(size_t cPairs, va_list va)
{
    uint32_t uHash = 0;
    for (uint32_t i = 0; i < cPairs; i++)
    {
        const char *psz = va_arg(va, const char *);
        size_t      cch = va_arg(va, size_t);
        uHash += sdbmIncN(psz, cch, uHash);
    }
    return uHash;
}

