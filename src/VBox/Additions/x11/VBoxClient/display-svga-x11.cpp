/* $Id: display-svga-x11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * X11 guest client - VMSVGA emulation resize event pass-through to X.Org
 * guest driver.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

/*
 * Known things to test when changing this code.  All assume a guest with VMSVGA
 * active and controlled by X11 or Wayland, and Guest Additions installed and
 * running, unless otherwise stated.
 *  - On Linux 4.6 and later guests, VBoxClient --vmsvga should be running as
 *    root and not as the logged-in user.  Dynamic resizing should work for all
 *    screens in any environment which handles kernel resize notifications,
 *    including at log-in screens.  Test GNOME Shell Wayland and GNOME Shell
 *    under X.Org or Unity or KDE at the log-in screen and after log-in.
 *  - Linux 4.10 changed the user-kernel-ABI introduced in 4.6: test both.
 *  - On other guests (than Linux 4.6 or later) running X.Org Server 1.3 or
 *    later, VBoxClient --vmsvga should never be running as root, and should run
 *    (and dynamic resizing and screen enable/disable should work for all
 *    screens) whenever a user is logged in to a supported desktop environment.
 *  - On guests running X.Org Server 1.2 or older, VBoxClient --vmsvga should
 *    never run as root and should run whenever a user is logged in to a
 *    supported desktop environment.  Dynamic resizing should work for the first
 *    screen, and enabling others should not be possible.
 *  - When VMSVGA is not enabled, VBoxClient --vmsvga should never stay running.
 *  - The following assumptions are done and should be taken into account when reading/chaning the code:
 *    # The order of the outputs (monitors) is assumed to be the same in RANDROUTPUT array and
 *    XRRScreenResources.outputs array.
 *  - This code does 2 related but separate things: 1- It resizes and enables/disables monitors upon host's
 *    requests (see the infinite loop in run()). 2- it listens to RandR events (caused by this or any other X11 client)
 *    on a different thread and notifies host about the new monitor positions. See sendMonitorPositions(...). This is
 *    mainly a work around since we have realized that vmsvga does not convey correct monitor positions thru FIFO.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdio.h>
#include <dlfcn.h>
/** For sleep(..) */
#include <unistd.h>
#include "VBoxClient.h"

#include <VBox/VBoxGuestLib.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/env.h>

#include <X11/Xlibint.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/panoramiXproto.h>

#include "display-svga-xf86cvt.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MILLIS_PER_INCH (25.4)
#define DEFAULT_DPI (96.0)

/* Time in milliseconds to relax if no X11 events available. */
#define VBOX_SVGA_X11_RELAX_TIME_MS  (500)
/* Time in milliseconds to wait for host events. */
#define VBOX_SVGA_HOST_EVENT_RX_TIMEOUT_MS  (500)

/** Maximum number of supported screens.  DRM and X11 both limit this to 32. */
/** @todo if this ever changes, dynamically allocate resizeable arrays in the
 *  context structure. */
#define VMW_MAX_HEADS 32

#define checkFunctionPtrReturn(pFunction) \
    do { \
        if (pFunction) { } \
        else \
        { \
            VBClLogFatalError("Could not find symbol address (%s)\n", #pFunction); \
            dlclose(x11Context.pRandLibraryHandle); \
            x11Context.pRandLibraryHandle = NULL; \
            return VERR_NOT_FOUND; \
        } \
    } while (0)

#define checkFunctionPtr(pFunction) \
    do { \
        if (pFunction) {} \
        else VBClLogError("Could not find symbol address (%s)\n", #pFunction);\
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#define X_VMwareCtrlSetRes  1

typedef struct
{
   CARD8    reqType;
   CARD8    VMwareCtrlReqType;
   CARD16   length B16;
   CARD32   screen B32;
   CARD32   x B32;
   CARD32   y B32;
} xVMwareCtrlSetResReq;
#define sz_xVMwareCtrlSetResReq 16

typedef struct
{
   BYTE     type;
   BYTE     pad1;
   CARD16   sequenceNumber B16;
   CARD32   length B32;
   CARD32   screen B32;
   CARD32   x B32;
   CARD32   y B32;
   CARD32   pad2 B32;
   CARD32   pad3 B32;
   CARD32   pad4 B32;
} xVMwareCtrlSetResReply;
#define sz_xVMwareCtrlSetResReply 32

typedef struct {
   CARD8  reqType;           /* always X_VMwareCtrlReqCode */
   CARD8  VMwareCtrlReqType; /* always X_VMwareCtrlSetTopology */
   CARD16 length B16;
   CARD32 screen B32;
   CARD32 number B32;
   CARD32 pad1   B32;
} xVMwareCtrlSetTopologyReq;
#define sz_xVMwareCtrlSetTopologyReq 16

#define X_VMwareCtrlSetTopology 2

typedef struct {
   BYTE   type; /* X_Reply */
   BYTE   pad1;
   CARD16 sequenceNumber B16;
   CARD32 length B32;
   CARD32 screen B32;
   CARD32 pad2   B32;
   CARD32 pad3   B32;
   CARD32 pad4   B32;
   CARD32 pad5   B32;
   CARD32 pad6   B32;
} xVMwareCtrlSetTopologyReply;
#define sz_xVMwareCtrlSetTopologyReply 32

struct X11VMWRECT
{
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
};
AssertCompileSize(struct X11VMWRECT, 8);

struct X11CONTEXT
{
    Display *pDisplay;
    /* We use a separate connection for randr event listening since sharing  a
       single display object with resizing (main) and event listening threads ends up having a deadlock.*/
    Display *pDisplayRandRMonitoring;
    Window rootWindow;
    int iDefaultScreen;
    XRRScreenResources *pScreenResources;
    int hRandRMajor;
    int hRandRMinor;
    int hRandREventBase;
    int hRandRErrorBase;
    int hEventMask;
    bool fMonitorInfoAvailable;
    /** The number of outputs (monitors, including disconnect ones) xrandr reports. */
    int hOutputCount;
    void *pRandLibraryHandle;
    bool fWmwareCtrlExtention;
    int hVMWCtrlMajorOpCode;
    /** Function pointers we used if we dlopen libXrandr instead of linking. */
    void (*pXRRSelectInput) (Display *, Window, int);
    Bool (*pXRRQueryExtension) (Display *, int *, int *);
    Status (*pXRRQueryVersion) (Display *, int *, int*);
    XRRMonitorInfo* (*pXRRGetMonitors)(Display *, Window, Bool, int *);
    XRRScreenResources* (*pXRRGetScreenResources)(Display *, Window);
    Status (*pXRRSetCrtcConfig)(Display *, XRRScreenResources *, RRCrtc,
                                Time, int, int, RRMode, Rotation, RROutput *, int);
    void (*pXRRFreeMonitors)(XRRMonitorInfo *);
    void (*pXRRFreeScreenResources)(XRRScreenResources *);
    void (*pXRRFreeModeInfo)(XRRModeInfo *);
    void (*pXRRFreeOutputInfo)(XRROutputInfo *);
    void (*pXRRSetScreenSize)(Display *, Window, int, int, int, int);
    int (*pXRRUpdateConfiguration)(XEvent *event);
    XRRModeInfo* (*pXRRAllocModeInfo)(_Xconst char *, int);
    RRMode (*pXRRCreateMode) (Display *, Window, XRRModeInfo *);
    XRROutputInfo* (*pXRRGetOutputInfo) (Display *, XRRScreenResources *, RROutput);
    XRRCrtcInfo* (*pXRRGetCrtcInfo) (Display *, XRRScreenResources *, RRCrtc crtc);
    void (*pXRRFreeCrtcInfo)(XRRCrtcInfo *);
    void (*pXRRAddOutputMode)(Display *, RROutput, RRMode);
    void (*pXRRDeleteOutputMode)(Display *, RROutput, RRMode);
    void (*pXRRDestroyMode)(Display *, RRMode);
    void (*pXRRSetOutputPrimary)(Display *, Window, RROutput);
};

static X11CONTEXT x11Context;

struct RANDROUTPUT
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    bool fEnabled;
    bool fPrimary;
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void x11Connect();
static int determineOutputCount();


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Monitor positions array. Allocated here and deallocated in the class descructor. */
RTPOINT *mpMonitorPositions;
/** Thread to listen to some of the X server events. */
RTTHREAD mX11MonitorThread = NIL_RTTHREAD;
/** Shutdown indicator for the monitor thread. */
static bool g_fMonitorThreadShutdown = false;



#ifdef RT_OS_SOLARIS
static bool VMwareCtrlSetRes(
    Display *dpy, int hExtensionMajorOpcode, int screen, int x, int y)
{
    xVMwareCtrlSetResReply rep;
    xVMwareCtrlSetResReq *pReq;
    bool fResult = false;

    LockDisplay(dpy);

    GetReq(VMwareCtrlSetRes, pReq);
    AssertPtrReturn(pReq, false);

    pReq->reqType = hExtensionMajorOpcode;
    pReq->VMwareCtrlReqType = X_VMwareCtrlSetRes;
    pReq->screen = screen;
    pReq->x = x;
    pReq->y = y;

    fResult = !!_XReply(dpy, (xReply *)&rep, (SIZEOF(xVMwareCtrlSetResReply) - SIZEOF(xReply)) >> 2, xFalse);

    UnlockDisplay(dpy);

    return fResult;
}
#endif /* RT_OS_SOLARIS */

/** Makes a call to vmwarectrl extension. This updates the
 * connection information and possible resolutions (modes)
 * of each monitor on the driver. Also sets the preferred mode
 * of each output (monitor) to currently selected one. */
bool VMwareCtrlSetTopology(Display *dpy, int hExtensionMajorOpcode,
                            int screen, xXineramaScreenInfo extents[], int number)
{
    xVMwareCtrlSetTopologyReply rep;
    xVMwareCtrlSetTopologyReq *req;

    long len;

    LockDisplay(dpy);

    GetReq(VMwareCtrlSetTopology, req);
    req->reqType = hExtensionMajorOpcode;
    req->VMwareCtrlReqType = X_VMwareCtrlSetTopology;
    req->screen = screen;
    req->number = number;

    len = ((long) number) << 1;
    SetReqLen(req, len, len);
    len <<= 2;
    _XSend(dpy, (char *)extents, len);

    if (!_XReply(dpy, (xReply *)&rep,
                 (SIZEOF(xVMwareCtrlSetTopologyReply) - SIZEOF(xReply)) >> 2,
                 xFalse))
    {
        UnlockDisplay(dpy);
        SyncHandle();
        return false;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return true;
}

/** This function assumes monitors are named as from Virtual1 to VirtualX. */
static int getMonitorIdFromName(const char *sMonitorName)
{
    if (!sMonitorName)
        return -1;
#ifdef RT_OS_SOLARIS
    if (!strcmp(sMonitorName, "default"))
        return 1;
#endif
    int iLen = strlen(sMonitorName);
    if (iLen <= 0)
        return -1;
    int iBase = 10;
    int iResult = 0;
    for (int i = iLen - 1; i >= 0; --i)
    {
        /* Stop upon seeing the first non-numeric char. */
        if (sMonitorName[i] < 48 || sMonitorName[i] > 57)
            break;
        iResult += (sMonitorName[i] - 48) * iBase / 10;
        iBase *= 10;
    }
    return iResult;
}

static void sendMonitorPositions(RTPOINT *pPositions, size_t cPositions)
{
    if (cPositions && !pPositions)
    {
        VBClLogError(("Monitor position update called with NULL pointer!\n"));
        return;
    }
    int rc = VbglR3SeamlessSendMonitorPositions(cPositions, pPositions);
    if (RT_SUCCESS(rc))
        VBClLogInfo("Sending monitor positions (%u of them)  to the host: %Rrc\n", cPositions, rc);
    else
        VBClLogError("Error during sending monitor positions (%u of them)  to the host: %Rrc\n", cPositions, rc);
}

static void queryMonitorPositions()
{
    static const int iSentinelPosition = -1;
    if (mpMonitorPositions)
    {
        free(mpMonitorPositions);
        mpMonitorPositions = NULL;
    }

    int iMonitorCount = 0;
    XRRMonitorInfo *pMonitorInfo = NULL;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    pMonitorInfo = XRRGetMonitors(x11Context.pDisplayRandRMonitoring,
                                  DefaultRootWindow(x11Context.pDisplayRandRMonitoring), true, &iMonitorCount);
#else
    if (x11Context.pXRRGetMonitors)
        pMonitorInfo = x11Context.pXRRGetMonitors(x11Context.pDisplayRandRMonitoring,
                                                  DefaultRootWindow(x11Context.pDisplayRandRMonitoring), true, &iMonitorCount);
#endif
    if (!pMonitorInfo)
        return;
    if (iMonitorCount == -1)
        VBClLogError("Could not get monitor info\n");
    else
    {
        mpMonitorPositions = (RTPOINT*)malloc(x11Context.hOutputCount * sizeof(RTPOINT));
        /** @todo memset? */
        for (int i = 0; i < x11Context.hOutputCount; ++i)
        {
            mpMonitorPositions[i].x = iSentinelPosition;
            mpMonitorPositions[i].y = iSentinelPosition;
        }
        for (int i = 0; i < iMonitorCount; ++i)
        {
            char *pszMonitorName = XGetAtomName(x11Context.pDisplayRandRMonitoring, pMonitorInfo[i].name);
            if (!pszMonitorName)
            {
                VBClLogError("queryMonitorPositions: skip monitor with unknown name %d\n", i);
                continue;
            }

            int iMonitorID = getMonitorIdFromName(pszMonitorName) - 1;
            XFree((void *)pszMonitorName);
            pszMonitorName = NULL;

            if (iMonitorID >= x11Context.hOutputCount || iMonitorID == -1)
            {
                VBClLogInfo("queryMonitorPositions: skip monitor %d (id %d) (w,h)=(%d,%d) (x,y)=(%d,%d)\n",
                            i, iMonitorID,
                            pMonitorInfo[i].width, pMonitorInfo[i].height,
                            pMonitorInfo[i].x, pMonitorInfo[i].y);
                continue;
            }
            VBClLogInfo("Monitor %d (w,h)=(%d,%d) (x,y)=(%d,%d)\n",
                        i,
                        pMonitorInfo[i].width, pMonitorInfo[i].height,
                        pMonitorInfo[i].x, pMonitorInfo[i].y);
            mpMonitorPositions[iMonitorID].x = pMonitorInfo[i].x;
            mpMonitorPositions[iMonitorID].y = pMonitorInfo[i].y;
        }
        if (iMonitorCount > 0)
            sendMonitorPositions(mpMonitorPositions, x11Context.hOutputCount);
    }
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRFreeMonitors(pMonitorInfo);
#else
    if (x11Context.pXRRFreeMonitors)
        x11Context.pXRRFreeMonitors(pMonitorInfo);
#endif
}

static void monitorRandREvents()
{
    XEvent event;

    if (XPending(x11Context.pDisplayRandRMonitoring) > 0)
    {
        XNextEvent(x11Context.pDisplayRandRMonitoring, &event);
        int eventTypeOffset = event.type - x11Context.hRandREventBase;
        VBClLogInfo("received X11 event (%d)\n", event.type);
        switch (eventTypeOffset)
        {
            case RRScreenChangeNotify:
                VBClLogInfo("RRScreenChangeNotify event received\n");
                queryMonitorPositions();
                break;
            default:
                break;
        }
    } else
    {
        RTThreadSleep(VBOX_SVGA_X11_RELAX_TIME_MS);
    }
}

/**
 * @callback_method_impl{FNRTTHREAD}
 */
static DECLCALLBACK(int) x11MonitorThreadFunction(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf, pvUser);
    while (!ASMAtomicReadBool(&g_fMonitorThreadShutdown))
    {
        monitorRandREvents();
    }

    VBClLogInfo("X11 thread gracefully terminated\n");

    return 0;
}

static int startX11MonitorThread()
{
    int rc;
    Assert(g_fMonitorThreadShutdown == false);
    if (mX11MonitorThread == NIL_RTTHREAD)
    {
        rc = RTThreadCreate(&mX11MonitorThread, x11MonitorThreadFunction, NULL /*pvUser*/, 0 /*cbStack*/,
                            RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE, "X11 events");
        if (RT_FAILURE(rc))
            VBClLogFatalError("Warning: failed to start X11 monitor thread (VBoxClient) rc=%Rrc!\n", rc);
    }
    else
        rc = VINF_ALREADY_INITIALIZED;
    return rc;
}

static int stopX11MonitorThread(void)
{
    int rc = VINF_SUCCESS;
    if (mX11MonitorThread != NIL_RTTHREAD)
    {
        ASMAtomicWriteBool(&g_fMonitorThreadShutdown, true);
        /** @todo  Send event to thread to get it out of XNextEvent. */
        //????????
        //mX11Monitor.interruptEventWait();
        rc = RTThreadWait(mX11MonitorThread, RT_MS_1SEC, NULL /*prc*/);
        if (RT_SUCCESS(rc))
        {
            mX11MonitorThread = NIL_RTTHREAD;
            g_fMonitorThreadShutdown = false;
        }
        else
            VBClLogError("Failed to stop X11 monitor thread, rc=%Rrc!\n", rc);
    }
    return rc;
}

static bool callVMWCTRL(struct RANDROUTPUT *paOutputs)
{
    int hHeight = 600;
    int hWidth = 800;
    bool fResult = false;
    int idxDefaultScreen = DefaultScreen(x11Context.pDisplay);

    AssertReturn(idxDefaultScreen >= 0, false);
    AssertReturn(idxDefaultScreen < x11Context.hOutputCount, false);

    xXineramaScreenInfo *extents = (xXineramaScreenInfo *)malloc(x11Context.hOutputCount * sizeof(xXineramaScreenInfo));
    if (!extents)
        return false;
    int hRunningOffset = 0;
    for (int i = 0; i < x11Context.hOutputCount; ++i)
    {
        if (paOutputs[i].fEnabled)
        {
            hHeight = paOutputs[i].height;
            hWidth = paOutputs[i].width;
        }
        else
        {
            hHeight = 0;
            hWidth = 0;
        }
        extents[i].x_org = hRunningOffset;
        extents[i].y_org = 0;
        extents[i].width = hWidth;
        extents[i].height = hHeight;
        hRunningOffset += hWidth;
    }
#ifdef RT_OS_SOLARIS
    fResult = VMwareCtrlSetRes(x11Context.pDisplay, x11Context.hVMWCtrlMajorOpCode,
                               idxDefaultScreen, extents[idxDefaultScreen].width,
                               extents[idxDefaultScreen].height);
#else
    fResult = VMwareCtrlSetTopology(x11Context.pDisplay, x11Context.hVMWCtrlMajorOpCode,
                                    idxDefaultScreen, extents, x11Context.hOutputCount);
#endif
    free(extents);
    return fResult;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclSVGAInit(void)
{
    int rc;

    /* In 32-bit guests GAs build on our release machines causes an xserver hang.
     * So for 32-bit GAs we use our DRM client. */
#if ARCH_BITS == 32
    rc = VbglR3DrmClientStart();
    if (RT_FAILURE(rc))
        VBClLogError("Starting DRM resizing client (32-bit) failed with %Rrc\n", rc);
    return VERR_NOT_AVAILABLE; /** @todo r=andy Why ignoring rc here? */
#endif

    /* If DRM client is already running don't start this service. */
    if (VbglR3DrmClientIsRunning())
    {
        VBClLogInfo("DRM resizing is already running. Exiting this service\n");
        return VERR_NOT_AVAILABLE;
    }

    if (VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND)
    {
        rc = VbglR3DrmClientStart();
        if (RT_SUCCESS(rc))
        {
            VBClLogInfo("VBoxDrmClient has been successfully started, exitting parent process\n");
            exit(0);
        }
        else
        {
            VBClLogError("Starting DRM resizing client failed with %Rrc\n", rc);
        }
        return rc;
    }

    x11Connect();

    if (x11Context.pDisplay == NULL)
        return VERR_NOT_AVAILABLE;

    /* don't start the monitoring thread if related randr functionality is not available. */
    if (x11Context.fMonitorInfoAvailable)
    {
        if (RT_FAILURE(startX11MonitorThread()))
            return VERR_NOT_AVAILABLE;
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclSVGAStop(void)
{
    int rc;

    rc = stopX11MonitorThread();
    if (RT_FAILURE(rc))
    {
        VBClLogError("cannot stop X11 monitor thread (%Rrc)\n", rc);
        return;
    }

    if (mpMonitorPositions)
    {
        free(mpMonitorPositions);
        mpMonitorPositions = NULL;
    }

    if (x11Context.pDisplayRandRMonitoring)
    {
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRSelectInput(x11Context.pDisplayRandRMonitoring, x11Context.rootWindow, 0);
#else
        if (x11Context.pXRRSelectInput)
            x11Context.pXRRSelectInput(x11Context.pDisplayRandRMonitoring, x11Context.rootWindow, 0);
#endif
    }

    if (x11Context.pDisplay)
    {
        XCloseDisplay(x11Context.pDisplay);
        x11Context.pDisplay = NULL;
    }

    if (x11Context.pDisplayRandRMonitoring)
    {
        XCloseDisplay(x11Context.pDisplayRandRMonitoring);
        x11Context.pDisplayRandRMonitoring = NULL;
    }

    if (x11Context.pRandLibraryHandle)
    {
        dlclose(x11Context.pRandLibraryHandle);
        x11Context.pRandLibraryHandle = NULL;
    }
}

#ifndef WITH_DISTRO_XRAND_XINERAMA
static int openLibRandR()
{
    x11Context.pRandLibraryHandle = dlopen("libXrandr.so", RTLD_LAZY /*| RTLD_LOCAL */);
    if (!x11Context.pRandLibraryHandle)
        x11Context.pRandLibraryHandle = dlopen("libXrandr.so.2", RTLD_LAZY /*| RTLD_LOCAL */);
    if (!x11Context.pRandLibraryHandle)
        x11Context.pRandLibraryHandle = dlopen("libXrandr.so.2.2.0", RTLD_LAZY /*| RTLD_LOCAL */);

    if (!x11Context.pRandLibraryHandle)
    {
        VBClLogFatalError("Could not locate libXrandr for dlopen\n");
        return VERR_NOT_FOUND;
    }

    *(void **)(&x11Context.pXRRSelectInput) = dlsym(x11Context.pRandLibraryHandle, "XRRSelectInput");
    checkFunctionPtrReturn(x11Context.pXRRSelectInput);

    *(void **)(&x11Context.pXRRQueryExtension) = dlsym(x11Context.pRandLibraryHandle, "XRRQueryExtension");
    checkFunctionPtrReturn(x11Context.pXRRQueryExtension);

    *(void **)(&x11Context.pXRRQueryVersion) = dlsym(x11Context.pRandLibraryHandle, "XRRQueryVersion");
    checkFunctionPtrReturn(x11Context.pXRRQueryVersion);

    /* Don't bail out when XRRGetMonitors XRRFreeMonitors are missing as in Oracle Solaris 10. It is not crucial esp. for single monitor. */
    *(void **)(&x11Context.pXRRGetMonitors) = dlsym(x11Context.pRandLibraryHandle, "XRRGetMonitors");
    checkFunctionPtr(x11Context.pXRRGetMonitors);

    *(void **)(&x11Context.pXRRFreeMonitors) = dlsym(x11Context.pRandLibraryHandle, "XRRFreeMonitors");
    checkFunctionPtr(x11Context.pXRRFreeMonitors);

    x11Context.fMonitorInfoAvailable = x11Context.pXRRGetMonitors && x11Context.pXRRFreeMonitors;

    *(void **)(&x11Context.pXRRGetScreenResources) = dlsym(x11Context.pRandLibraryHandle, "XRRGetScreenResources");
    checkFunctionPtr(x11Context.pXRRGetScreenResources);

    *(void **)(&x11Context.pXRRSetCrtcConfig) = dlsym(x11Context.pRandLibraryHandle, "XRRSetCrtcConfig");
    checkFunctionPtr(x11Context.pXRRSetCrtcConfig);

    *(void **)(&x11Context.pXRRFreeScreenResources) = dlsym(x11Context.pRandLibraryHandle, "XRRFreeScreenResources");
    checkFunctionPtr(x11Context.pXRRFreeScreenResources);

    *(void **)(&x11Context.pXRRFreeModeInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRFreeModeInfo");
    checkFunctionPtr(x11Context.pXRRFreeModeInfo);

    *(void **)(&x11Context.pXRRFreeOutputInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRFreeOutputInfo");
    checkFunctionPtr(x11Context.pXRRFreeOutputInfo);

    *(void **)(&x11Context.pXRRSetScreenSize) = dlsym(x11Context.pRandLibraryHandle, "XRRSetScreenSize");
    checkFunctionPtr(x11Context.pXRRSetScreenSize);

    *(void **)(&x11Context.pXRRUpdateConfiguration) = dlsym(x11Context.pRandLibraryHandle, "XRRUpdateConfiguration");
    checkFunctionPtr(x11Context.pXRRUpdateConfiguration);

    *(void **)(&x11Context.pXRRAllocModeInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRAllocModeInfo");
    checkFunctionPtr(x11Context.pXRRAllocModeInfo);

    *(void **)(&x11Context.pXRRCreateMode) = dlsym(x11Context.pRandLibraryHandle, "XRRCreateMode");
    checkFunctionPtr(x11Context.pXRRCreateMode);

    *(void **)(&x11Context.pXRRGetOutputInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRGetOutputInfo");
    checkFunctionPtr(x11Context.pXRRGetOutputInfo);

    *(void **)(&x11Context.pXRRGetCrtcInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRGetCrtcInfo");
    checkFunctionPtr(x11Context.pXRRGetCrtcInfo);

    *(void **)(&x11Context.pXRRFreeCrtcInfo) = dlsym(x11Context.pRandLibraryHandle, "XRRFreeCrtcInfo");
    checkFunctionPtr(x11Context.pXRRFreeCrtcInfo);

    *(void **)(&x11Context.pXRRAddOutputMode) = dlsym(x11Context.pRandLibraryHandle, "XRRAddOutputMode");
    checkFunctionPtr(x11Context.pXRRAddOutputMode);

    *(void **)(&x11Context.pXRRDeleteOutputMode) = dlsym(x11Context.pRandLibraryHandle, "XRRDeleteOutputMode");
    checkFunctionPtr(x11Context.pXRRDeleteOutputMode);

    *(void **)(&x11Context.pXRRDestroyMode) = dlsym(x11Context.pRandLibraryHandle, "XRRDestroyMode");
    checkFunctionPtr(x11Context.pXRRDestroyMode);

    *(void **)(&x11Context.pXRRSetOutputPrimary) = dlsym(x11Context.pRandLibraryHandle, "XRRSetOutputPrimary");
    checkFunctionPtr(x11Context.pXRRSetOutputPrimary);

    return VINF_SUCCESS;
}
#endif

static void x11Connect()
{
    x11Context.pScreenResources = NULL;
    x11Context.pXRRSelectInput = NULL;
    x11Context.pRandLibraryHandle = NULL;
    x11Context.pXRRQueryExtension = NULL;
    x11Context.pXRRQueryVersion = NULL;
    x11Context.pXRRGetMonitors = NULL;
    x11Context.pXRRGetScreenResources = NULL;
    x11Context.pXRRSetCrtcConfig = NULL;
    x11Context.pXRRFreeMonitors = NULL;
    x11Context.pXRRFreeScreenResources = NULL;
    x11Context.pXRRFreeOutputInfo = NULL;
    x11Context.pXRRFreeModeInfo = NULL;
    x11Context.pXRRSetScreenSize = NULL;
    x11Context.pXRRUpdateConfiguration = NULL;
    x11Context.pXRRAllocModeInfo = NULL;
    x11Context.pXRRCreateMode = NULL;
    x11Context.pXRRGetOutputInfo = NULL;
    x11Context.pXRRGetCrtcInfo = NULL;
    x11Context.pXRRFreeCrtcInfo = NULL;
    x11Context.pXRRAddOutputMode = NULL;
    x11Context.pXRRDeleteOutputMode = NULL;
    x11Context.pXRRDestroyMode = NULL;
    x11Context.pXRRSetOutputPrimary = NULL;
    x11Context.fWmwareCtrlExtention = false;
    x11Context.fMonitorInfoAvailable = false;
    x11Context.hRandRMajor = 0;
    x11Context.hRandRMinor = 0;

    int dummy;
    if (x11Context.pDisplay != NULL)
        VBClLogFatalError("%s called with bad argument\n", __func__);
    x11Context.pDisplay = XOpenDisplay(NULL);
    x11Context.pDisplayRandRMonitoring = XOpenDisplay(NULL);
    if (x11Context.pDisplay == NULL)
        return;
#ifndef WITH_DISTRO_XRAND_XINERAMA
    if (openLibRandR() != VINF_SUCCESS)
    {
        XCloseDisplay(x11Context.pDisplay);
        XCloseDisplay(x11Context.pDisplayRandRMonitoring);
        x11Context.pDisplay = NULL;
        x11Context.pDisplayRandRMonitoring = NULL;
        return;
    }
#endif

    x11Context.fWmwareCtrlExtention = XQueryExtension(x11Context.pDisplay, "VMWARE_CTRL",
                                                      &x11Context.hVMWCtrlMajorOpCode, &dummy, &dummy);
    if (!x11Context.fWmwareCtrlExtention)
        VBClLogError("VMWARE's ctrl extension is not available! Multi monitor management is not possible\n");
    else
        VBClLogInfo("VMWARE's ctrl extension is available. Major Opcode is %d.\n", x11Context.hVMWCtrlMajorOpCode);

    /* Check Xrandr stuff. */
    bool fSuccess = false;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    fSuccess = XRRQueryExtension(x11Context.pDisplay, &x11Context.hRandREventBase, &x11Context.hRandRErrorBase);
#else
    if (x11Context.pXRRQueryExtension)
        fSuccess = x11Context.pXRRQueryExtension(x11Context.pDisplay, &x11Context.hRandREventBase, &x11Context.hRandRErrorBase);
#endif
    if (fSuccess)
    {
        fSuccess = false;
#ifdef WITH_DISTRO_XRAND_XINERAMA
        fSuccess = XRRQueryVersion(x11Context.pDisplay, &x11Context.hRandRMajor, &x11Context.hRandRMinor);
#else
    if (x11Context.pXRRQueryVersion)
        fSuccess = x11Context.pXRRQueryVersion(x11Context.pDisplay, &x11Context.hRandRMajor, &x11Context.hRandRMinor);
#endif
        if (!fSuccess)
        {
            XCloseDisplay(x11Context.pDisplay);
            x11Context.pDisplay = NULL;
            return;
        }
        if (x11Context.hRandRMajor < 1 || x11Context.hRandRMinor <= 3)
        {
            VBClLogError("Resizing service requires libXrandr Version >= 1.4. Detected version is %d.%d\n", x11Context.hRandRMajor, x11Context.hRandRMinor);
            XCloseDisplay(x11Context.pDisplay);
            x11Context.pDisplay = NULL;

            int rc = VbglR3DrmLegacyX11AgentStart();
            VBClLogInfo("Attempt to start legacy X11 resize agent, rc=%Rrc\n", rc);

            return;
        }
    }
    x11Context.rootWindow = DefaultRootWindow(x11Context.pDisplay);
    x11Context.hEventMask = RRScreenChangeNotifyMask;

    /* Select the XEvent types we want to listen to. */
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRSelectInput(x11Context.pDisplayRandRMonitoring, x11Context.rootWindow, x11Context.hEventMask);
#else
    if (x11Context.pXRRSelectInput)
        x11Context.pXRRSelectInput(x11Context.pDisplayRandRMonitoring, x11Context.rootWindow, x11Context.hEventMask);
#endif
    x11Context.iDefaultScreen = DefaultScreen(x11Context.pDisplay);

#ifdef WITH_DISTRO_XRAND_XINERAMA
    x11Context.pScreenResources = XRRGetScreenResources(x11Context.pDisplay, x11Context.rootWindow);
#else
    if (x11Context.pXRRGetScreenResources)
        x11Context.pScreenResources = x11Context.pXRRGetScreenResources(x11Context.pDisplay, x11Context.rootWindow);
#endif
    x11Context.hOutputCount = RT_VALID_PTR(x11Context.pScreenResources) ? determineOutputCount() : 0;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRFreeScreenResources(x11Context.pScreenResources);
#else
    if (x11Context.pXRRFreeScreenResources)
        x11Context.pXRRFreeScreenResources(x11Context.pScreenResources);
#endif
}

static int determineOutputCount()
{
    if (!x11Context.pScreenResources)
        return 0;
    return x11Context.pScreenResources->noutput;
}

static int findExistingModeIndex(unsigned iXRes, unsigned iYRes)
{
    if (!x11Context.pScreenResources)
        return -1;
    for (int i = 0; i < x11Context.pScreenResources->nmode; ++i)
    {
        if (x11Context.pScreenResources->modes[i].width == iXRes && x11Context.pScreenResources->modes[i].height == iYRes)
            return i;
    }
    return -1;
}

static bool disableCRTC(RRCrtc crtcID)
{
    XRRCrtcInfo *pCrctInfo = NULL;

#ifdef WITH_DISTRO_XRAND_XINERAMA
    pCrctInfo = XRRGetCrtcInfo(x11Context.pDisplay, x11Context.pScreenResources, crtcID);
#else
    if (x11Context.pXRRGetCrtcInfo)
        pCrctInfo = x11Context.pXRRGetCrtcInfo(x11Context.pDisplay, x11Context.pScreenResources, crtcID);
#endif

    if (!pCrctInfo)
        return false;

    Status ret = Success;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    ret = XRRSetCrtcConfig(x11Context.pDisplay, x11Context.pScreenResources, crtcID,
                           CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0);
#else
    if (x11Context.pXRRSetCrtcConfig)
        ret = x11Context.pXRRSetCrtcConfig(x11Context.pDisplay, x11Context.pScreenResources, crtcID,
                                           CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0);
#endif

#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRFreeCrtcInfo(pCrctInfo);
#else
    if (x11Context.pXRRFreeCrtcInfo)
        x11Context.pXRRFreeCrtcInfo(pCrctInfo);
#endif

    /** @todo  In case of unsuccesful crtc config set  we have to revert frame buffer size and crtc sizes. */
    if (ret == Success)
        return true;
    else
        return false;
}

static XRRScreenSize currentSize()
{
    XRRScreenSize cSize;
    cSize.width = DisplayWidth(x11Context.pDisplay, x11Context.iDefaultScreen);
    cSize.mwidth = DisplayWidthMM(x11Context.pDisplay, x11Context.iDefaultScreen);
    cSize.height = DisplayHeight(x11Context.pDisplay, x11Context.iDefaultScreen);
    cSize.mheight = DisplayHeightMM(x11Context.pDisplay, x11Context.iDefaultScreen);
    return cSize;
}

static unsigned int computeDpi(unsigned int pixels, unsigned int mm)
{
   unsigned int dpi = 0;
   if (mm > 0)
      dpi = (unsigned int)((double)pixels * MILLIS_PER_INCH / (double)mm + 0.5);
   return dpi > 0 ? dpi : (unsigned int)DEFAULT_DPI;
}

static bool resizeFrameBuffer(struct RANDROUTPUT *paOutputs)
{
    unsigned int iXRes = 0;
    unsigned int iYRes = 0;
    /* Don't care about the output positions for now. */
    for (int i = 0; i < x11Context.hOutputCount; ++i)
    {
        if (!paOutputs[i].fEnabled)
            continue;
        iXRes += paOutputs[i].width;
        iYRes = iYRes < paOutputs[i].height ? paOutputs[i].height : iYRes;
    }
    XRRScreenSize cSize= currentSize();
    unsigned int xdpi = computeDpi(cSize.width, cSize.mwidth);
    unsigned int ydpi = computeDpi(cSize.height, cSize.mheight);
    unsigned int xmm;
    unsigned int ymm;
    xmm = (int)(MILLIS_PER_INCH * iXRes / ((double)xdpi) + 0.5);
    ymm = (int)(MILLIS_PER_INCH * iYRes / ((double)ydpi) + 0.5);
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRSelectInput(x11Context.pDisplay, x11Context.rootWindow, RRScreenChangeNotifyMask);
    XRRSetScreenSize(x11Context.pDisplay, x11Context.rootWindow, iXRes, iYRes, xmm, ymm);
#else
    if (x11Context.pXRRSelectInput)
        x11Context.pXRRSelectInput(x11Context.pDisplay, x11Context.rootWindow, RRScreenChangeNotifyMask);
    if (x11Context.pXRRSetScreenSize)
        x11Context.pXRRSetScreenSize(x11Context.pDisplay, x11Context.rootWindow, iXRes, iYRes, xmm, ymm);
#endif
    XSync(x11Context.pDisplay, False);
    XEvent configEvent;
    bool event = false;
    while (XCheckTypedEvent(x11Context.pDisplay, RRScreenChangeNotify + x11Context.hRandREventBase, &configEvent))
    {
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRUpdateConfiguration(&configEvent);
#else
        if (x11Context.pXRRUpdateConfiguration)
            x11Context.pXRRUpdateConfiguration(&configEvent);
#endif
        event = true;
    }
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRSelectInput(x11Context.pDisplay, x11Context.rootWindow, 0);
#else
    if (x11Context.pXRRSelectInput)
        x11Context.pXRRSelectInput(x11Context.pDisplay, x11Context.rootWindow, 0);
#endif
    XRRScreenSize newSize = currentSize();

    /* On Solaris guest, new screen size is not reported properly despite
     * RRScreenChangeNotify event arrives. Hense, only check for event here.
     * Linux guests do report new size correctly. */
    if (   !event
#ifndef RT_OS_SOLARIS
        || newSize.width != (int)iXRes || newSize.height != (int)iYRes
#endif
       )
    {
        VBClLogError("Resizing frame buffer to %d %d has failed, current mode %d %d\n",
                     iXRes, iYRes, newSize.width, newSize.height);
        return false;
    }
    return true;
}

static XRRModeInfo *createMode(int iXRes, int iYRes)
{
    XRRModeInfo *pModeInfo = NULL;
    char sModeName[126];
    sprintf(sModeName, "%dx%d_vbox", iXRes, iYRes);
#ifdef WITH_DISTRO_XRAND_XINERAMA
    pModeInfo = XRRAllocModeInfo(sModeName, strlen(sModeName));
#else
    if (x11Context.pXRRAllocModeInfo)
        pModeInfo = x11Context.pXRRAllocModeInfo(sModeName, strlen(sModeName));
#endif
    pModeInfo->width = iXRes;
    pModeInfo->height = iYRes;

    DisplayModeR const mode = VBoxClient_xf86CVTMode(iXRes, iYRes, 60 /*VRefresh */, true /*Reduced */, false  /* Interlaced */);

    /* Convert kHz to Hz: f86CVTMode returns clock value in units of kHz,
     * XRRCreateMode will expect it in units of Hz. */
    pModeInfo->dotClock = mode.Clock * 1000;

    pModeInfo->hSyncStart = mode.HSyncStart;
    pModeInfo->hSyncEnd = mode.HSyncEnd;
    pModeInfo->hTotal = mode.HTotal;
    pModeInfo->hSkew = mode.HSkew;
    pModeInfo->vSyncStart = mode.VSyncStart;
    pModeInfo->vSyncEnd = mode.VSyncEnd;
    pModeInfo->vTotal = mode.VTotal;

    RRMode newMode = None;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    newMode = XRRCreateMode(x11Context.pDisplay, x11Context.rootWindow, pModeInfo);
#else
    if (x11Context.pXRRCreateMode)
        newMode = x11Context.pXRRCreateMode(x11Context.pDisplay, x11Context.rootWindow, pModeInfo);
#endif
    if (newMode == None)
    {
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRFreeModeInfo(pModeInfo);
#else
        if (x11Context.pXRRFreeModeInfo)
            x11Context.pXRRFreeModeInfo(pModeInfo);
#endif
        return NULL;
    }
    pModeInfo->id = newMode;
    return pModeInfo;
}

static bool configureOutput(int iOutputIndex, struct RANDROUTPUT *paOutputs)
{
    if (iOutputIndex >= x11Context.hOutputCount)
    {
        VBClLogError("Output index %d is greater than # of oputputs %d\n", iOutputIndex, x11Context.hOutputCount);
        return false;
    }

    AssertReturn(iOutputIndex >= 0, false);
    AssertReturn(iOutputIndex < VMW_MAX_HEADS, false);

    /* Remember the last instantiated display mode ID here. This mode will be replaced with the
     * new one on the next guest screen resize event. */
    static RRMode aPrevMode[VMW_MAX_HEADS];

    RROutput outputId = x11Context.pScreenResources->outputs[iOutputIndex];
    XRROutputInfo *pOutputInfo = NULL;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    pOutputInfo = XRRGetOutputInfo(x11Context.pDisplay, x11Context.pScreenResources, outputId);
#else
    if (x11Context.pXRRGetOutputInfo)
        pOutputInfo = x11Context.pXRRGetOutputInfo(x11Context.pDisplay, x11Context.pScreenResources, outputId);
#endif
    if (!pOutputInfo)
        return false;
    XRRModeInfo *pModeInfo = NULL;
    bool fNewMode = false;
    /* Index of the mode within the XRRScreenResources.modes array. -1 if such a mode with required resolution does not exists*/
    int iModeIndex = findExistingModeIndex(paOutputs[iOutputIndex].width, paOutputs[iOutputIndex].height);
    if (iModeIndex != -1 && iModeIndex < x11Context.pScreenResources->nmode)
        pModeInfo = &(x11Context.pScreenResources->modes[iModeIndex]);
    else
    {
        /* A mode with required size was not found. Create a new one. */
        pModeInfo = createMode(paOutputs[iOutputIndex].width, paOutputs[iOutputIndex].height);
        VBClLogInfo("create mode %s (%u) on output %d\n", pModeInfo->name, pModeInfo->id, iOutputIndex);
        fNewMode = true;
    }
    if (!pModeInfo)
    {
        VBClLogError("Could not create mode for the resolution (%d, %d)\n",
                     paOutputs[iOutputIndex].width, paOutputs[iOutputIndex].height);
        return false;
    }

#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRAddOutputMode(x11Context.pDisplay, outputId, pModeInfo->id);
#else
    if (x11Context.pXRRAddOutputMode)
        x11Context.pXRRAddOutputMode(x11Context.pDisplay, outputId, pModeInfo->id);
#endif

    /* If mode has been newly created, destroy and forget mode created on previous guest screen resize event. */
    if (   aPrevMode[iOutputIndex] > 0
        && pModeInfo->id != aPrevMode[iOutputIndex]
        && fNewMode)
    {
        VBClLogInfo("removing unused mode %u from output %d\n", aPrevMode[iOutputIndex], iOutputIndex);
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRDeleteOutputMode(x11Context.pDisplay, outputId, aPrevMode[iOutputIndex]);
        XRRDestroyMode(x11Context.pDisplay, aPrevMode[iOutputIndex]);
#else
        if (x11Context.pXRRDeleteOutputMode)
            x11Context.pXRRDeleteOutputMode(x11Context.pDisplay, outputId, aPrevMode[iOutputIndex]);
        if (x11Context.pXRRDestroyMode)
            x11Context.pXRRDestroyMode(x11Context.pDisplay, aPrevMode[iOutputIndex]);
#endif
        /* Forget destroyed mode. */
        aPrevMode[iOutputIndex] = 0;
    }

    /* Only cache modes created "by us". XRRDestroyMode will complain if provided mode
     * was not created by XRRCreateMode call. */
    if (fNewMode)
        aPrevMode[iOutputIndex] = pModeInfo->id;

    if (paOutputs[iOutputIndex].fPrimary)
    {
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRSetOutputPrimary(x11Context.pDisplay, x11Context.rootWindow, outputId);
#else
        if (x11Context.pXRRSetOutputPrimary)
            x11Context.pXRRSetOutputPrimary(x11Context.pDisplay, x11Context.rootWindow, outputId);
#endif
    }

    /* Make sure outputs crtc is set. */
    pOutputInfo->crtc = pOutputInfo->crtcs[0];

    RRCrtc crtcId = pOutputInfo->crtcs[0];
    Status ret = Success;
#ifdef WITH_DISTRO_XRAND_XINERAMA
    ret = XRRSetCrtcConfig(x11Context.pDisplay, x11Context.pScreenResources, crtcId, CurrentTime,
                           paOutputs[iOutputIndex].x, paOutputs[iOutputIndex].y,
                           pModeInfo->id, RR_Rotate_0, &(outputId), 1 /*int noutputs*/);
#else
    if (x11Context.pXRRSetCrtcConfig)
        ret = x11Context.pXRRSetCrtcConfig(x11Context.pDisplay, x11Context.pScreenResources, crtcId, CurrentTime,
                                           paOutputs[iOutputIndex].x, paOutputs[iOutputIndex].y,
                                           pModeInfo->id, RR_Rotate_0, &(outputId), 1 /*int noutputs*/);
#endif
    if (ret != Success)
        VBClLogError("crtc set config failed for output %d\n", iOutputIndex);

#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRFreeOutputInfo(pOutputInfo);
#else
    if (x11Context.pXRRFreeOutputInfo)
        x11Context.pXRRFreeOutputInfo(pOutputInfo);
#endif

    if (fNewMode)
    {
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRFreeModeInfo(pModeInfo);
#else
        if (x11Context.pXRRFreeModeInfo)
            x11Context.pXRRFreeModeInfo(pModeInfo);
#endif
    }
    return true;
}

/** Construct the xrandr command which sets the whole monitor topology each time. */
static void setXrandrTopology(struct RANDROUTPUT *paOutputs)
{
    if (!x11Context.pDisplay)
    {
        VBClLogInfo("not connected to X11\n");
        return;
    }

    XGrabServer(x11Context.pDisplay);
    if (x11Context.fWmwareCtrlExtention)
        callVMWCTRL(paOutputs);

#ifdef WITH_DISTRO_XRAND_XINERAMA
    x11Context.pScreenResources = XRRGetScreenResources(x11Context.pDisplay, x11Context.rootWindow);
#else
    if (x11Context.pXRRGetScreenResources)
        x11Context.pScreenResources = x11Context.pXRRGetScreenResources(x11Context.pDisplay, x11Context.rootWindow);
#endif

    x11Context.hOutputCount = RT_VALID_PTR(x11Context.pScreenResources) ? determineOutputCount() : 0;
    if (!x11Context.pScreenResources)
    {
        XUngrabServer(x11Context.pDisplay);
        XFlush(x11Context.pDisplay);
        return;
    }

    /* Disable crtcs. */
    for (int i = 0; i < x11Context.pScreenResources->noutput; ++i)
    {
        XRROutputInfo *pOutputInfo = NULL;
#ifdef WITH_DISTRO_XRAND_XINERAMA
        pOutputInfo = XRRGetOutputInfo(x11Context.pDisplay, x11Context.pScreenResources, x11Context.pScreenResources->outputs[i]);
#else
        if (x11Context.pXRRGetOutputInfo)
            pOutputInfo = x11Context.pXRRGetOutputInfo(x11Context.pDisplay, x11Context.pScreenResources, x11Context.pScreenResources->outputs[i]);
#endif
        if (!pOutputInfo)
            continue;
        if (pOutputInfo->crtc == None)
            continue;

        if (!disableCRTC(pOutputInfo->crtc))
        {
            VBClLogFatalError("Crtc disable failed %lu\n", pOutputInfo->crtc);
            XUngrabServer(x11Context.pDisplay);
            XSync(x11Context.pDisplay, False);
#ifdef WITH_DISTRO_XRAND_XINERAMA
            XRRFreeScreenResources(x11Context.pScreenResources);
#else
            if (x11Context.pXRRFreeScreenResources)
                x11Context.pXRRFreeScreenResources(x11Context.pScreenResources);
#endif
            XFlush(x11Context.pDisplay);
            return;
        }
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRFreeOutputInfo(pOutputInfo);
#else
        if (x11Context.pXRRFreeOutputInfo)
            x11Context.pXRRFreeOutputInfo(pOutputInfo);
#endif
    }
    /* Resize the frame buffer. */
    if (!resizeFrameBuffer(paOutputs))
    {
        XUngrabServer(x11Context.pDisplay);
        XSync(x11Context.pDisplay, False);
#ifdef WITH_DISTRO_XRAND_XINERAMA
        XRRFreeScreenResources(x11Context.pScreenResources);
#else
        if (x11Context.pXRRFreeScreenResources)
            x11Context.pXRRFreeScreenResources(x11Context.pScreenResources);
#endif
        XFlush(x11Context.pDisplay);
        return;
    }

    /* Configure the outputs. */
    for (int i = 0; i < x11Context.hOutputCount; ++i)
    {
        /* be paranoid. */
        if (i >= x11Context.pScreenResources->noutput)
            break;
        if (!paOutputs[i].fEnabled)
            continue;
        if (configureOutput(i, paOutputs))
            VBClLogInfo("output[%d] successfully configured\n", i);
        else
            VBClLogError("failed to configure output[%d]\n", i);
    }
    XSync(x11Context.pDisplay, False);
#ifdef WITH_DISTRO_XRAND_XINERAMA
    XRRFreeScreenResources(x11Context.pScreenResources);
#else
    if (x11Context.pXRRFreeScreenResources)
        x11Context.pXRRFreeScreenResources(x11Context.pScreenResources);
#endif
    XUngrabServer(x11Context.pDisplay);
    XFlush(x11Context.pDisplay);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclSVGAWorker(bool volatile *pfShutdown)
{
    /* Do not acknowledge the first event we query for to pick up old events,
     * e.g. from before a guest reboot. */
    bool fAck = false;
    bool fFirstRun = true;
    static struct VMMDevDisplayDef aMonitors[VMW_MAX_HEADS];

    int rc = VbglR3CtlFilterMask(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 0);
    if (RT_FAILURE(rc))
        VBClLogFatalError("Failed to request display change events, rc=%Rrc\n", rc);
    rc = VbglR3AcquireGuestCaps(VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0, false);
    if (RT_FAILURE(rc))
        VBClLogFatalError("Failed to register resizing support, rc=%Rrc\n", rc);
    if (rc == VERR_RESOURCE_BUSY)  /* Someone else has already acquired it. */
        return VERR_RESOURCE_BUSY;

    /* Let the main thread know that it can continue spawning services. */
    RTThreadUserSignal(RTThreadSelf());

    for (;;)
    {
        struct VMMDevDisplayDef aDisplays[VMW_MAX_HEADS];
        uint32_t cDisplaysOut;
        /* Query the first size without waiting.  This lets us e.g. pick up
         * the last event before a guest reboot when we start again after. */
        rc = VbglR3GetDisplayChangeRequestMulti(VMW_MAX_HEADS, &cDisplaysOut, aDisplays, fAck);
        fAck = true;
        if (RT_FAILURE(rc))
            VBClLogError("Failed to get display change request, rc=%Rrc\n", rc);
        if (cDisplaysOut > VMW_MAX_HEADS)
            VBClLogError("Display change request contained, rc=%Rrc\n", rc);
        if (cDisplaysOut > 0)
        {
            for (unsigned i = 0; i < cDisplaysOut && i < VMW_MAX_HEADS; ++i)
            {
                uint32_t idDisplay = aDisplays[i].idDisplay;
                if (idDisplay >= VMW_MAX_HEADS)
                    continue;
                aMonitors[idDisplay].fDisplayFlags = aDisplays[i].fDisplayFlags;
                if (!(aDisplays[i].fDisplayFlags & VMMDEV_DISPLAY_DISABLED))
                {
                    if (idDisplay == 0 || (aDisplays[i].fDisplayFlags & VMMDEV_DISPLAY_ORIGIN))
                    {
                        aMonitors[idDisplay].xOrigin = aDisplays[i].xOrigin;
                        aMonitors[idDisplay].yOrigin = aDisplays[i].yOrigin;
                    } else {
                        aMonitors[idDisplay].xOrigin = aMonitors[idDisplay - 1].xOrigin + aMonitors[idDisplay - 1].cx;
                        aMonitors[idDisplay].yOrigin = aMonitors[idDisplay - 1].yOrigin;
                    }
                    aMonitors[idDisplay].cx = aDisplays[i].cx;
                    aMonitors[idDisplay].cy = aDisplays[i].cy;
                }
            }
            /* Create a whole topology and send it to xrandr. */
            struct RANDROUTPUT aOutputs[VMW_MAX_HEADS];
            int iRunningX = 0;
            for (int j = 0; j < x11Context.hOutputCount; ++j)
            {
                aOutputs[j].x = iRunningX;
                aOutputs[j].y = aMonitors[j].yOrigin;
                aOutputs[j].width = aMonitors[j].cx;
                aOutputs[j].height = aMonitors[j].cy;
                aOutputs[j].fEnabled = !(aMonitors[j].fDisplayFlags & VMMDEV_DISPLAY_DISABLED);
                aOutputs[j].fPrimary = (aMonitors[j].fDisplayFlags & VMMDEV_DISPLAY_PRIMARY);
                if (aOutputs[j].fEnabled)
                    iRunningX += aOutputs[j].width;
            }
            /* In 32-bit guests GAs build on our release machines causes an xserver lock during vmware_ctrl extention
               if we do the call withing XGrab. We make the call the said extension only once (to connect the outputs)
               rather than at each resize iteration. */
#if ARCH_BITS == 32
            if (fFirstRun)
                callVMWCTRL(aOutputs);
#endif
            setXrandrTopology(aOutputs);
            /* Wait for some seconds and set toplogy again after the boot. In some desktop environments (cinnamon) where
               DE get into our resizing our first resize is reverted by the DE. Sleeping for some secs. helps. Setting
               topology a 2nd time resolves the black screen I get after resizing.*/
            if (fFirstRun)
            {
                sleep(4);
                setXrandrTopology(aOutputs);
                fFirstRun = false;
            }
        }
        uint32_t events;
        do
        {
            rc = VbglR3WaitEvent(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, VBOX_SVGA_HOST_EVENT_RX_TIMEOUT_MS, &events);
        } while (rc == VERR_TIMEOUT && !ASMAtomicReadBool(pfShutdown));

        if (ASMAtomicReadBool(pfShutdown))
        {
            /* Shutdown requested. */
            break;
        }
        else if (RT_FAILURE(rc))
        {
            VBClLogFatalError("Failure waiting for event, rc=%Rrc\n", rc);
        }

    };

    return VINF_SUCCESS;
}

VBCLSERVICE g_SvcDisplaySVGA =
{
    "dp-svga-x11",                      /* szName */
    "SVGA X11 display",                 /* pszDescription */
    ".vboxclient-display-svga-x11",     /* pszPidFilePathTemplate */
    NULL,                               /* pszUsage */
    NULL,                               /* pszOptions */
    NULL,                               /* pfnOption */
    vbclSVGAInit,                       /* pfnInit */
    vbclSVGAWorker,                     /* pfnWorker */
    vbclSVGAStop,                       /* pfnStop*/
    NULL                                /* pfnTerm */
};

