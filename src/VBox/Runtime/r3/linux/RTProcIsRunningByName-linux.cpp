/* $Id: RTProcIsRunningByName-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTProcIsRunningByName, Linux implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/param.h>
#include <iprt/assert.h>

#include <unistd.h>


RTR3DECL(bool) RTProcIsRunningByName(const char *pszName)
{
    /*
     * Quick validation.
     */
    if (!pszName)
        return false;

    bool const fWithPath = RTPathHavePath(pszName);

    /*
     * Enumerate /proc.
     */
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, "/proc");
    AssertMsgRCReturn(rc, ("RTDirOpen on /proc failed: rc=%Rrc\n", rc), false);
    if (RT_SUCCESS(rc))
    {
        RTDIRENTRY DirEntry;
        while (RT_SUCCESS(RTDirRead(hDir, &DirEntry, NULL)))
        {
            /*
             * Filter numeric directory entries only.
             */
            if (   (   DirEntry.enmType == RTDIRENTRYTYPE_DIRECTORY
                    || DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN)
                && RTStrToUInt32(DirEntry.szName) > 0)
            {
                /*
                 * Try readlink on exe first since it's more faster and reliable.
                 * Fall back on reading the first line in cmdline if that fails
                 * (access errors typically). cmdline is unreliable as it might
                 * contain whatever the execv caller passes as argv[0].
                 */
                char szName[RTPATH_MAX];
                RTStrPrintf(szName, sizeof(szName), "/proc/%s/exe", &DirEntry.szName[0]);
                char szExe[RTPATH_MAX];
                int cchLink = readlink(szName, szExe, sizeof(szExe) - 1);
                if (    cchLink > 0
                    &&  (size_t)cchLink < sizeof(szExe))
                {
                    szExe[cchLink] = '\0';
                    rc = VINF_SUCCESS;
                }
                else
                {
                    RTStrPrintf(szName, sizeof(szName), "/proc/%s/cmdline", &DirEntry.szName[0]);
                    PRTSTREAM pStream;
                    rc = RTStrmOpen(szName, "r", &pStream);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTStrmGetLine(pStream, szExe, sizeof(szExe));
                        RTStrmClose(pStream);
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * We are interested on the file name part only.
                     */
                    char const *pszProcName = fWithPath ? szExe : RTPathFilename(szExe);
                    if (RTStrCmp(pszProcName, pszName) == 0)
                    {
                        /* Found it! */
                        RTDirClose(hDir);
                        return true;
                    }
                }
            }
        }
        RTDirClose(hDir);
    }

    return false;
}

