/* $Id: VBoxDispDriverDDraw.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver interface functions related to DirectDraw
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

static void VBoxDispGetDDHalInfo(PVBOXDISPDEV pDev, DD_HALINFO *pHalInfo)
{
    memset(pHalInfo, 0, sizeof(DD_HALINFO));
    pHalInfo->dwSize = sizeof(DD_HALINFO);

    /* memory info */

    pHalInfo->vmiData.fpPrimary = pDev->layout.offFramebuffer;
    /*pHalInfo->vmiData.dwFlags  - unused*/
    pHalInfo->vmiData.dwDisplayWidth = pDev->mode.ulWidth;
    pHalInfo->vmiData.dwDisplayHeight = pDev->mode.ulHeight;
    pHalInfo->vmiData.lDisplayPitch = pDev->mode.lScanlineStride;

    pHalInfo->vmiData.ddpfDisplay.dwSize = sizeof(DDPIXELFORMAT);
    pHalInfo->vmiData.ddpfDisplay.dwFlags = DDPF_RGB;
    if (pDev->surface.ulFormat == BMF_8BPP)
    {
        pHalInfo->vmiData.ddpfDisplay.dwFlags |= DDPF_PALETTEINDEXED8;
    }
    pHalInfo->vmiData.ddpfDisplay.dwRGBBitCount = pDev->mode.ulBitsPerPel;
    pHalInfo->vmiData.ddpfDisplay.dwRBitMask = pDev->mode.flMaskR;
    pHalInfo->vmiData.ddpfDisplay.dwGBitMask = pDev->mode.flMaskG;
    pHalInfo->vmiData.ddpfDisplay.dwBBitMask = pDev->mode.flMaskB;

    pHalInfo->vmiData.dwOffscreenAlign = 4;
    pHalInfo->vmiData.dwTextureAlign   = 4;
    pHalInfo->vmiData.dwZBufferAlign   = 4;
    pHalInfo->vmiData.dwOverlayAlign = 4;

    pHalInfo->vmiData.pvPrimary = pDev->memInfo.FrameBufferBase;

    /* caps */

    pHalInfo->ddCaps.dwSize = sizeof(DDNTCORECAPS);
    pHalInfo->ddCaps.dwCaps2 = DDCAPS2_WIDESURFACES;
    pHalInfo->ddCaps.dwVidMemTotal = pDev->layout.cbDDrawHeap;
    pHalInfo->ddCaps.dwVidMemFree = pDev->layout.cbDDrawHeap;
    pHalInfo->ddCaps.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

}

/* Called to get supported DirectDraw caps */
BOOL APIENTRY
VBoxDispDrvGetDirectDrawInfo(DHPDEV dhpdev, DD_HALINFO *pHalInfo, DWORD *pdwNumHeaps,
                             VIDEOMEMORY *pvmList, DWORD *pdwNumFourCCCodes, DWORD *pdwFourCC)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)dhpdev;
    LOGF_ENTER();

    VBoxDispGetDDHalInfo(pDev, pHalInfo);

#ifdef VBOX_WITH_VIDEOHWACCEL
    int rc;

    if (!pvmList && !pdwFourCC) /* first call */
    {
        rc = VBoxDispVHWAInitHostInfo1(pDev);
        VBOX_WARNRC_NOBP(rc);
    }

    if (pDev->vhwa.bEnabled)
    {
        rc = VBoxDispVHWAUpdateDDHalInfo(pDev, pHalInfo);
        VBOX_WARNRC(rc);

        pDev->vhwa.bEnabled = RT_SUCCESS(rc);
    }
#else
    RT_NOREF(pdwFourCC);
#endif

    /* we could only have 1 heap, so it's not really a list */
    if (pvmList && pDev->layout.cbDDrawHeap>0)
    {
        pvmList->dwFlags = VIDMEM_ISLINEAR;
        pvmList->fpStart = pDev->layout.offDDrawHeap;
        pvmList->fpEnd   = pDev->layout.offDDrawHeap + pDev->layout.cbDDrawHeap - 1;
#ifdef VBOX_WITH_VIDEOHWACCEL
        if (pDev->vhwa.bEnabled)
        {
            pvmList->ddsCaps.dwCaps = 0;
        }
        else
#endif
        {
            pvmList->ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        }
        pvmList->ddsCapsAlt.dwCaps = 0;

    }

    /* Always report number of heaps and supported FourCC's*/
    *pdwNumHeaps = (pDev->layout.cbDDrawHeap>0) ? 1:0;

#ifndef VBOX_WITH_VIDEOHWACCEL
    *pdwNumFourCCCodes = 0;
#else
    if (pDev->vhwa.bEnabled)
    {
        *pdwNumFourCCCodes = pDev->vhwa.numFourCC;
        if (pdwFourCC && pDev->vhwa.numFourCC)
        {
            rc = VBoxDispVHWAInitHostInfo2(pDev, pdwFourCC);
            VBOX_WARNRC(rc);

            if (RT_FAILURE(rc))
            {
                *pdwNumFourCCCodes = 0;
                pDev->vhwa.numFourCC = 0;
            }
        }

        pHalInfo->GetDriverInfo = VBoxDispDDGetDriverInfo;
        pHalInfo->dwFlags |= DDHALINFO_GETDRIVERINFOSET;
    }
#endif

    LOGF_LEAVE();
    return TRUE;
}

BOOL APIENTRY
VBoxDispDrvEnableDirectDraw(DHPDEV dhpdev, DD_CALLBACKS *pCallBacks, DD_SURFACECALLBACKS *pSurfaceCallBacks,
                            DD_PALETTECALLBACKS *pPaletteCallBacks)
{
    LOGF_ENTER();

    pCallBacks->dwSize                = sizeof(DD_CALLBACKS);
    pCallBacks->CreateSurface         = VBoxDispDDCreateSurface;
    pCallBacks->CanCreateSurface      = VBoxDispDDCanCreateSurface;
    pCallBacks->MapMemory             = VBoxDispDDMapMemory;
    pCallBacks->dwFlags               = DDHAL_CB32_CREATESURFACE|DDHAL_CB32_CANCREATESURFACE|DDHAL_CB32_MAPMEMORY;

    pSurfaceCallBacks->dwSize           = sizeof(DD_SURFACECALLBACKS);
    pSurfaceCallBacks->Lock             = VBoxDispDDLock;
    pSurfaceCallBacks->Unlock           = VBoxDispDDUnlock;
    pSurfaceCallBacks->dwFlags          = DDHAL_SURFCB32_LOCK|DDHAL_SURFCB32_UNLOCK;

    pPaletteCallBacks->dwSize           = sizeof(DD_PALETTECALLBACKS);
    pPaletteCallBacks->dwFlags          = 0;

#ifdef VBOX_WITH_VIDEOHWACCEL
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)dhpdev;

    if (pDev->vhwa.bEnabled)
    {
        pSurfaceCallBacks->DestroySurface = VBoxDispDDDestroySurface;
        pSurfaceCallBacks->Flip = VBoxDispDDFlip;
        pSurfaceCallBacks->GetFlipStatus = VBoxDispDDGetFlipStatus;
        pSurfaceCallBacks->Blt = VBoxDispDDBlt;
        pSurfaceCallBacks->GetBltStatus = VBoxDispDDGetBltStatus;
        pSurfaceCallBacks->SetColorKey = VBoxDispDDSetColorKey;
        pSurfaceCallBacks->dwFlags |= DDHAL_SURFCB32_DESTROYSURFACE|
                                      DDHAL_SURFCB32_FLIP|DDHAL_SURFCB32_GETFLIPSTATUS|
                                      DDHAL_SURFCB32_BLT|DDHAL_SURFCB32_GETBLTSTATUS|
                                      DDHAL_SURFCB32_SETCOLORKEY;

        if(pDev->vhwa.caps & VBOXVHWA_CAPS_OVERLAY)
        {
            pSurfaceCallBacks->UpdateOverlay = VBoxDispDDUpdateOverlay;
            pSurfaceCallBacks->SetOverlayPosition = VBoxDispDDSetOverlayPosition;
            pSurfaceCallBacks->dwFlags |= DDHAL_SURFCB32_UPDATEOVERLAY|DDHAL_SURFCB32_SETOVERLAYPOSITION;
        }
    }
#else
    RT_NOREF(dhpdev);
#endif

    LOGF_LEAVE();
    return TRUE;
}

VOID APIENTRY VBoxDispDrvDisableDirectDraw(DHPDEV  dhpdev)
{
    RT_NOREF(dhpdev);
    LOGF_ENTER();
    LOGF_LEAVE();
    return;
}

HBITMAP APIENTRY VBoxDispDrvDeriveSurface(DD_DIRECTDRAW_GLOBAL *pDirectDraw, DD_SURFACE_LOCAL *pSurface)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pDirectDraw->dhpdev;
    LOGF_ENTER();

    if (pSurface->ddsCaps.dwCaps & DDSCAPS_NONLOCALVIDMEM)
    {
        WARN(("Can't derive surface DDSCAPS_NONLOCALVIDMEM"));
        return NULL;
    }

    if (pSurface->lpSurfMore->ddsCapsEx.dwCaps2 & DDSCAPS2_TEXTUREMANAGE)
    {
        WARN(("Can't derive surface DDSCAPS2_TEXTUREMANAGE"));
        return NULL;
    }

    if (pSurface->lpGbl->ddpfSurface.dwRGBBitCount != pDev->mode.ulBitsPerPel)
    {
        WARN(("Can't derive surface with different bpp"));
        return NULL;
    }

    Assert(pDev->surface.hSurface);

    /* Create GDI managed bitmap, which resides in our DDraw heap memory */
    HBITMAP hBitmap;
    SIZEL size;

    size.cx = pDev->mode.ulWidth;
    size.cy = pDev->mode.ulHeight;

    hBitmap = EngCreateBitmap(size, pSurface->lpGbl->lPitch, pDev->surface.ulFormat,
                              pDev->mode.lScanlineStride>0 ? BMF_TOPDOWN:0,
                              (PBYTE)pDev->memInfo.VideoRamBase + pSurface->lpGbl->fpVidMem);

    if (!hBitmap)
    {
        WARN(("EngCreateBitmap failed"));
        return 0;
    }

    if (pSurface->lpGbl->fpVidMem == 0)
    {
        /* Screen surface, mark it so it will be recognized by the driver.
         * so the driver will be called on any operations on the surface
         * (required for VBVA and VRDP).
         */
        SURFOBJ *pso;

        if (!EngAssociateSurface((HSURF)hBitmap, pDev->hDevGDI, pDev->flDrawingHooks))
        {
            WARN(("EngAssociateSurface failed"));
            EngDeleteSurface((HSURF)hBitmap);
            return NULL;
        }

        pso = EngLockSurface((HSURF)hBitmap);
        if (!pso)
        {
            WARN(("EngLockSurface failed"));
            EngDeleteSurface((HSURF)hBitmap);
            return NULL;
        }

        pso->dhpdev = (DHPDEV)pDev;
        EngUnlockSurface(pso);
    }

    LOGF_LEAVE();
    return hBitmap;
}
