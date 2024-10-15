/* $Id: asn1-ut-time-decode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, UTC TIME and GENERALIZED TIME Types, Decoding.
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
#include <iprt/asn1.h>

#include <iprt/alloca.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/ctype.h>

#include <iprt/formats/asn1.h>


/**
 * Common code for UTCTime and GeneralizedTime converters that normalizes the
 * converted time and checks that the input values doesn't change.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor to use when reporting an error.
 * @param   pThis               The time to normalize and check.
 * @param   pszType             The type name.
 * @param   pszErrorTag         The error tag.
 */
static int rtAsn1Time_NormalizeTime(PRTASN1CURSOR pCursor, PRTASN1TIME pThis, const char *pszType, const char *pszErrorTag)
{
    int rc;
    if (   pThis->Time.u8Month  >  0
        && pThis->Time.u8Month  <= 12
        && pThis->Time.u8Hour   <  24
        && pThis->Time.u8Minute <  60
        && pThis->Time.u8Second <= 60)
    {
        /* Work around clever rounding error in DER_CFDateToUTCTime() on OS X.  This also
           suppresses any attempt at feeding us leap seconds.  If we pass 60 to the
           normalization code will move on to the next min/hour/day, which is wrong both
           for the OS X issue and for unwanted leap seconds.  Leap seconds are not valid
           ASN.1 by the by according to the specs available to us.  */
        if (pThis->Time.u8Second < 60)
        { /* likely */ }
        else
            pThis->Time.u8Second = 59;

        /* Normalize and move on. */
        RTTIME const TimeCopy = pThis->Time;
        if (RTTimeNormalize(&pThis->Time))
        {
            if (   TimeCopy.u8MonthDay  ==  pThis->Time.u8MonthDay
                && TimeCopy.u8Month     ==  pThis->Time.u8Month
                && TimeCopy.i32Year     ==  pThis->Time.i32Year
                && TimeCopy.u8Hour      ==  pThis->Time.u8Hour
                && TimeCopy.u8Minute    ==  pThis->Time.u8Minute
                && TimeCopy.u8Second    ==  pThis->Time.u8Second)
                return VINF_SUCCESS;

            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_TIME_NORMALIZE_MISMATCH,
                                     "%s: Normalized result not the same as %s: '%.*s' / %04u-%02u-%02uT%02u:%02u:%02u vs %04u-%02u-%02uT%02u:%02u:%02u",
                                     pszErrorTag, pszType, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch,
                                     TimeCopy.i32Year, TimeCopy.u8Month, TimeCopy.u8MonthDay,
                                     TimeCopy.u8Hour, TimeCopy.u8Minute, TimeCopy.u8Second,
                                     pThis->Time.i32Year, pThis->Time.u8Month, pThis->Time.u8MonthDay,
                                     pThis->Time.u8Hour, pThis->Time.u8Minute, pThis->Time.u8Second);
        }
        else
            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_TIME_NORMALIZE_ERROR,
                                     "%s: RTTimeNormalize failed on %s: '%.*s'",
                                     pszErrorTag, pszType, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
    }
    else
        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_TIME_BAD_NORMALIZE_INPUT,
                                 "%s: Bad %s values: '%.*s'; mth=%u h=%u min=%u sec=%u",
                                 pszErrorTag, pszType, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch,
                                 pThis->Time.u8Month, pThis->Time.u8Hour, pThis->Time.u8Minute, pThis->Time.u8Second);
    return rc;
}


/**
 * Converts the UTCTime string into an the RTTIME member of RTASN1TIME.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor to use when reporting an error.
 * @param   pThis               The time to parse.
 * @param   pszErrorTag         The error tag.
 */
static int rtAsn1Time_ConvertUTCTime(PRTASN1CURSOR pCursor, PRTASN1TIME pThis, const char *pszErrorTag)
{
    /*
     * While the current spec says the seconds field is not optional, this
     * restriction was added later on.  So, when parsing UTCTime we must deal
     * with it being absent.
     */
    int rc;
    bool fHaveSeconds = pThis->Asn1Core.cb == sizeof("YYMMDDHHMMSSZ") - 1;
    if (fHaveSeconds || pThis->Asn1Core.cb == sizeof("YYMMDDHHMMZ") - 1)
    {
        const char *pachTime = pThis->Asn1Core.uData.pch;

        /* Basic encoding validation. */
        if (   RT_C_IS_DIGIT(pachTime[0]) /* Y */
            && RT_C_IS_DIGIT(pachTime[1]) /* Y */
            && RT_C_IS_DIGIT(pachTime[2]) /* M */
            && RT_C_IS_DIGIT(pachTime[3]) /* M */
            && RT_C_IS_DIGIT(pachTime[4]) /* D */
            && RT_C_IS_DIGIT(pachTime[5]) /* D */
            && RT_C_IS_DIGIT(pachTime[6]) /* H */
            && RT_C_IS_DIGIT(pachTime[7]) /* H */
            && RT_C_IS_DIGIT(pachTime[8]) /* M */
            && RT_C_IS_DIGIT(pachTime[9]) /* M */
            && (   !fHaveSeconds
                || (   RT_C_IS_DIGIT(pachTime[10]) /* S */
                    && RT_C_IS_DIGIT(pachTime[11]) /* S */ ) )
            && pachTime[fHaveSeconds ? 12 : 10] == 'Z'
           )
        {
            /* Basic conversion. */
            pThis->Time.i32Year         = (pachTime[0] - '0') * 10  +  (pachTime[1] - '0');
            pThis->Time.i32Year        += pThis->Time.i32Year < 50 ? 2000 : 1900;
            pThis->Time.u8Month         = (pachTime[2] - '0') * 10  +  (pachTime[3] - '0');
            pThis->Time.u8WeekDay       = 0;
            pThis->Time.u16YearDay      = 0;
            pThis->Time.u8MonthDay      = (pachTime[4] - '0') * 10  +  (pachTime[5] - '0');
            pThis->Time.u8Hour          = (pachTime[6] - '0') * 10  +  (pachTime[7] - '0');
            pThis->Time.u8Minute        = (pachTime[8] - '0') * 10  +  (pachTime[9] - '0');
            if (fHaveSeconds)
                pThis->Time.u8Second    = (pachTime[10] - '0') * 10 +  (pachTime[11] - '0');
            else
                pThis->Time.u8Second    = 0;
            pThis->Time.u32Nanosecond   = 0;
            pThis->Time.fFlags          = RTTIME_FLAGS_TYPE_UTC;
            pThis->Time.offUTC          = 0;

            /* Check the convered data and normalize the time structure. */
            rc = rtAsn1Time_NormalizeTime(pCursor, pThis, "UTCTime", pszErrorTag);
            if (RT_SUCCESS(rc))
                return rc;
        }
        else
            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_UTC_TIME_ENCODING, "%s: Bad UTCTime encoding: '%.*s'",
                                     pszErrorTag, pThis->Asn1Core.cb, pachTime);
    }
    else
        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_UTC_TIME_ENCODING, "%s: Bad UTCTime length: %#x",
                                 pszErrorTag, pThis->Asn1Core.cb);
    RT_ZERO(*pThis);
    return rc;
}


/**
 * Converts the fraction part of a generalized time into nanoseconds.
 *
 * @returns IPRT status code.
 * @param   pCursor         The cursor to use when reporting an error.
 * @param   pchFraction     Pointer to the start of the fraction (dot).
 * @param   cchFraction     The length of the fraction.
 * @param   pThis           The time object we're working on,
 *                          Time.u32Nanoseconds will be update.
 * @param   pszErrorTag     The error tag.
 */
static int rtAsn1Time_ConvertGeneralizedTimeFraction(PRTASN1CURSOR pCursor, const char *pchFraction, uint32_t cchFraction,
                                                      PRTASN1TIME pThis, const char *pszErrorTag)
{
    pThis->Time.u32Nanosecond = 0;

    /*
     * Check the dot.
     */
    if (*pchFraction != '.')
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                   "%s: Expected GeneralizedTime fraction dot, found: '%c' ('%.*s')",
                                   pszErrorTag, *pchFraction, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
    pchFraction++;
    cchFraction--;
    if (!cchFraction)
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                   "%s: No digit following GeneralizedTime fraction dot: '%.*s'",
                                   pszErrorTag, pThis->Asn1Core.cb, pThis->Asn1Core);

    /*
     * Do the conversion.
     */
    char chLastDigit;
    uint32_t uMult = 100000000;
    do
    {
        char chDigit = chLastDigit = *pchFraction;
        if (!RT_C_IS_DIGIT(chDigit))
            return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                       "%s: Bad GeneralizedTime fraction digit: '%.*s'",
                                       pszErrorTag, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
        pThis->Time.u32Nanosecond += uMult * (uint32_t)(chDigit - '0');

        /* Advance */
        cchFraction--;
        pchFraction++;
        uMult /= 10;
    } while (cchFraction > 0 && uMult > 0);

    /*
     * Lazy bird: For now, we don't permit higher resolution than we can
     * internally represent.  Deal with this if it ever becomes an issue.
     */
    if (cchFraction > 0)
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                   "%s: Bad GeneralizedTime fraction too long: '%.*s'",
                                   pszErrorTag, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
    if (chLastDigit == '0')
        return RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                   "%s: Trailing zeros not allowed for GeneralizedTime: '%.*s'",
                                   pszErrorTag, pThis->Asn1Core.cb, pThis->Asn1Core.uData.pch);
    return VINF_SUCCESS;
}


/**
 * Converts the GeneralizedTime string into an the RTTIME member of RTASN1TIME.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor to use when reporting an error.
 * @param   pThis               The time to parse.
 * @param   pszErrorTag         The error tag.
 */
static int rtAsn1Time_ConvertGeneralizedTime(PRTASN1CURSOR pCursor, PRTASN1TIME pThis, const char *pszErrorTag)
{
    int rc;
    if (pThis->Asn1Core.cb >= sizeof("YYYYMMDDHHMMSSZ") - 1)
    {
        const char *pachTime = pThis->Asn1Core.uData.pch;

        /* Basic encoding validation. */
        if (   RT_C_IS_DIGIT(pachTime[0]) /* Y */
            && RT_C_IS_DIGIT(pachTime[1]) /* Y */
            && RT_C_IS_DIGIT(pachTime[2]) /* Y */
            && RT_C_IS_DIGIT(pachTime[3]) /* Y */
            && RT_C_IS_DIGIT(pachTime[4]) /* M */
            && RT_C_IS_DIGIT(pachTime[5]) /* M */
            && RT_C_IS_DIGIT(pachTime[6]) /* D */
            && RT_C_IS_DIGIT(pachTime[7]) /* D */
            && RT_C_IS_DIGIT(pachTime[8]) /* H */
            && RT_C_IS_DIGIT(pachTime[9]) /* H */
            && RT_C_IS_DIGIT(pachTime[10]) /* M */
            && RT_C_IS_DIGIT(pachTime[11]) /* M */
            && RT_C_IS_DIGIT(pachTime[12]) /* S */ /** @todo was this once optional? */
            && RT_C_IS_DIGIT(pachTime[13]) /* S */
            && pachTime[pThis->Asn1Core.cb - 1] == 'Z'
           )
        {
            /* Basic conversion. */
            pThis->Time.i32Year         = 1000 * (pachTime[0] - '0')
                                        +  100 * (pachTime[1] - '0')
                                        +   10 * (pachTime[2] - '0')
                                        +        (pachTime[3] - '0');
            pThis->Time.u8Month         = (pachTime[4]  - '0') * 10  +  (pachTime[5]  - '0');
            pThis->Time.u8WeekDay       = 0;
            pThis->Time.u16YearDay      = 0;
            pThis->Time.u8MonthDay      = (pachTime[6]  - '0') * 10  +  (pachTime[7]  - '0');
            pThis->Time.u8Hour          = (pachTime[8]  - '0') * 10  +  (pachTime[9]  - '0');
            pThis->Time.u8Minute        = (pachTime[10] - '0') * 10  +  (pachTime[11] - '0');
            pThis->Time.u8Second        = (pachTime[12] - '0') * 10  +  (pachTime[13] - '0');
            pThis->Time.u32Nanosecond   = 0;
            pThis->Time.fFlags          = RTTIME_FLAGS_TYPE_UTC;
            pThis->Time.offUTC          = 0;

            /* Optional fraction part. */
            rc = VINF_SUCCESS;
            uint32_t cchLeft = pThis->Asn1Core.cb - 14 - 1;
            if (cchLeft > 0)
                rc = rtAsn1Time_ConvertGeneralizedTimeFraction(pCursor, pachTime + 14, cchLeft, pThis, pszErrorTag);

            /* Check the convered data and normalize the time structure. */
            if (RT_SUCCESS(rc))
            {
                rc = rtAsn1Time_NormalizeTime(pCursor, pThis, "GeneralizedTime", pszErrorTag);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;
            }
        }
        else
            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                     "%s: Bad GeneralizedTime encoding: '%.*s'",
                                     pszErrorTag, pThis->Asn1Core.cb, pachTime);
    }
    else
        rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING,
                                 "%s: Bad GeneralizedTime length: %#x",
                                 pszErrorTag, pThis->Asn1Core.cb);
    RT_ZERO(*pThis);
    return rc;
}


RTDECL(int) RTAsn1Time_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1TIME pThis, const char *pszErrorTag)
{
    Assert(!(fFlags & RTASN1CURSOR_GET_F_IMPLICIT)); RT_NOREF_PV(fFlags);
    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        if (pThis->Asn1Core.fClass == (ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE) )
        {
            if (pThis->Asn1Core.uTag == ASN1_TAG_UTC_TIME)
            {
                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                pThis->Asn1Core.pOps    = &g_RTAsn1Time_Vtable;
                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                return rtAsn1Time_ConvertUTCTime(pCursor, pThis, pszErrorTag);
            }

            if (pThis->Asn1Core.uTag == ASN1_TAG_GENERALIZED_TIME)
            {
                RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
                pThis->Asn1Core.pOps    = &g_RTAsn1Time_Vtable;
                pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
                return rtAsn1Time_ConvertGeneralizedTime(pCursor, pThis, pszErrorTag);
            }

            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_TAG_MISMATCH, "%s: Not UTCTime nor GeneralizedTime: uTag=%#x",
                                     pszErrorTag, pThis->Asn1Core.uTag);
        }
        else
            rc = RTAsn1CursorSetInfo(pCursor, VERR_ASN1_CURSOR_TAG_FLAG_CLASS_MISMATCH,
                                     "%s: Not UTCTime nor GeneralizedTime: fClass=%#x / uTag=%#x",
                                     pszErrorTag, pThis->Asn1Core.fClass, pThis->Asn1Core.uTag);
    }
    RT_ZERO(*pThis);
    return rc;
}


RTDECL(int) RTAsn1UtcTime_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1TIME pThis, const char *pszErrorTag)
{
    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlags(pCursor, &pThis->Asn1Core, ASN1_TAG_UTC_TIME,
                                            ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                                            fFlags, pszErrorTag, "UTC TIME");
        if (RT_SUCCESS(rc))
        {
            RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
            pThis->Asn1Core.pOps    = &g_RTAsn1Time_Vtable;
            pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
            return rtAsn1Time_ConvertUTCTime(pCursor, pThis, pszErrorTag);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


RTDECL(int) RTAsn1GeneralizedTime_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTASN1TIME pThis, const char *pszErrorTag)
{
    int rc = RTAsn1CursorReadHdr(pCursor, &pThis->Asn1Core, pszErrorTag);
    if (RT_SUCCESS(rc))
    {
        rc = RTAsn1CursorMatchTagClassFlags(pCursor, &pThis->Asn1Core, ASN1_TAG_GENERALIZED_TIME,
                                            ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                                            fFlags, pszErrorTag, "GENERALIZED TIME");
        if (RT_SUCCESS(rc))
        {
            RTAsn1CursorSkip(pCursor, pThis->Asn1Core.cb);
            pThis->Asn1Core.pOps    = &g_RTAsn1Time_Vtable;
            pThis->Asn1Core.fFlags |= RTASN1CORE_F_PRIMITE_TAG_STRUCT;
            return rtAsn1Time_ConvertGeneralizedTime(pCursor, pThis, pszErrorTag);
        }
    }
    RT_ZERO(*pThis);
    return rc;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-time-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-asn1-decoder.h>

