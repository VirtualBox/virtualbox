/* $Id: tstRTFileModeStringToFlags.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - File mode string to IPRT file mode flags.
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
#include <iprt/file.h>

#include <iprt/errcore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTStrVersion", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    RTTestSub(hTest, "RTFileModeToFlags");
    static struct
    {
        int         iResult;
        const char *pszMode;
        uint64_t    uMode;
    } const aTests[] =
    {
        /* Invalid parameters. */
        { VERR_INVALID_PARAMETER,     "",           0 },
        { VERR_INVALID_PARAMETER,     "foo",        0 },
        { VERR_INVALID_PARAMETER,     "--",         0 },
        { VERR_INVALID_PARAMETER,     "++",         0 },
        { VERR_INVALID_PARAMETER,     "++",         0 },
        /* Missing action. */
        { VERR_INVALID_PARAMETER,     "z",          0 },
        /* Open for reading ("r"). */
            { VINF_SUCCESS          , "r",          RTFILE_O_OPEN | RTFILE_O_READ },
            { VINF_SUCCESS          , "r+",         RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "r+++",       RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "+++r",       RTFILE_O_OPEN | RTFILE_O_READ },
            { VINF_SUCCESS          , "r+t",        RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "r+b",        RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
        /* Open / append ("a"). */
            { VINF_SUCCESS          , "a",          RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_APPEND },
            { VINF_SUCCESS          , "a+",         RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_APPEND },
            { VINF_SUCCESS          , "a+++",       RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_APPEND },
            { VINF_SUCCESS          , "+++a",       RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_APPEND },
            { VINF_SUCCESS          , "a+t",        RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_APPEND },
            { VINF_SUCCESS          , "a+b",        RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_APPEND },
        /* Create / open ("c"). */
            { VINF_SUCCESS          , "c",          RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "c+",         RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "c+++",       RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VERR_INVALID_PARAMETER, "cr",         0 },
            { VERR_INVALID_PARAMETER, "cr+",        0 },
        /* Create / replace ("w"). */
            { VINF_SUCCESS          , "w",          RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_TRUNCATE },
            { VERR_INVALID_PARAMETER, "ww",         0 },
            { VERR_INVALID_PARAMETER, "wc",         0 },
            { VINF_SUCCESS          , "wb",         RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_TRUNCATE },
            { VINF_SUCCESS          , "wb+",        RTFILE_O_CREATE_REPLACE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_TRUNCATE },
            { VINF_SUCCESS          , "w+",         RTFILE_O_CREATE_REPLACE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_TRUNCATE },
            { VINF_SUCCESS          , "w++",        RTFILE_O_CREATE_REPLACE | RTFILE_O_READ | RTFILE_O_WRITE | RTFILE_O_TRUNCATE },
        /* Create only ("x"). */
            { VINF_SUCCESS          , "x",          RTFILE_O_CREATE | RTFILE_O_WRITE },
            { VERR_INVALID_PARAMETER, "xx",         0 },
            { VERR_INVALID_PARAMETER, "xc",         0 },
            { VINF_SUCCESS          , "xb",         RTFILE_O_CREATE | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "xb+",        RTFILE_O_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "x+",         RTFILE_O_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "x++",        RTFILE_O_CREATE | RTFILE_O_READ | RTFILE_O_WRITE }
    };

    for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTests); iTest++)
    {
        uint64_t uMode;
        int iResult = RTFileModeToFlags(aTests[iTest].pszMode, &uMode);
        if (iResult != aTests[iTest].iResult)
        {
            RTTestFailed(hTest, "#%u: mode string '%s', result is %Rrc, expected %Rrc",
                         iTest, aTests[iTest].pszMode, iResult, aTests[iTest].iResult);
            break;
        }

        /** @todo Testing sharing modes are not implemented yet,
         *        so just remove them from testing. */
        uMode &= ~RTFILE_O_DENY_NONE;

        if (   RT_SUCCESS(iResult)
            && uMode != aTests[iTest].uMode)
        {
            RTTestFailed(hTest, "#%u: mode string '%s', got 0x%x, expected 0x%x",
                         iTest, aTests[iTest].pszMode, uMode, aTests[iTest].uMode);
            break;
        }
    }

    RTTestSub(hTest, "RTFileModeToFlagsEx");
    static struct
    {
        int         iResult;
        const char *pszDisposition;
        const char *pszMode;
        /** @todo pszSharing not used yet. */
        uint64_t    uMode;
    } const aTestsEx[] =
    {
        /* Invalid parameters. */
        { VERR_INVALID_PARAMETER,     "",           "",         0 },
        { VERR_INVALID_PARAMETER,     "foo",        "",         0 },
        { VERR_INVALID_PARAMETER,     "--",         "",         0 },
        { VERR_INVALID_PARAMETER,     "++",         "",         0 },
        { VERR_INVALID_PARAMETER,     "++",         "",         0 },
        /* Missing action. */
        { VERR_INVALID_PARAMETER,     "z",          "",         0 },
        /* Open existing ("oe"). */
            { VINF_SUCCESS          , "oe",         "r",        RTFILE_O_OPEN | RTFILE_O_READ },
            { VINF_SUCCESS          , "oe",         "w",        RTFILE_O_OPEN | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oe",         "rw",       RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oe",         "rw+",      RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oe",         "++r",      RTFILE_O_OPEN | RTFILE_O_READ },
            { VINF_SUCCESS          , "oe",         "r+t",      RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oe",         "r+b",      RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_WRITE },
        /* Open / create ("oc"). */
            { VINF_SUCCESS          , "oc",         "r",        RTFILE_O_OPEN_CREATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "r+",       RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oc",         "r+++",     RTFILE_O_OPEN_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "oc",         "+++r",     RTFILE_O_OPEN_CREATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "w+t",      RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "w+b",      RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "w+t",      RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "wr",       RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "oc",         "rw",       RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
        /* Open and truncate ("ot"). */
            { VINF_SUCCESS          , "ot",         "r",        RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "r+",       RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ot",         "r+++",     RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ot",         "+++r",     RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "w+t",      RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "w+b",      RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "w+t",      RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "wr",       RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ot",         "rw",       RTFILE_O_OPEN | RTFILE_O_TRUNCATE | RTFILE_O_WRITE | RTFILE_O_READ },
        /* Create always ("ca"). */
            { VINF_SUCCESS          , "ca",         "r",        RTFILE_O_CREATE_REPLACE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "r+",       RTFILE_O_CREATE_REPLACE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ca",         "r+++",     RTFILE_O_CREATE_REPLACE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ca",         "+++r",     RTFILE_O_CREATE_REPLACE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "w+t",      RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "w+b",      RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "w+t",      RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "wr",       RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ca",         "rw",       RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_READ },
        /* Create if not exist ("ce"). */
            { VINF_SUCCESS          , "ce",         "r",        RTFILE_O_CREATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "r+",       RTFILE_O_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ce",         "r+++",     RTFILE_O_CREATE | RTFILE_O_READ | RTFILE_O_WRITE },
            { VINF_SUCCESS          , "ce",         "+++r",     RTFILE_O_CREATE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "w+t",      RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "w+b",      RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "w+t",      RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "wr",       RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_READ },
            { VINF_SUCCESS          , "ce",         "rw",       RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_READ }
    };

    for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTestsEx); iTest++)
    {
        uint64_t uMode;
        int iResult = RTFileModeToFlagsEx(aTestsEx[iTest].pszMode, aTestsEx[iTest].pszDisposition,
                                          NULL /* pszSharing */, &uMode);
        if (iResult != aTestsEx[iTest].iResult)
        {
            RTTestFailed(hTest, "#%u: disp '%s', mode '%s', result is %Rrc, expected %Rrc",
                         iTest, aTestsEx[iTest].pszDisposition, aTestsEx[iTest].pszMode,
                         iResult, aTestsEx[iTest].iResult);
            break;
        }

        /** @todo Testing sharing modes are not implemented yet,
         *        so just remove them from testing. */
        uMode &= ~RTFILE_O_DENY_NONE;

        if (   RT_SUCCESS(iResult)
            && uMode != aTestsEx[iTest].uMode)
        {
            RTTestFailed(hTest, "#%u: disp '%s', mode '%s', got 0x%x, expected 0x%x",
                         iTest, aTestsEx[iTest].pszDisposition, aTestsEx[iTest].pszMode,
                         uMode, aTestsEx[iTest].uMode);
            break;
        }
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

