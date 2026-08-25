/* $Id: VBoxSharedClipboardSvc-transport.cpp 115108 2026-08-25 07:19:33Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Opaque Main transport implementation.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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
#include <VBox/log.h>

#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include "VBoxSharedClipboardSvc-internal.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif


/**
 * Reports native clipboard formats to a guest through the Shared Clipboard HGCM queue.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque clipboard client to report to.
 * @param   fFormats            Host formats, VBOX_SHCL_FMT_XXX.
 * @param   pfReported          Where to return the filtered formats. Optional.
 */
static DECLCALLBACK(int) shClSvcOpReportFormatsToGuest(SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                              SHCLFORMATS *pfReported)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    fFormats = shClSvcHandleFormats(true /* fHostToGuest */, pClient, fFormats);
    if (pfReported)
        *pfReported = fFormats;

    uint32_t const uMode = ShClSvcClientGetMode(pClient);
    if (   uMode != VBOX_SHCL_MODE_BIDIRECTIONAL
        && uMode != VBOX_SHCL_MODE_HOST_TO_GUEST)
        return VINF_NO_CHANGE;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_FORMATS_REPORT, 2);
    if (!pMsg)
        return VERR_NO_MEMORY;

    HGCMSvcSetU32(&pMsg->aParms[0], VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    HGCMSvcSetU32(&pMsg->aParms[1], fFormats);

    ShClSvcClientLock(pClient);
    int const vrc = shClSvcClientMsgAddAndWakeupClient(pClient, pMsg);
    ShClSvcClientUnlock(pClient);
    return vrc;
}


/**
 * Reads clipboard data from the guest, asynchronous version.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque client to request data from.
 * @param   fFormats            The formats being requested, OR'ed together (VBOX_SHCL_FMT_XXX).
 * @param   ppEvent             Where to return the event for waiting for new data on success.
 *                              Must be released by the caller with ShClEventRelease(). Optional.
 *
 * @thread  On X11: Called from the X11 event thread.
 * @thread  On Windows: Called from the Windows event thread.
 *
 * @note    This will locally initialize a transfer if VBOX_SHCL_FMT_URI_LIST is being requested from the guest.
 */
static DECLCALLBACK(int) shClSvcOpReadDataFromGuestAsync(SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                                PSHCLEVENT *ppEvent)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    LogFlowFunc(("fFormats=%#x\n", fFormats));

    if (ppEvent)
        *ppEvent = NULL;

    SHCLFORMATS const fSupportedFormats = VBOX_SHCL_FMT_UNICODETEXT
                                        | VBOX_SHCL_FMT_BITMAP
                                        | VBOX_SHCL_FMT_HTML
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                        | VBOX_SHCL_FMT_URI_LIST
#endif
                                        ;
    if (   fFormats == VBOX_SHCL_FMT_NONE
        || (fFormats & ~fSupportedFormats))
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting unsupported guest clipboard data request formats %#x\n", fFormats));
        return VERR_NOT_SUPPORTED;
    }
    if (   ppEvent
        && (fFormats & (fFormats - 1)) != 0)
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting multi-format guest clipboard data request %#x with single event output\n",
                        fFormats));
        return VERR_INVALID_PARAMETER;
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (   (fFormats & VBOX_SHCL_FMT_URI_LIST)
        && !shClSvcClientTransfersAreAllowed(pClient))
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting host URI-list request without enabled and negotiated transfers\n"));
        return VERR_ACCESS_DENIED;
    }
#endif

    int vrc = VERR_NOT_SUPPORTED;

    /* Generate a separate message for every (valid) format we support. */
    while (fFormats)
    {
        /* Pick the next format to get from the mask: */
        /** @todo Make format reporting precedence configurable? */
        SHCLFORMAT fFormat;
        if (fFormats & VBOX_SHCL_FMT_UNICODETEXT)
            fFormat = VBOX_SHCL_FMT_UNICODETEXT;
        else if (fFormats & VBOX_SHCL_FMT_BITMAP)
            fFormat = VBOX_SHCL_FMT_BITMAP;
        else if (fFormats & VBOX_SHCL_FMT_HTML)
            fFormat = VBOX_SHCL_FMT_HTML;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        else if (fFormats & VBOX_SHCL_FMT_URI_LIST)
            fFormat = VBOX_SHCL_FMT_URI_LIST;
#endif
        else
        {
            vrc = VERR_NOT_SUPPORTED;
            break;
        }

        /* Remove it from the mask. */
        fFormats &= ~fFormat;

        if (LogRelIs2Enabled())
        {
            char *pszFmt = ShClFormatsToStrA(fFormat);
            LogRel2(("Shared Clipboard: Requesting guest clipboard data in format %#x/'%s'\n",
                     fFormat, pszFmt ? pszFmt : "<alloc failed>"));
            RTStrFree(pszFmt);
        }
        /*
         * Allocate messages, one for each format.
         */
        uint64_t const fGuestFeatures0 = ShClSvcClientGetGuestFeatures0(pClient);
        PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient,
                                                      fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID
                                                    ? VBOX_SHCL_HOST_MSG_READ_DATA_CID : VBOX_SHCL_HOST_MSG_READ_DATA,
                                                    2);
        if (pMsg)
        {
            ShClSvcClientLock(pClient);

            PSHCLEVENT pEvent;
            vrc = ShClEventSourceGenerateAndRegisterEvent(&pClient->EventSrc, &pEvent);
            if (RT_SUCCESS(vrc))
            {
                LogFlowFunc(("fFormats=%#x -> fFormat=%#x, idEvent=%#x\n", fFormats, fFormat, pEvent->idEvent));
                pEvent->uUser = fFormat;

                const uint64_t uCID = VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, pClient->EventSrc.uID, pEvent->idEvent);

                vrc = VINF_SUCCESS;

                /* Save the context ID in our legacy cruft if we have to deal with old(er) Guest Additions (< 6.1). */
                if (!(fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID))
                {
                    AssertStmt(pClient->Legacy.cCID < 4096, vrc = VERR_TOO_MUCH_DATA);
                    if (RT_SUCCESS(vrc))
                    {
                        PSHCLCLIENTLEGACYCID pCID = (PSHCLCLIENTLEGACYCID)RTMemAlloc(sizeof(SHCLCLIENTLEGACYCID));
                        if (pCID)
                        {
                            pCID->uCID    = uCID;
                            pCID->enmType = 0; /* Not used yet. */
                            pCID->uFormat = fFormat;
                            RTListAppend(&pClient->Legacy.lstCID, &pCID->Node);
                            pClient->Legacy.cCID++;
                        }
                        else
                            vrc = VERR_NO_MEMORY;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    /*
                     * Format the message.
                     */
                    if (pMsg->idMsg == VBOX_SHCL_HOST_MSG_READ_DATA_CID)
                        HGCMSvcSetU64(&pMsg->aParms[0], uCID);
                    else
                        HGCMSvcSetU32(&pMsg->aParms[0], VBOX_SHCL_HOST_MSG_READ_DATA);
                    HGCMSvcSetU32(&pMsg->aParms[1], fFormat);

                    ShClSvcClientMsgAdd(pClient, pMsg, true /* fAppend */);
                    /* Wake up the client to let it know that there are new messages. */
                    ShClSvcClientWakeup(pClient);

                    /* Return event to caller. */
                    if (ppEvent)
                        *ppEvent = pEvent;
                }

                /* Remove event from list if caller did not request event handle or in case
                 * of failure (in this case caller should not release event). */
                if (   RT_FAILURE(vrc)
                    || !ppEvent)
                {
                    ShClEventRelease(pEvent);
                    pEvent = NULL;
                }
            }
            else
                vrc = VERR_SHCLPB_MAX_EVENTS_REACHED;

            if (RT_FAILURE(vrc))
                ShClSvcClientMsgFree(pClient, pMsg);

            ShClSvcClientUnlock(pClient);
        }
        else
            vrc = VERR_NO_MEMORY;

        if (RT_FAILURE(vrc))
            break;
    }

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Requesting guest clipboard data failed with %Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Reads clipboard data from the guest.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_NO_DATA if no clipboard data is available.
 * @param   hClient             Opaque client to request data from.
 * @param   fFormats            The formats being requested, OR'ed together (VBOX_SHCL_FMT_XXX).
 * @param   ppv                 Where to return the allocated data read.
 *                              Must be free'd by the caller.
 * @param   pcb                 Where to return number of bytes read.
 */
static DECLCALLBACK(int) shClSvcOpReadDataFromGuest(SHCLCLIENTHANDLE hClient, SHCLFORMAT fFormats,
                                                           void **ppv, uint32_t *pcb)
{
    AssertPtrReturn(ppv, VERR_INVALID_POINTER);
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    /* Request data from the guest and wait for data to arrive. */
    PSHCLEVENT pEvent;
    int vrc = shClSvcOpReadDataFromGuestAsync(hClient, fFormats, &pEvent);
    if (RT_SUCCESS(vrc))
    {
        PSHCLEVENTPAYLOAD pPayload;
        vrc = ShClEventWait(pEvent, SHCL_TIMEOUT_DEFAULT_MS, &pPayload);
        if (RT_SUCCESS(vrc))
        {
            if (   pPayload
                && pPayload->cbData)
            {
                *ppv = pPayload->pvData;
                *pcb = pPayload->cbData;

                LogFlowFunc(("pv=%p, cb=%RU32\n", pPayload->pvData, pPayload->cbData));

                pPayload->pvData = NULL;
                pPayload->cbData = 0;
                ShClPayloadDestroy(pPayload);
            }
            else
            {
                ShClPayloadDestroy(pPayload);
                vrc = VERR_SHCLPB_NO_DATA;
            }
        }

        ShClEventRelease(pEvent);
    }

    if (   RT_FAILURE(vrc)
        && vrc != VERR_SHCLPB_NO_DATA)
        LogRel(("Shared Clipboard: Reading data from guest failed with %Rrc\n", vrc));
    return vrc;
}


/**
 * Applies transfer policy and compatibility rules to a clipboard format announcement.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque service client whose policy to apply.
 * @param   fHostToGuest        Whether the formats flow from host to guest.
 * @param   fFormats            Input formats, VBOX_SHCL_FMT_XXX.
 * @param   pfFiltered          Where to return the filtered format mask.
 */
static DECLCALLBACK(int) shClSvcOpFilterFormats(SHCLCLIENTHANDLE hClient, bool fHostToGuest,
                                                       SHCLFORMATS fFormats, SHCLFORMATS *pfFiltered)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFiltered, VERR_INVALID_POINTER);
    *pfFiltered = shClSvcHandleFormats(fHostToGuest, pClient, fFormats);
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Retains a transfer selected by its context-local ID.
 *
 * @returns Retained transfer, or NULL if it was not found.
 * @param   hClient             Opaque service client owning the transfer.
 * @param   idTransfer          Transfer ID to look up.
 *
 * @note    The caller must release a returned transfer with ShClTransferRelease().
 */
static DECLCALLBACK(PSHCLTRANSFER) shClSvcOpTransferGetByIdRetained(SHCLCLIENTHANDLE hClient,
                                                                          SHCLTRANSFERID idTransfer)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, NULL);
    return ShClTransferCtxGetTransferByIdRetained(&pClient->Transfers.Ctx, idTransfer);
}


/**
 * Retains a transfer selected by its complete generation key.
 *
 * @returns Retained transfer, or NULL if the key is stale or unknown.
 * @param   hClient             Opaque service client owning the transfer.
 * @param   idSession           Service session ID.
 * @param   idTransfer          Transfer ID.
 * @param   uGeneration         Transfer generation.
 *
 * @note    The caller must release a returned transfer with ShClTransferRelease().
 */
static DECLCALLBACK(PSHCLTRANSFER) shClSvcOpTransferGetByKeyRetained(SHCLCLIENTHANDLE hClient,
                                                                           SHCLSESSIONID idSession,
                                                                           SHCLTRANSFERID idTransfer,
                                                                           SHCLTRANSFERGEN uGeneration)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, NULL);
    return ShClTransferCtxGetTransferByKeyRetained(&pClient->Transfers.Ctx, idSession, idTransfer, uGeneration);
}


/**
 * Creates a retained service-owned transfer.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque service client which will own the transfer.
 * @param   enmDir              Transfer direction.
 * @param   enmSource           Transfer source.
 * @param   pCallbacks          Callback table to copy.  Optional.
 * @param   idTransfer          Requested transfer ID, or NIL_SHCLTRANSFERID.
 * @param   ppTransfer          Where to return the retained transfer.  Optional.
 */
static DECLCALLBACK(int) shClSvcOpTransferCreate(SHCLCLIENTHANDLE hClient, SHCLTRANSFERDIR enmDir,
                                                        SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                                                        SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer)
{
    return ShClSvcTransferCreate((PSHCLCLIENT)hClient, enmDir, enmSource, pCallbacks, idTransfer, ppTransfer);
}


/**
 * Initializes a service-owned transfer.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque service client owning the transfer.
 * @param   pTransfer           Transfer to initialize.
 */
static DECLCALLBACK(int) shClSvcOpTransferInit(SHCLCLIENTHANDLE hClient, PSHCLTRANSFER pTransfer)
{
    return ShClSvcTransferInit((PSHCLCLIENT)hClient, pTransfer);
}


/**
 * Reports a host-side terminal transfer status.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque service client owning the transfer.
 * @param   pTransfer           Transfer whose terminal state is being reported.
 * @param   enmStatus           Terminal transfer status.
 * @param   rcStatus            Status-specific result code.
 */
static DECLCALLBACK(int) shClSvcOpTransferReportStatus(SHCLCLIENTHANDLE hClient, PSHCLTRANSFER pTransfer,
                                                       SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    return ShClSvcTransferReportStatus((PSHCLCLIENT)hClient, pTransfer, enmStatus, rcStatus);
}


/**
 * Destroys a service-owned transfer selected by ID.
 *
 * @param   hClient             Opaque service client owning the transfer.
 * @param   idTransfer          Transfer ID to destroy.
 */
static DECLCALLBACK(void) shClSvcOpTransferDestroyById(SHCLCLIENTHANDLE hClient, SHCLTRANSFERID idTransfer)
{
    ShClSvcTransferDestroyById((PSHCLCLIENT)hClient, idTransfer);
}


/**
 * Destroys all transfers owned by a disconnecting service client.
 *
 * @param   hClient             Opaque service client whose transfers to destroy.
 */
static DECLCALLBACK(void) shClSvcOpTransferDestroyAll(SHCLCLIENTHANDLE hClient)
{
    shClSvcTransferDestroyAll((PSHCLCLIENT)hClient);
}


/**
 * Reads guest object payload and reports the successful byte count to Main.
 *
 * @copydoc SHCLTXPROVIDERIFACE::pfnObjRead
 */
static DECLCALLBACK(int) shClSvcOpTransferProviderGuestObjRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj,
                                                               void *pvData, uint32_t cbData, uint32_t fFlags,
                                                               uint32_t *pcbRead)
{
    uint32_t cbRead = 0;
    int const vrc = ShClSvcTransferIfaceGHObjRead(pCtx, hObj, pvData, cbData, fFlags, &cbRead);
    if (RT_SUCCESS(vrc))
    {
        if (pcbRead)
            *pcbRead = cbRead;

        PSHCLCLIENT const pClient = (PSHCLCLIENT)pCtx->pvUser;
        AssertPtr(pClient);
        ShClSvcTransferReportProgress(pClient, pCtx->pTransfer, hObj, cbRead);
    }
    return vrc;
}


/**
 * Initializes a provider which obtains transfer data from the guest.
 *
 * @returns VBox status code.
 * @param   hClient             Opaque service client used by the provider callbacks.
 * @param   pProvider           Provider structure to initialize.
 */
static DECLCALLBACK(int) shClSvcOpTransferProviderInitGuest(SHCLCLIENTHANDLE hClient,
                                                                   PSHCLTXPROVIDER pProvider)
{
    PSHCLCLIENT const pClient = (PSHCLCLIENT)hClient;
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pProvider, VERR_INVALID_POINTER);

    RT_ZERO(*pProvider);
    pProvider->Interface.pfnRootListRead  = ShClSvcTransferIfaceGHRootListRead;
    pProvider->Interface.pfnListOpen      = ShClSvcTransferIfaceGHListOpen;
    pProvider->Interface.pfnListClose     = ShClSvcTransferIfaceGHListClose;
    pProvider->Interface.pfnListHdrRead   = ShClSvcTransferIfaceGHListHdrRead;
    pProvider->Interface.pfnListEntryRead = ShClSvcTransferIfaceGHListEntryRead;
    pProvider->Interface.pfnObjOpen       = ShClSvcTransferIfaceGHObjOpen;
    pProvider->Interface.pfnObjClose      = ShClSvcTransferIfaceGHObjClose;
    pProvider->Interface.pfnObjRead       = shClSvcOpTransferProviderGuestObjRead;
    pProvider->enmSource = SHCLSOURCE_REMOTE;
    pProvider->pvUser    = pClient;
    pProvider->cbUser    = sizeof(*pClient);
    return VINF_SUCCESS;
}
#endif


/** The immutable operation table shared by all service-owned clients. */
static SHCLSVCOPS const s_ShClSvcOps =
{
    sizeof(s_ShClSvcOps),
    shClSvcOpFilterFormats,
    shClSvcOpReportFormatsToGuest,
    shClSvcOpReadDataFromGuestAsync,
    shClSvcOpReadDataFromGuest,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClSvcOpTransferGetByIdRetained,
    shClSvcOpTransferGetByKeyRetained,
    shClSvcOpTransferCreate,
    shClSvcOpTransferInit,
    shClSvcOpTransferReportStatus,
    shClSvcOpTransferDestroyById,
    shClSvcOpTransferDestroyAll,
    shClSvcOpTransferProviderInitGuest,
#endif
};


/**
 * Creates the synchronous, non-owning Main transport for a service client.
 *
 * @param   pClient             Service-owned client represented by the transport.
 * @param   pTransport          Where to return the transport value.
 *
 * @note    The transport is valid only while the service client remains connected.
 *          Callers must not retain it past the synchronous disconnect callback.
 */
void shClSvcCreateTransport(PSHCLCLIENT pClient, PSHCLTRANSPORT pTransport)
{
    AssertPtrReturnVoid(pClient);
    AssertPtrReturnVoid(pTransport);
    pTransport->hClient = (SHCLCLIENTHANDLE)pClient;
    pTransport->pOps    = &s_ShClSvcOps;
}
