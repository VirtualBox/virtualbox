/* $Id: tstRTStrCatCopy.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - String Concatenation and Copy.
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
#include <iprt/string.h>

#include <iprt/errcore.h>
#include <iprt/test.h>


static void testCopy1(RTTEST hTest)
{
    RTTestISub("RTStrCopy");

    char *pszBuf4H = (char *)RTTestGuardedAllocHead(hTest, 4);
    char *pszBuf4T = (char *)RTTestGuardedAllocTail(hTest, 4);
    RTTESTI_CHECK_RC(RTStrCopy(pszBuf4H, 4, "abc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCopy(pszBuf4T, 4, "abc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    RT_BZERO(pszBuf4H, 4); RT_BZERO(pszBuf4T, 4);
    RTTESTI_CHECK_RC(RTStrCopy(pszBuf4H, 4, "abcd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCopy(pszBuf4T, 4, "abcd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);
}


static void testCopyEx1(RTTEST hTest)
{
    RTTestISub("RTStrCopyEx");

    char *pszBuf4H = (char *)RTTestGuardedAllocHead(hTest, 4);
    char *pszBuf4T = (char *)RTTestGuardedAllocTail(hTest, 4);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4H, 4, "abc", RTSTR_MAX), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4T, 4, "abc", RTSTR_MAX), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    RT_BZERO(pszBuf4H, 4); RT_BZERO(pszBuf4T, 4);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4H, 4, "abcd", RTSTR_MAX), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4T, 4, "abcd", RTSTR_MAX), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    RT_BZERO(pszBuf4H, 4); RT_BZERO(pszBuf4T, 4);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4H, 4, "abcd", 3), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4T, 4, "abcd", 3), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    RT_BZERO(pszBuf4H, 4); RT_BZERO(pszBuf4T, 4);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4H, 4, "abcd", 2), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "ab") == 0);
    RTTESTI_CHECK_RC(RTStrCopyEx(pszBuf4T, 4, "abcd", 2), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "ab") == 0);
}


static void testCat1(RTTEST hTest)
{
    RTTestISub("RTStrCat");

    char *pszBuf4H = (char *)RTTestGuardedAllocHead(hTest, 4);
    char *pszBuf4T = (char *)RTTestGuardedAllocTail(hTest, 4);
    memset(pszBuf4T, 0xff, 4); *pszBuf4T = '\0';
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "abc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    memset(pszBuf4H, 0xff, 4); *pszBuf4H = '\0';
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "abc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "a");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "a");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "bc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "bc"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "ab");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "ab");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "c"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "c"), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "abc");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "abc");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, ""), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, ""), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "abcd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "abcd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "ab");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "ab");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "cd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "cd"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "abc");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "abc");
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4H, 4, "d"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCat(pszBuf4T, 4, "d"), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);
}


static void testCatEx1(RTTEST hTest)
{
    RTTestISub("RTStrCatEx");

    char *pszBuf4H = (char *)RTTestGuardedAllocHead(hTest, 4);
    char *pszBuf4T = (char *)RTTestGuardedAllocTail(hTest, 4);
    memset(pszBuf4T, 0xff, 4); *pszBuf4T = '\0';
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "abc", RTSTR_MAX), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    memset(pszBuf4H, 0xff, 4); *pszBuf4H = '\0';
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "abc", RTSTR_MAX), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "a");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "a");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "bc", 2), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "bc", 2), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "ab");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "ab");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "c", 1), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "c", 1), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "abc");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "abc");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "defg", 0), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "defg", 0), VINF_SUCCESS);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "abcd", 4), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "abcd", 4), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "ab");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "ab");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "cdefg", 2), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "cdefg", 2), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);

    memset(pszBuf4T, 0xff, 4); strcpy(pszBuf4T, "abc");
    memset(pszBuf4H, 0xff, 4); strcpy(pszBuf4H, "abc");
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4H, 4, "de", 1), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4H, "abc") == 0);
    RTTESTI_CHECK_RC(RTStrCatEx(pszBuf4T, 4, "de", 1), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK(strcmp(pszBuf4T, "abc") == 0);
}



int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTStrCatCopy", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    testCopy1(hTest);
    testCopyEx1(hTest);
    testCat1(hTest);
    testCatEx1(hTest);

    return RTTestSummaryAndDestroy(hTest);
}
