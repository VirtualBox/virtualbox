/* $Id: SvgaCmd.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaCmd_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaCmd_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SvgaHw.h"

void SvgaCmdDefineScreen(void *pvCmd, uint32_t u32Id, bool fActivate,
                         int32_t xOrigin, int32_t yOrigin, uint32_t u32Width, uint32_t u32Height,
                         bool fPrimary, uint32_t u32VRAMOffset, bool fBlank);
void SvgaCmdDestroyScreen(void *pvCmd, uint32_t u32Id);
void SvgaCmdUpdate(void *pvCmd, uint32_t u32X, uint32_t u32Y, uint32_t u32Width, uint32_t u32Height);
void SvgaCmdDefineCursor(void *pvCmd, uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                         uint32_t u32AndMaskDepth, uint32_t u32XorMaskDepth,
                         void const *pvAndMask, uint32_t cbAndMask, void const *pvXorMask, uint32_t cbXorMask);
void SvgaCmdDefineAlphaCursor(void *pvCmd, uint32_t u32HotspotX, uint32_t u32HotspotY, uint32_t u32Width, uint32_t u32Height,
                              void const *pvImage, uint32_t cbImage);
void SvgaCmdFence(void *pvCmd, uint32_t u32Fence);
void SvgaCmdDefineGMRFB(void *pvCmd, uint32_t u32Offset, uint32_t u32BytesPerLine);

void Svga3dCmdDefineContext(void *pvCmd, uint32_t u32Cid);
void Svga3dCmdDestroyContext(void *pvCmd, uint32_t u32Cid);

void Svga3dCmdPresent(void *pvCmd, uint32_t u32Sid, uint32_t u32Width, uint32_t u32Height);

void Svga3dCmdDefineSurface(void *pvCmd, uint32_t sid, GASURFCREATE const *pCreateParms,
                            GASURFSIZE const *paSizes, uint32_t cSizes);
void Svga3dCmdDestroySurface(void *pvCmd, uint32_t u32Sid);

void Svga3dCmdSurfaceDMAToFB(void *pvCmd, uint32_t u32Sid, uint32_t u32Width, uint32_t u32Height, uint32_t u32Offset);

void Svga3dCmdSurfaceDMA(void *pvCmd, SVGAGuestImage const *pGuestImage, SVGA3dSurfaceImageId const *pSurfId,
                         SVGA3dTransferType enmTransferType, uint32_t xSrc, uint32_t ySrc,
                         uint32_t xDst, uint32_t yDst, uint32_t cWidth, uint32_t cHeight);

void SvgaCmdBlitGMRFBToScreen(void *pvCmd, uint32_t idDstScreen, int32_t xSrc, int32_t ySrc,
                              int32_t xLeft, int32_t yTop, int32_t xRight, int32_t yBottom);
void SvgaCmdBlitScreenToGMRFB(void *pvCmd, uint32_t idSrcScreen, int32_t xSrc, int32_t ySrc,
                              int32_t xLeft, int32_t yTop, int32_t xRight, int32_t yBottom);

void Svga3dCmdBlitSurfaceToScreen(void *pvCmd, uint32_t sid,
                                  RECT const *pSrcRect,
                                  uint32_t idDstScreen,
                                  RECT const *pDstRect,
                                  uint32_t cDstClipRects,
                                  RECT const *paDstClipRects);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaCmd_h */
