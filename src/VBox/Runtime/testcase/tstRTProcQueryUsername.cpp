/* $Id: tstRTProcQueryUsername.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTProcQueryUsername.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include <iprt/test.h>


static void tstRTProcQueryUsername(void)
{
    RTTestISub("Basics");

    size_t  cbUser;
    char    szUser[1024];
    memset(szUser, '-', sizeof(szUser));

    /* negative stuff that may assert: */
    bool fMayPanic = RTAssertSetMayPanic(false);
    bool fQuiet    = RTAssertSetQuiet(true);

    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), NULL, 8, &cbUser), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), szUser, 0, &cbUser), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), NULL, 0, NULL), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(RTProcQueryUsernameA(RTProcSelf(), NULL), VERR_INVALID_POINTER);

    RTAssertSetMayPanic(fMayPanic);
    RTAssertSetQuiet(fQuiet);

    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), NULL, 0, &cbUser), VERR_BUFFER_OVERFLOW);
    memset(szUser, '-', sizeof(szUser));
    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), szUser, cbUser - 1, &cbUser), VERR_BUFFER_OVERFLOW);
    memset(szUser, '-', sizeof(szUser));
    RTTESTI_CHECK_RC(RTProcQueryUsername(RTProcSelf(), szUser, sizeof(szUser), &cbUser), VINF_SUCCESS);
    RTTestPrintf(NULL, RTTESTLVL_ALWAYS, "Username: %s\n", szUser); /* */

    char *pszUser = NULL;
    RTTESTI_CHECK_RC(RTProcQueryUsernameA(RTProcSelf(), &pszUser), VINF_SUCCESS);
    RTTestPrintf(NULL, RTTESTLVL_ALWAYS, "Username: %s\n", pszUser);
    RTTESTI_CHECK(strcmp(pszUser, szUser) == 0);
    RTStrFree(pszUser);
}


int main(int argc, char **argv)
{
    RT_NOREF_PV(argc); RT_NOREF_PV(argv);

    /*
     * Initialize IPRT and create the test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTProcQueryUsername", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstRTProcQueryUsername();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}
