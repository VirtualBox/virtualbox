/* $Id: VBoxDispMini.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, helper functions which interacts with our miniport driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispMini_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispMini_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDisp.h"

int VBoxDispMPGetVideoModes(HANDLE hDriver, PVIDEO_MODE_INFORMATION *ppModesTable, ULONG *cModes);
int VBoxDispMPGetPointerCaps(HANDLE hDriver, PVIDEO_POINTER_CAPABILITIES pCaps);
int VBoxDispMPSetCurrentMode(HANDLE hDriver, ULONG ulMode);
int VBoxDispMPMapMemory(PVBOXDISPDEV pDev, PVIDEO_MEMORY_INFORMATION pMemInfo);
int VBoxDispMPUnmapMemory(PVBOXDISPDEV pDev);
int VBoxDispMPQueryHGSMIInfo(HANDLE hDriver, QUERYHGSMIRESULT *pInfo);
int VBoxDispMPQueryHGSMICallbacks(HANDLE hDriver, HGSMIQUERYCALLBACKS *pCallbacks);
int VBoxDispMPHGSMIQueryPortProcs(HANDLE hDriver, HGSMIQUERYCPORTPROCS *pPortProcs);
#ifdef VBOX_WITH_VIDEOHWACCEL
int VBoxDispMPVHWAQueryInfo(HANDLE hDriver, VHWAQUERYINFO *pInfo);
#endif
int VBoxDispMPSetColorRegisters(HANDLE hDriver, PVIDEO_CLUT pClut, DWORD cbClut);
int VBoxDispMPDisablePointer(HANDLE hDriver);
int VBoxDispMPSetPointerPosition(HANDLE hDriver, PVIDEO_POINTER_POSITION pPos);
int VBoxDispMPSetPointerAttrs(PVBOXDISPDEV pDev);
int VBoxDispMPSetVisibleRegion(HANDLE hDriver, PRTRECT pRects, DWORD cRects);
int VBoxDispMPResetDevice(HANDLE hDriver);
int VBoxDispMPShareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem, PVIDEO_SHARE_MEMORY_INFORMATION pSMemInfo);
int VBoxDispMPUnshareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem);
int VBoxDispMPQueryRegistryFlags(HANDLE hDriver, ULONG *pulFlags);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispMini_h */
