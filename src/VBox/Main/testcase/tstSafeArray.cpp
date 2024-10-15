/* $Id: tstSafeArray.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * API Glue Testcase - SafeArray.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <VBox/com/array.h>
#include <VBox/com/string.h>

#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/uni.h>


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstSafeArray", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        /* Sizes / Pre-allocations. */
        com::SafeArray<int> aInt;
        RTTESTI_CHECK(aInt.size() == 0);

        com::SafeArray<int> aInt2(42);
        RTTESTI_CHECK(aInt2.size() == 42);
        aInt2.setNull();
        RTTESTI_CHECK(aInt2.size() == 0);
        aInt2.resize(42);
        RTTESTI_CHECK(aInt2.size() == 42);
        aInt2.setNull();

        com::SafeArray<int> aInt3((size_t)0);
        RTTESTI_CHECK(aInt3.size() == 0);
        aInt3.setNull();
        RTTESTI_CHECK(aInt3.size() == 0);

        /* Push to back. */
        int aPushToBack[] = { 51, 52, 53 };
        for (size_t i = 0; i < RT_ELEMENTS(aPushToBack); i++)
        {
            RTTESTI_CHECK(aInt.push_back(aPushToBack[i]));
            RTTESTI_CHECK(aInt.size() == i + 1);
            RTTESTI_CHECK(aInt[i] == aPushToBack[i]);
        }
        for (size_t i = 0; i < RT_ELEMENTS(aPushToBack); i++)
            RTTESTI_CHECK_MSG(aInt[i] == aPushToBack[i], ("Got %d, expected %d\n", aInt[i], aPushToBack[i]));

        aInt.setNull();

        /* Push to front. */
        int aPushToFront[] = { 41, 42, 43 };
        for (size_t i = 0; i < RT_ELEMENTS(aPushToFront); i++)
        {
            RTTESTI_CHECK(aInt.push_front(aPushToFront[i]));
            RTTESTI_CHECK(aInt.size() == i + 1);
            RTTESTI_CHECK(aInt[0] == aPushToFront[i]);
        }
        for (size_t i = 0; i < RT_ELEMENTS(aPushToFront); i++)
            RTTESTI_CHECK_MSG(aInt[i] == aPushToFront[RT_ELEMENTS(aPushToFront) - i - 1],
                              ("Got %d, expected %d\n", aInt[i], aPushToFront[RT_ELEMENTS(aPushToFront) - i - 1]));

        /* A bit more data. */
        aInt.setNull();
        for (size_t i = 0; i < RTRandU32Ex(_4K, _64M); i++)
        {
            RTTESTI_CHECK(aInt.push_front(42));
            RTTESTI_CHECK(aInt.push_back(41));
            RTTESTI_CHECK(aInt.size() == (i + 1) * 2);
        }
        aInt.setNull();

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

