/* $Id: tstRTErr-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Error Messages.
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
#include <iprt/errcore.h>

#include <iprt/test.h>
#include <iprt/string.h>
#include <iprt/err.h>



static void tstIprtStatuses(RTTEST hTest)
{
    RTTestSub(hTest, "IPRT status codes");

    char         szMsgShort[640];
    char         szMsgFull[sizeof(szMsgShort)];
    char         szMsgAll[sizeof(szMsgShort) + 80];
    size_t const cbBuf  = sizeof(szMsgShort);
    char * const pszBuf = (char *)RTTestGuardedAllocTail(hTest, cbBuf);
    RTTESTI_CHECK_RETV(pszBuf);

    static const struct
    {
        int rc;
        const char *pszDefine;
    } s_aTests[] =
    {
        { VINF_SUCCESS,                                             "VINF_SUCCESS" },
        { VERR_INVALID_PARAMETER,                                   "VERR_INVALID_PARAMETER" },
        { VERR_NOT_IMPLEMENTED,                                     "VERR_NOT_IMPLEMENTED" },
        { VERR_NUMBER_TOO_BIG,                                      "VERR_NUMBER_TOO_BIG" },
        { VWRN_NUMBER_TOO_BIG,                                      "VWRN_NUMBER_TOO_BIG" },
        { VERR_CANCELLED,                                           "VERR_CANCELLED" },
        { VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_BOOT_IND,    "VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_BOOT_IND" },
        { VERR_CR_CIPHER_INVALID_INITIALIZATION_VECTOR_LENGTH,      "VERR_CR_CIPHER_INVALID_INITIALIZATION_VECTOR_LENGTH" },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        int const          rc        = s_aTests[i].rc;
        const char * const pszDefine = s_aTests[i].pszDefine;
        size_t const       cchDefine = strlen(pszDefine);

        if (RTErrIsKnown(rc) != true)
            RTTestFailed(hTest, "RTErrIsKnown(%s) did not return true", pszDefine);

        RTTestDisableAssertions(hTest);
        size_t cchMsgShort = ~(size_t)0;
        size_t cchMsgFull  = ~(size_t)0;
        size_t cchMsgAll   = ~(size_t)0;
        size_t cbBuf2      = cbBuf - 1;
        while (cbBuf2-- > 0)
        {
#define CHECK_TEST_RESULT(a_szFunction, a_pszExpect, a_cchExpect) do { \
                    if (cbBuf2 > (a_cchExpect) && cchRet != (ssize_t)(a_cchExpect)) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> %zd, expected %zd", a_szFunction, pszDefine, cbBuf2, \
                                     cchRet, (a_cchExpect)); \
                    else if (cbBuf2 <= (a_cchExpect) && cchRet != VERR_BUFFER_OVERFLOW) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> %zd, expected %d", a_szFunction, pszDefine, cbBuf2, \
                                     cchRet, VERR_BUFFER_OVERFLOW); \
                    else if (cbBuf2 > (a_cchExpect) && memcmp(pszBuf2, (a_pszExpect), (a_cchExpect) + 1) != 0) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> '%.*s', expected '%s'", a_szFunction, pszDefine, cbBuf2, \
                                     cbBuf2, pszBuf2, (a_pszExpect)); \
                    /* Only check that it's terminated.  Compression may exit before it's quite full. */ \
                    else if (cbBuf2 > 0 && RTStrNLen(pszBuf2, cbBuf2) >= cbBuf2) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> result not terminated", a_szFunction, pszDefine, cbBuf2); \
                } while (0)

            /* RTErrQueryDefine: */
            memset(pszBuf, '?', cbBuf);
            char *pszBuf2 = &pszBuf[cbBuf - cbBuf2];
            ssize_t cchRet = RTErrQueryDefine(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrQueryDefine", pszDefine, cchDefine);

            /* RTErrQueryMsgShort:  */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTErrQueryMsgShort(rc, pszBuf2, cbBuf2, false);
            if (cchMsgShort == ~(size_t)0)
            {
                cchMsgShort = (size_t)cchRet;
                memcpy(szMsgShort, pszBuf2, cchMsgShort);
                szMsgShort[cchMsgShort] = '\0';
            }
            CHECK_TEST_RESULT("RTErrQueryMsgShort", szMsgShort, cchMsgShort);

            /* RTErrQueryMsgFull:  */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTErrQueryMsgFull(rc, pszBuf2, cbBuf2, false);
            if (cchMsgFull == ~(size_t)0)
            {
                cchMsgFull = (size_t)cchRet;
                memcpy(szMsgFull, pszBuf2, cchMsgFull);
                szMsgFull[cchMsgFull] = '\0';
            }
            CHECK_TEST_RESULT("RTErrQueryMsgFull", szMsgFull, cchMsgFull);

            /* Same thru the string formatter. */
#define CHECK_TEST_RESULT2(a_szFunction, a_pszExpect, a_cchExpect) do { \
                    ssize_t const cchLocalExpect = cbBuf2 > (a_cchExpect) ? (ssize_t)(a_cchExpect) : -(ssize_t)(a_cchExpect) - 1; \
                    if (cchRet != cchLocalExpect && cchRet > 0) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> %zd, expected %zd ('%s' vs '%s')", a_szFunction, pszDefine, cbBuf2, \
                                     cchRet, cchLocalExpect, pszBuf2, (a_pszExpect)); \
                    else if (cchRet != cchLocalExpect && cchRet <= 0) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> %zd, expected %zd", a_szFunction, pszDefine, cbBuf2, \
                                     cchRet, cchLocalExpect); \
                    else if (cbBuf2 > 0 && memcmp(pszBuf2, (a_pszExpect), RT_MIN(cbBuf2 - 1, (a_cchExpect) + 1)) != 0) \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> '%.*s', expected '%s'", a_szFunction, pszDefine, cbBuf2, \
                                     cbBuf2, pszBuf2, (a_pszExpect)); \
                    else if (cbBuf2 > 0 && cbBuf2 <= (a_cchExpect) && pszBuf2[cbBuf2 - 1] != '\0') \
                        RTTestFailed(hTest, "%s(%s, , %#x) -> result not terminated", a_szFunction, pszDefine, cbBuf2); \
                } while (0)
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrc", rc);
            CHECK_TEST_RESULT2("RTErrFormatDefine/%Rrc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrs", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgShort/%Rrs", szMsgShort, cchMsgShort);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrf", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgFull/%Rrf", szMsgFull, cchMsgFull);

            if (cchMsgAll == ~(size_t)0)
                cchMsgAll = RTStrPrintf(szMsgAll, sizeof(szMsgAll), "%s (%d) - %s", pszDefine, rc, szMsgFull);
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rra", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgAll/%Rra", szMsgAll, cchMsgAll);
        }
        RTTestRestoreAssertions(hTest);
    }

    /*
     * Same but for an unknown status code.
     */
    static const int s_arcUnknowns[] = { -270, 270, -88888888, 88888888, };
    for (size_t i = 0; i < RT_ELEMENTS(s_arcUnknowns); i++)
    {
        int const rc = s_arcUnknowns[i];

        if (RTErrIsKnown(rc) != false)
            RTTestFailed(hTest, "RTErrIsKnown(%d) did not return false", rc);

        size_t const       cchDefine = RTStrPrintf(szMsgFull, sizeof(szMsgFull), "%d", rc);
        const char * const pszDefine = szMsgFull;
        size_t const       cchMsg    = RTStrPrintf(szMsgShort, sizeof(szMsgShort), "Unknown Status %d (%#x)", rc, rc);
        const char * const pszMsg    = szMsgShort;

        RTTestDisableAssertions(hTest);
        size_t cbBuf2      = cbBuf - 1;
        while (cbBuf2-- > 0)
        {
            /* RTErrQueryDefine: */
            memset(pszBuf, '?', cbBuf);
            char *pszBuf2 = &pszBuf[cbBuf - cbBuf2];
            ssize_t cchRet = RTErrQueryDefine(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrQueryDefine", pszDefine, cchDefine);
            RTTEST_CHECK(hTest, RTErrQueryDefine(rc, pszBuf2, cbBuf2, true) == VERR_NOT_FOUND);


            /* RTErrQueryMsgShort:  */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTErrQueryMsgShort(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrQueryMsgShort", pszMsg, cchMsg);
            RTTEST_CHECK(hTest, RTErrQueryMsgShort(rc, pszBuf2, cbBuf2, true) == VERR_NOT_FOUND);

            /* RTErrQueryMsgFull:  */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTErrQueryMsgFull(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrQueryMsgFull", pszMsg, cchMsg);
            RTTEST_CHECK(hTest, RTErrQueryMsgFull(rc, pszBuf2, cbBuf2, true) == VERR_NOT_FOUND);

            /* Same thru the string formatter. */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrc", rc);
            CHECK_TEST_RESULT2("RTErrFormatDefine/%Rrc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrs", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgShort/%Rrs", pszMsg, cchMsg);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rrf", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgFull/%Rrf", pszMsg, cchMsg);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rra", rc);
            CHECK_TEST_RESULT2("RTErrFormatMsgAll/%Rra", pszMsg, cchMsg);
        }
        RTTestRestoreAssertions(hTest);
    }

    RTTestGuardedFree(hTest, pszBuf);
}


#ifdef RT_OS_WINDOWS
static void tstWinComStatuses(RTTEST hTest)
{
    RTTestSub(hTest, "COM/Win status codes");

    char         szMsg[640];
    char         szMsgAll[sizeof(szMsg) + 80];
    size_t const cbBuf  = sizeof(szMsg);
    char * const pszBuf = (char *)RTTestGuardedAllocTail(hTest, cbBuf);
    RTTESTI_CHECK_RETV(pszBuf);

    static const struct
    {
        int32_t rc;
        const char *pszDefine;
    } s_aTests[] =
    {
        { (int32_t)0x00000000, "ERROR_SUCCESS" },
        { (int32_t)0x0000000e, "ERROR_OUTOFMEMORY" },
        { (int32_t)0x8007000e, "E_OUTOFMEMORY" },
        { (int32_t)0x00000057, "ERROR_INVALID_PARAMETER" },
        { (int32_t)0x80070057, "E_INVALIDARG" },
        { (int32_t)0x80004005, "E_FAIL" },
        { (int32_t)0x00000783, "RPC_S_NOT_ALL_OBJS_EXPORTED" },

    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        int32_t const      rc        = s_aTests[i].rc;
        const char * const pszDefine = s_aTests[i].pszDefine;
        size_t const       cchDefine = strlen(pszDefine);

        if (RTErrWinIsKnown(rc) != true)
            RTTestFailed(hTest, "RTErrIsKnown(%s) did not return true", pszDefine);

        RTTestDisableAssertions(hTest);
        size_t cchMsg    = ~(size_t)0;
        size_t cchMsgAll = ~(size_t)0;
        size_t cbBuf2    = cbBuf - 1;
        while (cbBuf2-- > 0)
        {
            /* RTErrQueryDefine: */
            memset(pszBuf, '?', cbBuf);
            char *pszBuf2 = &pszBuf[cbBuf - cbBuf2];
            ssize_t cchRet = RTErrWinQueryDefine(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrWinQueryDefine", pszDefine, cchDefine);

            /* Thru the string formatter. */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwc", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatDefine/%Rwc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhrc", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatDefine/%Rhrc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwf", rc);
            if (cchMsg == ~(size_t)0)
            {
                cchMsg = (size_t)cchRet;
                memcpy(szMsg, pszBuf2, cchMsg + 1);
            }
            CHECK_TEST_RESULT2("RTErrWinFormatMsg/%Rwf", szMsg, cchMsg);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhrf", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsg/%Rhrf", szMsg, cchMsg);

            if (cchMsgAll == ~(size_t)0)
                cchMsgAll = RTStrPrintf(szMsgAll, sizeof(szMsgAll), "%s (%#x)", pszDefine, rc);
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwa", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsgAll/%Rwa", szMsgAll, cchMsgAll);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhra", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsgAll/%Rhra", szMsgAll, cchMsgAll);
        }
        RTTestRestoreAssertions(hTest);
    }

    /*
     * Same but for an unknown status code.
     */
    static const int32_t s_arcUnknowns[] = { (int32_t)0xff88ff88, 0x0f88ff88, };
    for (size_t i = 0; i < RT_ELEMENTS(s_arcUnknowns); i++)
    {
        int32_t const rc = s_arcUnknowns[i];

        if (RTErrIsKnown(rc) != false)
            RTTestFailed(hTest, "RTErrIsKnown(%d) did not return false", rc);

        size_t const       cchDefine = RTStrPrintf(szMsg, sizeof(szMsg), "%#x", rc);
        const char * const pszDefine = szMsg;
        size_t const       cchMsg    = RTStrPrintf(szMsgAll, sizeof(szMsgAll), "Unknown Status %#x", rc);
        const char * const pszMsg    = szMsgAll;

        RTTestDisableAssertions(hTest);
        size_t cbBuf2      = cbBuf - 1;
        while (cbBuf2-- > 0)
        {
            /* RTErrWinQueryDefine: */
            memset(pszBuf, '?', cbBuf);
            char *pszBuf2 = &pszBuf[cbBuf - cbBuf2];
            ssize_t cchRet = RTErrWinQueryDefine(rc, pszBuf2, cbBuf2, false);
            CHECK_TEST_RESULT("RTErrWinQueryDefine", pszDefine, cchDefine);
            RTTEST_CHECK(hTest, RTErrWinQueryDefine(rc, pszBuf2, cbBuf2, true) == VERR_NOT_FOUND);

            /* Thru the string formatter. */
            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwc", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatDefine/%Rwc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhrc", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatDefine/%Rhrc", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwf", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsg/%Rwf", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhrf", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsg/%Rhrf", pszDefine, cchDefine);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rwa", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsgAll/%Rwa", pszMsg, cchMsg);

            memset(pszBuf, '?', cbBuf);
            cchRet = RTStrPrintf2(pszBuf2, cbBuf2, "%Rhra", rc);
            CHECK_TEST_RESULT2("RTErrWinFormatMsgAll/%Rhra", pszMsg, cchMsg);
        }
        RTTestRestoreAssertions(hTest);
    }
}
#endif /* RT_OS_WINDOWS */


int main(int argc, char **argv)
{
    RT_NOREF2(argc, argv);
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTErr-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstIprtStatuses(hTest);
#ifdef RT_OS_WINDOWS
    tstWinComStatuses(hTest);
#else
#endif

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

