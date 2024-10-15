/* $Id: RTSystemShutdown-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemShutdown, Windows.
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
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/win/windows.h>


RTDECL(int) RTSystemShutdown(RTMSINTERVAL cMsDelay, uint32_t fFlags, const char *pszLogMsg)
{
    AssertPtrReturn(pszLogMsg, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTSYSTEM_SHUTDOWN_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Before we start, try grant the necessary privileges.
     */
    DWORD  dwErr;
    HANDLE hToken = NULL;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, TRUE /*OpenAsSelf*/, &hToken))
        dwErr = NO_ERROR;
    else
    {
        dwErr  = GetLastError();
        if (dwErr == ERROR_NO_TOKEN)
        {
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
                dwErr = NO_ERROR;
            else
                dwErr = GetLastError();
        }
    }
    if (dwErr == NO_ERROR)
    {
        union
        {
            TOKEN_PRIVILEGES TokenPriv;
            char ab[sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES)];
        } u;
        u.TokenPriv.PrivilegeCount = 1;
        u.TokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValue(NULL /*localhost*/, SE_SHUTDOWN_NAME, &u.TokenPriv.Privileges[0].Luid))
        {
            if (!AdjustTokenPrivileges(hToken,
                                       FALSE /*DisableAllPrivileges*/,
                                       &u.TokenPriv,
                                       RT_UOFFSETOF(TOKEN_PRIVILEGES, Privileges[1]),
                                       NULL,
                                       NULL) )
                dwErr = GetLastError();
        }
        else
            dwErr = GetLastError();
        CloseHandle(hToken);
    }

    /*
     * Do some parameter conversion.
     */
    PRTUTF16 pwszLogMsg;
    int rc = RTStrToUtf16(pszLogMsg, &pwszLogMsg);
    if (RT_FAILURE(rc))
        return rc;
    DWORD cSecsTimeout = (cMsDelay + 499) / 1000;

    /*
     * If we're told to power off the system, we should try use InitiateShutdownW (6.0+)
     * or ExitWindowsEx (3.50) rather than InitiateSystemShutdownW, because these other
     * APIs allows us to explicitly specify that we want to power off.
     *
     * Note! For NT version 4, 3.51, and 3.50 the system may instaed reboot since the
     *       x86 HALs typically didn't know how to perform a power off.
     */
    bool fDone = false;
    if (   (fFlags & RTSYSTEM_SHUTDOWN_ACTION_MASK) == RTSYSTEM_SHUTDOWN_POWER_OFF
        || (fFlags & RTSYSTEM_SHUTDOWN_ACTION_MASK) == RTSYSTEM_SHUTDOWN_POWER_OFF_HALT)
    {
        /* This API has the grace period thing. */
        decltype(InitiateShutdownW) *pfnInitiateShutdownW;
        pfnInitiateShutdownW = (decltype(InitiateShutdownW) *)GetProcAddress(GetModuleHandleW(L"ADVAPI32.DLL"), "InitiateShutdownW");
        if (pfnInitiateShutdownW)
        {
            DWORD fShutdownFlags = SHUTDOWN_POWEROFF;
            if (fFlags & RTSYSTEM_SHUTDOWN_FORCE)
                fShutdownFlags |= SHUTDOWN_FORCE_OTHERS | SHUTDOWN_FORCE_SELF;
            DWORD fReason = SHTDN_REASON_MAJOR_OTHER | (fFlags & RTSYSTEM_SHUTDOWN_PLANNED ? SHTDN_REASON_FLAG_PLANNED : 0);
            dwErr = pfnInitiateShutdownW(NULL /*pwszMachineName*/, pwszLogMsg, cSecsTimeout, fShutdownFlags, fReason);
            if (dwErr == ERROR_INVALID_PARAMETER)
            {
                fReason &= ~SHTDN_REASON_FLAG_PLANNED; /* just in case... */
                dwErr = pfnInitiateShutdownW(NULL /*pwszMachineName*/, pwszLogMsg, cSecsTimeout, fShutdownFlags, fReason);
            }
            if (dwErr == ERROR_SUCCESS)
            {
                rc = VINF_SUCCESS;
                fDone = true;
            }
        }

        if (!fDone)
        {
            /* No grace period here, too bad. */
            decltype(ExitWindowsEx) *pfnExitWindowsEx;
            pfnExitWindowsEx = (decltype(ExitWindowsEx) *)GetProcAddress(GetModuleHandleW(L"USER32.DLL"), "ExitWindowsEx");
            if (pfnExitWindowsEx)
            {
                DWORD fExitWindows = EWX_POWEROFF | EWX_SHUTDOWN;
                if (fFlags & RTSYSTEM_SHUTDOWN_FORCE)
                    fExitWindows |= EWX_FORCE | EWX_FORCEIFHUNG;

                if (pfnExitWindowsEx(fExitWindows, SHTDN_REASON_MAJOR_OTHER))
                    fDone = true;
                else if (pfnExitWindowsEx(fExitWindows & ~EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER))
                    fDone = true;
            }
        }
    }

    /*
     * Fall back on the oldest API.
     */
    if (!fDone)
    {
        BOOL fRebootAfterShutdown = (fFlags & RTSYSTEM_SHUTDOWN_ACTION_MASK) == RTSYSTEM_SHUTDOWN_REBOOT
                                   ? TRUE : FALSE;
        BOOL fForceAppsClosed     = fFlags & RTSYSTEM_SHUTDOWN_FORCE ? TRUE : FALSE;
        if (InitiateSystemShutdownW(NULL /*pwszMachineName = NULL = localhost*/,
                                    pwszLogMsg,
                                    cSecsTimeout,
                                    fForceAppsClosed,
                                    fRebootAfterShutdown))
            rc = (fFlags & RTSYSTEM_SHUTDOWN_ACTION_MASK) == RTSYSTEM_SHUTDOWN_HALT ? VINF_SYS_MAY_POWER_OFF : VINF_SUCCESS;
        else
            rc = RTErrConvertFromWin32(dwErr);
    }

    RTUtf16Free(pwszLogMsg);
    return rc;
}
RT_EXPORT_SYMBOL(RTSystemShutdown);

