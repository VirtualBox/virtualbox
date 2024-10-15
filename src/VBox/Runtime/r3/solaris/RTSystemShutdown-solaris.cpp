/* $Id: RTSystemShutdown-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemShutdown, linux implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/process.h>
#include <iprt/string.h>


RTDECL(int) RTSystemShutdown(RTMSINTERVAL cMsDelay, uint32_t fFlags, const char *pszLogMsg)
{
    AssertPtrReturn(pszLogMsg, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTSYSTEM_SHUTDOWN_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Assemble the argument vector.
     */
    int         iArg = 0;
    const char *apszArgs[8];

    RT_BZERO(apszArgs, sizeof(apszArgs));

    apszArgs[iArg++] = "/usr/sbin/shutdown";
    apszArgs[iArg++] = "-y"; /* Pre-answer confirmation question. */
    apszArgs[iArg++] = "-i"; /* Change to the following state. */
    switch (fFlags & RTSYSTEM_SHUTDOWN_ACTION_MASK)
    {
        case RTSYSTEM_SHUTDOWN_HALT:
            apszArgs[iArg++] = "0";
            break;
        case RTSYSTEM_SHUTDOWN_REBOOT:
            apszArgs[iArg++] = "6";
            break;
        case RTSYSTEM_SHUTDOWN_POWER_OFF:
        case RTSYSTEM_SHUTDOWN_POWER_OFF_HALT:
            apszArgs[iArg++] = "5";
            break;
    }

    apszArgs[iArg++] = "-g"; /* Grace period. */

    char szWhen[80];
    if (cMsDelay < 500)
        strcpy(szWhen, "0");
    else
        RTStrPrintf(szWhen, sizeof(szWhen), "%u", (unsigned)((cMsDelay + 499) / 1000));
    apszArgs[iArg++] = szWhen;

    apszArgs[iArg++] = pszLogMsg;


    /*
     * Start the shutdown process and wait for it to complete.
     */
    RTPROCESS hProc;
    int rc = RTProcCreate(apszArgs[0], apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, &hProc);
    if (RT_FAILURE(rc))
        return rc;

    RTPROCSTATUS ProcStatus;
    rc = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus);
    if (RT_SUCCESS(rc))
    {
        if (   ProcStatus.enmReason != RTPROCEXITREASON_NORMAL
            || ProcStatus.iStatus   != 0)
            rc = VERR_SYS_SHUTDOWN_FAILED;
    }

    return rc;
}

