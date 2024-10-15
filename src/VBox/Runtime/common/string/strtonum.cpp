/* $Id: strtonum.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String To Number Conversion.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h> /* needed for RT_C_IS_DIGIT */
#include <iprt/err.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern const unsigned char g_auchDigits[256]; /* shared with strtofloat.cpp - add header? */

/** 8-bit char -> digit.
 * Non-digits have values 255 (most), 254 (zero), 253 (colon), 252 (space), 251 (dot).
 *
 * @note Also used by strtofloat.cpp
 */
const unsigned char g_auchDigits[256] =
{
    254,255,255,255,255,255,255,255,255,252,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    252,255,255,255,255,255,255,255,255,255,255,255,255,255,251,255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,253,255,255,255,255,255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

#define DIGITS_ZERO_TERM 254
#define DIGITS_COLON     253
#define DIGITS_SPACE     252
#define DIGITS_DOT       251

/** Approximated overflow shift checks. */
static const char g_auchShift[36] =
{
  /*  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35 */
     64, 64, 63, 63, 62, 62, 62, 62, 61, 61, 61, 61, 61, 61, 61, 61, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 59, 59, 59, 59
};

/*
#include <stdio.h>
int main()
{
    int i;
    printf("static const unsigned char g_auchDigits[256] =\n"
           "{");
    for (i = 0; i < 256; i++)
    {
        int ch = 255;
        if (i >= '0' && i <= '9')
            ch = i - '0';
        else if (i >= 'a' && i <= 'z')
            ch = i - 'a' + 10;
        else if (i >= 'A' && i <= 'Z')
            ch = i - 'A' + 10;
        else if (i == 0)
            ch = 254;
        else if (i == ':')
            ch = 253;
        else if (i == ' ' || i == '\t')
            ch = 252;
        else if (i == '.')
            ch = 251;
        if (i == 0)
            printf("\n    %3d", ch);
        else if ((i % 32) == 0)
            printf(",\n    %3d", ch);
        else
            printf(",%3d", ch);
    }
    printf("\n"
           "};\n");
    return 0;
}
*/


RTDECL(int) RTStrToUInt64Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, uint64_t *pu64)
{
    const char   *psz = pszValue;
    int           iShift;
    int           rc;
    uint64_t      u64;
    unsigned char uch;
    bool          fPositive;

    /*
     * Split the base and length limit (latter is chiefly for sscanf).
     */
    unsigned uBase  = uBaseAndMaxLen & 0xff;
    unsigned cchMax = uBaseAndMaxLen >> 8;
    if (cchMax == 0)
        cchMax = ~0U;
    AssertStmt(uBase < RT_ELEMENTS(g_auchShift), uBase = 0);

    /*
     * Positive/Negative stuff.
     */
    fPositive = true;
    while (cchMax > 0)
    {
        if (*psz == '+')
            fPositive = true;
        else if (*psz == '-')
            fPositive = !fPositive;
        else
            break;
        psz++;
        cchMax--;
    }

    /*
     * Check for hex prefix.
     */
    if (!uBase)
    {
        uBase = 10;
        if (psz[0] == '0')
        {
            if (   psz[0] == '0'
                && cchMax > 1
                && (psz[1] == 'x' || psz[1] == 'X')
                && g_auchDigits[(unsigned char)psz[2]] < 16)
            {
                uBase     = 16;
                psz      += 2;
                cchMax   -= 2;
            }
            else if (   psz[0] == '0'
                     && g_auchDigits[(unsigned char)psz[1]] < 8)
                uBase = 8; /* don't skip the zero, in case it's alone. */
        }
    }
    else if (   uBase == 16
             && psz[0] == '0'
             && cchMax > 1
             && (psz[1] == 'x' || psz[1] == 'X')
             && g_auchDigits[(unsigned char)psz[2]] < 16)
    {
        cchMax -= 2;
        psz    += 2;
    }

    /*
     * Interpret the value.
     * Note: We only support ascii digits at this time... :-)
     */
    pszValue = psz; /* (Prefix and sign doesn't count in the digit counting.) */
    iShift   = g_auchShift[uBase];
    rc       = VINF_SUCCESS;
    u64      = 0;
    while (cchMax > 0 && (uch = (unsigned char)*psz) != 0)
    {
        unsigned char chDigit = g_auchDigits[uch];
        uint64_t u64Prev;

        if (chDigit >= uBase)
            break;

        u64Prev = u64;
        u64    *= uBase;
        u64    += chDigit;
        if (u64Prev > u64 || (u64Prev >> iShift))
            rc = VWRN_NUMBER_TOO_BIG;
        psz++;
        cchMax--;
    }

    if (!fPositive)
    {
        if (rc == VINF_SUCCESS)
            rc = VWRN_NEGATIVE_UNSIGNED;
        u64 = -(int64_t)u64;
    }

    if (pu64)
        *pu64 = u64;

    if (psz == pszValue)
        rc = VERR_NO_DIGITS;

    if (ppszNext)
        *ppszNext = (char *)psz;

    /*
     * Warn about trailing chars/spaces.
     */
    if (   rc == VINF_SUCCESS
        && *psz
        && cchMax > 0)
    {
        while (cchMax > 0 && (*psz == ' ' || *psz == '\t'))
            psz++, cchMax--;
        rc = cchMax > 0 && *psz ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt64Ex);


RTDECL(int) RTStrToUInt64Full(const char *pszValue, unsigned uBaseAndMaxLen, uint64_t *pu64)
{
    char *psz;
    int rc = RTStrToUInt64Ex(pszValue, &psz, uBaseAndMaxLen, pu64);
    if (RT_SUCCESS(rc) && *psz)
    {
        if (rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES)
            rc = -rc;
        else if (rc != VINF_SUCCESS)
        {
            unsigned cchMax = uBaseAndMaxLen >> 8;
            if (!cchMax)
                cchMax  = ~0U;
            else
                cchMax -= (unsigned)(psz - pszValue);
            if (cchMax > 0)
            {
                while (cchMax > 0 && (*psz == ' ' || *psz == '\t'))
                    psz++, cchMax--;
                rc = cchMax > 0 && *psz ? VERR_TRAILING_CHARS : VERR_TRAILING_SPACES;
            }
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt64Full);


RTDECL(uint64_t) RTStrToUInt64(const char *pszValue)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, NULL, 0, &u64);
    if (RT_SUCCESS(rc))
        return u64;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt64);


RTDECL(int) RTStrToUInt32Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, uint32_t *pu32)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu32)
        *pu32 = (uint32_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt32Ex);


RTDECL(int) RTStrToUInt32Full(const char *pszValue, unsigned uBaseAndMaxLen, uint32_t *pu32)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu32)
        *pu32 = (uint32_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt32Full);


RTDECL(uint32_t) RTStrToUInt32(const char *pszValue)
{
    uint32_t u32;
    int rc = RTStrToUInt32Ex(pszValue, NULL, 0, &u32);
    if (RT_SUCCESS(rc))
        return u32;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt32);


RTDECL(int) RTStrToUInt16Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, uint16_t *pu16)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu16)
        *pu16 = (uint16_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt16Ex);


RTDECL(int) RTStrToUInt16Full(const char *pszValue, unsigned uBaseAndMaxLen, uint16_t *pu16)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu16)
        *pu16 = (uint16_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt16Full);


RTDECL(uint16_t) RTStrToUInt16(const char *pszValue)
{
    uint16_t u16;
    int rc = RTStrToUInt16Ex(pszValue, NULL, 0, &u16);
    if (RT_SUCCESS(rc))
        return u16;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt16);


RTDECL(int) RTStrToUInt8Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, uint8_t *pu8)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu8)
        *pu8 = (uint8_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt8Ex);


RTDECL(int) RTStrToUInt8Full(const char *pszValue, unsigned uBaseAndMaxLen, uint8_t *pu8)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBaseAndMaxLen, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu8)
        *pu8 = (uint8_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt8Full);


RTDECL(uint8_t) RTStrToUInt8(const char *pszValue)
{
    uint8_t u8;
    int rc = RTStrToUInt8Ex(pszValue, NULL, 0, &u8);
    if (RT_SUCCESS(rc))
        return u8;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt8);


RTDECL(int) RTStrToInt64Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, int64_t *pi64)
{
    const char   *psz = pszValue;
    int           iShift;
    int           rc;
    uint64_t      u64;
    unsigned char uch;
    bool          fPositive;

    /*
     * Split the base and length limit (latter is chiefly for sscanf).
     */
    unsigned uBase  = uBaseAndMaxLen & 0xff;
    unsigned cchMax = uBaseAndMaxLen >> 8;
    if (cchMax == 0)
        cchMax = ~0U;
    AssertStmt(uBase < RT_ELEMENTS(g_auchShift), uBase = 0);

    /*
     * Positive/Negative stuff.
     */
    fPositive = true;
    while (cchMax > 0)
    {
        if (*psz == '+')
            fPositive = true;
        else if (*psz == '-')
            fPositive = !fPositive;
        else
            break;
        psz++;
        cchMax--;
    }

    /*
     * Check for hex prefix.
     */
    if (!uBase)
    {
        uBase = 10;
        if (psz[0] == '0')
        {
            if (   psz[0] == '0'
                && cchMax > 1
                && (psz[1] == 'x' || psz[1] == 'X')
                && g_auchDigits[(unsigned char)psz[2]] < 16)
            {
                uBase     = 16;
                psz      += 2;
                cchMax   -= 2;
            }
            else if (   psz[0] == '0'
                     && g_auchDigits[(unsigned char)psz[1]] < 8)
                uBase = 8; /* don't skip the zero, in case it's alone. */
        }
    }
    else if (   uBase == 16
             && psz[0] == '0'
             && cchMax > 1
             && (psz[1] == 'x' || psz[1] == 'X')
             && g_auchDigits[(unsigned char)psz[2]] < 16)
    {
        cchMax -= 2;
        psz    += 2;
    }

    /*
     * Interpret the value.
     * Note: We only support ascii digits at this time... :-)
     */
    pszValue = psz; /* (Prefix and sign doesn't count in the digit counting.) */
    iShift   = g_auchShift[uBase];
    rc       = VINF_SUCCESS;
    u64      = 0;
    while (cchMax > 0 && (uch = (unsigned char)*psz) != 0)
    {
        unsigned char chDigit = g_auchDigits[uch];
        uint64_t u64Prev;

        if (chDigit >= uBase)
            break;

        u64Prev = u64;
        u64    *= uBase;
        u64    += chDigit;
        if (u64Prev > u64 || (u64Prev >> iShift))
            rc = VWRN_NUMBER_TOO_BIG;
        psz++;
        cchMax--;
    }

    /* Mixing pi64 assigning and overflow checks is to pacify a tstRTCRest-1
       asan overflow warning.  */
    if (!(u64 & RT_BIT_64(63)))
    {
        if (psz == pszValue)
            rc = VERR_NO_DIGITS;
        if (pi64)
            *pi64 = fPositive ? u64 : -(int64_t)u64;
    }
    else if (!fPositive && u64 == RT_BIT_64(63))
    {
        if (pi64)
            *pi64 = INT64_MIN;
    }
    else
    {
        rc = VWRN_NUMBER_TOO_BIG;
        if (pi64)
            *pi64 = fPositive ? u64 : -(int64_t)u64;
    }

    if (ppszNext)
        *ppszNext = (char *)psz;

    /*
     * Warn about trailing chars/spaces.
     */
    if (   rc == VINF_SUCCESS
        && cchMax > 0
        && *psz)
    {
        while (cchMax > 0 && (*psz == ' ' || *psz == '\t'))
            psz++, cchMax--;
        rc = cchMax > 0 && *psz ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt64Ex);


RTDECL(int) RTStrToInt64Full(const char *pszValue, unsigned uBaseAndMaxLen, int64_t *pi64)
{
    char *psz;
    int rc = RTStrToInt64Ex(pszValue, &psz, uBaseAndMaxLen, pi64);
    if (RT_SUCCESS(rc) && *psz)
    {
        if (rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES)
            rc = -rc;
        else if (rc != VINF_SUCCESS)
        {
            unsigned cchMax = uBaseAndMaxLen >> 8;
            if (!cchMax)
                cchMax  = ~0U;
            else
                cchMax -= (unsigned)(psz - pszValue);
            if (cchMax > 0)
            {
                while (cchMax > 0 && (*psz == ' ' || *psz == '\t'))
                    psz++, cchMax--;
                rc = cchMax > 0 && *psz ? VERR_TRAILING_CHARS : VERR_TRAILING_SPACES;
            }
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt64Full);


RTDECL(int64_t) RTStrToInt64(const char *pszValue)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, NULL, 0, &i64);
    if (RT_SUCCESS(rc))
        return i64;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt64);


RTDECL(int) RTStrToInt32Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, int32_t *pi32)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int32_t i32 = (int32_t)i64;
        if (i64 != (int64_t)i32)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi32)
        *pi32 = (int32_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt32Ex);


RTDECL(int) RTStrToInt32Full(const char *pszValue, unsigned uBaseAndMaxLen, int32_t *pi32)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int32_t i32 = (int32_t)i64;
        if (i64 != (int64_t)i32)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi32)
        *pi32 = (int32_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt32Full);


RTDECL(int32_t) RTStrToInt32(const char *pszValue)
{
    int32_t i32;
    int rc = RTStrToInt32Ex(pszValue, NULL, 0, &i32);
    if (RT_SUCCESS(rc))
        return i32;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt32);


RTDECL(int) RTStrToInt16Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, int16_t *pi16)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int16_t i16 = (int16_t)i64;
        if (i64 != (int64_t)i16)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi16)
        *pi16 = (int16_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt16Ex);


RTDECL(int) RTStrToInt16Full(const char *pszValue, unsigned uBaseAndMaxLen, int16_t *pi16)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int16_t i16 = (int16_t)i64;
        if (i64 != (int64_t)i16)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi16)
        *pi16 = (int16_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt16Full);


RTDECL(int16_t) RTStrToInt16(const char *pszValue)
{
    int16_t i16;
    int rc = RTStrToInt16Ex(pszValue, NULL, 0, &i16);
    if (RT_SUCCESS(rc))
        return i16;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt16);


RTDECL(int) RTStrToInt8Ex(const char *pszValue, char **ppszNext, unsigned uBaseAndMaxLen, int8_t *pi8)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int8_t i8 = (int8_t)i64;
        if (i64 != (int64_t)i8)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi8)
        *pi8 = (int8_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt8Ex);


RTDECL(int) RTStrToInt8Full(const char *pszValue, unsigned uBaseAndMaxLen, int8_t *pi8)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBaseAndMaxLen, &i64);
    if (RT_SUCCESS(rc))
    {
        int8_t i8 = (int8_t)i64;
        if (i64 != (int64_t)i8)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi8)
        *pi8 = (int8_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt8Full);


RTDECL(int8_t) RTStrToInt8(const char *pszValue)
{
    int8_t i8;
    int rc = RTStrToInt8Ex(pszValue, NULL, 0, &i8);
    if (RT_SUCCESS(rc))
        return i8;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt8);


RTDECL(int) RTStrConvertHexBytesEx(char const *pszHex, void *pv, size_t cb, uint32_t fFlags,
                                   const char **ppszNext, size_t *pcbReturned)
{
    size_t               cbDst  = cb;
    uint8_t             *pbDst  = (uint8_t *)pv;
    const unsigned char *pszSrc = (const unsigned char *)pszHex;
    unsigned char        uchDigit;

    if (pcbReturned)
        *pcbReturned = 0;
    if (ppszNext)
        *ppszNext = NULL;
    AssertPtrReturn(pszHex, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTSTRCONVERTHEXBYTES_F_SEP_COLON), VERR_INVALID_FLAGS);

    if (fFlags & RTSTRCONVERTHEXBYTES_F_SEP_COLON)
    {
        /*
         * Optional colon separators.
         */
        bool fPrevColon = true; /* leading colon is taken to mean leading zero byte */
        for (;;)
        {
            /* Pick the next two digit from the string. */
            uchDigit = g_auchDigits[*pszSrc++];
            if (uchDigit >= 16)
            {
                if (uchDigit == 253 /* colon */)
                {
                    Assert(pszSrc[-1] == ':');
                    if (!fPrevColon)
                        fPrevColon = true;
                    /* Add zero byte if there is room. */
                    else if (cbDst > 0)
                    {
                        cbDst--;
                        *pbDst++ = 0;
                    }
                    else
                    {
                        if (pcbReturned)
                            *pcbReturned = pbDst - (uint8_t *)pv;
                        if (ppszNext)
                            *ppszNext = (const char *)pszSrc - 1;
                        return VERR_BUFFER_OVERFLOW;
                    }
                    continue;
                }
                else
                    break;
            }
            else
            {
                /* Got one digit, check what comes next: */
                unsigned char const uchDigit2 = g_auchDigits[*pszSrc++];
                if (uchDigit2 < 16)
                {
                    if (cbDst > 0)
                    {
                        *pbDst++ = (uchDigit << 4) | uchDigit2;
                        cbDst--;
                        fPrevColon = false;
                    }
                    else
                    {
                        if (pcbReturned)
                            *pcbReturned = pbDst - (uint8_t *)pv;
                        if (ppszNext)
                            *ppszNext = (const char *)pszSrc - 1;
                        return VERR_BUFFER_OVERFLOW;
                    }
                }
                /* Lone digits are only allowed if following a colon or at the very start, because
                   if there is more than one byte it ambigious whether it is the lead or tail byte
                   that only has one digit in it.
                   Note! This also ensures better compatibility with the no-separator variant
                         (except for single digit strings, which are accepted here but not below). */
                else if (fPrevColon)
                {
                    if (cbDst > 0)
                    {
                        *pbDst++ = uchDigit;
                        cbDst--;
                    }
                    else
                    {
                        if (pcbReturned)
                            *pcbReturned = pbDst - (uint8_t *)pv;
                        if (ppszNext)
                            *ppszNext = (const char *)pszSrc - 1;
                        return VERR_BUFFER_OVERFLOW;
                    }
                    if (uchDigit2 == 253 /* colon */)
                    {
                        Assert(pszSrc[-1] == ':');
                        fPrevColon = true;
                    }
                    else
                    {
                        fPrevColon = false;
                        uchDigit = uchDigit2;
                        break;
                    }
                }
                else
                {
                    if (pcbReturned)
                        *pcbReturned = pbDst - (uint8_t *)pv;
                    if (ppszNext)
                        *ppszNext = (const char *)pszSrc - 2;
                    return VERR_UNEVEN_INPUT;
                }
            }
        }

        /* Trailing colon means trailing zero byte: */
        if (fPrevColon)
        {
            if (cbDst > 0)
            {
                *pbDst++ = 0;
                cbDst--;
            }
            else
            {
                if (pcbReturned)
                    *pcbReturned = pbDst - (uint8_t *)pv;
                if (ppszNext)
                    *ppszNext = (const char *)pszSrc - 1;
                return VERR_BUFFER_OVERFLOW;
            }
        }
    }
    else
    {
        /*
         * No separators.
         */
        for (;;)
        {
            /* Pick the next two digit from the string. */
            uchDigit = g_auchDigits[*pszSrc++];
            if (uchDigit < 16)
            {
                unsigned char const uchDigit2 = g_auchDigits[*pszSrc++];
                if (uchDigit2 < 16)
                {
                    /* Add the byte to the output buffer. */
                    if (cbDst)
                    {
                        cbDst--;
                        *pbDst++ = (uchDigit << 4) | uchDigit2;
                    }
                    else
                    {
                        if (pcbReturned)
                            *pcbReturned = pbDst - (uint8_t *)pv;
                        if (ppszNext)
                            *ppszNext = (const char *)pszSrc - 2;
                        return VERR_BUFFER_OVERFLOW;
                    }
                }
                else
                {
                    if (pcbReturned)
                        *pcbReturned = pbDst - (uint8_t *)pv;
                    if (ppszNext)
                        *ppszNext = (const char *)pszSrc - 2;
                    return VERR_UNEVEN_INPUT;
                }
            }
            else
                break;
        }
    }

    /*
     * End of hex bytes, look what comes next and figure out what to return.
     */
    if (pcbReturned)
        *pcbReturned = pbDst - (uint8_t *)pv;
    if (ppszNext)
        *ppszNext = (const char *)pszSrc - 1;

    if (uchDigit == 254)
    {
        Assert(pszSrc[-1] == '\0');
        if (cbDst == 0)
            return VINF_SUCCESS;
        return pcbReturned ? VINF_BUFFER_UNDERFLOW : VERR_BUFFER_UNDERFLOW;
    }
    Assert(pszSrc[-1] != '\0');

    if (cbDst != 0 && !pcbReturned)
        return VERR_BUFFER_UNDERFLOW;

    while (uchDigit == 252)
    {
        Assert(pszSrc[-1] == ' ' || pszSrc[-1] == '\t');
        uchDigit = g_auchDigits[*pszSrc++];
    }

    Assert(pszSrc[-1] == '\0' ? uchDigit == 254 : uchDigit != 254);
    return uchDigit == 254 ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;

}
RT_EXPORT_SYMBOL(RTStrConvertHexBytesEx);


RTDECL(int) RTStrConvertHexBytes(char const *pszHex, void *pv, size_t cb, uint32_t fFlags)
{
    return RTStrConvertHexBytesEx(pszHex, pv, cb, fFlags, NULL /*ppszNext*/, NULL /*pcbReturned*/);

}
RT_EXPORT_SYMBOL(RTStrConvertHexBytes);

