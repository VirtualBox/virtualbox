/* $Id: strformatnum.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Formatter, Single Numbers.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_STRING
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include "internal/string.h"


RTDECL(ssize_t) RTStrFormatU8(char *pszBuf, size_t cbBuf, uint8_t u8Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_8BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u8Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u8Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet < cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU16(char *pszBuf, size_t cbBuf, uint16_t u16Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_16BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u16Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u16Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet < cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU32(char *pszBuf, size_t cbBuf, uint32_t u32Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_32BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u32Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u32Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet < cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU64(char *pszBuf, size_t cbBuf, uint64_t u64Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_64BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u64Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u64Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet < cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU128(char *pszBuf, size_t cbBuf, PCRTUINT128U pu128, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu128->QWords.qw1, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu128->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}


RTDECL(ssize_t) RTStrFormatU256(char *pszBuf, size_t cbBuf, PCRTUINT256U pu256, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu256->QWords.qw3, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw2, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw1, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}


RTDECL(ssize_t) RTStrFormatU512(char *pszBuf, size_t cbBuf, PCRTUINT512U pu512, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32 + 32+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu512->QWords.qw7, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw6, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw5, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw4, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw3, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw2, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw1, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}

