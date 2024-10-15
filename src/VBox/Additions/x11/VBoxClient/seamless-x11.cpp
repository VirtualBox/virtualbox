/* $Id: seamless-x11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * X11 Seamless mode.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/vector.h>
#include <iprt/thread.h>
#include <VBox/log.h>

#include "seamless.h"
#include "seamless-x11.h"
#include "VBoxClient.h"

#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>

#include <limits.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef TESTCASE
# undef DefaultRootWindow
# define DefaultRootWindow XDefaultRootWindow
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static unsigned char *XXGetProperty(Display *aDpy, Window aWnd, Atom aPropType,
                                    const char *aPropName, unsigned long *nItems)
{
    LogRelFlowFuncEnter();
    Atom propNameAtom = XInternAtom (aDpy, aPropName,
                                     True /* only_if_exists */);
    if (propNameAtom == None)
    {
        return NULL;
    }

    Atom actTypeAtom = None;
    int actFmt = 0;
    unsigned long nBytesAfter = 0;
    unsigned char *propVal = 0;
    int rc = XGetWindowProperty (aDpy, aWnd, propNameAtom,
                                 0, LONG_MAX, False /* delete */,
                                 aPropType, &actTypeAtom, &actFmt,
                                 nItems, &nBytesAfter, &propVal);
    if (rc != Success)
        return NULL;

    LogRelFlowFuncLeave();
    return propVal;
}

int VBClX11SeamlessMonitor::init(PFNSENDREGIONUPDATE pHostCallback)
{
    int rc = VINF_SUCCESS;

    LogRelFlowFuncEnter();
    if (mHostCallback != NULL)  /* Assertion */
    {
        VBClLogError("Attempting to initialise seamless guest object twice!\n");
        return VERR_INTERNAL_ERROR;
    }
    if (!(mDisplay = XOpenDisplay(NULL)))
    {
        VBClLogError("Seamless guest object failed to acquire a connection to the display\n");
        return VERR_ACCESS_DENIED;
    }
    mHostCallback = pHostCallback;
    mEnabled = false;
    unmonitorClientList();
    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Shutdown seamless event monitoring.
 */
void VBClX11SeamlessMonitor::uninit(void)
{
    if (mHostCallback)
        stop();
    mHostCallback = NULL;

    /* Before closing a Display, make sure X11 is still running. The indicator
     * that is when XOpenDisplay() returns non NULL. If it is not a
     * case, XCloseDisplay() will hang on internal X11 mutex forever. */
    Display *pDisplay = XOpenDisplay(NULL);
    if (pDisplay)
    {
        XCloseDisplay(pDisplay);
        if (mDisplay)
        {
            XCloseDisplay(mDisplay);
            mDisplay = NULL;
        }
    }

    if (mpRects)
    {
        RTMemFree(mpRects);
        mpRects = NULL;
    }
}

/**
 * Read information about currently visible windows in the guest and subscribe to X11
 * events about changes to this information.
 *
 * @note This class does not contain its own event thread, so an external thread must
 *       call nextConfigurationEvent() for as long as events are wished.
 * @todo This function should switch the guest to fullscreen mode.
 */
int VBClX11SeamlessMonitor::start(void)
{
    int rc = VINF_SUCCESS;
    /** Dummy values for XShapeQueryExtension */
    int error, event;

    LogRelFlowFuncEnter();
    if (mEnabled)
        return VINF_SUCCESS;
    mSupportsShape = XShapeQueryExtension(mDisplay, &event, &error);
    mEnabled = true;
    monitorClientList();
    rebuildWindowTree();
    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/** Stop reporting seamless events to the host.  Free information about guest windows
    and stop requesting updates. */
void VBClX11SeamlessMonitor::stop(void)
{
    LogRelFlowFuncEnter();
    if (!mEnabled)
        return;
    mEnabled = false;
    unmonitorClientList();
    freeWindowTree();
    LogRelFlowFuncLeave();
}

void VBClX11SeamlessMonitor::monitorClientList(void)
{
    LogRelFlowFuncEnter();
    XSelectInput(mDisplay, DefaultRootWindow(mDisplay), PropertyChangeMask | SubstructureNotifyMask);
}

void VBClX11SeamlessMonitor::unmonitorClientList(void)
{
    LogRelFlowFuncEnter();
    XSelectInput(mDisplay, DefaultRootWindow(mDisplay), PropertyChangeMask);
}

/**
 * Recreate the table of toplevel windows of clients on the default root window of the
 * X server.
 */
void VBClX11SeamlessMonitor::rebuildWindowTree(void)
{
    LogRelFlowFuncEnter();
    freeWindowTree();
    addClients(DefaultRootWindow(mDisplay));
    mChanged = true;
}


/**
 * Look at the list of children of a virtual root window and add them to the list of clients
 * if they belong to a client which is not a virtual root.
 *
 * @param hRoot the virtual root window to be examined
 */
void VBClX11SeamlessMonitor::addClients(const Window hRoot)
{
    /** Unused out parameters of XQueryTree */
    Window hRealRoot, hParent;
    /** The list of children of the root supplied, raw pointer */
    Window *phChildrenRaw = NULL;
    /** The list of children of the root supplied, auto-pointer */
    Window *phChildren;
    /** The number of children of the root supplied */
    unsigned cChildren;

    LogRelFlowFuncEnter();
    if (!XQueryTree(mDisplay, hRoot, &hRealRoot, &hParent, &phChildrenRaw, &cChildren))
        return;
    phChildren = phChildrenRaw;
    for (unsigned i = 0; i < cChildren; ++i)
        addClientWindow(phChildren[i]);
    XFree(phChildrenRaw);
    LogRelFlowFuncLeave();
}


void VBClX11SeamlessMonitor::addClientWindow(const Window hWin)
{
    LogRelFlowFuncEnter();
    XWindowAttributes winAttrib;
    bool fAddWin = true;
    Window hClient = XmuClientWindow(mDisplay, hWin);

    if (isVirtualRoot(hClient))
        fAddWin = false;
    if (fAddWin && !XGetWindowAttributes(mDisplay, hWin, &winAttrib))
    {
        VBClLogError("Failed to get the window attributes for window %d\n", hWin);
        fAddWin = false;
    }
    if (fAddWin && (winAttrib.map_state == IsUnmapped))
        fAddWin = false;
    XSizeHints dummyHints;
    long dummyLong;
    /* Apparently (?) some old kwin versions had unwanted client windows
     * without normal hints. */
    if (fAddWin && (!XGetWMNormalHints(mDisplay, hClient, &dummyHints,
                                       &dummyLong)))
    {
        LogRelFlowFunc(("window %lu, client window %lu has no size hints\n", hWin, hClient));
        fAddWin = false;
    }
    if (fAddWin)
    {
        XRectangle *pRects = NULL;
        int cRects = 0, iOrdering;
        bool hasShape = false;

        LogRelFlowFunc(("adding window %lu, client window %lu\n", hWin,
                     hClient));
        if (mSupportsShape)
        {
            XShapeSelectInput(mDisplay, hWin, ShapeNotifyMask);
            pRects = XShapeGetRectangles(mDisplay, hWin, ShapeBounding, &cRects, &iOrdering);
            if (!pRects)
                cRects = 0;
            else
            {
                if (   (cRects > 1)
                    || (pRects[0].x != 0)
                    || (pRects[0].y != 0)
                    || (pRects[0].width != winAttrib.width)
                    || (pRects[0].height != winAttrib.height)
                   )
                    hasShape = true;
            }
        }
        mGuestWindows.addWindow(hWin, hasShape, winAttrib.x, winAttrib.y,
                                winAttrib.width, winAttrib.height, cRects,
                                pRects);
    }
    LogRelFlowFuncLeave();
}


/**
 * Checks whether a window is a virtual root.
 * @returns true if it is, false otherwise
 * @param hWin the window to be examined
 */
bool VBClX11SeamlessMonitor::isVirtualRoot(Window hWin)
{
    unsigned char *windowTypeRaw = NULL;
    Atom *windowType;
    unsigned long ulCount;
    bool rc = false;

    LogRelFlowFuncEnter();
    windowTypeRaw = XXGetProperty(mDisplay, hWin, XA_ATOM, WM_TYPE_PROP, &ulCount);
    if (windowTypeRaw != NULL)
    {
        windowType = (Atom *)(windowTypeRaw);
        if (   (ulCount != 0)
            && (*windowType == XInternAtom(mDisplay, WM_TYPE_DESKTOP_PROP, True)))
            rc = true;
    }
    if (windowTypeRaw)
        XFree(windowTypeRaw);
    LogRelFlowFunc(("returning %RTbool\n", rc));
    return rc;
}

DECLCALLBACK(int) VBoxGuestWinFree(VBoxGuestWinInfo *pInfo, void *pvParam)
{
    Display *pDisplay = (Display *)pvParam;

    XShapeSelectInput(pDisplay, pInfo->Core.Key, 0);
    delete pInfo;
    return VINF_SUCCESS;
}

/**
 * Free all information in the tree of visible windows
 */
void VBClX11SeamlessMonitor::freeWindowTree(void)
{
    /* We use post-increment in the operation to prevent the iterator from being invalidated. */
    LogRelFlowFuncEnter();
    mGuestWindows.detachAll(VBoxGuestWinFree, mDisplay);
    LogRelFlowFuncLeave();
}


/**
 * Waits for a position or shape-related event from guest windows
 *
 * @note Called from the guest event thread.
 */
void VBClX11SeamlessMonitor::nextConfigurationEvent(void)
{
    XEvent event;

    LogRelFlowFuncEnter();
    /* Start by sending information about the current window setup to the host.  We do this
       here because we want to send all such information from a single thread. */
    if (mChanged && mEnabled)
    {
        updateRects();
        mHostCallback(mpRects, mcRects);
    }
    mChanged = false;

    if (XPending(mDisplay) > 0)
    {
        /* We execute this even when seamless is disabled, as it also waits for
         * enable and disable notification. */
        XNextEvent(mDisplay, &event);
    } else
    {
        /* This function is called in a loop by upper layer. In order to
         * prevent CPU spinning, sleep a bit before returning. */
        RTThreadSleep(300 /* ms */);
        return;
    }

    if (!mEnabled)
        return;
    switch (event.type)
    {
    case ConfigureNotify:
        {
            XConfigureEvent *pConf = &event.xconfigure;
            LogRelFlowFunc(("configure event, window=%lu, x=%i, y=%i, w=%i, h=%i, send_event=%RTbool\n",
                           (unsigned long) pConf->window, (int) pConf->x,
                           (int) pConf->y, (int) pConf->width,
                           (int) pConf->height, pConf->send_event));
        }
        doConfigureEvent(event.xconfigure.window);
        break;
    case MapNotify:
        LogRelFlowFunc(("map event, window=%lu, send_event=%RTbool\n",
                       (unsigned long) event.xmap.window,
                       event.xmap.send_event));
        rebuildWindowTree();
        break;
    case PropertyNotify:
        if (   event.xproperty.atom != XInternAtom(mDisplay, "_NET_CLIENT_LIST", True /* only_if_exists */)
            || event.xproperty.window != DefaultRootWindow(mDisplay))
            break;
        LogRelFlowFunc(("_NET_CLIENT_LIST property event on root window\n"));
        rebuildWindowTree();
        break;
    case VBoxShapeNotify:  /* This is defined wrong in my X11 header files! */
        LogRelFlowFunc(("shape event, window=%lu, send_event=%RTbool\n",
                       (unsigned long) event.xany.window,
                       event.xany.send_event));
    /* the window member in xany is in the same place as in the shape event */
        doShapeEvent(event.xany.window);
        break;
    case UnmapNotify:
        LogRelFlowFunc(("unmap event, window=%lu, send_event=%RTbool\n",
                       (unsigned long) event.xunmap.window,
                       event.xunmap.send_event));
        rebuildWindowTree();
        break;
    default:
        break;
    }
    LogRelFlowFunc(("processed event\n"));
}

/**
 * Handle a configuration event in the seamless event thread by setting the new position.
 *
 * @param hWin the window to be examined
 */
void VBClX11SeamlessMonitor::doConfigureEvent(Window hWin)
{
    VBoxGuestWinInfo *pInfo = mGuestWindows.find(hWin);
    if (pInfo)
    {
        XWindowAttributes winAttrib;

        if (!XGetWindowAttributes(mDisplay, hWin, &winAttrib))
            return;
        pInfo->mX = winAttrib.x;
        pInfo->mY = winAttrib.y;
        pInfo->mWidth = winAttrib.width;
        pInfo->mHeight = winAttrib.height;
        mChanged = true;
    }
}

/**
 * Handle a window shape change event in the seamless event thread.
 *
 * @param hWin the window to be examined
 */
void VBClX11SeamlessMonitor::doShapeEvent(Window hWin)
{
    LogRelFlowFuncEnter();
    VBoxGuestWinInfo *pInfo = mGuestWindows.find(hWin);
    if (pInfo)
    {
        XRectangle *pRects;
        int cRects = 0, iOrdering;

        pRects = XShapeGetRectangles(mDisplay, hWin, ShapeBounding, &cRects,
                                     &iOrdering);
        if (!pRects)
            cRects = 0;
        pInfo->mhasShape = true;
        if (pInfo->mpRects)
            XFree(pInfo->mpRects);
        pInfo->mcRects = cRects;
        pInfo->mpRects = pRects;
        mChanged = true;
    }
    LogRelFlowFuncLeave();
}

/**
 * Gets the list of visible rectangles
 */
RTRECT *VBClX11SeamlessMonitor::getRects(void)
{
    return mpRects;
}

/**
 * Gets the number of rectangles in the visible rectangle list
 */
size_t VBClX11SeamlessMonitor::getRectCount(void)
{
    return mcRects;
}

RTVEC_DECL(RectList, RTRECT)

static DECLCALLBACK(int) getRectsCallback(VBoxGuestWinInfo *pInfo, struct RectList *pRects)
{
    if (pInfo->mhasShape)
    {
        for (int i = 0; i < pInfo->mcRects; ++i)
        {
            RTRECT *pRect;

            pRect = RectListPushBack(pRects);
            if (!pRect)
                return VERR_NO_MEMORY;
            pRect->xLeft   =   pInfo->mX
                             + pInfo->mpRects[i].x;
            pRect->yBottom =   pInfo->mY
                             + pInfo->mpRects[i].y
                             + pInfo->mpRects[i].height;
            pRect->xRight  =   pInfo->mX
                             + pInfo->mpRects[i].x
                             + pInfo->mpRects[i].width;
            pRect->yTop    =   pInfo->mY
                             + pInfo->mpRects[i].y;
        }
    }
    else
    {
        RTRECT *pRect;

        pRect = RectListPushBack(pRects);
        if (!pRect)
            return VERR_NO_MEMORY;
        pRect->xLeft   =  pInfo->mX;
        pRect->yBottom =  pInfo->mY
                        + pInfo->mHeight;
        pRect->xRight  =  pInfo->mX
                        + pInfo->mWidth;
        pRect->yTop    =  pInfo->mY;
    }
    return VINF_SUCCESS;
}

/**
 * Updates the list of seamless rectangles
 */
int VBClX11SeamlessMonitor::updateRects(void)
{
    LogRelFlowFuncEnter();
    struct RectList rects = RTVEC_INITIALIZER;

    if (mcRects != 0)
    {
        int rc = RectListReserve(&rects, mcRects * 2);
        if (RT_FAILURE(rc))
            return rc;
    }
    mGuestWindows.doWithAll((PFNVBOXGUESTWINCALLBACK)getRectsCallback, &rects);
    if (mpRects)
        RTMemFree(mpRects);
    mcRects = RectListSize(&rects);
    mpRects = RectListDetach(&rects);
    LogRelFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Send a client event to wake up the X11 seamless event loop prior to stopping it.
 *
 * @note This function should only be called from the host event thread.
 */
bool VBClX11SeamlessMonitor::interruptEventWait(void)
{
    LogRelFlowFuncEnter();

    Display *pDisplay = XOpenDisplay(NULL);
    if (pDisplay == NULL)
    {
        VBClLogError("Failed to open X11 display\n");
        return false;
    }

    /* Message contents set to zero. */
    XClientMessageEvent clientMessage =
    {
        /* .type         = */ ClientMessage,
        /* .serial       = */ 0,
        /* .send_event   = */ 0,
        /* .display      = */ 0,
        /* .window       = */ 0,
        /* .message_type = */ XInternAtom(pDisplay, "VBOX_CLIENT_SEAMLESS_HEARTBEAT", false),
        /* .format       = */ 8,
        /* .data ... */
    };

    bool rc = false;
    if (XSendEvent(pDisplay, DefaultRootWindow(mDisplay), false,
                   PropertyChangeMask, (XEvent *)&clientMessage))
        rc = true;

    XCloseDisplay(pDisplay);
    LogRelFlowFunc(("returning %RTbool\n", rc));
    return rc;
}


/*********************************************************************************************************************************
 * VBClX11SeamlessSvc implementation                                                                                             *
 ********************************************************************************************************************************/

VBClX11SeamlessSvc::VBClX11SeamlessSvc(void)
{
    mX11MonitorThread         = NIL_RTTHREAD;
    mX11MonitorThreadStopping = false;

    mMode    = VMMDev_Seamless_Disabled;
    mfPaused = true;
}

VBClX11SeamlessSvc::~VBClX11SeamlessSvc()
{
    /* Stopping will be done via main.cpp. */
}

/** @copydoc VBCLSERVICE::pfnInit */
int VBClX11SeamlessSvc::init(void)
{
    int rc;
    const char *pcszStage;

    do
    {
        pcszStage = "Connecting to the X server";
        rc = mX11Monitor.init(VBClSeamlessSendRegionUpdate);
        if (RT_FAILURE(rc))
            break;
        pcszStage = "Setting guest IRQ filter mask";
        rc = VbglR3CtlFilterMask(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST, 0);
        if (RT_FAILURE(rc))
            break;
        pcszStage = "Reporting support for seamless capability";
        rc = VbglR3SeamlessSetCap(true);
        if (RT_FAILURE(rc))
            break;
        rc = startX11MonitorThread();
        if (RT_FAILURE(rc))
            break;

    } while(0);

    if (RT_FAILURE(rc))
        VBClLogError("Failed to start in stage '%s' -- error %Rrc\n", pcszStage, rc);

    return rc;
}

/** @copydoc VBCLSERVICE::pfnWorker */
int VBClX11SeamlessSvc::worker(bool volatile *pfShutdown)
{
    int rc = VINF_SUCCESS;

    /* Let the main thread know that it can continue spawning services. */
    RTThreadUserSignal(RTThreadSelf());

    /* This will only exit if something goes wrong. */
    for (;;)
    {
        if (ASMAtomicReadBool(pfShutdown))
            break;

        rc = nextStateChangeEvent();

        if (rc == VERR_TRY_AGAIN)
            rc = VINF_SUCCESS;

        if (RT_FAILURE(rc))
            break;

        if (ASMAtomicReadBool(pfShutdown))
            break;

        /* If we are not stopping, sleep for a bit to avoid using up too
           much CPU while retrying. */
        RTThreadYield();
    }

    return rc;
}

/** @copydoc VBCLSERVICE::pfnStop */
void VBClX11SeamlessSvc::stop(void)
{
    VbglR3SeamlessSetCap(false);
    VbglR3CtlFilterMask(0, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST);
    stopX11MonitorThread();
}

/** @copydoc VBCLSERVICE::pfnTerm */
int VBClX11SeamlessSvc::term(void)
{
    mX11Monitor.uninit();
    return VINF_SUCCESS;
}

/**
 * Waits for a seamless state change events from the host and dispatch it.
 *
 * @returns VBox return code, or
 *          VERR_TRY_AGAIN if no new status is available and we have to try it again
 *          at some later point in time.
 */
int VBClX11SeamlessSvc::nextStateChangeEvent(void)
{
    VMMDevSeamlessMode newMode = VMMDev_Seamless_Disabled;

    int rc = VbglR3SeamlessWaitEvent(&newMode);
    if (RT_SUCCESS(rc))
    {
        mMode = newMode;
        switch (newMode)
        {
            case VMMDev_Seamless_Visible_Region:
                /* A simplified seamless mode, obtained by making the host VM window
                 * borderless and making the guest desktop transparent. */
                VBClLogVerbose(2, "\"Visible region\" mode requested\n");
                break;
            case VMMDev_Seamless_Disabled:
                VBClLogVerbose(2, "\"Disabled\" mode requested\n");
                break;
            case VMMDev_Seamless_Host_Window:
                /* One host window represents one guest window.  Not yet implemented. */
                VBClLogVerbose(2, "Unsupported \"host window\" mode requested\n");
                return VERR_NOT_SUPPORTED;
            default:
                VBClLogError("Unsupported mode %d requested\n", newMode);
                return VERR_NOT_SUPPORTED;
        }
    }
    if (   RT_SUCCESS(rc)
        || rc == VERR_TRY_AGAIN)
    {
        if (mMode == VMMDev_Seamless_Visible_Region)
            mfPaused = false;
        else
            mfPaused = true;
        mX11Monitor.interruptEventWait();
    }
    else
        VBClLogError("VbglR3SeamlessWaitEvent returned %Rrc\n", rc);

    return rc;
}

/**
 * The actual X11 window configuration change monitor thread function.
 */
int VBClX11SeamlessSvc::x11MonitorThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);

    VBClX11SeamlessSvc *pThis = (VBClX11SeamlessSvc *)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    RTThreadUserSignal(hThreadSelf);

    VBClLogVerbose(2, "X11 monitor thread started\n");

    while (!pThis->mX11MonitorThreadStopping)
    {
        if (!pThis->mfPaused)
        {
            rc = pThis->mX11Monitor.start();
            if (RT_FAILURE(rc))
                VBClLogFatalError("Failed to change the X11 seamless service state, mfPaused=%RTbool, rc=%Rrc\n",
                                  pThis->mfPaused, rc);
        }

        pThis->mX11Monitor.nextConfigurationEvent();

        if (   pThis->mfPaused
            || pThis->mX11MonitorThreadStopping)
        {
            pThis->mX11Monitor.stop();
        }
    }

    VBClLogVerbose(2, "X11 monitor thread ended\n");

    return rc;
}

/**
 * Start the X11 window configuration change monitor thread.
 */
int VBClX11SeamlessSvc::startX11MonitorThread(void)
{
    mX11MonitorThreadStopping = false;

    if (isX11MonitorThreadRunning())
        return VINF_SUCCESS;

    int rc = RTThreadCreate(&mX11MonitorThread, x11MonitorThread, this, 0,
                            RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE,
                            "seamless x11");
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(mX11MonitorThread, RT_MS_30SEC);

    if (RT_FAILURE(rc))
        VBClLogError("Failed to start X11 monitor thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Stops the monitor thread.
 */
int VBClX11SeamlessSvc::stopX11MonitorThread(void)
{
    if (!isX11MonitorThreadRunning())
        return VINF_SUCCESS;

    mX11MonitorThreadStopping = true;
    if (!mX11Monitor.interruptEventWait())
    {
        VBClLogError("Unable to notify X11 monitor thread\n");
        return VERR_INVALID_STATE;
    }

    int rcThread;
    int rc = RTThreadWait(mX11MonitorThread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        rc = rcThread;

    if (RT_SUCCESS(rc))
    {
        mX11MonitorThread = NIL_RTTHREAD;
    }
    else
        VBClLogError("Waiting for X11 monitor thread to stop failed, rc=%Rrc\n", rc);

    return rc;
}

