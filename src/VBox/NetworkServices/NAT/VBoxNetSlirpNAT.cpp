/* $Id: VBoxNetSlirpNAT.cpp 114883 2026-08-07 07:12:24Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxNetNAT - NAT Service for connecting to IntNet.
 */

/*
 * Copyright (C) 2009-2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# include <iprt/win/ws2tcpip.h>
# include <iprt/win/windows.h>
# include "winutils.h"
# define inet_aton(x, y) inet_pton(2, x, y)
# define AF_INET6 23
#else
# include <signal.h>
#endif

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
# include <limits.h>
#endif

#include <VBox/com/assert.h>
#include <VBox/com/com.h>
#include <VBox/com/listeners.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/com/NativeEventQueue.h>

#include <iprt/net.h>
#include <iprt/initterm.h>
#include <iprt/alloca.h>
#ifndef RT_OS_WINDOWS
# include <arpa/inet.h>
#endif
#include <iprt/err.h>
#include <iprt/time.h>
#include <iprt/timer.h>
#include <iprt/thread.h>
#include <iprt/stream.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/req.h>
#include <iprt/file.h>
#include <iprt/semaphore.h>
#include <iprt/cpp/utils.h>
#include <VBox/log.h>

#include <iprt/buildconfig.h>
#include <iprt/getopt.h>
#include <iprt/process.h>

#include <VBox/sup.h>
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/vmm/vmm.h>
#include <VBox/version.h>

#ifndef RT_OS_WINDOWS
# include <sys/poll.h>
# include <sys/socket.h>
# include <netinet/in.h>
# ifdef RT_OS_LINUX
#  include <linux/icmp.h>       /* ICMP_FILTER */
# endif
# include <netinet/icmp6.h>
#endif

#include <map>
#include <vector>
#include <iprt/sanitized/string>

#include <stdio.h>

#include "../NetLib/IntNetIf.h"
#include "../NetLib/VBoxPortForwardString.h"

#include <slirp/libslirp.h>

#ifdef VBOX_RAWSOCK_DEBUG_HELPER
#if    defined(VBOX_WITH_HARDENING) /* obviously */     \
    || defined(RT_OS_WINDOWS)       /* not used */      \
    || defined(RT_OS_DARWIN)        /* not necessary */
# error Have you forgotten to turn off VBOX_RAWSOCK_DEBUG_HELPER?
#endif
/* ask the privileged helper to create a raw socket for us */
extern "C" int getrawsock(int type);
#endif


/** Main event queue interrupted by the host signal / console control handler. */
static com::NativeEventQueue *g_pSignalEventQ = NULL;


/**
 * Interrupts the main native event queue from a process termination signal.
 *
 * @note The event queue pointer may be NULL when signal handling is not fully
 *       installed or after it has been uninstalled.
 */
static void signalInterruptEventQueue(void) RT_NOTHROW_DEF
{
    com::NativeEventQueue *pQueue = g_pSignalEventQ;
    if (pQueue != NULL)
        pQueue->interruptEventQueueProcessing();
}


#ifdef RT_OS_WINDOWS
/**
 * Handles Windows console control notifications for VBoxNetSlirpNAT.
 *
 * @returns TRUE if the console control event was recognized and shutdown was
 *          requested, FALSE if the event should be passed to the next handler.
 * @param   dwCtrlType     Windows console control event identifier.
 *
 * @note This function runs on a Windows console control handler thread.  It
 *       must not perform full NAT teardown directly.
 */
static BOOL WINAPI signalHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    bool fEventHandled = false;
    switch (dwCtrlType)
    {
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            signalInterruptEventQueue();
            fEventHandled = true;
            break;

        default:
            break;
    }

    return fEventHandled;
}
#else
/**
 * Handles POSIX process termination signals for VBoxNetSlirpNAT.
 *
 * @param   iSignal        Signal number delivered by the host OS.
 *
 * The signal value is not currently distinguished.  Any installed signal means
 * that the service should leave the main event loop and shut down cleanly.
 */
static void signalHandler(int iSignal) RT_NOTHROW_DEF
{
    RT_NOREF(iSignal);
    signalInterruptEventQueue();
}
#endif


/**
 * Installs the platform-specific shutdown signal handler.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @param   pEventQ        Main native event queue to interrupt when a shutdown
 *                         signal or console control event is received.
 *
 * The caller must keep @a pEventQ valid until signalHandlerUninstall() has been
 * called.  The installed handler only interrupts the queue; it does not destroy
 * NAT resources itself.
 */
static int signalHandlerInstall(com::NativeEventQueue *pEventQ)
{
    g_pSignalEventQ = pEventQ;

#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)signalHandler, TRUE /* Add handler */))
        return RTErrConvertFromWin32(GetLastError());
#else
    if (   signal(SIGINT,  signalHandler) == SIG_ERR
        || signal(SIGTERM, signalHandler) == SIG_ERR)
        return RTErrConvertFromErrno(errno);
# ifdef SIGBREAK
    if (signal(SIGBREAK, signalHandler) == SIG_ERR)
        return RTErrConvertFromErrno(errno);
# endif
#endif

    return VINF_SUCCESS;
}


/**
 * Uninstalls the platform-specific shutdown signal handler.
 *
 * @returns VBox status code.  If several handler restoration operations fail,
 *          the first failure status is returned.
 * @retval  VINF_SUCCESS on success.
 *
 * This resets the process signal state used by VBoxNetSlirpNAT and clears the
 * saved event queue pointer so later signals cannot access a stale queue.
 */
static int signalHandlerUninstall(void)
{
    int rc = VINF_SUCCESS;

#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)signalHandler, FALSE /* Remove handler */))
        rc = RTErrConvertFromWin32(GetLastError());
#else
    if (signal(SIGINT, SIG_DFL) == SIG_ERR)
        rc = RTErrConvertFromErrno(errno);
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR && RT_SUCCESS(rc))
        rc = RTErrConvertFromErrno(errno);
# ifdef SIGBREAK
    if (signal(SIGBREAK, SIG_DFL) == SIG_ERR && RT_SUCCESS(rc))
        rc = RTErrConvertFromErrno(errno);
# endif
#endif

    g_pSignalEventQ = NULL;
    return rc;
}


/** The maximum (default) poll/WSAPoll timeout. */
#define DRVNAT_DEFAULT_TIMEOUT (int)RT_MS_1HOUR


typedef struct NATSERVICEPORTFORWARDRULE
{
    PORTFORWARDRULE Pfr;
} NATSERVICEPORTFORWARDRULE, *PNATSERVICEPORTFORWARDRULE;

typedef std::vector<NATSERVICEPORTFORWARDRULE> VECNATSERVICEPF;
typedef VECNATSERVICEPF::iterator ITERATORNATSERVICEPF;
typedef VECNATSERVICEPF::const_iterator CITERATORNATSERVICEPF;

/** The following assertions must hold in order to copy addresses around. */
AssertCompile(sizeof(in_addr)  == sizeof(RTNETADDRIPV4));
AssertCompile(sizeof(in6_addr) == sizeof(RTNETADDRIPV6));

/** Slirp Timer */
typedef struct slirpTimer
{
    struct slirpTimer *next;
    /** The time deadline (milliseconds, RTTimeMilliTS).   */
    int64_t msExpire;
    SlirpTimerCb pHandler;
    void *opaque;
} SlirpTimer;


class VBoxNetSlirpNAT
{
    static RTGETOPTDEF s_aGetOptDef[];

    com::Utf8Str m_strNetworkName;
    int m_uVerbosity;

    ComPtr<IVirtualBoxClient> virtualboxClient;
    ComPtr<IVirtualBox> virtualbox;
    ComPtr<IHost> m_host;
    ComPtr<INATNetwork> m_net;

    RTMAC m_MacAddress;
    INTNETIFCTX m_hIf;
    RTTHREAD m_hThrRecv;
    RTTHREAD m_hThrdPoll;
    /** Queue for NAT-thread-external events. */
    RTREQQUEUE              m_hSlirpReqQueue;

    /** Home folder location; used as default directory for several paths. */
    com::Utf8Str m_strHome;

#ifdef RT_OS_WINDOWS
    /** Wakeup socket pair for NAT thread.
     * Entry #0 is write, entry #1 is read. */
    SOCKET                  m_ahWakeupSockPair[2];
#else
    /** The write end of the control pipe. */
    RTPIPE                  m_hPipeWrite;
    /** The read end of the control pipe. */
    RTPIPE                  m_hPipeRead;
#endif

    volatile uint64_t       m_cWakeupNotifs;
    /** Initialization indicator.  */
    volatile bool           m_fInitialized;
    /** Set when service shutdown has been requested. */
    volatile bool           m_fShutdown;

    SlirpConfig m_ProxyOptions;
    struct sockaddr_in m_src4;
    struct sockaddr_in6 m_src6;

    /**
     * Loopback map holders.
     */
    ip4_lomap m_lo2off[10];
    ip4_lomap_desc m_loOptDescriptor;

    uint16_t m_u16Mtu;

    /** Number of valid descriptors in m_paPollFd. */
    unsigned int nsock;

    Slirp *m_pSlirp;

    /** Array of poll descriptors. */
    struct pollfd *m_paPollFd;
    /** Allocated size of m_paPollFd (in entries). */
    unsigned int m_cPollFd = 0;

    /** List of timers (in reverse creation order).
     * @note There is currently only one libslirp timer (v4.8 / 2025-01-16).  */
    SlirpTimer *pTimerHead;
    bool fPassDomain;

    VECNATSERVICEPF m_vecPortForwardRule4;
    VECNATSERVICEPF m_vecPortForwardRule6;

    class Listener
    {
        class Adapter;
        typedef ListenerImpl<Adapter, VBoxNetSlirpNAT *> Impl;

        ComObjPtr<Impl> m_pListenerImpl;
        ComPtr<IEventSource> m_pEventSource;

    public:
        HRESULT init(VBoxNetSlirpNAT *pNAT);
        void uninit();

        template <typename IEventful>
        HRESULT listen(const ComPtr<IEventful> &pEventful,
                       const VBoxEventType_T aEvents[]);
        HRESULT unlisten();

    private:
        HRESULT doListen(const ComPtr<IEventSource> &pEventSource,
                         const VBoxEventType_T aEvents[]);
    };

    Listener m_ListenerNATNet;
    Listener m_ListenerVirtualBox;
    Listener m_ListenerVBoxClient;

public:
    VBoxNetSlirpNAT();
    ~VBoxNetSlirpNAT();

    RTEXITCODE parseArgs(int argc, char *argv[]);

    int init();
    void destroy();
    int run();
    void shutdown();

    static void logInfo(const char *a_pcszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
    static void logError(const char *a_pcszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

private:
    RTEXITCODE usage();

    int initCom();
    int initHome();
    int initLog();
    int initIPv4();
    int initIPv4LoopbackMap();
    int initIPv6();
    int initComEvents();

    int getExtraData(com::Utf8Str &strValueOut, const char *pcszKey);
    void timersRunExpired(void);

    static HRESULT logComError(ComPtr<IUnknown> iface,
                                  const com::Utf8Str &strContext,
                                  HRESULT hrc);
    static void logErrorInfoList(const com::ErrorInfo &info,
                                    const com::Utf8Str &strContext);
    static void logErrorInfo(const com::ErrorInfo &info);

    HRESULT HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent);

    int getHostNameservers(struct in_addr **ppaNameservers, size_t *pcNameservers);
    int getHostNameservers6(struct in6_addr **ppaNameservers, size_t *pcNameservers);

    int slirpTimersAdjustTimeoutDown(int cMsTimeout);
    void slirpNotifyPollThread(const char *pszWho);

    int stopWorkerThreads(bool fWaitReceiveThread);

    int fetchNatPortForwardRules(VECNATSERVICEPF &vec, bool fIsIPv6);
    int natServiceProcessRegisteredPf(VECNATSERVICEPF &vecPf);

    static DECLCALLBACK(void) natServicePfRegister(VBoxNetSlirpNAT *pThis, PPORTFORWARDRULE pNatPf, bool fRemove, bool fRuntime);
    static DECLCALLBACK(int) pollThread(RTTHREAD hThreadSelf, void *pvUser);
    static DECLCALLBACK(int) receiveThread(RTTHREAD hThreadSelf, void *pvUser);

    /* input from intnet */
    static DECLCALLBACK(void) processFrame(void *pvUser, void *pvFrame, uint32_t cbFrame);

    static ssize_t slirpSendPacketCb(const void *pvBuf, ssize_t cb, void *pvUser) RT_NOTHROW_PROTO;
    static void    slirpGuestErrorCb(const char *pszMsg, void *pvUser) RT_NOTHROW_PROTO;
    static int64_t slirpClockGetNsCb(void *pvUser) RT_NOTHROW_PROTO;
    static void   *slirpTimerNewCb(SlirpTimerCb slirpTimeCb, void *cb_opaque, void *opaque) RT_NOTHROW_PROTO;
    static void    slirpTimerFreeCb(void *pvTimer, void *pvUser) RT_NOTHROW_PROTO;
    static void    slirpTimerModCb(void *pvTimer, int64_t msNewDeadlineTs, void *pvUser) RT_NOTHROW_PROTO;
    static void    slirpNotifyCb(void *opaque) RT_NOTHROW_PROTO;
    static void    slirpRegisterPoll(slirp_os_socket socket, void *opaque) RT_NOTHROW_PROTO;
    static void    slirpUnregisterPoll(slirp_os_socket socket, void *opaque) RT_NOTHROW_PROTO;
    static int     slirpAddPollCb(slirp_os_socket hFd, int iEvents, void *opaque) RT_NOTHROW_PROTO;
    static int     slirpGetREventsCb(int idx, void *opaque) RT_NOTHROW_PROTO;

    static DECLCALLBACK(void) slirpSendWorker(VBoxNetSlirpNAT *pThis, void *pvFrame, size_t cbFrame);
};



VBoxNetSlirpNAT::VBoxNetSlirpNAT()
  : m_uVerbosity(0),
    m_hIf(NULL),
    m_hThrRecv(NIL_RTTHREAD),
    m_hThrdPoll(NIL_RTTHREAD),
    m_hSlirpReqQueue(NIL_RTREQQUEUE),
#ifndef RT_OS_WINDOWS
    m_hPipeWrite(NIL_RTPIPE),
    m_hPipeRead(NIL_RTPIPE),
#endif
    m_cWakeupNotifs(0),
    m_fInitialized(false),
    m_fShutdown(false),
    m_u16Mtu(1500),
    m_pSlirp(NULL),
    fPassDomain(false)
{
    LogFlowFuncEnter();

#ifdef RT_OS_WINDOWS
    m_ahWakeupSockPair[0] = INVALID_SOCKET;
    m_ahWakeupSockPair[1] = INVALID_SOCKET;
#endif

    RT_ZERO(m_ProxyOptions);

    m_ProxyOptions.version = 6;
    m_ProxyOptions.restricted   = false;
    m_ProxyOptions.in_enabled   = true;
    m_ProxyOptions.if_mtu       = m_u16Mtu;
    m_ProxyOptions.disable_dhcp = true;
    m_ProxyOptions.disable_host_loopback = false;
    m_ProxyOptions.disable_dns = false;
    m_ProxyOptions.iSoMaxConn = 10;

    RT_ZERO(m_src4);
    RT_ZERO(m_src6);
    m_src4.sin_family = AF_INET;
    m_src6.sin6_family = AF_INET6;
#ifdef HAVE_SA_LEN
    m_src4.sin_len = sizeof(m_src4);
    m_src6.sin6_len = sizeof(m_src6);
#endif

    pTimerHead = NULL;
    nsock      = 0;

    m_cPollFd   = 64;
    m_paPollFd  = (struct pollfd *)RTMemAllocZ(m_cPollFd * sizeof(struct pollfd));
    if (!m_paPollFd)
        throw VERR_NO_MEMORY;

    m_MacAddress.au8[0] = 0x52;
    m_MacAddress.au8[1] = 0x54;
    m_MacAddress.au8[2] = 0;
    m_MacAddress.au8[3] = 0x12;
    m_MacAddress.au8[4] = 0x35;
    m_MacAddress.au8[5] = 0;

    LogFlowFuncLeave();
}


VBoxNetSlirpNAT::~VBoxNetSlirpNAT()
{
    destroy();
}


/**
 * Command line options.
 */
RTGETOPTDEF VBoxNetSlirpNAT::s_aGetOptDef[] =
{
    { "--network",              'n',   RTGETOPT_REQ_STRING },
    { "--verbose",              'v',   RTGETOPT_REQ_NOTHING },
};


/** Icky hack to tell the caller it should exit with RTEXITCODE_SUCCESS */
#define RTEXITCODE_DONE RTEXITCODE_32BIT_HACK

RTEXITCODE
VBoxNetSlirpNAT::usage()
{
    RTPrintf("%s Version %sr%u\n"
             "Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
             "\n"
             "Usage: %s <options>\n"
             "\n"
             "Options:\n",
             RTProcShortName(), RTBldCfgVersion(), RTBldCfgRevision(),
             RTProcShortName());
    for (size_t i = 0; i < RT_ELEMENTS(s_aGetOptDef); ++i)
        RTPrintf("    -%c, %s\n", s_aGetOptDef[i].iShort, s_aGetOptDef[i].pszLong);

    return RTEXITCODE_DONE;
}


RTEXITCODE
VBoxNetSlirpNAT::parseArgs(int argc, char *argv[])
{
    unsigned int uVerbosity = 0;

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, argc, argv,
                          s_aGetOptDef, RT_ELEMENTS(s_aGetOptDef),
                          1, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&State, &Val)) != 0)
    {
        switch (ch)
        {
            case 'n':           /* --network */
                if (m_strNetworkName.isNotEmpty())
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Multiple --network options");
                m_strNetworkName = Val.psz;
                break;

            case 'v':           /* --verbose */
                ++uVerbosity;
                break;


            /*
             * Standard options recognized by RTGetOpt()
             */

            case 'V':           /* --version */
                RTPrintf("%sr%u\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_DONE;

            case 'h':           /* --help */
                return usage();

            case VINF_GETOPT_NOT_OPTION:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unexpected non-option argument");

            default:
                return RTGetOptPrintError(ch, &Val);
        }
    }

    if (m_strNetworkName.isEmpty())
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --network option");

    m_uVerbosity = uVerbosity;
    return RTEXITCODE_SUCCESS;
}


/**
 * Perform actual initialization.
 *
 * This code runs on the main thread.  Establish COM connection with
 * VBoxSVC so that we can do API calls.  Starts the LWIP thread.
 */
int VBoxNetSlirpNAT::init()
{
    HRESULT hrc;
    int rc;

    /* Get the COM API set up. */
    rc = initCom();
    if (RT_FAILURE(rc))
    {
        logError("Initializing COM failed, rc=%Rrc", rc);
        return rc;
    }

    /* Get the home folder location.  It's ok if it fails. */
    rc = initHome();
    if (RT_FAILURE(rc))
        logError("Continuing after home folder lookup failed, rc=%Rrc", rc);

#ifdef RT_OS_WINDOWS
    WSADATA WsaData = { 0 };
    int err = WSAStartup(MAKEWORD(2,2), &WsaData);
    if (err)
    {
        logError("WSAStartup() failed with %d\n", err);
        return VERR_NET_INIT_FAILED;
    }
#endif

    /*
     * We get the network name on the command line.  Get hold of its
     * API object to get the rest of the configuration from.
     */
    hrc = virtualbox->FindNATNetworkByName(com::Bstr(m_strNetworkName).raw(),
                                           m_net.asOutParam());
    if (FAILED(hrc))
    {
        logComError(virtualbox, "FindNATNetworkByName", hrc);
        logError("Initialization failed: NAT network \"%s\" was not found", m_strNetworkName.c_str());
        return VERR_NOT_FOUND;
    }

    /*
     * Now that we know the network name and have ensured that it
     * indeed exists we can create the release log file.
     */
    rc = initLog();
    if (RT_FAILURE(rc))
        logError("Continuing after release logger initialization failed, rc=%Rrc", rc);

    // resolver changes are reported on vbox but are retrieved from
    // host so stash a pointer for future lookups
    hrc = virtualbox->COMGETTER(Host)(m_host.asOutParam());
    if (FAILED(hrc))
        logError("Initialization failed: host object lookup failed, hrc=%Rhrc", hrc);
    AssertComRCReturn(hrc, VERR_INTERNAL_ERROR);

    /* Get the settings related to IPv4. */
    rc = initIPv4();
    if (RT_FAILURE(rc))
    {
        logError("Initialization failed: IPv4 setup failed, rc=%Rrc", rc);
        return rc;
    }

    /* Get the settings related to IPv6. */
    rc = initIPv6();
    if (RT_FAILURE(rc))
    {
        logError("Initialization failed: IPv6 setup failed, rc=%Rrc", rc);
        return rc;
    }

    if (m_strHome.isNotEmpty())
    {
        com::Utf8StrFmt strTftpRoot("%s%c%s", m_strHome.c_str(), RTPATH_DELIMITER, "TFTP");
        char *pszStrTemp;       // avoid const char ** vs char **
        rc = RTStrUtf8ToCurrentCP(&pszStrTemp, strTftpRoot.c_str());
        if (RT_FAILURE(rc))
            logError("Initialization failed: TFTP root path conversion failed, rc=%Rrc", rc);
        AssertLogRelRCReturn(rc, rc);
        m_ProxyOptions.tftp_path = pszStrTemp;
    }

    static SlirpCb slirpCallbacks = { 0 };

    slirpCallbacks.send_packet            = slirpSendPacketCb;
    slirpCallbacks.guest_error            = slirpGuestErrorCb;
    slirpCallbacks.clock_get_ns           = slirpClockGetNsCb;
    slirpCallbacks.timer_new              = slirpTimerNewCb;
    slirpCallbacks.timer_free             = slirpTimerFreeCb;
    slirpCallbacks.timer_mod              = slirpTimerModCb;
    slirpCallbacks.notify                 = slirpNotifyCb;
    slirpCallbacks.init_completed         = NULL;
    slirpCallbacks.timer_new_opaque       = NULL;
    slirpCallbacks.register_poll_socket   = slirpRegisterPoll;
    slirpCallbacks.unregister_poll_socket = slirpUnregisterPoll;

    /*
     * Initialize Slirp
     */
    m_pSlirp = slirp_new(/* cfg */ &m_ProxyOptions, /* callbacks */ &slirpCallbacks, /* opaque */ this);
    if (!m_pSlirp)
    {
        logError("Initialization failed: libslirp setup failed, rc=%Rrc", VERR_NO_MEMORY);
        return VERR_NO_MEMORY;
    }

    rc = initComEvents();
    if (RT_FAILURE(rc))
        logError("Initialization failed: COM event listener setup failed, rc=%Rrc", rc);
    AssertLogRelRCReturn(rc, rc);
    /* end of COM initialization */

    rc = RTReqQueueCreate(&m_hSlirpReqQueue);
    if (RT_FAILURE(rc))
        logError("Initialization failed: request queue creation failed, rc=%Rrc", rc);
    AssertLogRelRCReturn(rc, rc);

#ifdef RT_OS_WINDOWS
    /* Create the wakeup socket pair (idx=0 is write, idx=1 is read). */
    m_ahWakeupSockPair[0] = INVALID_SOCKET;
    m_ahWakeupSockPair[1] = INVALID_SOCKET;
    rc = RTWinSocketPair(AF_INET, SOCK_DGRAM, 0, m_ahWakeupSockPair);
    if (RT_FAILURE(rc))
        logError("Initialization failed: wakeup socket pair creation failed, rc=%Rrc", rc);
    AssertLogRelRCReturn(rc, rc);

#else
    /* Create the control pipe. */
    rc = RTPipeCreate(&m_hPipeRead, &m_hPipeWrite, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        logError("Initialization failed: wakeup pipe creation failed, rc=%Rrc", rc);
    AssertLogRelRCReturn(rc, rc);
#endif

    /* connect to the intnet */
    rc = IntNetR3IfCreate(&m_hIf, m_strNetworkName.c_str());
    if (RT_FAILURE(rc))
        logError("Initialization failed: internal network interface creation failed, rc=%Rrc", rc);
    if (RT_SUCCESS(rc))
    {
        rc = IntNetR3IfSetActive(m_hIf, true /*fActive*/);
        if (RT_FAILURE(rc))
            logError("Initialization failed: internal network interface activation failed, rc=%Rrc", rc);
    }

    if (RT_SUCCESS(rc))
    {
        m_fInitialized = true;
        logInfo("Initialization completed\n");
    }
    else
        logError("Initialization failed with rc=%Rrc", rc);

    return rc;
}


/**
 * Stops worker activity and releases resources acquired by init().
 *
 * The method requests shutdown, wakes the polling and receive paths, waits for
 * worker threads to exit, tears down libslirp and the internal-network
 * interface, closes the wakeup descriptors, destroys the request queue, and
 * frees dynamically allocated configuration data.
 *
 * It is safe to call this more than once.  Calls made before successful
 * initialization, or after resources have already been released, return without
 * doing any work.
 */
void VBoxNetSlirpNAT::destroy()
{
    if (!ASMAtomicReadBool(&m_fInitialized))
        return;

    logInfo("Destroying NAT network \"%s\"\n", m_strNetworkName.c_str());

    ASMAtomicWriteBool(&m_fShutdown, true);
    if (m_hIf != NULL)
        IntNetR3IfWaitAbort(m_hIf);
    if (m_hThrdPoll != NIL_RTTHREAD)
        slirpNotifyPollThread("destroy");

    if (m_hThrRecv != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_hThrRecv, RT_INDEFINITE_WAIT, NULL);
        if (RT_FAILURE(rc))
            logError("Destruction failed: receive thread wait failed, rc=%Rrc", rc);
        AssertRC(rc);
        m_hThrRecv = NIL_RTTHREAD;
    }

    if (m_hThrdPoll != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_hThrdPoll, RT_INDEFINITE_WAIT, NULL);
        if (RT_FAILURE(rc))
            logError("Destruction failed: poll thread wait failed, rc=%Rrc", rc);
        AssertRC(rc);
        m_hThrdPoll = NIL_RTTHREAD;
    }

    if (m_pSlirp != NULL)
    {
        logInfo("Destroying libslirp instance\n");
        slirp_cleanup(m_pSlirp);
        m_pSlirp = NULL;
    }

    if (m_hIf != NULL)
    {
        int rc = IntNetR3IfDestroy(m_hIf);
        if (RT_FAILURE(rc))
            logError("Destruction failed: internal network interface destruction failed, rc=%Rrc", rc);
        AssertRC(rc);
        m_hIf = NULL;
    }

#ifdef RT_OS_WINDOWS
    for (unsigned i = 0; i < RT_ELEMENTS(m_ahWakeupSockPair); ++i)
        if (m_ahWakeupSockPair[i] != INVALID_SOCKET)
        {
            int rc = closesocket(m_ahWakeupSockPair[i]);
            if (rc != 0)
                logError("Destruction failed: wakeup socket close failed, rc=%d", rc);
            Assert(rc == 0);
            RT_NOREF(rc);
            m_ahWakeupSockPair[i] = INVALID_SOCKET;
        }
#else
    if (m_hPipeRead != NIL_RTPIPE)
    {
        int rc = RTPipeClose(m_hPipeRead);
        if (RT_FAILURE(rc))
            logError("Destruction failed: wakeup pipe read end close failed, rc=%Rrc", rc);
        AssertRC(rc);
        m_hPipeRead = NIL_RTPIPE;
    }
    if (m_hPipeWrite != NIL_RTPIPE)
    {
        int rc = RTPipeClose(m_hPipeWrite);
        if (RT_FAILURE(rc))
            logError("Destruction failed: wakeup pipe write end close failed, rc=%Rrc", rc);
        AssertRC(rc);
        m_hPipeWrite = NIL_RTPIPE;
    }
#endif

    int rc = RTReqQueueDestroy(m_hSlirpReqQueue);
    if (RT_FAILURE(rc))
        logError("Destruction failed: request queue destruction failed, rc=%Rrc", rc);
    AssertRC(rc);
    m_hSlirpReqQueue = NIL_RTREQQUEUE;

    if (m_ProxyOptions.outbound_addr)
    {
        RTMemFree(m_ProxyOptions.outbound_addr);
        m_ProxyOptions.outbound_addr = NULL;
    }

    RTMemFree(m_paPollFd);
    m_paPollFd = NULL;
    m_cPollFd  = 0;

    ASMAtomicWriteBool(&m_fInitialized, false);

    logInfo("Destruction completed\n");
}


/**
 * Primary COM initialization performed on the main thread.
 *
 * This initializes COM and obtains VirtualBox Client and VirtualBox
 * objects.
 *
 * @note The member variables for them are in the base class.  We
 * currently do it here so that we can report errors properly, because
 * the base class' VBoxNetBaseService::init() is a bit naive and
 * fixing that would just create unnecessary churn for little
 * immediate gain.  It's easier to ignore the base class code and do
 * it ourselves and do the refactoring later.
 */
int VBoxNetSlirpNAT::initCom()
{
    HRESULT hrc;

    hrc = com::Initialize();
    if (FAILED(hrc))
    {
#ifdef VBOX_WITH_XPCOM
        if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
        {
            char szHome[RTPATH_MAX] = "";
            int vrc = com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome), false);
            if (RT_SUCCESS(vrc))
            {
                return RTMsgErrorExit(RTEXITCODE_INIT,
                                      "Failed to initialize COM: %s: %Rhrf",
                                      szHome, hrc);
            }
        }
#endif  /* VBOX_WITH_XPCOM */
        return RTMsgErrorExit(RTEXITCODE_INIT,
                              "Failed to initialize COM: %Rhrf", hrc);
    }

    hrc = virtualboxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (FAILED(hrc))
    {
        logError("Failed to create VirtualBox Client object: %Rhra", hrc);
        return VERR_GENERAL_FAILURE;
    }

    hrc = virtualboxClient->COMGETTER(VirtualBox)(virtualbox.asOutParam());
    if (FAILED(hrc))
    {
        logError("Failed to obtain VirtualBox object: %Rhra", hrc);
        return VERR_GENERAL_FAILURE;
    }

    return VINF_SUCCESS;
}


/**
 * Get the VirtualBox home folder.
 *
 * It is used as the base directory for the default release log file
 * and for the TFTP root location.
 */
int VBoxNetSlirpNAT::initHome()
{
    HRESULT hrc;
    int rc;

    com::Bstr bstrHome;
    hrc = virtualbox->COMGETTER(HomeFolder)(bstrHome.asOutParam());
    if (SUCCEEDED(hrc))
    {
        m_strHome = bstrHome;
        return VINF_SUCCESS;
    }

    /*
     * In the unlikely event that we have failed to retrieve
     * HomeFolder via the API, try the fallback method.  Note that
     * despite "com" namespace it does not use COM.
     */
    char szHome[RTPATH_MAX] = "";
    rc = com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome), false);
    if (RT_SUCCESS(rc))
    {
        m_strHome = szHome;
        return VINF_SUCCESS;
    }

    return rc;
}


/*
 * Read IPv4 related settings and do necessary initialization.  These
 * settings will be picked up by the proxy on the lwIP thread.  See
 * onLwipTcpIpInit().
 */
int VBoxNetSlirpNAT::initIPv4()
{
    HRESULT hrc;
    int rc;

    AssertReturn(m_net.isNotNull(), VERR_GENERAL_FAILURE);

    /*
     * IPv4 address and mask.
     */
    com::Bstr bstrIPv4Prefix;
    hrc = m_net->COMGETTER(Network)(bstrIPv4Prefix.asOutParam());
    if (FAILED(hrc))
    {
        logComError(m_net, "Network", hrc);
        return VERR_GENERAL_FAILURE;
    }

    RTNETADDRIPV4 Net4, Mask4;
    int iPrefixLength;
    rc = RTNetStrToIPv4Cidr(com::Utf8Str(bstrIPv4Prefix).c_str(),
                            &Net4, &iPrefixLength);
    if (RT_FAILURE(rc))
    {
        logError("Failed to parse IPv4 prefix %ls\n", bstrIPv4Prefix.raw());
        return rc;
    }

    if (iPrefixLength > 30 || 0 >= iPrefixLength)
    {
        logError("Invalid IPv4 prefix length %d\n", iPrefixLength);
        return VERR_INVALID_PARAMETER;
    }

    rc = RTNetPrefixToMaskIPv4(iPrefixLength, &Mask4);
    AssertRCReturn(rc, rc);

    /** @todo r=uwe Check the address is unicast, not a loopback, etc. */

    RTNETADDRIPV4 Addr4;
    Addr4.u = Net4.u | RT_H2N_U32_C(0x00000001);

    memcpy(&m_ProxyOptions.vnetwork, &Net4, sizeof(RTNETADDRIPV4));
    memcpy(&m_ProxyOptions.vnetmask, &Mask4, sizeof(RTNETADDRIPV4));
    memcpy(&m_ProxyOptions.vhost,    &Addr4, sizeof(RTNETADDRIPV4));

    /*
     * IPv4 Nameservers
     */
    size_t cRealNameservers = 0;
    rc = getHostNameservers(&m_ProxyOptions.aRealNameservers, &cRealNameservers);
    if (RT_FAILURE(rc))
        return rc;
    m_ProxyOptions.cRealNameservers = cRealNameservers;
    if (cRealNameservers > 0)
    {
        logInfo("Transferred %zu nameservers to NAT engine.\n", cRealNameservers);
    }
    else
    {
        logInfo("No usable host IPv4 nameserver available; using libslirp DNS proxy\n");

        RTNETADDRIPV4 Nameserver4;
        Nameserver4.u = Net4.u | RT_H2N_U32_C(0x00000003);

        memcpy(&m_ProxyOptions.vnameserver, &Nameserver4, sizeof(in_addr));
        m_ProxyOptions.cRealNameservers = 0; /* Ensures libslirp uses fallback. */
        logInfo("Using fallback virtual nameserver: %RTnaipv4\n", Nameserver4.u);
    }

    rc = fetchNatPortForwardRules(m_vecPortForwardRule4, /* :fIsIPv6 */ false);
    AssertLogRelRCReturn(rc, rc);

    /* IPv4 source address (host), if configured. */
    com::Utf8Str strSourceIp4;
    rc = getExtraData(strSourceIp4, "SourceIp4");
    if (RT_SUCCESS(rc) && strSourceIp4.isNotEmpty())
    {
        RTNETADDRIPV4 addr;
        rc = RTNetStrToIPv4Addr(strSourceIp4.c_str(), &addr);
        if (RT_SUCCESS(rc))
        {
            m_ProxyOptions.outbound_addr = (struct sockaddr_in *)RTMemAlloc(sizeof(struct sockaddr_in));
            m_ProxyOptions.outbound_addr->sin_addr.s_addr = addr.u;
            m_ProxyOptions.outbound_addr->sin_family = AF_INET;
            m_ProxyOptions.outbound_addr->sin_port = 0;

            logInfo("Will use %RTnaipv4 as IPv4 source address\n",
                    m_src4.sin_addr.s_addr);
        }
        else
        {
            logError("Failed to parse IPv4 source address specification \"%s\"", strSourceIp4.c_str());
        }
    }

    /* Make host's loopback(s) available from inside the natnet */
    initIPv4LoopbackMap();

    return VINF_SUCCESS;
}

/**
 * Init mapping from the natnet's IPv4 addresses to host's IPv4
 * loopbacks.  Plural "loopbacks" because it's now quite common to run
 * services on loopback addresses other than 127.0.0.1.  E.g. a
 * caching dns proxy on 127.0.1.1 or 127.0.0.53.
 */
int VBoxNetSlirpNAT::initIPv4LoopbackMap()
{
    HRESULT hrc;
    int rc;

    com::SafeArray<BSTR> aStrLocalMappings;
    hrc = m_net->COMGETTER(LocalMappings)(ComSafeArrayAsOutParam(aStrLocalMappings));
    if (FAILED(hrc))
    {
        logComError(m_net, "LocalMappings", hrc);
        return VERR_GENERAL_FAILURE;
    }

    if (aStrLocalMappings.size() == 0)
        return VINF_SUCCESS;


    /* netmask in host order, to verify the offsets */
    uint32_t uMask = RT_N2H_U32(m_ProxyOptions.vnetmask.s_addr);

    /*
     * Process mappings of the form "127.x.y.z=off"
     */
    unsigned int dst = 0;      /* typeof(ip4_lomap_desc::num_lomap) */
    for (size_t i = 0; i < aStrLocalMappings.size(); ++i)
    {
        com::Utf8Str strMapping(aStrLocalMappings[i]);
        const char *pcszRule = strMapping.c_str();
        logInfo("IPv4 loopback mapping %zu: %s\n", i, pcszRule);

        RTNETADDRIPV4 Loopback4;
        char *pszNext;
        rc = RTNetStrToIPv4AddrEx(pcszRule, &Loopback4, &pszNext);
        if (RT_FAILURE(rc))
        {
            logError("Failed to parse IPv4 address, rc=%Rra", rc);
            continue;
        }

        if (Loopback4.au8[0] != 127)
        {
            logInfo("Not an IPv4 loopback address\n");
            continue;
        }

        if (rc != VWRN_TRAILING_CHARS)
        {
            logInfo("Missing right hand side\n");
            continue;
        }

        pcszRule = RTStrStripL(pszNext);
        if (*pcszRule != '=')
        {
            logError("Invalid rule format");
            continue;
        }

        pcszRule = RTStrStripL(pcszRule+1);
        if (*pszNext == '\0')
        {
            logInfo("Empty right hand side\n");
            continue;
        }

        uint32_t u32Offset;
        rc = RTStrToUInt32Ex(pcszRule, &pszNext, 10, &u32Offset);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        {
            logError("Invalid offset");
            continue;
        }

        if (u32Offset <= 1 || u32Offset == ~uMask)
        {
            logInfo("Offset maps to a reserved address\n");
            continue;
        }

        if ((u32Offset & uMask) != 0)
        {
            logInfo("Offset exceeds the network size\n");
            continue;
        }

        if (dst >= RT_ELEMENTS(m_lo2off))
        {
            logInfo("Ignoring the mapping, too many mappings already\n");
            continue;
        }

        m_lo2off[dst].loaddr = Loopback4.u;
        m_lo2off[dst].off = u32Offset;
        ++dst;
    }

    if (dst > 0)
    {
        m_loOptDescriptor.lomap = m_lo2off;
        m_loOptDescriptor.num_lomap = dst;
        m_ProxyOptions.mLoopbackMap = &m_loOptDescriptor;
    }

    return VINF_SUCCESS;
}

/*
 * Read IPv6 related settings and do necessary initialization.  These
 * settings will be picked up by the proxy on the lwIP thread.  See
 * onLwipTcpIpInit().
 */
int VBoxNetSlirpNAT::initIPv6()
{
    HRESULT hrc;
    int rc;

    AssertReturn(m_net.isNotNull(), VERR_GENERAL_FAILURE);

    /* Is IPv6 enabled for this network at all? */
    BOOL fIPv6Enabled = FALSE;
    hrc = m_net->COMGETTER(IPv6Enabled)(&fIPv6Enabled);
    if (FAILED(hrc))
    {
        logComError(m_net, "IPv6Enabled", hrc);
        return VERR_GENERAL_FAILURE;
    }

    m_ProxyOptions.in6_enabled = !!fIPv6Enabled;
    if (!fIPv6Enabled)
        return VINF_SUCCESS;


    /*
     * IPv6 address.
     */
    com::Bstr bstrIPv6Prefix;
    hrc = m_net->COMGETTER(IPv6Prefix)(bstrIPv6Prefix.asOutParam());
    if (FAILED(hrc))
    {
        logComError(m_net, "IPv6Prefix", hrc);
        return VERR_GENERAL_FAILURE;
    }

    RTNETADDRIPV6 Net6;
    int iPrefixLength;
    rc = RTNetStrToIPv6Cidr(com::Utf8Str(bstrIPv6Prefix).c_str(),
                            &Net6, &iPrefixLength);
    if (RT_FAILURE(rc))
    {
        logError("Failed to parse IPv6 prefix %ls\n", bstrIPv6Prefix.raw());
        return rc;
    }

    /* Allow both addr:: and addr::/64 */
    if (iPrefixLength == 128)   /* no length was specified after the address? */
        iPrefixLength = 64;     /*   take it to mean /64 which we require anyway */
    else if (iPrefixLength != 64)
    {
        logError("Invalid IPv6 prefix length %d,"
                    " must be 64.\n", iPrefixLength);
        return rc;
    }

    /* Verify the address is unicast. */
    if (   ((Net6.au8[0] & 0xe0) != 0x20)  /* global 2000::/3 */
        && ((Net6.au8[0] & 0xfe) != 0xfc)) /* local  fc00::/7 */
    {
        logError("IPv6 prefix %RTnaipv6 is not unicast.\n", &Net6);
        return VERR_INVALID_PARAMETER;
    }

    /* Verify the interfaces ID part is zero */
    if (Net6.au64[1] != 0)
    {
        logError("Non-zero bits in the interface ID part"
                    " of the IPv6 prefix %RTnaipv6/64.\n", &Net6);
        return VERR_INVALID_PARAMETER;
    }

    m_ProxyOptions.vprefix_len = iPrefixLength;
    memcpy(&m_ProxyOptions.vprefix_addr6, &Net6, sizeof(RTNETADDRIPV6));

    /* Use ...::1 as our address */
    RTNETADDRIPV6 Addr6 = Net6;
    Addr6.au8[15] = 0x01;
    memcpy(&m_ProxyOptions.vhost6, &Addr6, sizeof(RTNETADDRIPV6));

    /** @todo Verify DNS server default. */
    Addr6.au8[15] = 0x03;
    memcpy(&m_ProxyOptions.vnameserver6, &Addr6, sizeof(RTNETADDRIPV6));

    size_t cRealNameservers6 = 0;
    rc = getHostNameservers6(&m_ProxyOptions.aIPv6RealNameservers, &cRealNameservers6);
    if (RT_FAILURE(rc))
        return rc;
    m_ProxyOptions.cIPv6RealNameservers = cRealNameservers6;
    if (cRealNameservers6 > 0)
    {
        logInfo("Transfered %u IPv6 nameservers to NAT engine.\n", (unsigned)cRealNameservers6);
    }
    else
        logInfo("No IPv6 nameservers available from the host, using the virtual IPv6 DNS proxy address.\n");

    /*
     * Should we advertise ourselves as default IPv6 route?  If the
     * host doesn't have IPv6 connectivity, it's probably better not
     * to, to prevent the guest from IPv6 connection attempts doomed
     * to fail.
     *
     * We might want to make this modifiable while the natnet is
     * running.
     */
    BOOL fIPv6DefaultRoute = FALSE;
    hrc = m_net->COMGETTER(AdvertiseDefaultIPv6RouteEnabled)(&fIPv6DefaultRoute);
    if (FAILED(hrc))
    {
        logComError(m_net, "AdvertiseDefaultIPv6RouteEnabled", hrc);
        return VERR_GENERAL_FAILURE;
    }

    /* Keep sending RAs for SLAAC/RDNSS, but suppress the default route when requested. */
    m_ProxyOptions.fDisableIPv6RA = !fIPv6DefaultRoute;

    rc = fetchNatPortForwardRules(m_vecPortForwardRule6, /* :fIsIPv6 */ true);
    AssertLogRelRCReturn(rc, rc);

    /* IPv6 source address (host), if configured. */
    com::Utf8Str strSourceIp6;
    rc = getExtraData(strSourceIp6, "SourceIp6");
    if (RT_SUCCESS(rc) && strSourceIp6.isNotEmpty())
    {
        RTNETADDRIPV6 addr;
        char *pszZone = NULL;
        rc = RTNetStrToIPv6Addr(strSourceIp6.c_str(), &addr, &pszZone);
        if (RT_SUCCESS(rc))
        {
            memcpy(&m_ProxyOptions.outbound_addr6->sin6_addr.s6_addr, &addr, sizeof(uint128_t));

            logInfo("Will use %RTnaipv6 as IPv6 source address\n",
                    m_src6.sin6_addr.s6_addr);
        }
        else
        {
            logError("Failed to parse IPv6 source address specification \"%s\"", strSourceIp6.c_str());
        }
    }

    return VINF_SUCCESS;
}

/**
 * Adapter for the ListenerImpl template.  It has to be a separate
 * object because ListenerImpl deletes it.  Just a small wrapper that
 * delegates the real work back to VBoxNetSlirpNAT.
 */
class VBoxNetSlirpNAT::Listener::Adapter
{
    VBoxNetSlirpNAT *m_pNAT;
public:
    Adapter() : m_pNAT(NULL) {}
    HRESULT init() { return init(NULL); }
    void uninit() { m_pNAT = NULL; }

    HRESULT init(VBoxNetSlirpNAT *pNAT)
    {
        m_pNAT = pNAT;
        return S_OK;
    }

    HRESULT HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent)
    {
        if (RT_LIKELY(m_pNAT != NULL))
            return m_pNAT->HandleEvent(aEventType, pEvent);
        else
            return S_OK;
    }
};


HRESULT
VBoxNetSlirpNAT::Listener::init(VBoxNetSlirpNAT *pNAT)
{
    HRESULT hrc;

    hrc = m_pListenerImpl.createObject();
    if (FAILED(hrc))
        return hrc;

    hrc = m_pListenerImpl->init(new Adapter(), pNAT);
    if (FAILED(hrc))
    {
        VBoxNetSlirpNAT::logComError(m_pListenerImpl, "Init", hrc);
        return hrc;
    }

    return hrc;
}


void
VBoxNetSlirpNAT::Listener::uninit()
{
    unlisten();
    m_pListenerImpl.setNull();
}


/*
 * There's no base interface that exposes "eventSource" so fake it
 * with a template.
 */
template <typename IEventful>
HRESULT
VBoxNetSlirpNAT::Listener::listen(const ComPtr<IEventful> &pEventful,
                                         const VBoxEventType_T aEvents[])
{
    HRESULT hrc;

    if (m_pListenerImpl.isNull())
        return S_OK;

    ComPtr<IEventSource> pEventSource;
    hrc = pEventful->COMGETTER(EventSource)(pEventSource.asOutParam());
    if (FAILED(hrc))
    {
        VBoxNetSlirpNAT::logComError(pEventful, "EventSource", hrc);
        return hrc;
    }

    /* got a real interface, punt to the non-template code */
    hrc = doListen(pEventSource, aEvents);
    if (FAILED(hrc))
        return hrc;

    return hrc;
}


HRESULT
VBoxNetSlirpNAT::Listener::doListen(const ComPtr<IEventSource> &pEventSource,
                                   const VBoxEventType_T aEvents[])
{
    HRESULT hrc;

    com::SafeArray<VBoxEventType_T> aInteresting;
    for (size_t i = 0; aEvents[i] != VBoxEventType_Invalid; ++i)
        aInteresting.push_back(aEvents[i]);

    BOOL fActive = true;
    hrc = pEventSource->RegisterListener(m_pListenerImpl,
                                         ComSafeArrayAsInParam(aInteresting),
                                         fActive);
    if (FAILED(hrc))
    {
        VBoxNetSlirpNAT::logComError(m_pEventSource, "RegisterListener", hrc);
        return hrc;
    }

    m_pEventSource = pEventSource;
    return hrc;
}


HRESULT
VBoxNetSlirpNAT::Listener::unlisten()
{
    HRESULT hrc;

    if (m_pEventSource.isNull())
        return S_OK;

    const ComPtr<IEventSource> pEventSource = m_pEventSource;
    m_pEventSource.setNull();

    hrc = pEventSource->UnregisterListener(m_pListenerImpl);
    if (FAILED(hrc))
    {
        VBoxNetSlirpNAT::logComError(pEventSource, "UnregisterListener", hrc);
        return hrc;
    }

    return hrc;
}



/**
 * Create and register API event listeners.
 */
int VBoxNetSlirpNAT::initComEvents()
{
    /**
     * @todo r=uwe These events are reported on both IVirtualBox and
     * INATNetwork objects.  We used to listen for them on our
     * network, but it was changed later to listen on vbox.  Leave it
     * that way for now.  Note that HandleEvent() has to do additional
     * check for them to ignore events for other networks.
     */
    static const VBoxEventType_T s_aNATNetEvents[] = {
        VBoxEventType_OnNATNetworkPortForward,
        VBoxEventType_OnNATNetworkSetting,
        VBoxEventType_Invalid
    };
    m_ListenerNATNet.init(this);
    m_ListenerNATNet.listen(virtualbox, s_aNATNetEvents); // sic!

    static const VBoxEventType_T s_aVirtualBoxEvents[] = {
        VBoxEventType_OnHostNameResolutionConfigurationChange,
        VBoxEventType_OnNATNetworkStartStop,
        VBoxEventType_Invalid
    };
    m_ListenerVirtualBox.init(this);
    m_ListenerVirtualBox.listen(virtualbox, s_aVirtualBoxEvents);

    static const VBoxEventType_T s_aVBoxClientEvents[] = {
        VBoxEventType_OnVBoxSVCAvailabilityChanged,
        VBoxEventType_Invalid
    };
    m_ListenerVBoxClient.init(this);
    m_ListenerVBoxClient.listen(virtualboxClient, s_aVBoxClientEvents);

    return VINF_SUCCESS;
}


int VBoxNetSlirpNAT::stopWorkerThreads(bool fWaitReceiveThread)
{
    ASMAtomicWriteBool(&m_fShutdown, true);
    if (m_hThrdPoll != NIL_RTTHREAD)
        slirpNotifyPollThread("stop-worker-threads");

    if (fWaitReceiveThread && m_hIf != NULL)
        IntNetR3IfWaitAbort(m_hIf);

    int rcRet = VINF_SUCCESS;
    if (fWaitReceiveThread && m_hThrRecv != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_hThrRecv, 5000, NULL);
        if (RT_SUCCESS(rc))
            m_hThrRecv = NIL_RTTHREAD;
        else
        {
            logError("Worker thread cleanup failed: receive thread wait failed, rc=%Rrc", rc);
            rcRet = rc;
        }
    }

    if (m_hThrdPoll != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_hThrdPoll, 5000, NULL);
        if (RT_SUCCESS(rc))
            m_hThrdPoll = NIL_RTTHREAD;
        else
        {
            logError("Worker thread cleanup failed: poll thread wait failed, rc=%Rrc", rc);
            if (RT_SUCCESS(rcRet))
                rcRet = rc;
        }
    }

    return rcRet;
}


/**
 * Run the pumps.
 *
 * Spawn the intnet pump thread that gets packets from the intnet and
 * feeds them to lwIP.  Enter COM event loop here, on the main thread.
 */
int
VBoxNetSlirpNAT::run()
{
    AssertReturn(m_hThrRecv == NIL_RTTHREAD, VERR_INVALID_STATE);
    AssertReturn(m_hThrdPoll == NIL_RTTHREAD, VERR_INVALID_STATE);

    ASMAtomicWriteBool(&m_fShutdown, false);

    logInfo("Starting worker threads\n");

    /* Spawn the I/O polling thread. */
    int rc = RTThreadCreate(&m_hThrdPoll,
                            VBoxNetSlirpNAT::pollThread, this,
                            0, /* :cbStack */
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                            "Poll");
    if (RT_FAILURE(rc))
        logError("Startup failed: poll thread creation failed, rc=%Rrc", rc);
    AssertRCReturn(rc, rc);

    /* spawn intnet input pump */
    rc = RTThreadCreate(&m_hThrRecv,
                        VBoxNetSlirpNAT::receiveThread, this,
                        0, /* :cbStack */
                        RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                        "RECV");
    if (RT_FAILURE(rc))
    {
        int rcWait = stopWorkerThreads(false /* fWaitReceiveThread */);
        AssertRC(rcWait);
        return rc;
    }

    /* main thread will run the API event queue pump */
    com::NativeEventQueue *pQueue = com::NativeEventQueue::getMainEventQueue();
    if (pQueue == NULL)
    {
        logError("Startup failed: main event queue is not available");
        int const rcWait = stopWorkerThreads(true /* fWaitReceiveThread */);
        AssertRC(rcWait);
        return VERR_GENERAL_FAILURE;
    }

    /* dispatch API events to our listeners */
    for (;;)
    {
        rc = pQueue->processEventQueue(RT_INDEFINITE_WAIT);
        if (rc == VERR_INTERRUPTED)
        {
            logInfo("Shutdown requested by event queue\n");
            break;
        }
        else if (rc != VINF_SUCCESS)
        {
            /* note any unexpected rc */
            logError("Event queue processing failed, rc=%Rrc", rc);
        }
    }

    /*
     * We are out of the event loop, so we were told to shut down.
     * Tell other threads to wrap up.
     */

    logInfo("Stopping worker threads\n");

    int rcRet = stopWorkerThreads(true /* fWaitReceiveThread */);

    if (RT_SUCCESS(rcRet))
        logInfo("Worker threads stopped\n");
    return rcRet;
}


void
VBoxNetSlirpNAT::shutdown()
{
    int rc;

    logInfo("Shutdown requested\n");

    ASMAtomicWriteBool(&m_fShutdown, true);
    if (m_hThrdPoll != NIL_RTTHREAD)
        slirpNotifyPollThread("shutdown");
    if (m_hIf != NULL)
        IntNetR3IfWaitAbort(m_hIf);

    com::NativeEventQueue *pQueue = com::NativeEventQueue::getMainEventQueue();
    if (pQueue == NULL)
    {
        logError("Shutdown warning: main event queue is not available");
        return;
    }

    /* unregister listeners */
    m_ListenerNATNet.unlisten();
    m_ListenerVirtualBox.unlisten();
    m_ListenerVBoxClient.unlisten();

    /* tell the event loop in run() to stop */
    rc = pQueue->interruptEventQueueProcessing();
    if (RT_FAILURE(rc))
        logError("Shutdown failed: event queue interrupt failed, rc=%Rrc", rc);
    else
        logInfo("Shutdown notification sent\n");
}


/**
 * @note: this work on Event thread.
 */
HRESULT VBoxNetSlirpNAT::HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent)
{
    HRESULT hrc = S_OK;
    switch (aEventType)
    {
        case VBoxEventType_OnNATNetworkSetting:
        {
            ComPtr<INATNetworkSettingEvent> pSettingsEvent(pEvent);

            com::Bstr networkName;
            hrc = pSettingsEvent->COMGETTER(NetworkName)(networkName.asOutParam());
            AssertComRCReturn(hrc, hrc);
            if (networkName != m_strNetworkName)
                break; /* change not for our network */

            // XXX: only handle IPv6 default route for now
            if (!m_ProxyOptions.in6_enabled)
                break;

            BOOL fIPv6DefaultRoute = FALSE;
            hrc = pSettingsEvent->COMGETTER(AdvertiseDefaultIPv6RouteEnabled)(&fIPv6DefaultRoute);
            AssertComRCReturn(hrc, hrc);

#if 0 /** @todo */
            if (m_ProxyOptions.ipv6_defroute == fIPv6DefaultRoute)
                break;

            m_ProxyOptions.ipv6_defroute = fIPv6DefaultRoute;
            tcpip_callback_with_block(proxy_rtadvd_do_quick, &m_LwipNetIf, 0);
#endif
            break;
        }

        case VBoxEventType_OnNATNetworkPortForward:
        {
            int rc = VINF_SUCCESS;
            ComPtr<INATNetworkPortForwardEvent> pForwardEvent = pEvent;

            com::Bstr networkName;
            hrc = pForwardEvent->COMGETTER(NetworkName)(networkName.asOutParam());
            AssertComRCReturn(hrc, hrc);
            if (networkName != m_strNetworkName)
                break; /* change not for our network */

            BOOL fCreateFW;
            hrc = pForwardEvent->COMGETTER(Create)(&fCreateFW);
            AssertComRCReturn(hrc, hrc);

            BOOL  fIPv6FW;
            hrc = pForwardEvent->COMGETTER(Ipv6)(&fIPv6FW);
            AssertComRCReturn(hrc, hrc);

            com::Bstr name;
            hrc = pForwardEvent->COMGETTER(Name)(name.asOutParam());
            AssertComRCReturn(hrc, hrc);

            NATProtocol_T proto = NATProtocol_TCP;
            hrc = pForwardEvent->COMGETTER(Proto)(&proto);
            AssertComRCReturn(hrc, hrc);

            com::Bstr strHostAddr;
            hrc = pForwardEvent->COMGETTER(HostIp)(strHostAddr.asOutParam());
            AssertComRCReturn(hrc, hrc);

            LONG lHostPort;
            hrc = pForwardEvent->COMGETTER(HostPort)(&lHostPort);
            AssertComRCReturn(hrc, hrc);

            com::Bstr strGuestAddr;
            hrc = pForwardEvent->COMGETTER(GuestIp)(strGuestAddr.asOutParam());
            AssertComRCReturn(hrc, hrc);

            LONG lGuestPort;
            hrc = pForwardEvent->COMGETTER(GuestPort)(&lGuestPort);
            AssertComRCReturn(hrc, hrc);

            PPORTFORWARDRULE pNatPf = (PPORTFORWARDRULE)RTMemAllocZ(sizeof(*pNatPf));
            if (!pNatPf)
            {
                hrc = E_OUTOFMEMORY;
                goto port_forward_done;
            }

            pNatPf->fPfrIPv6 = fIPv6FW;

            switch (proto)
            {
                case NATProtocol_TCP:
                    pNatPf->iPfrProto = IPPROTO_TCP;
                    break;
                case NATProtocol_UDP:
                    pNatPf->iPfrProto = IPPROTO_UDP;
                    break;

                default:
                    logInfo("Event: %s %s port-forwarding rule \"%s\": invalid protocol %d\n",
                            fCreateFW ? "Add" : "Remove",
                            fIPv6FW ? "IPv6" : "IPv4",
                            com::Utf8Str(name).c_str(),
                            (int)proto);
                    goto port_forward_done;
            }

            logInfo("Event: %s %s port-forwarding rule \"%s\": %s %s%s%s:%d -> %s%s%s:%d\n",
                    fCreateFW ? "Add" : "Remove",
                    fIPv6FW ? "IPv6" : "IPv4",
                    com::Utf8Str(name).c_str(),
                    proto == NATProtocol_TCP ? "TCP" : "UDP",
                    /* from */
                    fIPv6FW ? "[" : "",
                    com::Utf8Str(strHostAddr).c_str(),
                    fIPv6FW ? "]" : "",
                    lHostPort,
                    /* to */
                    fIPv6FW ? "[" : "",
                    com::Utf8Str(strGuestAddr).c_str(),
                    fIPv6FW ? "]" : "",
                    lGuestPort);

            if (name.length() > sizeof(pNatPf->szPfrName))
            {
                hrc = E_INVALIDARG;
                goto port_forward_done;
            }

            RTStrPrintf(pNatPf->szPfrName, sizeof(pNatPf->szPfrName),
                        "%s", com::Utf8Str(name).c_str());

            RTStrPrintf(pNatPf->szPfrHostAddr, sizeof(pNatPf->szPfrHostAddr),
                        "%s", com::Utf8Str(strHostAddr).c_str());

            /* XXX: limits should be checked */
            pNatPf->u16PfrHostPort = (uint16_t)lHostPort;

            RTStrPrintf(pNatPf->szPfrGuestAddr, sizeof(pNatPf->szPfrGuestAddr),
                        "%s", com::Utf8Str(strGuestAddr).c_str());

            /* XXX: limits should be checked */
            pNatPf->u16PfrGuestPort = (uint16_t)lGuestPort;

            rc = RTReqQueueCallEx(m_hSlirpReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                  (PFNRT)natServicePfRegister, 4, this, pNatPf, !fCreateFW /*fRemove*/, true /*fRuntime*/);
            if (RT_FAILURE(rc))
                RTMemFree(pNatPf);

        port_forward_done:
            /* clean up strings */
            name.setNull();
            strHostAddr.setNull();
            strGuestAddr.setNull();
            break;
        }

        case VBoxEventType_OnHostNameResolutionConfigurationChange:
        {
            struct in_addr *paRealNameservers = NULL;
            size_t          cRealNameservers  = 0;
            int rc = getHostNameservers(&paRealNameservers, &cRealNameservers);
            if (RT_FAILURE(rc))
                return rc;
            if (cRealNameservers > 0)
            {
                m_ProxyOptions.aRealNameservers = paRealNameservers;
                m_ProxyOptions.cRealNameservers = cRealNameservers;

                slirp_set_RealNameservers(m_pSlirp, m_ProxyOptions.cRealNameservers,
                                          m_ProxyOptions.aRealNameservers);

                logInfo("Transfered %u nameservers to NAT engine.\n", (unsigned)cRealNameservers);
            }
            else
            {
                logInfo("No usable host IPv4 nameserver available; using libslirp DNS proxy\n");

                RTNETADDRIPV4 Nameserver4;
                Nameserver4.u = m_ProxyOptions.vnetwork.s_addr | RT_H2N_U32_C(0x00000003);

                memcpy(&m_ProxyOptions.vnameserver, &Nameserver4, sizeof(RTNETADDRIPV4));
                m_ProxyOptions.aRealNameservers = NULL;
                m_ProxyOptions.cRealNameservers = 0;

                // This ensures that any new DHCP requests use fallback.
                slirp_set_RealNameservers(m_pSlirp, 0, NULL);

                slirp_set_vnameserver(m_pSlirp, m_ProxyOptions.vnameserver);
            }

            struct in6_addr *paRealNameservers6 = NULL;
            size_t           cRealNameservers6  = 0;
            rc = getHostNameservers6(&paRealNameservers6, &cRealNameservers6);
            if (RT_FAILURE(rc))
                return rc;
            if (cRealNameservers6 > 0)
            {
                m_ProxyOptions.aIPv6RealNameservers = paRealNameservers6;
                m_ProxyOptions.cIPv6RealNameservers = cRealNameservers6;

                slirp_set_IPv6RealNameservers(m_pSlirp, m_ProxyOptions.cIPv6RealNameservers,
                                              m_ProxyOptions.aIPv6RealNameservers);

                logInfo("Transfered %u IPv6 nameservers to NAT engine.\n", (unsigned)cRealNameservers6);
            }
            else
            {
                m_ProxyOptions.aIPv6RealNameservers = NULL;
                m_ProxyOptions.cIPv6RealNameservers = 0;
                slirp_set_IPv6RealNameservers(m_pSlirp, 0, NULL);
                logInfo("No IPv6 nameservers available from the host, using the virtual IPv6 DNS proxy address.\n");
            }
            break;
        }

        case VBoxEventType_OnNATNetworkStartStop:
        {
            ComPtr <INATNetworkStartStopEvent> pStartStopEvent = pEvent;

            com::Bstr networkName;
            hrc = pStartStopEvent->COMGETTER(NetworkName)(networkName.asOutParam());
            AssertComRCReturn(hrc, hrc);
            if (networkName != m_strNetworkName)
                break; /* change not for our network */

            BOOL fStart = TRUE;
            hrc = pStartStopEvent->COMGETTER(StartEvent)(&fStart);
            AssertComRCReturn(hrc, hrc);

            if (!fStart)
                shutdown();
            break;
        }

        case VBoxEventType_OnVBoxSVCAvailabilityChanged:
        {
            logInfo("VBoxSVC became unavailable, exiting.\n");
            shutdown();
            break;
        }

        default: break; /* Shut up MSC. */
    }
    return hrc;
}


/**
 * Read the list of host's resolvers via the API.
 *
 * Called during initialization and in response to the
 * VBoxEventType_OnHostNameResolutionConfigurationChange event.
 *
 * @returns VBox status code.
 *
 * @param   ppaNameservers      Where to return the allocated array.
 * @param   pcNameservers       Where to return the number of entries.
 *
 * @note Can return a zero-sized array if only loopback nameservers are present
 *       or all candidates fail to parse.
 */
int VBoxNetSlirpNAT::getHostNameservers(struct in_addr **ppaNameservers, size_t *pcNameservers)
{
    AssertPtrReturn(ppaNameservers, VERR_INVALID_POINTER);
    AssertPtrReturn(pcNameservers, VERR_INVALID_POINTER);
    *ppaNameservers = NULL;
    *pcNameservers = 0;

    if (m_host.isNull())
        return VINF_SUCCESS;

    com::SafeArray<BSTR> aRawNameservers;
    HRESULT hrc = m_host->COMGETTER(NameServers)(ComSafeArrayAsOutParam(aRawNameservers));
    if (FAILED(hrc))
        return VINF_SUCCESS;

    const size_t cNameservers = aRawNameservers.size();
    if (cNameservers == 0)
        return VINF_SUCCESS;

    struct in_addr *paNameservers = (struct in_addr *)RTMemAllocZ(cNameservers * sizeof(*paNameservers));
    if (!paNameservers)
    {
        logError("Nameserver array construction failed. Out of memory.\n");
        return VERR_NO_MEMORY;
    }

    size_t cStoredNameservers = 0;
    for (size_t idx = 0; idx < cNameservers; idx++)
    {
        RTNETADDRIPV4 tmpNameserver;
        int rc = RTNetStrToIPv4Addr(com::Utf8Str(aRawNameservers[idx]).c_str(), &tmpNameserver);
        if (RT_FAILURE(rc))
        {
            logError("Failed to parse IPv4 nameserver %ls", aRawNameservers[idx]);
            continue;
        }

        /*
         * Checks to see if any nameservers are in 127.0.0.0/8 subnet.
         * Skips any if found.
         */
        /** @todo r=jack: Make calls to loopback DNS work. */
        if (!((tmpNameserver.u & RT_H2N_U32_C(IN_CLASSA_NET))
            == RT_H2N_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET)))
            paNameservers[cStoredNameservers++].s_addr = tmpNameserver.u;
    }

    if (cStoredNameservers == 0)
    {
        RTMemFree(paNameservers);
        paNameservers = NULL;
    }

    *ppaNameservers = paNameservers;
    *pcNameservers = cStoredNameservers;
    return VINF_SUCCESS;
}


/**
 * Read the list of host's IPv6 resolvers via the API.
 *
 * Called during initialization and in response to the
 * VBoxEventType_OnHostNameResolutionConfigurationChange event.
 *
 * @returns VBox status code.
 *
 * @param   ppaNameservers      Where to return the allocated array.
 * @param   pcNameservers       Where to return the number of entries.
 */
int VBoxNetSlirpNAT::getHostNameservers6(struct in6_addr **ppaNameservers, size_t *pcNameservers)
{
    AssertPtrReturn(ppaNameservers, VERR_INVALID_POINTER);
    AssertPtrReturn(pcNameservers, VERR_INVALID_POINTER);
    *ppaNameservers = NULL;
    *pcNameservers = 0;

    if (m_host.isNull())
        return VINF_SUCCESS;

    com::SafeArray<BSTR> aRawNameservers;
    HRESULT hrc = m_host->COMGETTER(V6NameServers)(ComSafeArrayAsOutParam(aRawNameservers));
    if (FAILED(hrc))
        return VINF_SUCCESS;

    const size_t cNameservers = aRawNameservers.size();
    if (cNameservers == 0)
        return VINF_SUCCESS;

    struct in6_addr *paNameservers = (struct in6_addr *)RTMemAllocZ(cNameservers * sizeof(*paNameservers));
    if (!paNameservers)
    {
        logError("IPv6 nameserver array construction failed. Out of memory.\n");
        return VERR_NO_MEMORY;
    }

    size_t cStoredNameservers = 0;
    for (size_t idx = 0; idx < cNameservers; idx++)
    {
        RTNETADDRIPV6 tmpNameserver;
        int rc = RTNetStrToIPv6AddrEx(com::Utf8Str(aRawNameservers[idx]).c_str(), &tmpNameserver, NULL /* ppszNext */);
        if (RT_FAILURE(rc))
        {
            logError("Failed to parse IPv6 nameserver %ls", aRawNameservers[idx]);
            continue;
        }

        memcpy(&paNameservers[cStoredNameservers], &tmpNameserver, sizeof(paNameservers[cStoredNameservers]));
        cStoredNameservers++;
    }

    if (cStoredNameservers == 0)
    {
        RTMemFree(paNameservers);
        paNameservers = NULL;
    }

    *ppaNameservers = paNameservers;
    *pcNameservers = cStoredNameservers;
    return VINF_SUCCESS;
}


/**
 * Fetch port-forwarding rules via the API.
 *
 * Reads the initial sets of rules from VBoxSVC.  The rules will be
 * activated when all the initialization and plumbing is done.  See
 * natServiceProcessRegisteredPf().
 */
int VBoxNetSlirpNAT::fetchNatPortForwardRules(VECNATSERVICEPF &vec, bool fIsIPv6)
{
    HRESULT hrc;

    com::SafeArray<BSTR> rules;
    if (fIsIPv6)
        hrc = m_net->COMGETTER(PortForwardRules6)(ComSafeArrayAsOutParam(rules));
    else
        hrc = m_net->COMGETTER(PortForwardRules4)(ComSafeArrayAsOutParam(rules));
    AssertComRCReturn(hrc, VERR_INTERNAL_ERROR);

    NATSERVICEPORTFORWARDRULE Rule;
    for (size_t idxRules = 0; idxRules < rules.size(); ++idxRules)
    {
        Log(("%d-%s rule: %ls\n", idxRules, (fIsIPv6 ? "IPv6" : "IPv4"), rules[idxRules]));
        RT_ZERO(Rule);

        int rc = netPfStrToPf(com::Utf8Str(rules[idxRules]).c_str(), fIsIPv6,
                              &Rule.Pfr);
        if (RT_FAILURE(rc))
            continue;

        vec.push_back(Rule);
    }

    return VINF_SUCCESS;
}


/**
 * Activate the initial set of port-forwarding rules.
 */
int VBoxNetSlirpNAT::natServiceProcessRegisteredPf(VECNATSERVICEPF& vecRules)
{
    ITERATORNATSERVICEPF it;
    for (it = vecRules.begin(); it != vecRules.end(); ++it)
    {
        NATSERVICEPORTFORWARDRULE &natPf = *it;

        logInfo("Loading %s port-forwarding rule \"%s\": %s %s%s%s:%d -> %s%s%s:%d\n",
                natPf.Pfr.fPfrIPv6 ? "IPv6" : "IPv4",
                natPf.Pfr.szPfrName,
                natPf.Pfr.iPfrProto == IPPROTO_TCP ? "TCP" : "UDP",
                /* from */
                natPf.Pfr.fPfrIPv6 ? "[" : "",
                natPf.Pfr.szPfrHostAddr,
                natPf.Pfr.fPfrIPv6 ? "]" : "",
                natPf.Pfr.u16PfrHostPort,
                /* to */
                natPf.Pfr.fPfrIPv6 ? "[" : "",
                natPf.Pfr.szPfrGuestAddr,
                natPf.Pfr.fPfrIPv6 ? "]" : "",
                natPf.Pfr.u16PfrGuestPort);

        natServicePfRegister(this, &natPf.Pfr, false /*fRemove*/, false /*fRuntime*/);
    }

    return VINF_SUCCESS;
}


/**
 * Activate a single port-forwarding rule.
 *
 * This is used both when we activate all the initial rules on startup
 * and when port-forwarding rules are changed and we are notified via
 * an API event.
 */
/* static */
DECLCALLBACK(void) VBoxNetSlirpNAT::natServicePfRegister(VBoxNetSlirpNAT *pThis, PPORTFORWARDRULE pNatPf, bool fRemove, bool fRuntime)
{
    bool fUdp;
    switch(pNatPf->iPfrProto)
    {
        case IPPROTO_TCP:
            fUdp = false;
            break;
        case IPPROTO_UDP:
            fUdp = true;
            break;
        default:
            return;
    }

    const char *pszHostAddr = pNatPf->szPfrHostAddr;
    if (pszHostAddr[0] == '\0')
    {
        if (pNatPf->fPfrIPv6)
            pszHostAddr = "::";
        else
            pszHostAddr = "0.0.0.0";
    }

    const char *pszGuestAddr = pNatPf->szPfrGuestAddr;
    if (pszGuestAddr[0] == '\0')
    {
        if (pNatPf->fPfrIPv6)
            pszGuestAddr = "::";
        else
            pszGuestAddr = "0.0.0.0";
    }

    struct in_addr guestIp, hostIp;
    if (inet_aton(pszHostAddr, &hostIp) == 0)
        hostIp.s_addr = INADDR_ANY;

    if (inet_aton(pszGuestAddr, &guestIp) == 0)
    {
        logError("Unable to convert guest address '%s' for %s rule \"%s\"",
                    pszGuestAddr, pNatPf->fPfrIPv6 ? "IPv6" : "IPv4",
                    pNatPf->szPfrName);
        return;
    }

    int rc;
    if (fRemove)
        rc = slirp_remove_hostfwd(pThis->m_pSlirp, fUdp, hostIp, pNatPf->u16PfrHostPort);
    else
        rc = slirp_add_hostfwd(pThis->m_pSlirp, fUdp,
                               hostIp, pNatPf->u16PfrHostPort,
                               guestIp, pNatPf->u16PfrGuestPort);
    if (!rc)
    {
        if (fRuntime)
        {
            VECNATSERVICEPF& rules = pNatPf->fPfrIPv6 ? pThis->m_vecPortForwardRule6
                                                      : pThis->m_vecPortForwardRule4;
            if (fRemove)
            {
                ITERATORNATSERVICEPF it;
                for (it = rules.begin(); it != rules.end(); ++it)
                {
                    /* compare */
                    NATSERVICEPORTFORWARDRULE &natFw = *it;
                    if (   natFw.Pfr.iPfrProto == pNatPf->iPfrProto
                        && natFw.Pfr.u16PfrHostPort == pNatPf->u16PfrHostPort
                        && strncmp(natFw.Pfr.szPfrHostAddr, pNatPf->szPfrHostAddr, INET6_ADDRSTRLEN) == 0
                        && natFw.Pfr.u16PfrGuestPort == pNatPf->u16PfrGuestPort
                        && strncmp(natFw.Pfr.szPfrGuestAddr, pNatPf->szPfrGuestAddr, INET6_ADDRSTRLEN) == 0)
                    {
                        rules.erase(it);
                        break;
                    }
                } /* loop over vector elements */
            }
            else /* Addition */
            {
                NATSERVICEPORTFORWARDRULE r;
                RT_ZERO(r);
                memcpy(&r.Pfr, pNatPf, sizeof(*pNatPf));
                rules.push_back(r);
            } /* condition add or delete */
        }
        else /* The rules vector is already up to date. */
            Assert(fRemove == false);
    }
    else
        logError("Unable to %s %s rule \"%s\"",
                    fRemove ? "remove" : "add",
                    pNatPf->fPfrIPv6 ? "IPv6" : "IPv4",
                    pNatPf->szPfrName);

    if (fRuntime)
    {
        /* Free the rule in any case. */
        RTMemFree(pNatPf);
    }
}


/**
 * Converts slirp representation of poll events to host representation.
 *
 * @param   iEvents     Integer representing slirp type poll events.
 *
 * @returns Integer representing host type poll events.
 */
DECLINLINE(short) pollEventSlirpToHost(int iEvents)
{
    short iRet = 0;
#ifndef RT_OS_WINDOWS
    if (iEvents & SLIRP_POLL_IN)  iRet |= POLLIN;
    if (iEvents & SLIRP_POLL_OUT) iRet |= POLLOUT;
    if (iEvents & SLIRP_POLL_PRI) iRet |= POLLPRI;
    if (iEvents & SLIRP_POLL_ERR) iRet |= POLLERR;
    if (iEvents & SLIRP_POLL_HUP) iRet |= POLLHUP;
#else
    if (iEvents & SLIRP_POLL_IN)  iRet |= (POLLRDNORM | POLLRDBAND);
    if (iEvents & SLIRP_POLL_OUT) iRet |= POLLWRNORM;
    if (iEvents & SLIRP_POLL_PRI) iRet |= (POLLIN);
    if (iEvents & SLIRP_POLL_ERR) iRet |= 0;
    if (iEvents & SLIRP_POLL_HUP) iRet |= 0;
#endif
    return iRet;
}

/**
 * Converts host representation of poll events to slirp representation.
 *
 * @param   iEvents     Integer representing host type poll events.
 *
 * @returns Integer representing slirp type poll events.
 *
 * @thread  ?
 */
DECLINLINE(int) pollEventHostToSlirp(int iEvents)
{
    int iRet = 0;
#ifndef RT_OS_WINDOWS
    if (iEvents & POLLIN)  iRet |= SLIRP_POLL_IN;
    if (iEvents & POLLOUT) iRet |= SLIRP_POLL_OUT;
    if (iEvents & POLLPRI) iRet |= SLIRP_POLL_PRI;
    if (iEvents & POLLERR) iRet |= SLIRP_POLL_ERR;
    if (iEvents & POLLHUP) iRet |= SLIRP_POLL_HUP;
#else
    if (iEvents & (POLLRDNORM | POLLRDBAND))  iRet |= SLIRP_POLL_IN;
    if (iEvents & POLLWRNORM) iRet |= SLIRP_POLL_OUT;
    if (iEvents & (POLLPRI)) iRet |= SLIRP_POLL_PRI;
    if (iEvents & POLLERR) iRet |= SLIRP_POLL_ERR;
    if (iEvents & POLLHUP) iRet |= SLIRP_POLL_HUP;
#endif
    return iRet;
}


/*
 * Libslirp Callbacks
 */
/**
 * Get the NAT thread out of poll/WSAWaitForMultipleEvents
 */
void VBoxNetSlirpNAT::slirpNotifyPollThread(const char *pszWho)
{
    RT_NOREF(pszWho);
#ifdef RT_OS_WINDOWS
    int cbWritten = send(m_ahWakeupSockPair[0], "", 1, NULL);
    if (RT_LIKELY(cbWritten != SOCKET_ERROR))
        ASMAtomicIncU64(&m_cWakeupNotifs);
    else
        Log4(("Notify NAT Thread Error %d\n", WSAGetLastError()));
#else
    /* kick poll() */
    size_t cbIgnored;
    int rc = RTPipeWrite(m_hPipeWrite, "", 1, &cbIgnored);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU64(&m_cWakeupNotifs);
#endif
}


/**
 * Callback called by libslirp to send packet into the internal network.
 *
 * @param   pvBuf   Pointer to packet buffer.
 * @param   cb      Size of packet.
 * @param   pvUser  Pointer to NAT State context.
 *
 * @returns Size of packet received or -1 on error.
 *
 * @thread  ?
 */
/*static*/ ssize_t VBoxNetSlirpNAT::slirpSendPacketCb(const void *pvBuf, ssize_t cb, void *pvUser) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(pvUser);
    AssertPtrReturn(pThis, -1);

    INTNETFRAME Frame;
    int rc = IntNetR3IfQueryOutputFrame(pThis->m_hIf, (uint32_t)cb, &Frame);
    if (RT_FAILURE(rc))
        return -1;

    memcpy(Frame.pvFrame, pvBuf, cb);
    rc = IntNetR3IfOutputFrameCommit(pThis->m_hIf, &Frame);
    if (RT_FAILURE(rc))
        return -1;
    return cb;
}


/**
 * Callback called by libslirp when the guest does something wrong.
 *
 * @param   pszMsg  Error message string.
 * @param   pvUser  Pointer to NAT State context.
 *
 * @thread  ?
 */
/*static*/ void VBoxNetSlirpNAT::slirpGuestErrorCb(const char *pszMsg, void *pvUser) RT_NOTHROW_DEF
{
    /* Note! This is _just_ libslirp complaining about odd guest behaviour. */
    LogRelMax(250, ("NAT Guest Error: %s\n", pszMsg));
    RT_NOREF(pvUser);
}


/**
 * Callback called by libslirp to get the current timestamp in nanoseconds.
 *
 * @param   pvUser  Pointer to NAT State context.
 *
 * @returns 64-bit signed integer representing time in nanoseconds.
 */
/*static*/ int64_t VBoxNetSlirpNAT::slirpClockGetNsCb(void *pvUser) RT_NOTHROW_DEF
{
    RT_NOREF(pvUser);
    return (int64_t)RTTimeNanoTS();
}

/**
 * Callback called by slirp to create a new timer and insert it into the given list.
 *
 * @param   slirpTimeCb     Callback function supplied to the new timer upon timer expiry.
 *                          Called later by the timeout handler.
 * @param   cb_opaque       Opaque object supplied to slirpTimeCb when called. Should be
 *                          Identical to the opaque parameter.
 * @param   opaque          Pointer to NAT State context.
 *
 * @returns Pointer to new timer.
 */
/*static*/ void *VBoxNetSlirpNAT::slirpTimerNewCb(SlirpTimerCb slirpTimeCb, void *cb_opaque, void *opaque) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(opaque);
    Assert(pThis);

    SlirpTimer * const pNewTimer = (SlirpTimer *)RTMemAlloc(sizeof(*pNewTimer));
    if (pNewTimer)
    {
        pNewTimer->msExpire = 0;
        pNewTimer->pHandler = slirpTimeCb;
        pNewTimer->opaque = cb_opaque;
        /** @todo r=bird: Not thread safe. Assumes pSlirpThread */
        pNewTimer->next = pThis->pTimerHead;
        pThis->pTimerHead = pNewTimer;
    }
    return pNewTimer;
}

/**
 * Callback called by slirp to free a timer.
 *
 * @param   pvTimer Pointer to slirpTimer object to be freed.
 * @param   pvUser  Pointer to NAT State context.
 */
/*static*/ void VBoxNetSlirpNAT::slirpTimerFreeCb(void *pvTimer, void *pvUser) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT    *pThis = static_cast<VBoxNetSlirpNAT *>(pvUser);
    SlirpTimer * const pTimer = (SlirpTimer *)pvTimer;
    Assert(pThis);

    SlirpTimer *pPrev    = NULL;
    SlirpTimer *pCurrent = pThis->pTimerHead;
    while (pCurrent != NULL)
    {
        if (pCurrent == pTimer)
        {
            /* unlink it. */
            if (!pPrev)
                pThis->pTimerHead = pCurrent->next;
            else
                pPrev->next       = pCurrent->next;
            pCurrent->next = NULL;
            RTMemFree(pCurrent);
            return;
        }

        /* advance */
        pPrev = pCurrent;
        pCurrent = pCurrent->next;
    }
    Assert(!pTimer);
}

/**
 * Callback called by slirp to modify a timer.
 *
 * @param   pvTimer         Pointer to slirpTimer object to be modified.
 * @param   msNewDeadlineTs The new absolute expiration time in milliseconds.
 *                          Zero stops it.
 * @param   pvUser          Pointer to NAT State context.
 */
/*static*/ void VBoxNetSlirpNAT::slirpTimerModCb(void *pvTimer, int64_t msNewDeadlineTs, void *pvUser) RT_NOTHROW_DEF
{
    SlirpTimer * const pTimer = (SlirpTimer *)pvTimer;
    pTimer->msExpire = msNewDeadlineTs;
    RT_NOREF(pvUser);
}

/**
 * Callback called by slirp when there is I/O that needs to happen.
 *
 * @param   opaque  Pointer to NAT State context.
 */
/*static*/ void VBoxNetSlirpNAT::slirpNotifyCb(void *opaque) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(opaque);
    RT_NOREF(pThis);
    // pThis->slirpNotifyPollThread("slirpNotifyCb");
}

/**
 * Registers poll. Unused function (other than logging).
 */
/*static*/ void VBoxNetSlirpNAT::slirpRegisterPoll(slirp_os_socket socket, void *opaque) RT_NOTHROW_DEF
{
    RT_NOREF(socket, opaque);
#ifdef RT_OS_WINDOWS
    Log4(("Poll registered: fd=%p\n", socket));
#else
    Log4(("Poll registered: fd=%d\n", socket));
#endif
}

/**
 * Unregisters poll. Unused function (other than logging).
 */
/*static*/ void VBoxNetSlirpNAT::slirpUnregisterPoll(slirp_os_socket socket, void *opaque) RT_NOTHROW_DEF
{
    RT_NOREF(socket, opaque);
#ifdef RT_OS_WINDOWS
    Log4(("Poll unregistered: fd=%p\n", socket));
#else
    Log4(("Poll unregistered: fd=%d\n", socket));
#endif
}

/**
 * Callback function to add entry to pollfd array.
 *
 * @param   hFd     Socket handle.
 * @param   iEvents Integer of slirp type poll events.
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns Index of latest pollfd entry.
 *
 * @thread  ?
 */
/*static*/ int VBoxNetSlirpNAT::slirpAddPollCb(slirp_os_socket hFd, int iEvents, void *opaque) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(opaque);

    if (pThis->nsock + 1 >= pThis->m_cPollFd)
    {
        size_t cbNew = pThis->m_cPollFd * 2 * sizeof(struct pollfd);
        struct pollfd *pvNew = (struct pollfd *)RTMemRealloc(pThis->m_paPollFd, cbNew);
        if (pvNew)
        {
            pThis->m_paPollFd = pvNew;
            pThis->m_cPollFd *= 2;
        }
        else
            return -1;
    }

    unsigned int uIdx = pThis->nsock;
    Assert(uIdx < INT_MAX);
    pThis->m_paPollFd[uIdx].fd = hFd;
    pThis->m_paPollFd[uIdx].events = pollEventSlirpToHost(iEvents);
    pThis->m_paPollFd[uIdx].revents = 0;
    pThis->nsock += 1;
    return uIdx;
}

/**
 * Get translated revents from a poll at a given index.
 *
 * @param   idx     Integer index of poll.
 * @param   opaque  Pointer to NAT State context.
 *
 * @returns Integer representing transalted revents.
 *
 * @thread  ?
 */
/*static*/ int VBoxNetSlirpNAT::slirpGetREventsCb(int idx, void *opaque) RT_NOTHROW_DEF
{
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(opaque);
    struct pollfd* polls = pThis->m_paPollFd;
    return pollEventHostToSlirp(polls[idx].revents);
}


/**
 * Run expired timers.
 *
 * @thread  pSlirpThread
 */
void VBoxNetSlirpNAT::timersRunExpired(void)
{
    int64_t const msNow    = slirpClockGetNsCb(this) / RT_NS_1MS;
    SlirpTimer   *pCurrent = pTimerHead;
    while (pCurrent != NULL)
    {
        SlirpTimer * const pNext = pCurrent->next; /* (in case the timer is destroyed from the callback) */
        if (pCurrent->msExpire <= msNow && pCurrent->msExpire > 0)
        {
            pCurrent->msExpire = 0;
            pCurrent->pHandler(pCurrent->opaque);
        }
        pCurrent = pNext;
    }
}


/**
 * Reduce the given timeout to match the earliest timer deadline.
 *
 * @returns Updated cMsTimeout value.
 * @param   cMsTimeout  The timeout to adjust, in milliseconds.
 *
 * @thread  pSlirpThread
 */
int VBoxNetSlirpNAT::slirpTimersAdjustTimeoutDown(int cMsTimeout)
{
    /** @todo r=bird: This and a most other stuff would be easier if msExpire was
     *                unsigned and we used UINT64_MAX for stopped timers.  */
    /** @todo The timer code isn't thread safe, it assumes a single user thread
     *        (pSlirpThread). */

    /* Find the first (lowest) deadline. */
    int64_t msDeadline = INT64_MAX;
    for (SlirpTimer *pCurrent = pTimerHead; pCurrent; pCurrent = pCurrent->next)
        if (pCurrent->msExpire < msDeadline && pCurrent->msExpire > 0)
            msDeadline = pCurrent->msExpire;

    /* Adjust the timeout if there is a timer with a deadline. */
    if (msDeadline < INT64_MAX)
    {
        int64_t const msNow = slirpClockGetNsCb(NULL) / RT_NS_1MS;
        if (msNow < msDeadline)
        {
            int64_t cMilliesToDeadline = msDeadline - msNow;
            if (cMilliesToDeadline < cMsTimeout)
                cMsTimeout = (int)cMilliesToDeadline;
        }
        else
            cMsTimeout = 0;
    }

    return cMsTimeout;
}


/**
 * Slirp polling thread.
 */
/* static */ DECLCALLBACK(int)
VBoxNetSlirpNAT::pollThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);

    AssertReturn(pvUser != NULL, VERR_INVALID_PARAMETER);
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(pvUser);

    if (ASMAtomicReadBool(&pThis->m_fShutdown))
        return VINF_SUCCESS;

    /* Activate the initial port forwarding rules. */
    pThis->natServiceProcessRegisteredPf(pThis->m_vecPortForwardRule4);
    pThis->natServiceProcessRegisteredPf(pThis->m_vecPortForwardRule6);

    /* The first polling entry is for the control/wakeup pipe. */
#ifdef RT_OS_WINDOWS
    pThis->slirpAddPollCb(pThis->m_ahWakeupSockPair[1], SLIRP_POLL_IN | SLIRP_POLL_HUP, pThis);
#else
    unsigned int cPollNegRet = 0;
    RTHCINTPTR const i64NativeReadPipe = RTPipeToNative(pThis->m_hPipeRead);
    int const        fdNativeReadPipe  = (int)i64NativeReadPipe;
    Assert(fdNativeReadPipe == i64NativeReadPipe); Assert(fdNativeReadPipe >= 0);
    pThis->m_paPollFd[0].fd = fdNativeReadPipe;
    pThis->m_paPollFd[0].events = POLLRDNORM | POLLPRI | POLLRDBAND;
    pThis->m_paPollFd[0].revents = 0;
#endif /* !RT_OS_WINDOWS */

    /*
     * Polling loop.
     */
    while (!ASMAtomicReadBool(&pThis->m_fShutdown))
    {
        /*
         * To prevent concurrent execution of sending/receiving threads
         */
        pThis->nsock = 1;

        uint32_t cMsTimeout = DRVNAT_DEFAULT_TIMEOUT;
        slirp_pollfds_fill_socket(pThis->m_pSlirp, &cMsTimeout, pThis->slirpAddPollCb /* SlirpAddPollCb */, pThis /* opaque */);
        cMsTimeout = pThis->slirpTimersAdjustTimeoutDown(cMsTimeout);

#ifdef RT_OS_WINDOWS
        int cChangedFDs = WSAPoll(pThis->m_paPollFd, pThis->nsock, cMsTimeout);
#else
        int cChangedFDs = poll(pThis->m_paPollFd, pThis->nsock, cMsTimeout);
#endif
        if (cChangedFDs < 0)
        {
#ifdef RT_OS_WINDOWS
            int const iLastErr = WSAGetLastError(); /* (In debug builds LogRel translates to two RTLogLoggerExWeak calls.) */
            logError("Poll failed, rc=%Rrc, changed fds=%d", iLastErr, cChangedFDs);
            Log4(("NAT: NSOCK = %d\n", pThis->nsock));
#else
            if (errno == EINTR)
            {
                Log2(("NAT: signal was caught while sleep on poll\n"));
                /* No error, just process all outstanding requests but don't wait */
                cChangedFDs = 0;
            }
            else if (cPollNegRet++ > 128)
            {
                logError("Poll failed, errno=%s, suppressed count=%u", strerror(errno), cPollNegRet);
                cPollNegRet = 0;
            }
#endif
        }

        Log4(("%s: poll\n", __FUNCTION__));
        slirp_pollfds_poll(pThis->m_pSlirp, cChangedFDs < 0, pThis->slirpGetREventsCb, pThis /* opaque */);

        /*
         * Drain the control pipe if necessary.
         */
        if (pThis->m_paPollFd[0].revents & (POLLIN|POLLRDNORM|POLLPRI|POLLRDBAND))   /* POLLPRI won't be seen with WSAPoll. */
        {
            char achBuf[1024];
            size_t cbRead;
            uint64_t cWakeupNotifs = ASMAtomicReadU64(&pThis->m_cWakeupNotifs);
#ifdef RT_OS_WINDOWS
            /** @todo r=bird: This returns -1 (SOCKET_ERROR) on failure, so any kind of
             *        error return here and we'll bugger up cbWakeupNotifs! */
            cbRead = recv(pThis->m_ahWakeupSockPair[1], &achBuf[0], RT_MIN(cWakeupNotifs, sizeof(achBuf)), NULL);
#else
            /** @todo r=bird: cbRead may in theory be used uninitialized here!  This
             *        isn't blocking, though, so we won't get stuck here if we mess up
             *         the count. */
            RTPipeRead(pThis->m_hPipeRead, &achBuf[0], RT_MIN(cWakeupNotifs, sizeof(achBuf)), &cbRead);
#endif
            ASMAtomicSubU64(&pThis->m_cWakeupNotifs, cbRead);
        }

        if (ASMAtomicReadBool(&pThis->m_fShutdown))
            break;

        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->m_hSlirpReqQueue, 0);
        pThis->timersRunExpired();
    }

    logInfo("Exiting\n");
    return VINF_SUCCESS;
}


/**
 * IntNetIf receive thread.  Runs intnet pump with our processFrame()
 * as input callback.
 */
/* static */ DECLCALLBACK(int)
VBoxNetSlirpNAT::receiveThread(RTTHREAD hThreadSelf, void *pvUser)
{
    HRESULT hrc;
    int rc;

    RT_NOREF(hThreadSelf);

    AssertReturn(pvUser != NULL, VERR_INVALID_PARAMETER);
    VBoxNetSlirpNAT *self = static_cast<VBoxNetSlirpNAT *>(pvUser);

    /* do we relaly need to init com on this thread? */
    hrc = com::Initialize();
    if (FAILED(hrc))
        return VERR_GENERAL_FAILURE;

    rc = IntNetR3IfPumpPkts(self->m_hIf, VBoxNetSlirpNAT::processFrame, self,
                            NULL /*pfnInputGso*/, NULL /*pvUserGso*/);
    if (rc == VERR_SEM_DESTROYED)
        return VINF_SUCCESS;

    logError("Packet pump failed unexpectedly, rc=%Rrc", rc);
    return VERR_INVALID_STATE;
}


/**
 * Worker function for drvNATSend().
 *
 * @param   pThis               Pointer to the NAT instance.
 * @param   pvFrame             Pointer to the frame data.
 * @param   cbFrame             Size of the frame in bytes.
 * @thread  NAT
 */
/*static*/ DECLCALLBACK(void) VBoxNetSlirpNAT::slirpSendWorker(VBoxNetSlirpNAT *pThis, void *pvFrame, size_t cbFrame)
{
    LogFlowFunc(("pThis=%p pvFrame=%p cbFrame=%zu\n", pThis, pvFrame, cbFrame));

    slirp_input(pThis->m_pSlirp, (uint8_t const *)pvFrame, (int)cbFrame);

    LogFlowFunc(("leave\n"));
    RTMemFree(pvFrame);
}


/**
 * Process an incoming frame received from the intnet.
 */
/* static */ DECLCALLBACK(void)
VBoxNetSlirpNAT::processFrame(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    AssertReturnVoid(pvFrame != NULL);

    LogFlowFunc(("processFrame:\n"));

    /* shouldn't happen, but if it does, don't even bother */
    if (RT_UNLIKELY(cbFrame < sizeof(RTNETETHERHDR)))
        return;

    /* we expect normal ethernet frame including .1Q and FCS */
    if (cbFrame > 1522)
    {
        Log2Func(("Error: Frame larger than expected.\n"));
        return;
    }

    AssertReturnVoid(pvUser != NULL);
    VBoxNetSlirpNAT *pThis = static_cast<VBoxNetSlirpNAT *>(pvUser);

    const void *pvBuf = RTMemDup(pvFrame, cbFrame);
    if (!pvBuf)
        return;

    int rc = RTReqQueueCallEx(pThis->m_hSlirpReqQueue, NULL /*ppReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)pThis->slirpSendWorker, 3, pThis, pvBuf, (size_t)cbFrame);
    if (RT_SUCCESS(rc))
    {
        pThis->slirpNotifyPollThread("processFrame");
        LogFlowFunc(("leave success\n"));
    }
}


/**
 * Retrieve network-specific extra data item.
 */
int VBoxNetSlirpNAT::getExtraData(com::Utf8Str &strValueOut, const char *pcszKey)
{
    HRESULT hrc;

    AssertReturn(!virtualbox.isNull(), E_FAIL);
    AssertReturn(m_strNetworkName.isNotEmpty(), E_FAIL);
    AssertReturn(pcszKey != NULL, E_FAIL);
    AssertReturn(*pcszKey != '\0', E_FAIL);

    com::BstrFmt bstrKey("NAT/%s/%s", m_strNetworkName.c_str(), pcszKey);
    com::Bstr bstrValue;
    hrc = virtualbox->GetExtraData(bstrKey.raw(), bstrValue.asOutParam());
    if (FAILED(hrc))
    {
        logComError(virtualbox, "GetExtraData", hrc);
        return VERR_GENERAL_FAILURE;
    }

    strValueOut = bstrValue;
    return VINF_SUCCESS;
}


/* static */
HRESULT VBoxNetSlirpNAT::logComError(ComPtr<IUnknown> iface,
                                       const com::Utf8Str &strContext,
                                       HRESULT hrc)
{
    const com::ErrorInfo info(iface, COM_IIDOF(IUnknown));
    if (info.isFullAvailable() || info.isBasicAvailable())
    {
        logErrorInfoList(info, strContext);
    }
    else
    {
        if (strContext.isNotEmpty())
            logError("%s: %Rhra", strContext.c_str(), hrc);
        else
            logError("%Rhra", hrc);
    }

    return hrc;
}


/* static */
void VBoxNetSlirpNAT::logErrorInfoList(const com::ErrorInfo &info,
                                         const com::Utf8Str &strContext)
{
    if (strContext.isNotEmpty())
        logError("%s", strContext.c_str());

    bool fFirst = true;
    for (const com::ErrorInfo *pInfo = &info;
         pInfo != NULL;
         pInfo = pInfo->getNext())
    {
        if (fFirst)
            fFirst = false;
        else
            logError("--------");

        logErrorInfo(*pInfo);
    }
}


/* static */
void VBoxNetSlirpNAT::logErrorInfo(const com::ErrorInfo &info)
{
#if defined (RT_OS_WIN)
    bool haveResultCode = info.isFullAvailable();
    bool haveComponent = true;
    bool haveInterfaceID = true;
#else /* !RT_OS_WIN */
    bool haveResultCode = true;
    bool haveComponent = info.isFullAvailable();
    bool haveInterfaceID = info.isFullAvailable();
#endif
    com::Utf8Str message;
    if (info.getText().isNotEmpty())
        message = info.getText();

    const char *pcszDetails = "Details: ";
    const char *pcszComma = ", ";
    const char *pcszSeparator = pcszDetails;

    if (haveResultCode)
    {
        message.appendPrintf("%s" "code %Rhrc (0x%RX32)",
            pcszSeparator, info.getResultCode(), info.getResultCode());
        pcszSeparator = pcszComma;
    }

    if (haveComponent)
    {
        message.appendPrintf("%s" "component %ls",
            pcszSeparator, info.getComponent().raw());
        pcszSeparator = pcszComma;
    }

    if (haveInterfaceID)
    {
        message.appendPrintf("%s" "interface %ls",
            pcszSeparator, info.getInterfaceName().raw());
        pcszSeparator = pcszComma;
    }

    if (info.getCalleeName().isNotEmpty())
    {
        message.appendPrintf("%s" "callee %ls",
            pcszSeparator, info.getCalleeName().raw());
        //pcszSeparator = pcszComma; unused
    }

    logError("%s", message.c_str());
}


/* static */
void VBoxNetSlirpNAT::logInfo(const char *a_pcszFormat, ...)
{
    va_list ap;

    va_start(ap, a_pcszFormat);
    com::Utf8Str message(a_pcszFormat, ap);
    va_end(ap);

    LogRel(("%s", message.c_str()));
}


/* static */
void VBoxNetSlirpNAT::logError(const char *a_pcszFormat, ...)
{
    va_list ap;

    va_start(ap, a_pcszFormat);
    com::Utf8Str message(a_pcszFormat, ap);
    va_end(ap);

    RTMsgError("%s", message.c_str());
    LogRel(("%s", message.c_str()));
}



/**
 * Create release logger.
 *
 * The NAT network name is sanitized so that it can be used in a path
 * component.  By default the release log is written to stdout and to
 * the file ~/.VirtualBox/${netname}.log but its destination and content
 * can be overridden with VBOXNET_${netname}_RELEASE_LOG family of
 * environment variables (also ..._DEST and ..._FLAGS).
 */
/* static */
int VBoxNetSlirpNAT::initLog()
{
    size_t cch;
    int rc;

    if (m_strNetworkName.isEmpty())
        return VERR_MISSING;

    char szNetwork[RTPATH_MAX];
    rc = RTStrCopy(szNetwork, sizeof(szNetwork), m_strNetworkName.c_str());
    if (RT_FAILURE(rc))
        return rc;

    // sanitize network name to be usable as a path component
    for (char *p = szNetwork; *p != '\0'; ++p)
    {
        if (RTPATH_IS_SEP(*p))
            *p = '_';
    }

    const char *pcszLogFile = NULL;
    char szLogFile[RTPATH_MAX];
    if (m_strHome.isNotEmpty())
    {
        cch = RTStrPrintf(szLogFile, sizeof(szLogFile),
                          "%s%c%s.log", m_strHome.c_str(), RTPATH_DELIMITER, szNetwork);
        if (cch < sizeof(szLogFile))
            pcszLogFile = szLogFile;
    }

    // sanitize network name some more to be usable as environment variable
    for (char *p = szNetwork; *p != '\0'; ++p)
    {
        if (*p != '_'
            && (*p < '0' || '9' < *p)
            && (*p < 'a' || 'z' < *p)
            && (*p < 'A' || 'Z' < *p))
        {
            *p = '_';
        }
    }

    char szEnvVarBase[128];
    const char *pcszEnvVarBase = szEnvVarBase;
    cch = RTStrPrintf(szEnvVarBase, sizeof(szEnvVarBase),
                      "VBOXNET_%s_RELEASE_LOG", szNetwork);
    if (cch >= sizeof(szEnvVarBase))
        pcszEnvVarBase = NULL;

    rc = com::VBoxLogRelCreate("NAT Network",
                               pcszLogFile,
                               RTLOGFLAGS_PREFIX_TIME_PROG | RTLOGFLAGS_PREFIX_THREAD,
                               "all all.restrict -default.restrict",
                               pcszEnvVarBase,
                               RTLOGDEST_FILE | RTLOGDEST_STDOUT,
                               32768 /* cMaxEntriesPerGroup */,
                               0 /* cHistory */,
                               0 /* uHistoryFileTime */,
                               0 /* uHistoryFileSize */,
                               NULL /*pErrInfo*/);
    if (RT_FAILURE(rc))
        RTMsgError("Failed to create log instance: %Rrc\n", rc);

    /*
     * Provide immediate feedback if corresponding LogRel level is
     * enabled.  It's frustrating when you chase some rare event and
     * discover you didn't actually have the corresponding log level
     * enabled because of a typo in the environment variable name or
     * its content.
     */
#define LOG_PING(_log) _log((#_log " enabled\n"))
    LOG_PING(LogRel2);
    LOG_PING(LogRel3);
    LOG_PING(LogRel4);
    LOG_PING(LogRel5);
    LOG_PING(LogRel6);
    LOG_PING(LogRel7);
    LOG_PING(LogRel8);
    LOG_PING(LogRel9);
    LOG_PING(LogRel10);
    LOG_PING(LogRel11);
    LOG_PING(LogRel12);

    return rc;
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    NOREF(envp);

    RTStrmPrintf(g_pStdOut, VBOX_PRODUCT " VBoxNetNAT " VBOX_VERSION_STRING " - r%s\n"
                 "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n", RTBldCfgRevisionStr());

    VBoxNetSlirpNAT NAT;

    int rcExit = NAT.parseArgs(argc, argv);
    if (rcExit != RTEXITCODE_SUCCESS)
    {
        /* messages are already printed */
        return rcExit == RTEXITCODE_DONE ? RTEXITCODE_SUCCESS : rcExit;
    }

    int rc = NAT.init();
    if (RT_FAILURE(rc))
        return RTEXITCODE_INIT;

    bool fSignalHandlerInstalled = false;
    com::NativeEventQueue *pQueue = com::NativeEventQueue::getMainEventQueue();
    if (pQueue != NULL)
    {
        rc = signalHandlerInstall(pQueue);
        if (RT_SUCCESS(rc))
            fSignalHandlerInstalled = true;
        else
            VBoxNetSlirpNAT::logError("Failed to install signal handler, rc=%Rrc", rc);
    }
    else
        VBoxNetSlirpNAT::logError("Failed to install signal handler: main event queue is not available");

    /* ignore rc */ NAT.run();

    if (fSignalHandlerInstalled)
    {
        rc = signalHandlerUninstall();
        if (RT_FAILURE(rc))
            VBoxNetSlirpNAT::logError("Failed to uninstall signal handler, rc=%Rrc", rc);
        AssertRC(rc);
    }

    NAT.destroy();

    VBoxNetSlirpNAT::logInfo("TrustedMain ended\n");
    return RTEXITCODE_SUCCESS;
}


#ifndef VBOX_WITH_HARDENING
int main(int argc, char **argv, char **envp)
{
#ifdef VBOX_WITH_INTNET_SERVICE_IN_R3
    int rc = RTR3InitExe(argc, &argv, 0 /* fFlags */);
#else
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
#endif
    if (RT_SUCCESS(rc))
        return TrustedMain(argc, argv, envp);
    return RTMsgInitFailure(rc);
}

# if defined(RT_OS_WINDOWS)
/** (We don't want a console usually.) */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    RT_NOREF(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    return main(__argc, __argv, environ);
}
# endif /* RT_OS_WINDOWS */

#endif /* !VBOX_WITH_HARDENING */
