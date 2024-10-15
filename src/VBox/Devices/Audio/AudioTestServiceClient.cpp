/* $Id: AudioTestServiceClient.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * AudioTestServiceClient - Audio Test Service (ATS), Client helpers.
 *
 * Note: Only does TCP/IP as transport layer for now.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_AUDIO_TEST

#include <iprt/crc.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/tcp.h>

#include <VBox/log.h>

#include "AudioTestService.h"
#include "AudioTestServiceInternal.h"
#include "AudioTestServiceClient.h"

/** @todo Use common defines between server protocol and this client. */

/**
 * A generic ATS reply, used by the client
 * to process the incoming packets.
 */
typedef struct ATSSRVREPLY
{
    char   szOp[ATSPKT_OPCODE_MAX_LEN];
    /** Pointer to payload data.
     *  This does *not* include the header! */
    void  *pvPayload;
    /** Size (in bytes) of the payload data.
     *  This does *not* include the header! */
    size_t cbPayload;
} ATSSRVREPLY;
/** Pointer to a generic ATS reply. */
typedef struct ATSSRVREPLY *PATSSRVREPLY;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int audioTestSvcClientDisconnectInternal(PATSCLIENT pClient);

/**
 * Initializes an ATS client, internal version.
 *
 * @param   pClient             Client to initialize.
 */
static void audioTestSvcClientInit(PATSCLIENT pClient)
{
    RT_BZERO(pClient, sizeof(ATSCLIENT));
}

/**
 * Destroys an ATS server reply.
 *
 * @param   pReply              Reply to destroy.
 */
static void audioTestSvcClientReplyDestroy(PATSSRVREPLY pReply)
{
    if (!pReply)
        return;

    if (pReply->pvPayload)
    {
        Assert(pReply->cbPayload);
        RTMemFree(pReply->pvPayload);
        pReply->pvPayload = NULL;
    }

    pReply->cbPayload = 0;
}

/**
 * Receives a reply from an ATS server.
 *
 * @returns VBox status code.
 * @param   pClient             Client to receive reply for.
 * @param   pReply              Where to store the reply.
 *                              The reply must be destroyed with audioTestSvcClientReplyDestroy() then.
 * @param   fNoDataOk           If it's okay that the reply is not expected to have any payload.
 */
static int audioTestSvcClientRecvReply(PATSCLIENT pClient, PATSSRVREPLY pReply, bool fNoDataOk)
{
    LogFlowFuncEnter();

    PATSPKTHDR pPktHdr;
    int rc = pClient->pTransport->pfnRecvPkt(pClient->pTransportInst, pClient->pTransportClient, &pPktHdr);
    if (RT_SUCCESS(rc))
    {
        AssertReleaseMsgReturn(pPktHdr->cb >= sizeof(ATSPKTHDR),
                               ("audioTestSvcClientRecvReply: Received invalid packet size (%RU32)\n", pPktHdr->cb),
                               VERR_NET_PROTOCOL_ERROR);
        pReply->cbPayload = pPktHdr->cb - sizeof(ATSPKTHDR);
        Log3Func(("szOp=%.8s, cb=%RU32\n", pPktHdr->achOpcode, pPktHdr->cb));
        if (pReply->cbPayload)
        {
            pReply->pvPayload = RTMemDup((uint8_t *)pPktHdr + sizeof(ATSPKTHDR), pReply->cbPayload);
        }
        else
            pReply->pvPayload = NULL;

        if (   !pReply->cbPayload
            && !fNoDataOk)
        {
            LogRelFunc(("Payload is empty (%zu), but caller expected data\n", pReply->cbPayload));
            rc = VERR_NET_PROTOCOL_ERROR;
        }
        else
        {
            memcpy(&pReply->szOp, &pPktHdr->achOpcode, sizeof(pReply->szOp));
        }

        RTMemFree(pPktHdr);
        pPktHdr = NULL;
    }

    if (RT_FAILURE(rc))
        LogRelFunc(("Receiving reply from server failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a reply for an ATS server and checks if it is an acknowledge (success) one.
 *
 * @returns VBox status code.
 * @retval  VERR_NET_PROTOCOL_ERROR if the reply indicates a failure.
 * @param   pClient             Client to receive reply for.
 */
static int audioTestSvcClientRecvAck(PATSCLIENT pClient)
{
    ATSSRVREPLY Reply;
    RT_ZERO(Reply);

    int rc = audioTestSvcClientRecvReply(pClient, &Reply, true /* fNoDataOk */);
    if (RT_SUCCESS(rc))
    {
        /* Most likely cases first. */
        if (     RTStrNCmp(Reply.szOp, "ACK     ", ATSPKT_OPCODE_MAX_LEN) == 0)
        {
            /* Nothing to do here. */
        }
        else if (RTStrNCmp(Reply.szOp, "FAILED  ", ATSPKT_OPCODE_MAX_LEN) == 0)
        {
            LogRelFunc(("Received error from server (cbPayload=%zu)\n", Reply.cbPayload));

            if (Reply.cbPayload)
            {
                if (   Reply.cbPayload >=  sizeof(int) /* At least the rc must be present. */
                    && Reply.cbPayload <= sizeof(ATSPKTREPFAIL) - sizeof(ATSPKTHDR))
                {
                    rc = *(int *)Reply.pvPayload; /* Reach error code back to caller. */

                    const char *pcszMsg = (char *)Reply.pvPayload + sizeof(int);
                    /** @todo Check NULL termination of pcszMsg? */

                    LogRelFunc(("Error message: %s (%Rrc)\n", pcszMsg, rc));
                }
                else
                {
                    LogRelFunc(("Received invalid failure payload (cb=%zu)\n", Reply.cbPayload));
                    rc = VERR_NET_PROTOCOL_ERROR;
                }
            }
        }
        else
        {
            LogRelFunc(("Received invalid opcode ('%.8s')\n", Reply.szOp));
            rc = VERR_NET_PROTOCOL_ERROR;
        }

        audioTestSvcClientReplyDestroy(&Reply);
    }

    LogRelFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a message plus optional payload to an ATS server.
 *
 * @returns VBox status code.
 * @param   pClient             Client to send message for.
 * @param   pvHdr               Pointer to header data to send.
 * @param   cbHdr               Size (in bytes) of \a pvHdr to send.
 */
static int audioTestSvcClientSendMsg(PATSCLIENT pClient, void *pvHdr, size_t cbHdr)
{
    RT_NOREF(cbHdr);
    AssertPtrReturn(pClient->pTransport,       VERR_INVALID_POINTER);
    AssertPtrReturn(pClient->pTransportInst,   VERR_INVALID_POINTER);
    AssertPtrReturn(pClient->pTransportClient, VERR_INVALID_POINTER);
    return pClient->pTransport->pfnSendPkt(pClient->pTransportInst, pClient->pTransportClient, (PCATSPKTHDR)pvHdr);
}

/**
 * Initializes a client request header.
 *
 * @param   pReqHdr             Request header to initialize.
 * @param   cbReq               Size (in bytes) the request will have (does *not* include payload).
 * @param   pszOp               Operation to perform with the request.
 * @param   cbPayload           Size (in bytes) of payload that will follow the header. Optional and can be 0.
 */
DECLINLINE(void) audioTestSvcClientReqHdrInit(PATSPKTHDR pReqHdr, size_t cbReq, const char *pszOp, size_t cbPayload)
{
    AssertReturnVoid(strlen(pszOp) >= 2);
    AssertReturnVoid(strlen(pszOp) <= ATSPKT_OPCODE_MAX_LEN);

    /** @todo Validate opcode. */

    RT_BZERO(pReqHdr, sizeof(ATSPKTHDR));

    memcpy(pReqHdr->achOpcode, pszOp, strlen(pszOp));
    pReqHdr->uCrc32 = 0; /** @todo Do CRC-32 calculation. */
    pReqHdr->cb     = (uint32_t)cbReq + (uint32_t)cbPayload;

    Assert(pReqHdr->cb <= ATSPKT_MAX_SIZE);
}

/**
 * Sends an acknowledege response back to the server.
 *
 * @returns VBox status code.
 * @param   pClient             Client to send command for.
 */
static int audioTestSvcClientSendAck(PATSCLIENT pClient)
{
    ATSPKTHDR Req;
    audioTestSvcClientReqHdrInit(&Req, sizeof(Req), "ACK     ", 0);

    return audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
}

/**
 * Sends a greeting command (handshake) to an ATS server.
 *
 * @returns VBox status code.
 * @param   pClient             Client to send command for.
 */
static int audioTestSvcClientDoGreet(PATSCLIENT pClient)
{
    ATSPKTREQHOWDY Req;
    Req.uVersion = ATS_PROTOCOL_VS;
    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_HOWDY, 0);
    int rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);
    return rc;
}

/**
 * Tells the ATS server that we want to disconnect.
 *
 * @returns VBox status code.
 * @param   pClient             Client to disconnect.
 */
static int audioTestSvcClientDoBye(PATSCLIENT pClient)
{
    ATSPKTHDR Req;
    audioTestSvcClientReqHdrInit(&Req, sizeof(Req), ATSPKT_OPCODE_BYE, 0);
    int rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);
    return rc;
}

/**
 * Creates an ATS client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to create.
 */
int AudioTestSvcClientCreate(PATSCLIENT pClient)
{
    audioTestSvcClientInit(pClient);

    /*
     * The default transporter is the first one.
     */
    pClient->pTransport = g_apTransports[0]; /** @todo Make this dynamic. */

    return pClient->pTransport->pfnCreate(&pClient->pTransportInst);
}

/**
 * Destroys an ATS client.
 *
 * @param   pClient             Client to destroy.
 */
void AudioTestSvcClientDestroy(PATSCLIENT pClient)
{
    if (!pClient)
        return;

    /* ignore rc */ audioTestSvcClientDisconnectInternal(pClient);

    if (pClient->pTransport)
    {
        pClient->pTransport->pfnDestroy(pClient->pTransportInst);
        pClient->pTransportInst = NULL; /* Invalidate pointer. */
    }
}

/**
 * Handles a command line option.
 *
 * @returns VBox status code.
 * @param   pClient             Client to handle option for.
 * @param   ch                  Option (short) to handle.
 * @param   pVal                Option union to store the result in on success.
 */
int AudioTestSvcClientHandleOption(PATSCLIENT pClient, int ch, PCRTGETOPTUNION pVal)
{
    AssertPtrReturn(pClient->pTransport, VERR_WRONG_ORDER); /* Must be created first via AudioTestSvcClientCreate(). */
    if (!pClient->pTransport->pfnOption)
        return VERR_GETOPT_UNKNOWN_OPTION;
    return pClient->pTransport->pfnOption(pClient->pTransportInst, ch, pVal);
}

/**
 * Connects to an ATS peer, extended version.
 *
 * @returns VBox status code.
 * @param   pClient             Client to connect.
 * @param   msTimeout           Timeout (in ms) waiting for a connection to be established.
 *                              Use RT_INDEFINITE_WAIT to wait indefinitely.
 */
int AudioTestSvcClientConnectEx(PATSCLIENT pClient, RTMSINTERVAL msTimeout)
{
    if (pClient->pTransportClient)
        return VERR_NET_ALREADY_CONNECTED;

    int rc = pClient->pTransport->pfnStart(pClient->pTransportInst);
    if (RT_SUCCESS(rc))
    {
        rc = pClient->pTransport->pfnWaitForConnect(pClient->pTransportInst,
                                                    msTimeout, NULL /* pfFromServer */, &pClient->pTransportClient);
        if (RT_SUCCESS(rc))
        {
            rc = audioTestSvcClientDoGreet(pClient);
        }
    }

    if (RT_FAILURE(rc))
        LogRelFunc(("Connecting to server (%RU32ms timeout) failed with %Rrc\n", msTimeout, rc));

    return rc;
}

/**
 * Connects to an ATS peer.
 *
 * @returns VBox status code.
 * @param   pClient             Client to connect.
 */
int AudioTestSvcClientConnect(PATSCLIENT pClient)
{
    return AudioTestSvcClientConnectEx(pClient, 30 * 1000 /* msTimeout */);
}

/**
 * Tells the server to begin a new test set.
 *
 * @returns VBox status code.
 * @param   pClient             Client to issue command for.
 * @param   pszTag              Tag to use for the test set to begin.
 */
int AudioTestSvcClientTestSetBegin(PATSCLIENT pClient, const char *pszTag)
{
    ATSPKTREQTSETBEG Req;

    int rc = RTStrCopy(Req.szTag, sizeof(Req.szTag), pszTag);
    AssertRCReturn(rc, rc);

    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_TESTSET_BEGIN, 0);

    rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);

    return rc;
}

/**
 * Tells the server to end a runing test set.
 *
 * @returns VBox status code.
 * @param   pClient             Client to issue command for.
 * @param   pszTag              Tag of test set to end.
 */
int AudioTestSvcClientTestSetEnd(PATSCLIENT pClient, const char *pszTag)
{
    ATSPKTREQTSETEND Req;

    int rc = RTStrCopy(Req.szTag, sizeof(Req.szTag), pszTag);
    AssertRCReturn(rc, rc);

    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_TESTSET_END, 0);

    rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);

    return rc;
}

/**
 * Tells the server to play a (test) tone.
 *
 * @returns VBox status code.
 * @param   pClient             Client to issue command for.
 * @param   pToneParms          Tone parameters to use.
 * @note    How (and if) the server plays a tone depends on the actual implementation side.
 */
int AudioTestSvcClientTonePlay(PATSCLIENT pClient, PAUDIOTESTTONEPARMS pToneParms)
{
    ATSPKTREQTONEPLAY Req;

    memcpy(&Req.ToneParms, pToneParms, sizeof(AUDIOTESTTONEPARMS));

    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_TONE_PLAY, 0);

    int rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);

    return rc;
}

/**
 * Tells the server to record a (test) tone.
 *
 * @returns VBox status code.
 * @param   pClient             Client to issue command for.
 * @param   pToneParms          Tone parameters to use.
 * @note    How (and if) the server plays a tone depends on the actual implementation side.
 */
int AudioTestSvcClientToneRecord(PATSCLIENT pClient, PAUDIOTESTTONEPARMS pToneParms)
{
    ATSPKTREQTONEREC Req;

    memcpy(&Req.ToneParms, pToneParms, sizeof(AUDIOTESTTONEPARMS));

    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_TONE_RECORD, 0);

    int rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = audioTestSvcClientRecvAck(pClient);

    return rc;
}

/**
 * Tells the server to send (download) a (packed up) test set archive.
 * The test set must not be running / open anymore.
 *
 * @returns VBox status code.
 * @param   pClient             Client to issue command for.
 * @param   pszTag              Tag of test set to send.
 * @param   pszPathOutAbs       Absolute path where to store the downloaded test set archive.
 */
int AudioTestSvcClientTestSetDownload(PATSCLIENT pClient, const char *pszTag, const char *pszPathOutAbs)
{
    ATSPKTREQTSETSND Req;

    int rc = RTStrCopy(Req.szTag, sizeof(Req.szTag), pszTag);
    AssertRCReturn(rc, rc);

    audioTestSvcClientReqHdrInit(&Req.Hdr, sizeof(Req), ATSPKT_OPCODE_TESTSET_SEND, 0);

    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszPathOutAbs, RTFILE_O_WRITE | RTFILE_O_CREATE | RTFILE_O_DENY_WRITE);
    AssertRCReturn(rc, rc);

    rc = audioTestSvcClientSendMsg(pClient, &Req, sizeof(Req));
    while (RT_SUCCESS(rc))
    {
        ATSSRVREPLY Reply;
        RT_ZERO(Reply);

        rc = audioTestSvcClientRecvReply(pClient, &Reply, false /* fNoDataOk */);
        if (RT_SUCCESS(rc))
        {
            /* Extract received CRC32 checksum. */
            const size_t cbCrc32 = sizeof(uint32_t); /* Skip CRC32 in payload for actual CRC verification. */

            uint32_t uSrcCrc32;
            memcpy(&uSrcCrc32, Reply.pvPayload, cbCrc32);

            if (uSrcCrc32)
            {
                const uint32_t uDstCrc32 = RTCrc32((uint8_t *)Reply.pvPayload + cbCrc32, Reply.cbPayload - cbCrc32);

                Log2Func(("uSrcCrc32=%#x, cbRead=%zu -> uDstCrc32=%#x\n"
                          "%.*Rhxd\n",
                          uSrcCrc32, Reply.cbPayload - cbCrc32, uDstCrc32,
                          RT_MIN(64, Reply.cbPayload - cbCrc32), (uint8_t *)Reply.pvPayload + cbCrc32));

                if (uSrcCrc32 != uDstCrc32)
                    rc = VERR_TAR_CHKSUM_MISMATCH; /** @todo Fudge! */
            }

            if (RT_SUCCESS(rc))
            {
                if (   RTStrNCmp(Reply.szOp, "DATA    ", ATSPKT_OPCODE_MAX_LEN) == 0
                    && Reply.pvPayload
                    && Reply.cbPayload)
                {
                    rc = RTFileWrite(hFile, (uint8_t *)Reply.pvPayload + cbCrc32, Reply.cbPayload - cbCrc32, NULL);
                }
                else if (RTStrNCmp(Reply.szOp, "DATA EOF", ATSPKT_OPCODE_MAX_LEN) == 0)
                {
                    rc = VINF_EOF;
                }
                else
                {
                    AssertMsgFailed(("Got unexpected reply '%s'", Reply.szOp));
                    rc = VERR_NOT_SUPPORTED;
                }
            }
        }

        audioTestSvcClientReplyDestroy(&Reply);

        int rc2 = audioTestSvcClientSendAck(pClient);
        if (rc == VINF_SUCCESS) /* Might be VINF_EOF already. */
            rc = rc2;

        if (rc == VINF_EOF)
            break;
    }

    int rc2 = RTFileClose(hFile);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

/**
 * Disconnects from an ATS server, internal version.
 *
 * @returns VBox status code.
 * @param   pClient             Client to disconnect.
 */
static int audioTestSvcClientDisconnectInternal(PATSCLIENT pClient)
{
    if (!pClient->pTransportClient) /* Not connected (yet)? Bail out early. */
        return VINF_SUCCESS;

    int rc = audioTestSvcClientDoBye(pClient);
    if (RT_SUCCESS(rc))
    {
        if (pClient->pTransport->pfnNotifyBye)
            pClient->pTransport->pfnNotifyBye(pClient->pTransportInst, pClient->pTransportClient);

        pClient->pTransport->pfnDisconnect(pClient->pTransportInst, pClient->pTransportClient);
        pClient->pTransportClient = NULL;

        pClient->pTransport->pfnStop(pClient->pTransportInst);
    }

    return rc;
}

/**
 * Disconnects from an ATS server.
 *
 * @returns VBox status code.
 * @param   pClient             Client to disconnect.
 */
int AudioTestSvcClientDisconnect(PATSCLIENT pClient)
{
    return audioTestSvcClientDisconnectInternal(pClient);
}

