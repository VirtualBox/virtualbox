/* $Id: VBoxNetLwipNAT.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetNAT - NAT Service for connecting to IntNet.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

/* Must be included before winutils.h (lwip/def.h), otherwise Windows build breaks. */
#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#include "winutils.h"

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

extern "C"
{
/* bunch of LWIP headers */
#include "lwip/sys.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ethip6.h"
#include "lwip/nd6.h"           // for proxy_na_hook
#include "lwip/mld6.h"
#include "lwip/tcpip.h"
#include "netif/etharp.h"

#include "proxy.h"
#include "pxremap.h"
#include "portfwd.h"
}

#include "VBoxLwipCore.h"

#ifdef VBOX_RAWSOCK_DEBUG_HELPER
#if    defined(VBOX_WITH_HARDENING) /* obviously */     \
    || defined(RT_OS_WINDOWS)       /* not used */      \
    || defined(RT_OS_DARWIN)        /* not necessary */
# error Have you forgotten to turn off VBOX_RAWSOCK_DEBUG_HELPER?
#endif
/* ask the privileged helper to create a raw socket for us */
extern "C" int getrawsock(int type);
#endif



typedef struct NATSERVICEPORTFORWARDRULE
{
    PORTFORWARDRULE Pfr;
    fwspec         FWSpec;
} NATSERVICEPORTFORWARDRULE, *PNATSERVICEPORTFORWARDRULE;

typedef std::vector<NATSERVICEPORTFORWARDRULE> VECNATSERVICEPF;
typedef VECNATSERVICEPF::iterator ITERATORNATSERVICEPF;
typedef VECNATSERVICEPF::const_iterator CITERATORNATSERVICEPF;


class VBoxNetLwipNAT
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

    /** Home folder location; used as default directory for several paths. */
    com::Utf8Str m_strHome;

    struct proxy_options m_ProxyOptions;
    struct sockaddr_in m_src4;
    struct sockaddr_in6 m_src6;
    /**
     * place for registered local interfaces.
     */
    ip4_lomap m_lo2off[10];
    ip4_lomap_desc m_loOptDescriptor;

    uint16_t m_u16Mtu;
    netif m_LwipNetIf;

    VECNATSERVICEPF m_vecPortForwardRule4;
    VECNATSERVICEPF m_vecPortForwardRule6;

    class Listener
    {
        class Adapter;
        typedef ListenerImpl<Adapter, VBoxNetLwipNAT *> Impl;

        ComObjPtr<Impl> m_pListenerImpl;
        ComPtr<IEventSource> m_pEventSource;

    public:
        HRESULT init(VBoxNetLwipNAT *pNAT);
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
    VBoxNetLwipNAT();
    ~VBoxNetLwipNAT();

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

    static void reportError(const char *a_pcszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

    static HRESULT reportComError(ComPtr<IUnknown> iface,
                                  const com::Utf8Str &strContext,
                                  HRESULT hrc);
    static void reportErrorInfoList(const com::ErrorInfo &info,
                                    const com::Utf8Str &strContext);
    static void reportErrorInfo(const com::ErrorInfo &info);

    void initIPv4RawSock();
    void initIPv6RawSock();

    static DECLCALLBACK(void) onLwipTcpIpInit(void *arg);
    static DECLCALLBACK(void) onLwipTcpIpFini(void *arg);
    static DECLCALLBACK(err_t) netifInit(netif *pNetif) RT_NOTHROW_PROTO;

    HRESULT HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent);

    const char **getHostNameservers();

    int fetchNatPortForwardRules(VECNATSERVICEPF &vec, bool fIsIPv6);
    static int natServiceProcessRegisteredPf(VECNATSERVICEPF &vecPf);
    static int natServicePfRegister(NATSERVICEPORTFORWARDRULE &natServicePf);

    static DECLCALLBACK(int) receiveThread(RTTHREAD hThreadSelf, void *pvUser);

    /* input from intnet */
    static DECLCALLBACK(void) processFrame(void *pvUser, void *pvFrame, uint32_t cbFrame);

    /* output to intnet */
    static DECLCALLBACK(err_t) netifLinkoutput(netif *pNetif, pbuf *pBuf) RT_NOTHROW_PROTO;
};



VBoxNetLwipNAT::VBoxNetLwipNAT()
  : m_uVerbosity(0),
    m_hIf(NULL),
    m_hThrRecv(NIL_RTTHREAD),
    m_u16Mtu(1500)
{
    LogFlowFuncEnter();

    RT_ZERO(m_ProxyOptions.ipv4_addr);
    RT_ZERO(m_ProxyOptions.ipv4_mask);
    RT_ZERO(m_ProxyOptions.ipv6_addr);
    m_ProxyOptions.ipv6_enabled = 0;
    m_ProxyOptions.ipv6_defroute = 0;
    m_ProxyOptions.icmpsock4 = INVALID_SOCKET;
    m_ProxyOptions.icmpsock6 = INVALID_SOCKET;
    m_ProxyOptions.tftp_root = NULL;
    m_ProxyOptions.src4 = NULL;
    m_ProxyOptions.src6 = NULL;
    RT_ZERO(m_src4);
    RT_ZERO(m_src6);
    m_src4.sin_family = AF_INET;
    m_src6.sin6_family = AF_INET6;
#if HAVE_SA_LEN
    m_src4.sin_len = sizeof(m_src4);
    m_src6.sin6_len = sizeof(m_src6);
#endif
    m_ProxyOptions.lomap_desc = NULL;
    m_ProxyOptions.nameservers = NULL;

    m_LwipNetIf.name[0] = 'N';
    m_LwipNetIf.name[1] = 'T';

    m_MacAddress.au8[0] = 0x52;
    m_MacAddress.au8[1] = 0x54;
    m_MacAddress.au8[2] = 0;
    m_MacAddress.au8[3] = 0x12;
    m_MacAddress.au8[4] = 0x35;
    m_MacAddress.au8[5] = 0;

    RT_ZERO(m_lo2off);
    m_loOptDescriptor.lomap = NULL;
    m_loOptDescriptor.num_lomap = 0;

    LogFlowFuncLeave();
}


VBoxNetLwipNAT::~VBoxNetLwipNAT()
{
    if (m_ProxyOptions.tftp_root)
    {
        RTStrFree((char *)m_ProxyOptions.tftp_root);
        m_ProxyOptions.tftp_root = NULL;
    }
    if (m_ProxyOptions.nameservers)
    {
        const char **pv = m_ProxyOptions.nameservers;
        while (*pv)
        {
            RTStrFree((char*)*pv);
            pv++;
        }
        RTMemFree(m_ProxyOptions.nameservers);
        m_ProxyOptions.nameservers = NULL;
    }
}


/**
 * Command line options.
 */
RTGETOPTDEF VBoxNetLwipNAT::s_aGetOptDef[] =
{
    { "--network",              'n',   RTGETOPT_REQ_STRING },
    { "--verbose",              'v',   RTGETOPT_REQ_NOTHING },
};


/** Icky hack to tell the caller it should exit with RTEXITCODE_SUCCESS */
#define RTEXITCODE_DONE RTEXITCODE_32BIT_HACK

RTEXITCODE
VBoxNetLwipNAT::usage()
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
VBoxNetLwipNAT::parseArgs(int argc, char *argv[])
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
int VBoxNetLwipNAT::init()
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


    fetchNatPortForwardRules(m_vecPortForwardRule4, /* :fIsIPv6 */ false);
    if (m_ProxyOptions.ipv6_enabled)
        fetchNatPortForwardRules(m_vecPortForwardRule6, /* :fIsIPv6 */ true);


    if (m_strHome.isNotEmpty())
    {
        com::Utf8StrFmt strTftpRoot("%s%c%s", m_strHome.c_str(), RTPATH_DELIMITER, "TFTP");
        char *pszStrTemp;       // avoid const char ** vs char **
        rc = RTStrUtf8ToCurrentCP(&pszStrTemp, strTftpRoot.c_str());
        AssertRC(rc);
        m_ProxyOptions.tftp_root = pszStrTemp;
    }

    m_ProxyOptions.nameservers = getHostNameservers();

    initComEvents();
    /* end of COM initialization */

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
int VBoxNetLwipNAT::initCom()
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
int VBoxNetLwipNAT::initHome()
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
int VBoxNetLwipNAT::initIPv4()
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

    memcpy(&m_ProxyOptions.ipv4_addr, &Addr4, sizeof(ip_addr));
    memcpy(&m_ProxyOptions.ipv4_mask, &Mask4, sizeof(ip_addr));


    /* Raw socket for ICMP. */
    initIPv4RawSock();


    /* IPv4 source address (host), if configured. */
    com::Utf8Str strSourceIp4;
    rc = getExtraData(strSourceIp4, "SourceIp4");
    if (RT_SUCCESS(rc) && strSourceIp4.isNotEmpty())
    {
        RTNETADDRIPV4 addr;
        rc = RTNetStrToIPv4Addr(strSourceIp4.c_str(), &addr);
        if (RT_SUCCESS(rc))
        {
            m_src4.sin_addr.s_addr = addr.u;
            m_ProxyOptions.src4 = &m_src4;

            LogRel(("Will use %RTnaipv4 as IPv4 source address\n",
                    m_src4.sin_addr.s_addr));
        }
        else
        {
            LogRel(("Failed to parse \"%s\" IPv4 source address specification\n",
                    strSourceIp4.c_str()));
        }
    }

    /* Make host's loopback(s) available from inside the natnet */
    initIPv4LoopbackMap();

    return VINF_SUCCESS;
}


/**
 * Create raw IPv4 socket for sending and snooping ICMP.
 */
void VBoxNetLwipNAT::initIPv4RawSock()
{
    SOCKET icmpsock4 = INVALID_SOCKET;

#ifndef RT_OS_DARWIN
    const int icmpstype = SOCK_RAW;
#else
    /* on OS X it's not privileged */
    const int icmpstype = SOCK_DGRAM;
#endif

    icmpsock4 = socket(AF_INET, icmpstype, IPPROTO_ICMP);
    if (icmpsock4 == INVALID_SOCKET)
    {
        perror("IPPROTO_ICMP");
#ifdef VBOX_RAWSOCK_DEBUG_HELPER
        icmpsock4 = getrawsock(AF_INET);
#endif
    }

    if (icmpsock4 != INVALID_SOCKET)
    {
#ifdef ICMP_FILTER              //  Linux specific
        struct icmp_filter flt = {
            ~(uint32_t)(
                  (1U << ICMP_ECHOREPLY)
                | (1U << ICMP_DEST_UNREACH)
                | (1U << ICMP_TIME_EXCEEDED)
            )
        };

        int status = setsockopt(icmpsock4, SOL_RAW, ICMP_FILTER,
                                &flt, sizeof(flt));
        if (status < 0)
        {
            perror("ICMP_FILTER");
        }
#endif
    }

    m_ProxyOptions.icmpsock4 = icmpsock4;
}


/**
 * Init mapping from the natnet's IPv4 addresses to host's IPv4
 * loopbacks.  Plural "loopbacks" because it's now quite common to run
 * services on loopback addresses other than 127.0.0.1.  E.g. a
 * caching dns proxy on 127.0.1.1 or 127.0.0.53.
 */
int VBoxNetLwipNAT::initIPv4LoopbackMap()
{
    HRESULT hrc;
    int rc;

    com::SafeArray<BSTR> aStrLocalMappings;
    hrc = m_net->COMGETTER(LocalMappings)(ComSafeArrayAsOutParam(aStrLocalMappings));
    if (FAILED(hrc))
    {
        reportComError(m_net, "LocalMappings", hrc);
        return VERR_GENERAL_FAILURE;
    }

    if (aStrLocalMappings.size() == 0)
        return VINF_SUCCESS;


    /* netmask in host order, to verify the offsets */
    uint32_t uMask = RT_N2H_U32(ip4_addr_get_u32(&m_ProxyOptions.ipv4_mask));


    /*
     * Process mappings of the form "127.x.y.z=off"
     */
    unsigned int dst = 0;      /* typeof(ip4_lomap_desc::num_lomap) */
    for (size_t i = 0; i < aStrLocalMappings.size(); ++i)
    {
        com::Utf8Str strMapping(aStrLocalMappings[i]);
        const char *pcszRule = strMapping.c_str();
        LogRel(("IPv4 loopback mapping %zu: %s\n", i, pcszRule));

        RTNETADDRIPV4 Loopback4;
        char *pszNext;
        rc = RTNetStrToIPv4AddrEx(pcszRule, &Loopback4, &pszNext);
        if (RT_FAILURE(rc))
        {
            LogRel(("Failed to parse IPv4 address: %Rra\n", rc));
            continue;
        }

        if (Loopback4.au8[0] != 127)
        {
            LogRel(("Not an IPv4 loopback address\n"));
            continue;
        }

        if (rc != VWRN_TRAILING_CHARS)
        {
            LogRel(("Missing right hand side\n"));
            continue;
        }

        pcszRule = RTStrStripL(pszNext);
        if (*pcszRule != '=')
        {
            LogRel(("Invalid rule format\n"));
            continue;
        }

        pcszRule = RTStrStripL(pcszRule+1);
        if (*pszNext == '\0')
        {
            LogRel(("Empty right hand side\n"));
            continue;
        }

        uint32_t u32Offset;
        rc = RTStrToUInt32Ex(pcszRule, &pszNext, 10, &u32Offset);
        if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        {
            LogRel(("Invalid offset\n"));
            continue;
        }

        if (u32Offset <= 1 || u32Offset == ~uMask)
        {
            LogRel(("Offset maps to a reserved address\n"));
            continue;
        }

        if ((u32Offset & uMask) != 0)
        {
            LogRel(("Offset exceeds the network size\n"));
            continue;
        }

        if (dst >= RT_ELEMENTS(m_lo2off))
        {
            LogRel(("Ignoring the mapping, too many mappings already\n"));
            continue;
        }

        ip4_addr_set_u32(&m_lo2off[dst].loaddr, Loopback4.u);
        m_lo2off[dst].off = u32Offset;
        ++dst;
    }

    if (dst > 0)
    {
        m_loOptDescriptor.lomap = m_lo2off;
        m_loOptDescriptor.num_lomap = dst;
        m_ProxyOptions.lomap_desc = &m_loOptDescriptor;
    }

    return VINF_SUCCESS;
}


/*
 * Read IPv6 related settings and do necessary initialization.  These
 * settings will be picked up by the proxy on the lwIP thread.  See
 * onLwipTcpIpInit().
 */
int VBoxNetLwipNAT::initIPv6()
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

    m_ProxyOptions.ipv6_enabled = !!fIPv6Enabled;
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

    /* Use ...::1 as our address */
    RTNETADDRIPV6 Addr6 = Net6;
    Addr6.au8[15] = 0x01;
    memcpy(&m_ProxyOptions.ipv6_addr, &Addr6, sizeof(ip6_addr_t));


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

    m_ProxyOptions.ipv6_defroute = fIPv6DefaultRoute;


    /* Raw socket for ICMP. */
    initIPv6RawSock();


    /* IPv6 source address, if configured. */
    com::Utf8Str strSourceIp6;
    rc = getExtraData(strSourceIp6, "SourceIp6");
    if (RT_SUCCESS(rc) && strSourceIp6.isNotEmpty())
    {
        RTNETADDRIPV6 addr;
        char *pszZone = NULL;
        rc = RTNetStrToIPv6Addr(strSourceIp6.c_str(), &addr, &pszZone);
        if (RT_SUCCESS(rc))
        {
            memcpy(&m_src6.sin6_addr, &addr, sizeof(addr));
            m_ProxyOptions.src6 = &m_src6;

            LogRel(("Will use %RTnaipv6 as IPv6 source address\n",
                    &m_src6.sin6_addr));
        }
        else
        {
            LogRel(("Failed to parse \"%s\" IPv6 source address specification\n",
                    strSourceIp6.c_str()));
        }
    }

    return VINF_SUCCESS;
}


/**
 * Create raw IPv6 socket for sending and snooping ICMP6.
 */
void VBoxNetLwipNAT::initIPv6RawSock()
{
    SOCKET icmpsock6 = INVALID_SOCKET;

#ifndef RT_OS_DARWIN
    const int icmpstype = SOCK_RAW;
#else
    /* on OS X it's not privileged */
    const int icmpstype = SOCK_DGRAM;
#endif

    icmpsock6 = socket(AF_INET6, icmpstype, IPPROTO_ICMPV6);
    if (icmpsock6 == INVALID_SOCKET)
    {
        perror("IPPROTO_ICMPV6");
#ifdef VBOX_RAWSOCK_DEBUG_HELPER
        icmpsock6 = getrawsock(AF_INET6);
#endif
    }

    if (icmpsock6 != INVALID_SOCKET)
    {
#ifdef ICMP6_FILTER             // Windows doesn't support RFC 3542 API
        /*
         * XXX: We do this here for now, not in pxping.c, to avoid
         * name clashes between lwIP and system headers.
         */
        struct icmp6_filter flt;
        ICMP6_FILTER_SETBLOCKALL(&flt);

        ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &flt);

        ICMP6_FILTER_SETPASS(ICMP6_DST_UNREACH, &flt);
        ICMP6_FILTER_SETPASS(ICMP6_PACKET_TOO_BIG, &flt);
        ICMP6_FILTER_SETPASS(ICMP6_TIME_EXCEEDED, &flt);
        ICMP6_FILTER_SETPASS(ICMP6_PARAM_PROB, &flt);

        int status = setsockopt(icmpsock6, IPPROTO_ICMPV6, ICMP6_FILTER,
                                &flt, sizeof(flt));
        if (status < 0)
        {
            perror("ICMP6_FILTER");
        }
#endif
    }

    m_ProxyOptions.icmpsock6 = icmpsock6;
}



/**
 * Adapter for the ListenerImpl template.  It has to be a separate
 * object because ListenerImpl deletes it.  Just a small wrapper that
 * delegates the real work back to VBoxNetLwipNAT.
 */
class VBoxNetLwipNAT::Listener::Adapter
{
    VBoxNetLwipNAT *m_pNAT;
public:
    Adapter() : m_pNAT(NULL) {}
    HRESULT init() { return init(NULL); }
    void uninit() { m_pNAT = NULL; }

    HRESULT init(VBoxNetLwipNAT *pNAT)
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
VBoxNetLwipNAT::Listener::init(VBoxNetLwipNAT *pNAT)
{
    HRESULT hrc;

    hrc = m_pListenerImpl.createObject();
    if (FAILED(hrc))
        return hrc;

    hrc = m_pListenerImpl->init(new Adapter(), pNAT);
    if (FAILED(hrc))
    {
        VBoxNetLwipNAT::reportComError(m_pListenerImpl, "init", hrc);
        return hrc;
    }

    return hrc;
}


void
VBoxNetLwipNAT::Listener::uninit()
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
VBoxNetLwipNAT::Listener::listen(const ComPtr<IEventful> &pEventful,
                                         const VBoxEventType_T aEvents[])
{
    HRESULT hrc;

    if (m_pListenerImpl.isNull())
        return S_OK;

    ComPtr<IEventSource> pEventSource;
    hrc = pEventful->COMGETTER(EventSource)(pEventSource.asOutParam());
    if (FAILED(hrc))
    {
        VBoxNetLwipNAT::reportComError(pEventful, "EventSource", hrc);
        return hrc;
    }

    /* got a real interface, punt to the non-template code */
    hrc = doListen(pEventSource, aEvents);
    if (FAILED(hrc))
        return hrc;

    return hrc;
}


HRESULT
VBoxNetLwipNAT::Listener::doListen(const ComPtr<IEventSource> &pEventSource,
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
        VBoxNetLwipNAT::reportComError(m_pEventSource, "RegisterListener", hrc);
        return hrc;
    }

    m_pEventSource = pEventSource;
    return hrc;
}


HRESULT
VBoxNetLwipNAT::Listener::unlisten()
{
    HRESULT hrc;

    if (m_pEventSource.isNull())
        return S_OK;

    const ComPtr<IEventSource> pEventSource = m_pEventSource;
    m_pEventSource.setNull();

    hrc = pEventSource->UnregisterListener(m_pListenerImpl);
    if (FAILED(hrc))
    {
        VBoxNetLwipNAT::reportComError(pEventSource, "UnregisterListener", hrc);
        return hrc;
    }

    return hrc;
}



/**
 * Create and register API event listeners.
 */
int VBoxNetLwipNAT::initComEvents()
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
 * Perform lwIP initialization on the lwIP "tcpip" thread.
 *
 * The lwIP thread was created in init() and this function is run
 * before the main lwIP loop is started.  It is responsible for
 * setting up lwIP state, configuring interface(s), etc.
 a*/
/*static*/
DECLCALLBACK(void) VBoxNetLwipNAT::onLwipTcpIpInit(void *arg)
{
    AssertPtrReturnVoid(arg);
    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(arg);

    HRESULT hrc = com::Initialize();
    AssertComRCReturnVoid(hrc);

    proxy_arp_hook = pxremap_proxy_arp;
    proxy_ip4_divert_hook = pxremap_ip4_divert;

    proxy_na_hook = pxremap_proxy_na;
    proxy_ip6_divert_hook = pxremap_ip6_divert;

    netif *pNetif = netif_add(&self->m_LwipNetIf /* Lwip Interface */,
                              &self->m_ProxyOptions.ipv4_addr, /* IP address*/
                              &self->m_ProxyOptions.ipv4_mask, /* Network mask */
                              &self->m_ProxyOptions.ipv4_addr, /* XXX: Gateway address */
                              self /* state */,
                              VBoxNetLwipNAT::netifInit /* netif_init_fn */,
                              tcpip_input /* netif_input_fn */);

    AssertPtrReturnVoid(pNetif);

    LogRel(("netif %c%c%d: mac %RTmac\n",
            pNetif->name[0], pNetif->name[1], pNetif->num,
            pNetif->hwaddr));
    LogRel(("netif %c%c%d: inet %RTnaipv4 netmask %RTnaipv4\n",
            pNetif->name[0], pNetif->name[1], pNetif->num,
            pNetif->ip_addr, pNetif->netmask));
    for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
        if (!ip6_addr_isinvalid(netif_ip6_addr_state(pNetif, i))) {
            LogRel(("netif %c%c%d: inet6 %RTnaipv6\n",
                    pNetif->name[0], pNetif->name[1], pNetif->num,
                    netif_ip6_addr(pNetif, i)));
        }
    }

    netif_set_up(pNetif);
    netif_set_link_up(pNetif);

    if (self->m_ProxyOptions.ipv6_enabled) {
        /*
         * XXX: lwIP currently only ever calls mld6_joingroup() in
         * nd6_tmr() for fresh tentative addresses, which is a wrong place
         * to do it - but I'm not keen on fixing this properly for now
         * (with correct handling of interface up and down transitions,
         * etc).  So stick it here as a kludge.
         */
        for (int i = 0; i <= 1; ++i) {
            ip6_addr_t *paddr = netif_ip6_addr(pNetif, i);

            ip6_addr_t solicited_node_multicast_address;
            ip6_addr_set_solicitednode(&solicited_node_multicast_address,
                                       paddr->addr[3]);
            mld6_joingroup(paddr, &solicited_node_multicast_address);
        }

        /*
         * XXX: We must join the solicited-node multicast for the
         * addresses we do IPv6 NA-proxy for.  We map IPv6 loopback to
         * proxy address + 1.  We only need the low 24 bits, and those are
         * fixed.
         */
        {
            ip6_addr_t solicited_node_multicast_address;

            ip6_addr_set_solicitednode(&solicited_node_multicast_address,
                                       /* last 24 bits of the address */
                                       PP_HTONL(0x00000002));
            mld6_netif_joingroup(pNetif,  &solicited_node_multicast_address);
        }
    }

    proxy_init(&self->m_LwipNetIf, &self->m_ProxyOptions);

    natServiceProcessRegisteredPf(self->m_vecPortForwardRule4);
    natServiceProcessRegisteredPf(self->m_vecPortForwardRule6);
}


/**
 * lwIP's callback to configure the interface.
 *
 * Called from onLwipTcpIpInit() via netif_add().  Called after the
 * initerface is mostly initialized, and its IPv4 address is already
 * configured.  Here we still need to configure the MAC address and
 * IPv6 addresses.  It's best to consult the source of netif_add() for
 * the exact details.
 */
/* static */ DECLCALLBACK(err_t)
VBoxNetLwipNAT::netifInit(netif *pNetif) RT_NOTHROW_DEF
{
    err_t rcLwip = ERR_OK;

    AssertPtrReturn(pNetif, ERR_ARG);

    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(pNetif->state);
    AssertPtrReturn(self, ERR_ARG);

    LogFlowFunc(("ENTER: pNetif[%c%c%d]\n", pNetif->name[0], pNetif->name[1], pNetif->num));
    /* validity */
    AssertReturn(   pNetif->name[0] == 'N'
                 && pNetif->name[1] == 'T', ERR_ARG);


    pNetif->hwaddr_len = sizeof(RTMAC);
    memcpy(pNetif->hwaddr, &self->m_MacAddress, sizeof(RTMAC));

    self->m_u16Mtu = 1500; // XXX: FIXME
    pNetif->mtu = self->m_u16Mtu;

    pNetif->flags = NETIF_FLAG_BROADCAST
      | NETIF_FLAG_ETHARP                /* Don't bother driver with ARP and let Lwip resolve ARP handling */
      | NETIF_FLAG_ETHERNET;             /* Lwip works with ethernet too */

    pNetif->linkoutput = netifLinkoutput; /* ether-level-pipe */
    pNetif->output = etharp_output;       /* ip-pipe */

    if (self->m_ProxyOptions.ipv6_enabled) {
        pNetif->output_ip6 = ethip6_output;

        /* IPv6 link-local address in slot 0 */
        netif_create_ip6_linklocal_address(pNetif, /* :from_mac_48bit */ 1);
        netif_ip6_addr_set_state(pNetif, 0, IP6_ADDR_PREFERRED); // skip DAD

        /* INATNetwork::IPv6Prefix in slot 1 */
        memcpy(netif_ip6_addr(pNetif, 1),
               &self->m_ProxyOptions.ipv6_addr, sizeof(ip6_addr_t));
        netif_ip6_addr_set_state(pNetif, 1, IP6_ADDR_PREFERRED);

#if LWIP_IPV6_SEND_ROUTER_SOLICIT
        pNetif->rs_count = 0;
#endif
    }

    LogFlowFunc(("LEAVE: %d\n", rcLwip));
    return rcLwip;
}


/**
 * Run the pumps.
 *
 * Spawn the intnet pump thread that gets packets from the intnet and
 * feeds them to lwIP.  Enter COM event loop here, on the main thread.
 */
int
VBoxNetLwipNAT::run()
{
    AssertReturn(m_hThrRecv == NIL_RTTHREAD, VERR_INVALID_STATE);

    /* spawn the lwIP tcpip thread */
    vboxLwipCoreInitialize(VBoxNetLwipNAT::onLwipTcpIpInit, this);

    /* spawn intnet input pump */
    int rc = RTThreadCreate(&m_hThrRecv,
                            VBoxNetLwipNAT::receiveThread, this,
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

    /* tell the lwIP tcpip thread to terminate */
    vboxLwipCoreFinalize(VBoxNetLwipNAT::onLwipTcpIpFini, this);

    rc = RTThreadWait(m_hThrRecv, 5000, NULL);
    m_hThrRecv = NIL_RTTHREAD;

    return rc;
}


void
VBoxNetLwipNAT::shutdown()
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
 * Run finalization on the lwIP "tcpip" thread.
 */
/* static */
DECLCALLBACK(void) VBoxNetLwipNAT::onLwipTcpIpFini(void *arg)
{
    AssertPtrReturnVoid(arg);
    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(arg);

    /* XXX: proxy finalization */
    netif_set_link_down(&self->m_LwipNetIf);
    netif_set_down(&self->m_LwipNetIf);
    netif_remove(&self->m_LwipNetIf);
}


/**
 * @note: this work on Event thread.
 */
HRESULT VBoxNetLwipNAT::HandleEvent(VBoxEventType_T aEventType, IEvent *pEvent)
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
            if (!m_ProxyOptions.ipv6_enabled)
                break;

            BOOL fIPv6DefaultRoute = FALSE;
            hrc = pSettingsEvent->COMGETTER(AdvertiseDefaultIPv6RouteEnabled)(&fIPv6DefaultRoute);
            AssertComRCReturn(hrc, hrc);

            if (m_ProxyOptions.ipv6_defroute == fIPv6DefaultRoute)
                break;

            m_ProxyOptions.ipv6_defroute = fIPv6DefaultRoute;
            tcpip_callback_with_block(proxy_rtadvd_do_quick, &m_LwipNetIf, 0);
            break;
        }

        case VBoxEventType_OnNATNetworkPortForward:
        {
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

            VECNATSERVICEPF& rules = fIPv6FW ? m_vecPortForwardRule6
                                             : m_vecPortForwardRule4;

            NATSERVICEPORTFORWARDRULE r;
            RT_ZERO(r);

            r.Pfr.fPfrIPv6 = fIPv6FW;

            switch (proto)
            {
                case NATProtocol_TCP:
                    r.Pfr.iPfrProto = IPPROTO_TCP;
                    break;
                case NATProtocol_UDP:
                    r.Pfr.iPfrProto = IPPROTO_UDP;
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

            if (name.length() > sizeof(r.Pfr.szPfrName))
            {
                hrc = E_INVALIDARG;
                goto port_forward_done;
            }

            RTStrPrintf(r.Pfr.szPfrName, sizeof(r.Pfr.szPfrName),
                        "%s", com::Utf8Str(name).c_str());

            RTStrPrintf(r.Pfr.szPfrHostAddr, sizeof(r.Pfr.szPfrHostAddr),
                        "%s", com::Utf8Str(strHostAddr).c_str());

            /* XXX: limits should be checked */
            r.Pfr.u16PfrHostPort = (uint16_t)lHostPort;

            RTStrPrintf(r.Pfr.szPfrGuestAddr, sizeof(r.Pfr.szPfrGuestAddr),
                        "%s", com::Utf8Str(strGuestAddr).c_str());

            /* XXX: limits should be checked */
            r.Pfr.u16PfrGuestPort = (uint16_t)lGuestPort;

            if (fCreateFW) /* Addition */
            {
                int rc = natServicePfRegister(r);
                if (RT_SUCCESS(rc))
                    rules.push_back(r);
            }
            else /* Deletion */
            {
                ITERATORNATSERVICEPF it;
                for (it = rules.begin(); it != rules.end(); ++it)
                {
                    /* compare */
                    NATSERVICEPORTFORWARDRULE &natFw = *it;
                    if (   natFw.Pfr.iPfrProto == r.Pfr.iPfrProto
                        && natFw.Pfr.u16PfrHostPort == r.Pfr.u16PfrHostPort
                        && strncmp(natFw.Pfr.szPfrHostAddr, r.Pfr.szPfrHostAddr, INET6_ADDRSTRLEN) == 0
                        && natFw.Pfr.u16PfrGuestPort == r.Pfr.u16PfrGuestPort
                        && strncmp(natFw.Pfr.szPfrGuestAddr, r.Pfr.szPfrGuestAddr, INET6_ADDRSTRLEN) == 0)
                    {
                        fwspec *pFwCopy = (fwspec *)RTMemDup(&natFw.FWSpec, sizeof(natFw.FWSpec));
                        if (pFwCopy)
                        {
                            int status = portfwd_rule_del(pFwCopy);
                            if (status == 0)
                                rules.erase(it);   /* (pFwCopy is owned by lwip thread now.) */
                            else
                                RTMemFree(pFwCopy);
                        }
                        break;
                    }
                } /* loop over vector elements */
            } /* condition add or delete */
        port_forward_done:
            /* clean up strings */
            name.setNull();
            strHostAddr.setNull();
            strGuestAddr.setNull();
            break;
        }

        case VBoxEventType_OnHostNameResolutionConfigurationChange:
        {
            const char **ppcszNameServers = getHostNameservers();
            err_t error;

            error = tcpip_callback_with_block(pxdns_set_nameservers,
                                              ppcszNameServers,
                                              /* :block */ 0);
            if (error != ERR_OK && ppcszNameServers != NULL)
                RTMemFree(ppcszNameServers);
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
const char **VBoxNetLwipNAT::getHostNameservers()
{
    if (m_host.isNull())
        return NULL;

    com::SafeArray<BSTR> aNameServers;
    HRESULT hrc = m_host->COMGETTER(NameServers)(ComSafeArrayAsOutParam(aNameServers));
    if (FAILED(hrc))
        return NULL;

    const size_t cNameServers = aNameServers.size();
    if (cNameServers == 0)
        return NULL;

    const char **ppcszNameServers =
        (const char **)RTMemAllocZ(sizeof(char *) * (cNameServers + 1));
    if (ppcszNameServers == NULL)
        return NULL;

    size_t idxLast = 0;
    for (size_t i = 0; i < cNameServers; ++i)
    {
        com::Utf8Str strNameServer(aNameServers[i]);
        ppcszNameServers[idxLast] = RTStrDup(strNameServer.c_str());
        if (ppcszNameServers[idxLast] != NULL)
            ++idxLast;
    }

    if (idxLast == 0)
    {
        RTMemFree(ppcszNameServers);
        return NULL;
    }

    return ppcszNameServers;
}


/**
 * Fetch port-forwarding rules via the API.
 *
 * Reads the initial sets of rules from VBoxSVC.  The rules will be
 * activated when all the initialization and plumbing is done.  See
 * natServiceProcessRegisteredPf().
 */
int VBoxNetLwipNAT::fetchNatPortForwardRules(VECNATSERVICEPF &vec, bool fIsIPv6)
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
 *
 * Happens after lwIP and lwIP proxy is initialized, right before lwIP
 * thread starts processing messages.
 */
/* static */
int VBoxNetLwipNAT::natServiceProcessRegisteredPf(VECNATSERVICEPF& vecRules)
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

        natServicePfRegister(natPf);
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
int VBoxNetLwipNAT::natServicePfRegister(NATSERVICEPORTFORWARDRULE &natPf)
{
    int lrc;

    int sockFamily = (natPf.Pfr.fPfrIPv6 ? PF_INET6 : PF_INET);
    int socketSpec;
    switch(natPf.Pfr.iPfrProto)
    {
        case IPPROTO_TCP:
            socketSpec = SOCK_STREAM;
            break;
        case IPPROTO_UDP:
            socketSpec = SOCK_DGRAM;
            break;
        default:
            return VERR_IGNORED;
    }

    const char *pszHostAddr = natPf.Pfr.szPfrHostAddr;
    if (pszHostAddr[0] == '\0')
    {
        if (sockFamily == PF_INET)
            pszHostAddr = "0.0.0.0";
        else
            pszHostAddr = "::";
    }

    lrc = fwspec_set(&natPf.FWSpec,
                     sockFamily,
                     socketSpec,
                     pszHostAddr,
                     natPf.Pfr.u16PfrHostPort,
                     natPf.Pfr.szPfrGuestAddr,
                     natPf.Pfr.u16PfrGuestPort);
    if (lrc != 0)
        return VERR_IGNORED;

    fwspec *pFwCopy = (fwspec *)RTMemDup(&natPf.FWSpec, sizeof(natPf.FWSpec));
    if (pFwCopy)
    {
        lrc = portfwd_rule_add(pFwCopy);
        if (lrc == 0)
            return VINF_SUCCESS; /* (pFwCopy is owned by lwip thread now.) */
        RTMemFree(pFwCopy);
    }
    else
        LogRel(("Unable to allocate memory for %s rule \"%s\"\n",
                natPf.Pfr.fPfrIPv6 ? "IPv6" : "IPv4",
                natPf.Pfr.szPfrName));
    return VERR_IGNORED;
}


/**
 * IntNetIf receive thread.  Runs intnet pump with our processFrame()
 * as input callback.
 */
/* static */ DECLCALLBACK(int)
VBoxNetLwipNAT::receiveThread(RTTHREAD hThreadSelf, void *pvUser)
{
    HRESULT hrc;
    int rc;

    RT_NOREF(hThreadSelf);

    AssertReturn(pvUser != NULL, VERR_INVALID_PARAMETER);
    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(pvUser);

    /* do we relaly need to init com on this thread? */
    hrc = com::Initialize();
    if (FAILED(hrc))
        return VERR_GENERAL_FAILURE;

    rc = IntNetR3IfPumpPkts(self->m_hIf, VBoxNetLwipNAT::processFrame, self,
                            NULL /*pfnInputGso*/, NULL /*pvUserGso*/);
    if (rc == VERR_SEM_DESTROYED)
        return VINF_SUCCESS;

    LogRel(("receiveThread: IntNetR3IfPumpPkts: unexpected %Rrc\n", rc));
    return VERR_INVALID_STATE;
}


/**
 * Process an incoming frame received from the intnet.
 */
/* static */ DECLCALLBACK(void)
VBoxNetLwipNAT::processFrame(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    AssertReturnVoid(pvFrame != NULL);

    /* shouldn't happen, but if it does, don't even bother */
    if (RT_UNLIKELY(cbFrame < sizeof(RTNETETHERHDR)))
        return;

    /* we expect normal ethernet frame including .1Q and FCS */
    if (cbFrame > 1522)
        return;

    AssertReturnVoid(pvUser != NULL);
    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(pvUser);

    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)cbFrame + ETH_PAD_SIZE, PBUF_POOL);
    if (RT_UNLIKELY(p == NULL))
        return;

    /*
     * The code below is inlined version of:
     *
     *   pbuf_header(p, -ETH_PAD_SIZE); // hide padding
     *   pbuf_take(p, pvFrame, cbFrame);
     *   pbuf_header(p, ETH_PAD_SIZE);  // reveal padding
     */
    struct pbuf *q = p;
    uint8_t *pu8Chunk = (uint8_t *)pvFrame;
    do {
        uint8_t *payload = (uint8_t *)q->payload;
        size_t len = q->len;

#if ETH_PAD_SIZE
        if (RT_LIKELY(q == p))  // single pbuf is large enough
        {
            payload += ETH_PAD_SIZE;
            len -= ETH_PAD_SIZE;
        }
#endif
        memcpy(payload, pu8Chunk, len);
        pu8Chunk += len;
        q = q->next;
    } while (RT_UNLIKELY(q != NULL));

    /* pass input to lwIP: netif input funcion tcpip_input() */
    self->m_LwipNetIf.input(p, &self->m_LwipNetIf);
}


/**
 * Send an outgoing frame from lwIP to intnet.
 */
/* static */ DECLCALLBACK(err_t)
VBoxNetLwipNAT::netifLinkoutput(netif *pNetif, pbuf *pPBuf) RT_NOTHROW_DEF
{
    int rc;

    AssertPtrReturn(pNetif, ERR_ARG);
    AssertPtrReturn(pPBuf, ERR_ARG);

    VBoxNetLwipNAT *self = static_cast<VBoxNetLwipNAT *>(pNetif->state);
    AssertPtrReturn(self, ERR_IF);
    AssertReturn(pNetif == &self->m_LwipNetIf, ERR_IF);

    LogFlowFunc(("ENTER: pNetif[%c%c%d], pPbuf:%p\n",
                 pNetif->name[0],
                 pNetif->name[1],
                 pNetif->num,
                 pPBuf));

    if (pPBuf->tot_len < sizeof(struct eth_hdr)) /* includes ETH_PAD_SIZE */
        return ERR_ARG;

    size_t cbFrame = (size_t)pPBuf->tot_len - ETH_PAD_SIZE;
    INTNETFRAME Frame;
    rc = IntNetR3IfQueryOutputFrame(self->m_hIf, (uint32_t)cbFrame, &Frame);
    if (RT_FAILURE(rc))
        return ERR_MEM;

    pbuf_copy_partial(pPBuf, Frame.pvFrame, (u16_t)cbFrame, ETH_PAD_SIZE);
    rc = IntNetR3IfOutputFrameCommit(self->m_hIf, &Frame);
    if (RT_FAILURE(rc))
        return ERR_IF;

    LogFlowFunc(("LEAVE: %d\n", ERR_OK));
    return ERR_OK;
}


/**
 * Retrieve network-specific extra data item.
 */
int VBoxNetLwipNAT::getExtraData(com::Utf8Str &strValueOut, const char *pcszKey)
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
HRESULT VBoxNetLwipNAT::reportComError(ComPtr<IUnknown> iface,
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
void VBoxNetLwipNAT::reportErrorInfoList(const com::ErrorInfo &info,
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
void VBoxNetLwipNAT::reportErrorInfo(const com::ErrorInfo &info)
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
void VBoxNetLwipNAT::reportError(const char *a_pcszFormat, ...)
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
int VBoxNetLwipNAT::initLog()
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

    VBoxNetLwipNAT NAT;

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

#  if 0 /* Some copy and paste from DHCP that nobody explained why was diabled. */
static LRESULT CALLBACK WindowProc(HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    if(uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc (hwnd, uMsg, wParam, lParam);
}

static LPCWSTR g_WndClassName = L"VBoxNetNatLwipClass";

static DWORD WINAPI MsgThreadProc(__in  LPVOID lpParameter)
{
     HWND                 hwnd = 0;
     HINSTANCE hInstance = (HINSTANCE)GetModuleHandle (NULL);
     bool bExit = false;

     /* Register the Window Class. */
     WNDCLASS wc;
     wc.style         = 0;
     wc.lpfnWndProc   = WindowProc;
     wc.cbClsExtra    = 0;
     wc.cbWndExtra    = sizeof(void *);
     wc.hInstance     = hInstance;
     wc.hIcon         = NULL;
     wc.hCursor       = NULL;
     wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
     wc.lpszMenuName  = NULL;
     wc.lpszClassName = g_WndClassName;

     ATOM atomWindowClass = RegisterClass(&wc);

     if (atomWindowClass != 0)
     {
         /* Create the window. */
         hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                               g_WndClassName, g_WndClassName, WS_POPUPWINDOW,
                               -200, -200, 100, 100, NULL, NULL, hInstance, NULL);

         if (hwnd)
         {
             SetWindowPos(hwnd, HWND_TOPMOST, -200, -200, 0, 0,
                          SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

             MSG msg;
             while (GetMessage(&msg, NULL, 0, 0))
             {
                 TranslateMessage(&msg);
                 DispatchMessage(&msg);
             }

             DestroyWindow (hwnd);

             bExit = true;
         }

         UnregisterClass (g_WndClassName, hInstance);
     }

     if(bExit)
     {
         /* no need any accuracy here, in anyway the DHCP server usually gets terminated with TerminateProcess */
         exit(0);
     }

     return 0;
}
#  endif


/** (We don't want a console usually.) */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    RT_NOREF(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
#  if 0 /* some copy and paste from DHCP that nobody explained why was diabled. */
    NOREF(hInstance); NOREF(hPrevInstance); NOREF(lpCmdLine); NOREF(nCmdShow);

    HANDLE hThread = CreateThread(
      NULL, /*__in_opt   LPSECURITY_ATTRIBUTES lpThreadAttributes, */
      0, /*__in       SIZE_T dwStackSize, */
      MsgThreadProc, /*__in       LPTHREAD_START_ROUTINE lpStartAddress,*/
      NULL, /*__in_opt   LPVOID lpParameter,*/
      0, /*__in       DWORD dwCreationFlags,*/
      NULL /*__out_opt  LPDWORD lpThreadId*/
    );

    if(hThread != NULL)
        CloseHandle(hThread);

#  endif
    return main(__argc, __argv, environ);
}
# endif /* RT_OS_WINDOWS */

#endif /* !VBOX_WITH_HARDENING */
