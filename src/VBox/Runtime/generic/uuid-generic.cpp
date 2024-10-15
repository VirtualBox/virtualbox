/* $Id: uuid-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UUID, Generic.
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
#include <iprt/uuid.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Conversion table used by the conversion functions.
 * 0xff if not a hex number, otherwise the value. */
static const uint8_t g_au8Digits[256] =
{
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 0..0f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 10..1f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 20..2f */
    0x00,0x01,0x02,0x03, 0x04,0x05,0x06,0x07,  0x08,0x09,0xff,0xff, 0xff,0xff,0xff,0xff, /* 30..3f */
    0xff,0x0a,0x0b,0x0c, 0x0d,0x0e,0x0f,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 40..4f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 50..5f */
    0xff,0x0a,0x0b,0x0c, 0x0d,0x0e,0x0f,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 60..6f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 70..7f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 80..8f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* 90..9f */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* a0..af */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* b0..bf */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* c0..cf */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* d0..df */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* e0..ef */
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,  0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, /* f0..ff */
};
/** Conversion to string. */
static const char g_achDigits[17] = "0123456789abcdef";



/* WARNING: This implementation ASSUMES little endian. Does not work on big endian! */

/* Remember, the time fields in the UUID must be little endian. */


RTDECL(int)  RTUuidClear(PRTUUID pUuid)
{
    AssertPtrReturn(pUuid, VERR_INVALID_PARAMETER);
    pUuid->au64[0] = 0;
    pUuid->au64[1] = 0;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUuidClear);


RTDECL(bool)  RTUuidIsNull(PCRTUUID pUuid)
{
    AssertPtrReturn(pUuid, true);
    return !pUuid->au64[0]
        && !pUuid->au64[1];
}
RT_EXPORT_SYMBOL(RTUuidIsNull);


RTDECL(int)  RTUuidCompare(PCRTUUID pUuid1, PCRTUUID pUuid2)
{
    /*
     * Special cases.
     */
    if (pUuid1 == pUuid2)
        return 0;
    if (!pUuid1)
        return RTUuidIsNull(pUuid2) ? 0 : -1;
    if (!pUuid2)
        return RTUuidIsNull(pUuid1) ? 0 : 1;
    AssertPtrReturn(pUuid1, -1);
    AssertPtrReturn(pUuid2, 1);

    /*
     * Standard cases.
     */
    if (pUuid1->Gen.u32TimeLow != pUuid2->Gen.u32TimeLow)
        return pUuid1->Gen.u32TimeLow < pUuid2->Gen.u32TimeLow ? -1 : 1;
    if (pUuid1->Gen.u16TimeMid != pUuid2->Gen.u16TimeMid)
        return pUuid1->Gen.u16TimeMid < pUuid2->Gen.u16TimeMid ? -1 : 1;
    if (pUuid1->Gen.u16TimeHiAndVersion != pUuid2->Gen.u16TimeHiAndVersion)
        return pUuid1->Gen.u16TimeHiAndVersion < pUuid2->Gen.u16TimeHiAndVersion ? -1 : 1;
    if (pUuid1->Gen.u8ClockSeqHiAndReserved != pUuid2->Gen.u8ClockSeqHiAndReserved)
        return pUuid1->Gen.u8ClockSeqHiAndReserved < pUuid2->Gen.u8ClockSeqHiAndReserved ? -1 : 1;
    if (pUuid1->Gen.u8ClockSeqLow != pUuid2->Gen.u8ClockSeqLow)
        return pUuid1->Gen.u8ClockSeqLow < pUuid2->Gen.u8ClockSeqLow ? -1 : 1;
    if (pUuid1->Gen.au8Node[0] != pUuid2->Gen.au8Node[0])
        return pUuid1->Gen.au8Node[0] < pUuid2->Gen.au8Node[0] ? -1 : 1;
    if (pUuid1->Gen.au8Node[1] != pUuid2->Gen.au8Node[1])
        return pUuid1->Gen.au8Node[1] < pUuid2->Gen.au8Node[1] ? -1 : 1;
    if (pUuid1->Gen.au8Node[2] != pUuid2->Gen.au8Node[2])
        return pUuid1->Gen.au8Node[2] < pUuid2->Gen.au8Node[2] ? -1 : 1;
    if (pUuid1->Gen.au8Node[3] != pUuid2->Gen.au8Node[3])
        return pUuid1->Gen.au8Node[3] < pUuid2->Gen.au8Node[3] ? -1 : 1;
    if (pUuid1->Gen.au8Node[4] != pUuid2->Gen.au8Node[4])
        return pUuid1->Gen.au8Node[4] < pUuid2->Gen.au8Node[4] ? -1 : 1;
    if (pUuid1->Gen.au8Node[5] != pUuid2->Gen.au8Node[5])
        return pUuid1->Gen.au8Node[5] < pUuid2->Gen.au8Node[5] ? -1 : 1;
    return 0;
}
RT_EXPORT_SYMBOL(RTUuidCompare);


RTDECL(int)  RTUuidCompareStr(PCRTUUID pUuid1, const char *pszString2)
{
    RTUUID Uuid2;
    int rc;

    /* check params */
    AssertPtrReturn(pUuid1, -1);
    AssertPtrReturn(pszString2, 1);

    /*
     * Try convert the string to a UUID and then compare the two.
     */
    rc = RTUuidFromStr(&Uuid2, pszString2);
    AssertRCReturn(rc, 1);

    return RTUuidCompare(pUuid1, &Uuid2);
}
RT_EXPORT_SYMBOL(RTUuidCompareStr);


RTDECL(int)  RTUuidCompare2Strs(const char *pszString1, const char *pszString2)
{
    RTUUID Uuid1;
    RTUUID Uuid2;
    int rc;

    /* check params */
    AssertPtrReturn(pszString1, -1);
    AssertPtrReturn(pszString2, 1);

    /*
     * Try convert the strings to UUIDs and then compare them.
     */
    rc = RTUuidFromStr(&Uuid1, pszString1);
    AssertRCReturn(rc, -1);

    rc = RTUuidFromStr(&Uuid2, pszString2);
    AssertRCReturn(rc, 1);

    return RTUuidCompare(&Uuid1, &Uuid2);
}
RT_EXPORT_SYMBOL(RTUuidCompare2Strs);


RTDECL(int)  RTUuidToStr(PCRTUUID pUuid, char *pszString, size_t cchString)
{
    uint32_t u32TimeLow;
    unsigned u;

    /* validate parameters */
    AssertPtrReturn(pUuid, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszString, VERR_INVALID_PARAMETER);
    AssertReturn(cchString >= RTUUID_STR_LENGTH, VERR_INVALID_PARAMETER);

    /*
     * RTStrPrintf(,,"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
     *             pUuid->Gen.u32TimeLow,
     *             pUuid->Gen.u16TimeMin,
     *             pUuid->Gen.u16TimeHiAndVersion,
     *             pUuid->Gen.u16ClockSeq & 0xff,
     *             pUuid->Gen.u16ClockSeq >> 8,
     *             pUuid->Gen.au8Node[0],
     *             pUuid->Gen.au8Node[1],
     *             pUuid->Gen.au8Node[2],
     *             pUuid->Gen.au8Node[3],
     *             pUuid->Gen.au8Node[4],
     *             pUuid->Gen.au8Node[5]);
     */
    u32TimeLow = RT_H2LE_U32(pUuid->Gen.u32TimeLow);
    pszString[ 0] = g_achDigits[(u32TimeLow >> 28)/*& 0xf*/];
    pszString[ 1] = g_achDigits[(u32TimeLow >> 24) & 0xf];
    pszString[ 2] = g_achDigits[(u32TimeLow >> 20) & 0xf];
    pszString[ 3] = g_achDigits[(u32TimeLow >> 16) & 0xf];
    pszString[ 4] = g_achDigits[(u32TimeLow >> 12) & 0xf];
    pszString[ 5] = g_achDigits[(u32TimeLow >>  8) & 0xf];
    pszString[ 6] = g_achDigits[(u32TimeLow >>  4) & 0xf];
    pszString[ 7] = g_achDigits[(u32TimeLow/*>>0*/)& 0xf];
    pszString[ 8] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeMid);
    pszString[ 9] = g_achDigits[(u >> 12)/*& 0xf*/];
    pszString[10] = g_achDigits[(u >>  8) & 0xf];
    pszString[11] = g_achDigits[(u >>  4) & 0xf];
    pszString[12] = g_achDigits[(u/*>>0*/)& 0xf];
    pszString[13] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeHiAndVersion);
    pszString[14] = g_achDigits[(u >> 12)/*& 0xf*/];
    pszString[15] = g_achDigits[(u >>  8) & 0xf];
    pszString[16] = g_achDigits[(u >>  4) & 0xf];
    pszString[17] = g_achDigits[(u/*>>0*/)& 0xf];
    pszString[18] = '-';
    pszString[19] = g_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved >> 4];
    pszString[20] = g_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved & 0xf];
    pszString[21] = g_achDigits[pUuid->Gen.u8ClockSeqLow >> 4];
    pszString[22] = g_achDigits[pUuid->Gen.u8ClockSeqLow & 0xf];
    pszString[23] = '-';
    pszString[24] = g_achDigits[pUuid->Gen.au8Node[0] >> 4];
    pszString[25] = g_achDigits[pUuid->Gen.au8Node[0] & 0xf];
    pszString[26] = g_achDigits[pUuid->Gen.au8Node[1] >> 4];
    pszString[27] = g_achDigits[pUuid->Gen.au8Node[1] & 0xf];
    pszString[28] = g_achDigits[pUuid->Gen.au8Node[2] >> 4];
    pszString[29] = g_achDigits[pUuid->Gen.au8Node[2] & 0xf];
    pszString[30] = g_achDigits[pUuid->Gen.au8Node[3] >> 4];
    pszString[31] = g_achDigits[pUuid->Gen.au8Node[3] & 0xf];
    pszString[32] = g_achDigits[pUuid->Gen.au8Node[4] >> 4];
    pszString[33] = g_achDigits[pUuid->Gen.au8Node[4] & 0xf];
    pszString[34] = g_achDigits[pUuid->Gen.au8Node[5] >> 4];
    pszString[35] = g_achDigits[pUuid->Gen.au8Node[5] & 0xf];
    pszString[36] = '\0';

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUuidToStr);


RTDECL(int)  RTUuidFromStr(PRTUUID pUuid, const char *pszString)
{
    bool fHaveBraces;

    /*
     * Validate parameters.
     */
    AssertPtrReturn(pUuid, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszString, VERR_INVALID_PARAMETER);

    fHaveBraces = pszString[0] == '{';
    pszString += fHaveBraces;

#define MY_CHECK(expr) do { if (RT_UNLIKELY(!(expr))) return VERR_INVALID_UUID_FORMAT; } while (0)
#define MY_ISXDIGIT(ch) (g_au8Digits[(ch) & 0xff] != 0xff)
    MY_CHECK(MY_ISXDIGIT(pszString[ 0]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 1]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 2]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 3]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 4]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 5]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 6]));
    MY_CHECK(MY_ISXDIGIT(pszString[ 7]));
    MY_CHECK(pszString[ 8] == '-');
    MY_CHECK(MY_ISXDIGIT(pszString[ 9]));
    MY_CHECK(MY_ISXDIGIT(pszString[10]));
    MY_CHECK(MY_ISXDIGIT(pszString[11]));
    MY_CHECK(MY_ISXDIGIT(pszString[12]));
    MY_CHECK(pszString[13] == '-');
    MY_CHECK(MY_ISXDIGIT(pszString[14]));
    MY_CHECK(MY_ISXDIGIT(pszString[15]));
    MY_CHECK(MY_ISXDIGIT(pszString[16]));
    MY_CHECK(MY_ISXDIGIT(pszString[17]));
    MY_CHECK(pszString[18] == '-');
    MY_CHECK(MY_ISXDIGIT(pszString[19]));
    MY_CHECK(MY_ISXDIGIT(pszString[20]));
    MY_CHECK(MY_ISXDIGIT(pszString[21]));
    MY_CHECK(MY_ISXDIGIT(pszString[22]));
    MY_CHECK(pszString[23] == '-');
    MY_CHECK(MY_ISXDIGIT(pszString[24]));
    MY_CHECK(MY_ISXDIGIT(pszString[25]));
    MY_CHECK(MY_ISXDIGIT(pszString[26]));
    MY_CHECK(MY_ISXDIGIT(pszString[27]));
    MY_CHECK(MY_ISXDIGIT(pszString[28]));
    MY_CHECK(MY_ISXDIGIT(pszString[29]));
    MY_CHECK(MY_ISXDIGIT(pszString[30]));
    MY_CHECK(MY_ISXDIGIT(pszString[31]));
    MY_CHECK(MY_ISXDIGIT(pszString[32]));
    MY_CHECK(MY_ISXDIGIT(pszString[33]));
    MY_CHECK(MY_ISXDIGIT(pszString[34]));
    MY_CHECK(MY_ISXDIGIT(pszString[35]));
    if (fHaveBraces)
        MY_CHECK(pszString[36] == '}');
    MY_CHECK(!pszString[36 + fHaveBraces]);
#undef MY_ISXDIGIT
#undef MY_CHECK

    /*
     * Inverse of RTUuidToStr (see above).
     */
#define MY_TONUM(ch) (g_au8Digits[(ch) & 0xff])
    pUuid->Gen.u32TimeLow = RT_LE2H_U32((uint32_t)MY_TONUM(pszString[ 0]) << 28
                          | (uint32_t)MY_TONUM(pszString[ 1]) << 24
                          | (uint32_t)MY_TONUM(pszString[ 2]) << 20
                          | (uint32_t)MY_TONUM(pszString[ 3]) << 16
                          | (uint32_t)MY_TONUM(pszString[ 4]) << 12
                          | (uint32_t)MY_TONUM(pszString[ 5]) <<  8
                          | (uint32_t)MY_TONUM(pszString[ 6]) <<  4
                          | (uint32_t)MY_TONUM(pszString[ 7]));
    pUuid->Gen.u16TimeMid = RT_LE2H_U16((uint16_t)MY_TONUM(pszString[ 9]) << 12
                          | (uint16_t)MY_TONUM(pszString[10]) << 8
                          | (uint16_t)MY_TONUM(pszString[11]) << 4
                          | (uint16_t)MY_TONUM(pszString[12]));
    pUuid->Gen.u16TimeHiAndVersion = RT_LE2H_U16(
                            (uint16_t)MY_TONUM(pszString[14]) << 12
                          | (uint16_t)MY_TONUM(pszString[15]) << 8
                          | (uint16_t)MY_TONUM(pszString[16]) << 4
                          | (uint16_t)MY_TONUM(pszString[17]));
    pUuid->Gen.u8ClockSeqHiAndReserved =
                            (uint16_t)MY_TONUM(pszString[19]) << 4
                          | (uint16_t)MY_TONUM(pszString[20]);
    pUuid->Gen.u8ClockSeqLow =
                            (uint16_t)MY_TONUM(pszString[21]) << 4
                          | (uint16_t)MY_TONUM(pszString[22]);
    pUuid->Gen.au8Node[0] = (uint8_t)MY_TONUM(pszString[24]) << 4
                          | (uint8_t)MY_TONUM(pszString[25]);
    pUuid->Gen.au8Node[1] = (uint8_t)MY_TONUM(pszString[26]) << 4
                          | (uint8_t)MY_TONUM(pszString[27]);
    pUuid->Gen.au8Node[2] = (uint8_t)MY_TONUM(pszString[28]) << 4
                          | (uint8_t)MY_TONUM(pszString[29]);
    pUuid->Gen.au8Node[3] = (uint8_t)MY_TONUM(pszString[30]) << 4
                          | (uint8_t)MY_TONUM(pszString[31]);
    pUuid->Gen.au8Node[4] = (uint8_t)MY_TONUM(pszString[32]) << 4
                          | (uint8_t)MY_TONUM(pszString[33]);
    pUuid->Gen.au8Node[5] = (uint8_t)MY_TONUM(pszString[34]) << 4
                          | (uint8_t)MY_TONUM(pszString[35]);
#undef MY_TONUM
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUuidFromStr);


RTDECL(int)  RTUuidToUtf16(PCRTUUID pUuid, PRTUTF16 pwszString, size_t cwcString)
{
    uint32_t u32TimeLow;
    unsigned u;

    /* validate parameters */
    AssertPtrReturn(pUuid, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pwszString, VERR_INVALID_PARAMETER);
    AssertReturn(cwcString >= RTUUID_STR_LENGTH, VERR_INVALID_PARAMETER);

    /*
     * RTStrPrintf(,,"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
     *             pUuid->Gen.u32TimeLow,
     *             pUuid->Gen.u16TimeMin,
     *             pUuid->Gen.u16TimeHiAndVersion,
     *             pUuid->Gen.u16ClockSeq & 0xff,
     *             pUuid->Gen.u16ClockSeq >> 8,
     *             pUuid->Gen.au8Node[0],
     *             pUuid->Gen.au8Node[1],
     *             pUuid->Gen.au8Node[2],
     *             pUuid->Gen.au8Node[3],
     *             pUuid->Gen.au8Node[4],
     *             pUuid->Gen.au8Node[5]);
     */
    u32TimeLow = RT_H2LE_U32(pUuid->Gen.u32TimeLow);
    pwszString[ 0] = g_achDigits[(u32TimeLow >> 28)/*& 0xf*/];
    pwszString[ 1] = g_achDigits[(u32TimeLow >> 24) & 0xf];
    pwszString[ 2] = g_achDigits[(u32TimeLow >> 20) & 0xf];
    pwszString[ 3] = g_achDigits[(u32TimeLow >> 16) & 0xf];
    pwszString[ 4] = g_achDigits[(u32TimeLow >> 12) & 0xf];
    pwszString[ 5] = g_achDigits[(u32TimeLow >>  8) & 0xf];
    pwszString[ 6] = g_achDigits[(u32TimeLow >>  4) & 0xf];
    pwszString[ 7] = g_achDigits[(u32TimeLow/*>>0*/)& 0xf];
    pwszString[ 8] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeMid);
    pwszString[ 9] = g_achDigits[(u >> 12)/*& 0xf*/];
    pwszString[10] = g_achDigits[(u >>  8) & 0xf];
    pwszString[11] = g_achDigits[(u >>  4) & 0xf];
    pwszString[12] = g_achDigits[(u/*>>0*/)& 0xf];
    pwszString[13] = '-';
    u = RT_H2LE_U16(pUuid->Gen.u16TimeHiAndVersion);
    pwszString[14] = g_achDigits[(u >> 12)/*& 0xf*/];
    pwszString[15] = g_achDigits[(u >>  8) & 0xf];
    pwszString[16] = g_achDigits[(u >>  4) & 0xf];
    pwszString[17] = g_achDigits[(u/*>>0*/)& 0xf];
    pwszString[18] = '-';
    pwszString[19] = g_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved >> 4];
    pwszString[20] = g_achDigits[pUuid->Gen.u8ClockSeqHiAndReserved & 0xf];
    pwszString[21] = g_achDigits[pUuid->Gen.u8ClockSeqLow >> 4];
    pwszString[22] = g_achDigits[pUuid->Gen.u8ClockSeqLow & 0xf];
    pwszString[23] = '-';
    pwszString[24] = g_achDigits[pUuid->Gen.au8Node[0] >> 4];
    pwszString[25] = g_achDigits[pUuid->Gen.au8Node[0] & 0xf];
    pwszString[26] = g_achDigits[pUuid->Gen.au8Node[1] >> 4];
    pwszString[27] = g_achDigits[pUuid->Gen.au8Node[1] & 0xf];
    pwszString[28] = g_achDigits[pUuid->Gen.au8Node[2] >> 4];
    pwszString[29] = g_achDigits[pUuid->Gen.au8Node[2] & 0xf];
    pwszString[30] = g_achDigits[pUuid->Gen.au8Node[3] >> 4];
    pwszString[31] = g_achDigits[pUuid->Gen.au8Node[3] & 0xf];
    pwszString[32] = g_achDigits[pUuid->Gen.au8Node[4] >> 4];
    pwszString[33] = g_achDigits[pUuid->Gen.au8Node[4] & 0xf];
    pwszString[34] = g_achDigits[pUuid->Gen.au8Node[5] >> 4];
    pwszString[35] = g_achDigits[pUuid->Gen.au8Node[5] & 0xf];
    pwszString[36] = '\0';

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUuidToUtf16);


RTDECL(int)  RTUuidFromUtf16(PRTUUID pUuid, PCRTUTF16 pwszString)
{
    bool fHaveBraces;

    /*
     * Validate parameters.
     */
    AssertPtrReturn(pUuid, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pwszString, VERR_INVALID_PARAMETER);

    fHaveBraces = pwszString[0] == '{';
    pwszString += fHaveBraces;

#define MY_CHECK(expr) do { if (RT_UNLIKELY(!(expr))) return VERR_INVALID_UUID_FORMAT; } while (0)
#define MY_ISXDIGIT(ch) (!((ch) & 0xff00) && g_au8Digits[(ch) & 0xff] != 0xff)
    MY_CHECK(MY_ISXDIGIT(pwszString[ 0]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 1]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 2]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 3]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 4]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 5]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 6]));
    MY_CHECK(MY_ISXDIGIT(pwszString[ 7]));
    MY_CHECK(pwszString[ 8] == '-');
    MY_CHECK(MY_ISXDIGIT(pwszString[ 9]));
    MY_CHECK(MY_ISXDIGIT(pwszString[10]));
    MY_CHECK(MY_ISXDIGIT(pwszString[11]));
    MY_CHECK(MY_ISXDIGIT(pwszString[12]));
    MY_CHECK(pwszString[13] == '-');
    MY_CHECK(MY_ISXDIGIT(pwszString[14]));
    MY_CHECK(MY_ISXDIGIT(pwszString[15]));
    MY_CHECK(MY_ISXDIGIT(pwszString[16]));
    MY_CHECK(MY_ISXDIGIT(pwszString[17]));
    MY_CHECK(pwszString[18] == '-');
    MY_CHECK(MY_ISXDIGIT(pwszString[19]));
    MY_CHECK(MY_ISXDIGIT(pwszString[20]));
    MY_CHECK(MY_ISXDIGIT(pwszString[21]));
    MY_CHECK(MY_ISXDIGIT(pwszString[22]));
    MY_CHECK(pwszString[23] == '-');
    MY_CHECK(MY_ISXDIGIT(pwszString[24]));
    MY_CHECK(MY_ISXDIGIT(pwszString[25]));
    MY_CHECK(MY_ISXDIGIT(pwszString[26]));
    MY_CHECK(MY_ISXDIGIT(pwszString[27]));
    MY_CHECK(MY_ISXDIGIT(pwszString[28]));
    MY_CHECK(MY_ISXDIGIT(pwszString[29]));
    MY_CHECK(MY_ISXDIGIT(pwszString[30]));
    MY_CHECK(MY_ISXDIGIT(pwszString[31]));
    MY_CHECK(MY_ISXDIGIT(pwszString[32]));
    MY_CHECK(MY_ISXDIGIT(pwszString[33]));
    MY_CHECK(MY_ISXDIGIT(pwszString[34]));
    MY_CHECK(MY_ISXDIGIT(pwszString[35]));
    if (fHaveBraces)
        MY_CHECK(pwszString[36] == '}');
    MY_CHECK(!pwszString[36 + fHaveBraces]);
#undef MY_ISXDIGIT
#undef MY_CHECK

    /*
     * Inverse of RTUuidToUtf8 (see above).
     */
#define MY_TONUM(ch) (g_au8Digits[(ch) & 0xff])
    pUuid->Gen.u32TimeLow = RT_LE2H_U32((uint32_t)MY_TONUM(pwszString[ 0]) << 28
                          | (uint32_t)MY_TONUM(pwszString[ 1]) << 24
                          | (uint32_t)MY_TONUM(pwszString[ 2]) << 20
                          | (uint32_t)MY_TONUM(pwszString[ 3]) << 16
                          | (uint32_t)MY_TONUM(pwszString[ 4]) << 12
                          | (uint32_t)MY_TONUM(pwszString[ 5]) <<  8
                          | (uint32_t)MY_TONUM(pwszString[ 6]) <<  4
                          | (uint32_t)MY_TONUM(pwszString[ 7]));
    pUuid->Gen.u16TimeMid = RT_LE2H_U16((uint16_t)MY_TONUM(pwszString[ 9]) << 12
                          | (uint16_t)MY_TONUM(pwszString[10]) << 8
                          | (uint16_t)MY_TONUM(pwszString[11]) << 4
                          | (uint16_t)MY_TONUM(pwszString[12]));
    pUuid->Gen.u16TimeHiAndVersion = RT_LE2H_U16(
                            (uint16_t)MY_TONUM(pwszString[14]) << 12
                          | (uint16_t)MY_TONUM(pwszString[15]) << 8
                          | (uint16_t)MY_TONUM(pwszString[16]) << 4
                          | (uint16_t)MY_TONUM(pwszString[17]));
    pUuid->Gen.u8ClockSeqHiAndReserved =
                            (uint16_t)MY_TONUM(pwszString[19]) << 4
                          | (uint16_t)MY_TONUM(pwszString[20]);
    pUuid->Gen.u8ClockSeqLow =
                            (uint16_t)MY_TONUM(pwszString[21]) << 4
                          | (uint16_t)MY_TONUM(pwszString[22]);
    pUuid->Gen.au8Node[0] = (uint8_t)MY_TONUM(pwszString[24]) << 4
                          | (uint8_t)MY_TONUM(pwszString[25]);
    pUuid->Gen.au8Node[1] = (uint8_t)MY_TONUM(pwszString[26]) << 4
                          | (uint8_t)MY_TONUM(pwszString[27]);
    pUuid->Gen.au8Node[2] = (uint8_t)MY_TONUM(pwszString[28]) << 4
                          | (uint8_t)MY_TONUM(pwszString[29]);
    pUuid->Gen.au8Node[3] = (uint8_t)MY_TONUM(pwszString[30]) << 4
                          | (uint8_t)MY_TONUM(pwszString[31]);
    pUuid->Gen.au8Node[4] = (uint8_t)MY_TONUM(pwszString[32]) << 4
                          | (uint8_t)MY_TONUM(pwszString[33]);
    pUuid->Gen.au8Node[5] = (uint8_t)MY_TONUM(pwszString[34]) << 4
                          | (uint8_t)MY_TONUM(pwszString[35]);
#undef MY_TONUM
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUuidFromUtf16);

