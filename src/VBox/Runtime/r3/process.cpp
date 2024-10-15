/* $Id: process.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Process, Common.
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
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include "internal/process.h"
#include "internal/thread.h"

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#else
# include <unistd.h>
#endif


/**
 * Get the identifier for the current process.
 *
 * @returns Process identifier.
 */
RTDECL(RTPROCESS) RTProcSelf(void)
{
    RTPROCESS Self = g_ProcessSelf;
    if (Self != NIL_RTPROCESS)
        return Self;

    /* lazy init. */
#ifdef _MSC_VER
    Self = GetCurrentProcessId(); /* since NT 3.1, not 3.51+ as listed on geoffchappell.com */
#else
    Self = getpid();
#endif
    g_ProcessSelf = Self;
    return Self;
}


/**
 * Attempts to alter the priority of the current process.
 *
 * @returns iprt status code.
 * @param   enmPriority     The new priority.
 */
RTR3DECL(int) RTProcSetPriority(RTPROCPRIORITY enmPriority)
{
    if (enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST)
        return rtThreadDoSetProcPriority(enmPriority);
    AssertMsgFailed(("enmPriority=%d\n", enmPriority));
    return VERR_INVALID_PARAMETER;
}


/**
 * Gets the current priority of this process.
 *
 * @returns The priority (see RTPROCPRIORITY).
 */
RTR3DECL(RTPROCPRIORITY) RTProcGetPriority(void)
{
    return g_enmProcessPriority;
}


RTR3DECL(char *) RTProcGetExecutablePath(char *pszExecPath, size_t cbExecPath)
{
    if (RT_UNLIKELY(g_szrtProcExePath[0] == '\0'))
        return NULL;

    /*
     * Calc the length and check if there is space before copying.
     */
    size_t cch = g_cchrtProcExePath;
    if (cch < cbExecPath)
    {
        memcpy(pszExecPath, g_szrtProcExePath, cch);
        pszExecPath[cch] = '\0';
        return pszExecPath;
    }

    AssertMsgFailed(("Buffer too small (%zu <= %zu)\n", cbExecPath, cch));
    return NULL;
}


RTR3DECL(const char *) RTProcExecutablePath(void)
{
    return g_szrtProcExePath;
}


RTR3DECL(const char *) RTProcShortName(void)
{
    return &g_szrtProcExePath[g_offrtProcName];
}

