/* $Id: tstDnDPath.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD path tests.
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
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/GuestHost/DragAndDrop.h>


static void tstPathRebase(RTTEST hTest)
{
    static struct
    {
        char const *pszPath;
        char const *pszPathOld;
        char const *pszPathNew;
        int rc;
        char const *pszResult;
    } const s_aTests[] = {
        /* Invalid stuff. */
        { NULL, NULL, NULL, VERR_INVALID_POINTER, NULL },
        { "foo", "old", NULL, VERR_INVALID_POINTER, NULL },
        /* Actual rebasing. */
        { "old/foo", "old", "new", VINF_SUCCESS, "new/foo" },
        /* Note: DnDPathRebase intentionally does not do any path conversions. */
#ifdef RT_OS_WINDOWS
        { "old\\foo", "old", "new", VINF_SUCCESS, "new/foo" },
        { "\\totally\\different\\path\\foo", "/totally/different/path", "/totally/different/path", VINF_SUCCESS, "/totally/different/path/foo" },
        { "\\old\\path\\foo", "", "/new/root/", VINF_SUCCESS, "/new/root/old/path/foo" },
        { "\\\\old\\path\\\\foo", "", "/new/root/", VINF_SUCCESS, "/new/root/old/path\\\\foo" }
#else
        { "old/foo", "old", "new", VINF_SUCCESS, "new/foo" },
        { "/totally/different/path/foo", "/totally/different/path", "/totally/different/path", VINF_SUCCESS, "/totally/different/path/foo" },
        { "/old/path/foo", "", "/new/root/", VINF_SUCCESS, "/new/root/old/path/foo" },
        { "//old/path//foo", "", "/new/root/", VINF_SUCCESS, "/new/root/old/path//foo" }
#endif
    };

    char *pszPath = NULL;
    for (size_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RTTestDisableAssertions(hTest);
        RTTEST_CHECK_RC(hTest, DnDPathRebase(s_aTests[i].pszPath, s_aTests[i].pszPathOld, s_aTests[i].pszPathNew, &pszPath),
                        s_aTests[i].rc);
        RTTestRestoreAssertions(hTest);
        if (RT_SUCCESS(s_aTests[i].rc))
        {
            if (s_aTests[i].pszResult)
                RTTEST_CHECK_MSG(hTest, RTPathCompare(pszPath, s_aTests[i].pszResult) == 0,
                                 (hTest, "Test #%zu failed: Got '%s', expected '%s'", i, pszPath, s_aTests[i].pszResult));
            RTStrFree(pszPath);
            pszPath = NULL;
        }
    }
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstDnDPath", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstPathRebase(hTest);

    return RTTestSummaryAndDestroy(hTest);
}

