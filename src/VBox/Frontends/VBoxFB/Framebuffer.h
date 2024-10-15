/* $Id: Framebuffer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxFB - Declaration of VBoxDirectFB class.
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

#ifndef VBOX_INCLUDED_SRC_VBoxFB_Framebuffer_h
#define VBOX_INCLUDED_SRC_VBoxFB_Framebuffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxFB.h"

class VBoxDirectFB : public IFramebuffer
{
public:
    VBoxDirectFB(IDirectFB *aDFB, IDirectFBSurface *aSurface);
    virtual ~VBoxDirectFB();

    NS_DECL_ISUPPORTS

    NS_IMETHOD GetWidth(PRUint32 *width);
    NS_IMETHOD GetHeight(PRUint32 *height);
    NS_IMETHOD GetBitsPerPixel(PRUint32 *bitsPerPixel);
    NS_IMETHOD GetBytesPerLine(PRUint32 *bytesPerLine);
    NS_IMETHOD GetPixelFormat(PRUint32 *pixelFormat);
    NS_IMETHOD GetHeightReduction(PRUint32 *heightReduction);
    NS_IMETHOD GetOverlay(IFramebufferOverlay **aOverlay);
    NS_IMETHOD GetWinId(PRInt64 *winId);
    NS_IMETHOD GetCapabilities(PRUint32 *pcCapabilites, FramebufferCapabilities_T **ppaenmCapabilities);

    NS_IMETHOD NotifyUpdate(PRUint32 x, PRUint32 y, PRUint32 cx, PRUint32 cy);
    NS_IMETHOD NotifyUpdateImage(PRUint32 x, PRUint32 y, PRUint32 cx, PRUint32 cy, PRUint32 cbImage, PRUint8 *pbImage);
    NS_IMETHOD NotifyChange(PRUint32 idScreen, PRUint32 xOrigin, PRUint32 yOrigin, PRUint32 cx, PRUint32 cy);
    NS_IMETHOD VideoModeSupported(PRUint32 width, PRUint32 height, PRUint32 bpp, PRBool *supported);
    NS_IMETHOD GetVisibleRegion(PRUint8 *paRectangles, PRUint32 cRectangles, PRUint32 *pcCopied);
    NS_IMETHOD SetVisibleRegion(PRUint8 *paRectangles, PRUint32 cRectangles);

    NS_IMETHOD ProcessVHWACommand(PRUint8 *pCommand, LONG enmCmd, BOOL fGuestCmd);

    NS_IMETHOD Notify3DEvent(PRUint32 type, PRUint32 cbData, PRUint8 *pbData);

    /// @todo obsolete?
    NS_IMETHOD GetAddress(PRUint8 **address);
    NS_IMETHOD Lock();
    NS_IMETHOD Unlock();
    NS_IMETHOD GetUsesGuestVRAM(PRBool *usesGuestVRAM);
    NS_IMETHOD RequestResize(PRUint32 aScreenId, PRUint32 pixelFormat, PRUint8 *vram,
                             PRUint32 bitsPerPixel, PRUint32 bytesPerLine,
                             PRUint32 w, PRUint32 h,
                             PRBool *finished);

private:
    int createSurface(uint32_t w, uint32_t h);

    IDirectFB *dfb;
    IDirectFBSurface *surface;
    uint32_t screenWidth;
    uint32_t screenHeight;
    IDirectFBSurface *fbInternalSurface;
    void *fbBufferAddress;
    uint32_t fbWidth;
    uint32_t fbHeight;
    uint32_t fbPitch;
    int fbSurfaceLocked;
};


#endif /* !VBOX_INCLUDED_SRC_VBoxFB_Framebuffer_h */

