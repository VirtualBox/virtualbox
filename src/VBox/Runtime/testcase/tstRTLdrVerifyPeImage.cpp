/* $Id: tstRTLdrVerifyPeImage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Testing the Authenticode signature verification code.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <iprt/ldr.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <iprt/md5.h>
#include <iprt/sha.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int g_iDummy = 0;


static DECLCALLBACK(int) TestCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo, PRTERRINFO pErrInfo, void *pvUser)
{
    RT_NOREF(hLdrMod, pInfo, pErrInfo, pvUser);
    return VINF_SUCCESS;
}


int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstAuthenticode", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Process input.
     */
    for (int i = 1; i < argc; i++)
    {
        const char *pszFullName = argv[i];
        const char *pszFilename = RTPathFilename(pszFullName);
        RTTestSub(hTest, pszFilename);

        RTLDRMOD hLdrMod;
        int rc = RTLdrOpen(pszFullName, RTLDR_O_FOR_VALIDATION, RTLDRARCH_WHATEVER, &hLdrMod);
        if (RT_SUCCESS(rc))
        {
            uint8_t abHash[128];
            char    szDigest[512];

            RTTESTI_CHECK_RC(rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_MD5, abHash, sizeof(abHash)), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTMd5ToString(abHash, szDigest, sizeof(szDigest));
                RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "md5=%s\n", szDigest);
            }
            RTTESTI_CHECK_RC(rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA1, abHash, sizeof(abHash)), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTSha1ToString(abHash, szDigest, sizeof(szDigest));
                RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "sha1=%s\n", szDigest);
            }
            RTTESTI_CHECK_RC(rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA256, abHash, sizeof(abHash)), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTSha256ToString(abHash, szDigest, sizeof(szDigest));
                RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "sha256=%s\n", szDigest);
            }
            RTTESTI_CHECK_RC(rc = RTLdrHashImage(hLdrMod, RTDIGESTTYPE_SHA512, abHash, sizeof(abHash)), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTSha512ToString(abHash, szDigest, sizeof(szDigest));
                RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "sha512=%s\n", szDigest);
            }

            if (rc != VERR_NOT_SUPPORTED)
            {
                RTERRINFOSTATIC ErrInfo;
                RTErrInfoInitStatic(&ErrInfo);
                rc = RTLdrVerifySignature(hLdrMod, TestCallback, &g_iDummy, &ErrInfo.Core);
                if (RT_FAILURE(rc))
                    RTTestFailed(hTest, "%s: %s (rc=%Rrc)", pszFilename, ErrInfo.Core.pszMsg, rc);
            }
            RTLdrClose(hLdrMod);
        }
        else
            RTTestFailed(hTest, "Error opening '%s': %Rrc\n", pszFullName, rc);
    }

    return RTTestSummaryAndDestroy(hTest);
}

