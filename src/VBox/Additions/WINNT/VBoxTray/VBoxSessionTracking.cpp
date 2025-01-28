/* $Id: VBoxSessionTracking.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxSessionTracking.cpp - Session tracking.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/win/windows.h>

/* Default desktop state tracking */
#include <Wtsapi32.h>

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"


/* St (session [state] tracking) functionality API impl */

typedef struct VBOXST
{
    HWND hWTSAPIWnd;
    RTLDRMOD hLdrModWTSAPI32;
    BOOL fIsConsole;
    WTS_CONNECTSTATE_CLASS enmConnectState;
    UINT_PTR idDelayedInitTimer;
    BOOL (WINAPI * pfnWTSRegisterSessionNotification)(HWND hWnd, DWORD dwFlags);
    BOOL (WINAPI * pfnWTSUnRegisterSessionNotification)(HWND hWnd);
    BOOL (WINAPI * pfnWTSQuerySessionInformationA)(HANDLE hServer, DWORD SessionId, WTS_INFO_CLASS WTSInfoClass, LPTSTR *ppBuffer, DWORD *pBytesReturned);
} VBOXST;

static VBOXST gVBoxSt;

/** @todo r=andy Lacks documentation / Doxygen headers. What does this, and why? */

int vboxStCheckState()
{
    int rc = VINF_SUCCESS;
    WTS_CONNECTSTATE_CLASS *penmConnectState = NULL;
    USHORT *pProtocolType = NULL;
    DWORD cbBuf = 0;
    if (gVBoxSt.pfnWTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSConnectState,
                                               (LPTSTR *)&penmConnectState, &cbBuf))
    {
        if (gVBoxSt.pfnWTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSClientProtocolType,
                                                   (LPTSTR *)&pProtocolType, &cbBuf))
        {
            gVBoxSt.fIsConsole = (*pProtocolType == 0);
            gVBoxSt.enmConnectState = *penmConnectState;
            LogFlowFunc(("Session information: ProtocolType %d, ConnectState %d", *pProtocolType, *penmConnectState));
            return VINF_SUCCESS;
        }
        else
        {
            DWORD dwErr = GetLastError();
            LogWarnFunc(("WTSQuerySessionInformationA WTSClientProtocolType failed, error = %08X\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }
    }
    else
    {
        DWORD dwErr = GetLastError();
        LogWarnFunc(("WTSQuerySessionInformationA WTSConnectState failed, error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }

    /* failure branch, set to "console-active" state */
    gVBoxSt.fIsConsole = TRUE;
    gVBoxSt.enmConnectState = WTSActive;

    return rc;
}

int vboxStInit(HWND hWnd)
{
    RT_ZERO(gVBoxSt);
    int rc = RTLdrLoadSystem("WTSAPI32.DLL", true /*fNoUnload*/, &gVBoxSt.hLdrModWTSAPI32);

    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(gVBoxSt.hLdrModWTSAPI32, "WTSRegisterSessionNotification",
                            (void **)&gVBoxSt.pfnWTSRegisterSessionNotification);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(gVBoxSt.hLdrModWTSAPI32, "WTSUnRegisterSessionNotification",
                            (void **)&gVBoxSt.pfnWTSUnRegisterSessionNotification);
        if (RT_SUCCESS(rc))
            rc = RTLdrGetSymbol(gVBoxSt.hLdrModWTSAPI32, "WTSQuerySessionInformationA",
                             (void **)&gVBoxSt.pfnWTSQuerySessionInformationA);

        AssertRC(rc);
        RTLdrClose(gVBoxSt.hLdrModWTSAPI32);
    }

    if (RT_SUCCESS(rc))
    {
        gVBoxSt.hWTSAPIWnd = hWnd;
        if (gVBoxSt.pfnWTSRegisterSessionNotification(gVBoxSt.hWTSAPIWnd, NOTIFY_FOR_THIS_SESSION))
        {
            LogFunc(("hWnd(%p) registered for WTS session changes\n", gVBoxSt.hWTSAPIWnd));
            vboxStCheckState();
        }
        else
        {
            DWORD const dwErr = GetLastError();
            VBoxTrayInfo("WTSRegisterSessionNotification failed, error = %08X\n", dwErr);
            if (dwErr == RPC_S_INVALID_BINDING)
            {
                gVBoxSt.idDelayedInitTimer = SetTimer(gVBoxSt.hWTSAPIWnd, TIMERID_VBOXTRAY_ST_DELAYED_INIT_TIMER,
                                                        2000, (TIMERPROC)NULL);
                gVBoxSt.fIsConsole = TRUE;
                gVBoxSt.enmConnectState = WTSActive;
                rc = VINF_SUCCESS;
            }
            else
                rc = RTErrConvertFromWin32(dwErr);
        }

        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
    }
    else
    {
        VBoxTrayInfo("WtsApi32.dll APIs are not available (%Rrc)\n", rc);

        gVBoxSt.pfnWTSRegisterSessionNotification = NULL;
        gVBoxSt.pfnWTSUnRegisterSessionNotification = NULL;
        gVBoxSt.pfnWTSQuerySessionInformationA = NULL;
    }

    RT_ZERO(gVBoxSt);
    gVBoxSt.fIsConsole = TRUE;
    gVBoxSt.enmConnectState = WTSActive;

    if (RT_FAILURE(rc))
        VBoxTrayError("Initializing sessiosn tracking failed with %Rrc\n", rc);

    return rc;
}

void vboxStTerm(void)
{
    AssertReturnVoid(gVBoxSt.hWTSAPIWnd);

    if (gVBoxSt.idDelayedInitTimer)
    {
        /* notification is not registered, just kill timer */
        KillTimer(gVBoxSt.hWTSAPIWnd, gVBoxSt.idDelayedInitTimer);
        gVBoxSt.idDelayedInitTimer = 0;
    }
    else
    {
        if (!gVBoxSt.pfnWTSUnRegisterSessionNotification(gVBoxSt.hWTSAPIWnd))
        {
            LogWarnFunc(("WTSAPI32 unload failed, error = %08X\n", GetLastError()));
        }
    }

    RT_ZERO(gVBoxSt);
}

static const char* vboxStDbgGetString(DWORD val)
{
    switch (val)
    {
        RT_CASE_RET_STR(WTS_CONSOLE_CONNECT);
        RT_CASE_RET_STR(WTS_CONSOLE_DISCONNECT);
        RT_CASE_RET_STR(WTS_REMOTE_CONNECT);
        RT_CASE_RET_STR(WTS_REMOTE_DISCONNECT);
        RT_CASE_RET_STR(WTS_SESSION_LOGON);
        RT_CASE_RET_STR(WTS_SESSION_LOGOFF);
        RT_CASE_RET_STR(WTS_SESSION_LOCK);
        RT_CASE_RET_STR(WTS_SESSION_UNLOCK);
        RT_CASE_RET_STR(WTS_SESSION_REMOTE_CONTROL);
        default:
            LogWarnFunc(("invalid WTS state %d\n", val));
            return "Unknown";
    }
}

BOOL vboxStCheckTimer(WPARAM wEvent)
{
    if (wEvent != gVBoxSt.idDelayedInitTimer)
        return FALSE;

    if (gVBoxSt.pfnWTSRegisterSessionNotification(gVBoxSt.hWTSAPIWnd, NOTIFY_FOR_THIS_SESSION))
    {
        KillTimer(gVBoxSt.hWTSAPIWnd, gVBoxSt.idDelayedInitTimer);
        gVBoxSt.idDelayedInitTimer = 0;
        vboxStCheckState();
    }
    else
    {
        LogWarnFunc(("timer WTSRegisterSessionNotification failed, error = %08X\n", GetLastError()));
        Assert(gVBoxSt.fIsConsole == TRUE);
        Assert(gVBoxSt.enmConnectState == WTSActive);
    }

    return TRUE;
}

BOOL vboxStIsActiveConsole(void)
{
    return (gVBoxSt.enmConnectState == WTSActive && gVBoxSt.fIsConsole);
}

BOOL vboxStHandleEvent(WPARAM wEvent)
{
    RT_NOREF(wEvent);
    LogFunc(("WTS Event: %s\n", vboxStDbgGetString(wEvent)));
    BOOL fOldIsActiveConsole = vboxStIsActiveConsole();

    vboxStCheckState();

    return !vboxStIsActiveConsole() != !fOldIsActiveConsole;
}

