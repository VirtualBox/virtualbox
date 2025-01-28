/* $Id: VBoxTrayInternal.h 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxSeamless - Display notifications
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayInternal_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/* caps API */
#define VBOXCAPS_ENTRY_IDX_SEAMLESS  0
#define VBOXCAPS_ENTRY_IDX_GRAPHICS  1
#define VBOXCAPS_ENTRY_IDX_COUNT     2

typedef enum VBOXCAPS_ENTRY_FUNCSTATE
{
    /* the cap is unsupported */
    VBOXCAPS_ENTRY_FUNCSTATE_UNSUPPORTED = 0,
    /* the cap is supported */
    VBOXCAPS_ENTRY_FUNCSTATE_SUPPORTED,
    /* the cap functionality is started, it can be disabled however if its AcState is not ACQUIRED */
    VBOXCAPS_ENTRY_FUNCSTATE_STARTED,
} VBOXCAPS_ENTRY_FUNCSTATE;


int VBoxAcquireGuestCaps(uint32_t fOr, uint32_t fNot, bool fCfg);
void VBoxCapsEntryFuncStateSet(uint32_t iCup, VBOXCAPS_ENTRY_FUNCSTATE enmFuncState);
int VBoxCapsInit();
int VBoxCapsReleaseAll();
void VBoxCapsTerm();
BOOL VBoxCapsEntryIsAcquired(uint32_t iCap);
BOOL VBoxCapsEntryIsEnabled(uint32_t iCap);
BOOL VBoxCapsCheckTimer(WPARAM wParam);
int VBoxCapsEntryRelease(uint32_t iCap);
int VBoxCapsEntryAcquire(uint32_t iCap);
int VBoxCapsAcquireAllSupported();


/* console-related caps API */
BOOL VBoxConsoleIsAllowed();
void VBoxConsoleEnable(BOOL fEnable);
void VBoxConsoleCapSetSupported(uint32_t iCap, BOOL fSupported);


/*
 * Dt (desktop [state] tracking) functionality API
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * */
int vboxDtInit();
void vboxDtTerm();
/* @returns true on "IsInputDesktop" state change */
BOOL vboxDtHandleEvent();
/* @returns true iff the application (VBoxTray) desktop is input */
BOOL vboxDtIsInputDesktop();
HANDLE vboxDtGetNotifyEvent();
BOOL vboxDtCheckTimer(WPARAM wParam);
void vboxDtDoCheck();


/*
 * St (session [state] tracking) functionality API
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * it is supposed to be called & used from within the window message handler thread
 * of the window passed to vboxStInit */
int vboxStInit(HWND hWnd);
void vboxStTerm(void);
/* @returns true on "IsActiveConsole" state change */
BOOL vboxStHandleEvent(WPARAM EventID);
BOOL vboxStIsActiveConsole();
BOOL vboxStCheckTimer(WPARAM wEvent);


DWORD VBoxDisplayGetCount();
DWORD VBoxDisplayGetConfig(const DWORD NumDevices, DWORD *pDevPrimaryNum, DWORD *pNumDevices, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes);

DWORD EnableAndResizeDispDev(DEVMODE *paDeviceModes, DISPLAY_DEVICE *paDisplayDevices, DWORD totalDispNum, UINT Id, DWORD aWidth, DWORD aHeight,
                             DWORD aBitsPerPixel, LONG aPosX, LONG aPosY, BOOL fEnabled, BOOL fExtDispSup);

int                      VBoxTrayLogCreate(const char *pszLogFile);
void                     VBoxTrayLogDestroy(void);

void                     VBoxTrayInfo(const char *pszFormat, ...);
RTEXITCODE               VBoxTrayError(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
void                     VBoxTrayVerbose(unsigned iLevel, const char *pszFormat, ...)  RT_IPRT_FORMAT_ATTR(2, 3);
RTEXITCODE               VBoxTrayShowError(const char *pszFormat, ...);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTrayInternal_h */
