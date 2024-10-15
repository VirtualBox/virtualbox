/* $Id: tstRTStrSplit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - String splitting.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#include <iprt/test.h>
#include <iprt/mem.h>
#include <iprt/stream.h>


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTStrSplit", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /* Invalid stuff. */
    RTTestDisableAssertions(hTest);
    RTTEST_CHECK_RC(hTest, RTStrSplit(NULL, 0, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTEST_CHECK_RC(hTest, RTStrSplit("foo", 0, NULL, NULL, NULL), VERR_INVALID_PARAMETER);
    RTTEST_CHECK_RC(hTest, RTStrSplit("foo", 42, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTestRestoreAssertions(hTest);

    char **papszStrings = NULL;
    size_t cStrings = 0;

#define DO_CLEANUP() \
    for (size_t i = 0; i < cStrings; ++i) \
        RTStrFree(papszStrings[i]); \
    RTMemFree(papszStrings);
    cStrings = 0;

    /* Empty stuff. */
    const char szEmpty[] = "";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szEmpty, sizeof(szEmpty), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 0);
    DO_CLEANUP();

    /* No separator given. */
    const char szNoSep[] = "foo";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szNoSep, sizeof(szNoSep), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 1);
    RTTEST_CHECK(hTest, RTStrICmp(papszStrings[0], "foo") == 0);
    DO_CLEANUP();

    /* Single string w/ separator. */
    const char szWithSep[] = "foo\r\n";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szWithSep, sizeof(szWithSep), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 1);
    RTTEST_CHECK(hTest, papszStrings && RTStrICmp(papszStrings[0], "foo") == 0);
    DO_CLEANUP();

    /* Multiple strings w/ separator. */
    const char szWithSep2[] = "foo\r\nbar";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szWithSep2, sizeof(szWithSep2), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 2);
    RTTEST_CHECK(hTest,    cStrings == 2
                        && papszStrings
                        && RTStrICmp(papszStrings[0], "foo") == 0
                        && RTStrICmp(papszStrings[1], "bar") == 0);
    DO_CLEANUP();

    /* Multiple strings w/ two consequtive separators. */
    const char szWithSep3[] = "foo\r\nbar\r\n\r\n";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szWithSep3, sizeof(szWithSep3), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 2);
    RTTEST_CHECK(hTest,    cStrings == 2
                        && papszStrings
                        && RTStrICmp(papszStrings[0], "foo") == 0
                        && RTStrICmp(papszStrings[1], "bar") == 0);
    DO_CLEANUP();

    /* Multiple strings w/ two consequtive separators. */
    const char szWithSep4[] = "foo\r\nbar\r\n\r\nbaz";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szWithSep4, sizeof(szWithSep4), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 3);
    RTTEST_CHECK(hTest,    cStrings == 3
                        && papszStrings
                        && RTStrICmp(papszStrings[0], "foo") == 0
                        && RTStrICmp(papszStrings[1], "bar") == 0
                        && RTStrICmp(papszStrings[2], "baz") == 0);
    DO_CLEANUP();

    /* Multiple strings w/ trailing separators. */
    const char szWithSep5[] = "foo\r\nbar\r\n\r\nbaz\r\n\r\n";
    RTTEST_CHECK_RC(hTest, RTStrSplit(szWithSep5, sizeof(szWithSep5), "\r\n", &papszStrings, &cStrings), VINF_SUCCESS);
    RTTEST_CHECK(hTest, cStrings == 3);
    RTTEST_CHECK(hTest,    cStrings == 3
                        && papszStrings
                        && RTStrICmp(papszStrings[0], "foo") == 0
                        && RTStrICmp(papszStrings[1], "bar") == 0
                        && RTStrICmp(papszStrings[2], "baz") == 0);
    DO_CLEANUP();

#undef DO_CLEANUP

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

