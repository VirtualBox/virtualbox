/* $Id: VBoxSeamless.cpp 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxSeamless - Seamless windows
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
#include <iprt/assert.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/system.h>

#define _WIN32_WINNT 0x0500
#include <iprt/win/windows.h>

#include <VBoxHook.h> /* from ../include/ */

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxHelpers.h"
#include "VBoxSeamless.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _VBOXSEAMLESSCONTEXT
{
    const VBOXTRAYSVCENV *pEnv;

    RTLDRMOD hModHook;

    BOOL    (* pfnVBoxHookInstallWindowTracker)(HMODULE hDll);
    BOOL    (* pfnVBoxHookRemoveWindowTracker)();

    PVBOXDISPIFESCAPE lpEscapeData;
} VBOXSEAMLESSCONTEXT, *PVBOXSEAMLESSCONTEXT;

typedef struct
{
    HDC     hdc;
    HRGN    hrgn;
} VBOX_ENUM_PARAM, *PVBOX_ENUM_PARAM;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXSEAMLESSCONTEXT g_Ctx = { 0 };


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
void VBoxLogString(HANDLE hDriver, char *pszStr);
static void vboxSeamlessSetSupported(BOOL fSupported);


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrSeamlessPreInit(void)
{
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrSeamlessOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
static DECLCALLBACK(int) vbtrSeamlessInit(const PVBOXTRAYSVCENV pEnv, void **ppvInstance)
{
    LogFlowFuncEnter();

    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /* Only one instance at the moment. */
    AssertPtr(pCtx);

    pCtx->pEnv     = pEnv;
    pCtx->hModHook = NIL_RTLDRMOD;

    int rc;

    /* We have to jump out here when using NT4, otherwise it complains about
       a missing API function "UnhookWinEvent" used by the dynamically loaded VBoxHook.dll below */
    uint64_t const uNtVersion = RTSystemGetNtVersion();
    if (uNtVersion < RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* Windows NT 4.0 or older */
    {
        LogRel(("Seamless: Windows NT 4.0 or older not supported!\n"));
        rc = VERR_NOT_SUPPORTED;
    }
    else
    {
        /* Will fail if SetWinEventHook is not present (version < NT4 SP6 apparently) */
        rc = RTLdrLoadAppPriv(VBOXHOOK_DLL_NAME, &pCtx->hModHook);
        if (RT_SUCCESS(rc))
        {
            *(PFNRT *)&pCtx->pfnVBoxHookInstallWindowTracker = RTLdrGetFunction(pCtx->hModHook, "VBoxHookInstallWindowTracker");
            *(PFNRT *)&pCtx->pfnVBoxHookRemoveWindowTracker  = RTLdrGetFunction(pCtx->hModHook, "VBoxHookRemoveWindowTracker");

            if (   pCtx->pfnVBoxHookInstallWindowTracker
                && pCtx->pfnVBoxHookRemoveWindowTracker)
            {
                vboxSeamlessSetSupported(TRUE);

                *ppvInstance = pCtx;
            }
            else
            {
                LogRel(("Seamless: Not supported, skipping\n"));
                rc = VERR_NOT_SUPPORTED;
            }
        }
        else
        {
            LogRel(("Seamless: Could not load %s (%Rrc), skipping\n", VBOXHOOK_DLL_NAME, rc));
            rc = VERR_NOT_SUPPORTED;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
static DECLCALLBACK(void) VBoxSeamlessDestroy(void *pvInstance)
{
    LogFlowFuncEnter();

    if (!pvInstance)
        return;

    PVBOXSEAMLESSCONTEXT pCtx = (PVBOXSEAMLESSCONTEXT)pvInstance;
    AssertPtr(pCtx);

    vboxSeamlessSetSupported(FALSE);

    /* Inform the host that we no longer support the seamless window mode. */
    if (pCtx->pfnVBoxHookRemoveWindowTracker)
        pCtx->pfnVBoxHookRemoveWindowTracker();
    if (pCtx->hModHook != NIL_RTLDRMOD)
    {
        RTLdrClose(pCtx->hModHook);
        pCtx->hModHook = NIL_RTLDRMOD;
    }
    return;
}

static void VBoxSeamlessInstallHook(void)
{
    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);

    if (pCtx->pfnVBoxHookInstallWindowTracker)
    {
        /* Check current visible region state */
        VBoxSeamlessCheckWindows(true);

        HMODULE hMod = (HMODULE)RTLdrGetNativeHandle(pCtx->hModHook);
        Assert(hMod != (HMODULE)~(uintptr_t)0);
        pCtx->pfnVBoxHookInstallWindowTracker(hMod);
    }
}

static void VBoxSeamlessRemoveHook(void)
{
    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);

    if (pCtx->pfnVBoxHookRemoveWindowTracker)
        pCtx->pfnVBoxHookRemoveWindowTracker();

    if (pCtx->lpEscapeData)
    {
        RTMemFree(pCtx->lpEscapeData);
        pCtx->lpEscapeData = NULL;
    }
}

extern HANDLE g_hSeamlessKmNotifyEvent;

static VBOXDISPIF_SEAMLESS gVBoxDispIfSeamless; /** @todo r=andy Move this into VBOXSEAMLESSCONTEXT? */


void VBoxSeamlessEnable(void)
{
    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);

    Assert(g_hSeamlessKmNotifyEvent);

    VBoxDispIfSeamlessCreate(&pCtx->pEnv->dispIf, &gVBoxDispIfSeamless, g_hSeamlessKmNotifyEvent);

    VBoxSeamlessInstallHook();
}

void VBoxSeamlessDisable(void)
{
    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);
    NOREF(pCtx);

    VBoxSeamlessRemoveHook();

    VBoxDispIfSeamlessTerm(&gVBoxDispIfSeamless);
}

void vboxSeamlessSetSupported(BOOL fSupported)
{
    VBoxConsoleCapSetSupported(VBOXCAPS_ENTRY_IDX_SEAMLESS, fSupported);
}

BOOL CALLBACK VBoxEnumFunc(HWND hwnd, LPARAM lParam) RT_NOTHROW_DEF
{
    PVBOX_ENUM_PARAM    lpParam = (PVBOX_ENUM_PARAM)lParam;
    DWORD               dwStyle, dwExStyle;
    RECT                rectWindow, rectVisible;

    dwStyle   = GetWindowLong(hwnd, GWL_STYLE);
    dwExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    if ( !(dwStyle & WS_VISIBLE) || (dwStyle & WS_CHILD))
        return TRUE;

    LogFlow(("VBoxTray: VBoxEnumFunc %x\n", hwnd));
    /* Only visible windows that are present on the desktop are interesting here */
    if (!GetWindowRect(hwnd, &rectWindow))
    {
        return TRUE;
    }

    char szWindowText[256];
    char szWindowClass[256];
    HWND hStart = NULL;

    szWindowText[0] = 0;
    szWindowClass[0] = 0;

    GetWindowText(hwnd, szWindowText, sizeof(szWindowText));
    GetClassName(hwnd, szWindowClass, sizeof(szWindowClass));

    uint64_t const uNtVersion = RTSystemGetNtVersion();
    if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
    {
        hStart = ::FindWindowEx(GetDesktopWindow(), NULL, "Button", "Start");

        if ( hwnd == hStart && !strcmp(szWindowText, "Start") )
        {
            /* for vista and above. To solve the issue of small bar above
             * the Start button when mouse is hovered over the start button in seamless mode.
             * Difference of 7 is observed in Win 7 platform between the dimensions of rectangle with Start title and its shadow.
            */
            rectWindow.top += 7;
            rectWindow.bottom -=7;
        }
    }

    rectVisible = rectWindow;

    /* Filter out Windows XP shadow windows */
    /** @todo still shows inside the guest */
    if ( szWindowText[0] == 0 &&
            (dwStyle == (WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS)
                && dwExStyle == (WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST))
            || (dwStyle == (WS_POPUP | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                && dwExStyle == (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE))
            || (dwStyle == (WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                && dwExStyle == (WS_EX_TOOLWINDOW)) )
    {
        Log(("VBoxTray: Filter out shadow window style=%x exstyle=%x\n", dwStyle, dwExStyle));
        Log(("VBoxTray: Enum hwnd=%x rect (%d,%d) (%d,%d) (filtered)\n", hwnd, rectWindow.left, rectWindow.top, rectWindow.right, rectWindow.bottom));
        Log(("VBoxTray: title=%s style=%x exStyle=%x\n", szWindowText, dwStyle, dwExStyle));
        return TRUE;
    }

    /** Such a windows covers the whole screen making desktop background*/
    if (strcmp(szWindowText, "Program Manager") && strcmp(szWindowClass, "ApplicationFrameWindow"))
    {
        Log(("VBoxTray: Enum hwnd=%x rect (%d,%d)-(%d,%d) [%d x %d](applying)\n", hwnd,
            rectWindow.left, rectWindow.top, rectWindow.right, rectWindow.bottom,
            rectWindow.left - rectWindow.right, rectWindow.bottom - rectWindow.top));
        Log(("VBoxTray: title=%s style=%x exStyle=%x\n", szWindowText, dwStyle, dwExStyle));

        HRGN hrgn = CreateRectRgn(0, 0, 0, 0);

        int ret = GetWindowRgn(hwnd, hrgn);

        if (ret == ERROR)
        {
            Log(("VBoxTray: GetWindowRgn failed with rc=%d, adding antire rect\n", GetLastError()));
            SetRectRgn(hrgn, rectVisible.left, rectVisible.top, rectVisible.right, rectVisible.bottom);
        }
        else
        {
            /* this region is relative to the window origin instead of the desktop origin */
            OffsetRgn(hrgn, rectWindow.left, rectWindow.top);
        }

        if (lpParam->hrgn)
        {
            /* create a union of the current visible region and the visible rectangle of this window. */
            CombineRgn(lpParam->hrgn, lpParam->hrgn, hrgn, RGN_OR);
            DeleteObject(hrgn);
        }
        else
            lpParam->hrgn = hrgn;
    }
    else
    {
        Log(("VBoxTray: Enum hwnd=%x rect (%d,%d)-(%d,%d) [%d x %d](ignored)\n", hwnd,
            rectWindow.left, rectWindow.top, rectWindow.right, rectWindow.bottom,
            rectWindow.left - rectWindow.right, rectWindow.bottom - rectWindow.top));
        Log(("VBoxTray: title=%s style=%x exStyle=%x\n", szWindowText, dwStyle, dwExStyle));
    }

    return TRUE; /* continue enumeration */
}

void VBoxSeamlessCheckWindows(bool fForce)
{
    PVBOXSEAMLESSCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);

    if (!VBoxDispIfSeamlesIsValid(&gVBoxDispIfSeamless))
        return;

    VBOX_ENUM_PARAM param;

    param.hdc       = GetDC(HWND_DESKTOP);
    param.hrgn      = 0;

    EnumWindows(VBoxEnumFunc, (LPARAM)&param);

    if (param.hrgn)
    {
        DWORD cbSize = GetRegionData(param.hrgn, 0, NULL);
        if (cbSize)
        {
            PVBOXDISPIFESCAPE lpEscapeData = (PVBOXDISPIFESCAPE)RTMemAllocZ(VBOXDISPIFESCAPE_SIZE(cbSize));
            if (lpEscapeData)
            {
                lpEscapeData->escapeCode = VBOXESC_SETVISIBLEREGION;
                LPRGNDATA lpRgnData = VBOXDISPIFESCAPE_DATA(lpEscapeData, RGNDATA);

                cbSize = GetRegionData(param.hrgn, cbSize, lpRgnData);
                if (cbSize)
                {
#ifdef LOG_ENABLED
                    RECT *paRects = (RECT *)&lpRgnData->Buffer[0];
                    Log(("VBoxTray: New visible region: \n"));
                    for (DWORD i = 0; i < lpRgnData->rdh.nCount; i++)
                        Log(("VBoxTray: visible rect (%d,%d)(%d,%d)\n",
                             paRects[i].left, paRects[i].top, paRects[i].right, paRects[i].bottom));
#endif

                    LPRGNDATA lpCtxRgnData = VBOXDISPIFESCAPE_DATA(pCtx->lpEscapeData, RGNDATA);

                    if (   fForce
                        || !pCtx->lpEscapeData
                        || (lpCtxRgnData->rdh.dwSize + lpCtxRgnData->rdh.nRgnSize != cbSize)
                        || memcmp(lpCtxRgnData, lpRgnData, cbSize))
                    {
                        /* send to display driver */
                        VBoxDispIfSeamlessSubmit(&gVBoxDispIfSeamless, lpEscapeData, cbSize);

                        if (pCtx->lpEscapeData)
                            RTMemFree(pCtx->lpEscapeData);
                        pCtx->lpEscapeData = lpEscapeData;
                    }
                    else
                        Log(("VBoxTray: Visible rectangles haven't changed; ignore\n"));
                }
                if (lpEscapeData != pCtx->lpEscapeData)
                    RTMemFree(lpEscapeData);
            }
        }

        DeleteObject(param.hrgn);
    }

    ReleaseDC(HWND_DESKTOP, param.hdc);
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
static DECLCALLBACK(int) vbtrSeamlessWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtrReturn(pvInstance, VERR_INVALID_POINTER);
    LogFlowFunc(("pvInstance=%p\n", pvInstance));

    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    int rc = VbglR3CtlFilterMask(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST, 0 /*fNot*/);
    if (RT_FAILURE(rc))
    {
        LogRel(("Seamless: VbglR3CtlFilterMask(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST,0) failed with %Rrc, exiting ...\n", rc));
        return rc;
    }

    BOOL fWasScreenSaverActive = FALSE;
    for (;;)
    {
        /*
         * Wait for a seamless change event, check for shutdown both before and after.
         */
        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        /** @todo r=andy We do duplicate code here (see VbglR3SeamlessWaitEvent()). */
        uint32_t fEvent = 0;
        rc = VbglR3WaitEvent(VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST, 5000 /*ms*/, &fEvent);

        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        if (RT_SUCCESS(rc))
        {
            /* did we get the right event? */
            if (fEvent & VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST)
            {
                /*
                 * We got at least one event. Read the requested resolution
                 * and try to set it until success. New events will not be seen
                 * but a new resolution will be read in this poll loop.
                 */
                for (;;)
                {
                    /* get the seamless change request */
                    VMMDevSeamlessMode enmMode = (VMMDevSeamlessMode)-1;
                    rc = VbglR3SeamlessGetLastEvent(&enmMode);
                    if (RT_SUCCESS(rc))
                    {
                        LogFlowFunc(("Mode changed to %d\n", enmMode));

                        BOOL fRet;
                        switch (enmMode)
                        {
                            case VMMDev_Seamless_Disabled:
                                if (fWasScreenSaverActive)
                                {
                                    LogRel(("Seamless: Re-enabling the screensaver\n"));
                                    fRet = SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, TRUE, NULL, 0);
                                    if (!fRet)
                                        LogRel(("Seamless: SystemParametersInfo SPI_SETSCREENSAVEACTIVE failed with %ld\n", GetLastError()));
                                }
                                PostMessage(g_hwndToolWindow, WM_VBOX_SEAMLESS_DISABLE, 0, 0);
                                break;

                            case VMMDev_Seamless_Visible_Region:
                                fRet = SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &fWasScreenSaverActive, 0);
                                if (!fRet)
                                    LogRel(("Seamless: SystemParametersInfo SPI_GETSCREENSAVEACTIVE failed with %ld\n", GetLastError()));

                                if (fWasScreenSaverActive)
                                    LogRel(("Seamless: Disabling the screensaver\n"));

                                fRet = SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, FALSE, NULL, 0);
                                if (!fRet)
                                    LogRel(("Seamless: SystemParametersInfo SPI_SETSCREENSAVEACTIVE failed with %ld\n", GetLastError()));
                                PostMessage(g_hwndToolWindow, WM_VBOX_SEAMLESS_ENABLE, 0, 0);
                                break;

                            case VMMDev_Seamless_Host_Window:
                                break;

                            default:
                                AssertFailed();
                                break;
                        }
                        break;
                    }

                    LogRel(("Seamless: VbglR3SeamlessGetLastEvent() failed with %Rrc\n", rc));

                    if (*pfShutdown)
                        break;

                    /* sleep a bit to not eat too much CPU while retrying */
                    RTThreadSleep(10);
                }
            }
        }
        /* sleep a bit to not eat too much CPU in case the above call always fails */
        else if (rc != VERR_TIMEOUT)
            RTThreadSleep(10);
    }

    int rc2 = VbglR3CtlFilterMask(0 /*fOk*/, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST);
    if (RT_FAILURE(rc2))
        LogRel(("Seamless: VbglR3CtlFilterMask(0, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST) failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescSeamless =
{
    /* pszName. */
    "seamless",
    /* pszDescription. */
    "Seamless Windows",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrSeamlessPreInit,
    vbtrSeamlessOption,
    vbtrSeamlessInit,
    vbtrSeamlessWorker,
    NULL /* pfnStop */,
    VBoxSeamlessDestroy
};

