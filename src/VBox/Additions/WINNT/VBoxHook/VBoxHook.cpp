/* $Id: VBoxHook.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxHook -- Global windows hook dll
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>

#include <VBoxHook.h>
#include <VBox/VBoxGuestLib.h>
#ifdef DEBUG
# include <VBox/VBoxGuest.h>
# include <VBox/VMMDev.h>
# include <iprt/string.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#pragma data_seg("SHARED")
static HWINEVENTHOOK    g_ahWinEventHook[2]   = { NULL, NULL };
static HWINEVENTHOOK    g_hDesktopEventHook   = NULL;
#pragma data_seg()
#pragma comment(linker, "/section:SHARED,RWS")

static HANDLE   g_hWinNotifyEvent     = NULL;
static HANDLE   g_hDesktopNotifyEvent = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef DEBUG
static void WriteLog(const char *pszFormat, ...);
# define dprintf(a) do { WriteLog a; } while (0)
#else
# define dprintf(a) do {} while (0)
#endif /* !DEBUG */


static void CALLBACK VBoxHandleWinEvent(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                        LONG idObject, LONG idChild,
                                        DWORD dwEventThread, DWORD dwmsEventTime) RT_NOTHROW_DEF
{
    RT_NOREF(hWinEventHook, idChild, dwEventThread, dwmsEventTime);
    DWORD dwStyle;
    if (    idObject != OBJID_WINDOW
        ||  !hwnd)
        return;

    dwStyle  = GetWindowLong(hwnd, GWL_STYLE);
    if (dwStyle & WS_CHILD)
        return;

    switch(event)
    {
    case EVENT_OBJECT_LOCATIONCHANGE:
        if (!(dwStyle & WS_VISIBLE))
            return;

    case EVENT_OBJECT_CREATE:
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_SHOW:
#ifdef DEBUG
        switch(event)
        {
        case EVENT_OBJECT_LOCATIONCHANGE:
            dprintf(("VBoxHandleWinEvent EVENT_OBJECT_LOCATIONCHANGE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_CREATE:
            dprintf(("VBoxHandleWinEvent EVENT_OBJECT_CREATE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_HIDE:
            dprintf(("VBoxHandleWinEvent EVENT_OBJECT_HIDE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_SHOW:
            dprintf(("VBoxHandleWinEvent EVENT_OBJECT_SHOW for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_DESTROY:
            dprintf(("VBoxHandleWinEvent EVENT_OBJECT_DESTROY for window %x\n", hwnd));
            break;
        }
#endif
        if (!g_hWinNotifyEvent)
        {
            g_hWinNotifyEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, VBOXHOOK_GLOBAL_WT_EVENT_NAME);
            dprintf(("OpenEvent returned %x (last err=%x)\n", g_hWinNotifyEvent, GetLastError()));
        }
        BOOL fRc = SetEvent(g_hWinNotifyEvent);
        dprintf(("SetEvent %x returned %d (last error %x)\n", g_hWinNotifyEvent, fRc, GetLastError())); NOREF(fRc);
        break;
    }
}

static void CALLBACK VBoxHandleDesktopEvent(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                            LONG idObject, LONG idChild,
                                            DWORD dwEventThread, DWORD dwmsEventTime) RT_NOTHROW_DEF
{
    RT_NOREF(hWinEventHook, event, hwnd, idObject, idChild, dwEventThread, dwmsEventTime);
    if (!g_hDesktopNotifyEvent)
    {
        g_hDesktopNotifyEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, VBOXHOOK_GLOBAL_DT_EVENT_NAME);
        dprintf(("OpenEvent returned %x (last err=%x)\n", g_hDesktopNotifyEvent, GetLastError()));
    }
    BOOL fRc = SetEvent(g_hDesktopNotifyEvent);
    dprintf(("SetEvent %x returned %d (last error %x)\n", g_hDesktopNotifyEvent, fRc, GetLastError())); NOREF(fRc);
}

BOOL VBoxHookInstallActiveDesktopTracker(HMODULE hDll)
{
    if (g_hDesktopEventHook)
        return TRUE;

    CoInitialize(NULL);
    g_hDesktopEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                                          hDll,
                                          VBoxHandleDesktopEvent,
                                          0, 0,
                                          0);

    return !!g_hDesktopEventHook;

}

BOOL VBoxHookRemoveActiveDesktopTracker()
{
    if (g_hDesktopEventHook)
    {
        UnhookWinEvent(g_hDesktopEventHook);
        CoUninitialize();
    }
    g_hDesktopEventHook = 0;
    return TRUE;
}

/** Install the global message hook */
BOOL VBoxHookInstallWindowTracker(HMODULE hDll)
{
    if (g_ahWinEventHook[0] || g_ahWinEventHook[1])
        return TRUE;

    CoInitialize(NULL);
    g_ahWinEventHook[0] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                          hDll,
                                          VBoxHandleWinEvent,
                                          0, 0,
                                          WINEVENT_INCONTEXT | WINEVENT_SKIPOWNPROCESS);

    g_ahWinEventHook[1] = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
                                          hDll,
                                          VBoxHandleWinEvent,
                                          0, 0,
                                          WINEVENT_INCONTEXT | WINEVENT_SKIPOWNPROCESS);
    return !!g_ahWinEventHook[0];
}

/** Remove the global message hook */
BOOL VBoxHookRemoveWindowTracker()
{
    if (g_ahWinEventHook[0] && g_ahWinEventHook[1])
    {
        UnhookWinEvent(g_ahWinEventHook[0]);
        UnhookWinEvent(g_ahWinEventHook[1]);
        CoUninitialize();
    }
    g_ahWinEventHook[0] = g_ahWinEventHook[1] = 0;
    return TRUE;
}


#ifdef DEBUG
/**
 * dprintf worker using VBoxGuest.sys and VMMDevReq_LogString.
 */
static void WriteLog(const char *pszFormat, ...)
{
    /*
     * Open VBox guest driver once.
     */
    static HANDLE s_hVBoxGuest = INVALID_HANDLE_VALUE;
    HANDLE hVBoxGuest = s_hVBoxGuest;
    if (hVBoxGuest == INVALID_HANDLE_VALUE)
    {
        hVBoxGuest = CreateFile(VBOXGUEST_DEVICE_NAME,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                NULL);
        if (hVBoxGuest == INVALID_HANDLE_VALUE)
            return;
        s_hVBoxGuest = hVBoxGuest;
    }

    /*
     * We're apparently afraid of using stack here, so we use a static buffer
     * instead and pray we won't be here at the same time on two threads...
     */
    static union
    {
        VMMDevReqLogString Req;
        uint8_t abBuf[1024];
    } s_uBuf;

    vmmdevInitRequest(&s_uBuf.Req.header, VMMDevReq_LogString);

    va_list va;
    va_start(va, pszFormat);
    size_t cch = RTStrPrintf(s_uBuf.Req.szString, sizeof(s_uBuf.Req.szString), pszFormat, va);
    va_end(va);

    s_uBuf.Req.header.size += (uint32_t)cch;
    if (s_uBuf.Req.header.size > sizeof(s_uBuf))
        __debugbreak();

    DWORD cbReturned;
    DeviceIoControl(hVBoxGuest, VBGL_IOCTL_VMMDEV_REQUEST(s_uBuf.Req.size),
                    &s_uBuf.Req, s_uBuf.Req.header.size,
                    &s_uBuf.Req, s_uBuf.Req.header.size,
                    &cbReturned, NULL);
}
#endif /* DEBUG */

