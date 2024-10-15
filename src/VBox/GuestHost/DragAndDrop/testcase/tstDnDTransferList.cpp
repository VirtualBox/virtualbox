/* $Id: tstDnDTransferList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD transfer list  tests.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/GuestHost/DragAndDrop.h>


int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstDnDTransferList", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    char szPathWellKnown[RTPATH_MAX];
    RTStrCopy(szPathWellKnown, sizeof(szPathWellKnown),
#ifdef RT_OS_WINDOWS
              "C:\\Windows\\System32\\Boot\\");
#else
              "/bin/");
#endif

    char szPathWellKnownURI[RTPATH_MAX];
    RTStrPrintf(szPathWellKnownURI, sizeof(szPathWellKnownURI), "file:///%s", szPathWellKnown);

    DNDTRANSFERLIST list;
    RT_ZERO(list);

    /* Invalid stuff. */
    RTTestDisableAssertions(hTest);
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, "", DNDTRANSFERLISTFMT_NATIVE), VERR_INVALID_PARAMETER);
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnown, DNDTRANSFERLISTFMT_NATIVE), VINF_SUCCESS);
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnown, DNDTRANSFERLISTFMT_NATIVE), VERR_WRONG_ORDER);
    RTTestRestoreAssertions(hTest);
    DnDTransferListDestroy(&list);

    /* Empty. */
    RTTEST_CHECK_RC(hTest, DnDTransferListInit(&list), VINF_SUCCESS);
    DnDTransferListDestroy(&list);

    /* Initial status. */
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnown, DNDTRANSFERLISTFMT_NATIVE), VINF_SUCCESS);
    RTTEST_CHECK(hTest, DnDTransferListGetRootCount(&list) == 0);
    RTTEST_CHECK(hTest, DnDTransferListObjCount(&list) == 0);
    RTTEST_CHECK(hTest, DnDTransferListObjTotalBytes(&list) == 0);
    RTTEST_CHECK(hTest, DnDTransferListObjGetFirst(&list) == NULL);
    DnDTransferListDestroy(&list);

    char szPathTest[RTPATH_MAX];

    /* Root path handling. */
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnown, DNDTRANSFERLISTFMT_NATIVE), VINF_SUCCESS);
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_NATIVE, "/wrong/root/path", DNDTRANSFERLIST_FLAGS_NONE), VERR_INVALID_PARAMETER);
    rc = RTPathJoin(szPathTest, sizeof(szPathTest), szPathWellKnown, "/non/existing");
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_NATIVE, szPathTest, DNDTRANSFERLIST_FLAGS_NONE), VERR_PATH_NOT_FOUND);
    DnDTransferListDestroy(&list);

    /* Adding native stuff. */
    /* No root path set yet and non-recursive -> will set root path to szPathWellKnown, but without any entries added. */
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnown, DNDTRANSFERLISTFMT_NATIVE), VINF_SUCCESS);
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_NATIVE, szPathWellKnown, DNDTRANSFERLIST_FLAGS_NONE), VINF_SUCCESS);
    RTTEST_CHECK(hTest, DnDTransferListGetRootCount(&list));
    RTTEST_CHECK(hTest, DnDTransferListObjCount(&list));

    /* Add szPathWellKnown again, this time recursively. */
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_NATIVE, szPathWellKnown, DNDTRANSFERLIST_FLAGS_RECURSIVE), VINF_SUCCESS);
    RTTEST_CHECK(hTest, DnDTransferListGetRootCount(&list));
    RTTEST_CHECK(hTest, DnDTransferListObjCount(&list));

    char *pszString = NULL;
    size_t cbString = 0;
    RTTEST_CHECK_RC_OK(hTest, DnDTransferListGetRoots(&list, DNDTRANSFERLISTFMT_NATIVE, &pszString, &cbString));
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Roots:\n%s\n\n", pszString);
    RTStrFree(pszString);

    PDNDTRANSFEROBJECT pObj;
    while ((pObj = DnDTransferListObjGetFirst(&list)))
    {
        RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Obj: %s\n", DnDTransferObjectGetDestPath(pObj));
        DnDTransferListObjRemoveFirst(&list);
    }
    DnDTransferListDestroy(&list);

    char  *pszBuf;
    size_t cbBuf;

    /* To URI data. */
    RTTEST_CHECK_RC(hTest, DnDTransferListInitEx(&list, szPathWellKnownURI, DNDTRANSFERLISTFMT_URI), VINF_SUCCESS);
    RTStrPrintf(szPathTest, sizeof(szPathTest), "%s/foo", szPathWellKnownURI);
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_URI, szPathWellKnownURI, DNDTRANSFERLIST_FLAGS_NONE), VINF_SUCCESS);
    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPath(&list, DNDTRANSFERLISTFMT_URI, szPathTest, DNDTRANSFERLIST_FLAGS_NONE), VERR_PATH_NOT_FOUND);
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_NATIVE, "" /* pszBasePath */, "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Roots (native):\n%s\n", pszBuf);
    RTStrFree(pszBuf);
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_URI, "" /* pszBasePath */, "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Roots (URI):\n%s\n", pszBuf);
    RTStrFree(pszBuf);
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_URI, "\\new\\base\\path", "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Roots (URI, new base):\n%s\n", pszBuf);
    RTStrFree(pszBuf);
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_URI, "\\..\\invalid\\path", "\n", &pszBuf, &cbBuf), VERR_INVALID_PARAMETER);
    DnDTransferListDestroy(&list);

    /* From URI data. */
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    RTStrPrintf(szPathTest, sizeof(szPathTest),  "C:/Windows/");
    static const char s_szURI[] =
        "file:///C:/Windows/System32/Boot/\r\n"
        "file:///C:/Windows/System/\r\n";
    static const char s_szURIFmtURI[] =
        "file:///base/System32/Boot/\r\n"
        "file:///base/System/\r\n";
    static const char s_szURIFmtNative[] =
        "\\base\\System32\\Boot\\\r\n"
        "\\base\\System\\\r\n";
#else
    RTStrPrintf(szPathTest, sizeof(szPathTest), "/usr/");
    static const char s_szURI[] =
        "file:///usr/bin/\r\n"
        "file:///usr/lib/\r\n";
    static const char s_szURIFmtURI[] =
        "file:///base/bin/\r\n"
        "file:///base/lib/\r\n";
    static const char s_szURIFmtNative[] =
        "/base/bin/\r\n"
        "/base/lib/\r\n";
#endif

    RTTEST_CHECK_RC(hTest, DnDTransferListAppendPathsFromBuffer(&list, DNDTRANSFERLISTFMT_URI, s_szURI, sizeof(s_szURI), "\r\n",
                                                                DNDTRANSFERLIST_FLAGS_NONE), VINF_SUCCESS);
    RTTEST_CHECK(hTest, DnDTransferListGetRootCount(&list) == 2);
    RTTEST_CHECK(hTest, RTPathCompare(DnDTransferListGetRootPathAbs(&list), szPathTest) == 0);

    /* Validate returned lengths. */
    pszBuf = NULL;
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_URI, "/base/", "\r\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTEST_CHECK_MSG(hTest, RTStrCmp(pszBuf, s_szURIFmtURI) == 0, (hTest, "Got '%s'", pszBuf));
    RTTEST_CHECK_MSG(hTest, cbBuf == strlen(pszBuf) + 1, (hTest, "Got %d, expected %d\n", cbBuf, strlen(pszBuf) + 1));
    RTStrFree(pszBuf);

    pszBuf = NULL;
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_NATIVE, "/base/", "\r\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTEST_CHECK_MSG(hTest, RTStrCmp(pszBuf, s_szURIFmtNative) == 0,
                     (hTest, "Expected %.*Rhxs\nGot      %.*Rhxs\n   '%s'",
                      sizeof(s_szURIFmtNative) - 1, s_szURIFmtNative,
                      strlen(pszBuf), pszBuf, pszBuf));
    RTTEST_CHECK_MSG(hTest, cbBuf == strlen(pszBuf) + 1, (hTest, "Got %d, expected %d\n", cbBuf, strlen(pszBuf) + 1));
    RTStrFree(pszBuf);

    /* Validate roots with a new base. */
    pszBuf = NULL;
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_NATIVE, "/native/base/path", "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Roots (URI, new base):\n%s\n", pszBuf);
    RTStrFree(pszBuf);

    pszBuf = NULL;
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_NATIVE, "\\windows\\path", "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Roots (URI, new base):\n%s\n", pszBuf);
    RTStrFree(pszBuf);

    pszBuf = NULL;
    RTTEST_CHECK_RC(hTest, DnDTransferListGetRootsEx(&list, DNDTRANSFERLISTFMT_NATIVE, "\\\\windows\\\\path", "\n", &pszBuf, &cbBuf), VINF_SUCCESS);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Roots (URI, new base):\n%s\n", pszBuf);
    RTStrFree(pszBuf);

    DnDTransferListDestroy(&list);
    DnDTransferListDestroy(&list); /* Doing this twice here is intentional. */

    return RTTestSummaryAndDestroy(hTest);
}

