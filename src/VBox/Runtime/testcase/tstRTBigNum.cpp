/* $Id: tstRTBigNum.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase for the RTBigNum* functions.
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
#include <iprt/bignum.h>
#include <iprt/uint256.h>
#include <iprt/uint128.h>
#include <iprt/uint64.h>
#include <iprt/uint32.h>

#include <iprt/err.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#if 1
# include "../include/internal/openssl-pre.h"
# include <openssl/bn.h>
# include "../include/internal/openssl-post.h"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


static uint8_t const g_abLargePositive[] =
{
    0x67,0xcd,0xd6,0x60,0x4e,0xaa,0xe9,0x8e,0x06,0x99,0xde,0xb2,0xf5,0x1c,0xc3,0xfc,
    0xf5,0x17,0x41,0xec,0x42,0x68,0xf0,0xab,0x0e,0xe6,0x79,0xa8,0x32,0x97,0x55,0x00,
    0x49,0x21,0x2b,0x72,0x4b,0x34,0x33,0xe1,0xe2,0xfe,0xa2,0xb8,0x39,0x7a,0x2f,0x17,
    0xae,0x1f,0xbb,0xdb,0x46,0xbc,0x59,0x8b,0x13,0x05,0x28,0x96,0xf6,0xfd,0xc1,0xa4
};
static RTBIGNUM g_LargePositive;
static RTBIGNUM g_LargePositive2; /**< Smaller than g_LargePositive.  */

static uint8_t const g_abLargePositiveMinus1[] =
{
    0x67,0xcd,0xd6,0x60,0x4e,0xaa,0xe9,0x8e,0x06,0x99,0xde,0xb2,0xf5,0x1c,0xc3,0xfc,
    0xf5,0x17,0x41,0xec,0x42,0x68,0xf0,0xab,0x0e,0xe6,0x79,0xa8,0x32,0x97,0x55,0x00,
    0x49,0x21,0x2b,0x72,0x4b,0x34,0x33,0xe1,0xe2,0xfe,0xa2,0xb8,0x39,0x7a,0x2f,0x17,
    0xae,0x1f,0xbb,0xdb,0x46,0xbc,0x59,0x8b,0x13,0x05,0x28,0x96,0xf6,0xfd,0xc1,0xa3
};
static RTBIGNUM g_LargePositiveMinus1; /**< g_LargePositive - 1 */


static uint8_t const g_abLargeNegative[] =
{
    0xf2,0xde,0xbd,0xaf,0x43,0x9e,0x1e,0x88,0xdc,0x64,0x37,0xa9,0xdb,0xb7,0x26,0x31,
    0x92,0x1d,0xf5,0x43,0x4c,0xb0,0x21,0x2b,0x07,0x4e,0xf5,0x94,0x9e,0xce,0x15,0x79,
    0x13,0x0c,0x70,0x68,0x49,0x46,0xcf,0x72,0x2b,0xc5,0x8f,0xab,0x7c,0x88,0x2d,0x1e,
    0x3b,0x43,0x5b,0xdb,0x47,0x45,0x7a,0x25,0x74,0x46,0x1d,0x87,0x24,0xaa,0xab,0x0d,
    0x3e,0xdf,0xd1,0xd8,0x44,0x6f,0x01,0x84,0x01,0x36,0xe0,0x84,0x6e,0x6f,0x41,0xbb,
    0xae,0x1a,0x31,0xef,0x42,0x23,0xfd,0xda,0xda,0x0f,0x7d,0x88,0x8f,0xf5,0x63,0x72,
    0x36,0x9f,0xa9,0xa4,0x4f,0xa0,0xa6,0xb1,0x3b,0xbe,0x0d,0x9d,0x62,0x88,0x98,0x8b
};
static RTBIGNUM g_LargeNegative;
static RTBIGNUM g_LargeNegative2; /**< A few digits less than g_LargeNegative, i.e. larger value.  */

static uint8_t const g_abLargeNegativePluss1[] =
{
    0xf2,0xde,0xbd,0xaf,0x43,0x9e,0x1e,0x88,0xdc,0x64,0x37,0xa9,0xdb,0xb7,0x26,0x31,
    0x92,0x1d,0xf5,0x43,0x4c,0xb0,0x21,0x2b,0x07,0x4e,0xf5,0x94,0x9e,0xce,0x15,0x79,
    0x13,0x0c,0x70,0x68,0x49,0x46,0xcf,0x72,0x2b,0xc5,0x8f,0xab,0x7c,0x88,0x2d,0x1e,
    0x3b,0x43,0x5b,0xdb,0x47,0x45,0x7a,0x25,0x74,0x46,0x1d,0x87,0x24,0xaa,0xab,0x0d,
    0x3e,0xdf,0xd1,0xd8,0x44,0x6f,0x01,0x84,0x01,0x36,0xe0,0x84,0x6e,0x6f,0x41,0xbb,
    0xae,0x1a,0x31,0xef,0x42,0x23,0xfd,0xda,0xda,0x0f,0x7d,0x88,0x8f,0xf5,0x63,0x72,
    0x36,0x9f,0xa9,0xa4,0x4f,0xa0,0xa6,0xb1,0x3b,0xbe,0x0d,0x9d,0x62,0x88,0x98,0x8c
};
static RTBIGNUM g_LargeNegativePluss1; /**< g_LargeNegative + 1 */


static uint8_t const g_ab64BitPositive1[] = { 0x53, 0xe0, 0xdf, 0x11,  0x85, 0x93, 0x06, 0x21 };
static uint64_t g_u64BitPositive1 = UINT64_C(0x53e0df1185930621);
static RTBIGNUM g_64BitPositive1;


static RTBIGNUM g_Zero;
static RTBIGNUM g_One;
static RTBIGNUM g_Two;
static RTBIGNUM g_Three;
static RTBIGNUM g_Four;
static RTBIGNUM g_Five;
static RTBIGNUM g_Ten;
static RTBIGNUM g_FourtyTwo;

static uint8_t const g_abMinus1[] = { 0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff };
//static int64_t  g_iBitMinus1 = -1;
static RTBIGNUM g_Minus1;


/** @name The components of a real PKCS #7 signature (extracted from a build of
 *   this testcase).
 * @{ */
static uint8_t const g_abPubKeyExp[] = { 0x01, 0x00, 0x01 };
static RTBIGNUM      g_PubKeyExp;
static uint8_t const g_abPubKeyMod[] =
{
    0x00, 0xea, 0x61, 0x4e, 0xa0, 0xb2, 0xae, 0x38, 0xbc, 0x43, 0x24, 0x5a, 0x28, 0xc7, 0xa0, 0x69,
    0x82, 0x11, 0xd5, 0x78, 0xe8, 0x6b, 0x41, 0x54, 0x7b, 0x6c, 0x69, 0x13, 0xc8, 0x68, 0x75, 0x0f,
    0xe4, 0x66, 0x54, 0xcd, 0xe3, 0x55, 0x33, 0x3b, 0x7f, 0x9f, 0x55, 0x75, 0x80, 0x6e, 0xd0, 0x8a,
    0xff, 0xc1, 0xf4, 0xbf, 0xfd, 0x70, 0x9b, 0x73, 0x7e, 0xee, 0xf1, 0x80, 0x23, 0xd4, 0xbd, 0xba,
    0xdc, 0xce, 0x09, 0x4a, 0xeb, 0xb0, 0xdd, 0x86, 0x4a, 0x0b, 0x8e, 0x3e, 0x9a, 0x8a, 0x58, 0xed,
    0x98, 0x4f, 0x25, 0xe5, 0x0c, 0x18, 0xd8, 0x10, 0x95, 0xce, 0xe4, 0x19, 0x82, 0x38, 0xcd, 0x76,
    0x6a, 0x38, 0xe5, 0x14, 0xe6, 0x95, 0x0d, 0x80, 0xc5, 0x09, 0x5e, 0x93, 0xf4, 0x6f, 0x82, 0x8e,
    0x9c, 0x81, 0x09, 0xd6, 0xd4, 0xee, 0xd5, 0x1f, 0x94, 0x2d, 0x13, 0x18, 0x9a, 0xbc, 0x88, 0x5d,
    0x9a, 0xe5, 0x66, 0x08, 0x99, 0x93, 0x1b, 0x8a, 0x69, 0x3f, 0x68, 0xb2, 0x97, 0x2a, 0x24, 0xf6,
    0x65, 0x2a, 0x94, 0x33, 0x94, 0x14, 0x5c, 0x6f, 0xff, 0x95, 0xd0, 0x2b, 0xf0, 0x2b, 0xcb, 0x49,
    0xcd, 0x03, 0x3a, 0x45, 0xd5, 0x22, 0x1c, 0xb3, 0xee, 0xd5, 0xaf, 0xb3, 0x5b, 0xcb, 0x1b, 0x35,
    0x4e, 0xff, 0x21, 0x0a, 0x55, 0x1f, 0xa0, 0xf9, 0xdc, 0xad, 0x7a, 0x89, 0x0b, 0x6e, 0x3f, 0x75,
    0xc0, 0x6c, 0x44, 0xff, 0x90, 0x63, 0x79, 0xcf, 0x70, 0x20, 0x60, 0x33, 0x3c, 0xb1, 0xfa, 0x6b,
    0x6c, 0x55, 0x3c, 0xeb, 0x8d, 0x18, 0xe9, 0x0a, 0x81, 0xd5, 0x24, 0xc1, 0x88, 0x7c, 0xa6, 0x8e,
    0xd3, 0x2c, 0x51, 0x1d, 0x6d, 0xdf, 0x51, 0xd5, 0x72, 0x54, 0x7a, 0x98, 0xc0, 0x36, 0x35, 0x21,
    0x66, 0x3c, 0x2f, 0x01, 0xc0, 0x8e, 0xb0, 0x56, 0x60, 0x6e, 0x67, 0x4f, 0x5f, 0xac, 0x05, 0x60,
    0x9b
};
static RTBIGNUM g_PubKeyMod;
static uint8_t const g_abSignature[] =
{
    0x00, 0xae, 0xca, 0x93, 0x47, 0x0b, 0xfa, 0xd8, 0xb9, 0xbb, 0x5a, 0x5e, 0xf6, 0x75, 0x90, 0xed,
    0x80, 0x37, 0x03, 0x6d, 0x23, 0x91, 0x30, 0x0c, 0x9d, 0xbf, 0x34, 0xc1, 0xf9, 0x43, 0xa7, 0xec,
    0xc0, 0x83, 0xc0, 0x98, 0x3f, 0x8a, 0x65, 0x48, 0x7c, 0xa4, 0x9f, 0x14, 0x4d, 0x52, 0x90, 0x2d,
    0x17, 0xd1, 0x3e, 0x05, 0xd6, 0x35, 0x1b, 0xdb, 0xe5, 0x1a, 0xa2, 0x54, 0x8c, 0x30, 0x6f, 0xfe,
    0xa1, 0xd9, 0x98, 0x3f, 0xb5, 0x65, 0x14, 0x9c, 0x50, 0x55, 0xa1, 0xbf, 0xb5, 0x12, 0xc4, 0xf2,
    0x72, 0x27, 0x14, 0x59, 0xb5, 0x23, 0x67, 0x11, 0x2a, 0xd8, 0xa8, 0x85, 0x4b, 0xc5, 0xb0, 0x2f,
    0x73, 0x54, 0xcf, 0x33, 0xa0, 0x06, 0xf2, 0x8e, 0x4f, 0x4b, 0x18, 0x97, 0x08, 0x47, 0xce, 0x0c,
    0x47, 0x97, 0x0d, 0xbd, 0x8b, 0xce, 0x61, 0x31, 0x21, 0x7e, 0xc4, 0x1d, 0x03, 0xf8, 0x06, 0xca,
    0x9f, 0xd3, 0x5e, 0x4b, 0xfc, 0xf1, 0x99, 0x34, 0x78, 0x83, 0xfa, 0xab, 0x9c, 0x7c, 0x6b, 0x5c,
    0x3d, 0x45, 0x39, 0x6d, 0x6a, 0x6c, 0xd5, 0x63, 0x3e, 0xbe, 0x09, 0x62, 0x64, 0x5f, 0x83, 0x3b,
    0xb6, 0x5c, 0x7e, 0x8e, 0xeb, 0x1e, 0x6a, 0x34, 0xb9, 0xc7, 0x92, 0x92, 0x58, 0x64, 0x48, 0xfe,
    0xf8, 0x35, 0x53, 0x07, 0x89, 0xb4, 0x29, 0x4d, 0x3d, 0x79, 0x43, 0x73, 0x0f, 0x16, 0x21, 0xab,
    0xb7, 0x07, 0x2b, 0x5a, 0x8a, 0x0f, 0xd7, 0x2e, 0x95, 0xb4, 0x26, 0x66, 0x65, 0x72, 0xac, 0x7e,
    0x46, 0x70, 0xe6, 0xad, 0x43, 0xa2, 0x73, 0x54, 0x6a, 0x41, 0xc8, 0x9c, 0x1e, 0x65, 0xed, 0x06,
    0xd1, 0xc7, 0x99, 0x3e, 0x5f, 0x5a, 0xd3, 0xd0, 0x1a, 0x9b, 0x0e, 0x3e, 0x04, 0x66, 0xb6, 0xaa,
    0xa6, 0x51, 0xb8, 0xc0, 0x13, 0x19, 0x34, 0x0e, 0x86, 0x02, 0xd5, 0xc8, 0x10, 0xaa, 0x1f, 0x97,
    0x95
};
static RTBIGNUM g_Signature;
static uint8_t const g_abSignatureDecrypted[] =
{
    0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x30, 0x21, 0x30,
    0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14, 0x54, 0x60, 0xb0, 0x65,
    0xf1, 0xbc, 0x40, 0x77, 0xfc, 0x9e, 0xfc, 0x2f, 0x94, 0x62, 0x62, 0x61, 0x43, 0xb9, 0x01, 0xb9
};
static RTBIGNUM g_SignatureDecrypted;
/** @} */


static void testInitOneLittleEndian(uint8_t const *pb, size_t cb, PRTBIGNUM pBigNum)
{
    uint8_t abLittleEndian[sizeof(g_abLargePositive) + sizeof(g_abLargeNegative)];
    RTTESTI_CHECK_RETV(cb <= sizeof(abLittleEndian));

    size_t         cbLeft = cb;
    uint8_t       *pbDst  = abLittleEndian + cb - 1;
    uint8_t const *pbSrc  = pb;
    while (cbLeft-- > 0)
        *pbDst-- = *pbSrc++;

    RTBIGNUM Num;
    RTTESTI_CHECK_RC_RETV(RTBigNumInit(&Num, RTBIGNUMINIT_F_ENDIAN_LITTLE | RTBIGNUMINIT_F_SIGNED,
                                       abLittleEndian, cb), VINF_SUCCESS);
    RTTESTI_CHECK(Num.fNegative == pBigNum->fNegative);
    RTTESTI_CHECK(Num.cUsed == pBigNum->cUsed);
    RTTESTI_CHECK(RTBigNumCompare(&Num, pBigNum) == 0);
    RTTESTI_CHECK_RC(RTBigNumDestroy(&Num), VINF_SUCCESS);

    RTTESTI_CHECK_RC_RETV(RTBigNumInit(&Num, RTBIGNUMINIT_F_ENDIAN_LITTLE | RTBIGNUMINIT_F_SIGNED | RTBIGNUMINIT_F_SENSITIVE,
                                       abLittleEndian, cb), VINF_SUCCESS);
    RTTESTI_CHECK(Num.fNegative == pBigNum->fNegative);
    RTTESTI_CHECK(Num.cUsed == pBigNum->cUsed);
    RTTESTI_CHECK(RTBigNumCompare(&Num, pBigNum) == 0);
    RTTESTI_CHECK_RC(RTBigNumDestroy(&Num), VINF_SUCCESS);
}

static void testMoreInit(void)
{
    RTTESTI_CHECK(!g_LargePositive.fNegative);
    RTTESTI_CHECK(!g_LargePositive.fSensitive);
    RTTESTI_CHECK(!g_LargePositive2.fNegative);
    RTTESTI_CHECK(!g_LargePositive2.fSensitive);
    RTTESTI_CHECK(g_LargeNegative.fNegative);
    RTTESTI_CHECK(!g_LargeNegative.fSensitive);
    RTTESTI_CHECK(g_LargeNegative2.fNegative);
    RTTESTI_CHECK(!g_LargeNegative2.fSensitive);

    RTTESTI_CHECK(!g_Zero.fNegative);
    RTTESTI_CHECK(!g_Zero.fSensitive);
    RTTESTI_CHECK(g_Zero.cUsed == 0);

    RTTESTI_CHECK(g_Minus1.fNegative);
    RTTESTI_CHECK(!g_Minus1.fSensitive);
    RTTESTI_CHECK(g_Minus1.cUsed == 1);
    RTTESTI_CHECK(g_Minus1.pauElements[0] == 1);

    RTTESTI_CHECK(g_One.cUsed       == 1 && g_One.pauElements[0]        == 1);
    RTTESTI_CHECK(g_Two.cUsed       == 1 && g_Two.pauElements[0]        == 2);
    RTTESTI_CHECK(g_Three.cUsed     == 1 && g_Three.pauElements[0]      == 3);
    RTTESTI_CHECK(g_Four.cUsed      == 1 && g_Four.pauElements[0]       == 4);
    RTTESTI_CHECK(g_Ten.cUsed       == 1 && g_Ten.pauElements[0]        == 10);
    RTTESTI_CHECK(g_FourtyTwo.cUsed == 1 && g_FourtyTwo.pauElements[0]  == 42);

    /* Test big endian initialization w/ sensitive variation. */
    testInitOneLittleEndian(g_abLargePositive, sizeof(g_abLargePositive), &g_LargePositive);
    testInitOneLittleEndian(g_abLargePositive, sizeof(g_abLargePositive) - 11, &g_LargePositive2);

    testInitOneLittleEndian(g_abLargeNegative, sizeof(g_abLargeNegative), &g_LargeNegative);
    testInitOneLittleEndian(g_abLargeNegative, sizeof(g_abLargeNegative) - 9, &g_LargeNegative2);

    RTTESTI_CHECK(g_Minus1.cUsed == 1);
    testInitOneLittleEndian(g_abMinus1, sizeof(g_abMinus1), &g_Minus1);
    testInitOneLittleEndian(g_abMinus1, 1, &g_Minus1);
    testInitOneLittleEndian(g_abMinus1, 4, &g_Minus1);

}


static void testCompare(void)
{
    RTTestSub(g_hTest, "RTBigNumCompare*");
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_LargePositive) == 0);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive2, &g_LargePositive) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_LargePositive2) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_Zero, &g_LargePositive) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_Zero) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive2, &g_Zero) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_LargePositiveMinus1) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositiveMinus1, &g_LargePositive) == -1);

    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_LargeNegative) == 0);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_LargeNegative2) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative2, &g_LargeNegative) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_Zero, &g_LargeNegative) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_Zero) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative2, &g_Zero) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_LargeNegativePluss1) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegativePluss1, &g_LargeNegative) == 1);

    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_LargePositive) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_LargeNegative) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative2, &g_LargePositive) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive, &g_LargeNegative2) == 1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative2, &g_LargePositive2) == -1);
    RTTESTI_CHECK(RTBigNumCompare(&g_LargePositive2, &g_LargeNegative2) == 1);

    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_Zero, 0) == 0);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_Zero, 1) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_Zero, UINT32_MAX) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_Zero, UINT64_MAX) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargePositive, UINT64_MAX) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargePositive2, 0x7213593) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargeNegative, 0) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargeNegative, 1) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargeNegative, UINT64_MAX) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_LargeNegative, 0x80034053) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_64BitPositive1, g_u64BitPositive1) == 0);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_64BitPositive1, g_u64BitPositive1 - 1) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithU64(&g_64BitPositive1, g_u64BitPositive1 + 1) == -1);

    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Zero, 0) == 0);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Zero, 1) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Zero, -1) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Zero, INT32_MAX) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_LargeNegative, INT32_MIN) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_LargeNegative, INT64_MIN) == -1);
    RTTESTI_CHECK(g_u64BitPositive1 < (uint64_t)INT64_MAX);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_64BitPositive1, g_u64BitPositive1) == 0);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_64BitPositive1, g_u64BitPositive1 - 1) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_64BitPositive1, g_u64BitPositive1 + 1) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_64BitPositive1, INT64_MIN) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_64BitPositive1, INT64_MAX) == -1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Minus1, -1) == 0);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Minus1, -2) == 1);
    RTTESTI_CHECK(RTBigNumCompareWithS64(&g_Minus1, 0) == -1);
}


static void testSubtraction(void)
{
    RTTestSub(g_hTest, "RTBigNumSubtract");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Result2;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result2, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_Minus1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_Zero, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_Minus1, &g_Zero), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_64BitPositive1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, g_u64BitPositive1 + 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_Minus1, &g_64BitPositive1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, INT64_C(-1) - g_u64BitPositive1) == 0);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_LargePositive, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);
        RTTESTI_CHECK(Result.cUsed == 1);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_LargePositiveMinus1, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);
        RTTESTI_CHECK(Result.cUsed == 1);

        RTTESTI_CHECK(RTBigNumCompare(&g_LargeNegative, &g_LargeNegativePluss1) < 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_LargeNegative, &g_LargeNegativePluss1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);
        RTTESTI_CHECK(Result.cUsed == 1);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_LargeNegativePluss1, &g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);
        RTTESTI_CHECK(Result.cUsed == 1);

        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result, &g_LargeNegativePluss1, &g_LargeNegativePluss1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);
        RTTESTI_CHECK(Result.cUsed == 0);

        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result2), VINF_SUCCESS);
    }
}


static void testAddition(void)
{
    RTTestSub(g_hTest, "RTBigNumAdd");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Result2;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result2, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_Minus1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -2) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_Zero, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_Zero, &g_64BitPositive1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, g_u64BitPositive1) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_Minus1, &g_64BitPositive1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, g_u64BitPositive1 - 1) == 0);

        RTTESTI_CHECK(g_u64BitPositive1 * 2 > g_u64BitPositive1);
        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_64BitPositive1, &g_64BitPositive1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, g_u64BitPositive1 * 2) == 0);


        RTTESTI_CHECK_RC(RTBigNumAssign(&Result2, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumNegateThis(&Result2), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargePositive, &Result2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &Result2, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithU64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargePositiveMinus1, &Result2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &Result2, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);


        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargePositive, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargePositive) > 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result2, &Result, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargePositive) == 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result2, &Result, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargePositiveMinus1) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargePositive, &g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargeNegative) > 0);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargePositive) < 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result2, &Result, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargeNegative) == 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result2, &Result, &g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargePositive) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargeNegativePluss1, &g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargeNegative) < 0);
        RTTESTI_CHECK_RC(RTBigNumSubtract(&Result2, &Result, &g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargeNegativePluss1) == 0);

        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result2), VINF_SUCCESS);
    }
}

static void testShift(void)
{
    RTTestSub(g_hTest, "RTBigNumShiftLeft, RTBigNumShiftRight");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Result2;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result2, fFlags), VINF_SUCCESS);

        /* basic left tests */
        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Minus1, 1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -2) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Minus1, 0), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Minus1, 2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -4) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Minus1, 8), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -256) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Zero, 511), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_FourtyTwo, 1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 84) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_FourtyTwo, 27+24), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, UINT64_C(0x150000000000000)) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_FourtyTwo, 27), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result2, &Result, 24), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result2, UINT64_C(0x150000000000000)) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_LargePositive, 2), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result2, &g_LargePositive, &g_Four), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &Result) == 0);

        /* basic right tests. */
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_Minus1, 1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_Minus1, 8), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_Zero, 511), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 0), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 42) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 21) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 10) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 3), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 5) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 4), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 2) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 5), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 6), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_FourtyTwo, 549), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivideLong(&Result2, &Result, &g_LargePositive, &g_Four), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &g_LargePositive, 2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &Result) == 0);

        /* Some simple back and forth. */
        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_One, 2), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result2, &Result, 2), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_One) == 0);

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_Three, 63), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result2, &Result, 63), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_Three) == 0);

        for (uint32_t i = 0; i < 1024; i++)
        {
            RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_LargePositive, i), VINF_SUCCESS);
            RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result2, &Result, i), VINF_SUCCESS);
            RTTESTI_CHECK(RTBigNumCompare(&Result2, &g_LargePositive) == 0);
        }

        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &g_LargePositive, 2), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result2, &Result, 250), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftLeft(&Result, &Result2, 999), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result2, &Result, 1), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &Result2, 250), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result2, &Result, 1), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumShiftRight(&Result, &Result2, 999), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargePositive) == 0);


        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result2), VINF_SUCCESS);
    }
}

static bool testHexStringToNum(PRTBIGNUM pBigNum, const char *pszHex, uint32_t fFlags)
{
    uint8_t abBuf[_4K];
    size_t  cbHex = strlen(pszHex);
    RTTESTI_CHECK_RET(!(cbHex & 1), false);
    cbHex /= 2;
    RTTESTI_CHECK_RET(cbHex < sizeof(abBuf), false);
    RTTESTI_CHECK_RC_RET(RTStrConvertHexBytes(pszHex, abBuf, cbHex, 0), VINF_SUCCESS, false);
    RTTESTI_CHECK_RC_RET(RTBigNumInit(pBigNum, RTBIGNUMINIT_F_ENDIAN_BIG | fFlags, abBuf, cbHex), VINF_SUCCESS, false);
    return true;
}

static void testMultiplication(void)
{
    RTTestSub(g_hTest, "RTBigNumMultiply");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Result2;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result2, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &g_Minus1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &g_Zero, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);
        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &g_Minus1, &g_Zero), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &g_Minus1, &g_64BitPositive1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -(int64_t)g_u64BitPositive1) == 0);
        RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &g_64BitPositive1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -(int64_t)g_u64BitPositive1) == 0);


        static struct
        {
            const char *pszF1, *pszF2, *pszResult;
        } s_aTests[] =
        {
            {
                "29865DBFA717181B9DD4B515BD072DE10A5A314385F6DED735AC553FCD307D30C499",
                "4DD65692F7365B90C55F63988E5B6C448653E7DB9DD941507586BD8CF71398287C",
                "0CA02E8FFDB0EEA37264338A4AAA91C8974E162DDFCBCF804B434A11955671B89B3645AAB75423D60CA3459B0B4F3F28978DA768779FB54CF362FD61924637582F221C"
            },
            {
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE0000000000000000000000000000000000000001"
            }
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTBIGNUM F1, F2, Expected;
            if (   testHexStringToNum(&F1, s_aTests[i].pszF1, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&F2, s_aTests[i].pszF2, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Expected, s_aTests[i].pszResult, RTBIGNUMINIT_F_UNSIGNED | fFlags))
            {
                RTTESTI_CHECK_RC(RTBigNumMultiply(&Result, &F1, &F2), VINF_SUCCESS);
                RTTESTI_CHECK(RTBigNumCompare(&Result, &Expected) == 0);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&F1), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&F2), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Expected), VINF_SUCCESS);
            }
        }
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result2), VINF_SUCCESS);
    }
}


#if 0 /* Java program for generating testDivision test data. */
import java.math.BigInteger;
import java.lang.System;
import java.lang.Integer;
import java.util.Random;
import java.security.SecureRandom;

class bigintdivtestgen
{

public static String format(BigInteger BigNum)
{
    String str = BigNum.toString(16);
    if ((str.length() & 1) != 0)
        str = "0" + str;
    return str;
}

public static void main(String args[])
{
    Random Rnd = new SecureRandom();

    /* Can't go to far here because before we reach 11K both windows compilers
       will have reached some kind of section limit. Probably string pool related. */
    int cDivisorLarger = 0;
    for (int i = 0; i < 9216; i++)
    {
        int cDividendBits = Rnd.nextInt(4095) + 1;
        int cDivisorBits  = i < 9 ? cDividendBits : Rnd.nextInt(4095) + 1;
        if (cDivisorBits > cDividendBits)
        {
            cDivisorLarger++;
            if (cDivisorLarger > i / 4)
                cDivisorBits = Rnd.nextInt(cDividendBits);
        }

        BigInteger Dividend = new BigInteger(cDividendBits, Rnd);
        BigInteger Divisor  = new BigInteger(cDivisorBits, Rnd);
        while (Divisor.compareTo(BigInteger.ZERO) == 0) {
            cDivisorBits++;
            Divisor = new BigInteger(cDivisorBits, Rnd);
        }

        BigInteger[] Result = Dividend.divideAndRemainder(Divisor);

        System.out.println("    { /* i=" + Integer.toString(i)
                           + " cDividendBits=" + Integer.toString(cDividendBits)
                           + " cDivisorBits=" + Integer.toString(cDivisorBits) + " */");

        System.out.println("        \"" + format(Dividend)  + "\",");
        System.out.println("        \"" + format(Divisor)   + "\",");
        System.out.println("        \"" + format(Result[0]) + "\",");
        System.out.println("        \"" + format(Result[1]) + "\"");
        System.out.println("    },");
    }
}
}
#endif

static void testDivision(void)
{
    RTTestSub(g_hTest, "RTBigNumDivide");

    //for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    uint32_t fFlags = 0;
    {
        RTBIGNUM Quotient;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Quotient, fFlags), VINF_SUCCESS);
        RTBIGNUM Remainder;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Remainder, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Minus1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 1) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Zero, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 0) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Minus1, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_LargeNegative, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_LargePositive, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Four, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 2) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Three, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 1) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_Ten, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 5) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 0) == 0);


        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_LargePositive, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 1) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &g_LargeNegative, &g_LargeNegativePluss1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Quotient, 1) == 0);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Remainder, -1) == 0);

        static struct
        {
            const char *pszDividend, *pszDivisor, *pszQuotient, *pszRemainder;
        } const s_aTests[] =
        {
#if 1
#include "tstRTBigNum-div-test-data.h"
            {   "ff", "10", /* = */ "0f", "0f" },
            { /* cDividendBits=323 cDivisorBits=195 */
                "064530fd21b30e179b5bd5efd1f4a7e8df173c13965bd75e1502891303060b417e62711ceb17a73e56",
                "0784fac4a7c6b5165a99dc3228b6484cba9c7dfadde85cdde3",
                "d578cc87ed22ac3630a4d1e5fc590ae6",
                "06acef436982f9c4fc9b0a44d3df1e72cad3ef0cb51ba20664"
            },
            {
                "ffffffffffffffffffffffffffffffffffffffffffffffff",
                "fffffffffffffffffffffffffffffffffffffffffffffffe",
                "01",
                "01"
            },
            {
                "922222222222222222222222222222222222222222222222",
                "811111111111111111111111111111111111111111111111",
                "01",
                "111111111111111111111111111111111111111111111111"
            },
            {
                "955555555555555555555555555555555555555555555555",
                "211111111111111111111111111111111111111111111111",
                "04",
                "111111111111111111111111111111111111111111111111"
            },
#endif
            /* This test triggers negative special cases in Knuth's division algorithm. */
            {
                "0137698320ec00bcaa13cd9c18df564bf6df45c5c4c73ad2012cb36cf897c5ff00db638256e19c9ba5a8fbe828ac6e8d470a5f3391d4350ca1390f79c4e4f944eb",
                "67cdd6604eaae98e0699deb2f51cc3fcf51741ec4268f0ab0ee679a83297550049212b724b3433e1e2fea2b8397a2f17ae1fbbdb46bc598b13052896f6fdc1a4",
                "02",
                "67cdd6604eaae98e0699deb2f51cc3fcf51741ec4268f0ab0ee679a83297550049212b724b3433e1e2fea2b8397a2f17ae1fbbdb46bc598b13052896f6fdc1a3"
            },
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTBIGNUM Dividend, Divisor, ExpectedQ, ExpectedR;
            if (   testHexStringToNum(&Dividend,  s_aTests[i].pszDividend, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Divisor,   s_aTests[i].pszDivisor, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&ExpectedQ, s_aTests[i].pszQuotient, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&ExpectedR, s_aTests[i].pszRemainder, RTBIGNUMINIT_F_UNSIGNED | fFlags))
            {
                RTTESTI_CHECK_RC(RTBigNumDivide(&Quotient, &Remainder, &Dividend, &Divisor), VINF_SUCCESS);

                if (   RTBigNumCompare(&Quotient,  &ExpectedQ) != 0
                    || RTBigNumCompare(&Remainder, &ExpectedR) != 0)
                {
                    RTTestIFailed("i=%#x both\n"
                                  "ExpQ: %.*Rhxs\n"
                                  "GotQ: %.*Rhxs\n"
                                  "ExpR: %.*Rhxs\n"
                                  "GotR: %.*Rhxs\n",
                                  i,
                                  ExpectedQ.cUsed * RTBIGNUM_ELEMENT_SIZE, ExpectedQ.pauElements,
                                  Quotient.cUsed  * RTBIGNUM_ELEMENT_SIZE, Quotient.pauElements,
                                  ExpectedR.cUsed * RTBIGNUM_ELEMENT_SIZE, ExpectedR.pauElements,
                                  Remainder.cUsed * RTBIGNUM_ELEMENT_SIZE, Remainder.pauElements);
                    RTTestIPrintf(RTTESTLVL_ALWAYS,  "{ \"%s\", \"%s\", \"%s\", \"%s\" },\n",
                                  s_aTests[i].pszDividend, s_aTests[i].pszDivisor,
                                  s_aTests[i].pszQuotient, s_aTests[i].pszRemainder);
                }

                RTTESTI_CHECK_RC(RTBigNumDivideLong(&Quotient, &Remainder, &Dividend, &Divisor), VINF_SUCCESS);
                RTTESTI_CHECK(RTBigNumCompare(&Quotient,  &ExpectedQ) == 0);
                RTTESTI_CHECK(RTBigNumCompare(&Remainder, &ExpectedR) == 0);

                RTTESTI_CHECK_RC(RTBigNumModulo(&Remainder, &Dividend, &Divisor), VINF_SUCCESS);
                RTTESTI_CHECK(RTBigNumCompare(&Remainder, &ExpectedR) == 0);


                RTTESTI_CHECK_RC(RTBigNumDestroy(&ExpectedR), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&ExpectedQ), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Divisor), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Dividend), VINF_SUCCESS);
            }
        }

        RTTESTI_CHECK_RC(RTBigNumDestroy(&Quotient), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Remainder), VINF_SUCCESS);
    }
}


static void testModulo(void)
{
    RTTestSub(g_hTest, "RTBigNumModulo");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Tmp;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Tmp, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Minus1, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Zero, &g_Minus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Minus1, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_LargeNegative, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_LargePositive, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Four, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Three, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_Ten, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_LargePositive, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_LargePositiveMinus1, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargePositiveMinus1) == 0);

        RTTESTI_CHECK_RC(RTBigNumAdd(&Result, &g_LargePositiveMinus1, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumAdd(&Tmp, &g_LargePositive, &Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &Tmp, &g_LargePositiveMinus1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 2) == 0);
        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &Tmp, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompare(&Result, &g_LargePositiveMinus1) == 0);

        RTTESTI_CHECK_RC(RTBigNumModulo(&Result, &g_LargeNegative, &g_LargeNegativePluss1), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, -1) == 0);

        RTTESTI_CHECK_RC(RTBigNumDestroy(&Tmp), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
    }
}


static void testExponentiation(void)
{
    RTTestSub(g_hTest, "RTBigNumExponentiate");

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTBIGNUM Result;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);
        RTBIGNUM Result2;
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result2, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_One, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_Two, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 2) == 0);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_Two, &g_Two), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 4) == 0);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_Two, &g_Ten), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1024) == 0);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_Five, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 3125) == 0);

        RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &g_Five, &g_Ten), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 9765625) == 0);

        static struct
        {
            const char *pszBase, *pszExponent, *pszResult;
        } s_aTests[] =
        {
            {
                "180DB4284A119D6133AE4BB0C27C27D1", /*^*/ "3A", /* = */
                "04546412B9E39476F10009F62608F614774C5AE475482434F138C3EA976583ECE09E58F1F03CE41F821A1D5DA59B69D031290B0AC7F7D5058E3AFA2CA3DAA7261D1620CA"
                "D050576C0AFDF51ADBFCB9073B9D8324E816EA6BE4648DF68092F6617ED609045E6BE9D5410AE2CFF725832414E67656233F4DFA952461D321282426D50E2AF524D779EC"
                "0744547E8A4F0768C2C49AF3A5A89D129430CA58456BE4534BC53C67523506C7A8B5770D88CF28B6B3EEBE73F3EA71BA2CE27C4C89BE0D699922B1A1EB20143CB0830A43"
                "D864DDFFF026BA781614C2D55F3EDEA7257B93A0F40824E57D6EDFCFFB4611C316374D0D15698E6584851F1898DCAE75FC4D180908763DDB2FF93766EF144D091274AFE5"
                "6980A1F4F574D577DAD833EA9486A4B499BFCA9C08225D7BDB2C632B4D9B53EF51C02ED419F22657D626064BCC2B083CD664E1A8D68F82F33233A833AC98AA0282B8B88D"
                "A430CF2E581A1C7C4A1D646CA42760ED10C398F7C032A94D53964E6885B5C1CA884EC15081D4C010978627C85767FEC6F93364044EA86567F9610ABFB837808CC995FB5F"
                "710B21CE198E0D4AD9F73C3BD56CB9965C85C790BF3F4B326B5245BFA81783126217BF80687C4A8AA3AE80969A4407191B4F90E71A0ABCCB5FEDD40477CE9D10FBAEF103"
                "8457AB19BD793CECDFF8B29A96F12F590BFED544E08F834A44DEEF461281C40024EFE9388689AAC69BCBAB3D06434172D9319F30754756E1CF77B300679215BEBD27FC20"
                "A2F1D2029BC767D4894A5F7B21BD784CD1DD4F41697839969CB6D2AA1E0AFA5D3D644A792586F681EB36475CAE59EB457E55D6AC2E286E196BFAC000C7389A96C514552D"
                "5D9D3DD962F72DAE4A7575A9A67856646239560A39E50826BB2523598C8F8FF0EC8D09618378E9F362A8FBFE842B55CD1855A95D8A5E93B8B91D31EB8FBBF57113F06171"
                "BB69B81C4240EC4C7D1AC67EA1CE4CEBEE71828917EC1CF500E1AD2F09535F5498CD6E613383810A840A265AED5DD20AE58FFF2D0DEB8EF99FA494B22714F520E8E8B684"
                "5E8521966A7B1699236998A730FDF9F049CE2A4EA44D1EBC3B9754908848540D0DEE64A6D60E2BFBC3362B659C10543BDC20C1BAD3D68B173442C100C2C366CB885E8490"
                "EDB977E49E9D51D4427B73B3B999AF4BA17685387182C3918D20808197A2E3FCDD0F66ECDEC05542C23A08B94C83BDF93606A49E9A0645B002CFCA1EAE1917BEED0D6542"
                "9A0EF00E5FB5F70D61C8C4DF1F1E9DA58188A221"
            },
            {
                "03", /*^*/ "164b", /* = */
                "29ABEC229C2B15C41573F8608D4DCD2DADAACA94CA3C40B42FFAD32D6202E228E16F61E050FF97EC5D45F24A4EB057C2D1A5DA72DFC5944E6941DBEDDE70EF56702BEC35"
                "A3150EFE84E87185E3CBAB1D73F434EB820E41298BDD4F3941230DFFD8DFF1D2E2F3C5D0CB5088505B9C78507A81AAD8073C28B8FA70771C3E04110344328C6B3F38E55A"
                "32B009F4DDA1813232C3FF422DF4E4D12545C803C63D0BE67E2E773B2BAC41CC69D895787B217D7BE9CE80BD4B500AE630AA21B50A06E0A74953F8011E9F23863CA79885"
                "35D5FF0214DBD9B25756BE3D43008A15C018348E6A7C3355F4BECF37595BD530E5AC1AD3B14182862E47AD002097465F6B78F435B0D6365E18490567F508CD3CAAAD340A"
                "E76A218FE8B517F923FE9CCDE61CB35409590CDBC606D89BA33B32A3862DEE7AB99DFBE103D02D2BED6D418B949E6B3C51CAB8AB5BE93AA104FA10D3A02D4CAD6700CD0F"
                "83922EAAB18705915198DE51C1C562984E2B7571F36A4D756C459B61E0A4B7DE268A74E807311273DD51C2863771AB72504044C870E2498F13BF1DE92C13D93008E304D2"
                "879C5D8A646DB5BF7BC64D96BB9E2FBA2EA6BF55CD825ABD995762F661C327133BE01F9A9F298CA096B3CE61CBBD8047A003870B218AC505D72ED6C7BF3B37BE5877B6A1"
                "606A713EE86509C99B2A3627FD74AE7E81FE7F69C34B40E01A6F8B18A328E0F9D18A7911E5645331540538AA76B6D5D591F14313D730CFE30728089A245EE91058748F0C"
                "E3E6CE4DE51D23E233BFF9007E0065AEBAA3FB0D0FACE62A4757FE1C9C7075E2214071197D5074C92AF1E6D853F7DE782F32F1E40507CB981A1C10AC6B1C23AC46C07EF1"
                "EDE857C444902B936771DF75E0EE6C2CB3F0F9DBB387BAD0658E98F42A7338DE45E2F1B012B530FFD66861F74137C041D7558408A4A23B83FBDDE494381D9F9FF0326D44"
                "302F75DE68B91A54CFF6E3C2821D09F2664CA74783C29AF98E2F1D3D84CAC49EAE55BABE3D2CBE8833D50517109E19CB5C63D1DE26E308ACC213D1CBCCF7C3AAE05B06D9"
                "909AB0A1AEFD02A193CFADC7F724D377E1F4E78DC21012BE26D910548CDF55B0AB9CB64756045FF48C3B858E954553267C4087EC5A9C860CFA56CF5CFBB442BDDA298230"
                "D6C000A6A6010D87FB4C3859C3AFAF15C37BCE03EBC392E8149056C489508841110060A991F1EEAF1E7CCF0B279AB2B35F3DAC0FAB4F4A107794E67D305E6D61A27C8FEB"
                "DEA00C3334C888B2092E740DD3EFF7A69F06CE12EF511126EB23D80902D1D54BF4AEE04DF9457D59E8859AA83D6229481E1B1BC7C3ED96F6F7C1CEEF7B904268FD00BE51"
                "1EF69692D593F8A9F7CCC053C343306940A4054A55DBA94D95FF6D02B7A73E110C2DBE6CA29C01B5921420B5BC9C92DAA9D82003829C6AE772FF12135C2E138C6725DC47"
                "7938F3062264575EBBB1CBB359E496DD7A38AE0E33D1B1D9C16BDD87E6DE44DFB832286AE01D00AA14B423DBF7ECCC34A0A06A249707B75C2BA931D7F4F513FDF0F6E516"
                "345B8DA85FEFD218B390828AECADF0C47916FAF44CB29010B0BB2BBA8E120B6DAFB2CC90B9D1B8659C2AFB"
            }
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTBIGNUM Base, Exponent, Expected;
            if (   testHexStringToNum(&Base, s_aTests[i].pszBase, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Exponent, s_aTests[i].pszExponent, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Expected, s_aTests[i].pszResult, RTBIGNUMINIT_F_UNSIGNED | fFlags))
            {
                RTTESTI_CHECK_RC(RTBigNumExponentiate(&Result, &Base, &Exponent), VINF_SUCCESS);
                RTTESTI_CHECK(RTBigNumCompare(&Result, &Expected) == 0);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Base), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Exponent), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Expected), VINF_SUCCESS);
            }
        }
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result2), VINF_SUCCESS);
    }
}


static void testModExp(void)
{
    RTTestSub(g_hTest, "RTBigNumModExp");
    RTBIGNUM Result;

    for (uint32_t fFlags = 0; fFlags <= RTBIGNUMINIT_F_SENSITIVE; fFlags += RTBIGNUMINIT_F_SENSITIVE)
    {
        RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, fFlags), VINF_SUCCESS);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_One, &g_One, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_One, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_LargePositive, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_One, &g_Zero, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 1);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_Zero, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 1);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_Zero, &g_One), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_Zero, &g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 1);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Zero, &g_Zero, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_Zero, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);
        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_LargePositive, &g_LargePositive, &g_Zero), VERR_BIGNUM_DIV_BY_ZERO);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Two, &g_Four, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Two, &g_Four, &g_Three), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 1) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Three, &g_Three, &g_Three), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 0) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Three, &g_Three, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 2) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Three, &g_Five, &g_Five), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 3) == 0);

        RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Three, &g_Five, &g_Four), VINF_SUCCESS);
        RTTESTI_CHECK(RTBigNumCompareWithS64(&Result, 3) == 0);

#if 0
        static struct
        {
            const char *pszBase, *pszExponent, *pszModulus, *pszResult;
        } s_aTests[] =
        {
            {
                "180DB4284A119D6133AE4BB0C27C27D1", /*^*/ "3A", /*mod */ " ",  /* = */
            },
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTBIGNUM Base, Exponent, Expected, Modulus;
            if (   testHexStringToNum(&Base, s_aTests[i].pszBase, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Exponent, s_aTests[i].pszExponent, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Modulus, s_aTests[i].pszModulus, RTBIGNUMINIT_F_UNSIGNED | fFlags)
                && testHexStringToNum(&Expected, s_aTests[i].pszResult, RTBIGNUMINIT_F_UNSIGNED | fFlags))
            {
                RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &Base, &Exponent, &Modulus), VINF_SUCCESS);
                RTTESTI_CHECK(RTBigNumCompare(&Result, &Expected) == 0);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Base), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Exponent), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Expected), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTBigNumDestroy(&Modulus), VINF_SUCCESS);
            }
        }
#endif

        RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
    }

    /* Decrypt a PKCS#7 signature. */
    RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Result, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumModExp(&Result, &g_Signature, &g_PubKeyExp, &g_PubKeyMod), VINF_SUCCESS);
    RTTESTI_CHECK(RTBigNumCompare(&Result, &g_SignatureDecrypted) == 0);
    RTTESTI_CHECK_RC(RTBigNumDestroy(&Result), VINF_SUCCESS);
}


static void testToBytes(void)
{
    RTTestSub(g_hTest, "RTBigNumToBytes*Endian");
    uint8_t abBuf[sizeof(g_abLargePositive) + sizeof(g_abLargeNegative)];

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Zero, abBuf, 1), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0 && abBuf[1] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Zero, abBuf, 2), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0 && abBuf[1] == 0 && abBuf[2] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Zero, abBuf, 3), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0 && abBuf[1] == 0 && abBuf[2] == 0 && abBuf[3] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Zero, abBuf, 4), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0 && abBuf[1] == 0 && abBuf[2] == 0 && abBuf[3] == 0 && abBuf[4] == 0xcc);


    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Minus1, abBuf, 1), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0xff && abBuf[1] == 0xcc && abBuf[2] == 0xcc && abBuf[3] == 0xcc && abBuf[4] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Minus1, abBuf, 2), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0xff && abBuf[1] == 0xff && abBuf[2] == 0xcc && abBuf[3] == 0xcc && abBuf[4] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Minus1, abBuf, 3), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0xff && abBuf[1] == 0xff && abBuf[2] == 0xff && abBuf[3] == 0xcc && abBuf[4] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_Minus1, abBuf, 4), VINF_SUCCESS);
    RTTESTI_CHECK(abBuf[0] == 0xff && abBuf[1] == 0xff && abBuf[2] == 0xff && abBuf[3] == 0xff && abBuf[4] == 0xcc);


    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_LargePositive, abBuf, sizeof(g_abLargePositive)), VINF_SUCCESS);
    RTTESTI_CHECK(memcmp(abBuf, g_abLargePositive, sizeof(g_abLargePositive)) == 0);
    RTTESTI_CHECK(abBuf[sizeof(g_abLargePositive)] == 0xcc);

    memset(abBuf, 0xcc, sizeof(abBuf));
    RTTESTI_CHECK_RC(RTBigNumToBytesBigEndian(&g_LargePositive, abBuf, sizeof(g_abLargePositive) -1 ), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(memcmp(abBuf, &g_abLargePositive[1], sizeof(g_abLargePositive) - 1) == 0);
    RTTESTI_CHECK(abBuf[sizeof(g_abLargePositive) - 1] == 0xcc);
}


static void testBenchmarks(bool fOnlyModExp)
{
    RTTestSub(g_hTest, "Benchmarks");

    /*
     * For the modexp benchmark we decrypt a real PKCS #7 signature.
     */
    RTBIGNUM Decrypted;
    RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Decrypted, 0 /*fFlags*/), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTBigNumModExp(&Decrypted, &g_Signature, &g_PubKeyExp, &g_PubKeyMod), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTBigNumModExp(&Decrypted, &g_Signature, &g_PubKeyExp, &g_PubKeyMod), VINF_SUCCESS);

    RTThreadYield();
    int      rc       = VINF_SUCCESS;
    uint32_t cRounds  = 0;
    uint64_t uStartTS = RTTimeNanoTS();
    while (cRounds < 10240)
    {
        rc |= RTBigNumModExp(&Decrypted, &g_Signature, &g_PubKeyExp, &g_PubKeyMod);
        cRounds++;
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTestIValue("RTBigNumModExp", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);

    if (fOnlyModExp)
        return;

#if 1
    /* Compare with OpenSSL BN. */
    BN_CTX *pObnCtx = BN_CTX_new();
    BIGNUM *pObnPubKeyExp = BN_bin2bn(g_abPubKeyExp, sizeof(g_abPubKeyExp), NULL);
    BIGNUM *pObnPubKeyMod = BN_bin2bn(g_abPubKeyMod, sizeof(g_abPubKeyMod), NULL);
    BIGNUM *pObnSignature = BN_bin2bn(g_abSignature, sizeof(g_abSignature), NULL);
    BIGNUM *pObnSignatureDecrypted = BN_bin2bn(g_abSignatureDecrypted, sizeof(g_abSignatureDecrypted), NULL);
    BIGNUM *pObnResult = BN_new();
    RTTESTI_CHECK_RETV(BN_mod_exp(pObnResult, pObnSignature, pObnPubKeyExp, pObnPubKeyMod, pObnCtx) == 1);
    RTTESTI_CHECK_RETV(BN_ucmp(pObnResult, pObnSignatureDecrypted) == 0);

    rc = 1;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 4096)
    {
        rc &= BN_mod_exp(pObnResult, pObnSignature, pObnPubKeyExp, pObnPubKeyMod, pObnCtx);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, 1);
    RTTestIValue("BN_mod_exp", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);

    rc = 1;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 4096)
    {
        rc &= BN_mod_exp_simple(pObnResult, pObnSignature, pObnPubKeyExp, pObnPubKeyMod, pObnCtx);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, 1);
    RTTestIValue("BN_mod_exp_simple", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);
#endif

    /*
     * Check out the speed of modulo.
     */
    RTBIGNUM Product;
    RTTESTI_CHECK_RC_RETV(RTBigNumInitZero(&Product, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTBigNumMultiply(&Product, &g_Signature, &g_Signature), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTBigNumModulo(&Decrypted, &Product, &g_PubKeyMod), VINF_SUCCESS);
    RTThreadYield();
    rc       = VINF_SUCCESS;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 10240)
    {
        rc |= RTBigNumModulo(&Decrypted, &Product, &g_PubKeyMod);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTestIValue("RTBigNumModulo", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);

    RTBigNumDestroy(&Decrypted);

#if 1
    /* Compare with OpenSSL BN. */
    BIGNUM *pObnProduct = BN_new();
    RTTESTI_CHECK_RETV(BN_mul(pObnProduct, pObnSignature, pObnSignature, pObnCtx) == 1);
    RTTESTI_CHECK_RETV(BN_mod(pObnResult, pObnProduct, pObnPubKeyMod, pObnCtx) == 1);
    rc = 1;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 10240)
    {
        rc &= BN_mod(pObnResult, pObnProduct, pObnPubKeyMod, pObnCtx);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, 1);
    RTTestIValue("BN_mod", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);
#endif

    /*
     * Check out the speed of multiplication.
     */
    RTThreadYield();
    rc       = VINF_SUCCESS;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 10240)
    {
        rc |= RTBigNumMultiply(&Product, &g_Signature, &g_Signature);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTestIValue("RTBigNumMultiply", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);

    RTBigNumDestroy(&Product);

#if 1
    /* Compare with OpenSSL BN. */
    rc = 1;
    cRounds  = 0;
    uStartTS = RTTimeNanoTS();
    while (cRounds < 10240)
    {
        rc &= BN_mul(pObnProduct, pObnSignature, pObnSignature, pObnCtx);
        cRounds++;
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK_RC(rc, 1);
    RTTestIValue("BN_mul", uElapsed / cRounds, RTTESTUNIT_NS_PER_CALL);

    BN_free(pObnPubKeyExp);
    BN_free(pObnPubKeyMod);
    BN_free(pObnSignature);
    BN_free(pObnSignatureDecrypted);
    BN_free(pObnResult);
    BN_free(pObnProduct);
    BN_CTX_free(pObnCtx);
#endif

}

/*
 * UInt128 tests (RTBigInt uses UInt128 in some cases.
 */

static void testUInt128Subtraction(void)
{
    RTTestSub(g_hTest, "RTUInt128Sub");

    static struct
    {
        RTUINT128U uMinuend, uSubtrahend, uResult;
    } const s_aTests[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(~0, ~0) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(~0, ~0) },
        { RTUINT128_INIT_C(2, 9), RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(0, 9) },
        { RTUINT128_INIT_C(2, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(1, ~0) },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0x0000000000000000),
            RTUINT128_INIT_C(0x0000000000000000, 0xffffffffffffffff),
            RTUINT128_INIT_C(0xfffffffffffffffe, 0x0000000000000001),
        },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0xfffffffffff00000),
            RTUINT128_INIT_C(0x0000000000000000, 0x00000000000fffff),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffe00001),
        },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x00000fffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0xfffff00000000000, 0x0000000000000000)
        },
        {
            RTUINT128_INIT_C(0x0000000000000000, 0x000000251ce8fe85),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000301f41b4d),
            RTUINT128_INIT_C(0x0000000000000000, 0x000000221af4e338),
        },
        {
            RTUINT128_INIT_C(0xfd4d22a441ffa48c, 0x170739b573a9498d),
            RTUINT128_INIT_C(0x43459cea40782b26, 0xc8c16bb29cb3b343),
            RTUINT128_INIT_C(0xba0785ba01877965, 0x4e45ce02d6f5964a),
        },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RTUINT128U uResult;
        PRTUINT128U pResult = RTUInt128Sub(&uResult, &s_aTests[i].uMinuend, &s_aTests[i].uSubtrahend);
        if (pResult != &uResult)
            RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
        else if (RTUInt128IsNotEqual(&uResult, &s_aTests[i].uResult))
            RTTestIFailed("test #%i failed: remainder differs:\nExp: %016RX64`%016RX64\nGot: %016RX64`%016RX64",
                          i, s_aTests[i].uResult.s.Hi, s_aTests[i].uResult.s.Lo, uResult.s.Hi, uResult.s.Lo );

        uResult = s_aTests[i].uMinuend;
        pResult = RTUInt128AssignSub(&uResult, &s_aTests[i].uSubtrahend);
        RTTESTI_CHECK(pResult == &uResult);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
    }
}


static void testUInt128Addition(void)
{
    RTTestSub(g_hTest, "RTUInt128Add");

    static struct
    {
        RTUINT128U uAugend, uAddend, uResult;
    } const s_aTests[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 2) },
        { RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 3) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(0, 3) },
        { RTUINT128_INIT_C(2, 9), RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(4, 9) },
        { RTUINT128_INIT_C(2, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(2, 3) },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0x0000000000000000),
            RTUINT128_INIT_C(0x0000000000000000, 0xffffffffffffffff),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
        },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0xfffffffffff00000),
            RTUINT128_INIT_C(0x0000000000000000, 0x00000000000ffeff),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xfffffffffffffeff),
        },
        {
            RTUINT128_INIT_C(0xefffffffffffffff, 0xfffffffffff00000),
            RTUINT128_INIT_C(0x0000000000000000, 0x00000000001fffff),
            RTUINT128_INIT_C(0xf000000000000000, 0x00000000000fffff),
        },
        {
            RTUINT128_INIT_C(0xeeeeeeeeeeeeeeee, 0xeeeeeeeeeee00000),
            RTUINT128_INIT_C(0x0111111111111111, 0x11111111112fffff),
            RTUINT128_INIT_C(0xf000000000000000, 0x00000000000fffff),
        },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x00000fffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x00000fffffffffff, 0xfffffffffffffffe)
        },
        {
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xfffffffffffffffe)
        },
        {
            RTUINT128_INIT_C(0x0000000000000000, 0x000000251ce8fe85),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000301f41b4d),
            RTUINT128_INIT_C(0x0000000000000000, 0x000000281edd19d2),
        },
        {
            RTUINT128_INIT_C(0xfd4d22a441ffa48c, 0x170739b573a9498d),
            RTUINT128_INIT_C(0x43459cea40782b26, 0xc8c16bb29cb3b343),
            RTUINT128_INIT_C(0x4092bf8e8277cfb2, 0xdfc8a568105cfcd0),
        },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RTUINT128U uResult;
        PRTUINT128U pResult = RTUInt128Add(&uResult, &s_aTests[i].uAugend, &s_aTests[i].uAddend);
        if (pResult != &uResult)
            RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
        else if (RTUInt128IsNotEqual(&uResult, &s_aTests[i].uResult))
            RTTestIFailed("test #%i failed: result differs:\nExp: %016RX64`%016RX64\nGot: %016RX64`%016RX64",
                          i, s_aTests[i].uResult.s.Hi, s_aTests[i].uResult.s.Lo, uResult.s.Hi, uResult.s.Lo );

        uResult = s_aTests[i].uAugend;
        pResult = RTUInt128AssignAdd(&uResult, &s_aTests[i].uAddend);
        RTTESTI_CHECK(pResult == &uResult);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));

        if (s_aTests[i].uAddend.s.Hi == 0)
        {
            pResult = RTUInt128AddU64(&uResult, &s_aTests[i].uAugend, s_aTests[i].uAddend.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));

            uResult = s_aTests[i].uAugend;
            pResult = RTUInt128AssignAddU64(&uResult, s_aTests[i].uAddend.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
        }

        if (s_aTests[i].uAugend.s.Hi == 0)
        {
            pResult = RTUInt128AddU64(&uResult, &s_aTests[i].uAddend, s_aTests[i].uAugend.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));

            uResult = s_aTests[i].uAddend;
            pResult = RTUInt128AssignAddU64(&uResult, s_aTests[i].uAugend.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
        }
    }
}

static void testUInt128Multiplication(void)
{
    RTTestSub(g_hTest, "RTUInt128Mul");

    static struct
    {
        RTUINT128U uFactor1, uFactor2, uResult;
    } const s_aTests[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(~0, ~0), RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1) },
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(0, 2) },
        { RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(0, 0) },
        { RTUINT128_INIT_C(2, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(4, 2) },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0, 2),
            RTUINT128_INIT_C(0x2222222222222222, 0x2222222222222222)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0, 0xf),
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0,                             0x30000),
            RTUINT128_INIT_C(0x3333333333333333, 0x3333333333330000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0,                          0x30000000),
            RTUINT128_INIT_C(0x3333333333333333, 0x3333333330000000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0,                     0x3000000000000),
            RTUINT128_INIT_C(0x3333333333333333, 0x3333000000000000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0x0000000000000003, 0x0000000000000000),
            RTUINT128_INIT_C(0x3333333333333333, 0x0000000000000000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0x0000000300000000, 0x0000000000000000),
            RTUINT128_INIT_C(0x3333333300000000, 0x0000000000000000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0x0003000000000000, 0x0000000000000000),
            RTUINT128_INIT_C(0x3333000000000000, 0x0000000000000000)
        },
        {
            RTUINT128_INIT_C(0x1111111111111111, 0x1111111111111111),
            RTUINT128_INIT_C(0x3000000000000000, 0x0000000000000000),
            RTUINT128_INIT_C(0x3000000000000000, 0x0000000000000000)
        },
        {
            RTUINT128_INIT_C(0x0000000000000000, 0x6816816816816817),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000000a0280a),
            RTUINT128_INIT_C(0x0000000000411e58, 0x7627627627b1a8e6)
        },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RTUINT128U uResult;
        PRTUINT128U pResult = RTUInt128Mul(&uResult, &s_aTests[i].uFactor1, &s_aTests[i].uFactor2);
        if (pResult != &uResult)
            RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
        else if (RTUInt128IsNotEqual(&uResult, &s_aTests[i].uResult))
            RTTestIFailed("test #%i failed: \nExp: %016RX64`%016RX64\nGot: %016RX64`%016RX64",
                          i, s_aTests[i].uResult.s.Hi, s_aTests[i].uResult.s.Lo, uResult.s.Hi, uResult.s.Lo );

        if (s_aTests[i].uFactor2.s.Hi == 0)
        {
            pResult = RTUInt128MulByU64(&uResult, &s_aTests[i].uFactor1, s_aTests[i].uFactor2.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
        }

        if (s_aTests[i].uFactor1.s.Hi == 0)
        {
            pResult = RTUInt128MulByU64(&uResult, &s_aTests[i].uFactor2, s_aTests[i].uFactor1.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
        }

        uResult = s_aTests[i].uFactor1;
        pResult = RTUInt128AssignMul(&uResult, &s_aTests[i].uFactor2);
        RTTESTI_CHECK(pResult == &uResult);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTests[i].uResult));
    }

    /* extended versions */
    RTTestSub(g_hTest, "RTUInt128MulEx");
    static struct
    {
        RTUINT128U uFactor1, uFactor2;
        RTUINT256U uResult;
    } const s_aTestsEx[] =
    {
    { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(~0, 0),    RTUINT256_INIT_C(~1, ~0, 1, 0) },
    { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(~0, ~0),   RTUINT256_INIT_C(~0, ~1, 0, 1) },
        { RTUINT128_INIT_C(0, 0),       RTUINT128_INIT_C(0, 1),     RTUINT256_INIT_C(0, 0, 0, 0) },
        { RTUINT128_INIT_C(0, 1),       RTUINT128_INIT_C(0, 1),     RTUINT256_INIT_C(0, 0, 0, 1) },
        { RTUINT128_INIT_C(0, 2),       RTUINT128_INIT_C(0, 2),     RTUINT256_INIT_C(0, 0, 0, 4) },
        { RTUINT128_INIT_C(2, 0),       RTUINT128_INIT_C(0, 4),     RTUINT256_INIT_C(0, 0, 8, 0) },
        { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(0, 0),     RTUINT256_INIT_C(0, 0, 0, 0) },
        { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(0, ~0),    RTUINT256_INIT_C(0, ~1, ~0, 1) },
        { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(~0, 0),    RTUINT256_INIT_C(~1, ~0, 1, 0) },
        { RTUINT128_INIT_C(~0, ~0),     RTUINT128_INIT_C(~0, ~0),   RTUINT256_INIT_C(~0, ~1, 0, 1) },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aTestsEx); i++)
    {
        RTUINT256U uResult;
        PRTUINT256U pResult = RTUInt128MulEx(&uResult, &s_aTestsEx[i].uFactor1, &s_aTestsEx[i].uFactor2);
        if (pResult != &uResult)
            RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
        else if (!RTUInt256IsEqual(&uResult, &s_aTestsEx[i].uResult))
            RTTestIFailed("test #%i failed: \nExp: %016RX64`%016RX64`%016RX64`%016RX64\nGot: %016RX64`%016RX64`%016RX64`%016RX64",
                          i, s_aTestsEx[i].uResult.QWords.qw3, s_aTestsEx[i].uResult.QWords.qw2, s_aTestsEx[i].uResult.QWords.qw1,
                          s_aTestsEx[i].uResult.QWords.qw0, uResult.QWords.qw3, uResult.QWords.qw2, uResult.QWords.qw1,
                          uResult.QWords.qw0 );

        if (s_aTestsEx[i].uFactor2.s.Hi == 0)
        {
            RTUInt256AssignBitwiseNot(&uResult);
            pResult = RTUInt128MulByU64Ex(&uResult, &s_aTestsEx[i].uFactor1, s_aTestsEx[i].uFactor2.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt256IsEqual(&uResult, &s_aTestsEx[i].uResult));
        }

        if (s_aTestsEx[i].uFactor1.s.Hi == 0)
        {
            RTUInt256AssignBitwiseNot(&uResult);
            pResult = RTUInt128MulByU64Ex(&uResult, &s_aTestsEx[i].uFactor2, s_aTestsEx[i].uFactor1.s.Lo);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt256IsEqual(&uResult, &s_aTestsEx[i].uResult));
        }

#if 0
        uResult = s_aTestsEx[i].uFactor1;
        pResult = RTUInt128AssignMul(&uResult, &s_aTestsEx[i].uFactor2);
        RTTESTI_CHECK(pResult == &uResult);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResult, &s_aTestsEx[i].uResult));
#endif
    }

}


#if 0 /* Java program for generating testUInt128Division test data. */
import java.math.BigInteger;
import java.lang.System;
import java.lang.Integer;
import java.util.Random;
import java.security.SecureRandom;

class uint128divtestgen
{

public static String format(BigInteger BigNum)
{
    String str = BigNum.toString(16);
    while (str.length() < 32)
        str = "0" + str;
    return "RTUINT128_INIT_C(0x" + str.substring(0, 16) + ", 0x" + str.substring(16) + ")";
}

public static void main(String args[])
{
    Random Rnd = new SecureRandom();

    int cDivisorLarger = 0;
    for (int i = 0; i < 4096; i++)
    {
        int cDividendBits = Rnd.nextInt(127) + 1;
        int cDivisorBits  = i < 9 ? cDividendBits : Rnd.nextInt(127) + 1;
        if (cDivisorBits > cDividendBits)
        {
            if (cDivisorLarger > i / 16)
                cDividendBits = 128 - Rnd.nextInt(16);
            else
                cDivisorLarger++;
        }

        BigInteger Dividend = new BigInteger(cDividendBits, Rnd);
        BigInteger Divisor  = new BigInteger(cDivisorBits, Rnd);
        while (Divisor.compareTo(BigInteger.ZERO) == 0) {
            cDivisorBits++;
            Divisor = new BigInteger(cDivisorBits, Rnd);
        }

        BigInteger[] Result = Dividend.divideAndRemainder(Divisor);

        System.out.println("    { /* i=" + Integer.toString(i) + "; " + Integer.toString(cDividendBits) + " / " + Integer.toString(cDivisorBits) + " */");
        System.out.println("        " + format(Dividend)  + ", " + format(Divisor)   + ",");
        System.out.println("        " + format(Result[0]) + ", " + format(Result[1]) + "");
        System.out.println("    },");
    }
}
}
#endif

static void testUInt128Division(void)
{
    RTTestSub(g_hTest, "RTUInt128DivMod");

    static struct
    {
        RTUINT128U uDividend, uDivisor, uQuotient, uRemainder;
    } const s_aTests[] =
    {
        { RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 0) }, /* #0 */
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 0) }, /* #1 */
        { RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(0, 0), RTUINT128_INIT_C(0, 1) }, /* #2 */
        { RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(2, 0), RTUINT128_INIT_C(0, 1), RTUINT128_INIT_C(0, 0) }, /* #3 */
        { RTUINT128_INIT_C(2, 1), RTUINT128_INIT_C(0, 2), RTUINT128_INIT_C(1, 0), RTUINT128_INIT_C(0, 1) }, /* #4 */
        {   /* #5 */
            RTUINT128_INIT_C(0xffffffffffffffff, 0x0000000000000000),
            RTUINT128_INIT_C(0x0000000000000000, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x0000000000000001, 0x0000000000000000),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000000000000)
        },
        {   /* #6 */
            RTUINT128_INIT_C(0xffffffffffffffff, 0xfffffffffff00000),
            RTUINT128_INIT_C(0x00000fffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000000100000),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000000000000)
        },
        {   /* #7 */
            RTUINT128_INIT_C(0xffffffffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x00000fffffffffff, 0xffffffffffffffff),
            RTUINT128_INIT_C(0x0000000000000000, 0x0000000000100000),
            RTUINT128_INIT_C(0x0000000000000000, 0x00000000000fffff)
        },
        {   /* #8 */
            RTUINT128_INIT_C(0x0000000000000000, 0x000000251ce8fe85), RTUINT128_INIT_C(0x0000000000000000, 0x0000000301f41b4d),
            RTUINT128_INIT_C(0x0000000000000000, 0x000000000000000c), RTUINT128_INIT_C(0x0000000000000000, 0x000000010577b6e9)
        },

#include "tstRTBigNum-uint128-div-test-data.h"
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RTUINT128U uResultQ, uResultR;
        PRTUINT128U pResultQ = RTUInt128DivRem(&uResultQ, &uResultR, &s_aTests[i].uDividend, &s_aTests[i].uDivisor);
        if (pResultQ != &uResultQ)
            RTTestIFailed("test #%i returns %p instead of %p", i, pResultQ, &uResultQ);
        else if (   RTUInt128IsNotEqual(&uResultQ, &s_aTests[i].uQuotient)
                 && RTUInt128IsNotEqual(&uResultR, &s_aTests[i].uRemainder))
        {
            RTTestIFailed("test #%i failed on both counts", i);
        }
        else if (RTUInt128IsNotEqual(&uResultQ, &s_aTests[i].uQuotient))
            RTTestIFailed("test #%i failed: quotient differs:\nExp: %016RX64`%016RX64\nGot: %016RX64`%016RX64",
                          i, s_aTests[i].uQuotient.s.Hi, s_aTests[i].uQuotient.s.Lo, uResultQ.s.Hi, uResultQ.s.Lo );
        else if (RTUInt128IsNotEqual(&uResultR, &s_aTests[i].uRemainder))
            RTTestIFailed("test #%i failed: remainder differs:\nExp: %016RX64`%016RX64\nGot: %016RX64`%016RX64",
                          i, s_aTests[i].uRemainder.s.Hi, s_aTests[i].uRemainder.s.Lo, uResultR.s.Hi, uResultR.s.Lo );

        pResultQ = RTUInt128Div(&uResultQ, &s_aTests[i].uDividend, &s_aTests[i].uDivisor);
        RTTESTI_CHECK(pResultQ == &uResultQ);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResultQ, &s_aTests[i].uQuotient));

        uResultQ = s_aTests[i].uDividend;
        pResultQ = RTUInt128AssignDiv(&uResultQ, &s_aTests[i].uDivisor);
        RTTESTI_CHECK(pResultQ == &uResultQ);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResultQ, &s_aTests[i].uQuotient));


        PRTUINT128U pResultR = RTUInt128Mod(&uResultR, &s_aTests[i].uDividend, &s_aTests[i].uDivisor);
        RTTESTI_CHECK(pResultR == &uResultR);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResultR, &s_aTests[i].uRemainder));

        uResultR = s_aTests[i].uDividend;
        pResultR = RTUInt128AssignMod(&uResultR, &s_aTests[i].uDivisor);
        RTTESTI_CHECK(pResultR == &uResultR);
        RTTESTI_CHECK(RTUInt128IsEqual(&uResultR, &s_aTests[i].uRemainder));
    }
}


static void testUInt64Division(void)
{
    /*
     * Check the results against native code.
     */
    RTTestSub(g_hTest, "RTUInt64DivRem");
    for (uint32_t i = 0; i < _1M / 2; i++)
    {
        uint64_t const uDividend  = RTRandU64Ex(0, UINT64_MAX);
        uint64_t const uDivisor   = RTRandU64Ex(1, UINT64_MAX);
        uint64_t const uQuotient  = uDividend / uDivisor;
        uint64_t const uRemainder = uDividend % uDivisor;
        RTUINT64U Dividend  = { uDividend  };
        RTUINT64U Divisor   = { uDivisor   };
        RTUINT64U Quotient  = { UINT64_MAX };
        RTUINT64U Remainder = { UINT64_MAX };
        RTTESTI_CHECK(RTUInt64DivRem(&Quotient, &Remainder, &Dividend, &Divisor) == &Quotient);
        if (uQuotient != Quotient.u || uRemainder != Remainder.u)
            RTTestIFailed("%RU64 / %RU64 -> %RU64 rem %RU64, expected %RU64 rem %RU64",
                          uDividend, uDivisor, Quotient.u, Remainder.u, uQuotient, uRemainder);
    }
}


static void testUInt32Division(void)
{
    /*
     * Check the results against native code.
     */
    RTTestSub(g_hTest, "RTUInt32DivRem");
    for (uint32_t i = 0; i < _1M / 2; i++)
    {
        uint32_t const uDividend  = RTRandU32Ex(0, UINT32_MAX);
        uint32_t const uDivisor   = RTRandU32Ex(1, UINT32_MAX);
        uint32_t const uQuotient  = uDividend / uDivisor;
        uint32_t const uRemainder = uDividend % uDivisor;
        RTUINT32U Dividend  = { uDividend  };
        RTUINT32U Divisor   = { uDivisor   };
        RTUINT32U Quotient  = { UINT32_MAX };
        RTUINT32U Remainder = { UINT32_MAX };
        RTTESTI_CHECK(RTUInt32DivRem(&Quotient, &Remainder, &Dividend, &Divisor) == &Quotient);
        if (uQuotient != Quotient.u || uRemainder != Remainder.u)
            RTTestIFailed("%u / %u -> %u rem %u, expected %u rem %u",
                          uDividend, uDivisor, Quotient.u, Remainder.u, uQuotient, uRemainder);
    }
}


static void testUInt256Shift(void)
{
    {
        RTTestSub(g_hTest, "RTUInt256ShiftLeft");
        static struct
        {
            RTUINT256U uValue, uResult;
            unsigned   cShift;
        } const s_aTests[] =
        {
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 1   },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 128 },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 127 },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 255 },
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(1, 0, 0, 0), 192 },
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 1, 0, 0), 128 },
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 0, 1, 0), 64 },
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 0, 0, 1), 0},
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(4, 0, 0, 0), 194 },
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 0, 0x10, 0), 68},
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 2, 0, 0), 129},
            { RTUINT256_INIT_C(0, 0, 0, 1), RTUINT256_INIT_C(0, 0, 0, 0x8000000000000000), 63 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0xfdfcfbfaf9f8f7f6, 0xf5f4f3f2f1f0ff3f, 0x3e3d3c3b3a393837, 0x3635343332313000), 8 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0x6f5f4f3f2f1f0ff3, 0xf3e3d3c3b3a39383, 0x7363534333231300, 0), 68 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0x3e3d3c3b3a393837, 0x3635343332313000, 0, 0), 136 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0x6353433323130000, 0, 0, 0), 204 },
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTUINT256U uResult;
            PRTUINT256U pResult = RTUInt256ShiftLeft(&uResult, &s_aTests[i].uValue, s_aTests[i].cShift);
            if (pResult != &uResult)
                RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
            else if (RTUInt256IsNotEqual(&uResult, &s_aTests[i].uResult))
                RTTestIFailed("test #%i failed: \nExp: %016RX64`%016RX64'%016RX64`%016RX64\nGot: %016RX64`%016RX64'%016RX64`%016RX64",
                              i, s_aTests[i].uResult.QWords.qw3, s_aTests[i].uResult.QWords.qw2, s_aTests[i].uResult.QWords.qw1,
                              s_aTests[i].uResult.QWords.qw0, uResult.QWords.qw3, uResult.QWords.qw2, uResult.QWords.qw1,
                              uResult.QWords.qw0);

            uResult = s_aTests[i].uValue;
            pResult = RTUInt256AssignShiftLeft(&uResult, s_aTests[i].cShift);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt256IsEqual(&uResult, &s_aTests[i].uResult));
        }
    }
    {
        RTTestSub(g_hTest, "RTUInt256ShiftRight");
        static struct
        {
            RTUINT256U uValue, uResult;
            unsigned   cShift;
        } const s_aTests[] =
        {
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 1   },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 128 },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 127 },
            { RTUINT256_INIT_C(0, 0, 0, 0), RTUINT256_INIT_C(0, 0, 0, 0), 255 },
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 0, 0, 1), 192},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 0, 1, 0), 128},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 1, 0, 0), 64},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(1, 0, 0, 1), 0},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 0, 0, 4), 190},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 0, 1, 0), 128},
            { RTUINT256_INIT_C(1, 0, 0, 1), RTUINT256_INIT_C(0, 8, 0, 0), 61},
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0x00fefdfcfbfaf9f8, 0xf7f6f5f4f3f2f1f0, 0xff3f3e3d3c3b3a39, 0x3837363534333231), 8 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0, 0x0fefdfcfbfaf9f8f, 0x7f6f5f4f3f2f1f0f, 0xf3f3e3d3c3b3a393), 68 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0, 0, 0x0fefdfcfbfaf9f8f, 0x7f6f5f4f3f2f1f0f), 132 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0, 0, 0, 0xfefdfcfbfaf9f8f7), 192 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0, 0, 0, 0x000fefdfcfbfaf9f), 204 },
            { RTUINT256_INIT_C(0xfefdfcfbfaf9f8f7, 0xf6f5f4f3f2f1f0ff, 0x3f3e3d3c3b3a3938, 0x3736353433323130),
              RTUINT256_INIT_C(0, 0, 0, 1), 255 },
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            RTUINT256U uResult;
            PRTUINT256U pResult = RTUInt256ShiftRight(&uResult, &s_aTests[i].uValue, s_aTests[i].cShift);
            if (pResult != &uResult)
                RTTestIFailed("test #%i returns %p instead of %p", i, pResult, &uResult);
            else if (RTUInt256IsNotEqual(&uResult, &s_aTests[i].uResult))
                RTTestIFailed("test #%i failed: \nExp: %016RX64`%016RX64'%016RX64`%016RX64\nGot: %016RX64`%016RX64'%016RX64`%016RX64",
                              i, s_aTests[i].uResult.QWords.qw3, s_aTests[i].uResult.QWords.qw2, s_aTests[i].uResult.QWords.qw1,
                              s_aTests[i].uResult.QWords.qw0, uResult.QWords.qw3, uResult.QWords.qw2, uResult.QWords.qw1,
                              uResult.QWords.qw0);

            uResult = s_aTests[i].uValue;
            pResult = RTUInt256AssignShiftRight(&uResult, s_aTests[i].cShift);
            RTTESTI_CHECK(pResult == &uResult);
            RTTESTI_CHECK(RTUInt256IsEqual(&uResult, &s_aTests[i].uResult));
        }
    }
}



int main(int argc, char **argv)
{
    RT_NOREF_PV(argv);

    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTBigNum", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /* Init fixed integers. */
    RTTestSub(g_hTest, "RTBigNumInit");
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargePositive, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargePositive, sizeof(g_abLargePositive)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargePositive2, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargePositive, sizeof(g_abLargePositive) - 11), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargePositiveMinus1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargePositiveMinus1, sizeof(g_abLargePositiveMinus1)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargeNegative, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargeNegative, sizeof(g_abLargeNegative)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargeNegative2, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargeNegative, sizeof(g_abLargeNegative) - 9), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_LargeNegativePluss1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abLargeNegativePluss1, sizeof(g_abLargeNegativePluss1)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_64BitPositive1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_ab64BitPositive1, sizeof(g_ab64BitPositive1)), VINF_SUCCESS);

    RTTESTI_CHECK_RC(RTBigNumInitZero(&g_Zero, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_One,       RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x01", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Two,       RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x02", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Three,     RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x03", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Four,      RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x04", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Five,      RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x05", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Ten,       RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x0a", 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_FourtyTwo, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED, "\x2a", 1), VINF_SUCCESS);

    RTTESTI_CHECK_RC(RTBigNumInit(&g_Minus1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_SIGNED,
                                  g_abMinus1, sizeof(g_abMinus1)), VINF_SUCCESS);

    RTTESTI_CHECK_RC(RTBigNumInit(&g_PubKeyExp, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                                  g_abPubKeyExp, sizeof(g_abPubKeyExp)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_PubKeyMod, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                                  g_abPubKeyMod, sizeof(g_abPubKeyMod)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_Signature, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                                  g_abSignature, sizeof(g_abSignature)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTBigNumInit(&g_SignatureDecrypted, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                                  g_abSignatureDecrypted, sizeof(g_abSignatureDecrypted)), VINF_SUCCESS);
    testMoreInit();

    if (RTTestIErrorCount() == 0)
    {
        if (argc != 2)
        {
            /* Test UInt128 first as it may be used by RTBigInt. */
            testUInt128Multiplication();
            testUInt128Division();
            testUInt128Subtraction();
            testUInt128Addition();

            /* Test UInt32 and UInt64 division as it's used by the watcom support code (BIOS, ValKit, OS/2 GAs). */
            testUInt32Division();
            testUInt64Division();

            /* Test some UInt256 bits given what we do above already. */
            testUInt256Shift();

            /* Test the RTBigInt operations. */
            testCompare();
            testSubtraction();
            testAddition();
            testShift();
            testMultiplication();
            testDivision();
            testModulo();
            testExponentiation();
            testModExp();
            testToBytes();
        }

        /* Benchmarks */
        testBenchmarks(argc == 2);

        /* Cleanups. */
        RTTestSub(g_hTest, "RTBigNumDestroy");
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_LargePositive), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_LargePositive2), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_LargeNegative), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_LargeNegative2), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_Zero), VINF_SUCCESS);
        RTTESTI_CHECK_RC(RTBigNumDestroy(&g_64BitPositive1), VINF_SUCCESS);
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

