/* $Id: VBoxTray.cpp 107164 2024-11-26 07:15:16Z andreas.loeffler@oracle.com $ */
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
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
int                   g_cVerbosity             = 0;
HANDLE                g_hStopSem;
HANDLE                g_hSeamlessWtNotifyEvent = 0;
HANDLE                g_hSeamlessKmNotifyEvent = 0;
HINSTANCE             g_hInstance              = NULL;
HWND                  g_hwndToolWindow;
NOTIFYICONDATA        g_NotifyIconData;

uint32_t              g_fGuestDisplaysChanged = 0;

static PRTLOGGER      g_pLoggerRelease = NULL;           /**< This is actually the debug logger in DEBUG builds! */
static uint32_t       g_cHistory = 10;                   /**< Enable log rotation, 10 files. */
static uint32_t       g_uHistoryFileTime = RT_SEC_1DAY;  /**< Max 1 day per file. */
static uint64_t       g_uHistoryFileSize = 100 * _1M;    /**< Max 100MB per file. */

#ifdef DEBUG_andy
static VBOXSERVICEINFO g_aServices[] =
{
    { &g_SvcDescClipboard,      NIL_RTTHREAD, NULL, false, false, false, false, true }
};
#else
/**
 * The details of the services that has been compiled in.
 */
static VBOXSERVICEINFO g_aServices[] =
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
#endif

/* The global message table. */
static VBOXGLOBALMESSAGE g_vboxGlobalMessageTable[] =
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
 * @return  IPRT status code.
 * @param   wParam
 * @param   lParam
 */
static int vboxTrayGlMsgTaskbarCreated(WPARAM wParam, LPARAM lParam)
{
    RT_NOREF(wParam, lParam);
    return vboxTrayCreateTrayIcon();
}

static int vboxTrayCreateTrayIcon(void)
{
    HICON hIcon = LoadIcon(g_hInstance, "IDI_ICON1"); /* see Artwork/win/TemplateR3.rc */
    if (hIcon == NULL)
    {
        DWORD dwErr = GetLastError();
        LogFunc(("Could not load tray icon, error %08X\n", dwErr));
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
        DWORD dwErr = GetLastError();
        LogFunc(("Could not create tray icon, error=%ld\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
        RT_ZERO(g_NotifyIconData);
    }

    if (hIcon)
        DestroyIcon(hIcon);
    return rc;
}

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
    PVBOXSERVICEINFO pSvc = (PVBOXSERVICEINFO)pvUser;
    AssertPtr(pSvc);

#ifndef RT_OS_WINDOWS
    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);
#endif

    int rc = pSvc->pDesc->pfnWorker(pSvc->pInstance, &pSvc->fShutdown);
    ASMAtomicXchgBool(&pSvc->fShutdown, true);
    RTThreadUserSignal(ThreadSelf);

    LogFunc(("Worker for '%s' ended with %Rrc\n", pSvc->pDesc->pszName, rc));
    return rc;
}

static int vboxTrayServicesStart(PVBOXSERVICEENV pEnv)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);

    LogRel(("Starting services ...\n"));

    int rc = VINF_SUCCESS;

    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        PVBOXSERVICEINFO pSvc = &g_aServices[i];
        LogRel(("Starting service '%s' ...\n", pSvc->pDesc->pszName));

        pSvc->hThread   = NIL_RTTHREAD;
        pSvc->pInstance = NULL;
        pSvc->fStarted  = false;
        pSvc->fShutdown = false;

        int rc2 = VINF_SUCCESS;

        if (pSvc->pDesc->pfnInit)
            rc2 = pSvc->pDesc->pfnInit(pEnv, &pSvc->pInstance);

        if (RT_FAILURE(rc2))
        {
            switch (rc2)
            {
                case VERR_NOT_SUPPORTED:
                    LogRel(("Service '%s' is not supported on this system\n", pSvc->pDesc->pszName));
                    rc2 = VINF_SUCCESS; /* Keep going. */
                    break;

                case VERR_HGCM_SERVICE_NOT_FOUND:
                    LogRel(("Service '%s' is not available on the host\n", pSvc->pDesc->pszName));
                    rc2 = VINF_SUCCESS; /* Keep going. */
                    break;

                default:
                    LogRel(("Failed to initialize service '%s', rc=%Rrc\n", pSvc->pDesc->pszName, rc2));
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
                        LogRel(("Service '%s' failed to start!\n", pSvc->pDesc->pszName));
                        rc = VERR_GENERAL_FAILURE;
                    }
                    else
                        LogRel(("Service '%s' started\n", pSvc->pDesc->pszName));
                }
                else
                {
                    LogRel(("Failed to start thread for service '%s': %Rrc\n", rc2));
                    if (pSvc->pDesc->pfnDestroy)
                        pSvc->pDesc->pfnDestroy(pSvc->pInstance);
                }
            }
        }

        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    if (RT_SUCCESS(rc))
        LogRel(("All services started\n"));
    else
        LogRel(("Services started, but some with errors\n"));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vboxTrayServicesStop(VBOXSERVICEENV *pEnv)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);

    LogRel2(("Stopping all services ...\n"));

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
        PVBOXSERVICEINFO pSvc = &g_aServices[i];
        if (   pSvc->fStarted
            && pSvc->pDesc->pfnStop)
        {
            LogRel2(("Calling stop function for service '%s' ...\n", pSvc->pDesc->pszName));
            int rc2 = pSvc->pDesc->pfnStop(pSvc->pInstance);
            if (RT_FAILURE(rc2))
                LogRel(("Failed to stop service '%s': %Rrc\n", pSvc->pDesc->pszName, rc2));
        }
    }

    LogRel2(("All stop functions for services called\n"));

    int rc = VINF_SUCCESS;

    /*
     * Wait for all the service threads to complete.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        PVBOXSERVICEINFO pSvc = &g_aServices[i];
        if (!pSvc->fEnabled) /* Only stop services which were started before. */
            continue;

        if (pSvc->hThread != NIL_RTTHREAD)
        {
            LogRel2(("Waiting for service '%s' to stop ...\n", pSvc->pDesc->pszName));
            int rc2 = VINF_SUCCESS;
            for (int j = 0; j < 30; j++) /* Wait 30 seconds in total */
            {
                rc2 = RTThreadWait(pSvc->hThread, 1000 /* Wait 1 second */, NULL);
                if (RT_SUCCESS(rc2))
                    break;
            }
            if (RT_FAILURE(rc2))
            {
                LogRel(("Service '%s' failed to stop (%Rrc)\n", pSvc->pDesc->pszName, rc2));
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        }

        if (   pSvc->pDesc->pfnDestroy
            && pSvc->pInstance) /* pInstance might be NULL if initialization of a service failed. */
        {
            LogRel2(("Terminating service '%s' ...\n", pSvc->pDesc->pszName));
            pSvc->pDesc->pfnDestroy(pSvc->pInstance);
        }
    }

    if (RT_SUCCESS(rc))
        LogRel(("All services stopped\n"));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vboxTrayRegisterGlobalMessages(PVBOXGLOBALMESSAGE pTable)
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
            Log(("Registering global message \"%s\" failed, error = %08X\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }

        /* Advance to next table element. */
        pTable++;
    }
    return rc;
}

static bool vboxTrayHandleGlobalMessages(PVBOXGLOBALMESSAGE pTable, UINT uMsg,
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
 * Header/footer callback for the release logger.
 *
 * @param   pLoggerRelease
 * @param   enmPhase
 * @param   pfnLog
 */
static DECLCALLBACK(void) vboxTrayLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "VBoxTray %s r%s %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), VBOX_BUILD_TARGET,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef VBOX_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   VBOX_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */;
    }
}

/**
 * Creates the default release logger outputting to the specified file.
 *
 * @return  IPRT status code.
 * @param   pszLogFile          Path to log file to use. Can be NULL if not needed.
 */
static int vboxTrayLogCreate(const char *pszLogFile)
{
    /* Create release (or debug) logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
#ifdef DEBUG
    static const char s_szEnvVarPfx[] = "VBOXTRAY_LOG";
    static const char s_szGroupSettings[] = "all.e.l.f";
#else
    static const char s_szEnvVarPfx[] = "VBOXTRAY_RELEASE_LOG";
    static const char s_szGroupSettings[] = "all";
#endif
    RTERRINFOSTATIC ErrInfo;
    int rc = RTLogCreateEx(&g_pLoggerRelease, s_szEnvVarPfx,
                           RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG | RTLOGFLAGS_USECRLF,
                           s_szGroupSettings, RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT,
                           vboxTrayLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           RTErrInfoInitStatic(&ErrInfo), "%s", pszLogFile ? pszLogFile : "");
    if (RT_SUCCESS(rc))
    {
#ifdef DEBUG
        /* Register this logger as the _debug_ logger. */
        RTLogSetDefaultInstance(g_pLoggerRelease);
#else
        /* Register this logger as the release logger. */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);
#endif
        /* If verbosity is explicitly set, make sure to increase the logging levels for
         * the logging groups we offer functionality for in VBoxTray. */
        if (g_cVerbosity)
        {
            /* All groups we want to enable logging for VBoxTray. */
#ifdef DEBUG
            const char *apszGroups[] = { "guest_dnd", "shared_clipboard" };
#else /* For release builds we always want all groups being logged in verbose mode. Don't change this! */
            const char *apszGroups[] = { "all" };
#endif
            char        szGroupSettings[_1K];

            szGroupSettings[0] = '\0';

            for (size_t i = 0; i < RT_ELEMENTS(apszGroups); i++)
            {
                if (i > 0)
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), "+");
                if (RT_SUCCESS(rc))
                    rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), apszGroups[i]);
                if (RT_FAILURE(rc))
                    break;

                switch (g_cVerbosity)
                {
                    case 1:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2");
                        break;

                    case 2:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3");
                        break;

                    case 3:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4");
                        break;

                    case 4:
                        RT_FALL_THROUGH();
                    default:
                        rc = RTStrCat(szGroupSettings, sizeof(szGroupSettings), ".e.l.l2.l3.l4.f");
                        break;
                }

                if (RT_FAILURE(rc))
                    break;
            }

            LogRel(("Verbose log settings are: %s\n", szGroupSettings));

            if (RT_SUCCESS(rc))
                rc = RTLogGroupSettings(g_pLoggerRelease, szGroupSettings);
            if (RT_FAILURE(rc))
                RTMsgError("Setting log group settings failed, rc=%Rrc\n", rc);
        }

        /* Explicitly flush the log in case of VBOXTRAY_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);
    }
    else
        VBoxTrayShowError(ErrInfo.szMsg);

    return rc;
}

static void vboxTrayLogDestroy(void)
{
    /* Only want to destroy the release logger before calling exit(). The debug
       logger can be useful after that point... */
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
RTEXITCODE VBoxTrayShowError(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);

    AssertPtr(psz);
    LogRel(("Error: %s", psz));

    MessageBox(GetDesktopWindow(), psz, "VBoxTray - Error", MB_OK | MB_ICONERROR);

    RTStrFree(psz);

    return RTEXITCODE_FAILURE;
}

static void vboxTrayDestroyToolWindow(void)
{
    if (g_hwndToolWindow)
    {
        Log(("Destroying tool window ...\n"));

        /* Destroy the tool window. */
        DestroyWindow(g_hwndToolWindow);
        g_hwndToolWindow = NULL;

        UnregisterClass("VBoxTrayToolWndClass", g_hInstance);
    }
}

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
        Log(("Registering invisible tool window failed, error = %08X\n", dwErr));
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
    return RTErrConvertFromWin32(dwErr);
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
    return RTErrConvertFromWin32(dwErr);
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

static int vboxTrayServiceMain(void)
{
    int rc = VINF_SUCCESS;
    LogFunc(("Entering vboxTrayServiceMain\n"));

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
        VBOXSERVICEENV svcEnv;
        svcEnv.hInstance = g_hInstance;

        /* Initializes disp-if to default (XPDM) mode. */
        VBoxDispIfInit(&svcEnv.dispIf); /* Cannot fail atm. */
    #ifdef VBOX_WITH_WDDM
        /*
         * For now the display mode will be adjusted to WDDM mode if needed
         * on display service initialization when it detects the display driver type.
         */
    #endif

        /* Finally start all the built-in services! */
        rc = vboxTrayServicesStart(&svcEnv);
        if (RT_FAILURE(rc))
        {
            /* Terminate service if something went wrong. */
            vboxTrayServicesStop(&svcEnv);
        }
        else
        {
            uint64_t const uNtVersion = RTSystemGetNtVersion();
            rc = vboxTrayCreateTrayIcon();
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
                hlpReportStatus(VBoxGuestFacilityStatus_Active);
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
            LogFunc(("Waiting for services to stop ...\n"));
            vboxTrayServicesStop(&svcEnv);
        } /* Services started */
        CloseHandle(g_hStopSem);
    } /* Stop event created */

    vboxTrayRemoveTrayIcon();

    LogFunc(("Leaving with rc=%Rrc\n", rc));
    return rc;
}

/**
 * Main function
 */
int main(int cArgs, char **papszArgs)
{
    int rc = RTR3InitExe(cArgs, &papszArgs, RTR3INIT_FLAGS_STANDALONE_APP);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse the top level arguments until we find a command.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
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
                hlpShowMessageBox(VBOX_PRODUCT " - " VBOX_VBOXTRAY_TITLE,
                                  MB_ICONINFORMATION,
                     "-- " VBOX_PRODUCT " %s v%u.%u.%ur%u --\n\n"
                     "Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n"
                     "Command Line Parameters:\n\n"
                     "-l, --logfile <file>\n"
                     "    Enables logging to a file\n"
                     "-v, --verbose\n"
                     "    Increases verbosity\n"
                     "-V, --version\n"
                     "   Displays version number and exit\n"
                     "-?, -h, --help\n"
                     "   Displays this help text and exit\n"
                     "\n"
                     "Examples:\n"
                     "  %s -vvv\n",
                     VBOX_VBOXTRAY_TITLE, VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV,
                     papszArgs[0], papszArgs[0]);
                return RTEXITCODE_SUCCESS;

            case 'l':
                if (*ValueUnion.psz == '\0')
                    szLogFile[0] = '\0';
                else
                {
                    rc = RTPathAbs(ValueUnion.psz, szLogFile, sizeof(szLogFile));
                    if (RT_FAILURE(rc))
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAbs failed on log file path: %Rrc (%s)",
                                              rc, ValueUnion.psz);
                }
                break;

            case 'v':
                g_cVerbosity++;
                break;

            case 'V':
                hlpShowMessageBox(VBOX_VBOXTRAY_TITLE, MB_ICONINFORMATION,
                                  "Version: %u.%u.%ur%u",
                                  VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV);
                return RTEXITCODE_SUCCESS;

            default:
                rc = RTGetOptPrintError(ch, &ValueUnion);
                break;
        }
    }

    /* Note: Do not use a global namespace ("Global\\") for mutex name here,
     * will blow up NT4 compatibility! */
    HANDLE hMutexAppRunning = CreateMutex(NULL, FALSE, VBOX_VBOXTRAY_TITLE);
    if (   hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        /* VBoxTray already running? Bail out. */
        CloseHandle (hMutexAppRunning);
        hMutexAppRunning = NULL;
        return RTEXITCODE_SUCCESS;
    }

    rc = VbglR3Init();
    if (RT_SUCCESS(rc))
    {
        rc = vboxTrayLogCreate(szLogFile[0] ? szLogFile : NULL);
        if (RT_SUCCESS(rc))
        {
            LogRel(("Verbosity level: %d\n", g_cVerbosity));

            /* Log the major windows NT version: */
            uint64_t const uNtVersion = RTSystemGetNtVersion();
            LogRel(("Windows version %u.%u build %u (uNtVersion=%#RX64)\n", RTSYSTEM_NT_VERSION_GET_MAJOR(uNtVersion),
                    RTSYSTEM_NT_VERSION_GET_MINOR(uNtVersion), RTSYSTEM_NT_VERSION_GET_BUILD(uNtVersion), uNtVersion ));

            /* Set the instance handle. */
#ifdef IPRT_NO_CRT
            Assert(g_hInstance == NULL); /* Make sure this isn't set before by WinMain(). */
            g_hInstance = GetModuleHandleW(NULL);
#endif
            hlpReportStatus(VBoxGuestFacilityStatus_Init);
            rc = vboxTrayCreateToolWindow();
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
                if (!RT_SUCCESS(rc))
                {
                    LogFlowFunc(("vboxDtInit failed, rc=%Rrc\n", rc));
                    /* ignore the Dt Init failure. this can happen for < XP win that do not support WTS API
                     * in that case the session is treated as active connected to the physical console
                     * (i.e. fallback to the old behavior that was before introduction of VBoxSt) */
                    Assert(vboxDtIsInputDesktop());
                }

                rc = VBoxAcquireGuestCaps(VMMDEV_GUEST_SUPPORTS_SEAMLESS | VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0, true);
                if (!RT_SUCCESS(rc))
                    LogFlowFunc(("VBoxAcquireGuestCaps failed with rc=%Rrc, ignoring ...\n", rc));

                rc = vboxTraySetupSeamless(); /** @todo r=andy Do we really want to be this critical for the whole application? */
                if (RT_SUCCESS(rc))
                {
                    rc = vboxTrayServiceMain();
                    if (RT_SUCCESS(rc))
                        hlpReportStatus(VBoxGuestFacilityStatus_Terminating);
                    vboxTrayShutdownSeamless();
                }

                /* it should be safe to call vboxDtTerm even if vboxStInit above failed */
                vboxDtTerm();

                /* it should be safe to call vboxStTerm even if vboxStInit above failed */
                vboxStTerm();

                VBoxCapsTerm();

                vboxTrayDestroyToolWindow();
            }
            if (RT_SUCCESS(rc))
                hlpReportStatus(VBoxGuestFacilityStatus_Terminated);
            else
            {
                LogRel(("Error while starting, rc=%Rrc\n", rc));
                hlpReportStatus(VBoxGuestFacilityStatus_Failed);
            }

            LogRel(("Ended\n"));

            vboxTrayLogDestroy();
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

    /* Release instance mutex. */
    if (hMutexAppRunning != NULL)
    {
        CloseHandle(hMutexAppRunning);
        hMutexAppRunning = NULL;
    }

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
                    if (RT_SUCCESS(VBoxCheckHostVersion()))
                    {
                        /* After successful run we don't need to check again. */
                        KillTimer(g_hwndToolWindow, TIMERID_VBOXTRAY_CHECK_HOSTVERSION);
                    }
                    return 0;

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

