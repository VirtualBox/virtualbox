/* $Id: VBoxDesktopTracking.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDesktopTracking.cpp - Desktop tracking.
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
#include <iprt/system.h>
#include <iprt/win/windows.h>

#include <VBoxHook.h>

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"

/* Default desktop state tracking */
#include <Wtsapi32.h>


/** @todo r=andy Lacks documentation / Doxygen headers. What does this, and why? */

/*
 * Dt (desktop [state] tracking) functionality API impl
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * */

typedef struct VBOXDT
{
    HANDLE hNotifyEvent;
    BOOL fIsInputDesktop;
    UINT_PTR idTimer;
    RTLDRMOD hLdrModHook;
    BOOL (* pfnVBoxHookInstallActiveDesktopTracker)(HMODULE hDll);
    BOOL (* pfnVBoxHookRemoveActiveDesktopTracker)();
    HDESK (WINAPI * pfnGetThreadDesktop)(DWORD dwThreadId);
    HDESK (WINAPI * pfnOpenInputDesktop)(DWORD dwFlags, BOOL fInherit, ACCESS_MASK dwDesiredAccess);
    BOOL (WINAPI * pfnCloseDesktop)(HDESK hDesktop);
} VBOXDT;

static VBOXDT gVBoxDt;

BOOL vboxDtCalculateIsInputDesktop()
{
    BOOL fIsInputDt = FALSE;
    HDESK hInput = gVBoxDt.pfnOpenInputDesktop(0, FALSE, DESKTOP_CREATEWINDOW);
    if (hInput)
    {
//        DWORD dwThreadId = GetCurrentThreadId();
//        HDESK hThreadDt = gVBoxDt.pfnGetThreadDesktop(dwThreadId);
//        if (hThreadDt)
//        {
            fIsInputDt = TRUE;
//        }
//        else
//        {
//            DWORD dwErr = GetLastError();
//            LogFlowFunc(("pfnGetThreadDesktop for Seamless failed, last error = %08X\n", dwErr));
//        }

        gVBoxDt.pfnCloseDesktop(hInput);
    }
    else
    {
//        DWORD dwErr = GetLastError();
//        LogFlowFunc(("pfnOpenInputDesktop for Seamless failed, last error = %08X\n", dwErr));
    }
    return fIsInputDt;
}

void vboxDtDoCheck()
{
    BOOL fOldAllowedState = VBoxConsoleIsAllowed();
    if (vboxDtHandleEvent())
    {
        if (!VBoxConsoleIsAllowed() != !fOldAllowedState)
            VBoxConsoleEnable(!fOldAllowedState);
    }
}

BOOL vboxDtCheckTimer(WPARAM wParam)
{
    if (wParam != gVBoxDt.idTimer)
        return FALSE;

    vboxDtDoCheck();

    return TRUE;
}

int vboxDtInit()
{
    RT_ZERO(gVBoxDt);

    int rc;
    gVBoxDt.hNotifyEvent = CreateEvent(NULL, FALSE, FALSE, VBOXHOOK_GLOBAL_DT_EVENT_NAME);
    if (gVBoxDt.hNotifyEvent != NULL)
    {
        /* Load the hook dll and resolve the necessary entry points. */
        rc = RTLdrLoadAppPriv(VBOXHOOK_DLL_NAME, &gVBoxDt.hLdrModHook);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(gVBoxDt.hLdrModHook, "VBoxHookInstallActiveDesktopTracker",
                                (void **)&gVBoxDt.pfnVBoxHookInstallActiveDesktopTracker);
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrGetSymbol(gVBoxDt.hLdrModHook, "VBoxHookRemoveActiveDesktopTracker",
                                    (void **)&gVBoxDt.pfnVBoxHookRemoveActiveDesktopTracker);
                if (RT_FAILURE(rc))
                    LogFlowFunc(("VBoxHookRemoveActiveDesktopTracker not found\n"));
            }
            else
                LogFlowFunc(("VBoxHookInstallActiveDesktopTracker not found\n"));
            if (RT_SUCCESS(rc))
            {
                /* Try get the system APIs we need. */
                *(void **)&gVBoxDt.pfnGetThreadDesktop = RTLdrGetSystemSymbol("user32.dll", "GetThreadDesktop");
                if (!gVBoxDt.pfnGetThreadDesktop)
                {
                    LogFlowFunc(("GetThreadDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                *(void **)&gVBoxDt.pfnOpenInputDesktop = RTLdrGetSystemSymbol("user32.dll", "OpenInputDesktop");
                if (!gVBoxDt.pfnOpenInputDesktop)
                {
                    LogFlowFunc(("OpenInputDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                *(void **)&gVBoxDt.pfnCloseDesktop = RTLdrGetSystemSymbol("user32.dll", "CloseDesktop");
                if (!gVBoxDt.pfnCloseDesktop)
                {
                    LogFlowFunc(("CloseDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                if (RT_SUCCESS(rc))
                {
                    BOOL fRc = FALSE;
                    /* For Vista and up we need to change the integrity of the security descriptor, too. */
                    uint64_t const uNtVersion = RTSystemGetNtVersion();
                    if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
                    {
                        HMODULE hModHook = (HMODULE)RTLdrGetNativeHandle(gVBoxDt.hLdrModHook);
                        Assert((uintptr_t)hModHook != ~(uintptr_t)0);
                        fRc = gVBoxDt.pfnVBoxHookInstallActiveDesktopTracker(hModHook);
                        if (!fRc)
                            LogFlowFunc(("pfnVBoxHookInstallActiveDesktopTracker failed, last error = %08X\n", GetLastError()));
                    }

                    if (!fRc)
                    {
                        gVBoxDt.idTimer = SetTimer(g_hwndToolWindow, TIMERID_VBOXTRAY_DT_TIMER, 500, (TIMERPROC)NULL);
                        if (!gVBoxDt.idTimer)
                        {
                            DWORD dwErr = GetLastError();
                            LogFlowFunc(("SetTimer error %08X\n", dwErr));
                            rc = RTErrConvertFromWin32(dwErr);
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        gVBoxDt.fIsInputDesktop = vboxDtCalculateIsInputDesktop();
                        return VINF_SUCCESS;
                    }
                }
            }

            RTLdrClose(gVBoxDt.hLdrModHook);
        }
        else
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }

        CloseHandle(gVBoxDt.hNotifyEvent);
    }
    else
    {
        DWORD dwErr = GetLastError();
        LogFlowFunc(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }


    RT_ZERO(gVBoxDt);
    gVBoxDt.fIsInputDesktop = TRUE;

    if (RT_FAILURE(rc))
        VBoxTrayError("Initializing desktop tracking failed with %Rrc\n", rc);

    return rc;
}

void vboxDtTerm()
{
    if (!gVBoxDt.hLdrModHook)
        return;

    gVBoxDt.pfnVBoxHookRemoveActiveDesktopTracker();

    RTLdrClose(gVBoxDt.hLdrModHook);
    CloseHandle(gVBoxDt.hNotifyEvent);

    RT_ZERO(gVBoxDt);
}

/* @returns true on "IsInputDesktop" state change */
BOOL vboxDtHandleEvent()
{
    BOOL fIsInputDesktop = gVBoxDt.fIsInputDesktop;
    gVBoxDt.fIsInputDesktop = vboxDtCalculateIsInputDesktop();
    return !fIsInputDesktop != !gVBoxDt.fIsInputDesktop;
}

HANDLE vboxDtGetNotifyEvent()
{
    return gVBoxDt.hNotifyEvent;
}

/* @returns true iff the application (VBoxTray) desktop is input */
BOOL vboxDtIsInputDesktop()
{
    return gVBoxDt.fIsInputDesktop;
}

