/* $Id: VBoxTray.cpp 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxTray - Guest Additions Tray Application
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
#include <package-generated.h>
#include "product-generated.h"

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxTrayMsg.h"
#include "VBoxHelpers.h"
#include "VBoxSeamless.h"
#include "VBoxClipboard.h"
#include "VBoxDisplay.h"
#include "VBoxVRDP.h"
#include "VBoxHostVersion.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "VBoxDnD.h"
#endif
#include "VBoxIPC.h"
#include "VBoxLA.h"
#include <VBoxHook.h>

#include <sddl.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/time.h>
#include <iprt/utf16.h>

#include <VBox/log.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void VBoxGrapicsSetSupported(BOOL fSupported);
static int vboxTrayCreateTrayIcon(void);
static LRESULT CALLBACK vboxToolWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Global message handler prototypes. */
static int vboxTrayGlMsgTaskbarCreated(WPARAM lParam, LPARAM wParam);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef BOOL (WINAPI *PFNALLOCCONSOLE)(VOID);
typedef BOOL (WINAPI *PFNATTACHCONSOLE)(DWORD);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Mutex for checking if VBoxTray already is running. */
HANDLE                g_hMutexAppRunning       = NULL;
/** Whether VBoxTray is connected to a (parent) console. */
bool                  g_fHasConsole            = false;
/** The current verbosity level. */
unsigned              g_cVerbosity             = 0;
HANDLE                g_hStopSem;
HANDLE                g_hSeamlessWtNotifyEvent = 0;
HANDLE                g_hSeamlessKmNotifyEvent = 0;
HINSTANCE             g_hInstance              = NULL;
HWND                  g_hwndToolWindow;
NOTIFYICONDATA        g_NotifyIconData;

uint32_t              g_fGuestDisplaysChanged = 0;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
DECL_HIDDEN_DATA(PFNALLOCCONSOLE)  g_pfnAllocConsole = NULL;  /* For W2K+. */
DECL_HIDDEN_DATA(PFNATTACHCONSOLE) g_pfnAttachConsole = NULL; /* For W2K+. */


/**
 * The details of the services that has been compiled in.
 */
static VBOXTRAYSVCINFO g_aServices[] =
{
    { &g_SvcDescDisplay,        NIL_RTTHREAD, NULL, false, false, false, false, true },
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    { &g_SvcDescClipboard,      NIL_RTTHREAD, NULL, false, false, false, false, true },
#endif
    { &g_SvcDescSeamless,       NIL_RTTHREAD, NULL, false, false, false, false, true },
    { &g_SvcDescVRDP,           NIL_RTTHREAD, NULL, false, false, false, false, true },
    { &g_SvcDescIPC,            NIL_RTTHREAD, NULL, false, false, false, false, true },
    { &g_SvcDescLA,             NIL_RTTHREAD, NULL, false, false, false, false, true },
#ifdef VBOX_WITH_DRAG_AND_DROP
    { &g_SvcDescDnD,            NIL_RTTHREAD, NULL, false, false, false, false, true }
#endif
};

/**
 * The global message table.
 */
static VBOXTRAYGLOBALMSG g_vboxGlobalMessageTable[] =
{
    /* Windows specific stuff. */
    {
        "TaskbarCreated",
        vboxTrayGlMsgTaskbarCreated
    },

    /* VBoxTray specific stuff. */
    /** @todo Add new messages here! */
    {
        NULL
    }
};

/**
 * Gets called whenever the Windows main taskbar
 * get (re-)created. Nice to install our tray icon.
 *
 * @return  VBox status code.
 * @param   wParam
 * @param   lParam
 */
static int vboxTrayGlMsgTaskbarCreated(WPARAM wParam, LPARAM lParam)
{
    RT_NOREF(wParam, lParam);
    return vboxTrayCreateTrayIcon();
}

/**
 * Creates VBoxTray's tray icon.
 *
 * @returns VBox status code.
 */
static int vboxTrayCreateTrayIcon(void)
{
    HICON hIcon = LoadIcon(g_hInstance, "IDI_ICON1"); /* see Artwork/win/TemplateR3.rc */
    if (hIcon == NULL)
    {
        DWORD const dwErr = GetLastError();
        VBoxTrayError("Could not load tray icon (%#x)\n", dwErr);
        return RTErrConvertFromWin32(dwErr);
    }

    /* Prepare the system tray icon. */
    RT_ZERO(g_NotifyIconData);
    g_NotifyIconData.cbSize           = NOTIFYICONDATA_V1_SIZE; // sizeof(NOTIFYICONDATA);
    g_NotifyIconData.hWnd             = g_hwndToolWindow;
    g_NotifyIconData.uID              = ID_TRAYICON;
    g_NotifyIconData.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_NotifyIconData.uCallbackMessage = WM_VBOXTRAY_TRAY_ICON;
    g_NotifyIconData.hIcon            = hIcon;

    RTStrPrintf(g_NotifyIconData.szTip, sizeof(g_NotifyIconData.szTip), "%s Guest Additions %d.%d.%dr%d",
                VBOX_PRODUCT, VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV);

    int rc = VINF_SUCCESS;
    if (!Shell_NotifyIcon(NIM_ADD, &g_NotifyIconData))
    {
        DWORD const dwErr = GetLastError();
        VBoxTrayError("Could not create tray icon (%#x)\n", dwErr);
        rc = RTErrConvertFromWin32(dwErr);
        RT_ZERO(g_NotifyIconData);
    }

    if (hIcon)
        DestroyIcon(hIcon);
    return rc;
}

/**
 * Removes VBoxTray's tray icon.
 *
 * @returns VBox status code.
 */
static void vboxTrayRemoveTrayIcon(void)
{
    if (g_NotifyIconData.cbSize > 0)
    {
        /* Remove the system tray icon and refresh system tray. */
        Shell_NotifyIcon(NIM_DELETE, &g_NotifyIconData);
        HWND hTrayWnd = FindWindow("Shell_TrayWnd", NULL); /* We assume we only have one tray atm. */
        if (hTrayWnd)
        {
            HWND hTrayNotifyWnd = FindWindowEx(hTrayWnd, 0, "TrayNotifyWnd", NULL);
            if (hTrayNotifyWnd)
                SendMessage(hTrayNotifyWnd, WM_PAINT, 0, NULL);
        }
        RT_ZERO(g_NotifyIconData);
    }
}

/**
 * The service thread.
 *
 * @returns Whatever the worker function returns.
 * @param   ThreadSelf      My thread handle.
 * @param   pvUser          The service index.
 */
static DECLCALLBACK(int) vboxTrayServiceThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PVBOXTRAYSVCINFO pSvc = (PVBOXTRAYSVCINFO)pvUser;
    AssertPtr(pSvc);

#ifndef RT_OS_WINDOWS
    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);
#endif

    int rc = pSvc->pDesc->pfnWorker(pSvc->pvInstance, &pSvc->fShutdown);
    ASMAtomicXchgBool(&pSvc->fShutdown, true);
    RTThreadUserSignal(ThreadSelf);

    VBoxTrayVerbose(1, "Thread for '%s' ended with %Rrc\n", pSvc->pDesc->pszName, rc);
    return rc;
}

/**
 * Lazily calls the pfnPreInit method on each service.
 *
 * @returns VBox status code, error message displayed.
 */
static int vboxTrayServicesLazyPreInit(void)
{
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (!g_aServices[j].fPreInited)
        {
            int rc = g_aServices[j].pDesc->pfnPreInit();
            if (RT_FAILURE(rc))
                return VBoxTrayError("Service '%s' failed pre-init: %Rrc\n", g_aServices[j].pDesc->pszName, rc);
            g_aServices[j].fPreInited = true;
        }
    return VINF_SUCCESS;
}

/**
 * Starts all services.
 *
 * @returns VBox status code.
 * @param   pEnv                Service environment to use.
 */
static int vboxTrayServicesStart(PVBOXTRAYSVCENV pEnv)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);

    VBoxTrayInfo("Starting services ...\n");

    int rc = VINF_SUCCESS;

    size_t cServicesStarted = 0;

    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        PVBOXTRAYSVCINFO pSvc = &g_aServices[i];

        if (!pSvc->fEnabled)
        {
            VBoxTrayInfo("Skipping starting service '%s' (disabled)\n", pSvc->pDesc->pszName);
            continue;
        }

        VBoxTrayInfo("Starting service '%s' ...\n", pSvc->pDesc->pszName);

        pSvc->hThread   = NIL_RTTHREAD;
        pSvc->pvInstance = NULL;
        pSvc->fStarted  = false;
        pSvc->fShutdown = false;

        int rc2 = VINF_SUCCESS;

        if (pSvc->pDesc->pfnInit)
            rc2 = pSvc->pDesc->pfnInit(pEnv, &pSvc->pvInstance);

        if (RT_FAILURE(rc2))
        {
            switch (rc2)
            {
                case VERR_NOT_SUPPORTED:
                    VBoxTrayInfo("Service '%s' is not supported on this system\n", pSvc->pDesc->pszName);
                    rc2 = VINF_SUCCESS; /* Keep going. */
                    break;

                case VERR_HGCM_SERVICE_NOT_FOUND:
                    VBoxTrayInfo("Service '%s' is not available on the host\n", pSvc->pDesc->pszName);
                    rc2 = VINF_SUCCESS; /* Keep going. */
                    break;

                default:
                    VBoxTrayError("Failed to initialize service '%s', rc=%Rrc\n", pSvc->pDesc->pszName, rc2);
                    break;
            }
        }
        else
        {
            if (pSvc->pDesc->pfnWorker)
            {
                rc2 = RTThreadCreate(&pSvc->hThread, vboxTrayServiceThread, pSvc /* pvUser */,
                                     0 /* Default stack size */, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, pSvc->pDesc->pszName);
                if (RT_SUCCESS(rc2))
                {
                    pSvc->fStarted = true;

                    RTThreadUserWait(pSvc->hThread, 30 * 1000 /* Timeout in ms */);
                    if (pSvc->fShutdown)
                    {
                        VBoxTrayError("Service '%s' failed to start!\n", pSvc->pDesc->pszName);
                        rc = VERR_GENERAL_FAILURE;
                    }
                    else
                    {
                        cServicesStarted++;
                        VBoxTrayInfo("Service '%s' started\n", pSvc->pDesc->pszName);
                    }
                }
                else
                {
                    VBoxTrayInfo("Failed to start thread for service '%s': %Rrc\n", rc2);
                    if (pSvc->pDesc->pfnDestroy)
                        pSvc->pDesc->pfnDestroy(pSvc->pvInstance);
                }
            }
        }

        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    VBoxTrayInfo("%zu/%zu service(s) started\n", cServicesStarted, RT_ELEMENTS(g_aServices));
    if (RT_FAILURE(rc))
        VBoxTrayInfo("Some service(s) reported errors when starting -- see log above\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Stops all services.
 *
 * @returns VBox status code.
 * @param   pEnv                Service environment to use.
 */
static int vboxTrayServicesStop(VBOXTRAYSVCENV *pEnv)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);

    VBoxTrayVerbose(1, "Stopping all services ...\n");

    /*
     * Signal all the services.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
        ASMAtomicWriteBool(&g_aServices[i].fShutdown, true);

    /*
     * Do the pfnStop callback on all running services.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        PVBOXTRAYSVCINFO pSvc = &g_aServices[i];
        if (   pSvc->fStarted
            && pSvc->pDesc->pfnStop)
        {
            VBoxTrayVerbose(1, "Calling stop function for service '%s' ...\n", pSvc->pDesc->pszName);
            int rc2 = pSvc->pDesc->pfnStop(pSvc->pvInstance);
            if (RT_FAILURE(rc2))
                VBoxTrayError("Failed to stop service '%s': %Rrc\n", pSvc->pDesc->pszName, rc2);
        }
    }

    VBoxTrayVerbose(2, "All stop functions for services called\n");

    int rc = VINF_SUCCESS;

    /*
     * Wait for all the service threads to complete.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        PVBOXTRAYSVCINFO pSvc = &g_aServices[i];
        if (!pSvc->fEnabled) /* Only stop services which were started before. */
            continue;

        if (pSvc->hThread != NIL_RTTHREAD)
        {
            VBoxTrayVerbose(1, "Waiting for service '%s' to stop ...\n", pSvc->pDesc->pszName);
            int rc2 = VINF_SUCCESS;
            for (int j = 0; j < 30; j++) /* Wait 30 seconds in total */
            {
                rc2 = RTThreadWait(pSvc->hThread, 1000 /* Wait 1 second */, NULL);
                if (RT_SUCCESS(rc2))
                    break;
            }
            if (RT_FAILURE(rc2))
            {
                VBoxTrayError("Service '%s' failed to stop (%Rrc)\n", pSvc->pDesc->pszName, rc2);
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        }

        if (   pSvc->pDesc->pfnDestroy
            && pSvc->pvInstance) /* pvInstance might be NULL if initialization of a service failed. */
        {
            VBoxTrayVerbose(1, "Terminating service '%s' ...\n", pSvc->pDesc->pszName);
            pSvc->pDesc->pfnDestroy(pSvc->pvInstance);
        }
    }

    if (RT_SUCCESS(rc))
        VBoxTrayVerbose(1, "All services stopped\n");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Registers all global window messages of a specific table.
 *
 * @returns VBox status code.
 * @param   pTable              Table to register messages for.
 */
static int vboxTrayRegisterGlobalMessages(PVBOXTRAYGLOBALMSG pTable)
{
    int rc = VINF_SUCCESS;
    if (pTable == NULL) /* No table to register? Skip. */
        return rc;
    while (   pTable->pszName
           && RT_SUCCESS(rc))
    {
        /* Register global accessible window messages. */
        pTable->uMsgID = RegisterWindowMessage(TEXT(pTable->pszName));
        if (!pTable->uMsgID)
        {
            DWORD dwErr = GetLastError();
            VBoxTrayError("Registering global message \"%s\" failed, error = %08X\n", pTable->pszName, dwErr);
            rc = RTErrConvertFromWin32(dwErr);
        }

        /* Advance to next table element. */
        pTable++;
    }
    return rc;
}

/**
 * Handler for global (registered) window messages.
 *
 * @returns \c true if message got handeled, \c false if not.
 * @param   pTable              Message table to look message up in.
 * @param   uMsg                Message ID to handle.
 * @param   wParam              WPARAM of the message.
 * @param   lParam              LPARAM of the message.
 */
static bool vboxTrayHandleGlobalMessages(PVBOXTRAYGLOBALMSG pTable, UINT uMsg,
                                         WPARAM wParam, LPARAM lParam)
{
    if (pTable == NULL)
        return false;
    while (pTable && pTable->pszName)
    {
        if (pTable->uMsgID == uMsg)
        {
            if (pTable->pfnHandler)
                pTable->pfnHandler(wParam, lParam);
            return true;
        }

        /* Advance to next table element. */
        pTable++;
    }
    return false;
}

/**
 * Destroys the invisible tool window of VBoxTray.
 */
static void vboxTrayDestroyToolWindow(void)
{
    if (g_hwndToolWindow)
    {
        /* Destroy the tool window. */
        DestroyWindow(g_hwndToolWindow);
        g_hwndToolWindow = NULL;

        UnregisterClass("VBoxTrayToolWndClass", g_hInstance);
    }
}

/**
 * Creates the invisible tool window of VBoxTray.
 *
 * @returns VBox status code.
 */
static int vboxTrayCreateToolWindow(void)
{
    DWORD dwErr = ERROR_SUCCESS;

    /* Create a custom window class. */
    WNDCLASSEX wc = { 0 };
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_NOCLOSE;
    wc.lpfnWndProc   = (WNDPROC)vboxToolWndProc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "VBoxTrayToolWndClass";

    if (!RegisterClassEx(&wc))
    {
        dwErr = GetLastError();
        VBoxTrayError("Registering invisible tool window failed, error = %08X\n", dwErr);
    }
    else
    {
        /*
         * Create our (invisible) tool window.
         * Note: The window name ("VBoxTrayToolWnd") and class ("VBoxTrayToolWndClass") is
         * needed for posting globally registered messages to VBoxTray and must not be
         * changed! Otherwise things get broken!
         *
         */
        g_hwndToolWindow = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                         "VBoxTrayToolWndClass", "VBoxTrayToolWnd",
                                         WS_POPUPWINDOW,
                                         -200, -200, 100, 100, NULL, NULL, g_hInstance, NULL);
        if (!g_hwndToolWindow)
        {
            dwErr = GetLastError();
            Log(("Creating invisible tool window failed, error = %08X\n", dwErr));
        }
        else
        {
            /* Reload the cursor(s). */
            hlpReloadCursor();

            Log(("Invisible tool window handle = %p\n", g_hwndToolWindow));
        }
    }

    if (dwErr != ERROR_SUCCESS)
         vboxTrayDestroyToolWindow();

    int const rc = RTErrConvertFromWin32(dwErr);

    if (RT_FAILURE(rc))
        VBoxTrayError("Could not create tool window, rc=%Rrc\n", rc);

    return rc;
}

static int vboxTraySetupSeamless(void)
{
    /* We need to setup a security descriptor to allow other processes modify access to the seamless notification event semaphore. */
    SECURITY_ATTRIBUTES     SecAttr;
    DWORD                   dwErr = ERROR_SUCCESS;
    char                    secDesc[SECURITY_DESCRIPTOR_MIN_LENGTH];
    BOOL                    fRC;

    SecAttr.nLength              = sizeof(SecAttr);
    SecAttr.bInheritHandle       = FALSE;
    SecAttr.lpSecurityDescriptor = &secDesc;
    InitializeSecurityDescriptor(SecAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    fRC = SetSecurityDescriptorDacl(SecAttr.lpSecurityDescriptor, TRUE, 0, FALSE);
    if (!fRC)
    {
        dwErr = GetLastError();
        Log(("SetSecurityDescriptorDacl failed with last error = %08X\n", dwErr));
    }
    else
    {
        /* For Vista and up we need to change the integrity of the security descriptor, too. */
        uint64_t const uNtVersion = RTSystemGetNtVersion();
        if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
        {
            BOOL (WINAPI * pfnConvertStringSecurityDescriptorToSecurityDescriptorA)(LPCSTR StringSecurityDescriptor, DWORD StringSDRevision, PSECURITY_DESCRIPTOR  *SecurityDescriptor, PULONG  SecurityDescriptorSize);
            *(void **)&pfnConvertStringSecurityDescriptorToSecurityDescriptorA =
                RTLdrGetSystemSymbol("advapi32.dll", "ConvertStringSecurityDescriptorToSecurityDescriptorA");
            Log(("pfnConvertStringSecurityDescriptorToSecurityDescriptorA = %p\n",
                 RT_CB_LOG_CAST(pfnConvertStringSecurityDescriptorToSecurityDescriptorA)));
            if (pfnConvertStringSecurityDescriptorToSecurityDescriptorA)
            {
                PSECURITY_DESCRIPTOR    pSD;
                PACL                    pSacl          = NULL;
                BOOL                    fSaclPresent   = FALSE;
                BOOL                    fSaclDefaulted = FALSE;

                fRC = pfnConvertStringSecurityDescriptorToSecurityDescriptorA("S:(ML;;NW;;;LW)", /* this means "low integrity" */
                                                                              SDDL_REVISION_1, &pSD, NULL);
                if (!fRC)
                {
                    dwErr = GetLastError();
                    Log(("ConvertStringSecurityDescriptorToSecurityDescriptorA failed with last error = %08X\n", dwErr));
                }
                else
                {
                    fRC = GetSecurityDescriptorSacl(pSD, &fSaclPresent, &pSacl, &fSaclDefaulted);
                    if (!fRC)
                    {
                        dwErr = GetLastError();
                        Log(("GetSecurityDescriptorSacl failed with last error = %08X\n", dwErr));
                    }
                    else
                    {
                        fRC = SetSecurityDescriptorSacl(SecAttr.lpSecurityDescriptor, TRUE, pSacl, FALSE);
                        if (!fRC)
                        {
                            dwErr = GetLastError();
                            Log(("SetSecurityDescriptorSacl failed with last error = %08X\n", dwErr));
                        }
                    }
                }
            }
        }

        if (   dwErr == ERROR_SUCCESS
            && uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* Only for W2K and up ... */
        {
            g_hSeamlessWtNotifyEvent = CreateEvent(&SecAttr, FALSE, FALSE, VBOXHOOK_GLOBAL_WT_EVENT_NAME);
            if (g_hSeamlessWtNotifyEvent == NULL)
            {
                dwErr = GetLastError();
                Log(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            }

            g_hSeamlessKmNotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            if (g_hSeamlessKmNotifyEvent == NULL)
            {
                dwErr = GetLastError();
                Log(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            }
        }
    }

    int const rc = RTErrConvertFromWin32(dwErr);

    if (RT_FAILURE(rc))
        VBoxTrayError("Could not setup seamless, rc=%Rrc\n", rc);

    return rc;
}

static void vboxTrayShutdownSeamless(void)
{
    if (g_hSeamlessWtNotifyEvent)
    {
        CloseHandle(g_hSeamlessWtNotifyEvent);
        g_hSeamlessWtNotifyEvent = NULL;
    }

    if (g_hSeamlessKmNotifyEvent)
    {
        CloseHandle(g_hSeamlessKmNotifyEvent);
        g_hSeamlessKmNotifyEvent = NULL;
    }
}

/**
 * Main routine for starting / stopping all internal services.
 *
 * @returns VBox status code.
 */
static int vboxTrayServiceMain(void)
{
    int rc = VINF_SUCCESS;
    VBoxTrayVerbose(2, "Entering main loop\n");

    g_hStopSem = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_hStopSem == NULL)
    {
        rc = RTErrConvertFromWin32(GetLastError());
        LogFunc(("CreateEvent for stopping VBoxTray failed, rc=%Rrc\n", rc));
    }
    else
    {
        /*
         * Start services listed in the vboxServiceTable.
         */
        VBOXTRAYSVCENV svcEnv;
        svcEnv.hInstance = g_hInstance;

        /* Initializes disp-if to default (XPDM) mode. */
        VBoxDispIfInit(&svcEnv.dispIf); /* Cannot fail atm. */
#ifdef VBOX_WITH_WDDM
        /*
         * For now the display mode will be adjusted to WDDM mode if needed
         * on display service initialization when it detects the display driver type.
         */
#endif
        VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_Init);

        /* Finally start all the built-in services! */
        rc = vboxTrayServicesStart(&svcEnv);
        if (RT_SUCCESS(rc))
        {
            uint64_t const uNtVersion = RTSystemGetNtVersion();
            if (   RT_SUCCESS(rc)
                && uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* Only for W2K and up ... */
            {
                /* We're ready to create the tooltip balloon.
                   Check in 10 seconds (@todo make seconds configurable) ... */
                SetTimer(g_hwndToolWindow,
                         TIMERID_VBOXTRAY_CHECK_HOSTVERSION,
                         10 * 1000, /* 10 seconds */
                         NULL       /* No timerproc */);
            }

            if (RT_SUCCESS(rc))
            {
                /* Report the host that we're up and running! */
                VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_Active);
            }

            if (RT_SUCCESS(rc))
            {
                /* Boost thread priority to make sure we wake up early for seamless window notifications
                 * (not sure if it actually makes any difference though). */
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

                /*
                 * Main execution loop
                 * Wait for the stop semaphore to be posted or a window event to arrive
                 */

                HANDLE hWaitEvent[4] = {0};
                DWORD dwEventCount = 0;

                hWaitEvent[dwEventCount++] = g_hStopSem;

                /* Check if seamless mode is not active and add seamless event to the list */
                if (0 != g_hSeamlessWtNotifyEvent)
                {
                    hWaitEvent[dwEventCount++] = g_hSeamlessWtNotifyEvent;
                }

                if (0 != g_hSeamlessKmNotifyEvent)
                {
                    hWaitEvent[dwEventCount++] = g_hSeamlessKmNotifyEvent;
                }

                if (0 != vboxDtGetNotifyEvent())
                {
                    hWaitEvent[dwEventCount++] = vboxDtGetNotifyEvent();
                }

                LogFlowFunc(("Number of events to wait in main loop: %ld\n", dwEventCount));
                while (true)
                {
                    DWORD waitResult = MsgWaitForMultipleObjectsEx(dwEventCount, hWaitEvent, 500, QS_ALLINPUT, 0);
                    waitResult = waitResult - WAIT_OBJECT_0;

                    /* Only enable for message debugging, lots of traffic! */
                    //Log(("Wait result  = %ld\n", waitResult));

                    if (waitResult == 0)
                    {
                        LogFunc(("Event 'Exit' triggered\n"));
                        /* exit */
                        break;
                    }
                    else
                    {
                        BOOL fHandled = FALSE;
                        if (waitResult < RT_ELEMENTS(hWaitEvent))
                        {
                            if (hWaitEvent[waitResult])
                            {
                                if (hWaitEvent[waitResult] == g_hSeamlessWtNotifyEvent)
                                {
                                    LogFunc(("Event 'Seamless' triggered\n"));

                                    /* seamless window notification */
                                    VBoxSeamlessCheckWindows(false);
                                    fHandled = TRUE;
                                }
                                else if (hWaitEvent[waitResult] == g_hSeamlessKmNotifyEvent)
                                {
                                    LogFunc(("Event 'Km Seamless' triggered\n"));

                                    /* seamless window notification */
                                    VBoxSeamlessCheckWindows(true);
                                    fHandled = TRUE;
                                }
                                else if (hWaitEvent[waitResult] == vboxDtGetNotifyEvent())
                                {
                                    LogFunc(("Event 'Dt' triggered\n"));
                                    vboxDtDoCheck();
                                    fHandled = TRUE;
                                }
                            }
                        }

                        if (!fHandled)
                        {
                            /* timeout or a window message, handle it */
                            MSG msg;
                            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                            {
#ifdef DEBUG_andy
                                LogFlowFunc(("PeekMessage %u\n", msg.message));
#endif
                                if (msg.message == WM_QUIT)
                                {
                                    LogFunc(("Terminating ...\n"));
                                    SetEvent(g_hStopSem);
                                }
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                            }
                        }
                    }
                }
                LogFunc(("Returned from main loop, exiting ...\n"));
            }

        } /* Services started */

        LogFunc(("Waiting for services to stop ...\n"));

        VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_Terminating);

        vboxTrayServicesStop(&svcEnv);

        CloseHandle(g_hStopSem);

    } /* Stop event created */

    VBoxTrayVerbose(2, "Leaving main loop with %Rrc\n", rc);
    return rc;
}

/**
 * Attaches to a parent console (if any) or creates an own (dedicated) console window.
 *
 * @returns VBox status code.
 */
static int vboxTrayAttachConsole(void)
{
    if (    g_fHasConsole       /* Console already attached? Bail out. */
        || !g_pfnAttachConsole) /* AttachConsole() not available (NT <= 4.0)? */
        return VINF_SUCCESS;

    /* As we run with the WINDOWS subsystem, we need to either attach to or create an own console
     * to get any stdout / stderr output. */
    bool fAllocConsole = false;
    if (!g_pfnAttachConsole(ATTACH_PARENT_PROCESS))
        fAllocConsole = true;

    if (fAllocConsole)
    {
        AssertPtrReturn(g_pfnAllocConsole, VERR_NOT_AVAILABLE);
        if (!g_pfnAllocConsole())
            VBoxTrayShowError("Unable to attach to or allocate a console!");
        /* Continue running. */
    }

    RTFILE hStdIn;
    RTFileFromNative(&hStdIn,  (RTHCINTPTR)GetStdHandle(STD_INPUT_HANDLE));
    /** @todo Closing of standard handles not support via IPRT (yet). */
    RTStrmOpenFileHandle(hStdIn, "r", 0, &g_pStdIn);

    RTFILE hStdOut;
    RTFileFromNative(&hStdOut,  (RTHCINTPTR)GetStdHandle(STD_OUTPUT_HANDLE));
    /** @todo Closing of standard handles not support via IPRT (yet). */
    RTStrmOpenFileHandle(hStdOut, "wt", 0, &g_pStdOut);

    RTFILE hStdErr;
    RTFileFromNative(&hStdErr,  (RTHCINTPTR)GetStdHandle(STD_ERROR_HANDLE));
    RTStrmOpenFileHandle(hStdErr, "wt", 0, &g_pStdErr);

    if (!fAllocConsole) /* When attaching to the parent console, make sure we start on a fresh line. */
        RTPrintf("\n");

    g_fHasConsole = true;

    return VINF_SUCCESS;
}

/**
 * Detaches from the (parent) console.
 */
static void vboxTrayDetachConsole()
{
    g_fHasConsole = false;
}

/**
 * Early initialization code, required for resolving dynamic symbols and such.
 *
 * @returns VBox status code.
 */
static int vboxTrayPreInit()
{
    RTLDRMOD hMod;
    int rc = RTLdrLoadSystem("kernel32.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        /* only W2K+, ignore rc */ RTLdrGetSymbol(hMod, "AllocConsole", (void **)&g_pfnAllocConsole);
        /* only W2K+, ignore rc */ RTLdrGetSymbol(hMod, "AttachConsole", (void **)&g_pfnAttachConsole);

        RTLdrClose(hMod);
    }

    return rc;
}

/**
 * Destroys VBoxTray.
 *
 * @returns RTEXITCODE_SUCCESS.
 */
static RTEXITCODE vboxTrayDestroy()
{
    vboxTrayDetachConsole();

    /* Release instance mutex. */
    if (g_hMutexAppRunning != NULL)
    {
        CloseHandle(g_hMutexAppRunning);
        g_hMutexAppRunning = NULL;
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Prints the help to either a message box or a console (if attached).
 *
 * @returns RTEXITCODE_SYNTAX.
 * @param   cArgs               Number of arguments given via argc.
 * @param   papszArgs           Arguments given specified by \a cArgs.
 */
static RTEXITCODE vboxTrayPrintHelp(int cArgs, char **papszArgs)
{
    RT_NOREF(cArgs);

    char szServices[64] = { 0 };
    for (size_t i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        char szName[RTTHREAD_NAME_LEN];
        int rc2 = RTStrCopy(szName, sizeof(szName), g_aServices[i].pDesc->pszName);
        RTStrToLower(szName); /* To make it easier for users to recognize the service name via command line. */
        AssertRCBreak(rc2);
        if (i > 0)
        {
            rc2 = RTStrCat(szServices, sizeof(szServices), ", ");
            AssertRCBreak(rc2);
        }
        rc2 = RTStrCat(szServices, sizeof(szServices), szName);
        AssertRCBreak(rc2);
    }

    VBoxTrayShowMsgBox(VBOX_PRODUCT " - " VBOX_VBOXTRAY_TITLE,
                       MB_ICONINFORMATION,
                       VBOX_PRODUCT " %s v%u.%u.%ur%u\n"
                       "Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n"
                       "Command Line Parameters:\n\n"
                       "-d, --debug\n"
                       "    Enables debugging mode\n"
                       "-f, --foreground\n"
                       "    Enables running in foreground\n"
                       "-l, --logfile <file>\n"
                       "    Enables logging to a file\n"
                       "-v, --verbose\n"
                       "    Increases verbosity\n"
                       "-V, --version\n"
                       "   Displays version number and exit\n"
                       "-?, -h, --help\n"
                       "   Displays this help text and exit\n"
                       "\n"
                       "Service parameters:\n\n"
                       "--enable-<service-name>\n"
                       "   Enables the given service\n"
                       "--disable-<service-name>\n"
                       "   Disables the given service\n"
                       "--only-<service-name>\n"
                       "   Only starts the given service\n"
                       "\n"
                       "Examples:\n"
                       "  %s -vvv --logfile C:\\Temp\\VBoxTray.log\n"
                       "  %s --foreground -vvvv --only-draganddrop\n"
                       "\n"
                       "Available services: %s\n\n",
                       VBOX_VBOXTRAY_TITLE, VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV,
                       papszArgs[0], papszArgs[0], szServices);

    vboxTrayDestroy();

    return RTEXITCODE_SYNTAX;
}

/**
 * Main function
 */
int main(int cArgs, char **papszArgs)
{
    int rc = RTR3InitExe(cArgs, &papszArgs, RTR3INIT_FLAGS_STANDALONE_APP);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    rc = vboxTrayPreInit();
    if (RT_FAILURE(rc))
        return VBoxTrayShowError(VBOX_VBOXTRAY_TITLE " Pre-init failed: %Rrc\n", rc);

    /* If a debugger is present, we always want to attach a console. */
    if (IsDebuggerPresent())
        vboxTrayAttachConsole();

    /*
     * Parse the top level arguments until we find a command.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--debug",            'd',                         RTGETOPT_REQ_NOTHING },
        { "/debug",             'd',                         RTGETOPT_REQ_NOTHING },
        { "--foreground",       'f',                         RTGETOPT_REQ_NOTHING },
        { "/foreground",        'f',                         RTGETOPT_REQ_NOTHING },
        { "--help",             'h',                         RTGETOPT_REQ_NOTHING },
        { "-help",              'h',                         RTGETOPT_REQ_NOTHING },
        { "/help",              'h',                         RTGETOPT_REQ_NOTHING },
        { "/?",                 'h',                         RTGETOPT_REQ_NOTHING },
        { "--logfile",          'l',                         RTGETOPT_REQ_STRING  },
        { "--verbose",          'v',                         RTGETOPT_REQ_NOTHING },
        { "--version",          'V',                         RTGETOPT_REQ_NOTHING },
    };

    char szLogFile[RTPATH_MAX] = {0};

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc\n", rc);

    int ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'h':
                return vboxTrayPrintHelp(cArgs, papszArgs);

            case 'd':
            {
                /* ignore rc */ vboxTrayAttachConsole();
                g_cVerbosity = 4; /* Set verbosity to level 4. */
                break;
            }

            case 'f':
            {
                /* ignore rc */ vboxTrayAttachConsole();
                /* Don't increase verbosity automatically here. */
                break;
            }

            case 'l':
            {
                if (*ValueUnion.psz == '\0')
                    szLogFile[0] = '\0';
                else
                {
                    rc = RTPathAbs(ValueUnion.psz, szLogFile, sizeof(szLogFile));
                    if (RT_FAILURE(rc))
                    {
                        int rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAbs failed on log file path: %Rrc (%s)",
                                                    rc, ValueUnion.psz);
                        vboxTrayDestroy();
                        return rcExit;
                    }
                }
                break;
            }

            case 'v':
                g_cVerbosity++;
                break;

            case 'V':
                VBoxTrayShowMsgBox(VBOX_PRODUCT " - " VBOX_VBOXTRAY_TITLE,
                                   MB_ICONINFORMATION,
                                   "%u.%u.%ur%u", VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV);
                return vboxTrayDestroy();

            default:
            {
                const  char *psz = ValueUnion.psz;
                size_t const cch = strlen(ValueUnion.psz);
                bool fFound = false;

                if (cch > sizeof("--enable-") && !memcmp(psz, RT_STR_TUPLE("--enable-")))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("--enable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = true;

                if (cch > sizeof("--disable-") && !memcmp(psz, RT_STR_TUPLE("--disable-")))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("--disable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = false;

                if (cch > sizeof("--only-") && !memcmp(psz, RT_STR_TUPLE("--only-")))
                    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
                    {
                        g_aServices[j].fEnabled = !RTStrICmp(psz + sizeof("--only-") - 1, g_aServices[j].pDesc->pszName);
                        if (g_aServices[j].fEnabled)
                            fFound = true;
                    }

                if (!fFound)
                {
                    rc = vboxTrayServicesLazyPreInit();
                    if (RT_FAILURE(rc))
                        break;
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                    {
                        rc = g_aServices[j].pDesc->pfnOption(NULL, cArgs, papszArgs, NULL);
                        fFound = rc == VINF_SUCCESS;
                        if (fFound)
                            break;
                        if (rc != -1) /* Means not parsed. */
                            break;
                    }
                }
                if (!fFound)
                {
                    RTGetOptPrintError(ch, &ValueUnion); /* Only shown on console. */
                    return vboxTrayPrintHelp(cArgs, papszArgs);
                }

                continue;
            }
        }
    }

    if (RT_FAILURE(rc))
    {
        vboxTrayDestroy();
        return RTEXITCODE_FAILURE;
    }

    /**
     * VBoxTray already running? Bail out.
     *
     * Note: Do not use a global namespace ("Global\\") for mutex name here,
     * will blow up NT4 compatibility!
     */
    g_hMutexAppRunning = CreateMutex(NULL, FALSE, VBOX_VBOXTRAY_TITLE);
    if (   g_hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        VBoxTrayError(VBOX_VBOXTRAY_TITLE " already running!\n");
        return vboxTrayDestroy();
    }

    /* Set the instance handle. */
#ifdef IPRT_NO_CRT
    Assert(g_hInstance == NULL); /* Make sure this isn't set before by WinMain(). */
    g_hInstance = GetModuleHandleW(NULL);
#endif

    rc = VbglR3Init();
    if (RT_SUCCESS(rc))
    {
        rc = VBoxTrayLogCreate(szLogFile[0] ? szLogFile : NULL);
        if (RT_SUCCESS(rc))
        {
            VBoxTrayInfo("Verbosity level: %d\n", g_cVerbosity);

            rc = vboxTrayCreateToolWindow();
            if (RT_SUCCESS(rc))
                rc = vboxTrayCreateTrayIcon();

            VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_PreInit);

            if (RT_SUCCESS(rc))
            {
                VBoxCapsInit();

                rc = vboxStInit(g_hwndToolWindow);
                if (!RT_SUCCESS(rc))
                {
                    LogFlowFunc(("vboxStInit failed, rc=%Rrc\n", rc));
                    /* ignore the St Init failure. this can happen for < XP win that do not support WTS API
                     * in that case the session is treated as active connected to the physical console
                     * (i.e. fallback to the old behavior that was before introduction of VBoxSt) */
                    Assert(vboxStIsActiveConsole());
                }

                rc = vboxDtInit();
                if (RT_FAILURE(rc))
                {
                    /* ignore the Dt Init failure. this can happen for < XP win that do not support WTS API
                     * in that case the session is treated as active connected to the physical console
                     * (i.e. fallback to the old behavior that was before introduction of VBoxSt) */
                    Assert(vboxDtIsInputDesktop());
                }

                VBoxAcquireGuestCaps(VMMDEV_GUEST_SUPPORTS_SEAMLESS | VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0, true);

                vboxTraySetupSeamless();

                rc = vboxTrayServiceMain();
                /* Note: Do *not* overwrite rc in the following code, as this acts as the exit code. */

                vboxTrayShutdownSeamless();

                /* it should be safe to call vboxDtTerm even if vboxStInit above failed */
                vboxDtTerm();

                /* it should be safe to call vboxStTerm even if vboxStInit above failed */
                vboxStTerm();

                VBoxCapsTerm();
            }

            vboxTrayRemoveTrayIcon();
            vboxTrayDestroyToolWindow();

            if (RT_SUCCESS(rc))

                VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_Terminated);
            else
                VBoxTrayHlpReportStatus(VBoxGuestFacilityStatus_Failed);

            VBoxTrayInfo("VBoxTray terminated with %Rrc\n", rc);

            VBoxTrayLogDestroy();
        }

        VbglR3Term();
    }
    else
    {
        /* Only show something if started in verbose mode.
         * Otherwise just fail silently as we ever did. Needed in order to not break installs on non-VMs. */
        if (g_cVerbosity)
        {
            if (rc == VERR_OPEN_FAILED)
                VBoxTrayShowError("Error opening a connection to the VBoxGuest.sys driver.\n\n"
                                  "This might be due to not having the Windows Guest Additions installed\n"
                                  "or that something went wrong when installing those.\n\n"
                                  "Re-installing the Guest Additions might resolve the issue.\n");
            else
                VBoxTrayShowError("VbglR3Init failed: %Rrc\n", rc);
        }
    }

    vboxTrayDestroy();

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

#ifndef IPRT_NO_CRT
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    RT_NOREF(hPrevInstance, lpCmdLine, nCmdShow);

    g_hInstance = hInstance;

    return main(__argc, __argv);
}
#endif /* IPRT_NO_CRT */

/**
 * Window procedure for our main tool window.
 */
static LRESULT CALLBACK vboxToolWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LogFlowFunc(("hWnd=%p, uMsg=%u\n", hWnd, uMsg));

    switch (uMsg)
    {
        case WM_CREATE:
        {
            LogFunc(("Tool window created\n"));

            int rc = vboxTrayRegisterGlobalMessages(&g_vboxGlobalMessageTable[0]);
            if (RT_FAILURE(rc))
                LogFunc(("Error registering global window messages, rc=%Rrc\n", rc));
            return 0;
        }

        case WM_CLOSE:
            return 0;

        case WM_DESTROY:
        {
            LogFunc(("Tool window destroyed\n"));
            KillTimer(g_hwndToolWindow, TIMERID_VBOXTRAY_CHECK_HOSTVERSION);
            return 0;
        }

        case WM_TIMER:
        {
            if (VBoxCapsCheckTimer(wParam))
                return 0;
            if (vboxDtCheckTimer(wParam))
                return 0;
            if (vboxStCheckTimer(wParam))
                return 0;

            switch (wParam)
            {
                case TIMERID_VBOXTRAY_CHECK_HOSTVERSION:
                {
                    if (RT_SUCCESS(VBoxCheckHostVersion()))
                    {
                        /* After a successful run we don't need to check again. */
                        KillTimer(g_hwndToolWindow, TIMERID_VBOXTRAY_CHECK_HOSTVERSION);
                    }

                    return 0;
                }

                default:
                    break;
            }

            break; /* Make sure other timers get processed the usual way! */
        }

        case WM_VBOXTRAY_TRAY_ICON:
        {
            switch (LOWORD(lParam))
            {
                case WM_LBUTTONDBLCLK:
                    break;
                case WM_RBUTTONDOWN:
                {
                    if (!g_cVerbosity) /* Don't show menu when running in non-verbose mode. */
                        break;

                    POINT lpCursor;
                    if (GetCursorPos(&lpCursor))
                    {
                        HMENU hContextMenu = CreatePopupMenu();
                        if (hContextMenu)
                        {
                            UINT_PTR uMenuItem = 9999;
                            UINT     fMenuItem = MF_BYPOSITION | MF_STRING;
                            if (InsertMenuW(hContextMenu, UINT_MAX, fMenuItem, uMenuItem, L"Exit"))
                            {
                                SetForegroundWindow(hWnd);

                                const bool fBlockWhileTracking = true;

                                UINT fTrack = TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN;

                                if (fBlockWhileTracking)
                                    fTrack |= TPM_RETURNCMD | TPM_NONOTIFY;

                                uMsg = TrackPopupMenu(hContextMenu, fTrack, lpCursor.x, lpCursor.y, 0, hWnd, NULL);
                                if (   uMsg
                                    && fBlockWhileTracking)
                                {
                                    if (uMsg == uMenuItem)
                                        PostMessage(g_hwndToolWindow, WM_QUIT, 0, 0);
                                }
                                else if (!uMsg)
                                    LogFlowFunc(("Tracking popup menu failed with %ld\n", GetLastError()));
                            }

                            DestroyMenu(hContextMenu);
                        }
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_VBOX_SEAMLESS_ENABLE:
        {
            VBoxCapsEntryFuncStateSet(VBOXCAPS_ENTRY_IDX_SEAMLESS, VBOXCAPS_ENTRY_FUNCSTATE_STARTED);
            if (VBoxCapsEntryIsEnabled(VBOXCAPS_ENTRY_IDX_SEAMLESS))
                VBoxSeamlessCheckWindows(true);
            return 0;
        }

        case WM_VBOX_SEAMLESS_DISABLE:
        {
            VBoxCapsEntryFuncStateSet(VBOXCAPS_ENTRY_IDX_SEAMLESS, VBOXCAPS_ENTRY_FUNCSTATE_SUPPORTED);
            return 0;
        }

        case WM_DISPLAYCHANGE:
            ASMAtomicUoWriteU32(&g_fGuestDisplaysChanged, 1);
            // No break or return is intentional here.
        case WM_VBOX_SEAMLESS_UPDATE:
        {
            if (VBoxCapsEntryIsEnabled(VBOXCAPS_ENTRY_IDX_SEAMLESS))
                VBoxSeamlessCheckWindows(true);
            return 0;
        }

        case WM_VBOX_GRAPHICS_SUPPORTED:
        {
            VBoxGrapicsSetSupported(TRUE);
            return 0;
        }

        case WM_VBOX_GRAPHICS_UNSUPPORTED:
        {
            VBoxGrapicsSetSupported(FALSE);
            return 0;
        }

        case WM_WTSSESSION_CHANGE:
        {
            BOOL fOldAllowedState = VBoxConsoleIsAllowed();
            if (vboxStHandleEvent(wParam))
            {
                if (!VBoxConsoleIsAllowed() != !fOldAllowedState)
                    VBoxConsoleEnable(!fOldAllowedState);
            }
            return 0;
        }

        default:
        {
            /* Handle all globally registered window messages. */
            if (vboxTrayHandleGlobalMessages(&g_vboxGlobalMessageTable[0], uMsg,
                                             wParam, lParam))
            {
                return 0; /* We handled the message. @todo Add return value!*/
            }
            break; /* We did not handle the message, dispatch to DefWndProc. */
        }
    }

    /* Only if message was *not* handled by our switch above, dispatch to DefWindowProc. */
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void VBoxGrapicsSetSupported(BOOL fSupported)
{
    VBoxConsoleCapSetSupported(VBOXCAPS_ENTRY_IDX_GRAPHICS, fSupported);
}

