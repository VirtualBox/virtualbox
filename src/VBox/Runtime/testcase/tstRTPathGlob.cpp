/* $Id: tstRTPathGlob.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Manual RTPathGlob test.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>


int main(int argc, char **argv)
{
    /*
     * Init RT+Test.
     */
    RTTEST hTest;
    int rc = RTTestInitExAndCreate(argc, &argv, 0, "tstRTPathGlob", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    if (argc <= 1)
        return RTTestSkipAndDestroy(hTest, "Requires arguments");


    /*
     * Manual glob testing.
     */
    for (int i = 1; i < argc; i++)
    {
        uint32_t            cResults = UINT32_MAX;
        PCRTPATHGLOBENTRY   pHead = (PCRTPATHGLOBENTRY)&cResults;
        rc = RTPathGlob(argv[i], 0, &pHead, &cResults);
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "#%u '%s' -> %Rrc cResult=%u\n", i, argv[i], rc, cResults);
        if (RT_SUCCESS(rc))
        {
            uint32_t iEntry = 0;
            for (PCRTPATHGLOBENTRY pCur = pHead; pCur; pCur = pCur->pNext, iEntry++)
            {
                RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "  #%3u: '%s'\n", iEntry, pCur->szPath);
                RTTEST_CHECK(hTest, strlen(pCur->szPath) == pCur->cchPath);
            }

            RTPathGlobFree(pHead);
        }
    }


    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

