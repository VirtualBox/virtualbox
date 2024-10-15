/* $Id: VBoxDispMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, mouse pointer related functions
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
#include "VBoxDispMini.h"

static BOOL VBoxDispFillMonoShape(PVBOXDISPDEV pDev, SURFOBJ *psoMask)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;
    PVIDEO_POINTER_ATTRIBUTES pAttrs = pDev->pointer.pAttrs;

    LOGF_ENTER();
    Assert(pAttrs);

    srcMaskW = psoMask->sizlBitmap.cx;
    srcMaskH = psoMask->sizlBitmap.cy/2; /* psoMask contains both AND and XOR masks */

    /* truncate masks if we exceed size supported by miniport */
    pAttrs->Width = min(srcMaskW, pDev->pointer.caps.MaxWidth);
    pAttrs->Height = min(srcMaskH, pDev->pointer.caps.MaxHeight);
    pAttrs->WidthInBytes = pAttrs->Width * 4;

    /* copy AND mask */
    pSrc = (BYTE*)psoMask->pvScan0;
    pDst = pAttrs->Pixels;
    dstBytesPerLine = (pAttrs->Width+7)/8;

    for (y=0; y<pAttrs->Height; ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+(LONG)y*psoMask->lDelta, dstBytesPerLine);
    }

    /* convert XOR mask to RGB0 DIB, it start in pAttrs->Pixels should be 4bytes aligned */
    pSrc = (BYTE*)psoMask->pvScan0 + (LONG)srcMaskH*psoMask->lDelta;
    pDst = pAttrs->Pixels + RT_ALIGN_T(dstBytesPerLine*pAttrs->Height, 4, ULONG);
    dstBytesPerLine = pAttrs->Width * 4;

    for (y=0; y<pAttrs->Height; ++y)
    {
        for (x=0, bit=7; x<pAttrs->Width; ++x, --bit)
        {
            if (0xFF==bit) bit=7;

            *(ULONG*)&pDst[y*dstBytesPerLine+x*4] = (pSrc[(LONG)y*psoMask->lDelta+x/8] & RT_BIT(bit)) ? 0x00FFFFFF : 0;
        }
    }

    LOGF_LEAVE();
    return TRUE;
}

static SURFOBJ *VBoxDispConvSurfTo32BPP(PVBOXDISPDEV pDev, SURFOBJ *psoScreen, SURFOBJ *psoSrc, XLATEOBJ *pxlo, HSURF *phDstSurf)
{
    *phDstSurf = NULL;

    if (psoSrc->iType==STYPE_BITMAP && psoSrc->iBitmapFormat==BMF_32BPP)
    {
        LOG(("no convertion needed"));
        return psoSrc;
    }

    HSURF hSurfBitmap=NULL, hSurfRes=NULL;
    SURFOBJ *psoBitmap=NULL, *psoRes=NULL;

    /* Convert src surface */
    if (psoSrc->iType!=STYPE_BITMAP || (pxlo && pxlo->flXlate!=XO_TRIVIAL))
    {
        LOG(("Converting color surface to bitmap"));

        /* Convert unknown format surface to screen format bitmap */
        hSurfBitmap = (HSURF) EngCreateBitmap(psoSrc->sizlBitmap, 0, psoScreen->iBitmapFormat, BMF_TOPDOWN, NULL);
        if (!hSurfBitmap)
        {
            WARN(("EngCreateBitmap for tmp surface failed"));
            return NULL;
        }

        psoBitmap = EngLockSurface(hSurfBitmap);
        if (!psoBitmap)
        {
            WARN(("EngLockSurface for tmp surface failed"));
            EngDeleteSurface(hSurfBitmap);
            return NULL;
        }

        RECTL rclDst;
        POINTL ptlSrc;

        rclDst.left   = 0;
        rclDst.top    = 0;
        rclDst.right  = psoSrc->sizlBitmap.cx;
        rclDst.bottom = psoSrc->sizlBitmap.cy;

        ptlSrc.x = 0;
        ptlSrc.y = 0;

        if (!EngCopyBits(psoBitmap, psoSrc, NULL, pxlo, &rclDst, &ptlSrc))
        {
            WARN(("EngCopyBits failed"));
            EngUnlockSurface(psoBitmap);
            EngDeleteSurface(hSurfBitmap);
            return NULL;
        }
    }
    else
    {
        psoBitmap = psoSrc;
    }

    /* Allocate result surface */
    hSurfRes = (HSURF) EngCreateBitmap(psoSrc->sizlBitmap, 0, BMF_32BPP, BMF_TOPDOWN, NULL);
    if (!hSurfRes)
    {
        WARN(("EngCreateBitmap for res surface failed"));
        if (hSurfBitmap)
        {
            EngUnlockSurface(psoBitmap);
            EngDeleteSurface(hSurfBitmap);
        }
        return NULL;
    }

    psoRes = EngLockSurface(hSurfRes);
    if (!psoRes)
    {
        WARN(("EngLockSurface for res surface failed"));
        EngDeleteSurface(hSurfRes);
        if (hSurfBitmap)
        {
            EngUnlockSurface(psoBitmap);
            EngDeleteSurface(hSurfBitmap);
        }
        return NULL;
    }

    /* Convert known fromats src surface to 32bpp */
    PBYTE pSrc = (PBYTE) psoBitmap->pvScan0;
    PBYTE pDst = (PBYTE) psoRes->pvScan0;
    ULONG x, y;

    if (psoBitmap->iBitmapFormat==BMF_8BPP && pDev->pPalette)
    {
        LOG(("BMF_8BPP"));
        for (y=0; y<(ULONG)psoSrc->sizlBitmap.cy; ++y)
        {
            for (x=0; x<(ULONG)psoSrc->sizlBitmap.cx; ++x)
            {
                BYTE bSrc = pSrc[(LONG)y*psoBitmap->lDelta+x*1];

                pDst[(LONG)y*psoRes->lDelta+x*4+0] = pDev->pPalette[bSrc].peBlue;
                pDst[(LONG)y*psoRes->lDelta+x*4+1] = pDev->pPalette[bSrc].peGreen;
                pDst[(LONG)y*psoRes->lDelta+x*4+2] = pDev->pPalette[bSrc].peRed;
                pDst[(LONG)y*psoRes->lDelta+x*4+3] = 0;
            }
        }
    }
    else if (psoBitmap->iBitmapFormat == BMF_16BPP)
    {
        LOG(("BMF_16BPP"));
        for (y=0; y<(ULONG)psoSrc->sizlBitmap.cy; ++y)
        {
            for (x=0; x<(ULONG)psoSrc->sizlBitmap.cx; ++x)
            {
                USHORT usSrc = *(USHORT*)&pSrc[(LONG)y*psoBitmap->lDelta+x*2];

                pDst[(LONG)y*psoRes->lDelta+x*4+0] = (BYTE) (usSrc<<3);
                pDst[(LONG)y*psoRes->lDelta+x*4+1] = (BYTE) ((usSrc>>5)<<2);
                pDst[(LONG)y*psoRes->lDelta+x*4+2] = (BYTE) ((usSrc>>11)<<3);
                pDst[(LONG)y*psoRes->lDelta+x*4+3] = 0;
            }
        }
    }
    else if (psoBitmap->iBitmapFormat == BMF_24BPP)
    {
        LOG(("BMF_24BPP"));
        for (y=0; y<(ULONG)psoSrc->sizlBitmap.cy; ++y)
        {
            for (x=0; x<(ULONG)psoSrc->sizlBitmap.cx; ++x)
            {
                pDst[(LONG)y*psoRes->lDelta+x*4+0] = pSrc[(LONG)y*psoBitmap->lDelta+x*3+0];
                pDst[(LONG)y*psoRes->lDelta+x*4+1] = pSrc[(LONG)y*psoBitmap->lDelta+x*3+1];
                pDst[(LONG)y*psoRes->lDelta+x*4+2] = pSrc[(LONG)y*psoBitmap->lDelta+x*3+2];
                pDst[(LONG)y*psoRes->lDelta+x*4+3] = 0;
            }
        }
    }
    else if (psoBitmap->iBitmapFormat == BMF_32BPP)
    {
        LOG(("BMF_32BPP"));
        memcpy(psoRes->pvBits, psoBitmap->pvBits, min(psoRes->cjBits, psoBitmap->cjBits));
    }
    else
    {
        WARN(("unsupported bpp"));
        EngUnlockSurface(psoRes);
        EngDeleteSurface(hSurfRes);
        if (hSurfBitmap)
        {
            EngUnlockSurface(psoBitmap);
            EngDeleteSurface(hSurfBitmap);
        }
        return NULL;
    }

    /* cleanup tmp surface */
    if (hSurfBitmap)
    {
        EngUnlockSurface(psoBitmap);
        EngDeleteSurface(hSurfBitmap);
    }

    *phDstSurf = hSurfRes;
    return psoRes;
}

static BOOL VBoxDispFillColorShape(PVBOXDISPDEV pDev, SURFOBJ *psoScreen, SURFOBJ *psoMask, SURFOBJ *psoColor,
                                   XLATEOBJ *pxlo, FLONG fl)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;
    PVIDEO_POINTER_ATTRIBUTES pAttrs = pDev->pointer.pAttrs;
    SURFOBJ *pso32bpp = NULL;
    HSURF hSurf32bpp = NULL;

    LOGF_ENTER();
    Assert(pAttrs);

    srcMaskW = psoColor->sizlBitmap.cx;
    srcMaskH = psoColor->sizlBitmap.cy;

    /* truncate masks if we exceed size supported by miniport */
    pAttrs->Width = min(srcMaskW, pDev->pointer.caps.MaxWidth);
    pAttrs->Height = min(srcMaskH, pDev->pointer.caps.MaxHeight);
    pAttrs->WidthInBytes = pAttrs->Width * 4;

    if (fl & SPS_ALPHA)
    {
        LOG(("SPS_ALPHA"));
        /* Construct AND mask from alpha color channel */
        pSrc = (PBYTE) psoColor->pvScan0;
        pDst = pAttrs->Pixels;
        dstBytesPerLine = (pAttrs->Width+7)/8;

        memset(pDst, 0xFF, dstBytesPerLine*pAttrs->Height);

        for (y=0; y<pAttrs->Height; ++y)
        {
            for (x=0, bit=7; x<pAttrs->Width; ++x, --bit)
            {
                if (0xFF==bit) bit=7;

                if (pSrc[(LONG)y*psoColor->lDelta + x*4 + 3] > 0x7F)
                {
                    pDst[y*dstBytesPerLine + x/8] &= ~RT_BIT(bit);
                }
            }
        }

        pso32bpp = psoColor;
    }
    else
    {
        LOG(("Surface mask"));
        if (!psoMask)
        {
            WARN(("!psoMask"));
            return FALSE;
        }

        /* copy AND mask */
        pSrc = (BYTE*)psoMask->pvScan0;
        pDst = pAttrs->Pixels;
        dstBytesPerLine = (pAttrs->Width+7)/8;

        for (y=0; y<pAttrs->Height; ++y)
        {
            memcpy(pDst+y*dstBytesPerLine, pSrc+(LONG)y*psoMask->lDelta, dstBytesPerLine);
        }

        pso32bpp = VBoxDispConvSurfTo32BPP(pDev, psoScreen, psoColor, pxlo, &hSurf32bpp);
        if (!pso32bpp)
        {
            WARN(("failed to convert to 32bpp"));
            return FALSE;
        }
    }

    Assert(pso32bpp->iType==STYPE_BITMAP && pso32bpp->iBitmapFormat==BMF_32BPP);

    /* copy 32bit bitmap to XOR DIB in pAttrs->pixels, it start there should be 4bytes aligned */
    pSrc = (PBYTE) pso32bpp->pvScan0;
    pDst = pAttrs->Pixels + RT_ALIGN_T(dstBytesPerLine*pAttrs->Height, 4, ULONG);
    dstBytesPerLine = pAttrs->Width * 4;

    for (y=0; y<pAttrs->Height; ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+(LONG)y*pso32bpp->lDelta, dstBytesPerLine);
    }

    /* deallocate temp surface */
    if (hSurf32bpp)
    {
        EngUnlockSurface(pso32bpp);
        EngDeleteSurface(hSurf32bpp);
    }

    LOGF_LEAVE();
    return TRUE;
}

int VBoxDispInitPointerCaps(PVBOXDISPDEV pDev, DEVINFO *pDevInfo)
{
    int rc;

    rc = VBoxDispMPGetPointerCaps(pDev->hDriver, &pDev->pointer.caps);
    VBOX_WARNRC_RETRC(rc);

    if (pDev->pointer.caps.Flags & VIDEO_MODE_ASYNC_POINTER)
    {
        pDevInfo->flGraphicsCaps |= GCAPS_ASYNCMOVE;
    }

    pDevInfo->flGraphicsCaps2 |= GCAPS2_ALPHACURSOR;

    return VINF_SUCCESS;
}


int VBoxDispInitPointerAttrs(PVBOXDISPDEV pDev)
{
    DWORD bytesPerLine;

    /* We have no idea what bpp would have pointer glyph DIBs,
     * so make sure it's enough to fit largest one.
     */
    if (pDev->pointer.caps.Flags & VIDEO_MODE_COLOR_POINTER)
    {
        bytesPerLine = pDev->pointer.caps.MaxWidth*4;
    }
    else
    {
        bytesPerLine = (pDev->pointer.caps.MaxWidth + 7)/8;
    }

    /* VIDEO_POINTER_ATTRIBUTES followed by data and mask DIBs.*/
    pDev->pointer.cbAttrs = sizeof(VIDEO_POINTER_ATTRIBUTES) + 2*(pDev->pointer.caps.MaxHeight*bytesPerLine);

    pDev->pointer.pAttrs = (PVIDEO_POINTER_ATTRIBUTES) EngAllocMem(0, pDev->pointer.cbAttrs, MEM_ALLOC_TAG);
    if (!pDev->pointer.pAttrs)
    {
        WARN(("can't allocate %d bytes pDev->pPointerAttrs buffer", pDev->pointer.cbAttrs));
        return VERR_NO_MEMORY;
    }

    pDev->pointer.pAttrs->Flags        = pDev->pointer.caps.Flags;
    pDev->pointer.pAttrs->Width        = pDev->pointer.caps.MaxWidth;
    pDev->pointer.pAttrs->Height       = pDev->pointer.caps.MaxHeight;
    pDev->pointer.pAttrs->WidthInBytes = bytesPerLine;
    pDev->pointer.pAttrs->Enable       = 0;
    pDev->pointer.pAttrs->Column       = 0;
    pDev->pointer.pAttrs->Row          = 0;

    return VINF_SUCCESS;
}

/*
 * Display driver callbacks.
 */

VOID APIENTRY VBoxDispDrvMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    int rc;
    NOREF(prcl);
    LOGF_ENTER();

    /* For NT4 offset pointer position by display origin in virtual desktop */
    x -= pDev->orgDisp.x;
    y -= pDev->orgDisp.y;

    if (-1==x) /* hide pointer */
    {
        rc = VBoxDispMPDisablePointer(pDev->hDriver);
        VBOX_WARNRC(rc);
    }
    else
    {
        VIDEO_POINTER_POSITION pos;

        pos.Column = (SHORT) (x - pDev->pointer.orgHotSpot.x);
        pos.Row = (SHORT) (y - pDev->pointer.orgHotSpot.y);

        rc = VBoxDispMPSetPointerPosition(pDev->hDriver, &pos);
        VBOX_WARNRC(rc);
    }

    LOGF_LEAVE();
    return;
}

ULONG APIENTRY
VBoxDispDrvSetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo,
                           LONG xHot, LONG yHot, LONG x, LONG y, RECTL *prcl, FLONG fl)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    int rc;
    NOREF(prcl);
    LOGF_ENTER();

    /* sanity check */
    if (!pDev->pointer.pAttrs)
    {
        WARN(("pDev->pointer.pAttrs == NULL"));
        return SPS_ERROR;
    }

    /* Check if we've been requested to make pointer transparent */
    if (!psoMask && !(fl & SPS_ALPHA))
    {
        LOG(("SPS_ALPHA"));
        rc = VBoxDispMPDisablePointer(pDev->hDriver);
        VBOX_WARNRC(rc);
        return SPS_ACCEPT_NOEXCLUDE;
    }

    /* Fill data and mask DIBs to pass to miniport driver */
    LOG(("pso=%p, psoMask=%p, psoColor=%p, pxlo=%p, hot=%i,%i xy=%i,%i fl=%#x",
         pso, psoMask, psoColor, pxlo, xHot, yHot, x, y, fl));
    if (psoMask)
    {
        LOG(("psoMask.size = %d,%d", psoMask->sizlBitmap.cx, psoMask->sizlBitmap.cy));
    }
    if (psoColor)
    {
        LOG(("psoColor.size = %d,%d", psoColor->sizlBitmap.cx, psoColor->sizlBitmap.cy));
    }

    if (!psoColor) /* Monochrome pointer */
    {
        if (!(pDev->pointer.caps.Flags & VIDEO_MODE_MONO_POINTER)
            || !VBoxDispFillMonoShape(pDev, psoMask))
        {
            rc = VBoxDispMPDisablePointer(pDev->hDriver);
            VBOX_WARNRC(rc);
            return SPS_DECLINE;
        }
        pDev->pointer.pAttrs->Flags = VIDEO_MODE_MONO_POINTER;
    }
    else /* Color pointer */
    {
        if (!(pDev->pointer.caps.Flags & VIDEO_MODE_COLOR_POINTER)
            || !VBoxDispFillColorShape(pDev, pso, psoMask, psoColor, pxlo, fl))
        {
            rc = VBoxDispMPDisablePointer(pDev->hDriver);
            VBOX_WARNRC(rc);
            return SPS_DECLINE;
        }
        pDev->pointer.pAttrs->Flags = VIDEO_MODE_COLOR_POINTER;

    }

    /* Fill position and enable bits to pass to miniport driver.
     * Note: pDev->pointer.pAttrs->Enable is also used to pass hotspot coordinates in it's high word
     * to miniport driver.
     */
    pDev->pointer.pAttrs->Column = (SHORT) (x - xHot);
    pDev->pointer.pAttrs->Row = (SHORT) (y - yHot);

    pDev->pointer.pAttrs->Enable = VBOX_MOUSE_POINTER_SHAPE;
    pDev->pointer.pAttrs->Enable |= (yHot & 0xFF) << 24;
    pDev->pointer.pAttrs->Enable |= (xHot & 0xFF) << 16;

    if (x!=-1)
    {
        pDev->pointer.pAttrs->Enable |= VBOX_MOUSE_POINTER_VISIBLE;
    }

    if (fl & SPS_ALPHA)
    {
        pDev->pointer.pAttrs->Enable |= VBOX_MOUSE_POINTER_ALPHA;
    }

    /* Update Flags */
    if (fl & SPS_ANIMATESTART)
    {
        pDev->pointer.pAttrs->Flags |= VIDEO_MODE_ANIMATE_START;
    }
    else if (fl & SPS_ANIMATEUPDATE)
    {
        pDev->pointer.pAttrs->Flags |= VIDEO_MODE_ANIMATE_UPDATE;
    }

    if ((fl & SPS_FREQMASK) || (fl & SPS_LENGTHMASK))
    {
        WARN(("asked for mousetrail without GCAPS2_MOUSETRAILS"));
    }

    /* Pass attributes to miniport */
    rc = VBoxDispMPSetPointerAttrs(pDev);
    if (RT_FAILURE(rc))
    {
        VBOX_WARNRC(rc);
        rc = VBoxDispMPDisablePointer(pDev->hDriver);
        VBOX_WARNRC(rc);
        return SPS_DECLINE;
    }

    pDev->pointer.orgHotSpot.x = xHot;
    pDev->pointer.orgHotSpot.y = yHot;

    /* Move pointer to requested position */
    if (x!=-1)
    {
        VBoxDispDrvMovePointer(pso, x, y, NULL);
    }

    LOGF_LEAVE();
    return SPS_ACCEPT_NOEXCLUDE;
}
