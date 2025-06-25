/* $Id: VBoxMPWddm.cpp 109969 2025-06-25 16:36:39Z dmitrii.grigorev@oracle.com $ */
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

#include "VBoxMPWddm.h"
#include "common/VBoxMPCommon.h"
#include "common/VBoxMPHGSMI.h"
#ifdef VBOX_WITH_VIDEOHWACCEL
# include "VBoxMPVhwa.h"
#endif
#include "VBoxMPVidPn.h"
#include "VBoxMPLegacy.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/initterm.h>
#include <iprt/utf16.h>
#include <iprt/x86.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/VMMDev.h> /* for VMMDevVideoSetVisibleRegion */
#include <VBoxVideo.h>
#include <wingdi.h> /* needed for RGNDATA definition */
#include <VBoxDisplay.h> /* this is from Additions/WINNT/include/ to include escape codes */
#include <VBoxVideoVBE.h>
#include <VBox/Version.h>

#ifdef VBOX_WITH_VMSVGA
#include "gallium/VBoxMPGaWddm.h"
#endif

#ifdef DEBUG
DWORD g_VBoxLogUm = VBOXWDDM_CFG_LOG_UM_BACKDOOR;
#else
DWORD g_VBoxLogUm = 0;
#endif
DWORD g_RefreshRate = 0;

/* Whether the driver is display-only (no 3D) for Windows 8 or newer guests. */
DWORD g_VBoxDisplayOnly = 0;

#define VBOXWDDM_MEMTAG 'MDBV'
PVOID vboxWddmMemAlloc(IN SIZE_T cbSize)
{
    POOL_TYPE enmPoolType = (VBoxQueryWinVersion(NULL) >= WINVERSION_8) ? NonPagedPoolNx : NonPagedPool;
    return ExAllocatePoolWithTag(enmPoolType, cbSize, VBOXWDDM_MEMTAG);
}

PVOID vboxWddmMemAllocZero(IN SIZE_T cbSize)
{
    PVOID pvMem = vboxWddmMemAlloc(cbSize);
    if (pvMem)
        memset(pvMem, 0, cbSize);
    return pvMem;
}


VOID vboxWddmMemFree(PVOID pvMem)
{
    ExFreePool(pvMem);
}

DECLINLINE(void) VBoxWddmOaHostIDReleaseLocked(PVBOXWDDM_OPENALLOCATION pOa)
{
    Assert(pOa->cHostIDRefs);
    PVBOXWDDM_ALLOCATION pAllocation = pOa->pAllocation;
    Assert(pAllocation->AllocData.cHostIDRefs >= pOa->cHostIDRefs);
    Assert(pAllocation->AllocData.hostID);
    --pOa->cHostIDRefs;
    --pAllocation->AllocData.cHostIDRefs;
    if (!pAllocation->AllocData.cHostIDRefs)
        pAllocation->AllocData.hostID = 0;
}

DECLINLINE(void) VBoxWddmOaHostIDCheckReleaseLocked(PVBOXWDDM_OPENALLOCATION pOa)
{
    if (pOa->cHostIDRefs)
        VBoxWddmOaHostIDReleaseLocked(pOa);
}

DECLINLINE(void) VBoxWddmOaRelease(PVBOXWDDM_OPENALLOCATION pOa)
{
    PVBOXWDDM_ALLOCATION pAllocation = pOa->pAllocation;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    Assert(pAllocation->cOpens);
    VBoxWddmOaHostIDCheckReleaseLocked(pOa);
    --pAllocation->cOpens;
    uint32_t cOpens = --pOa->cOpens;
    Assert(cOpens < UINT32_MAX/2);
    if (!cOpens)
    {
        RemoveEntryList(&pOa->ListEntry);
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
        vboxWddmMemFree(pOa);
    }
    else
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
}

DECLINLINE(PVBOXWDDM_OPENALLOCATION) VBoxWddmOaSearchLocked(PVBOXWDDM_DEVICE pDevice, PVBOXWDDM_ALLOCATION pAllocation)
{
    for (PLIST_ENTRY pCur = pAllocation->OpenList.Flink; pCur != &pAllocation->OpenList; pCur = pCur->Flink)
    {
        PVBOXWDDM_OPENALLOCATION pCurOa = CONTAINING_RECORD(pCur, VBOXWDDM_OPENALLOCATION, ListEntry);
        if (pCurOa->pDevice == pDevice)
        {
            return pCurOa;
        }
    }
    return NULL;
}

DECLINLINE(PVBOXWDDM_OPENALLOCATION) VBoxWddmOaSearch(PVBOXWDDM_DEVICE pDevice, PVBOXWDDM_ALLOCATION pAllocation)
{
    PVBOXWDDM_OPENALLOCATION pOa;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    pOa = VBoxWddmOaSearchLocked(pDevice, pAllocation);
    KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
    return pOa;
}

DECLINLINE(int) VBoxWddmOaSetHostID(PVBOXWDDM_DEVICE pDevice, PVBOXWDDM_ALLOCATION pAllocation, uint32_t hostID, uint32_t *pHostID)
{
    PVBOXWDDM_OPENALLOCATION pOa;
    KIRQL OldIrql;
    int rc = VINF_SUCCESS;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    pOa = VBoxWddmOaSearchLocked(pDevice, pAllocation);
    if (!pOa)
    {
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);;
        WARN(("no open allocation!"));
        return VERR_INVALID_STATE;
    }

    if (hostID)
    {
        if (pAllocation->AllocData.hostID == 0)
        {
            pAllocation->AllocData.hostID = hostID;
        }
        else if (pAllocation->AllocData.hostID != hostID)
        {
            WARN(("hostID differ: alloc(%d), trying to assign(%d)", pAllocation->AllocData.hostID, hostID));
            hostID = pAllocation->AllocData.hostID;
            rc = VERR_NOT_EQUAL;
        }

        ++pAllocation->AllocData.cHostIDRefs;
        ++pOa->cHostIDRefs;
    }
    else
        VBoxWddmOaHostIDCheckReleaseLocked(pOa);

    KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);

    if (pHostID)
        *pHostID = hostID;

    return rc;
}

DECLINLINE(PVBOXWDDM_ALLOCATION) vboxWddmGetAllocationFromHandle(PVBOXMP_DEVEXT pDevExt, D3DKMT_HANDLE hAllocation)
{
    DXGKARGCB_GETHANDLEDATA GhData;
    GhData.hObject = hAllocation;
    GhData.Type = DXGK_HANDLE_ALLOCATION;
    GhData.Flags.Value = 0;
    return (PVBOXWDDM_ALLOCATION)pDevExt->u.primary.DxgkInterface.DxgkCbGetHandleData(&GhData);
}

int vboxWddmGhDisplayPostInfoScreen(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *pScreen =
        (VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx,
                                                                          sizeof(VBVAINFOSCREEN),
                                                                          HGSMI_CH_VBVA,
                                                                          VBVA_INFO_SCREEN);
    if (!pScreen != NULL)
    {
        WARN(("VBoxHGSMIBufferAlloc failed"));
        return VERR_OUT_OF_RESOURCES;
    }

    int rc = vboxWddmScreenInfoInit(pScreen, pAllocData, pVScreenPos, fFlags);
    if (RT_SUCCESS(rc))
    {
        pScreen->u32StartOffset = 0; //(uint32_t)offVram; /* we pretend the view is located at the start of each framebuffer */

        rc = VBoxHGSMIBufferSubmit(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pScreen);
        if (RT_FAILURE(rc))
            WARN(("VBoxHGSMIBufferSubmit failed %d", rc));
    }
    else
        WARN(("VBoxHGSMIBufferSubmit failed %d", rc));

    VBoxHGSMIBufferFree(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pScreen);

    return rc;
}

int vboxWddmGhDisplayPostInfoView(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData)
{
    VBOXVIDEOOFFSET offVram = vboxWddmAddrFramOffset(&pAllocData->Addr);
    if (offVram == VBOXVIDEOOFFSET_VOID)
    {
        WARN(("offVram == VBOXVIDEOOFFSET_VOID"));
        return VERR_INVALID_PARAMETER;
    }

    /* Issue the screen info command. */
    VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_HOST *pView =
        (VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx,
                                                                        sizeof(VBVAINFOVIEW), HGSMI_CH_VBVA, VBVA_INFO_VIEW);
    if (!pView)
    {
        WARN(("VBoxHGSMIBufferAlloc failed"));
        return VERR_OUT_OF_RESOURCES;
    }
    pView->u32ViewIndex     = pAllocData->SurfDesc.VidPnSourceId;
    pView->u32ViewOffset    = (uint32_t)offVram; /* we pretend the view is located at the start of each framebuffer */
    pView->u32ViewSize      = vboxWddmVramCpuVisibleSegmentSize(pDevExt)/VBoxCommonFromDeviceExt(pDevExt)->cDisplays;
    pView->u32MaxScreenSize = pView->u32ViewSize;

    int rc = VBoxHGSMIBufferSubmit (&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pView);
    if (RT_FAILURE(rc))
        WARN(("VBoxHGSMIBufferSubmit failed %d", rc));

    VBoxHGSMIBufferFree(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pView);
    return rc;
}

NTSTATUS vboxWddmGhDisplayPostResizeLegacy(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    int rc;

    if (!(fFlags & (VBVA_SCREEN_F_DISABLED | VBVA_SCREEN_F_BLANK2)))
    {
        rc = vboxWddmGhDisplayPostInfoView(pDevExt, pAllocData);
        if (RT_FAILURE(rc))
        {
            WARN(("vboxWddmGhDisplayPostInfoView failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
    }

    rc = vboxWddmGhDisplayPostInfoScreen(pDevExt, pAllocData, pVScreenPos, fFlags);
    if (RT_FAILURE(rc))
    {
        WARN(("vboxWddmGhDisplayPostInfoScreen failed %d", rc));
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS vboxWddmGhDisplayPostResizeNew(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint16_t fFlags)
{
    RT_NOREF(pDevExt, pAllocData, pTargetMap, pVScreenPos, fFlags);
    AssertFailed(); // Should not be here.
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS vboxWddmGhDisplaySetMode(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData)
{
    RT_NOREF(pDevExt);
    VBOXVIDEOOFFSET offVram = vboxWddmAddrFramOffset(&pAllocData->Addr);;
    if (offVram == VBOXVIDEOOFFSET_VOID)
    {
        WARN(("offVram == VBOXVIDEOOFFSET_VOID"));
        return STATUS_UNSUCCESSFUL;
    }

    USHORT width  = pAllocData->SurfDesc.width;
    USHORT height = pAllocData->SurfDesc.height;
    USHORT bpp    = pAllocData->SurfDesc.bpp;
    ULONG cbLine  = VBOXWDDM_ROUNDBOUND(((width * bpp) + 7) / 8, 4);
    ULONG yOffset = (ULONG)offVram / cbLine;
    ULONG xOffset = (ULONG)offVram % cbLine;

    if (bpp == 4)
    {
        xOffset <<= 1;
    }
    else
    {
        Assert(!(xOffset%((bpp + 7) >> 3)));
        xOffset /= ((bpp + 7) >> 3);
    }
    Assert(xOffset <= 0xffff);
    Assert(yOffset <= 0xffff);

    VBoxVideoSetModeRegisters(width, height, width, bpp, 0, (uint16_t)xOffset, (uint16_t)yOffset);
    /** @todo read back from port to check if mode switch was successful */

    return STATUS_SUCCESS;
}

static uint16_t vboxWddmCalcScreenFlags(PVBOXMP_DEVEXT pDevExt, bool fValidAlloc, bool fPowerOff, bool fDisabled)
{
    uint16_t u16Flags;

    if (fValidAlloc)
    {
        u16Flags = VBVA_SCREEN_F_ACTIVE;
    }
    else
    {
        if (   !fDisabled
            && fPowerOff
            && RT_BOOL(VBoxCommonFromDeviceExt(pDevExt)->u16SupportedScreenFlags & VBVA_SCREEN_F_BLANK2))
        {
            u16Flags = VBVA_SCREEN_F_ACTIVE | VBVA_SCREEN_F_BLANK2;
        }
        else
        {
            u16Flags = VBVA_SCREEN_F_DISABLED;
        }
    }

    return u16Flags;
}

NTSTATUS vboxWddmGhDisplaySetInfoLegacy(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint8_t u8CurCyncState, bool fPowerOff, bool fDisabled)
{
    RT_NOREF(u8CurCyncState);
    NTSTATUS Status = STATUS_SUCCESS;
    bool fValidAlloc = pAllocData->SurfDesc.width > 0 && pAllocData->SurfDesc.height > 0;
    uint16_t fu16Flags = vboxWddmCalcScreenFlags(pDevExt, fValidAlloc, fPowerOff, fDisabled);

    if (fValidAlloc)
    {
        if (pAllocData->SurfDesc.VidPnSourceId == 0)
            Status = vboxWddmGhDisplaySetMode(pDevExt, pAllocData);
    }

    if (NT_SUCCESS(Status))
    {
        Status = vboxWddmGhDisplayPostResizeLegacy(pDevExt, pAllocData, pVScreenPos,
                fu16Flags);
        if (NT_SUCCESS(Status))
        {
            return STATUS_SUCCESS;
        }
        else
            WARN(("vboxWddmGhDisplayPostResize failed, Status 0x%x", Status));
    }
    else
        WARN(("vboxWddmGhDisplaySetMode failed, Status 0x%x", Status));

    return Status;
}

NTSTATUS vboxWddmGhDisplaySetInfoNew(PVBOXMP_DEVEXT pDevExt, const VBOXWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint8_t u8CurCyncState, bool fPowerOff, bool fDisabled)
{
    RT_NOREF(u8CurCyncState);
    NTSTATUS Status = STATUS_SUCCESS;
    bool fValidAlloc = pAllocData->SurfDesc.width > 0 && pAllocData->SurfDesc.height > 0;
    uint16_t fu16Flags = vboxWddmCalcScreenFlags(pDevExt, fValidAlloc, fPowerOff, fDisabled);

    if (fValidAlloc)
    {
        if (ASMBitTest(pTargetMap, 0))
            Status = vboxWddmGhDisplaySetMode(pDevExt, pAllocData);
    }

    if (NT_SUCCESS(Status))
    {
        Status = vboxWddmGhDisplayPostResizeNew(pDevExt, pAllocData, pTargetMap, pVScreenPos, fu16Flags);
        if (NT_SUCCESS(Status))
        {
            return STATUS_SUCCESS;
        }
        else
            WARN(("vboxWddmGhDisplayPostResizeNew failed, Status 0x%x", Status));
    }
    else
        WARN(("vboxWddmGhDisplaySetMode failed, Status 0x%x", Status));

    return Status;
}

bool vboxWddmGhDisplayCheckSetInfoFromSourceNew(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, bool fReportTargets)
{
    if (pSource->u8SyncState == VBOXWDDM_HGSYNC_F_SYNCED_ALL)
    {
        if (!pSource->fTargetsReported && fReportTargets)
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
        else
            return false;
    }

    if (!pSource->AllocData.Addr.SegmentId && pSource->AllocData.SurfDesc.width)
        return false;

    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);
    uint32_t *pTargetMap;
    if (fReportTargets)
        pTargetMap = pSource->aTargetMap;
    else
    {
        memset(aTargetMap, 0, sizeof (aTargetMap));
        pTargetMap = aTargetMap;
    }

    NTSTATUS Status = vboxWddmGhDisplaySetInfoNew(pDevExt, &pSource->AllocData, pTargetMap, &pSource->VScreenPos, pSource->u8SyncState, RT_BOOL(pSource->bBlankedByPowerOff), false);
    if (NT_SUCCESS(Status))
    {
        if (fReportTargets && (pSource->u8SyncState & VBOXWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY) != VBOXWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY)
        {
            VBOXWDDM_TARGET_ITER Iter;
            VBoxVidPnStTIterInit(pSource, pDevExt->aTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);

            for (PVBOXWDDM_TARGET pTarget = VBoxVidPnStTIterNext(&Iter);
                    pTarget;
                    pTarget = VBoxVidPnStTIterNext(&Iter))
            {
                pTarget->u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
            }
        }

        pSource->u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
        pSource->fTargetsReported = !!fReportTargets;
        return true;
    }

    WARN(("vboxWddmGhDisplaySetInfoNew failed, Status (0x%x)", Status));
    return false;
}

bool vboxWddmGhDisplayCheckSetInfoFromSourceLegacy(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, bool fReportTargets)
{
    if (!fReportTargets)
        return false;

    if (pSource->u8SyncState == VBOXWDDM_HGSYNC_F_SYNCED_ALL)
        return false;

    if (!pSource->AllocData.Addr.SegmentId)
        return false;

    VBOXWDDM_TARGET_ITER Iter;
    VBoxVidPnStTIterInit(pSource, pDevExt->aTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);
    uint8_t u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
    VBOXWDDM_ALLOC_DATA AllocData = pSource->AllocData;

    for (PVBOXWDDM_TARGET pTarget = VBoxVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = VBoxVidPnStTIterNext(&Iter))
    {
        AllocData.SurfDesc.VidPnSourceId = pTarget->u32Id;
        NTSTATUS Status = vboxWddmGhDisplaySetInfoLegacy(pDevExt, &AllocData, &pSource->VScreenPos, pSource->u8SyncState | pTarget->u8SyncState, pTarget->fBlankedByPowerOff, pTarget->fDisabled);
        if (NT_SUCCESS(Status))
            pTarget->u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
        else
        {
            WARN(("vboxWddmGhDisplaySetInfoLegacy failed, Status (0x%x)", Status));
            u8SyncState = 0;
        }
    }

    pSource->u8SyncState |= u8SyncState;

    return true;
}

bool vboxWddmGhDisplayCheckSetInfoFromSourceEx(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, bool fReportTargets)
{
    if (pDevExt->fCmdVbvaEnabled)
        return vboxWddmGhDisplayCheckSetInfoFromSourceNew(pDevExt, pSource, fReportTargets);
    return vboxWddmGhDisplayCheckSetInfoFromSourceLegacy(pDevExt, pSource, fReportTargets);
}

bool vboxWddmGhDisplayCheckSetInfoFromSource(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;
    return vboxWddmGhDisplayCheckSetInfoFromSourceEx(pDevExt, pSource, fReportTargets);
}

bool vboxWddmGhDisplayCheckSetInfoForDisabledTargetsNew(PVBOXMP_DEVEXT pDevExt)
{
    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);

    memset(aTargetMap, 0, sizeof (aTargetMap));

    bool fFound = false;
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        Assert(pTarget->u32Id == (unsigned)i);
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        /* Explicitely disabled targets must not be skipped. */
        if (pTarget->fBlankedByPowerOff && !pTarget->fDisabled)
        {
            LOG(("Skip doing DISABLED request for PowerOff tgt %d", pTarget->u32Id));
            continue;
        }

        if (pTarget->u8SyncState != VBOXWDDM_HGSYNC_F_SYNCED_ALL)
        {
            ASMBitSet(aTargetMap, i);
            fFound = true;
        }
    }

    if (!fFound)
        return false;

    POINT VScreenPos = {0};
    VBOXWDDM_ALLOC_DATA AllocData;
    VBoxVidPnAllocDataInit(&AllocData, D3DDDI_ID_UNINITIALIZED);
    NTSTATUS Status = vboxWddmGhDisplaySetInfoNew(pDevExt, &AllocData, aTargetMap, &VScreenPos, 0, false, true);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxWddmGhDisplaySetInfoNew failed %#x", Status));
        return false;
    }

    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        pTarget->u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
    }

    return true;
}

bool vboxWddmGhDisplayCheckSetInfoForDisabledTargetsLegacy(PVBOXMP_DEVEXT pDevExt)
{
    POINT VScreenPos = {0};
    bool fFound = false;
    VBOXWDDM_ALLOC_DATA AllocData;
    VBoxVidPnAllocDataInit(&AllocData, D3DDDI_ID_UNINITIALIZED);

    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        Assert(pTarget->u32Id == (unsigned)i);
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        if (pTarget->u8SyncState == VBOXWDDM_HGSYNC_F_SYNCED_ALL)
            continue;

        fFound = true;
        AllocData.SurfDesc.VidPnSourceId = i;
        NTSTATUS Status = vboxWddmGhDisplaySetInfoLegacy(pDevExt, &AllocData, &VScreenPos, 0, pTarget->fBlankedByPowerOff, pTarget->fDisabled);
        if (NT_SUCCESS(Status))
            pTarget->u8SyncState = VBOXWDDM_HGSYNC_F_SYNCED_ALL;
        else
            WARN(("vboxWddmGhDisplaySetInfoLegacy failed, Status (0x%x)", Status));
    }

    return fFound;
}

void vboxWddmGhDisplayCheckSetInfoForDisabledTargets(PVBOXMP_DEVEXT pDevExt)
{
    if (pDevExt->fCmdVbvaEnabled)
        vboxWddmGhDisplayCheckSetInfoForDisabledTargetsNew(pDevExt);
    else
        vboxWddmGhDisplayCheckSetInfoForDisabledTargetsLegacy(pDevExt);
}

void vboxWddmGhDisplayCheckSetInfoForDisabledTargetsCheck(PVBOXMP_DEVEXT pDevExt)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;

    if (fReportTargets)
        vboxWddmGhDisplayCheckSetInfoForDisabledTargets(pDevExt);
}

void vboxWddmGhDisplayCheckSetInfoEx(PVBOXMP_DEVEXT pDevExt, bool fReportTargets)
{
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[i];
        vboxWddmGhDisplayCheckSetInfoFromSourceEx(pDevExt, pSource, fReportTargets);
    }

    if (fReportTargets)
        vboxWddmGhDisplayCheckSetInfoForDisabledTargets(pDevExt);
}

void vboxWddmGhDisplayCheckSetInfo(PVBOXMP_DEVEXT pDevExt)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;
    vboxWddmGhDisplayCheckSetInfoEx(pDevExt, fReportTargets);
}

PVBOXSHGSMI vboxWddmHgsmiGetHeapFromCmdOffset(PVBOXMP_DEVEXT pDevExt, HGSMIOFFSET offCmd)
{
    if (HGSMIAreaContainsOffset(&VBoxCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, offCmd))
        return &VBoxCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx;
    return NULL;
}

NTSTATUS vboxWddmPickResources(PVBOXMP_DEVEXT pDevExt, PDXGK_DEVICE_INFO pDeviceInfo, PVBOXWDDM_HWRESOURCES pHwResources)
{
    RT_NOREF(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT DispiId;
    memset(pHwResources, 0, sizeof (*pHwResources));
    pHwResources->cbVRAM = VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES;

    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID2);
    DispiId = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    if (DispiId == VBE_DISPI_ID2)
    {
       LOGREL(("found the VBE card"));
       /*
        * Write some hardware information to registry, so that
        * it's visible in Windows property dialog.
        */

       /*
        * Query the adapter's memory size. It's a bit of a hack, we just read
        * an ULONG from the data port without setting an index before.
        */
       pHwResources->cbVRAM = VBVO_PORT_READ_U32(VBE_DISPI_IOPORT_DATA);
       if (VBoxHGSMIIsSupported ())
       {
           PCM_RESOURCE_LIST pRcList = pDeviceInfo->TranslatedResourceList;
           /** @todo verify resources */
           for (ULONG i = 0; i < pRcList->Count; ++i)
           {
               PCM_FULL_RESOURCE_DESCRIPTOR pFRc = &pRcList->List[i];
               for (ULONG j = 0; j < pFRc->PartialResourceList.Count; ++j)
               {
                   PCM_PARTIAL_RESOURCE_DESCRIPTOR pPRc = &pFRc->PartialResourceList.PartialDescriptors[j];
                   switch (pPRc->Type)
                   {
                       case CmResourceTypePort:
#ifdef VBOX_WITH_VMSVGA
                           AssertBreak(pHwResources->phIO.QuadPart == 0);
                           pHwResources->phIO = pPRc->u.Port.Start;
                           pHwResources->cbIO = pPRc->u.Port.Length;
#endif
                           break;
                       case CmResourceTypeInterrupt:
                           break;
                       case CmResourceTypeMemory:
#ifdef VBOX_WITH_VMSVGA
                           if (pHwResources->phVRAM.QuadPart)
                           {
                               AssertBreak(pHwResources->phFIFO.QuadPart == 0);
                               pHwResources->phFIFO = pPRc->u.Memory.Start;
                               pHwResources->cbFIFO = pPRc->u.Memory.Length;
                               break;
                           }
#else
                           /* we assume there is one memory segment */
                           AssertBreak(pHwResources->phVRAM.QuadPart == 0);
#endif
                           pHwResources->phVRAM = pPRc->u.Memory.Start;
                           Assert(pHwResources->phVRAM.QuadPart != 0);
                           pHwResources->ulApertureSize = pPRc->u.Memory.Length;
                           Assert(pHwResources->cbVRAM <= pHwResources->ulApertureSize);
                           break;
                       case CmResourceTypeDma:
                           break;
                       case CmResourceTypeDeviceSpecific:
                           break;
                       case CmResourceTypeBusNumber:
                           break;
                       default:
                           break;
                   }
               }
           }
       }
       else
       {
           LOGREL(("HGSMI unsupported, returning err"));
           /** @todo report a better status */
           Status = STATUS_UNSUCCESSFUL;
       }
    }
    else
    {
        LOGREL(("VBE card not found, returning err"));
        Status = STATUS_UNSUCCESSFUL;
    }


    return Status;
}

static void vboxWddmDevExtZeroinit(PVBOXMP_DEVEXT pDevExt, CONST PDEVICE_OBJECT pPDO)
{
    memset(pDevExt, 0, sizeof (VBOXMP_DEVEXT));
    pDevExt->pPDO = pPDO;
    PWCHAR pName = (PWCHAR)(((uint8_t*)pDevExt) + VBOXWDDM_ROUNDBOUND(sizeof(VBOXMP_DEVEXT), 8));
    RtlInitUnicodeString(&pDevExt->RegKeyName, pName);

    VBoxVidPnSourcesInit(pDevExt->aSources, RT_ELEMENTS(pDevExt->aSources), 0);

    VBoxVidPnTargetsInit(pDevExt->aTargets, RT_ELEMENTS(pDevExt->aTargets), 0);

    BOOLEAN f3DSupported = FALSE;
    uint32_t u32 = 0;
    if (VBoxVGACfgAvailable())
    {
        VBoxVGACfgQuery(VBE_DISPI_CFG_ID_3D, &u32, 0);
        f3DSupported = RT_BOOL(u32);

        VBoxVGACfgQuery(VBE_DISPI_CFG_ID_VMSVGA, &u32, 0);
    }

    pDevExt->enmHwType = u32 ? VBOXVIDEO_HWTYPE_VMSVGA : VBOXVIDEO_HWTYPE_VBOX;

    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
    {
        pDevExt->f3DEnabled = FALSE;
    }
    else if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        pDevExt->f3DEnabled = f3DSupported;
    }
    else
    {
        /* No supported 3D hardware, Fallback to VBox 2D only. */
        pDevExt->enmHwType = VBOXVIDEO_HWTYPE_VBOX;
        pDevExt->f3DEnabled = FALSE;
    }
}

static void vboxWddmSetupDisplaysLegacy(PVBOXMP_DEVEXT pDevExt)
{
    /* For WDDM, we simply store the number of monitors as we will deal with
     * VidPN stuff later */
    int rc = STATUS_SUCCESS;

    if (VBoxCommonFromDeviceExt(pDevExt)->bHGSMI)
    {
        ULONG ulAvailable = VBoxCommonFromDeviceExt(pDevExt)->cbVRAM
                            - VBoxCommonFromDeviceExt(pDevExt)->cbMiniportHeap
                            - VBVA_ADAPTER_INFORMATION_SIZE;

        ULONG ulSize;
        ULONG offset;
        offset = ulAvailable;
        rc = vboxVdmaCreate (pDevExt, &pDevExt->u.primary.Vdma
                );
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            /* can enable it right away since the host does not need any screen/FB info
             * for basic DMA functionality */
            rc = vboxVdmaEnable(pDevExt, &pDevExt->u.primary.Vdma);
            AssertRC(rc);
            if (RT_FAILURE(rc))
                vboxVdmaDestroy(pDevExt, &pDevExt->u.primary.Vdma);
        }

        ulAvailable = offset;
        ulSize = ulAvailable/2;
        offset = ulAvailable - ulSize;

        NTSTATUS Status = vboxVideoAMgrCreate(pDevExt, &pDevExt->AllocMgr, offset, ulSize);
        AssertNtStatusSuccess(Status);
        if (Status != STATUS_SUCCESS)
        {
            offset = ulAvailable;
        }

#ifdef VBOXWDDM_RENDER_FROM_SHADOW
        if (RT_SUCCESS(rc))
        {
            ulAvailable = offset;
            ulSize = ulAvailable / 2;
            ulSize /= VBoxCommonFromDeviceExt(pDevExt)->cDisplays;
            Assert(ulSize > VBVA_MIN_BUFFER_SIZE);
            if (ulSize > VBVA_MIN_BUFFER_SIZE)
            {
                ULONG ulRatio = ulSize/VBVA_MIN_BUFFER_SIZE;
                ulRatio >>= 4; /* /= 16; */
                if (ulRatio)
                    ulSize = VBVA_MIN_BUFFER_SIZE * ulRatio;
                else
                    ulSize = VBVA_MIN_BUFFER_SIZE;
            }
            else
            {
                /** @todo ?? */
            }

            ulSize &= ~0xFFF;
            Assert(ulSize);

            Assert(ulSize * VBoxCommonFromDeviceExt(pDevExt)->cDisplays < ulAvailable);

            for (int i = VBoxCommonFromDeviceExt(pDevExt)->cDisplays-1; i >= 0; --i)
            {
                offset -= ulSize;
                rc = vboxVbvaCreate(pDevExt, &pDevExt->aSources[i].Vbva, offset, ulSize, i);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    rc = vboxVbvaEnable(pDevExt, &pDevExt->aSources[i].Vbva);
                    AssertRC(rc);
                    if (RT_FAILURE(rc))
                    {
                        /** @todo de-initialize */
                    }
                }
            }
        }
#endif

        /* vboxWddmVramCpuVisibleSize uses this value */
        pDevExt->cbVRAMCpuVisible = offset;

        rc = VBoxMPCmnMapAdapterMemory(VBoxCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram,
                                       0, vboxWddmVramCpuVisibleSize(pDevExt));
        Assert(rc == VINF_SUCCESS);
        if (rc != VINF_SUCCESS)
            pDevExt->pvVisibleVram = NULL;

        if (RT_FAILURE(rc))
            VBoxCommonFromDeviceExt(pDevExt)->bHGSMI = FALSE;
    }
}

static NTSTATUS vboxWddmSetupDisplaysNew(PVBOXMP_DEVEXT pDevExt)
{
    if (!VBoxCommonFromDeviceExt(pDevExt)->bHGSMI)
        return STATUS_UNSUCCESSFUL;

    ULONG cbAvailable = VBoxCommonFromDeviceExt(pDevExt)->cbVRAM
                            - VBoxCommonFromDeviceExt(pDevExt)->cbMiniportHeap
                            - VBVA_ADAPTER_INFORMATION_SIZE;

    /* vboxWddmVramCpuVisibleSize uses this value */
    pDevExt->cbVRAMCpuVisible = cbAvailable;

    int rc = VBoxMPCmnMapAdapterMemory(VBoxCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram,
                                   0, vboxWddmVramCpuVisibleSize(pDevExt));
    if (RT_FAILURE(rc))
    {
        WARN(("VBoxMPCmnMapAdapterMemory failed, rc %d", rc));
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS vboxWddmSetupDisplays(PVBOXMP_DEVEXT pDevExt)
{
    if (pDevExt->fCmdVbvaEnabled)
    {
        NTSTATUS Status = vboxWddmSetupDisplaysNew(pDevExt);
        if (!NT_SUCCESS(Status))
            VBoxCommonFromDeviceExt(pDevExt)->bHGSMI = FALSE;
        return Status;
    }

    vboxWddmSetupDisplaysLegacy(pDevExt);
    return VBoxCommonFromDeviceExt(pDevExt)->bHGSMI ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static int vboxWddmFreeDisplays(PVBOXMP_DEVEXT pDevExt)
{
    int rc = VINF_SUCCESS;

    Assert(pDevExt->pvVisibleVram);
    if (pDevExt->pvVisibleVram)
        VBoxMPCmnUnmapAdapterMemory(VBoxCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram);

    if (!pDevExt->fCmdVbvaEnabled)
    {
        for (int i = VBoxCommonFromDeviceExt(pDevExt)->cDisplays-1; i >= 0; --i)
        {
            rc = vboxVbvaDisable(pDevExt, &pDevExt->aSources[i].Vbva);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                rc = vboxVbvaDestroy(pDevExt, &pDevExt->aSources[i].Vbva);
                AssertRC(rc);
                if (RT_FAILURE(rc))
                {
                    /** @todo */
                }
            }
        }

        vboxVideoAMgrDestroy(pDevExt, &pDevExt->AllocMgr);

        rc = vboxVdmaDisable(pDevExt, &pDevExt->u.primary.Vdma);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = vboxVdmaDestroy(pDevExt, &pDevExt->u.primary.Vdma);
            AssertRC(rc);
        }
    }

    return rc;
}


/* driver callbacks */
NTSTATUS DxgkDdiAddDevice(
    IN CONST PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PVOID *MiniportDeviceContext
    )
{
    /* The DxgkDdiAddDevice function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, pdo(0x%x)", PhysicalDeviceObject));

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXMP_DEVEXT pDevExt = NULL;

    WCHAR RegKeyBuf[512];
    ULONG cbRegKeyBuf = sizeof (RegKeyBuf);

    Status = IoGetDeviceProperty (PhysicalDeviceObject,
                                  DevicePropertyDriverKeyName,
                                  cbRegKeyBuf,
                                  RegKeyBuf,
                                  &cbRegKeyBuf);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        pDevExt = (PVBOXMP_DEVEXT)vboxWddmMemAllocZero(VBOXWDDM_ROUNDBOUND(sizeof(VBOXMP_DEVEXT), 8) + cbRegKeyBuf);
        if (pDevExt)
        {
            PWCHAR pName = (PWCHAR)(((uint8_t*)pDevExt) + VBOXWDDM_ROUNDBOUND(sizeof(VBOXMP_DEVEXT), 8));
            memcpy(pName, RegKeyBuf, cbRegKeyBuf);
            vboxWddmDevExtZeroinit(pDevExt, PhysicalDeviceObject);
            *MiniportDeviceContext = pDevExt;
        }
        else
        {
            Status  = STATUS_NO_MEMORY;
            LOGREL(("ERROR, failed to create context"));
        }
    }

    LOGF(("LEAVE, Status(0x%x), pDevExt(0x%x)", Status, pDevExt));

    return Status;
}

NTSTATUS DxgkDdiStartDevice(
    IN CONST PVOID  MiniportDeviceContext,
    IN PDXGK_START_INFO  DxgkStartInfo,
    IN PDXGKRNL_INTERFACE  DxgkInterface,
    OUT PULONG  NumberOfVideoPresentSources,
    OUT PULONG  NumberOfChildren
    )
{
    /* The DxgkDdiStartDevice function should be made pageable. */
    PAGED_CODE();

    NTSTATUS Status;

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    if ( ARGUMENT_PRESENT(MiniportDeviceContext) &&
        ARGUMENT_PRESENT(DxgkInterface) &&
        ARGUMENT_PRESENT(DxgkStartInfo) &&
        ARGUMENT_PRESENT(NumberOfVideoPresentSources) &&
        ARGUMENT_PRESENT(NumberOfChildren)
        )
    {
        PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

        vboxWddmVGuidGet(pDevExt);

        /* Save DeviceHandle and function pointers supplied by the DXGKRNL_INTERFACE structure passed to DxgkInterface. */
        memcpy(&pDevExt->u.primary.DxgkInterface, DxgkInterface, sizeof (DXGKRNL_INTERFACE));

        /* Allocate a DXGK_DEVICE_INFO structure, and call DxgkCbGetDeviceInformation to fill in the members of that structure, which include the registry path, the PDO, and a list of translated resources for the display adapter represented by MiniportDeviceContext. Save selected members (ones that the display miniport driver will need later)
         * of the DXGK_DEVICE_INFO structure in the context block represented by MiniportDeviceContext. */
        DXGK_DEVICE_INFO DeviceInfo;
        Status = pDevExt->u.primary.DxgkInterface.DxgkCbGetDeviceInformation (pDevExt->u.primary.DxgkInterface.DeviceHandle, &DeviceInfo);
        if (Status == STATUS_SUCCESS)
        {
            Status = vboxWddmPickResources(pDevExt, &DeviceInfo, &pDevExt->HwResources);
            if (Status == STATUS_SUCCESS)
            {
                /* Figure out the host capabilities. Start with nothing. */
                pDevExt->fCmdVbvaEnabled = FALSE;
                pDevExt->fComplexTopologiesEnabled = TRUE;

                if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
                {
                    pDevExt->f3DEnabled = FALSE;
                }
#ifdef VBOX_WITH_VMSVGA
                else if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
                {
                    if (pDevExt->f3DEnabled)
                    {
                        /// @todo This enables legacy code which is shared with VMSVGA, for example displays setup.
                        //  Must be removed eventually.
                        pDevExt->fCmdVbvaEnabled = TRUE;
                        pDevExt->fComplexTopologiesEnabled = TRUE; /** @todo Implement clones support. */
                    }
                }
#endif /* VBOX_WITH_VMSVGA */
                else
                {
                    pDevExt->f3DEnabled = FALSE;
                }

                LOGREL(("Handling complex topologies %s", pDevExt->fComplexTopologiesEnabled ? "enabled" : "disabled"));

                /* Guest supports only HGSMI, the old VBVA via VMMDev is not supported.
                 * The host will however support both old and new interface to keep compatibility
                 * with old guest additions.
                 */
                VBoxSetupDisplaysHGSMI(VBoxCommonFromDeviceExt(pDevExt),
                                       pDevExt->HwResources.phVRAM, pDevExt->HwResources.ulApertureSize, pDevExt->HwResources.cbVRAM,
                                       VBVACAPS_COMPLETEGCMD_BY_IOREAD | VBVACAPS_IRQ);
                if (VBoxCommonFromDeviceExt(pDevExt)->bHGSMI)
                {
                    vboxWddmSetupDisplays(pDevExt);
                    if (!VBoxCommonFromDeviceExt(pDevExt)->bHGSMI)
                        VBoxFreeDisplaysHGSMI(VBoxCommonFromDeviceExt(pDevExt));
                }
                if (VBoxCommonFromDeviceExt(pDevExt)->bHGSMI)
                {
                    LOGREL(("using HGSMI"));
                    *NumberOfVideoPresentSources = VBoxCommonFromDeviceExt(pDevExt)->cDisplays;
                    *NumberOfChildren = VBoxCommonFromDeviceExt(pDevExt)->cDisplays;
                    LOG(("sources(%d), children(%d)", *NumberOfVideoPresentSources, *NumberOfChildren));

                    vboxVdmaDdiNodesInit(pDevExt);
                    vboxVideoCmInit(&pDevExt->CmMgr);
                    vboxVideoCmInit(&pDevExt->SeamlessCtxMgr);
                    pDevExt->cContexts3D = 0;
                    pDevExt->cContexts2D = 0;
                    pDevExt->cContextsDispIfResize = 0;
                    pDevExt->cUnlockedVBVADisabled = 0;
                    pDevExt->fDisableTargetUpdate = 0;
                    VBOXWDDM_CTXLOCK_INIT(pDevExt);
                    KeInitializeSpinLock(&pDevExt->SynchLock);

                    VBoxCommonFromDeviceExt(pDevExt)->fAnyX = VBoxVideoAnyWidthAllowed();
#if 0
                    vboxShRcTreeInit(pDevExt);
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
                    vboxVhwaInit(pDevExt);
#endif
                    VBoxWddmSlInit(pDevExt);

                    for (UINT i = 0; i < (UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                    {
                        PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[i];
                        KeInitializeSpinLock(&pSource->AllocationLock);
                    }

                    DWORD dwVal = VBOXWDDM_CFG_DRV_DEFAULT;
                    HANDLE hKey = NULL;

                    Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_READ, &hKey);
                    if (!NT_SUCCESS(Status))
                    {
                        WARN(("IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
                        hKey = NULL;
                    }


                    if (hKey)
                    {
                        Status = vboxWddmRegQueryValueDword(hKey, VBOXWDDM_REG_DRV_FLAGS_NAME, &dwVal);
                        LogRel2((VBOX_VIDEO_LOG_NAME ": RegQueryDword '%ls'. Status 0x%X, dwVal %d\n", VBOXWDDM_REG_DRV_FLAGS_NAME, Status, dwVal));
                        if (!NT_SUCCESS(Status))
                        {
                            LOG(("vboxWddmRegQueryValueDword failed, Status = 0x%x", Status));
                            dwVal = VBOXWDDM_CFG_DRV_DEFAULT;
                        }
                    }

                    pDevExt->dwDrvCfgFlags = dwVal;

                    for (UINT i = 0; i < (UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                    {
                        PVBOXWDDM_TARGET pTarget = &pDevExt->aTargets[i];
                        if (i == 0 || (pDevExt->dwDrvCfgFlags & VBOXWDDM_CFG_DRV_SECONDARY_TARGETS_CONNECTED) || !hKey)
                        {
                            pTarget->fConnected = true;
                            pTarget->fConfigured = true;
                        }
                        else if (hKey)
                        {
                            WCHAR wszNameBuf[sizeof(VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX) / sizeof(WCHAR) + 32];
                            RTUtf16Printf(wszNameBuf, RT_ELEMENTS(wszNameBuf), "%ls%u", VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX, i);
                            Status = vboxWddmRegQueryValueDword(hKey, wszNameBuf, &dwVal);
                            LogRel2((VBOX_VIDEO_LOG_NAME ": RegQueryDword '%ls'. Status 0x%X, dwVal %d\n", wszNameBuf, Status, dwVal));
                            if (NT_SUCCESS(Status))
                            {
                                pTarget->fConnected = !!(dwVal & VBOXWDDM_CFG_DRVTARGET_CONNECTED);
                                pTarget->fConfigured = true;
                            }
                            else
                            {
                                WARN(("vboxWddmRegQueryValueDword failed, Status = 0x%x", Status));
                                pTarget->fConnected = false;
                                pTarget->fConfigured = false;
                            }
                        }
                    }

                    if (hKey)
                    {
                        NTSTATUS rcNt2 = ZwClose(hKey);
                        Assert(rcNt2 == STATUS_SUCCESS); NOREF(rcNt2);
                    }

                    Status = STATUS_SUCCESS;

                    if (VBoxQueryWinVersion(NULL) >= WINVERSION_8)
                    {
                        DXGK_DISPLAY_INFORMATION DisplayInfo;
                        Status = pDevExt->u.primary.DxgkInterface.DxgkCbAcquirePostDisplayOwnership(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                                &DisplayInfo);
                        if (NT_SUCCESS(Status))
                        {
                            PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[0];
                            PHYSICAL_ADDRESS PhAddr;
                            /* display info may sometimes not be valid, e.g. on from-full-graphics wddm driver update
                             * ensure we have something meaningful here */
                            if (!DisplayInfo.Width)
                            {
                                PhAddr = VBoxCommonFromDeviceExt(pDevExt)->phVRAM;
                                vboxWddmDiInitDefault(&DisplayInfo, PhAddr, 0);
                            }
                            else
                            {
                                PhAddr = DisplayInfo.PhysicAddress;
                                DisplayInfo.TargetId = 0;
                            }

                            vboxWddmDiToAllocData(pDevExt, &DisplayInfo, &pSource->AllocData);

                            /* init the rest source infos with some default values */
                            for (UINT i = 1; i < (UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                            {
                                PhAddr.QuadPart += pSource->AllocData.SurfDesc.cbSize;
                                PhAddr.QuadPart = ROUND_TO_PAGES(PhAddr.QuadPart);
                                vboxWddmDiInitDefault(&DisplayInfo, PhAddr, i);
                                pSource = &pDevExt->aSources[i];
                                vboxWddmDiToAllocData(pDevExt, &DisplayInfo, &pSource->AllocData);
                            }
                        }
                        else
                        {
                            WARN(("DxgkCbAcquirePostDisplayOwnership failed, Status 0x%x", Status));
                        }
                    }

                    VBoxWddmVModesInit(pDevExt);

#ifdef VBOX_WITH_VMSVGA
                    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
                    {
                        LOGREL(("WDDM: VRAM %#RX64/%#RX32, FIFO %#RX64/%#RX32, IO %#RX64/%#RX32",
                                pDevExt->HwResources.phVRAM.QuadPart, pDevExt->HwResources.cbVRAM,
                                pDevExt->HwResources.phFIFO.QuadPart, pDevExt->HwResources.cbFIFO,
                                pDevExt->HwResources.phIO.QuadPart, pDevExt->HwResources.cbIO));

                        Status = GaAdapterStart(pDevExt);
                        if (Status == STATUS_SUCCESS)
                        { /* likely */ }
                        else
                            LOGREL(("WDDM: GaAdapterStart failed Status(0x%x)", Status));
                    }
#endif
                }
                else
                {
                    LOGREL(("HGSMI failed to initialize, returning err"));

                    /** @todo report a better status */
                    Status = STATUS_UNSUCCESSFUL;
                }
            }
            else
            {
                LOGREL(("vboxWddmPickResources failed Status(0x%x), returning err", Status));
                Status = STATUS_UNSUCCESSFUL;
            }
        }
        else
        {
            LOGREL(("DxgkCbGetDeviceInformation failed Status(0x%x), returning err", Status));
        }
    }
    else
    {
        LOGREL(("invalid parameter, returning err"));
        Status = STATUS_INVALID_PARAMETER;
    }

    LOGF(("LEAVE, status(0x%x)", Status));

    return Status;
}

NTSTATUS DxgkDdiStopDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    /* The DxgkDdiStopDevice function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;
    NTSTATUS Status = STATUS_SUCCESS;

#ifdef VBOX_WITH_VMSVGA
     if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
     {
         GaAdapterStop(pDevExt);
     }
#endif

    VBoxWddmSlTerm(pDevExt);

    vboxVideoCmTerm(&pDevExt->CmMgr);

    vboxVideoCmTerm(&pDevExt->SeamlessCtxMgr);

    /* do everything we did on DxgkDdiStartDevice in the reverse order */
#ifdef VBOX_WITH_VIDEOHWACCEL
    vboxVhwaFree(pDevExt);
#endif
#if 0
    vboxShRcTreeTerm(pDevExt);
#endif

    int rc = vboxWddmFreeDisplays(pDevExt);
    if (RT_SUCCESS(rc))
        VBoxFreeDisplaysHGSMI(VBoxCommonFromDeviceExt(pDevExt));
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        vboxWddmVGuidFree(pDevExt);

        VBoxWddmVModesCleanup();
        /* revert back to the state we were right after the DxgkDdiAddDevice */
        vboxWddmDevExtZeroinit(pDevExt, pDevExt->pPDO);
    }
    else
        Status = STATUS_UNSUCCESSFUL;

    return Status;
}

NTSTATUS DxgkDdiRemoveDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    /* DxgkDdiRemoveDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    vboxWddmMemFree(MiniportDeviceContext);

    LOGF(("LEAVE, context(0x%p)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiDispatchIoRequest(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG VidPnSourceId,
    IN PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    LOGF(("ENTER, context(0x%p), ctl(0x%x)", MiniportDeviceContext, VideoRequestPacket->IoControlCode));

    AssertBreakpoint();
#if 0
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

    switch (VideoRequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES:
        {
            if (VideoRequestPacket->OutputBufferLength < sizeof(VIDEO_COLOR_CAPABILITIES))
            {
                AssertBreakpoint();
                VideoRequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return TRUE;
            }
            VIDEO_COLOR_CAPABILITIES *pCaps = (VIDEO_COLOR_CAPABILITIES*)VideoRequestPacket->OutputBuffer;

            pCaps->Length = sizeof (VIDEO_COLOR_CAPABILITIES);
            pCaps->AttributeFlags = VIDEO_DEVICE_COLOR;
            pCaps->RedPhosphoreDecay = 0;
            pCaps->GreenPhosphoreDecay = 0;
            pCaps->BluePhosphoreDecay = 0;
            pCaps->WhiteChromaticity_x = 3127;
            pCaps->WhiteChromaticity_y = 3290;
            pCaps->WhiteChromaticity_Y = 0;
            pCaps->RedChromaticity_x = 6700;
            pCaps->RedChromaticity_y = 3300;
            pCaps->GreenChromaticity_x = 2100;
            pCaps->GreenChromaticity_y = 7100;
            pCaps->BlueChromaticity_x = 1400;
            pCaps->BlueChromaticity_y = 800;
            pCaps->WhiteGamma = 0;
            pCaps->RedGamma = 20000;
            pCaps->GreenGamma = 20000;
            pCaps->BlueGamma = 20000;

            VideoRequestPacket->StatusBlock->Status = NO_ERROR;
            VideoRequestPacket->StatusBlock->Information = sizeof (VIDEO_COLOR_CAPABILITIES);
            break;
        }
#if 0
        case IOCTL_VIDEO_HANDLE_VIDEOPARAMETERS:
        {
            if (VideoRequestPacket->OutputBufferLength < sizeof(VIDEOPARAMETERS)
                    || VideoRequestPacket->InputBufferLength < sizeof(VIDEOPARAMETERS))
            {
                AssertBreakpoint();
                VideoRequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return TRUE;
            }

            Result = VBoxVideoResetDevice((PVBOXMP_DEVEXT)HwDeviceExtension,
                                          RequestPacket->StatusBlock);
            break;
        }
#endif
        default:
            AssertBreakpoint();
            VideoRequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
            VideoRequestPacket->StatusBlock->Information = 0;
    }
#else
    RT_NOREF(MiniportDeviceContext, VidPnSourceId, VideoRequestPacket);
#endif
    LOGF(("LEAVE, context(0x%p), ctl(0x%x)", MiniportDeviceContext, VideoRequestPacket->IoControlCode));

    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiQueryChildRelations(
    IN CONST PVOID MiniportDeviceContext,
    IN OUT PDXGK_CHILD_DESCRIPTOR ChildRelations,
    IN ULONG ChildRelationsSize
    )
{
    RT_NOREF(ChildRelationsSize);
    /* The DxgkDdiQueryChildRelations function should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));
    Assert(ChildRelationsSize == (VBoxCommonFromDeviceExt(pDevExt)->cDisplays + 1)*sizeof(DXGK_CHILD_DESCRIPTOR));
    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        ChildRelations[i].ChildDeviceType = TypeVideoOutput;
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HD15; /* VGA */
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE; //D3DKMDT_MOA_INTERRUPTIBLE; /* ?? D3DKMDT_MOA_NONE*/
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        ChildRelations[i].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible; /* ?? HpdAwarenessAlwaysConnected; */
        ChildRelations[i].AcpiUid =  0; /* */
        ChildRelations[i].ChildUid = i; /* should be == target id */
    }
    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiQueryChildStatus(
    IN CONST PVOID  MiniportDeviceContext,
    IN PDXGK_CHILD_STATUS  ChildStatus,
    IN BOOLEAN  NonDestructiveOnly
    )
{
    RT_NOREF(NonDestructiveOnly);
    /* The DxgkDdiQueryChildStatus should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

    if (ChildStatus->ChildUid >= (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
    {
        WARN(("Invalid child id %d", ChildStatus->ChildUid));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    switch (ChildStatus->Type)
    {
        case StatusConnection:
        {
            LOGF(("StatusConnection"));
            VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[ChildStatus->ChildUid];
            BOOLEAN Connected = !!pTarget->fConnected;
            if (!Connected)
                LOGREL(("Tgt[%d] DISCONNECTED!!", ChildStatus->ChildUid));
            ChildStatus->HotPlug.Connected = !!pTarget->fConnected;
            break;
        }
        case StatusRotation:
            LOGF(("StatusRotation"));
            ChildStatus->Rotation.Angle = 0;
            break;
        default:
            WARN(("ERROR: status type: %d", ChildStatus->Type));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    return Status;
}

NTSTATUS DxgkDdiQueryDeviceDescriptor(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG ChildUid,
    IN OUT PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    )
{
    RT_NOREF(MiniportDeviceContext, ChildUid, DeviceDescriptor);
    /* The DxgkDdiQueryDeviceDescriptor should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    /* we do not support EDID */
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

NTSTATUS DxgkDdiSetPowerState(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG DeviceUid,
    IN DEVICE_POWER_STATE DevicePowerState,
    IN POWER_ACTION ActionType
    )
{
    RT_NOREF(MiniportDeviceContext, DeviceUid, DevicePowerState, ActionType);
    /* The DxgkDdiSetPowerState function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiNotifyAcpiEvent(
    IN CONST PVOID  MiniportDeviceContext,
    IN DXGK_EVENT_TYPE  EventType,
    IN ULONG  Event,
    IN PVOID  Argument,
    OUT PULONG  AcpiFlags
    )
{
    RT_NOREF(MiniportDeviceContext, EventType, Event, Argument, AcpiFlags);
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    vboxVDbgBreakF();

    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

VOID DxgkDdiResetDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    RT_NOREF(MiniportDeviceContext);
    /* DxgkDdiResetDevice can be called at any IRQL, so it must be in nonpageable memory.  */
    vboxVDbgBreakF();



    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));
    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));
}

VOID DxgkDdiUnload(
    VOID
    )
{
    /* DxgkDdiUnload should be made pageable. */
    PAGED_CODE();
    LOGF((": unloading"));

    vboxVDbgBreakFv();

    VbglR0TerminateClient();

    PRTLOGGER pLogger = RTLogRelSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }
    pLogger = RTLogSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }

    RTR0Term();
}

NTSTATUS DxgkDdiQueryInterface(
    IN CONST PVOID MiniportDeviceContext,
    IN PQUERY_INTERFACE QueryInterface
    )
{
    RT_NOREF(MiniportDeviceContext, QueryInterface);
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    vboxVDbgBreakFv();

    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    return STATUS_NOT_SUPPORTED;
}

VOID DxgkDdiControlEtwLogging(
    IN BOOLEAN  Enable,
    IN ULONG  Flags,
    IN UCHAR  Level
    )
{
    RT_NOREF(Enable, Flags, Level);
    LOGF(("ENTER"));

    vboxVDbgBreakF();

    LOGF(("LEAVE"));
}

#ifdef VBOX_WITH_VMSVGA3D_DX
typedef struct VBOXDXSEGMENTDESCRIPTOR
{
    DXGK_SEGMENTFLAGS Flags;
    PHYSICAL_ADDRESS  CpuTranslatedAddress;
    SIZE_T            Size;
} VBOXDXSEGMENTDESCRIPTOR;

#define VBOXDX_SEGMENTS_COUNT 3

static void vmsvgaDXGetSegmentDescription(PVBOXMP_DEVEXT pDevExt, int idxSegment, VBOXDXSEGMENTDESCRIPTOR *pDesc)
{
    /** @todo 2 segments for pDevExt->fLegacy flag. */
    /* 3 segments:
     * 1: The usual VRAM, CpuVisible;
     * 2: Aperture segment for guest backed objects;
     * 3: Host resources, CPU invisible.
     */
    RT_ZERO(*pDesc);
    if (idxSegment == 0)
    {
        pDesc->CpuTranslatedAddress = VBoxCommonFromDeviceExt(pDevExt)->phVRAM;
        pDesc->Size                 = pDevExt->cbVRAMCpuVisible & X86_PAGE_4K_BASE_MASK;
        pDesc->Flags.CpuVisible     = 1;
    }
    else if (idxSegment == 1)
    {
        pDesc->Size                 = _2G; /** @todo */
        pDesc->Flags.CpuVisible     = 1;
        pDesc->Flags.Aperture       = 1;
    }
    else if (idxSegment == 2)
    {
        pDesc->Size                 = _2G; /** @todo */
    }
}
#endif

/**
 * DxgkDdiQueryAdapterInfo
 */
NTSTATUS APIENTRY DxgkDdiQueryAdapterInfo(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO*  pQueryAdapterInfo)
{
    /* The DxgkDdiQueryAdapterInfo should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x), Query type (%d)", hAdapter, pQueryAdapterInfo->Type));
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    vboxVDbgBreakFv();

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            DXGK_DRIVERCAPS *pCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;
            memset(pCaps, 0, pQueryAdapterInfo->OutputDataSize);

            pCaps->HighestAcceptableAddress.LowPart = ~0UL;
#ifdef RT_ARCH_AMD64
            /* driver talks to host in terms of page numbers when reffering to RAM
             * we use uint32_t field to pass page index to host, so max would be (~0UL) << PAGE_OFFSET,
             * which seems quite enough */
            pCaps->HighestAcceptableAddress.HighPart = PAGE_OFFSET_MASK;
#endif
            pCaps->MaxPointerWidth  = VBOXWDDM_C_POINTER_MAX_WIDTH;
            pCaps->MaxPointerHeight = VBOXWDDM_C_POINTER_MAX_HEIGHT;
#ifdef VBOX_WITH_VMSVGA
            if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
            {
                pCaps->MaxPointerWidth  = VBOXWDDM_C_POINTER_MAX_WIDTH_LEGACY;
                pCaps->MaxPointerHeight = VBOXWDDM_C_POINTER_MAX_HEIGHT_LEGACY;
            }
#endif
            pCaps->PointerCaps.Value = 3; /* Monochrome , Color*/ /* MaskedColor == Value | 4, disable for now */
            if (!g_VBoxDisplayOnly)
            {
                pCaps->MaxAllocationListSlotId = 16;
                pCaps->ApertureSegmentCommitLimit = 0;
                pCaps->InterruptMessageNumber = 0;
                pCaps->NumberOfSwizzlingRanges = 0;
                pCaps->MaxOverlays = 0;
#ifdef VBOX_WITH_VIDEOHWACCEL
                for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    if ( pDevExt->aSources[i].Vhwa.Settings.fFlags & VBOXVHWA_F_ENABLED)
                        pCaps->MaxOverlays += pDevExt->aSources[i].Vhwa.Settings.cOverlaysSupported;
                }
#endif
                pCaps->GammaRampCaps.Value = 0;
                pCaps->PresentationCaps.Value = 0;
                pCaps->PresentationCaps.NoScreenToScreenBlt = 1;
                pCaps->PresentationCaps.NoOverlapScreenBlt = 1;
                pCaps->PresentationCaps.AlignmentShift = 2;
                pCaps->PresentationCaps.MaxTextureWidthShift = 2; /* Up to 8196 */
                pCaps->PresentationCaps.MaxTextureHeightShift = 2; /* Up to 8196 */
                pCaps->MaxQueuedFlipOnVSync = 0; /* do we need it? */
                pCaps->FlipCaps.Value = 0;
                /* ? pCaps->FlipCaps.FlipOnVSyncWithNoWait = 1; */
                pCaps->SchedulingCaps.Value = 0;
                /* we might need it for Aero.
                 * Setting this flag means we support DeviceContext, i.e.
                 *  DxgkDdiCreateContext and DxgkDdiDestroyContext
                 */
                pCaps->SchedulingCaps.MultiEngineAware = 1;
                pCaps->MemoryManagementCaps.Value = 0;
                /** @todo this correlates with pCaps->SchedulingCaps.MultiEngineAware */
                pCaps->MemoryManagementCaps.PagingNode = 0;
                /** @todo this correlates with pCaps->SchedulingCaps.MultiEngineAware */
                pCaps->GpuEngineTopology.NbAsymetricProcessingNodes = VBOXWDDM_NUM_NODES;
#ifdef VBOX_WITH_VMSVGA
                if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
                {
                    /* The Gallium node has NodeOrdinal == 0, because:
                     *   GDI context is created with it;
                     *   we generate commands for the context;
                     *   there seems to be no easy way to distinguish which node a fence was completed for.
                     *
                     * GDI context is used for example for copying between D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE
                     * and D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE.
                     */
                    pCaps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
                }
#endif

                if (VBoxQueryWinVersion(NULL) >= WINVERSION_8)
                    pCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            }
            else
            {
                pCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            }
            break;
        }
        case DXGKQAITYPE_QUERYSEGMENT:
        {
            if (!g_VBoxDisplayOnly)
            {
#ifdef VBOX_WITH_VMSVGA3D_DX
                if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA && SvgaIsDXSupported(pDevExt))
                {
                    DXGK_QUERYSEGMENTOUT *pOut = (DXGK_QUERYSEGMENTOUT *)pQueryAdapterInfo->pOutputData;
                    if (!pOut->pSegmentDescriptor)
                        pOut->NbSegment = VBOXDX_SEGMENTS_COUNT; /* Return the number of segments. */
                    else if (pOut->NbSegment == VBOXDX_SEGMENTS_COUNT)
                    {
                        DXGK_SEGMENTDESCRIPTOR *paDesc = pOut->pSegmentDescriptor;
                        for (unsigned i = 0; i < VBOXDX_SEGMENTS_COUNT; ++i)
                        {
                            VBOXDXSEGMENTDESCRIPTOR desc;
                            vmsvgaDXGetSegmentDescription(pDevExt, i, &desc);
                            paDesc[i].CpuTranslatedAddress = desc.CpuTranslatedAddress;
                            paDesc[i].Size                 = desc.Size;
                            paDesc[i].CommitLimit          = desc.Size;
                            paDesc[i].Flags                = desc.Flags;
                        }

                        pOut->PagingBufferSegmentId       = 0;
                        pOut->PagingBufferSize            = PAGE_SIZE;
                        pOut->PagingBufferPrivateDataSize = PAGE_SIZE;
                    }
                    else
                    {
                        WARN(("NbSegment %d", pOut->NbSegment));
                        Status = STATUS_INVALID_PARAMETER;
                    }
                    break;
                }
#endif
                /* no need for DXGK_QUERYSEGMENTIN as it contains AGP aperture info, which (AGP aperture) we do not support
                 * DXGK_QUERYSEGMENTIN *pQsIn = (DXGK_QUERYSEGMENTIN*)pQueryAdapterInfo->pInputData; */
                DXGK_QUERYSEGMENTOUT *pQsOut = (DXGK_QUERYSEGMENTOUT*)pQueryAdapterInfo->pOutputData;
# define VBOXWDDM_SEGMENTS_COUNT 2
                if (!pQsOut->pSegmentDescriptor)
                {
                    /* we are requested to provide the number of segments we support */
                    pQsOut->NbSegment = VBOXWDDM_SEGMENTS_COUNT;
                }
                else if (pQsOut->NbSegment != VBOXWDDM_SEGMENTS_COUNT)
                {
                    WARN(("NbSegment (%d) != 1", pQsOut->NbSegment));
                    Status = STATUS_INVALID_PARAMETER;
                }
                else
                {
                    DXGK_SEGMENTDESCRIPTOR* pDr = pQsOut->pSegmentDescriptor;
                    /* we are requested to provide segment information */
                    pDr->BaseAddress.QuadPart = 0;
                    pDr->CpuTranslatedAddress = VBoxCommonFromDeviceExt(pDevExt)->phVRAM;
                    /* make sure the size is page aligned */
                    /** @todo need to setup VBVA buffers and adjust the mem size here */
                    pDr->Size = vboxWddmVramCpuVisibleSegmentSize(pDevExt);
                    pDr->NbOfBanks = 0;
                    pDr->pBankRangeTable = 0;
                    pDr->CommitLimit = pDr->Size;
                    pDr->Flags.Value = 0;
                    pDr->Flags.CpuVisible = 1;

                    ++pDr;
                    /* create cpu-invisible segment of the same size */
                    pDr->BaseAddress.QuadPart = 0;
                    pDr->CpuTranslatedAddress.QuadPart = 0;
                    /* make sure the size is page aligned */
                    /** @todo need to setup VBVA buffers and adjust the mem size here */
                    pDr->Size = vboxWddmVramCpuInvisibleSegmentSize(pDevExt);
                    pDr->NbOfBanks = 0;
                    pDr->pBankRangeTable = 0;
                    pDr->CommitLimit = pDr->Size;
                    pDr->Flags.Value = 0;

                    pQsOut->PagingBufferSegmentId = 0;
                    pQsOut->PagingBufferSize = PAGE_SIZE;
                    pQsOut->PagingBufferPrivateDataSize = PAGE_SIZE;
                }
            }
            else
            {
                WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
                Status = STATUS_NOT_SUPPORTED;
            }

            break;
        }
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (!g_VBoxDisplayOnly)
            {
                if (pQueryAdapterInfo->OutputDataSize >= sizeof(VBOXWDDM_QAI))
                {
                    VBOXWDDM_QAI *pQAI = (VBOXWDDM_QAI *)pQueryAdapterInfo->pOutputData;
                    memset(pQAI, 0, sizeof(VBOXWDDM_QAI));

                    pQAI->u32Version = VBOXVIDEOIF_VERSION;
                    pQAI->enmHwType = pDevExt->enmHwType;
                    pQAI->u32AdapterCaps = pDevExt->f3DEnabled ? VBOXWDDM_QAI_CAP_3D : 0;
                    pQAI->u32AdapterCaps |= VBOXWDDM_QAI_CAP_DXVA; /** @todo Fetch from registry. */
                    if (VBoxQueryWinVersion(NULL) >= WINVERSION_7)
                    {
                        pQAI->u32AdapterCaps |= VBOXWDDM_QAI_CAP_WIN7;
                        // pQAI->u32AdapterCaps |= VBOXWDDM_QAI_CAP_DXVAHD; /** @todo Fetch from registry. */
                    }

                    static int cLoggedCaps = 0;
                    if (cLoggedCaps < 1)
                    {
                        ++cLoggedCaps;
                        LOGREL_EXACT(("WDDM: adapter capabilities 0x%08X\n", pQAI->u32AdapterCaps));
                    }

                    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
                    {
                        pQAI->u.vbox.u32VBox3DCaps = 0;
                    }
#ifdef VBOX_WITH_VMSVGA
                    else if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
                        GaQueryInfo(pDevExt->pGa, pDevExt->enmHwType, &pQAI->u.vmsvga.HWInfo);
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
                    pQAI->cInfos = VBoxCommonFromDeviceExt(pDevExt)->cDisplays;
                    for (uint32_t i = 0; i < pQAI->cInfos; ++i)
                    {
                        pQAI->aInfos[i] = pDevExt->aSources[i].Vhwa.Settings;
                    }
#endif
                }
                else
                {
                    WARN(("incorrect buffer size %d, expected %d", pQueryAdapterInfo->OutputDataSize, sizeof(VBOXWDDM_QAI)));
                    Status = STATUS_BUFFER_TOO_SMALL;
                }
            }
            else
            {
                WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
                Status = STATUS_NOT_SUPPORTED;
            }
            break;

        case DXGKQAITYPE_QUERYSEGMENT3:
#ifdef VBOX_WITH_VMSVGA3D_DX
            if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA && SvgaIsDXSupported(pDevExt))
            {
                DXGK_QUERYSEGMENTOUT3 *pOut = (DXGK_QUERYSEGMENTOUT3 *)pQueryAdapterInfo->pOutputData;
                if (!pOut->pSegmentDescriptor)
                    pOut->NbSegment = VBOXDX_SEGMENTS_COUNT; /* Return the number of segments. */
                else if (pOut->NbSegment == VBOXDX_SEGMENTS_COUNT)
                {
                    DXGK_SEGMENTDESCRIPTOR3 *paDesc = pOut->pSegmentDescriptor;
                    for (unsigned i = 0; i < VBOXDX_SEGMENTS_COUNT; ++i)
                    {
                        VBOXDXSEGMENTDESCRIPTOR desc;
                        vmsvgaDXGetSegmentDescription(pDevExt, i, &desc);
                        paDesc[i].Flags                = desc.Flags;
                        paDesc[i].CpuTranslatedAddress = desc.CpuTranslatedAddress;
                        paDesc[i].Size                 = desc.Size;
                        paDesc[i].CommitLimit          = desc.Size;
                    }

                    pOut->PagingBufferSegmentId       = 0;
                    pOut->PagingBufferSize            = PAGE_SIZE;
                    pOut->PagingBufferPrivateDataSize = PAGE_SIZE;
                }
                else
                {
                    WARN(("NbSegment %d", pOut->NbSegment));
                    Status = STATUS_INVALID_PARAMETER;
                }
                break;
            }
#endif
            LOGREL(("DXGKQAITYPE_QUERYSEGMENT3 treating as unsupported!"));
            Status = STATUS_NOT_SUPPORTED;
            break;

        default:
            WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    LOGF(("LEAVE, context(0x%x), Status(0x%x)", hAdapter, Status));
    return Status;
}

/**
 * DxgkDdiCreateDevice
 */
NTSTATUS APIENTRY DxgkDdiCreateDevice(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEDEVICE*  pCreateDevice)
{
    /* DxgkDdiCreateDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    vboxVDbgBreakFv();

    PVBOXWDDM_DEVICE pDevice = (PVBOXWDDM_DEVICE)vboxWddmMemAllocZero(sizeof (VBOXWDDM_DEVICE));
    if (!pDevice)
    {
        WARN(("vboxWddmMemAllocZero failed for WDDM device structure"));
        return STATUS_NO_MEMORY;
    }

    pDevice->pAdapter = pDevExt;
    pDevice->hDevice = pCreateDevice->hDevice;

    pCreateDevice->hDevice = pDevice;
    if (pCreateDevice->Flags.SystemDevice)
        pDevice->enmType = VBOXWDDM_DEVICE_TYPE_SYSTEM;

    pCreateDevice->pInfo = NULL;

#ifdef VBOX_WITH_VMSVGA
     if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
     {
         Status = GaDeviceCreate(pDevExt->pGa, pDevice);
         if (Status != STATUS_SUCCESS)
         {
             vboxWddmMemFree(pDevice);
         }
     }
#endif

    LOGF(("LEAVE, context(0x%x), Status(0x%x)", hAdapter, Status));

    return Status;
}

PVBOXWDDM_RESOURCE vboxWddmResourceCreate(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_RCINFO pRcInfo)
{
    RT_NOREF(pDevExt);
    PVBOXWDDM_RESOURCE pResource = (PVBOXWDDM_RESOURCE)vboxWddmMemAllocZero(RT_UOFFSETOF_DYN(VBOXWDDM_RESOURCE,
                                                                                             aAllocations[pRcInfo->cAllocInfos]));
    if (!pResource)
    {
        AssertFailed();
        return NULL;
    }
    pResource->cRefs = 1;
    pResource->cAllocations = pRcInfo->cAllocInfos;
    pResource->fFlags = pRcInfo->fFlags;
    pResource->RcDesc = pRcInfo->RcDesc;
    return pResource;
}

VOID vboxWddmResourceRetain(PVBOXWDDM_RESOURCE pResource)
{
    ASMAtomicIncU32(&pResource->cRefs);
}

static VOID vboxWddmResourceDestroy(PVBOXWDDM_RESOURCE pResource)
{
    vboxWddmMemFree(pResource);
}

VOID vboxWddmResourceWaitDereference(PVBOXWDDM_RESOURCE pResource)
{
    vboxWddmCounterU32Wait(&pResource->cRefs, 1);
}

VOID vboxWddmResourceRelease(PVBOXWDDM_RESOURCE pResource)
{
    uint32_t cRefs = ASMAtomicDecU32(&pResource->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        vboxWddmResourceDestroy(pResource);
    }
}

void vboxWddmAllocationDeleteFromResource(PVBOXWDDM_RESOURCE pResource, PVBOXWDDM_ALLOCATION pAllocation)
{
    Assert(pAllocation->pResource == pResource);
    if (pResource)
    {
        Assert(&pResource->aAllocations[pAllocation->iIndex] == pAllocation);
        vboxWddmResourceRelease(pResource);
    }
    else
    {
        vboxWddmMemFree(pAllocation);
    }
}

VOID vboxWddmAllocationCleanupAssignment(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation)
{
    switch (pAllocation->enmType)
    {
        case VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
        case VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
        {
            if (pAllocation->bAssigned)
            {
                /** @todo do we need to notify host? */
                vboxWddmAssignPrimary(&pDevExt->aSources[pAllocation->AllocData.SurfDesc.VidPnSourceId], NULL, pAllocation->AllocData.SurfDesc.VidPnSourceId);
            }
            break;
        }
        default:
            break;
    }
}

VOID vboxWddmAllocationCleanup(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_ALLOCATION pAllocation)
{
    RT_NOREF(pDevExt);
    switch (pAllocation->enmType)
    {
        case VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
        case VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
        {
#if 0
            if (pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC)
            {
                if (pAllocation->hSharedHandle)
                {
                    vboxShRcTreeRemove(pDevExt, pAllocation);
                }
            }
#endif
            break;
        }
        case VBOXWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER:
        {
            break;
        }
        default:
            break;
    }
}

VOID vboxWddmAllocationDestroy(PVBOXWDDM_ALLOCATION pAllocation)
{
    PAGED_CODE();

    vboxWddmAllocationDeleteFromResource(pAllocation->pResource, pAllocation);
}

PVBOXWDDM_ALLOCATION vboxWddmAllocationCreateFromResource(PVBOXWDDM_RESOURCE pResource, uint32_t iIndex)
{
    PVBOXWDDM_ALLOCATION pAllocation = NULL;
    if (pResource)
    {
        Assert(iIndex < pResource->cAllocations);
        if (iIndex < pResource->cAllocations)
        {
            pAllocation = &pResource->aAllocations[iIndex];
            memset(pAllocation, 0, sizeof (VBOXWDDM_ALLOCATION));
        }
        vboxWddmResourceRetain(pResource);
    }
    else
        pAllocation = (PVBOXWDDM_ALLOCATION)vboxWddmMemAllocZero(sizeof (VBOXWDDM_ALLOCATION));

    if (pAllocation)
    {
        if (pResource)
        {
            pAllocation->pResource = pResource;
            pAllocation->iIndex = iIndex;
        }
    }

    return pAllocation;
}

NTSTATUS vboxWddmAllocationCreate(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_RESOURCE pResource, uint32_t iIndex, DXGK_ALLOCATIONINFO* pAllocationInfo)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    Assert(pAllocationInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_ALLOCINFO));
    if (pAllocationInfo->PrivateDriverDataSize >= sizeof (VBOXWDDM_ALLOCINFO))
    {
        PVBOXWDDM_ALLOCINFO pAllocInfo = (PVBOXWDDM_ALLOCINFO)pAllocationInfo->pPrivateDriverData;
        PVBOXWDDM_ALLOCATION pAllocation = vboxWddmAllocationCreateFromResource(pResource, iIndex);
        Assert(pAllocation);
        if (pAllocation)
        {
            pAllocationInfo->pPrivateDriverData = NULL;
            pAllocationInfo->PrivateDriverDataSize = 0;
            pAllocationInfo->Alignment = 0;
            pAllocationInfo->PitchAlignedSize = 0;
            pAllocationInfo->HintedBank.Value = 0;
            pAllocationInfo->PreferredSegment.Value = 0;
            pAllocationInfo->SupportedReadSegmentSet = 1;
            pAllocationInfo->SupportedWriteSegmentSet = 1;
            pAllocationInfo->EvictionSegmentSet = 0;
            pAllocationInfo->MaximumRenamingListLength = 0;
            pAllocationInfo->hAllocation = pAllocation;
            pAllocationInfo->Flags.Value = 0;
            pAllocationInfo->pAllocationUsageHint = NULL;
            pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;

            pAllocation->enmType = pAllocInfo->enmType;
            pAllocation->AllocData.Addr.SegmentId = 0;
            pAllocation->AllocData.Addr.offVram = VBOXVIDEOOFFSET_VOID;
            pAllocation->bVisible = FALSE;
            pAllocation->bAssigned = FALSE;
            KeInitializeSpinLock(&pAllocation->OpenLock);
            InitializeListHead(&pAllocation->OpenList);
            pAllocation->CurVidPnSourceId = -1;

            switch (pAllocInfo->enmType)
            {
                case VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
                case VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
                case VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE:
                case VBOXWDDM_ALLOC_TYPE_STD_STAGINGSURFACE:
                {
                    pAllocation->fRcFlags = pAllocInfo->fFlags;
                    pAllocation->AllocData.SurfDesc = pAllocInfo->SurfDesc;
                    pAllocation->AllocData.hostID = pAllocInfo->hostID;

                    pAllocationInfo->Size = pAllocInfo->SurfDesc.cbSize;

                    switch (pAllocInfo->enmType)
                    {
                        case VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
                            if (SvgaIsDXSupported(pDevExt))
                            {
                                pAllocationInfo->PreferredSegment.Value      = 0;
                                pAllocationInfo->SupportedReadSegmentSet     = 1; /* VRAM */
                                pAllocationInfo->SupportedWriteSegmentSet    = 1; /* VRAM */
                                /// @todo Required?  pAllocationInfo->Flags.CpuVisible = 1;
                            }
                            break;
                        case VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
#ifdef VBOX_WITH_VIDEOHWACCEL
                            if (pAllocInfo->fFlags.Overlay)
                            {
                                /* actually we can not "properly" issue create overlay commands to the host here
                                 * because we do not know source VidPn id here, i.e.
                                 * the primary which is supposed to be overlayed,
                                 * however we need to get some info like pitch & size from the host here */
                                int rc = vboxVhwaHlpGetSurfInfo(pDevExt, pAllocation);
                                AssertRC(rc);
                                if (RT_SUCCESS(rc))
                                {
                                    pAllocationInfo->Flags.Overlay = 1;
                                    pAllocationInfo->Flags.CpuVisible = 1;
                                    pAllocationInfo->Size = pAllocation->AllocData.SurfDesc.cbSize;

                                    pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_HIGH;
                                }
                                else
                                    Status = STATUS_UNSUCCESSFUL;
                            }
                            else
#endif
                            {
                                RT_NOREF(pDevExt);

                                Assert(pAllocation->AllocData.SurfDesc.bpp);
                                Assert(pAllocation->AllocData.SurfDesc.pitch);
                                Assert(pAllocation->AllocData.SurfDesc.cbSize);

                                /*
                                 * Mark the allocation as visible to the CPU so we can
                                 * lock it in the user mode driver for SYSTEM pool allocations.
                                 * See @bugref{8040} for further information.
                                 */
                                if (!pAllocInfo->fFlags.SharedResource && !pAllocInfo->hostID)
                                    pAllocationInfo->Flags.CpuVisible = 1;

                                if (pAllocInfo->fFlags.SharedResource)
                                {
                                    pAllocation->hSharedHandle = (HANDLE)pAllocInfo->hSharedHandle;
#if 0
                                    if (pAllocation->hSharedHandle)
                                    {
                                        vboxShRcTreePut(pDevExt, pAllocation);
                                    }
#endif
                                }

#if 0
                                /* Allocation from the CPU invisible second segment does not
                                 * work apparently and actually fails on Vista.
                                 *
                                 * @todo Find out what exactly is wrong.
                                 */
//                                if (pAllocInfo->hostID)
                                {
                                    pAllocationInfo->SupportedReadSegmentSet = 2;
                                    pAllocationInfo->SupportedWriteSegmentSet = 2;
                                }
#endif
                            }
                            break;
                        case VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE:
                        case VBOXWDDM_ALLOC_TYPE_STD_STAGINGSURFACE:
                            if (SvgaIsDXSupported(pDevExt))
                            {
                                pAllocationInfo->PreferredSegment.Value      = 0;
                                pAllocationInfo->SupportedReadSegmentSet     = 1; /* VRAM */
                                pAllocationInfo->SupportedWriteSegmentSet    = 1; /* VRAM */
                            }
                            pAllocationInfo->Flags.CpuVisible = 1;
                            break;
                        default: AssertFailedBreak(); /* Shut up MSC.*/
                    }

                    if (Status == STATUS_SUCCESS)
                    {
                        pAllocation->UsageHint.Version = 0;
                        pAllocation->UsageHint.v1.Flags.Value = 0;
                        pAllocation->UsageHint.v1.Format = pAllocInfo->SurfDesc.format;
                        pAllocation->UsageHint.v1.SwizzledFormat = 0;
                        pAllocation->UsageHint.v1.ByteOffset = 0;
                        pAllocation->UsageHint.v1.Width = pAllocation->AllocData.SurfDesc.width;
                        pAllocation->UsageHint.v1.Height = pAllocation->AllocData.SurfDesc.height;
                        pAllocation->UsageHint.v1.Pitch = pAllocation->AllocData.SurfDesc.pitch;
                        pAllocation->UsageHint.v1.Depth = 0;
                        pAllocation->UsageHint.v1.SlicePitch = 0;

                        Assert(!pAllocationInfo->pAllocationUsageHint);
                        pAllocationInfo->pAllocationUsageHint = &pAllocation->UsageHint;
                    }

                    break;
                }
                case VBOXWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER:
                {
                    pAllocationInfo->Size = pAllocInfo->cbBuffer;
                    pAllocation->fUhgsmiType = pAllocInfo->fUhgsmiType;
                    pAllocation->AllocData.SurfDesc.cbSize = pAllocInfo->cbBuffer;
                    pAllocationInfo->Flags.CpuVisible = 1;
//                    pAllocationInfo->Flags.SynchronousPaging = 1;
                    pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_MAXIMUM;
                    break;
                }

                default:
                    LOGREL(("ERROR: invalid alloc info type(%d)", pAllocInfo->enmType));
                    AssertBreakpoint();
                    Status = STATUS_INVALID_PARAMETER;
                    break;

            }

            if (Status != STATUS_SUCCESS)
                vboxWddmAllocationDeleteFromResource(pResource, pAllocation);
        }
        else
        {
            LOGREL(("ERROR: failed to create allocation description"));
            Status = STATUS_NO_MEMORY;
        }

    }
    else
    {
        LOGREL(("ERROR: PrivateDriverDataSize(%d) less than header size(%d)", pAllocationInfo->PrivateDriverDataSize, sizeof (VBOXWDDM_ALLOCINFO)));
        Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

NTSTATUS APIENTRY DxgkDdiCreateAllocation(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEALLOCATION*  pCreateAllocation)
{
    /* DxgkDdiCreateAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

#ifdef VBOX_WITH_VMSVGA3D_DX
    /* The driver distinguished between the legacy and the new D3D(DX) requests by checking the size. */
    AssertCompile(sizeof(VBOXDXALLOCATIONDESC) != sizeof(VBOXWDDM_ALLOCINFO));

    /* Check if this is a request from the new D3D driver. */
    if (   pCreateAllocation->PrivateDriverDataSize == 0
        && pCreateAllocation->NumAllocations == 1
        && pCreateAllocation->pAllocationInfo[0].PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC))
        return DxgkDdiDXCreateAllocation(hAdapter, pCreateAllocation);
#endif

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_RESOURCE pResource = NULL;

    if (pCreateAllocation->PrivateDriverDataSize)
    {
        Assert(pCreateAllocation->PrivateDriverDataSize == sizeof (VBOXWDDM_RCINFO));
        Assert(pCreateAllocation->pPrivateDriverData);
        if (pCreateAllocation->PrivateDriverDataSize < sizeof (VBOXWDDM_RCINFO))
        {
            WARN(("invalid private data size (%d)", pCreateAllocation->PrivateDriverDataSize));
            return STATUS_INVALID_PARAMETER;
        }

        PVBOXWDDM_RCINFO pRcInfo = (PVBOXWDDM_RCINFO)pCreateAllocation->pPrivateDriverData;
//            Assert(pRcInfo->RcDesc.VidPnSourceId < VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
        if (pRcInfo->cAllocInfos != pCreateAllocation->NumAllocations)
        {
            WARN(("invalid number of allocations passed in, (%d), expected (%d)", pRcInfo->cAllocInfos, pCreateAllocation->NumAllocations));
            return STATUS_INVALID_PARAMETER;
        }

        /* a check to ensure we do not get the allocation size which is too big to overflow the 32bit value */
        if (VBOXWDDM_TRAILARRAY_MAXELEMENTSU32(VBOXWDDM_RESOURCE, aAllocations) < pRcInfo->cAllocInfos)
        {
            WARN(("number of allocations passed too big (%d), max is (%d)", pRcInfo->cAllocInfos, VBOXWDDM_TRAILARRAY_MAXELEMENTSU32(VBOXWDDM_RESOURCE, aAllocations)));
            return STATUS_INVALID_PARAMETER;
        }

        pResource = (PVBOXWDDM_RESOURCE)vboxWddmMemAllocZero(RT_UOFFSETOF_DYN(VBOXWDDM_RESOURCE, aAllocations[pRcInfo->cAllocInfos]));
        if (!pResource)
        {
            WARN(("vboxWddmMemAllocZero failed for (%d) allocations", pRcInfo->cAllocInfos));
            return STATUS_NO_MEMORY;
        }

        pResource->cRefs = 1;
        pResource->cAllocations = pRcInfo->cAllocInfos;
        pResource->fFlags = pRcInfo->fFlags;
        pResource->RcDesc = pRcInfo->RcDesc;
    }


    for (UINT i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        Status = vboxWddmAllocationCreate(pDevExt, pResource, i, &pCreateAllocation->pAllocationInfo[i]);
        if (Status != STATUS_SUCCESS)
        {
            WARN(("vboxWddmAllocationCreate(%d) failed, Status(0x%x)", i, Status));
            /* note: i-th allocation is expected to be cleared in a fail handling code above */
            for (UINT j = 0; j < i; ++j)
            {
                PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pCreateAllocation->pAllocationInfo[j].hAllocation;
                vboxWddmAllocationCleanup(pDevExt, pAllocation);
                vboxWddmAllocationDestroy(pAllocation);
            }
            break;
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        pCreateAllocation->hResource = pResource;
    }
    else
    {
        if (pResource)
            vboxWddmResourceRelease(pResource);
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyAllocation(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_DESTROYALLOCATION*  pDestroyAllocation)
{
    /* DxgkDdiDestroyAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    for (UINT i = 0; i < pDestroyAllocation->NumAllocations; ++i)
    {
        PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pDestroyAllocation->pAllocationList[0];
        if (pAllocation->CurVidPnSourceId != -1)
        {
            VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[pAllocation->CurVidPnSourceId];
            vboxWddmAssignPrimary(pSource, NULL, pAllocation->CurVidPnSourceId);
        }
    }

#ifdef VBOX_WITH_VMSVGA3D_DX
    /* Check if this is a request from the D3D driver. */
    if (pDestroyAllocation->NumAllocations >= 1)
    {
        PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pDestroyAllocation->pAllocationList[0];
        if (pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_D3D)
            return DxgkDdiDXDestroyAllocation(hAdapter, pDestroyAllocation);
    }
#endif

    NTSTATUS Status = STATUS_SUCCESS;

    PVBOXWDDM_RESOURCE pRc = (PVBOXWDDM_RESOURCE)pDestroyAllocation->hResource;
    if (pRc)
    {
        Assert(pRc->cAllocations == pDestroyAllocation->NumAllocations);
    }

    for (UINT i = 0; i < pDestroyAllocation->NumAllocations; ++i)
    {
        PVBOXWDDM_ALLOCATION pAlloc = (PVBOXWDDM_ALLOCATION)pDestroyAllocation->pAllocationList[i];
        Assert(pAlloc->pResource == pRc);
        vboxWddmAllocationCleanupAssignment(pDevExt, pAlloc);
        /* wait for all current allocation-related ops are completed */
        vboxWddmAllocationCleanup(pDevExt, pAlloc);
        vboxWddmAllocationDestroy(pAlloc);
    }

    if (pRc)
    {
        /* wait for all current resource-related ops are completed */
        vboxWddmResourceWaitDereference(pRc);
        vboxWddmResourceRelease(pRc);
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

/**
 * DxgkDdiDescribeAllocation
 */
NTSTATUS
APIENTRY
DxgkDdiDescribeAllocation(
    CONST HANDLE  hAdapter,
    DXGKARG_DESCRIBEALLOCATION*  pDescribeAllocation)
{
    RT_NOREF(hAdapter);
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXWDDM_ALLOCATION pAllocation = (PVBOXWDDM_ALLOCATION)pDescribeAllocation->hAllocation;
#ifdef VBOX_WITH_VMSVGA3D_DX
    /* Check if this is a request from the D3D driver. */
    if (pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_D3D)
        return DxgkDdiDXDescribeAllocation(hAdapter, pDescribeAllocation);
#endif
    pDescribeAllocation->Width = pAllocation->AllocData.SurfDesc.width;
    pDescribeAllocation->Height = pAllocation->AllocData.SurfDesc.height;
    pDescribeAllocation->Format = pAllocation->AllocData.SurfDesc.format;
    memset (&pDescribeAllocation->MultisampleMethod, 0, sizeof (pDescribeAllocation->MultisampleMethod));
    pDescribeAllocation->RefreshRate.Numerator = g_RefreshRate * 1000;
    pDescribeAllocation->RefreshRate.Denominator = 1000;
    pDescribeAllocation->PrivateDriverFormatAttribute = 0;

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

/**
 * DxgkDdiGetStandardAllocationDriverData
 */
NTSTATUS
APIENTRY
DxgkDdiGetStandardAllocationDriverData(
    CONST HANDLE  hAdapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA*  pGetStandardAllocationDriverData)
{
    RT_NOREF(hAdapter);
    /* DxgkDdiGetStandardAllocationDriverData should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_ALLOCINFO pAllocInfo = NULL;

    switch (pGetStandardAllocationDriverData->StandardAllocationType)
    {
        case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE"));
            if(pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
            {
                pAllocInfo = (PVBOXWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                memset (pAllocInfo, 0, sizeof (VBOXWDDM_ALLOCINFO));
                pAllocInfo->enmType = VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE;
                pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Width;
                pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Height;
                pAllocInfo->SurfDesc.format = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Format;
                pAllocInfo->SurfDesc.bpp = vboxWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.pitch = vboxWddmCalcPitch(pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Width, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.cbSize = vboxWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.depth = 0;
                pAllocInfo->SurfDesc.slicePitch = 0;
                pAllocInfo->SurfDesc.RefreshRate = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->RefreshRate;
                pAllocInfo->SurfDesc.VidPnSourceId = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->VidPnSourceId;
            }
            pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (VBOXWDDM_ALLOCINFO);

            pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            break;
        }
        case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE"));
            UINT bpp = vboxWddmCalcBitsPerPixel(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format);
            Assert(bpp);
            if (bpp != 0)
            {
                UINT Pitch = vboxWddmCalcPitch(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width, pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format);
                pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Pitch = Pitch;

                /** @todo need [d/q]word align?? */

                if (pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
                {
                    pAllocInfo = (PVBOXWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                    pAllocInfo->enmType = VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE;
                    pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width;
                    pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Height;
                    pAllocInfo->SurfDesc.format = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format;
                    pAllocInfo->SurfDesc.bpp = vboxWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.pitch = vboxWddmCalcPitch(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width, pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.cbSize = vboxWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.depth = 0;
                    pAllocInfo->SurfDesc.slicePitch = 0;
                    pAllocInfo->SurfDesc.RefreshRate.Numerator = 0;
                    pAllocInfo->SurfDesc.RefreshRate.Denominator = 1000;
                    pAllocInfo->SurfDesc.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

                    pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Pitch = pAllocInfo->SurfDesc.pitch;
                }
                pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (VBOXWDDM_ALLOCINFO);

                pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            }
            else
            {
                LOGREL(("Invalid format (%d)", pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format));
                Status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE"));
            if(pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
            {
                pAllocInfo = (PVBOXWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                pAllocInfo->enmType = VBOXWDDM_ALLOC_TYPE_STD_STAGINGSURFACE;
                pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Width;
                pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Height;
                pAllocInfo->SurfDesc.format = D3DDDIFMT_X8R8G8B8; /* staging has always always D3DDDIFMT_X8R8G8B8 */
                pAllocInfo->SurfDesc.bpp = vboxWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.pitch = vboxWddmCalcPitch(pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Width, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.cbSize = vboxWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.depth = 0;
                pAllocInfo->SurfDesc.slicePitch = 0;
                pAllocInfo->SurfDesc.RefreshRate.Numerator = 0;
                pAllocInfo->SurfDesc.RefreshRate.Denominator = 1000;
                pAllocInfo->SurfDesc.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

                pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Pitch = pAllocInfo->SurfDesc.pitch;
            }
            pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (VBOXWDDM_ALLOCINFO);

            pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            break;
        }
//#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
//        case D3DKMDT_STANDARDALLOCATION_GDISURFACE:
//# error port to Win7 DDI
//              break;
//#endif
        default:
            LOGREL(("Invalid allocation type (%d)", pGetStandardAllocationDriverData->StandardAllocationType));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiAcquireSwizzlingRange(
    CONST HANDLE  hAdapter,
    DXGKARG_ACQUIRESWIZZLINGRANGE*  pAcquireSwizzlingRange)
{
    RT_NOREF(hAdapter, pAcquireSwizzlingRange);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiReleaseSwizzlingRange(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RELEASESWIZZLINGRANGE*  pReleaseSwizzlingRange)
{
    RT_NOREF(hAdapter, pReleaseSwizzlingRange);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

typedef struct VBOXWDDM_CALL_ISR
{
    PVBOXMP_DEVEXT pDevExt;
    ULONG MessageNumber;
} VBOXWDDM_CALL_ISR, *PVBOXWDDM_CALL_ISR;

static BOOLEAN vboxWddmCallIsrCb(PVOID Context)
{
    PVBOXWDDM_CALL_ISR pdc = (PVBOXWDDM_CALL_ISR)Context;
    PVBOXMP_DEVEXT pDevExt = pdc->pDevExt;
    if (pDevExt->fCmdVbvaEnabled)
    {
#ifdef DEBUG_sunlover
        /** @todo Remove VBOX_WITH_VIDEOHWACCEL code, because the host does not support it anymore. */
        AssertFailed(); /* Should not be here, because this is not used with 3D gallium driver. */
#endif
        return FALSE;
    }
    return DxgkDdiInterruptRoutineLegacy(pDevExt, pdc->MessageNumber);
}

NTSTATUS vboxWddmCallIsr(PVBOXMP_DEVEXT pDevExt)
{
    VBOXWDDM_CALL_ISR context;
    context.pDevExt = pDevExt;
    context.MessageNumber = 0;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            vboxWddmCallIsrCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    AssertNtStatusSuccess(Status);
    return Status;
}


NTSTATUS
APIENTRY
DxgkDdiSetPalette(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPALETTE*  pSetPalette
    )
{
    RT_NOREF(hAdapter, pSetPalette);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();
    /** @todo fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

/** Find which area of a 32 bit mouse pointer bitmap is actually used.
 * Zero pixels on the right and the bottom of the bitmap are considered unused.
 *
 * @param pPixels               The bitmap.
 * @param Pitch                 The bitmap scanline size in bytes.
 * @param Width                 The bitmap width.
 * @param Height                The bitmap height.
 * @param piMaxFilledPixel      Where to store the maximum index of non-zero pixel within a scanline.
 * @param piMaxFilledScanline   Where to store the zero based index of the last scanline with non-zero pixels.
 */
static void vboxWddmPointerFindDimensionsColor(void const *pPixels, UINT Pitch, UINT Width, UINT Height,
                                               LONG *piMaxFilledPixel, LONG *piMaxFilledScanline)
{
    /* Windows always uses the maximum pointer size VBOXWDDM_C_POINTER_MAX_*
     * Exclude zero pixels (which are transparent anyway) from the right and the bottom of the bitmap.
     */
    DWORD const *pdwScanline = (DWORD *)pPixels;
    LONG iMaxFilledScanline = -1;
    LONG iMaxFilledPixel = -1;
    for (ULONG y = 0; y < Height; ++y)
    {
        LONG iLastFilledPixel = -1;
        for (ULONG x = 0; x < Width; ++x)
        {
            if (pdwScanline[x])
                iLastFilledPixel = x;
        }

        iMaxFilledPixel = RT_MAX(iMaxFilledPixel, iLastFilledPixel);

        if (iLastFilledPixel >= 0)
        {
            /* Scanline contains non-zero pixels. */
            iMaxFilledScanline = y;
        }

        pdwScanline = (DWORD *)((uint8_t *)pdwScanline + Pitch);
    }

    *piMaxFilledPixel = iMaxFilledPixel;
    *piMaxFilledScanline = iMaxFilledScanline;
}

/** Find which area of a 1 bit AND/XOR mask bitmap is actually used, i.e. filled with actual data.
 * For the AND mask the bytes with a value 0xff on the right and the bottom of the bitmap are considered unused.
 * For the XOR mask the blank value is 0x00.
 *
 * @param pPixels             The 1bit bitmap.
 * @param Pitch               The 1bit bitmap scanline size in bytes.
 * @param Width               The bitmap width.
 * @param Height              The bitmap height.
 * @param Blank               The value of the unused bytes in the supplied bitmap.
 * @param piMaxFilledPixel    Where to store the maximum index of a filled pixel within a scanline.
 * @param piMaxFilledScanline Where to store the zero based index of the last scanline with filled pixels.
 */
static void vboxWddmPointerFindDimensionsMono(void const *pPixels, UINT Pitch, UINT Width, UINT Height, BYTE Blank,
                                              LONG *piMaxFilledPixel, LONG *piMaxFilledScanline)
{
    /* Windows always uses the maximum pointer size VBOXWDDM_C_POINTER_MAX_*
     * Exclude the blank pixels (which are transparent anyway) from the right and the bottom of the bitmap.
     */
    BYTE const *pbScanline = (BYTE *)pPixels;
    LONG iMaxFilledScanline = -1;
    LONG iMaxFilledByte = -1;
    for (ULONG y = 0; y < Height; ++y)
    {
        LONG iLastFilledByte = -1;
        for (ULONG x = 0; x < Width / 8; ++x)
        {
            if (pbScanline[x] != Blank)
                iLastFilledByte = x;
        }

        iMaxFilledByte = RT_MAX(iMaxFilledByte, iLastFilledByte);

        if (iLastFilledByte >= 0)
        {
            /* Scanline contains filled pixels. */
            iMaxFilledScanline = y;
        }

        pbScanline += Pitch;
    }

    *piMaxFilledPixel = iMaxFilledByte * 8;
    *piMaxFilledScanline = iMaxFilledScanline;
}

/** Adjust the width and the height of the mouse pointer bitmap.
 * See comments in the function for the adjustment criteria.
 *
 * @param iMaxX   The index of the rightmost pixel which we want to keep.
 * @param iMaxY   The index of the bottom-most pixel which we want to keep.
 * @param XHot    The mouse pointer hot spot.
 * @param YHot    The mouse pointer hot spot.
 * @param pWidth  Where to store the bitmap width.
 * @param pHeight Where to store the bitmap height.
 */
static void vboxWddmPointerAdjustDimensions(LONG iMaxX, LONG iMaxY, UINT XHot, UINT YHot,
                                            ULONG *pWidth, ULONG *pHeight)
{
    /* Both input parameters are zero based indexes, add 1 to get a width and a height. */
    ULONG W = iMaxX + 1;
    ULONG H = iMaxY + 1;

    /* Always include the hotspot point. */
    W = RT_MAX(XHot, W);
    H = RT_MAX(YHot, H);

    /* Align to 8 pixels, because the XOR/AND pointers are aligned like that.
     * The AND mask has one bit per pixel with 8 bits per byte.
     * In case the host can't deal with unaligned data.
     */
    W = RT_ALIGN_T(W, 8, ULONG);
    H = RT_ALIGN_T(H, 8, ULONG);

    /* Do not send bitmaps with zero dimensions. Actually make the min size 32x32. */
    W = RT_MAX(32, W);
    H = RT_MAX(32, H);

    /* Make it square. Some hosts are known to require square pointers. */
    W = RT_MAX(W, H);
    H = W;

    /* Do not exceed the supported size.
     * Actually this should not be necessary because Windows never creates such pointers.
     */
    W = RT_MIN(W, VBOXWDDM_C_POINTER_MAX_WIDTH);
    H = RT_MIN(H, VBOXWDDM_C_POINTER_MAX_HEIGHT);

    *pWidth = W;
    *pHeight = H;
}

BOOL vboxWddmPointerCopyColorData(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PVIDEO_POINTER_ATTRIBUTES pPointerAttributes,
                                  bool fDwordAlignScanlines)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;

    /* Windows often uses the maximum pointer size VBOXWDDM_C_POINTER_MAX_*
     * Exclude zero pixels (which are transparent anyway) from the right and the bottom of the bitmap.
     */
    LONG iMaxFilledPixel;
    LONG iMaxFilledScanline;
    vboxWddmPointerFindDimensionsColor(pSetPointerShape->pPixels, pSetPointerShape->Pitch,
                                       pSetPointerShape->Width, pSetPointerShape->Height,
                                       &iMaxFilledPixel, &iMaxFilledScanline);

    vboxWddmPointerAdjustDimensions(iMaxFilledPixel, iMaxFilledScanline,
                                    pSetPointerShape->XHot, pSetPointerShape->YHot,
                                    &srcMaskW, &srcMaskH);

    pPointerAttributes->Width = srcMaskW;
    pPointerAttributes->Height = srcMaskH;
    pPointerAttributes->WidthInBytes = pPointerAttributes->Width * 4;

    /* cnstruct and mask from alpha color channel */
    pSrc = (PBYTE)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels;
    dstBytesPerLine = (pPointerAttributes->Width+7)/8;
    if (fDwordAlignScanlines)
        dstBytesPerLine = RT_ALIGN_T(dstBytesPerLine, 4, ULONG);

    /* sanity check */
    uint32_t cbData = RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG)+
                      pPointerAttributes->Height*pPointerAttributes->WidthInBytes;
    uint32_t cbPointerAttributes = RT_UOFFSETOF_DYN(VIDEO_POINTER_ATTRIBUTES, Pixels[cbData]);
    Assert(VBOXWDDM_POINTER_ATTRIBUTES_SIZE >= cbPointerAttributes);
    if (VBOXWDDM_POINTER_ATTRIBUTES_SIZE < cbPointerAttributes)
    {
        LOGREL(("VBOXWDDM_POINTER_ATTRIBUTES_SIZE(%d) < cbPointerAttributes(%d)", VBOXWDDM_POINTER_ATTRIBUTES_SIZE, cbPointerAttributes));
        return FALSE;
    }

    memset(pDst, 0xFF, dstBytesPerLine*pPointerAttributes->Height);
    for (y=0; y<RT_MIN(pSetPointerShape->Height, pPointerAttributes->Height); ++y)
    {
        for (x=0, bit=7; x<RT_MIN(pSetPointerShape->Width, pPointerAttributes->Width); ++x, --bit)
        {
            if (0xFF==bit) bit=7;

            if (pSrc[y*pSetPointerShape->Pitch + x*4 + 3] > 0x7F)
            {
                pDst[y*dstBytesPerLine + x/8] &= ~RT_BIT(bit);
            }
        }
    }

    /* copy 32bpp to XOR DIB, it start in pPointerAttributes->Pixels should be 4bytes aligned */
    pSrc = (BYTE*)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels + RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG);
    dstBytesPerLine = pPointerAttributes->Width * 4;

    for (y=0; y<RT_MIN(pSetPointerShape->Height, pPointerAttributes->Height); ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+y*pSetPointerShape->Pitch, RT_MIN(dstBytesPerLine, pSetPointerShape->Pitch));
    }

    return TRUE;
}

BOOL vboxWddmPointerCopyMonoData(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PVIDEO_POINTER_ATTRIBUTES pPointerAttributes,
                                 bool fDwordAlignScanlines)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;

    /* Windows always uses the maximum pointer size VBOXWDDM_C_POINTER_MAX_*
     * Exclude unused pixels (which are transparent anyway) from the right and the bottom of the bitmap.
     */
    LONG iMaxFilledPixelAND;
    LONG iMaxFilledScanlineAND;
    vboxWddmPointerFindDimensionsMono(pSetPointerShape->pPixels, pSetPointerShape->Pitch,
                                      pSetPointerShape->Width, pSetPointerShape->Height, 0xff,
                                      &iMaxFilledPixelAND, &iMaxFilledScanlineAND);

    LONG iMaxFilledPixelXOR;
    LONG iMaxFilledScanlineXOR;
    vboxWddmPointerFindDimensionsMono((BYTE *)pSetPointerShape->pPixels + pSetPointerShape->Height * pSetPointerShape->Pitch,
                                      pSetPointerShape->Pitch,
                                      pSetPointerShape->Width, pSetPointerShape->Height, 0x00,
                                      &iMaxFilledPixelXOR, &iMaxFilledScanlineXOR);

    LONG iMaxFilledPixel = RT_MAX(iMaxFilledPixelAND, iMaxFilledPixelXOR);
    LONG iMaxFilledScanline = RT_MAX(iMaxFilledScanlineAND, iMaxFilledScanlineXOR);

    vboxWddmPointerAdjustDimensions(iMaxFilledPixel, iMaxFilledScanline,
                                    pSetPointerShape->XHot, pSetPointerShape->YHot,
                                    &srcMaskW, &srcMaskH);

    pPointerAttributes->Width = srcMaskW;
    pPointerAttributes->Height = srcMaskH;
    pPointerAttributes->WidthInBytes = pPointerAttributes->Width * 4;

    /* copy AND mask */
    pSrc = (PBYTE)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels;
    dstBytesPerLine = (pPointerAttributes->Width+7)/8;
    if (fDwordAlignScanlines)
        dstBytesPerLine = RT_ALIGN_T(dstBytesPerLine, 4, ULONG);

    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+y*pSetPointerShape->Pitch, dstBytesPerLine);
    }

    /* convert XOR mask to RGB0 DIB, it start in pPointerAttributes->Pixels should be 4bytes aligned */
    pSrc = (BYTE*)pSetPointerShape->pPixels + pSetPointerShape->Height*pSetPointerShape->Pitch;
    pDst = pPointerAttributes->Pixels + RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG);
    dstBytesPerLine = pPointerAttributes->Width * 4;

    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        for (x=0, bit=7; x<pPointerAttributes->Width; ++x, --bit)
        {
            if (0xFF==bit) bit=7;

            *(ULONG*)&pDst[y*dstBytesPerLine+x*4] = (pSrc[y*pSetPointerShape->Pitch+x/8] & RT_BIT(bit)) ? 0x00FFFFFF : 0;
        }
    }

    return TRUE;
}

static BOOLEAN vboxVddmPointerShapeToAttributes(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PVBOXWDDM_POINTER_INFO pPointerInfo,
                                                bool fDwordAlignScanlines)
{
    PVIDEO_POINTER_ATTRIBUTES pPointerAttributes = &pPointerInfo->Attributes.data;
    /* pPointerAttributes maintains the visibility state, clear all except visibility */
    pPointerAttributes->Enable &= VBOX_MOUSE_POINTER_VISIBLE;

    Assert(pSetPointerShape->Flags.Value == 1 || pSetPointerShape->Flags.Value == 2);
    if (pSetPointerShape->Flags.Color)
    {
        if (vboxWddmPointerCopyColorData(pSetPointerShape, pPointerAttributes, fDwordAlignScanlines))
        {
            pPointerAttributes->Flags = VIDEO_MODE_COLOR_POINTER;
            pPointerAttributes->Enable |= VBOX_MOUSE_POINTER_ALPHA;
        }
        else
        {
            LOGREL(("vboxWddmPointerCopyColorData failed"));
            AssertBreakpoint();
            return FALSE;
        }

    }
    else if (pSetPointerShape->Flags.Monochrome)
    {
        if (vboxWddmPointerCopyMonoData(pSetPointerShape, pPointerAttributes, fDwordAlignScanlines))
        {
            pPointerAttributes->Flags = VIDEO_MODE_MONO_POINTER;
        }
        else
        {
            LOGREL(("vboxWddmPointerCopyMonoData failed"));
            AssertBreakpoint();
            return FALSE;
        }
    }
    else
    {
        LOGREL(("unsupported pointer type Flags.Value(0x%x)", pSetPointerShape->Flags.Value));
        AssertBreakpoint();
        return FALSE;
    }

    pPointerAttributes->Enable |= VBOX_MOUSE_POINTER_SHAPE;

    /*
     * The hot spot coordinates and alpha flag will be encoded in the pPointerAttributes::Enable field.
     * High word will contain hot spot info and low word - flags.
     */
    pPointerAttributes->Enable |= (pSetPointerShape->YHot & 0xFF) << 24;
    pPointerAttributes->Enable |= (pSetPointerShape->XHot & 0xFF) << 16;

    return TRUE;
}

bool vboxWddmUpdatePointerShape(PVBOXMP_DEVEXT pDevExt, PVIDEO_POINTER_ATTRIBUTES pAttrs, uint32_t cbLength)
{
#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType != VBOXVIDEO_HWTYPE_VBOX)
    {
        NTSTATUS Status = STATUS_SUCCESS;

        /** @todo Get rid of the unnecesary en-/decode procedure (XPDM legacy). */
        uint32_t fFlags = pAttrs->Enable & 0x0000FFFF;
        uint32_t xHot = (pAttrs->Enable >> 16) & 0xFF;
        uint32_t yHot = (pAttrs->Enable >> 24) & 0xFF;
        uint32_t cWidth = pAttrs->Width;
        uint32_t cHeight = pAttrs->Height;
        uint32_t cbAndMask = 0;
        uint32_t cbXorMask = 0;

        if (fFlags & VBOX_MOUSE_POINTER_SHAPE)
        {
            /* Size of the pointer data: sizeof(AND mask) + sizeof(XOR mask) */
            /* "Each scanline is padded to a 32-bit boundary." */
            cbAndMask = ((((cWidth + 7) / 8) + 3) & ~3) * cHeight;
            cbXorMask = cWidth * 4 * cHeight;

            /* Send the shape to the host. */
            if (fFlags & VBOX_MOUSE_POINTER_ALPHA)
            {
                void const *pvImage = &pAttrs->Pixels[cbAndMask];
                Status = GaDefineAlphaCursor(pDevExt->pGa,
                                             xHot,
                                             yHot,
                                             cWidth,
                                             cHeight,
                                             pvImage,
                                             cbXorMask);
            }
            else
            {
                uint32_t u32AndMaskDepth = 1;
                uint32_t u32XorMaskDepth = 32;

                void const *pvAndMask = &pAttrs->Pixels[0];
                void const *pvXorMask = &pAttrs->Pixels[cbAndMask];
                Status = GaDefineCursor(pDevExt->pGa,
                                        xHot,
                                        yHot,
                                        cWidth,
                                        cHeight,
                                        u32AndMaskDepth,
                                        u32XorMaskDepth,
                                        pvAndMask,
                                        cbAndMask,
                                        pvXorMask,
                                        cbXorMask);
            }
        }

        /** @todo Hack: Use the legacy interface to handle visibility.
         * Eventually the VMSVGA WDDM driver should use the SVGA_FIFO_CURSOR_* interface.
         */
        VIDEO_POINTER_ATTRIBUTES attrs;
        RT_ZERO(attrs);
        attrs.Enable = pAttrs->Enable & VBOX_MOUSE_POINTER_VISIBLE;
        if (!VBoxMPCmnUpdatePointerShape(VBoxCommonFromDeviceExt(pDevExt), &attrs, sizeof(attrs)))
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        return Status == STATUS_SUCCESS;
    }
#endif

    /* VBOXVIDEO_HWTYPE_VBOX */
    return VBoxMPCmnUpdatePointerShape(VBoxCommonFromDeviceExt(pDevExt), pAttrs, cbLength);
}

static void vboxWddmHostPointerEnable(PVBOXMP_DEVEXT pDevExt, BOOLEAN fEnable)
{
    VIDEO_POINTER_ATTRIBUTES PointerAttributes;
    RT_ZERO(PointerAttributes);
    if (fEnable)
    {
        PointerAttributes.Enable = VBOX_MOUSE_POINTER_VISIBLE;
    }
    vboxWddmUpdatePointerShape(pDevExt, &PointerAttributes, sizeof(PointerAttributes));
}

/**
 * Reports the current mouse cursor position to the host.
 *
 * @returns VBox status code.
 * @param   pDevExt             Device extension to use.
 * @param   xPos                X position to report to the host.
 * @param   yPos                Y position to report to the host.
 */
static int vboxWddmReportCursorPosition(PVBOXMP_DEVEXT pDevExt, uint32_t xPos, uint32_t yPos)
{
    VIDEO_POINTER_POSITION Pos;
    RT_ZERO(Pos);
    Pos.Column = xPos;
    Pos.Row    = yPos;

    return VBoxMPCmnReportCursorPosition(VBoxCommonFromDeviceExt(pDevExt), &Pos);
}

NTSTATUS
APIENTRY
DxgkDdiSetPointerPosition(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPOINTERPOSITION*  pSetPointerPosition)
{
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    /* mouse integration is ON */
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    PVBOXWDDM_POINTER_INFO pPointerInfo = &pDevExt->aSources[pSetPointerPosition->VidPnSourceId].PointerInfo;
    PVBOXWDDM_GLOBAL_POINTER_INFO pGlobalPointerInfo = &pDevExt->PointerInfo;
    PVIDEO_POINTER_ATTRIBUTES pPointerAttributes = &pPointerInfo->Attributes.data;
    BOOLEAN fScreenVisState = !!(pPointerAttributes->Enable & VBOX_MOUSE_POINTER_VISIBLE);
    BOOLEAN fVisStateChanged = FALSE;
    BOOLEAN fScreenChanged = pGlobalPointerInfo->iLastReportedScreen != pSetPointerPosition->VidPnSourceId;

    if (pSetPointerPosition->Flags.Visible)
    {
        pPointerAttributes->Enable |= VBOX_MOUSE_POINTER_VISIBLE;
        if (!fScreenVisState)
        {
            fVisStateChanged = TRUE;
        }
    }
    else
    {
        pPointerAttributes->Enable &= ~VBOX_MOUSE_POINTER_VISIBLE;
        if (fScreenVisState)
        {
            fVisStateChanged = TRUE;
        }
    }

    pGlobalPointerInfo->iLastReportedScreen = pSetPointerPosition->VidPnSourceId;

    const bool fWantsAbsolute = VBoxQueryHostWantsAbsolute();

    if ((fVisStateChanged || fScreenChanged) && fWantsAbsolute)
    {
        if (fScreenChanged)
        {
            BOOLEAN bResult = vboxWddmUpdatePointerShape(pDevExt, &pPointerInfo->Attributes.data, VBOXWDDM_POINTER_ATTRIBUTES_SIZE);
            if (!bResult)
            {
                vboxWddmHostPointerEnable(pDevExt, FALSE);
            }
        }

        // Always update the visibility as requested. Tell the host to use the guest's pointer.
        vboxWddmHostPointerEnable(pDevExt, pSetPointerPosition->Flags.Visible);
    }

    /* Report the mouse cursor position to the host if changed. */
    if (   fWantsAbsolute
        && (   pGlobalPointerInfo->iLastPosX != (uint32_t)pSetPointerPosition->X
            || pGlobalPointerInfo->iLastPosY != (uint32_t)pSetPointerPosition->Y))
    {
        vboxWddmReportCursorPosition(pDevExt, (uint32_t)pSetPointerPosition->X, (uint32_t)pSetPointerPosition->Y);

        pGlobalPointerInfo->iLastPosX = (uint32_t)pSetPointerPosition->X;
        pGlobalPointerInfo->iLastPosY = (uint32_t)pSetPointerPosition->Y;
    }

    //    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiSetPointerShape(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPOINTERSHAPE*  pSetPointerShape)
{
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (VBoxQueryHostWantsAbsolute())
    {
        /* mouse integration is ON */
        PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
        PVBOXWDDM_POINTER_INFO pPointerInfo = &pDevExt->aSources[pSetPointerShape->VidPnSourceId].PointerInfo;
        bool const fDwordAlignScanlines = pDevExt->enmHwType != VBOXVIDEO_HWTYPE_VBOX;
        /** @todo to avoid extra data copy and extra heap allocation,
         *  need to maintain the pre-allocated HGSMI buffer and convert the data directly to it */
        if (vboxVddmPointerShapeToAttributes(pSetPointerShape, pPointerInfo, fDwordAlignScanlines))
        {
            pDevExt->PointerInfo.iLastReportedScreen = pSetPointerShape->VidPnSourceId;
            if (vboxWddmUpdatePointerShape(pDevExt, &pPointerInfo->Attributes.data, VBOXWDDM_POINTER_ATTRIBUTES_SIZE))
                Status = STATUS_SUCCESS;
            else
            {
                // tell the host to use the guest's pointer
                vboxWddmHostPointerEnable(pDevExt, FALSE);
            }
        }
    }

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY CALLBACK
DxgkDdiResetFromTimeout(
    CONST HANDLE  hAdapter)
{
    RT_NOREF(hAdapter);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();
    /** @todo fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}


/* the lpRgnData->Buffer comes to us as RECT
 * to avoid extra memcpy we cast it to PRTRECT assuming
 * they are identical */
AssertCompile(sizeof(RECT) == sizeof(RTRECT));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(RTRECT, xLeft));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(RTRECT, yBottom));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(RTRECT, xRight));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(RTRECT, yTop));

NTSTATUS
APIENTRY
DxgkDdiEscape(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_ESCAPE*  pEscape)
{
    PAGED_CODE();

//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    Assert(pEscape->PrivateDriverDataSize >= sizeof (VBOXDISPIFESCAPE));
    if (pEscape->PrivateDriverDataSize >= sizeof (VBOXDISPIFESCAPE))
    {
        PVBOXDISPIFESCAPE pEscapeHdr = (PVBOXDISPIFESCAPE)pEscape->pPrivateDriverData;
        switch (pEscapeHdr->escapeCode)
        {
            case VBOXESC_SETVISIBLEREGION:
            {
#ifdef VBOX_DISPIF_WITH_OPCONTEXT
                PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("VBOXESC_SETVISIBLEREGION no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS)
                {
                    WARN(("VBOXESC_SETVISIBLEREGION invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif
                /* visible regions for seamless */
                LPRGNDATA lpRgnData = VBOXDISPIFESCAPE_DATA(pEscapeHdr, RGNDATA);
                uint32_t cbData = VBOXDISPIFESCAPE_DATA_SIZE(pEscape->PrivateDriverDataSize);
                uint32_t cbRects = cbData - RT_UOFFSETOF(RGNDATA, Buffer);
                /* the lpRgnData->Buffer comes to us as RECT
                 * to avoid extra memcpy we cast it to PRTRECT assuming
                 * they are identical
                 * see AssertCompile's above */

                RTRECT   *pRect = (RTRECT *)&lpRgnData->Buffer;

                uint32_t cRects = cbRects/sizeof(RTRECT);
                int      rc;

                LOG(("IOCTL_VIDEO_VBOX_SETVISIBLEREGION cRects=%d", cRects));
                Assert(cbRects >= sizeof(RTRECT)
                    &&  cbRects == cRects*sizeof(RTRECT)
                    &&  cRects == lpRgnData->rdh.nCount);
                if (    cbRects >= sizeof(RTRECT)
                    &&  cbRects == cRects*sizeof(RTRECT)
                    &&  cRects == lpRgnData->rdh.nCount)
                {
                    /*
                     * Inform the host about the visible region
                     */
                    VMMDevVideoSetVisibleRegion *pReq = NULL;

                    rc = VbglR0GRAlloc ((VMMDevRequestHeader **)&pReq,
                                      sizeof (VMMDevVideoSetVisibleRegion) + (cRects-1)*sizeof(RTRECT),
                                      VMMDevReq_VideoSetVisibleRegion);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        pReq->cRect = cRects;
                        memcpy(&pReq->Rect, pRect, cRects*sizeof(RTRECT));

                        rc = VbglR0GRPerform (&pReq->header);
                        AssertRC(rc);
                        if (RT_SUCCESS(rc))
                            Status = STATUS_SUCCESS;
                        else
                        {
                            WARN(("VbglR0GRPerform failed rc (%d)", rc));
                            Status = STATUS_UNSUCCESSFUL;
                        }
                        VbglR0GRFree(&pReq->header);
                    }
                    else
                    {
                        WARN(("VbglR0GRAlloc failed rc (%d)", rc));
                        Status = STATUS_UNSUCCESSFUL;
                    }
                }
                else
                {
                    WARN(("VBOXESC_SETVISIBLEREGION: incorrect buffer size (%d), reported count (%d)", cbRects, lpRgnData->rdh.nCount));
                    Status = STATUS_INVALID_PARAMETER;
                }
                break;
            }
            case VBOXESC_ISVRDPACTIVE:
                /** @todo implement */
                Status = STATUS_SUCCESS;
                break;
            case VBOXESC_CONFIGURETARGETS:
            {
                LOG(("=> VBOXESC_CONFIGURETARGETS"));

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("VBOXESC_CONFIGURETARGETS called without HardwareAccess flag set, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

#ifdef VBOX_DISPIF_WITH_OPCONTEXT
                /* win8.1 does not allow context-based escapes for display-only mode */
                PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("VBOXESC_CONFIGURETARGETS no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE)
                {
                    WARN(("VBOXESC_CONFIGURETARGETS invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif

                if (pEscape->PrivateDriverDataSize != sizeof (*pEscapeHdr))
                {
                    WARN(("VBOXESC_CONFIGURETARGETS invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscapeHdr->u32CmdSpecific)
                {
                    WARN(("VBOXESC_CONFIGURETARGETS invalid command %d", pEscapeHdr->u32CmdSpecific));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                HANDLE hKey = NULL;
                uint32_t cAdjusted = 0;

                for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
                    if (pTarget->fConfigured)
                        continue;

                    pTarget->fConfigured = true;

                    if (!pTarget->fConnected)
                    {
                        Status = VBoxWddmChildStatusConnect(pDevExt, (uint32_t)i, TRUE);
                        if (NT_SUCCESS(Status))
                            ++cAdjusted;
                        else
                            WARN(("VBOXESC_CONFIGURETARGETS vboxWddmChildStatusConnectSecondaries failed Status 0x%x\n", Status));
                    }

                    if (!hKey)
                    {
                        Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_WRITE, &hKey);
                        if (!NT_SUCCESS(Status))
                        {
                            WARN(("VBOXESC_CONFIGURETARGETS IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
                            hKey = NULL;
                            continue;
                        }
                    }

                    Assert(hKey);

                    WCHAR wszNameBuf[sizeof(VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX) / sizeof(WCHAR) + 32];
                    RTUtf16Printf(wszNameBuf, RT_ELEMENTS(wszNameBuf), "%ls%d", VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX, i);
                    Status = vboxWddmRegSetValueDword(hKey, wszNameBuf, VBOXWDDM_CFG_DRVTARGET_CONNECTED);
                    LogRel2((VBOX_VIDEO_LOG_NAME ": RegSetDword '%ls'. Status 0x%X, dwVal %d\n", wszNameBuf, Status, VBOXWDDM_CFG_DRVTARGET_CONNECTED));
                    if (!NT_SUCCESS(Status))
                        WARN(("VBOXESC_CONFIGURETARGETS vboxWddmRegSetValueDword (%ls) failed Status 0x%x\n", wszNameBuf, Status));

                }

                if (hKey)
                {
                    NTSTATUS rcNt2 = ZwClose(hKey);
                    Assert(rcNt2 == STATUS_SUCCESS); NOREF(rcNt2);
                }

                pEscapeHdr->u32CmdSpecific = cAdjusted;

                Status = STATUS_SUCCESS;

                LOG(("<= VBOXESC_CONFIGURETARGETS"));
                break;
            }

            case VBOXESC_RECONNECT_TARGETS:
            {
                LOG(("=> VBOXESC_RECONNECT_TARGETS"));

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("VBOXESC_RECONNECT_TARGETS called without HardwareAccess flag set, failing\n"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscape->PrivateDriverDataSize < sizeof (VBOXDISPIFESCAPE_RECONNECT_TARGETS))
                {
                    WARN(("VBOXESC_RECONNECT_TARGETS invalid private driver size %d\n", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscapeHdr->u32CmdSpecific != 0)
                {
                    WARN(("VBOXESC_RECONNECT_TARGETS u32CmdSpecific is not zero\n"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                VBOXDISPIFESCAPE_RECONNECT_TARGETS *pVBoxEscapeReconnectTargets = (VBOXDISPIFESCAPE_RECONNECT_TARGETS *)pEscape->pPrivateDriverData;
                uint32_t u32ConnectMask = pVBoxEscapeReconnectTargets->u32ConnectMask;
                uint32_t u32DisconnectMask = pVBoxEscapeReconnectTargets->u32DisconnectMask;

                LogRel2((VBOX_VIDEO_LOG_NAME ": VBOXESC_RECONNECT_TARGETS 0x%X, 0x%X\n", u32ConnectMask, u32DisconnectMask));

                if (u32ConnectMask & u32DisconnectMask)
                {
                    WARN(("VBOXESC_RECONNECT_TARGETS (u32ConnectMask & u32DisconnectMask) is not zero\n"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                HANDLE hKey = NULL;
                Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_WRITE, &hKey);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("VBOXESC_CONFIGURETARGETS IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
                    hKey = NULL;
                }

                for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
                    bool fConnectReq;

                    if (u32ConnectMask & RT_BIT(i))
                        fConnectReq = true;
                    else if (u32DisconnectMask & RT_BIT(i))
                        fConnectReq = false;
                    else
                        continue;

                    pTarget->fConfigured = true;

                    if (pTarget->fConnected != fConnectReq)
                    {
                        Status = VBoxWddmChildStatusConnect(pDevExt, (uint32_t)i, fConnectReq);
                        LogRel2((VBOX_VIDEO_LOG_NAME ": VBOXESC_RECONNECT_TARGETS %sconnecting target %d, status 0x%x\n", fConnectReq ? "" : "dis", i, Status));
                        pTarget->fConnected = fConnectReq;

                        if (RT_LIKELY(hKey))
                        {
                            WCHAR wszNameBuf[sizeof(VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX) / sizeof(WCHAR) + 32];
                            RTUtf16Printf(wszNameBuf, RT_ELEMENTS(wszNameBuf), "%ls%u", VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX, i);
                            Status = vboxWddmRegSetValueDword(hKey, wszNameBuf, fConnectReq);
                            LogRel2((VBOX_VIDEO_LOG_NAME ": RegSetDword '%ls'. Status 0x%X, dwVal %d\n", wszNameBuf, Status, fConnectReq));
                        }
                    }
                }

                if (RT_LIKELY(hKey))
                {
                    NTSTATUS rcNt2 = ZwClose(hKey);
                    Assert(rcNt2 == STATUS_SUCCESS); NOREF(rcNt2);
                }

                Status = STATUS_SUCCESS;

                LOG(("<= VBOXESC_RECONNECT_TARGETS"));
                break;
            }

            case VBOXESC_SETALLOCHOSTID:
            {
                PVBOXWDDM_DEVICE pDevice = (PVBOXWDDM_DEVICE)pEscape->hDevice;
                if (!pDevice)
                {
                    WARN(("VBOXESC_SETALLOCHOSTID called without no device specified, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscape->PrivateDriverDataSize != sizeof (VBOXDISPIFESCAPE_SETALLOCHOSTID))
                {
                    WARN(("invalid buffer size for VBOXDISPIFESCAPE_SETALLOCHOSTID, was(%d), but expected (%d)",
                            pEscape->PrivateDriverDataSize, sizeof (VBOXDISPIFESCAPE_SETALLOCHOSTID)));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("VBOXESC_SETALLOCHOSTID not HardwareAccess"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PVBOXDISPIFESCAPE_SETALLOCHOSTID pSetHostID = (PVBOXDISPIFESCAPE_SETALLOCHOSTID)pEscapeHdr;
                PVBOXWDDM_ALLOCATION pAlloc = vboxWddmGetAllocationFromHandle(pDevExt, (D3DKMT_HANDLE)pSetHostID->hAlloc);
                if (!pAlloc)
                {
                    WARN(("failed to get allocation from handle"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pAlloc->enmType == VBOXWDDM_ALLOC_TYPE_D3D)
                {
                    pSetHostID->EscapeHdr.u32CmdSpecific = pAlloc->dx.sid;
                    pSetHostID->rc = VERR_NOT_EQUAL;
                    Status = STATUS_SUCCESS;
                    break;
                }

                if (pAlloc->enmType != VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE)
                {
                    WARN(("setHostID: invalid allocation type: %d", pAlloc->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                pSetHostID->rc = VBoxWddmOaSetHostID(pDevice, pAlloc, pSetHostID->hostID, &pSetHostID->EscapeHdr.u32CmdSpecific);

                if (pAlloc->bAssigned)
                {
                    PVBOXMP_DEVEXT pDevExt2 = pDevice->pAdapter;
                    Assert(pAlloc->AllocData.SurfDesc.VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt2)->cDisplays);
                    PVBOXWDDM_SOURCE pSource = &pDevExt2->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
                    if (pSource->AllocData.hostID != pAlloc->AllocData.hostID)
                    {
                        pSource->AllocData.hostID = pAlloc->AllocData.hostID;
                        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION;

                        vboxWddmGhDisplayCheckSetInfo(pDevExt2);
                    }
                }

                Status = STATUS_SUCCESS;
                break;
            }
            case VBOXESC_ISANYX:
            {
                if (pEscape->PrivateDriverDataSize != sizeof (VBOXDISPIFESCAPE_ISANYX))
                {
                    WARN(("invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PVBOXDISPIFESCAPE_ISANYX pIsAnyX = (PVBOXDISPIFESCAPE_ISANYX)pEscapeHdr;
                pIsAnyX->u32IsAnyX = VBoxCommonFromDeviceExt(pDevExt)->fAnyX;
                Status = STATUS_SUCCESS;
                break;
            }
            case VBOXESC_UPDATEMODES:
            {
                LOG(("=> VBOXESC_UPDATEMODES"));

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("VBOXESC_UPDATEMODES called without HardwareAccess flag set, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

#ifdef VBOX_DISPIF_WITH_OPCONTEXT
                /* win8.1 does not allow context-based escapes for display-only mode */
                PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("VBOXESC_UPDATEMODES no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE)
                {
                    WARN(("VBOXESC_UPDATEMODES invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif

                if (pEscape->PrivateDriverDataSize != sizeof (VBOXDISPIFESCAPE_UPDATEMODES))
                {
                    WARN(("VBOXESC_UPDATEMODES invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                VBOXDISPIFESCAPE_UPDATEMODES *pData = (VBOXDISPIFESCAPE_UPDATEMODES*)pEscapeHdr;
                Status = VBoxVidPnUpdateModes(pDevExt, pData->u32TargetId, &pData->Size);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("VBoxVidPnUpdateModes failed Status(%#x)\n", Status));
                    return Status;
                }

                Status = STATUS_SUCCESS;
                break;
            }
            case VBOXESC_TARGET_CONNECTIVITY:
            {
                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("VBOXESC_TARGET_CONNECTIVITY called without HardwareAccess flag set, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscape->PrivateDriverDataSize != sizeof(VBOXDISPIFESCAPE_TARGETCONNECTIVITY))
                {
                    WARN(("VBOXESC_TARGET_CONNECTIVITY invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                VBOXDISPIFESCAPE_TARGETCONNECTIVITY *pData = (VBOXDISPIFESCAPE_TARGETCONNECTIVITY *)pEscapeHdr;
                LOG(("=> VBOXESC_TARGET_CONNECTIVITY[%d] 0x%08X", pData->u32TargetId, pData->fu32Connect));

                if (pData->u32TargetId >= (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
                {
                    WARN(("VBOXESC_TARGET_CONNECTIVITY invalid screen index 0x%x", pData->u32TargetId));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PVBOXWDDM_TARGET pTarget = &pDevExt->aTargets[pData->u32TargetId];
                pTarget->fDisabled = !RT_BOOL(pData->fu32Connect & 1);
                pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;

                Status = STATUS_SUCCESS;
                break;
            }
            case VBOXESC_DBGPRINT:
            {
                /* use RT_OFFSETOF instead of sizeof since sizeof will give an aligned size that might
                 * be bigger than the VBOXDISPIFESCAPE_DBGPRINT with a data containing just a few chars */
                Assert(pEscape->PrivateDriverDataSize >= RT_UOFFSETOF(VBOXDISPIFESCAPE_DBGPRINT, aStringBuf[1]));
                /* only do DbgPrint when pEscape->PrivateDriverDataSize > RT_OFFSETOF(VBOXDISPIFESCAPE_DBGPRINT, aStringBuf[1])
                 * since == RT_OFFSETOF(VBOXDISPIFESCAPE_DBGPRINT, aStringBuf[1]) means the buffer contains just \0,
                 * i.e. no need to print it */
                if (pEscape->PrivateDriverDataSize > RT_UOFFSETOF(VBOXDISPIFESCAPE_DBGPRINT, aStringBuf[1]))
                {
                    PVBOXDISPIFESCAPE_DBGPRINT pDbgPrint = (PVBOXDISPIFESCAPE_DBGPRINT)pEscapeHdr;
                    /* ensure the last char is \0*/
                    if (*((uint8_t*)pDbgPrint + pEscape->PrivateDriverDataSize - 1) == '\0')
                    {
                        if (g_VBoxLogUm & VBOXWDDM_CFG_LOG_UM_DBGPRINT)
                            DbgPrint("%s\n", pDbgPrint->aStringBuf);
                        if (g_VBoxLogUm & VBOXWDDM_CFG_LOG_UM_BACKDOOR)
                            LOGREL_EXACT(("%s\n", pDbgPrint->aStringBuf));
                    }
                }
                Status = STATUS_SUCCESS;
                break;
            }
            case VBOXESC_DBGDUMPBUF:
            {
                Status = vboxUmdDumpBuf((PVBOXDISPIFESCAPE_DBGDUMPBUF)pEscapeHdr, pEscape->PrivateDriverDataSize);
                break;
            }
            case VBOXESC_GUEST_DISPLAYCHANGED:
            {
                LOG(("=> VBOXESC_GUEST_DISPLAYCHANGED"));

                for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
#ifdef VBOX_WITH_VMSVGA
                    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
                    {
                        GaVidPnSourceCheckPos(pDevExt, i);
                        continue;
                    }
#endif

                    vboxWddmDisplaySettingsCheckPos(pDevExt, i);
                }
                Status = STATUS_SUCCESS;
                break;
            }
            default:
#ifdef VBOX_WITH_VMSVGA
                Status = GaDxgkDdiEscape(hAdapter, pEscape);
                if (NT_SUCCESS(Status) || Status != STATUS_NOT_SUPPORTED)
                    break;
#endif
                WARN(("unsupported escape code (0x%x)", pEscapeHdr->escapeCode));
                break;
        }
    }
    else
    {
        WARN(("pEscape->PrivateDriverDataSize(%d) < (%d)", pEscape->PrivateDriverDataSize, sizeof (VBOXDISPIFESCAPE)));
        Status = STATUS_BUFFER_TOO_SMALL;
    }

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCollectDbgInfo(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_COLLECTDBGINFO*  pCollectDbgInfo
    )
{
    RT_NOREF(hAdapter, pCollectDbgInfo);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiIsSupportedVidPn(
    CONST HANDLE  hAdapter,
    OUT DXGKARG_ISSUPPORTEDVIDPN*  pIsSupportedVidPnArg
    )
{
    /* The DxgkDdiIsSupportedVidPn should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    NTSTATUS Status = VBoxVidPnIsSupported(pDevExt, pIsSupportedVidPnArg->hDesiredVidPn, &pIsSupportedVidPnArg->IsVidPnSupported);
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxVidPnIsSupported failed Status(%#x)\n", Status));
        return Status;
    }

    LOGF(("LEAVE, isSupported(%d), context(0x%x)", pIsSupportedVidPnArg->IsVidPnSupported, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendFunctionalVidPn(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPnArg
    )
{
    /* The DxgkDdiRecommendFunctionalVidPn should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    if (pRecommendFunctionalVidPnArg->PrivateDriverDataSize != sizeof (VBOXWDDM_RECOMMENDVIDPN))
    {
        WARN(("invalid size"));
        return STATUS_INVALID_PARAMETER;
    }

    VBOXWDDM_RECOMMENDVIDPN *pData = (VBOXWDDM_RECOMMENDVIDPN*)pRecommendFunctionalVidPnArg->pPrivateDriverData;
    Assert(pData);

    NTSTATUS Status = VBoxVidPnRecommendFunctional(pDevExt, pRecommendFunctionalVidPnArg->hRecommendedFunctionalVidPn, pData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxVidPnRecommendFunctional failed %#x", Status));
        return Status;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiEnumVidPnCofuncModality(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModalityArg
    )
{
    /* The DxgkDdiEnumVidPnCofuncModality function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    NTSTATUS Status = VBoxVidPnCofuncModality(pDevExt, pEnumCofuncModalityArg->hConstrainingVidPn, pEnumCofuncModalityArg->EnumPivotType, &pEnumCofuncModalityArg->EnumPivot);
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxVidPnCofuncModality failed Status(%#x)\n", Status));
        return Status;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiSetVidPnSourceAddress(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEADDRESS*  pSetVidPnSourceAddress
    )
{
    /* The DxgkDdiSetVidPnSourceAddress function should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", hAdapter));
    LOG(("id %d, seg %d, addr 0x%RX64, hAllocation %p, ctx cnt %d, f 0x%x",
         pSetVidPnSourceAddress->VidPnSourceId,
         pSetVidPnSourceAddress->PrimarySegment,
         pSetVidPnSourceAddress->PrimaryAddress.QuadPart,
         pSetVidPnSourceAddress->hAllocation,
         pSetVidPnSourceAddress->ContextCount,
         pSetVidPnSourceAddress->Flags.Value));

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    if ((UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays <= pSetVidPnSourceAddress->VidPnSourceId)
    {
        WARN(("invalid VidPnSourceId (%d), for displays(%d)", pSetVidPnSourceAddress->VidPnSourceId, VBoxCommonFromDeviceExt(pDevExt)->cDisplays));
        return STATUS_INVALID_PARAMETER;
    }

#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType != VBOXVIDEO_HWTYPE_VMSVGA)
#endif
    vboxWddmDisplaySettingsCheckPos(pDevExt, pSetVidPnSourceAddress->VidPnSourceId);

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pSetVidPnSourceAddress->VidPnSourceId];

    /*
     * Update the source VRAM address.
     */
    PVBOXWDDM_ALLOCATION pAllocation;
    Assert(pSetVidPnSourceAddress->hAllocation);
    Assert(pSetVidPnSourceAddress->hAllocation || pSource->pPrimaryAllocation);
    Assert (pSetVidPnSourceAddress->Flags.Value < 2); /* i.e. 0 or 1 (ModeChange) */

    if (pSetVidPnSourceAddress->hAllocation)
    {
        pAllocation = (PVBOXWDDM_ALLOCATION)pSetVidPnSourceAddress->hAllocation;
        vboxWddmAssignPrimary(pSource, pAllocation, pSetVidPnSourceAddress->VidPnSourceId);
    }
    else
        pAllocation = pSource->pPrimaryAllocation;

    if (pAllocation)
    {
        vboxWddmAddrSetVram(&pAllocation->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment, (VBOXVIDEOOFFSET)pSetVidPnSourceAddress->PrimaryAddress.QuadPart);
    }

    if (g_VBoxDisplayOnly && !pAllocation)
    {
        /* the VRAM here is an absolute address, nto an offset!
         * convert to offset since all internal VBox functionality is offset-based */
        vboxWddmAddrSetVram(&pSource->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment,
                vboxWddmVramAddrToOffset(pDevExt, pSetVidPnSourceAddress->PrimaryAddress));
    }
    else
    {
        Assert(!g_VBoxDisplayOnly);
        vboxWddmAddrSetVram(&pSource->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment,
                                                    pSetVidPnSourceAddress->PrimaryAddress.QuadPart);
    }

    pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION;

    /*
     * Report the source.
     */
#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        /* Query the position of the screen to make sure it is up to date. */
        vboxWddmDisplaySettingsQueryPos(pDevExt, pSetVidPnSourceAddress->VidPnSourceId, &pSource->VScreenPos);

        GaVidPnSourceReport(pDevExt, pSource);
        return STATUS_SUCCESS;
    }
#endif

    vboxWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiSetVidPnSourceVisibility(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility
    )
{
    /* DxgkDdiSetVidPnSourceVisibility should be made pageable. */
    PAGED_CODE();

    vboxVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    if ((UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays <= pSetVidPnSourceVisibility->VidPnSourceId)
    {
        WARN(("invalid VidPnSourceId (%d), for displays(%d)", pSetVidPnSourceVisibility->VidPnSourceId, VBoxCommonFromDeviceExt(pDevExt)->cDisplays));
        return STATUS_INVALID_PARAMETER;
    }

#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType != VBOXVIDEO_HWTYPE_VMSVGA)
#endif
    vboxWddmDisplaySettingsCheckPos(pDevExt, pSetVidPnSourceVisibility->VidPnSourceId);

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pSetVidPnSourceVisibility->VidPnSourceId];
    PVBOXWDDM_ALLOCATION pAllocation = pSource->pPrimaryAllocation;
    if (pAllocation)
    {
        Assert(pAllocation->bVisible == pSource->bVisible);
        pAllocation->bVisible = pSetVidPnSourceVisibility->Visible;
    }

    if (pSource->bVisible != pSetVidPnSourceVisibility->Visible)
    {
        pSource->bVisible = pSetVidPnSourceVisibility->Visible;
//        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_VISIBILITY;
//        if (pDevExt->fCmdVbvaEnabled || pSource->bVisible)
//        {
//            vboxWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);
//        }
    }

#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        GaVidPnSourceCheckPos(pDevExt, pSetVidPnSourceVisibility->VidPnSourceId);
    }
#endif

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCommitVidPn(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_COMMITVIDPN* CONST  pCommitVidPnArg
    )
{
    LOG(("ENTER AffectedVidPnSourceId(%d) hAdapter(0x%x)", pCommitVidPnArg->AffectedVidPnSourceId, hAdapter));

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    NTSTATUS Status;

    vboxVDbgBreakFv();

    VBOXWDDM_SOURCE *paSources = (VBOXWDDM_SOURCE*)RTMemAlloc(sizeof (VBOXWDDM_SOURCE) * VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    if (!paSources)
    {
        WARN(("RTMemAlloc failed"));
        return STATUS_NO_MEMORY;
    }

    VBOXWDDM_TARGET *paTargets = (VBOXWDDM_TARGET*)RTMemAlloc(sizeof (VBOXWDDM_TARGET) * VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    if (!paTargets)
    {
        WARN(("RTMemAlloc failed"));
        RTMemFree(paSources);
        return STATUS_NO_MEMORY;
    }

    VBoxVidPnSourcesInit(paSources, VBoxCommonFromDeviceExt(pDevExt)->cDisplays, VBOXWDDM_HGSYNC_F_SYNCED_ALL);

    VBoxVidPnTargetsInit(paTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays, VBOXWDDM_HGSYNC_F_SYNCED_ALL);

    VBoxVidPnSourcesCopy(paSources, pDevExt->aSources, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    VBoxVidPnTargetsCopy(paTargets, pDevExt->aTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

    do {
        const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
        Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPnArg->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("DxgkCbQueryVidPnInterface failed Status 0x%x", Status));
            break;
        }

#ifdef VBOXWDDM_DEBUG_VIDPN
        vboxVidPnDumpVidPn("\n>>>>COMMIT VidPN: >>>>", pDevExt, pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

        if (pCommitVidPnArg->AffectedVidPnSourceId != D3DDDI_ID_ALL)
        {
            Status = VBoxVidPnCommitSourceModeForSrcId(
                    pDevExt,
                    pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface,
                    (PVBOXWDDM_ALLOCATION)pCommitVidPnArg->hPrimaryAllocation,
                    pCommitVidPnArg->AffectedVidPnSourceId, paSources, paTargets, pCommitVidPnArg->Flags.PathPowerTransition);
            if (!NT_SUCCESS(Status))
            {
                WARN(("VBoxVidPnCommitSourceModeForSrcId for current VidPn failed Status 0x%x", Status));
                break;
            }
        }
        else
        {
            Status = VBoxVidPnCommitAll(pDevExt, pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface,
                    (PVBOXWDDM_ALLOCATION)pCommitVidPnArg->hPrimaryAllocation,
                    paSources, paTargets);
            if (!NT_SUCCESS(Status))
            {
                WARN(("VBoxVidPnCommitAll for current VidPn failed Status 0x%x", Status));
                break;
            }
        }

        Assert(NT_SUCCESS(Status));
        pDevExt->u.primary.hCommittedVidPn = pCommitVidPnArg->hFunctionalVidPn;
        VBoxVidPnSourcesCopy(pDevExt->aSources, paSources, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
        VBoxVidPnTargetsCopy(pDevExt->aTargets, paTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

        VBoxDumpSourceTargetArrays(paSources, paTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

#ifdef VBOX_WITH_VMSVGA
        if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        {
            for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
            {
                VBOXWDDM_SOURCE *pSource = &pDevExt->aSources[i];

                LOG(("Source [%d]: visible %d, blanked %d", i, pSource->bVisible, pSource->bBlankedByPowerOff));

                /* Update positions of all screens. */
                vboxWddmDisplaySettingsQueryPos(pDevExt, i, &pSource->VScreenPos);

                GaVidPnSourceReport(pDevExt, pSource);
            }

            for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
            {
                VBOXWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
                Assert(pTarget->u32Id == (unsigned)i);
                if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
                {
                    continue;
                }

                LOG(("Target [%d]: blanked %d", i, pTarget->fBlankedByPowerOff));

                if (pTarget->fBlankedByPowerOff)
                {
                    GaScreenDefine(pDevExt->pGa, 0, pTarget->u32Id, 0, 0, 0, 0, true);
                }
                else
                {
                    GaScreenDestroy(pDevExt->pGa, pTarget->u32Id);
                }
            }

            break;
        }
#endif
        vboxWddmGhDisplayCheckSetInfo(pDevExt);
    } while (0);

    RTMemFree(paSources);
    RTMemFree(paTargets);

    LOG(("LEAVE, status(0x%x), hAdapter(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPathArg
    )
{
    RT_NOREF(hAdapter, pUpdateActiveVidPnPresentPathArg);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendMonitorModes(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModesArg
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    NTSTATUS Status = VBoxVidPnRecommendMonitorModes(pDevExt, pRecommendMonitorModesArg->VideoPresentTargetId,
            pRecommendMonitorModesArg->hMonitorSourceModeSet, pRecommendMonitorModesArg->pMonitorSourceModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxVidPnRecommendMonitorModes failed %#x", Status));
        return Status;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendVidPnTopology(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopologyArg
    )
{
    RT_NOREF(hAdapter, pRecommendVidPnTopologyArg);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    vboxVDbgBreakFv();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}

NTSTATUS
APIENTRY
DxgkDdiGetScanLine(
    CONST HANDLE  hAdapter,
    DXGKARG_GETSCANLINE*  pGetScanLine)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

#ifdef DEBUG_misha
//    RT_BREAKPOINT();
#endif

    NTSTATUS Status = VBoxWddmSlGetScanLine(pDevExt, pGetScanLine);

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiStopCapture(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_STOPCAPTURE*  pStopCapture)
{
    RT_NOREF(hAdapter, pStopCapture);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiControlInterrupt(
    CONST HANDLE hAdapter,
    CONST DXGK_INTERRUPT_TYPE InterruptType,
    BOOLEAN Enable
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;

    switch (InterruptType)
    {
        case DXGK_INTERRUPT_DISPLAYONLY_VSYNC:
        case DXGK_INTERRUPT_CRTC_VSYNC:
        {
            Status = VBoxWddmSlEnableVSyncNotification(pDevExt, Enable);
            if (NT_SUCCESS(Status))
                Status = STATUS_SUCCESS; /* <- sanity */
            else
                WARN(("VSYNC Interrupt control failed Enable(%d), Status(0x%x)", Enable, Status));
            break;
        }
        case DXGK_INTERRUPT_DMA_COMPLETED:
        case DXGK_INTERRUPT_DMA_PREEMPTED:
        case DXGK_INTERRUPT_DMA_FAULTED:
            WARN(("Unexpected interrupt type! %d", InterruptType));
            break;
        default:
            WARN(("UNSUPPORTED interrupt type! %d", InterruptType));
            break;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCreateOverlay(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEOVERLAY  *pCreateOverlay)
{
    LOGF(("ENTER, hAdapter(0x%p)", hAdapter));

    NTSTATUS Status = STATUS_SUCCESS;

#ifdef VBOX_WITH_VIDEOHWACCEL
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
    PVBOXWDDM_OVERLAY pOverlay = (PVBOXWDDM_OVERLAY)vboxWddmMemAllocZero(sizeof (VBOXWDDM_OVERLAY));
    Assert(pOverlay);
    if (pOverlay)
    {
        int rc = vboxVhwaHlpOverlayCreate(pDevExt, pCreateOverlay->VidPnSourceId, &pCreateOverlay->OverlayInfo, pOverlay);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            pCreateOverlay->hOverlay = pOverlay;
        }
        else
        {
            vboxWddmMemFree(pOverlay);
            Status = STATUS_UNSUCCESSFUL;
        }
    }
    else
        Status = STATUS_NO_MEMORY;
#else
    RT_NOREF(hAdapter, pCreateOverlay);
#endif

    LOGF(("LEAVE, hAdapter(0x%p)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyDevice(
    CONST HANDLE  hDevice)
{
    /* DxgkDdiDestroyDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    vboxVDbgBreakFv();

#ifdef VBOX_WITH_VMSVGA
    PVBOXWDDM_DEVICE pDevice = (PVBOXWDDM_DEVICE)hDevice;
    PVBOXMP_DEVEXT pDevExt = pDevice->pAdapter;
    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        GaDeviceDestroy(pDevExt->pGa, pDevice);
    }
#endif

    vboxWddmMemFree(hDevice);

    LOGF(("LEAVE, "));

    return STATUS_SUCCESS;
}



/*
 * DxgkDdiOpenAllocation
 */
NTSTATUS
APIENTRY
DxgkDdiOpenAllocation(
    CONST HANDLE  hDevice,
    CONST DXGKARG_OPENALLOCATION  *pOpenAllocation)
{
    /* DxgkDdiOpenAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    vboxVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_DEVICE pDevice = (PVBOXWDDM_DEVICE)hDevice;
    PVBOXMP_DEVEXT pDevExt = pDevice->pAdapter;
    PVBOXWDDM_RCINFO pRcInfo = NULL;
    if (pOpenAllocation->PrivateDriverSize)
    {
        Assert(pOpenAllocation->pPrivateDriverData);
        if (pOpenAllocation->PrivateDriverSize == sizeof (VBOXWDDM_RCINFO))
        {
            pRcInfo = (PVBOXWDDM_RCINFO)pOpenAllocation->pPrivateDriverData;
            Assert(pRcInfo->cAllocInfos == pOpenAllocation->NumAllocations);
        }
        else
        {
            WARN(("Invalid PrivateDriverSize %d", pOpenAllocation->PrivateDriverSize));
            Status = STATUS_INVALID_PARAMETER;
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        UINT i = 0;
        for (; i < pOpenAllocation->NumAllocations; ++i)
        {
            DXGK_OPENALLOCATIONINFO* pInfo = &pOpenAllocation->pOpenAllocation[i];
#ifdef VBOX_WITH_VMSVGA3D_DX
            Assert(   pInfo->PrivateDriverDataSize == sizeof(VBOXDXALLOCATIONDESC)
                   || pInfo->PrivateDriverDataSize == sizeof(VBOXWDDM_ALLOCINFO));
#else
            Assert(pInfo->PrivateDriverDataSize == sizeof (VBOXWDDM_ALLOCINFO));
#endif
            Assert(pInfo->pPrivateDriverData);
            PVBOXWDDM_ALLOCATION pAllocation = vboxWddmGetAllocationFromHandle(pDevExt, pInfo->hAllocation);
            if (!pAllocation)
            {
                WARN(("invalid handle"));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

#ifdef DEBUG
            Assert(!pAllocation->fAssumedDeletion);
#endif
            if (pRcInfo)
            {
                Assert(pAllocation->enmType == VBOXWDDM_ALLOC_TYPE_UMD_RC_GENERIC);

                if (pInfo->PrivateDriverDataSize != sizeof (VBOXWDDM_ALLOCINFO)
                        || !pInfo->pPrivateDriverData)
                {
                    WARN(("invalid data size"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

#ifdef VBOX_WITH_VIDEOHWACCEL
                PVBOXWDDM_ALLOCINFO pAllocInfo = (PVBOXWDDM_ALLOCINFO)pInfo->pPrivateDriverData;

                if (pRcInfo->RcDesc.fFlags.Overlay)
                {
                    /* we have queried host for some surface info, like pitch & size,
                     * need to return it back to the UMD (User Mode Drive) */
                    pAllocInfo->SurfDesc = pAllocation->AllocData.SurfDesc;
                    /* success, just continue */
                }
#endif
            }

            KIRQL OldIrql;
            PVBOXWDDM_OPENALLOCATION pOa;
            KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
            pOa = VBoxWddmOaSearchLocked(pDevice, pAllocation);
            if (pOa)
            {
                ++pOa->cOpens;
                ++pAllocation->cOpens;
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
            }
            else
            {
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
                pOa = (PVBOXWDDM_OPENALLOCATION)vboxWddmMemAllocZero(sizeof (VBOXWDDM_OPENALLOCATION));
                if (!pOa)
                {
                    WARN(("failed to allocation alloc info"));
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                pOa->hAllocation = pInfo->hAllocation;
                pOa->pAllocation = pAllocation;
                pOa->pDevice = pDevice;
                pOa->cOpens = 1;

                PVBOXWDDM_OPENALLOCATION pConcurrentOa;
                KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
                pConcurrentOa = VBoxWddmOaSearchLocked(pDevice, pAllocation);
                if (!pConcurrentOa)
                    InsertHeadList(&pAllocation->OpenList, &pOa->ListEntry);
                else
                    ++pConcurrentOa->cOpens;
                ++pAllocation->cOpens;
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
                if (pConcurrentOa)
                {
                    vboxWddmMemFree(pOa);
                    pOa = pConcurrentOa;
                }
            }

            pInfo->hDeviceSpecificAllocation = pOa;
        }

        if (Status != STATUS_SUCCESS)
        {
            for (UINT j = 0; j < i; ++j)
            {
                DXGK_OPENALLOCATIONINFO* pInfo2Free = &pOpenAllocation->pOpenAllocation[j];
                PVBOXWDDM_OPENALLOCATION pOa2Free = (PVBOXWDDM_OPENALLOCATION)pInfo2Free->hDeviceSpecificAllocation;
                VBoxWddmOaRelease(pOa2Free);
            }
        }
    }
    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCloseAllocation(
    CONST HANDLE  hDevice,
    CONST DXGKARG_CLOSEALLOCATION*  pCloseAllocation)
{
    RT_NOREF(hDevice);
    /* DxgkDdiCloseAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    vboxVDbgBreakFv();

    for (UINT i = 0; i < pCloseAllocation->NumAllocations; ++i)
    {
        PVBOXWDDM_OPENALLOCATION pOa2Free = (PVBOXWDDM_OPENALLOCATION)pCloseAllocation->pOpenHandleList[i];
        PVBOXWDDM_ALLOCATION pAllocation = pOa2Free->pAllocation;
        Assert(pAllocation->cShRcRefs >= pOa2Free->cShRcRefs);
        pAllocation->cShRcRefs -= pOa2Free->cShRcRefs;
        VBoxWddmOaRelease(pOa2Free);
    }

    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return STATUS_SUCCESS;
}

#define VBOXVDMACMD_DMA_PRESENT_BLT_MINSIZE() (VBOXVDMACMD_SIZE(VBOXVDMACMD_DMA_PRESENT_BLT))
#define VBOXVDMACMD_DMA_PRESENT_BLT_SIZE(_c) (VBOXVDMACMD_BODY_FIELD_OFFSET(UINT, VBOXVDMACMD_DMA_PRESENT_BLT, aDstSubRects[_c]))

DECLINLINE(BOOLEAN) vboxWddmPixFormatConversionSupported(D3DDDIFORMAT From, D3DDDIFORMAT To)
{
    Assert(From != D3DDDIFMT_UNKNOWN);
    Assert(To != D3DDDIFMT_UNKNOWN);
    Assert(From == To);
    return From == To;
}

NTSTATUS
APIENTRY
DxgkDdiUpdateOverlay(
    CONST HANDLE  hOverlay,
    CONST DXGKARG_UPDATEOVERLAY  *pUpdateOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;

#ifdef VBOX_WITH_VIDEOHWACCEL
    PVBOXWDDM_OVERLAY pOverlay = (PVBOXWDDM_OVERLAY)hOverlay;
    AssertPtr(pOverlay);
    int rc = vboxVhwaHlpOverlayUpdate(pOverlay, &pUpdateOverlay->OverlayInfo);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        Status = STATUS_UNSUCCESSFUL;
#else
    RT_NOREF(hOverlay, pUpdateOverlay);
#endif

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));
    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiFlipOverlay(
    CONST HANDLE  hOverlay,
    CONST DXGKARG_FLIPOVERLAY  *pFlipOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;

#ifdef VBOX_WITH_VIDEOHWACCEL
    PVBOXWDDM_OVERLAY pOverlay = (PVBOXWDDM_OVERLAY)hOverlay;
    AssertPtr(pOverlay);
    int rc = vboxVhwaHlpOverlayFlip(pOverlay, pFlipOverlay);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        Status = STATUS_UNSUCCESSFUL;
#else
    RT_NOREF(hOverlay, pFlipOverlay);
#endif

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyOverlay(
    CONST HANDLE  hOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;

#ifdef VBOX_WITH_VIDEOHWACCEL
    PVBOXWDDM_OVERLAY pOverlay = (PVBOXWDDM_OVERLAY)hOverlay;
    AssertPtr(pOverlay);
    int rc = vboxVhwaHlpOverlayDestroy(pOverlay);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        vboxWddmMemFree(pOverlay);
    else
        Status = STATUS_UNSUCCESSFUL;
#else
    RT_NOREF(hOverlay);
#endif

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));

    return Status;
}

/**
 * DxgkDdiCreateContext
 */
NTSTATUS
APIENTRY
DxgkDdiCreateContext(
    CONST HANDLE  hDevice,
    DXGKARG_CREATECONTEXT  *pCreateContext)
{
    /* DxgkDdiCreateContext should be made pageable */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    vboxVDbgBreakFv();

    if (pCreateContext->NodeOrdinal >= VBOXWDDM_NUM_NODES)
    {
        WARN(("Invalid NodeOrdinal (%d), expected to be less that (%d)\n", pCreateContext->NodeOrdinal, VBOXWDDM_NUM_NODES));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    PVBOXWDDM_DEVICE pDevice = (PVBOXWDDM_DEVICE)hDevice;
    PVBOXMP_DEVEXT pDevExt = pDevice->pAdapter;
    PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)vboxWddmMemAllocZero(sizeof (VBOXWDDM_CONTEXT));
    Assert(pContext);
    if (pContext)
    {
        pContext->pDevice = pDevice;
        pContext->hContext = pCreateContext->hContext;
        pContext->EngineAffinity = pCreateContext->EngineAffinity;
        pContext->NodeOrdinal = pCreateContext->NodeOrdinal;
        vboxVideoCmCtxInitEmpty(&pContext->CmContext);
        if (pCreateContext->Flags.SystemContext || pCreateContext->PrivateDriverDataSize == 0)
        {
            Assert(pCreateContext->PrivateDriverDataSize == 0);
            Assert(!pCreateContext->pPrivateDriverData);
            Assert(pCreateContext->Flags.Value <= 2); /* 2 is a GDI context in Win7 */
            pContext->enmType = VBOXWDDM_CONTEXT_TYPE_SYSTEM;

            if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VBOX)
            {
                for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    vboxWddmDisplaySettingsCheckPos(pDevExt, i);
                }
            }
            Status = STATUS_SUCCESS;
        }
        else
        {
            Assert(pCreateContext->Flags.Value == 0);
            Assert(pCreateContext->PrivateDriverDataSize == sizeof (VBOXWDDM_CREATECONTEXT_INFO));
            Assert(pCreateContext->pPrivateDriverData);
            if (pCreateContext->PrivateDriverDataSize == sizeof (VBOXWDDM_CREATECONTEXT_INFO))
            {
                PVBOXWDDM_CREATECONTEXT_INFO pInfo = (PVBOXWDDM_CREATECONTEXT_INFO)pCreateContext->pPrivateDriverData;
                switch (pInfo->enmType)
                {
                    case VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE:
                    {
                        pContext->enmType = pInfo->enmType;
                        ASMAtomicIncU32(&pDevExt->cContextsDispIfResize);
                        break;
                    }
                    case VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS:
                    {
                        pContext->enmType = pInfo->enmType;
                        Status = vboxVideoCmCtxAdd(&pDevice->pAdapter->SeamlessCtxMgr, &pContext->CmContext, (HANDLE)pInfo->u.vbox.hUmEvent, pInfo->u.vbox.u64UmInfo);
                        if (!NT_SUCCESS(Status))
                        {
                            WARN(("vboxVideoCmCtxAdd failed, Status 0x%x", Status));
                        }
                        break;
                    }
#ifdef VBOX_WITH_VMSVGA
                    case VBOXWDDM_CONTEXT_TYPE_GA_3D:
                    {
                        pContext->enmType = VBOXWDDM_CONTEXT_TYPE_GA_3D;
                        Status = GaContextCreate(pDevExt->pGa, pInfo, pContext);
                        break;
                    }
#endif
#ifdef VBOX_WITH_VMSVGA3D_DX
                    case VBOXWDDM_CONTEXT_TYPE_VMSVGA_D3D:
                    {
                        /* VMSVGA_D3D context type shares some code with GA_3D, because both work with VMSVGA GPU. */
                        pContext->enmType = VBOXWDDM_CONTEXT_TYPE_VMSVGA_D3D;
                        Status = GaContextCreate(pDevExt->pGa, pInfo, pContext);
                        break;
                    }
#endif
                    default:
                    {
                        WARN(("unsupported context type %d", pInfo->enmType));
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
            }
        }

        if (Status == STATUS_SUCCESS)
        {
            pCreateContext->hContext = pContext;
            pCreateContext->ContextInfo.DmaBufferSize = VBOXWDDM_C_DMA_BUFFER_SIZE;
            pCreateContext->ContextInfo.DmaBufferSegmentSet = 0;
            pCreateContext->ContextInfo.DmaBufferPrivateDataSize = VBOXWDDM_C_DMA_PRIVATEDATA_SIZE;
            pCreateContext->ContextInfo.AllocationListSize = VBOXWDDM_C_ALLOC_LIST_SIZE;
            pCreateContext->ContextInfo.PatchLocationListSize = VBOXWDDM_C_PATH_LOCATION_LIST_SIZE;
        //#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
        //# error port to Win7 DDI
        //    //pCreateContext->ContextInfo.DmaBufferAllocationGroup = ???;
        //#endif // DXGKDDI_INTERFACE_VERSION
        }
        else
            vboxWddmMemFree(pContext);
    }
    else
        Status = STATUS_NO_MEMORY;

    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyContext(
    CONST HANDLE  hContext)
{
    LOGF(("ENTER, hContext(0x%x)", hContext));
    vboxVDbgBreakFv();
    PVBOXWDDM_CONTEXT pContext = (PVBOXWDDM_CONTEXT)hContext;
    PVBOXMP_DEVEXT pDevExt = pContext->pDevice->pAdapter;
    NTSTATUS Status = STATUS_SUCCESS;

    switch(pContext->enmType)
    {
        case VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE:
        {
            uint32_t cContexts = ASMAtomicDecU32(&pDevExt->cContextsDispIfResize);
            Assert(cContexts < UINT32_MAX/2);
            if (!cContexts)
            {
                if (pDevExt->fDisableTargetUpdate)
                {
                    pDevExt->fDisableTargetUpdate = FALSE;
                    vboxWddmGhDisplayCheckSetInfoEx(pDevExt, true);
                }
            }
            break;
        }
        case VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS:
        {
            Status = vboxVideoCmCtxRemove(&pContext->pDevice->pAdapter->SeamlessCtxMgr, &pContext->CmContext);
            if (!NT_SUCCESS(Status))
                WARN(("vboxVideoCmCtxRemove failed, Status 0x%x", Status));

            Assert(pContext->CmContext.pSession == NULL);
            break;
        }
#ifdef VBOX_WITH_VMSVGA
        case VBOXWDDM_CONTEXT_TYPE_GA_3D:
        {
            Status = GaContextDestroy(pDevExt->pGa, pContext);
            break;
        }
#endif
#ifdef VBOX_WITH_VMSVGA3D_DX
        case VBOXWDDM_CONTEXT_TYPE_VMSVGA_D3D:
        {
            Status = GaContextDestroy(pDevExt->pGa, pContext);
            break;
        }
#endif
        default:
            break;
    }

    Status = vboxVideoAMgrCtxDestroy(&pContext->AllocContext);
    if (NT_SUCCESS(Status))
    {
        Status = vboxVideoCmCtxRemove(&pContext->pDevice->pAdapter->CmMgr, &pContext->CmContext);
        if (NT_SUCCESS(Status))
            vboxWddmMemFree(pContext);
        else
            WARN(("vboxVideoCmCtxRemove failed, Status 0x%x", Status));
    }
    else
        WARN(("vboxVideoAMgrCtxDestroy failed, Status 0x%x", Status));

    LOGF(("LEAVE, hContext(0x%x)", hContext));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiLinkDevice(
    __in CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    __in CONST PVOID  MiniportDeviceContext,
    __inout PLINKED_DEVICE  LinkedDevice
    )
{
    RT_NOREF(PhysicalDeviceObject, MiniportDeviceContext, LinkedDevice);
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
DxgkDdiSetDisplayPrivateDriverFormat(
    CONST HANDLE  hAdapter,
    /*CONST*/ DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT*  pSetDisplayPrivateDriverFormat
    )
{
    RT_NOREF(hAdapter, pSetDisplayPrivateDriverFormat);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY CALLBACK DxgkDdiRestartFromTimeout(IN_CONST_HANDLE hAdapter)
{
    RT_NOREF(hAdapter);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiQueryVidPnHWCapability(
        __in     const HANDLE hAdapter,
        __inout  DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps
      )
{
    RT_NOREF(hAdapter);
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    vboxVDbgBreakF();
    pVidPnHWCaps->VidPnHWCaps.DriverRotation = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiPresentDisplayOnly(
        _In_  const HANDLE hAdapter,
        _In_  const DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly
      )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    vboxVDbgBreakFv();

    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)hAdapter;
#ifdef VBOX_WITH_VMSVGA
    if (pDevExt->enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        return GaDxgkDdiPresentDisplayOnly(hAdapter, pPresentDisplayOnly);
    }
#endif
    PVBOXWDDM_SOURCE pSource = &pDevExt->aSources[pPresentDisplayOnly->VidPnSourceId];
    Assert(pSource->AllocData.Addr.SegmentId == 1);
    VBOXWDDM_ALLOC_DATA SrcAllocData;
    SrcAllocData.SurfDesc.width = pPresentDisplayOnly->Pitch * pPresentDisplayOnly->BytesPerPixel;
    SrcAllocData.SurfDesc.height = ~0UL;
    switch (pPresentDisplayOnly->BytesPerPixel)
    {
        case 4:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_A8R8G8B8;
            break;
        case 3:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_R8G8B8;
            break;
        case 2:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_R5G6B5;
            break;
        case 1:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_P8;
            break;
        default:
            WARN(("Unknown format"));
            SrcAllocData.SurfDesc.format = D3DDDIFMT_UNKNOWN;
            break;
    }
    SrcAllocData.SurfDesc.bpp = pPresentDisplayOnly->BytesPerPixel >> 3;
    SrcAllocData.SurfDesc.pitch = pPresentDisplayOnly->Pitch;
    SrcAllocData.SurfDesc.depth = 1;
    SrcAllocData.SurfDesc.slicePitch = pPresentDisplayOnly->Pitch;
    SrcAllocData.SurfDesc.cbSize =  ~0UL;
    SrcAllocData.Addr.SegmentId = 0;
    SrcAllocData.Addr.pvMem = pPresentDisplayOnly->pSource;
    SrcAllocData.hostID = 0;

    RECT UpdateRect;
    RT_ZERO(UpdateRect);
    BOOLEAN bUpdateRectInited = FALSE;

    for (UINT i = 0; i < pPresentDisplayOnly->NumMoves; ++i)
    {
        if (!bUpdateRectInited)
        {
            UpdateRect = pPresentDisplayOnly->pMoves[i].DestRect;
            bUpdateRectInited = TRUE;
        }
        else
            vboxWddmRectUnite(&UpdateRect, &pPresentDisplayOnly->pMoves[i].DestRect);
        vboxVdmaGgDmaBltPerform(pDevExt, &SrcAllocData, &pPresentDisplayOnly->pMoves[i].DestRect, &pSource->AllocData, &pPresentDisplayOnly->pMoves[i].DestRect);
    }

    for (UINT i = 0; i < pPresentDisplayOnly->NumDirtyRects; ++i)
    {
        RECT *pDirtyRect = &pPresentDisplayOnly->pDirtyRect[i];

        if (pDirtyRect->left >= pDirtyRect->right || pDirtyRect->top >= pDirtyRect->bottom)
        {
            WARN(("Wrong dirty rect (%d, %d)-(%d, %d)",
                pDirtyRect->left, pDirtyRect->top, pDirtyRect->right, pDirtyRect->bottom));
            continue;
        }

        vboxVdmaGgDmaBltPerform(pDevExt, &SrcAllocData, pDirtyRect, &pSource->AllocData, pDirtyRect);

        if (!bUpdateRectInited)
        {
            UpdateRect = *pDirtyRect;
            bUpdateRectInited = TRUE;
        }
        else
            vboxWddmRectUnite(&UpdateRect, pDirtyRect);
    }

    if (bUpdateRectInited && pSource->bVisible)
    {
        VBOXVBVA_OP_WITHLOCK(ReportDirtyRect, pDevExt, pSource, &UpdateRect);
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

static NTSTATUS DxgkDdiStopDeviceAndReleasePostDisplayOwnership(
  _In_   PVOID MiniportDeviceContext,
  _In_   D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
  _Out_  PDXGK_DISPLAY_INFORMATION DisplayInfo
)
{
    RT_NOREF(MiniportDeviceContext, TargetId, DisplayInfo);
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS DxgkDdiSystemDisplayEnable(
        _In_   PVOID MiniportDeviceContext,
        _In_   D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
        _In_   PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
        _Out_  UINT *Width,
        _Out_  UINT *Height,
        _Out_  D3DDDIFORMAT *ColorFormat
      )
{
    RT_NOREF(MiniportDeviceContext, TargetId, Flags, Width, Height, ColorFormat);
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_SUPPORTED;
}

static VOID DxgkDdiSystemDisplayWrite(
  _In_  PVOID MiniportDeviceContext,
  _In_  PVOID Source,
  _In_  UINT SourceWidth,
  _In_  UINT SourceHeight,
  _In_  UINT SourceStride,
  _In_  UINT PositionX,
  _In_  UINT PositionY
)
{
    RT_NOREF(MiniportDeviceContext, Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
}

static NTSTATUS DxgkDdiGetChildContainerId(
  _In_     PVOID MiniportDeviceContext,
  _In_     ULONG ChildUid,
  _Inout_  PDXGK_CHILD_CONTAINER_ID ContainerId
)
{
    RT_NOREF(MiniportDeviceContext, ChildUid, ContainerId);
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiSetPowerComponentFState(
  _In_  const HANDLE DriverContext,
  UINT ComponentIndex,
  UINT FState
)
{
    RT_NOREF(DriverContext, ComponentIndex, FState);
    LOGF(("ENTER, DriverContext(0x%x)", DriverContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, DriverContext(0x%x)", DriverContext));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiPowerRuntimeControlRequest(
  _In_       const HANDLE DriverContext,
  _In_       LPCGUID PowerControlCode,
  _In_opt_   PVOID InBuffer,
  _In_       SIZE_T InBufferSize,
  _Out_opt_  PVOID OutBuffer,
  _In_       SIZE_T OutBufferSize,
  _Out_opt_  PSIZE_T BytesReturned
)
{
    RT_NOREF(DriverContext, PowerControlCode, InBuffer, InBufferSize, OutBuffer, OutBufferSize, BytesReturned);
    LOGF(("ENTER, DriverContext(0x%x)", DriverContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, DriverContext(0x%x)", DriverContext));
    return STATUS_SUCCESS;
}

static NTSTATUS DxgkDdiNotifySurpriseRemoval(
        _In_  PVOID MiniportDeviceContext,
        _In_  DXGK_SURPRISE_REMOVAL_TYPE RemovalType
      )
{
    RT_NOREF(MiniportDeviceContext, RemovalType);
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    vboxVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

static BOOLEAN DxgkDdiInterruptRoutine(const PVOID MiniportDeviceContext,
                                       ULONG MessageNumber)
{
#ifdef VBOX_WITH_VMSVGA
    BOOLEAN const fVMSVGA = GaDxgkDdiInterruptRoutine(MiniportDeviceContext, MessageNumber);
#else
    BOOLEAN const fVMSVGA = FALSE;
#endif

    BOOLEAN const fHGSMI = DxgkDdiInterruptRoutineLegacy(MiniportDeviceContext, MessageNumber);
    return fVMSVGA || fHGSMI;
}

static VOID DxgkDdiDpcRoutine(const PVOID MiniportDeviceContext)
{
    PVBOXMP_DEVEXT pDevExt = (PVBOXMP_DEVEXT)MiniportDeviceContext;

#ifdef VBOX_WITH_VMSVGA
    GaDxgkDdiDpcRoutine(MiniportDeviceContext);
#endif
    DxgkDdiDpcRoutineLegacy(MiniportDeviceContext);

    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
}

static NTSTATUS vboxWddmInitDisplayOnlyDriver(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath)
{
    KMDDOD_INITIALIZATION_DATA DriverInitializationData = {'\0'};

    DriverInitializationData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;

    DriverInitializationData.DxgkDdiAddDevice = DxgkDdiAddDevice;
    DriverInitializationData.DxgkDdiStartDevice = DxgkDdiStartDevice;
    DriverInitializationData.DxgkDdiStopDevice = DxgkDdiStopDevice;
    DriverInitializationData.DxgkDdiRemoveDevice = DxgkDdiRemoveDevice;
    DriverInitializationData.DxgkDdiDispatchIoRequest = DxgkDdiDispatchIoRequest;
    DriverInitializationData.DxgkDdiInterruptRoutine = DxgkDdiInterruptRoutine;
    DriverInitializationData.DxgkDdiDpcRoutine = DxgkDdiDpcRoutine;
    DriverInitializationData.DxgkDdiQueryChildRelations = DxgkDdiQueryChildRelations;
    DriverInitializationData.DxgkDdiQueryChildStatus = DxgkDdiQueryChildStatus;
    DriverInitializationData.DxgkDdiQueryDeviceDescriptor = DxgkDdiQueryDeviceDescriptor;
    DriverInitializationData.DxgkDdiSetPowerState = DxgkDdiSetPowerState;
    DriverInitializationData.DxgkDdiNotifyAcpiEvent = DxgkDdiNotifyAcpiEvent;
    DriverInitializationData.DxgkDdiResetDevice = DxgkDdiResetDevice;
    DriverInitializationData.DxgkDdiUnload = DxgkDdiUnload;
    DriverInitializationData.DxgkDdiQueryInterface = DxgkDdiQueryInterface;
    DriverInitializationData.DxgkDdiControlEtwLogging = DxgkDdiControlEtwLogging;
    DriverInitializationData.DxgkDdiQueryAdapterInfo = DxgkDdiQueryAdapterInfo;
    DriverInitializationData.DxgkDdiSetPalette = DxgkDdiSetPalette;
    DriverInitializationData.DxgkDdiSetPointerPosition = DxgkDdiSetPointerPosition;
    DriverInitializationData.DxgkDdiSetPointerShape = DxgkDdiSetPointerShape;
    DriverInitializationData.DxgkDdiEscape = DxgkDdiEscape;
    DriverInitializationData.DxgkDdiCollectDbgInfo = DxgkDdiCollectDbgInfo;
    DriverInitializationData.DxgkDdiIsSupportedVidPn = DxgkDdiIsSupportedVidPn;
    DriverInitializationData.DxgkDdiRecommendFunctionalVidPn = DxgkDdiRecommendFunctionalVidPn;
    DriverInitializationData.DxgkDdiEnumVidPnCofuncModality = DxgkDdiEnumVidPnCofuncModality;
    DriverInitializationData.DxgkDdiSetVidPnSourceVisibility = DxgkDdiSetVidPnSourceVisibility;
    DriverInitializationData.DxgkDdiCommitVidPn = DxgkDdiCommitVidPn;
    DriverInitializationData.DxgkDdiUpdateActiveVidPnPresentPath = DxgkDdiUpdateActiveVidPnPresentPath;
    DriverInitializationData.DxgkDdiRecommendMonitorModes = DxgkDdiRecommendMonitorModes;
    DriverInitializationData.DxgkDdiQueryVidPnHWCapability = DxgkDdiQueryVidPnHWCapability;
    DriverInitializationData.DxgkDdiPresentDisplayOnly = DxgkDdiPresentDisplayOnly;
    DriverInitializationData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    DriverInitializationData.DxgkDdiSystemDisplayEnable = DxgkDdiSystemDisplayEnable;
    DriverInitializationData.DxgkDdiSystemDisplayWrite = DxgkDdiSystemDisplayWrite;
//    DriverInitializationData.DxgkDdiGetChildContainerId = DxgkDdiGetChildContainerId;
//    DriverInitializationData.DxgkDdiSetPowerComponentFState = DxgkDdiSetPowerComponentFState;
//    DriverInitializationData.DxgkDdiPowerRuntimeControlRequest = DxgkDdiPowerRuntimeControlRequest;
//    DriverInitializationData.DxgkDdiNotifySurpriseRemoval = DxgkDdiNotifySurpriseRemoval;

    /* Display-only driver is not required to report VSYNC.
     * The Microsoft KMDOD driver sample does not implement DxgkDdiControlInterrupt and DxgkDdiGetScanLine.
     * The functions must be either both implemented or none implemented.
     * Windows 10 10586 guests had problems with VSYNC in display-only driver (#8228).
     * Therefore the driver does not implement DxgkDdiControlInterrupt and DxgkDdiGetScanLine.
     */

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject,
                          pRegistryPath,
                          &DriverInitializationData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkInitializeDisplayOnlyDriver failed! Status 0x%x", Status));
    }
    return Status;
}

static NTSTATUS vboxWddmInitFullGraphicsDriver(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath, VBOXVIDEO_HWTYPE enmHwType)
{
    DRIVER_INITIALIZATION_DATA DriverInitializationData = {'\0'};

    // Fill in the DriverInitializationData structure and call DxgkInitialize()
    if (VBoxQueryWinVersion(NULL) >= WINVERSION_8)
        DriverInitializationData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
    else
        DriverInitializationData.Version = DXGKDDI_INTERFACE_VERSION_VISTA_SP1;

    DriverInitializationData.DxgkDdiAddDevice = DxgkDdiAddDevice;
    DriverInitializationData.DxgkDdiStartDevice = DxgkDdiStartDevice;
    DriverInitializationData.DxgkDdiStopDevice = DxgkDdiStopDevice;
    DriverInitializationData.DxgkDdiRemoveDevice = DxgkDdiRemoveDevice;
    DriverInitializationData.DxgkDdiDispatchIoRequest = DxgkDdiDispatchIoRequest;
    DriverInitializationData.DxgkDdiInterruptRoutine  = DxgkDdiInterruptRoutine;
    DriverInitializationData.DxgkDdiDpcRoutine        = DxgkDdiDpcRoutine;

#ifdef VBOX_WITH_VMSVGA
    if (enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
    {
        DriverInitializationData.DxgkDdiPatch             = GaDxgkDdiPatch;
        DriverInitializationData.DxgkDdiSubmitCommand     = GaDxgkDdiSubmitCommand;
        DriverInitializationData.DxgkDdiPreemptCommand    = GaDxgkDdiPreemptCommand;
        DriverInitializationData.DxgkDdiBuildPagingBuffer = GaDxgkDdiBuildPagingBuffer;
        DriverInitializationData.DxgkDdiQueryCurrentFence = GaDxgkDdiQueryCurrentFence;
        DriverInitializationData.DxgkDdiRender            = GaDxgkDdiRender;
        DriverInitializationData.DxgkDdiPresent           = SvgaDxgkDdiPresent;
    }
    else
#endif
    {
        RT_NOREF(enmHwType);

        DriverInitializationData.DxgkDdiPatch             = DxgkDdiPatchLegacy;
        DriverInitializationData.DxgkDdiSubmitCommand     = DxgkDdiSubmitCommandLegacy;
        DriverInitializationData.DxgkDdiPreemptCommand    = DxgkDdiPreemptCommandLegacy;
        DriverInitializationData.DxgkDdiBuildPagingBuffer = DxgkDdiBuildPagingBufferLegacy;
        DriverInitializationData.DxgkDdiQueryCurrentFence = DxgkDdiQueryCurrentFenceLegacy;
        DriverInitializationData.DxgkDdiRender            = DxgkDdiRenderLegacy;
        DriverInitializationData.DxgkDdiPresent           = DxgkDdiPresentLegacy;
    }

    DriverInitializationData.DxgkDdiQueryChildRelations = DxgkDdiQueryChildRelations;
    DriverInitializationData.DxgkDdiQueryChildStatus = DxgkDdiQueryChildStatus;
    DriverInitializationData.DxgkDdiQueryDeviceDescriptor = DxgkDdiQueryDeviceDescriptor;
    DriverInitializationData.DxgkDdiSetPowerState = DxgkDdiSetPowerState;
    DriverInitializationData.DxgkDdiNotifyAcpiEvent = DxgkDdiNotifyAcpiEvent;
    DriverInitializationData.DxgkDdiResetDevice = DxgkDdiResetDevice;
    DriverInitializationData.DxgkDdiUnload = DxgkDdiUnload;
    DriverInitializationData.DxgkDdiQueryInterface = DxgkDdiQueryInterface;
    DriverInitializationData.DxgkDdiControlEtwLogging = DxgkDdiControlEtwLogging;

    DriverInitializationData.DxgkDdiQueryAdapterInfo = DxgkDdiQueryAdapterInfo;
    DriverInitializationData.DxgkDdiCreateDevice = DxgkDdiCreateDevice;
    DriverInitializationData.DxgkDdiCreateAllocation = DxgkDdiCreateAllocation;
    DriverInitializationData.DxgkDdiDestroyAllocation = DxgkDdiDestroyAllocation;
    DriverInitializationData.DxgkDdiDescribeAllocation = DxgkDdiDescribeAllocation;
    DriverInitializationData.DxgkDdiGetStandardAllocationDriverData = DxgkDdiGetStandardAllocationDriverData;
    DriverInitializationData.DxgkDdiAcquireSwizzlingRange = DxgkDdiAcquireSwizzlingRange;
    DriverInitializationData.DxgkDdiReleaseSwizzlingRange = DxgkDdiReleaseSwizzlingRange;

    DriverInitializationData.DxgkDdiSetPalette = DxgkDdiSetPalette;
    DriverInitializationData.DxgkDdiSetPointerPosition = DxgkDdiSetPointerPosition;
    DriverInitializationData.DxgkDdiSetPointerShape = DxgkDdiSetPointerShape;
    DriverInitializationData.DxgkDdiResetFromTimeout = DxgkDdiResetFromTimeout;
    DriverInitializationData.DxgkDdiRestartFromTimeout = DxgkDdiRestartFromTimeout;
    DriverInitializationData.DxgkDdiEscape = DxgkDdiEscape;
    DriverInitializationData.DxgkDdiCollectDbgInfo = DxgkDdiCollectDbgInfo;
    DriverInitializationData.DxgkDdiIsSupportedVidPn = DxgkDdiIsSupportedVidPn;
    DriverInitializationData.DxgkDdiRecommendFunctionalVidPn = DxgkDdiRecommendFunctionalVidPn;
    DriverInitializationData.DxgkDdiEnumVidPnCofuncModality = DxgkDdiEnumVidPnCofuncModality;
    DriverInitializationData.DxgkDdiSetVidPnSourceAddress = DxgkDdiSetVidPnSourceAddress;
    DriverInitializationData.DxgkDdiSetVidPnSourceVisibility = DxgkDdiSetVidPnSourceVisibility;
    DriverInitializationData.DxgkDdiCommitVidPn = DxgkDdiCommitVidPn;
    DriverInitializationData.DxgkDdiUpdateActiveVidPnPresentPath = DxgkDdiUpdateActiveVidPnPresentPath;
    DriverInitializationData.DxgkDdiRecommendMonitorModes = DxgkDdiRecommendMonitorModes;
    DriverInitializationData.DxgkDdiRecommendVidPnTopology = DxgkDdiRecommendVidPnTopology;
    DriverInitializationData.DxgkDdiGetScanLine = DxgkDdiGetScanLine;
    DriverInitializationData.DxgkDdiStopCapture = DxgkDdiStopCapture;
    DriverInitializationData.DxgkDdiControlInterrupt = DxgkDdiControlInterrupt;
    DriverInitializationData.DxgkDdiCreateOverlay = DxgkDdiCreateOverlay;

    DriverInitializationData.DxgkDdiDestroyDevice = DxgkDdiDestroyDevice;
    DriverInitializationData.DxgkDdiOpenAllocation = DxgkDdiOpenAllocation;
    DriverInitializationData.DxgkDdiCloseAllocation = DxgkDdiCloseAllocation;

    DriverInitializationData.DxgkDdiUpdateOverlay = DxgkDdiUpdateOverlay;
    DriverInitializationData.DxgkDdiFlipOverlay = DxgkDdiFlipOverlay;
    DriverInitializationData.DxgkDdiDestroyOverlay = DxgkDdiDestroyOverlay;

    DriverInitializationData.DxgkDdiCreateContext = DxgkDdiCreateContext;
    DriverInitializationData.DxgkDdiDestroyContext = DxgkDdiDestroyContext;

    DriverInitializationData.DxgkDdiLinkDevice = NULL; //DxgkDdiLinkDevice;
    DriverInitializationData.DxgkDdiSetDisplayPrivateDriverFormat = DxgkDdiSetDisplayPrivateDriverFormat;

    if (DriverInitializationData.Version >= DXGKDDI_INTERFACE_VERSION_WIN7)
    {
        DriverInitializationData.DxgkDdiQueryVidPnHWCapability = DxgkDdiQueryVidPnHWCapability;
    }

    NTSTATUS Status = DxgkInitialize(pDriverObject,
                          pRegistryPath,
                          &DriverInitializationData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkInitialize failed! Status 0x%x", Status));
    }
    return Status;
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    PAGED_CODE();

    vboxVDbgBreakFv();

    int irc = RTR0Init(0);
    if (RT_FAILURE(irc))
    {
        RTLogBackdoorPrintf("VBoxWddm: RTR0Init failed: %Rrc!\n", irc);
        return STATUS_UNSUCCESSFUL;
    }

#ifdef DEBUG
#define VBOXWDDM_BUILD_TYPE "dbg"
#else
#define VBOXWDDM_BUILD_TYPE "rel"
#endif

    LOGREL(("VBox WDDM Driver for Windows %s version %d.%d.%dr%d %s, %d bit; Built %s %s",
            VBoxQueryWinVersion(NULL) >= WINVERSION_8 ? "8+" : "Vista and 7",
            VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV,
            VBOXWDDM_BUILD_TYPE,
            (sizeof (void*) << 3), __DATE__, __TIME__));

    if (   !ARGUMENT_PRESENT(DriverObject)
        || !ARGUMENT_PRESENT(RegistryPath))
        return STATUS_INVALID_PARAMETER;

    vboxWddmLoggerCreate(RegistryPath);
    vboxWddmDrvCfgInit(RegistryPath);

    ULONG major, minor, build;
    BOOLEAN fCheckedBuild = PsGetVersion(&major, &minor, &build, NULL); NOREF(fCheckedBuild);

    LOGREL(("OsVersion(%d, %d, %d)", major, minor, build));

    NTSTATUS Status = STATUS_SUCCESS;
    /* Initialize VBoxGuest library, which is used for requests which go through VMMDev. */
    int rc = VbglR0InitClient();
    if (RT_SUCCESS(rc))
    {
        /* Check whether 3D is provided by the host. */
        VBOXVIDEO_HWTYPE enmHwType = VBOXVIDEO_HWTYPE_VBOX;
        BOOL f3DSupported = FALSE;

        if (VBoxVGACfgAvailable())
        {
            /* New configuration query interface is available. */
            uint32_t u32;
            if (VBoxVGACfgQuery(VBE_DISPI_CFG_ID_VERSION, &u32, 0))
            {
                LOGREL(("WDDM: VGA configuration version %d", u32));
            }

            VBoxVGACfgQuery(VBE_DISPI_CFG_ID_3D, &u32, 0);
            f3DSupported = RT_BOOL(u32);

            VBoxVGACfgQuery(VBE_DISPI_CFG_ID_VMSVGA, &u32, 0);
            if (u32)
            {
                enmHwType = VBOXVIDEO_HWTYPE_VMSVGA;
            }

            BOOL fVGPU10 = FALSE;
            VBoxVGACfgQuery(VBE_DISPI_CFG_ID_VMSVGA_DX, &u32, 0);
            if (u32)
            {
                fVGPU10 = TRUE;
            }
            LOGREL(("WDDM: VGA configuration: 3D %d, hardware type %d, VGPU10 %d", f3DSupported, enmHwType, fVGPU10));
            if (!fVGPU10)
                f3DSupported = FALSE;
        }

        if (enmHwType == VBOXVIDEO_HWTYPE_VBOX)
        {
            /* No 3D for legacy adapter. */
            f3DSupported = FALSE;
        }
        else if (enmHwType == VBOXVIDEO_HWTYPE_VMSVGA)
        {
            /* Nothing. */
        }
        else
        {
            /* No supported hardware, fallback to VBox 2D only. */
            enmHwType = VBOXVIDEO_HWTYPE_VBOX;
            f3DSupported = FALSE;
        }

        LOGREL(("WDDM: 3D is %ssupported, hardware type %d", f3DSupported? "": "not ", enmHwType));

        if (NT_SUCCESS(Status))
        {
            if (!f3DSupported)
            {
                /* No 3D support by the host. */
                if (VBoxQueryWinVersion(NULL) >= WINVERSION_8)
                {
                    /* Use display only driver for Win8+. */
                    g_VBoxDisplayOnly = 1;

                    /* Black list some builds. */
                    if (major == 6 && minor == 4 && build == 9841)
                    {
                        /* W10 Technical preview crashes with display-only driver. */
                        LOGREL(("3D is NOT supported by the host, fallback to the system video driver."));
                        Status = STATUS_UNSUCCESSFUL;
                    }
                    else
                    {
                        LOGREL(("3D is NOT supported by the host, falling back to display-only mode.."));
                    }
                }
                else
                {
                    LOGREL(("3D is NOT supported by the host, but it is NOT required for Windows Vista/7 guests, continue with disabled 3D."));
                }
            }
        }

        if (NT_SUCCESS(Status))
        {
            if (g_VBoxDisplayOnly)
            {
                Status = vboxWddmInitDisplayOnlyDriver(DriverObject, RegistryPath);
            }
            else
            {
                Status = vboxWddmInitFullGraphicsDriver(DriverObject, RegistryPath, enmHwType);
            }

            if (NT_SUCCESS(Status))
            {
                /*
                 * Successfully initialized the driver.
                 */
                return Status;
            }

            /*
             * Cleanup on failure.
             */
        }
        else
            LOGREL(("Aborting the video driver load due to 3D support missing"));

        VbglR0TerminateClient();
    }
    else
    {
        WARN(("VbglR0InitClient failed, rc(%d)", rc));
        Status = STATUS_UNSUCCESSFUL;
    }

    AssertRelease(!NT_SUCCESS(Status));

    PRTLOGGER pLogger = RTLogSetDefaultInstance(NULL);

    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }

    return Status;
}
