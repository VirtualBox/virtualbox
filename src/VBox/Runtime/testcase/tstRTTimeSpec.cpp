/* $Id: tstRTTimeSpec.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTTimeSpec and PRTTIME tests.
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
#if !defined(RT_OS_WINDOWS)
# define RTTIME_INCL_TIMEVAL
# define RTTIME_INCL_TIMESPEC
# include <time.h>
# include <sys/time.h>
#endif
#include <iprt/time.h>

#include <iprt/rand.h>
#include <iprt/test.h>
#include <iprt/string.h>


/**
 * Format the time into a string using a static buffer.
 */
static char *ToString(PRTTIME pTime)
{
    static char szBuf[128];
    RTStrPrintf(szBuf, sizeof(szBuf), "%04d-%02d-%02dT%02u:%02u:%02u.%09u [YD%u WD%u UO%d F%#x]",
                pTime->i32Year,
                pTime->u8Month,
                pTime->u8MonthDay,
                pTime->u8Hour,
                pTime->u8Minute,
                pTime->u8Second,
                pTime->u32Nanosecond,
                pTime->u16YearDay,
                pTime->u8WeekDay,
                pTime->offUTC,
                pTime->fFlags);
    return szBuf;
}

#define CHECK_NZ(expr) do { if (!(expr)) { RTTestIFailed("at line %d: %#x\n", __LINE__, #expr); return RTTestSummaryAndDestroy(hTest); } } while (0)

#define TEST_NS(ns) do {\
        CHECK_NZ(RTTimeExplode(&T1, RTTimeSpecSetNano(&Ts1, ns))); \
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%RI64 ns - %s\n", ns, ToString(&T1)); \
        CHECK_NZ(RTTimeImplode(&Ts2, &T1)); \
        if (!RTTimeSpecIsEqual(&Ts2, &Ts1)) \
            RTTestIFailed("FAILURE - %RI64 != %RI64, line no. %d\n", \
                          RTTimeSpecGetNano(&Ts2), RTTimeSpecGetNano(&Ts1), __LINE__); \
    } while (0)

#define TEST_NS_LOCAL(ns) do {\
        CHECK_NZ(RTTimeLocalExplode(&T1, RTTimeSpecSetNano(&Ts1, ns))); \
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%RI64 ns - %s\n", ns, ToString(&T1)); \
        CHECK_NZ(RTTimeImplode(&Ts2, &T1)); \
        if (!RTTimeSpecIsEqual(&Ts2, &Ts1)) \
            RTTestIFailed("FAILURE - %RI64 != %RI64, line no. %d\n", \
                          RTTimeSpecGetNano(&Ts2), RTTimeSpecGetNano(&Ts1), __LINE__); \
    } while (0)

#define TEST_SEC(sec) do {\
        CHECK_NZ(RTTimeExplode(&T1, RTTimeSpecSetSeconds(&Ts1, sec))); \
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%RI64 sec - %s\n", sec, ToString(&T1)); \
        CHECK_NZ(RTTimeImplode(&Ts2, &T1)); \
        if (!RTTimeSpecIsEqual(&Ts2, &Ts1)) \
                RTTestIFailed("FAILURE - %RI64 != %RI64, line no. %d\n", \
                              RTTimeSpecGetNano(&Ts2), RTTimeSpecGetNano(&Ts1), __LINE__); \
    } while (0)

#define CHECK_TIME_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, _Silent)\
    do { \
        if (    (pTime)->i32Year != (_i32Year) \
            ||  (pTime)->u8Month != (_u8Month) \
            ||  (pTime)->u8WeekDay != (_u8WeekDay) \
            ||  (pTime)->u16YearDay != (_u16YearDay) \
            ||  (pTime)->u8MonthDay != (_u8MonthDay) \
            ||  (pTime)->u8Hour != (_u8Hour) \
            ||  (pTime)->u8Minute != (_u8Minute) \
            ||  (pTime)->u8Second != (_u8Second) \
            ||  (pTime)->u32Nanosecond != (_u32Nanosecond) \
            ||  (pTime)->offUTC != (_offUTC) \
            ||  (pTime)->fFlags != (_fFlags) \
            ) \
        { \
            RTTestIFailed("   %s ; line no %d\n" \
                          "!= %04d-%02d-%02dT%02u:%02u:%02u.%09u [YD%u WD%u UO%d F%#x]\n", \
                          ToString(pTime), __LINE__, (_i32Year), (_u8Month), (_u8MonthDay), (_u8Hour), (_u8Minute), \
                          (_u8Second), (_u32Nanosecond), (_u16YearDay), (_u8WeekDay), (_offUTC), (_fFlags)); \
        } \
        else if (!_Silent) \
            RTTestIPrintf(RTTESTLVL_ALWAYS, "=> %s\n", ToString(pTime)); \
    } while (0)
#define CHECK_TIME(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags) CHECK_TIME_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, false)
#define CHECK_TIME_SILENT(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags) CHECK_TIME_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, true)

#define CHECK_TIME_LOCAL_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, _Silent)\
    do { \
        uint32_t fOrigFlags = (pTime)->fFlags; \
        CHECK_NZ(RTTimeConvertToZulu(pTime)); \
        if (    (pTime)->i32Year != (_i32Year) \
            ||  (pTime)->u8Month != (_u8Month) \
            ||  (pTime)->u8WeekDay != (_u8WeekDay) \
            ||  (pTime)->u16YearDay != (_u16YearDay) \
            ||  (pTime)->u8MonthDay != (_u8MonthDay) \
            ||  (pTime)->u8Hour != (_u8Hour) \
            ||  (pTime)->u8Minute != (_u8Minute) \
            ||  (pTime)->u8Second != (_u8Second) \
            ||  (pTime)->u32Nanosecond != (_u32Nanosecond) \
            ||  (pTime)->offUTC != (_offUTC) \
            ||  (fOrigFlags & RTTIME_FLAGS_TYPE_MASK) != RTTIME_FLAGS_TYPE_LOCAL \
            ||  (pTime)->fFlags != (_fFlags) \
            ) \
        { \
            RTTestIFailed("   %s ; line no %d\n" \
                          "!= %04d-%02d-%02dT%02u:%02u:%02u.%09u [YD%u WD%u UO%d F%#x]\n", \
                          ToString(pTime), __LINE__, (_i32Year), (_u8Month), (_u8MonthDay), (_u8Hour), (_u8Minute), \
                          (_u8Second), (_u32Nanosecond), (_u16YearDay), (_u8WeekDay), (_offUTC), (_fFlags)); \
        } \
        else if (!_Silent) \
            RTTestIPrintf(RTTESTLVL_ALWAYS, "=> %s\n", ToString(pTime)); \
    } while (0)
#define CHECK_TIME_LOCAL(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags) CHECK_TIME_LOCAL_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, false)
#define CHECK_TIME_LOCAL_SILENT(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags) CHECK_TIME_LOCAL_EX(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags, true)

#define SET_TIME(pTime, _i32Year, _u8Month, _u8MonthDay, _u8Hour, _u8Minute, _u8Second, _u32Nanosecond, _u16YearDay, _u8WeekDay, _offUTC, _fFlags)\
    do { \
        (pTime)->i32Year = (_i32Year); \
        (pTime)->u8Month = (_u8Month); \
        (pTime)->u8WeekDay = (_u8WeekDay); \
        (pTime)->u16YearDay = (_u16YearDay); \
        (pTime)->u8MonthDay = (_u8MonthDay); \
        (pTime)->u8Hour = (_u8Hour); \
        (pTime)->u8Minute = (_u8Minute); \
        (pTime)->u8Second = (_u8Second); \
        (pTime)->u32Nanosecond = (_u32Nanosecond); \
        (pTime)->offUTC = (_offUTC); \
        (pTime)->fFlags = (_fFlags); \
        RTTestIPrintf(RTTESTLVL_ALWAYS, "   %s\n", ToString(pTime)); \
    } while (0)


int main()
{
    RTTIMESPEC      Now;
    RTTIMESPEC      Ts1;
    RTTIMESPEC      Ts2;
    RTTIME          T1;
    RTTIME          T2;
#ifdef RTTIME_INCL_TIMEVAL
    struct timeval  Tv1;
    struct timeval  Tv2;
    struct timespec Tsp1;
    struct timespec Tsp2;
#endif
    RTTEST          hTest;

    int rc = RTTestInitAndCreate("tstRTTimeSpec", &hTest);
    if (rc)
        return rc;

    /*
     * Simple test with current time.
     */
    RTTestSub(hTest, "Current time (UTC)");
    CHECK_NZ(RTTimeNow(&Now));
    CHECK_NZ(RTTimeExplode(&T1, &Now));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "   %RI64 ns - %s\n", RTTimeSpecGetNano(&Now), ToString(&T1));
    CHECK_NZ(RTTimeImplode(&Ts1, &T1));
    if (!RTTimeSpecIsEqual(&Ts1, &Now))
        RTTestIFailed("%RI64 != %RI64\n", RTTimeSpecGetNano(&Ts1), RTTimeSpecGetNano(&Now));

    /*
     * Simple test with current local time.
     */
    RTTestSub(hTest, "Current time (local)");
    CHECK_NZ(RTTimeLocalNow(&Now));
    CHECK_NZ(RTTimeExplode(&T1, &Now));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "   %RI64 ns - %s\n", RTTimeSpecGetNano(&Now), ToString(&T1));
    CHECK_NZ(RTTimeImplode(&Ts1, &T1));
    if (!RTTimeSpecIsEqual(&Ts1, &Now))
        RTTestIFailed("%RI64 != %RI64\n", RTTimeSpecGetNano(&Ts1), RTTimeSpecGetNano(&Now));

    /*
     * Some simple tests with fixed dates (just checking for smoke).
     */
    RTTestSub(hTest, "Smoke");
    TEST_NS(INT64_C(0));
    CHECK_TIME(&T1, 1970,01,01, 00,00,00,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS(INT64_C(86400000000000));
    CHECK_TIME(&T1, 1970,01,02, 00,00,00,        0,   2, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    TEST_NS(INT64_C(1));
    CHECK_TIME(&T1, 1970,01,01, 00,00,00,        1,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS(INT64_C(-1));
    CHECK_TIME(&T1, 1969,12,31, 23,59,59,999999999, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /*
     * Some local time tests with dates triggering unexpected wraparound bugs in previous code version
     * (on 2nd of a month). Test every hour to cover any TZ of the host OS.
     */
    RTTestSub(hTest, "Wraparound (local)");
    TEST_NS_LOCAL(INT64_C(1522576800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 10,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522580400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 11,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522584000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 12,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522587600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 13,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522591200000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 14,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522594800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 15,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522598400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 16,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522602000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 17,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522605600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 18,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522609200000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 19,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522612800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 20,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522616400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 21,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522620000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 22,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522623600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,01, 23,00,00,        0,  91, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522627200000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  0,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522630800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  1,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522634400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  2,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522638000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  3,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522641600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  4,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522645200000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  5,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522648800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  6,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522652400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  7,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522656000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  8,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522659600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02,  9,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522663200000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02, 10,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522666800000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02, 11,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522670400000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02, 12,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522674000000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02, 13,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    TEST_NS_LOCAL(INT64_C(1522677600000000000));
    CHECK_TIME_LOCAL(&T1, 2018,04,02, 14,00,00,        0,  92, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /*
     * Test the limits.
     */
    RTTestSub(hTest, "Extremes");
    TEST_NS(INT64_MAX);
    TEST_NS(INT64_MIN);
    TEST_SEC(INT64_C(1095379198));
    CHECK_TIME(&T1, 2004, 9,16, 23,59,58,        0, 260, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);
    TEST_SEC(INT64_C(1095379199));
    CHECK_TIME(&T1, 2004, 9,16, 23,59,59,        0, 260, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);
    TEST_SEC(INT64_C(1095379200));
    CHECK_TIME(&T1, 2004, 9,17, 00,00,00,        0, 261, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);
    TEST_SEC(INT64_C(1095379201));
    CHECK_TIME(&T1, 2004, 9,17, 00,00,01,        0, 261, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    /*
     * Test normalization (UTC).
     */
    RTTestSub(hTest, "Normalization (UTC)");
    /* simple */
    CHECK_NZ(RTTimeNow(&Now));
    CHECK_NZ(RTTimeExplode(&T1, &Now));
    T2 = T1;
    CHECK_NZ(RTTimeNormalize(&T1));
    if (memcmp(&T1, &T2, sizeof(T1)))
        RTTestIFailed("simple normalization failed\n");
    CHECK_NZ(RTTimeImplode(&Ts1, &T1));
    CHECK_NZ(RTTimeSpecIsEqual(&Ts1, &Now));

    /* a few partial dates. */
    memset(&T1, 0, sizeof(T1));
    SET_TIME(  &T1, 1970,01,01, 00,00,00,        0,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1970,01,01, 00,00,00,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1970,00,00, 00,00,00,        1,   1, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1970,01,01, 00,00,00,        1,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,12,06, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2007,12,06, 02,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1968,01,30, 00,19,24,        5,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1968,01,30, 00,19,24,        5,  30, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 1969,01,31, 00, 9, 2,        7,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,01,31, 00, 9, 2,        7,  31, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,03,31, 00, 9, 2,        7,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,03,31, 00, 9, 2,        7,  90, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,31, 00,00,00,        9,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,31, 00,00,00,        9, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,30, 00,00,00,       30,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,30, 00,00,00,       30, 364, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 363, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,29, 00,00,00,       30, 363, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 362, 6, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,28, 00,00,00,       30, 362, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,27, 00,00,00,       30,   0, 5, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,27, 00,00,00,       30, 361, 5, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 360, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,26, 00,00,00,       30, 360, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,25, 00,00,00,       12,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,25, 00,00,00,       12, 359, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,24, 00,00,00,       16,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1969,12,24, 00,00,00,       16, 358, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* outside the year table range */
    SET_TIME(  &T1, 1200,01,30, 00,00,00,        2,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1200,01,30, 00,00,00,        2,  30, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2555,11,29, 00,00,00,        2,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2555,11,29, 00,00,00,        2, 333, 5, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2555,00,00, 00,00,00,        3, 333, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2555,11,29, 00,00,00,        3, 333, 5, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* time overflow */
    SET_TIME(  &T1, 1969,12,30, 255,255,255, UINT32_MAX, 364, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 1970,01, 9, 19,19,19,294967295,   9, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* date overflow */
    SET_TIME(  &T1, 2007,11,36, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2007,12,06, 02,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,10,67, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2007,12,06, 02,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,10,98, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2008,01,06, 02,15,23,        1,   6, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2006,24,06, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2007,12,06, 02,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2003,60,37, 02,15,23,        1,   0, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2008,01,06, 02,15,23,        1,   6, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2003,00,00, 02,15,23,        1,1801, 0, 0, 0);
    CHECK_NZ(RTTimeNormalize(&T1));
    CHECK_TIME(&T1, 2007,12,06, 02,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /*
     * Test normalization (local).
     */
    RTTestSub(hTest, "Normalization (local)");
    /* simple */
    CHECK_NZ(RTTimeNow(&Now));
    CHECK_NZ(RTTimeLocalExplode(&T1, &Now));
    T2 = T1;
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    if (memcmp(&T1, &T2, sizeof(T1)))
        RTTestIFailed("simple normalization failed\n");
    CHECK_NZ(RTTimeImplode(&Ts1, &T1));
    CHECK_NZ(RTTimeSpecIsEqual(&Ts1, &Now));

    /* a few partial dates. */
    memset(&T1, 0, sizeof(T1));
    SET_TIME(  &T1, 1970,01,01, 00,00,00,        0,   0, 0, -60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1970,01,01, 01,00,00,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1970,00,00, 00,00,00,        1,   1, 0, -120, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1970,01,01, 02,00,00,        1,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,12,06, 02,15,23,        1,   0, 0, 120, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2007,12,06, 00,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1968,01,30, 00,19,24,        5,   0, 0, -480, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1968,01,30,  8,19,24,        5,  30, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 1969,01,31, 03, 9, 2,        7,   0, 0, 180, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,01,31, 00, 9, 2,        7,  31, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,03,31, 00, 9, 2,        7,   0, 0, -60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,03,31, 01, 9, 2,        7,  90, 0, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,30, 18,00,00,        9,   0, 0, -360, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,31, 00,00,00,        9, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,29, 12,00,00,       30,   0, 0, -720, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,30, 00,00,00,       30, 364, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 363, 0, 30, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,28, 23,30,00,       30, 362, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 362, 6, -60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,28, 01,00,00,       30, 362, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,27, 00,00,00,       30,   0, 5, -120, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,27, 02,00,00,       30, 361, 5, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,00,00, 00,00,00,       30, 360, 0, -120, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,26, 02,00,00,       30, 360, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,25, 00,00,00,       12,   0, 0, 15, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,24, 23,45,00,       12, 358, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 1969,12,24, 00,00,00,       16,   0, 0, -15, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1969,12,24, 00,15,00,       16, 358, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* outside the year table range */
    SET_TIME(  &T1, 1200,01,30, 00,00,00,        2,   0, 0, -720, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1200,01,30, 12,00,00,        2,  30, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2555,11,29, 00,00,00,        2,   0, 0, -480, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2555,11,29,  8,00,00,        2, 333, 5, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2555,00,00, 00,00,00,        3, 333, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2555,11,28, 23,00,00,        3, 332, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* time overflow */
    SET_TIME(  &T1, 1969,12,30, 255,255,255, UINT32_MAX, 364, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 1970,01, 9, 18,19,19,294967295,   9, 4, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /* date overflow */
    SET_TIME(  &T1, 2007,11,36, 02,15,23,        1,   0, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2007,12,06, 01,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,10,67, 02,15,23,        1,   0, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2007,12,06, 01,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2007,10,98, 02,15,23,        1,   0, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2008,01,06, 01,15,23,        1,   6, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2006,24,06, 02,15,23,        1,   0, 0, 60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2007,12,06, 01,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    SET_TIME(  &T1, 2003,60,37, 02,15,23,        1,   0, 0, -60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2008,01,06, 03,15,23,        1,   6, 6, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);

    SET_TIME(  &T1, 2003,00,00, 02,15,23,        1,1801, 0, -60, 0);
    CHECK_NZ(RTTimeLocalNormalize(&T1));
    CHECK_TIME_LOCAL(&T1, 2007,12,06, 03,15,23,        1, 340, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);

    /*
     * Test UTC and local time explode/implode round trips every 29 minutes for 3 years.
     * Relies heavily on correct behavior of RTTimeNormalize and does limited sanity checking.
     */
    RTTestSub(hTest, "Wraparound 3 year (UTC+local), silent");
    RTTimeSpecSetNano(&Ts1, INT64_C(1420070400000000000));
    RTTIME Tcheck;
    memset(&Tcheck, 0, sizeof(Tcheck));
    Tcheck.i32Year = 2015;
    Tcheck.u16YearDay = 1;
    CHECK_NZ(RTTimeNormalize(&Tcheck));
    while (Tcheck.i32Year <= 2017)
    {
        if (RTTimeIsLeapYear(Tcheck.i32Year))
        {
            if (!(Tcheck.fFlags & RTTIME_FLAGS_LEAP_YEAR))
                RTTestIFailed("FAILURE - %d is not marked as a leap year, line no. %d\n",
                              Tcheck.i32Year, __LINE__);
        }
        else
        {
            if (!(Tcheck.fFlags & RTTIME_FLAGS_COMMON_YEAR))
                RTTestIFailed("FAILURE - %d is not marked as a common year, line no. %d\n",
                              Tcheck.i32Year, __LINE__);
        }

        CHECK_NZ(RTTimeExplode(&T1, &Ts1));
        CHECK_NZ(RTTimeImplode(&Ts2, &T1));
        if (!RTTimeSpecIsEqual(&Ts2, &Ts1))
            RTTestIFailed("FAILURE - %RI64 != %RI64, line no. %d\n",
                          RTTimeSpecGetNano(&Ts2), RTTimeSpecGetNano(&Ts1), __LINE__);
        CHECK_TIME_SILENT(&T1, Tcheck.i32Year, Tcheck.u8Month, Tcheck.u8MonthDay, Tcheck.u8Hour, Tcheck.u8Minute, Tcheck.u8Second, Tcheck.u32Nanosecond, Tcheck.u16YearDay, Tcheck.u8WeekDay, Tcheck.offUTC, Tcheck.fFlags);

        CHECK_NZ(RTTimeLocalExplode(&T1, &Ts1));
        CHECK_NZ(RTTimeImplode(&Ts2, &T1));
        if (!RTTimeSpecIsEqual(&Ts2, &Ts1))
            RTTestIFailed("FAILURE - %RI64 != %RI64, line no. %d\n",
                          RTTimeSpecGetNano(&Ts2), RTTimeSpecGetNano(&Ts1), __LINE__);
        CHECK_TIME_LOCAL_SILENT(&T1, Tcheck.i32Year, Tcheck.u8Month, Tcheck.u8MonthDay, Tcheck.u8Hour, Tcheck.u8Minute, Tcheck.u8Second, Tcheck.u32Nanosecond, Tcheck.u16YearDay, Tcheck.u8WeekDay, Tcheck.offUTC, Tcheck.fFlags);

        RTTimeSpecAddNano(&Ts1, 29 * RT_NS_1MIN);
        Tcheck.u8Minute += 29;
        CHECK_NZ(RTTimeNormalize(&Tcheck));
    }

    /*
     * Conversions.
     */
#define CHECK_NSEC(Ts1, T2) \
    do { \
        RTTIMESPEC TsTmp; \
        RTTESTI_CHECK_MSG( RTTimeSpecGetNano(&(Ts1)) == RTTimeSpecGetNano(RTTimeImplode(&TsTmp, &(T2))), \
                          ("line %d: %RI64, %RI64\n", __LINE__, \
                           RTTimeSpecGetNano(&(Ts1)),   RTTimeSpecGetNano(RTTimeImplode(&TsTmp, &(T2)))) ); \
    } while (0)
    RTTestSub(hTest, "Conversions, positive");
    SET_TIME(&T1, 1980,01,01, 00,00,00,        0,   1, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetDosSeconds(&Ts2,    0) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetDosSeconds(&Ts2) == 0);
    CHECK_NSEC(Ts2, T1);

    SET_TIME(&T1, 1980,01,01, 00,00,00,        0,   1, 1, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_LEAP_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetNtTime(&Ts2,    INT64_C(119600064000000000)) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetNtTime(&Ts2) == INT64_C(119600064000000000));
    CHECK_NSEC(Ts2, T1);

    SET_TIME(&T1, 1970,01,01, 00,00,01,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetSeconds(&Ts2,    1) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetSeconds(&Ts2) == 1);
    CHECK_NSEC(Ts2, T1);

    SET_TIME(&T1, 1970,01,01, 00,00,01,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetMilli(&Ts2,    1000) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetMilli(&Ts2) == 1000);
    CHECK_NSEC(Ts2, T1);

    SET_TIME(&T1, 1970,01,01, 00,00,01,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetMicro(&Ts2,    1000000) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetMicro(&Ts2) == 1000000);
    CHECK_NSEC(Ts2, T1);

    SET_TIME(&T1, 1970,01,01, 00,00,01,        0,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK(RTTimeSpecSetNano(&Ts2,    1000000000) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetNano(&Ts2) == 1000000000);
    CHECK_NSEC(Ts2, T1);

#ifdef RTTIME_INCL_TIMEVAL
    SET_TIME(&T1, 1970,01,01, 00,00,01,     5000,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    Tv1.tv_sec  = 1;
    Tv1.tv_usec = 5;
    RTTESTI_CHECK(RTTimeSpecSetTimeval(&Ts2, &Tv1) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetMicro(&Ts2) == 1000005);
    CHECK_NSEC(Ts2, T1);
    RTTESTI_CHECK(RTTimeSpecGetTimeval(&Ts2, &Tv2) == &Tv2);
    RTTESTI_CHECK(Tv1.tv_sec == Tv2.tv_sec); RTTESTI_CHECK(Tv1.tv_usec == Tv2.tv_usec);
#endif

#ifdef RTTIME_INCL_TIMESPEC
    SET_TIME(&T1, 1970,01,01, 00,00,01,        5,   1, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    Tsp1.tv_sec  = 1;
    Tsp1.tv_nsec = 5;
    RTTESTI_CHECK(RTTimeSpecSetTimespec(&Ts2, &Tsp1) == &Ts2);
    RTTESTI_CHECK(RTTimeSpecGetNano(&Ts2) == 1000000005);
    CHECK_NSEC(Ts2, T1);
    RTTESTI_CHECK(RTTimeSpecGetTimespec(&Ts2, &Tsp2) == &Tsp2);
    RTTESTI_CHECK(Tsp1.tv_sec == Tsp2.tv_sec); RTTESTI_CHECK(Tsp1.tv_nsec == Tsp2.tv_nsec);
#endif


    RTTestSub(hTest, "Conversions, negative");

#ifdef RTTIME_INCL_TIMEVAL
    SET_TIME(&T1, 1969,12,31, 23,59,58,999995000, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    Tv1.tv_sec  = -2;
    Tv1.tv_usec = 999995;
    RTTESTI_CHECK(RTTimeSpecSetTimeval(&Ts2, &Tv1) == &Ts2);
    RTTESTI_CHECK_MSG(RTTimeSpecGetMicro(&Ts2) == -1000005, ("%RI64\n", RTTimeSpecGetMicro(&Ts2)));
    CHECK_NSEC(Ts2, T1);
    RTTESTI_CHECK(RTTimeSpecGetTimeval(&Ts2, &Tv2) == &Tv2);
    RTTESTI_CHECK(Tv1.tv_sec == Tv2.tv_sec); RTTESTI_CHECK(Tv1.tv_usec == Tv2.tv_usec);
#endif

#ifdef RTTIME_INCL_TIMESPEC
    SET_TIME(&T1, 1969,12,31, 23,59,58,999999995, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    Tsp1.tv_sec  = -2;
    Tsp1.tv_nsec = 999999995;
    RTTESTI_CHECK(RTTimeSpecSetTimespec(&Ts2, &Tsp1) == &Ts2);
    RTTESTI_CHECK_MSG(RTTimeSpecGetNano(&Ts2) == -1000000005, ("%RI64\n", RTTimeSpecGetMicro(&Ts2)));
    CHECK_NSEC(Ts2, T1);
    RTTESTI_CHECK(RTTimeSpecGetTimespec(&Ts2, &Tsp2) == &Tsp2);
    RTTESTI_CHECK(Tsp1.tv_sec == Tsp2.tv_sec); RTTESTI_CHECK(Tsp1.tv_nsec == Tsp2.tv_nsec);
#endif

    /*
     * Test some string formatting too, while we're here...
     */
    RTTestSub(hTest, "Formatting");
    char szValue[256];
#define RTTESTI_CHECK_FMT(a_FmtCall, a_szExpect) \
        do { \
            ssize_t cchResult = a_FmtCall; \
            if (cchResult != sizeof(a_szExpect) - 1 || strcmp(szValue, a_szExpect) != 0) \
                RTTestFailed(hTest, "Got %s (%zd bytes) expected %s (%zu bytes); line " RT_XSTR(__LINE__), \
                             szValue, cchResult, a_szExpect, sizeof(a_szExpect) - 1); \
        } while (0)
#define RTTESTI_CHECK_FROM(a_FromCall) \
        do { \
            RTRandBytes(&T2, sizeof(T2)); \
            PRTTIME pResult = a_FromCall; \
            if (!pResult) \
                RTTestFailed(hTest, "%s failed on line " RT_XSTR(__LINE__), #a_FromCall); \
            else if (memcmp(&T1, &T2, sizeof(T2)) != 0) \
                RTTestFailed(hTest, "%s produced incorrect result on line " RT_XSTR(__LINE__)": %s", #a_FromCall, ToString(&T2)); \
        } while (0)
    SET_TIME(&T1, 1969,12,31, 23,59,58,999995000, 365, 2, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK_FMT(RTTimeToRfc2822(&T1, szValue, sizeof(szValue),                    0), "Wed, 31 Dec 1969 23:59:58 -0000");
    RTTESTI_CHECK_FMT(RTTimeToRfc2822(&T1, szValue, sizeof(szValue), RTTIME_RFC2822_F_GMT), "Wed, 31 Dec 1969 23:59:58 GMT");
    RTTESTI_CHECK_FMT(RTTimeToStringEx(&T1, szValue, sizeof(szValue), 0), "1969-12-31T23:59:58Z");
    RTTESTI_CHECK_FMT(RTTimeToStringEx(&T1, szValue, sizeof(szValue), 1), "1969-12-31T23:59:58.9Z");
    RTTESTI_CHECK_FMT(RTTimeToStringEx(&T1, szValue, sizeof(szValue), 5), "1969-12-31T23:59:58.99999Z");
    RTTESTI_CHECK_FMT(RTTimeToStringEx(&T1, szValue, sizeof(szValue), 9), "1969-12-31T23:59:58.999995000Z");
    RTTESTI_CHECK_FROM(RTTimeFromString(&T2, "1969-12-31T23:59:58.999995000Z"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Wed, 31 Dec 1969 23:59:58.999995 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Wed, 31 Dec 69 23:59:58.999995 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "31 Dec 69 23:59:58.999995 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "31 Dec 1969 23:59:58.999995 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "31 dec 1969 23:59:58.999995 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "wEd, 31 Dec 69 23:59:58.999995 UT"));

    SET_TIME(&T1, 2018, 9, 6,  4, 9, 8,        0, 249, 3, 0, RTTIME_FLAGS_TYPE_UTC | RTTIME_FLAGS_COMMON_YEAR);
    RTTESTI_CHECK_FMT(RTTimeToRfc2822(&T1, szValue, sizeof(szValue),                    0), "Thu, 6 Sep 2018 04:09:08 -0000");
    RTTESTI_CHECK_FMT(RTTimeToStringEx(&T1, szValue, sizeof(szValue), 0), "2018-09-06T04:09:08Z");
    RTTESTI_CHECK_FROM(RTTimeFromString(&T2, "2018-09-06T04:09:08Z"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Thu, 6 Sep 2018 04:09:08 -0000"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Thu, 6 Sep 2018 04:09:08 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Thu, 06 Sep 2018 04:09:08 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, "Thu, 00006 Sep 2018 04:09:08 GMT"));
    RTTESTI_CHECK_FROM(RTTimeFromRfc2822(&T2, " 00006 Sep 2018 04:09:08 GMT "));


    /*
     * Duration.
     */
    RTTestSub(hTest, "Duration Formatting");
    static struct { int64_t cNanoSecs; uint8_t cFractionDigits; const char *pszExpect; } const s_aDuration[] =
    {
        {   0, 0, "PT0S" },
        {   0, 9, "PT0S" },
        {   RT_NS_1WEEK*52 + RT_NS_1DAY*3 + RT_NS_1HOUR*11 + RT_NS_1MIN*29 + RT_NS_1SEC_64*42 + 123456789, 9,
            "P52W3DT11H29M42.123456789S" },
        {   RT_NS_1WEEK*52 + RT_NS_1DAY*3 + RT_NS_1HOUR*11 + RT_NS_1MIN*29 + RT_NS_1SEC_64*42 + 123456789, 0,
            "P52W3DT11H29M42S" },
        {   RT_NS_1WEEK*9999 + RT_NS_1SEC_64*22 + 905964245, 0,
            "P9999WT0H0M22S" },
        {   RT_NS_1WEEK*9999 + RT_NS_1SEC_64*22 + 905964245, 6,
            "P9999WT0H0M22.905964S" },
        {   -(int64_t)(RT_NS_1WEEK*9999 + RT_NS_1SEC_64*22 + 905964245), 7,
            "-P9999WT0H0M22.9059642S" },
        {   -(int64_t)(RT_NS_1WEEK*9999 + RT_NS_1SEC_64*22 + 905964245), 7,
            "-P9999WT0H0M22.9059642S" },
        {   RT_NS_1WEEK*1 + RT_NS_1DAY*1 + RT_NS_1HOUR*1 + RT_NS_1MIN*2 + RT_NS_1SEC_64*1 + 111111111, 9,
            "P1W1DT1H2M1.111111111S" },
        {   1, 9, "PT0.000000001S" },
        {   1, 3, "PT0.000S" },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aDuration); i++)
    {
        RTTIMESPEC TimeSpec;
        RTTimeSpecSetNano(&TimeSpec, s_aDuration[i].cNanoSecs);
        ssize_t cchRet = RTTimeFormatDurationEx(szValue, sizeof(szValue), &TimeSpec, s_aDuration[i].cFractionDigits);
        if (   cchRet != (ssize_t)strlen(s_aDuration[i].pszExpect)
            || memcmp(szValue, s_aDuration[i].pszExpect, cchRet + 1) != 0)
            RTTestIFailed("RTTimeFormatDurationEx/#%u: cchRet=%zd\n"
                          "  szValue: '%s', length %zu\n"
                          " expected: '%s', length %zu",
                          i, cchRet, szValue, strlen(szValue), s_aDuration[i].pszExpect, strlen(s_aDuration[i].pszExpect));
    }


    /*
     * Check that RTTimeZoneGetCurrent works (not really timespec, but whatever).
     */
    RTTestSub(hTest, "RTTimeZoneGetCurrent");
    szValue[0] = '\0';
    RTTESTI_CHECK_RC(RTTimeZoneGetCurrent(szValue, sizeof(szValue)), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "TimeZone: %s", szValue);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

