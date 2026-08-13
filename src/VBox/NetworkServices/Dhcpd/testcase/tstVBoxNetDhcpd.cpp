/* $Id: tstVBoxNetDhcpd.cpp 115037 2026-08-13 20:27:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxNetDHCP in-process testcase.
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

#include <iprt/initterm.h>
#include <iprt/test.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/net.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/uuid.h>

#include <VBox/intnet.h>

#include "../Config.h"
#include "../../NetLib/IntNetIf.h"

extern "C" int  VBoxNetDhcpdTestStart(int argc, char **argv, void **ppvHandle);
extern "C" int  VBoxNetDhcpdTestStop(void *pvHandle);
extern "C" bool VBoxNetDhcpdTestIsRunning(void *pvHandle);
extern "C" int  VBoxIntNetSwitchTestStart(void **ppvHandle);
extern "C" int  VBoxIntNetSwitchTestStop(void *pvHandle);

static RTTEST g_hTest = NIL_RTTEST;

#define TST_MAX_FRAME        2048
#define TST_MAX_RX_FRAMES      64
#define TST_DHCP_TIMEOUT_MS  3000
#define TST_ANY_XID          UINT32_MAX

#define DHCP4_DISCOVER 1
#define DHCP4_OFFER    2
#define DHCP4_REQUEST  3
#define DHCP4_ACK      5

#define TST_CHECK(a_Expr) \
    do { if (!(a_Expr)) RTTestFailed(g_hTest, "%s:%u: %s", __FILE__, __LINE__, #a_Expr); } while (0)

#define TST_CHECK_RC_OK(a_rc) \
    do { int rc__ = (a_rc); if (RT_FAILURE(rc__)) RTTestFailed(g_hTest, "%s:%u: %s -> %Rrc", __FILE__, __LINE__, #a_rc, rc__); } while (0)

static uint16_t tstH2N16(uint16_t u) { return RT_H2N_U16(u); }
static uint32_t tstH2N32(uint32_t u) { return RT_H2N_U32(u); }

static uint32_t tstIPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return tstH2N32(((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d);
}

static int tstMakeUuidName(const char *pszPrefix, char *pszOut, size_t cbOut)
{
    RTUUID Uuid;
    char szUuid[RTUUID_STR_LENGTH];

    int rc = RTUuidCreate(&Uuid);
    if (RT_FAILURE(rc))
        return rc;

    RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));

    ssize_t cch = RTStrPrintf2(pszOut, cbOut, "%s-%s", pszPrefix, szUuid);
    return cch > 0 && (size_t)cch < cbOut ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
}

static int tstMakeTempPath(const char *pszPrefix, const char *pszSuffix, char *pszOut, size_t cbOut)
{
    char szTmp[RTPATH_MAX];
    RTUUID Uuid;
    char szUuid[RTUUID_STR_LENGTH];

    int rc = RTPathTemp(szTmp, sizeof(szTmp));
    if (RT_FAILURE(rc))
        return rc;

    rc = RTUuidCreate(&Uuid);
    if (RT_FAILURE(rc))
        return rc;

    RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));

    ssize_t cch = RTStrPrintf2(pszOut, cbOut, "%s%c%s-%s%s",
                               szTmp, RTPATH_DELIMITER, pszPrefix, szUuid, pszSuffix);
    return cch > 0 && (size_t)cch < cbOut ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
}

static int tstWriteAll(const char *pszFilename, const void *pvBuf, size_t cbBuf)
{
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile,
                        pszFilename,
                        RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTFileWrite(hFile, pvBuf, cbBuf, NULL);

    int rcClose = RTFileClose(hFile);
    if (RT_SUCCESS(rc) && RT_FAILURE(rcClose))
        rc = rcClose;

    return rc;
}

static int tstWriteConfigFile(const char *pszNetwork,
                              const char *pszServerIp,
                              const char *pszMask,
                              const char *pszLower,
                              const char *pszUpper,
                              char *pszCfgFile,
                              size_t cbCfgFile)
{
    char szLeases[RTPATH_MAX];
    char szXml[8192];

    int rc = tstMakeTempPath("VBoxNetDhcpd", ".xml", pszCfgFile, cbCfgFile);
    if (RT_FAILURE(rc))
        return rc;

    rc = tstMakeTempPath("VBoxNetDhcpd", ".leases", szLeases, sizeof(szLeases));
    if (RT_FAILURE(rc))
        return rc;

    ssize_t cch = RTStrPrintf2(szXml, sizeof(szXml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<DHCPServer\n"
        "  networkName=\"%s\"\n"
        "  trunkName=\"\"\n"
        "  trunkType=\"none\"\n"
        "  IPAddress=\"%s\"\n"
        "  networkMask=\"%s\"\n"
        "  lowerIP=\"%s\"\n"
        "  upperIP=\"%s\"\n"
        "  leasesFilename=\"%s\">\n"
        "  <Options secDefaultLeaseTime=\"600\" secMinLeaseTime=\"60\" secMaxLeaseTime=\"7200\">\n"
        "    <Option name=\"1\" value=\"%s\"/>\n"
        "    <Option name=\"3\" value=\"%s\"/>\n"
        "    <Option name=\"6\" value=\"%s\"/>\n"
        "    <ForcedOption name=\"1\"/>\n"
        "  </Options>\n"
        "</DHCPServer>\n",
        pszNetwork,
        pszServerIp,
        pszMask,
        pszLower,
        pszUpper,
        szLeases,
        pszMask,
        pszServerIp,
        pszServerIp);

    if (cch <= 0 || (size_t)cch >= sizeof(szXml))
        return VERR_BUFFER_OVERFLOW;

    return tstWriteAll(pszCfgFile, szXml, (size_t)cch);
}

static bool tstConfigCreateFromFile(const char *pszCfgFile)
{
    char *argv[3];
    argv[0] = (char *)"tstVBoxNetDhcpd-config";
    argv[1] = (char *)"--config";
    argv[2] = (char *)pszCfgFile;

    Config *pConfig = Config::create(3, argv);
    if (pConfig != NULL)
    {
        delete pConfig;
        return true;
    }

    return false;
}

static void tstConfigValidation(void)
{
    RTTestSub(g_hTest, "configuration validation");

    char szNetwork[128];
    char szCfg[RTPATH_MAX];

    TST_CHECK_RC_OK(tstMakeUuidName("VBoxNetDhcpdCfg", szNetwork, sizeof(szNetwork)));
    TST_CHECK_RC_OK(tstWriteConfigFile(szNetwork, "10.37.0.1", "255.255.255.0", "10.37.0.10", "10.37.0.12",
                                       szCfg, sizeof(szCfg)));
    TST_CHECK(tstConfigCreateFromFile(szCfg));

    TST_CHECK_RC_OK(tstMakeUuidName("VBoxNetDhcpdBadOutside", szNetwork, sizeof(szNetwork)));
    TST_CHECK_RC_OK(tstWriteConfigFile(szNetwork, "10.37.0.1", "255.255.255.0", "10.38.0.10", "10.38.0.12",
                                       szCfg, sizeof(szCfg)));
    TST_CHECK(!tstConfigCreateFromFile(szCfg));

    TST_CHECK_RC_OK(tstMakeUuidName("VBoxNetDhcpdBadReversed", szNetwork, sizeof(szNetwork)));
    TST_CHECK_RC_OK(tstWriteConfigFile(szNetwork, "10.37.0.1", "255.255.255.0", "10.37.0.12", "10.37.0.10",
                                       szCfg, sizeof(szCfg)));
    TST_CHECK(!tstConfigCreateFromFile(szCfg));

    TST_CHECK_RC_OK(tstMakeUuidName("VBoxNetDhcpdBadMask", szNetwork, sizeof(szNetwork)));
    TST_CHECK_RC_OK(tstWriteConfigFile(szNetwork, "10.37.0.1", "255.255.0.255", "10.37.0.10", "10.37.0.12",
                                       szCfg, sizeof(szCfg)));
    TST_CHECK(!tstConfigCreateFromFile(szCfg));

    TST_CHECK_RC_OK(tstMakeUuidName("VBoxNetDhcpdIPv6Unsupported", szNetwork, sizeof(szNetwork)));
    TST_CHECK_RC_OK(tstWriteConfigFile(szNetwork, "fd00:1::1", "ffff:ffff:ffff:ffff::",
                                       "fd00:1::100", "fd00:1::1ff", szCfg, sizeof(szCfg)));
    TST_CHECK(!tstConfigCreateFromFile(szCfg));

    {
        char *argv[1];
        argv[0] = (char *)"tstVBoxNetDhcpd-no-config";
        Config *pConfig = Config::create(1, argv);
        TST_CHECK(pConfig == NULL);
        if (pConfig)
            delete pConfig;
    }
}

typedef struct TSTPKT
{
    uint32_t cb;
    uint8_t  ab[TST_MAX_FRAME];
} TSTPKT;
typedef TSTPKT *PTSTPKT;
typedef const TSTPKT *PCTSTPKT;

typedef struct TSTRXFRAME
{
    uint32_t cb;
    uint8_t  ab[TST_MAX_FRAME];
} TSTRXFRAME;
typedef TSTRXFRAME *PTSTRXFRAME;

typedef struct TSTCLIENT
{
    INTNETIFCTX hIf;
    RTTHREAD    hThread;
    /** Receive thread status, VINF_SUCCESS while it is running normally. */
    int32_t volatile rcThread;
    RTCRITSECT  CritSect;
    RTSEMEVENT  hEvtRx;
    uint32_t    iRead;
    uint32_t    iWrite;
    uint32_t    cFrames;
    TSTRXFRAME  aFrames[TST_MAX_RX_FRAMES];
} TSTCLIENT;
typedef TSTCLIENT *PTSTCLIENT;

#pragma pack(1)
typedef struct TSTETHHDR
{
    uint8_t  abDst[6];
    uint8_t  abSrc[6];
    uint16_t uType;
} TSTETHHDR;

typedef struct TSTIP4HDR
{
    uint8_t  uVerIhl;
    uint8_t  uTos;
    uint16_t uLen;
    uint16_t uId;
    uint16_t uFragOff;
    uint8_t  uTtl;
    uint8_t  uProto;
    uint16_t uChecksum;
    uint32_t uSrc;
    uint32_t uDst;
} TSTIP4HDR;

typedef struct TSTIP6HDR
{
    uint32_t uVerTcFl;
    uint16_t uPayloadLen;
    uint8_t  uNextHeader;
    uint8_t  uHopLimit;
    uint8_t  abSrc[16];
    uint8_t  abDst[16];
} TSTIP6HDR;

typedef struct TSTUDPHDR
{
    uint16_t uSrcPort;
    uint16_t uDstPort;
    uint16_t uLen;
    uint16_t uChecksum;
} TSTUDPHDR;

typedef struct TSTDHCP4HDR
{
    uint8_t  uOp;
    uint8_t  uHType;
    uint8_t  uHLen;
    uint8_t  uHops;
    uint32_t uXid;
    uint16_t uSecs;
    uint16_t uFlags;
    uint32_t uCiAddr;
    uint32_t uYiAddr;
    uint32_t uSiAddr;
    uint32_t uGiAddr;
    uint8_t  abChAddr[16];
    uint8_t  abSName[64];
    uint8_t  abFile[128];
    uint32_t uCookie;
} TSTDHCP4HDR;
#pragma pack()

typedef struct TSTDHCP4REPLY
{
    uint8_t  uMsgType;
    uint32_t uYiAddrBe;
    uint32_t uServerIdBe;
    uint32_t uXid;
} TSTDHCP4REPLY;
typedef TSTDHCP4REPLY *PTSTDHCP4REPLY;

static uint32_t tstChecksumAdd(uint32_t uSum, const void *pv, size_t cb)
{
    const uint8_t *pb = (const uint8_t *)pv;

    while (cb > 1)
    {
        uSum += ((uint16_t)pb[0] << 8) | pb[1];
        pb += 2;
        cb -= 2;
    }

    if (cb)
        uSum += ((uint16_t)pb[0] << 8);

    return uSum;
}

static uint16_t tstChecksumFinish(uint32_t uSum)
{
    while (uSum >> 16)
        uSum = (uSum & 0xffff) + (uSum >> 16);
    return (uint16_t)~uSum;
}

static int tstPktAppend(PTSTPKT pPkt, const void *pv, uint32_t cb)
{
    if (pPkt->cb + cb > sizeof(pPkt->ab))
        return VERR_BUFFER_OVERFLOW;

    memcpy(&pPkt->ab[pPkt->cb], pv, cb);
    pPkt->cb += cb;
    return VINF_SUCCESS;
}

static int tstDhcp4AppendOpt(PTSTPKT pPkt, uint8_t uOpt, const void *pv, uint8_t cb)
{
    uint8_t abHdr[2];
    abHdr[0] = uOpt;
    abHdr[1] = cb;

    int rc = tstPktAppend(pPkt, abHdr, sizeof(abHdr));
    if (RT_SUCCESS(rc))
        rc = tstPktAppend(pPkt, pv, cb);
    return rc;
}

static int tstDhcp4AppendOptU32(PTSTPKT pPkt, uint8_t uOpt, uint32_t uValueBe)
{
    return tstDhcp4AppendOpt(pPkt, uOpt, &uValueBe, sizeof(uValueBe));
}

static int tstBuildDhcp4(PTSTPKT pDhcp,
                         uint8_t uMsgType,
                         uint32_t uXid,
                         PCRTMAC pMac,
                         uint32_t uRequestedIpBe,
                         uint32_t uServerIdBe,
                         bool fBadCookie)
{
    RT_ZERO(*pDhcp);
    pDhcp->cb = sizeof(TSTDHCP4HDR);

    TSTDHCP4HDR *pHdr = (TSTDHCP4HDR *)&pDhcp->ab[0];

    pHdr->uOp     = 1; /* BOOTREQUEST */
    pHdr->uHType  = 1; /* Ethernet */
    pHdr->uHLen   = 6;
    pHdr->uXid    = tstH2N32(uXid);
    pHdr->uFlags  = tstH2N16(0x8000);
    pHdr->uCookie = fBadCookie ? tstH2N32(0xdeadbeef) : tstH2N32(0x63825363);
    memcpy(pHdr->abChAddr, pMac, sizeof(*pMac));

    uint8_t b = uMsgType;
    int rc = tstDhcp4AppendOpt(pDhcp, 53, &b, sizeof(b));
    if (RT_FAILURE(rc))
        return rc;

    uint8_t abClientId[7];
    abClientId[0] = 1;
    memcpy(&abClientId[1], pMac, sizeof(*pMac));
    rc = tstDhcp4AppendOpt(pDhcp, 61, abClientId, sizeof(abClientId));
    if (RT_FAILURE(rc))
        return rc;

    {
        uint8_t abPrl[] = { 1, 3, 6, 51, 54 };
        rc = tstDhcp4AppendOpt(pDhcp, 55, abPrl, sizeof(abPrl));
        if (RT_FAILURE(rc))
            return rc;
    }

    if (uRequestedIpBe)
    {
        rc = tstDhcp4AppendOptU32(pDhcp, 50, uRequestedIpBe);
        if (RT_FAILURE(rc))
            return rc;
    }

    if (uServerIdBe)
    {
        rc = tstDhcp4AppendOptU32(pDhcp, 54, uServerIdBe);
        if (RT_FAILURE(rc))
            return rc;
    }

    b = 255;
    rc = tstPktAppend(pDhcp, &b, sizeof(b));
    if (RT_FAILURE(rc))
        return rc;

    while (pDhcp->cb < 300)
    {
        b = 0;
        rc = tstPktAppend(pDhcp, &b, sizeof(b));
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}

static int tstBuildUdp4Frame(PTSTPKT pFrame,
                             PCRTMAC pSrcMac,
                             uint32_t uSrcIpBe,
                             uint32_t uDstIpBe,
                             uint16_t uSrcPort,
                             uint16_t uDstPort,
                             PCTSTPKT pPayload)
{
    static const uint8_t s_abBcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    RT_ZERO(*pFrame);

    TSTETHHDR Eth;
    memcpy(Eth.abDst, s_abBcast, sizeof(Eth.abDst));
    memcpy(Eth.abSrc, pSrcMac, sizeof(Eth.abSrc));
    Eth.uType = tstH2N16(0x0800);

    int rc = tstPktAppend(pFrame, &Eth, sizeof(Eth));
    if (RT_FAILURE(rc))
        return rc;

    TSTIP4HDR Ip;
    RT_ZERO(Ip);
    Ip.uVerIhl   = 0x45;
    Ip.uLen      = tstH2N16((uint16_t)(sizeof(TSTIP4HDR) + sizeof(TSTUDPHDR) + pPayload->cb));
    Ip.uId       = tstH2N16((uint16_t)RTRandU32());
    Ip.uTtl      = 64;
    Ip.uProto    = 17;
    Ip.uSrc      = uSrcIpBe;
    Ip.uDst      = uDstIpBe;
    Ip.uChecksum = tstH2N16(tstChecksumFinish(tstChecksumAdd(0, &Ip, sizeof(Ip))));

    rc = tstPktAppend(pFrame, &Ip, sizeof(Ip));
    if (RT_FAILURE(rc))
        return rc;

    TSTUDPHDR Udp;
    RT_ZERO(Udp);
    Udp.uSrcPort  = tstH2N16(uSrcPort);
    Udp.uDstPort  = tstH2N16(uDstPort);
    Udp.uLen      = tstH2N16((uint16_t)(sizeof(TSTUDPHDR) + pPayload->cb));
    Udp.uChecksum = 0; /* legal for IPv4 */

    rc = tstPktAppend(pFrame, &Udp, sizeof(Udp));
    if (RT_FAILURE(rc))
        return rc;

    return tstPktAppend(pFrame, pPayload->ab, pPayload->cb);
}

static int tstBuildDhcp6SolicitFrame(PTSTPKT pFrame, PCRTMAC pSrcMac)
{
    static const uint8_t s_abDstMac[6] = { 0x33, 0x33, 0x00, 0x01, 0x00, 0x02 };
    static const uint8_t s_abSrcIp[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0,
                                           0x02, 0x00, 0x27, 0xff, 0xfe, 0x12, 0x34, 0x56 };
    static const uint8_t s_abDstIp[16] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0x01, 0x02 };

    RT_ZERO(*pFrame);

    TSTETHHDR Eth;
    memcpy(Eth.abDst, s_abDstMac, sizeof(Eth.abDst));
    memcpy(Eth.abSrc, pSrcMac, sizeof(Eth.abSrc));
    Eth.uType = tstH2N16(0x86dd);

    int rc = tstPktAppend(pFrame, &Eth, sizeof(Eth));
    if (RT_FAILURE(rc))
        return rc;

    TSTIP6HDR Ip6;
    RT_ZERO(Ip6);
    Ip6.uVerTcFl    = tstH2N32(6U << 28);
    Ip6.uPayloadLen = tstH2N16((uint16_t)(sizeof(TSTUDPHDR) + 4));
    Ip6.uNextHeader = 17;
    Ip6.uHopLimit   = 1;
    memcpy(Ip6.abSrc, s_abSrcIp, sizeof(Ip6.abSrc));
    memcpy(Ip6.abDst, s_abDstIp, sizeof(Ip6.abDst));

    rc = tstPktAppend(pFrame, &Ip6, sizeof(Ip6));
    if (RT_FAILURE(rc))
        return rc;

    TSTUDPHDR Udp;
    RT_ZERO(Udp);
    Udp.uSrcPort = tstH2N16(546);
    Udp.uDstPort = tstH2N16(547);
    Udp.uLen     = tstH2N16((uint16_t)(sizeof(TSTUDPHDR) + 4));

    uint8_t abDhcp6[4];
    abDhcp6[0] = 1;    /* SOLICIT */
    abDhcp6[1] = 0x12;
    abDhcp6[2] = 0x34;
    abDhcp6[3] = 0x56;

    uint32_t uSum = 0;
    uint32_t uLenBe = tstH2N32(sizeof(TSTUDPHDR) + sizeof(abDhcp6));
    uint8_t  abNextHdr[4] = { 0, 0, 0, 17 };

    uSum = tstChecksumAdd(uSum, s_abSrcIp, sizeof(s_abSrcIp));
    uSum = tstChecksumAdd(uSum, s_abDstIp, sizeof(s_abDstIp));
    uSum = tstChecksumAdd(uSum, &uLenBe, sizeof(uLenBe));
    uSum = tstChecksumAdd(uSum, abNextHdr, sizeof(abNextHdr));
    uSum = tstChecksumAdd(uSum, &Udp, sizeof(Udp));
    uSum = tstChecksumAdd(uSum, abDhcp6, sizeof(abDhcp6));

    uint16_t uCsum = tstChecksumFinish(uSum);
    if (uCsum == 0)
        uCsum = 0xffff;
    Udp.uChecksum = tstH2N16(uCsum);

    rc = tstPktAppend(pFrame, &Udp, sizeof(Udp));
    if (RT_FAILURE(rc))
        return rc;

    return tstPktAppend(pFrame, abDhcp6, sizeof(abDhcp6));
}

static DECLCALLBACK(void) tstClientRx(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    PTSTCLIENT pThis = (PTSTCLIENT)pvUser;

    if (cbFrame > TST_MAX_FRAME)
        return;

    RTCritSectEnter(&pThis->CritSect);

    if (pThis->cFrames == TST_MAX_RX_FRAMES)
    {
        pThis->iRead = (pThis->iRead + 1) % TST_MAX_RX_FRAMES;
        pThis->cFrames--;
    }

    PTSTRXFRAME pDst = &pThis->aFrames[pThis->iWrite];
    memcpy(pDst->ab, pvFrame, cbFrame);
    pDst->cb = cbFrame;

    pThis->iWrite = (pThis->iWrite + 1) % TST_MAX_RX_FRAMES;
    pThis->cFrames++;

    RTCritSectLeave(&pThis->CritSect);
    RTSemEventSignal(pThis->hEvtRx);
}

static DECLCALLBACK(int) tstClientThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PTSTCLIENT pThis = (PTSTCLIENT)pvUser;
    int const rc = IntNetR3IfPumpPkts(pThis->hIf, tstClientRx, pThis, NULL, NULL);
    ASMAtomicWriteS32(&pThis->rcThread, rc);
    RTSemEventSignal(pThis->hEvtRx);
    return rc;
}

static void tstClientDestroy(PTSTCLIENT pThis);

static int tstClientInit(PTSTCLIENT pThis, const char *pszNetwork)
{
    RT_ZERO(*pThis);
    pThis->hThread = NIL_RTTHREAD;
    pThis->hEvtRx  = NIL_RTSEMEVENT;
    pThis->rcThread = VINF_SUCCESS;

    int rc = RTCritSectInit(&pThis->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTSemEventCreate(&pThis->hEvtRx);
    if (RT_FAILURE(rc))
    {
        tstClientDestroy(pThis);
        return rc;
    }

    rc = IntNetR3IfCreateEx(&pThis->hIf, pszNetwork, kIntNetTrunkType_WhateverNone, "", _256K, _256K, 0);
    if (RT_FAILURE(rc))
    {
        tstClientDestroy(pThis);
        return rc;
    }

    rc = IntNetR3IfSetActive(pThis->hIf, true);
    if (RT_FAILURE(rc))
    {
        tstClientDestroy(pThis);
        return rc;
    }

    rc = RTThreadCreate(&pThis->hThread,
                        tstClientThread,
                        pThis,
                        0,
                        RTTHREADTYPE_IO,
                        RTTHREADFLAGS_WAITABLE,
                        "DhcpCli");
    if (RT_FAILURE(rc))
        tstClientDestroy(pThis);
    return rc;
}

static void tstClientDestroy(PTSTCLIENT pThis)
{
    if (pThis->hIf != NULL)
    {
        int const rc = IntNetR3IfWaitAbort(pThis->hIf);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Aborting the test client receive wait failed: %Rrc", rc);
    }

    if (pThis->hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int const rc = RTThreadWait(pThis->hThread, 30000, &rcThread);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "Joining the test client receive thread failed: %Rrc", rc);
            return;
        }
        pThis->hThread = NIL_RTTHREAD;
        if (rcThread != VERR_SEM_DESTROYED)
            RTTestFailed(g_hTest, "Test client receive thread returned %Rrc, expected %Rrc",
                         rcThread, VERR_SEM_DESTROYED);
    }

    if (pThis->hIf != NULL)
    {
        IntNetR3IfSetActive(pThis->hIf, false);
        IntNetR3IfDestroy(pThis->hIf);
        pThis->hIf = NULL;
    }

    if (pThis->hEvtRx != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtRx);
        pThis->hEvtRx = NIL_RTSEMEVENT;
    }

    if (pThis->CritSect.u32Magic == RTCRITSECT_MAGIC)
        RTCritSectDelete(&pThis->CritSect);
}

static bool tstClientPopFrame(PTSTCLIENT pThis, PTSTRXFRAME pFrame)
{
    bool fHave = false;

    RTCritSectEnter(&pThis->CritSect);

    if (pThis->cFrames > 0)
    {
        *pFrame = pThis->aFrames[pThis->iRead];
        pThis->iRead = (pThis->iRead + 1) % TST_MAX_RX_FRAMES;
        pThis->cFrames--;
        fHave = true;
    }

    RTCritSectLeave(&pThis->CritSect);
    return fHave;
}

static uint32_t tstClientQueuedFrameCount(PTSTCLIENT pThis)
{
    RTCritSectEnter(&pThis->CritSect);
    uint32_t const cFrames = pThis->cFrames;
    RTCritSectLeave(&pThis->CritSect);
    return cFrames;
}

static int tstClientSendFrame(PTSTCLIENT pThis, PCTSTPKT pFrame)
{
    INTNETFRAME Frame;
    int rc = IntNetR3IfQueryOutputFrame(pThis->hIf, pFrame->cb, &Frame);
    if (RT_FAILURE(rc))
        return rc;

    memcpy(Frame.pvFrame, pFrame->ab, pFrame->cb);
    return IntNetR3IfOutputFrameCommit(pThis->hIf, &Frame);
}

static bool tstParseDhcp4Reply(const uint8_t *pbFrame, uint32_t cbFrame, uint32_t uWantXid, PTSTDHCP4REPLY pReply)
{
    if (cbFrame < sizeof(TSTETHHDR) + sizeof(TSTIP4HDR) + sizeof(TSTUDPHDR) + sizeof(TSTDHCP4HDR))
        return false;

    const TSTETHHDR *pEth = (const TSTETHHDR *)pbFrame;
    if (RT_N2H_U16(pEth->uType) != 0x0800)
        return false;

    const TSTIP4HDR *pIp = (const TSTIP4HDR *)(pbFrame + sizeof(TSTETHHDR));
    if ((pIp->uVerIhl >> 4) != 4 || pIp->uProto != 17)
        return false;

    uint32_t cbIp = (pIp->uVerIhl & 0x0f) * 4;
    if (cbIp < sizeof(TSTIP4HDR))
        return false;

    if (cbFrame < sizeof(TSTETHHDR) + cbIp + sizeof(TSTUDPHDR) + sizeof(TSTDHCP4HDR))
        return false;

    const TSTUDPHDR *pUdp = (const TSTUDPHDR *)(pbFrame + sizeof(TSTETHHDR) + cbIp);
    if (RT_N2H_U16(pUdp->uSrcPort) != 67 || RT_N2H_U16(pUdp->uDstPort) != 68)
        return false;

    const TSTDHCP4HDR *pDhcp = (const TSTDHCP4HDR *)(pbFrame + sizeof(TSTETHHDR) + cbIp + sizeof(TSTUDPHDR));

    if (pDhcp->uOp != 2)
        return false;

    uint32_t uXid = RT_N2H_U32(pDhcp->uXid);
    if (uWantXid != TST_ANY_XID && uXid != uWantXid)
        return false;

    if (RT_N2H_U32(pDhcp->uCookie) != 0x63825363)
        return false;

    uint8_t uMsgType = 0;
    uint32_t uServerIdBe = 0;

    const uint8_t *pbOpt = (const uint8_t *)(pDhcp + 1);
    const uint8_t *pbEnd = pbFrame + cbFrame;

    while (pbOpt < pbEnd)
    {
        uint8_t uOpt = *pbOpt++;

        if (uOpt == 255)
            break;
        if (uOpt == 0)
            continue;

        if (pbOpt >= pbEnd)
            return false;

        uint8_t cbOpt = *pbOpt++;
        if (pbOpt + cbOpt > pbEnd)
            return false;

        if (uOpt == 53 && cbOpt == 1)
            uMsgType = pbOpt[0];
        else if (uOpt == 54 && cbOpt == 4)
            memcpy(&uServerIdBe, pbOpt, sizeof(uServerIdBe));

        pbOpt += cbOpt;
    }

    if (uMsgType == 0)
        return false;

    pReply->uMsgType    = uMsgType;
    pReply->uYiAddrBe   = pDhcp->uYiAddr;
    pReply->uServerIdBe = uServerIdBe;
    pReply->uXid        = uXid;
    return true;
}

static bool tstFrameIsDhcp6Reply(const uint8_t *pbFrame, uint32_t cbFrame)
{
    if (cbFrame < sizeof(TSTETHHDR) + sizeof(TSTIP6HDR) + sizeof(TSTUDPHDR) + 4)
        return false;

    const TSTETHHDR *pEth = (const TSTETHHDR *)pbFrame;
    if (RT_N2H_U16(pEth->uType) != 0x86dd)
        return false;

    const TSTIP6HDR *pIp6 = (const TSTIP6HDR *)(pbFrame + sizeof(TSTETHHDR));
    if ((RT_N2H_U32(pIp6->uVerTcFl) >> 28) != 6 || pIp6->uNextHeader != 17)
        return false;

    const TSTUDPHDR *pUdp = (const TSTUDPHDR *)(pbFrame + sizeof(TSTETHHDR) + sizeof(TSTIP6HDR));
    return RT_N2H_U16(pUdp->uSrcPort) == 547 && RT_N2H_U16(pUdp->uDstPort) == 546;
}

static bool tstClientWaitForDhcp4(PTSTCLIENT pClient,
                                  uint32_t uXid,
                                  uint8_t uMsgType,
                                  PTSTDHCP4REPLY pReply,
                                  uint32_t cMs)
{
    uint64_t uDeadline = RTTimeMilliTS() + cMs;

    while (RTTimeMilliTS() < uDeadline)
    {
        TSTRXFRAME Frame;
        if (tstClientPopFrame(pClient, &Frame))
        {
            TSTDHCP4REPLY Reply;
            if (tstParseDhcp4Reply(Frame.ab, Frame.cb, uXid, &Reply) && Reply.uMsgType == uMsgType)
            {
                if (pReply)
                    *pReply = Reply;
                return true;
            }
        }
        else
            RTSemEventWait(pClient->hEvtRx, 50);
    }

    return false;
}

static bool tstClientWaitForDhcp6Reply(PTSTCLIENT pClient, uint32_t cMs)
{
    uint64_t uDeadline = RTTimeMilliTS() + cMs;

    while (RTTimeMilliTS() < uDeadline)
    {
        TSTRXFRAME Frame;
        if (tstClientPopFrame(pClient, &Frame))
        {
            if (tstFrameIsDhcp6Reply(Frame.ab, Frame.cb))
                return true;
        }
        else
            RTSemEventWait(pClient->hEvtRx, 50);
    }

    return false;
}

static int tstDhcp4DiscoverOnce(PTSTCLIENT pClient,
                                PCRTMAC pMac,
                                uint32_t uXid,
                                PTSTDHCP4REPLY pOffer,
                                uint32_t cMs)
{
    TSTPKT Dhcp;
    TSTPKT Frame;

    int rc = tstBuildDhcp4(&Dhcp, DHCP4_DISCOVER, uXid, pMac, 0, 0, false);
    if (RT_FAILURE(rc))
        return rc;

    rc = tstBuildUdp4Frame(&Frame, pMac, tstIPv4(0,0,0,0), tstIPv4(255,255,255,255), 68, 67, &Dhcp);
    if (RT_FAILURE(rc))
        return rc;

    rc = tstClientSendFrame(pClient, &Frame);
    if (RT_FAILURE(rc))
        return rc;

    return tstClientWaitForDhcp4(pClient, uXid, DHCP4_OFFER, pOffer, cMs) ? VINF_SUCCESS : VERR_TIMEOUT;
}

static bool tstDhcp4Discover(PTSTCLIENT pClient, PCRTMAC pMac, PTSTDHCP4REPLY pOffer)
{
    /* Retransmissions belong to one DHCP transaction, so delayed replies from
       an earlier attempt must remain acceptable on a loaded testbox. */
    uint32_t const uXid = RTRandU32();
    for (unsigned i = 0; i < 8; i++)
    {
        int const rc = tstDhcp4DiscoverOnce(pClient, pMac, uXid, pOffer, 600);
        if (RT_SUCCESS(rc))
            return true;
        if (rc != VERR_TIMEOUT)
        {
            RTTestFailed(g_hTest, "Sending DHCPDISCOVER for %RTmac (xid %#RX32) failed: %Rrc", pMac, uXid, rc);
            return false;
        }
        RTThreadSleep(100);
    }
    RTTestFailed(g_hTest, "No DHCPOFFER for %RTmac (xid %#RX32) after 8 attempts", pMac, uXid);
    RTTestFailureDetails(g_hTest, "client receive status: %Rrc; queued frames: %RU32\n",
                         ASMAtomicReadS32(&pClient->rcThread), tstClientQueuedFrameCount(pClient));
    return false;
}

static bool tstDhcp4Request(PTSTCLIENT pClient,
                            PCRTMAC pMac,
                            const TSTDHCP4REPLY *pOffer,
                            PTSTDHCP4REPLY pAck)
{
    /* A DHCPREQUEST selecting an offer continues the DISCOVER transaction. */
    uint32_t const uXid = pOffer->uXid;
    TSTPKT Dhcp;
    TSTPKT Frame;

    int rc = tstBuildDhcp4(&Dhcp,
                           DHCP4_REQUEST,
                           uXid,
                           pMac,
                           pOffer->uYiAddrBe,
                           pOffer->uServerIdBe,
                           false);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Building DHCPREQUEST for %RTmac failed: %Rrc", pMac, rc);
        return false;
    }

    rc = tstBuildUdp4Frame(&Frame, pMac, tstIPv4(0,0,0,0), tstIPv4(255,255,255,255), 68, 67, &Dhcp);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Building DHCPREQUEST frame for %RTmac failed: %Rrc", pMac, rc);
        return false;
    }

    rc = tstClientSendFrame(pClient, &Frame);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Sending DHCPREQUEST for %RTmac (xid %#RX32) failed: %Rrc", pMac, uXid, rc);
        return false;
    }

    if (tstClientWaitForDhcp4(pClient, uXid, DHCP4_ACK, pAck, TST_DHCP_TIMEOUT_MS))
        return true;

    RTTestFailed(g_hTest, "No DHCPACK for %RTmac (xid %#RX32, requested %RTnaipv4)",
                 pMac, uXid, pOffer->uYiAddrBe);
    RTTestFailureDetails(g_hTest, "client receive status: %Rrc; queued frames: %RU32\n",
                         ASMAtomicReadS32(&pClient->rcThread), tstClientQueuedFrameCount(pClient));
    return false;
}

static bool tstIPv4InPool(uint32_t uIpBe, uint8_t uFirst, uint8_t uLast)
{
    uint32_t uIp = RT_N2H_U32(uIpBe);
    uint32_t uLo = RT_N2H_U32(tstIPv4(10,37,0,uFirst));
    uint32_t uHi = RT_N2H_U32(tstIPv4(10,37,0,uLast));

    return uIp >= uLo && uIp <= uHi;
}

static bool tstSendBadCookieNoReply(PTSTCLIENT pClient)
{
    static const RTMAC s_Mac = { { 0x08, 0x00, 0x27, 0xde, 0xad, 0x01 } };

    uint32_t uXid = RTRandU32();
    TSTPKT Dhcp;
    TSTPKT Frame;

    int rc = tstBuildDhcp4(&Dhcp, DHCP4_DISCOVER, uXid, &s_Mac, 0, 0, true);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Building bad-cookie DHCPDISCOVER failed: %Rrc", rc);
        return false;
    }

    rc = tstBuildUdp4Frame(&Frame, &s_Mac, tstIPv4(0,0,0,0), tstIPv4(255,255,255,255), 68, 67, &Dhcp);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Building bad-cookie DHCPDISCOVER frame failed: %Rrc", rc);
        return false;
    }

    rc = tstClientSendFrame(pClient, &Frame);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Sending bad-cookie DHCPDISCOVER failed: %Rrc", rc);
        return false;
    }

    if (tstClientWaitForDhcp4(pClient, uXid, DHCP4_OFFER, NULL, 750))
    {
        RTTestFailed(g_hTest, "Server replied to bad-cookie DHCPDISCOVER (xid %#RX32)", uXid);
        return false;
    }

    int const rcThread = ASMAtomicReadS32(&pClient->rcThread);
    if (RT_FAILURE(rcThread))
    {
        RTTestFailed(g_hTest, "Client receive thread failed during bad-cookie test: %Rrc", rcThread);
        return false;
    }
    return true;
}

static bool tstSendDhcp6SolicitNoReply(PTSTCLIENT pClient)
{
    static const RTMAC s_Mac = { { 0x08, 0x00, 0x27, 0x66, 0x66, 0x66 } };

    TSTPKT Frame;
    int rc = tstBuildDhcp6SolicitFrame(&Frame, &s_Mac);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Building DHCPv6 SOLICIT frame failed: %Rrc", rc);
        return false;
    }

    rc = tstClientSendFrame(pClient, &Frame);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Sending DHCPv6 SOLICIT failed: %Rrc", rc);
        return false;
    }

    if (tstClientWaitForDhcp6Reply(pClient, 1000))
    {
        RTTestFailed(g_hTest, "IPv4-only server unexpectedly replied to DHCPv6 SOLICIT");
        return false;
    }

    int const rcThread = ASMAtomicReadS32(&pClient->rcThread);
    if (RT_FAILURE(rcThread))
    {
        RTTestFailed(g_hTest, "Client receive thread failed during DHCPv6 test: %Rrc", rcThread);
        return false;
    }
    return true;
}

static bool tstLeaseClient(PTSTCLIENT pClient,
                           PCRTMAC pMac,
                           PTSTDHCP4REPLY pAck)
{
    TSTDHCP4REPLY Offer;
    if (!tstDhcp4Discover(pClient, pMac, &Offer))
        return false;
    if (!tstIPv4InPool(Offer.uYiAddrBe, 10, 12))
    {
        RTTestFailed(g_hTest, "DHCPOFFER for %RTmac contains out-of-pool address %RTnaipv4", pMac, Offer.uYiAddrBe);
        return false;
    }
    if (Offer.uServerIdBe != tstIPv4(10,37,0,1))
    {
        RTTestFailed(g_hTest, "DHCPOFFER for %RTmac has server ID %RTnaipv4, expected 10.37.0.1", pMac, Offer.uServerIdBe);
        return false;
    }
    if (!tstDhcp4Request(pClient, pMac, &Offer, pAck))
        return false;
    if (pAck->uYiAddrBe != Offer.uYiAddrBe)
    {
        RTTestFailed(g_hTest, "DHCPACK for %RTmac assigns %RTnaipv4, offered %RTnaipv4",
                     pMac, pAck->uYiAddrBe, Offer.uYiAddrBe);
        return false;
    }
    return true;
}

static bool tstStartInProcessServer(char *pszNetwork, size_t cbNetwork, PTSTCLIENT pClient, void **ppvDhcpd)
{
    char szCfg[RTPATH_MAX];

    int rc = tstMakeUuidName("VBoxNetDhcpdWire", pszNetwork, cbNetwork);
    if (RT_FAILURE(rc))
        return false;

    rc = tstWriteConfigFile(pszNetwork,
                            "10.37.0.1",
                            "255.255.255.0",
                            "10.37.0.10",
                            "10.37.0.12",
                            szCfg,
                            sizeof(szCfg));
    if (RT_FAILURE(rc))
        return false;

    char *argv[5];
    argv[0] = (char *)"VBoxNetDHCP-inproc";
    argv[1] = (char *)"--config";
    argv[2] = szCfg;
    argv[3] = (char *)"--comment";
    argv[4] = pszNetwork;

    rc = VBoxNetDhcpdTestStart(5, argv, ppvDhcpd);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "VBoxNetDhcpdTestStart failed: %Rrc", rc);
        return false;
    }

    rc = tstClientInit(pClient, pszNetwork);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "test client IntNet attach failed: %Rrc", rc);
        tstClientDestroy(pClient);
        return false;
    }

    return true;
}

static void tstWireDhcp(void)
{
    RTTestSub(g_hTest, "in-process daemon setup");

    char szNetwork[128];
    TSTCLIENT Client;
    void *pvDhcpd = NULL;

    RT_ZERO(Client);

    if (!tstStartInProcessServer(szNetwork, sizeof(szNetwork), &Client, &pvDhcpd))
    {
        if (pvDhcpd)
            VBoxNetDhcpdTestStop(pvDhcpd);
        return;
    }

    if (!VBoxNetDhcpdTestIsRunning(pvDhcpd))
        RTTestFailed(g_hTest, "VBoxNetDHCP stopped after reporting successful startup");

    static const RTMAC s_Mac1 = { { 0x08, 0x00, 0x27, 0x12, 0x34, 0x56 } };
    static const RTMAC s_Mac2 = { { 0x08, 0x00, 0x27, 0x00, 0x00, 0x02 } };
    static const RTMAC s_Mac3 = { { 0x08, 0x00, 0x27, 0x00, 0x00, 0x03 } };
    static const RTMAC s_Mac4 = { { 0x08, 0x00, 0x27, 0x00, 0x00, 0x04 } };

    TSTDHCP4REPLY Ack1;
    TSTDHCP4REPLY Ack1Again;
    TSTDHCP4REPLY Ack2;
    TSTDHCP4REPLY Ack3;
    TSTDHCP4REPLY Offer4;

    RT_ZERO(Ack1);
    RT_ZERO(Ack1Again);
    RT_ZERO(Ack2);
    RT_ZERO(Ack3);
    RT_ZERO(Offer4);

    RTTestSub(g_hTest, "first client lease");
    bool const fAck1 = tstLeaseClient(&Client, &s_Mac1, &Ack1);

    RTTestSub(g_hTest, "first client lease reuse");
    bool fAck1Again = false;
    if (fAck1)
    {
        fAck1Again = tstLeaseClient(&Client, &s_Mac1, &Ack1Again);
        if (fAck1Again && Ack1Again.uYiAddrBe != Ack1.uYiAddrBe)
            RTTestFailed(g_hTest, "Repeated lease changed from %RTnaipv4 to %RTnaipv4",
                         Ack1.uYiAddrBe, Ack1Again.uYiAddrBe);
    }
    else
        RTTestSkipped(g_hTest, "Initial lease failed");

    RTTestSub(g_hTest, "invalid DHCP cookie rejection");
    tstSendBadCookieNoReply(&Client);

    RTTestSub(g_hTest, "DHCPv6 packet rejection");
    tstSendDhcp6SolicitNoReply(&Client);

    RTTestSub(g_hTest, "second client lease");
    bool const fAck2 = tstLeaseClient(&Client, &s_Mac2, &Ack2);

    RTTestSub(g_hTest, "third client lease");
    bool const fAck3 = tstLeaseClient(&Client, &s_Mac3, &Ack3);

    RTTestSub(g_hTest, "unique client leases");
    if (fAck1 && fAck2 && fAck3)
    {
        if (Ack1.uYiAddrBe == Ack2.uYiAddrBe)
            RTTestFailed(g_hTest, "First and second clients both received %RTnaipv4", Ack1.uYiAddrBe);
        if (Ack1.uYiAddrBe == Ack3.uYiAddrBe)
            RTTestFailed(g_hTest, "First and third clients both received %RTnaipv4", Ack1.uYiAddrBe);
        if (Ack2.uYiAddrBe == Ack3.uYiAddrBe)
            RTTestFailed(g_hTest, "Second and third clients both received %RTnaipv4", Ack2.uYiAddrBe);
    }
    else
        RTTestSkipped(g_hTest, "One or more prerequisite lease exchanges failed");

    RTTestSub(g_hTest, "exhausted address pool");
    if (fAck1 && fAck2 && fAck3)
    {
        uint32_t const uXid = RTRandU32();
        int const rc = tstDhcp4DiscoverOnce(&Client, &s_Mac4, uXid, &Offer4, 1000);
        if (RT_SUCCESS(rc))
            RTTestFailed(g_hTest, "Exhausted pool offered %RTnaipv4 to fourth client (xid %#RX32)",
                         Offer4.uYiAddrBe, uXid);
        else if (rc != VERR_TIMEOUT)
            RTTestFailed(g_hTest, "Fourth-client DHCPDISCOVER failed with %Rrc instead of timing out", rc);
        else
        {
            int const rcThread = ASMAtomicReadS32(&Client.rcThread);
            if (RT_FAILURE(rcThread))
                RTTestFailed(g_hTest, "Client receive thread failed while checking pool exhaustion: %Rrc", rcThread);
        }
    }
    else
        RTTestSkipped(g_hTest, "Address pool was not successfully filled");

    RTTestSub(g_hTest, "in-process daemon teardown");
    int const rcClientBeforeTeardown = ASMAtomicReadS32(&Client.rcThread);
    tstClientDestroy(&Client);

    int rc = VBoxNetDhcpdTestStop(pvDhcpd);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "VBoxNetDhcpdTestStop failed: %Rrc", rc);
        RTTestFailureDetails(g_hTest, "client receive status before teardown: %Rrc; after teardown: %Rrc\n",
                             rcClientBeforeTeardown, ASMAtomicReadS32(&Client.rcThread));
    }
}

int main(int argc, char **argv)
{
    /* The embedded-switch build forces the driverless R3 path, so SUPLib must
       not be initialized by the testcase process. */
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTTestInitAndCreate("tstVBoxNetDhcpd", &g_hTest);
    if (g_hTest == NIL_RTTEST)
        return 1;

    RTTestBanner(g_hTest);

    tstConfigValidation();

    void *pvSwitch = NULL;
    rc = VBoxIntNetSwitchTestStart(&pvSwitch);
    if (RT_SUCCESS(rc))
    {
        tstWireDhcp();
        rc = VBoxIntNetSwitchTestStop(pvSwitch);
        TST_CHECK_RC_OK(rc);
    }
    else
        RTTestFailed(g_hTest, "Embedded IntNet switch startup failed: %Rrc", rc);

    return RTTestSummaryAndDestroy(g_hTest);
}
