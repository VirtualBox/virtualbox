/* $Id: VMMDevHGCM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDev - HGCM - Host-Guest Communication Manager, internal header.
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

#ifndef VBOX_INCLUDED_SRC_VMMDev_VMMDevHGCM_h
#define VBOX_INCLUDED_SRC_VMMDev_VMMDevHGCM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VMMDevState.h"

RT_C_DECLS_BEGIN
int  vmmdevR3HgcmConnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                         const VMMDevHGCMConnect *pHGCMConnect, RTGCPHYS GCPhys);
int  vmmdevR3HgcmDisconnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                            const VMMDevHGCMDisconnect *pHGCMDisconnect, RTGCPHYS GCPhys);
int  vmmdevR3HgcmCall(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, const VMMDevHGCMCall *pHGCMCall, uint32_t cbHGCMCall,
                      RTGCPHYS GCPhys, VMMDevRequestType enmRequestType, uint64_t tsArrival, PVMMDEVREQLOCK *ppLock);

int  vmmdevR3HgcmCancel(PVMMDEVCC pThisCC, const VMMDevHGCMCancel *pHGCMCancel, RTGCPHYS GCPhys);
int  vmmdevR3HgcmCancel2(PVMMDEVCC pThisCC, RTGCPHYS GCPhys);

DECLCALLBACK(int)  hgcmR3Completed(PPDMIHGCMPORT pInterface, int32_t result, PVBOXHGCMCMD pCmdPtr);
DECLCALLBACK(bool) hgcmR3IsCmdRestored(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd);
DECLCALLBACK(bool) hgcmR3IsCmdCancelled(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd);
DECLCALLBACK(uint32_t) hgcmR3GetRequestor(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd);
DECLCALLBACK(uint64_t) hgcmR3GetVMMDevSessionId(PPDMIHGCMPORT pInterface);

int  vmmdevR3HgcmSaveState(PVMMDEVCC pThisCC, PSSMHANDLE pSSM);
int  vmmdevR3HgcmLoadState(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, PSSMHANDLE pSSM, uint32_t uVersion);
int  vmmdevR3HgcmLoadStateDone(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC);

void vmmdevR3HgcmDestroy(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC);
int  vmmdevR3HgcmInit(PVMMDEVCC pThisCC);
RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VMMDev_VMMDevHGCM_h */

