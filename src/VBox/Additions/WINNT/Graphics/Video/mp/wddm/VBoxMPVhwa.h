/* $Id: VBoxMPVhwa.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVhwa_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVhwa_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

#include "VBoxMPShgsmi.h"

VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST * vboxVhwaCommandCreate(PVBOXMP_DEVEXT pDevExt,
                                                               D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId,
                                                               VBOXVHWACMD_TYPE enmCmd,
                                                               VBOXVHWACMD_LENGTH cbCmd);

void vboxVhwaCommandFree(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd);
int  vboxVhwaCommandSubmit(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST * pCmd);
void vboxVhwaCommandSubmitAsynchAndComplete(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd);

typedef DECLCALLBACKTYPE(void, FNVBOXVHWACMDCOMPLETION,(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                                        void *pvContext));
typedef FNVBOXVHWACMDCOMPLETION *PFNVBOXVHWACMDCOMPLETION;

void vboxVhwaCommandSubmitAsynch(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                 PFNVBOXVHWACMDCOMPLETION pfnCompletion, void * pContext);
void vboxVhwaCommandSubmitAsynchByEvent(PVBOXMP_DEVEXT pDevExt, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, RTSEMEVENT hEvent);

#define VBOXVHWA_CMD2LISTENTRY(_pCmd)   ((PVBOXVTLIST_ENTRY)&(_pCmd)->u.pNext)
#define VBOXVHWA_LISTENTRY2CMD(_pEntry) ( (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *)((uint8_t *)(_pEntry) - RT_UOFFSETOF(VBOXVHWACMD, u.pNext)) )

DECLINLINE(void) vboxVhwaPutList(VBOXVTLIST *pList, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    vboxVtListPut(pList, VBOXVHWA_CMD2LISTENTRY(pCmd), VBOXVHWA_CMD2LISTENTRY(pCmd));
}

void vboxVhwaCompletionListProcess(PVBOXMP_DEVEXT pDevExt, VBOXVTLIST *pList);

int vboxVhwaEnable(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
int vboxVhwaDisable(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
void vboxVhwaInit(PVBOXMP_DEVEXT pDevExt);
void vboxVhwaFree(PVBOXMP_DEVEXT pDevExt);

int vboxVhwaHlpOverlayFlip(PVBOXWDDM_OVERLAY pOverlay, const DXGKARG_FLIPOVERLAY *pFlipInfo);
int vboxVhwaHlpOverlayUpdate(PVBOXWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo);
int vboxVhwaHlpOverlayDestroy(PVBOXWDDM_OVERLAY pOverlay);
int vboxVhwaHlpOverlayCreate(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, DXGK_OVERLAYINFO *pOverlayInfo, /* OUT */ PVBOXWDDM_OVERLAY pOverlay);

int vboxVhwaHlpGetSurfInfo(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pSurf);

BOOLEAN vboxVhwaHlpOverlayListIsEmpty(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
void vboxVhwaHlpOverlayDstRectUnion(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RECT *pRect);
void vboxVhwaHlpOverlayDstRectGet(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_OVERLAY pOverlay, RECT *pRect);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVhwa_h */
