/* $Id: tstRTSystemQueryOsInfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSystemQueryOSInfo.
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
#include <iprt/system.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTSystemQueryOsInfo", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Simple stuff.
     */
    char szInfo[_4K];

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szInfo, sizeof(szInfo));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "PRODUCT: \"%s\", rc=%Rrc\n", szInfo, rc);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szInfo, sizeof(szInfo));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "RELEASE: \"%s\", rc=%Rrc\n", szInfo, rc);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szInfo, sizeof(szInfo));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "VERSION: \"%s\", rc=%Rrc\n", szInfo, rc);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szInfo, sizeof(szInfo));
    RTTestIPrintf(RTTESTLVL_ALWAYS, "SERVICE_PACK: \"%s\", rc=%Rrc\n", szInfo, rc);

    uint64_t cbTotal;
    rc = RTSystemQueryTotalRam(&cbTotal);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Total RAM: %'RU64 Bytes (%RU64 KB, %RU64 MB)\n",
                  cbTotal, cbTotal / _1K, cbTotal / _1M);

    uint64_t cbAvailable;
    rc = RTSystemQueryAvailableRam(&cbAvailable);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Available RAM: %'RU64 Bytes (%RU64 KB, %RU64 MB)\n",
                  cbAvailable, cbAvailable / _1K, cbAvailable / _1M);

    /*
     * Check that unsupported stuff is terminated correctly.
     */
    for (int i = RTSYSOSINFO_INVALID + 1; i < RTSYSOSINFO_END; i++)
    {
        memset(szInfo, ' ', sizeof(szInfo));
        rc = RTSystemQueryOSInfo((RTSYSOSINFO)i, szInfo, sizeof(szInfo));
        if (    rc == VERR_NOT_SUPPORTED
            &&  szInfo[0] != '\0')
            RTTestIFailed("level=%d; unterminated buffer on VERR_NOT_SUPPORTED\n", i);
        else if (RT_SUCCESS(rc) || rc == VERR_BUFFER_OVERFLOW)
            RTTESTI_CHECK(RTStrEnd(szInfo, sizeof(szInfo)) != NULL);
        else if (rc != VERR_NOT_SUPPORTED)
            RTTestIFailed("level=%d unexpected rc=%Rrc\n", i, rc);
    }

    /*
     * Check buffer overflow
     */
    RTAssertSetQuiet(true);
    RTAssertSetMayPanic(false);
    for (int i = RTSYSDMISTR_INVALID + 1; i < RTSYSDMISTR_END; i++)
    {
        RTTESTI_CHECK_RC(RTSystemQueryDmiString((RTSYSDMISTR)i, szInfo, 0), VERR_INVALID_PARAMETER);

        /* Get the length of the info and check that we get overflow errors for
           everything less that it.  */
        rc = RTSystemQueryOSInfo((RTSYSOSINFO)i, szInfo, sizeof(szInfo));
        if (RT_FAILURE(rc))
            continue;
        size_t const cchInfo = strlen(szInfo);

        for (size_t cch = 1; cch < sizeof(szInfo) && cch < cchInfo; cch++)
        {
            memset(szInfo, 0x7f, sizeof(szInfo));
            RTTESTI_CHECK_RC(RTSystemQueryOSInfo((RTSYSOSINFO)i, szInfo, cch), VERR_BUFFER_OVERFLOW);

            /* check the padding. */
            for (size_t off = cch; off < sizeof(szInfo); off++)
                if (szInfo[off] != 0x7f)
                {
                    RTTestIFailed("level=%d, rc=%Rrc, cch=%zu, off=%zu: Wrote too much!\n", i, rc, cch, off);
                    break;
                }

            /* check for zero terminator. */
            if (!RTStrEnd(szInfo, cch))
                RTTestIFailed("level=%d, rc=%Rrc, cch=%zu: Buffer not terminated!\n", i, rc, cch);
        }

        /* Check that the exact length works. */
        rc = RTSystemQueryOSInfo((RTSYSOSINFO)i, szInfo, cchInfo + 1);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("level=%d: rc=%Rrc when specifying exactly right buffer length (%zu)\n", i, rc, cchInfo + 1);
    }

#if defined RT_OS_WINDOWS
    RTTestISub("Windows Features");
    struct
    {
        const char    *pszDesc;
        RTSYSNTFEATURE enmFeature;
        int            rc;
    } s_aNtFeatures[] =
    {
        { "Core Isolation (Memory Integrity)", RTSYSNTFEATURE_CORE_ISOLATION_MEMORY_INTEGRITY, VINF_SUCCESS }
    };

    for (size_t i = 0; i < RT_ELEMENTS(s_aNtFeatures); i++)
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Testing '%s': ", s_aNtFeatures[i].pszDesc);
        bool fEnabled = false;
        int rcTst = RTSystemQueryNtFeatureEnabled(s_aNtFeatures[i].enmFeature, &fEnabled);
        if (RT_SUCCESS(rcTst))
            RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", fEnabled ? "ENABLED\n" : "DISABLED\n");
        else if (rc == VERR_NOT_SUPPORTED) /* Don't freak out on older (host) OSes which don't have this feature. */
            RTTestIPrintf(RTTESTLVL_ALWAYS, "SKIPPED (not supported)\n");
        else
            RTTestIPrintf(RTTESTLVL_ALWAYS, "ERROR (%Rrc)\n", rcTst);
        RTTESTI_CHECK_RC(rcTst, s_aNtFeatures[i].rc);
    }
#endif

    return RTTestSummaryAndDestroy(hTest);
}

