/* $Id: VBoxManageGuestCtrl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of guestcontrol command.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "VBoxManage.h"
#include "VBoxManageGuestCtrl.h"

#include <VBox/com/array.h>
#include <VBox/com/com.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/listeners.h>
#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>

#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/list.h>
#include <iprt/path.h>
#include <iprt/process.h> /* For RTProcSelf(). */
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>

#include <iprt/cpp/path.h>

#include <map>
#include <vector>

#ifdef USE_XPCOM_QUEUE
# include <sys/select.h>
# include <errno.h>
#endif

#include <signal.h>

#ifdef RT_OS_DARWIN
# include <CoreFoundation/CFRunLoop.h>
#endif

using namespace com;


/*********************************************************************************************************************************
 * Defined Constants And Macros                                                                                                  *
*********************************************************************************************************************************/

#define GCTLCMD_COMMON_OPT_USER             999 /**< The --username option number. */
#define GCTLCMD_COMMON_OPT_PASSWORD         998 /**< The --password option number. */
#define GCTLCMD_COMMON_OPT_PASSWORD_FILE    997 /**< The --password-file option number. */
#define GCTLCMD_COMMON_OPT_DOMAIN           996 /**< The --domain option number. */
/** Common option definitions. */
#define GCTLCMD_COMMON_OPTION_DEFS() \
        { "--user",                 GCTLCMD_COMMON_OPT_USER,            RTGETOPT_REQ_STRING  }, \
        { "--username",             GCTLCMD_COMMON_OPT_USER,            RTGETOPT_REQ_STRING  }, \
        { "--passwordfile",         GCTLCMD_COMMON_OPT_PASSWORD_FILE,   RTGETOPT_REQ_STRING  }, \
        { "--password",             GCTLCMD_COMMON_OPT_PASSWORD,        RTGETOPT_REQ_STRING  }, \
        { "--domain",               GCTLCMD_COMMON_OPT_DOMAIN,          RTGETOPT_REQ_STRING  }, \
        { "--quiet",                'q',                                RTGETOPT_REQ_NOTHING }, \
        { "--verbose",              'v',                                RTGETOPT_REQ_NOTHING },

/** Handles common options in the typical option parsing switch. */
#define GCTLCMD_COMMON_OPTION_CASES(a_pCtx, a_ch, a_pValueUnion) \
        case 'v': \
        case 'q': \
        case GCTLCMD_COMMON_OPT_USER: \
        case GCTLCMD_COMMON_OPT_DOMAIN: \
        case GCTLCMD_COMMON_OPT_PASSWORD: \
        case GCTLCMD_COMMON_OPT_PASSWORD_FILE: \
        { \
            RTEXITCODE rcExitCommon = gctlCtxSetOption(a_pCtx, a_ch, a_pValueUnion); \
            if (RT_UNLIKELY(rcExitCommon != RTEXITCODE_SUCCESS)) \
                return rcExitCommon; \
        } break


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set by the signal handler when current guest control
 *  action shall be aborted. */
static volatile bool g_fGuestCtrlCanceled = false;
/** Event semaphore used for wait notifications.
 *  Also being used for the listener implementations in VBoxManageGuestCtrlListener.cpp. */
       RTSEMEVENT    g_SemEventGuestCtrlCanceled = NIL_RTSEMEVENT;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Listener declarations.
 */
VBOX_LISTENER_DECLARE(GuestFileEventListenerImpl)
VBOX_LISTENER_DECLARE(GuestProcessEventListenerImpl)
VBOX_LISTENER_DECLARE(GuestSessionEventListenerImpl)
VBOX_LISTENER_DECLARE(GuestEventListenerImpl)
VBOX_LISTENER_DECLARE(GuestAdditionsRunlevelListener)

/**
 * Definition of a guestcontrol command, with handler and various flags.
 */
typedef struct GCTLCMDDEF
{
    /** The command name. */
    const char *pszName;

    /**
     * Actual command handler callback.
     *
     * @param   pCtx            Pointer to command context to use.
     */
    DECLR3CALLBACKMEMBER(RTEXITCODE, pfnHandler, (struct GCTLCMDCTX *pCtx, int argc, char **argv));

    /** The sub-command scope flags. */
    uint64_t    fSubcommandScope;
    /** Command context flags (GCTLCMDCTX_F_XXX). */
    uint32_t    fCmdCtx;
} GCTLCMD;
/** Pointer to a const guest control command definition. */
typedef GCTLCMDDEF const *PCGCTLCMDDEF;

/** @name GCTLCMDCTX_F_XXX - Command context flags.
 * @{
 */
/** No flags set. */
#define GCTLCMDCTX_F_NONE               0
/** Don't install a signal handler (CTRL+C trap). */
#define GCTLCMDCTX_F_NO_SIGNAL_HANDLER  RT_BIT(0)
/** No guest session needed. */
#define GCTLCMDCTX_F_SESSION_ANONYMOUS  RT_BIT(1)
/** @} */

/**
 * Context for handling a specific command.
 */
typedef struct GCTLCMDCTX
{
    HandlerArg *pArg;

    /** Pointer to the command definition. */
    PCGCTLCMDDEF pCmdDef;
    /** The VM name or UUID. */
    const char *pszVmNameOrUuid;

    /** Whether we've done the post option parsing init already. */
    bool fPostOptionParsingInited;
    /** Whether we've locked the VM session. */
    bool fLockedVmSession;
    /** Whether to detach (@c true) or close the session. */
    bool fDetachGuestSession;
    /** Set if we've installed the signal handler.   */
    bool fInstalledSignalHandler;
    /** The verbosity level. */
    uint32_t cVerbose;
    /** User name. */
    Utf8Str strUsername;
    /** Password. */
    Utf8Str strPassword;
    /** Domain. */
    Utf8Str strDomain;
    /** Pointer to the IGuest interface. */
    ComPtr<IGuest> pGuest;
    /** Pointer to the to be used guest session. */
    ComPtr<IGuestSession> pGuestSession;
    /** The guest session ID. */
    ULONG uSessionID;

} GCTLCMDCTX, *PGCTLCMDCTX;


/**
 * An entry for an element which needs to be copied/created to/on the guest.
 */
typedef struct DESTFILEENTRY
{
    DESTFILEENTRY(Utf8Str strFilename) : mFilename(strFilename) {}
    Utf8Str mFilename;
} DESTFILEENTRY, *PDESTFILEENTRY;
/*
 * Map for holding destination entries, whereas the key is the destination
 * directory and the mapped value is a vector holding all elements for this directory.
 */
typedef std::map< Utf8Str, std::vector<DESTFILEENTRY> > DESTDIRMAP, *PDESTDIRMAP;
typedef std::map< Utf8Str, std::vector<DESTFILEENTRY> >::iterator DESTDIRMAPITER, *PDESTDIRMAPITER;


enum kStreamTransform
{
    kStreamTransform_None = 0,
    kStreamTransform_Dos2Unix,
    kStreamTransform_Unix2Dos
};


DECLARE_TRANSLATION_CONTEXT(GuestCtrl);


#ifdef RT_OS_WINDOWS
static BOOL WINAPI gctlSignalHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        /* User pressed CTRL+C or CTRL+BREAK or an external event was sent
         * via GenerateConsoleCtrlEvent(). */
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
            ASMAtomicWriteBool(&g_fGuestCtrlCanceled, true);
            RTSemEventSignal(g_SemEventGuestCtrlCanceled);
            fEventHandled = TRUE;
            break;
        default:
            break;
        /** @todo Add other events here. */
    }

    return fEventHandled;
}
#else /* !RT_OS_WINDOWS */
/**
 * Signal handler that sets g_fGuestCtrlCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Don't do anything
 * unnecessary here.
 */
static void gctlSignalHandler(int iSignal) RT_NOTHROW_DEF
{
    RT_NOREF(iSignal);
    ASMAtomicWriteBool(&g_fGuestCtrlCanceled, true);
    RTSemEventSignal(g_SemEventGuestCtrlCanceled);
}
#endif


/**
 * Installs a custom signal handler to get notified
 * whenever the user wants to intercept the program.
 *
 * @todo Make this handler available for all VBoxManage modules?
 */
static int gctlSignalHandlerInstall(void)
{
    g_fGuestCtrlCanceled = false;

    int vrc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)gctlSignalHandler, TRUE /* Add handler */))
    {
        vrc = RTErrConvertFromWin32(GetLastError());
        RTMsgError(GuestCtrl::tr("Unable to install console control handler, vrc=%Rrc\n"), vrc);
    }
#else
    signal(SIGINT,   gctlSignalHandler);
    signal(SIGTERM,  gctlSignalHandler);
# ifdef SIGBREAK
    signal(SIGBREAK, gctlSignalHandler);
# endif
#endif

    if (RT_SUCCESS(vrc))
        vrc = RTSemEventCreate(&g_SemEventGuestCtrlCanceled);

    return vrc;
}


/**
 * Uninstalls a previously installed signal handler.
 */
static int gctlSignalHandlerUninstall(void)
{
    int vrc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)NULL, FALSE /* Remove handler */))
    {
        vrc = RTErrConvertFromWin32(GetLastError());
        RTMsgError(GuestCtrl::tr("Unable to uninstall console control handler, vrc=%Rrc\n"), vrc);
    }
#else
    signal(SIGINT,   SIG_DFL);
    signal(SIGTERM,  SIG_DFL);
# ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
# endif
#endif

    if (g_SemEventGuestCtrlCanceled != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(g_SemEventGuestCtrlCanceled);
        g_SemEventGuestCtrlCanceled = NIL_RTSEMEVENT;
    }
    return vrc;
}


/**
 * Translates a process status to a human readable string.
 *
 * @sa GuestProcess::i_statusToString()
 */
const char *gctlProcessStatusToText(ProcessStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case ProcessStatus_Starting:
            return GuestCtrl::tr("starting");
        case ProcessStatus_Started:
            return GuestCtrl::tr("started");
        case ProcessStatus_Paused:
            return GuestCtrl::tr("paused");
        case ProcessStatus_Terminating:
            return GuestCtrl::tr("terminating");
        case ProcessStatus_TerminatedNormally:
            return GuestCtrl::tr("successfully terminated");
        case ProcessStatus_TerminatedSignal:
            return GuestCtrl::tr("terminated by signal");
        case ProcessStatus_TerminatedAbnormally:
            return GuestCtrl::tr("abnormally aborted");
        case ProcessStatus_TimedOutKilled:
            return GuestCtrl::tr("timed out");
        case ProcessStatus_TimedOutAbnormally:
            return GuestCtrl::tr("timed out, hanging");
        case ProcessStatus_Down:
            return GuestCtrl::tr("killed");
        case ProcessStatus_Error:
            return GuestCtrl::tr("error");
        default:
            break;
    }
    return GuestCtrl::tr("unknown");
}

/**
 * Translates a guest process wait result to a human readable string.
 */
static const char *gctlProcessWaitResultToText(ProcessWaitResult_T enmWaitResult)
{
    switch (enmWaitResult)
    {
        case ProcessWaitResult_Start:
            return GuestCtrl::tr("started");
        case ProcessWaitResult_Terminate:
            return GuestCtrl::tr("terminated");
        case ProcessWaitResult_Status:
            return GuestCtrl::tr("status changed");
        case ProcessWaitResult_Error:
            return GuestCtrl::tr("error");
        case ProcessWaitResult_Timeout:
            return GuestCtrl::tr("timed out");
        case ProcessWaitResult_StdIn:
            return GuestCtrl::tr("stdin ready");
        case ProcessWaitResult_StdOut:
            return GuestCtrl::tr("data on stdout");
        case ProcessWaitResult_StdErr:
            return GuestCtrl::tr("data on stderr");
        case ProcessWaitResult_WaitFlagNotSupported:
            return GuestCtrl::tr("waiting flag not supported");
        default:
            break;
    }
    return GuestCtrl::tr("unknown");
}

/**
 * Translates a guest session status to a human readable string.
 */
const char *gctlGuestSessionStatusToText(GuestSessionStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case GuestSessionStatus_Starting:
            return GuestCtrl::tr("starting");
        case GuestSessionStatus_Started:
            return GuestCtrl::tr("started");
        case GuestSessionStatus_Terminating:
            return GuestCtrl::tr("terminating");
        case GuestSessionStatus_Terminated:
            return GuestCtrl::tr("terminated");
        case GuestSessionStatus_TimedOutKilled:
            return GuestCtrl::tr("timed out");
        case GuestSessionStatus_TimedOutAbnormally:
            return GuestCtrl::tr("timed out, hanging");
        case GuestSessionStatus_Down:
            return GuestCtrl::tr("killed");
        case GuestSessionStatus_Error:
            return GuestCtrl::tr("error");
        default:
            break;
    }
    return GuestCtrl::tr("unknown");
}

/**
 * Translates a guest file status to a human readable string.
 */
const char *gctlFileStatusToText(FileStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case FileStatus_Opening:
            return GuestCtrl::tr("opening");
        case FileStatus_Open:
            return GuestCtrl::tr("open");
        case FileStatus_Closing:
            return GuestCtrl::tr("closing");
        case FileStatus_Closed:
            return GuestCtrl::tr("closed");
        case FileStatus_Down:
            return GuestCtrl::tr("killed");
        case FileStatus_Error:
            return GuestCtrl::tr("error");
        default:
            break;
    }
    return GuestCtrl::tr("unknown");
}

/**
 * Translates a file system objec type to a string.
 */
static const char *gctlFsObjTypeToName(FsObjType_T enmType)
{
    switch (enmType)
    {
        case FsObjType_Unknown:     return GuestCtrl::tr("unknown");
        case FsObjType_Fifo:        return GuestCtrl::tr("fifo");
        case FsObjType_DevChar:     return GuestCtrl::tr("char-device");
        case FsObjType_Directory:   return GuestCtrl::tr("directory");
        case FsObjType_DevBlock:    return GuestCtrl::tr("block-device");
        case FsObjType_File:        return GuestCtrl::tr("file");
        case FsObjType_Symlink:     return GuestCtrl::tr("symlink");
        case FsObjType_Socket:      return GuestCtrl::tr("socket");
        case FsObjType_WhiteOut:    return GuestCtrl::tr("white-out");
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case FsObjType_32BitHack: break;
#endif
    }
    return GuestCtrl::tr("unknown");
}

static int gctlPrintError(com::ErrorInfo &errorInfo)
{
    if (   errorInfo.isFullAvailable()
        || errorInfo.isBasicAvailable())
    {
        /* If we got a VBOX_E_IPRT error we handle the error in a more gentle way
         * because it contains more accurate info about what went wrong. */
        if (errorInfo.getResultCode() == VBOX_E_IPRT_ERROR)
            RTMsgError("%ls.", errorInfo.getText().raw());
        else
        {
            RTMsgError(GuestCtrl::tr("Error details:"));
            GluePrintErrorInfo(errorInfo);
        }
        return VERR_GENERAL_FAILURE; /** @todo */
    }
    AssertMsgFailedReturn((GuestCtrl::tr("Object has indicated no error (%Rhrc)!?\n"), errorInfo.getResultCode()),
                          VERR_INVALID_PARAMETER);
}

static int gctlPrintError(IUnknown *pObj, const GUID &aIID)
{
    com::ErrorInfo ErrInfo(pObj, aIID);
    return gctlPrintError(ErrInfo);
}

static int gctlPrintProgressError(ComPtr<IProgress> pProgress)
{
    int vrc = VINF_SUCCESS;
    HRESULT hrc;

    do
    {
        BOOL fCanceled;
        CHECK_ERROR_BREAK(pProgress, COMGETTER(Canceled)(&fCanceled));
        if (!fCanceled)
        {
            LONG rcProc;
            CHECK_ERROR_BREAK(pProgress, COMGETTER(ResultCode)(&rcProc));
            if (FAILED(rcProc))
            {
                com::ProgressErrorInfo ErrInfo(pProgress);
                vrc = gctlPrintError(ErrInfo);
            }
        }

    } while(0);

    AssertMsgStmt(SUCCEEDED(hrc), (GuestCtrl::tr("Could not lookup progress information\n")), vrc = VERR_COM_UNEXPECTED);

    return vrc;
}



/*
 *
 *
 * Guest Control Command Context
 * Guest Control Command Context
 * Guest Control Command Context
 * Guest Control Command Context
 *
 *
 *
 */


/**
 * Initializes a guest control command context structure.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE on failure (after
 *           informing the user of course).
 * @param   pCtx                The command context to init.
 * @param   pArg                The handle argument package.
 */
static RTEXITCODE gctrCmdCtxInit(PGCTLCMDCTX pCtx, HandlerArg *pArg)
{
    pCtx->pArg                      = pArg;
    pCtx->pCmdDef                   = NULL;
    pCtx->pszVmNameOrUuid           = NULL;
    pCtx->fPostOptionParsingInited  = false;
    pCtx->fLockedVmSession          = false;
    pCtx->fDetachGuestSession       = false;
    pCtx->fInstalledSignalHandler   = false;
    pCtx->cVerbose                  = 0;
    pCtx->strUsername.setNull();
    pCtx->strPassword.setNull();
    pCtx->strDomain.setNull();
    pCtx->pGuest.setNull();
    pCtx->pGuestSession.setNull();
    pCtx->uSessionID                = 0;

    /*
     * The user name defaults to the host one, if we can get at it.
     */
    char szUser[1024];
    int vrc = RTProcQueryUsername(RTProcSelf(), szUser, sizeof(szUser), NULL);
    if (   RT_SUCCESS(vrc)
        && RTStrIsValidEncoding(szUser)) /* paranoia was required on posix at some point, not needed any more! */
    {
        try
        {
            pCtx->strUsername = szUser;
        }
        catch (std::bad_alloc &)
        {
            return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory"));
        }
    }
    /* else: ignore this failure. */

    return RTEXITCODE_SUCCESS;
}


/**
 * Worker for GCTLCMD_COMMON_OPTION_CASES.
 *
 * @returns RTEXITCODE_SUCCESS if the option was handled successfully.  If not,
 *          an error message is printed and an appropriate failure exit code is
 *          returned.
 * @param   pCtx                The guest control command context.
 * @param   ch                  The option char or ordinal.
 * @param   pValueUnion         The option value union.
 */
static RTEXITCODE gctlCtxSetOption(PGCTLCMDCTX pCtx, int ch, PRTGETOPTUNION pValueUnion)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    switch (ch)
    {
        case GCTLCMD_COMMON_OPT_USER: /* User name */
            if (!pCtx->pCmdDef || !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS))
                pCtx->strUsername = pValueUnion->psz;
            else
                RTMsgWarning(GuestCtrl::tr("The --username|-u option is ignored by '%s'"), pCtx->pCmdDef->pszName);
            break;

        case GCTLCMD_COMMON_OPT_PASSWORD: /* Password */
            if (!pCtx->pCmdDef || !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS))
            {
                if (pCtx->strPassword.isNotEmpty())
                    RTMsgWarning(GuestCtrl::tr("Password is given more than once."));
                pCtx->strPassword = pValueUnion->psz;
            }
            else
                RTMsgWarning(GuestCtrl::tr("The --password option is ignored by '%s'"), pCtx->pCmdDef->pszName);
            break;

        case GCTLCMD_COMMON_OPT_PASSWORD_FILE: /* Password file */
            if (!pCtx->pCmdDef || !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS))
                rcExit = readPasswordFile(pValueUnion->psz, &pCtx->strPassword);
            else
                RTMsgWarning(GuestCtrl::tr("The --password-file|-p option is ignored by '%s'"), pCtx->pCmdDef->pszName);
            break;

        case GCTLCMD_COMMON_OPT_DOMAIN: /* domain */
            if (!pCtx->pCmdDef || !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS))
                pCtx->strDomain = pValueUnion->psz;
            else
                RTMsgWarning(GuestCtrl::tr("The --domain option is ignored by '%s'"), pCtx->pCmdDef->pszName);
            break;

        case 'v': /* --verbose */
            pCtx->cVerbose++;
            break;

        case 'q': /* --quiet */
            if (pCtx->cVerbose)
                pCtx->cVerbose--;
            break;

        default:
            AssertFatalMsgFailed(("ch=%d (%c)\n", ch, ch));
    }
    return rcExit;
}


/**
 * Initializes the VM for IGuest operation.
 *
 * This opens a shared session to a running VM and gets hold of IGuest.
 *
 * @returns RTEXITCODE_SUCCESS on success.  RTEXITCODE_FAILURE and user message
 *          on failure.
 * @param   pCtx            The guest control command context.
 *                          GCTLCMDCTX::pGuest will be set on success.
 */
static RTEXITCODE gctlCtxInitVmSession(PGCTLCMDCTX pCtx)
{
    HRESULT hrc;
    AssertPtr(pCtx);
    AssertPtr(pCtx->pArg);

    /*
     * Find the VM and check if it's running.
     */
    ComPtr<IMachine> machine;
    CHECK_ERROR(pCtx->pArg->virtualBox, FindMachine(Bstr(pCtx->pszVmNameOrUuid).raw(), machine.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        MachineState_T enmMachineState;
        CHECK_ERROR(machine, COMGETTER(State)(&enmMachineState));
        if (   SUCCEEDED(hrc)
            && enmMachineState == MachineState_Running)
        {
            /*
             * It's running. So, open a session to it and get the IGuest interface.
             */
            CHECK_ERROR(machine, LockMachine(pCtx->pArg->session, LockType_Shared));
            if (SUCCEEDED(hrc))
            {
                pCtx->fLockedVmSession = true;
                ComPtr<IConsole> ptrConsole;
                CHECK_ERROR(pCtx->pArg->session, COMGETTER(Console)(ptrConsole.asOutParam()));
                if (SUCCEEDED(hrc))
                {
                    if (ptrConsole.isNotNull())
                    {
                        CHECK_ERROR(ptrConsole, COMGETTER(Guest)(pCtx->pGuest.asOutParam()));
                        if (SUCCEEDED(hrc))
                            return RTEXITCODE_SUCCESS;
                    }
                    else
                        RTMsgError(GuestCtrl::tr("Failed to get a IConsole pointer for the machine. Is it still running?\n"));
                }
            }
        }
        else if (SUCCEEDED(hrc))
            RTMsgError(GuestCtrl::tr("Machine \"%s\" is not running (currently %s)!\n"),
                       pCtx->pszVmNameOrUuid, machineStateToName(enmMachineState, false));
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Creates a guest session with the VM.
 *
 * @retval  RTEXITCODE_SUCCESS on success.
 * @retval  RTEXITCODE_FAILURE and user message on failure.
 * @param   pCtx            The guest control command context.
 *                          GCTCMDCTX::pGuestSession and GCTLCMDCTX::uSessionID
 *                          will be set.
 */
static RTEXITCODE gctlCtxInitGuestSession(PGCTLCMDCTX pCtx)
{
    HRESULT hrc;
    AssertPtr(pCtx);
    Assert(!(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS));
    Assert(pCtx->pGuest.isNotNull());

    /*
     * Build up a reasonable guest session name. Useful for identifying
     * a specific session when listing / searching for them.
     */
    char *pszSessionName;
    if (RTStrAPrintf(&pszSessionName,
                     GuestCtrl::tr("[%RU32] VBoxManage Guest Control [%s] - %s"),
                     RTProcSelf(), pCtx->pszVmNameOrUuid, pCtx->pCmdDef->pszName) < 0)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("No enough memory for session name"));

    /*
     * Create a guest session.
     */
    if (pCtx->cVerbose)
        RTPrintf(GuestCtrl::tr("Creating guest session as user '%s'...\n"), pCtx->strUsername.c_str());
    try
    {
        CHECK_ERROR(pCtx->pGuest, CreateSession(Bstr(pCtx->strUsername).raw(),
                                                Bstr(pCtx->strPassword).raw(),
                                                Bstr(pCtx->strDomain).raw(),
                                                Bstr(pszSessionName).raw(),
                                                pCtx->pGuestSession.asOutParam()));
    }
    catch (std::bad_alloc &)
    {
        RTMsgError(GuestCtrl::tr("Out of memory setting up IGuest::CreateSession call"));
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        /*
         * Wait for guest session to start.
         */
        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Waiting for guest session to start...\n"));
        GuestSessionWaitResult_T enmWaitResult = GuestSessionWaitResult_None; /* Shut up MSC */
        try
        {
            com::SafeArray<GuestSessionWaitForFlag_T> aSessionWaitFlags;
            aSessionWaitFlags.push_back(GuestSessionWaitForFlag_Start);
            CHECK_ERROR(pCtx->pGuestSession, WaitForArray(ComSafeArrayAsInParam(aSessionWaitFlags),
                                                          /** @todo Make session handling timeouts configurable. */
                                                          30 * 1000, &enmWaitResult));
        }
        catch (std::bad_alloc &)
        {
            RTMsgError(GuestCtrl::tr("Out of memory setting up IGuestSession::WaitForArray call"));
            hrc = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hrc))
        {
            /* The WaitFlagNotSupported result may happen with GAs older than 4.3. */
            if (   enmWaitResult == GuestSessionWaitResult_Start
                || enmWaitResult == GuestSessionWaitResult_WaitFlagNotSupported)
            {
                /*
                 * Get the session ID and we're ready to rumble.
                 */
                CHECK_ERROR(pCtx->pGuestSession, COMGETTER(Id)(&pCtx->uSessionID));
                if (SUCCEEDED(hrc))
                {
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Successfully started guest session (ID %RU32)\n"), pCtx->uSessionID);
                    RTStrFree(pszSessionName);
                    return RTEXITCODE_SUCCESS;
                }
            }
            else
            {
                GuestSessionStatus_T enmSessionStatus;
                CHECK_ERROR(pCtx->pGuestSession, COMGETTER(Status)(&enmSessionStatus));
                RTMsgError(GuestCtrl::tr("Error starting guest session (current status is: %s)\n"),
                           SUCCEEDED(hrc) ? gctlGuestSessionStatusToText(enmSessionStatus) : GuestCtrl::tr("<unknown>"));
            }
        }
    }

    RTStrFree(pszSessionName);
    return RTEXITCODE_FAILURE;
}


/**
 * Completes the guest control context initialization after parsing arguments.
 *
 * Will validate common arguments, open a VM session, and if requested open a
 * guest session and install the CTRL-C signal handler.
 *
 * It is good to validate all the options and arguments you can before making
 * this call.  However, the VM session, IGuest and IGuestSession interfaces are
 * not availabe till after this call, so take care.
 *
 * @retval  RTEXITCODE_SUCCESS on success.
 * @retval  RTEXITCODE_FAILURE and user message on failure.
 * @param   pCtx            The guest control command context.
 *                          GCTCMDCTX::pGuestSession and GCTLCMDCTX::uSessionID
 *                          will be set.
 * @remarks Can safely be called multiple times, will only do work once.
 */
static RTEXITCODE gctlCtxPostOptionParsingInit(PGCTLCMDCTX pCtx)
{
    if (pCtx->fPostOptionParsingInited)
        return RTEXITCODE_SUCCESS;

    /*
     * Check that the user name isn't empty when we need it.
     */
    RTEXITCODE rcExit;
    if (  (pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS)
        || pCtx->strUsername.isNotEmpty())
    {
        /*
         * Open the VM session and if required, a guest session.
         */
        rcExit = gctlCtxInitVmSession(pCtx);
        if (   rcExit == RTEXITCODE_SUCCESS
            && !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS))
            rcExit = gctlCtxInitGuestSession(pCtx);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Install signal handler if requested (errors are ignored).
             */
            if (!(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_NO_SIGNAL_HANDLER))
            {
                int vrc = gctlSignalHandlerInstall();
                pCtx->fInstalledSignalHandler = RT_SUCCESS(vrc);
            }
        }
    }
    else
        rcExit = errorSyntax(GuestCtrl::tr("No user name specified!"));

    pCtx->fPostOptionParsingInited = rcExit == RTEXITCODE_SUCCESS;
    return rcExit;
}


/**
 * Cleans up the context when the command returns.
 *
 * This will close any open guest session, unless the DETACH flag is set.
 * It will also close any VM session that may be been established.  Any signal
 * handlers we've installed will also be removed.
 *
 * Un-initializes the VM after guest control usage.
 * @param   pCmdCtx                 Pointer to command context.
 */
static void gctlCtxTerm(PGCTLCMDCTX pCtx)
{
    HRESULT hrc;
    AssertPtr(pCtx);

    /*
     * Uninstall signal handler.
     */
    if (pCtx->fInstalledSignalHandler)
    {
        gctlSignalHandlerUninstall();
        pCtx->fInstalledSignalHandler = false;
    }

    /*
     * Close, or at least release, the guest session.
     */
    if (pCtx->pGuestSession.isNotNull())
    {
        if (   !(pCtx->pCmdDef->fCmdCtx & GCTLCMDCTX_F_SESSION_ANONYMOUS)
            && !pCtx->fDetachGuestSession)
        {
            if (pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Closing guest session ...\n"));

            if (pCtx->pGuestSession.isNotNull())
                CHECK_ERROR(pCtx->pGuestSession, Close());

            if (pCtx->cVerbose > 4)
            {
                SafeIfaceArray <IGuestSession> collSessions;
                CHECK_ERROR(pCtx->pGuest, COMGETTER(Sessions)(ComSafeArrayAsOutParam(collSessions)));
                RTPrintf(GuestCtrl::tr("Now %zu guest sessions registered\n"), collSessions.size());
            }
        }
        else if (   pCtx->fDetachGuestSession
                 && pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Guest session detached\n"));

        pCtx->pGuestSession.setNull();
    }

    /*
     * Close the VM session.
     */
    if (pCtx->fLockedVmSession)
    {
        Assert(pCtx->pArg->session.isNotNull());
        CHECK_ERROR(pCtx->pArg->session, UnlockMachine());
        pCtx->fLockedVmSession = false;
    }
}





/*
 *
 *
 * Guest Control Command Handling.
 * Guest Control Command Handling.
 * Guest Control Command Handling.
 * Guest Control Command Handling.
 * Guest Control Command Handling.
 *
 *
 */


/** @name EXITCODEEXEC_XXX - Special run exit codes.
 *
 * Special exit codes for returning errors/information of a started guest
 * process to the command line VBoxManage was started from.  Useful for e.g.
 * scripting.
 *
 * ASSUMING that all platforms have at least 7-bits for the exit code we can do
 * the following mapping:
 *  - Guest exit code 0 is mapped to 0 on the host.
 *  - Guest exit codes 1 thru 93 (0x5d) are displaced by 32, so that 1
 *    becomes 33 (0x21) on the host and 93 becomes 125 (0x7d) on the host.
 *  - Guest exit codes 94 (0x5e) and above are mapped to 126 (0x5e).
 *
 * We ASSUME that all VBoxManage status codes are in the range 0 thru 32.
 *
 * @note    These are frozen as of 4.1.0.
 * @note    The guest exit code mappings was introduced with 5.0 and the 'run'
 *          command, they are/was not supported by 'exec'.
 * @sa      gctlRunCalculateExitCode
 */
/** Process exited normally but with an exit code <> 0. */
#define EXITCODEEXEC_CODE           ((RTEXITCODE)16)
#define EXITCODEEXEC_FAILED         ((RTEXITCODE)17)
#define EXITCODEEXEC_TERM_SIGNAL    ((RTEXITCODE)18)
#define EXITCODEEXEC_TERM_ABEND     ((RTEXITCODE)19)
#define EXITCODEEXEC_TIMEOUT        ((RTEXITCODE)20)
#define EXITCODEEXEC_DOWN           ((RTEXITCODE)21)
/** Execution was interrupt by user (ctrl-c). */
#define EXITCODEEXEC_CANCELED       ((RTEXITCODE)22)
/** The first mapped guest (non-zero) exit code. */
#define EXITCODEEXEC_MAPPED_FIRST           33
/** The last mapped guest (non-zero) exit code value (inclusive). */
#define EXITCODEEXEC_MAPPED_LAST            125
/** The number of exit codes from EXITCODEEXEC_MAPPED_FIRST to
 * EXITCODEEXEC_MAPPED_LAST.  This is also the highest guest exit code number
 * we're able to map. */
#define EXITCODEEXEC_MAPPED_RANGE           (93)
/** The guest exit code displacement value. */
#define EXITCODEEXEC_MAPPED_DISPLACEMENT    32
/** The guest exit code was too big to be mapped. */
#define EXITCODEEXEC_MAPPED_BIG             ((RTEXITCODE)126)
/** @} */

/**
 * Calculates the exit code of VBoxManage.
 *
 * @returns The exit code to return.
 * @param   enmStatus           The guest process status.
 * @param   uExitCode           The associated guest process exit code (where
 *                              applicable).
 * @param   fReturnExitCodes    Set if we're to use the 32-126 range for guest
 *                              exit codes.
 */
static RTEXITCODE gctlRunCalculateExitCode(ProcessStatus_T enmStatus, ULONG uExitCode, bool fReturnExitCodes)
{
    switch (enmStatus)
    {
        case ProcessStatus_TerminatedNormally:
            if (uExitCode == 0)
                return RTEXITCODE_SUCCESS;
            if (!fReturnExitCodes)
                return EXITCODEEXEC_CODE;
            if (uExitCode <= EXITCODEEXEC_MAPPED_RANGE)
                return (RTEXITCODE) (uExitCode + EXITCODEEXEC_MAPPED_DISPLACEMENT);
            return EXITCODEEXEC_MAPPED_BIG;

        case ProcessStatus_TerminatedAbnormally:
            return EXITCODEEXEC_TERM_ABEND;
        case ProcessStatus_TerminatedSignal:
            return EXITCODEEXEC_TERM_SIGNAL;

#if 0  /* see caller! */
        case ProcessStatus_TimedOutKilled:
            return EXITCODEEXEC_TIMEOUT;
        case ProcessStatus_Down:
            return EXITCODEEXEC_DOWN;   /* Service/OS is stopping, process was killed. */
        case ProcessStatus_Error:
            return EXITCODEEXEC_FAILED;

        /* The following is probably for detached? */
        case ProcessStatus_Starting:
            return RTEXITCODE_SUCCESS;
        case ProcessStatus_Started:
            return RTEXITCODE_SUCCESS;
        case ProcessStatus_Paused:
            return RTEXITCODE_SUCCESS;
        case ProcessStatus_Terminating:
            return RTEXITCODE_SUCCESS; /** @todo ???? */
#endif

        default:
            AssertMsgFailed(("Unknown exit status (%u/%u) from guest process returned!\n", enmStatus, uExitCode));
            return RTEXITCODE_FAILURE;
    }
}


/**
 * Pumps guest output to the host.
 *
 * @return  IPRT status code.
 * @param   pProcess        Pointer to appropriate process object.
 * @param   hVfsIosDst      Where to write the data. Can be the bit bucket or a (valid [std]) handle.
 * @param   uHandle         Handle where to read the data from.
 * @param   cMsTimeout      Timeout (in ms) to wait for the operation to
 *                          complete.
 */
static int gctlRunPumpOutput(IProcess *pProcess, RTVFSIOSTREAM hVfsIosDst, ULONG uHandle, RTMSINTERVAL cMsTimeout)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    Assert(hVfsIosDst != NIL_RTVFSIOSTREAM);

    int vrc;

    SafeArray<BYTE> aOutputData;
    HRESULT hrc = pProcess->Read(uHandle, _64K, RT_MAX(cMsTimeout, 1), ComSafeArrayAsOutParam(aOutputData));
    if (SUCCEEDED(hrc))
    {
        size_t cbOutputData = aOutputData.size();
        if (cbOutputData == 0)
            vrc = VINF_SUCCESS;
        else
        {
            BYTE const *pbBuf = aOutputData.raw();
            AssertPtr(pbBuf);

            vrc = RTVfsIoStrmWrite(hVfsIosDst, pbBuf, cbOutputData, true /*fBlocking*/,  NULL);
            if (RT_FAILURE(vrc))
                RTMsgError(GuestCtrl::tr("Unable to write output, vrc=%Rrc\n"), vrc);
        }
    }
    else
        vrc = gctlPrintError(pProcess, COM_IIDOF(IProcess));
    return vrc;
}


/**
 * Configures a host handle for pumping guest bits.
 *
 * @returns true if enabled and we successfully configured it.
 * @param   fEnabled            Whether pumping this pipe is configured to std handles,
 *                              or going to the bit bucket instead.
 * @param   enmHandle           The IPRT standard handle designation.
 * @param   pszName             The name for user messages.
 * @param   enmTransformation   The transformation to apply.
 * @param   phVfsIos            Where to return the resulting I/O stream handle.
 */
static bool gctlRunSetupHandle(bool fEnabled, RTHANDLESTD enmHandle, const char *pszName,
                               kStreamTransform enmTransformation, PRTVFSIOSTREAM phVfsIos)
{
    if (fEnabled)
    {
        int vrc = RTVfsIoStrmFromStdHandle(enmHandle, 0, true /*fLeaveOpen*/, phVfsIos);
        if (RT_SUCCESS(vrc))
        {
            if (enmTransformation != kStreamTransform_None)
            {
                RTMsgWarning(GuestCtrl::tr("Unsupported %s line ending conversion"), pszName);
                /** @todo Implement dos2unix and unix2dos stream filters. */
            }
            return true;
        }
        RTMsgWarning(GuestCtrl::tr("Error getting %s handle: %Rrc"), pszName, vrc);
    }
    else /* If disabled, all goes to / gets fed to/from the bit bucket. */
    {
        RTFILE hFile;
        int vrc = RTFileOpenBitBucket(&hFile, enmHandle == RTHANDLESTD_INPUT ? RTFILE_O_READ : RTFILE_O_WRITE);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTVfsIoStrmFromRTFile(hFile, 0 /* fOpen */, false /* fLeaveOpen */, phVfsIos);
            if (RT_SUCCESS(vrc))
                return true;
        }
    }

    return false;
}


/**
 * Returns the remaining time (in ms) based on the start time and a set
 * timeout value. Returns RT_INDEFINITE_WAIT if no timeout was specified.
 *
 * @return  RTMSINTERVAL    Time left (in ms).
 * @param   u64StartMs      Start time (in ms).
 * @param   cMsTimeout      Timeout value (in ms).
 */
static RTMSINTERVAL gctlRunGetRemainingTime(uint64_t u64StartMs, RTMSINTERVAL cMsTimeout)
{
    if (!cMsTimeout || cMsTimeout == RT_INDEFINITE_WAIT) /* If no timeout specified, wait forever. */
        return RT_INDEFINITE_WAIT;

    uint64_t u64ElapsedMs = RTTimeMilliTS() - u64StartMs;
    if (u64ElapsedMs >= cMsTimeout)
        return 0;

    return cMsTimeout - (RTMSINTERVAL)u64ElapsedMs;
}

/**
 * Common handler for the 'run' and 'start' commands.
 *
 * @returns Command exit code.
 * @param   pCtx        Guest session context.
 * @param   argc        The argument count.
 * @param   argv        The argument vector for this command.
 * @param   fRunCmd     Set if it's 'run' clear if 'start'.
 */
static RTEXITCODE gctlHandleRunCommon(PGCTLCMDCTX pCtx, int argc, char **argv, bool fRunCmd)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /*
     * Parse arguments.
     */
    enum kGstCtrlRunOpt
    {
        kGstCtrlRunOpt_IgnoreOrphanedProcesses = 1000,
        kGstCtrlRunOpt_NoProfile, /** @todo Deprecated and will be removed soon; use kGstCtrlRunOpt_Profile instead, if needed. */
        kGstCtrlRunOpt_Profile,
        kGstCtrlRunOpt_Dos2Unix,
        kGstCtrlRunOpt_Unix2Dos,
        kGstCtrlRunOpt_WaitForStdOut,
        kGstCtrlRunOpt_NoWaitForStdOut,
        kGstCtrlRunOpt_WaitForStdErr,
        kGstCtrlRunOpt_NoWaitForStdErr
    };
    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--arg0",                         '0',                                      RTGETOPT_REQ_STRING  },
        { "--cwd",                          'C',                                      RTGETOPT_REQ_STRING  },
        { "--putenv",                       'E',                                      RTGETOPT_REQ_STRING  },
        { "--exe",                          'e',                                      RTGETOPT_REQ_STRING  },
        { "--timeout",                      't',                                      RTGETOPT_REQ_UINT32  },
        { "--unquoted-args",                'u',                                      RTGETOPT_REQ_NOTHING },
        { "--ignore-orphaned-processes",    kGstCtrlRunOpt_IgnoreOrphanedProcesses,   RTGETOPT_REQ_NOTHING },
        { "--no-profile",                   kGstCtrlRunOpt_NoProfile,                 RTGETOPT_REQ_NOTHING }, /** @todo Deprecated. */
        { "--profile",                      kGstCtrlRunOpt_Profile,                   RTGETOPT_REQ_NOTHING },
        /* run only: 6 - options */
        { "--dos2unix",                     kGstCtrlRunOpt_Dos2Unix,                  RTGETOPT_REQ_NOTHING },
        { "--unix2dos",                     kGstCtrlRunOpt_Unix2Dos,                  RTGETOPT_REQ_NOTHING },
        { "--no-wait-stdout",               kGstCtrlRunOpt_NoWaitForStdOut,           RTGETOPT_REQ_NOTHING },
        { "--wait-stdout",                  kGstCtrlRunOpt_WaitForStdOut,             RTGETOPT_REQ_NOTHING },
        { "--no-wait-stderr",               kGstCtrlRunOpt_NoWaitForStdErr,           RTGETOPT_REQ_NOTHING },
        { "--wait-stderr",                  kGstCtrlRunOpt_WaitForStdErr,             RTGETOPT_REQ_NOTHING },
    };

    /** @todo stdin handling.   */

    int                     ch;
    RTGETOPTUNION           ValueUnion;
    RTGETOPTSTATE           GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions) - (fRunCmd ? 0 : 6),
                           1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRC(vrc);

    com::SafeArray<ProcessCreateFlag_T>     aCreateFlags;
    com::SafeArray<ProcessWaitForFlag_T>    aWaitFlags;
    com::SafeArray<IN_BSTR>                 aArgs;
    com::SafeArray<IN_BSTR>                 aEnv;
    const char *                            pszImage            = NULL;
    const char *                            pszArg0             = NULL; /* Argument 0 to use. pszImage will be used if not specified. */
    const char *                            pszCwd              = NULL;
    bool                                    fWaitForStdOut      = fRunCmd;
    bool                                    fWaitForStdErr      = fRunCmd;
    RTVFSIOSTREAM                           hVfsStdOut          = NIL_RTVFSIOSTREAM;
    RTVFSIOSTREAM                           hVfsStdErr          = NIL_RTVFSIOSTREAM;
    enum kStreamTransform                   enmStdOutTransform  = kStreamTransform_None;
    enum kStreamTransform                   enmStdErrTransform  = kStreamTransform_None;
    RTMSINTERVAL                            cMsTimeout          = 0;

    try
    {
        /* Wait for process start in any case. This is useful for scripting VBoxManage
         * when relying on its overall exit code. */
        aWaitFlags.push_back(ProcessWaitForFlag_Start);

        while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
        {
            /* For options that require an argument, ValueUnion has received the value. */
            switch (ch)
            {
                GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

                case 'E':
                    if (   ValueUnion.psz[0] == '\0'
                        || ValueUnion.psz[0] == '=')
                        return errorSyntax(GuestCtrl::tr("Invalid argument variable[=value]: '%s'"), ValueUnion.psz);
                    aEnv.push_back(Bstr(ValueUnion.psz).raw());
                    break;

                case kGstCtrlRunOpt_IgnoreOrphanedProcesses:
                    aCreateFlags.push_back(ProcessCreateFlag_IgnoreOrphanedProcesses);
                    break;

                case kGstCtrlRunOpt_NoProfile:
                    /** @todo Deprecated, will be removed. */
                    RTPrintf(GuestCtrl::tr("Warning: Deprecated option \"--no-profile\" specified\n"));
                    break;

                case kGstCtrlRunOpt_Profile:
                    aCreateFlags.push_back(ProcessCreateFlag_Profile);
                    break;

                case '0':
                    pszArg0 = ValueUnion.psz;
                    break;

                case 'C':
                    pszCwd = ValueUnion.psz;
                    break;

                case 'e':
                    pszImage = ValueUnion.psz;
                    break;

                case 'u':
                    aCreateFlags.push_back(ProcessCreateFlag_UnquotedArguments);
                    break;

                /** @todo Add a hidden flag. */

                case 't': /* Timeout */
                    cMsTimeout = ValueUnion.u32;
                    break;

                /* run only options: */
                case kGstCtrlRunOpt_Dos2Unix:
                    Assert(fRunCmd);
                    enmStdErrTransform = enmStdOutTransform = kStreamTransform_Dos2Unix;
                    break;
                case kGstCtrlRunOpt_Unix2Dos:
                    Assert(fRunCmd);
                    enmStdErrTransform = enmStdOutTransform = kStreamTransform_Unix2Dos;
                    break;

                case kGstCtrlRunOpt_WaitForStdOut:
                    Assert(fRunCmd);
                    fWaitForStdOut = true;
                    break;
                case kGstCtrlRunOpt_NoWaitForStdOut:
                    Assert(fRunCmd);
                    fWaitForStdOut = false;
                    break;

                case kGstCtrlRunOpt_WaitForStdErr:
                    Assert(fRunCmd);
                    fWaitForStdErr = true;
                    break;
                case kGstCtrlRunOpt_NoWaitForStdErr:
                    Assert(fRunCmd);
                    fWaitForStdErr = false;
                    break;

                case VINF_GETOPT_NOT_OPTION:
                    /* VINF_GETOPT_NOT_OPTION comes after all options have been specified;
                     * so if pszImage still is zero at this stage, we use the first non-option found
                     * as the image being executed. */
                    if (!pszImage)
                        pszImage = ValueUnion.psz;
                    else /* Add anything else to the arguments vector. */
                        aArgs.push_back(Bstr(ValueUnion.psz).raw());
                    break;

                default:
                    return errorGetOpt(ch, &ValueUnion);

            } /* switch */
        } /* while RTGetOpt */

        /* Must have something to execute. */
        if (!pszImage || !*pszImage)
            return errorSyntax(GuestCtrl::tr("No executable specified!"));

        /* Set the arg0 argument (descending precedence):
         *   - If an argument 0 is explicitly specified (via "--arg0"), use this as argument 0.
         *   - When an image is specified explicitly (via "--exe <image>"), use <image> as argument 0.
         *     Note: This is (and ever was) the default behavior users expect, so don't change this! */
        if (pszArg0)
            aArgs.push_front(Bstr(pszArg0).raw());
        else
            aArgs.push_front(Bstr(pszImage).raw());

        if (pCtx->cVerbose) /* Print the final execution parameters in verbose mode. */
        {
            RTPrintf(GuestCtrl::tr("Executing:\n  Image : %s\n"), pszImage);
            for (size_t i = 0; i < aArgs.size(); i++)
                RTPrintf(GuestCtrl::tr("  arg[%d]: %ls\n"), i, aArgs[i]);
        }
        /* No altering of aArgs and/or pszImage after this point! */

        /*
         * Finalize process creation and wait flags and input/output streams.
         */
        if (!fRunCmd)
        {
            aCreateFlags.push_back(ProcessCreateFlag_WaitForProcessStartOnly);
            Assert(!fWaitForStdOut);
            Assert(!fWaitForStdErr);
        }
        else
        {
            aWaitFlags.push_back(ProcessWaitForFlag_Terminate);
            if (gctlRunSetupHandle(fWaitForStdOut, RTHANDLESTD_OUTPUT, "stdout", enmStdOutTransform, &hVfsStdOut))
            {
                if (fWaitForStdOut)
                {
                    aCreateFlags.push_back(ProcessCreateFlag_WaitForStdOut);
                    aWaitFlags.push_back(ProcessWaitForFlag_StdOut);
                }
            }
            else /* Failed to set up handle, disable. */
                fWaitForStdOut = false;

            if (gctlRunSetupHandle(fWaitForStdErr, RTHANDLESTD_ERROR, "stderr", enmStdErrTransform, &hVfsStdErr))
            {
                if (fWaitForStdErr)
                {
                    aCreateFlags.push_back(ProcessCreateFlag_WaitForStdErr);
                    aWaitFlags.push_back(ProcessWaitForFlag_StdErr);
                }
            }
            else /* Failed to set up handle, disable. */
                fWaitForStdErr = false;
        }
    }
    catch (std::bad_alloc &)
    {
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "VERR_NO_MEMORY\n");
    }

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    HRESULT hrc;

    try
    {
        do
        {
            /* Get current time stamp to later calculate rest of timeout left. */
            uint64_t msStart = RTTimeMilliTS();

            /*
             * Create the process.
             */
            if (pCtx->cVerbose)
            {
                if (cMsTimeout == 0)
                    RTPrintf(GuestCtrl::tr("Starting guest process ...\n"));
                else
                    RTPrintf(GuestCtrl::tr("Starting guest process (within %ums)\n"), cMsTimeout);
            }
            ComPtr<IGuestProcess> pProcess;
            CHECK_ERROR_BREAK(pCtx->pGuestSession, ProcessCreate(Bstr(pszImage).raw(),
                                                                 ComSafeArrayAsInParam(aArgs),
                                                                 Bstr(pszCwd).raw(),
                                                                 ComSafeArrayAsInParam(aEnv),
                                                                 ComSafeArrayAsInParam(aCreateFlags),
                                                                 gctlRunGetRemainingTime(msStart, cMsTimeout),
                                                                 pProcess.asOutParam()));

            /*
             * Explicitly wait for the guest process to be in a started state.
             */
            com::SafeArray<ProcessWaitForFlag_T> aWaitStartFlags;
            aWaitStartFlags.push_back(ProcessWaitForFlag_Start);
            ProcessWaitResult_T waitResult;
            CHECK_ERROR_BREAK(pProcess, WaitForArray(ComSafeArrayAsInParam(aWaitStartFlags),
                                                     gctlRunGetRemainingTime(msStart, cMsTimeout), &waitResult));

            ULONG uPID = 0;
            CHECK_ERROR_BREAK(pProcess, COMGETTER(PID)(&uPID));
            if (fRunCmd && pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Process '%s' (PID %RU32) started\n"), pszImage, uPID);
            else if (!fRunCmd && pCtx->cVerbose)
            {
                /* Just print plain PID to make it easier for scripts
                 * invoking VBoxManage. */
                RTPrintf(GuestCtrl::tr("[%RU32 - Session %RU32]\n"), uPID, pCtx->uSessionID);
            }

            /*
             * Wait for process to exit/start...
             */
            RTMSINTERVAL    cMsTimeLeft = 1; /* Will be calculated. */
            bool            fReadStdOut = false;
            bool            fReadStdErr = false;
            bool            fCompleted  = false;
            bool            fCompletedStartCmd = false;

            vrc = VINF_SUCCESS;
            while (   !fCompleted
                   && cMsTimeLeft > 0)
            {
                cMsTimeLeft = gctlRunGetRemainingTime(msStart, cMsTimeout);
                CHECK_ERROR_BREAK(pProcess, WaitForArray(ComSafeArrayAsInParam(aWaitFlags),
                                                         RT_MIN(500 /*ms*/, RT_MAX(cMsTimeLeft, 1 /*ms*/)),
                                                         &waitResult));
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Wait result is '%s' (%d)\n"), gctlProcessWaitResultToText(waitResult), waitResult);
                switch (waitResult)
                {
                    case ProcessWaitResult_Start: /** @todo you always wait for 'start', */
                        fCompletedStartCmd = fCompleted = !fRunCmd; /* Only wait for startup if the 'start' command. */
                        if (!fCompleted && aWaitFlags[0] == ProcessWaitForFlag_Start)
                            aWaitFlags[0] = ProcessWaitForFlag_Terminate;
                        break;
                    case ProcessWaitResult_StdOut:
                        fReadStdOut = true;
                        break;
                    case ProcessWaitResult_StdErr:
                        fReadStdErr = true;
                        break;
                    case ProcessWaitResult_Terminate:
                        if (pCtx->cVerbose)
                            RTPrintf(GuestCtrl::tr("Process terminated\n"));
                        /* Process terminated, we're done. */
                        fCompleted = true;
                        break;
                    case ProcessWaitResult_WaitFlagNotSupported:
                        /* The guest does not support waiting for stdout/err, so
                         * yield to reduce the CPU load due to busy waiting. */
                        RTThreadYield();
                        fReadStdOut = fReadStdErr = true;
                        /* Note: In case the user specified explicitly not wanting to wait for stdout / stderr,
                         * the configured VFS handle goes to / will be fed from the bit bucket. */
                        break;
                    case ProcessWaitResult_Timeout:
                    {
                        /** @todo It is really unclear whether we will get stuck with the timeout
                         *        result here if the guest side times out the process and fails to
                         *        kill the process...  To be on the save side, double the IPC and
                         *        check the process status every time we time out.  */
                        ProcessStatus_T enmProcStatus;
                        CHECK_ERROR_BREAK(pProcess, COMGETTER(Status)(&enmProcStatus));
                        if (   enmProcStatus == ProcessStatus_TimedOutKilled
                            || enmProcStatus == ProcessStatus_TimedOutAbnormally)
                            fCompleted = true;
                        fReadStdOut = fReadStdErr = true;
                        break;
                    }
                    case ProcessWaitResult_Status:
                        /* ignore. */
                        break;
                    case ProcessWaitResult_Error:
                        /* waitFor is dead in the water, I think, so better leave the loop. */
                        vrc = VERR_CALLBACK_RETURN;
                        break;

                    case ProcessWaitResult_StdIn:   AssertFailed(); /* did ask for this! */ break;
                    case ProcessWaitResult_None:    AssertFailed(); /* used. */ break;
                    default:                        AssertFailed(); /* huh? */ break;
                }

                if (g_fGuestCtrlCanceled)
                    break;

                /*
                 * Pump output as needed.
                 */
                if (fReadStdOut)
                {
                    cMsTimeLeft = gctlRunGetRemainingTime(msStart, cMsTimeout);
                    int vrc2 = gctlRunPumpOutput(pProcess, hVfsStdOut, 1 /* StdOut */, cMsTimeLeft);
                    if (RT_FAILURE(vrc2) && RT_SUCCESS(vrc))
                        vrc = vrc2;
                    fReadStdOut = false;
                }
                if (fReadStdErr)
                {
                    cMsTimeLeft = gctlRunGetRemainingTime(msStart, cMsTimeout);
                    int vrc2 = gctlRunPumpOutput(pProcess, hVfsStdErr, 2 /* StdErr */, cMsTimeLeft);
                    if (RT_FAILURE(vrc2) && RT_SUCCESS(vrc))
                        vrc = vrc2;
                    fReadStdErr = false;
                }
                if (   RT_FAILURE(vrc)
                    || g_fGuestCtrlCanceled)
                    break;

                /*
                 * Process events before looping.
                 */
                NativeEventQueue::getMainEventQueue()->processEventQueue(0);
            } /* while */

            /*
             * Report status back to the user.
             */
            if (g_fGuestCtrlCanceled)
            {
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Process execution aborted!\n"));
                rcExit = EXITCODEEXEC_CANCELED;
            }
            else if (fCompletedStartCmd)
            {
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Process successfully started!\n"));
                rcExit = RTEXITCODE_SUCCESS;
            }
            else if (fCompleted)
            {
                ProcessStatus_T procStatus;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(Status)(&procStatus));
                if (   procStatus == ProcessStatus_TerminatedNormally
                    || procStatus == ProcessStatus_TerminatedAbnormally
                    || procStatus == ProcessStatus_TerminatedSignal)
                {
                    LONG lExitCode;
                    CHECK_ERROR_BREAK(pProcess, COMGETTER(ExitCode)(&lExitCode));
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Exit code=%u (Status=%u [%s])\n"),
                                 lExitCode, procStatus, gctlProcessStatusToText(procStatus));

                    rcExit = gctlRunCalculateExitCode(procStatus, lExitCode, true /*fReturnExitCodes*/);
                }
                else if (   procStatus == ProcessStatus_TimedOutKilled
                         || procStatus == ProcessStatus_TimedOutAbnormally)
                {
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Process timed out (guest side) and %s\n"),
                                 procStatus == ProcessStatus_TimedOutAbnormally
                                 ? GuestCtrl::tr("failed to terminate so far") : GuestCtrl::tr("was terminated"));
                    rcExit = EXITCODEEXEC_TIMEOUT;
                }
                else
                {
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Process now is in status [%s] (unexpected)\n"),
                                 gctlProcessStatusToText(procStatus));
                    rcExit = RTEXITCODE_FAILURE;
                }
            }
            else if (RT_FAILURE_NP(vrc))
            {
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Process monitor loop quit with vrc=%Rrc\n"), vrc);
                rcExit = RTEXITCODE_FAILURE;
            }
            else
            {
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Process monitor loop timed out\n"));
                rcExit = EXITCODEEXEC_TIMEOUT;
            }

        } while (0);
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }

    /*
     * Decide what to do with the guest session.
     *
     * If it's the 'start' command where detach the guest process after
     * starting, don't close the guest session it is part of, except on
     * failure or ctrl-c.
     *
     * For the 'run' command the guest process quits with us.
     */
    if (!fRunCmd && SUCCEEDED(hrc) && !g_fGuestCtrlCanceled)
        pCtx->fDetachGuestSession = true;

    /* Make sure we return failure on failure. */
    if (FAILED(hrc) && rcExit == RTEXITCODE_SUCCESS)
        rcExit = RTEXITCODE_FAILURE;
    return rcExit;
}


static DECLCALLBACK(RTEXITCODE) gctlHandleRun(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    return gctlHandleRunCommon(pCtx, argc, argv, true /*fRunCmd*/);
}


static DECLCALLBACK(RTEXITCODE) gctlHandleStart(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    return gctlHandleRunCommon(pCtx, argc, argv, false /*fRunCmd*/);
}


static RTEXITCODE gctlHandleCopy(PGCTLCMDCTX pCtx, int argc, char **argv, bool fHostToGuest)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /*
     * IGuest::CopyToGuest is kept as simple as possible to let the developer choose
     * what and how to implement the file enumeration/recursive lookup, like VBoxManage
     * does in here.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--follow",              'L',     RTGETOPT_REQ_NOTHING }, /* Kept for backwards-compatibility (VBox < 7.0). */
        { "--dereference",         'L',     RTGETOPT_REQ_NOTHING },
        { "--no-replace",          'n',     RTGETOPT_REQ_NOTHING }, /* like "-n" via cp. */
        { "--recursive",           'R',     RTGETOPT_REQ_NOTHING },
        { "--target-directory",    't',     RTGETOPT_REQ_STRING  },
        { "--update",              'u',     RTGETOPT_REQ_NOTHING }  /* like "-u" via cp. */
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    bool fDstMustBeDir = false;
    const char *pszDst = NULL;
    bool fFollow = false;
    bool fRecursive = false;
    bool fUpdate = false; /* Whether to copy the file only if it's newer than the target. */
    bool fNoReplace = false; /* Only copy the file if it does not exist yet. */

    int vrc = VINF_SUCCESS;
    while (  (ch = RTGetOpt(&GetState, &ValueUnion)) != 0
           && ch != VINF_GETOPT_NOT_OPTION)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'L':
                if (!RTStrICmp(ValueUnion.pDef->pszLong, "--follow"))
                    RTMsgWarning("--follow is deprecated; use --dereference instead.");
                fFollow = true;
                break;

            case 'n':
                fNoReplace = true;
                break;

            case 'R':
                fRecursive = true;
                break;

            case 't':
                pszDst = ValueUnion.psz;
                fDstMustBeDir = true;
                break;

            case 'u':
                fUpdate = true;
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    char **papszSources = RTGetOptNonOptionArrayPtr(&GetState);
    size_t cSources = &argv[argc] - papszSources;

    if (!cSources)
        return errorSyntax(GuestCtrl::tr("No sources specified!"));

    /* Unless a --target-directory is given, the last argument is the destination, so
       bump it from the source list. */
    if (pszDst == NULL && cSources >= 2)
        pszDst = papszSources[--cSources];

    if (pszDst == NULL)
        return errorSyntax(GuestCtrl::tr("No destination specified!"));

    char szAbsDst[RTPATH_MAX];
    if (!fHostToGuest)
    {
        vrc = RTPathAbs(pszDst, szAbsDst, sizeof(szAbsDst));
        if (RT_SUCCESS(vrc))
            pszDst = szAbsDst;
        else
            return RTMsgErrorExitFailure(GuestCtrl::tr("RTPathAbs failed on '%s': %Rrc"), pszDst, vrc);
    }

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Done parsing arguments, do some more preparations.
     */
    if (pCtx->cVerbose)
    {
        if (fHostToGuest)
            RTPrintf(GuestCtrl::tr("Copying from host to guest ...\n"));
        else
            RTPrintf(GuestCtrl::tr("Copying from guest to host ...\n"));
    }

    HRESULT hrc = S_OK;

    com::SafeArray<IN_BSTR> aSources;
    com::SafeArray<IN_BSTR> aFilters; /** @todo Populate those? For now we use caller-based globbing. */
    com::SafeArray<IN_BSTR> aCopyFlags;

    size_t iSrc = 0;
    for (; iSrc < cSources; iSrc++)
    {
        aSources.push_back(Bstr(papszSources[iSrc]).raw());
        aFilters.push_back(Bstr("").raw()); /* Empty for now. See @todo above. */

        /* Compile the comma-separated list of flags.
         * Certain flags are only available for specific file system objects, e.g. directories. */
        bool fIsDir = false;
        if (fHostToGuest)
        {
            RTFSOBJINFO ObjInfo;
            vrc = RTPathQueryInfo(papszSources[iSrc], &ObjInfo, RTFSOBJATTRADD_NOTHING);
            if (RT_SUCCESS(vrc))
                fIsDir = RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode);

            if (RT_FAILURE(vrc))
                break;
        }
        else /* Guest to host. */
        {
            ComPtr<IGuestFsObjInfo> pFsObjInfo;
            hrc = pCtx->pGuestSession->FsObjQueryInfo(Bstr(papszSources[iSrc]).raw(), RT_BOOL(fFollow) /* fFollowSymlinks */,
                                                      pFsObjInfo.asOutParam());
            if (SUCCEEDED(hrc))
            {
                FsObjType_T enmObjType;
                CHECK_ERROR(pFsObjInfo,COMGETTER(Type)(&enmObjType));
                if (SUCCEEDED(hrc))
                {
                    /* Take action according to source file. */
                    fIsDir = enmObjType == FsObjType_Directory;
                }
            }

            if (FAILED(hrc))
            {
                vrc = gctlPrintError(pCtx->pGuestSession, COM_IIDOF(IGuestSession));
                break;
            }
        }

        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Source '%s' is a %s\n"), papszSources[iSrc], fIsDir ? "directory" : "file");

        Utf8Str strCopyFlags;
        if (fRecursive && fIsDir) /* Only available for directories. Just ignore otherwise. */
            strCopyFlags += "Recursive,";
        if (fFollow)
            strCopyFlags += "FollowLinks,";
        if (fUpdate)    /* Only copy source files which are newer than the destination file. */
            strCopyFlags += "Update,";
        if (fNoReplace) /* Do not overwrite files. */
            strCopyFlags += "NoReplace,";
        else if (!fNoReplace && fIsDir)
            strCopyFlags += "CopyIntoExisting,"; /* Only copy into existing directories if "--no-replace" isn't specified. */
       aCopyFlags.push_back(Bstr(strCopyFlags).raw());
    }

    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure(GuestCtrl::tr("Error looking file system information for source '%s', vrc=%Rrc"),
                                     papszSources[iSrc], vrc);

    ComPtr<IProgress> pProgress;
    if (fHostToGuest)
    {
        hrc = pCtx->pGuestSession->CopyToGuest(ComSafeArrayAsInParam(aSources),
                                               ComSafeArrayAsInParam(aFilters), ComSafeArrayAsInParam(aCopyFlags),
                                               Bstr(pszDst).raw(), pProgress.asOutParam());
    }
    else /* Guest to host. */
    {
        hrc = pCtx->pGuestSession->CopyFromGuest(ComSafeArrayAsInParam(aSources),
                                                 ComSafeArrayAsInParam(aFilters), ComSafeArrayAsInParam(aCopyFlags),
                                                 Bstr(pszDst).raw(), pProgress.asOutParam());
    }

    if (FAILED(hrc))
    {
        vrc = gctlPrintError(pCtx->pGuestSession, COM_IIDOF(IGuestSession));
    }
    else if (pProgress.isNotNull())
    {
        if (pCtx->cVerbose)
            hrc = showProgress(pProgress);
        else
            hrc = pProgress->WaitForCompletion(-1 /* No timeout */);
        if (SUCCEEDED(hrc))
            CHECK_PROGRESS_ERROR(pProgress, (GuestCtrl::tr("File copy failed")));
        vrc = gctlPrintProgressError(pProgress);
    }

    if (RT_FAILURE(vrc))
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleCopyFrom(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    return gctlHandleCopy(pCtx, argc, argv, false /* Guest to host */);
}

static DECLCALLBACK(RTEXITCODE) gctlHandleCopyTo(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    return gctlHandleCopy(pCtx, argc, argv, true /* Host to guest */);
}

static DECLCALLBACK(RTEXITCODE) gctrlHandleMkDir(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--mode",                'm',                             RTGETOPT_REQ_UINT32  },
        { "--parents",             'P',                             RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    SafeArray<DirectoryCreateFlag_T> aDirCreateFlags;
    uint32_t    fDirMode     = 0; /* Default mode. */
    uint32_t    cDirsCreated = 0;
    RTEXITCODE  rcExit       = RTEXITCODE_SUCCESS;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'm': /* Mode */
                fDirMode = ValueUnion.u32;
                break;

            case 'P': /* Create parents */
                aDirCreateFlags.push_back(DirectoryCreateFlag_Parents);
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (cDirsCreated == 0)
                {
                    /*
                     * First non-option - no more options now.
                     */
                    rcExit = gctlCtxPostOptionParsingInit(pCtx);
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Creating %RU32 directories...\n", "", argc - GetState.iNext + 1),
                                 argc - GetState.iNext + 1);
                }
                if (g_fGuestCtrlCanceled)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("mkdir was interrupted by Ctrl-C (%u left)\n"),
                                          argc - GetState.iNext + 1);

                /*
                 * Create the specified directory.
                 *
                 * On failure we'll change the exit status to failure and
                 * continue with the next directory that needs creating. We do
                 * this because we only create new things, and because this is
                 * how /bin/mkdir works on unix.
                 */
                cDirsCreated++;
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Creating directory \"%s\" ...\n"), ValueUnion.psz);
                try
                {
                    HRESULT hrc;
                    CHECK_ERROR(pCtx->pGuestSession, DirectoryCreate(Bstr(ValueUnion.psz).raw(),
                                                                     fDirMode, ComSafeArrayAsInParam(aDirCreateFlags)));
                    if (FAILED(hrc))
                        rcExit = RTEXITCODE_FAILURE;
                }
                catch (std::bad_alloc &)
                {
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory\n"));
                }
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!cDirsCreated)
        return errorSyntax(GuestCtrl::tr("No directory to create specified!"));
    return rcExit;
}


static DECLCALLBACK(RTEXITCODE) gctlHandleRmDir(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--recursive",           'R',                             RTGETOPT_REQ_NOTHING },
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    bool        fRecursive  = false;
    uint32_t    cDirRemoved = 0;
    RTEXITCODE  rcExit      = RTEXITCODE_SUCCESS;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'R':
                fRecursive = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (cDirRemoved == 0)
                {
                    /*
                     * First non-option - no more options now.
                     */
                    rcExit = gctlCtxPostOptionParsingInit(pCtx);
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                    if (pCtx->cVerbose)
                    {
                        if (fRecursive)
                            RTPrintf(GuestCtrl::tr("Removing %RU32 directory tree(s)...\n", "", argc - GetState.iNext + 1),
                                     argc - GetState.iNext + 1);
                        else
                            RTPrintf(GuestCtrl::tr("Removing %RU32 directorie(s)...\n", "", argc - GetState.iNext + 1),
                                     argc - GetState.iNext + 1);
                    }
                }
                if (g_fGuestCtrlCanceled)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("rmdir was interrupted by Ctrl-C (%u left)\n"),
                                          argc - GetState.iNext + 1);

                cDirRemoved++;
                HRESULT hrc;
                if (!fRecursive)
                {
                    /*
                     * Remove exactly one directory.
                     */
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Removing directory \"%s\" ...\n"), ValueUnion.psz);
                    try
                    {
                        CHECK_ERROR(pCtx->pGuestSession, DirectoryRemove(Bstr(ValueUnion.psz).raw()));
                    }
                    catch (std::bad_alloc &)
                    {
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory\n"));
                    }
                }
                else
                {
                    /*
                     * Remove the directory and anything under it, that means files
                     * and everything.  This is in the tradition of the Windows NT
                     * CMD.EXE "rmdir /s" operation, a tradition which jpsoft's TCC
                     * strongly warns against (and half-ways questions the sense of).
                     */
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Recursively removing directory \"%s\" ...\n"), ValueUnion.psz);
                    try
                    {
                        /** @todo Make flags configurable. */
                        com::SafeArray<DirectoryRemoveRecFlag_T> aRemRecFlags;
                        aRemRecFlags.push_back(DirectoryRemoveRecFlag_ContentAndDir);

                        ComPtr<IProgress> ptrProgress;
                        CHECK_ERROR(pCtx->pGuestSession, DirectoryRemoveRecursive(Bstr(ValueUnion.psz).raw(),
                                                                                  ComSafeArrayAsInParam(aRemRecFlags),
                                                                                  ptrProgress.asOutParam()));
                        if (SUCCEEDED(hrc))
                        {
                            if (pCtx->cVerbose)
                                hrc = showProgress(ptrProgress);
                            else
                                hrc = ptrProgress->WaitForCompletion(-1 /* indefinitely */);
                            if (SUCCEEDED(hrc))
                                CHECK_PROGRESS_ERROR(ptrProgress, (GuestCtrl::tr("Directory deletion failed")));
                            ptrProgress.setNull();
                        }
                    }
                    catch (std::bad_alloc &)
                    {
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory during recursive rmdir\n"));
                    }
                }

                /*
                 * This command returns immediately on failure since it's destructive in nature.
                 */
                if (FAILED(hrc))
                    return RTEXITCODE_FAILURE;
                break;
            }

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!cDirRemoved)
        return errorSyntax(GuestCtrl::tr("No directory to remove specified!"));
    return rcExit;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleRm(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--force",                         'f',                                       RTGETOPT_REQ_NOTHING, },
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    uint32_t    cFilesDeleted   = 0;
    RTEXITCODE  rcExit          = RTEXITCODE_SUCCESS;
    bool        fForce         = true;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case VINF_GETOPT_NOT_OPTION:
                if (cFilesDeleted == 0)
                {
                    /*
                     * First non-option - no more options now.
                     */
                    rcExit = gctlCtxPostOptionParsingInit(pCtx);
                    if (rcExit != RTEXITCODE_SUCCESS)
                        return rcExit;
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Removing %RU32 file(s)...\n", "", argc - GetState.iNext + 1),
                                 argc - GetState.iNext + 1);
                }
                if (g_fGuestCtrlCanceled)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("rm was interrupted by Ctrl-C (%u left)\n"),
                                          argc - GetState.iNext + 1);

                /*
                 * Remove the specified file.
                 *
                 * On failure we will by default stop, however, the force option will
                 * by unix traditions force us to ignore errors and continue.
                 */
                cFilesDeleted++;
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Removing file \"%s\" ...\n", ValueUnion.psz));
                try
                {
                    /** @todo How does IGuestSession::FsObjRemove work with read-only files? Do we
                     *        need to do some chmod or whatever to better emulate the --force flag? */
                    HRESULT hrc;
                    CHECK_ERROR(pCtx->pGuestSession, FsObjRemove(Bstr(ValueUnion.psz).raw()));
                    if (FAILED(hrc) && !fForce)
                        return RTEXITCODE_FAILURE;
                }
                catch (std::bad_alloc &)
                {
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory\n"));
                }
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!cFilesDeleted && !fForce)
        return errorSyntax(GuestCtrl::tr("No file to remove specified!"));
    return rcExit;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleMv(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
/** @todo Missing --force/-f flag.   */
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    int vrc = VINF_SUCCESS;

    bool fDryrun = false;
    std::vector< Utf8Str > vecSources;
    const char *pszDst = NULL;
    com::SafeArray<FsObjRenameFlag_T> aRenameFlags;

    try
    {
        /** @todo Make flags configurable. */
        aRenameFlags.push_back(FsObjRenameFlag_NoReplace);

        while (   (ch = RTGetOpt(&GetState, &ValueUnion))
               && RT_SUCCESS(vrc))
        {
            /* For options that require an argument, ValueUnion has received the value. */
            switch (ch)
            {
                GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

                /** @todo Implement a --dryrun command. */
                /** @todo Implement rename flags. */

                case VINF_GETOPT_NOT_OPTION:
                    vecSources.push_back(Utf8Str(ValueUnion.psz));
                    pszDst = ValueUnion.psz;
                    break;

                default:
                    return errorGetOpt(ch, &ValueUnion);
            }
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Failed to initialize, vrc=%Rrc\n"), vrc);

    size_t cSources = vecSources.size();
    if (!cSources)
        return errorSyntax(GuestCtrl::tr("No source(s) to move specified!"));
    if (cSources < 2)
        return errorSyntax(GuestCtrl::tr("No destination specified!"));

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /* Delete last element, which now is the destination. */
    vecSources.pop_back();
    cSources = vecSources.size();

    HRESULT hrc = S_OK;

    /* Destination must be a directory when specifying multiple sources. */
    if (cSources > 1)
    {
        ComPtr<IGuestFsObjInfo> pFsObjInfo;
        hrc = pCtx->pGuestSession->FsObjQueryInfo(Bstr(pszDst).raw(), FALSE /*followSymlinks*/, pFsObjInfo.asOutParam());
        if (FAILED(hrc))
        {
            return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Destination does not exist\n"));
        }
        else
        {
            FsObjType_T enmObjType = FsObjType_Unknown; /* Shut up MSC */
            hrc = pFsObjInfo->COMGETTER(Type)(&enmObjType);
            if (SUCCEEDED(hrc))
            {
                if (enmObjType != FsObjType_Directory)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                          GuestCtrl::tr("Destination must be a directory when specifying multiple sources\n"));
            }
            else
                return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                      GuestCtrl::tr("Unable to determine destination type: %Rhrc\n"),
                                      hrc);
        }
    }

    /*
     * Rename (move) the entries.
     */
    if (pCtx->cVerbose)
        RTPrintf(GuestCtrl::tr("Renaming %RU32 %s ...\n"), cSources,
                 cSources > 1 ? GuestCtrl::tr("sources", "", cSources) : GuestCtrl::tr("source"));

    std::vector< Utf8Str >::iterator it = vecSources.begin();
    while (   it != vecSources.end()
           && !g_fGuestCtrlCanceled)
    {
        Utf8Str strSrcCur = (*it);

        ComPtr<IGuestFsObjInfo> pFsObjInfo;
        FsObjType_T enmObjType = FsObjType_Unknown; /* Shut up MSC */
        hrc = pCtx->pGuestSession->FsObjQueryInfo(Bstr(strSrcCur).raw(), FALSE /*followSymlinks*/, pFsObjInfo.asOutParam());
        if (SUCCEEDED(hrc))
            hrc = pFsObjInfo->COMGETTER(Type)(&enmObjType);
        if (FAILED(hrc))
        {
            RTPrintf(GuestCtrl::tr("Cannot stat \"%s\": No such file or directory\n"), strSrcCur.c_str());
            ++it;
            continue; /* Skip. */
        }

        char *pszDstCur = NULL;

        if (cSources > 1)
        {
            pszDstCur = RTPathJoinA(pszDst, RTPathFilename(strSrcCur.c_str()));
        }
        else
            pszDstCur = RTStrDup(pszDst);

        AssertPtrBreakStmt(pszDstCur, hrc = E_OUTOFMEMORY);

        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Renaming %s \"%s\" to \"%s\" ...\n"),
                     enmObjType == FsObjType_Directory ? GuestCtrl::tr("directory", "object") : GuestCtrl::tr("file","object"),
                     strSrcCur.c_str(), pszDstCur);

        if (!fDryrun)
        {
            CHECK_ERROR(pCtx->pGuestSession, FsObjRename(Bstr(strSrcCur).raw(),
                                                         Bstr(pszDstCur).raw(),
                                                         ComSafeArrayAsInParam(aRenameFlags)));
            /* Keep going with next item in case of errors. */
        }

        RTStrFree(pszDstCur);

        ++it;
    }

    if (   (it != vecSources.end())
        && pCtx->cVerbose)
    {
        RTPrintf(GuestCtrl::tr("Warning: Not all sources were renamed\n"));
    }

    return FAILED(hrc) ? RTEXITCODE_FAILURE : RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleMkTemp(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--mode",                'm',                             RTGETOPT_REQ_UINT32  },
        { "--directory",           'D',                             RTGETOPT_REQ_NOTHING },
        { "--secure",              's',                             RTGETOPT_REQ_NOTHING },
        { "--tmpdir",              't',                             RTGETOPT_REQ_STRING  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    Utf8Str strTemplate;
    uint32_t fMode = 0; /* Default mode. */
    bool fDirectory = false;
    bool fSecure = false;
    Utf8Str strTempDir;

    DESTDIRMAP mapDirs;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'm': /* Mode */
                fMode = ValueUnion.u32;
                break;

            case 'D': /* Create directory */
                fDirectory = true;
                break;

            case 's': /* Secure */
                fSecure = true;
                break;

            case 't': /* Temp directory */
                strTempDir = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (strTemplate.isEmpty())
                    strTemplate = ValueUnion.psz;
                else
                    return errorSyntax(GuestCtrl::tr("More than one template specified!\n"));
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (strTemplate.isEmpty())
        return errorSyntax(GuestCtrl::tr("No template specified!"));

#ifndef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (!fDirectory)
        return errorSyntax(GuestCtrl::tr("Creating temporary files is currently not supported!"));
#endif

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Create the directories.
     */
    if (pCtx->cVerbose)
    {
        if (fDirectory && !strTempDir.isEmpty())
            RTPrintf(GuestCtrl::tr("Creating temporary directory from template '%s' in directory '%s' ...\n"),
                     strTemplate.c_str(), strTempDir.c_str());
        else if (fDirectory)
            RTPrintf(GuestCtrl::tr("Creating temporary directory from template '%s' in default temporary directory ...\n"),
                     strTemplate.c_str());
        else if (!fDirectory && !strTempDir.isEmpty())
            RTPrintf(GuestCtrl::tr("Creating temporary file from template '%s' in directory '%s' ...\n"),
                     strTemplate.c_str(), strTempDir.c_str());
        else if (!fDirectory)
            RTPrintf(GuestCtrl::tr("Creating temporary file from template '%s' in default temporary directory ...\n"),
                     strTemplate.c_str());
    }

    HRESULT hrc = S_OK;
    if (fDirectory)
    {
        Bstr bstrDirectory;
        CHECK_ERROR(pCtx->pGuestSession, DirectoryCreateTemp(Bstr(strTemplate).raw(),
                                                             fMode, Bstr(strTempDir).raw(),
                                                             fSecure,
                                                             bstrDirectory.asOutParam()));
        if (SUCCEEDED(hrc))
            RTPrintf(GuestCtrl::tr("Directory name: %ls\n"), bstrDirectory.raw());
    }
    else
    {
        // else - temporary file not yet implemented
        /** @todo implement temporary file creation (we fend it off above, no
         *        worries). */
        hrc = E_FAIL;
    }

    return FAILED(hrc) ? RTEXITCODE_FAILURE : RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleMount(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    HRESULT hrc = S_OK;

    com::SafeArray<BSTR> mountPoints;
    CHECK_ERROR_RET(pCtx->pGuestSession, COMGETTER(MountPoints)(ComSafeArrayAsOutParam(mountPoints)), RTEXITCODE_FAILURE);

    for (size_t i = 0; i < mountPoints.size(); ++i)
        RTPrintf("%ls\n", mountPoints[i]);

    if (pCtx->cVerbose)
        RTPrintf("Found %zu mount points\n", mountPoints.size());

    return RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleFsInfo(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /*
     * Parse arguments.
     */
    enum GCTLCMD_FSINFO_OPT
    {
        GCTLCMD_FSINFO_OPT_TOTAL = 1000
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--human-readable",           'h',                            RTGETOPT_REQ_NOTHING },
        { "--total",                    GCTLCMD_FSINFO_OPT_TOTAL,       RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    bool fHumanReadable = false;
    bool fShowTotal = false;

    while (  (ch = RTGetOpt(&GetState, &ValueUnion)) != 0
           && ch != VINF_GETOPT_NOT_OPTION)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'h':
                fHumanReadable = true;
                break;

            case GCTLCMD_FSINFO_OPT_TOTAL:
                fShowTotal = true;
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (ch != VINF_GETOPT_NOT_OPTION)
        return errorSyntax(GuestCtrl::tr("No path specified to query information for!"));

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /* Stay within 80 characters width by default. */
    unsigned const cwFileSys     = 10;
    /* When displaying human-readable sizes, we need less space for a column. */
    unsigned const cwSize        = fHumanReadable ? 10 : 14;
    unsigned const cwSizeTotal   = cwSize;
    unsigned const cwSizeUsed    = cwSize;
    unsigned const cwSizeAvail   = cwSize;
    unsigned const cwUsePercent  = 6;
    unsigned const cwPathSpacing = 3; /* Spacing between last value and actual path. */

    RTPrintf("%-*s%*s%*s%*s%*s%*s%s\n",
             cwFileSys, GuestCtrl::tr("Filesystem"),
             cwSizeTotal, GuestCtrl::tr("Total"), cwSizeUsed, GuestCtrl::tr("Used"), cwSizeAvail, GuestCtrl::tr("Avail"),
             cwUsePercent, GuestCtrl::tr("Use%"),
             cwPathSpacing, "",
             GuestCtrl::tr("Path"));

    uint64_t cbTotalSize = 0;
    uint64_t cbTotalFree = 0;

    while (ch == VINF_GETOPT_NOT_OPTION)
    {
        ComPtr<IGuestFsInfo> pFsInfo;
        HRESULT hrc;
        CHECK_ERROR(pCtx->pGuestSession, FsQueryInfo(Bstr(ValueUnion.psz).raw(), pFsInfo.asOutParam()));
        if (FAILED(hrc))
        {
            rcExit = RTEXITCODE_FAILURE;
        }
        else
        {
            Bstr bstr;
            CHECK_ERROR2I(pFsInfo, COMGETTER(Type)(bstr.asOutParam()));
            /** @todo Add label and mount point once we return this. */
            LONG64 cbTotal, cbFree;
            CHECK_ERROR2I(pFsInfo, COMGETTER(TotalSize)(&cbTotal));
            CHECK_ERROR2I(pFsInfo, COMGETTER(FreeSize)(&cbFree));
            uint8_t const uPercentUsed = (cbTotal - cbFree) * 100 / cbTotal;
            if (fHumanReadable)
            {
                RTPrintf("%-*ls%*Rhcb%*Rhcb%*Rhcb%*RU8%%%*s%s",
                         cwFileSys, bstr.raw(),                                 /* Filesystem */
                         cwSizeTotal, cbTotal,                                  /* Total */
                         cwSizeUsed,  cbTotal - cbFree,                         /* Used */
                         cwSizeAvail, cbFree,                                   /* Available */
                         cwUsePercent - 1 /* For percent sign */, uPercentUsed, /* Percent */
                         cwPathSpacing, "",
                         ValueUnion.psz);                                       /* Path */
            }
            else
            {
                RTPrintf("%-*ls%*RU64%*RU64%*RU64%*RU8%%%*s%s",
                         cwFileSys, bstr.raw(),                                 /* Filesystem */
                         cwSizeTotal, cbTotal,                                  /* Total */
                         cwSizeUsed,  cbTotal - cbFree,                         /* Used */
                         cwSizeAvail, cbFree,                                   /* Available */
                         cwUsePercent - 1 /* For percent sign */, uPercentUsed, /* Percent */
                         cwPathSpacing, "",
                         ValueUnion.psz);                                       /* Path */
            }

            if (fShowTotal)
            {
                cbTotalSize += cbTotal;
                cbTotalFree += cbFree;
            }
            RTPrintf("\n");
        }

        /* Next path. */
        ch = RTGetOpt(&GetState, &ValueUnion);
    }

    if (fShowTotal)
    {
        uint8_t const uPercentUsed = (cbTotalSize - cbTotalFree) * 100 / cbTotalSize;

        if (fHumanReadable)
        {
            RTPrintf("%-*s%*Rhcb%*Rhcb%*Rhcb%*RU8%%%*s%s",
                     cwFileSys, "total",
                     cwSizeTotal, cbTotalSize,                                  /* Total */
                     cwSizeUsed,  cbTotalSize - cbTotalFree,                    /* Used */
                     cwSizeAvail, cbTotalFree,                                  /* Available */
                     cwUsePercent - 1 /* For percent sign */, uPercentUsed,     /* Percent */
                     cwPathSpacing, "",
                     "-");                                                      /* Path */
        }
        else
        {
            RTPrintf("%-*s%*RU64%*RU64%*RU64%*RU8%%%*s%s",
                     cwFileSys, "total",                                        /* Filesystem */
                     cwSizeTotal, cbTotalSize,                                  /* Total */
                     cwSizeUsed,  cbTotalSize - cbTotalFree,                    /* Used */
                     cwSizeAvail, cbTotalFree,                                  /* Available */
                     cwUsePercent - 1 /* For percent sign */, uPercentUsed,     /* Percent */
                     cwPathSpacing, "",
                     "-");                                                      /* Path */
        }
        RTPrintf("\n");
    }

    return rcExit;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleStat(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--dereference",         'L',                             RTGETOPT_REQ_NOTHING },
        { "--file-system",         'f',                             RTGETOPT_REQ_NOTHING },
        { "--format",              'c',                             RTGETOPT_REQ_STRING },
        { "--terse",               't',                             RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    while (  (ch = RTGetOpt(&GetState, &ValueUnion)) != 0
           && ch != VINF_GETOPT_NOT_OPTION)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'L': /* Dereference */
            case 'f': /* File-system */
            case 'c': /* Format */
            case 't': /* Terse */
                return errorSyntax(GuestCtrl::tr("Command \"%s\" not implemented yet!"), ValueUnion.psz);

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (ch != VINF_GETOPT_NOT_OPTION)
        return errorSyntax(GuestCtrl::tr("Nothing to stat!"));

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;


    /*
     * Do the file stat'ing.
     */
    while (ch == VINF_GETOPT_NOT_OPTION)
    {
        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Checking for element \"%s\" ...\n"), ValueUnion.psz);

        ComPtr<IGuestFsObjInfo> pFsObjInfo;
        HRESULT hrc = pCtx->pGuestSession->FsObjQueryInfo(Bstr(ValueUnion.psz).raw(), FALSE /*followSymlinks*/,
                                                          pFsObjInfo.asOutParam());
        if (FAILED(hrc))
        {
            /** @todo r=bird: There might be other reasons why we end up here than
             * non-existing "element" (object or file, please, nobody calls it elements). */
            if (pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Failed to stat '%s': No such file\n"), ValueUnion.psz);
            rcExit = RTEXITCODE_FAILURE;
        }
        else
        {
            RTPrintf(GuestCtrl::tr("  File: '%s'\n"), ValueUnion.psz); /** @todo escape this name. */

            FsObjType_T enmType = FsObjType_Unknown;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(Type)(&enmType));
            LONG64      cbObject = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(ObjectSize)(&cbObject));
            LONG64      cbAllocated = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(AllocatedSize)(&cbAllocated));
            LONG        uid = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(UID)(&uid));
            LONG        gid = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(GID)(&gid));
            Bstr        bstrUsername;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(UserName)(bstrUsername.asOutParam()));
            Bstr        bstrGroupName;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(GroupName)(bstrGroupName.asOutParam()));
            Bstr        bstrAttribs;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(FileAttributes)(bstrAttribs.asOutParam()));
            LONG64      idNode = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(NodeId)(&idNode));
            ULONG       uDevNode = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(NodeIdDevice)(&uDevNode));
            ULONG       uDeviceNo = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(DeviceNumber)(&uDeviceNo));
            ULONG       cHardLinks = 1;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(HardLinks)(&cHardLinks));
            LONG64      nsBirthTime = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(BirthTime)(&nsBirthTime));
            LONG64      nsChangeTime = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(ChangeTime)(&nsChangeTime));
            LONG64      nsModificationTime = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(ModificationTime)(&nsModificationTime));
            LONG64      nsAccessTime = 0;
            CHECK_ERROR2I(pFsObjInfo, COMGETTER(AccessTime)(&nsAccessTime));

            RTPrintf(GuestCtrl::tr("  Size: %-17RU64 Alloc: %-19RU64 Type: %s\n"),
                     cbObject, cbAllocated, gctlFsObjTypeToName(enmType));
            RTPrintf(GuestCtrl::tr("Device: %#-17RX32 INode: %-18RU64 Links: %u\n"), uDevNode, idNode, cHardLinks);

            Utf8Str strAttrib(bstrAttribs);
            char *pszMode    = strAttrib.mutableRaw();
            char *pszAttribs = strchr(pszMode, ' ');
            if (pszAttribs)
                do *pszAttribs++ = '\0';
                while (*pszAttribs == ' ');
            else
                pszAttribs = strchr(pszMode, '\0');
            if (uDeviceNo != 0)
                RTPrintf(GuestCtrl::tr("  Mode: %-16s Attrib: %-17s Dev ID: %#RX32\n"), pszMode, pszAttribs, uDeviceNo);
            else
                RTPrintf(GuestCtrl::tr("  Mode: %-16s Attrib: %s\n"), pszMode, pszAttribs);

            RTPrintf(GuestCtrl::tr(" Owner: %4d/%-12ls Group: %4d/%ls\n"), uid, bstrUsername.raw(),  gid, bstrGroupName.raw());

            RTTIMESPEC  TimeSpec;
            char        szTmp[RTTIME_STR_LEN];
            RTPrintf(GuestCtrl::tr(" Birth: %s\n"), RTTimeSpecToString(RTTimeSpecSetNano(&TimeSpec, nsBirthTime),
                     szTmp, sizeof(szTmp)));
            RTPrintf(GuestCtrl::tr("Change: %s\n"), RTTimeSpecToString(RTTimeSpecSetNano(&TimeSpec, nsChangeTime),
                     szTmp, sizeof(szTmp)));
            RTPrintf(GuestCtrl::tr("Modify: %s\n"), RTTimeSpecToString(RTTimeSpecSetNano(&TimeSpec, nsModificationTime),
                     szTmp, sizeof(szTmp)));
            RTPrintf(GuestCtrl::tr("Access: %s\n"), RTTimeSpecToString(RTTimeSpecSetNano(&TimeSpec, nsAccessTime),
                     szTmp, sizeof(szTmp)));

            /* Skiping: Generation ID - only the ISO9660 VFS sets this.  FreeBSD user flags. */
        }

        /* Next file. */
        ch = RTGetOpt(&GetState, &ValueUnion);
    }

    return rcExit;
}

/**
 * Waits for a Guest Additions run level being reached.
 *
 * @returns VBox status code.
 *          Returns VERR_CANCELLED if waiting for cancelled due to signal handling, e.g. when CTRL+C or some sort was pressed.
 * @param   pCtx                The guest control command context.
 * @param   enmRunLevel         Run level to wait for.
 * @param   cMsTimeout          Timeout (in ms) for waiting.
 */
static int gctlWaitForRunLevel(PGCTLCMDCTX pCtx, AdditionsRunLevelType_T enmRunLevel, RTMSINTERVAL cMsTimeout)
{
    int vrc = VINF_SUCCESS; /* Shut up MSVC. */

    try
    {
        HRESULT hrc = S_OK;
        /** Whether we need to actually wait for the run level or if we already reached it. */
        bool fWait = false;

        /* Install an event handler first to catch any runlevel changes. */
        ComObjPtr<GuestAdditionsRunlevelListenerImpl> pGuestListener;
        do
        {
            /* Listener creation. */
            pGuestListener.createObject();
            pGuestListener->init(new GuestAdditionsRunlevelListener(enmRunLevel));

            /* Register for IGuest events. */
            ComPtr<IEventSource> es;
            CHECK_ERROR_BREAK(pCtx->pGuest, COMGETTER(EventSource)(es.asOutParam()));
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnGuestAdditionsStatusChanged);
            CHECK_ERROR_BREAK(es, RegisterListener(pGuestListener, ComSafeArrayAsInParam(eventTypes),
                                                   true /* Active listener */));

            AdditionsRunLevelType_T enmRunLevelCur = AdditionsRunLevelType_None;
            CHECK_ERROR_BREAK(pCtx->pGuest, COMGETTER(AdditionsRunLevel)(&enmRunLevelCur));
            fWait = enmRunLevelCur != enmRunLevel;

            if (pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Current run level is %RU32\n"), enmRunLevelCur);

        } while (0);

        if (fWait)
        {
            if (pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Waiting for run level %RU32 ...\n"), enmRunLevel);

            RTMSINTERVAL tsStart = RTTimeMilliTS();
            while (RTTimeMilliTS() - tsStart < cMsTimeout)
            {
                /* Wait for the global signal semaphore getting signalled. */
                vrc = RTSemEventWait(g_SemEventGuestCtrlCanceled, 100 /* ms */);
                if (RT_FAILURE(vrc))
                {
                    if (vrc == VERR_TIMEOUT)
                        continue;
                    else
                    {
                        RTPrintf(GuestCtrl::tr("Waiting failed with %Rrc\n"), vrc);
                        break;
                    }
                }
                else if (pCtx->cVerbose)
                {
                    RTPrintf(GuestCtrl::tr("Run level %RU32 reached\n"), enmRunLevel);
                    break;
                }

                NativeEventQueue::getMainEventQueue()->processEventQueue(0);
            }

            if (   vrc == VERR_TIMEOUT
                && pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Run level %RU32 not reached within time\n"), enmRunLevel);
        }

        if (!pGuestListener.isNull())
        {
            /* Guest callback unregistration. */
            ComPtr<IEventSource> pES;
            CHECK_ERROR(pCtx->pGuest, COMGETTER(EventSource)(pES.asOutParam()));
            if (!pES.isNull())
                CHECK_ERROR(pES, UnregisterListener(pGuestListener));
            pGuestListener.setNull();
        }

        if (g_fGuestCtrlCanceled)
            vrc = VERR_CANCELLED;
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    return vrc;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleUpdateAdditions(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /** Timeout to wait for the whole updating procedure to complete. */
    uint32_t                cMsTimeout = RT_INDEFINITE_WAIT;
    /** Source path to .ISO Guest Additions file to use. */
    Utf8Str                 strSource;
    com::SafeArray<IN_BSTR> aArgs;
    /** Whether to reboot the guest automatically when the update process has finished successfully. */
    bool fRebootOnFinish = false;
    /** Whether to only wait for getting the update process started instead of waiting until it finishes. */
    bool fWaitStartOnly  = false;
    /** Whether to wait for the VM being ready to start the update. Needs Guest Additions facility reporting. */
    bool fWaitReady      = false;
    /** Whether to verify if the Guest Additions were successfully updated on the guest. */
    bool fVerify         = false;

    /*
     * Parse arguments.
     */
    enum KGSTCTRLUPDATEADDITIONSOPT
    {
        KGSTCTRLUPDATEADDITIONSOPT_REBOOT = 1000,
        KGSTCTRLUPDATEADDITIONSOPT_SOURCE,
        KGSTCTRLUPDATEADDITIONSOPT_TIMEOUT,
        KGSTCTRLUPDATEADDITIONSOPT_VERIFY,
        KGSTCTRLUPDATEADDITIONSOPT_WAITREADY,
        KGSTCTRLUPDATEADDITIONSOPT_WAITSTART
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--reboot",              KGSTCTRLUPDATEADDITIONSOPT_REBOOT,           RTGETOPT_REQ_NOTHING },
        { "--source",              KGSTCTRLUPDATEADDITIONSOPT_SOURCE,           RTGETOPT_REQ_STRING  },
        { "--timeout",             KGSTCTRLUPDATEADDITIONSOPT_TIMEOUT,          RTGETOPT_REQ_UINT32 },
        { "--verify",              KGSTCTRLUPDATEADDITIONSOPT_VERIFY,           RTGETOPT_REQ_NOTHING },
        { "--wait-ready",          KGSTCTRLUPDATEADDITIONSOPT_WAITREADY,        RTGETOPT_REQ_NOTHING },
        { "--wait-start",          KGSTCTRLUPDATEADDITIONSOPT_WAITSTART,        RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    int vrc = VINF_SUCCESS;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case KGSTCTRLUPDATEADDITIONSOPT_REBOOT:
                fRebootOnFinish = true;
                break;

            case KGSTCTRLUPDATEADDITIONSOPT_SOURCE:
                vrc = RTPathAbsCxx(strSource, ValueUnion.psz);
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExitFailure(GuestCtrl::tr("RTPathAbsCxx failed on '%s': %Rrc"), ValueUnion.psz, vrc);
                break;

            case KGSTCTRLUPDATEADDITIONSOPT_WAITSTART:
                fWaitStartOnly = true;
                break;

            case KGSTCTRLUPDATEADDITIONSOPT_WAITREADY:
                fWaitReady = true;
                break;

            case KGSTCTRLUPDATEADDITIONSOPT_VERIFY:
                fVerify         = true;
                fRebootOnFinish = true; /* Verification needs a mandatory reboot after successful update. */
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (aArgs.size() == 0 && strSource.isEmpty())
                    strSource = ValueUnion.psz;
                else
                    aArgs.push_back(Bstr(ValueUnion.psz).raw());
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (pCtx->cVerbose)
        RTPrintf(GuestCtrl::tr("Updating Guest Additions ...\n"));

    HRESULT hrc = S_OK;
    while (strSource.isEmpty())
    {
        ComPtr<ISystemProperties> pProperties;
        CHECK_ERROR_BREAK(pCtx->pArg->virtualBox, COMGETTER(SystemProperties)(pProperties.asOutParam()));
        Bstr strISO;
        CHECK_ERROR_BREAK(pProperties, COMGETTER(DefaultAdditionsISO)(strISO.asOutParam()));
        strSource = strISO;
        break;
    }

    /* Determine source if not set yet. */
    if (strSource.isEmpty())
    {
        RTMsgError(GuestCtrl::tr("No Guest Additions source found or specified, aborting\n"));
        vrc = VERR_FILE_NOT_FOUND;
    }
    else if (!RTFileExists(strSource.c_str()))
    {
        RTMsgError(GuestCtrl::tr("Source \"%s\" does not exist!\n"), strSource.c_str());
        vrc = VERR_FILE_NOT_FOUND;
    }



#if 0
        ComPtr<IGuest> guest;
        HRESULT hrc = pConsole->COMGETTER(Guest)(guest.asOutParam());
        if (SUCCEEDED(hrc) && !guest.isNull())
        {
            SHOW_STRING_PROP_NOT_EMPTY(guest, OSTypeId, "GuestOSType", GuestCtrl::tr("OS type:"));

            AdditionsRunLevelType_T guestRunLevel; /** @todo Add a runlevel-to-string (e.g. 0 = "None") method? */
            hrc = guest->COMGETTER(AdditionsRunLevel)(&guestRunLevel);
            if (SUCCEEDED(hrc))
                SHOW_ULONG_VALUE("GuestAdditionsRunLevel", GuestCtrl::tr("Additions run level:"), (ULONG)guestRunLevel, "");

            Bstr guestString;
            hrc = guest->COMGETTER(AdditionsVersion)(guestString.asOutParam());
            if (   SUCCEEDED(hrc)
                && !guestString.isEmpty())
            {
                ULONG uRevision;
                hrc = guest->COMGETTER(AdditionsRevision)(&uRevision);
                if (FAILED(hrc))
                    uRevision = 0;
                RTStrPrintf(szValue, sizeof(szValue), "%ls r%u", guestString.raw(), uRevision);
                SHOW_UTF8_STRING("GuestAdditionsVersion", GuestCtrl::tr("Additions version:"), szValue);
            }
        }
#endif

    if (RT_SUCCESS(vrc))
    {
        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Using source: %s\n"), strSource.c_str());

        RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;

        if (fWaitReady)
        {
            if (pCtx->cVerbose)
                RTPrintf(GuestCtrl::tr("Waiting for current Guest Additions inside VM getting ready for updating ...\n"));

            const uint64_t uTsStart = RTTimeMilliTS();
            vrc = gctlWaitForRunLevel(pCtx, AdditionsRunLevelType_Userland, cMsTimeout);
            if (RT_SUCCESS(vrc))
                cMsTimeout = cMsTimeout != RT_INDEFINITE_WAIT ? cMsTimeout - (RTTimeMilliTS() - uTsStart) : cMsTimeout;
        }

        if (RT_SUCCESS(vrc))
        {
            /* Get current Guest Additions version / revision. */
            Bstr  strGstVerCur;
            ULONG uGstRevCur   = 0;
            hrc = pCtx->pGuest->COMGETTER(AdditionsVersion)(strGstVerCur.asOutParam());
            if (   SUCCEEDED(hrc)
                && !strGstVerCur.isEmpty())
            {
                hrc = pCtx->pGuest->COMGETTER(AdditionsRevision)(&uGstRevCur);
                if (SUCCEEDED(hrc))
                {
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Guest Additions %lsr%RU64 currently installed, waiting for Guest Additions installer to start ...\n"),
                                 strGstVerCur.raw(), uGstRevCur);
                }
            }

            com::SafeArray<AdditionsUpdateFlag_T> aUpdateFlags;
            if (fWaitStartOnly)
                aUpdateFlags.push_back(AdditionsUpdateFlag_WaitForUpdateStartOnly);

            ComPtr<IProgress> pProgress;
            CHECK_ERROR(pCtx->pGuest, UpdateGuestAdditions(Bstr(strSource).raw(),
                                                           ComSafeArrayAsInParam(aArgs),
                                                           ComSafeArrayAsInParam(aUpdateFlags),
                                                           pProgress.asOutParam()));
            if (FAILED(hrc))
                vrc = gctlPrintError(pCtx->pGuest, COM_IIDOF(IGuest));
            else
            {
                if (pCtx->cVerbose)
                    hrc = showProgress(pProgress);
                else
                    hrc = pProgress->WaitForCompletion((int32_t)cMsTimeout);

                if (SUCCEEDED(hrc))
                    CHECK_PROGRESS_ERROR(pProgress, (GuestCtrl::tr("Guest Additions update failed")));
                vrc = gctlPrintProgressError(pProgress);
                if (RT_SUCCESS(vrc))
                {
                    if (pCtx->cVerbose)
                        RTPrintf(GuestCtrl::tr("Guest Additions update successful.\n"));

                    if (fRebootOnFinish)
                    {
                        if (pCtx->cVerbose)
                            RTPrintf(GuestCtrl::tr("Rebooting guest ...\n"));
                        com::SafeArray<GuestShutdownFlag_T> aShutdownFlags;
                        aShutdownFlags.push_back(GuestShutdownFlag_Reboot);
                        CHECK_ERROR(pCtx->pGuest, Shutdown(ComSafeArrayAsInParam(aShutdownFlags)));
                        if (FAILED(hrc))
                        {
                            if (hrc == VBOX_E_NOT_SUPPORTED)
                            {
                                RTPrintf(GuestCtrl::tr("Current installed Guest Additions don't support automatic rebooting. "
                                                       "Please reboot manually.\n"));
                                vrc = VERR_NOT_SUPPORTED;
                            }
                            else
                                vrc = gctlPrintError(pCtx->pGuest, COM_IIDOF(IGuest));
                        }
                        else
                        {
                            if (fWaitReady)
                            {
                                if (pCtx->cVerbose)
                                    RTPrintf(GuestCtrl::tr("Waiting for new Guest Additions inside VM getting ready ...\n"));

                                vrc = gctlWaitForRunLevel(pCtx, AdditionsRunLevelType_Userland, cMsTimeout);
                                if (RT_SUCCESS(vrc))
                                {
                                    if (fVerify)
                                    {
                                        if (pCtx->cVerbose)
                                            RTPrintf(GuestCtrl::tr("Verifying Guest Additions update ...\n"));

                                        /* Get new Guest Additions version / revision. */
                                        Bstr strGstVerNew;
                                        ULONG uGstRevNew   = 0;
                                        hrc = pCtx->pGuest->COMGETTER(AdditionsVersion)(strGstVerNew.asOutParam());
                                        if (   SUCCEEDED(hrc)
                                            && !strGstVerNew.isEmpty())
                                        {
                                            hrc = pCtx->pGuest->COMGETTER(AdditionsRevision)(&uGstRevNew);
                                            if (FAILED(hrc))
                                                uGstRevNew = 0;
                                        }

                                        /** @todo Do more verification here. */
                                        vrc = uGstRevNew > uGstRevCur ? VINF_SUCCESS : VERR_NO_CHANGE;

                                        if (pCtx->cVerbose)
                                        {
                                            RTPrintf(GuestCtrl::tr("Old Guest Additions: %ls%RU64\n"), strGstVerCur.raw(),
                                                                   uGstRevCur);
                                            RTPrintf(GuestCtrl::tr("New Guest Additions: %ls%RU64\n"), strGstVerNew.raw(),
                                                                   uGstRevNew);

                                            if (RT_FAILURE(vrc))
                                            {
                                                RTPrintf(GuestCtrl::tr("\nError updating Guest Additions, please check guest installer log\n"));
                                            }
                                            else
                                            {
                                                if (uGstRevNew < uGstRevCur)
                                                    RTPrintf(GuestCtrl::tr("\nWARNING: Guest Additions were downgraded\n"));
                                            }
                                        }
                                    }
                                }
                            }
                            else if (pCtx->cVerbose)
                                RTPrintf(GuestCtrl::tr("The guest needs to be restarted in order to make use of the updated Guest Additions.\n"));
                        }
                    }
                }
            }
        }
    }

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Returns a Guest Additions run level from a string.
 *
 * @returns Run level if found, or AdditionsRunLevelType_None if not found / invalid.
 * @param   pcszStr             String to return run level for.
 */
static AdditionsRunLevelType_T gctlGetRunLevelFromStr(const char *pcszStr)
{
    AssertPtrReturn(pcszStr, AdditionsRunLevelType_None);

    if      (RTStrICmp(pcszStr, "system")   == 0) return AdditionsRunLevelType_System;
    else if (RTStrICmp(pcszStr, "userland") == 0) return AdditionsRunLevelType_Userland;
    else if (RTStrICmp(pcszStr, "desktop") == 0)  return AdditionsRunLevelType_Desktop;

    return AdditionsRunLevelType_None;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleWaitRunLevel(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /** Timeout to wait for run level being reached.
     *  By default we wait until it's reached. */
    uint32_t cMsTimeout = RT_INDEFINITE_WAIT;

    /*
     * Parse arguments.
     */
    enum KGSTCTRLWAITRUNLEVELOPT
    {
        KGSTCTRLWAITRUNLEVELOPT_TIMEOUT = 1000
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--timeout",             KGSTCTRLWAITRUNLEVELOPT_TIMEOUT,          RTGETOPT_REQ_UINT32 }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    AdditionsRunLevelType_T enmRunLevel = AdditionsRunLevelType_None;

    int vrc = VINF_SUCCESS;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case KGSTCTRLWAITRUNLEVELOPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                enmRunLevel = gctlGetRunLevelFromStr(ValueUnion.psz);
                if (enmRunLevel == AdditionsRunLevelType_None)
                    return errorSyntax(GuestCtrl::tr("Invalid run level specified. Valid values are: system, userland, desktop"));
                break;
            }

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    if (enmRunLevel == AdditionsRunLevelType_None)
        return errorSyntax(GuestCtrl::tr("Missing run level to wait for"));

    vrc = gctlWaitForRunLevel(pCtx, enmRunLevel, cMsTimeout);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleList(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    bool fSeenListArg   = false;
    bool fListAll       = false;
    bool fListSessions  = false;
    bool fListProcesses = false;
    bool fListFiles     = false;

    int vrc = VINF_SUCCESS;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case VINF_GETOPT_NOT_OPTION:
                if (   !RTStrICmp(ValueUnion.psz, "sessions")
                    || !RTStrICmp(ValueUnion.psz, "sess"))
                    fListSessions = true;
                else if (   !RTStrICmp(ValueUnion.psz, "processes")
                         || !RTStrICmp(ValueUnion.psz, "procs"))
                    fListSessions = fListProcesses = true; /* Showing processes implies showing sessions. */
                else if (!RTStrICmp(ValueUnion.psz, "files"))
                    fListSessions = fListFiles = true;     /* Showing files implies showing sessions. */
                else if (!RTStrICmp(ValueUnion.psz, "all"))
                    fListAll = true;
                else
                    return errorSyntax(GuestCtrl::tr("Unknown list: '%s'"), ValueUnion.psz);
                fSeenListArg = true;
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!fSeenListArg)
        return errorSyntax(GuestCtrl::tr("Missing list name"));
    Assert(fListAll || fListSessions);

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;


    /** @todo Do we need a machine-readable output here as well? */

    HRESULT hrc;
    size_t cTotalProcs = 0;
    size_t cTotalFiles = 0;

    SafeIfaceArray <IGuestSession> collSessions;
    CHECK_ERROR(pCtx->pGuest, COMGETTER(Sessions)(ComSafeArrayAsOutParam(collSessions)));
    if (SUCCEEDED(hrc))
    {
        size_t const cSessions = collSessions.size();
        if (cSessions)
        {
            RTPrintf(GuestCtrl::tr("Active guest sessions:\n"));

            /** @todo Make this output a bit prettier. No time now. */

            for (size_t i = 0; i < cSessions; i++)
            {
                ComPtr<IGuestSession> pCurSession = collSessions[i];
                if (!pCurSession.isNull())
                {
                    do
                    {
                        ULONG uID;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(Id)(&uID));
                        Bstr strName;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(Name)(strName.asOutParam()));
                        Bstr strUser;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(User)(strUser.asOutParam()));
                        GuestSessionStatus_T sessionStatus;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(Status)(&sessionStatus));
                        RTPrintf(GuestCtrl::tr("\n\tSession #%-3zu ID=%-3RU32 User=%-16ls Status=[%s] Name=%ls"),
                                 i, uID, strUser.raw(), gctlGuestSessionStatusToText(sessionStatus), strName.raw());
                    } while (0);

                    if (   fListAll
                        || fListProcesses)
                    {
                        SafeIfaceArray <IGuestProcess> collProcesses;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(Processes)(ComSafeArrayAsOutParam(collProcesses)));
                        for (size_t a = 0; a < collProcesses.size(); a++)
                        {
                            ComPtr<IGuestProcess> pCurProcess = collProcesses[a];
                            if (!pCurProcess.isNull())
                            {
                                do
                                {
                                    ULONG uPID;
                                    CHECK_ERROR_BREAK(pCurProcess, COMGETTER(PID)(&uPID));
                                    Bstr strExecPath;
                                    CHECK_ERROR_BREAK(pCurProcess, COMGETTER(ExecutablePath)(strExecPath.asOutParam()));
                                    ProcessStatus_T procStatus;
                                    CHECK_ERROR_BREAK(pCurProcess, COMGETTER(Status)(&procStatus));

                                    RTPrintf(GuestCtrl::tr("\n\t\tProcess #%-03zu PID=%-6RU32 Status=[%s] Command=%ls"),
                                             a, uPID, gctlProcessStatusToText(procStatus), strExecPath.raw());
                                } while (0);
                            }
                        }

                        cTotalProcs += collProcesses.size();
                    }

                    if (   fListAll
                        || fListFiles)
                    {
                        SafeIfaceArray <IGuestFile> collFiles;
                        CHECK_ERROR_BREAK(pCurSession, COMGETTER(Files)(ComSafeArrayAsOutParam(collFiles)));
                        for (size_t a = 0; a < collFiles.size(); a++)
                        {
                            ComPtr<IGuestFile> pCurFile = collFiles[a];
                            if (!pCurFile.isNull())
                            {
                                do
                                {
                                    ULONG idFile;
                                    CHECK_ERROR_BREAK(pCurFile, COMGETTER(Id)(&idFile));
                                    Bstr strName;
                                    CHECK_ERROR_BREAK(pCurFile, COMGETTER(Filename)(strName.asOutParam()));
                                    FileStatus_T fileStatus;
                                    CHECK_ERROR_BREAK(pCurFile, COMGETTER(Status)(&fileStatus));

                                    RTPrintf(GuestCtrl::tr("\n\t\tFile #%-03zu ID=%-6RU32 Status=[%s] Name=%ls"),
                                             a, idFile, gctlFileStatusToText(fileStatus), strName.raw());
                                } while (0);
                            }
                        }

                        cTotalFiles += collFiles.size();
                    }
                }
            }

            RTPrintf(GuestCtrl::tr("\n\nTotal guest sessions: %zu\n"), collSessions.size());
            if (fListAll || fListProcesses)
                RTPrintf(GuestCtrl::tr("Total guest processes: %zu\n"), cTotalProcs);
            if (fListAll || fListFiles)
                RTPrintf(GuestCtrl::tr("Total guest files: %zu\n"), cTotalFiles);
        }
        else
            RTPrintf(GuestCtrl::tr("No active guest sessions found\n"));
    }

    if (FAILED(hrc)) /** @todo yeah, right... Only the last error? */
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}

static DECLCALLBACK(RTEXITCODE) gctlHandleCloseProcess(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--session-id",          'i',                             RTGETOPT_REQ_UINT32  },
        { "--session-name",        'n',                             RTGETOPT_REQ_STRING  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    std::vector < uint32_t > vecPID;
    ULONG idSession = UINT32_MAX;
    Utf8Str strSessionName;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'n': /* Session name (or pattern) */
                strSessionName = ValueUnion.psz;
                break;

            case 'i': /* Session ID */
                idSession = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                /* Treat every else specified as a PID to kill. */
                uint32_t uPid;
                vrc = RTStrToUInt32Ex(ValueUnion.psz, NULL, 0, &uPid);
                if (   RT_SUCCESS(vrc)
                    && vrc != VWRN_TRAILING_CHARS
                    && vrc != VWRN_NUMBER_TOO_BIG
                    && vrc != VWRN_NEGATIVE_UNSIGNED)
                {
                    if (uPid != 0)
                    {
                        try
                        {
                            vecPID.push_back(uPid);
                        }
                        catch (std::bad_alloc &)
                        {
                            return RTMsgErrorExit(RTEXITCODE_FAILURE, GuestCtrl::tr("Out of memory"));
                        }
                    }
                    else
                        return errorSyntax(GuestCtrl::tr("Invalid PID value: 0"));
                }
                else
                    return errorSyntax(GuestCtrl::tr("Error parsing PID value: %Rrc"), vrc);
                break;
            }

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (vecPID.empty())
        return errorSyntax(GuestCtrl::tr("At least one PID must be specified to kill!"));

    if (   strSessionName.isEmpty()
        && idSession == UINT32_MAX)
        return errorSyntax(GuestCtrl::tr("No session ID specified!"));

    if (   strSessionName.isNotEmpty()
        && idSession != UINT32_MAX)
        return errorSyntax(GuestCtrl::tr("Either session ID or name (pattern) must be specified"));

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    HRESULT hrc = S_OK;

    ComPtr<IGuestSession> pSession;
    ComPtr<IGuestProcess> pProcess;
    do
    {
        uint32_t uProcsTerminated = 0;

        SafeIfaceArray <IGuestSession> collSessions;
        CHECK_ERROR_BREAK(pCtx->pGuest, COMGETTER(Sessions)(ComSafeArrayAsOutParam(collSessions)));
        size_t cSessions = collSessions.size();

        uint32_t cSessionsHandled = 0;
        for (size_t i = 0; i < cSessions; i++)
        {
            pSession = collSessions[i];
            Assert(!pSession.isNull());

            ULONG uID; /* Session ID */
            CHECK_ERROR_BREAK(pSession, COMGETTER(Id)(&uID));
            Bstr strName;
            CHECK_ERROR_BREAK(pSession, COMGETTER(Name)(strName.asOutParam()));
            Utf8Str strNameUtf8(strName); /* Session name */

            bool fSessionFound;
            if (strSessionName.isEmpty()) /* Search by ID. Slow lookup. */
                fSessionFound = uID == idSession;
            else /* ... or by naming pattern. */
                fSessionFound = RTStrSimplePatternMatch(strSessionName.c_str(), strNameUtf8.c_str());
            if (fSessionFound)
            {
                AssertStmt(!pSession.isNull(), break);
                cSessionsHandled++;

                SafeIfaceArray <IGuestProcess> collProcs;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Processes)(ComSafeArrayAsOutParam(collProcs)));

                size_t cProcs = collProcs.size();
                for (size_t p = 0; p < cProcs; p++)
                {
                    pProcess = collProcs[p];
                    Assert(!pProcess.isNull());

                    ULONG uPID; /* Process ID */
                    CHECK_ERROR_BREAK(pProcess, COMGETTER(PID)(&uPID));

                    bool fProcFound = false;
                    for (size_t a = 0; a < vecPID.size(); a++) /* Slow, but works. */
                    {
                        fProcFound = vecPID[a] == uPID;
                        if (fProcFound)
                            break;
                    }

                    if (fProcFound)
                    {
                        if (pCtx->cVerbose)
                            RTPrintf(GuestCtrl::tr("Terminating process (PID %RU32) (session ID %RU32) ...\n"),
                                     uPID, uID);
                        CHECK_ERROR_BREAK(pProcess, Terminate());
                        uProcsTerminated++;
                    }
                    else
                    {
                        if (idSession != UINT32_MAX)
                            RTPrintf(GuestCtrl::tr("No matching process(es) for session ID %RU32 found\n"),
                                     idSession);
                    }

                    pProcess.setNull();
                }

                pSession.setNull();
            }
        }

        if (!cSessionsHandled)
            RTPrintf(GuestCtrl::tr("No matching session(s) found\n"));

        if (uProcsTerminated)
            RTPrintf(GuestCtrl::tr("%RU32 process(es) terminated\n", "", uProcsTerminated), uProcsTerminated);

    } while (0);

    pProcess.setNull();
    pSession.setNull();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static DECLCALLBACK(RTEXITCODE) gctlHandleCloseSession(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    enum GETOPTDEF_SESSIONCLOSE
    {
        GETOPTDEF_SESSIONCLOSE_ALL = 2000
    };
    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--all",                 GETOPTDEF_SESSIONCLOSE_ALL,      RTGETOPT_REQ_NOTHING  },
        { "--session-id",          'i',                             RTGETOPT_REQ_UINT32  },
        { "--session-name",        'n',                             RTGETOPT_REQ_STRING  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    ULONG idSession = UINT32_MAX;
    Utf8Str strSessionName;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 'n': /* Session name pattern */
                strSessionName = ValueUnion.psz;
                break;

            case 'i': /* Session ID */
                idSession = ValueUnion.u32;
                break;

            case GETOPTDEF_SESSIONCLOSE_ALL:
                strSessionName = "*";
                break;

            case VINF_GETOPT_NOT_OPTION:
                /** @todo Supply a CSV list of IDs or patterns to close?
                 *  break; */
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (   strSessionName.isEmpty()
        && idSession == UINT32_MAX)
        return errorSyntax(GuestCtrl::tr("No session ID specified!"));

    if (   !strSessionName.isEmpty()
        && idSession != UINT32_MAX)
        return errorSyntax(GuestCtrl::tr("Either session ID or name (pattern) must be specified"));

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    HRESULT hrc = S_OK;

    do
    {
        size_t cSessionsHandled = 0;

        SafeIfaceArray <IGuestSession> collSessions;
        CHECK_ERROR_BREAK(pCtx->pGuest, COMGETTER(Sessions)(ComSafeArrayAsOutParam(collSessions)));
        size_t cSessions = collSessions.size();

        for (size_t i = 0; i < cSessions; i++)
        {
            ComPtr<IGuestSession> pSession = collSessions[i];
            Assert(!pSession.isNull());

            ULONG uID; /* Session ID */
            CHECK_ERROR_BREAK(pSession, COMGETTER(Id)(&uID));
            Bstr strName;
            CHECK_ERROR_BREAK(pSession, COMGETTER(Name)(strName.asOutParam()));
            Utf8Str strNameUtf8(strName); /* Session name */

            bool fSessionFound;
            if (strSessionName.isEmpty()) /* Search by ID. Slow lookup. */
                fSessionFound = uID == idSession;
            else /* ... or by naming pattern. */
                fSessionFound = RTStrSimplePatternMatch(strSessionName.c_str(), strNameUtf8.c_str());
            if (fSessionFound)
            {
                cSessionsHandled++;

                Assert(!pSession.isNull());
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Closing guest session ID=#%RU32 \"%s\" ...\n"),
                             uID, strNameUtf8.c_str());
                CHECK_ERROR_BREAK(pSession, Close());
                if (pCtx->cVerbose)
                    RTPrintf(GuestCtrl::tr("Guest session successfully closed\n"));

                pSession.setNull();
            }
        }

        if (!cSessionsHandled)
        {
            RTPrintf(GuestCtrl::tr("No guest session(s) found\n"));
            hrc = E_ABORT; /* To set exit code accordingly. */
        }

    } while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static DECLCALLBACK(RTEXITCODE) gctlHandleWatch(PGCTLCMDCTX pCtx, int argc, char **argv)
{
    AssertPtrReturn(pCtx, RTEXITCODE_FAILURE);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        GCTLCMD_COMMON_OPTION_DEFS()
        { "--timeout",                      't',                                      RTGETOPT_REQ_UINT32  }
    };

    uint32_t cMsTimeout = RT_INDEFINITE_WAIT;

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            GCTLCMD_COMMON_OPTION_CASES(pCtx, ch, &ValueUnion);

            case 't': /* Timeout */
                cMsTimeout = ValueUnion.u32;
                break;

            case VINF_GETOPT_NOT_OPTION:
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    /** @todo Specify categories to watch for. */

    RTEXITCODE rcExit = gctlCtxPostOptionParsingInit(pCtx);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    HRESULT hrc;

    try
    {
        ComObjPtr<GuestEventListenerImpl> pGuestListener;
        do
        {
            /* Listener creation. */
            pGuestListener.createObject();
            pGuestListener->init(new GuestEventListener());

            /* Register for IGuest events. */
            ComPtr<IEventSource> es;
            CHECK_ERROR_BREAK(pCtx->pGuest, COMGETTER(EventSource)(es.asOutParam()));
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnGuestSessionRegistered);
            /** @todo Also register for VBoxEventType_OnGuestUserStateChanged on demand? */
            CHECK_ERROR_BREAK(es, RegisterListener(pGuestListener, ComSafeArrayAsInParam(eventTypes),
                                                   true /* Active listener */));
            /* Note: All other guest control events have to be registered
             *       as their corresponding objects appear. */

        } while (0);

        if (pCtx->cVerbose)
            RTPrintf(GuestCtrl::tr("Waiting for events ...\n"));

        RTMSINTERVAL tsStart = RTTimeMilliTS();
        while (RTTimeMilliTS() - tsStart < cMsTimeout)
        {
            /* Wait for the global signal semaphore getting signalled. */
            int vrc = RTSemEventWait(g_SemEventGuestCtrlCanceled, 100 /* ms */);
            if (RT_FAILURE(vrc))
            {
                if (vrc != VERR_TIMEOUT)
                {
                    RTPrintf(GuestCtrl::tr("Waiting failed with %Rrc\n"), vrc);
                    break;
                }
            }
            else
                break;

            /* We need to process the event queue, otherwise our registered listeners won't get any events. */
            NativeEventQueue::getMainEventQueue()->processEventQueue(0);
        }

        if (!pGuestListener.isNull())
        {
            /* Guest callback unregistration. */
            ComPtr<IEventSource> pES;
            CHECK_ERROR(pCtx->pGuest, COMGETTER(EventSource)(pES.asOutParam()));
            if (!pES.isNull())
                CHECK_ERROR(pES, UnregisterListener(pGuestListener));
            pGuestListener.setNull();
        }
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Access the guest control store.
 *
 * @returns program exit code.
 * @note see the command line API description for parameters
 */
RTEXITCODE handleGuestControl(HandlerArg *pArg)
{
    AssertPtr(pArg);

    /*
     * Command definitions.
     */
    static const GCTLCMDDEF s_aCmdDefs[] =
    {
        { "run",                gctlHandleRun,              HELP_SCOPE_GUESTCONTROL_RUN,       0 },
        { "start",              gctlHandleStart,            HELP_SCOPE_GUESTCONTROL_START,     0 },
        { "copyfrom",           gctlHandleCopyFrom,         HELP_SCOPE_GUESTCONTROL_COPYFROM,  0 },
        { "copyto",             gctlHandleCopyTo,           HELP_SCOPE_GUESTCONTROL_COPYTO,    0 },

        { "mkdir",              gctrlHandleMkDir,           HELP_SCOPE_GUESTCONTROL_MKDIR,     0 },
        { "md",                 gctrlHandleMkDir,           HELP_SCOPE_GUESTCONTROL_MKDIR,     0 },
        { "createdirectory",    gctrlHandleMkDir,           HELP_SCOPE_GUESTCONTROL_MKDIR,     0 },
        { "createdir",          gctrlHandleMkDir,           HELP_SCOPE_GUESTCONTROL_MKDIR,     0 },

        { "rmdir",              gctlHandleRmDir,            HELP_SCOPE_GUESTCONTROL_RMDIR,     0 },
        { "removedir",          gctlHandleRmDir,            HELP_SCOPE_GUESTCONTROL_RMDIR,     0 },
        { "removedirectory",    gctlHandleRmDir,            HELP_SCOPE_GUESTCONTROL_RMDIR,     0 },

        { "rm",                 gctlHandleRm,               HELP_SCOPE_GUESTCONTROL_RM,        0 },
        { "removefile",         gctlHandleRm,               HELP_SCOPE_GUESTCONTROL_RM,        0 },
        { "erase",              gctlHandleRm,               HELP_SCOPE_GUESTCONTROL_RM,        0 },
        { "del",                gctlHandleRm,               HELP_SCOPE_GUESTCONTROL_RM,        0 },
        { "delete",             gctlHandleRm,               HELP_SCOPE_GUESTCONTROL_RM,        0 },

        { "mv",                 gctlHandleMv,               HELP_SCOPE_GUESTCONTROL_MV,        0 },
        { "move",               gctlHandleMv,               HELP_SCOPE_GUESTCONTROL_MV,        0 },
        { "ren",                gctlHandleMv,               HELP_SCOPE_GUESTCONTROL_MV,        0 },
        { "rename",             gctlHandleMv,               HELP_SCOPE_GUESTCONTROL_MV,        0 },

        { "mktemp",             gctlHandleMkTemp,           HELP_SCOPE_GUESTCONTROL_MKTEMP,    0 },
        { "createtemp",         gctlHandleMkTemp,           HELP_SCOPE_GUESTCONTROL_MKTEMP,    0 },
        { "createtemporary",    gctlHandleMkTemp,           HELP_SCOPE_GUESTCONTROL_MKTEMP,    0 },

        { "mount",              gctlHandleMount,            HELP_SCOPE_GUESTCONTROL_MOUNT,     0 },

        { "df",                 gctlHandleFsInfo,           HELP_SCOPE_GUESTCONTROL_FSINFO,    0 },
        { "fsinfo",             gctlHandleFsInfo,           HELP_SCOPE_GUESTCONTROL_FSINFO,    0 },

        { "stat",               gctlHandleStat,             HELP_SCOPE_GUESTCONTROL_STAT,      0 },

        { "closeprocess",       gctlHandleCloseProcess,     HELP_SCOPE_GUESTCONTROL_CLOSEPROCESS, GCTLCMDCTX_F_SESSION_ANONYMOUS | GCTLCMDCTX_F_NO_SIGNAL_HANDLER },
        { "closesession",       gctlHandleCloseSession,     HELP_SCOPE_GUESTCONTROL_CLOSESESSION, GCTLCMDCTX_F_SESSION_ANONYMOUS | GCTLCMDCTX_F_NO_SIGNAL_HANDLER },
        { "list",               gctlHandleList,             HELP_SCOPE_GUESTCONTROL_LIST,         GCTLCMDCTX_F_SESSION_ANONYMOUS | GCTLCMDCTX_F_NO_SIGNAL_HANDLER },
        { "watch",              gctlHandleWatch,            HELP_SCOPE_GUESTCONTROL_WATCH,        GCTLCMDCTX_F_SESSION_ANONYMOUS },

        {"updateguestadditions",gctlHandleUpdateAdditions,  HELP_SCOPE_GUESTCONTROL_UPDATEGA,     GCTLCMDCTX_F_SESSION_ANONYMOUS },
        { "updateadditions",    gctlHandleUpdateAdditions,  HELP_SCOPE_GUESTCONTROL_UPDATEGA,     GCTLCMDCTX_F_SESSION_ANONYMOUS },
        { "updatega",           gctlHandleUpdateAdditions,  HELP_SCOPE_GUESTCONTROL_UPDATEGA,     GCTLCMDCTX_F_SESSION_ANONYMOUS },

        { "waitrunlevel",       gctlHandleWaitRunLevel,     HELP_SCOPE_GUESTCONTROL_WAITRUNLEVEL, GCTLCMDCTX_F_SESSION_ANONYMOUS },
        { "waitforrunlevel",    gctlHandleWaitRunLevel,     HELP_SCOPE_GUESTCONTROL_WAITRUNLEVEL, GCTLCMDCTX_F_SESSION_ANONYMOUS },
    };

    /*
     * VBoxManage guestcontrol [common-options] <VM> [common-options] <sub-command> ...
     *
     * Parse common options and VM name until we find a sub-command.  Allowing
     * the user  to put the user and password related options before the
     * sub-command makes it easier to edit the command line when doing several
     * operations with the same guest user account.  (Accidentally, it also
     * makes the syntax diagram shorter and easier to read.)
     */
    GCTLCMDCTX CmdCtx;
    RTEXITCODE rcExit = gctrCmdCtxInit(&CmdCtx, pArg);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        static const RTGETOPTDEF s_CommonOptions[] = { GCTLCMD_COMMON_OPTION_DEFS() };

        int ch;
        RTGETOPTUNION ValueUnion;
        RTGETOPTSTATE GetState;
        RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_CommonOptions, RT_ELEMENTS(s_CommonOptions), 0, 0 /* No sorting! */);

        while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
        {
            switch (ch)
            {
                GCTLCMD_COMMON_OPTION_CASES(&CmdCtx, ch, &ValueUnion);

                case VINF_GETOPT_NOT_OPTION:
                    /* First comes the VM name or UUID. */
                    if (!CmdCtx.pszVmNameOrUuid)
                        CmdCtx.pszVmNameOrUuid = ValueUnion.psz;
                    /*
                     * The sub-command is next.  Look it up and invoke it.
                     * Note! Currently no warnings about user/password options (like we'll do later on)
                     *       for GCTLCMDCTX_F_SESSION_ANONYMOUS commands. No reason to be too pedantic.
                     */
                    else
                    {
                        const char *pszCmd = ValueUnion.psz;
                        uint32_t    iCmd;
                        for (iCmd = 0; iCmd < RT_ELEMENTS(s_aCmdDefs); iCmd++)
                            if (strcmp(s_aCmdDefs[iCmd].pszName, pszCmd) == 0)
                            {
                                CmdCtx.pCmdDef = &s_aCmdDefs[iCmd];

                                setCurrentSubcommand(s_aCmdDefs[iCmd].fSubcommandScope);
                                rcExit = s_aCmdDefs[iCmd].pfnHandler(&CmdCtx, pArg->argc - GetState.iNext + 1,
                                                                     &pArg->argv[GetState.iNext - 1]);

                                gctlCtxTerm(&CmdCtx);
                                return rcExit;
                            }
                        return errorSyntax(GuestCtrl::tr("Unknown sub-command: '%s'"), pszCmd);
                    }
                    break;

                default:
                    return errorGetOpt(ch, &ValueUnion);
            }
        }
        if (CmdCtx.pszVmNameOrUuid)
            rcExit = errorSyntax(GuestCtrl::tr("Missing sub-command"));
        else
            rcExit = errorSyntax(GuestCtrl::tr("Missing VM name and sub-command"));
    }
    return rcExit;
}
