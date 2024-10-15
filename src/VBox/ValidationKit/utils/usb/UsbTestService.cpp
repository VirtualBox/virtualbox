/* $Id: UsbTestService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestService - Remote USB test configuration and execution server.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/initterm.h>
#include <iprt/json.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "UsbTestServiceInternal.h"
#include "UsbTestServiceGadget.h"
#include "UsbTestServicePlatform.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#define UTS_USBIP_PORT_FIRST 3240
#define UTS_USBIP_PORT_LAST  3340

/**
 * UTS client state.
 */
typedef enum UTSCLIENTSTATE
{
    /** Invalid client state. */
    UTSCLIENTSTATE_INVALID = 0,
    /** Client is initialising, only the HOWDY and BYE packets are allowed. */
    UTSCLIENTSTATE_INITIALISING,
    /** Client is in fully cuntional state and ready to process all requests. */
    UTSCLIENTSTATE_READY,
    /** Client is destroying. */
    UTSCLIENTSTATE_DESTROYING,
    /** 32bit hack. */
    UTSCLIENTSTATE_32BIT_HACK = 0x7fffffff
} UTSCLIENTSTATE;

/**
 * UTS client instance.
 */
typedef struct UTSCLIENT
{
    /** List node for new clients. */
    RTLISTNODE             NdLst;
    /** The current client state. */
    UTSCLIENTSTATE         enmState;
    /** Transport backend specific data. */
    PUTSTRANSPORTCLIENT    pTransportClient;
    /** Client hostname. */
    char                  *pszHostname;
    /** Gadget host handle. */
    UTSGADGETHOST          hGadgetHost;
    /** Handle fo the current configured gadget. */
    UTSGADGET              hGadget;
} UTSCLIENT;
/** Pointer to a UTS client instance. */
typedef UTSCLIENT *PUTSCLIENT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Transport layers.
 */
static const PCUTSTRANSPORT g_apTransports[] =
{
    &g_TcpTransport,
    //&g_SerialTransport,
    //&g_FileSysTransport,
    //&g_GuestPropTransport,
    //&g_TestDevTransport,
};

/** The select transport layer. */
static PCUTSTRANSPORT       g_pTransport;
/** The config path. */
static char                 g_szCfgPath[RTPATH_MAX];
/** The scratch path. */
static char                 g_szScratchPath[RTPATH_MAX];
/** The default scratch path. */
static char                 g_szDefScratchPath[RTPATH_MAX];
/** The CD/DVD-ROM path. */
static char                 g_szCdRomPath[RTPATH_MAX];
/** The default CD/DVD-ROM path. */
static char                 g_szDefCdRomPath[RTPATH_MAX];
/** The operating system short name. */
static char                 g_szOsShortName[16];
/** The CPU architecture short name. */
static char                 g_szArchShortName[16];
/** The combined "OS.arch" name. */
static char                 g_szOsDotArchShortName[32];
/** The combined "OS/arch" name. */
static char                 g_szOsSlashArchShortName[32];
/** The executable suffix. */
static char                 g_szExeSuff[8];
/** The shell script suffix. */
static char                 g_szScriptSuff[8];
/** Whether to display the output of the child process or not.  */
static bool                 g_fDisplayOutput = true;
/** Whether to terminate or not.
 * @todo implement signals and stuff.  */
static bool volatile        g_fTerminate = false;
/** Configuration AST. */
static RTJSONVAL            g_hCfgJson = NIL_RTJSONVAL;
/** Pipe for communicating with the serving thread about new clients. - read end */
static RTPIPE               g_hPipeR;
/** Pipe for communicating with the serving thread about new clients. - write end */
static RTPIPE               g_hPipeW;
/** Thread serving connected clients. */
static RTTHREAD             g_hThreadServing;
/** Critical section protecting the list of new clients. */
static RTCRITSECT           g_CritSectClients;
/** List of new clients waiting to be picked up by the client worker thread. */
static RTLISTANCHOR         g_LstClientsNew;
/** First USB/IP port we can use. */
static uint16_t             g_uUsbIpPortFirst = UTS_USBIP_PORT_FIRST;
/** Last USB/IP port we can use. */
static uint16_t             g_uUsbIpPortLast  = UTS_USBIP_PORT_LAST;
/** Next free port. */
static uint16_t             g_uUsbIpPortNext  = UTS_USBIP_PORT_FIRST;



/**
 * Returns the string represenation of the given state.
 */
static const char *utsClientStateStringify(UTSCLIENTSTATE enmState)
{
    switch (enmState)
    {
        case UTSCLIENTSTATE_INVALID:
            return "INVALID";
        case UTSCLIENTSTATE_INITIALISING:
            return "INITIALISING";
        case UTSCLIENTSTATE_READY:
            return "READY";
        case UTSCLIENTSTATE_DESTROYING:
            return "DESTROYING";
        case UTSCLIENTSTATE_32BIT_HACK:
        default:
            break;
    }

    AssertMsgFailed(("Unknown state %#x\n", enmState));
    return "UNKNOWN";
}

/**
 * Calculates the checksum value, zero any padding space and send the packet.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPkt                The packet to send.  Must point to a correctly
 *                              aligned buffer.
 */
static int utsSendPkt(PUTSCLIENT pClient, PUTSPKTHDR pPkt)
{
    Assert(pPkt->cb >= sizeof(*pPkt));
    pPkt->uCrc32 = RTCrc32(pPkt->achOpcode, pPkt->cb - RT_UOFFSETOF(UTSPKTHDR, achOpcode));
    if (pPkt->cb != RT_ALIGN_32(pPkt->cb, UTSPKT_ALIGNMENT))
        memset((uint8_t *)pPkt + pPkt->cb, '\0', RT_ALIGN_32(pPkt->cb, UTSPKT_ALIGNMENT) - pPkt->cb);

    Log(("utsSendPkt: cb=%#x opcode=%.8s\n", pPkt->cb, pPkt->achOpcode));
    Log2(("%.*Rhxd\n", RT_MIN(pPkt->cb, 256), pPkt));
    int rc = g_pTransport->pfnSendPkt(pClient->pTransportClient, pPkt);
    while (RT_UNLIKELY(rc == VERR_INTERRUPTED) && !g_fTerminate)
        rc = g_pTransport->pfnSendPkt(pClient->pTransportClient, pPkt);
    if (RT_FAILURE(rc))
        Log(("utsSendPkt: rc=%Rrc\n", rc));

    return rc;
}

/**
 * Sends a babble reply and disconnects the client (if applicable).
 *
 * @param   pClient             The UTS client structure.
 * @param   pszOpcode           The BABBLE opcode.
 */
static void utsReplyBabble(PUTSCLIENT pClient, const char *pszOpcode)
{
    UTSPKTHDR Reply;
    Reply.cb     = sizeof(Reply);
    Reply.uCrc32 = 0;
    memcpy(Reply.achOpcode, pszOpcode, sizeof(Reply.achOpcode));

    g_pTransport->pfnBabble(pClient->pTransportClient, &Reply, 20*1000);
}

/**
 * Receive and validate a packet.
 *
 * Will send bable responses to malformed packets that results in a error status
 * code.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   ppPktHdr            Where to return the packet on success.  Free
 *                              with RTMemFree.
 * @param   fAutoRetryOnFailure Whether to retry on error.
 */
static int utsRecvPkt(PUTSCLIENT pClient, PPUTSPKTHDR ppPktHdr, bool fAutoRetryOnFailure)
{
    for (;;)
    {
        PUTSPKTHDR pPktHdr;
        int rc = g_pTransport->pfnRecvPkt(pClient->pTransportClient, &pPktHdr);
        if (RT_SUCCESS(rc))
        {
            /* validate the packet. */
            if (   pPktHdr->cb >= sizeof(UTSPKTHDR)
                && pPktHdr->cb < UTSPKT_MAX_SIZE)
            {
                Log2(("utsRecvPkt: pPktHdr=%p cb=%#x crc32=%#x opcode=%.8s\n"
                      "%.*Rhxd\n",
                      pPktHdr, pPktHdr->cb, pPktHdr->uCrc32, pPktHdr->achOpcode, RT_MIN(pPktHdr->cb, 256), pPktHdr));
                uint32_t uCrc32Calc = pPktHdr->uCrc32 != 0
                                    ? RTCrc32(&pPktHdr->achOpcode[0], pPktHdr->cb - RT_UOFFSETOF(UTSPKTHDR, achOpcode))
                                    : 0;
                if (pPktHdr->uCrc32 == uCrc32Calc)
                {
                    AssertCompileMemberSize(UTSPKTHDR, achOpcode, 8);
                    if (   RT_C_IS_UPPER(pPktHdr->achOpcode[0])
                        && RT_C_IS_UPPER(pPktHdr->achOpcode[1])
                        && (RT_C_IS_UPPER(pPktHdr->achOpcode[2]) || pPktHdr->achOpcode[2] == ' ')
                        && (RT_C_IS_PRINT(pPktHdr->achOpcode[3]) || pPktHdr->achOpcode[3] == ' ')
                        && (RT_C_IS_PRINT(pPktHdr->achOpcode[4]) || pPktHdr->achOpcode[4] == ' ')
                        && (RT_C_IS_PRINT(pPktHdr->achOpcode[5]) || pPktHdr->achOpcode[5] == ' ')
                        && (RT_C_IS_PRINT(pPktHdr->achOpcode[6]) || pPktHdr->achOpcode[6] == ' ')
                        && (RT_C_IS_PRINT(pPktHdr->achOpcode[7]) || pPktHdr->achOpcode[7] == ' ')
                       )
                    {
                        Log(("utsRecvPkt: cb=%#x opcode=%.8s\n", pPktHdr->cb, pPktHdr->achOpcode));
                        *ppPktHdr = pPktHdr;
                        return rc;
                    }

                    rc = VERR_IO_BAD_COMMAND;
                }
                else
                {
                    Log(("utsRecvPkt: cb=%#x opcode=%.8s crc32=%#x actual=%#x\n",
                         pPktHdr->cb, pPktHdr->achOpcode, pPktHdr->uCrc32, uCrc32Calc));
                    rc = VERR_IO_CRC;
                }
            }
            else
                rc = VERR_IO_BAD_LENGTH;

            /* Send babble reply and disconnect the client if the transport is
               connection oriented. */
            if (rc == VERR_IO_BAD_LENGTH)
                utsReplyBabble(pClient, "BABBLE L");
            else if (rc == VERR_IO_CRC)
                utsReplyBabble(pClient, "BABBLE C");
            else if (rc == VERR_IO_BAD_COMMAND)
                utsReplyBabble(pClient, "BABBLE O");
            else
                utsReplyBabble(pClient, "BABBLE  ");
            RTMemFree(pPktHdr);
        }

        /* Try again or return failure? */
        if (   g_fTerminate
            || rc != VERR_INTERRUPTED
            || !fAutoRetryOnFailure
            )
        {
            Log(("utsRecvPkt: rc=%Rrc\n", rc));
            return rc;
        }
    }
}

/**
 * Make a simple reply, only status opcode.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pReply              The reply packet.
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   cbExtra             Bytes in addition to the header.
 */
static int utsReplyInternal(PUTSCLIENT pClient, PUTSPKTSTS pReply, const char *pszOpcode, size_t cbExtra)
{
    /* copy the opcode, don't be too strict in case of a padding screw up. */
    size_t cchOpcode = strlen(pszOpcode);
    if (RT_LIKELY(cchOpcode == sizeof(pReply->Hdr.achOpcode)))
        memcpy(pReply->Hdr.achOpcode, pszOpcode, sizeof(pReply->Hdr.achOpcode));
    else
    {
        Assert(cchOpcode == sizeof(pReply->Hdr.achOpcode));
        while (cchOpcode > 0 && pszOpcode[cchOpcode - 1] == ' ')
            cchOpcode--;
        AssertMsgReturn(cchOpcode < sizeof(pReply->Hdr.achOpcode), ("%d/'%.8s'\n", cchOpcode, pszOpcode), VERR_INTERNAL_ERROR_4);
        memcpy(pReply->Hdr.achOpcode, pszOpcode, cchOpcode);
        memset(&pReply->Hdr.achOpcode[cchOpcode], ' ', sizeof(pReply->Hdr.achOpcode) - cchOpcode);
    }

    pReply->Hdr.cb     = (uint32_t)sizeof(UTSPKTSTS) + (uint32_t)cbExtra;
    pReply->Hdr.uCrc32 = 0;

    return utsSendPkt(pClient, &pReply->Hdr);
}

/**
 * Make a simple reply, only status opcode.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The original packet (for future use).
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 */
static int utsReplySimple(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, const char *pszOpcode)
{
    UTSPKTSTS Pkt;

    RT_ZERO(Pkt);
    Pkt.rcReq = VINF_SUCCESS;
    Pkt.cchStsMsg = 0;
    NOREF(pPktHdr);
    return utsReplyInternal(pClient, &Pkt, pszOpcode, 0);
}

/**
 * Acknowledges a packet with success.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The original packet (for future use).
 */
static int utsReplyAck(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    return utsReplySimple(pClient, pPktHdr, "ACK     ");
}

/**
 * Replies with a failure.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The original packet (for future use).
 * @param   rcReq               Status code.
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   rcReq               The status code of the request.
 * @param   pszDetailFmt        Longer description of the problem (format string).
 * @param   va                  Format arguments.
 */
static int utsReplyFailureV(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, const char *pszOpcode, int rcReq, const char *pszDetailFmt, va_list va)
{
    NOREF(pPktHdr);
    union
    {
        UTSPKTSTS   Hdr;
        char        ach[256];
    } uPkt;

    RT_ZERO(uPkt);
    size_t cchDetail = RTStrPrintfV(&uPkt.ach[sizeof(UTSPKTSTS)],
                                    sizeof(uPkt) - sizeof(UTSPKTSTS),
                                    pszDetailFmt, va);
    uPkt.Hdr.rcReq = rcReq;
    uPkt.Hdr.cchStsMsg = cchDetail;
    return utsReplyInternal(pClient, &uPkt.Hdr, pszOpcode, cchDetail + 1);
}

/**
 * Replies with a failure.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The original packet (for future use).
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   rcReq               Status code.
 * @param   pszDetailFmt        Longer description of the problem (format string).
 * @param   ...                 Format arguments.
 */
static int utsReplyFailure(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, const char *pszOpcode, int rcReq, const char *pszDetailFmt, ...)
{
    va_list va;
    va_start(va, pszDetailFmt);
    int rc = utsReplyFailureV(pClient, pPktHdr, pszOpcode, rcReq, pszDetailFmt, va);
    va_end(va);
    return rc;
}

/**
 * Replies according to the return code.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet to reply to.
 * @param   rcOperation         The status code to report.
 * @param   pszOperationFmt     The operation that failed.  Typically giving the
 *                              function call with important arguments.
 * @param   ...                 Arguments to the format string.
 */
static int utsReplyRC(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, int rcOperation, const char *pszOperationFmt, ...)
{
    if (RT_SUCCESS(rcOperation))
        return utsReplyAck(pClient, pPktHdr);

    char    szOperation[128];
    va_list va;
    va_start(va, pszOperationFmt);
    RTStrPrintfV(szOperation, sizeof(szOperation), pszOperationFmt, va);
    va_end(va);

    return utsReplyFailure(pClient, pPktHdr, "FAILED  ", rcOperation, "%s failed with rc=%Rrc (opcode '%.8s')",
                           szOperation, rcOperation, pPktHdr->achOpcode);
}

#if 0 /* unused */
/**
 * Signal a bad packet minum size.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet to reply to.
 * @param   cbMin               The minimum size.
 */
static int utsReplyBadMinSize(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, size_t cbMin)
{
    return utsReplyFailure(pClient, pPktHdr, "BAD SIZE", VERR_INVALID_PARAMETER, "Expected at least %zu bytes, got %u (opcode '%.8s')",
                           cbMin, pPktHdr->cb, pPktHdr->achOpcode);
}
#endif

/**
 * Signal a bad packet exact size.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet to reply to.
 * @param   cb                  The wanted size.
 */
static int utsReplyBadSize(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr, size_t cb)
{
    return utsReplyFailure(pClient, pPktHdr, "BAD SIZE", VERR_INVALID_PARAMETER, "Expected at %zu bytes, got %u  (opcode '%.8s')",
                           cb, pPktHdr->cb, pPktHdr->achOpcode);
}

#if 0 /* unused */
/**
 * Deals with a command that isn't implemented yet.
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet which opcode isn't implemented.
 */
static int utsReplyNotImplemented(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    return utsReplyFailure(pClient, pPktHdr, "NOT IMPL", VERR_NOT_IMPLEMENTED, "Opcode '%.8s' is not implemented", pPktHdr->achOpcode);
}
#endif

/**
 * Deals with a unknown command.
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet to reply to.
 */
static int utsReplyUnknown(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    return utsReplyFailure(pClient, pPktHdr, "UNKNOWN ", VERR_NOT_FOUND, "Opcode '%.8s' is not known", pPktHdr->achOpcode);
}

/**
 * Deals with a command which contains an unterminated string.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet containing the unterminated string.
 */
static int utsReplyBadStrTermination(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    return utsReplyFailure(pClient, pPktHdr, "BAD TERM", VERR_INVALID_PARAMETER, "Opcode '%.8s' contains an unterminated string", pPktHdr->achOpcode);
}

/**
 * Deals with a command sent in an invalid client state.
 *
 * @returns IPRT status code of the send.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The packet containing the unterminated string.
 */
static int utsReplyInvalidState(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    return utsReplyFailure(pClient, pPktHdr, "INVSTATE", VERR_INVALID_STATE, "Opcode '%.8s' is not supported at client state '%s",
                           pPktHdr->achOpcode, utsClientStateStringify(pClient->enmState));
}

/**
 * Parses an unsigned integer from the given value string.
 *
 * @returns IPRT status code.
 * @retval  VERR_OUT_OF_RANGE if the parsed value exceeds the given maximum.
 * @param   pszVal              The value string.
 * @param   uMax                The maximum value.
 * @param   pu64                Where to store the parsed number on success.
 */
static int utsDoGadgetCreateCfgParseUInt(const char *pszVal, uint64_t uMax, uint64_t *pu64)
{
    int rc = RTStrToUInt64Ex(pszVal, NULL, 0, pu64);
    if (RT_SUCCESS(rc))
    {
        if (*pu64 > uMax)
            rc = VERR_OUT_OF_RANGE;
    }

    return rc;
}

/**
 * Parses a signed integer from the given value string.
 *
 * @returns IPRT status code.
 * @retval  VERR_OUT_OF_RANGE if the parsed value exceeds the given range.
 * @param   pszVal              The value string.
 * @param   iMin                The minimum value.
 * @param   iMax                The maximum value.
 * @param   pi64                Where to store the parsed number on success.
 */
static int utsDoGadgetCreateCfgParseInt(const char *pszVal, int64_t iMin, int64_t iMax, int64_t *pi64)
{
    int rc = RTStrToInt64Ex(pszVal, NULL, 0, pi64);
    if (RT_SUCCESS(rc))
    {
        if (   *pi64 < iMin
            || *pi64 > iMax)
            rc = VERR_OUT_OF_RANGE;
    }

    return rc;
}

/**
 * Parses the given config item and fills in the value according to the given type.
 *
 * @returns IPRT status code.
 * @param   pCfgItem            The config item to parse.
 * @param   u32Type             The config type.
 * @param   pszVal              The value encoded as a string.
 */
static int utsDoGadgetCreateCfgParseItem(PUTSGADGETCFGITEM pCfgItem, uint32_t u32Type,
                                         const char *pszVal)
{
    int rc = VINF_SUCCESS;

    switch (u32Type)
    {
        case UTSPKT_GDGT_CFG_ITEM_TYPE_BOOLEAN:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_BOOLEAN;
            if (   RTStrICmp(pszVal, "enabled")
                || RTStrICmp(pszVal, "1")
                || RTStrICmp(pszVal, "true"))
                pCfgItem->Val.u.f = true;
            else if (   RTStrICmp(pszVal, "disabled")
                     || RTStrICmp(pszVal, "0")
                     || RTStrICmp(pszVal, "false"))
                pCfgItem->Val.u.f = false;
            else
                rc = VERR_INVALID_PARAMETER;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_STRING:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_STRING;
            pCfgItem->Val.u.psz = RTStrDup(pszVal);
            if (!pCfgItem->Val.u.psz)
                rc = VERR_NO_STR_MEMORY;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_UINT8:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_UINT8;

            uint64_t u64;
            rc = utsDoGadgetCreateCfgParseUInt(pszVal, UINT8_MAX, &u64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.u8 = (uint8_t)u64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_UINT16:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_UINT16;

            uint64_t u64;
            rc = utsDoGadgetCreateCfgParseUInt(pszVal, UINT16_MAX, &u64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.u16 = (uint16_t)u64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_UINT32:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_UINT32;

            uint64_t u64;
            rc = utsDoGadgetCreateCfgParseUInt(pszVal, UINT32_MAX, &u64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.u32 = (uint32_t)u64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_UINT64:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_UINT64;
            rc = utsDoGadgetCreateCfgParseUInt(pszVal, UINT64_MAX, &pCfgItem->Val.u.u64);
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_INT8:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_INT8;

            int64_t i64;
            rc = utsDoGadgetCreateCfgParseInt(pszVal, INT8_MIN, INT8_MAX, &i64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.i8 = (int8_t)i64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_INT16:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_INT16;

            int64_t i64;
            rc = utsDoGadgetCreateCfgParseInt(pszVal, INT16_MIN, INT16_MAX, &i64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.i16 = (int16_t)i64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_INT32:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_INT32;

            int64_t i64;
            rc = utsDoGadgetCreateCfgParseInt(pszVal, INT32_MIN, INT32_MAX, &i64);
            if (RT_SUCCESS(rc))
                pCfgItem->Val.u.i32 = (int32_t)i64;
            break;
        }
        case UTSPKT_GDGT_CFG_ITEM_TYPE_INT64:
        {
            pCfgItem->Val.enmType = UTSGADGETCFGTYPE_INT64;
            rc = utsDoGadgetCreateCfgParseInt(pszVal, INT64_MIN, INT64_MAX, &pCfgItem->Val.u.i64);
            break;
        }
        default:
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/**
 * Creates the configuration from the given GADGET CREATE packet.
 *
 * @returns IPRT status code.
 * @param   pCfgItem            The first config item header in the request packet.
 * @param   cCfgItems           Number of config items in the packet to parse.
 * @param   cbPkt               Number of bytes left in the packet for the config data.
 * @param   paCfg               The array of configuration items to fill.
 */
static int utsDoGadgetCreateFillCfg(PUTSPKTREQGDGTCTORCFGITEM pCfgItem, unsigned cCfgItems,
                                    size_t cbPkt, PUTSGADGETCFGITEM paCfg)
{
    int rc = VINF_SUCCESS;
    unsigned idxCfg = 0;

    while (   RT_SUCCESS(rc)
           && cCfgItems
           && cbPkt)
    {
        if (cbPkt >= sizeof(UTSPKTREQGDGTCTORCFGITEM))
        {
            cbPkt -= sizeof(UTSPKTREQGDGTCTORCFGITEM);
            if (pCfgItem->u32KeySize + pCfgItem->u32ValSize >= cbPkt)
            {
                const char *pszKey = (const char *)(pCfgItem + 1);
                const char *pszVal = pszKey + pCfgItem->u32KeySize;

                /* Validate termination. */
                if (   *(pszKey + pCfgItem->u32KeySize - 1) != '\0'
                    || *(pszVal + pCfgItem->u32ValSize - 1) != '\0')
                    rc = VERR_INVALID_PARAMETER;
                else
                {
                    paCfg[idxCfg].pszKey = RTStrDup(pszKey);

                    rc = utsDoGadgetCreateCfgParseItem(&paCfg[idxCfg], pCfgItem->u32Type, pszVal);
                    if (RT_SUCCESS(rc))
                    {
                        cbPkt -= pCfgItem->u32KeySize + pCfgItem->u32ValSize;
                        cCfgItems--;
                        idxCfg++;
                        pCfgItem = (PUTSPKTREQGDGTCTORCFGITEM)(pszVal + pCfgItem->u32ValSize);
                    }
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/**
 * Verifies and acknowledges a "BYE" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The howdy packet.
 */
static int utsDoBye(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    int rc;
    if (pPktHdr->cb == sizeof(UTSPKTHDR))
        rc = utsReplyAck(pClient, pPktHdr);
    else
        rc = utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTHDR));
    return rc;
}

/**
 * Verifies and acknowledges a "HOWDY" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The howdy packet.
 */
static int utsDoHowdy(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    int rc = VINF_SUCCESS;

    if (pPktHdr->cb != sizeof(UTSPKTREQHOWDY))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTREQHOWDY));

    if (pClient->enmState != UTSCLIENTSTATE_INITIALISING)
        return utsReplyInvalidState(pClient, pPktHdr);

    PUTSPKTREQHOWDY pReq = (PUTSPKTREQHOWDY)pPktHdr;

    if (pReq->uVersion != UTS_PROTOCOL_VS)
        return utsReplyRC(pClient, pPktHdr, VERR_VERSION_MISMATCH, "The given version %#x is not supported", pReq->uVersion);

    /* Verify hostname string. */
    if (pReq->cchHostname >= sizeof(pReq->achHostname))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(pReq->achHostname) - 1);

    if (pReq->achHostname[pReq->cchHostname] != '\0')
        return utsReplyBadStrTermination(pClient, pPktHdr);

    /* Extract string. */
    pClient->pszHostname = RTStrDup(&pReq->achHostname[0]);
    if (!pClient->pszHostname)
        return utsReplyRC(pClient, pPktHdr, VERR_NO_MEMORY, "Failed to allocate memory for the hostname string");

    if (pReq->fUsbConn & UTSPKT_HOWDY_CONN_F_PHYSICAL)
        return utsReplyRC(pClient, pPktHdr, VERR_NOT_SUPPORTED, "Physical connections are not yet supported");

    if (pReq->fUsbConn & UTSPKT_HOWDY_CONN_F_USBIP)
    {
        /* Set up the USB/IP server, find an unused port we can start the server on. */
        UTSGADGETCFGITEM aCfg[2];

        uint16_t uPort = g_uUsbIpPortNext;

        if (g_uUsbIpPortNext == g_uUsbIpPortLast)
            g_uUsbIpPortNext = g_uUsbIpPortFirst;
        else
            g_uUsbIpPortNext++;

        aCfg[0].pszKey      = "UsbIp/Port";
        aCfg[0].Val.enmType = UTSGADGETCFGTYPE_UINT16;
        aCfg[0].Val.u.u16   = uPort;
        aCfg[1].pszKey      = NULL;

        rc = utsGadgetHostCreate(UTSGADGETHOSTTYPE_USBIP, &aCfg[0], &pClient->hGadgetHost);
        if (RT_SUCCESS(rc))
        {
            /* Send the reply with the configured USB/IP port. */
            UTSPKTREPHOWDY Rep;

            RT_ZERO(Rep);

            Rep.uVersion         = UTS_PROTOCOL_VS;
            Rep.fUsbConn         = UTSPKT_HOWDY_CONN_F_USBIP;
            Rep.uUsbIpPort       = uPort;
            Rep.cUsbIpDevices    = 1;
            Rep.cPhysicalDevices = 0;

            rc = utsReplyInternal(pClient, &Rep.Sts, "ACK     ", sizeof(Rep) - sizeof(UTSPKTSTS));
            if (RT_SUCCESS(rc))
            {
                g_pTransport->pfnNotifyHowdy(pClient->pTransportClient);
                pClient->enmState = UTSCLIENTSTATE_READY;
                RTDirRemoveRecursive(g_szScratchPath, RTDIRRMREC_F_CONTENT_ONLY);
            }
        }
        else
            return utsReplyRC(pClient, pPktHdr, rc, "Creating the USB/IP gadget host failed");
    }
    else
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_PARAMETER, "No access method requested");

    return rc;
}

/**
 * Verifies and processes a "GADGET CREATE" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The gadget create packet.
 */
static int utsDoGadgetCreate(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    int rc = VINF_SUCCESS;

    if (pPktHdr->cb < sizeof(UTSPKTREQGDGTCTOR))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTREQGDGTCTOR));

    if (   pClient->enmState != UTSCLIENTSTATE_READY
        || pClient->hGadgetHost == NIL_UTSGADGETHOST)
        return utsReplyInvalidState(pClient, pPktHdr);

    PUTSPKTREQGDGTCTOR pReq = (PUTSPKTREQGDGTCTOR)pPktHdr;

    if (pReq->u32GdgtType != UTSPKT_GDGT_CREATE_TYPE_TEST)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_PARAMETER, "The given gadget type is not supported");

    if (pReq->u32GdgtAccess != UTSPKT_GDGT_CREATE_ACCESS_USBIP)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_PARAMETER, "The given gadget access method is not supported");

    PUTSGADGETCFGITEM paCfg = NULL;
    if (pReq->u32CfgItems > 0)
    {
        paCfg = (PUTSGADGETCFGITEM)RTMemAllocZ((pReq->u32CfgItems + 1) * sizeof(UTSGADGETCFGITEM));
        if (RT_UNLIKELY(!paCfg))
            return utsReplyRC(pClient, pPktHdr, VERR_NO_MEMORY, "Failed to allocate memory for configration items");

        rc = utsDoGadgetCreateFillCfg((PUTSPKTREQGDGTCTORCFGITEM)(pReq + 1), pReq->u32CfgItems,
                                      pPktHdr->cb - sizeof(UTSPKTREQGDGTCTOR), paCfg);
        if (RT_FAILURE(rc))
        {
            RTMemFree(paCfg);
            return utsReplyRC(pClient, pPktHdr, rc, "Failed to parse configuration");
        }
    }

    rc = utsGadgetCreate(pClient->hGadgetHost, UTSGADGETCLASS_TEST, paCfg, &pClient->hGadget);
    if (RT_SUCCESS(rc))
    {
        UTSPKTREPGDGTCTOR Rep;
        RT_ZERO(Rep);

        Rep.idGadget = 0;
        Rep.u32BusId = utsGadgetGetBusId(pClient->hGadget);
        Rep.u32DevId = utsGadgetGetDevId(pClient->hGadget);
        rc = utsReplyInternal(pClient, &Rep.Sts, "ACK     ", sizeof(Rep) - sizeof(UTSPKTSTS));
    }
    else
        rc = utsReplyRC(pClient, pPktHdr, rc, "Failed to create gadget with %Rrc\n", rc);

    return rc;
}

/**
 * Verifies and processes a "GADGET DESTROY" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The gadget destroy packet.
 */
static int utsDoGadgetDestroy(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(UTSPKTREQGDGTDTOR))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTREQGDGTDTOR));

    if (   pClient->enmState != UTSCLIENTSTATE_READY
        || pClient->hGadgetHost == NIL_UTSGADGETHOST)
        return utsReplyInvalidState(pClient, pPktHdr);

    PUTSPKTREQGDGTDTOR pReq = (PUTSPKTREQGDGTDTOR)pPktHdr;

    if (pReq->idGadget != 0)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_HANDLE, "The given gadget handle is invalid");
    if (pClient->hGadget == NIL_UTSGADGET)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_STATE, "The gadget is not set up");

    utsGadgetRelease(pClient->hGadget);
    pClient->hGadget = NIL_UTSGADGET;

    return utsReplyAck(pClient, pPktHdr);
}

/**
 * Verifies and processes a "GADGET CONNECT" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The gadget connect packet.
 */
static int utsDoGadgetConnect(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(UTSPKTREQGDGTCNCT))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTREQGDGTCNCT));

    if (   pClient->enmState != UTSCLIENTSTATE_READY
        || pClient->hGadgetHost == NIL_UTSGADGETHOST)
        return utsReplyInvalidState(pClient, pPktHdr);

    PUTSPKTREQGDGTCNCT pReq = (PUTSPKTREQGDGTCNCT)pPktHdr;

    if (pReq->idGadget != 0)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_HANDLE, "The given gadget handle is invalid");
    if (pClient->hGadget == NIL_UTSGADGET)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_STATE, "The gadget is not set up");

    int rc = utsGadgetConnect(pClient->hGadget);
    if (RT_SUCCESS(rc))
        rc = utsReplyAck(pClient, pPktHdr);
    else
        rc = utsReplyRC(pClient, pPktHdr, rc, "Failed to connect the gadget");

    return rc;
}

/**
 * Verifies and processes a "GADGET DISCONNECT" request.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure.
 * @param   pPktHdr             The gadget disconnect packet.
 */
static int utsDoGadgetDisconnect(PUTSCLIENT pClient, PCUTSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(UTSPKTREQGDGTDCNT))
        return utsReplyBadSize(pClient, pPktHdr, sizeof(UTSPKTREQGDGTDCNT));

    if (   pClient->enmState != UTSCLIENTSTATE_READY
        || pClient->hGadgetHost == NIL_UTSGADGETHOST)
        return utsReplyInvalidState(pClient, pPktHdr);

    PUTSPKTREQGDGTDCNT pReq = (PUTSPKTREQGDGTDCNT)pPktHdr;

    if (pReq->idGadget != 0)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_HANDLE, "The given gadget handle is invalid");
    if (pClient->hGadget == NIL_UTSGADGET)
        return utsReplyRC(pClient, pPktHdr, VERR_INVALID_STATE, "The gadget is not set up");

    int rc = utsGadgetDisconnect(pClient->hGadget);
    if (RT_SUCCESS(rc))
        rc = utsReplyAck(pClient, pPktHdr);
    else
        rc = utsReplyRC(pClient, pPktHdr, rc, "Failed to disconnect the gadget");

    return rc;
}

/**
 * Main request processing routine for each client.
 *
 * @returns IPRT status code.
 * @param   pClient             The UTS client structure sending the request.
 */
static int utsClientReqProcess(PUTSCLIENT pClient)
{
    /*
     * Read client command packet and process it.
     */
    PUTSPKTHDR pPktHdr = NULL;
    int rc = utsRecvPkt(pClient, &pPktHdr, true /*fAutoRetryOnFailure*/);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do a string switch on the opcode bit.
     */
    /* Connection: */
    if (     utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_HOWDY))
        rc = utsDoHowdy(pClient, pPktHdr);
    else if (utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_BYE))
        rc = utsDoBye(pClient, pPktHdr);
    /* Gadget API. */
    else if (utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_GADGET_CREATE))
        rc = utsDoGadgetCreate(pClient, pPktHdr);
    else if (utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_GADGET_DESTROY))
        rc = utsDoGadgetDestroy(pClient, pPktHdr);
    else if (utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_GADGET_CONNECT))
        rc = utsDoGadgetConnect(pClient, pPktHdr);
    else if (utsIsSameOpcode(pPktHdr, UTSPKT_OPCODE_GADGET_DISCONNECT))
        rc = utsDoGadgetDisconnect(pClient, pPktHdr);
    /* Misc: */
    else
        rc = utsReplyUnknown(pClient, pPktHdr);

    RTMemFree(pPktHdr);

    return rc;
}

/**
 * Destroys a client instance.
 *
 * @param   pClient             The UTS client structure.
 */
static void utsClientDestroy(PUTSCLIENT pClient)
{
    if (pClient->pszHostname)
        RTStrFree(pClient->pszHostname);
    if (pClient->hGadget != NIL_UTSGADGET)
        utsGadgetRelease(pClient->hGadget);
    if (pClient->hGadgetHost != NIL_UTSGADGETHOST)
        utsGadgetHostRelease(pClient->hGadgetHost);
    RTMemFree(pClient);
}

/**
 * The main thread worker serving the clients.
 */
static DECLCALLBACK(int) utsClientWorker(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF2(hThread, pvUser);
    unsigned    cClientsMax = 0;
    unsigned    cClientsCur = 0;
    PUTSCLIENT *papClients  = NULL;
    RTPOLLSET   hPollSet;

    int rc = RTPollSetCreate(&hPollSet);
    if (RT_FAILURE(rc))
        return rc;

    /* Add the pipe to the poll set. */
    rc = RTPollSetAddPipe(hPollSet, g_hPipeR, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, 0);
    if (RT_SUCCESS(rc))
    {
        while (!g_fTerminate)
        {
            uint32_t fEvts;
            uint32_t uId;
            rc = RTPoll(hPollSet, RT_INDEFINITE_WAIT, &fEvts, &uId);
            if (RT_SUCCESS(rc))
            {
                if (uId == 0)
                {
                    if (fEvts & RTPOLL_EVT_ERROR)
                        break;

                    /* We got woken up because of a new client. */
                    Assert(fEvts & RTPOLL_EVT_READ);

                    uint8_t bRead;
                    size_t cbRead = 0;
                    rc = RTPipeRead(g_hPipeR, &bRead, 1, &cbRead);
                    AssertRC(rc);

                    RTCritSectEnter(&g_CritSectClients);
                    /* Walk the list and add all new clients. */
                    PUTSCLIENT pIt, pItNext;
                    RTListForEachSafe(&g_LstClientsNew, pIt, pItNext, UTSCLIENT, NdLst)
                    {
                        RTListNodeRemove(&pIt->NdLst);
                        Assert(cClientsCur <= cClientsMax);
                        if (cClientsCur == cClientsMax)
                        {
                            /* Realloc to accommodate for the new clients. */
                            PUTSCLIENT *papClientsNew = (PUTSCLIENT *)RTMemReallocZ(papClients, cClientsMax * sizeof(PUTSCLIENT), (cClientsMax + 10) * sizeof(PUTSCLIENT));
                            if (RT_LIKELY(papClientsNew))
                            {
                                cClientsMax += 10;
                                papClients = papClientsNew;
                            }
                        }

                        if (cClientsCur < cClientsMax)
                        {
                            /* Find a free slot in the client array. */
                            unsigned idxSlt = 0;
                            while (   idxSlt < cClientsMax
                                   && papClients[idxSlt] != NULL)
                                idxSlt++;

                            rc = g_pTransport->pfnPollSetAdd(hPollSet, pIt->pTransportClient, idxSlt + 1);
                            if (RT_SUCCESS(rc))
                            {
                                cClientsCur++;
                                papClients[idxSlt] = pIt;
                            }
                            else
                            {
                                g_pTransport->pfnNotifyBye(pIt->pTransportClient);
                                utsClientDestroy(pIt);
                            }
                        }
                        else
                        {
                            g_pTransport->pfnNotifyBye(pIt->pTransportClient);
                            utsClientDestroy(pIt);
                        }
                    }
                    RTCritSectLeave(&g_CritSectClients);
                }
                else
                {
                    /* Client sends a request, pick the right client and process it. */
                    PUTSCLIENT pClient = papClients[uId - 1];
                    AssertPtr(pClient);
                    if (fEvts & RTPOLL_EVT_READ)
                        rc = utsClientReqProcess(pClient);

                    if (   (fEvts & RTPOLL_EVT_ERROR)
                        || RT_FAILURE(rc))
                    {
                        /* Close connection and remove client from array. */
                        rc = g_pTransport->pfnPollSetRemove(hPollSet, pClient->pTransportClient, uId);
                        AssertRC(rc);

                        g_pTransport->pfnNotifyBye(pClient->pTransportClient);
                        papClients[uId - 1] = NULL;
                        cClientsCur--;
                        utsClientDestroy(pClient);
                    }
                }
            }
        }
    }

    RTPollSetDestroy(hPollSet);
    if (papClients)
        RTMemFree(papClients);
    return rc;
}

/**
 * The main loop.
 *
 * @returns exit code.
 */
static RTEXITCODE utsMainLoop(void)
{
    RTEXITCODE enmExitCode = RTEXITCODE_SUCCESS;
    while (!g_fTerminate)
    {
        /*
         * Wait for new connection and spin off a new thread
         * for every new client.
         */
        PUTSTRANSPORTCLIENT pTransportClient;
        int rc = g_pTransport->pfnWaitForConnect(&pTransportClient);
        if (RT_FAILURE(rc))
            continue;

        /*
         * New connection, create new client structure and spin of
         * the request handling thread.
         */
        PUTSCLIENT pClient = (PUTSCLIENT)RTMemAllocZ(sizeof(UTSCLIENT));
        if (RT_LIKELY(pClient))
        {
            pClient->enmState         = UTSCLIENTSTATE_INITIALISING;
            pClient->pTransportClient = pTransportClient;
            pClient->pszHostname      = NULL;
            pClient->hGadgetHost      = NIL_UTSGADGETHOST;
            pClient->hGadget          = NIL_UTSGADGET;

            /* Add client to the new list and inform the worker thread. */
            RTCritSectEnter(&g_CritSectClients);
            RTListAppend(&g_LstClientsNew, &pClient->NdLst);
            RTCritSectLeave(&g_CritSectClients);

            size_t cbWritten = 0;
            rc = RTPipeWrite(g_hPipeW, "", 1, &cbWritten);
            if (RT_FAILURE(rc))
                RTMsgError("Failed to inform worker thread of a new client");
        }
        else
        {
            RTMsgError("Creating new client structure failed with out of memory error\n");
            g_pTransport->pfnNotifyBye(pTransportClient);
        }


    }

    return enmExitCode;
}

/**
 * Initializes the global UTS state.
 *
 * @returns IPRT status code.
 */
static int utsInit(void)
{
    int rc = VINF_SUCCESS;
    PRTERRINFO pErrInfo = NULL;

    RTListInit(&g_LstClientsNew);

    rc = RTJsonParseFromFile(&g_hCfgJson, g_szCfgPath, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        rc = utsPlatformInit();
        if (RT_SUCCESS(rc))
        {
            rc = RTCritSectInit(&g_CritSectClients);
            if (RT_SUCCESS(rc))
            {
                rc = RTPipeCreate(&g_hPipeR, &g_hPipeW, 0);
                if (RT_SUCCESS(rc))
                {
                    /* Spin off the thread serving connections. */
                    rc = RTThreadCreate(&g_hThreadServing, utsClientWorker, NULL, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                                        "USBTSTSRV");
                    if (RT_SUCCESS(rc))
                        return VINF_SUCCESS;
                    else
                        RTMsgError("Creating the client worker thread failed with %Rrc\n", rc);

                    RTPipeClose(g_hPipeR);
                    RTPipeClose(g_hPipeW);
                }
                else
                    RTMsgError("Creating communications pipe failed with %Rrc\n", rc);

                RTCritSectDelete(&g_CritSectClients);
            }
            else
                RTMsgError("Creating global critical section failed with %Rrc\n", rc);

            RTJsonValueRelease(g_hCfgJson);
        }
        else
            RTMsgError("Initializing the platform failed with %Rrc\n", rc);
    }
    else
    {
        if (RTErrInfoIsSet(pErrInfo))
        {
            RTMsgError("Failed to parse config with detailed error: %s (%Rrc)\n",
                       pErrInfo->pszMsg, pErrInfo->rc);
            RTErrInfoFree(pErrInfo);
        }
        else
            RTMsgError("Failed to parse config with unknown error (%Rrc)\n", rc);
    }

    return rc;
}

/**
 * Determines the default configuration.
 */
static void utsSetDefaults(void)
{
    /*
     * OS and ARCH.
     */
    AssertCompile(sizeof(KBUILD_TARGET) <= sizeof(g_szOsShortName));
    strcpy(g_szOsShortName, KBUILD_TARGET);

    AssertCompile(sizeof(KBUILD_TARGET_ARCH) <= sizeof(g_szArchShortName));
    strcpy(g_szArchShortName, KBUILD_TARGET_ARCH);

    AssertCompile(sizeof(KBUILD_TARGET) + sizeof(KBUILD_TARGET_ARCH) <= sizeof(g_szOsDotArchShortName));
    strcpy(g_szOsDotArchShortName, KBUILD_TARGET);
    g_szOsDotArchShortName[sizeof(KBUILD_TARGET) - 1] = '.';
    strcpy(&g_szOsDotArchShortName[sizeof(KBUILD_TARGET)], KBUILD_TARGET_ARCH);

    AssertCompile(sizeof(KBUILD_TARGET) + sizeof(KBUILD_TARGET_ARCH) <= sizeof(g_szOsSlashArchShortName));
    strcpy(g_szOsSlashArchShortName, KBUILD_TARGET);
    g_szOsSlashArchShortName[sizeof(KBUILD_TARGET) - 1] = '/';
    strcpy(&g_szOsSlashArchShortName[sizeof(KBUILD_TARGET)], KBUILD_TARGET_ARCH);

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    strcpy(g_szExeSuff,    ".exe");
    strcpy(g_szScriptSuff, ".cmd");
#else
    strcpy(g_szExeSuff,    "");
    strcpy(g_szScriptSuff, ".sh");
#endif

    /*
     * The CD/DVD-ROM location.
     */
    /** @todo do a better job here :-) */
#ifdef RT_OS_WINDOWS
    strcpy(g_szDefCdRomPath, "D:/");
#elif defined(RT_OS_OS2)
    strcpy(g_szDefCdRomPath, "D:/");
#else
    if (RTDirExists("/media"))
        strcpy(g_szDefCdRomPath, "/media/cdrom");
    else
        strcpy(g_szDefCdRomPath, "/mnt/cdrom");
#endif
    strcpy(g_szCdRomPath, g_szDefCdRomPath);

    /*
     * Temporary directory.
     */
    int rc = RTPathTemp(g_szDefScratchPath, sizeof(g_szDefScratchPath));
    if (RT_SUCCESS(rc))
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) || defined(RT_OS_DOS)
        rc = RTPathAppend(g_szDefScratchPath, sizeof(g_szDefScratchPath), "uts-XXXX.tmp");
#else
        rc = RTPathAppend(g_szDefScratchPath, sizeof(g_szDefScratchPath), "uts-XXXXXXXXX.tmp");
#endif
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTPathTemp/Append failed when constructing scratch path: %Rrc\n", rc);
        strcpy(g_szDefScratchPath, "/tmp/uts-XXXX.tmp");
    }
    strcpy(g_szScratchPath, g_szDefScratchPath);

    /*
     * Config file location.
     */
    /** @todo Improve */
#if !defined(RT_OS_WINDOWS)
    strcpy(g_szCfgPath, "/etc/uts.conf");
#else
    strcpy(g_szCfgPath, "");
#endif

    /*
     * The default transporter is the first one.
     */
    g_pTransport = g_apTransports[0];
}

/**
 * Prints the usage.
 *
 * @param   pStrm               Where to print it.
 * @param   pszArgv0            The program name (argv[0]).
 */
static void utsUsage(PRTSTREAM pStrm, const char *pszArgv0)
{
    RTStrmPrintf(pStrm,
                 "Usage: %Rbn [options]\n"
                 "\n"
                 "Options:\n"
                 "  --config <path>\n"
                 "      Where to load the config from\n"
                 "  --cdrom <path>\n"
                 "      Where the CD/DVD-ROM will be mounted.\n"
                 "      Default: %s\n"
                 "  --scratch <path>\n"
                 "      Where to put scratch files.\n"
                 "      Default: %s \n"
                 ,
                 pszArgv0,
                 g_szDefCdRomPath,
                 g_szDefScratchPath);
    RTStrmPrintf(pStrm,
                 "  --transport <name>\n"
                 "      Use the specified transport layer, one of the following:\n");
    for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
        RTStrmPrintf(pStrm, "          %s - %s\n", g_apTransports[i]->szName, g_apTransports[i]->pszDesc);
    RTStrmPrintf(pStrm, "      Default: %s\n", g_pTransport->szName);
    RTStrmPrintf(pStrm,
                 "  --display-output, --no-display-output\n"
                 "      Display the output and the result of all child processes.\n");
    RTStrmPrintf(pStrm,
                 "  --foreground\n"
                 "      Don't daemonize, run in the foreground.\n");
    RTStrmPrintf(pStrm,
                 "  --help, -h, -?\n"
                 "      Display this message and exit.\n"
                 "  --version, -V\n"
                 "      Display the version and exit.\n");

    for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
        if (g_apTransports[i]->cOpts)
        {
            RTStrmPrintf(pStrm,
                         "\n"
                         "Options for %s:\n", g_apTransports[i]->szName);
            g_apTransports[i]->pfnUsage(g_pStdOut);
        }
}

/**
 * Parses the arguments.
 *
 * @returns Exit code. Exit if this is non-zero or @a *pfExit is set.
 * @param   argc                The number of arguments.
 * @param   argv                The argument vector.
 * @param   pfExit              For indicating exit when the exit code is zero.
 */
static RTEXITCODE utsParseArgv(int argc, char **argv, bool *pfExit)
{
    *pfExit = false;

    /*
     * Storage for locally handled options.
     */
    bool fDaemonize  = true;
    bool fDaemonized = false;

    /*
     * Combine the base and transport layer option arrays.
     */
    static const RTGETOPTDEF s_aBaseOptions[] =
    {
        { "--config",           'C', RTGETOPT_REQ_STRING  },
        { "--transport",        't', RTGETOPT_REQ_STRING  },
        { "--cdrom",            'c', RTGETOPT_REQ_STRING  },
        { "--scratch",          's', RTGETOPT_REQ_STRING  },
        { "--display-output",   'd', RTGETOPT_REQ_NOTHING  },
        { "--no-display-output",'D', RTGETOPT_REQ_NOTHING  },
        { "--foreground",       'f', RTGETOPT_REQ_NOTHING  },
        { "--daemonized",       'Z', RTGETOPT_REQ_NOTHING  },
    };

    size_t cOptions = RT_ELEMENTS(s_aBaseOptions);
    for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
        cOptions += g_apTransports[i]->cOpts;

    PRTGETOPTDEF paOptions = (PRTGETOPTDEF)alloca(cOptions * sizeof(RTGETOPTDEF));
    if (!paOptions)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "alloca failed\n");

    memcpy(paOptions, s_aBaseOptions, sizeof(s_aBaseOptions));
    cOptions = RT_ELEMENTS(s_aBaseOptions);
    for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
    {
        memcpy(&paOptions[cOptions], g_apTransports[i]->paOpts, g_apTransports[i]->cOpts * sizeof(RTGETOPTDEF));
        cOptions += g_apTransports[i]->cOpts;
    }

    /*
     * Parse the arguments.
     */
    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, argc, argv, paOptions, cOptions, 1, 0 /* fFlags */);
    AssertRC(rc);

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&GetState, &Val)))
    {
        switch (ch)
        {
            case 'C':
                rc = RTStrCopy(g_szCfgPath, sizeof(g_szCfgPath), Val.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Config file path is path too long (%Rrc)\n", rc);
                break;

            case 'c':
                rc = RTStrCopy(g_szCdRomPath, sizeof(g_szCdRomPath), Val.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "CD/DVD-ROM is path too long (%Rrc)\n", rc);
                break;

            case 'd':
                g_fDisplayOutput = true;
                break;

            case 'D':
                g_fDisplayOutput = false;
                break;

            case 'f':
                fDaemonize = false;
                break;

            case 'h':
                utsUsage(g_pStdOut, argv[0]);
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 's':
                rc = RTStrCopy(g_szScratchPath, sizeof(g_szScratchPath), Val.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "scratch path is too long (%Rrc)\n", rc);
                break;

            case 't':
            {
                PCUTSTRANSPORT pTransport = NULL;
                for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
                    if (!strcmp(g_apTransports[i]->szName, Val.psz))
                    {
                        pTransport = g_apTransports[i];
                        break;
                    }
                if (!pTransport)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown transport layer name '%s'\n", Val.psz);
                g_pTransport = pTransport;
                break;
            }

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 'Z':
                fDaemonized = true;
                fDaemonize = false;
                break;

            default:
            {
                rc = VERR_TRY_AGAIN;
                for (size_t i = 0; i < RT_ELEMENTS(g_apTransports); i++)
                    if (g_apTransports[i]->cOpts)
                    {
                        rc = g_apTransports[i]->pfnOption(ch, &Val);
                        if (RT_SUCCESS(rc))
                            break;
                        if (rc != VERR_TRY_AGAIN)
                        {
                            *pfExit = true;
                            return RTEXITCODE_SYNTAX;
                        }
                    }
                if (rc == VERR_TRY_AGAIN)
                {
                    *pfExit = true;
                    return RTGetOptPrintError(ch, &Val);
                }
                break;
            }
        }
    }

    /*
     * Daemonize ourselves if asked to.
     */
    if (fDaemonize && !*pfExit)
    {
        rc = RTProcDaemonize(argv, "--daemonized");
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcDaemonize: %Rrc\n", rc);
        *pfExit = true;
    }

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    /*
     * Initialize the runtime.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Determine defaults and parse the arguments.
     */
    utsSetDefaults();
    bool fExit;
    RTEXITCODE rcExit = utsParseArgv(argc, argv, &fExit);
    if (rcExit != RTEXITCODE_SUCCESS || fExit)
        return rcExit;

    /*
     * Initialize global state.
     */
    rc = utsInit();
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Initialize the transport layer.
     */
    rc = g_pTransport->pfnInit();
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Ok, start working
     */
    rcExit = utsMainLoop();

    /*
     * Cleanup.
     */
    g_pTransport->pfnTerm();

    utsPlatformTerm();

    return rcExit;
}

