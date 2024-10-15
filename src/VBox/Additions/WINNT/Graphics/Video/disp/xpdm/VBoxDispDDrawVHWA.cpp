/* $Id: VBoxDispDDrawVHWA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, DirectDraw callbacks VHWA related
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

#include "VBoxDisp.h"
#include "VBoxDispDDraw.h"
#include <iprt/asm.h>

/** @callback_method_impl{FNVBOXVHWACMDCOMPLETION} */
static DECLCALLBACK(void)
VBoxDispVHWASurfBltCompletion(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, void *pvContext)
{
    RT_NOREF(pvContext);
    VBOXVHWACMD_SURF_BLT RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_BLT);
    PVBOXVHWASURFDESC pSrcDesc  = (PVBOXVHWASURFDESC)(uintptr_t)pBody->SrcGuestSurfInfo;
    PVBOXVHWASURFDESC pDestDesc = (PVBOXVHWASURFDESC)(uintptr_t)pBody->DstGuestSurfInfo;

    ASMAtomicDecU32(&pSrcDesc->cPendingBltsSrc);
    ASMAtomicDecU32(&pDestDesc->cPendingBltsDst);

    VBoxDispVHWACommandRelease(pDev, pCmd);
}

/** @callback_method_impl{FNVBOXVHWACMDCOMPLETION} */
static DECLCALLBACK(void)
VBoxDispVHWASurfFlipCompletion(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd, void *pvContext)
{
    RT_NOREF(pvContext);
    VBOXVHWACMD_SURF_FLIP RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);
    PVBOXVHWASURFDESC pCurrDesc = (PVBOXVHWASURFDESC)(uintptr_t)pBody->CurrGuestSurfInfo;
    PVBOXVHWASURFDESC pTargDesc = (PVBOXVHWASURFDESC)(uintptr_t)pBody->TargGuestSurfInfo;

    ASMAtomicDecU32(&pCurrDesc->cPendingFlipsCurr);
    ASMAtomicDecU32(&pTargDesc->cPendingFlipsTarg);

    VBoxDispVHWACommandRelease(pDev, pCmd);
}

#define VBOXVHWA_CAP(_pdev, _cap) ((_pdev)->vhwa.caps & (_cap))
#define ROP_INDEX(_rop) ((BYTE)((_rop)>>16))
#define SET_SUPPORT_ROP(_aRops, _rop) _aRops[ROP_INDEX(_rop)/32] |= 1L << ((DWORD)(ROP_INDEX(_rop)%32))

int VBoxDispVHWAUpdateDDHalInfo(PVBOXDISPDEV pDev, DD_HALINFO *pHalInfo)
{
    if (!VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT) && !VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY))
    {
        return VERR_NOT_SUPPORTED;
    }

    pHalInfo->ddCaps.dwCaps |= VBoxDispVHWAToDDCAPS(pDev->vhwa.caps);
    if (VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT))
    {
        /* we only support simple dst=src copy
         * Note: search "ternary raster operations" on msdn for more info
         */
        SET_SUPPORT_ROP(pHalInfo->ddCaps.dwRops, SRCCOPY);
    }

    pHalInfo->ddCaps.ddsCaps.dwCaps |= VBoxDispVHWAToDDSCAPS(pDev->vhwa.surfaceCaps);
    pHalInfo->ddCaps.dwCaps2 |= VBoxDispVHWAToDDCAPS2(pDev->vhwa.caps2);

    if (VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT) && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLTSTRETCH))
    {
        pHalInfo->ddCaps.dwFXCaps |= DDFXCAPS_BLTSTRETCHX  | DDFXCAPS_BLTSTRETCHY
                                   | DDFXCAPS_BLTSTRETCHXN | DDFXCAPS_BLTSTRETCHYN
                                   | DDFXCAPS_BLTSHRINKX   | DDFXCAPS_BLTSHRINKY
                                   | DDFXCAPS_BLTSHRINKXN  | DDFXCAPS_BLTSHRINKYN
                                   | DDFXCAPS_BLTARITHSTRETCHY;
    }

    if (VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY) && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAYSTRETCH))
    {
        pHalInfo->ddCaps.dwFXCaps |= DDFXCAPS_OVERLAYSTRETCHX  | DDFXCAPS_OVERLAYSTRETCHY
                                   | DDFXCAPS_OVERLAYSTRETCHXN | DDFXCAPS_OVERLAYSTRETCHYN
                                   | DDFXCAPS_OVERLAYSHRINKX   | DDFXCAPS_OVERLAYSHRINKY
                                   | DDFXCAPS_OVERLAYSHRINKXN  | DDFXCAPS_OVERLAYSHRINKYN
                                   | DDFXCAPS_OVERLAYARITHSTRETCHY;
    }

    pHalInfo->ddCaps.dwCKeyCaps = VBoxDispVHWAToDDCKEYCAPS(pDev->vhwa.colorKeyCaps);

    if (VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY))
    {
        pHalInfo->ddCaps.dwMaxVisibleOverlays = pDev->vhwa.numOverlays;
        pHalInfo->ddCaps.dwCurrVisibleOverlays = 0;
        pHalInfo->ddCaps.dwMinOverlayStretch = 1;
        pHalInfo->ddCaps.dwMaxOverlayStretch = 32000;
    }

    return VINF_SUCCESS;
}

/*
 * DirectDraw callbacks.
 */

#define IF_NOT_SUPPORTED(_guid)                  \
    if (IsEqualIID(&lpData->guidInfo, &(_guid))) \
    {                                            \
        LOG((#_guid));                           \
    }

DWORD APIENTRY VBoxDispDDGetDriverInfo(DD_GETDRIVERINFODATA *lpData)
{
    LOGF_ENTER();

    lpData->ddRVal = DDERR_CURRENTLYNOTAVAIL;

    if (IsEqualIID(&lpData->guidInfo, &GUID_NTPrivateDriverCaps))
    {
        LOG(("GUID_NTPrivateDriverCaps"));

        DD_NTPRIVATEDRIVERCAPS caps;
        memset(&caps, 0, sizeof(caps));
        caps.dwSize = sizeof(DD_NTPRIVATEDRIVERCAPS);
        caps.dwPrivateCaps = DDHAL_PRIVATECAP_NOTIFYPRIMARYCREATION;

        lpData->dwActualSize = sizeof(DD_NTPRIVATEDRIVERCAPS);
        lpData->ddRVal = DD_OK;
        memcpy(lpData->lpvData, &caps, min(lpData->dwExpectedSize, sizeof(DD_NTPRIVATEDRIVERCAPS)));
    }
    else IF_NOT_SUPPORTED(GUID_NTCallbacks)
    else IF_NOT_SUPPORTED(GUID_D3DCallbacks2)
    else IF_NOT_SUPPORTED(GUID_D3DCallbacks3)
    else IF_NOT_SUPPORTED(GUID_D3DExtendedCaps)
    else IF_NOT_SUPPORTED(GUID_ZPixelFormats)
    else IF_NOT_SUPPORTED(GUID_D3DParseUnknownCommandCallback)
    else IF_NOT_SUPPORTED(GUID_Miscellaneous2Callbacks)
    else IF_NOT_SUPPORTED(GUID_UpdateNonLocalHeap)
    else IF_NOT_SUPPORTED(GUID_GetHeapAlignment)
    else IF_NOT_SUPPORTED(GUID_DDStereoMode)
    else IF_NOT_SUPPORTED(GUID_NonLocalVidMemCaps)
    else IF_NOT_SUPPORTED(GUID_KernelCaps)
    else IF_NOT_SUPPORTED(GUID_KernelCallbacks)
    else IF_NOT_SUPPORTED(GUID_MotionCompCallbacks)
    else IF_NOT_SUPPORTED(GUID_VideoPortCallbacks)
    else IF_NOT_SUPPORTED(GUID_ColorControlCallbacks)
    else IF_NOT_SUPPORTED(GUID_VideoPortCaps)
    else IF_NOT_SUPPORTED(GUID_DDMoreSurfaceCaps)
    else
    {
        LOG(("unknown guid"));
    }


    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDSetColorKey(PDD_SETCOLORKEYDATA lpSetColorKey)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpSetColorKey->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pSurf = lpSetColorKey->lpDDSurface;
    PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)pSurf->lpGbl->dwReserved1;

    lpSetColorKey->ddRVal = DD_OK;

    if (pDesc)
    {
        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd =
            VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_SURF_COLORKEY_SET, sizeof(VBOXVHWACMD_SURF_COLORKEY_SET));
        if (pCmd)
        {
            VBOXVHWACMD_SURF_COLORKEY_SET RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_COLORKEY_SET);

            pBody->u.in.offSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pSurf->lpGbl->fpVidMem);
            pBody->u.in.hSurf = pDesc->hHostHandle;
            pBody->u.in.flags = VBoxDispVHWAFromDDCKEYs(lpSetColorKey->dwFlags);
            VBoxDispVHWAFromDDCOLORKEY(&pBody->u.in.CKey, &lpSetColorKey->ckNew);

            VBoxDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);
        }
        else
        {
            WARN(("VBoxDispVHWACommandCreate failed!"));
            lpSetColorKey->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!pDesc"));
        lpSetColorKey->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDAddAttachedSurface(PDD_ADDATTACHEDSURFACEDATA lpAddAttachedSurface)
{
    LOGF_ENTER();

    lpAddAttachedSurface->ddRVal = DD_OK;

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDBlt(PDD_BLTDATA lpBlt)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpBlt->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pSrcSurf = lpBlt->lpDDSrcSurface;
    DD_SURFACE_LOCAL *pDstSurf = lpBlt->lpDDDestSurface;
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;
    PVBOXVHWASURFDESC pDstDesc = (PVBOXVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

    if (pSrcDesc && pDstDesc)
    {
        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST  *pCmd;

        pCmd = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_SURF_BLT, sizeof(VBOXVHWACMD_SURF_BLT));
        if (pCmd)
        {
            VBOXVHWACMD_SURF_BLT RT_UNTRUSTED_VOLATILE_HOST  *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_BLT);

            pBody->u.in.offSrcSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);
            pBody->u.in.offDstSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);

            pBody->u.in.hDstSurf = pDstDesc->hHostHandle;
            VBoxDispVHWAFromRECTL(&pBody->u.in.dstRect, &lpBlt->rDest);
            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;
            VBoxDispVHWAFromRECTL(&pBody->u.in.srcRect, &lpBlt->rSrc);
            pBody->DstGuestSurfInfo = (uintptr_t)pDstDesc;
            pBody->SrcGuestSurfInfo = (uintptr_t)pSrcDesc;

            pBody->u.in.flags = VBoxDispVHWAFromDDBLTs(lpBlt->dwFlags);
            VBoxDispVHWAFromDDBLTFX(&pBody->u.in.desc, &lpBlt->bltFX);

            ASMAtomicIncU32(&pSrcDesc->cPendingBltsSrc);
            ASMAtomicIncU32(&pDstDesc->cPendingBltsDst);

            VBoxDispVHWARegionAdd(&pDstDesc->NonupdatedMemRegion, &lpBlt->rDest);
            VBoxDispVHWARegionTrySubstitute(&pDstDesc->UpdatedMemRegion, &lpBlt->rDest);

            if(pSrcDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedSrcMemValid = 1;
                VBoxDispVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                VBoxDispVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
            }

            VBoxDispVHWACommandSubmitAsynch(pDev, pCmd, VBoxDispVHWASurfBltCompletion, NULL);

            lpBlt->ddRVal = DD_OK;
        }
        else
        {
            WARN(("VBoxDispVHWACommandCreate failed!"));
            lpBlt->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pSrcDesc && pDstDesc)"));
        lpBlt->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDFlip(PDD_FLIPDATA lpFlip)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpFlip->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pCurrSurf = lpFlip->lpSurfCurr;
    DD_SURFACE_LOCAL *pTargSurf = lpFlip->lpSurfTarg;
    PVBOXVHWASURFDESC pCurrDesc = (PVBOXVHWASURFDESC) pCurrSurf->lpGbl->dwReserved1;
    PVBOXVHWASURFDESC pTargDesc = (PVBOXVHWASURFDESC) pTargSurf->lpGbl->dwReserved1;

    if (pCurrDesc && pTargDesc)
    {
        if(ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
           || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
        {
            VBoxDispVHWACommandCheckHostCmds(pDev);

            if(ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
               || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
            {
                lpFlip->ddRVal = DDERR_WASSTILLDRAWING;
                return DDHAL_DRIVER_HANDLED;
            }
        }

        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd
            = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_SURF_FLIP, sizeof(VBOXVHWACMD_SURF_FLIP));
        if (pCmd)
        {
            VBOXVHWACMD_SURF_FLIP RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);

            pBody->u.in.offCurrSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pCurrSurf->lpGbl->fpVidMem);
            pBody->u.in.offTargSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pTargSurf->lpGbl->fpVidMem);

            pBody->u.in.hTargSurf = pTargDesc->hHostHandle;
            pBody->u.in.hCurrSurf = pCurrDesc->hHostHandle;
            pBody->TargGuestSurfInfo = (uintptr_t)pTargDesc;
            pBody->CurrGuestSurfInfo = (uintptr_t)pCurrDesc;

            pTargDesc->bVisible = pCurrDesc->bVisible;
            pCurrDesc->bVisible = false;


            ASMAtomicIncU32(&pCurrDesc->cPendingFlipsCurr);
            ASMAtomicIncU32(&pTargDesc->cPendingFlipsTarg);
#ifdef DEBUG
            ASMAtomicIncU32(&pCurrDesc->cFlipsCurr);
            ASMAtomicIncU32(&pTargDesc->cFlipsTarg);
#endif

            if(pTargDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedTargMemValid = 1;
                VBoxDispVHWAFromRECTL(&pBody->u.in.xUpdatedTargMemRect, &pTargDesc->UpdatedMemRegion.Rect);
                VBoxDispVHWARegionClear(&pTargDesc->UpdatedMemRegion);
            }

            VBoxDispVHWACommandSubmitAsynch(pDev, pCmd, VBoxDispVHWASurfFlipCompletion, NULL);

            lpFlip->ddRVal = DD_OK;
        }
        else
        {
            WARN(("VBoxDispVHWACommandCreate failed!"));
            lpFlip->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pCurrDesc && pTargDesc)"));
        lpFlip->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDGetBltStatus(PDD_GETBLTSTATUSDATA lpGetBltStatus)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpGetBltStatus->lpDD->dhpdev;
    PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpGetBltStatus->lpDDSurface->lpGbl->dwReserved1;
    LOGF_ENTER();

    if(lpGetBltStatus->dwFlags == DDGBS_CANBLT)
    {
        lpGetBltStatus->ddRVal = DD_OK;
    }
    else /* DDGBS_ISBLTDONE */
    {
        if (pDesc)
        {
            if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc) || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst))
            {
                VBoxDispVHWACommandCheckHostCmds(pDev);

                if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc) || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst))
                {
                    lpGetBltStatus->ddRVal = DDERR_WASSTILLDRAWING;
                }
                else
                {
                    lpGetBltStatus->ddRVal = DD_OK;
                }
            }
            else
            {
                lpGetBltStatus->ddRVal = DD_OK;
            }
        }
        else
        {
            WARN(("!pDesc"));
            lpGetBltStatus->ddRVal = DDERR_GENERIC;
        }
    }


    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDGetFlipStatus(PDD_GETFLIPSTATUSDATA lpGetFlipStatus)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpGetFlipStatus->lpDD->dhpdev;
    PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpGetFlipStatus->lpDDSurface->lpGbl->dwReserved1;
    LOGF_ENTER();

    /*can't flip is there's a flip pending, so result is same for DDGFS_CANFLIP/DDGFS_ISFLIPDONE */

    if (pDesc)
    {
        if(ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr))
        {
            VBoxDispVHWACommandCheckHostCmds(pDev);

            if(ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr))
            {
                lpGetFlipStatus->ddRVal = DDERR_WASSTILLDRAWING;
            }
            else
            {
                lpGetFlipStatus->ddRVal = DD_OK;
            }
        }
        else
        {
            lpGetFlipStatus->ddRVal = DD_OK;
        }
    }
    else
    {
        WARN(("!pDesc"));
        lpGetFlipStatus->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDSetOverlayPosition(PDD_SETOVERLAYPOSITIONDATA lpSetOverlayPosition)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpSetOverlayPosition->lpDD->dhpdev;
    DD_SURFACE_LOCAL *pSrcSurf = lpSetOverlayPosition->lpDDSrcSurface;
    DD_SURFACE_LOCAL *pDstSurf = lpSetOverlayPosition->lpDDDestSurface;
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;
    PVBOXVHWASURFDESC pDstDesc = (PVBOXVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

    LOGF_ENTER();

    if (pSrcDesc && pDstDesc)
    {
        if (!pSrcDesc->bVisible)
        {
            WARN(("!pSrcDesc->bVisible"));
            lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
            return DDHAL_DRIVER_HANDLED;
        }

        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd
            = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION, sizeof(VBOXVHWACMD_SURF_OVERLAY_SETPOSITION));
        if (pCmd)
        {
            VBOXVHWACMD_SURF_OVERLAY_SETPOSITION RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_SETPOSITION);

            pBody->u.in.offSrcSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);
            pBody->u.in.offDstSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);

            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;
            pBody->u.in.hDstSurf = pDstDesc->hHostHandle;

            pBody->u.in.xPos = lpSetOverlayPosition->lXPos;
            pBody->u.in.yPos = lpSetOverlayPosition->lYPos;

            VBoxDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

            lpSetOverlayPosition->ddRVal = DD_OK;
        }
        else
        {
            WARN(("VBoxDispVHWACommandCreate failed!"));
            lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pSrcDesc && pDstDesc)"));
        lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY VBoxDispDDUpdateOverlay(PDD_UPDATEOVERLAYDATA lpUpdateOverlay)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) lpUpdateOverlay->lpDD->dhpdev;
    DD_SURFACE_LOCAL* pSrcSurf = lpUpdateOverlay->lpDDSrcSurface;
    DD_SURFACE_LOCAL* pDstSurf = lpUpdateOverlay->lpDDDestSurface;
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;

    LOGF_ENTER();

    if (pSrcDesc)
    {
        VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd
            = VBoxDispVHWACommandCreate(pDev, VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));
        if (pCmd)
        {
            VBOXVHWACMD_SURF_OVERLAY_UPDATE RT_UNTRUSTED_VOLATILE_HOST *pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);

            pBody->u.in.offSrcSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);

            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;

            VBoxDispVHWAFromRECTL(&pBody->u.in.dstRect, &lpUpdateOverlay->rDest);
            VBoxDispVHWAFromRECTL(&pBody->u.in.srcRect, &lpUpdateOverlay->rSrc);

            pBody->u.in.flags = VBoxDispVHWAFromDDOVERs(lpUpdateOverlay->dwFlags);
            VBoxDispVHWAFromDDOVERLAYFX(&pBody->u.in.desc, &lpUpdateOverlay->overlayFX);

            if (lpUpdateOverlay->dwFlags & DDOVER_HIDE)
            {
                pSrcDesc->bVisible = false;
            }
            else if(lpUpdateOverlay->dwFlags & DDOVER_SHOW)
            {
                pSrcDesc->bVisible = true;
                if(pSrcDesc->UpdatedMemRegion.bValid)
                {
                    pBody->u.in.xFlags = VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT;
                    VBoxDispVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                    VBoxDispVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
                }
            }

            if(pDstSurf)
            {
                PVBOXVHWASURFDESC pDstDesc = (PVBOXVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

                if (!pDstDesc)
                {
                    WARN(("!pDstDesc"));
                    lpUpdateOverlay->ddRVal = DDERR_GENERIC;
                    return DDHAL_DRIVER_HANDLED;
                }

                pBody->u.in.hDstSurf = pDstDesc->hHostHandle;
                pBody->u.in.offDstSurface = VBoxDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);
            }

            VBoxDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

            lpUpdateOverlay->ddRVal = DD_OK;
        }
        else
        {
            WARN(("VBoxDispVHWACommandCreate failed!"));
            lpUpdateOverlay->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!pSrcDesc"));
        lpUpdateOverlay->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}
