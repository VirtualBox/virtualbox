/* $Id: VBoxSharedClipboardSvc-ext.cpp 115054 2026-08-17 16:27:08Z andreas.loeffler@oracle.com $ */
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


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int shClSvcExtCall(uint32_t u32Function, void *pvParms, uint32_t cbParms);


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


/**
 * Checks whether a Main service extension is registered.
 *
 * @returns true if an extension is registered, false otherwise.
 */
bool shClSvcExtIsRegistered(void)
{
    return g_ShClSvc.ExtState.pfnExtension != NULL;
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

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS, &parms, sizeof(parms));
}


/**
 * Notifies Main about a transfer status reply.
 *
 * @returns VBox status code returned by Main.
 * @param   pClient            Service client owning the transfer.
 * @param   pTransfer          Transfer whose status changed.
 * @param   enmSource          Endpoint which supplied the reply.
 * @param   pReply             Status reply.  Valid for the duration of the call.
 */
int shClSvcExtNotifyTransferStatus(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                    PSHCLREPLY pReply)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.FileTransferData.pClient       = pClient;
    parms.u.FileTransferData.pTransfer     = pTransfer;
    parms.u.FileTransferData.enmShClSource = enmSource;
    parms.u.FileTransferData.pReply        = pReply;

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER, &parms, sizeof(parms));
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
 * @param   pCmdCtx            Guest command context identifying the reply.
 * @param   uFormat            Clipboard format of the data.
 * @param   pvData             Data buffer.  Optional if @a cbData is zero.
 * @param   cbData             Data size in bytes.
 */
int shClSvcExtWriteData(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);
    shClSvcExtSetClient(&parms, pClient);
    parms.u.ReadWriteData.uFormat     = uFormat;
    parms.u.ReadWriteData.pvData      = pvData;
    parms.u.ReadWriteData.cbData      = cbData;
    parms.u.ReadWriteData.pClient     = pClient;
    parms.u.ReadWriteData.pCmdCtx     = pCmdCtx;

    return shClSvcExtCall(VBOX_CLIPBOARD_EXT_FN_DATA_WRITE, &parms, sizeof(parms));
}


/**
 * Unregisters the Main service extension, then tears down its backend.
 *
 * @returns VBox status code.
 */
int shClSvcExtUnregisterAndDestroy(void)
{
    shClSvcLock();
    PFNHGCMSVCEXT const pfnExtension = g_ShClSvc.ExtState.pfnExtension;
    void * const pvExtension = g_ShClSvc.ExtState.pvExtension;
    shClSvcUnlock();

    if (!pfnExtension)
        return VINF_SUCCESS;

    /* Console unregisters the extension before HGCM disconnects its client. */
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
