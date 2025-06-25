/* $Id: VBoxMPVidPn.cpp 109969 2025-06-25 16:36:39Z dmitrii.grigorev@oracle.com $ */
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
#include "VBoxMPVidPn.h"
#include "common/VBoxMPCommon.h"


static NTSTATUS vboxVidPnCheckMonitorModes(PVBOXMP_DEVEXT pDevExt, uint32_t u32Target, const CR_SORTARRAY *pSupportedTargetModes = NULL);

static D3DDDIFORMAT vboxWddmCalcPixelFormat(const VIDEO_MODE_INFORMATION *pInfo)
{
    switch (pInfo->BitsPerPlane)
    {
        case 32:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xFF0000 && pInfo->GreenMask == 0xFF00 && pInfo->BlueMask == 0xFF)
                    return D3DDDIFMT_A8R8G8B8;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                      pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 24:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xFF0000 && pInfo->GreenMask == 0xFF00 && pInfo->BlueMask == 0xFF)
                    return D3DDDIFMT_R8G8B8;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                     pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 16:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xF800 && pInfo->GreenMask == 0x7E0 && pInfo->BlueMask == 0x1F)
                    return D3DDDIFMT_R5G6B5;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                      pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 8:
            if((pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && (pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                return D3DDDIFMT_P8;
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        default:
            WARN(("unsupported bpp(%d)", pInfo->BitsPerPlane));
            AssertBreakpoint();
            break;
    }

    return D3DDDIFMT_UNKNOWN;
}

static int vboxWddmResolutionFind(const D3DKMDT_2DREGION *pResolutions, int cResolutions, const D3DKMDT_2DREGION *pRes)
{
    for (int i = 0; i < cResolutions; ++i)
    {
        const D3DKMDT_2DREGION *pResolution = &pResolutions[i];
        if (pResolution->cx == pRes->cx && pResolution->cy == pRes->cy)
            return i;
    }
    return -1;
}

static bool vboxWddmVideoModesMatch(const VIDEO_MODE_INFORMATION *pMode1, const VIDEO_MODE_INFORMATION *pMode2)
{
    return pMode1->VisScreenHeight == pMode2->VisScreenHeight
            && pMode1->VisScreenWidth == pMode2->VisScreenWidth
            && pMode1->BitsPerPlane == pMode2->BitsPerPlane;
}

static int vboxWddmVideoModeFind(const VIDEO_MODE_INFORMATION *pModes, int cModes, const VIDEO_MODE_INFORMATION *pM)
{
    for (int i = 0; i < cModes; ++i)
    {
        const VIDEO_MODE_INFORMATION *pMode = &pModes[i];
        if (vboxWddmVideoModesMatch(pMode, pM))
            return i;
    }
    return -1;
}

static NTSTATUS vboxVidPnPopulateVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVsi,
        const RTRECTSIZE *pResolution,
        ULONG VSync)
{
    NTSTATUS Status = STATUS_SUCCESS;

    pVsi->VideoStandard  = D3DKMDT_VSS_OTHER;
    pVsi->ActiveSize.cx = pResolution->cx;
    pVsi->ActiveSize.cy = pResolution->cy;
    pVsi->TotalSize = pVsi->ActiveSize;
    if (VBOXWDDM_IS_DISPLAYONLY())
    {
        /* VSYNC is not implemented in display-only mode (#8228).
         * In this case Windows checks that frequencies are not specified.
         */
        pVsi->VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVsi->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVsi->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVsi->HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVsi->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    }
    else
    {
        pVsi->VSyncFreq.Numerator = VSync * 1000;
        pVsi->VSyncFreq.Denominator = 1000;
        pVsi->PixelRate = pVsi->TotalSize.cx * pVsi->TotalSize.cy * VSync;
        pVsi->HSyncFreq.Numerator = (UINT)((VSync * pVsi->TotalSize.cy) * 1000);
        pVsi->HSyncFreq.Denominator = 1000;
    }
    pVsi->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    return Status;
}

BOOLEAN vboxVidPnMatchVideoSignal(const D3DKMDT_VIDEO_SIGNAL_INFO *pVsi1, const D3DKMDT_VIDEO_SIGNAL_INFO *pVsi2)
{
    if (pVsi1->VideoStandard != pVsi2->VideoStandard)
        return FALSE;
    if (pVsi1->TotalSize.cx != pVsi2->TotalSize.cx)
        return FALSE;
    if (pVsi1->TotalSize.cy != pVsi2->TotalSize.cy)
        return FALSE;
    if (pVsi1->ActiveSize.cx != pVsi2->ActiveSize.cx)
        return FALSE;
    if (pVsi1->ActiveSize.cy != pVsi2->ActiveSize.cy)
        return FALSE;
    if (pVsi1->VSyncFreq.Numerator != pVsi2->VSyncFreq.Numerator)
        return FALSE;
    if (pVsi1->VSyncFreq.Denominator != pVsi2->VSyncFreq.Denominator)
        return FALSE;
    if (pVsi1->HSyncFreq.Numerator != pVsi2->HSyncFreq.Numerator)
        return FALSE;
    if (pVsi1->HSyncFreq.Denominator != pVsi2->HSyncFreq.Denominator)
        return FALSE;
    if (pVsi1->PixelRate != pVsi2->PixelRate)
        return FALSE;
    if (pVsi1->ScanLineOrdering != pVsi2->ScanLineOrdering)
        return FALSE;

    return TRUE;
}

static void vboxVidPnPopulateSourceModeInfo(D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, const RTRECTSIZE *pSize, D3DDDIFORMAT PixelFormat = D3DDDIFMT_A8R8G8B8)
{
    /* this is a graphics mode */
    pNewVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
    pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pSize->cx;
    pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pSize->cy;
    pNewVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
    pNewVidPnSourceModeInfo->Format.Graphics.Stride = pSize->cx * 4;
    pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat = PixelFormat;
    Assert(pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat != D3DDDIFMT_UNKNOWN);
    pNewVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
    if (pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat == D3DDDIFMT_P8)
        pNewVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_SETTABLEPALETTE;
    else
        pNewVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
}

static void vboxVidPnPopulateMonitorModeInfo(D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode, const RTRECTSIZE *pResolution)
{
    vboxVidPnPopulateVideoSignalInfo(&pMonitorSourceMode->VideoSignalInfo, pResolution, g_RefreshRate);
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 0;
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_NOTPREFERRED;
}

static NTSTATUS vboxVidPnPopulateTargetModeInfo(D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, const RTRECTSIZE *pResolution)
{
    pNewVidPnTargetModeInfo->Preference = D3DKMDT_MP_NOTPREFERRED;
    return vboxVidPnPopulateVideoSignalInfo(&pNewVidPnTargetModeInfo->VideoSignalInfo, pResolution, g_RefreshRate);
}

void VBoxVidPnStTargetCleanup(PVBOXWDDM_SOURCE paSources, uint32_t cScreens, PVBOXWDDM_TARGET pTarget)
{
    RT_NOREF(cScreens);
    if (pTarget->VidPnSourceId == D3DDDI_ID_UNINITIALIZED)
        return;

    Assert(pTarget->VidPnSourceId < cScreens);

    PVBOXWDDM_SOURCE pSource = &paSources[pTarget->VidPnSourceId];
    if (!pSource)
        return;
    Assert(pSource->cTargets);
    Assert(ASMBitTest(pSource->aTargetMap, pTarget->u32Id));
    ASMBitClear(pSource->aTargetMap, pTarget->u32Id);
    pSource->cTargets--;
    pTarget->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

    pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
    pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
}

void VBoxVidPnStSourceTargetAdd(PVBOXWDDM_SOURCE paSources, uint32_t cScreens, PVBOXWDDM_SOURCE pSource, PVBOXWDDM_TARGET pTarget)
{
    if (pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId)
        return;

    VBoxVidPnStTargetCleanup(paSources, cScreens, pTarget);

    ASMBitSet(pSource->aTargetMap, pTarget->u32Id);
    pSource->cTargets++;
    pTarget->VidPnSourceId = pSource->AllocData.SurfDesc.VidPnSourceId;

    pTarget->fBlankedByPowerOff = RT_BOOL(pSource->bBlankedByPowerOff);
    LOG(("src %d and tgt %d are now blank %d",
        pSource->AllocData.SurfDesc.VidPnSourceId, pTarget->u32Id, pTarget->fBlankedByPowerOff));

    pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
    pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
}

void VBoxVidPnStTIterInit(PVBOXWDDM_SOURCE pSource, PVBOXWDDM_TARGET paTargets, uint32_t cTargets, VBOXWDDM_TARGET_ITER *pIter)
{
    pIter->pSource = pSource;
    pIter->paTargets = paTargets;
    pIter->cTargets = cTargets;
    pIter->i = 0;
    pIter->c = 0;
}

PVBOXWDDM_TARGET VBoxVidPnStTIterNext(VBOXWDDM_TARGET_ITER *pIter)
{
    PVBOXWDDM_SOURCE pSource = pIter->pSource;
    if (pSource->cTargets <= pIter->c)
        return NULL;

    int i =  (!pIter->c) ? ASMBitFirstSet(pSource->aTargetMap, pIter->cTargets)
            : ASMBitNextSet(pSource->aTargetMap, pIter->cTargets, pIter->i);
    if (i < 0)
        STOP_FATAL();

    pIter->i = (uint32_t)i;
    pIter->c++;
    return &pIter->paTargets[i];
}

void VBoxVidPnStSourceCleanup(PVBOXWDDM_SOURCE paSources, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, PVBOXWDDM_TARGET paTargets, uint32_t cTargets)
{
    PVBOXWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    VBOXWDDM_TARGET_ITER Iter;
    VBoxVidPnStTIterInit(pSource, paTargets, cTargets, &Iter);
    for (PVBOXWDDM_TARGET pTarget = VBoxVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = VBoxVidPnStTIterNext(&Iter))
    {
        Assert(pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId);
        VBoxVidPnStTargetCleanup(paSources, cTargets, pTarget);
        /* iterator is not safe wrt target removal, reinit it */
        VBoxVidPnStTIterInit(pSource, paTargets, cTargets, &Iter);
    }
}

void VBoxVidPnStCleanup(PVBOXWDDM_SOURCE paSources, PVBOXWDDM_TARGET paTargets, uint32_t cScreens)
{
    for (UINT i = 0; i < cScreens; ++i)
    {
        PVBOXWDDM_TARGET pTarget = &paTargets[i];
        VBoxVidPnStTargetCleanup(paSources, cScreens, pTarget);
    }
}

void VBoxVidPnAllocDataInit(VBOXWDDM_ALLOC_DATA *pData, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    memset(pData, 0, sizeof (*pData));
    pData->SurfDesc.VidPnSourceId = VidPnSourceId;
    pData->Addr.offVram = VBOXVIDEOOFFSET_VOID;
}

void VBoxVidPnSourceInit(PVBOXWDDM_SOURCE pSource, const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, uint8_t u8SyncState)
{
    memset(pSource, 0, sizeof (*pSource));
    VBoxVidPnAllocDataInit(&pSource->AllocData, VidPnSourceId);
    pSource->u8SyncState = (u8SyncState & VBOXWDDM_HGSYNC_F_SYNCED_ALL);
}

void VBoxVidPnTargetInit(PVBOXWDDM_TARGET pTarget, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, uint8_t u8SyncState)
{
    memset(pTarget, 0, sizeof (*pTarget));
    pTarget->u32Id = VidPnTargetId;
    pTarget->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    pTarget->u8SyncState = (u8SyncState & VBOXWDDM_HGSYNC_F_SYNCED_ALL);
}

void VBoxVidPnSourcesInit(PVBOXWDDM_SOURCE pSources, uint32_t cScreens, uint8_t u8SyncState)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        VBoxVidPnSourceInit(&pSources[i], i, u8SyncState);
}

void VBoxVidPnTargetsInit(PVBOXWDDM_TARGET pTargets, uint32_t cScreens, uint8_t u8SyncState)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        VBoxVidPnTargetInit(&pTargets[i], i, u8SyncState);
}

void VBoxVidPnSourceCopy(VBOXWDDM_SOURCE *pDst, const VBOXWDDM_SOURCE *pSrc)
{
    uint8_t u8SyncState = pDst->u8SyncState;
    *pDst = *pSrc;
    pDst->u8SyncState &= u8SyncState;
}

void VBoxVidPnTargetCopy(VBOXWDDM_TARGET *pDst, const VBOXWDDM_TARGET *pSrc)
{
    uint8_t u8SyncState = pDst->u8SyncState;
    *pDst = *pSrc;
    pDst->u8SyncState &= u8SyncState;
}

void VBoxVidPnSourcesCopy(VBOXWDDM_SOURCE *pDst, const VBOXWDDM_SOURCE *pSrc, uint32_t cScreens)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        VBoxVidPnSourceCopy(&pDst[i], &pSrc[i]);
}

void VBoxVidPnTargetsCopy(VBOXWDDM_TARGET *pDst, const VBOXWDDM_TARGET *pSrc, uint32_t cScreens)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        VBoxVidPnTargetCopy(&pDst[i], &pSrc[i]);
}

void VBoxDumpSourceTargetArrays(VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets, uint32_t cScreens)
{
    RT_NOREF(paSources, paTargets, cScreens);

    for (uint32_t i = 0; i < cScreens; i++)
    {
        LOG_EXACT(("source [%d] Sync 0x%x, cTgt %d, TgtMap0 0x%x, TgtRep %d, blanked %d\n",
            i, paSources[i].u8SyncState, paSources[i].cTargets, paSources[i].aTargetMap[0], paSources[i].fTargetsReported, paSources[i].bBlankedByPowerOff));

        LOG_EXACT(("target [%d] Sync 0x%x, VidPnSourceId %d, blanked %d\n",
            i, paTargets[i].u8SyncState, paTargets[i].VidPnSourceId, paTargets[i].fBlankedByPowerOff));
    }
}

static D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE vboxVidPnCofuncModalityCurrentPathPivot(D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot,
                    const DXGK_ENUM_PIVOT *pPivot,
                    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    switch (enmPivot)
    {
        case D3DKMDT_EPT_VIDPNSOURCE:
            if (pPivot->VidPnSourceId == VidPnSourceId)
                return D3DKMDT_EPT_VIDPNSOURCE;
            if (pPivot->VidPnSourceId == D3DDDI_ID_ALL)
            {
#ifdef DEBUG_misha
                AssertFailed();
#endif
                return D3DKMDT_EPT_VIDPNSOURCE;
            }
            return D3DKMDT_EPT_NOPIVOT;
        case D3DKMDT_EPT_VIDPNTARGET:
            if (pPivot->VidPnTargetId == VidPnTargetId)
                return D3DKMDT_EPT_VIDPNTARGET;
            if (pPivot->VidPnTargetId == D3DDDI_ID_ALL)
            {
#ifdef DEBUG_misha
                AssertFailed();
#endif
                return D3DKMDT_EPT_VIDPNTARGET;
            }
            return D3DKMDT_EPT_NOPIVOT;
        case D3DKMDT_EPT_SCALING:
        case D3DKMDT_EPT_ROTATION:
        case D3DKMDT_EPT_NOPIVOT:
            return D3DKMDT_EPT_NOPIVOT;
        default:
            WARN(("unexpected pivot"));
            return D3DKMDT_EPT_NOPIVOT;
    }
}

NTSTATUS vboxVidPnQueryPinnedTargetMode(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, RTRECTSIZE *pSize)
{
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;
    pSize->cx = 0;
    pSize->cy = 0;
    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquireTargetModeSet failed Status(0x%x)", Status));
        return Status;
    }

    CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;
    Status = pCurVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
    if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
    {
        pPinnedVidPnTargetModeInfo = NULL;
        Status = STATUS_SUCCESS;
    }
    else if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));
    }
    else
    {
        Assert(pPinnedVidPnTargetModeInfo);
        pSize->cx = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cx;
        pSize->cy = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cy;
        NTSTATUS rcNt2 = pCurVidPnTargetModeSetInterface->pfnReleaseModeInfo(hCurVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        AssertNtStatus(rcNt2);
    }

    NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    AssertNtStatusSuccess(rcNt2);

    return Status;
}

NTSTATUS vboxVidPnQueryPinnedSourceMode(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RTRECTSIZE *pSize)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;
    pSize->cx = 0;
    pSize->cy = 0;
    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquireSourceModeSet failed Status(0x%x)", Status));
        return Status;
    }

    CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;
    Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
    if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
    {
        pPinnedVidPnSourceModeInfo = NULL;
        Status = STATUS_SUCCESS;
    }
    else if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));
    }
    else
    {
        Assert(pPinnedVidPnSourceModeInfo);
        pSize->cx = pPinnedVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize.cx;
        pSize->cy = pPinnedVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize.cy;
        NTSTATUS rcNt2 = pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        AssertNtStatus(rcNt2);
    }

    NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    AssertNtStatusSuccess(rcNt2);

    return Status;
}

static NTSTATUS vboxVidPnSourceModeSetToArray(D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet,
                    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    VBOXVIDPN_SOURCEMODE_ITER Iter;
    const D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;

    VBoxVidPnSourceModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = VBoxVidPnSourceModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->Format.Graphics.VisibleRegionSize.cx;
        size.cy = pVidPnModeInfo->Format.Graphics.VisibleRegionSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            VBoxVidPnSourceModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    VBoxVidPnSourceModeIterTerm(&Iter);

    return VBoxVidPnSourceModeIterStatus(&Iter);
}

static NTSTATUS vboxVidPnSourceModeSetFromArray(D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet,
        const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        for (uint32_t m = 0; m < 2; ++m)
        {
            D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;
            NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
            if (!NT_SUCCESS(Status))
            {
                WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
                return Status;
            }

            vboxVidPnPopulateSourceModeInfo(pVidPnModeInfo, &size, m == 0 ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_A8B8G8R8);

            Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
            if (!NT_SUCCESS(Status))
            {
                WARN(("pfnAddMode (%d x %d) failed, Status 0x%x", size.cx, size.cy, Status));
                VBoxVidPnDumpSourceMode("SourceMode: ", pVidPnModeInfo, "\n");
                NTSTATUS rcNt2 = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
                AssertNtStatusSuccess(rcNt2);
                // Continue adding modes into modeset even if a mode was rejected
                continue;
            }
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnTargetModeSetToArray(D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet,
                    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    VBOXVIDPN_TARGETMODE_ITER Iter;
    const D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;

    VBoxVidPnTargetModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = VBoxVidPnTargetModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            VBoxVidPnTargetModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    VBoxVidPnTargetModeIterTerm(&Iter);

    return VBoxVidPnTargetModeIterStatus(&Iter);
}

static NTSTATUS vboxVidPnTargetModeSetFromArray(D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet,
        const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;
        NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
            return Status;
        }

        vboxVidPnPopulateTargetModeInfo(pVidPnModeInfo, &size);

        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAddMode (%d x %d) failed, Status 0x%x", size.cx, size.cy, Status));
            VBoxVidPnDumpTargetMode("TargetMode: ", pVidPnModeInfo, "\n");
            NTSTATUS rcNt2 = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
            AssertNtStatusSuccess(rcNt2);
            // Continue adding modes into modeset even if a mode was rejected
            continue;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnMonitorModeSetToArray(D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet,
                    const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    VBOXVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    VBoxVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = VBoxVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            VBoxVidPnMonitorModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    VBoxVidPnMonitorModeIterTerm(&Iter);

    return VBoxVidPnMonitorModeIterStatus(&Iter);
}

static NTSTATUS vboxVidPnMonitorModeSetFromArray(D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet,
        const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;
        NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
            return Status;
        }

        vboxVidPnPopulateMonitorModeInfo(pVidPnModeInfo, &size);

        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAddMode (%d x %d) failed, Status 0x%x", size.cx, size.cy, Status));
            NTSTATUS rcNt2 = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
            AssertNtStatusSuccess(rcNt2);
            // Continue adding modes into modeset even if a mode was rejected
            continue;
        }

        LOGF(("mode (%d x %d) added to monitor modeset", size.cx, size.cy));
    }

    return STATUS_SUCCESS;
}


static NTSTATUS vboxVidPnCollectInfoForPathTarget(PVBOXMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    const CR_SORTARRAY* pSupportedModes = VBoxWddmVModesGet(pDevExt, VidPnTargetId);
    NTSTATUS Status;
    if (enmCurPivot == D3DKMDT_EPT_VIDPNTARGET)
    {
        D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
        const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
        Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                    VidPnTargetId,
                    &hVidPnModeSet,
                    &pVidPnModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAcquireTargetModeSet failed %#x", Status));
            return Status;
        }

        /* intersect modes from target */
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Status = vboxVidPnTargetModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CR_SORTARRAY Arr;
            CrSaInit(&Arr, 0);
            Status = vboxVidPnTargetModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            CrSaIntersect(&aModes[VidPnTargetId], &Arr);
            CrSaCleanup(&Arr);
        }

        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);

        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnTargetModeSetToArray failed %#x", Status));
            return Status;
        }

        return STATUS_SUCCESS;
    }

    RTRECTSIZE pinnedSize = {0};
    Status = vboxVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
        }

        return STATUS_SUCCESS;
    }


    Status = vboxVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }

        return STATUS_SUCCESS;
    }

    /* now we are here because no pinned info is specified, we need to populate it based on the supported info
     * and modes already configured,
     * this is pretty simple actually */

    if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
    {
        Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
        int rc = CrSaClone(pSupportedModes, &aModes[VidPnTargetId]);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrSaClone failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
        ASMBitSet(aAdjustedModeMap, VidPnTargetId);
    }
    else
    {
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);
    }

    /* we are done */
    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnApplyInfoForPathTarget(PVBOXMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        const CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RT_NOREF(aAdjustedModeMap, VidPnSourceId);
    Assert(ASMBitTest(aAdjustedModeMap, VidPnTargetId));

    if (enmCurPivot == D3DKMDT_EPT_VIDPNTARGET)
        return STATUS_SUCCESS;

    RTRECTSIZE pinnedSize = {0};
    NTSTATUS Status = vboxVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
        return STATUS_SUCCESS;

    /* now just create the new source mode set and apply it */
    D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
    Status = pVidPnInterface->pfnCreateNewTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hVidPnModeSet,
                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnCreateNewTargetModeSet failed Status(0x%x)", Status));
        return Status;
    }

    Status = vboxVidPnTargetModeSetFromArray(hVidPnModeSet,
            pVidPnModeSetInterface,
            &aModes[VidPnTargetId]);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnTargetModeSetFromArray failed Status(0x%x)", Status));
        vboxVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        VBoxVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);
        return Status;
    }

    Status = pVidPnInterface->pfnAssignTargetModeSet(hVidPn, VidPnTargetId, hVidPnModeSet);
    if (!NT_SUCCESS(Status))
    {
        WARN(("\n\n!!!!!!!\n\n pfnAssignTargetModeSet failed, Status(0x%x)", Status));
        vboxVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        VBoxVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);
        return Status;
    }

    Status = vboxVidPnCheckMonitorModes(pDevExt, VidPnTargetId, &aModes[VidPnTargetId]);

    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnCheckMonitorModes failed, Status(0x%x)", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnApplyInfoForPathSource(PVBOXMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        const CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RT_NOREF(aAdjustedModeMap);
    Assert(ASMBitTest(aAdjustedModeMap, VidPnTargetId));

    if (enmCurPivot == D3DKMDT_EPT_VIDPNSOURCE)
        return STATUS_SUCCESS;

    RTRECTSIZE pinnedSize = {0};
    NTSTATUS Status = vboxVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
        return STATUS_SUCCESS;

    /* now just create the new source mode set and apply it */
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    Status = pVidPnInterface->pfnCreateNewSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hVidPnModeSet,
                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnCreateNewSourceModeSet failed Status(0x%x)", Status));
        return Status;
    }

    Status = vboxVidPnSourceModeSetFromArray(hVidPnModeSet,
            pVidPnModeSetInterface,
            &aModes[VidPnTargetId]); /* <- target modes always! */
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnSourceModeSetFromArray failed Status(0x%x)", Status));
        vboxVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        VBoxVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);
        return Status;
    }

    Status = pVidPnInterface->pfnAssignSourceModeSet(hVidPn, VidPnSourceId, hVidPnModeSet);
    if (!NT_SUCCESS(Status))
    {
        WARN(("\n\n!!!!!!!\n\n pfnAssignSourceModeSet failed, Status(0x%x)", Status));
        vboxVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        VBoxVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnCollectInfoForPathSource(PVBOXMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    const CR_SORTARRAY* pSupportedModes = VBoxWddmVModesGet(pDevExt, VidPnTargetId); /* <- yes, modes are target-determined always */
    NTSTATUS Status;

    if (enmCurPivot == D3DKMDT_EPT_VIDPNSOURCE)
    {
        D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
        Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                    VidPnSourceId,
                    &hVidPnModeSet,
                    &pVidPnModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAcquireSourceModeSet failed %#x", Status));
            return Status;
        }

        /* intersect modes from target */
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Status = vboxVidPnSourceModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CR_SORTARRAY Arr;
            CrSaInit(&Arr, 0);
            Status = vboxVidPnSourceModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            CrSaIntersect(&aModes[VidPnTargetId], &Arr);
            CrSaCleanup(&Arr);
        }

        NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        AssertNtStatusSuccess(rcNt2);

        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnReleaseSourceModeSet failed %#x", Status));
            return Status;
        }

        /* intersect it with supported target modes, just in case */
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);

        return STATUS_SUCCESS;
    }

    RTRECTSIZE pinnedSize = {0};
    Status = vboxVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);

            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }

        return STATUS_SUCCESS;
    }


    Status = vboxVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
        }

        return STATUS_SUCCESS;
    }

    /* now we are here because no pinned info is specified, we need to populate it based on the supported info
     * and modes already configured,
     * this is pretty simple actually */

    if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
    {
        Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
        int rc = CrSaClone(pSupportedModes, &aModes[VidPnTargetId]);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrSaClone failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
        ASMBitSet(aAdjustedModeMap, VidPnTargetId);
    }
    else
    {
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);
    }

    /* we are done */
    return STATUS_SUCCESS;
}

static NTSTATUS vboxVidPnCheckMonitorModes(PVBOXMP_DEVEXT pDevExt, uint32_t u32Target, const CR_SORTARRAY *pSupportedModes)
{
    NTSTATUS Status;
    CONST DXGK_MONITOR_INTERFACE *pMonitorInterface;
    Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryMonitorInterface(pDevExt->u.primary.DxgkInterface.DeviceHandle, DXGK_MONITOR_INTERFACE_VERSION_V1, &pMonitorInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

    if (!pSupportedModes)
    {
        pSupportedModes = VBoxWddmVModesGet(pDevExt, u32Target);
    }

    CR_SORTARRAY DiffModes;
    int rc = CrSaInit(&DiffModes, CrSaGetSize(pSupportedModes));
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrSaInit failed"));
        return STATUS_NO_MEMORY;
    }


    Status = pMonitorInterface->pfnAcquireMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                                        u32Target,
                                        &hVidPnModeSet,
                                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
//        if (Status == STATUS_GRAPHICS_MONITOR_NOT_CONNECTED)
        CrSaCleanup(&DiffModes);
        return Status;
    }

    VBOXVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    rc = CrSaClone(pSupportedModes, &DiffModes);
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrSaClone failed"));
        Status = STATUS_NO_MEMORY;
        goto done;
    }

    VBoxVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = VBoxVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        CrSaRemove(&DiffModes, CR_RSIZE2U64(size));
        LOGF(("mode (%d x %d) is already in monitor modeset\n", size.cx, size.cy));
    }

    VBoxVidPnMonitorModeIterTerm(&Iter);

    Status = VBoxVidPnMonitorModeIterStatus(&Iter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("iter status failed %#x", Status));
        goto done;
    }

    LOGF(("Adding %d additional modes to monitor modeset\n", CrSaGetSize(&DiffModes)));

    Status = vboxVidPnMonitorModeSetFromArray(hVidPnModeSet, pVidPnModeSetInterface, &DiffModes);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnMonitorModeSetFromArray failed %#x", Status));
        goto done;
    }

done:
    NTSTATUS rcNt2 = pMonitorInterface->pfnReleaseMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle, hVidPnModeSet);
    if (!NT_SUCCESS(rcNt2))
        WARN(("pfnReleaseMonitorSourceModeSet failed rcNt2(0x%x)", rcNt2));

    CrSaCleanup(&DiffModes);

    return Status;
}

static NTSTATUS vboxVidPnPathAdd(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
        D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE enmImportance)
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        return Status;
    }

    D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo;
    Status = pVidPnTopologyInterface->pfnCreateNewPathInfo(hVidPnTopology, &pNewVidPnPresentPathInfo);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        return Status;
    }

    pNewVidPnPresentPathInfo->VidPnSourceId = VidPnSourceId;
    pNewVidPnPresentPathInfo->VidPnTargetId = VidPnTargetId;
    pNewVidPnPresentPathInfo->ImportanceOrdinal = enmImportance;
    pNewVidPnPresentPathInfo->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
    memset(&pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport,
            0, sizeof (pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport));
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Identity = 1;
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Centered = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Stretched = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Identity = 1;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate180 = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate270 = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate90 = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy = 0;
    pNewVidPnPresentPathInfo->VidPnTargetColorBasis = D3DKMDT_CB_SRGB; /** @todo how does it matters? */
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FirstChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.SecondChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.ThirdChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel =  0;
    pNewVidPnPresentPathInfo->Content = D3DKMDT_VPPC_GRAPHICS;
    pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_UNINITIALIZED;
//                    pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
    pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits = 0;
    memset(&pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport, 0, sizeof (pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport));
//            pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport.NoProtection  = 1;
    memset (&pNewVidPnPresentPathInfo->GammaRamp, 0, sizeof (pNewVidPnPresentPathInfo->GammaRamp));
//            pNewVidPnPresentPathInfo->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
//            pNewVidPnPresentPathInfo->GammaRamp.DataSize = 0;
    Status = pVidPnTopologyInterface->pfnAddPath(hVidPnTopology, pNewVidPnPresentPathInfo);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        NTSTATUS rcNt2 = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pNewVidPnPresentPathInfo);
        AssertNtStatus(rcNt2);
    }

    LOG(("Recommended Path (%d->%d)", VidPnSourceId, VidPnTargetId));

    return Status;
}

NTSTATUS VBoxVidPnRecommendMonitorModes(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
                        D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    const CR_SORTARRAY *pSupportedModes = VBoxWddmVModesGet(pDevExt, VideoPresentTargetId);

    NTSTATUS Status = vboxVidPnMonitorModeSetFromArray(hVidPnModeSet, pVidPnModeSetInterface, pSupportedModes);
    if (!NT_SUCCESS(Status))
    {
        WARN(("vboxVidPnMonitorModeSetFromArray failed %d", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS VBoxVidPnUpdateModes(PVBOXMP_DEVEXT pDevExt, uint32_t u32TargetId, const RTRECTSIZE *pSize)
{
    LogRel2((VBOX_VIDEO_LOG_NAME ": VBoxVidPnUpdateModes u32TargetId(%d) mode(%d x %d)\n", u32TargetId, pSize->cx, pSize->cy));

    if (u32TargetId >= (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
    {
        WARN(("invalid target id"));
        return STATUS_INVALID_PARAMETER;
    }

    int rc = VBoxWddmVModesAdd(pDevExt, u32TargetId, pSize, TRUE);
    LOGF(("VBoxWddmVModesAdd returned (%d)", rc));

    if (RT_FAILURE(rc))
    {
        WARN(("VBoxWddmVModesAdd failed %d", rc));
        return STATUS_UNSUCCESSFUL;
    }

    if (rc == VINF_ALREADY_INITIALIZED)
    {
        /* mode was already in list, just return */
        Assert(CrSaContains(VBoxWddmVModesGet(pDevExt, u32TargetId), CR_RSIZE2U64(*pSize)));
        LOGF(("LEAVE mode was already in modeset, just return"));
        return STATUS_SUCCESS;
    }

#ifdef VBOX_WDDM_REPLUG_ON_MODE_CHANGE
    /* The VBOXESC_UPDATEMODES is a hint for the driver to use new display mode as soon as VidPn
     * manager will ask for it.
     * Probably, some new interface is required to plug/unplug displays by calling
     * VBoxWddmChildStatusReportReconnected.
     * But it is a bad idea to mix sending a display mode hint and (un)plug displays in VBOXESC_UPDATEMODES.
     */

    /* modes have changed, need to replug */
    NTSTATUS Status = VBoxWddmChildStatusReportReconnected(pDevExt, u32TargetId);
    LOG(("VBoxWddmChildStatusReportReconnected returned (%d)", Status));
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxWddmChildStatusReportReconnected failed Status(%#x)", Status));
        return Status;
    }
#endif

    LOGF(("LEAVE u32TargetId(%d)", u32TargetId));
    return STATUS_SUCCESS;
}

NTSTATUS VBoxVidPnRecommendFunctional(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const VBOXWDDM_RECOMMENDVIDPN *pData)
{
    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status(%#x)", Status));
        return Status;
    }

    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedSourceMap);

    memset(aVisitedSourceMap, 0, sizeof (aVisitedSourceMap));

    uint32_t Importance = (uint32_t)D3DKMDT_VPPI_PRIMARY;

    for (uint32_t i = 0; i < (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        int32_t iSource = pData->aTargets[i].iSource;
        if (iSource < 0)
            continue;

        if (iSource >= VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
        {
            WARN(("invalid iSource"));
            return STATUS_INVALID_PARAMETER;
        }

        if (!pDevExt->fComplexTopologiesEnabled && iSource != (int32_t)i)
        {
            WARN(("complex topologies not supported!"));
            return STATUS_INVALID_PARAMETER;
        }

        bool fNewSource = false;

        if (!ASMBitTest(aVisitedSourceMap, iSource))
        {
            int rc = VBoxWddmVModesAdd(pDevExt, i, &pData->aSources[iSource].Size, TRUE);
            if (RT_FAILURE(rc))
            {
                WARN(("VBoxWddmVModesAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }

            Assert(CrSaContains(VBoxWddmVModesGet(pDevExt, i), CR_RSIZE2U64(pData->aSources[iSource].Size)));

            Status = vboxVidPnCheckMonitorModes(pDevExt, i);
            if (!NT_SUCCESS(Status))
            {
                WARN(("vboxVidPnCheckMonitorModes failed %#x", Status));
                return Status;
            }

            ASMBitSet(aVisitedSourceMap, iSource);
            fNewSource = true;
        }

        Status = vboxVidPnPathAdd(hVidPn, pVidPnInterface,
                (const D3DDDI_VIDEO_PRESENT_SOURCE_ID)iSource, (const D3DDDI_VIDEO_PRESENT_TARGET_ID)i,
                (D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE)Importance);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnPathAdd failed Status()0x%x\n", Status));
            return Status;
        }

        Importance++;

        do {
            D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
            const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;

            Status = pVidPnInterface->pfnCreateNewTargetModeSet(hVidPn,
                                i,
                                &hVidPnModeSet,
                                &pVidPnModeSetInterface);
            if (NT_SUCCESS(Status))
            {
                D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;
                Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
                if (NT_SUCCESS(Status))
                {
                    vboxVidPnPopulateTargetModeInfo(pVidPnModeInfo, &pData->aSources[iSource].Size);

                    IN_CONST_D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID idMode = pVidPnModeInfo->Id;

                    Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
                    if (NT_SUCCESS(Status))
                    {
                        pVidPnModeInfo = NULL;

                        Status = pVidPnModeSetInterface->pfnPinMode(hVidPnModeSet, idMode);
                        if (NT_SUCCESS(Status))
                        {
                            Status = pVidPnInterface->pfnAssignTargetModeSet(hVidPn, i, hVidPnModeSet);
                            if (NT_SUCCESS(Status))
                            {
                                LOG(("Recommended Target[%d] (%dx%d)", i, pData->aSources[iSource].Size.cx, pData->aSources[iSource].Size.cy));
                                break;
                            }
                            else
                                WARN(("pfnAssignTargetModeSet failed %#x", Status));
                        }
                        else
                            WARN(("pfnPinMode failed %#x", Status));

                    }
                    else
                        WARN(("pfnAddMode failed %#x", Status));

                    if (pVidPnModeInfo)
                    {
                        NTSTATUS rcNt2 = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
                        AssertNtStatusSuccess(rcNt2);
                    }
                }
                else
                    WARN(("pfnCreateNewTargetModeSet failed %#x", Status));

                NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
                AssertNtStatusSuccess(rcNt2);
            }
            else
                WARN(("pfnCreateNewTargetModeSet failed %#x", Status));

            Assert(!NT_SUCCESS(Status));

            return Status;
        } while (0);

        if (fNewSource)
        {
            do {
                D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
                const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

                Status = pVidPnInterface->pfnCreateNewSourceModeSet(hVidPn,
                                    iSource,
                                    &hVidPnModeSet,
                                    &pVidPnModeSetInterface);
                if (NT_SUCCESS(Status))
                {
                    D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;
                    Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
                    if (NT_SUCCESS(Status))
                    {
                        vboxVidPnPopulateSourceModeInfo(pVidPnModeInfo, &pData->aSources[iSource].Size);

                        IN_CONST_D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID idMode = pVidPnModeInfo->Id;

                        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
                        if (NT_SUCCESS(Status))
                        {
                            pVidPnModeInfo = NULL;

                            Status = pVidPnModeSetInterface->pfnPinMode(hVidPnModeSet, idMode);
                            if (NT_SUCCESS(Status))
                            {
                                Status = pVidPnInterface->pfnAssignSourceModeSet(hVidPn, iSource, hVidPnModeSet);
                                if (NT_SUCCESS(Status))
                                {
                                    LOG(("Recommended Source[%d] (%dx%d)", iSource, pData->aSources[iSource].Size.cx, pData->aSources[iSource].Size.cy));
                                    break;
                                }
                                else
                                    WARN(("pfnAssignSourceModeSet failed %#x", Status));
                            }
                            else
                                WARN(("pfnPinMode failed %#x", Status));

                        }
                        else
                            WARN(("pfnAddMode failed %#x", Status));

                        if (pVidPnModeInfo)
                        {
                            NTSTATUS rcNt2 = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
                            AssertNtStatusSuccess(rcNt2);
                        }
                    }
                    else
                        WARN(("pfnCreateNewSourceModeSet failed %#x", Status));

                    NTSTATUS rcNt2 = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
                    AssertNtStatusSuccess(rcNt2);
                }
                else
                    WARN(("pfnCreateNewSourceModeSet failed %#x", Status));

                Assert(!NT_SUCCESS(Status));

                return Status;
            } while (0);
        }
    }

    Assert(NT_SUCCESS(Status));
    return STATUS_SUCCESS;
}

static BOOLEAN vboxVidPnIsPathSupported(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo)
{
    if (!pDevExt->fComplexTopologiesEnabled && pNewVidPnPresentPathInfo->VidPnSourceId != pNewVidPnPresentPathInfo->VidPnTargetId)
    {
        LOG(("unsupported source(%d)->target(%d) pair", pNewVidPnPresentPathInfo->VidPnSourceId, pNewVidPnPresentPathInfo->VidPnTargetId));
        return FALSE;
    }

    /*
    ImportanceOrdinal does not matter for now
    pNewVidPnPresentPathInfo->ImportanceOrdinal
    */

    if (pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_UNPINNED
            && pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY
            && pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED)
    {
        WARN(("unsupported Scaling (%d)", pNewVidPnPresentPathInfo->ContentTransformation.Scaling));
        return FALSE;
    }

    if (    !pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Identity
         || pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Centered
         || pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Stretched)
    {
        WARN(("unsupported Scaling support"));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_UNPINNED
            && pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY
            && pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED)
    {
        WARN(("unsupported rotation (%d)", pNewVidPnPresentPathInfo->ContentTransformation.Rotation));
        return FALSE;
    }

    if (    !pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Identity
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate90
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate180
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate270)
    {
        WARN(("unsupported RotationSupport"));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx
            || pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy)
    {
        WARN(("Non-zero TLOffset: cx(%d), cy(%d)",
                pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx,
                pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx
            || pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy)
    {
        WARN(("Non-zero TLOffset: cx(%d), cy(%d)",
                pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx,
                pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VidPnTargetColorBasis != D3DKMDT_CB_SRGB
            && pNewVidPnPresentPathInfo->VidPnTargetColorBasis != D3DKMDT_CB_UNINITIALIZED)
    {
        WARN(("unsupported VidPnTargetColorBasis (%d)", pNewVidPnPresentPathInfo->VidPnTargetColorBasis));
        return FALSE;
    }

    /* channels?
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FirstChannel;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.SecondChannel;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.ThirdChannel;
    we definitely not support fourth channel
    */
    if (pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel)
    {
        WARN(("Non-zero FourthChannel (%d)", pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel));
        return FALSE;
    }

    /* Content (D3DKMDT_VPPC_GRAPHICS, _NOTSPECIFIED, _VIDEO), does not matter for now
    pNewVidPnPresentPathInfo->Content
    */
    /* not support copy protection for now */
    if (pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType != D3DKMDT_VPPMT_NOPROTECTION
            && pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType != D3DKMDT_VPPMT_UNINITIALIZED)
    {
        WARN(("Copy protection not supported CopyProtectionType(%d)", pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits)
    {
        WARN(("Copy protection not supported APSTriggerBits(%d)", pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits));
        return FALSE;
    }

    D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_SUPPORT tstCPSupport = {0};
    tstCPSupport.NoProtection = 1;
    if (memcmp(&tstCPSupport, &pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport, sizeof(tstCPSupport)))
    {
        WARN(("Copy protection support (0x%x)", *((UINT*)&pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport)));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT
            && pNewVidPnPresentPathInfo->GammaRamp.Type != D3DDDI_GAMMARAMP_UNINITIALIZED)
    {
        WARN(("Unsupported GammaRamp.Type (%d)", pNewVidPnPresentPathInfo->GammaRamp.Type));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->GammaRamp.DataSize != 0)
    {
        WARN(("Warning: non-zero GammaRamp.DataSize (%d), treating as supported", pNewVidPnPresentPathInfo->GammaRamp.DataSize));
    }

    return TRUE;
}

NTSTATUS VBoxVidPnIsSupported(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, BOOLEAN *pfSupported)
{
    /* According Microsoft Docs we must return pfSupported = TRUE here if hVidPn is NULL, as
     * the display adapter can always be configured to display nothing. */
    if (hVidPn == NULL)
    {
        *pfSupported = TRUE;
        return STATUS_SUCCESS;
    }

    *pfSupported = FALSE;

    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status()0x%x\n", Status));
        return Status;
    }

#ifdef VBOXWDDM_DEBUG_VIDPN
    vboxVidPnDumpVidPn(">>>>IsSupported VidPN (IN) : >>>>\n", pDevExt, hVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status()0x%x\n", Status));
        return Status;
    }

    VBOXVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH * pPath;
    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedTargetMap);

    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));

    BOOLEAN fSupported = TRUE;
    /* collect info first */
    VBoxVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = VBoxVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        if (!vboxVidPnIsPathSupported(pDevExt, pPath))
        {
            fSupported = FALSE;
            break;
        }

        RTRECTSIZE TargetSize;
        RTRECTSIZE SourceSize;
        Status = vboxVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &TargetSize);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnQueryPinnedTargetMode failed %#x", Status));
            break;
        }

        Status = vboxVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &SourceSize);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnQueryPinnedSourceMode failed %#x", Status));
            break;
        }

        if (memcmp(&TargetSize, &SourceSize, sizeof (TargetSize)) && TargetSize.cx)
        {
            if (!SourceSize.cx)
                WARN(("not expected?"));

            fSupported = FALSE;
            break;
        }
    }

    VBoxVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = VBoxVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        goto done;
    }

    *pfSupported = fSupported;
done:

    return Status;
}

NTSTATUS VBoxVidPnCofuncModality(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot)
{
    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status()0x%x\n", Status));
        return Status;
    }

#ifdef VBOXWDDM_DEBUG_VIDPN
    vboxVidPnDumpCofuncModalityArg(">>>>MODALITY Args: ", enmPivot, pPivot, "\n");
    vboxVidPnDumpVidPn(">>>>MODALITY VidPN (IN) : >>>>\n", pDevExt, hVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status()0x%x\n", Status));
        return Status;
    }

    VBOXVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH * pPath;
    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedTargetMap);
    VBOXCMDVBVA_SCREENMAP_DECL(uint32_t, aAdjustedModeMap);
    CR_SORTARRAY aModes[VBOX_VIDEO_MAX_SCREENS];

    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));
    memset(aAdjustedModeMap, 0, sizeof (aAdjustedModeMap));
    memset(aModes, 0, sizeof (aModes));

    /* collect info first */
    VBoxVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = VBoxVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot = vboxVidPnCofuncModalityCurrentPathPivot(enmPivot, pPivot, VidPnSourceId, VidPnTargetId);

        Status = vboxVidPnCollectInfoForPathTarget(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnCollectInfoForPathTarget failed Status(0x%x\n", Status));
            VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Assert(CrSaCovers(VBoxWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));

        Status = vboxVidPnCollectInfoForPathSource(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnCollectInfoForPathSource failed Status(0x%x\n", Status));
            VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Assert(CrSaCovers(VBoxWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));
    }

    VBoxVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = VBoxVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
        goto done;
    }

    /* now we have collected all the necessary info,
     * go ahead and apply it */
    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));
    VBoxVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = VBoxVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot = vboxVidPnCofuncModalityCurrentPathPivot(enmPivot, pPivot, VidPnSourceId, VidPnTargetId);

        bool bUpdatePath = false;
        D3DKMDT_VIDPN_PRESENT_PATH AdjustedPath = {0};
        AdjustedPath.VidPnSourceId = pPath->VidPnSourceId;
        AdjustedPath.VidPnTargetId = pPath->VidPnTargetId;
        AdjustedPath.ContentTransformation = pPath->ContentTransformation;
        AdjustedPath.CopyProtection = pPath->CopyProtection;

        if (pPath->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
        {
            AdjustedPath.ContentTransformation.ScalingSupport.Identity = TRUE;
            bUpdatePath = true;
        }

        if (pPath->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
        {
            AdjustedPath.ContentTransformation.RotationSupport.Identity = TRUE;
            bUpdatePath = true;
        }

        if (bUpdatePath)
        {
            Status = pVidPnTopologyInterface->pfnUpdatePathSupportInfo(hVidPnTopology, &AdjustedPath);
            if (!NT_SUCCESS(Status))
            {
                WARN(("pfnUpdatePathSupportInfo failed Status()0x%x\n", Status));
                VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
                goto done;
            }
        }

        Assert(CrSaCovers(VBoxWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));

        Status = vboxVidPnApplyInfoForPathTarget(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnApplyInfoForPathTarget failed Status(0x%x\n", Status));
            VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Status = vboxVidPnApplyInfoForPathSource(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("vboxVidPnApplyInfoForPathSource failed Status(0x%x\n", Status));
            VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }
    }

    VBoxVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = VBoxVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        VBoxVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
        goto done;
    }

done:

    for (uint32_t i = 0; i < (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        CrSaCleanup(&aModes[i]);
    }

    return Status;
}

NTSTATUS vboxVidPnEnumMonitorSourceModes(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        PFNVBOXVIDPNENUMMONITORSOURCEMODES pfnCallback, PVOID pContext)
{
    CONST D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSMI;
    NTSTATUS Status = pMonitorSMSIf->pfnAcquireFirstModeInfo(hMonitorSMS, &pMonitorSMI);
    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pMonitorSMI);
        while (1)
        {
            CONST D3DKMDT_MONITOR_SOURCE_MODE *pNextMonitorSMI;
            Status = pMonitorSMSIf->pfnAcquireNextModeInfo(hMonitorSMS, pMonitorSMI, &pNextMonitorSMI);
            if (!pfnCallback(hMonitorSMS, pMonitorSMSIf, pMonitorSMI, pContext))
            {
                Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET);
                if (Status == STATUS_SUCCESS)
                    pMonitorSMSIf->pfnReleaseModeInfo(hMonitorSMS, pNextMonitorSMI);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }
                break;
            }
            else if (Status == STATUS_SUCCESS)
                pMonitorSMI = pNextMonitorSMI;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNextMonitorSMI = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS vboxVidPnEnumSourceModes(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                    PFNVBOXVIDPNENUMSOURCEMODES pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo;
    NTSTATUS Status = pVidPnSourceModeSetInterface->pfnAcquireFirstModeInfo(hNewVidPnSourceModeSet, &pNewVidPnSourceModeInfo);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pNewVidPnSourceModeInfo);
        while (1)
        {
            const D3DKMDT_VIDPN_SOURCE_MODE *pNextVidPnSourceModeInfo;
            Status = pVidPnSourceModeSetInterface->pfnAcquireNextModeInfo(hNewVidPnSourceModeSet, pNewVidPnSourceModeInfo, &pNextVidPnSourceModeInfo);
            if (!pfnCallback(hNewVidPnSourceModeSet, pVidPnSourceModeSetInterface,
                    pNewVidPnSourceModeInfo, pContext))
            {
                AssertNtStatusSuccess(Status);
                if (Status == STATUS_SUCCESS)
                    pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hNewVidPnSourceModeSet, pNextVidPnSourceModeInfo);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnSourceModeInfo = pNextVidPnSourceModeInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNewVidPnSourceModeInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS vboxVidPnEnumTargetModes(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        PFNVBOXVIDPNENUMTARGETMODES pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo;
    NTSTATUS Status = pVidPnTargetModeSetInterface->pfnAcquireFirstModeInfo(hNewVidPnTargetModeSet, &pNewVidPnTargetModeInfo);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pNewVidPnTargetModeInfo);
        while (1)
        {
            const D3DKMDT_VIDPN_TARGET_MODE *pNextVidPnTargetModeInfo;
            Status = pVidPnTargetModeSetInterface->pfnAcquireNextModeInfo(hNewVidPnTargetModeSet, pNewVidPnTargetModeInfo, &pNextVidPnTargetModeInfo);
            if (!pfnCallback(hNewVidPnTargetModeSet, pVidPnTargetModeSetInterface,
                    pNewVidPnTargetModeInfo, pContext))
            {
                AssertNtStatusSuccess(Status);
                if (Status == STATUS_SUCCESS)
                    pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hNewVidPnTargetModeSet, pNextVidPnTargetModeInfo);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnTargetModeInfo = pNextVidPnTargetModeInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNewVidPnTargetModeInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS vboxVidPnEnumTargetsForSource(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        PFNVBOXVIDPNENUMTARGETSFORSOURCE pfnCallback, PVOID pContext)
{
    SIZE_T cTgtPaths;
    NTSTATUS Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, VidPnSourceId, &cTgtPaths);
    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY);
    if (Status == STATUS_SUCCESS)
    {
        for (SIZE_T i = 0; i < cTgtPaths; ++i)
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology, VidPnSourceId, i, &VidPnTargetId);
            AssertNtStatusSuccess(Status);
            if (Status == STATUS_SUCCESS)
            {
                if (!pfnCallback(pDevExt, hVidPnTopology, pVidPnTopologyInterface, VidPnSourceId, VidPnTargetId, cTgtPaths, pContext))
                    break;
            }
            else
            {
                LOGREL(("pfnEnumPathTargetsFromSource failed Status(0x%x)", Status));
                break;
            }
        }
    }
    else if (Status != STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        LOGREL(("pfnGetNumPathsFromSource failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS vboxVidPnEnumPaths(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        PFNVBOXVIDPNENUMPATHS pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo = NULL;
    NTSTATUS Status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(hVidPnTopology, &pNewVidPnPresentPathInfo);
    if (Status == STATUS_SUCCESS)
    {
        while (1)
        {
            const D3DKMDT_VIDPN_PRESENT_PATH *pNextVidPnPresentPathInfo;
            Status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(hVidPnTopology, pNewVidPnPresentPathInfo, &pNextVidPnPresentPathInfo);

            if (!pfnCallback(hVidPnTopology, pVidPnTopologyInterface, pNewVidPnPresentPathInfo, pContext))
            {
                if (Status == STATUS_SUCCESS)
                    pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pNextVidPnPresentPathInfo);
                else
                {
                    if (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                        WARN(("pfnAcquireNextPathInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnPresentPathInfo = pNextVidPnPresentPathInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                WARN(("pfnAcquireNextPathInfo Failed Status(0x%x)", Status));
                pNewVidPnPresentPathInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        WARN(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS vboxVidPnSetupSourceInfo(PVBOXMP_DEVEXT pDevExt, CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo,
                                  PVBOXWDDM_ALLOCATION pAllocation, D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId,
                                  VBOXWDDM_SOURCE *paSources)
{
    RT_NOREF(pDevExt);
    PVBOXWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    /* pVidPnSourceModeInfo could be null if STATUS_GRAPHICS_MODE_NOT_PINNED,
     * see VBoxVidPnCommitSourceModeForSrcId */
    uint8_t fChanges = 0;
    if (pVidPnSourceModeInfo)
    {
        if (pSource->AllocData.SurfDesc.width != pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.width = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx;
        }
        if (pSource->AllocData.SurfDesc.height != pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.height = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy;
        }
        if (pSource->AllocData.SurfDesc.format != pVidPnSourceModeInfo->Format.Graphics.PixelFormat)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.format = pVidPnSourceModeInfo->Format.Graphics.PixelFormat;
        }
        if (pSource->AllocData.SurfDesc.bpp != vboxWddmCalcBitsPerPixel(pVidPnSourceModeInfo->Format.Graphics.PixelFormat))
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.bpp = vboxWddmCalcBitsPerPixel(pVidPnSourceModeInfo->Format.Graphics.PixelFormat);
        }
        if(pSource->AllocData.SurfDesc.pitch != pVidPnSourceModeInfo->Format.Graphics.Stride)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.pitch = pVidPnSourceModeInfo->Format.Graphics.Stride;
        }
        pSource->AllocData.SurfDesc.depth = 1;
        if (pSource->AllocData.SurfDesc.slicePitch != pVidPnSourceModeInfo->Format.Graphics.Stride)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.slicePitch = pVidPnSourceModeInfo->Format.Graphics.Stride;
        }
        if (pSource->AllocData.SurfDesc.cbSize != pVidPnSourceModeInfo->Format.Graphics.Stride * pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy)
        {
            fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.cbSize = pVidPnSourceModeInfo->Format.Graphics.Stride * pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy;
        }

        if (g_VBoxDisplayOnly)
        {
            vboxWddmDmSetupDefaultVramLocation(pDevExt, VidPnSourceId, paSources);
        }
    }
    else
    {
        VBoxVidPnAllocDataInit(&pSource->AllocData, VidPnSourceId);
        Assert(!pAllocation);
        fChanges |= VBOXWDDM_HGSYNC_F_SYNCED_ALL;
    }

    Assert(!g_VBoxDisplayOnly || !pAllocation);
    if (!g_VBoxDisplayOnly)
    {
        vboxWddmAssignPrimary(pSource, pAllocation, VidPnSourceId);
    }

    Assert(pSource->AllocData.SurfDesc.VidPnSourceId == VidPnSourceId);
    pSource->u8SyncState &= ~fChanges;
    return STATUS_SUCCESS;
}

NTSTATUS vboxVidPnCommitSourceMode(PVBOXMP_DEVEXT pDevExt, CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, PVBOXWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, VBOXWDDM_SOURCE *paSources)
{
    if (VidPnSourceId < (UINT)VBoxCommonFromDeviceExt(pDevExt)->cDisplays)
        return vboxVidPnSetupSourceInfo(pDevExt, pVidPnSourceModeInfo, pAllocation, VidPnSourceId, paSources);

    WARN(("invalid srcId (%d), cSources(%d)", VidPnSourceId, VBoxCommonFromDeviceExt(pDevExt)->cDisplays));
    return STATUS_INVALID_PARAMETER;
}

typedef struct VBOXVIDPNCOMMITTARGETMODE
{
    NTSTATUS Status;
    D3DKMDT_HVIDPN hVidPn;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
    VBOXWDDM_SOURCE *paSources;
    VBOXWDDM_TARGET *paTargets;
} VBOXVIDPNCOMMITTARGETMODE;

DECLCALLBACK(BOOLEAN) vboxVidPnCommitTargetModeEnum(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
                                                    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
                                                    CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
                                                    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, SIZE_T cTgtPaths,
                                                    PVOID pContext)
{
    RT_NOREF(hVidPnTopology, pVidPnTopologyInterface, cTgtPaths);
    VBOXVIDPNCOMMITTARGETMODE *pInfo = (VBOXVIDPNCOMMITTARGETMODE*)pContext;
    Assert(cTgtPaths <= (SIZE_T)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);
    D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface;
    NTSTATUS Status = pInfo->pVidPnInterface->pfnAcquireTargetModeSet(pInfo->hVidPn, VidPnTargetId, &hVidPnTargetModeSet, &pVidPnTargetModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;
        Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
        AssertNtStatusSuccess(Status);
        if (Status == STATUS_SUCCESS)
        {
            VBOXWDDM_SOURCE *pSource = &pInfo->paSources[VidPnSourceId];
            VBOXWDDM_TARGET *pTarget = &pInfo->paTargets[VidPnTargetId];
            pTarget->Size.cx = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cx;
            pTarget->Size.cy = pPinnedVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cy;

            VBoxVidPnStSourceTargetAdd(pInfo->paSources, VBoxCommonFromDeviceExt(pDevExt)->cDisplays, pSource, pTarget);

            pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_DIMENSIONS;

            pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        }
        else
            WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));

        pInfo->pVidPnInterface->pfnReleaseTargetModeSet(pInfo->hVidPn, hVidPnTargetModeSet);
    }
    else
        WARN(("pfnAcquireTargetModeSet failed Status(0x%x)", Status));

    pInfo->Status = Status;
    return Status == STATUS_SUCCESS;
}

NTSTATUS VBoxVidPnCommitSourceModeForSrcId(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PVBOXWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets, BOOLEAN bPathPowerTransition)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    PVBOXWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    NTSTATUS Status;

    if (bPathPowerTransition)
    {
        RTRECTSIZE PinnedModeSize;
        bool bHasPinnedMode;

        Status = vboxVidPnQueryPinnedSourceMode(hDesiredVidPn, pVidPnInterface, VidPnSourceId, &PinnedModeSize);
        bHasPinnedMode = Status == STATUS_SUCCESS && PinnedModeSize.cx > 0 && PinnedModeSize.cy > 0;
        pSource->bBlankedByPowerOff = !bHasPinnedMode;

        LOG(("Path power transition: srcId %d goes blank %d", VidPnSourceId, pSource->bBlankedByPowerOff));
    }

    VBOXWDDM_TARGET_ITER Iter;
    VBoxVidPnStTIterInit(pSource, paTargets, (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);
    for (PVBOXWDDM_TARGET pTarget = VBoxVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = VBoxVidPnStTIterNext(&Iter))
    {
        Assert(pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId);
        pTarget->Size.cx = 0;
        pTarget->Size.cy = 0;
        pTarget->fBlankedByPowerOff = RT_BOOL(pSource->bBlankedByPowerOff);
        pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_ALL;
    }

    VBoxVidPnStSourceCleanup(paSources, VidPnSourceId, paTargets, (uint32_t)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

    Status = pVidPnInterface->pfnAcquireSourceModeSet(hDesiredVidPn,
                VidPnSourceId,
                &hCurVidPnSourceModeSet,
                &pCurVidPnSourceModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;
        Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            Assert(pPinnedVidPnSourceModeInfo);
            Status = vboxVidPnCommitSourceMode(pDevExt, pPinnedVidPnSourceModeInfo, pAllocation, VidPnSourceId, paSources);
            AssertNtStatusSuccess(Status);
            if (Status == STATUS_SUCCESS)
            {
                D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
                CONST DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
                Status = pVidPnInterface->pfnGetTopology(hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
                AssertNtStatusSuccess(Status);
                if (Status == STATUS_SUCCESS)
                {
                    VBOXVIDPNCOMMITTARGETMODE TgtModeInfo = {0};
                    TgtModeInfo.Status = STATUS_SUCCESS; /* <- to ensure we're succeeded if no targets are set */
                    TgtModeInfo.hVidPn = hDesiredVidPn;
                    TgtModeInfo.pVidPnInterface = pVidPnInterface;
                    TgtModeInfo.paSources = paSources;
                    TgtModeInfo.paTargets = paTargets;
                    Status = vboxVidPnEnumTargetsForSource(pDevExt, hVidPnTopology, pVidPnTopologyInterface,
                            VidPnSourceId,
                            vboxVidPnCommitTargetModeEnum, &TgtModeInfo);
                    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY);
                    if (Status == STATUS_SUCCESS)
                    {
                        Status = TgtModeInfo.Status;
                        AssertNtStatusSuccess(Status);
                    }
                    else if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else
                        WARN(("vboxVidPnEnumTargetsForSource failed Status(0x%x)", Status));
                }
                else
                    WARN(("pfnGetTopology failed Status(0x%x)", Status));
            }
            else
                WARN(("vboxVidPnCommitSourceMode failed Status(0x%x)", Status));
            /* release */
            pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            Status = vboxVidPnCommitSourceMode(pDevExt, NULL, pAllocation, VidPnSourceId, paSources);
            AssertNtStatusSuccess(Status);
        }
        else
            WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));

        pVidPnInterface->pfnReleaseSourceModeSet(hDesiredVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        WARN(("pfnAcquireSourceModeSet failed Status(0x%x)", Status));
    }

    return Status;
}

NTSTATUS VBoxVidPnCommitAll(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PVBOXWDDM_ALLOCATION pAllocation,
        VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets)
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status 0x%x", Status));
        return Status;
    }

    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        PVBOXWDDM_TARGET pTarget = &paTargets[i];
        pTarget->Size.cx = 0;
        pTarget->Size.cy = 0;
        pTarget->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_ALL;

        if (pTarget->VidPnSourceId == D3DDDI_ID_UNINITIALIZED)
            continue;

        Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

        VBOXWDDM_SOURCE *pSource = &paSources[pTarget->VidPnSourceId];
        VBoxVidPnAllocDataInit(&pSource->AllocData, pTarget->VidPnSourceId);
        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_ALL;
    }

    VBoxVidPnStCleanup(paSources, paTargets, VBoxCommonFromDeviceExt(pDevExt)->cDisplays);

    VBOXVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH *pPath;
    VBoxVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = VBoxVidPnPathIterNext(&PathIter)) != NULL)
    {
        Status = VBoxVidPnCommitSourceModeForSrcId(pDevExt, hDesiredVidPn, pVidPnInterface, pAllocation,
                    pPath->VidPnSourceId, paSources, paTargets, FALSE);
        if (Status != STATUS_SUCCESS)
        {
            WARN(("VBoxVidPnCommitSourceModeForSrcId failed Status(0x%x)", Status));
            break;
        }
    }

    VBoxVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
    {
        WARN((""));
        return Status;
    }

    Status = VBoxVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("VBoxVidPnPathIterStatus failed Status 0x%x", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

#define VBOXVIDPNDUMP_STRCASE(_t) \
        case _t: return #_t;
#define VBOXVIDPNDUMP_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

#define VBOXVIDPNDUMP_STRFLAGS(_v, _t) \
        if ((_v)._t return #_t;

const char* vboxVidPnDumpStrImportance(D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE ImportanceOrdinal)
{
    switch (ImportanceOrdinal)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_PRIMARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SECONDARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_TERTIARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_QUATERNARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_QUINARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SENARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SEPTENARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_OCTONARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_NONARY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPI_DENARY);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrScaling(D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling)
{
    switch (Scaling)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_IDENTITY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_CENTERED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_STRETCHED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_UNPINNED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPS_NOTSPECIFIED);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrRotation(D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    switch (Rotation)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_IDENTITY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE90);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE180);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE270);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_UNPINNED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPR_NOTSPECIFIED);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrColorBasis(const D3DKMDT_COLOR_BASIS ColorBasis)
{
    switch (ColorBasis)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_INTENSITY);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_SRGB);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_SCRGB);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_YCBCR);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_CB_YPBPR);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char * vboxVidPnDumpStrMonCapabilitiesOrigin(D3DKMDT_MONITOR_CAPABILITIES_ORIGIN enmOrigin)
{
    switch (enmOrigin)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_DEFAULTMONITORPROFILE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_MONITORDESCRIPTOR);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_MONITORDESCRIPTOR_REGISTRYOVERRIDE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_SPECIFICCAP_REGISTRYOVERRIDE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MCO_DRIVER);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrPvam(D3DKMDT_PIXEL_VALUE_ACCESS_MODE PixelValueAccessMode)
{
    switch (PixelValueAccessMode)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_PVAM_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_PVAM_DIRECT);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_PVAM_PRESETPALETTE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_PVAM_SETTABLEPALETTE);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}



const char* vboxVidPnDumpStrContent(D3DKMDT_VIDPN_PRESENT_PATH_CONTENT Content)
{
    switch (Content)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPC_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPC_GRAPHICS);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPC_VIDEO);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPC_NOTSPECIFIED);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrCopyProtectionType(D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_TYPE CopyProtectionType)
{
    switch (CopyProtectionType)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_NOPROTECTION);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_MACROVISION_APSTRIGGER);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_MACROVISION_FULLSUPPORT);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrGammaRampType(D3DDDI_GAMMARAMP_TYPE Type)
{
    switch (Type)
    {
        VBOXVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_DEFAULT);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_RGB256x3x16);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_DXGI_1);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrSourceModeType(D3DKMDT_VIDPN_SOURCE_MODE_TYPE Type)
{
    switch (Type)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_RMT_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_RMT_GRAPHICS);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_RMT_TEXT);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrScanLineOrdering(D3DDDI_VIDEO_SIGNAL_SCANLINE_ORDERING ScanLineOrdering)
{
    switch (ScanLineOrdering)
    {
        VBOXVIDPNDUMP_STRCASE(D3DDDI_VSSLO_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_VSSLO_PROGRESSIVE);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST);
        VBOXVIDPNDUMP_STRCASE(D3DDDI_VSSLO_OTHER);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrCFMPivotType(D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE EnumPivotType)
{
    switch (EnumPivotType)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_VIDPNSOURCE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_VIDPNTARGET);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_SCALING);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_ROTATION);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_EPT_NOPIVOT);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrModePreference(D3DKMDT_MODE_PREFERENCE Preference)
{
    switch (Preference)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MP_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MP_PREFERRED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_MP_NOTPREFERRED);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrSignalStandard(D3DKMDT_VIDEO_SIGNAL_STANDARD VideoStandard)
{
    switch (VideoStandard)
    {
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_UNINITIALIZED);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_DMT);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_GTF);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_CVT);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_IBM);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_APPLE);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_M);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_J);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_443);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_B);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_B1);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_G);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_H);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_I);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_D);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_N);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_NC);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_B);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_D);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_G);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_H);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_K);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_K1);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_L);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_L1);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861A);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861B);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_K);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_K1);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_L);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_M);
        VBOXVIDPNDUMP_STRCASE(D3DKMDT_VSS_OTHER);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* vboxVidPnDumpStrPixFormat(D3DDDIFORMAT PixelFormat)
{
    switch (PixelFormat)
    {
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_UNKNOWN);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R8G8B8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8R8G8B8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X8R8G8B8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R5G6B5);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X1R5G5B5);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A1R5G5B5);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A4R4G4B4);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R3G3B2);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8R3G3B2);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X4R4G4B4);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A2B10G10R10);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8B8G8R8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X8B8G8R8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_G16R16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A2R10G10B10);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A16B16G16R16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8P8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R32F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_G32R32F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A32B32G32R32F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_CxV8U8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A1);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_BINARYBUFFER);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_VERTEXDATA);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_INDEX16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_INDEX32);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_Q16W16V16U16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_MULTI2_ARGB8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R16F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_G16R16F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A16B16G16R16F);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D32F_LOCKABLE);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D24FS8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D32_LOCKABLE);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_S8_LOCKABLE);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_S1D15);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_S8D24);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X8D24);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X4S4D24);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_L16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_UYVY);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_R8G8_B8G8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_YUY2);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_G8R8_G8B8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_DXT1);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_DXT2);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_DXT3);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_DXT4);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_DXT5);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D16_LOCKABLE);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D32);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D15S1);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D24S8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D24X8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D24X4S4);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_D16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_P8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_L8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A8L8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A4L4);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_V8U8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_L6V5U5);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_X8L8V8U8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_Q8W8V8U8);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_V16U16);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_W11V11U10);
        VBOXVIDPNDUMP_STRCASE(D3DDDIFMT_A2W10V10U10);
        VBOXVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

void vboxVidPnDumpCopyProtectoin(const char *pPrefix, const D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION *pCopyProtection, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), TODO%s", pPrefix,
            vboxVidPnDumpStrCopyProtectionType(pCopyProtection->CopyProtectionType), pSuffix));
}


void vboxVidPnDumpPathTransformation(const D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION *pContentTransformation)
{
    LOGREL_EXACT(("  --Transformation: Scaling(%s), ScalingSupport(%d), Rotation(%s), RotationSupport(%d)--",
            vboxVidPnDumpStrScaling(pContentTransformation->Scaling), pContentTransformation->ScalingSupport,
            vboxVidPnDumpStrRotation(pContentTransformation->Rotation), pContentTransformation->RotationSupport));
}

void vboxVidPnDumpRegion(const char *pPrefix, const D3DKMDT_2DREGION *pRegion, const char *pSuffix)
{
    LOGREL_EXACT(("%s%dX%d%s", pPrefix, pRegion->cx, pRegion->cy, pSuffix));
}

void vboxVidPnDumpRational(const char *pPrefix, const D3DDDI_RATIONAL *pRational, const char *pSuffix)
{
    LOGREL_EXACT(("%s%d/%d=%d%s", pPrefix, pRational->Numerator, pRational->Denominator, pRational->Numerator/pRational->Denominator, pSuffix));
}

void vboxVidPnDumpRanges(const char *pPrefix, const D3DKMDT_COLOR_COEFF_DYNAMIC_RANGES *pDynamicRanges, const char *pSuffix)
{
    LOGREL_EXACT(("%sFirstChannel(%d), SecondChannel(%d), ThirdChannel(%d), FourthChannel(%d)%s", pPrefix,
            pDynamicRanges->FirstChannel,
            pDynamicRanges->SecondChannel,
            pDynamicRanges->ThirdChannel,
            pDynamicRanges->FourthChannel,
            pSuffix));
}

void vboxVidPnDumpGammaRamp(const char *pPrefix, const D3DKMDT_GAMMA_RAMP *pGammaRamp, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), DataSize(%d), TODO: dump the rest%s", pPrefix,
            vboxVidPnDumpStrGammaRampType(pGammaRamp->Type), pGammaRamp->DataSize,
            pSuffix));
}

void VBoxVidPnDumpSourceMode(const char *pPrefix, const D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), ", pPrefix, vboxVidPnDumpStrSourceModeType(pVidPnSourceModeInfo->Type)));
    vboxVidPnDumpRegion("surf(", &pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize, "), ");
    vboxVidPnDumpRegion("vis(", &pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize, "), ");
    LOGREL_EXACT(("stride(%d), ", pVidPnSourceModeInfo->Format.Graphics.Stride));
    LOGREL_EXACT(("format(%s), ", vboxVidPnDumpStrPixFormat(pVidPnSourceModeInfo->Format.Graphics.PixelFormat)));
    LOGREL_EXACT(("clrBasis(%s), ", vboxVidPnDumpStrColorBasis(pVidPnSourceModeInfo->Format.Graphics.ColorBasis)));
    LOGREL_EXACT(("pvam(%s)%s", vboxVidPnDumpStrPvam(pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode), pSuffix));
}

void vboxVidPnDumpSignalInfo(const char *pPrefix, const D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%sVStd(%s), ", pPrefix, vboxVidPnDumpStrSignalStandard(pVideoSignalInfo->VideoStandard)));
    vboxVidPnDumpRegion("totSize(", &pVideoSignalInfo->TotalSize, "), ");
    vboxVidPnDumpRegion("activeSize(", &pVideoSignalInfo->ActiveSize, "), ");
    vboxVidPnDumpRational("VSynch(", &pVideoSignalInfo->VSyncFreq, "), ");
    LOGREL_EXACT(("PixelRate(%d), ScanLineOrdering(%s)%s", pVideoSignalInfo->PixelRate, vboxVidPnDumpStrScanLineOrdering(pVideoSignalInfo->ScanLineOrdering), pSuffix));
}

void VBoxVidPnDumpTargetMode(const char *pPrefix, const D3DKMDT_VIDPN_TARGET_MODE* CONST  pVidPnTargetModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));
    LOGREL_EXACT(("ID: %d, ", pVidPnTargetModeInfo->Id));
    vboxVidPnDumpSignalInfo("VSI: ", &pVidPnTargetModeInfo->VideoSignalInfo, ", ");
    LOGREL_EXACT(("Preference(%s)%s", vboxVidPnDumpStrModePreference(pVidPnTargetModeInfo->Preference), pSuffix));
}

void VBoxVidPnDumpMonitorMode(const char *pPrefix, const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));

    LOGREL_EXACT(("ID: %d, ", pVidPnModeInfo->Id));

    vboxVidPnDumpSignalInfo("VSI: ", &pVidPnModeInfo->VideoSignalInfo, ", ");

    LOGREL_EXACT(("ColorBasis: %s, ", vboxVidPnDumpStrColorBasis(pVidPnModeInfo->ColorBasis)));

    vboxVidPnDumpRanges("Ranges: ", &pVidPnModeInfo->ColorCoeffDynamicRanges, ", ");

    LOGREL_EXACT(("MonCapOr: %s, ", vboxVidPnDumpStrMonCapabilitiesOrigin(pVidPnModeInfo->Origin)));

    LOGREL_EXACT(("Preference(%s)%s", vboxVidPnDumpStrModePreference(pVidPnModeInfo->Preference), pSuffix));
}

NTSTATUS VBoxVidPnDumpMonitorModeSet(const char *pPrefix, PVBOXMP_DEVEXT pDevExt, uint32_t u32Target, const char *pSuffix)
{
    LOGREL_EXACT(("%s Tgt[%d]\n", pPrefix, u32Target));

    NTSTATUS Status;
    CONST DXGK_MONITOR_INTERFACE *pMonitorInterface;
    Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryMonitorInterface(pDevExt->u.primary.DxgkInterface.DeviceHandle, DXGK_MONITOR_INTERFACE_VERSION_V1, &pMonitorInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

    Status = pMonitorInterface->pfnAcquireMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                                        u32Target,
                                        &hVidPnModeSet,
                                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    VBOXVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    VBoxVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = VBoxVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        VBoxVidPnDumpMonitorMode("MonitorMode: ",pVidPnModeInfo, "\n");
    }

    VBoxVidPnMonitorModeIterTerm(&Iter);

    Status = VBoxVidPnMonitorModeIterStatus(&Iter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("iter status failed %#x", Status));
    }

    NTSTATUS rcNt2 = pMonitorInterface->pfnReleaseMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle, hVidPnModeSet);
    if (!NT_SUCCESS(rcNt2))
        WARN(("pfnReleaseMonitorSourceModeSet failed rcNt2(0x%x)", rcNt2));

    LOGREL_EXACT(("%s", pSuffix));

    return Status;
}

void vboxVidPnDumpPinnedSourceMode(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;

        Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            VBoxVidPnDumpSourceMode("Source Pinned: ", pPinnedVidPnSourceModeInfo, "\n");
            pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            LOGREL_EXACT(("Source NOT Pinned\n"));
        }
        else
        {
            LOGREL_EXACT(("ERROR getting piined Source Mode(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting SourceModeSet(0x%x)\n", Status));
    }
}


DECLCALLBACK(BOOLEAN) vboxVidPnDumpSourceModeSetEnum(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet,
                                                     const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                                     const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext)
{

    RT_NOREF(hNewVidPnSourceModeSet, pVidPnSourceModeSetInterface, pNewVidPnSourceModeInfo, pContext);
    VBoxVidPnDumpSourceMode("SourceMode: ", pNewVidPnSourceModeInfo, "\n");
    return TRUE;
}

void vboxVidPnDumpSourceModeSet(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
                                D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    RT_NOREF(pDevExt);
    LOGREL_EXACT(("\n  >>>+++SourceMode Set for Source(%d)+++\n", VidPnSourceId));
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {

        Status = vboxVidPnEnumSourceModes(hCurVidPnSourceModeSet, pCurVidPnSourceModeSetInterface,
                vboxVidPnDumpSourceModeSetEnum, NULL);
        AssertNtStatusSuccess(Status);
        if (Status != STATUS_SUCCESS)
        {
            LOGREL_EXACT(("ERROR enumerating Source Modes(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting SourceModeSet for Source(%d), Status(0x%x)\n", VidPnSourceId, Status));
    }

    LOGREL_EXACT(("  <<<+++End Of SourceMode Set for Source(%d)+++", VidPnSourceId));
}

DECLCALLBACK(BOOLEAN) vboxVidPnDumpTargetModeSetEnum(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet,
                                                     const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                                     const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext)
{
    RT_NOREF(hNewVidPnTargetModeSet, pVidPnTargetModeSetInterface, pNewVidPnTargetModeInfo, pContext);
    VBoxVidPnDumpTargetMode("TargetMode: ", pNewVidPnTargetModeInfo, "\n");
    return TRUE;
}

void vboxVidPnDumpTargetModeSet(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
                                D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RT_NOREF(pDevExt);
    LOGREL_EXACT(("\n  >>>---TargetMode Set for Target(%d)---\n", VidPnTargetId));
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {

        Status = vboxVidPnEnumTargetModes(hCurVidPnTargetModeSet, pCurVidPnTargetModeSetInterface,
                vboxVidPnDumpTargetModeSetEnum, NULL);
        AssertNtStatusSuccess(Status);
        if (Status != STATUS_SUCCESS)
        {
            LOGREL_EXACT(("ERROR enumerating Target Modes(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting TargetModeSet for Target(%d), Status(0x%x)\n", VidPnTargetId, Status));
    }

    LOGREL_EXACT(("  <<<---End Of TargetMode Set for Target(%d)---", VidPnTargetId));
}


void vboxVidPnDumpPinnedTargetMode(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;

        Status = pCurVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            VBoxVidPnDumpTargetMode("Target Pinned: ", pPinnedVidPnTargetModeInfo, "\n");
            pCurVidPnTargetModeSetInterface->pfnReleaseModeInfo(hCurVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            LOGREL_EXACT(("Target NOT Pinned\n"));
        }
        else
        {
            LOGREL_EXACT(("ERROR getting piined Target Mode(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting TargetModeSet(0x%x)\n", Status));
    }
}

void VBoxVidPnDumpCofuncModalityInfo(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmEnumPivotType, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix)
{
    LOGREL_EXACT(("%sPivotType(%s), SourceId(0x%x), TargetId(0x%x),%s", pPrefix, vboxVidPnDumpStrCFMPivotType(enmEnumPivotType),
            pPivot->VidPnSourceId, pPivot->VidPnTargetId, pSuffix));
}

void vboxVidPnDumpCofuncModalityArg(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix)
{
    LOGREL_EXACT(("%sPivotType(%s), SourceId(0x%x), TargetId(0x%x),%s", pPrefix, vboxVidPnDumpStrCFMPivotType(enmPivot),
            pPivot->VidPnSourceId, pPivot->VidPnTargetId, pSuffix));
}

void vboxVidPnDumpPath(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo)
{
    LOGREL_EXACT((" >>**** Start Dump VidPn Path ****>>\n"));
    LOGREL_EXACT(("VidPnSourceId(%d),  VidPnTargetId(%d)\n",
            pVidPnPresentPathInfo->VidPnSourceId, pVidPnPresentPathInfo->VidPnTargetId));

    vboxVidPnDumpPinnedSourceMode(hVidPn, pVidPnInterface, pVidPnPresentPathInfo->VidPnSourceId);
    vboxVidPnDumpPinnedTargetMode(hVidPn, pVidPnInterface, pVidPnPresentPathInfo->VidPnTargetId);

    vboxVidPnDumpPathTransformation(&pVidPnPresentPathInfo->ContentTransformation);

    LOGREL_EXACT(("Importance(%s), TargetColorBasis(%s), Content(%s), ",
            vboxVidPnDumpStrImportance(pVidPnPresentPathInfo->ImportanceOrdinal),
            vboxVidPnDumpStrColorBasis(pVidPnPresentPathInfo->VidPnTargetColorBasis),
            vboxVidPnDumpStrContent(pVidPnPresentPathInfo->Content)));
    vboxVidPnDumpRegion("VFA_TL_O(", &pVidPnPresentPathInfo->VisibleFromActiveTLOffset, "), ");
    vboxVidPnDumpRegion("VFA_BR_O(", &pVidPnPresentPathInfo->VisibleFromActiveBROffset, "), ");
    vboxVidPnDumpRanges("CCDynamicRanges: ", &pVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges, "| ");
    vboxVidPnDumpCopyProtectoin("CProtection: ", &pVidPnPresentPathInfo->CopyProtection, "| ");
    vboxVidPnDumpGammaRamp("GammaRamp: ", &pVidPnPresentPathInfo->GammaRamp, "\n");

    LOGREL_EXACT((" <<**** Stop Dump VidPn Path ****<<"));
}

typedef struct VBOXVIDPNDUMPPATHENUM
{
    D3DKMDT_HVIDPN hVidPn;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
} VBOXVIDPNDUMPPATHENUM, *PVBOXVIDPNDUMPPATHENUM;

static DECLCALLBACK(BOOLEAN) vboxVidPnDumpPathEnum(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo, PVOID pContext)
{
    PVBOXVIDPNDUMPPATHENUM pData = (PVBOXVIDPNDUMPPATHENUM)pContext;
    vboxVidPnDumpPath(pData->hVidPn, pData->pVidPnInterface, pVidPnPresentPathInfo);

    pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathInfo);
    return TRUE;
}

void vboxVidPnDumpVidPn(const char * pPrefix, PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const char * pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));

    VBOXVIDPNDUMPPATHENUM CbData;
    CbData.hVidPn = hVidPn;
    CbData.pVidPnInterface = pVidPnInterface;
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        Status = vboxVidPnEnumPaths(hVidPnTopology, pVidPnTopologyInterface,
                                        vboxVidPnDumpPathEnum, &CbData);
        AssertNtStatusSuccess(Status);
    }

    for (int i = 0; i < VBoxCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        vboxVidPnDumpSourceModeSet(pDevExt, hVidPn, pVidPnInterface, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)i);
        vboxVidPnDumpTargetModeSet(pDevExt, hVidPn, pVidPnInterface, (D3DDDI_VIDEO_PRESENT_TARGET_ID)i);
    }

    LOGREL_EXACT(("%s", pSuffix));
}
