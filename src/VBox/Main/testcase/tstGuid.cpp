/* $Id: tstGuid.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * API Glue Testcase - Guid.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/Guid.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/uni.h>


static void test1(RTTEST hTest)
{
    RTTestSub(hTest, "Basics");

#define CHECK(expr) RTTESTI_CHECK(expr)
#define CHECK_DUMP(expr, value) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%s\"", __LINE__, #expr, value); \
    } while (0)

#define CHECK_DUMP_I(expr) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%d\"", __LINE__, #expr, expr); \
    } while (0)
#define CHECK_EQUAL(Str, szExpect) \
    do { \
        if (!(Str).equals(szExpect)) \
            RTTestIFailed("line %u: expected \"%s\" got \"%s\"", __LINE__, szExpect, (Str).c_str()); \
    } while (0)
#define CHECK_EQUAL_I(iRes, iExpect) \
    do { \
        if (iRes != iExpect) \
            RTTestIFailed("line %u: expected \"%zd\" got \"%zd\"", __LINE__, iExpect, iRes); \
    } while (0)

    com::Guid zero;
    CHECK(zero.isZero());

    com::Guid copyZero(zero);
    CHECK(copyZero.isZero());

    com::Guid assignZero(zero);
    CHECK(assignZero.isZero());

    com::Guid random;
    random.create();
    CHECK(!random.isZero());

    com::Guid copyRandom(random);
    CHECK(!copyRandom.isZero());

    com::Guid assignRandom(random);
    CHECK(!assignRandom.isZero());

    /** @todo extend this a lot, it needs to cover many more cases */

#undef CHECK
#undef CHECK_DUMP
#undef CHECK_DUMP_I
#undef CHECK_EQUAL
}


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstGuid", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        test1(hTest);

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

