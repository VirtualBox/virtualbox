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

#define LOG_GROUP LOG_GROUP_GUI

#include <iprt/asm.h>
#include <iprt/stream.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <VBox/log.h>

#include "Framebuffer.h"
#include "Display.h"
#include "Ico64x01.h"

#include <SDL.h>

static bool gfSdlInitialized = false;           /**< if SDL was initialized */
static SDL_Surface *gWMIcon = NULL;             /**< the application icon */
static RTNATIVETHREAD gSdlNativeThread = NIL_RTNATIVETHREAD; /**< the SDL thread */

DECL_HIDDEN_DATA(RTSEMEVENT) g_EventSemSDLEvents;
DECL_HIDDEN_DATA(volatile int32_t) g_cNotifyUpdateEventsPending;

/**
 * Ensure that an SDL event is really enqueued. Try multiple times if necessary.
 */
int PushSDLEventForSure(SDL_Event *event)
{
    int ntries = 10;
    for (; ntries > 0; ntries--)
    {
        int rc = SDL_PushEvent(event);
        RTSemEventSignal(g_EventSemSDLEvents);
        if (rc == 1)
            return 0;
        Log(("PushSDLEventForSure: waiting for 2ms (rc = %d)\n", rc));
        RTThreadSleep(2);
    }
    LogRel(("WARNING: Failed to enqueue SDL event %d.%d!\n",
            event->type, event->type == SDL_USEREVENT ? event->user.type : 0));
    return -1;
}

#if defined(VBOXSDL_WITH_X11) || defined(RT_OS_DARWIN)
/**
 * Special SDL_PushEvent function for NotifyUpdate events. These events may occur in bursts
 * so make sure they don't flood the SDL event queue.
 */
void PushNotifyUpdateEvent(SDL_Event *event)
{
    int rc = SDL_PushEvent(event);
    bool fSuccess = (rc == 1);

    RTSemEventSignal(g_EventSemSDLEvents);
    AssertMsg(fSuccess, ("SDL_PushEvent returned SDL error\n"));
    /* A global counter is faster than SDL_PeepEvents() */
    if (fSuccess)
        ASMAtomicIncS32(&g_cNotifyUpdateEventsPending);
    /* In order to not flood the SDL event queue, yield the CPU or (if there are already many
     * events queued) even sleep */
    if (g_cNotifyUpdateEventsPending > 96)
    {
        /* Too many NotifyUpdate events, sleep for a small amount to give the main thread time
         * to handle these events. The SDL queue can hold up to 128 events. */
        Log(("PushNotifyUpdateEvent: Sleep 1ms\n"));
        RTThreadSleep(1);
    }
    else
        RTThreadYield();
}
#endif /* VBOXSDL_WITH_X11 */


//
// Constructor / destructor
//

Framebuffer::Framebuffer(Display *pDisplay, uint32_t uScreenId,
                         bool fFullscreen, bool fResizable, bool fShowSDLConfig,
                         bool fKeepHostRes, uint32_t u32FixedWidth,
                         uint32_t u32FixedHeight, uint32_t u32FixedBPP,
                         bool fUpdateImage)
{
    LogFlow(("Framebuffer::Framebuffer\n"));

    m_pDisplay      = pDisplay;
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
}


Framebuffer::~Framebuffer()
{
    LogFlow(("Framebuffer::~Framebuffer\n"));
    if (mSurfVRAM)
    {
        SDL_FreeSurface(mSurfVRAM);
        mSurfVRAM = NULL;
    }
    RTCritSectDelete(&mUpdateLock);
}

/* static */
bool Framebuffer::init(bool fShowSDLConfig)
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

    /* create SDL event semaphore */
    int vrc = RTSemEventCreate(&g_EventSemSDLEvents);
    AssertReleaseRC(vrc);

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
void Framebuffer::uninit()
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
uint32_t Framebuffer::getWidth()
{
    LogFlow(("Framebuffer::getWidth\n"));
    return mGuestXRes;
}

/**
 * Returns the current framebuffer height in pixels.
 *
 * @returns COM status code
 * @param   height Address of result buffer.
 */
uint32_t Framebuffer::getHeight()
{
    LogFlow(("Framebuffer::getHeight\n"));
    return mGuestYRes;
}

/**
 * Return the current framebuffer color depth.
 *
 * @returns COM status code
 * @param   bitsPerPixel Address of result variable
 */
uint32_t Framebuffer::getBitsPerPixel()
{
    LogFlow(("Framebuffer::getBitsPerPixel\n"));
    /* get the information directly from the surface in use */
    Assert(mSurfVRAM);
    return mSurfVRAM ? mSurfVRAM->format->BitsPerPixel : 0;
}

/**
 * Return the current framebuffer line size in bytes.
 *
 * @returns COM status code.
 * @param   lineSize Address of result variable.
 */
uint32_t Framebuffer::getBytesPerLine()
{
    LogFlow(("Framebuffer::getBytesPerLine\n"));
    /* get the information directly from the surface */
    Assert(mSurfVRAM);
    return mSurfVRAM ? mSurfVRAM->pitch : 0;
}

/**
 * Return the current framebuffer pixel data buffer.
 *
 * @returns COM status code.
 * @param   lineSize Address of result variable.
 */
uint8_t *Framebuffer::getPixelData()
{
    LogFlow(("Framebuffer::getPixelData\n"));
    /* get the information directly from the surface */
    Assert(mSurfVRAM);
    return mSurfVRAM ? (uint8_t *)mSurfVRAM->pixels : NULL;
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
int Framebuffer::notifyUpdate(uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    /*
     * The input values are in guest screen coordinates.
     */
    LogFlow(("Framebuffer::notifyUpdate: x = %d, y = %d, w = %d, h = %d\n",
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
    AssertPtrReturn(pUpdateRect, VERR_NO_MEMORY);
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

    return VINF_SUCCESS;
}


int Framebuffer::NotifyUpdateImage(uint32_t aX,
                                   uint32_t aY,
                                   uint32_t aWidth,
                                   uint32_t aHeight,
                                   void *pvImage)
{
    LogFlow(("NotifyUpdateImage: %d,%d %dx%d\n", aX, aY, aWidth, aHeight));

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
    SDL_Surface *surfSrc = SDL_CreateRGBSurfaceFrom(pvImage, aWidth, aHeight, 32, aWidth * 4,
                                                    Rmask, Gmask, Bmask, Amask);
    if (surfSrc)
    {
        RTCritSectEnter(&mUpdateLock);
        if (mfUpdates)
            SDL_BlitSurface(surfSrc, &srcRect, mSurfVRAM, &dstRect);
        RTCritSectLeave(&mUpdateLock);

        SDL_FreeSurface(surfSrc);
    }

    return notifyUpdate(aX, aY, aWidth, aHeight);
}

int Framebuffer::notifyChange(uint32_t aScreenId,
                              uint32_t aXOrigin,
                              uint32_t aYOrigin,
                              uint32_t aWidth,
                              uint32_t aHeight)
{
    LogRel(("NotifyChange: %d %d,%d %dx%d\n",
            aScreenId, aXOrigin, aYOrigin, aWidth, aHeight));

    RTCritSectEnter(&mUpdateLock);

    /* Disable screen updates. */
    mfUpdates = false;

    mGuestXRes   = aWidth;
    mGuestYRes   = aHeight;
    mPtrVRAM     = NULL;
    mBitsPerPixel = 0;
    mBytesPerLine = 0;

    RTCritSectLeave(&mUpdateLock);

    SDL_Event event;
    event.type       = SDL_USEREVENT;
    event.user.type  = SDL_USER_EVENT_NOTIFYCHANGE;
    event.user.code  = mScreenId;

    PushSDLEventForSure(&event);

    RTThreadYield();

    return VINF_SUCCESS;
}

//
// Internal public methods
//

/* This method runs on the main SDL thread. */
void Framebuffer::notifyChange(uint32_t aScreenId)
{
    /* Disable screen updates. */
    RTCritSectEnter(&mUpdateLock);

    mPtrVRAM      = NULL;
    mBitsPerPixel = 32;
    mBytesPerLine = mGuestXRes * 4;

    RTCritSectLeave(&mUpdateLock);

    resizeGuest();

    m_pDisplay->i_invalidateAndUpdateScreen(aScreenId);
}

/**
 * Method that does the actual resize of the guest framebuffer and
 * then changes the SDL framebuffer setup.
 */
void Framebuffer::resizeGuest()
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
        LogFlow(("Framebuffer:: using the source bitmap\n"));
    }
    else
    {
        mSurfVRAM = SDL_CreateRGBSurface(SDL_SWSURFACE, mGuestXRes, mGuestYRes, 32,
                                         Rmask, Gmask, Bmask, Amask);
        LogFlow(("Framebuffer:: using SDL_SWSURFACE\n"));
    }
    LogFlow(("Framebuffer:: created VRAM surface %p\n", mSurfVRAM));

    if (mfSameSizeRequested)
    {
        mfSameSizeRequested = false;
        LogFlow(("Framebuffer:: the same resolution requested, skipping the resize.\n"));
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
void Framebuffer::resizeSDL(void)
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
void Framebuffer::update(int x, int y, int w, int h, bool fGuestRelative)
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
void Framebuffer::repaint()
{
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
    LogFlow(("Framebuffer::repaint\n"));
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
void Framebuffer::setFullscreen(bool fFullscreen)
{
    AssertMsg(gSdlNativeThread == RTThreadNativeSelf(), ("Wrong thread! SDL is not threadsafe!\n"));
    LogFlow(("Framebuffer::SetFullscreen: fullscreen: %d\n", fFullscreen));
    mfFullscreen = fFullscreen;
    /* only change the SDL resolution, do not touch the guest framebuffer */
    resizeSDL();
    repaint();
}

/**
 * Return the geometry of the host. This isn't very well tested but it seems
 * to work at least on Linux hosts.
 */
void Framebuffer::getFullscreenGeometry(uint32_t *width, uint32_t *height)
{
    SDL_DisplayMode dm;
    int rc = SDL_GetDesktopDisplayMode(0, &dm); /** @BUGBUG Handle multi monitor setups! */
    if (rc == 0)
    {
        *width  = dm.w;
        *height = dm.w;
    }
}

int Framebuffer::setWindowTitle(const char *pcszTitle)
{
    SDL_SetWindowTitle(mpWindow, pcszTitle);

    return VINF_SUCCESS;
}
