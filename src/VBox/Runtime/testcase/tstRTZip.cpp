/* $Id: tstRTZip.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTZip, kind of.
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
#include <iprt/zip.h>

#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/test.h>


static void testFile(const char *pszFilename)
{
    size_t  cbSrcActually = 0;
    void   *pvSrc;
    size_t  cbSrc;
    int rc = RTFileReadAll(pszFilename, &pvSrc, &cbSrc);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    size_t  cbDstActually = 0;
    size_t  cbDst = RT_MAX(cbSrc * 8, _1M);
    void   *pvDst = RTMemAllocZ(cbDst);

    rc = RTZipBlockDecompress(RTZIPTYPE_ZLIB, 0, pvSrc, cbSrc, &cbSrcActually, pvDst, cbDst, &cbDstActually);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "cbSrc=%zu cbSrcActually=%zu cbDst=%zu cbDstActually=%zu rc=%Rrc\n",
                  cbSrc, cbSrcActually, cbDst, cbDstActually, rc);
    RTTESTI_CHECK_RC_OK(rc);

}


int main(int argc, char **argv)
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTZip", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    if (argc > 1)
    {
        for (int i = 1; i < argc; i++)
            testFile(argv[i]);
    }
    else
    {
        /** @todo testcase */
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

