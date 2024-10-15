/* $Id: seamless-x11.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Seamless mode - X11 guests.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_seamless_x11_h
#define GA_INCLUDED_SRC_x11_VBoxClient_seamless_x11_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/log.h>
#include <iprt/avl.h>
#ifdef RT_NEED_NEW_AND_DELETE
# include <iprt/mem.h>
# include <new>
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#define WM_TYPE_PROP "_NET_WM_WINDOW_TYPE"
#define WM_TYPE_DESKTOP_PROP "_NET_WM_WINDOW_TYPE_DESKTOP"

/* This is defined wrong in my X11 header files! */
#define VBoxShapeNotify 64

/**
 * Callback which provides the interface for notifying the host of changes to
 * the X11 window configuration, mainly split out from @a VBoxGuestSeamlessHost
 * to simplify the unit test.
 */
typedef void FNSENDREGIONUPDATE(RTRECT *pRects, size_t cRects);
typedef FNSENDREGIONUPDATE *PFNSENDREGIONUPDATE;

/** Structure containing information about a guest window's position and visible area.
    Used inside of VBoxGuestWindowList. */
struct VBoxGuestWinInfo
{
public:
    /** Header structure for insertion into an AVL tree */
    AVLU32NODECORE Core;
    /** Is the window currently mapped? */
    bool mhasShape;
    /** Co-ordinates in the guest screen. */
    int mX, mY;
    /** Window dimensions. */
    int mWidth, mHeight;
    /** Number of rectangles used to represent the visible area. */
    int mcRects;
    /** Rectangles representing the visible area.  These must be allocated
     * by XMalloc and will be freed automatically if non-null when the class
     * is destroyed. */
    XRectangle *mpRects;
    /** Constructor. */
    VBoxGuestWinInfo(bool hasShape, int x, int y, int w, int h, int cRects, XRectangle *pRects)
        : mhasShape(hasShape), mX(x), mY(y), mWidth(w), mHeight(h)
        , mcRects(cRects), mpRects(pRects)
    {}

    /** Destructor */
    ~VBoxGuestWinInfo()
    {
        if (mpRects)
            XFree(mpRects);
    }
#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif

private:
    // We don't want a copy constructor or assignment operator
    VBoxGuestWinInfo(const VBoxGuestWinInfo &);
    VBoxGuestWinInfo &operator=(const VBoxGuestWinInfo &);
};

/** Callback type used for "DoWithAll" calls */
typedef DECLCALLBACKTYPE(int, FNVBOXGUESTWINCALLBACK,(VBoxGuestWinInfo *, void *));
/** Pointer to VBOXGUESTWINCALLBACK */
typedef FNVBOXGUESTWINCALLBACK *PFNVBOXGUESTWINCALLBACK;

static inline DECLCALLBACK(int) VBoxGuestWinCleanup(VBoxGuestWinInfo *pInfo, void *)
{
    delete pInfo;
    return VINF_SUCCESS;
}

/**
 * This class is just a wrapper around a map of structures containing
 * information about the windows on the guest system.  It has a function for
 * adding a structure (see addWindow) and one for removing it by window
 * handle (see removeWindow).
 */
class VBoxGuestWindowList
{
private:
    // We don't want a copy constructor or an assignment operator
    VBoxGuestWindowList(const VBoxGuestWindowList&);
    VBoxGuestWindowList& operator=(const VBoxGuestWindowList&);

    // Private class members
    AVLU32TREE mWindows;

public:
    // Constructor
    VBoxGuestWindowList(void) : mWindows(NULL) {}
    // Destructor
    ~VBoxGuestWindowList()
    {
        /** @todo having this inside the container class hard codes that the
         *        elements have to be allocated with the "new" operator, and
         *        I don't see a need to require this. */
        doWithAll(VBoxGuestWinCleanup, NULL);
    }

#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif

    // Standard operations
    VBoxGuestWinInfo *find(Window hWin)
    {
        return (VBoxGuestWinInfo *)RTAvlU32Get(&mWindows, hWin);
    }

    void detachAll(PFNVBOXGUESTWINCALLBACK pfnCallback, void *pvParam)
    {
        RTAvlU32Destroy(&mWindows, (PAVLU32CALLBACK)pfnCallback, pvParam);
    }

    int doWithAll(PFNVBOXGUESTWINCALLBACK pfnCallback, void *pvParam)
    {
        return RTAvlU32DoWithAll(&mWindows, 1, (PAVLU32CALLBACK)pfnCallback, pvParam);
    }

    bool addWindow(Window hWin, bool isMapped, int x, int y, int w, int h, int cRects,
                   XRectangle *pRects)
    {
        LogRelFlowFunc(("hWin=%lu, isMapped=%RTbool, x=%d, y=%d, w=%d, h=%d, cRects=%d\n",
                        (unsigned long) hWin, isMapped, x, y, w, h, cRects));
        VBoxGuestWinInfo *pInfo = new VBoxGuestWinInfo(isMapped, x, y, w, h, cRects, pRects);
        pInfo->Core.Key = hWin;
        LogRelFlowFuncLeave();
        return RTAvlU32Insert(&mWindows, &pInfo->Core);
    }

    VBoxGuestWinInfo *removeWindow(Window hWin)
    {
        LogRelFlowFuncEnter();
        return (VBoxGuestWinInfo *)RTAvlU32Remove(&mWindows, hWin);
    }
};

class VBClX11SeamlessMonitor
{
private:
    // We don't want a copy constructor or assignment operator
    VBClX11SeamlessMonitor(const VBClX11SeamlessMonitor&);
    VBClX11SeamlessMonitor& operator=(const VBClX11SeamlessMonitor&);

    // Private member variables
    /** Pointer to the host callback. */
    PFNSENDREGIONUPDATE mHostCallback;
    /** Our connection to the X11 display we are running on. */
    Display *mDisplay;
    /** Class to keep track of visible guest windows. */
    VBoxGuestWindowList mGuestWindows;
    /** The current set of seamless rectangles. */
    RTRECT *mpRects;
    /** The current number of seamless rectangles. */
    int mcRects;
    /** Do we support the X shaped window extension? */
    bool mSupportsShape;
    /** Is seamless mode currently enabled?  */
    bool mEnabled;
    /** Have there been changes since the last time we sent a notification? */
    bool mChanged;

    // Private methods

    // Methods to manage guest window information
    /**
     * Store information about a desktop window and register for structure events on it.
     * If it is mapped, go through the list of it's children and add information about
     * mapped children to the tree of visible windows, making sure that those windows are
     * not already in our list of desktop windows.
     *
     * @param   hWin     the window concerned - should be a "desktop" window
     */
    void monitorClientList(void);
    void unmonitorClientList(void);
    void rebuildWindowTree(void);
    void addClients(const Window hRoot);
    bool isVirtualRoot(Window hWin);
    void addClientWindow(Window hWin);
    void freeWindowTree(void);
    void updateHostSeamlessInfo(void);
    int updateRects(void);

public:
    /**
     * Initialise the guest and ensure that it is capable of handling seamless mode
     * @param   pHostCallback Host interface callback to notify of window configuration
     *                        changes.
     *
     * @returns iprt status code
     */
    int init(PFNSENDREGIONUPDATE pHostCallback);

    /**
     * Shutdown seamless event monitoring.
     */
    void uninit(void);

    /**
     * Initialise seamless event reporting in the guest.
     *
     * @returns IPRT status code
     */
    int start(void);
    /** Stop reporting seamless events. */
    void stop(void);
    /** Get the current list of visible rectangles. */
    RTRECT *getRects(void);
    /** Get the number of visible rectangles in the current list */
    size_t getRectCount(void);

    /** Process next event in the guest event queue - called by the event thread. */
    void nextConfigurationEvent(void);
    /** Wake up the event thread if it is waiting for an event so that it can exit. */
    bool interruptEventWait(void);

    /* Methods to handle X11 events.  These are public so that the unit test
     * can call them. */
    void doConfigureEvent(Window hWin);
    void doShapeEvent(Window hWin);

    VBClX11SeamlessMonitor(void)
        : mHostCallback(NULL), mDisplay(NULL), mpRects(NULL), mcRects(0),
          mSupportsShape(false), mEnabled(false), mChanged(false) {}

    ~VBClX11SeamlessMonitor()
    {
        uninit();
    }

#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
};

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_seamless_x11_h */
