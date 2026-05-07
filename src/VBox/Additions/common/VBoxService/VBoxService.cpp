/* $Id: VBoxService.cpp 114106 2026-05-07 12:04:49Z knut.osmundsen@oracle.com $ */
/** @file
 * VBoxService - Guest Additions Service Skeleton.
 */

/*
 * Copyright (C) 2007-2025 Oracle and/or its affiliates.
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


/** @page pg_vgsvc VBoxService
 *
 * VBoxService is a root daemon for implementing guest additions features.
 *
 * It is structured as one binary that contains many sub-services.  The reason
 * for this is partially historical and partially practical.  The practical
 * reason is that the VBoxService binary is typically statically linked, at
 * least with IPRT and the guest library, so we save quite a lot of space having
 * on single binary instead individual binaries for each sub-service and their
 * helpers (currently up to 9 subservices and 8 helpers).  The historical is
 * simply that it started its life on OS/2 dreaming of conquring Windows next,
 * so it kind of felt natural to have it all in one binary.
 *
 * Even if it's structured as a single binary, it is possible, by using command
 * line options, to start each subservice as an individual process.
 *
 * Subservices:
 *  - @subpage pg_vgsvc_timesync    "Time Synchronization"
 *  - @subpage pg_vgsvc_vminfo      "VM Information"
 *  - @subpage pg_vgsvc_vmstats     "VM Statistics"
 *  - @subpage pg_vgsvc_gstctrl     "Guest Control"
 *  - @subpage pg_vgsvc_pagesharing "Page Sharing"
 *  - @subpage pg_vgsvc_memballoon  "Memory Balooning"
 *  - @subpage pg_vgsvc_cpuhotplug  "CPU Hot-Plugging"
 *  - @subpage pg_vgsvc_automount   "Shared Folder Automounting"
 *  - @subpage pg_vgsvc_clipboard   "Clipboard (OS/2 only)"
 *
 * Now, since the service predates a lot of stuff, including RTGetOpt, we're
 * currently doing our own version of argument parsing here, which is kind of
 * stupid.  That will hopefully be cleaned up eventually.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
/** @todo LOG_GROUP*/
#ifndef _MSC_VER
# include <unistd.h>
#endif
#ifndef RT_OS_WINDOWS
# include <errno.h>
# include <signal.h>
# ifdef RT_OS_OS2
#  define pthread_sigmask sigprocmask
# endif
#endif
#ifdef RT_OS_FREEBSD
# include <pthread.h>
#endif

#include <package-generated.h>
#include "product-generated.h"

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#ifdef DEBUG
# include <iprt/memtracker.h>
#endif
#include <iprt/env.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/thread.h>

#include <VBox/err.h>
#include <VBox/log.h>

#include "VBoxServiceInternal.h"
#ifdef VBOX_WITH_VBOXSERVICE_CONTROL
# include "VBoxServiceControl.h"
#endif
#ifdef VBOX_WITH_VBOXSERVICE_TOOLBOX
# include "VBoxServiceToolBox.h"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The program name (derived from argv[0]). */
char                *g_pszProgName =  (char *)"";
/** Whether to daemonize the service (--foreground clear this). */
static bool          g_fDaemonize  = true;
/** The --daemonized option indicator. */
static bool          g_fDaemonized = false;
/** The current verbosity level. */
unsigned             g_cVerbosity = 0;
char                 g_szLogFile[RTPATH_MAX + 128] = "";
char                 g_szPidFile[RTPATH_MAX] = "";
/** Logging parameters. */
/** @todo Make this configurable later. */
static PRTLOGGER     g_pLoggerRelease = NULL;
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */
/** Critical section for (debug) logging. */
#ifdef DEBUG
 RTCRITSECT          g_csLog;
#endif
/** The default service interval in seconds (the -i | --interval) option). */
uint32_t             g_cSecDefaultInterval = 0;
#ifdef RT_OS_WINDOWS
/** Signal shutdown to the Windows service thread. */
static bool volatile g_fWindowsServiceShutdown;
/** Event the Windows service thread waits for shutdown. */
static RTSEMEVENT    g_hEvtWindowsService;
#endif

/**
 * The details of the services that has been compiled in.
 */
static struct
{
    /** Pointer to the service descriptor. */
    PCVBOXSERVICE   pDesc;
    /** The worker thread. NIL_RTTHREAD if it's the main thread. */
    RTTHREAD        Thread;
    /** Whether Pre-init was called. */
    bool            fPreInited;
    /** Shutdown indicator. */
    bool volatile   fShutdown;
    /** Indicator set by the service thread exiting. */
    bool volatile   fStopped;
    /** Whether the service was started or not. */
    bool            fStarted;
    /** Whether the service is enabled or not. */
    bool            fEnabled;
} g_aServices[] =
{
#ifdef VBOX_WITH_VBOXSERVICE_CONTROL
    { &g_Control,       NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_VBOXSERVICE_TIMESYNC
    { &g_TimeSync,      NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_VBOXSERVICE_CLIPBOARD
    { &g_Clipboard,     NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_VBOXSERVICE_VMINFO
    { &g_VMInfo,        NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_VBOXSERVICE_CPUHOTPLUG
    { &g_CpuHotPlug,    NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_VBOXSERVICE_MANAGEMENT
# ifdef VBOX_WITH_MEMBALLOON
    { &g_MemBalloon,    NIL_RTTHREAD, false, false, false, false, true },
# endif
    { &g_VMStatistics,  NIL_RTTHREAD, false, false, false, false, true },
#endif
#if defined(VBOX_WITH_VBOXSERVICE_PAGE_SHARING)
    { &g_PageSharing,   NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
    { &g_AutoMount,     NIL_RTTHREAD, false, false, false, false, true },
#endif
#ifdef RT_OS_WINDOWS
    { &g_DisplayConfig, NIL_RTTHREAD, false, false, false, false, true },
#endif
};


/*
 * Default call-backs for services which do not need special behaviour.
 */

/**
 * @interface_method_impl{VBOXSERVICE,pfnPreInit, Default Implementation}
 */
DECLCALLBACK(int) VGSvcDefaultPreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnOption, Default Implementation}
 */
DECLCALLBACK(RTEXITCODE) VGSvcDefaultOption(int iShort, PCRTGETOPTUNION pValueUnion, bool fCmdLine)
{
    RT_NOREF(iShort, pValueUnion);
    AssertMsgFailed(("iShort=%u\n", iShort));
    return VGSvcSyntax(fCmdLine, "Unexpected option iShort=%u (%#x)!\n", iShort, iShort);
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnInit, Default Implementation}
 */
DECLCALLBACK(int) VGSvcDefaultInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnTerm, Default Implementation}
 */
DECLCALLBACK(void) VGSvcDefaultTerm(void)
{
    return;
}


/**
 * @callback_method_impl{FNRTLOGPHASE, Release logger callback}
 */
static DECLCALLBACK(void) vgsvcLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
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
                   "VBoxService %s r%s (verbosity: %u) %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity, VBOX_BUILD_TARGET,
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
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
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
            /* nothing */
            break;
    }
}


/**
 * Creates the default release logger outputting to the specified file.
 *
 * Pass NULL to disabled logging.
 *
 * @return  IPRT status code.
 * @param   pszLogFile      Filename for log output.  NULL disables logging
 *                          (r=bird: No, it doesn't!).
 */
int VGSvcLogCreate(const char *pszLogFile)
{
    /* Create release logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    int rc = RTLogCreateEx(&g_pLoggerRelease, "VBOXSERVICE_RELEASE_LOG", fFlags, "all",
                           RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX /*cMaxEntriesPerGroup*/,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT | RTLOGDEST_USER,
                           vgsvcLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           NULL /*pErrInfo*/, "%s", pszLogFile ? pszLogFile : "");
    if (RT_SUCCESS(rc))
    {
        /* register this logger as the release logger */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);

        /* Explicitly flush the log in case of VBOXSERVICE_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);
    }

    return rc;
}


/**
 * Logs a verbose message.
 *
 * @param   pszFormat   The message text.
 * @param   va          Format arguments.
 */
void VGSvcLogV(const char *pszFormat, va_list va)
{
#ifdef DEBUG
    int rc = RTCritSectEnter(&g_csLog);
    if (RT_SUCCESS(rc))
    {
#endif
        char *psz = NULL;
        RTStrAPrintfV(&psz, pszFormat, va);

        AssertPtr(psz);
        LogRel(("%s", psz));

        RTStrFree(psz);
#ifdef DEBUG
        RTCritSectLeave(&g_csLog);
    }
#endif
}


/**
 * Destroys the currently active logging instance.
 */
void VGSvcLogDestroy(void)
{
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}


/**
 * Displays the program usage message.
 *
 * @returns 1.
 */
static int vgsvcUsage(void)
{
    RTPrintf("Usage: %s [-f|--foreground] [-v|--verbose] [-l|--logfile <file>]\n"
             "           [-p|--pidfile <file>] [-i|--interval <seconds>]\n"
             "           [--disable-<service>] [--enable-<service>]\n"
             "           [--only-<service>] [-h|-?|--help]\n", g_pszProgName);
#ifdef RT_OS_WINDOWS
    RTPrintf("           [-r|--register] [-u|--unregister]\n");
#endif
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].pDesc->pszUsage)
            RTPrintf("%s\n", g_aServices[j].pDesc->pszUsage);
    RTPrintf("\n"
             "Options:\n"
             "    -i | --interval         The default interval.\n"
             "    -f | --foreground       Don't daemonize the program. For debugging.\n"
             "    -l | --logfile <file>   Enables logging to a file.\n"
             "    -p | --pidfile <file>   Write the process ID to a file.\n"
             "    -v | --verbose          Increment the verbosity level. For debugging.\n"
             "    -V | --version          Show version information.\n"
             "    -h | -? | --help        Show this message and exit with status 1.\n"
             );
#ifdef RT_OS_WINDOWS
    RTPrintf("    -r | --register         Installs the service.\n"
             "    -u | --unregister       Uninstall service.\n");
#endif

    RTPrintf("\n"
             "Service-specific options:\n");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        RTPrintf("    --enable-%-14s Enables the %s service. (default)\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        RTPrintf("    --disable-%-13s Disables the %s service.\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        RTPrintf("    --only-%-16s Only enables the %s service.\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        if (g_aServices[j].pDesc->pszOptions)
            RTPrintf("%s", g_aServices[j].pDesc->pszOptions);
    }
    RTPrintf("\n"
             " Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n");

    return 1;
}


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
RTEXITCODE VGSvcError(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);

    AssertPtr(psz);
    LogRel(("Error: %s", psz));

    RTStrFree(psz);

    return RTEXITCODE_FAILURE;
}


/**
 * Display a syntax error message.
 *
 * @returns RTEXITCODE_SYNTAX
 * @param   fCmdLine    Set if the source is the command line,
 *                      clear if its the host (guest properties).
 * @param   pszFormat   The message format string.
 * @param   ...         Arguments for the message.
 */
RTEXITCODE VGSvcSyntax(bool fCmdLine, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (fCmdLine)
        RTMsgSyntaxV(pszFormat, va);
    else
        RTMsgWarning("(host option) %N", pszFormat, &va);
    va_end(va);
    return RTEXITCODE_SYNTAX;
}


/**
 * Displays a verbose message based on the currently
 * set global verbosity level.
 *
 * @param   iLevel      Minimum log level required to display this message.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
void VGSvcVerbose(unsigned iLevel, const char *pszFormat, ...)
{
    if (iLevel <= g_cVerbosity)
    {
        va_list va;
        va_start(va, pszFormat);
        VGSvcLogV(pszFormat, va);
        va_end(va);
    }
}


/**
 * Reports the current VBoxService status to the host.
 *
 * This makes sure that the Failed state is sticky.
 *
 * @return  IPRT status code.
 * @param   enmStatus               Status to report to the host.
 */
int VGSvcReportStatus(VBoxGuestFacilityStatus enmStatus)
{
    /*
     * VBoxGuestFacilityStatus_Failed is sticky.
     */
    static VBoxGuestFacilityStatus s_enmLastStatus = VBoxGuestFacilityStatus_Inactive;
    VGSvcVerbose(4, "Setting VBoxService status to %u\n", enmStatus);
    if (s_enmLastStatus != VBoxGuestFacilityStatus_Failed)
    {
        int rc = VbglR3ReportAdditionsStatus(VBoxGuestFacilityType_VBoxService, enmStatus, 0 /* Flags */);
        if (RT_FAILURE(rc))
        {
            VGSvcError("Could not report VBoxService status (%u), rc=%Rrc\n", enmStatus, rc);
            return rc;
        }
        s_enmLastStatus = enmStatus;
    }
    return VINF_SUCCESS;
}


/**
 * The service thread.
 *
 * @returns Whatever the worker function returns.
 * @param   ThreadSelf      My thread handle.
 * @param   pvUser          The service index.
 */
static DECLCALLBACK(int) vgsvcThread(RTTHREAD ThreadSelf, void *pvUser)
{
    const unsigned i = (uintptr_t)pvUser;

#ifndef RT_OS_WINDOWS
    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);
#endif

    int rc = g_aServices[i].pDesc->pfnWorker(&g_aServices[i].fShutdown);
    ASMAtomicXchgBool(&g_aServices[i].fShutdown, true);
    RTThreadUserSignal(ThreadSelf);
    return rc;
}


/**
 * Lazily calls the pfnPreInit method on each service.
 *
 * @returns VBox status code, error message displayed.
 */
static RTEXITCODE vgsvcLazyPreInit(void)
{
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (!g_aServices[j].fPreInited && g_aServices[j].fEnabled)
        {
            int rc = g_aServices[j].pDesc->pfnPreInit();
            if (RT_FAILURE(rc))
                return VGSvcError("Service '%s' failed pre-init: %Rrc\n", g_aServices[j].pDesc->pszName, rc);
            g_aServices[j].fPreInited = true;
        }
    return RTEXITCODE_SUCCESS;
}


/**
 * Count the number of enabled services.
 */
static unsigned vgsvcCountEnabledServices(void)
{
    unsigned cEnabled = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aServices); i++)
        cEnabled += g_aServices[i].fEnabled;
   return cEnabled;
}


#ifdef RT_OS_WINDOWS
/**
 * Console control event callback.
 *
 * @returns TRUE if handled, FALSE if not.
 * @param   dwCtrlType      The control event type.
 *
 * @remarks This is generally called on a new thread, so we're racing every
 *          other thread in the process.
 */
static BOOL WINAPI vgsvcWinConsoleControlHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    int rc = VINF_SUCCESS;
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        /* User pressed CTRL+C or CTRL+BREAK or an external event was sent
         * via GenerateConsoleCtrlEvent(). */
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
            VGSvcVerbose(2, "ControlHandler: Received break/close event\n");
            rc = VGSvcStopServices();
            fEventHandled = TRUE;
            break;
        default:
            break;
        /** @todo Add other events here. */
    }

    if (RT_FAILURE(rc))
        VGSvcError("ControlHandler: Event %ld handled with error rc=%Rrc\n",
                         dwCtrlType, rc);
    return fEventHandled;
}
#endif /* RT_OS_WINDOWS */


/**
 * Starts the service.
 *
 * @returns VBox status code, errors are fully bitched.
 *
 * @remarks Also called from VBoxService-win.cpp, thus not static.
 */
int VGSvcStartServices(void)
{
    int rc;

    VGSvcReportStatus(VBoxGuestFacilityStatus_Init);

    /*
     * Initialize the services.
     */
    VGSvcVerbose(2, "Initializing services ...\n");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fEnabled)
        {
            rc = g_aServices[j].pDesc->pfnInit();
            if (RT_FAILURE(rc))
            {
                if (rc != VERR_SERVICE_DISABLED)
                {
                    VGSvcError("Service '%s' failed to initialize: %Rrc\n", g_aServices[j].pDesc->pszName, rc);
                    VGSvcReportStatus(VBoxGuestFacilityStatus_Failed);
                    return rc;
                }

                g_aServices[j].fEnabled = false;
                VGSvcVerbose(0, "Service '%s' was disabled because of missing functionality\n", g_aServices[j].pDesc->pszName);
            }
        }

    /*
     * Start the service(s).
     */
    VGSvcVerbose(2, "Starting services ...\n");
    rc = VINF_SUCCESS;
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        if (!g_aServices[j].fEnabled)
            continue;

        VGSvcVerbose(2, "Starting service     '%s' ...\n", g_aServices[j].pDesc->pszName);
        rc = RTThreadCreate(&g_aServices[j].Thread, vgsvcThread, (void *)(uintptr_t)j, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, g_aServices[j].pDesc->pszName);
        if (RT_FAILURE(rc))
        {
            VGSvcError("RTThreadCreate failed, rc=%Rrc\n", rc);
            break;
        }
        g_aServices[j].fStarted = true;

        /* Wait for the thread to initialize. */
        /** @todo There is a race between waiting and checking
         * the fShutdown flag of a thread here and processing
         * the thread's actual worker loop. If the thread decides
         * to exit the loop before we skipped the fShutdown check
         * below the service will fail to start! */
        /** @todo This presumably means either a one-shot service or that
         * something has gone wrong.  In the second case treating it as failure
         * to start is probably right, so we need a way to signal the first
         * rather than leaving the idle thread hanging around.  A flag in the
         * service description? */
        RTThreadUserWait(g_aServices[j].Thread, 60 * 1000);
        if (g_aServices[j].fShutdown)
        {
            VGSvcError("Service '%s' failed to start!\n", g_aServices[j].pDesc->pszName);
            rc = VERR_GENERAL_FAILURE;
        }
    }

    if (RT_SUCCESS(rc))
        VGSvcVerbose(1, "All services started.\n");
    else
    {
        VGSvcError("An error occcurred while the services!\n");
        VGSvcReportStatus(VBoxGuestFacilityStatus_Failed);
    }
    return rc;
}


/**
 * Stops and terminates the services.
 *
 * This should be called even when VBoxServiceStartServices fails so it can
 * clean up anything that we succeeded in starting.
 *
 * @remarks Also called from VBoxService-win.cpp, thus not static.
 */
int VGSvcStopServices(void)
{
    VGSvcReportStatus(VBoxGuestFacilityStatus_Terminating);

    /*
     * Signal all the services.
     */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        ASMAtomicWriteBool(&g_aServices[j].fShutdown, true);

    /*
     * Do the pfnStop callback on all running services.
     */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fStarted)
        {
            VGSvcVerbose(3, "Calling stop function for service '%s' ...\n", g_aServices[j].pDesc->pszName);
            g_aServices[j].pDesc->pfnStop();
        }

    VGSvcVerbose(3, "All stop functions for services called\n");

    /*
     * Wait for all the service threads to complete.
     */
    int rc = VINF_SUCCESS;
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        if (!g_aServices[j].fEnabled) /* Only stop services which were started before. */
            continue;
        if (g_aServices[j].Thread != NIL_RTTHREAD)
        {
            VGSvcVerbose(2, "Waiting for service '%s' to stop ...\n", g_aServices[j].pDesc->pszName);
            int rc2 = VINF_SUCCESS;
            for (int i = 0; i < 30; i++) /* Wait 30 seconds in total */
            {
                rc2 = RTThreadWait(g_aServices[j].Thread, 1000 /* Wait 1 second */, NULL);
                if (RT_SUCCESS(rc2))
                    break;
#ifdef RT_OS_WINDOWS
                /* Notify SCM that it takes a bit longer ... */
                VGSvcWinSetStopPendingStatus(i + j*32);
#endif
            }
            if (RT_FAILURE(rc2))
            {
                VGSvcError("Service '%s' failed to stop. (%Rrc)\n", g_aServices[j].pDesc->pszName, rc2);
                rc = rc2;
            }
        }
        VGSvcVerbose(3, "Terminating service '%s' (%d) ...\n", g_aServices[j].pDesc->pszName, j);
        g_aServices[j].pDesc->pfnTerm();
    }

#ifdef RT_OS_WINDOWS
    /*
     * Wake up and tell the main() thread that we're shutting down (it's
     * sleeping in VBoxServiceMainWait).
     */
    ASMAtomicWriteBool(&g_fWindowsServiceShutdown, true);
    if (g_hEvtWindowsService != NIL_RTSEMEVENT)
    {
        VGSvcVerbose(3, "Stopping the main thread...\n");
        int rc2 = RTSemEventSignal(g_hEvtWindowsService);
        AssertRC(rc2);
    }
#endif

# ifdef RT_OS_LINUX
    rc = VbglR3DrmClientStop();
    if (RT_FAILURE(rc))
        VGSvcVerbose(0, "VMSVGA DRM resizing client could not be stopped, rc=%Rrc\n", rc);
# endif /* RT_OS_LINUX */

    VGSvcVerbose(2, "Stopping services returning: %Rrc\n", rc);
    VGSvcReportStatus(RT_SUCCESS(rc) ? VBoxGuestFacilityStatus_Paused : VBoxGuestFacilityStatus_Failed);
    return rc;
}


/**
 * Checks the range of a uint32_t option value, assigning it if within range and
 * complaining if not.
 */
RTEXITCODE VGSvcOptUInt32(uint32_t *puValue, PCRTGETOPTUNION pValueUnion, uint32_t uMin, uint32_t uMax, const char *pszUnit,
                          const char *pszValueDesc, const char *pszService, bool fCmdLine)
{
    uint32_t const uValue = pValueUnion->u32;
    if (uValue >= uMin && uValue <= uMax)
        *puValue = uValue;
    else if (pszService)
        return VGSvcSyntax(fCmdLine, "The %s sub-service %s of %RU32 %s%sis out of range [%RU32..%RU32]\n",
                           pszService, pszValueDesc, uValue, pszUnit ? pszUnit : "", pszUnit ? " " : "", uMin, uMax);
    else
        return VGSvcSyntax(fCmdLine, "The (global) %s of %RU32 %s%sis out of range [%RU32..%RU32]\n",
                           pszValueDesc, uValue, pszUnit ? pszUnit : "", pszUnit ? " " : "", uMin, uMax);
    return RTEXITCODE_SUCCESS;
}


/**
 * Processes one service option.
 */
static RTEXITCODE vgsvcProcessServiceOption(int iShort, PCRTGETOPTUNION pValueUnion, bool fCmdLine, size_t idxService)
{
    AssertLogRelReturn(idxService < RT_ELEMENTS(g_aServices), RTEXITCODE_SUCCESS);

    /* Don't bother with options for disabled services. */
    if (g_aServices[idxService].fEnabled)
    {
        /* Make sure it is pre-initialized. */
        if (!g_aServices[idxService].fPreInited)
        {
            int rc = g_aServices[idxService].pDesc->pfnPreInit();
            if (RT_FAILURE(rc))
                return VGSvcError("Service '%s' failed pre-init: %Rrc\n", g_aServices[idxService].pDesc->pszName, rc);
            g_aServices[idxService].fPreInited = true;
        }

        /* Let the service process the option. */
        return g_aServices[idxService].pDesc->pfnOption(iShort, pValueUnion, fCmdLine);
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Processes one global option.
 */
static RTEXITCODE vgsvcProcessGlobalOption(int iShort, PCRTGETOPTUNION pValueUnion, bool fCmdLine)
{
    size_t cch;
    switch (iShort)
    {
        case 'i':
            return VGSvcOptUInt32(&g_cSecDefaultInterval, pValueUnion, 1, (UINT32_MAX / 1000) - 1, "seconds", "interval",
                                  NULL, fCmdLine);

        case 'f':
            if (fCmdLine)
                g_fDaemonize = false;
            break;

        case kVGSvcOptGlobalDaemonzied:
            if (fCmdLine)
                g_fDaemonized = true;
            break;

        case 'v':
            g_cVerbosity++;
            break;

        case 'l':
            cch = strlen(pValueUnion->psz);
            if (cch >= sizeof(g_szLogFile))
                return VGSvcSyntax(fCmdLine, "The log filename is too long: %s\n", pValueUnion->psz);
            memcpy(g_szLogFile, pValueUnion->psz, cch + 1);
            break;

        case 'p':
            if (fCmdLine)
            {
                cch = strlen(pValueUnion->psz);
                if (cch >= sizeof(g_szPidFile))
                    return VGSvcSyntax(fCmdLine, "The pid-file filename is too long: %s\n", pValueUnion->psz);
                memcpy(g_szPidFile, pValueUnion->psz, cch + 1);
            }
            break;

        default:
            AssertMsgFailed(("iShort=%u\n", iShort));
            return RTGetOptPrintError(iShort, pValueUnion);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Processes option from the host (guest properties).
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE. Syntax errors are ignored.
 */
static RTEXITCODE vgsvcProcessHostOpts(PCRTGETOPTDEF paOptions, uint8_t const *paidxOptToService, size_t cOptions)
{
    RTEXITCODE        rcExit = RTEXITCODE_SUCCESS;

    VBGLGSTPROPCLIENT GuestProcClient;
    int rc = VbglGuestPropConnect(&GuestProcClient);
    if (RT_SUCCESS(rc))
    {
        /* Note! We don't need to check that the property is RDONLYGUEST, because
                 the service ensures the guest cannot write properties with the
                 prefix (path) we're using. */
        static char const           s_szNamePrefix[] =   "/VirtualBox/GuestAdd/VBoxService/";
        static const char * const   s_apszPatterns[] = { "/VirtualBox/GuestAdd/VBoxService/--*", };
        PVBGLGUESTPROPENUM          pEnumHandle      = NULL;
        const char                 *pszName;
        const char                 *pszValue;
        for (rc = VbglGuestPropEnum(&GuestProcClient, s_apszPatterns, RT_ELEMENTS(s_apszPatterns),
                                    &pEnumHandle, &pszName, &pszValue, NULL, NULL);
             RT_SUCCESS(rc) && pszName != NULL;
             rc = VbglGuestPropEnumNext(pEnumHandle, &pszName, &pszValue, NULL, NULL))
        {
            /* Skip the prefix and check that it's an option. */
            AssertContinue(strncmp(pszName, s_szNamePrefix, sizeof(s_szNamePrefix) - 1) == 0); /* paranoia */
            pszName += sizeof(s_szNamePrefix) - 1;
            AssertContinue(pszName[0] == '-' && pszName[1] == '-'  && pszName[2] != '\0');

            /* Lookup the option. */
            for (size_t i = 0; i < cOptions; i++)
            {
                if (   paOptions[i].pszLong
                    && strcmp(pszName, paOptions[i].pszLong) == 0)
                {
                    /* Fount it. Process the value, if one is requested. */
                    RTGETOPTUNION ValueUnion;
                    ValueUnion.PairU64.uFirst  = 0;
                    ValueUnion.PairU64.uSecond = 0;
                    if (paOptions[i].fFlags != RTGETOPT_REQ_NOTHING)
                    {
                        rc = RTGetOptStringToValue(pszValue, paOptions[i].fFlags, &ValueUnion);
                        if (RT_FAILURE(rc))
                        {
                            RTMsgWarning("option '%s' is given an incorrect value '%s' via guest properties, ignoring: %Rrc",
                                         pszName, pszValue, rc);
                            break;
                        }
                    }

                    /*
                     * Feed the option to the appropriate handler. Ignore syntax errors.
                     */
                    uint8_t const idxService = paidxOptToService[i];
                    RTEXITCODE    rcExit2;
                    if (idxService != UINT8_MAX)
                        rcExit2 = vgsvcProcessServiceOption(paOptions[i].iShort, &ValueUnion, false /*fCmdLine*/, idxService);
                    else
                        rcExit2 = vgsvcProcessGlobalOption(paOptions[i].iShort, &ValueUnion, false /*fCmdLine*/);
                    if (rcExit2 == RTEXITCODE_FAILURE)
                        rcExit = RTEXITCODE_FAILURE;
                    break;
                }
            }
        }
        VbglGuestPropEnumFree(pEnumHandle);
        VbglGuestPropDisconnect(&GuestProcClient);
    }
    else
        RTMsgWarning("Unable to open guest property service for reading command line options: %Rrc", rc);

    return rcExit;
}


/**
 * Processes the command line options.
 *
 * @returns Exit code.
 * @param   paOptions           The option list.
 * @param   paidxOptToService   Array running parallel to paOptions that maps
 *                              them to a service (g_aServices index) or global
 *                              (UINT8_MAX).
 * @param   cOptions            Number of entries in the paOptions and
 *                              paidxOptToService arrays.
 * @param   argc                Number of arguments in @a argv.
 * @param   argv                The main() argument vector.
 */
static RTEXITCODE vgsvcProcessCmdLineOptions(PCRTGETOPTDEF paOptions, uint8_t const *paidxOptToService,
                                             size_t cOptions, int argc, char **argv)
{
    RTGETOPTSTATE OptState;
    int rc = RTGetOptInit(&OptState, argc, argv, paOptions, cOptions, 1, 0 /*fFlags*/);
    AssertRCReturn(rc, RTMsgErrorExitFailure("RTGetOptInit failed: %Rrc", rc));

    RTEXITCODE    rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&OptState, &ValueUnion)) != 0)
        switch (rc)
        {
            case 'V':
            case 'h':
                AssertFailed(); RT_FALL_THRU();
            case VINF_GETOPT_NOT_OPTION:
            case VERR_GETOPT_UNKNOWN_OPTION:
            case VERR_GETOPT_REQUIRED_ARGUMENT_MISSING:
            case VERR_GETOPT_INVALID_ARGUMENT_FORMAT:
            case VERR_GETOPT_INDEX_MISSING:
                return RTGetOptPrintError(rc, &ValueUnion);

            default:
                if (rc < kVGSvcOptGlobalStartFirstPassOnly)
                {
                    size_t const  iOption    = (size_t)(OptState.pDef - paOptions);
                    AssertBreak(iOption < cOptions);
                    uint8_t const idxService = paidxOptToService[iOption];
                    RTEXITCODE rcExit2;
                    if (idxService != UINT8_MAX)
                        rcExit2 = vgsvcProcessServiceOption(paOptions[iOption].iShort, &ValueUnion, true /*fCmdLine*/, idxService);
                    else
                        rcExit2 = vgsvcProcessGlobalOption(paOptions[iOption].iShort, &ValueUnion, true /*fCmdLine*/);
                    if (rcExit2 != RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                }
                break;
        }

    return rcExit;
}


/**
 * Block the main thread until the service shuts down.
 *
 * @remarks Also called from VBoxService-win.cpp, thus not static.
 */
void VGSvcMainWait(void)
{
    int rc;

    VGSvcReportStatus(VBoxGuestFacilityStatus_Active);

#ifdef RT_OS_WINDOWS
    /*
     * Wait for the semaphore to be signalled.
     */
    VGSvcVerbose(1, "Waiting in main thread\n");
    rc = RTSemEventCreate(&g_hEvtWindowsService);
    AssertRC(rc);
    while (!ASMAtomicReadBool(&g_fWindowsServiceShutdown))
    {
        rc = RTSemEventWait(g_hEvtWindowsService, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    RTSemEventDestroy(g_hEvtWindowsService);
    g_hEvtWindowsService = NIL_RTSEMEVENT;
#else
    /*
     * Wait explicitly for a HUP, INT, QUIT, ABRT or TERM signal, blocking
     * all important signals.
     *
     * The annoying EINTR/ERESTART loop is for the benefit of Solaris where
     * sigwait returns when we receive a SIGCHLD.  Kind of makes sense since
     * the signal has to be delivered...  Anyway, darwin (10.9.5) has a much
     * worse way of dealing with SIGCHLD, apparently it'll just return any
     * of the signals we're waiting on when SIGCHLD becomes pending on this
     * thread. So, we wait for SIGCHLD here and ignores it.
     */
    sigset_t signalMask;
    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGHUP);
    sigaddset(&signalMask, SIGINT);
    sigaddset(&signalMask, SIGQUIT);
    sigaddset(&signalMask, SIGABRT);
    sigaddset(&signalMask, SIGTERM);
    sigaddset(&signalMask, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);

    int iSignal;
    do
    {
        iSignal = -1;
        rc = sigwait(&signalMask, &iSignal);
    }
    while (   rc == EINTR
# ifdef ERESTART
           || rc == ERESTART
# endif
           || iSignal == SIGCHLD
          );

    VGSvcVerbose(3, "VGSvcMainWait: Received signal %d (rc=%d)\n", iSignal, rc);
#endif /* !RT_OS_WINDOWS */
}


/**
 * Report VbglR3InitUser / VbglR3Init failure.
 *
 * @returns RTEXITCODE_FAILURE
 * @param   rcVbgl      The failing status code.
 */
static RTEXITCODE vbglInitFailure(int rcVbgl)
{
    if (rcVbgl == VERR_ACCESS_DENIED)
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Insufficient privileges to start %s! Please start with Administrator/root privileges!\n",
                              g_pszProgName);
    return RTMsgErrorExit(RTEXITCODE_FAILURE, "VbglR3Init failed with rc=%Rrc\n", rcVbgl);
}


int main(int argc, char **argv)
{
    RTEXITCODE rcExit;

    /*
     * Init globals and such.
     *
     * Note! The --utf8-argv stuff is an internal hack to avoid locale configuration
     *       issues preventing us from passing non-ASCII string to child processes.
     */
    uint32_t fIprtFlags = 0;
#ifdef VBOXSERVICE_ARG1_UTF8_ARGV
    if (argc > 1 && strcmp(argv[1], VBOXSERVICE_ARG1_UTF8_ARGV) == 0)
    {
        argv[1] = argv[0];
        argv++;
        argc--;
        fIprtFlags |= RTR3INIT_FLAGS_UTF8_ARGV;
    }
#endif
    int rc = RTR3InitExe(argc, &argv, fIprtFlags);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    g_pszProgName = RTPathFilename(argv[0]);
#ifdef RT_OS_WINDOWS
    VGSvcWinResolveApis();
#endif
#ifdef DEBUG
    rc = RTCritSectInit(&g_csLog);
    AssertRC(rc);
#endif

#ifdef VBOX_WITH_VBOXSERVICE_TOOLBOX
    /*
     * Run toolbox code before all other stuff since these things are simpler
     * shell/file/text utility like programs that just happens to be inside
     * VBoxService and shouldn't be subject to /dev/vboxguest, pid-files and
     * global mutex restrictions.
     */
    if (VGSvcToolboxMain(argc, argv, &rcExit))
        return rcExit;
#endif

    bool fUserSession = false;
#ifdef VBOX_WITH_VBOXSERVICE_CONTROL
    /*
     * Check if we're the specially spawned VBoxService.exe process that
     * handles a guest control session.
     */
    if (   argc >= 2
        && !RTStrICmp(argv[1], VBOXSERVICECTRLSESSION_GETOPT_PREFIX))
        fUserSession = true;
#endif

    /*
     * Connect to the kernel part before daemonizing and *before* we do the sub-service
     * pre-init just in case one of services needs do to some initial stuff with it.
     *
     * However, we do not fail till after we've parsed arguments, because that will
     * prevent useful stuff like --help, --register, --unregister and --version from
     * working when the driver hasn't been installed/loaded yet.
     */
    int const rcVbgl = fUserSession ? VbglR3InitUser() : VbglR3Init();

#ifdef RT_OS_WINDOWS
    /*
     * Check if we're the specially spawned VBoxService.exe process that
     * handles page fusion.  This saves an extra statically linked executable.
     */
    if (   argc == 2
        && !RTStrICmp(argv[1], "pagefusion"))
    {
        if (RT_SUCCESS(rcVbgl))
            return VGSvcPageSharingWorkerChild();
        return vbglInitFailure(rcVbgl);
    }
#endif

#ifdef VBOX_WITH_VBOXSERVICE_CONTROL
    /*
     * Check if we're the specially spawned VBoxService.exe process that
     * handles a guest control session.
     */
    if (fUserSession)
    {
        if (RT_SUCCESS(rcVbgl))
            return VGSvcGstCtrlSessionSpawnInit(argc, argv);
        return vbglInitFailure(rcVbgl);
    }
#endif

    /*
     * Gather the options definitions from all the services.
     */
    static const RTGETOPTDEF s_aGlobalOptions[] =
    {
        { "--foreground",       'f',                              RTGETOPT_REQ_NOTHING },
        { "--verbose",          'v',                              RTGETOPT_REQ_NOTHING },
        { "--interval",         'i',                              RTGETOPT_REQ_UINT32 },
#ifdef RT_OS_WINDOWS
        { "--register",         'r',                              RTGETOPT_REQ_NOTHING },
        { "--unregister",       'u',                              RTGETOPT_REQ_NOTHING },
#endif
        { "--logfile",          'l',                              RTGETOPT_REQ_STRING },
        { "--pidfile",          'p',                              RTGETOPT_REQ_STRING },
        { "--daemonized",       kVGSvcOptGlobalDaemonzied,        RTGETOPT_REQ_NOTHING },
        { "--host-opts-first",  kVGSvcOptGlobalHostOptsFirst,     RTGETOPT_REQ_NOTHING },
        { "--host-opts-last",   kVGSvcOptGlobalHostOptsLast,      RTGETOPT_REQ_NOTHING },
        { "--no-host-opts",     kVGSvcOptGlobalNoHostOpts,        RTGETOPT_REQ_NOTHING },
    };

    static const RTGETOPTDEF s_aTemplateOptions[] =
    {
        { "--only-",            kVGSvcOptGlobalOnlyService,       RTGETOPT_REQ_NOTHING },
        { "--enable-",          kVGSvcOptGlobalEnableService,     RTGETOPT_REQ_NOTHING },
        { "--disable-",         kVGSvcOptGlobalDisableService,    RTGETOPT_REQ_NOTHING },
    };
    AssertCompile(RT_ELEMENTS(g_aServices) < 64);

    /* Calculate the sizes. */
    size_t cbTemplateStrings = 0;
    size_t cchTemplateOptions[RT_ELEMENTS(s_aTemplateOptions)];
    for (size_t i = 0; i < RT_ELEMENTS(s_aTemplateOptions); i++)
    {
        size_t const cchPrefix = strlen(s_aTemplateOptions[i].pszLong);
        cchTemplateOptions[i]  = cchPrefix;
        cbTemplateStrings     += cchPrefix + 1;
    }

    size_t cOptions        = RT_ELEMENTS(s_aGlobalOptions);
    size_t cbOptionStrings = 0;
    for (size_t i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        cOptions        += g_aServices[i].pDesc->cOptions + RT_ELEMENTS(s_aTemplateOptions);
        cbOptionStrings += cbTemplateStrings + strlen(g_aServices[i].pDesc->pszName) * RT_ELEMENTS(s_aTemplateOptions);
    }

    /* Allocate. */
    cbOptionStrings = RT_ALIGN_Z(cbOptionStrings, 16);
    size_t const       cbOptionsAndStrings = sizeof(RTGETOPTDEF) * cOptions + cOptions + cbOptionStrings;
    PRTGETOPTDEF const paOptions           = (PRTGETOPTDEF)RTMemAllocZ(cbOptionsAndStrings);
    RTMEM_MAY_LEAK(paOptions);
    if (!paOptions)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Out of memory!\n");
    uint8_t * const    paidxOptToService   = (uint8_t *)&paOptions[cOptions];
    char              *pszDst              = (char *)&paidxOptToService[cOptions];

    /* Copy options into the combined option array. */
    memcpy(paOptions, s_aGlobalOptions, sizeof(s_aGlobalOptions));
    for (size_t i = 0; i < RT_ELEMENTS(s_aGlobalOptions); i++)
        paidxOptToService[i] = UINT8_MAX;
    size_t iDst = RT_ELEMENTS(s_aGlobalOptions);

    for (size_t i = 0; i < RT_ELEMENTS(g_aServices); i++)
        if (g_aServices[i].pDesc->cOptions)
        {
            memcpy(&paOptions[iDst], g_aServices[i].pDesc->paOptions, sizeof(paOptions[0]) * g_aServices[i].pDesc->cOptions);
            for (size_t iOpt = 0; iOpt < g_aServices[i].pDesc->cOptions; iOpt++)
                paidxOptToService[iDst++] = (uint8_t)i;
        }

    /* Add the generic --only-<service>, --disable-<service> and --enable-<service>
       options.  These cannot be set via guest properties from the host. */
    size_t const cBaseOptions = cOptions;
    for (size_t i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        const char  *pszName = g_aServices[i].pDesc->pszName;
        size_t const cbName  = strlen(pszName) + 1;
        for (size_t iTmpl = 0; iTmpl < RT_ELEMENTS(s_aTemplateOptions); iTmpl++)
        {
            paOptions[iDst].pszLong = pszDst;
            size_t const cchPrefix = cchTemplateOptions[iTmpl];
            memcpy(pszDst, s_aTemplateOptions[iTmpl].pszLong, cchPrefix);
            memcpy(&pszDst[cchPrefix], pszName, cbName);
            pszDst += cchPrefix + cbName;

            paOptions[iDst].iShort = s_aTemplateOptions[iTmpl].iShort + (int)i;
            paOptions[iDst].fFlags = s_aTemplateOptions[iTmpl].fFlags;

            paidxOptToService[iDst++] = (uint8_t)i;
        }
    }

    Assert(iDst == cOptions);
    Assert((uintptr_t)pszDst - (uintptr_t)paOptions <= cbOptionsAndStrings);

    /*
     * Syntax check the arguments and check if we've got any --help or
     * --version options that will make us quit without doing anything.
     * We can skip loading options from the host via guest properties
     * if this is the case.
     *
     * Also, check for --only|enable|disable-<service>, --host-opts-first/last
     * , --no-host-opts and the windows install/remove service options.
     */
    RTGETOPTSTATE OptState;
    rc = RTGetOptInit(&OptState, argc, argv, paOptions, cOptions, 1, 0 /*fFlags*/);
    AssertRCReturn(rc, RTMsgErrorExitFailure("RTGetOptInit failed: %Rrc", rc));

    int           iHostOptsOrder = kVGSvcOptGlobalHostOptsFirst;
    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&OptState, &ValueUnion)) != 0)
        switch (rc)
        {
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;

            case 'h':
                return vgsvcUsage();

            case kVGSvcOptGlobalHostOptsFirst:
            case kVGSvcOptGlobalHostOptsLast:
            case kVGSvcOptGlobalNoHostOpts:
                iHostOptsOrder = rc;
                break;

            default:
            {
                unsigned iService;
                if ((iService = (unsigned)(rc - kVGSvcOptGlobalEnableService))       < RT_ELEMENTS(g_aServices))
                    g_aServices[iService].fEnabled = true;
                else if ((iService = (unsigned)(rc - kVGSvcOptGlobalDisableService)) < RT_ELEMENTS(g_aServices))
                    g_aServices[iService].fEnabled = false;
                else if ((iService = (unsigned)(rc - kVGSvcOptGlobalOnlyService))    < RT_ELEMENTS(g_aServices))
                {
                    for (size_t i = 0; i < RT_ELEMENTS(g_aServices); i++)
                        g_aServices[i].fEnabled = false;
                    g_aServices[iService].fEnabled = true;
                }
                break;
            }

#ifdef RT_OS_WINDOWS
            case 'r':
                return VGSvcWinInstall();

            case 'u':
                return VGSvcWinUninstall();
#endif

            case VINF_GETOPT_NOT_OPTION:
            case VERR_GETOPT_UNKNOWN_OPTION:
            case VERR_GETOPT_REQUIRED_ARGUMENT_MISSING:
            case VERR_GETOPT_INVALID_ARGUMENT_FORMAT:
            case VERR_GETOPT_INDEX_MISSING:
                return RTGetOptPrintError(rc, &ValueUnion);
        }

    /* Now we can report the VBGL failure. */
    if (RT_FAILURE(rcVbgl))
        return vbglInitFailure(rcVbgl);

    /* Check that at least one service is enabled. */
    if (vgsvcCountEnabledServices() == 0)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "At least one service must be enabled\n");

    /*
     * Do the parameter parsing.
     */
    /** @todo Allow the host to enable/disable services iff the
     *        enabled-service-count is higher than 1? */
    rcExit = RTEXITCODE_SUCCESS;
    if (iHostOptsOrder == kVGSvcOptGlobalHostOptsFirst)
        rcExit = vgsvcProcessHostOpts(paOptions, paidxOptToService, cBaseOptions);

    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = vgsvcProcessCmdLineOptions(paOptions, paidxOptToService, cOptions, argc, argv);

    if (iHostOptsOrder == kVGSvcOptGlobalHostOptsLast && rcExit == RTEXITCODE_SUCCESS)
        rcExit = vgsvcProcessHostOpts(paOptions, paidxOptToService, cBaseOptions);

    RTMemFree(paOptions);

    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    rc = VGSvcLogCreate(g_szLogFile[0] ? g_szLogFile : NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create release log '%s', rc=%Rrc\n",
                              g_szLogFile[0] ? g_szLogFile : "<None>", rc);

    /* Call pre-init if we didn't do it already. */
    rcExit = vgsvcLazyPreInit();
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

#ifdef VBOX_WITH_VBOXSERVICE_DRMRESIZE
# ifdef RT_OS_LINUX
    rc = VbglR3DrmClientStart();
    if (RT_FAILURE(rc))
        VGSvcVerbose(0, "VMSVGA DRM resizing client not started, rc=%Rrc\n", rc);
# endif /* RT_OS_LINUX */
#endif /* VBOX_WITH_VBOXSERVICE_DRMRESIZE */

#ifdef RT_OS_WINDOWS
    /*
     * Make sure only one instance of VBoxService runs at a time.  Create a
     * global mutex for that.
     *
     * Note! The \\Global\ namespace was introduced with Win2K, thus the
     *       version check.
     * Note! If the mutex exists CreateMutex will open it and set last error to
     *       ERROR_ALREADY_EXISTS.
     */
    SetLastError(NO_ERROR);
    HANDLE hMutexAppRunning;
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(5,0,0)) /* Windows 2000 */
        hMutexAppRunning = CreateMutexW(NULL, FALSE, L"Global\\" RT_CONCAT(L,VBOXSERVICE_NAME));
    else
        hMutexAppRunning = CreateMutexW(NULL, FALSE, RT_CONCAT(L,VBOXSERVICE_NAME));
    if (hMutexAppRunning == NULL)
    {
        DWORD dwErr = GetLastError();
        if (   dwErr == ERROR_ALREADY_EXISTS
            || dwErr == ERROR_ACCESS_DENIED)
        {
            VGSvcError("%s is already running! Terminating.\n", g_pszProgName);
            return RTEXITCODE_FAILURE;
        }

        VGSvcError("CreateMutex failed with last error %u! Terminating.\n", GetLastError());
        return RTEXITCODE_FAILURE;
    }

#else  /* !RT_OS_WINDOWS */
    /* On other OSes we have PID file support provided by the actual service definitions / service wrapper scripts,
     * like vboxadd-service.sh on Linux or vboxservice.xml on Solaris. */
#endif /* !RT_OS_WINDOWS */

    VGSvcVerbose(0, "%s r%s started. Verbose level = %d\n", RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity);

    /*
     * Daemonize if requested.
     */
    if (g_fDaemonize && !g_fDaemonized)
    {
#ifdef RT_OS_WINDOWS
        VGSvcVerbose(2, "Starting service dispatcher ...\n");
        rcExit = VGSvcWinEnterCtrlDispatcher();
#else
        VGSvcVerbose(1, "Daemonizing...\n");
        rc = VbglR3Daemonize(false /* fNoChDir */, false /* fNoClose */,
                             false /* fRespawn */, NULL /* pcRespawn */);
        if (RT_FAILURE(rc))
            return VGSvcError("Daemon failed: %Rrc\n", rc);
        /* in-child */
#endif
    }
#ifdef RT_OS_WINDOWS
    else
#endif
    {
        /*
         * Windows: We're running the service as a console application now. Start the
         *          services, enter the main thread's run loop and stop them again
         *          when it returns.
         *
         * POSIX:   This is used for both daemons and console runs. Start all services
         *          and return immediately.
         */
#ifdef RT_OS_WINDOWS
        /* Install console control handler. */
        if (!SetConsoleCtrlHandler(vgsvcWinConsoleControlHandler, TRUE /* Add handler */))
        {
            VGSvcError("Unable to add console control handler, error=%ld\n", GetLastError());
            /* Just skip this error, not critical. */
        }
#endif /* RT_OS_WINDOWS */
        rc = VGSvcStartServices();
        RTFILE hPidFile = NIL_RTFILE;
        if (RT_SUCCESS(rc))
            if (g_szPidFile[0])
                rc = VbglR3PidFile(g_szPidFile, &hPidFile);
        rcExit = RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
        if (RT_SUCCESS(rc))
            VGSvcMainWait();
        if (g_szPidFile[0] && hPidFile != NIL_RTFILE)
            VbglR3ClosePidFile(g_szPidFile, hPidFile);
#ifdef RT_OS_WINDOWS
        /* Uninstall console control handler. */
        if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)NULL, FALSE /* Remove handler */))
        {
            VGSvcError("Unable to remove console control handler, error=%ld\n", GetLastError());
            /* Just skip this error, not critical. */
        }
#else /* !RT_OS_WINDOWS */
        /* On Windows - since we're running as a console application - we already stopped all services
         * through the console control handler. So only do the stopping of services here on other platforms
         * where the break/shutdown/whatever signal was just received. */
        VGSvcStopServices();
#endif /* RT_OS_WINDOWS */
    }
    VGSvcReportStatus(VBoxGuestFacilityStatus_Terminated);

#ifdef RT_OS_WINDOWS
    /*
     * Cleanup mutex.
     */
    CloseHandle(hMutexAppRunning);
#endif

    VGSvcVerbose(0, "Ended.\n");

#ifdef DEBUG
    RTCritSectDelete(&g_csLog);
    //RTMemTrackerDumpAllToStdOut();
#endif

    VGSvcLogDestroy();

    return rcExit;
}
