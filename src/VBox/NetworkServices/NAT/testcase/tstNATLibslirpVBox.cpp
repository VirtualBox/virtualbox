/* $Id: tstNATLibslirpVBox.cpp 114884 2026-08-07 07:22:52Z andreas.loeffler@oracle.com $ */
/** @file
 * NAT libslirp VBox testcase.
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
#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# include <iprt/win/ws2tcpip.h>
# include <iprt/win/windows.h>
#endif

#include <slirp/libslirp.h>

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <string.h>


/** Captured frame emitted by libslirp toward the guest. */
typedef struct TSTNATOUTPUT
{
    uint8_t  abFrame[2048];
    size_t   cbFrame;
    uint32_t cFrames;
} TSTNATOUTPUT;
/** Pointer to a captured libslirp output frame. */
typedef TSTNATOUTPUT *PTSTNATOUTPUT;




static DECLCALLBACK(slirp_ssize_t) tstSendPacket(const void *pvBuf, ssize_t cbBuf, void *pvOpaque)
{
    PTSTNATOUTPUT pOutput = (PTSTNATOUTPUT)pvOpaque;
    if (pOutput != NULL && cbBuf > 0)
    {
        pOutput->cbFrame = RT_MIN((size_t)cbBuf, sizeof(pOutput->abFrame));
        memcpy(pOutput->abFrame, pvBuf, pOutput->cbFrame);
        pOutput->cFrames++;
    }
    return cbBuf;
}


static DECLCALLBACK(void) tstGuestError(const char *pszMsg, void *pvOpaque)
{
    RT_NOREF(pszMsg, pvOpaque);
}


static DECLCALLBACK(int64_t) tstClockGetNs(void *pvOpaque)
{
    RT_NOREF(pvOpaque);
    return 0;
}


static DECLCALLBACK(void *) tstTimerNew(SlirpTimerCb pfnTimer, void *pvTimerOpaque, void *pvOpaque)
{
    RT_NOREF(pfnTimer, pvTimerOpaque, pvOpaque);
    return (void *)(uintptr_t)1;
}


static DECLCALLBACK(void) tstTimerFree(void *pvTimer, void *pvOpaque)
{
    RT_NOREF(pvTimer, pvOpaque);
}


static DECLCALLBACK(void) tstTimerMod(void *pvTimer, int64_t nsExpire, void *pvOpaque)
{
    RT_NOREF(pvTimer, nsExpire, pvOpaque);
}


static struct in_addr tstParseIPv4(const char *pszAddr)
{
    RTNETADDRIPV4 ParsedAddr;
    int rc = RTNetStrToIPv4Addr(pszAddr, &ParsedAddr);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTNetStrToIPv4Addr failed for '%s': %Rrc", pszAddr, rc);

    struct in_addr Addr;
    RT_ZERO(Addr);
    Addr.s_addr = RT_SUCCESS(rc) ? ParsedAddr.u : 0;
    return Addr;
}


static struct in6_addr tstParseIPv6(const char *pszAddr)
{
    RTNETADDRIPV6 ParsedAddr;
    char *pszZone = NULL;
    int rc = RTNetStrToIPv6Addr(pszAddr, &ParsedAddr, &pszZone);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTNetStrToIPv6Addr failed for '%s': %Rrc", pszAddr, rc);

    struct in6_addr Addr;
    RT_ZERO(Addr);
    if (RT_SUCCESS(rc))
        memcpy(&Addr, &ParsedAddr, sizeof(Addr));
    return Addr;
}


static struct in_addr *tstDupInAddr(const char *pszAddr)
{
    struct in_addr Addr = tstParseIPv4(pszAddr);
    return (struct in_addr *)RTMemDup(&Addr, sizeof(Addr));
}


static struct in6_addr *tstDupIn6Addr(const char *pszAddr)
{
    struct in6_addr Addr = tstParseIPv6(pszAddr);
    return (struct in6_addr *)RTMemDup(&Addr, sizeof(Addr));
}


static void tstVBoxAbi(void)
{
    RTTestISub("ABI: VBox-only SlirpConfig fields");

    struct ip4_lomap aLoopbackMap[] =
    {
        { tstParseIPv4("127.0.0.1").s_addr, 42 }
    };
    const struct ip4_lomap_desc LoopbackMapDesc =
    {
        aLoopbackMap, RT_ELEMENTS(aLoopbackMap)
    };

    struct in_addr *paRealNameservers = tstDupInAddr("8.8.4.4");
    struct in6_addr *paIPv6RealNameservers = tstDupIn6Addr("2001:db8::53");
    RTTESTI_CHECK_RETV(paRealNameservers);
    RTTESTI_CHECK_RETV(paIPv6RealNameservers);

    SlirpConfig Cfg;
    RT_ZERO(Cfg);
    Cfg.version             = SLIRP_CONFIG_VERSION_MAX;
    Cfg.in_enabled          = true;
    Cfg.in6_enabled         = false;
    Cfg.vnetwork            = tstParseIPv4("10.0.2.0");
    Cfg.vnetmask            = tstParseIPv4("255.255.255.0");
    Cfg.vhost               = tstParseIPv4("10.0.2.2");
    Cfg.vdhcp_start         = tstParseIPv4("10.0.2.15");
    Cfg.vnameserver         = tstParseIPv4("10.0.2.3");
    Cfg.vprefix_len         = 64;
    Cfg.vhost6              = tstParseIPv6("fec0::2");
    Cfg.vnameserver6        = tstParseIPv6("fec0::3");
    Cfg.if_mtu              = 1500;
    Cfg.if_mru              = 1500;
    Cfg.if_mtu_v6           = 1280;
    Cfg.if_mru_v6           = 1280;
    Cfg.aRealNameservers    = paRealNameservers;
    Cfg.cRealNameservers    = 1;
    Cfg.aIPv6RealNameservers = paIPv6RealNameservers;
    Cfg.cIPv6RealNameservers = 1;
    Cfg.fForwardBroadcast   = true;
    Cfg.iSoMaxConn          = 7;
    Cfg.fDisableIPv6RA      = true;
    Cfg.mLoopbackMap        = &LoopbackMapDesc;

    SlirpCb Cb;
    RT_ZERO(Cb);
    Cb.send_packet  = tstSendPacket;
    Cb.guest_error  = tstGuestError;
    Cb.clock_get_ns = tstClockGetNs;
    Cb.timer_new    = tstTimerNew;
    Cb.timer_free   = tstTimerFree;
    Cb.timer_mod    = tstTimerMod;

    TSTNATOUTPUT Output;
    RT_ZERO(Output);
    Slirp *pSlirp = slirp_new(&Cfg, &Cb, &Output);
    if (!pSlirp)
    {
        RTTestIFailed("slirp_new failed");
        RTMemFree(paRealNameservers);
        RTMemFree(paIPv6RealNameservers);
        return;
    }

    struct in_addr NetworkAddr = slirp_get_vnetwork_addr(pSlirp);
    RTTESTI_CHECK_MSG(NetworkAddr.s_addr == Cfg.vnetwork.s_addr,
                      ("NetworkAddr=%#RX32 expected %#RX32", NetworkAddr.s_addr, Cfg.vnetwork.s_addr));

    char *pszDomain = slirp_set_vdomainname(pSlirp, "vbox.example");
    RTTESTI_CHECK(pszDomain != NULL);
    RTTESTI_CHECK_MSG(strcmp(slirp_get_vdomainname(pSlirp), "vbox.example") == 0,
                      ("vdomainname='%s'", slirp_get_vdomainname(pSlirp)));

    static const char * const s_apszSearchDomains[] = { "vbox.example", "test.invalid", NULL };
    RTTESTI_CHECK(slirp_set_vdnssearch(pSlirp, s_apszSearchDomains) == 0);

    struct in_addr NameServer = tstParseIPv4("1.1.1.1");
    slirp_set_vnameserver(pSlirp, NameServer);

    struct in6_addr NameServer6 = tstParseIPv6("2001:4860:4860::8888");
    slirp_set_vnameserver6(pSlirp, NameServer6);
    slirp_set_disable_dns(pSlirp, true);
    slirp_set_disable_dns(pSlirp, false);

    struct in_addr *paNewRealNameservers = tstDupInAddr("9.9.9.9");
    struct in6_addr *paNewIPv6RealNameservers = tstDupIn6Addr("2001:db8::54");
    RTTESTI_CHECK(paNewRealNameservers != NULL);
    RTTESTI_CHECK(paNewIPv6RealNameservers != NULL);
    if (paNewRealNameservers && paNewIPv6RealNameservers)
    {
        slirp_set_RealNameservers(pSlirp, 1, paNewRealNameservers);
        slirp_set_IPv6RealNameservers(pSlirp, 1, paNewIPv6RealNameservers);
    }
    else
    {
        RTMemFree(paNewRealNameservers);
        RTMemFree(paNewIPv6RealNameservers);
    }

    RTTESTI_CHECK(slirp_version_string() != NULL);

    RTTestISub("Data path: IPv4 gateway ARP");

    uint8_t abRequest[sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4)];
    RT_ZERO(abRequest);
    PRTNETETHERHDR pEth = (PRTNETETHERHDR)&abRequest[0];
    memset(&pEth->DstMac, 0xff, sizeof(pEth->DstMac));
    pEth->SrcMac.au8[0] = 0x08;
    pEth->SrcMac.au8[1] = 0x00;
    pEth->SrcMac.au8[2] = 0x27;
    pEth->SrcMac.au8[3] = 0x12;
    pEth->SrcMac.au8[4] = 0x34;
    pEth->SrcMac.au8[5] = 0x56;
    pEth->EtherType = RT_H2N_U16(RTNET_ETHERTYPE_ARP);

    PRTNETARPIPV4 pArp = (PRTNETARPIPV4)&abRequest[sizeof(*pEth)];
    pArp->Hdr.ar_htype = RT_H2N_U16(RTNET_ARP_ETHER);
    pArp->Hdr.ar_ptype = RT_H2N_U16(RTNET_ETHERTYPE_IPV4);
    pArp->Hdr.ar_hlen  = sizeof(RTMAC);
    pArp->Hdr.ar_plen  = sizeof(RTNETADDRIPV4);
    pArp->Hdr.ar_oper  = RT_H2N_U16(RTNET_ARPOP_REQUEST);
    pArp->ar_sha       = pEth->SrcMac;
    pArp->ar_spa.u     = tstParseIPv4("10.0.2.15").s_addr;
    pArp->ar_tpa.u     = Cfg.vhost.s_addr;

    RT_ZERO(Output);
    slirp_input(pSlirp, abRequest, sizeof(abRequest));
    RTTESTI_CHECK_MSG(Output.cFrames > 0, ("No frame returned for gateway ARP request"));
    RTTESTI_CHECK_MSG(Output.cbFrame >= sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4),
                      ("ARP reply is too small: %zu", Output.cbFrame));
    if (Output.cbFrame >= sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4))
    {
        PCRTNETETHERHDR pReplyEth = (PCRTNETETHERHDR)&Output.abFrame[0];
        PCRTNETARPIPV4 pReplyArp = (PCRTNETARPIPV4)&Output.abFrame[sizeof(*pReplyEth)];
        RTTESTI_CHECK(RT_N2H_U16(pReplyEth->EtherType) == RTNET_ETHERTYPE_ARP);
        RTTESTI_CHECK(RT_N2H_U16(pReplyArp->Hdr.ar_oper) == RTNET_ARPOP_REPLY);
        RTTESTI_CHECK(pReplyArp->ar_spa.u == Cfg.vhost.s_addr);
        RTTESTI_CHECK(pReplyArp->ar_tpa.u == pArp->ar_spa.u);
        RTTESTI_CHECK(memcmp(&pReplyArp->ar_tha, &pEth->SrcMac, sizeof(RTMAC)) == 0);
    }

    slirp_cleanup(pSlirp);
}


int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstNATLibslirpVBox", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstVBoxAbi();

    return RTTestSummaryAndDestroy(hTest);
}
