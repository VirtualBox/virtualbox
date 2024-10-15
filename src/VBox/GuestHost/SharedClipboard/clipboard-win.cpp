/* $Id: clipboard-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard: Windows-specific functions for clipboard handling.
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

#include <VBox/GuestHost/SharedClipboard.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/win/windows.h>
# include <iprt/win/shlobj.h> /* For CFSTR_FILEDESCRIPTORXXX + CFSTR_FILECONTENTS. */
# include <iprt/utf16.h>
#endif

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>

#include <VBox/HostServices/VBoxClipboardSvc.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#include <VBox/GuestHost/SharedClipboard-win.h>
#include <VBox/GuestHost/clipboard-helper.h>


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int ShClWinTransferDropFilesToStringList(DROPFILES *pDropFiles, char **ppszList, uint32_t *pcbList);
#endif


/**
 * Opens the clipboard of a specific window.
 *
 * @returns VBox status code.
 * @param   hWnd                Handle of window to open clipboard for.
 */
int ShClWinOpen(HWND hWnd)
{
    /* "OpenClipboard fails if another window has the clipboard open."
     * So try a few times and wait up to 1 second.
     */
    BOOL fOpened = FALSE;

    LogFlowFunc(("hWnd=%p\n", hWnd));

    int i = 0;
    for (;;)
    {
        if (OpenClipboard(hWnd))
        {
            fOpened = TRUE;
            break;
        }

        if (i >= 10) /* sleep interval = [1..512] ms */
            break;

        RTThreadSleep(1 << i);
        ++i;
    }

#ifdef LOG_ENABLED
    if (i > 0)
        LogFlowFunc(("%d times tried to open clipboard\n", i + 1));
#endif

    int rc;
    if (fOpened)
        rc = VINF_SUCCESS;
    else
    {
        const DWORD dwLastErr = GetLastError();
        rc = RTErrConvertFromWin32(dwLastErr);
        LogRel(("Shared Clipboard: Failed to open clipboard, rc=%Rrc (0x%x)\n", rc, dwLastErr));
    }

    return rc;
}

/**
 * Closes the clipboard for the current thread.
 *
 * @returns VBox status code.
 */
int ShClWinClose(void)
{
    int rc;

    const BOOL fRc = CloseClipboard();
    if (RT_UNLIKELY(!fRc))
    {
        const DWORD dwLastErr = GetLastError();
        if (dwLastErr == ERROR_CLIPBOARD_NOT_OPEN)
        {
            rc = VINF_SUCCESS; /* Not important, so just report success instead. */
        }
        else
        {
            rc = RTErrConvertFromWin32(dwLastErr);
            LogFunc(("Failed with %Rrc (0x%x)\n", rc, dwLastErr));
        }
    }
    else
        rc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Clears the clipboard for the current thread.
 *
 * @returns VBox status code.
 */
int ShClWinClear(void)
{
    LogFlowFuncEnter();
    if (EmptyClipboard())
        return VINF_SUCCESS;

    DWORD const dwLastErr = GetLastError();
    if (dwLastErr == ERROR_CLIPBOARD_NOT_OPEN) /* Can happen if we didn't open the clipboard before. Just ignore this. */
        return VINF_SUCCESS;

    int const rc = RTErrConvertFromWin32(dwLastErr);
    LogRel2(("Shared Clipboard: Clearing Windows clipboard failed with %Rrc (0x%x)\n", rc, dwLastErr));

    return rc;
}

/**
 * Initializes a Shared Clipboard Windows context.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Shared Clipboard Windows context to initialize.
 */
int ShClWinCtxInit(PSHCLWINCTX pWinCtx)
{
    int rc = RTCritSectInit(&pWinCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* Check that new Clipboard API is available. */
        ShClWinCheckAndInitNewAPI(&pWinCtx->newAPI);
        /* Do *not* check the rc, as the call might return VERR_SYMBOL_NOT_FOUND is the new API isn't available. */

        pWinCtx->hWnd                 = NULL;
        pWinCtx->hWndClipboardOwnerUs = NULL;
        pWinCtx->hWndNextInChain      = NULL;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        pWinCtx->pDataObjInFlight = NULL;
#endif
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a Shared Clipboard Windows context.
 *
 * @param   pWinCtx             Shared Clipboard Windows context to destroy.
 */
void ShClWinCtxDestroy(PSHCLWINCTX pWinCtx)
{
    if (!pWinCtx)
        return;

    LogFlowFuncEnter();

    if (RTCritSectIsInitialized(&pWinCtx->CritSect))
    {
        int rc2 = RTCritSectDelete(&pWinCtx->CritSect);
        AssertRC(rc2);
    }
}

/**
 * Checks and initializes function pointer which are required for using
 * the new clipboard API.
 *
 * @returns VBox status code, or VERR_SYMBOL_NOT_FOUND if the new API is not available.
 * @param   pAPI                Where to store the retrieved function pointers.
 *                              Will be set to NULL if the new API is not available.
 */
int ShClWinCheckAndInitNewAPI(PSHCLWINAPINEW pAPI)
{
    RTLDRMOD hUser32 = NIL_RTLDRMOD;
    int rc = RTLdrLoadSystem("User32.dll", /* fNoUnload = */ true, &hUser32);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hUser32, "AddClipboardFormatListener", (void **)&pAPI->pfnAddClipboardFormatListener);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(hUser32, "RemoveClipboardFormatListener", (void **)&pAPI->pfnRemoveClipboardFormatListener);
        }

        RTLdrClose(hUser32);
    }

    if (RT_SUCCESS(rc))
    {
        LogRel(("Shared Clipboard: New Clipboard API enabled\n"));
    }
    else
    {
        RT_BZERO(pAPI, sizeof(SHCLWINAPINEW));
        LogRel(("Shared Clipboard: New Clipboard API not available (%Rrc)\n", rc));
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns if the new clipboard API is available or not.
 *
 * @returns @c true if the new API is available, or @c false if not.
 * @param   pAPI                Structure used for checking if the new clipboard API is available or not.
 */
bool ShClWinIsNewAPI(PSHCLWINAPINEW pAPI)
{
    if (!pAPI)
        return false;
    return pAPI->pfnAddClipboardFormatListener != NULL;
}

/**
 * Adds ourselves into the chain of cliboard listeners.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows clipboard context to use to add ourselves.
 */
int ShClWinChainAdd(PSHCLWINCTX pCtx)
{
    const PSHCLWINAPINEW pAPI = &pCtx->newAPI;

    BOOL fRc;
    if (ShClWinIsNewAPI(pAPI))
        fRc = pAPI->pfnAddClipboardFormatListener(pCtx->hWnd);
    else
    {
        SetLastError(NO_ERROR);
        pCtx->hWndNextInChain = SetClipboardViewer(pCtx->hWnd);
        fRc = pCtx->hWndNextInChain != NULL || GetLastError() == NO_ERROR;
    }

    int rc = VINF_SUCCESS;

    if (!fRc)
    {
        const DWORD dwLastErr = GetLastError();
        rc = RTErrConvertFromWin32(dwLastErr);
        LogFunc(("Failed with %Rrc (0x%x)\n", rc, dwLastErr));
    }

    return rc;
}

/**
 * Remove ourselves from the chain of cliboard listeners
 *
 * @returns VBox status code.
 * @param   pCtx                Windows clipboard context to use to remove ourselves.
 */
int ShClWinChainRemove(PSHCLWINCTX pCtx)
{
    if (!pCtx->hWnd)
        return VINF_SUCCESS;

    const PSHCLWINAPINEW pAPI = &pCtx->newAPI;

    BOOL fRc;
    if (ShClWinIsNewAPI(pAPI))
    {
        fRc = pAPI->pfnRemoveClipboardFormatListener(pCtx->hWnd);
    }
    else
    {
        fRc = ChangeClipboardChain(pCtx->hWnd, pCtx->hWndNextInChain);
        if (fRc)
            pCtx->hWndNextInChain = NULL;
    }

    int rc = VINF_SUCCESS;

    if (!fRc)
    {
        const DWORD dwLastErr = GetLastError();
        rc = RTErrConvertFromWin32(dwLastErr);
        LogFunc(("Failed with %Rrc (0x%x)\n", rc, dwLastErr));
    }

    return rc;
}

/**
 * Callback which is invoked when we have successfully pinged ourselves down the
 * clipboard chain.  We simply unset a boolean flag to say that we are responding.
 * There is a race if a ping returns after the next one is initiated, but nothing
 * very bad is likely to happen.
 *
 * @param   hWnd                Window handle to use for this callback. Not used currently.
 * @param   uMsg                Message to handle. Not used currently.
 * @param   dwData              Pointer to user-provided data. Contains our Windows clipboard context.
 * @param   lResult             Additional data to pass. Not used currently.
 */
VOID CALLBACK ShClWinChainPingProc(HWND hWnd, UINT uMsg, ULONG_PTR dwData, LRESULT lResult) RT_NOTHROW_DEF
{
    RT_NOREF(hWnd);
    RT_NOREF(uMsg);
    RT_NOREF(lResult);

    /** @todo r=andy Why not using SetWindowLongPtr for keeping the context? */
    PSHCLWINCTX pCtx = (PSHCLWINCTX)dwData;
    AssertPtrReturnVoid(pCtx);

    pCtx->oldAPI.fCBChainPingInProcess = FALSE;
}

/**
 * Passes a window message to the next window in the clipboard chain.
 *
 * @returns LRESULT
 * @param   pWinCtx             Window context to use.
 * @param   msg                 Window message to pass.
 * @param   wParam              WPARAM to pass.
 * @param   lParam              LPARAM to pass.
 */
LRESULT ShClWinChainPassToNext(PSHCLWINCTX pWinCtx,
                                          UINT msg, WPARAM wParam, LPARAM lParam)
{
    LogFlowFuncEnter();

    LRESULT lresultRc = 0;

    if (pWinCtx->hWndNextInChain)
    {
        LogFunc(("hWndNextInChain=%p\n", pWinCtx->hWndNextInChain));

        /* Pass the message to next window in the clipboard chain. */
        DWORD_PTR dwResult;
        lresultRc = SendMessageTimeout(pWinCtx->hWndNextInChain, msg, wParam, lParam, 0,
                                       SHCL_WIN_CBCHAIN_TIMEOUT_MS, &dwResult);
        if (!lresultRc)
            lresultRc = dwResult;
    }

    LogFlowFunc(("lresultRc=%ld\n", lresultRc));
    return lresultRc;
}

/**
 * Converts a (registered or standard) Windows clipboard format to a VBox clipboard format.
 *
 * @returns Converted VBox clipboard format, or VBOX_SHCL_FMT_NONE if not found.
 * @param   uFormat             Windows clipboard format to convert.
 */
SHCLFORMAT ShClWinClipboardFormatToVBox(UINT uFormat)
{
    /* Insert the requested clipboard format data into the clipboard. */
    SHCLFORMAT vboxFormat = VBOX_SHCL_FMT_NONE;

    switch (uFormat)
    {
        case CF_UNICODETEXT:
            vboxFormat = VBOX_SHCL_FMT_UNICODETEXT;
            break;

        case CF_DIB:
            vboxFormat = VBOX_SHCL_FMT_BITMAP;
            break;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /* CF_HDROP handles file system entries which are locally present
         * on source for transferring to the target.
         *
         * This does *not* invoke any IDataObject / IStream implementations! */
        case CF_HDROP:
            vboxFormat = VBOX_SHCL_FMT_URI_LIST;
            break;
#endif

        default:
            if (uFormat >= 0xC000) /** Formats registered with RegisterClipboardFormat() start at this index. */
            {
                TCHAR szFormatName[256]; /** @todo r=andy Do we need Unicode support here as well? */
                int cActual = GetClipboardFormatName(uFormat, szFormatName, sizeof(szFormatName) / sizeof(TCHAR));
                if (cActual)
                {
                    LogFlowFunc(("uFormat=%u -> szFormatName=%s\n", uFormat, szFormatName));

                    if (RTStrCmp(szFormatName, SHCL_WIN_REGFMT_HTML) == 0)
                        vboxFormat = VBOX_SHCL_FMT_HTML;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                    /* These types invoke our IDataObject / IStream implementations. */
                    else if (   (RTStrCmp(szFormatName, CFSTR_FILEDESCRIPTORA) == 0)
                             || (RTStrCmp(szFormatName, CFSTR_FILECONTENTS)    == 0))
                        vboxFormat = VBOX_SHCL_FMT_URI_LIST;
                    /** @todo Do we need to handle CFSTR_FILEDESCRIPTORW here as well? */
#endif
                }
            }
            break;
    }

    LogFlowFunc(("uFormat=%u -> vboxFormat=0x%x\n", uFormat, vboxFormat));
    return vboxFormat;
}

/**
 * Retrieves all supported clipboard formats of a specific clipboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Windows clipboard context to retrieve formats for.
 * @param   pfFormats           Where to store the retrieved formats.
 */
int ShClWinGetFormats(PSHCLWINCTX pCtx, PSHCLFORMATS pfFormats)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFormats, VERR_INVALID_POINTER);

    SHCLFORMATS fFormats = VBOX_SHCL_FMT_NONE;

    /* Query list of available formats and report to host. */
    int rc = ShClWinOpen(pCtx->hWnd);
    if (RT_SUCCESS(rc))
    {
        UINT uCurFormat = 0; /* Must be set to zero for EnumClipboardFormats(). */
        while ((uCurFormat = EnumClipboardFormats(uCurFormat)) != 0)
            fFormats |= ShClWinClipboardFormatToVBox(uCurFormat);

        int rc2 = ShClWinClose();
        AssertRC(rc2);
        LogFlowFunc(("fFormats=%#x\n", fFormats));
    }
    else
        LogFunc(("Failed with rc=%Rrc (fFormats=%#x)\n", rc, fFormats));

    *pfFormats = fFormats;
    return rc;
}

/**
 * Extracts a field value from CF_HTML data.
 *
 * @returns VBox status code.
 * @param   pszSrc      source in CF_HTML format.
 * @param   pszOption   Name of CF_HTML field.
 * @param   puValue     Where to return extracted value of CF_HTML field.
 */
int ShClWinGetCFHTMLHeaderValue(const char *pszSrc, const char *pszOption, uint32_t *puValue)
{
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszOption, VERR_INVALID_POINTER);

    int rc = VERR_INVALID_PARAMETER;

    const char *pszOptionValue = RTStrStr(pszSrc, pszOption);
    if (pszOptionValue)
    {
        size_t cchOption = strlen(pszOption);
        Assert(cchOption);

        rc = RTStrToUInt32Ex(pszOptionValue + cchOption, NULL, 10, puValue);
    }
    return rc;
}

/**
 * Check that the source string contains CF_HTML struct.
 *
 * @returns @c true if the @a pszSource string is in CF_HTML format.
 * @param   pszSource   Source string to check.
 */
bool ShClWinIsCFHTML(const char *pszSource)
{
    return    RTStrStr(pszSource, "Version:") != NULL
           && RTStrStr(pszSource, "StartHTML:") != NULL;
}

/**
 * Converts clipboard data from CF_HTML format to MIME clipboard format.
 *
 * Returns allocated buffer that contains html converted to text/html mime type
 *
 * @returns VBox status code.
 * @param   pszSource   The input.
 * @param   cch         The length of the input.
 * @param   ppszOutput  Where to return the result.  Free using RTMemFree.
 * @param   pcbOutput   Where to the return length of the result (bytes/chars).
 */
int ShClWinConvertCFHTMLToMIME(const char *pszSource, const uint32_t cch, char **ppszOutput, uint32_t *pcbOutput)
{
    Assert(pszSource);
    Assert(cch);
    Assert(ppszOutput);
    Assert(pcbOutput);

    uint32_t offStart;
    int rc = ShClWinGetCFHTMLHeaderValue(pszSource, "StartFragment:", &offStart);
    if (RT_SUCCESS(rc))
    {
        uint32_t offEnd;
        rc = ShClWinGetCFHTMLHeaderValue(pszSource, "EndFragment:", &offEnd);
        if (RT_SUCCESS(rc))
        {
            if (   offStart > 0
                && offEnd > 0
                && offEnd >= offStart
                && offEnd <= cch)
            {
                uint32_t cchSubStr = offEnd - offStart;
                char *pszResult = (char *)RTMemAlloc(cchSubStr + 1);
                if (pszResult)
                {
                    rc = RTStrCopyEx(pszResult, cchSubStr + 1, pszSource + offStart, cchSubStr);
                    if (RT_SUCCESS(rc))
                    {
                        *ppszOutput = pszResult;
                        *pcbOutput  = (uint32_t)(cchSubStr + 1);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        LogRel(("Shared Clipboard: Unknown CF_HTML format, expected EndFragment, rc=%Rrc\n", rc));
                        RTMemFree(pszResult);
                    }
                }
                else

                    rc = VERR_NO_MEMORY;
            }
            else
            {
                LogRel(("Shared Clipboard: Error: CF_HTML out of bounds - offStart=%#x, offEnd=%#x, cch=%#x\n", offStart, offEnd, cch));
                rc = VERR_INVALID_PARAMETER;
            }
        }
        else
        {
            LogRel(("Shared Clipboard: Error: Unknown CF_HTML format, expected EndFragment, rc=%Rrc\n", rc));
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
    {
        LogRel(("Shared Clipboard: Error: Unknown CF_HTML format, expected StartFragment, rc=%Rrc\n", rc));
        rc = VERR_INVALID_PARAMETER;
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: HTML to MIME conversion failed with %Rrc\n", rc));

    return rc;
}

/**
 * Converts source UTF-8 MIME HTML clipboard data to UTF-8 CF_HTML format.
 *
 * This is just encapsulation work, slapping a header on the data.
 *
 * It allocates [..]
 *
 * Calculations:
 *   Header length = format Length + (2*(10 - 5('%010d'))('digits')) - 2('%s') = format length + 8
 *   EndHtml       = Header length + fragment length
 *   StartHtml     = 105(constant)
 *   StartFragment = 141(constant) may vary if the header html content will be extended
 *   EndFragment   = Header length + fragment length - 38(ending length)
 *
 * For more format details, check out:
 * https://docs.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/platform-apis/aa767917(v=vs.85)
 *
 * @returns VBox status code.
 * @param   pszSource   Source buffer that contains utf-16 string in mime html format
 * @param   cb          Size of source buffer in bytes
 * @param   ppszOutput  Where to return the allocated output buffer to put converted UTF-8
 *                      CF_HTML clipboard data.  This function allocates memory for this.
 * @param   pcbOutput   Where to return the size of allocated result buffer in bytes/chars, including zero terminator
 *
 * @note    output buffer should be free using RTMemFree()
 * @note    Everything inside of fragment can be UTF8. Windows allows it. Everything in header should be Latin1.
 */
int ShClWinConvertMIMEToCFHTML(const char *pszSource, size_t cb, char **ppszOutput, uint32_t *pcbOutput)
{
    Assert(ppszOutput);
    Assert(pcbOutput);
    Assert(pszSource);
    Assert(cb);

    /*
     * Check that input UTF-8 and properly zero terminated.
     * Note! The zero termination may come earlier than 'cb' - 1, that's fine.
     */
    int rc = RTStrValidateEncodingEx(pszSource, cb, RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        LogRel2(("Shared Clipboard: Error: Invalid source fragment.for HTML MIME data, rc=%Rrc\n", rc));
        return rc;
    }
    size_t const cchFragment = strlen(pszSource); /* Unfortunately the validator doesn't return the length. */

    /*
     * @StartHtml     - Absolute offset of <html>
     * @EndHtml       - Size of the whole resulting text (excluding ending zero char)
     * @StartFragment - Absolute position after <!--StartFragment-->
     * @EndFragment   - Absolute position of <!--EndFragment-->
     *
     * Note! The offset are zero padded to max width so we don't have any variations due to those.
     * Note! All values includes CRLFs inserted into text.
     *
     * Calculations:
     *   Header length = Format sample length - 2 ('%s')
     *   EndHtml       = Header length + fragment length
     *   StartHtml     = 101(constant)
     *   StartFragment = 137(constant)
     *   EndFragment   = Header length + fragment length - 38 (ending length)
     */
    static const char s_szFormatSample[] =
    /*   0:   */ "Version:1.0\r\n"
    /*  13:   */ "StartHTML:000000101\r\n"
    /*  34:   */ "EndHTML:%0000009u\r\n" // END HTML = Header length + fragment length
    /*  53:   */ "StartFragment:000000137\r\n"
    /*  78:   */ "EndFragment:%0000009u\r\n"
    /* 101:   */ "<html>\r\n"
    /* 109:   */ "<body>\r\n"
    /* 117:   */ "<!--StartFragment-->"
    /* 137:   */ "%s"
    /* 137+2: */ "<!--EndFragment-->\r\n"
    /* 157+2: */ "</body>\r\n"
    /* 166+2: */ "</html>\r\n"
    /* 175+2: */ ;
    AssertCompile(sizeof(s_szFormatSample) == 175 + 2 + 1);

    /* Calculate parameters of the CF_HTML header */
    size_t const cchHeader      = sizeof(s_szFormatSample) - 2 /*%s*/ - 1 /*'\0'*/;
    size_t const offEndHtml     = cchHeader + cchFragment;
    size_t const offEndFragment = cchHeader + cchFragment - 38; /* 175-137 = 38 */
    char *pszResult = (char *)RTMemAlloc(offEndHtml + 1);
    AssertLogRelReturn(pszResult, VERR_NO_MEMORY);

    /* Format resulting CF_HTML string: */
    size_t cchFormatted = RTStrPrintf(pszResult, offEndHtml + 1, s_szFormatSample, offEndHtml, offEndFragment, pszSource);
    Assert(offEndHtml == cchFormatted);

#ifdef VBOX_STRICT
    /*
     * Check the calculations.
     */

    /* check 'StartFragment:' value */
    static const char s_szStartFragment[] = "<!--StartFragment-->";
    const char *pszRealStartFragment = RTStrStr(pszResult, s_szStartFragment);
    Assert(&pszRealStartFragment[sizeof(s_szStartFragment) - 1] - pszResult == 137);

    /* check 'EndFragment:' value */
    static const char s_szEndFragment[] = "<!--EndFragment-->";
    const char *pszRealEndFragment = RTStrStr(pszResult, s_szEndFragment);
    Assert((size_t)(pszRealEndFragment - pszResult) == offEndFragment);
#endif

    *ppszOutput = pszResult;
    *pcbOutput = (uint32_t)cchFormatted + 1;
    Assert(*pcbOutput == cchFormatted + 1);

    return VINF_SUCCESS;
}

/**
 * Handles the WM_CHANGECBCHAIN code.
 *
 * @returns LRESULT
 * @param   pWinCtx             Windows context to use.
 * @param   hWnd                Window handle to use.
 * @param   msg                 Message ID to pass on.
 * @param   wParam              wParam to pass on
 * @param   lParam              lParam to pass on.
 */
LRESULT ShClWinHandleWMChangeCBChain(PSHCLWINCTX pWinCtx,
                                              HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT lresultRc = 0;

    LogFlowFuncEnter();

    if (ShClWinIsNewAPI(&pWinCtx->newAPI))
    {
        lresultRc = DefWindowProc(hWnd, msg, wParam, lParam);
    }
    else /* Old API */
    {
        HWND hwndRemoved = (HWND)wParam;
        HWND hwndNext    = (HWND)lParam;

        if (hwndRemoved == pWinCtx->hWndNextInChain)
        {
            /* The window that was next to our in the chain is being removed.
             * Relink to the new next window.
             */
            pWinCtx->hWndNextInChain = hwndNext;
        }
        else
        {
            if (pWinCtx->hWndNextInChain)
            {
                /* Pass the message further. */
                DWORD_PTR dwResult;
                lresultRc = SendMessageTimeout(pWinCtx->hWndNextInChain, WM_CHANGECBCHAIN, wParam, lParam, 0,
                                                SHCL_WIN_CBCHAIN_TIMEOUT_MS,
                                                &dwResult);
                if (!lresultRc)
                    lresultRc = (LRESULT)dwResult;
            }
        }
    }

    LogFlowFunc(("lresultRc=%ld\n", lresultRc));
    return lresultRc;
}

/**
 * Handles the WM_DESTROY code.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 */
int ShClWinHandleWMDestroy(PSHCLWINCTX pWinCtx)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    /* MS recommends to remove from Clipboard chain in this callback. */
    ShClWinChainRemove(pWinCtx);

    if (pWinCtx->oldAPI.timerRefresh)
    {
        Assert(pWinCtx->hWnd);
        KillTimer(pWinCtx->hWnd, 0);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles the WM_RENDERALLFORMATS message.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   hWnd                Window handle to use.
 */
int ShClWinHandleWMRenderAllFormats(PSHCLWINCTX pWinCtx, HWND hWnd)
{
    RT_NOREF(pWinCtx);

    LogFlowFuncEnter();

    /* Do nothing. The clipboard formats will be unavailable now, because the
     * windows is to be destroyed and therefore the guest side becomes inactive.
     */
    int rc = ShClWinOpen(hWnd);
    if (RT_SUCCESS(rc))
    {
        ShClWinClear();
        ShClWinClose();
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles the WM_TIMER code, which is needed if we're running with the so-called "old" Windows clipboard API.
 * Does nothing if we're running with the "new" Windows API.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 */
int ShClWinHandleWMTimer(PSHCLWINCTX pWinCtx)
{
    int rc = VINF_SUCCESS;

    if (!ShClWinIsNewAPI(&pWinCtx->newAPI)) /* Only run when using the "old" Windows API. */
    {
        LogFlowFuncEnter();

        HWND hViewer = GetClipboardViewer();

        /* Re-register ourselves in the clipboard chain if our last ping
         * timed out or there seems to be no valid chain. */
        if (!hViewer || pWinCtx->oldAPI.fCBChainPingInProcess)
        {
            ShClWinChainRemove(pWinCtx);
            ShClWinChainAdd(pWinCtx);
       }

       /* Start a new ping by passing a dummy WM_CHANGECBCHAIN to be
        * processed by ourselves to the chain. */
       pWinCtx->oldAPI.fCBChainPingInProcess = TRUE;

       hViewer = GetClipboardViewer();
       if (hViewer)
           SendMessageCallback(hViewer, WM_CHANGECBCHAIN, (WPARAM)pWinCtx->hWndNextInChain, (LPARAM)pWinCtx->hWndNextInChain,
                               ShClWinChainPingProc, (ULONG_PTR)pWinCtx);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Announces a clipboard format to the Windows clipboard.
 *
 * The actual rendering (setting) of the clipboard data will be done later with
 * a separate WM_RENDERFORMAT message.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if *all* format(s) is/are not supported / handled.
 * @param   pWinCtx             Windows context to use.
 * @param   fFormats            Clipboard format(s) to announce.
 */
static int shClWinAnnounceFormats(PSHCLWINCTX pWinCtx, SHCLFORMATS fFormats)
{
    LogFunc(("fFormats=0x%x\n", fFormats));

    /* Make sure that if VBOX_SHCL_FMT_URI_LIST is announced, we don't announced anything else.
     * This otherwise this will trigger a WM_DRAWCLIPBOARD or friends, which will result in fun bugs coming up. */
    AssertReturn((  !(fFormats & VBOX_SHCL_FMT_URI_LIST)
                  || (fFormats & VBOX_SHCL_FMT_URI_LIST) == VBOX_SHCL_FMT_URI_LIST), VERR_INVALID_PARAMETER);
    /*
     * Set the clipboard formats.
     */
    static struct
    {
        /** VBox format to handle. */
        uint32_t        fVBoxFormat;
        /** Native Windows format to use.
         *  Set to 0 if unused / needs special handling. */
        UINT            uWinFormat;
        /** Own registered format. Set to NULL if not used / applicable. */
        const char     *pszRegFormat;
        const char     *pszLog;
    } s_aFormats[] =
    {
        { VBOX_SHCL_FMT_UNICODETEXT,    CF_UNICODETEXT, NULL,                 "CF_UNICODETEXT" },
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /* We don't announce anything here for an URI list to the Windows clipboard, as we later have to
         * initialize our custom IDataObject and set it via OleSetClipboard(). */
        { VBOX_SHCL_FMT_URI_LIST,       0,              NULL,                 "SHCL_URI_LIST" },
#endif
        { VBOX_SHCL_FMT_BITMAP,         CF_DIB,         NULL,                 "CF_DIB" },
        { VBOX_SHCL_FMT_HTML,           0,              SHCL_WIN_REGFMT_HTML, "SHCL_WIN_REGFMT_HTML" }
    };

    unsigned    cSuccessfullySet = 0;
    SHCLFORMATS fFormatsLeft     = fFormats;
    int         rc               = VINF_SUCCESS;
    for (uintptr_t i = 0; i < RT_ELEMENTS(s_aFormats) && fFormatsLeft != 0; i++)
    {
        if (fFormatsLeft & s_aFormats[i].fVBoxFormat)
        {
            LogRel2(("Shared Clipboard: Announcing format '%s' to clipboard\n", s_aFormats[i].pszLog));
            fFormatsLeft &= ~s_aFormats[i].fVBoxFormat;

            UINT uWinFormat = 0;
            if (s_aFormats[i].pszRegFormat) /* See if we have (special) registered format. */
            {
                uWinFormat = RegisterClipboardFormat(s_aFormats[i].pszRegFormat);
                AssertContinue(uWinFormat != 0);
            }
            else /* Native format. */
                uWinFormat = s_aFormats[i].uWinFormat;

            /* Any native format set? If not, just skip it (as successful). */
            if (!uWinFormat)
            {
                cSuccessfullySet++;
                continue;
            }

            /* Tell the clipboard we've got data upon a request.  We check the
               last error here as hClip will be NULL even on success (despite
               what MSDN says). */
            SetLastError(NO_ERROR);
            HANDLE hClip = SetClipboardData(uWinFormat, NULL);
            DWORD dwErr = GetLastError();
            if (dwErr == NO_ERROR || hClip != NULL)
                cSuccessfullySet++;
            else
            {
                AssertMsgFailed(("%s/%u: %u\n", s_aFormats[i].pszLog, uWinFormat, dwErr));
                rc = RTErrConvertFromWin32(dwErr);
            }
        }
    }

    /*
     * Consider setting anything a success, converting any error into
     * informational status.  Unsupport error only happens if all formats
     * were unsupported.
     */
    if (cSuccessfullySet > 0)
    {
        pWinCtx->hWndClipboardOwnerUs = GetClipboardOwner();
        if (RT_FAILURE(rc))
            rc = -rc;
    }
    else if (RT_SUCCESS(rc) && fFormatsLeft != 0)
    {
        LogRel(("Shared Clipboard: Unable to announce unsupported/invalid formats: %#x (%#x)\n", fFormatsLeft, fFormats));
        rc = VERR_NOT_SUPPORTED;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Opens the clipboard, clears it, announces @a fFormats and closes it.
 *
 * The actual rendering (setting) of the clipboard data will be done later with
 * a separate WM_RENDERFORMAT message.
 *
 * @returns VBox status code.
 * @param   pWinCtx     Windows context to use.
 * @param   fFormats    Clipboard format(s) to announce.
 * @param   hWnd        The window handle to use as owner.
 */
int ShClWinClearAndAnnounceFormats(PSHCLWINCTX pWinCtx, SHCLFORMATS fFormats, HWND hWnd)
{
    int rc = ShClWinOpen(hWnd);
    if (RT_SUCCESS(rc))
    {
        ShClWinClear();

        rc = shClWinAnnounceFormats(pWinCtx, fFormats);
        Assert(pWinCtx->hWndClipboardOwnerUs == hWnd || pWinCtx->hWndClipboardOwnerUs == NULL);

        ShClWinClose();
    }
    return rc;
}

/**
 * Writes (places) clipboard data into the Windows clipboard.
 *
 * @returns VBox status code.
 * @param   cfFormat            Windows clipboard format to write data for.
 * @param   pvData              Pointer to actual clipboard data to write.
 * @param   cbData              Size (in bytes) of actual clipboard data to write.
 *
 * @note    ASSUMES that the clipboard has already been opened.
 */
int ShClWinDataWrite(UINT cfFormat, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn   (cbData, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    HANDLE hMem = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, cbData);

    LogFlowFunc(("hMem=%p\n", hMem));

    if (hMem)
    {
        void *pMem = GlobalLock(hMem);

        LogFlowFunc(("pMem=%p, GlobalSize=%zu\n", pMem, GlobalSize(hMem)));

        if (pMem)
        {
            LogFlowFunc(("Setting data\n"));

            memcpy(pMem, pvData, cbData);

            /* The memory must be unlocked before inserting to the Clipboard. */
            GlobalUnlock(hMem);

            /* 'hMem' contains the host clipboard data.
             * size is 'cb' and format is 'format'.
             */
            HANDLE hClip = SetClipboardData(cfFormat, hMem);

            LogFlowFunc(("hClip=%p\n", hClip));

            if (hClip)
            {
                /* The hMem ownership has gone to the system. Nothing to do. */
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
        }
        else
            rc = VERR_ACCESS_DENIED;

        GlobalFree(hMem);
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

    if (RT_FAILURE(rc))
        LogFunc(("Setting clipboard data failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Creates an Shared Clipboard transfer by announcing transfer data (via IDataObject) to Windows.
 *
 * This creates the necessary IDataObject + IStream implementations and initiates the actual transfers required for getting
 * the meta data. Whether or not the actual (file++) transfer(s) are happening is up to the user (at some point) later then.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pCtx                Shared Clipboard context to use.
 *                              Needed for the data object to communicate with the main window thread.
 * @param   pCallbacks          Callbacks table to use.
 */
int ShClWinTransferCreateAndSetDataObject(PSHCLWINCTX pWinCtx,
                                                     PSHCLCONTEXT pCtx, ShClWinDataObject::PCALLBACKS pCallbacks)
{
    AssertPtrReturn(pWinCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCallbacks, VERR_INVALID_POINTER);

    /* Make sure to enter the critical section before setting the clipboard data, as otherwise WM_CLIPBOARDUPDATE
     * might get called *before* we had the opportunity to set pWinCtx->hWndClipboardOwnerUs below. */
    int rc = RTCritSectEnter(&pWinCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("pWinCtx->pDataObjInFlight=%p\n", pWinCtx->pDataObjInFlight));

        /* Create a new data object here, assign it as the the current data object in-flight and
         * announce it to Windows below.
         *
         * The data object will be deleted automatically once its refcount reaches 0.
         */
        ShClWinDataObject *pObj = new ShClWinDataObject();
        if (pObj)
        {
            rc = pObj->Init(pCtx, pCallbacks);
            if (RT_SUCCESS(rc))
            {
                if (RT_SUCCESS(rc))
                    pWinCtx->pDataObjInFlight = pObj;
            }
        }
        else
            rc = VERR_NO_MEMORY;

        if (RT_SUCCESS(rc))
        {
            ShClWinClose();
            /* Note: Clipboard must be closed first before calling OleSetClipboard(). */

            /** @todo There is a potential race between ShClWinClose() and OleSetClipboard(),
             *        where another application could own the clipboard (open), and thus the call to
             *        OleSetClipboard() will fail. Needs (better) fixing. */
            HRESULT hr = S_OK;

            for (unsigned uTries = 0; uTries < 3; uTries++)
            {
                hr = OleSetClipboard(pWinCtx->pDataObjInFlight);
                if (SUCCEEDED(hr))
                {
                    Assert(OleIsCurrentClipboard(pWinCtx->pDataObjInFlight) == S_OK); /* Sanity. */

                    /*
                     * Calling OleSetClipboard() changed the clipboard owner, which in turn will let us receive
                     * a WM_CLIPBOARDUPDATE message. To not confuse ourselves with our own clipboard owner changes,
                     * save a new window handle and deal with it in WM_CLIPBOARDUPDATE.
                     */
                    pWinCtx->hWndClipboardOwnerUs = GetClipboardOwner();

                    LogFlowFunc(("hWndClipboardOwnerUs=%p\n", pWinCtx->hWndClipboardOwnerUs));
                    break;
                }

                LogFlowFunc(("Failed with %Rhrc (try %u/3)\n", hr, uTries + 1));
                RTThreadSleep(500); /* Wait a bit. */
            }

            if (FAILED(hr))
            {
                rc = VERR_ACCESS_DENIED; /** @todo Fudge; fix this. */
                LogRel(("Shared Clipboard: Failed with %Rhrc when setting data object to clipboard\n", hr));
            }
        }

        int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
        AssertRC(rc2);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates implementation-specific data for a Windows Shared Clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Shared Clipboard transfer to create implementation-specific data for.
 */
int ShClWinTransferCreate(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer)
{
    RT_NOREF(pWinCtx);

    AssertMsgReturn(   pTransfer->pvUser == NULL
                    && pTransfer->cbUser == 0, ("Already initialized Windows-specific data\n"), VERR_WRONG_ORDER);

    pTransfer->pvUser = new ShClWinTransferCtx();  /** @todo Can this throw? */
    AssertPtrReturn(pTransfer->pvUser, VERR_INVALID_POINTER);
    pTransfer->cbUser = sizeof(ShClWinTransferCtx);

    return VINF_SUCCESS;
}

/**
 * Destroys implementation-specific data for a Windows Shared Clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Shared Clipboard transfer to destroy implementation-specific data for.
 */
void ShClWinTransferDestroy(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer)
{
    RT_NOREF(pWinCtx);

    if (!pTransfer)
        return;

    LogFlowFuncEnter();

    if (pTransfer->pvUser)
    {
        Assert(pTransfer->cbUser == sizeof(ShClWinTransferCtx));
        ShClWinTransferCtx *pWinURITransferCtx = (ShClWinTransferCtx *)pTransfer->pvUser;
        AssertPtr(pWinURITransferCtx);

        /* If the transfer has a data object assigned, uninitialize it here.
         * Note: We don't free the object here, as other processes like the Windows Explorer still might refer to it. */
        if (pWinURITransferCtx->pDataObj)
        {
            pWinURITransferCtx->pDataObj->Uninit();
            pWinURITransferCtx->pDataObj = NULL;
        }

        delete pWinURITransferCtx;
        pWinURITransferCtx = NULL;

        pTransfer->pvUser = NULL;
        pTransfer->cbUser = 0;
    }
}

/**
 * Initializes a Windows transfer for a given data object.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Transfer to initialize for the data object.
 * @param   pObj                Data object to initialize transfer for.
 */
static int shClWinTransferInitializeInternal(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer,
                                             ShClWinDataObject *pObj)
{
    RT_NOREF(pWinCtx);

    return pObj->SetTransfer(pTransfer);
}

/**
 * Initializes a Windows transfer for the current data object in-flight.
 *
 * @returns VBox status code.
 * @retval  VERR_WRONG_ORDER if no current in-flight data object is available.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Transfer to initialize for the data object.
 */
int ShClWinTransferInitialize(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer)
{
    int rc = RTCritSectEnter(&pWinCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        ShClWinDataObject *pObj = pWinCtx->pDataObjInFlight;
        if (pObj)
        {
            rc = shClWinTransferInitializeInternal(pWinCtx, pTransfer, pObj);
        }
        else /* No current in-flight data object. */
            rc = VERR_WRONG_ORDER;

        int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
        AssertRC(rc2);
    }

    return rc;
}

/**
 * Starts a Windows transfer for a given data object.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pObj                Data object to start transfer for.
 */
static int shClWinTransferStartInternal(PSHCLWINCTX pWinCtx, ShClWinDataObject *pObj)
{
    RT_NOREF(pWinCtx);

    return pObj->SetStatus(ShClWinDataObject::Running);
}

/**
 * Starts a Windows transfer for the current data object in-flight.
 *
 * This hands off the data object in-flight to Windows on success.
 *
 * @returns VBox status code.
 * @retval  VERR_WRONG_ORDER if no current in-flight data object is available.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Transfer to initialize for the data object.
 */
int ShClWinTransferStart(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer)
{
    RT_NOREF(pTransfer);

    int rc = RTCritSectEnter(&pWinCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        ShClWinDataObject *pObj = pWinCtx->pDataObjInFlight;
        if (pObj)
        {
            rc = shClWinTransferStartInternal(pWinCtx, pObj);
            if (RT_SUCCESS(rc))
                pWinCtx->pDataObjInFlight = NULL; /* Hand off to Windows on success. */
        }
        else /* No current in-flight data object. */
            rc = VERR_WRONG_ORDER;

        int rc2 = RTCritSectLeave(&pWinCtx->CritSect);
        AssertRC(rc2);
    }

    return rc;
}

/**
 * Retrieves the roots for a transfer by opening the clipboard and getting the clipboard data
 * as string list (CF_HDROP), assigning it to the transfer as roots then.
 *
 * @returns VBox status code.
 * @param   pWinCtx             Windows context to use.
 * @param   pTransfer           Transfer to get roots for.
 */
int ShClWinTransferGetRootsFromClipboard(PSHCLWINCTX pWinCtx, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pWinCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    Assert(ShClTransferGetSource(pTransfer) == SHCLSOURCE_LOCAL); /* Sanity. */

    int rc = ShClWinOpen(pWinCtx->hWnd);
    if (RT_SUCCESS(rc))
    {
        /* The data data in CF_HDROP format, as the files are locally present and don't need to be
         * presented as a IDataObject or IStream. */
        HANDLE hClip = hClip = GetClipboardData(CF_HDROP);
        if (hClip)
        {
            HDROP hDrop = (HDROP)GlobalLock(hClip);
            if (hDrop)
            {
                char    *pszList = NULL;
                uint32_t cbList;
                rc = ShClWinTransferDropFilesToStringList((DROPFILES *)hDrop, &pszList, &cbList);

                GlobalUnlock(hClip);

                if (RT_SUCCESS(rc))
                {
                    rc = ShClTransferRootsSetFromStringList(pTransfer, pszList, cbList);
                    RTStrFree(pszList);
                }
            }
            else
                LogRel(("Shared Clipboard: Unable to lock clipboard data, last error: %ld\n", GetLastError()));
        }
        else
            LogRel(("Shared Clipboard: Unable to retrieve clipboard data from clipboard (CF_HDROP), last error: %ld\n",
                    GetLastError()));

        ShClWinClose();
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Converts a DROPFILES (HDROP) structure to a string list, separated by SHCL_TRANSFER_URI_LIST_SEP_STR.
 * Does not do any locking on the input data.
 *
 * @returns VBox status code.
 * @param   pDropFiles          Pointer to DROPFILES structure to convert.
 * @param   ppszList            Where to store the allocated string list on success.
 *                              Needs to be free'd with RTStrFree().
 * @param   pcbList             Where to store the size (in bytes) of the allocated string list.
 *                              Includes zero terminator.
 */
int ShClWinTransferDropFilesToStringList(DROPFILES *pDropFiles, char **ppszList, uint32_t *pcbList)
{
    AssertPtrReturn(pDropFiles, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszList,   VERR_INVALID_POINTER);
    AssertPtrReturn(pcbList,    VERR_INVALID_POINTER);

    /* Do we need to do Unicode stuff? */
    const bool fUnicode = RT_BOOL(pDropFiles->fWide);

    /* Get the offset of the file list. */
    Assert(pDropFiles->pFiles >= sizeof(DROPFILES));

    /* Note: This is *not* pDropFiles->pFiles! DragQueryFile only
     *       will work with the plain storage medium pointer! */
    HDROP hDrop = (HDROP)(pDropFiles);

    int rc = VINF_SUCCESS;

    /* First, get the file count. */
    /** @todo Does this work on Windows 2000 / NT4? */
    char *pszFiles = NULL;
    uint32_t cchFiles = 0;
    UINT cFiles = DragQueryFile(hDrop, UINT32_MAX /* iFile */, NULL /* lpszFile */, 0 /* cchFile */);

    LogFlowFunc(("Got %RU16 file(s), fUnicode=%RTbool\n", cFiles, fUnicode));

    for (UINT i = 0; i < cFiles; i++)
    {
        UINT cchFile = DragQueryFile(hDrop, i /* File index */, NULL /* Query size first */, 0 /* cchFile */);
        Assert(cchFile);

        if (RT_FAILURE(rc))
            break;

        char *pszFileUtf8 = NULL; /* UTF-8 version. */
        UINT cchFileUtf8 = 0;
        if (fUnicode)
        {
            /* Allocate enough space (including terminator). */
            WCHAR *pwszFile = (WCHAR *)RTMemAlloc((cchFile + 1) * sizeof(WCHAR));
            if (pwszFile)
            {
                const UINT cwcFileUtf16 = DragQueryFileW(hDrop, i /* File index */,
                                                         pwszFile, cchFile + 1 /* Include terminator */);

                AssertMsg(cwcFileUtf16 == cchFile, ("cchFileUtf16 (%RU16) does not match cchFile (%RU16)\n",
                                                    cwcFileUtf16, cchFile));
                RT_NOREF(cwcFileUtf16);

                rc = RTUtf16ToUtf8(pwszFile, &pszFileUtf8);
                if (RT_SUCCESS(rc))
                {
                    cchFileUtf8 = (UINT)strlen(pszFileUtf8);
                    Assert(cchFileUtf8);
                }

                RTMemFree(pwszFile);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else /* ANSI */
        {
            /* Allocate enough space (including terminator). */
            char *pszFileANSI = (char *)RTMemAlloc((cchFile + 1) * sizeof(char));
            UINT  cchFileANSI = 0;
            if (pszFileANSI)
            {
                cchFileANSI = DragQueryFileA(hDrop, i /* File index */,
                                             pszFileANSI, cchFile + 1 /* Include terminator */);

                AssertMsg(cchFileANSI == cchFile, ("cchFileANSI (%RU16) does not match cchFile (%RU16)\n",
                                                   cchFileANSI, cchFile));

                /* Convert the ANSI codepage to UTF-8. */
                rc = RTStrCurrentCPToUtf8(&pszFileUtf8, pszFileANSI);
                if (RT_SUCCESS(rc))
                {
                    cchFileUtf8 = (UINT)strlen(pszFileUtf8);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("\tFile: %s (cchFile=%RU16)\n", pszFileUtf8, cchFileUtf8));

            LogRel2(("Shared Clipboard: Adding file '%s' to transfer\n", pszFileUtf8));

            rc = RTStrAAppendExN(&pszFiles, 1 /* cPairs */, pszFileUtf8, strlen(pszFileUtf8));
            cchFiles += (uint32_t)strlen(pszFileUtf8);
        }

        if (pszFileUtf8)
            RTStrFree(pszFileUtf8);

        if (RT_FAILURE(rc))
        {
            LogFunc(("Error handling file entry #%u, rc=%Rrc\n", i, rc));
            break;
        }

        /* Add separation between filenames.
         * Note: Also do this for the last element of the list. */
        rc = RTStrAAppendExN(&pszFiles, 1 /* cPairs */, SHCL_TRANSFER_URI_LIST_SEP_STR, 2 /* Bytes */);
        if (RT_SUCCESS(rc))
            cchFiles += 2; /* Include SHCL_TRANSFER_URI_LIST_SEP_STR */
    }

    if (RT_SUCCESS(rc))
    {
        cchFiles += 1; /* Add string termination. */
        uint32_t cbFiles = cchFiles * sizeof(char); /* UTF-8. */

        LogFlowFunc(("cFiles=%u, cchFiles=%RU32, cbFiles=%RU32, pszFiles=0x%p\n",
                     cFiles, cchFiles, cbFiles, pszFiles));

        *ppszList = pszFiles;
        *pcbList   = cbFiles;
    }
    else
    {
        if (pszFiles)
            RTStrFree(pszFiles);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

