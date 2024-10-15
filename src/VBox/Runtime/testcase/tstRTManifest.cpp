/* $Id: tstRTManifest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Manifest files.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/manifest.h>

#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/test.h>




/**
 * Basic API checks.
 */
static void tst1(void)
{
    RTTestISub("Manifest creation");

    size_t cbSize = 0;
    size_t iFailed = 0;

    /*
     * test1.txt = "This is a test text."
     * test2.txt = "Another test text."
     */
    static RTMANIFESTTEST /*const*/ s_aFiles[] = /** @todo API misdesign, this should be const. */
    {
        { "test1.txt", "794a8cc644b318ae6461aeea62915e399e441e8"  },
        { "test2.txt", "f17393902ee94c1e8bbd4bf417cdc70051feca00" }
    };
    static const char s_szTestPattern[] =
        "SHA1 (test1.txt)= 794a8cc644b318ae6461aeea62915e399e441e8\n"
        "SHA1 (test2.txt)= f17393902ee94c1e8bbd4bf417cdc70051feca00\n"
    ;

    void *pvBuf = NULL;
    RTTESTI_CHECK_RC_RETV(RTManifestWriteFilesBuf(&pvBuf, &cbSize, RTDIGESTTYPE_SHA1, s_aFiles, 2), VINF_SUCCESS);

    /* Check returned memory size */
    RTTESTI_CHECK_RETV(cbSize == strlen(s_szTestPattern));

    /* Check for correct manifest file content */
    RTTESTI_CHECK(memcmp(pvBuf, s_szTestPattern, cbSize) == 0);

    RTTestISub("Manifest verify");
    RTTESTI_CHECK_RC(RTManifestVerifyFilesBuf(pvBuf, cbSize, s_aFiles, 2, 0), VINF_SUCCESS);

    /* To little files to check */
    RTTESTI_CHECK_RC(RTManifestVerifyFilesBuf(pvBuf, cbSize, s_aFiles, 1, 0), VERR_MANIFEST_FILE_MISMATCH);

    /* Make the digest type invalid */
    ((char*)pvBuf)[0] = 'L';
    RTTESTI_CHECK_RC(RTManifestVerifyFilesBuf(pvBuf, cbSize, s_aFiles, 2, 0), VERR_MANIFEST_UNSUPPORTED_DIGEST_TYPE);
    ((char*)pvBuf)[0] = 'S'; /* Restore */

    /* Make the file name invalid */
    ((char*)pvBuf)[8] = 'z';
    RTTESTI_CHECK_RC(RTManifestVerifyFilesBuf(pvBuf, cbSize, s_aFiles, 2, 0), VERR_MANIFEST_FILE_MISMATCH);
    ((char*)pvBuf)[8] = 's'; /* Restore */

    /* Make the second digest invalid */
    ((char*)pvBuf)[99] = '0';
    RTTESTI_CHECK_RC(RTManifestVerifyFilesBuf(pvBuf, cbSize, s_aFiles, 2, &iFailed), VERR_MANIFEST_DIGEST_MISMATCH);
    RTTESTI_CHECK(iFailed == 1);

    /* Cleanup */
    RTMemFree(pvBuf);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTManifest", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tst1();

    return RTTestSummaryAndDestroy(hTest);
}

