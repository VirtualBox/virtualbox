/* $Id: tstSeamlessX11-auto.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Automated test of the X11 seamless Additions code.
 * @todo Better separate test data from implementation details!
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#include <stdlib.h> /* exit() */

#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>

#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "../seamless.h"

#undef DefaultRootWindow

/******************************************************
* Mock X11 functions needed by the seamless X11 class *
******************************************************/

int XFree(void *data)
{
    RTMemFree(data);
    return 0;
}

#define TEST_DISPLAY ((Display *)0xffff)
#define TEST_ROOT ((Window)1)

void VBClLogError(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);

    AssertPtr(psz);
    RTPrintf("Error: %s", psz);

    RTStrFree(psz);
}

/** Exit with a fatal error. */
void VBClLogFatalError(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);

    AssertPtr(psz);
    RTPrintf("Fatal error: %s", psz);

    RTStrFree(psz);

    exit(1);
}

void VBClLogVerbose(unsigned iLevel, const char *pszFormat, ...)
{
    RT_NOREF(iLevel);

    va_list va;
    va_start(va, pszFormat);
    RTPrintf("%s", pszFormat);
    va_end(va);
}

extern "C" Display *XOpenDisplay(const char *display_name);
Display *XOpenDisplay(const char *display_name)
{
    RT_NOREF1(display_name);
    return TEST_DISPLAY;
}

extern "C" int XCloseDisplay(Display *display);
int XCloseDisplay(Display *display)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    return 0;
}

enum
{
    ATOM_PROP = 1,
    ATOM_DESKTOP_PROP
};

extern "C" Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists);
Atom            XInternAtom(Display *display, const char *atom_name, Bool only_if_exists)
{
    RT_NOREF2(only_if_exists, display);
    Assert(display == TEST_DISPLAY);
    if (!RTStrCmp(atom_name, WM_TYPE_PROP))
        return (Atom) ATOM_PROP;
    if (!RTStrCmp(atom_name, WM_TYPE_DESKTOP_PROP))
        return (Atom) ATOM_DESKTOP_PROP;
    AssertFailed();
    return (Atom)0;
}

/** The window (if any) on which the WM_TYPE_PROP property is set to the
 * WM_TYPE_DESKTOP_PROP atom. */
static Window g_hSmlsDesktopWindow = 0;

extern "C" int XGetWindowProperty(Display *display, Window w, Atom property,
                                  long long_offset, long long_length,
                                  Bool delProp, Atom req_type,
                                  Atom *actual_type_return,
                                  int *actual_format_return,
                                  unsigned long *nitems_return,
                                  unsigned long *bytes_after_return,
                                  unsigned char **prop_return);
int            XGetWindowProperty(Display *display, Window w, Atom property,
                                  long long_offset, long long_length, Bool delProp,
                                  Atom req_type, Atom *actual_type_return,
                                  int *actual_format_return,
                                  unsigned long *nitems_return,
                                  unsigned long *bytes_after_return,
                                  unsigned char **prop_return)
{
    RT_NOREF2(display, long_length);
    Assert(display == TEST_DISPLAY);
    Atom atomType = XInternAtom (display, WM_TYPE_PROP, true);
    Atom atomTypeDesktop = XInternAtom (display, WM_TYPE_DESKTOP_PROP, true);
    /* We only handle things we expect. */
    AssertReturn((req_type == XA_ATOM) || (req_type == AnyPropertyType),
                 0xffff);
    AssertReturn(property == atomType, 0xffff);
    *actual_type_return = XA_ATOM;
    *actual_format_return = sizeof(Atom) * 8;
    *nitems_return = 0;
    *bytes_after_return = sizeof(Atom);
    *prop_return = NULL;
    if ((w != g_hSmlsDesktopWindow) || (g_hSmlsDesktopWindow == 0))
        return Success;
    AssertReturn(long_offset == 0, 0);
    AssertReturn(delProp == false, 0);
    unsigned char *pProp;
    pProp = (unsigned char *)RTMemDup(&atomTypeDesktop,
                                      sizeof(atomTypeDesktop));
    AssertReturn(pProp, 0xffff);
    *nitems_return = 1;
    *prop_return = pProp;
    *bytes_after_return = 0;
    return 0;
}

#if 0 /* unused */
/** Sets the current set of properties for all mock X11 windows */
static void smlsSetDesktopWindow(Window hWin)
{
    g_hSmlsDesktopWindow = hWin;
}
#endif

extern "C" Bool XShapeQueryExtension(Display *dpy, int *event_basep, int *error_basep);
Bool            XShapeQueryExtension(Display *dpy, int *event_basep, int *error_basep)
{
    RT_NOREF3(dpy, event_basep, error_basep);
    Assert(dpy == TEST_DISPLAY);
    return true;
}

/* We silently ignore this for now. */
extern "C" int XSelectInput(Display *display, Window w, long event_mask);
int            XSelectInput(Display *display, Window w, long event_mask)
{
    RT_NOREF3(display,  w,  event_mask);
    Assert(display == TEST_DISPLAY);
    return 0;
}

/* We silently ignore this for now. */
extern "C" void XShapeSelectInput(Display *display, Window w, unsigned long event_mask);
void            XShapeSelectInput(Display *display, Window w, unsigned long event_mask)
{
    RT_NOREF3(display, w, event_mask);
    Assert(display == TEST_DISPLAY);
}

extern "C" Window XDefaultRootWindow(Display *display);
Window XDefaultRootWindow(Display *display)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    return TEST_ROOT;
}

static unsigned g_cSmlsWindows = 0;
static Window *g_paSmlsWindows = NULL;
static XWindowAttributes *g_paSmlsWinAttribs = NULL;
static const char **g_papszSmlsWinNames = NULL;

extern "C" Status XQueryTree(Display *display, Window w, Window *root_return,
                             Window *parent_return, Window **children_return,
                             unsigned int *nchildren_return);
Status XQueryTree(Display *display, Window w, Window *root_return,
                  Window *parent_return, Window **children_return,
                  unsigned int *nchildren_return)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    AssertReturn(w == TEST_ROOT, False);  /* We support nothing else */
    AssertPtrReturn(children_return, False);
    AssertReturn(g_paSmlsWindows, False);
    if (root_return)
        *root_return = TEST_ROOT;
    if (parent_return)
        *parent_return = TEST_ROOT;
    *children_return = (Window *)RTMemDup(g_paSmlsWindows,
                                          g_cSmlsWindows * sizeof(Window));
    if (nchildren_return)
        *nchildren_return = g_cSmlsWindows;
    return (g_cSmlsWindows != 0);
}

extern "C" Window XmuClientWindow(Display *dpy, Window win);
Window XmuClientWindow(Display *dpy, Window win)
{
    RT_NOREF1(dpy);
    Assert(dpy == TEST_DISPLAY);
    return win;
}

extern "C" Status XGetWindowAttributes(Display *display, Window w,
                      XWindowAttributes *window_attributes_return);
Status XGetWindowAttributes(Display *display, Window w,
                            XWindowAttributes *window_attributes_return)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    AssertPtrReturn(window_attributes_return, 1);
    for (unsigned i = 0; i < g_cSmlsWindows; ++i)
        if (g_paSmlsWindows[i] == w)
        {
            *window_attributes_return = g_paSmlsWinAttribs[i];
            return 1;
        }
    return 0;
}

extern "C" Status XGetWMNormalHints(Display *display, Window w,
                                    XSizeHints *hints_return,
                                    long *supplied_return);

Status XGetWMNormalHints(Display *display, Window w,
                         XSizeHints *hints_return, long *supplied_return)
{
    RT_NOREF4(display, w, hints_return, supplied_return);
    Assert(display == TEST_DISPLAY);
    return 1;
}

static void smlsSetWindowAttributes(XWindowAttributes *pAttribs,
                                    Window *pWindows, unsigned cAttribs,
                                    const char **paNames)
{
    g_paSmlsWinAttribs = pAttribs;
    g_paSmlsWindows = pWindows;
    g_cSmlsWindows = cAttribs;
    g_papszSmlsWinNames = paNames;
}

static Window g_SmlsShapedWindow = 0;
static int g_cSmlsShapeRectangles = 0;
static XRectangle *g_pSmlsShapeRectangles = NULL;

extern "C" XRectangle *XShapeGetRectangles (Display *dpy, Window window,
                                            int kind, int *count,
                                            int *ordering);
XRectangle *XShapeGetRectangles (Display *dpy, Window window, int kind,
                                 int *count, int *ordering)
{
    RT_NOREF2(dpy, kind);
    Assert(dpy == TEST_DISPLAY);
    if ((window != g_SmlsShapedWindow) || (window == 0))
        return NULL;  /* Probably not correct, but works for us. */
    *count = g_cSmlsShapeRectangles;
    *ordering = 0;
    return (XRectangle *)RTMemDup(g_pSmlsShapeRectangles,
                                    sizeof(XRectangle)
                                  * g_cSmlsShapeRectangles);
}

static void smlsSetShapeRectangles(Window window, int cRects,
                                   XRectangle *pRects)
{
    g_SmlsShapedWindow = window;
    g_cSmlsShapeRectangles = cRects;
    g_pSmlsShapeRectangles = pRects;
}

static int g_SmlsEventType = 0;
static Window g_SmlsEventWindow = 0;

/* This should not be needed in the bits of the code we test. */
extern "C" int XNextEvent(Display *display, XEvent *event_return);
int XNextEvent(Display *display, XEvent *event_return)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    event_return->xany.type = g_SmlsEventType;
    event_return->xany.window = g_SmlsEventWindow;
    event_return->xmap.window = g_SmlsEventWindow;
    return True;
}

/* Mock XPending(): this also should not be needed. Just in case, always
 * return that at least one event is pending to be processed. */
extern "C" int XPending(Display *display);
int XPending(Display *display)
{
    RT_NOREF1(display);
    return 1;
}

static void smlsSetNextEvent(int type, Window window)
{
    g_SmlsEventType = type;
    g_SmlsEventWindow = window;
}

/* This should not be needed in the bits of the code we test. */
extern "C" Status XSendEvent(Display *display, Window w, Bool propagate,
                             long event_mask, XEvent *event_send);
Status XSendEvent(Display *display, Window w, Bool propagate,
                  long event_mask, XEvent *event_send)
{
    RT_NOREF5(display, w, propagate, event_mask, event_send);
    Assert(display == TEST_DISPLAY);
    AssertFailedReturn(0);
}

/* This should not be needed in the bits of the code we test. */
extern "C" int XFlush(Display *display);
int XFlush(Display *display)
{
    RT_NOREF1(display);
    Assert(display == TEST_DISPLAY);
    AssertFailedReturn(0);
}

/** Global "received a notification" flag. */
static bool g_fNotified = false;

/** Dummy host callback. */
void VBClSeamlessSendRegionUpdate(RTRECT *pRects, size_t cRects)
{
    RT_NOREF2(pRects, cRects);
    g_fNotified = true;
}

int VbglR3SeamlessSetCap(bool bState)
{
    RTPrintf("%s\n", bState ? "Seamless capability set"
                            : "Seamless capability unset");
    return VINF_SUCCESS;
}

int VbglR3CtlFilterMask(uint32_t u32OrMask, uint32_t u32NotMask)
{
    RTPrintf("IRQ filter mask changed.  Or mask: 0x%x.  Not mask: 0x%x\n",
             u32OrMask, u32NotMask);
    return VINF_SUCCESS;
}

int VbglR3SeamlessWaitEvent(VMMDevSeamlessMode *pMode)
{
    RT_NOREF(pMode);
    return VINF_SUCCESS;
}

static bool gotNotification(void)
{
    if (!g_fNotified)
        return false;
    g_fNotified = false;
    return true;
}

/*****************************
* The actual tests to be run *
*****************************/

/** The name of the unit test */
static const char *g_pszTestName = NULL;

/*** Test fixture data and data structures ***/

/** A structure describing a test fixture to be run through.  Each fixture
 * describes the state of the windows visible (and unmapped) on the X server
 * before and after a particular event is delivered, and the expected
 * on-screen positions of all interesting visible windows at the end of the
 * fixture as reported by the code (currently in the order it is likely to
 * report them in, @todo sort this).  We expect that the set of visible
 * windows will be the same whether we start the code before the event and
 * handle it or start the code after the event.
 */
struct SMLSFIXTURE
{
    /** The number of windows visible before the event */
    unsigned cWindowsBefore;
    /** An array of Window IDs for the visible and unmapped windows before
     * the event */
    Window *pahWindowsBefore;
    /** The window attributes matching the windows in @a paWindowsBefore */
    XWindowAttributes *paAttribsBefore;
    /** The window names matching the windows in @a paWindowsBefore */
    const char **papszNamesBefore;
    /** The shaped window before the event - we allow at most one of these.
     * Zero for none. */
    Window hShapeWindowBefore;
    /** The number of rectangles in the shaped window before the event. */
    int cShapeRectsBefore;
    /** The rectangles in the shaped window before the event */
    XRectangle *paShapeRectsBefore;
    /** The number of windows visible after the event */
    unsigned cWindowsAfter;
    /** An array of Window IDs for the visible and unmapped windows after
     * the event */
    Window *pahWindowsAfter;
    /** The window attributes matching the windows in @a paWindowsAfter */
    XWindowAttributes *paAttribsAfter;
    /** The window names matching the windows in @a paWindowsAfter */
    const char **papszNamesAfter;
    /** The shaped window after the event - we allow at most one of these.
     * Zero for none. */
    Window hShapeWindowAfter;
    /** The number of rectangles in the shaped window after the event. */
    int cShapeRectsAfter;
    /** The rectangles in the shaped window after the event */
    XRectangle *paShapeRectsAfter;
    /** The event to delivered */
    int x11EventType;
    /** The window for which the event in @enmEvent is delivered */
    Window hEventWindow;
    /** The number of windows expected to be reported at the end of the
     * fixture */
    unsigned cReportedRects;
    /** The onscreen positions of those windows. */
    RTRECT *paReportedRects;
    /** Do we expect notification after the event? */
    bool fExpectNotification;
};

/*** Test fixture to test the code against X11 configure (move) events ***/

static Window g_ahWin1[] = { 20 };
static XWindowAttributes g_aAttrib1Before[] =
{ { 100, 200, 200, 300, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IsViewable }
};
static XRectangle g_aRectangle1[] =
{
    { 0, 0, 50, 50 },
    { 50, 50, 150, 250 }
};
static XWindowAttributes g_aAttrib1After[] =
{ { 200, 300, 200, 300, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IsViewable }
};
static const char *g_apszNames1[] = { "Test Window" };

AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_aAttrib1Before));
AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_aAttrib1After));
AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_apszNames1));

static RTRECT g_aRects1[] =
{
    { 200, 300, 250, 350 },
    { 250, 350, 400, 600 }
};

static SMLSFIXTURE g_testMove =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    ConfigureNotify,
    20,
    RT_ELEMENTS(g_aRects1),
    g_aRects1,
    true
};

/*** Test fixture to test the code against X11 configure (resize) events ***/

static XWindowAttributes g_aAttrib2Before[] =
{ { 100, 200, 200, 300, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IsViewable }
};
static XRectangle g_aRectangle2Before[] =
{
    { 0, 0, 50, 50 },
    { 50, 50, 100, 100 }
};

AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_aAttrib2Before));

static SMLSFIXTURE g_testResize =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib2Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle2Before),
    g_aRectangle2Before,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    ConfigureNotify,
    20,
    RT_ELEMENTS(g_aRects1),
    g_aRects1,
    true
};

/*** Test fixture to test the code against X11 map events ***/

static XWindowAttributes g_aAttrib3Before[] =
{ { 200, 300, 200, 300, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IsUnmapped }
};

AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_aAttrib3Before));

static SMLSFIXTURE g_testMap =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib3Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    MapNotify,
    20,
    RT_ELEMENTS(g_aRects1),
    g_aRects1,
    true
};

/*** Test fixtures to test the code against X11 unmap events ***/

static XWindowAttributes g_aAttrib4After[] =
{ { 100, 200, 300, 400, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, IsUnmapped }
};

AssertCompile(RT_ELEMENTS(g_ahWin1) == RT_ELEMENTS(g_aAttrib4After));

static SMLSFIXTURE g_testUnmap =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib4After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    UnmapNotify,
    20,
    0,
    NULL,
    true
};

/*** A window we are not monitoring has been unmapped.  Nothing should
 *** happen, especially nothing bad. ***/

static RTRECT g_aRects2[] =
{
    { 100, 200, 150, 250 },
    { 150, 250, 300, 500 }
};

static SMLSFIXTURE g_testUnmapOther =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1Before,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    UnmapNotify,
    21,
    RT_ELEMENTS(g_aRects2),
    g_aRects2,
    false
};

/*** Test fixture to test the code against X11 shape events ***/

static XRectangle g_aRectangle5Before[] =
{
    { 0, 0, 200, 200 }
};

static SMLSFIXTURE g_testShape =
{
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle5Before),
    g_aRectangle5Before,
    RT_ELEMENTS(g_ahWin1),
    g_ahWin1,
    g_aAttrib1After,
    g_apszNames1,
    20,
    RT_ELEMENTS(g_aRectangle1),
    g_aRectangle1,
    VBoxShapeNotify,
    20,
    RT_ELEMENTS(g_aRects1),
    g_aRects1,
    true
};

/*** And the test code proper ***/

/** Compare two RTRECT structures */
static bool smlsCompRect(RTRECT *pFirst, RTRECT *pSecond)
{
    return (   (pFirst->xLeft == pSecond->xLeft)
            && (pFirst->yTop == pSecond->yTop)
            && (pFirst->xRight == pSecond->xRight)
            && (pFirst->yBottom == pSecond->yBottom));
}

static void smlsPrintDiffRects(RTRECT *pExp, RTRECT *pGot)
{
    RTPrintf("    Expected: %d, %d, %d, %d.  Got: %d, %d, %d, %d\n",
             pExp->xLeft, pExp->yTop, pExp->xRight, pExp->yBottom,
             pGot->xLeft, pGot->yTop, pGot->xRight, pGot->yBottom);
}

/** Run through a test fixture */
static unsigned smlsDoFixture(SMLSFIXTURE *pFixture, const char *pszDesc)
{
    VBClX11SeamlessMonitor subject;
    unsigned cErrs = 0;

    subject.init(VBClSeamlessSendRegionUpdate);
    smlsSetWindowAttributes(pFixture->paAttribsBefore,
                            pFixture->pahWindowsBefore,
                            pFixture->cWindowsBefore,
                            pFixture->papszNamesBefore);
    smlsSetShapeRectangles(pFixture->hShapeWindowBefore,
                           pFixture->cShapeRectsBefore,
                           pFixture->paShapeRectsBefore);
    subject.start();
    smlsSetWindowAttributes(pFixture->paAttribsAfter,
                            pFixture->pahWindowsAfter,
                            pFixture->cWindowsAfter,
                            pFixture->papszNamesAfter);
    smlsSetShapeRectangles(pFixture->hShapeWindowAfter,
                           pFixture->cShapeRectsAfter,
                           pFixture->paShapeRectsAfter);
    smlsSetNextEvent(pFixture->x11EventType, pFixture->hEventWindow);
    if (gotNotification())  /* Initial window tree rebuild */
    {
        RTPrintf("%s: fixture: %s.  Notification was set before the first event!!!\n",
                 g_pszTestName, pszDesc);
        ++cErrs;
    }
    subject.nextConfigurationEvent();
    if (!gotNotification())
    {
        RTPrintf("%s: fixture: %s.  No notification was sent for the initial window tree rebuild.\n",
                 g_pszTestName, pszDesc);
        ++cErrs;
    }
    smlsSetNextEvent(0, 0);
    subject.nextConfigurationEvent();
    if (pFixture->fExpectNotification && !gotNotification())
    {
        RTPrintf("%s: fixture: %s.  No notification was sent after the event.\n",
                 g_pszTestName, pszDesc);
        ++cErrs;
    }
    RTRECT *pRects = subject.getRects();
    size_t cRects = subject.getRectCount();
    if (cRects != pFixture->cReportedRects)
    {
        RTPrintf("%s: fixture: %s.  Wrong number of rectangles reported after processing event (expected %u, got %u).\n",
                 g_pszTestName, pszDesc, pFixture->cReportedRects,
                 cRects);
        ++cErrs;
    }
    else
        for (unsigned i = 0; i < cRects; ++i)
            if (!smlsCompRect(&pRects[i], &pFixture->paReportedRects[i]))
            {
                RTPrintf("%s: fixture: %s.  Rectangle %u wrong after processing event.\n",
                         g_pszTestName, pszDesc, i);
                smlsPrintDiffRects(&pFixture->paReportedRects[i],
                                   &pRects[i]);
                ++cErrs;
                break;
            }
    subject.stop();
    subject.start();
    if (cRects != pFixture->cReportedRects)
    {
        RTPrintf("%s: fixture: %s.  Wrong number of rectangles reported without processing event (expected %u, got %u).\n",
                 g_pszTestName, pszDesc, pFixture->cReportedRects,
                 cRects);
        ++cErrs;
    }
    else
        for (unsigned i = 0; i < cRects; ++i)
            if (!smlsCompRect(&pRects[i], &pFixture->paReportedRects[i]))
            {
                RTPrintf("%s: fixture: %s.  Rectangle %u wrong without processing event.\n",
                         g_pszTestName, pszDesc, i);
                smlsPrintDiffRects(&pFixture->paReportedRects[i],
                                   &pRects[i]);
                ++cErrs;
                break;
            }
    return cErrs;
}

int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);
    unsigned cErrs = 0;
    g_pszTestName = RTPathFilename(argv[0]);

    RTPrintf("%s: TESTING\n", g_pszTestName);

/** @todo r=bird: This testcase is broken and we didn't notice because we
 *        don't run it on the testboxes! @bugref{9842} */
if (argc == 1)
{
    RTPrintf("%s: Note! This testcase is broken, skipping!\n", g_pszTestName);
    return RTEXITCODE_SUCCESS;
}

    cErrs += smlsDoFixture(&g_testMove,
                           "ConfigureNotify event (window moved)");
    // Currently not working
    cErrs += smlsDoFixture(&g_testResize,
                           "ConfigureNotify event (window resized)");
    cErrs += smlsDoFixture(&g_testMap, "MapNotify event");
    cErrs += smlsDoFixture(&g_testUnmap, "UnmapNotify event");
    cErrs += smlsDoFixture(&g_testUnmapOther,
                           "UnmapNotify event for unmonitored window");
    cErrs += smlsDoFixture(&g_testShape, "ShapeNotify event");
    if (cErrs > 0)
        RTPrintf("%u errors\n", cErrs);
    return cErrs == 0 ? 0 : 1;
}
