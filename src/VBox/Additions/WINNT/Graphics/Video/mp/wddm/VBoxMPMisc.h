/* $Id: VBoxMPMisc.h 109967 2025-06-25 16:24:55Z dmitrii.grigorev@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPMisc_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPMisc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "../../common/VBoxVideoTools.h"

DECLINLINE(void) vboxVideoLeDetach(LIST_ENTRY *pList, LIST_ENTRY *pDstList)
{
    if (IsListEmpty(pList))
    {
        InitializeListHead(pDstList);
    }
    else
    {
        *pDstList = *pList;
        Assert(pDstList->Flink->Blink == pList);
        Assert(pDstList->Blink->Flink == pList);
        /* pDstList->Flink & pDstList->Blink point to the "real| entries, never to pList
         * since we've checked IsListEmpty(pList) above */
        pDstList->Flink->Blink = pDstList;
        pDstList->Blink->Flink = pDstList;
        InitializeListHead(pList);
    }
}

typedef uint32_t VBOXWDDM_HANDLE;
#define VBOXWDDM_HANDLE_INVALID 0UL

typedef struct VBOXWDDM_HTABLE
{
    uint32_t cData;
    uint32_t iNext2Search;
    uint32_t cSize;
    PVOID *paData;
} VBOXWDDM_HTABLE, *PVBOXWDDM_HTABLE;

typedef struct VBOXWDDM_HTABLE_ITERATOR
{
    PVBOXWDDM_HTABLE pTbl;
    uint32_t iCur;
    uint32_t cLeft;
} VBOXWDDM_HTABLE_ITERATOR, *PVBOXWDDM_HTABLE_ITERATOR;

VOID vboxWddmHTableIterInit(PVBOXWDDM_HTABLE pTbl, PVBOXWDDM_HTABLE_ITERATOR pIter);
PVOID vboxWddmHTableIterNext(PVBOXWDDM_HTABLE_ITERATOR pIter, VBOXWDDM_HANDLE *phHandle);
BOOL vboxWddmHTableIterHasNext(PVBOXWDDM_HTABLE_ITERATOR pIter);
PVOID vboxWddmHTableIterRemoveCur(PVBOXWDDM_HTABLE_ITERATOR pIter);
NTSTATUS vboxWddmHTableCreate(PVBOXWDDM_HTABLE pTbl, uint32_t cSize);
VOID vboxWddmHTableDestroy(PVBOXWDDM_HTABLE pTbl);
NTSTATUS vboxWddmHTableRealloc(PVBOXWDDM_HTABLE pTbl, uint32_t cNewSize);
VBOXWDDM_HANDLE vboxWddmHTablePut(PVBOXWDDM_HTABLE pTbl, PVOID pvData);
PVOID vboxWddmHTableRemove(PVBOXWDDM_HTABLE pTbl, VBOXWDDM_HANDLE hHandle);
PVOID vboxWddmHTableGet(PVBOXWDDM_HTABLE pTbl, VBOXWDDM_HANDLE hHandle);

NTSTATUS vboxWddmRegQueryDisplaySettingsKeyName(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);
NTSTATUS vboxWddmRegOpenDisplaySettingsKey(IN PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, OUT PHANDLE phKey);
NTSTATUS vboxWddmRegDisplaySettingsQueryRelX(HANDLE hKey, int * pResult);
NTSTATUS vboxWddmRegDisplaySettingsQueryRelY(HANDLE hKey, int * pResult);
NTSTATUS vboxWddmDisplaySettingsQueryPos(IN PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, POINT * pPos);
void vboxWddmDisplaySettingsCheckPos(IN PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
NTSTATUS vboxWddmRegQueryVideoGuidString(PVBOXMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS vboxWddmRegQueryDrvKeyName(PVBOXMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS vboxWddmRegOpenKeyEx(OUT PHANDLE phKey, IN HANDLE hRootKey, IN PCWCHAR pName, IN ACCESS_MASK fAccess);
NTSTATUS vboxWddmRegOpenKey(OUT PHANDLE phKey, IN PCWCHAR pName, IN ACCESS_MASK fAccess);
NTSTATUS vboxWddmRegQueryValueDword(IN HANDLE hKey, IN PCWCHAR pName, OUT PDWORD pDword);
NTSTATUS vboxWddmRegSetValueDword(IN HANDLE hKey, IN PCWCHAR pName, IN DWORD val);

NTSTATUS vboxWddmRegDrvFlagsSet(PVBOXMP_DEVEXT pDevExt, DWORD fVal);
DWORD vboxWddmRegDrvFlagsGet(PVBOXMP_DEVEXT pDevExt, DWORD fDefault);

UNICODE_STRING* vboxWddmVGuidGet(PVBOXMP_DEVEXT pDevExt);
VOID vboxWddmVGuidFree(PVBOXMP_DEVEXT pDevExt);

#define VBOXWDDM_MM_VOID 0xffffffffUL

typedef struct VBOXWDDM_MM
{
    RTL_BITMAP BitMap;
    UINT cPages;
    UINT cAllocs;
    PULONG pBuffer;
} VBOXWDDM_MM, *PVBOXWDDM_MM;

NTSTATUS vboxMmInit(PVBOXWDDM_MM pMm, UINT cPages);
ULONG vboxMmAlloc(PVBOXWDDM_MM pMm, UINT cPages);
VOID vboxMmFree(PVBOXWDDM_MM pMm, UINT iPage, UINT cPages);
NTSTATUS vboxMmTerm(PVBOXWDDM_MM pMm);

typedef struct VBOXVIDEOCM_ALLOC_MGR
{
    /* synch lock */
    FAST_MUTEX Mutex;
    VBOXWDDM_HTABLE AllocTable;
    VBOXWDDM_MM Mm;
//    PHYSICAL_ADDRESS PhData;
    uint8_t *pvData;
    uint32_t offData;
    uint32_t cbData;
} VBOXVIDEOCM_ALLOC_MGR, *PVBOXVIDEOCM_ALLOC_MGR;

typedef struct VBOXVIDEOCM_ALLOC_CONTEXT
{
    PVBOXVIDEOCM_ALLOC_MGR pMgr;
    /* synch lock */
    FAST_MUTEX Mutex;
    VBOXWDDM_HTABLE AllocTable;
} VBOXVIDEOCM_ALLOC_CONTEXT, *PVBOXVIDEOCM_ALLOC_CONTEXT;

NTSTATUS vboxVideoAMgrCreate(PVBOXMP_DEVEXT pDevExt, PVBOXVIDEOCM_ALLOC_MGR pMgr, uint32_t offData, uint32_t cbData);
NTSTATUS vboxVideoAMgrDestroy(PVBOXMP_DEVEXT pDevExt, PVBOXVIDEOCM_ALLOC_MGR pMgr);

NTSTATUS vboxVideoAMgrCtxCreate(PVBOXVIDEOCM_ALLOC_MGR pMgr, PVBOXVIDEOCM_ALLOC_CONTEXT pCtx);
NTSTATUS vboxVideoAMgrCtxDestroy(PVBOXVIDEOCM_ALLOC_CONTEXT pCtx);

NTSTATUS vboxVideoAMgrCtxAllocCreate(PVBOXVIDEOCM_ALLOC_CONTEXT pContext, PVBOXVIDEOCM_UM_ALLOC pUmAlloc);
NTSTATUS vboxVideoAMgrCtxAllocDestroy(PVBOXVIDEOCM_ALLOC_CONTEXT pContext, VBOXDISP_KMHANDLE hSesionHandle);

VOID vboxWddmSleep(uint32_t u32Val);
VOID vboxWddmCounterU32Wait(uint32_t volatile * pu32, uint32_t u32Val);

NTSTATUS vboxUmdDumpBuf(PVBOXDISPIFESCAPE_DBGDUMPBUF pBuf, uint32_t cbBuffer);

#if 0
/* wine shrc handle -> allocation map */
VOID vboxShRcTreeInit(PVBOXMP_DEVEXT pDevExt);
VOID vboxShRcTreeTerm(PVBOXMP_DEVEXT pDevExt);
BOOLEAN vboxShRcTreePut(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAlloc);
PVBOXWDDM_ALLOCATION vboxShRcTreeGet(PVBOXMP_DEVEXT pDevExt, HANDLE hSharedRc);
BOOLEAN vboxShRcTreeRemove(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAlloc);
#endif

NTSTATUS vboxWddmDrvCfgInit(PUNICODE_STRING pRegStr);
NTSTATUS vboxWddmLoggerCreate(PUNICODE_STRING pRegStr);

NTSTATUS VBoxWddmSlEnableVSyncNotification(PVBOXMP_DEVEXT pDevExt, BOOLEAN fEnable);
NTSTATUS VBoxWddmSlGetScanLine(PVBOXMP_DEVEXT pDevExt, DXGKARG_GETSCANLINE *pSl);
NTSTATUS VBoxWddmSlInit(PVBOXMP_DEVEXT pDevExt);
NTSTATUS VBoxWddmSlTerm(PVBOXMP_DEVEXT pDevExt);

void vboxWddmDiInitDefault(DXGK_DISPLAY_INFORMATION *pInfo, PHYSICAL_ADDRESS PhAddr, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
void vboxWddmDiToAllocData(PVBOXMP_DEVEXT pDevExt, const DXGK_DISPLAY_INFORMATION *pInfo, struct VBOXWDDM_ALLOC_DATA *pAllocData);
void vboxWddmDmSetupDefaultVramLocation(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID ModifiedVidPnSourceId, struct VBOXWDDM_SOURCE *paSources);

char const *vboxWddmAllocTypeString(PVBOXWDDM_ALLOCATION pAlloc);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPMisc_h */
