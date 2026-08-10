/* $Id: ClipboardBackendWin.cpp 114971 2026-08-10 17:29:14Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Win32 host.
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

#include <iprt/win/windows.h>

#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/GuestHost/SharedClipboard-win.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ldr.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/utf16.h>
#endif

#include <process.h>
#include <iprt/win/shlobj.h> /* Needed for shell objects. */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif
#ifdef VBOX_COM_INPROC
# include "GuestShClPrivate.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Global context information used by the host glue for the X11 clipboard backend.
 */
struct SHCLCONTEXT
{
    /** Handle for window message handling thread. */
    RTTHREAD                   hThread;
    /** Structure for keeping and communicating with service client. */
    PSHCLCLIENT                pClient;
    /** Windows-specific context data. */
    SHCLWINCTX                 Win;
};


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int vboxClipboardSvcWinSyncInternal(PSHCLCONTEXT pCtx);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static DECLCALLBACK(int) shClSvcWinTransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx);
#endif


/**
 * Copy clipboard data into the guest buffer.
 *
 * At first attempt, guest will provide a buffer of default size.
 * Usually 1K or 4K (see platform specific Guest Additions code around
 * VbglR3ClipboardReadData calls). If this buffer is not big enough
 * to fit host clipboard content, this function will return VINF_BUFFER_OVERFLOW
 * and provide guest with host's clipboard buffer actual size. This will be a
 * signal for the guest to re-read host clipboard data providing bigger buffer
 * to store it.
 *
 * @returns IPRT status code.
 * @returns VINF_BUFFER_OVERFLOW returned when guest buffer size if not big
 *          enough to store host clipboard data. This is a signal to the guest
 *          to re-issue host clipboard read request with bigger buffer size
 *          (specified in @a pcbActualDst output parameter).
 * @param   u32Format           VBox clipboard format (VBOX_SHCL_FMT_XXX) of copied data.
 *                              VBOX_SHCL_FMT_NONE returns 0 data.
 * @param   pvSrc               Pointer to host clipboard data.
 * @param   cbSrc               Size (in bytes) of actual clipboard data to copy.
 * @param   pvDst               Pointer to guest buffer to store clipboard data.
 * @param   cbDst               Size (in bytes) of guest buffer.
 * @param   pcbActualDst        Actual size (in bytes) of host clipboard data.
 *                              Only set if guest buffer size if not big enough
 *                              to store host clipboard content. When set,
 *                              function returns VINF_BUFFER_OVERFLOW.
 */
static int vboxClipboardSvcWinDataGet(SHCLFORMAT u32Format, const void *pvSrc, uint32_t cbSrc,
                                      void *pvDst, uint32_t cbDst, uint32_t *pcbActualDst)
{
    AssertPtrReturn(pcbActualDst, VERR_INVALID_POINTER);
    if (u32Format == VBOX_SHCL_FMT_NONE)
    {
        *pcbActualDst = 0;
        return VINF_SUCCESS;
    }

    AssertPtrReturn(pvSrc, VERR_INVALID_POINTER);
    AssertReturn   (cbSrc, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvDst, VERR_INVALID_POINTER);
    AssertReturn   (cbDst, VERR_INVALID_PARAMETER);

    LogFlowFunc(("cbSrc = %d, cbDst = %d\n", cbSrc, cbDst));

    if (   u32Format == VBOX_SHCL_FMT_HTML
        && ShClWinIsCFHTML((const char *)pvSrc))
    {
        /** @todo r=bird: Why the double conversion? */
        char *pszBuf = NULL;
        uint32_t cbBuf = 0;
        int vrc = ShClWinConvertCFHTMLToMIME((const char *)pvSrc, cbSrc, &pszBuf, &cbBuf);
        if (RT_SUCCESS(vrc))
        {
            *pcbActualDst = cbBuf;
            if (cbBuf > cbDst)
            {
                /* Do not copy data. The dst buffer is not enough. */
                RTMemFree(pszBuf);
                return VINF_BUFFER_OVERFLOW;
            }
            memcpy(pvDst, pszBuf, cbBuf);
            RTMemFree(pszBuf);
        }
        else
            *pcbActualDst = 0;
    }
    else
    {
        *pcbActualDst = cbSrc; /* Tell the caller how much space we need. */

        if (cbSrc > cbDst)
            return VINF_BUFFER_OVERFLOW;

        memcpy(pvDst, pvSrc, cbSrc);
    }

#ifdef LOG_ENABLED
    ShClDbgDumpData(pvDst, cbSrc, u32Format);
#endif

    return VINF_SUCCESS;
}

/**
 * Worker for a reading clipboard from the guest.
 */
static int vboxClipboardSvcWinReadDataFromGuestWorker(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData)
{
    return ShClSvcReadDataFromGuest(pCtx->pClient, uFmt, ppvData, pcbData);
}

static int vboxClipboardSvcWinReadDataFromGuest(PSHCLCONTEXT pCtx, UINT uWinFormat, void **ppvData, uint32_t *pcbData)
{
    SHCLFORMAT uVBoxFmt = ShClWinClipboardFormatToVBox(uWinFormat);
    if (uVBoxFmt == VBOX_SHCL_FMT_NONE)
    {
        LogRel2(("Shared Clipboard: Windows format %u not supported, ignoring\n", uWinFormat));
        return VERR_NOT_SUPPORTED;
    }

    int vrc = vboxClipboardSvcWinReadDataFromGuestWorker(pCtx, uVBoxFmt, ppvData, pcbData);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc SHCLCALLBACKS::pfnOnRequestDataFromSource
 *
 * Called from the IDataObject implementation to request data from the guest.
 *
 * @thread  Windows event thread.
 */
static DECLCALLBACK(int) vboxClipboardSvcWinRequestDataFromSourceCallback(PSHCLCONTEXT pCtx,
                                                                          SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFuncEnter();

    int vrc = vboxClipboardSvcWinReadDataFromGuestWorker(pCtx, uFmt, ppv, pcb);

    LogFlowFuncLeaveRC(vrc);

    return vrc;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnCreated
 *
 * @thread Service main thread.
 */
static DECLCALLBACK(void) shClSvcWinTransferOnCreatedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
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

    int vrc = VINF_SUCCESS;

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* G->H */
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

        case SHCLTRANSFERDIR_TO_REMOTE: /* H->G */
        {
            pIface->pfnRootListRead  = shClSvcWinTransferIfaceHGRootListRead;
            break;
        }

        default:
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        vrc = ShClTransferSetProvider(pTransfer, &pClient->Transfers.Provider);
        if (RT_SUCCESS(vrc))
            vrc = ShClWinTransferCreate(&pCtx->Win, pTransfer);
    }

    LogFlowFuncLeaveRC(vrc);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnInitialize
 *
 * For G->H: Called on transfer intialization to notify the "in-flight" IDataObject about a data transfer.
 * For H->G: Called on transfer intialization to populate the transfer's root list.
 *
 * @thread  Service main thread.
 */
static DECLCALLBACK(int) shClSvcWinTransferOnInitializeCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    int vrc = VINF_SUCCESS;

    switch (ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* G->H */
        {
            vrc = ShClWinTransferInitialize(&pCtx->Win, pTransfer);
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* H->G */
        {
            vrc = ShClTransferRootListRead(pTransfer); /* Calls shClSvcWinTransferIfaceHGRootListRead(). */
            break;
        }

        default:
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
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
static DECLCALLBACK(void) shClSvcWinTransferOnInitializedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    int vrc = VINF_SUCCESS;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    switch(ShClTransferGetDir(pTransfer))
    {
        case SHCLTRANSFERDIR_FROM_REMOTE: /* H->G */
        {
            vrc = ShClWinTransferStart(&pCtx->Win, pTransfer);
            break;
        }

        case SHCLTRANSFERDIR_TO_REMOTE: /* G->H */
            break;

        default:
            break;
    }

    LogFlowFuncLeaveRC(vrc);
}

/**
 * @copydoc SHCLTRANSFERCALLBACKS::pfnOnDestroy
 *
 * @thread  Service main thread.
 */
static DECLCALLBACK(void) shClSvcWinTransferOnDestroyCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer = pCbCtx->pTransfer;
    AssertPtr(pTransfer);

    ShClWinTransferDestroy(&pCtx->Win, pTransfer);
}

/**
 * @copydoc ShClWinDataObject::CALLBACKS::pfnTransferBegin
 *
 * Called by ShClWinDataObject::GetData() when the user wants to paste data.
 * This then creates and initializes a new transfer on the host + lets the guest know about that new transfer.
 *
 * @thread  Service main thread.
 */
static DECLCALLBACK(int) shClSvcWinDataObjectTransferBeginCallback(ShClWinDataObject::PCALLBACKCTX pCbCtx)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)pCbCtx->pvUser;
    AssertPtr(pCtx);

    PSHCLTRANSFER pTransfer;
    int vrc = ShClSvcTransferCreate(pCtx->pClient, SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE,
                                   NIL_SHCLTRANSFERID /* Creates a new transfer ID */, &pTransfer);
    if (RT_SUCCESS(vrc))
    {
        /* Initialize the transfer on the host side. */
        vrc = ShClSvcTransferInit(pCtx->pClient, pTransfer);
        if (RT_FAILURE(vrc))
             ShClSvcTransferDestroy(pCtx->pClient, pTransfer);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

static LRESULT CALLBACK vboxClipboardSvcWinWndProcMain(PSHCLCONTEXT pCtx,
                                                       HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    AssertPtr(pCtx);

    LRESULT lresultRc = 0;

    const PSHCLWINCTX pWinCtx = &pCtx->Win;

    switch (uMsg)
    {
        case WM_CLIPBOARDUPDATE:
        {
            LogFunc(("WM_CLIPBOARDUPDATE\n"));

            int vrc = RTCritSectEnter(&pWinCtx->CritSect);
            if (RT_SUCCESS(vrc))
            {
                const HWND hWndClipboardOwner = GetClipboardOwner();

                LogFunc(("WM_CLIPBOARDUPDATE: hWndClipboardOwnerUs=%p, hWndNewClipboardOwner=%p\n",
                         pWinCtx->hWndClipboardOwnerUs, hWndClipboardOwner));

                if (pWinCtx->hWndClipboardOwnerUs != hWndClipboardOwner)
                {
                    int vrc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(vrc2);

                    /* Clipboard was updated by another application, retrieve formats and report back. */
                    vrc = vboxClipboardSvcWinSyncInternal(pCtx);
                }
                else
                {
                    int vrc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(vrc2);
                }
            }

            if (RT_FAILURE(vrc))
                LogRel(("Shared Clipboard: WM_CLIPBOARDUPDATE failed with %Rrc\n", vrc));

            break;
        }

        case WM_CHANGECBCHAIN:
        {
            LogFunc(("WM_CHANGECBCHAIN\n"));
            lresultRc = ShClWinHandleWMChangeCBChain(pWinCtx, hWnd, uMsg, wParam, lParam);
            break;
        }

        case WM_DRAWCLIPBOARD:
        {
            LogFunc(("WM_DRAWCLIPBOARD\n"));

            int vrc = RTCritSectEnter(&pWinCtx->CritSect);
            if (RT_SUCCESS(vrc))
            {
                const HWND hWndClipboardOwner = GetClipboardOwner();

                LogFunc(("WM_DRAWCLIPBOARD: hWndClipboardOwnerUs=%p, hWndNewClipboardOwner=%p\n",
                         pWinCtx->hWndClipboardOwnerUs, hWndClipboardOwner));

                if (pWinCtx->hWndClipboardOwnerUs != hWndClipboardOwner)
                {
                    int vrc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(vrc2);

                    /* Clipboard was updated by another application, retrieve formats and report back. */
                    vrc = vboxClipboardSvcWinSyncInternal(pCtx);
                }
                else
                {
                    int vrc2 = RTCritSectLeave(&pWinCtx->CritSect);
                    AssertRC(vrc2);
                }
            }

            lresultRc = ShClWinChainPassToNext(pWinCtx, uMsg, wParam, lParam);
            break;
        }

        case WM_TIMER:
        {
            int vrc = ShClWinHandleWMTimer(pWinCtx);
            AssertRC(vrc);

            break;
        }

        case WM_RENDERFORMAT:
        {
            /* Insert the requested clipboard format data into the clipboard. */
            const UINT       uFmtWin  = (UINT)wParam;
            const SHCLFORMAT uFmtVBox = ShClWinClipboardFormatToVBox(uFmtWin);

            LogFunc(("WM_RENDERFORMAT: uFmtWin=%u -> uFmtVBox=0x%x\n", uFmtWin, uFmtVBox));
            if (LogRelIsEnabled())
            {
                char *pszFmts = ShClFormatsToStrA(uFmtVBox);
                LogRel(("Shared Clipboard: Rendering Windows format %#x as VBox format %#x/'%s'\n",
                        uFmtWin, uFmtVBox, pszFmts ? pszFmts : "<alloc failed>"));
                RTStrFree(pszFmts);
            }
            if (   uFmtVBox      == VBOX_SHCL_FMT_NONE
                || pCtx->pClient == NULL)
            {
                /* Unsupported clipboard format is requested. */
                LogFunc(("WM_RENDERFORMAT unsupported format requested or client is not active\n"));
                ShClWinClear();
            }
            else
            {
                void    *pvData = NULL;
                uint32_t cbData = 0;
                int vrc = ShClSvcReadDataFromGuest(pCtx->pClient, uFmtVBox, &pvData, &cbData);
                if (RT_SUCCESS(vrc))
                {
                    /* Wrap HTML clipboard content info CF_HTML format if needed. */
                    if (uFmtVBox == VBOX_SHCL_FMT_HTML
                        && !ShClWinIsCFHTML((char *)pvData))
                    {
                        char *pszWrapped = NULL;
                        uint32_t cbWrapped = 0;
                        vrc = ShClWinConvertMIMEToCFHTML((char *)pvData, cbData, &pszWrapped, &cbWrapped);
                        if (RT_SUCCESS(vrc))
                        {
                            /* Replace buffer with wrapped data content. */
                            RTMemFree(pvData);
                            pvData = (void *)pszWrapped;
                            cbData = cbWrapped;
                        }
                        else
                            LogRel(("Shared Clipboard: cannot convert HTML clipboard into CF_HTML format, vrc=%Rrc\n", vrc));
                    }

                    vrc = ShClWinDataWrite(uFmtWin, pvData, cbData);
                    if (RT_FAILURE(vrc))
                        LogRel(("Shared Clipboard: Setting clipboard data for Windows host failed with %Rrc\n", vrc));

                    RTMemFree(pvData);
                    cbData = 0;
                }

                if (RT_FAILURE(vrc))
                    ShClWinClear();
            }

            break;
        }

        case WM_RENDERALLFORMATS:
        {
            LogFunc(("WM_RENDERALLFORMATS\n"));

            int vrc = ShClWinHandleWMRenderAllFormats(pWinCtx, hWnd);
            AssertRC(vrc);

            break;
        }

        case SHCL_WIN_WM_REPORT_FORMATS: /* Guest reported clipboard formats. */
        {
            /* Announce available formats. Do not insert data -- will be inserted in WM_RENDERFORMAT (or via IDataObject). */
            SHCLFORMATS fFormats = (uint32_t)lParam;
            LogFunc(("SHCL_WIN_WM_REPORT_FORMATS: fFormats=%#xn", fFormats));

            int vrc = ShClWinClearAndAnnounceFormats(pWinCtx, fFormats, hWnd);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (   RT_SUCCESS(vrc)
                && fFormats & VBOX_SHCL_FMT_URI_LIST)
            {
                /*
                 * Create our IDataObject implementation and push it to the Windows clibpoard.
                 * That way Windows will recognize that there is a data transfer available.
                 */
                ShClWinDataObject::CALLBACKS Callbacks;
                RT_ZERO(Callbacks);
                Callbacks.pfnTransferBegin = shClSvcWinDataObjectTransferBeginCallback;

                vrc = ShClWinTransferCreateAndSetDataObject(pWinCtx, pCtx, &Callbacks);
            }
#else
            RT_NOREF(vrc);
#endif
            LogFunc(("SHCL_WIN_WM_REPORT_FORMATS: lastErr=%ld\n", GetLastError()));
            break;
        }

        case WM_DESTROY:
        {
            LogFunc(("WM_DESTROY\n"));

            int vrc = ShClWinHandleWMDestroy(pWinCtx);
            AssertRC(vrc);

            PostQuitMessage(0);
            break;
        }

        default:
            lresultRc = DefWindowProc(hWnd, uMsg, wParam, lParam);
            break;
    }

    LogFlowFunc(("LEAVE hWnd=%p, WM_ %u -> %#zx\n", hWnd, uMsg, lresultRc));
    return lresultRc;
}

/**
 * Static helper function for having a per-client proxy window instances.
 */
static LRESULT CALLBACK vboxClipboardSvcWinWndProcInstance(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    LONG_PTR pUserData = GetWindowLongPtr(hWnd, GWLP_USERDATA);
    AssertPtrReturn(pUserData, 0);

    PSHCLCONTEXT pCtx = reinterpret_cast<PSHCLCONTEXT>(pUserData);
    if (pCtx)
        return vboxClipboardSvcWinWndProcMain(pCtx, hWnd, uMsg, wParam, lParam);

    return 0;
}

/**
 * Static helper function for routing Windows messages to a specific
 * proxy window instance.
 */
static LRESULT CALLBACK vboxClipboardSvcWinWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    /* Note: WM_NCCREATE is not the first ever message which arrives, but
     *       early enough for us. */
    if (uMsg == WM_NCCREATE)
    {
        LogFlowFunc(("WM_NCCREATE\n"));

        LPCREATESTRUCT pCS = (LPCREATESTRUCT)lParam;
        AssertPtr(pCS);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pCS->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)vboxClipboardSvcWinWndProcInstance);

        return vboxClipboardSvcWinWndProcInstance(hWnd, uMsg, wParam, lParam);
    }

    /* No window associated yet. */
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

DECLCALLBACK(int) vboxClipboardSvcWinThread(RTTHREAD hThreadSelf, void *pvUser)
{
    LogFlowFuncEnter();

    bool fThreadSignalled = false;

    const PSHCLCONTEXT pCtx    = (PSHCLCONTEXT)pvUser;
    AssertPtr(pCtx);
    const PSHCLWINCTX  pWinCtx = &pCtx->Win;

    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);

    /* Register the Window Class. */
    WNDCLASS wc;
    RT_ZERO(wc);

    wc.style         = CS_NOCLOSE;
    wc.lpfnWndProc   = vboxClipboardSvcWinWndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);

    /* Register an unique wnd class name. */
#ifdef UNICODE
    RTUTF16 szWndClassName[32];
    RTUtf16Printf(szWndClassName, sizeof(szWndClassName),
                  "%s-%RU64", RT_LSTR(SHCL_WIN_WNDCLASS_NAME), RTThreadGetNative(hThreadSelf));
#else
    char szWndClassName[32];
    RTStrPrintf2(szWndClassName, sizeof(szWndClassName),
                 "%s-%RU64", SHCL_WIN_WNDCLASS_NAME, RTThreadGetNative(hThreadSelf));
#endif
    wc.lpszClassName = szWndClassName;

    int vrc;

    ATOM atomWindowClass = RegisterClass(&wc);
    if (atomWindowClass == 0)
    {
        LogFunc(("Failed to register window class\n"));
        vrc = VERR_NOT_SUPPORTED;
    }
    else
    {
        /* Create a window and make it a clipboard viewer. */
        pWinCtx->hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                       szWndClassName, szWndClassName,
                                       WS_POPUPWINDOW,
                                       -200, -200, 100, 100, NULL, NULL, hInstance, pCtx /* lpParam */);
        if (pWinCtx->hWnd == NULL)
        {
            LogFunc(("Failed to create window\n"));
            vrc = VERR_NOT_SUPPORTED;
        }
        else
        {
            SetWindowPos(pWinCtx->hWnd, HWND_TOPMOST, -200, -200, 0, 0,
                         SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

            vrc = ShClWinChainAdd(&pCtx->Win);
            if (RT_SUCCESS(vrc))
            {
                if (!ShClWinIsNewAPI(&pWinCtx->newAPI))
                    pWinCtx->oldAPI.timerRefresh = SetTimer(pWinCtx->hWnd, 0, 10 * 1000, NULL);
            }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (RT_SUCCESS(vrc))
            {
                HRESULT hr = OleInitialize(NULL);
                if (FAILED(hr))
                {
                    LogRel(("Shared Clipboard: Initializing window thread OLE failed (%Rhrc) -- file transfers unavailable\n", hr));
                    /* Not critical, the rest of the clipboard might work. */
                }
                else
                    LogRel(("Shared Clipboard: Initialized window thread OLE\n"));
            }
#endif
            int vrc2 = RTThreadUserSignal(hThreadSelf);
            AssertRC(vrc2);

            fThreadSignalled = true;

            MSG msg;
            BOOL msgret = 0;
            while ((msgret = GetMessage(&msg, NULL, 0, 0)) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            /*
             * Window procedure can return error, * but this is exceptional situation that should be
             * identified in testing.
             */
            Assert(msgret >= 0);
            LogFunc(("Message loop finished. GetMessage returned %d, message id: %d \n", msgret, msg.message));

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            OleSetClipboard(NULL); /* Make sure to flush the clipboard on destruction. */
            OleUninitialize();
#endif
        }
    }

    pWinCtx->hWnd = NULL;

    if (atomWindowClass != 0)
    {
        UnregisterClass(szWndClassName, hInstance);
        atomWindowClass = 0;
    }

    if (!fThreadSignalled)
    {
        int vrc2 = RTThreadUserSignal(hThreadSelf);
        AssertRC(vrc2);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

static int shClBackendReportFormatsToGuestAndMain(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
#ifdef VBOX_COM_INPROC
    return GuestShCl::GetInst()->ReportFormatsToGuest(pClient, fFormats, SHCLSOURCE_LOCAL);
#else
    return ShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats);
#endif
}

/**
 * Synchronizes the host and the guest clipboard formats by sending all supported host clipboard
 * formats to the guest.
 *
 * @returns VBox status code, VINF_NO_CHANGE if no synchronization was required.
 * @param   pCtx                Clipboard context to synchronize.
 */
static int vboxClipboardSvcWinSyncInternal(PSHCLCONTEXT pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int vrc;

    if (pCtx->pClient)
    {
        SHCLFORMATS fFormats = 0;
        vrc = ShClWinGetFormats(&pCtx->Win, &fFormats);
        if (RT_SUCCESS(vrc))
            vrc = shClBackendReportFormatsToGuestAndMain(pCtx->pClient, fFormats);
    }
    else /* If we don't have any client data (yet), bail out. */
        vrc = VINF_NO_CHANGE;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}


/*********************************************************************************************************************************
*   Backend implementation                                                                                                       *
*********************************************************************************************************************************/
int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable)
{
    pBackend->pHelpers = pTable->pHelpers;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr))
    {
        LogRel(("Shared Clipboard: Initializing OLE failed (%Rhrc) -- file transfers unavailable\n", hr));
        /* Not critical, the rest of the clipboard might work. */
    }
    else
        LogRel(("Shared Clipboard: Initialized OLE\n"));
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    return VINF_SUCCESS;
}

void ShClBackendDestroy(PSHCLBACKEND pBackend)
{
    RT_NOREF(pBackend);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    OleSetClipboard(NULL); /* Make sure to flush the clipboard on destruction. */
    OleUninitialize();
#endif
}

int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    LogFlowFuncEnter();

    int vrc;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    if (pCtx)
    {
        vrc = ShClWinCtxInit(&pCtx->Win);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTThreadCreate(&pCtx->hThread, vboxClipboardSvcWinThread, pCtx /* pvUser */, _64K /* Stack size */,
                                RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "ShClWin");
            if (RT_SUCCESS(vrc))
            {
                int vrc2 = RTThreadUserWait(pCtx->hThread, RT_MS_30SEC /* Timeout in ms */);
                AssertRC(vrc2);
            }
        }

        pClient->State.pCtx = pCtx;
        pClient->State.pCtx->pClient = pClient;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /*
         * Set callbacks.
         * Those will be registered within ShClSvcTransferInit() when a new transfer gets initialized.
         */
        RT_ZERO(pClient->Transfers.Callbacks);

        pClient->Transfers.Callbacks.pvUser = pCtx; /* Assign context as user-provided callback data. */
        pClient->Transfers.Callbacks.cbUser = sizeof(SHCLCONTEXT);

        pClient->Transfers.Callbacks.pfnOnCreated     = shClSvcWinTransferOnCreatedCallback;
        pClient->Transfers.Callbacks.pfnOnInitialize  = shClSvcWinTransferOnInitializeCallback;
        pClient->Transfers.Callbacks.pfnOnInitialized = shClSvcWinTransferOnInitializedCallback;
        pClient->Transfers.Callbacks.pfnOnDestroy     = shClSvcWinTransferOnDestroyCallback;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    }
    else
        vrc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    /* Sync the host clipboard content with the client. */
    return vboxClipboardSvcWinSyncInternal(pClient->State.pCtx);
}

int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend);

    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    int vrc = VINF_SUCCESS;

    PSHCLCONTEXT pCtx = pClient->State.pCtx;
    if (pCtx)
    {
        if (pCtx->Win.hWnd)
            PostMessage(pCtx->Win.hWnd, WM_DESTROY, 0 /* wParam */, 0 /* lParam */);

        if (pCtx->hThread != NIL_RTTHREAD)
        {
            LogFunc(("Waiting for thread to terminate ...\n"));

            /* Wait for the window thread to terminate. */
            vrc = RTThreadWait(pCtx->hThread, RT_MS_30SEC /* Timeout in ms */, NULL);
            if (RT_FAILURE(vrc))
                LogRel(("Shared Clipboard: Waiting for window thread termination failed with vrc=%Rrc\n", vrc));

            pCtx->hThread = NIL_RTTHREAD;
        }

        ShClWinCtxDestroy(&pCtx->Win);

        if (RT_SUCCESS(vrc))
        {
            RTMemFree(pCtx);
            pCtx = NULL;

            pClient->State.pCtx = NULL;
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    PSHCLCONTEXT pCtx = pClient->State.pCtx;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    LogFlowFunc(("fFormats=0x%x, hWnd=%p\n", fFormats, pCtx->Win.hWnd));

    /*
     * The guest announced formats. Forward to the window thread.
     */
    PostMessage(pCtx->Win.hWnd, SHCL_WIN_WM_REPORT_FORMATS, 0 /* wParam */, fFormats /* lParam */);

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

int ShClBackendReportFormatsToGuest(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend);

    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    int vrc;

    uint32_t uMode = pClient->State.uMode;
    if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
        || uMode == VBOX_SHCL_MODE_HOST_TO_GUEST)
    { /* likely */ }
    else
        return VINF_SUCCESS;

    fFormats = shClSvcHandleFormats(true /* fHostToGuest */, pClient, fFormats);

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

int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx,
                        SHCLFORMAT uFmt, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    AssertPtrReturn(pClient,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCmdCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    RT_NOREF(pBackend, pCmdCtx);

    AssertPtrReturn(pClient->State.pCtx, VERR_INVALID_POINTER);

    LogFlowFunc(("uFmt=%#x\n", uFmt));

    HANDLE hClip = NULL;

    const PSHCLWINCTX pWinCtx = &pClient->State.pCtx->Win;

    /*
     * The guest wants to read data in the given format.
     */
    int vrc = ShClWinOpen(pWinCtx->hWnd);
    if (RT_SUCCESS(vrc))
    {
        if (uFmt & VBOX_SHCL_FMT_BITMAP)
        {
            LogFunc(("CF_DIB\n"));
            hClip = GetClipboardData(CF_DIB);
            if (hClip != NULL)
            {
                LPVOID lp = GlobalLock(hClip);
                if (lp != NULL)
                {
                    vrc = vboxClipboardSvcWinDataGet(VBOX_SHCL_FMT_BITMAP, lp, GlobalSize(hClip),
                                                    pvData, cbData, pcbActual);
                    GlobalUnlock(hClip);
                }
                else
                {
                    hClip = NULL;
                }
            }
        }
        else if (uFmt & VBOX_SHCL_FMT_UNICODETEXT)
        {
            LogFunc(("CF_UNICODETEXT\n"));
            hClip = GetClipboardData(CF_UNICODETEXT);
            if (hClip != NULL)
            {
                LPWSTR uniString = (LPWSTR)GlobalLock(hClip);
                if (uniString != NULL)
                {
                    vrc = vboxClipboardSvcWinDataGet(VBOX_SHCL_FMT_UNICODETEXT, uniString, (lstrlenW(uniString) + 1) * 2,
                                                    pvData, cbData, pcbActual);
                    GlobalUnlock(hClip);
                }
                else
                {
                    hClip = NULL;
                }
            }
        }
        else if (uFmt & VBOX_SHCL_FMT_HTML)
        {
            LogFunc(("SHCL_WIN_REGFMT_HTML\n"));
#ifdef UNICODE
            UINT uRegFmt = RegisterClipboardFormat(RT_LSTR(SHCL_WIN_REGFMT_HTML));
#else
            UINT uRegFmt = RegisterClipboardFormat(SHCL_WIN_REGFMT_HTML);
#endif
            if (uRegFmt != 0)
            {
                hClip = GetClipboardData(uRegFmt);
                if (hClip != NULL)
                {
                    LPVOID lp = GlobalLock(hClip);
                    if (lp != NULL)
                    {
                        vrc = vboxClipboardSvcWinDataGet(VBOX_SHCL_FMT_HTML, lp, GlobalSize(hClip),
                                                        pvData, cbData, pcbActual);
#ifdef LOG_ENABLED
                        if (RT_SUCCESS(vrc))
                        {
                            LogFlowFunc(("Raw HTML clipboard data from host:\n"));
                            ShClDbgDumpHtml((char *)pvData, cbData);
                        }
#endif
                        GlobalUnlock(hClip);
                    }
                    else
                    {
                        hClip = NULL;
                    }
                }
            }
        }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        else if (uFmt & VBOX_SHCL_FMT_URI_LIST)
        {
            hClip = GetClipboardData(CF_HDROP);
            if (hClip)
            {
                HDROP hDrop = (HDROP)GlobalLock(hClip);
                if (hDrop)
                {
                    char    *pszList = NULL;
                    uint32_t cbList;
                    vrc = ShClWinTransferDropFilesToStringList((DROPFILES *)hDrop, &pszList, &cbList);

                    GlobalUnlock(hClip);

                    if (RT_SUCCESS(vrc))
                    {
                        *pcbActual = cbList;
                        if (cbList <= cbData)
                            memcpy(pvData, pszList, cbList);
                        else
                            vrc = VINF_BUFFER_OVERFLOW;

                        RTStrFree(pszList);
                    }
                }
                else
                {
                    DWORD const dwLastErr = GetLastError();
                    vrc = RTErrConvertFromWin32(dwLastErr);
                    if (RT_SUCCESS(vrc))
                        vrc = VERR_INVALID_HANDLE;
                    LogRel(("Shared Clipboard: Unable to lock clipboard data, last error: %ld\n", dwLastErr));
                }
            }
            else
            {
                DWORD const dwLastErr = GetLastError();
                vrc = RTErrConvertFromWin32(dwLastErr);
                if (RT_SUCCESS(vrc))
                    vrc = VERR_NOT_FOUND;
                LogRel(("Shared Clipboard: Unable to retrieve clipboard data from clipboard (CF_HDROP), last error: %ld\n",
                        dwLastErr));
            }
        }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
        ShClWinClose();
    }

    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error reading host clipboard data in format %#x from Windows, vrc=%Rrc\n", uFmt, vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
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

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# ifndef UNIT_TEST
/**
 * Handles transfer status replies from the guest.
 */
int ShClBackendTransferHandleStatusReply(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_NOREF(pBackend, pClient, pTransfer, enmSource, enmStatus, rcStatus);

    return VINF_SUCCESS;
}
# endif /* !UNIT_TEST */


/*********************************************************************************************************************************
*   Provider interface implementation                                                                                            *
*********************************************************************************************************************************/

/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) shClSvcWinTransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    LogFlowFuncEnter();

    PSHCLCLIENT pClient = (PSHCLCLIENT)pCtx->pvUser;
    AssertPtr(pClient);

    AssertPtr(pClient->State.pCtx);
    PSHCLWINCTX pWin = &pClient->State.pCtx->Win;

    int vrc = ShClWinTransferGetRootsFromClipboard(pWin, pCtx->pTransfer);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
