/* $Id: VBoxSharedClipboardSvc-ext.cpp 115134 2026-08-27 15:09:45Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Service extension bridge handling.
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
#include <VBox/log.h>
#include <VBox/vmm/vmmr3vtable.h> /* must be included before hgcmsvc.h */

#include <iprt/errcore.h>
#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/string.h>

#include "VBoxSharedClipboardSvc-internal.h"
#include "VBoxSharedClipboardSvc-transfers.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int shClSvcExtCall(uint32_t u32Function, void *pvParms, uint32_t cbParms);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static int  shClSvcExtTransferCallbackEnter(void);
static void shClSvcExtTransferCallbackLeave(void);
#endif


/**
 * Stores a service-client transport in a service-extension parameter block.
 *
 * @param   pParms             Parameter block to update.
 * @param   pClient            Service-owned client represented by the transport.
 */
static void shClSvcExtSetClient(PSHCLEXTPARMS pParms, PSHCLCLIENT pClient)
{
    SHCLTRANSPORT Transport;
    shClSvcCreateTransport(pClient, &Transport);
    ShClSvcExtSetTransport(pParms, &Transport);
}


/**
 * Calls the registered Main service extension.
 *
 * @returns VBox status code returned by the extension.
 * @retval  VERR_NOT_SUPPORTED if no extension is registered.
 * @param   u32Function        VBOX_CLIPBOARD_EXT_FN_XXX function number.
 * @param   pvParms            Function parameters.  Optional when @a cbParms is zero.
 * @param   cbParms            Size of the function parameters in bytes.
 *
 * @thread  The caller must ensure that the extension remains registered for the
 *          duration of the call.
 */
static int shClSvcExtCall(uint32_t u32Function, void *pvParms, uint32_t cbParms)
{
    LogFlowFunc(("u32Function=%RU32, pvParms=%p, cbParms=%RU32\n", u32Function, pvParms, cbParms));

    int rc;
    if (g_ShClSvc.ExtState.pfnExtension)
        rc = g_ShClSvc.ExtState.pfnExtension(g_ShClSvc.ExtState.pvExtension, u32Function, pvParms, cbParms);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}


/** Initializes service-extension delivery state. */
int shClSvcExtInit(void)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_ZERO(g_ShClSvc.ExtState.CritSectTransferCallbacks);
    return RTCritSectInit(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
#else
    return VINF_SUCCESS;
#endif
}


/** Destroys service-extension delivery state. */
int shClSvcExtTerm(void)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClSvcLock();
    bool const fRegistered = g_ShClSvc.ExtState.pfnExtension != NULL;
    shClSvcUnlock();
    AssertReturn(!fRegistered, VERR_WRONG_ORDER);
    return RTCritSectDelete(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
#else
    return VINF_SUCCESS;
#endif
}


/**
 * Checks whether a Main service extension is registered.
 *
 * @returns true if an extension is registered, false otherwise.
 */
bool shClSvcExtIsRegistered(void)
{
    shClSvcLock();
    bool const fRegistered = g_ShClSvc.ExtState.pfnExtension != NULL;
    shClSvcUnlock();
    return fRegistered;
}


/**
 * Requests process-wide native backend initialization from Main.
 *
 * @returns VBox status code returned by Main.
 */
int shClSvcExtBackendInit(void)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT, &parms, sizeof(parms));
}


/**
 * Connects a service client to the native backend owned by Main.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client to connect.
 */
int shClSvcExtBackendConnect(PSHCLCLIENT pClient)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT, &parms, sizeof(parms));
}


/**
 * Synchronizes a connected service client with the native backend.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Connected service client to synchronize.
 */
int shClSvcExtBackendSync(PSHCLCLIENT pClient)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC, &parms, sizeof(parms));
}


/**
 * Disconnects a service client from the native backend.
 *
 * @param   pClient            Connected service client to disconnect.
 *
 * @note    The service-extension callback is synchronous.  On return, Main no
 *          longer retains the service transport for this client.
 */
void shClSvcExtBackendDisconnect(PSHCLCLIENT pClient)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* The active-client pointer was cleared before this call.  Fence a direct
     * transfer callback admitted just before that change.  Do not hold the
     * serializer across BACKEND_DISCONNECT: native teardown may wait for a data
     * plane thread which is itself reporting a terminal status. */
    int const rcCritSect = RTCritSectEnter(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
    AssertRC(rcCritSect);
    if (RT_SUCCESS(rcCritSect))
    {
        int const rcLeave = RTCritSectLeave(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
        AssertRC(rcLeave);
    }
#endif

    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);

    shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT, &parms, sizeof(parms));
}


/**
 * Requests process-wide native backend destruction from Main.
 */
void shClSvcExtBackendDestroy(void)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY, &parms, sizeof(parms));
}


/**
 * Reports a Shared Clipboard error to Main.
 *
 * @returns VBox status code returned by Main.
 * @param   pszId              Error identifier.  Must remain valid for the call.
 * @param   pszMsg             Human-readable error text.  Must remain valid for the call.
 * @param   rcError            VBox status code describing the error.
 */
int shClSvcExtReportError(char *pszId, char *pszMsg, int rcError)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    parms.u.Error.pszId  = pszId;
    parms.u.Error.rc     = rcError;
    parms.u.Error.pszMsg = pszMsg;

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_ERROR, &parms, sizeof(parms));
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Queries Main for the native callbacks to attach to a transfer.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client owning the transfer.
 * @param   pCallbacks         Where to return the callback table.  The table is
 *                             cleared before calling Main.
 */
int shClSvcExtQueryTransferCallbacks(PSHCLCLIENT pClient, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pCallbacks, VERR_INVALID_POINTER);

    RT_ZERO(*pCallbacks);

    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.TransferCallbacks.pClient    = pClient;
    parms.u.TransferCallbacks.pCallbacks = pCallbacks;

    int rc = shClSvcExtTransferCallbackEnter();
    if (RT_SUCCESS(rc))
    {
        shClSvcLock();
        bool const fCall =    g_ShClSvc.ExtState.pfnExtension
                           && g_ShClSvc.pActiveClient == pClient;
        shClSvcUnlock();

        if (fCall)
            rc = shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS, &parms, sizeof(parms));
        else
            rc = VERR_NOT_SUPPORTED;
        shClSvcExtTransferCallbackLeave();
    }
    return rc;
}


/** Checks that a captured status still belongs to the exact registered transfer. */
static bool shClSvcExtTransferStatusIsCurrentLocked(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus)
{
    Assert(RTCritSectIsOwner(&g_ShClSvc.CritSect));
    AssertPtrReturn(pClient, false);
    AssertPtrReturn(pStatus, false);

    PSHCLTRANSFER const pTransfer
        = ShClTransferCtxGetTransferByKeyRetained(&pClient->Transfers.Ctx, &pStatus->Key);
    if (!pTransfer)
        return false;

    SHCLTRANSFERSTATUS const enmCurrentStatus = ShClTransferGetStatus(pTransfer);
    bool const fCurrent =    ShClTransferGetDir(pTransfer) == pStatus->enmDir
                          && ShClTransferGetSource(pTransfer) == pStatus->enmTransferSource
                          && (   ShClTransferStatusIsTerminal(pStatus->enmStatus)
                              || !ShClTransferStatusIsTerminal(enmCurrentStatus));
    ShClTransferRelease(pTransfer);
    return fCurrent;
}


/** Checks that captured progress still belongs to the exact active transfer. */
static bool shClSvcExtTransferProgressIsCurrentLocked(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERPROGRESS pProgress)
{
    Assert(RTCritSectIsOwner(&g_ShClSvc.CritSect));
    AssertPtrReturn(pClient, false);
    AssertPtrReturn(pProgress, false);

    PSHCLTRANSFER const pTransfer
        = ShClTransferCtxGetTransferByKeyRetained(&pClient->Transfers.Ctx, &pProgress->Key);
    if (!pTransfer)
        return false;

    SHCLTRANSFERSTATUS const enmStatus = ShClTransferGetStatus(pTransfer);
    bool const fCurrent =    enmStatus == SHCLTRANSFERSTATUS_INITIALIZED
                          || enmStatus == SHCLTRANSFERSTATUS_STARTED;
    ShClTransferRelease(pTransfer);
    return fCurrent;
}


/** Enters the direct transfer-callback serializer without any service-side lock held. */
static int shClSvcExtTransferCallbackEnter(void)
{
    AssertReturn(!RTCritSectIsOwner(&g_ShClSvc.CritSect), VERR_WRONG_ORDER);
    return RTCritSectEnter(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
}


/** Leaves the direct transfer-callback serializer. */
static void shClSvcExtTransferCallbackLeave(void)
{
    int const rc = RTCritSectLeave(&g_ShClSvc.ExtState.CritSectTransferCallbacks);
    AssertRC(rc);
}


/**
 * Directly resets Main transfer state while the transfer callback serializer is locked.
 *
 * @returns VBox status code.
 * @param   pClient            Service client whose Main transfer state is reset.
 */
static int shClSvcExtNotifyTransferResetLocked(PSHCLCLIENT pClient)
{
    Assert(RTCritSectIsOwner(&g_ShClSvc.ExtState.CritSectTransferCallbacks));
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcCreateTransport(pClient, &parms.u.FileTransferReset.Transport);

    int rc = VERR_NOT_SUPPORTED;
    shClSvcLock();
    if (   g_ShClSvc.ExtState.pfnExtension
        && g_ShClSvc.pActiveClient == pClient)
        rc = VINF_SUCCESS;
    shClSvcUnlock();

    if (RT_SUCCESS(rc))
        rc = shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET, &parms, sizeof(parms));
    return rc;
}


/**
 * Directly reports an immutable transfer status to Main.
 *
 * The callback serializer preserves lifecycle order across service and native
 * producers.  Main only records the snapshot here; its own COM-MTA worker
 * publishes progress and API events after this callback returns.
 *
 * @retval  VINF_NO_CHANGE          if the status no longer belongs to the current transfer.
 * @retval  VERR_INVALID_POINTER    if @a pClient or @a pStatus is NULL.
 * @retval  VERR_INVALID_PARAMETER  if the status snapshot is invalid.
 * @retval  VERR_WRONG_ORDER        if the service lock is held by the caller.
 * @retval  VERR_NOT_SUPPORTED      if Main is not connected to this service client.
 * @returns                         Status returned by the registered Main extension callback.
 * @param   pClient                 Service client owning the transfer.
 * @param   pStatus                 Immutable status captured while the transfer was valid.
 * @param   fDetachedTerminal       Whether the terminal transfer is already detached.
 */
static int shClSvcExtNotifyTransferStatusEx(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus,
                                            bool fDetachedTerminal)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pStatus, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferKeyIsValid(&pStatus->Key), VERR_INVALID_PARAMETER);
    AssertReturn(ShClTransferDirIsValid(pStatus->enmDir), VERR_INVALID_PARAMETER);
    AssertReturn(ShClSourceIsValid(pStatus->enmTransferSource), VERR_INVALID_PARAMETER);
    AssertReturn(ShClSourceIsValid(pStatus->enmReplySource), VERR_INVALID_PARAMETER);
    AssertReturn(   (   pStatus->enmTransferSource == SHCLSOURCE_REMOTE
                     && pStatus->enmDir == SHCLTRANSFERDIR_GUEST_TO_HOST)
                 || (   pStatus->enmTransferSource == SHCLSOURCE_LOCAL
                     && pStatus->enmDir == SHCLTRANSFERDIR_HOST_TO_GUEST), VERR_INVALID_PARAMETER);
    AssertReturn(   pStatus->enmStatus != SHCLTRANSFERSTATUS_NONE
                 && ShClTransferStatusResultIsValid(pStatus->enmStatus, pStatus->rcStatus), VERR_INVALID_PARAMETER);

    bool const fTerminal = ShClTransferStatusIsTerminal(pStatus->enmStatus);
    AssertReturn(!fDetachedTerminal || fTerminal, VERR_INVALID_PARAMETER);

    int rc = shClSvcExtTransferCallbackEnter();
    if (RT_FAILURE(rc))
        return rc;

    bool fDeliver = false;
    shClSvcLock();
    if (   g_ShClSvc.ExtState.pfnExtension
        && g_ShClSvc.pActiveClient == pClient)
    {
        ShClSvcClientLock(pClient);
        fDeliver =    pClient->State.uSessionID == ShClTransferKeyGetSessionId(&pStatus->Key)
                   && (   fDetachedTerminal
                       || shClSvcExtTransferStatusIsCurrentLocked(pClient, pStatus));
        rc = fDeliver ? VINF_SUCCESS : VINF_NO_CHANGE;
        ShClSvcClientUnlock(pClient);
    }
    else
        rc = VERR_NOT_SUPPORTED;
    shClSvcUnlock();

    if (fDeliver)
    {
        SHCLEXTPARMS parms;
        RT_ZERO(parms);
        shClSvcCreateTransport(pClient, &parms.u.FileTransferData.Transport);
        parms.u.FileTransferData.Key                = pStatus->Key;
        parms.u.FileTransferData.enmDir             = pStatus->enmDir;
        parms.u.FileTransferData.enmTransferSource  = pStatus->enmTransferSource;
        parms.u.FileTransferData.enmReplySource     = pStatus->enmReplySource;
        parms.u.FileTransferData.enmStatus          = pStatus->enmStatus;
        parms.u.FileTransferData.rcStatus           = pStatus->rcStatus;
        parms.u.FileTransferData.pszPath            = pStatus->szPath[0] ? pStatus->szPath : NULL;

        rc = shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER, &parms, sizeof(parms));
        if (   RT_FAILURE(rc)
            && fTerminal)
            shClSvcExtNotifyTransferResetLocked(pClient);
    }

    shClSvcExtTransferCallbackLeave();
    return rc;
}


/** Directly reports a status while its exact transfer remains registered. */
int shClSvcExtNotifyTransferStatus(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus)
{
    return shClSvcExtNotifyTransferStatusEx(pClient, pStatus, false /* fDetachedTerminal */);
}


/** Directly reports a trusted terminal status from an already detached transfer. */
int shClSvcExtNotifyTransferDetachedStatus(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERSTATUS pStatus)
{
    return shClSvcExtNotifyTransferStatusEx(pClient, pStatus, true /* fDetachedTerminal */);
}


/**
 * Directly reports immutable exact aggregate transfer progress to Main.
 *
 * Native accounting already limits this to the first positive snapshot and one
 * snapshot per changed integer percentage, so no service-side queue or
 * coalescing is needed.
 *
 * @returns VBox status code.
 * @param   pClient            Service client owning the transfer.
 * @param   pProgress          Immutable exact aggregate progress snapshot.
 */
int shClSvcExtNotifyTransferProgress(PSHCLCLIENT pClient, PCSHCLSVCEXTTRANSFERPROGRESS pProgress)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pProgress, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferKeyIsValid(&pProgress->Key), VERR_INVALID_PARAMETER);
    AssertReturn(pProgress->cbTotal > 0, VERR_INVALID_PARAMETER);
    AssertReturn(pProgress->cbProcessed <= pProgress->cbTotal, VERR_INVALID_PARAMETER);

    int rc = shClSvcExtTransferCallbackEnter();
    if (RT_FAILURE(rc))
        return rc;

    bool fDeliver = false;
    shClSvcLock();
    if (   g_ShClSvc.ExtState.pfnExtension
        && g_ShClSvc.pActiveClient == pClient)
    {
        ShClSvcClientLock(pClient);
        fDeliver =    pClient->State.uSessionID == ShClTransferKeyGetSessionId(&pProgress->Key)
                   && shClSvcExtTransferProgressIsCurrentLocked(pClient, pProgress);
        rc = fDeliver ? VINF_SUCCESS : VINF_NO_CHANGE;
        ShClSvcClientUnlock(pClient);
    }
    else
        rc = VERR_NOT_SUPPORTED;
    shClSvcUnlock();

    if (fDeliver)
    {
        SHCLEXTPARMS parms;
        RT_ZERO(parms);
        shClSvcCreateTransport(pClient, &parms.u.FileTransferProgress.Transport);
        parms.u.FileTransferProgress.Key         = pProgress->Key;
        parms.u.FileTransferProgress.cbProcessed = pProgress->cbProcessed;
        parms.u.FileTransferProgress.cbTotal     = pProgress->cbTotal;
        rc = shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS, &parms, sizeof(parms));
    }

    shClSvcExtTransferCallbackLeave();
    return rc;
}


/**
 * Directly resets all Main transfer state for a service client.
 *
 * @returns VBox status code.
 * @param   pClient            Service client whose Main transfer state is reset.
 *
 * @thread  The caller must not hold a service, client or transfer lock.
 */
int shClSvcExtNotifyTransferReset(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    int rc = shClSvcExtTransferCallbackEnter();
    if (RT_SUCCESS(rc))
    {
        rc = shClSvcExtNotifyTransferResetLocked(pClient);
        shClSvcExtTransferCallbackLeave();
    }
    return rc;
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Reports guest clipboard formats to Main.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client reporting the formats.
 * @param   fFormats           Guest formats, VBOX_SHCL_FMT_XXX.
 */
int shClSvcExtReportFormatsToHost(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.ReportFormats.uFormats  = fFormats;
    parms.u.ReportFormats.pClient   = pClient;
    parms.u.ReportFormats.enmSource = SHCLSOURCE_INVALID;

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST, &parms, sizeof(parms));
}


/**
 * Reads native clipboard data through Main.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client requesting the data.
 * @param   uFormat            Clipboard format to read.
 * @param   pvData             Destination buffer.  Optional if @a cbData is zero.
 * @param   cbData             Destination buffer size in bytes.
 * @param   pcbActual          Where to return the actual or required size.  Optional.
 */
int shClSvcExtReadData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.ReadWriteData.uFormat  = uFormat;
    parms.u.ReadWriteData.pvData   = pvData;
    parms.u.ReadWriteData.cbData   = cbData;
    parms.u.ReadWriteData.cbActual = pcbActual ? *pcbActual : 0;
    parms.u.ReadWriteData.pClient  = pClient;

    /* Read clipboard data from the extension. */
    int rc = shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_DATA_READ, &parms, sizeof(parms));
    if (   RT_SUCCESS(rc)
        && pcbActual)
        *pcbActual = parms.u.ReadWriteData.cbActual;

    return rc;
}


/**
 * Writes guest clipboard data through Main.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client supplying the data.
 * @param   uFormat            Clipboard format of the data.
 * @param   pvData             Data buffer.  Optional if @a cbData is zero.
 * @param   cbData             Data size in bytes.
 */
int shClSvcExtWriteData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.ReadWriteData.uFormat     = uFormat;
    parms.u.ReadWriteData.pvData      = pvData;
    parms.u.ReadWriteData.cbData      = cbData;
    parms.u.ReadWriteData.pClient     = pClient;

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_DATA_WRITE, &parms, sizeof(parms));
}


/**
 * Unregisters the Main service extension, then tears down its backend.
 *
 * @returns VBox status code.
 */
int shClSvcExtUnregisterAndDestroy(void)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    int rc = shClSvcExtTransferCallbackEnter();
    if (RT_FAILURE(rc))
        return rc;

    shClSvcLock();
    PFNHGCMSVCEXT const pfnExtension = g_ShClSvc.ExtState.pfnExtension;
    void * const pvExtension = g_ShClSvc.ExtState.pvExtension;
    g_ShClSvc.ExtState.pfnExtension = NULL;
    g_ShClSvc.ExtState.pvExtension  = NULL;
    shClSvcUnlock();
    shClSvcExtTransferCallbackLeave();

    if (pfnExtension)
    {
        SHCLEXTPARMS parms;
        RT_ZERO(parms);
        pfnExtension(pvExtension, VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY, &parms, sizeof(parms));
        LogRel2(("Shared Clipboard: de-registered service extension\n"));
    }
    return VINF_SUCCESS;
#else
    shClSvcLock();
    PFNHGCMSVCEXT const pfnExtension = g_ShClSvc.ExtState.pfnExtension;
    void * const pvExtension = g_ShClSvc.ExtState.pvExtension;
    shClSvcUnlock();

    if (!pfnExtension)
        return VINF_SUCCESS;
    shClSvcExtBackendDestroy();

    shClSvcLock();
    if (   g_ShClSvc.ExtState.pfnExtension == pfnExtension
        && g_ShClSvc.ExtState.pvExtension == pvExtension)
    {
        g_ShClSvc.ExtState.pvExtension  = NULL;
        g_ShClSvc.ExtState.pfnExtension = NULL;
    }
    shClSvcUnlock();

    LogRel2(("Shared Clipboard: de-registered service extension\n"));
    return VINF_SUCCESS;
#endif
}


/**
 * Registers or unregisters the Main Shared Clipboard service extension.
 *
 * @returns VBox status code.
 * @param   pvService          HGCM service instance.  Not used.
 * @param   pfnExtension       Extension callback to register, or NULL to unregister.
 * @param   pvExtension        Opaque callback argument owned by Main.
 */
DECLCALLBACK(int) shClSvcRegisterExtension(void *pvService, PFNHGCMSVCEXT pfnExtension, void *pvExtension)
{
    RT_NOREF(pvService);
    LogFlowFunc(("pfnExtension=%p\n", pfnExtension));

    if (pfnExtension)
    {
        shClSvcLock();
        g_ShClSvc.ExtState.pfnExtension = pfnExtension;
        g_ShClSvc.ExtState.pvExtension  = pvExtension;
        shClSvcUnlock();

        LogRel2(("Shared Clipboard: registered service extension\n"));
        return VINF_SUCCESS;
    }

    return shClSvcExtUnregisterAndDestroy();
}
