/* $Id: tstDnDTransferObject.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD URI object (DNDTRANSFEROBJECT) tests.
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
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/GuestHost/DragAndDrop.h>


static void tstPaths(RTTEST hTest)
{
    RTTestSub(hTest, "Testing path handling");

    char szBuf[64];

    DNDTRANSFEROBJECT Obj;
    RT_ZERO(Obj);

    /*
     * Initialization handling.
     */
    RTTEST_CHECK(hTest, DnDTransferObjectGetSourcePath(&Obj) == NULL);
    RTTEST_CHECK_RC(hTest, DnDTransferObjectGetDestPathEx(&Obj, DNDTRANSFEROBJPATHSTYLE_TRANSPORT, szBuf, sizeof(szBuf)), VERR_NOT_FOUND);
    RTTEST_CHECK(hTest, DnDTransferObjectGetMode(&Obj) == 0);
    RTTEST_CHECK(hTest, DnDTransferObjectGetSize(&Obj) == 0);
    RTTEST_CHECK(hTest, DnDTransferObjectGetProcessed(&Obj) == 0);
    RTTEST_CHECK(hTest, DnDTransferObjectGetType(&Obj) == DNDTRANSFEROBJTYPE_UNKNOWN);

    /*
     * Paths handling.
     */
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_FILE, "", "/rel/path/to/dst"));
    RTTestDisableAssertions(hTest);
    RTTEST_CHECK_RC   (hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_FILE, "", "/rel/path/to/dst"), VERR_WRONG_ORDER);
    RTTestRestoreAssertions(hTest);
    DnDTransferObjectReset(&Obj);

    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_FILE, "/src/path1", "dst/path2"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetSourcePath(&Obj), "/src/path1/dst/path2") == 0);
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/path2") == 0);
    RTTEST_CHECK(hTest,    DnDTransferObjectGetDestPathEx(&Obj, DNDTRANSFEROBJPATHSTYLE_DOS, szBuf, sizeof(szBuf)) == VINF_SUCCESS
                        && RTStrCmp(szBuf, "dst\\path2") == 0);
    DnDTransferObjectReset(&Obj);
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_FILE, "", "dst/with/ending/slash/"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/with/ending/slash/") == 0);
    RTTEST_CHECK(hTest,    DnDTransferObjectGetDestPathEx(&Obj, DNDTRANSFEROBJPATHSTYLE_TRANSPORT, szBuf, sizeof(szBuf)) == VINF_SUCCESS
                        && RTStrCmp(szBuf, "dst/with/ending/slash/") == 0);
    DnDTransferObjectReset(&Obj);
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "", "dst/path2"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetSourcePath(&Obj), "dst/path2/") == 0);
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/path2/") == 0);
    DnDTransferObjectReset(&Obj);
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "", "dst\\to\\path2"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetSourcePath(&Obj), "dst/to/path2/") == 0);
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/to/path2/") == 0);
    DnDTransferObjectReset(&Obj);
    /* Test that the destination does not have a beginning slash. */
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "/src/path2", "/dst/to/path2/"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetSourcePath(&Obj), "/src/path2/dst/to/path2/") == 0);
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/to/path2/") == 0);
    DnDTransferObjectReset(&Obj);
    RTTEST_CHECK_RC_OK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "/src/path2", "//////dst/to/path2/"));
    RTTEST_CHECK(hTest, RTStrCmp(DnDTransferObjectGetDestPath(&Obj), "dst/to/path2/") == 0);

    /*
     * Invalid stuff.
     */
    DnDTransferObjectReset(&Obj);
    RTTestDisableAssertions(hTest);
    RTTEST_CHECK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "/src/path3", "../../dst/path3") == VERR_INVALID_PARAMETER);
    RTTEST_CHECK(hTest, DnDTransferObjectInitEx(&Obj, DNDTRANSFEROBJTYPE_DIRECTORY, "/src/../../path3", "dst/path3") == VERR_INVALID_PARAMETER);
    RTTestRestoreAssertions(hTest);

    /*
     * Reset handling.
     */
    DnDTransferObjectReset(&Obj);
    RTTEST_CHECK(hTest, DnDTransferObjectGetSourcePath(&Obj) == NULL);
    RTTEST_CHECK(hTest, DnDTransferObjectGetDestPath(&Obj) == NULL);

    DnDTransferObjectDestroy(&Obj);
    DnDTransferObjectDestroy(&Obj); /* Doing this twice here is intentional. */
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstDnDTransferObject", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstPaths(hTest);

    return RTTestSummaryAndDestroy(hTest);
}

