/* $Id: tstRTFsQueries.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTFs Queries..
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
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/errcore.h>


struct TSTCTX
{
    int var;
    int rc;
} TestCtx = { 42, VINF_SUCCESS };

static DECLCALLBACK(int) mountpointsEnumCallback(const char *pszMountpoint, void *pvUser)
{
    TSTCTX *pCtx = (TSTCTX *)pvUser;
    RTTESTI_CHECK(pCtx->var == 42);

    RTTestIPrintf(RTTESTLVL_ALWAYS, "Mountpoint: %s\n", pszMountpoint);

    return pCtx->rc;
}

static void tstMountpointsEnum(RTTEST hTest)
{
    RTTEST_CHECK_RC_OK(hTest, RTFsMountpointsEnum(mountpointsEnumCallback, &TestCtx));
}

/** @todo r=andy Overhaul this code to make use of the RTTEST APIs. */
int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, 0 /* fFlags */, "tstRTFsQueries", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Process all arguments (including the executable).
     */
    int cErrors = 0;
    for (int i = 0; i < argc; i++)
    {
        RTPrintf("tstRTFsQueries: '%s'...\n", argv[i]);

        uint32_t u32Serial;
        int rc = RTFsQuerySerial(argv[i], &u32Serial);
        if (RT_SUCCESS(rc))
            RTPrintf("tstRTFsQueries: u32Serial=%#010RX32\n", u32Serial);
        else
        {
            RTPrintf("tstRTFsQueries: RTFsQuerySerial failed, rc=%Rrc\n", rc);
            cErrors++;
        }

        RTFOFF cbTotal = 42;
        RTFOFF cbFree  = 42;
        uint32_t cbBlock = 42;
        uint32_t cbSector = 42;
        rc = RTFsQuerySizes(argv[i], &cbTotal, &cbFree, &cbBlock, &cbSector);
        if (RT_SUCCESS(rc))
            RTPrintf("tstRTFsQueries: cbTotal=%RTfoff cbFree=%RTfoff cbBlock=%d cbSector=%d\n",
                     cbTotal, cbFree, cbBlock, cbSector);
        else
        {
            RTPrintf("tstRTFsQueries: RTFsQuerySerial failed, rc=%Rrc\n", rc);
            cErrors++;
        }

        rc = RTFsQuerySizes(argv[i], NULL, NULL, NULL, NULL);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstRTFsQueries: RTFsQuerySizes(nop) failed, rc=%Rrc\n", rc);
            cErrors++;
        }

        RTFSTYPE enmType;
        rc = RTFsQueryType(argv[i], &enmType);
        if (RT_SUCCESS(rc))
            RTPrintf("tstRTFsQueries: file system type is '%s'\n", RTFsTypeName(enmType));
        else
        {
            RTPrintf("tstRTFsQueries: RTFsQueryType failed, rc=%Rrc\n", rc);
            cErrors++;
        }

        RTFSPROPERTIES Props;
        rc = RTFsQueryProperties(argv[i], &Props);
        if (RT_SUCCESS(rc))
            RTPrintf("tstRTFsQueries: cbMaxComponent=%u %s %s %s %s %s %s\n",
                     Props.cbMaxComponent,
                     Props.fCaseSensitive ? "case" : "not-case",
                     Props.fCompressed ? "compressed" : "not-compressed",
                     Props.fFileCompression ? "file-compression" : "no-file-compression",
                     Props.fReadOnly ? "readonly" : "readwrite",
                     Props.fRemote ? "remote" : "not-remote",
                     Props.fSupportsUnicode ? "supports-unicode" : "doesn't-support-unicode");
        else
        {
            RTPrintf("tstRTFsQueries: RTFsQueryProperties failed, rc=%Rrc\n", rc);
            cErrors++;
        }
    }

    tstMountpointsEnum(hTest);

    if (cErrors)
        RTTestFailed(hTest, "tstRTFsQueries: FAIlURE - %u errors\n", cErrors);

    /*
     * Done.
     */
    return RTTestSummaryAndDestroy(hTest);
}
