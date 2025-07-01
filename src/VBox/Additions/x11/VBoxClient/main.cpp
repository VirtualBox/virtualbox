/* $Id: main.cpp 110055 2025-07-01 06:32:10Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Guest Additions - X11 Client.
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
#include <sys/wait.h>
#include <stdlib.h>       /* For exit */
#include <signal.h>
#include <X11/Xlib.h>
#include "product-generated.h"
#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/errno.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/env.h>
#include <iprt/process.h>
#include <iprt/linux/sysfs.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include "VBoxClient.h"


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define VBOXCLIENT_OPT_SERVICES             980
#define VBOXCLIENT_OPT_CHECKHOSTVERSION     VBOXCLIENT_OPT_SERVICES
#define VBOXCLIENT_OPT_CLIPBOARD            VBOXCLIENT_OPT_SERVICES + 1
#define VBOXCLIENT_OPT_DRAGANDDROP          VBOXCLIENT_OPT_SERVICES + 2
#define VBOXCLIENT_OPT_SEAMLESS             VBOXCLIENT_OPT_SERVICES + 3
#define VBOXCLIENT_OPT_VMSVGA               VBOXCLIENT_OPT_SERVICES + 4
#define VBOXCLIENT_OPT_VMSVGA_SESSION       VBOXCLIENT_OPT_SERVICES + 5
#define VBOXCLIENT_OPT_DISPLAY              VBOXCLIENT_OPT_SERVICES + 6
#define VBOXCLIENT_OPT_SESSION_DETECT       VBOXCLIENT_OPT_SERVICES + 7
#define VBOXCLIENT_OPT_SESSION_TYPE         VBOXCLIENT_OPT_SERVICES + 8
#define VBOXCLIENT_OPT_WAYLAND              VBOXCLIENT_OPT_SERVICES + 9


/*********************************************************************************************************************************
*   Local structures                                                                                                             *
*********************************************************************************************************************************/
/**
 * The global service state.
 */
typedef struct VBCLSERVICESTATE
{
    /** Pointer to the service descriptor. */
    PVBCLSERVICE    pDesc;
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
} VBCLSERVICESTATE;
/** Pointer to a service state. */
typedef VBCLSERVICESTATE *PVBCLSERVICESTATE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The global service state. */
VBCLSERVICESTATE       g_Service = { 0 };

/** Set by the signal handler when being called. */
static volatile bool   g_fSignalHandlerCalled = false;
/** Critical section for the signal handler. */
static RTCRITSECT      g_csSignalHandler;
/** Flag indicating Whether the service starts in daemonized mode or not. */
bool                   g_fDaemonized = false;
/** The name of our pidfile.  It is global for the benefit of the cleanup
 * routine. */
static char            g_szPidFile[RTPATH_MAX] = "";
/** The file handle of our pidfile.  It is global for the benefit of the
 * cleanup routine. */
static RTFILE          g_hPidFile;
/** The name of pidfile for parent (control) process. */
static char            g_szControlPidFile[RTPATH_MAX] = "";
/** The file handle of parent process pidfile. */
static RTFILE          g_hControlPidFile;
/** The display server type to use. */
static VBGHDISPLAYSERVERTYPE g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_AUTO;

/** Global critical section held during the clean-up routine (to prevent it
 * being called on multiple threads at once) or things which may not happen
 * during clean-up (e.g. pausing and resuming the service).
 */
static RTCRITSECT    g_critSect;
/** Counter of how often our daemon has been respawned. */
unsigned             g_cRespawn = 0;
/** Logging verbosity level. */
unsigned             g_cVerbosity = 0;
/** Absolute path to log file, if any. */
static char          g_szLogFile[RTPATH_MAX + 128] = "";
/** Set by the signal handler when SIGUSR1 received. */
static volatile bool g_fProcessReloadRequested = false;


/**
 * Shut down if we get a signal or something.
 *
 * This is extern so that we can call it from other compilation units.
 */
void VBClShutdown(bool fExit /*=true*/)
{
    /* We never release this, as we end up with a call to exit(3) which is not
     * async-safe.  Unless we fix this application properly, we should be sure
     * never to exit from anywhere except from this method. */
    int rc = RTCritSectEnter(&g_critSect);
    if (RT_FAILURE(rc))
        VBClLogFatalError("Failure while acquiring the global critical section, rc=%Rrc\n", rc);

    /* Ask service to stop. */
    if (g_Service.pDesc &&
        g_Service.pDesc->pfnStop)
    {
        ASMAtomicWriteBool(&g_Service.fShutdown, true);
        g_Service.pDesc->pfnStop();

    }

    if (g_szPidFile[0] && g_hPidFile)
        VbglR3ClosePidFile(g_szPidFile, g_hPidFile);

    VBClLogDestroy();

    if (fExit)
        exit(RTEXITCODE_SUCCESS);
}

/**
 * Returns the current display server type.
 *
 * @returns The display server type.
 */
VBGHDISPLAYSERVERTYPE VBClGetDisplayServerType(void)
{
    return g_enmDisplayServerType;
}

/**
 * Xlib error handler for certain errors that we can't avoid.
 */
static int vboxClientXLibErrorHandler(Display *pDisplay, XErrorEvent *pError)
{
    char errorText[1024];

    XGetErrorText(pDisplay, pError->error_code, errorText, sizeof(errorText));
    VBClLogError("An X Window protocol error occurred: %s (error code %d).  Request code: %d, minor code: %d, serial number: %d\n",
                 errorText, pError->error_code, pError->request_code, pError->minor_code, pError->serial);
    return 0;
}

/**
 * Xlib error handler for fatal errors.  This often means that the programme is still running
 * when X exits.
 */
static int vboxClientXLibIOErrorHandler(Display *pDisplay)
{
    RT_NOREF1(pDisplay);
    VBClLogError("A fatal guest X Window error occurred. This may just mean that the Window system was shut down while the client was still running\n");
    VBClShutdown();
    return 0;  /* We should never reach this. */
}

/**
 * A standard signal handler which cleans up and exits.
 */
static void vboxClientSignalHandler(int iSignal)
{
    int rc;

    /* On Wayland, SIGPIPE might be issued if compositor no longer wants
     * to communicate. This should not be a reason for process termination. */
    if (iSignal == SIGPIPE)
        return;

    rc = RTCritSectEnter(&g_csSignalHandler);
    if (RT_SUCCESS(rc))
    {
        if (g_fSignalHandlerCalled)
        {
            RTCritSectLeave(&g_csSignalHandler);
            return;
        }

        VBClLogVerbose(2, "Received signal %d\n", iSignal);
        g_fSignalHandlerCalled = true;

        /* In our internal convention, when VBoxClient process receives SIGUSR1,
         * this is a trigger for restarting a process with exec() call. Usually
         * happens as a result of Guest Additions update in order to seamlessly
         * restart newly installed binaries. */
        if (iSignal == SIGUSR1)
            g_fProcessReloadRequested = true;

        /* Leave critical section before stopping the service. */
        RTCritSectLeave(&g_csSignalHandler);

        if (   g_Service.pDesc
            && g_Service.pDesc->pfnStop)
        {
            VBClLogVerbose(2, "Notifying service to stop ...\n");

            /* Signal the service to stop. */
            ASMAtomicWriteBool(&g_Service.fShutdown, true);

            g_Service.pDesc->pfnStop();

            VBClLogVerbose(2, "Service notified to stop, waiting on worker thread to stop ...\n");
        }
    }
}

/**
 * Reset all standard termination signals to call our signal handler.
 */
static int vboxClientSignalHandlerInstall(void)
{
    struct sigaction sigAction;
    sigAction.sa_handler = vboxClientSignalHandler;
    sigemptyset(&sigAction.sa_mask);
    sigAction.sa_flags = 0;
    sigaction(SIGHUP, &sigAction, NULL);
    sigaction(SIGINT, &sigAction, NULL);
    sigaction(SIGQUIT, &sigAction, NULL);
    sigaction(SIGPIPE, &sigAction, NULL);
    sigaction(SIGALRM, &sigAction, NULL);
    sigaction(SIGTERM, &sigAction, NULL);
    sigaction(SIGUSR1, &sigAction, NULL);
    sigaction(SIGUSR2, &sigAction, NULL);

    return RTCritSectInit(&g_csSignalHandler);
}

/**
 * Uninstalls a previously installed signal handler.
 */
static int vboxClientSignalHandlerUninstall(void)
{
    signal(SIGTERM,  SIG_DFL);
#ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
#endif

    return RTCritSectDelete(&g_csSignalHandler);
}

/**
 * Print out a usage message and exit with success.
 */
static void vboxClientUsage(const char *pcszFileName)
{
    RTPrintf(VBOX_PRODUCT " VBoxClient "
             VBOX_VERSION_STRING "\n"
             "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");

    RTPrintf("Usage: %s "
#ifdef VBOX_WITH_SHARED_CLIPBOARD
             "--clipboard|"
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
             "--draganddrop|"
#endif
#ifdef VBOX_WITH_GUEST_PROPS
             "--checkhostversion|"
#endif
#ifdef VBOX_WITH_SEAMLESS
             "--seamless|"
#endif
#ifdef VBOX_WITH_VMSVGA
             "--vmsvga|"
             "--vmsvga-session"
#endif
             "\n[-d|--nodaemon]\n", pcszFileName);
    RTPrintf("\n");
    RTPrintf("Options:\n");
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    RTPrintf("  --clipboard          starts the shared clipboard service\n");
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
    RTPrintf("  --draganddrop        starts the drag and drop service\n");
#endif
#ifdef VBOX_WITH_GUEST_PROPS
    RTPrintf("  --checkhostversion   starts the host version notifier service\n");
#endif
#ifdef VBOX_WITH_SEAMLESS
    RTPrintf("  --seamless           starts the seamless windows service\n");
#endif
#ifdef VBOX_WITH_VMSVGA
    RTPrintf("  --vmsvga             starts VMSVGA dynamic resizing for X11/Wayland guests\n");
#ifdef RT_OS_LINUX
    RTPrintf("  --vmsvga-session     starts Desktop Environment specific screen assistant for X11/Wayland guests\n"
             "                       (VMSVGA graphics adapter only)\n");
#else
    RTPrintf("  --vmsvga-session     an alias for --vmsvga\n");
#endif
    RTPrintf("  --display            starts VMSVGA dynamic resizing for legacy guests\n");
#endif
    RTPrintf("  --session-type       specifies the session type to use (auto, x11, wayland)\n");
    RTPrintf("  --session-detect     detects and prints the current session type\n"
             "                       (exit code 0 if detection succeeded)\n");
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
    RTPrintf("  --wayland            starts the shared clipboard and drag-and-drop services for Wayland\n");
#endif
    RTPrintf("  -f, --foreground     run in the foreground (no daemonizing)\n");
    RTPrintf("  -d, --nodaemon       continues running as a system service\n");
    RTPrintf("  -h, --help           shows this help text\n");
    RTPrintf("  -l, --logfile <path> enables logging to a file\n");
    RTPrintf("  -v, --verbose        increases logging verbosity level\n");
    RTPrintf("  -V, --version        shows version information\n");
    RTPrintf("\n");
}

/**
 * Complains about seeing more than one service specification.
 *
 * @returns RTEXITCODE_SYNTAX.
 */
static int vbclSyntaxOnlyOneService(void)
{
    RTMsgError("More than one service specified! Only one, please.");
    return RTEXITCODE_SYNTAX;
}

/**
 * The service thread.
 *
 * @returns Whatever the worker function returns.
 * @param   ThreadSelf      My thread handle.
 * @param   pvUser          The service index.
 */
static DECLCALLBACK(int) vbclThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PVBCLSERVICESTATE pState = (PVBCLSERVICESTATE)pvUser;
    AssertPtrReturn(pState, VERR_INVALID_POINTER);

#ifndef RT_OS_WINDOWS
    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);
#endif

    AssertPtrReturn(pState->pDesc->pfnWorker, VERR_INVALID_POINTER);
    int rc = pState->pDesc->pfnWorker(&pState->fShutdown);

    VBClLogVerbose(2, "Worker loop ended with %Rrc\n", rc);

    ASMAtomicXchgBool(&pState->fShutdown, true);
    RTThreadUserSignal(ThreadSelf);
    return rc;
}

/**
 * Wait for SIGUSR1 and re-exec.
 */
static void vbclHandleUpdateStarted(char *const argv[])
{
    /* Context of parent process */
    sigset_t signalMask;
    int      iSignal;
    int      rc;

    /* Release reference to guest driver. */
    VbglR3Term();

    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGUSR1);
    rc = pthread_sigmask(SIG_BLOCK, &signalMask, NULL);

    if (rc == 0)
    {
        LogRel(("%s: waiting for Guest Additions installation to be completed\n",
                g_Service.pDesc->pszDesc));

        /* Wait for SIGUSR1. */
        rc = sigwait(&signalMask, &iSignal);
        if (rc == 0)
        {
            LogRel(("%s: Guest Additions installation to be completed, reloading service\n",
                    g_Service.pDesc->pszDesc));

            /* Release pidfile, otherwise new VBoxClient instance won't be able to quire it. */
            VBClShutdown(false);

            rc = RTProcCreate(argv[0], argv, RTENV_DEFAULT,
                              RTPROC_FLAGS_DETACHED | RTPROC_FLAGS_SEARCH_PATH, NULL);
            if (RT_SUCCESS(rc))
                LogRel(("%s: service restarted\n", g_Service.pDesc->pszDesc));
            else
                LogRel(("%s: cannot replace running image with %s (%s), automatic service reloading has failed\n",
                        g_Service.pDesc->pszDesc, argv[0], strerror(errno)));
        }
        else
            LogRel(("%s: cannot wait for signal (%s), automatic service reloading has failed\n",
                    g_Service.pDesc->pszDesc, strerror(errno)));
    }
    else
        LogRel(("%s: failed to setup signal handler, automatic service reloading has failed\n",
                g_Service.pDesc->pszDesc));

    exit(RT_BOOL(rc != 0));
}

/**
 * Compose pidfile name.
 *
 * @returns IPRT status code.
 * @param   szBuf           Buffer to store pidfile name into.
 * @param   cbBuf           Size of buffer.
 * @param   szTemplate      Null-terminated string which contains pidfile name.
 * @param   fParentProcess  Whether pidfile path should be composed for
 *                          parent (control) process or for a child (actual
 *                          service) process.
 */
static int vbclGetPidfileName(char *szBuf, size_t cbBuf, const char *szTemplate,
                              bool fParentProcess)
{
    int rc;
    char pszActiveTTY[128];
    size_t cchRead;

    RT_ZERO(pszActiveTTY);

    AssertPtrReturn(szBuf, VERR_INVALID_PARAMETER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(szTemplate, VERR_INVALID_PARAMETER);

    rc = RTPathUserHome(szBuf, cbBuf);
    if (RT_FAILURE(rc))
        VBClLogFatalError("%s: getting home directory failed: %Rrc\n",
                          g_Service.pDesc->pszDesc, rc);

    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szBuf, cbBuf, szTemplate);

#ifdef RT_OS_LINUX
    if (RT_SUCCESS(rc))
        rc = RTLinuxSysFsReadStrFile(pszActiveTTY, sizeof(pszActiveTTY) - 1 /* reserve last byte for string termination */,
                                     &cchRead, "class/tty/tty0/active");
    if (RT_SUCCESS(rc))
    {
        RTStrCat(szBuf, cbBuf, "-");
        RTStrCat(szBuf, cbBuf, pszActiveTTY);
    }
    else
    {
        VBClLogInfo("%s: cannot detect currently active tty device, "
                    "multiple service instances for a single user will not be allowed, rc=%Rrc",
                    g_Service.pDesc->pszDesc, rc);

        /* Older kernels, such as 2.6 series, do not have /sys/class/tty/tty0/active.
         * For such cases, simply reset rc and let VBoxClient to start normally.
         * A side effect is if user runs multiple XServers, only one
         * instance of VBoxClient service can be started (for example, there will
         * be no Shared Clipboard for the second XServer session). */
        rc = (rc == VERR_FILE_NOT_FOUND ? VINF_SUCCESS : rc);
    }
#endif /* RT_OS_LINUX */

    if (RT_SUCCESS(rc))
        RTStrCat(szBuf, cbBuf, fParentProcess ? "-control.pid" : "-service.pid");

    if (RT_FAILURE(rc))
        VBClLogFatalError("%s: creating PID file path failed: %Rrc\n",
                          g_Service.pDesc->pszDesc, rc);

    return rc;
}

/**
 * The main loop for the VBoxClient daemon.
 */
int main(int argc, char *argv[])
{
    /* Note: No VBClLogXXX calls before actually creating the log. */

    /* Initialize our runtime before all else. */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /* A flag which is returned to the parent process when Guest Additions update started. */
    bool fUpdateStarted = false;

    /* Get our file name for usage info and hints. */
    const char *pcszFileName = RTPathFilename(argv[0]);
    if (!pcszFileName)
        pcszFileName = "VBoxClient";

    /* Parse our option(s). */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--nodaemon",                     'd',                                RTGETOPT_REQ_NOTHING },
        { "--foreground",                   'f',                                RTGETOPT_REQ_NOTHING },
        { "--help",                         'h',                                RTGETOPT_REQ_NOTHING },
        { "--logfile",                      'l',                                RTGETOPT_REQ_STRING  },
        { "--version",                      'V',                                RTGETOPT_REQ_NOTHING },
        { "--verbose",                      'v',                                RTGETOPT_REQ_NOTHING },

        /* Services */
#ifdef VBOX_WITH_GUEST_PROPS
        { "--checkhostversion",             VBOXCLIENT_OPT_CHECKHOSTVERSION,    RTGETOPT_REQ_NOTHING },
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD
        { "--clipboard",                    VBOXCLIENT_OPT_CLIPBOARD,           RTGETOPT_REQ_NOTHING },
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
        { "--draganddrop",                  VBOXCLIENT_OPT_DRAGANDDROP,         RTGETOPT_REQ_NOTHING },
#endif
#ifdef VBOX_WITH_SEAMLESS
        { "--seamless",                     VBOXCLIENT_OPT_SEAMLESS,            RTGETOPT_REQ_NOTHING },
#endif
#ifdef VBOX_WITH_VMSVGA
        { "--vmsvga",                       VBOXCLIENT_OPT_VMSVGA,              RTGETOPT_REQ_NOTHING },
        { "--vmsvga-session",               VBOXCLIENT_OPT_VMSVGA_SESSION,      RTGETOPT_REQ_NOTHING },
        { "--display",                      VBOXCLIENT_OPT_DISPLAY,             RTGETOPT_REQ_NOTHING },
#endif
        { "--session-detect",               VBOXCLIENT_OPT_SESSION_DETECT,      RTGETOPT_REQ_NOTHING },
        { "--session-type",                 VBOXCLIENT_OPT_SESSION_TYPE,        RTGETOPT_REQ_STRING  },
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
        { "--wayland",                      VBOXCLIENT_OPT_WAYLAND,             RTGETOPT_REQ_NOTHING }
#endif
    };

    int                     ch;
    RTGETOPTUNION           ValueUnion;
    RTGETOPTSTATE           GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to parse command line options, rc=%Rrc\n", rc);

    AssertRC(rc);

    bool fDaemonise = true;
    bool fRespawn   = true;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case 'd':
            {
                fDaemonise = false;
                break;
            }

            case 'h':
            {
                vboxClientUsage(pcszFileName);
                return RTEXITCODE_SUCCESS;
            }

            case 'f':
            {
               fDaemonise = false;
               fRespawn   = false;
               break;
            }

            case 'l':
            {
                rc = RTStrCopy(g_szLogFile, sizeof(g_szLogFile), ValueUnion.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("Unable to set log file path, rc=%Rrc\n", rc);
                break;
            }

            case 'n':
            {
                fRespawn = false;
                break;
            }

            case 'v':
            {
                g_cVerbosity++;
                break;
            }

            case 'V':
            {
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;
            }

            /* Services */
#ifdef VBOX_WITH_GUEST_PROPS
            case VBOXCLIENT_OPT_CHECKHOSTVERSION:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcHostVersion;
                break;
            }
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD
            case VBOXCLIENT_OPT_CLIPBOARD:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcClipboard;
                break;
            }
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
            case VBOXCLIENT_OPT_DRAGANDDROP:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcDragAndDrop;
                break;
            }
#endif
#ifdef VBOX_WITH_SEAMLESS
            case VBOXCLIENT_OPT_SEAMLESS:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcSeamless;
                break;
            }
#endif
#ifdef VBOX_WITH_VMSVGA
            case VBOXCLIENT_OPT_VMSVGA:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcDisplaySVGA;
                break;
            }

            case VBOXCLIENT_OPT_VMSVGA_SESSION:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
# ifdef RT_OS_LINUX
                g_Service.pDesc = &g_SvcDisplaySVGASession;
# else
                g_Service.pDesc = &g_SvcDisplaySVGA;
# endif
                break;
            }

            case VBOXCLIENT_OPT_DISPLAY:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcDisplayLegacy;
                break;
            }
#endif
            case VBOXCLIENT_OPT_SESSION_DETECT:
            {
                rc = VBClLogCreateEx("" /* No file logging */, false /* No header */);
                if (RT_SUCCESS(rc))
                {
                    /* Make sure that we increase the verbosity (if needed), to gain some more insights
                     * when detecting the display server. */
                    rc = VBClLogModify("stdout", g_cVerbosity);
                    if (RT_SUCCESS(rc))
                    {
                        VBGHDISPLAYSERVERTYPE const enmType = VBGHDisplayServerTypeDetect();
                        VBClLogInfo("Detected session: %s\n", VBGHDisplayServerTypeToStr(enmType));
                        return enmType != VBGHDISPLAYSERVERTYPE_NONE ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
                    }
                }

                return RTEXITCODE_FAILURE;
            }

            case VBOXCLIENT_OPT_SESSION_TYPE:
            {
                if (!RTStrICmp(ValueUnion.psz, "x11"))
                    g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_X11;
                else if (!RTStrICmp(ValueUnion.psz, "wayland"))
                    g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_PURE_WAYLAND;
                else if (!RTStrICmp(ValueUnion.psz, "none"))
                    g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_NONE;
                else if (!RTStrICmp(ValueUnion.psz, "auto"))
                    g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_AUTO;
                else
                {
                    RTMsgError("Session type \"%s\" is invalid; defaulting to \"auto\" instead.\n", ValueUnion.psz);
                    g_enmDisplayServerType = VBGHDISPLAYSERVERTYPE_AUTO;
                }
                break;
            }
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
            case VBOXCLIENT_OPT_WAYLAND:
            {
                if (g_Service.pDesc)
                    return vbclSyntaxOnlyOneService();
                g_Service.pDesc = &g_SvcWayland;
                break;
            }
#endif

            case VINF_GETOPT_NOT_OPTION:
                break;

            case VERR_GETOPT_UNKNOWN_OPTION:
                RT_FALL_THROUGH();
            default:
            {
                if (   g_Service.pDesc
                    && g_Service.pDesc->pfnOption)
                {
                    rc = g_Service.pDesc->pfnOption(NULL, argc, argv, &GetState.iNext);
                }
                else /* No service specified yet. */
                    rc = VERR_NOT_FOUND;

                if (RT_FAILURE(rc))
                {
                    RTMsgError("unrecognized option '%s'", ValueUnion.psz);
                    RTMsgInfo("Try '%s --help' for more information", pcszFileName);
                    return RTEXITCODE_SYNTAX;
                }
                break;
            }

        } /* switch */
    } /* while RTGetOpt */

    if (!g_Service.pDesc)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No service specified. Quitting because nothing to do!");

    /* Initialize VbglR3 before we do anything else with the logger. */
    rc = VbglR3InitUser();
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("VbglR3InitUser failed: %Rrc", rc);

    rc = VBClLogCreate(g_szLogFile[0] ? g_szLogFile : "");
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE; /* Error message already printed in VBClLogCreateEx(). */

    /* If the user is running in "no daemon" mode, send critical logging to stdout as well. */
    rc = VBClLogModify(fDaemonise ? "" : "stdout", g_cVerbosity);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE; /* Error message already printed in VBClLogModify(). */

    VBClLogInfo("VBoxClient %s r%s started. Verbose level = %d\n",
                RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity);

    /* Try to detect the current session type early on, if needed. */
    if (g_enmDisplayServerType == VBGHDISPLAYSERVERTYPE_AUTO)
    {
        g_enmDisplayServerType = VBGHDisplayServerTypeDetect();
    }
    else
        VBClLogInfo("Session type was manually set to: %s\n", VBGHDisplayServerTypeToStr(g_enmDisplayServerType));

    VBClLogInfo("Session type is: %s\n", VBGHDisplayServerTypeToStr(g_enmDisplayServerType));

    VBClLogInfo("Service: %s\n", g_Service.pDesc->pszDesc);

    rc = RTCritSectInit(&g_critSect);
    if (RT_FAILURE(rc))
        VBClLogFatalError("Initializing critical section failed: %Rrc\n", rc);
    if (g_Service.pDesc->pszPidFilePathTemplate)
    {
        /* Get pidfile name for parent (control) process. */
        rc = vbclGetPidfileName(g_szControlPidFile, sizeof(g_szControlPidFile), g_Service.pDesc->pszPidFilePathTemplate, true);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;

        /* Get pidfile name for service process. */
        rc = vbclGetPidfileName(g_szPidFile, sizeof(g_szPidFile), g_Service.pDesc->pszPidFilePathTemplate, false);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;
    }

    if (fDaemonise)
    {
        VBClLogInfo("Daemonizing service ...\n");
        rc = VbglR3DaemonizeEx(false /* fNoChDir */, false /* fNoClose */, fRespawn, &g_cRespawn,
                               true /* fReturnOnUpdate */, &fUpdateStarted, g_szControlPidFile, &g_hControlPidFile);
        if (RT_SUCCESS(rc))
        {
            g_fDaemonized = true;

            if (fUpdateStarted) /* This combination only works in context of parent process. */
                vbclHandleUpdateStarted(argv);
        }
        else
            return RTMsgErrorExitFailure("Daemonizing service failed: %Rrc\n", rc);
    }

    if (g_szPidFile[0])
    {
        rc = VbglR3PidFile(g_szPidFile, &g_hPidFile);
        if (rc == VERR_FILE_LOCK_VIOLATION)  /* Already running. */
        {
            VBClLogInfo("%s: service already running, exitting\n",
                        g_Service.pDesc->pszDesc);
            return RTEXITCODE_SUCCESS;
        }
        if (RT_FAILURE(rc))
        {
            VBClLogFatalError("Creating PID file %s failed: %Rrc\n", g_szPidFile, rc);
            return RTEXITCODE_FAILURE;
        }
    }

    if (g_enmDisplayServerType == VBGHDISPLAYSERVERTYPE_X11)
    {
        /* This should never be called twice in one process - in fact one Display
         * object should probably never be used from multiple threads anyway. */
        if (!XInitThreads())
            return RTMsgErrorExitFailure("Failed to initialize X11 threads\n");
        /* Set an X11 error handler, so that we don't die when we get unavoidable
         * errors. */
        XSetErrorHandler(vboxClientXLibErrorHandler);
        /* Set an X11 I/O error handler, so that we can shutdown properly on
         * fatal errors. */
        XSetIOErrorHandler(vboxClientXLibIOErrorHandler);
    }

    bool fSignalHandlerInstalled = false;
    if (RT_SUCCESS(rc))
    {
        rc = vboxClientSignalHandlerInstall();
        if (RT_SUCCESS(rc))
            fSignalHandlerInstalled = true;
    }

    if (   RT_SUCCESS(rc)
        && g_Service.pDesc->pfnInit)
    {
        VBClLogInfo("Initializing service ...\n");
        rc = g_Service.pDesc->pfnInit();
    }

    if (RT_SUCCESS(rc))
    {
        VBClLogInfo("Creating worker thread ...\n");
        rc = RTThreadCreate(&g_Service.Thread, vbclThread, (void *)&g_Service, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, g_Service.pDesc->pszName);
        if (RT_FAILURE(rc))
        {
            VBClLogError("Creating worker thread failed, rc=%Rrc\n", rc);
        }
        else
        {
            g_Service.fStarted = true;

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
            RTThreadUserWait(g_Service.Thread, RT_MS_1MIN);
            if (g_Service.fShutdown)
            {
                VBClLogError("Service failed to start!\n");
                rc = VERR_GENERAL_FAILURE;
            }
            else
            {
                VBClLogInfo("Service started\n");

                int rcThread;
                rc = RTThreadWait(g_Service.Thread, RT_INDEFINITE_WAIT, &rcThread);
                if (RT_SUCCESS(rc))
                    rc = rcThread;

                if (RT_FAILURE(rc))
                    VBClLogError("Waiting on worker thread to stop failed, rc=%Rrc\n", rc);

                if (g_Service.pDesc->pfnTerm)
                {
                    VBClLogInfo("Terminating service\n");

                    int rc2 = g_Service.pDesc->pfnTerm();
                    if (RT_SUCCESS(rc))
                        rc = rc2;

                    if (RT_SUCCESS(rc))
                    {
                        VBClLogInfo("Service terminated\n");
                    }
                    else
                        VBClLogError("Service failed to terminate, rc=%Rrc\n", rc);
                }
            }
        }
    }

    if (RT_FAILURE(rc))
    {
        if (rc == VERR_NOT_AVAILABLE)
            VBClLogInfo("Service is not availabe, skipping\n");
        else if (rc == VERR_NOT_SUPPORTED)
            VBClLogInfo("Service is not supported on this platform, skipping\n");
        else
            VBClLogError("Service ended with error %Rrc\n", rc);
    }
    else
        VBClLogVerbose(2, "Service ended\n");

    if (fSignalHandlerInstalled)
    {
        int rc2 = vboxClientSignalHandlerUninstall();
        AssertRC(rc2);
    }

    VBClShutdown(false /*fExit*/);

    /** @todo r=andy Should we return an appropriate exit code if the service failed to init?
     *               Must be tested carefully with our init scripts first. */
    return g_fProcessReloadRequested ? VBGLR3EXITCODERELOAD : RTEXITCODE_SUCCESS;
}

