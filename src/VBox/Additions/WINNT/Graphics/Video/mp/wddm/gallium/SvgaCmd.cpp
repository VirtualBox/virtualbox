/* $Id: SvgaCmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA command encoders.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#include "SvgaCmd.h"

/*
 * Function names correspond to the command structure names:
 *   Structure     Function
 *   SVGAFifoCmd*  SvgaCmd*
 *   SVGA3dCmd*    Svga3dCmd*
 */

void SvgaCmdDefineScreen(void *pvCmd, uint32_t u32Id, bool fActivate,
                         int32_t xOrigin, int32_t yOrigin, uint32_t u32Width, uint32_t u32Height,
                         bool fPrimary, uint32_t u32VRAMOffset, bool fBlank)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdDefineScreen *pCommand = (SVGAFifoCmdDefineScreen *)&pu32Id[1];

    *pu32Id = SVGA_CMD_DEFINE_SCREEN;

    pCommand->screen.structSize  = sizeof(SVGAScreenObject);
    pCommand->screen.id          = u32Id;
    pCommand->screen.flags       = SVGA_SCREEN_MUST_BE_SET;
    pCommand->screen.flags      |= fPrimary  ? SVGA_SCREEN_IS_PRIMARY : 0;
    pCommand->screen.flags      |= !fActivate? SVGA_SCREEN_DEACTIVATE : 0;
    pCommand->screen.flags      |= fBlank    ? SVGA_SCREEN_BLANKING   : 0;
    pCommand->screen.size.width  = u32Width;
    pCommand->screen.size.height = u32Height;
    pCommand->screen.root.x      = xOrigin;
    pCommand->screen.root.y      = yOrigin;
    pCommand->screen.backingStore.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
    pCommand->screen.backingStore.ptr.offset = u32VRAMOffset;
    pCommand->screen.backingStore.pitch      = u32Width * 4;
    pCommand->screen.cloneCount  = 1;
}

void SvgaCmdDestroyScreen(void *pvCmd, uint32_t u32Id)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdDestroyScreen *pCommand = (SVGAFifoCmdDestroyScreen *)&pu32Id[1];

    *pu32Id = SVGA_CMD_DESTROY_SCREEN;

    pCommand->screenId = u32Id;
}

void SvgaCmdUpdate(void *pvCmd, uint32_t u32X, uint32_t u32Y, uint32_t u32Width, uint32_t u32Height)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdUpdate *pCommand = (SVGAFifoCmdUpdate *)&pu32Id[1];

    *pu32Id = SVGA_CMD_UPDATE;

    pCommand->x = u32X;
    pCommand->y = u32Y;
    pCommand->width = u32Width;
    pCommand->height = u32Height;
}

void SvgaCmdDefineCursor(void *pvCmd, uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                         uint32_t u32AndMaskDepth, uint32_t u32XorMaskDepth,
                         void const *pvAndMask, uint32_t cbAndMask, void const *pvXorMask, uint32_t cbXorMask)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdDefineCursor *pCommand = (SVGAFifoCmdDefineCursor *)&pu32Id[1];

    *pu32Id = SVGA_CMD_DEFINE_CURSOR;

    pCommand->id = 0;
    pCommand->hotspotX = u32HotspotX;
    pCommand->hotspotY = u32HotspotY;
    pCommand->width = u32Width;
    pCommand->height = u32Height;
    pCommand->andMaskDepth = u32AndMaskDepth;
    pCommand->xorMaskDepth = u32XorMaskDepth;

    uint8_t *pu8AndMask = (uint8_t *)&pCommand[1];
    memcpy(pu8AndMask, pvAndMask, cbAndMask);

    uint8_t *pu8XorMask = pu8AndMask + cbAndMask;
    memcpy(pu8XorMask, pvXorMask, cbXorMask);
}

void SvgaCmdDefineAlphaCursor(void *pvCmd, uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                              void const *pvImage, uint32_t cbImage)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdDefineAlphaCursor *pCommand = (SVGAFifoCmdDefineAlphaCursor *)&pu32Id[1];

    *pu32Id = SVGA_CMD_DEFINE_ALPHA_CURSOR;

    pCommand->id = 0;
    pCommand->hotspotX = u32HotspotX;
    pCommand->hotspotY = u32HotspotY;
    pCommand->width = u32Width;
    pCommand->height = u32Height;

    uint8_t *pu8Image = (uint8_t *)&pCommand[1];
    memcpy(pu8Image, pvImage, cbImage);
}

void SvgaCmdFence(void *pvCmd, uint32_t u32Fence)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdFence *pCommand = (SVGAFifoCmdFence *)&pu32Id[1];

    *pu32Id = SVGA_CMD_FENCE;

    pCommand->fence = u32Fence;
}

void SvgaCmdDefineGMRFB(void *pvCmd, uint32_t u32Offset, uint32_t u32BytesPerLine)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdDefineGMRFB *pCommand = (SVGAFifoCmdDefineGMRFB *)&pu32Id[1];

    *pu32Id = SVGA_CMD_DEFINE_GMRFB;

    pCommand->ptr.gmrId = SVGA_GMR_FRAMEBUFFER;
    pCommand->ptr.offset = u32Offset;
    pCommand->bytesPerLine = u32BytesPerLine;
    pCommand->format.bitsPerPixel = 32;
    pCommand->format.colorDepth = 24;
    pCommand->format.reserved = 0;
}

void Svga3dCmdDefineContext(void *pvCmd, uint32_t u32Cid)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdDefineContext *pCommand = (SVGA3dCmdDefineContext *)&pHeader[1];

    pHeader->id = SVGA_3D_CMD_CONTEXT_DEFINE;
    pHeader->size = sizeof(SVGA3dCmdDefineContext);

    pCommand->cid = u32Cid;
}

void Svga3dCmdDestroyContext(void *pvCmd, uint32_t u32Cid)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdDestroyContext *pCommand = (SVGA3dCmdDestroyContext *)&pHeader[1];

    pHeader->id = SVGA_3D_CMD_CONTEXT_DESTROY;
    pHeader->size = sizeof(SVGA3dCmdDestroyContext);

    pCommand->cid = u32Cid;
}

void Svga3dCmdDefineSurface(void *pvCmd, uint32_t sid, GASURFCREATE const *pCreateParms,
                            GASURFSIZE const *paSizes, uint32_t cSizes)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdDefineSurface *pCommand = (SVGA3dCmdDefineSurface *)&pHeader[1];

    uint32_t cbCommand = sizeof(SVGA3dCmdDefineSurface) + cSizes * sizeof(SVGA3dSize);
    pHeader->id = SVGA_3D_CMD_SURFACE_DEFINE;
    pHeader->size = cbCommand;

    pCommand->sid = sid;
    pCommand->surfaceFlags = pCreateParms->flags;
    pCommand->format = (SVGA3dSurfaceFormat)pCreateParms->format;

    unsigned i;
    for (i = 0; i < SVGA3D_MAX_SURFACE_FACES; ++i)
    {
        pCommand->face[i].numMipLevels = pCreateParms->mip_levels[i];
    }
    SVGA3dSize *paSvgaSizes = (SVGA3dSize *)&pCommand[1];
    for (i = 0; i < cSizes; ++i)
    {
        paSvgaSizes[i].width  = paSizes[i].cWidth;
        paSvgaSizes[i].height = paSizes[i].cHeight;
        paSvgaSizes[i].depth  = paSizes[i].cDepth;
    }
}

void Svga3dCmdDestroySurface(void *pvCmd, uint32_t u32Sid)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdDestroySurface *pCommand = (SVGA3dCmdDestroySurface *)&pHeader[1];

    uint32_t cbCommand = sizeof(SVGA3dCmdDestroySurface);
    pHeader->id = SVGA_3D_CMD_SURFACE_DESTROY;
    pHeader->size = cbCommand;

    pCommand->sid = u32Sid;
}

void Svga3dCmdSurfaceDMAToFB(void *pvCmd, uint32_t u32Sid, uint32_t u32Width, uint32_t u32Height, uint32_t u32Offset)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdSurfaceDMA *pCommand      = (SVGA3dCmdSurfaceDMA *)&pHeader[1];
    SVGA3dCopyBox *pCopyBox            = (SVGA3dCopyBox *)&pCommand[1];
    SVGA3dCmdSurfaceDMASuffix *pSuffix = (SVGA3dCmdSurfaceDMASuffix *)&pCopyBox[1];

    pHeader->id = SVGA_3D_CMD_SURFACE_DMA;
    pHeader->size =   sizeof(SVGA3dCmdSurfaceDMA)
                    + sizeof(SVGA3dCopyBox)
                    + sizeof(SVGA3dCmdSurfaceDMASuffix);

    pCommand->guest.ptr.gmrId = SVGA_GMR_FRAMEBUFFER;
    pCommand->guest.ptr.offset = u32Offset;
    pCommand->guest.pitch = u32Width * 4; /** @todo */
    pCommand->host.sid = u32Sid;
    pCommand->host.face = 0;
    pCommand->host.mipmap = 0;
    pCommand->transfer = SVGA3D_READ_HOST_VRAM;

    pCopyBox->x = 0;
    pCopyBox->y = 0;
    pCopyBox->z = 0;
    pCopyBox->w = u32Width;
    pCopyBox->h = u32Height;
    pCopyBox->d = 1;
    pCopyBox->srcx = 0;
    pCopyBox->srcy = 0;
    pCopyBox->srcz = 0;

    pSuffix->suffixSize = sizeof(SVGA3dCmdSurfaceDMASuffix);
    pSuffix->maximumOffset = MAX_UINT32;
    pSuffix->flags.discard = 0;
    pSuffix->flags.unsynchronized = 0;
    pSuffix->flags.reserved = 0;
}

void Svga3dCmdSurfaceDMA(void *pvCmd, SVGAGuestImage const *pGuestImage, SVGA3dSurfaceImageId const *pSurfId,
                         SVGA3dTransferType enmTransferType, uint32_t xSrc, uint32_t ySrc,
                         uint32_t xDst, uint32_t yDst, uint32_t cWidth, uint32_t cHeight)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdSurfaceDMA *pCommand      = (SVGA3dCmdSurfaceDMA *)&pHeader[1];
    SVGA3dCopyBox *pCopyBox            = (SVGA3dCopyBox *)&pCommand[1];
    SVGA3dCmdSurfaceDMASuffix *pSuffix = (SVGA3dCmdSurfaceDMASuffix *)&pCopyBox[1];

    pHeader->id = SVGA_3D_CMD_SURFACE_DMA;
    pHeader->size =   sizeof(SVGA3dCmdSurfaceDMA)
                    + sizeof(SVGA3dCopyBox)
                    + sizeof(SVGA3dCmdSurfaceDMASuffix);

    pCommand->guest    = *pGuestImage;
    pCommand->host     = *pSurfId;
    pCommand->transfer = enmTransferType;

    /* The command needs this:
     * 'define the "source" in each copyBox as the guest image and the
     * "destination" as the host image, regardless of transfer direction.'
     *
     * But the function is more convenient if src and dst coords are specificed
     * for actual source or dest.
     */
    /* Host image coords. */
    if (enmTransferType == SVGA3D_READ_HOST_VRAM) /* Surf -> GuestImage(GMR) */
    {
        pCopyBox->x = xSrc;
        pCopyBox->y = ySrc;
    }
    else
    {
        pCopyBox->x = xDst;
        pCopyBox->y = yDst;
    }
    pCopyBox->z = 0;

    pCopyBox->w = cWidth;
    pCopyBox->h = cHeight;
    pCopyBox->d = 1;

    /* Guest image coords. */
    if (enmTransferType == SVGA3D_READ_HOST_VRAM) /* Surf -> GuestImage(GMR) */
    {
        pCopyBox->srcx = xDst;
        pCopyBox->srcy = yDst;
    }
    else
    {
        pCopyBox->srcx = xSrc;
        pCopyBox->srcy = ySrc;
    }
    pCopyBox->srcz = 0;

    pSuffix->suffixSize = sizeof(SVGA3dCmdSurfaceDMASuffix);
    pSuffix->maximumOffset = MAX_UINT32;
    pSuffix->flags.discard = 0;
    pSuffix->flags.unsynchronized = 0;
    pSuffix->flags.reserved = 0;
}

void Svga3dCmdPresent(void *pvCmd, uint32_t u32Sid, uint32_t u32Width, uint32_t u32Height)
{
    SVGA3dCmdHeader *pHeader   = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdPresent *pCommand = (SVGA3dCmdPresent *)&pHeader[1];
    SVGA3dCopyRect *pCopyRect  = (SVGA3dCopyRect *)&pCommand[1];

    pHeader->id = SVGA_3D_CMD_PRESENT;
    pHeader->size =   sizeof(SVGA3dCmdPresent)
                    + sizeof(SVGA3dCopyRect);

    pCommand->sid = u32Sid;

    pCopyRect->x = 0;
    pCopyRect->y = 0;
    pCopyRect->w = u32Width;
    pCopyRect->h = u32Height;
    pCopyRect->srcx = 0;
    pCopyRect->srcy = 0;
}

void SvgaCmdBlitGMRFBToScreen(void *pvCmd, uint32_t idDstScreen, int32_t xSrc, int32_t ySrc,
                              int32_t xLeft, int32_t yTop, int32_t xRight, int32_t yBottom)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdBlitGMRFBToScreen *pCommand = (SVGAFifoCmdBlitGMRFBToScreen *)&pu32Id[1];

    *pu32Id = SVGA_CMD_BLIT_GMRFB_TO_SCREEN;

    pCommand->srcOrigin.x     = xSrc;
    pCommand->srcOrigin.y     = ySrc;
    pCommand->destRect.left   = xLeft;
    pCommand->destRect.top    = yTop;
    pCommand->destRect.right  = xRight;
    pCommand->destRect.bottom = yBottom;
    pCommand->destScreenId    = idDstScreen;
}

void SvgaCmdBlitScreenToGMRFB(void *pvCmd, uint32_t idSrcScreen, int32_t xSrc, int32_t ySrc,
                              int32_t xLeft, int32_t yTop, int32_t xRight, int32_t yBottom)
{
    uint32_t *pu32Id = (uint32_t *)pvCmd;
    SVGAFifoCmdBlitScreenToGMRFB *pCommand = (SVGAFifoCmdBlitScreenToGMRFB *)&pu32Id[1];

    *pu32Id = SVGA_CMD_BLIT_SCREEN_TO_GMRFB;

    pCommand->destOrigin.x   = xSrc;
    pCommand->destOrigin.y   = ySrc;
    pCommand->srcRect.left   = xLeft;
    pCommand->srcRect.top    = yTop;
    pCommand->srcRect.right  = xRight;
    pCommand->srcRect.bottom = yBottom;
    pCommand->srcScreenId    = idSrcScreen;
}

void Svga3dCmdBlitSurfaceToScreen(void *pvCmd, uint32_t sid,
                                  RECT const *pSrcRect,
                                  uint32_t idDstScreen,
                                  RECT const *pDstRect,
                                  uint32_t cDstClipRects,
                                  RECT const *paDstClipRects)
{
    SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pvCmd;
    SVGA3dCmdBlitSurfaceToScreen *pCommand = (SVGA3dCmdBlitSurfaceToScreen *)&pHeader[1];

    pHeader->id   = SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN;
    pHeader->size = sizeof(SVGA3dCmdBlitSurfaceToScreen) + cDstClipRects * sizeof(SVGASignedRect);

    pCommand->srcImage.sid    = sid;
    pCommand->srcImage.face   = 0;
    pCommand->srcImage.mipmap = 0;

    pCommand->srcRect.left   = pSrcRect->left;
    pCommand->srcRect.top    = pSrcRect->top;
    pCommand->srcRect.right  = pSrcRect->right;
    pCommand->srcRect.bottom = pSrcRect->bottom;

    pCommand->destScreenId    = idDstScreen;

    pCommand->destRect.left   = pDstRect->left;
    pCommand->destRect.top    = pDstRect->top;
    pCommand->destRect.right  = pDstRect->right;
    pCommand->destRect.bottom = pDstRect->bottom;

    uint32_t cRects = cDstClipRects;
    RECT const *pInRect = paDstClipRects;
    SVGASignedRect *pCmdRect = (SVGASignedRect *)&pCommand[1];
    while (cRects--)
    {
        /* "The clip rectangle coordinates are measured relative to the top-left corner of destRect." */
        pCmdRect->left   = pInRect->left   - pDstRect->left;
        pCmdRect->top    = pInRect->top    - pDstRect->top;
        pCmdRect->right  = pInRect->right  - pDstRect->left;
        pCmdRect->bottom = pInRect->bottom - pDstRect->top;

        ++pCmdRect;
        ++pInRect;
    }
}

