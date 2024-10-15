/* $Id: Framebuffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSDL - Implementation of VBoxSDLFB (SDL framebuffer) class
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

#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/stream.h>
#include <iprt/env.h>

#ifdef RT_OS_OS2
# undef RT_MAX
// from <iprt/cdefs.h>
# define RT_MAX(Value1, Value2)  ((Value1) >= (Value2) ? (Value1) : (Value2))
#endif

using namespace com;

#define LOG_GROUP LOG_GROUP_GUI
#include <iprt/errcore.h>
#include <VBox/log.h>

#include "VBoxSDL.h"
#include "Framebuffer.h"
#include "Ico64x01.h"

#if defined(RT_OS_WINDOWS) || defined(RT_OS_LINUX)
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4121) /* warning C4121: 'SDL_SysWMmsg' : alignment of a member was sensitive to packing*/
# endif
# include <SDL_syswm.h>           /* for SDL_GetWMInfo() */
# ifdef _MSC_VER
#  pragma warning(pop)
# endif
#endif

#if defined(VBOX_WITH_XPCOM)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(VBoxSDLFB, IFramebuffer)
NS_DECL_CLASSINFO(VBoxSDLFB)
NS_IMPL_THREADSAFE_ISUPPORTS2_CI(VBoxSDLFBOverlay, IFramebufferOverlay, IFramebuffer)
NS_DECL_CLASSINFO(VBoxSDLFBOverlay)
#endif

static bool gfSdlInitialized = false;           /**< if SDL was initialized */
static SDL_Surface *gWMIcon = NULL;             /**< the application icon */
static RTNATIVETHREAD gSdlNativeThread = NIL_RTNATIVETHREAD; /**< the SDL thread */

//
// Constructor / destructor
//

VBoxSDLFB::VBoxSDLFB()
{
}

HRESULT VBoxSDLFB::FinalConstruct()
{
    return 0;
}

void VBoxSDLFB::FinalRelease()
{
    return;
}

/**
 * SDL framebuffer constructor. It is called from the main
 * (i.e. SDL) thread. Therefore it is safe to use SDL calls
 * here.
 * @param fFullscreen    flag whether we start in fullscreen mode
 * @param fResizable     flag whether the SDL window should be resizable
 * @param fShowSDLConfig flag whether we print out SDL settings
 * @param fKeepHostRes   flag whether we switch the host screen resolution
 *                       when switching to fullscreen or not
 * @param iFixedWidth    fixed SDL width (-1 means not set)
 * @param iFixedHeight   fixed SDL height (-1 means not set)
 */
HRESULT VBoxSDLFB::init(uint32_t uScreenId,
                     bool fFullscreen, bool fResizable, bool fShowSDLConfig,
                     bool fKeepHostRes, uint32_t u32FixedWidth,
                     uint32_t u32FixedHeight, uint32_t u32FixedBPP,
                     bool fUpdateImage)
{
    LogFlow(("VBoxSDLFB::VBoxSDLFB\n"));

    mScreenId       = uScreenId;
    mfUpdateImage   = fUpdateImage;
    mpWindow        = NULL;
    mpTexture       = NULL;
    mpRenderer      = NULL;
    mSurfVRAM       = NULL;
    mfInitialized   = false;
    mfFullscreen    = fFullscreen;
    mfKeepHostRes   = fKeepHostRes;
    mTopOffset      = 0;
    mfResizable     = fResizable;
    mfShowSDLConfig = fShowSDLConfig;
    mFixedSDLWidth  = u32FixedWidth;
    mFixedSDLHeight = u32FixedHeight;
    mFixedSDLBPP    = u32FixedBPP;
    mCenterXOffset  = 0;
    mCenterYOffset  = 0;
    /* Start with standard screen dimensions. */
    mGuestXRes      = 640;
    mGuestYRes      = 480;
    mPtrVRAM        = NULL;
    mBitsPerPixel   = 0;
    mBytesPerLine   = 0;
    mfSameSizeRequested = false;
    mfUpdates = false;

    int rc = RTCritSectInit(&mUpdateLock);
    AssertMsg(rc == VINF_SUCCESS, ("Error from RTCritSectInit!\n"));

    resizeGuest();
    mfInitialized = true;
#ifdef RT_OS_WINDOWS
    HRESULT hr = CoCreateFreeThreadedMarshaler(this, m_pUnkMarshaler.asOutParam());
    Log(("CoCreateFreeThreadedMarshaler hr %08X\n", hr)); NOREF(hr);
#endif

    rc = SDL_GetRendererInfo(mpRenderer, &mRenderInfo);
    if (RT_SUCCESS(rc))
    {
        if (fShowSDLConfig)
            RTPrintf("Render info:\n"
                     "  Name:                    %s\n"
                     "  Render flags:            0x%x\n"
                     "  SDL video driver:        %s\n",
                     mRenderInfo.name,
                     mRenderInfo.flags,
                     RTEnvGet("SDL_VIDEODRIVER"));
    }

    return rc;
}

VBoxSDLFB::~VBoxSDLFB()
{
    LogFlow(("VBoxSDLFB::~VBoxSDLFB\n"));
    if (mSurfVRAM)
    {
        SDL_FreeSurface(mSurfVRAM);
        mSurfVRAM = NULL;
    }
    RTCritSectDelete(&mUpdateLock);
}

/* static */
bool VBoxSDLFB::init(bool fShowSDLConfig)
{
    LogFlow(("VBoxSDLFB::init\n"));

    /* memorize the thread that inited us, that's the SDL thread */
    gSdlNativeThread = RTThreadNativeSelf();

#ifdef RT_OS_WINDOWS
    /* default to DirectX if nothing else set */
    if (!RTEnvExist("SDL_VIDEODRIVER"))
    {
        RTEnvSet("SDL_VIDEODRIVER", "directx");
    }
#endif
#ifdef VBOXSDL_WITH_X11
    /* On some X servers the mouse is stuck inside the bottom right corner.
     * See http://wiki.clug.org.za/wiki/QEMU_mouse_not_working */
    RTEnvSet("SDL_VIDEO_X11_DGAMOUSE", "0");
#endif

    int rc = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE);
    if (rc != 0)
    {
        RTPrintf("SDL Error: '%s'\n", SDL_GetError());
        return false;
    }
    gfSdlInitialized = true;

    RT_NOREF(fShowSDLConfig);
    return true;
}

/**
 * Terminate SDL
 *
 * @remarks must be called from the SDL thread!
 */
void VBoxSDLFB::uninit()
{
    if (gfSdlInitialized)
    {
        AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

/**
 * Returns the current framebuffer width in pixels.
 *
 * @returns COM status code
 * @param   width Address of result buffer.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(Width)(ULONG *width)
{
    LogFlow(("VBoxSDLFB::GetWidth\n"));
    if (!width)
        return E_INVALIDARG;
    *width = mGuestXRes;
    return S_OK;
}

/**
 * Returns the current framebuffer height in pixels.
 *
 * @returns COM status code
 * @param   height Address of result buffer.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(Height)(ULONG *height)
{
    LogFlow(("VBoxSDLFB::GetHeight\n"));
    if (!height)
        return E_INVALIDARG;
    *height = mGuestYRes;
    return S_OK;
}

/**
 * Return the current framebuffer color depth.
 *
 * @returns COM status code
 * @param   bitsPerPixel Address of result variable
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(BitsPerPixel)(ULONG *bitsPerPixel)
{
    LogFlow(("VBoxSDLFB::GetBitsPerPixel\n"));
    if (!bitsPerPixel)
        return E_INVALIDARG;
    /* get the information directly from the surface in use */
    Assert(mSurfVRAM);
    *bitsPerPixel = (ULONG)(mSurfVRAM ? mSurfVRAM->format->BitsPerPixel : 0);
    return S_OK;
}

/**
 * Return the current framebuffer line size in bytes.
 *
 * @returns COM status code.
 * @param   lineSize Address of result variable.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(BytesPerLine)(ULONG *bytesPerLine)
{
    LogFlow(("VBoxSDLFB::GetBytesPerLine\n"));
    if (!bytesPerLine)
        return E_INVALIDARG;
    /* get the information directly from the surface */
    Assert(mSurfVRAM);
    *bytesPerLine = (ULONG)(mSurfVRAM ? mSurfVRAM->pitch : 0);
    return S_OK;
}

STDMETHODIMP VBoxSDLFB::COMGETTER(PixelFormat) (BitmapFormat_T *pixelFormat)
{
    if (!pixelFormat)
        return E_POINTER;
    *pixelFormat = BitmapFormat_BGR;
    return S_OK;
}

/**
 * Returns by how many pixels the guest should shrink its
 * video mode height values.
 *
 * @returns COM status code.
 * @param   heightReduction Address of result variable.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(HeightReduction)(ULONG *heightReduction)
{
    if (!heightReduction)
        return E_POINTER;
    *heightReduction = 0;
    return S_OK;
}

/**
 * Returns a pointer to an alpha-blended overlay used for displaying status
 * icons above the framebuffer.
 *
 * @returns COM status code.
 * @param   aOverlay The overlay framebuffer.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(Overlay)(IFramebufferOverlay **aOverlay)
{
    if (!aOverlay)
        return E_POINTER;
    /* Not yet implemented */
    *aOverlay = 0;
    return S_OK;
}

/**
 * Returns handle of window where framebuffer context is being drawn
 *
 * @returns COM status code.
 * @param   winId Handle of associated window.
 */
STDMETHODIMP VBoxSDLFB::COMGETTER(WinId)(LONG64 *winId)
{
    if (!winId)
        return E_POINTER;

    *winId = mWinId;
    return S_OK;
}

STDMETHODIMP VBoxSDLFB::COMGETTER(Capabilities)(ComSafeArrayOut(FramebufferCapabilities_T, aCapabilities))
{
    if (ComSafeArrayOutIsNull(aCapabilities))
        return E_POINTER;

    com::SafeArray<FramebufferCapabilities_T> caps;

    if (mfUpdateImage)
    {
        caps.resize(2);
        caps[0] = FramebufferCapabilities_UpdateImage;
        caps[1] = FramebufferCapabilities_RenderCursor;
    }
    else
    {
        caps.resize(1);
        caps[0] = FramebufferCapabilities_RenderCursor;
    }

    caps.detachTo(ComSafeArrayOutArg(aCapabilities));
    return S_OK;
}

/**
 * Notify framebuffer of an update.
 *
 * @returns COM status code
 * @param   x        Update region upper left corner x value.
 * @param   y        Update region upper left corner y value.
 * @param   w        Update region width in pixels.
 * @param   h        Update region height in pixels.
 * @param   finished Address of output flag whether the update
 *                   could be fully processed in this call (which
 *                   has to return immediately) or VBox should wait
 *                   for a call to the update complete API before
 *                   continuing with display updates.
 */
STDMETHODIMP VBoxSDLFB::NotifyUpdate(ULONG x, ULONG y,
                                     ULONG w, ULONG h)
{
    /*
     * The input values are in guest screen coordinates.
     */
    LogFlow(("VBoxSDLFB::NotifyUpdate: x = %d, y = %d, w = %d, h = %d\n",
             x, y, w, h));

#if defined(VBOXSDL_WITH_X11) || defined(RT_OS_DARWIN)
    /*
     * SDL does not allow us to make this call from any other thread than
     * the main SDL thread (which initialized the video mode). So we have
     * to send an event to the main SDL thread and process it there. For
     * sake of simplicity, we encode all information in the event parameters.
     */
    SDL_Event event;
    event.type       = SDL_USEREVENT;
    event.user.code  = mScreenId;
    event.user.type  = SDL_USER_EVENT_UPDATERECT;

    SDL_Rect *pUpdateRect = (SDL_Rect *)RTMemAlloc(sizeof(SDL_Rect));
    AssertPtrReturn(pUpdateRect, E_OUTOFMEMORY);
    pUpdateRect->x = x;
    pUpdateRect->y = y;
    pUpdateRect->w = w;
    pUpdateRect->h = h;
    event.user.data1 = pUpdateRect;

    event.user.data2 = NULL;
    PushNotifyUpdateEvent(&event);
#else /* !VBOXSDL_WITH_X11 */
    update(x, y, w, h, true /* fGuestRelative */);
#endif /* !VBOXSDL_WITH_X11 */

    return S_OK;
}

STDMETHODIMP VBoxSDLFB::NotifyUpdateImage(ULONG aX,
                                          ULONG aY,
                                          ULONG aWidth,
                                          ULONG aHeight,
                                          ComSafeArrayIn(BYTE, aImage))
{
    LogFlow(("NotifyUpdateImage: %d,%d %dx%d\n", aX, aY, aWidth, aHeight));

    com::SafeArray<BYTE> image(ComSafeArrayInArg(aImage));

    /* Copy to mSurfVRAM. */
    SDL_Rect srcRect;
    SDL_Rect dstRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.w = (uint16_t)aWidth;
    srcRect.h = (uint16_t)aHeight;
    dstRect.x = (int16_t)aX;
    dstRect.y = (int16_t)aY;
    dstRect.w = (uint16_t)aWidth;
    dstRect.h = (uint16_t)aHeight;

    const uint32_t Rmask = 0x00FF0000, Gmask = 0x0000FF00, Bmask = 0x000000FF, Amask = 0;
    SDL_Surface *surfSrc = SDL_CreateRGBSurfaceFrom(image.raw(), aWidth, aHeight, 32, aWidth * 4,
                                                    Rmask, Gmask, Bmask, Amask);
    if (surfSrc)
    {
        RTCritSectEnter(&mUpdateLock);
        if (mfUpdates)
            SDL_BlitSurface(surfSrc, &srcRect, mSurfVRAM, &dstRect);
        RTCritSectLeave(&mUpdateLock);

        SDL_FreeSurface(surfSrc);
    }

    return NotifyUpdate(aX, aY, aWidth, aHeight);
}

extern ComPtr<IDisplay> gpDisplay;

STDMETHODIMP VBoxSDLFB::NotifyChange(ULONG aScreenId,
                                     ULONG aXOrigin,
                                     ULONG aYOrigin,
                                     ULONG aWidth,
                                     ULONG aHeight)
{
    LogRel(("NotifyChange: %d %d,%d %dx%d\n",
            aScreenId, aXOrigin, aYOrigin, aWidth, aHeight));

    ComPtr<IDisplaySourceBitmap> pSourceBitmap;
    if (!mfUpdateImage)
        gpDisplay->QuerySourceBitmap(aScreenId, pSourceBitmap.asOutParam());

    RTCritSectEnter(&mUpdateLock);

    /* Disable screen updates. */
    mfUpdates = false;

    if (mfUpdateImage)
    {
        mGuestXRes   = aWidth;
        mGuestYRes   = aHeight;
        mPtrVRAM     = NULL;
        mBitsPerPixel = 0;
        mBytesPerLine = 0;
    }
    else
    {
        /* Save the new bitmap. */
        mpPendingSourceBitmap = pSourceBitmap;
    }

    RTCritSectLeave(&mUpdateLock);

    SDL_Event event;
    event.type       = SDL_USEREVENT;
    event.user.type  = SDL_USER_EVENT_NOTIFYCHANGE;
    event.user.code  = mScreenId;

    PushSDLEventForSure(&event);

    RTThreadYield();

    return S_OK;
}

/**
 * Returns whether we like the given video mode.
 *
 * @returns COM status code
 * @param   width     video mode width in pixels
 * @param   height    video mode height in pixels
 * @param   bpp       video mode bit depth in bits per pixel
 * @param   supported pointer to result variable
 */
STDMETHODIMP VBoxSDLFB::VideoModeSupported(ULONG width, ULONG height, ULONG bpp, BOOL *supported)
{
    RT_NOREF(bpp);

    if (!supported)
        return E_POINTER;

    /* are constraints set? */
    if (   (   (mMaxScreenWidth != ~(uint32_t)0)
            && (width > mMaxScreenWidth))
        || (   (mMaxScreenHeight != ~(uint32_t)0)
            && (height > mMaxScreenHeight)))
    {
        /* nope, we don't want that (but still don't freak out if it is set) */
#ifdef DEBUG
        RTPrintf("VBoxSDL::VideoModeSupported: we refused mode %dx%dx%d\n", width, height, bpp);
#endif
        *supported = false;
    }
    else
    {
        /* anything will do */
        *supported = true;
    }
    return S_OK;
}

STDMETHODIMP VBoxSDLFB::GetVisibleRegion(BYTE *aRectangles, ULONG aCount,
                                         ULONG *aCountCopied)
{
    PRTRECT rects = (PRTRECT)aRectangles;

    if (!rects)
        return E_POINTER;

    /// @todo

    NOREF(aCount);
    NOREF(aCountCopied);

    return S_OK;
}

STDMETHODIMP VBoxSDLFB::SetVisibleRegion(BYTE *aRectangles, ULONG aCount)
{
    PRTRECT rects = (PRTRECT)aRectangles;

    if (!rects)
        return E_POINTER;

    /// @todo

    NOREF(aCount);

    return S_OK;
}

STDMETHODIMP VBoxSDLFB::ProcessVHWACommand(BYTE *pCommand, LONG enmCmd, BOOL fGuestCmd)
{
    RT_NOREF(pCommand, enmCmd, fGuestCmd);
    return E_NOTIMPL;
}

STDMETHODIMP VBoxSDLFB::Notify3DEvent(ULONG uType, ComSafeArrayIn(BYTE, aData))
{
    RT_NOREF(uType); ComSafeArrayNoRef(aData);
    return E_NOTIMPL;
}

//
// Internal public methods
//

/* This method runs on the main SDL thread. */
void VBoxSDLFB::notifyChange(ULONG aScreenId)
{
    /* Disable screen updates. */
    RTCritSectEnter(&mUpdateLock);

    if (!mfUpdateImage && mpPendingSourceBitmap.isNull())
    {
        /* Do nothing. Change event already processed. */
        RTCritSectLeave(&mUpdateLock);
        return;
    }

    /* Release the current bitmap and keep the pending one. */
    mpSourceBitmap = mpPendingSourceBitmap;
    mpPendingSourceBitmap.setNull();

    RTCritSectLeave(&mUpdateLock);

    if (mpSourceBitmap.isNull())
    {
        mPtrVRAM      = NULL;
        mBitsPerPixel = 32;
        mBytesPerLine = mGuestXRes * 4;
    }
    else
    {
        BYTE *pAddress = NULL;
        ULONG ulWidth = 0;
        ULONG ulHeight = 0;
        ULONG ulBitsPerPixel = 0;
        ULONG ulBytesPerLine = 0;
        BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

        mpSourceBitmap->QueryBitmapInfo(&pAddress,
                                        &ulWidth,
                                        &ulHeight,
                                        &ulBitsPerPixel,
                                        &ulBytesPerLine,
                                        &bitmapFormat);

        if (   mGuestXRes    == ulWidth
            && mGuestYRes    == ulHeight
            && mBitsPerPixel == ulBitsPerPixel
            && mBytesPerLine == ulBytesPerLine
            && mPtrVRAM == pAddress
           )
        {
            mfSameSizeRequested = true;
        }
        else
        {
            mfSameSizeRequested = false;
        }

        mGuestXRes   = ulWidth;
        mGuestYRes   = ulHeight;
        mPtrVRAM     = pAddress;
        mBitsPerPixel = ulBitsPerPixel;
        mBytesPerLine = ulBytesPerLine;
    }

    resizeGuest();

    gpDisplay->InvalidateAndUpdateScreen(aScreenId);
}

/**
 * Method that does the actual resize of the guest framebuffer and
 * then changes the SDL framebuffer setup.
 */
void VBoxSDLFB::resizeGuest()
{
    LogFlowFunc (("mGuestXRes: %d, mGuestYRes: %d\n", mGuestXRes, mGuestYRes));
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(),
              ("Wrong thread! SDL is not threadsafe!\n"));

    RTCritSectEnter(&mUpdateLock);

    const uint32_t Rmask = 0x00FF0000, Gmask = 0x0000FF00, Bmask = 0x000000FF, Amask = 0;

    /* first free the current surface */
    if (mSurfVRAM)
    {
        SDL_FreeSurface(mSurfVRAM);
        mSurfVRAM = NULL;
    }

    if (mPtrVRAM)
    {
        /* Create a source surface from the source bitmap. */
        mSurfVRAM = SDL_CreateRGBSurfaceFrom(mPtrVRAM, mGuestXRes, mGuestYRes, mBitsPerPixel,
                                             mBytesPerLine, Rmask, Gmask, Bmask, Amask);
        LogFlow(("VBoxSDL:: using the source bitmap\n"));
    }
    else
    {
        mSurfVRAM = SDL_CreateRGBSurface(SDL_SWSURFACE, mGuestXRes, mGuestYRes, 32,
                                         Rmask, Gmask, Bmask, Amask);
        LogFlow(("VBoxSDL:: using SDL_SWSURFACE\n"));
    }
    LogFlow(("VBoxSDL:: created VRAM surface %p\n", mSurfVRAM));

    if (mfSameSizeRequested)
    {
        mfSameSizeRequested = false;
        LogFlow(("VBoxSDL:: the same resolution requested, skipping the resize.\n"));
    }
    else
    {
        /* now adjust the SDL resolution */
        resizeSDL();
    }

    /* Enable screen updates. */
    mfUpdates = true;

    RTCritSectLeave(&mUpdateLock);

    repaint();
}

/**
 * Sets SDL video mode. This is independent from guest video
 * mode changes.
 *
 * @remarks Must be called from the SDL thread!
 */
void VBoxSDLFB::resizeSDL(void)
{
    LogFlow(("VBoxSDL:resizeSDL\n"));

    const int cDisplays = SDL_GetNumVideoDisplays();
    if (cDisplays > 0)
    {
        for (int d = 0; d < cDisplays; d++)
        {
            const int cDisplayModes = SDL_GetNumDisplayModes(d);
            for (int m = 0; m < cDisplayModes; m++)
            {
                SDL_DisplayMode mode = { SDL_PIXELFORMAT_UNKNOWN, 0, 0, 0, 0 };
                if (SDL_GetDisplayMode(d, m, &mode) != 0)
                {
                    RTPrintf("Display #%d, mode %d:\t\t%i bpp\t%i x %i",
                             SDL_BITSPERPIXEL(mode.format), mode.w, mode.h);
                }

                if (m == 0)
                {
                    /*
                     * according to the SDL documentation, the API guarantees that
                     * the modes are sorted from larger to smaller, so we just
                     * take the first entry as the maximum.
                     */
                    mMaxScreenWidth  = mode.w;
                    mMaxScreenHeight = mode.h;
                }

                /* Keep going. */
            }
        }
    }
    else
        AssertFailed(); /** @todo */

    uint32_t newWidth;
    uint32_t newHeight;

    /* reset the centering offsets */
    mCenterXOffset = 0;
    mCenterYOffset = 0;

    /* we either have a fixed SDL resolution or we take the guest's */
    if (mFixedSDLWidth != ~(uint32_t)0)
    {
        newWidth  = mFixedSDLWidth;
        newHeight = mFixedSDLHeight;
    }
    else
    {
        newWidth  = RT_MIN(mGuestXRes, mMaxScreenWidth);
        newHeight = RT_MIN(mGuestYRes, mMaxScreenHeight);
    }

    /* we don't have any extra space by default */
    mTopOffset = 0;

    int sdlWindowFlags = SDL_WINDOW_SHOWN;
    if (mfResizable)
        sdlWindowFlags |= SDL_WINDOW_RESIZABLE;
    if (!mpWindow)
    {
        SDL_DisplayMode desktop_mode;
        int x = 40 + mScreenId * 20;
        int y = 40 + mScreenId * 15;

        SDL_GetDesktopDisplayMode(mScreenId, &desktop_mode);
        /* create new window */

        char szTitle[64];
        RTStrPrintf(szTitle, sizeof(szTitle), "SDL window %d", mScreenId);
        mpWindow = SDL_CreateWindow(szTitle, x, y,
                                   newWidth, newHeight, sdlWindowFlags);
        mpRenderer = SDL_CreateRenderer(mpWindow, -1, 0 /* SDL_RendererFlags */);
        if (mpRenderer)
        {
            SDL_GetRendererInfo(mpRenderer, &mRenderInfo);

            mpTexture = SDL_CreateTexture(mpRenderer, desktop_mode.format,
                                          SDL_TEXTUREACCESS_STREAMING, newWidth, newHeight);
            if (!mpTexture)
                AssertReleaseFailed();
        }
        else
            AssertReleaseFailed();

        if (12320 == g_cbIco64x01)
        {
            gWMIcon = SDL_CreateRGBSurface(0 /* Flags, must be 0 */, 64, 64, 24, 0xff, 0xff00, 0xff0000, 0);
            /** @todo make it as simple as possible. No PNM interpreter here... */
            if (gWMIcon)
            {
                memcpy(gWMIcon->pixels, g_abIco64x01+32, g_cbIco64x01-32);
                SDL_SetWindowIcon(mpWindow, gWMIcon);
            }
        }
    }
    else
    {
        int w, h;
        uint32_t format;
        int access;

        /* resize current window */
        SDL_GetWindowSize(mpWindow, &w, &h);

        if (w != (int)newWidth || h != (int)newHeight)
            SDL_SetWindowSize(mpWindow, newWidth, newHeight);

        SDL_QueryTexture(mpTexture, &format, &access, &w, &h);
        SDL_DestroyTexture(mpTexture);
        mpTexture = SDL_CreateTexture(mpRenderer, format, access, newWidth, newHeight);
        if (!mpTexture)
            AssertReleaseFailed();
    }
}

/**
 * Update specified framebuffer area. The coordinates can either be
 * relative to the guest framebuffer or relative to the screen.
 *
 * @remarks Must be called from the SDL thread on Linux!
 * @param   x              left column
 * @param   y              top row
 * @param   w              width in pixels
 * @param   h              height in pixels
 * @param   fGuestRelative flag whether the above values are guest relative or screen relative;
 */
void VBoxSDLFB::update(int x, int y, int w, int h, bool fGuestRelative)
{
#if defined(VBOXSDL_WITH_X11) || defined(RT_OS_DARWIN)
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
#endif
    RTCritSectEnter(&mUpdateLock);
    Log3Func(("mfUpdates=%RTbool %d,%d %dx%d\n", mfUpdates, x, y, w, h));
    if (!mfUpdates)
    {
        RTCritSectLeave(&mUpdateLock);
        return;
    }

    Assert(mSurfVRAM);
    if (!mSurfVRAM)
    {
        RTCritSectLeave(&mUpdateLock);
        return;
    }

    /* this is how many pixels we have to cut off from the height for this specific blit */
    int const yCutoffGuest = 0;

    /**
     * If we get a SDL window relative update, we
     * just perform a full screen update to keep things simple.
     *
     * @todo improve
     */
    if (!fGuestRelative)
    {
        x = 0;
        w = mGuestXRes;
        y = 0;
        h = mGuestYRes;
    }

    SDL_Rect srcRect;
    srcRect.x = x;
    srcRect.y = y + yCutoffGuest;
    srcRect.w = w;
    srcRect.h = RT_MAX(0, h - yCutoffGuest);

    /*
     * Destination rectangle is just offset by the label height.
     * There are two cases though: label height is added to the
     * guest resolution (mTopOffset == mLabelHeight; yCutoffGuest == 0)
     * or the label cuts off a portion of the guest screen (mTopOffset == 0;
     * yCutoffGuest >= 0)
     */
    SDL_Rect dstRect;
    dstRect.x = x + mCenterXOffset;
    dstRect.y = y + yCutoffGuest + mTopOffset + mCenterYOffset;
    dstRect.w = w;
    dstRect.h = RT_MAX(0, h - yCutoffGuest);

    /* Calculate the offset within the VRAM to update the streaming texture directly. */
    uint8_t const *pbOff = (uint8_t *)mSurfVRAM->pixels
                         + (srcRect.y * mBytesPerLine) + (srcRect.x * (mBitsPerPixel / 8));
    SDL_UpdateTexture(mpTexture, &srcRect, pbOff, mSurfVRAM->pitch);
    SDL_RenderCopy(mpRenderer, mpTexture, NULL, NULL);

    RTCritSectLeave(&mUpdateLock);

    SDL_RenderPresent(mpRenderer);
}

/**
 * Repaint the whole framebuffer
 *
 * @remarks Must be called from the SDL thread!
 */
void VBoxSDLFB::repaint()
{
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
    LogFlow(("VBoxSDLFB::repaint\n"));
    int w, h;
    uint32_t format;
    int access;
    SDL_QueryTexture(mpTexture, &format, &access, &w, &h);
    update(0, 0, w, h, false /* fGuestRelative */);
}

/**
 * Toggle fullscreen mode
 *
 * @remarks Must be called from the SDL thread!
 */
void VBoxSDLFB::setFullscreen(bool fFullscreen)
{
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
    LogFlow(("VBoxSDLFB::SetFullscreen: fullscreen: %d\n", fFullscreen));
    mfFullscreen = fFullscreen;
    /* only change the SDL resolution, do not touch the guest framebuffer */
    resizeSDL();
    repaint();
}

/**
 * Return the geometry of the host. This isn't very well tested but it seems
 * to work at least on Linux hosts.
 */
void VBoxSDLFB::getFullscreenGeometry(uint32_t *width, uint32_t *height)
{
    SDL_DisplayMode dm;
    int rc = SDL_GetDesktopDisplayMode(0, &dm); /** @BUGBUG Handle multi monitor setups! */
    if (rc == 0)
    {
        *width  = dm.w;
        *height = dm.w;
    }
}

int VBoxSDLFB::setWindowTitle(const char *pcszTitle)
{
    SDL_SetWindowTitle(mpWindow, pcszTitle);

    return VINF_SUCCESS;
}
