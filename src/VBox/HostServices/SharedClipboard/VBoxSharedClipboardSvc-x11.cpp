/* $Id: VBoxSharedClipboardSvc-x11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard Service - Linux host.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <iprt/errcore.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
#  include <VBox/GuestHost/SharedClipboard-transfers.h>
# endif
#endif

#include "VBoxSharedClipboardSvc-internal.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif

/* Number of currently extablished connections. */
static volatile uint32_t g_cShClConnections;


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
    /** Pointer to the VBox host client data structure. */
    PSHCLCLIENT          pClient;
    /** We set this when we start shutting down as a hint not to post any new
     * requests. */
    bool                 fShuttingDown;
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
static DECLCALLBACK(void) shClSvcX11TransferOnUnregisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx);

static DECLCALLBACK(int) shClSvcX11TransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx);
#endif


/*********************************************************************************************************************************
*   Backend implementation                                                                                                       *
*********************************************************************************************************************************/
int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable)
{
    RT_NOREF(pBackend);

    LogFlowFuncEnter();

    /* Override the connection limit. */
    for (uintptr_t i = 0; i < RT_ELEMENTS(pTable->acMaxClients); i++)
        pTable->acMaxClients[i] = RT_MIN(VBOX_SHARED_CLIPBOARD_X11_CONNECTIONS_MAX, pTable->acMaxClients[i]);

    RT_ZERO(pBackend->Callbacks);
    /* Use internal callbacks by default. */
    pBackend->Callbacks.pfnReportFormats           = shClSvcX11ReportFormatsCallback;
    pBackend->Callbacks.pfnOnRequestDataFromSource = shClSvcX11RequestDataFromSourceCallback;

    return VINF_SUCCESS;
}

void ShClBackendDestroy(PSHCLBACKEND pBackend)
{
    RT_NOREF(pBackend);

    LogFlowFuncEnter();
}

void ShClBackendSetCallbacks(PSHCLBACKEND pBackend, PSHCLCALLBACKS pCallbacks)
{
#define SET_FN_IF_NOT_NULL(a_Fn) \
    if (pCallbacks->pfn##a_Fn) \
        pBackend->Callbacks.pfn##a_Fn = pCallbacks->pfn##a_Fn;

    SET_FN_IF_NOT_NULL(ReportFormats);
    SET_FN_IF_NOT_NULL(OnRequestDataFromSource);

#undef SET_FN_IF_NOT_NULL
}

/**
 * @note  On the host, we assume that some other application already owns
 *        the clipboard and leave ownership to X11.
 */
int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, bool fHeadless)
{
    int rc;

    /* Check if maximum allowed connections count has reached. */
    if (ASMAtomicIncU32(&g_cShClConnections) > VBOX_SHARED_CLIPBOARD_X11_CONNECTIONS_MAX)
    {
        ASMAtomicDecU32(&g_cShClConnections);
        LogRel(("Shared Clipboard: maximum amount for client connections reached\n"));
        return VERR_OUT_OF_RESOURCES;
    }

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    if (pCtx)
    {
        rc = RTCritSectInit(&pCtx->CritSect);
        if (RT_SUCCESS(rc))
        {
            rc = ShClX11Init(&pCtx->X11, &pBackend->Callbacks, pCtx, fHeadless);
            if (RT_SUCCESS(rc))
            {
                pClient->State.pCtx = pCtx;
                pCtx->pClient = pClient;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                /*
                 * Set callbacks.
                 * Those will be registered within ShClSvcTransferInit() when a new transfer gets initialized.
                 *
                 * Used for starting / stopping the HTTP server.
                 */
                RT_ZERO(pClient->Transfers.Callbacks);

                pClient->Transfers.Callbacks.pvUser = pCtx; /* Assign context as user-provided callback data. */
                pClient->Transfers.Callbacks.cbUser = sizeof(SHCLCONTEXT);

                pClient->Transfers.Callbacks.pfnOnCreated      = shClSvcX11TransferOnCreatedCallback;
                pClient->Transfers.Callbacks.pfnOnInitialize   = shClSvcX11TransferOnInitCallback;
                pClient->Transfers.Callbacks.pfnOnDestroy      = shClSvcX11TransferOnDestroyCallback;
                pClient->Transfers.Callbacks.pfnOnUnregistered = shClSvcX11TransferOnUnregisteredCallback;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

                rc = ShClX11ThreadStart(&pCtx->X11, true /* grab shared clipboard */);
                if (RT_FAILURE(rc))
                    ShClX11Destroy(&pCtx->X11);
            }

            if (RT_FAILURE(rc))
                RTCritSectDelete(&pCtx->CritSect);
        }

        if (RT_FAILURE(rc))
        {
            pClient->State.pCtx = NULL;
            RTMemFree(pCtx);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
    {
        /* Restore active connections count. */
        ASMAtomicDecU32(&g_cShClConnections);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    LogFlowFuncEnter();

    /* Tell the guest we have no data in case X11 is not available.  If
     * there is data in the host clipboard it will automatically be sent to
     * the guest when the clipboard starts up. */
    return ShClSvcReportFormats(pClient, VBOX_SHCL_FMT_NONE);
}

/**
 * Shuts down the shared clipboard service and "disconnect" the guest.
 * Note!  Host glue code
 */
int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = pClient->State.pCtx;
    AssertPtr(pCtx);

    /* Drop the reference to the client, in case it is still there.  This
     * will cause any outstanding clipboard data requests from X11 to fail
     * immediately. */
    pCtx->fShuttingDown = true;

    int rc = ShClX11ThreadStop(&pCtx->X11);
    /** @todo handle this slightly more reasonably, or be really sure
     *        it won't go wrong. */
    AssertRC(rc);

    ShClX11Destroy(&pCtx->X11);
    RTCritSectDelete(&pCtx->CritSect);

    RTMemFree(pCtx);

    /* Decrease active connections count. */
    ASMAtomicDecU32(&g_cShClConnections);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reports clipboard formats to the host clipboard.
 */
int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    int rc = ShClX11ReportFormatsToX11Async(&pClient->State.pCtx->X11, fFormats);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads data from the host clipboard.
 *
 * Schedules a request to the X11 event thread.
 *
 * @note   We always fail or complete asynchronously.
 */
int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat,
                        void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    RT_NOREF(pBackend);

    AssertPtrReturn(pClient,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCmdCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    RT_NOREF(pCmdCtx);

    LogFlowFunc(("pClient=%p, uFormat=%#x, pv=%p, cb=%RU32, pcbActual=%p\n",
                 pClient, uFormat, pvData, cbData, pcbActual));

    uint32_t cbRead;
    int rc = ShClX11ReadDataFromX11(&pClient->State.pCtx->X11, &pClient->EventSrc,
                                    SHCL_TIMEOUT_DEFAULT_MS, uFormat, pvData, cbData, &cbRead);
    if (RT_SUCCESS(rc))
    {
        LogRel2(("Shared Clipboard: Read %RU32 bytes host X11 clipboard data\n", cbRead));
        *pcbActual = cbRead;
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Error reading host clipboard data from X11, rc=%Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClBackendWriteData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx,
                         SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pBackend, pClient, pCmdCtx, uFormat, pvData, cbData);

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

    int rc = VINF_SUCCESS;
    PSHCLCLIENT pClient = pCtx->pClient;
    AssertPtr(pClient);

    rc = ShClSvcReportFormats(pClient, fFormats);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCreated
 *
 * @thread Service main thread.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLCLIENT const pClient = pCtx->pClient;
    AssertPtr(pClient);

    /*
     * Set transfer provider.
     * Those will be registered within ShClSvcTransferInit() when a new transfer gets initialized.
     */

    /* Set the interface to the local provider by default first. */
    RT_ZERO(pClient->Transfers.Provider);
    ShClTransferProviderLocalQueryInterface(&pClient->Transfers.Provider);

    PSHCLTXPROVIDERIFACE pIface = &pClient->Transfers.Provider.Interface;

    pClient->Transfers.Provider.enmSource = pClient->State.enmSource;
    pClient->Transfers.Provider.pvUser    = pClient;

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* Guest -> Host. */
        {
            pIface->pfnRootListRead  = ShClSvcTransferIfaceGHRootListRead;

            pIface->pfnListOpen      = ShClSvcTransferIfaceGHListOpen;
            pIface->pfnListClose     = ShClSvcTransferIfaceGHListClose;
            pIface->pfnListHdrRead   = ShClSvcTransferIfaceGHListHdrRead;
            pIface->pfnListEntryRead = ShClSvcTransferIfaceGHListEntryRead;

            pIface->pfnObjOpen       = ShClSvcTransferIfaceGHObjOpen;
            pIface->pfnObjClose      = ShClSvcTransferIfaceGHObjClose;
            pIface->pfnObjRead       = ShClSvcTransferIfaceGHObjRead;
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* Host -> Guest. */
        {
            pIface->pfnRootListRead  = shClSvcX11TransferIfaceHGRootListRead;
            break;
        }

        default:
            AssertFailed();
    }

    int rc = ShClTransferSetProvider(pTransfer, &pClient->Transfers.Provider); RT_NOREF(rc);

    LogFlowFuncLeaveRC(rc);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize
 *
 * For G->H: Starts the HTTP server if not done yet and registers the transfer with it.
 * For H->G: Called on transfer intialization to populate the transfer's root list.
 *
 * @thread  Service main thread.
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

    int rc;

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* G->H */
        {
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
            /* We only need to start the HTTP server when we actually receive data from the remote (host). */
            rc = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
# endif
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* H->G */
        {
            rc = ShClTransferRootListRead(pTransfer); /* Calls shClSvcX11TransferIfaceHGRootListRead(). */
            break;
        }

        default:
            AssertFailedStmt(rc = VERR_NOT_SUPPORTED);
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnDestroy
 *
 * This stops the HTTP server if not done yet.
 *
 * @thread Service main thread.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE)
        ShClTransferHttpServerMaybeStop(&pCtx->X11.HttpCtx);
# else
    RT_NOREF(pCbCtx);
# endif

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
static void shClSvcX11HttpTransferUnregister(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer)
{
    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE)
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
 * @thread Clipboard main thread.
 */
static DECLCALLBACK(void) shClSvcX11TransferOnUnregisteredCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, PSHCLTRANSFERCTX pTransferCtx)
{
    RT_NOREF(pTransferCtx);
    shClSvcX11HttpTransferUnregister((PSHCLCONTEXT)pCbCtx->pvUser, pCbCtx->pTransfer);
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

    if (pCtx->fShuttingDown)
    {
        /* The shared clipboard is disconnecting. */
        LogRel(("Shared Clipboard: Host requested guest clipboard data after guest had disconnected\n"));
        return VERR_WRONG_ORDER;
    }

    PSHCLCLIENT const pClient = pCtx->pClient;
    int rc = ShClSvcReadDataFromGuest(pClient, uFmt, ppv, pcb);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Note: We always return a generic URI list (as HTTP links) here.
     *       As we don't know which Atom target format was requested by the caller, the X11 clipboard codes needs
     *       to decide & transform the list into the actual clipboard Atom target format the caller wanted.
     */
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
    {
        PSHCLTRANSFER pTransfer;
        rc = ShClSvcTransferCreate(pClient, SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE,
                                   NIL_SHCLTRANSFERID /* Creates a new transfer ID */, &pTransfer);
        if (RT_SUCCESS(rc))
        {
            /* Initialize the transfer on the host side. */
            rc = ShClSvcTransferInit(pClient, pTransfer);
            if (RT_FAILURE(rc))
                 ShClSvcTransferDestroy(pClient, pTransfer);
        }

        if (RT_SUCCESS(rc))
        {
            /* We have to wait for the guest reporting the transfer as being initialized.
             * Only then we can start reading stuff. */
            rc = ShClTransferWaitForStatus(pTransfer, SHCL_TIMEOUT_DEFAULT_MS, SHCLTRANSFERSTATUS_INITIALIZED);
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferRootListRead(pTransfer);
                if (RT_SUCCESS(rc))
                {
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                    /* As soon as we register the transfer with the HTTP server, the transfer needs to have its roots set. */
                    PSHCLHTTPSERVER const pHttpSrv = &pCtx->X11.HttpCtx.HttpServer;
                    rc = ShClTransferHttpServerRegisterTransfer(pHttpSrv, pTransfer);
                    if (RT_SUCCESS(rc))
                    {
                        char  *pszData;
                        size_t cbData;
                        rc = ShClTransferHttpConvertToStringList(pHttpSrv, pTransfer, &pszData, &cbData);
                        if (RT_SUCCESS(rc))
                        {
                            *ppv = pszData;
                            *pcb = cbData;
                            /* ppv has ownership of pszData now. */
                        }
                    }
# else
                    rc = VERR_NOT_SUPPORTED;
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */
                }
            }
        }
    }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Requesting X11 data in format %#x from guest failed with %Rrc\n", uFmt, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles transfer status replies from the guest.
 */
int ShClBackendTransferHandleStatusReply(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_NOREF(pBackend, pClient, enmSource, rcStatus);

    PSHCLCONTEXT pCtx = pClient->State.pCtx; RT_NOREF(pCtx);

    if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_FROM_REMOTE) /* Guest -> Host */
    {
        switch (enmStatus)
        {
            case SHCLTRANSFERSTATUS_INITIALIZED:
            {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
                int rc2 = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
                if (RT_SUCCESS(rc2))
                {

                }

                if (RT_FAILURE(rc2))
                    LogRel(("Shared Clipboard: Registering HTTP transfer failed: %Rrc\n", rc2));
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */
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

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    AssertPtr(pClient->State.pCtx);
    PSHCLX11CTX pX11 = &pClient->State.pCtx->X11;

    /* X supplies the data asynchronously, so we need to wait for data to arrive first. */
    void    *pvData;
    uint32_t cbData;
    int rc = ShClX11ReadDataFromX11Ex(pX11, &pClient->EventSrc, SHCL_TIMEOUT_DEFAULT_MS, VBOX_SHCL_FMT_URI_LIST,
                                      &pvData, &cbData);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferRootsSetFromStringList(pCtx->pTransfer, (const char *)pvData, cbData);
        if (RT_SUCCESS(rc))
            LogRel2(("Shared Clipboard: Host reported %RU64 X11 root entries for transfer to guest\n",
                     ShClTransferRootsCount(pCtx->pTransfer)));

        RTMemFree(pvData);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

