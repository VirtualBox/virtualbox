/* $Id: VBoxHeadless.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxHeadless - The VirtualBox Headless frontend for running VMs on servers.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/NativeEventQueue.h>

#include <VBox/com/VirtualBox.h>
#include <VBox/com/listeners.h>

using namespace com;

#define LOG_GROUP LOG_GROUP_GUI

#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/semaphore.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/ldr.h>
#include <iprt/getopt.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>
#include <VBoxVideo.h>

#ifdef VBOX_WITH_RECORDING
# include <cstdlib>
# include <iprt/process.h>
#endif

#if defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
# include <cerrno>
#endif

#ifdef RT_OS_DARWIN
# include <iprt/asm.h>
# include <dlfcn.h>
# include <sys/mman.h>
#endif

#if !defined(RT_OS_WINDOWS)
# include <signal.h>
# include <unistd.h>
# include <sys/uio.h>
#endif

#include "PasswordInput.h"

////////////////////////////////////////////////////////////////////////////////

#define LogError(m,rc) \
    do { \
        Log(("VBoxHeadless: ERROR: " m " [rc=0x%08X]\n", rc)); \
        RTPrintf("%s\n", m); \
    } while (0)

////////////////////////////////////////////////////////////////////////////////

/* global weak references (for event handlers) */
static IConsole *gConsole = NULL;
static NativeEventQueue *gEventQ = NULL;
/** Inidcates whether gEventQ can safely be used or not. */
static volatile bool g_fEventQueueSafe = false;

/* keep this handy for messages */
static com::Utf8Str g_strVMName;
static com::Utf8Str g_strVMUUID;

/** flag whether frontend should terminate */
static volatile bool g_fTerminateFE = false;

////////////////////////////////////////////////////////////////////////////////

/**
 *  Handler for VirtualBoxClient events.
 */
class VirtualBoxClientEventListener
{
public:
    VirtualBoxClientEventListener()
    {
    }

    virtual ~VirtualBoxClientEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnVBoxSVCAvailabilityChanged:
            {
                ComPtr<IVBoxSVCAvailabilityChangedEvent> pVSACEv = aEvent;
                Assert(pVSACEv);
                BOOL fAvailable = FALSE;
                pVSACEv->COMGETTER(Available)(&fAvailable);
                if (!fAvailable)
                {
                    LogRel(("VBoxHeadless: VBoxSVC became unavailable, exiting.\n"));
                    RTPrintf("VBoxSVC became unavailable, exiting.\n");
                    /* Terminate the VM as cleanly as possible given that VBoxSVC
                     * is no longer present. */
                    g_fTerminateFE = true;
                    gEventQ->interruptEventQueueProcessing();
                }
                break;
            }
            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
};

/**
 *  Handler for machine events.
 */
class ConsoleEventListener
{
public:
    ConsoleEventListener() :
        mLastVRDEPort(-1),
        m_fIgnorePowerOffEvents(false),
        m_fNoLoggedInUsers(true)
    {
    }

    virtual ~ConsoleEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnMouseCapabilityChanged:
            {

                ComPtr<IMouseCapabilityChangedEvent> mccev = aEvent;
                Assert(!mccev.isNull());

                BOOL fSupportsAbsolute = false;
                mccev->COMGETTER(SupportsAbsolute)(&fSupportsAbsolute);

                /* Emit absolute mouse event to actually enable the host mouse cursor. */
                if (fSupportsAbsolute && gConsole)
                {
                    ComPtr<IMouse> mouse;
                    gConsole->COMGETTER(Mouse)(mouse.asOutParam());
                    if (mouse)
                    {
                        mouse->PutMouseEventAbsolute(-1, -1, 0, 0 /* Horizontal wheel */, 0);
                    }
                }
                break;
            }
            case VBoxEventType_OnStateChanged:
            {
                ComPtr<IStateChangedEvent> scev = aEvent;
                Assert(scev);

                MachineState_T machineState;
                scev->COMGETTER(State)(&machineState);

                /* Terminate any event wait operation if the machine has been
                 * PoweredDown/Saved/Aborted. */
                if (machineState < MachineState_Running && !m_fIgnorePowerOffEvents)
                {
                    g_fTerminateFE = true;
                    gEventQ->interruptEventQueueProcessing();
                }

                break;
            }
            case VBoxEventType_OnVRDEServerInfoChanged:
            {
                ComPtr<IVRDEServerInfoChangedEvent> rdicev = aEvent;
                Assert(rdicev);

                if (gConsole)
                {
                    ComPtr<IVRDEServerInfo> info;
                    gConsole->COMGETTER(VRDEServerInfo)(info.asOutParam());
                    if (info)
                    {
                        LONG port;
                        info->COMGETTER(Port)(&port);
                        if (port != mLastVRDEPort)
                        {
                            if (port == -1)
                                RTPrintf("VRDE server is inactive.\n");
                            else if (port == 0)
                                RTPrintf("VRDE server failed to start.\n");
                            else
                                RTPrintf("VRDE server is listening on port %d.\n", port);

                            mLastVRDEPort = port;
                        }
                    }
                }
                break;
            }
            case VBoxEventType_OnCanShowWindow:
            {
                ComPtr<ICanShowWindowEvent> cswev = aEvent;
                Assert(cswev);
                cswev->AddVeto(NULL);
                break;
            }
            case VBoxEventType_OnShowWindow:
            {
                ComPtr<IShowWindowEvent> swev = aEvent;
                Assert(swev);
                /* Ignore the event, WinId is either still zero or some other listener assigned it. */
                NOREF(swev); /* swev->COMSETTER(WinId)(0); */
                break;
            }
            case VBoxEventType_OnGuestPropertyChanged:
            {
                ComPtr<IGuestPropertyChangedEvent> pChangedEvent = aEvent;
                Assert(pChangedEvent);

                HRESULT hrc;

                ComPtr <IMachine> pMachine;
                if (gConsole)
                {
                    hrc = gConsole->COMGETTER(Machine)(pMachine.asOutParam());
                    if (FAILED(hrc) || !pMachine)
                        hrc = VBOX_E_OBJECT_NOT_FOUND;
                }
                else
                    hrc = VBOX_E_INVALID_VM_STATE;

                if (SUCCEEDED(hrc))
                {
                    Bstr strKey;
                    hrc = pChangedEvent->COMGETTER(Name)(strKey.asOutParam());
                    AssertComRC(hrc);

                    Bstr strValue;
                    hrc = pChangedEvent->COMGETTER(Value)(strValue.asOutParam());
                    AssertComRC(hrc);

                    Utf8Str utf8Key = strKey;
                    Utf8Str utf8Value = strValue;
                    LogRelFlow(("Guest property \"%s\" has been changed to \"%s\"\n",
                                utf8Key.c_str(), utf8Value.c_str()));

                    if (utf8Key.equals("/VirtualBox/GuestInfo/OS/NoLoggedInUsers"))
                    {
                        LogRelFlow(("Guest indicates that there %s logged in users\n",
                                    utf8Value.equals("true") ? "are no" : "are"));

                        /* Check if this is our machine and the "disconnect on logout feature" is enabled. */
                        BOOL fProcessDisconnectOnGuestLogout = FALSE;

                        /* Does the machine handle VRDP disconnects? */
                        Bstr strDiscon;
                        hrc = pMachine->GetExtraData(Bstr("VRDP/DisconnectOnGuestLogout").raw(),
                                                    strDiscon.asOutParam());
                        if (SUCCEEDED(hrc))
                        {
                            Utf8Str utf8Discon = strDiscon;
                            fProcessDisconnectOnGuestLogout = utf8Discon.equals("1")
                                                            ? TRUE : FALSE;
                        }

                        LogRelFlow(("VRDE: hrc=%Rhrc: Host %s disconnecting clients (current host state known: %s)\n",
                                    hrc, fProcessDisconnectOnGuestLogout ? "will handle" : "does not handle",
                                    m_fNoLoggedInUsers ? "No users logged in" : "Users logged in"));

                        if (fProcessDisconnectOnGuestLogout)
                        {
                            bool fDropConnection = false;
                            if (!m_fNoLoggedInUsers) /* Only if the property really changes. */
                            {
                                if (   utf8Value == "true"
                                    /* Guest property got deleted due to reset,
                                     * so it has no value anymore. */
                                    || utf8Value.isEmpty())
                                {
                                    m_fNoLoggedInUsers = true;
                                    fDropConnection = true;
                                }
                            }
                            else if (utf8Value == "false")
                                m_fNoLoggedInUsers = false;
                            /* Guest property got deleted due to reset,
                             * take the shortcut without touching the m_fNoLoggedInUsers
                             * state. */
                            else if (utf8Value.isEmpty())
                                fDropConnection = true;

                            LogRelFlow(("VRDE: szNoLoggedInUsers=%s, m_fNoLoggedInUsers=%RTbool, fDropConnection=%RTbool\n",
                                        utf8Value.c_str(), m_fNoLoggedInUsers, fDropConnection));

                            if (fDropConnection)
                            {
                                /* If there is a connection, drop it. */
                                ComPtr<IVRDEServerInfo> info;
                                hrc = gConsole->COMGETTER(VRDEServerInfo)(info.asOutParam());
                                if (SUCCEEDED(hrc) && info)
                                {
                                    ULONG cClients = 0;
                                    hrc = info->COMGETTER(NumberOfClients)(&cClients);

                                    LogRelFlow(("VRDE: connected clients=%RU32\n", cClients));
                                    if (SUCCEEDED(hrc) && cClients > 0)
                                    {
                                        ComPtr <IVRDEServer> vrdeServer;
                                        hrc = pMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
                                        if (SUCCEEDED(hrc) && vrdeServer)
                                        {
                                            LogRel(("VRDE: the guest user has logged out, disconnecting remote clients.\n"));
                                            hrc = vrdeServer->COMSETTER(Enabled)(FALSE);
                                            AssertComRC(hrc);
                                            HRESULT hrc2 = vrdeServer->COMSETTER(Enabled)(TRUE);
                                            if (SUCCEEDED(hrc))
                                                hrc = hrc2;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (FAILED(hrc))
                        LogRelFlow(("VRDE: returned error=%Rhrc\n", hrc));
                }

                break;
            }

            default:
                AssertFailed();
        }
        return S_OK;
    }

    void ignorePowerOffEvents(bool fIgnore)
    {
        m_fIgnorePowerOffEvents = fIgnore;
    }

private:

    long mLastVRDEPort;
    bool m_fIgnorePowerOffEvents;
    bool m_fNoLoggedInUsers;
};

typedef ListenerImpl<VirtualBoxClientEventListener> VirtualBoxClientEventListenerImpl;
typedef ListenerImpl<ConsoleEventListener> ConsoleEventListenerImpl;

VBOX_LISTENER_DECLARE(VirtualBoxClientEventListenerImpl)
VBOX_LISTENER_DECLARE(ConsoleEventListenerImpl)

#if !defined(RT_OS_WINDOWS)

/** Signals we handle. */
static int const g_aiSigs[] = { SIGHUP, SIGINT, SIGTERM, SIGUSR1 };

/** The signal handler. */
static void HandleSignal(int sig)
{
# if 1
    struct iovec aSegs[8];
    int          cSegs = 0;
    aSegs[cSegs++].iov_base = (char *)"VBoxHeadless: signal ";
    aSegs[cSegs++].iov_base = (char *)strsignal(sig);
    const char *pszThread = RTThreadSelfName();
    if (pszThread)
    {
        aSegs[cSegs++].iov_base = (char *)"(on thread ";
        aSegs[cSegs++].iov_base = (char *)pszThread;
        aSegs[cSegs++].iov_base = (char *)")\n";
    }
    else
        aSegs[cSegs++].iov_base = (char *)"\n";
    for (int i = 0; i < cSegs; i++)
        aSegs[i].iov_len = strlen((const char *)aSegs[i].iov_base);
    ssize_t ignored = writev(2, aSegs, cSegs); RT_NOREF_PV(ignored);
# else
    LogRel(("VBoxHeadless: received signal %d\n", sig)); /** @todo r=bird: This is not at all safe. */
# endif
    g_fTerminateFE = true;
}

# ifdef RT_OS_DARWIN

#if 0 /* unused */
/* For debugging. */
uint32_t GetSignalMask(void)
{
    /* For some totally messed up reason, the xnu sigprocmask actually returns
       the signal mask of the calling thread rather than the process one
       (p_sigmask), so can call sigprocmask just as well as pthread_sigmask here. */
    sigset_t Sigs;
    RT_ZERO(Sigs);
    sigprocmask(SIG_UNBLOCK, NULL, &Sigs);
    RTMsgInfo("debug: thread %s mask: %.*Rhxs\n", RTThreadSelfName(), sizeof(Sigs), &Sigs);
    for (int i = 0; i < 32; i++)
        if (sigismember(&Sigs, i)) RTMsgInfo("debug: sig %2d blocked: %s\n", i, strsignal(i));
    return *(uint32_t const *)&Sigs;
}
#endif

/**
 * Blocks or unblocks the signals we handle.
 *
 * @note Only for darwin does fProcess make a difference, all others always
 *       work on the calling thread regardless of the flag value.
 */
static void SetSignalMask(bool fBlock, bool fProcess)
{
    sigset_t Sigs;
    sigemptyset(&Sigs);
    for (unsigned i = 0; i < RT_ELEMENTS(g_aiSigs); i++)
        sigaddset(&Sigs, g_aiSigs[i]);
    if (fProcess)
    {
        if (sigprocmask(fBlock ? SIG_BLOCK : SIG_UNBLOCK, &Sigs, NULL) != 0)
            RTMsgError("sigprocmask failed: %d", errno);
    }
    else
    {
        if (pthread_sigmask(fBlock ? SIG_BLOCK : SIG_UNBLOCK, &Sigs, NULL) != 0)
            RTMsgError("pthread_sigmask failed: %d", errno);
    }
}

/**
 * @callback_method_impl{FNRTTHREAD, Signal wait thread}
 */
static DECLCALLBACK(int) SigThreadProc(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf, pvUser);

    /* The signals to wait for: */
    sigset_t SigSetWait;
    sigemptyset(&SigSetWait);
    for (unsigned i = 0; i < RT_ELEMENTS(g_aiSigs); i++)
        sigaddset(&SigSetWait, g_aiSigs[i]);

    /* The wait + processing loop: */
    for (;;)
    {
        int iSignal = -1;
        if (sigwait(&SigSetWait, &iSignal) == 0)
        {
            LogRel(("VBoxHeadless: Caught signal: %s\n", strsignal(iSignal)));
            RTMsgInfo("");
            RTMsgInfo("Caught signal: %s", strsignal(iSignal));
            g_fTerminateFE = true;
        }

        if (g_fTerminateFE && g_fEventQueueSafe && gEventQ != NULL)
            gEventQ->interruptEventQueueProcessing();
    }
}

/** The handle to the signal wait thread. */
static RTTHREAD g_hSigThread = NIL_RTTHREAD;

# endif /* RT_OS_DARWIN */

static void SetUpSignalHandlers(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    /* Don't touch SIGUSR2 as IPRT could be using it for RTThreadPoke(). */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aiSigs); i++)
    {
        struct sigaction sa;
        RT_ZERO(sa);
        sa.sa_handler = HandleSignal;
        if (sigaction(g_aiSigs[i], &sa, NULL) != 0)
            RTMsgError("sigaction failed for signal #%u: %d", g_aiSigs[i], errno);
    }

# if defined(RT_OS_DARWIN)
    /*
     * On darwin processEventQueue() does not return with VERR_INTERRUPTED or
     * similar if a signal arrives while we're waiting for events.  So, in
     * order to respond promptly to signals after they arrives, we use a
     * dedicated thread for fielding the signals and poking the event queue
     * after each signal.
     *
     * We block the signals for all threads (this is fine as the p_sigmask
     * isn't actually used for anything at all and wont prevent signal
     * delivery).  The signal thread should have them blocked as well, as it
     * uses sigwait to do the waiting (better than sigsuspend, as we can safely
     * LogRel the signal this way).
     */
    if (g_hSigThread == NIL_RTTHREAD)
    {
        SetSignalMask(true /*fBlock */, true /*fProcess*/);
        int vrc = RTThreadCreate(&g_hSigThread, SigThreadProc, NULL, 0, RTTHREADTYPE_DEFAULT, 0, "SigWait");
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Failed to create signal waiter thread: %Rrc", vrc);
            SetSignalMask(false /*fBlock */, false /*fProcess*/);
        }
    }
# endif
}

#endif /* !RT_OS_WINDOWS */

////////////////////////////////////////////////////////////////////////////////

static void show_usage()
{
    RTPrintf("Usage:\n"
             "   -s, -startvm, --startvm <name|uuid>   Start given VM (required argument)\n"
             "   -v, -vrde, --vrde on|off|config       Enable or disable the VRDE server\n"
             "                                           or don't change the setting (default)\n"
             "   -e, -vrdeproperty, --vrdeproperty <name=[value]> Set a VRDE property:\n"
             "                                     \"TCP/Ports\" - comma-separated list of\n"
             "                                       ports the VRDE server can bind to; dash\n"
             "                                       between two port numbers specifies range\n"
             "                                     \"TCP/Address\" - interface IP the VRDE\n"
             "                                       server will bind to\n"
             "   --settingspw <pw>                 Specify the VirtualBox settings password\n"
             "   --settingspwfile <file>           Specify a file containing the\n"
             "                                       VirtualBox settings password\n"
             "   --password <file>|-               Specify the VM password. Either file containing\n"
             "                                     the VM password or \"-\" to read it from console\n"
             "   --password-id <id>                Specify the password id for the VM password\n"
             "   -start-paused, --start-paused     Start the VM in paused state\n"
#ifdef VBOX_WITH_RECORDING
             "   -c, -record, --record             Record the VM screen output to a file\n"
             "   -w, --videowidth                  Video frame width when recording\n"
             "   -h, --videoheight                 Video frame height when recording\n"
             "   -r, --videobitrate                Recording bit rate when recording\n"
             "   -f, --filename                    File name when recording. The codec used\n"
             "                                     will be chosen based on file extension\n"
#endif
             "\n");
}

#ifdef VBOX_WITH_RECORDING
/**
 * Parse the environment for variables which can influence the VIDEOREC settings.
 * purely for backwards compatibility.
 * @param pulFrameWidth may be updated with a desired frame width
 * @param pulFrameHeight may be updated with a desired frame height
 * @param pulBitRate may be updated with a desired bit rate
 * @param ppszFilename may be updated with a desired file name
 */
static void parse_environ(uint32_t *pulFrameWidth, uint32_t *pulFrameHeight,
                          uint32_t *pulBitRate, const char **ppszFilename)
{
    const char *pszEnvTemp;
/** @todo r=bird: This isn't up to scratch. The life time of an RTEnvGet
 *        return value is only up to the next RTEnv*, *getenv, *putenv,
 *        setenv call in _any_ process in the system and the it has known and
 *        documented code page issues.
 *
 *        Use RTEnvGetEx instead! */
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDWIDTH")) != 0)
    {
        errno = 0;
        unsigned long ulFrameWidth = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDWIDTH environment variable", 0);
        else
            *pulFrameWidth = ulFrameWidth;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDHEIGHT")) != 0)
    {
        errno = 0;
        unsigned long ulFrameHeight = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDHEIGHT environment variable", 0);
        else
            *pulFrameHeight = ulFrameHeight;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDBITRATE")) != 0)
    {
        errno = 0;
        unsigned long ulBitRate = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_RECORDBITRATE environment variable", 0);
        else
            *pulBitRate = ulBitRate;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_RECORDFILE")) != 0)
        *ppszFilename = pszEnvTemp;
}
#endif /* VBOX_WITH_RECORDING defined */


#ifdef RT_OS_WINDOWS

#define MAIN_WND_CLASS L"VirtualBox Headless Interface"

HINSTANCE g_hInstance = NULL;
HWND g_hWindow = NULL;
RTSEMEVENT g_hCanQuit;

static DECLCALLBACK(int) windowsMessageMonitor(RTTHREAD ThreadSelf, void *pvUser);
static int createWindow();
static LRESULT CALLBACK WinMainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void destroyWindow();


static DECLCALLBACK(int)
windowsMessageMonitor(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf, pvUser);
    int rc;

    rc = createWindow();
    if (RT_FAILURE(rc))
        return rc;

    RTSemEventCreate(&g_hCanQuit);

    MSG msg;
    BOOL b;
    while ((b = ::GetMessage(&msg, 0, 0, 0)) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    if (b < 0)
        LogRel(("VBoxHeadless: GetMessage failed\n"));

    destroyWindow();
    return VINF_SUCCESS;
}


static int
createWindow()
{
    /* program instance handle */
    g_hInstance = (HINSTANCE)::GetModuleHandle(NULL);
    if (g_hInstance == NULL)
    {
        LogRel(("VBoxHeadless: failed to obtain module handle\n"));
        return VERR_GENERAL_FAILURE;
    }

    /* window class */
    WNDCLASS wc;
    RT_ZERO(wc);

    wc.style = CS_NOCLOSE;
    wc.lpfnWndProc = WinMainWndProc;
    wc.hInstance = g_hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszClassName = MAIN_WND_CLASS;

    ATOM atomWindowClass = ::RegisterClass(&wc);
    if (atomWindowClass == 0)
    {
        LogRel(("VBoxHeadless: failed to register window class\n"));
        return VERR_GENERAL_FAILURE;
    }

    /* secret window, secret garden */
    g_hWindow = ::CreateWindowEx(0, MAIN_WND_CLASS, MAIN_WND_CLASS, 0,
                                 0, 0, 1, 1, NULL, NULL, g_hInstance, NULL);
    if (g_hWindow == NULL)
    {
        LogRel(("VBoxHeadless: failed to create window\n"));
        return VERR_GENERAL_FAILURE;
    }

    return VINF_SUCCESS;
}


static void
destroyWindow()
{
    if (g_hWindow == NULL)
        return;

    ::DestroyWindow(g_hWindow);
    g_hWindow = NULL;

    if (g_hInstance == NULL)
        return;

    ::UnregisterClass(MAIN_WND_CLASS, g_hInstance);
    g_hInstance = NULL;
}


static LRESULT CALLBACK
WinMainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int rc;

    LRESULT lResult = 0;
    switch (msg)
    {
        case WM_QUERYENDSESSION:
            LogRel(("VBoxHeadless: WM_QUERYENDSESSION:%s%s%s%s (0x%08lx)\n",
                    lParam == 0                  ? " shutdown" : "",
                    lParam & ENDSESSION_CRITICAL ? " critical" : "",
                    lParam & ENDSESSION_LOGOFF   ? " logoff"   : "",
                    lParam & ENDSESSION_CLOSEAPP ? " close"    : "",
                    (unsigned long)lParam));

            /* do not block windows session termination */
            lResult = TRUE;
            break;

        case WM_ENDSESSION:
            lResult = 0;
            LogRel(("WM_ENDSESSION:%s%s%s%s%s (%s/0x%08lx)\n",
                    lParam == 0                  ? " shutdown"  : "",
                    lParam & ENDSESSION_CRITICAL ? " critical"  : "",
                    lParam & ENDSESSION_LOGOFF   ? " logoff"    : "",
                    lParam & ENDSESSION_CLOSEAPP ? " close"     : "",
                    wParam == FALSE              ? " cancelled" : "",
                    wParam ? "TRUE" : "FALSE",
                    (unsigned long)lParam));
            if (wParam == FALSE)
                break;

            /* tell the user what we are doing */
            ::ShutdownBlockReasonCreate(hwnd,
                com::BstrFmt("%s saving state",
                             g_strVMName.c_str()).raw());

            /* tell the VM to save state/power off */
            g_fTerminateFE = true;
            if (g_fEventQueueSafe && gEventQ != NULL)
                gEventQ->interruptEventQueueProcessing();

            if (g_hCanQuit != NIL_RTSEMEVENT)
            {
                LogRel(("VBoxHeadless: WM_ENDSESSION: waiting for VM termination...\n"));

                rc = RTSemEventWait(g_hCanQuit, RT_INDEFINITE_WAIT);
                if (RT_SUCCESS(rc))
                    LogRel(("VBoxHeadless: WM_ENDSESSION: done\n"));
                else
                    LogRel(("VBoxHeadless: WM_ENDSESSION: failed to wait for VM termination: %Rrc\n", rc));
            }
            else
            {
                LogRel(("VBoxHeadless: WM_ENDSESSION: cannot wait for VM termination\n"));
            }
            break;

        default:
            lResult = ::DefWindowProc(hwnd, msg, wParam, lParam);
            break;
    }
    return lResult;
}


static const char * const g_apszCtrlEventNames[] =
{
    "CTRL_C_EVENT",
    "CTRL_BREAK_EVENT",
    "CTRL_CLOSE_EVENT",
    /* reserved, not used */
    "<console control event 3>",
    "<console control event 4>",
    /* not sent to processes that load gdi32.dll or user32.dll */
    "CTRL_LOGOFF_EVENT",
    "CTRL_SHUTDOWN_EVENT",
};


BOOL WINAPI
ConsoleCtrlHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    const char *pszSigName;
    char szNameBuf[48];
    if (dwCtrlType < RT_ELEMENTS(g_apszCtrlEventNames))
        pszSigName = g_apszCtrlEventNames[dwCtrlType];
    else
    {
        /* should not happen, but be prepared */
        RTStrPrintf(szNameBuf, sizeof(szNameBuf), "<console control event %u>", dwCtrlType);
        pszSigName = szNameBuf;
    }

    LogRel(("VBoxHeadless: got %s\n", pszSigName));
    RTMsgInfo("Got %s", pszSigName);
    RTMsgInfo("");

    /* tell the VM to save state/power off */
    g_fTerminateFE = true;
    if (g_fEventQueueSafe && gEventQ != NULL)
        gEventQ->interruptEventQueueProcessing();

    /*
     * We don't need to wait for Ctrl-C / Ctrl-Break, but we must wait
     * for Close, or we will be killed before the VM is saved.
     */
    if (g_hCanQuit != NIL_RTSEMEVENT)
    {
        LogRel(("VBoxHeadless: waiting for VM termination...\n"));

        int rc = RTSemEventWait(g_hCanQuit, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            LogRel(("VBoxHeadless: Failed to wait for VM termination: %Rrc\n", rc));
    }

    /* tell the system we handled it */
    LogRel(("VBoxHeadless: ConsoleCtrlHandler: return\n"));
    return TRUE;
}
#endif /* RT_OS_WINDOWS */


/*
 * Simplified version of showProgress() borrowed from VBoxManage.
 * Note that machine power up/down operations are not cancelable, so
 * we don't bother checking for signals.
 */
static HRESULT
showProgress(const ComPtr<IProgress> &progress)
{
    BOOL fCompleted = FALSE;
    ULONG ulLastPercent = 0;
    ULONG ulCurrentPercent = 0;
    HRESULT hrc;

    com::Bstr bstrDescription;
    hrc = progress->COMGETTER(Description(bstrDescription.asOutParam()));
    if (FAILED(hrc))
    {
        RTStrmPrintf(g_pStdErr, "Failed to get progress description: %Rhrc\n", hrc);
        return hrc;
    }

    RTStrmPrintf(g_pStdErr, "%ls: ", bstrDescription.raw());
    RTStrmFlush(g_pStdErr);

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

        gEventQ->processEventQueue(500);
        hrc = progress->COMGETTER(Completed(&fCompleted));
    }

    /* complete the line. */
    LONG iRc = E_FAIL;
    hrc = progress->COMGETTER(ResultCode)(&iRc);
    if (SUCCEEDED(hrc))
    {
        if (SUCCEEDED(iRc))
            RTStrmPrintf(g_pStdErr, "100%%\n");
#if 0
        else if (g_fCanceled)
            RTStrmPrintf(g_pStdErr, "CANCELED\n");
#endif
        else
        {
            RTStrmPrintf(g_pStdErr, "\n");
            RTStrmPrintf(g_pStdErr, "Operation failed: %Rhrc\n", iRc);
        }
        hrc = iRc;
    }
    else
    {
        RTStrmPrintf(g_pStdErr, "\n");
        RTStrmPrintf(g_pStdErr, "Failed to obtain operation result: %Rhrc\n", hrc);
    }
    RTStrmFlush(g_pStdErr);
    return hrc;
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF(envp);
    const char *vrdePort = NULL;
    const char *vrdeAddress = NULL;
    const char *vrdeEnabled = NULL;
    unsigned cVRDEProperties = 0;
    const char *apszVRDEProperties[16];
    unsigned fPaused = 0;
#ifdef VBOX_WITH_RECORDING
    bool fRecordEnabled = false;
    uint32_t ulRecordVideoWidth = 800;
    uint32_t ulRecordVideoHeight = 600;
    uint32_t ulRecordVideoRate = 300000;
    char szRecordFilename[RTPATH_MAX];
    const char *pszRecordFilenameTemplate = "VBox-%d.webm"; /* .webm container by default. */
#endif /* VBOX_WITH_RECORDING */
#ifdef RT_OS_WINDOWS
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

    LogFlow(("VBoxHeadless STARTED.\n"));
    RTPrintf(VBOX_PRODUCT " Headless Interface " VBOX_VERSION_STRING "\n"
             "Copyright (C) 2008-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");

#ifdef VBOX_WITH_RECORDING
    /* Parse the environment */
    parse_environ(&ulRecordVideoWidth, &ulRecordVideoHeight, &ulRecordVideoRate, &pszRecordFilenameTemplate);
#endif

    enum eHeadlessOptions
    {
        OPT_SETTINGSPW = 0x100,
        OPT_SETTINGSPW_FILE,
        OPT_COMMENT,
        OPT_PAUSED,
        OPT_VMPW,
        OPT_VMPWID
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "-startvm", 's', RTGETOPT_REQ_STRING },
        { "--startvm", 's', RTGETOPT_REQ_STRING },
        { "-vrdpport", 'p', RTGETOPT_REQ_STRING },     /* VRDE: deprecated. */
        { "--vrdpport", 'p', RTGETOPT_REQ_STRING },    /* VRDE: deprecated. */
        { "-vrdpaddress", 'a', RTGETOPT_REQ_STRING },  /* VRDE: deprecated. */
        { "--vrdpaddress", 'a', RTGETOPT_REQ_STRING }, /* VRDE: deprecated. */
        { "-vrdp", 'v', RTGETOPT_REQ_STRING },         /* VRDE: deprecated. */
        { "--vrdp", 'v', RTGETOPT_REQ_STRING },        /* VRDE: deprecated. */
        { "-vrde", 'v', RTGETOPT_REQ_STRING },
        { "--vrde", 'v', RTGETOPT_REQ_STRING },
        { "-vrdeproperty", 'e', RTGETOPT_REQ_STRING },
        { "--vrdeproperty", 'e', RTGETOPT_REQ_STRING },
        { "--settingspw", OPT_SETTINGSPW, RTGETOPT_REQ_STRING },
        { "--settingspwfile", OPT_SETTINGSPW_FILE, RTGETOPT_REQ_STRING },
        { "--password", OPT_VMPW, RTGETOPT_REQ_STRING },
        { "--password-id", OPT_VMPWID, RTGETOPT_REQ_STRING },
#ifdef VBOX_WITH_RECORDING
        { "-record", 'c', 0 },
        { "--record", 'c', 0 },
        { "--videowidth", 'w', RTGETOPT_REQ_UINT32 },
        { "--videoheight", 'h', RTGETOPT_REQ_UINT32 }, /* great choice of short option! */
        { "--videorate", 'r', RTGETOPT_REQ_UINT32 },
        { "--filename", 'f', RTGETOPT_REQ_STRING },
#endif /* VBOX_WITH_RECORDING defined */
        { "-comment", OPT_COMMENT, RTGETOPT_REQ_STRING },
        { "--comment", OPT_COMMENT, RTGETOPT_REQ_STRING },
        { "-start-paused", OPT_PAUSED, 0 },
        { "--start-paused", OPT_PAUSED, 0 }
    };

    const char *pcszNameOrUUID = NULL;

    // parse the command line
    int ch;
    const char *pcszSettingsPw = NULL;
    const char *pcszSettingsPwFile = NULL;
    const char *pcszVmPassword = NULL;
    const char *pcszVmPasswordId = NULL;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch(ch)
        {
            case 's':
                pcszNameOrUUID = ValueUnion.psz;
                break;
            case 'p':
                RTPrintf("Warning: '-p' or '-vrdpport' are deprecated. Use '-e \"TCP/Ports=%s\"'\n", ValueUnion.psz);
                vrdePort = ValueUnion.psz;
                break;
            case 'a':
                RTPrintf("Warning: '-a' or '-vrdpaddress' are deprecated. Use '-e \"TCP/Address=%s\"'\n", ValueUnion.psz);
                vrdeAddress = ValueUnion.psz;
                break;
            case 'v':
                vrdeEnabled = ValueUnion.psz;
                break;
            case 'e':
                if (cVRDEProperties < RT_ELEMENTS(apszVRDEProperties))
                    apszVRDEProperties[cVRDEProperties++] = ValueUnion.psz;
                else
                     RTPrintf("Warning: too many VRDE properties. Ignored: '%s'\n", ValueUnion.psz);
                break;
            case OPT_SETTINGSPW:
                pcszSettingsPw = ValueUnion.psz;
                break;
            case OPT_SETTINGSPW_FILE:
                pcszSettingsPwFile = ValueUnion.psz;
                break;
            case OPT_VMPW:
                pcszVmPassword = ValueUnion.psz;
                break;
            case OPT_VMPWID:
                pcszVmPasswordId = ValueUnion.psz;
                break;
            case OPT_PAUSED:
                fPaused = true;
                break;
#ifdef VBOX_WITH_RECORDING
            case 'c':
                fRecordEnabled = true;
                break;
            case 'w':
                ulRecordVideoWidth = ValueUnion.u32;
                break;
            case 'r':
                ulRecordVideoRate = ValueUnion.u32;
                break;
            case 'f':
                pszRecordFilenameTemplate = ValueUnion.psz;
                break;
#endif /* VBOX_WITH_RECORDING defined */
            case 'h':
#ifdef VBOX_WITH_RECORDING
                if ((GetState.pDef->fFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING)
                {
                    ulRecordVideoHeight = ValueUnion.u32;
                    break;
                }
#endif
                show_usage();
                return 0;
            case OPT_COMMENT:
                /* nothing to do */
                break;
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;
            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                show_usage();
                return ch;
        }
    }

#ifdef VBOX_WITH_RECORDING
    if (ulRecordVideoWidth < 512 || ulRecordVideoWidth > 2048 || ulRecordVideoWidth % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video frame width between 512 and 2048", 0);
        return 1;
    }
    if (ulRecordVideoHeight < 384 || ulRecordVideoHeight > 1536 || ulRecordVideoHeight % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video frame height between 384 and 1536", 0);
        return 1;
    }
    if (ulRecordVideoRate < 300000 || ulRecordVideoRate > 1000000)
    {
        LogError("VBoxHeadless: ERROR: please specify an even video bitrate between 300000 and 1000000", 0);
        return 1;
    }
    /* Make sure we only have %d or %u (or none) in the file name specified */
    char *pcPercent = (char*)strchr(pszRecordFilenameTemplate, '%');
    if (pcPercent != 0 && *(pcPercent + 1) != 'd' && *(pcPercent + 1) != 'u')
    {
        LogError("VBoxHeadless: ERROR: Only %%d and %%u are allowed in the recording file name.", -1);
        return 1;
    }
    /* And no more than one % in the name */
    if (pcPercent != 0 && strchr(pcPercent + 1, '%') != 0)
    {
        LogError("VBoxHeadless: ERROR: Only one format modifier is allowed in the recording file name.", -1);
        return 1;
    }
    RTStrPrintf(&szRecordFilename[0], RTPATH_MAX, pszRecordFilenameTemplate, RTProcSelf());
#endif /* defined VBOX_WITH_RECORDING */

    if (!pcszNameOrUUID)
    {
        show_usage();
        return 1;
    }

    HRESULT hrc;
    int vrc;

    hrc = com::Initialize();
#ifdef VBOX_WITH_XPCOM
    if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        RTPrintf("Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
        return 1;
    }
#endif
    if (FAILED(hrc))
    {
        RTPrintf("VBoxHeadless: ERROR: failed to initialize COM!\n");
        return 1;
    }

    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;
    ComPtr<IMachine> machine;
    bool fSessionOpened = false;
    ComPtr<IEventListener> vboxClientListener;
    ComPtr<IEventListener> vboxListener;
    ComObjPtr<ConsoleEventListenerImpl> consoleListener;

    do
    {
        hrc = pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
        if (FAILED(hrc))
        {
            RTPrintf("VBoxHeadless: ERROR: failed to create the VirtualBoxClient object!\n");
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
            {
                com::GluePrintRCMessage(hrc);
                RTPrintf("Most likely, the VirtualBox COM server is not running or failed to start.\n");
            }
            else
                GluePrintErrorInfo(info);
            break;
        }

        hrc = pVirtualBoxClient->COMGETTER(VirtualBox)(virtualBox.asOutParam());
        if (FAILED(hrc))
        {
            RTPrintf("Failed to get VirtualBox object (rc=%Rhrc)!\n", hrc);
            break;
        }
        hrc = pVirtualBoxClient->COMGETTER(Session)(session.asOutParam());
        if (FAILED(hrc))
        {
            RTPrintf("Failed to get session object (rc=%Rhrc)!\n", hrc);
            break;
        }

        if (pcszSettingsPw)
        {
            CHECK_ERROR(virtualBox, SetSettingsSecret(Bstr(pcszSettingsPw).raw()));
            if (FAILED(hrc))
                break;
        }
        else if (pcszSettingsPwFile)
        {
            int rcExit = settingsPasswordFile(virtualBox, pcszSettingsPwFile);
            if (rcExit != RTEXITCODE_SUCCESS)
                break;
        }

        ComPtr<IMachine> m;

        hrc = virtualBox->FindMachine(Bstr(pcszNameOrUUID).raw(), m.asOutParam());
        if (FAILED(hrc))
        {
            LogError("Invalid machine name or UUID!\n", hrc);
            break;
        }

        /* add VM password if required */
        if (pcszVmPassword && pcszVmPasswordId)
        {
            com::Utf8Str strPassword;
            if (!RTStrCmp(pcszVmPassword, "-"))
            {
                /* Get password from console. */
                RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, "Enter the password:");
                if (rcExit == RTEXITCODE_FAILURE)
                    break;
            }
            else
            {
                RTEXITCODE rcExit = readPasswordFile(pcszVmPassword, &strPassword);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
            }
            CHECK_ERROR_BREAK(m, AddEncryptionPassword(Bstr(pcszVmPasswordId).raw(),
                                                       Bstr(strPassword).raw()));
        }
        Bstr bstrVMId;
        hrc = m->COMGETTER(Id)(bstrVMId.asOutParam());
        AssertComRC(hrc);
        if (FAILED(hrc))
            break;
        g_strVMUUID = bstrVMId;

        Bstr bstrVMName;
        hrc = m->COMGETTER(Name)(bstrVMName.asOutParam());
        AssertComRC(hrc);
        if (FAILED(hrc))
            break;
        g_strVMName = bstrVMName;

        Log(("VBoxHeadless: Opening a session with machine (id={%s})...\n",
             g_strVMUUID.c_str()));

        // set session name
        CHECK_ERROR_BREAK(session, COMSETTER(Name)(Bstr("headless").raw()));
        // open a session
        CHECK_ERROR_BREAK(m, LockMachine(session, LockType_VM));
        fSessionOpened = true;

        /* get the console */
        ComPtr<IConsole> console;
        CHECK_ERROR_BREAK(session, COMGETTER(Console)(console.asOutParam()));

        /* get the mutable machine */
        CHECK_ERROR_BREAK(console, COMGETTER(Machine)(machine.asOutParam()));

        ComPtr<IDisplay> display;
        CHECK_ERROR_BREAK(console, COMGETTER(Display)(display.asOutParam()));

#ifdef VBOX_WITH_RECORDING
        if (fRecordEnabled)
        {
            ComPtr<IRecordingSettings> recordingSettings;
            CHECK_ERROR_BREAK(machine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));
            CHECK_ERROR_BREAK(recordingSettings, COMSETTER(Enabled)(TRUE));

            SafeIfaceArray <IRecordingScreenSettings> saRecordScreenScreens;
            CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Screens)(ComSafeArrayAsOutParam(saRecordScreenScreens)));

            /* Note: For now all screens have the same configuration. */
            for (size_t i = 0; i < saRecordScreenScreens.size(); ++i)
            {
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(Enabled)(TRUE));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(Filename)(Bstr(szRecordFilename).raw()));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoWidth)(ulRecordVideoWidth));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoHeight)(ulRecordVideoHeight));
                CHECK_ERROR_BREAK(saRecordScreenScreens[i], COMSETTER(VideoRate)(ulRecordVideoRate));
            }
        }
#endif /* defined(VBOX_WITH_RECORDING) */

#if 0
        /* get the machine debugger (isn't necessarily available) */
        ComPtr <IMachineDebugger> machineDebugger;
        console->COMGETTER(Debugger)(machineDebugger.asOutParam());
        if (machineDebugger)
            Log(("Machine debugger available!\n"));
#endif

        /* initialize global references */
        gConsole = console;
        gEventQ = com::NativeEventQueue::getMainEventQueue();
        g_fEventQueueSafe = true;

        /* VirtualBoxClient events registration. */
        {
            ComPtr<IEventSource> pES;
            CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
            ComObjPtr<VirtualBoxClientEventListenerImpl> listener;
            listener.createObject();
            listener->init(new VirtualBoxClientEventListener());
            vboxClientListener = listener;
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnVBoxSVCAvailabilityChanged);
            CHECK_ERROR(pES, RegisterListener(vboxClientListener, ComSafeArrayAsInParam(eventTypes), true));
        }

        /* Console events registration. */
        {
            ComPtr<IEventSource> es;
            CHECK_ERROR(console, COMGETTER(EventSource)(es.asOutParam()));
            consoleListener.createObject();
            consoleListener->init(new ConsoleEventListener());
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnMouseCapabilityChanged);
            eventTypes.push_back(VBoxEventType_OnStateChanged);
            eventTypes.push_back(VBoxEventType_OnVRDEServerInfoChanged);
            eventTypes.push_back(VBoxEventType_OnCanShowWindow);
            eventTypes.push_back(VBoxEventType_OnShowWindow);
            eventTypes.push_back(VBoxEventType_OnGuestPropertyChanged);
            CHECK_ERROR(es, RegisterListener(consoleListener, ComSafeArrayAsInParam(eventTypes), true));
        }

        /* Default is to use the VM setting for the VRDE server. */
        enum VRDEOption
        {
            VRDEOption_Config,
            VRDEOption_Off,
            VRDEOption_On
        };
        VRDEOption enmVRDEOption = VRDEOption_Config;
        BOOL fVRDEEnabled;
        ComPtr <IVRDEServer> vrdeServer;
        CHECK_ERROR_BREAK(machine, COMGETTER(VRDEServer)(vrdeServer.asOutParam()));
        CHECK_ERROR_BREAK(vrdeServer, COMGETTER(Enabled)(&fVRDEEnabled));

        if (vrdeEnabled != NULL)
        {
            /* -vrde on|off|config */
            if (!strcmp(vrdeEnabled, "off") || !strcmp(vrdeEnabled, "disable"))
                enmVRDEOption = VRDEOption_Off;
            else if (!strcmp(vrdeEnabled, "on") || !strcmp(vrdeEnabled, "enable"))
                enmVRDEOption = VRDEOption_On;
            else if (strcmp(vrdeEnabled, "config"))
            {
                RTPrintf("-vrde requires an argument (on|off|config)\n");
                break;
            }
        }

        Log(("VBoxHeadless: enmVRDE %d, fVRDEEnabled %d\n", enmVRDEOption, fVRDEEnabled));

        if (enmVRDEOption != VRDEOption_Off)
        {
            /* Set other specified options. */

            /* set VRDE port if requested by the user */
            if (vrdePort != NULL)
            {
                Bstr bstr = vrdePort;
                CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(Bstr("TCP/Ports").raw(), bstr.raw()));
            }
            /* set VRDE address if requested by the user */
            if (vrdeAddress != NULL)
            {
                CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(Bstr("TCP/Address").raw(), Bstr(vrdeAddress).raw()));
            }

            /* Set VRDE properties. */
            if (cVRDEProperties > 0)
            {
                for (unsigned i = 0; i < cVRDEProperties; i++)
                {
                    /* Split 'name=value' and feed the parts to SetVRDEProperty. */
                    const char *pszDelimiter = strchr(apszVRDEProperties[i], '=');
                    if (pszDelimiter)
                    {
                        Bstr bstrName(apszVRDEProperties[i], pszDelimiter - apszVRDEProperties[i]);
                        Bstr bstrValue(pszDelimiter + 1);
                        CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(bstrName.raw(), bstrValue.raw()));
                    }
                    else
                    {
                        RTPrintf("Error: Invalid VRDE property '%s'\n", apszVRDEProperties[i]);
                        hrc = E_INVALIDARG;
                        break;
                    }
                }
                if (FAILED(hrc))
                    break;
            }

        }

        if (enmVRDEOption == VRDEOption_On)
        {
            /* enable VRDE server (only if currently disabled) */
            if (!fVRDEEnabled)
            {
                CHECK_ERROR_BREAK(vrdeServer, COMSETTER(Enabled)(TRUE));
            }
        }
        else if (enmVRDEOption == VRDEOption_Off)
        {
            /* disable VRDE server (only if currently enabled */
            if (fVRDEEnabled)
            {
                CHECK_ERROR_BREAK(vrdeServer, COMSETTER(Enabled)(FALSE));
            }
        }

        /* Disable the host clipboard before powering up */
        console->COMSETTER(UseHostClipboard)(false);

        Log(("VBoxHeadless: Powering up the machine...\n"));


        /**
         * @todo We should probably install handlers earlier so that
         * we can undo any temporary settings we do above in case of
         * an early signal and use RAII to ensure proper cleanup.
         */
#if !defined(RT_OS_WINDOWS)
        ::SetUpSignalHandlers();
#else
        /*
         * Register windows console signal handler to react to Ctrl-C,
         * Ctrl-Break, Close, non-interactive session termination.
         */
        ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif


        ComPtr <IProgress> progress;
        if (!fPaused)
            CHECK_ERROR_BREAK(console, PowerUp(progress.asOutParam()));
        else
            CHECK_ERROR_BREAK(console, PowerUpPaused(progress.asOutParam()));

        hrc = showProgress(progress);
        if (FAILED(hrc))
        {
            com::ProgressErrorInfo info(progress);
            if (info.isBasicAvailable())
            {
                RTPrintf("Error: failed to start machine. Error message: %ls\n", info.getText().raw());
            }
            else
            {
                RTPrintf("Error: failed to start machine. No error message available!\n");
            }
            break;
        }

#ifdef RT_OS_WINDOWS
        /*
         * Spawn windows message pump to monitor session events.
         */
        RTTHREAD hThrMsg;
        vrc = RTThreadCreate(&hThrMsg,
                             windowsMessageMonitor, NULL,
                             0, /* :cbStack */
                             RTTHREADTYPE_MSG_PUMP, 0,
                             "MSG");
        if (RT_FAILURE(vrc))    /* not fatal */
            LogRel(("VBoxHeadless: failed to start windows message monitor: %Rrc\n", vrc));
#endif /* RT_OS_WINDOWS */


        /*
         * Pump vbox events forever
         */
        LogRel(("VBoxHeadless: starting event loop\n"));
        for (;;)
        {
            if (g_fTerminateFE)
            {
                LogRel(("VBoxHeadless: processEventQueue: termination requested\n"));
                break;
            }

            vrc = gEventQ->processEventQueue(RT_INDEFINITE_WAIT);

            /*
             * interruptEventQueueProcessing from another thread is
             * reported as VERR_INTERRUPTED, so check the flag first.
             */
            if (g_fTerminateFE)
            {
                LogRel(("VBoxHeadless: processEventQueue: %Rrc, termination requested\n", vrc));
                break;
            }

            if (RT_FAILURE(vrc))
            {
                LogRel(("VBoxHeadless: processEventQueue: %Rrc\n", vrc));
                RTMsgError("event loop: %Rrc", vrc);
                break;
            }
        }

        Log(("VBoxHeadless: event loop has terminated...\n"));

#ifdef VBOX_WITH_RECORDING
        if (fRecordEnabled)
        {
            if (!machine.isNull())
            {
                ComPtr<IRecordingSettings> recordingSettings;
                CHECK_ERROR_BREAK(machine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));
                CHECK_ERROR_BREAK(recordingSettings, COMSETTER(Enabled)(FALSE));
            }
        }
#endif /* VBOX_WITH_RECORDING */

        /* we don't have to disable VRDE here because we don't save the settings of the VM */
    }
    while (0);

    /*
     * Get the machine state.
     */
    MachineState_T machineState = MachineState_Aborted;
    if (!machine.isNull())
    {
        hrc = machine->COMGETTER(State)(&machineState);
        if (SUCCEEDED(hrc))
            Log(("machine state = %RU32\n", machineState));
        else
            Log(("IMachine::getState: %Rhrc\n", hrc));
    }
    else
    {
        Log(("machine == NULL\n"));
    }

    /*
     * Turn off the VM if it's running
     */
    if (   gConsole
        && (   machineState == MachineState_Running
            || machineState == MachineState_Teleporting
            || machineState == MachineState_LiveSnapshotting
            /** @todo power off paused VMs too? */
           )
       )
    do
    {
        consoleListener->getWrapped()->ignorePowerOffEvents(true);

        ComPtr<IProgress> pProgress;
        if (!machine.isNull())
            CHECK_ERROR_BREAK(machine, SaveState(pProgress.asOutParam()));
        else
            CHECK_ERROR_BREAK(gConsole, PowerDown(pProgress.asOutParam()));

        hrc = showProgress(pProgress);
        if (FAILED(hrc))
        {
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
                com::GluePrintRCMessage(hrc);
            else
                com::GluePrintErrorInfo(info);
            break;
        }
    } while (0);

    /* No point in trying to post dummy messages to the event queue now. */
    g_fEventQueueSafe = false;

    /* VirtualBox callback unregistration. */
    if (vboxListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(virtualBox, COMGETTER(EventSource)(es.asOutParam()));
        if (!es.isNull())
            CHECK_ERROR(es, UnregisterListener(vboxListener));
        vboxListener.setNull();
    }

    /* Console callback unregistration. */
    if (consoleListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(gConsole, COMGETTER(EventSource)(es.asOutParam()));
        if (!es.isNull())
            CHECK_ERROR(es, UnregisterListener(consoleListener));
        consoleListener.setNull();
    }

    /* VirtualBoxClient callback unregistration. */
    if (vboxClientListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(vboxClientListener));
        vboxClientListener.setNull();
    }

    /* No more access to the 'console' object, which will be uninitialized by the next session->Close call. */
    gConsole = NULL;

    if (fSessionOpened)
    {
        /*
         * Close the session. This will also uninitialize the console and
         * unregister the callback we've registered before.
         */
        Log(("VBoxHeadless: Closing the session...\n"));
        session->UnlockMachine();
    }

    /* Must be before com::Shutdown */
    session.setNull();
    virtualBox.setNull();
    pVirtualBoxClient.setNull();
    machine.setNull();

    com::Shutdown();

#ifdef RT_OS_WINDOWS
    /* tell the session monitor it can ack WM_ENDSESSION */
    if (g_hCanQuit != NIL_RTSEMEVENT)
    {
        RTSemEventSignal(g_hCanQuit);
    }

    /* tell the session monitor to quit */
    if (g_hWindow != NULL)
    {
        ::PostMessage(g_hWindow, WM_QUIT, 0, 0);
    }
#endif

    LogRel(("VBoxHeadless: exiting\n"));
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);
    if (RT_SUCCESS(rc))
        return TrustedMain(argc, argv, envp);
    RTPrintf("VBoxHeadless: Runtime initialization failed: %Rrc - %Rrf\n", rc, rc);
    return RTEXITCODE_FAILURE;
}
#endif /* !VBOX_WITH_HARDENING */
