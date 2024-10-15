/* $Id: rtProcInitExePath-netbsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - rtProcInitName, NetBSD.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <sys/param.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <link.h>

#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include "internal/process.h"
#include "internal/path.h"


DECLHIDDEN(int) rtProcInitExePath(char *pszPath, size_t cchPath)
{
    /*
     * Read the /proc/curproc/file link, convert to native and return it.
     */
    int cchLink = readlink("/proc/curproc/exe", pszPath, cchPath - 1);
    if (cchLink > 0 && (size_t)cchLink <= cchPath - 1)
    {
        pszPath[cchLink] = '\0';

        char const *pszTmp;
        int rc = rtPathFromNative(&pszTmp, pszPath, NULL);
        AssertMsgRCReturn(rc, ("rc=%Rrc pszLink=\"%s\"\nhex: %.*Rhxs\n", rc, pszPath, cchLink, pszPath), rc);
        if (pszTmp != pszPath)
        {
            rc = RTStrCopy(pszPath, cchPath, pszTmp);
            rtPathFreeIprt(pszTmp, pszPath);
        }
        return rc;
    }

    int err = errno;

    /*
     * Fall back on the dynamic linker since /proc is optional.
     */
    void *hExe = dlopen(NULL, 0);
    if (hExe)
    {
        struct link_map const *pLinkMap = 0;
        if (dlinfo(hExe, RTLD_DI_LINKMAP, &pLinkMap) == 0)
        {
            const char *pszImageName = pLinkMap->l_name;
            if (*pszImageName == '/') /* this may not always be absolute, despite the docs. :-( */
            {
                char const *pszTmp;
                int rc = rtPathFromNative(&pszTmp, pszImageName, NULL);
                AssertMsgRCReturn(rc, ("rc=%Rrc pszImageName=\"%s\"\n", rc, pszImageName), rc);
                if (pszTmp != pszPath)
                {
                    rc = RTStrCopy(pszPath, cchPath, pszTmp);
                    rtPathFreeIprt(pszTmp, pszPath);
                }
                return rc;
            }
            /** @todo Try search the PATH for the file name or append the current
             *        directory, which ever makes sense... */
        }
    }

    int rc = RTErrConvertFromErrno(err);
    AssertMsgFailed(("rc=%Rrc err=%d cchLink=%d hExe=%p\n", rc, err, cchLink, hExe));
    return rc;
}
