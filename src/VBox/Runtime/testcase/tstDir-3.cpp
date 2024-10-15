/* $Id: tstDir-3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Directory listing & filtering (no parameters needed).
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

#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/string.h>

static int tstDirOpenFiltered(const char *pszFilter, unsigned *pcFilesMatch, int *pRc)
{
    int rcRet = 0;
    unsigned cFilesMatch = 0;
    RTDIR hDir;
    int rc = RTDirOpenFiltered(&hDir, pszFilter, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            RTDIRENTRY DirEntry;
            rc = RTDirRead(hDir, &DirEntry, NULL);
            if (RT_FAILURE(rc))
                break;
            cFilesMatch++;
        }

        if (rc != VERR_NO_MORE_FILES)
        {
            RTPrintf("tstDir-3: Enumeration '%s' failed! rc=%Rrc\n", pszFilter, rc);
            rcRet = 1;
        }

        /* close up */
        rc = RTDirClose(hDir);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstDir-3: Failed to close dir '%s'! rc=%Rrc\n", pszFilter, rc);
            rcRet = 1;
        }
    }
    else
    {
        RTPrintf("tstDir-3: Failed to open '%s', rc=%Rrc\n", pszFilter, rc);
        rcRet = 1;
    }

    *pcFilesMatch = cFilesMatch;
    *pRc = rc;
    return rcRet;
}

int main(int argc, char **argv)
{
    int rcRet = 0;
    int rcRet2;
    int rc;
    unsigned cMatch;
    RTR3InitExe(argc, &argv, 0);

    const char *pszTestDir = ".";

    char *pszFilter1 = RTPathJoinA(pszTestDir, "xyxzxq*");
    if (!pszFilter1)
    {
        RTPrintf("tstDir-3: cannot create non-match filter!\n");
        return 1;
    }

    char *pszFilter2 = RTPathJoinA(pszTestDir, "*");
    if (!pszFilter2)
    {
        RTPrintf("tstDir-3: cannot create match filter!\n");
        RTStrFree(pszFilter1);
        return 1;
    }

    rcRet2 = tstDirOpenFiltered(pszFilter1, &cMatch, &rc);
    if (rcRet2)
        rcRet = rcRet2;
    if (RT_FAILURE(rc))
        RTPrintf("tstDir-3: filter '%s' failed! rc=%Rrc\n", pszFilter1, rc);
    if (cMatch)
        RTPrintf("tstDir-3: filter '%s' gave wrong result count! cMatch=%u\n", pszFilter1, cMatch);

    rcRet2 = tstDirOpenFiltered(pszFilter2, &cMatch, &rc);
    if (rcRet2)
        rcRet = rcRet2;
    if (RT_FAILURE(rc))
        RTPrintf("tstDir-3: filter '%s' failed! rc=%Rrc\n", pszFilter2, rc);
    if (!cMatch)
        RTPrintf("tstDir-3: filter '%s' gave wrong result count! cMatch=%u\n", pszFilter2, cMatch);

    RTStrFree(pszFilter2);
    RTStrFree(pszFilter1);

    if (!rcRet)
        RTPrintf("tstDir-3: OK\n");
    return rcRet;
}
