/* $Id: tstRTNoCrt-4.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Testcases for the No-CRT vector bits.
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

#include <iprt/nocrt/vector>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RTTEST  g_hTest;


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTNoCrt-4", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * No-CRT vector testcases.
     */
    std::vector<int> v;
    v.clear();
    RTTESTI_CHECK(v.size() == 0);
    RTTESTI_CHECK(v.empty());
    v.push_back(42);
    RTTESTI_CHECK(v.size() == 1);
    v.pop_back();
    RTTESTI_CHECK(v.size() == 0);
    v.push_back(42);
    RTTESTI_CHECK(v.front() == 42);
    RTTESTI_CHECK(v.back() == 42);
    v.push_back(22);
    RTTESTI_CHECK(v.front() == 42);
    RTTESTI_CHECK(v[0] == 42);
    RTTESTI_CHECK(v.back() == 22);
    RTTESTI_CHECK(v[1] == 22);
    v.pop_back();
    v.pop_back();
    RTTESTI_CHECK(v.empty());

    return RTTestSummaryAndDestroy(g_hTest);
}
