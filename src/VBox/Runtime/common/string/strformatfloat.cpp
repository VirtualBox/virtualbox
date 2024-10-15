/* $Id: strformatfloat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Formatter, Floating Point Numbers (simple approach).
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

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/string.h"


/**
 * Helper for rtStrFormatR80Worker that copies out the resulting string.
 */
static ssize_t rtStrFormatCopyOutStr(char *pszBuf, size_t cbBuf, const char *pszSrc, size_t cchSrc)
{
    if (cchSrc < cbBuf)
    {
        memcpy(pszBuf, pszSrc, cchSrc);
        pszBuf[cchSrc] = '\0';
        return cchSrc;
    }
    if (cbBuf)
    {
        memcpy(pszBuf, pszSrc, cbBuf - 1);
        pszBuf[cbBuf - 1] = '\0';
    }
    return VERR_BUFFER_OVERFLOW;
}


RTDECL(ssize_t) RTStrFormatR32(char *pszBuf, size_t cbBuf, PCRTFLOAT32U pr32Value, signed int cchWidth,
                               signed int cchPrecision, uint32_t fFlags)
{
    RT_NOREF(cchWidth, cchPrecision);

    /*
     * Handle some special values that does require any value annotating.
     */
    bool const fSign = pr32Value->s.fSign;
    if (RTFLOAT32U_IS_ZERO(pr32Value))
        return fSign
             ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-0"))
             : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+0"));
    if (RTFLOAT32U_IS_INF(pr32Value))
        return fSign
             ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-Inf"))
             : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+Inf"));

    /*
     * Output sign first.
     */
    char  szTmp[80];
    char *pszTmp = szTmp;
    if (fSign)
        *pszTmp++ = '-';
    else
        *pszTmp++ = '+';

    /*
     * Normal?
     */
    uint16_t const uExponent = pr32Value->s.uExponent;
    uint32_t const uFraction = pr32Value->s.uFraction;
    if (RTFLOAT32U_IS_NORMAL(pr32Value))
    {
        *pszTmp++ = '1';
        *pszTmp++ = 'm';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT32U_FRACTION_BITS + 3) / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_32BIT);

        *pszTmp++ = '^';
        pszTmp += RTStrFormatNumber(pszTmp, (int32_t)uExponent - RTFLOAT32U_EXP_BIAS, 10, 0, 0,
                                    RTSTR_F_ZEROPAD | RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
    }
    /*
     * Subnormal?
     */
    else if (RTFLOAT32U_IS_SUBNORMAL(pr32Value))
    {
        *pszTmp++ = '0';
        *pszTmp++ = 'm';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT32U_FRACTION_BITS + 3) / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_32BIT);
        if (fFlags & RTSTR_F_SPECIAL)
            pszTmp = (char *)memcpy(pszTmp, "[SubN]", 6) + 6;
    }
    /*
     * NaN.
     */
    else
    {
        Assert(RTFLOAT32U_IS_NAN(pr32Value));
        if (!(fFlags & RTSTR_F_SPECIAL))
            return rtStrFormatCopyOutStr(pszBuf, cbBuf,
                                         RTFLOAT32U_IS_SIGNALLING_NAN(pr32Value)
                                         ? (fSign ? "-SNan[" : "+SNan[") : fSign ? "-QNan[" : "+QNan[", 5);
        *pszTmp++ = RTFLOAT32U_IS_SIGNALLING_NAN(pr32Value) ? 'S' : 'Q';
        *pszTmp++ = 'N';
        *pszTmp++ = 'a';
        *pszTmp++ = 'N';
        *pszTmp++ = '[';
        *pszTmp++ = '.';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT32U_FRACTION_BITS + 3) / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_32BIT);
        *pszTmp++ = ']';
    }
    return rtStrFormatCopyOutStr(pszBuf, cbBuf, szTmp, pszTmp - &szTmp[0]);
}




RTDECL(ssize_t) RTStrFormatR64(char *pszBuf, size_t cbBuf, PCRTFLOAT64U pr64Value, signed int cchWidth,
                               signed int cchPrecision, uint32_t fFlags)
{
    RT_NOREF(cchWidth, cchPrecision);

    /*
     * Handle some special values that does require any value annotating.
     */
    bool const fSign = pr64Value->s.fSign;
    if (RTFLOAT64U_IS_ZERO(pr64Value))
        return fSign
             ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-0"))
             : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+0"));
    if (RTFLOAT64U_IS_INF(pr64Value))
        return fSign
             ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-Inf"))
             : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+Inf"));

    /*
     * Output sign first.
     */
    char  szTmp[160];
    char *pszTmp = szTmp;
    if (fSign)
        *pszTmp++ = '-';
    else
        *pszTmp++ = '+';

    /*
     * Normal?
     */
    uint16_t const uExponent = pr64Value->s.uExponent;
    uint64_t const uFraction = RT_MAKE_U64(pr64Value->s.uFractionLow, pr64Value->s.uFractionHigh);
    if (RTFLOAT64U_IS_NORMAL(pr64Value))
    {
        *pszTmp++ = '1';
        *pszTmp++ = 'm';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT64U_FRACTION_BITS + 3) / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);

        *pszTmp++ = '^';
        pszTmp += RTStrFormatNumber(pszTmp, (int32_t)uExponent - RTFLOAT64U_EXP_BIAS, 10, 0, 0,
                                    RTSTR_F_ZEROPAD | RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
    }
    /*
     * Subnormal?
     */
    else if (RTFLOAT64U_IS_SUBNORMAL(pr64Value))
    {
        *pszTmp++ = '0';
        *pszTmp++ = 'm';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT64U_FRACTION_BITS + 3) / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
        if (fFlags & RTSTR_F_SPECIAL)
            pszTmp = (char *)memcpy(pszTmp, "[SubN]", 6) + 6;
    }
    /*
     * NaN.
     */
    else
    {
        Assert(RTFLOAT64U_IS_NAN(pr64Value));
        if (!(fFlags & RTSTR_F_SPECIAL))
            return rtStrFormatCopyOutStr(pszBuf, cbBuf,
                                         RTFLOAT64U_IS_SIGNALLING_NAN(pr64Value)
                                         ? (fSign ? "-SNan[" : "+SNan[") : fSign ? "-QNan[" : "+QNan[", 5);
        *pszTmp++ = RTFLOAT64U_IS_SIGNALLING_NAN(pr64Value) ? 'S' : 'Q';
        *pszTmp++ = 'N';
        *pszTmp++ = 'a';
        *pszTmp++ = 'N';
        *pszTmp++ = '[';
        *pszTmp++ = '.';
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + RTFLOAT64U_FRACTION_BITS / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
        *pszTmp++ = ']';
    }
    return rtStrFormatCopyOutStr(pszBuf, cbBuf, szTmp, pszTmp - &szTmp[0]);
}


/**
 * Common worker for RTStrFormatR80 and RTStrFormatR80u2.
 */
static ssize_t rtStrFormatR80Worker(char *pszBuf, size_t cbBuf, bool const fSign, bool const fInteger,
                                    uint64_t const uFraction, uint16_t uExponent, uint32_t fFlags)
{
    char szTmp[160];

    /*
     * Output sign first.
     */
    char *pszTmp = szTmp;
    if (fSign)
        *pszTmp++ = '-';
    else
        *pszTmp++ = '+';

    /*
     * Then check for special numbers (indicated by expontent).
     */
    bool fDenormal = false;
    if (uExponent == 0)
    {
        /* Zero? */
        if (   !uFraction
            && !fInteger)
            return fSign
                 ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-0"))
                 : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+0"));
        fDenormal = true;
        uExponent = 1;
    }
    else if (uExponent == RTFLOAT80U_EXP_MAX)
    {
        if (!fInteger)
        {
            if (!uFraction)
                return fSign
                     ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-PseudoInf"))
                     : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+PseudoInf"));
            if (!(fFlags & RTSTR_F_SPECIAL))
                return fSign
                     ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-PseudoNan"))
                     : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+PseudoNan"));
            pszTmp = (char *)memcpy(pszTmp, "PseudoNan[", 10) + 10;
        }
        else if (!(uFraction & RT_BIT_64(62)))
        {
            if (!(uFraction & (RT_BIT_64(62) - 1)))
                return fSign
                     ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-Inf"))
                     : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+Inf"));
            if (!(fFlags & RTSTR_F_SPECIAL))
                return rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("SNan"));
            pszTmp = (char *)memcpy(pszTmp, "SNan[", 5) + 5;
        }
        else
        {
            if (!(uFraction & (RT_BIT_64(62) - 1)))
                return fSign
                     ? rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("-Ind"))
                     : rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("+Ind"));
            if (!(fFlags & RTSTR_F_SPECIAL))
                return rtStrFormatCopyOutStr(pszBuf, cbBuf, RT_STR_TUPLE("QNan"));
            pszTmp = (char *)memcpy(pszTmp, "QNan[", 5) + 5;
        }
        pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + RTFLOAT80U_FRACTION_BITS / 4, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
        *pszTmp++ = ']';
        return rtStrFormatCopyOutStr(pszBuf, cbBuf, szTmp, pszTmp - &szTmp[0]);
    }

    /*
     * Format the mantissa and exponent.
     */
    *pszTmp++ = fInteger ? '1' : '0';
    *pszTmp++ = 'm';
    pszTmp += RTStrFormatNumber(pszTmp, uFraction, 16, 2 + (RTFLOAT80U_FRACTION_BITS + 3) / 4, 0,
                                RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);

    *pszTmp++ = '^';
    pszTmp += RTStrFormatNumber(pszTmp, (int32_t)uExponent - RTFLOAT80U_EXP_BIAS, 10, 0, 0,
                                RTSTR_F_ZEROPAD | RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
    if (fFlags & RTSTR_F_SPECIAL)
    {
        if (fDenormal)
        {
            if (fInteger)
                pszTmp = (char *)memcpy(pszTmp, "[PDn]", 5) + 5;
            else
                pszTmp = (char *)memcpy(pszTmp, "[Den]", 5) + 5;
        }
        else if (!fInteger)
            pszTmp = (char *)memcpy(pszTmp, "[Unn]", 5) + 5;
    }
    return rtStrFormatCopyOutStr(pszBuf, cbBuf, szTmp, pszTmp - &szTmp[0]);
}


RTDECL(ssize_t) RTStrFormatR80u2(char *pszBuf, size_t cbBuf, PCRTFLOAT80U2 pr80Value, signed int cchWidth,
                                 signed int cchPrecision, uint32_t fFlags)
{
    RT_NOREF(cchWidth, cchPrecision);
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
    return rtStrFormatR80Worker(pszBuf, cbBuf, pr80Value->sj64.fSign, pr80Value->sj64.fInteger,
                                pr80Value->sj64.uFraction, pr80Value->sj64.uExponent, fFlags);
#else
    return rtStrFormatR80Worker(pszBuf, cbBuf, pr80Value->sj.fSign, pr80Value->sj.fInteger,
                                RT_MAKE_U64(pr80Value->sj.u32FractionLow, pr80Value->sj.u31FractionHigh),
                                pr80Value->sj.uExponent, fFlags);
#endif
}


RTDECL(ssize_t) RTStrFormatR80(char *pszBuf, size_t cbBuf, PCRTFLOAT80U pr80Value, signed int cchWidth,
                               signed int cchPrecision, uint32_t fFlags)
{
    RT_NOREF(cchWidth, cchPrecision);
    return rtStrFormatR80Worker(pszBuf, cbBuf, pr80Value->s.fSign, pr80Value->s.uMantissa >> 63,
                                pr80Value->s.uMantissa & (RT_BIT_64(63) - 1), pr80Value->s.uExponent, fFlags);
}

