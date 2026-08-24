/* $Id: ClipboardBackendX11.cpp 115105 2026-08-24 16:57:58Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - X11 backend.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include "GuestShClBackend.h"
#include "../GuestShClBackendPrivate.h"
#include "GuestShClConn.h"
#include <iprt/errcore.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
# include <VBox/GuestHost/clipboard-transfers-http.h>
#endif

/** Test callback overrides applied when constructing a new X11 context. */
static SHCLCALLBACKS g_ShClCallbackOverrides;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Global context information used by the host glue for the X11 clipboard backend.
 */
struct SHCLCONTEXT
{
    /** This mutex is grabbed during any critical operations on the clipboard
     * which might clash with others. */
    RTCRITSECT           CritSect;
    /** X11 context data. */
    SHCLX11CTX           X11;
    /** Main connection to the Shared Clipboard service. */
    GuestShClConn       *pConn;
    /** Event source used for synchronous X11 reads. */
    SHCLEVENTSOURCE      EventSrc;
    /** We set this when we start shutting down as a hint not to post any new
     * requests. */
    bool                 fShuttingDown;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    /** Host-side asynchronous X11 HTTP file-transfer state. */
    SHCLX11TRANSFERSTATE X11TransferState;
    /** Event notifying the preparation worker about a new clipboard offer. */
    RTSEMEVENT           hX11TransferPreparationEvent;
    /** Persistent worker which prepares guest file-transfer URI-list data. */
    RTTHREAD             hX11TransferPreparationThread;
#endif
};


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static DECLCALLBACK(int) shClSvcX11ReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser);
static DECLCALLBACK(int) shClSvcX11RequestDataFromSourceCallback(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static DECLCALLBACK(void) shClSvcX11TransferOnCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(int)  shClSvcX11TransferOnInitCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) shClSvcX11TransferOnDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) shClSvcX11TransferOnCompletedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcCompletion);
static DECLCALLBACK(void) shClSvcX11TransferOnUnregisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx);

static DECLCALLBACK(int) shClSvcX11TransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx);
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
static DECLCALLBACK(int) shClSvcX11TransferPreparationThread(RTTHREAD hThreadSelf, void *pvUser);
# endif
#endif


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/** Resets the exact transfer key bound to an in-progress preparation. */
static void shClSvcX11TransferPreparationResetKey(PSHCLX11TRANSFERSTATE pX11TransferState)
{
    pX11TransferState->idTransfer          = NIL_SHCLTRANSFERID;
    pX11TransferState->uTransferGeneration = NIL_SHCLTRANSFERGEN;
}

/** Resets the exact transfer key backing the advertised URI-list data. */
static void shClSvcX11TransferPublishedResetKey(PSHCLX11TRANSFERSTATE pX11TransferState)
{
    pX11TransferState->idPublishedTransfer          = NIL_SHCLTRANSFERID;
    pX11TransferState->uPublishedTransferGeneration = NIL_SHCLTRANSFERGEN;
}

/** Checks an exact transfer ID and generation against a transfer. */
static bool shClSvcX11TransferKeyMatches(SHCLTRANSFERID idTransfer, SHCLTRANSFERGEN uGeneration,
                                         PSHCLTRANSFER pTransfer)
{
    return    idTransfer  != NIL_SHCLTRANSFERID
           && uGeneration != NIL_SHCLTRANSFERGEN
           && idTransfer  == ShClTransferGetID(pTransfer)
           && uGeneration == ShClTransferGetGeneration(pTransfer);
}

/** Checks whether a worker result still belongs to the current URI offer. */
static bool shClSvcX11TransferOfferIsCurrent(PSHCLCONTEXT pCtx, uint64_t uOfferGeneration)
{
    int vrc = RTCritSectEnter(&pCtx->CritSect);
    AssertRCReturn(vrc, false);

    bool const fCurrent =    !pCtx->fShuttingDown
                          && pCtx->X11TransferState.uOfferGeneration == uOfferGeneration
                          && (pCtx->X11TransferState.fFormats & VBOX_SHCL_FMT_URI_LIST);

    vrc = RTCritSectLeave(&pCtx->CritSect);
    AssertRC(vrc);
    return fCurrent;
}

/**
 * Discards the unused transfer backing a URI-list offer hidden by a newer offer.
 *
 * A transfer which has already started serving an HTTP request must remain
 * alive until that request finishes.  Otherwise it is safe to destroy the
 * transfer after the URI targets have been removed from X11.
 *
 * @thread X11 transfer preparation worker.
 */
static void shClSvcX11TransferPublishedCancel(PSHCLCONTEXT pCtx)
{
    SHCLTRANSFERID  idTransfer;
    SHCLTRANSFERGEN uGeneration;

    int vrc = RTCritSectEnter(&pCtx->CritSect);
    AssertRCReturnVoid(vrc);

    idTransfer  = pCtx->X11TransferState.idPublishedTransfer;
    uGeneration = pCtx->X11TransferState.uPublishedTransferGeneration;
    shClSvcX11TransferPublishedResetKey(&pCtx->X11TransferState);

    vrc = RTCritSectLeave(&pCtx->CritSect);
    AssertRC(vrc);

    if (idTransfer == NIL_SHCLTRANSFERID)
        return;

    PSHCLTRANSFER pTransfer = pCtx->pConn->transferGetByIdRetained(idTransfer);
    if (   pTransfer
        && shClSvcX11TransferKeyMatches(idTransfer, uGeneration, pTransfer))
    {
        SHCLTRANSFERSTATUS const enmStatus = ShClTransferGetStatus(pTransfer);
        if (enmStatus != SHCLTRANSFERSTATUS_STARTED)
        {
            ShClTransferRelease(pTransfer);
            pCtx->pConn->transferDestroyById(idTransfer);
        }
        else
        {
            LogRel2(("Shared Clipboard: Keeping superseded X11 transfer %RU16/%RU64 alive while it is in use\n",
                     idTransfer, uGeneration));
            ShClTransferRelease(pTransfer);
        }
    }
    else
    {
        if (pTransfer)
            ShClTransferRelease(pTransfer);
        LogRel2(("Shared Clipboard: Published X11 transfer %RU16/%RU64 was already gone or replaced\n",
                 idTransfer, uGeneration));
    }
}

/** Starts the persistent host-side X11 transfer preparation worker. */
static int shClSvcX11TransferPreparationStart(PSHCLCONTEXT pCtx)
{
    RT_ZERO(pCtx->X11TransferState);
    shClSvcX11TransferPreparationResetKey(&pCtx->X11TransferState);
    shClSvcX11TransferPublishedResetKey(&pCtx->X11TransferState);
    pCtx->hX11TransferPreparationEvent  = NIL_RTSEMEVENT;
    pCtx->hX11TransferPreparationThread = NIL_RTTHREAD;

    int vrc = RTSemEventCreate(&pCtx->hX11TransferPreparationEvent);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTThreadCreate(&pCtx->hX11TransferPreparationThread, shClSvcX11TransferPreparationThread,
                             pCtx, 0 /* cbStack */, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "ShClX11Tx");
        if (RT_SUCCESS(vrc))
        {
            vrc = RTThreadUserWait(pCtx->hX11TransferPreparationThread, RT_MS_30SEC);
            if (RT_SUCCESS(vrc))
                return VINF_SUCCESS;

            pCtx->fShuttingDown = true;
            int const vrcSignal = RTSemEventSignal(pCtx->hX11TransferPreparationEvent);
            AssertFatalMsgRC(vrcSignal, ("Signalling the X11 transfer preparation worker after startup failure"
                                         " failed with %Rrc\n", vrcSignal));
            int const vrcWait = RTThreadWait(pCtx->hX11TransferPreparationThread, RT_INDEFINITE_WAIT, NULL);
            AssertFatalMsgRC(vrcWait, ("Reaping the X11 transfer preparation worker after startup failure"
                                       " failed with %Rrc\n", vrcWait));
            pCtx->hX11TransferPreparationThread = NIL_RTTHREAD;
        }

        RTSemEventDestroy(pCtx->hX11TransferPreparationEvent);
        pCtx->hX11TransferPreparationEvent = NIL_RTSEMEVENT;
    }

    return vrc;
}

/** Stops the transfer preparation worker before its X11 and client contexts are released. */
static int shClSvcX11TransferPreparationStop(PSHCLCONTEXT pCtx)
{
    if (pCtx->hX11TransferPreparationThread == NIL_RTTHREAD)
    {
        pCtx->fShuttingDown = true;
        return VINF_SUCCESS;
    }

    int vrc = RTCritSectEnter(&pCtx->CritSect);
    AssertFatalMsgRC(vrc, ("Entering X11 backend critical section during shutdown failed with %Rrc\n", vrc));
    pCtx->fShuttingDown = true;
    pCtx->X11TransferState.uOfferGeneration++;
    vrc = RTCritSectLeave(&pCtx->CritSect);
    AssertFatalMsgRC(vrc, ("Leaving the X11 backend critical section during shutdown failed with %Rrc\n", vrc));

    int vrc2 = RTSemEventSignal(pCtx->hX11TransferPreparationEvent);
    AssertFatalMsgRC(vrc2, ("Signalling the X11 transfer preparation worker during shutdown failed with %Rrc\n", vrc2));
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    vrc2 = RTThreadWait(pCtx->hX11TransferPreparationThread, RT_INDEFINITE_WAIT, NULL);
    AssertFatalMsgRC(vrc2, ("Reaping the X11 transfer preparation worker during shutdown failed with %Rrc\n", vrc2));
    pCtx->hX11TransferPreparationThread = NIL_RTTHREAD;

    vrc2 = RTSemEventDestroy(pCtx->hX11TransferPreparationEvent);
    AssertFatalMsgRC(vrc2, ("Destroying the X11 transfer preparation event failed with %Rrc\n", vrc2));
    if (RT_SUCCESS(vrc))
        vrc = vrc2;
    pCtx->hX11TransferPreparationEvent = NIL_RTSEMEVENT;

    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */


/*********************************************************************************************************************************
*   Backend implementation                                                                                                       *
*********************************************************************************************************************************/
/**
 * Initializes the process-wide X11 clipboard backend.
 *
 * @returns VBox status code.
 */
static int shClBackendX11Init(void)
{
    LogFlowFuncEnter();

    RT_ZERO(g_ShClCallbackOverrides);

    return VINF_SUCCESS;
}

/**
 * Destroys the process-wide X11 clipboard backend.
 */
static void shClBackendX11Destroy(void)
{
    LogFlowFuncEnter();
}

/**
 * Replaces the X11 callback table for testing.
 *
 * @param   pCallbacks          Callback overrides, or NULL to restore defaults.
 */
static void shClBackendX11SetCallbacks(PSHCLCALLBACKS pCallbacks)
{
    RT_ZERO(g_ShClCallbackOverrides);
    if (!pCallbacks)
        return;

#define SET_FN_IF_NOT_NULL(a_Fn) \
    if (pCallbacks->pfn##a_Fn) \
        g_ShClCallbackOverrides.pfn##a_Fn = pCallbacks->pfn##a_Fn;

    SET_FN_IF_NOT_NULL(ReportFormats);
    SET_FN_IF_NOT_NULL(OnClipboardRead);
    SET_FN_IF_NOT_NULL(OnClipboardWrite);
    SET_FN_IF_NOT_NULL(OnRequestDataFromSource);
    SET_FN_IF_NOT_NULL(OnSendDataToDest);

#undef SET_FN_IF_NOT_NULL
}

/**
 * Connects a Main service connection to the X11 clipboard backend.
 *
 * @returns VBox status code.
 * @param   pConn               Main service connection to associate.
 * @param   ppCtx               Where to return the allocated backend context.
 *
 * @note    On the host, another application is assumed to own the clipboard;
 *          ownership remains with X11 until guest formats are announced.
 */
static int shClBackendX11Connect(GuestShClConn *pConn, PSHCLCONTEXT *ppCtx)
{
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(ppCtx, VERR_INVALID_POINTER);

    *ppCtx = NULL;

    int vrc;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    if (pCtx)
    {
        vrc = RTCritSectInit(&pCtx->CritSect);
        if (RT_SUCCESS(vrc))
        {
            vrc = ShClEventSourceInit(&pCtx->EventSrc, 0 /* idEvtSrc */);
            if (RT_FAILURE(vrc))
            {
                RTCritSectDelete(&pCtx->CritSect);
                RTMemFree(pCtx);
                return vrc;
            }

            SHCLCALLBACKS Callbacks;
            RT_ZERO(Callbacks);
            Callbacks.pfnReportFormats           = shClSvcX11ReportFormatsCallback;
            Callbacks.pfnOnRequestDataFromSource = shClSvcX11RequestDataFromSourceCallback;

#define SET_FN_IF_NOT_NULL(a_Fn) \
            if (g_ShClCallbackOverrides.pfn##a_Fn) \
                Callbacks.pfn##a_Fn = g_ShClCallbackOverrides.pfn##a_Fn;

            SET_FN_IF_NOT_NULL(ReportFormats);
            SET_FN_IF_NOT_NULL(OnClipboardRead);
            SET_FN_IF_NOT_NULL(OnClipboardWrite);
            SET_FN_IF_NOT_NULL(OnRequestDataFromSource);
            SET_FN_IF_NOT_NULL(OnSendDataToDest);

#undef SET_FN_IF_NOT_NULL

            vrc = ShClX11Init(&pCtx->X11, &Callbacks, pCtx);
            if (RT_SUCCESS(vrc))
            {
                pCtx->pConn = pConn;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                vrc = shClSvcX11TransferPreparationStart(pCtx);
#endif
                if (RT_SUCCESS(vrc))
                {
                    vrc = ShClX11ThreadStart(&pCtx->X11, true /* grab shared clipboard */);
                    if (RT_SUCCESS(vrc))
                        *ppCtx = pCtx;
                }

                if (RT_FAILURE(vrc))
                {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                    shClSvcX11TransferPreparationStop(pCtx);
                    AssertFatal(pCtx->hX11TransferPreparationThread == NIL_RTTHREAD);
#endif
                    AssertFatal(pCtx->X11.Thread == NIL_RTTHREAD);
                    int const vrcTerm = ShClX11Term(&pCtx->X11);
                    AssertFatalMsgRC(vrcTerm, ("Terminating X11 context after backend startup failure failed with %Rrc\n",
                                               vrcTerm));
                }
            }
            else
            {
                int const vrcTerm = ShClX11Term(&pCtx->X11);
                AssertFatalMsgRC(vrcTerm, ("Terminating partially initialized X11 context failed with %Rrc\n",
                                           vrcTerm));
            }

            if (RT_FAILURE(vrc))
            {
                ShClEventSourceTerm(&pCtx->EventSrc);
                int const vrcDelete = RTCritSectDelete(&pCtx->CritSect);
                AssertFatalMsgRC(vrcDelete, ("Deleting X11 backend critical section after startup failure"
                                             " failed with %Rrc\n", vrcDelete));
            }
        }

        if (RT_FAILURE(vrc))
            RTMemFree(pCtx);
    }
    else
        vrc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Returns the X11 callbacks for a new transfer.
 *
 * @param   pCtx                Connected backend context.
 * @param   pCallbacks          Where to return the callback table.
 */
static void shClBackendX11TransferGetCallbacks(PSHCLCONTEXT pCtx, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturnVoid(pCallbacks);
    RT_ZERO(*pCallbacks);
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pCtx->pConn);

    pCallbacks->pvUser            = pCtx;
    pCallbacks->cbUser            = sizeof(*pCtx);
    pCallbacks->pfnOnCreated      = shClSvcX11TransferOnCreatedCallback;
    pCallbacks->pfnOnInitialize   = shClSvcX11TransferOnInitCallback;
    pCallbacks->pfnOnDestroy      = shClSvcX11TransferOnDestroyCallback;
    pCallbacks->pfnOnCompleted    = shClSvcX11TransferOnCompletedCallback;
    pCallbacks->pfnOnUnregistered = shClSvcX11TransferOnUnregisteredCallback;
}
#endif

/**
 * Synchronizes X11 clipboard state with a connected guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 */
static int shClBackendX11Sync(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx,          VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int const vrc = ShClX11ReportCurrentFormatsToVBoxAsync(&pCtx->X11);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Disconnects and destroys an X11 clipboard backend context.
 *
 * @returns VBox status code.
 * @param   pCtx                Backend context to disconnect.
 */
static int shClBackendX11Disconnect(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx,        VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    /* Stop transfer preparation before releasing either the client or X11
     * context it uses.  This also makes later X11 data requests fail. */
    int vrc;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    vrc = shClSvcX11TransferPreparationStop(pCtx);
    AssertFatal(pCtx->hX11TransferPreparationThread == NIL_RTTHREAD);
#else
    pCtx->fShuttingDown = true;
    vrc = VINF_SUCCESS;
#endif

    int vrc2 = ShClX11ThreadStop(&pCtx->X11);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;
    AssertFatal(pCtx->X11.Thread == NIL_RTTHREAD);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* Transfer callback tables retain pCtx as their user argument.  Destroy
     * all transfers before deleting that context; the service-side client
     * teardown which follows treats an already empty context as a no-op. */
    pCtx->pConn->transferDestroyAll();
#endif
    vrc2 = ShClX11Term(&pCtx->X11);
    AssertFatalMsgRC(vrc2, ("Terminating X11 clipboard context failed with %Rrc\n", vrc2));
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    vrc2 = ShClEventSourceTerm(&pCtx->EventSrc);
    AssertFatalMsgRC(vrc2, ("Terminating X11 backend event source failed with %Rrc\n", vrc2));
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    vrc2 = RTCritSectDelete(&pCtx->CritSect);
    AssertFatalMsgRC(vrc2, ("Deleting X11 backend critical section failed with %Rrc\n", vrc2));
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    RTMemFree(pCtx);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Reports clipboard formats to the host clipboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   fFormats            Guest formats, VBOX_SHCL_FMT_XXX.
 */
static int shClBackendX11ReportFormats(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

#if defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS) && !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP)
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        LogRelMax(16, ("Shared Clipboard: X11 backend cannot expose guest URI-list data because HTTP transfer support is not built in; masking format %#x\n",
                        VBOX_SHCL_FMT_URI_LIST));
        fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
    }
#endif

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    int vrc = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(vrc))
    {
        if (!pCtx->fShuttingDown)
        {
            PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
            pX11TransferState->fFormats = fFormats;
            pX11TransferState->uOfferGeneration++;
            if (!pX11TransferState->uOfferGeneration)
                pX11TransferState->uOfferGeneration = 1;

            /* Remove an old URI target immediately.  The worker adds it back
             * only after the new URI-list data has been prepared and cached. */
            vrc = ShClX11ReportFormatsToX11Async(&pCtx->X11, fFormats & ~VBOX_SHCL_FMT_URI_LIST);

            int vrc2 = RTSemEventSignal(pCtx->hX11TransferPreparationEvent);
            if (RT_SUCCESS(vrc))
                vrc = vrc2;
        }
        else
            vrc = VERR_WRONG_ORDER;

        int const vrc2 = RTCritSectLeave(&pCtx->CritSect);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }
#else
    int vrc = ShClX11ReportFormatsToX11Async(&pCtx->X11, fFormats);
#endif

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Reports formats discovered by X11 to the connected guest.
 *
 * @returns VBox status code.
 * @param   pConn               Main service connection to report through.
 * @param   fFormats            Native formats, VBOX_SHCL_FMT_XXX.
 */
static int shClBackendX11ReportLocalFormats(GuestShClConn *pConn, SHCLFORMATS fFormats)
{
#if defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS) && !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP)
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        LogRelMax(16, ("Shared Clipboard: X11 backend cannot announce host URI-list data because HTTP transfer support is not built in; masking format %#x\n",
                        VBOX_SHCL_FMT_URI_LIST));
        fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
    }
#endif

    return pConn->reportLocalFormats(fFormats);
}

/**
 * Reads data from the host clipboard.
 *
 * Schedules a request to the X11 event thread.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   uFormat             Clipboard format to read.
 * @param   pvData              Destination buffer.
 * @param   cbData              Destination buffer size in bytes.
 * @param   pcbActual           Where to return the actual or required byte count.
 */
static int shClBackendX11ReadData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(pCtx,        VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,      VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual,   VERR_INVALID_POINTER);

    LogFlowFunc(("pConn=%p, uFormat=%#x, pv=%p, cb=%RU32, pcbActual=%p\n",
                 pCtx->pConn, uFormat, pvData, cbData, pcbActual));

    uint32_t cbRead;
    int vrc = ShClX11ReadDataFromX11(&pCtx->X11, &pCtx->EventSrc,
                                     SHCL_TIMEOUT_DEFAULT_MS, uFormat, pvData, cbData, &cbRead);
    if (RT_SUCCESS(vrc))
    {
        LogRel2(("Shared Clipboard: Read %RU32 bytes host X11 clipboard data\n", cbRead));
        *pcbActual = cbRead;
    }

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error reading host clipboard data from X11, vrc=%Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Writes guest clipboard data through the X11 backend.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   uFormat             Clipboard format to write.
 * @param   pvData              Data buffer.
 * @param   cbData              Data size in bytes.
 */
static int shClBackendX11WriteData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pCtx, uFormat, pvData, cbData);

    LogFlowFuncEnter();

    /* Nothing to do here yet. */

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * @copydoc SHCLCALLBACKS::pfnReportFormats
 *
 * Reports clipboard formats to the guest.
 */
static DECLCALLBACK(int) shClSvcX11ReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFunc(("pCtx=%p, fFormats=%#x\n", pCtx, fFormats));

    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    int const vrc = shClBackendX11ReportLocalFormats(pCtx->pConn, fFormats);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/**
 * Prepares and publishes one exact guest-to-host HTTP-backed URI-list offer.
 *
 * All guest communication and transfer waits happen on the preparation
 * worker.  The X11 event thread only observes the final cache-seeding format
 * report and therefore never waits on the guest.
 *
 * @thread X11 transfer preparation worker.
 */
static int shClSvcX11TransferPrepare(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats, uint64_t uOfferGeneration)
{
    AssertReturn(fFormats & VBOX_SHCL_FMT_URI_LIST, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    /* Preserve the established protocol sequence by consuming the URI-list
     * data reply before creating and initializing the file transfer. */
    void    *pvData = NULL;
    uint32_t cbData = 0;
    int vrc = pCtx->pConn->readDataFromGuest(VBOX_SHCL_FMT_URI_LIST, &pvData, &cbData);
    RTMemFree(pvData);
    if (   RT_SUCCESS(vrc)
        && !shClSvcX11TransferOfferIsCurrent(pCtx, uOfferGeneration))
        vrc = VERR_CANCELLED;

    PSHCLTRANSFER pTransfer = NULL;
    if (RT_SUCCESS(vrc))
    {
        SHCLTRANSFERCALLBACKS Callbacks;
        shClBackendX11TransferGetCallbacks(pCtx, &Callbacks);
        vrc = pCtx->pConn->transferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &Callbacks,
                                          NIL_SHCLTRANSFERID, &pTransfer);
    }

    SHCLTRANSFERID  idTransfer  = NIL_SHCLTRANSFERID;
    SHCLTRANSFERGEN uGeneration = NIL_SHCLTRANSFERGEN;
    if (RT_SUCCESS(vrc))
    {
        idTransfer  = ShClTransferGetID(pTransfer);
        uGeneration = ShClTransferGetGeneration(pTransfer);

        int vrc2 = RTCritSectEnter(&pCtx->CritSect);
        if (RT_SUCCESS(vrc2))
        {
            PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
            if (   pX11TransferState->fPreparing
                && pX11TransferState->uPreparingOfferGeneration == uOfferGeneration)
            {
                pX11TransferState->idTransfer          = idTransfer;
                pX11TransferState->uTransferGeneration = uGeneration;
            }
            vrc2 = RTCritSectLeave(&pCtx->CritSect);
        }
        if (RT_FAILURE(vrc2))
            vrc = vrc2;
    }

    if (RT_SUCCESS(vrc))
        vrc = pCtx->pConn->transferInit(pTransfer);
    if (RT_SUCCESS(vrc))
    {
        /* Wait on this transfer object, never on global HTTP-server state. */
        vrc = ShClTransferWaitForStatus(pTransfer, SHCL_TIMEOUT_DEFAULT_MS,
                                        SHCLTRANSFERSTATUS_INITIALIZED);
    }
    if (   RT_SUCCESS(vrc)
        && !shClSvcX11TransferOfferIsCurrent(pCtx, uOfferGeneration))
        vrc = VERR_CANCELLED;
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferRootListRead(pTransfer);
    if (   RT_SUCCESS(vrc)
        && !ShClTransferRootsCount(pTransfer))
        vrc = VERR_SHCLPB_NO_DATA;
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferHttpServerRegisterTransfer(&pCtx->X11.HttpCtx.HttpServer, pTransfer);

    char  *pszUriList = NULL;
    size_t cbUriList  = 0;
    if (RT_SUCCESS(vrc))
        vrc = ShClTransferHttpConvertToStringList(&pCtx->X11.HttpCtx.HttpServer, pTransfer,
                                                  &pszUriList, &cbUriList);
    if (RT_SUCCESS(vrc))
    {
        if (!pszUriList || !cbUriList)
            vrc = VERR_SHCLPB_NO_DATA;
        else if (cbUriList > UINT32_MAX)
            vrc = VERR_BUFFER_OVERFLOW;
    }

    bool fPublished = false;
    int vrc2 = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(vrc2))
    {
        PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
        bool const fBoundTransfer =    pTransfer
                                    && pX11TransferState->fPreparing
                                    && pX11TransferState->uPreparingOfferGeneration == uOfferGeneration
                                    && shClSvcX11TransferKeyMatches(pX11TransferState->idTransfer,
                                                                   pX11TransferState->uTransferGeneration,
                                                                   pTransfer);
        bool const fCurrentOffer =    !pCtx->fShuttingDown
                                   && pX11TransferState->uOfferGeneration == uOfferGeneration
                                   && (pX11TransferState->fFormats & VBOX_SHCL_FMT_URI_LIST);

        if (   RT_SUCCESS(vrc)
            && fBoundTransfer
            && fCurrentOffer)
        {
            vrc = ShClX11ReportFormatsToX11AsyncEx(&pCtx->X11, pX11TransferState->fFormats,
                                                    VBOX_SHCL_FMT_URI_LIST, pszUriList,
                                                    (uint32_t)cbUriList);
            if (RT_SUCCESS(vrc))
            {
                pX11TransferState->idPublishedTransfer          = idTransfer;
                pX11TransferState->uPublishedTransferGeneration = uGeneration;
                fPublished = true;
            }
        }
        else if (RT_SUCCESS(vrc))
            vrc = VERR_CANCELLED;

        pX11TransferState->fPreparing = false;
        shClSvcX11TransferPreparationResetKey(pX11TransferState);

        vrc2 = RTCritSectLeave(&pCtx->CritSect);
    }
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    RTStrFree(pszUriList);

    if (pTransfer)
    {
        /* pfnTransferCreate returns a retained transfer.  Drop that
         * ownership before a consuming destroy can wait for users. */
        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
    }

    if (   !fPublished
        && ShClTransferIdIsValid(idTransfer))
        pCtx->pConn->transferDestroyById(idTransfer);

    if (fPublished)
        LogRel2(("Shared Clipboard: Advertised cached host X11 URI list for transfer %RU16/%RU64, offer generation %RU64\n",
                 idTransfer, uGeneration, uOfferGeneration));
    else if (vrc != VERR_CANCELLED)
        LogRel(("Shared Clipboard: Preparing host X11 URI list for offer generation %RU64 failed with %Rrc\n",
                uOfferGeneration, vrc));

    return vrc;
}

/**
 * Persistent worker which serializes transfer preparation by clipboard offer
 * generation.  Multiple reports are coalesced and stale results are discarded.
 */
static DECLCALLBACK(int) shClSvcX11TransferPreparationThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    int vrc = RTThreadUserSignal(hThreadSelf);
    AssertRCReturn(vrc, vrc);

    uint64_t uProcessedOfferGeneration = 0;
    for (;;)
    {
        vrc = RTSemEventWait(pCtx->hX11TransferPreparationEvent, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(vrc))
            break;

        for (;;)
        {
            SHCLFORMATS fFormats;
            uint64_t    uOfferGeneration;

            vrc = RTCritSectEnter(&pCtx->CritSect);
            if (RT_FAILURE(vrc))
                break;

            if (pCtx->fShuttingDown)
            {
                RTCritSectLeave(&pCtx->CritSect);
                shClSvcX11TransferPublishedCancel(pCtx);
                return VINF_SUCCESS;
            }

            PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
            uOfferGeneration = pX11TransferState->uOfferGeneration;
            if (uOfferGeneration == uProcessedOfferGeneration)
            {
                RTCritSectLeave(&pCtx->CritSect);
                break;
            }

            uProcessedOfferGeneration = uOfferGeneration;
            fFormats = pX11TransferState->fFormats;
            pX11TransferState->fPreparing = RT_BOOL(fFormats & VBOX_SHCL_FMT_URI_LIST);
            pX11TransferState->uPreparingOfferGeneration = uOfferGeneration;
            shClSvcX11TransferPreparationResetKey(pX11TransferState);

            vrc = RTCritSectLeave(&pCtx->CritSect);
            if (RT_FAILURE(vrc))
                break;

            shClSvcX11TransferPublishedCancel(pCtx);

            if (fFormats & VBOX_SHCL_FMT_URI_LIST)
                shClSvcX11TransferPrepare(pCtx, fFormats, uOfferGeneration);
        }

        if (RT_FAILURE(vrc))
            break;
    }

    shClSvcX11TransferPublishedCancel(pCtx);
    LogRel(("Shared Clipboard: Host X11 transfer preparation worker failed with %Rrc\n", vrc));
    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCreated
 *
 * @thread Shared Clipboard service thread or X11 preparation worker.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    AssertPtrReturnVoid(pCtx->pConn);

    /*
     * Set transfer provider.
     * Those will be registered when a new transfer gets initialized.
     */

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);

    int vrc = VINF_SUCCESS;

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_GUEST_TO_HOST:
        {
            vrc = pCtx->pConn->transferProviderInitGuest(&Provider);
            break;
        }

        case SHCLTRANSFERDIR_HOST_TO_GUEST:
        {
            ShClTransferProviderLocalQueryInterface(&Provider);
            Provider.Interface.pfnRootListRead = shClSvcX11TransferIfaceHGRootListRead;
            Provider.enmSource = SHCLSOURCE_LOCAL;
            Provider.pvUser    = pCtx;
            Provider.cbUser    = sizeof(*pCtx);
            break;
        }

        default:
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);
    }

    if (RT_SUCCESS(vrc))
        vrc = ShClTransferSetProvider(pTransfer, &Provider);
    RT_NOREF(vrc);

    LogFlowFuncLeaveRC(vrc);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize
 *
 * For G->H: Starts the HTTP server if not done yet and registers the transfer with it.
 * For H->G: Called on transfer intialization to populate the transfer's root list.
 *
 * @thread  Shared Clipboard service thread or X11 preparation worker.
 */
static DECLCALLBACK(int) shClSvcX11TransferOnInitCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);
# endif

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    int vrc = VERR_NOT_SUPPORTED; /* Shut up GCC. */

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_GUEST_TO_HOST:
        {
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
            /* We only need to start the HTTP server when we actually receive data from the remote (host). */
            vrc = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
# endif
            break;
        }

        case SHCLTRANSFERDIR_HOST_TO_GUEST:
        {
            vrc = ShClTransferRootListRead(pTransfer); /* Calls shClSvcX11TransferIfaceHGRootListRead(). */
            break;
        }

        default:
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnDestroy
 *
 * This stops the HTTP server if not done yet.
 *
 * @thread Shared Clipboard service thread or X11 preparation worker.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_GUEST_TO_HOST)
        ShClTransferHttpServerMaybeStop(&pCtx->X11.HttpCtx);
# else
    RT_NOREF(pCbCtx);
# endif

    LogFlowFuncLeave();
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCompleted
 *
 * Reports successful host-side HTTP completion to the service and Main.
 * Non-successful terminal states remain the responsibility of their lifecycle
 * owner.
 *
 * @thread HTTP server request thread.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnCompletedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcCompletion)
{
    if (RT_FAILURE(rcCompletion))
        return;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pCtx->pConn);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtrReturnVoid(pTransfer);

    if (   ShClTransferGetDir(pTransfer) != SHCLTRANSFERDIR_GUEST_TO_HOST
        || ShClTransferGetStatus(pTransfer) != SHCLTRANSFERSTATUS_COMPLETED)
        return;

    int const vrc = pCtx->pConn->transferReportStatus(pTransfer, SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Reporting completed X11 HTTP transfer failed, rc=%Rrc\n", vrc));
}

/**
 * Unregisters a transfer from a HTTP server.
 *
 * This also stops the HTTP server if no active transfers are found anymore.
 *
 * @param   pCtx                Shared clipboard context to unregister transfer for.
 * @param   pTransfer           Transfer to unregister.
 *
 * @thread Shared Clipboard service thread or X11 preparation worker.
 */
static void shClSvcX11HttpTransferUnregister(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer)
{
    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_GUEST_TO_HOST)
    {
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
        if (ShClTransferHttpServerIsInitialized(&pCtx->X11.HttpCtx.HttpServer))
        {
            ShClTransferHttpServerUnregisterTransfer(&pCtx->X11.HttpCtx.HttpServer, pTransfer);
            ShClTransferHttpServerMaybeStop(&pCtx->X11.HttpCtx);
        }
# else
        RT_NOREF(pCtx);
# endif
    }

    //ShClTransferRelease(pTransfer);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnUnregistered
 *
 * Unregisters a (now) unregistered transfer from the HTTP server.
 *
 * @thread Shared Clipboard service thread or X11 preparation worker.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnUnregisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx)
{
    RT_NOREF(pTransferCtx);

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    int vrc = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(vrc))
    {
        PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
        if (shClSvcX11TransferKeyMatches(pX11TransferState->idTransfer,
                                         pX11TransferState->uTransferGeneration, pCbCtx->pTransfer))
            shClSvcX11TransferPreparationResetKey(pX11TransferState);
        if (shClSvcX11TransferKeyMatches(pX11TransferState->idPublishedTransfer,
                                         pX11TransferState->uPublishedTransferGeneration, pCbCtx->pTransfer))
            shClSvcX11TransferPublishedResetKey(pX11TransferState);

        vrc = RTCritSectLeave(&pCtx->CritSect);
        AssertRC(vrc);
    }
# endif
    shClSvcX11HttpTransferUnregister(pCtx, pCbCtx->pTransfer);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * @copydoc SHCLCALLBACKS::pfnOnRequestDataFromSource
 *
 * Requests clipboard data from the guest.
 *
 * @thread  Called from X11 event thread.
 */
static DECLCALLBACK(int) shClSvcX11RequestDataFromSourceCallback(PSHCLCONTEXT pCtx,
                                                                 SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFunc(("pCtx=%p, uFmt=0x%x\n", pCtx, uFmt));

    *ppv = NULL;
    *pcb = 0;

    if (pCtx->fShuttingDown)
    {
        /* The shared clipboard is disconnecting. */
        LogRel(("Shared Clipboard: Host requested guest clipboard data after guest had disconnected\n"));
        return VERR_WRONG_ORDER;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
    {
        /* URI targets are advertised cache-only after the worker prepared the
         * exact transfer.  Never fall back to guest I/O on the X11 thread. */
        LogRelMax(16, ("Shared Clipboard: Host X11 URI-list conversion missed its prepared URI-list data cache\n"));
        return VERR_SHCLPB_NO_DATA;
    }
#elif defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS)
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
        return VERR_NOT_SUPPORTED;
#endif

    int const vrc = pCtx->pConn->readDataFromGuest(uFmt, ppv, pcb);

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Requesting X11 data in format %#x from guest failed with %Rrc\n", uFmt, vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles transfer status replies from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   pTransfer           Transfer whose status changed.
 * @param   enmSource           Endpoint which supplied the reply.
 * @param   enmStatus           New transfer status.
 * @param   rcStatus            Status-specific VBox status code.
 */
static int shClBackendX11TransferHandleStatusReply(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                         SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    RT_NOREF(enmSource, rcStatus);

    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_GUEST_TO_HOST)
    {
        switch (enmStatus)
        {
            case SHCLTRANSFERSTATUS_INITIALIZED:
            {
#  ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                int vrc2 = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
                if (RT_SUCCESS(vrc2))
                {

                }

                if (RT_FAILURE(vrc2))
                    LogRel(("Shared Clipboard: Registering HTTP transfer failed: %Rrc\n", vrc2));
#  endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */
                break;
            }

            default:
                break;
        }
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Provider interface implementation                                                                                            *
*********************************************************************************************************************************/

/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) shClSvcX11TransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pBackendCtx = (PSHCLCONTEXT)pCtx->pvUser;
    AssertPtrReturn(pBackendCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pBackendCtx->pConn, VERR_INVALID_POINTER);
    PSHCLX11CTX pX11 = &pBackendCtx->X11;

    /* X supplies the data asynchronously, so we need to wait for data to arrive first. */
    void    *pvData;
    uint32_t cbData;
    int vrc = ShClX11ReadDataFromX11Ex(pX11, &pBackendCtx->EventSrc, SHCL_TIMEOUT_DEFAULT_MS, VBOX_SHCL_FMT_URI_LIST,
                                       &pvData, &cbData);
    if (RT_SUCCESS(vrc))
    {
        vrc = ShClTransferRootsSetFromStringList(pCtx->pTransfer, (const char *)pvData, cbData);
        if (RT_SUCCESS(vrc))
            LogRelMax2(16, ("Shared Clipboard: Host reported %RU64 X11 root entries for transfer to guest\n",
                            ShClTransferRootsCount(pCtx->pTransfer)));
        else
            LogRelMax(16, ("Shared Clipboard: Converting X11 URI-list clipboard data (%RU32 bytes) to transfer roots failed with %Rrc\n",
                            cbData, vrc));

        RTMemFree(pvData);
    }
    else
        LogRelMax(16, ("Shared Clipboard: Reading X11 URI-list clipboard data for transfer failed with %Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/** Native X11 Shared Clipboard backend operations. */
static SHCLBACKENDOPS const s_ShClBackendX11Ops =
{
    shClBackendX11Init,
    shClBackendX11Destroy,
    shClBackendX11SetCallbacks,
    shClBackendX11Connect,
    shClBackendX11Disconnect,
    shClBackendX11ReportFormats,
    shClBackendX11ReadData,
    shClBackendX11WriteData,
    shClBackendX11Sync,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClBackendX11TransferGetCallbacks,
    shClBackendX11TransferHandleStatusReply,
#endif
};


/**
 * Returns the native X11 Shared Clipboard backend operations.
 *
 * @returns Immutable X11 backend operation table.
 */
PCSHCLBACKENDOPS ShClBackendGetOps(void)
{
    return &s_ShClBackendX11Ops;
}
