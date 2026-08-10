/* $Id: tstVBoxNatGuestSide.cpp 114964 2026-08-10 15:18:23Z andreas.loeffler@oracle.com $ */
/** @file
 * tstVBoxNatGuestSide - Guest-side NAT over Ring-3 IntNet testcase.
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
#include <iprt/asm.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/localipc.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/uuid.h>

#include <VBox/intnetr3ipc.h>

#include <stdlib.h>
#include <string.h>

#include "../NetLib/IntNetIf.h"
#include "tstVBoxNatGuestSideInternal.h"


/*********************************************************************************************************************************
*   Constants                                                                                                                    *
*********************************************************************************************************************************/
#define TST_MAX_FRAME             2048
#define TST_MAX_QUEUED_FRAMES       16
#define TST_DHCP_XID        UINT32_C(0x33445566)
#define TST_WAIT_MS                 3000
#define TST_POLL_MS                    5


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** One captured or synthetic Ethernet frame. */
typedef struct TSTFRAME
{
    /** Frame bytes. */
    uint8_t ab[TST_MAX_FRAME];
    /** Number of valid frame bytes. */
    size_t  cb;
} TSTFRAME;
/** Pointer to a frame. */
typedef TSTFRAME *PTSTFRAME;
/** Pointer to a const frame. */
typedef const TSTFRAME *PCTSTFRAME;

/** Bounded FIFO for frames emitted by libslirp. */
typedef struct TSTFRAMEQUEUE
{
    /** Captured frames. */
    TSTFRAME aFrames[TST_MAX_QUEUED_FRAMES];
    /** Index of the oldest frame. */
    uint32_t iHead;
    /** Number of queued frames. */
    uint32_t cFrames;
} TSTFRAMEQUEUE;
/** Pointer to a frame queue. */
typedef TSTFRAMEQUEUE *PTSTFRAMEQUEUE;

/** Parsed fields from a DHCP server response. */
typedef struct TSTDHCPOFFER
{
    /** DHCP message type. */
    uint8_t         uMsgType;
    /** Offered or acknowledged client address. */
    RTNETADDRIPV4   YiAddr;
    /** Subnet mask option. */
    RTNETADDRIPV4   Netmask;
    /** Server identifier option. */
    RTNETADDRIPV4   ServerId;
    /** Whether a subnet mask option was present. */
    bool            fNetmask;
    /** Whether a server identifier option was present. */
    bool            fServerId;
    /** Whether a router option was present. */
    bool            fRouter;
    /** Whether a DNS option was present. */
    bool            fDns;
} TSTDHCPOFFER;
/** Pointer to parsed DHCP fields. */
typedef TSTDHCPOFFER *PTSTDHCPOFFER;

/** State for the standalone R3 switch helper. */
typedef struct TSTSWITCHSERVICE
{
    /** Helper process ID once the auto-start path has published it. */
    RTPROCESS hProcess;
    /** Whether the helper was successfully reaped. */
    bool      fProcessReaped;
    /** Whether the temporary directory was created. */
    bool      fTempDirCreated;
    /** Unique local IPC service name. */
    char      szService[INTNET_R3_IPC_MAX_SERVICE_NAME];
    /** Absolute helper executable path. */
    char      szExec[RTPATH_MAX];
    /** Per-test temporary directory. */
    char      szTempDir[RTPATH_MAX];
    /** Helper PID file. */
    char      szPidFile[RTPATH_MAX];
    /** Helper lock file. */
    char      szLockFile[RTPATH_MAX];
} TSTSWITCHSERVICE;
/** Pointer to switch helper state. */
typedef TSTSWITCHSERVICE *PTSTSWITCHSERVICE;

/** NAT-facing IntNet receive pump. */
typedef struct TSTNATPUMP
{
    /** NAT-facing IntNet interface. */
    INTNETIFCTX             hIf;
    /** Restricted libslirp wrapper. */
    PVBOXNETSLIRPNATTEST    hNat;
    /** Pump thread. */
    RTTHREAD                hThread;
    /** First input failure reported by the pump callback. */
    int                     rcInput;
} TSTNATPUMP;
/** Pointer to NAT pump state. */
typedef TSTNATPUMP *PTSTNATPUMP;

/** Guest-facing IntNet receive collector. */
typedef struct TSTGUESTRX
{
    /** Guest-facing IntNet interface. */
    INTNETIFCTX     hIf;
    /** Receive thread. */
    RTTHREAD        hThread;
    /** Signalled when the expected ARP reply is captured. */
    RTSEMEVENT      hReplyEvent;
    /** Expected gateway address. */
    RTNETADDRIPV4   GatewayIp;
    /** Expected guest address. */
    RTNETADDRIPV4   GuestIp;
    /** Expected guest MAC address. */
    RTMAC           GuestMac;
    /** Captured reply. */
    TSTFRAME        Reply;
    /** Whether Reply contains the expected ARP response. */
    bool            fReply;
} TSTGUESTRX;
/** Pointer to guest receive state. */
typedef TSTGUESTRX *PTSTGUESTRX;

/** IntNet destination used by the libslirp output callback. */
typedef struct TSTNATOUTPUT
{
    /** NAT-facing IntNet interface. */
    INTNETIFCTX hIf;
} TSTNATOUTPUT;
/** Pointer to the IntNet output state. */
typedef TSTNATOUTPUT *PTSTNATOUTPUT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Test handle used by fixture cleanup diagnostics. */
static RTTEST g_hTest = NIL_RTTEST;


/*********************************************************************************************************************************
*   Packet Helpers                                                                                                               *
*********************************************************************************************************************************/
/** Constructs a MAC address from literal octets. */
static RTMAC tstMac(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
    RTMAC Mac;
    Mac.au8[0] = b0;
    Mac.au8[1] = b1;
    Mac.au8[2] = b2;
    Mac.au8[3] = b3;
    Mac.au8[4] = b4;
    Mac.au8[5] = b5;
    return Mac;
}


/** Returns the Ethernet broadcast address. */
static RTMAC tstMacBroadcast(void)
{
    return tstMac(0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
}


/** Compares two MAC addresses. */
static bool tstMacEqual(PCRTMAC pLeft, PCRTMAC pRight)
{
    return memcmp(pLeft, pRight, sizeof(*pLeft)) == 0;
}


/** Constructs an IPv4 address from dotted-decimal octets. */
static RTNETADDRIPV4 tstIPv4(uint8_t b3, uint8_t b2, uint8_t b1, uint8_t b0)
{
    return RTNetIPv4AddrFromU8(b3, b2, b1, b0);
}


/** Populates the restricted configuration shared by the tests. */
static void tstNatConfigInit(PVBOXNETSLIRPNATTESTCFG pCfg, bool fDhcp)
{
    RT_ZERO(*pCfg);
    pCfg->IPv4Network    = tstIPv4(10, 0, 2, 0);
    pCfg->IPv4Netmask    = tstIPv4(255, 255, 255, 0);
    pCfg->IPv4Host       = tstIPv4(10, 0, 2, 2);
    pCfg->IPv4DhcpStart  = tstIPv4(10, 0, 2, 15);
    pCfg->IPv4Nameserver = tstIPv4(10, 0, 2, 3);
    pCfg->fDhcp          = fDhcp;
    pCfg->fDns           = true;
    pCfg->fRestricted    = true;
}


/** Starts an Ethernet frame. */
static void tstEthernetBegin(PTSTFRAME pFrame, PCRTMAC pDstMac, PCRTMAC pSrcMac, uint16_t uEtherType)
{
    RT_ZERO(*pFrame);
    PRTNETETHERHDR pEthernet = (PRTNETETHERHDR)pFrame->ab;
    pEthernet->DstMac   = *pDstMac;
    pEthernet->SrcMac   = *pSrcMac;
    pEthernet->EtherType = RT_H2N_U16(uEtherType);
    pFrame->cb = sizeof(*pEthernet);
}


/** Builds a guest ARP request. */
static void tstBuildArpRequest(PTSTFRAME pFrame, PCRTMAC pGuestMac,
                               RTNETADDRIPV4 GuestIp, RTNETADDRIPV4 TargetIp)
{
    RTMAC const Broadcast = tstMacBroadcast();
    tstEthernetBegin(pFrame, &Broadcast, pGuestMac, RTNET_ETHERTYPE_ARP);

    PRTNETARPIPV4 pArp = (PRTNETARPIPV4)&pFrame->ab[pFrame->cb];
    RT_ZERO(*pArp);
    pArp->Hdr.ar_htype = RT_H2N_U16(RTNET_ARP_ETHER);
    pArp->Hdr.ar_ptype = RT_H2N_U16(RTNET_ETHERTYPE_IPV4);
    pArp->Hdr.ar_hlen  = sizeof(RTMAC);
    pArp->Hdr.ar_plen  = sizeof(RTNETADDRIPV4);
    pArp->Hdr.ar_oper  = RT_H2N_U16(RTNET_ARPOP_REQUEST);
    pArp->ar_sha       = *pGuestMac;
    pArp->ar_spa       = GuestIp;
    pArp->ar_tpa       = TargetIp;
    pFrame->cb += sizeof(*pArp);
}


/** Recognizes an internally consistent ARP reply for the expected guest. */
static bool tstIsArpReply(PCTSTFRAME pFrame, RTNETADDRIPV4 SenderIp, RTNETADDRIPV4 TargetIp,
                          PCRTMAC pTargetMac, PRTMAC pSenderMac)
{
    if (pFrame->cb < sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4))
        return false;

    PCRTNETETHERHDR pEthernet = (PCRTNETETHERHDR)pFrame->ab;
    PCRTNETARPIPV4 pArp = (PCRTNETARPIPV4)(pEthernet + 1);
    if (   RT_N2H_U16(pEthernet->EtherType) != RTNET_ETHERTYPE_ARP
        || RT_N2H_U16(pArp->Hdr.ar_htype) != RTNET_ARP_ETHER
        || RT_N2H_U16(pArp->Hdr.ar_ptype) != RTNET_ETHERTYPE_IPV4
        || pArp->Hdr.ar_hlen != sizeof(RTMAC)
        || pArp->Hdr.ar_plen != sizeof(RTNETADDRIPV4)
        || RT_N2H_U16(pArp->Hdr.ar_oper) != RTNET_ARPOP_REPLY
        || pArp->ar_spa.u != SenderIp.u
        || pArp->ar_tpa.u != TargetIp.u
        || !tstMacEqual(&pEthernet->SrcMac, &pArp->ar_sha)
        || !tstMacEqual(&pEthernet->DstMac, &pArp->ar_tha)
        || (pTargetMac && !tstMacEqual(&pArp->ar_tha, pTargetMac)))
        return false;

    if (pSenderMac)
        *pSenderMac = pArp->ar_sha;
    return true;
}


/** Appends a one-byte DHCP option. */
static void tstDhcpOptionU8(uint8_t **ppb, uint8_t uOption, uint8_t uValue)
{
    *(*ppb)++ = uOption;
    *(*ppb)++ = 1;
    *(*ppb)++ = uValue;
}


/** Appends an IPv4-valued DHCP option. */
static void tstDhcpOptionIPv4(uint8_t **ppb, uint8_t uOption, RTNETADDRIPV4 Addr)
{
    *(*ppb)++ = uOption;
    *(*ppb)++ = sizeof(Addr);
    memcpy(*ppb, &Addr, sizeof(Addr));
    *ppb += sizeof(Addr);
}


/** Builds a DHCP DISCOVER or REQUEST Ethernet frame. */
static void tstBuildDhcpRequest(PTSTFRAME pFrame, PCRTMAC pGuestMac, uint8_t uMsgType,
                                RTNETADDRIPV4 RequestedAddr, RTNETADDRIPV4 ServerId)
{
    RTMAC const Broadcast = tstMacBroadcast();
    RTNETADDRIPV4 const Zero = tstIPv4(0, 0, 0, 0);
    RTNETADDRIPV4 const All  = tstIPv4(255, 255, 255, 255);
    tstEthernetBegin(pFrame, &Broadcast, pGuestMac, RTNET_ETHERTYPE_IPV4);

    PRTNETIPV4 pIp = (PRTNETIPV4)&pFrame->ab[pFrame->cb];
    RT_ZERO(*pIp);
    pIp->ip_v   = 4;
    pIp->ip_hl  = 5;
    pIp->ip_len = RT_H2N_U16(RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + sizeof(RTNETBOOTP));
    pIp->ip_id  = RT_H2N_U16(0x4400);
    pIp->ip_off = RT_H2N_U16(RTNETIPV4_FLAGS_DF);
    pIp->ip_ttl = 64;
    pIp->ip_p   = RTNETIPV4_PROT_UDP;
    pIp->ip_src = Zero;
    pIp->ip_dst = All;
    pIp->ip_sum = RTNetIPv4HdrChecksum(pIp);
    pFrame->cb += RTNETIPV4_MIN_LEN;

    PRTNETUDP pUdp = (PRTNETUDP)&pFrame->ab[pFrame->cb];
    RT_ZERO(*pUdp);
    pUdp->uh_sport = RT_H2N_U16(RTNETIPV4_PORT_BOOTPC);
    pUdp->uh_dport = RT_H2N_U16(RTNETIPV4_PORT_BOOTPS);
    pUdp->uh_ulen  = RT_H2N_U16(RTNETUDP_MIN_LEN + sizeof(RTNETBOOTP));
    pFrame->cb += RTNETUDP_MIN_LEN;

    PRTNETBOOTP pDhcp = (PRTNETBOOTP)&pFrame->ab[pFrame->cb];
    RT_ZERO(*pDhcp);
    pDhcp->bp_op       = RTNETBOOTP_OP_REQUEST;
    pDhcp->bp_htype    = RTNET_ARP_ETHER;
    pDhcp->bp_hlen     = sizeof(RTMAC);
    pDhcp->bp_xid      = RT_H2N_U32(TST_DHCP_XID);
    pDhcp->bp_flags    = RT_H2N_U16(RTNET_DHCP_FLAG_BROADCAST);
    pDhcp->bp_chaddr.Mac = *pGuestMac;
    pDhcp->bp_vend.Dhcp.dhcp_cookie = RT_H2N_U32(RTNET_DHCP_COOKIE);

    uint8_t *pbOption = pDhcp->bp_vend.Dhcp.dhcp_opts;
    tstDhcpOptionU8(&pbOption, RTNET_DHCP_OPT_MSG_TYPE, uMsgType);
    if (uMsgType == RTNET_DHCP_MT_REQUEST)
    {
        tstDhcpOptionIPv4(&pbOption, RTNET_DHCP_OPT_REQ_ADDR, RequestedAddr);
        tstDhcpOptionIPv4(&pbOption, RTNET_DHCP_OPT_SERVER_ID, ServerId);
    }
    *pbOption++ = RTNET_DHCP_OPT_PARAM_REQ_LIST;
    *pbOption++ = 4;
    *pbOption++ = RTNET_DHCP_OPT_SUBNET_MASK;
    *pbOption++ = RTNET_DHCP_OPT_ROUTERS;
    *pbOption++ = RTNET_DHCP_OPT_DNS;
    *pbOption++ = RTNET_DHCP_OPT_DOMAIN_NAME;
    *pbOption++ = RTNET_DHCP_OPT_END;

    pFrame->cb += sizeof(*pDhcp);
    pUdp->uh_sum = RTNetIPv4UDPChecksum(pIp, pUdp, pDhcp);
}


/** Parses a bounded IPv4/UDP DHCP server response. */
static bool tstParseDhcpResponse(PCTSTFRAME pFrame, PCRTMAC pGuestMac, PTSTDHCPOFFER pOffer)
{
    if (pFrame->cb < sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN)
        return false;

    PCRTNETETHERHDR pEthernet = (PCRTNETETHERHDR)pFrame->ab;
    if (RT_N2H_U16(pEthernet->EtherType) != RTNET_ETHERTYPE_IPV4)
        return false;
    PCRTNETIPV4 pIp = (PCRTNETIPV4)(pEthernet + 1);
    if (pIp->ip_v != 4 || pIp->ip_hl < 5 || pIp->ip_p != RTNETIPV4_PROT_UDP)
        return false;

    size_t const cbIpHdr = pIp->ip_hl * 4;
    size_t const cbIpMax = pFrame->cb - sizeof(*pEthernet);
    size_t const cbIp    = RT_N2H_U16(pIp->ip_len);
    if (   !RTNetIPv4IsHdrValid(pIp, cbIpMax, cbIpMax, true /* fChecksum */)
        || cbIp < cbIpHdr + RTNETUDP_MIN_LEN
        || cbIp > cbIpMax
        || (RT_N2H_U16(pIp->ip_off) & (RTNETIPV4_FLAGS_MF | UINT16_C(0x1fff))))
        return false;
    PCRTNETUDP pUdp = (PCRTNETUDP)((uint8_t const *)pIp + cbIpHdr);
    size_t const cbUdp = RT_N2H_U16(pUdp->uh_ulen);
    if (   cbUdp < RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN
        || cbIpHdr + cbUdp > cbIp
        || !RTNetIPv4IsUDPValid(pIp, pUdp, pUdp + 1, cbIp - cbIpHdr, true /* fChecksum */)
        || RT_N2H_U16(pUdp->uh_sport) != RTNETIPV4_PORT_BOOTPS
        || RT_N2H_U16(pUdp->uh_dport) != RTNETIPV4_PORT_BOOTPC)
        return false;

    size_t const cbDhcp = cbUdp - RTNETUDP_MIN_LEN;
    PCRTNETBOOTP pDhcp = (PCRTNETBOOTP)(pUdp + 1);
    if (   pDhcp->bp_op != RTNETBOOTP_OP_REPLY
        || pDhcp->bp_htype != RTNET_ARP_ETHER
        || pDhcp->bp_hlen != sizeof(RTMAC)
        || pDhcp->bp_xid != RT_H2N_U32(TST_DHCP_XID)
        || !tstMacEqual(&pDhcp->bp_chaddr.Mac, pGuestMac)
        || pDhcp->bp_vend.Dhcp.dhcp_cookie != RT_H2N_U32(RTNET_DHCP_COOKIE))
        return false;

    RT_ZERO(*pOffer);
    pOffer->YiAddr = pDhcp->bp_yiaddr;
    uint8_t const *pbOption = pDhcp->bp_vend.Dhcp.dhcp_opts;
    uint8_t const *pbEnd = (uint8_t const *)pDhcp + cbDhcp;
    while (pbOption < pbEnd)
    {
        uint8_t const uOption = *pbOption++;
        if (uOption == RTNET_DHCP_OPT_END)
            break;
        if (uOption == RTNET_DHCP_OPT_PAD)
            continue;
        if (pbOption >= pbEnd)
            return false;
        uint8_t const cbOption = *pbOption++;
        if ((size_t)(pbEnd - pbOption) < cbOption)
            return false;

        if (uOption == RTNET_DHCP_OPT_MSG_TYPE && cbOption == 1)
            pOffer->uMsgType = pbOption[0];
        else if (uOption == RTNET_DHCP_OPT_SUBNET_MASK && cbOption == sizeof(RTNETADDRIPV4))
        {
            memcpy(&pOffer->Netmask, pbOption, sizeof(pOffer->Netmask));
            pOffer->fNetmask = true;
        }
        else if (uOption == RTNET_DHCP_OPT_SERVER_ID && cbOption == sizeof(RTNETADDRIPV4))
        {
            memcpy(&pOffer->ServerId, pbOption, sizeof(pOffer->ServerId));
            pOffer->fServerId = true;
        }
        else if (uOption == RTNET_DHCP_OPT_ROUTERS && cbOption >= sizeof(RTNETADDRIPV4))
            pOffer->fRouter = true;
        else if (uOption == RTNET_DHCP_OPT_DNS && cbOption >= sizeof(RTNETADDRIPV4))
            pOffer->fDns = true;
        pbOption += cbOption;
    }
    return pOffer->uMsgType != 0;
}


/*********************************************************************************************************************************
*   Direct libslirp Test Helpers                                                                                                 *
*********************************************************************************************************************************/
/** Captures a frame emitted by the restricted libslirp wrapper. */
static DECLCALLBACK(int) tstCaptureOutput(const void *pvFrame, size_t cbFrame, void *pvUser)
{
    PTSTFRAMEQUEUE pQueue = (PTSTFRAMEQUEUE)pvUser;
    if (!pQueue || cbFrame > TST_MAX_FRAME || pQueue->cFrames >= TST_MAX_QUEUED_FRAMES)
        return VERR_BUFFER_OVERFLOW;

    uint32_t const iFrame = (pQueue->iHead + pQueue->cFrames) % TST_MAX_QUEUED_FRAMES;
    memcpy(pQueue->aFrames[iFrame].ab, pvFrame, cbFrame);
    pQueue->aFrames[iFrame].cb = cbFrame;
    pQueue->cFrames++;
    return VINF_SUCCESS;
}


/** Pops the oldest captured frame. */
static bool tstQueuePop(PTSTFRAMEQUEUE pQueue, PTSTFRAME pFrame)
{
    if (!pQueue->cFrames)
        return false;
    *pFrame = pQueue->aFrames[pQueue->iHead];
    pQueue->iHead = (pQueue->iHead + 1) % TST_MAX_QUEUED_FRAMES;
    pQueue->cFrames--;
    return true;
}


/** Finds and consumes a matching ARP reply. */
static bool tstFindArpReply(PTSTFRAMEQUEUE pQueue, RTNETADDRIPV4 SenderIp, RTNETADDRIPV4 TargetIp,
                            PCRTMAC pTargetMac, PRTMAC pSenderMac)
{
    uint32_t cLeft = pQueue->cFrames;
    while (cLeft--)
    {
        TSTFRAME Frame;
        tstQueuePop(pQueue, &Frame);
        if (tstIsArpReply(&Frame, SenderIp, TargetIp, pTargetMac, pSenderMac))
            return true;
    }
    return false;
}


/** Finds and consumes a DHCP response of the requested type. */
static bool tstFindDhcpResponse(PTSTFRAMEQUEUE pQueue, PCRTMAC pGuestMac, uint8_t uMsgType,
                                PTSTDHCPOFFER pOffer)
{
    uint32_t cLeft = pQueue->cFrames;
    while (cLeft--)
    {
        TSTFRAME Frame;
        tstQueuePop(pQueue, &Frame);
        if (tstParseDhcpResponse(&Frame, pGuestMac, pOffer) && pOffer->uMsgType == uMsgType)
            return true;
    }
    return false;
}


/** Polls libslirp until a matching ARP reply is captured or the deadline expires. */
static int tstWaitForArpReply(PVBOXNETSLIRPNATTEST hNat, PTSTFRAMEQUEUE pQueue,
                              RTNETADDRIPV4 SenderIp, RTNETADDRIPV4 TargetIp,
                              PCRTMAC pTargetMac, PRTMAC pSenderMac)
{
    uint64_t const msStart = RTTimeMilliTS();
    for (;;)
    {
        if (tstFindArpReply(pQueue, SenderIp, TargetIp, pTargetMac, pSenderMac))
            return VINF_SUCCESS;
        if (RTTimeMilliTS() - msStart >= TST_WAIT_MS)
            return VERR_TIMEOUT;
        int const rc = VBoxNetSlirpNATTestPoll(hNat, TST_POLL_MS);
        if (RT_FAILURE(rc))
            return rc;
    }
}


/** Polls libslirp until a matching DHCP response is captured or the deadline expires. */
static int tstWaitForDhcpResponse(PVBOXNETSLIRPNATTEST hNat, PTSTFRAMEQUEUE pQueue,
                                  PCRTMAC pGuestMac, uint8_t uMsgType, PTSTDHCPOFFER pOffer)
{
    uint64_t const msStart = RTTimeMilliTS();
    for (;;)
    {
        if (tstFindDhcpResponse(pQueue, pGuestMac, uMsgType, pOffer))
            return VINF_SUCCESS;
        if (RTTimeMilliTS() - msStart >= TST_WAIT_MS)
            return VERR_TIMEOUT;
        int const rc = VBoxNetSlirpNATTestPoll(hNat, TST_POLL_MS);
        if (RT_FAILURE(rc))
            return rc;
    }
}


/** Verifies the restricted wrapper rejects unsafe or inconsistent configuration. */
static void tstConfigValidation(void)
{
    RTTestSub(g_hTest, "restricted configuration validation");

    VBOXNETSLIRPNATTESTCFG Cfg;
    tstNatConfigInit(&Cfg, false);
    TSTFRAMEQUEUE Queue;
    RT_ZERO(Queue);
    PVBOXNETSLIRPNATTEST hNat = NULL;

    Cfg.fRestricted = false;
    int rc = VBoxNetSlirpNATTestCreate(&Cfg, tstCaptureOutput, &Queue, &hNat);
    RTTEST_CHECK_RC(g_hTest, rc, VERR_ACCESS_DENIED);
    RTTEST_CHECK(g_hTest, hNat == NULL);

    tstNatConfigInit(&Cfg, false);
    Cfg.IPv4Host = tstIPv4(192, 0, 2, 1);
    rc = VBoxNetSlirpNATTestCreate(&Cfg, tstCaptureOutput, &Queue, &hNat);
    RTTEST_CHECK_RC(g_hTest, rc, VERR_INVALID_PARAMETER);
    RTTEST_CHECK(g_hTest, hNat == NULL);
}


/** Verifies the full restricted DHCP DISCOVER/OFFER/REQUEST/ACK exchange. */
static void tstDhcp(void)
{
    RTTestSub(g_hTest, "restricted libslirp DHCPv4 lease");

    VBOXNETSLIRPNATTESTCFG Cfg;
    tstNatConfigInit(&Cfg, true);
    TSTFRAMEQUEUE Queue;
    RT_ZERO(Queue);
    PVBOXNETSLIRPNATTEST hNat = NULL;
    int rc = VBoxNetSlirpNATTestCreate(&Cfg, tstCaptureOutput, &Queue, &hNat);
    RTTEST_CHECK_RC_RETV(g_hTest, rc, VINF_SUCCESS);

    RTMAC const GuestMac = tstMac(0x08, 0x00, 0x27, 0xaa, 0xbb, 0x15);
    TSTFRAME Frame;
    tstBuildDhcpRequest(&Frame, &GuestMac, RTNET_DHCP_MT_DISCOVER,
                        tstIPv4(0, 0, 0, 0), tstIPv4(0, 0, 0, 0));
    rc = VBoxNetSlirpNATTestInput(hNat, Frame.ab, Frame.cb);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    TSTDHCPOFFER Offer;
    RT_ZERO(Offer);
    if (RT_SUCCESS(rc))
        rc = tstWaitForDhcpResponse(hNat, &Queue, &GuestMac, RTNET_DHCP_MT_OFFER, &Offer);
    bool const fOffer = RT_SUCCESS(rc);
    RTTEST_CHECK_MSG(g_hTest, fOffer, (g_hTest, "No valid DHCPOFFER was returned\n"));
    if (fOffer)
    {
        RTTEST_CHECK(g_hTest, Offer.YiAddr.u != 0);
        RTTEST_CHECK(g_hTest, (Offer.YiAddr.u & Cfg.IPv4Netmask.u) == Cfg.IPv4Network.u);
        RTTEST_CHECK(g_hTest, Offer.fNetmask && Offer.Netmask.u == Cfg.IPv4Netmask.u);
        RTTEST_CHECK(g_hTest, Offer.fServerId && Offer.ServerId.u == Cfg.IPv4Host.u);
        RTTEST_CHECK_MSG(g_hTest, !Offer.fRouter && !Offer.fDns,
                         (g_hTest, "Restricted DHCP unexpectedly advertised host routing or DNS access\n"));

        RT_ZERO(Queue);
        tstBuildDhcpRequest(&Frame, &GuestMac, RTNET_DHCP_MT_REQUEST, Offer.YiAddr, Offer.ServerId);
        rc = VBoxNetSlirpNATTestInput(hNat, Frame.ab, Frame.cb);
        RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

        TSTDHCPOFFER Ack;
        RT_ZERO(Ack);
        if (RT_SUCCESS(rc))
            rc = tstWaitForDhcpResponse(hNat, &Queue, &GuestMac, RTNET_DHCP_MT_ACK, &Ack);
        bool const fAck = RT_SUCCESS(rc);
        RTTEST_CHECK_MSG(g_hTest, fAck, (g_hTest, "No valid DHCPACK was returned\n"));
        if (fAck)
        {
            RTTEST_CHECK(g_hTest, Ack.YiAddr.u == Offer.YiAddr.u);
            RTTEST_CHECK(g_hTest, Ack.fNetmask && Ack.Netmask.u == Cfg.IPv4Netmask.u);
            RTTEST_CHECK(g_hTest, Ack.fServerId && Ack.ServerId.u == Cfg.IPv4Host.u);
        }
    }

    VBoxNetSlirpNATTestDestroy(hNat);
}


/** Verifies gateway ARP and negative ARP behavior directly through libslirp. */
static void tstArp(void)
{
    RTTestSub(g_hTest, "restricted libslirp ARP");

    VBOXNETSLIRPNATTESTCFG Cfg;
    tstNatConfigInit(&Cfg, false);
    TSTFRAMEQUEUE Queue;
    RT_ZERO(Queue);
    PVBOXNETSLIRPNATTEST hNat = NULL;
    int rc = VBoxNetSlirpNATTestCreate(&Cfg, tstCaptureOutput, &Queue, &hNat);
    RTTEST_CHECK_RC_RETV(g_hTest, rc, VINF_SUCCESS);

    RTMAC const GuestMac = tstMac(0x08, 0x00, 0x27, 0xaa, 0xbb, 0x16);
    RTNETADDRIPV4 const GuestIp = tstIPv4(10, 0, 2, 16);
    TSTFRAME Frame;
    tstBuildArpRequest(&Frame, &GuestMac, GuestIp, Cfg.IPv4Host);
    rc = VBoxNetSlirpNATTestInput(hNat, Frame.ab, Frame.cb);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    RTMAC GatewayMac;
    RT_ZERO(GatewayMac);
    if (RT_SUCCESS(rc))
        rc = tstWaitForArpReply(hNat, &Queue, Cfg.IPv4Host, GuestIp, &GuestMac, &GatewayMac);
    RTTEST_CHECK_MSG(g_hTest, RT_SUCCESS(rc),
                     (g_hTest, "No valid gateway ARP reply was returned\n"));
    RTTEST_CHECK(g_hTest, !tstMacEqual(&GatewayMac, &GuestMac));

    RT_ZERO(Queue);
    tstBuildArpRequest(&Frame, &GuestMac, GuestIp, tstIPv4(10, 0, 2, 99));
    rc = VBoxNetSlirpNATTestInput(hNat, Frame.ab, Frame.cb);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = VBoxNetSlirpNATTestPoll(hNat, 20);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    RTTEST_CHECK_MSG(g_hTest, Queue.cFrames == 0,
                     (g_hTest, "libslirp answered ARP for an unowned guest-network address\n"));

    VBoxNetSlirpNATTestDestroy(hNat);
}


/** Verifies production-sized frame filtering and malformed-input drops. */
static void tstMalformedFrames(void)
{
    RTTestSub(g_hTest, "malformed and oversized Ethernet input");

    VBOXNETSLIRPNATTESTCFG Cfg;
    tstNatConfigInit(&Cfg, false);
    TSTFRAMEQUEUE Queue;
    RT_ZERO(Queue);
    PVBOXNETSLIRPNATTEST hNat = NULL;
    int rc = VBoxNetSlirpNATTestCreate(&Cfg, tstCaptureOutput, &Queue, &hNat);
    RTTEST_CHECK_RC_RETV(g_hTest, rc, VINF_SUCCESS);

    uint8_t abFrame[1523];
    RT_ZERO(abFrame);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, NULL, sizeof(abFrame)), VERR_INVALID_PARAMETER);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, abFrame, 0), VERR_INVALID_PARAMETER);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, abFrame, _64K + 1), VERR_INVALID_PARAMETER);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, abFrame, 8), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, abFrame, sizeof(abFrame)), VINF_SUCCESS);

    RTNETETHERHDR *pEthernet = (RTNETETHERHDR *)abFrame;
    pEthernet->DstMac = tstMacBroadcast();
    pEthernet->SrcMac = tstMac(0x08, 0x00, 0x27, 0xaa, 0xbb, 0x17);
    pEthernet->EtherType = RT_H2N_U16(0x88b5);
    RTTEST_CHECK_RC(g_hTest, VBoxNetSlirpNATTestInput(hNat, abFrame, 60), VINF_SUCCESS);
    RTTEST_CHECK(g_hTest, Queue.cFrames == 0);

    VBoxNetSlirpNATTestDestroy(hNat);
}


/*********************************************************************************************************************************
*   Standalone Ring-3 Switch Fixture                                                                                             *
*********************************************************************************************************************************/
/** Creates a unique name for a service or network. */
static int tstMakeUuidName(const char *pszPrefix, char *pszName, size_t cbName)
{
    RTUUID Uuid;
    int rc = RTUuidCreate(&Uuid);
    if (RT_SUCCESS(rc))
    {
        char szUuid[RTUUID_STR_LENGTH];
        rc = RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));
        if (RT_SUCCESS(rc))
        {
            ssize_t const cch = RTStrPrintf2(pszName, cbName, "%s-%s", pszPrefix, szUuid);
            if (cch < 0 || (size_t)cch >= cbName)
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    return rc;
}


/** Returns whether a local IPC connection failure means no server is present. */
static bool tstServiceIsAbsent(int rc)
{
    return    rc == VERR_FILE_NOT_FOUND
           || rc == VERR_PATH_NOT_FOUND
           || rc == VERR_NET_CONNECTION_REFUSED
           || rc == VERR_PIPE_NOT_CONNECTED;
}


/** Reads the auto-started switch process ID. */
static int tstServiceReadPid(PTSTSWITCHSERVICE pService)
{
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pService->szPidFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        char szPid[32];
        size_t cbRead = 0;
        rc = RTFileRead(hFile, szPid, sizeof(szPid) - 1, &cbRead);
        int const rcClose = RTFileClose(hFile);
        if (RT_SUCCESS(rc))
            rc = rcClose;
        if (RT_SUCCESS(rc))
        {
            szPid[cbRead] = '\0';
            uint32_t uPid = NIL_RTPROCESS;
            rc = RTStrToUInt32Full(szPid, 10, &uPid);
            if (RT_SUCCESS(rc))
            {
                if (uPid == NIL_RTPROCESS || uPid == RTProcSelf())
                    rc = VERR_INVALID_PARAMETER;
                else
                    pService->hProcess = uPid;
            }
        }
    }
    return rc;
}


/** Waits for the auto-started switch to publish its process ID. */
static int tstServiceWaitForPid(PTSTSWITCHSERVICE pService)
{
    uint64_t const msStart = RTTimeMilliTS();
    int rc;
    do
    {
        rc = tstServiceReadPid(pService);
        if (RT_SUCCESS(rc))
            return rc;
        if (   rc != VERR_FILE_NOT_FOUND
            && rc != VERR_PATH_NOT_FOUND
            && rc != VERR_NO_DIGITS)
            return rc;
        RTThreadSleep(10);
    } while (RTTimeMilliTS() - msStart < TST_WAIT_MS);
    return rc;
}


/** Prepares unique environment variables used by the production auto-start path. */
static int tstServiceStart(PTSTSWITCHSERVICE pService)
{
    RT_ZERO(*pService);
    pService->hProcess = NIL_RTPROCESS;

    int rc = RTPathTemp(pService->szTempDir, sizeof(pService->szTempDir));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pService->szTempDir, sizeof(pService->szTempDir), "tstVBoxNatGuestSide-XXXXXX");
    if (RT_SUCCESS(rc))
    {
        rc = RTDirCreateTemp(pService->szTempDir, 0700);
        if (RT_SUCCESS(rc))
            pService->fTempDirCreated = true;
    }
    if (RT_SUCCESS(rc))
        rc = tstMakeUuidName("tst-vbox-nat", pService->szService, sizeof(pService->szService));
    if (RT_SUCCESS(rc))
        rc = RTPathExecDir(pService->szExec, sizeof(pService->szExec));
    if (RT_SUCCESS(rc))
#ifdef RT_OS_WINDOWS
        rc = RTPathAppend(pService->szExec, sizeof(pService->szExec), "VBoxIntNetR3SwitchTestHelper.exe");
#else
        rc = RTPathAppend(pService->szExec, sizeof(pService->szExec), "VBoxIntNetR3SwitchTestHelper");
#endif
    if (RT_SUCCESS(rc))
        rc = RTStrCopy(pService->szPidFile, sizeof(pService->szPidFile), pService->szTempDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pService->szPidFile, sizeof(pService->szPidFile), "switch.pid");
    if (RT_SUCCESS(rc))
        rc = RTStrCopy(pService->szLockFile, sizeof(pService->szLockFile), pService->szTempDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pService->szLockFile, sizeof(pService->szLockFile), "switch.lock");
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SVC_NAME", pService->szService);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_EXE", pService->szExec);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_PID_FILE", pService->szPidFile);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_LOCK_FILE", pService->szLockFile);
    if (RT_SUCCESS(rc))
    {
        RTLOCALIPCSESSION hExisting = NIL_RTLOCALIPCSESSION;
        rc = RTLocalIpcSessionConnect(&hExisting, pService->szService,
                                      RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
        if (RT_SUCCESS(rc))
        {
            RTLocalIpcSessionClose(hExisting);
            rc = VERR_ALREADY_EXISTS;
        }
        else if (tstServiceIsAbsent(rc))
            rc = VINF_SUCCESS;
    }
    return rc;
}


/** Removes a stale POSIX local IPC filesystem node after the helper exits. */
static void tstServiceCleanupEndpoint(PTSTSWITCHSERVICE pService)
{
    if (!pService->szService[0])
        return;

    RTLOCALIPCSESSION hExisting = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcSessionConnect(&hExisting, pService->szService,
                                      RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
    if (RT_SUCCESS(rc))
    {
        RTLocalIpcSessionClose(hExisting);
        RTTestFailed(g_hTest, "Switch IPC endpoint still accepts clients after helper exit");
        return;
    }
    if (rc == VERR_NET_CONNECTION_REFUSED)
        RTTestFailed(g_hTest, "Switch IPC endpoint pathname was left behind after helper exit");
    else if (!tstServiceIsAbsent(rc))
    {
        RTTestFailed(g_hTest, "Checking switch IPC endpoint failed: %Rrc", rc);
        return;
    }

    RTLOCALIPCSERVER hCleanup = NIL_RTLOCALIPCSERVER;
    rc = RTLocalIpcServerCreate(&hCleanup, pService->szService, RTLOCALIPC_FLAGS_RESTRICT_TO_USER);
    if (RT_SUCCESS(rc))
        RTLocalIpcServerDestroy(hCleanup);
    else
        RTTestFailed(g_hTest, "Cleaning switch IPC endpoint failed: %Rrc", rc);
}


/** Waits for the oneshot helper and removes all fixture state. */
static void tstServiceStop(PTSTSWITCHSERVICE pService)
{
    if (   !pService->fProcessReaped
        && pService->hProcess == NIL_RTPROCESS
        && pService->szPidFile[0])
    {
        uint64_t const msStart = RTTimeMilliTS();
        int rc;
        do
        {
            rc = tstServiceReadPid(pService);
            if (RT_SUCCESS(rc) || (rc != VERR_FILE_NOT_FOUND && rc != VERR_PATH_NOT_FOUND))
                break;
            RTThreadSleep(10);
        } while (RTTimeMilliTS() - msStart < RT_MS_1SEC);
    }

    if (pService->hProcess != NIL_RTPROCESS)
    {
        RTPROCSTATUS Status;
        RT_ZERO(Status);
        int rc = VERR_PROCESS_RUNNING;
        uint64_t const msStart = RTTimeMilliTS();
        while (rc == VERR_PROCESS_RUNNING && RTTimeMilliTS() - msStart < RT_MS_5SEC)
        {
            rc = RTProcWait(pService->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &Status);
            if (rc == VERR_PROCESS_RUNNING)
                RTThreadSleep(10);
        }
        if (rc == VERR_PROCESS_RUNNING)
        {
            RTTestFailed(g_hTest, "Switch helper did not exit after its last client disconnected");
            int const rcTerminate = RTProcTerminate(pService->hProcess);
            if (RT_SUCCESS(rcTerminate))
                rc = RTProcWait(pService->hProcess, RTPROCWAIT_FLAGS_BLOCK, &Status);
            else
                rc = rcTerminate;
        }
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Waiting for switch helper failed: %Rrc", rc);
        else if (Status.enmReason != RTPROCEXITREASON_NORMAL || Status.iStatus != RTEXITCODE_SUCCESS)
            RTTestFailed(g_hTest, "Switch helper exit reason/status: %d/%d", Status.enmReason, Status.iStatus);
        else
            pService->fProcessReaped = true;
        pService->hProcess = NIL_RTPROCESS;
    }

    tstServiceCleanupEndpoint(pService);
    RTEnvUnset("VBOX_INTNET_R3_SWITCH_LOCK_FILE");
    RTEnvUnset("VBOX_INTNET_R3_SWITCH_PID_FILE");
    RTEnvUnset("VBOX_INTNET_R3_SWITCH_EXE");
    RTEnvUnset("VBOX_INTNET_R3_SVC_NAME");

    if (pService->szPidFile[0])
        RTFileDelete(pService->szPidFile);
    if (pService->szLockFile[0])
        RTFileDelete(pService->szLockFile);
    if (pService->fTempDirCreated)
    {
        int const rc = RTDirRemove(pService->szTempDir);
        if (RT_FAILURE(rc) && rc != VERR_PATH_NOT_FOUND && rc != VERR_FILE_NOT_FOUND)
            RTTestFailed(g_hTest, "Removing switch helper temporary directory failed: %Rrc", rc);
    }
}


/*********************************************************************************************************************************
*   Ring-3 IntNet / NAT Integration                                                                                              *
*********************************************************************************************************************************/
/** Commits one libslirp output frame to the NAT-facing IntNet interface. */
static DECLCALLBACK(int) tstIntNetOutput(const void *pvFrame, size_t cbFrame, void *pvUser)
{
    PTSTNATOUTPUT pOutput = (PTSTNATOUTPUT)pvUser;
    if (!pOutput || cbFrame > UINT32_MAX)
        return VERR_INVALID_PARAMETER;

    INTNETFRAME Frame;
    int rc = IntNetR3IfQueryOutputFrame(pOutput->hIf, (uint32_t)cbFrame, &Frame);
    if (RT_SUCCESS(rc))
    {
        memcpy(Frame.pvFrame, pvFrame, cbFrame);
        rc = IntNetR3IfOutputFrameCommit(pOutput->hIf, &Frame);
    }
    return rc;
}


/** Injects one NAT-facing IntNet frame into libslirp. */
static DECLCALLBACK(void) tstNatInput(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    PTSTNATPUMP pPump = (PTSTNATPUMP)pvUser;
    int const rc = VBoxNetSlirpNATTestInput(pPump->hNat, pvFrame, cbFrame);
    if (RT_FAILURE(rc) && RT_SUCCESS(pPump->rcInput))
        pPump->rcInput = rc;
}


/** Runs the NAT-facing IntNet receive pump. */
static DECLCALLBACK(int) tstNatPumpThread(RTTHREAD hThread, void *pvUser)
{
    PTSTNATPUMP pPump = (PTSTNATPUMP)pvUser;
    RTThreadUserSignal(hThread);
    return IntNetR3IfPumpPkts(pPump->hIf, tstNatInput, pPump, NULL, NULL);
}


/** Captures only the expected gateway ARP reply on the guest interface. */
static DECLCALLBACK(void) tstGuestInput(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    PTSTGUESTRX pRx = (PTSTGUESTRX)pvUser;
    if (!pRx->fReply && cbFrame <= sizeof(pRx->Reply.ab))
    {
        TSTFRAME Frame;
        Frame.cb = cbFrame;
        memcpy(Frame.ab, pvFrame, cbFrame);
        if (tstIsArpReply(&Frame, pRx->GatewayIp, pRx->GuestIp, &pRx->GuestMac, NULL))
        {
            pRx->Reply = Frame;
            pRx->fReply = true;
            RTSemEventSignal(pRx->hReplyEvent);
        }
    }
}


/** Runs the guest-facing IntNet receive pump. */
static DECLCALLBACK(int) tstGuestRxThread(RTTHREAD hThread, void *pvUser)
{
    PTSTGUESTRX pRx = (PTSTGUESTRX)pvUser;
    RTThreadUserSignal(hThread);
    return IntNetR3IfPumpPkts(pRx->hIf, tstGuestInput, pRx, NULL, NULL);
}


/** Starts one waitable IntNet pump thread. */
static int tstPumpStart(PRTTHREAD phThread, PFNRTTHREAD pfnThread, void *pvUser, const char *pszName)
{
    int rc = RTThreadCreate(phThread, pfnThread, pvUser, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, pszName);
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(*phThread, RT_MS_1SEC);
    return rc;
}


/** Aborts and joins one IntNet pump. */
static void tstPumpStop(INTNETIFCTX hIf, PRTTHREAD phThread, const char *pszName)
{
    if (*phThread == NIL_RTTHREAD)
        return;

    int rc = IntNetR3IfWaitAbort(hIf);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    rc = RTThreadWait(*phThread, RT_MS_5SEC, &rcThread);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "%s did not stop within five seconds: %Rrc", pszName, rc);
        int const rcAbort = IntNetR3IfWaitAbort(hIf);
        RTTEST_CHECK_RC(g_hTest, rcAbort, VINF_SUCCESS);
        rc = RTThreadWait(*phThread, RT_INDEFINITE_WAIT, &rcThread);
    }
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_FAILURE(rc))
        exit(RTEXITCODE_FAILURE);
    RTTEST_CHECK_MSG(g_hTest, rcThread == VERR_SEM_DESTROYED,
                     (g_hTest, "%s returned %Rrc instead of %Rrc\n", pszName, rcThread, VERR_SEM_DESTROYED));
    *phThread = NIL_RTTHREAD;
}


/** Sends one Ethernet frame through an IntNet interface. */
static int tstIntNetSend(INTNETIFCTX hIf, PCTSTFRAME pFrame)
{
    INTNETFRAME Frame;
    int rc = IntNetR3IfQueryOutputFrame(hIf, (uint32_t)pFrame->cb, &Frame);
    if (RT_SUCCESS(rc))
    {
        memcpy(Frame.pvFrame, pFrame->ab, pFrame->cb);
        rc = IntNetR3IfOutputFrameCommit(hIf, &Frame);
    }
    return rc;
}


/** Verifies an ARP request/reply through the external local IPC R3 switch. */
static void tstR3IntNetNatArp(void)
{
    RTTestSub(g_hTest, "R3 IntNet switch -> restricted NAT -> R3 IntNet");

    TSTSWITCHSERVICE Service;
    int rc = tstServiceStart(&Service);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_FAILURE(rc))
    {
        tstServiceStop(&Service);
        return;
    }

    INTNETIFCTX hGuestIf = NULL;
    INTNETIFCTX hNatIf   = NULL;
    PVBOXNETSLIRPNATTEST hNat = NULL;
    TSTNATPUMP NatPump;
    RT_ZERO(NatPump);
    NatPump.hThread = NIL_RTTHREAD;
    NatPump.rcInput = VINF_SUCCESS;
    TSTGUESTRX GuestRx;
    RT_ZERO(GuestRx);
    GuestRx.hThread     = NIL_RTTHREAD;
    GuestRx.hReplyEvent = NIL_RTSEMEVENT;

    char szNetwork[INTNET_MAX_NETWORK_NAME];
    rc = tstMakeUuidName("tst-nat-r3", szNetwork, sizeof(szNetwork));
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfCreate(&hGuestIf, szNetwork);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = tstServiceWaitForPid(&Service);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfCreate(&hNatIf, szNetwork);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    RTMAC const GuestMac = tstMac(0x08, 0x00, 0x27, 0xaa, 0xbb, 0x18);
    RTMAC const NatMac   = tstMac(0x52, 0x54, 0x00, 0x12, 0x35, 0x00);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfSetMacAddress(hGuestIf, &GuestMac);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfSetMacAddress(hNatIf, &NatMac);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfSetActive(hGuestIf, true);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = IntNetR3IfSetActive(hNatIf, true);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    VBOXNETSLIRPNATTESTCFG Cfg;
    tstNatConfigInit(&Cfg, false);
    TSTNATOUTPUT NatOutput;
    NatOutput.hIf = hNatIf;
    if (RT_SUCCESS(rc))
        rc = VBoxNetSlirpNATTestCreate(&Cfg, tstIntNetOutput, &NatOutput, &hNat);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    NatPump.hIf  = hNatIf;
    NatPump.hNat = hNat;
    if (RT_SUCCESS(rc))
        rc = tstPumpStart(&NatPump.hThread, tstNatPumpThread, &NatPump, "NatR3Pump");
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    GuestRx.hIf       = hGuestIf;
    GuestRx.GatewayIp = Cfg.IPv4Host;
    GuestRx.GuestIp   = tstIPv4(10, 0, 2, 18);
    GuestRx.GuestMac  = GuestMac;
    if (RT_SUCCESS(rc))
        rc = RTSemEventCreate(&GuestRx.hReplyEvent);
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = tstPumpStart(&GuestRx.hThread, tstGuestRxThread, &GuestRx, "GuestR3Rx");
    RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);

    if (RT_SUCCESS(rc))
    {
        TSTFRAME Request;
        tstBuildArpRequest(&Request, &GuestMac, GuestRx.GuestIp, Cfg.IPv4Host);
        rc = tstIntNetSend(hGuestIf, &Request);
        RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            rc = RTSemEventWait(GuestRx.hReplyEvent, TST_WAIT_MS);
        RTTEST_CHECK_RC(g_hTest, rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTEST_CHECK_MSG(g_hTest, GuestRx.fReply,
                             (g_hTest, "Guest did not receive the NAT gateway ARP reply through the R3 switch\n"));
    }

    tstPumpStop(hGuestIf, &GuestRx.hThread, "Guest R3 receive pump");
    tstPumpStop(hNatIf, &NatPump.hThread, "NAT R3 receive pump");
    RTTEST_CHECK_RC(g_hTest, NatPump.rcInput, VINF_SUCCESS);
    if (GuestRx.hReplyEvent != NIL_RTSEMEVENT)
        RTSemEventDestroy(GuestRx.hReplyEvent);
    VBoxNetSlirpNATTestDestroy(hNat);
    if (hNatIf)
        IntNetR3IfDestroy(hNatIf);
    if (hGuestIf)
        IntNetR3IfDestroy(hGuestIf);
    tstServiceStop(&Service);
}


/*********************************************************************************************************************************
*   Main                                                                                                                         *
*********************************************************************************************************************************/
int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    int rc = RTTestInitAndCreate("tstVBoxNatGuestSide", &g_hTest);
    if (RT_FAILURE(rc))
        return rc;
    RTTestBanner(g_hTest);

    tstConfigValidation();
    tstDhcp();
    tstArp();
    tstMalformedFrames();
    tstR3IntNetNatArp();

    return RTTestSummaryAndDestroy(g_hTest);
}
