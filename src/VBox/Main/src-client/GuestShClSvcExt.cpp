/* $Id: GuestShClSvcExt.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard service extension handling for Main.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "LoggingNew.h"

#include "ConsoleImpl.h"
#include "ClipboardImpl.h"
#include "GuestShClPrivate.h"
#include "Global.h"

#include <VBox/GuestHost/SharedClipboard.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/VMMDev.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/string.h>


/** Maximum service-extension error string length accepted from the host service. */
static size_t const s_cchShClSvcExtStringMax = _64K;


/**
 * Validates a single Shared Clipboard format from service-extension parameters.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if \a uFormat names one valid VBOX_SHCL_FMT_XXX bit.
 * @retval  VERR_INVALID_PARAMETER otherwise.
 * @param   uFormat             Format value to validate.
 * @param   u32Function         Service-extension function being validated.
 */
static int shClSvcExtValidateFormat(SHCLFORMAT uFormat, uint32_t u32Function)
{
    if (ShClFormatIsValid(uFormat))
        return VINF_SUCCESS;
    LogRelMax2(16, ("Shared Clipboard: Rejecting service-extension function %RU32 with invalid format %#x\n",
                    u32Function, uFormat));
    return VERR_INVALID_PARAMETER;
}

/**
 * Validates a string coming from the HGCM dispatcher.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if \a pcszString is NULL and allowed, or if it is NUL-terminated within
 *          the configured limit and contains valid UTF-8.
 * @retval  VERR_INVALID_POINTER if \a pcszString is required but NULL.
 * @retval  VERR_INVALID_PARAMETER if the string is empty when disallowed, not terminated within
 *          the configured limit, or is not valid UTF-8.
 * @param   pcszString          String to validate. Can be NULL if \a fAllowNull is true.
 * @param   fAllowNull          Whether NULL is accepted.
 * @param   fAllowEmpty         Whether an empty string is accepted.
 */
static int shClSvcExtValidateUtf8Z(const char *pcszString, bool fAllowNull, bool fAllowEmpty)
{
    if (!pcszString)
        return fAllowNull ? VINF_SUCCESS : VERR_INVALID_POINTER;
    AssertReturn(RT_VALID_PTR(pcszString), VERR_INVALID_POINTER);

    size_t cchString = 0;
    int vrc = RTStrNLenEx(pcszString, s_cchShClSvcExtStringMax, &cchString);
    if (RT_FAILURE(vrc))
        return VERR_INVALID_PARAMETER;
    if (   !fAllowEmpty
        && !cchString)
        return VERR_INVALID_PARAMETER;

    vrc = RTStrValidateEncodingEx(pcszString, cchString + 1,
                                  RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED
                                | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_FAILURE(vrc))
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}

/**
 * Validates a Shared Clipboard data buffer pointer and size pair.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if the buffer contract is valid.
 * @retval  VERR_INVALID_POINTER if \a pvData is required but invalid.
 * @retval  VERR_INVALID_PARAMETER if \a cbData exceeds the negotiated HGCM payload limit.
 * @param   pvData              Data buffer pointer. Can be NULL only when \a cbData is zero.
 * @param   cbData              Data buffer size in bytes.
 */
static int shClSvcExtValidateDataBuffer(void *pvData, uint32_t cbData)
{
    AssertReturn(cbData <= VBOX_SHCL_MAX_CHUNK_SIZE, VERR_INVALID_PARAMETER);
    if (cbData)
        AssertReturn(RT_VALID_PTR(pvData), VERR_INVALID_POINTER);
    return VINF_SUCCESS;
}


/**
 * Forwards a Shared Clipboard service extension request to the chained VRDP extension.
 *
 * @returns VBox status code returned by the chained extension, or VERR_NOT_SUPPORTED if no
 *          chained extension is registered.
 * @param   u32Function         Service extension function to forward.
 * @param   pvParms             Raw service extension parameters to forward.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_forwardToSvcExt(uint32_t u32Function, void *pvParms, uint32_t cbParms)
{
    PSHCLSVCEXT const pSvcExtVRDP = &m_SvcExtVRDP; /* Currently we have one extension only. */
    if (pSvcExtVRDP->pfnExt)
        return pSvcExtVRDP->pfnExt(pSvcExtVRDP->pvExt, u32Function, pvParms, cbParms);
    return VERR_NOT_SUPPORTED;
}

/**
 * Validates Shared Clipboard service extension parameters before dispatching a request.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_POINTER if a required pointer is NULL or not a valid host pointer.
 * @retval  VERR_INVALID_PARAMETER if a parameter ist invalid.
 * @param   u32Function         Service extension function being dispatched.
 * @param   pvParms             Raw service extension parameters to validate. Optional for unknown
 *                              function IDs.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_validateSvcExtParms(uint32_t u32Function, void *pvParms, uint32_t cbParms)
{
    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK:
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST:
        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
        case VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE:
        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        case VBOX_CLIPBOARD_EXT_FN_ERROR:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
#endif
            AssertReturn(RT_VALID_PTR(pvParms), VERR_INVALID_POINTER);
            AssertReturn(cbParms == sizeof(SHCLEXTPARMS), VERR_INVALID_PARAMETER);
            break;

        default:
            return VINF_SUCCESS;
    }

    PSHCLCLIENT pActiveClient = NULL;
    int vrc = lock();
    if (RT_SUCCESS(vrc))
    {
        pActiveClient = m_pClient;
        unlock();
    }
    else
        return vrc;

    PSHCLEXTPARMS const pParms = (PSHCLEXTPARMS)pvParms;
    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK:
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            AssertReturn(ShClFormatsAreValid(pParms->u.ReportFormats.uFormats),
                         VERR_INVALID_PARAMETER);
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReportFormats.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            AssertPtrReturn(pParms->u.ReportFormats.pClient->pBackend, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReportFormats.enmSource == SHCLSOURCE_INVALID, VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST:
            AssertReturn(ShClFormatsAreValid(pParms->u.ReportFormats.uFormats),
                         VERR_INVALID_PARAMETER);
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReportFormats.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            AssertPtrReturn(pParms->u.ReportFormats.pClient->pBackend, VERR_INVALID_POINTER);
            AssertReturn(ShClSourceIsValid(pParms->u.ReportFormats.enmSource), VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            vrc = shClSvcExtValidateFormat(pParms->u.ReadWriteData.uFormat, u32Function);
            if (RT_FAILURE(vrc))
                return vrc;
            vrc = shClSvcExtValidateDataBuffer(pParms->u.ReadWriteData.pvData, pParms->u.ReadWriteData.cbData);
            if (RT_FAILURE(vrc))
                return vrc;
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReadWriteData.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            AssertPtrReturn(pParms->u.ReadWriteData.pClient->pBackend, VERR_INVALID_POINTER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE:
            vrc = shClSvcExtValidateFormat(pParms->u.ReadWriteData.uFormat, u32Function);
            if (RT_FAILURE(vrc))
                return vrc;
            vrc = shClSvcExtValidateDataBuffer(pParms->u.ReadWriteData.pvData, pParms->u.ReadWriteData.cbData);
            if (RT_FAILURE(vrc))
                return vrc;
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReadWriteData.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
            vrc = shClSvcExtValidateFormat(pParms->u.ReadWriteData.uFormat, u32Function);
            if (RT_FAILURE(vrc))
                return vrc;
            vrc = shClSvcExtValidateDataBuffer(pParms->u.ReadWriteData.pvData, pParms->u.ReadWriteData.cbData);
            if (RT_FAILURE(vrc))
                return vrc;
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.ReadWriteData.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            AssertPtrReturn(pParms->u.ReadWriteData.pClient->pBackend, VERR_INVALID_POINTER);
            AssertPtrReturn(pParms->u.ReadWriteData.pCmdCtx, VERR_INVALID_POINTER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
            AssertReturn(pActiveClient == NULL, VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
            AssertReturn(pActiveClient == NULL, VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_ERROR:
            vrc = shClSvcExtValidateUtf8Z(pParms->u.Error.pszId, true /* fAllowNull */, true /* fAllowEmpty */);
            if (RT_FAILURE(vrc))
                return vrc;
            vrc = shClSvcExtValidateUtf8Z(pParms->u.Error.pszMsg, false /* fAllowNull */, false /* fAllowEmpty */);
            if (RT_FAILURE(vrc))
                return vrc;
            AssertReturn(RT_FAILURE(pParms->u.Error.rc), VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        {
            AssertPtrReturn(pActiveClient, VERR_INVALID_POINTER);
            AssertReturn(pParms->u.FileTransferData.pClient == pActiveClient, VERR_INVALID_PARAMETER);
            PSHCLCLIENT const pClient = pParms->u.FileTransferData.pClient;
            AssertPtrReturn(pClient->pBackend, VERR_INVALID_POINTER);
            PSHCLTRANSFER const pTransfer = pParms->u.FileTransferData.pTransfer;
            AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
            AssertPtrReturn(pParms->u.FileTransferData.pReply, VERR_INVALID_POINTER);
            AssertReturn(ShClSourceIsValid(pParms->u.FileTransferData.enmShClSource), VERR_INVALID_PARAMETER);
            PSHCLTRANSFER const pRegisteredTransfer
                = ShClTransferCtxGetTransferByKey(&pClient->Transfers.Ctx,
                                                  ShClTransferGetSessionId(pTransfer),
                                                  ShClTransferGetID(pTransfer),
                                                  ShClTransferGetGeneration(pTransfer));
            if (pRegisteredTransfer != pTransfer)
                return VERR_INVALID_CONTEXT;

            PSHCLREPLY const pReply = pParms->u.FileTransferData.pReply;
            AssertReturn(pReply->uType == VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS, VERR_INVALID_PARAMETER);
            AssertReturn(pReply->pvPayload == NULL, VERR_INVALID_PARAMETER);
            AssertReturn(pReply->cbPayload == 0, VERR_INVALID_PARAMETER);
            SHCLTRANSFERSTATUS const enmStatus = pReply->u.TransferStatus.uStatus;
            AssertReturn(   enmStatus != SHCLTRANSFERSTATUS_NONE
                         && ShClTransferStatusResultIsValid(enmStatus, (int)pReply->rc),
                         VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;
        }
#endif

        default:
            return VINF_SUCCESS;
    }
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK from the Shared Clipboard host service.
 *
 * @returns VBox status code.
 * @param   pParms              Service extension parameters containing the callback to install.
 */
int GuestShCl::i_handleSvcExtSetCallback(PSHCLEXTPARMS pParms)
{
    m_pfnExtCallback = pParms->u.SetCallback.pfnCallback;
    return VINF_SUCCESS;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST from the Shared Clipboard host service.
 *
 * Reports guest clipboard formats to Main and forwards the notification to the chained VRDP extension.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtReportFormatsToHost(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    SHCLFORMATS fFormats = pParms->u.ReportFormats.uFormats;

    i_incGuestDataSeq();

    AssertPtr(m_pConsole->i_getClipboard());
    if (m_pConsole->i_getClipboard())
        m_pConsole->i_getClipboard()->i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE,
                                                       fFormats, ClipboardSource_Guest,
                                                       true /* fForceNotify */);

    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST, pvParms, cbParms);
    return vrc == VERR_NOT_SUPPORTED ? VINF_SUCCESS : vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST from the Shared Clipboard host service.
 *
 * Forwards host or remote clipboard format notifications to the chained VRDP extension without
 * implicitly publishing them through Main or the regular backend.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtReportFormatsToGuest(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pParms);

    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST, pvParms, cbParms);
    return vrc == VERR_NOT_SUPPORTED ? VINF_SUCCESS : vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_DATA_READ from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension service the request first, then reads clipboard data provided
 * explicitly through Main.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtDataRead(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_DATA_READ, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        void *pvData = pParms->u.ReadWriteData.pvData;
        uint32_t cbData = pParms->u.ReadWriteData.cbData;
        SHCLFORMATS fFormats = pParms->u.ReadWriteData.uFormat;

        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        if (pClipboard)
        {
            HRESULT hrc = pClipboard->i_readDataForGuest(fFormats, pvData, cbData, &pParms->u.ReadWriteData.cbActual);
            vrc = SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_NO_DATA;
        }
        else
            vrc = VERR_NOT_AVAILABLE;
        if (RT_SUCCESS(vrc))
            LogRel2(("Shared Clipboard: Read Main clipboard data (max %RU32 bytes), got %RU32 bytes\n", cbData,
                     pParms->u.ReadWriteData.cbActual));
        else
            LogRel2(("Shared Clipboard: No explicit Main clipboard data available, vrc=%Rrc\n", vrc));
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE from the Shared Clipboard host service.
 *
 * Reads clipboard data asynchronously from the guest for the VRDE extension path and copies the
 * received payload into the supplied extension buffer.
 *
 * @returns VBox status code.
 * @param   pParms              Service extension parameters describing the read request.
 */
int GuestShCl::i_handleSvcExtDataReadVrde(PSHCLEXTPARMS pParms)
{
    PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
    SHCLFORMATS fFormats = pParms->u.ReadWriteData.uFormat;
    PSHCLEVENT pEvent;
    void *pvData = pParms->u.ReadWriteData.pvData;
    uint32_t cbData = pParms->u.ReadWriteData.cbData;

    int vrc = ShClSvcReadDataFromGuestAsync(pClient, fFormats, &pEvent);
    if (RT_SUCCESS(vrc))
    {
        PSHCLEVENTPAYLOAD pPayload = NULL;
        vrc = ShClEventWait(pEvent, SHCL_TIMEOUT_DEFAULT_MS, &pPayload);
        if (RT_SUCCESS(vrc))
        {
            if (pPayload)
            {
                memcpy(pvData, pPayload->pvData, RT_MIN(cbData, pPayload->cbData));
                ShClPayloadDestroy(pPayload);
                pPayload = NULL;
            }
            else
                pvData = NULL;
        }
        ShClEventRelease(pEvent);
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_DATA_WRITE from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension service the request first, then signals pending guest-data waiters
 * without implicitly writing guest data to the host clipboard backend.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtDataWrite(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
    PSHCLCLIENTCMDCTX pCmdCtx = pParms->u.ReadWriteData.pCmdCtx;
    SHCLFORMATS fFormats = pParms->u.ReadWriteData.uFormat;

    PSHCLEVENT pEvent = NULL;
    int vrc = ShClSvcGuestDataRetainValidatedEvent(pClient, pCmdCtx, fFormats, &pEvent);
    if (RT_FAILURE(vrc) || !pEvent)
        return vrc;

    int const vrcChained = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_DATA_WRITE, pvParms, cbParms);
    if (vrcChained == VERR_NOT_SUPPORTED)
    {
        void *pvData = pParms->u.ReadWriteData.pvData;
        uint32_t cbData = pParms->u.ReadWriteData.cbData;

        SHCLEVENTID const idEvent = VBOX_SHCL_CONTEXTID_GET_EVENT(pCmdCtx->uContextID);
        vrc = ShClSvcGuestDataSignalEvent(pEvent, idEvent, pvData, cbData);
        if (RT_FAILURE(vrc))
            LogRelMax(16, ("Shared Clipboard: Signalling host about guest clipboard data failed with %Rrc\n", vrc));
        AssertRC(vrc);
    }
    else
        vrc = vrcChained;
    ShClEventRelease(pEvent);
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension initialize first, then initializes the regular Shared Clipboard
 * backend if the extension did not handle the request.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtBackendInit(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
        VBOXHGCMSVCFNTABLE *pTable = pParms->u.ReadWriteData.pTable;
        vrc = ShClBackendInit(pBackend, pTable);
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension tear down first, then destroys the regular Shared Clipboard backend
 * if the extension did not handle the request.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtBackendDestroy(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
        ShClBackendDestroy(pBackend);
        vrc = VINF_SUCCESS;
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension connect first, then connects the regular backend and records the
 * active HGCM client when the backend connection succeeds.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtBackendConnect(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
        PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
        bool fHeadless = pParms->u.ReadWriteData.fHeadless;
        vrc = ShClBackendConnect(pBackend, pClient, fHeadless);
        if (RT_SUCCESS(vrc))
        {
            lock();
            m_pClient = pClient;
            m_fGuestReadsBlocked = false;
            unlock();
        }
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension disconnect first, then disconnects the regular backend and clears
 * the active HGCM client if it matches the disconnecting client.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtBackendDisconnect(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
        lock();
        if (m_pClient == pClient)
            m_fGuestReadsBlocked = true;
        unlock();

        i_waitForGuestReads();

        vrc = ShClBackendDisconnect(pClient->pBackend, pClient);

        lock();
        if (m_pClient == pClient)
        {
            m_pClient = NULL;
            m_fGuestReadsBlocked = false;
        }
        unlock();
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension synchronize first, then synchronizes the regular Shared Clipboard
 * backend if the extension did not handle the request.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtBackendSync(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
    {
        PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
        PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
        vrc = ShClBackendSync(pBackend, pClient);
    }
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_ERROR from the Shared Clipboard host service.
 *
 * Converts the extension error parameters into a Main clipboard error notification.
 *
 * @returns VBox status code.
 * @param   pParms              Service extension parameters containing the error details.
 */
int GuestShCl::i_handleSvcExtError(PSHCLEXTPARMS pParms)
{
    return ReportError(pParms->u.Error.pszId, pParms->u.Error.rc, pParms->u.Error.pszMsg);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER from the Shared Clipboard host service.
 *
 * Lets the chained VRDP extension handle transfer status first, then forwards the transfer status
 * reply to the regular Shared Clipboard backend if the extension did not handle the request.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Raw service extension parameters to forward to the chained extension.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_handleSvcExtFileTransfer(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    PSHCLCLIENT pClient = pParms->u.FileTransferData.pClient;
    PSHCLTRANSFER pTransfer = pParms->u.FileTransferData.pTransfer;
    SHCLSOURCE const enmShClSource = pParms->u.FileTransferData.enmShClSource;
    PSHCLREPLY pReply = pParms->u.FileTransferData.pReply;
    SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);
    SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);
    SHCLTRANSFERSTATUS const enmStatus = pReply->u.TransferStatus.uStatus;
    int const vrcTransfer = (int)pReply->rc;

    int vrc = i_forwardToSvcExt(VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER, pvParms, cbParms);
    if (vrc == VERR_NOT_SUPPORTED)
        vrc = ShClBackendTransferHandleStatusReply(pClient->pBackend, pClient, pTransfer, enmShClSource,
                                                   pReply->u.TransferStatus.uStatus, (int)pReply->rc);

    if (RT_SUCCESS(vrc))
    {
        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        if (pClipboard)
        {
            /*
             * enmShClSource identifies the endpoint which issued this reply
             * and is therefore the right value for the platform backend.
             * Main's persistent transfer object records the data source
             * instead, which is an invariant of the backing transfer even
             * when the opposite endpoint reports a lifecycle transition.
             */
            SHCLSOURCE const enmTransferSource = ShClTransferGetSource(pTransfer);
            HRESULT const hrc = pClipboard->i_handleTransferStatus(idSession, idTransfer, uGeneration, pTransfer,
                                                                   enmTransferSource, enmStatus, vrcTransfer);
            if (FAILED(hrc))
            {
                LogFunc(("Main transfer status handling failed: hrc=%Rhrc\n", hrc));
                vrc = Global::vboxStatusCodeFromCOM(hrc);
            }
        }
    }

    return vrc;
}
#endif
