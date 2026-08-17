/* $Id: ClipboardBackendDarwin.cpp 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Mac OS X host.
 */

/*
 * Copyright (C) 2008-2026 Oracle and/or its affiliates.
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
#include "GuestShClBackend.h"
#include "../GuestShClBackendPrivate.h"
#include "GuestShClConn.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/thread.h>

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
    /** Main connection to the Shared Clipboard service. */
    GuestShClConn          *pConn;
    /** Whether @a pConn may be used by the pasteboard poller. */
    bool                    fClientReady;
    /** Random 64-bit number embedded into szGuestOwnershipFlavor. */
    uint64_t                idGuestOwnership;
    /** Ownership flavor CFStringRef returned by takePasteboardOwnership().
     * This is the same a szGuestOwnershipFlavor only in core foundation terms. */
    void                   *hStrOwnershipFlavor;
    /** The guest ownership flavor (type) string. */
    char                    szGuestOwnershipFlavor[64];
    /** Serialize access to the current pasteboard. */
    RTCRITSECT              CritSectPasteboard;
    /** Serialize the client pointer and its readiness state. */
    RTCRITSECT              CritSect;
} SHCLCONTEXT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Only one client is supported. There seems to be no need for more clients. */
static SHCLCONTEXT g_ctx;


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) shClSvcDarwinTransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pProviderCtx)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pProviderCtx->pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    char  *pszRoots = NULL;
    size_t cbRoots = 0;
    int vrc = RTCritSectEnter(&pCtx->CritSectPasteboard);
    if (RT_SUCCESS(vrc))
    {
        vrc = readFileURLsFromPasteboard(pCtx->hPasteboard, &pszRoots, &cbRoots);

        int const vrc2 = RTCritSectLeave(&pCtx->CritSectPasteboard);
        AssertRC(vrc2);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    if (RT_SUCCESS(vrc))
        vrc = ShClTransferRootsSetFromStringList(pProviderCtx->pTransfer, pszRoots, cbRoots);
    RTStrFree(pszRoots);
    return vrc;
}


/** @copydoc SHCLTRANSFERCALLBACKS::pfnOnCreated */
static DECLCALLBACK(void) shClSvcDarwinTransferOnCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtrReturnVoid(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtrReturnVoid(pTransfer);

    if (   ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_TO_REMOTE
        && ShClTransferGetSource(pTransfer) == SHCLSOURCE_LOCAL)
    {
        SHCLTXPROVIDER Provider;
        RT_ZERO(Provider);
        ShClTransferProviderLocalQueryInterface(&Provider);
        Provider.Interface.pfnRootListRead = shClSvcDarwinTransferIfaceHGRootListRead;
        Provider.enmSource = SHCLSOURCE_LOCAL;
        Provider.pvUser    = pCtx;
        Provider.cbUser    = sizeof(*pCtx);

        int const vrc = ShClTransferSetProvider(pTransfer, &Provider);
        AssertRC(vrc);
    }
}


/** @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize */
static DECLCALLBACK(int) shClSvcDarwinTransferOnInitializeCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    if (   ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_TO_REMOTE
        && ShClTransferGetSource(pTransfer) == SHCLSOURCE_LOCAL)
        return ShClTransferRootListRead(pTransfer);
    return VERR_NOT_SUPPORTED;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Checks if something is present on the clipboard and calls shclSvcReportMsg.
 *
 * @returns IPRT status code (ignored).
 * @param   pCtx    The context.
 * @param   fForce  Whether to report the current pasteboard content even if
 *                  its change was already observed.
 *
 */
static int vboxClipboardChanged(PSHCLCONTEXT pCtx, bool fForce)
{
    int      vrc      = VINF_SUCCESS;
    uint32_t fFormats = 0;

    RTCritSectEnter(&pCtx->CritSect);

    if (   pCtx->pConn
        && pCtx->fClientReady)
    {
        /* Retrieve the formats currently in the clipboard and supported by VBox. */
        bool fChanged = false;
        vrc = RTCritSectEnter(&pCtx->CritSectPasteboard);
        if (RT_SUCCESS(vrc))
        {
            vrc = queryNewPasteboardFormats(pCtx->hPasteboard, pCtx->idGuestOwnership, pCtx->hStrOwnershipFlavor,
                                            fForce, &fFormats, &fChanged);

            int const vrc2 = RTCritSectLeave(&pCtx->CritSectPasteboard);
            AssertRC(vrc2);
            if (RT_SUCCESS(vrc))
                vrc = vrc2;
        }
        if (RT_SUCCESS(vrc) && fChanged)
            vrc = pCtx->pConn->reportLocalFormats(fFormats);
    }

    RTCritSectLeave(&pCtx->CritSect);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @callback_method_impl{FNRTTHREAD, The poller thread.
 *
 * This thread will check for the arrival of new data on the clipboard.}
 */
static DECLCALLBACK(int) vboxClipboardThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvUser;
    AssertPtr(pCtx);
    LogFlowFuncEnter();
    int vrc;

    while (!ASMAtomicReadBool(&pCtx->fTerminate))
    {
        vboxClipboardChanged(pCtx, false /* fForce */);

        /* Sleep for 200 msecs before next poll */
        vrc = RTThreadUserWait(ThreadSelf, 200);
        if (RT_SUCCESS(vrc))
            break;
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}


/**
 * Initializes the process-wide macOS clipboard backend.
 *
 * @returns VBox status code.
 */
static int shClBackendDarwinInit(void)
{
    g_ctx.fTerminate = false;
    g_ctx.fClientReady = false;

    int vrc;

    vrc = RTCritSectInit(&g_ctx.CritSect);
    AssertRCReturn(vrc, vrc);

    vrc = RTCritSectInit(&g_ctx.CritSectPasteboard);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&g_ctx.CritSect);
        return vrc;
    }

    vrc = initPasteboard(&g_ctx.hPasteboard);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&g_ctx.CritSectPasteboard);
        RTCritSectDelete(&g_ctx.CritSect);
        return vrc;
    }

    vrc = RTThreadCreate(&g_ctx.hThread, vboxClipboardThread, &g_ctx, 0,
                         RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SHCLIP");
    if (RT_FAILURE(vrc))
    {
        g_ctx.hThread = NIL_RTTHREAD;
        destroyPasteboard(&g_ctx.hPasteboard);
        RTCritSectDelete(&g_ctx.CritSectPasteboard);
        RTCritSectDelete(&g_ctx.CritSect);
    }

    return vrc;
}

/**
 * Destroys the process-wide macOS clipboard backend.
 */
static void shClBackendDarwinDestroy(void)
{
    /*
     * Signal the termination of the polling thread and wait for it to respond.
     */
    ASMAtomicWriteBool(&g_ctx.fTerminate, true);
    if (g_ctx.hThread != NIL_RTTHREAD)
    {
        int vrc = RTThreadUserSignal(g_ctx.hThread);
        AssertRC(vrc);
        vrc = RTThreadWait(g_ctx.hThread, RT_INDEFINITE_WAIT, NULL);
        AssertFatalMsgRC(vrc, ("Reaping the Darwin clipboard poller failed with %Rrc\n", vrc));
        g_ctx.hThread = NIL_RTTHREAD;
    }

    /*
     * Destroy the hPasteboard and uninitialize the global context record.
     */
    destroyPasteboard(&g_ctx.hPasteboard);
    g_ctx.pConn = NULL;
    g_ctx.fClientReady = false;

    if (RTCritSectIsInitialized(&g_ctx.CritSectPasteboard))
        RTCritSectDelete(&g_ctx.CritSectPasteboard);
    if (RTCritSectIsInitialized(&g_ctx.CritSect))
        RTCritSectDelete(&g_ctx.CritSect);
}

/**
 * Connects a Main service connection to the macOS clipboard backend.
 *
 * @returns VBox status code.
 * @param   pConn               Main service connection to associate.
 * @param   ppCtx               Where to return the backend context.
 */
static int shClBackendDarwinConnect(GuestShClConn *pConn, PSHCLCONTEXT *ppCtx)
{
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(ppCtx,   VERR_INVALID_POINTER);
    *ppCtx = NULL;

    RTCritSectEnter(&g_ctx.CritSect);

    int vrc;
    if (!g_ctx.pConn)
    {
        g_ctx.pConn = pConn;
        g_ctx.fClientReady = false;
        *ppCtx = &g_ctx;
        vrc = VINF_SUCCESS;
    }
    else
        vrc = VERR_NOT_SUPPORTED; /* One client only. */

    RTCritSectLeave(&g_ctx.CritSect);

    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Returns the macOS callbacks for a new transfer.
 *
 * @param   pCtx                Connected backend context.
 * @param   pCallbacks          Where to return the callback table.
 */
static void shClBackendDarwinTransferGetCallbacks(PSHCLCONTEXT pCtx, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    AssertPtrReturnVoid(pCallbacks);
    RT_ZERO(*pCallbacks);
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pCtx->pConn);

    pCallbacks->pvUser          = pCtx;
    pCallbacks->cbUser          = sizeof(*pCtx);
    pCallbacks->pfnOnCreated    = shClSvcDarwinTransferOnCreatedCallback;
    pCallbacks->pfnOnInitialize = shClSvcDarwinTransferOnInitializeCallback;
}
#endif

/**
 * Synchronizes macOS clipboard state with a connected guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 */
static int shClBackendDarwinSync(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx,          VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    /* GuestShClConn publishes the connection while shClBackendDarwinConnect runs, but
     * do not expose it to the poller until the initial service sync. */
    RTCritSectEnter(&g_ctx.CritSect);
    int vrc = VINF_SUCCESS;
    if (pCtx->pConn)
        pCtx->fClientReady = true;
    else
        vrc = VERR_NOT_SUPPORTED;
    RTCritSectLeave(&g_ctx.CritSect);

    /* Sync the host clipboard content with the client. */
    if (RT_SUCCESS(vrc))
        vrc = vboxClipboardChanged(pCtx, true /* fForce */);
    return vrc;
}

/**
 * Disconnects a Main service connection from the macOS clipboard backend.
 *
 * @returns VBox status code.
 * @param   pCtx                Backend context to disconnect.
 */
static int shClBackendDarwinDisconnect(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    GuestShClConn * const pConn = pCtx->pConn;
#endif

    RTCritSectEnter(&g_ctx.CritSect);

    if (pCtx->pConn)
    {
        pCtx->fClientReady = false;
        pCtx->pConn = NULL;
    }

    RTCritSectLeave(&g_ctx.CritSect);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* Transfer callback tables retain pCtx as their user argument. */
    pConn->transferDestroyAll();
#endif

    return VINF_SUCCESS;
}

/**
 * Reports guest clipboard formats to the macOS pasteboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   fFormats            Guest formats, VBOX_SHCL_FMT_XXX.
 */
static int shClBackendDarwinReportFormats(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    AssertPtrReturn(pCtx,          VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    LogFlowFunc(("fFormats=%02X\n", fFormats));

    if (fFormats == VBOX_SHCL_FMT_NONE)
    {
        RTCritSectEnter(&pCtx->CritSectPasteboard);
        int vrcClear = clearPasteboard(pCtx->hPasteboard, &pCtx->hStrOwnershipFlavor);
        RTCritSectLeave(&pCtx->CritSectPasteboard);
        return vrcClear;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        LogRel2(("Shared Clipboard: Darwin backend does not support guest-to-host file-transfer offers yet\n"));
        fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
        if (fFormats == VBOX_SHCL_FMT_NONE)
        {
            RTCritSectEnter(&pCtx->CritSectPasteboard);
            int vrcClear = clearPasteboard(pCtx->hPasteboard, &pCtx->hStrOwnershipFlavor);
            RTCritSectLeave(&pCtx->CritSectPasteboard);
            return vrcClear;
        }
    }
#endif

    RTCritSectEnter(&pCtx->CritSectPasteboard);

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

    RTCritSectLeave(&pCtx->CritSectPasteboard);

    /*
     * Now, request the data from the guest.
     */
    return pCtx->pConn->readDataFromGuestAsync(fFormats, NULL /* ppEvent */);
}

/**
 * Reads clipboard data from the macOS pasteboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   fFormat             Clipboard format to read.
 * @param   pvData              Destination buffer.
 * @param   cbData              Destination buffer size in bytes.
 * @param   pcbActual           Where to return the actual or required byte count.
 */
static int shClBackendDarwinReadData(PSHCLCONTEXT pCtx, SHCLFORMAT fFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    RTCritSectEnter(&pCtx->CritSectPasteboard);

    /* Default to no data available. */
    *pcbActual = 0;

    int vrc = readFromPasteboard(pCtx->hPasteboard, fFormat, pvData, cbData, pcbActual);
    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error reading host clipboard data from macOS, vrc=%Rrc\n", vrc));

    RTCritSectLeave(&pCtx->CritSectPasteboard);

    return vrc;
}

/**
 * Writes guest clipboard data to the macOS pasteboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Connected backend context.
 * @param   fFormat             Clipboard format to write.
 * @param   pvData              Data buffer.
 * @param   cbData              Data size in bytes.
 */
static int shClBackendDarwinWriteData(PSHCLCONTEXT pCtx, SHCLFORMAT fFormat, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pConn, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    RTCritSectEnter(&pCtx->CritSectPasteboard);

    int vrc = writeToPasteboard(pCtx->hPasteboard, pCtx->idGuestOwnership, pvData, cbData, fFormat);

    RTCritSectLeave(&pCtx->CritSectPasteboard);

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Writing guest data to the macOS pasteboard failed, vrc=%Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles transfer status replies from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard backend context.
 * @param   pTransfer           Shared Clipboard transfer.
 * @param   enmSource           Transfer source which issued the reply.
 * @param   enmStatus           Transfer status.
 * @param   rcStatus            Transfer status code.
 */
static int shClBackendDarwinTransferHandleStatusReply(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                         SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_NOREF(pCtx, pTransfer, enmSource, enmStatus, rcStatus);

    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/** Native macOS Shared Clipboard backend operations. */
static SHCLBACKENDOPS const s_ShClBackendDarwinOps =
{
    shClBackendDarwinInit,
    shClBackendDarwinDestroy,
    NULL,
    shClBackendDarwinConnect,
    shClBackendDarwinDisconnect,
    shClBackendDarwinReportFormats,
    shClBackendDarwinReadData,
    shClBackendDarwinWriteData,
    shClBackendDarwinSync,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClBackendDarwinTransferGetCallbacks,
    shClBackendDarwinTransferHandleStatusReply,
#endif
};


/**
 * Returns the native macOS Shared Clipboard backend operations.
 *
 * @returns Immutable macOS backend operation table.
 */
PCSHCLBACKENDOPS ShClBackendGetOps(void)
{
    return &s_ShClBackendDarwinOps;
}
