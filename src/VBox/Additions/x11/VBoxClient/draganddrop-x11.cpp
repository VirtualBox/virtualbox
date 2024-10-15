/* $Id: draganddrop-x11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - VBoxClient X11 drag'n drop implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#ifdef VBOX_DND_WITH_XTEST
# include <X11/extensions/XTest.h>
#endif

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <iprt/cpp/mtlist.h>
#include <iprt/cpp/ministring.h>

#include <limits.h>

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/version.h>

#include "VBox/HostServices/DragAndDropSvc.h"
#include "VBoxClient.h"
#include "draganddrop.h"


/* Enable this to handle drag'n drop "promises".
 * This is needed for supporting certain applications (i.e. PcManFM on LXDE),
 * which require the drag'n drop meta data a lot earlier than actually needed.
 * That behavior is similar to macOS' drag'n drop promises, hence the name.
 *
 * Those applications query the data right while dragging over them (see GtkWidget::drag-motion),
 * instead of when the source dropped the data (GtkWidget::drag-drop).
 *
 * This might be entirely implementation-specific, so not being a bug in GTK/GDK. Also see #9820.
 */
#ifdef VBOX_WITH_DRAG_AND_DROP_PROMISES
# undef VBOX_WITH_DRAG_AND_DROP_PROMISES
#endif

/**
 * For X11 guest Xdnd is used. See http://www.acc.umu.se/~vatten/XDND.html for
 * a walk trough.
 *
 * Also useful pages:
 *     - https://www.freedesktop.org/wiki/Draganddropwarts/
 *     - https://www.freedesktop.org/wiki/Specifications/XDNDRevision/
 *
 * Host -> Guest:
 *     For X11 this means mainly forwarding all the events from HGCM to the
 *     appropriate X11 events. There exists a proxy window, which is invisible and
 *     used for all the X11 communication. On a HGCM Enter event, we set our proxy
 *     window as XdndSelection owner with the given mime-types. On every HGCM move
 *     event, we move the X11 mouse cursor to the new position and query for the
 *     window below that position. Depending on if it is XdndAware, a new window or
 *     a known window, we send the appropriate X11 messages to it. On HGCM drop, we
 *     send a XdndDrop message to the current window and wait for a X11
 *     SelectionMessage from the target window. Because we didn't have the data in
 *     the requested mime-type, yet, we save that message and ask the host for the
 *     data. When the data is successfully received from the host, we put the data
 *     as a property to the window and send a X11 SelectionNotify event to the
 *     target window.
 *
 * Guest -> Host:
 *     This is a lot more trickery than H->G. When a pending event from HGCM
 *     arrives, we ask if there currently is an owner of the XdndSelection
 *     property. If so, our proxy window is shown (1x1, but without backing store)
 *     and some mouse event is triggered. This should be followed by an XdndEnter
 *     event send to the proxy window. From this event we can fetch the necessary
 *     info of the MIME types and allowed actions and send this back to the host.
 *     On a drop request from the host, we query for the selection and should get
 *     the data in the specified mime-type. This data is send back to the host.
 *     After that we send a XdndLeave event to the source window.
 *
 ** @todo Cancelling (e.g. with ESC key) doesn't work.
 ** @todo INCR (incremental transfers) support.
 ** @todo Really check for the Xdnd version and the supported features.
 ** @todo Either get rid of the xHelpers class or properly unify the code with the drag instance class.
 */

/*********************************************************************************************************************************
 * Definitions                                                                                                                   *
 ********************************************************************************************************************************/

/** The Xdnd protocol version we support. */
#define VBOX_XDND_VERSION                       (5)

/** No flags specified. */
#define VBOX_XDND_STATUS_FLAG_NONE              0
/** Whether the target window accepts the data being dragged over or not. */
#define VBOX_XDND_STATUS_FLAG_ACCEPT            RT_BIT(0)
/** Whether the target window wants XdndPosition messages while dragging stuff over it. */
#define VBOX_XDND_STATUS_FLAG_WANTS_POS         RT_BIT(1)

/** Whether the target window accepted the drop data or not. */
#define VBOX_XDND_FINISHED_FLAG_SUCCEEDED       RT_BIT(0)

/** How many X properties our proxy window can hold. */
#define VBOX_MAX_XPROPERTIES (LONG_MAX-1)

/** The notification header text for VBClShowNotify(). */
#define VBOX_DND_SHOWNOTIFY_HEADER              VBOX_PRODUCT " Drag'n Drop"

enum XA_Type
{
    /* States */
    XA_WM_STATE = 0,
    /* Properties */
    XA_TARGETS,
    XA_MULTIPLE,
    XA_INCR,
    /* Mime Types */
    XA_image_bmp,
    XA_image_jpg,
    XA_image_tiff,
    XA_image_png,
    XA_text_uri_list,
    XA_text_uri,
    XA_text_plain,
    XA_TEXT,
    /* Xdnd */
    XA_XdndSelection,
    XA_XdndAware,
    XA_XdndEnter,
    XA_XdndLeave,
    XA_XdndTypeList,
    XA_XdndActionList,
    XA_XdndPosition,
    XA_XdndActionCopy,
    XA_XdndActionMove,
    XA_XdndActionLink,
    XA_XdndStatus,
    XA_XdndDrop,
    XA_XdndFinished,
    /* Our own stop marker */
    XA_dndstop,
    /* End marker */
    XA_End
};

/**
 * Xdnd message value indices, sorted by message type.
 */
typedef enum XdndMsg
{
    /** XdndEnter. */
    XdndEnterTypeCount = 3,         /* Maximum number of types in XdndEnter message. */

    XdndEnterWindow = 0,            /* Source window (sender). */
    XdndEnterFlags,                 /* Version in high byte, bit 0 => more data types. */
    XdndEnterType1,                 /* First available data type. */
    XdndEnterType2,                 /* Second available data type. */
    XdndEnterType3,                 /* Third available data type. */

    XdndEnterMoreTypesFlag = 1,     /* Set if there are more than XdndEnterTypeCount. */
    XdndEnterVersionRShift = 24,    /* Right shift to position version number. */
    XdndEnterVersionMask   = 0xFF,  /* Mask to get version after shifting. */

    /** XdndHere. */
    XdndHereWindow = 0,             /* Source window (sender). */
    XdndHereFlags,                  /* Reserved. */
    XdndHerePt,                     /* X + Y coordinates of mouse (root window coords). */
    XdndHereTimeStamp,              /* Timestamp for requesting data. */
    XdndHereAction,                 /* Action requested by user. */

    /** XdndPosition. */
    XdndPositionWindow = 0,         /* Source window (sender). */
    XdndPositionFlags,              /* Flags. */
    XdndPositionXY,                 /* X/Y coordinates of the mouse position relative to the root window. */
    XdndPositionTimeStamp,          /* Time stamp for retrieving the data. */
    XdndPositionAction,             /* Action requested by the user. */

    /** XdndStatus. */
    XdndStatusWindow = 0,           /* Target window (sender).*/
    XdndStatusFlags,                /* Flags returned by target. */
    XdndStatusNoMsgXY,              /* X + Y of "no msg" rectangle (root window coords). */
    XdndStatusNoMsgWH,              /* Width + height of "no msg" rectangle. */
    XdndStatusAction,               /* Action accepted by target. */

    XdndStatusAcceptDropFlag = 1,   /* Set if target will accept the drop. */
    XdndStatusSendHereFlag   = 2,   /* Set if target wants a stream of XdndPosition. */

    /** XdndLeave. */
    XdndLeaveWindow = 0,            /* Source window (sender). */
    XdndLeaveFlags,                 /* Reserved. */

    /** XdndDrop. */
    XdndDropWindow = 0,             /* Source window (sender). */
    XdndDropFlags,                  /* Reserved. */
    XdndDropTimeStamp,              /* Timestamp for requesting data. */

    /** XdndFinished. */
    XdndFinishedWindow = 0,         /* Target window (sender). */
    XdndFinishedFlags,              /* Since version 5: Bit 0 is set if the current target accepted the drop. */
    XdndFinishedAction              /* Since version 5: Contains the action performed by the target. */

} XdndMsg;

class VBClX11DnDSvc;

/** List of Atoms. */
#define VBoxDnDAtomList RTCList<Atom>

class VBClX11DnDXHelpers
{
public:

    static VBClX11DnDXHelpers *getInstance(Display *pDisplay = 0)
    {
        if (!m_pInstance)
        {
            AssertPtrReturn(pDisplay, NULL);
            m_pInstance = new VBClX11DnDXHelpers(pDisplay);
        }

        return m_pInstance;
    }

    static void destroyInstance(void)
    {
        if (m_pInstance)
        {
            delete m_pInstance;
            m_pInstance = NULL;
        }
    }

    inline Display *display()    const { return m_pDisplay; }
    inline Atom xAtom(XA_Type e) const { return m_xAtoms[e]; }

    inline Atom stringToxAtom(const char *pcszString) const
    {
        return XInternAtom(m_pDisplay, pcszString, False);
    }
    inline RTCString xAtomToString(Atom atom) const
    {
        if (atom == None) return "None";

        char* pcsAtom = XGetAtomName(m_pDisplay, atom);
        RTCString strAtom(pcsAtom);
        XFree(pcsAtom);

        return strAtom;
    }

    inline RTCString xAtomListToString(const VBoxDnDAtomList &formatList, const RTCString &strSep = DND_FORMATS_SEPARATOR_STR)
    {
        RTCString format;
        for (size_t i = 0; i < formatList.size(); ++i)
            format += xAtomToString(formatList.at(i)) + strSep;
        return format;
    }

    /**
     * Returns a filtered X11 atom list.
     *
     * @returns Filtered list.
     * @param   formatList      Atom list to convert.
     * @param   filterList      Atom list to filter out.
     */
    inline VBoxDnDAtomList xAtomListFiltered(const VBoxDnDAtomList &formatList, const VBoxDnDAtomList &filterList)
    {
        VBoxDnDAtomList tempList = formatList;
        tempList.filter(filterList);
        return tempList;
    }

    RTCString xErrorToString(int xRc) const;
    Window applicationWindowBelowCursor(Window parentWin) const;

private:
#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
    VBClX11DnDXHelpers(Display *pDisplay)
      : m_pDisplay(pDisplay)
    {
        /* Not all x11 atoms we use are defined in the headers. Create the
         * additional one we need here. */
        for (int i = 0; i < XA_End; ++i)
            m_xAtoms[i] = XInternAtom(m_pDisplay, m_xAtomNames[i], False);
    };

    /* Private member vars */
    static VBClX11DnDXHelpers   *m_pInstance;
    Display           *m_pDisplay;
    Atom               m_xAtoms[XA_End];
    static const char *m_xAtomNames[XA_End];
};

/* Some xHelpers convenience defines. */
#define gX11 VBClX11DnDXHelpers::getInstance()
#define xAtom(xa) VBClX11DnDXHelpers::getInstance()->xAtom((xa))
#define xAtomToString(xa) VBClX11DnDXHelpers::getInstance()->xAtomToString((xa))

/*********************************************************************************************************************************
 * xHelpers implementation.                                                                                                      *
 ********************************************************************************************************************************/

VBClX11DnDXHelpers *VBClX11DnDXHelpers::m_pInstance = NULL;

/* Has to be in sync with the XA_Type enum. */
const char *VBClX11DnDXHelpers::m_xAtomNames[] =
{
    /* States */
    "WM_STATE",
    /* Properties */
    "TARGETS",
    "MULTIPLE",
    "INCR",
    /* Mime Types */
    "image/bmp",
    "image/jpg",
    "image/tiff",
    "image/png",
    "text/uri-list",
    "text/uri",
    "text/plain",
    "TEXT",
    /* Xdnd */
    "XdndSelection",
    "XdndAware",
    "XdndEnter",
    "XdndLeave",
    "XdndTypeList",
    "XdndActionList",
    "XdndPosition",
    "XdndActionCopy",
    "XdndActionMove",
    "XdndActionLink",
    "XdndStatus",
    "XdndDrop",
    "XdndFinished",
    /* Our own stop marker */
    "dndstop"
};

RTCString VBClX11DnDXHelpers::xErrorToString(int xRc) const
{
    switch (xRc)
    {
        case Success:           return RTCStringFmt("%d (Success)", xRc);           break;
        case BadRequest:        return RTCStringFmt("%d (BadRequest)", xRc);        break;
        case BadValue:          return RTCStringFmt("%d (BadValue)", xRc);          break;
        case BadWindow:         return RTCStringFmt("%d (BadWindow)", xRc);         break;
        case BadPixmap:         return RTCStringFmt("%d (BadPixmap)", xRc);         break;
        case BadAtom:           return RTCStringFmt("%d (BadAtom)", xRc);           break;
        case BadCursor:         return RTCStringFmt("%d (BadCursor)", xRc);         break;
        case BadFont:           return RTCStringFmt("%d (BadFont)", xRc);           break;
        case BadMatch:          return RTCStringFmt("%d (BadMatch)", xRc);          break;
        case BadDrawable:       return RTCStringFmt("%d (BadDrawable)", xRc);       break;
        case BadAccess:         return RTCStringFmt("%d (BadAccess)", xRc);         break;
        case BadAlloc:          return RTCStringFmt("%d (BadAlloc)", xRc);          break;
        case BadColor:          return RTCStringFmt("%d (BadColor)", xRc);          break;
        case BadGC:             return RTCStringFmt("%d (BadGC)", xRc);             break;
        case BadIDChoice:       return RTCStringFmt("%d (BadIDChoice)", xRc);       break;
        case BadName:           return RTCStringFmt("%d (BadName)", xRc);           break;
        case BadLength:         return RTCStringFmt("%d (BadLength)", xRc);         break;
        case BadImplementation: return RTCStringFmt("%d (BadImplementation)", xRc); break;
    }
    return RTCStringFmt("%d (unknown)", xRc);
}

/** @todo Make this iterative. */
Window VBClX11DnDXHelpers::applicationWindowBelowCursor(Window wndParent) const
{
    /* No parent, nothing to do. */
    if(wndParent == 0)
        return 0;

    Window wndApp = 0;
    int cProps = -1;

    /* Fetch all x11 window properties of the parent window. */
    Atom *pProps = XListProperties(m_pDisplay, wndParent, &cProps);
    if (cProps > 0)
    {
        /* We check the window for the WM_STATE property. */
        for (int i = 0; i < cProps; ++i)
        {
            if (pProps[i] == xAtom(XA_WM_STATE))
            {
                /* Found it. */
                wndApp = wndParent;
                break;
            }
        }

        /* Cleanup */
        XFree(pProps);
    }

    if (!wndApp)
    {
        Window wndChild, wndTemp;
        int tmp;
        unsigned int utmp;

        /* Query the next child window of the parent window at the current
         * mouse position. */
        XQueryPointer(m_pDisplay, wndParent, &wndTemp, &wndChild, &tmp, &tmp, &tmp, &tmp, &utmp);

        /* Recursive call our self to dive into the child tree. */
        wndApp = applicationWindowBelowCursor(wndChild);
    }

    return wndApp;
}

#ifdef DEBUG
# define VBOX_DND_FN_DECL_LOG(x) inline x /* For LogFlowXXX logging. */
#else
# define VBOX_DND_FN_DECL_LOG(x) x
#endif

/**
 * Class which handles a single drag'n drop proxy window.
 ** @todo Move all proxy window-related stuff into this class! Clean up this mess.
 */
class VBClX11DnDProxyWnd
{

public:
#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
    VBClX11DnDProxyWnd(void);
    virtual ~VBClX11DnDProxyWnd(void);

public:

    int init(Display *pDisplay);
    void destroy();

    int sendFinished(Window hWndSource, VBOXDNDACTION dndAction);

public:

    Display *pDisp;
    /** Proxy window handle. */
    Window   hWnd;
    int      iX;
    int      iY;
    int      iWidth;
    int      iHeight;
};

/** This class only serve to avoid dragging in generic new() and delete(). */
class VBClX11DnDWrappedXEvent
{
public:
    XEvent m_Event;

public:
#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
    VBClX11DnDWrappedXEvent(const XEvent &a_rSrcEvent)
    {
        m_Event = a_rSrcEvent;
    }

    VBClX11DnDWrappedXEvent()
    {
        RT_ZERO(m_Event);
    }

    VBClX11DnDWrappedXEvent &operator=(const XEvent &a_rSrcEvent)
    {
        m_Event = a_rSrcEvent;
        return *this;
    }
};

/**
 * Class for handling a single drag and drop operation, that is,
 * one source and one target at a time.
 *
 * For now only one DragInstance will exits when the app is running.
 */
class VBClX11DnDInst
{
public:

    enum State
    {
        Uninitialized = 0,
        Initialized,
        Dragging,
        Dropped,
        State_32BIT_Hack = 0x7fffffff
    };

    enum Mode
    {
        Unknown = 0,
        HG,
        GH,
        Mode_32Bit_Hack = 0x7fffffff
    };

#ifdef RT_NEED_NEW_AND_DELETE
    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
    VBClX11DnDInst(Display *pDisplay, VBClX11DnDSvc *pParent);

public:

    int  init(uint32_t uScreenID);
    int  term(void);
    void stop(void);
    void reset(void);

    /* X11 message processing. */
    int onX11ClientMessage(const XEvent &e);
    int onX11MotionNotify(const XEvent &e);
    int onX11SelectionClear(const XEvent &e);
    int onX11SelectionNotify(const XEvent &e);
    int onX11SelectionRequest(const XEvent &evReq);
    int onX11Event(const XEvent &e);
    int  waitForStatusChange(uint32_t enmState, RTMSINTERVAL uTimeoutMS = 30000);
    bool waitForX11Msg(XEvent &evX, int iType, RTMSINTERVAL uTimeoutMS = 100);
    bool waitForX11ClientMsg(XClientMessageEvent &evMsg, Atom aType, RTMSINTERVAL uTimeoutMS = 100);

    /* Session handling. */
    int checkForSessionChange(void);

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    /* Guest -> Host handling. */
    int ghIsDnDPending(void);
    int ghDropped(const RTCString &strFormat, VBOXDNDACTION dndActionRequested);
#endif

    /* Host -> Guest handling. */
    int hgEnter(const RTCList<RTCString> &formats, VBOXDNDACTIONLIST dndListActionsAllowed);
    int hgLeave(void);
    int hgMove(uint32_t uPosX, uint32_t uPosY, VBOXDNDACTION dndActionDefault);
    int hgDrop(uint32_t uPosX, uint32_t uPosY, VBOXDNDACTION dndActionDefault);
    int hgDataReceive(PVBGLR3GUESTDNDMETADATA pMeta);

    /* X11 helpers. */
    int  mouseCursorFakeMove(void);
    int  mouseCursorMove(int iPosX, int iPosY);
    void mouseButtonSet(Window wndDest, int rx, int ry, int iButton, bool fPress);
    int proxyWinShow(int *piRootX = NULL, int *piRootY = NULL) const;
    int proxyWinHide(void);

    /* X11 window helpers. */
    char *wndX11GetNameA(Window wndThis) const;

    /* Xdnd protocol helpers. */
    void wndXDnDClearActionList(Window wndThis) const;
    void wndXDnDClearFormatList(Window wndThis) const;
    int wndXDnDGetActionList(Window wndThis, VBoxDnDAtomList &lstActions) const;
    int wndXDnDGetFormatList(Window wndThis, VBoxDnDAtomList &lstTypes) const;
    int wndXDnDSetActionList(Window wndThis, const VBoxDnDAtomList &lstActions) const;
    int wndXDnDSetFormatList(Window wndThis, Atom atmProp, const VBoxDnDAtomList &lstFormats) const;

    /* Atom / HGCM formatting helpers. */
    int             appendFormatsToList(const RTCList<RTCString> &lstFormats, VBoxDnDAtomList &lstAtoms) const;
    int             appendDataToList(const void *pvData, uint32_t cbData, VBoxDnDAtomList &lstAtoms) const;
    static Atom     toAtomAction(VBOXDNDACTION dndAction);
    static int      toAtomActions(VBOXDNDACTIONLIST dndActionList, VBoxDnDAtomList &lstAtoms);
    static uint32_t toHGCMAction(Atom atom);
    static uint32_t toHGCMActions(const VBoxDnDAtomList &actionsList);

protected:

    /** The instance's own DnD context. */
    VBGLR3GUESTDNDCMDCTX        m_dndCtx;
    /** Pointer to service instance. */
    VBClX11DnDSvc              *m_pParent;
    /** Pointer to X display operating on. */
    Display                    *m_pDisplay;
    /** X screen ID to operate on. */
    int                         m_screenID;
    /** Pointer to X screen operating on. */
    Screen                     *m_pScreen;
    /** Root window handle. */
    Window                      m_wndRoot;
    /** Proxy window. */
    VBClX11DnDProxyWnd          m_wndProxy;
    /** Current source/target window handle. */
    Window                      m_wndCur;
    /** The XDnD protocol version the current source/target window is using.
     *  Set to 0 if not available / not set yet. */
    uint8_t                     m_uXdndVer;
    /** Last mouse X position (in pixels, absolute to root window).
     *  Set to -1 if not set yet. */
    int                         m_lastMouseX;
    /** Last mouse Y position (in pixels, absolute to root window).
     *  Set to -1 if not set yet. */
    int                         m_lastMouseY;
    /** List of default (Atom) formats required for X11 Xdnd handling.
     *  This list will be included by \a m_lstAtomFormats. */
    VBoxDnDAtomList             m_lstAtomFormatsX11;
    /** List of (Atom) formats the current source/target window supports. */
    VBoxDnDAtomList             m_lstAtomFormats;
    /** List of (Atom) actions the current source/target window supports. */
    VBoxDnDAtomList             m_lstAtomActions;
    /** Buffer for answering the target window's selection request. */
    void                       *m_pvSelReqData;
    /** Size (in bytes) of selection request data buffer. */
    uint32_t                    m_cbSelReqData;
    /** Current operation mode. */
    volatile uint32_t           m_enmMode;
    /** Current state of operation mode. */
    volatile uint32_t           m_enmState;
    /** The instance's own X event queue. */
    RTCMTList<VBClX11DnDWrappedXEvent>    m_eventQueueList;
    /** Critical section for providing serialized access to list event queue's contents. */
    RTCRITSECT                  m_eventQueueCS;
    /** Event for notifying this instance in case of a new event. */
    RTSEMEVENT                  m_eventQueueEvent;
    /** Critical section for data access. */
    RTCRITSECT                  m_dataCS;
    /** List of allowed formats. */
    RTCList<RTCString>          m_lstAllowedFormats;
    /** Number of failed attempts by the host
     *  to query for an active drag and drop operation on the guest. */
    uint16_t                    m_cFailedPendingAttempts;
};


/*********************************************************************************************************************************
 * DragInstanc implementation.                                                                                                   *
 ********************************************************************************************************************************/

VBClX11DnDInst::VBClX11DnDInst(Display *pDisplay, VBClX11DnDSvc *pParent)
    : m_pParent(pParent)
    , m_pDisplay(pDisplay)
    , m_pScreen(0)
    , m_wndRoot(0)
    , m_wndCur(0)
    , m_uXdndVer(0)
    , m_pvSelReqData(NULL)
    , m_cbSelReqData(0)
    , m_enmMode(Unknown)
    , m_enmState(Uninitialized)
{
    /* Append default targets we support.
     * Note: The order is sorted by preference; be careful when changing this. */
    m_lstAtomFormatsX11.append(xAtom(XA_TARGETS));
    m_lstAtomFormatsX11.append(xAtom(XA_MULTIPLE));
    /** @todo Support INC (incremental transfers). */
}

/**
 * Stops this drag instance.
 */
void VBClX11DnDInst::stop(void)
{
    LogFlowFuncEnter();

    int rc2 = VbglR3DnDDisconnect(&m_dndCtx);
    AssertRC(rc2);

    LogFlowFuncLeave();
}

/**
 * Terminates (destroys) this drag instance.
 *
 * @return VBox status code.
 */
int VBClX11DnDInst::term(void)
{
    LogFlowFuncEnter();

    if (m_wndProxy.hWnd != 0)
        XDestroyWindow(m_pDisplay, m_wndProxy.hWnd);

    int rc = VbglR3DnDDisconnect(&m_dndCtx);
    AssertRCReturn(rc, rc);

    if (m_pvSelReqData)
        RTMemFree(m_pvSelReqData);

    rc = RTSemEventDestroy(m_eventQueueEvent);
    AssertRCReturn(rc, rc);

    rc = RTCritSectDelete(&m_eventQueueCS);
    AssertRCReturn(rc, rc);

    rc = RTCritSectDelete(&m_dataCS);
    AssertRCReturn(rc, rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resets this drag instance.
 */
void VBClX11DnDInst::reset(void)
{
    LogFlowFuncEnter();

    /* Hide the proxy win. */
    proxyWinHide();

    int rc2 = RTCritSectEnter(&m_dataCS);
    if (RT_SUCCESS(rc2))
    {
        /* If we are currently the Xdnd selection owner, clear that. */
        Window pWnd = XGetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection));
        if (pWnd == m_wndProxy.hWnd)
            XSetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection), None, CurrentTime);

        /* Clear any other DnD specific data on the proxy window. */
        wndXDnDClearFormatList(m_wndProxy.hWnd);
        wndXDnDClearActionList(m_wndProxy.hWnd);

        m_lstAtomActions.clear();

        /* First, clear the formats list and apply the X11-specific default formats,
         * required for making Xdnd to work. */
        m_lstAtomFormats.clear();
        m_lstAtomFormats.append(m_lstAtomFormatsX11);

        m_wndCur                 = 0;
        m_uXdndVer               = 0;
        m_lastMouseX             = -1;
        m_lastMouseY             = -1;
        m_enmState               = Initialized;
        m_enmMode                = Unknown;
        m_cFailedPendingAttempts = 0;

        /* Reset the selection request buffer. */
        if (m_pvSelReqData)
        {
            RTMemFree(m_pvSelReqData);
            m_pvSelReqData = NULL;

            Assert(m_cbSelReqData);
            m_cbSelReqData = 0;
        }

        rc2 = RTCritSectEnter(&m_eventQueueCS);
        if (RT_SUCCESS(rc2))
        {
            m_eventQueueList.clear();

            rc2 = RTCritSectLeave(&m_eventQueueCS);
            AssertRC(rc2);
        }

        RTCritSectLeave(&m_dataCS);
    }

    LogFlowFuncLeave();
}

/**
 * Initializes this drag instance.
 *
 * @return  IPRT status code.
 * @param   uScreenID             X' screen ID to use.
 */
int VBClX11DnDInst::init(uint32_t uScreenID)
{
    int rc = VbglR3DnDConnect(&m_dndCtx);
    /* Note: Can return VINF_PERMISSION_DENIED if HGCM host service is not available. */
    if (rc != VINF_SUCCESS)
        return rc;

    if (g_cVerbosity)
    {
        RTCString strBody = RTCStringFmt("Connected (screen %RU32, verbosity %u)", uScreenID, g_cVerbosity);
        VBClShowNotify(VBOX_DND_SHOWNOTIFY_HEADER, strBody.c_str());
    }

    do
    {
        rc = RTSemEventCreate(&m_eventQueueEvent);
        if (RT_FAILURE(rc))
            break;

        rc = RTCritSectInit(&m_eventQueueCS);
        if (RT_FAILURE(rc))
            break;

        rc = RTCritSectInit(&m_dataCS);
        if (RT_FAILURE(rc))
            break;

        /*
         * Enough screens configured in the x11 server?
         */
        if ((int)uScreenID > ScreenCount(m_pDisplay))
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }
#if 0
        /* Get the screen number from the x11 server. */
        pDrag->screen = ScreenOfDisplay(m_pDisplay, uScreenID);
        if (!pDrag->screen)
        {
            rc = VERR_GENERAL_FAILURE;
            break;
        }
#endif
        m_screenID = uScreenID;

        /* Now query the corresponding root window of this screen. */
        m_wndRoot = RootWindow(m_pDisplay, m_screenID);
        if (!m_wndRoot)
        {
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        /*
         * Create an invisible window which will act as proxy for the DnD
         * operation. This window will be used for both the GH and HG
         * direction.
         */
        XSetWindowAttributes attr;
        RT_ZERO(attr);
        attr.event_mask            =   EnterWindowMask  | LeaveWindowMask
                                     | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
        attr.override_redirect     = True;
        attr.do_not_propagate_mask = NoEventMask;

        if (g_cVerbosity >= 3)
        {
            attr.background_pixel      = XWhitePixel(m_pDisplay, m_screenID);
            attr.border_pixel          = XBlackPixel(m_pDisplay, m_screenID);
            m_wndProxy.hWnd = XCreateWindow(m_pDisplay, m_wndRoot               /* Parent */,
                                            100, 100,                           /* Position */
                                            100, 100,                           /* Width + height */
                                            2,                                  /* Border width */
                                            CopyFromParent,                     /* Depth */
                                            InputOutput,                        /* Class */
                                            CopyFromParent,                     /* Visual */
                                              CWBackPixel
                                            | CWBorderPixel
                                            | CWOverrideRedirect
                                            | CWDontPropagate,                  /* Value mask */
                                            &attr);                             /* Attributes for value mask */
        }

        m_wndProxy.hWnd = XCreateWindow(m_pDisplay, m_wndRoot                   /* Parent */,
                                        0, 0,                                   /* Position */
                                        1, 1,                                   /* Width + height */
                                        0,                                      /* Border width */
                                        CopyFromParent,                         /* Depth */
                                        InputOnly,                              /* Class */
                                        CopyFromParent,                         /* Visual */
                                        CWOverrideRedirect | CWDontPropagate,   /* Value mask */
                                        &attr);                                 /* Attributes for value mask */

        if (!m_wndProxy.hWnd)
        {
            VBClLogError("Error creating proxy window\n");
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        rc = m_wndProxy.init(m_pDisplay);
        if (RT_FAILURE(rc))
        {
            VBClLogError("Error initializing proxy window, rc=%Rrc\n", rc);
            break;
        }

        if (g_cVerbosity >= 3) /* Make debug window visible. */
        {
            XFlush(m_pDisplay);
            XMapWindow(m_pDisplay, m_wndProxy.hWnd);
            XRaiseWindow(m_pDisplay, m_wndProxy.hWnd);
            XFlush(m_pDisplay);
        }

        VBClLogInfo("Proxy window=%#x (debug mode: %RTbool), root window=%#x ...\n",
                    m_wndProxy.hWnd, RT_BOOL(g_cVerbosity >= 3), m_wndRoot);

        /* Set the window's name for easier lookup. */
        XStoreName(m_pDisplay, m_wndProxy.hWnd, "VBoxClientWndDnD");

        /* Make the new window Xdnd aware. */
        Atom atmVer = VBOX_XDND_VERSION;
        XChangeProperty(m_pDisplay, m_wndProxy.hWnd, xAtom(XA_XdndAware), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&atmVer), 1);
    } while (0);

    if (RT_SUCCESS(rc))
    {
        reset();
    }
    else
        VBClLogError("Initializing drag instance for screen %RU32 failed with rc=%Rrc\n", uScreenID, rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Callback handler for a generic client message from a window.
 *
 * @return  IPRT status code.
 * @param   e                       X11 event to handle.
 */
int VBClX11DnDInst::onX11ClientMessage(const XEvent &e)
{
    AssertReturn(e.type == ClientMessage, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));
    LogFlowThisFunc(("Event wnd=%#x, msg=%s\n", e.xclient.window, xAtomToString(e.xclient.message_type).c_str()));

    int rc = VINF_SUCCESS;

    char *pszWndCurName = wndX11GetNameA(m_wndCur);
    AssertPtrReturn(pszWndCurName, VERR_NO_MEMORY);

    switch (m_enmMode)
    {
        case HG:
        {
            /*
             * Client messages are used to inform us about the status of a XdndAware
             * window, in response of some events we send to them.
             */

            /* The target window informs us of the current Xdnd status. */
            if (e.xclient.message_type == xAtom(XA_XdndStatus))
            {
                Window wndTgt = static_cast<Window>(e.xclient.data.l[XdndStatusWindow]);

                char *pszWndTgtName = wndX11GetNameA(wndTgt);
                AssertPtrBreakStmt(pszWndTgtName, VERR_NO_MEMORY);

                /* Does the target accept the drop? */
                bool const fAcceptDrop    = RT_BOOL(e.xclient.data.l[XdndStatusFlags] & VBOX_XDND_STATUS_FLAG_ACCEPT);
                /* Does the target want XdndPosition messages? */
                bool const fWantsPosition = RT_BOOL(e.xclient.data.l[XdndStatusFlags] & VBOX_XDND_STATUS_FLAG_WANTS_POS);

                /*
                 * The XdndStatus message tell us if the window will accept the DnD
                 * event and with which action. We immediately send this info down to
                 * the host as a response of a previous DnD message.
                 */
                RTCString strActions = xAtomToString(e.xclient.data.l[XdndStatusAction]);

                VBClLogInfo("Target window %#x ('%s')\n", wndTgt, pszWndTgtName);
                VBClLogInfo("    - %s accept data (actions '%s')\n", fAcceptDrop ? "does" : "does not", strActions.c_str());
                VBClLogInfo("    - %s want position messages\n", fWantsPosition ? "does" : "does not");

                uint16_t const x  = RT_HI_U16((uint32_t)e.xclient.data.l[XdndStatusNoMsgXY]);
                uint16_t const y  = RT_LO_U16((uint32_t)e.xclient.data.l[XdndStatusNoMsgXY]);
                uint16_t const cx = RT_HI_U16((uint32_t)e.xclient.data.l[XdndStatusNoMsgWH]);
                uint16_t const cy = RT_LO_U16((uint32_t)e.xclient.data.l[XdndStatusNoMsgWH]);

                if (cx && cy)
                {
                    VBClLogInfo("Target window %#x ('%s') reported dead area at %RU16,%RU16 (%RU16 x %RU16)\n",
                                wndTgt, pszWndTgtName, x, y, cx, cy);
                    /** @todo Save dead area and don't send XdndPosition messages anymore into it. */
                }

                if (m_wndCur == wndTgt)
                {
                    VBOXDNDACTION dndAction = VBOX_DND_ACTION_IGNORE; /* Default is ignoring. */
                    /** @todo Compare this with the allowed actions. */
                    if (fAcceptDrop)
                        dndAction = toHGCMAction(static_cast<Atom>(e.xclient.data.l[XdndStatusAction]));

                    rc = VbglR3DnDHGSendAckOp(&m_dndCtx, dndAction);
                }
                else
                    VBClLogInfo("Target window %#x ('%s') is not our current window, skipping\n", wndTgt, pszWndTgtName);

                RTStrFree(pszWndTgtName);
            }
            /* The target window informs us that it finished the Xdnd operation and that we may free all data. */
            else if (e.xclient.message_type == xAtom(XA_XdndFinished))
            {
                Window wndTarget = static_cast<Window>(e.xclient.data.l[XdndFinishedWindow]);

                char *pszWndTgtName = wndX11GetNameA(wndTarget);
                AssertPtrBreakStmt(pszWndTgtName, VERR_NO_MEMORY);

                if (m_uXdndVer >= 5)
                {
                    const bool  fSucceeded = e.xclient.data.l[XdndFinishedFlags] & VBOX_XDND_FINISHED_FLAG_SUCCEEDED;
            #if 0 /** @todo Returns garbage -- investigate this! */
                    //const char *pcszAction = fSucceeded ? xAtomToString(e.xclient.data.l[XdndFinishedAction]).c_str() : NULL;
            #endif
                    VBClLogInfo("Target window %#x ('%s') has %s the data\n",
                                wndTarget, pszWndTgtName, fSucceeded ? "accepted" : "rejected");
                }
                else /* Xdnd < version 5 did not have the XdndFinishedFlags / XdndFinishedAction properties. */
                    VBClLogInfo("Target window %#x ('%s') has accepted the data\n", wndTarget, pszWndTgtName);

                RTStrFree(pszWndTgtName);

                reset();
            }
            else
            {
                LogFlowThisFunc(("Unhandled client message '%s'\n", xAtomToString(e.xclient.message_type).c_str()));
                rc = VERR_NOT_SUPPORTED;
            }

            break;
        }

        case Unknown: /* Mode not set (yet). */
            RT_FALL_THROUGH();
        case GH:
        {
            /*
             * This message marks the beginning of a new drag and drop
             * operation on the guest.
             */
            if (e.xclient.message_type == xAtom(XA_XdndEnter))
            {
                /*
                 * Get the window which currently has the XA_XdndSelection
                 * bit set.
                 */
                Window wndSel = XGetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection));
                char *pszWndSelName = wndX11GetNameA(wndSel);
                AssertPtrBreakStmt(pszWndSelName, VERR_NO_MEMORY);

                mouseButtonSet(m_wndProxy.hWnd, -1, -1, 1, true /* fPress */);

                /*
                 * Update our state and the window handle to process.
                 */
                rc = RTCritSectEnter(&m_dataCS);
                if (RT_SUCCESS(rc))
                {
                    uint8_t const uXdndVer = (uint8_t)e.xclient.data.l[XdndEnterFlags] >> XdndEnterVersionRShift;

                    VBClLogInfo("Entered new source window %#x ('%s'), supports Xdnd version %u\n", wndSel, pszWndSelName, uXdndVer);
#ifdef DEBUG
                    XWindowAttributes xwa;
                    XGetWindowAttributes(m_pDisplay, m_wndCur, &xwa);
                    LogFlowThisFunc(("wndCur=%#x, x=%d, y=%d, width=%d, height=%d\n", m_wndCur, xwa.x, xwa.y, xwa.width, xwa.height));
#endif
                    /*
                     * Retrieve supported formats.
                     */

                    /* Check if the MIME types are in the message itself or if we need
                     * to fetch the XdndTypeList property from the window. */
                    bool fMoreTypes = e.xclient.data.l[XdndEnterFlags] & XdndEnterMoreTypesFlag;
                    if (!fMoreTypes)
                    {
                        /* Only up to 3 format types supported. */
                        /* Start with index 2 (first item). */
                        for (int i = 2; i < 5; i++)
                        {
                            LogFlowThisFunc(("\t%s\n", gX11->xAtomToString(e.xclient.data.l[i]).c_str()));
                            m_lstAtomFormats.append(e.xclient.data.l[i]);
                        }
                    }
                    else
                    {
                        /* More than 3 format types supported. */
                        rc = wndXDnDGetFormatList(wndSel, m_lstAtomFormats);
                    }

                    if (RT_FAILURE(rc))
                    {
                        VBClLogError("Error retrieving supported formats, rc=%Rrc\n", rc);
                        break;
                    }

                    /*
                     * Retrieve supported actions.
                     */
                    if (uXdndVer >= 2) /* More than one action allowed since protocol version 2. */
                    {
                        rc = wndXDnDGetActionList(wndSel, m_lstAtomActions);
                    }
                    else /* Only "copy" action allowed on legacy applications. */
                        m_lstAtomActions.append(XA_XdndActionCopy);

                    if (RT_FAILURE(rc))
                    {
                        VBClLogError("Error retrieving supported actions, rc=%Rrc\n", rc);
                        break;
                    }

                    VBClLogInfo("Source window %#x ('%s')\n", wndSel, pszWndSelName);
                    VBClLogInfo("    - supports the formats ");
                    for (size_t i = 0; i < m_lstAtomFormats.size(); i++)
                    {
                        if (i > 0)
                            VBClLogInfo(", ");
                        VBClLogInfo("%s", gX11->xAtomToString(m_lstAtomFormats[i]).c_str());
                    }
                    VBClLogInfo("\n");
                    VBClLogInfo("    - supports the actions ");
                    for (size_t i = 0; i < m_lstAtomActions.size(); i++)
                    {
                        if (i > 0)
                            VBClLogInfo(", ");
                        VBClLogInfo("%s", gX11->xAtomToString(m_lstAtomActions[i]).c_str());
                    }
                    VBClLogInfo("\n");

                    AssertBreakStmt(wndSel == (Window)e.xclient.data.l[XdndEnterWindow],
                                    rc = VERR_INVALID_PARAMETER); /* Source window. */

                    m_wndCur   = wndSel;
                    m_uXdndVer = uXdndVer;
                    m_enmMode  = GH;
                    m_enmState = Dragging;

                    RTCritSectLeave(&m_dataCS);
                }

                RTStrFree(pszWndSelName);
            }
            else if (   e.xclient.message_type == xAtom(XA_XdndPosition)
                     && m_wndCur               == static_cast<Window>(e.xclient.data.l[XdndPositionWindow]))
            {
                if (m_enmState != Dragging) /* Wrong mode? Bail out. */
                {
                    reset();
                    break;
                }
#ifdef LOG_ENABLED
                int32_t iPos      = e.xclient.data.l[XdndPositionXY];
                Atom    atmAction = m_uXdndVer >= 2 /* Actions other than "copy" or only supported since protocol version 2. */
                                  ? e.xclient.data.l[XdndPositionAction] : xAtom(XA_XdndActionCopy);
                LogFlowThisFunc(("XA_XdndPosition: wndProxy=%#x, wndCur=%#x, x=%RI32, y=%RI32, strAction=%s\n",
                                 m_wndProxy.hWnd, m_wndCur, RT_HIWORD(iPos), RT_LOWORD(iPos),
                                 xAtomToString(atmAction).c_str()));
#endif
                bool fAcceptDrop = true;

                /* Reply with a XdndStatus message to tell the source whether
                 * the data can be dropped or not. */
                XClientMessageEvent m;
                RT_ZERO(m);
                m.type         = ClientMessage;
                m.display      = m_pDisplay;
                m.window       = e.xclient.data.l[XdndPositionWindow];
                m.message_type = xAtom(XA_XdndStatus);
                m.format       = 32;
                m.data.l[XdndStatusWindow]  = m_wndProxy.hWnd;
                m.data.l[XdndStatusFlags]   = fAcceptDrop ? VBOX_XDND_STATUS_FLAG_ACCEPT : VBOX_XDND_STATUS_FLAG_NONE; /* Whether to accept the drop or not. */

                /* We don't want any new XA_XdndPosition messages while being
                 * in our proxy window. */
                m.data.l[XdndStatusNoMsgXY] = RT_MAKE_U32(m_wndProxy.iY, m_wndProxy.iX);
                m.data.l[XdndStatusNoMsgWH] = RT_MAKE_U32(m_wndProxy.iHeight, m_wndProxy.iWidth);

                /** @todo Handle default action! */
                m.data.l[XdndStatusAction]  = fAcceptDrop ? toAtomAction(VBOX_DND_ACTION_COPY) : None;

                int xRc = XSendEvent(m_pDisplay, e.xclient.data.l[XdndPositionWindow],
                                     False /* Propagate */, NoEventMask, reinterpret_cast<XEvent *>(&m));
                if (xRc == 0)
                    VBClLogError("Error sending position status event to current window %#x ('%s'): %s\n",
                                 m_wndCur, pszWndCurName, gX11->xErrorToString(xRc).c_str());
            }
            else if (   e.xclient.message_type == xAtom(XA_XdndLeave)
                     && m_wndCur               == static_cast<Window>(e.xclient.data.l[XdndLeaveWindow]))
            {
                LogFlowThisFunc(("XA_XdndLeave\n"));
                VBClLogInfo("Guest to host transfer canceled by the guest source window\n");

                /* Start over. */
                reset();
            }
            else if (   e.xclient.message_type == xAtom(XA_XdndDrop)
                     && m_wndCur               == static_cast<Window>(e.xclient.data.l[XdndDropWindow]))
            {
                LogFlowThisFunc(("XA_XdndDrop\n"));

                if (m_enmState != Dropped) /* Wrong mode? Bail out. */
                {
                    /* Can occur when dragging from guest->host, but then back in to the guest again. */
                    VBClLogInfo("Could not drop on own proxy window\n"); /* Not fatal. */

                    /* Let the source know. */
                    rc = m_wndProxy.sendFinished(m_wndCur, VBOX_DND_ACTION_IGNORE);

                    /* Start over. */
                    reset();
                    break;
                }

                m_eventQueueList.append(e);
                rc = RTSemEventSignal(m_eventQueueEvent);
            }
            else /* Unhandled event, abort. */
            {
                VBClLogInfo("Unhandled event from wnd=%#x, msg=%s\n", e.xclient.window, xAtomToString(e.xclient.message_type).c_str());

                /* Let the source know. */
                rc = m_wndProxy.sendFinished(m_wndCur, VBOX_DND_ACTION_IGNORE);

                /* Start over. */
                reset();
            }
            break;
        }

        default:
        {
            AssertMsgFailed(("Drag and drop mode not implemented: %RU32\n", m_enmMode));
            rc = VERR_NOT_IMPLEMENTED;
            break;
        }
    }

    RTStrFree(pszWndCurName);

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

int VBClX11DnDInst::onX11MotionNotify(const XEvent &e)
{
    RT_NOREF1(e);
    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));

    return VINF_SUCCESS;
}

/**
 * Callback handler for being notified if some other window now
 * is the owner of the current selection.
 *
 * @return  IPRT status code.
 * @param   e                       X11 event to handle.
 *
 * @remark
 */
int VBClX11DnDInst::onX11SelectionClear(const XEvent &e)
{
    RT_NOREF1(e);
    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));

    return VINF_SUCCESS;
}

/**
 * Callback handler for a XDnD selection notify from a window. This is needed
 * to let the us know if a certain window has drag'n drop data to share with us,
 * e.g. our proxy window.
 *
 * @return  IPRT status code.
 * @param   e                       X11 event to handle.
 */
int VBClX11DnDInst::onX11SelectionNotify(const XEvent &e)
{
    AssertReturn(e.type == SelectionNotify, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));

    int rc;

    switch (m_enmMode)
    {
        case GH:
        {
            if (m_enmState == Dropped)
            {
                m_eventQueueList.append(e);
                rc = RTSemEventSignal(m_eventQueueEvent);
            }
            else
                rc = VERR_WRONG_ORDER;
            break;
        }

        default:
        {
            LogFlowThisFunc(("Unhandled: wnd=%#x, msg=%s\n",
                             e.xclient.data.l[0], xAtomToString(e.xclient.message_type).c_str()));
            rc = VERR_INVALID_STATE;
            break;
        }
    }

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

/**
 * Callback handler for a XDnD selection request from a window. This is needed
 * to retrieve the data required to complete the actual drag'n drop operation.
 *
 * @returns IPRT status code.
 * @param   evReq               X11 event to handle.
 */
int VBClX11DnDInst::onX11SelectionRequest(const XEvent &evReq)
{
    AssertReturn(evReq.type == SelectionRequest, VERR_INVALID_PARAMETER);

    const XSelectionRequestEvent *pEvReq = &evReq.xselectionrequest;

    char *pszWndSrcName = wndX11GetNameA(pEvReq->owner);
    AssertPtrReturn(pszWndSrcName, VERR_INVALID_POINTER);
    char *pszWndTgtName = wndX11GetNameA(pEvReq->requestor);
    AssertPtrReturn(pszWndTgtName, VERR_INVALID_POINTER);

    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));
    LogFlowThisFunc(("Event owner=%#x ('%s'), requestor=%#x ('%s'), selection=%s, target=%s, prop=%s, time=%u\n",
                     pEvReq->owner, pszWndSrcName,
                     pEvReq->requestor, pszWndTgtName,
                     xAtomToString(pEvReq->selection).c_str(),
                     xAtomToString(pEvReq->target).c_str(),
                     xAtomToString(pEvReq->property).c_str(),
                     pEvReq->time));

    VBClLogInfo("Window '%s' is asking '%s' for '%s' / '%s'\n",
                pszWndTgtName, pszWndSrcName, xAtomToString(pEvReq->selection).c_str(), xAtomToString(pEvReq->property).c_str());

    RTStrFree(pszWndSrcName);
    /* Note: pszWndTgtName will be free'd below. */

    int rc;

    switch (m_enmMode)
    {
        case HG:
        {
            rc = VINF_SUCCESS;

            /*
             * Start by creating a refusal selection notify message.
             * That way we only need to care for the success case.
             */

            XEvent evResp;
            RT_ZERO(evResp);

            XSelectionEvent *pEvResp = &evResp.xselection;

            pEvResp->type      = SelectionNotify;
            pEvResp->display   = pEvReq->display;
            pEvResp->requestor = pEvReq->requestor;
            pEvResp->selection = pEvReq->selection;
            pEvResp->target    = pEvReq->target;
            pEvResp->property  = None;                          /* "None" means refusal. */
            pEvResp->time      = pEvReq->time;

            if (g_cVerbosity)
            {
                VBClLogVerbose(1, "Supported formats by VBoxClient:\n");
                for (size_t i = 0; i < m_lstAtomFormats.size(); i++)
                    VBClLogVerbose(1, "\t%s\n", xAtomToString(m_lstAtomFormats.at(i)).c_str());
            }

            /* Is the requestor asking for the possible MIME types? */
            if (pEvReq->target == xAtom(XA_TARGETS))
            {
                VBClLogInfo("Target window %#x ('%s') asking for target list\n", pEvReq->requestor, pszWndTgtName);

                /* If so, set the window property with the formats on the requestor
                 * window. */
                rc = wndXDnDSetFormatList(pEvReq->requestor, pEvReq->property, m_lstAtomFormats);
                if (RT_SUCCESS(rc))
                    pEvResp->property = pEvReq->property;
            }
            /* Is the requestor asking for a specific MIME type (we support)? */
            else if (m_lstAtomFormats.contains(pEvReq->target))
            {
                VBClLogInfo("Target window %#x ('%s') is asking for data as '%s'\n",
                            pEvReq->requestor, pszWndTgtName, xAtomToString(pEvReq->target).c_str());

#ifdef VBOX_WITH_DRAG_AND_DROP_PROMISES
# error "Implement me!"
#else
                /* Did we not drop our stuff to the guest yet? Bail out. */
                if (m_enmState != Dropped)
                {
                    VBClLogError("Data not dropped by the host on the guest yet (client state %RU32, mode %RU32), refusing selection request by guest\n",
                                 m_enmState, m_enmMode);
                }
                /* Did we not store the requestor's initial selection request yet? Then do so now. */
                else
                {
#endif /* VBOX_WITH_DRAG_AND_DROP_PROMISES */
                    /* Get the data format the requestor wants from us. */
                    VBClLogInfo("Target window %#x ('%s') requested data from host as '%s', rc=%Rrc\n",
                                pEvReq->requestor, pszWndTgtName, xAtomToString(pEvReq->target).c_str(), rc);

                    /* Make a copy of the MIME data to be passed back. The X server will be become
                     * the new owner of that data, so no deletion needed. */
                    /** @todo Do we need to do some more conversion here? XConvertSelection? */
                    AssertMsgBreakStmt(m_pvSelReqData != NULL, ("Selection request data is NULL\n"),   rc = VERR_INVALID_PARAMETER);
                    AssertMsgBreakStmt(m_cbSelReqData  > 0,    ("Selection request data size is 0\n"), rc = VERR_INVALID_PARAMETER);

                    void    const *pvData = RTMemDup(m_pvSelReqData, m_cbSelReqData);
                    AssertMsgBreakStmt(pvData != NULL, ("Duplicating selection request failed\n"), rc = VERR_NO_MEMORY);
                    uint32_t const cbData = m_cbSelReqData;

                    /* Always return the requested property. */
                    evResp.xselection.property = pEvReq->property;

                    /* Note: Always seems to return BadRequest. Seems fine. */
                    int xRc = XChangeProperty(pEvResp->display, pEvResp->requestor, pEvResp->property,
                                              pEvResp->target, 8, PropModeReplace,
                                              reinterpret_cast<const unsigned char*>(pvData), cbData);

                    LogFlowFunc(("Changing property '%s' (of type '%s') of window %#x ('%s'): %s\n",
                                 xAtomToString(pEvReq->property).c_str(),
                                 xAtomToString(pEvReq->target).c_str(),
                                 pEvReq->requestor, pszWndTgtName,
                                 gX11->xErrorToString(xRc).c_str()));
                    RT_NOREF(xRc);
#ifndef VBOX_WITH_DRAG_AND_DROP_PROMISES
                }
#endif
            }
            /* Anything else. */
            else
            {
                VBClLogError("Refusing unknown command/format '%s' of wnd=%#x ('%s')\n",
                             xAtomToString(pEvReq->target).c_str(), pEvReq->requestor, pszWndTgtName);
                rc = VERR_NOT_SUPPORTED;
            }

            VBClLogVerbose(1, "Offering type '%s', property '%s' to window %#x ('%s') ...\n",
                           xAtomToString(pEvReq->target).c_str(),
                           xAtomToString(pEvReq->property).c_str(), pEvReq->requestor, pszWndTgtName);

            int xRc = XSendEvent(pEvReq->display, pEvReq->requestor, True /* Propagate */, 0, &evResp);
            if (xRc == 0)
                VBClLogError("Error sending SelectionNotify(1) event to window %#x ('%s'): %s\n",
                             pEvReq->requestor, pszWndTgtName, gX11->xErrorToString(xRc).c_str());

            XFlush(pEvReq->display);
            break;
        }

        default:
            rc = VERR_INVALID_STATE;
            break;
    }

    RTStrFree(pszWndTgtName);
    pszWndTgtName = NULL;

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

/**
 * Handles X11 events, called by x11EventThread.
 *
 * @returns IPRT status code.
 * @param   e                       X11 event to handle.
 */
int VBClX11DnDInst::onX11Event(const XEvent &e)
{
    int rc;

    LogFlowThisFunc(("X11 event, type=%d\n", e.type));
    switch (e.type)
    {
        /*
         * This can happen if a guest->host drag operation
         * goes back from the host to the guest. This is not what
         * we want and thus resetting everything.
         */
        case ButtonPress:
            RT_FALL_THROUGH();
        case ButtonRelease:
        {
            VBClLogInfo("Mouse button %s\n", e.type == ButtonPress ? "pressed" : "released");

            reset();

            rc = VINF_SUCCESS;
            break;
        }

        case ClientMessage:
            rc = onX11ClientMessage(e);
            break;

        case SelectionClear:
            rc = onX11SelectionClear(e);
            break;

        case SelectionNotify:
            rc = onX11SelectionNotify(e);
            break;

        case SelectionRequest:
            rc = onX11SelectionRequest(e);
            break;

        case MotionNotify:
            rc = onX11MotionNotify(e);
            break;

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    LogFlowThisFunc(("rc=%Rrc\n", rc));
    return rc;
}

int VBClX11DnDInst::waitForStatusChange(uint32_t enmState, RTMSINTERVAL uTimeoutMS /* = 30000 */)
{
    const uint64_t uiStart = RTTimeMilliTS();
    volatile uint32_t enmCurState;

    int rc = VERR_TIMEOUT;

    LogFlowFunc(("enmState=%RU32, uTimeoutMS=%RU32\n", enmState, uTimeoutMS));

    do
    {
        enmCurState = ASMAtomicReadU32(&m_enmState);
        if (enmCurState == enmState)
        {
            rc = VINF_SUCCESS;
            break;
        }
    }
    while (RTTimeMilliTS() - uiStart < uTimeoutMS);

    LogFlowThisFunc(("Returning %Rrc\n", rc));
    return rc;
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
/**
 * Waits for an X11 event of a specific type.
 *
 * @returns IPRT status code.
 * @param   evX                     Reference where to store the event into.
 * @param   iType                   Event type to wait for.
 * @param   uTimeoutMS              Timeout (in ms) to wait for the event.
 */
bool VBClX11DnDInst::waitForX11Msg(XEvent &evX, int iType, RTMSINTERVAL uTimeoutMS /* = 100 */)
{
    LogFlowThisFunc(("iType=%d, uTimeoutMS=%RU32, cEventQueue=%zu\n", iType, uTimeoutMS, m_eventQueueList.size()));

    bool fFound = false;
    uint64_t const tsStartMs = RTTimeMilliTS();

    do
    {
        /* Check if there is a client message in the queue. */
        for (size_t i = 0; i < m_eventQueueList.size(); i++)
        {
            int rc2 = RTCritSectEnter(&m_eventQueueCS);
            if (RT_SUCCESS(rc2))
            {
                XEvent e = m_eventQueueList.at(i).m_Event;

                fFound = e.type == iType;
                if (fFound)
                {
                    m_eventQueueList.removeAt(i);
                    evX = e;
                }

                rc2 = RTCritSectLeave(&m_eventQueueCS);
                AssertRC(rc2);

                if (fFound)
                    break;
            }
        }

        if (fFound)
            break;

        int rc2 = RTSemEventWait(m_eventQueueEvent, 25 /* ms */);
        if (   RT_FAILURE(rc2)
            && rc2 != VERR_TIMEOUT)
        {
            LogFlowFunc(("Waiting failed with rc=%Rrc\n", rc2));
            break;
        }
    }
    while (RTTimeMilliTS() - tsStartMs < uTimeoutMS);

    LogFlowThisFunc(("Returning fFound=%RTbool, msRuntime=%RU64\n", fFound, RTTimeMilliTS() - tsStartMs));
    return fFound;
}

/**
 * Waits for an X11 client message of a specific type.
 *
 * @returns IPRT status code.
 * @param   evMsg                   Reference where to store the event into.
 * @param   aType                   Event type to wait for.
 * @param   uTimeoutMS              Timeout (in ms) to wait for the event.
 */
bool VBClX11DnDInst::waitForX11ClientMsg(XClientMessageEvent &evMsg, Atom aType,
                                       RTMSINTERVAL uTimeoutMS /* = 100 */)
{
    LogFlowThisFunc(("aType=%s, uTimeoutMS=%RU32, cEventQueue=%zu\n",
                     xAtomToString(aType).c_str(), uTimeoutMS, m_eventQueueList.size()));

    bool fFound = false;
    const uint64_t uiStart = RTTimeMilliTS();
    do
    {
        /* Check if there is a client message in the queue. */
        for (size_t i = 0; i < m_eventQueueList.size(); i++)
        {
            int rc2 = RTCritSectEnter(&m_eventQueueCS);
            if (RT_SUCCESS(rc2))
            {
                XEvent e = m_eventQueueList.at(i).m_Event;
                if (   e.type                 == ClientMessage
                    && e.xclient.message_type == aType)
                {
                    m_eventQueueList.removeAt(i);
                    evMsg = e.xclient;

                    fFound = true;
                }

                if (e.type == ClientMessage)
                {
                    LogFlowThisFunc(("Client message: Type=%ld (%s)\n",
                                     e.xclient.message_type, xAtomToString(e.xclient.message_type).c_str()));
                }
                else
                    LogFlowThisFunc(("X message: Type=%d\n", e.type));

                rc2 = RTCritSectLeave(&m_eventQueueCS);
                AssertRC(rc2);

                if (fFound)
                    break;
            }
        }

        if (fFound)
            break;

        int rc2 = RTSemEventWait(m_eventQueueEvent, 25 /* ms */);
        if (   RT_FAILURE(rc2)
            && rc2 != VERR_TIMEOUT)
        {
            LogFlowFunc(("Waiting failed with rc=%Rrc\n", rc2));
            break;
        }
    }
    while (RTTimeMilliTS() - uiStart < uTimeoutMS);

    LogFlowThisFunc(("Returning fFound=%RTbool, msRuntime=%RU64\n", fFound, RTTimeMilliTS() - uiStart));
    return fFound;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

/*
 * Host -> Guest
 */

/**
 * Host -> Guest: Event signalling that the host's (mouse) cursor just entered the VM's (guest's) display
 *                area.
 *
 * @returns IPRT status code.
 * @param   lstFormats              List of supported formats from the host.
 * @param   dndListActionsAllowed   (ORed) List of supported actions from the host.
 */
int VBClX11DnDInst::hgEnter(const RTCList<RTCString> &lstFormats, uint32_t dndListActionsAllowed)
{
    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));

    if (m_enmMode != Unknown)
        return VERR_INVALID_STATE;

    reset();

#ifdef DEBUG
    LogFlowThisFunc(("dndListActionsAllowed=0x%x, lstFormats=%zu: ", dndListActionsAllowed, lstFormats.size()));
    for (size_t i = 0; i < lstFormats.size(); ++i)
        LogFlow(("'%s' ", lstFormats.at(i).c_str()));
    LogFlow(("\n"));
#endif

    int rc;

    do
    {
        /* Check if the VM session has changed and reconnect to the HGCM service if necessary. */
        rc = checkForSessionChange();
        AssertRCBreak(rc);

        /* Append all actual (MIME) formats we support to the list.
         * These must come last, after the default Atoms above. */
        rc = appendFormatsToList(lstFormats, m_lstAtomFormats);
        AssertRCBreak(rc);

        rc = wndXDnDSetFormatList(m_wndProxy.hWnd, xAtom(XA_XdndTypeList), m_lstAtomFormats);
        AssertRCBreak(rc);

        /* Announce the possible actions. */
        VBoxDnDAtomList lstActions;
        rc = toAtomActions(dndListActionsAllowed, lstActions);
        AssertRCBreak(rc);

        rc = wndXDnDSetActionList(m_wndProxy.hWnd, lstActions);
        AssertRCBreak(rc);

        /* Set the DnD selection owner to our window. */
        /** @todo Don't use CurrentTime -- according to ICCCM section 2.1. */
        XSetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection), m_wndProxy.hWnd, CurrentTime);

        if (g_cVerbosity)
        {
            RTCString strMsg("Enter: Host -> Guest\n");
            strMsg += RTCStringFmt("Allowed actions: ");
            for (size_t i = 0; i < lstActions.size(); i++)
            {
                if (i > 0)
                    strMsg += ", ";
                strMsg += DnDActionToStr(toHGCMAction(lstActions.at(i)));
            }
            strMsg += " - Formats: ";
            for (size_t i = 0; i < lstFormats.size(); i++)
            {
                if (i > 0)
                    strMsg += ", ";
                strMsg += lstFormats.at(i);
            }

            VBClShowNotify(VBOX_DND_SHOWNOTIFY_HEADER, strMsg.c_str());
        }

        m_enmMode  = HG;
        m_enmState = Dragging;

    } while (0);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest: Event signalling that the host's (mouse) cursor has left the VM's (guest's)
 *                display area.
 */
int VBClX11DnDInst::hgLeave(void)
{
    if (g_cVerbosity)
        VBClShowNotify(VBOX_DND_SHOWNOTIFY_HEADER, "Leave: Host -> Guest");

    if (m_enmMode == HG) /* Only reset if in the right operation mode. */
        reset();

    return VINF_SUCCESS;
}

/**
 * Host -> Guest: Event signalling that the host's (mouse) cursor has been moved within the VM's
 *                (guest's) display area.
 *
 * @returns IPRT status code.
 * @param   uPosX                   Relative X position within the guest's display area.
 * @param   uPosY                   Relative Y position within the guest's display area.
 * @param   dndActionDefault        Default action the host wants to perform on the guest
 *                                  as soon as the operation successfully finishes.
 */
int VBClX11DnDInst::hgMove(uint32_t uPosX, uint32_t uPosY, VBOXDNDACTION dndActionDefault)
{
    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));
    LogFlowThisFunc(("uPosX=%RU32, uPosY=%RU32, dndActionDefault=0x%x\n", uPosX, uPosY, dndActionDefault));

    if (   m_enmMode  != HG
        || m_enmState != Dragging)
    {
        return VERR_INVALID_STATE;
    }

    int rc  = VINF_SUCCESS;
    int xRc = Success;

    /* Move the mouse cursor within the guest. */
    mouseCursorMove(uPosX, uPosY);

    /* Search for the application window below the cursor. */
    Window wndBelowCursor       = gX11->applicationWindowBelowCursor(m_wndRoot);
    char *pszWndBelowCursorName = wndX11GetNameA(wndBelowCursor);
    AssertPtrReturn(pszWndBelowCursorName, VERR_NO_MEMORY);

    uint8_t uBelowCursorXdndVer = 0; /* 0 means the current window is _not_ XdndAware. */

    if (wndBelowCursor != None)
    {
        /* Temp stuff for the XGetWindowProperty call. */
        Atom atmTmp;
        int fmt;
        unsigned long cItems, cbRemaining;
        unsigned char *pcData = NULL;

        /* Query the XdndAware property from the window. We are interested in
         * the version and if it is XdndAware at all. */
        xRc = XGetWindowProperty(m_pDisplay, wndBelowCursor, xAtom(XA_XdndAware),
                                 0, 2, False, AnyPropertyType,
                                 &atmTmp, &fmt, &cItems, &cbRemaining, &pcData);
        if (xRc != Success)
        {
            VBClLogError("Error getting properties of cursor window=%#x: %s\n", wndBelowCursor, gX11->xErrorToString(xRc).c_str());
        }
        else
        {
            if (pcData == NULL || fmt != 32 || cItems != 1)
            {
                /** @todo Do we need to deal with this? */
                VBClLogError("Wrong window properties for window %#x: pcData=%#x, iFmt=%d, cItems=%ul\n",
                             wndBelowCursor, pcData, fmt, cItems);
            }
            else
            {
                /* Get the current window's Xdnd version. */
                uBelowCursorXdndVer = (uint8_t)reinterpret_cast<long *>(pcData)[0];
            }

            XFree(pcData);
        }
    }

    char *pszWndCurName = wndX11GetNameA(m_wndCur);
    AssertPtrReturn(pszWndCurName, VERR_NO_MEMORY);

    LogFlowThisFunc(("wndCursor=%x ('%s', Xdnd version %u), wndCur=%x ('%s', Xdnd version %u)\n",
                     wndBelowCursor, pszWndBelowCursorName, uBelowCursorXdndVer, m_wndCur, pszWndCurName, m_uXdndVer));

    if (   wndBelowCursor != m_wndCur
        && m_uXdndVer)
    {
        VBClLogInfo("Left old window %#x ('%s'), supported Xdnd version %u\n", m_wndCur, pszWndCurName, m_uXdndVer);

        /* We left the current XdndAware window. Announce this to the current indow. */
        XClientMessageEvent m;
        RT_ZERO(m);
        m.type                    = ClientMessage;
        m.display                 = m_pDisplay;
        m.window                  = m_wndCur;
        m.message_type            = xAtom(XA_XdndLeave);
        m.format                  = 32;
        m.data.l[XdndLeaveWindow] = m_wndProxy.hWnd;

        xRc = XSendEvent(m_pDisplay, m_wndCur, False, NoEventMask, reinterpret_cast<XEvent*>(&m));
        if (xRc == 0)
            VBClLogError("Error sending leave event to old window %#x: %s\n", m_wndCur, gX11->xErrorToString(xRc).c_str());

        /* Reset our current window. */
        m_wndCur   = 0;
        m_uXdndVer = 0;
    }

    /*
     * Do we have a new Xdnd-aware window which now is under the cursor?
     */
    if (   wndBelowCursor != m_wndCur
        && uBelowCursorXdndVer)
    {
        VBClLogInfo("Entered new window %#x ('%s'), supports Xdnd version=%u\n",
                    wndBelowCursor, pszWndBelowCursorName, uBelowCursorXdndVer);

        /*
         * We enter a new window. Announce the XdndEnter event to the new
         * window. The first three mime types are attached to the event (the
         * others could be requested by the XdndTypeList property from the
         * window itself).
         */
        XClientMessageEvent m;
        RT_ZERO(m);
        m.type         = ClientMessage;
        m.display      = m_pDisplay;
        m.window       = wndBelowCursor;
        m.message_type = xAtom(XA_XdndEnter);
        m.format       = 32;
        m.data.l[XdndEnterWindow] = m_wndProxy.hWnd;
        m.data.l[XdndEnterFlags]  = RT_MAKE_U32_FROM_U8(
                                    /* Bit 0 is set if the source supports more than three data types. */
                                    m_lstAtomFormats.size() > 3 ? RT_BIT(0) : 0,
                                    /* Reserved for future use. */
                                    0, 0,
                                    /* Protocol version to use. */
                                    RT_MIN(VBOX_XDND_VERSION, uBelowCursorXdndVer));
        m.data.l[XdndEnterType1]  = m_lstAtomFormats.value(0, None); /* First data type to use. */
        m.data.l[XdndEnterType2]  = m_lstAtomFormats.value(1, None); /* Second data type to use. */
        m.data.l[XdndEnterType3]  = m_lstAtomFormats.value(2, None); /* Third data type to use. */

        xRc = XSendEvent(m_pDisplay, wndBelowCursor, False, NoEventMask, reinterpret_cast<XEvent*>(&m));
        if (xRc == 0)
            VBClLogError("Error sending enter event to window %#x: %s\n", wndBelowCursor, gX11->xErrorToString(xRc).c_str());
    }

    if (uBelowCursorXdndVer)
    {
        Assert(wndBelowCursor != None);

        Atom atmAction = toAtomAction(dndActionDefault);
        LogFlowThisFunc(("strAction=%s\n", xAtomToString(atmAction).c_str()));

        VBClLogInfo("Sent position event (%RU32 x %RU32) to window %#x ('%s') with actions '%s'\n",
                    uPosX, uPosY, wndBelowCursor, pszWndBelowCursorName, xAtomToString(atmAction).c_str());

        /*
         * Send a XdndPosition event with the proposed action to the guest.
         */
        XClientMessageEvent m;
        RT_ZERO(m);
        m.type         = ClientMessage;
        m.display      = m_pDisplay;
        m.window       = wndBelowCursor;
        m.message_type = xAtom(XA_XdndPosition);
        m.format       = 32;
        m.data.l[XdndPositionWindow]    = m_wndProxy.hWnd;               /* X window ID of source window. */
        m.data.l[XdndPositionFlags]     = 0;                             /* Reserved, set to 0. */
        m.data.l[XdndPositionXY]        = RT_MAKE_U32(uPosY, uPosX);     /* Cursor coordinates relative to the root window. */
        m.data.l[XdndPositionTimeStamp] = CurrentTime;                   /* Timestamp for retrieving data. */
        m.data.l[XdndPositionAction]    = atmAction;                     /* Actions requested by the user. */

        xRc = XSendEvent(m_pDisplay, wndBelowCursor, False, NoEventMask, reinterpret_cast<XEvent*>(&m));
        if (xRc == 0)
            VBClLogError("Error sending position event to current window %#x: %s\n", wndBelowCursor, gX11->xErrorToString(xRc).c_str());
    }

    if (uBelowCursorXdndVer == 0)
    {
        /* No window to process, so send a ignore ack event to the host. */
        rc = VbglR3DnDHGSendAckOp(&m_dndCtx, VBOX_DND_ACTION_IGNORE);
    }
    else
    {
        Assert(wndBelowCursor != None);

        m_wndCur   = wndBelowCursor;
        m_uXdndVer = uBelowCursorXdndVer;
    }

    RTStrFree(pszWndBelowCursorName);
    RTStrFree(pszWndCurName);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest: Event signalling that the host has dropped the data over the VM (guest) window.
 *
 * @returns IPRT status code.
 * @param   uPosX                   Relative X position within the guest's display area.
 * @param   uPosY                   Relative Y position within the guest's display area.
 * @param   dndActionDefault        Default action the host wants to perform on the guest
 *                                  as soon as the operation successfully finishes.
 */
int VBClX11DnDInst::hgDrop(uint32_t uPosX, uint32_t uPosY, VBOXDNDACTION dndActionDefault)
{
    RT_NOREF3(uPosX, uPosY, dndActionDefault);
    LogFlowThisFunc(("wndCur=%RU32, wndProxy=%RU32, mode=%RU32, state=%RU32\n", m_wndCur, m_wndProxy.hWnd, m_enmMode, m_enmState));
    LogFlowThisFunc(("uPosX=%RU32, uPosY=%RU32, dndActionDefault=0x%x\n", uPosX, uPosY, dndActionDefault));

    if (   m_enmMode  != HG
        || m_enmState != Dragging)
    {
        return VERR_INVALID_STATE;
    }

    /* Set the state accordingly. */
    m_enmState = Dropped;

    /*
     * Ask the host to send the raw data, as we don't (yet) know which format
     * the guest exactly expects. As blocking in a SelectionRequest message turned
     * out to be very unreliable (e.g. with KDE apps) we request to start transferring
     * file/directory data (if any) here.
     */
    char szFormat[] = { "text/uri-list" };

    int rc = VbglR3DnDHGSendReqData(&m_dndCtx, szFormat);
    VBClLogInfo("Drop event from host resulted in: %Rrc\n", rc);

    if (g_cVerbosity)
        VBClShowNotify(VBOX_DND_SHOWNOTIFY_HEADER, "Drop: Host -> Guest");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest: Event signalling that the host has finished sending drag'n drop
 *                data to the guest for further processing.
 *
 * @returns IPRT status code.
 * @param   pMeta               Pointer to meta data from host.
 */
int VBClX11DnDInst::hgDataReceive(PVBGLR3GUESTDNDMETADATA pMeta)
{
    LogFlowThisFunc(("enmMode=%RU32, enmState=%RU32\n", m_enmMode, m_enmState));
    LogFlowThisFunc(("enmMetaType=%RU32\n", pMeta->enmType));

    if (   m_enmMode  != HG
        || m_enmState != Dropped)
    {
        return VERR_INVALID_STATE;
    }

    void  *pvData = NULL;
    size_t cbData = 0;

    int rc = VINF_SUCCESS; /* Shut up GCC. */

    switch (pMeta->enmType)
    {
        case VBGLR3GUESTDNDMETADATATYPE_RAW:
        {
            AssertBreakStmt(pMeta->u.Raw.pvMeta != NULL, rc = VERR_INVALID_POINTER);
            pvData = pMeta->u.Raw.pvMeta;
            AssertBreakStmt(pMeta->u.Raw.cbMeta, rc = VERR_INVALID_PARAMETER);
            cbData = pMeta->u.Raw.cbMeta;

            rc = VINF_SUCCESS;
            break;
        }

        case VBGLR3GUESTDNDMETADATATYPE_URI_LIST:
        {
            const char *pcszRootPath = DnDTransferListGetRootPathAbs(&pMeta->u.URI.Transfer);
            AssertPtrBreakStmt(pcszRootPath, VERR_INVALID_POINTER);

            VBClLogInfo("Transfer list root directory is '%s'\n", pcszRootPath);

            /* Note: Use the URI format here, as X' DnD spec says so. */
            rc = DnDTransferListGetRootsEx(&pMeta->u.URI.Transfer, DNDTRANSFERLISTFMT_URI, pcszRootPath,
                                           DND_PATH_SEPARATOR_STR, (char **)&pvData, &cbData);
            break;
        }

        default:
            AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
            break;
    }

    if (RT_FAILURE(rc))
        return rc;

    /*
     * At this point all data needed (including sent files/directories) should
     * be on the guest, so proceed working on communicating with the target window.
     */
    VBClLogInfo("Received %RU32 bytes of meta data from host\n", cbData);

    /* Destroy any old data. */
    if (m_pvSelReqData)
    {
        Assert(m_cbSelReqData);

        RTMemFree(m_pvSelReqData); /** @todo RTMemRealloc? */
        m_cbSelReqData = 0;
    }

    /** @todo Handle incremental transfers. */

    /* Make a copy of the data. This data later then will be used to fill into
     * the selection request. */
    if (cbData)
    {
        m_pvSelReqData = RTMemAlloc(cbData);
        if (!m_pvSelReqData)
            return VERR_NO_MEMORY;

        memcpy(m_pvSelReqData, pvData, cbData);
        m_cbSelReqData = cbData;
    }

    /*
     * Send a drop event to the current window (target).
     * This window in turn then will raise a SelectionRequest message to our proxy window,
     * which we will handle in our onX11SelectionRequest handler.
     *
     * The SelectionRequest will tell us in which format the target wants the data from the host.
     */
    XClientMessageEvent m;
    RT_ZERO(m);
    m.type         = ClientMessage;
    m.display      = m_pDisplay;
    m.window       = m_wndCur;
    m.message_type = xAtom(XA_XdndDrop);
    m.format       = 32;
    m.data.l[XdndDropWindow]    = m_wndProxy.hWnd;  /* Source window. */
    m.data.l[XdndDropFlags]     = 0;                /* Reserved for future use. */
    m.data.l[XdndDropTimeStamp] = CurrentTime;      /* Our DnD data does not rely on any timing, so just use the current time. */

    int xRc = XSendEvent(m_pDisplay, m_wndCur, False /* Propagate */, NoEventMask, reinterpret_cast<XEvent*>(&m));
    if (xRc == 0)
        VBClLogError("Error sending XA_XdndDrop event to window=%#x: %s\n", m_wndCur, gX11->xErrorToString(xRc).c_str());
    XFlush(m_pDisplay);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Checks if the VM session has changed (can happen when restoring the VM from a saved state)
 * and do a reconnect to the DnD HGCM service.
 *
 * @returns IPRT status code.
 */
int VBClX11DnDInst::checkForSessionChange(void)
{
    uint64_t uSessionID;
    int rc = VbglR3GetSessionId(&uSessionID);
    if (   RT_SUCCESS(rc)
        && uSessionID != m_dndCtx.uSessionID)
    {
        LogFlowThisFunc(("VM session has changed to %RU64\n", uSessionID));

        rc = VbglR3DnDDisconnect(&m_dndCtx);
        AssertRC(rc);

        rc = VbglR3DnDConnect(&m_dndCtx);
        AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
/**
 * Guest -> Host: Event signalling that the host is asking whether there is a pending
 *                drag event on the guest (to the host).
 *
 * @returns IPRT status code.
 */
int VBClX11DnDInst::ghIsDnDPending(void)
{
    LogFlowThisFunc(("mode=%RU32, state=%RU32\n", m_enmMode, m_enmState));

    int rc;

    RTCString         strFormats       = DND_PATH_SEPARATOR_STR; /** @todo If empty, IOCTL fails with VERR_ACCESS_DENIED. */
    VBOXDNDACTION     dndActionDefault = VBOX_DND_ACTION_IGNORE;
    VBOXDNDACTIONLIST dndActionList    = VBOX_DND_ACTION_IGNORE;

    /* Currently in wrong mode? Bail out. */
    if (m_enmMode == HG)
    {
        rc = VERR_INVALID_STATE;
    }
    /* Message already processed successfully? */
    else if (   m_enmMode  == GH
             && (   m_enmState == Dragging
                 || m_enmState == Dropped)
            )
    {
        /* No need to query for the source window again. */
        rc = VINF_SUCCESS;
    }
    else
    {
        /* Check if the VM session has changed and reconnect to the HGCM service if necessary. */
        rc = checkForSessionChange();

        /* Determine the current window which currently has the XdndSelection set. */
        Window wndSel = XGetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection));
        LogFlowThisFunc(("wndSel=%#x, wndProxy=%#x, wndCur=%#x\n", wndSel, m_wndProxy.hWnd, m_wndCur));

        /* Is this another window which has a Xdnd selection and not our proxy window? */
        if (   RT_SUCCESS(rc)
            && wndSel
            && wndSel != m_wndCur)
        {
            char *pszWndSelName = wndX11GetNameA(wndSel);
            AssertPtrReturn(pszWndSelName, VERR_NO_MEMORY);
            VBClLogInfo("New guest source window %#x ('%s')\n", wndSel, pszWndSelName);

            /* Start over. */
            reset();

            /* Map the window on the current cursor position, which should provoke
             * an XdndEnter event. */
            rc = proxyWinShow();
            if (RT_SUCCESS(rc))
            {
                rc = mouseCursorFakeMove();
                if (RT_SUCCESS(rc))
                {
                    bool fWaitFailed = false; /* Waiting for status changed failed? */

                    /* Wait until we're in "Dragging" state. */
                    rc = waitForStatusChange(Dragging, 100 /* 100ms timeout */);

                    /*
                     * Note: Don't wait too long here, as this mostly will make
                     *       the drag and drop experience on the host being laggy
                     *       and unresponsive.
                     *
                     *       Instead, let the host query multiple times with 100ms
                     *       timeout each (see above) and only report an error if
                     *       the overall querying time has been exceeded.<
                     */
                    if (RT_SUCCESS(rc))
                    {
                        m_enmMode = GH;
                    }
                    else if (rc == VERR_TIMEOUT)
                    {
                        /** @todo Make m_cFailedPendingAttempts configurable. For slower window managers? */
                        if (m_cFailedPendingAttempts++ > 50) /* Tolerate up to 5s total (100ms for each slot). */
                            fWaitFailed = true;
                        else
                            rc = VINF_SUCCESS;
                    }
                    else if (RT_FAILURE(rc))
                        fWaitFailed = true;

                    if (fWaitFailed)
                    {
                        VBClLogError("Error mapping proxy window to guest source window %#x ('%s'), rc=%Rrc\n",
                                     wndSel, pszWndSelName, rc);

                        /* Reset the counter in any case. */
                        m_cFailedPendingAttempts = 0;
                    }
                }
            }

            RTStrFree(pszWndSelName);
        }
        else
            VBClLogInfo("No guest source window\n");
    }

    /*
     * Acknowledge to the host in any case, regardless
     * if something failed here or not. Be responsive.
     */

    int rc2 = RTCritSectEnter(&m_dataCS);
    if (RT_SUCCESS(rc2))
    {
        /* Filter out the default X11-specific formats (required for Xdnd, 'TARGET' / 'MULTIPLE');
         * those will not be supported by VirtualBox. */
        VBoxDnDAtomList const lstAtomFormatsFiltered = gX11->xAtomListFiltered(m_lstAtomFormats, m_lstAtomFormatsX11);

        /* Anything left to report to the host? */
        if (lstAtomFormatsFiltered.size())
        {
            strFormats       = gX11->xAtomListToString(lstAtomFormatsFiltered);
            dndActionDefault = VBOX_DND_ACTION_COPY; /** @todo Handle default action! */
            dndActionList    = VBOX_DND_ACTION_COPY; /** @todo Ditto. */
            dndActionList   |= toHGCMActions(m_lstAtomActions);
        }

        RTCritSectLeave(&m_dataCS);
    }

    if (g_cVerbosity)
    {
        char *pszActions = DnDActionListToStrA(dndActionList);
        AssertPtrReturn(pszActions, VERR_NO_MEMORY);
        VBClLogVerbose(1, "Reporting formats '%s' (actions '%s' / %#x, default action is '%s' (%#x)\n",
                       strFormats.c_str(), pszActions, dndActionList, DnDActionToStr(dndActionDefault), dndActionDefault);
        RTStrFree(pszActions);
    }

    rc2 = VbglR3DnDGHSendAckPending(&m_dndCtx, dndActionDefault, dndActionList,
                                    strFormats.c_str(), strFormats.length() + 1 /* Include termination */);
    LogFlowThisFunc(("uClientID=%RU32, dndActionDefault=0x%x, dndActionList=0x%x, strFormats=%s, rc=%Rrc\n",
                     m_dndCtx.uClientID, dndActionDefault, dndActionList, strFormats.c_str(), rc2));
    if (RT_FAILURE(rc2))
    {
        switch (rc2)
        {
            case VERR_ACCESS_DENIED:
            {
                rc = VBClShowNotify(VBOX_DND_SHOWNOTIFY_HEADER,
                                    "Drag and drop to the host either is not supported or disabled. "
                                    "Please enable Guest to Host or Bidirectional drag and drop mode "
                                    "or re-install the VirtualBox Guest Additions.");
                AssertRC(rc);
                break;
            }

            default:
                break;
        }

        VBClLogError("Error reporting pending drag and drop operation status to host: %Rrc\n", rc2);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Guest -> Host: Event signalling that the host has dropped the item(s) on the
 *                host side.
 *
 * @returns IPRT status code.
 * @param   strFormat               Requested format to send to the host.
 * @param   dndActionRequested      Requested action to perform on the guest.
 */
int VBClX11DnDInst::ghDropped(const RTCString &strFormat, VBOXDNDACTION dndActionRequested)
{
    LogFlowThisFunc(("mode=%RU32, state=%RU32, strFormat=%s, dndActionRequested=0x%x\n",
                     m_enmMode, m_enmState, strFormat.c_str(), dndActionRequested));

    /* Currently in wrong mode? Bail out. */
    if (   m_enmMode == Unknown
        || m_enmMode == HG)
    {
        return VERR_INVALID_STATE;
    }

    if (   m_enmMode  == GH
        && m_enmState != Dragging)
    {
        return VERR_INVALID_STATE;
    }

    int rc = VINF_SUCCESS;

    m_enmState = Dropped;

#ifdef DEBUG
    XWindowAttributes xwa;
    XGetWindowAttributes(m_pDisplay, m_wndCur, &xwa);
    LogFlowThisFunc(("wndProxy=%RU32, wndCur=%RU32, x=%d, y=%d, width=%d, height=%d\n",
                     m_wndProxy.hWnd, m_wndCur, xwa.x, xwa.y, xwa.width, xwa.height));

    Window wndSelection = XGetSelectionOwner(m_pDisplay, xAtom(XA_XdndSelection));
    LogFlowThisFunc(("wndSelection=%#x\n", wndSelection));
#endif

    /* We send a fake mouse move event to the current window, cause
     * this should have the grab. */
    mouseCursorFakeMove();

    /**
     * The fake button release event above should lead to a XdndDrop event from the
     * source window. Because of showing our proxy window, other Xdnd events can
     * occur before, e.g. a XdndPosition event. We are not interested
     * in those, so just try to get the right one.
     */

    XClientMessageEvent evDnDDrop;
    bool fDrop = waitForX11ClientMsg(evDnDDrop, xAtom(XA_XdndDrop), 5 * 1000 /* 5s timeout */);
    if (fDrop)
    {
        LogFlowThisFunc(("XA_XdndDrop\n"));

        /* Request to convert the selection in the specific format and
         * place it to our proxy window as property. */
        Assert(evDnDDrop.message_type == xAtom(XA_XdndDrop));

        Window wndSource = evDnDDrop.data.l[XdndDropWindow]; /* Source window which has sent the message. */
        Assert(wndSource == m_wndCur);

        Atom aFormat     = gX11->stringToxAtom(strFormat.c_str());

        Time tsDrop;
        if (m_uXdndVer >= 1)
            tsDrop = evDnDDrop.data.l[XdndDropTimeStamp];
        else
            tsDrop = CurrentTime;

        XConvertSelection(m_pDisplay, xAtom(XA_XdndSelection), aFormat, xAtom(XA_XdndSelection),
                          m_wndProxy.hWnd, tsDrop);

        /* Wait for the selection notify event. */
        XEvent evSelNotify;
        RT_ZERO(evSelNotify);
        if (waitForX11Msg(evSelNotify, SelectionNotify, 5 * 1000 /* 5s timeout */))
        {
            bool fCancel = false;

            /* Make some paranoid checks. */
            if (   evSelNotify.xselection.type      == SelectionNotify
                && evSelNotify.xselection.display   == m_pDisplay
                && evSelNotify.xselection.selection == xAtom(XA_XdndSelection)
                && evSelNotify.xselection.requestor == m_wndProxy.hWnd
                && evSelNotify.xselection.target    == aFormat)
            {
                LogFlowThisFunc(("Selection notfiy (from wnd=%#x)\n", m_wndCur));

                Atom aPropType;
                int iPropFormat;
                unsigned long cItems, cbRemaining;
                unsigned char *pcData = NULL;
                int xRc = XGetWindowProperty(m_pDisplay, m_wndProxy.hWnd,
                                             xAtom(XA_XdndSelection)  /* Property */,
                                             0                        /* Offset */,
                                             VBOX_MAX_XPROPERTIES     /* Length of 32-bit multiples */,
                                             True                     /* Delete property? */,
                                             AnyPropertyType,         /* Property type */
                                             &aPropType, &iPropFormat, &cItems, &cbRemaining, &pcData);
                if (xRc != Success)
                    VBClLogError("Error getting XA_XdndSelection property of proxy window=%#x: %s\n",
                                 m_wndProxy.hWnd, gX11->xErrorToString(xRc).c_str());

                LogFlowThisFunc(("strType=%s, iPropFormat=%d, cItems=%RU32, cbRemaining=%RU32\n",
                                 gX11->xAtomToString(aPropType).c_str(), iPropFormat, cItems, cbRemaining));

                if (   aPropType   != None
                    && pcData      != NULL
                    && iPropFormat >= 8
                    && cItems      >  0
                    && cbRemaining == 0)
                {
                    size_t cbData = cItems * (iPropFormat / 8);
                    LogFlowThisFunc(("cbData=%zu\n", cbData));

                    /* For whatever reason some of the string MIME types are not
                     * zero terminated. Check that and correct it when necessary,
                     * because the guest side wants this in any case. */
                    if (   m_lstAllowedFormats.contains(strFormat)
                        && pcData[cbData - 1] != '\0')
                    {
                        unsigned char *pvDataTmp = static_cast<unsigned char*>(RTMemAlloc(cbData + 1));
                        if (pvDataTmp)
                        {
                            memcpy(pvDataTmp, pcData, cbData);
                            pvDataTmp[cbData++] = '\0';

                            rc = VbglR3DnDGHSendData(&m_dndCtx, strFormat.c_str(), pvDataTmp, cbData);
                            RTMemFree(pvDataTmp);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    else
                    {
                        /* Send the raw data to the host. */
                        rc = VbglR3DnDGHSendData(&m_dndCtx, strFormat.c_str(), pcData, cbData);
                        LogFlowThisFunc(("Sent strFormat=%s, rc=%Rrc\n", strFormat.c_str(), rc));
                    }

                    if (RT_SUCCESS(rc))
                    {
                        rc = m_wndProxy.sendFinished(wndSource, dndActionRequested);
                    }
                    else
                        fCancel = true;
                }
                else
                {
                    if (aPropType == xAtom(XA_INCR))
                    {
                        /** @todo Support incremental transfers. */
                        AssertMsgFailed(("Incremental transfers are not supported yet\n"));

                        VBClLogError("Incremental transfers are not supported yet\n");
                        rc = VERR_NOT_IMPLEMENTED;
                    }
                    else
                    {
                        VBClLogError("Not supported data type: %s\n", gX11->xAtomToString(aPropType).c_str());
                        rc = VERR_NOT_SUPPORTED;
                    }

                    fCancel = true;
                }

                if (fCancel)
                {
                    VBClLogInfo("Cancelling dropping to host\n");

                    /* Cancel the operation -- inform the source window by
                     * sending a XdndFinished message so that the source can toss the required data. */
                    rc = m_wndProxy.sendFinished(wndSource, VBOX_DND_ACTION_IGNORE);
                }

                /* Cleanup. */
                if (pcData)
                    XFree(pcData);
            }
            else
                rc = VERR_INVALID_PARAMETER;
        }
        else
            rc = VERR_TIMEOUT;
    }
    else
        rc = VERR_TIMEOUT;

    /* Inform the host on error. */
    if (RT_FAILURE(rc))
    {
        int rc2 = VbglR3DnDSendError(&m_dndCtx, rc);
        LogFlowThisFunc(("Sending error %Rrc to host resulted in %Rrc\n", rc, rc2)); RT_NOREF(rc2);
        /* This is not fatal for us, just ignore. */
    }

    /* At this point, we have either successfully transfered any data or not.
     * So reset our internal state because we are done here for the current (ongoing)
     * drag and drop operation. */
    reset();

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

/*
 * Helpers
 */

/**
 * Fakes moving the mouse cursor to provoke various drag and drop
 * events such as entering a target window or moving within a
 * source window.
 *
 * Not the most elegant and probably correct function, but does
 * the work for now.
 *
 * @returns IPRT status code.
 */
int VBClX11DnDInst::mouseCursorFakeMove(void)
{
    int iScreenID = XDefaultScreen(m_pDisplay);
    /** @todo What about multiple screens? Test this! */

    const int iScrX = XDisplayWidth(m_pDisplay, iScreenID);
    const int iScrY = XDisplayHeight(m_pDisplay, iScreenID);

    int fx, fy, rx, ry;
    Window wndTemp, wndChild;
    int wx, wy; unsigned int mask;
    XQueryPointer(m_pDisplay, m_wndRoot, &wndTemp, &wndChild, &rx, &ry, &wx, &wy, &mask);

    /*
     * Apply some simple clipping and change the position slightly.
     */

    /* FakeX */
    if      (rx == 0)     fx = 1;
    else if (rx == iScrX) fx = iScrX - 1;
    else                  fx = rx + 1;

    /* FakeY */
    if      (ry == 0)     fy = 1;
    else if (ry == iScrY) fy = iScrY - 1;
    else                  fy = ry + 1;

    /*
     * Move the cursor to trigger the wanted events.
     */
    LogFlowThisFunc(("cursorRootX=%d, cursorRootY=%d\n", fx, fy));
    int rc = mouseCursorMove(fx, fy);
    if (RT_SUCCESS(rc))
    {
        /* Move the cursor back to its original position. */
        rc = mouseCursorMove(rx, ry);
    }

    return rc;
}

/**
 * Moves the mouse pointer to a specific position.
 *
 * @returns IPRT status code.
 * @param   iPosX                   Absolute X coordinate.
 * @param   iPosY                   Absolute Y coordinate.
 */
int VBClX11DnDInst::mouseCursorMove(int iPosX, int iPosY)
{
    int const iScreenID = XDefaultScreen(m_pDisplay);
    /** @todo What about multiple screens? Test this! */

    int const iScreenWidth  = XDisplayWidth (m_pDisplay, iScreenID);
    int const iScreenHeight = XDisplayHeight(m_pDisplay, iScreenID);

    iPosX = RT_CLAMP(iPosX, 0, iScreenWidth);
    iPosY = RT_CLAMP(iPosY, 0, iScreenHeight);

    /* Same mouse position as before? No need to do anything. */
    if (   m_lastMouseX == iPosX
        && m_lastMouseY == iPosY)
    {
        return VINF_SUCCESS;
    }

    LogFlowThisFunc(("iPosX=%d, iPosY=%d, m_wndRoot=%#x\n", iPosX, iPosY, m_wndRoot));

    /* Move the guest pointer to the DnD position, so we can find the window
     * below that position. */
    int xRc = XWarpPointer(m_pDisplay, None, m_wndRoot, 0, 0, 0, 0, iPosX, iPosY);
    if (xRc == Success)
    {
        XFlush(m_pDisplay);

        m_lastMouseX = iPosX;
        m_lastMouseY = iPosY;
    }
    else
        VBClLogError("Moving mouse cursor failed: %s", gX11->xErrorToString(xRc).c_str());

    return VINF_SUCCESS;
}

/**
 * Sends a mouse button event to a specific window.
 *
 * @param   wndDest                 Window to send the mouse button event to.
 * @param   rx                      X coordinate relative to the root window's origin.
 * @param   ry                      Y coordinate relative to the root window's origin.
 * @param   iButton                 Mouse button to press/release.
 * @param   fPress                  Whether to press or release the mouse button.
 */
void VBClX11DnDInst::mouseButtonSet(Window wndDest, int rx, int ry, int iButton, bool fPress)
{
    LogFlowThisFunc(("wndDest=%#x, rx=%d, ry=%d, iBtn=%d, fPress=%RTbool\n",
                     wndDest, rx, ry, iButton, fPress));

#ifdef VBOX_DND_WITH_XTEST
    /** @todo Make this check run only once. */
    int ev, er, ma, mi;
    if (XTestQueryExtension(m_pDisplay, &ev, &er, &ma, &mi))
    {
        LogFlowThisFunc(("XText extension available\n"));

        int xRc = XTestFakeButtonEvent(m_pDisplay, 1, fPress ? True : False, CurrentTime);
        if (Rc == 0)
            VBClLogError("Error sending XTestFakeButtonEvent event: %s\n", gX11->xErrorToString(xRc).c_str());
        XFlush(m_pDisplay);
    }
    else
    {
#endif
        LogFlowThisFunc(("Note: XText extension not available or disabled\n"));

        unsigned int mask = 0;

        if (   rx == -1
            && ry == -1)
        {
            Window wndRoot, wndChild;
            int wx, wy;
            XQueryPointer(m_pDisplay, m_wndRoot, &wndRoot, &wndChild, &rx, &ry, &wx, &wy, &mask);
            LogFlowThisFunc(("Mouse pointer is at root x=%d, y=%d\n", rx, ry));
        }

        XButtonEvent eBtn;
        RT_ZERO(eBtn);

        eBtn.display      = m_pDisplay;
        eBtn.root         = m_wndRoot;
        eBtn.window       = wndDest;
        eBtn.subwindow    = None;
        eBtn.same_screen  = True;
        eBtn.time         = CurrentTime;
        eBtn.button       = iButton;
        eBtn.state        = mask | (iButton == 1 ? Button1MotionMask :
                                    iButton == 2 ? Button2MotionMask :
                                    iButton == 3 ? Button3MotionMask :
                                    iButton == 4 ? Button4MotionMask :
                                    iButton == 5 ? Button5MotionMask : 0);
        eBtn.type         = fPress ? ButtonPress : ButtonRelease;
        eBtn.send_event   = False;
        eBtn.x_root       = rx;
        eBtn.y_root       = ry;

        XTranslateCoordinates(m_pDisplay, eBtn.root, eBtn.window, eBtn.x_root, eBtn.y_root, &eBtn.x, &eBtn.y, &eBtn.subwindow);
        LogFlowThisFunc(("state=0x%x, x=%d, y=%d\n", eBtn.state, eBtn.x, eBtn.y));

        int xRc = XSendEvent(m_pDisplay, wndDest, True /* fPropagate */,
                             ButtonPressMask,
                             reinterpret_cast<XEvent*>(&eBtn));
        if (xRc == 0)
            VBClLogError("Error sending XButtonEvent event to window=%#x: %s\n", wndDest, gX11->xErrorToString(xRc).c_str());

        XFlush(m_pDisplay);

#ifdef VBOX_DND_WITH_XTEST
    }
#endif
}

/**
 * Shows the (invisible) proxy window. The proxy window is needed for intercepting
 * drags from the host to the guest or from the guest to the host. It acts as a proxy
 * between the host and the actual (UI) element on the guest OS.
 *
 * To not make it miss any actions this window gets spawned across the entire guest
 * screen (think of an umbrella) to (hopefully) capture everything. A proxy window
 * which follows the cursor would be far too slow here.
 *
 * @returns IPRT status code.
 * @param   piRootX                 X coordinate relative to the root window's origin. Optional.
 * @param   piRootY                 Y coordinate relative to the root window's origin. Optional.
 */
int VBClX11DnDInst::proxyWinShow(int *piRootX /* = NULL */, int *piRootY /* = NULL */) const
{
    /* piRootX is optional. */
    /* piRootY is optional. */

    LogFlowThisFuncEnter();

    int rc = VINF_SUCCESS;

#if 0
# ifdef VBOX_DND_WITH_XTEST
    XTestGrabControl(m_pDisplay, False);
# endif
#endif

    /* Get the mouse pointer position and determine if we're on the same screen as the root window
     * and return the current child window beneath our mouse pointer, if any. */
    int iRootX, iRootY;
    int iChildX, iChildY;
    unsigned int iMask;
    Window wndRoot, wndChild;
    Bool fInRootWnd = XQueryPointer(m_pDisplay, m_wndRoot, &wndRoot, &wndChild,
                                    &iRootX, &iRootY, &iChildX, &iChildY, &iMask);

    LogFlowThisFunc(("fInRootWnd=%RTbool, wndRoot=%RU32, wndChild=%RU32, iRootX=%d, iRootY=%d\n",
                     RT_BOOL(fInRootWnd), wndRoot, wndChild, iRootX, iRootY)); RT_NOREF(fInRootWnd);

    if (piRootX)
        *piRootX = iRootX;
    if (piRootY)
        *piRootY = iRootY;

    XSynchronize(m_pDisplay, True /* Enable sync */);

    /* Bring our proxy window into foreground. */
    XMapWindow(m_pDisplay, m_wndProxy.hWnd);
    XRaiseWindow(m_pDisplay, m_wndProxy.hWnd);

    /* Spawn our proxy window over the entire screen, making it an easy drop target for the host's cursor. */
    LogFlowThisFunc(("Proxy window x=%d, y=%d, width=%d, height=%d\n",
                     m_wndProxy.iX, m_wndProxy.iY, m_wndProxy.iWidth, m_wndProxy.iHeight));
    XMoveResizeWindow(m_pDisplay, m_wndProxy.hWnd, m_wndProxy.iX, m_wndProxy.iY, m_wndProxy.iWidth, m_wndProxy.iHeight);

    XFlush(m_pDisplay);

    XSynchronize(m_pDisplay, False /* Disable sync */);

#if 0
# ifdef VBOX_DND_WITH_XTEST
    XTestGrabControl(m_pDisplay, True);
# endif
#endif

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Hides the (invisible) proxy window.
 */
int VBClX11DnDInst::proxyWinHide(void)
{
    LogFlowFuncEnter();

    XUnmapWindow(m_pDisplay, m_wndProxy.hWnd);
    XFlush(m_pDisplay);

    return VINF_SUCCESS; /** @todo Add error checking. */
}

/**
 * Allocates the name (title) of an X window.
 * The returned pointer must be freed using RTStrFree().
 *
 * @returns Pointer to the allocated window name.
 * @retval  NULL on allocation failure.
 * @retval  "<No name>" if window name was not found / invalid window handle.
 * @param   wndThis                 Window to retrieve name for.
 */
char *VBClX11DnDInst::wndX11GetNameA(Window wndThis) const
{
    char *pszName = NULL;

    XTextProperty propName;
    if (   wndThis != None
        && XGetWMName(m_pDisplay, wndThis, &propName))
    {
        if (propName.value)
            pszName = RTStrDup((char *)propName.value); /** @todo UTF8? */
        XFree(propName.value);
    }

    if (!pszName) /* No window name found? */
        pszName = RTStrDup("<No name>");

    return pszName;
}

/**
 * Clear a window's supported/accepted actions list.
 *
 * @param   wndThis                 Window to clear the list for.
 */
void VBClX11DnDInst::wndXDnDClearActionList(Window wndThis) const
{
    XDeleteProperty(m_pDisplay, wndThis, xAtom(XA_XdndActionList));
}

/**
 * Clear a window's supported/accepted formats list.
 *
 * @param   wndThis                 Window to clear the list for.
 */
void VBClX11DnDInst::wndXDnDClearFormatList(Window wndThis) const
{
    XDeleteProperty(m_pDisplay, wndThis, xAtom(XA_XdndTypeList));
}

/**
 * Retrieves a window's supported/accepted XDnD actions.
 *
 * @returns IPRT status code.
 * @param   wndThis                 Window to retrieve the XDnD actions for.
 * @param   lstActions              Reference to VBoxDnDAtomList to store the action into.
 */
int VBClX11DnDInst::wndXDnDGetActionList(Window wndThis, VBoxDnDAtomList &lstActions) const
{
    Atom iActType = None;
    int iActFmt;
    unsigned long cItems, cbData;
    unsigned char *pcbData = NULL;

    /* Fetch the possible list of actions, if this property is set. */
    int xRc = XGetWindowProperty(m_pDisplay, wndThis,
                                 xAtom(XA_XdndActionList),
                                 0, VBOX_MAX_XPROPERTIES,
                                 False, XA_ATOM, &iActType, &iActFmt, &cItems, &cbData, &pcbData);
    if (xRc != Success)
    {
        LogFlowThisFunc(("Error getting XA_XdndActionList atoms from window=%#x: %s\n",
                         wndThis, gX11->xErrorToString(xRc).c_str()));
        return VERR_NOT_FOUND;
    }

    LogFlowThisFunc(("wndThis=%#x, cItems=%RU32, pcbData=%p\n", wndThis, cItems, pcbData));

    if (cItems > 0)
    {
        AssertPtr(pcbData);
        Atom *paData = reinterpret_cast<Atom *>(pcbData);

        for (unsigned i = 0; i < RT_MIN(VBOX_MAX_XPROPERTIES, cItems); i++)
        {
            LogFlowThisFunc(("\t%s\n", gX11->xAtomToString(paData[i]).c_str()));
            lstActions.append(paData[i]);
        }

        XFree(pcbData);
    }

    return VINF_SUCCESS;
}

/**
 * Retrieves a window's supported/accepted XDnD formats.
 *
 * @returns IPRT status code.
 * @param   wndThis                 Window to retrieve the XDnD formats for.
 * @param   lstTypes                Reference to VBoxDnDAtomList to store the formats into.
 */
int VBClX11DnDInst::wndXDnDGetFormatList(Window wndThis, VBoxDnDAtomList &lstTypes) const
{
    Atom iActType = None;
    int iActFmt;
    unsigned long cItems, cbData;
    unsigned char *pcbData = NULL;

    int xRc = XGetWindowProperty(m_pDisplay, wndThis,
                             xAtom(XA_XdndTypeList),
                             0, VBOX_MAX_XPROPERTIES,
                             False, XA_ATOM, &iActType, &iActFmt, &cItems, &cbData, &pcbData);
    if (xRc != Success)
    {
        LogFlowThisFunc(("Error getting XA_XdndTypeList atoms from window=%#x: %s\n",
                         wndThis, gX11->xErrorToString(xRc).c_str()));
        return VERR_NOT_FOUND;
    }

    LogFlowThisFunc(("wndThis=%#x, cItems=%RU32, pcbData=%p\n", wndThis, cItems, pcbData));

    if (cItems > 0)
    {
        AssertPtr(pcbData);
        Atom *paData = reinterpret_cast<Atom *>(pcbData);

        for (unsigned i = 0; i < RT_MIN(VBOX_MAX_XPROPERTIES, cItems); i++)
        {
            LogFlowThisFunc(("\t%s\n", gX11->xAtomToString(paData[i]).c_str()));
            lstTypes.append(paData[i]);
        }

        XFree(pcbData);
    }

    return VINF_SUCCESS;
}

/**
 * Sets (replaces) a window's XDnD accepted/allowed actions.
 *
 * @returns IPRT status code.
 * @param   wndThis                 Window to set the format list for.
 * @param   lstActions              Reference to list of XDnD actions to set.
 */
int VBClX11DnDInst::wndXDnDSetActionList(Window wndThis, const VBoxDnDAtomList &lstActions) const
{
    if (lstActions.isEmpty())
        return VINF_SUCCESS;

    XChangeProperty(m_pDisplay, wndThis,
                    xAtom(XA_XdndActionList),
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(lstActions.raw()),
                    lstActions.size());

    return VINF_SUCCESS;
}

/**
 * Sets (replaces) a window's XDnD accepted format list.
 *
 * @returns IPRT status code.
 * @param   wndThis                 Window to set the format list for.
 * @param   atmProp                 Property to set.
 * @param   lstFormats              Reference to list of XDnD formats to set.
 */
int VBClX11DnDInst::wndXDnDSetFormatList(Window wndThis, Atom atmProp, const VBoxDnDAtomList &lstFormats) const
{
    if (lstFormats.isEmpty())
        return VERR_INVALID_PARAMETER;

    /* Add the property with the property data to the window. */
    XChangeProperty(m_pDisplay, wndThis, atmProp,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(lstFormats.raw()),
                    lstFormats.size());

    return VINF_SUCCESS;
}

/**
 * Appends a RTCString list to VBoxDnDAtomList list.
 *
 * @returns IPRT status code.
 * @param   lstFormats              Reference to RTCString list to convert.
 * @param   lstAtoms                Reference to VBoxDnDAtomList list to store results in.
 */
int VBClX11DnDInst::appendFormatsToList(const RTCList<RTCString> &lstFormats, VBoxDnDAtomList &lstAtoms) const
{
    for (size_t i = 0; i < lstFormats.size(); ++i)
        lstAtoms.append(XInternAtom(m_pDisplay, lstFormats.at(i).c_str(), False));

    return VINF_SUCCESS;
}

/**
 * Appends a raw-data string list to VBoxDnDAtomList list.
 *
 * @returns IPRT status code.
 * @param   pvData                  Pointer to string data to convert.
 * @param   cbData                  Size (in bytes) to convert.
 * @param   lstAtoms                Reference to VBoxDnDAtomList list to store results in.
 */
int VBClX11DnDInst::appendDataToList(const void *pvData, uint32_t cbData, VBoxDnDAtomList &lstAtoms) const
{
    RT_NOREF1(lstAtoms);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    const char *pszStr = (char *)pvData;
    uint32_t cbStr = cbData;

    int rc = VINF_SUCCESS;

    VBoxDnDAtomList lstAtom;
    while (cbStr)
    {
        size_t cbSize = RTStrNLen(pszStr, cbStr);

        /* Create a copy with max N chars, so that we are on the save side,
         * even if the data isn't zero terminated. */
        char *pszTmp = RTStrDupN(pszStr, cbSize);
        if (!pszTmp)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        lstAtom.append(XInternAtom(m_pDisplay, pszTmp, False));
        RTStrFree(pszTmp);

        pszStr  += cbSize + 1;
        cbStr   -= cbSize + 1;
    }

    return rc;
}

/**
 * Converts a HGCM-based drag'n drop action to a Atom-based drag'n drop action.
 *
 * @returns Converted Atom-based drag'n drop action.
 * @param   dndAction               HGCM drag'n drop actions to convert.
 */
/* static */
Atom VBClX11DnDInst::toAtomAction(VBOXDNDACTION dndAction)
{
    /* Ignore is None. */
    return (isDnDCopyAction(dndAction) ? xAtom(XA_XdndActionCopy) :
            isDnDMoveAction(dndAction) ? xAtom(XA_XdndActionMove) :
            isDnDLinkAction(dndAction) ? xAtom(XA_XdndActionLink) :
            None);
}

/**
 * Converts HGCM-based drag'n drop actions to a VBoxDnDAtomList list.
 *
 * @returns IPRT status code.
 * @param   dndActionList           HGCM drag'n drop actions to convert.
 * @param   lstAtoms                Reference to VBoxDnDAtomList to store actions in.
 */
/* static */
int VBClX11DnDInst::toAtomActions(VBOXDNDACTIONLIST dndActionList, VBoxDnDAtomList &lstAtoms)
{
    if (hasDnDCopyAction(dndActionList))
        lstAtoms.append(xAtom(XA_XdndActionCopy));
    if (hasDnDMoveAction(dndActionList))
        lstAtoms.append(xAtom(XA_XdndActionMove));
    if (hasDnDLinkAction(dndActionList))
        lstAtoms.append(xAtom(XA_XdndActionLink));

    return VINF_SUCCESS;
}

/**
 * Converts an Atom-based drag'n drop action to a HGCM drag'n drop action.
 *
 * @returns HGCM drag'n drop action.
 * @param   atom                    Atom-based drag'n drop action to convert.
 */
/* static */
uint32_t VBClX11DnDInst::toHGCMAction(Atom atom)
{
    uint32_t uAction = VBOX_DND_ACTION_IGNORE;

    if (atom == xAtom(XA_XdndActionCopy))
        uAction = VBOX_DND_ACTION_COPY;
    else if (atom == xAtom(XA_XdndActionMove))
        uAction = VBOX_DND_ACTION_MOVE;
    else if (atom == xAtom(XA_XdndActionLink))
        uAction = VBOX_DND_ACTION_LINK;

    return uAction;
}

/**
 * Converts an VBoxDnDAtomList list to an HGCM action list.
 *
 * @returns ORed HGCM action list.
 * @param   lstActions              List of Atom-based actions to convert.
 */
/* static */
uint32_t VBClX11DnDInst::toHGCMActions(const VBoxDnDAtomList &lstActions)
{
    uint32_t uActions = VBOX_DND_ACTION_IGNORE;

    for (size_t i = 0; i < lstActions.size(); i++)
        uActions |= toHGCMAction(lstActions.at(i));

    return uActions;
}

/*********************************************************************************************************************************
 * VBoxDnDProxyWnd implementation.                                                                                               *
 ********************************************************************************************************************************/

VBClX11DnDProxyWnd::VBClX11DnDProxyWnd(void)
    : pDisp(NULL)
    , hWnd(0)
    , iX(0)
    , iY(0)
    , iWidth(0)
    , iHeight(0)
{

}

VBClX11DnDProxyWnd::~VBClX11DnDProxyWnd(void)
{
    destroy();
}

int VBClX11DnDProxyWnd::init(Display *pDisplay)
{
    /** @todo What about multiple screens? Test this! */
    int iScreenID = XDefaultScreen(pDisplay);

    iWidth   = XDisplayWidth(pDisplay, iScreenID);
    iHeight  = XDisplayHeight(pDisplay, iScreenID);
    pDisp    = pDisplay;

    return VINF_SUCCESS;
}

void VBClX11DnDProxyWnd::destroy(void)
{

}

int VBClX11DnDProxyWnd::sendFinished(Window hWndSource, VBOXDNDACTION dndAction)
{
    /* Was the drop accepted by the host? That is, anything than ignoring. */
    bool fDropAccepted = dndAction > VBOX_DND_ACTION_IGNORE;

    LogFlowFunc(("dndAction=0x%x\n", dndAction));

    /* Confirm the result of the transfer to the target window. */
    XClientMessageEvent m;
    RT_ZERO(m);
    m.type         = ClientMessage;
    m.display      = pDisp;
    m.window       = hWnd;
    m.message_type = xAtom(XA_XdndFinished);
    m.format       = 32;
    m.data.l[XdndFinishedWindow] = hWnd;                                                         /* Target window. */
    m.data.l[XdndFinishedFlags]  = fDropAccepted ? RT_BIT(0) : 0;                                /* Was the drop accepted? */
    m.data.l[XdndFinishedAction] = fDropAccepted ? VBClX11DnDInst::toAtomAction(dndAction) : None; /* Action used on accept. */

    int xRc = XSendEvent(pDisp, hWndSource, True, NoEventMask, reinterpret_cast<XEvent*>(&m));
    if (xRc == 0)
    {
        VBClLogError("Error sending finished event to source window=%#x: %s\n",
                      hWndSource, gX11->xErrorToString(xRc).c_str());

        return VERR_GENERAL_FAILURE; /** @todo Fudge. */
    }

    return VINF_SUCCESS;
}

/*********************************************************************************************************************************
 * DragAndDropService implementation.                                                                                            *
 ********************************************************************************************************************************/

/** @copydoc VBCLSERVICE::pfnInit */
int VBClX11DnDSvc::init(void)
{
    LogFlowFuncEnter();

    /* Connect to the x11 server. */
    m_pDisplay = XOpenDisplay(NULL);
    if (!m_pDisplay)
    {
        VBClLogFatalError("Unable to connect to X server -- running in a terminal session?\n");
        return VERR_NOT_FOUND;
    }

    VBClX11DnDXHelpers *pHelpers = VBClX11DnDXHelpers::getInstance(m_pDisplay);
    if (!pHelpers)
        return VERR_NO_MEMORY;

    int rc;

    do
    {
        rc = RTSemEventCreate(&m_hEventSem);
        AssertRCBreak(rc);

        rc = RTCritSectInit(&m_eventQueueCS);
        AssertRCBreak(rc);

        rc = VbglR3DnDConnect(&m_dndCtx);
        AssertRCBreak(rc);

        /* Event thread for events coming from the HGCM device. */
        rc = RTThreadCreate(&m_hHGCMThread, hgcmEventThread, this,
                            0, RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE, "dndHGCM");
        AssertRCBreak(rc);

        rc = RTThreadUserWait(m_hHGCMThread, RT_MS_30SEC);
        AssertRCBreak(rc);

        if (ASMAtomicReadBool(&m_fStop))
            break;

        /* Event thread for events coming from the x11 system. */
        rc = RTThreadCreate(&m_hX11Thread, x11EventThread, this,
                            0, RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE, "dndX11");
        AssertRCBreak(rc);

        rc = RTThreadUserWait(m_hX11Thread, RT_MS_30SEC);
        AssertRCBreak(rc);

        if (ASMAtomicReadBool(&m_fStop))
            break;

    } while (0);

    if (m_fStop)
        rc = VERR_GENERAL_FAILURE; /** @todo Fudge! */

    if (RT_FAILURE(rc))
        VBClLogError("Failed to initialize, rc=%Rrc\n", rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc VBCLSERVICE::pfnWorker */
int VBClX11DnDSvc::worker(bool volatile *pfShutdown)
{
    int rc;
    do
    {
        m_pCurDnD = new VBClX11DnDInst(m_pDisplay, this);
        if (!m_pCurDnD)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* Note: For multiple screen support in VBox it is not necessary to use
         * another screen number than zero. Maybe in the future it will become
         * necessary if VBox supports multiple X11 screens. */
        rc = m_pCurDnD->init(0 /* uScreenID */);
        /* Note: Can return VINF_PERMISSION_DENIED if HGCM host service is not available. */
        if (rc != VINF_SUCCESS)
        {
            if (RT_FAILURE(rc))
                VBClLogError("Unable to connect to drag and drop service, rc=%Rrc\n", rc);
            else if (rc == VINF_PERMISSION_DENIED) /* No error, DnD might be just disabled. */
                VBClLogInfo("Not available on host, terminating\n");
            break;
        }

        /* Let the main thread know that it can continue spawning services. */
        RTThreadUserSignal(RTThreadSelf());

        /* Enter the main event processing loop. */
        do
        {
            VBCLDNDEVENT e;
            RT_ZERO(e);

            LogFlowFunc(("Waiting for new events ...\n"));
            rc = RTSemEventWait(m_hEventSem, RT_INDEFINITE_WAIT);
            if (RT_FAILURE(rc))
                break;

            size_t cEvents = 0;

            int rc2 = RTCritSectEnter(&m_eventQueueCS);
            if (RT_SUCCESS(rc2))
            {
                cEvents = m_eventQueue.size();

                rc2 = RTCritSectLeave(&m_eventQueueCS);
                AssertRC(rc2);
            }

            while (cEvents)
            {
                rc2 = RTCritSectEnter(&m_eventQueueCS);
                if (RT_SUCCESS(rc2))
                {
                    if (m_eventQueue.isEmpty())
                    {
                        rc2 = RTCritSectLeave(&m_eventQueueCS);
                        AssertRC(rc2);
                        break;
                    }

                    e = m_eventQueue.first();
                    m_eventQueue.removeFirst();

                    rc2 = RTCritSectLeave(&m_eventQueueCS);
                    AssertRC(rc2);
                }

                if (e.enmType == VBCLDNDEVENT::DnDEventType_HGCM)
                {
                    PVBGLR3DNDEVENT pVbglR3Event = e.hgcm;
                    AssertPtrBreak(pVbglR3Event);

                    LogFlowThisFunc(("HGCM event enmType=%RU32\n", pVbglR3Event->enmType));
                    switch (pVbglR3Event->enmType)
                    {
                        case VBGLR3DNDEVENTTYPE_HG_ENTER:
                        {
                            if (pVbglR3Event->u.HG_Enter.cbFormats)
                            {
                                RTCList<RTCString> lstFormats =
                                    RTCString(pVbglR3Event->u.HG_Enter.pszFormats, pVbglR3Event->u.HG_Enter.cbFormats - 1).split(DND_PATH_SEPARATOR_STR);
                                rc = m_pCurDnD->hgEnter(lstFormats, pVbglR3Event->u.HG_Enter.dndLstActionsAllowed);
                                if (RT_FAILURE(rc))
                                    break;
                                /* Enter is always followed by a move event. */
                            }
                            else
                            {
                                AssertMsgFailed(("cbFormats is 0\n"));
                                rc = VERR_INVALID_PARAMETER;
                                break;
                            }

                            /* Note: After HOST_DND_FN_HG_EVT_ENTER there immediately is a move
                             *       event, so fall through is intentional here. */
                            RT_FALL_THROUGH();
                        }

                        case VBGLR3DNDEVENTTYPE_HG_MOVE:
                        {
                            rc = m_pCurDnD->hgMove(pVbglR3Event->u.HG_Move.uXpos, pVbglR3Event->u.HG_Move.uYpos,
                                                   pVbglR3Event->u.HG_Move.dndActionDefault);
                            break;
                        }

                        case VBGLR3DNDEVENTTYPE_HG_LEAVE:
                        {
                            rc = m_pCurDnD->hgLeave();
                            break;
                        }

                        case VBGLR3DNDEVENTTYPE_HG_DROP:
                        {
                            rc = m_pCurDnD->hgDrop(pVbglR3Event->u.HG_Drop.uXpos, pVbglR3Event->u.HG_Drop.uYpos,
                                                   pVbglR3Event->u.HG_Drop.dndActionDefault);
                            break;
                        }

                        /* Note: VbglR3DnDRecvNextMsg() will return HOST_DND_FN_HG_SND_DATA_HDR when
                         *       the host has finished copying over all the data to the guest.
                         *
                         *       The actual data transfer (and message processing for it) will be done
                         *       internally by VbglR3DnDRecvNextMsg() to not duplicate any code for different
                         *       platforms.
                         *
                         *       The data header now will contain all the (meta) data the guest needs in
                         *       order to complete the DnD operation. */
                        case VBGLR3DNDEVENTTYPE_HG_RECEIVE:
                        {
                            rc = m_pCurDnD->hgDataReceive(&pVbglR3Event->u.HG_Received.Meta);
                            break;
                        }

                        case VBGLR3DNDEVENTTYPE_CANCEL:
                        {
                            m_pCurDnD->reset();
                            break;
                        }

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
                        case VBGLR3DNDEVENTTYPE_GH_ERROR:
                        {
                            m_pCurDnD->reset();
                            break;
                        }

                        case VBGLR3DNDEVENTTYPE_GH_REQ_PENDING:
                        {
                            rc = m_pCurDnD->ghIsDnDPending();
                            break;
                        }

                        case VBGLR3DNDEVENTTYPE_GH_DROP:
                        {
                            rc = m_pCurDnD->ghDropped(pVbglR3Event->u.GH_Drop.pszFormat, pVbglR3Event->u.GH_Drop.dndActionRequested);
                            break;
                        }
#endif
                        case VBGLR3DNDEVENTTYPE_QUIT:
                        {
                            rc = VINF_SUCCESS;
                            break;
                        }

                        default:
                        {
                            VBClLogError("Received unsupported message type %RU32\n", pVbglR3Event->enmType);
                            rc = VERR_NOT_SUPPORTED;
                            break;
                        }
                    }

                    LogFlowFunc(("Message %RU32 processed with %Rrc\n", pVbglR3Event->enmType, rc));
                    if (RT_FAILURE(rc))
                    {
                        /* Tell the user. */
                        VBClLogError("Processing message %RU32 failed with %Rrc\n", pVbglR3Event->enmType, rc);

                        /* If anything went wrong, do a reset and start over. */
                        reset();
                    }

                    const bool fQuit = pVbglR3Event->enmType == VBGLR3DNDEVENTTYPE_QUIT;

                    VbglR3DnDEventFree(e.hgcm);
                    e.hgcm = NULL;

                    if (fQuit)
                        break;
                }
                else if (e.enmType == VBCLDNDEVENT::DnDEventType_X11)
                {
                    LogFlowThisFunc(("X11 event (type %#x)\n", e.x11.type));
                    m_pCurDnD->onX11Event(e.x11);
                }
                else
                    AssertMsgFailed(("Unknown event queue type %RU32\n", e.enmType));

                --cEvents;

            } /* for */

            /*
             * Make sure that any X11 requests have actually been sent to the
             * server, since we are waiting for responses using poll() on
             * another thread which will not automatically trigger flushing.
             */
            XFlush(m_pDisplay);

            if (m_fStop)
                break;

        } while (!ASMAtomicReadBool(pfShutdown));

    } while (0);

    if (m_pCurDnD)
    {
        delete m_pCurDnD;
        m_pCurDnD = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resets the DnD service' data.
 */
void VBClX11DnDSvc::reset(void)
{
    LogFlowFuncEnter();

    if (m_pCurDnD)
        m_pCurDnD->reset();

    /*
     * Clear the event queue.
     */
    int rc2 = RTCritSectEnter(&m_eventQueueCS);
    if (RT_SUCCESS(rc2))
    {
        for (size_t i = 0; i < m_eventQueue.size(); i++)
        {
            switch (m_eventQueue[i].enmType)
            {
                case VBCLDNDEVENT::DnDEventType_HGCM:
                {
                    VbglR3DnDEventFree(m_eventQueue[i].hgcm);
                    break;
                }

                default:
                    break;
            }

        }

        m_eventQueue.clear();

        rc2 = RTCritSectLeave(&m_eventQueueCS);
        AssertRC(rc2);
    }

    LogFlowFuncLeave();
}

/** @copydoc VBCLSERVICE::pfnStop */
void VBClX11DnDSvc::stop(void)
{
    LogFlowFuncEnter();

    /* Set stop flag first. */
    ASMAtomicXchgBool(&m_fStop, true);

    /* First, disconnect any instances. */
    if (m_pCurDnD)
        m_pCurDnD->stop();

    /* Second, disconnect the service's DnD connection. */
    VbglR3DnDDisconnect(&m_dndCtx);

    LogFlowFuncLeave();
}

/** @copydoc VBCLSERVICE::pfnTerm */
int VBClX11DnDSvc::term(void)
{
    int rc = VINF_SUCCESS;

    /*
     * Wait for threads to terminate.
     */
    int rcThread;

    if (m_hX11Thread != NIL_RTTHREAD)
    {
        VBClLogVerbose(2, "Terminating X11 thread ...\n");

        int rc2 = RTThreadWait(m_hX11Thread, RT_MS_30SEC, &rcThread);
        if (RT_SUCCESS(rc2))
            rc2 = rcThread;

        if (RT_FAILURE(rc2))
            VBClLogError("Error waiting for X11 thread to terminate: %Rrc\n", rc2);

        if (RT_SUCCESS(rc))
            rc = rc2;

        m_hX11Thread = NIL_RTTHREAD;

        VBClLogVerbose(2, "X11 thread terminated\n");
    }

    if (m_hHGCMThread != NIL_RTTHREAD)
    {
        VBClLogVerbose(2, "Terminating HGCM thread ...\n");

        int rc2 = RTThreadWait(m_hHGCMThread, RT_MS_30SEC, &rcThread);
        if (RT_SUCCESS(rc2))
            rc2 = rcThread;

        if (RT_FAILURE(rc2))
            VBClLogError("Error waiting for HGCM thread to terminate: %Rrc\n", rc2);

        if (RT_SUCCESS(rc))
            rc = rc2;

        m_hHGCMThread = NIL_RTTHREAD;

        VBClLogVerbose(2, "HGCM thread terminated\n");
    }

    reset();

    if (m_pCurDnD)
    {
        delete m_pCurDnD;
        m_pCurDnD = NULL;
    }

    VBClX11DnDXHelpers::destroyInstance();

    return rc;
}

/**
 * Static callback function for HGCM message processing thread. An internal
 * message queue will be filled which then will be processed by the according
 * drag'n drop instance.
 *
 * @returns IPRT status code.
 * @param   hThread                 Thread handle to use.
 * @param   pvUser                  Pointer to DragAndDropService instance to use.
 */
/* static */
DECLCALLBACK(int) VBClX11DnDSvc::hgcmEventThread(RTTHREAD hThread, void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_PARAMETER);
    VBClX11DnDSvc *pThis = static_cast<VBClX11DnDSvc*>(pvUser);

    /* Let the service instance know in any case. */
    int rc = RTThreadUserSignal(hThread);
    AssertRCReturn(rc, rc);

    VBClLogVerbose(2, "HGCM thread started\n");

    /* Number of invalid messages skipped in a row. */
    int cMsgSkippedInvalid = 0;
    VBCLDNDEVENT e;

    do
    {
        RT_ZERO(e);
        e.enmType = VBCLDNDEVENT::DnDEventType_HGCM;

        /* Wait for new events. */
        rc = VbglR3DnDEventGetNext(&pThis->m_dndCtx, &e.hgcm);
        if (RT_SUCCESS(rc))
        {
            cMsgSkippedInvalid = 0; /* Reset skipped messages count. */

            int rc2 = RTCritSectEnter(&pThis->m_eventQueueCS);
            if (RT_SUCCESS(rc2))
            {
                VBClLogVerbose(2, "Received new HGCM message (type %#x)\n", e.hgcm->enmType);

                pThis->m_eventQueue.append(e);

                rc2 = RTCritSectLeave(&pThis->m_eventQueueCS);
                AssertRC(rc2);
            }

            rc = RTSemEventSignal(pThis->m_hEventSem);
            if (RT_FAILURE(rc))
                break;
        }
        else
        {
            VBClLogError("Processing next message failed with rc=%Rrc\n", rc);

            /* Old(er) hosts either are broken regarding DnD support or otherwise
             * don't support the stuff we do on the guest side, so make sure we
             * don't process invalid messages forever. */

            if (cMsgSkippedInvalid++ > 32)
            {
                VBClLogError("Too many invalid/skipped messages from host, exiting ...\n");
                break;
            }
        }

    } while (!ASMAtomicReadBool(&pThis->m_fStop));

    VBClLogVerbose(2, "HGCM thread ended\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Static callback function for X11 message processing thread. All X11 messages
 * will be directly routed to the according drag'n drop instance.
 *
 * @returns IPRT status code.
 * @param   hThread                 Thread handle to use.
 * @param   pvUser                  Pointer to DragAndDropService instance to use.
 */
/* static */
DECLCALLBACK(int) VBClX11DnDSvc::x11EventThread(RTTHREAD hThread, void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_PARAMETER);
    VBClX11DnDSvc *pThis = static_cast<VBClX11DnDSvc*>(pvUser);
    AssertPtr(pThis);

    int rc = VINF_SUCCESS;

    /* Note: Nothing to initialize here (yet). */

    /* Let the service instance know in any case. */
    int rc2 = RTThreadUserSignal(hThread);
    AssertRC(rc2);

    VBClLogVerbose(2, "X11 thread started\n");

    VBCLDNDEVENT e;
    RT_ZERO(e);
    e.enmType = VBCLDNDEVENT::DnDEventType_X11;

    do
    {
        /*
         * Wait for new events. We can't use XIfEvent here, cause this locks
         * the window connection with a mutex and if no X11 events occurs this
         * blocks any other calls we made to X11. So instead check for new
         * events and if there are not any new one, sleep for a certain amount
         * of time.
         */
        unsigned cNewEvents = 0;
        unsigned cQueued    = XEventsQueued(pThis->m_pDisplay, QueuedAfterFlush);
        while (cQueued)
        {
            /* XNextEvent will block until a new X event becomes available. */
            XNextEvent(pThis->m_pDisplay, &e.x11);
            {
                rc2 = RTCritSectEnter(&pThis->m_eventQueueCS);
                if (RT_SUCCESS(rc2))
                {
                    LogFlowFunc(("Added new X11 event, type=%d\n", e.x11.type));

                    pThis->m_eventQueue.append(e);
                    cNewEvents++;

                    rc2 = RTCritSectLeave(&pThis->m_eventQueueCS);
                    AssertRC(rc2);
                }
            }

            cQueued--;
        }

        if (cNewEvents)
        {
            rc = RTSemEventSignal(pThis->m_hEventSem);
            if (RT_FAILURE(rc))
                break;

            continue;
        }

        /* No new events; wait a bit. */
        RTThreadSleep(25 /* ms */);

    } while (!ASMAtomicReadBool(&pThis->m_fStop));

    VBClLogVerbose(2, "X11 thread ended\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

