/* $Id: tstRTShMem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTShMem.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/shmem.h>

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global shared memory object used across all tests. */
static RTSHMEM g_hShMem = NIL_RTSHMEM;
/** Data to read/write initially. */
static char g_szDataBefore[] = "Data before modification!";
/** Data to read/write for the modification. */
static char g_szDataAfter[] = "Data after modification!";



static void tstRTShMem2(void)
{
    RTTestISub("Negative");

    /** @todo */
}


static void tstRTShMem1(void)
{
    RTTestISub("Basics");

    /* create and destroy. */
    int rc = RTShMemOpen(&g_hShMem, "tstRTShMem-Share", RTSHMEM_O_F_CREATE_EXCL | RTSHMEM_O_F_READWRITE | RTSHMEM_O_F_MAYBE_EXEC,
                         _512K, 0);
    if (RT_FAILURE(rc))
    {
        RTTESTI_CHECK_RC_RETV(rc, VERR_ALREADY_EXISTS);
        RTTESTI_CHECK_RC(RTShMemDelete("tstRTShMem-Share"), VINF_SUCCESS);
        RTTESTI_CHECK_RC_RETV(RTShMemOpen(&g_hShMem, "tstRTShMem-Share", RTSHMEM_O_F_CREATE | RTSHMEM_O_F_READWRITE | RTSHMEM_O_F_MAYBE_EXEC,
                                          _512K, 0),
                              VINF_SUCCESS);
    }

    RTTESTI_CHECK_RETV(g_hShMem != NIL_RTSHMEM);

    /* Query the size. */
    size_t cbShMem = 0;
    RTTESTI_CHECK_RC(RTShMemQuerySize(g_hShMem, &cbShMem), VINF_SUCCESS);
    RTTESTI_CHECK(cbShMem == _512K);

    /* Create a mapping. */
    void *pvMap;
    RTTESTI_CHECK_RC_RETV(RTShMemMapRegion(g_hShMem, 0, cbShMem, RTSHMEM_MAP_F_READ | RTSHMEM_MAP_F_WRITE, &pvMap), VINF_SUCCESS);
    memset(pvMap, 0, cbShMem);
    memcpy(pvMap, &g_szDataBefore[0], sizeof(g_szDataBefore));

    /* Open the shared memory object and create a second mapping. */
    RTSHMEM hShMemRead = NIL_RTSHMEM;
    RTTESTI_CHECK_RC_RETV(RTShMemOpen(&hShMemRead, "tstRTShMem-Share", RTSHMEM_O_F_READWRITE | RTSHMEM_O_F_MAYBE_EXEC,
                                      0, 0),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RETV(hShMemRead != NIL_RTSHMEM);

    void *pvMapRead = NULL;
    RTTESTI_CHECK_RC(RTShMemQuerySize(hShMemRead, &cbShMem), VINF_SUCCESS);
    RTTESTI_CHECK(cbShMem == _512K);
    RTTESTI_CHECK_RC_RETV(RTShMemMapRegion(hShMemRead, 0, cbShMem, RTSHMEM_MAP_F_READ | RTSHMEM_MAP_F_WRITE, &pvMapRead), VINF_SUCCESS);
    RTTESTI_CHECK(!memcmp(pvMapRead, &g_szDataBefore[0], sizeof(g_szDataBefore)));
    RTTESTI_CHECK(!memcmp(pvMapRead, pvMap, cbShMem));

    /* Alter the data in the first mapping and check that it is visible in the second one. */
    memcpy(pvMap, &g_szDataAfter[0], sizeof(g_szDataAfter));
    RTTESTI_CHECK(!memcmp(pvMapRead, &g_szDataAfter[0], sizeof(g_szDataAfter)));
    RTTESTI_CHECK(!memcmp(pvMapRead, pvMap, cbShMem));

    RTTESTI_CHECK_RC(RTShMemUnmapRegion(hShMemRead, pvMapRead), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTShMemClose(hShMemRead), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTShMemUnmapRegion(g_hShMem, pvMap), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTShMemClose(g_hShMem), VINF_SUCCESS);
    g_hShMem = NIL_RTSHMEM;
}

int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTShMem", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * The tests.
     */
    tstRTShMem1();
    if (RTTestErrorCount(hTest) == 0)
    {
        bool fMayPanic = RTAssertMayPanic();
        bool fQuiet    = RTAssertAreQuiet();
        RTAssertSetMayPanic(false);
        RTAssertSetQuiet(true);
        tstRTShMem2();
        RTAssertSetQuiet(fQuiet);
        RTAssertSetMayPanic(fMayPanic);
    }

    if (g_hShMem != NIL_RTSHMEM)
    {
        RTTESTI_CHECK_RC(RTShMemClose(g_hShMem), VINF_SUCCESS);
        g_hShMem = NIL_RTSHMEM;
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

