/* $Id: VBoxService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxService - Guest Additions Service Skeleton.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "VBoxServiceUtils.h"
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
/** The default service interval (the -i | --interval) option). */
uint32_t             g_DefaultInterval = 0;
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
DECLCALLBACK(int) VGSvcDefaultOption(const char **ppszShort, int argc,
                                           char **argv, int *pi)
{
    NOREF(ppszShort);
    NOREF(argc);
    NOREF(argv);
    NOREF(pi);

    return -1;
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
 * Gets a 32-bit value argument.
 * @todo Get rid of this and VGSvcArgString() as soon as we have RTOpt handling.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   argc    The argument count.
 * @param   argv    The argument vector
 * @param   psz     Where in *pi to start looking for the value argument.
 * @param   pi      Where to find and perhaps update the argument index.
 * @param   pu32    Where to store the 32-bit value.
 * @param   u32Min  The minimum value.
 * @param   u32Max  The maximum value.
 */
int VGSvcArgUInt32(int argc, char **argv, const char *psz, int *pi, uint32_t *pu32, uint32_t u32Min, uint32_t u32Max)
{
    if (*psz == ':' || *psz == '=')
        psz++;
    if (!*psz)
    {
        if (*pi + 1 >= argc)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing value for the '%s' argument\n", argv[*pi]);
        psz = argv[++*pi];
    }

    char *pszNext;
    int rc = RTStrToUInt32Ex(psz, &pszNext, 0, pu32);
    if (RT_FAILURE(rc) || *pszNext)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Failed to convert interval '%s' to a number\n", psz);
    if (*pu32 < u32Min || *pu32 > u32Max)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The timesync interval of %RU32 seconds is out of range [%RU32..%RU32]\n",
                              *pu32, u32Min, u32Max);
    return 0;
}


/** @todo Get rid of this and VGSvcArgUInt32() as soon as we have RTOpt handling. */
static int vgsvcArgString(int argc, char **argv, const char *psz, int *pi, char *pszBuf, size_t cbBuf)
{
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);

    if (*psz == ':' || *psz == '=')
        psz++;
    if (!*psz)
    {
        if (*pi + 1 >= argc)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing string for the '%s' argument\n", argv[*pi]);
        psz = argv[++*pi];
    }

    if (!RTStrPrintf(pszBuf, cbBuf, "%s", psz))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "String for '%s' argument too big\n", argv[*pi]);
    return 0;
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
        if (!g_aServices[j].fPreInited)
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

    VGSvcVerbose(2, "Stopping services returning: %Rrc\n", rc);
    VGSvcReportStatus(RT_SUCCESS(rc) ? VBoxGuestFacilityStatus_Paused : VBoxGuestFacilityStatus_Failed);
    return rc;
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
     * Parse the arguments.
     *
     * Note! This code predates RTGetOpt, thus the manual parsing.
     */
    bool fDaemonize = true;
    bool fDaemonized = false;
    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (*psz != '-')
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown argument '%s'\n", psz);
        psz++;

        /* translate long argument to short */
        if (*psz == '-')
        {
            psz++;
            size_t cch = strlen(psz);
#define MATCHES(strconst)       (   cch == sizeof(strconst) - 1 \
                                 && !memcmp(psz, strconst, sizeof(strconst) - 1) )
            if (MATCHES("foreground"))
                psz = "f";
            else if (MATCHES("verbose"))
                psz = "v";
            else if (MATCHES("version"))
                psz = "V";
            else if (MATCHES("help"))
                psz = "h";
            else if (MATCHES("interval"))
                psz = "i";
#ifdef RT_OS_WINDOWS
            else if (MATCHES("register"))
                psz = "r";
            else if (MATCHES("unregister"))
                psz = "u";
#endif
            else if (MATCHES("logfile"))
                psz = "l";
            else if (MATCHES("pidfile"))
                psz = "p";
            else if (MATCHES("daemonized"))
            {
                fDaemonized = true;
                continue;
            }
            else
            {
                bool fFound = false;

                if (cch > sizeof("enable-") && !memcmp(psz, RT_STR_TUPLE("enable-")))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("enable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = true;

                if (cch > sizeof("disable-") && !memcmp(psz, RT_STR_TUPLE("disable-")))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("disable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = false;

                if (cch > sizeof("only-") && !memcmp(psz, RT_STR_TUPLE("only-")))
                    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
                    {
                        g_aServices[j].fEnabled = !RTStrICmp(psz + sizeof("only-") - 1, g_aServices[j].pDesc->pszName);
                        if (g_aServices[j].fEnabled)
                            fFound = true;
                    }

                if (!fFound)
                {
                    rcExit = vgsvcLazyPreInit();
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                    {
                        rc = g_aServices[j].pDesc->pfnOption(NULL, argc, argv, &i);
                        fFound = rc == VINF_SUCCESS;
                        if (fFound)
                            break;
                        if (rc != -1)
                            return rc;
                    }
                }
                if (!fFound)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown option '%s'\n", argv[i]);
                continue;
            }
#undef MATCHES
        }

        /* handle the string of short options. */
        do
        {
            switch (*psz)
            {
                case 'i':
                    rc = VGSvcArgUInt32(argc, argv, psz + 1, &i, &g_DefaultInterval, 1, (UINT32_MAX / 1000) - 1);
                    if (rc)
                        return rc;
                    psz = NULL;
                    break;

                case 'f':
                    fDaemonize = false;
                    break;

                case 'v':
                    g_cVerbosity++;
                    break;

                case 'V':
                    RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                    return RTEXITCODE_SUCCESS;

                case 'h':
                case '?':
                    return vgsvcUsage();

#ifdef RT_OS_WINDOWS
                case 'r':
                    return VGSvcWinInstall();

                case 'u':
                    return VGSvcWinUninstall();
#endif

                case 'l':
                {
                    rc = vgsvcArgString(argc, argv, psz + 1, &i, g_szLogFile, sizeof(g_szLogFile));
                    if (rc)
                        return rc;
                    psz = NULL;
                    break;
                }

                case 'p':
                {
                    rc = vgsvcArgString(argc, argv, psz + 1, &i, g_szPidFile, sizeof(g_szPidFile));
                    if (rc)
                        return rc;
                    psz = NULL;
                    break;
                }

                default:
                {
                    rcExit = vgsvcLazyPreInit();
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;

                    bool fFound = false;
                    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
                    {
                        rc = g_aServices[j].pDesc->pfnOption(&psz, argc, argv, &i);
                        fFound = rc == VINF_SUCCESS;
                        if (fFound)
                            break;
                        if (rc != -1)
                            return rc;
                    }
                    if (!fFound)
                        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown option '%c' (%s)\n", *psz, argv[i]);
                    break;
                }
            }
        } while (psz && *++psz);
    }

    /* Now we can report the VBGL failure. */
    if (RT_FAILURE(rcVbgl))
        return vbglInitFailure(rcVbgl);

    /* Check that at least one service is enabled. */
    if (vgsvcCountEnabledServices() == 0)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "At least one service must be enabled\n");

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
    OSVERSIONINFOEX OSInfoEx;
    RT_ZERO(OSInfoEx);
    OSInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);


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
    if (fDaemonize && !fDaemonized)
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
