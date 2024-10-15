/** $Id: clipboard-x11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - X11 Shared Clipboard implementation.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#include "VBoxClient.h"
#include "clipboard.h"

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/log.h>


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
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
                rc = ShClTransferRootsSetFromStringListEx(pTransfer, (const char *)pvData, cbData,
                                                          "\n" /* X11-based Desktop environments separate entries with "\n" */);
                RTMemFree(pvData);
            }
            break;
        }

        case SHCLTRANSFERDIR_FROM_REMOTE: /* H->G */
        {
            /* Retrieve the root entries as a first action, so that the transfer is ready to go
             * once it gets registered to HTTP server. */
            int rc2 = ShClTransferRootListRead(pTransfer);
            if (   RT_SUCCESS(rc2)
                /* As soon as we register the transfer with the HTTP server, the transfer needs to have its roots set. */
                && ShClTransferRootsCount(pTransfer))
            {
                rc2 = ShClTransferHttpServerRegisterTransfer(&pCtx->X11.HttpCtx.HttpServer, pTransfer);
            }
            break;
        }

        default:
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnRegistered
 *
 * This starts the HTTP server if not done yet and registers the transfer with it.
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
        int rc2 = ShClTransferHttpServerMaybeStart(&pCtx->X11.HttpCtx);
        if (RT_FAILURE(rc2))
            LogRel(("Shared Clipboard: Registering HTTP transfer failed: %Rrc\n", rc2));
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
    vbclX11TransferUnregister((PSHCLCONTEXT)pCbCtx->pvUser, pCbCtx->pTransfer);
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
    RT_NOREF(rc);
    vbclX11TransferUnregister((PSHCLCONTEXT)pCbCtx->pvUser, pCbCtx->pTransfer);
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

    int rc;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    if (uFmt == VBOX_SHCL_FMT_URI_LIST)
    {
        rc = vbclX11ReadDataWorker(pCtx, uFmt, ppv, pcb, pvUser);
        if (RT_SUCCESS(rc))
        {
            /* Request a new H->G transfer from the host.
             * This is needed in order to get a transfer ID from the host we can initialize our own local transfer with.
             * Transfer creation and set up will be done in VbglR3. */
            rc = VbglR3ClipboardTransferRequest(&pCtx->CmdCtx);
            if (RT_SUCCESS(rc))
            {
                PSHCLHTTPSERVER pSrv = &pCtx->X11.HttpCtx.HttpServer;

                /* Wait until the HTTP server got the transfer registered, so that we have something to work with. */
                rc = ShClTransferHttpServerWaitForStatusChange(pSrv, SHCLHTTPSERVERSTATUS_TRANSFER_REGISTERED, SHCL_TIMEOUT_DEFAULT_MS);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferHttpServerGetTransferLast(pSrv);
                    if (pTransfer)
                    {
                        rc = ShClTransferWaitForStatus(pTransfer, SHCL_TIMEOUT_DEFAULT_MS, SHCLTRANSFERSTATUS_INITIALIZED);
                        if (RT_SUCCESS(rc))
                        {
                            char  *pszData;
                            size_t cbData;
                            rc = ShClTransferHttpConvertToStringList(pSrv, pTransfer, &pszData, &cbData);
                            if (RT_SUCCESS(rc))
                            {
                                *ppv = pszData;
                                *pcb = cbData;
                                /* ppv has ownership of pszData now. */
                            }
                        }
                    }
                    else
                        AssertMsgFailed(("No registered transfer found for HTTP server\n"));
                }
                else
                    LogRel(("Shared Clipboard: Could not start transfer, as no new HTTP transfer was registered in time\n"));
            }
        }
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

    int rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fFormats);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes the X11-specifc Shared Clipboard code.
 *
 * @returns VBox status code.
 */
int VBClX11ClipboardInit(void)
{
    LogFlowFuncEnter();

    int rc = ShClEventSourceCreate(&g_Ctx.EventSrc, 0 /* uID */);
    AssertRCReturn(rc, rc);

    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats           = vbclX11ReportFormatsCallback;
    Callbacks.pfnOnRequestDataFromSource = vbclX11OnRequestDataFromSourceCallback;

    rc = ShClX11Init(&g_Ctx.X11, &Callbacks, &g_Ctx, false /* fHeadless */);
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
        ShClX11Destroy(&g_Ctx.X11);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys the X11-specifc Shared Clipboard code.
 *
 * @returns VBox status code.
 */
int VBClX11ClipboardDestroy(void)
{
    return ShClEventSourceDestroy(&g_Ctx.EventSrc);
}

/**
 * The main loop of the X11-specifc Shared Clipboard code.
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
                    ShClX11ReportFormatsToX11Async(&g_Ctx.X11, pEvent->u.fReportedFormats);
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
