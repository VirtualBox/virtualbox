/* $Id: clipboard-x11.cpp 115057 2026-08-17 16:48:01Z andreas.loeffler@oracle.com $ */
/** @file
 * Guest Additions - X11 Shared Clipboard implementation.
 */

/*
 * Copyright (C) 2007-2026 Oracle and/or its affiliates.
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
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
# include <VBox/GuestHost/clipboard-transfers-http.h>
#endif

#include "VBoxClient.h"
#include "clipboard.h"

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/log.h>


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
static void vbclX11TransferUnregister(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer);

/** Resets the transfer key bound to the current asynchronous preparation. */
static void vbclX11TransferStateResetKey(PSHCLX11TRANSFERSTATE pX11TransferState)
{
    pX11TransferState->idTransfer          = NIL_SHCLTRANSFERID;
    pX11TransferState->uTransferGeneration = NIL_SHCLTRANSFERGEN;
}

/** Resets the key of the transfer backing the advertised URI-list data. */
static void vbclX11TransferPublishedResetKey(PSHCLX11TRANSFERSTATE pX11TransferState)
{
    pX11TransferState->idPublishedTransfer          = NIL_SHCLTRANSFERID;
    pX11TransferState->uPublishedTransferGeneration = NIL_SHCLTRANSFERGEN;
}

/**
 * Checks whether a transfer is the exact transfer bound to the current
 * asynchronous preparation.
 */
static bool vbclX11TransferStateMatches(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    return    pX11TransferState->fPreparing
           && pX11TransferState->idTransfer          != NIL_SHCLTRANSFERID
           && pX11TransferState->uTransferGeneration != NIL_SHCLTRANSFERGEN
           && pX11TransferState->idTransfer          == ShClTransferGetID(pTransfer)
           && pX11TransferState->uTransferGeneration == ShClTransferGetGeneration(pTransfer);
}

/** Checks whether a transfer backs the currently advertised URI-list data. */
static bool vbclX11TransferPublishedMatches(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    return    pX11TransferState->idPublishedTransfer          != NIL_SHCLTRANSFERID
           && pX11TransferState->uPublishedTransferGeneration != NIL_SHCLTRANSFERGEN
           && pX11TransferState->idPublishedTransfer          == ShClTransferGetID(pTransfer)
           && pX11TransferState->uPublishedTransferGeneration == ShClTransferGetGeneration(pTransfer);
}

/** Cancels the transfer backing URI-list data superseded by a new clipboard offer. */
static void vbclX11TransferPublishedCancel(PSHCLCONTEXT pCtx)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    if (pX11TransferState->idPublishedTransfer == NIL_SHCLTRANSFERID)
        return;

    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(&pCtx->TransferCtx,
                                                             pX11TransferState->idPublishedTransfer);
    if (   pTransfer
        && vbclX11TransferPublishedMatches(pCtx, pTransfer))
    {
        vbclX11TransferUnregister(pCtx, pTransfer);
        if (ShClTransferGetStatus(pTransfer) == SHCLTRANSFERSTATUS_INITIALIZED)
        {
            int rc = VbglR3ClipboardTransferSendStatus(&pCtx->CmdCtx, pTransfer,
                                                       SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Canceling superseded transfer %RU16/%RU64 failed with %Rrc\n",
                        ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer), rc));
        }
    }
    else
        LogRel2(("Shared Clipboard: Published transfer %RU16/%RU64 was already gone or replaced\n",
                 pX11TransferState->idPublishedTransfer, pX11TransferState->uPublishedTransferGeneration));

    vbclX11TransferPublishedResetKey(pX11TransferState);
}

/**
 * Starts preparing HTTP-backed URI-list data for the current host clipboard offer.
 *
 * The request is issued by the clipboard service worker.  Its transfer is
 * created and initialized later while that worker processes transfer-status
 * messages, so the X11 event thread never waits for this operation.
 */
static int vbclX11TransferStateStart(PSHCLCONTEXT pCtx)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    AssertReturn(!pX11TransferState->fPreparing, VERR_WRONG_ORDER);
    AssertReturn(pX11TransferState->fFormats & VBOX_SHCL_FMT_URI_LIST, VERR_INVALID_PARAMETER);

    pX11TransferState->fPreparing                = true;
    pX11TransferState->uPreparingOfferGeneration = pX11TransferState->uOfferGeneration;
    vbclX11TransferStateResetKey(pX11TransferState);

    /* Preserve the existing protocol sequence: consume the URI-list clipboard
     * data response before requesting the HTTP transfer.  The actual X11 data
     * will be the URI list of HTTP URLs produced after transfer initialization. */
    void    *pvData = NULL;
    uint32_t cbData = 0;
    int rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, VBOX_SHCL_FMT_URI_LIST, &pvData, &cbData);
    RTMemFree(pvData);
    if (RT_SUCCESS(rc))
        rc = VbglR3ClipboardTransferRequest(&pCtx->CmdCtx);

    if (RT_FAILURE(rc))
    {
        pX11TransferState->fPreparing = false;
        vbclX11TransferStateResetKey(pX11TransferState);
        LogRel(("Shared Clipboard: Starting asynchronous X11 transfer preparation failed with %Rrc\n", rc));
    }
    else
        LogRel2(("Shared Clipboard: Preparing X11 URI list for clipboard offer generation %RU64\n",
                 pX11TransferState->uPreparingOfferGeneration));

    return rc;
}

/**
 * Completes asynchronous preparation for an exact transfer ID and generation.
 *
 * A result which no longer matches the current clipboard offer is discarded.
 * If the host clipboard changed while a transfer was being initialized, a new
 * serialized request is started for the latest offer after the old transfer
 * has been canceled.
 *
 * @returns VBox status code for preparing and publishing the URI list.
 */
static int vbclX11TransferStateComplete(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer,
                                        const char *pszUriList, size_t cbUriList, int rcPreparation)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    if (!vbclX11TransferStateMatches(pCtx, pTransfer))
    {
        LogRel2(("Shared Clipboard: Ignoring URI-list preparation completion for unbound transfer %RU16/%RU64\n",
                 ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer)));
        return VINF_SUCCESS;
    }

    uint64_t const uPreparingOfferGeneration = pX11TransferState->uPreparingOfferGeneration;
    bool const fCurrentOffer =    uPreparingOfferGeneration == pX11TransferState->uOfferGeneration
                               && (pX11TransferState->fFormats & VBOX_SHCL_FMT_URI_LIST);

    pX11TransferState->fPreparing = false;
    vbclX11TransferStateResetKey(pX11TransferState);

    bool fPublished = false;
    if (RT_SUCCESS(rcPreparation) && fCurrentOffer)
    {
        if (!pszUriList || !cbUriList)
            rcPreparation = VERR_SHCLPB_NO_DATA;
        else if (cbUriList > UINT32_MAX)
            rcPreparation = VERR_BUFFER_OVERFLOW;
        else
        {
            rcPreparation = ShClX11ReportFormatsToX11AsyncEx(&pCtx->X11, pX11TransferState->fFormats,
                                                              VBOX_SHCL_FMT_URI_LIST, pszUriList,
                                                              (uint32_t)cbUriList);
            if (RT_SUCCESS(rcPreparation))
            {
                fPublished = true;
                pX11TransferState->idPublishedTransfer          = ShClTransferGetID(pTransfer);
                pX11TransferState->uPublishedTransferGeneration = ShClTransferGetGeneration(pTransfer);
                LogRel2(("Shared Clipboard: Advertised cached X11 URI list for transfer %RU16/%RU64, "
                         "clipboard offer generation %RU64\n", ShClTransferGetID(pTransfer),
                         ShClTransferGetGeneration(pTransfer), uPreparingOfferGeneration));
            }
        }
    }

    if (!fPublished)
    {
        if (RT_FAILURE(rcPreparation))
            LogRel(("Shared Clipboard: Preparing X11 URI list for transfer %RU16/%RU64 failed with %Rrc\n",
                    ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer), rcPreparation));
        else
            LogRel2(("Shared Clipboard: Discarding stale X11 URI list for transfer %RU16/%RU64\n",
                     ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer)));

        vbclX11TransferUnregister(pCtx, pTransfer);
        if (   RT_SUCCESS(rcPreparation)
            && ShClTransferGetStatus(pTransfer) == SHCLTRANSFERSTATUS_INITIALIZED)
        {
            int rc2 = VbglR3ClipboardTransferSendStatus(&pCtx->CmdCtx, pTransfer,
                                                        SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
            if (RT_FAILURE(rc2))
                LogRel(("Shared Clipboard: Canceling unused transfer %RU16/%RU64 failed with %Rrc\n",
                        ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer), rc2));
        }
    }

    if (   uPreparingOfferGeneration != pX11TransferState->uOfferGeneration
        && (pX11TransferState->fFormats & VBOX_SHCL_FMT_URI_LIST))
    {
        int rc2 = vbclX11TransferStateStart(pCtx);
        if (RT_FAILURE(rc2))
            LogRel(("Shared Clipboard: Restarting X11 transfer preparation failed with %Rrc\n", rc2));
    }

    return rcPreparation;
}

/**
 * Handles a new host clipboard offer without exposing an unprepared URI target.
 */
static int vbclX11ReportHostFormats(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
    vbclX11TransferPublishedCancel(pCtx);
    pX11TransferState->fFormats = fFormats;
    pX11TransferState->uOfferGeneration++;
    if (!pX11TransferState->uOfferGeneration)
        pX11TransferState->uOfferGeneration = 1;

    /* Non-transfer formats can be advertised immediately.  The URI-list bit is
     * added by the completion callback together with its pre-seeded cache. */
    int rc = ShClX11ReportFormatsToX11Async(&pCtx->X11, fFormats & ~VBOX_SHCL_FMT_URI_LIST);
    if (   (fFormats & VBOX_SHCL_FMT_URI_LIST)
        && !pX11TransferState->fPreparing)
    {
        int rc2 = vbclX11TransferStateStart(pCtx);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(int) vbclX11OnTransferInitializeCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    int rc = VINF_SUCCESS;

    /* If this is a G->H transfer, we need to set the root list entries here, as the host
     * will start reading those as soon as we report the INITIALIZED status. */
    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_TO_REMOTE: /* G->H */
        {
            void    *pvData;
            uint32_t cbData;
            rc = ShClX11ReadDataFromX11Ex(&g_Ctx.X11, &pCtx->EventSrc, SHCL_TIMEOUT_DEFAULT_MS, VBOX_SHCL_FMT_URI_LIST,
                                          &pvData, &cbData);
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferRootsSetFromStringList(pTransfer, (const char *)pvData, cbData);
                RTMemFree(pvData);
            }
            break;
        }

        case SHCLTRANSFERDIR_FROM_REMOTE: /* H->G */
            break;

        default:
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialized
 *
 * Reads the root list, then builds and publishes URI-list data only for the
 * transfer ID and generation captured when the current asynchronous request
 * was registered.
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(int) vbclX11OnTransferInitializedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE)
    {
        if (!vbclX11TransferStateMatches(pCtx, pTransfer))
        {
            LogRelMax(16, ("Shared Clipboard: Rejecting unbound initialized transfer %RU16/%RU64\n",
                     ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer)));
            int rc2 = VbglR3ClipboardTransferSendStatus(&pCtx->CmdCtx, pTransfer,
                                                        SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
            if (RT_FAILURE(rc2))
                LogRel(("Shared Clipboard: Canceling unbound transfer %RU16/%RU64 failed with %Rrc\n",
                        ShClTransferGetID(pTransfer), ShClTransferGetGeneration(pTransfer), rc2));
            return VINF_SUCCESS;
        }

        /* The remote provider rejects root-list reads until ShClTransferInit()
         * has changed the transfer state to INITIALIZED.  Registering the HTTP
         * transfer from pfnOnInitialize therefore races ahead of that state
         * transition and leaves URI-list conversion waiting forever. */
        rc = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
        if (RT_SUCCESS(rc))
            rc = ShClTransferRootListRead(pTransfer);
        if (RT_SUCCESS(rc))
        {
            if (ShClTransferRootsCount(pTransfer))
                rc = ShClTransferHttpServerRegisterTransfer(&pCtx->X11.HttpCtx.HttpServer, pTransfer);
            else
                rc = VERR_SHCLPB_NO_DATA;
        }

        char  *pszUriList = NULL;
        size_t cbUriList  = 0;
        if (RT_SUCCESS(rc))
            rc = ShClTransferHttpConvertToStringList(&pCtx->X11.HttpCtx.HttpServer, pTransfer,
                                                     &pszUriList, &cbUriList);

        rc = vbclX11TransferStateComplete(pCtx, pTransfer, pszUriList, cbUriList, rc);
        RTStrFree(pszUriList);
    }

    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnRegistered
 *
 * This binds pending transfer preparation to the newly registered transfer's
 * exact ID and generation.  The HTTP server is started and the transfer is
 * added after its roots have been read by the initialized callback.
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(void) vbclX11OnTransferRegisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx)
{
    RT_NOREF(pTransferCtx);

    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    /* We only need to start the HTTP server when we actually receive data from the remote (host). */
    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE) /* H->G */
    {
        PSHCLX11TRANSFERSTATE pX11TransferState = &pCtx->X11TransferState;
        /* H->G requests are serialized by fPreparing.  The first H->G
         * registration is therefore the protocol response to the outstanding
         * request.  Capture its immutable key once; later registration
         * callbacks must not redirect this preparation to another transfer. */
        if (   pX11TransferState->fPreparing
            && pX11TransferState->idTransfer == NIL_SHCLTRANSFERID)
        {
            pX11TransferState->idTransfer          = ShClTransferGetID(pTransfer);
            pX11TransferState->uTransferGeneration = ShClTransferGetGeneration(pTransfer);
            LogRel2(("Shared Clipboard: Bound X11 transfer preparation to transfer %RU16/%RU64\n",
                     pX11TransferState->idTransfer, pX11TransferState->uTransferGeneration));
        }
        else if (pX11TransferState->fPreparing)
            LogRel2(("Shared Clipboard: Keeping X11 transfer preparation bound to transfer %RU16/%RU64; "
                     "ignoring registration of %RU16/%RU64\n", pX11TransferState->idTransfer,
                     pX11TransferState->uTransferGeneration, ShClTransferGetID(pTransfer),
                     ShClTransferGetGeneration(pTransfer)));

    }

    LogFlowFuncLeave();
}

/**
 * Unregisters a transfer from a HTTP server.
 *
 * This also stops the HTTP server if no active transfers are found anymore.
 *
 * @param   pCtx                Shared clipboard context to unregister transfer for.
 * @param   pTransfer           Transfer to unregister.
 *
 * @thread Clipboard main thread.
 */
static void vbclX11TransferUnregister(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer)
{
    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE)
    {
        if (ShClTransferHttpServerIsInitialized(&pCtx->X11.HttpCtx.HttpServer))
        {
            ShClTransferHttpServerUnregisterTransfer(&pCtx->X11.HttpCtx.HttpServer, pTransfer);
            ShClTransferHttpServerMaybeStop(&pCtx->X11.HttpCtx);
        }
    }
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnUnregistered
 *
 * Unregisters a (now) unregistered transfer from the HTTP server.
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(void) vbclX11OnTransferUnregisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx)
{
    RT_NOREF(pTransferCtx);

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    if (vbclX11TransferStateMatches(pCtx, pCbCtx->pTransfer))
        vbclX11TransferStateComplete(pCtx, pCbCtx->pTransfer, NULL, 0, VERR_CANCELLED);
    if (vbclX11TransferPublishedMatches(pCtx, pCbCtx->pTransfer))
        vbclX11TransferPublishedResetKey(&pCtx->X11TransferState);
    vbclX11TransferUnregister(pCtx, pCbCtx->pTransfer);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCompleted
 *
 * Unregisters a complete transfer from the HTTP server.
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(void) vbclX11OnTransferCompletedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rc)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    if (vbclX11TransferStateMatches(pCtx, pCbCtx->pTransfer))
        vbclX11TransferStateComplete(pCtx, pCbCtx->pTransfer, NULL, 0,
                                           RT_SUCCESS(rc) ? VERR_SHCLPB_NO_DATA : rc);
    if (vbclX11TransferPublishedMatches(pCtx, pCbCtx->pTransfer))
        vbclX11TransferPublishedResetKey(&pCtx->X11TransferState);
    vbclX11TransferUnregister(pCtx, pCbCtx->pTransfer);
}

/** @copydoc SHCLTRANSFERCALLBACKS::pfnOnError
 *
 * Unregisters a failed transfer from the HTTP server.
 *
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(void) vbclX11OnTransferErrorCallback(PSHCLTRANSFERCALLBACKCTX pCtx, int rc)
{
    return vbclX11OnTransferCompletedCallback(pCtx, rc);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

/**
 * Worker for a reading clipboard from the host.
 */
static DECLCALLBACK(int) vbclX11ReadDataWorker(PSHCLCONTEXT pCtx,
                                               SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData, void *pvUser)
{
    RT_NOREF(pvUser);

    return VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uFmt, ppvData, pcbData);
}

/**
 * @copydoc SHCLCALLBACKS::pfnOnRequestDataFromSource
 *
 * Requests data from the host.
 *
 * For transfers: This requests a transfer from the host. Most of the handling will be done VbglR3 then.
 *
 * @thread  X11 event thread.
 */
static DECLCALLBACK(int) vbclX11OnRequestDataFromSourceCallback(PSHCLCONTEXT pCtx,
                                                                SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFunc(("pCtx=%p, uFmt=%#x\n", pCtx, uFmt));

    *ppv = NULL;
    *pcb = 0;

#if defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS) && !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP)
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
        return VERR_NOT_SUPPORTED;
#endif

    int rc;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
    {
        /* URI targets are marked cache-only when advertised, so the common
         * X11 code normally handles a miss without invoking this callback.
         * Refuse it here as well: never start or wait for a transfer from the
         * X11 event thread. */
        LogRelMax(16, ("Shared Clipboard: X11 URI-list conversion missed its prepared URI-list data cache\n"));
        rc = VERR_SHCLPB_NO_DATA;
    }
    else /* Anything else */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */
    {
        rc = vbclX11ReadDataWorker(pCtx, uFmt, ppv, pcb, pvUser);
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Requesting data in format %#x from host failed with %Rrc\n", uFmt, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLCALLBACKS::pfnReportFormats
 *
 * Reports clipboard formats to the host.
 *
 * @thread  X11 event thread.
 */
static DECLCALLBACK(int) vbclX11ReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFunc(("fFormats=%#x\n", fFormats));

#if defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS) && !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP)
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        LogRelMax(16, ("Shared Clipboard: X11 guest requires HTTP transfer support for URI-list offers, masking format\n"));
        fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
    }
#endif

    int rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fFormats);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes the X11-specific Shared Clipboard code.
 *
 * @returns VBox status code.
 */
int VBClX11ClipboardInit(void)
{
    LogFlowFuncEnter();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    RT_ZERO(g_Ctx.X11TransferState);
    vbclX11TransferStateResetKey(&g_Ctx.X11TransferState);
    vbclX11TransferPublishedResetKey(&g_Ctx.X11TransferState);
#endif

    int rc = ShClEventSourceInit(&g_Ctx.EventSrc, 0 /* uID */);
    AssertRCReturn(rc, rc);

    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats           = vbclX11ReportFormatsCallback;
    Callbacks.pfnOnRequestDataFromSource = vbclX11OnRequestDataFromSourceCallback;

    rc = ShClX11Init(&g_Ctx.X11, &Callbacks, &g_Ctx);
    if (RT_SUCCESS(rc))
    {
        rc = ShClX11ThreadStart(&g_Ctx.X11, false /* grab */);
        if (RT_SUCCESS(rc))
        {
            rc = VbglR3ClipboardConnectEx(&g_Ctx.CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
            if (RT_FAILURE(rc))
                ShClX11ThreadStop(&g_Ctx.X11);
        }
    }
    else
        VBClLogError("Initializing clipboard failed with %Rrc\n", rc);

    if (RT_FAILURE(rc))
    {
        VbglR3ClipboardDisconnectEx(&g_Ctx.CmdCtx);
        ShClX11Term(&g_Ctx.X11);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys the X11-specific Shared Clipboard code.
 *
 * @returns VBox status code.
 */
int VBClX11ClipboardDestroy(void)
{
    return ShClEventSourceTerm(&g_Ctx.EventSrc);
}

/**
 * The main loop of the X11-specific Shared Clipboard code.
 *
 * @returns VBox status code.
 *
 * @thread  Clipboard service worker thread.
 */
int VBClX11ClipboardMain(void)
{
    PSHCLCONTEXT pCtx = &g_Ctx;

    bool fShutdown = false;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    /*
     * Set callbacks.
     * Those will be registered within VbglR3 when a new transfer gets initialized.
     *
     * Used for starting / stopping the HTTP server.
     */
    RT_ZERO(pCtx->CmdCtx.Transfers.Callbacks);

    pCtx->CmdCtx.Transfers.Callbacks.pvUser = pCtx; /* Assign context as user-provided callback data. */
    pCtx->CmdCtx.Transfers.Callbacks.cbUser = sizeof(SHCLCONTEXT);

    pCtx->CmdCtx.Transfers.Callbacks.pfnOnInitialize   = vbclX11OnTransferInitializeCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnInitialized  = vbclX11OnTransferInitializedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnRegistered   = vbclX11OnTransferRegisteredCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnUnregistered = vbclX11OnTransferUnregisteredCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnCompleted    = vbclX11OnTransferCompletedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnError        = vbclX11OnTransferErrorCallback;
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    LogFlowFunc(("fUseLegacyProtocol=%RTbool, fHostFeatures=%#RX64 ...\n",
                 pCtx->CmdCtx.fUseLegacyProtocol, pCtx->CmdCtx.fHostFeatures));

    int rc;

    /* The thread waits for incoming messages from the host. */
    for (;;)
    {
        PVBGLR3CLIPBOARDEVENT pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
        AssertPtrBreakStmt(pEvent, rc = VERR_NO_MEMORY);

        uint32_t idMsg  = 0;
        uint32_t cParms = 0;
        rc = VbglR3ClipboardMsgPeekWait(&pCtx->CmdCtx, &idMsg, &cParms, NULL /* pidRestoreCheck */);
        if (RT_SUCCESS(rc))
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            rc = VbglR3ClipboardEventGetNextEx(idMsg, cParms, &pCtx->CmdCtx, &pCtx->TransferCtx, pEvent);
#else
            rc = VbglR3ClipboardEventGetNext(idMsg, cParms, &pCtx->CmdCtx, pEvent);
#endif
        }

        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("Getting next event failed with %Rrc\n", rc));

            VbglR3ClipboardEventFree(pEvent);
            pEvent = NULL;

            if (fShutdown)
                break;

            /* Wait a bit before retrying. */
            RTThreadSleep(RT_MS_1SEC);
            continue;
        }
        else
        {
            AssertPtr(pEvent);
            LogFlowFunc(("Event uType=%RU32\n", pEvent->enmType));

            switch (pEvent->enmType)
            {
                case VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS:
                {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                    rc = vbclX11ReportHostFormats(pCtx, pEvent->u.fReportedFormats);
#else
                    rc = ShClX11ReportFormatsToX11Async(&g_Ctx.X11, pEvent->u.fReportedFormats);
#endif
                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
                {
                    void    *pvData;
                    uint32_t cbData;
                    rc = ShClX11ReadDataFromX11Ex(&g_Ctx.X11, &pCtx->EventSrc, SHCL_TIMEOUT_DEFAULT_MS, pEvent->u.fReadData,
                                                  &pvData, &cbData);
                    if (RT_SUCCESS(rc))
                    {
                        rc = VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, pEvent->u.fReadData, pvData, cbData);
                        RTMemFree(pvData);
                    }

                    if (RT_FAILURE(rc))
                        VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, pEvent->u.fReadData, NULL, 0);

                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_QUIT:
                {
                    VBClLogVerbose(2, "Host requested termination\n");
                    fShutdown = true;
                    break;
                }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                case VBGLR3CLIPBOARDEVENTTYPE_TRANSFER_STATUS:
                {
                    if (pEvent->u.TransferStatus.Report.uStatus == SHCLTRANSFERSTATUS_STARTED)
                    {

                    }
                    rc = VINF_SUCCESS;
                    break;
                }
#endif
                case VBGLR3CLIPBOARDEVENTTYPE_NONE:
                {
                    /* Nothing to do here. */
                    rc = VINF_SUCCESS;
                    break;
                }

                default:
                {
                    AssertMsgFailedBreakStmt(("Event type %RU32 not implemented\n", pEvent->enmType), rc = VERR_NOT_SUPPORTED);
                }
            }

            if (pEvent)
            {
                VbglR3ClipboardEventFree(pEvent);
                pEvent = NULL;
            }
        }

        if (fShutdown)
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
