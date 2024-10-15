/* $Id: VBoxDispVBVA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
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
#include <HGSMI.h>
#include <HGSMIChSetup.h>

#ifdef VBOX_VBVA_ADJUST_RECT
static ULONG vbvaConvertPixel(BYTE *pu8PixelFrom, int cbPixelFrom, int cbPixelTo)
{
    BYTE r, g, b;
    ULONG ulConvertedPixel = 0;

    switch (cbPixelFrom)
    {
        case 4:
        {
           switch (cbPixelTo)
           {
               case 3:
               {
                   memcpy (&ulConvertedPixel, pu8PixelFrom, 3);
               } break;

               case 2:
               {
                   ulConvertedPixel = *(ULONG *)pu8PixelFrom;

                   r = (BYTE)(ulConvertedPixel >> 16);
                   g = (BYTE)(ulConvertedPixel >> 8);
                   b = (BYTE)(ulConvertedPixel);

                   ulConvertedPixel = ((r >> 3) << 11) + ((g >> 2) << 5) + (b >> 3);
               } break;
           }
        } break;

        case 3:
        {
           switch (cbPixelTo)
           {
               case 2:
               {
                   memcpy (&ulConvertedPixel, pu8PixelFrom, 3);

                   r = (BYTE)(ulConvertedPixel >> 16);
                   g = (BYTE)(ulConvertedPixel >> 8);
                   b = (BYTE)(ulConvertedPixel);

                   ulConvertedPixel = ((r >> 3) << 11) + ((g >> 2) << 5) + (b >> 3);
               } break;
           }
        } break;
    }

    return ulConvertedPixel;
}

BOOL vbvaFindChangedRect(SURFOBJ *psoDest, SURFOBJ *psoSrc, RECTL *prclDest, POINTL *pptlSrc)
{
    int x, y;
    int fTopNonEqualFound;
    int yTopmost;
    int yBottommost;
    int cbPixelSrc;
    int cbPixelDest;
    RECTL rclDest;
    RECTL rclSrc;
    BYTE *pu8Src;
    BYTE *pu8Dest;

    if (!prclDest || !pptlSrc)
    {
        return TRUE;
    }

    LOGF(("dest %d,%d %dx%d from %d,%d",
          prclDest->left, prclDest->top, prclDest->right - prclDest->left, prclDest->bottom - prclDest->top,
          pptlSrc->x, pptlSrc->y));

    switch (psoDest->iBitmapFormat)
    {
        case BMF_16BPP: cbPixelDest = 2; break;
        case BMF_24BPP: cbPixelDest = 3; break;
        case BMF_32BPP: cbPixelDest = 4; break;
        default: cbPixelDest = 0;
    }

    switch (psoSrc->iBitmapFormat)
    {
        case BMF_16BPP: cbPixelSrc = 2; break;
        case BMF_24BPP: cbPixelSrc = 3; break;
        case BMF_32BPP: cbPixelSrc = 4; break;
        default: cbPixelSrc = 0;
    }

    if (cbPixelDest == 0 || cbPixelSrc == 0)
    {
        WARN(("unsupported pixel format src %d dst %d", psoDest->iBitmapFormat, psoSrc->iBitmapFormat));
        return TRUE;
    }

    rclDest = *prclDest;

    vrdpAdjustRect(psoDest, &rclDest);

    pptlSrc->x += rclDest.left - prclDest->left;
    pptlSrc->y += rclDest.top - prclDest->top;

    *prclDest = rclDest;

    if (   rclDest.right == rclDest.left
        || rclDest.bottom == rclDest.top)
    {
        WARN(("empty dest rect: %d-%d, %d-%d", rclDest.left, rclDest.right, rclDest.top, rclDest.bottom));
        return FALSE;
    }

    rclSrc.left   = pptlSrc->x;
    rclSrc.top    = pptlSrc->y;
    rclSrc.right  = pptlSrc->x + (rclDest.right - rclDest.left);
    rclSrc.bottom = pptlSrc->y + (rclDest.bottom - rclDest.top);
    vrdpAdjustRect (psoSrc, &rclSrc);

    if (   rclSrc.right == rclSrc.left
        || rclSrc.bottom == rclSrc.top)
    {
         prclDest->right = prclDest->left;
         prclDest->bottom = prclDest->top;

         WARN(("empty src rect: %d-%d, %d-%d", rclSrc.left, rclSrc.right, rclSrc.top, rclSrc.bottom));
         return FALSE;
    }

    Assert(pptlSrc->x == rclSrc.left);
    Assert(pptlSrc->y == rclSrc.top);

    /*
     * Compare the content of the screen surface (psoDest) with the source surface (psoSrc).
     * Update the prclDest with the rectangle that will be actually changed after
     * copying the source bits to the screen.
     */
    pu8Src = (BYTE *)psoSrc->pvScan0 + psoSrc->lDelta * pptlSrc->y + cbPixelSrc * pptlSrc->x;
    pu8Dest = (BYTE *)psoDest->pvScan0 + psoDest->lDelta * prclDest->top + cbPixelDest * prclDest->left;

    /* Use the rclDest as the bounding rectangle for the changed area. */
    rclDest.left   = prclDest->right;  /* +inf */
    rclDest.right  = prclDest->left;   /* -inf */
    rclDest.top    = prclDest->bottom; /* +inf */
    rclDest.bottom = prclDest->top;    /* -inf */

    fTopNonEqualFound = 0;
    yTopmost = prclDest->top;        /* inclusive */
    yBottommost = prclDest->top - 1; /* inclusive */

    for (y = prclDest->top; y < prclDest->bottom; y++)
    {
        int fLeftNonEqualFound = 0;

        /* Init to an empty line. */
        int xLeftmost = prclDest->left;      /* inclusive */
        int xRightmost = prclDest->left - 1; /* inclusive */

        BYTE *pu8SrcLine = pu8Src;
        BYTE *pu8DestLine = pu8Dest;

        for (x = prclDest->left; x < prclDest->right; x++)
        {
            int fEqualPixels;

            if (cbPixelSrc == cbPixelDest)
            {
                fEqualPixels = (memcmp (pu8SrcLine, pu8DestLine, cbPixelDest) == 0);
            }
            else
            {
                /* Convert larger pixel to the smaller pixel format. */
                ULONG ulConvertedPixel;
                if (cbPixelSrc > cbPixelDest)
                {
                    /* Convert the source pixel to the destination pixel format. */
                    ulConvertedPixel = vbvaConvertPixel ( /*from*/ pu8SrcLine, cbPixelSrc,
                                                          /*to*/ cbPixelDest);
                    fEqualPixels = (memcmp (&ulConvertedPixel, pu8DestLine, cbPixelDest) == 0);
                }
                else
                {
                    /* Convert the destination pixel to the source pixel format. */
                    ulConvertedPixel = vbvaConvertPixel ( /*from*/ pu8DestLine, cbPixelDest,
                                                          /*to*/ cbPixelSrc);
                    fEqualPixels = (memcmp (&ulConvertedPixel, pu8SrcLine, cbPixelSrc) == 0);
                }
            }

            if (fEqualPixels)
            {
                /* Equal pixels. */
                if (!fLeftNonEqualFound)
                {
                    xLeftmost = x;
                }
            }
            else
            {
                fLeftNonEqualFound = 1;
                xRightmost = x;
            }

            pu8SrcLine += cbPixelSrc;
            pu8DestLine += cbPixelDest;
        }

        /* min */
        if (rclDest.left > xLeftmost)
        {
            rclDest.left = xLeftmost;
        }

        /* max */
        if (rclDest.right < xRightmost)
        {
            rclDest.right = xRightmost;
        }

        if (xLeftmost > xRightmost) /* xRightmost is inclusive, so '>', not '>='. */
        {
            /* Empty line. */
            if (!fTopNonEqualFound)
            {
                yTopmost = y;
            }
        }
        else
        {
            fTopNonEqualFound = 1;
            yBottommost = y;
        }

        pu8Src += psoSrc->lDelta;
        pu8Dest += psoDest->lDelta;
    }

    /* min */
    if (rclDest.top > yTopmost)
    {
        rclDest.top = yTopmost;
    }

    /* max */
    if (rclDest.bottom < yBottommost)
    {
        rclDest.bottom = yBottommost;
    }

    /* rclDest was calculated with right-bottom inclusive.
     * The following checks and the caller require exclusive coords.
     */
    rclDest.right++;
    rclDest.bottom++;

    LOG(("new dest %d,%d %dx%d from %d,%d",
         rclDest.left, rclDest.top, rclDest.right - rclDest.left, rclDest.bottom - rclDest.top,
         pptlSrc->x, pptlSrc->y));

    /* Update the rectangle with the changed area. */
    if (   rclDest.left >= rclDest.right
        || rclDest.top >= rclDest.bottom)
    {
        /* Empty rect. */
        LOG(("empty"));
        prclDest->right = prclDest->left;
        prclDest->bottom = prclDest->top;
        return FALSE;
    }

    LOG(("not empty"));

    pptlSrc->x += rclDest.left - prclDest->left;
    pptlSrc->y += rclDest.top - prclDest->top;

    *prclDest = rclDest;

    return TRUE;
}
#endif /* VBOX_VBVA_ADJUST_RECT */

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return EngAllocMem(0, cb, MEM_ALLOC_TAG);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    EngFreeMem(pv);
}

static HGSMIENV g_hgsmiEnvDisp =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

int VBoxDispVBVAInit(PVBOXDISPDEV pDev)
{
    LOGF_ENTER();

    /* Check if HGSMI is supported and obtain necessary info */
    QUERYHGSMIRESULT info;
    int rc = VBoxDispMPQueryHGSMIInfo(pDev->hDriver, &info);
    if (RT_SUCCESS(rc))
    {
        HGSMIQUERYCALLBACKS callbacks;
        rc = VBoxDispMPQueryHGSMICallbacks(pDev->hDriver, &callbacks);
        if (RT_SUCCESS(rc))
        {
            HGSMIQUERYCPORTPROCS portProcs;
            rc = VBoxDispMPHGSMIQueryPortProcs(pDev->hDriver, &portProcs);
            if (RT_SUCCESS(rc))
            {
                pDev->hgsmi.bSupported = TRUE;

                pDev->hgsmi.mp = callbacks;
                pDev->vpAPI = portProcs;
            }
        }
    }

    if (pDev->hgsmi.bSupported)
    {
        HGSMIHANDLERENABLE HandlerReg;

        memset(&HandlerReg, 0, sizeof(HandlerReg));
        HandlerReg.u8Channel = HGSMI_CH_VBVA;
        ULONG cbReturned;
        DWORD dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_HGSMI_HANDLER_ENABLE, &HandlerReg, sizeof(HandlerReg),
                                        0, NULL, &cbReturned);
        VBOX_WARN_WINERR(dwrc);

#ifdef VBOX_WITH_VIDEOHWACCEL
        if (NO_ERROR == dwrc)
        {
            VBoxDispVHWAInit(pDev);
        }
#endif
    }

    /* Check if we have enough VRAM and update layout info.
     * 0=Framebuffer(fixed)->DDrawHeap(all left vram)->VBVABuffer(64k..cbFramebuffer)->DisplayInfo(fixed)->=EndOfVRAM
     */
    if (pDev->hgsmi.bSupported)
    {
        ULONG cbAvailable;
        VBOXDISPVRAMLAYOUT *vram = &pDev->layout;

        pDev->iDevice = info.iDevice;

        vram->cbVRAM = pDev->memInfo.VideoRamLength;

        vram->offFramebuffer = 0;
        vram->cbFramebuffer  = RT_ALIGN_32(pDev->memInfo.FrameBufferLength, 0x1000);
        cbAvailable = vram->cbVRAM - vram->cbFramebuffer;

        if (cbAvailable <= info.u32DisplayInfoSize)
        {
            pDev->hgsmi.bSupported = FALSE;
        }
        else
        {
            vram->offDisplayInfo = vram->cbVRAM - info.u32DisplayInfoSize;
            vram->cbDisplayInfo = info.u32DisplayInfoSize;
            cbAvailable -= vram->cbDisplayInfo;

            for (vram->cbVBVABuffer = vram->cbFramebuffer;
                 vram->cbVBVABuffer >= info.u32MinVBVABufferSize;
                 vram->cbVBVABuffer /= 2)
            {
                if (vram->cbVBVABuffer < cbAvailable)
                {
                    break;
                }
            }

            if (vram->cbVBVABuffer >= cbAvailable)
            {
                pDev->hgsmi.bSupported = FALSE;
            }
            else
            {
                vram->offDDrawHeap = vram->offFramebuffer + vram->cbFramebuffer;

                cbAvailable -= vram->cbVBVABuffer;
                vram->cbDDrawHeap = cbAvailable;

                vram->offVBVABuffer = vram->offDDrawHeap + vram->cbDDrawHeap;
            }
        }
    }

    /* Setup HGSMI heap in the display information area.
     * The area has some space reserved for HGSMI event flags in the beginning.
     */
    if (pDev->hgsmi.bSupported)
    {
        LOG(("offBase=%#x", info.areaDisplay.offBase));

        rc = HGSMIHeapSetup(&pDev->hgsmi.ctx.heapCtx,
                            (uint8_t *)pDev->memInfo.VideoRamBase+pDev->layout.offDisplayInfo+sizeof(HGSMIHOSTFLAGS),
                            pDev->layout.cbDisplayInfo-sizeof(HGSMIHOSTFLAGS),
                            info.areaDisplay.offBase+pDev->layout.offDisplayInfo+sizeof(HGSMIHOSTFLAGS),
                            &g_hgsmiEnvDisp);

        if (RT_SUCCESS(rc))
        {
            pDev->hgsmi.ctx.port = info.IOPortGuestCommand;
        }
        else
        {
            VBOX_WARNRC(rc);
            pDev->hgsmi.bSupported = FALSE;
        }
    }

    /* If we don't have HGSMI or doesn't have enough VRAM, setup layout without VBVA buffer and display info */
    if (!pDev->hgsmi.bSupported)
    {
        VBOXDISPVRAMLAYOUT *vram = &pDev->layout;

        pDev->iDevice = 0;

        /* Setup a layout without both the VBVA buffer and the display information. */
        vram->cbVRAM = pDev->memInfo.VideoRamLength;

        vram->offFramebuffer = 0;
        vram->cbFramebuffer  = RT_ALIGN_32(pDev->memInfo.FrameBufferLength, 0x1000);

        vram->offDDrawHeap = vram->offFramebuffer + vram->cbFramebuffer;
        vram->cbDDrawHeap  = vram->cbVRAM - vram->offDDrawHeap;

        vram->offVBVABuffer = vram->offDDrawHeap + vram->cbDDrawHeap;
        vram->cbVBVABuffer  = 0;

        vram->offDisplayInfo = vram->offVBVABuffer + vram->cbVBVABuffer;
        vram->cbDisplayInfo = 0;
    }

    /* Update buffer layout in VBVA context info */
    VBoxVBVASetupBufferContext(&pDev->vbvaCtx, pDev->layout.offVBVABuffer, pDev->layout.cbVBVABuffer);

    LOG(("\n"
         "    cbVRAM=%#X\n"
         "    offFramebuffer=%#X  cbFramebuffer=%#X\n"
         "    offDDrawHeap=%#X    cbDDrawHeap=%#X\n"
         "    offVBVABuffer=%#X   cbVBVABuffer=%#X\n"
         "    offDisplayInfo=%#X  cbDisplayInfo=%#X\n",
         pDev->layout.cbVRAM,
         pDev->layout.offFramebuffer, pDev->layout.cbFramebuffer,
         pDev->layout.offDDrawHeap, pDev->layout.cbDDrawHeap,
         pDev->layout.offVBVABuffer, pDev->layout.cbVBVABuffer,
         pDev->layout.offDisplayInfo, pDev->layout.cbDisplayInfo
       ));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

void VBoxDispVBVAHostCommandComplete(PVBOXDISPDEV pDev, VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    pDev->hgsmi.mp.pfnCompletionHandler(pDev->hgsmi.mp.hContext, pCmd);
}

void vbvaReportDirtyRect(PVBOXDISPDEV pDev, RECTL *pRectOrig)
{
    if (pDev && pRectOrig)
    {

        LOG(("dirty rect: left %d, top: %d, width: %d, height: %d",
             pRectOrig->left, pRectOrig->top,
             pRectOrig->right - pRectOrig->left, pRectOrig->bottom - pRectOrig->top));

        VBVACMDHDR hdr;
        RECTL rect;

        /* Ensure correct order. */
        if (pRectOrig->left <= pRectOrig->right)
        {
            rect.left = pRectOrig->left;
            rect.right = pRectOrig->right;
        }
        else
        {
            rect.left = pRectOrig->right;
            rect.right = pRectOrig->left;
        }

        if (pRectOrig->top <= pRectOrig->bottom)
        {
            rect.top = pRectOrig->top;
            rect.bottom = pRectOrig->bottom;
        }
        else
        {
            rect.top = pRectOrig->bottom;
            rect.bottom = pRectOrig->top;
        }

        /* Clip the rectangle. */
        rect.left   = RT_CLAMP(rect.left,   0, (LONG)pDev->mode.ulWidth);
        rect.top    = RT_CLAMP(rect.top,    0, (LONG)pDev->mode.ulHeight);
        rect.right  = RT_CLAMP(rect.right,  0, (LONG)pDev->mode.ulWidth);
        rect.bottom = RT_CLAMP(rect.bottom, 0, (LONG)pDev->mode.ulHeight);

        /* If the rectangle is empty, still report it. */
        if (rect.right < rect.left)
        {
            rect.right = rect.left;
        }
        if (rect.bottom < rect.top)
        {
            rect.bottom = rect.top;
        }

        hdr.x = (int16_t)(rect.left + pDev->orgDev.x);
        hdr.y = (int16_t)(rect.top + pDev->orgDev.y);
        hdr.w = (uint16_t)(rect.right - rect.left);
        hdr.h = (uint16_t)(rect.bottom - rect.top);

        VBoxVBVAWrite(&pDev->vbvaCtx, &pDev->hgsmi.ctx, &hdr, sizeof(hdr));
    }
}

static void vbvaReportDirtyPath(PVBOXDISPDEV pDev, PATHOBJ *ppo)
{
    RECTFX rcfxBounds;
    RECTL rclBounds;

    PATHOBJ_vGetBounds(ppo, &rcfxBounds);

    rclBounds.left   = FXTOLFLOOR(rcfxBounds.xLeft);
    rclBounds.right  = FXTOLCEILING(rcfxBounds.xRight);
    rclBounds.top    = FXTOLFLOOR(rcfxBounds.yTop);
    rclBounds.bottom = FXTOLCEILING(rcfxBounds.yBottom);

    vbvaReportDirtyRect(pDev, &rclBounds);
}

static void vbvaReportDirtyClip(PVBOXDISPDEV pDev, CLIPOBJ *pco, RECTL *prcl)
{
    if (prcl)
    {
        vbvaReportDirtyRect(pDev, prcl);
    }
    else if (pco)
    {
        vbvaReportDirtyRect(pDev, &pco->rclBounds);
    }
}

/*
 * VBVA driver functions.
 */

void vbvaDrvLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
                   LONG x1, LONG y1, LONG x2, LONG y2, RECTL *prclBounds, MIX mix)
{
    RT_NOREF(pbo, x1, y1, x2, y2, mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vbvaReportDirtyClip(pDev, pco, prclBounds);
}

void vbvaDrvStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo,
                       BRUSHOBJ  *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs, MIX mix)
{
    RT_NOREF(pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vbvaReportDirtyPath(pDev, ppo);
}

void vbvaDrvFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
                     MIX mix, FLONG flOptions)
{
    RT_NOREF(pco, pbo, pptlBrushOrg, mix, flOptions);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vbvaReportDirtyPath(pDev, ppo);
}

void vbvaDrvPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix)
{
    RT_NOREF(pbo, pptlBrushOrg, mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vbvaReportDirtyClip(pDev, pco, NULL);
}

void vbvaDrvTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo, CLIPOBJ *pco,
                    RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
                    BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix)
{
    RT_NOREF(pfo, prclExtra, pboFore, pboOpaque, pptlOrg, mix);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    vbvaReportDirtyClip(pDev, pco, prclOpaque ? prclOpaque : &pstro->rclBkGround);
}

void vbvaDrvSaveScreenBits(SURFOBJ *pso, ULONG iMode, ULONG_PTR ident, RECTL *prcl)
{
    RT_NOREF(iMode, ident);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

    Assert(iMode == SS_RESTORE || iMode == SS_SAVE);
    vbvaReportDirtyRect(pDev, prcl);
}

void vbvaDrvBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                   RECTL *prclTrg, POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush,
                   ROP4 rop4)
{
    RT_NOREF(psoSrc, psoMask, pxlo, pptlSrc, pptlMask, pbo, pptlBrush, rop4);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoTrg->dhpdev;
    vbvaReportDirtyClip(pDev, pco, prclTrg);
}

void vbvaDrvStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                       COLORADJUSTMENT *pca, POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc,
                       POINTL *pptlMask, ULONG iMode)
{
    RT_NOREF(psoSrc, psoMask, pxlo, pca, pptlHTOrg, prclSrc, pptlMask, iMode);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoDest->dhpdev;
    vbvaReportDirtyClip(pDev, pco, prclDest);
}

void vbvaDrvCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc)
{
    RT_NOREF(psoSrc, pxlo, pptlSrc);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoDest->dhpdev;
    vbvaReportDirtyClip(pDev, pco, prclDest);
}
