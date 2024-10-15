/* $Id: Framebuffer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * Declaration of VBoxSDLFB (SDL framebuffer) class
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

#ifndef VBOX_INCLUDED_SRC_VBoxSDL_Framebuffer_h
#define VBOX_INCLUDED_SRC_VBoxSDL_Framebuffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxSDL.h"
#include <iprt/thread.h>

#include <iprt/critsect.h>

class VBoxSDLFBOverlay;

class ATL_NO_VTABLE VBoxSDLFB :
    public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
    VBOX_SCRIPTABLE_IMPL(IFramebuffer)
{
public:
    VBoxSDLFB();
    virtual ~VBoxSDLFB();

    HRESULT init(uint32_t uScreenId,
                 bool fFullscreen, bool fResizable, bool fShowSDLConfig,
                 bool fKeepHostRes, uint32_t u32FixedWidth,
                 uint32_t u32FixedHeight, uint32_t u32FixedBPP,
                 bool fUpdateImage);

    static bool init(bool fShowSDLConfig);
    static void uninit();

    DECLARE_NOT_AGGREGATABLE(VBoxSDLFB)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(VBoxSDLFB)
        COM_INTERFACE_ENTRY(IFramebuffer)
        COM_INTERFACE_ENTRY2(IDispatch,IFramebuffer)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.m_p)
    END_COM_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    STDMETHOD(COMGETTER(Width))(ULONG *width);
    STDMETHOD(COMGETTER(Height))(ULONG *height);
    STDMETHOD(COMGETTER(BitsPerPixel))(ULONG *bitsPerPixel);
    STDMETHOD(COMGETTER(BytesPerLine))(ULONG *bytesPerLine);
    STDMETHOD(COMGETTER(PixelFormat))(BitmapFormat_T *pixelFormat);
    STDMETHOD(COMGETTER(HeightReduction))(ULONG *heightReduction);
    STDMETHOD(COMGETTER(Overlay))(IFramebufferOverlay **aOverlay);
    STDMETHOD(COMGETTER(WinId))(LONG64 *winId);
    STDMETHOD(COMGETTER(Capabilities))(ComSafeArrayOut(FramebufferCapabilities_T, aCapabilities));

    STDMETHOD(NotifyUpdate)(ULONG x, ULONG y, ULONG w, ULONG h);
    STDMETHOD(NotifyUpdateImage)(ULONG x, ULONG y, ULONG w, ULONG h, ComSafeArrayIn(BYTE, aImage));
    STDMETHOD(NotifyChange)(ULONG aScreenId,
                            ULONG aXOrigin,
                            ULONG aYOrigin,
                            ULONG aWidth,
                            ULONG aHeight);
    STDMETHOD(VideoModeSupported)(ULONG width, ULONG height, ULONG bpp, BOOL *supported);

    STDMETHOD(GetVisibleRegion)(BYTE *aRectangles, ULONG aCount, ULONG *aCountCopied);
    STDMETHOD(SetVisibleRegion)(BYTE *aRectangles, ULONG aCount);

    STDMETHOD(ProcessVHWACommand)(BYTE *pCommand, LONG enmCmd, BOOL fGuestCmd);

    STDMETHOD(Notify3DEvent)(ULONG uType, ComSafeArrayIn(BYTE, aData));

    // internal public methods
    bool initialized() { return mfInitialized; }
    void notifyChange(ULONG aScreenId);
    void resizeGuest();
    void resizeSDL();
    void update(int x, int y, int w, int h, bool fGuestRelative);
    void repaint();
    void setFullscreen(bool fFullscreen);
    void getFullscreenGeometry(uint32_t *width, uint32_t *height);
    uint32_t getScreenId() { return mScreenId; }
    uint32_t getGuestXRes() { return mGuestXRes; }
    uint32_t getGuestYRes() { return mGuestYRes; }
    int32_t getOriginX() { return mOriginX; }
    int32_t getOriginY() { return mOriginY; }
    int32_t getXOffset() { return mCenterXOffset; }
    int32_t getYOffset() { return mCenterYOffset; }
    SDL_Window *getWindow() { return mpWindow; }
    bool hasWindow(uint32_t id) { return SDL_GetWindowID(mpWindow) == id; }
    int setWindowTitle(const char *pcszTitle);
    void setWinId(int64_t winId) { mWinId = winId; }
    void setOrigin(int32_t axOrigin, int32_t ayOrigin) { mOriginX = axOrigin; mOriginY = ayOrigin; }
    bool getFullscreen() { return mfFullscreen; }

private:

    /** the SDL window */
    SDL_Window *mpWindow;
    /** the texture */
    SDL_Texture *mpTexture;
    /** renderer */
    SDL_Renderer *mpRenderer;
    /** render info */
    SDL_RendererInfo mRenderInfo;
    /** false if constructor failed */
    bool mfInitialized;
    /** the screen number of this framebuffer */
    uint32_t mScreenId;
    /** use NotifyUpdateImage */
    bool mfUpdateImage;
    /** maximum possible screen width in pixels (~0 = no restriction) */
    uint32_t mMaxScreenWidth;
    /** maximum possible screen height in pixels (~0 = no restriction) */
    uint32_t mMaxScreenHeight;
    /** current guest screen width in pixels */
    ULONG mGuestXRes;
    /** current guest screen height in pixels */
    ULONG mGuestYRes;
    int32_t mOriginX;
    int32_t mOriginY;
    /** fixed SDL screen width (~0 = not set) */
    uint32_t mFixedSDLWidth;
    /** fixed SDL screen height (~0 = not set) */
    uint32_t mFixedSDLHeight;
    /** fixed SDL bits per pixel (~0 = not set) */
    uint32_t mFixedSDLBPP;
    /** Y offset in pixels, i.e. guest-nondrawable area at the top */
    uint32_t mTopOffset;
    /** X offset for guest screen centering */
    uint32_t mCenterXOffset;
    /** Y offset for guest screen centering */
    uint32_t mCenterYOffset;
    /** flag whether we're in fullscreen mode */
    bool  mfFullscreen;
    /** flag whether we keep the host screen resolution when switching to
     *  fullscreen or not */
    bool  mfKeepHostRes;
    /** framebuffer update semaphore */
    RTCRITSECT mUpdateLock;
    /** flag whether the SDL window should be resizable */
    bool mfResizable;
    /** flag whether we print out SDL information */
    bool mfShowSDLConfig;
    /** handle to window where framebuffer context is being drawn*/
    int64_t mWinId;
    SDL_Surface *mSurfVRAM;

    BYTE *mPtrVRAM;
    ULONG mBitsPerPixel;
    ULONG mBytesPerLine;
    BOOL mfSameSizeRequested;

    ComPtr<IDisplaySourceBitmap> mpSourceBitmap;
    ComPtr<IDisplaySourceBitmap> mpPendingSourceBitmap;
    bool mfUpdates;

#ifdef RT_OS_WINDOWS
     ComPtr<IUnknown> m_pUnkMarshaler;
#endif
};

class VBoxSDLFBOverlay :
    public IFramebufferOverlay
{
public:
    VBoxSDLFBOverlay(ULONG x, ULONG y, ULONG width, ULONG height, BOOL visible,
                     VBoxSDLFB *aParent);
    virtual ~VBoxSDLFBOverlay();

#ifdef RT_OS_WINDOWS
    STDMETHOD_(ULONG, AddRef)()
    {
        return ::InterlockedIncrement(&refcnt);
    }
    STDMETHOD_(ULONG, Release)()
    {
        long cnt = ::InterlockedDecrement(&refcnt);
        if (cnt == 0)
            delete this;
        return cnt;
    }
#endif
    VBOX_SCRIPTABLE_DISPATCH_IMPL(IFramebuffer)

    NS_DECL_ISUPPORTS

    STDMETHOD(COMGETTER(X))(ULONG *x);
    STDMETHOD(COMGETTER(Y))(ULONG *y);
    STDMETHOD(COMGETTER(Width))(ULONG *width);
    STDMETHOD(COMGETTER(Height))(ULONG *height);
    STDMETHOD(COMGETTER(Visible))(BOOL *visible);
    STDMETHOD(COMSETTER(Visible))(BOOL visible);
    STDMETHOD(COMGETTER(Alpha))(ULONG *alpha);
    STDMETHOD(COMSETTER(Alpha))(ULONG alpha);
    STDMETHOD(COMGETTER(BytesPerLine))(ULONG *bytesPerLine);

    /* These are not used, or return standard values. */
    STDMETHOD(COMGETTER(BitsPerPixel))(ULONG *bitsPerPixel);
    STDMETHOD(COMGETTER(PixelFormat))(ULONG *pixelFormat);
    STDMETHOD(COMGETTER(UsesGuestVRAM))(BOOL *usesGuestVRAM);
    STDMETHOD(COMGETTER(HeightReduction))(ULONG *heightReduction);
    STDMETHOD(COMGETTER(Overlay))(IFramebufferOverlay **aOverlay);
    STDMETHOD(COMGETTER(WinId))(LONG64 *winId);

    STDMETHOD(Lock)();
    STDMETHOD(Unlock)();
    STDMETHOD(Move)(ULONG x, ULONG y);
    STDMETHOD(NotifyUpdate)(ULONG x, ULONG y, ULONG w, ULONG h);
    STDMETHOD(RequestResize)(ULONG aScreenId, ULONG pixelFormat, ULONG vram,
                             ULONG bitsPerPixel, ULONG bytesPerLine,
                             ULONG w, ULONG h, BOOL *finished);
    STDMETHOD(VideoModeSupported)(ULONG width, ULONG height, ULONG bpp, BOOL *supported);

    // internal public methods
    HRESULT init();

private:
    /** Overlay X offset */
    ULONG mOverlayX;
    /** Overlay Y offset */
    ULONG mOverlayY;
    /** Overlay width */
    ULONG mOverlayWidth;
    /** Overlay height */
    ULONG mOverlayHeight;
    /** Whether the overlay is currently active */
    BOOL mOverlayVisible;
    /** The parent IFramebuffer */
    VBoxSDLFB *mParent;
    /** SDL surface containing the actual framebuffer bits */
    SDL_Surface *mOverlayBits;
    /** Additional SDL surface used for combining the framebuffer and the overlay */
    SDL_Surface *mBlendedBits;
#ifdef RT_OS_WINDOWS
    long refcnt;
#endif
};

#endif /* !VBOX_INCLUDED_SRC_VBoxSDL_Framebuffer_h */
