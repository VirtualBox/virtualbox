/* $Id: VBoxDispIf.cpp 110208 2025-07-13 17:55:53Z dmitrii.grigorev@oracle.com $ */
/** @file
 * VBoxTray - Display Settings Interface abstraction for XPDM & WDDM
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define _WIN32_WINNT 0x0601
#include "VBoxTray.h"
#include "VBoxTrayInternal.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/system.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG
# define WARN(_m) do { \
            AssertFailed(); \
            LogRelFunc(_m); \
        } while (0)
#else
# define WARN(_m) do { \
            LogRelFunc(_m); \
        } while (0)
#endif

#ifdef VBOX_WITH_WDDM
#include <iprt/asm.h>
#endif

#include "VBoxDisplay.h"

#ifndef NT_SUCCESS
# define NT_SUCCESS(_Status) ((_Status) >= 0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VBOXDISPIF_OP
{
    PCVBOXDISPIF pIf;
#ifdef VBOX_WITH_WDDM
    union
    {
        struct
        {
            VBOXDISPKMT_ADAPTER Adapter;
            VBOXDISPKMT_DEVICE Device;
            VBOXDISPKMT_CONTEXT Context;
        } wddm;
    } u;
#endif
} VBOXDISPIF_OP;

/*
 * APIs specific to Win7 and above WDDM architecture. Not available for Vista WDDM.
 * This is the reason they have not been put in the VBOXDISPIF struct in VBoxDispIf.h.
 */
typedef struct _VBOXDISPLAYWDDMAPICONTEXT
{
    DECLCALLBACKMEMBER_EX(LONG, WINAPI, pfnSetDisplayConfig,(UINT numPathArrayElements, DISPLAYCONFIG_PATH_INFO *pathArray,
                                                             UINT numModeInfoArrayElements,
                                                             DISPLAYCONFIG_MODE_INFO *modeInfoArray, UINT Flags));
    DECLCALLBACKMEMBER_EX(LONG, WINAPI, pfnQueryDisplayConfig,(UINT Flags, UINT *pNumPathArrayElements,
                                                               DISPLAYCONFIG_PATH_INFO *pPathInfoArray,
                                                               UINT *pNumModeInfoArrayElements,
                                                               DISPLAYCONFIG_MODE_INFO *pModeInfoArray,
                                                               DISPLAYCONFIG_TOPOLOGY_ID *pCurrentTopologyId));
    DECLCALLBACKMEMBER_EX(LONG, WINAPI, pfnGetDisplayConfigBufferSizes,(UINT Flags, UINT *pNumPathArrayElements,
                                                                        UINT *pNumModeInfoArrayElements));
} _VBOXDISPLAYWDDMAPICONTEXT;

static _VBOXDISPLAYWDDMAPICONTEXT gCtx = {0};

typedef struct VBOXDISPIF_WDDM_DISPCFG
{
    UINT32 cPathInfoArray;
    DISPLAYCONFIG_PATH_INFO *pPathInfoArray;
    UINT32 cModeInfoArray;
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray;
} VBOXDISPIF_WDDM_DISPCFG;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DWORD vboxDispIfWddmResizeDisplay(PCVBOXDISPIF const pIf, UINT Id, BOOL fEnable, DISPLAY_DEVICE * paDisplayDevices,
                                         DEVMODE *paDeviceModes, UINT devModes);

static DWORD vboxDispIfWddmResizeDisplay2(PCVBOXDISPIF const pIf, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT devModes);

static DWORD vboxDispIfResizePerform(PCVBOXDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup,
                                     DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes);
static DWORD vboxDispIfWddmEnableDisplaysTryingTopology(PCVBOXDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnable);
static DWORD vboxDispIfResizeStartedOpWDDM(VBOXDISPIF_OP *pOp);

static void vboxDispIfWddmDcLogRel(VBOXDISPIF_WDDM_DISPCFG const *pCfg, UINT fFlags)
{
    LogRel2(("Display config: Flags = 0x%08X\n", fFlags));

    LogRel2(("PATH_INFO[%d]:\n", pCfg->cPathInfoArray));
    for (uint32_t i = 0; i < pCfg->cPathInfoArray; ++i)
    {
        DISPLAYCONFIG_PATH_INFO *p = &pCfg->pPathInfoArray[i];

        LogRel2(("%d: flags 0x%08x\n", i, p->flags));

        LogRel2(("  sourceInfo: adapterId 0x%08x:%08x, id %u, modeIdx %d, statusFlags 0x%08x\n",
                p->sourceInfo.adapterId.HighPart, p->sourceInfo.adapterId.LowPart,
                p->sourceInfo.id, p->sourceInfo.modeInfoIdx, p->sourceInfo.statusFlags));

        LogRel2(("  targetInfo: adapterId 0x%08x:%08x, id %u, modeIdx %d,\n"
                "              ot %d, r %d, s %d, rr %d/%d, so %d, ta %d, statusFlags 0x%08x\n",
                p->targetInfo.adapterId.HighPart, p->targetInfo.adapterId.LowPart,
                p->targetInfo.id, p->targetInfo.modeInfoIdx,
                p->targetInfo.outputTechnology,
                p->targetInfo.rotation,
                p->targetInfo.scaling,
                p->targetInfo.refreshRate.Numerator, p->targetInfo.refreshRate.Denominator,
                p->targetInfo.scanLineOrdering,
                p->targetInfo.targetAvailable,
                p->targetInfo.statusFlags
              ));
    }

    LogRel2(("MODE_INFO[%d]:\n", pCfg->cModeInfoArray));
    for (uint32_t i = 0; i < pCfg->cModeInfoArray; ++i)
    {
        DISPLAYCONFIG_MODE_INFO *p = &pCfg->pModeInfoArray[i];

        LogRel2(("%d: adapterId 0x%08x:%08x, id %u\n",
                i, p->adapterId.HighPart, p->adapterId.LowPart, p->id));

        if (p->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
        {
            LogRel2(("  src %ux%u, fmt %d, @%dx%d\n",
                    p->sourceMode.width, p->sourceMode.height, p->sourceMode.pixelFormat,
                    p->sourceMode.position.x, p->sourceMode.position.y));
        }
        else if (p->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
        {
            LogRel2(("  tgt pr 0x%RX64, hSyncFreq %d/%d, vSyncFreq %d/%d, active %ux%u, total %ux%u, std %d, so %d\n",
                    p->targetMode.targetVideoSignalInfo.pixelRate,
                    p->targetMode.targetVideoSignalInfo.hSyncFreq.Numerator, p->targetMode.targetVideoSignalInfo.hSyncFreq.Denominator,
                    p->targetMode.targetVideoSignalInfo.vSyncFreq.Numerator, p->targetMode.targetVideoSignalInfo.vSyncFreq.Denominator,
                    p->targetMode.targetVideoSignalInfo.activeSize.cx, p->targetMode.targetVideoSignalInfo.activeSize.cy,
                    p->targetMode.targetVideoSignalInfo.totalSize.cx, p->targetMode.targetVideoSignalInfo.totalSize.cy,
                    p->targetMode.targetVideoSignalInfo.videoStandard,
                    p->targetMode.targetVideoSignalInfo.scanLineOrdering));
        }
        else
        {
            LogRel2(("  Invalid infoType %u(0x%08x)\n", p->infoType, p->infoType));
        }
    }
}

static DWORD vboxDispIfWddmDcCreate(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT32 fFlags)
{
    UINT32 cPathInfoArray = 0;
    UINT32 cModeInfoArray = 0;
    DWORD winEr = gCtx.pfnGetDisplayConfigBufferSizes(fFlags, &cPathInfoArray, &cModeInfoArray);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed GetDisplayConfigBufferSizes\n"));
        return winEr;
    }

    DISPLAYCONFIG_PATH_INFO *pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)RTMemAlloc(  cPathInfoArray
                                                                                    * sizeof(DISPLAYCONFIG_PATH_INFO));
    if (!pPathInfoArray)
    {
        WARN(("VBoxTray: (WDDM) RTMemAlloc failed!\n"));
        return ERROR_OUTOFMEMORY;
    }
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)RTMemAlloc(  cModeInfoArray
                                                                                    * sizeof(DISPLAYCONFIG_MODE_INFO));
    if (!pModeInfoArray)
    {
        WARN(("VBoxTray: (WDDM) RTMemAlloc failed!\n"));
        RTMemFree(pPathInfoArray);
        return ERROR_OUTOFMEMORY;
    }

    winEr = gCtx.pfnQueryDisplayConfig(fFlags, &cPathInfoArray, pPathInfoArray, &cModeInfoArray, pModeInfoArray, NULL);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed QueryDisplayConfig\n"));
        RTMemFree(pPathInfoArray);
        RTMemFree(pModeInfoArray);
        return winEr;
    }

    pCfg->cPathInfoArray = cPathInfoArray;
    pCfg->pPathInfoArray = pPathInfoArray;
    pCfg->cModeInfoArray = cModeInfoArray;
    pCfg->pModeInfoArray = pModeInfoArray;
    return ERROR_SUCCESS;
}

static DWORD vboxDispIfWddmDcClone(VBOXDISPIF_WDDM_DISPCFG *pCfg, VBOXDISPIF_WDDM_DISPCFG *pCfgDst)
{
    memset(pCfgDst, 0, sizeof (*pCfgDst));

    if (pCfg->cPathInfoArray)
    {
        pCfgDst->pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)RTMemAlloc(pCfg->cPathInfoArray * sizeof (DISPLAYCONFIG_PATH_INFO));
        if (!pCfgDst->pPathInfoArray)
        {
            WARN(("VBoxTray: (WDDM) RTMemAlloc failed!\n"));
            return ERROR_OUTOFMEMORY;
        }

        memcpy(pCfgDst->pPathInfoArray, pCfg->pPathInfoArray, pCfg->cPathInfoArray * sizeof (DISPLAYCONFIG_PATH_INFO));

        pCfgDst->cPathInfoArray = pCfg->cPathInfoArray;
    }

    if (pCfg->cModeInfoArray)
    {
        pCfgDst->pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)RTMemAlloc(pCfg->cModeInfoArray * sizeof (DISPLAYCONFIG_MODE_INFO));
        if (!pCfgDst->pModeInfoArray)
        {
            WARN(("VBoxTray: (WDDM) RTMemAlloc failed!\n"));
            if (pCfgDst->pPathInfoArray)
            {
                RTMemFree(pCfgDst->pPathInfoArray);
                pCfgDst->pPathInfoArray = NULL;
            }
            return ERROR_OUTOFMEMORY;
        }

        memcpy(pCfgDst->pModeInfoArray, pCfg->pModeInfoArray, pCfg->cModeInfoArray * sizeof (DISPLAYCONFIG_MODE_INFO));

        pCfgDst->cModeInfoArray = pCfg->cModeInfoArray;
    }

    return ERROR_SUCCESS;
}


static VOID vboxDispIfWddmDcTerm(VBOXDISPIF_WDDM_DISPCFG *pCfg)
{
    if (pCfg->pPathInfoArray)
        RTMemFree(pCfg->pPathInfoArray);
    if (pCfg->pModeInfoArray)
        RTMemFree(pCfg->pModeInfoArray);
    /* sanity */
    memset(pCfg, 0, sizeof (*pCfg));
}

static UINT32 g_cVBoxDispIfWddmDisplays = 0;
static DWORD vboxDispIfWddmDcQueryNumDisplays(UINT32 *pcDisplays)
{
    if (!g_cVBoxDispIfWddmDisplays)
    {
        VBOXDISPIF_WDDM_DISPCFG DispCfg;
        *pcDisplays = 0;
        DWORD winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray:(WDDM) vboxDispIfWddmDcCreate Failed winEr %d\n", winEr));
            return winEr;
        }

        int cDisplays = -1;

        for (UINT iter = 0; iter < DispCfg.cPathInfoArray; ++iter)
        {
            if (cDisplays < (int)(DispCfg.pPathInfoArray[iter].sourceInfo.id))
                cDisplays = (int)(DispCfg.pPathInfoArray[iter].sourceInfo.id);
        }

        cDisplays++;

        g_cVBoxDispIfWddmDisplays = cDisplays;
        Assert(g_cVBoxDispIfWddmDisplays);

        vboxDispIfWddmDcTerm(&DispCfg);
    }

    *pcDisplays = g_cVBoxDispIfWddmDisplays;
    return ERROR_SUCCESS;
}

#define VBOX_WDDM_DC_SEARCH_PATH_ANY (~(UINT)0)
static int vboxDispIfWddmDcSearchPath(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT srcId, UINT trgId)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if (   (srcId == VBOX_WDDM_DC_SEARCH_PATH_ANY || pCfg->pPathInfoArray[iter].sourceInfo.id == srcId)
            && (trgId == VBOX_WDDM_DC_SEARCH_PATH_ANY || pCfg->pPathInfoArray[iter].targetInfo.id == trgId))
        {
            return (int)iter;
        }
    }
    return -1;
}

static int vboxDispIfWddmDcSearchActiveSourcePath(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT srcId)
{
    for (UINT i = 0; i < pCfg->cPathInfoArray; ++i)
    {
        if (   pCfg->pPathInfoArray[i].sourceInfo.id == srcId
            && RT_BOOL(pCfg->pPathInfoArray[i].flags & DISPLAYCONFIG_PATH_ACTIVE))
        {
            return (int)i;
        }
    }
    return -1;
}

static int vboxDispIfWddmDcSearchActivePath(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT srcId, UINT trgId)
{
    int idx = vboxDispIfWddmDcSearchPath(pCfg, srcId, trgId);
    if (idx < 0)
        return idx;

    if (!(pCfg->pPathInfoArray[idx].flags & DISPLAYCONFIG_PATH_ACTIVE))
        return -1;

    return idx;
}

static VOID vboxDispIfWddmDcSettingsInvalidateModeIndex(VBOXDISPIF_WDDM_DISPCFG *pCfg, int idx)
{
    pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    pCfg->pPathInfoArray[idx].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
}

static VOID vboxDispIfWddmDcSettingsInvalidateModeIndeces(VBOXDISPIF_WDDM_DISPCFG *pCfg)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        vboxDispIfWddmDcSettingsInvalidateModeIndex(pCfg, (int)iter);
    }

    if (pCfg->pModeInfoArray)
    {
        RTMemFree(pCfg->pModeInfoArray);
        pCfg->pModeInfoArray = NULL;
    }
    pCfg->cModeInfoArray = 0;
}

static DWORD vboxDispIfWddmDcSettingsModeAdd(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT *pIdx)
{
    UINT32 cModeInfoArray = pCfg->cModeInfoArray + 1;
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)RTMemAlloc(  cModeInfoArray
                                                                                    * sizeof (DISPLAYCONFIG_MODE_INFO));
    if (!pModeInfoArray)
    {
        WARN(("VBoxTray: (WDDM) RTMemAlloc failed!\n"));
        return ERROR_OUTOFMEMORY;
    }

    memcpy (pModeInfoArray, pCfg->pModeInfoArray, pCfg->cModeInfoArray * sizeof(DISPLAYCONFIG_MODE_INFO));
    memset(&pModeInfoArray[cModeInfoArray-1], 0, sizeof (pModeInfoArray[0]));
    RTMemFree(pCfg->pModeInfoArray);
    *pIdx = cModeInfoArray-1;
    pCfg->pModeInfoArray = pModeInfoArray;
    pCfg->cModeInfoArray = cModeInfoArray;
    return ERROR_SUCCESS;
}

static DWORD vboxDispIfWddmDcSettingsUpdate(VBOXDISPIF_WDDM_DISPCFG *pCfg, int idx, DEVMODE *pDeviceMode, BOOL fInvalidateSrcMode, BOOL fEnable)
{
    if (fInvalidateSrcMode)
        pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    else if (pDeviceMode)
    {
        UINT iSrcMode = pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx;
        if (iSrcMode == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
        {

            WARN(("VBoxTray: (WDDM) no source mode index specified"));
            DWORD winEr = vboxDispIfWddmDcSettingsModeAdd(pCfg, &iSrcMode);
            if (winEr != ERROR_SUCCESS)
            {
                WARN(("VBoxTray:(WDDM) vboxDispIfWddmDcSettingsModeAdd Failed winEr %d\n", winEr));
                return winEr;
            }
            pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = iSrcMode;
        }

        for (int i = 0; i < (int)pCfg->cPathInfoArray; ++i)
        {
            if (i == idx)
                continue;

            if (pCfg->pPathInfoArray[i].sourceInfo.modeInfoIdx == iSrcMode)
            {
                /* this is something we're not expecting/supporting */
                WARN(("VBoxTray: (WDDM) multiple paths have the same mode index"));
                return ERROR_NOT_SUPPORTED;
            }
        }

        if (pDeviceMode->dmFields & DM_PELSWIDTH)
            pCfg->pModeInfoArray[iSrcMode].sourceMode.width = pDeviceMode->dmPelsWidth;
        if (pDeviceMode->dmFields & DM_PELSHEIGHT)
            pCfg->pModeInfoArray[iSrcMode].sourceMode.height = pDeviceMode->dmPelsHeight;
        if (pDeviceMode->dmFields & DM_POSITION)
        {
            LogFlowFunc(("DM_POSITION %d,%d -> %d,%d\n",
                         pCfg->pModeInfoArray[iSrcMode].sourceMode.position.x,
                         pCfg->pModeInfoArray[iSrcMode].sourceMode.position.y,
                         pDeviceMode->dmPosition.x, pDeviceMode->dmPosition.y));
            pCfg->pModeInfoArray[iSrcMode].sourceMode.position.x = pDeviceMode->dmPosition.x;
            pCfg->pModeInfoArray[iSrcMode].sourceMode.position.y = pDeviceMode->dmPosition.y;
        }
        if (pDeviceMode->dmFields & DM_BITSPERPEL)
        {
            switch (pDeviceMode->dmBitsPerPel)
            {
                case 32:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                    break;
                case 24:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
                    break;
                case 16:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
                    break;
                case 8:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
                    break;
                default:
                    LogRel(("VBoxTray: (WDDM) invalid bpp %d, using 32\n", pDeviceMode->dmBitsPerPel));
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                    break;
            }
        }
    }

    pCfg->pPathInfoArray[idx].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

    /* "A refresh rate with both the numerator and denominator set to zero indicates that
     * the caller does not specify a refresh rate and the operating system should use
     * the most optimal refresh rate available. For this case, in a call to the SetDisplayConfig
     * function, the caller must set the scanLineOrdering member to the
     * DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED value; otherwise, SetDisplayConfig fails."
     *
     * If a refresh rate is set to a value, then the resize will fail if miniport driver
     * does not support VSync, i.e. with display-only driver on Win8+ (@bugref{8440}).
     */
    pCfg->pPathInfoArray[idx].targetInfo.refreshRate.Numerator = 0;
    pCfg->pPathInfoArray[idx].targetInfo.refreshRate.Denominator = 0;
    pCfg->pPathInfoArray[idx].targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;

    /* Make sure that "The output can be forced on this target even if a monitor is not detected." */
    pCfg->pPathInfoArray[idx].targetInfo.targetAvailable = TRUE;
    pCfg->pPathInfoArray[idx].targetInfo.statusFlags |= DISPLAYCONFIG_TARGET_FORCIBLE;

    if (fEnable)
        pCfg->pPathInfoArray[idx].flags |= DISPLAYCONFIG_PATH_ACTIVE;
    else
        pCfg->pPathInfoArray[idx].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;

    return ERROR_SUCCESS;
}

static DWORD vboxDispIfWddmDcSet(VBOXDISPIF_WDDM_DISPCFG *pCfg, UINT fFlags)
{
    DWORD winEr = gCtx.pfnSetDisplayConfig(pCfg->cPathInfoArray, pCfg->pPathInfoArray, pCfg->cModeInfoArray, pCfg->pModeInfoArray, fFlags);
    if (winEr != ERROR_SUCCESS)
        Log(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed for Flags 0x%x\n", fFlags));
    return winEr;
}

static BOOL vboxDispIfWddmDcSettingsAdjustSupportedPaths(VBOXDISPIF_WDDM_DISPCFG *pCfg)
{
    BOOL fAdjusted = FALSE;
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if (pCfg->pPathInfoArray[iter].sourceInfo.id == pCfg->pPathInfoArray[iter].targetInfo.id)
            continue;

        if (!(pCfg->pPathInfoArray[iter].flags & DISPLAYCONFIG_PATH_ACTIVE))
            continue;

        pCfg->pPathInfoArray[iter].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
        fAdjusted = TRUE;
    }

    return fAdjusted;
}

static void vboxDispIfWddmDcSettingsAttachDisbledToPrimary(VBOXDISPIF_WDDM_DISPCFG *pCfg)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if ((pCfg->pPathInfoArray[iter].flags & DISPLAYCONFIG_PATH_ACTIVE))
            continue;

        pCfg->pPathInfoArray[iter].sourceInfo.id = 0;
        pCfg->pPathInfoArray[iter].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        pCfg->pPathInfoArray[iter].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    }
}

static DWORD vboxDispIfWddmDcSettingsIncludeAllTargets(VBOXDISPIF_WDDM_DISPCFG *pCfg)
{
    UINT32 cDisplays = 0;
    VBOXDISPIF_WDDM_DISPCFG AllCfg;
    BOOL fAllCfgInited = FALSE;

    DWORD winEr = vboxDispIfWddmDcQueryNumDisplays(&cDisplays);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) vboxDispIfWddmDcQueryNumDisplays Failed winEr %d\n", winEr));
        return winEr;
    }

    DISPLAYCONFIG_PATH_INFO *pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)RTMemAlloc(cDisplays * sizeof(DISPLAYCONFIG_PATH_INFO));
    if (!pPathInfoArray)
    {
        WARN(("RTMemAlloc failed\n"));
        return ERROR_OUTOFMEMORY;
    }

    for (UINT i = 0; i < cDisplays; ++i)
    {
        int idx = vboxDispIfWddmDcSearchPath(pCfg, i, i);
        if (idx < 0)
        {
            idx = vboxDispIfWddmDcSearchPath(pCfg, VBOX_WDDM_DC_SEARCH_PATH_ANY, i);
            if (idx >= 0)
            {
                WARN(("VBoxTray:(WDDM) different source and target paare enabled, this is something we would not expect\n"));
            }
        }

        if (idx >= 0)
            pPathInfoArray[i] = pCfg->pPathInfoArray[idx];
        else
        {
            if (!fAllCfgInited)
            {
                winEr = vboxDispIfWddmDcCreate(&AllCfg, QDC_ALL_PATHS);
                if (winEr != ERROR_SUCCESS)
                {
                    WARN(("VBoxTray:(WDDM) vboxDispIfWddmDcCreate Failed winEr %d\n", winEr));
                    RTMemFree(pPathInfoArray);
                    return winEr;
                }
                fAllCfgInited = TRUE;
            }

            idx = vboxDispIfWddmDcSearchPath(&AllCfg, i, i);
            if (idx < 0)
            {
                WARN(("VBoxTray:(WDDM) %d %d path not supported\n", i, i));
                idx = vboxDispIfWddmDcSearchPath(pCfg, VBOX_WDDM_DC_SEARCH_PATH_ANY, i);
                if (idx < 0)
                {
                    WARN(("VBoxTray:(WDDM) %d %d path not supported\n", -1, i));
                }
            }

            if (idx >= 0)
            {
                pPathInfoArray[i] = AllCfg.pPathInfoArray[idx];

                if (pPathInfoArray[i].flags & DISPLAYCONFIG_PATH_ACTIVE)
                {
                    WARN(("VBoxTray:(WDDM) disabled path %d %d is marked active\n",
                            pPathInfoArray[i].sourceInfo.id, pPathInfoArray[i].targetInfo.id));
                    pPathInfoArray[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
                }

                Assert(pPathInfoArray[i].sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);
                Assert(pPathInfoArray[i].sourceInfo.statusFlags == 0);

                Assert(pPathInfoArray[i].targetInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);
                Assert(pPathInfoArray[i].targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15);
                Assert(pPathInfoArray[i].targetInfo.rotation == DISPLAYCONFIG_ROTATION_IDENTITY);
                Assert(pPathInfoArray[i].targetInfo.scaling == DISPLAYCONFIG_SCALING_PREFERRED);
                Assert(pPathInfoArray[i].targetInfo.refreshRate.Numerator == 0);
                Assert(pPathInfoArray[i].targetInfo.refreshRate.Denominator == 0);
                Assert(pPathInfoArray[i].targetInfo.scanLineOrdering == DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED);
                Assert(pPathInfoArray[i].targetInfo.targetAvailable == TRUE);
                Assert(pPathInfoArray[i].targetInfo.statusFlags == DISPLAYCONFIG_TARGET_FORCIBLE);

                Assert(pPathInfoArray[i].flags == 0);
            }
            else
            {
                pPathInfoArray[i].sourceInfo.adapterId = pCfg->pPathInfoArray[0].sourceInfo.adapterId;
                pPathInfoArray[i].sourceInfo.id = i;
                pPathInfoArray[i].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
                pPathInfoArray[i].sourceInfo.statusFlags = 0;

                pPathInfoArray[i].targetInfo.adapterId = pPathInfoArray[i].sourceInfo.adapterId;
                pPathInfoArray[i].targetInfo.id = i;
                pPathInfoArray[i].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
                pPathInfoArray[i].targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15;
                pPathInfoArray[i].targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
                pPathInfoArray[i].targetInfo.scaling = DISPLAYCONFIG_SCALING_PREFERRED;
                pPathInfoArray[i].targetInfo.refreshRate.Numerator = 0;
                pPathInfoArray[i].targetInfo.refreshRate.Denominator = 0;
                pPathInfoArray[i].targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
                pPathInfoArray[i].targetInfo.targetAvailable = TRUE;
                pPathInfoArray[i].targetInfo.statusFlags = DISPLAYCONFIG_TARGET_FORCIBLE;

                pPathInfoArray[i].flags = 0;
            }
        }
    }

    RTMemFree(pCfg->pPathInfoArray);
    pCfg->pPathInfoArray = pPathInfoArray;
    pCfg->cPathInfoArray = cDisplays;
    if (fAllCfgInited)
        vboxDispIfWddmDcTerm(&AllCfg);

    return ERROR_SUCCESS;
}

#ifdef VBOX_WITH_WDDM
static DWORD vboxDispIfOpBeginWddm(PCVBOXDISPIF pIf, VBOXDISPIF_OP *pOp)
{
    pOp->pIf = pIf;

    HRESULT hr = VBoxWddmKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &pOp->u.wddm.Adapter);
    if (SUCCEEDED(hr))
    {
        hr = vboxDispKmtCreateDevice(&pOp->u.wddm.Adapter, &pOp->u.wddm.Device);
        if (SUCCEEDED(hr))
        {
            hr = vboxDispKmtCreateContext(&pOp->u.wddm.Device, &pOp->u.wddm.Context, VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE,
                    NULL, 0ULL);
            if (SUCCEEDED(hr))
                return ERROR_SUCCESS;
            else
                WARN(("VBoxTray: vboxDispKmtCreateContext failed hr 0x%x", hr));

            vboxDispKmtDestroyDevice(&pOp->u.wddm.Device);
        }
        else
            WARN(("VBoxTray: vboxDispKmtCreateDevice failed hr 0x%x", hr));

        VBoxWddmKmtCloseAdapter(&pOp->u.wddm.Adapter);
    }

    return ERROR_NOT_SUPPORTED;
}

static VOID vboxDispIfOpEndWddm(VBOXDISPIF_OP *pOp)
{
    vboxDispKmtDestroyContext(&pOp->u.wddm.Context);
    vboxDispKmtDestroyDevice(&pOp->u.wddm.Device);
    VBoxWddmKmtCloseAdapter(&pOp->u.wddm.Adapter);
}
#endif /* VBOX_WITH_WDDM */

/* display driver interface abstraction for XPDM & WDDM
 * with WDDM we can not use ExtEscape to communicate with our driver
 * because we do not have XPDM display driver any more, i.e. escape requests are handled by cdd
 * that knows nothing about us */
DWORD VBoxDispIfInit(PVBOXDISPIF pDispIf)
{
    /* Note: NT4 is handled implicitly by VBoxDispIfSwitchMode(). */
    VBoxDispIfSwitchMode(pDispIf, VBOXDISPIF_MODE_XPDM, NULL);

    return NO_ERROR;
}

#ifdef VBOX_WITH_WDDM
static void vboxDispIfWddmTerm(PCVBOXDISPIF pIf);
static DWORD vboxDispIfWddmInit(PCVBOXDISPIF pIf);
#endif

DWORD VBoxDispIfTerm(PVBOXDISPIF pIf)
{
#ifdef VBOX_WITH_WDDM
    if (pIf->enmMode >= VBOXDISPIF_MODE_WDDM)
    {
        vboxDispIfWddmTerm(pIf);

        vboxDispKmtCallbacksTerm(&pIf->modeData.wddm.KmtCallbacks);
    }
#endif

    pIf->enmMode = VBOXDISPIF_MODE_UNKNOWN;
    return NO_ERROR;
}

static DWORD vboxDispIfEscapeXPDM(PCVBOXDISPIF pIf, PVBOXDISPIFESCAPE pEscape, int cbData, int iDirection)
{
    RT_NOREF(pIf);
    HDC  hdc = GetDC(HWND_DESKTOP);
    VOID *pvData = cbData ? VBOXDISPIFESCAPE_DATA(pEscape, VOID) : NULL;
    int iRet = ExtEscape(hdc, pEscape->escapeCode,
            iDirection >= 0 ? cbData : 0,
            iDirection >= 0 ? (LPSTR)pvData : NULL,
            iDirection <= 0 ? cbData : 0,
            iDirection <= 0 ? (LPSTR)pvData : NULL);
    ReleaseDC(HWND_DESKTOP, hdc);
    if (iRet > 0)
        return VINF_SUCCESS;
    if (iRet == 0)
        return ERROR_NOT_SUPPORTED;
    /* else */
    return ERROR_GEN_FAILURE;
}

#ifdef VBOX_WITH_WDDM
static DWORD vboxDispIfSwitchToWDDM(PVBOXDISPIF pIf)
{
    DWORD err = NO_ERROR;

    bool fSupported = true;

    uint64_t const uNtVersion = RTSystemGetNtVersion();
    if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
    {
        LogFunc(("this is vista and up\n"));
        HMODULE hUser = GetModuleHandle("user32.dll");
        if (hUser)
        {
            *(uintptr_t *)&pIf->modeData.wddm.pfnChangeDisplaySettingsEx = (uintptr_t)GetProcAddress(hUser, "ChangeDisplaySettingsExA");
            LogFunc(("VBoxDisplayInit: pfnChangeDisplaySettingsEx = %p\n", pIf->modeData.wddm.pfnChangeDisplaySettingsEx));
            fSupported &= !!(pIf->modeData.wddm.pfnChangeDisplaySettingsEx);

            *(uintptr_t *)&pIf->modeData.wddm.pfnEnumDisplayDevices = (uintptr_t)GetProcAddress(hUser, "EnumDisplayDevicesA");
            LogFunc(("VBoxDisplayInit: pfnEnumDisplayDevices = %p\n", pIf->modeData.wddm.pfnEnumDisplayDevices));
            fSupported &= !!(pIf->modeData.wddm.pfnEnumDisplayDevices);

            /* for win 7 and above */
            if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 1, 0))
            {
                *(uintptr_t *)&gCtx.pfnSetDisplayConfig = (uintptr_t)GetProcAddress(hUser, "SetDisplayConfig");
                LogFunc(("VBoxDisplayInit: pfnSetDisplayConfig = %p\n", gCtx.pfnSetDisplayConfig));
                fSupported &= !!(gCtx.pfnSetDisplayConfig);

                *(uintptr_t *)&gCtx.pfnQueryDisplayConfig = (uintptr_t)GetProcAddress(hUser, "QueryDisplayConfig");
                LogFunc(("VBoxDisplayInit: pfnQueryDisplayConfig = %p\n", gCtx.pfnQueryDisplayConfig));
                fSupported &= !!(gCtx.pfnQueryDisplayConfig);

                *(uintptr_t *)&gCtx.pfnGetDisplayConfigBufferSizes = (uintptr_t)GetProcAddress(hUser, "GetDisplayConfigBufferSizes");
                LogFunc(("VBoxDisplayInit: pfnGetDisplayConfigBufferSizes = %p\n", gCtx.pfnGetDisplayConfigBufferSizes));
                fSupported &= !!(gCtx.pfnGetDisplayConfigBufferSizes);
            }

            /* this is vista and up */
            HRESULT hr = vboxDispKmtCallbacksInit(&pIf->modeData.wddm.KmtCallbacks);
            if (FAILED(hr))
            {
                WARN(("VBoxTray: vboxDispKmtCallbacksInit failed hr 0x%x\n", hr));
                err = hr;
            }
        }
        else
        {
            WARN(("GetModuleHandle(USER32) failed, err(%d)\n", GetLastError()));
            err = ERROR_NOT_SUPPORTED;
        }
    }
    else
    {
        WARN(("can not switch to VBOXDISPIF_MODE_WDDM, because os is not Vista or upper\n"));
        err = ERROR_NOT_SUPPORTED;
    }

    if (err == ERROR_SUCCESS)
    {
        err = vboxDispIfWddmInit(pIf);
    }

    return err;
}

static DWORD vboxDispIfSwitchToWDDM_W7(PVBOXDISPIF pIf)
{
    return vboxDispIfSwitchToWDDM(pIf);
}

static DWORD vboxDispIfWDDMAdpHdcCreate(int iDisplay, HDC *phDc, DISPLAY_DEVICE *pDev)
{
    DWORD winEr = ERROR_INVALID_STATE;
    memset(pDev, 0, sizeof (*pDev));
    pDev->cb = sizeof (*pDev);

    for (int i = 0; ; ++i)
    {
        if (EnumDisplayDevices(NULL, /* LPCTSTR lpDevice */ i, /* DWORD iDevNum */
                pDev, 0 /* DWORD dwFlags*/))
        {
            if (i == iDisplay || (iDisplay < 0 && pDev->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            {
                HDC hDc = CreateDC(NULL, pDev->DeviceName, NULL, NULL);
                if (hDc)
                {
                    *phDc = hDc;
                    return NO_ERROR;
                }
                else
                {
                    winEr = GetLastError();
                    WARN(("CreateDC failed %d", winEr));
                    break;
                }
            }
            Log(("display data no match display(%d): i(%d), flags(%d)", iDisplay, i, pDev->StateFlags));
        }
        else
        {
            winEr = GetLastError();
            WARN(("EnumDisplayDevices failed %d", winEr));
            break;
        }
    }

    WARN(("vboxDispIfWDDMAdpHdcCreate failure branch %d", winEr));
    return winEr;
}

static DWORD vboxDispIfEscapeWDDM(PCVBOXDISPIF pIf, PVBOXDISPIFESCAPE pEscape, int cbData, BOOL fHwAccess)
{
    DWORD winEr = ERROR_SUCCESS;
    VBOXDISPKMT_ADAPTER Adapter;
    HRESULT hr = VBoxWddmKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &Adapter);
    if (!SUCCEEDED(hr))
    {
        WARN(("VBoxTray: vboxDispKmtOpenAdapter failed hr 0x%x\n", hr));
        return hr;
    }

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = Adapter.hAdapter;
    //EscapeData.hDevice = NULL;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    if (fHwAccess)
        EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = pEscape;
    EscapeData.PrivateDriverDataSize = VBOXDISPIFESCAPE_SIZE(cbData);
    //EscapeData.hContext = NULL;

    NTSTATUS Status = pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        winEr = ERROR_SUCCESS;
    else
    {
        WARN(("VBoxTray: pfnD3DKMTEscape(0x%08X) failed Status 0x%x\n", pEscape->escapeCode, Status));
        winEr = ERROR_GEN_FAILURE;
    }

    VBoxWddmKmtCloseAdapter(&Adapter);

    return winEr;
}
#endif

DWORD VBoxDispIfEscape(PCVBOXDISPIF pIf, PVBOXDISPIFESCAPE pEscape, int cbData)
{
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
        case VBOXDISPIF_MODE_XPDM:
            return vboxDispIfEscapeXPDM(pIf, pEscape, cbData, 1);
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfEscapeWDDM(pIf, pEscape, cbData, TRUE /* BOOL fHwAccess */);
#endif
        default:
            LogFunc(("unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

DWORD VBoxDispIfEscapeInOut(PCVBOXDISPIF const pIf, PVBOXDISPIFESCAPE pEscape, int cbData)
{
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
        case VBOXDISPIF_MODE_XPDM:
            return vboxDispIfEscapeXPDM(pIf, pEscape, cbData, 0);
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfEscapeWDDM(pIf, pEscape, cbData, TRUE /* BOOL fHwAccess */);
#endif
        default:
            LogFunc(("unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

#ifdef VBOX_WITH_WDDM

#define VBOXRR_TIMER_ID 1234

typedef struct VBOXRR
{
    HANDLE hThread;
    DWORD idThread;
    HANDLE hEvent;
    HWND hWnd;
    CRITICAL_SECTION CritSect;
    UINT_PTR idTimer;
    PCVBOXDISPIF pIf;
    UINT iChangedMode;
    BOOL fEnable;
    BOOL fExtDispSup;
    DISPLAY_DEVICE *paDisplayDevices;
    DEVMODE *paDeviceModes;
    UINT cDevModes;
} VBOXRR, *PVBOXRR;

static VBOXRR g_VBoxRr = {0};

#define VBOX_E_INSUFFICIENT_BUFFER HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
#define VBOX_E_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)

static void vboxRrRetryStopLocked()
{
    PVBOXRR pMon = &g_VBoxRr;
    if (pMon->pIf)
    {
        if (pMon->paDisplayDevices)
        {
            RTMemFree(pMon->paDisplayDevices);
            pMon->paDisplayDevices = NULL;
        }

        if (pMon->paDeviceModes)
        {
            RTMemFree(pMon->paDeviceModes);
            pMon->paDeviceModes = NULL;
        }

        if (pMon->idTimer)
        {
            KillTimer(pMon->hWnd, pMon->idTimer);
            pMon->idTimer = 0;
        }

        pMon->cDevModes = 0;
        pMon->pIf = NULL;
    }
}

static void VBoxRrRetryStop()
{
    PVBOXRR pMon = &g_VBoxRr;
    EnterCriticalSection(&pMon->CritSect);
    vboxRrRetryStopLocked();
    LeaveCriticalSection(&pMon->CritSect);
}

//static DWORD vboxDispIfWddmValidateFixResize(PCVBOXDISPIF const pIf, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes);

static void vboxRrRetryReschedule()
{
}

static void VBoxRrRetrySchedule(PCVBOXDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    PVBOXRR pMon = &g_VBoxRr;
    EnterCriticalSection(&pMon->CritSect);
    vboxRrRetryStopLocked();

    pMon->pIf = pIf;
    pMon->iChangedMode = iChangedMode;
    pMon->fEnable = fEnable;
    pMon->fExtDispSup = fExtDispSup;

    if (cDevModes)
    {
        pMon->paDisplayDevices = (DISPLAY_DEVICE*)RTMemAlloc(sizeof (*paDisplayDevices) * cDevModes);
        Assert(pMon->paDisplayDevices);
        if (!pMon->paDisplayDevices)
        {
            Log(("RTMemAlloc failed!"));
            vboxRrRetryStopLocked();
            LeaveCriticalSection(&pMon->CritSect);
            return;
        }
        memcpy(pMon->paDisplayDevices, paDisplayDevices, sizeof (*paDisplayDevices) * cDevModes);

        pMon->paDeviceModes = (DEVMODE*)RTMemAlloc(sizeof (*paDeviceModes) * cDevModes);
        Assert(pMon->paDeviceModes);
        if (!pMon->paDeviceModes)
        {
            Log(("RTMemAlloc failed!"));
            vboxRrRetryStopLocked();
            LeaveCriticalSection(&pMon->CritSect);
            return;
        }
        memcpy(pMon->paDeviceModes, paDeviceModes, sizeof (*paDeviceModes) * cDevModes);
    }
    pMon->cDevModes = cDevModes;

    pMon->idTimer = SetTimer(pMon->hWnd, VBOXRR_TIMER_ID, 1000, (TIMERPROC)NULL);
    Assert(pMon->idTimer);
    if (!pMon->idTimer)
    {
        WARN(("VBoxTray: SetTimer failed!, err %d\n", GetLastError()));
        vboxRrRetryStopLocked();
    }

    LeaveCriticalSection(&pMon->CritSect);
}

static void vboxRrRetryPerform()
{
    PVBOXRR pMon = &g_VBoxRr;
    EnterCriticalSection(&pMon->CritSect);
    if (pMon->pIf)
    {
        DWORD dwErr = vboxDispIfResizePerform(pMon->pIf, pMon->iChangedMode, pMon->fEnable, pMon->fExtDispSup, pMon->paDisplayDevices, pMon->paDeviceModes, pMon->cDevModes);
        if (ERROR_RETRY != dwErr)
            VBoxRrRetryStop();
        else
            vboxRrRetryReschedule();
    }
    LeaveCriticalSection(&pMon->CritSect);
}

static LRESULT CALLBACK vboxRrWndProc(HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch(uMsg)
    {
        case WM_DISPLAYCHANGE:
        {
            Log(("VBoxTray: WM_DISPLAYCHANGE\n"));
            VBoxRrRetryStop();
            return 0;
        }
        case WM_TIMER:
        {
            if (wParam == VBOXRR_TIMER_ID)
            {
                Log(("VBoxTray: VBOXRR_TIMER_ID\n"));
                vboxRrRetryPerform();
                return 0;
            }
            break;
        }
        case WM_NCHITTEST:
            LogFunc(("got WM_NCHITTEST for hwnd(0x%x)\n", hwnd));
            return HTNOWHERE;
        default:
            break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define VBOXRRWND_NAME "VBoxRrWnd"

static HRESULT vboxRrWndCreate(HWND *phWnd)
{
    HRESULT hr = S_OK;

    /** @todo r=andy Use VBOXSERVICEENV::hInstance. */
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);

    /* Register the Window Class. */
    WNDCLASSEX wc = { 0 };
    wc.cbSize     = sizeof(WNDCLASSEX);

    if (!GetClassInfoEx(hInstance, VBOXRRWND_NAME, &wc))
    {
        wc.lpfnWndProc   = vboxRrWndProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = VBOXRRWND_NAME;

        if (!RegisterClassEx(&wc))
        {
            WARN(("RegisterClass failed, winErr(%d)\n", GetLastError()));
            hr = E_FAIL;
        }
    }

    if (hr == S_OK)
    {
        HWND hWnd = CreateWindowEx (WS_EX_TOOLWINDOW,
                                        VBOXRRWND_NAME, VBOXRRWND_NAME,
                                        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED,
                                        -100, -100,
                                        10, 10,
                                        NULL, //GetDesktopWindow() /* hWndParent */,
                                        NULL /* hMenu */,
                                        hInstance,
                                        NULL /* lpParam */);
        Assert(hWnd);
        if (hWnd)
        {
            *phWnd = hWnd;
        }
        else
        {
            WARN(("CreateWindowEx failed, winErr(%d)\n", GetLastError()));
            hr = E_FAIL;
        }
    }

    return hr;
}

static HRESULT vboxRrWndDestroy(HWND hWnd)
{
    BOOL bResult = DestroyWindow(hWnd);
    if (bResult)
        return S_OK;

    DWORD winErr = GetLastError();
    WARN(("DestroyWindow failed, winErr(%d) for hWnd(0x%x)\n", winErr, hWnd));

    return HRESULT_FROM_WIN32(winErr);
}

static HRESULT vboxRrWndInit()
{
    PVBOXRR pMon = &g_VBoxRr;
    return vboxRrWndCreate(&pMon->hWnd);
}

HRESULT vboxRrWndTerm()
{
    PVBOXRR pMon = &g_VBoxRr;
    HRESULT hrTmp = vboxRrWndDestroy(pMon->hWnd);
    Assert(hrTmp == S_OK); NOREF(hrTmp);

    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    UnregisterClass(VBOXRRWND_NAME, hInstance);

    return S_OK;
}

#define WM_VBOXRR_INIT_QUIT (WM_APP+2)

HRESULT vboxRrRun()
{
    PVBOXRR pMon = &g_VBoxRr;
    MSG Msg;

    HRESULT hr = S_FALSE;

    /* Create the thread message queue*/
    PeekMessage(&Msg,
            NULL /* HWND hWnd */,
            WM_USER /* UINT wMsgFilterMin */,
            WM_USER /* UINT wMsgFilterMax */,
            PM_NOREMOVE);

    /*
    * Send signal that message queue is ready.
    * From this moment only the thread is ready to receive messages.
    */
    BOOL bRc = SetEvent(pMon->hEvent);
    if (!bRc)
    {
        DWORD winErr = GetLastError();
        WARN(("SetEvent failed, winErr = (%d)", winErr));
        HRESULT hrTmp = HRESULT_FROM_WIN32(winErr);
        Assert(hrTmp != S_OK); NOREF(hrTmp);
    }

    do
    {
        BOOL bResult = GetMessage(&Msg,
            0 /*HWND hWnd*/,
            0 /*UINT wMsgFilterMin*/,
            0 /*UINT wMsgFilterMax*/
            );

        if (bResult == -1) /* error occurred */
        {
            DWORD winEr = GetLastError();
            hr = HRESULT_FROM_WIN32(winEr);
            /* just ensure we never return success in this case */
            Assert(hr != S_OK);
            Assert(hr != S_FALSE);
            if (hr == S_OK || hr == S_FALSE)
                hr = E_FAIL;
            WARN(("VBoxTray: GetMessage returned -1, err %d\n", winEr));
            VBoxRrRetryStop();
            break;
        }

        if(!bResult) /* WM_QUIT was posted */
        {
            hr = S_FALSE;
            Log(("VBoxTray: GetMessage returned FALSE\n"));
            VBoxRrRetryStop();
            break;
        }

        switch (Msg.message)
        {
            case WM_VBOXRR_INIT_QUIT:
            case WM_CLOSE:
            {
                Log(("VBoxTray: closing Rr %d\n", Msg.message));
                VBoxRrRetryStop();
                PostQuitMessage(0);
                break;
            }
            default:
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
                break;
        }
    } while (1);
    return 0;
}

/** @todo r=bird: Only the CRT uses CreateThread for creating threading!! */
static DWORD WINAPI vboxRrRunnerThread(void *pvUser) RT_NOTHROW_DEF
{
    RT_NOREF(pvUser);
    HRESULT hr = vboxRrWndInit();
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        hr = vboxRrRun();
        Assert(hr == S_OK);

        vboxRrWndTerm();
    }

    return 0;
}

HRESULT VBoxRrInit()
{
    HRESULT hr = E_FAIL;
    PVBOXRR pMon = &g_VBoxRr;
    memset(pMon, 0, sizeof (*pMon));

    InitializeCriticalSection(&pMon->CritSect);

    pMon->hEvent = CreateEvent(NULL, /* LPSECURITY_ATTRIBUTES lpEventAttributes*/
            TRUE, /* BOOL bManualReset*/
            FALSE, /* BOOL bInitialState */
            NULL /* LPCTSTR lpName */
          );
    if (pMon->hEvent)
    {
        /** @todo r=bird: What kind of stupid nonsense is this?!?
         *  Only the CRT uses CreateThread for creating threading!!
         */
        pMon->hThread = CreateThread(NULL /* LPSECURITY_ATTRIBUTES lpThreadAttributes */,
                                              0 /* SIZE_T dwStackSize */,
                                     vboxRrRunnerThread,
                                     pMon,
                                     0 /* DWORD dwCreationFlags */,
                                     &pMon->idThread);
        if (pMon->hThread)
        {
            DWORD dwResult = WaitForSingleObject(pMon->hEvent, INFINITE);
            if (dwResult == WAIT_OBJECT_0)
                return S_OK;
            Log(("WaitForSingleObject failed!"));
            hr = E_FAIL;
        }
        else
        {
            DWORD winErr = GetLastError();
            WARN(("CreateThread failed, winErr = (%d)", winErr));
            hr = HRESULT_FROM_WIN32(winErr);
            Assert(hr != S_OK);
        }
        CloseHandle(pMon->hEvent);
    }
    else
    {
        DWORD winErr = GetLastError();
        WARN(("CreateEvent failed, winErr = (%d)", winErr));
        hr = HRESULT_FROM_WIN32(winErr);
        Assert(hr != S_OK);
    }

    DeleteCriticalSection(&pMon->CritSect);

    return hr;
}

VOID VBoxRrTerm()
{
    HRESULT hr;
    PVBOXRR pMon = &g_VBoxRr;
    if (!pMon->hThread)
        return;

    BOOL bResult = PostThreadMessage(pMon->idThread, WM_VBOXRR_INIT_QUIT, 0, 0);
    DWORD winErr;
    if (bResult
            || (winErr = GetLastError()) == ERROR_INVALID_THREAD_ID) /* <- could be that the thread is terminated */
    {
        DWORD dwErr = WaitForSingleObject(pMon->hThread, INFINITE);
        if (dwErr == WAIT_OBJECT_0)
        {
            hr = S_OK;
        }
        else
        {
            winErr = GetLastError();
            hr = HRESULT_FROM_WIN32(winErr);
        }
    }
    else
    {
        hr = HRESULT_FROM_WIN32(winErr);
    }

    DeleteCriticalSection(&pMon->CritSect);

    CloseHandle(pMon->hThread);
    pMon->hThread = 0;
    CloseHandle(pMon->hEvent);
    pMon->hThread = 0;
}

static DWORD vboxDispIfWddmInit(PCVBOXDISPIF pIf)
{
    RT_NOREF(pIf);
    HRESULT hr = VBoxRrInit();
    if (SUCCEEDED(hr))
        return ERROR_SUCCESS;
    WARN(("VBoxTray: VBoxRrInit failed hr 0x%x\n", hr));
    return hr;
}

static void vboxDispIfWddmTerm(PCVBOXDISPIF pIf)
{
    RT_NOREF(pIf);
    VBoxRrTerm();
}

static DWORD vboxDispIfQueryDisplayConnection(VBOXDISPIF_OP *pOp, UINT32 iDisplay, BOOL *pfConnected)
{
    if (pOp->pIf->enmMode == VBOXDISPIF_MODE_WDDM)
    {
        /** @todo do we need ti impl it? */
        *pfConnected = TRUE;
        return ERROR_SUCCESS;
    }

    *pfConnected = FALSE;

    VBOXDISPIF_WDDM_DISPCFG DispCfg;
    DWORD winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcCreate winEr %d\n", winEr));
        return winEr;
    }

    int idx = vboxDispIfWddmDcSearchPath(&DispCfg, iDisplay, iDisplay);
    *pfConnected = (idx >= 0);

    vboxDispIfWddmDcTerm(&DispCfg);

    return ERROR_SUCCESS;
}

static DWORD vboxDispIfWaitDisplayDataInited(VBOXDISPIF_OP *pOp)
{
    DWORD winEr = ERROR_SUCCESS;
    do
    {
        Sleep(100);

        D3DKMT_POLLDISPLAYCHILDREN PollData = {0};
        PollData.hAdapter = pOp->u.wddm.Adapter.hAdapter;
        PollData.NonDestructiveOnly = 1;
        NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTPollDisplayChildren(&PollData);
        if (Status != 0)
        {
            Log(("VBoxTray: (WDDM) pfnD3DKMTPollDisplayChildren failed, Status (0x%x)\n", Status));
            continue;
        }

        BOOL fFound = FALSE;
#if 0
        for (UINT i = 0; i < VBOXWDDM_SCREENMASK_SIZE; ++i)
        {
            if (pu8DisplayMask && !ASMBitTest(pu8DisplayMask, i))
                continue;

            BOOL fConnected = FALSE;
            winEr = vboxDispIfQueryDisplayConnection(pOp, i, &fConnected);
            if (winEr != ERROR_SUCCESS)
            {
                WARN(("VBoxTray: (WDDM) Failed vboxDispIfQueryDisplayConnection winEr %d\n", winEr));
                return winEr;
            }

            if (!fConnected)
            {
                WARN(("VBoxTray: (WDDM) Display %d not connected, not expected\n", i));
                fFound = TRUE;
                break;
            }
        }
#endif
        if (!fFound)
            break;
    } while (1);

    return winEr;
}

static DWORD vboxDispIfUpdateModesWDDM(VBOXDISPIF_OP *pOp, uint32_t u32TargetId, const RTRECTSIZE *pSize)
{
    DWORD winEr = ERROR_SUCCESS;
    VBOXDISPIFESCAPE_UPDATEMODES EscData = {{0}};
    EscData.EscapeHdr.escapeCode = VBOXESC_UPDATEMODES;
    EscData.u32TargetId = u32TargetId;
    EscData.Size = *pSize;

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pOp->u.wddm.Adapter.hAdapter;
#ifdef VBOX_DISPIF_WITH_OPCONTEXT
    /* win8.1 does not allow context-based escapes for display-only mode */
    EscapeData.hDevice = pOp->Device.hDevice;
    EscapeData.hContext = pOp->Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = &EscData;
    EscapeData.PrivateDriverDataSize = sizeof (EscData);

    NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        winEr = ERROR_SUCCESS;
    else
    {
        WARN(("VBoxTray: pfnD3DKMTEscape VBOXESC_UPDATEMODES failed Status 0x%x\n", Status));
        winEr = ERROR_GEN_FAILURE;
    }

#ifdef VBOX_WDDM_REPLUG_ON_MODE_CHANGE
    /* The code was disabled because VBOXESC_UPDATEMODES should not cause (un)plugging virtual displays. */
    winEr =  vboxDispIfWaitDisplayDataInited(pOp);
    if (winEr != NO_ERROR)
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWaitDisplayDataInited winEr %d\n", winEr));
#endif

    return winEr;
}

static DWORD vboxDispIfTargetConnectivityWDDM(VBOXDISPIF_OP *pOp, uint32_t u32TargetId, uint32_t fu32Connect)
{
    VBOXDISPIFESCAPE_TARGETCONNECTIVITY PrivateData;
    RT_ZERO(PrivateData);
    PrivateData.EscapeHdr.escapeCode = VBOXESC_TARGET_CONNECTIVITY;
    PrivateData.u32TargetId = u32TargetId;
    PrivateData.fu32Connect = fu32Connect;

    D3DKMT_ESCAPE EscapeData;
    RT_ZERO(EscapeData);
    EscapeData.hAdapter = pOp->u.wddm.Adapter.hAdapter;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = &PrivateData;
    EscapeData.PrivateDriverDataSize = sizeof(PrivateData);

    NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        return ERROR_SUCCESS;

    WARN(("VBoxTray: pfnD3DKMTEscape VBOXESC_TARGETCONNECTIVITY failed Status 0x%x\n", Status));
    return ERROR_GEN_FAILURE;
}

DWORD vboxDispIfCancelPendingResizeWDDM(PCVBOXDISPIF const pIf)
{
    RT_NOREF(pIf);
    Log(("VBoxTray: cancelling pending resize\n"));
    VBoxRrRetryStop();
    return NO_ERROR;
}

static DWORD vboxDispIfWddmResizeDisplayVista(DEVMODE *paDeviceModes, DISPLAY_DEVICE *paDisplayDevices, DWORD cDevModes, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup)
{
    /* Without this, Windows will not ask the miniport for its
     * mode table but uses an internal cache instead.
     */
    for (DWORD i = 0; i < cDevModes; i++)
    {
        DEVMODE tempDevMode;
        ZeroMemory (&tempDevMode, sizeof (tempDevMode));
        tempDevMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings((LPSTR)paDisplayDevices[i].DeviceName, 0xffffff, &tempDevMode);
        Log(("VBoxTray: ResizeDisplayDevice: EnumDisplaySettings last error %d\n", GetLastError ()));
    }

    DWORD winEr = EnableAndResizeDispDev(paDeviceModes, paDisplayDevices, cDevModes, iChangedMode, paDeviceModes[iChangedMode].dmPelsWidth, paDeviceModes[iChangedMode].dmPelsHeight,
            paDeviceModes[iChangedMode].dmBitsPerPel, paDeviceModes[iChangedMode].dmPosition.x, paDeviceModes[iChangedMode].dmPosition.y, fEnable, fExtDispSup);
    if (winEr != NO_ERROR)
        WARN(("VBoxTray: (WDDM) Failed EnableAndResizeDispDev winEr %d\n", winEr));

    return winEr;
}

static DWORD vboxDispIfResizePerform(PCVBOXDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    LogFunc((" ENTER"));
    DWORD winEr;

    if (pIf->enmMode > VBOXDISPIF_MODE_WDDM)
    {
        if (fEnable)
            paDisplayDevices[iChangedMode].StateFlags |= DISPLAY_DEVICE_ACTIVE;
        else
            paDisplayDevices[iChangedMode].StateFlags &= ~DISPLAY_DEVICE_ACTIVE;

        winEr = vboxDispIfWddmResizeDisplay2(pIf, paDisplayDevices, paDeviceModes, cDevModes);

        if (winEr != NO_ERROR)
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmResizeDisplay winEr %d\n", winEr));
    }
    else
    {
        winEr = vboxDispIfWddmResizeDisplayVista(paDeviceModes, paDisplayDevices, cDevModes, iChangedMode, fEnable, fExtDispSup);
        if (winEr != NO_ERROR)
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmResizeDisplayVista winEr %d\n", winEr));
    }

    LogFunc((" LEAVE"));
    return winEr;
}

DWORD vboxDispIfResizeModesWDDM(PCVBOXDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    DWORD winEr = NO_ERROR;

    Log(("VBoxTray: vboxDispIfResizeModesWDDM iChanged %d cDevModes %d fEnable %d fExtDispSup %d\n", iChangedMode, cDevModes, fEnable, fExtDispSup));
    VBoxRrRetryStop();

    VBOXDISPIF_OP Op;

    winEr = vboxDispIfOpBeginWddm(pIf, &Op);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfOpBegin failed winEr 0x%x", winEr));
        return winEr;
    }

/*  The pfnD3DKMTInvalidateActiveVidPn was deprecated since Win7 and causes deadlocks since Win10 TH2.
    Instead, the VidPn Manager can replace an old VidPn as soon as SetDisplayConfig or ChangeDisplaySettingsEx will try to set a new display mode.
    On Vista D3DKMTInvalidateActiveVidPn is still required. TBD: Get rid of it. */
    if (Op.pIf->enmMode < VBOXDISPIF_MODE_WDDM_W7)
    {
        D3DKMT_INVALIDATEACTIVEVIDPN ddiArgInvalidateVidPN;
        VBOXWDDM_RECOMMENDVIDPN vboxRecommendVidPN;

        memset(&ddiArgInvalidateVidPN, 0, sizeof(ddiArgInvalidateVidPN));
        memset(&vboxRecommendVidPN, 0, sizeof(vboxRecommendVidPN));

        uint32_t cElements = 0;

        for (uint32_t i = 0; i < cDevModes; ++i)
        {
            if ((i == iChangedMode) ? fEnable : (paDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE))
            {
                vboxRecommendVidPN.aSources[cElements].Size.cx = paDeviceModes[i].dmPelsWidth;
                vboxRecommendVidPN.aSources[cElements].Size.cy = paDeviceModes[i].dmPelsHeight;
                vboxRecommendVidPN.aTargets[cElements].iSource = cElements;
                ++cElements;
            }
            else
                vboxRecommendVidPN.aTargets[cElements].iSource = -1;
        }

        ddiArgInvalidateVidPN.hAdapter = Op.u.wddm.Adapter.hAdapter;
        ddiArgInvalidateVidPN.pPrivateDriverData = &vboxRecommendVidPN;
        ddiArgInvalidateVidPN.PrivateDriverDataSize = sizeof (vboxRecommendVidPN);

        NTSTATUS Status;
        Status = Op.pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTInvalidateActiveVidPn(&ddiArgInvalidateVidPN);
        LogFunc(("D3DKMTInvalidateActiveVidPn returned %d)\n", Status));
    }

    vboxDispIfTargetConnectivityWDDM(&Op, iChangedMode, fEnable? 1: 0);

    /* Whether the current display is already or should be enabled. */
    BOOL fChangedEnable = fEnable || RT_BOOL(paDisplayDevices[iChangedMode].StateFlags & DISPLAY_DEVICE_ACTIVE);

    if (fChangedEnable)
    {
        RTRECTSIZE Size;

        Size.cx = paDeviceModes[iChangedMode].dmPelsWidth;
        Size.cy = paDeviceModes[iChangedMode].dmPelsHeight;

        LogFunc(("Calling vboxDispIfUpdateModesWDDM to change target %d mode to (%d x %d)\n", iChangedMode, Size.cx, Size.cy));
        winEr = vboxDispIfUpdateModesWDDM(&Op, iChangedMode, &Size);
    }

    winEr = vboxDispIfResizePerform(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);

    if (winEr == ERROR_RETRY)
    {
        VBoxRrRetrySchedule(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);

        winEr = NO_ERROR;
    }

    vboxDispIfOpEndWddm(&Op);

    return winEr;
}

static DWORD vboxDispIfWddmEnableDisplays(PCVBOXDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnabled, BOOL fSetTopology, DEVMODE *pDeviceMode)
{
    RT_NOREF(pIf);
    VBOXDISPIF_WDDM_DISPCFG DispCfg;

    DWORD winEr;
    int iPath;
    UINT fSetFlags = 0; /* /permissive- hack */

    winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcCreate winEr %d\n", winEr));
        return winEr;
    }

    UINT cChangeIds = 0;
    UINT *pChangeIds = (UINT*)alloca(cIds * sizeof (*pChangeIds));
    if (!pChangeIds)
    {
        WARN(("VBoxTray: (WDDM) Failed to alloc change ids\n"));
        winEr = ERROR_OUTOFMEMORY;
        goto done;
    }

    for (UINT i = 0; i < cIds; ++i)
    {
        UINT Id = pIds[i];
        bool fIsDup = false;
        for (UINT j = 0; j < cChangeIds; ++j)
        {
            if (pChangeIds[j] == Id)
            {
                fIsDup = true;
                break;
            }
        }

        if (fIsDup)
            continue;

        iPath = vboxDispIfWddmDcSearchPath(&DispCfg, Id, Id);

        if (!((iPath >= 0) && (DispCfg.pPathInfoArray[iPath].flags & DISPLAYCONFIG_PATH_ACTIVE)) != !fEnabled)
        {
            pChangeIds[cChangeIds] = Id;
            ++cChangeIds;
        }
    }

    if (cChangeIds == 0)
    {
        Log(("VBoxTray: (WDDM) vboxDispIfWddmEnableDisplay: settings are up to date\n"));
        winEr = ERROR_SUCCESS;
        goto done;
    }

    /* we want to set primary for every disabled for non-topoly mode only */
    winEr = vboxDispIfWddmDcSettingsIncludeAllTargets(&DispCfg);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcSettingsIncludeAllTargets winEr %d\n", winEr));
        return winEr;
    }

    if (fSetTopology)
        vboxDispIfWddmDcSettingsInvalidateModeIndeces(&DispCfg);

    for (UINT i = 0; i < cChangeIds; ++i)
    {
        UINT Id = pChangeIds[i];
        /* re-query paths */
        iPath = vboxDispIfWddmDcSearchPath(&DispCfg, VBOX_WDDM_DC_SEARCH_PATH_ANY, Id);
        if (iPath < 0)
        {
            WARN(("VBoxTray: (WDDM) path index not found while it should"));
            winEr = ERROR_GEN_FAILURE;
            goto done;
        }

        winEr = vboxDispIfWddmDcSettingsUpdate(&DispCfg, iPath, pDeviceMode, !fEnabled || fSetTopology, fEnabled);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcSettingsUpdate winEr %d\n", winEr));
            goto done;
        }
    }

    if (!fSetTopology)
        vboxDispIfWddmDcSettingsAttachDisbledToPrimary(&DispCfg);

#if 0
    /* ensure the zero-index (primary) screen is enabled */
    iPath = vboxDispIfWddmDcSearchPath(&DispCfg, 0, 0);
    if (iPath < 0)
    {
        WARN(("VBoxTray: (WDDM) path index not found while it should"));
        winEr = ERROR_GEN_FAILURE;
        goto done;
    }

    winEr = vboxDispIfWddmDcSettingsUpdate(&DispCfg, iPath, /* just re-use device node here*/ pDeviceMode, fSetTopology, TRUE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcSettingsUpdate winEr %d\n", winEr));
        goto done;
    }
#endif

    fSetFlags = !fSetTopology ? (SDC_USE_SUPPLIED_DISPLAY_CONFIG) : (SDC_ALLOW_PATH_ORDER_CHANGES | SDC_TOPOLOGY_SUPPLIED);
    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        if (!fSetTopology)
        {
            WARN(("VBoxTray: (WDDM) vboxDispIfWddmDcSet validation failed winEr, trying with changes %d\n", winEr));
            fSetFlags |= SDC_ALLOW_CHANGES;
        }
        else
        {
            Log(("VBoxTray: (WDDM) vboxDispIfWddmDcSet topology validation failed winEr %d\n", winEr));
            goto done;
        }
    }

    if (!fSetTopology)
        fSetFlags |= SDC_SAVE_TO_DATABASE;

    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
        WARN(("VBoxTray: (WDDM) vboxDispIfWddmDcSet apply failed winEr %d\n", winEr));

done:
    vboxDispIfWddmDcTerm(&DispCfg);

    return winEr;
}

static DWORD vboxDispIfWddmEnableDisplaysTryingTopology(PCVBOXDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnable)
{
    DWORD winEr = vboxDispIfWddmEnableDisplays(pIf, cIds, pIds, fEnable, FALSE, NULL);
    if (winEr != ERROR_SUCCESS)
    {
        if (fEnable)
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmEnableDisplay mode winEr %d\n", winEr));
        else
            Log(("VBoxTray: (WDDM) Failed vboxDispIfWddmEnableDisplay mode winEr %d\n", winEr));
        winEr = vboxDispIfWddmEnableDisplays(pIf, cIds, pIds, fEnable, TRUE, NULL);
        if (winEr != ERROR_SUCCESS)
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmEnableDisplay mode winEr %d\n", winEr));
    }

    return winEr;
}

BOOL VBoxDispIfResizeDisplayWin7Wddm(PCVBOXDISPIF const pIf, uint32_t cDispDef, const VMMDevDisplayDef *paDispDef)
{
    const VMMDevDisplayDef *pDispDef;
    uint32_t i;

    /* SetDisplayConfig assumes the top-left corner of a primary display at (0, 0) position */
    const VMMDevDisplayDef* pDispDefPrimary = NULL;

    for (i = 0; i < cDispDef; ++i)
    {
        pDispDef = &paDispDef[i];

        if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_PRIMARY)
        {
            pDispDefPrimary = pDispDef;
            break;
        }
    }

    VBOXDISPIF_OP Op;
    DWORD winEr = vboxDispIfOpBeginWddm(pIf, &Op);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: vboxDispIfOpBegin failed winEr 0x%x", winEr));
        return (winEr == ERROR_SUCCESS);
    }

    for (i = 0; i < cDispDef; ++i)
    {
        pDispDef = &paDispDef[i];

        if (RT_BOOL(pDispDef->fDisplayFlags & VMMDEV_DISPLAY_DISABLED))
            continue;

        if (   RT_BOOL(pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CX)
            && RT_BOOL(pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CY))
        {
            RTRECTSIZE Size;
            Size.cx = pDispDef->cx;
            Size.cy = pDispDef->cy;

            winEr = vboxDispIfUpdateModesWDDM(&Op, pDispDef->idDisplay, &Size);
            if (winEr != ERROR_SUCCESS)
                break;
        }
    }

    vboxDispIfOpEndWddm(&Op);

    if (winEr != ERROR_SUCCESS)
        return (winEr == ERROR_SUCCESS);

    VBOXDISPIF_WDDM_DISPCFG DispCfg;
    winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: vboxDispIfWddmDcCreate failed winEr 0x%x", winEr));
        return (winEr == ERROR_SUCCESS);
    }

    if (pDispDefPrimary == NULL)
    {
        /* Try to keep the current primary display by finding it. */
        for (i = 0; i < cDispDef; ++i)
        {
            int const iPath = vboxDispIfWddmDcSearchPath(&DispCfg, i, i);
            if (iPath < 0)
                continue;

            DISPLAYCONFIG_PATH_INFO *pPathInfo = &DispCfg.pPathInfoArray[iPath];
            if (pPathInfo->flags & DISPLAYCONFIG_PATH_ACTIVE)
            {
                UINT const iSrcMode = pPathInfo->sourceInfo.modeInfoIdx;
                DISPLAYCONFIG_SOURCE_MODE *pSrcMode = &DispCfg.pModeInfoArray[iSrcMode].sourceMode;
                if (   pSrcMode->position.x == 0
                    && pSrcMode->position.y == 0)
                {
                     /* "The source surface that is positioned at (0, 0) is considered the primary." */
                     pDispDefPrimary = &paDispDef[i];
                     break;
                }
            }
        }
    }

    for (i = 0; i < cDispDef; ++i)
    {
        pDispDef = &paDispDef[i];

        /* Modify the path which the same source and target ids. */
        int const iPath = vboxDispIfWddmDcSearchPath(&DispCfg, pDispDef->idDisplay, pDispDef->idDisplay);
        if (iPath < 0)
        {
            WARN(("VBoxTray:(WDDM) Unexpected iPath(%d) between src(%d) and tgt(%d)\n", iPath, pDispDef->idDisplay, pDispDef->idDisplay));
            continue;
        }

        /* If the source is used by another active path, then deactivate the path. */
        int const iActiveSrcPath = vboxDispIfWddmDcSearchActiveSourcePath(&DispCfg, pDispDef->idDisplay);
        if (iActiveSrcPath >= 0 && iActiveSrcPath != iPath)
            DispCfg.pPathInfoArray[iActiveSrcPath].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;

        DISPLAYCONFIG_PATH_INFO *pPathInfo = &DispCfg.pPathInfoArray[iPath];

        if (!(pDispDef->fDisplayFlags & VMMDEV_DISPLAY_DISABLED))
        {
            DISPLAYCONFIG_SOURCE_MODE *pSrcMode;
            DISPLAYCONFIG_TARGET_MODE *pTgtMode;

            if (pPathInfo->flags & DISPLAYCONFIG_PATH_ACTIVE)
            {
                UINT iSrcMode = pPathInfo->sourceInfo.modeInfoIdx;
                UINT iTgtMode = pPathInfo->targetInfo.modeInfoIdx;

                if (iSrcMode >= DispCfg.cModeInfoArray || iTgtMode >= DispCfg.cModeInfoArray)
                {
                    WARN(("VBoxTray:(WDDM) Unexpected iSrcMode(%d) and/or iTgtMode(%d)\n", iSrcMode, iTgtMode));
                    continue;
                }

                pSrcMode = &DispCfg.pModeInfoArray[iSrcMode].sourceMode;
                pTgtMode = &DispCfg.pModeInfoArray[iTgtMode].targetMode;

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CX)
                {
                    pSrcMode->width =
                    pTgtMode->targetVideoSignalInfo.activeSize.cx =
                    pTgtMode->targetVideoSignalInfo.totalSize.cx = pDispDef->cx;
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CY)
                {
                    pSrcMode->height =
                    pTgtMode->targetVideoSignalInfo.activeSize.cy =
                    pTgtMode->targetVideoSignalInfo.totalSize.cy = pDispDef->cy;
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN)
                {
                    pSrcMode->position.x = pDispDef->xOrigin - (pDispDefPrimary ? pDispDefPrimary->xOrigin : 0);
                    pSrcMode->position.y = pDispDef->yOrigin - (pDispDefPrimary ? pDispDefPrimary->yOrigin : 0);
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_BPP)
                {
                    switch (pDispDef->cBitsPerPixel)
                    {
                    case 32:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                        break;
                    case 24:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
                        break;
                    case 16:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
                        break;
                    case 8:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
                        break;
                    default:
                        WARN(("VBoxTray: (WDDM) invalid bpp %d, using 32bpp instead\n", pDispDef->cBitsPerPixel));
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                        break;
                    }
                }
            }
            else
            {
                /* "The source and target modes for each source and target identifiers can only appear
                 * in the modeInfoArray array once."
                 * Try to find the source mode.
                 */
                DISPLAYCONFIG_MODE_INFO *pSrcModeInfo = NULL, *pTgtModeInfo = NULL;
                int iSrcModeInfo = -1, iTgtModeInfo = -1;
                int cModeInfoNew = 0;

                for (UINT j = 0; j < DispCfg.cModeInfoArray; ++j)
                {
                    if (   DispCfg.pModeInfoArray[j].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE
                        && DispCfg.pModeInfoArray[j].id == pDispDef->idDisplay)
                    {
                        pSrcModeInfo = &DispCfg.pModeInfoArray[j];
                        iSrcModeInfo = (int)j;
                        break;
                    }
                }

                for (UINT j = 0; j < DispCfg.cModeInfoArray; ++j)
                {
                    if (   DispCfg.pModeInfoArray[j].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET
                        && DispCfg.pModeInfoArray[j].id == pDispDef->idDisplay)
                    {
                        pTgtModeInfo = &DispCfg.pModeInfoArray[j];
                        iTgtModeInfo = (int)j;
                        break;
                    }
                }

                if (pSrcModeInfo == NULL)
                    cModeInfoNew++;

                if (pTgtModeInfo == NULL)
                    cModeInfoNew++;

                if (cModeInfoNew > 0)
                {
                    DISPLAYCONFIG_MODE_INFO *paModeInfo = (DISPLAYCONFIG_MODE_INFO *)RTMemRealloc(DispCfg.pModeInfoArray,
                                                                                                    (DispCfg.cModeInfoArray + cModeInfoNew)
                                                                                                  * sizeof(paModeInfo[0]));

                    if (!paModeInfo)
                    {
                        WARN(("VBoxTray:(WDDM) Unable to re-allocate DispCfg.pModeInfoArray\n"));
                        continue;
                    }

                    DispCfg.pModeInfoArray = paModeInfo;

                    if (pSrcModeInfo == NULL)
                    {
                        iSrcModeInfo = DispCfg.cModeInfoArray;
                        pSrcModeInfo = &paModeInfo[iSrcModeInfo];
                        DispCfg.cModeInfoArray++;

                        RT_ZERO(*pSrcModeInfo);
                        pSrcModeInfo->infoType  = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
                        pSrcModeInfo->id        = pDispDef->idDisplay;
                        pSrcModeInfo->adapterId = DispCfg.pModeInfoArray[0].adapterId;
                    }

                    if (pTgtModeInfo == NULL)
                    {
                        iTgtModeInfo = DispCfg.cModeInfoArray;
                        pTgtModeInfo = &paModeInfo[iTgtModeInfo];
                        DispCfg.cModeInfoArray++;

                        RT_ZERO(*pTgtModeInfo);
                        pTgtModeInfo->infoType  = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
                        pTgtModeInfo->id        = pDispDef->idDisplay;
                        pTgtModeInfo->adapterId = DispCfg.pModeInfoArray[0].adapterId;

                        pTgtModeInfo->targetMode.targetVideoSignalInfo.pixelRate = 0xfffffffe;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.hSyncFreq.Numerator = 0xfffffffe;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.hSyncFreq.Denominator = 0xfffffffe;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.vSyncFreq.Numerator = 0xfffffffe;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.vSyncFreq.Denominator = 0xfffffffe;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.videoStandard = 255;
                        pTgtModeInfo->targetMode.targetVideoSignalInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
                    }
                }

                /* Update the source mode information. */
                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CX)
                {
                    pSrcModeInfo->sourceMode.width = pDispDef->cx;
                    pTgtModeInfo->targetMode.targetVideoSignalInfo.activeSize.cx =
                    pTgtModeInfo->targetMode.targetVideoSignalInfo.totalSize.cx = pDispDef->cx;
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_CY)
                {
                    pSrcModeInfo->sourceMode.height = pDispDef->cy;
                    pTgtModeInfo->targetMode.targetVideoSignalInfo.activeSize.cy =
                    pTgtModeInfo->targetMode.targetVideoSignalInfo.totalSize.cy = pDispDef->cy;
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_BPP)
                {
                    switch (pDispDef->cBitsPerPixel)
                    {
                        case 32:
                            pSrcModeInfo->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                            break;
                        case 24:
                            pSrcModeInfo->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
                            break;
                        case 16:
                            pSrcModeInfo->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
                            break;
                        case 8:
                            pSrcModeInfo->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
                            break;
                        default:
                            WARN(("VBoxTray: (WDDM) invalid bpp %d, using 32bpp instead\n", pDispDef->cBitsPerPixel));
                            pSrcModeInfo->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                            break;
                    }
                }

                if (pDispDef->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN)
                {
                    pSrcModeInfo->sourceMode.position.x = pDispDef->xOrigin - (pDispDefPrimary ? pDispDefPrimary->xOrigin : 0);
                    pSrcModeInfo->sourceMode.position.y = pDispDef->yOrigin - (pDispDefPrimary ? pDispDefPrimary->yOrigin : 0);
                }

                /* Configure the path information. */
                Assert(pPathInfo->sourceInfo.id == pDispDef->idDisplay);
                pPathInfo->sourceInfo.modeInfoIdx = iSrcModeInfo;

                Assert(pPathInfo->targetInfo.id == pDispDef->idDisplay);
                pPathInfo->targetInfo.modeInfoIdx      = iTgtModeInfo;
                pPathInfo->targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15;
                pPathInfo->targetInfo.rotation         = DISPLAYCONFIG_ROTATION_IDENTITY;
                pPathInfo->targetInfo.scaling          = DISPLAYCONFIG_SCALING_PREFERRED;
                /* "A refresh rate with both the numerator and denominator set to zero indicates that
                 * the caller does not specify a refresh rate and the operating system should use
                 * the most optimal refresh rate available. For this case, in a call to the SetDisplayConfig
                 * function, the caller must set the scanLineOrdering member to the
                 * DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED value; otherwise, SetDisplayConfig fails."
                 *
                 * If a refresh rate is set to a value, then the resize will fail if miniport driver
                 * does not support VSync, i.e. with display-only driver on Win8+ (@bugref{8440}).
                 */
                pPathInfo->targetInfo.refreshRate.Numerator   = 0;
                pPathInfo->targetInfo.refreshRate.Denominator = 0;
                pPathInfo->targetInfo.scanLineOrdering        = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
                /* Make sure that "The output can be forced on this target even if a monitor is not detected." */
                pPathInfo->targetInfo.targetAvailable         = TRUE;
                pPathInfo->targetInfo.statusFlags             = DISPLAYCONFIG_TARGET_FORCIBLE;
            }

            pPathInfo->flags |= DISPLAYCONFIG_PATH_ACTIVE;
        }
        else
        {
            pPathInfo->flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
        }
    }

    UINT fSetFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to VALIDATE winEr %d.\n", winEr));
        vboxDispIfWddmDcLogRel(&DispCfg, fSetFlags);
        fSetFlags |= SDC_ALLOW_CHANGES;
    }
    else
    {
        Log(("VBoxTray:(WDDM) pfnSetDisplayConfig Ok to VALIDATE.\n"));
        vboxDispIfWddmDcLogRel(&DispCfg, fSetFlags);
    }

    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_SAVE_TO_DATABASE | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to SET, winEr %d.\n", winEr));

        vboxDispIfWddmDcSettingsInvalidateModeIndeces(&DispCfg);
        winEr = vboxDispIfWddmDcSet(&DispCfg, SDC_TOPOLOGY_SUPPLIED | SDC_ALLOW_PATH_ORDER_CHANGES | SDC_APPLY);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to APPLY TOPOLOGY ONLY, winEr %d.\n", winEr));
            winEr = vboxDispIfWddmDcSet(&DispCfg, SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_APPLY);
            if (winEr != ERROR_SUCCESS)
            {
                WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to APPLY ANY TOPOLOGY, winEr %d.\n", winEr));
            }
        }
    }

    vboxDispIfWddmDcTerm(&DispCfg);

    return (winEr == ERROR_SUCCESS);
}

static DWORD vboxDispIfWddmResizeDisplay2(PCVBOXDISPIF const pIf, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT devModes)
{
    RT_NOREF(pIf, paDeviceModes);
    VBOXDISPIF_WDDM_DISPCFG DispCfg;
    DWORD winEr = ERROR_SUCCESS;
    UINT idx;
    int iPath;

    winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);

    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcCreate\n"));
        return winEr;
    }

    for (idx = 0; idx < devModes; idx++)
    {
        DEVMODE *pDeviceMode = &paDeviceModes[idx];

        if (paDisplayDevices[idx].StateFlags & DISPLAY_DEVICE_ACTIVE)
        {
            DISPLAYCONFIG_PATH_INFO *pPathInfo;

            iPath = vboxDispIfWddmDcSearchPath(&DispCfg, idx, idx);

            if (iPath < 0)
            {
                WARN(("VBoxTray:(WDDM) Unexpected iPath(%d) between src(%d) and tgt(%d)\n", iPath, idx, idx));
                continue;
            }

            pPathInfo = &DispCfg.pPathInfoArray[iPath];

            if (pPathInfo->flags & DISPLAYCONFIG_PATH_ACTIVE)
            {
                UINT iSrcMode, iTgtMode;
                DISPLAYCONFIG_SOURCE_MODE *pSrcMode;
                DISPLAYCONFIG_TARGET_MODE *pTgtMode;

                iSrcMode = pPathInfo->sourceInfo.modeInfoIdx;
                iTgtMode = pPathInfo->targetInfo.modeInfoIdx;

                if (iSrcMode >= DispCfg.cModeInfoArray || iTgtMode >= DispCfg.cModeInfoArray)
                {
                    WARN(("VBoxTray:(WDDM) Unexpected iSrcMode(%d) and/or iTgtMode(%d)\n", iSrcMode, iTgtMode));
                    continue;
                }

                pSrcMode = &DispCfg.pModeInfoArray[iSrcMode].sourceMode;
                pTgtMode = &DispCfg.pModeInfoArray[iTgtMode].targetMode;

                if (pDeviceMode->dmFields & DM_PELSWIDTH)
                {
                    pSrcMode->width = pDeviceMode->dmPelsWidth;
                    pTgtMode->targetVideoSignalInfo.activeSize.cx = pDeviceMode->dmPelsWidth;
                    pTgtMode->targetVideoSignalInfo.totalSize.cx  = pDeviceMode->dmPelsWidth;
                }

                if (pDeviceMode->dmFields & DM_PELSHEIGHT)
                {
                    pSrcMode->height = pDeviceMode->dmPelsHeight;
                    pTgtMode->targetVideoSignalInfo.activeSize.cy = pDeviceMode->dmPelsHeight;
                    pTgtMode->targetVideoSignalInfo.totalSize.cy  = pDeviceMode->dmPelsHeight;
                }

                if (pDeviceMode->dmFields & DM_POSITION)
                {
                    pSrcMode->position.x = pDeviceMode->dmPosition.x;
                    pSrcMode->position.y = pDeviceMode->dmPosition.y;
                }

                if (pDeviceMode->dmFields & DM_BITSPERPEL)
                {
                    switch (pDeviceMode->dmBitsPerPel)
                    {
                    case 32:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                        break;
                    case 24:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
                        break;
                    case 16:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
                        break;
                    case 8:
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
                        break;
                    default:
                        LogRel(("VBoxTray: (WDDM) invalid bpp %d, using 32bpp instead\n", pDeviceMode->dmBitsPerPel));
                        pSrcMode->pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                        break;
                    }
                }
            }
            else
            {
                DISPLAYCONFIG_MODE_INFO *pModeInfo = (DISPLAYCONFIG_MODE_INFO *)RTMemRealloc(DispCfg.pModeInfoArray,
                                                                                               (DispCfg.cModeInfoArray + 2)
                                                                                             * sizeof(pModeInfo[0]));
                if (!pModeInfo)
                {
                    WARN(("VBoxTray:(WDDM) Unable to re-allocate DispCfg.pModeInfoArray\n"));
                    continue;
                }

                DispCfg.pModeInfoArray = pModeInfo;

                *pPathInfo = DispCfg.pPathInfoArray[0];
                pPathInfo->sourceInfo.id = idx;
                pPathInfo->targetInfo.id = idx;

                DISPLAYCONFIG_MODE_INFO *pModeInfoNew = &pModeInfo[DispCfg.cModeInfoArray];

                pModeInfoNew->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
                pModeInfoNew->id = idx;
                pModeInfoNew->adapterId = pModeInfo[0].adapterId;
                pModeInfoNew->sourceMode.width  = pDeviceMode->dmPelsWidth;
                pModeInfoNew->sourceMode.height = pDeviceMode->dmPelsHeight;
                pModeInfoNew->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                pModeInfoNew->sourceMode.position.x = pDeviceMode->dmPosition.x;
                pModeInfoNew->sourceMode.position.y = pDeviceMode->dmPosition.y;
                pPathInfo->sourceInfo.modeInfoIdx = DispCfg.cModeInfoArray;

                pModeInfoNew++;
                pModeInfoNew->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
                pModeInfoNew->id = idx;
                pModeInfoNew->adapterId = pModeInfo[0].adapterId;
                pModeInfoNew->targetMode = pModeInfo[0].targetMode;
                pModeInfoNew->targetMode.targetVideoSignalInfo.activeSize.cx = pDeviceMode->dmPelsWidth;
                pModeInfoNew->targetMode.targetVideoSignalInfo.totalSize.cx  = pDeviceMode->dmPelsWidth;
                pModeInfoNew->targetMode.targetVideoSignalInfo.activeSize.cy = pDeviceMode->dmPelsHeight;
                pModeInfoNew->targetMode.targetVideoSignalInfo.totalSize.cy  = pDeviceMode->dmPelsHeight;
                pPathInfo->targetInfo.modeInfoIdx = DispCfg.cModeInfoArray + 1;

                DispCfg.cModeInfoArray += 2;
            }
        }
        else
        {
            iPath = vboxDispIfWddmDcSearchActivePath(&DispCfg, idx, idx);

            if (iPath >= 0)
            {
                DispCfg.pPathInfoArray[idx].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
            }
        }
    }

    UINT fSetFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
        fSetFlags |= SDC_ALLOW_CHANGES;
    }

    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_SAVE_TO_DATABASE | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
    }

    vboxDispIfWddmDcTerm(&DispCfg);

    return winEr;
}

static DWORD vboxDispIfWddmResizeDisplay(PCVBOXDISPIF const pIf, UINT Id, BOOL fEnable, DISPLAY_DEVICE *paDisplayDevices,
                                         DEVMODE *paDeviceModes, UINT devModes)
{
    RT_NOREF(paDisplayDevices, devModes);
    VBOXDISPIF_WDDM_DISPCFG DispCfg;
    DWORD winEr;
    int iPath;

    winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcCreate\n"));
        return winEr;
    }

    iPath = vboxDispIfWddmDcSearchActivePath(&DispCfg, Id, Id);

    if (iPath < 0)
    {
        vboxDispIfWddmDcTerm(&DispCfg);

        if (!fEnable)
        {
            /* nothing to be done here, just leave */
            return ERROR_SUCCESS;
        }

        winEr = vboxDispIfWddmEnableDisplaysTryingTopology(pIf, 1, &Id, fEnable);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmEnableDisplaysTryingTopology winEr %d\n", winEr));
            return winEr;
        }

        winEr = vboxDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcCreate winEr %d\n", winEr));
            return winEr;
        }

        iPath = vboxDispIfWddmDcSearchPath(&DispCfg, Id, Id);
        if (iPath < 0)
        {
            WARN(("VBoxTray: (WDDM) path (%d) is still disabled, going to retry winEr %d\n", winEr));
            vboxDispIfWddmDcTerm(&DispCfg);
            return ERROR_RETRY;
        }
    }

    Assert(iPath >= 0);

    if (!fEnable)
    {
        /* need to disable it, and we are done */
        vboxDispIfWddmDcTerm(&DispCfg);

        winEr = vboxDispIfWddmEnableDisplaysTryingTopology(pIf, 1, &Id, fEnable);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmEnableDisplaysTryingTopology winEr %d\n", winEr));
            return winEr;
        }

        return winEr;
    }

    Assert(fEnable);

    winEr = vboxDispIfWddmDcSettingsUpdate(&DispCfg, iPath, &paDeviceModes[Id], FALSE, fEnable);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray: (WDDM) Failed vboxDispIfWddmDcSettingsUpdate\n"));
        vboxDispIfWddmDcTerm(&DispCfg);
        return winEr;
    }

    UINT fSetFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
        fSetFlags |= SDC_ALLOW_CHANGES;
    }

    winEr = vboxDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_SAVE_TO_DATABASE | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("VBoxTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
    }

    vboxDispIfWddmDcTerm(&DispCfg);

    return winEr;
}

#endif /* VBOX_WITH_WDDM */

DWORD VBoxDispIfResizeModes(PCVBOXDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
#ifndef VBOX_WITH_WDDM
    RT_NOREF(iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);
#endif
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
            return ERROR_NOT_SUPPORTED;
        case VBOXDISPIF_MODE_XPDM:
            return ERROR_NOT_SUPPORTED;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfResizeModesWDDM(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);
#endif
        default:
            WARN(("unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

DWORD VBoxDispIfCancelPendingResize(PCVBOXDISPIF const pIf)
{
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
            return NO_ERROR;
        case VBOXDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfCancelPendingResizeWDDM(pIf);
#endif
        default:
            WARN(("unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

#ifdef VBOX_WITH_WDDM
static DWORD vboxDispIfConfigureTargetsWDDM(VBOXDISPIF_OP *pOp, uint32_t *pcConnected)
{
    VBOXDISPIFESCAPE EscapeHdr = {0};
    EscapeHdr.escapeCode = VBOXESC_CONFIGURETARGETS;
    EscapeHdr.u32CmdSpecific = 0;

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pOp->u.wddm.Adapter.hAdapter;
#ifdef VBOX_DISPIF_WITH_OPCONTEXT
    /* win8.1 does not allow context-based escapes for display-only mode */
    EscapeData.hDevice = pOp->Device.hDevice;
    EscapeData.hContext = pOp->Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = &EscapeHdr;
    EscapeData.PrivateDriverDataSize = sizeof (EscapeHdr);

    NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
    {
        if (pcConnected)
            *pcConnected = EscapeHdr.u32CmdSpecific;
        return NO_ERROR;
    }
    WARN(("VBoxTray: pfnD3DKMTEscape VBOXESC_CONFIGURETARGETS failed Status 0x%x\n", Status));
    return Status;
}

static DWORD vboxDispIfResizeStartedOpWDDM(VBOXDISPIF_OP *pOp)
{
    DWORD NumDevices = VBoxDisplayGetCount();
    if (NumDevices == 0)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: Zero devices found\n"));
        return ERROR_GEN_FAILURE;
    }

    DISPLAY_DEVICE *paDisplayDevices = (DISPLAY_DEVICE *)alloca (sizeof (DISPLAY_DEVICE) * NumDevices);
    DEVMODE *paDeviceModes = (DEVMODE *)alloca (sizeof (DEVMODE) * NumDevices);
    DWORD DevNum = 0;
    DWORD DevPrimaryNum = 0;

    DWORD winEr = VBoxDisplayGetConfig(NumDevices, &DevPrimaryNum, &DevNum, paDisplayDevices, paDeviceModes);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: VBoxGetDisplayConfig failed, %d\n", winEr));
        return winEr;
    }

    if (NumDevices != DevNum)
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: NumDevices(%d) != DevNum(%d)\n", NumDevices, DevNum));


    uint32_t cConnected = 0;
    winEr = vboxDispIfConfigureTargetsWDDM(pOp, &cConnected);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM failed winEr 0x%x\n", winEr));
        return winEr;
    }

    if (!cConnected)
    {
        Log(("VBoxTray: all targets already connected, nothing to do\n"));
        return NO_ERROR;
    }

    winEr = vboxDispIfWaitDisplayDataInited(pOp);
    if (winEr != NO_ERROR)
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: vboxDispIfWaitDisplayDataInited failed winEr 0x%x\n", winEr));

    DWORD NewNumDevices = VBoxDisplayGetCount();
    if (NewNumDevices == 0)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: Zero devices found\n"));
        return ERROR_GEN_FAILURE;
    }

    if (NewNumDevices != NumDevices)
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: NumDevices(%d) != NewNumDevices(%d)\n", NumDevices, NewNumDevices));

    DISPLAY_DEVICE *paNewDisplayDevices = (DISPLAY_DEVICE *)alloca (sizeof (DISPLAY_DEVICE) * NewNumDevices);
    DEVMODE *paNewDeviceModes = (DEVMODE *)alloca (sizeof (DEVMODE) * NewNumDevices);
    DWORD NewDevNum = 0;
    DWORD NewDevPrimaryNum = 0;

    winEr = VBoxDisplayGetConfig(NewNumDevices, &NewDevPrimaryNum, &NewDevNum, paNewDisplayDevices, paNewDeviceModes);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: VBoxGetDisplayConfig failed for new devices, %d\n", winEr));
        return winEr;
    }

    if (NewNumDevices != NewDevNum)
        WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: NewNumDevices(%d) != NewDevNum(%d)\n", NewNumDevices, NewDevNum));

    DWORD minDevNum = RT_MIN(DevNum, NewDevNum);
    UINT *pIds = (UINT*)alloca (sizeof (UINT) * minDevNum);
    UINT cIds = 0;
    for (DWORD i = 0; i < minDevNum; ++i)
    {
        if ((paNewDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE)
                && !(paDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE))
        {
            pIds[cIds] = i;
            ++cIds;
        }
    }

    if (!cIds)
    {
        /* this is something we would not regularly expect */
        WARN(("VBoxTray: all targets already have proper config, nothing to do\n"));
        return NO_ERROR;
    }

    if (pOp->pIf->enmMode > VBOXDISPIF_MODE_WDDM)
    {
        winEr = vboxDispIfWddmEnableDisplaysTryingTopology(pOp->pIf, cIds, pIds, FALSE);
        if (winEr != NO_ERROR)
            WARN(("VBoxTray: vboxDispIfWddmEnableDisplaysTryingTopology failed to record current settings, %d, ignoring\n", winEr));
    }
    else
    {
        for (DWORD i = 0; i < cIds; ++i)
        {
            winEr = vboxDispIfWddmResizeDisplayVista(paNewDeviceModes, paNewDisplayDevices, NewDevNum, i, FALSE, TRUE);
            if (winEr != NO_ERROR)
                WARN(("VBoxTray: vboxDispIfResizeStartedOpWDDM: vboxDispIfWddmResizeDisplayVista failed winEr 0x%x\n", winEr));
        }
    }

    return winEr;
}

static DWORD vboxDispIfResizeStartedWDDM(PCVBOXDISPIF const pIf)
{
    VBOXDISPIF_OP Op;

    DWORD winEr = vboxDispIfOpBeginWddm(pIf, &Op);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfOpBegin failed winEr 0x%x\n", winEr));
        return winEr;
    }

    winEr = vboxDispIfResizeStartedOpWDDM(&Op);
    if (winEr != NO_ERROR)
    {
        WARN(("VBoxTray: vboxDispIfResizeStartedWDDMOp failed winEr 0x%x\n", winEr));
    }

    vboxDispIfOpEndWddm(&Op);

    return winEr;
}
#endif /* VBOX_WITH_WDDM */

DWORD VBoxDispIfResizeStarted(PCVBOXDISPIF const pIf)
{
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
            return NO_ERROR;
        case VBOXDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfResizeStartedWDDM(pIf);
#endif
        default:
            WARN(("unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

static DWORD vboxDispIfSwitchToXPDM_NT4(PVBOXDISPIF pIf)
{
    RT_NOREF(pIf);
    return NO_ERROR;
}

static DWORD vboxDispIfSwitchToXPDM(PVBOXDISPIF pIf)
{
    DWORD err = NO_ERROR;

    uint64_t const uNtVersion = RTSystemGetNtVersion();
    if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0))
    {
        HMODULE hUser = GetModuleHandle("user32.dll");
        if (NULL != hUser)
        {
            *(uintptr_t *)&pIf->modeData.xpdm.pfnChangeDisplaySettingsEx = (uintptr_t)GetProcAddress(hUser, "ChangeDisplaySettingsExA");
            LogFunc(("pfnChangeDisplaySettingsEx = %p\n", pIf->modeData.xpdm.pfnChangeDisplaySettingsEx));
            bool const fSupported = RT_BOOL(pIf->modeData.xpdm.pfnChangeDisplaySettingsEx);
            if (!fSupported)
            {
                WARN(("pfnChangeDisplaySettingsEx function pointer failed to initialize\n"));
                err = ERROR_NOT_SUPPORTED;
            }
        }
        else
        {
            WARN(("failed to get USER32 handle, err (%d)\n", GetLastError()));
            err = ERROR_NOT_SUPPORTED;
        }
    }
    else
    {
        WARN(("can not switch to VBOXDISPIF_MODE_XPDM, because os is not >= w2k\n"));
        err = ERROR_NOT_SUPPORTED;
    }

    return err;
}

DWORD VBoxDispIfSwitchMode(PVBOXDISPIF pIf, VBOXDISPIF_MODE enmMode, VBOXDISPIF_MODE *penmOldMode)
{
    /** @todo may need to addd synchronization in case we want to change modes dynamically
     * i.e. currently the mode is supposed to be initialized once on service initialization */
    if (penmOldMode)
        *penmOldMode = pIf->enmMode;

    if (enmMode == pIf->enmMode)
        return NO_ERROR;

    /* Make sure that we never try to run anything else but VBOXDISPIF_MODE_XPDM_NT4 on NT4 guests.
     * Anything else will get us into serious trouble. */
    if (RTSystemGetNtVersion() < RTSYSTEM_MAKE_NT_VERSION(5, 0, 0))
        enmMode = VBOXDISPIF_MODE_XPDM_NT4;

#ifdef VBOX_WITH_WDDM
    if (pIf->enmMode >= VBOXDISPIF_MODE_WDDM)
    {
        vboxDispIfWddmTerm(pIf);

        vboxDispKmtCallbacksTerm(&pIf->modeData.wddm.KmtCallbacks);
    }
#endif

    DWORD err = NO_ERROR;
    switch (enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
            LogFunc(("request to switch to VBOXDISPIF_MODE_XPDM_NT4\n"));
            err = vboxDispIfSwitchToXPDM_NT4(pIf);
            if (err == NO_ERROR)
            {
                LogFunc(("successfully switched to XPDM_NT4 mode\n"));
                pIf->enmMode = VBOXDISPIF_MODE_XPDM_NT4;
            }
            else
                WARN(("failed to switch to XPDM_NT4 mode, err (%d)\n", err));
            break;
        case VBOXDISPIF_MODE_XPDM:
            LogFunc(("request to switch to VBOXDISPIF_MODE_XPDM\n"));
            err = vboxDispIfSwitchToXPDM(pIf);
            if (err == NO_ERROR)
            {
                LogFunc(("successfully switched to XPDM mode\n"));
                pIf->enmMode = VBOXDISPIF_MODE_XPDM;
            }
            else
                WARN(("failed to switch to XPDM mode, err (%d)\n", err));
            break;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        {
            LogFunc(("request to switch to VBOXDISPIF_MODE_WDDM\n"));
            err = vboxDispIfSwitchToWDDM(pIf);
            if (err == NO_ERROR)
            {
                LogFunc(("successfully switched to WDDM mode\n"));
                pIf->enmMode = VBOXDISPIF_MODE_WDDM;
            }
            else
                WARN(("failed to switch to WDDM mode, err (%d)\n", err));
            break;
        }
        case VBOXDISPIF_MODE_WDDM_W7:
        {
            LogFunc(("request to switch to VBOXDISPIF_MODE_WDDM_W7\n"));
            err = vboxDispIfSwitchToWDDM_W7(pIf);
            if (err == NO_ERROR)
            {
                LogFunc(("successfully switched to WDDM mode\n"));
                pIf->enmMode = VBOXDISPIF_MODE_WDDM_W7;
            }
            else
                WARN(("failed to switch to WDDM mode, err (%d)\n", err));
            break;
        }
#endif
        default:
            err = ERROR_INVALID_PARAMETER;
            break;
    }
    return err;
}

#ifdef VBOX_WITH_WDDM
static DWORD vboxDispIfSeamlessCreateWDDM(PCVBOXDISPIF const pIf, VBOXDISPIF_SEAMLESS *pSeamless, HANDLE hEvent)
{
    RT_NOREF(hEvent);
    HRESULT hr = VBoxWddmKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &pSeamless->modeData.wddm.Adapter);
    if (SUCCEEDED(hr))
    {
#ifndef VBOX_DISPIF_WITH_OPCONTEXT
        return ERROR_SUCCESS;
#else
        hr = vboxDispKmtCreateDevice(&pSeamless->modeData.wddm.Adapter, &pSeamless->modeData.wddm.Device);
        if (SUCCEEDED(hr))
        {
            hr = vboxDispKmtCreateContext(&pSeamless->modeData.wddm.Device, &pSeamless->modeData.wddm.Context, VBOXWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS,
                    hEvent, 0ULL);
            if (SUCCEEDED(hr))
                return ERROR_SUCCESS;
            WARN(("VBoxTray: vboxDispKmtCreateContext failed hr 0x%x", hr));

            vboxDispKmtDestroyDevice(&pSeamless->modeData.wddm.Device);
        }
        else
            WARN(("VBoxTray: vboxDispKmtCreateDevice failed hr 0x%x", hr));

        VBoxWddmKmtCloseAdapter(&pSeamless->modeData.wddm.Adapter);
#endif /* VBOX_DISPIF_WITH_OPCONTEXT */
    }

    return hr;
}

static DWORD vboxDispIfSeamlessTermWDDM(VBOXDISPIF_SEAMLESS *pSeamless)
{
#ifdef VBOX_DISPIF_WITH_OPCONTEXT
    vboxDispKmtDestroyContext(&pSeamless->modeData.wddm.Context);
    vboxDispKmtDestroyDevice(&pSeamless->modeData.wddm.Device);
#endif
    VBoxWddmKmtCloseAdapter(&pSeamless->modeData.wddm.Adapter);

    return NO_ERROR;
}

static DWORD vboxDispIfSeamlessSubmitWDDM(VBOXDISPIF_SEAMLESS *pSeamless, VBOXDISPIFESCAPE *pData, int cbData)
{
    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pSeamless->modeData.wddm.Adapter.hAdapter;
#ifdef VBOX_DISPIF_WITH_OPCONTEXT
    EscapeData.hDevice = pSeamless->modeData.wddm.Device.hDevice;
    EscapeData.hContext = pSeamless->modeData.wddm.Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    /*EscapeData.Flags.HardwareAccess = 1;*/
    EscapeData.pPrivateDriverData = pData;
    EscapeData.PrivateDriverDataSize = VBOXDISPIFESCAPE_SIZE(cbData);

    NTSTATUS Status = pSeamless->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        return ERROR_SUCCESS;

    WARN(("VBoxTray: pfnD3DKMTEscape Seamless failed Status 0x%x\n", Status));
    return Status;
}
#endif /* VBOX_WITH_WDDM */

DWORD VBoxDispIfSeamlessCreate(PCVBOXDISPIF const pIf, VBOXDISPIF_SEAMLESS *pSeamless, HANDLE hEvent)
{
    memset(pSeamless, 0, sizeof (*pSeamless));
    pSeamless->pIf = pIf;

    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
        case VBOXDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfSeamlessCreateWDDM(pIf, pSeamless, hEvent);
#endif
        default:
            break;
    }

#ifndef VBOX_WITH_WDDM
    RT_NOREF(hEvent);
#endif
    WARN(("VBoxTray: VBoxDispIfSeamlessCreate: invalid mode %d\n", pIf->enmMode));
    return ERROR_INVALID_PARAMETER;
}

DWORD VBoxDispIfSeamlessTerm(VBOXDISPIF_SEAMLESS *pSeamless)
{
    PCVBOXDISPIF const pIf = pSeamless->pIf;
    DWORD winEr;
    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
        case VBOXDISPIF_MODE_XPDM:
            winEr = NO_ERROR;
            break;
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            winEr = vboxDispIfSeamlessTermWDDM(pSeamless);
            break;
#endif
        default:
            WARN(("VBoxTray: VBoxDispIfSeamlessTerm: invalid mode %d\n", pIf->enmMode));
            winEr = ERROR_INVALID_PARAMETER;
            break;
    }

    if (winEr == NO_ERROR)
        memset(pSeamless, 0, sizeof (*pSeamless));

    return winEr;
}

DWORD VBoxDispIfSeamlessSubmit(VBOXDISPIF_SEAMLESS *pSeamless, VBOXDISPIFESCAPE *pData, int cbData)
{
    PCVBOXDISPIF const pIf = pSeamless->pIf;

    if (pData->escapeCode != VBOXESC_SETVISIBLEREGION)
    {
        WARN(("VBoxTray: invalid escape code for Seamless submit %d\n", pData->escapeCode));
        return ERROR_INVALID_PARAMETER;
    }

    switch (pIf->enmMode)
    {
        case VBOXDISPIF_MODE_XPDM_NT4:
        case VBOXDISPIF_MODE_XPDM:
            return VBoxDispIfEscape(pIf, pData, cbData);
#ifdef VBOX_WITH_WDDM
        case VBOXDISPIF_MODE_WDDM:
        case VBOXDISPIF_MODE_WDDM_W7:
            return vboxDispIfSeamlessSubmitWDDM(pSeamless, pData, cbData);
#endif
        default:
            WARN(("VBoxTray: VBoxDispIfSeamlessSubmit: invalid mode %d\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

