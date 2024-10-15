/* $Id: tstRTStrVersion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Version String Comparison.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/stream.h>


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTStrVersion", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    RTTestSub(hTest, "RTStrVersionCompare");
    static struct
    {
        const char *pszVer1;
        const char *pszVer2;
        int         iResult;
    } const aTests[] =
    {
        { "",           "",                 0 },
        { "asdf",       "",                 1 },
        { "asdf234",    "1.4.5",            1 },
        { "12.foo006",  "12.6",             1 },
        { "1",          "1",                0 },
        { "1",          "100",              -1},
        { "100",        "1",                1 },
        { "3",          "4",                -1},
        { "1",          "0.1",              1 },
        { "1",          "0.0.0.0.10000",    1 },
        { "0100",       "100",              0 },
        { "1.0.0",      "1",                0 },
        { "1.0.0",      "100.0.0",          -1},
        { "1",          "1.0.3.0",          -1},
        { "1.4.5",      "1.2.3",            1 },
        { "1.2.3",      "1.4.5",            -1},
        { "1.2.3",      "4.5.6",            -1},
        { "1.0.4",      "1.0.3",            1 },
        { "0.1",        "0.0.1",            1 },
        { "0.0.1",      "0.1.1",            -1},
        { "3.1.0",      "3.0.14",           1 },
        { "2.0.12",     "3.0.14",           -1},
        { "3.1",        "3.0.22",           1 },
        { "3.0.14",     "3.1.0",            -1},
        { "45.63",      "04.560.30",        1 },
        { "45.006",     "45.6",             0 },
        { "23.206",     "23.06",            1 },
        { "23.2",       "23.060",           -1},

        { "VirtualBox-2.0.8-Beta2",     "VirtualBox-2.0.8_Beta3-r12345",    -1 },
        { "VirtualBox-2.2.4-Beta2",     "VirtualBox-2.2.2",                  1 },
        { "VirtualBox-2.2.4-Beta3",     "VirtualBox-2.2.2-Beta4",            1 },
        { "VirtualBox-3.1.8-Alpha1",    "VirtualBox-3.1.8-Alpha1-r61454",   -1 },
        { "VirtualBox-3.1.0",           "VirtualBox-3.1.2_Beta1",           -1 },
        { "3.1.0_BETA-r12345",          "3.1.2",                            -1 },
        { "3.1.0_BETA1r12345",          "3.1.0",                            -1 },
        { "3.1.0_BETAr12345",           "3.1.0",                            -1 },
        { "3.1.0_BETA-r12345",          "3.1.0",                            -1 },
        { "3.1.0_BETA-r12345",          "3.1.0",                            -1 },
        { "3.1.0_BETA-r12345",          "3.1.0.0",                          -1 },
        { "3.1.0_BETA",                 "3.1.0.0",                          -1 },
        { "3.1.0_BETA1",                "3.1.0",                            -1 },
        { "3.1.0_BETA-r12345",          "3.1.0r12345",                      -1 },
        { "3.1.0_BETA1-r12345",         "3.1.0_BETA-r12345",                 0 },
        { "3.1.0_BETA1-r12345",         "3.1.0_BETA1-r12345",                0 },
        { "3.1.0_BETA2-r12345",         "3.1.0_BETA1-r12345",                1 },
        { "3.1.0_BETA2-r12345",         "3.1.0_BETA999-r12345",             -1 },
        { "3.1.0_BETA2",                "3.1.0_ABC",                        -1 }, /* ABC isn't indicating a prerelease, BETA does. */
        { "3.1.0_BETA",                 "3.1.0_ATEB",                       -1 },
        { "4.0.0_ALPHAr68482",          "4.0.0_ALPHAr68483",                -1 },
        { "4.0.0_ALPHA1r68482",         "4.0.0_ALPHAr68482",                 0 },
        { "4.0.0_ALPHA-r68482",         "4.0.0_ALPHAr68482",                 0 },
        { "4.0.0_ALPHAr68483",          "4.0.0_BETAr68783",                 -1 },
        { "4.0.0_ALPHAr68483",          "4.0.0_BETA1r68783",                -1 },
        { "4.0.0_ALPHAr68483",          "4.0.0_BETA2r68783",                -1 },
        { "4.0.0_ALPHAr68483",          "4.0.0_BETA2r68784",                -1 },
        { "4.0.6",                      "4.0.6_Ubuntu",                     -1 }, /* Without stripped guest OS string (Ubuntu). */
        { "4.0.6_Windows",              "4.0.6",                             1 }, /* Without stripped guest OS string (Windows). */
        { "4.1.6r74567",                "4.1.6r74567",                       0 },
        { "4.1.7r74567",                "4.1.6r74567",                       1 },
        { "4.1.5r74567",                "4.1.6r74567",                      -1 },
        { "4.1.6r74567-ENTERPRISE",     "4.1.6r74567",                       1 }  /* The tagged version is "newer". */
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTests); iTest++)
    {
        int iResult = RTStrVersionCompare(aTests[iTest].pszVer1, aTests[iTest].pszVer2);
        if (iResult != aTests[iTest].iResult)
            RTTestFailed(hTest, "#%u: '%s' <-> '%s' -> %d, expected %d",
                         iTest, aTests[iTest].pszVer1, aTests[iTest].pszVer2, iResult, aTests[iTest].iResult);

        iResult = -RTStrVersionCompare(aTests[iTest].pszVer2, aTests[iTest].pszVer1);
        if (iResult != aTests[iTest].iResult)
            RTTestFailed(hTest, "#%u: '%s' <-> '%s' -> %d, expected %d [inv]",
                         iTest, aTests[iTest].pszVer1, aTests[iTest].pszVer2, iResult, aTests[iTest].iResult);
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

