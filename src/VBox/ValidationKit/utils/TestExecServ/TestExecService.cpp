/* $Id: TestExecService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TestExecServ - Basic Remote Execution Service.
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
#include <iprt/buildconfig.h>
#include <iprt/cdrom.h>
#include <iprt/critsect.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/initterm.h>
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
#include <iprt/system.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/uuid.h>
#include <iprt/zip.h>

#include <package-generated.h>
#include "product-generated.h"

#include <VBox/version.h>
#include <VBox/log.h>

#include "product-generated.h"
#include "TestExecServiceInternal.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Handle IDs used by txsDoExec for the poll set.
 */
typedef enum TXSEXECHNDID
{
    TXSEXECHNDID_STDIN = 0,
    TXSEXECHNDID_STDOUT,
    TXSEXECHNDID_STDERR,
    TXSEXECHNDID_TESTPIPE,
    TXSEXECHNDID_STDIN_WRITABLE,
    TXSEXECHNDID_TRANSPORT,
    TXSEXECHNDID_THREAD
} TXSEXECHNDID;


/**
 * For buffering process input supplied by the client.
 */
typedef struct TXSEXECSTDINBUF
{
    /** The mount of buffered data. */
    size_t  cb;
    /** The current data offset. */
    size_t  off;
    /** The data buffer. */
    char   *pch;
    /** The amount of allocated buffer space. */
    size_t  cbAllocated;
    /** Send further input into the bit bucket (stdin is dead). */
    bool    fBitBucket;
    /** The CRC-32 for standard input (received part). */
    uint32_t uCrc32;
} TXSEXECSTDINBUF;
/** Pointer to a standard input buffer. */
typedef TXSEXECSTDINBUF *PTXSEXECSTDINBUF;

/**
 * TXS child process info.
 */
typedef struct TXSEXEC
{
    PCTXSPKTHDR     pPktHdr;
    RTMSINTERVAL    cMsTimeout;
    int             rcReplySend;

    RTPOLLSET       hPollSet;
    RTPIPE          hStdInW;
    RTPIPE          hStdOutR;
    RTPIPE          hStdErrR;
    RTPIPE          hTestPipeR;
    RTPIPE          hWakeUpPipeR;
    RTTHREAD        hThreadWaiter;

    /** @name For the setup phase
     * @{ */
    struct StdPipe
    {
        RTHANDLE    hChild;
        PRTHANDLE   phChild;
    }               StdIn,
                    StdOut,
                    StdErr;
    RTPIPE          hTestPipeW;
    RTENV           hEnv;
    /** @} */

    /** For serializating some access. */
    RTCRITSECT      CritSect;
    /** @name Members protected by the critical section.
     * @{ */
    RTPROCESS       hProcess;
    /** The process status.  Only valid when fProcessAlive is cleared. */
    RTPROCSTATUS    ProcessStatus;
    /** Set when the process is alive, clear when dead. */
    bool volatile   fProcessAlive;
    /** The end of the pipe that hThreadWaiter writes to. */
    RTPIPE          hWakeUpPipeW;
    /** @} */
} TXSEXEC;
/** Pointer to a the TXS child process info. */
typedef TXSEXEC *PTXSEXEC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Transport layers.
 */
static const PCTXSTRANSPORT g_apTransports[] =
{
    &g_TcpTransport,
#ifndef RT_OS_OS2
    &g_SerialTransport,
#endif
    //&g_FileSysTransport,
    //&g_GuestPropTransport,
    //&g_TestDevTransport,
};

/** The release logger. */
static PRTLOGGER            g_pRelLogger;
/** The select transport layer. */
static PCTXSTRANSPORT       g_pTransport;
/** The scratch path. */
static char                 g_szScratchPath[RTPATH_MAX];
/** The default scratch path. */
static char                 g_szDefScratchPath[RTPATH_MAX];
/** The CD/DVD-ROM path. */
static char                 g_szCdRomPath[RTPATH_MAX];
/** The default CD/DVD-ROM path. */
static char                 g_szDefCdRomPath[RTPATH_MAX];
/** The directory containing the TXS executable. */
static char                 g_szTxsDir[RTPATH_MAX];
/** The current working directory for TXS (doesn't change). */
static char                 g_szCwd[RTPATH_MAX];
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
/** UUID identifying this TXS instance.  This can be used to see if TXS
 * has been restarted or not. */
static RTUUID               g_InstanceUuid;
/** Whether to display the output of the child process or not.  */
static bool                 g_fDisplayOutput = true;
/** Whether to terminate or not.
 * @todo implement signals and stuff.  */
static bool volatile        g_fTerminate = false;
/** Verbosity level. */
uint32_t                    g_cVerbose = 1;


/**
 * Calculates the checksum value, zero any padding space and send the packet.
 *
 * @returns IPRT status code.
 * @param   pPkt                The packet to send.  Must point to a correctly
 *                              aligned buffer.
 */
static int txsSendPkt(PTXSPKTHDR pPkt)
{
    Assert(pPkt->cb >= sizeof(*pPkt));
    pPkt->uCrc32 = RTCrc32(pPkt->achOpcode, pPkt->cb - RT_UOFFSETOF(TXSPKTHDR, achOpcode));
    if (pPkt->cb != RT_ALIGN_32(pPkt->cb, TXSPKT_ALIGNMENT))
        memset((uint8_t *)pPkt + pPkt->cb, '\0', RT_ALIGN_32(pPkt->cb, TXSPKT_ALIGNMENT) - pPkt->cb);

    Log(("txsSendPkt: cb=%#x opcode=%.8s\n", pPkt->cb, pPkt->achOpcode));
    Log2(("%.*Rhxd\n", RT_MIN(pPkt->cb, 256), pPkt));
    int rc = g_pTransport->pfnSendPkt(pPkt);
    while (RT_UNLIKELY(rc == VERR_INTERRUPTED) && !g_fTerminate)
        rc = g_pTransport->pfnSendPkt(pPkt);
    if (RT_FAILURE(rc))
        Log(("txsSendPkt: rc=%Rrc\n", rc));

    return rc;
}

/**
 * Sends a babble reply and disconnects the client (if applicable).
 *
 * @param   pszOpcode           The BABBLE opcode.
 */
static void txsReplyBabble(const char *pszOpcode)
{
    TXSPKTHDR Reply;
    Reply.cb     = sizeof(Reply);
    Reply.uCrc32 = 0;
    memcpy(Reply.achOpcode, pszOpcode, sizeof(Reply.achOpcode));

    g_pTransport->pfnBabble(&Reply, 20*1000);
}

/**
 * Receive and validate a packet.
 *
 * Will send bable responses to malformed packets that results in a error status
 * code.
 *
 * @returns IPRT status code.
 * @param   ppPktHdr            Where to return the packet on success.  Free
 *                              with RTMemFree.
 * @param   fAutoRetryOnFailure Whether to retry on error.
 */
static int txsRecvPkt(PPTXSPKTHDR ppPktHdr, bool fAutoRetryOnFailure)
{
    for (;;)
    {
        PTXSPKTHDR pPktHdr;
        int rc = g_pTransport->pfnRecvPkt(&pPktHdr);
        if (RT_SUCCESS(rc))
        {
            /* validate the packet. */
            if (   pPktHdr->cb >= sizeof(TXSPKTHDR)
                && pPktHdr->cb < TXSPKT_MAX_SIZE)
            {
                Log2(("txsRecvPkt: pPktHdr=%p cb=%#x crc32=%#x opcode=%.8s\n"
                      "%.*Rhxd\n",
                      pPktHdr, pPktHdr->cb, pPktHdr->uCrc32, pPktHdr->achOpcode, RT_MIN(pPktHdr->cb, 256), pPktHdr));
                uint32_t uCrc32Calc = pPktHdr->uCrc32 != 0
                                    ? RTCrc32(&pPktHdr->achOpcode[0], pPktHdr->cb - RT_UOFFSETOF(TXSPKTHDR, achOpcode))
                                    : 0;
                if (pPktHdr->uCrc32 == uCrc32Calc)
                {
                    AssertCompileMemberSize(TXSPKTHDR, achOpcode, 8);
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
                        Log(("txsRecvPkt: cb=%#x opcode=%.8s\n", pPktHdr->cb, pPktHdr->achOpcode));
                        *ppPktHdr = pPktHdr;
                        return rc;
                    }

                    rc = VERR_IO_BAD_COMMAND;
                }
                else
                {
                    Log(("txsRecvPkt: cb=%#x opcode=%.8s crc32=%#x actual=%#x\n",
                         pPktHdr->cb, pPktHdr->achOpcode, pPktHdr->uCrc32, uCrc32Calc));
                    rc = VERR_IO_CRC;
                }
            }
            else
                rc = VERR_IO_BAD_LENGTH;

            /* Send babble reply and disconnect the client if the transport is
               connection oriented. */
            if (rc == VERR_IO_BAD_LENGTH)
                txsReplyBabble("BABBLE L");
            else if (rc == VERR_IO_CRC)
                txsReplyBabble("BABBLE C");
            else if (rc == VERR_IO_BAD_COMMAND)
                txsReplyBabble("BABBLE O");
            else
                txsReplyBabble("BABBLE  ");
            RTMemFree(pPktHdr);
        }

        /* Try again or return failure? */
        if (   g_fTerminate
            || rc != VERR_INTERRUPTED
            || !fAutoRetryOnFailure
            )
        {
            Log(("txsRecvPkt: rc=%Rrc\n", rc));
            return rc;
        }
    }
}

/**
 * Make a simple reply, only status opcode.
 *
 * @returns IPRT status code of the send.
 * @param   pReply              The reply packet.
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   cbExtra             Bytes in addition to the header.
 */
static int txsReplyInternal(PTXSPKTHDR pReply, const char *pszOpcode, size_t cbExtra)
{
    /* copy the opcode, don't be too strict in case of a padding screw up. */
    size_t cchOpcode = strlen(pszOpcode);
    if (RT_LIKELY(cchOpcode == sizeof(pReply->achOpcode)))
        memcpy(pReply->achOpcode, pszOpcode, sizeof(pReply->achOpcode));
    else
    {
        Assert(cchOpcode == sizeof(pReply->achOpcode));
        while (cchOpcode > 0 && pszOpcode[cchOpcode - 1] == ' ')
            cchOpcode--;
        AssertMsgReturn(cchOpcode < sizeof(pReply->achOpcode), ("%d/'%.8s'\n", cchOpcode, pszOpcode), VERR_INTERNAL_ERROR_4);
        memcpy(pReply->achOpcode, pszOpcode, cchOpcode);
        memset(&pReply->achOpcode[cchOpcode], ' ', sizeof(pReply->achOpcode) - cchOpcode);
    }

    pReply->cb     = (uint32_t)sizeof(TXSPKTHDR) + (uint32_t)cbExtra;
    pReply->uCrc32 = 0; /* (txsSendPkt sets it) */

    return txsSendPkt(pReply);
}

/**
 * Make a simple reply, only status opcode.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The original packet (for future use).
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 */
static int txsReplySimple(PCTXSPKTHDR pPktHdr, const char *pszOpcode)
{
    TXSPKTHDR Pkt;
    NOREF(pPktHdr);
    return txsReplyInternal(&Pkt, pszOpcode, 0);
}

/**
 * Acknowledges a packet with success.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The original packet (for future use).
 */
static int txsReplyAck(PCTXSPKTHDR pPktHdr)
{
    return txsReplySimple(pPktHdr, "ACK     ");
}

/**
 * Replies with a failure.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The original packet (for future use).
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   pszDetailFmt        Longer description of the problem (format
 *                              string).
 * @param   va                  Format arguments.
 */
static int txsReplyFailureV(PCTXSPKTHDR pPktHdr, const char *pszOpcode, const char *pszDetailFmt, va_list va)
{
    NOREF(pPktHdr);
    union
    {
        TXSPKTHDR   Hdr;
        char        ach[256];
    } uPkt;

    size_t cchDetail = RTStrPrintfV(&uPkt.ach[sizeof(TXSPKTHDR)],
                                    sizeof(uPkt) - sizeof(TXSPKTHDR),
                                    pszDetailFmt, va);
    return txsReplyInternal(&uPkt.Hdr, pszOpcode, cchDetail + 1);
}

/**
 * Replies with a failure.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The original packet (for future use).
 * @param   pszOpcode           The status opcode.  Exactly 8 chars long, padd
 *                              with space.
 * @param   pszDetailFmt        Longer description of the problem (format
 *                              string).
 * @param   ...                 Format arguments.
 */
static int txsReplyFailure(PCTXSPKTHDR pPktHdr, const char *pszOpcode, const char *pszDetailFmt, ...)
{
    va_list va;
    va_start(va, pszDetailFmt);
    int rc = txsReplyFailureV(pPktHdr, pszOpcode, pszDetailFmt, va);
    va_end(va);
    return rc;
}

/**
 * Replies according to the return code.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The packet to reply to.
 * @param   rcOperation         The status code to report.
 * @param   pszOperationFmt     The operation that failed.  Typically giving the
 *                              function call with important arguments.
 * @param   ...                 Arguments to the format string.
 */
static int txsReplyRC(PCTXSPKTHDR pPktHdr, int rcOperation, const char *pszOperationFmt, ...)
{
    if (RT_SUCCESS(rcOperation))
        return txsReplyAck(pPktHdr);

    char    szOperation[128];
    va_list va;
    va_start(va, pszOperationFmt);
    RTStrPrintfV(szOperation, sizeof(szOperation), pszOperationFmt, va);
    va_end(va);

    return txsReplyFailure(pPktHdr, "FAILED  ", "%s failed with rc=%Rrc (opcode '%.8s')",
                           szOperation, rcOperation, pPktHdr->achOpcode);
}

/**
 * Signal a bad packet minum size.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The packet to reply to.
 * @param   cbMin               The minimum size.
 */
static int txsReplyBadMinSize(PCTXSPKTHDR pPktHdr, size_t cbMin)
{
    return txsReplyFailure(pPktHdr, "BAD SIZE", "Expected at least %zu bytes, got %u (opcode '%.8s')",
                           cbMin, pPktHdr->cb, pPktHdr->achOpcode);
}

/**
 * Signal a bad packet exact size.
 *
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The packet to reply to.
 * @param   cb                  The wanted size.
 */
static int txsReplyBadSize(PCTXSPKTHDR pPktHdr, size_t cb)
{
    return txsReplyFailure(pPktHdr, "BAD SIZE", "Expected at %zu bytes, got %u  (opcode '%.8s')",
                           cb, pPktHdr->cb, pPktHdr->achOpcode);
}

/**
 * Deals with a command that isn't implemented yet.
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The packet which opcode isn't implemented.
 */
static int txsReplyNotImplemented(PCTXSPKTHDR pPktHdr)
{
    return txsReplyFailure(pPktHdr, "NOT IMPL", "Opcode '%.8s' is not implemented", pPktHdr->achOpcode);
}

/**
 * Deals with a unknown command.
 * @returns IPRT status code of the send.
 * @param   pPktHdr             The packet to reply to.
 */
static int txsReplyUnknown(PCTXSPKTHDR pPktHdr)
{
    return txsReplyFailure(pPktHdr, "UNKNOWN ", "Opcode '%.8s' is not known", pPktHdr->achOpcode);
}

/**
 * Replaces a variable with its value.
 *
 * @returns VINF_SUCCESS or VERR_NO_STR_MEMORY.
 * @param   ppszNew             In/Out.
 * @param   pcchNew             In/Out. (Messed up on failure.)
 * @param   offVar              Variable offset.
 * @param   cchVar              Variable length.
 * @param   pszValue            The value.
 * @param   cchValue            Value length.
 */
static int txsReplaceStringVariable(char **ppszNew, size_t *pcchNew, size_t offVar, size_t cchVar,
                                    const char *pszValue, size_t cchValue)
{
    size_t const cchAfter = *pcchNew - offVar - cchVar;
    if (cchVar < cchValue)
    {
        *pcchNew += cchValue - cchVar;
        int rc = RTStrRealloc(ppszNew, *pcchNew + 1);
        if (RT_FAILURE(rc))
            return rc;
    }

    char *pszNew = *ppszNew;
    memmove(&pszNew[offVar + cchValue], &pszNew[offVar + cchVar], cchAfter + 1);
    memcpy(&pszNew[offVar], pszValue, cchValue);
    return VINF_SUCCESS;
}

/**
 * Replace the variables found in the source string, returning a new string that
 * lives on the string heap.
 *
 * @returns Boolean success indicator.  Will reply to the client with all the
 *          gory detail on failure.
 * @param   pPktHdr             The packet the string relates to.  For replying
 *                              on error.
 * @param   pszSrc              The source string.
 * @param   ppszNew             Where to return the new string.
 * @param   prcSend             Where to return the status code of the send on
 *                              failure.
 */
static int txsReplaceStringVariables(PCTXSPKTHDR pPktHdr, const char *pszSrc, char **ppszNew, int *prcSend)
{
    /* Lazy approach that employs memmove.  */
    size_t  cchNew    = strlen(pszSrc);
    char   *pszNew    = RTStrDup(pszSrc);
    char   *pszDollar = pszNew;
    while (pszDollar && (pszDollar = strchr(pszDollar, '$')) != NULL)
    {
        if (pszDollar[1] == '{')
        {
            char *pszEnd = strchr(&pszDollar[2], '}');
            if (pszEnd)
            {
#define IF_VARIABLE_DO(pszDollar, szVarExpr, pszValue) \
    if (   cchVar == sizeof(szVarExpr) - 1 \
        && !memcmp(pszDollar, szVarExpr, sizeof(szVarExpr) - 1) ) \
    { \
        size_t const cchValue = strlen(pszValue); \
        rc = txsReplaceStringVariable(&pszNew, &cchNew, offDollar, \
                                      sizeof(szVarExpr) - 1, pszValue, cchValue); \
        offDollar += cchValue; \
    }
                int          rc;
                size_t const cchVar    = pszEnd - pszDollar + 1; /* includes "${}" */
                size_t       offDollar = pszDollar - pszNew;
                     IF_VARIABLE_DO(pszDollar, "${CDROM}",   g_szCdRomPath)
                else IF_VARIABLE_DO(pszDollar, "${SCRATCH}", g_szScratchPath)
                else IF_VARIABLE_DO(pszDollar, "${ARCH}",    g_szArchShortName)
                else IF_VARIABLE_DO(pszDollar, "${OS}",      g_szOsShortName)
                else IF_VARIABLE_DO(pszDollar, "${OS.ARCH}", g_szOsDotArchShortName)
                else IF_VARIABLE_DO(pszDollar, "${OS/ARCH}", g_szOsSlashArchShortName)
                else IF_VARIABLE_DO(pszDollar, "${EXESUFF}", g_szExeSuff)
                else IF_VARIABLE_DO(pszDollar, "${SCRIPTSUFF}", g_szScriptSuff)
                else IF_VARIABLE_DO(pszDollar, "${TXSDIR}",  g_szTxsDir)
                else IF_VARIABLE_DO(pszDollar, "${CWD}",     g_szCwd)
                else if (   cchVar >= sizeof("${env.") + 1
                         && memcmp(pszDollar, RT_STR_TUPLE("${env.")) == 0)
                {
                    const char *pszEnvVar = pszDollar + 6;
                    size_t      cchValue  = 0;
                    char        szValue[RTPATH_MAX];
                    *pszEnd = '\0';
                    rc = RTEnvGetEx(RTENV_DEFAULT, pszEnvVar, szValue, sizeof(szValue), &cchValue);
                    if (RT_SUCCESS(rc))
                    {
                        *pszEnd = '}';
                        rc = txsReplaceStringVariable(&pszNew, &cchNew, offDollar, cchVar, szValue, cchValue);
                        offDollar += cchValue;
                    }
                    else
                    {
                        if (rc == VERR_ENV_VAR_NOT_FOUND)
                            *prcSend = txsReplyFailure(pPktHdr, "UNKN VAR", "Environment variable '%s' encountered in '%s'",
                                                       pszEnvVar, pszSrc);
                        else
                            *prcSend = txsReplyFailure(pPktHdr, "FAILDENV",
                                                       "RTEnvGetEx(,'%s',,,) failed with %Rrc (opcode '%.8s')",
                                                       pszEnvVar, rc, pPktHdr->achOpcode);
                        RTStrFree(pszNew);
                        *ppszNew = NULL;
                        return false;
                    }
                }
                else
                {
                    RTStrFree(pszNew);
                    *prcSend = txsReplyFailure(pPktHdr, "UNKN VAR", "Unknown variable '%.*s' encountered in '%s'",
                                               cchVar, pszDollar, pszSrc);
                    *ppszNew = NULL;
                    return false;
                }
                pszDollar = &pszNew[offDollar];

                if (RT_FAILURE(rc))
                {
                    RTStrFree(pszNew);
                    *prcSend = txsReplyRC(pPktHdr, rc, "RTStrRealloc");
                    *ppszNew = NULL;
                    return false;
                }
#undef IF_VARIABLE_DO
            }
        }
        /* Undo dollar escape sequences: $$ -> $ */
        else if (pszDollar[1] == '$')
        {
            size_t cchLeft = cchNew - (&pszDollar[1] - pszNew);
            memmove(pszDollar, &pszDollar[1], cchLeft);
            pszDollar[cchLeft] = '\0';
            cchNew -= 1;
        }
        else /* No match, move to next char to avoid endless looping. */
            pszDollar++;
    }

    *ppszNew = pszNew;
    *prcSend = VINF_SUCCESS;
    return true;
}

/**
 * Checks if the string is valid and returns the expanded version.
 *
 * @returns true if valid, false if invalid.
 * @param   pPktHdr             The packet being unpacked.
 * @param   pszArgName          The argument name.
 * @param   psz                 Pointer to the string within pPktHdr.
 * @param   ppszExp             Where to return the expanded string.  Must be
 *                              freed by calling RTStrFree().
 * @param   ppszNext            Where to return the pointer to the next field.
 *                              If NULL, then we assume this string is at the
 *                              end of the packet and will make sure it has the
 *                              advertised length.
 * @param   prcSend             Where to return the status code of the send on
 *                              failure.
 */
static bool txsIsStringValid(PCTXSPKTHDR pPktHdr, const char *pszArgName, const char *psz,
                             char **ppszExp, const char **ppszNext, int *prcSend)
{
    *ppszExp = NULL;
    if (ppszNext)
        *ppszNext = NULL;

    size_t const    off = psz - (const char *)pPktHdr;
    if (pPktHdr->cb <= off)
    {
        *prcSend = txsReplyFailure(pPktHdr, "STR MISS", "Missing string argument '%s' in '%.8s'",
                                   pszArgName, pPktHdr->achOpcode);
        return false;
    }

    size_t const    cchMax = pPktHdr->cb - off;
    const char     *pszEnd = RTStrEnd(psz, cchMax);
    if (!pszEnd)
    {
        *prcSend = txsReplyFailure(pPktHdr, "STR TERM", "The string argument '%s' in '%.8s' is unterminated",
                                   pszArgName, pPktHdr->achOpcode);
        return false;
    }

    if (!ppszNext && (size_t)(pszEnd - psz) != cchMax - 1)
    {
        *prcSend = txsReplyFailure(pPktHdr, "STR SHRT", "The string argument '%s' in '%.8s' is shorter than advertised",
                                   pszArgName, pPktHdr->achOpcode);
        return false;
    }

    if (!txsReplaceStringVariables(pPktHdr, psz, ppszExp, prcSend))
        return false;
    if (ppszNext)
        *ppszNext = pszEnd + 1;
    return true;
}

/**
 * Validates a packet with a single string after the header.
 *
 * @returns true if valid, false if invalid.
 * @param   pPktHdr             The packet.
 * @param   pszArgName          The argument name.
 * @param   ppszExp             Where to return the string pointer.  Variables
 *                              will be replaced and it must therefore be freed
 *                              by calling RTStrFree().
 * @param   prcSend             Where to return the status code of the send on
 *                              failure.
 */
static bool txsIsStringPktValid(PCTXSPKTHDR pPktHdr, const char *pszArgName, char **ppszExp, int *prcSend)
{
    if (pPktHdr->cb < sizeof(TXSPKTHDR) + 2)
    {
        *ppszExp = NULL;
        *prcSend = txsReplyBadMinSize(pPktHdr, sizeof(TXSPKTHDR) + 2);
        return false;
    }

    return txsIsStringValid(pPktHdr, pszArgName, (const char *)(pPktHdr + 1), ppszExp, NULL, prcSend);
}

/**
 * Checks if the two opcodes match.
 *
 * @returns true on match, false on mismatch.
 * @param   pPktHdr             The packet header.
 * @param   pszOpcode2          The opcode we're comparing with.  Does not have
 *                              to be the whole 8 chars long.
 */
DECLINLINE(bool) txsIsSameOpcode(PCTXSPKTHDR pPktHdr, const char *pszOpcode2)
{
    if (pPktHdr->achOpcode[0] != pszOpcode2[0])
        return false;
    if (pPktHdr->achOpcode[1] != pszOpcode2[1])
        return false;

    unsigned i = 2;
    while (   i < RT_SIZEOFMEMB(TXSPKTHDR, achOpcode)
           && pszOpcode2[i] != '\0')
    {
        if (pPktHdr->achOpcode[i] != pszOpcode2[i])
            break;
        i++;
    }

    if (   i < RT_SIZEOFMEMB(TXSPKTHDR, achOpcode)
        && pszOpcode2[i] == '\0')
    {
        while (   i < RT_SIZEOFMEMB(TXSPKTHDR, achOpcode)
               && pPktHdr->achOpcode[i] == ' ')
            i++;
    }

    return i == RT_SIZEOFMEMB(TXSPKTHDR, achOpcode);
}

/**
 * Used by txsDoGetFile to wait for a reply ACK from the client.
 *
 * @returns VINF_SUCCESS on ACK, VERR_GENERAL_FAILURE on NACK,
 *          VERR_NET_NOT_CONNECTED on unknown response (sending a bable reply),
 *          or whatever txsRecvPkt returns.
 * @param   pPktHdr             The original packet (for future use).
 */
static int txsWaitForAck(PCTXSPKTHDR pPktHdr)
{
    NOREF(pPktHdr);
    /** @todo timeout? */
    PTXSPKTHDR pReply;
    int rc = txsRecvPkt(&pReply, false /*fAutoRetryOnFailure*/);
    if (RT_SUCCESS(rc))
    {
        if (txsIsSameOpcode(pReply, "ACK"))
            rc = VINF_SUCCESS;
        else if (txsIsSameOpcode(pReply, "NACK"))
            rc = VERR_GENERAL_FAILURE;
        else
        {
            txsReplyBabble("BABBLE  ");
            rc = VERR_NET_NOT_CONNECTED;
        }
        RTMemFree(pReply);
    }
    return rc;
}

/**
 * Expands the variables in the string and sends it back to the host.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The expand string packet.
 */
static int txsDoExpandString(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszExpanded;
    if (!txsIsStringPktValid(pPktHdr, "string", &pszExpanded, &rc))
        return rc;

    struct
    {
        TXSPKTHDR   Hdr;
        char        szString[_64K];
        char        abPadding[TXSPKT_ALIGNMENT];
    } Pkt;

    size_t const cbExpanded = strlen(pszExpanded) + 1;
    if (cbExpanded <= sizeof(Pkt.szString))
    {
        memcpy(Pkt.szString, pszExpanded, cbExpanded);
        rc = txsReplyInternal(&Pkt.Hdr, "STRING  ", cbExpanded);
    }
    else
    {
        memcpy(Pkt.szString, pszExpanded, sizeof(Pkt.szString));
        Pkt.szString[0] = '\0';
        rc = txsReplyInternal(&Pkt.Hdr, "SHORTSTR", sizeof(Pkt.szString));
    }

    RTStrFree(pszExpanded);
    return rc;
}

/**
 * Packs a tar file / directory.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The pack file packet.
 */
static int txsDoPackFile(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszFile = NULL;
    char *pszSource = NULL;

    /* Packet cursor. */
    const char *pch = (const char *)(pPktHdr + 1);

    if (txsIsStringValid(pPktHdr, "file", pch, &pszFile, &pch, &rc))
    {
        if (txsIsStringValid(pPktHdr, "source", pch, &pszSource, &pch, &rc))
        {
            char *pszSuff = RTPathSuffix(pszFile);

            const char *apszArgs[7];
            unsigned cArgs = 0;

            apszArgs[cArgs++] = "RTTar";
            apszArgs[cArgs++] = "--create";

            apszArgs[cArgs++] = "--file";
            apszArgs[cArgs++] = pszFile;

            if (   pszSuff
                && (   !RTStrICmp(pszSuff, ".gz")
                    || !RTStrICmp(pszSuff, ".tgz")))
                apszArgs[cArgs++] = "--gzip";

            apszArgs[cArgs++] = pszSource;

            RTEXITCODE rcExit = RTZipTarCmd(cArgs, (char **)apszArgs);
            if (rcExit != RTEXITCODE_SUCCESS)
                rc = VERR_GENERAL_FAILURE; /** @todo proper return code. */
            else
                rc = VINF_SUCCESS;

            rc = txsReplyRC(pPktHdr, rc, "RTZipTarCmd(\"%s\",\"%s\")",
                            pszFile, pszSource);

            RTStrFree(pszSource);
        }
        RTStrFree(pszFile);
    }

    return rc;
}

/**
 * Unpacks a tar file.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The unpack file packet.
 */
static int txsDoUnpackFile(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszFile = NULL;
    char *pszDirectory = NULL;

    /* Packet cursor. */
    const char *pch = (const char *)(pPktHdr + 1);

    if (txsIsStringValid(pPktHdr, "file", pch, &pszFile, &pch, &rc))
    {
        if (txsIsStringValid(pPktHdr, "directory", pch, &pszDirectory, &pch, &rc))
        {
            char *pszSuff = RTPathSuffix(pszFile);

            const char *apszArgs[7];
            unsigned cArgs = 0;

            apszArgs[cArgs++] = "RTTar";
            apszArgs[cArgs++] = "--extract";

            apszArgs[cArgs++] = "--file";
            apszArgs[cArgs++] = pszFile;

            apszArgs[cArgs++] = "--directory";
            apszArgs[cArgs++] = pszDirectory;

            if (   pszSuff
                && (   !RTStrICmp(pszSuff, ".gz")
                    || !RTStrICmp(pszSuff, ".tgz")))
                apszArgs[cArgs++] = "--gunzip";

            RTEXITCODE rcExit = RTZipTarCmd(cArgs, (char **)apszArgs);
            if (rcExit != RTEXITCODE_SUCCESS)
                rc = VERR_GENERAL_FAILURE; /** @todo proper return code. */
            else
                rc = VINF_SUCCESS;

            rc = txsReplyRC(pPktHdr, rc, "RTZipTarCmd(\"%s\",\"%s\")",
                            pszFile, pszDirectory);

            RTStrFree(pszDirectory);
        }
        RTStrFree(pszFile);
    }

    return rc;
}

/**
 * Downloads a file to the client.
 *
 * The transfer sends a stream of DATA packets (0 or more) and ends it all with
 * a ACK packet.  If an error occurs, a FAILURE packet is sent and the transfer
 * aborted.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The get file packet.
 */
static int txsDoGetFile(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "file", &pszPath, &rc))
        return rc;

    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszPath, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_SUCCESS(rc))
    {
        uint32_t uMyCrc32 = RTCrc32Start();
        for (;;)
        {
            struct
            {
                TXSPKTHDR   Hdr;
                uint32_t    uCrc32;
                char        ab[_64K];
                char        abPadding[TXSPKT_ALIGNMENT];
            }       Pkt;
            size_t  cbRead;
            rc = RTFileRead(hFile, &Pkt.ab[0], _64K, &cbRead);
            if (RT_FAILURE(rc) || cbRead == 0)
            {
                if (rc == VERR_EOF || (RT_SUCCESS(rc) && cbRead == 0))
                {
                    Pkt.uCrc32 = RTCrc32Finish(uMyCrc32);
                    rc = txsReplyInternal(&Pkt.Hdr, "DATA EOF", sizeof(uint32_t));
                    if (RT_SUCCESS(rc))
                        rc = txsWaitForAck(&Pkt.Hdr);
                }
                else
                    rc = txsReplyRC(pPktHdr, rc, "RTFileRead");
                break;
            }

            uMyCrc32   = RTCrc32Process(uMyCrc32, &Pkt.ab[0], cbRead);
            Pkt.uCrc32 = RTCrc32Finish(uMyCrc32);
            rc = txsReplyInternal(&Pkt.Hdr, "DATA    ", cbRead + sizeof(uint32_t));
            if (RT_FAILURE(rc))
                break;
            rc = txsWaitForAck(&Pkt.Hdr);
            if (RT_FAILURE(rc))
                break;
        }

        RTFileClose(hFile);
    }
    else
        rc = txsReplyRC(pPktHdr, rc, "RTFileOpen(,\"%s\",)", pszPath);

    RTStrFree(pszPath);
    return rc;
}

/**
 * Copies a file from the source to the destination locally.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The copy file packet.
 */
static int txsDoCopyFile(PCTXSPKTHDR pPktHdr)
{
    /* After the packet header follows a 32-bit file mode,
     * the remainder of the packet are two zero terminated paths. */
    size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2;
    if (pPktHdr->cb < cbMin)
        return txsReplyBadMinSize(pPktHdr, cbMin);

    /* Packet cursor. */
    const char *pch = (const char *)(pPktHdr + 1);

    int rc;

    RTFMODE const fMode = *(RTFMODE const *)pch;

    char *pszSrc;
    if (txsIsStringValid(pPktHdr, "source", (const char *)pch + sizeof(RTFMODE), &pszSrc, &pch, &rc))
    {
        char *pszDst;
        if (txsIsStringValid(pPktHdr, "dest", pch, &pszDst, NULL /* Check for string termination */, &rc))
        {
            rc = RTFileCopy(pszSrc, pszDst);
            if (RT_SUCCESS(rc))
            {
                if (fMode) /* Do we need to set the file mode? */
                {
                    rc = RTPathSetMode(pszDst, fMode);
                    if (RT_FAILURE(rc))
                        rc = txsReplyRC(pPktHdr, rc, "RTPathSetMode(\"%s\", %#x)", pszDst, fMode);
                }

                if (RT_SUCCESS(rc))
                    rc = txsReplyAck(pPktHdr);
            }
            else
                rc = txsReplyRC(pPktHdr, rc, "RTFileCopy");
            RTStrFree(pszDst);
        }

        RTStrFree(pszSrc);
    }

    return rc;
}

/**
 * Uploads a file from the client.
 *
 * The transfer sends a stream of DATA packets (0 or more) and ends it all with
 * a DATA EOF packet.  We ACK each of these, so that if a write error occurs we
 * can abort the transfer straight away.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The put file packet.
 * @param   fHasMode            Set if the packet starts with a mode field.
 */
static int txsDoPutFile(PCTXSPKTHDR pPktHdr, bool fHasMode)
{
    int rc;
    RTFMODE fMode = 0;
    char   *pszPath;
    if (!fHasMode)
    {
        if (!txsIsStringPktValid(pPktHdr, "file", &pszPath, &rc))
            return rc;
    }
    else
    {
        /* After the packet header follows a mode mask and the remainder of
           the packet is the zero terminated file name. */
        size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2;
        if (pPktHdr->cb < cbMin)
            return txsReplyBadMinSize(pPktHdr, cbMin);
        if (!txsIsStringValid(pPktHdr, "file", (const char *)(pPktHdr + 1) + sizeof(RTFMODE), &pszPath, NULL, &rc))
            return rc;
        fMode = *(RTFMODE const *)(pPktHdr + 1);
        fMode <<= RTFILE_O_CREATE_MODE_SHIFT;
        fMode &= RTFILE_O_CREATE_MODE_MASK;
    }

    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszPath, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE | fMode);
    if (RT_SUCCESS(rc))
    {
        bool fSuccess = false;
        rc = txsReplyAck(pPktHdr);
        if (RT_SUCCESS(rc))
        {
            if (fMode)
                RTFileSetMode(hFile, fMode);

            /*
             * Read client command packets and process them.
             */
            uint32_t uMyCrc32 = RTCrc32Start();
            for (;;)
            {
                PTXSPKTHDR pDataPktHdr;
                rc = txsRecvPkt(&pDataPktHdr, false /*fAutoRetryOnFailure*/);
                if (RT_FAILURE(rc))
                    break;

                if (txsIsSameOpcode(pDataPktHdr, "DATA"))
                {
                    size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(uint32_t);
                    if (pDataPktHdr->cb >= cbMin)
                    {
                        size_t      cbData = pDataPktHdr->cb - cbMin;
                        const void *pvData = (const char *)pDataPktHdr + cbMin;
                        uint32_t    uCrc32 = *(uint32_t const *)(pDataPktHdr + 1);

                        uMyCrc32 = RTCrc32Process(uMyCrc32, pvData, cbData);
                        if (RTCrc32Finish(uMyCrc32) == uCrc32)
                        {
                            rc = RTFileWrite(hFile, pvData, cbData, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                rc = txsReplyAck(pDataPktHdr);
                                RTMemFree(pDataPktHdr);
                                continue;
                            }

                            rc = txsReplyRC(pDataPktHdr, rc, "RTFileWrite");
                        }
                        else
                            rc = txsReplyFailure(pDataPktHdr, "BAD DCRC", "mycrc=%#x your=%#x", uMyCrc32, uCrc32);
                    }
                    else
                        rc = txsReplyBadMinSize(pPktHdr, cbMin);
                }
                else if (txsIsSameOpcode(pDataPktHdr, "DATA EOF"))
                {
                    if (pDataPktHdr->cb == sizeof(TXSPKTHDR) + sizeof(uint32_t))
                    {
                        uint32_t    uCrc32 = *(uint32_t const *)(pDataPktHdr + 1);
                        if (RTCrc32Finish(uMyCrc32) == uCrc32)
                        {
                            rc = txsReplyAck(pDataPktHdr);
                            fSuccess = RT_SUCCESS(rc);
                        }
                        else
                            rc = txsReplyFailure(pDataPktHdr, "BAD DCRC", "mycrc=%#x your=%#x", uMyCrc32, uCrc32);
                    }
                    else
                        rc = txsReplyAck(pDataPktHdr);
                }
                else if (txsIsSameOpcode(pDataPktHdr, "ABORT"))
                    rc = txsReplyAck(pDataPktHdr);
                else
                    rc = txsReplyFailure(pDataPktHdr, "UNKNOWN ", "Opcode '%.8s' is not known or not recognized during PUT FILE", pDataPktHdr->achOpcode);
                RTMemFree(pDataPktHdr);
                break;
            }
        }

        RTFileClose(hFile);

        /*
         * Delete the file on failure.
         */
        if (!fSuccess)
            RTFileDelete(pszPath);
    }
    else
        rc = txsReplyRC(pPktHdr, rc, "RTFileOpen(,\"%s\",)", pszPath);

    RTStrFree(pszPath);
    return rc;
}

/**
 * List the entries in the specified directory.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The list packet.
 */
static int txsDoList(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "dir", &pszPath, &rc))
        return rc;

    rc = txsReplyNotImplemented(pPktHdr);

    RTStrFree(pszPath);
    return rc;
}

/**
 * Worker for STAT and LSTAT for packing down the file info reply.
 *
 * @returns IPRT status code from send.
 * @param   pInfo               The info to pack down.
 */
static int txsReplyObjInfo(PCRTFSOBJINFO pInfo)
{
    struct
    {
        TXSPKTHDR   Hdr;
        int64_t     cbObject;
        int64_t     cbAllocated;
        int64_t     nsAccessTime;
        int64_t     nsModificationTime;
        int64_t     nsChangeTime;
        int64_t     nsBirthTime;
        uint32_t    fMode;
        uint32_t    uid;
        uint32_t    gid;
        uint32_t    cHardLinks;
        uint64_t    INodeIdDevice;
        uint64_t    INodeId;
        uint64_t    Device;
        char        abPadding[TXSPKT_ALIGNMENT];
    } Pkt;

    Pkt.cbObject            = pInfo->cbObject;
    Pkt.cbAllocated         = pInfo->cbAllocated;
    Pkt.nsAccessTime        = RTTimeSpecGetNano(&pInfo->AccessTime);
    Pkt.nsModificationTime  = RTTimeSpecGetNano(&pInfo->ModificationTime);
    Pkt.nsChangeTime        = RTTimeSpecGetNano(&pInfo->ChangeTime);
    Pkt.nsBirthTime         = RTTimeSpecGetNano(&pInfo->BirthTime);
    Pkt.fMode               = pInfo->Attr.fMode;
    Pkt.uid                 = pInfo->Attr.u.Unix.uid;
    Pkt.gid                 = pInfo->Attr.u.Unix.gid;
    Pkt.cHardLinks          = pInfo->Attr.u.Unix.cHardlinks;
    Pkt.INodeIdDevice       = pInfo->Attr.u.Unix.INodeIdDevice;
    Pkt.INodeId             = pInfo->Attr.u.Unix.INodeId;
    Pkt.Device              = pInfo->Attr.u.Unix.Device;

    return txsReplyInternal(&Pkt.Hdr, "FILEINFO", sizeof(Pkt) - TXSPKT_ALIGNMENT - sizeof(TXSPKTHDR));
}

/**
 * Get info about a file system object, following all but the symbolic links
 * except in the final path component.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The lstat packet.
 */
static int txsDoLStat(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "path", &pszPath, &rc))
        return rc;

    RTFSOBJINFO Info;
    rc = RTPathQueryInfoEx(pszPath, &Info, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
        rc = txsReplyObjInfo(&Info);
    else
        rc = txsReplyRC(pPktHdr, rc, "RTPathQueryInfoEx(\"%s\",,UNIX,ON_LINK)",  pszPath);

    RTStrFree(pszPath);
    return rc;
}

/**
 * Get info about a file system object, following all symbolic links.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The stat packet.
 */
static int txsDoStat(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "path", &pszPath, &rc))
        return rc;

    RTFSOBJINFO Info;
    rc = RTPathQueryInfoEx(pszPath, &Info, RTFSOBJATTRADD_UNIX, RTPATH_F_FOLLOW_LINK);
    if (RT_SUCCESS(rc))
        rc = txsReplyObjInfo(&Info);
    else
        rc = txsReplyRC(pPktHdr, rc, "RTPathQueryInfoEx(\"%s\",,UNIX,FOLLOW_LINK)",  pszPath);

    RTStrFree(pszPath);
    return rc;
}

/**
 * Checks if the specified path is a symbolic link.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The issymlnk packet.
 */
static int txsDoIsSymlnk(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "symlink", &pszPath, &rc))
        return rc;

    RTFSOBJINFO Info;
    rc = RTPathQueryInfoEx(pszPath, &Info, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc) && RTFS_IS_SYMLINK(Info.Attr.fMode))
        rc = txsReplySimple(pPktHdr, "TRUE    ");
    else
        rc = txsReplySimple(pPktHdr, "FALSE   ");

    RTStrFree(pszPath);
    return rc;
}

/**
 * Checks if the specified path is a file or not.
 *
 * If the final path element is a symbolic link to a file, we'll return
 * FALSE.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The isfile packet.
 */
static int txsDoIsFile(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "dir", &pszPath, &rc))
        return rc;

    RTFSOBJINFO Info;
    rc = RTPathQueryInfoEx(pszPath, &Info, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc) && RTFS_IS_FILE(Info.Attr.fMode))
        rc = txsReplySimple(pPktHdr, "TRUE    ");
    else
        rc = txsReplySimple(pPktHdr, "FALSE   ");

    RTStrFree(pszPath);
    return rc;
}

/**
 * Checks if the specified path is a directory or not.
 *
 * If the final path element is a symbolic link to a directory, we'll return
 * FALSE.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The isdir packet.
 */
static int txsDoIsDir(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "dir", &pszPath, &rc))
        return rc;

    RTFSOBJINFO Info;
    rc = RTPathQueryInfoEx(pszPath, &Info, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc) && RTFS_IS_DIRECTORY(Info.Attr.fMode))
        rc = txsReplySimple(pPktHdr, "TRUE    ");
    else
        rc = txsReplySimple(pPktHdr, "FALSE   ");

    RTStrFree(pszPath);
    return rc;
}

/**
 * Changes the owner of a file, directory or symbolic link.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The chmod packet.
 */
static int txsDoChOwn(PCTXSPKTHDR pPktHdr)
{
#ifdef RT_OS_WINDOWS
    return txsReplyNotImplemented(pPktHdr);
#else
    /* After the packet header follows a 32-bit UID and 32-bit GID, while the
       remainder of the packet is the zero terminated path. */
    size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2;
    if (pPktHdr->cb < cbMin)
        return txsReplyBadMinSize(pPktHdr, cbMin);

    int rc;
    char *pszPath;
    if (!txsIsStringValid(pPktHdr, "path", (const char *)(pPktHdr + 1) + sizeof(uint32_t) * 2, &pszPath, NULL, &rc))
        return rc;

    uint32_t uid = ((uint32_t const *)(pPktHdr + 1))[0];
    uint32_t gid = ((uint32_t const *)(pPktHdr + 1))[1];

    rc = RTPathSetOwnerEx(pszPath, uid, gid, RTPATH_F_ON_LINK);

    rc = txsReplyRC(pPktHdr, rc, "RTPathSetOwnerEx(\"%s\", %u, %u)", pszPath, uid, gid);
    RTStrFree(pszPath);
    return rc;
#endif
}

/**
 * Changes the mode of a file or directory.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The chmod packet.
 */
static int txsDoChMod(PCTXSPKTHDR pPktHdr)
{
    /* After the packet header follows a mode mask and the remainder of
       the packet is the zero terminated file name. */
    size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2;
    if (pPktHdr->cb < cbMin)
        return txsReplyBadMinSize(pPktHdr, cbMin);

    int rc;
    char *pszPath;
    if (!txsIsStringValid(pPktHdr, "path", (const char *)(pPktHdr + 1) + sizeof(RTFMODE), &pszPath, NULL, &rc))
        return rc;

    RTFMODE fMode = *(RTFMODE const *)(pPktHdr + 1);

    rc = RTPathSetMode(pszPath, fMode);

    rc = txsReplyRC(pPktHdr, rc, "RTPathSetMode(\"%s\", %o)", pszPath, fMode);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Removes a directory tree.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The rmtree packet.
 */
static int txsDoRmTree(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "dir", &pszPath, &rc))
        return rc;

    rc = RTDirRemoveRecursive(pszPath, 0 /*fFlags*/);

    rc = txsReplyRC(pPktHdr, rc, "RTDirRemoveRecusive(\"%s\",0)", pszPath);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Removes a symbolic link.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The rmsymlink packet.
 */
static int txsDoRmSymlnk(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "symlink", &pszPath, &rc))
        return rc;

    rc = RTSymlinkDelete(pszPath, 0);

    rc = txsReplyRC(pPktHdr, rc, "RTSymlinkDelete(\"%s\")", pszPath);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Removes a file.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The rmfile packet.
 */
static int txsDoRmFile(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "file", &pszPath, &rc))
        return rc;

    rc = RTFileDelete(pszPath);

    rc = txsReplyRC(pPktHdr, rc, "RTFileDelete(\"%s\")", pszPath);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Removes a directory.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The rmdir packet.
 */
static int txsDoRmDir(PCTXSPKTHDR pPktHdr)
{
    int rc;
    char *pszPath;
    if (!txsIsStringPktValid(pPktHdr, "dir", &pszPath, &rc))
        return rc;

    rc = RTDirRemove(pszPath);

    rc = txsReplyRC(pPktHdr, rc, "RTDirRemove(\"%s\")", pszPath);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Creates a symbolic link.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The mksymlnk packet.
 */
static int txsDoMkSymlnk(PCTXSPKTHDR pPktHdr)
{
    return txsReplyNotImplemented(pPktHdr);
}

/**
 * Creates a directory and all its parents.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The mkdir -p packet.
 */
static int txsDoMkDrPath(PCTXSPKTHDR pPktHdr)
{
    /* The same format as the MKDIR command. */
    if (pPktHdr->cb < sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2)
        return txsReplyBadMinSize(pPktHdr, sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2);

    int     rc;
    char   *pszPath;
    if (!txsIsStringValid(pPktHdr, "dir", (const char *)(pPktHdr + 1) + sizeof(RTFMODE), &pszPath, NULL, &rc))
        return rc;

    RTFMODE fMode = *(RTFMODE const *)(pPktHdr + 1);

    rc = RTDirCreateFullPathEx(pszPath, fMode, RTDIRCREATE_FLAGS_IGNORE_UMASK);

    rc = txsReplyRC(pPktHdr, rc, "RTDirCreateFullPath(\"%s\", %#x)", pszPath, fMode);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Creates a directory.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The mkdir packet.
 */
static int txsDoMkDir(PCTXSPKTHDR pPktHdr)
{
    /* After the packet header follows a mode mask and the remainder of
       the packet is the zero terminated directory name. */
    size_t const cbMin = sizeof(TXSPKTHDR) + sizeof(RTFMODE) + 2;
    if (pPktHdr->cb < cbMin)
        return txsReplyBadMinSize(pPktHdr, cbMin);

    int     rc;
    char   *pszPath;
    if (!txsIsStringValid(pPktHdr, "dir", (const char *)(pPktHdr + 1) + sizeof(RTFMODE), &pszPath, NULL, &rc))
        return rc;

    RTFMODE fMode = *(RTFMODE const *)(pPktHdr + 1);
    rc = RTDirCreate(pszPath, fMode, RTDIRCREATE_FLAGS_IGNORE_UMASK);

    rc = txsReplyRC(pPktHdr, rc, "RTDirCreate(\"%s\", %#x)", pszPath, fMode);
    RTStrFree(pszPath);
    return rc;
}

/**
 * Cleans up the scratch area.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The shutdown packet.
 */
static int txsDoCleanup(PCTXSPKTHDR pPktHdr)
{
    int rc = RTDirRemoveRecursive(g_szScratchPath, RTDIRRMREC_F_CONTENT_ONLY);
    return txsReplyRC(pPktHdr, rc, "RTDirRemoveRecursive(\"%s\", CONTENT_ONLY)", g_szScratchPath);
}

/**
 * Ejects the specified DVD/CD drive.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The eject packet.
 */
static int txsDoCdEject(PCTXSPKTHDR pPktHdr)
{
    /* After the packet header follows a uint32_t ordinal. */
    size_t const cbExpected = sizeof(TXSPKTHDR) + sizeof(uint32_t);
    if (pPktHdr->cb != cbExpected)
        return txsReplyBadSize(pPktHdr, cbExpected);
    uint32_t iOrdinal = *(uint32_t const *)(pPktHdr + 1);

    RTCDROM hCdrom;
    int rc = RTCdromOpenByOrdinal(iOrdinal, RTCDROM_O_CONTROL, &hCdrom);
    if (RT_FAILURE(rc))
        return txsReplyRC(pPktHdr, rc, "RTCdromOpenByOrdinal(%u, RTCDROM_O_CONTROL, )", iOrdinal);
    rc = RTCdromEject(hCdrom, true /*fForce*/);
    RTCdromRelease(hCdrom);

    return txsReplyRC(pPktHdr, rc, "RTCdromEject(ord=%u, fForce=true)", iOrdinal);
}

/**
 * Common worker for txsDoShutdown and txsDoReboot.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The reboot packet.
 * @param   fAction             Which action to take.
 */
static int txsCommonShutdownReboot(PCTXSPKTHDR pPktHdr, uint32_t fAction)
{
    /*
     * We ACK the reboot & shutdown before actually performing them, then we
     * terminate the transport layer.
     *
     * This is to make sure the client isn't stuck with a dead connection. The
     * transport layer termination also make sure we won't accept new
     * connections in case the client is too eager to reconnect to a rebooted
     * test victim.  On the down side, we cannot easily report RTSystemShutdown
     * failures failures this way.  But the client can kind of figure it out by
     * reconnecting and seeing that our UUID was unchanged.
     */
    if (pPktHdr->cb != sizeof(TXSPKTHDR))
        return txsReplyBadSize(pPktHdr, sizeof(TXSPKTHDR));
    g_pTransport->pfnNotifyReboot();
    int rc = txsReplyAck(pPktHdr);
    AssertRCReturn(rc, rc);
    RTThreadSleep(2560);                /* fudge factor */
    g_pTransport->pfnTerm();

    /*
     * Do the job, if it fails we'll restart the transport layer.
     */
#if 0
    rc = VINF_SUCCESS;
#else
    rc = RTSystemShutdown(0 /*cMsDelay*/,
                          fAction | RTSYSTEM_SHUTDOWN_PLANNED | RTSYSTEM_SHUTDOWN_FORCE,
                          "Test Execution Service");
#endif
    if (RT_SUCCESS(rc))
    {
        RTMsgInfo(fAction == RTSYSTEM_SHUTDOWN_REBOOT ? "Rebooting...\n" : "Shutting down...\n");
        g_fTerminate = true;
    }
    else
    {
        RTMsgError("RTSystemShutdown w/ fAction=%#x failed: %Rrc", fAction, rc);

        int rc2 = g_pTransport->pfnInit();
        if (RT_FAILURE(rc2))
        {
            g_fTerminate = true;
            rc = rc2;
        }
    }
    return rc;
}

/**
 * Shuts down the machine, powering it off if possible.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The shutdown packet.
 */
static int txsDoShutdown(PCTXSPKTHDR pPktHdr)
{
    return txsCommonShutdownReboot(pPktHdr, RTSYSTEM_SHUTDOWN_POWER_OFF_HALT);
}

/**
 * Reboots the machine.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The reboot packet.
 */
static int txsDoReboot(PCTXSPKTHDR pPktHdr)
{
    return txsCommonShutdownReboot(pPktHdr, RTSYSTEM_SHUTDOWN_REBOOT);
}

/**
 * Verifies and acknowledges a "UUID" request.
 *
 * @returns IPRT status code.
 * @param   pPktHdr             The UUID packet.
 */
static int txsDoUuid(PCTXSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(TXSPKTHDR))
        return txsReplyBadSize(pPktHdr, sizeof(TXSPKTHDR));

    struct
    {
        TXSPKTHDR   Hdr;
        char        szUuid[RTUUID_STR_LENGTH];
        char        abPadding[TXSPKT_ALIGNMENT];
    } Pkt;

    int rc = RTUuidToStr(&g_InstanceUuid, Pkt.szUuid, sizeof(Pkt.szUuid));
    if (RT_FAILURE(rc))
        return txsReplyRC(pPktHdr, rc, "RTUuidToStr");
    return txsReplyInternal(&Pkt.Hdr, "ACK UUID", strlen(Pkt.szUuid) + 1);
}

/**
 * Verifies and acknowledges a "BYE" request.
 *
 * @returns IPRT status code.
 * @param   pPktHdr             The bye packet.
 */
static int txsDoBye(PCTXSPKTHDR pPktHdr)
{
    int rc;
    if (pPktHdr->cb == sizeof(TXSPKTHDR))
        rc = txsReplyAck(pPktHdr);
    else
        rc = txsReplyBadSize(pPktHdr, sizeof(TXSPKTHDR));
    g_pTransport->pfnNotifyBye();
    return rc;
}

/**
 * Verifies and acknowledges a "VER" request.
 *
 * @returns IPRT status code.
 * @param   pPktHdr             The version packet.
 */
static int txsDoVer(PCTXSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(TXSPKTHDR))
        return txsReplyBadSize(pPktHdr, sizeof(TXSPKTHDR));

    struct
    {
        TXSPKTHDR   Hdr;
        char        szVer[96];
        char        abPadding[TXSPKT_ALIGNMENT];
    } Pkt;

    if (RTStrPrintf2(Pkt.szVer, sizeof(Pkt.szVer), "%s r%s %s.%s (%s %s)",
                     RTBldCfgVersion(), RTBldCfgRevisionStr(), KBUILD_TARGET, KBUILD_TARGET_ARCH, __DATE__, __TIME__) > 0)
    {
        return txsReplyInternal(&Pkt.Hdr, "ACK VER ", strlen(Pkt.szVer) + 1);
    }

    return txsReplyRC(pPktHdr, VERR_BUFFER_OVERFLOW, "RTStrPrintf2");
}

/**
 * Verifies and acknowledges a "HOWDY" request.
 *
 * @returns IPRT status code.
 * @param   pPktHdr             The howdy packet.
 */
static int txsDoHowdy(PCTXSPKTHDR pPktHdr)
{
    if (pPktHdr->cb != sizeof(TXSPKTHDR))
        return txsReplyBadSize(pPktHdr, sizeof(TXSPKTHDR));
    int rc = txsReplyAck(pPktHdr);
    if (RT_SUCCESS(rc))
    {
        g_pTransport->pfnNotifyHowdy();
        RTDirRemoveRecursive(g_szScratchPath, RTDIRRMREC_F_CONTENT_ONLY);
    }
    return rc;
}

/**
 * Replies according to the return code.
 *
 * @returns rcOperation and pTxsExec->rcReplySend.
 * @param   pTxsExec            The TXSEXEC instance.
 * @param   rcOperation         The status code to report.
 * @param   pszOperationFmt     The operation that failed.  Typically giving the
 *                              function call with important arguments.
 * @param   ...                 Arguments to the format string.
 */
static int txsExecReplyRC(PTXSEXEC pTxsExec, int rcOperation, const char *pszOperationFmt, ...)
{
    AssertStmt(RT_FAILURE_NP(rcOperation), rcOperation = VERR_IPE_UNEXPECTED_INFO_STATUS);

    char    szOperation[128];
    va_list va;
    va_start(va, pszOperationFmt);
    RTStrPrintfV(szOperation, sizeof(szOperation), pszOperationFmt, va);
    va_end(va);

    pTxsExec->rcReplySend = txsReplyFailure(pTxsExec->pPktHdr, "FAILED  ",
                                            "%s failed with rc=%Rrc (opcode '%.8s')",
                                            szOperation, rcOperation, pTxsExec->pPktHdr->achOpcode);
    return rcOperation;
}


/**
 * Sends the process exit status reply to the TXS client.
 *
 * @returns IPRT status code of the send.
 * @param   pTxsExec            The TXSEXEC instance.
 * @param   fProcessAlive       Whether the process is still alive (against our
 *                              will).
 * @param   fProcessTimedOut    Whether the process timed out.
 * @param   MsProcessKilled     When the process was killed, UINT64_MAX if not.
 */
static int txsExecSendExitStatus(PTXSEXEC pTxsExec, bool fProcessAlive, bool fProcessTimedOut, uint64_t MsProcessKilled)
{
    int rc;
    if (     fProcessTimedOut  && !fProcessAlive && MsProcessKilled != UINT64_MAX)
    {
        rc = txsReplySimple(pTxsExec->pPktHdr, "PROC TOK");
        if (g_fDisplayOutput)
            RTPrintf("txs: Process timed out and was killed\n");
    }
    else if (fProcessTimedOut  &&  fProcessAlive && MsProcessKilled != UINT64_MAX)
    {
        rc = txsReplySimple(pTxsExec->pPktHdr, "PROC TOA");
        if (g_fDisplayOutput)
            RTPrintf("txs: Process timed out and was not killed successfully\n");
    }
    else if (g_fTerminate && (fProcessAlive || MsProcessKilled != UINT64_MAX))
        rc = txsReplySimple(pTxsExec->pPktHdr, "PROC DWN");
    else if (fProcessAlive)
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC DOO", "Doofus! process is alive when it should not");
        AssertFailed();
    }
    else if (MsProcessKilled != UINT64_MAX)
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC DOO", "Doofus! process has been killed when it should not");
        AssertFailed();
    }
    else if (   pTxsExec->ProcessStatus.enmReason == RTPROCEXITREASON_NORMAL
             && pTxsExec->ProcessStatus.iStatus   == 0)
    {
        rc = txsReplySimple(pTxsExec->pPktHdr, "PROC OK ");
        if (g_fDisplayOutput)
            RTPrintf("txs: Process exited with status: 0\n");
    }
    else if (pTxsExec->ProcessStatus.enmReason == RTPROCEXITREASON_NORMAL)
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC NOK", "%d", pTxsExec->ProcessStatus.iStatus);
        if (g_fDisplayOutput)
            RTPrintf("txs: Process exited with status: %d\n", pTxsExec->ProcessStatus.iStatus);
    }
    else if (pTxsExec->ProcessStatus.enmReason == RTPROCEXITREASON_SIGNAL)
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC SIG", "%d", pTxsExec->ProcessStatus.iStatus);
        if (g_fDisplayOutput)
            RTPrintf("txs: Process exited with status: signal %d\n", pTxsExec->ProcessStatus.iStatus);
    }
    else if (pTxsExec->ProcessStatus.enmReason == RTPROCEXITREASON_ABEND)
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC ABD", "");
        if (g_fDisplayOutput)
            RTPrintf("txs: Process exited with status: abend\n");
    }
    else
    {
        rc = txsReplyFailure(pTxsExec->pPktHdr, "PROC DOO", "enmReason=%d iStatus=%d",
                             pTxsExec->ProcessStatus.enmReason, pTxsExec->ProcessStatus.iStatus);
        AssertMsgFailed(("enmReason=%d iStatus=%d", pTxsExec->ProcessStatus.enmReason, pTxsExec->ProcessStatus.iStatus));
    }
    return rc;
}

/**
 * Handle pending output data or error on standard out, standard error or the
 * test pipe.
 *
 * @returns IPRT status code from client send.
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phPipeR             The pipe handle.
 * @param   puCrc32             The current CRC-32 of the stream. (In/Out)
 * @param   enmHndId            The handle ID.
 * @param   pszOpcode           The opcode for the data upload.
 *
 * @todo    Put the last 4 parameters into a struct!
 */
static int txsDoExecHlpHandleOutputEvent(RTPOLLSET hPollSet, uint32_t fPollEvt, PRTPIPE phPipeR,
                                         uint32_t *puCrc32, TXSEXECHNDID enmHndId, const char *pszOpcode)
{
    Log(("txsDoExecHlpHandleOutputEvent: %s fPollEvt=%#x\n", pszOpcode, fPollEvt));

    /*
     * Try drain the pipe before acting on any errors.
     */
    int rc = VINF_SUCCESS;
    struct
    {
        TXSPKTHDR   Hdr;
        uint32_t    uCrc32;
        char        abBuf[_64K];
        char        abPadding[TXSPKT_ALIGNMENT];
    }       Pkt;
    size_t  cbRead;
    int     rc2 = RTPipeRead(*phPipeR, Pkt.abBuf, sizeof(Pkt.abBuf), &cbRead);
    if (RT_SUCCESS(rc2) && cbRead)
    {
        Log(("Crc32=%#x ", *puCrc32));
        *puCrc32 = RTCrc32Process(*puCrc32, Pkt.abBuf, cbRead);
        Log(("cbRead=%#x Crc32=%#x \n", cbRead, *puCrc32));
        Pkt.uCrc32 = RTCrc32Finish(*puCrc32);
        if (g_fDisplayOutput)
        {
            if (enmHndId == TXSEXECHNDID_STDOUT)
                RTStrmPrintf(g_pStdErr, "%.*s", cbRead, Pkt.abBuf);
            else if (enmHndId == TXSEXECHNDID_STDERR)
                RTStrmPrintf(g_pStdErr, "%.*s", cbRead, Pkt.abBuf);
        }

        rc = txsReplyInternal(&Pkt.Hdr, pszOpcode, cbRead + sizeof(uint32_t));

        /* Make sure we go another poll round in case there was too much data
           for the buffer to hold. */
        fPollEvt &= RTPOLL_EVT_ERROR;
    }
    else if (RT_FAILURE(rc2))
    {
        fPollEvt |= RTPOLL_EVT_ERROR;
        AssertMsg(rc2 == VERR_BROKEN_PIPE, ("%Rrc\n", rc));
    }

    /*
     * If an error was raised signalled,
     */
    if (fPollEvt & RTPOLL_EVT_ERROR)
    {
        rc2 = RTPollSetRemove(hPollSet, enmHndId);
        AssertRC(rc2);

        rc2 = RTPipeClose(*phPipeR);
        AssertRC(rc2);
        *phPipeR = NIL_RTPIPE;
    }
    return rc;
}

/**
 * Try write some more data to the standard input of the child.
 *
 * @returns IPRT status code.
 * @param   pStdInBuf           The standard input buffer.
 * @param   hStdInW             The standard input pipe.
 */
static int txsDoExecHlpWriteStdIn(PTXSEXECSTDINBUF pStdInBuf, RTPIPE hStdInW)
{
    size_t  cbToWrite = pStdInBuf->cb - pStdInBuf->off;
    size_t  cbWritten;
    int     rc = RTPipeWrite(hStdInW, &pStdInBuf->pch[pStdInBuf->off], cbToWrite, &cbWritten);
    if (RT_SUCCESS(rc))
    {
        Assert(cbWritten == cbToWrite);
        pStdInBuf->off += cbWritten;
    }
    return rc;
}

/**
 * Handle an error event on standard input.
 *
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phStdInW            The standard input pipe handle.
 * @param   pStdInBuf           The standard input buffer.
 */
static void txsDoExecHlpHandleStdInErrorEvent(RTPOLLSET hPollSet, uint32_t fPollEvt, PRTPIPE phStdInW,
                                              PTXSEXECSTDINBUF pStdInBuf)
{
    NOREF(fPollEvt);
    int rc2;
    if (pStdInBuf->off < pStdInBuf->cb)
    {
        rc2 = RTPollSetRemove(hPollSet, TXSEXECHNDID_STDIN_WRITABLE);
        AssertRC(rc2);
    }

    rc2 = RTPollSetRemove(hPollSet, TXSEXECHNDID_STDIN);
    AssertRC(rc2);

    rc2 = RTPipeClose(*phStdInW);
    AssertRC(rc2);
    *phStdInW = NIL_RTPIPE;

    RTMemFree(pStdInBuf->pch);
    pStdInBuf->pch          = NULL;
    pStdInBuf->off          = 0;
    pStdInBuf->cb           = 0;
    pStdInBuf->cbAllocated  = 0;
    pStdInBuf->fBitBucket   = true;
}

/**
 * Handle an event indicating we can write to the standard input pipe of the
 * child process.
 *
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phStdInW            The standard input pipe.
 * @param   pStdInBuf           The standard input buffer.
 */
static void txsDoExecHlpHandleStdInWritableEvent(RTPOLLSET hPollSet, uint32_t fPollEvt, PRTPIPE phStdInW,
                                                 PTXSEXECSTDINBUF pStdInBuf)
{
    int rc;
    if (!(fPollEvt & RTPOLL_EVT_ERROR))
    {
        rc = txsDoExecHlpWriteStdIn(pStdInBuf, *phStdInW);
        if (RT_FAILURE(rc) && rc != VERR_BAD_PIPE)
        {
            /** @todo do we need to do something about this error condition? */
            AssertRC(rc);
        }

        if (pStdInBuf->off < pStdInBuf->cb)
        {
            rc = RTPollSetRemove(hPollSet, TXSEXECHNDID_STDIN_WRITABLE);
            AssertRC(rc);
        }
    }
    else
        txsDoExecHlpHandleStdInErrorEvent(hPollSet, fPollEvt, phStdInW, pStdInBuf);
}

/**
 * Handle a transport event or successful pfnPollIn() call.
 *
 * @returns IPRT status code from client send.
 * @retval  VINF_EOF indicates ABORT command.
 *
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   idPollHnd           The handle ID.
 * @param   phStdInW            The standard input pipe.
 * @param   pStdInBuf           The standard input buffer.
 */
static int txsDoExecHlpHandleTransportEvent(RTPOLLSET hPollSet, uint32_t fPollEvt, uint32_t idPollHnd,
                                            PRTPIPE phStdInW, PTXSEXECSTDINBUF pStdInBuf)
{
    /* ASSUMES the transport layer will detect or clear any error condition. */
    NOREF(fPollEvt); NOREF(idPollHnd);
    Log(("txsDoExecHlpHandleTransportEvent\n"));
    /** @todo Use a callback for this case? */

    /*
     * Read client command packet and process it.
     */
    /** @todo Sometimes this hangs on windows because there isn't any data pending.
     * We probably get woken up at the wrong time or in the wrong way, i.e. RTPoll()
     * is busted for sockets.
     *
     * Temporary workaround: Poll for input before trying to read it. */
    if (!g_pTransport->pfnPollIn())
    {
        Log(("Bad transport event\n"));
        RTThreadYield();
        return VINF_SUCCESS;
    }
    PTXSPKTHDR pPktHdr;
    int rc = txsRecvPkt(&pPktHdr, false /*fAutoRetryOnFailure*/);
    if (RT_FAILURE(rc))
        return rc;
    Log(("Bad transport event\n"));

    /*
     * The most common thing here would be a STDIN request with data
     * for the child process.
     */
    if (txsIsSameOpcode(pPktHdr, "STDIN"))
    {
        if (   !pStdInBuf->fBitBucket
            && pPktHdr->cb >= sizeof(TXSPKTHDR) + sizeof(uint32_t))
        {
            uint32_t    uCrc32  = *(uint32_t *)(pPktHdr + 1);
            const char *pch     = (const char *)(pPktHdr + 1) + sizeof(uint32_t);
            size_t      cb      = pPktHdr->cb - sizeof(TXSPKTHDR) - sizeof(uint32_t);

            /* Check the CRC */
            pStdInBuf->uCrc32 = RTCrc32Process(pStdInBuf->uCrc32, pch, cb);
            if (RTCrc32Finish(pStdInBuf->uCrc32) == uCrc32)
            {

                /* Rewind the buffer if it's empty. */
                size_t      cbInBuf   = pStdInBuf->cb - pStdInBuf->off;
                bool const  fAddToSet = cbInBuf == 0;
                if (fAddToSet)
                    pStdInBuf->cb = pStdInBuf->off = 0;

                /* Try and see if we can simply append the data. */
                if (cb + pStdInBuf->cb <= pStdInBuf->cbAllocated)
                {
                    memcpy(&pStdInBuf->pch[pStdInBuf->cb], pch, cb);
                    pStdInBuf->cb += cb;
                    rc = txsReplyAck(pPktHdr);
                }
                else
                {
                    /* Try write a bit or two before we move+realloc the buffer. */
                    if (cbInBuf > 0)
                        txsDoExecHlpWriteStdIn(pStdInBuf, *phStdInW);

                    /* Move any buffered data to the front. */
                    cbInBuf = pStdInBuf->cb - pStdInBuf->off;
                    if (cbInBuf == 0)
                        pStdInBuf->cb = pStdInBuf->off = 0;
                    else
                    {
                        memmove(pStdInBuf->pch, &pStdInBuf->pch[pStdInBuf->off], cbInBuf);
                        pStdInBuf->cb  = cbInBuf;
                        pStdInBuf->off = 0;
                    }

                    /* Do we need to grow the buffer? */
                    if (cb + pStdInBuf->cb > pStdInBuf->cbAllocated)
                    {
                        size_t cbAlloc = pStdInBuf->cb + cb;
                        cbAlloc = RT_ALIGN_Z(cbAlloc, _64K);
                        void *pvNew = RTMemRealloc(pStdInBuf->pch, cbAlloc);
                        if (pvNew)
                        {
                            pStdInBuf->pch         = (char *)pvNew;
                            pStdInBuf->cbAllocated = cbAlloc;
                        }
                    }

                    /* Finally, copy the data. */
                    if (cb + pStdInBuf->cb <= pStdInBuf->cbAllocated)
                    {
                        memcpy(&pStdInBuf->pch[pStdInBuf->cb], pch, cb);
                        pStdInBuf->cb += cb;
                        rc = txsReplyAck(pPktHdr);
                    }
                    else
                        rc = txsReplySimple(pPktHdr, "STDINMEM");
                }

                /*
                 * Flush the buffered data and add/remove the standard input
                 * handle from the set.
                 */
                txsDoExecHlpWriteStdIn(pStdInBuf, *phStdInW);
                if (fAddToSet && pStdInBuf->off < pStdInBuf->cb)
                {
                    int rc2 = RTPollSetAddPipe(hPollSet, *phStdInW, RTPOLL_EVT_WRITE, TXSEXECHNDID_STDIN_WRITABLE);
                    AssertRC(rc2);
                }
                else if (!fAddToSet && pStdInBuf->off >= pStdInBuf->cb)
                {
                    int rc2 = RTPollSetRemove(hPollSet, TXSEXECHNDID_STDIN_WRITABLE);
                    AssertRC(rc2);
                }
            }
            else
                rc = txsReplyFailure(pPktHdr, "STDINCRC", "Invalid CRC checksum expected %#x got %#x",
                                     pStdInBuf->uCrc32, uCrc32);
        }
        else if (pPktHdr->cb < sizeof(TXSPKTHDR) + sizeof(uint32_t))
            rc = txsReplySimple(pPktHdr, "STDINBAD");
        else
            rc = txsReplySimple(pPktHdr, "STDINIGN");
    }
    /*
     * Marks the end of the stream for stdin.
     */
    else if (txsIsSameOpcode(pPktHdr, "STDINEOS"))
    {
        if (RT_LIKELY(pPktHdr->cb == sizeof(TXSPKTHDR)))
        {
            /* Close the pipe. */
            txsDoExecHlpHandleStdInErrorEvent(hPollSet, fPollEvt, phStdInW, pStdInBuf);
            rc = txsReplyAck(pPktHdr);
        }
        else
            rc = txsReplySimple(pPktHdr, "STDINBAD");
    }
    /*
     * The only other two requests are connection oriented and we return a error
     * code so that we unwind the whole EXEC shebang and start afresh.
     */
    else if (txsIsSameOpcode(pPktHdr, "BYE"))
    {
        rc = txsDoBye(pPktHdr);
        if (RT_SUCCESS(rc))
            rc = VERR_NET_NOT_CONNECTED;
    }
    else if (txsIsSameOpcode(pPktHdr, "HOWDY"))
    {
        rc = txsDoHowdy(pPktHdr);
        if (RT_SUCCESS(rc))
            rc = VERR_NET_NOT_CONNECTED;
    }
    else if (txsIsSameOpcode(pPktHdr, "ABORT"))
    {
        rc = txsReplyAck(pPktHdr);
        if (RT_SUCCESS(rc))
            rc = VINF_EOF;              /* this is but ugly! */
    }
    else
        rc = txsReplyFailure(pPktHdr, "UNKNOWN ", "Opcode '%.8s' is not known or not recognized during EXEC", pPktHdr->achOpcode);

    RTMemFree(pPktHdr);
    return rc;
}

/**
 * Handles the output and input of the process, waits for it finish up.
 *
 * @returns IPRT status code from reply send.
 * @param   pTxsExec            The TXSEXEC instance.
 */
static int txsDoExecHlp2(PTXSEXEC pTxsExec)
{
    int                 rc;             /* client send. */
    int                 rc2;
    TXSEXECSTDINBUF     StdInBuf            = { 0, 0, NULL, 0, pTxsExec->hStdInW == NIL_RTPIPE, RTCrc32Start() };
    uint32_t            uStdOutCrc32        = RTCrc32Start();
    uint32_t            uStdErrCrc32        = uStdOutCrc32;
    uint32_t            uTestPipeCrc32      = uStdOutCrc32;
    uint64_t const      MsStart             = RTTimeMilliTS();
    bool                fProcessTimedOut    = false;
    uint64_t            MsProcessKilled     = UINT64_MAX;
    RTMSINTERVAL const  cMsPollBase         = g_pTransport->pfnPollSetAdd || pTxsExec->hStdInW == NIL_RTPIPE
                                            ? RT_MS_5SEC : 100;
    RTMSINTERVAL        cMsPollCur          = 0;

    /*
     * Before entering the loop, tell the client that we've started the guest
     * and that it's now OK to send input to the process.  (This is not the
     * final ACK, so the packet header is NULL ... kind of bogus.)
     */
    rc = txsReplyAck(NULL);

    /*
     * Process input, output, the test pipe and client requests.
     */
    while (   RT_SUCCESS(rc)
           && RT_UNLIKELY(!g_fTerminate))
    {
        /*
         * Wait/Process all pending events.
         */
        uint32_t idPollHnd;
        uint32_t fPollEvt;
        Log3(("Calling RTPollNoResume(,%u,)...\n", cMsPollCur));
        rc2 = RTPollNoResume(pTxsExec->hPollSet, cMsPollCur, &fPollEvt, &idPollHnd);
        Log3(("RTPollNoResume -> fPollEvt=%#x idPollHnd=%u\n", fPollEvt, idPollHnd));
        if (g_fTerminate)
            continue;
        cMsPollCur = 0;                 /* no rest until we've checked everything. */

        if (RT_SUCCESS(rc2))
        {
            switch (idPollHnd)
            {
                case TXSEXECHNDID_STDOUT:
                    rc = txsDoExecHlpHandleOutputEvent(pTxsExec->hPollSet, fPollEvt, &pTxsExec->hStdOutR, &uStdOutCrc32,
                                                       TXSEXECHNDID_STDOUT, "STDOUT  ");
                    break;

                case TXSEXECHNDID_STDERR:
                    rc = txsDoExecHlpHandleOutputEvent(pTxsExec->hPollSet, fPollEvt, &pTxsExec->hStdErrR, &uStdErrCrc32,
                                                       TXSEXECHNDID_STDERR, "STDERR  ");
                    break;

                case TXSEXECHNDID_TESTPIPE:
                    rc = txsDoExecHlpHandleOutputEvent(pTxsExec->hPollSet, fPollEvt, &pTxsExec->hTestPipeR, &uTestPipeCrc32,
                                                       TXSEXECHNDID_TESTPIPE, "TESTPIPE");
                    break;

                case TXSEXECHNDID_STDIN:
                    txsDoExecHlpHandleStdInErrorEvent(pTxsExec->hPollSet, fPollEvt, &pTxsExec->hStdInW, &StdInBuf);
                    break;

                case TXSEXECHNDID_STDIN_WRITABLE:
                    txsDoExecHlpHandleStdInWritableEvent(pTxsExec->hPollSet, fPollEvt, &pTxsExec->hStdInW, &StdInBuf);
                    break;

                case TXSEXECHNDID_THREAD:
                    rc2 = RTPollSetRemove(pTxsExec->hPollSet, TXSEXECHNDID_THREAD); AssertRC(rc2);
                    break;

                default:
                    rc = txsDoExecHlpHandleTransportEvent(pTxsExec->hPollSet, fPollEvt, idPollHnd, &pTxsExec->hStdInW,
                                                          &StdInBuf);
                    break;
            }
            if (RT_FAILURE(rc) || rc == VINF_EOF)
                break; /* abort command, or client dead or something */
            continue;
        }

        /*
         * Check for incoming data.
         */
        if (g_pTransport->pfnPollIn())
        {
            rc = txsDoExecHlpHandleTransportEvent(pTxsExec->hPollSet, 0, UINT32_MAX, &pTxsExec->hStdInW, &StdInBuf);
            if (RT_FAILURE(rc) || rc == VINF_EOF)
                break; /* abort command, or client dead or something */
            continue;
        }

        /*
         * If the process has terminated, we're should head out.
         */
        if (!ASMAtomicReadBool(&pTxsExec->fProcessAlive))
            break;

        /*
         * Check for timed out, killing the process.
         */
        uint32_t cMilliesLeft = RT_INDEFINITE_WAIT;
        if (pTxsExec->cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t u64Now = RTTimeMilliTS();
            uint64_t cMsElapsed = u64Now - MsStart;
            if (cMsElapsed >= pTxsExec->cMsTimeout)
            {
                fProcessTimedOut = true;
                if (    MsProcessKilled == UINT64_MAX
                    ||  u64Now - MsProcessKilled > RT_MS_1SEC)
                {
                    if (   MsProcessKilled != UINT64_MAX
                        && u64Now - MsProcessKilled > 20*RT_MS_1MIN)
                        break; /* give up after 20 mins */
                    RTCritSectEnter(&pTxsExec->CritSect);
                    if (pTxsExec->fProcessAlive)
                        RTProcTerminate(pTxsExec->hProcess);
                    RTCritSectLeave(&pTxsExec->CritSect);
                    MsProcessKilled = u64Now;
                    continue;
                }
                cMilliesLeft = RT_MS_10SEC;
            }
            else
                cMilliesLeft = pTxsExec->cMsTimeout - (uint32_t)cMsElapsed;
        }

        /* Reset the polling interval since we've done all pending work. */
        cMsPollCur = cMilliesLeft >= cMsPollBase ? cMsPollBase : cMilliesLeft;
    }

    /*
     * At this point we should hopefully only have to wait 0 ms on the thread
     * to release the handle... But if for instance the process refuses to die,
     * we'll have to try kill it again. Bothersome.
     */
    for (size_t i = 0; i < 22; i++)
    {
        rc2 = RTThreadWait(pTxsExec->hThreadWaiter, RT_MS_1SEC / 2, NULL);
        if (RT_SUCCESS(rc2))
        {
            pTxsExec->hThreadWaiter = NIL_RTTHREAD;
            Assert(!pTxsExec->fProcessAlive);
            break;
        }
        if (i == 0 || i == 10 || i == 15 || i == 18 || i > 20)
        {
            RTCritSectEnter(&pTxsExec->CritSect);
            if (pTxsExec->fProcessAlive)
                RTProcTerminate(pTxsExec->hProcess);
            RTCritSectLeave(&pTxsExec->CritSect);
        }
    }

    /*
     * If we don't have a client problem (RT_FAILURE(rc) we'll reply to the
     * clients exec packet now.
     */
    if (RT_SUCCESS(rc))
        rc = txsExecSendExitStatus(pTxsExec, pTxsExec->fProcessAlive, fProcessTimedOut, MsProcessKilled);

    RTMemFree(StdInBuf.pch);
    return rc;
}

/**
 * Creates a poll set for the pipes and let the transport layer add stuff to it
 * as well.
 *
 * @returns IPRT status code, reply to client made on error.
 * @param   pTxsExec            The TXSEXEC instance.
 */
static int txsExecSetupPollSet(PTXSEXEC pTxsExec)
{
    int rc = RTPollSetCreate(&pTxsExec->hPollSet);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetCreate");

    rc = RTPollSetAddPipe(pTxsExec->hPollSet, pTxsExec->hStdInW, RTPOLL_EVT_ERROR, TXSEXECHNDID_STDIN);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetAddPipe/stdin");

    rc = RTPollSetAddPipe(pTxsExec->hPollSet, pTxsExec->hStdOutR,   RTPOLL_EVT_READ | RTPOLL_EVT_ERROR,
                          TXSEXECHNDID_STDOUT);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetAddPipe/stdout");

    rc = RTPollSetAddPipe(pTxsExec->hPollSet, pTxsExec->hStdErrR,   RTPOLL_EVT_READ | RTPOLL_EVT_ERROR,
                          TXSEXECHNDID_STDERR);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetAddPipe/stderr");

    rc = RTPollSetAddPipe(pTxsExec->hPollSet, pTxsExec->hTestPipeR, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR,
                          TXSEXECHNDID_TESTPIPE);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetAddPipe/test");

    rc = RTPollSetAddPipe(pTxsExec->hPollSet, pTxsExec->hWakeUpPipeR, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR,
                          TXSEXECHNDID_THREAD);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTPollSetAddPipe/wakeup");

    if (g_pTransport->pfnPollSetAdd)
    {
        rc = g_pTransport->pfnPollSetAdd(pTxsExec->hPollSet, TXSEXECHNDID_TRANSPORT);
        if (RT_FAILURE(rc))
            return txsExecReplyRC(pTxsExec, rc, "%s->pfnPollSetAdd/stdin", g_pTransport->szName);
    }

    return VINF_SUCCESS;
}

/**
 * Thread that calls RTProcWait and signals the main thread when it returns.
 *
 * The thread is created before the process is started and is waiting for a user
 * signal from the main thread before it calls RTProcWait.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   hThreadSelf         The thread handle.
 * @param   pvUser              The TXEEXEC structure.
 */
static DECLCALLBACK(int) txsExecWaitThreadProc(RTTHREAD hThreadSelf, void *pvUser)
{
    PTXSEXEC pTxsExec = (PTXSEXEC)pvUser;

    /* Wait for the go ahead... */
    int rc = RTThreadUserWait(hThreadSelf, RT_INDEFINITE_WAIT); AssertRC(rc);

    RTCritSectEnter(&pTxsExec->CritSect);
    for (;;)
    {
        RTCritSectLeave(&pTxsExec->CritSect);
        rc = RTProcWaitNoResume(pTxsExec->hProcess, RTPROCWAIT_FLAGS_BLOCK, &pTxsExec->ProcessStatus);
        RTCritSectEnter(&pTxsExec->CritSect);

        /* If the pipe is NIL, the destructor wants us to get lost ASAP. */
        if (pTxsExec->hWakeUpPipeW == NIL_RTPIPE)
            break;

        if (RT_FAILURE(rc))
        {
            rc = RTProcWait(pTxsExec->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &pTxsExec->ProcessStatus);
            if (rc == VERR_PROCESS_RUNNING)
                continue;

            if (RT_FAILURE(rc))
            {
                AssertRC(rc);
                pTxsExec->ProcessStatus.iStatus   = rc;
                pTxsExec->ProcessStatus.enmReason = RTPROCEXITREASON_ABEND;
            }
        }

        /* The process finished, signal the main thread over the pipe. */
        ASMAtomicWriteBool(&pTxsExec->fProcessAlive, false);
        size_t cbIgnored;
        RTPipeWrite(pTxsExec->hWakeUpPipeW, "done", 4, &cbIgnored);
        RTPipeClose(pTxsExec->hWakeUpPipeW);
        pTxsExec->hWakeUpPipeW = NIL_RTPIPE;
        break;
    }
    RTCritSectLeave(&pTxsExec->CritSect);

    return VINF_SUCCESS;
}

/**
 * Sets up the thread that waits for the process to complete.
 *
 * @returns IPRT status code, reply to client made on error.
 * @param   pTxsExec            The TXSEXEC instance.
 */
static int txsExecSetupThread(PTXSEXEC pTxsExec)
{
    int rc = RTPipeCreate(&pTxsExec->hWakeUpPipeR, &pTxsExec->hWakeUpPipeW, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
    {
        pTxsExec->hWakeUpPipeR = pTxsExec->hWakeUpPipeW = NIL_RTPIPE;
        return txsExecReplyRC(pTxsExec, rc, "RTPipeCreate/wait");
    }

    rc = RTThreadCreate(&pTxsExec->hThreadWaiter, txsExecWaitThreadProc,
                        pTxsExec, 0 /*cbStack */, RTTHREADTYPE_DEFAULT,
                        RTTHREADFLAGS_WAITABLE, "TxsProcW");
    if (RT_FAILURE(rc))
    {
        pTxsExec->hThreadWaiter = NIL_RTTHREAD;
        return txsExecReplyRC(pTxsExec, rc, "RTThreadCreate");
    }

    return VINF_SUCCESS;
}

/**
 * Sets up the test pipe.
 *
 * @returns IPRT status code, reply to client made on error.
 * @param   pTxsExec            The TXSEXEC instance.
 * @param   pszTestPipe         How to set up the test pipe.
 */
static int txsExecSetupTestPipe(PTXSEXEC pTxsExec, const char *pszTestPipe)
{
    if (strcmp(pszTestPipe, "|"))
        return VINF_SUCCESS;

    int rc = RTPipeCreate(&pTxsExec->hTestPipeR, &pTxsExec->hTestPipeW, RTPIPE_C_INHERIT_WRITE);
    if (RT_FAILURE(rc))
    {
        pTxsExec->hTestPipeR = pTxsExec->hTestPipeW = NIL_RTPIPE;
        return txsExecReplyRC(pTxsExec, rc, "RTPipeCreate/test/%s", pszTestPipe);
    }

    char szVal[64];
    RTStrPrintf(szVal, sizeof(szVal), "%#llx", (uint64_t)RTPipeToNative(pTxsExec->hTestPipeW));
    rc = RTEnvSetEx(pTxsExec->hEnv, "IPRT_TEST_PIPE", szVal);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTEnvSetEx/test/%s", pszTestPipe);

    return VINF_SUCCESS;
}

/**
 * Sets up the redirection / pipe / nothing for one of the standard handles.
 *
 * @returns IPRT status code, reply to client made on error.
 * @param   pTxsExec            The TXSEXEC instance.
 * @param   pszHowTo            How to set up this standard handle.
 * @param   pszStdWhat          For what to setup redirection (stdin/stdout/stderr).
 * @param   fd                  Which standard handle it is (0 == stdin, 1 ==
 *                              stdout, 2 == stderr).
 * @param   ph                  The generic handle that @a pph may be set
 *                              pointing to.  Always set.
 * @param   pph                 Pointer to the RTProcCreateExec argument.
 *                              Always set.
 * @param   phPipe              Where to return the end of the pipe that we
 *                              should service.  Always set.
 */
static int txsExecSetupRedir(PTXSEXEC pTxsExec, const char *pszHowTo, const char *pszStdWhat, int fd, PRTHANDLE ph, PRTHANDLE *pph, PRTPIPE phPipe)
{
    ph->enmType = RTHANDLETYPE_PIPE;
    ph->u.hPipe = NIL_RTPIPE;
    *pph        = NULL;
    *phPipe     = NIL_RTPIPE;

    int rc;
    if (!strcmp(pszHowTo, "|"))
    {
        /*
         * Setup a pipe for forwarding to/from the client.
         */
        if (fd == 0)
            rc = RTPipeCreate(&ph->u.hPipe, phPipe, RTPIPE_C_INHERIT_READ);
        else
            rc = RTPipeCreate(phPipe, &ph->u.hPipe, RTPIPE_C_INHERIT_WRITE);
        if (RT_FAILURE(rc))
            return txsExecReplyRC(pTxsExec, rc, "RTPipeCreate/%s/%s", pszStdWhat, pszHowTo);
        ph->enmType = RTHANDLETYPE_PIPE;
        *pph = ph;
    }
    else if (!strcmp(pszHowTo, "/dev/null"))
    {
        /*
         * Redirect to/from /dev/null.
         */
        RTFILE hFile;
        rc = RTFileOpenBitBucket(&hFile, fd == 0 ? RTFILE_O_READ : RTFILE_O_WRITE);
        if (RT_FAILURE(rc))
            return txsExecReplyRC(pTxsExec, rc, "RTFileOpenBitBucket/%s/%s", pszStdWhat, pszHowTo);

        ph->enmType = RTHANDLETYPE_FILE;
        ph->u.hFile = hFile;
        *pph = ph;
    }
    else if (*pszHowTo)
    {
        /*
         * Redirect to/from file.
         */
        uint32_t fFlags;
        if (fd == 0)
            fFlags = RTFILE_O_READ  | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN;
        else
        {
            if (pszHowTo[0] != '>' || pszHowTo[1] != '>')
                fFlags = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE;
            else
            {
                /* append */
                pszHowTo += 2;
                fFlags = RTFILE_O_WRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND;
            }
        }

        RTFILE hFile;
        rc = RTFileOpen(&hFile, pszHowTo, fFlags);
        if (RT_FAILURE(rc))
            return txsExecReplyRC(pTxsExec, rc, "RTFileOpen/%s/%s", pszStdWhat, pszHowTo);

        ph->enmType = RTHANDLETYPE_FILE;
        ph->u.hFile = hFile;
        *pph = ph;
    }
    else
        /* same as parent (us) */
        rc = VINF_SUCCESS;
    return rc;
}

/**
 * Create the environment.
 *
 * @returns IPRT status code, reply to client made on error.
 * @param   pTxsExec            The TXSEXEC instance.
 * @param   cEnvVars            The number of environment variables.
 * @param   papszEnv            The environment variables (var=value).
 */
static int txsExecSetupEnv(PTXSEXEC pTxsExec, uint32_t cEnvVars, const char * const *papszEnv)
{
    /*
     * Create the environment.
     */
    int rc = RTEnvClone(&pTxsExec->hEnv, RTENV_DEFAULT);
    if (RT_FAILURE(rc))
        return txsExecReplyRC(pTxsExec, rc, "RTEnvClone");

    for (size_t i = 0; i < cEnvVars; i++)
    {
        rc = RTEnvPutEx(pTxsExec->hEnv, papszEnv[i]);
        if (RT_FAILURE(rc))
            return txsExecReplyRC(pTxsExec, rc, "RTEnvPutEx(,'%s')", papszEnv[i]);
    }
    return VINF_SUCCESS;
}

/**
 * Deletes the TXSEXEC structure and frees the memory backing it.
 *
 * @param   pTxsExec            The structure to destroy.
 */
static void txsExecDestroy(PTXSEXEC pTxsExec)
{
    int rc2;

    rc2 = RTEnvDestroy(pTxsExec->hEnv);             AssertRC(rc2);
    pTxsExec->hEnv              = NIL_RTENV;
    rc2 = RTPipeClose(pTxsExec->hTestPipeW);        AssertRC(rc2);
    pTxsExec->hTestPipeW        = NIL_RTPIPE;
    rc2 = RTHandleClose(pTxsExec->StdErr.phChild);  AssertRC(rc2);
    pTxsExec->StdErr.phChild    = NULL;
    rc2 = RTHandleClose(pTxsExec->StdOut.phChild);  AssertRC(rc2);
    pTxsExec->StdOut.phChild    = NULL;
    rc2 = RTHandleClose(pTxsExec->StdIn.phChild);   AssertRC(rc2);
    pTxsExec->StdIn.phChild     = NULL;

    rc2 = RTPipeClose(pTxsExec->hTestPipeR);        AssertRC(rc2);
    pTxsExec->hTestPipeR        = NIL_RTPIPE;
    rc2 = RTPipeClose(pTxsExec->hStdErrR);          AssertRC(rc2);
    pTxsExec->hStdErrR          = NIL_RTPIPE;
    rc2 = RTPipeClose(pTxsExec->hStdOutR);          AssertRC(rc2);
    pTxsExec->hStdOutR          = NIL_RTPIPE;
    rc2 = RTPipeClose(pTxsExec->hStdInW);           AssertRC(rc2);
    pTxsExec->hStdInW           = NIL_RTPIPE;

    RTPollSetDestroy(pTxsExec->hPollSet);
    pTxsExec->hPollSet          = NIL_RTPOLLSET;

    /*
     * If the process is still running we're in a bit of a fix...  Try kill it,
     * although that's potentially racing process termination and reusage of
     * the pid.
     */
    RTCritSectEnter(&pTxsExec->CritSect);

    RTPipeClose(pTxsExec->hWakeUpPipeW);
    pTxsExec->hWakeUpPipeW      = NIL_RTPIPE;
    RTPipeClose(pTxsExec->hWakeUpPipeR);
    pTxsExec->hWakeUpPipeR      = NIL_RTPIPE;

    if (   pTxsExec->hProcess != NIL_RTPROCESS
        && pTxsExec->fProcessAlive)
        RTProcTerminate(pTxsExec->hProcess);

    RTCritSectLeave(&pTxsExec->CritSect);

    int rcThread = VINF_SUCCESS;
    if (pTxsExec->hThreadWaiter != NIL_RTTHREAD)
        rcThread = RTThreadWait(pTxsExec->hThreadWaiter, 5000, NULL);
    if (RT_SUCCESS(rcThread))
    {
        pTxsExec->hThreadWaiter = NIL_RTTHREAD;
        RTCritSectDelete(&pTxsExec->CritSect);
        RTMemFree(pTxsExec);
    }
    /* else: leak it or RTThreadWait may cause heap corruption later. */
}

/**
 * Initializes the TXSEXEC structure.
 *
 * @returns VINF_SUCCESS and non-NULL *ppTxsExec on success, reply send status
 *        and *ppTxsExec set to NULL on failure.
 * @param   pPktHdr             The exec packet.
 * @param   cMsTimeout          The time parameter.
 * @param   ppTxsExec           Where to return the structure.
 */
static int txsExecCreate(PCTXSPKTHDR pPktHdr, RTMSINTERVAL cMsTimeout, PTXSEXEC *ppTxsExec)
{
    *ppTxsExec = NULL;

    /*
     * Allocate the basic resources.
     */
    PTXSEXEC pTxsExec = (PTXSEXEC)RTMemAlloc(sizeof(*pTxsExec));
    if (!pTxsExec)
        return txsReplyRC(pPktHdr, VERR_NO_MEMORY, "RTMemAlloc(%zu)", sizeof(*pTxsExec));
    int rc = RTCritSectInit(&pTxsExec->CritSect);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pTxsExec);
        return txsReplyRC(pPktHdr, rc, "RTCritSectInit");
    }

    /*
     * Initialize the member to NIL values.
     */
    pTxsExec->pPktHdr           = pPktHdr;
    pTxsExec->cMsTimeout        = cMsTimeout;
    pTxsExec->rcReplySend       = VINF_SUCCESS;

    pTxsExec->hPollSet          = NIL_RTPOLLSET;
    pTxsExec->hStdInW           = NIL_RTPIPE;
    pTxsExec->hStdOutR          = NIL_RTPIPE;
    pTxsExec->hStdErrR          = NIL_RTPIPE;
    pTxsExec->hTestPipeR        = NIL_RTPIPE;
    pTxsExec->hWakeUpPipeR      = NIL_RTPIPE;
    pTxsExec->hThreadWaiter     = NIL_RTTHREAD;

    pTxsExec->StdIn.phChild     = NULL;
    pTxsExec->StdOut.phChild    = NULL;
    pTxsExec->StdErr.phChild    = NULL;
    pTxsExec->hTestPipeW        = NIL_RTPIPE;
    pTxsExec->hEnv              = NIL_RTENV;

    pTxsExec->hProcess          = NIL_RTPROCESS;
    pTxsExec->ProcessStatus.iStatus   = 254;
    pTxsExec->ProcessStatus.enmReason = RTPROCEXITREASON_ABEND;
    pTxsExec->fProcessAlive     = false;
    pTxsExec->hWakeUpPipeW      = NIL_RTPIPE;

    *ppTxsExec = pTxsExec;
    return VINF_SUCCESS;
}

/**
 * txsDoExec helper that takes over when txsDoExec has expanded the packet.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The exec packet.
 * @param   fFlags              Flags, reserved for future use.
 * @param   pszExecName         The executable name.
 * @param   cArgs               The argument count.
 * @param   papszArgs           The arguments.
 * @param   cEnvVars            The environment variable count.
 * @param   papszEnv            The environment variables.
 * @param   pszStdIn            How to deal with standard in.
 * @param   pszStdOut           How to deal with standard out.
 * @param   pszStdErr           How to deal with standard err.
 * @param   pszTestPipe         How to deal with the test pipe.
 * @param   pszUsername         The user to run the program as.
 * @param   cMillies            The process time limit in milliseconds.
 */
static int txsDoExecHlp(PCTXSPKTHDR pPktHdr, uint32_t fFlags, const char *pszExecName,
                        uint32_t cArgs,    const char * const *papszArgs,
                        uint32_t cEnvVars, const char * const *papszEnv,
                        const char *pszStdIn, const char *pszStdOut, const char *pszStdErr, const char *pszTestPipe,
                        const char *pszUsername, RTMSINTERVAL cMillies)
{
    int     rc2;
    RT_NOREF_PV(fFlags);

    /*
     * Input validation, filter out things we don't yet support..
     */
    Assert(!fFlags);
    if (!*pszExecName)
        return txsReplyFailure(pPktHdr, "STR ZERO", "Executable name is empty");
    if (!*pszStdIn)
        return txsReplyFailure(pPktHdr, "STR ZERO", "The stdin howto is empty");
    if (!*pszStdOut)
        return txsReplyFailure(pPktHdr, "STR ZERO", "The stdout howto is empty");
    if (!*pszStdErr)
        return txsReplyFailure(pPktHdr, "STR ZERO", "The stderr howto is empty");
    if (!*pszTestPipe)
        return txsReplyFailure(pPktHdr, "STR ZERO", "The testpipe howto is empty");
    if (strcmp(pszTestPipe, "|") && strcmp(pszTestPipe,  "/dev/null"))
        return txsReplyFailure(pPktHdr, "BAD TSTP", "Only \"|\" and \"/dev/null\" are allowed as testpipe howtos ('%s')",
                               pszTestPipe);
    if (*pszUsername)
        return txsReplyFailure(pPktHdr, "NOT IMPL", "Executing as a specific user is not implemented ('%s')", pszUsername);

    /*
     * Prepare for process launch.
     */
    PTXSEXEC pTxsExec;
    int rc = txsExecCreate(pPktHdr, cMillies, &pTxsExec);
    if (pTxsExec == NULL)
        return rc;
    rc = txsExecSetupEnv(pTxsExec, cEnvVars, papszEnv);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupRedir(pTxsExec, pszStdIn, "StdIn",   0, &pTxsExec->StdIn.hChild,  &pTxsExec->StdIn.phChild,  &pTxsExec->hStdInW);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupRedir(pTxsExec, pszStdOut, "StdOut", 1, &pTxsExec->StdOut.hChild, &pTxsExec->StdOut.phChild, &pTxsExec->hStdOutR);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupRedir(pTxsExec, pszStdErr, "StdErr", 2, &pTxsExec->StdErr.hChild, &pTxsExec->StdErr.phChild, &pTxsExec->hStdErrR);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupTestPipe(pTxsExec, pszTestPipe);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupThread(pTxsExec);
    if (RT_SUCCESS(rc))
        rc = txsExecSetupPollSet(pTxsExec);
    if (RT_SUCCESS(rc))
    {
        char szPathResolved[RTPATH_MAX + 1];
        rc = RTPathReal(pszExecName, szPathResolved, sizeof(szPathResolved));
        if (RT_SUCCESS(rc))
        {
            /*
             * Create the process.
             */
            if (g_fDisplayOutput)
            {
                RTPrintf("txs: Executing \"%s\" -> \"%s\": ", pszExecName, szPathResolved);
                for (uint32_t i = 0; i < cArgs; i++)
                    RTPrintf(" \"%s\"", papszArgs[i]);
                RTPrintf("\n");
            }

            rc = RTProcCreateEx(szPathResolved, papszArgs, pTxsExec->hEnv, 0 /*fFlags*/,
                                pTxsExec->StdIn.phChild, pTxsExec->StdOut.phChild, pTxsExec->StdErr.phChild,
                                *pszUsername ? pszUsername : NULL, NULL, NULL,
                                &pTxsExec->hProcess);
            if (RT_SUCCESS(rc))
            {
                ASMAtomicWriteBool(&pTxsExec->fProcessAlive, true);
                rc2 = RTThreadUserSignal(pTxsExec->hThreadWaiter); AssertRC(rc2);

                /*
                 * Close the child ends of any pipes and redirected files.
                 */
                rc2 = RTHandleClose(pTxsExec->StdIn.phChild);   AssertRC(rc2);
                pTxsExec->StdIn.phChild     = NULL;
                rc2 = RTHandleClose(pTxsExec->StdOut.phChild);  AssertRC(rc2);
                pTxsExec->StdOut.phChild    = NULL;
                rc2 = RTHandleClose(pTxsExec->StdErr.phChild);  AssertRC(rc2);
                pTxsExec->StdErr.phChild    = NULL;
                rc2 = RTPipeClose(pTxsExec->hTestPipeW);        AssertRC(rc2);
                pTxsExec->hTestPipeW        = NIL_RTPIPE;

                /*
                 * Let another worker function funnel output and input to the
                 * client as well as the process exit code.
                 */
                rc = txsDoExecHlp2(pTxsExec);
            }
        }

        if (RT_FAILURE(rc))
           rc = txsReplyFailure(pPktHdr, "FAILED  ", "Executing process \"%s\" failed with %Rrc",
                                pszExecName, rc);
    }
    else
        rc = pTxsExec->rcReplySend;
    txsExecDestroy(pTxsExec);
    return rc;
}

/**
 * Execute a program.
 *
 * @returns IPRT status code from send.
 * @param   pPktHdr             The exec packet.
 */
static int txsDoExec(PCTXSPKTHDR pPktHdr)
{
    /*
     * This packet has a lot of parameters, most of which are zero terminated
     * strings.  The strings used in items 7 thru 10 are either file names,
     * "/dev/null" or a pipe char (|).
     *
     * Packet content:
     *    1. Flags reserved for future use (32-bit unsigned).
     *    2. The executable name (string).
     *    3. The argument count given as a 32-bit unsigned integer.
     *    4. The arguments strings.
     *    5. The number of environment strings (32-bit unsigned).
     *    6. The environment strings (var=val) to apply the environment.
     *    7. What to do about standard in (string).
     *    8. What to do about standard out (string).
     *    9. What to do about standard err (string).
     *   10. What to do about the test pipe (string).
     *   11. The name of the user to run the program as (string).  Empty string
     *       means running it as the current user.
     *   12. Process time limit in milliseconds (32-bit unsigned).  Max == no limit.
     */
    size_t const cbMin = sizeof(TXSPKTHDR)
                       + sizeof(uint32_t) /* flags */ + 2
                       + sizeof(uint32_t) /* argc */  + 2 /* argv */
                       + sizeof(uint32_t) + 0 /* environ */
                       + 4 * 1
                       + sizeof(uint32_t) /* timeout */;
    if (pPktHdr->cb < cbMin)
        return txsReplyBadMinSize(pPktHdr, cbMin);

    /* unpack the packet */
    char const     *pchEnd = (char const *)pPktHdr + pPktHdr->cb;
    char const     *pch    = (char const *)(pPktHdr + 1); /* cursor */

    /* 1. flags */
    uint32_t const  fFlags = *(uint32_t const *)pch;
    pch                   += sizeof(uint32_t);
    if (fFlags != 0)
        return txsReplyFailure(pPktHdr, "BAD FLAG", "Invalid EXEC flags %#x, expected 0", fFlags);

    /* 2. exec name */
    int             rc;
    char           *pszExecName = NULL;
    if (!txsIsStringValid(pPktHdr, "execname", pch, &pszExecName, &pch, &rc))
        return rc;

    /* 3. argc */
    uint32_t const  cArgs  = (size_t)(pchEnd - pch) > sizeof(uint32_t) ? *(uint32_t const *)pch : 0xff;
    pch                   += sizeof(uint32_t);
    if (cArgs * 1 >= (size_t)(pchEnd - pch))
        rc = txsReplyFailure(pPktHdr, "BAD ARGC", "Bad or missing argument count (%#x)", cArgs);
    else if (cArgs > 128)
        rc = txsReplyFailure(pPktHdr, "BAD ARGC", "Too many arguments (%#x)", cArgs);
    else
    {
        char **papszArgs = (char **)RTMemTmpAllocZ(sizeof(char *) * (cArgs + 1));
        if (papszArgs)
        {
            /* 4. argv */
            bool fOk = true;
            for (size_t i = 0; i < cArgs && fOk; i++)
            {
                fOk = txsIsStringValid(pPktHdr, "argvN", pch, &papszArgs[i], &pch, &rc);
                if (!fOk)
                    break;
            }
            if (fOk)
            {
                /* 5. cEnvVars */
                uint32_t const  cEnvVars = (size_t)(pchEnd - pch) > sizeof(uint32_t) ? *(uint32_t const *)pch : 0xfff;
                pch                     += sizeof(uint32_t);
                if (cEnvVars * 1 >= (size_t)(pchEnd - pch))
                    rc = txsReplyFailure(pPktHdr, "BAD ENVC", "Bad or missing environment variable count (%#x)", cEnvVars);
                else if (cEnvVars > 256)
                    rc = txsReplyFailure(pPktHdr, "BAD ENVC", "Too many environment variables (%#x)", cEnvVars);
                else
                {
                    char **papszEnv = (char **)RTMemTmpAllocZ(sizeof(char *) * (cEnvVars + 1));
                    if (papszEnv)
                    {
                        /* 6. environ */
                        for (size_t i = 0; i < cEnvVars && fOk; i++)
                        {
                            fOk = txsIsStringValid(pPktHdr, "envN", pch, &papszEnv[i], &pch, &rc);
                            if (!fOk) /* Bail out on error. */
                                break;
                        }
                        if (fOk)
                        {
                            /* 7. stdout */
                            char *pszStdIn;
                            if (txsIsStringValid(pPktHdr, "stdin", pch, &pszStdIn, &pch, &rc))
                            {
                                /* 8. stdout */
                                char *pszStdOut;
                                if (txsIsStringValid(pPktHdr, "stdout", pch, &pszStdOut, &pch, &rc))
                                {
                                    /* 9. stderr */
                                    char *pszStdErr;
                                    if (txsIsStringValid(pPktHdr, "stderr", pch, &pszStdErr, &pch, &rc))
                                    {
                                        /* 10. testpipe */
                                        char *pszTestPipe;
                                        if (txsIsStringValid(pPktHdr, "testpipe", pch, &pszTestPipe, &pch, &rc))
                                        {
                                            /* 11. username */
                                            char *pszUsername;
                                            if (txsIsStringValid(pPktHdr, "username", pch, &pszUsername, &pch, &rc))
                                            {
                                                /** @todo No password value? */

                                                /* 12. time limit */
                                                uint32_t const  cMillies = (size_t)(pchEnd - pch) >= sizeof(uint32_t)
                                                                         ? *(uint32_t const *)pch
                                                                         : 0;
                                                if ((size_t)(pchEnd - pch) > sizeof(uint32_t))
                                                    rc = txsReplyFailure(pPktHdr, "BAD END ", "Timeout argument not at end of packet (%#x)", (size_t)(pchEnd - pch));
                                                else if ((size_t)(pchEnd - pch) < sizeof(uint32_t))
                                                    rc = txsReplyFailure(pPktHdr, "BAD NOTO", "No timeout argument");
                                                else if (cMillies < 1000)
                                                    rc = txsReplyFailure(pPktHdr, "BAD TO  ", "Timeout is less than a second (%#x)", cMillies);
                                                else
                                                {
                                                    pch += sizeof(uint32_t);

                                                    /*
                                                     * Time to employ a helper here before we go way beyond
                                                     * the right margin...
                                                     */
                                                    rc = txsDoExecHlp(pPktHdr, fFlags, pszExecName,
                                                                      cArgs,    papszArgs,
                                                                      cEnvVars, papszEnv,
                                                                      pszStdIn, pszStdOut, pszStdErr, pszTestPipe,
                                                                      pszUsername,
                                                                      cMillies == UINT32_MAX ? RT_INDEFINITE_WAIT : cMillies);
                                                }
                                                RTStrFree(pszUsername);
                                            }
                                            RTStrFree(pszTestPipe);
                                        }
                                        RTStrFree(pszStdErr);
                                    }
                                    RTStrFree(pszStdOut);
                                }
                                RTStrFree(pszStdIn);
                            }
                        }
                        for (size_t i = 0; i < cEnvVars; i++)
                            RTStrFree(papszEnv[i]);
                        RTMemTmpFree(papszEnv);
                    }
                    else
                        rc = txsReplyFailure(pPktHdr, "NO MEM  ", "Failed to allocate %zu bytes environ", sizeof(char *) * (cEnvVars + 1));
                }
            }
            for (size_t i = 0; i < cArgs; i++)
                RTStrFree(papszArgs[i]);
            RTMemTmpFree(papszArgs);
        }
        else
            rc = txsReplyFailure(pPktHdr, "NO MEM  ", "Failed to allocate %zu bytes for argv", sizeof(char *) * (cArgs + 1));
    }
    RTStrFree(pszExecName);

    return rc;
}

/**
 * The main loop.
 *
 * @returns exit code.
 */
static RTEXITCODE txsMainLoop(void)
{
    if (g_cVerbose > 0)
        RTMsgInfo("txsMainLoop: start...\n");
    RTEXITCODE enmExitCode = RTEXITCODE_SUCCESS;
    while (!g_fTerminate)
    {
        /*
         * Read client command packet and process it.
         */
        PTXSPKTHDR pPktHdr;
        int rc = txsRecvPkt(&pPktHdr, true /*fAutoRetryOnFailure*/);
        if (RT_FAILURE(rc))
            continue;
        if (g_cVerbose > 0)
            RTMsgInfo("txsMainLoop: CMD: %.8s...", pPktHdr->achOpcode);

        /*
         * Do a string switch on the opcode bit.
         */
        /* Connection: */
        if (     txsIsSameOpcode(pPktHdr, "HOWDY   "))
            rc = txsDoHowdy(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "BYE     "))
            rc = txsDoBye(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "VER     "))
            rc = txsDoVer(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "UUID    "))
            rc = txsDoUuid(pPktHdr);
        /* Process: */
        else if (txsIsSameOpcode(pPktHdr, "EXEC    "))
            rc = txsDoExec(pPktHdr);
        /* Admin: */
        else if (txsIsSameOpcode(pPktHdr, "REBOOT  "))
            rc = txsDoReboot(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "SHUTDOWN"))
            rc = txsDoShutdown(pPktHdr);
        /* CD/DVD control: */
        else if (txsIsSameOpcode(pPktHdr, "CD EJECT"))
            rc = txsDoCdEject(pPktHdr);
        /* File system: */
        else if (txsIsSameOpcode(pPktHdr, "CLEANUP "))
            rc = txsDoCleanup(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "MKDIR   "))
            rc = txsDoMkDir(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "MKDRPATH"))
            rc = txsDoMkDrPath(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "MKSYMLNK"))
            rc = txsDoMkSymlnk(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "RMDIR   "))
            rc = txsDoRmDir(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "RMFILE  "))
            rc = txsDoRmFile(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "RMSYMLNK"))
            rc = txsDoRmSymlnk(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "RMTREE  "))
            rc = txsDoRmTree(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "CHMOD   "))
            rc = txsDoChMod(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "CHOWN   "))
            rc = txsDoChOwn(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "ISDIR   "))
            rc = txsDoIsDir(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "ISFILE  "))
            rc = txsDoIsFile(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "ISSYMLNK"))
            rc = txsDoIsSymlnk(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "STAT    "))
            rc = txsDoStat(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "LSTAT   "))
            rc = txsDoLStat(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "LIST    "))
            rc = txsDoList(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "CPFILE  "))
            rc = txsDoCopyFile(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "PUT FILE"))
            rc = txsDoPutFile(pPktHdr, false /*fHasMode*/);
        else if (txsIsSameOpcode(pPktHdr, "PUT2FILE"))
            rc = txsDoPutFile(pPktHdr, true /*fHasMode*/);
        else if (txsIsSameOpcode(pPktHdr, "GET FILE"))
            rc = txsDoGetFile(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "PKFILE  "))
            rc = txsDoPackFile(pPktHdr);
        else if (txsIsSameOpcode(pPktHdr, "UNPKFILE"))
            rc = txsDoUnpackFile(pPktHdr);
        /* Misc: */
        else if (txsIsSameOpcode(pPktHdr, "EXP STR "))
            rc = txsDoExpandString(pPktHdr);
        else
            rc = txsReplyUnknown(pPktHdr);

        if (g_cVerbose > 0)
            RTMsgInfo("txsMainLoop: CMD: %.8s -> %Rrc", pPktHdr->achOpcode, rc);
        RTMemFree(pPktHdr);
    }

    if (g_cVerbose > 0)
        RTMsgInfo("txsMainLoop: end\n");
    return enmExitCode;
}


/**
 * Finalizes the scratch directory, making sure it exists.
 *
 * @returns exit code.
 */
static RTEXITCODE txsFinalizeScratch(void)
{
    RTPathStripTrailingSlash(g_szScratchPath);
    char *pszFilename = RTPathFilename(g_szScratchPath);
    if (!pszFilename)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "cannot use root for scratch (%s)\n", g_szScratchPath);

    int rc;
    if (strchr(pszFilename, 'X'))
    {
        char ch = *pszFilename;
        rc = RTDirCreateFullPath(g_szScratchPath, 0700);
        *pszFilename = ch;
        if (RT_SUCCESS(rc))
            rc = RTDirCreateTemp(g_szScratchPath, 0700);
    }
    else
    {
        if (RTDirExists(g_szScratchPath))
            rc = VINF_SUCCESS;
        else
            rc = RTDirCreateFullPath(g_szScratchPath, 0700);
    }
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to create scratch directory: %Rrc (%s)\n", rc, g_szScratchPath);
    return RTEXITCODE_SUCCESS;
}

/**
 * Attempts to complete an upgrade by updating the original and relaunching
 * ourselves from there again.
 *
 * On failure, we'll continue running as the temporary copy.
 *
 * @returns Exit code. Exit if this is non-zero or @a *pfExit is set.
 * @param   argc                The number of arguments.
 * @param   argv                The argument vector.
 * @param   pfExit              For indicating exit when the exit code is zero.
 * @param   pszUpgrading        The upgraded image path.
 */
static RTEXITCODE txsAutoUpdateStage2(int argc, char **argv, bool *pfExit, const char *pszUpgrading)
{
    if (g_cVerbose > 0)
        RTMsgInfo("Auto update stage 2...");

    /*
     * Copy the current executable onto the original.
     * Note that we're racing the original program on some platforms, thus the
     * 60 sec sleep mess.
     */
    char szUpgradePath[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szUpgradePath, sizeof(szUpgradePath)))
    {
        RTMsgError("RTProcGetExecutablePath failed (step 2)\n");
        return RTEXITCODE_SUCCESS;
    }
    void    *pvUpgrade;
    size_t   cbUpgrade;
    int rc = RTFileReadAll(szUpgradePath, &pvUpgrade, &cbUpgrade);
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTFileReadAllEx(\"%s\"): %Rrc (step 2)\n", szUpgradePath, rc);
        return RTEXITCODE_SUCCESS;
    }

    uint64_t StartMilliTS = RTTimeMilliTS();
    RTFILE  hFile;
    rc = RTFileOpen(&hFile, pszUpgrading,
                    RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE
                    | (0755 << RTFILE_O_CREATE_MODE_SHIFT));
    while (   RT_FAILURE(rc)
           && RTTimeMilliTS() - StartMilliTS < 60000)
    {
        RTThreadSleep(1000);
        rc = RTFileOpen(&hFile, pszUpgrading,
                        RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE
                        | (0755 << RTFILE_O_CREATE_MODE_SHIFT));
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTFileWrite(hFile, pvUpgrade, cbUpgrade, NULL);
        RTFileClose(hFile);
        if (RT_SUCCESS(rc))
        {
            /*
             * Relaunch the service with the original name, foricbly barring
             * further upgrade cycles in case of bugs (and simplifying the code).
             */
            const char **papszArgs = (const char **)RTMemAlloc((argc + 1 + 1) * sizeof(char **));
            if (papszArgs)
            {
                papszArgs[0] = pszUpgrading;
                for (int i = 1; i < argc; i++)
                    papszArgs[i] = argv[i];
                papszArgs[argc] = "--no-auto-upgrade";
                papszArgs[argc + 1] = NULL;

                RTMsgInfo("Launching upgraded image: \"%s\"\n", pszUpgrading);
                RTPROCESS hProc;
                rc = RTProcCreate(pszUpgrading, papszArgs, RTENV_DEFAULT, 0 /*fFlags*/, &hProc);
                if (RT_SUCCESS(rc))
                    *pfExit = true;
                else
                    RTMsgError("RTProcCreate(\"%s\"): %Rrc (upgrade stage 2)\n", pszUpgrading, rc);
                RTMemFree(papszArgs);
            }
            else
                RTMsgError("RTMemAlloc failed during upgrade attempt (stage 2)\n");
        }
        else
            RTMsgError("RTFileWrite(%s,,%zu): %Rrc (step 2) - BAD\n", pszUpgrading, cbUpgrade, rc);
    }
    else
        RTMsgError("RTFileOpen(,%s,): %Rrc\n", pszUpgrading, rc);
    RTFileReadAllFree(pvUpgrade, cbUpgrade);
    return RTEXITCODE_SUCCESS;
}

/**
 * Checks for an upgrade and respawns if there is.
 *
 * @returns Exit code. Exit if this is non-zero or @a *pfExit is set.
 * @param   argc                The number of arguments.
 * @param   argv                The argument vector.
 * @param   cSecsCdWait         Number of seconds to wait on the CD.
 * @param   pfExit              For indicating exit when the exit code is zero.
 */
static RTEXITCODE txsAutoUpdateStage1(int argc, char **argv, uint32_t cSecsCdWait, bool *pfExit)
{
    if (g_cVerbose > 1)
        RTMsgInfo("Auto update stage 1...");

    /*
     * Figure names of the current service image and the potential upgrade.
     */
    char szOrgPath[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szOrgPath, sizeof(szOrgPath)))
    {
        RTMsgError("RTProcGetExecutablePath failed\n");
        return RTEXITCODE_SUCCESS;
    }

    char szUpgradePath[RTPATH_MAX];
    int rc = RTPathJoin(szUpgradePath, sizeof(szUpgradePath), g_szCdRomPath, g_szOsSlashArchShortName);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szUpgradePath, sizeof(szUpgradePath), RTPathFilename(szOrgPath));
    if (RT_FAILURE(rc))
    {
        RTMsgError("Failed to construct path to potential service upgrade: %Rrc\n", rc);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Query information about the two images and read the entire potential source file.
     * Because the CD may take a little time to be mounted when the system boots, we
     * need to do some fudging here.
     */
    uint64_t nsStart = RTTimeNanoTS();
    RTFSOBJINFO UpgradeInfo;
    for (;;)
    {
        rc = RTPathQueryInfo(szUpgradePath, &UpgradeInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
            break;
        if (   rc != VERR_FILE_NOT_FOUND
            && rc != VERR_PATH_NOT_FOUND
            && rc != VERR_MEDIA_NOT_PRESENT
            && rc != VERR_MEDIA_NOT_RECOGNIZED)
        {
            RTMsgError("RTPathQueryInfo(\"%s\"): %Rrc (upgrade)\n", szUpgradePath, rc);
            return RTEXITCODE_SUCCESS;
        }
        uint64_t cNsElapsed = RTTimeNanoTS() - nsStart;
        if (cNsElapsed >= cSecsCdWait * RT_NS_1SEC_64)
        {
            if (g_cVerbose > 0)
                RTMsgInfo("Auto update: Giving up waiting for media.");
            return RTEXITCODE_SUCCESS;
        }
        RTThreadSleep(500);
    }

    RTFSOBJINFO OrgInfo;
    rc = RTPathQueryInfo(szOrgPath, &OrgInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTPathQueryInfo(\"%s\"): %Rrc (old)\n", szOrgPath, rc);
        return RTEXITCODE_SUCCESS;
    }

    void    *pvUpgrade;
    size_t   cbUpgrade;
    rc = RTFileReadAllEx(szUpgradePath, 0, UpgradeInfo.cbObject, RTFILE_RDALL_O_DENY_NONE, &pvUpgrade, &cbUpgrade);
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTPathQueryInfo(\"%s\"): %Rrc (old)\n", szOrgPath, rc);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Compare and see if we've got a different service image or not.
     */
    if (OrgInfo.cbObject == UpgradeInfo.cbObject)
    {
        /* must compare bytes. */
        void    *pvOrg;
        size_t   cbOrg;
        rc = RTFileReadAllEx(szOrgPath, 0, OrgInfo.cbObject, RTFILE_RDALL_O_DENY_NONE, &pvOrg, &cbOrg);
        if (RT_FAILURE(rc))
        {
            RTMsgError("RTFileReadAllEx(\"%s\"): %Rrc\n", szOrgPath, rc);
            RTFileReadAllFree(pvUpgrade, cbUpgrade);
            return RTEXITCODE_SUCCESS;
        }
        bool fSame = !memcmp(pvUpgrade, pvOrg, OrgInfo.cbObject);
        RTFileReadAllFree(pvOrg, cbOrg);
        if (fSame)
        {
            RTFileReadAllFree(pvUpgrade, cbUpgrade);
            if (g_cVerbose > 0)
                RTMsgInfo("Auto update: Not necessary.");
            return RTEXITCODE_SUCCESS;
        }
    }

    /*
     * Should upgrade.  Start by creating an executable copy of the update
     * image in the scratch area.
     */
    RTEXITCODE rcExit = txsFinalizeScratch();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        char szTmpPath[RTPATH_MAX];
        rc = RTPathJoin(szTmpPath, sizeof(szTmpPath), g_szScratchPath, RTPathFilename(szOrgPath));
        if (RT_SUCCESS(rc))
        {
            RTFileDelete(szTmpPath); /* shouldn't hurt. */

            RTFILE hFile;
            rc = RTFileOpen(&hFile, szTmpPath,
                            RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE
                            | (0755 << RTFILE_O_CREATE_MODE_SHIFT));
            if (RT_SUCCESS(rc))
            {
                rc = RTFileWrite(hFile, pvUpgrade, UpgradeInfo.cbObject, NULL);
                RTFileClose(hFile);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Try execute the new image and quit if it works.
                     */
                    const char **papszArgs = (const char **)RTMemAlloc((argc + 2 + 1) * sizeof(char **));
                    if (papszArgs)
                    {
                        papszArgs[0] = szTmpPath;
                        for (int i = 1; i < argc; i++)
                            papszArgs[i] = argv[i];
                        papszArgs[argc] = "--upgrading";
                        papszArgs[argc + 1] = szOrgPath;
                        papszArgs[argc + 2] = NULL;

                        RTMsgInfo("Launching intermediate automatic upgrade stage: \"%s\"\n", szTmpPath);
                        RTPROCESS hProc;
                        rc = RTProcCreate(szTmpPath, papszArgs, RTENV_DEFAULT, 0 /*fFlags*/, &hProc);
                        if (RT_SUCCESS(rc))
                            *pfExit = true;
                        else
                            RTMsgError("RTProcCreate(\"%s\"): %Rrc (upgrade stage 1)\n", szTmpPath, rc);
                        RTMemFree(papszArgs);
                    }
                    else
                        RTMsgError("RTMemAlloc failed during upgrade attempt (stage)\n");
                }
                else
                    RTMsgError("RTFileWrite(%s,,%zu): %Rrc\n", szTmpPath, UpgradeInfo.cbObject, rc);
            }
            else
                RTMsgError("RTFileOpen(,%s,): %Rrc\n", szTmpPath, rc);
        }
        else
            RTMsgError("Failed to construct path to temporary upgrade image: %Rrc\n", rc);
    }

    RTFileReadAllFree(pvUpgrade, cbUpgrade);
    return rcExit;
}

/**
 * Determines the default configuration.
 */
static void txsSetDefaults(void)
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

    int rc = RTPathGetCurrent(g_szCwd, sizeof(g_szCwd));
    if (RT_FAILURE(rc))
        RTMsgError("RTPathGetCurrent failed: %Rrc\n", rc);
    g_szCwd[sizeof(g_szCwd) - 1] = '\0';

    if (!RTProcGetExecutablePath(g_szTxsDir, sizeof(g_szTxsDir)))
        RTMsgError("RTProcGetExecutablePath failed!\n");
    g_szTxsDir[sizeof(g_szTxsDir) - 1] = '\0';
    RTPathStripFilename(g_szTxsDir);
    RTPathStripTrailingSlash(g_szTxsDir);

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
    rc = RTPathTemp(g_szDefScratchPath, sizeof(g_szDefScratchPath));
    if (RT_SUCCESS(rc))
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS) || defined(RT_OS_DOS)
        rc = RTPathAppend(g_szDefScratchPath, sizeof(g_szDefScratchPath), "txs-XXXX.tmp");
#else
        rc = RTPathAppend(g_szDefScratchPath, sizeof(g_szDefScratchPath), "txs-XXXXXXXXX.tmp");
#endif
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTPathTemp/Append failed when constructing scratch path: %Rrc\n", rc);
        strcpy(g_szDefScratchPath, "/tmp/txs-XXXX.tmp");
    }
    strcpy(g_szScratchPath, g_szDefScratchPath);

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
static void txsUsage(PRTSTREAM pStrm, const char *pszArgv0)
{
    RTStrmPrintf(pStrm,
                 "Usage: %Rbn [options]\n"
                 "\n"
                 "Options:\n"
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
                 "  --auto-upgrade, --no-auto-upgrade\n"
                 "      To enable or disable the automatic upgrade mechanism where any different\n"
                 "      version found on the CD-ROM on startup will replace the initial copy.\n"
                 "      Default: --auto-upgrade\n"
                 "  --wait-cdrom <secs>\n"
                 "     Number of seconds to wait for the CD-ROM to be mounted before giving up\n"
                 "     on automatic upgrading.\n"
                 "     Default: --wait-cdrom 1;  solaris: --wait-cdrom 8\n"
                 "  --upgrading <org-path>\n"
                 "      Internal use only.\n");
    RTStrmPrintf(pStrm,
                 "  --display-output, --no-display-output\n"
                 "      Display the output and the result of all child processes.\n");
    RTStrmPrintf(pStrm,
                 "  --foreground\n"
                 "      Don't daemonize, run in the foreground.\n");
    RTStrmPrintf(pStrm,
                 "  --verbose, -v\n"
                 "      Increases the verbosity level. Can be specified multiple times.\n");
    RTStrmPrintf(pStrm,
                 "  --quiet, -q\n"
                 "      Mutes any logging output.\n");
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
static RTEXITCODE txsParseArgv(int argc, char **argv, bool *pfExit)
{
    *pfExit = false;

    /*
     * Storage for locally handled options.
     */
    bool        fAutoUpgrade    = true;
    bool        fDaemonize      = true;
    const char *pszUpgrading    = NULL;
#ifdef RT_OS_SOLARIS
    uint32_t    cSecsCdWait     = 8;
#else
    uint32_t    cSecsCdWait     = 1;
#endif

    /*
     * Combine the base and transport layer option arrays.
     */
    static const RTGETOPTDEF s_aBaseOptions[] =
    {
        { "--transport",        't', RTGETOPT_REQ_STRING  },
        { "--cdrom",            'c', RTGETOPT_REQ_STRING  },
        { "--wait-cdrom",       'w', RTGETOPT_REQ_UINT32  },
        { "--scratch",          's', RTGETOPT_REQ_STRING  },
        { "--auto-upgrade",     'a', RTGETOPT_REQ_NOTHING },
        { "--no-auto-upgrade",  'A', RTGETOPT_REQ_NOTHING },
        { "--upgrading",        'U', RTGETOPT_REQ_STRING  },
        { "--display-output",   'd', RTGETOPT_REQ_NOTHING },
        { "--no-display-output",'D', RTGETOPT_REQ_NOTHING },
        { "--foreground",       'f', RTGETOPT_REQ_NOTHING },
        { "--daemonized",       'Z', RTGETOPT_REQ_NOTHING },
        { "--quiet",            'q', RTGETOPT_REQ_NOTHING },
        { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
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
            case 'a':
                fAutoUpgrade = true;
                break;

            case 'A':
                fAutoUpgrade = false;
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
                txsUsage(g_pStdOut, argv[0]);
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 's':
                rc = RTStrCopy(g_szScratchPath, sizeof(g_szScratchPath), Val.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "scratch path is too long (%Rrc)\n", rc);
                break;

            case 't':
            {
                PCTXSTRANSPORT pTransport = NULL;
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

            case 'U':
                pszUpgrading = Val.psz;
                break;

            case 'w':
                cSecsCdWait = Val.u32;
                break;

            case 'q':
                g_cVerbose = 0;
                break;

            case 'v':
                g_cVerbose++;
                break;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 'Z':
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
     * Handle automatic upgrading of the service.
     */
    if (fAutoUpgrade && !*pfExit)
    {
        RTEXITCODE rcExit;
        if (pszUpgrading)
            rcExit = txsAutoUpdateStage2(argc, argv, pfExit, pszUpgrading);
        else
            rcExit = txsAutoUpdateStage1(argc, argv, cSecsCdWait, pfExit);
        if (   *pfExit
            || rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    /*
     * Daemonize ourselves if asked to.
     */
    if (fDaemonize && !*pfExit)
    {
        if (g_cVerbose > 0)
            RTMsgInfo("Daemonizing...");
        rc = RTProcDaemonize(argv, "--daemonized");
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcDaemonize: %Rrc\n", rc);
        *pfExit = true;
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * @callback_method_impl{FNRTLOGPHASE, Release logger callback}
 */
static DECLCALLBACK(void) logHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "TestExecService (Validation Kit TxS) %s r%s (verbosity: %u) %s %s (%s %s) release log\n"
                   "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbose,
                   KBUILD_TARGET, KBUILD_TARGET_ARCH,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef VBOX_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   VBOX_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */
            break;
    }
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
    txsSetDefaults();
    bool fExit;
    RTEXITCODE rcExit = txsParseArgv(argc, argv, &fExit);
    if (rcExit != RTEXITCODE_SUCCESS || fExit)
        return rcExit;

    /*
     * Enable (release) TxS logging to stdout + file. This is independent from the actual test cases being run.
     *
     * Keep the log file path + naming predictable (the OS' temp dir) so that we later can retrieve it
     * from the host side without guessing much.
     *
     * If enabling logging fails for some reason, just tell but don't bail out to not make tests fail.
     */
    char szLogFile[RTPATH_MAX];
    rc = RTPathTemp(szLogFile, sizeof(szLogFile));
    if (RT_SUCCESS(rc))
    {
        rc = RTPathAppend(szLogFile, sizeof(szLogFile), "vbox-txs-release.log");
        if (RT_FAILURE(rc))
            RTMsgError("RTPathAppend failed when constructing log file path: %Rrc\n", rc);
    }
    else
        RTMsgError("RTPathTemp failed when constructing log file path: %Rrc\n", rc);

    if (RT_SUCCESS(rc))
    {
        RTUINT fFlags  = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
               fFlags |= RTLOGFLAGS_USECRLF;
#endif
        static const char * const s_apszLogGroups[] = VBOX_LOGGROUP_NAMES;
        rc = RTLogCreateEx(&g_pRelLogger, "VBOX_TXS_RELEASE_LOG", fFlags, "all",
                           RT_ELEMENTS(s_apszLogGroups), s_apszLogGroups, UINT32_MAX /* cMaxEntriesPerGroup */,
                           0 /*cBufDescs*/, NULL /* paBufDescs */, RTLOGDEST_STDOUT | RTLOGDEST_FILE,
                           logHeaderFooter /* pfnPhase */ ,
                           10 /* cHistory */, 100 * _1M /* cbHistoryFileMax */, RT_SEC_1DAY /* cSecsHistoryTimeSlot */,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           NULL /* pErrInfo */, "%s", szLogFile);
        if (RT_SUCCESS(rc))
        {
            RTLogRelSetDefaultInstance(g_pRelLogger);
            if (g_cVerbose)
            {
                RTMsgInfo("Setting verbosity logging to level %u\n", g_cVerbose);
                switch (g_cVerbose) /* Not very elegant, but has to do it for now. */
                {
                    case 1:
                        rc = RTLogGroupSettings(g_pRelLogger, "all.e.l.l2");
                        break;

                    case 2:
                        rc = RTLogGroupSettings(g_pRelLogger, "all.e.l.l2.l3");
                        break;

                    case 3:
                        rc = RTLogGroupSettings(g_pRelLogger, "all.e.l.l2.l3.l4");
                        break;

                    case 4:
                        RT_FALL_THROUGH();
                    default:
                        rc = RTLogGroupSettings(g_pRelLogger, "all.e.l.l2.l3.l4.f");
                        break;
                }
                if (RT_FAILURE(rc))
                    RTMsgError("Setting logging groups failed, rc=%Rrc\n", rc);
            }
        }
        else
            RTMsgError("Failed to create release logger: %Rrc", rc);

        if (RT_SUCCESS(rc))
            RTMsgInfo("Log file written to '%s'\n", szLogFile);
    }

    /*
     * Generate a UUID for this TXS instance.
     */
    rc = RTUuidCreate(&g_InstanceUuid);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTUuidCreate failed: %Rrc", rc);
    if (g_cVerbose > 0)
        RTMsgInfo("Instance UUID: %RTuuid", &g_InstanceUuid);

    /*
     * Finalize the scratch directory and initialize the transport layer.
     */
    rcExit = txsFinalizeScratch();
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    rc = g_pTransport->pfnInit();
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Ok, start working
     */
    rcExit = txsMainLoop();

    /*
     * Cleanup.
     */
    g_pTransport->pfnTerm();

    return rcExit;
}
