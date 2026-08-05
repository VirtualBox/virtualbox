/* $Id: VBoxManageClipboard.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxManage - Implementation of the clipboard command.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <VBox/com/array.h>
#include <VBox/com/com.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/listeners.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/GuestHost/clipboard-helper.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/critsect.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/stdarg.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/utf16.h>

#include "VBoxManage.h"

#include <signal.h>
#include <string.h>
#include <vector>

using namespace com;

DECLARE_TRANSLATION_CONTEXT(Clipboard);


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Default MIME type used by clipboard copy when no format is specified. */
#define CLIPBOARD_DEFAULT_FORMAT        "text/plain;charset=utf-8"
/** Default copy wait time; infinite means wait until a matching guest request is observed. */
#define CLIPBOARD_COPY_DEFAULT_TIMEOUT  RT_INDEFINITE_WAIT
/** Default paste wait time; infinite means wait until guest data is available and written to stdout. */
#define CLIPBOARD_PASTE_DEFAULT_TIMEOUT RT_INDEFINITE_WAIT
/** Quiet period after a matching guest data request before copy considers the transfer complete. */
#define CLIPBOARD_COPY_REQUEST_QUIET_MS 250

/** Maximum number of bytes shown in the multiline paste safety preview. */
#define CLIPBOARD_PREVIEW_MAX           1024
/** Maximum number of bytes converted for extra-verbose payload logging. */
#define CLIPBOARD_VERBOSE_MAX           _64K


/** Long option identifiers used by RTGetOpt for clipboard subcommands. */
enum
{
    /** --format input MIME type or listen output format. */
    CLIPBOARD_OPT_FORMAT = 1000,
    /** --raw input mode for copy. */
    CLIPBOARD_OPT_RAW,
    /** --timeout maximum wait time. */
    CLIPBOARD_OPT_TIMEOUT,
    /** --wait alias used by paste for waiting on guest data. */
    CLIPBOARD_OPT_WAIT,
    /** --count maximum number of listen events. */
    CLIPBOARD_OPT_COUNT,
    /** --events listen event filter list. */
    CLIPBOARD_OPT_EVENTS,
    /** --format output selector for listen. */
    CLIPBOARD_OPT_OUTPUT_FORMAT,
    /** --machinereadable output mode. */
    CLIPBOARD_OPT_MACHINE_READABLE,
    /** --direction transfer direction filter. */
    CLIPBOARD_OPT_DIRECTION,
    /** --verbose diagnostic output flag. */
    CLIPBOARD_OPT_VERBOSE
};


/** Output formats supported by the clipboard listen subcommand. */
typedef enum CLIPBOARDLISTENFMT
{
    /** Human-readable event output. */
    CLIPBOARDLISTENFMT_HUMAN = 0,
    /** Shell-friendly key/value event output. */
    CLIPBOARDLISTENFMT_MACHINE_READABLE,
    /** JSON event output. */
    CLIPBOARDLISTENFMT_JSON
} CLIPBOARDLISTENFMT;


typedef struct SHCLHANDLESERVESTATE
{
    SHCLHANDLESERVESTATE()
        : fHaveClipboardId(false)
        , uClipboardId(0)
    { }

    bool                 fHaveClipboardId;
    ULONG                uClipboardId;
} SHCLHANDLESERVESTATE;


typedef struct SHCLCLIPBOARDEVENTINFO
{
    SHCLCLIPBOARDEVENTINFO()
        : fHaveRevision(false)
        , iRevision(0)
        , fHaveClientId(false)
        , uClientId(VBOX_SHCL_MAIN_CLIENT_NONE)
    { }

    bool                 fHaveRevision;
    LONG64               iRevision;
    bool                 fHaveClientId;
    ULONG                uClientId;
} SHCLCLIPBOARDEVENTINFO;


typedef struct SHCLHANDLELISTENSTATE
{
    SHCLHANDLELISTENSTATE()
        : fHaveSource(false)
        , enmSource(ClipboardSource_Custom)
    { }

    /** Whether enmSource contains the last source reported by an event. */
    bool                 fHaveSource;
    /** Last clipboard source reported by an event. */
    ClipboardSource_T    enmSource;
} SHCLHANDLELISTENSTATE;


/** Current verbosity level for clipboard command diagnostics. */
static unsigned g_uVerbosity = 0;


/**
 * Writes a verbose diagnostic line to stderr.
 *
 * @param   pszFormat       Format string.
 * @param   ...             Format arguments.
 */
static void shclVerbose(const char *pszFormat, ...)
{
    if (!g_uVerbosity)
        return;

    RTStrmPrintf(g_pStdErr, "clipboard: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);
    RTStrmPutCh(g_pStdErr, '\n');
    RTStrmFlush(g_pStdErr);
}


/**
 * Writes a non-verbose status line to stderr.
 *
 * @param   pszFormat       Format string.
 * @param   ...             Format arguments.
 */
static void shclInfo(const char *pszFormat, ...)
{
    if (g_uVerbosity)
        return;

    RTStrmPrintf(g_pStdErr, "clipboard: ");
    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);
    RTStrmPutCh(g_pStdErr, '\n');
    RTStrmFlush(g_pStdErr);
}


/**
 * Creates an anonymous clipboard API session.
 *
 * @returns COM status code.
 * @param   ptrClipboard        Clipboard object to create the session on.
 * @param   fExcludeOwnChanges  Whether to suppress changes made by the session.
 * @param   fExcludeReflections Whether to suppress reflected changes.
 * @param   fIncludeInitialState Whether to request initial clipboard state events.
 * @param   fIncludePayload     Whether clipboard data events should include payloads.
 * @param   ptrSession          Where to return the created session.
 */
static HRESULT shclCreateSession(const ComPtr<IClipboard> &ptrClipboard,
                                 bool fExcludeOwnChanges,
                                 bool fExcludeReflections,
                                 bool fIncludeInitialState,
                                 bool fIncludePayload,
                                 ComPtr<IClipboardSession> &ptrSession)
{
    SafeArray<IClipboardSessionFlag_T> aFlags;
    if (fExcludeOwnChanges)
        aFlags.push_back(IClipboardSessionFlag_ExcludeOwnChanges);
    if (fExcludeReflections)
        aFlags.push_back(IClipboardSessionFlag_ExcludeReflections);
    if (fIncludeInitialState)
        aFlags.push_back(IClipboardSessionFlag_IncludeInitialState);
    if (fIncludePayload)
        aFlags.push_back(IClipboardSessionFlag_IncludePayload);

    return ptrClipboard->CreateSession(ComSafeArrayAsInParam(aFlags), ptrSession.asOutParam());
}


/**
 * Reads optional clipboard-event identity fields.
 *
 * @returns true if at least one identity field was available.
 * @param   ptrEvent            Event to inspect.
 * @param   pInfo               Where to store the identity fields.
 */
template <class EventIface>
static bool shclGetClipboardEventInfoFrom(const ComPtr<IEvent> &ptrEvent, SHCLCLIPBOARDEVENTINFO *pInfo)
{
    ComPtr<EventIface> ptrClipboardEvent = ptrEvent;
    if (ptrClipboardEvent.isNull())
        return false;

    LONG64 iRevision = 0;
    HRESULT hrc = ptrClipboardEvent->COMGETTER(Revision)(&iRevision);
    if (SUCCEEDED(hrc))
    {
        pInfo->fHaveRevision = true;
        pInfo->iRevision = iRevision;
    }

    ULONG uClientId = VBOX_SHCL_MAIN_CLIENT_NONE;
    hrc = ptrClipboardEvent->COMGETTER(ClientId)(&uClientId);
    if (SUCCEEDED(hrc))
    {
        pInfo->fHaveClientId = true;
        pInfo->uClientId = uClientId;
    }

    return pInfo->fHaveRevision || pInfo->fHaveClientId;
}


static bool shclGetClipboardEventInfo(const ComPtr<IEvent> &ptrEvent, SHCLCLIPBOARDEVENTINFO *pInfo)
{
    AssertPtrReturn(pInfo, false);
    *pInfo = SHCLCLIPBOARDEVENTINFO();

    if (shclGetClipboardEventInfoFrom<IClipboardEvent>(ptrEvent, pInfo))
        return true;
    if (shclGetClipboardEventInfoFrom<IClipboardModeChangedEvent>(ptrEvent, pInfo))
        return true;
    if (shclGetClipboardEventInfoFrom<IClipboardFileTransferModeChangedEvent>(ptrEvent, pInfo))
        return true;
    if (shclGetClipboardEventInfoFrom<IClipboardSourceChangedEvent>(ptrEvent, pInfo))
        return true;
    if (shclGetClipboardEventInfoFrom<IClipboardTransferEvent>(ptrEvent, pInfo))
        return true;

    return false;
}


static void shclHandleListenJsonString(const char *pszValue);


/**
 * Starts a uniformly ordered clipboard-listen event record.
 *
 * The caller appends event-specific fields and terminates the record.
 *
 * @param   enmFormat           Output format.
 * @param   pInfo               Event identity fields, optional.
 * @param   pszSource           Clipboard source, optional.
 * @param   pszEvent            Event name.
 * @param   pszAction           Clipboard action, optional.
 */
static void shclHandleListenPrintPrefix(CLIPBOARDLISTENFMT enmFormat,
                                        const SHCLCLIPBOARDEVENTINFO *pInfo,
                                        const char *pszSource,
                                        const char *pszEvent,
                                        const char *pszAction)
{
    AssertPtrReturnVoid(pszEvent);

    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        const char *pszSeparator = "";
        RTStrmPutCh(g_pStdOut, '{');
        if (pInfo && pInfo->fHaveRevision)
        {
            RTStrmPrintf(g_pStdOut, "\"revision\":%RI64", (int64_t)pInfo->iRevision);
            pszSeparator = ",";
        }
        if (pszSource)
        {
            RTStrmPrintf(g_pStdOut, "%s\"source\":", pszSeparator);
            shclHandleListenJsonString(pszSource);
            pszSeparator = ",";
        }
        if (g_uVerbosity && pInfo && pInfo->fHaveClientId)
        {
            RTStrmPrintf(g_pStdOut, "%s\"clientId\":%RU32", pszSeparator, (uint32_t)pInfo->uClientId);
            pszSeparator = ",";
        }
        RTStrmPrintf(g_pStdOut, "%s\"event\":", pszSeparator);
        shclHandleListenJsonString(pszEvent);
        if (pszAction)
        {
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"action\":"));
            shclHandleListenJsonString(pszAction);
        }
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        const char *pszSeparator = "";
        if (pInfo && pInfo->fHaveRevision)
        {
            RTPrintf("revision=\"%RI64\"", (int64_t)pInfo->iRevision);
            pszSeparator = " ";
        }
        if (pszSource)
        {
            RTPrintf("%ssource=\"%s\"", pszSeparator, pszSource);
            pszSeparator = " ";
        }
        if (g_uVerbosity && pInfo && pInfo->fHaveClientId)
        {
            RTPrintf("%sclientId=\"%RU32\"", pszSeparator, (uint32_t)pInfo->uClientId);
            pszSeparator = " ";
        }
        RTPrintf("%sevent=\"%s\"", pszSeparator, pszEvent);
        if (pszAction)
            RTPrintf(" action=\"%s\"", pszAction);
    }
    else
    {
        const char *pszSeparator = "";
        if (pInfo && pInfo->fHaveRevision)
        {
            RTPrintf("rev=%RI64", (int64_t)pInfo->iRevision);
            pszSeparator = " ";
        }
        if (pszSource)
        {
            RTPrintf("%ssrc=%s", pszSeparator, pszSource);
            pszSeparator = " ";
        }
        if (g_uVerbosity && pInfo && pInfo->fHaveClientId)
        {
            RTPrintf("%scid=%RU32", pszSeparator, (uint32_t)pInfo->uClientId);
            pszSeparator = " ";
        }
        RTPrintf("%sevent=%s", pszSeparator, pszEvent);
        if (pszAction)
            RTPrintf(" action=%s", pszAction);
    }
}


static const char *shclEventTypeToString(VBoxEventType_T enmType);


/**
 * Logs a received clipboard event with optional identity metadata.
 *
 * @param   pszCommand          Clipboard subcommand name.
 * @param   pszOperation        Operation text.
 * @param   enmType             Event type.
 * @param   ptrEvent            Event object to inspect.
 */
static void shclVerboseClipboardEvent(const char *pszCommand, const char *pszOperation,
                                      VBoxEventType_T enmType, const ComPtr<IEvent> &ptrEvent)
{
    if (!g_uVerbosity)
        return;

    SHCLCLIPBOARDEVENTINFO Info;
    shclGetClipboardEventInfo(ptrEvent, &Info);
    if (Info.fHaveRevision && Info.fHaveClientId)
        shclVerbose("%s: %s '%s' revision=%RI64 clientId=%RU32", pszCommand, pszOperation,
                    shclEventTypeToString(enmType), (int64_t)Info.iRevision, (uint32_t)Info.uClientId);
    else if (Info.fHaveRevision)
        shclVerbose("%s: %s '%s' revision=%RI64", pszCommand, pszOperation,
                    shclEventTypeToString(enmType), (int64_t)Info.iRevision);
    else if (Info.fHaveClientId)
        shclVerbose("%s: %s '%s' clientId=%RU32", pszCommand, pszOperation,
                    shclEventTypeToString(enmType), (uint32_t)Info.uClientId);
    else
        shclVerbose("%s: %s '%s'", pszCommand, pszOperation, shclEventTypeToString(enmType));
}


/** Set by the signal handler when a long-running clipboard command should stop. */
static bool volatile g_fClipboardCanceled = false;
#ifdef RT_OS_WINDOWS
/**
 * Console control handler for gracefully stopping long-running clipboard commands.
 *
 * @param   dwCtrlType      Console control event type.
 */
static BOOL WINAPI shclSignalHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
            ASMAtomicWriteBool(&g_fClipboardCanceled, true);
            fEventHandled = TRUE;
            break;
        default:
            break;
    }

    return fEventHandled;
}
#elif !defined(RT_OS_OS2)
/** Signal handler function pointer type. */
typedef void (*PFNSHCLSIGNALHANDLER)(int);
/** Previous SIGINT handler restored after a long-running clipboard command exits. */
static PFNSHCLSIGNALHANDLER g_pfnClipboardSigIntOld = SIG_DFL;
/** Previous SIGTERM handler restored after a long-running clipboard command exits. */
static PFNSHCLSIGNALHANDLER g_pfnClipboardSigTermOld = SIG_DFL;
# ifdef SIGBREAK
/** Previous SIGBREAK handler restored after a long-running clipboard command exits. */
static PFNSHCLSIGNALHANDLER g_pfnClipboardSigBreakOld = SIG_DFL;
# endif


/**
 * Signal handler for gracefully stopping long-running clipboard commands.
 *
 * @param   iSignal         Signal number.
 */
static void shclSignalHandler(int iSignal) RT_NOTHROW_DEF
{
    RT_NOREF(iSignal);
    ASMAtomicWriteBool(&g_fClipboardCanceled, true);
}
#endif /* !RT_OS_WINDOWS && !RT_OS_OS2 */


/** Installs signal handling for long-running clipboard commands. */
static void shclSignalHandlerInstall(void)
{
    ASMAtomicWriteBool(&g_fClipboardCanceled, false);
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)shclSignalHandler, TRUE /* Add handler */))
        RTMsgError(Clipboard::tr("Unable to install console control handler: %Rrc\n"),
                   RTErrConvertFromWin32(GetLastError()));
#elif !defined(RT_OS_OS2)
    g_pfnClipboardSigIntOld = signal(SIGINT, shclSignalHandler);
    g_pfnClipboardSigTermOld = signal(SIGTERM, shclSignalHandler);
# ifdef SIGBREAK
    g_pfnClipboardSigBreakOld = signal(SIGBREAK, shclSignalHandler);
# endif
#endif
}


/** Uninstalls signal handling for long-running clipboard commands. */
static void shclSignalHandlerUninstall(void)
{
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)NULL, FALSE /* Remove handler */))
        RTMsgError(Clipboard::tr("Unable to uninstall console control handler: %Rrc\n"),
                   RTErrConvertFromWin32(GetLastError()));
#elif !defined(RT_OS_OS2)
    if (g_pfnClipboardSigIntOld != SIG_ERR)
        (void)signal(SIGINT, g_pfnClipboardSigIntOld);
    if (g_pfnClipboardSigTermOld != SIG_ERR)
        (void)signal(SIGTERM, g_pfnClipboardSigTermOld);
# ifdef SIGBREAK
    if (g_pfnClipboardSigBreakOld != SIG_ERR)
        (void)signal(SIGBREAK, g_pfnClipboardSigBreakOld);
# endif
#endif
}


/**
 * Checks whether signal handling requested cancellation.
 *
 * @returns true if the current clipboard command should stop.
 */
static bool shclSignalWasCaught(void)
{
    return ASMAtomicReadBool(&g_fClipboardCanceled);
}


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static HRESULT shclGetFormatMimeType(const ComPtr<IClipboardFormat> &ptrFormat, Utf8Str &strMimeType);
static bool shclMimeEquivalent(const Utf8Str &strRequested, const Utf8Str &strActual);
static void shclClearHostClipboard(const ComPtr<IHostClipboard> &ptrHostClipboard);




/**
 * Gets the live clipboard object for a running VM.
 *
 * @returns COM status code.
 * @param   pArg            Command handler arguments.
 * @param   pszMachine      Machine name or UUID.
 * @param   ptrClipboard    Where to return the clipboard object.
 */
static HRESULT shclGet(HandlerArg *pArg, const char *pszMachine, ComPtr<IClipboard> &ptrClipboard)
{
    HRESULT hrc;
    ComPtr<IMachine> ptrMachine;
    CHECK_ERROR2_RET(hrc, pArg->virtualBox, FindMachine(Bstr(pszMachine).raw(), ptrMachine.asOutParam()), hrc);

    CHECK_ERROR2_RET(hrc, ptrMachine, LockMachine(pArg->session, LockType_Shared), hrc);

    ComPtr<IConsole> ptrConsole;
    CHECK_ERROR2_RET(hrc, pArg->session, COMGETTER(Console)(ptrConsole.asOutParam()), hrc);
    if (ptrConsole.isNull())
    {
        RTMsgError(Clipboard::tr("Machine '%s' is not currently running."), pszMachine);
        return E_FAIL;
    }

    CHECK_ERROR2_RET(hrc, ptrConsole, COMGETTER(Clipboard)(ptrClipboard.asOutParam()), hrc);
    if (ptrClipboard.isNull())
    {
        RTMsgError(Clipboard::tr("Machine '%s' has no live clipboard object."), pszMachine);
        return E_FAIL;
    }
    return hrc;
}


/**
 * Gets the Shared Clipboard mode from the machine locked by the current session.
 *
 * @returns COM status code.
 * @param   pArg            Command handler arguments.
 * @param   penmMode        Where to return the Shared Clipboard mode.
 */
static HRESULT shclGetMode(HandlerArg *pArg, ClipboardMode_T *penmMode)
{
    AssertPtrReturn(pArg, E_POINTER);
    AssertPtrReturn(penmMode, E_POINTER);

    ComPtr<IMachine> ptrMachine;
    HRESULT hrc = pArg->session->COMGETTER(Machine)(ptrMachine.asOutParam());
    if (FAILED(hrc))
        return hrc;
    if (ptrMachine.isNull())
        return E_FAIL;

    ComPtr<IClipboardSettings> ptrClipboardSettings;
    hrc = ptrMachine->COMGETTER(Clipboard)(ptrClipboardSettings.asOutParam());
    if (FAILED(hrc))
        return hrc;
    if (ptrClipboardSettings.isNull())
        return E_FAIL;

    return ptrClipboardSettings->COMGETTER(Mode)(penmMode);
}


/**
 * Opens the VM clipboard settings object for modification.
 *
 * @returns COM status code.
 * @param   pArg            Command handler arguments.
 * @param   pszMachine      Machine name or UUID.
 * @param   ptrMachine      Where to return the session machine.
 * @param   ptrClipboard    Where to return the clipboard settings object.
 */
static HRESULT shclGetSettings(HandlerArg *pArg, const char *pszMachine, ComPtr<IMachine> &ptrMachine,
                               ComPtr<IClipboardSettings> &ptrClipboard)
{
    HRESULT hrc;
    ComPtr<IMachine> ptrFoundMachine;
    CHECK_ERROR2_RET(hrc, pArg->virtualBox, FindMachine(Bstr(pszMachine).raw(), ptrFoundMachine.asOutParam()), hrc);

    CHECK_ERROR2_RET(hrc, ptrFoundMachine, LockMachine(pArg->session, LockType_Shared), hrc);

    CHECK_ERROR2(hrc, pArg->session, COMGETTER(Machine)(ptrMachine.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        CHECK_ERROR2(hrc, ptrMachine, COMGETTER(Clipboard)(ptrClipboard.asOutParam()));
    }
    if (FAILED(hrc))
        pArg->session->UnlockMachine();
    return hrc;
}


/**
 * Reads all clipboard payload bytes from standard input.
 *
 * @returns VBox status code.
 * @param   abData          Where to append the bytes read from standard input.
 */
static int shclReadStdin(std::vector<BYTE> &abData)
{
    RTStrmSetMode(g_pStdIn, true /* fBinary */, -1 /* fCurrentCodeSet */);

    for (;;)
    {
        BYTE abBuf[_64K];
        size_t cbRead = 0;
        int vrc = RTStrmReadEx(g_pStdIn, abBuf, sizeof(abBuf), &cbRead);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_EOF)
                return VINF_SUCCESS;
            return vrc;
        }
        if (!cbRead)
            return VINF_SUCCESS;
        try
        {
            abData.insert(abData.end(), &abBuf[0], &abBuf[cbRead]);
        }
        catch (std::bad_alloc &)
        {
            return VERR_NO_MEMORY;
        }
    }
}


/**
 * Prints an escaped preview of clipboard payload bytes.
 *
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 */
static void shclPrintEscapedPreview(const BYTE *pbData, size_t cbData)
{
    size_t const cbPreview = RT_MIN(cbData, (size_t)CLIPBOARD_PREVIEW_MAX);
    RTStrmPrintf(g_pStdErr, "%s\n", Clipboard::tr("Clipboard text preview:"));
    RTStrmPrintf(g_pStdErr, "---\n");
    for (size_t i = 0; i < cbPreview; i++)
    {
        BYTE const b = pbData[i];
        switch (b)
        {
            case '\n': RTStrmPrintf(g_pStdErr, "\\n\n"); break;
            case '\r': RTStrmPrintf(g_pStdErr, "\\r"); break;
            case '\t': RTStrmPrintf(g_pStdErr, "\\t"); break;
            case '\\': RTStrmPrintf(g_pStdErr, "\\\\"); break;
            default:
                if (RT_C_IS_PRINT(b))
                    RTStrmPutCh(g_pStdErr, b);
                else
                    RTStrmPrintf(g_pStdErr, "\\x%02x", b);
                break;
        }
    }
    if (cbPreview < cbData)
        RTStrmPrintf(g_pStdErr, "\n%s", Clipboard::tr("... (preview truncated)"));
    RTStrmPrintf(g_pStdErr, "\n---\n");
}


/**
 * Prompts before writing multiline clipboard text to an interactive terminal.
 *
 * @returns RTEXITCODE_SUCCESS if output may proceed, failure otherwise.
 * @param   strMimeType     MIME type of the payload.
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 */
static RTEXITCODE shclConfirmMultilineTerminalPaste(const Utf8Str &strMimeType, const BYTE *pbData, size_t cbData)
{
    if (   !RTStrmIsTerminal(g_pStdOut)
        || !ShClHlpIsMultilineText(strMimeType.c_str(), pbData, cbData))
        return RTEXITCODE_SUCCESS;

    RTMsgWarning(Clipboard::tr("The clipboard contains multiline text. Writing it to an interactive terminal could be "
                                "unsafe if it is pasted or interpreted by a shell."));
    shclPrintEscapedPreview(pbData, cbData);

    if (!RTStrmIsTerminal(g_pStdIn))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Refusing to write multiline clipboard text to the terminal without confirmation. "
                                            "Redirect stdout to a file or pipe to avoid this prompt."));

    char szAnswer[16];
    RTStrmPrintf(g_pStdErr, "%s ", Clipboard::tr("Write this text to stdout? [y/N]"));
    int vrc = RTStrmGetLine(g_pStdIn, szAnswer, sizeof(szAnswer));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read confirmation: %Rrc"), vrc);
    if (   !RTStrICmp(szAnswer, "y")
        || !RTStrICmp(szAnswer, "yes"))
        return RTEXITCODE_SUCCESS;

    return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard output cancelled."));
}


/**
 * Writes clipboard payload bytes to standard output.
 *
 * @returns VBox status code.
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 */
static int shclWriteStdout(const BYTE *pbData, size_t cbData)
{
    RTStrmSetMode(g_pStdOut, true /* fBinary */, -1 /* fCurrentCodeSet */);
    return cbData ? RTStrmWrite(g_pStdOut, pbData, cbData) : VINF_SUCCESS;
}



/**
 * Extracts data and metadata from a clipboard item.
 *
 * @returns COM status code.
 * @param   ptrItem         Clipboard item to inspect.
 * @param   strMimeType     Where to return the MIME type.
 * @param   penmSource      Where to return the source, optional.
 * @param   aBuffer         Where to return the payload bytes.
 */
static HRESULT shclGetItemData(const ComPtr<IClipboardItem> &ptrItem, Utf8Str &strMimeType,
                               ClipboardSource_T *penmSource, SafeArray<BYTE> &aBuffer)
{
    ClipboardSource_T enmSource = ClipboardSource_Host;
    HRESULT hrc = ptrItem->COMGETTER(Source)(&enmSource);
    if (FAILED(hrc))
        return hrc;

    ComPtr<IClipboardFormat> ptrFormat;
    hrc = ptrItem->COMGETTER(Format)(ptrFormat.asOutParam());
    if (FAILED(hrc))
        return hrc;
    Bstr bstrMimeType;
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
        return hrc;

    aBuffer.setNull();
    hrc = ptrItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aBuffer));
    if (FAILED(hrc))
        return hrc;

    strMimeType = bstrMimeType;
    if (penmSource)
        *penmSource = enmSource;
    return S_OK;
}


/**
 * Returns the public Shared Clipboard format preference for a MIME type.
 *
 * This mirrors Main's ReadDataRaw() auto-selection order and supported MIME
 * mappings.
 *
 * @returns Format priority, UINT32_MAX if unsupported.
 * @param   strMimeType     MIME type to classify.
 */
static uint32_t shclMimePriority(const Utf8Str &strMimeType)
{
    const char *pszMimeType = strMimeType.c_str();
    if (   !RTStrICmp(pszMimeType, "text/plain")
        || !RTStrNICmp(pszMimeType, "text/plain;", sizeof("text/plain;") - 1)
        || !RTStrICmp(pszMimeType, "text/plain;charset=utf-8")
        || !RTStrICmp(pszMimeType, "text/plain;charset=UTF-8"))
        return 0;
    if (   !RTStrICmp(pszMimeType, "text/html")
        || !RTStrNICmp(pszMimeType, "text/html;", sizeof("text/html;") - 1))
        return 1;
    if (   !RTStrICmp(pszMimeType, "image/bmp")
        || !RTStrICmp(pszMimeType, "image/x-bmp")
        || !RTStrICmp(pszMimeType, "application/x-bmp"))
        return 2;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (   !RTStrICmp(pszMimeType, "text/uri-list")
        || !RTStrICmp(pszMimeType, "application/x-virtualbox-shared-clipboard-uri-list"))
        return 3;
#endif
    return UINT32_MAX;
}


/**
 * Checks whether two MIME types map to the same public Shared Clipboard format.
 *
 * @returns true if the MIME types are equivalent supported clipboard formats.
 * @param   strRequested    Requested MIME type.
 * @param   strActual       Actual MIME type.
 */
static bool shclMimeEquivalent(const Utf8Str &strRequested, const Utf8Str &strActual)
{
    uint32_t const uRequested = shclMimePriority(strRequested);
    return uRequested != UINT32_MAX && uRequested == shclMimePriority(strActual);
}


/**
 * Checks whether a format array contains a MIME type equivalent to a requested type.
 *
 * @returns true if an equivalent MIME type is present, false otherwise.
 * @param   strRequested    Requested MIME type.
 * @param   aFormats        Formats to inspect.
 */
static bool shclMimeTypeIsInFormatArray(const Utf8Str &strRequested, const SafeIfaceArray<IClipboardFormat> &aFormats)
{
    for (size_t i = 0; i < aFormats.size(); ++i)
    {
        Utf8Str strMimeType;
        HRESULT hrc = shclGetFormatMimeType(aFormats[i], strMimeType);
        if (SUCCEEDED(hrc) && shclMimeEquivalent(strRequested, strMimeType))
            return true;
    }
    return false;
}


/**
 * Checks whether an actual MIME type matches the optional requested type.
 *
 * @returns true on match, false otherwise.
 * @param   pszRequested    Requested MIME type, optional.
 * @param   strActual       Actual MIME type.
 */
static bool shclMimeMatches(const char *pszRequested, const Utf8Str &strActual)
{
    if (!pszRequested || !*pszRequested)
        return true;
    return shclMimeEquivalent(Utf8Str(pszRequested), strActual);
}


/**
 * Selects the preferred format for ReadDataRaw() auto-selection.
 *
 * @returns COM status code.
 * @param   aFormats        Candidate clipboard formats.
 * @param   ptrFormat       Where to return the selected format.
 * @param   strMimeType     Where to return the selected MIME type.
 */
static HRESULT shclSelectPreferredFormat(const SafeIfaceArray<IClipboardFormat> &aFormats,
                                         ComPtr<IClipboardFormat> &ptrFormat,
                                         Utf8Str &strMimeType)
{
    ptrFormat.setNull();
    strMimeType.setNull();

    uint32_t uBestPriority = UINT32_MAX;
    Utf8Str strBestMimeType;
    ComPtr<IClipboardFormat> ptrBestFormat;
    for (size_t i = 0; i < aFormats.size(); ++i)
    {
        Utf8Str strThisMimeType;
        HRESULT hrc = shclGetFormatMimeType(aFormats[i], strThisMimeType);
        if (FAILED(hrc))
            return hrc;

        uint32_t const uPriority = shclMimePriority(strThisMimeType);
        if (uPriority < uBestPriority)
        {
            uBestPriority = uPriority;
            strBestMimeType = strThisMimeType;
            ptrBestFormat = aFormats[i];
        }
    }

    if (ptrBestFormat.isNull())
        return VBOX_E_SHCL_FORMAT_NOT_SUPPORTED;

    ptrFormat = ptrBestFormat;
    strMimeType = strBestMimeType;
    return S_OK;
}


/**
 * Keeps only formats VBoxManage serve can read back through the public API.
 *
 * @returns COM status code.
 * @param   aFormats            Candidate clipboard formats.
 * @param   pvecReportFormats   Where to return the supported subset.
 * @param   pvecReportMimeTypes Where to return the supported MIME types, optional.
 * @param   pstrPreferredMimeType Where to return the preferred MIME type, optional.
 */
static HRESULT shclFilterSupportedFormats(const SafeIfaceArray<IClipboardFormat> &aFormats,
                                          std::vector<ComPtr<IClipboardFormat> > *pvecReportFormats,
                                          std::vector<Utf8Str> *pvecReportMimeTypes,
                                          Utf8Str *pstrPreferredMimeType)
{
    AssertPtrReturn(pvecReportFormats, E_POINTER);
    pvecReportFormats->clear();
    if (pvecReportMimeTypes)
        pvecReportMimeTypes->clear();

    uint32_t uBestPriority = UINT32_MAX;
    Utf8Str strBestMimeType;

    for (size_t i = 0; i < aFormats.size(); ++i)
    {
        Utf8Str strMimeType;
        HRESULT hrc = shclGetFormatMimeType(aFormats[i], strMimeType);
        if (FAILED(hrc))
            return hrc;

        uint32_t const uPriority = shclMimePriority(strMimeType);
        if (uPriority != UINT32_MAX)
        {
            pvecReportFormats->push_back(aFormats[i]);
            if (pvecReportMimeTypes)
                pvecReportMimeTypes->push_back(strMimeType);
            if (uPriority < uBestPriority)
            {
                uBestPriority = uPriority;
                strBestMimeType = strMimeType;
            }
        }
    }

    if (pvecReportFormats->empty())
        return VBOX_E_SHCL_FORMAT_NOT_SUPPORTED;

    if (pstrPreferredMimeType)
        *pstrPreferredMimeType = strBestMimeType;
    return S_OK;
}




/**
 * Writes clipboard data for the paste command after validating its format.
 *
 * @returns Process exit code.
 * @param   strMimeType     MIME type of the payload.
 * @param   pszFormat       Requested MIME type, optional.
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 */
static RTEXITCODE shclHandlePasteOutputData(const Utf8Str &strMimeType, const char *pszFormat, const BYTE *pbData, size_t cbData)
{
    if (!shclMimeMatches(pszFormat, strMimeType))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Clipboard data has MIME type '%s', not requested MIME type '%s'."),
                              strMimeType.c_str(), pszFormat);

    RTEXITCODE rcExit = shclConfirmMultilineTerminalPaste(strMimeType, pbData, cbData);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    int vrc = shclWriteStdout(pbData, cbData);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to write clipboard data: %Rrc"), vrc);

    return RTEXITCODE_SUCCESS;
}


/**
 * Writes a clipboard item for the paste command.
 *
 * @returns Process exit code.
 * @param   ptrItem         Clipboard item to output.
 * @param   pszFormat       Requested MIME type, optional.
 */
static RTEXITCODE shclHandlePasteOutputItem(const ComPtr<IClipboardItem> &ptrItem, const char *pszFormat)
{
    Utf8Str strMimeType;
    ClipboardSource_T enmSource;
    SafeArray<BYTE> aBuffer;
    HRESULT hrc = shclGetItemData(ptrItem, strMimeType, &enmSource, aBuffer);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to get clipboard data: %Rhrc"), hrc);

    if (enmSource != ClipboardSource_Guest)
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Clipboard paste only outputs guest-origin data; current data source is %RU32."),
                              (uint32_t)enmSource);
    return shclHandlePasteOutputData(strMimeType, pszFormat, aBuffer.raw(), aBuffer.size());
}


/**
 * Writes a JSON string value to standard output.
 *
 * @param   pszValue        String value to write, optional.
 * @param   cchValue        Number of bytes to write.
 */
static void shclHandleListenJsonStringN(const char *pszValue, size_t cchValue)
{
    RTStrmPutCh(g_pStdOut, '"');
    if (pszValue)
    {
        size_t offPending = 0;
        for (size_t i = 0; i < cchValue; i++)
        {
            unsigned char const ch = (unsigned char)pszValue[i];
            const char *pszEscape = NULL;
            size_t cchEscape = 0;
            switch (ch)
            {
                case '\\': pszEscape = "\\\\"; cchEscape = 2; break;
                case '"':  pszEscape = "\\\""; cchEscape = 2; break;
                case '\n': pszEscape = "\\n";  cchEscape = 2; break;
                case '\r': pszEscape = "\\r";  cchEscape = 2; break;
                case '\t': pszEscape = "\\t";  cchEscape = 2; break;
                default:
                    if (ch < 0x20)
                    {
                        if (i > offPending)
                            RTStrmWrite(g_pStdOut, &pszValue[offPending], i - offPending);
                        RTStrmPrintf(g_pStdOut, "\\u%04x", ch);
                        offPending = i + 1;
                    }
                    break;
            }
            if (pszEscape)
            {
                if (i > offPending)
                    RTStrmWrite(g_pStdOut, &pszValue[offPending], i - offPending);
                RTStrmWrite(g_pStdOut, pszEscape, cchEscape);
                offPending = i + 1;
            }
        }
        if (offPending < cchValue)
            RTStrmWrite(g_pStdOut, &pszValue[offPending], cchValue - offPending);
    }
    RTStrmPutCh(g_pStdOut, '"');
}


/**
 * Writes a zero-terminated JSON string value to standard output.
 *
 * @param   pszValue        String value to write, optional.
 */
static void shclHandleListenJsonString(const char *pszValue)
{
    shclHandleListenJsonStringN(pszValue, pszValue ? strlen(pszValue) : 0);
}


/**
 * Writes an escaped quoted string value to standard output.
 *
 * @param   pszValue        String value to write, optional.
 */
static void shclHandleListenQuotedString(const char *pszValue)
{
    RTStrmPutCh(g_pStdOut, '"');
    if (pszValue)
        ShClHlpPrintEscapedString(g_pStdOut, pszValue, strlen(pszValue));
    RTStrmPutCh(g_pStdOut, '"');
}


/**
 * Prints the startup warning used when Shared Clipboard is disabled.
 *
 * @param   enmFormat       Output format.
 */
static void shclHandleListenPrintDisabledWarning(CLIPBOARDLISTENFMT enmFormat)
{
    const char *pszMessage = Clipboard::tr("Shared Clipboard is disabled for this VM.");

    shclHandleListenPrintPrefix(enmFormat, NULL /* pInfo */, NULL /* pszSource */,
                                "warning", NULL /* pszAction */);
    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"mode\":\"disabled\",\"message\":"));
        shclHandleListenJsonString(pszMessage);
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" mode=\"disabled\" message="));
        shclHandleListenQuotedString(pszMessage);
        RTStrmPutCh(g_pStdOut, '\n');
    }
    else
    {
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" mode=disabled message="));
        shclHandleListenQuotedString(pszMessage);
        RTStrmPutCh(g_pStdOut, '\n');
    }
}


/**
 * Converts a bounded UTF-8 or UTF-16 clipboard payload to UTF-8 for verbose event output.
 *
 * @returns true if UTF-8 text was returned, false if the format is unsupported or invalid.
 * @param   strMimeType     MIME type of the payload.
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 * @param   ppszText        Where to return allocated UTF-8 text. Free with RTStrFree().
 * @param   pcchText        Where to return the UTF-8 byte length.
 * @param   pfTruncated     Where to return whether input was truncated to CLIPBOARD_VERBOSE_MAX.
 */
static bool shclGetVerboseText(const Utf8Str &strMimeType, const BYTE *pbData, size_t cbData, char **ppszText,
                               size_t *pcchText, bool *pfTruncated)
{
    *ppszText = NULL;
    *pcchText = 0;
    *pfTruncated = cbData > CLIPBOARD_VERBOSE_MAX;

    size_t cbText = RT_MIN(cbData, (size_t)CLIPBOARD_VERBOSE_MAX);
    if (ShClHlpIsUtf8TextMimeType(strMimeType.c_str()))
    {
        if (!cbText)
        {
            *ppszText = RTStrDup("");
            return *ppszText != NULL;
        }

        size_t cchText = cbText;
        int vrc = RTStrValidateEncodingEx((const char *)pbData, cchText, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        for (unsigned i = 0; RT_FAILURE(vrc) && *pfTruncated && i < 3 && cchText > 0; i++)
        {
            cchText--;
            vrc = RTStrValidateEncodingEx((const char *)pbData, cchText, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        }
        if (RT_FAILURE(vrc))
            return false;

        *ppszText = RTStrDupN((const char *)pbData, cchText);
        if (!*ppszText)
            return false;
        *pcchText = cchText;
        return true;
    }

    if (!ShClHlpIsUtf16TextMimeType(strMimeType.c_str()))
        return false;

    cbText &= ~(size_t)1;
    if (!cbText)
    {
        *ppszText = RTStrDup("");
        return *ppszText != NULL;
    }

    size_t const cwcText = cbText / sizeof(RTUTF16);
    try
    {
        std::vector<RTUTF16> awcText(cwcText + 1);
        if (cbText)
            memcpy(&awcText[0], pbData, cbText);
        awcText[cwcText] = 0;

        PCRTUTF16 pwszText = &awcText[0];
        size_t cwcConvert = cwcText;
        bool fUtf16BE = RTStrIStr(strMimeType.c_str(), "charset=utf-16be") != NULL;
        bool fUtf16LE = RTStrIStr(strMimeType.c_str(), "charset=utf-16le") != NULL;
        if (cbText >= 2 && pbData[0] == 0xff && pbData[1] == 0xfe)
        {
            pwszText++;
            cwcConvert--;
            fUtf16LE = true;
            fUtf16BE = false;
        }
        else if (cbText >= 2 && pbData[0] == 0xfe && pbData[1] == 0xff)
        {
            pwszText++;
            cwcConvert--;
            fUtf16BE = true;
            fUtf16LE = false;
        }

        int vrc;
        if (fUtf16BE)
            vrc = RTUtf16BigToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
        else if (fUtf16LE)
            vrc = RTUtf16LittleToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
        else
            vrc = RTUtf16ToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
        if (RT_FAILURE(vrc) && *pfTruncated && cwcConvert > 0)
        {
            cwcConvert--;
            if (fUtf16BE)
                vrc = RTUtf16BigToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
            else if (fUtf16LE)
                vrc = RTUtf16LittleToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
            else
                vrc = RTUtf16ToUtf8Ex(pwszText, cwcConvert, ppszText, 0, pcchText);
        }
        if (RT_FAILURE(vrc))
            return false;
    }
    catch (std::bad_alloc &)
    {
        return false;
    }
    return true;
}


/**
 * Logs payload data for extra-verbose clipboard diagnostics.
 *
 * @param   pszCommand      Clipboard subcommand name.
 * @param   pszOperation    Copying operation being logged.
 * @param   strMimeType     MIME type of the payload.
 * @param   pbData          Payload bytes.
 * @param   cbData          Number of payload bytes.
 */
static void shclVerbosePayloadData(const char *pszCommand, const char *pszOperation,
                                   const Utf8Str &strMimeType, const BYTE *pbData, size_t cbData)
{
    if (g_uVerbosity < 2)
        return;

    char *pszVerboseText = NULL;
    size_t cchVerboseText = 0;
    bool fVerboseTruncated = false;
    bool const fHaveVerboseText = shclGetVerboseText(strMimeType, pbData, cbData, &pszVerboseText,
                                                     &cchVerboseText, &fVerboseTruncated);

    RTStrmPrintf(g_pStdErr, "clipboard: %s: %s format=%s size=%zu data=\"", pszCommand, pszOperation,
                 strMimeType.c_str(), cbData);
    if (fHaveVerboseText)
        ShClHlpPrintEscapedString(g_pStdErr, pszVerboseText, cchVerboseText);
    else
        RTStrmWrite(g_pStdErr, RT_STR_TUPLE("<binary>"));
    RTStrmPutCh(g_pStdErr, '"');
    if (fHaveVerboseText && fVerboseTruncated)
        RTStrmWrite(g_pStdErr, RT_STR_TUPLE(" data-truncated=true"));
    RTStrmPutCh(g_pStdErr, '\n');
    RTStrmFlush(g_pStdErr);

    RTStrFree(pszVerboseText);
}


/**
 * Reads current guest data for an extra-verbose format event.
 *
 * @returns true if matching non-empty guest data was returned.
 * @param   ptrSession          Clipboard session to read from.
 * @param   strExpectedMimeType MIME type selected from the format event.
 * @param   strReadMimeType     Where to return the MIME type read.
 * @param   aBuffer             Where to return the payload read.
 */
static bool shclHandleListenReadGuestData(const ComPtr<IClipboardSession> &ptrSession,
                                          const Utf8Str &strExpectedMimeType,
                                          Utf8Str &strReadMimeType,
                                          SafeArray<BYTE> &aBuffer)
{
    if (g_uVerbosity < 2 || ptrSession.isNull())
        return false;

    ClipboardSource_T enmReadSource = ClipboardSource_Custom;
    Bstr bstrRequestedMimeType("");
    Bstr bstrReadMimeType;
    HRESULT hrc = ptrSession->ReadDataRaw(ClipboardAction_Copy, bstrRequestedMimeType.raw(), &enmReadSource,
                                          bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aBuffer));
    if (FAILED(hrc))
        return false;

    strReadMimeType = bstrReadMimeType;
    if (enmReadSource != ClipboardSource_Guest)
        return false;
    if (!shclMimeEquivalent(strExpectedMimeType, strReadMimeType))
        return false;
    if (!aBuffer.size())
        return false;
    return true;
}


/**
 * Gets the MIME type of a clipboard format.
 *
 * @returns COM status code.
 * @param   ptrFormat       Clipboard format to inspect.
 * @param   strMimeType     Where to return the MIME type.
 */
static HRESULT shclGetFormatMimeType(const ComPtr<IClipboardFormat> &ptrFormat, Utf8Str &strMimeType)
{
    if (ptrFormat.isNull())
        return E_FAIL;

    Bstr bstrMimeType;
    HRESULT hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
        return hrc;

    strMimeType = bstrMimeType;
    return S_OK;
}


static const char *shclActionToString(ClipboardAction_T enmAction)
{
    switch (enmAction)
    {
        case ClipboardAction_Copy:   return "copy";
        case ClipboardAction_Cut:    return "cut";
        case ClipboardAction_Paste:  return "paste";
        case ClipboardAction_Custom: return "custom";
        default:                     break;
    }
    return "unknown";
}


/**
 * Converts a clipboard transfer direction to a printable string.
 *
 * @returns Printable transfer direction name.
 * @param   enmDirection    Transfer direction.
 */
static const char *shclTransferDirectionToString(ClipboardTransferDirection_T enmDirection)
{
    switch (enmDirection)
    {
        case ClipboardTransferDirection_Any:     return "any";
        case ClipboardTransferDirection_ToGuest: return "to-guest";
        case ClipboardTransferDirection_ToHost:  return "to-host";
        default:                                 break;
    }
    return "unknown";
}


/**
 * Converts a clipboard transfer interaction to a printable string.
 *
 * @returns Printable transfer interaction name.
 * @param   enmInteraction  Transfer interaction.
 */
static const char *shclTransferInteractionToString(ClipboardTransferInteraction_T enmInteraction)
{
    switch (enmInteraction)
    {
        case ClipboardTransferInteraction_None:          return "none";
        case ClipboardTransferInteraction_Approval:      return "approval";
        case ClipboardTransferInteraction_Destination:   return "destination";
        case ClipboardTransferInteraction_Overwrite:     return "overwrite";
        case ClipboardTransferInteraction_Rename:        return "rename";
        case ClipboardTransferInteraction_ErrorRecovery: return "error-recovery";
        default:                                        break;
    }
    return "unknown";
}


/**
 * Converts a clipboard error to a printable string.
 *
 * @returns Printable clipboard error name.
 * @param   enmError        Clipboard error.
 */
static const char *shclClipboardErrorToString(ClipboardError_T enmError)
{
    switch (enmError)
    {
        case ClipboardError_None:               return "none";
        case ClipboardError_NotSupported:       return "not-supported";
        case ClipboardError_AccessDenied:       return "access-denied";
        case ClipboardError_FormatNotSupported: return "format-not-supported";
        case ClipboardError_OperationFailed:    return "operation-failed";
        default:                                break;
    }
    return "unknown";
}


static const char *shclEventTypeToString(VBoxEventType_T enmType)
{
    switch (enmType)
    {
        case VBoxEventType_OnClipboardSourceChanged:           return "source-changed";
        case VBoxEventType_OnClipboardFormatChanged:           return "format-changed";
        case VBoxEventType_OnClipboardDataChanged:             return "data-changed";
        case VBoxEventType_OnClipboardDataRequested:           return "data-requested";
        case VBoxEventType_OnClipboardTransfer:                return "transfer";
        case VBoxEventType_OnClipboardError:                   return "error";
        case VBoxEventType_OnClipboardModeChanged:             return "mode-changed";
        case VBoxEventType_OnClipboardFileTransferModeChanged: return "file-transfer-mode-changed";
        default:                                               break;
    }
    return "unknown";
}


/**
 * Prints a clipboard event carrying an item.
 *
 * @param   enmFormat       Output format.
 * @param   pszEvent        Event name.
 * @param   ptrItem         Clipboard item associated with the event.
 * @param   pszAction       Clipboard action associated with the event, optional.
 * @param   fVerboseData    Whether to include payload data.
 * @param   pEventInfo      Optional event identity fields.
 * @param   pListenState    Listen state to update and use as a source fallback.
 */
static void shclHandleListenPrintEventItem(CLIPBOARDLISTENFMT enmFormat, const char *pszEvent,
                                           const ComPtr<IClipboardItem> &ptrItem, const char *pszAction,
                                           bool fVerboseData, const SHCLCLIPBOARDEVENTINFO *pEventInfo,
                                           SHCLHANDLELISTENSTATE *pListenState)
{
    Utf8Str strMimeType;
    ClipboardSource_T enmSource = ClipboardSource_Host;
    SafeArray<BYTE> aBuffer;
    HRESULT hrc = ptrItem.isNotNull() ? shclGetItemData(ptrItem, strMimeType, &enmSource, aBuffer) : E_FAIL;
    if (SUCCEEDED(hrc) && pListenState)
    {
        pListenState->fHaveSource = true;
        pListenState->enmSource = enmSource;
    }

    const char *pszSource = NULL;
    if (SUCCEEDED(hrc))
        pszSource = ShClHlpSourceToString(enmSource);
    else if (pListenState && pListenState->fHaveSource)
        pszSource = ShClHlpSourceToString(pListenState->enmSource);

    char *pszVerboseText = NULL;
    size_t cchVerboseText = 0;
    bool fVerboseTruncated = false;
    bool const fHaveVerboseData = fVerboseData && SUCCEEDED(hrc);
    bool const fHaveVerboseText =    fHaveVerboseData
                                  && shclGetVerboseText(strMimeType, aBuffer.raw(), aBuffer.size(), &pszVerboseText,
                                                             &cchVerboseText, &fVerboseTruncated);

    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, pszEvent, pszAction);
        if (SUCCEEDED(hrc))
        {
            RTStrmPrintf(g_pStdOut, ",\"format\":");
            shclHandleListenJsonString(strMimeType.c_str());
            RTStrmPrintf(g_pStdOut, ",\"size\":%zu", aBuffer.size());
            if (fHaveVerboseData)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"data\":"));
                if (fHaveVerboseText)
                    shclHandleListenJsonStringN(pszVerboseText, cchVerboseText);
                else
                    shclHandleListenJsonString("<binary>");
                if (fHaveVerboseText && fVerboseTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"data-truncated\":true"));
            }
        }
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, pszEvent, pszAction);
        if (SUCCEEDED(hrc))
        {
            RTPrintf(" format=\"%s\" size=\"%zu\"", strMimeType.c_str(), aBuffer.size());
            if (fHaveVerboseData)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data=\""));
                if (fHaveVerboseText)
                    ShClHlpPrintEscapedString(g_pStdOut, pszVerboseText, cchVerboseText);
                else
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("<binary>"));
                RTStrmPutCh(g_pStdOut, '"');
                if (fHaveVerboseText && fVerboseTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=\"true\""));
            }
        }
        RTPrintf("\n");
    }
    else
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, pszEvent, pszAction);
        if (SUCCEEDED(hrc))
        {
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" format="));
            shclHandleListenQuotedString(strMimeType.c_str());
            RTPrintf(" size=%zu", aBuffer.size());
            if (fHaveVerboseData)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data=\""));
                if (fHaveVerboseText)
                    ShClHlpPrintEscapedString(g_pStdOut, pszVerboseText, cchVerboseText);
                else
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("<binary>"));
                RTStrmPutCh(g_pStdOut, '"');
                if (fHaveVerboseText && fVerboseTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=true"));
            }
        }
        RTPrintf("\n");
    }

    RTStrFree(pszVerboseText);
}


/**
 * Prints a clipboard format-changed event.
 *
 * @param   enmFormat       Output format.
 * @param   ptrFormatEvent  Format-changed event object to print.
 * @param   ptrSession      Clipboard session for extra-verbose guest reads.
 * @param   pEventInfo      Optional event identity fields.
 * @param   pListenState    Listen state to update.
 */
static void shclHandleListenPrintFormatChangedEvent(CLIPBOARDLISTENFMT enmFormat,
                                                    const ComPtr<IClipboardFormatChangedEvent> &ptrFormatEvent,
                                                    const ComPtr<IClipboardSession> &ptrSession,
                                                    const SHCLCLIPBOARDEVENTINFO *pEventInfo,
                                                    SHCLHANDLELISTENSTATE *pListenState)
{
    ClipboardSource_T enmSource = ClipboardSource_Host;
    HRESULT hrc = ptrFormatEvent->COMGETTER(ClipboardSource)(&enmSource);
    bool const fHaveSource = SUCCEEDED(hrc);
    if (SUCCEEDED(hrc) && pListenState)
    {
        pListenState->fHaveSource = true;
        pListenState->enmSource = enmSource;
    }

    SafeIfaceArray<IClipboardFormat> aFormats;
    if (SUCCEEDED(hrc))
        hrc = ptrFormatEvent->COMGETTER(Formats)(ComSafeArrayAsOutParam(aFormats));

    std::vector<Utf8Str> vecMimeTypes;
    if (SUCCEEDED(hrc))
    {
        for (size_t i = 0; i < aFormats.size(); ++i)
        {
            Utf8Str strMimeType;
            hrc = shclGetFormatMimeType(aFormats[i], strMimeType);
            if (FAILED(hrc))
                break;
            vecMimeTypes.push_back(strMimeType);
        }
    }

    Utf8Str strDataMimeType;
    SafeArray<BYTE> aDataBuffer;
    bool fHaveData = false;
    if (SUCCEEDED(hrc) && enmSource == ClipboardSource_Guest && g_uVerbosity > 1)
    {
        ComPtr<IClipboardFormat> ptrPreferredFormat;
        Utf8Str strPreferredMimeType;
        HRESULT const hrcSelect = shclSelectPreferredFormat(aFormats, ptrPreferredFormat, strPreferredMimeType);
        if (SUCCEEDED(hrcSelect))
            fHaveData = shclHandleListenReadGuestData(ptrSession, strPreferredMimeType,
                                                      strDataMimeType, aDataBuffer);
    }

    char *pszData = NULL;
    size_t cchData = 0;
    bool fDataTruncated = false;
    bool const fHaveText =    fHaveData
                           && shclGetVerboseText(strDataMimeType, aDataBuffer.raw(), aDataBuffer.size(),
                                                &pszData, &cchData, &fDataTruncated);

    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    fHaveSource ? ShClHlpSourceToString(enmSource) : NULL,
                                    "format-changed", NULL /* pszAction */);
        if (SUCCEEDED(hrc))
        {
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"formats\":["));
            for (size_t i = 0; i < vecMimeTypes.size(); ++i)
            {
                if (i)
                    RTStrmPutCh(g_pStdOut, ',');
                shclHandleListenJsonString(vecMimeTypes[i].c_str());
            }
            RTStrmPutCh(g_pStdOut, ']');
            if (fHaveData)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"data-format\":"));
                shclHandleListenJsonString(strDataMimeType.c_str());
                RTStrmPrintf(g_pStdOut, ",\"size\":%zu,\"data\":", aDataBuffer.size());
                if (fHaveText)
                    shclHandleListenJsonStringN(pszData, cchData);
                else
                    shclHandleListenJsonString("<binary>");
                if (fHaveText && fDataTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"data-truncated\":true"));
            }
        }
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    fHaveSource ? ShClHlpSourceToString(enmSource) : NULL,
                                    "format-changed", NULL /* pszAction */);
        if (SUCCEEDED(hrc))
        {
            for (size_t i = 0; i < vecMimeTypes.size(); ++i)
                RTPrintf(" format=\"%s\"", vecMimeTypes[i].c_str());
            if (fHaveData)
            {
                RTPrintf(" data-format=\"%s\" size=\"%zu\" data=\"", strDataMimeType.c_str(), aDataBuffer.size());
                if (fHaveText)
                    ShClHlpPrintEscapedString(g_pStdOut, pszData, cchData);
                else
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("<binary>"));
                RTStrmPutCh(g_pStdOut, '"');
                if (fHaveText && fDataTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=\"true\""));
            }
        }
        RTPrintf("\n");
    }
    else
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    fHaveSource ? ShClHlpSourceToString(enmSource) : NULL,
                                    "format-changed", NULL /* pszAction */);
        if (SUCCEEDED(hrc))
        {
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" formats=\""));
            for (size_t i = 0; i < vecMimeTypes.size(); ++i)
            {
                if (i)
                    RTStrmPutCh(g_pStdOut, ',');
                ShClHlpPrintEscapedString(g_pStdOut, vecMimeTypes[i].c_str(), vecMimeTypes[i].length());
            }
            RTStrmPutCh(g_pStdOut, '"');
            if (fHaveData)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-format="));
                shclHandleListenQuotedString(strDataMimeType.c_str());
                RTPrintf(" size=%zu data=\"", aDataBuffer.size());
                if (fHaveText)
                    ShClHlpPrintEscapedString(g_pStdOut, pszData, cchData);
                else
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("<binary>"));
                RTStrmPutCh(g_pStdOut, '"');
                if (fHaveText && fDataTruncated)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=true"));
            }
        }
        RTPrintf("\n");
    }

    RTStrFree(pszData);
}


/**
 * Prints a clipboard data-requested event.
 *
 * @param   enmFormat       Output format.
 * @param   ptrRequestEvent Data-requested event object to print.
 * @param   pEventInfo      Optional event identity fields.
 * @param   pListenState    Listen state to update.
 */
static void shclHandleListenPrintDataRequestedEvent(CLIPBOARDLISTENFMT enmFormat,
                                                    const ComPtr<IClipboardDataRequestedEvent> &ptrRequestEvent,
                                                    const SHCLCLIPBOARDEVENTINFO *pEventInfo,
                                                    SHCLHANDLELISTENSTATE *pListenState)
{
    ULONG uRequestId = 0;
    ClipboardAction_T enmAction = ClipboardAction_Copy;
    ClipboardSource_T enmSource = ClipboardSource_Host;
    ComPtr<IClipboardFormat> ptrFormat;

    HRESULT hrc = ptrRequestEvent->COMGETTER(RequestId)(&uRequestId);
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(Action)(&enmAction);
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(ClipboardSource)(&enmSource);
    bool const fHaveSource = SUCCEEDED(hrc);
    if (fHaveSource && pListenState)
    {
        pListenState->fHaveSource = true;
        pListenState->enmSource = enmSource;
    }
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(Format)(ptrFormat.asOutParam());

    Utf8Str strMimeType;
    if (SUCCEEDED(hrc))
        hrc = shclGetFormatMimeType(ptrFormat, strMimeType);

    ComPtr<IClipboardItem> ptrItem;
    HRESULT hrcItem = ptrRequestEvent->COMGETTER(Item)(ptrItem.asOutParam());
    Utf8Str strItemMimeType;
    ClipboardSource_T enmItemSource = ClipboardSource_Custom;
    SafeArray<BYTE> aItemBuffer;
    if (SUCCEEDED(hrcItem) && ptrItem.isNotNull())
        hrcItem = shclGetItemData(ptrItem, strItemMimeType, &enmItemSource, aItemBuffer);
    else
        hrcItem = E_FAIL;

    char *pszText = NULL;
    size_t cchText = 0;
    bool fTextTruncated = false;
    bool const fHaveText =    g_uVerbosity > 1
                           && SUCCEEDED(hrc)
                           && enmSource == ClipboardSource_Host
                           && SUCCEEDED(hrcItem)
                           && enmItemSource == ClipboardSource_Host
                           && shclMimeEquivalent(strMimeType, strItemMimeType)
                           && shclGetVerboseText(strItemMimeType, aItemBuffer.raw(), aItemBuffer.size(),
                                                &pszText, &cchText, &fTextTruncated);

    const char *pszSource = fHaveSource ? ShClHlpSourceToString(enmSource) : NULL;
    const char *pszAction = SUCCEEDED(hrc) ? shclActionToString(enmAction) : NULL;

    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, "data-requested", pszAction);
        if (SUCCEEDED(hrc))
        {
            RTStrmPrintf(g_pStdOut, ",\"request-id\":%RU32,\"format\":", (uint32_t)uRequestId);
            shclHandleListenJsonString(strMimeType.c_str());
        }
        if (fHaveText)
        {
            RTStrmPrintf(g_pStdOut, ",\"size\":%zu,\"data\":", aItemBuffer.size());
            shclHandleListenJsonStringN(pszText, cchText);
            if (fTextTruncated)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"data-truncated\":true"));
        }
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, "data-requested", pszAction);
        if (SUCCEEDED(hrc))
            RTPrintf(" request-id=\"%RU32\" format=\"%s\"", (uint32_t)uRequestId, strMimeType.c_str());
        if (fHaveText)
        {
            RTPrintf(" size=\"%zu\" data=\"", aItemBuffer.size());
            ShClHlpPrintEscapedString(g_pStdOut, pszText, cchText);
            RTStrmPutCh(g_pStdOut, '"');
            if (fTextTruncated)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=\"true\""));
        }
        RTPrintf("\n");
    }
    else
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, "data-requested", pszAction);
        if (SUCCEEDED(hrc))
        {
            RTPrintf(" request-id=%RU32 format=", (uint32_t)uRequestId);
            shclHandleListenQuotedString(strMimeType.c_str());
        }
        if (fHaveText)
        {
            RTPrintf(" size=%zu data=\"", aItemBuffer.size());
            ShClHlpPrintEscapedString(g_pStdOut, pszText, cchText);
            RTStrmPutCh(g_pStdOut, '"');
            if (fTextTruncated)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(" data-truncated=true"));
        }
        RTPrintf("\n");
    }

    RTStrFree(pszText);
}


/**
 * Prints a clipboard transfer event.
 *
 * @param   enmFormat          Output format.
 * @param   ptrTransferEvent   Transfer event object to print.
 * @param   pEventInfo         Optional event identity fields.
 */
static void shclHandleListenPrintTransferEvent(CLIPBOARDLISTENFMT enmFormat,
                                               const ComPtr<IClipboardTransferEvent> &ptrTransferEvent,
                                               const SHCLCLIPBOARDEVENTINFO *pEventInfo)
{
    ComPtr<IClipboardTransfer> ptrTransfer;
    ClipboardTransferState_T enmState = ClipboardTransferState_Added;
    ClipboardTransferInteraction_T enmInteraction = ClipboardTransferInteraction_None;
    ClipboardError_T enmError = ClipboardError_None;
    Bstr bstrPath;
    Bstr bstrMessage;

    HRESULT hrcEvent = ptrTransferEvent.isNotNull() ? ptrTransferEvent->COMGETTER(Transfer)(ptrTransfer.asOutParam()) : E_FAIL;
    if (SUCCEEDED(hrcEvent)) hrcEvent = ptrTransferEvent->COMGETTER(State)(&enmState);
    if (SUCCEEDED(hrcEvent)) hrcEvent = ptrTransferEvent->COMGETTER(Interaction)(&enmInteraction);
    if (SUCCEEDED(hrcEvent)) hrcEvent = ptrTransferEvent->COMGETTER(Path)(bstrPath.asOutParam());
    if (SUCCEEDED(hrcEvent)) hrcEvent = ptrTransferEvent->COMGETTER(Message)(bstrMessage.asOutParam());
    if (SUCCEEDED(hrcEvent)) hrcEvent = ptrTransferEvent->COMGETTER(Error)(&enmError);

    ULONG idTransfer = 0;
    ClipboardTransferDirection_T enmDirection = ClipboardTransferDirection_Any;
    ClipboardSource_T enmSource = ClipboardSource_Host;
    ClipboardAction_T enmAction = ClipboardAction_Copy;
    HRESULT hrcTransfer = ptrTransfer.isNotNull() ? ptrTransfer->COMGETTER(Id)(&idTransfer) : E_FAIL;
    if (SUCCEEDED(hrcTransfer)) hrcTransfer = ptrTransfer->COMGETTER(Direction)(&enmDirection);
    if (SUCCEEDED(hrcTransfer)) hrcTransfer = ptrTransfer->COMGETTER(Source)(&enmSource);
    if (SUCCEEDED(hrcTransfer)) hrcTransfer = ptrTransfer->COMGETTER(Action)(&enmAction);

    Utf8Str const strPath(bstrPath);
    Utf8Str const strMessage(bstrMessage);

    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    SUCCEEDED(hrcTransfer) ? ShClHlpSourceToString(enmSource) : NULL,
                                    "transfer", SUCCEEDED(hrcTransfer) ? shclActionToString(enmAction) : NULL);
        if (SUCCEEDED(hrcTransfer))
        {
            RTStrmPrintf(g_pStdOut, ",\"id\":%RU32,\"direction\":", (uint32_t)idTransfer);
            shclHandleListenJsonString(shclTransferDirectionToString(enmDirection));
        }
        if (SUCCEEDED(hrcEvent))
        {
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"state\":"));
            shclHandleListenJsonString(ShClHlpTransferStateToString(enmState));
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"interaction\":"));
            shclHandleListenJsonString(shclTransferInteractionToString(enmInteraction));
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"path\":"));
            shclHandleListenJsonString(strPath.c_str());
            RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"error\":"));
            shclHandleListenJsonString(shclClipboardErrorToString(enmError));
            RTStrmPrintf(g_pStdOut, ",\"error-code\":%u,\"message\":", (unsigned)enmError);
            shclHandleListenJsonString(strMessage.c_str());
        }
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    }
    else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    SUCCEEDED(hrcTransfer) ? ShClHlpSourceToString(enmSource) : NULL,
                                    "transfer", SUCCEEDED(hrcTransfer) ? shclActionToString(enmAction) : NULL);
        if (SUCCEEDED(hrcTransfer))
            RTPrintf(" id=\"%RU32\" direction=\"%s\"",
                     (uint32_t)idTransfer, shclTransferDirectionToString(enmDirection));
        if (SUCCEEDED(hrcEvent))
        {
            RTPrintf(" state=\"%s\" interaction=\"%s\" path=",
                     ShClHlpTransferStateToString(enmState), shclTransferInteractionToString(enmInteraction));
            shclHandleListenQuotedString(strPath.c_str());
            RTPrintf(" error=\"%s\" error-code=\"%u\" message=", shclClipboardErrorToString(enmError), (unsigned)enmError);
            shclHandleListenQuotedString(strMessage.c_str());
        }
        RTPrintf("\n");
    }
    else
    {
        shclHandleListenPrintPrefix(enmFormat, pEventInfo,
                                    SUCCEEDED(hrcTransfer) ? ShClHlpSourceToString(enmSource) : NULL,
                                    "transfer", SUCCEEDED(hrcTransfer) ? shclActionToString(enmAction) : NULL);
        if (SUCCEEDED(hrcTransfer))
            RTPrintf(" id=%RU32 direction=%s", (uint32_t)idTransfer, shclTransferDirectionToString(enmDirection));
        if (SUCCEEDED(hrcEvent))
        {
            RTPrintf(" state=%s interaction=%s path=",
                     ShClHlpTransferStateToString(enmState), shclTransferInteractionToString(enmInteraction));
            shclHandleListenQuotedString(strPath.c_str());
            RTPrintf(" error=%s error-code=%u message=", shclClipboardErrorToString(enmError), (unsigned)enmError);
            shclHandleListenQuotedString(strMessage.c_str());
        }
        RTPrintf("\n");
    }
}


/**
 * Prints a clipboard event without additional payload.
 *
 * @param   enmFormat       Output format.
 * @param   pszEvent        Event name.
 * @param   pEventInfo      Optional event identity fields.
 * @param   pListenState    Listen state providing the current source, optional.
 */
static void shclHandleListenPrintSimpleEvent(CLIPBOARDLISTENFMT enmFormat, const char *pszEvent,
                                             const SHCLCLIPBOARDEVENTINFO *pEventInfo,
                                             const SHCLHANDLELISTENSTATE *pListenState)
{
    const char *pszSource = pListenState && pListenState->fHaveSource
                          ? ShClHlpSourceToString(pListenState->enmSource) : NULL;
    shclHandleListenPrintPrefix(enmFormat, pEventInfo, pszSource, pszEvent, NULL /* pszAction */);
    if (enmFormat == CLIPBOARDLISTENFMT_JSON)
        RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
    else
        RTPrintf("\n");
}


/**
 * Prints a clipboard event.
 *
 * @param   enmFormat       Output format.
 * @param   ptrEvent        Event object to print.
 * @param   ptrSession      Clipboard session for extra-verbose diagnostic reads.
 * @param   pListenState    Listen state to update.
 */
static void shclHandleListenPrintEvent(CLIPBOARDLISTENFMT enmFormat, const ComPtr<IEvent> &ptrEvent,
                                       const ComPtr<IClipboardSession> &ptrSession,
                                       SHCLHANDLELISTENSTATE *pListenState)
{
    VBoxEventType_T enmType;
    HRESULT hrc = ptrEvent->COMGETTER(Type)(&enmType);
    if (FAILED(hrc))
        return;

    SHCLCLIPBOARDEVENTINFO EventInfo;
    shclGetClipboardEventInfo(ptrEvent, &EventInfo);

    switch (enmType)
    {
        case VBoxEventType_OnClipboardSourceChanged:
        {
            ComPtr<IClipboardSourceChangedEvent> ptrSourceEvent = ptrEvent;
            ClipboardSource_T enmSource = ClipboardSource_Host;
            HRESULT const hrcSource = ptrSourceEvent->COMGETTER(ClipboardSource)(&enmSource);
            if (SUCCEEDED(hrcSource) && pListenState)
            {
                pListenState->fHaveSource = true;
                pListenState->enmSource = enmSource;
            }
            const char *pszSource = SUCCEEDED(hrcSource) ? ShClHlpSourceToString(enmSource) : NULL;
            shclHandleListenPrintPrefix(enmFormat, &EventInfo, pszSource, "source-changed", NULL /* pszAction */);
            if (enmFormat == CLIPBOARDLISTENFMT_JSON)
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
            else
                RTPrintf("\n");
            break;
        }
        case VBoxEventType_OnClipboardFormatChanged:
        {
            ComPtr<IClipboardFormatChangedEvent> ptrFormatEvent = ptrEvent;
            shclHandleListenPrintFormatChangedEvent(enmFormat, ptrFormatEvent, ptrSession, &EventInfo, pListenState);
            break;
        }
        case VBoxEventType_OnClipboardDataChanged:
        {
            ComPtr<IClipboardDataChangedEvent> ptrDataEvent = ptrEvent;
            ComPtr<IClipboardItem> ptrItem;
            ptrDataEvent->COMGETTER(Item)(ptrItem.asOutParam());
            ClipboardAction_T enmAction = ClipboardAction_Copy;
            HRESULT const hrcAction = ptrDataEvent->COMGETTER(Action)(&enmAction);
            shclHandleListenPrintEventItem(enmFormat, "data-changed", ptrItem,
                                           SUCCEEDED(hrcAction) ? shclActionToString(enmAction) : NULL,
                                           g_uVerbosity > 1, &EventInfo, pListenState);
            break;
        }
        case VBoxEventType_OnClipboardDataRequested:
        {
            ComPtr<IClipboardDataRequestedEvent> ptrRequestEvent = ptrEvent;
            shclHandleListenPrintDataRequestedEvent(enmFormat, ptrRequestEvent, &EventInfo, pListenState);
            break;
        }
        case VBoxEventType_OnClipboardTransfer:
        {
            ComPtr<IClipboardTransferEvent> ptrTransferEvent = ptrEvent;
            shclHandleListenPrintTransferEvent(enmFormat, ptrTransferEvent, &EventInfo);
            break;
        }
        case VBoxEventType_OnClipboardError:
        {
            ComPtr<IClipboardErrorEvent> ptrErrorEvent = ptrEvent;
            Bstr bstrMsg;
            LONG rcError = VERR_GENERAL_FAILURE;
            ptrErrorEvent->COMGETTER(Msg)(bstrMsg.asOutParam());
            ptrErrorEvent->COMGETTER(RcError)(&rcError);
            Utf8Str strMsg(bstrMsg);
            const char *pszSource = pListenState && pListenState->fHaveSource
                                  ? ShClHlpSourceToString(pListenState->enmSource) : NULL;
            shclHandleListenPrintPrefix(enmFormat, &EventInfo, pszSource, "error", NULL /* pszAction */);
            if (enmFormat == CLIPBOARDLISTENFMT_JSON)
            {
                RTStrmPrintf(g_pStdOut, ",\"rc\":%RI32,\"message\":", (int32_t)rcError);
                shclHandleListenJsonString(strMsg.c_str());
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE("}\n"));
            }
            else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
            {
                RTPrintf(" rc=\"%RI32\" message=", (int32_t)rcError);
                shclHandleListenQuotedString(strMsg.c_str());
                RTPrintf("\n");
            }
            else
            {
                RTPrintf(" rc=%RI32 message=", (int32_t)rcError);
                shclHandleListenQuotedString(strMsg.c_str());
                RTPrintf("\n");
            }
            break;
        }
        case VBoxEventType_OnClipboardModeChanged:
        {
            ComPtr<IClipboardModeChangedEvent> ptrModeEvent = ptrEvent;
            ClipboardMode_T enmMode = ClipboardMode_Disabled;
            ptrModeEvent->COMGETTER(ClipboardMode)(&enmMode);
            const char *pszMode = ShClHlpModeToString(enmMode);
            const char *pszSource = pListenState && pListenState->fHaveSource
                                  ? ShClHlpSourceToString(pListenState->enmSource) : NULL;
            shclHandleListenPrintPrefix(enmFormat, &EventInfo, pszSource, "mode-changed", NULL /* pszAction */);
            if (enmFormat == CLIPBOARDLISTENFMT_JSON)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"mode\":"));
                shclHandleListenJsonString(pszMode);
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"active\":true}\n"));
            }
            else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
                RTPrintf(" mode=\"%s\" active=\"true\"\n", pszMode);
            else
                RTPrintf(" mode=%s active=true\n", pszMode);
            break;
        }
        case VBoxEventType_OnClipboardFileTransferModeChanged:
        {
            ComPtr<IClipboardFileTransferModeChangedEvent> ptrModeEvent = ptrEvent;
            BOOL fEnabled = FALSE;
            ptrModeEvent->COMGETTER(Enabled)(&fEnabled);
            const char *pszSource = pListenState && pListenState->fHaveSource
                                  ? ShClHlpSourceToString(pListenState->enmSource) : NULL;
            shclHandleListenPrintPrefix(enmFormat, &EventInfo, pszSource,
                                        "file-transfer-mode-changed", NULL /* pszAction */);
            if (enmFormat == CLIPBOARDLISTENFMT_JSON)
            {
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"enabled\":"));
                if (fEnabled)
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("true"));
                else
                    RTStrmWrite(g_pStdOut, RT_STR_TUPLE("false"));
                RTStrmWrite(g_pStdOut, RT_STR_TUPLE(",\"active\":true}\n"));
            }
            else if (enmFormat == CLIPBOARDLISTENFMT_MACHINE_READABLE)
                RTPrintf(" enabled=\"%s\" active=\"true\"\n", fEnabled ? "true" : "false");
            else
                RTPrintf(" enabled=%s active=true\n", fEnabled ? "true" : "false");
            break;
        }
        default:
            shclHandleListenPrintSimpleEvent(enmFormat, "unknown", &EventInfo, pListenState);
            break;
    }
}


/**
 * Checks whether an event satisfies a paste wait request.
 *
 * @returns true if the event carries matching guest clipboard data.
 * @param   ptrEvent        Event object to inspect.
 * @param   pszFormat       Requested MIME type, optional.
 * @param   ptrItem         Where to return the matching item.
 */
static bool shclHandlePasteEventMatchesWait(const ComPtr<IEvent> &ptrEvent, const char *pszFormat, ComPtr<IClipboardItem> &ptrItem)
{
    VBoxEventType_T enmType;
    HRESULT hrc = ptrEvent->COMGETTER(Type)(&enmType);
    if (FAILED(hrc) || enmType != VBoxEventType_OnClipboardDataChanged)
        return false;

    ComPtr<IClipboardDataChangedEvent> ptrDataEvent = ptrEvent;
    hrc = ptrDataEvent->COMGETTER(Item)(ptrItem.asOutParam());
    if (FAILED(hrc) || ptrItem.isNull())
        return false;

    Utf8Str strMimeType;
    ClipboardSource_T enmSource = ClipboardSource_Host;
    SafeArray<BYTE> aBuffer;
    hrc = shclGetItemData(ptrItem, strMimeType, &enmSource, aBuffer);
    if (FAILED(hrc))
        return false;

    return enmSource == ClipboardSource_Guest && shclMimeMatches(pszFormat, strMimeType);
}


/**
 * Checks whether a paste event should trigger a current-data read attempt.
 *
 * @returns true if the event indicates matching guest data may be readable.
 * @param   ptrEvent        Event object to inspect.
 * @param   pszFormat       Requested MIME type, optional.
 */
static bool shclHandlePasteEventMayNeedRead(const ComPtr<IEvent> &ptrEvent, const char *pszFormat)
{
    VBoxEventType_T enmType = VBoxEventType_Invalid;
    HRESULT hrc = ptrEvent->COMGETTER(Type)(&enmType);
    if (FAILED(hrc))
        return false;

    if (enmType == VBoxEventType_OnClipboardSourceChanged)
    {
        ComPtr<IClipboardSourceChangedEvent> ptrSourceEvent = ptrEvent;
        if (ptrSourceEvent.isNull())
            return false;
        ClipboardSource_T enmSource = ClipboardSource_Custom;
        hrc = ptrSourceEvent->COMGETTER(ClipboardSource)(&enmSource);
        return SUCCEEDED(hrc) && enmSource == ClipboardSource_Guest;
    }

    if (enmType == VBoxEventType_OnClipboardFormatChanged)
    {
        ComPtr<IClipboardFormatChangedEvent> ptrFormatEvent = ptrEvent;
        if (ptrFormatEvent.isNull())
            return false;
        ClipboardSource_T enmSource = ClipboardSource_Custom;
        SafeIfaceArray<IClipboardFormat> aFormats;
        hrc = ptrFormatEvent->COMGETTER(ClipboardSource)(&enmSource);
        if (SUCCEEDED(hrc))
            hrc = ptrFormatEvent->COMGETTER(Formats)(ComSafeArrayAsOutParam(aFormats));
        if (FAILED(hrc) || enmSource != ClipboardSource_Guest)
            return false;
        if (pszFormat && *pszFormat)
            return shclMimeTypeIsInFormatArray(Utf8Str(pszFormat), aFormats);

        ComPtr<IClipboardFormat> ptrFormat;
        Utf8Str strMimeType;
        return SUCCEEDED(shclSelectPreferredFormat(aFormats, ptrFormat, strMimeType));
    }

    if (enmType != VBoxEventType_OnClipboardDataChanged)
        return false;

    ComPtr<IClipboardDataChangedEvent> ptrDataEvent = ptrEvent;
    if (ptrDataEvent.isNull())
        return true;

    ComPtr<IClipboardItem> ptrItem;
    hrc = ptrDataEvent->COMGETTER(Item)(ptrItem.asOutParam());
    if (FAILED(hrc) || ptrItem.isNull())
        return true;

    Utf8Str strMimeType;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    SafeArray<BYTE> aBuffer;
    hrc = shclGetItemData(ptrItem, strMimeType, &enmSource, aBuffer);
    return SUCCEEDED(hrc) && enmSource == ClipboardSource_Guest && shclMimeMatches(pszFormat, strMimeType);
}


/**
 * Logs a clipboard error event while paste continues waiting for data.
 *
 * @param   ptrEvent        Error event to inspect.
 */
static void shclHandlePasteLogErrorEvent(const ComPtr<IEvent> &ptrEvent)
{
    ComPtr<IClipboardErrorEvent> ptrErrorEvent = ptrEvent;
    if (ptrErrorEvent.isNull())
    {
        shclVerbose("paste: ignoring clipboard error event while waiting");
        return;
    }

    LONG vrcError = VERR_GENERAL_FAILURE;
    Bstr bstrMsg;
    HRESULT hrc = ptrErrorEvent->COMGETTER(RcError)(&vrcError);
    if (SUCCEEDED(hrc))
        hrc = ptrErrorEvent->COMGETTER(Msg)(bstrMsg.asOutParam());
    if (FAILED(hrc))
    {
        shclVerbose("paste: ignoring clipboard error event while waiting; inspection failed: %Rhrc", hrc);
        return;
    }

    Utf8Str strMsg(bstrMsg);
    shclVerbose("paste: ignoring clipboard error while waiting: rc=%Rrc msg=%s", (int)vrcError, strMsg.c_str());
}


/**
 * Registers a passive clipboard session event listener.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to listen on.
 * @param   aEventTypes     Event types to subscribe to.
 * @param   ptrEventSource  Where to return the event source.
 * @param   ptrListener     Where to return the event listener.
 */
static RTEXITCODE shclRegisterListener(const ComPtr<IClipboardSession> &ptrSession, SafeArray<VBoxEventType_T> &aEventTypes,
                                       ComPtr<IEventSource> &ptrEventSource, ComPtr<IEventListener> &ptrListener)
{
    HRESULT hrc;
    CHECK_ERROR2_RET(hrc, ptrSession, COMGETTER(EventSource)(ptrEventSource.asOutParam()), RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc, ptrEventSource, CreateListener(ptrListener.asOutParam()), RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc, ptrEventSource, RegisterListener(ptrListener, ComSafeArrayAsInParam(aEventTypes), false /* passive */),
                     RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}


/**
 * Unregisters a clipboard event listener.
 *
 * @param   ptrEventSource  Event source the listener was registered with.
 * @param   ptrListener     Event listener to unregister.
 */
static void shclUnregisterListener(const ComPtr<IEventSource> &ptrEventSource, const ComPtr<IEventListener> &ptrListener)
{
    if (ptrEventSource.isNotNull() && ptrListener.isNotNull())
        ptrEventSource->UnregisterListener(ptrListener);
}


/**
 * Cleans up a clipboard wait listener and optional signal handler.
 *
 * @param   ptrEventSource          Event source the listener was registered with.
 * @param   ptrListener             Event listener to unregister.
 * @param   fSignalHandlerInstalled Whether to uninstall clipboard signal handling.
 */
static void shclCleanupListener(const ComPtr<IEventSource> &ptrEventSource,
                                const ComPtr<IEventListener> &ptrListener,
                                bool fSignalHandlerInstalled)
{
    if (fSignalHandlerInstalled)
        shclSignalHandlerUninstall();
    shclUnregisterListener(ptrEventSource, ptrListener);
}


/**
 * Cleans up host clipboard publication, listener and optional signal handler.
 *
 * @param   ptrHostClipboard        Host clipboard endpoint to clear.
 * @param   fClearHostClipboard     Whether to clear @a ptrHostClipboard.
 * @param   ptrEventSource          Event source the listener was registered with.
 * @param   ptrListener             Event listener to unregister.
 * @param   fSignalHandlerInstalled Whether to uninstall clipboard signal handling.
 */
static void shclCleanupHostClipboardListener(const ComPtr<IHostClipboard> &ptrHostClipboard,
                                             bool fClearHostClipboard,
                                             const ComPtr<IEventSource> &ptrEventSource,
                                             const ComPtr<IEventListener> &ptrListener,
                                             bool fSignalHandlerInstalled)
{
    if (fClearHostClipboard)
        shclClearHostClipboard(ptrHostClipboard);
    shclCleanupListener(ptrEventSource, ptrListener, fSignalHandlerInstalled);
}


/**
 * Marks a passive listener event as processed if the event is waitable.
 *
 * @param   ptrEventSource  Event source that delivered the event.
 * @param   ptrListener     Listener that received the event.
 * @param   ptrEvent        Event to mark as processed.
 */
static void shclMarkEventProcessed(const ComPtr<IEventSource> &ptrEventSource,
                                   const ComPtr<IEventListener> &ptrListener,
                                   const ComPtr<IEvent> &ptrEvent)
{
    BOOL fWaitable = FALSE;
    HRESULT hrc = ptrEvent.isNotNull() ? ptrEvent->COMGETTER(Waitable)(&fWaitable) : E_FAIL;
    if (SUCCEEDED(hrc) && fWaitable)
    {
        hrc = ptrEventSource->EventProcessed(ptrListener, ptrEvent);
        if (FAILED(hrc))
            RTMsgWarning(Clipboard::tr("Failed to acknowledge clipboard event: %Rhrc"), hrc);
    }
}


/**
 * Gets data-request details from a clipboard event.
 *
 * @returns true if @a ptrEvent is a valid data-requested event, false otherwise.
 * @param   ptrEvent        Event to inspect.
 * @param   puRequestId     Where to return the request ID, optional.
 * @param   penmAction      Where to return the requested action.
 * @param   penmSource      Where to return the requested source.
 * @param   strMimeType     Where to return the requested MIME type.
 */
static bool shclGetDataRequestedEvent(const ComPtr<IEvent> &ptrEvent,
                                      ULONG *puRequestId,
                                      ClipboardAction_T *penmAction,
                                      ClipboardSource_T *penmSource,
                                      Utf8Str &strMimeType)
{
    VBoxEventType_T enmType = VBoxEventType_Invalid;
    HRESULT hrc = ptrEvent->COMGETTER(Type)(&enmType);
    if (FAILED(hrc) || enmType != VBoxEventType_OnClipboardDataRequested)
        return false;

    ComPtr<IClipboardDataRequestedEvent> ptrRequestEvent = ptrEvent;
    if (ptrRequestEvent.isNull())
        return false;

    ULONG uRequestId = 0;
    ComPtr<IClipboardFormat> ptrFormat;
    hrc = ptrRequestEvent->COMGETTER(RequestId)(&uRequestId);
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(Action)(penmAction);
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(ClipboardSource)(penmSource);
    if (SUCCEEDED(hrc))
        hrc = ptrRequestEvent->COMGETTER(Format)(ptrFormat.asOutParam());
    if (SUCCEEDED(hrc))
        hrc = shclGetFormatMimeType(ptrFormat, strMimeType);
    if (SUCCEEDED(hrc) && puRequestId)
        *puRequestId = uRequestId;

    return SUCCEEDED(hrc);
}


/** Active event listener used by clipboard copy while waiting for guest data requests. */
class ClipboardProviderEventListener
{
public:

    ClipboardProviderEventListener(const char *pszFormat)
        : mStrFormat(pszFormat)
        , mSemEvent(NIL_RTSEMEVENT)
        , mfCritSect(false)
        , mcRequests(0)
        , mhrcLast(S_OK)
        , mfDone(false)
        , mfFailed(false)
    {
    }

    virtual ~ClipboardProviderEventListener(void)
    {
    }

    HRESULT init(void)
    {
        int vrc = RTCritSectInit(&mCritSect);
        if (RT_FAILURE(vrc))
            return E_FAIL;
        mfCritSect = true;

        vrc = RTSemEventCreate(&mSemEvent);
        if (RT_FAILURE(vrc))
        {
            RTCritSectDelete(&mCritSect);
            mfCritSect = false;
            return E_FAIL;
        }
        return S_OK;
    }

    void uninit(void)
    {
        if (mSemEvent != NIL_RTSEMEVENT)
        {
            RTSemEventDestroy(mSemEvent);
            mSemEvent = NIL_RTSEMEVENT;
        }
        if (mfCritSect)
        {
            RTCritSectDelete(&mCritSect);
            mfCritSect = false;
        }
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        ComPtr<IEvent> ptrEvent = aEvent;
        shclVerboseClipboardEvent("copy", "received active event", aType, ptrEvent);

        switch (aType)
        {
            case VBoxEventType_OnClipboardDataRequested:
                return handleDataRequested(aEvent);

            case VBoxEventType_OnClipboardError:
                shclVerbose("copy: received clipboard error event while waiting for guest read");
                setFailed(VBOX_E_SHCL_GUEST_ERROR);
                break;

            default:
                shclVerbose("copy: ignoring active event '%s'", shclEventTypeToString(aType));
                break;
        }
        return S_OK;
    }

    int wait(uint32_t cMsTimeout)
    {
        return RTSemEventWait(mSemEvent, cMsTimeout);
    }

    void getStatus(bool *pfDone, bool *pfFailed, HRESULT *phrcLast, uint32_t *pcRequests)
    {
        RTCritSectEnter(&mCritSect);
        *pfDone = mfDone;
        *pfFailed = mfFailed;
        *phrcLast = mhrcLast;
        *pcRequests = mcRequests;
        RTCritSectLeave(&mCritSect);
    }

private:

    HRESULT handleDataRequested(IEvent *aEvent)
    {
        ClipboardAction_T enmAction = ClipboardAction_Copy;
        ClipboardSource_T enmSource = ClipboardSource_Host;
        Utf8Str strRequestedMimeType;
        ComPtr<IEvent> ptrEvent = aEvent;
        bool const fHaveRequest = shclGetDataRequestedEvent(ptrEvent, NULL, &enmAction, &enmSource, strRequestedMimeType);
        if (fHaveRequest)
            shclVerbose("copy: request action=%s source=%s format=%s",
                             shclActionToString(enmAction), ShClHlpSourceToString(enmSource), strRequestedMimeType.c_str());
        if (   !fHaveRequest
            || enmAction != ClipboardAction_Copy
            || enmSource != ClipboardSource_Host
            || !shclMimeMatches(mStrFormat.c_str(), strRequestedMimeType))
        {
            if (fHaveRequest)
                shclVerbose("copy: ignoring non-matching request");
            return S_OK;
        }

        shclVerbose("copy: matching guest read request observed");

        RTCritSectEnter(&mCritSect);
        if (mcRequests < UINT32_MAX)
            mcRequests++;
        mfDone = true;
        uint32_t const cRequests = mcRequests;
        RTCritSectLeave(&mCritSect);

        shclVerbose("copy: guest read observed (%RU32 request(s))", cRequests);
        if (mSemEvent != NIL_RTSEMEVENT)
            RTSemEventSignal(mSemEvent);
        return S_OK;
    }

    void setFailed(HRESULT hrc)
    {
        RTCritSectEnter(&mCritSect);
        mhrcLast = hrc;
        mfFailed = true;
        mfDone = true;
        RTCritSectLeave(&mCritSect);
        if (mSemEvent != NIL_RTSEMEVENT)
            RTSemEventSignal(mSemEvent);
    }

private:

    Utf8Str            mStrFormat;
    RTSEMEVENT         mSemEvent;
    RTCRITSECT         mCritSect;
    bool               mfCritSect;
    uint32_t           mcRequests;
    HRESULT            mhrcLast;
    bool               mfDone;
    bool               mfFailed;
};

typedef ListenerImpl<ClipboardProviderEventListener> ClipboardProviderEventListenerImpl;

VBOX_LISTENER_DECLARE(ClipboardProviderEventListenerImpl)


/**
 * Publishes host clipboard data to the guest.
 *
 * @returns COM status code.
 * @param   ptrSession      Clipboard session to publish through.
 * @param   pszFormat       MIME type of @a aBuffer.
 * @param   aBuffer         Payload to publish.
 */
static HRESULT shclHandleCopyWriteDataRaw(const ComPtr<IClipboardSession> &ptrSession, const char *pszFormat,
                                          SafeArray<BYTE> &aBuffer)
{
    ClipboardSource_T enmWrittenSource = ClipboardSource_Host;
    Bstr bstrWrittenMimeType;
    SafeArray<BYTE> aWrittenBuffer;
    return ptrSession->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host, Bstr(pszFormat).raw(),
                                    ComSafeArrayAsInParam(aBuffer), &enmWrittenSource,
                                    bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
}


/**
 * Publishes host clipboard data and waits for the guest to request it.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to publish through.
 * @param   pszFormat       MIME type of @a aBuffer.
 * @param   aBuffer         Payload to publish.
 * @param   cMsTimeout      Maximum wait time in milliseconds.
 */
static RTEXITCODE shclHandleCopyProvideData(const ComPtr<IClipboardSession> &ptrSession,
                                            const char *pszFormat,
                                            SafeArray<BYTE> &aBuffer,
                                            uint32_t cMsTimeout)
{
    HRESULT hrc;
    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;

    if (cMsTimeout != 0)
    {
        SafeArray<VBoxEventType_T> aEventTypes;
        aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
        aEventTypes.push_back(VBoxEventType_OnClipboardError);

        shclVerbose("copy: registering listener for data requests and errors");
        RTEXITCODE rcExit = shclRegisterListener(ptrSession, aEventTypes, ptrEventSource, ptrListener);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    shclVerbose("copy: publishing %zu bytes as '%s'", aBuffer.size(), pszFormat);
    hrc = shclHandleCopyWriteDataRaw(ptrSession, pszFormat, aBuffer);
    if (FAILED(hrc))
    {
        shclCleanupListener(ptrEventSource, ptrListener, false /* fSignalHandlerInstalled */);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Publishing clipboard data to the guest failed: %Rhrc"), hrc);
    }
    if (cMsTimeout == 0)
        return RTEXITCODE_SUCCESS;

    shclSignalHandlerInstall();

    uint64_t const msStart = RTTimeMilliTS();
    uint64_t msLastRequest = 0;
    bool fTimedOut = false;
    uint32_t cRequests = 0;
    shclVerbose("copy: waiting for guest read; timeout=%s",
                     cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());
    if (cMsTimeout == RT_INDEFINITE_WAIT)
        shclInfo(Clipboard::tr("Waiting for the guest to request and receive the clipboard data..."));
    else
        shclInfo(Clipboard::tr("Waiting up to %RU32 ms for the guest to request and receive the clipboard data..."), cMsTimeout);
    for (;;)
    {
        if (shclSignalWasCaught())
            break;

        uint64_t const msNow = RTTimeMilliTS();
        if (cRequests && msNow - msLastRequest >= CLIPBOARD_COPY_REQUEST_QUIET_MS)
            break;

        uint32_t cMsThisWait = 1000;
        if (cRequests)
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)cMsThisWait,
                                           CLIPBOARD_COPY_REQUEST_QUIET_MS - (msNow - msLastRequest));
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = msNow - msStart;
            if (cMsElapsed >= cMsTimeout)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)cMsThisWait, cMsTimeout - cMsElapsed);
        }

        shclVerbose("copy: waiting up to %RU32 ms for a clipboard event", cMsThisWait);
        ComPtr<IEvent> ptrEvent;
        hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
        {
            shclVerbose("copy: no clipboard event within %RU32 ms", cMsThisWait);
            continue;
        }
        if (FAILED(hrc))
        {
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard events failed: %Rhrc"), hrc);
        }
        if (ptrEvent.isNull())
            continue;

        VBoxEventType_T enmType = VBoxEventType_Invalid;
        hrc = ptrEvent->COMGETTER(Type)(&enmType);
        if (FAILED(hrc))
            shclVerbose("copy: failed to get event type: %Rhrc", hrc);
        else
            shclVerboseClipboardEvent("copy", "received event", enmType, ptrEvent);

        if (SUCCEEDED(hrc) && enmType == VBoxEventType_OnClipboardError)
        {
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard error while waiting for the guest read."));
        }

        ClipboardAction_T enmAction = ClipboardAction_Copy;
        ClipboardSource_T enmSource = ClipboardSource_Custom;
        Utf8Str strRequestedMimeType;
        if (shclGetDataRequestedEvent(ptrEvent, NULL, &enmAction, &enmSource, strRequestedMimeType))
        {
            shclVerbose("copy: request action=%s source=%s format=%s",
                        shclActionToString(enmAction), ShClHlpSourceToString(enmSource), strRequestedMimeType.c_str());
            if (   enmAction == ClipboardAction_Copy
                && enmSource == ClipboardSource_Host
                && shclMimeMatches(pszFormat, strRequestedMimeType))
            {
                if (cRequests < UINT32_MAX)
                    cRequests++;
                msLastRequest = RTTimeMilliTS();
                shclVerbose("copy: matching guest read request observed (%RU32 request(s))", cRequests);
            }
            else
                shclVerbose("copy: ignoring non-matching request");
        }

        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
    if (fInterrupted)
    {
        shclVerbose("copy: interrupted; shutting down");
        return RTEXITCODE_SUCCESS;
    }
    if (fTimedOut)
    {
        shclVerbose("copy: timed out after %RU32 ms waiting for guest read; observed %RU32 request(s)",
                    cMsTimeout, cRequests);
        if (cRequests)
            shclInfo(Clipboard::tr("Timed out after the guest requested clipboard data; the transfer might not be complete."));
        else
            shclInfo(Clipboard::tr("Timed out waiting for the guest to request clipboard data."));
        return RTEXITCODE_SUCCESS;
    }
    shclVerbose("copy: finished after guest read (%RU32 request(s) observed)", cRequests);
    shclInfo(Clipboard::tr("The guest requested and received the clipboard data."));
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the clipboard set-mode subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleSetMode(HandlerArg *pArg, int argc, char **argv)
{
    if (argc <= 2)
        return errorSyntax(Clipboard::tr("Missing argument to '%s'."), argv[1]);
    if (argc > 3)
        return errorSyntax(Clipboard::tr("Too many arguments to '%s'."), argv[1]);

    uint32_t uMode = ClipboardMode_Disabled;
    if (!ShClHlpModeFromString(argv[2], &uMode))
        return errorSyntax(Clipboard::tr("Invalid '%s' argument '%s'."), argv[1], argv[2]);
    ClipboardMode_T const enmMode = (ClipboardMode_T)uMode;

    ComPtr<IMachine> ptrMachine;
    ComPtr<IClipboardSettings> ptrClipboard;
    HRESULT hrc = shclGetSettings(pArg, argv[0], ptrMachine, ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    CHECK_ERROR2(hrc, ptrClipboard, COMSETTER(Mode)(enmMode));
    if (SUCCEEDED(hrc))
    {
        CHECK_ERROR2(hrc, ptrMachine, SaveSettings());
    }
    pArg->session->UnlockMachine();
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Handles the clipboard set-filetransfers subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleSetFileTransfers(HandlerArg *pArg, int argc, char **argv)
{
    if (argc <= 2)
        return errorSyntax(Clipboard::tr("Missing argument to '%s'."), argv[1]);
    if (argc > 3)
        return errorSyntax(Clipboard::tr("Too many arguments to '%s'."), argv[1]);

    bool fEnabled;
    if (RT_FAILURE(parseBool(argv[2], &fEnabled)))
        return errorSyntax(Clipboard::tr("Invalid '%s' argument '%s'."), argv[1], argv[2]);

    ComPtr<IMachine> ptrMachine;
    ComPtr<IClipboardSettings> ptrClipboard;
    HRESULT hrc = shclGetSettings(pArg, argv[0], ptrMachine, ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    CHECK_ERROR2(hrc, ptrClipboard, COMSETTER(FileTransfersEnabled)(fEnabled));
    if (SUCCEEDED(hrc))
    {
        CHECK_ERROR2(hrc, ptrMachine, SaveSettings());
    }
    pArg->session->UnlockMachine();
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a file-system object type to a printable string.
 *
 * @returns Printable file-system object type name.
 * @param   enmType         File-system object type.
 */
static const char *shclFsObjTypeToString(FsObjType_T enmType)
{
    switch (enmType)
    {
        case FsObjType_Unknown:   return "unknown";
        case FsObjType_Fifo:      return "fifo";
        case FsObjType_DevChar:   return "char-device";
        case FsObjType_Directory: return "directory";
        case FsObjType_DevBlock:  return "block-device";
        case FsObjType_File:      return "file";
        case FsObjType_Symlink:   return "symlink";
        case FsObjType_Socket:    return "socket";
        case FsObjType_WhiteOut:  return "whiteout";
        default:                  break;
    }
    return "unknown";
}




/**
 * Gets a transfer ID, returning zero on failure.
 *
 * @returns Transfer ID or zero.
 * @param   ptrTransfer     Transfer object.
 */
static ULONG shclGetTransferId(const ComPtr<IClipboardTransfer> &ptrTransfer)
{
    ULONG idTransfer = 0;
    if (ptrTransfer.isNotNull())
        ptrTransfer->COMGETTER(Id)(&idTransfer);
    return idTransfer;
}


/**
 * Resolves the first transfer matching a direction.
 *
 * @returns COM status code.
 * @param   ptrManager      Transfer manager.
 * @param   enmDirection    Direction to match.
 * @param   ptrTransfer     Where to return the transfer, if any.
 */
static HRESULT shclGetFirstTransfer(const ComPtr<IClipboardTransferManager> &ptrManager,
                                    ClipboardTransferDirection_T enmDirection,
                                    ComPtr<IClipboardTransfer> &ptrTransfer)
{
    ptrTransfer.setNull();
    SafeIfaceArray<IClipboardTransfer> aTransfers;
    HRESULT hrc = ptrManager->GetTransfers(enmDirection, 0 /* aFlags */, ComSafeArrayAsOutParam(aTransfers));
    if (FAILED(hrc))
        return hrc;
    if (aTransfers.size())
        ptrTransfer = aTransfers[0];
    return S_OK;
}


/**
 * Responds to a transfer interaction event using VBoxManage's default policy.
 *
 * @returns Process exit code.
 * @param   ptrManager      Transfer manager.
 * @param   ptrTransfer     Transfer being interacted with.
 * @param   enmInteraction  Interaction kind.
 * @param   strPath         Transfer-relative interaction path.
 */
static RTEXITCODE shclRespondToTransferInteraction(const ComPtr<IClipboardTransferManager> &ptrManager,
                                                   const ComPtr<IClipboardTransfer> &ptrTransfer,
                                                   ClipboardTransferInteraction_T enmInteraction,
                                                   const Utf8Str &strPath)
{
    HRESULT hrc = S_OK;
    switch (enmInteraction)
    {
        case ClipboardTransferInteraction_None:
            return RTEXITCODE_SUCCESS;

        case ClipboardTransferInteraction_Approval:
            shclVerbose("transfer: approving transfer id=%RU32", (uint32_t)shclGetTransferId(ptrTransfer));
            hrc = ptrManager->Approve(ptrTransfer, 0 /* aFlags */);
            break;

        case ClipboardTransferInteraction_Destination:
            shclVerbose("transfer: accepting destination request for path='%s'", strPath.c_str());
            hrc = ptrManager->Respond(ptrTransfer, enmInteraction, Bstr(strPath).raw(), ClipboardTransferResponse_Accept,
                                      Bstr("").raw(), 0 /* aFlags */);
            break;

        case ClipboardTransferInteraction_Overwrite:
            shclVerbose("transfer: accepting overwrite request for path='%s'", strPath.c_str());
            hrc = ptrManager->Respond(ptrTransfer, enmInteraction, Bstr(strPath).raw(), ClipboardTransferResponse_Overwrite,
                                      Bstr("").raw(), 0 /* aFlags */);
            break;

        case ClipboardTransferInteraction_Rename:
        case ClipboardTransferInteraction_ErrorRecovery:
            shclVerbose("transfer: canceling unsupported interaction '%s' for path='%s'",
                        shclTransferInteractionToString(enmInteraction), strPath.c_str());
            hrc = ptrManager->Respond(ptrTransfer, enmInteraction, Bstr(strPath).raw(), ClipboardTransferResponse_Cancel,
                                      Bstr("").raw(), 0 /* aFlags */);
            break;

        default:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Unknown clipboard transfer interaction: %u"),
                                  (unsigned)enmInteraction);
    }

    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Responding to clipboard transfer interaction failed: %Rhrc"), hrc);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a clipboard transfer event for transfer copy/paste commands.
 *
 * @returns Process exit code.
 * @param   ptrManager          Transfer manager.
 * @param   ptrEvent            Transfer event.
 * @param   enmExpectedDirection Expected transfer direction.
 * @param   idExpectedTransfer  Expected transfer ID, or zero for any matching transfer.
 * @param   ptrEventTransfer    Where to return the event transfer, optional.
 * @param   pfTerminal          Where to return whether the event was terminal.
 * @param   pfFailed            Where to return whether the terminal event failed.
 */
static RTEXITCODE shclHandleTransferEvent(const ComPtr<IClipboardTransferManager> &ptrManager,
                                          const ComPtr<IEvent> &ptrEvent,
                                          ClipboardTransferDirection_T enmExpectedDirection,
                                          ULONG idExpectedTransfer,
                                          ComPtr<IClipboardTransfer> *ptrEventTransfer,
                                          bool *pfTerminal,
                                          bool *pfFailed)
{
    if (pfTerminal)
        *pfTerminal = false;
    if (pfFailed)
        *pfFailed = false;
    if (ptrEventTransfer)
        ptrEventTransfer->setNull();

    ComPtr<IClipboardTransferEvent> ptrTransferEvent = ptrEvent;
    if (ptrTransferEvent.isNull())
        return RTEXITCODE_SUCCESS;

    ComPtr<IClipboardTransfer> ptrTransfer;
    HRESULT hrc = ptrTransferEvent->COMGETTER(Transfer)(ptrTransfer.asOutParam());
    if (FAILED(hrc) || ptrTransfer.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Inspecting clipboard transfer event failed: %Rhrc"), hrc);

    ULONG idTransfer = 0;
    ptrTransfer->COMGETTER(Id)(&idTransfer);

    ClipboardTransferDirection_T enmDirection = ClipboardTransferDirection_Any;
    hrc = ptrTransfer->COMGETTER(Direction)(&enmDirection);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading clipboard transfer direction failed: %Rhrc"), hrc);

    if (   enmExpectedDirection != ClipboardTransferDirection_Any
        && enmDirection != enmExpectedDirection)
    {
        shclVerbose("transfer: ignoring event for transfer id=%RU32 direction=%s",
                    (uint32_t)idTransfer, shclTransferDirectionToString(enmDirection));
        return RTEXITCODE_SUCCESS;
    }
    if (idExpectedTransfer && idTransfer != idExpectedTransfer)
    {
        shclVerbose("transfer: ignoring event for transfer id=%RU32; expected id=%RU32",
                    (uint32_t)idTransfer, (uint32_t)idExpectedTransfer);
        return RTEXITCODE_SUCCESS;
    }

    ClipboardTransferState_T enmState = ClipboardTransferState_Added;
    ClipboardTransferInteraction_T enmInteraction = ClipboardTransferInteraction_None;
    ClipboardError_T enmError = ClipboardError_None;
    Bstr bstrPath;
    Bstr bstrMessage;
    ptrTransferEvent->COMGETTER(State)(&enmState);
    ptrTransferEvent->COMGETTER(Interaction)(&enmInteraction);
    ptrTransferEvent->COMGETTER(Path)(bstrPath.asOutParam());
    ptrTransferEvent->COMGETTER(Message)(bstrMessage.asOutParam());
    ptrTransferEvent->COMGETTER(Error)(&enmError);

    Utf8Str strPath(bstrPath);
    Utf8Str strMessage(bstrMessage);
    shclVerbose("transfer: event id=%RU32 direction=%s state=%s interaction=%s path='%s' error=%u message='%s'",
                (uint32_t)idTransfer, shclTransferDirectionToString(enmDirection),
                ShClHlpTransferStateToString(enmState), shclTransferInteractionToString(enmInteraction),
                strPath.c_str(), (unsigned)enmError, strMessage.c_str());

    if (enmState == ClipboardTransferState_Interaction)
    {
        RTEXITCODE rcExit = shclRespondToTransferInteraction(ptrManager, ptrTransfer, enmInteraction, strPath);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    if (ptrEventTransfer)
        *ptrEventTransfer = ptrTransfer;
    if (   enmState == ClipboardTransferState_Completed
        || enmState == ClipboardTransferState_Canceled
        || enmState == ClipboardTransferState_Failed
        || enmState == ClipboardTransferState_Removed)
    {
        if (pfTerminal)
            *pfTerminal = true;
        if (pfFailed)
            *pfFailed = enmState != ClipboardTransferState_Completed;
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Waits for clipboard transfer events.
 *
 * @returns Process exit code.
 * @param   ptrSession          Clipboard session.
 * @param   ptrManager          Transfer manager.
 * @param   enmExpectedDirection Expected transfer direction.
 * @param   idExpectedTransfer  Expected transfer ID, or zero for any matching transfer.
 * @param   cMsTimeout          Maximum wait time.
 */
static RTEXITCODE shclWaitForTransferCompletion(const ComPtr<IClipboardSession> &ptrSession,
                                                const ComPtr<IClipboardTransferManager> &ptrManager,
                                                ClipboardTransferDirection_T enmExpectedDirection,
                                                ULONG idExpectedTransfer,
                                                uint32_t cMsTimeout)
{
    if (cMsTimeout == 0)
        return RTEXITCODE_SUCCESS;

    SafeArray<VBoxEventType_T> aEventTypes;
    aEventTypes.push_back(VBoxEventType_OnClipboardTransfer);
    aEventTypes.push_back(VBoxEventType_OnClipboardError);

    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    RTEXITCODE rcExit = shclRegisterListener(ptrSession, aEventTypes, ptrEventSource, ptrListener);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    shclSignalHandlerInstall();

    uint64_t const msStart = RTTimeMilliTS();
    bool fTimedOut = false;
    for (;;)
    {
        if (shclSignalWasCaught())
            break;

        uint32_t cMsThisWait = 1000;
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            if (cMsElapsed >= cMsTimeout)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)1000, cMsTimeout - cMsElapsed);
        }

        ComPtr<IEvent> ptrEvent;
        HRESULT hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND || ptrEvent.isNull())
            continue;
        if (FAILED(hrc))
        {
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard transfer events failed: %Rhrc"), hrc);
        }

        VBoxEventType_T enmType = VBoxEventType_Invalid;
        ptrEvent->COMGETTER(Type)(&enmType);
        if (enmType == VBoxEventType_OnClipboardError)
        {
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard error while waiting for file transfer."));
        }

        bool fTerminal = false;
        bool fFailed = false;
        rcExit = shclHandleTransferEvent(ptrManager, ptrEvent, enmExpectedDirection, idExpectedTransfer,
                                         NULL /* ptrEventTransfer */, &fTerminal, &fFailed);
        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;
        if (fTerminal)
        {
            rcExit = fFailed
                   ? RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard file transfer did not complete successfully."))
                   : RTEXITCODE_SUCCESS;
            break;
        }
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
    if (fInterrupted)
        shclVerbose("transfer: interrupted; shutting down");
    else if (fTimedOut)
        shclInfo(Clipboard::tr("Timed out waiting for clipboard file transfer completion."));
    return rcExit;
}


/**
 * Builds a safe host path below a destination directory from a transfer path.
 *
 * @returns VBox status code.
 * @param   pszDestination  Host destination directory.
 * @param   pszTransferPath Transfer-relative path.
 * @param   strHostPath     Where to return the host path.
 */
static int shclTransferPathToHostPath(const char *pszDestination, const char *pszTransferPath, Utf8Str &strHostPath)
{
    if (   !pszDestination
        || !*pszDestination
        || !pszTransferPath)
        return VERR_INVALID_PARAMETER;
    if (   pszTransferPath[0] == '/'
        || pszTransferPath[0] == '\\'
        || strchr(pszTransferPath, '\\')
        || strchr(pszTransferPath, ':'))
        return VERR_INVALID_NAME;

    char szPath[RTPATH_MAX];
    int vrc = RTStrCopy(szPath, sizeof(szPath), pszDestination);
    if (RT_FAILURE(vrc))
        return vrc;

    char *pszCopy = RTStrDup(pszTransferPath);
    if (!pszCopy)
        return VERR_NO_MEMORY;

    char *psz = pszCopy;
    while (RT_SUCCESS(vrc) && *psz)
    {
        char *pszSlash = strchr(psz, '/');
        if (pszSlash)
            *pszSlash = '\0';
        if (   !*psz
            || !strcmp(psz, ".")
            || !strcmp(psz, ".."))
        {
            vrc = VERR_INVALID_NAME;
            break;
        }
        vrc = RTPathAppend(szPath, sizeof(szPath), psz);
        if (!pszSlash)
            break;
        psz = pszSlash + 1;
    }
    RTStrFree(pszCopy);
    if (RT_SUCCESS(vrc))
        strHostPath = szPath;
    return vrc;
}


/**
 * Ensures that a file's parent directory exists.
 *
 * @returns VBox status code.
 * @param   pszPath         Host file path.
 */
static int shclEnsureParentDirectory(const char *pszPath)
{
    char *pszParent = RTStrDup(pszPath);
    if (!pszParent)
        return VERR_NO_MEMORY;
    RTPathStripFilename(pszParent);
    int vrc = *pszParent ? RTDirCreateFullPath(pszParent, 0755) : VINF_SUCCESS;
    RTStrFree(pszParent);
    return vrc;
}


/**
 * Copies one transfer file to the host filesystem.
 *
 * @returns Process exit code.
 * @param   ptrTransfer     Transfer object.
 * @param   pszTransferPath Transfer-relative file path.
 * @param   pszHostPath     Destination host path.
 */
static RTEXITCODE shclCopyTransferFileToHost(const ComPtr<IClipboardTransfer> &ptrTransfer,
                                             const char *pszTransferPath,
                                             const char *pszHostPath)
{
    ComPtr<IClipboardTransferFile> ptrFile;
    HRESULT hrc = ptrTransfer->OpenFile(Bstr(pszTransferPath).raw(), FileAccessMode_ReadOnly,
                                        FileOpenAction_OpenExisting, FileSharingMode_Read, 0 /* aCreationMode */,
                                        ptrFile.asOutParam());
    if (FAILED(hrc) || ptrFile.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Opening clipboard transfer file '%s' failed: %Rhrc"),
                              pszTransferPath, hrc);

    int vrc = shclEnsureParentDirectory(pszHostPath);
    if (RT_SUCCESS(vrc))
    {
        RTFILE hFile = NIL_RTFILE;
        vrc = RTFileOpen(&hFile, pszHostPath, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_ALL);
        if (RT_SUCCESS(vrc))
        {
            uint64_t cbTotal = 0;
            for (;;)
            {
                SafeArray<BYTE> aChunk;
                hrc = ptrFile->Read(_64K, 0 /* timeoutMS */, ComSafeArrayAsOutParam(aChunk));
                if (FAILED(hrc))
                {
                    vrc = VERR_READ_ERROR;
                    RTMsgError(Clipboard::tr("Reading clipboard transfer file '%s' failed: %Rhrc"), pszTransferPath, hrc);
                    break;
                }
                if (!aChunk.size())
                    break;
                vrc = RTFileWrite(hFile, aChunk.raw(), aChunk.size(), NULL /* pcbWritten */);
                if (RT_FAILURE(vrc))
                    break;
                cbTotal += aChunk.size();
            }
            int vrc2 = RTFileClose(hFile);
            if (RT_SUCCESS(vrc) && RT_FAILURE(vrc2))
                vrc = vrc2;
            if (RT_SUCCESS(vrc))
                shclVerbose("paste: wrote file '%s' (%RU64 bytes)", pszHostPath, cbTotal);
        }
    }

    ptrFile->Close();
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Writing host file '%s' failed: %Rrc"), pszHostPath, vrc);
    return RTEXITCODE_SUCCESS;
}


/**
 * Materializes a transfer tree below a host destination directory.
 *
 * @returns Process exit code.
 * @param   ptrTransfer     Transfer object.
 * @param   pszDestination  Host destination directory.
 */
static RTEXITCODE shclMaterializeTransferToHost(const ComPtr<IClipboardTransfer> &ptrTransfer, const char *pszDestination)
{
    int vrc = RTDirCreateFullPath(pszDestination, 0755);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating destination directory '%s' failed: %Rrc"),
                              pszDestination, vrc);

    SafeIfaceArray<IClipboardTransferFsObjInfo> aNodes;
    HRESULT hrc = ptrTransfer->List(Bstr("").raw(), ClipboardTransferListFlag_None, ComSafeArrayAsOutParam(aNodes));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Listing clipboard transfer contents failed: %Rhrc"), hrc);
    if (!aNodes.size())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard transfer contains no files or directories."));

    for (size_t i = 0; i < aNodes.size(); ++i)
    {
        Bstr bstrPath;
        FsObjType_T enmType = FsObjType_Unknown;
        LONG64 cbObject = 0;
        hrc = aNodes[i]->COMGETTER(Path)(bstrPath.asOutParam());
        if (SUCCEEDED(hrc))
            hrc = aNodes[i]->COMGETTER(Type)(&enmType);
        if (SUCCEEDED(hrc))
            aNodes[i]->COMGETTER(ObjectSize)(&cbObject);
        if (FAILED(hrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading clipboard transfer entry metadata failed: %Rhrc"), hrc);

        Utf8Str strTransferPath(bstrPath);
        if (strTransferPath.isEmpty())
            continue;

        Utf8Str strHostPath;
        vrc = shclTransferPathToHostPath(pszDestination, strTransferPath.c_str(), strHostPath);
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Invalid transfer path '%s': %Rrc"),
                                  strTransferPath.c_str(), vrc);

        shclVerbose("paste: materializing %s '%s' -> '%s' size=%RI64",
                    shclFsObjTypeToString(enmType), strTransferPath.c_str(), strHostPath.c_str(), (int64_t)cbObject);
        if (enmType == FsObjType_Directory)
        {
            vrc = RTDirCreateFullPath(strHostPath.c_str(), 0755);
            if (RT_FAILURE(vrc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating directory '%s' failed: %Rrc"),
                                      strHostPath.c_str(), vrc);
        }
        else if (enmType == FsObjType_File)
        {
            RTEXITCODE rcExit = shclCopyTransferFileToHost(ptrTransfer, strTransferPath.c_str(), strHostPath.c_str());
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }
        else
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  Clipboard::tr("Clipboard transfer entry '%s' has unsupported type '%s'."),
                                  strTransferPath.c_str(), shclFsObjTypeToString(enmType));
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the clipboard transfer offer subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleTransferOffer(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--recursive", 'R',                   RTGETOPT_REQ_NOTHING },
        { "--timeout",   CLIPBOARD_OPT_TIMEOUT, RTGETOPT_REQ_UINT32 },
        { "--verbose",   'v',                   RTGETOPT_REQ_NOTHING }
    };

    std::vector<Utf8Str> vecAbsSources;
    bool fRecursive = false;
    bool fSawDirectory = false;
    uint32_t cMsTimeout = CLIPBOARD_COPY_DEFAULT_TIMEOUT;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 3, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'R':
                fRecursive = true;
                break;
            case CLIPBOARD_OPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            case VINF_GETOPT_NOT_OPTION:
            {
                const char *pszSource = ValueUnion.psz;
                if (!pszSource || !*pszSource)
                    return errorSyntax(Clipboard::tr("Empty source path for clipboard transfer offer."));

                char szAbsSource[RTPATH_MAX];
                int vrc = RTPathAbs(pszSource, szAbsSource, sizeof(szAbsSource));
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Resolving source path '%s' failed: %Rrc"),
                                          pszSource, vrc);

                RTFSOBJINFO ObjInfo;
                vrc = RTPathQueryInfo(szAbsSource, &ObjInfo, RTFSOBJATTRADD_NOTHING);
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Cannot access source path '%s': %Rrc"),
                                          szAbsSource, vrc);
                if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
                    fSawDirectory = true;
                else if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                          Clipboard::tr("Source path '%s' is not an existing file or directory: %Rrc"),
                                          szAbsSource, VERR_NOT_A_FILE);

                shclVerbose("transfer offer: source[%zu]='%s' type=%s", vecAbsSources.size(), szAbsSource,
                            RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode) ? "directory" : "file");
                vecAbsSources.push_back(Utf8Str(szAbsSource));
                break;
            }
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (vecAbsSources.empty())
        return errorSyntax(Clipboard::tr("Missing source path for clipboard transfer offer."));

    if (fSawDirectory && !fRecursive)
        shclVerbose("transfer offer: at least one source is a directory; directory transfers are recursive by default (use -R for explicitness)");

    shclVerbose("transfer offer: target='%s' sources=%zu recursive=%RTbool timeout=%s", argv[0], vecAbsSources.size(), fRecursive,
                cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            false /* fExcludeOwnChanges */,
                            false /* fExcludeReflections */,
                            true  /* fIncludeInitialState */,
                            false /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    }

    ComPtr<IClipboardTransferManager> ptrManager;
    hrc = ptrClipboard->COMGETTER(Transfers)(ptrManager.asOutParam());
    if (FAILED(hrc) || ptrManager.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard transfer manager failed: %Rhrc"), hrc);
    }

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = ptrManager->Create(ClipboardTransferDirection_ToGuest, ClipboardSource_Host,
                             ClipboardAction_Copy, ptrTransfer.asOutParam());
    if (FAILED(hrc) || ptrTransfer.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard file transfer failed: %Rhrc"), hrc);
    }

    SafeArray<IN_BSTR> aSourcePaths;
    for (size_t i = 0; i < vecAbsSources.size(); i++)
        aSourcePaths.push_back(Bstr(vecAbsSources[i]).raw());
    hrc = ptrTransfer->SetSourcePaths(ComSafeArrayAsInParam(aSourcePaths));
    if (FAILED(hrc))
    {
        HRESULT const hrcRemove = ptrManager->Remove(ptrTransfer);
        if (FAILED(hrcRemove))
            shclVerbose("transfer offer: removing the unconfigured manager transfer failed: %Rhrc", hrcRemove);
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Configuring clipboard file transfer source paths failed: %Rhrc"), hrc);
    }

    ULONG idTransfer = shclGetTransferId(ptrTransfer);
    if (idTransfer)
        shclVerbose("transfer offer: configured manager transfer id=%RU32", (uint32_t)idTransfer);
    shclInfo(Clipboard::tr("Configured %zu host path(s) on a manager-tracked clipboard file transfer."),
             vecAbsSources.size());

    RTEXITCODE rcExit = shclWaitForTransferCompletion(ptrClipboardSession, ptrManager,
                                                      ClipboardTransferDirection_ToGuest, idTransfer, cMsTimeout);
    pArg->session->UnlockMachine();
    return rcExit;
}


/**
 * Waits for and materializes a guest-to-host clipboard file transfer.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session.
 * @param   ptrManager      Transfer manager.
 * @param   pszDestination  Host destination directory.
 * @param   cMsTimeout      Maximum wait time.
 */
static RTEXITCODE shclWaitForPasteTransfer(const ComPtr<IClipboardSession> &ptrSession,
                                           const ComPtr<IClipboardTransferManager> &ptrManager,
                                           const char *pszDestination,
                                           uint32_t cMsTimeout)
{
    ComPtr<IClipboardTransfer> ptrTransfer;
    HRESULT hrc = shclGetFirstTransfer(ptrManager, ClipboardTransferDirection_ToHost, ptrTransfer);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Querying clipboard file transfers failed: %Rhrc"), hrc);
    if (ptrTransfer.isNotNull())
    {
        shclVerbose("paste: using current transfer id=%RU32", (uint32_t)shclGetTransferId(ptrTransfer));
        return shclMaterializeTransferToHost(ptrTransfer, pszDestination);
    }
    if (cMsTimeout == 0)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("No guest-to-host clipboard file transfer is currently available."));

    SafeArray<VBoxEventType_T> aEventTypes;
    aEventTypes.push_back(VBoxEventType_OnClipboardTransfer);
    aEventTypes.push_back(VBoxEventType_OnClipboardError);

    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    RTEXITCODE rcExit = shclRegisterListener(ptrSession, aEventTypes, ptrEventSource, ptrListener);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    shclSignalHandlerInstall();

    uint64_t const msStart = RTTimeMilliTS();
    bool fTimedOut = false;
    for (;;)
    {
        if (shclSignalWasCaught())
            break;

        uint32_t cMsThisWait = 1000;
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            if (cMsElapsed >= cMsTimeout)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)1000, cMsTimeout - cMsElapsed);
        }

        ComPtr<IEvent> ptrEvent;
        hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND || ptrEvent.isNull())
            continue;
        if (FAILED(hrc))
        {
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard transfer events failed: %Rhrc"), hrc);
        }

        VBoxEventType_T enmType = VBoxEventType_Invalid;
        ptrEvent->COMGETTER(Type)(&enmType);
        if (enmType == VBoxEventType_OnClipboardError)
        {
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard error while waiting for guest file transfer."));
        }

        ComPtr<IClipboardTransfer> ptrEventTransfer;
        bool fTerminal = false;
        bool fFailed = false;
        rcExit = shclHandleTransferEvent(ptrManager, ptrEvent, ClipboardTransferDirection_ToHost, 0 /* idExpectedTransfer */,
                                         &ptrEventTransfer, &fTerminal, &fFailed);
        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;
        if (ptrEventTransfer.isNotNull() && !fTerminal)
        {
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return shclMaterializeTransferToHost(ptrEventTransfer, pszDestination);
        }
        if (fTerminal && fFailed)
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Guest clipboard file transfer failed before data was available."));
            break;
        }
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
    if (fInterrupted)
        shclVerbose("paste: interrupted; shutting down");
    else if (fTimedOut)
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Timed out waiting for guest clipboard file transfer."));
    return rcExit;
}


/**
 * Handles the clipboard transfer receive subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleTransferReceive(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--timeout", CLIPBOARD_OPT_TIMEOUT, RTGETOPT_REQ_UINT32 },
        { "--verbose", 'v',                   RTGETOPT_REQ_NOTHING }
    };

    const char *pszDestination = NULL;
    uint32_t cMsTimeout = CLIPBOARD_PASTE_DEFAULT_TIMEOUT;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 3, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            case VINF_GETOPT_NOT_OPTION:
                if (pszDestination)
                    return errorSyntax(Clipboard::tr("Too many destination paths specified."));
                pszDestination = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!pszDestination || !*pszDestination)
        return errorSyntax(Clipboard::tr("Missing destination path for clipboard transfer receive."));

    char szAbsDestination[RTPATH_MAX];
    int vrc = RTPathAbs(pszDestination, szAbsDestination, sizeof(szAbsDestination));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Resolving destination path '%s' failed: %Rrc"),
                              pszDestination, vrc);

    shclVerbose("transfer receive: target='%s' destination='%s' timeout=%s", argv[0], szAbsDestination,
                cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            false /* fExcludeOwnChanges */,
                            false /* fExcludeReflections */,
                            true  /* fIncludeInitialState */,
                            false /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    }

    ComPtr<IClipboardTransferManager> ptrManager;
    hrc = ptrClipboard->COMGETTER(Transfers)(ptrManager.asOutParam());
    if (FAILED(hrc) || ptrManager.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard transfer manager failed: %Rhrc"), hrc);
    }

    RTEXITCODE rcExit = shclWaitForPasteTransfer(ptrClipboardSession, ptrManager, szAbsDestination, cMsTimeout);
    if (rcExit == RTEXITCODE_SUCCESS)
        shclInfo(Clipboard::tr("Received guest clipboard file transfer into '%s'."), szAbsDestination);
    pArg->session->UnlockMachine();
    return rcExit;
}


/**
 * Parses a clipboard transfer direction string.
 *
 * @returns Process exit code.
 * @param   pszDirection    Direction string to parse.
 * @param   penmDirection   Where to return the parsed direction.
 */
static RTEXITCODE shclParseTransferDirection(const char *pszDirection, ClipboardTransferDirection_T *penmDirection)
{
    AssertPtrReturn(penmDirection, RTEXITCODE_FAILURE);
    if (!pszDirection || !*pszDirection)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard transfer direction must not be empty."));
    if (!RTStrICmp(pszDirection, "any"))
        *penmDirection = ClipboardTransferDirection_Any;
    else if (!RTStrICmp(pszDirection, "to-guest"))
        *penmDirection = ClipboardTransferDirection_ToGuest;
    else if (!RTStrICmp(pszDirection, "to-host"))
        *penmDirection = ClipboardTransferDirection_ToHost;
    else
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Invalid clipboard transfer direction '%s'. Use any, to-guest, or to-host."),
                              pszDirection);
    return RTEXITCODE_SUCCESS;
}


/**
 * Finds a clipboard transfer by ID.
 *
 * @returns COM status code.
 * @param   ptrManager      Transfer manager.
 * @param   idTransfer      Transfer ID to find.
 * @param   ptrTransfer     Where to return the matching transfer, if any.
 */
static HRESULT shclGetTransferById(const ComPtr<IClipboardTransferManager> &ptrManager,
                                   ULONG idTransfer,
                                   ComPtr<IClipboardTransfer> &ptrTransfer)
{
    ptrTransfer.setNull();
    SafeIfaceArray<IClipboardTransfer> aTransfers;
    HRESULT hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0 /* aFlags */, ComSafeArrayAsOutParam(aTransfers));
    if (FAILED(hrc))
        return hrc;
    for (size_t i = 0; i < aTransfers.size(); i++)
    {
        ULONG idThisTransfer = 0;
        hrc = aTransfers[i]->COMGETTER(Id)(&idThisTransfer);
        if (FAILED(hrc))
            return hrc;
        if (idThisTransfer == idTransfer)
        {
            ptrTransfer = aTransfers[i];
            break;
        }
    }
    return S_OK;
}


/**
 * Handles the clipboard transfer list subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleTransferList(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--direction", CLIPBOARD_OPT_DIRECTION, RTGETOPT_REQ_STRING }
    };

    ClipboardTransferDirection_T enmDirection = ClipboardTransferDirection_Any;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 3, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_DIRECTION:
            {
                RTEXITCODE rcExit = shclParseTransferDirection(ValueUnion.psz, &enmDirection);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;
            }
            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(Clipboard::tr("The clipboard transfer list command takes no positional operands."));
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IClipboardTransferManager> ptrManager;
    hrc = ptrClipboard->COMGETTER(Transfers)(ptrManager.asOutParam());
    if (FAILED(hrc) || ptrManager.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard transfer manager failed: %Rhrc"), hrc);
    }

    SafeIfaceArray<IClipboardTransfer> aTransfers;
    hrc = ptrManager->GetTransfers(enmDirection, 0 /* aFlags */, ComSafeArrayAsOutParam(aTransfers));
    if (FAILED(hrc))
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Querying clipboard file transfers failed: %Rhrc"), hrc);
    }

    for (size_t i = 0; i < aTransfers.size(); i++)
    {
        ULONG idTransfer = 0;
        ClipboardTransferDirection_T enmThisDirection = ClipboardTransferDirection_Any;
        ClipboardSource_T enmSource = ClipboardSource_Host;
        ClipboardAction_T enmAction = ClipboardAction_Copy;
        ClipboardTransferState_T enmState = ClipboardTransferState_Added;
        ClipboardError_T enmError = ClipboardError_None;
        Bstr bstrMessage;

        hrc = aTransfers[i]->COMGETTER(Id)(&idTransfer);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(Direction)(&enmThisDirection);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(Source)(&enmSource);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(Action)(&enmAction);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(State)(&enmState);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(Error)(&enmError);
        if (SUCCEEDED(hrc)) hrc = aTransfers[i]->COMGETTER(Message)(bstrMessage.asOutParam());
        if (FAILED(hrc))
        {
            pArg->session->UnlockMachine();
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading clipboard transfer metadata failed: %Rhrc"), hrc);
        }

        Utf8Str strMessage(bstrMessage);
        RTPrintf("id=%RU32 direction=%s source=%s action=%s state=%s error=%u message=\"%s\"\n",
                 (uint32_t)idTransfer, shclTransferDirectionToString(enmThisDirection), ShClHlpSourceToString(enmSource),
                 shclActionToString(enmAction), ShClHlpTransferStateToString(enmState), (unsigned)enmError, strMessage.c_str());
    }

    pArg->session->UnlockMachine();
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the clipboard transfer cancel subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleTransferCancel(HandlerArg *pArg, int argc, char **argv)
{
    uint32_t idTransfer = 0;
    bool fHaveId = false;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, NULL /* paOptions */, 0 /* cOptions */, 3, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
            {
                if (fHaveId)
                    return errorSyntax(Clipboard::tr("Too many clipboard transfer IDs specified."));
                char *pszNext = NULL;
                int vrc = RTStrToUInt32Ex(ValueUnion.psz, &pszNext, 0, &idTransfer);
                if (RT_FAILURE(vrc) || *pszNext || !idTransfer)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Invalid clipboard transfer ID '%s'."),
                                          ValueUnion.psz);
                fHaveId = true;
                break;
            }
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!fHaveId)
        return errorSyntax(Clipboard::tr("Missing clipboard transfer ID."));

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IClipboardTransferManager> ptrManager;
    hrc = ptrClipboard->COMGETTER(Transfers)(ptrManager.asOutParam());
    if (FAILED(hrc) || ptrManager.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard transfer manager failed: %Rhrc"), hrc);
    }

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = shclGetTransferById(ptrManager, idTransfer, ptrTransfer);
    if (FAILED(hrc))
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Querying clipboard file transfers failed: %Rhrc"), hrc);
    }
    if (ptrTransfer.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard transfer %RU32 was not found."), idTransfer);
    }

    hrc = ptrManager->Cancel(ptrTransfer);
    pArg->session->UnlockMachine();
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Canceling clipboard transfer %RU32 failed: %Rhrc"),
                              idTransfer, hrc);

    shclInfo(Clipboard::tr("Canceled clipboard transfer %RU32."), idTransfer);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the clipboard transfer subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleTransfer(HandlerArg *pArg, int argc, char **argv)
{
    if (argc <= 2)
        return errorSyntax(Clipboard::tr("Missing clipboard transfer subcommand."));

    const char *pszSubcommand = argv[2];
    if (   !strcmp(pszSubcommand, "offer")
        || !strcmp(pszSubcommand, "send"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_TRANSFER_OFFER);
        return shclHandleTransferOffer(pArg, argc, argv);
    }
    if (   !strcmp(pszSubcommand, "receive")
        || !strcmp(pszSubcommand, "recv"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_TRANSFER_RECEIVE);
        return shclHandleTransferReceive(pArg, argc, argv);
    }
    if (!strcmp(pszSubcommand, "list"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_TRANSFER_LIST);
        return shclHandleTransferList(pArg, argc, argv);
    }
    if (!strcmp(pszSubcommand, "cancel"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_TRANSFER_CANCEL);
        return shclHandleTransferCancel(pArg, argc, argv);
    }

    return errorSyntax(Clipboard::tr("Unknown clipboard transfer subcommand '%s'."), pszSubcommand);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Handles the clipboard copy subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleCopy(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--format",  CLIPBOARD_OPT_FORMAT,  RTGETOPT_REQ_STRING },
        { "--raw",     CLIPBOARD_OPT_RAW,     RTGETOPT_REQ_NOTHING },
        { "--timeout", CLIPBOARD_OPT_TIMEOUT, RTGETOPT_REQ_UINT32 },
        { "--verbose", 'v',                   RTGETOPT_REQ_NOTHING }
    };

    const char *pszFormat = CLIPBOARD_DEFAULT_FORMAT;
    uint32_t cMsTimeout = CLIPBOARD_COPY_DEFAULT_TIMEOUT;
    bool fRaw = false;
    bool fFormatSpecified = false;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_FORMAT:
                pszFormat = ValueUnion.psz;
                fFormatSpecified = true;
                break;
            case CLIPBOARD_OPT_RAW:
                fRaw = true;
                break;
            case CLIPBOARD_OPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(Clipboard::tr("The clipboard copy command reads data from standard input; use 'clipboard <vm> transfer offer' for file transfers."));
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (!pszFormat || !*pszFormat)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard MIME type must not be empty."));
    if (fRaw && !fFormatSpecified)
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("--raw requires --format; generic binary clipboard data is not supported."));

    shclVerbose("copy: target='%s' format='%s' timeout=%s", argv[0], pszFormat,
                     cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    shclVerbose("copy: live clipboard object acquired");

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            true /* fExcludeOwnChanges */,
                            true /* fExcludeReflections */,
                            false /* fIncludeInitialState */,
                            false /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    }
    shclVerbose("copy: clipboard session acquired");

    shclVerbose("copy: reading standard input");
    std::vector<BYTE> abData;
    int vrc = shclReadStdin(abData);
    if (RT_FAILURE(vrc))
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard input: %Rrc"), vrc);
    }

    shclVerbose("copy: read %zu input bytes", abData.size());

    SafeArray<BYTE> aBuffer(abData.size());
    if (aBuffer.size() != abData.size())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to allocate clipboard input buffer."));
    }
    if (!abData.empty())
        memcpy(aBuffer.raw(), &abData[0], abData.size());

    RTEXITCODE rcExit = shclHandleCopyProvideData(ptrClipboardSession, pszFormat, aBuffer, cMsTimeout);
    pArg->session->UnlockMachine();
    return rcExit;
}


/**
 * Reports current guest clipboard formats through the host clipboard endpoint for paste.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to read formats from.
 * @param   ptrHostClipboard Host clipboard endpoint used to trigger guest rendering.
 * @param   pszFormat       Requested MIME type, optional.
 * @param   pfReported      Where to return whether formats were reported.
 */
static RTEXITCODE shclHandlePasteReportGuestFormats(const ComPtr<IClipboardSession> &ptrSession,
                                                    const ComPtr<IHostClipboard> &ptrHostClipboard,
                                                    const char *pszFormat,
                                                    bool *pfReported)
{
    AssertPtrReturn(pfReported, RTEXITCODE_FAILURE);
    *pfReported = false;

    SafeIfaceArray<IClipboardFormat> aFormats;
    HRESULT hrc = ptrSession->ReadFormats(ComSafeArrayAsOutParam(aFormats));
    if (FAILED(hrc))
    {
        shclVerbose("paste: reading current guest formats failed: %Rhrc", hrc);
        if (   hrc == VBOX_E_SHCL_NO_DATA
            || hrc == VBOX_E_SHCL_ACCESS_DENIED
            || hrc == VBOX_E_SHCL_GUEST_ERROR)
            return RTEXITCODE_SUCCESS;
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading guest clipboard formats failed: %Rhrc"), hrc);
    }

    std::vector<ComPtr<IClipboardFormat> > vecReportFormats;
    std::vector<Utf8Str> vecReportMimeTypes;
    Utf8Str strPreferredMimeType;
    hrc = shclFilterSupportedFormats(aFormats, &vecReportFormats, &vecReportMimeTypes, &strPreferredMimeType);
    if (hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED)
    {
        shclVerbose("paste: current guest offer has no supported formats");
        return RTEXITCODE_SUCCESS;
    }
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard format metadata: %Rhrc"), hrc);

    if (pszFormat && *pszFormat)
    {
        std::vector<ComPtr<IClipboardFormat> > vecFilteredFormats;
        std::vector<Utf8Str> vecFilteredMimeTypes;
        for (size_t i = 0; i < vecReportMimeTypes.size(); i++)
            if (shclMimeMatches(pszFormat, vecReportMimeTypes[i]))
            {
                vecFilteredFormats.push_back(vecReportFormats[i]);
                vecFilteredMimeTypes.push_back(vecReportMimeTypes[i]);
            }
        vecReportFormats = vecFilteredFormats;
        vecReportMimeTypes = vecFilteredMimeTypes;
        strPreferredMimeType = vecReportMimeTypes.empty() ? Utf8Str() : vecReportMimeTypes[0];
    }

    if (vecReportFormats.empty())
    {
        shclVerbose("paste: current guest offer does not include requested format %s", pszFormat ? pszFormat : "<none>");
        return RTEXITCODE_SUCCESS;
    }

    SafeIfaceArray<IClipboardFormat> aReportFormats(vecReportFormats);
    hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest, ComSafeArrayAsInParam(aReportFormats));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Reporting guest clipboard formats to the host failed: %Rhrc"), hrc);

    *pfReported = true;
    shclVerbose("paste: reported %zu guest format(s) to host clipboard; preferred=%s",
                aReportFormats.size(), strPreferredMimeType.c_str());
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a host clipboard data request for paste and writes the requested data.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to read guest data from.
 * @param   ptrHostClipboard Host clipboard endpoint to satisfy the request through.
 * @param   ptrEvent        Data-requested event to inspect.
 * @param   pszFormat       Requested MIME type, optional.
 * @param   pfNeedWait      Where to return whether paste should keep waiting.
 */
static RTEXITCODE shclHandlePasteDataRequested(const ComPtr<IClipboardSession> &ptrSession,
                                               const ComPtr<IHostClipboard> &ptrHostClipboard,
                                               const ComPtr<IEvent> &ptrEvent,
                                               const char *pszFormat,
                                               bool *pfNeedWait)
{
    AssertPtrReturn(pfNeedWait, RTEXITCODE_FAILURE);
    *pfNeedWait = true;

    ULONG uRequestId = 0;
    ClipboardAction_T enmAction = ClipboardAction_Copy;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    Utf8Str strRequestedMimeType;
    if (!shclGetDataRequestedEvent(ptrEvent, &uRequestId, &enmAction, &enmSource, strRequestedMimeType))
    {
        shclVerbose("paste: ignoring malformed data-request event");
        return RTEXITCODE_SUCCESS;
    }

    shclVerbose("paste: request id=%RU32 action=%s source=%s format=%s", (uint32_t)uRequestId,
                shclActionToString(enmAction), ShClHlpSourceToString(enmSource), strRequestedMimeType.c_str());
    if (   enmAction != ClipboardAction_Copy
        || enmSource != ClipboardSource_Guest
        || !shclMimeMatches(pszFormat, strRequestedMimeType))
    {
        shclVerbose("paste: request id=%RU32 ignored", (uint32_t)uRequestId);
        return RTEXITCODE_SUCCESS;
    }

    ClipboardSource_T enmReadSource = ClipboardSource_Custom;
    Bstr bstrRequestedMimeType(strRequestedMimeType);
    Bstr bstrReadMimeType;
    SafeArray<BYTE> aBuffer;
    HRESULT hrc = ptrSession->ReadDataRaw(enmAction, bstrRequestedMimeType.raw(), &enmReadSource,
                                          bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aBuffer));
    if (FAILED(hrc))
    {
        shclVerbose("paste: request id=%RU32 read failed: %Rhrc", (uint32_t)uRequestId, hrc);
        if (   hrc == VBOX_E_SHCL_NO_DATA
            || hrc == VBOX_E_SHCL_ACCESS_DENIED
            || hrc == VBOX_E_SHCL_GUEST_ERROR)
            return RTEXITCODE_SUCCESS;
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading guest clipboard data failed: %Rhrc"), hrc);
    }

    Utf8Str strReadMimeType(bstrReadMimeType);
    shclVerbose("paste: request id=%RU32 read source=%s format=%s size=%zu", (uint32_t)uRequestId,
                ShClHlpSourceToString(enmReadSource), strReadMimeType.c_str(), aBuffer.size());
    if (enmReadSource != ClipboardSource_Guest)
    {
        shclVerbose("paste: request id=%RU32 ignored non-guest data", (uint32_t)uRequestId);
        return RTEXITCODE_SUCCESS;
    }

    if (aBuffer.size())
    {
        hrc = ptrHostClipboard->ProvideData(uRequestId, enmAction, ClipboardSource_Guest, bstrReadMimeType.raw(),
                                            ComSafeArrayAsInParam(aBuffer));
        if (FAILED(hrc))
            RTMsgWarning(Clipboard::tr("Providing guest clipboard data for host request %RU32 failed: %Rhrc"),
                         (uint32_t)uRequestId, hrc);
    }

    *pfNeedWait = false;
    return shclHandlePasteOutputData(strRequestedMimeType, pszFormat, aBuffer.raw(), aBuffer.size());
}


/**
 * Tries to write currently available guest clipboard data for the paste command.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to read from.
 * @param   pszFormat       Requested MIME type, optional.
 * @param   pfNeedWait      Where to return whether the caller should wait for data.
 * @param   pfGuestOffer    Where to return whether a guest offer appears to be current, optional.
 */
static RTEXITCODE shclHandlePasteTryCurrent(ComPtr<IClipboardSession> const &ptrSession, const char *pszFormat,
                                            bool *pfNeedWait, bool *pfGuestOffer)
{
    AssertPtrReturn(pfNeedWait, RTEXITCODE_FAILURE);
    *pfNeedWait = false;
    if (pfGuestOffer)
        *pfGuestOffer = false;

    std::vector<Utf8Str> vecMimeTypes;
    if (pszFormat && *pszFormat)
        vecMimeTypes.push_back(Utf8Str(pszFormat));
    else
    {
        SafeIfaceArray<IClipboardFormat> aFormats;
        HRESULT hrc = ptrSession->ReadFormats(ComSafeArrayAsOutParam(aFormats));
        if (FAILED(hrc))
        {
            shclVerbose("paste: ReadFormats failed: %Rhrc", hrc);
            if (   hrc == VBOX_E_SHCL_NO_DATA
                || hrc == VBOX_E_SHCL_ACCESS_DENIED
                || hrc == VBOX_E_SHCL_GUEST_ERROR)
            {
                *pfNeedWait = true;
                return RTEXITCODE_SUCCESS;
            }
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading shared clipboard formats failed: %Rhrc"), hrc);
        }

        std::vector<ComPtr<IClipboardFormat> > vecFormats;
        HRESULT hrcFilter = shclFilterSupportedFormats(aFormats, &vecFormats, &vecMimeTypes, NULL /* pstrPreferredMimeType */);
        if (hrcFilter == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED)
        {
            shclVerbose("paste: no supported current clipboard formats; waiting for guest data");
            *pfNeedWait = true;
            return RTEXITCODE_SUCCESS;
        }
        if (FAILED(hrcFilter))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading shared clipboard format metadata failed: %Rhrc"), hrcFilter);
    }

    std::vector<Utf8Str> vecTryMimeTypes;
    if (pszFormat && *pszFormat)
        vecTryMimeTypes = vecMimeTypes;
    else
        for (uint32_t uPriority = 0; uPriority <= 3; uPriority++)
            for (size_t i = 0; i < vecMimeTypes.size(); i++)
                if (shclMimePriority(vecMimeTypes[i]) == uPriority)
                    vecTryMimeTypes.push_back(vecMimeTypes[i]);

    shclVerbose("paste: reading current guest clipboard data from %zu format(s)", vecTryMimeTypes.size());
    bool fNeedWait = false;
    for (size_t i = 0; i < vecTryMimeTypes.size(); i++)
    {
        Utf8Str const &strMimeType = vecTryMimeTypes[i];

        ClipboardSource_T enmSource = ClipboardSource_Host;
        Bstr bstrRequestedMimeType(strMimeType);
        Bstr bstrReadMimeType;
        SafeArray<BYTE> aBuffer;
        HRESULT hrc = ptrSession->ReadDataRaw(ClipboardAction_Copy, bstrRequestedMimeType.raw(), &enmSource,
                                              bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aBuffer));
        if (FAILED(hrc))
        {
            shclVerbose("paste: ReadDataRaw(%s) failed: %Rhrc", strMimeType.c_str(), hrc);
            if (   hrc == VBOX_E_SHCL_NO_DATA
                || hrc == VBOX_E_SHCL_ACCESS_DENIED
                || hrc == VBOX_E_SHCL_GUEST_ERROR)
            {
                if (hrc == VBOX_E_SHCL_GUEST_ERROR && pfGuestOffer)
                    *pfGuestOffer = true;
                fNeedWait = true;
                continue;
            }
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading shared clipboard data failed: %Rhrc"), hrc);
        }

        Utf8Str strReadMimeType(bstrReadMimeType);
        shclVerbose("paste: read source=%s format=%s size=%zu", ShClHlpSourceToString(enmSource),
                    strReadMimeType.c_str(), aBuffer.size());
        if (enmSource != ClipboardSource_Guest)
        {
            shclVerbose("paste: current data is not guest-owned; waiting for guest data");
            fNeedWait = true;
            continue;
        }
        if (pfGuestOffer)
            *pfGuestOffer = true;
        if (!shclMimeMatches(pszFormat, strReadMimeType))
        {
            shclVerbose("paste: current data does not match requested format; waiting for matching data");
            fNeedWait = true;
            continue;
        }

        return shclHandlePasteOutputData(strReadMimeType, pszFormat, aBuffer.raw(), aBuffer.size());
    }

    *pfNeedWait = fNeedWait || !vecTryMimeTypes.empty();
    return RTEXITCODE_SUCCESS;
}


/**
 * Waits for guest clipboard data and writes the matching payload.
 *
 * @returns Process exit code.
 * @param   ptrSession          Clipboard session to wait on.
 * @param   ptrHostClipboard    Host clipboard endpoint used to trigger guest rendering.
 * @param   pszFormat           Requested MIME type, optional.
 * @param   cMsWait             Maximum wait time in milliseconds, or RT_INDEFINITE_WAIT.
 */
static RTEXITCODE shclHandlePasteWaitAndOutput(ComPtr<IClipboardSession> const &ptrSession,
                                               const ComPtr<IHostClipboard> &ptrHostClipboard,
                                               const char *pszFormat,
                                               uint32_t cMsWait)
{
    SafeArray<VBoxEventType_T> aEventTypes;
    aEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardDataChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardSourceChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
    aEventTypes.push_back(VBoxEventType_OnClipboardError);

    shclVerbose("paste: registering listener for guest format, source, data changes, data requests and errors");

    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    RTEXITCODE rcExit = shclRegisterListener(ptrSession, aEventTypes, ptrEventSource, ptrListener);
    HRESULT hrc;
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    bool fNeedWait = false;
    rcExit = shclHandlePasteTryCurrent(ptrSession, pszFormat, &fNeedWait, NULL /* pfGuestOffer */);
    if (rcExit != RTEXITCODE_SUCCESS || !fNeedWait)
    {
        shclCleanupListener(ptrEventSource, ptrListener, false /* fSignalHandlerInstalled */);
        return rcExit;
    }

    bool fHostClipboardReported = false;
    bool fInitialReported = false;
    rcExit = shclHandlePasteReportGuestFormats(ptrSession, ptrHostClipboard, pszFormat, &fInitialReported);
    if (rcExit != RTEXITCODE_SUCCESS)
    {
        shclCleanupListener(ptrEventSource, ptrListener, false /* fSignalHandlerInstalled */);
        return rcExit;
    }
    fHostClipboardReported = fInitialReported;

    shclSignalHandlerInstall();

    uint64_t const msStart = RTTimeMilliTS();
    bool fTimedOut = false;
    bool fDone = false;
    shclVerbose("paste: waiting for matching guest data; format=%s timeout=%s",
                     pszFormat ? pszFormat : "any",
                     cMsWait == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsWait).c_str());
    if (cMsWait == RT_INDEFINITE_WAIT)
        shclInfo(Clipboard::tr("Waiting for guest clipboard data..."));
    else
        shclInfo(Clipboard::tr("Waiting up to %RU32 ms for guest clipboard data..."), cMsWait);
    for (;;)
    {
        if (shclSignalWasCaught())
            break;

        uint64_t const msNow = RTTimeMilliTS();
        uint32_t cMsThisWait = 1000;
        if (cMsWait != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = msNow - msStart;
            if (cMsElapsed >= cMsWait)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)cMsThisWait, cMsWait - cMsElapsed);
        }

        shclVerbose("paste: waiting up to %RU32 ms for an event", cMsThisWait);
        ComPtr<IEvent> ptrEvent;
        hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
        {
            shclVerbose("paste: no event received within %RU32 ms; retrying current guest data read", cMsThisWait);
            bool fEventNeedWait = false;
            rcExit = shclHandlePasteTryCurrent(ptrSession, pszFormat, &fEventNeedWait, NULL /* pfGuestOffer */);
            if (rcExit != RTEXITCODE_SUCCESS || !fEventNeedWait)
            {
                fDone = true;
                break;
            }
            continue;
        }
        if (FAILED(hrc))
        {
            shclCleanupHostClipboardListener(ptrHostClipboard, fHostClipboardReported, ptrEventSource, ptrListener,
                                             true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard events failed: %Rhrc"), hrc);
        }
        if (ptrEvent.isNull())
        {
            shclVerbose("paste: no event received within %RU32 ms; retrying current guest data read", cMsThisWait);
            bool fEventNeedWait = false;
            rcExit = shclHandlePasteTryCurrent(ptrSession, pszFormat, &fEventNeedWait, NULL /* pfGuestOffer */);
            if (rcExit != RTEXITCODE_SUCCESS || !fEventNeedWait)
            {
                fDone = true;
                break;
            }
            continue;
        }

        VBoxEventType_T enmType = VBoxEventType_Invalid;
        hrc = ptrEvent->COMGETTER(Type)(&enmType);
        if (FAILED(hrc))
            shclVerbose("paste: failed to get event type: %Rhrc", hrc);
        else
            shclVerboseClipboardEvent("paste", "received event", enmType, ptrEvent);
        if (SUCCEEDED(hrc) && enmType == VBoxEventType_OnClipboardError)
        {
            shclHandlePasteLogErrorEvent(ptrEvent);
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            continue;
        }
        if (SUCCEEDED(hrc) && enmType == VBoxEventType_OnClipboardDataRequested)
        {
            bool fEventNeedWait = true;
            rcExit = shclHandlePasteDataRequested(ptrSession, ptrHostClipboard, ptrEvent, pszFormat, &fEventNeedWait);
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            if (rcExit != RTEXITCODE_SUCCESS || !fEventNeedWait)
            {
                fDone = true;
                break;
            }
            continue;
        }

        ComPtr<IClipboardItem> ptrItem;
        if (shclHandlePasteEventMatchesWait(ptrEvent, pszFormat, ptrItem))
        {
            shclVerbose("paste: event carries matching guest clipboard data");
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            rcExit = shclHandlePasteOutputItem(ptrItem, pszFormat);
            fDone = true;
            break;
        }

        if (shclHandlePasteEventMayNeedRead(ptrEvent, pszFormat))
        {
            bool fEventNeedWait = false;
            rcExit = shclHandlePasteTryCurrent(ptrSession, pszFormat, &fEventNeedWait, NULL /* pfGuestOffer */);
            if (rcExit != RTEXITCODE_SUCCESS || !fEventNeedWait)
            {
                shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
                fDone = true;
                break;
            }

            bool fReported = false;
            rcExit = shclHandlePasteReportGuestFormats(ptrSession, ptrHostClipboard, pszFormat, &fReported);
            if (rcExit != RTEXITCODE_SUCCESS)
            {
                shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
                shclCleanupHostClipboardListener(ptrHostClipboard, fHostClipboardReported, ptrEventSource, ptrListener,
                                                 true /* fSignalHandlerInstalled */);
                return rcExit;
            }
            fHostClipboardReported = fHostClipboardReported || fReported;
        }
        else
            shclVerbose("paste: event does not indicate matching guest data");

        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupHostClipboardListener(ptrHostClipboard, fHostClipboardReported, ptrEventSource, ptrListener,
                                     true /* fSignalHandlerInstalled */);
    if (fInterrupted)
    {
        shclVerbose("paste: interrupted; shutting down");
        return RTEXITCODE_SUCCESS;
    }
    if (fDone)
    {
        if (rcExit == RTEXITCODE_SUCCESS)
            shclInfo(Clipboard::tr("Received guest clipboard data."));
        return rcExit;
    }
    if (fTimedOut)
    {
        shclInfo(Clipboard::tr("Timed out waiting for guest clipboard data."));
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Timed out after %RU32 ms waiting for new guest clipboard data%s%s."),
                              cMsWait, pszFormat ? " in format " : "", pszFormat ? pszFormat : "");
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Handles the clipboard paste subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandlePaste(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--format",  CLIPBOARD_OPT_FORMAT,  RTGETOPT_REQ_STRING },
        { "--timeout", CLIPBOARD_OPT_TIMEOUT, RTGETOPT_REQ_UINT32 },
        { "--wait",    CLIPBOARD_OPT_WAIT,    RTGETOPT_REQ_UINT32 },
        { "--verbose", 'v',                   RTGETOPT_REQ_NOTHING }
    };

    const char *pszFormat = NULL;
    uint32_t cMsTimeout = CLIPBOARD_PASTE_DEFAULT_TIMEOUT;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_FORMAT:
                pszFormat = ValueUnion.psz;
                break;
            case CLIPBOARD_OPT_TIMEOUT:
            case CLIPBOARD_OPT_WAIT:
                cMsTimeout = ValueUnion.u32;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(Clipboard::tr("The clipboard paste command writes data to standard output; use 'clipboard <vm> transfer receive' for file transfers."));
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    if (pszFormat && !*pszFormat)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard MIME type must not be empty."));

    shclVerbose("paste: target='%s' format=%s timeout=%s", argv[0], pszFormat ? pszFormat : "any",
                     cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    shclVerbose("paste: live clipboard object acquired");

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            true /* fExcludeOwnChanges */,
                            true /* fExcludeReflections */,
                            true /* fIncludeInitialState */,
                            true /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    }
    shclVerbose("paste: clipboard session acquired");

    RTEXITCODE rcExit;
    if (cMsTimeout == 0)
    {
        bool fNeedWait = false;
        rcExit = shclHandlePasteTryCurrent(ptrClipboardSession, pszFormat, &fNeedWait, NULL /* pfGuestOffer */);
        if (rcExit == RTEXITCODE_SUCCESS && fNeedWait)
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("No matching guest clipboard data is currently available."));
    }
    else
    {
        ComPtr<IHostClipboard> ptrHostClipboard;
        hrc = ptrClipboardSession->COMGETTER(HostClipboard)(ptrHostClipboard.asOutParam());
        if (FAILED(hrc))
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard session host endpoint failed: %Rhrc"), hrc);
        else if (ptrHostClipboard.isNull())
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Machine '%s' has no host clipboard endpoint."), argv[0]);
        else
        {
            shclVerbose("paste: session host clipboard endpoint acquired");
            rcExit = shclHandlePasteWaitAndOutput(ptrClipboardSession, ptrHostClipboard, pszFormat, cMsTimeout);
        }
    }

    pArg->session->UnlockMachine();
    return rcExit;
}


/**
 * Handles the clipboard formats subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleFormats(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--machinereadable", CLIPBOARD_OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING }
    };

    bool fMachineReadable = false;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_MACHINE_READABLE:
                fMachineReadable = true;
                break;
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            false /* fExcludeOwnChanges */,
                            false /* fExcludeReflections */,
                            false /* fIncludeInitialState */,
                            false /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
    {
        pArg->session->UnlockMachine();
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    }

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    SafeIfaceArray<IClipboardFormat> aFormats;
    CHECK_ERROR2(hrc, ptrClipboardSession, ReadFormats(ComSafeArrayAsOutParam(aFormats)));
    if (FAILED(hrc))
        rcExit = RTEXITCODE_FAILURE;
    else
    {
        for (size_t i = 0; i < aFormats.size(); i++)
        {
            Bstr bstrMimeType;
            hrc = aFormats[i]->COMGETTER(MimeType)(bstrMimeType.asOutParam());
            if (FAILED(hrc))
            {
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to get clipboard format: %Rhrc"), hrc);
                break;
            }
            Utf8Str strMimeType(bstrMimeType);
            if (fMachineReadable)
                RTPrintf("format=\"%s\"\n", strMimeType.c_str());
            else
                RTPrintf("%s\n", strMimeType.c_str());
        }
    }

    pArg->session->UnlockMachine();
    return rcExit;
}


/**
 * Handles the clipboard reset subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleReset(HandlerArg *pArg, int argc, char **argv)
{
    RT_NOREF(argc, argv);

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    CHECK_ERROR2_RET(hrc, ptrClipboard, Reset(), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}




/**
 * Handles a data-changed event for the serve command.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to forward host data through.
 * @param   ptrHostClipboard Host clipboard endpoint to publish guest data to.
 * @param   ptrEvent        Data-changed event to handle.
 * @param   pState          Serve command state.
 */
static RTEXITCODE shclHandleServeDataChanged(const ComPtr<IClipboardSession> &ptrSession,
                                             const ComPtr<IHostClipboard> &ptrHostClipboard,
                                             const ComPtr<IEvent> &ptrEvent,
                                             SHCLHANDLESERVESTATE *pState)
{
    ComPtr<IClipboardDataChangedEvent> ptrDataEvent = ptrEvent;
    if (ptrDataEvent.isNull())
        return RTEXITCODE_SUCCESS;

    ComPtr<IClipboardItem> ptrItem;
    HRESULT hrc = ptrDataEvent->COMGETTER(Item)(ptrItem.asOutParam());
    if (FAILED(hrc) || ptrItem.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to inspect clipboard data event: %Rhrc"), hrc);

    ULONG uClipboardId = 0;
    hrc = ptrItem->COMGETTER(Id)(&uClipboardId);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard data event ID: %Rhrc"), hrc);
    if (pState && (!pState->fHaveClipboardId || pState->uClipboardId != uClipboardId))
    {
        pState->fHaveClipboardId = true;
        pState->uClipboardId = uClipboardId;
        RTStrmPrintf(g_pStdErr, "clipboard: serve: new clipboard id=%RU32\n", (uint32_t)uClipboardId);
        RTStrmFlush(g_pStdErr);
    }

    Utf8Str strMimeType;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    SafeArray<BYTE> aBuffer;
    hrc = shclGetItemData(ptrItem, strMimeType, &enmSource, aBuffer);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard data event payload: %Rhrc"), hrc);

    shclVerbose("serve: data event id=%RU32 source=%s format=%s size=%zu", (uint32_t)uClipboardId,
                     ShClHlpSourceToString(enmSource), strMimeType.c_str(), aBuffer.size());
    if (shclMimePriority(strMimeType) == UINT32_MAX)
    {
        shclVerbose("serve: data event id=%RU32 ignoring unsupported format: %s", (uint32_t)uClipboardId, strMimeType.c_str());
        return RTEXITCODE_SUCCESS;
    }
    if (!aBuffer.size())
    {
        shclVerbose("serve: data event id=%RU32 ignoring empty data event", (uint32_t)uClipboardId);
        return RTEXITCODE_SUCCESS;
    }

    Bstr bstrMimeType(strMimeType);
    if (enmSource == ClipboardSource_Host)
    {
        ClipboardSource_T enmWrittenSource = ClipboardSource_Custom;
        Bstr bstrWrittenMimeType;
        SafeArray<BYTE> aWrittenBuffer;
        hrc = ptrSession->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host, bstrMimeType.raw(),
                                       ComSafeArrayAsInParam(aBuffer), &enmWrittenSource,
                                       bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
        if (FAILED(hrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  Clipboard::tr("Forwarding host clipboard data to the guest failed: %Rhrc"), hrc);
        shclVerbose("serve: data event id=%RU32 forwarded host data to guest clipboard: %s size=%zu",
                    (uint32_t)uClipboardId, strMimeType.c_str(), aBuffer.size());
        shclVerbosePayloadData("serve", Utf8StrFmt("data event id=%RU32 forwarded host data", (uint32_t)uClipboardId).c_str(),
                               strMimeType, aBuffer.raw(), aBuffer.size());
        return RTEXITCODE_SUCCESS;
    }

    if (enmSource != ClipboardSource_Guest)
    {
        shclVerbose("serve: data event id=%RU32 observed non-host/non-guest data event: source=%s format=%s size=%zu",
                     (uint32_t)uClipboardId, ShClHlpSourceToString(enmSource), strMimeType.c_str(), aBuffer.size());
        return RTEXITCODE_SUCCESS;
    }

    hrc = ptrHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest, bstrMimeType.raw(),
                                    ComSafeArrayAsInParam(aBuffer));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Publishing guest clipboard data to the host failed: %Rhrc"), hrc);

    shclVerbose("serve: data event id=%RU32 published guest data to host clipboard: %s size=%zu",
                (uint32_t)uClipboardId, strMimeType.c_str(), aBuffer.size());
    shclVerbosePayloadData("serve", Utf8StrFmt("data event id=%RU32 published guest data", (uint32_t)uClipboardId).c_str(),
                           strMimeType, aBuffer.raw(), aBuffer.size());
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a guest format-changed event for the serve command.
 *
 * @returns Process exit code.
 * @param   ptrHostClipboard Host clipboard endpoint to publish to.
 * @param   ptrEvent        Format-changed event to handle.
 */
static RTEXITCODE shclHandleServeFormatChanged(const ComPtr<IHostClipboard> &ptrHostClipboard,
                                               const ComPtr<IEvent> &ptrEvent)
{
    ComPtr<IClipboardFormatChangedEvent> ptrFormatEvent = ptrEvent;
    if (ptrFormatEvent.isNull())
        return RTEXITCODE_SUCCESS;

    ClipboardSource_T enmSource = ClipboardSource_Custom;
    SafeIfaceArray<IClipboardFormat> aFormats;
    HRESULT hrc = ptrFormatEvent->COMGETTER(ClipboardSource)(&enmSource);
    if (SUCCEEDED(hrc))
        hrc = ptrFormatEvent->COMGETTER(Formats)(ComSafeArrayAsOutParam(aFormats));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to inspect clipboard format event: %Rhrc"), hrc);

    shclVerbose("serve: format event source=%s count=%zu", ShClHlpSourceToString(enmSource), aFormats.size());

    std::vector<ComPtr<IClipboardFormat> > vecReportFormats;
    Utf8Str strMimeType;
    hrc = shclFilterSupportedFormats(aFormats, &vecReportFormats, NULL /* pvecReportMimeTypes */, &strMimeType);
    if (hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED)
    {
        shclVerbose("serve: format event source=%s has no supported formats", ShClHlpSourceToString(enmSource));
        return RTEXITCODE_SUCCESS;
    }
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard format metadata: %Rhrc"), hrc);

    if (enmSource == ClipboardSource_Host)
    {
        shclVerbose("serve: format event observed host format for guest clipboard: %s", strMimeType.c_str());
        return RTEXITCODE_SUCCESS;
    }

    if (enmSource != ClipboardSource_Guest)
    {
        shclVerbose("serve: format event observed non-host/non-guest format event: source=%s format=%s",
                         ShClHlpSourceToString(enmSource), strMimeType.c_str());
        return RTEXITCODE_SUCCESS;
    }

    SafeIfaceArray<IClipboardFormat> aReportFormats(vecReportFormats);
    hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest, ComSafeArrayAsInParam(aReportFormats));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Reporting guest clipboard formats to the host failed: %Rhrc"), hrc);

    shclVerbose("serve: format event reported %zu guest formats to host clipboard; preferred=%s",
                aReportFormats.size(), strMimeType.c_str());
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a guest source-changed event for the serve command.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to read current formats from.
 * @param   ptrHostClipboard Host clipboard endpoint to publish to.
 * @param   ptrEvent        Source-changed event to handle.
 */
static RTEXITCODE shclHandleServeSourceChanged(const ComPtr<IClipboardSession> &ptrSession,
                                               const ComPtr<IHostClipboard> &ptrHostClipboard,
                                               const ComPtr<IEvent> &ptrEvent)
{
    ComPtr<IClipboardSourceChangedEvent> ptrSourceEvent = ptrEvent;
    if (ptrSourceEvent.isNull())
        return RTEXITCODE_SUCCESS;

    ClipboardSource_T enmSource = ClipboardSource_Custom;
    HRESULT hrc = ptrSourceEvent->COMGETTER(ClipboardSource)(&enmSource);
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to inspect clipboard source event: %Rhrc"), hrc);

    shclVerbose("serve: source event source=%s", ShClHlpSourceToString(enmSource));
    if (enmSource != ClipboardSource_Guest)
        return RTEXITCODE_SUCCESS;

    SafeIfaceArray<IClipboardFormat> aFormats;
    hrc = ptrSession->ReadFormats(ComSafeArrayAsOutParam(aFormats));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Reading guest clipboard formats failed: %Rhrc"), hrc);

    std::vector<ComPtr<IClipboardFormat> > vecReportFormats;
    Utf8Str strMimeType;
    hrc = shclFilterSupportedFormats(aFormats, &vecReportFormats, NULL /* pvecReportMimeTypes */, &strMimeType);
    if (hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED)
    {
        shclVerbose("serve: source event has no supported guest formats");
        return RTEXITCODE_SUCCESS;
    }
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Failed to read clipboard format metadata: %Rhrc"), hrc);

    SafeIfaceArray<IClipboardFormat> aReportFormats(vecReportFormats);
    hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest, ComSafeArrayAsInParam(aReportFormats));
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Reporting guest clipboard formats to the host failed: %Rhrc"), hrc);

    shclVerbose("serve: source event reported %zu guest formats to host clipboard; preferred=%s",
                aReportFormats.size(), strMimeType.c_str());
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles a native host data request for the serve command.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to read guest data from.
 * @param   ptrHostClipboard Host clipboard endpoint to answer through.
 * @param   ptrEvent        Data-requested event to handle.
 */
static RTEXITCODE shclHandleServeDataRequested(const ComPtr<IClipboardSession> &ptrSession,
                                               const ComPtr<IHostClipboard> &ptrHostClipboard,
                                               const ComPtr<IEvent> &ptrEvent)
{
    ULONG uRequestId = 0;
    ClipboardAction_T enmAction = ClipboardAction_Copy;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    Utf8Str strRequestedMimeType;
    if (!shclGetDataRequestedEvent(ptrEvent, &uRequestId, &enmAction, &enmSource, strRequestedMimeType))
    {
        shclVerbose("serve: ignoring malformed data-request event");
        return RTEXITCODE_SUCCESS;
    }

    shclVerbose("serve: request id=%RU32 action=%s source=%s format=%s", (uint32_t)uRequestId,
                     shclActionToString(enmAction), ShClHlpSourceToString(enmSource), strRequestedMimeType.c_str());
    if (   enmAction != ClipboardAction_Copy
        || enmSource != ClipboardSource_Guest)
    {
        shclVerbose("serve: request id=%RU32 ignoring non-guest/non-copy data request", (uint32_t)uRequestId);
        return RTEXITCODE_SUCCESS;
    }

    ClipboardSource_T enmReadSource = ClipboardSource_Custom;
    Bstr bstrRequestedMimeType(strRequestedMimeType);
    Bstr bstrReadMimeType;
    SafeArray<BYTE> aBuffer;
    HRESULT hrc = ptrSession->ReadDataRaw(enmAction, bstrRequestedMimeType.raw(), &enmReadSource,
                                          bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aBuffer));
    if (FAILED(hrc))
    {
        RTMsgWarning(Clipboard::tr("Reading guest clipboard data for host request %RU32 failed: %Rhrc"),
                     (uint32_t)uRequestId, hrc);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strReadMimeType(bstrReadMimeType);
    shclVerbose("serve: request id=%RU32 read data source=%s format=%s size=%zu",
                     (uint32_t)uRequestId, ShClHlpSourceToString(enmReadSource), strReadMimeType.c_str(), aBuffer.size());
    if (enmReadSource != ClipboardSource_Guest)
    {
        RTMsgWarning(Clipboard::tr("Ignoring host clipboard request %RU32 because current data is not guest-owned."),
                     (uint32_t)uRequestId);
        return RTEXITCODE_SUCCESS;
    }
    if (!shclMimeEquivalent(strRequestedMimeType, strReadMimeType))
    {
        RTMsgWarning(Clipboard::tr("Ignoring host clipboard request %RU32 for '%s'; readable guest data is '%s'."),
                     (uint32_t)uRequestId, strRequestedMimeType.c_str(), strReadMimeType.c_str());
        return RTEXITCODE_SUCCESS;
    }
    if (!aBuffer.size())
    {
        RTMsgWarning(Clipboard::tr("Ignoring host clipboard request %RU32 because guest data is empty."),
                     (uint32_t)uRequestId);
        return RTEXITCODE_SUCCESS;
    }

    hrc = ptrHostClipboard->ProvideData(uRequestId, enmAction, ClipboardSource_Guest, bstrReadMimeType.raw(),
                                        ComSafeArrayAsInParam(aBuffer));
    if (FAILED(hrc))
    {
        RTMsgWarning(Clipboard::tr("Providing guest clipboard data for host request %RU32 failed: %Rhrc"),
                     (uint32_t)uRequestId, hrc);
        return RTEXITCODE_SUCCESS;
    }

    shclVerbose("serve: request id=%RU32 provided data format=%s size=%zu", (uint32_t)uRequestId,
                     strReadMimeType.c_str(), aBuffer.size());
    shclVerbosePayloadData("serve", Utf8StrFmt("request id=%RU32 provided guest data", (uint32_t)uRequestId).c_str(),
                           strReadMimeType, aBuffer.raw(), aBuffer.size());
    return RTEXITCODE_SUCCESS;
}


/**
 * Clears the native host clipboard endpoint.
 *
 * @param   ptrHostClipboard    Host clipboard endpoint to clear.
 */
static void shclClearHostClipboard(const ComPtr<IHostClipboard> &ptrHostClipboard)
{
    shclVerbose("clearing host clipboard");

    HRESULT hrc = ptrHostClipboard->Clear();
    if (FAILED(hrc))
        RTMsgWarning(Clipboard::tr("Clearing host clipboard failed: %Rhrc"), hrc);
    else
        shclVerbose("host clipboard cleared");
}


/**
 * Serves guest clipboard data to the native host clipboard until interrupted or timed out.
 *
 * @returns Process exit code.
 * @param   ptrSession      Clipboard session to serve.
 * @param   ptrHostClipboard Host clipboard endpoint to publish through.
 * @param   cMsTimeout      Maximum serve lifetime in milliseconds.
 */
static RTEXITCODE shclHandleServeLoop(const ComPtr<IClipboardSession> &ptrSession,
                                      const ComPtr<IHostClipboard> &ptrHostClipboard,
                                      uint32_t cMsTimeout)
{
    SafeArray<VBoxEventType_T> aEventTypes;
    aEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardDataChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
    aEventTypes.push_back(VBoxEventType_OnClipboardError);
    aEventTypes.push_back(VBoxEventType_OnClipboardSourceChanged);
    aEventTypes.push_back(VBoxEventType_OnClipboardModeChanged);

    shclVerbose("serve: registering listener for guest clipboard publication");

    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    RTEXITCODE rcExit = shclRegisterListener(ptrSession, aEventTypes, ptrEventSource, ptrListener);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    shclSignalHandlerInstall();

    SHCLHANDLESERVESTATE State;
    uint64_t const msStart = RTTimeMilliTS();
    bool fTimedOut = false;
    shclVerbose("serve: running; timeout=%s",
                     cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());
    for (;;)
    {
        if (shclSignalWasCaught())
            break;

        uint32_t cMsThisWait = 1000;
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            if (cMsElapsed >= cMsTimeout)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)1000, cMsTimeout - cMsElapsed);
        }

        ComPtr<IEvent> ptrEvent;
        HRESULT hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
            continue;
        if (FAILED(hrc))
        {
            shclCleanupHostClipboardListener(ptrHostClipboard, true /* fClearHostClipboard */, ptrEventSource, ptrListener,
                                             true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard serve events failed: %Rhrc"), hrc);
        }
        if (ptrEvent.isNull())
            continue;

        VBoxEventType_T enmType = VBoxEventType_Invalid;
        hrc = ptrEvent->COMGETTER(Type)(&enmType);
        if (FAILED(hrc))
        {
            shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
            shclVerbose("serve: failed to get event type: %Rhrc", hrc);
            continue;
        }

        shclVerboseClipboardEvent("serve", "received event", enmType, ptrEvent);
        switch (enmType)
        {
            case VBoxEventType_OnClipboardFormatChanged:
                rcExit = shclHandleServeFormatChanged(ptrHostClipboard, ptrEvent);
                break;
            case VBoxEventType_OnClipboardDataChanged:
                rcExit = shclHandleServeDataChanged(ptrSession, ptrHostClipboard, ptrEvent, &State);
                break;
            case VBoxEventType_OnClipboardDataRequested:
                rcExit = shclHandleServeDataRequested(ptrSession, ptrHostClipboard, ptrEvent);
                break;
            case VBoxEventType_OnClipboardSourceChanged:
                rcExit = shclHandleServeSourceChanged(ptrSession, ptrHostClipboard, ptrEvent);
                break;
            case VBoxEventType_OnClipboardError:
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Clipboard error while serving host clipboard."));
                break;
            default:
                shclVerbose("serve: event '%s' is diagnostic only", shclEventTypeToString(enmType));
                break;
        }
        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupHostClipboardListener(ptrHostClipboard, true /* fClearHostClipboard */, ptrEventSource, ptrListener,
                                     true /* fSignalHandlerInstalled */);
    if (fInterrupted)
        shclVerbose("serve: interrupted; shutting down");
    else if (fTimedOut)
        shclVerbose("serve: timed out after %RU32 ms", cMsTimeout);
    return rcExit;
}


/**
 * Handles the clipboard serve subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleServe(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--timeout", CLIPBOARD_OPT_TIMEOUT, RTGETOPT_REQ_UINT32 },
        { "--verbose", 'v',                   RTGETOPT_REQ_NOTHING }
    };

    uint32_t cMsTimeout = RT_INDEFINITE_WAIT;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    shclVerbose("serve: target='%s' timeout=%s", argv[0],
                     cMsTimeout == RT_INDEFINITE_WAIT ? "infinite" : Utf8StrFmt("%RU32 ms", cMsTimeout).c_str());

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    shclVerbose("serve: live clipboard object acquired");

    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            true /* fExcludeOwnChanges */,
                            true /* fExcludeReflections */,
                            true /* fIncludeInitialState */,
                            true /* fIncludePayload */,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);
    shclVerbose("serve: clipboard session acquired");

    ComPtr<IHostClipboard> ptrHostClipboard;
    hrc = ptrClipboardSession->COMGETTER(HostClipboard)(ptrHostClipboard.asOutParam());
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Getting clipboard session host endpoint failed: %Rhrc"), hrc);
    if (ptrHostClipboard.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Machine '%s' has no host clipboard endpoint."), argv[0]);
    shclVerbose("serve: session host clipboard endpoint acquired");

    return shclHandleServeLoop(ptrClipboardSession, ptrHostClipboard, cMsTimeout);
}


/**
 * Parses the listen subcommand event filter list.
 *
 * @returns true if the event list was parsed successfully, false otherwise.
 * @param   pszEvents       Comma-separated event list, optional.
 * @param   aEventTypes     Where to return the selected event types.
 */
static bool shclHandleListenParseEventList(const char *pszEvents, SafeArray<VBoxEventType_T> &aEventTypes)
{
    if (!pszEvents || !*pszEvents)
    {
        aEventTypes.push_back(VBoxEventType_OnClipboardSourceChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardDataChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
        aEventTypes.push_back(VBoxEventType_OnClipboardTransfer);
        aEventTypes.push_back(VBoxEventType_OnClipboardError);
        aEventTypes.push_back(VBoxEventType_OnClipboardModeChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardFileTransferModeChanged);
        return true;
    }

    char **papszEvents = NULL;
    size_t cEvents = 0;
    int vrc = RTStrSplit(pszEvents, strlen(pszEvents) + 1, ",", &papszEvents, &cEvents);
    if (RT_FAILURE(vrc))
        return false;

    bool fSuccess = true;
    for (size_t i = 0; i < cEvents; i++)
    {
        const char *psz = papszEvents[i];
        if (   !RTStrICmp(psz, "source")
            || !RTStrICmp(psz, "source-changed"))
            aEventTypes.push_back(VBoxEventType_OnClipboardSourceChanged);
        else if (   !RTStrICmp(psz, "format")
                 || !RTStrICmp(psz, "format-changed"))
            aEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
        else if (   !RTStrICmp(psz, "data")
                 || !RTStrICmp(psz, "data-changed"))
            aEventTypes.push_back(VBoxEventType_OnClipboardDataChanged);
        else if (   !RTStrICmp(psz, "data-requested")
                 || !RTStrICmp(psz, "request"))
            aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
        else if (!RTStrICmp(psz, "transfer"))
            aEventTypes.push_back(VBoxEventType_OnClipboardTransfer);
        else if (!RTStrICmp(psz, "error"))
            aEventTypes.push_back(VBoxEventType_OnClipboardError);
        else if (!RTStrICmp(psz, "mode"))
            aEventTypes.push_back(VBoxEventType_OnClipboardModeChanged);
        else if (!RTStrICmp(psz, "file-transfer-mode"))
            aEventTypes.push_back(VBoxEventType_OnClipboardFileTransferModeChanged);
        else
        {
            fSuccess = false;
            break;
        }
    }

    for (size_t i = 0; i < cEvents; i++)
        RTStrFree(papszEvents[i]);
    RTMemFree(papszEvents);
    return fSuccess && aEventTypes.size() > 0;
}


/**
 * Handles the clipboard listen subcommand.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 * @param   argc            Number of command arguments.
 * @param   argv            Command argument vector.
 */
static RTEXITCODE shclHandleListen(HandlerArg *pArg, int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--events",          CLIPBOARD_OPT_EVENTS,           RTGETOPT_REQ_STRING },
        { "--format",          CLIPBOARD_OPT_OUTPUT_FORMAT,    RTGETOPT_REQ_STRING },
        { "--timeout",         CLIPBOARD_OPT_TIMEOUT,          RTGETOPT_REQ_UINT32 },
        { "--count",           CLIPBOARD_OPT_COUNT,            RTGETOPT_REQ_UINT32 },
        { "--machinereadable", CLIPBOARD_OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING },
        { "--verbose",         'v',                            RTGETOPT_REQ_NOTHING }
    };

    const char *pszEvents = NULL;
    CLIPBOARDLISTENFMT enmOutputFormat = CLIPBOARDLISTENFMT_HUMAN;
    uint32_t cMsTimeout = RT_INDEFINITE_WAIT;
    uint32_t cEventsMax = UINT32_MAX;
    g_uVerbosity = 0;

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 2, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case CLIPBOARD_OPT_EVENTS:
                pszEvents = ValueUnion.psz;
                break;
            case CLIPBOARD_OPT_OUTPUT_FORMAT:
                if (!RTStrICmp(ValueUnion.psz, "human"))
                    enmOutputFormat = CLIPBOARDLISTENFMT_HUMAN;
                else if (!RTStrICmp(ValueUnion.psz, "json"))
                    enmOutputFormat = CLIPBOARDLISTENFMT_JSON;
                else if (!RTStrICmp(ValueUnion.psz, "machinereadable"))
                    enmOutputFormat = CLIPBOARDLISTENFMT_MACHINE_READABLE;
                else
                    return errorArgument(Clipboard::tr("Invalid --format argument '%s'."), ValueUnion.psz);
                break;
            case CLIPBOARD_OPT_TIMEOUT:
                cMsTimeout = ValueUnion.u32;
                break;
            case CLIPBOARD_OPT_COUNT:
                cEventsMax = ValueUnion.u32 ? ValueUnion.u32 : UINT32_MAX;
                break;
            case CLIPBOARD_OPT_MACHINE_READABLE:
                enmOutputFormat = CLIPBOARDLISTENFMT_MACHINE_READABLE;
                break;
            case 'v':
                g_uVerbosity++;
                break;
            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    SafeArray<VBoxEventType_T> aEventTypes;
    if (!shclHandleListenParseEventList(pszEvents, aEventTypes))
        return errorArgument(Clipboard::tr("Invalid --events argument '%s'."), pszEvents ? pszEvents : "");

    ComPtr<IClipboard> ptrClipboard;
    HRESULT hrc = shclGet(pArg, argv[0], ptrClipboard);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    bool const fIncludePayload = g_uVerbosity > 1;
    ComPtr<IClipboardSession> ptrClipboardSession;
    hrc = shclCreateSession(ptrClipboard,
                            false /* fExcludeOwnChanges */,
                            false /* fExcludeReflections */,
                            false /* fIncludeInitialState */,
                            fIncludePayload,
                            ptrClipboardSession);
    if (FAILED(hrc) || ptrClipboardSession.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Creating clipboard session failed: %Rhrc"), hrc);

    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    RTEXITCODE rcExit = shclRegisterListener(ptrClipboardSession, aEventTypes, ptrEventSource, ptrListener);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    hrc = shclGetMode(pArg, &enmMode);
    if (SUCCEEDED(hrc) && enmMode == ClipboardMode_Disabled)
    {
        shclHandleListenPrintDisabledWarning(enmOutputFormat);
        RTStrmFlush(g_pStdOut);
    }
    else if (FAILED(hrc))
        shclVerbose("getting the Shared Clipboard mode failed: %Rhrc", hrc);

    shclSignalHandlerInstall();

    uint64_t const msStart = RTTimeMilliTS();
    uint32_t cEvents = 0;
    bool fTimedOut = false;
    SHCLHANDLELISTENSTATE ListenState;
    while (cEvents < cEventsMax)
    {
        if (shclSignalWasCaught())
            break;

        uint32_t cMsThisWait = 1000;
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            if (cMsElapsed >= cMsTimeout)
            {
                fTimedOut = true;
                break;
            }
            cMsThisWait = (uint32_t)RT_MIN((uint64_t)1000, cMsTimeout - cMsElapsed);
        }

        ComPtr<IEvent> ptrEvent;
        hrc = ptrEventSource->GetEvent(ptrListener, (LONG)cMsThisWait, ptrEvent.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
            continue;
        if (FAILED(hrc))
        {
            shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Clipboard::tr("Waiting for clipboard events failed: %Rhrc"), hrc);
        }
        if (ptrEvent.isNull())
            continue;

        shclMarkEventProcessed(ptrEventSource, ptrListener, ptrEvent);
        shclHandleListenPrintEvent(enmOutputFormat, ptrEvent, ptrClipboardSession, &ListenState);
        RTStrmFlush(g_pStdOut);
        cEvents++;
    }

    bool const fInterrupted = shclSignalWasCaught();
    shclCleanupListener(ptrEventSource, ptrListener, true /* fSignalHandlerInstalled */);
    if (fInterrupted)
        shclVerbose("interrupted; shutting down");
    else if (fTimedOut)
        shclVerbose("timed out after %RU32 ms", cMsTimeout);
    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the VBoxManage clipboard command.
 *
 * @returns Process exit code.
 * @param   pArg            Command handler arguments.
 */
RTEXITCODE handleClipboard(HandlerArg *pArg)
{
    AssertPtr(pArg);

    if (pArg->argc < 2)
        return errorNoSubcommand();

    const char *pszSubcommand = pArg->argv[1];
    if (!strcmp(pszSubcommand, "set-mode"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_SET_MODE);
        return shclHandleSetMode(pArg, pArg->argc, pArg->argv);
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (!strcmp(pszSubcommand, "set-filetransfers"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_SET_FILETRANSFERS);
        return shclHandleSetFileTransfers(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "transfer"))
    {
        setCurrentSubcommand(   HELP_SCOPE_CLIPBOARD_TRANSFER_OFFER
                             | HELP_SCOPE_CLIPBOARD_TRANSFER_RECEIVE
                             | HELP_SCOPE_CLIPBOARD_TRANSFER_LIST
                             | HELP_SCOPE_CLIPBOARD_TRANSFER_CANCEL);
        return shclHandleTransfer(pArg, pArg->argc, pArg->argv);
    }
#else
    if (   !strcmp(pszSubcommand, "set-filetransfers")
        || !strcmp(pszSubcommand, "transfer"))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              Clipboard::tr("Clipboard transfer commands are not implemented on this platform."));
#endif
    if (!strcmp(pszSubcommand, "copy"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_COPY);
        return shclHandleCopy(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "paste"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_PASTE);
        return shclHandlePaste(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "formats"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_FORMATS);
        return shclHandleFormats(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "reset"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_RESET);
        return shclHandleReset(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "serve"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_SERVE);
        return shclHandleServe(pArg, pArg->argc, pArg->argv);
    }
    if (!strcmp(pszSubcommand, "listen"))
    {
        setCurrentSubcommand(HELP_SCOPE_CLIPBOARD_LISTEN);
        return shclHandleListen(pArg, pArg->argc, pArg->argv);
    }

    return errorUnknownSubcommand(pszSubcommand);
}
