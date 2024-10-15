/* $Id: VBoxHlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Implementation of OS/2-specific helpers that require to reside in a DLL
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#define OS2EMX_PLAIN_CHAR

#define INCL_BASE
#define INCL_PM
#define INCL_DOSINFOSEG
#define INCL_DOSDEVIOCTL
#include <os2.h>

#include "VBoxHlp.h"

/**
 *  Undocumented PM hook that is called before the pressed key is checked
 *  against the global accelerator table.
 *
 *  Taken from the xWorkplace source code where it appears to come from the
 *  ProgramCommander/2 source code. Thanks to Ulrich Moeller and Roman Stangl.
 */
#define HK_PREACCEL 17

/* NOTE: all global data is per-process (DATA32 is multiple, nonshared). */

/* Module handle of this DLL */
static HMODULE gThisModule = NULLHANDLE;

static PGINFOSEG gGIS = NULL;
static PLINFOSEG gLIS = NULL;

/* Parameters for the keyboard hook (VBoxHlpInstallKbdHook()) */
HAB gKbdHookHab = NULLHANDLE;
HWND gKbdHookHwnd = NULLHANDLE;
ULONG gKbdHookMsg = 0;

/**
 *  Message Input hook used to monitor the system message queue.
 *
 *  @param aHab     Anchor handle.
 *  @param aHwnd    Pointer to the QMSG structure.
 *  @param aFS      Flags from WinPeekMsg(), either PM_NOREMOVE or
 *                  PM_REMOVE.
 *
 *  @return @c TRUE to steal the given message or @c FALSE to pass it to the
 *  rest of the hook chain.
 */
static
BOOL EXPENTRY vboxInputHook (HAB aHab, PQMSG aMsg, ULONG aFS)
{
    if (aMsg->msg == WM_CHAR)
    {
        /* For foreign processes that didn't call VBoxHlpInstallKbdHook(),
         * gKbdHookHwnd remains NULL. If it's the case while in this input
         * hook, it means that the given foreign process is in foreground
         * now. Since forwarding should work only for processes that
         * called VBoxHlpInstallKbdHook(), we ignore the message. */
        if (gKbdHookHwnd != NULLHANDLE)
        {
            MRESULT reply =
                WinSendMsg (gKbdHookHwnd, gKbdHookMsg, aMsg->mp1, aMsg->mp2);
            return (BOOL) reply;
        }
    }

    return FALSE;
}

/**
 *  Installs a hook that will intercept all keyboard input (WM_CHAR) messages
 *  and forward them to the given window handle using the given message
 *  identifier. Messages are intercepted only when the given top-level window
 *  is the active desktop window (i.e. a window receiving keyboard input).
 *
 *  When the WM_CHAR message is intercepted, it is forwarded as is (including
 *  all parameters) except that the message ID is changed to the given message
 *  ID. The result of the WinSendMsg() call is then converted to BOOL and if
 *  it results to TRUE then the message is considered to be processed,
 *  otherwise it is passed back to the system for normal processing.
 *
 *  If the hook is already installed for the same or another window, this
 *  method will return @c false.
 *
 *  @note This function is not thread-safe and must be called only on the main
 *  thread once per process.
 *
 *  @param aHab     Window anchor block.
 *  @param aHwnd    Top-level window handle to forward WM_CHAR messages to.
 *  @param aMsg     Message ID to use when forwarding.
 *
 *  @return @c true on success and @c false otherwise.  */
VBOXHLPDECL(bool) VBoxHlpInstallKbdHook (HAB aHab, HWND aHwnd,
                                         unsigned long aMsg)
{
    if (gKbdHookHwnd != NULLHANDLE ||
        aHwnd == NULLHANDLE)
        return false;

    BOOL ok = WinSetHook (aHab, NULLHANDLE, HK_PREACCEL,
                          (PFN) vboxInputHook, gThisModule);

    if (ok)
    {
        gKbdHookHab = aHab;
        gKbdHookHwnd = aHwnd;
        gKbdHookMsg = aMsg;
    }

    return (bool) ok;
}

/**
 *  Uninstalls the keyboard hook installed by VBoxHlpInstallKbdHook().
 *  All arguments must match arguments passed to VBoxHlpInstallKbdHook(),
 *  otherwise this method will do nothing and return @c false.
 *
 *  @return @c true on success and @c false otherwise.
 */
VBOXHLPDECL(bool) VBoxHlpUninstallKbdHook (HAB aHab, HWND aHwnd,
                                           unsigned long aMsg)
{
    if (gKbdHookHab != aHab ||
        gKbdHookHwnd != aHwnd ||
        gKbdHookMsg != aMsg)
        return false;

    BOOL ok = WinReleaseHook (aHab, NULLHANDLE, HK_PREACCEL,
                              (PFN) vboxInputHook, gThisModule);

    if (ok)
    {
        gKbdHookHab = NULLHANDLE;
        gKbdHookHwnd = NULLHANDLE;
        gKbdHookMsg = 0;
    }

    return (bool) ok;
}

/**
 *  DLL entry point.
 *
 *  @param aHandle  DLL module handle.
 *  @param aFlag    0 on initialization or 1 on termination.
 *
 *  @return Non-zero for success or 0 for failure.
 */
ULONG _System _DLL_InitTerm (HMODULE aHandle, ULONG aFlag)
{
    bool ok = true;

    if (aFlag == 0)
    {
        /* DLL initialization */

        gThisModule = aHandle;

        gGIS = GETGINFOSEG();
        gLIS = GETLINFOSEG();
    }
    else
    {
        /* DLL termination */

        /* Make sure we release the hook if the user forgets to do so. */
        if (gKbdHookHwnd != NULLHANDLE)
            WinReleaseHook (gKbdHookHab, NULLHANDLE, HK_PREACCEL,
                            (PFN) vboxInputHook, gThisModule);

        gThisModule = NULLHANDLE;
    }

    return (unsigned long) ok;
}

