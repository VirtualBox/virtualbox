/* $Id: tftp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - TFTP server.
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

/*
 * This code is based on:
 *
 * tftp.c - a simple, read-only tftp server for qemu
 *
 * Copyright (c) 2004 Magnus Damm <damm@opensource.se>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <slirp.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/path.h>

typedef enum ENMTFTPSESSIONFMT
{
    TFTPFMT_NONE = 0,
    TFTPFMT_OCTET,
    TFTPFMT_NETASCII,
    TFTPFMT_MAIL,
    TFTPFMT_NOT_FMT = 0xffff
} ENMTFTPSESSIONFMT;

typedef struct TFPTPSESSIONOPTDESC
{
    int fRequested;
    uint64_t u64Value;
} TFPTPSESSIONOPTDESC, *PTFPTPSESSIONOPTDESC;

typedef struct TFTPSESSION
{
    int         fInUse;
    struct      in_addr IpClientAddress;
    uint16_t    u16ClientPort;
    int         iTimestamp;
    uint64_t    cbTransfered;
    uint16_t    cTftpAck;
    ENMTFTPSESSIONFMT enmTftpFmt;
    TFPTPSESSIONOPTDESC OptionBlkSize;
    TFPTPSESSIONOPTDESC OptionTSize;
    TFPTPSESSIONOPTDESC OptionTimeout;

    const char *pcszFilenameHost;
    char szFilename[TFTP_FILENAME_MAX];
} TFTPSESSION, *PTFTPSESSION, **PPTFTPSESSION;

#pragma pack(1)
typedef struct TFTPCOREHDR
{
    uint16_t    u16TftpOpCode;
    /* Data lays here (might be raw uint8_t* or header of payload ) */
} TFTPCOREHDR, *PTFTPCOREHDR;

typedef struct TFTPIPHDR
{
    struct ip       IPv4Hdr;
    struct udphdr   UdpHdr;
    uint16_t        u16TftpOpType;
    TFTPCOREHDR     Core;
    /* Data lays here */
} TFTPIPHDR, *PTFTPIPHDR;
#pragma pack()

typedef const PTFTPIPHDR PCTFTPIPHDR;

typedef const PTFTPSESSION PCTFTPSESSION;


typedef struct TFTPOPTIONDESC
{
    const char *pszName;
    ENMTFTPSESSIONFMT enmType;
    int         cbName;
    bool        fHasValue;
} TFTPOPTIONDESC, *PTFTPOPTIONDESC;

typedef const PTFTPOPTIONDESC PCTFTPOPTIONDESC;
static TFTPOPTIONDESC g_TftpTransferFmtDesc[] =
{
    {"octet", TFTPFMT_OCTET, 5, false}, /* RFC1350 */
    {"netascii", TFTPFMT_NETASCII, 8, false}, /* RFC1350 */
    {"mail", TFTPFMT_MAIL, 4, false}, /* RFC1350 */
};

static TFTPOPTIONDESC g_TftpDesc[] =
{
    {"blksize", TFTPFMT_NOT_FMT, 7, true}, /* RFC2348 */
    {"timeout", TFTPFMT_NOT_FMT, 7, true}, /* RFC2349 */
    {"tsize", TFTPFMT_NOT_FMT, 5, true}, /* RFC2349 */
    {"size", TFTPFMT_NOT_FMT, 4, true}, /* RFC2349 */
};


DECLINLINE(struct mbuf *) slirpTftpMbufAlloc(PNATState pData)
{
    struct mbuf *m = slirpServiceMbufAlloc(pData, CTL_TFTP);
    if (RT_UNLIKELY(m == NULL))
        LogFlowFunc(("LEAVE: Can't allocate mbuf\n"));
    return m;
}


/**
 * This function resolves file name relative to tftp prefix.
 * @param pData
 * @param pTftpSession
 */
DECLINLINE(int) tftpSecurityFilenameCheck(PNATState pData, PTFTPSESSION pTftpSession)
{
    int rc = VERR_FILE_NOT_FOUND; /* guilty until proved innocent */

    AssertPtrReturn(pTftpSession, VERR_INVALID_PARAMETER);
    AssertReturn(pTftpSession->pcszFilenameHost == NULL, VERR_INVALID_PARAMETER);

    /* prefix must be set to an absolute pathname.  assert? */
    if (tftp_prefix == NULL || RTPathSkipRootSpec(tftp_prefix) == tftp_prefix)
        goto done;

    /* replace backslashes with forward slashes */
    char *s = pTftpSession->szFilename;
    while ((s = strchr(s, '\\')) != NULL)
        *s++ = '/';

    /* deny dot-dot by itself or at the beginning */
    if (   pTftpSession->szFilename[0] == '.'
        && pTftpSession->szFilename[1] == '.'
        && (   pTftpSession->szFilename[2] == '\0'
            || pTftpSession->szFilename[2] == '/'))
        goto done;

    /* deny dot-dot in the middle */
    if (RTStrStr(pTftpSession->szFilename, "/../") != NULL)
        goto done;

    /* deny dot-dot at the end (there's no RTStrEndsWith) */
    const char *dotdot = RTStrStr(pTftpSession->szFilename, "/..");
    if (dotdot != NULL && dotdot[3] == '\0')
        goto done;

    char *pszPathHostAbs;
    int cbLen = RTStrAPrintf(&pszPathHostAbs, "%s/%s",
                           tftp_prefix, pTftpSession->szFilename);
    if (cbLen == -1)
        goto done;

    LogRel2(("NAT: TFTP: %s\n", pszPathHostAbs));
    pTftpSession->pcszFilenameHost = pszPathHostAbs;
    rc = VINF_SUCCESS;

  done:
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/*
 * This function returns index of option descriptor in passed descriptor array
 * @param piIdxOpt returned index value
 * @param paTftpDesc array of known Tftp descriptors
 * @param caTftpDesc size of array of tftp descriptors
 * @param pszOpt name of option
 */
DECLINLINE(int) tftpFindDesciptorIndexByName(int *piIdxOpt, PCTFTPOPTIONDESC paTftpDesc, int caTftpDesc, const char *pszOptName)
{
    int rc = VINF_SUCCESS;
    int idxOption = 0;
    AssertReturn(piIdxOpt, VERR_INVALID_PARAMETER);
    AssertReturn(paTftpDesc, VERR_INVALID_PARAMETER);
    AssertReturn(pszOptName, VERR_INVALID_PARAMETER);
    for (idxOption = 0; idxOption < caTftpDesc; ++idxOption)
    {
        if (!RTStrNICmp(pszOptName, paTftpDesc[idxOption].pszName, 10))
        {
            *piIdxOpt = idxOption;
            return rc;
        }
    }
    rc = VERR_NOT_FOUND;
    return rc;
}

/**
 * Helper function to look for index of descriptor in transfer format descriptors
 * @param piIdxOpt returned value of index
 * @param pszOpt name of option
 */
DECLINLINE(int) tftpFindTransferFormatIdxbyName(int *piIdxOpt, const char *pszOpt)
{
    return tftpFindDesciptorIndexByName(piIdxOpt, &g_TftpTransferFmtDesc[0], RT_ELEMENTS(g_TftpTransferFmtDesc), pszOpt);
}

/**
 * Helper function to look for index of descriptor in options descriptors
 * @param piIdxOpt returned value of index
 * @param pszOpt name of option
 */
DECLINLINE(int) tftpFindOptionIdxbyName(int *piIdxOpt, const char *pszOpt)
{
    return tftpFindDesciptorIndexByName(piIdxOpt, &g_TftpDesc[0], RT_ELEMENTS(g_TftpDesc), pszOpt);
}


#if 0 /* unused */
DECLINLINE(bool) tftpIsAcceptableOption(const char *pszOptionName)
{
    int idxOptDesc = 0;
    AssertPtrReturn(pszOptionName, false);
    AssertReturn(RTStrNLen(pszOptionName,10) >= 4, false);
    AssertReturn(RTStrNLen(pszOptionName,10) < 8, false);
    for(idxOptDesc = 0; idxOptDesc < RT_ELEMENTS(g_TftpTransferFmtDesc); ++idxOptDesc)
    {
        if (!RTStrNICmp(pszOptionName, g_TftpTransferFmtDesc[idxOptDesc].pszName, 10))
            return true;
    }
    for(idxOptDesc = 0; idxOptDesc < RT_ELEMENTS(g_TftpDesc); ++idxOptDesc)
    {
        if (!RTStrNICmp(pszOptionName, g_TftpDesc[idxOptDesc].pszName, 10))
            return true;
    }
    return false;
}
#endif /* unused */


/**
 * This helper function that validate if client want to operate in supported by server mode.
 * @param pcTftpHeader comulative header (IP, UDP, TFTP)
 * @param pcu8Options pointer to the options supposing that pointer points at the mode option
 * @param cbOptions size of the options buffer
 */
DECLINLINE(int) tftpIsSupportedTransferMode(PCTFTPSESSION pcTftpSession)
{
    AssertPtrReturn(pcTftpSession, 0);
    return (pcTftpSession->enmTftpFmt == TFTPFMT_OCTET);
}


DECLINLINE(void) tftpSessionUpdate(PNATState pData, PTFTPSESSION pTftpSession)
{
    pTftpSession->iTimestamp = curtime;
    pTftpSession->fInUse = 1;
}

DECLINLINE(void) tftpSessionTerminate(PTFTPSESSION pTftpSession)
{
    if (pTftpSession->pcszFilenameHost != NULL)
    {
        RTStrFree((char *)pTftpSession->pcszFilenameHost);
        pTftpSession->pcszFilenameHost = NULL;
    }

    pTftpSession->fInUse = 0;
}

DECLINLINE(int) tftpSessionParseAndMarkOption(const char *pcszRawOption, PTFPTPSESSIONOPTDESC pTftpSessionOption)
{
    int rc  = VINF_SUCCESS;
    rc = RTStrToInt64Full(pcszRawOption, 0, (int64_t *)&pTftpSessionOption->u64Value);
    AssertRCReturn(rc, rc);
    pTftpSessionOption->fRequested = 1;
    return rc;
}

DECLINLINE(int) tftpSessionOptionParse(PTFTPSESSION pTftpSession, PCTFTPIPHDR pcTftpIpHeader)
{
    int rc = VINF_SUCCESS;
    char *pszTftpRRQRaw;
    size_t idxTftpRRQRaw = 0;
    ssize_t cbTftpRRQRaw = 0;
    int fWithArg = 0;
    int idxOptionArg = 0;

    AssertPtrReturn(pTftpSession, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcTftpIpHeader, VERR_INVALID_PARAMETER);
    AssertReturn(RT_N2H_U16(pcTftpIpHeader->u16TftpOpType) == TFTP_RRQ, VERR_INVALID_PARAMETER);
    LogFlowFunc(("pTftpSession:%p, pcTftpIpHeader:%p\n", pTftpSession, pcTftpIpHeader));

    pszTftpRRQRaw = (char *)&pcTftpIpHeader->Core;
    cbTftpRRQRaw = RT_H2N_U16(pcTftpIpHeader->UdpHdr.uh_ulen) + sizeof(struct ip) - RT_UOFFSETOF(TFTPIPHDR, Core);
    while (cbTftpRRQRaw)
    {
        rc = RTStrNLenEx(pszTftpRRQRaw, cbTftpRRQRaw, &idxTftpRRQRaw);
        if (RT_SUCCESS(rc))
            ++idxTftpRRQRaw;    /* count the NUL too */
        else
            break;

        if (RTStrNLen(pTftpSession->szFilename, TFTP_FILENAME_MAX) == 0)
        {
            rc = RTStrCopy(pTftpSession->szFilename, TFTP_FILENAME_MAX, pszTftpRRQRaw);
            if (RT_FAILURE(rc))
            {
                LogFlowFuncLeaveRC(rc);
                AssertRCReturn(rc,rc);
            }
        }
        else if (pTftpSession->enmTftpFmt == TFTPFMT_NONE)
        {
            int idxFmt = 0;
            rc = tftpFindTransferFormatIdxbyName(&idxFmt, pszTftpRRQRaw);
            if (RT_FAILURE(rc))
            {
                LogFlowFuncLeaveRC(VERR_INTERNAL_ERROR);
                return VERR_INTERNAL_ERROR;
            }
            AssertReturn(   g_TftpTransferFmtDesc[idxFmt].enmType != TFTPFMT_NONE
                         && g_TftpTransferFmtDesc[idxFmt].enmType != TFTPFMT_NOT_FMT, VERR_INTERNAL_ERROR);
            pTftpSession->enmTftpFmt = g_TftpTransferFmtDesc[idxFmt].enmType;
        }
        else if (fWithArg)
        {
            if (!RTStrICmp("blksize", g_TftpDesc[idxOptionArg].pszName))
                rc = tftpSessionParseAndMarkOption(pszTftpRRQRaw, &pTftpSession->OptionBlkSize);

            if (   RT_SUCCESS(rc)
                && !RTStrICmp("tsize", g_TftpDesc[idxOptionArg].pszName))
                rc = tftpSessionParseAndMarkOption(pszTftpRRQRaw, &pTftpSession->OptionTSize);

            /** @todo we don't use timeout, but its value in the range 0-255 */
            if (   RT_SUCCESS(rc)
                && !RTStrICmp("timeout", g_TftpDesc[idxOptionArg].pszName))
                rc = tftpSessionParseAndMarkOption(pszTftpRRQRaw, &pTftpSession->OptionTimeout);

            /** @todo unknown option detection */
            if (RT_FAILURE(rc))
            {
                LogFlowFuncLeaveRC(rc);
                AssertRCReturn(rc,rc);
            }
            fWithArg = 0;
            idxOptionArg = 0;
        }
        else
        {
            rc = tftpFindOptionIdxbyName(&idxOptionArg, pszTftpRRQRaw);
            if (RT_SUCCESS(rc))
                fWithArg = 1;
            else
            {
                LogFlowFuncLeaveRC(rc);
                AssertRCReturn(rc,rc);
            }
        }
        pszTftpRRQRaw += idxTftpRRQRaw;
        cbTftpRRQRaw  -= idxTftpRRQRaw;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int tftpAllocateSession(PNATState pData, PCTFTPIPHDR pcTftpIpHeader, PPTFTPSESSION ppTftpSession)
{
    PTFTPSESSION pTftpSession = NULL;
    int rc = VINF_SUCCESS;
    int idxSession;
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcTftpIpHeader, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppTftpSession, VERR_INVALID_PARAMETER);

    for (idxSession = 0; idxSession < TFTP_SESSIONS_MAX; idxSession++)
    {
        pTftpSession = &((PTFTPSESSION)pData->pvTftpSessions)[idxSession];

        if (!pTftpSession->fInUse)
            goto found;

        /* sessions time out after 5 inactive seconds */
        if ((int)(curtime - pTftpSession->iTimestamp) > 5000)
            goto found;
    }

    return VERR_NOT_FOUND;

 found:
    if (pTftpSession->pcszFilenameHost != NULL)
    {
        RTStrFree((char *)pTftpSession->pcszFilenameHost);
        // pTftpSession->pcszFilenameHost = NULL; /* will be zeroed out below */
    }
    RT_ZERO(*pTftpSession);

    memcpy(&pTftpSession->IpClientAddress, &pcTftpIpHeader->IPv4Hdr.ip_src, sizeof(pTftpSession->IpClientAddress));
    pTftpSession->u16ClientPort = pcTftpIpHeader->UdpHdr.uh_sport;
    rc = tftpSessionOptionParse(pTftpSession, pcTftpIpHeader);
    AssertRCReturn(rc, VERR_INTERNAL_ERROR);
    *ppTftpSession = pTftpSession;

    LogRel(("NAT: TFTP RRQ %s", pTftpSession->szFilename));
    const char *pszPrefix = " ";
    if (pTftpSession->OptionBlkSize.fRequested)
    {
        LogRel(("%s" "blksize=%RU64", pszPrefix, pTftpSession->OptionBlkSize.u64Value));
        pszPrefix = ", ";
    }
    if (pTftpSession->OptionTSize.fRequested)
    {
        LogRel(("%s" "tsize=%RU64", pszPrefix, pTftpSession->OptionTSize.u64Value));
        pszPrefix = ", ";
    }
    if (pTftpSession->OptionTimeout.fRequested)
    {
        LogRel(("%s" "timeout=%RU64", pszPrefix, pTftpSession->OptionTimeout.u64Value));
        pszPrefix = ", ";
    }
    LogRel(("\n"));

    tftpSessionUpdate(pData, pTftpSession);

    return VINF_SUCCESS;
}

static int tftpSessionFind(PNATState pData, PCTFTPIPHDR pcTftpIpHeader, PPTFTPSESSION ppTftpSessions)
{
    PTFTPSESSION pTftpSession;
    int idxTftpSession;
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcTftpIpHeader, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppTftpSessions, VERR_INVALID_PARAMETER);

    for (idxTftpSession = 0; idxTftpSession < TFTP_SESSIONS_MAX; idxTftpSession++)
    {
        pTftpSession = &((PTFTPSESSION)pData->pvTftpSessions)[idxTftpSession];

        if (pTftpSession->fInUse)
        {
            if (!memcmp(&pTftpSession->IpClientAddress, &pcTftpIpHeader->IPv4Hdr.ip_src, sizeof(pTftpSession->IpClientAddress)))
            {
                if (pTftpSession->u16ClientPort == pcTftpIpHeader->UdpHdr.uh_sport)
                {
                    *ppTftpSessions = pTftpSession;
                    return VINF_SUCCESS;
                }
            }
        }
    }

    return VERR_NOT_FOUND;
}

DECLINLINE(int) pftpSessionOpenFile(PTFTPSESSION pTftpSession, PRTFILE pSessionFile)
{
    int rc;
    LogFlowFuncEnter();

    if (pTftpSession->pcszFilenameHost == NULL)
    {
        rc = VERR_FILE_NOT_FOUND;
    }
    else
    {
        rc = RTFileOpen(pSessionFile, pTftpSession->pcszFilenameHost,
                        RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE);
        if (RT_FAILURE(rc))
            rc = VERR_FILE_NOT_FOUND;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

DECLINLINE(int) tftpSessionEvaluateOptions(PTFTPSESSION pTftpSession)
{
    int rc;
    RTFILE hSessionFile;
    uint64_t cbSessionFile = 0;
    int cOptions;
    LogFlowFunc(("pTftpSession:%p\n", pTftpSession));

    rc = pftpSessionOpenFile(pTftpSession, &hSessionFile);
    if (RT_FAILURE(rc))
    {
        LogFlowFuncLeaveRC(rc);
        return rc;
    }

    rc = RTFileQuerySize(hSessionFile, &cbSessionFile);
    RTFileClose(hSessionFile);
    if (RT_FAILURE(rc))
    {
        LogFlowFuncLeaveRC(rc);
        return rc;
    }

    cOptions = 0;

    if (pTftpSession->OptionTSize.fRequested)
    {
        pTftpSession->OptionTSize.u64Value = cbSessionFile;
        ++cOptions;
    }

    if (pTftpSession->OptionBlkSize.fRequested)
    {
        if (pTftpSession->OptionBlkSize.u64Value < 8)
        {
            /*
             * we cannot make a counter-offer larger than the client's
             * value, so just pretend we didn't recognize it and use
             * default block size
             */
            pTftpSession->OptionBlkSize.fRequested = 0;
            pTftpSession->OptionBlkSize.u64Value = 512;
        }
        else if (pTftpSession->OptionBlkSize.u64Value > 1428)
        {
            pTftpSession->OptionBlkSize.u64Value = 1428;
            ++cOptions;
        }
    }
    else
    {
        pTftpSession->OptionBlkSize.u64Value = 512;
    }

    rc = cOptions > 0 ? VINF_SUCCESS : VWRN_NOT_FOUND;
    LogFlowFuncLeaveRC(rc);
    return rc;
}

DECLINLINE(int) tftpSend(PNATState pData,
                         PTFTPSESSION pTftpSession,
                         struct mbuf *pMBuf,
                         PCTFTPIPHDR pcTftpIpHeaderRecv)
{
    struct sockaddr_in saddr, daddr;
    int error, rc;

    LogFlowFunc(("pMBuf:%p, pcTftpIpHeaderRecv:%p\n", pMBuf, pcTftpIpHeaderRecv));
    saddr.sin_addr = pcTftpIpHeaderRecv->IPv4Hdr.ip_dst;
    saddr.sin_port = pcTftpIpHeaderRecv->UdpHdr.uh_dport;

    daddr.sin_addr = pTftpSession->IpClientAddress;
    daddr.sin_port = pTftpSession->u16ClientPort;


    pMBuf->m_data += sizeof(struct udpiphdr);
    pMBuf->m_len -= sizeof(struct udpiphdr);

    error = udp_output2(pData, NULL, pMBuf, &saddr, &daddr, IPTOS_LOWDELAY);
    rc = error ? VERR_GENERAL_FAILURE : VINF_SUCCESS;

    LogFlowFuncLeaveRC(rc);
    return rc;
}


DECLINLINE(int) tftpSendError(PNATState pData, PTFTPSESSION pTftpSession, uint16_t errorcode,
                              const char *msg, PCTFTPIPHDR pcTftpIpHeaderRecv); /* gee wiz */

DECLINLINE(int) tftpReadDataBlock(PNATState pData,
                                  PTFTPSESSION pcTftpSession,
                                  uint8_t *pu8Data,
                                  int *pcbReadData)
{
    RTFILE  hSessionFile;
    int rc = VINF_SUCCESS;
    uint16_t u16BlkSize = 0;
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcTftpSession, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pu8Data, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbReadData, VERR_INVALID_PARAMETER);
    LogFlowFunc(("pcTftpSession:%p, pu8Data:%p, pcbReadData:%p\n",
                    pcTftpSession,
                    pu8Data,
                    pcbReadData));

    u16BlkSize = (uint16_t)pcTftpSession->OptionBlkSize.u64Value;
    rc = pftpSessionOpenFile(pcTftpSession, &hSessionFile);
    if (RT_FAILURE(rc))
    {
        LogFlowFuncLeaveRC(rc);
        return rc;
    }

    if (pcbReadData)
    {
        size_t cbRead;

        rc = RTFileSeek(hSessionFile,
                        pcTftpSession->cbTransfered,
                        RTFILE_SEEK_BEGIN,
                        NULL);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hSessionFile);
            LogFlowFuncLeaveRC(rc);
            return rc;
        }
        rc = RTFileRead(hSessionFile, pu8Data, u16BlkSize, &cbRead);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hSessionFile);
            LogFlowFuncLeaveRC(rc);
            return rc;
        }
        *pcbReadData = (int)cbRead;
    }

    rc = RTFileClose(hSessionFile);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

DECLINLINE(int) tftpAddOptionToOACK(PNATState pData, struct mbuf *pMBuf, const char *pszOptName, uint64_t u64OptValue)
{
    char szOptionBuffer[256];
    size_t iOptLength;
    int rc = VINF_SUCCESS;
    int cbMBufCurrent = pMBuf->m_len;
    LogFlowFunc(("pMBuf:%p, pszOptName:%s, u16OptValue:%ld\n", pMBuf, pszOptName, u64OptValue));
    AssertPtrReturn(pMBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszOptName, VERR_INVALID_PARAMETER);

    RT_ZERO(szOptionBuffer);
    iOptLength  = RTStrPrintf(szOptionBuffer, 256 , "%s", pszOptName) + 1;
    iOptLength += RTStrPrintf(szOptionBuffer + iOptLength, 256 - iOptLength , "%llu", u64OptValue) + 1;
    if (iOptLength > M_TRAILINGSPACE(pMBuf))
        rc = VERR_BUFFER_OVERFLOW; /* buffer too small */
    else
    {
        pMBuf->m_len += (int)iOptLength;
        m_copyback(pData, pMBuf, cbMBufCurrent, (int)iOptLength, szOptionBuffer);
    }
    LogFlowFuncLeaveRC(rc);
    return rc;
}


DECLINLINE(int) tftpSendOACK(PNATState pData,
                             PTFTPSESSION pTftpSession,
                             PCTFTPIPHDR pcTftpIpHeaderRecv)
{
    struct mbuf *m;
    PTFTPIPHDR pTftpIpHeader;
    int rc;

    rc = tftpSessionEvaluateOptions(pTftpSession);
    if (RT_FAILURE(rc))
    {
        tftpSendError(pData, pTftpSession, TFTP_EACCESS, "Option negotiation failure (file not found or inaccessible?)", pcTftpIpHeaderRecv);
        LogFlowFuncLeave();
        return rc;
    }

    if (rc == VWRN_NOT_FOUND)
        return rc;

    m = slirpTftpMbufAlloc(pData);
    if (m == NULL)
    {
        tftpSessionTerminate(pTftpSession);
        return VERR_NO_MEMORY;
    }

    m->m_data += if_maxlinkhdr;
    m->m_pkthdr.header = mtod(m, void *);
    pTftpIpHeader = mtod(m, PTFTPIPHDR);
    m->m_len = sizeof(TFTPIPHDR) - sizeof(uint16_t); /* no u16TftpOpCode */

    pTftpIpHeader->u16TftpOpType = RT_H2N_U16_C(TFTP_OACK);

    if (pTftpSession->OptionBlkSize.fRequested)
        rc = tftpAddOptionToOACK(pData, m, "blksize", pTftpSession->OptionBlkSize.u64Value);

    if (   RT_SUCCESS(rc)
        && pTftpSession->OptionTSize.fRequested)
        rc = tftpAddOptionToOACK(pData, m, "tsize", pTftpSession->OptionTSize.u64Value);

    rc = tftpSend(pData, pTftpSession, m, pcTftpIpHeaderRecv);
    if (RT_FAILURE(rc))
        tftpSessionTerminate(pTftpSession);

    return rc;
}


DECLINLINE(int) tftpSendError(PNATState pData,
                              PTFTPSESSION pTftpSession,
                              uint16_t errorcode,
                              const char *msg,
                              PCTFTPIPHDR pcTftpIpHeaderRecv)
{
    struct mbuf *m = NULL;

    LogFlowFunc(("ENTER: errorcode: %RX16, msg: %s\n", errorcode, msg));
    m = slirpTftpMbufAlloc(pData);
    if (m != NULL)
    {
        u_int cbMsg = (u_int)strlen(msg) + 1; /* ending zero */
        PTFTPIPHDR pTftpIpHeader;

        m->m_data += if_maxlinkhdr;
        m->m_len = sizeof(TFTPIPHDR) + cbMsg;
        m->m_pkthdr.header = mtod(m, void *);
        pTftpIpHeader = mtod(m, PTFTPIPHDR);

        pTftpIpHeader->u16TftpOpType = RT_H2N_U16_C(TFTP_ERROR);
        pTftpIpHeader->Core.u16TftpOpCode = RT_H2N_U16(errorcode);

        m_copyback(pData, m, sizeof(TFTPIPHDR), cbMsg, (c_caddr_t)msg);

        tftpSend(pData, pTftpSession, m, pcTftpIpHeaderRecv);
    }

    tftpSessionTerminate(pTftpSession);

    LogFlowFuncLeave();
    return 0;
}


static int tftpSendData(PNATState pData,
                          PTFTPSESSION pTftpSession,
                          uint16_t u16Block,
                          PCTFTPIPHDR pcTftpIpHeaderRecv)
{
    struct mbuf *m;
    PTFTPIPHDR pTftpIpHeader;
    int cbRead = 0;
    int rc = VINF_SUCCESS;

    if (u16Block == pTftpSession->cTftpAck)
        pTftpSession->cTftpAck++;
    else
    {
        tftpSendError(pData, pTftpSession, TFTP_EEXIST, "ACK is wrong", pcTftpIpHeaderRecv);
        return -1;
    }

    m = slirpTftpMbufAlloc(pData);
    if (!m)
        return -1;

    m->m_data += if_maxlinkhdr;
    m->m_pkthdr.header = mtod(m, void *);
    pTftpIpHeader = mtod(m, PTFTPIPHDR);
    m->m_len = sizeof(TFTPIPHDR);

    pTftpIpHeader->u16TftpOpType = RT_H2N_U16_C(TFTP_DATA);
    pTftpIpHeader->Core.u16TftpOpCode = RT_H2N_U16(pTftpSession->cTftpAck);

    if (RT_LIKELY(M_TRAILINGSPACE(m) >= pTftpSession->OptionBlkSize.u64Value))
    {
        uint8_t *pu8Data = (uint8_t *)&pTftpIpHeader->Core.u16TftpOpCode + sizeof(uint16_t);
        rc = tftpReadDataBlock(pData, pTftpSession, pu8Data, &cbRead);
    }
    else
        rc = VERR_BUFFER_OVERFLOW;

    if (RT_SUCCESS(rc))
    {
        pTftpSession->cbTransfered += cbRead;
        m->m_len += cbRead;
        tftpSend(pData, pTftpSession, m, pcTftpIpHeaderRecv);
        if (cbRead > 0)
            tftpSessionUpdate(pData, pTftpSession);
        else
            tftpSessionTerminate(pTftpSession);
    }
    else
    {
        m_freem(pData, m);
        tftpSendError(pData, pTftpSession, TFTP_ENOENT, "File not found", pcTftpIpHeaderRecv);
        /* send "file not found" error back */
        return -1;
    }

    return 0;
}

DECLINLINE(void) tftpProcessRRQ(PNATState pData, PCTFTPIPHDR pTftpIpHeader, int pktlen)
{
    PTFTPSESSION pTftpSession = NULL;
    uint8_t *pu8Payload = NULL;
    int     cbPayload = 0;
    size_t cbFileName = 0;
    int rc = VINF_SUCCESS;

    AssertPtrReturnVoid(pTftpIpHeader);
    AssertPtrReturnVoid(pData);
    AssertReturnVoid(pktlen > sizeof(TFTPIPHDR));
    LogFlowFunc(("ENTER: pTftpIpHeader:%p, pktlen:%d\n", pTftpIpHeader, pktlen));

    rc = tftpAllocateSession(pData, pTftpIpHeader, &pTftpSession);
    if (   RT_FAILURE(rc)
        || pTftpSession == NULL)
    {
        LogFlowFuncLeave();
        return;
    }

    pu8Payload = (uint8_t *)&pTftpIpHeader->Core;
    cbPayload = pktlen - sizeof(TFTPIPHDR);

    cbFileName = RTStrNLen((char *)pu8Payload, cbPayload);
    /* We assume that file name should finish with '\0' and shouldn't bigger
     *  than buffer for name storage.
     */
    AssertReturnVoid(   (ssize_t)cbFileName < cbPayload
                     && cbFileName < TFTP_FILENAME_MAX /* current limit in tftp session handle */
                     && cbFileName);

    /* Dont't bother with rest processing in case of invalid access */
    if (RT_FAILURE(tftpSecurityFilenameCheck(pData, pTftpSession)))
    {
        tftpSendError(pData, pTftpSession, TFTP_EACCESS, "Access violation", pTftpIpHeader);
        LogFlowFuncLeave();
        return;
    }



    if (RT_UNLIKELY(!tftpIsSupportedTransferMode(pTftpSession)))
    {
        tftpSendError(pData, pTftpSession, TFTP_ENOSYS, "Unsupported transfer mode", pTftpIpHeader);
        LogFlowFuncLeave();
        return;
    }


    rc = tftpSendOACK(pData, pTftpSession, pTftpIpHeader);
    if (rc == VWRN_NOT_FOUND)
        rc = tftpSendData(pData, pTftpSession, 0, pTftpIpHeader);

    LogFlowFuncLeave();
    return;
}

static void tftpProcessACK(PNATState pData, PTFTPIPHDR pTftpIpHeader)
{
    int rc;
    PTFTPSESSION pTftpSession = NULL;

    rc = tftpSessionFind(pData, pTftpIpHeader, &pTftpSession);
    if (RT_FAILURE(rc))
        return;

    if (tftpSendData(pData, pTftpSession,
                     RT_N2H_U16(pTftpIpHeader->Core.u16TftpOpCode),
                     pTftpIpHeader))
        LogRel(("NAT: TFTP send failed\n"));
}

int slirpTftpInit(PNATState pData)
{
    AssertPtrReturn(pData, VERR_INVALID_PARAMETER);
    pData->pvTftpSessions = RTMemAllocZ(sizeof(TFTPSESSION) * TFTP_SESSIONS_MAX);
    AssertPtrReturn(pData->pvTftpSessions, VERR_NO_MEMORY);
    return VINF_SUCCESS;
}

void slirpTftpTerm(PNATState pData)
{
    RTMemFree(pData->pvTftpSessions);
}

int slirpTftpInput(PNATState pData, struct mbuf *pMbuf)
{
    PTFTPIPHDR pTftpIpHeader = NULL;
    AssertPtr(pData);
    AssertPtr(pMbuf);
    pTftpIpHeader = mtod(pMbuf, PTFTPIPHDR);

    switch(RT_N2H_U16(pTftpIpHeader->u16TftpOpType))
    {
        case TFTP_RRQ:
            tftpProcessRRQ(pData, pTftpIpHeader, m_length(pMbuf, NULL));
            break;

        case TFTP_ACK:
            tftpProcessACK(pData, pTftpIpHeader);
            break;

        case TFTP_ERROR:
        {
            PTFTPSESSION pTftpSession;
            int rc = tftpSessionFind(pData, pTftpIpHeader, &pTftpSession);
            if (RT_SUCCESS(rc))
                tftpSessionTerminate(pTftpSession);
        }

        default:;
    }
    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}
