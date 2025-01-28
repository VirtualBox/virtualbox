/* $Id: VBoxClipboard.cpp 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxClipboard - Shared clipboard, Windows Guest Implementation.
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
#include <VBox/log.h>

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxHelpers.h"

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/utf16.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-win.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h> /* Temp, remove. */
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/win/shlobj.h>
# include <iprt/win/shlwapi.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct SHCLCONTEXT
{
    /** Pointer to the VBoxClient service environment. */
    const VBOXTRAYSVCENV *pEnv;
    /** Command context. */
    VBGLR3SHCLCMDCTX      CmdCtx;
    /** Windows-specific context data. */
    SHCLWINCTX            Win;
    /** Thread handle for window thread. */
    RTTHREAD              hThread;
    /** Start indicator flag. */
    bool                  fStarted;
    /** Shutdown indicator flag. */
    bool                  fShutdown;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Associated transfer data. */
    SHCLTRANSFERCTX       TransferCtx;
#endif
};


/*********************************************************************************************************************************
*   Static variables                                                                                                             *
*********************************************************************************************************************************/
/** Static clipboard context (since it is the single instance). Directly used in the windows proc. */
static SHCLCONTEXT g_Ctx = { NULL };
/** Static window class name. */
static char s_szClipWndClassName[] = SHCL_WIN_WNDCLASS_NAME;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static DECLCALLBACK(void) vbtrShClTransferCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) vbtrShClTransferDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) vbtrShClTransferInitializedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) vbtrShClTransferStartedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx);
static DECLCALLBACK(void) vbtrShClTransferErrorCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rc);
#endif


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * @copydoc ShClWinDataObject::CALLBACKS::pfnTransferBegin
 *
 * Called by ShClWinDataObject::GetData() when the user wants to paste data.
 * This then requests a new transfer on the host.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(int) vbtrShClDataObjectTransferBeginCallback(ShClWinDataObject::PCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    int rc = VbglR3ClipboardTransferRequest(&pCtx->CmdCtx);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCreated
 *
 * Called by ShClTransferCreate via VbglR3.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(void) vbtrShClTransferCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    int rc = ShClWinTransferCreate(&pCtx->Win, pCbCtx->pTransfer);

    LogFlowFuncLeaveRC(rc);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnDestroy
 *
 * Called by ShClTransferDestroy via VbglR3.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(void) vbtrShClTransferDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    ShClWinTransferDestroy(&pCtx->Win, pCbCtx->pTransfer);

    LogFlowFuncLeave();
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize
 *
 * Called by ShClTransferInit via VbglR3.
 * For H->G: Called on transfer intialization to initialize the "in-flight" IDataObject for a data transfer.
 * For G->H: Called on transfer intialization to populate the transfer's root list.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(int) vbtrShClTransferInitializeCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    switch(ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* H->G */
        {
            rc = ShClWinTransferInitialize(&pCtx->Win, pTransfer);
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* G->H */
        {
            rc = ShClWinTransferGetRootsFromClipboard(&pCtx->Win, pTransfer);
            break;
        }

        default:
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialized
 *
 * Called by ShClTransferInit via VbglR3.
 * For H->G: Called on transfer intialization to start the data transfer for the "in-flight" IDataObject.
 * For G->H: Nothing to do here.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(void) vbtrShClTransferInitializedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    switch(ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* H->G */
        {
            rc = ShClWinTransferStart(&pCtx->Win, pTransfer);
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* G->H */
            break;

        default:
            break;
    }

    LogFlowFuncLeaveRC(rc);
}

/**
 * Worker for a reading clipboard from the host.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_NO_DATA if no clipboard data is available.
 * @param   pCtx                Shared Clipbaord context to use.
 * @param   uFmt                The format to read clipboard data in.
 * @param   ppvData             Where to return the allocated data read.
 *                              Must be free'd by the caller.
 * @param   pcbData             Where to return number of bytes read.
 * @param   pvUser              User-supplied context.
 *
 * @thread  Clipboard main thread.
 *
 */
static DECLCALLBACK(int) vbtrShClRequestDataFromSourceCallbackWorker(PSHCLCONTEXT pCtx,
                                                                     SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData, void *pvUser)
{
    RT_NOREF(pvUser);

    return VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uFmt, ppvData, pcbData);
}

/**
 * @copydoc SHCLCALLBACKS::pfnOnRequestDataFromSource
 *
 * Called from the IDataObject implementation to request data from the host.
 *
 * @thread  shclwnd thread.
 */
DECLCALLBACK(int) vbtrShClRequestDataFromSourceCallback(PSHCLCONTEXT pCtx,
                                                        SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    PRTREQ pReq = NULL;
    int rc = RTReqQueueCallEx(pCtx->Win.hReqQ, &pReq, SHCL_TIMEOUT_DEFAULT_MS, RTREQFLAGS_IPRT_STATUS,
                              (PFNRT)vbtrShClRequestDataFromSourceCallbackWorker, 5, pCtx, uFmt, ppv, pcb, pvUser);
    RTReqRelease(pReq);
    return rc;
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnStart
 *
 * Called from VbglR3 (main thread) to notify the IDataObject.
 *
 * @thread  Clipboard main thread.
 */
static DECLCALLBACK(void) vbtrShClTransferStartedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    SHCLTRANSFERDIR const enmDir = ShClTransferGetDir(pTransfer);

    int rc = VINF_SUCCESS;

    /* The guest wants to transfer data to the host. */
    if (enmDir == SHCLTRANSFERDIR_TO_REMOTE) /* G->H */
    {
        rc = ShClWinTransferGetRootsFromClipboard(&pCtx->Win, pTransfer);
    }
    else if (enmDir == SHCLTRANSFERDIR_FROM_REMOTE) /* H->G */
    {
        /* Nothing to do here. */
    }
    else
        AssertFailedStmt(rc = VERR_NOT_SUPPORTED);

    if (RT_FAILURE(rc))
        VBoxTrayError("Shared Clipboard: Starting transfer failed, rc=%Rrc\n", rc);
}

/** @copydoc SHCLTRANSFERCALLBACKS::pfnOnCompleted */
static DECLCALLBACK(void) vbtrShClTransferCompletedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcCompletion)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    LogFlowFunc(("rcCompletion=%Rrc\n", rcCompletion));

    VBoxTrayVerbose(1, "Shared Clipboard: Transfer %RU16 %s\n",
                    ShClTransferGetID(pCbCtx->pTransfer), rcCompletion == VERR_CANCELLED ? "canceled" : "complete");

    SHCLTRANSFERSTATUS enmSts;

    switch (rcCompletion)
    {
        case VERR_CANCELLED:
            enmSts = SHCLTRANSFERSTATUS_CANCELED;
            break;

        case VINF_SUCCESS:
            enmSts = SHCLTRANSFERSTATUS_COMPLETED;
            break;

        default:
            AssertFailedStmt(enmSts = SHCLTRANSFERSTATUS_ERROR);
            break;
    }

    int rc = VbglR3ClipboardTransferSendStatus(&pCtx->CmdCtx, pCbCtx->pTransfer, enmSts, rcCompletion);
    LogFlowFuncLeaveRC(rc);
}

/** @copydoc SHCLTRANSFERCALLBACKS::pfnOnError */
static DECLCALLBACK(void) vbtrShClTransferErrorCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcError)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    VBoxTrayError("Shared Clipboard: Transfer %RU16 failed with %Rrc\n", ShClTransferGetID(pCbCtx->pTransfer), rcError);

    if (g_cVerbosity) /* Only show this in verbose mode. */
    {
        char szMsg  [256]; /* Sizes according to MSDN. */
        char szTitle[64];

        /** @todo Add some translation macros here. */
        RTStrPrintf(szTitle, sizeof(szTitle), "VirtualBox Shared Clipboard");
        RTStrPrintf(szMsg, sizeof(szMsg),
                    "Transfer %RU16 failed with %Rrc", ShClTransferGetID(pCbCtx->pTransfer), rcError);

        VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                    szMsg, szTitle,
                                    5000 /* Time to display in msec */, NIIF_INFO);
    }

    int rc = VbglR3ClipboardTransferSendStatus(&pCtx->CmdCtx, pCbCtx->pTransfer, SHCLTRANSFERSTATUS_ERROR, rcError);
    LogFlowFuncLeaveRC(rc);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

static LRESULT vbtrShClWndProcWorker(PSHCLCONTEXT pCtx, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AssertPtr(pCtx);

    const PSHCLWINCTX pWinCtx = &pCtx->Win;

    LRESULT lresultRc = 0;

    switch (msg)
    {
        case WM_CLIPBOARDUPDATE:
        {
            LogFunc(("WM_CLIPBOARDUPDATE: pWinCtx=%p\n", pWinCtx));

            if (pCtx->fShutdown) /* If we're about to shut down, skip handling stuff here. */
                break;

            int rc = RTCritSectEnter(&pWinCtx->CritSect);
            if (RT_SUCCESS(rc))
            {
                const HWND hWndClipboardOwner = GetClipboardOwner();

                LogFunc(("WM_CLIPBOARDUPDATE: hWndOldClipboardOwner=%p, hWndNewClipboardOwner=%p\n",
                         pWinCtx->hWndClipboardOwnerUs, hWndClipboardOwner));

                if (pWinCtx->hWndClipboardOwnerUs != hWndClipboardOwner)
                {
                    int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(rc2);

                    /* Clipboard was updated by another application.
                     * Report available formats to the host. */
                    SHCLFORMATS fFormats;
                    rc = ShClWinGetFormats(pWinCtx, &fFormats);
                    if (RT_SUCCESS(rc))
                    {
                        LogFunc(("WM_CLIPBOARDUPDATE: Reporting formats %#x\n", fFormats));
                        rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fFormats);
                    }
                }
                else
                {
                    int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(rc2);
                }
            }

            if (RT_FAILURE(rc))
                VBoxTrayError("Shared Clipboard: WM_CLIPBOARDUPDATE failed with %Rrc\n", rc);

            break;
        }

        case WM_CHANGECBCHAIN:
        {
            LogFunc(("WM_CHANGECBCHAIN\n"));
            lresultRc = ShClWinHandleWMChangeCBChain(pWinCtx, hwnd, msg, wParam, lParam);
            break;
        }

        case WM_DRAWCLIPBOARD:
        {
            LogFlowFunc(("WM_DRAWCLIPBOARD: pWinCtx=%p\n", pWinCtx));

            int rc = RTCritSectEnter(&pWinCtx->CritSect);
            if (RT_SUCCESS(rc))
            {
                const HWND hWndClipboardOwner = GetClipboardOwner();

                LogFunc(("WM_DRAWCLIPBOARD: hWndClipboardOwnerUs=%p, hWndNewClipboardOwner=%p\n",
                         pWinCtx->hWndClipboardOwnerUs, hWndClipboardOwner));

                if (pWinCtx->hWndClipboardOwnerUs != hWndClipboardOwner)
                {
                    int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(rc2);

                    /* Clipboard was updated by another application. */
                    /* WM_DRAWCLIPBOARD always expects a return code of 0, so don't change "rc" here. */
                    SHCLFORMATS fFormats;
                    rc = ShClWinGetFormats(pWinCtx, &fFormats);
                    if (   RT_SUCCESS(rc)
                        && fFormats != VBOX_SHCL_FMT_NONE)
                        rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fFormats);
                }
                else
                {
                    int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(rc2);
                }
            }

            lresultRc = ShClWinChainPassToNext(pWinCtx, msg, wParam, lParam);
            break;
        }

        case WM_TIMER:
        {
            int rc = ShClWinHandleWMTimer(pWinCtx);
            AssertRC(rc);

            break;
        }

        case WM_CLOSE:
        {
            /* Do nothing. Ignore the message. */
            break;
        }

        case WM_RENDERFORMAT: /* Guest wants to render the clipboard data. */
        {
            /* Insert the requested clipboard format data into the clipboard. */
            const UINT       uFmtWin  = (UINT)wParam;
            const SHCLFORMAT uFmtVBox = ShClWinClipboardFormatToVBox(uFmtWin);

            LogFunc(("WM_RENDERFORMAT: uFmtWin=%u -> uFmtVBox=0x%x\n", uFmtWin, uFmtVBox));

            char *pszFmts = ShClFormatsToStrA(uFmtVBox);
            AssertPtrReturn(pszFmts, 0);
            VBoxTrayVerbose(1, "Shared Clipboard: Rendering Windows format %#x as VBox format '%s'\n", uFmtWin, pszFmts);
            RTStrFree(pszFmts);

            if (uFmtVBox == VBOX_SHCL_FMT_NONE)
            {
                VBoxTrayInfo("Shared Clipboard: Unsupported format (%#x) requested\n", uFmtWin);
                ShClWinClear();
            }
            else
            {
                void    *pvData = NULL;
                uint32_t cbData;

                HANDLE hMem;
                int rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uFmtVBox, &pvData, &cbData);
                if (RT_SUCCESS(rc))
                {
                    /* Verify the size of returned text, the memory block for clipboard must have the exact string size. */
                    if (uFmtVBox == VBOX_SHCL_FMT_UNICODETEXT)
                    {
                        size_t cwcActual = 0;
                        rc = RTUtf16NLenEx((PCRTUTF16)pvData, cbData / sizeof(RTUTF16), &cwcActual);
                        if (RT_SUCCESS(rc))
                            cbData = (uint32_t)((cwcActual + 1 /* '\0' */) * sizeof(RTUTF16));
                        else
                            VBoxTrayError("Shared Clipboard: Invalid UTF16 string from host: cb=%RU32, cwcActual=%zu, rc=%Rrc\n",
                                          cbData, cwcActual, rc);
                    }
                    else if (uFmtVBox == VBOX_SHCL_FMT_HTML)
                    {
                        /* Wrap content into CF_HTML clipboard format if needed. */
                        if (!ShClWinIsCFHTML((const char *)pvData))
                        {
                            char    *pszWrapped = NULL;
                            uint32_t cbWrapped  = 0;
                            rc = ShClWinConvertMIMEToCFHTML((const char *)pvData, cbData, &pszWrapped, &cbWrapped);
                            if (RT_SUCCESS(rc))
                            {
                                AssertBreakStmt(cbWrapped, rc = VERR_INVALID_PARAMETER);
                                pvData = RTMemRealloc(pvData, cbWrapped);
                                if (pvData)
                                {
                                    memcpy(pvData, pszWrapped, cbWrapped);
                                    cbData = cbWrapped;
                                }
                                else
                                    rc = VERR_NO_MEMORY;
                                RTMemFree(pszWrapped);
                            }

                            if (RT_FAILURE(rc))
                                VBoxTrayError("Shared Clipboard: Cannot convert HTML clipboard data into CF_HTML clipboard format, rc=%Rrc\n", rc);
                        }
                    }

                    hMem = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, cbData);
                    if (hMem)
                    {
                        void *pvMem = GlobalLock(hMem);
                        if (pvMem)
                        {
                            memcpy(pvMem, pvData, cbData);
                            GlobalUnlock(hMem);

                            HANDLE hClip = SetClipboardData(uFmtWin, hMem);
                            if (!hClip)
                            {
                                /* The hMem ownership has gone to the system. Finish the processing. */
                                break;
                            }
                            else
                                VBoxTrayError("Shared Clipboard: Setting host data buffer to clipboard failed with %Rrc\n",
                                              RTErrConvertFromWin32(GetLastError()));
                        }
                        else
                            VBoxTrayError("Shared Clipboard: Failed to lock memory (%Rrc)\n", RTErrConvertFromWin32(GetLastError()));
                        GlobalFree(hMem);
                    }
                    else
                        VBoxTrayError("Shared Clipboard: No memory for allocating host data buffer\n");
                }
            }

            break;
        }

        case WM_RENDERALLFORMATS:
        {
            LogFunc(("WM_RENDERALLFORMATS\n"));

            int rc = ShClWinHandleWMRenderAllFormats(pWinCtx, hwnd);
            AssertRC(rc);

            break;
        }

        case SHCL_WIN_WM_REPORT_FORMATS: /* Host reported clipboard formats. */
        {
            LogFunc(("SHCL_WIN_WM_REPORT_FORMATS\n"));

            /* Announce available formats. Do not insert data -- will be inserted in WM_RENDERFORMAT. */
            PVBGLR3CLIPBOARDEVENT pEvent = (PVBGLR3CLIPBOARDEVENT)lParam;
            AssertPtr(pEvent);
            Assert(pEvent->enmType == VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS);

            const SHCLFORMATS fFormats = pEvent->u.fReportedFormats;

            char *pszFmts = ShClFormatsToStrA(fFormats);
            AssertPtrReturn(pszFmts, 0);
            VBoxTrayVerbose(1, "Shared Clipboard: Host reported formats '%s'\n", pszFmts);
            RTStrFree(pszFmts);

            if (fFormats != VBOX_SHCL_FMT_NONE) /* Could arrive with some older GA versions. */
            {
                int rc = ShClWinClearAndAnnounceFormats(pWinCtx, fFormats, hwnd);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                if (   RT_SUCCESS(rc)
                    && fFormats & VBOX_SHCL_FMT_URI_LIST)
                {
                    /*
                     * Create our IDataObject implementation and push it to the Windows clibpoard.
                     * That way Windows will recognize that there is a data transfer available.
                     */
                    ShClWinDataObject::CALLBACKS Callbacks;
                    RT_ZERO(Callbacks);
                    Callbacks.pfnTransferBegin = vbtrShClDataObjectTransferBeginCallback;

                    rc = ShClWinTransferCreateAndSetDataObject(pWinCtx, pCtx, &Callbacks);
                }
#else
                RT_NOREF(rc);
#endif
            }

            LogFunc(("SHCL_WIN_WM_REPORT_FORMATS: fFormats=0x%x, lastErr=%ld\n", fFormats, GetLastError()));
            break;
        }

        case SHCL_WIN_WM_READ_DATA: /* Host wants to read clipboard data from the guest. */
        {
            /* Send data in the specified format to the host. */
            PVBGLR3CLIPBOARDEVENT pEvent = (PVBGLR3CLIPBOARDEVENT)lParam;
            AssertPtr(pEvent);
            Assert(pEvent->enmType == VBGLR3CLIPBOARDEVENTTYPE_READ_DATA);

            const SHCLFORMAT fFormat = (uint32_t)pEvent->u.fReadData;

            LogFlowFunc(("SHCL_WIN_WM_READ_DATA: fFormat=%#x\n", fFormat));

            char *pszFmts = ShClFormatsToStrA(fFormat);
            AssertPtrReturn(pszFmts, 0);
            VBoxTrayVerbose(1, "Shared Clipboard: Sending data to host as '%s'\n", pszFmts);
            RTStrFree(pszFmts);

            int rc = ShClWinOpen(hwnd);
            HANDLE hClip = NULL;
            if (RT_SUCCESS(rc))
            {
                if (fFormat & VBOX_SHCL_FMT_BITMAP)
                {
                    hClip = GetClipboardData(CF_DIB);
                    if (hClip != NULL)
                    {
                        LPVOID lp = GlobalLock(hClip);
                        if (lp != NULL)
                        {
                            rc = VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx, fFormat, lp, (uint32_t)GlobalSize(hClip));

                            GlobalUnlock(hClip);
                        }
                        else
                            hClip = NULL;
                    }
                }
                else if (fFormat & VBOX_SHCL_FMT_UNICODETEXT)
                {
                    hClip = GetClipboardData(CF_UNICODETEXT);
                    if (hClip != NULL)
                    {
                        LPWSTR uniString = (LPWSTR)GlobalLock(hClip);
                        if (uniString != NULL)
                        {
                            rc = VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx,
                                                            fFormat, uniString, ((uint32_t)lstrlenW(uniString) + 1) * 2);

                            GlobalUnlock(hClip);
                        }
                        else
                            hClip = NULL;
                    }
                }
                else if (fFormat & VBOX_SHCL_FMT_HTML)
                {
                    UINT format = RegisterClipboardFormat(SHCL_WIN_REGFMT_HTML);
                    if (format != 0)
                    {
                        hClip = GetClipboardData(format);
                        if (hClip != NULL)
                        {
                            LPVOID const pvClip = GlobalLock(hClip);
                            if (pvClip != NULL)
                            {
                                uint32_t const cbClip = (uint32_t)GlobalSize(hClip);

                                /* Unwrap clipboard content from CF_HTML format if needed. */
                                if (ShClWinIsCFHTML((const char *)pvClip))
                                {
                                    char        *pszBuf = NULL;
                                    uint32_t    cbBuf   = 0;
                                    rc = ShClWinConvertCFHTMLToMIME((const char *)pvClip, cbClip, &pszBuf, &cbBuf);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx, fFormat, pszBuf, cbBuf);
                                        RTMemFree(pszBuf);
                                    }
                                    else
                                        rc = VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx, fFormat, pvClip, cbClip);
                                }
                                else
                                    rc = VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx, fFormat, pvClip, cbClip);

                                GlobalUnlock(hClip);
                            }
                            else
                                hClip = NULL;
                        }
                    }
                }

                if (hClip == NULL)
                    LogFunc(("SHCL_WIN_WM_READ_DATA: hClip=NULL, lastError=%ld\n", GetLastError()));

                ShClWinClose();
            }

            /* If the requested clipboard format is not available, we must send empty data. */
            if (hClip == NULL)
                VbglR3ClipboardWriteDataEx(&pEvent->cmdCtx, VBOX_SHCL_FMT_NONE, NULL, 0);
            break;
        }

        case WM_DESTROY:
        {
            LogFunc(("WM_DESTROY\n"));

            int rc = ShClWinHandleWMDestroy(pWinCtx);
            AssertRC(rc);

            /*
             * Don't need to call PostQuitMessage cause
             * the VBoxTray already finished a message loop.
             */

            break;
        }

        default:
        {
            LogFunc(("WM_ %p\n", msg));
            lresultRc = DefWindowProc(hwnd, msg, wParam, lParam);
            break;
        }
    }

    LogFunc(("WM_ rc %d\n", lresultRc));
    return lresultRc;
}

static LRESULT CALLBACK vbtrShClWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static int vbtrShClCreateWindow(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    AssertPtr(pCtx->pEnv);
    HINSTANCE hInstance = pCtx->pEnv->hInstance;
    Assert(hInstance != 0);

    /* Register the Window Class. */
    WNDCLASSEX wc;
    RT_ZERO(wc);

    wc.cbSize = sizeof(WNDCLASSEX);

    if (!GetClassInfoEx(hInstance, s_szClipWndClassName, &wc))
    {
        wc.style         = CS_NOCLOSE;
        wc.lpfnWndProc   = vbtrShClWndProc;
        wc.hInstance     = pCtx->pEnv->hInstance;
        wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
        wc.lpszClassName = s_szClipWndClassName;

        ATOM wndClass = RegisterClassEx(&wc);
        if (wndClass == 0)
            rc = RTErrConvertFromWin32(GetLastError());
    }

    if (RT_SUCCESS(rc))
    {
        const PSHCLWINCTX pWinCtx = &pCtx->Win;

        /* Create the window. */
        pWinCtx->hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                       s_szClipWndClassName, s_szClipWndClassName,
                                       WS_POPUPWINDOW,
                                       -200, -200, 100, 100, NULL, NULL, hInstance, NULL);
        if (pWinCtx->hWnd == NULL)
        {
            rc = VERR_NOT_SUPPORTED;
        }
        else
        {
            SetWindowPos(pWinCtx->hWnd, HWND_TOPMOST, -200, -200, 0, 0,
                         SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

            rc = ShClWinChainAdd(pWinCtx);
            if (RT_SUCCESS(rc))
            {
                if (!ShClWinIsNewAPI(&pWinCtx->newAPI))
                    pWinCtx->oldAPI.timerRefresh = SetTimer(pWinCtx->hWnd, 0, 10 * 1000 /* 10s */, NULL);
            }
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) vbtrShClWindowThread(RTTHREAD hThread, void *pvUser)
{
    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvUser;
    AssertPtr(pCtx);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr))
    {
        VBoxTrayError("Shared Clipboard: Initializing OLE in window thread failed (%Rhrc) -- file transfers unavailable\n", hr);
        /* Not critical, the rest of the clipboard might work. */
    }
    else
        VBoxTrayInfo("Shared Clipboard: Initialized OLE in window thread\n");
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    int rc = vbtrShClCreateWindow(pCtx);
    if (RT_FAILURE(rc))
    {
        VBoxTrayError("Shared Clipboard: Unable to create window, rc=%Rrc\n", rc);
        return rc;
    }

    pCtx->fStarted = true; /* Set started indicator. */

    int rc2 = RTThreadUserSignal(hThread);
    bool fSignalled = RT_SUCCESS(rc2);

    VBoxTrayInfo("Shared Clipboard: Window thread running\n");

    if (RT_SUCCESS(rc))
    {
        for (;;)
        {
            MSG uMsg;
            BOOL fRet;
            while ((fRet = GetMessage(&uMsg, 0, 0, 0)) > 0)
            {
                TranslateMessage(&uMsg);
                DispatchMessage(&uMsg);
            }
            Assert(fRet >= 0);

            if (ASMAtomicReadBool(&pCtx->fShutdown))
                break;

            /** @todo Immediately drop on failure? */
        }
    }

    if (!fSignalled)
    {
        rc2 = RTThreadUserSignal(hThread);
        AssertRC(rc2);
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    OleSetClipboard(NULL); /* Make sure to flush the clipboard on destruction. */
    OleUninitialize();
#endif

    VBoxTrayInfo("Shared Clipboard: Window thread ended\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static LRESULT CALLBACK vbtrShClWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PSHCLCONTEXT pCtx = &g_Ctx; /** @todo r=andy Make pCtx available through SetWindowLongPtr() / GWL_USERDATA. */
    AssertPtr(pCtx);

    /* Forward with proper context. */
    return vbtrShClWndProcWorker(pCtx, hWnd, uMsg, wParam, lParam);
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrShClPreInit(void)
{
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrShClOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
DECLCALLBACK(int) vbtrShClInit(const PVBOXTRAYSVCENV pEnv, void **ppvInstance)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = &g_Ctx; /* Only one instance for now. */
    AssertPtr(pCtx);

    if (pCtx->pEnv)
    {
        /* Clipboard was already initialized. 2 or more instances are not supported. */
        return VERR_NOT_SUPPORTED;
    }

    if (VbglR3AutoLogonIsRemoteSession())
    {
        /* Do not use clipboard for remote sessions. */
        VBoxTrayInfo("Shared Clipboard: Clipboard has been disabled for a remote session\n");
        return VERR_NOT_SUPPORTED;
    }

    pCtx->pEnv      = pEnv;
    pCtx->hThread   = NIL_RTTHREAD;
    pCtx->fStarted  = false;
    pCtx->fShutdown = false;

    int rc = RTReqQueueCreate(&pCtx->Win.hReqQ);
    AssertRCReturn(rc, rc);

    rc = ShClWinCtxInit(&pCtx->Win);
    if (RT_SUCCESS(rc))
        rc = VbglR3ClipboardConnectEx(&pCtx->CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
    if (RT_SUCCESS(rc))
    {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        rc = ShClTransferCtxInit(&pCtx->TransferCtx);
#endif
        if (RT_SUCCESS(rc))
        {
            /* Message pump thread for our proxy window. */
            rc = RTThreadCreate(&pCtx->hThread, vbtrShClWindowThread, pCtx /* pvUser */,
                                0, RTTHREADTYPE_MSG_PUMP, RTTHREADFLAGS_WAITABLE,
                                "shclwnd");
            if (RT_SUCCESS(rc))
            {
                int rc2 = RTThreadUserWait(pCtx->hThread, RT_MS_30SEC /* Timeout in ms */);
                AssertRC(rc2);

                if (!pCtx->fStarted) /* Did the thread fail to start? */
                    rc = VERR_NOT_SUPPORTED; /* Report back Shared Clipboard as not being supported. */
            }
        }

        if (RT_SUCCESS(rc))
        {
            *ppvInstance = pCtx;
        }
        else
            VbglR3ClipboardDisconnectEx(&pCtx->CmdCtx);
    }

    if (RT_FAILURE(rc))
        VBoxTrayError("Shared Clipboard: Unable to initialize, rc=%Rrc\n", rc);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
DECLCALLBACK(int) vbtrShClWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtr(pvInstance);
    LogFlowFunc(("pvInstance=%p\n", pvInstance));

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    const PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvInstance;
    AssertPtr(pCtx);

    const PSHCLWINCTX pWinCtx = &pCtx->Win;

    VBoxTrayVerbose(1, "Shared Clipboard: Worker loop running\n");

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr))
    {
        VBoxTrayError("Shared Clipboard: Initializing OLE in worker thread failed (%Rhrc) -- file transfers unavailable\n", hr);
        /* Not critical, the rest of the clipboard might work. */
    }
    else
        VBoxTrayInfo("Shared Clipboard: Initialized OLE in worker thread\n");

    /*
     * Init callbacks.
     * Those will be registered within VbglR3 when a new transfer gets initialized.
     */
    RT_ZERO(pCtx->CmdCtx.Transfers.Callbacks);

    pCtx->CmdCtx.Transfers.Callbacks.pvUser = pCtx; /* Assign context as user-provided callback data. */
    pCtx->CmdCtx.Transfers.Callbacks.cbUser = sizeof(SHCLCONTEXT);

    pCtx->CmdCtx.Transfers.Callbacks.pfnOnCreated     = vbtrShClTransferCreatedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnDestroy     = vbtrShClTransferDestroyCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnInitialize  = vbtrShClTransferInitializeCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnInitialized = vbtrShClTransferInitializedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnStarted     = vbtrShClTransferStartedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnCompleted   = vbtrShClTransferCompletedCallback;
    pCtx->CmdCtx.Transfers.Callbacks.pfnOnError       = vbtrShClTransferErrorCallback;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    int rc;

    /* The thread waits for incoming messages from the host. */
    PVBGLR3CLIPBOARDEVENT pEvent = NULL;
    for (;;)
    {
        LogFlowFunc(("Waiting for host message (fUseLegacyProtocol=%RTbool, fHostFeatures=%#RX64) ...\n",
                     pCtx->CmdCtx.fUseLegacyProtocol, pCtx->CmdCtx.fHostFeatures));

        if (!pEvent)
            pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
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
        else if (rc == VERR_TRY_AGAIN) /* No new message (yet). */
        {
            RTReqQueueProcess(pCtx->Win.hReqQ, RT_MS_1SEC);
            continue;
        }

        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("Getting next event failed with %Rrc\n", rc));

            VbglR3ClipboardEventFree(pEvent);
            pEvent = NULL;

            if (*pfShutdown)
                break;

            /* Wait a bit before retrying. */
            RTThreadSleep(1000);
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
                    /* The host has announced available clipboard formats.
                     * Forward the information to the window, so it can later
                     * respond to WM_RENDERFORMAT message. */
                    ::PostMessage(pWinCtx->hWnd, SHCL_WIN_WM_REPORT_FORMATS,
                                  0 /* wParam */, (LPARAM)pEvent /* lParam */);

                    pEvent = NULL; /* Consume pointer. */
                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
                {
                    /* The host needs data in the specified format. */
                    ::PostMessage(pWinCtx->hWnd, SHCL_WIN_WM_READ_DATA,
                                  0 /* wParam */, (LPARAM)pEvent /* lParam */);

                    pEvent = NULL; /* Consume pointer. */
                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_QUIT:
                {
                    VBoxTrayVerbose(1, "Shared Clipboard: Host requested termination\n");
                    ASMAtomicXchgBool(pfShutdown, true);
                    break;
                }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                case VBGLR3CLIPBOARDEVENTTYPE_TRANSFER_STATUS:
                {
                    /* Nothing to do here. */
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

        if (*pfShutdown)
            break;
    }

    VBoxTrayVerbose(1, "Shared Clipboard: Worker loop ended\n");

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    OleSetClipboard(NULL); /* Make sure to flush the clipboard on destruction. */
    OleUninitialize();
#endif

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnStop}
 */
DECLCALLBACK(int) vbtrShClStop(void *pvInstance)
{
    AssertPtrReturn(pvInstance, VERR_INVALID_POINTER);

    LogFunc(("Stopping pvInstance=%p\n", pvInstance));

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvInstance;
    AssertPtr(pCtx);

    /* Set shutdown indicator. */
    ASMAtomicWriteBool(&pCtx->fShutdown, true);

    /* Let our clipboard know that we're going to shut down. */
    PostMessage(pCtx->Win.hWnd, WM_QUIT, 0, 0);

    /* Disconnect from the host service.
     * This will also send a VBOX_SHCL_HOST_MSG_QUIT from the host so that we can break out from our message worker. */
    VbglR3ClipboardDisconnect(pCtx->CmdCtx.idClient);
    pCtx->CmdCtx.idClient = 0;

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
DECLCALLBACK(void) vbtrShClDestroy(void *pvInstance)
{
    AssertPtrReturnVoid(pvInstance);

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pvInstance;
    AssertPtrReturnVoid(pCtx);

    /* Make sure that we are disconnected. */
    Assert(pCtx->CmdCtx.idClient == 0);

    LogFlowFunc(("pCtx=%p\n", pCtx));

    VBoxTrayVerbose(1, "Shared Clipboard: Destroying ...\n");

    const PSHCLWINCTX pWinCtx = &pCtx->Win;

    if (pCtx->hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_WRONG_ORDER;
        int rc = RTThreadWait(pCtx->hThread, 60 * 1000 /* Timeout in ms */, &rcThread);
        LogFlowFunc(("Waiting for thread resulted in %Rrc (thread exited with %Rrc)\n",
                     rc, rcThread));
        RT_NOREF(rc);
    }

    if (pWinCtx->hWnd)
    {
        DestroyWindow(pWinCtx->hWnd);
        pWinCtx->hWnd = NULL;
    }

    UnregisterClass(s_szClipWndClassName, pCtx->pEnv->hInstance);

    ShClWinCtxDestroy(&pCtx->Win);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ShClTransferCtxDestroy(&pCtx->TransferCtx);
#endif

    RTReqQueueDestroy(pCtx->Win.hReqQ);

    VBoxTrayVerbose(1, "Shared Clipboard: Destroyed\n");

    return;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescClipboard =
{
    /* pszName. */
    "clipboard",
    /* pszDescription. */
    "Shared Clipboard",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrShClPreInit,
    vbtrShClOption,
    vbtrShClInit,
    vbtrShClWorker,
    vbtrShClStop,
    vbtrShClDestroy
};
