/* $Id: GuestShClSvcExt.cpp 115131 2026-08-25 17:30:42Z andreas.loeffler@oracle.com $ */
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
#include "ConsoleVRDPServer.h"
#include "ClipboardImpl.h"
#include "GuestShClPrivate.h"
#include "GuestShClConn.h"
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
    LogRelMax(16, ("Shared Clipboard: Rejecting service-extension function %RU32 with invalid format %#x\n",
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
static int shClSvcExtValidateDataBuffer(void const *pvData, uint32_t cbData)
{
    AssertReturn(cbData <= VBOX_SHCL_MAX_CHUNK_SIZE, VERR_INVALID_PARAMETER);
    if (cbData)
        AssertReturn(RT_VALID_PTR(pvData), VERR_INVALID_POINTER);
    return VINF_SUCCESS;
}


/**
 * Validates Shared Clipboard service extension parameters before dispatching a request.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_POINTER if a required pointer is NULL or not a valid host pointer.
 * @retval  VERR_INVALID_PARAMETER if a parameter is invalid.
 * @retval  VERR_RESOURCE_BUSY if a backend connection is requested while a client is active.
 * @param   u32Function         Service extension function being dispatched.
 * @param   pvParms             Raw service extension parameters to validate. Optional for unknown
 *                              function IDs.
 * @param   cbParms             Size, in bytes, of \a pvParms.
 */
int GuestShCl::i_svcExtParmsValidate(uint32_t u32Function, void *pvParms, uint32_t cbParms)
{
    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        case VBOX_CLIPBOARD_EXT_FN_ERROR:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS:
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET:
#endif
            AssertReturn(RT_VALID_PTR(pvParms), VERR_INVALID_POINTER);
            AssertReturn(cbParms == sizeof(SHCLEXTPARMS), VERR_INVALID_PARAMETER);
            break;

        default:
            return VINF_SUCCESS;
    }

    PSHCLEXTPARMS const pParms = (PSHCLEXTPARMS)pvParms;
    SHCLTRANSPORT Transport;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (u32Function == VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER)
        Transport = pParms->u.FileTransferData.Transport;
    else if (u32Function == VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS)
        Transport = pParms->u.FileTransferProgress.Transport;
    else if (u32Function == VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET)
        Transport = pParms->u.FileTransferReset.Transport;
    else
#endif
        Transport = ShClSvcExtGetTransport(pParms);
#define SHCL_VALIDATE_ACTIVE(a_Transport) \
    do { AssertReturn(m_pConn->matches(&(a_Transport)), VERR_INVALID_HANDLE); } while (0)
    int vrc;
    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            SHCL_VALIDATE_ACTIVE(Transport);
            AssertReturn(ShClFormatsAreValid(pParms->u.ReportFormats.uFormats),
                         VERR_INVALID_PARAMETER);
            AssertReturn(pParms->u.ReportFormats.enmSource == SHCLSOURCE_INVALID, VERR_INVALID_PARAMETER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            SHCL_VALIDATE_ACTIVE(Transport);
            vrc = shClSvcExtValidateFormat(pParms->u.ReadWriteData.uFormat, u32Function);
            if (RT_FAILURE(vrc))
                return vrc;
            vrc = shClSvcExtValidateDataBuffer(pParms->u.ReadWriteData.pvData, pParms->u.ReadWriteData.cbData);
            return vrc;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
            SHCL_VALIDATE_ACTIVE(Transport);
            vrc = shClSvcExtValidateFormat(pParms->u.ReadWriteData.uFormat, u32Function);
            if (RT_FAILURE(vrc))
                return vrc;
            return shClSvcExtValidateDataBuffer(pParms->u.ReadWriteData.pvData, pParms->u.ReadWriteData.cbData);

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
            AssertReturn(!m_pConn->isConnected(), VERR_RESOURCE_BUSY);
            AssertReturn(ShClTransportIsValid(&Transport), VERR_INVALID_HANDLE);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
            SHCL_VALIDATE_ACTIVE(Transport);
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
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
            SHCL_VALIDATE_ACTIVE(Transport);
            AssertPtrReturn(pParms->u.TransferCallbacks.pCallbacks, VERR_INVALID_POINTER);
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        {
            SHCL_VALIDATE_ACTIVE(Transport);
            PCSHCLTRANSFERKEY const pKey = &pParms->u.FileTransferData.Key;
            AssertReturn(ShClTransferKeyIsValid(pKey), VERR_INVALID_PARAMETER);
            AssertReturn(ShClTransferDirIsValid(pParms->u.FileTransferData.enmDir), VERR_INVALID_PARAMETER);
            AssertReturn(ShClSourceIsValid(pParms->u.FileTransferData.enmTransferSource), VERR_INVALID_PARAMETER);
            AssertReturn(ShClSourceIsValid(pParms->u.FileTransferData.enmReplySource), VERR_INVALID_PARAMETER);
            AssertReturn(   (   pParms->u.FileTransferData.enmTransferSource == SHCLSOURCE_REMOTE
                              && pParms->u.FileTransferData.enmDir == SHCLTRANSFERDIR_GUEST_TO_HOST)
                         || (   pParms->u.FileTransferData.enmTransferSource == SHCLSOURCE_LOCAL
                              && pParms->u.FileTransferData.enmDir == SHCLTRANSFERDIR_HOST_TO_GUEST),
                         VERR_INVALID_PARAMETER);

            SHCLTRANSFERSTATUS const enmStatus = pParms->u.FileTransferData.enmStatus;
            AssertReturn(   enmStatus != SHCLTRANSFERSTATUS_NONE
                         && ShClTransferStatusResultIsValid(enmStatus, pParms->u.FileTransferData.rcStatus),
                         VERR_INVALID_PARAMETER);

            PSHCLTRANSFER const pRegisteredTransfer
                = m_pConn->transferGetByKeyRetained(pKey);
            if (pRegisteredTransfer)
            {
                bool const fMatches =    ShClTransferGetDir(pRegisteredTransfer) == pParms->u.FileTransferData.enmDir
                                      && ShClTransferGetSource(pRegisteredTransfer)
                                      == pParms->u.FileTransferData.enmTransferSource;
                ShClTransferRelease(pRegisteredTransfer);
                AssertReturn(fMatches, VERR_INVALID_CONTEXT);
            }
            else
                AssertReturn(ShClTransferStatusIsTerminal(enmStatus), VERR_INVALID_CONTEXT);
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS:
        {
            SHCL_VALIDATE_ACTIVE(Transport);
            PCSHCLTRANSFERKEY const pKey = &pParms->u.FileTransferProgress.Key;
            AssertReturn(ShClTransferKeyIsValid(pKey), VERR_INVALID_PARAMETER);
            AssertReturn(pParms->u.FileTransferProgress.cbTotal > 0, VERR_INVALID_PARAMETER);
            AssertReturn(pParms->u.FileTransferProgress.cbProcessed <= pParms->u.FileTransferProgress.cbTotal,
                         VERR_INVALID_PARAMETER);
            PSHCLTRANSFER const pRegisteredTransfer
                = m_pConn->transferGetByKeyRetained(pKey);
            if (!pRegisteredTransfer)
                return VERR_INVALID_CONTEXT;
            ShClTransferRelease(pRegisteredTransfer);
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET:
            SHCL_VALIDATE_ACTIVE(Transport);
            return VINF_SUCCESS;
#endif

        default:
            return VINF_SUCCESS;
    }
#undef SHCL_VALIDATE_ACTIVE
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST from the Shared Clipboard host service.
 *
 * Reports guest clipboard formats to Main and connected remote-desktop clients.
 *
 * @retval  VERR_INVALID_PARAMETER if the reported format mask is invalid.
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtReportFormatsToHostCallback(PSHCLEXTPARMS pParms)
{
    SHCLFORMATS fFormats = pParms->u.ReportFormats.uFormats;
    i_incGuestDataSeq();

    if (IsNativeBackendActive())
    {
        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        AssertPtr(pClipboard);
        if (pClipboard)
            pClipboard->i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE, fFormats, ClipboardSource_Guest,
                                        true /* fForceNotify */);
    }

    ConsoleVRDPServer *pVrde = m_pConsole->i_consoleVRDPServer();
    int const vrc = pVrde ? pVrde->ClipboardReportGuestFormats(fFormats & ~VBOX_SHCL_FMT_URI_LIST)
                          : VERR_NOT_SUPPORTED;
    return vrc == VERR_NOT_SUPPORTED ? VINF_SUCCESS : vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_DATA_READ from the Shared Clipboard host service.
 *
 * Reads clipboard data from the provider selected by Main.
 *
 * @retval  VERR_INVALID_PARAMETER if the requested format is invalid.
 * @retval  VERR_NOT_SUPPORTED if VRDE is selected and the requested format is
 *          VBOX_SHCL_FMT_URI_LIST, or VRDE has no clipboard interface.
 * @retval  VERR_NOT_AVAILABLE if the selected provider is unavailable.
 * @retval  VERR_NO_DATA if reading from Main's native clipboard fails.
 * @retval  VERR_RESOURCE_BUSY if a read from the selected VRDE clipboard provider is already in progress.
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtDataReadCallback(PSHCLEXTPARMS pParms)
{
    void *pvData = pParms->u.ReadWriteData.pvData;
    uint32_t cbData = pParms->u.ReadWriteData.cbData;
    SHCLFORMAT uFormat = pParms->u.ReadWriteData.uFormat;
    pParms->u.ReadWriteData.cbActual = 0;

    int vrc;
    if (!IsNativeBackendActive())
    {
        if (uFormat == VBOX_SHCL_FMT_URI_LIST)
            return VERR_NOT_SUPPORTED;

        int vrcLock = RTCritSectEnter(&m_RemoteFormatsCritSect);
        if (RT_FAILURE(vrcLock))
            return vrcLock;
        if (m_fRemoteDataReadActive)
        {
            RTCritSectLeave(&m_RemoteFormatsCritSect);
            return VERR_RESOURCE_BUSY;
        }
        m_fRemoteDataReadActive = true;
        int const vrcLeave = RTCritSectLeave(&m_RemoteFormatsCritSect);
        AssertRC(vrcLeave);

        ConsoleVRDPServer *pVrde = m_pConsole->i_consoleVRDPServer();
        vrc = pVrde ? pVrde->ClipboardReadRemoteData(uFormat, pvData, cbData,
                                                     &pParms->u.ReadWriteData.cbActual)
                    : VERR_NOT_AVAILABLE;

        vrcLock = RTCritSectEnter(&m_RemoteFormatsCritSect);
        if (RT_SUCCESS(vrcLock))
        {
            m_fRemoteDataReadActive = false;
            if (m_fRemoteFormatsPending)
            {
                SHCLFORMATS const fPendingFormats = m_fPendingRemoteFormats;
                m_fRemoteFormatsPending = false;
                m_fPendingRemoteFormats = VBOX_SHCL_FMT_NONE;
                if (!IsNativeBackendActive())
                {
                    int const vrcFormats = i_reportRemoteFormatsToGuestNow(fPendingFormats);
                    if (RT_FAILURE(vrcFormats))
                        LogRelMax(16, ("Shared Clipboard: Reporting remote-to-guest formats %#x deferred while reading format %#x failed with %Rrc\n",
                                       fPendingFormats, uFormat, vrcFormats));
                }
            }

            int const vrcLeavePending = RTCritSectLeave(&m_RemoteFormatsCritSect);
            AssertRC(vrcLeavePending);
        }
        else
            AssertRC(vrcLock);
    }
    else
    {
        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        if (pClipboard)
        {
            HRESULT const hrc = pClipboard->i_readDataForGuest(uFormat, pvData, cbData,
                                                               &pParms->u.ReadWriteData.cbActual);
            vrc = SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_NO_DATA;
        }
        else
            vrc = VERR_NOT_AVAILABLE;
    }

    if (RT_SUCCESS(vrc))
        LogRel2(("Shared Clipboard: Read Main clipboard data (max %RU32 bytes), got %RU32 bytes\n", cbData,
                 pParms->u.ReadWriteData.cbActual));
    else
        LogRel2(("Shared Clipboard: Reading Main clipboard data failed with %Rrc\n", vrc));
    return vrc;
}

/**
 * Handles VBOX_CLIPBOARD_EXT_FN_DATA_WRITE from the Shared Clipboard host service.
 *
 * Mirrors the reply to VRDE when VRDE owns the clipboard route.
 *
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtDataWriteCallback(PSHCLEXTPARMS pParms)
{
    SHCLFORMAT const uFormat = pParms->u.ReadWriteData.uFormat;
    if (   !IsNativeBackendActive()
        && uFormat != VBOX_SHCL_FMT_URI_LIST)
    {
        ConsoleVRDPServer *pVrde = m_pConsole->i_consoleVRDPServer();
        if (pVrde)
        {
            int const vrcVrde = pVrde->ClipboardWriteGuestData(uFormat, pParms->u.ReadWriteData.pvData,
                                                               pParms->u.ReadWriteData.cbData);
            if (RT_FAILURE(vrcVrde) && vrcVrde != VERR_NOT_SUPPORTED)
                LogRelMax(16, ("Shared Clipboard: Mirroring guest clipboard data to VRDE failed with %Rrc\n", vrcVrde));
        }
    }
    return VINF_SUCCESS;
}

/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT from the Shared Clipboard host service.
 *
 * Initializes Main's native Shared Clipboard backend once.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Unused raw protocol parameters.
 * @param   cbParms             Size, in bytes, of \a pvParms. Unused.
 */
int GuestShCl::i_svcExtBackendInitCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pParms, pvParms, cbParms);

    return m_pConn->initBackend();
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY from the Shared Clipboard host service.
 *
 * Disconnects an active client, if any, and destroys Main's native backend.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Unused raw protocol parameters.
 * @param   cbParms             Size, in bytes, of \a pvParms. Unused.
 */
int GuestShCl::i_svcExtBackendDestroyCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pParms, pvParms, cbParms);

    int const vrc = m_pConn->destroyBackend();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    Clipboard *pClipboard = m_pConsole->i_getClipboard();
    if (pClipboard)
        pClipboard->i_resetTransfersFromService();
#endif
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT from the Shared Clipboard host service.
 *
 * Connects a client to Main's native backend and records the returned context.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Unused raw protocol parameters.
 * @param   cbParms             Size, in bytes, of \a pvParms. Unused.
 */
int GuestShCl::i_svcExtBackendConnectCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pvParms, cbParms);
    SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);

    return m_pConn->connect(&Transport);
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT from the Shared Clipboard host service.
 *
 * Disconnects a client from Main's native backend and clears its context.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Unused raw protocol parameters.
 * @param   cbParms             Size, in bytes, of \a pvParms. Unused.
 */
int GuestShCl::i_svcExtBackendDisconnectCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pvParms, cbParms);
    SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);

    int const vrc = m_pConn->disconnect(&Transport);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    Clipboard *pClipboard = m_pConsole->i_getClipboard();
    if (pClipboard)
        pClipboard->i_resetTransfersFromService();
#endif
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC from the Shared Clipboard host service.
 *
 * Synchronizes Main's native Shared Clipboard backend.
 *
 * @retval  VERR_INVALID_PARAMETER if the callback transport does not match the
 *          active Shared Clipboard connection.
 * @param   pParms              Decoded service extension parameters.
 * @param   pvParms             Unused raw protocol parameters.
 * @param   cbParms             Size, in bytes, of \a pvParms. Unused.
 */
int GuestShCl::i_svcExtBackendSyncCallback(PSHCLEXTPARMS pParms, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(pvParms, cbParms);
    SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);

    if (!m_pConn->matches(&Transport))
        return VERR_INVALID_PARAMETER;

    return IsNativeBackendActive() ? m_pConn->syncBackend() : VINF_SUCCESS;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_ERROR from the Shared Clipboard host service.
 *
 * Converts the extension error parameters into a Main clipboard error notification.
 *
 * @returns VBox status code.
 * @param   pParms              Service extension parameters containing the error details.
 */
int GuestShCl::i_svcExtErrorCallback(PSHCLEXTPARMS pParms)
{
    return ReportError(pParms->u.Error.pszId, pParms->u.Error.rc, pParms->u.Error.pszMsg);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS from the Shared Clipboard host service.
 *
 * @returns VBox status code.
 * @param   pParms              Service extension parameters containing the callback destination.
 */
int GuestShCl::i_svcExtTransferGetCallbacksCallback(PSHCLEXTPARMS pParms)
{
    SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
    return m_pConn->matches(&Transport)
         ? m_pConn->transferGetCallbacks(pParms->u.TransferCallbacks.pCallbacks)
         : VERR_INVALID_PARAMETER;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER from the Shared Clipboard host service.
 *
 * Forwards a transfer status reply to the Shared Clipboard backend and Main.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtFileTransferCallback(PSHCLEXTPARMS pParms)
{
    SHCLTRANSPORT const Transport = pParms->u.FileTransferData.Transport;
    PCSHCLTRANSFERKEY const pKey = &pParms->u.FileTransferData.Key;
    SHCLSOURCE const enmTransferSource = pParms->u.FileTransferData.enmTransferSource;
    SHCLSOURCE const enmReplySource = pParms->u.FileTransferData.enmReplySource;
    SHCLTRANSFERSTATUS const enmStatus = pParms->u.FileTransferData.enmStatus;
    int const vrcTransfer = pParms->u.FileTransferData.rcStatus;

    int vrc = m_pConn->transportAcquire(&Transport);
    if (RT_FAILURE(vrc))
        return vrc;

    if (RT_SUCCESS(vrc))
    {
        PSHCLTRANSFER const pTransfer = m_pConn->transferGetByKeyRetained(pKey);
        if (pTransfer)
        {
            if (   ShClTransferGetDir(pTransfer) == pParms->u.FileTransferData.enmDir
                && ShClTransferGetSource(pTransfer) == enmTransferSource)
                vrc = m_pConn->transferHandleStatusReply(pTransfer, enmReplySource, enmStatus, vrcTransfer);
            else
                vrc = VERR_INVALID_CONTEXT;
            ShClTransferRelease(pTransfer);
        }
        else if (!ShClTransferStatusIsTerminal(enmStatus))
            vrc = VERR_INVALID_CONTEXT;
    }

    /* A terminal guest status must always reach Main.  The native backend may
     * already have retired the transfer, but Main still owns the public record. */
    if (   RT_SUCCESS(vrc)
        || ShClTransferStatusIsTerminal(enmStatus))
    {
        Clipboard *pClipboard = m_pConsole->i_getClipboard();
        if (pClipboard)
        {
            HRESULT const hrc = pClipboard->i_handleTransferStatus(pKey, NULL,
                                                                   enmTransferSource, enmStatus, vrcTransfer);
            if (FAILED(hrc))
            {
                LogFunc(("Main transfer status handling failed: hrc=%Rhrc\n", hrc));
                if (RT_SUCCESS(vrc))
                    vrc = Global::vboxStatusCodeFromCOM(hrc);
            }
        }
    }

    m_pConn->transportRelease();
    return vrc;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS from the Shared Clipboard host service.
 *
 * Forwards exact aggregate byte progress to Main.  Progress reporting remains
 * best-effort and cannot fail the file-transfer data path.
 *
 * @returns VINF_SUCCESS.
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtTransferProgressCallback(PSHCLEXTPARMS pParms)
{
    SHCLTRANSPORT const Transport = pParms->u.FileTransferProgress.Transport;
    int const vrc = m_pConn->transportAcquire(&Transport);
    if (RT_FAILURE(vrc))
        return vrc;

    Clipboard *pClipboard = m_pConsole->i_getClipboard();
    if (pClipboard)
    {
        HRESULT const hrc = pClipboard->i_handleTransferProgress(&pParms->u.FileTransferProgress.Key,
                                                                  pParms->u.FileTransferProgress.cbProcessed,
                                                                  pParms->u.FileTransferProgress.cbTotal);
        if (FAILED(hrc))
            LogFunc(("Main transfer progress handling failed: hrc=%Rhrc, vrc=%Rrc\n",
                     hrc, Global::vboxStatusCodeFromCOM(hrc)));
    }

    m_pConn->transportRelease();
    return VINF_SUCCESS;
}


/**
 * Handles VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET from the Shared Clipboard host service.
 *
 * Resets Main's local transfer records without issuing a host-service call.
 *
 * @returns VBox status code.
 * @param   pParms              Decoded service extension parameters.
 */
int GuestShCl::i_svcExtTransferResetCallback(PSHCLEXTPARMS pParms)
{
    SHCLTRANSPORT const Transport = pParms->u.FileTransferReset.Transport;
    int const vrc = m_pConn->transportAcquire(&Transport);
    if (RT_FAILURE(vrc))
        return vrc;

    Clipboard *pClipboard = m_pConsole->i_getClipboard();
    if (pClipboard)
        pClipboard->i_resetTransfersFromService();

    m_pConn->transportRelease();
    return VINF_SUCCESS;
}
#endif
