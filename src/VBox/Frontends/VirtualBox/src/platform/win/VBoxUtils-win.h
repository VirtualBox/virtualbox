/* $Id: VBoxUtils-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions for handling Windows specific tasks.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_platform_win_VBoxUtils_win_h
#define FEQT_INCLUDED_SRC_platform_win_VBoxUtils_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QRegion>

/* GUI includes: */
#include "UILibraryDefs.h"

/* External includes: */
#include <iprt/win/windows.h>

/* Namespace for native window sub-system functions: */
namespace NativeWindowSubsystem
{
    /* Returns area covered by visible always-on-top (top-most) windows: */
    SHARED_LIBRARY_STUFF const QRegion areaCoveredByTopMostWindows();
    SHARED_LIBRARY_STUFF const void setScreenSaverActive(BOOL fDisableScreenSaver);

    /** Wraps WinAPI ShutdownBlockReasonCreate function. */
    SHARED_LIBRARY_STUFF BOOL ShutdownBlockReasonCreateAPI(HWND hWnd, LPCWSTR pwszReason);

    /** Activates window with certain @a wId, @a fSwitchDesktop if requested. */
    bool WinActivateWindow(WId wId, bool fSwitchDesktop);
}

#endif /* !FEQT_INCLUDED_SRC_platform_win_VBoxUtils_win_h */
