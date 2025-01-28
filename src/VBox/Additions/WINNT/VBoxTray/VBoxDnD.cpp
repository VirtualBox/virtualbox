/* $Id: VBoxDnD.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDnD.cpp - Windows-specific bits of the drag and drop service.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>

#include <iprt/win/windows.h>
#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxHelpers.h"
#include "VBoxDnD.h"

#include <VBox/VBoxGuestLib.h>
#include "VBox/HostServices/DragAndDropSvc.h"

using namespace DragAndDropSvc;

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>

#include <iprt/cpp/mtlist.h>
#include <iprt/cpp/ministring.h>

#include <iprt/cpp/mtlist.h>

#include <VBox/err.h>
#include <VBox/version.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The drag and drop window's window class. */
#define VBOX_DND_WND_CLASS            "VBoxTrayDnDWnd"

/** @todo Merge this with messages from VBoxTray.h. */
#define WM_VBOXTRAY_DND_MESSAGE       WM_APP + 401

/** The notification header text for hlpShowBalloonTip(). */
#define VBOX_DND_SHOWBALLOON_HEADER   VBOX_PRODUCT " Drag'n Drop"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Function pointer for SendInput(). This only is available starting
 *  at NT4 SP3+. */
typedef BOOL (WINAPI *PFNSENDINPUT)(UINT, LPINPUT, int);
typedef BOOL (WINAPI* PFNENUMDISPLAYMONITORS)(HDC, LPCRECT, MONITORENUMPROC, LPARAM);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Static pointer to SendInput() function. */
static PFNSENDINPUT             g_pfnSendInput = NULL;
static PFNENUMDISPLAYMONITORS   g_pfnEnumDisplayMonitors = NULL;

static VBOXDNDCONTEXT           g_Ctx = { 0 };


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static LRESULT CALLBACK vboxDnDWndProcInstance(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_PROTO;
static LRESULT CALLBACK vboxDnDWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_PROTO;




VBoxDnDWnd::VBoxDnDWnd(void)
    : m_hThread(NIL_RTTHREAD),
      m_EvtSem(NIL_RTSEMEVENT),
      m_hWnd(NULL),
      m_lstActionsAllowed(VBOX_DND_ACTION_IGNORE),
      m_fMouseButtonDown(false),
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
      m_pDropTarget(NULL),
#endif
      m_enmMode(Unknown),
      m_enmState(Uninitialized)
{
    RT_ZERO(m_startupInfo);

    LogFlowFunc(("Supported formats:\n"));
    const RTCString arrEntries[] = { VBOX_DND_FORMATS_DEFAULT };
    for (size_t i = 0; i < RT_ELEMENTS(arrEntries); i++)
    {
        LogFlowFunc(("\t%s\n", arrEntries[i].c_str()));
        this->m_lstFmtSup.append(arrEntries[i]);
    }
}

VBoxDnDWnd::~VBoxDnDWnd(void)
{
    Destroy();
}

/**
 * Initializes the proxy window with a given DnD context.
 *
 * @return  VBox status code.
 * @param   a_pCtx  Pointer to context to use.
 */
int VBoxDnDWnd::Initialize(PVBOXDNDCONTEXT a_pCtx)
{
    AssertPtrReturn(a_pCtx, VERR_INVALID_POINTER);

    /* Save the context. */
    this->m_pCtx = a_pCtx;

    int rc = RTSemEventCreate(&m_EvtSem);
    if (RT_SUCCESS(rc))
        rc = RTCritSectInit(&m_CritSect);

    if (RT_SUCCESS(rc))
    {
        /* Message pump thread for our proxy window. */
        rc = RTThreadCreate(&m_hThread, VBoxDnDWnd::Thread, this,
                            0, RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE,
                            "dndwnd"); /** @todo Include ID if there's more than one proxy window. */
        if (RT_SUCCESS(rc))
        {
            int rc2 = RTThreadUserWait(m_hThread, 30 * 1000 /* Timeout in ms */);
            AssertRC(rc2);

            if (!a_pCtx->fStarted) /* Did the thread fail to start? */
                rc = VERR_NOT_SUPPORTED; /* Report back DnD as not being supported. */
        }
    }

    if (RT_FAILURE(rc))
        VBoxTrayError("DnD: Failed to initialize proxy window, rc=%Rrc\n", rc);

    LogFlowThisFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}

/**
 * Destroys the proxy window and releases all remaining
 * resources again.
 */
void VBoxDnDWnd::Destroy(void)
{
    if (m_hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_WRONG_ORDER;
        int rc = RTThreadWait(m_hThread, 60 * 1000 /* Timeout in ms */, &rcThread);
        LogFlowFunc(("Waiting for thread resulted in %Rrc (thread exited with %Rrc)\n",
                     rc, rcThread));
        NOREF(rc);
    }

    Reset();

    RTCritSectDelete(&m_CritSect);
    if (m_EvtSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(m_EvtSem);
        m_EvtSem = NIL_RTSEMEVENT;
    }

    if (m_pCtx->wndClass != 0)
    {
        UnregisterClass(VBOX_DND_WND_CLASS, m_pCtx->pEnv->hInstance);
        m_pCtx->wndClass = 0;
    }

    LogFlowFuncLeave();
}

/**
 * Thread for handling the window's message pump.
 *
 * @return  VBox status code.
 * @param   hThread                 Handle to this thread.
 * @param   pvUser                  Pointer to VBoxDnDWnd instance which
 *                                  is using the thread.
 */
/*static*/ DECLCALLBACK(int) VBoxDnDWnd::Thread(RTTHREAD hThread, void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    VBoxDnDWnd *pThis = (VBoxDnDWnd*)pvUser;
    AssertPtr(pThis);

    PVBOXDNDCONTEXT m_pCtx = pThis->m_pCtx;
    AssertPtr(m_pCtx);
    AssertPtr(m_pCtx->pEnv);

    int rc = VINF_SUCCESS;

    AssertPtr(m_pCtx->pEnv);
    HINSTANCE hInstance = m_pCtx->pEnv->hInstance;
    Assert(hInstance != 0);

    /* Create our proxy window. */
    WNDCLASSEX wc = { 0 };
    wc.cbSize     = sizeof(WNDCLASSEX);

    if (!GetClassInfoEx(hInstance, VBOX_DND_WND_CLASS, &wc))
    {
        wc.lpfnWndProc   = vboxDnDWndProc;
        wc.lpszClassName = VBOX_DND_WND_CLASS;
        wc.hInstance     = hInstance;
        wc.style         = CS_NOCLOSE;

        if (g_cVerbosity)
        {
            /* Make it a solid red color so that we can see the window. */
            wc.style        |= CS_HREDRAW | CS_VREDRAW;
            wc.hbrBackground = (HBRUSH)(CreateSolidBrush(RGB(255, 0, 0)));
        }
        else
            wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);

        if (!RegisterClassEx(&wc))
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("Unable to register proxy window class, error=%ld\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }
    }

    if (RT_SUCCESS(rc))
    {
        DWORD dwExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        DWORD dwStyle   = WS_POPUP;
        if (g_cVerbosity)
        {
            dwStyle |= WS_VISIBLE;
        }
        else
            dwExStyle |= WS_EX_TRANSPARENT;

        pThis->m_hWnd = CreateWindowEx(dwExStyle,
                                       VBOX_DND_WND_CLASS, VBOX_DND_WND_CLASS,
                                       dwStyle,
                                       -200, -200, 100, 100, NULL, NULL,
                                       hInstance, pThis /* lParm */);
        if (!pThis->m_hWnd)
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("Unable to create proxy window, error=%ld\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }
        else
        {
            BOOL fRc = SetWindowPos(pThis->m_hWnd, HWND_TOPMOST, -200, -200, 0, 0,
                                      SWP_NOACTIVATE | SWP_HIDEWINDOW
                                    | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);
            AssertMsg(fRc, ("Unable to set window position, error=%ld\n", GetLastError()));

            LogFlowFunc(("Proxy window created, hWnd=0x%x\n", pThis->m_hWnd));

            if (g_cVerbosity)
            {
                /*
                 * Install some mouse tracking.
                 */
                TRACKMOUSEEVENT me;
                RT_ZERO(me);
                me.cbSize    = sizeof(TRACKMOUSEEVENT);
                me.dwFlags   = TME_HOVER | TME_LEAVE | TME_NONCLIENT;
                me.hwndTrack = pThis->m_hWnd;

                fRc = TrackMouseEvent(&me);
                AssertMsg(fRc, ("Unable to enable debug mouse tracking, error=%ld\n", GetLastError()));
            }
        }
    }

    HRESULT hr = OleInitialize(NULL);
    if (SUCCEEDED(hr))
    {
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
        rc = pThis->RegisterAsDropTarget();
#endif
    }
    else
    {
        VBoxTrayError("DnD: Unable to initialize OLE, hr=%Rhrc\n", hr);
        rc = VERR_COM_UNEXPECTED;
    }

    if (RT_SUCCESS(rc))
        m_pCtx->fStarted = true; /* Set started indicator on success. */

    int rc2 = RTThreadUserSignal(hThread);
    bool fSignalled = RT_SUCCESS(rc2);

    if (RT_SUCCESS(rc))
    {
        bool fShutdown = false;
        for (;;)
        {
            MSG uMsg;
            BOOL fRet;
            while ((fRet = GetMessage(&uMsg, 0, 0, 0)) > 0)
            {
                TranslateMessage(&uMsg);
                DispatchMessage(&uMsg);
            }
            Assert(fRet >= 0);

            if (ASMAtomicReadBool(&m_pCtx->fShutdown))
                fShutdown = true;

            if (fShutdown)
            {
                LogFlowFunc(("Closing proxy window ...\n"));
                break;
            }

            /** @todo Immediately drop on failure? */
        }

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
        rc2 = pThis->UnregisterAsDropTarget();
        if (RT_SUCCESS(rc))
            rc = rc2;
#endif
        OleUninitialize();
    }

    if (!fSignalled)
    {
        rc2 = RTThreadUserSignal(hThread);
        AssertRC(rc2);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Monitor enumeration callback for building up a simple bounding
 * box, capable of holding all enumerated monitors.
 *
 * @return  BOOL                    TRUE if enumeration should continue,
 *                                  FALSE if not.
 * @param   hMonitor                Handle to current monitor being enumerated.
 * @param   hdcMonitor              The current monitor's DC (device context).
 * @param   lprcMonitor             The current monitor's RECT.
 * @param   lParam                  Pointer to a RECT structure holding the
 *                                  bounding box to build.
 */
/* static */
BOOL CALLBACK VBoxDnDWnd::MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM lParam)
{
    RT_NOREF(hMonitor, hdcMonitor);
    LPRECT pRect = (LPRECT)lParam;
    AssertPtrReturn(pRect, FALSE);

    AssertPtr(lprcMonitor);
    LogFlowFunc(("Monitor is %ld,%ld,%ld,%ld\n",
                 lprcMonitor->left, lprcMonitor->top,
                 lprcMonitor->right, lprcMonitor->bottom));

    /* Build up a simple bounding box to hold the entire (virtual) screen. */
    if (pRect->left > lprcMonitor->left)
        pRect->left = lprcMonitor->left;
    if (pRect->right < lprcMonitor->right)
        pRect->right = lprcMonitor->right;
    if (pRect->top > lprcMonitor->top)
        pRect->top = lprcMonitor->top;
    if (pRect->bottom < lprcMonitor->bottom)
        pRect->bottom = lprcMonitor->bottom;

    return TRUE;
}

/**
 * The proxy window's WndProc.
 */
LRESULT CALLBACK VBoxDnDWnd::WndProc(HWND a_hWnd, UINT a_uMsg, WPARAM a_wParam, LPARAM a_lParam)
{
    switch (a_uMsg)
    {
        case WM_CREATE:
        {
            int rc = OnCreate();
            if (RT_FAILURE(rc))
            {
                VBoxTrayError("DnD: Failed to create proxy window, rc=%Rrc\n", rc);
                return -1;
            }
            return 0;
        }

        case WM_QUIT:
        {
            LogFlowThisFunc(("WM_QUIT\n"));
            PostQuitMessage(0);
            return 0;
        }

        case WM_DESTROY:
        {
            LogFlowThisFunc(("WM_DESTROY\n"));

            OnDestroy();
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            LogFlowThisFunc(("WM_LBUTTONDOWN\n"));
            m_fMouseButtonDown = true;
            return 0;
        }

        case WM_LBUTTONUP:
        {
            LogFlowThisFunc(("WM_LBUTTONUP\n"));
            m_fMouseButtonDown = false;

            /* As the mouse button was released, Hide the proxy window again.
             * This can happen if
             * - the user bumped a guest window to the screen's edges
             * - there was no drop data from the guest available and the user
             *   enters the guest screen again after this unsuccessful operation */
            Reset();
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            LogFlowThisFunc(("WM_MOUSELEAVE\n"));
            return 0;
        }

        /* Will only be called once; after the first mouse move, this
         * window will be hidden! */
        case WM_MOUSEMOVE:
        {
            LogFlowThisFunc(("WM_MOUSEMOVE: mfMouseButtonDown=%RTbool, mMode=%ld, mState=%ld\n",
                             m_fMouseButtonDown, m_enmMode, m_enmState));
#ifdef DEBUG_andy
            POINT p;
            GetCursorPos(&p);
            LogFlowThisFunc(("WM_MOUSEMOVE: curX=%ld, curY=%ld\n", p.x, p.y));
#endif
            int rc = VINF_SUCCESS;
            if (m_enmMode == HG) /* Host to guest. */
            {
                /* Dragging not started yet? Kick it off ... */
                if (   m_fMouseButtonDown
                    && (m_enmState != Dragging))
                {
                    m_enmState = Dragging;
#if 0
                    /* Delay hiding the proxy window a bit when debugging, to see
                     * whether the desired range is covered correctly. */
                    RTThreadSleep(5000);
#endif
                    Hide();

                    LogFlowThisFunc(("Starting drag and drop: dndLstActionsAllowed=0x%x, dwOKEffects=0x%x ...\n",
                                     m_lstActionsAllowed, m_startupInfo.dwOKEffects));

                    AssertPtr(m_startupInfo.pDataObject);
                    AssertPtr(m_startupInfo.pDropSource);
                    DWORD dwEffect;
                    HRESULT hr = DoDragDrop(m_startupInfo.pDataObject, m_startupInfo.pDropSource,
                                            m_startupInfo.dwOKEffects, &dwEffect);
                    LogFlowThisFunc(("hr=%Rhrc, dwEffect=%RI32\n", hr, dwEffect));
                    switch (hr)
                    {
                        case DRAGDROP_S_DROP:
                            m_enmState = Dropped;
                            break;

                        case DRAGDROP_S_CANCEL:
                            m_enmState = Canceled;
                            break;

                        default:
                            LogFlowThisFunc(("Drag and drop failed with %Rhrc\n", hr));
                            m_enmState = Canceled;
                            rc = VERR_GENERAL_FAILURE; /** @todo Find a better status code. */
                            break;
                    }

                    int rc2 = RTCritSectEnter(&m_CritSect);
                    if (RT_SUCCESS(rc2))
                    {
                        m_startupInfo.pDropSource->Release();
                        m_startupInfo.pDataObject->Release();

                        RT_ZERO(m_startupInfo);

                        rc2 = RTCritSectLeave(&m_CritSect);
                        if (RT_SUCCESS(rc))
                            rc = rc2;
                    }

                    m_enmMode = Unknown;
                }
            }
            else if (m_enmMode == GH) /* Guest to host. */
            {
                /* Starting here VBoxDnDDropTarget should
                 * take over; was instantiated when registering
                 * this proxy window as a (valid) drop target. */
            }
            else
                rc = VERR_NOT_SUPPORTED;

            LogFlowThisFunc(("WM_MOUSEMOVE: mMode=%ld, mState=%ld, rc=%Rrc\n",
                             m_enmMode, m_enmState, rc));
            return 0;
        }

        case WM_NCMOUSEHOVER:
            LogFlowThisFunc(("WM_NCMOUSEHOVER\n"));
            return 0;

        case WM_NCMOUSELEAVE:
            LogFlowThisFunc(("WM_NCMOUSELEAVE\n"));
            return 0;

        case WM_VBOXTRAY_DND_MESSAGE:
        {
            PVBOXDNDEVENT pEvent = (PVBOXDNDEVENT)a_lParam;
            if (!pEvent)
                break; /* No event received, bail out. */

            PVBGLR3DNDEVENT pVbglR3Event = pEvent->pVbglR3Event;
            AssertPtrBreak(pVbglR3Event);

            LogFlowThisFunc(("Received enmType=%RU32\n", pVbglR3Event->enmType));

            int rc;
            switch (pVbglR3Event->enmType)
            {
                case VBGLR3DNDEVENTTYPE_HG_ENTER:
                {
                    if (pVbglR3Event->u.HG_Enter.cbFormats)
                    {
                        RTCList<RTCString> lstFormats =
                            RTCString(pVbglR3Event->u.HG_Enter.pszFormats, pVbglR3Event->u.HG_Enter.cbFormats - 1).split(DND_FORMATS_SEPARATOR_STR);
                        rc = OnHgEnter(lstFormats, pVbglR3Event->u.HG_Enter.dndLstActionsAllowed);
                        if (RT_FAILURE(rc))
                            break;
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
                    rc = OnHgMove(pVbglR3Event->u.HG_Move.uXpos, pVbglR3Event->u.HG_Move.uYpos,
                                  pVbglR3Event->u.HG_Move.dndActionDefault);
                    break;
                }

                case VBGLR3DNDEVENTTYPE_HG_LEAVE:
                {
                    rc = OnHgLeave();
                    break;
                }

                case VBGLR3DNDEVENTTYPE_HG_DROP:
                {
                    rc = OnHgDrop();
                    break;
                }

                /**
                 * The data header now will contain all the (meta) data the guest needs in
                 * order to complete the DnD operation.
                 */
                case VBGLR3DNDEVENTTYPE_HG_RECEIVE:
                {
                    rc = OnHgDataReceive(&pVbglR3Event->u.HG_Received.Meta);
                    break;
                }

                case VBGLR3DNDEVENTTYPE_CANCEL:
                {
                    rc = OnHgCancel();
                    break;
                }

                case VBGLR3DNDEVENTTYPE_QUIT:
                {
                    VBoxTrayInfo("DnD: Received quit message, shutting down ...\n");
                    PostQuitMessage(0);
                }

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
                case VBGLR3DNDEVENTTYPE_GH_ERROR:
                {
                    Reset();
                    rc = VINF_SUCCESS;
                    break;
                }

                case VBGLR3DNDEVENTTYPE_GH_REQ_PENDING:
                {
                    rc = OnGhIsDnDPending();
                    break;
                }

                case VBGLR3DNDEVENTTYPE_GH_DROP:
                {
                    rc = OnGhDrop(pVbglR3Event->u.GH_Drop.pszFormat, pVbglR3Event->u.GH_Drop.dndActionRequested);
                    break;
                }
#endif
                default:
                {
                    VBoxTrayVerbose(1, "DnD: Received unsupported message '%RU32'\n", pVbglR3Event->enmType);
                    rc = VERR_NOT_SUPPORTED;
                    break;
                }
            }

            LogFlowFunc(("Message %RU32 processed with %Rrc\n", pVbglR3Event->enmType, rc));
            if (RT_FAILURE(rc))
            {
                /* Tell the user. */
                VBoxTrayError("DnD: Processing message %RU32 failed with %Rrc\n", pVbglR3Event->enmType, rc);

                /* If anything went wrong, do a reset and start over. */
                Reset();
            }

            if (pEvent)
            {
                VbglR3DnDEventFree(pEvent->pVbglR3Event);
                pEvent->pVbglR3Event = NULL;

                RTMemFree(pEvent);
            }

            return 0;
        }

        default:
            break;
    }

    return DefWindowProc(a_hWnd, a_uMsg, a_wParam, a_lParam);
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH

/**
 * Registers this proxy window as a local drop target.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::RegisterAsDropTarget(void)
{
    if (m_pDropTarget) /* Already registered as drop target? */
        return VINF_SUCCESS;

# ifdef RT_EXCEPTIONS_ENABLED
    try { m_pDropTarget = new VBoxDnDDropTarget(this /* pParent */); }
    catch (std::bad_alloc &)
# else
    m_pDropTarget = new VBoxDnDDropTarget(this /* pParent */);
    if (!m_pDropTarget)
# endif
    {
        LogFunc(("VERR_NO_MEMORY!\n"));
        return VERR_NO_MEMORY;
    }

    HRESULT hrc = CoLockObjectExternal(m_pDropTarget, TRUE /* fLock */, FALSE /* fLastUnlockReleases */);
    if (SUCCEEDED(hrc))
    {
        hrc = RegisterDragDrop(m_hWnd, m_pDropTarget);
        if (SUCCEEDED(hrc))
        {
            LogFlowFuncLeaveRC(VINF_SUCCESS);
            return VINF_SUCCESS;
        }
    }
    if (hrc != DRAGDROP_E_INVALIDHWND) /* Could be because the DnD host service is not available. */
        VBoxTrayError("DnD: Creating drop target failed with hr=%Rhrc\n", hrc);

    LogFlowFuncLeaveRC(VERR_NOT_SUPPORTED);
    return VERR_NOT_SUPPORTED; /* Report back DnD as not being supported. */
}

/**
 * Unregisters this proxy as a drop target.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::UnregisterAsDropTarget(void)
{
    LogFlowFuncEnter();

    if (!m_pDropTarget) /* No drop target? Bail out. */
        return VINF_SUCCESS;

    HRESULT hr = RevokeDragDrop(m_hWnd);
    if (SUCCEEDED(hr))
        hr = CoLockObjectExternal(m_pDropTarget, FALSE /* fLock */,
                                  TRUE /* fLastUnlockReleases */);
    if (SUCCEEDED(hr))
    {
        ULONG cRefs = m_pDropTarget->Release();
        Assert(cRefs == 0); NOREF(cRefs);
        m_pDropTarget = NULL;
    }

    int rc = SUCCEEDED(hr)
           ? VINF_SUCCESS : VERR_GENERAL_FAILURE; /** @todo Fix this. */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

/**
 * Handles the creation of a proxy window.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::OnCreate(void)
{
    LogFlowFuncEnter();
    int rc = VbglR3DnDConnect(&m_cmdCtx);
    if (RT_FAILURE(rc))
    {
        VBoxTrayError("DnD: Connection to host service failed, rc=%Rrc\n", rc);
        return rc;
    }

    LogFlowThisFunc(("Client ID=%RU32, rc=%Rrc\n", m_cmdCtx.uClientID, rc));
    return rc;
}

/**
 * Handles the destruction of a proxy window.
 */
void VBoxDnDWnd::OnDestroy(void)
{
    DestroyWindow(m_hWnd);

    VbglR3DnDDisconnect(&m_cmdCtx);
    LogFlowThisFuncLeave();
}

/**
 * Aborts an in-flight DnD operation on the guest.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::Abort(void)
{
    LogFlowThisFunc(("mMode=%ld, mState=%RU32\n", m_enmMode, m_enmState));
    VBoxTrayInfo("DnD: Drag and drop operation aborted\n");

    int rc = RTCritSectEnter(&m_CritSect);
    if (RT_SUCCESS(rc))
    {
        if (m_startupInfo.pDataObject)
            m_startupInfo.pDataObject->Abort();

        RTCritSectLeave(&m_CritSect);
    }

    /* Post ESC to our window to officially abort the
     * drag and drop operation. */
    this->PostMessage(WM_KEYDOWN, VK_ESCAPE /* wParam */, 0 /* lParam */);

    Reset();

    return rc;
}

/**
 * Handles actions required when the host cursor enters
 * the guest's screen to initiate a host -> guest DnD operation.
 *
 * @return  VBox status code.
 * @param   a_lstFormats            Supported formats offered by the host.
 * @param   a_fDndLstActionsAllowed Supported actions offered by the host.
 */
int VBoxDnDWnd::OnHgEnter(const RTCList<RTCString> &a_lstFormats, VBOXDNDACTIONLIST a_fDndLstActionsAllowed)
{
    if (m_enmMode == GH) /* Wrong mode? Bail out. */
        return VERR_WRONG_ORDER;

#ifdef DEBUG
    LogFlowThisFunc(("dndActionList=0x%x, a_lstFormats=%zu: ", a_fDndLstActionsAllowed, a_lstFormats.size()));
    for (size_t i = 0; i < a_lstFormats.size(); i++)
        LogFlow(("'%s' ", a_lstFormats.at(i).c_str()));
    LogFlow(("\n"));
#endif

    Reset();
    setMode(HG);

    /* Check if the VM session has changed and reconnect to the HGCM service if necessary. */
    int rc = checkForSessionChange();
    if (RT_FAILURE(rc))
        return rc;

    /* Save all allowed actions. */
    this->m_lstActionsAllowed = a_fDndLstActionsAllowed;

    /*
     * Check if reported formats from host are compatible with this client.
     */
    size_t cFormatsSup    = this->m_lstFmtSup.size();
    ULONG  cFormatsActive = 0;

    LPFORMATETC paFormatEtc = (LPFORMATETC)RTMemTmpAllocZ(sizeof(paFormatEtc[0]) * cFormatsSup);
    AssertReturn(paFormatEtc, VERR_NO_TMP_MEMORY);

    LPSTGMEDIUM paStgMeds   = (LPSTGMEDIUM)RTMemTmpAllocZ(sizeof(paStgMeds[0]) * cFormatsSup);
    AssertReturnStmt(paFormatEtc, RTMemTmpFree(paFormatEtc), VERR_NO_TMP_MEMORY);

    VBoxTrayVerbose(1, "DnD: Reported formats:\n");
    for (size_t i = 0; i < a_lstFormats.size(); i++)
    {
        bool fSupported = false;
        for (size_t a = 0; a < this->m_lstFmtSup.size(); a++)
        {
            const char *pszFormat = a_lstFormats.at(i).c_str();
            LogFlowThisFunc(("\t\"%s\" <=> \"%s\"\n", this->m_lstFmtSup.at(a).c_str(), pszFormat));

            fSupported = RTStrICmp(this->m_lstFmtSup.at(a).c_str(), pszFormat) == 0;
            if (fSupported)
            {
                this->m_lstFmtActive.append(a_lstFormats.at(i));

                /** @todo Put this into a \#define / struct. */
                if (!RTStrICmp(pszFormat, "text/uri-list"))
                {
                    paFormatEtc[cFormatsActive].cfFormat = CF_HDROP;
                    paFormatEtc[cFormatsActive].dwAspect = DVASPECT_CONTENT;
                    paFormatEtc[cFormatsActive].lindex   = -1;
                    paFormatEtc[cFormatsActive].tymed    = TYMED_HGLOBAL;

                    paStgMeds  [cFormatsActive].tymed    = TYMED_HGLOBAL;
                    cFormatsActive++;
                }
                else if (   !RTStrICmp(pszFormat, "text/plain")
                         || !RTStrICmp(pszFormat, "text/html")
                         || !RTStrICmp(pszFormat, "text/plain;charset=utf-8")
                         || !RTStrICmp(pszFormat, "text/plain;charset=utf-16")
                         || !RTStrICmp(pszFormat, "text/plain")
                         || !RTStrICmp(pszFormat, "text/richtext")
                         || !RTStrICmp(pszFormat, "UTF8_STRING")
                         || !RTStrICmp(pszFormat, "TEXT")
                         || !RTStrICmp(pszFormat, "STRING"))
                {
                    paFormatEtc[cFormatsActive].cfFormat = CF_TEXT;
                    paFormatEtc[cFormatsActive].dwAspect = DVASPECT_CONTENT;
                    paFormatEtc[cFormatsActive].lindex   = -1;
                    paFormatEtc[cFormatsActive].tymed    = TYMED_HGLOBAL;

                    paStgMeds  [cFormatsActive].tymed    = TYMED_HGLOBAL;
                    cFormatsActive++;
                }
                else /* Should never happen. */
                    AssertReleaseMsgFailedBreak(("Format specification for '%s' not implemented\n", pszFormat));
                break;
            }
        }

        VBoxTrayVerbose(1, "DnD: \t%s: %RTbool\n", a_lstFormats.at(i).c_str(), fSupported);
    }

    if (g_cVerbosity)
    {
        RTCString strMsg("Enter: Host -> Guest\n");
        strMsg += RTCStringFmt("Allowed actions: ");
        char *pszActions = DnDActionListToStrA(a_fDndLstActionsAllowed);
        AssertPtrReturn(pszActions, VERR_NO_STR_MEMORY);
        strMsg += pszActions;
        RTStrFree(pszActions);
        strMsg += "\nFormats: ";
        for (size_t i = 0; i < this->m_lstFmtActive.size(); i++)
        {
            if (i > 0)
                strMsg += ", ";
            strMsg += this->m_lstFmtActive[i];
        }

        VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                    strMsg.c_str(), VBOX_DND_SHOWBALLOON_HEADER,
                                    15 * 1000 /* Time to display in msec */, NIIF_INFO);
    }

    /*
     * Warn in the log if this guest does not accept anything.
     */
    Assert(cFormatsActive <= cFormatsSup);
    if (cFormatsActive)
    {
        VBoxTrayVerbose(1, "DnD: %RU32 supported formats found:\n", cFormatsActive);
        for (size_t i = 0; i < cFormatsActive; i++)
            VBoxTrayVerbose(1, "DnD: \t%s\n", this->m_lstFmtActive.at(i).c_str());
    }
    else
        VBoxTrayInfo("DnD: Warning: No supported drag and drop formats on the guest found!\n");

    /*
     * Prepare the startup info for DoDragDrop().
     */

    /* Translate our drop actions into allowed Windows drop effects. */
    m_startupInfo.dwOKEffects = DROPEFFECT_NONE;
    if (a_fDndLstActionsAllowed)
    {
        if (a_fDndLstActionsAllowed & VBOX_DND_ACTION_COPY)
            m_startupInfo.dwOKEffects |= DROPEFFECT_COPY;
        if (a_fDndLstActionsAllowed & VBOX_DND_ACTION_MOVE)
            m_startupInfo.dwOKEffects |= DROPEFFECT_MOVE;
        if (a_fDndLstActionsAllowed & VBOX_DND_ACTION_LINK)
            m_startupInfo.dwOKEffects |= DROPEFFECT_LINK;
    }

    VBoxTrayVerbose(1, "DnD: Supported drop actions: 0x%x\n", m_startupInfo.dwOKEffects);

#ifdef RT_EXCEPTIONS_ENABLED
    try
    {
        m_startupInfo.pDropSource = new VBoxDnDDropSource(this);
        m_startupInfo.pDataObject = new VBoxDnDDataObject(paFormatEtc, paStgMeds, cFormatsActive);
    }
    catch (std::bad_alloc &)
#else
    m_startupInfo.pDropSource = new VBoxDnDDropSource(this);
    m_startupInfo.pDataObject = new VBoxDnDDataObject(paFormatEtc, paStgMeds, cFormatsActive);
    if (!m_startupInfo.pDropSource || !m_startupInfo.pDataObject)
#endif
    {
        LogFunc(("VERR_NO_MEMORY!"));
        rc = VERR_NO_MEMORY;
    }

    RTMemTmpFree(paFormatEtc);
    RTMemTmpFree(paStgMeds);

    if (RT_SUCCESS(rc))
        rc = makeFullscreen();

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles actions required when the host cursor moves inside
 * the guest's screen.
 *
 * @return  VBox status code.
 * @param   u32xPos                 Absolute X position (in pixels) of the host cursor
 *                                  inside the guest.
 * @param   u32yPos                 Absolute Y position (in pixels) of the host cursor
 *                                  inside the guest.
 * @param   dndAction               Action the host wants to perform while moving.
 *                                  Currently ignored.
 */
int VBoxDnDWnd::OnHgMove(uint32_t u32xPos, uint32_t u32yPos, VBOXDNDACTION dndAction)
{
    RT_NOREF(dndAction);
    int rc;

    uint32_t uActionNotify = VBOX_DND_ACTION_IGNORE;
    if (m_enmMode == HG)
    {
        LogFlowThisFunc(("u32xPos=%RU32, u32yPos=%RU32, dndAction=0x%x\n",
                         u32xPos, u32yPos, dndAction));

        rc = mouseMove(u32xPos, u32yPos, MOUSEEVENTF_LEFTDOWN);

        if (RT_SUCCESS(rc))
            rc = RTCritSectEnter(&m_CritSect);
        if (RT_SUCCESS(rc))
        {
            if (   (Dragging == m_enmState)
                && m_startupInfo.pDropSource)
                uActionNotify = m_startupInfo.pDropSource->GetCurrentAction();

            RTCritSectLeave(&m_CritSect);
        }
    }
    else /* Just acknowledge the operation with an ignore action. */
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        rc = VbglR3DnDHGSendAckOp(&m_cmdCtx, uActionNotify);
        if (RT_FAILURE(rc))
            LogFlowThisFunc(("Acknowledging operation failed with rc=%Rrc\n", rc));
    }

    LogFlowThisFunc(("Returning uActionNotify=0x%x, rc=%Rrc\n", uActionNotify, rc));
    return rc;
}

/**
 * Handles actions required when the host cursor leaves
 * the guest's screen again.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::OnHgLeave(void)
{
    if (m_enmMode == GH) /* Wrong mode? Bail out. */
        return VERR_WRONG_ORDER;

    if (g_cVerbosity)
        VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                    "Leave: Host -> Guest", VBOX_DND_SHOWBALLOON_HEADER,
                                    15 * 1000 /* Time to display in msec */, NIIF_INFO);

    int rc = Abort();

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles actions required when the host cursor wants to drop
 * and therefore start a "drop" action in the guest.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::OnHgDrop(void)
{
    if (m_enmMode == GH)
        return VERR_WRONG_ORDER;

    LogFlowThisFunc(("mMode=%ld, mState=%RU32\n", m_enmMode, m_enmState));

    int rc = VINF_SUCCESS;
    if (m_enmState == Dragging)
    {
        if (g_cVerbosity)
            VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                        "Drop: Host -> Guest", VBOX_DND_SHOWBALLOON_HEADER,
                                        15 * 1000 /* Time to display in msec */, NIIF_INFO);

        if (m_lstFmtActive.size() >= 1)
        {
            /** @todo What to do when multiple formats are available? */
            m_strFmtReq = m_lstFmtActive.at(0);

            rc = RTCritSectEnter(&m_CritSect);
            if (RT_SUCCESS(rc))
            {
                if (m_startupInfo.pDataObject)
                    m_startupInfo.pDataObject->SetStatus(VBoxDnDDataObject::Status_Dropping);
                else
                    rc = VERR_NOT_FOUND;

                RTCritSectLeave(&m_CritSect);
            }

            if (RT_SUCCESS(rc))
            {
                VBoxTrayInfo("DnD: Requesting data as '%s' ...\n", m_strFmtReq.c_str());
                rc = VbglR3DnDHGSendReqData(&m_cmdCtx, m_strFmtReq.c_str());
                if (RT_FAILURE(rc))
                    LogFlowThisFunc(("Requesting data failed with rc=%Rrc\n", rc));
            }

        }
        else /* Should never happen. */
            VBoxTrayError("DnD: Host did not specify a data format for drop data\n");
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles actions required when the host has sent over DnD data
 * to the guest after a "drop" event.
 *
 * @return  VBox status code.
 * @param   pMeta                   Pointer to meta data received.
 */
int VBoxDnDWnd::OnHgDataReceive(PVBGLR3GUESTDNDMETADATA pMeta)
{
    LogFlowThisFunc(("mState=%ld, enmMetaType=%RU32\n", m_enmState, pMeta->enmType));

    int rc = RTCritSectEnter(&m_CritSect);
    if (RT_SUCCESS(rc))
    {
        m_enmState = Dropped;

        if (m_startupInfo.pDataObject)
        {
            switch (pMeta->enmType)
            {
                case VBGLR3GUESTDNDMETADATATYPE_RAW:
                {
                    AssertBreakStmt(pMeta->u.Raw.pvMeta != NULL, rc = VERR_INVALID_POINTER);
                    AssertBreakStmt(pMeta->u.Raw.cbMeta, rc = VERR_INVALID_PARAMETER);

                    rc = m_startupInfo.pDataObject->Signal(m_strFmtReq, pMeta->u.Raw.pvMeta, pMeta->u.Raw.cbMeta);
                    break;
                }

                case VBGLR3GUESTDNDMETADATATYPE_URI_LIST:
                {
                    VBoxTrayVerbose(1, "DnD: URI transfer root directory is '%s'\n",
                                    DnDTransferListGetRootPathAbs(&pMeta->u.URI.Transfer));

                    char  *pszBuf;
                    size_t cbBuf;
                    /* Note: The transfer list already has its root set to a temporary directory, so no need to set/add a new
                     *       path base here. */
                    rc = DnDTransferListGetRootsEx(&pMeta->u.URI.Transfer, DNDTRANSFERLISTFMT_NATIVE, NULL /* pszPathBase */,
                                                   DND_PATH_SEPARATOR_STR, &pszBuf, &cbBuf);
                    if (RT_SUCCESS(rc))
                    {
                        rc = m_startupInfo.pDataObject->Signal(m_strFmtReq, pszBuf, cbBuf);
                        RTStrFree(pszBuf);
                    }
                    break;
                }

                default:
                    AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
                    break;
            }
        }
        else
            rc = VERR_NOT_FOUND;

        int rc2 = mouseRelease();
        if (RT_SUCCESS(rc))
            rc = rc2;

        RTCritSectLeave(&m_CritSect);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles actions required when the host wants to cancel the current
 * host -> guest operation.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::OnHgCancel(void)
{
    return Abort();
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
/**
 * Handles actions required to start a guest -> host DnD operation.
 * This works by letting the host ask whether a DnD operation is pending
 * on the guest. The guest must not know anything about the host's DnD state
 * and/or operations due to security reasons.
 *
 * To capture a pending DnD operation on the guest which then can be communicated
 * to the host the proxy window needs to be registered as a drop target. This drop
 * target then will act as a proxy target between the guest OS and the host. In other
 * words, the guest OS will use this proxy target as a regular (invisible) window
 * which can be used by the regular guest OS' DnD mechanisms, independently of the
 * host OS. To make sure this proxy target is able receive an in-progress DnD operation
 * on the guest, it will be shown invisibly across all active guest OS screens. Just
 * think of an opened umbrella across all screens here.
 *
 * As soon as the proxy target and its underlying data object receive appropriate
 * DnD messages they'll be hidden again, and the control will be transferred back
 * this class again.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::OnGhIsDnDPending(void)
{
    LogFlowThisFunc(("mMode=%ld, mState=%ld\n", m_enmMode, m_enmState));

    if (m_enmMode == Unknown)
        setMode(GH);

    if (m_enmMode != GH)
        return VERR_WRONG_ORDER;

    if (m_enmState == Uninitialized)
    {
        /* Nothing to do here yet. */
        m_enmState = Initialized;
    }

    int rc;
    if (m_enmState == Initialized)
    {
        /* Check if the VM session has changed and reconnect to the HGCM service if necessary. */
        rc = checkForSessionChange();
        if (RT_SUCCESS(rc))
        {
            rc = makeFullscreen();
            if (RT_SUCCESS(rc))
            {
                /*
                 * We have to release the left mouse button to
                 * get into our (invisible) proxy window.
                 */
                mouseRelease();

                /*
                 * Even if we just released the left mouse button
                 * we're still in the dragging state to handle our
                 * own drop target (for the host).
                 */
                m_enmState = Dragging;
            }
        }
    }
    else
        rc = VINF_SUCCESS;

    /**
     * Some notes regarding guest cursor movement:
     * - The host only sends an HOST_DND_FN_GH_REQ_PENDING message to the guest
     *   if the mouse cursor is outside the VM's window.
     * - The guest does not know anything about the host's cursor
     *   position / state due to security reasons.
     * - The guest *only* knows that the host currently is asking whether a
     *   guest DnD operation is in progress.
     */

    if (   RT_SUCCESS(rc)
        && m_enmState == Dragging)
    {
        /** @todo Put this block into a function! */
        POINT p;
        GetCursorPos(&p);
        ClientToScreen(m_hWnd, &p);
#ifdef DEBUG_andy
        LogFlowThisFunc(("Client to screen curX=%ld, curY=%ld\n", p.x, p.y));
#endif

        /** @todo Multi-monitor setups? */
#if 0 /* unused */
        int iScreenX = GetSystemMetrics(SM_CXSCREEN) - 1;
        int iScreenY = GetSystemMetrics(SM_CYSCREEN) - 1;
#endif

        LONG px = p.x;
        if (px <= 0)
            px = 1;
        LONG py = p.y;
        if (py <= 0)
            py = 1;

        rc = mouseMove(px, py, 0 /* dwMouseInputFlags */);
    }

    if (RT_SUCCESS(rc))
    {
        VBOXDNDACTION dndActionDefault = VBOX_DND_ACTION_IGNORE;

        AssertPtr(m_pDropTarget);
        RTCString strFormats = m_pDropTarget->Formats();
        if (!strFormats.isEmpty())
        {
            dndActionDefault = VBOX_DND_ACTION_COPY;

            LogFlowFunc(("Acknowledging pDropTarget=0x%p, dndActionDefault=0x%x, dndLstActionsAllowed=0x%x, strFormats=%s\n",
                         m_pDropTarget, dndActionDefault, m_lstActionsAllowed, strFormats.c_str()));
        }
        else
        {
            strFormats = "unknown"; /* Prevent VERR_IO_GEN_FAILURE for IOCTL. */
            LogFlowFunc(("No format data from proxy window available yet\n"));
        }

        /** @todo Support more than one action at a time. */
        m_lstActionsAllowed = dndActionDefault;

        int rc2 = VbglR3DnDGHSendAckPending(&m_cmdCtx,
                                            dndActionDefault, m_lstActionsAllowed,
                                            strFormats.c_str(), (uint32_t)strFormats.length() + 1 /* Include termination */);
        if (RT_FAILURE(rc2))
        {
            char szMsg[256]; /* Sizes according to MSDN. */
            char szTitle[64];

            /** @todo Add some i18l tr() macros here. */
            RTStrPrintf(szTitle, sizeof(szTitle), "VirtualBox Guest Additions Drag and Drop");
            RTStrPrintf(szMsg, sizeof(szMsg), "Drag and drop to the host either is not supported or disabled. "
                                              "Please enable Guest to Host or Bidirectional drag and drop mode "
                                              "or re-install the VirtualBox Guest Additions.");
            switch (rc2)
            {
                case VERR_ACCESS_DENIED:
                {
                    rc = VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                                     szMsg, szTitle,
                                                     15 * 1000 /* Time to display in msec */, NIIF_INFO);
                    AssertRC(rc);
                    break;
                }

                default:
                    break;
            }

            VBoxTrayVerbose(1, "DnD: Host refuses drag and drop operation from guest: %Rrc\n", rc2);
            Reset();
        }
    }

    if (RT_FAILURE(rc))
        Reset(); /* Reset state on failure. */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles actions required to let the guest know that the host
 * started a "drop" action on the host. This will tell the guest
 * to send data in a specific format the host requested.
 *
 * @return  VBox status code.
 * @param   pszFormat               Format the host requests the data in.
 * @param   cbFormat                Size (in bytes) of format string.
 * @param   dndActionDefault        Default action on the host.
 */
int VBoxDnDWnd::OnGhDrop(const RTCString &strFormat, uint32_t dndActionDefault)
{
    LogFlowThisFunc(("mMode=%ld, mState=%ld, pDropTarget=0x%p, strFormat=%s, dndActionDefault=0x%x\n",
                     m_enmMode, m_enmState, m_pDropTarget, strFormat.c_str(), dndActionDefault));
    int rc;
    if (m_enmMode == GH)
    {
        if (g_cVerbosity)
        {
            RTCString strMsg("Drop: Guest -> Host\n\n");
            strMsg += RTCStringFmt("Action: %#x\n", dndActionDefault);
            strMsg += RTCStringFmt("Format: %s\n", strFormat.c_str());

            VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                        strMsg.c_str(), VBOX_DND_SHOWBALLOON_HEADER,
                                        15 * 1000 /* Time to display in msec */, NIIF_INFO);
        }

        if (m_enmState == Dragging)
        {
            AssertPtr(m_pDropTarget);
            rc = m_pDropTarget->WaitForDrop(5 * 1000 /* 5s timeout */);

            Reset();
        }
        else if (m_enmState == Dropped)
        {
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_WRONG_ORDER;

        if (RT_SUCCESS(rc))
        {
            /** @todo Respect uDefAction. */
            void *pvData    = m_pDropTarget->DataMutableRaw();
            uint32_t cbData = (uint32_t)m_pDropTarget->DataSize();
            Assert(cbData == m_pDropTarget->DataSize());

            if (   pvData
                && cbData)
            {
                rc = VbglR3DnDGHSendData(&m_cmdCtx, strFormat.c_str(), pvData, cbData);
                LogFlowFunc(("Sent pvData=0x%p, cbData=%RU32, rc=%Rrc\n", pvData, cbData, rc));
            }
            else
                rc = VERR_NO_DATA;
        }
    }
    else
        rc = VERR_WRONG_ORDER;

    if (RT_FAILURE(rc))
    {
        /*
         * If an error occurred or the guest is in a wrong DnD mode,
         * send an error to the host in any case so that the host does
         * not wait for the data it expects from the guest.
         */
        int rc2 = VbglR3DnDSendError(&m_cmdCtx, rc);
        AssertRC(rc2);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

void VBoxDnDWnd::PostMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LogFlowFunc(("Posting message %u\n"));
    BOOL fRc = ::PostMessage(m_hWnd, uMsg, wParam, lParam);
    Assert(fRc); NOREF(fRc);
}

/**
 * Injects a DnD event in this proxy window's Windows
 * event queue. The (allocated) event will be deleted by
 * this class after processing.
 *
 * @return  VBox status code.
 * @param   pEvent                  Event to inject.
 */
int VBoxDnDWnd::ProcessEvent(PVBOXDNDEVENT pEvent)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    BOOL fRc = ::PostMessage(m_hWnd, WM_VBOXTRAY_DND_MESSAGE,
                             0 /* wParm */, (LPARAM)pEvent /* lParm */);
    if (!fRc)
    {
        DWORD dwErr = GetLastError();

        static int s_iBitchedAboutFailedDnDMessages = 0;
        if (s_iBitchedAboutFailedDnDMessages++ < 32)
        {
            VBoxTrayError("DnD: Processing event %p failed with %ld (%Rrc), skipping\n",
                          pEvent, dwErr, RTErrConvertFromWin32(dwErr));
        }

        VbglR3DnDEventFree(pEvent->pVbglR3Event);

        RTMemFree(pEvent);
        pEvent = NULL;

        return RTErrConvertFromWin32(dwErr);
    }

    return VINF_SUCCESS;
}

/**
 * Checks if the VM session has changed (can happen when restoring the VM from a saved state)
 * and do a reconnect to the DnD HGCM service.
 *
 * @returns VBox status code.
 */
int VBoxDnDWnd::checkForSessionChange(void)
{
    uint64_t uSessionID;
    int rc = VbglR3GetSessionId(&uSessionID);
    if (   RT_SUCCESS(rc)
        && uSessionID != m_cmdCtx.uSessionID)
    {
        LogFlowThisFunc(("VM session has changed to %RU64\n", uSessionID));

        rc = VbglR3DnDDisconnect(&m_cmdCtx);
        AssertRC(rc);

        rc = VbglR3DnDConnect(&m_cmdCtx);
        AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Hides the proxy window again.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::Hide(void)
{
#ifdef DEBUG_andy
    LogFlowFunc(("\n"));
#endif
    ShowWindow(m_hWnd, SW_HIDE);

    return VINF_SUCCESS;
}

/**
 * Shows the (invisible) proxy window in fullscreen,
 * spawned across all active guest monitors.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::makeFullscreen(void)
{
    int rc = VINF_SUCCESS;

    RECT r;
    RT_ZERO(r);

    BOOL fRc;
    HDC hDC = GetDC(NULL /* Entire screen */);
    if (hDC)
    {
        fRc = g_pfnEnumDisplayMonitors
            /* EnumDisplayMonitors is not available on NT4. */
            ? g_pfnEnumDisplayMonitors(hDC, NULL, VBoxDnDWnd::MonitorEnumProc, (LPARAM)&r):
              FALSE;

        if (!fRc)
            rc = VERR_NOT_FOUND;
        ReleaseDC(NULL, hDC);
    }
    else
        rc = VERR_ACCESS_DENIED;

    if (RT_FAILURE(rc))
    {
        /* If multi-monitor enumeration failed above, try getting at least the
         * primary monitor as a fallback. */
        r.left   = 0;
        r.top    = 0;
        r.right  = GetSystemMetrics(SM_CXSCREEN);
        r.bottom = GetSystemMetrics(SM_CYSCREEN);

        rc = VINF_SUCCESS;
    }

    if (RT_SUCCESS(rc))
    {
        LONG lStyle = GetWindowLong(m_hWnd, GWL_STYLE);
        SetWindowLong(m_hWnd, GWL_STYLE,
                      lStyle & ~(WS_CAPTION | WS_THICKFRAME));
        LONG lExStyle = GetWindowLong(m_hWnd, GWL_EXSTYLE);
        SetWindowLong(m_hWnd, GWL_EXSTYLE,
                      lExStyle & ~(  WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE
                                   | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        fRc = SetWindowPos(m_hWnd, HWND_TOPMOST,
                           r.left,
                           r.top,
                           r.right  - r.left,
                           r.bottom - r.top,
                             g_cVerbosity
                           ? SWP_SHOWWINDOW | SWP_FRAMECHANGED
                           : SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOACTIVATE);
        if (fRc)
        {
            LogFlowFunc(("Virtual screen is %ld,%ld,%ld,%ld (%ld x %ld)\n",
                         r.left, r.top, r.right, r.bottom,
                         r.right - r.left, r.bottom - r.top));
        }
        else
        {
            DWORD dwErr = GetLastError();
            VBoxTrayError("DnD: Failed to set proxy window position, rc=%Rrc\n",
                          RTErrConvertFromWin32(dwErr));
        }
    }
    else
        VBoxTrayError("DnD: Failed to determine virtual screen size, rc=%Rrc\n", rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Moves the guest mouse cursor to a specific position.
 *
 * @return  VBox status code.
 * @param   x                       X position (in pixels) to move cursor to.
 * @param   y                       Y position (in pixels) to move cursor to.
 * @param   dwMouseInputFlags       Additional movement flags. @sa MOUSEEVENTF_ flags.
 */
int VBoxDnDWnd::mouseMove(int x, int y, DWORD dwMouseInputFlags)
{
    int iScreenX = GetSystemMetrics(SM_CXSCREEN) - 1;
    int iScreenY = GetSystemMetrics(SM_CYSCREEN) - 1;

    INPUT Input[1] = { {0} };
    Input[0].type       = INPUT_MOUSE;
    Input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE
                        | dwMouseInputFlags;
    Input[0].mi.dx      = x * (65535 / iScreenX);
    Input[0].mi.dy      = y * (65535 / iScreenY);

    int rc;
    if (g_pfnSendInput(1 /* Number of inputs */,
                       Input, sizeof(INPUT)))
    {
#ifdef DEBUG_andy
        CURSORINFO ci;
        RT_ZERO(ci);
        ci.cbSize = sizeof(ci);
        BOOL fRc = GetCursorInfo(&ci);
        if (fRc)
            LogFlowThisFunc(("Cursor shown=%RTbool, cursor=0x%p, x=%d, y=%d\n",
                             (ci.flags & CURSOR_SHOWING) ? true : false,
                             ci.hCursor, ci.ptScreenPos.x, ci.ptScreenPos.y));
#endif
        rc = VINF_SUCCESS;
    }
    else
    {
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        LogFlowFunc(("SendInput failed with rc=%Rrc\n", rc));
    }

    return rc;
}

/**
 * Releases a previously pressed left guest mouse button.
 *
 * @return  VBox status code.
 */
int VBoxDnDWnd::mouseRelease(void)
{
    LogFlowFuncEnter();

    int rc;

    /* Release mouse button in the guest to start the "drop"
     * action at the current mouse cursor position. */
    INPUT Input[1] = { {0} };
    Input[0].type       = INPUT_MOUSE;
    Input[0].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (!g_pfnSendInput(1, Input, sizeof(INPUT)))
    {
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        LogFlowFunc(("SendInput failed with rc=%Rrc\n", rc));
    }
    else
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * Resets the proxy window.
 */
void VBoxDnDWnd::Reset(void)
{
    LogFlowThisFunc(("Resetting, old mMode=%ld, mState=%ld\n",
                     m_enmMode, m_enmState));

    /*
     * Note: Don't clear this->lstAllowedFormats at the moment, as this value is initialized
     *       on class creation. We might later want to modify the allowed formats at runtime,
     *       so keep this in mind when implementing this.
     */

    this->m_lstFmtActive.clear();
    this->m_lstActionsAllowed = VBOX_DND_ACTION_IGNORE;

    int rc2 = setMode(Unknown);
    AssertRC(rc2);

    Hide();
}

/**
 * Sets the current operation mode of this proxy window.
 *
 * @return  VBox status code.
 * @param   enmMode                 New mode to set.
 */
int VBoxDnDWnd::setMode(Mode enmMode)
{
    LogFlowThisFunc(("Old mode=%ld, new mode=%ld\n",
                     m_enmMode, enmMode));

    m_enmMode = enmMode;
    m_enmState = Initialized;

    return VINF_SUCCESS;
}

/**
 * Static helper function for having an own WndProc for proxy
 * window instances.
 */
static LRESULT CALLBACK vboxDnDWndProcInstance(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    LONG_PTR pUserData = GetWindowLongPtr(hWnd, GWLP_USERDATA);
    AssertPtrReturn(pUserData, 0);

    VBoxDnDWnd *pWnd = reinterpret_cast<VBoxDnDWnd *>(pUserData);
    if (pWnd)
        return pWnd->WndProc(hWnd, uMsg, wParam, lParam);

    return 0;
}

/**
 * Static helper function for routing Windows messages to a specific
 * proxy window instance.
 */
static LRESULT CALLBACK vboxDnDWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    /* Note: WM_NCCREATE is not the first ever message which arrives, but
     *       early enough for us. */
    if (uMsg == WM_NCCREATE)
    {
        LPCREATESTRUCT pCS = (LPCREATESTRUCT)lParam;
        AssertPtr(pCS);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pCS->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)vboxDnDWndProcInstance);

        return vboxDnDWndProcInstance(hWnd, uMsg, wParam, lParam);
    }

    /* No window associated yet. */
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrDnDPreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrDnDOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
DECLCALLBACK(int) vbtrDnDInit(const PVBOXTRAYSVCENV pEnv, void **ppInstance)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
    AssertPtrReturn(ppInstance, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    PVBOXDNDCONTEXT pCtx = &g_Ctx; /* Only one instance at the moment. */
    AssertPtr(pCtx);

    int rc;
    bool fSupportedOS = true;

    if (VbglR3AutoLogonIsRemoteSession())
    {
        /* Do not do drag and drop for remote sessions. */
        VBoxTrayInfo("DnD: Drag and drop has been disabled for a remote session\n");
        rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        g_pfnSendInput = (PFNSENDINPUT)
            RTLdrGetSystemSymbol("User32.dll", "SendInput");
        fSupportedOS = !RT_BOOL(g_pfnSendInput == NULL);
        g_pfnEnumDisplayMonitors = (PFNENUMDISPLAYMONITORS)
            RTLdrGetSystemSymbol("User32.dll", "EnumDisplayMonitors");
        /* g_pfnEnumDisplayMonitors is optional. */

        if (!fSupportedOS)
        {
            VBoxTrayInfo("DnD: Not supported Windows version, disabling drag and drop support\n");
            rc = VERR_NOT_SUPPORTED;
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Assign service environment to our context. */
        pCtx->pEnv = pEnv;

        /* Create the proxy window. At the moment we
         * only support one window at a time. */
        VBoxDnDWnd *pWnd = NULL;
#ifdef RT_EXCEPTIONS_ENABLED
        try { pWnd = new VBoxDnDWnd(); }
        catch (std::bad_alloc &)
#else
        pWnd = new VBoxDnDWnd();
        if (!pWnd)
#endif
        {
            rc = VERR_NO_MEMORY;
        }
        if (RT_SUCCESS(rc))
        {
            rc = pWnd->Initialize(pCtx);
            if (RT_SUCCESS(rc))
            {
                /* Add proxy window to our proxy windows list. */
#ifdef RT_EXCEPTIONS_ENABLED
                try { pCtx->lstWnd.append(pWnd); /** @todo the list implementation sucks wrt exception handling. */ }
                catch (std::bad_alloc &)
                {
                    delete pWnd;
                    rc = VERR_NO_MEMORY;
                }
#else
                pCtx->lstWnd.append(pWnd); /** @todo the list implementation sucks wrt exception handling. */
#endif
            }
            else
                delete pWnd;
        }
    }

    if (RT_SUCCESS(rc))
        rc = RTSemEventCreate(&pCtx->hEvtQueueSem);
    if (RT_SUCCESS(rc))
    {
        *ppInstance = pCtx;

        VBoxTrayInfo("DnD: Drag and drop service successfully started\n");
    }
    else
        VBoxTrayError("DnD: Initializing drag and drop service failed with rc=%Rrc\n", rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnStop}
 */
DECLCALLBACK(int) vbtrDnDStop(void *pInstance)
{
    AssertPtrReturn(pInstance, VERR_INVALID_POINTER);

    LogFunc(("Stopping pInstance=%p\n", pInstance));

    PVBOXDNDCONTEXT pCtx = (PVBOXDNDCONTEXT)pInstance;
    AssertPtr(pCtx);

    /* Set shutdown indicator. */
    ASMAtomicWriteBool(&pCtx->fShutdown, true);

    /* Disconnect. */
    VbglR3DnDDisconnect(&pCtx->cmdCtx);

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
DECLCALLBACK(void) vbtrDnDDestroy(void *pInstance)
{
    AssertPtrReturnVoid(pInstance);

    LogFunc(("Destroying pInstance=%p\n", pInstance));

    PVBOXDNDCONTEXT pCtx = (PVBOXDNDCONTEXT)pInstance;
    AssertPtr(pCtx);

    /** @todo At the moment we only have one DnD proxy window. */
    Assert(pCtx->lstWnd.size() == 1);
    VBoxDnDWnd *pWnd = pCtx->lstWnd.first();
    if (pWnd)
    {
        delete pWnd;
        pWnd = NULL;
    }

    if (pCtx->hEvtQueueSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pCtx->hEvtQueueSem);
        pCtx->hEvtQueueSem = NIL_RTSEMEVENT;
    }

    LogFunc(("Destroyed pInstance=%p\n", pInstance));
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
DECLCALLBACK(int) vbtrDnDWorker(void *pInstance, bool volatile *pfShutdown)
{
    AssertPtr(pInstance);
    AssertPtr(pfShutdown);

    LogFlowFunc(("pInstance=%p\n", pInstance));

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    PVBOXDNDCONTEXT pCtx = (PVBOXDNDCONTEXT)pInstance;
    AssertPtr(pCtx);

    int rc = VbglR3DnDConnect(&pCtx->cmdCtx);
    if (RT_FAILURE(rc))
        return rc;

    if (g_cVerbosity)
        VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                    RTCStringFmt("Running (worker client ID %RU32)", pCtx->cmdCtx.uClientID).c_str(),
                                    VBOX_DND_SHOWBALLOON_HEADER,
                                    15 * 1000 /* Time to display in msec */, NIIF_INFO);

    /** @todo At the moment we only have one DnD proxy window. */
    Assert(pCtx->lstWnd.size() == 1);
    VBoxDnDWnd *pWnd = pCtx->lstWnd.first();
    AssertPtr(pWnd);

    /* Number of invalid messages skipped in a row. */
    int cMsgSkippedInvalid = 0;
    PVBOXDNDEVENT pEvent = NULL;

    for (;;)
    {
        pEvent = (PVBOXDNDEVENT)RTMemAllocZ(sizeof(VBOXDNDEVENT));
        if (!pEvent)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        /* Note: pEvent will be free'd by the consumer later. */

        PVBGLR3DNDEVENT pVbglR3Event = NULL;
        rc = VbglR3DnDEventGetNext(&pCtx->cmdCtx, &pVbglR3Event);
        if (RT_SUCCESS(rc))
        {
            LogFunc(("enmType=%RU32, rc=%Rrc\n", pVbglR3Event->enmType, rc));

            cMsgSkippedInvalid = 0; /* Reset skipped messages count. */

            VBoxTrayVerbose(2, "DnD: Received new event, type=%RU32, rc=%Rrc\n", pVbglR3Event->enmType, rc);

            /* pEvent now owns pVbglR3Event. */
            pEvent->pVbglR3Event = pVbglR3Event;
            pVbglR3Event         = NULL;

            rc = pWnd->ProcessEvent(pEvent);
            if (RT_SUCCESS(rc))
            {
                /* Event was consumed and the proxy window till take care of the memory -- NULL it. */
                pEvent = NULL;
            }
            else
                VBoxTrayInfo("DnD: Processing proxy window event %RU32 failed with %Rrc\n", pVbglR3Event->enmType, rc);
        }

        if (RT_FAILURE(rc))
        {
            if (pEvent)
            {
                RTMemFree(pEvent);
                pEvent = NULL;
            }

            LogFlowFunc(("Processing next message failed with rc=%Rrc\n", rc));

            /* Old(er) hosts either are broken regarding DnD support or otherwise
             * don't support the stuff we do on the guest side, so make sure we
             * don't process invalid messages forever. */
            if (cMsgSkippedInvalid++ > 32)
            {
                VBoxTrayInfo("DnD: Too many invalid/skipped messages from host, exiting ...\n");
                break;
            }

            /* Make sure our proxy window is hidden when an error occured to
             * not block the guest's UI. */
            int rc2 = pWnd->Abort();
            AssertRC(rc2);
        }

        if (*pfShutdown)
            break;

        if (ASMAtomicReadBool(&pCtx->fShutdown))
            break;

        if (RT_FAILURE(rc)) /* Don't hog the CPU on errors. */
            RTThreadSleep(1000 /* ms */);
    }

    if (pEvent)
    {
        VbglR3DnDEventFree(pEvent->pVbglR3Event);

        RTMemFree(pEvent);
        pEvent = NULL;
    }

    VbglR3DnDDisconnect(&pCtx->cmdCtx);

    VBoxTrayInfo("DnD: Ended\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescDnD =
{
    /* pszName. */
    "draganddrop",
    /* pszDescription. */
    "Drag and Drop",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrDnDPreInit,
    vbtrDnDOption,
    vbtrDnDInit,
    vbtrDnDWorker,
    vbtrDnDStop,
    vbtrDnDDestroy
};

