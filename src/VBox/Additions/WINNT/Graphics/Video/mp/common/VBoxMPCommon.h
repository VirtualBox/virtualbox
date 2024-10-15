/* $Id: VBoxMPCommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport common functions used by XPDM/WDDM drivers
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPCommon_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxMPDevExt.h"

RT_C_DECLS_BEGIN

int VBoxMPCmnMapAdapterMemory(PVBOXMP_COMMON pCommon, void **ppv, uint32_t ulOffset, uint32_t ulSize);
void VBoxMPCmnUnmapAdapterMemory(PVBOXMP_COMMON pCommon, void **ppv);

typedef bool(*PFNVIDEOIRQSYNC)(void *);
bool VBoxMPCmnSyncToVideoIRQ(PVBOXMP_COMMON pCommon, PFNVIDEOIRQSYNC pfnSync, void *pvUser);

/* Video modes related */
#ifdef VBOX_XPDM_MINIPORT
void VBoxMPCmnInitCustomVideoModes(PVBOXMP_DEVEXT pExt);
VIDEO_MODE_INFORMATION* VBoxMPCmnGetCustomVideoModeInfo(ULONG ulIndex);
VIDEO_MODE_INFORMATION* VBoxMPCmnGetVideoModeInfo(PVBOXMP_DEVEXT pExt, ULONG ulIndex);
VIDEO_MODE_INFORMATION* VBoxMPXpdmCurrentVideoMode(PVBOXMP_DEVEXT pExt);
ULONG VBoxMPXpdmGetVideoModesCount(PVBOXMP_DEVEXT pExt);
void VBoxMPXpdmBuildVideoModesTable(PVBOXMP_DEVEXT pExt);
#endif

/* Registry access */
#ifdef VBOX_XPDM_MINIPORT
typedef PVBOXMP_DEVEXT VBOXMPCMNREGISTRY;
#else
typedef HANDLE VBOXMPCMNREGISTRY;
#endif

VP_STATUS VBoxMPCmnRegInit(IN PVBOXMP_DEVEXT pExt, OUT VBOXMPCMNREGISTRY *pReg);
VP_STATUS VBoxMPCmnRegFini(IN VBOXMPCMNREGISTRY Reg);
VP_STATUS VBoxMPCmnRegSetDword(IN VBOXMPCMNREGISTRY Reg, PCWSTR pName, uint32_t Val);
VP_STATUS VBoxMPCmnRegQueryDword(IN VBOXMPCMNREGISTRY Reg, PCWSTR pName, uint32_t *pVal);

/* Pointer related */
bool VBoxMPCmnUpdatePointerShape(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_ATTRIBUTES pAttrs, uint32_t cbLength);
int VBoxMPCmnReportCursorPositionEx(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_POSITION pPos, uint32_t *pxHost, uint32_t *pyHost);
int VBoxMPCmnReportCursorPosition(PVBOXMP_COMMON pCommon, PVIDEO_POINTER_POSITION pPos);

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_common_VBoxMPCommon_h */
