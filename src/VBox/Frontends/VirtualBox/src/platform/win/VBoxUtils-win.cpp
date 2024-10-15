/* $Id: VBoxUtils-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions for handling Windows specific tasks.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Qt includes: */
#include <QList>
#include <QRect>

/* GUI includes: */
#include "VBoxUtils-win.h"


/** Namespace for native window sub-system functions. */
namespace NativeWindowSubsystem
{
    /** Enumerates visible always-on-top (top-most) windows. */
    BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) RT_NOTHROW_PROTO;
    /** Contains visible top-most-window rectangles. */
    QList<QRect> topMostRects;
}

BOOL CALLBACK NativeWindowSubsystem::EnumWindowsProc(HWND hWnd, LPARAM) RT_NOTHROW_DEF
{
    /* Ignore NULL HWNDs: */
    if (!hWnd)
        return TRUE;

    /* Ignore hidden windows: */
    if (!IsWindowVisible(hWnd))
        return TRUE;

    /* Get window style: */
    LONG uStyle = GetWindowLong(hWnd, GWL_STYLE);
    /* Ignore minimized windows: */
    if (uStyle & WS_MINIMIZE)
        return TRUE;

    /* Get extended window style: */
    LONG uExtendedStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
    /* Ignore non-top-most windows: */
    if (!(uExtendedStyle & WS_EX_TOPMOST))
        return TRUE;

    /* Get that window rectangle: */
    RECT rect;
    GetWindowRect(hWnd, &rect);
    topMostRects << QRect(QPoint(rect.left, rect.top), QPoint(rect.right - 1, rect.bottom - 1));

    /* Proceed to the next window: */
    return TRUE;
}

const QRegion NativeWindowSubsystem::areaCoveredByTopMostWindows()
{
    /* Prepare the top-most region: */
    QRegion topMostRegion;
    /* Initialize the list of the top-most rectangles: */
    topMostRects.clear();
    /* Populate the list of top-most rectangles: */
    EnumWindows((WNDENUMPROC)EnumWindowsProc, 0);
    /* Update the top-most region with top-most rectangles: */
    for (int iRectIndex = 0; iRectIndex < topMostRects.size(); ++iRectIndex)
        topMostRegion += topMostRects[iRectIndex];
    /* Return top-most region: */
    return topMostRegion;
}

const void NativeWindowSubsystem::setScreenSaverActive(BOOL fDisableScreenSaver)
{
    BOOL fIsActive;
    SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &fIsActive, 0);
    if (fIsActive == !fDisableScreenSaver)
        return;
    //printf("before %d\n", fIsActive);

    SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, !fDisableScreenSaver, NULL, 0);

    SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &fIsActive, 0);
    /*if (fIsActive == !fDisableScreenSaver)
        printf("success %d %d\n", fIsActive, fDisableScreenSaver);
*/
}

BOOL NativeWindowSubsystem::ShutdownBlockReasonCreateAPI(HWND hWnd, LPCWSTR pwszReason)
{
    BOOL fResult = FALSE;
    typedef BOOL(WINAPI *PFNSHUTDOWNBLOCKREASONCREATE)(HWND hWnd, LPCWSTR pwszReason);

    PFNSHUTDOWNBLOCKREASONCREATE pfn = (PFNSHUTDOWNBLOCKREASONCREATE)GetProcAddress(
        GetModuleHandle(L"User32.dll"), "ShutdownBlockReasonCreate");
    _ASSERTE(pfn);
    if (pfn)
        fResult = pfn(hWnd, pwszReason);
    return fResult;
}

bool NativeWindowSubsystem::WinActivateWindow(WId wId, bool)
{
    bool fResult = true;
    HWND handle = (HWND)wId;

    if (IsIconic(handle))
        fResult &= !!ShowWindow(handle, SW_RESTORE);
    else if (!IsWindowVisible(handle))
        fResult &= !!ShowWindow(handle, SW_SHOW);

    fResult &= !!SetForegroundWindow(handle);
    return fResult;
}
