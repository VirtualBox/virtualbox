/* $Id: VBoxNetSlirpNAT.cpp 110448 2025-07-28 20:27:59Z jack.doherty@oracle.com $ */
/** @file
 * VBoxNetNAT - NAT Service for connecting to IntNet.
 */

/*
 * Copyright (C) 2009-2025 Oracle and/or its affiliates.
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
# include "winutils.h"
# define inet_aton(x, y) inet_pton(2, x, y)
# define AF_INET6 23
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

/** The maximum (default) poll/WSAPoll timeout. */
#define DRVNAT_DEFAULT_TIMEOUT (int)RT_MS_1HOUR


typedef struct NATSERVICEPORTFORWARDRULE
{
    PORTFORWARDRULE Pfr;
} NATSERVICEPORTFORWARDRULE, *PNATSERVICEPORTFORWARDRULE;

typedef std::vector<NATSERVICEPORTFORWARDRULE> VECNATSERVICEPF;
typedef VECNATSERVICEPF::iterator ITERATORNATSERVICEPF;
typedef VECNATSERVICEPF::const_iterator CITERATORNATSERVICEPF;


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

    SlirpConfig m_ProxyOptions;
    struct sockaddr_in m_src4;
    struct sockaddr_in6 m_src6;

    uint16_t m_u16Mtu;

    unsigned int nsock;

    Slirp *m_pSlirp;
    struct pollfd *polls;

    /** Num Polls (not bytes) */
    unsigned int uPollCap = 0;

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
    int run();
    void shutdown();

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

    static void reportError(const char *a_pcszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

    static HRESULT reportComError(ComPtr<IUnknown> iface,
                                  const com::Utf8Str &strContext,
                                  HRESULT hrc);
    static void reportErrorInfoList(const com::ErrorInfo &info,
                                    const com::Utf8Str &strContext);
    static void reportErrorInfo(const com::ErrorInfo &info);

    HRESULT HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent);

    PCRTNETADDRIPV4 getHostNameservers();

    int slirpTimersAdjustTimeoutDown(int cMsTimeout);
    void slirpNotifyPollThread(const char *pszWho);

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
    m_cWakeupNotifs(0),
    m_u16Mtu(1500)
{
    LogFlowFuncEnter();

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

    polls      = (struct pollfd *)RTMemAllocZ(64 * sizeof(struct pollfd));
    uPollCap   = 64;

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
    RTReqQueueDestroy(m_hSlirpReqQueue);
    m_hSlirpReqQueue = NIL_RTREQQUEUE;
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
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "multiple --network options");
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
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "unexpected non-option argument");

            default:
                return RTGetOptPrintError(ch, &Val);
        }
    }

    if (m_strNetworkName.isEmpty())
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "missing --network option");

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

    LogFlowFuncEnter();

    /* Get the COM API set up. */
    rc = initCom();
    if (RT_FAILURE(rc))
        return rc;

    /* Get the home folder location.  It's ok if it fails. */
    initHome();

    /*
     * We get the network name on the command line.  Get hold of its
     * API object to get the rest of the configuration from.
     */
    hrc = virtualbox->FindNATNetworkByName(com::Bstr(m_strNetworkName).raw(),
                                           m_net.asOutParam());
    if (FAILED(hrc))
    {
        reportComError(virtualbox, "FindNATNetworkByName", hrc);
        return VERR_NOT_FOUND;
    }

    /*
     * Now that we know the network name and have ensured that it
     * indeed exists we can create the release log file.
     */
    initLog();

    // resolver changes are reported on vbox but are retrieved from
    // host so stash a pointer for future lookups
    hrc = virtualbox->COMGETTER(Host)(m_host.asOutParam());
    AssertComRCReturn(hrc, VERR_INTERNAL_ERROR);

    /* Get the settings related to IPv4. */
    rc = initIPv4();
    if (RT_FAILURE(rc))
        return rc;

    /* Get the settings related to IPv6. */
    rc = initIPv6();
    if (RT_FAILURE(rc))
        return rc;

    if (m_ProxyOptions.in6_enabled)


    if (m_strHome.isNotEmpty())
    {
        com::Utf8StrFmt strTftpRoot("%s%c%s", m_strHome.c_str(), RTPATH_DELIMITER, "TFTP");
        char *pszStrTemp;       // avoid const char ** vs char **
        rc = RTStrUtf8ToCurrentCP(&pszStrTemp, strTftpRoot.c_str());
        AssertRC(rc);
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
        return VERR_NO_MEMORY;

    initComEvents();
    /* end of COM initialization */

    rc = RTReqQueueCreate(&m_hSlirpReqQueue);
    AssertLogRelRCReturn(rc, rc);

#ifdef RT_OS_WINDOWS
    /* Create the wakeup socket pair (idx=0 is write, idx=1 is read). */
    m_ahWakeupSockPair[0] = INVALID_SOCKET;
    m_ahWakeupSockPair[1] = INVALID_SOCKET;
    rc = RTWinSocketPair(AF_INET, SOCK_DGRAM, 0, m_ahWakeupSockPair);
    AssertRCReturn(rc, rc);
#else
    /* Create the control pipe. */
    rc = RTPipeCreate(&m_hPipeRead, &m_hPipeWrite, 0 /*fFlags*/);
    AssertRCReturn(rc, rc);
#endif

    /* connect to the intnet */
    rc = IntNetR3IfCreate(&m_hIf, m_strNetworkName.c_str());
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfSetActive(m_hIf, true /*fActive*/);

    LogFlowFuncLeaveRC(rc);
    return rc;
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
        reportError("Failed to create VirtualBox Client object: %Rhra", hrc);
        return VERR_GENERAL_FAILURE;
    }

    hrc = virtualboxClient->COMGETTER(VirtualBox)(virtualbox.asOutParam());
    if (FAILED(hrc))
    {
        reportError("Failed to obtain VirtualBox object: %Rhra", hrc);
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
        reportComError(m_net, "Network", hrc);
        return VERR_GENERAL_FAILURE;
    }

    RTNETADDRIPV4 Net4, Mask4;
    int iPrefixLength;
    rc = RTNetStrToIPv4Cidr(com::Utf8Str(bstrIPv4Prefix).c_str(),
                            &Net4, &iPrefixLength);
    if (RT_FAILURE(rc))
    {
        reportError("Failed to parse IPv4 prefix %ls\n", bstrIPv4Prefix.raw());
        return rc;
    }

    if (iPrefixLength > 30 || 0 >= iPrefixLength)
    {
        reportError("Invalid IPv4 prefix length %d\n", iPrefixLength);
        return VERR_INVALID_PARAMETER;
    }

    rc = RTNetPrefixToMaskIPv4(iPrefixLength, &Mask4);
    AssertRCReturn(rc, rc);

    /** @todo r=uwe Check the address is unicast, not a loopback, etc. */

    RTNETADDRIPV4 Addr4;
    Addr4.u = Net4.u | RT_H2N_U32_C(0x00000001);

    memcpy(&m_ProxyOptions.vnetwork, &Net4, sizeof(in_addr));
    memcpy(&m_ProxyOptions.vnetmask, &Mask4, sizeof(in_addr));
    memcpy(&m_ProxyOptions.vhost,    &Addr4, sizeof(in_addr));

    /*
     * IPv4 Nameservers
     */
    PCRTNETADDRIPV4 acNameservers = getHostNameservers();
    //populate only first entry
    /** @todo r=jack: fix that in libslirp. */
    if (acNameservers != NULL &&
        !((acNameservers[0].u & RT_H2N_U32_C(IN_CLASSA_NET))
          == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET)))
    {
        memcpy(&m_ProxyOptions.vnameserver, acNameservers, sizeof(RTNETADDRIPV4));
        // m_ProxyOptions.disable_dns = true;
    }
    else
    {
        if (   acNameservers
            && (acNameservers[0].u & RT_H2N_U32_C(IN_CLASSA_NET)) == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET))
            LogRel(("Nameserver is on 127/8 network."
                    "Falling back to libslirp DNS proxy.\n"));
        else
            LogRel(("Failed to obtain IPv4 nameservers from host."
                    "Falling back to libslirp DNS proxy.\n"));

        RTNETADDRIPV4 Nameserver4;
        Nameserver4.u = Net4.u | RT_H2N_U32_C(0x00000003);

        memcpy(&m_ProxyOptions.vnameserver, &Nameserver4, sizeof(in_addr));
        LogRel(("nameserver: %u", Nameserver4.u));
    }

    if (acNameservers)
    {
        RTMemFree((PRTNETADDRIPV4)acNameservers);
        acNameservers = NULL;
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
            m_ProxyOptions.outbound_addr->sin_addr.s_addr = addr.u;

            LogRel(("Will use %RTnaipv4 as IPv4 source address\n",
                    m_src4.sin_addr.s_addr));
        }
        else
        {
            LogRel(("Failed to parse \"%s\" IPv4 source address specification\n",
                    strSourceIp4.c_str()));
        }
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
        reportComError(m_net, "IPv6Enabled", hrc);
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
        reportComError(m_net, "IPv6Prefix", hrc);
        return VERR_GENERAL_FAILURE;
    }

    RTNETADDRIPV6 Net6;
    int iPrefixLength;
    rc = RTNetStrToIPv6Cidr(com::Utf8Str(bstrIPv6Prefix).c_str(),
                            &Net6, &iPrefixLength);
    if (RT_FAILURE(rc))
    {
        reportError("Failed to parse IPv6 prefix %ls\n", bstrIPv6Prefix.raw());
        return rc;
    }

    /* Allow both addr:: and addr::/64 */
    if (iPrefixLength == 128)   /* no length was specified after the address? */
        iPrefixLength = 64;     /*   take it to mean /64 which we require anyway */
    else if (iPrefixLength != 64)
    {
        reportError("Invalid IPv6 prefix length %d,"
                    " must be 64.\n", iPrefixLength);
        return rc;
    }

    /* Verify the address is unicast. */
    if (   ((Net6.au8[0] & 0xe0) != 0x20)  /* global 2000::/3 */
        && ((Net6.au8[0] & 0xfe) != 0xfc)) /* local  fc00::/7 */
    {
        reportError("IPv6 prefix %RTnaipv6 is not unicast.\n", &Net6);
        return VERR_INVALID_PARAMETER;
    }

    /* Verify the interfaces ID part is zero */
    if (Net6.au64[1] != 0)
    {
        reportError("Non-zero bits in the interface ID part"
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
        reportComError(m_net, "AdvertiseDefaultIPv6RouteEnabled", hrc);
        return VERR_GENERAL_FAILURE;
    }

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

            LogRel(("Will use %RTnaipv6 as IPv6 source address\n",
                    m_src6.sin6_addr.s6_addr));
        }
        else
        {
            LogRel(("Failed to parse \"%s\" IPv6 source address specification\n",
                    strSourceIp6.c_str()));
        }
    }

#if 0 /** @todo */
    m_ProxyOptions.ipv6_defroute = fIPv6DefaultRoute;


    // /* Raw socket for ICMP. */
    // initIPv6RawSock();

#endif

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
        VBoxNetSlirpNAT::reportComError(m_pListenerImpl, "init", hrc);
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
        VBoxNetSlirpNAT::reportComError(pEventful, "EventSource", hrc);
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
        VBoxNetSlirpNAT::reportComError(m_pEventSource, "RegisterListener", hrc);
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
        VBoxNetSlirpNAT::reportComError(pEventSource, "UnregisterListener", hrc);
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

    /* Spawn the I/O polling thread. */
    int rc = RTThreadCreate(&m_hThrdPoll,
                            VBoxNetSlirpNAT::pollThread, this,
                            0, /* :cbStack */
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                            "Poll");
    AssertRCReturn(rc, rc);

    /* spawn intnet input pump */
    rc = RTThreadCreate(&m_hThrRecv,
                        VBoxNetSlirpNAT::receiveThread, this,
                        0, /* :cbStack */
                        RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                        "RECV");
    AssertRCReturn(rc, rc);

    /* main thread will run the API event queue pump */
    com::NativeEventQueue *pQueue = com::NativeEventQueue::getMainEventQueue();
    if (pQueue == NULL)
    {
        LogRel(("run: getMainEventQueue() == NULL\n"));
        return VERR_GENERAL_FAILURE;
    }

    /* dispatch API events to our listeners */
    for (;;)
    {
        rc = pQueue->processEventQueue(RT_INDEFINITE_WAIT);
        if (rc == VERR_INTERRUPTED)
        {
            LogRel(("run: shutdown\n"));
            break;
        }
        else if (rc != VINF_SUCCESS)
        {
            /* note any unexpected rc */
            LogRel(("run: processEventQueue: %Rrc\n", rc));
        }
    }

    /*
     * We are out of the event loop, so we were told to shut down.
     * Tell other threads to wrap up.
     */

    /* tell the intnet input pump to terminate */
    IntNetR3IfWaitAbort(m_hIf);

    rc = RTThreadWait(m_hThrRecv, 5000, NULL);
    m_hThrRecv = NIL_RTTHREAD;

    return rc;
}


void
VBoxNetSlirpNAT::shutdown()
{
    int rc;

    com::NativeEventQueue *pQueue = com::NativeEventQueue::getMainEventQueue();
    if (pQueue == NULL)
    {
        LogRel(("shutdown: getMainEventQueue() == NULL\n"));
        return;
    }

    /* unregister listeners */
    m_ListenerNATNet.unlisten();
    m_ListenerVirtualBox.unlisten();
    m_ListenerVBoxClient.unlisten();

    /* tell the event loop in run() to stop */
    rc = pQueue->interruptEventQueueProcessing();
    if (RT_FAILURE(rc))
        LogRel(("shutdown: interruptEventQueueProcessing: %Rrc\n", rc));
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
                    LogRel(("Event: %s %s port-forwarding rule \"%s\": invalid protocol %d\n",
                            fCreateFW ? "Add" : "Remove",
                            fIPv6FW ? "IPv6" : "IPv4",
                            com::Utf8Str(name).c_str(),
                            (int)proto));
                    goto port_forward_done;
            }

            LogRel(("Event: %s %s port-forwarding rule \"%s\": %s %s%s%s:%d -> %s%s%s:%d\n",
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
                    lGuestPort));

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
            PCRTNETADDRIPV4 acNameservers = getHostNameservers();
            //populate only first entry
            /** @todo r=jack: fix that in libslirp. */
            if (acNameservers != NULL &&
                !((acNameservers[0].u & RT_H2N_U32_C(IN_CLASSA_NET))
                  == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET)))
            {
                memcpy(&m_ProxyOptions.vnameserver, acNameservers, sizeof(RTNETADDRIPV4));
                slirp_set_vnameserver(m_pSlirp, m_ProxyOptions.vnameserver);
                // m_ProxyOptions.disable_dns = true;
                // slirp_set_disable_dns(m_pSlirp, m_ProxyOptions.disable_dns);
            }
            else
            {
                if((acNameservers[0].u & RT_H2N_U32_C(IN_CLASSA_NET))
                    == RT_N2H_U32_C(INADDR_LOOPBACK & IN_CLASSA_NET))
                    LogRel(("Nameserver is on 127/8 network."
                            "Falling back to libslirp DNS proxy.\n"));
                else
                    LogRel(("Failed to obtain IPv4 nameservers from host."
                            "Falling back to libslirp DNS proxy.\n"));

                RTNETADDRIPV4 Nameserver4;
                Nameserver4.u = m_ProxyOptions.vnetwork.s_addr | RT_H2N_U32_C(0x00000003);

                memcpy(&m_ProxyOptions.vnameserver, &Nameserver4, sizeof(in_addr));
                slirp_set_vnameserver(m_pSlirp, m_ProxyOptions.vnameserver);
                // m_ProxyOptions.disable_dns = false;
                // slirp_set_disable_dns(m_pSlirp, m_ProxyOptions.disable_dns);
            }

            if (acNameservers)
            {
                RTMemFree((PRTNETADDRIPV4)acNameservers);
                acNameservers = NULL;
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
            LogRel(("VBoxSVC became unavailable, exiting.\n"));
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
 */
PCRTNETADDRIPV4 VBoxNetSlirpNAT::getHostNameservers()
{
    if (m_host.isNull())
        return NULL;

    com::SafeArray<BSTR> aRawNameservers;
    HRESULT hrc = m_host->COMGETTER(NameServers)(ComSafeArrayAsOutParam(aRawNameservers));
    if (FAILED(hrc))
        return NULL;

    const size_t cNameservers = aRawNameservers.size();
    if (cNameservers == 0)
        return NULL;

    PRTNETADDRIPV4 aNameservers =
        (PRTNETADDRIPV4)RTMemAllocZ(sizeof(RTNETADDRIPV4) * cNameservers);
    if (aNameservers == NULL)
        return NULL;

    size_t idx = 0;
    for (; idx < cNameservers; idx++)
    {
        RTNETADDRIPV4 tmpNameserver;
        int rc = RTNetStrToIPv4Addr(com::Utf8Str(aRawNameservers[idx]).c_str(), &tmpNameserver);
        if (RT_FAILURE(rc))
        {
            LogRel(("Failed to parse IPv4 nameserver %ls\n", aNameservers[idx]));
            return NULL;
        }

        memcpy(&aNameservers[idx], &tmpNameserver, sizeof(RTNETADDRIPV4));
    }

    if (idx == 0)
    {
        RTMemFree(aNameservers);
        return NULL;
    }

    /** @todo r=jack: fix this in libslirp. */
    if (idx > 1)
        LogRel(("NAT Network: More than one IPv4 nameserver detected. Due to current "
                "libslirp limitations, only the first entry  will be provided to the guest.\n"));

    return aNameservers;
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

        LogRel(("Loading %s port-forwarding rule \"%s\": %s %s%s%s:%d -> %s%s%s:%d\n",
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
                natPf.Pfr.u16PfrGuestPort));

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
        LogRel(("Unable to convert guest address '%s' for %s rule \"%s\"\n",
                pszGuestAddr, pNatPf->fPfrIPv6 ? "IPv6" : "IPv4",
                pNatPf->szPfrName));
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
        LogRel(("Unable to %s %s rule \"%s\"\n",
                fRemove ? "remove" : "add",
                pNatPf->fPfrIPv6 ? "IPv6" : "IPv4",
                pNatPf->szPfrName));

    /* Free the rule in any case. */
    RTMemFree(pNatPf);
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

    if (pThis->nsock + 1 >= pThis->uPollCap)
    {
        size_t cbNew = pThis->uPollCap * 2 * sizeof(struct pollfd);
        struct pollfd *pvNew = (struct pollfd *)RTMemRealloc(pThis->polls, cbNew);
        if (pvNew)
        {
            pThis->polls = pvNew;
            pThis->uPollCap *= 2;
        }
        else
            return -1;
    }

    unsigned int uIdx = pThis->nsock;
    Assert(uIdx < INT_MAX);
    pThis->polls[uIdx].fd = hFd;
    pThis->polls[uIdx].events = pollEventSlirpToHost(iEvents);
    pThis->polls[uIdx].revents = 0;
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
    struct pollfd* polls = pThis->polls;
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
    pThis->polls[0].fd = fdNativeReadPipe;
    pThis->polls[0].events = POLLRDNORM | POLLPRI | POLLRDBAND;
    pThis->polls[0].revents = 0;
#endif /* !RT_OS_WINDOWS */

    /*
     * Polling loop.
     */
    for (;;)
    {
        /*
         * To prevent concurrent execution of sending/receiving threads
         */
        pThis->nsock = 1;

        uint32_t cMsTimeout = DRVNAT_DEFAULT_TIMEOUT;
        slirp_pollfds_fill_socket(pThis->m_pSlirp, &cMsTimeout, pThis->slirpAddPollCb /* SlirpAddPollCb */, pThis /* opaque */);
        cMsTimeout = pThis->slirpTimersAdjustTimeoutDown(cMsTimeout);

#ifdef RT_OS_WINDOWS
        int cChangedFDs = WSAPoll(pThis->polls, pThis->nsock, cMsTimeout);
#else
        int cChangedFDs = poll(pThis->polls, pThis->nsock, cMsTimeout);
#endif
        if (cChangedFDs < 0)
        {
#ifdef RT_OS_WINDOWS
            int const iLastErr = WSAGetLastError(); /* (In debug builds LogRel translates to two RTLogLoggerExWeak calls.) */
            LogRel(("NAT: RTWinPoll returned error=%Rrc (cChangedFDs=%d)\n", iLastErr, cChangedFDs));
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
                LogRel(("NAT: Poll returns (%s) suppressed %d\n", strerror(errno), cPollNegRet));
                cPollNegRet = 0;
            }
#endif
        }

        Log4(("%s: poll\n", __FUNCTION__));
        slirp_pollfds_poll(pThis->m_pSlirp, cChangedFDs < 0, pThis->slirpGetREventsCb, pThis /* opaque */);

        /*
         * Drain the control pipe if necessary.
         */
        if (pThis->polls[0].revents & (POLLRDNORM|POLLPRI|POLLRDBAND))   /* POLLPRI won't be seen with WSAPoll. */
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

        /* process _all_ outstanding requests but don't wait */
        RTReqQueueProcess(pThis->m_hSlirpReqQueue, 0);
        pThis->timersRunExpired();
    }

#if 0
    LogRel(("pollThread: Exiting\n"));
    return VERR_INVALID_STATE;
#endif
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

    LogRel(("receiveThread: IntNetR3IfPumpPkts: unexpected %Rrc\n", rc));
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
        reportComError(virtualbox, "GetExtraData", hrc);
        return VERR_GENERAL_FAILURE;
    }

    strValueOut = bstrValue;
    return VINF_SUCCESS;
}


/* static */
HRESULT VBoxNetSlirpNAT::reportComError(ComPtr<IUnknown> iface,
                                       const com::Utf8Str &strContext,
                                       HRESULT hrc)
{
    const com::ErrorInfo info(iface, COM_IIDOF(IUnknown));
    if (info.isFullAvailable() || info.isBasicAvailable())
    {
        reportErrorInfoList(info, strContext);
    }
    else
    {
        if (strContext.isNotEmpty())
            reportError("%s: %Rhra", strContext.c_str(), hrc);
        else
            reportError("%Rhra", hrc);
    }

    return hrc;
}


/* static */
void VBoxNetSlirpNAT::reportErrorInfoList(const com::ErrorInfo &info,
                                         const com::Utf8Str &strContext)
{
    if (strContext.isNotEmpty())
        reportError("%s", strContext.c_str());

    bool fFirst = true;
    for (const com::ErrorInfo *pInfo = &info;
         pInfo != NULL;
         pInfo = pInfo->getNext())
    {
        if (fFirst)
            fFirst = false;
        else
            reportError("--------");

        reportErrorInfo(*pInfo);
    }
}


/* static */
void VBoxNetSlirpNAT::reportErrorInfo(const com::ErrorInfo &info)
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

    reportError("%s", message.c_str());
}


/* static */
void VBoxNetSlirpNAT::reportError(const char *a_pcszFormat, ...)
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
 * component.  By default the release log is written to the file
 * ~/.VirtualBox/${netname}.log but its destiation and content can be
 * overridden with VBOXNET_${netname}_RELEASE_LOG family of
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
                               RTLOGFLAGS_PREFIX_TIME_PROG,
                               "all all.restrict -default.restrict",
                               pcszEnvVarBase,
                               RTLOGDEST_FILE,
                               32768 /* cMaxEntriesPerGroup */,
                               0 /* cHistory */,
                               0 /* uHistoryFileTime */,
                               0 /* uHistoryFileSize */,
                               NULL /*pErrInfo*/);

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
    LogFlowFuncEnter();
    NOREF(envp);

#ifdef RT_OS_WINDOWS
    WSADATA WsaData = {0};
    int err = WSAStartup(MAKEWORD(2,2), &WsaData);
    if (err)
    {
        fprintf(stderr, "wsastartup: failed (%d)\n", err);
        return RTEXITCODE_INIT;
    }
#endif

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

    NAT.run();

    LogRel(("Terminating\n"));
    return RTEXITCODE_SUCCESS;
}


#ifndef VBOX_WITH_HARDENING

int main(int argc, char **argv, char **envp)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
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
