/* $Id: RTThreadGetNativeState-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTThreadGetNativeState, linux implementation.
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
#include <iprt/thread.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>

#include "internal/thread.h"

#include <unistd.h>
#include <sys/fcntl.h>


RTDECL(RTTHREADNATIVESTATE) RTThreadGetNativeState(RTTHREAD hThread)
{
    RTTHREADNATIVESTATE enmRet  = RTTHREADNATIVESTATE_INVALID;
    PRTTHREADINT        pThread = rtThreadGet(hThread);
    if (pThread)
    {
        enmRet = RTTHREADNATIVESTATE_UNKNOWN;

        char szName[512];
        RTStrPrintf(szName, sizeof(szName), "/proc/self/task/%u/stat", pThread->tid);
        int fd = open(szName, O_RDONLY, 0);
        if (fd >= 0)
        {
            ssize_t cch = read(fd, szName, sizeof(szName) - 1);
            close(fd);
            if (cch > 0)
            {
                szName[cch] = '\0';

                /* skip the pid, the (comm name) and stop at the status char. */
                const char *psz = szName;
                while (   *psz
                       && (   *psz != ')'
                           || !RT_C_IS_SPACE(psz[1])
                           || !RT_C_IS_ALPHA(psz[2])
                           || !RT_C_IS_SPACE(psz[3])
                          )
                      )
                    psz++;
                if (*psz == ')')
                {
                    switch (psz[2])
                    {
                        case 'R':   /* running */
                            enmRet = RTTHREADNATIVESTATE_RUNNING;
                            break;

                        case 'S':   /* sleeping */
                        case 'D':   /* disk sleeping */
                            enmRet = RTTHREADNATIVESTATE_BLOCKED;
                            break;

                        case 'T':   /* stopped or tracking stop */
                            enmRet = RTTHREADNATIVESTATE_SUSPENDED;
                            break;

                        case 'Z':   /* zombie */
                        case 'X':   /* dead */
                            enmRet = RTTHREADNATIVESTATE_TERMINATED;
                            break;

                        default:
                            AssertMsgFailed(("state=%c\n", psz[2]));
                            enmRet = RTTHREADNATIVESTATE_UNKNOWN;
                            break;
                    }
                }
                else
                    AssertMsgFailed(("stat='%s'\n", szName));
            }
        }
        rtThreadRelease(pThread);
    }
    return enmRet;
}

