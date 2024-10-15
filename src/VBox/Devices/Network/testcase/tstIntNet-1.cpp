/* $Id: tstIntNet-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Testcase for internal networking, simple NetFlt trunk creation.
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
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/sup.h>
#include <VBox/vmm/vmm.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/alloc.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include <iprt/getopt.h>
#include <iprt/rand.h>
#include <iprt/log.h>
#include <iprt/crc.h>
#include <iprt/net.h>

#include "../Pcap.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int      g_cErrors = 0;
static uint64_t g_StartTS = 0;
static uint32_t g_DhcpXID = 0;
static bool     g_fDhcpReply = false;
static bool     g_fPingReply = false;
static uint32_t g_cOtherPkts = 0;
static uint32_t g_cArpPkts = 0;
static uint32_t g_cIpv4Pkts = 0;
static uint32_t g_cUdpPkts = 0;
static uint32_t g_cDhcpPkts = 0;
static uint32_t g_cTcpPkts = 0;


/**
 * Error reporting wrapper.
 *
 * @param   pErrStrm        The stream to write the error message to. Can be NULL.
 * @param   pszFormat       The message format string.
 * @param   ...             Format arguments.
 */
static void tstIntNetError(PRTSTREAM pErrStrm, const char *pszFormat, ...)
{
    if (!pErrStrm)
        pErrStrm = g_pStdOut;

    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintf(pErrStrm, "tstIntNet-1: ERROR - ");
    RTStrmPrintfV(pErrStrm, pszFormat, va);
    va_end(va);

    g_cErrors++;
}


/**
 * Parses a frame an runs in thru the RTNet validation code so it gets
 * some exercise.
 *
 * @param   pvFrame     Pointer to the ethernet frame.
 * @param   cbFrame     The size of the ethernet frame.
 * @param   pErrStrm    The error stream.
 */
static void tstIntNetTestFrame(void const *pvFrame, size_t cbFrame, PRTSTREAM pErrStrm, bool fGso)
{
    /*
     * Ethernet header.
     */
    PCRTNETETHERHDR pEtherHdr = (PCRTNETETHERHDR)pvFrame;
    if (cbFrame <= sizeof(*pEtherHdr))
        return tstIntNetError(pErrStrm, "cbFrame=%#x <= %#x (ether)\n", cbFrame, sizeof(*pEtherHdr));
    ssize_t cbLeft = cbFrame - sizeof(*pEtherHdr);
    uint8_t const *pbCur = (uint8_t const *)(pEtherHdr + 1);

    switch (RT_BE2H_U16(pEtherHdr->EtherType))
    {
        case RTNET_ETHERTYPE_ARP:
        {
            g_cArpPkts++;
            break;
        }

        case RTNET_ETHERTYPE_IPV4:
        {
            g_cIpv4Pkts++;

            PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)pbCur;
            if (!RTNetIPv4IsHdrValid(pIpHdr, cbLeft, cbLeft, !fGso /*fChecksum*/))
                return tstIntNetError(pErrStrm, "RTNetIPv4IsHdrValid failed\n");
            pbCur += pIpHdr->ip_hl * 4;
            cbLeft -= pIpHdr->ip_hl * 4;
            AssertFatal(cbLeft >= 0);

            switch (pIpHdr->ip_p)
            {
                case RTNETIPV4_PROT_ICMP:
                {
                    /** @todo ICMP? */
                    break;
                }

                case RTNETIPV4_PROT_UDP:
                {
                    g_cUdpPkts++;
                    PCRTNETUDP pUdpHdr = (PCRTNETUDP)pbCur;
                    if (!RTNetIPv4IsUDPValid(pIpHdr, pUdpHdr, pUdpHdr + 1, cbLeft, !fGso /*fChecksum*/))
                        return tstIntNetError(pErrStrm, "RTNetIPv4IsUDPValid failed\n");
                    pbCur += sizeof(*pUdpHdr);
                    cbLeft -= sizeof(*pUdpHdr);

                    if (RT_BE2H_U16(pUdpHdr->uh_dport) == RTNETIPV4_PORT_BOOTPS)
                    {
                        g_cDhcpPkts++;
                        PCRTNETBOOTP pDhcp = (PCRTNETBOOTP)pbCur;
                        if (!RTNetIPv4IsDHCPValid(pUdpHdr, pDhcp, cbLeft, NULL))
                            return tstIntNetError(pErrStrm, "RTNetIPv4IsDHCPValid failed\n");
                    }
                    break;
                }

                case RTNETIPV4_PROT_TCP:
                {
                    g_cTcpPkts++;
                    PCRTNETTCP pTcpHdr = (PCRTNETTCP)pbCur;
                    if (!RTNetIPv4IsTCPValid(pIpHdr, pTcpHdr, cbLeft, NULL, cbLeft, !fGso /*fChecksum*/))
                        return tstIntNetError(pErrStrm, "RTNetIPv4IsTCPValid failed\n");
                    break;
                }
            }
            break;
        }

        //case RTNET_ETHERTYPE_IPV6:
        default:
            g_cOtherPkts++;
            break;
    }
}


/**
 * Transmits one frame after appending the CRC.
 *
 * @param   hIf             The interface handle.
 * @param   pSession        The session.
 * @param   pBuf            The shared interface buffer.
 * @param   pvFrame         The frame without a crc.
 * @param   cbFrame         The size of it.
 * @param   pFileRaw        The file to write the raw data to (optional).
 * @param   pFileText       The file to write a textual packet summary to (optional).
 */
static void doXmitFrame(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, PINTNETBUF pBuf, void *pvFrame, size_t cbFrame, PRTSTREAM pFileRaw, PRTSTREAM pFileText)
{
    /*
     * Log it.
     */
    if (pFileText)
    {
        PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;
        uint64_t    NanoTS = RTTimeNanoTS() - g_StartTS;
        RTStrmPrintf(pFileText, "%3RU64.%09u: cb=%04x dst=%.6Rhxs src=%.6Rhxs type=%04x Send!\n",
                     NanoTS / 1000000000, (uint32_t)(NanoTS % 1000000000),
                     cbFrame, &pEthHdr->SrcMac, &pEthHdr->DstMac, RT_BE2H_U16(pEthHdr->EtherType));
    }

    /*
     * Run in thru the frame validator to test the RTNet code.
     */
    tstIntNetTestFrame(pvFrame, cbFrame, pFileText, false /*fGso*/);

    /*
     * Write the frame and push the queue.
     *
     * Don't bother with dealing with overflows like DrvIntNet does, because
     * it's not supposed to happen here in this testcase.
     */
    int rc = IntNetRingWriteFrame(&pBuf->Send, pvFrame, cbFrame);
    if (RT_SUCCESS(rc))
    {
        if (pFileRaw)
            PcapStreamFrame(pFileRaw, g_StartTS, pvFrame, cbFrame, 0xffff);
    }
    else
    {
        RTPrintf("tstIntNet-1: IntNetRingWriteFrame failed, %Rrc; cbFrame=%d pBuf->cbSend=%d\n", rc, cbFrame, pBuf->cbSend);
        g_cErrors++;
    }

    INTNETIFSENDREQ SendReq;
    SendReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    SendReq.Hdr.cbReq = sizeof(SendReq);
    SendReq.pSession = pSession;
    SendReq.hIf = hIf;
    rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_SEND, 0, &SendReq.Hdr);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstIntNet-1: SUPR3CallVMMR0Ex(,VMMR0_DO_INTNET_IF_SEND,) failed, rc=%Rrc\n", rc);
        g_cErrors++;
    }

}


/**
 * Does the transmit test.
 *
 * @param   hIf             The interface handle.
 * @param   pSession        The session.
 * @param   pBuf            The shared interface buffer.
 * @param   pSrcMac         The mac address to use as source.
 * @param   pFileRaw        The file to write the raw data to (optional).
 * @param   pFileText       The file to write a textual packet summary to (optional).
 */
static void doXmitTest(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, PINTNETBUF pBuf, PCRTMAC pSrcMac, PRTSTREAM pFileRaw, PRTSTREAM pFileText)
{
    uint8_t abFrame[4096];
    PRTNETETHERHDR      pEthHdr  = (PRTNETETHERHDR)&abFrame[0];
    PRTNETIPV4          pIpHdr   = (PRTNETIPV4)    (pEthHdr + 1);
    PRTNETUDP           pUdpHdr  = (PRTNETUDP)     (pIpHdr  + 1);
    PRTNETDHCP          pDhcpMsg = (PRTNETDHCP)    (pUdpHdr + 1);

    /*
     * Create a simple DHCP broadcast request.
     */
    memset(&abFrame, 0, sizeof(abFrame));

    pDhcpMsg->Op = 1; /* request */
    pDhcpMsg->HType = 1; /* ethernet */
    pDhcpMsg->HLen = sizeof(RTMAC);
    pDhcpMsg->Hops = 0;
    pDhcpMsg->XID = g_DhcpXID = RTRandU32();
    pDhcpMsg->Secs = 0;
    pDhcpMsg->Flags = 0; /* unicast */ //RT_H2BE_U16(0x8000); /* broadcast */
    pDhcpMsg->CIAddr.u = 0;
    pDhcpMsg->YIAddr.u = 0;
    pDhcpMsg->SIAddr.u = 0;
    pDhcpMsg->GIAddr.u = 0;
    memset(&pDhcpMsg->CHAddr[0], '\0', sizeof(pDhcpMsg->CHAddr));
    memcpy(&pDhcpMsg->CHAddr[0], pSrcMac, sizeof(*pSrcMac));
    memset(&pDhcpMsg->SName[0], '\0', sizeof(pDhcpMsg->SName));
    memset(&pDhcpMsg->File[0], '\0', sizeof(pDhcpMsg->File));
    pDhcpMsg->abMagic[0] = 99;
    pDhcpMsg->abMagic[1] = 130;
    pDhcpMsg->abMagic[2] = 83;
    pDhcpMsg->abMagic[3] = 99;

    pDhcpMsg->DhcpOpt = 53; /* DHCP Msssage Type option */
    pDhcpMsg->DhcpLen = 1;
    pDhcpMsg->DhcpReq = 1;  /* DHCPDISCOVER */

    memset(&pDhcpMsg->abOptions[0], '\0', sizeof(pDhcpMsg->abOptions));
    uint8_t *pbOpt = &pDhcpMsg->abOptions[0];

    *pbOpt++ = 116;         /* DHCP Auto-Configure */
    *pbOpt++ = 1;
    *pbOpt++ = 1;

    *pbOpt++ = 61;          /* Client identifier */
    *pbOpt++ = 1 + sizeof(*pSrcMac);
    *pbOpt++ = 1;           /* hw type: ethernet */
    memcpy(pbOpt, pSrcMac, sizeof(*pSrcMac));
    pbOpt += sizeof(*pSrcMac);

    *pbOpt++ = 12;          /* Host name */
    *pbOpt++ = sizeof("tstIntNet-1") - 1;
    memcpy(pbOpt, "tstIntNet-1", sizeof("tstIntNet-1") - 1);
    pbOpt += sizeof("tstIntNet-1") - 1;

    *pbOpt = 0xff;          /* the end */

    /* UDP */
    pUdpHdr->uh_sport = RT_H2BE_U16(68); /* bootp */
    pUdpHdr->uh_dport = RT_H2BE_U16(67); /* bootps */
    pUdpHdr->uh_ulen = RT_H2BE_U16(sizeof(*pDhcpMsg) + sizeof(*pUdpHdr));
    pUdpHdr->uh_sum = 0; /* pretend checksumming is disabled */

    /* IP */
    pIpHdr->ip_v = 4;
    pIpHdr->ip_hl = sizeof(*pIpHdr) / sizeof(uint32_t);
    pIpHdr->ip_tos = 0;
    pIpHdr->ip_len = RT_H2BE_U16(sizeof(*pDhcpMsg) + sizeof(*pUdpHdr) + sizeof(*pIpHdr));
    pIpHdr->ip_id = (uint16_t)RTRandU32();
    pIpHdr->ip_off = 0;
    pIpHdr->ip_ttl = 255;
    pIpHdr->ip_p = 0x11; /* UDP */
    pIpHdr->ip_sum = 0;
    pIpHdr->ip_src.u = 0;
    pIpHdr->ip_dst.u = UINT32_C(0xffffffff); /* broadcast */
    pIpHdr->ip_sum = RTNetIPv4HdrChecksum(pIpHdr);

    /* calc the UDP checksum. */
    pUdpHdr->uh_sum = RTNetIPv4UDPChecksum(pIpHdr, pUdpHdr, pUdpHdr + 1);

    /* Ethernet */
    memset(&pEthHdr->DstMac, 0xff, sizeof(pEthHdr->DstMac)); /* broadcast */
    pEthHdr->SrcMac = *pSrcMac;
    pEthHdr->EtherType = RT_H2BE_U16(RTNET_ETHERTYPE_IPV4); /* IP */

    doXmitFrame(hIf, pSession, pBuf, &abFrame[0], (uint8_t *)(pDhcpMsg + 1) - (uint8_t *)&abFrame[0], pFileRaw, pFileText);
}


static uint16_t icmpChecksum(PRTNETICMPV4HDR pHdr, size_t cbHdr)
{
    size_t cbLeft = cbHdr;
    uint16_t *pbSrc = (uint16_t *)pHdr;
    uint16_t oddByte = 0;
    int cSum = 0;

    while (cbLeft > 1)
    {
        cSum += *pbSrc++;
        cbLeft -= 2;
    }

    if (cbLeft == 1)
    {
        *(uint16_t *)(&oddByte) = *(uint16_t *)pbSrc;
        cSum += oddByte;
    }

    cSum = (cSum >> 16) + (cSum & 0xffff);
    cSum += (cSum >> 16);
    uint16_t Result = ~cSum;
    return Result;
}


/**
 * Does the rudimentary ping test with fixed destination and source IPs.
 *
 * @param   hIf             The interface handle.
 * @param   pSession        The session.
 * @param   pBuf            The shared interface buffer.
 * @param   pSrcMac         The mac address to use as source.
 * @param   pFileRaw        The file to write the raw data to (optional).
 * @param   pFileText       The file to write a textual packet summary to (optional).
 */
static void doPingTest(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, PINTNETBUF pBuf, PCRTMAC pSrcMac, PRTSTREAM pFileRaw, PRTSTREAM pFileText)
{
    uint8_t abFrame[4096];
    PRTNETETHERHDR      pEthHdr  = (PRTNETETHERHDR)&abFrame[0];
    PRTNETIPV4          pIpHdr   = (PRTNETIPV4)         (pEthHdr + 1);
    PRTNETICMPV4ECHO    pIcmpEcho = (PRTNETICMPV4ECHO)  (pIpHdr + 1);

    /*
     * Create a simple ping request.
     */
    memset(&abFrame, 0, sizeof(abFrame));

    pIcmpEcho->Hdr.icmp_type = RTNETICMPV4_TYPE_ECHO_REQUEST;
    pIcmpEcho->Hdr.icmp_code = 0;
    pIcmpEcho->icmp_id = 0x06;
    pIcmpEcho->icmp_seq = 0x05;
    size_t cbPad = 56;
    memset(&pIcmpEcho->icmp_data, '\0', cbPad);
    pIcmpEcho->Hdr.icmp_cksum = icmpChecksum(&pIcmpEcho->Hdr, cbPad + 8);

    /* IP */
    pIpHdr->ip_v = 4;
    pIpHdr->ip_hl = sizeof(*pIpHdr) / sizeof(uint32_t);
    pIpHdr->ip_tos = 0;
    pIpHdr->ip_len = RT_H2BE_U16((uint16_t)(sizeof(*pIcmpEcho) + cbPad + sizeof(*pIpHdr)));
    pIpHdr->ip_id = (uint16_t)RTRandU32();
    pIpHdr->ip_off = 0;
    pIpHdr->ip_ttl = 255;
    pIpHdr->ip_p = 0x01; /*ICMP */
    pIpHdr->ip_sum = 0;
    pIpHdr->ip_src.u = UINT32_C(0x9701A8C0);    /* 192.168.1.151 */
    pIpHdr->ip_dst.u = UINT32_C(0xF9A344D0);    /* 208.68.163.249 */
    pIpHdr->ip_sum = RTNetIPv4HdrChecksum(pIpHdr);

    /* Ethernet */
    memset(&pEthHdr->DstMac, 0xff, sizeof(pEthHdr->DstMac)); /* broadcast */

    pEthHdr->SrcMac = *pSrcMac;
#if 0   /* Enable with host's real Mac address for testing of the testcase. */
    pEthHdr->SrcMac.au8[0] = 0x00;
    pEthHdr->SrcMac.au8[1] = 0x1b;
    pEthHdr->SrcMac.au8[2] = 0x24;
    pEthHdr->SrcMac.au8[3] = 0xa0;
    pEthHdr->SrcMac.au8[4] = 0x2f;
    pEthHdr->SrcMac.au8[5] = 0xce;
#endif

    pEthHdr->EtherType = RT_H2BE_U16(RTNET_ETHERTYPE_IPV4); /* IP */

    doXmitFrame(hIf, pSession, pBuf, &abFrame[0], (uint8_t *)(pIcmpEcho + 1) + cbPad - (uint8_t *)&abFrame[0], pFileRaw, pFileText);
}


/**
 * Does packet sniffing for a given period of time.
 *
 * @param   hIf             The interface handle.
 * @param   pSession        The session.
 * @param   pBuf            The shared interface buffer.
 * @param   cMillies        The time period, ms.
 * @param   pFileRaw        The file to write the raw data to (optional).
 * @param   pFileText       The file to write a textual packet summary to (optional).
 * @param   pSrcMac         Out MAC address.
 */
static void doPacketSniffing(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, PINTNETBUF pBuf, uint32_t cMillies,
                             PRTSTREAM pFileRaw, PRTSTREAM pFileText, PCRTMAC pSrcMac)
{
    /*
     * The loop.
     */
    PINTNETRINGBUF pRingBuf = &pBuf->Recv;
    for (;;)
    {
        /*
         * Wait for a packet to become available.
         */
        uint64_t cElapsedMillies = (RTTimeNanoTS() - g_StartTS) / 1000000;
        if (cElapsedMillies >= cMillies)
            break;
        INTNETIFWAITREQ WaitReq;
        WaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        WaitReq.Hdr.cbReq = sizeof(WaitReq);
        WaitReq.pSession = pSession;
        WaitReq.hIf = hIf;
        WaitReq.cMillies = cMillies - (uint32_t)cElapsedMillies;
        int rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_WAIT, 0, &WaitReq.Hdr);
        if (rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED)
            break;
        if (RT_FAILURE(rc))
        {
            g_cErrors++;
            RTPrintf("tstIntNet-1: VMMR0_DO_INTNET_IF_WAIT returned %Rrc\n", rc);
            break;
        }

        /*
         * Process the receive buffer.
         */
        PINTNETHDR pHdr;
        while ((pHdr = IntNetRingGetNextFrameToRead(pRingBuf)))
        {
            if (pHdr->u8Type == INTNETHDR_TYPE_FRAME)
            {
                size_t      cbFrame = pHdr->cbFrame;
                const void *pvFrame = IntNetHdrGetFramePtr(pHdr, pBuf);
                uint64_t    NanoTS  = RTTimeNanoTS() - g_StartTS;

                if (pFileRaw)
                    PcapStreamFrame(pFileRaw, g_StartTS, pvFrame, cbFrame, 0xffff);

                PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;
                if (pFileText)
                    RTStrmPrintf(pFileText, "%3RU64.%09u: cb=%04x dst=%.6Rhxs src=%.6Rhxs type=%04x%s\n",
                                 NanoTS / 1000000000, (uint32_t)(NanoTS % 1000000000),
                                 cbFrame, &pEthHdr->DstMac, &pEthHdr->SrcMac, RT_BE2H_U16(pEthHdr->EtherType),
                                 !memcmp(&pEthHdr->DstMac, pSrcMac, sizeof(*pSrcMac)) ? " Mine!" : "");
                tstIntNetTestFrame(pvFrame, cbFrame, pFileText, false /*fGso*/);

                /* Loop for the DHCP reply. */
                if (    cbFrame > 64
                    &&  RT_BE2H_U16(pEthHdr->EtherType) == 0x0800 /* EtherType == IP */)
                {
                    PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)(pEthHdr + 1);
                    PCRTNETUDP pUdpHdr = (PCRTNETUDP)((uint32_t *)pIpHdr + pIpHdr->ip_hl);
                    if (    pIpHdr->ip_p == 0x11 /*UDP*/
                        &&  RT_BE2H_U16(pUdpHdr->uh_dport) == 68 /* bootp */
                        &&  RT_BE2H_U16(pUdpHdr->uh_sport) == 67 /* bootps */)
                    {
                        PCRTNETDHCP pDhcpMsg = (PCRTNETDHCP)(pUdpHdr + 1);
                        if (    pDhcpMsg->Op == 2 /* boot reply */
                            &&  pDhcpMsg->HType == 1 /* ethernet */
                            &&  pDhcpMsg->HLen == sizeof(RTMAC)
                            &&  (pDhcpMsg->XID == g_DhcpXID || !g_DhcpXID)
                            &&  !memcmp(&pDhcpMsg->CHAddr[0], pSrcMac, sizeof(*pSrcMac)))
                        {
                            g_fDhcpReply = true;
                            RTPrintf("tstIntNet-1: DHCP server reply! My IP: %d.%d.%d.%d\n",
                                     pDhcpMsg->YIAddr.au8[0],
                                     pDhcpMsg->YIAddr.au8[1],
                                     pDhcpMsg->YIAddr.au8[2],
                                     pDhcpMsg->YIAddr.au8[3]);
                        }
                    }
                    else if (pIpHdr->ip_p == 0x01)  /* ICMP */
                    {
                        PRTNETICMPV4HDR pIcmpHdr = (PRTNETICMPV4HDR)(pIpHdr + 1);
                        PRTNETICMPV4ECHO pIcmpEcho = (PRTNETICMPV4ECHO)(pIpHdr + 1);
                        if (   pIcmpHdr->icmp_type == RTNETICMPV4_TYPE_ECHO_REPLY
                            && pIcmpEcho->icmp_seq == 0x05
                            && pIpHdr->ip_dst.u == UINT32_C(0x9701A8C0)
#if 0
                            /** Enable with the host's real Mac address for testing of the testcase.*/
                            && pEthHdr->DstMac.au8[0] == 0x00
                            && pEthHdr->DstMac.au8[1] == 0x1b
                            && pEthHdr->DstMac.au8[2] == 0x24
                            && pEthHdr->DstMac.au8[3] == 0xa0
                            && pEthHdr->DstMac.au8[4] == 0x2f
                            && pEthHdr->DstMac.au8[5] == 0xce
#else
                            && pEthHdr->DstMac.au16[0] == pSrcMac->au16[0]
                            && pEthHdr->DstMac.au16[1] == pSrcMac->au16[1]
                            && pEthHdr->DstMac.au16[2] == pSrcMac->au16[2]
#endif
                         )
                        {
                            g_fPingReply = true;
                            RTPrintf("tstIntNet-1: Ping reply! From %d.%d.%d.%d\n",
                                    pIpHdr->ip_src.au8[0],
                                    pIpHdr->ip_src.au8[1],
                                    pIpHdr->ip_src.au8[2],
                                    pIpHdr->ip_src.au8[3]);
                        }
                        else
                            RTPrintf("type=%d seq=%d dstmac=%.6Rhxs ip=%d.%d.%d.%d\n", pIcmpHdr->icmp_type, pIcmpEcho->icmp_seq,
                                    &pEthHdr->DstMac, pIpHdr->ip_dst.au8[0], pIpHdr->ip_dst.au8[1], pIpHdr->ip_dst.au8[2], pIpHdr->ip_dst.au8[3]);
                    }
                }
            }
            else if (pHdr->u8Type == INTNETHDR_TYPE_GSO)
            {
                PCPDMNETWORKGSO pGso    = IntNetHdrGetGsoContext(pHdr, pBuf);
                size_t          cbFrame = pHdr->cbFrame;
                if (PDMNetGsoIsValid(pGso, cbFrame, cbFrame - sizeof(*pGso)))
                {
                    const void *pvFrame = pGso + 1;
                    uint64_t    NanoTS  = RTTimeNanoTS() - g_StartTS;
                    cbFrame -= sizeof(pGso);

                    if (pFileRaw)
                        PcapStreamGsoFrame(pFileRaw, g_StartTS, pGso, pvFrame, cbFrame, 0xffff);

                    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;
                    if (pFileText)
                        RTStrmPrintf(pFileText, "%3RU64.%09u: cb=%04x dst=%.6Rhxs src=%.6Rhxs type=%04x%s [GSO]\n",
                                     NanoTS / 1000000000, (uint32_t)(NanoTS % 1000000000),
                                     cbFrame, &pEthHdr->DstMac, &pEthHdr->SrcMac, RT_BE2H_U16(pEthHdr->EtherType),
                                     !memcmp(&pEthHdr->DstMac, pSrcMac, sizeof(*pSrcMac)) ? " Mine!" : "");
                    tstIntNetTestFrame(pvFrame, cbFrame, pFileText, true /*fGso*/);
                }
                else
                {
                    RTPrintf("tstIntNet-1: Bad GSO frame: %.*Rhxs\n", sizeof(*pGso), pGso);
                    STAM_REL_COUNTER_INC(&pBuf->cStatBadFrames);
                    g_cErrors++;
                }
            }
            else if (pHdr->u8Type != INTNETHDR_TYPE_PADDING)
            {
                RTPrintf("tstIntNet-1: Unknown frame type %d\n", pHdr->u8Type);
                STAM_REL_COUNTER_INC(&pBuf->cStatBadFrames);
                g_cErrors++;
            }

            /* Advance to the next frame. */
            IntNetRingSkipFrame(pRingBuf);
        }
    }

    uint64_t NanoTS = RTTimeNanoTS() - g_StartTS;
    RTStrmPrintf(pFileText ? pFileText : g_pStdOut,
                 "%3RU64.%09u: stopped. cRecvs=%RU64 cbRecv=%RU64 cLost=%RU64 cOYs=%RU64 cNYs=%RU64\n",
                 NanoTS / 1000000000, (uint32_t)(NanoTS % 1000000000),
                 pBuf->Recv.cStatFrames.c,
                 pBuf->Recv.cbStatWritten.c,
                 pBuf->cStatLost.c,
                 pBuf->cStatYieldsOk.c,
                 pBuf->cStatYieldsNok.c
                 );
    RTStrmPrintf(pFileText ? pFileText : g_pStdOut,
                 "%3RU64.%09u: cOtherPkts=%RU32 cArpPkts=%RU32 cIpv4Pkts=%RU32 cTcpPkts=%RU32 cUdpPkts=%RU32 cDhcpPkts=%RU32\n",
                 NanoTS / 1000000000, (uint32_t)(NanoTS % 1000000000),
                 g_cOtherPkts, g_cArpPkts, g_cIpv4Pkts, g_cTcpPkts, g_cUdpPkts, g_cDhcpPkts);
}

#ifdef RT_OS_LINUX
#include <stdio.h>
#include <net/if.h>
#include <net/route.h>
/**
 * Obtain the name of the interface used for default routing.
 *
 * NOTE: Copied from Main/src-server/linux/NetIf-linux.cpp
 *
 * @returns VBox status code.
 *
 * @param   pszName     The buffer where to put the name.
 * @param   cbName      The buffer length.
 */
static int getDefaultIfaceName(char *pszName, size_t cbName)
{
    FILE *fp = fopen("/proc/net/route", "r");
    char szBuf[1024];
    char szIfName[17];
    uint32_t uAddr;
    uint32_t uGateway;
    uint32_t uMask;
    int  iTmp;
    unsigned uFlags;

    if (fp)
    {
        while (fgets(szBuf, sizeof(szBuf)-1, fp))
        {
            int n = sscanf(szBuf, "%16s %x %x %x %d %d %d %x %d %d %d\n",
                           szIfName, &uAddr, &uGateway, &uFlags, &iTmp, &iTmp, &iTmp,
                           &uMask, &iTmp, &iTmp, &iTmp);
            if (n < 10 || !(uFlags & RTF_UP))
                continue;

            if (uAddr == 0 && uMask == 0)
            {
                fclose(fp);
                szIfName[sizeof(szIfName) - 1] = '\0';
                return RTStrCopy(pszName, cbName, szIfName);
            }
        }
        fclose(fp);
    }
    return VERR_INTERNAL_ERROR;
}
#endif /* RT_OS_LINUX */


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF(envp);

    /*
     * Init the runtime and parse the arguments.
     */
    RTR3InitExe(argc, &argv, 0);

    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--duration",     'd', RTGETOPT_REQ_UINT32 },
        { "--file",         'f', RTGETOPT_REQ_STRING },
        { "--interface",    'i', RTGETOPT_REQ_STRING },
        { "--mac-sharing",  'm', RTGETOPT_REQ_NOTHING },
        { "--network",      'n', RTGETOPT_REQ_STRING },
        { "--promiscuous",  'p', RTGETOPT_REQ_NOTHING },
        { "--recv-buffer",  'r', RTGETOPT_REQ_UINT32 },
        { "--send-buffer",  's', RTGETOPT_REQ_UINT32 },
        { "--sniffer",      'S', RTGETOPT_REQ_NOTHING },
        { "--text-file",    't', RTGETOPT_REQ_STRING },
        { "--xmit-test",    'x', RTGETOPT_REQ_NOTHING },
        { "--ping-test",    'P', RTGETOPT_REQ_NOTHING },
    };

    uint32_t    cMillies = 1000;
    PRTSTREAM   pFileRaw = NULL;
#ifdef RT_OS_DARWIN
    const char *pszIf = "en0";
#elif defined(RT_OS_LINUX)
    char        szIf[IFNAMSIZ+1] = "eth0"; /* Reasonable default */
    /*
     * Try to update the default interface by consulting the routing table.
     * If we fail we still have our reasonable default.
     */
    getDefaultIfaceName(szIf, sizeof(szIf));
    const char *pszIf = szIf;
#elif defined(RT_OS_SOLARIS)
    const char* pszIf = "rge0";
#else
    const char *pszIf = "em0";
#endif
    bool        fMacSharing = false;
    const char *pszNetwork = "tstIntNet-1";
    bool        fPromiscuous = false;
    uint32_t    cbRecv = 0;
    uint32_t    cbSend = 0;
    bool        fSniffer = false;
    PRTSTREAM   pFileText = g_pStdOut;
    bool        fXmitTest = false;
    bool        fPingTest = false;
    RTMAC       SrcMac;
    SrcMac.au8[0] = 0x08;
    SrcMac.au8[1] = 0x03;
    SrcMac.au8[2] = 0x86;
    RTRandBytes(&SrcMac.au8[3], sizeof(SrcMac) - 3);

    int rc;
    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &Value)))
        switch (ch)
        {
            case 'd':
                cMillies = Value.u32 * 1000;
                if (cMillies / 1000 != Value.u32)
                {
                    RTPrintf("tstIntNet-1: warning duration overflowed\n");
                    cMillies = UINT32_MAX - 1;
                }
                break;

            case 'f':
                rc = RTStrmOpen(Value.psz, "w+b", &pFileRaw);
                if (RT_FAILURE(rc))
                {
                    RTPrintf("tstIntNet-1: Failed to creating \"%s\" for writing: %Rrc\n", Value.psz, rc);
                    return 1;
                }
                break;

            case 'i':
                pszIf = Value.psz;
                if (strlen(pszIf) >= INTNET_MAX_TRUNK_NAME)
                {
                    RTPrintf("tstIntNet-1: Interface name is too long (max %d chars): %s\n", INTNET_MAX_TRUNK_NAME - 1, pszIf);
                    return 1;
                }
                break;

            case 'm':
                fMacSharing = true;
                break;

            case 'n':
                pszNetwork = Value.psz;
                if (strlen(pszNetwork) >= INTNET_MAX_NETWORK_NAME)
                {
                    RTPrintf("tstIntNet-1: Network name is too long (max %d chars): %s\n", INTNET_MAX_NETWORK_NAME - 1, pszNetwork);
                    return 1;
                }
                break;

            case 'p':
                fPromiscuous = true;
                break;

            case 'r':
                cbRecv = Value.u32;
                break;

            case 's':
                cbSend = Value.u32;
                break;

            case 'S':
                fSniffer = true;
                break;

            case 't':
                if (!*Value.psz)
                    pFileText = NULL;
                else if (!strcmp(Value.psz, "-"))
                    pFileText = g_pStdOut;
                else if (!strcmp(Value.psz, "!"))
                    pFileText = g_pStdErr;
                else
                {
                    rc = RTStrmOpen(Value.psz, "w", &pFileText);
                    if (RT_FAILURE(rc))
                    {
                        RTPrintf("tstIntNet-1: Failed to creating \"%s\" for writing: %Rrc\n", Value.psz, rc);
                        return 1;
                    }
                }
                break;

            case 'x':
                fXmitTest = true;
                break;

            case 'P':
                fPingTest = true;
                break;

            case 'h':
                RTPrintf("syntax: tstIntNet-1 <options>\n"
                         "\n"
                         "Options:\n");
                for (size_t i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                    RTPrintf("    -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                RTPrintf("\n"
                         "Examples:\n"
                         "    tstIntNet-1 -r 8192 -s 4096 -xS\n"
                         "    tstIntNet-1 -n VBoxNetDhcp -r 4096 -s 4096 -i \"\" -xS\n");
                return 1;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &Value);
        }

    RTPrintf("tstIntNet-1: TESTING...\n");

    /*
     * Open the session, load ring-0 and issue the request.
     */
    PSUPDRVSESSION pSession;
    rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstIntNet-1: SUPR3Init -> %Rrc\n", rc);
        return 1;
    }

    static const char s_szVmmR0[] = "/../VMMR0.r0";
    char              szPath[RTPATH_MAX];
    rc = RTPathExecDir(szPath, sizeof(szPath) - sizeof(s_szVmmR0));
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstIntNet-1: RTPathExecDir -> %Rrc\n", rc);
        return 1;
    }
    memcpy(strchr(szPath, '\0'), s_szVmmR0, sizeof(s_szVmmR0));

    char szAbsPath[RTPATH_MAX];
    rc = RTPathAbs(szPath, szAbsPath, sizeof(szAbsPath));
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstIntNet-1: RTPathAbs -> %Rrc\n", rc);
        return 1;
    }

    rc = SUPR3LoadVMM(szAbsPath, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstIntNet-1: SUPR3LoadVMM(\"%s\") -> %Rrc\n", szAbsPath, rc);
        return 1;
    }

    /*
     * Create the request, picking the network and trunk names from argv[2]
     * and argv[1] if present.
     */
    INTNETOPENREQ OpenReq;
    OpenReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    OpenReq.Hdr.cbReq = sizeof(OpenReq);
    OpenReq.pSession = pSession;
    RTStrCopy(OpenReq.szNetwork, sizeof(OpenReq.szNetwork), pszNetwork);
    RTStrCopy(OpenReq.szTrunk, sizeof(OpenReq.szTrunk), pszIf);
    OpenReq.enmTrunkType = *pszIf ? kIntNetTrunkType_NetFlt : kIntNetTrunkType_WhateverNone;
    OpenReq.fFlags = fMacSharing ? INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE : 0;
    OpenReq.cbSend = cbSend;
    OpenReq.cbRecv = cbRecv;
    OpenReq.hIf = INTNET_HANDLE_INVALID;

    /*
     * Issue the request.
     */
    RTPrintf("tstIntNet-1: attempting to open/create network \"%s\" with NetFlt trunk \"%s\"...\n",
             OpenReq.szNetwork, OpenReq.szTrunk);
    RTStrmFlush(g_pStdOut);
    rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_OPEN, 0, &OpenReq.Hdr);
    if (RT_SUCCESS(rc))
    {
        RTPrintf("tstIntNet-1: successfully opened/created \"%s\" with NetFlt trunk \"%s\" - hIf=%#x\n",
                 OpenReq.szNetwork, OpenReq.szTrunk, OpenReq.hIf);
        RTStrmFlush(g_pStdOut);

        /*
         * Get the ring-3 address of the shared interface buffer.
         */
        INTNETIFGETBUFFERPTRSREQ GetBufferPtrsReq;
        GetBufferPtrsReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        GetBufferPtrsReq.Hdr.cbReq = sizeof(GetBufferPtrsReq);
        GetBufferPtrsReq.pSession = pSession;
        GetBufferPtrsReq.hIf = OpenReq.hIf;
        GetBufferPtrsReq.pRing3Buf = NULL;
        GetBufferPtrsReq.pRing0Buf = NIL_RTR0PTR;
        rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, 0, &GetBufferPtrsReq.Hdr);
        if (RT_SUCCESS(rc))
        {
            PINTNETBUF pBuf = GetBufferPtrsReq.pRing3Buf;
            RTPrintf("tstIntNet-1: pBuf=%p cbBuf=%d cbSend=%d cbRecv=%d\n",
                     pBuf, pBuf->cbBuf, pBuf->cbSend, pBuf->cbRecv);
            RTStrmFlush(g_pStdOut);
            if (fPromiscuous)
            {
                INTNETIFSETPROMISCUOUSMODEREQ PromiscReq;
                PromiscReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                PromiscReq.Hdr.cbReq    = sizeof(PromiscReq);
                PromiscReq.pSession     = pSession;
                PromiscReq.hIf          = OpenReq.hIf;
                PromiscReq.fPromiscuous = true;
                rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE, 0, &PromiscReq.Hdr);
                if (RT_SUCCESS(rc))
                    RTPrintf("tstIntNet-1: interface in promiscuous mode\n");
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Activate the interface.
                 */
                INTNETIFSETACTIVEREQ ActiveReq;
                ActiveReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                ActiveReq.Hdr.cbReq = sizeof(ActiveReq);
                ActiveReq.pSession = pSession;
                ActiveReq.hIf = OpenReq.hIf;
                ActiveReq.fActive = true;
                rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_SET_ACTIVE, 0, &ActiveReq.Hdr);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Start the stop watch, init the pcap file.
                     */
                    g_StartTS = RTTimeNanoTS();
                    if (pFileRaw)
                        PcapStreamHdr(pFileRaw, g_StartTS);

                    /*
                     * Do the transmit test first and so we can sniff for the response.
                     */
                    if (fXmitTest)
                        doXmitTest(OpenReq.hIf, pSession, pBuf, &SrcMac, pFileRaw, pFileText);

                    if (fPingTest)
                        doPingTest(OpenReq.hIf, pSession, pBuf, &SrcMac, pFileRaw, pFileText);

                    /*
                     * Either enter sniffing mode or do a timeout thing.
                     */
                    if (fSniffer)
                    {
                        doPacketSniffing(OpenReq.hIf, pSession, pBuf, cMillies, pFileRaw, pFileText, &SrcMac);
                        if (   fXmitTest
                            && !g_fDhcpReply)
                        {
                            RTPrintf("tstIntNet-1: Error! The DHCP server didn't reply... (Perhaps you don't have one?)\n");
                            g_cErrors++;
                        }

                        if (   fPingTest
                            && !g_fPingReply)
                        {
                            RTPrintf("tstIntNet-1: Error! No reply for ping request...\n");
                            g_cErrors++;
                        }
                    }
                    else
                        RTThreadSleep(cMillies);
                }
                else
                {
                    RTPrintf("tstIntNet-1: SUPR3CallVMMR0Ex(,VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE,) failed, rc=%Rrc\n", rc);
                    g_cErrors++;
                }
            }
            else
            {
                RTPrintf("tstIntNet-1: SUPR3CallVMMR0Ex(,VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE,) failed, rc=%Rrc\n", rc);
                g_cErrors++;
            }
        }
        else
        {
            RTPrintf("tstIntNet-1: SUPR3CallVMMR0Ex(,VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS,) failed, rc=%Rrc\n", rc);
            g_cErrors++;
        }
    }
    else
    {
        RTPrintf("tstIntNet-1: SUPR3CallVMMR0Ex(,VMMR0_DO_INTNET_OPEN,) failed, rc=%Rrc\n", rc);
        g_cErrors++;
    }

    SUPR3Term(false /*fForced*/);

    /* close open files  */
    if (pFileRaw)
        RTStrmClose(pFileRaw);
    if (pFileText && pFileText != g_pStdErr && pFileText != g_pStdOut)
        RTStrmClose(pFileText);

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstIntNet-1: SUCCESS\n");
    else
        RTPrintf("tstIntNet-1: FAILURE - %d errors\n", g_cErrors);

    return !!g_cErrors;
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

