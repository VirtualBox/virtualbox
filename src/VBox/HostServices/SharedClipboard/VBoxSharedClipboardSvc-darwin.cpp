/* $Id: VBoxSharedClipboardSvc-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard Service - Mac OS X host.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "VBoxSharedClipboardSvc-internal.h"
#include "darwin-pasteboard.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Global clipboard context information */
typedef struct SHCLCONTEXT
{
    /** We have a separate thread to poll for new clipboard content. */
    RTTHREAD                hThread;
    /** Termination indicator.   */
    bool volatile           fTerminate;
    /** The reference to the current pasteboard */
    PasteboardRef           hPasteboard;
    /** Shared clipboard client. */
    PSHCLCLIENT             pClient;
    /** Random 64-bit number embedded into szGuestOwnershipFlavor. */
    uint64_t                idGuestOwnership;
    /** Ownership flavor CFStringRef returned by takePasteboardOwnership().
     * This is the same a szGuestOwnershipFlavor only in core foundation terms. */
    void                   *hStrOwnershipFlavor;
    /** The guest ownership flavor (type) string. */
    char                    szGuestOwnershipFlavor[64];
} SHCLCONTEXT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Only one client is supported. There seems to be no need for more clients. */
static SHCLCONTEXT g_ctx;


/**
 * Checks if something is present on the clipboard and calls shclSvcReportMsg.
 *
 * @returns IPRT status code (ignored).
 * @param   pCtx    The context.
 *
 * @note    Call must own lock.
 */
static int vboxClipboardChanged(SHCLCONTEXT *pCtx)
{
    if (pCtx->pClient == NULL)
        return VINF_SUCCESS;

    /* Retrieve the formats currently in the clipboard and supported by vbox */
    uint32_t fFormats = 0;
    bool     fChanged = false;
    int rc = queryNewPasteboardFormats(pCtx->hPasteboard, pCtx->idGuestOwnership, pCtx->hStrOwnershipFlavor,
                                       &fFormats, &fChanged);
    if (   RT_SUCCESS(rc)
        && fChanged)
        rc = ShClSvcReportFormats(pCtx->pClient, fFormats);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @callback_method_impl{FNRTTHREAD, The poller thread.
 *
 * This thread will check for the arrival of new data on the clipboard.}
 */
static DECLCALLBACK(int) vboxClipboardThread(RTTHREAD ThreadSelf, void *pvUser)
{
    SHCLCONTEXT *pCtx = (SHCLCONTEXT *)pvUser;
    AssertPtr(pCtx);
    LogFlowFuncEnter();

    while (!pCtx->fTerminate)
    {
        /* call this behind the lock because we don't know if the api is
           thread safe and in any case we're calling several methods. */
        ShClSvcLock();
        vboxClipboardChanged(pCtx);
        ShClSvcUnlock();

        /* Sleep for 200 msecs before next poll */
        RTThreadUserWait(ThreadSelf, 200);
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}


int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable)
{
    RT_NOREF(pBackend, pTable);
    g_ctx.fTerminate = false;

    int rc = initPasteboard(&g_ctx.hPasteboard);
    AssertRCReturn(rc, rc);

    rc = RTThreadCreate(&g_ctx.hThread, vboxClipboardThread, &g_ctx, 0,
                        RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SHCLIP");
    if (RT_FAILURE(rc))
    {
        g_ctx.hThread = NIL_RTTHREAD;
        destroyPasteboard(&g_ctx.hPasteboard);
    }

    return rc;
}

void ShClBackendDestroy(PSHCLBACKEND pBackend)
{
    RT_NOREF(pBackend);

    /*
     * Signal the termination of the polling thread and wait for it to respond.
     */
    ASMAtomicWriteBool(&g_ctx.fTerminate, true);
    int rc = RTThreadUserSignal(g_ctx.hThread);
    AssertRC(rc);
    rc = RTThreadWait(g_ctx.hThread, RT_INDEFINITE_WAIT, NULL);
    AssertRC(rc);

    /*
     * Destroy the hPasteboard and uninitialize the global context record.
     */
    destroyPasteboard(&g_ctx.hPasteboard);
    g_ctx.hThread = NIL_RTTHREAD;
    g_ctx.pClient = NULL;
}

int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, bool fHeadless)
{
    RT_NOREF(pBackend, fHeadless);

    if (g_ctx.pClient != NULL)
    {
        /* One client only. */
        return VERR_NOT_SUPPORTED;
    }

    ShClSvcLock();

    pClient->State.pCtx = &g_ctx;
    pClient->State.pCtx->pClient = pClient;

    ShClSvcUnlock();

    return VINF_SUCCESS;
}

int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    /* Sync the host clipboard content with the client. */
    ShClSvcLock();

    int rc = vboxClipboardChanged(pClient->State.pCtx);

    ShClSvcUnlock();

    return rc;
}

int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    ShClSvcLock();

    pClient->State.pCtx->pClient = NULL;

    ShClSvcUnlock();

    return VINF_SUCCESS;
}

int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    LogFlowFunc(("fFormats=%02X\n", fFormats));

    /** @todo r=bird: BUGBUG: The following is probably a mistake. */
    /** @todo r=andy: BUGBUG: Has been there since forever; needs investigation first before removing. */
    if (fFormats == VBOX_SHCL_FMT_NONE)
    {
        /* This is just an automatism, not a genuine announcement */
        return VINF_SUCCESS;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormats & VBOX_SHCL_FMT_URI_LIST) /* No transfer support yet. */
        return VINF_SUCCESS;
#endif

    SHCLCONTEXT *pCtx = pClient->State.pCtx;
    ShClSvcLock();

    /*
     * Generate a unique flavor string for this format announcement.
     */
    uint64_t idFlavor = RTRandU64();
    pCtx->idGuestOwnership = idFlavor;
    RTStrPrintf(pCtx->szGuestOwnershipFlavor, sizeof(pCtx->szGuestOwnershipFlavor),
                "org.virtualbox.sharedclipboard.%RTproc.%RX64", RTProcSelf(), idFlavor);

    /*
     * Empty the pasteboard and put our ownership indicator flavor there
     * with the stringified formats as value.
     */
    char szValue[32];
    RTStrPrintf(szValue, sizeof(szValue), "%#x", fFormats);

    takePasteboardOwnership(pCtx->hPasteboard, pCtx->idGuestOwnership, pCtx->szGuestOwnershipFlavor, szValue,
                            &pCtx->hStrOwnershipFlavor);

    ShClSvcUnlock();

    /*
     * Now, request the data from the guest.
     */
    return ShClSvcReadDataFromGuestAsync(pClient, fFormats, NULL /* ppEvent */);
}

int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT fFormat,
                        void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(pClient,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCmdCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    RT_NOREF(pBackend, pCmdCtx);

    ShClSvcLock();

    /* Default to no data available. */
    *pcbActual = 0;

    int rc = readFromPasteboard(pClient->State.pCtx->hPasteboard, fFormat, pvData, cbData, pcbActual);
    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Error reading host clipboard data from macOS, rc=%Rrc\n", rc));

    ShClSvcUnlock();

    return rc;
}

int ShClBackendWriteData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT fFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pBackend, pCmdCtx);

    LogFlowFuncEnter();

    ShClSvcLock();

    writeToPasteboard(pClient->State.pCtx->hPasteboard, pClient->State.pCtx->idGuestOwnership, pvData, cbData, fFormat);

    ShClSvcUnlock();

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS

int ShClBackendTransferReadDir(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLDIRDATA pDirData)
{
    RT_NOREF(pBackend, pClient, pDirData);
    return VERR_NOT_IMPLEMENTED;
}

int ShClBackendTransferWriteDir(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLDIRDATA pDirData)
{
    RT_NOREF(pBackend, pClient, pDirData);
    return VERR_NOT_IMPLEMENTED;
}

int ShClBackendTransferReadFileHdr(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLFILEHDR pFileHdr)
{
    RT_NOREF(pBackend, pClient, pFileHdr);
    return VERR_NOT_IMPLEMENTED;
}

int ShClBackendTransferWriteFileHdr(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLFILEHDR pFileHdr)
{
    RT_NOREF(pBackend, pClient, pFileHdr);
    return VERR_NOT_IMPLEMENTED;
}

int ShClBackendTransferReadFileData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLFILEDATA pFileData)
{
    RT_NOREF(pBackend, pClient, pFileData);
    return VERR_NOT_IMPLEMENTED;
}

int ShClBackendTransferWriteFileData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLFILEDATA pFileData)
{
    RT_NOREF(pBackend, pClient, pFileData);
    return VERR_NOT_IMPLEMENTED;
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

