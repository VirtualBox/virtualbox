/* $Id: tstRTUuid.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - UUID.
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
#include <iprt/test.h>
#include <iprt/stream.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/utf16.h>


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTUuid", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);


#define CHECK_RC() \
    do { if (RT_FAILURE(rc)) { RTTestFailed(hTest, "line %d: rc=%Rrc", __LINE__, rc); } } while (0)

    RTTestSub(hTest, "RTUuidClear & RTUuisIsNull");
    RTUUID UuidNull;
    rc = RTUuidClear(&UuidNull); CHECK_RC();

    RTTEST_CHECK(hTest, RTUuidIsNull(&UuidNull));
    RTTEST_CHECK(hTest, RTUuidCompare(&UuidNull, &UuidNull) == 0);

    RTTestSub(hTest, "RTUuidCreate");
    RTUUID Uuid;
    rc = RTUuidCreate(&Uuid); CHECK_RC();
    RTTEST_CHECK(hTest, !RTUuidIsNull(&Uuid));
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid) == 0);
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &UuidNull) > 0);
    RTTEST_CHECK(hTest, RTUuidCompare(&UuidNull, &Uuid) < 0);

    RTTestSub(hTest, "RTUuidToStr");
    char sz[RTUUID_STR_LENGTH];
    rc = RTUuidToStr(&Uuid, sz, sizeof(sz)); CHECK_RC();
    RTTEST_CHECK(hTest, strlen(sz) == RTUUID_STR_LENGTH - 1);
    RTTestPrintf(hTest, RTTESTLVL_INFO, "UUID=%s\n", sz);

    RTTestSub(hTest, "RTUuidFromStr");
    RTUUID Uuid2;
    rc = RTUuidFromStr(&Uuid2, sz); CHECK_RC();
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);

    char *psz = (char *)RTTestGuardedAllocTail(hTest, RTUUID_STR_LENGTH);
    if (psz)
    {
        RTStrPrintf(psz, RTUUID_STR_LENGTH, "%s", sz);
        RTTESTI_CHECK_RC(RTUuidFromStr(&Uuid2, psz), VINF_SUCCESS);
        RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);
        for (unsigned off = 1; off < RTUUID_STR_LENGTH; off++)
        {
            char *psz2 = psz + off;
            RTStrPrintf(psz2, RTUUID_STR_LENGTH - off, "%s", sz);
            RTTESTI_CHECK_RC(RTUuidFromStr(&Uuid2, psz2), VERR_INVALID_UUID_FORMAT);
        }
        RTTestGuardedFree(hTest, psz);
    }

    RTUuidClear(&Uuid2);
    char sz2[RTUUID_STR_LENGTH + 2];
    RTStrPrintf(sz2, sizeof(sz2), "{%s}", sz);
    rc = RTUuidFromStr(&Uuid2, sz2); CHECK_RC();
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);

    psz = (char *)RTTestGuardedAllocTail(hTest, RTUUID_STR_LENGTH + 2);
    if (psz)
    {
        RTStrPrintf(psz, RTUUID_STR_LENGTH + 2, "{%s}", sz);
        RTTESTI_CHECK_RC(RTUuidFromStr(&Uuid2, psz), VINF_SUCCESS);
        RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);
        for (unsigned off = 1; off < RTUUID_STR_LENGTH + 2; off++)
        {
            char *psz2 = psz + off;
            RTStrPrintf(psz2, RTUUID_STR_LENGTH + 2 - off, "{%s}", sz);
            RTTESTI_CHECK_RC(RTUuidFromStr(&Uuid2, psz2), VERR_INVALID_UUID_FORMAT);
        }
        RTTestGuardedFree(hTest, psz);
    }

    RTTestSub(hTest, "RTUuidToUtf16");
    RTUTF16 wsz[RTUUID_STR_LENGTH];
    rc = RTUuidToUtf16(&Uuid, wsz, sizeof(wsz)); CHECK_RC();
    RTTEST_CHECK(hTest, RTUtf16Len(wsz) == RTUUID_STR_LENGTH - 1);

    RTTestSub(hTest, "RTUuidFromUtf16");
    rc = RTUuidFromUtf16(&Uuid2, wsz); CHECK_RC();
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);

    RTUTF16 *pwsz;
    rc = RTStrToUtf16(sz2, &pwsz);
    RTTEST_CHECK(hTest, rc == VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_RC(RTUuidFromUtf16(&Uuid2, pwsz), VINF_SUCCESS);
        RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);
        RTUTF16 *pwsz2 = (RTUTF16*)RTTestGuardedAllocTail(hTest, 2 * (RTUUID_STR_LENGTH + 2));
        if (pwsz2)
        {
            memcpy(pwsz2, pwsz, 2 * (RTUUID_STR_LENGTH + 2));
            RTTESTI_CHECK_RC(RTUuidFromUtf16(&Uuid2, pwsz2), VINF_SUCCESS);
            RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid2) == 0);
            for (unsigned off = 1; off < RTUUID_STR_LENGTH + 2; off++)
            {
                RTUTF16 *pwsz3 = pwsz2 + off;
                memcpy(pwsz3, pwsz, 2 * (RTUUID_STR_LENGTH + 1 - off));
                pwsz3[RTUUID_STR_LENGTH + 1 - off] = 0;
                RTTESTI_CHECK_RC(RTUuidFromUtf16(&Uuid2, pwsz3), VERR_INVALID_UUID_FORMAT);
            }
            RTTestGuardedFree(hTest, pwsz2);
        }
        RTUtf16Free(pwsz);
    }

    RTTestSub(hTest, "RTUuidCompareStr");
    RTTEST_CHECK(hTest, RTUuidCompareStr(&Uuid, sz) == 0);
    RTTEST_CHECK(hTest, RTUuidCompareStr(&Uuid, "00000000-0000-0000-0000-000000000000") > 0);
    RTTEST_CHECK(hTest, RTUuidCompareStr(&UuidNull, "00000000-0000-0000-0000-000000000000") == 0);

    RTTestSub(hTest, "RTUuidCompare2Strs");
    RTTEST_CHECK(hTest, RTUuidCompare2Strs(sz, sz) == 0);
    RTTEST_CHECK(hTest, RTUuidCompare2Strs(sz, "00000000-0000-0000-0000-000000000000") > 0);
    RTTEST_CHECK(hTest, RTUuidCompare2Strs("00000000-0000-0000-0000-000000000000", sz) < 0);
    RTTEST_CHECK(hTest, RTUuidCompare2Strs("00000000-0000-0000-0000-000000000000", "00000000-0000-0000-0000-000000000000") == 0);
    RTTEST_CHECK(hTest, RTUuidCompare2Strs("d95d883b-f91d-4ce5-a5c5-d08bb6a85dec", "a56193c7-3e0b-4c03-9d66-56efb45082f7") > 0);
    RTTEST_CHECK(hTest, RTUuidCompare2Strs("a56193c7-3e0b-4c03-9d66-56efb45082f7", "d95d883b-f91d-4ce5-a5c5-d08bb6a85dec") < 0);

    /*
     * Check the binary representation.
     */
    RTTestSub(hTest, "Binary representation");
    RTUUID Uuid3;
    Uuid3.au8[0]  = 0x01;
    Uuid3.au8[1]  = 0x23;
    Uuid3.au8[2]  = 0x45;
    Uuid3.au8[3]  = 0x67;
    Uuid3.au8[4]  = 0x89;
    Uuid3.au8[5]  = 0xab;
    Uuid3.au8[6]  = 0xcd;
    Uuid3.au8[7]  = 0x4f;
    Uuid3.au8[8]  = 0x10;
    Uuid3.au8[9]  = 0xb2;
    Uuid3.au8[10] = 0x54;
    Uuid3.au8[11] = 0x76;
    Uuid3.au8[12] = 0x98;
    Uuid3.au8[13] = 0xba;
    Uuid3.au8[14] = 0xdc;
    Uuid3.au8[15] = 0xfe;
    Uuid3.Gen.u8ClockSeqHiAndReserved = (Uuid3.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
    Uuid3.Gen.u16TimeHiAndVersion = (Uuid3.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
    const char *pszUuid3 = "67452301-ab89-4fcd-90b2-547698badcfe";
    rc = RTUuidToStr(&Uuid3, sz, sizeof(sz)); CHECK_RC();
    RTTEST_CHECK(hTest, strcmp(sz, pszUuid3) == 0);
    rc = RTUuidFromStr(&Uuid, pszUuid3); CHECK_RC();
    RTTEST_CHECK(hTest, RTUuidCompare(&Uuid, &Uuid3) == 0);
    RTTEST_CHECK(hTest, memcmp(&Uuid3, &Uuid, sizeof(Uuid)) == 0);

    /*
     * checking the clock seq and time hi and version bits...
     */
    RTTestSub(hTest, "Clock seq, time hi, version bits");
    RTUUID Uuid4Changes;
    Uuid4Changes.au64[0] = 0;
    Uuid4Changes.au64[1] = 0;

    RTUUID Uuid4Prev;
    RTUuidCreate(&Uuid4Prev);

    for (unsigned i = 0; i < 1024; i++)
    {
        RTUUID Uuid4;
        RTUuidCreate(&Uuid4);

        Uuid4Changes.au64[0] |= Uuid4.au64[0] ^ Uuid4Prev.au64[0];
        Uuid4Changes.au64[1] |= Uuid4.au64[1] ^ Uuid4Prev.au64[1];

#if 0   /** @todo make a bit string/dumper similar to %Rhxs/d. */
        RTPrintf("tstUuid: %d %d %d %d-%d %d %d %d  %d %d %d %d-%d %d %d %d ; %d %d %d %d-%d %d %d %d  %d %d %d %d-%d %d %d %d\n",
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(0)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(1)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(2)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(3)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(4)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(5)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(6)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(7)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(8)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(9)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(10)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(11)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(12)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(13)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(14)),
                 !!(Uuid4.Gen.u16ClockSeq & RT_BIT(15)),

                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(0)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(1)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(2)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(3)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(4)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(5)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(6)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(7)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(8)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(9)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(10)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(11)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(12)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(13)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(14)),
                 !!(Uuid4.Gen.u16TimeHiAndVersion & RT_BIT(15))
                 );
#endif
        Uuid4Prev = Uuid4;
    }

    RTUUID Uuid4Fixed;
    Uuid4Fixed.au64[0] = ~Uuid4Changes.au64[0];
    Uuid4Fixed.au64[1] = ~Uuid4Changes.au64[1];
    RTTestPrintf(hTest, RTTESTLVL_INFO, "fixed bits: %RTuuid (mask)\n", &Uuid4Fixed);
    RTTestPrintf(hTest, RTTESTLVL_INFO, "tstUuid:        raw: %.*Rhxs\n", sizeof(Uuid4Fixed), &Uuid4Fixed);

    Uuid4Prev.au64[0] &= Uuid4Fixed.au64[0];
    Uuid4Prev.au64[1] &= Uuid4Fixed.au64[1];
    RTTestPrintf(hTest, RTTESTLVL_INFO, "tstUuid: fixed bits: %RTuuid (value)\n", &Uuid4Prev);
    RTTestPrintf(hTest, RTTESTLVL_INFO, "tstUuid:        raw: %.*Rhxs\n", sizeof(Uuid4Prev), &Uuid4Prev);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

