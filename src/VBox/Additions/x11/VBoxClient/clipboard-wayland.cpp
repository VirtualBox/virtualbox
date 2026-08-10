/* $Id: clipboard-wayland.cpp 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Wayland Shared Clipboard.
 */

/*
 * Copyright (C) 2017-2026 Oracle and/or its affiliates.
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
#include <iprt/pipe.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLibGuestProp.h> /* Needed to get VBCLHOSTINPUTFOCUSSTATE from VBoxClient.h. */

#include "VBoxClient.h"
#include "clipboard.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Handle to thread running vbclClipboardWaylandHostEventThread(). */
static RTTHREAD                 g_hHostClipboardEventThread = NIL_RTTHREAD;
/** The read end of the runloop notification pipe. */
static RTPIPE                   g_hPipeWaylandClipboardNotifyRd = NIL_RTPIPE;
/** The write end of the runloop notification pipe. */
static RTPIPE                   g_hPipeWaylandClipboardNotifyWr = NIL_RTPIPE;
/** The host input focus monitior state  (basic wayland protocol only). */
static VBCLHOSTINPUTFOCUSSTATE  g_WaylandClipboardHostInputFocusState;


/**
 * @callback_method_impl{FNVBGHWAYLANDRLWAKEUPPIPE,
 *      Handles data pending in the runloop wakeup pipe.}
 */
static DECLCALLBACK(int) vbclWaylandClipboardRunloopHandleWakeup(RTPIPE hPipe, bool volatile *pfReturn, void *pvUser)
{
    PSHCLCONTEXT const pCtx = (PSHCLCONTEXT)pvUser;

    do
    {
        VBClLogVerbose(6, "vbclWaylandClipboardRunloopHandleWakeup: reading revision...\n");

        /*
         * Read revision number.
         */
        uint64_t uRevision = 0;
        int rc = RTPipeReadBlocking(hPipe, &uRevision, sizeof(uRevision), NULL);
        if (*pfReturn)
        {
            VBClLogVerbose(2, "vbclWaylandClipboardRunloopHandleWakeup: return flag set (read returned %Rrc/%RX64)\n", rc, uRevision);
            return rc;
        }
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BROKEN_PIPE)
                VBClLogVerbose(2, "vbclWaylandClipboardRunloopHandleWakeup: VERR_BROKEN_PIPE\n");
            else
                VBClLogError("vbclWaylandClipboardRunloopHandleWakeup: RTPipeReadBlocking failed: %Rrc\n", rc);
            return rc;
        }

        /*
         * Enter the critical section and check if the revision still current.
         */
        RTCritSectEnter(&pCtx->Wl.CritSect);
        uint64_t const uCurRevision = pCtx->Wl.uRevision;
        RTCritSectLeave(&pCtx->Wl.CritSect);
        if (uRevision == uCurRevision)
        {
            /* If the revision number is for the host, we start offering the host data. */
            if (SHCLWLCTX_REV_IS_OTHER(uRevision))
            {
                RTERRINFOSTATIC ErrInfo;
                rc = VbghWaylandClipboardMakeDataOffering(&pCtx->Wl, "VBoxClient-clipboard-host-offer",
                                                          "org.virtualbox.VBoxClient.clipboard.host.offer",
                                                          RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    VBClLogError("VbghWaylandClipboardMakeDataOffering failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
            }
            else
                VBClLogVerbose(1, "vbclWaylandClipboardRunloopHandleWakeup: did not expect guest-side revision: %#RX64\n",
                               uRevision);
        }
        else
            VBClLogVerbose(3, "vbclWaylandClipboardRunloopHandleWakeup: outdated revision (read %#RX64, cur %#RX64)\n",
                           uRevision, uCurRevision);

    } while (   !*pfReturn
             && RT_SUCCESS(RTPipeSelectOne(hPipe, 0)));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNHOSTCLIPREPORTFMTS}
 */
static DECLCALLBACK(int) vbclWaylandClipboardHstEvt_HgReportCommon(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    /*
     * Perform common tasks before calling backend code.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    pCtx->Wl.fOtherFormats = fFormats;
    ShClCacheInvalidate(&pCtx->Wl.OtherCache);
    uint64_t const uRevision = (pCtx->Wl.uRevision + 2) & ~(uint64_t)1; /* even numbers for host ownership */
    pCtx->Wl.uRevision = uRevision;
    RTCritSectLeave(&pCtx->Wl.CritSect);

    /*
     * If we don't have any extended clipboard protocols available, launch a
     * new --clipboard-set process.
     */
    if (pCtx->Wl.enmProtocol == SHCLWAYLANDPROTO_WL)
        VBClClipboardWaylandPopupSetAll(pCtx, fFormats);
    /*
     * Otherwise, forward the request to the wayland event thread (main
     * service thread), since we're currently on shcl-hst thread which
     * shouldn't do any wayland stuff.
     */
    else
        RTPipeWriteBlocking(g_hPipeWaylandClipboardNotifyWr, &uRevision, sizeof(uRevision), NULL);

    return VINF_SUCCESS;
}


/**
 * Helper for vbclWaylandClipboardGhRead.
 *
 * Must be called while inside the CritSect. Will have left it upon return.
 */
static int vbclWaylandClipboardHstEvt_GhReadCommonCacheHit(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, PSHCLCACHEENTRY pEntry)
{
    size_t const cbData = pEntry->cbData;
    VBClLogVerbose(4, "vbclWaylandClipboardHstEvt_GhReadCommon: Cache hit for %#x: %#x bytes, transferring to host...\n", uFmt, cbData);
    int rc = VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, uFmt, pEntry->pvData, cbData);
    RTCritSectLeave(&pCtx->Wl.CritSect);
    VBClLogVerbose(4, "vbclWaylandClipboardHstEvt_GhReadCommon: Cache hit for %#x: %#x bytes, rc=%Rrc\n", uFmt, cbData, rc);
    return rc;
}


/**
 * @callback_method_impl{FNHOSTCLIPREAD}
 */
static DECLCALLBACK(int) vbclWaylandClipboardHstEvt_GhReadCommon(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt)
{
    /*
     * Try serve it from the cache first.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    PSHCLCACHEENTRY pEntry = ShClCacheGet(&pCtx->Wl.OurCache, uFmt);
    if (pEntry)
        return vbclWaylandClipboardHstEvt_GhReadCommonCacheHit(pCtx, uFmt, pEntry);

    /*
     * Is the cache filling still ongoing?
     */
    int rc = RTSemEventMultiWait(pCtx->Wl.hOurCacheFilledEvent, 0);
    if (rc == VERR_TIMEOUT)
    {
        RTCritSectLeave(&pCtx->Wl.CritSect);
        VBClLogVerbose(4, "vbclWaylandClipboardHstEvt_GhReadCommon: Waiting for cache to be filled...\n");

        rc = RTSemEventMultiWait(pCtx->Wl.hOurCacheFilledEvent, RT_MS_30SEC);
        if (RT_SUCCESS(rc))
        {
            RTCritSectEnter(&pCtx->Wl.CritSect);
            pEntry = ShClCacheGet(&pCtx->Wl.OurCache, uFmt);
            if (pEntry)
                return vbclWaylandClipboardHstEvt_GhReadCommonCacheHit(pCtx, uFmt, pEntry);
        }
    }
    RTCritSectLeave(&pCtx->Wl.CritSect);

    /*
     * No matching data.
     */
    VBClLogVerbose(2, "vbclWaylandClipboardHstEvt_GhReadCommon: No data for %#x!\n", uFmt);
    return VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, uFmt, NULL, 0);
}


/**
 * @callback_method_impl{FNRTTHREAD,
 *      Thread processing shared clipboard events from the host.}
 */
static DECLCALLBACK(int) vbclClipboardWaylandHostEventThread(RTTHREAD ThreadSelf, void *pvUser)
{
    /* Retrieve the arguments. */
    PSHCLCONTEXT const    pCtx       =    (PSHCLCONTEXT)((void * volatile *)pvUser)[0];
    bool volatile * const pfShutdown = (bool volatile *)((void * volatile *)pvUser)[1];

    /* Notify parent thread that we're up running. */
    RTThreadUserSignal(ThreadSelf);

    /*
     * The main event loop processing host events.
     */
    int rc = VINF_SUCCESS;
    while (!ASMAtomicReadBool(pfShutdown))
    {
        rc = VBClClipboardReadHostEvent(pCtx, vbclWaylandClipboardHstEvt_HgReportCommon, vbclWaylandClipboardHstEvt_GhReadCommon);
        if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
        {
            VBClLogInfo("cannot process host clipboard event: %Rrc\n", rc);
            if (!ASMAtomicReadBool(pfShutdown))
                RTThreadSleep(RT_MS_1SEC / 2);
        }
    }

    VBClLogVerbose(2, "clipboard host event thread exitting: %Rrc\n", rc);
    return rc;
}


/**
 * @interface_method_impl{VBCLHOSTINPUTFOCUSSTATE,pfnFocusExit}
 */
static DECLCALLBACK(bool) vbclClipboardWaylandHostFocusExit(PVBCLHOSTINPUTFOCUSSTATE pThis)
{
    VBClClipboardWaylandPopupGetAll((SHCLCONTEXT *)pThis->pvUser);
    return false;
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnReportOurFormats}
 */
static DECLCALLBACK(void) vbclClipbordWaylandCb_ReportOurFormats(PSHCLWAYLANDCTX pWlCtx, SHCLFORMATS fOurFormats)
{
    SHCLCONTEXT * const pCtx = RT_FROM_MEMBER(pWlCtx, SHCLCONTEXT, Wl);
    int rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fOurFormats);
    if (RT_FAILURE(rc))
        VBClLogVerbose(1, "VbglR3ClipboardReportFormats/%#x failed: %Rrc\n", fOurFormats, rc);
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnQueryRemoteData}
 */
static DECLCALLBACK(int)
vbclClipbordWaylandCb_QueryRemoteData(PSHCLWAYLANDCTX pWlCtx, SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData)
{
    SHCLCONTEXT * const pCtx = RT_FROM_MEMBER(pWlCtx, SHCLCONTEXT, Wl);
    return VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uFmt, ppvData, pcbData);
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnQueryRemoteDataFree}
 */
static DECLCALLBACK(void) vbclClipbordWaylandCb_QueryRemoteDataFree(PSHCLWAYLANDCTX pWlCtx, void *pvData, size_t cbData)
{
    /** @todo VbglR3ClipboardReadDataFree */
    RTMemFree(pvData);
    RT_NOREF(pWlCtx, cbData);
}


/**
 * @note    ASSUMES this is called on the same thread as VBClClipboardWaylandMain.
 * @thread  Wayland
 */
int VBClClipboardWaylandInit(SHCLCONTEXT *pCtx)
{
    VBClHostInputFocusMonitorInit(&g_WaylandClipboardHostInputFocusState);

    RTERRINFOSTATIC ErrInfo;
    int rc = ExplicitlyLoadlibwayland_client(true /*fResolveAllImports*/, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        /*
         * Init the basic wayland clipboard machinery.
         */
        rc = VbghWaylandClipboardInit(&pCtx->Wl, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            VBClLogVerbose(2, "VbghWaylandClipboardInit succeeded. protocol %s\n", pCtx->Wl.pszProtocol);
            pCtx->Wl.pfnReportOurFormats    = vbclClipbordWaylandCb_ReportOurFormats;
            pCtx->Wl.pfnQueryRemoteData     = vbclClipbordWaylandCb_QueryRemoteData;
            pCtx->Wl.pfnQueryRemoteDataFree = vbclClipbordWaylandCb_QueryRemoteDataFree;

            /*
             * Create notification pipe.
             */
            rc = RTPipeCreate(&g_hPipeWaylandClipboardNotifyRd, &g_hPipeWaylandClipboardNotifyWr, 0);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Setup for event listening and offering.
                 */
                if (pCtx->Wl.enmProtocol != SHCLWAYLANDPROTO_WL)
                    rc = VbghWaylandClipboardSetupListening(&pCtx->Wl, RTErrInfoInitStatic(&ErrInfo));
                if (RT_SUCCESS(rc))
                {
                    if (pCtx->Wl.enmProtocol != SHCLWAYLANDPROTO_WL)
                        rc = VbghWaylandClipboardSetupOffer(&pCtx->Wl, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Try connect to the host service.
                         */
                        rc = VbglR3ClipboardConnectEx(&pCtx->CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
                        if (RT_SUCCESS(rc))
                            return VINF_SUCCESS;

                        VBClLogError("VbglR3ClipboardConnectEx: %Rrc\n", rc);
                    }
                    else
                        VBClLogError("VbghWaylandClipboardSetupOffer failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
                }
                else
                    VBClLogError("VbghWaylandClipboardSetupListening failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
            }
            else
                VBClLogError("RTPipeCreate failed: %Rrc\n", rc);
            VbghWaylandClipboardTerm(&pCtx->Wl);
        }
        else
            VBClLogError("VbghWaylandClipboardInit failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
    }
    else
        VBClLogError("ExplicitlyLoadlibwayland_client failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
    return rc;
}


/**
 * @note    ASSUMES this is called on the same thread as VBClClipboardWaylandInit.
 * @thread  Wayland
 */
int VBClClipboardWaylandMain(SHCLCONTEXT *pCtx, bool volatile *pfShutdown)
{
    /*
     * Start the thread checking for focus changes, if required.
     */
    int rc = VINF_SUCCESS;
    if (pCtx->Wl.enmProtocol == SHCLWAYLANDPROTO_WL)
    {
        g_WaylandClipboardHostInputFocusState.pvUser        = pCtx;
        g_WaylandClipboardHostInputFocusState.pfShutdown    = pfShutdown;
        g_WaylandClipboardHostInputFocusState.pfnFocusExit  = vbclClipboardWaylandHostFocusExit;
        rc = VBClHostInputFocusMonitorStart(&g_WaylandClipboardHostInputFocusState, "shcl-foc");
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Start the thread reading host events.
         */
        static void *s_apvArgs[2];
        s_apvArgs[0] = pCtx;
        s_apvArgs[1] = (void *)pfShutdown;
        rc = VBClStartThread(&g_hHostClipboardEventThread, vbclClipboardWaylandHostEventThread, "shcl-hst", s_apvArgs);
        if (RT_SUCCESS(rc))
        {
            /*
             * Process wayland events.
             */
            VbghWaylandRunloopForDisplay(pCtx->Wl.GhCore.pDisplay,
                                         g_hPipeWaylandClipboardNotifyRd, vbclWaylandClipboardRunloopHandleWakeup, pCtx,
                                         NIL_RTPIPE, RT_MS_5SEC, pfShutdown);

            /* Note! We're not doing any thread termination here, as VBoxClient
                     is ASSUMED to be a single service operation, so the main
                     thread will automaticially call VBClClipboardWaylandStop
                     and VBClClipboardWaylandTerm. */

        }
    }
    VBClLogVerbose(3, "VBClClipboardWaylandMain returning (%Rrc)\n", rc);
    return rc;
}


void VBClClipboardWaylandStop(SHCLCONTEXT *pCtx)
{
    RT_NOREF(pCtx);
    VBClLogVerbose(6, "VBClClipboardWaylandStop: 1\n");

    VBClHostInputFocusMonitorStop(&g_WaylandClipboardHostInputFocusState);
    /* vbclClipboardWaylandHostEventThread termination is triggered by the caller (vbclShClStop). */
    VBClLogVerbose(6, "VBClClipboardWaylandStop: 2\n");

    /* Wakeup the main thread. */
    uint64_t uQuitRev = UINT64_MAX;
    RTPipeWriteBlocking(g_hPipeWaylandClipboardNotifyWr, &uQuitRev, sizeof(uQuitRev), NULL);
    VBClLogVerbose(6, "VBClClipboardWaylandStop: 3\n");
}


void VBClClipboardWaylandTerm(SHCLCONTEXT *pCtx)
{
    VBClLogVerbose(6, "VBClClipboardWaylandTerm: 1\n");
    VBClHostInputFocusMonitorTerm(&g_WaylandClipboardHostInputFocusState);
    VBClLogVerbose(6, "VBClClipboardWaylandTerm: 2\n");

    if (g_hHostClipboardEventThread != NIL_RTTHREAD)
    {
        VBClLogVerbose(6, "VBClClipboardWaylandTerm: 3\n");
        int rc = RTThreadWait(g_hHostClipboardEventThread, 1, NULL);
        VBClLogVerbose(6, "VBClClipboardWaylandTerm: 4 - %Rrc\n", rc);
        if (rc == VERR_TIMEOUT)
        {
            RTThreadPoke(g_hHostClipboardEventThread);
            VBClLogVerbose(6, "VBClClipboardWaylandTerm: 5\n");
            RTThreadWait(g_hHostClipboardEventThread, RT_MS_30SEC, NULL);
            VBClLogVerbose(6, "VBClClipboardWaylandTerm: 6\n");
        }
    }

    VBClLogVerbose(6, "VBClClipboardWaylandTerm: 7\n");
    VbghWaylandClipboardTerm(&pCtx->Wl);
    VBClLogVerbose(6, "VBClClipboardWaylandTerm: 8\n");
}

