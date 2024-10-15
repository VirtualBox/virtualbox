/* $Id: Framebuffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxFB - Implementation of the VBoxDirectFB class.
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

#include "VBoxFB.h"
#include "Framebuffer.h"

NS_IMPL_ISUPPORTS1_CI(VBoxDirectFB, IFramebuffer)
NS_DECL_CLASSINFO(VBoxDirectFB)

VBoxDirectFB::VBoxDirectFB(IDirectFB *aDFB, IDirectFBSurface *aSurface)
{
    dfb = aDFB;
    surface = aSurface;
    fbInternalSurface = NULL;
    fbBufferAddress = NULL;
    // initialize screen dimensions
    DFBCHECK(surface->GetSize(surface, (int*)&screenWidth, (int*)&screenHeight));
    fbWidth = 640;
    fbHeight = 480;
    if ((screenWidth != fbWidth) || (screenHeight != fbHeight))
    {
        createSurface(fbWidth, fbHeight);
    }
    fbSurfaceLocked = 0;
    PRUint32 bitsPerPixel;
    GetBitsPerPixel(&bitsPerPixel);
    fbPitch = fbWidth * (bitsPerPixel / 8);
}

VBoxDirectFB::~VBoxDirectFB()
{
    // free our internal surface
    if (fbInternalSurface)
    {
        DFBCHECK((DFBResult)fbInternalSurface->Release(fbInternalSurface));
        fbInternalSurface = NULL;
    }
}

NS_IMETHODIMP VBoxDirectFB::GetWidth(uint32 *width)
{
    if (!width)
        return NS_ERROR_INVALID_POINTER;
    *width = fbWidth;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetHeight(PRUint32 *height)
{
    if (!height)
        return NS_ERROR_INVALID_POINTER;
    *height = fbHeight;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::Lock()
{
    // do we have an internal framebuffer?
    if (fbInternalSurface)
    {
       if (fbSurfaceLocked)
       {
           printf("internal surface already locked!\n");
       } else
       {
           DFBCHECK(fbInternalSurface->Lock(fbInternalSurface,
                                            (DFBSurfaceLockFlags)(DSLF_WRITE | DSLF_READ),
                                            &fbBufferAddress, (int*)&fbPitch));
           fbSurfaceLocked = 1;
       }
    } else
    {
        if (fbSurfaceLocked)
        {
            printf("surface already locked!\n");
        } else
        {
            DFBCHECK(surface->Lock(surface, (DFBSurfaceLockFlags)(DSLF_WRITE | DSLF_READ),
                                   &fbBufferAddress, (int*)&fbPitch));
            fbSurfaceLocked = 1;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::Unlock()
{
    // do we have an internal framebuffer?
    if (fbInternalSurface)
    {
        if (!fbSurfaceLocked)
        {
            printf("internal surface not locked!\n");
        } else
        {
            DFBCHECK(fbInternalSurface->Unlock(fbInternalSurface));
            fbSurfaceLocked = 0;
        }
    } else
    {
        if (!fbSurfaceLocked)
        {
            printf("surface not locked!\n");
        } else
        {
            DFBCHECK(surface->Unlock(surface));
            fbSurfaceLocked = 0;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetAddress(PRUint8 **address)
{
    if (!address)
        return NS_ERROR_INVALID_POINTER;
    *address = (PRUint8 *)fbBufferAddress;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetBitsPerPixel(PRUint32 *bitsPerPixel)
{
    if (!bitsPerPixel)
        return NS_ERROR_INVALID_POINTER;
    DFBSurfacePixelFormat pixelFormat;
    DFBCHECK(surface->GetPixelFormat(surface, &pixelFormat));
    switch (pixelFormat)
    {
        case DSPF_RGB16:
            *bitsPerPixel = 16;
            break;
        case DSPF_RGB24:
            *bitsPerPixel = 24;
            break;
        case DSPF_RGB32:
            *bitsPerPixel = 32;
            break;
        default:
            // not good! @@@AH do something!
            *bitsPerPixel = 16;
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetBytesPerLine(PRUint32 *bytesPerLine)
{
    if (!bytesPerLine)
        return NS_ERROR_INVALID_POINTER;
    *bytesPerLine = fbPitch;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetPixelFormat(BitmapFormat_T *pixelFormat)
{
    if (!pixelFormat)
        return NS_ERROR_INVALID_POINTER;
    *pixelFormat = BitmapFormat_RGBA;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetUsesGuestVRAM (PRBool *usesGuestVRAM)
{
    if (!usesGuestVRAM)
        return NS_ERROR_INVALID_POINTER;
    *usesGuestVRAM = false;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetHeightReduction(PRUint32 *heightReduction)
{
    if (!heightReduction)
        return NS_ERROR_INVALID_POINTER;
    *heightReduction = 0;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetOverlay(IFramebufferOverlay **overlay)
{
    if (!overlay)
        return NS_ERROR_INVALID_POINTER;
    /* Not yet implemented */
    *overlay = 0;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetWinId(PRInt64 *winId)
{
    if (!winId)
        return NS_ERROR_INVALID_POINTER;
    *winId = 0;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetCapabilities(PRUint32 *pcCapabilites, FramebufferCapabilities_T **ppaenmCapabilities)
{
    RT_NOREF(pcCapabilites, ppaenmCapabilities);
    AssertMsgFailed(("Not implemented"));
    return E_NOTIMPL;
}


NS_IMETHODIMP VBoxDirectFB::NotifyUpdate(PRUint32 x, PRUint32 y,
                                         PRUint32 w, PRUint32 h)
{
    // we only need to take action if we have a memory framebuffer
    if (fbInternalSurface)
    {
        //printf("blitting %u %u %u %u...\n", x, y, w, h);
        DFBRectangle blitRectangle;
        blitRectangle.x = x;
        blitRectangle.y = y;
        blitRectangle.w = w;
        blitRectangle.h = h;
        if (g_scaleGuest)
        {
            DFBRectangle hostRectangle;
            float factorX = (float)screenWidth / (float)fbWidth;
            float factorY = (float)screenHeight / (float)fbHeight;
            hostRectangle.x = (int)((float)blitRectangle.x * factorX);
            hostRectangle.y = (int)((float)blitRectangle.y * factorY);
            hostRectangle.w = (int)((float)blitRectangle.w * factorX);
            hostRectangle.h = (int)((float)blitRectangle.h * factorY);
            DFBCHECK(surface->StretchBlit(surface, fbInternalSurface,
                                          &blitRectangle, &hostRectangle));
        }
        else
        {
            DFBCHECK(surface->Blit(surface, fbInternalSurface, &blitRectangle,
                                   x + ((screenWidth - fbWidth) / 2),
                                   y + (screenHeight - fbHeight) / 2));
        }
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::NotifyUpdateImage(PRUint32 x, PRUint32 y, PRUint32 cx, PRUint32 cy, PRUint32 cbImage, PRUint8 *pbImage)
{
    RT_NOREF(x, y, cx, cy, cbImage, pbImage);
    AssertMsgFailed(("Not implemented"));
    return E_NOTIMPL;
}

NS_IMETHODIMP VBoxDirectFB::NotifyChange(PRUint32 idScreen, PRUint32 xOrigin, PRUint32 yOrigin, PRUint32 cx, PRUint32 cy)
{
    RT_NOREF(idScreen, xOrigin, yOrigin, cx, cy);
    AssertMsgFailed(("Not implemented"));
    return E_NOTIMPL;
}

NS_IMETHODIMP VBoxDirectFB::RequestResize(PRUint32 aScreenId, PRUint32 pixelFormat, PRUint8 *vram,
                                          PRUint32 bitsPerPixel, PRUint32 bytesPerLine,
                                          PRUint32 w, PRUint32 h,
                                          PRBool *finished)
{
    uint32_t needsLocking = fbSurfaceLocked;

    printf("RequestResize: aScreenId = %d, pixelFormat = %d, vram = %p, bitsPerPixel = %d, bytesPerLine = %d, w = %d, h = %d, fbSurfaceLocked = %d\n", aScreenId, pixelFormat, vram, bitsPerPixel, bytesPerLine, w, h, fbSurfaceLocked);

    // we can't work with a locked surface
    if (needsLocking)
    {
        Unlock();
    }

    // in any case we gotta free a possible internal framebuffer
    if (fbInternalSurface)
    {
        printf("freeing internal surface\n");
        fbInternalSurface->Release(fbInternalSurface);
        fbInternalSurface = NULL;
    }

    // check if we have a fixed host video mode
    if (g_useFixedVideoMode)
    {
        // does the current video mode differ from what the guest wants?
        if (screenWidth == w && screenHeight == h)
            printf("requested guest mode matches current host mode!\n");
        else
            createSurface(w, h);
    }
    else
    {
        // we adopt to the guest resolution or the next higher that is available
        int32_t bestMode = getBestVideoMode(w, h, bitsPerPixel);
        if (bestMode == -1)
        {
            // oh oh oh oh
            printf("RequestResize: no suitable mode found!\n");
            return NS_OK;
        }

        // does the mode differ from what we wanted?
        if (   g_videoModes[bestMode].width  != w
            || g_videoModes[bestMode].height != h
            || g_videoModes[bestMode].bpp    != bitsPerPixel)
        {
            printf("The mode does not fit exactly!\n");
            createSurface(w, h);
        }
        else
        {
            printf("The mode fits exactly!\n");
        }
        // switch to this mode
        DFBCHECK(dfb->SetVideoMode(dfb,
                                   g_videoModes[bestMode].width,
                                   g_videoModes[bestMode].height,
                                   g_videoModes[bestMode].bpp));
    }

    // update dimensions to the new size
    fbWidth = w;
    fbHeight = h;

    // clear the screen
    DFBCHECK(surface->Clear(surface, 0, 0, 0, 0));

    // if it was locked before the resize, obtain the lock again
    if (needsLocking)
    {
        Lock();
    }

    if (finished)
        *finished = true;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::VideoModeSupported(PRUint32 w, PRUint32 h, PRUint32 bpp, PRBool *supported)
{
    RT_NOREF(w, h, bpp);
    if (!supported)
        return NS_ERROR_INVALID_POINTER;
    *supported = true;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetVisibleRegion(PRUint8 *rectangles, PRUint32 count, PRUint32 *countCopied)
{
    RT_NOREF(count);
    PRTRECT rects = (PRTRECT)rectangles;
    if (!rects || !countCopied)
        return NS_ERROR_INVALID_POINTER;
    /** @todo */
    *countCopied = 0;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::SetVisibleRegion(PRUint8 *rectangles, PRUint32 count)
{
    RT_NOREF(count);
    PRTRECT rects = (PRTRECT)rectangles;
    if (!rects)
        return NS_ERROR_INVALID_POINTER;
    /** @todo */
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::ProcessVHWACommand(PRUint8 *command, LONG enmCmd, BOOL fGuestCmd)
{
    RT_NOREF(command, enmCmd, fGuestCmd);
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP VBoxDirectFB::Notify3DEvent(PRUint32 type, PRUint32 cbData, PRUint8 *pbData)
{
    RT_NOREF(type, cbData, pbData);
    return NS_ERROR_NOT_IMPLEMENTED;
}

int VBoxDirectFB::createSurface(uint32_t w, uint32_t h)
{
    printf("creating a new internal surface, w = %u, h = %u...\n", w, h);
    // create a surface
    DFBSurfaceDescription dsc;
    DFBSurfacePixelFormat pixelFormat;
    dsc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT);
    dsc.width = w;
    dsc.height = h;
    DFBCHECK(surface->GetPixelFormat(surface, &pixelFormat));
    dsc.pixelformat = pixelFormat;
    DFBCHECK(dfb->CreateSurface(dfb, &dsc, &fbInternalSurface));
    return 0;
}
