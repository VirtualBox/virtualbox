/* $Id: RTShutdown.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - System Shutdown.
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

#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--halt",         'H', RTGETOPT_REQ_NOTHING },
        { "--poweroff",     'p', RTGETOPT_REQ_NOTHING },
        { "--reboot",       'r', RTGETOPT_REQ_NOTHING },
        { "--force",        'f', RTGETOPT_REQ_NOTHING },
        { "--delay",        'd', RTGETOPT_REQ_UINT32  },
        { "--message",      'm', RTGETOPT_REQ_STRING  }
    };

    const char     *pszMsg   = "RTShutdown";
    RTMSINTERVAL    cMsDelay = 0;
    uint32_t        fFlags   = RTSYSTEM_SHUTDOWN_POWER_OFF | RTSYSTEM_SHUTDOWN_PLANNED;

    RTGETOPTSTATE   GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                      RTGETOPTINIT_FLAGS_OPTS_FIRST);
    for (;;)
    {
        RTGETOPTUNION ValueUnion;
        rc = RTGetOpt(&GetState, &ValueUnion);
        if (rc == 0)
            break;
        switch (rc)
        {
            case 'H': fFlags = (fFlags & ~RTSYSTEM_SHUTDOWN_ACTION_MASK) | RTSYSTEM_SHUTDOWN_HALT; break;
            case 'p': fFlags = (fFlags & ~RTSYSTEM_SHUTDOWN_ACTION_MASK) | RTSYSTEM_SHUTDOWN_POWER_OFF_HALT; break;
            case 'r': fFlags = (fFlags & ~RTSYSTEM_SHUTDOWN_ACTION_MASK) | RTSYSTEM_SHUTDOWN_REBOOT; break;
            case 'f': fFlags |= RTSYSTEM_SHUTDOWN_FORCE; break;
            case 'd': cMsDelay = ValueUnion.u32; break;
            case 'm': pszMsg = ValueUnion.psz; break;

            case 'h':
                RTPrintf("Usage: RTShutdown [-H|-p|-r] [-f] [-d <milliseconds>] [-m <msg>]\n");
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Do the deed.
     */
    rc = RTSystemShutdown(cMsDelay, fFlags, pszMsg);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTSystemShutdown(%u, %#x, \"%s\") returned %Rrc\n", cMsDelay, fFlags, pszMsg, rc);
    return RTEXITCODE_SUCCESS;
}

