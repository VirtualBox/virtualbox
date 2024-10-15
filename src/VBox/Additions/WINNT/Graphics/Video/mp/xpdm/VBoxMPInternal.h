/* $Id: VBoxMPInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Miniport internal header
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_xpdm_VBoxMPInternal_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_xpdm_VBoxMPInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "common/VBoxMPUtils.h"
#include "common/VBoxMPDevExt.h"
#include "common/xpdm/VBoxVideoIOCTL.h"

RT_C_DECLS_BEGIN
ULONG DriverEntry(IN PVOID Context1, IN PVOID Context2);
RT_C_DECLS_END

/* ==================== Misc ==================== */
void VBoxSetupVideoPortAPI(PVBOXMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo);
void VBoxCreateDisplays(PVBOXMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo);
int VBoxVbvaEnable(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult);
DECLCALLBACK(void) VBoxMPHGSMIHostCmdCompleteCB(HVBOXVIDEOHGSMI hHGSMI, struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pCmd);
DECLCALLBACK(int) VBoxMPHGSMIHostCmdRequestCB(HVBOXVIDEOHGSMI hHGSMI, uint8_t u8Channel, uint32_t iDisplay,
                                              struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST **ppCmd);
int VBoxVbvaChannelDisplayEnable(PVBOXMP_COMMON pCommon, int iDisplay, uint8_t u8Channel);

/* ==================== System VRP's handlers ==================== */
BOOLEAN VBoxMPSetCurrentMode(PVBOXMP_DEVEXT pExt, PVIDEO_MODE pMode, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPResetDevice(PVBOXMP_DEVEXT pExt, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPMapVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_MEMORY pRequestedAddress,
                             PVIDEO_MEMORY_INFORMATION pMapInfo, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPUnmapVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_MEMORY VideoMemory, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPShareVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pShareMem,
                               PVIDEO_SHARE_MEMORY_INFORMATION pShareMemInfo, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPUnshareVideoMemory(PVBOXMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pMem, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryCurrentMode(PVBOXMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModeInfo, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryNumAvailModes(PVBOXMP_DEVEXT pExt, PVIDEO_NUM_MODES pNumModes, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryAvailModes(PVBOXMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModes, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPSetColorRegisters(PVBOXMP_DEVEXT pExt, PVIDEO_CLUT pClut, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPSetPointerAttr(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_ATTRIBUTES pPointerAttrs, uint32_t cbLen, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPReportCursorPosition(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_POSITION pPos, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPEnablePointer(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryPointerPosition(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_POSITION pPos, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryPointerCapabilities(PVBOXMP_DEVEXT pExt, PVIDEO_POINTER_CAPABILITIES pCaps, PSTATUS_BLOCK pStatus);

/* ==================== VirtualBox VRP's handlers ==================== */
BOOLEAN VBoxMPVBVAEnable(PVBOXMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPSetVisibleRegion(uint32_t cRects, RTRECT *pRects, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPHGSMIQueryPortProcs(PVBOXMP_DEVEXT pExt, HGSMIQUERYCPORTPROCS *pProcs, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPHGSMIQueryCallbacks(PVBOXMP_DEVEXT pExt, HGSMIQUERYCALLBACKS *pCallbacks, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPQueryHgsmiInfo(PVBOXMP_DEVEXT pExt, QUERYHGSMIRESULT *pResult, PSTATUS_BLOCK pStatus);
BOOLEAN VBoxMPHgsmiHandlerEnable(PVBOXMP_DEVEXT pExt, HGSMIHANDLERENABLE *pChannel, PSTATUS_BLOCK pStatus);
#ifdef VBOX_WITH_VIDEOHWACCEL
BOOLEAN VBoxMPVhwaQueryInfo(PVBOXMP_DEVEXT pExt, VHWAQUERYINFO *pInfo, PSTATUS_BLOCK pStatus);
#endif
BOOLEAN VBoxMPQueryRegistryFlags(PVBOXMP_DEVEXT pExt, ULONG *pulFlags, PSTATUS_BLOCK pStatus);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_xpdm_VBoxMPInternal_h */
