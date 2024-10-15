/* $Id: RTProcDaemonize-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTProcDaemonize, generic implementation.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/process.h"


RTR3DECL(int) RTProcDaemonize(const char * const *papszArgs, const char *pszDaemonizedOpt)
{
    /*
     * Get the executable name.
     * If this asserts, it's probably because rtR3Init hasn't been called.
     */
    char szExecPath[RTPATH_MAX];
    AssertReturn(RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)) == szExecPath, VERR_WRONG_ORDER);

    /*
     * Create a copy of the argument list with the daemonized option appended.
     */
    unsigned cArgs = 0;
    while (papszArgs[cArgs])
        cArgs++;

    char const **papszNewArgs = (char const **)RTMemAlloc(sizeof(const char *) * (cArgs + 2));
    if (!papszNewArgs)
        return VERR_NO_MEMORY;
    for (unsigned i = 0; i < cArgs; i++)
        papszNewArgs[i] = papszArgs[i];
    papszNewArgs[cArgs] = pszDaemonizedOpt;
    papszNewArgs[cArgs + 1] = NULL;

    /*
     * Open the bitbucket handles and create the detached process.
     */
    RTHANDLE hStdIn;
    int rc = RTFileOpenBitBucket(&hStdIn.u.hFile, RTFILE_O_READ);
    if (RT_SUCCESS(rc))
    {
        hStdIn.enmType = RTHANDLETYPE_FILE;

        RTHANDLE hStdOutAndErr;
        rc = RTFileOpenBitBucket(&hStdOutAndErr.u.hFile, RTFILE_O_WRITE);
        if (RT_SUCCESS(rc))
        {
            hStdOutAndErr.enmType = RTHANDLETYPE_FILE;

            rc = RTProcCreateEx(szExecPath, papszNewArgs, RTENV_DEFAULT,
                                RTPROC_FLAGS_DETACHED | RTPROC_FLAGS_SAME_CONTRACT,
                                &hStdIn, &hStdOutAndErr, &hStdOutAndErr,
                                NULL /*pszAsUser*/,  NULL /*pszPassword*/, NULL /*pExtraData*/, NULL /*phProcess*/);

            RTFileClose(hStdOutAndErr.u.hFile);
        }

        RTFileClose(hStdIn.u.hFile);
    }
    RTMemFree(papszNewArgs);
    return rc;
}

