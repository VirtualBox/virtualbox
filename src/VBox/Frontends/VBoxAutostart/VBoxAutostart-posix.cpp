/* $Id: VBoxAutostart-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/listeners.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <package-generated.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/time.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>

#include <signal.h>

#include "VBoxAutostart.h"

using namespace com;

#if defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD) || defined(RT_OS_DARWIN)
# define VBOXAUTOSTART_DAEMONIZE
#endif

ComPtr<IVirtualBoxClient> g_pVirtualBoxClient = NULL;
ComPtr<IVirtualBox>       g_pVirtualBox = NULL;
ComPtr<ISession>          g_pSession    = NULL;

/** Logging parameters. */
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */

/** Verbosity level. */
unsigned             g_cVerbosity = 0;

/** Run in background. */
static bool          g_fDaemonize = false;

/**
 * Command line arguments.
 */
static const RTGETOPTDEF g_aOptions[] = {
#ifdef VBOXAUTOSTART_DAEMONIZE
    { "--background",           'b',                                       RTGETOPT_REQ_NOTHING },
#endif
    /** For displayHelp(). */
    { "--help",                 'h',                                       RTGETOPT_REQ_NOTHING },
    { "--verbose",              'v',                                       RTGETOPT_REQ_NOTHING },
    { "--start",                's',                                       RTGETOPT_REQ_NOTHING },
    { "--stop",                 'd',                                       RTGETOPT_REQ_NOTHING },
    { "--config",               'c',                                       RTGETOPT_REQ_STRING },
    { "--logfile",              'F',                                       RTGETOPT_REQ_STRING },
    { "--logrotate",            'R',                                       RTGETOPT_REQ_UINT32 },
    { "--logsize",              'S',                                       RTGETOPT_REQ_UINT64 },
    { "--loginterval",          'I',                                       RTGETOPT_REQ_UINT32 },
    { "--quiet",                'Q',                                       RTGETOPT_REQ_NOTHING }
};

/** Set by the signal handler. */
static volatile bool    g_fCanceled = false;


/**
 * Signal handler that sets g_fCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Do not doing anything
 * unnecessary here.
 */
static void showProgressSignalHandler(int iSignal)
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fCanceled, true);
}

/**
 * Print out progress on the console.
 *
 * This runs the main event queue every now and then to prevent piling up
 * unhandled things (which doesn't cause real problems, just makes things
 * react a little slower than in the ideal case).
 */
DECLHIDDEN(HRESULT) showProgress(ComPtr<IProgress> progress)
{
    using namespace com;

    BOOL fCompleted = FALSE;
    ULONG ulCurrentPercent = 0;
    ULONG ulLastPercent = 0;

    Bstr bstrOperationDescription;

    NativeEventQueue::getMainEventQueue()->processEventQueue(0);

    ULONG cOperations = 1;
    HRESULT hrc = progress->COMGETTER(OperationCount)(&cOperations);
    if (FAILED(hrc))
    {
        RTStrmPrintf(g_pStdErr, "Progress object failure: %Rhrc\n", hrc);
        RTStrmFlush(g_pStdErr);
        return hrc;
    }

    /*
     * Note: Outputting the progress info to stderr (g_pStdErr) is intentional
     *       to not get intermixed with other (raw) stdout data which might get
     *       written in the meanwhile.
     */
    RTStrmPrintf(g_pStdErr, "0%%...");
    RTStrmFlush(g_pStdErr);

    /* setup signal handling if cancelable */
    bool fCanceledAlready = false;
    BOOL fCancelable;
    hrc = progress->COMGETTER(Cancelable)(&fCancelable);
    if (FAILED(hrc))
        fCancelable = FALSE;
    if (fCancelable)
    {
        signal(SIGINT,   showProgressSignalHandler);
#ifdef SIGBREAK
        signal(SIGBREAK, showProgressSignalHandler);
#endif
    }

    hrc = progress->COMGETTER(Completed(&fCompleted));
    while (SUCCEEDED(hrc))
    {
        progress->COMGETTER(Percent(&ulCurrentPercent));

        /* did we cross a 10% mark? */
        if (ulCurrentPercent / 10  >  ulLastPercent / 10)
        {
            /* make sure to also print out missed steps */
            for (ULONG curVal = (ulLastPercent / 10) * 10 + 10; curVal <= (ulCurrentPercent / 10) * 10; curVal += 10)
            {
                if (curVal < 100)
                {
                    RTStrmPrintf(g_pStdErr, "%u%%...", curVal);
                    RTStrmFlush(g_pStdErr);
                }
            }
            ulLastPercent = (ulCurrentPercent / 10) * 10;
        }

        if (fCompleted)
            break;

        /* process async cancelation */
        if (g_fCanceled && !fCanceledAlready)
        {
            hrc = progress->Cancel();
            if (SUCCEEDED(hrc))
                fCanceledAlready = true;
            else
                g_fCanceled = false;
        }

        /* make sure the loop is not too tight */
        progress->WaitForCompletion(100);

        NativeEventQueue::getMainEventQueue()->processEventQueue(0);
        hrc = progress->COMGETTER(Completed(&fCompleted));
    }

    /* undo signal handling */
    if (fCancelable)
    {
        signal(SIGINT,   SIG_DFL);
#ifdef SIGBREAK
        signal(SIGBREAK, SIG_DFL);
#endif
    }

    /* complete the line. */
    LONG iRc = (LONG)E_FAIL;
    hrc = progress->COMGETTER(ResultCode)(&iRc);
    if (SUCCEEDED(hrc))
    {
        if (SUCCEEDED(iRc))
            RTStrmPrintf(g_pStdErr, "100%%\n");
        else if (g_fCanceled)
            RTStrmPrintf(g_pStdErr, "CANCELED\n");
        else
        {
            RTStrmPrintf(g_pStdErr, "\n");
            RTStrmPrintf(g_pStdErr, "Progress state: %Rhrc\n", iRc);
        }
        hrc = (HRESULT)iRc;
    }
    else
    {
        RTStrmPrintf(g_pStdErr, "\n");
        RTStrmPrintf(g_pStdErr, "Progress object failure: %Rhrc\n", hrc);
    }
    RTStrmFlush(g_pStdErr);
    return hrc;
}

DECLHIDDEN(void) autostartSvcOsLogStr(const char *pszMsg, AUTOSTARTLOGTYPE enmLogType)
{
    if (   enmLogType == AUTOSTARTLOGTYPE_VERBOSE
        && !g_cVerbosity)
        return;

    LogRel(("%s", pszMsg));
}

/**
 * Shows the help.
 *
 * @param   pszImage                Name of program name (image).
 */
static void showHelp(const char *pszImage)
{
    AssertPtrReturnVoid(pszImage);

    autostartSvcShowHeader();

    RTStrmPrintf(g_pStdErr,
                 "Usage: %s [-v|--verbose] [-h|-?|--help]\n"
                 "           [-V|--version]\n"
                 "           [-F|--logfile=<file>] [-R|--logrotate=<num>]\n"
                 "           [-S|--logsize=<bytes>] [-I|--loginterval=<seconds>]\n"
                 "           [-c|--config=<config file>]\n",
                 pszImage);

    RTStrmPrintf(g_pStdErr,
                 "\n"
                 "Options:\n");
    for (unsigned i = 0; i < RT_ELEMENTS(g_aOptions); i++)
    {
        const char *pcszDescr;
        switch (g_aOptions[i].iShort)
        {
            case 'h':
                pcszDescr = "Prints this help message and exit.";
                break;

#ifdef VBOXAUTOSTART_DAEMONIZE
            case 'b':
                pcszDescr = "Run in background (daemon mode).";
                break;
#endif

            case 'F':
                pcszDescr = "Name of file to write log to (no file).";
                break;

            case 'R':
                pcszDescr = "Number of log files (0 disables log rotation).";
                break;

            case 'S':
                pcszDescr = "Maximum size of a log file to trigger rotation (bytes).";
                break;

            case 'I':
                pcszDescr = "Maximum time interval to trigger log rotation (seconds).";
                break;

            case 'c':
                pcszDescr = "Name of the configuration file for the global overrides.";
                break;

            case 'V':
                pcszDescr = "Shows the service version.";
                break;

            default:
                AssertFailedBreakStmt(pcszDescr = "");
        }

        if (g_aOptions[i].iShort < 1000)
            RTStrmPrintf(g_pStdErr,
                         "  %s, -%c\n"
                         "      %s\n", g_aOptions[i].pszLong, g_aOptions[i].iShort, pcszDescr);
        else
            RTStrmPrintf(g_pStdErr,
                         "  %s\n"
                         "      %s\n", g_aOptions[i].pszLong, pcszDescr);
    }

    RTStrmPrintf(g_pStdErr,
                 "\n"
                 "Use environment variable VBOXAUTOSTART_RELEASE_LOG for logging options.\n");
}

int main(int argc, char *argv[])
{
    /*
     * Before we do anything, init the runtime without loading
     * the support driver.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse the global options
    */
    int c;
    const char *pszLogFile = NULL;
    const char *pszConfigFile = NULL;
    bool fQuiet = false;
    bool fStart = false;
    bool fStop  = false;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv,
                 g_aOptions, RT_ELEMENTS(g_aOptions), 1 /* First */, 0 /*fFlags*/);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'h':
                showHelp(argv[0]);
                return RTEXITCODE_SUCCESS;

            case 'v':
                g_cVerbosity++;
                break;

#ifdef VBOXAUTOSTART_DAEMONIZE
            case 'b':
                g_fDaemonize = true;
                break;
#endif
            case 'V':
                autostartSvcShowVersion(false);
                return RTEXITCODE_SUCCESS;

            case 'F':
                pszLogFile = ValueUnion.psz;
                break;

            case 'R':
                g_cHistory = ValueUnion.u32;
                break;

            case 'S':
                g_uHistoryFileSize = ValueUnion.u64;
                break;

            case 'I':
                g_uHistoryFileTime = ValueUnion.u32;
                break;

            case 'Q':
                fQuiet = true;
                break;

            case 'c':
                pszConfigFile = ValueUnion.psz;
                break;

            case 's':
                fStart = true;
                break;

            case 'd':
                fStop = true;
                break;

            default:
                return RTGetOptPrintError(c, &ValueUnion);
        }
    }

    if (!fStart && !fStop)
    {
        showHelp(argv[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Either --start or --stop must be present");
    }
    else if (fStart && fStop)
    {
        showHelp(argv[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "--start or --stop are mutually exclusive");
    }

    if (!pszConfigFile)
    {
        showHelp(argv[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "--config <config file> is missing");
    }

    if (!fQuiet)
        autostartSvcShowHeader();

    PCFGAST pCfgAst = NULL;
    char *pszUser = NULL;
    PCFGAST pCfgAstUser = NULL;
    PCFGAST pCfgAstPolicy = NULL;
    PCFGAST pCfgAstUserHome = NULL;
    bool fAllow = false;

    rc = autostartParseConfig(pszConfigFile, &pCfgAst);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    rc = RTProcQueryUsernameA(RTProcSelf(), &pszUser);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query username of the process");

    pCfgAstUser = autostartConfigAstGetByName(pCfgAst, pszUser);
    pCfgAstPolicy = autostartConfigAstGetByName(pCfgAst, "default_policy");

    /* Check default policy. */
    if (pCfgAstPolicy)
    {
        if (   pCfgAstPolicy->enmType == CFGASTNODETYPE_KEYVALUE
            && (   !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "allow")
                || !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "deny")))
        {
            if (!RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "allow"))
                fAllow = true;
        }
        else
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "'default_policy' must be either 'allow' or 'deny'");
    }

    if (   pCfgAstUser
        && pCfgAstUser->enmType == CFGASTNODETYPE_COMPOUND)
    {
        pCfgAstPolicy = autostartConfigAstGetByName(pCfgAstUser, "allow");
        if (pCfgAstPolicy)
        {
            if (   pCfgAstPolicy->enmType == CFGASTNODETYPE_KEYVALUE
                && (   !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "true")
                    || !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "false")))
            {
                if (!RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "true"))
                    fAllow = true;
                else
                    fAllow = false;
            }
            else
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "'allow' must be either 'true' or 'false'");
        }
        pCfgAstUserHome = autostartConfigAstGetByName(pCfgAstUser, "VBOX_USER_HOME");
        if (   pCfgAstUserHome
            && pCfgAstUserHome->enmType == CFGASTNODETYPE_KEYVALUE)
        {
            rc = RTEnvSet("VBOX_USER_HOME", pCfgAstUserHome->u.KeyValue.aszValue);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "'VBOX_USER_HOME' could not be set for this user");
        }
    }
    else if (pCfgAstUser)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid config, user is not a compound node");

    if (!fAllow)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "User is not allowed to autostart VMs");

    RTStrFree(pszUser);

    /* Don't start if the VirtualBox settings directory does not exist. */
    char szUserHomeDir[RTPATH_MAX];
    rc = com::GetVBoxUserHomeDirectory(szUserHomeDir, sizeof(szUserHomeDir), false /* fCreateDir */);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not get base directory: %Rrc", rc);
    else if (!RTDirExists(szUserHomeDir))
        return RTEXITCODE_SUCCESS;

    /* create release logger, to stdout */
    RTERRINFOSTATIC ErrInfo;
    rc = com::VBoxLogRelCreate("Autostart", g_fDaemonize ? NULL : pszLogFile,
                               RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                               "all", "VBOXAUTOSTART_RELEASE_LOG",
                               RTLOGDEST_STDOUT, UINT32_MAX /* cMaxEntriesPerGroup */,
                               g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                               RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, rc);

#ifdef VBOXAUTOSTART_DAEMONIZE
    if (g_fDaemonize)
    {
        /* prepare release logging */
        char szLogFile[RTPATH_MAX];

        if (!pszLogFile || !*pszLogFile)
        {
            rc = com::GetVBoxUserHomeDirectory(szLogFile, sizeof(szLogFile));
            if (RT_FAILURE(rc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not get base directory for logging: %Rrc", rc);
            rc = RTPathAppend(szLogFile, sizeof(szLogFile), "vboxautostart.log");
            if (RT_FAILURE(rc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not construct logging path: %Rrc", rc);
            pszLogFile = szLogFile;
        }

        rc = RTProcDaemonizeUsingFork(false /* fNoChDir */, false /* fNoClose */, NULL);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to daemonize, rc=%Rrc. exiting.", rc);
        /* create release logger, to file */
        rc = com::VBoxLogRelCreate("Autostart", pszLogFile,
                                   RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                                   "all", "VBOXAUTOSTART_RELEASE_LOG",
                                   RTLOGDEST_FILE, UINT32_MAX /* cMaxEntriesPerGroup */,
                                   g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                                   RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, rc);
    }
#endif

    /* Set up COM */
    rc = autostartSetup();
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    if (fStart)
        rc = autostartStartMain(pCfgAstUser);
    else
    {
        Assert(fStop);
        rc = autostartStopMain(pCfgAstUser);
    }

    autostartConfigAstDestroy(pCfgAst);
    NativeEventQueue::getMainEventQueue()->processEventQueue(0);
    autostartShutdown();

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

