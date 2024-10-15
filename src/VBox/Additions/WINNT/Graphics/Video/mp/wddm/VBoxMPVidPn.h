/* $Id: VBoxMPVidPn.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVidPn_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVidPn_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOXVDPN_C_DISPLAY_HBLANK_SIZE 200
#define VBOXVDPN_C_DISPLAY_VBLANK_SIZE 180

void VBoxVidPnAllocDataInit(struct VBOXWDDM_ALLOC_DATA *pData, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);

void VBoxVidPnSourceInit(PVBOXWDDM_SOURCE pSource, const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, uint8_t u8SyncState);
void VBoxVidPnTargetInit(PVBOXWDDM_TARGET pTarget, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, uint8_t u8SyncState);
void VBoxVidPnSourceCopy(VBOXWDDM_SOURCE *pDst, const VBOXWDDM_SOURCE *pSrc);
void VBoxVidPnTargetCopy(VBOXWDDM_TARGET *pDst, const VBOXWDDM_TARGET *pSrc);

void VBoxVidPnSourcesInit(PVBOXWDDM_SOURCE pSources, uint32_t cScreens, uint8_t u8SyncState);
void VBoxVidPnTargetsInit(PVBOXWDDM_TARGET pTargets, uint32_t cScreens, uint8_t u8SyncState);
void VBoxVidPnSourcesCopy(VBOXWDDM_SOURCE *pDst, const VBOXWDDM_SOURCE *pSrc, uint32_t cScreens);
void VBoxVidPnTargetsCopy(VBOXWDDM_TARGET *pDst, const VBOXWDDM_TARGET *pSrc, uint32_t cScreens);

typedef struct VBOXWDDM_TARGET_ITER
{
    PVBOXWDDM_SOURCE pSource;
    PVBOXWDDM_TARGET paTargets;
    uint32_t cTargets;
    uint32_t i;
    uint32_t c;
} VBOXWDDM_TARGET_ITER;

void VBoxVidPnStCleanup(PVBOXWDDM_SOURCE paSources, PVBOXWDDM_TARGET paTargets, uint32_t cScreens);
void VBoxVidPnStTIterInit(PVBOXWDDM_SOURCE pSource, PVBOXWDDM_TARGET paTargets, uint32_t cTargets, VBOXWDDM_TARGET_ITER *pIter);
PVBOXWDDM_TARGET VBoxVidPnStTIterNext(VBOXWDDM_TARGET_ITER *pIter);

void VBoxDumpSourceTargetArrays(VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets, uint32_t cScreens);

/* !!!NOTE: The callback is responsible for releasing the path */
typedef DECLCALLBACKTYPE(BOOLEAN, FNVBOXVIDPNENUMPATHS,(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
                                                        const DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface,
                                                        const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo,
                                                        PVOID pContext));
typedef FNVBOXVIDPNENUMPATHS *PFNVBOXVIDPNENUMPATHS;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACKTYPE(BOOLEAN, FNVBOXVIDPNENUMSOURCEMODES,(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet,
                                                              const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                                              const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo,
                                                              PVOID pContext));
typedef FNVBOXVIDPNENUMSOURCEMODES *PFNVBOXVIDPNENUMSOURCEMODES;

/* !!!NOTE: The callback is responsible for releasing the target mode info */
typedef DECLCALLBACKTYPE(BOOLEAN, FNVBOXVIDPNENUMTARGETMODES,(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet,
                                                              const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                                              const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo,
                                                              PVOID pContext));
typedef FNVBOXVIDPNENUMTARGETMODES *PFNVBOXVIDPNENUMTARGETMODES;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACKTYPE(BOOLEAN, FNVBOXVIDPNENUMMONITORSOURCEMODES,(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS,
                                                                     CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
                                                                     CONST D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSMI,
                                                                     PVOID pContext));
typedef FNVBOXVIDPNENUMMONITORSOURCEMODES *PFNVBOXVIDPNENUMMONITORSOURCEMODES;

typedef DECLCALLBACKTYPE(BOOLEAN, FNVBOXVIDPNENUMTARGETSFORSOURCE,(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
                                                                   const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
                                                                   CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
                                                                   D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
                                                                   SIZE_T cTgtPaths, PVOID pContext));
typedef FNVBOXVIDPNENUMTARGETSFORSOURCE *PFNVBOXVIDPNENUMTARGETSFORSOURCE;

NTSTATUS VBoxVidPnCommitSourceModeForSrcId(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PVBOXWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets, BOOLEAN bPathPowerTransition);

NTSTATUS VBoxVidPnCommitAll(PVBOXMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PVBOXWDDM_ALLOCATION pAllocation,
        VBOXWDDM_SOURCE *paSources, VBOXWDDM_TARGET *paTargets);

NTSTATUS vboxVidPnEnumPaths(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        PFNVBOXVIDPNENUMPATHS pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumSourceModes(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        PFNVBOXVIDPNENUMSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumTargetModes(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        PFNVBOXVIDPNENUMTARGETMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumMonitorSourceModes(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        PFNVBOXVIDPNENUMMONITORSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumTargetsForSource(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        PFNVBOXVIDPNENUMTARGETSFORSOURCE pfnCallback, PVOID pContext);

void VBoxVidPnDumpTargetMode(const char *pPrefix, const D3DKMDT_VIDPN_TARGET_MODE* CONST  pVidPnTargetModeInfo, const char *pSuffix);
void VBoxVidPnDumpMonitorMode(const char *pPrefix, const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo, const char *pSuffix);
NTSTATUS VBoxVidPnDumpMonitorModeSet(const char *pPrefix, PVBOXMP_DEVEXT pDevExt, uint32_t u32Target, const char *pSuffix);
void VBoxVidPnDumpSourceMode(const char *pPrefix, const D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, const char *pSuffix);
void VBoxVidPnDumpCofuncModalityInfo(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmEnumPivotType, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix);

void vboxVidPnDumpVidPn(const char * pPrefix, PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const char * pSuffix);
void vboxVidPnDumpCofuncModalityArg(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix);
DECLCALLBACK(BOOLEAN) vboxVidPnDumpSourceModeSetEnum(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext);
DECLCALLBACK(BOOLEAN) vboxVidPnDumpTargetModeSetEnum(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext);


typedef struct VBOXVIDPN_SOURCEMODE_ITER
{
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_VIDPN_SOURCE_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} VBOXVIDPN_SOURCEMODE_ITER;

DECLINLINE(void) VBoxVidPnSourceModeIterInit(VBOXVIDPN_SOURCEMODE_ITER *pIter, D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) VBoxVidPnSourceModeIterTerm(VBOXVIDPN_SOURCEMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_SOURCE_MODE *) VBoxVidPnSourceModeIterNext(VBOXVIDPN_SOURCEMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_SOURCE_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Source info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) VBoxVidPnSourceModeIterStatus(VBOXVIDPN_SOURCEMODE_ITER *pIter)
{
    return pIter->Status;
}

typedef struct VBOXVIDPN_TARGETMODE_ITER
{
    D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_VIDPN_TARGET_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} VBOXVIDPN_TARGETMODE_ITER;

DECLINLINE(void) VBoxVidPnTargetModeIterInit(VBOXVIDPN_TARGETMODE_ITER *pIter,D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) VBoxVidPnTargetModeIterTerm(VBOXVIDPN_TARGETMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_TARGET_MODE *) VBoxVidPnTargetModeIterNext(VBOXVIDPN_TARGETMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_TARGET_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Target info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) VBoxVidPnTargetModeIterStatus(VBOXVIDPN_TARGETMODE_ITER *pIter)
{
    return pIter->Status;
}


typedef struct VBOXVIDPN_MONITORMODE_ITER
{
    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_MONITOR_SOURCE_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} VBOXVIDPN_MONITORMODE_ITER;


DECLINLINE(void) VBoxVidPnMonitorModeIterInit(VBOXVIDPN_MONITORMODE_ITER *pIter, D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) VBoxVidPnMonitorModeIterTerm(VBOXVIDPN_MONITORMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_MONITOR_SOURCE_MODE *) VBoxVidPnMonitorModeIterNext(VBOXVIDPN_MONITORMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_MONITOR_SOURCE_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Monitor info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) VBoxVidPnMonitorModeIterStatus(VBOXVIDPN_MONITORMODE_ITER *pIter)
{
    return pIter->Status;
}



typedef struct VBOXVIDPN_PATH_ITER
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    const D3DKMDT_VIDPN_PRESENT_PATH *pCurVidPnPathInfo;
    NTSTATUS Status;
} VBOXVIDPN_PATH_ITER;


DECLINLINE(void) VBoxVidPnPathIterInit(VBOXVIDPN_PATH_ITER *pIter, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface)
{
    pIter->hVidPnTopology = hVidPnTopology;
    pIter->pVidPnTopologyInterface = pVidPnTopologyInterface;
    pIter->pCurVidPnPathInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) VBoxVidPnPathIterTerm(VBOXVIDPN_PATH_ITER *pIter)
{
    if (pIter->pCurVidPnPathInfo)
    {
        pIter->pVidPnTopologyInterface->pfnReleasePathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo);
        pIter->pCurVidPnPathInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_PRESENT_PATH *) VBoxVidPnPathIterNext(VBOXVIDPN_PATH_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_PRESENT_PATH *pCurVidPnPathInfo;

    if (!pIter->pCurVidPnPathInfo)
        Status = pIter->pVidPnTopologyInterface->pfnAcquireFirstPathInfo(pIter->hVidPnTopology, &pCurVidPnPathInfo);
    else
        Status = pIter->pVidPnTopologyInterface->pfnAcquireNextPathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo, &pCurVidPnPathInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnPathInfo);

        if (pIter->pCurVidPnPathInfo)
            pIter->pVidPnTopologyInterface->pfnReleasePathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo);

        pIter->pCurVidPnPathInfo = pCurVidPnPathInfo;
        return pCurVidPnPathInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Path info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS)  VBoxVidPnPathIterStatus(VBOXVIDPN_PATH_ITER *pIter)
{
    return pIter->Status;
}

NTSTATUS VBoxVidPnRecommendMonitorModes(PVBOXMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
                        D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface);

NTSTATUS VBoxVidPnRecommendFunctional(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const VBOXWDDM_RECOMMENDVIDPN *pData);

NTSTATUS VBoxVidPnCofuncModality(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot);

NTSTATUS VBoxVidPnIsSupported(PVBOXMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, BOOLEAN *pfSupported);

NTSTATUS VBoxVidPnUpdateModes(PVBOXMP_DEVEXT pDevExt, uint32_t u32TargetId, const RTRECTSIZE *pSize);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVidPn_h */
