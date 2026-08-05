/* $Id: ClipboardBackendDarwin.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
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
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "darwin-pasteboard.h"
#ifdef VBOX_COM_INPROC
# include "GuestShClPrivate.h"
#endif


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
    /** Whether @a pClient may be used by the pasteboard poller. */
    bool                    fClientReady;
    /** Random 64-bit number embedded into szGuestOwnershipFlavor. */
    uint64_t                idGuestOwnership;
    /** Ownership flavor CFStringRef returned by takePasteboardOwnership().
     * This is the same a szGuestOwnershipFlavor only in core foundation terms. */
    void                   *hStrOwnershipFlavor;
    /** The guest ownership flavor (type) string. */
    char                    szGuestOwnershipFlavor[64];
    /** Serialize access to the current pasteboard. */
    RTCRITSECT              CritSect;
} SHCLCONTEXT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Only one client is supported. There seems to be no need for more clients. */
static SHCLCONTEXT g_ctx;


static int shClBackendReportFormatsToGuestAndMain(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
#ifdef VBOX_COM_INPROC
    return GuestShCl::GetInst()->ReportFormatsToGuest(pClient, fFormats, SHCLSOURCE_LOCAL);
#endif
    return ShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats);
}

/**
 * Checks if something is present on the clipboard and calls shclSvcReportMsg.
 *
 * @returns IPRT status code (ignored).
 * @param   pCtx    The context.
 *
 */
static int vboxClipboardChanged(SHCLCONTEXT *pCtx)
{
    int      vrc      = VINF_SUCCESS;
    uint32_t fFormats = 0;

    RTCritSectEnter(&pCtx->CritSect);

    if (   pCtx->pClient
        && pCtx->fClientReady)
    {
        /* Retrieve the formats currently in the clipboard and supported by VBox. */
        bool     fChanged = false;
        vrc = queryNewPasteboardFormats(pCtx->hPasteboard, pCtx->idGuestOwnership, pCtx->hStrOwnershipFlavor,
                                        &fFormats, &fChanged);
        if (   RT_SUCCESS(vrc)
            && fChanged)
        {
            uint32_t const uMode = pCtx->pClient->State.uMode;
            if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
                || uMode == VBOX_SHCL_MODE_HOST_TO_GUEST)
                vrc = shClBackendReportFormatsToGuestAndMain(pCtx->pClient, fFormats);
        }
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
    SHCLCONTEXT *pCtx = (SHCLCONTEXT *)pvUser;
    AssertPtr(pCtx);
    LogFlowFuncEnter();
    int vrc;

    while (!ASMAtomicReadBool(&pCtx->fTerminate))
    {
        vboxClipboardChanged(pCtx);

        /* Sleep for 200 msecs before next poll */
        vrc = RTThreadUserWait(ThreadSelf, 200);
        if (RT_SUCCESS(vrc))
            break;
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}


int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable)
{
    g_ctx.fTerminate = false;
    g_ctx.fClientReady = false;

    int vrc;

    vrc = RTCritSectInit(&g_ctx.CritSect);
    AssertRCReturn(vrc, vrc);

    vrc = initPasteboard(&g_ctx.hPasteboard);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&g_ctx.CritSect);
        return vrc;
    }

    pBackend->pHelpers = pTable->pHelpers;

    vrc = RTThreadCreate(&g_ctx.hThread, vboxClipboardThread, &g_ctx, 0,
                         RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SHCLIP");
    if (RT_FAILURE(vrc))
    {
        g_ctx.hThread = NIL_RTTHREAD;
        destroyPasteboard(&g_ctx.hPasteboard);
        RTCritSectDelete(&g_ctx.CritSect);
    }

    return vrc;
}

void ShClBackendDestroy(PSHCLBACKEND pBackend)
{
    RT_NOREF(pBackend);

    /*
     * Signal the termination of the polling thread and wait for it to respond.
     */
    ASMAtomicWriteBool(&g_ctx.fTerminate, true);
    if (g_ctx.hThread != NIL_RTTHREAD)
    {
        int vrc = RTThreadUserSignal(g_ctx.hThread);
        AssertRC(vrc);
        vrc = RTThreadWait(g_ctx.hThread, RT_INDEFINITE_WAIT, NULL);
        AssertRC(vrc);
        g_ctx.hThread = NIL_RTTHREAD;
    }

    /*
     * Destroy the hPasteboard and uninitialize the global context record.
     */
    destroyPasteboard(&g_ctx.hPasteboard);
    g_ctx.pClient = NULL;
    g_ctx.fClientReady = false;

    if (RTCritSectIsInitialized(&g_ctx.CritSect))
        RTCritSectDelete(&g_ctx.CritSect);
}

int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, bool fHeadless)
{
    RT_NOREF(pBackend, fHeadless);

    RTCritSectEnter(&g_ctx.CritSect);

    int vrc;
    if (g_ctx.pClient == NULL)
    {
        pClient->State.pCtx = &g_ctx;
        g_ctx.pClient = pClient;
        g_ctx.fClientReady = false;
        vrc = VINF_SUCCESS;
    }
    else
        vrc = VERR_NOT_SUPPORTED; /* One client only. */

    RTCritSectLeave(&g_ctx.CritSect);

    return vrc;
}

int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    /* GuestShCl records the active client after ShClBackendConnect returns.  Do
     * not expose it to the poller before that lifetime guard is in place. */
    RTCritSectEnter(&g_ctx.CritSect);
    int vrc = VINF_SUCCESS;
    if (pClient->State.pCtx->pClient == pClient)
        pClient->State.pCtx->fClientReady = true;
    else
        vrc = VERR_NOT_SUPPORTED;
    RTCritSectLeave(&g_ctx.CritSect);

    /* Sync the host clipboard content with the client. */
    if (RT_SUCCESS(vrc))
        vrc = vboxClipboardChanged(pClient->State.pCtx);
    return vrc;
}

int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    RTCritSectEnter(&g_ctx.CritSect);

    if (pClient->State.pCtx->pClient == pClient)
    {
        pClient->State.pCtx->fClientReady = false;
        pClient->State.pCtx->pClient = NULL;
    }

    RTCritSectLeave(&g_ctx.CritSect);

    return VINF_SUCCESS;
}

int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    LogFlowFunc(("fFormats=%02X\n", fFormats));

    if (fFormats == VBOX_SHCL_FMT_NONE)
    {
        SHCLCONTEXT *pCtx = pClient->State.pCtx;
        RTCritSectEnter(&g_ctx.CritSect);
        int vrcClear = clearPasteboard(pCtx->hPasteboard, &pCtx->hStrOwnershipFlavor);
        RTCritSectLeave(&g_ctx.CritSect);
        return vrcClear;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        LogRel2(("Shared Clipboard: Darwin backend does not support file-transfer clipboard offers yet\n"));
        fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
        if (fFormats == VBOX_SHCL_FMT_NONE)
        {
            SHCLCONTEXT *pCtx = pClient->State.pCtx;
            RTCritSectEnter(&g_ctx.CritSect);
            int vrcClear = clearPasteboard(pCtx->hPasteboard, &pCtx->hStrOwnershipFlavor);
            RTCritSectLeave(&g_ctx.CritSect);
            return vrcClear;
        }
    }
#endif

    SHCLCONTEXT *pCtx = pClient->State.pCtx;
    RTCritSectEnter(&g_ctx.CritSect);

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

    RTCritSectLeave(&g_ctx.CritSect);

    /*
     * Now, request the data from the guest.
     */
    return ShClSvcReadDataFromGuestAsync(pClient, fFormats, NULL /* ppEvent */);
}

/**
 * The host reports clipboard formats to the guest clipboard.
 */
int ShClBackendReportFormatsToGuest(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    int vrc;

    PSHCLCLIENTMSG pMsg = ShClSvcClientMsgAlloc(pClient, VBOX_SHCL_HOST_MSG_FORMATS_REPORT, 2);
    if (pMsg)
    {
        HGCMSvcSetU32(&pMsg->aParms[0], VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
        HGCMSvcSetU32(&pMsg->aParms[1], fFormats);

        ShClSvcClientLock(pClient);

        vrc = shClSvcClientMsgAddAndWakeupClient(pClient, pMsg);

        ShClSvcClientUnlock(pClient);
    }
    else
        vrc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT fFormat,
                        void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(pClient,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCmdCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    RT_NOREF(pBackend, pCmdCtx);

    RTCritSectEnter(&g_ctx.CritSect);

    /* Default to no data available. */
    *pcbActual = 0;

    int vrc = readFromPasteboard(pClient->State.pCtx->hPasteboard, fFormat, pvData, cbData, pcbActual);
    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error reading host clipboard data from macOS, vrc=%Rrc\n", vrc));

    RTCritSectLeave(&g_ctx.CritSect);

    return vrc;
}

int ShClBackendWriteData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT fFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pBackend, pCmdCtx);

    LogFlowFuncEnter();

    RTCritSectEnter(&g_ctx.CritSect);

    int vrc = writeToPasteboard(pClient->State.pCtx->hPasteboard, pClient->State.pCtx->idGuestOwnership,
                                pvData, cbData, fFormat);

    RTCritSectLeave(&g_ctx.CritSect);

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Writing guest data to the macOS pasteboard failed, vrc=%Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# ifndef UNIT_TEST
/**
 * Handles transfer status replies from the guest.
 *
 * @returns VBox status code.
 * @param   pBackend            Shared Clipboard backend.
 * @param   pClient             Shared Clipboard client context.
 * @param   pTransfer           Shared Clipboard transfer.
 * @param   enmSource           Transfer source which issued the reply.
 * @param   enmStatus           Transfer status.
 * @param   rcStatus            Transfer status code.
 */
int ShClBackendTransferHandleStatusReply(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer,
                                         SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_NOREF(pBackend, pClient, pTransfer, enmSource, enmStatus, rcStatus);

    return VINF_SUCCESS;
}
# endif /* !UNIT_TEST */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
