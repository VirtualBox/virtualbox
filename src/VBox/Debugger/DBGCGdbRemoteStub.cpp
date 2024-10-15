/* $Id: DBGCGdbRemoteStub.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, GDB Remote Stub.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/dbg.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/vmapi.h> /* VMR3GetVM() */
#include <VBox/vmm/hm.h>    /* HMR3IsEnabled */
#include <VBox/vmm/nem.h>   /* NEMR3IsEnabled */
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <stdlib.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Character indicating the start of a packet. */
#define GDBSTUB_PKT_START                   '$'
/** Character indicating the end of a packet (excluding the checksum). */
#define GDBSTUB_PKT_END                     '#'
/** The escape character. */
#define GDBSTUB_PKT_ESCAPE                  '{'
/** The out-of-band interrupt character. */
#define GDBSTUB_OOB_INTERRUPT               0x03


/** Indicate support for the 'qXfer:features:read' packet to support the target description. */
#define GDBSTUBCTX_FEATURES_F_TGT_DESC      RT_BIT(0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Trace point type.
 */
typedef enum GDBSTUBTPTYPE
{
    /** Invalid type, do not use. */
    GDBSTUBTPTYPE_INVALID = 0,
    /** An instruction software trace point. */
    GDBSTUBTPTYPE_EXEC_SW,
    /** An instruction hardware trace point. */
    GDBSTUBTPTYPE_EXEC_HW,
    /** A memory read trace point. */
    GDBSTUBTPTYPE_MEM_READ,
    /** A memory write trace point. */
    GDBSTUBTPTYPE_MEM_WRITE,
    /** A memory access trace point. */
    GDBSTUBTPTYPE_MEM_ACCESS,
    /** 32bit hack. */
    GDBSTUBTPTYPE_32BIT_HACK = 0x7fffffff
} GDBSTUBTPTYPE;


/**
 * GDB stub receive state.
 */
typedef enum GDBSTUBRECVSTATE
{
    /** Invalid state. */
    GDBSTUBRECVSTATE_INVALID = 0,
    /** Waiting for the start character. */
    GDBSTUBRECVSTATE_PACKET_WAIT_FOR_START,
    /** Reiceiving the packet body up until the END character. */
    GDBSTUBRECVSTATE_PACKET_RECEIVE_BODY,
    /** Receiving the checksum. */
    GDBSTUBRECVSTATE_PACKET_RECEIVE_CHECKSUM,
    /** Blow up the enum to 32bits for easier alignment of members in structs. */
    GDBSTUBRECVSTATE_32BIT_HACK = 0x7fffffff
} GDBSTUBRECVSTATE;


/**
 * GDB target register descriptor.
 */
typedef struct GDBREGDESC
{
    /** Register name. */
    const char                  *pszName;
    /** DBGF register index. */
    DBGFREG                     enmReg;
    /** Bitsize */
    uint32_t                    cBits;
    /** Type. */
    const char                  *pszType;
    /** Group. */
    const char                  *pszGroup;
} GDBREGDESC;
/** Pointer to a GDB target register descriptor. */
typedef GDBREGDESC *PGDBREGDESC;
/** Pointer to a const GDB target register descriptor. */
typedef const GDBREGDESC *PCGDBREGDESC;


/**
 * A tracepoint descriptor.
 */
typedef struct GDBSTUBTP
{
    /** List node for the list of tracepoints. */
    RTLISTNODE                  NdTps;
    /** The breakpoint number from the DBGF API. */
    uint32_t                    iBp;
    /** The tracepoint type for identification. */
    GDBSTUBTPTYPE               enmTpType;
    /** The tracepoint address for identification. */
    uint64_t                    GdbTgtAddr;
    /** The tracepoint kind for identification. */
    uint64_t                    uKind;
} GDBSTUBTP;
/** Pointer to a tracepoint. */
typedef GDBSTUBTP *PGDBSTUBTP;


/**
 * GDB stub context data.
 */
typedef struct GDBSTUBCTX
{
    /** Internal debugger console data. */
    DBGC                        Dbgc;
    /** The current state when receiving a new packet. */
    GDBSTUBRECVSTATE            enmState;
    /** Maximum number of bytes the packet buffer can hold. */
    size_t                      cbPktBufMax;
    /** Current offset into the packet buffer. */
    size_t                      offPktBuf;
    /** The size of the packet (minus the start, end characters and the checksum). */
    size_t                      cbPkt;
    /** Pointer to the packet buffer data. */
    uint8_t                     *pbPktBuf;
    /** Number of bytes left for the checksum. */
    size_t                      cbChksumRecvLeft;
    /** Send packet checksum. */
    uint8_t                     uChkSumSend;
    /** Feature flags supported we negotiated with the remote end. */
    uint32_t                    fFeatures;
    /** Pointer to the XML target description. */
    char                        *pachTgtXmlDesc;
    /** Size of the XML target description. */
    size_t                      cbTgtXmlDesc;
    /** Pointer to the selected GDB register set. */
    PCGDBREGDESC                paRegs;
    /** Number of entries in the register set. */
    uint32_t                    cRegs;
    /** Flag whether the stub is in extended mode. */
    bool                        fExtendedMode;
    /** Flag whether was something was output using the 'O' packet since it was reset last. */
    bool                        fOutput;
    /** List of registered trace points.
     * GDB removes breakpoints/watchpoints using the parameters they were
     * registered with while we only use the BP number form DBGF internally.
     * Means we have to track all registration so we can remove them later on. */
    RTLISTANCHOR                LstTps;
    /** Flag whether a ThreadInfo query was started. */
    bool                        fInThrdInfoQuery;
    /** Next ID to return in the current ThreadInfo query. */
    VMCPUID                     idCpuNextThrdInfoQuery;
} GDBSTUBCTX;
/** Pointer to the GDB stub context data. */
typedef GDBSTUBCTX *PGDBSTUBCTX;
/** Pointer to const GDB stub context data. */
typedef const GDBSTUBCTX *PCGDBSTUBCTX;
/** Pointer to a GDB stub context data pointer. */
typedef PGDBSTUBCTX *PPGDBSTUBCTX;


/**
 * Specific query packet processor callback.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbVal               Pointer to the remaining value.
 * @param   cbVal               Size of the remaining value in bytes.
 */
typedef DECLCALLBACKTYPE(int, FNGDBSTUBQPKTPROC,(PGDBSTUBCTX pThis, const uint8_t *pbVal, size_t cbVal));
typedef FNGDBSTUBQPKTPROC *PFNGDBSTUBQPKTPROC;


/**
 * 'q' packet processor.
 */
typedef struct GDBSTUBQPKTPROC
{
    /** Name */
    const char                  *pszName;
    /** Length of name in characters (without \0 terminator). */
    uint32_t                    cchName;
    /** The callback to call for processing the particular query. */
    PFNGDBSTUBQPKTPROC          pfnProc;
} GDBSTUBQPKTPROC;
/** Pointer to a 'q' packet processor entry. */
typedef GDBSTUBQPKTPROC *PGDBSTUBQPKTPROC;
/** Pointer to a const 'q' packet processor entry. */
typedef const GDBSTUBQPKTPROC *PCGDBSTUBQPKTPROC;


/**
 * 'v' packet processor.
 */
typedef struct GDBSTUBVPKTPROC
{
    /** Name */
    const char                  *pszName;
    /** Length of name in characters (without \0 terminator). */
    uint32_t                    cchName;
    /** Replay to a query packet (ends with ?). */
    const char                  *pszReplyQ;
    /** Length of the query reply (without \0 terminator). */
    uint32_t                    cchReplyQ;
    /** The callback to call for processing the particular query. */
    PFNGDBSTUBQPKTPROC          pfnProc;
} GDBSTUBVPKTPROC;
/** Pointer to a 'q' packet processor entry. */
typedef GDBSTUBVPKTPROC *PGDBSTUBVPKTPROC;
/** Pointer to a const 'q' packet processor entry. */
typedef const GDBSTUBVPKTPROC *PCGDBSTUBVPKTPROC;


/**
 * Feature callback.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbVal               Pointer to the value.
 * @param   cbVal               Size of the value in bytes.
 */
typedef DECLCALLBACKTYPE(int, FNGDBSTUBFEATHND,(PGDBSTUBCTX pThis, const uint8_t *pbVal, size_t cbVal));
typedef FNGDBSTUBFEATHND *PFNGDBSTUBFEATHND;


/**
 * GDB feature descriptor.
 */
typedef struct GDBSTUBFEATDESC
{
    /** Feature name */
    const char                  *pszName;
    /** Length of the feature name in characters (without \0 terminator). */
    uint32_t                    cchName;
    /** The callback to call for processing the particular feature. */
    PFNGDBSTUBFEATHND           pfnHandler;
    /** Flag whether the feature requires a value. */
    bool                        fVal;
} GDBSTUBFEATDESC;
/** Pointer to a GDB feature descriptor. */
typedef GDBSTUBFEATDESC *PGDBSTUBFEATDESC;
/** Pointer to a const GDB feature descriptor. */
typedef const GDBSTUBFEATDESC *PCGDBSTUBFEATDESC;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Tries to find a trace point with the given parameters in the list of registered trace points.
 *
 * @returns Pointer to the trace point registration record if found or NULL if none was found.
 * @param   pThis               The GDB stub context.
 * @param   enmTpType           The trace point type.
 * @param   GdbTgtAddr          Target address given by GDB.
 * @param   uKind               Trace point kind.
 */
static PGDBSTUBTP dbgcGdbStubTpFind(PGDBSTUBCTX pThis, GDBSTUBTPTYPE enmTpType, uint64_t GdbTgtAddr, uint64_t uKind)
{
    PGDBSTUBTP pTpCur = NULL;
    RTListForEach(&pThis->LstTps, pTpCur, GDBSTUBTP, NdTps)
    {
        if (   pTpCur->enmTpType == enmTpType
            && pTpCur->GdbTgtAddr == GdbTgtAddr
            && pTpCur->uKind == uKind)
            return pTpCur;
    }

    return NULL;
}


/**
 * Registers a new trace point.
 *
 * @returns VBox status code.
 * @param   pThis               The GDB stub context.
 * @param   enmTpType           The trace point type.
 * @param   GdbTgtAddr          Target address given by GDB.
 * @param   uKind               Trace point kind.
 * @param   iBp                 The internal DBGF breakpoint ID this trace point was registered with.
 */
static int dbgcGdbStubTpRegister(PGDBSTUBCTX pThis, GDBSTUBTPTYPE enmTpType, uint64_t GdbTgtAddr, uint64_t uKind, uint32_t iBp)
{
    int rc = VERR_ALREADY_EXISTS;

    /* Can't register a tracepoint with the same parameters twice or we can't decide whom to remove later on. */
    PGDBSTUBTP pTp = dbgcGdbStubTpFind(pThis, enmTpType, GdbTgtAddr, uKind);
    if (!pTp)
    {
        pTp = (PGDBSTUBTP)RTMemAllocZ(sizeof(*pTp));
        if (pTp)
        {
            pTp->enmTpType  = enmTpType;
            pTp->GdbTgtAddr = GdbTgtAddr;
            pTp->uKind      = uKind;
            pTp->iBp        = iBp;
            RTListAppend(&pThis->LstTps, &pTp->NdTps);
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Deregisters the given trace point (needs to be unregistered from DBGF by the caller before).
 *
 * @param   pTp                 The trace point to deregister.
 */
static void dbgcGdbStubTpDeregister(PGDBSTUBTP pTp)
{
    RTListNodeRemove(&pTp->NdTps);
    RTMemFree(pTp);
}


/**
 * Converts a given to the hexadecimal value if valid.
 *
 * @returns The hexadecimal value the given character represents 0-9,a-f,A-F or 0xff on error.
 * @param   ch                  The character to convert.
 */
DECLINLINE(uint8_t) dbgcGdbStubCtxChrToHex(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 0xa;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 0xa;

    return 0xff;
}


/**
 * Converts a 4bit hex number to the appropriate character.
 *
 * @returns Character representing the 4bit hex number.
 * @param   uHex                The 4 bit hex number.
 */
DECLINLINE(char) dbgcGdbStubCtxHexToChr(uint8_t uHex)
{
    if (uHex < 0xa)
        return '0' + uHex;
    if (uHex <= 0xf)
        return 'A' + uHex - 0xa;

    return 'X';
}


/**
 * Wrapper for the I/O interface write callback.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pvPkt               The packet data to send.
 * @param   cbPkt               Size of the packet in bytes.
 */
DECLINLINE(int) dbgcGdbStubCtxWrite(PGDBSTUBCTX pThis, const void *pvPkt, size_t cbPkt)
{
    return pThis->Dbgc.pIo->pfnWrite(pThis->Dbgc.pIo, pvPkt, cbPkt, NULL /*pcbWritten*/);
}


/**
 * Starts transmission of a new reply packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxReplySendBegin(PGDBSTUBCTX pThis)
{
    pThis->uChkSumSend = 0;

    uint8_t chPktStart = GDBSTUB_PKT_START;
    return dbgcGdbStubCtxWrite(pThis, &chPktStart, sizeof(chPktStart));
}


/**
 * Sends the given data in the reply.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pvReplyData         The reply data to send.
 * @param   cbReplyData         Size of the reply data in bytes.
 */
static int dbgcGdbStubCtxReplySendData(PGDBSTUBCTX pThis, const void *pvReplyData, size_t cbReplyData)
{
    /* Update checksum. */
    const uint8_t *pbData = (const uint8_t *)pvReplyData;
    for (uint32_t i = 0; i < cbReplyData; i++)
        pThis->uChkSumSend += pbData[i];

    return dbgcGdbStubCtxWrite(pThis, pvReplyData, cbReplyData);
}


/**
 * Finishes transmission of the current reply by sending the packet end character and the checksum.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxReplySendEnd(PGDBSTUBCTX pThis)
{
    uint8_t achPktEnd[3];

    achPktEnd[0] = GDBSTUB_PKT_END;
    achPktEnd[1] = dbgcGdbStubCtxHexToChr(pThis->uChkSumSend >> 4);
    achPktEnd[2] = dbgcGdbStubCtxHexToChr(pThis->uChkSumSend & 0xf);

    return dbgcGdbStubCtxWrite(pThis, &achPktEnd[0], sizeof(achPktEnd));
}


/**
 * Sends the given reply packet, doing the framing, checksumming, etc. in one call.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pvReplyPkt          The reply packet to send.
 * @param   cbReplyPkt          Size of the reply packet in bytes.
 */
static int dbgcGdbStubCtxReplySend(PGDBSTUBCTX pThis, const void *pvReplyPkt, size_t cbReplyPkt)
{
    int rc = dbgcGdbStubCtxReplySendBegin(pThis);
    if (RT_SUCCESS(rc))
    {
        rc = dbgcGdbStubCtxReplySendData(pThis, pvReplyPkt, cbReplyPkt);
        if (RT_SUCCESS(rc))
            rc = dbgcGdbStubCtxReplySendEnd(pThis);
    }

    return rc;
}


/**
 * Encodes the given buffer as a hexstring string it into the given destination buffer.
 *
 * @returns Status code.
 * @param   pbDst               Where store the resulting hex string on success.
 * @param   cbDst               Size of the destination buffer in bytes.
 * @param   pvSrc               The data to encode.
 * @param   cbSrc               Number of bytes to encode.
 */
DECLINLINE(int) dbgcGdbStubCtxEncodeBinaryAsHex(uint8_t *pbDst, size_t cbDst, const void *pvSrc, size_t cbSrc)
{
    return RTStrPrintHexBytes((char *)pbDst, cbDst, pvSrc, cbSrc, RTSTRPRINTHEXBYTES_F_UPPER);
}


/**
 * Decodes the given ASCII hexstring as binary data up until the given separator is found or the end of the string is reached.
 *
 * @returns Status code.
 * @param   pbBuf               The buffer containing the hexstring to convert.
 * @param   cbBuf               Size of the buffer in bytes.
 * @param   puVal               Where to store the decoded integer.
 * @param   chSep               The character to stop conversion at.
 * @param   ppbSep              Where to store the pointer in the buffer where the separator was found, optional.
 */
static int dbgcGdbStubCtxParseHexStringAsInteger(const uint8_t *pbBuf, size_t cbBuf, uint64_t *puVal, uint8_t chSep, const uint8_t **ppbSep)
{
    uint64_t uVal = 0;

    while (   cbBuf
           && *pbBuf != chSep)
    {
        uVal = uVal * 16 + dbgcGdbStubCtxChrToHex(*pbBuf++);
        cbBuf--;
    }

    *puVal = uVal;

    if (ppbSep)
        *ppbSep = pbBuf;

    return VINF_SUCCESS;
}


/**
 * Decodes the given ASCII hexstring as a byte buffer up until the given separator is found or the end of the string is reached.
 *
 * @returns Status code.
 * @param   pbBuf               The buffer containing the hexstring to convert.
 * @param   cbBuf               Size of the buffer in bytes.
 * @param   pvDst               Where to store the decoded data.
 * @param   cbDst               Maximum buffer size in bytes.
 * @param   pcbDecoded          Where to store the number of consumed bytes from the input.
 */
DECLINLINE(int) dbgcGdbStubCtxParseHexStringAsByteBuf(const uint8_t *pbBuf, size_t cbBuf, void *pvDst, size_t cbDst, size_t *pcbDecoded)
{
    size_t cbDecode = RT_MIN(cbBuf, cbDst * 2);

    if (pcbDecoded)
        *pcbDecoded = cbDecode;

    return RTStrConvertHexBytes((const char *)pbBuf, pvDst, cbDecode, 0 /* fFlags*/);
}

#if 0 /*unused for now*/
/**
 * Sends a 'OK' part of a reply packet only (packet start and end needs to be handled separately).
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxReplySendOkData(PGDBSTUBCTX pThis)
{
    char achOk[2] = { 'O', 'K' };
    return dbgcGdbStubCtxReplySendData(pThis, &achOk[0], sizeof(achOk));
}
#endif


/**
 * Sends a 'OK' reply packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxReplySendOk(PGDBSTUBCTX pThis)
{
    char achOk[2] = { 'O', 'K' };
    return dbgcGdbStubCtxReplySend(pThis, &achOk[0], sizeof(achOk));
}

#if 0 /*unused for now*/
/**
 * Sends a 'E NN' part of a reply packet only (packet start and end needs to be handled separately).
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   uErr                The error code to send.
 */
static int dbgcGdbStubCtxReplySendErrData(PGDBSTUBCTX pThis, uint8_t uErr)
{
    char achErr[3] = { 'E', 0, 0 };
    achErr[1] = dbgcGdbStubCtxHexToChr(uErr >> 4);
    achErr[2] = dbgcGdbStubCtxHexToChr(uErr & 0xf);
    return dbgcGdbStubCtxReplySendData(pThis, &achErr[0], sizeof(achErr));
}
#endif

/**
 * Sends a 'E NN' reply packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   uErr                The error code to send.
 */
static int dbgcGdbStubCtxReplySendErr(PGDBSTUBCTX pThis, uint8_t uErr)
{
    char achErr[3] = { 'E', 0, 0 };
    achErr[1] = dbgcGdbStubCtxHexToChr(uErr >> 4);
    achErr[2] = dbgcGdbStubCtxHexToChr(uErr & 0xf);
    return dbgcGdbStubCtxReplySend(pThis, &achErr[0], sizeof(achErr));
}


/**
 * Sends a signal trap (S 05) packet to indicate that the target has stopped.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxReplySendSigTrap(PGDBSTUBCTX pThis)
{
    char achReply[32];
    ssize_t cchStr = RTStrPrintf2(&achReply[0], sizeof(achReply), "T05thread:%02x;", pThis->Dbgc.idCpu + 1);
    return dbgcGdbStubCtxReplySend(pThis, &achReply[0], cchStr);
}


/**
 * Sends a GDB stub status code indicating an error using the error reply packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   rc                  The status code to send.
 */
static int dbgcGdbStubCtxReplySendErrSts(PGDBSTUBCTX pThis, int rc)
{
    /** @todo convert error codes maybe. */
    return dbgcGdbStubCtxReplySendErr(pThis, (-rc) & 0xff);
}


/**
 * Ensures that there is at least the given amount of bytes of free space left in the packet buffer.
 *
 * @returns Status code (error when increasing the buffer failed).
 * @param   pThis               The GDB stub context.
 * @param   cbSpace             Number of bytes required.
 */
static int dbgcGdbStubCtxEnsurePktBufSpace(PGDBSTUBCTX pThis, size_t cbSpace)
{
    if (pThis->cbPktBufMax - pThis->offPktBuf >= cbSpace)
        return VINF_SUCCESS;

    /* Slow path allocate new buffer and copy content over. */
    int rc = VINF_SUCCESS;
    size_t cbPktBufMaxNew = pThis->cbPktBufMax + cbSpace;
    void *pvNew = RTMemRealloc(pThis->pbPktBuf, cbPktBufMaxNew);
    if (pvNew)
    {
        pThis->pbPktBuf    = (uint8_t *)pvNew;
        pThis->cbPktBufMax = cbPktBufMaxNew;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Parses the arguments of a 'Z' and 'z' packet.
 *
 * @returns Status code.
 * @param   pbArgs                  Pointer to the start of the first argument.
 * @param   cbArgs                  Number of argument bytes.
 * @param   penmTpType              Where to store the tracepoint type on success.
 * @param   pGdbTgtAddr             Where to store the address on success.
 * @param   puKind                  Where to store the kind argument on success.
 */
static int dbgcGdbStubCtxParseTpPktArgs(const uint8_t *pbArgs, size_t cbArgs, GDBSTUBTPTYPE *penmTpType, uint64_t *pGdbTgtAddr, uint64_t *puKind)
{
    const uint8_t *pbPktSep = NULL;
    uint64_t uType = 0;

    int rc = dbgcGdbStubCtxParseHexStringAsInteger(pbArgs, cbArgs, &uType,
                                                   ',', &pbPktSep);
    if (RT_SUCCESS(rc))
    {
        cbArgs -= (uintptr_t)(pbPktSep - pbArgs) - 1;
        rc = dbgcGdbStubCtxParseHexStringAsInteger(pbPktSep + 1, cbArgs, pGdbTgtAddr,
                                                   ',', &pbPktSep);
        if (RT_SUCCESS(rc))
        {
            cbArgs -= (uintptr_t)(pbPktSep - pbArgs) - 1;
            rc = dbgcGdbStubCtxParseHexStringAsInteger(pbPktSep + 1, cbArgs, puKind,
                                                       GDBSTUB_PKT_END, NULL);
            if (RT_SUCCESS(rc))
            {
                switch (uType)
                {
                    case 0:
                        *penmTpType = GDBSTUBTPTYPE_EXEC_SW;
                        break;
                    case 1:
                        *penmTpType = GDBSTUBTPTYPE_EXEC_HW;
                        break;
                    case 2:
                        *penmTpType = GDBSTUBTPTYPE_MEM_WRITE;
                        break;
                    case 3:
                        *penmTpType = GDBSTUBTPTYPE_MEM_READ;
                        break;
                    case 4:
                        *penmTpType = GDBSTUBTPTYPE_MEM_ACCESS;
                        break;
                    default:
                        rc = VERR_INVALID_PARAMETER;
                        break;
                }
            }
        }
    }

    return rc;
}


/**
 * Processes the 'TStatus' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryTStatus(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    RT_NOREF(pbArgs, cbArgs);

    char achReply[2] = { 'T', '0' };
    return dbgcGdbStubCtxReplySend(pThis, &achReply[0], sizeof(achReply));
}


/**
 * @copydoc FNGDBSTUBQPKTPROC
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessFeatXmlRegs(PGDBSTUBCTX pThis, const uint8_t *pbVal, size_t cbVal)
{
    /*
     * xmlRegisters contain a list of supported architectures delimited by ','.
     * Check that the architecture is in the supported list.
     */
    while (cbVal)
    {
        /* Find the next delimiter. */
        size_t cbThisVal = cbVal;
        const uint8_t *pbDelim = (const uint8_t *)memchr(pbVal, ',', cbVal);
        if (pbDelim)
            cbThisVal = pbDelim - pbVal;

        const size_t cchArch64 = sizeof("i386:x86-64") - 1;
        const size_t cchArch32 = sizeof("i386") - 1;
        if (   !memcmp(pbVal, "i386:x86-64", RT_MIN(cbVal, cchArch64))
            || !memcmp(pbVal, "i386", RT_MIN(cbVal, cchArch32)))
        {
            /* Set the flag to support the qXfer:features:read packet. */
            pThis->fFeatures |= GDBSTUBCTX_FEATURES_F_TGT_DESC;
            break;
        }

        cbVal -= cbThisVal + (pbDelim ? 1 : 0);
        pbVal = pbDelim + (pbDelim ? 1 : 0);
    }

    return VINF_SUCCESS;
}


/**
 * Features which can be reported by the remote GDB which we might support.
 *
 * @note The sorting matters for features which start the same, the longest must come first.
 */
static const GDBSTUBFEATDESC g_aGdbFeatures[] =
{
#define GDBSTUBFEATDESC_INIT(a_Name, a_pfnHnd, a_fVal) { a_Name, sizeof(a_Name) - 1, a_pfnHnd, a_fVal }
    GDBSTUBFEATDESC_INIT("xmlRegisters",   dbgcGdbStubCtxPktProcessFeatXmlRegs, true),
#undef GDBSTUBFEATDESC_INIT
};


/**
 * Calculates the feature length of the next feature pointed to by the given arguments buffer.
 *
 * @returns Status code.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 * @param   pcbArg              Where to store the size of the argument in bytes on success (excluding the delimiter).
 * @param   pfTerminator        Whereto store the flag whether the packet terminator (#) was seen as a delimiter.
 */
static int dbgcGdbStubCtxQueryPktQueryFeatureLen(const uint8_t *pbArgs, size_t cbArgs, size_t *pcbArg, bool *pfTerminator)
{
    const uint8_t *pbArgCur = pbArgs;

    while (   cbArgs
           && *pbArgCur != ';'
           && *pbArgCur != GDBSTUB_PKT_END)
    {
        cbArgs--;
        pbArgCur++;
    }

    if (   !cbArgs
        && *pbArgCur != ';'
        && *pbArgCur != GDBSTUB_PKT_END)
        return VERR_NET_PROTOCOL_ERROR;

    *pcbArg       = pbArgCur - pbArgs;
    *pfTerminator = *pbArgCur == GDBSTUB_PKT_END ? true : false;

    return VINF_SUCCESS;
}


/**
 * Sends the reply to the 'qSupported' packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxPktProcessQuerySupportedReply(PGDBSTUBCTX pThis)
{
    /** @todo Enhance. */
    if (pThis->fFeatures & GDBSTUBCTX_FEATURES_F_TGT_DESC)
        return dbgcGdbStubCtxReplySend(pThis, "qXfer:features:read+;vContSupported+", sizeof("qXfer:features:read+;vContSupported+") - 1);

    return dbgcGdbStubCtxReplySend(pThis, NULL, 0);
}


/**
 * Processes the 'Supported' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQuerySupported(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    /* Skip the : following the qSupported start. */
    if (   cbArgs < 1
        || pbArgs[0] != ':')
        return VERR_NET_PROTOCOL_ERROR;

    cbArgs--;
    pbArgs++;

    /*
     * Each feature but the last one are separated by ; and the last one is delimited by the # packet end symbol.
     * We first determine the boundaries of the reported feature and pass it to the appropriate handler.
     */
    int rc = VINF_SUCCESS;
    while (   cbArgs
           && RT_SUCCESS(rc))
    {
        bool fTerminator = false;
        size_t cbArg = 0;
        rc = dbgcGdbStubCtxQueryPktQueryFeatureLen(pbArgs, cbArgs, &cbArg, &fTerminator);
        if (RT_SUCCESS(rc))
        {
            /* Search for the feature handler. */
            for (uint32_t i = 0; i < RT_ELEMENTS(g_aGdbFeatures); i++)
            {
                PCGDBSTUBFEATDESC pFeatDesc = &g_aGdbFeatures[i];

                if (   cbArg > pFeatDesc->cchName /* At least one character must come after the feature name ('+', '-' or '='). */
                    && !memcmp(pFeatDesc->pszName, pbArgs, pFeatDesc->cchName))
                {
                    /* Found, execute handler after figuring out whether there is a value attached. */
                    const uint8_t *pbVal = pbArgs + pFeatDesc->cchName;
                    size_t cbVal = cbArg - pFeatDesc->cchName;

                    if (pFeatDesc->fVal)
                    {
                        if (   *pbVal == '='
                            && cbVal > 1)
                        {
                            pbVal++;
                            cbVal--;
                        }
                        else
                            rc = VERR_NET_PROTOCOL_ERROR;
                    }
                    else if (   cbVal != 1
                             || (   *pbVal != '+'
                                 && *pbVal != '-')) /* '+' and '-' are allowed to indicate support for a particular feature. */
                        rc = VERR_NET_PROTOCOL_ERROR;

                    if (RT_SUCCESS(rc))
                        rc = pFeatDesc->pfnHandler(pThis, pbVal, cbVal);
                    break;
                }
            }

            cbArgs -= cbArg;
            pbArgs += cbArg;
            if (!fTerminator)
            {
                cbArgs--;
                pbArgs++;
            }
            else
                break;
        }
    }

    /* If everything went alright send the reply with our supported features. */
    if (RT_SUCCESS(rc))
        rc = dbgcGdbStubCtxPktProcessQuerySupportedReply(pThis);

    return rc;
}


/**
 * Sends the reply to a 'qXfer:object:read:...' request.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   offRead             Where to start reading from within the object.
 * @param   cbRead              How much to read.
 * @param   pbObj               The start of the object.
 * @param   cbObj               Size of the object.
 */
static int dbgcGdbStubCtxQueryXferReadReply(PGDBSTUBCTX pThis, uint32_t offRead, size_t cbRead, const uint8_t *pbObj, size_t cbObj)
{
    int rc = VINF_SUCCESS;
    if (offRead < cbObj)
    {
        /** @todo Escaping */
        size_t cbThisRead = offRead + cbRead < cbObj ? cbRead : cbObj - offRead;

        rc = dbgcGdbStubCtxEnsurePktBufSpace(pThis, cbThisRead + 1);
        if (RT_SUCCESS(rc))
        {
            uint8_t *pbPktBuf = pThis->pbPktBuf;
            *pbPktBuf++ = cbThisRead < cbRead ? 'l' : 'm';
            memcpy(pbPktBuf, pbObj + offRead, cbThisRead);
            rc = dbgcGdbStubCtxReplySend(pThis, pThis->pbPktBuf, cbThisRead + 1);
        }
        else
            rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NO_MEMORY);
    }
    else if (offRead == cbObj)
        rc = dbgcGdbStubCtxReplySend(pThis, "l", sizeof("l") - 1);
    else
        rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);

    return rc;
}


/**
 * Parses the annex:offset,length part of a 'qXfer:object:read:...' request.
 *
 * @returns Status code.
 * @param   pbArgs              Start of the arguments beginning with annex.
 * @param   cbArgs              Number of bytes remaining for the arguments.
 * @param   ppchAnnex           Where to store the pointer to the beginning of the annex on success.
 * @param   pcchAnnex           Where to store the number of characters for the annex on success.
 * @param   poffRead            Where to store the offset on success.
 * @param   pcbRead             Where to store the length on success.
 */
static int dbgcGdbStubCtxPktProcessQueryXferParseAnnexOffLen(const uint8_t *pbArgs, size_t cbArgs, const char **ppchAnnex, size_t *pcchAnnex,
                                                             uint32_t *poffRead, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;
    const uint8_t *pbSep = (const uint8_t *)memchr(pbArgs, ':', cbArgs);
    if (pbSep)
    {
        *ppchAnnex = (const char *)pbArgs;
        *pcchAnnex = pbSep - pbArgs;

        pbSep++;
        cbArgs -= *pcchAnnex + 1;

        uint64_t u64Tmp = 0;
        const uint8_t *pbLenStart = NULL;
        rc = dbgcGdbStubCtxParseHexStringAsInteger(pbSep, cbArgs, &u64Tmp, ',', &pbLenStart);
        if (   RT_SUCCESS(rc)
            && (uint32_t)u64Tmp == u64Tmp)
        {
            *poffRead = (uint32_t)u64Tmp;
            cbArgs -= pbLenStart - pbSep;

            rc = dbgcGdbStubCtxParseHexStringAsInteger(pbLenStart + 1, cbArgs, &u64Tmp, '#', &pbLenStart);
            if (   RT_SUCCESS(rc)
                && (size_t)u64Tmp == u64Tmp)
                *pcbRead = (size_t)u64Tmp;
            else
                rc = VERR_NET_PROTOCOL_ERROR;
        }
        else
            rc = VERR_NET_PROTOCOL_ERROR;
    }
    else
        rc = VERR_NET_PROTOCOL_ERROR;

    return rc;
}


#define DBGREG_DESC_INIT_INT64(a_Name, a_enmDbgfReg)    { a_Name, a_enmDbgfReg, 64, "int64",    NULL }
#define DBGREG_DESC_INIT_INT32(a_Name, a_enmDbgfReg)    { a_Name, a_enmDbgfReg, 32, "int32",    NULL }
#define DBGREG_DESC_INIT_DATA_PTR64(a_Name, a_enmDbgfReg) { a_Name, a_enmDbgfReg, 64, "data_ptr", NULL }
#define DBGREG_DESC_INIT_CODE_PTR64(a_Name, a_enmDbgfReg) { a_Name, a_enmDbgfReg, 64, "code_ptr", NULL }
#define DBGREG_DESC_INIT_DATA_PTR32(a_Name, a_enmDbgfReg) { a_Name, a_enmDbgfReg, 32, "data_ptr", NULL }
#define DBGREG_DESC_INIT_CODE_PTR32(a_Name, a_enmDbgfReg) { a_Name, a_enmDbgfReg, 32, "code_ptr", NULL }
#define DBGREG_DESC_INIT_X87(a_Name, a_enmDbgfReg)      { a_Name, a_enmDbgfReg, 80, "i387_ext", NULL }
#define DBGREG_DESC_INIT_X87_CTRL(a_Name, a_enmDbgfReg) { a_Name, a_enmDbgfReg, 32, "int",      "float" }


/**
 * amd64 GDB register set.
 */
static const GDBREGDESC g_aGdbRegs64[] =
{
    DBGREG_DESC_INIT_INT64(     "rax",    DBGFREG_RAX),
    DBGREG_DESC_INIT_INT64(     "rbx",    DBGFREG_RBX),
    DBGREG_DESC_INIT_INT64(     "rcx",    DBGFREG_RCX),
    DBGREG_DESC_INIT_INT64(     "rdx",    DBGFREG_RDX),
    DBGREG_DESC_INIT_INT64(     "rsi",    DBGFREG_RSI),
    DBGREG_DESC_INIT_INT64(     "rdi",    DBGFREG_RDI),
    DBGREG_DESC_INIT_DATA_PTR64("rbp",    DBGFREG_RBP),
    DBGREG_DESC_INIT_DATA_PTR64("rsp",    DBGFREG_RSP),
    DBGREG_DESC_INIT_INT64(     "r8",     DBGFREG_R8),
    DBGREG_DESC_INIT_INT64(     "r9",     DBGFREG_R9),
    DBGREG_DESC_INIT_INT64(     "r10",    DBGFREG_R10),
    DBGREG_DESC_INIT_INT64(     "r11",    DBGFREG_R11),
    DBGREG_DESC_INIT_INT64(     "r12",    DBGFREG_R12),
    DBGREG_DESC_INIT_INT64(     "r13",    DBGFREG_R13),
    DBGREG_DESC_INIT_INT64(     "r14",    DBGFREG_R14),
    DBGREG_DESC_INIT_INT64(     "r15",    DBGFREG_R15),
    DBGREG_DESC_INIT_CODE_PTR64("rip",    DBGFREG_RIP),
    DBGREG_DESC_INIT_INT32(     "eflags", DBGFREG_FLAGS),
    DBGREG_DESC_INIT_INT32(     "cs",     DBGFREG_CS),
    DBGREG_DESC_INIT_INT32(     "ss",     DBGFREG_SS),
    DBGREG_DESC_INIT_INT32(     "ds",     DBGFREG_DS),
    DBGREG_DESC_INIT_INT32(     "es",     DBGFREG_ES),
    DBGREG_DESC_INIT_INT32(     "fs",     DBGFREG_FS),
    DBGREG_DESC_INIT_INT32(     "gs",     DBGFREG_GS),

    DBGREG_DESC_INIT_X87(       "st0",    DBGFREG_ST0),
    DBGREG_DESC_INIT_X87(       "st1",    DBGFREG_ST1),
    DBGREG_DESC_INIT_X87(       "st2",    DBGFREG_ST2),
    DBGREG_DESC_INIT_X87(       "st3",    DBGFREG_ST3),
    DBGREG_DESC_INIT_X87(       "st4",    DBGFREG_ST4),
    DBGREG_DESC_INIT_X87(       "st5",    DBGFREG_ST5),
    DBGREG_DESC_INIT_X87(       "st6",    DBGFREG_ST6),
    DBGREG_DESC_INIT_X87(       "st7",    DBGFREG_ST7),

    DBGREG_DESC_INIT_X87_CTRL(  "fctrl",  DBGFREG_FCW),
    DBGREG_DESC_INIT_X87_CTRL(  "fstat",  DBGFREG_FSW),
    DBGREG_DESC_INIT_X87_CTRL(  "ftag",   DBGFREG_FTW),
    DBGREG_DESC_INIT_X87_CTRL(  "fop",    DBGFREG_FOP),
    DBGREG_DESC_INIT_X87_CTRL(  "fioff",  DBGFREG_FPUIP),
    DBGREG_DESC_INIT_X87_CTRL(  "fiseg",  DBGFREG_FPUCS),
    DBGREG_DESC_INIT_X87_CTRL(  "fooff",  DBGFREG_FPUDP),
    DBGREG_DESC_INIT_X87_CTRL(  "foseg",  DBGFREG_FPUDS)
};


/**
 * i386 GDB register set.
 */
static const GDBREGDESC g_aGdbRegs32[] =
{
    DBGREG_DESC_INIT_INT32(     "eax",    DBGFREG_EAX),
    DBGREG_DESC_INIT_INT32(     "ebx",    DBGFREG_EBX),
    DBGREG_DESC_INIT_INT32(     "ecx",    DBGFREG_ECX),
    DBGREG_DESC_INIT_INT32(     "edx",    DBGFREG_EDX),
    DBGREG_DESC_INIT_INT32(     "esi",    DBGFREG_ESI),
    DBGREG_DESC_INIT_INT32(     "edi",    DBGFREG_EDI),
    DBGREG_DESC_INIT_DATA_PTR32("ebp",    DBGFREG_EBP),
    DBGREG_DESC_INIT_DATA_PTR32("esp",    DBGFREG_ESP),
    DBGREG_DESC_INIT_CODE_PTR32("eip",    DBGFREG_EIP),
    DBGREG_DESC_INIT_INT32(     "eflags", DBGFREG_FLAGS),
    DBGREG_DESC_INIT_INT32(     "cs",     DBGFREG_CS),
    DBGREG_DESC_INIT_INT32(     "ss",     DBGFREG_SS),
    DBGREG_DESC_INIT_INT32(     "ds",     DBGFREG_DS),
    DBGREG_DESC_INIT_INT32(     "es",     DBGFREG_ES),
    DBGREG_DESC_INIT_INT32(     "fs",     DBGFREG_FS),
    DBGREG_DESC_INIT_INT32(     "gs",     DBGFREG_GS),

    DBGREG_DESC_INIT_X87(       "st0",    DBGFREG_ST0),
    DBGREG_DESC_INIT_X87(       "st1",    DBGFREG_ST1),
    DBGREG_DESC_INIT_X87(       "st2",    DBGFREG_ST2),
    DBGREG_DESC_INIT_X87(       "st3",    DBGFREG_ST3),
    DBGREG_DESC_INIT_X87(       "st4",    DBGFREG_ST4),
    DBGREG_DESC_INIT_X87(       "st5",    DBGFREG_ST5),
    DBGREG_DESC_INIT_X87(       "st6",    DBGFREG_ST6),
    DBGREG_DESC_INIT_X87(       "st7",    DBGFREG_ST7),

    DBGREG_DESC_INIT_X87_CTRL(  "fctrl",  DBGFREG_FCW),
    DBGREG_DESC_INIT_X87_CTRL(  "fstat",  DBGFREG_FSW),
    DBGREG_DESC_INIT_X87_CTRL(  "ftag",   DBGFREG_FTW),
    DBGREG_DESC_INIT_X87_CTRL(  "fop",    DBGFREG_FOP),
    DBGREG_DESC_INIT_X87_CTRL(  "fioff",  DBGFREG_FPUIP),
    DBGREG_DESC_INIT_X87_CTRL(  "fiseg",  DBGFREG_FPUCS),
    DBGREG_DESC_INIT_X87_CTRL(  "fooff",  DBGFREG_FPUDP),
    DBGREG_DESC_INIT_X87_CTRL(  "foseg",  DBGFREG_FPUDS)
};

#undef DBGREG_DESC_INIT_CODE_PTR64
#undef DBGREG_DESC_INIT_DATA_PTR64
#undef DBGREG_DESC_INIT_CODE_PTR32
#undef DBGREG_DESC_INIT_DATA_PTR32
#undef DBGREG_DESC_INIT_INT32
#undef DBGREG_DESC_INIT_INT64
#undef DBGREG_DESC_INIT_X87
#undef DBGREG_DESC_INIT_X87_CTRL


/**
 * Creates the target XML description.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxTgtXmlDescCreate(PGDBSTUBCTX pThis)
{
    static const char s_szXmlTgtHdr64[] =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
        "<target version=\"1.0\">\n"
        "    <architecture>i386:x86-64</architecture>\n"
        "    <feature name=\"org.gnu.gdb.i386.core\">\n";
    static const char s_szXmlTgtHdr32[] =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
        "<target version=\"1.0\">\n"
        "    <architecture>i386</architecture>\n"
        "    <feature name=\"org.gnu.gdb.i386.core\">\n";
    static const char s_szXmlTgtFooter[] =
        "    </feature>\n"
        "</target>\n";

    int rc = VINF_SUCCESS;

    pThis->pachTgtXmlDesc = (char *)RTStrAlloc(_32K);
    if (pThis->pachTgtXmlDesc)
    {
        size_t cbLeft       = _32K;
        char *pachXmlCur    = pThis->pachTgtXmlDesc;
        pThis->cbTgtXmlDesc = cbLeft;

        rc = RTStrCatP(&pachXmlCur, &cbLeft, pThis->paRegs == &g_aGdbRegs64[0] ? &s_szXmlTgtHdr64[0] : &s_szXmlTgtHdr32[0]);
        if (RT_SUCCESS(rc))
        {
            /* Register */
            for (uint32_t i = 0; i < pThis->cRegs && RT_SUCCESS(rc); i++)
            {
                const struct GDBREGDESC *pReg = &pThis->paRegs[i];

                ssize_t cchStr = 0;
                if (pReg->pszGroup)
                   cchStr = RTStrPrintf2(pachXmlCur, cbLeft,
                                         "<reg name=\"%s\" bitsize=\"%u\" regnum=\"%u\" type=\"%s\" group=\"%s\"/>\n",
                                         pReg->pszName, pReg->cBits, i, pReg->pszType, pReg->pszGroup);
                else
                   cchStr = RTStrPrintf2(pachXmlCur, cbLeft,
                                         "<reg name=\"%s\" bitsize=\"%u\" regnum=\"%u\" type=\"%s\"/>\n",
                                         pReg->pszName, pReg->cBits, i, pReg->pszType);

                if (cchStr > 0)
                {
                    pachXmlCur += cchStr;
                    cbLeft     -= cchStr;
                }
                else
                    rc = VERR_BUFFER_OVERFLOW;
            }
        }

        if (RT_SUCCESS(rc))
            rc = RTStrCatP(&pachXmlCur, &cbLeft, &s_szXmlTgtFooter[0]);

        pThis->cbTgtXmlDesc -= cbLeft;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Returns the GDB register descriptor describing the given DBGF register enum.
 *
 * @returns Pointer to the GDB register descriptor or NULL if not found.
 * @param   pThis               The GDB stub context.
 * @param   idxReg              The register to look for.
 */
static const GDBREGDESC *dbgcGdbStubRegGet(PGDBSTUBCTX pThis, uint32_t idxReg)
{
    if (RT_LIKELY(idxReg < pThis->cRegs))
        return &pThis->paRegs[idxReg];

    return NULL;
}


/**
 * Processes the 'C' query (query current thread ID).
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryThreadId(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    RT_NOREF(pbArgs, cbArgs);

    int rc = VERR_BUFFER_OVERFLOW;
    char achReply[32];
    ssize_t cchStr = RTStrPrintf(&achReply[0], sizeof(achReply), "QC %02x", pThis->Dbgc.idCpu + 1);
    if (cchStr > 0)
        rc = dbgcGdbStubCtxReplySend(pThis, &achReply[0], cchStr);

    return rc;
}


/**
 * Processes the 'Attached' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryAttached(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    RT_NOREF(pbArgs, cbArgs);

    /* We always report attached so that the VM doesn't get killed when GDB quits. */
    uint8_t bAttached = '1';
    return dbgcGdbStubCtxReplySend(pThis, &bAttached, sizeof(bAttached));
}


/**
 * Processes the 'Xfer:features:read' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryXferFeatRead(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    /* Skip the : following the Xfer:features:read start. */
    if (   cbArgs < 1
        || pbArgs[0] != ':')
        return VERR_NET_PROTOCOL_ERROR;

    cbArgs--;
    pbArgs++;

    int rc = VINF_SUCCESS;
    if (pThis->fFeatures & GDBSTUBCTX_FEATURES_F_TGT_DESC)
    {
        /* Create the target XML description if not existing. */
        if (!pThis->pachTgtXmlDesc)
            rc = dbgcGdbStubCtxTgtXmlDescCreate(pThis);

        if (RT_SUCCESS(rc))
        {
            /* Parse annex, offset and length and return the data. */
            const char *pchAnnex = NULL;
            size_t cchAnnex = 0;
            uint32_t offRead = 0;
            size_t cbRead = 0;

            rc = dbgcGdbStubCtxPktProcessQueryXferParseAnnexOffLen(pbArgs, cbArgs,
                                                                   &pchAnnex, &cchAnnex,
                                                                   &offRead, &cbRead);
            if (RT_SUCCESS(rc))
            {
                /* Check whether the annex is supported. */
                if (   cchAnnex == sizeof("target.xml") - 1
                    && !memcmp(pchAnnex, "target.xml", cchAnnex))
                    rc = dbgcGdbStubCtxQueryXferReadReply(pThis, offRead, cbRead, (const uint8_t *)pThis->pachTgtXmlDesc,
                                                          pThis->cbTgtXmlDesc);
                else
                    rc = dbgcGdbStubCtxReplySendErr(pThis, 0);
            }
            else
                rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
        }
        else
            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
    }
    else
        rc = dbgcGdbStubCtxReplySend(pThis, NULL, 0); /* Not supported. */

    return rc;
}


/**
 * Processes the 'Rcmd' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryRcmd(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    /* Skip the , following the qRcmd start. */
    if (   cbArgs < 1
        || pbArgs[0] != ',')
        return VERR_NET_PROTOCOL_ERROR;

    cbArgs--;
    pbArgs++;

    /* Decode the command. */
    /** @todo Make this dynamic. */
    char szCmd[_4K];
    RT_ZERO(szCmd);

    if (cbArgs / 2 >= sizeof(szCmd))
        return VERR_NET_PROTOCOL_ERROR;

    size_t cbDecoded = 0;
    int rc = RTStrConvertHexBytesEx((const char *)pbArgs, &szCmd[0], sizeof(szCmd), 0 /*fFlags*/,
                                    NULL /* ppszNext */, &cbDecoded);
    if (rc == VWRN_TRAILING_CHARS)
        rc = VINF_SUCCESS;
    if (RT_SUCCESS(rc))
    {
        szCmd[cbDecoded] = '\0'; /* Ensure zero termination. */

        pThis->fOutput = false;
        rc = dbgcEvalCommand(&pThis->Dbgc, &szCmd[0], cbDecoded - 1, false /*fNoExecute*/);
        dbgcGdbStubCtxReplySendOk(pThis);
        if (   rc != VERR_DBGC_QUIT
            && rc != VWRN_DBGC_CMD_PENDING)
            rc = VINF_SUCCESS; /* ignore other statuses */
    }

    return rc;
}


/**
 * Worker for both 'qfThreadInfo' and 'qsThreadInfo'.
 *
 * @returns VBox status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxPktProcessQueryThreadInfoWorker(PGDBSTUBCTX pThis)
{
    int rc = dbgcGdbStubCtxReplySendBegin(pThis);
    if (RT_SUCCESS(rc))
    {
        uint8_t bReplyStart = { 'm' };
        rc = dbgcGdbStubCtxReplySendData(pThis, &bReplyStart, sizeof(bReplyStart));
        if (RT_SUCCESS(rc))
        {
            char achReply[32];
            ssize_t cchStr = RTStrPrintf(&achReply[0], sizeof(achReply), "%02x", pThis->idCpuNextThrdInfoQuery + 1);
            if (cchStr <= 0)
                rc = VERR_BUFFER_OVERFLOW;

            if (RT_SUCCESS(rc))
                rc = dbgcGdbStubCtxReplySendData(pThis, &achReply[0], cchStr);
            if (RT_SUCCESS(rc))
                rc = dbgcGdbStubCtxReplySendEnd(pThis);
            pThis->idCpuNextThrdInfoQuery++;
        }
    }

    return rc;
}


/**
 * Processes the 'fThreadInfo' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryThreadInfoStart(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    RT_NOREF(pbArgs, cbArgs);

    pThis->idCpuNextThrdInfoQuery = 0;
    pThis->fInThrdInfoQuery = true;
    return dbgcGdbStubCtxPktProcessQueryThreadInfoWorker(pThis);
}


/**
 * Processes the 'fThreadInfo' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryThreadInfoCont(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    RT_NOREF(pbArgs, cbArgs);

    /* If we are in a thread info query we just send the end of list specifier (all thread IDs where sent previously already). */
    if (!pThis->fInThrdInfoQuery)
        return dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);

    VMCPUID cCpus = DBGFR3CpuGetCount(pThis->Dbgc.pUVM);
    if (pThis->idCpuNextThrdInfoQuery == cCpus)
    {
        pThis->fInThrdInfoQuery = false;
        uint8_t bEoL = 'l';
        return dbgcGdbStubCtxReplySend(pThis, &bEoL, sizeof(bEoL));
    }

    return dbgcGdbStubCtxPktProcessQueryThreadInfoWorker(pThis);
}


/**
 * Processes the 'ThreadExtraInfo' query.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessQueryThreadExtraInfo(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    /* Skip the , following the qThreadExtraInfo start. */
    if (   cbArgs < 1
        || pbArgs[0] != ',')
        return VERR_NET_PROTOCOL_ERROR;

    /*cbArgs--;*/ /* Not used */
    pbArgs++;

    /* We know there is an # character denoting the end so the following must return with VWRN_TRAILING_CHARS. */
    VMCPUID idCpu;
    int rc = RTStrToUInt32Ex((const char *)pbArgs, NULL /*ppszNext*/, 16, &idCpu);
    if (   rc == VWRN_TRAILING_CHARS
        && idCpu > 0)
    {
        idCpu--;

        VMCPUID cCpus = DBGFR3CpuGetCount(pThis->Dbgc.pUVM);
        if (idCpu < cCpus)
        {
            const char *pszCpuState = DBGFR3CpuGetState(pThis->Dbgc.pUVM, idCpu);
            size_t cchCpuState = strlen(pszCpuState);

            if (!pszCpuState)
                pszCpuState = "DBGFR3CpuGetState() -> NULL";

            rc = dbgcGdbStubCtxReplySendBegin(pThis);
            if (RT_SUCCESS(rc))
            {
                /* Convert the characters to hex. */
                const char *pachCur = pszCpuState;

                while (   cchCpuState
                       && RT_SUCCESS(rc))
                {
                    uint8_t achHex[512 + 1];
                    size_t cbThisSend = RT_MIN((sizeof(achHex) - 1) / 2, cchCpuState); /* Each character needs two bytes. */

                    rc = dbgcGdbStubCtxEncodeBinaryAsHex(&achHex[0], cbThisSend * 2 + 1, pachCur, cbThisSend);
                    if (RT_SUCCESS(rc))
                        rc = dbgcGdbStubCtxReplySendData(pThis, &achHex[0], cbThisSend * 2);

                    pachCur     += cbThisSend;
                    cchCpuState -= cbThisSend;
                }

                dbgcGdbStubCtxReplySendEnd(pThis);
            }
        }
        else
            rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
    }
    else if (   RT_SUCCESS(rc)
             || !idCpu)
        rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);

    return rc;
}


/**
 * List of supported query packets.
 */
static const GDBSTUBQPKTPROC g_aQPktProcs[] =
{
#define GDBSTUBQPKTPROC_INIT(a_Name, a_pfnProc) { a_Name, sizeof(a_Name) - 1, a_pfnProc }
    GDBSTUBQPKTPROC_INIT("C",                  dbgcGdbStubCtxPktProcessQueryThreadId),
    GDBSTUBQPKTPROC_INIT("Attached",           dbgcGdbStubCtxPktProcessQueryAttached),
    GDBSTUBQPKTPROC_INIT("TStatus",            dbgcGdbStubCtxPktProcessQueryTStatus),
    GDBSTUBQPKTPROC_INIT("Supported",          dbgcGdbStubCtxPktProcessQuerySupported),
    GDBSTUBQPKTPROC_INIT("Xfer:features:read", dbgcGdbStubCtxPktProcessQueryXferFeatRead),
    GDBSTUBQPKTPROC_INIT("Rcmd",               dbgcGdbStubCtxPktProcessQueryRcmd),
    GDBSTUBQPKTPROC_INIT("fThreadInfo",        dbgcGdbStubCtxPktProcessQueryThreadInfoStart),
    GDBSTUBQPKTPROC_INIT("sThreadInfo",        dbgcGdbStubCtxPktProcessQueryThreadInfoCont),
    GDBSTUBQPKTPROC_INIT("ThreadExtraInfo",    dbgcGdbStubCtxPktProcessQueryThreadExtraInfo),
#undef GDBSTUBQPKTPROC_INIT
};


/**
 * Processes a 'q' packet, sending the appropriate reply.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbQuery             The query packet data (without the 'q').
 * @param   cbQuery             Size of the remaining query packet in bytes.
 */
static int dbgcGdbStubCtxPktProcessQuery(PGDBSTUBCTX pThis, const uint8_t *pbQuery, size_t cbQuery)
{
    /* Search the query and execute the processor or return an empty reply if not supported. */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aQPktProcs); i++)
    {
        size_t cbCmp = g_aQPktProcs[i].cchName < cbQuery ? g_aQPktProcs[i].cchName : cbQuery;

        if (!memcmp(pbQuery, g_aQPktProcs[i].pszName, cbCmp))
            return g_aQPktProcs[i].pfnProc(pThis, pbQuery + cbCmp, cbQuery - cbCmp);
    }

    return dbgcGdbStubCtxReplySend(pThis, NULL, 0);
}


/**
 * Processes a 'vCont[;action[:thread-id]]' packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbArgs              Pointer to the start of the arguments in the packet.
 * @param   cbArgs              Size of arguments in bytes.
 */
static DECLCALLBACK(int) dbgcGdbStubCtxPktProcessVCont(PGDBSTUBCTX pThis, const uint8_t *pbArgs, size_t cbArgs)
{
    int rc = VINF_SUCCESS;

    /* Skip the ; following the identifier. */
    if (   cbArgs < 2
        || pbArgs[0] != ';')
        return dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);

    pbArgs++;
    /*cbArgs--;*/ /* Not used */

    /** @todo For now we don't care about multiple threads and ignore thread IDs and multiple actions. */
    switch (pbArgs[0])
    {
        case 'c':
        {
            if (DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
                DBGFR3Resume(pThis->Dbgc.pUVM, VMCPUID_ALL);
            break;
        }
        case 's':
        {
            PDBGFADDRESS pStackPop  = NULL;
            RTGCPTR      cbStackPop = 0;
            rc = DBGFR3StepEx(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGF_STEP_F_INTO, NULL,
                              pStackPop, cbStackPop, 1 /*cMaxSteps*/);
            if (RT_FAILURE(rc))
                dbgcGdbStubCtxReplySendErrSts(pThis, rc);
            break;
        }
        case 't':
        {
            if (!DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
                rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
            /* The reply will be send in the event loop. */
            break;
        }
        default:
            rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
    }

    return rc;
}


/**
 * List of supported 'v<identifier>' packets.
 */
static const GDBSTUBVPKTPROC g_aVPktProcs[] =
{
#define GDBSTUBVPKTPROC_INIT(a_Name, a_pszReply, a_pfnProc) { a_Name, sizeof(a_Name) - 1, a_pszReply, sizeof(a_pszReply) - 1, a_pfnProc }
    GDBSTUBVPKTPROC_INIT("Cont", "vCont;s;c;t", dbgcGdbStubCtxPktProcessVCont)
#undef GDBSTUBVPKTPROC_INIT
};


/**
 * Processes a 'v<identifier>' packet, sending the appropriate reply.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbPktRem            The remaining packet data (without the 'v').
 * @param   cbPktRem            Size of the remaining packet in bytes.
 */
static int dbgcGdbStubCtxPktProcessV(PGDBSTUBCTX pThis, const uint8_t *pbPktRem, size_t cbPktRem)
{
    /* Determine the end of the identifier, delimiters are '?', ';' or end of packet. */
    bool fQuery = false;
    const uint8_t *pbDelim = (const uint8_t *)memchr(pbPktRem, '?', cbPktRem);
    if (!pbDelim)
        pbDelim = (const uint8_t *)memchr(pbPktRem, ';', cbPktRem);
    else
        fQuery = true;

    size_t cchId = 0;
    if (pbDelim) /* Delimiter found, calculate length. */
        cchId = pbDelim - pbPktRem;
    else /* Not found, size goes till end of packet. */
        cchId = cbPktRem;

    /* Search the query and execute the processor or return an empty reply if not supported. */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aVPktProcs); i++)
    {
        PCGDBSTUBVPKTPROC pVProc = &g_aVPktProcs[i];

        if (   pVProc->cchName == cchId
            && !memcmp(pbPktRem, pVProc->pszName, cchId))
        {
            /* Just send the static reply for a query and execute the processor for everything else. */
            if (fQuery)
                return dbgcGdbStubCtxReplySend(pThis, pVProc->pszReplyQ, pVProc->cchReplyQ);

            /* Execute the handler. */
            return pVProc->pfnProc(pThis, pbPktRem + cchId, cbPktRem - cchId);
        }
    }

    return dbgcGdbStubCtxReplySend(pThis, NULL, 0);
}


/**
 * Processes a 'H<op><thread-id>' packet, sending the appropriate reply.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   pbPktRem            The remaining packet data (without the 'H').
 * @param   cbPktRem            Size of the remaining packet in bytes.
 */
static int dbgcGdbStubCtxPktProcessH(PGDBSTUBCTX pThis, const uint8_t *pbPktRem, size_t cbPktRem)
{
    int rc = VINF_SUCCESS;

    if (*pbPktRem == 'g')
    {
        /*Unused: cbPktRem--;*/ RT_NOREF(cbPktRem);
        pbPktRem++;

        /* We know there is an # character denoting the end so the following must return with VWRN_TRAILING_CHARS. */
        VMCPUID idCpu;
        rc = RTStrToUInt32Ex((const char *)pbPktRem, NULL /*ppszNext*/, 16, &idCpu);
        if (   rc == VWRN_TRAILING_CHARS
            && idCpu > 0)
        {
            idCpu--;

            VMCPUID cCpus = DBGFR3CpuGetCount(pThis->Dbgc.pUVM);
            if (idCpu < cCpus)
            {
                pThis->Dbgc.idCpu = idCpu;
                rc = dbgcGdbStubCtxReplySendOk(pThis);
            }
            else
                rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
        }
        else
            rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
    }
    else /* Do not support the 'c' operation for now (will be handled through vCont later on anyway). */
        rc = dbgcGdbStubCtxReplySend(pThis, NULL, 0);

    return rc;
}


/**
 * Processes a completely received packet.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxPktProcess(PGDBSTUBCTX pThis)
{
    int rc = VINF_SUCCESS;

    if (pThis->cbPkt >= 1)
    {
        switch (pThis->pbPktBuf[1])
        {
            case '!': /* Enabled extended mode. */
            {
                pThis->fExtendedMode = true;
                rc = dbgcGdbStubCtxReplySendOk(pThis);
                break;
            }
            case '?':
            {
                /* Return signal state. */
                rc = dbgcGdbStubCtxReplySendSigTrap(pThis);
                break;
            }
            case 's': /* Single step, response will be sent in the event loop. */
            {
                PDBGFADDRESS pStackPop  = NULL;
                RTGCPTR      cbStackPop = 0;
                rc = DBGFR3StepEx(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, DBGF_STEP_F_INTO, NULL,
                                  pStackPop, cbStackPop, 1 /*cMaxSteps*/);
                if (RT_FAILURE(rc))
                    dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'c': /* Continue, no response */
            {
                if (DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
                    DBGFR3Resume(pThis->Dbgc.pUVM, VMCPUID_ALL);
                break;
            }
            case 'H':
            {
                rc = dbgcGdbStubCtxPktProcessH(pThis, &pThis->pbPktBuf[2], pThis->cbPkt - 1);
                break;
            }
            case 'T':
            {
                rc = dbgcGdbStubCtxReplySendOk(pThis);
                break;
            }
            case 'g': /* Read general registers. */
            {
                uint32_t idxRegMax = 0;
                size_t cbRegs = 0;
                for (;;)
                {
                    const GDBREGDESC *pReg = &pThis->paRegs[idxRegMax++];
                    cbRegs += pReg->cBits / 8;
                    if (pReg->enmReg == DBGFREG_SS) /* Up to this seems to belong to the general register set. */
                        break;
                }

                size_t cbReplyPkt = cbRegs * 2 + 1; /* One byte needs two characters. */
                rc = dbgcGdbStubCtxEnsurePktBufSpace(pThis, cbReplyPkt);
                if (RT_SUCCESS(rc))
                {
                    size_t cbLeft = cbReplyPkt;
                    uint8_t *pbReply = pThis->pbPktBuf;

                    for (uint32_t i = 0; i < idxRegMax && RT_SUCCESS(rc); i++)
                    {
                        const GDBREGDESC *pReg = &pThis->paRegs[i];
                        size_t cbReg = pReg->cBits / 8;
                        union
                        {
                            uint32_t u32;
                            uint64_t u64;
                            uint8_t  au8[8];
                        } RegVal;

                        if (pReg->cBits == 32)
                            rc = DBGFR3RegCpuQueryU32(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, pReg->enmReg, &RegVal.u32);
                        else
                            rc = DBGFR3RegCpuQueryU64(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, pReg->enmReg, &RegVal.u64);

                        if (RT_SUCCESS(rc))
                            rc = dbgcGdbStubCtxEncodeBinaryAsHex(pbReply, cbLeft, &RegVal.au8[0], cbReg);

                        pbReply += cbReg * 2;
                        cbLeft  -= cbReg * 2;
                    }

                    if (RT_SUCCESS(rc))
                        rc = dbgcGdbStubCtxReplySend(pThis, pThis->pbPktBuf, cbReplyPkt);
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                }

                break;
            }
            case 'm': /* Read memory. */
            {
                uint64_t GdbTgtAddr = 0;
                const uint8_t *pbPktSep = NULL;

                rc = dbgcGdbStubCtxParseHexStringAsInteger(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &GdbTgtAddr,
                                                           ',', &pbPktSep);
                if (RT_SUCCESS(rc))
                {
                    size_t cbProcessed = pbPktSep - &pThis->pbPktBuf[2];
                    uint64_t cbRead = 0;
                    rc = dbgcGdbStubCtxParseHexStringAsInteger(pbPktSep + 1, pThis->cbPkt - 1 - cbProcessed - 1, &cbRead, GDBSTUB_PKT_END, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        size_t cbReplyPkt = cbRead * 2 + 1; /* One byte needs two characters. */

                        rc = dbgcGdbStubCtxEnsurePktBufSpace(pThis, cbReplyPkt);
                        if (RT_SUCCESS(rc))
                        {
                            uint8_t *pbPktBuf = pThis->pbPktBuf;
                            size_t cbPktBufLeft = cbReplyPkt;
                            DBGFADDRESS AddrRead;

                            DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrRead, GdbTgtAddr);

                            while (   cbRead
                                   && RT_SUCCESS(rc))
                            {
                                uint8_t abTmp[_4K];
                                size_t cbThisRead = RT_MIN(cbRead, sizeof(abTmp));

                                rc = DBGFR3MemRead(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &AddrRead, &abTmp[0], cbThisRead);
                                if (RT_FAILURE(rc))
                                    break;

                                rc = dbgcGdbStubCtxEncodeBinaryAsHex(pbPktBuf, cbPktBufLeft, &abTmp[0], cbThisRead);
                                if (RT_FAILURE(rc))
                                    break;

                                DBGFR3AddrAdd(&AddrRead, cbThisRead);
                                cbRead       -= cbThisRead;
                                pbPktBuf     += cbThisRead;
                                cbPktBufLeft -= cbThisRead;
                            }

                            if (RT_SUCCESS(rc))
                                rc = dbgcGdbStubCtxReplySend(pThis, pThis->pbPktBuf, cbReplyPkt);
                            else
                                rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                        }
                        else
                            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'M': /* Write memory. */
            {
                uint64_t GdbTgtAddr = 0;
                const uint8_t *pbPktSep = NULL;

                rc = dbgcGdbStubCtxParseHexStringAsInteger(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &GdbTgtAddr,
                                                           ',', &pbPktSep);
                if (RT_SUCCESS(rc))
                {
                    size_t cbProcessed = pbPktSep - &pThis->pbPktBuf[2];
                    uint64_t cbWrite = 0;
                    rc = dbgcGdbStubCtxParseHexStringAsInteger(pbPktSep + 1, pThis->cbPkt - 1 - cbProcessed - 1, &cbWrite, ':', &pbPktSep);
                    if (RT_SUCCESS(rc))
                    {
                        cbProcessed = pbPktSep - &pThis->pbPktBuf[2];
                        const uint8_t *pbDataCur = pbPktSep + 1;
                        size_t cbDataLeft = pThis->cbPkt - 1 - cbProcessed - 1 - 1;
                        DBGFADDRESS AddrWrite;

                        DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &AddrWrite, GdbTgtAddr);

                        while (   cbWrite
                               && RT_SUCCESS(rc))
                        {
                            uint8_t abTmp[_4K];
                            size_t cbThisWrite = RT_MIN(cbWrite, sizeof(abTmp));
                            size_t cbDecoded = 0;

                            rc = dbgcGdbStubCtxParseHexStringAsByteBuf(pbDataCur, cbDataLeft, &abTmp[0], cbThisWrite, &cbDecoded);
                            if (!rc)
                                rc = DBGFR3MemWrite(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &AddrWrite, &abTmp[0], cbThisWrite);

                            DBGFR3AddrAdd(&AddrWrite, cbThisWrite);
                            cbWrite    -= cbThisWrite;
                            pbDataCur  += cbDecoded;
                            cbDataLeft -= cbDecoded;
                        }

                        if (RT_SUCCESS(rc))
                            rc = dbgcGdbStubCtxReplySendOk(pThis);
                        else
                            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'p': /* Read a single register */
            {
                uint64_t uReg = 0;
                rc = dbgcGdbStubCtxParseHexStringAsInteger(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &uReg,
                                                           GDBSTUB_PKT_END, NULL);
                if (RT_SUCCESS(rc))
                {
                    DBGFREGVAL RegVal;
                    DBGFREGVALTYPE enmType;
                    const GDBREGDESC *pReg = dbgcGdbStubRegGet(pThis, uReg);
                    if (RT_LIKELY(pReg))
                    {
                        rc = DBGFR3RegNmQuery(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, pReg->pszName, &RegVal, &enmType);
                        if (RT_SUCCESS(rc))
                        {
                            size_t cbReg = pReg->cBits / 8;
                            size_t cbReplyPkt = cbReg * 2 + 1; /* One byte needs two characters. */

                            /* Encode data and send. */
                            rc = dbgcGdbStubCtxEnsurePktBufSpace(pThis, cbReplyPkt);
                            if (RT_SUCCESS(rc))
                            {
                                rc = dbgcGdbStubCtxEncodeBinaryAsHex(pThis->pbPktBuf, pThis->cbPktBufMax, &RegVal.au8[0], cbReg);
                                if (RT_SUCCESS(rc))
                                    rc = dbgcGdbStubCtxReplySend(pThis, pThis->pbPktBuf, cbReplyPkt);
                                else
                                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                            }
                            else
                                rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                        }
                        else
                            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'P': /* Write a single register */
            {
                uint64_t uReg = 0;
                const uint8_t *pbPktSep = NULL;
                rc = dbgcGdbStubCtxParseHexStringAsInteger(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &uReg,
                                                       '=', &pbPktSep);
                if (RT_SUCCESS(rc))
                {
                    const GDBREGDESC *pReg = dbgcGdbStubRegGet(pThis, uReg);

                    if (pReg)
                    {
                        DBGFREGVAL RegVal;
                        DBGFREGVALTYPE enmValType = pReg->cBits == 64 ? DBGFREGVALTYPE_U64 : DBGFREGVALTYPE_U32;
                        size_t cbProcessed = pbPktSep - &pThis->pbPktBuf[2];
                        rc = dbgcGdbStubCtxParseHexStringAsByteBuf(pbPktSep + 1, pThis->cbPkt - 1 - cbProcessed - 1, &RegVal.au8[0], pReg->cBits / 8, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            rc = DBGFR3RegNmSet(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, pReg->pszName, &RegVal, enmValType);
                            if (RT_SUCCESS(rc))
                                rc = dbgcGdbStubCtxReplySendOk(pThis);
                            else
                                rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                        }
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NET_PROTOCOL_ERROR);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'Z': /* Insert a breakpoint/watchpoint. */
            {
                GDBSTUBTPTYPE enmTpType = GDBSTUBTPTYPE_INVALID;
                uint64_t      GdbTgtTpAddr = 0;
                uint64_t      uKind = 0;

                rc = dbgcGdbStubCtxParseTpPktArgs(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &enmTpType, &GdbTgtTpAddr, &uKind);
                if (RT_SUCCESS(rc))
                {
                    uint32_t iBp = 0;
                    DBGFADDRESS BpAddr;
                    DBGFR3AddrFromFlat(pThis->Dbgc.pUVM, &BpAddr, GdbTgtTpAddr);

                    switch (enmTpType)
                    {
                        case GDBSTUBTPTYPE_EXEC_SW:
                        {
                            rc = DBGFR3BpSetInt3(pThis->Dbgc.pUVM, pThis->Dbgc.idCpu, &BpAddr,
                                                 1 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/, &iBp);
                            break;
                        }
                        case GDBSTUBTPTYPE_EXEC_HW:
                        {
                            rc = DBGFR3BpSetReg(pThis->Dbgc.pUVM, &BpAddr,
                                                1 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/,
                                                X86_DR7_RW_EO, 1 /*cb*/, &iBp);
                            break;
                        }
                        case GDBSTUBTPTYPE_MEM_ACCESS:
                        case GDBSTUBTPTYPE_MEM_READ:
                        {
                            rc = DBGFR3BpSetReg(pThis->Dbgc.pUVM, &BpAddr,
                                                1 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/,
                                                X86_DR7_RW_RW, uKind /*cb*/, &iBp);
                            break;
                        }
                        case GDBSTUBTPTYPE_MEM_WRITE:
                        {
                            rc = DBGFR3BpSetReg(pThis->Dbgc.pUVM, &BpAddr,
                                                1 /*iHitTrigger*/, UINT64_MAX /*iHitDisable*/,
                                                X86_DR7_RW_WO, uKind /*cb*/, &iBp);
                            break;
                        }
                        default:
                            AssertMsgFailed(("Invalid trace point type %d\n", enmTpType));
                    }

                    if (RT_SUCCESS(rc))
                    {
                        rc = dbgcBpAdd(&pThis->Dbgc, iBp, NULL /*pszCmd*/);
                        if (RT_SUCCESS(rc))
                        {
                            rc = dbgcGdbStubTpRegister(pThis, enmTpType, GdbTgtTpAddr, uKind, iBp);
                            if (RT_SUCCESS(rc))
                                rc = dbgcGdbStubCtxReplySendOk(pThis);
                            else
                                dbgcBpDelete(&pThis->Dbgc, iBp);
                        }

                        if (RT_FAILURE(rc))
                        {
                            DBGFR3BpClear(pThis->Dbgc.pUVM, iBp);
                            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                        }
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'z': /* Remove a breakpoint/watchpoint. */
            {
                GDBSTUBTPTYPE enmTpType = GDBSTUBTPTYPE_INVALID;
                uint64_t      GdbTgtTpAddr = 0;
                uint64_t      uKind = 0;

                rc = dbgcGdbStubCtxParseTpPktArgs(&pThis->pbPktBuf[2], pThis->cbPkt - 1, &enmTpType, &GdbTgtTpAddr, &uKind);
                if (RT_SUCCESS(rc))
                {
                    PGDBSTUBTP pTp = dbgcGdbStubTpFind(pThis, enmTpType, GdbTgtTpAddr, uKind);
                    if (pTp)
                    {
                        int rc2 = DBGFR3BpClear(pThis->Dbgc.pUVM, pTp->iBp);
                        if (RT_SUCCESS(rc2) || rc2 == VERR_DBGF_BP_NOT_FOUND)
                            dbgcBpDelete(&pThis->Dbgc, pTp->iBp);

                        if (RT_SUCCESS(rc2))
                        {
                            dbgcGdbStubTpDeregister(pTp);
                            rc = dbgcGdbStubCtxReplySendOk(pThis);
                        }
                        else
                            rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                    }
                    else
                        rc = dbgcGdbStubCtxReplySendErrSts(pThis, VERR_NOT_FOUND);
                }
                else
                    rc = dbgcGdbStubCtxReplySendErrSts(pThis, rc);
                break;
            }
            case 'q': /* Query packet */
            {
                rc = dbgcGdbStubCtxPktProcessQuery(pThis, &pThis->pbPktBuf[2], pThis->cbPkt - 1);
                break;
            }
            case 'v': /* Multiletter identifier (verbose?) */
            {
                rc = dbgcGdbStubCtxPktProcessV(pThis, &pThis->pbPktBuf[2], pThis->cbPkt - 1);
                break;
            }
            case 'R': /* Restart target. */
            {
                rc = dbgcGdbStubCtxReplySend(pThis, NULL, 0);
                break;
            }
            case 'k': /* Kill target. */
            {
                /* This is what the 'harakiri' command is doing. */
                for (;;)
                    exit(126);
                break;
            }
            case 'D': /* Detach */
            {
                rc = dbgcGdbStubCtxReplySendOk(pThis);
                if (RT_SUCCESS(rc))
                    rc = VERR_DBGC_QUIT;
                break;
            }
            default:
                /* Not supported, send empty reply. */
                rc = dbgcGdbStubCtxReplySend(pThis, NULL, 0);
        }
    }

    return rc;
}


/**
 * Resets the packet buffer.
 *
 * @param   pThis               The GDB stub context.
 */
static void dbgcGdbStubCtxPktBufReset(PGDBSTUBCTX pThis)
{
    pThis->offPktBuf        = 0;
    pThis->cbPkt            = 0;
    pThis->cbChksumRecvLeft = 2;
}


/**
 * Resets the given GDB stub context to the initial state.
 *
 * @param   pThis               The GDB stub context.
 */
static void dbgcGdbStubCtxReset(PGDBSTUBCTX pThis)
{
    pThis->enmState = GDBSTUBRECVSTATE_PACKET_WAIT_FOR_START;
    dbgcGdbStubCtxPktBufReset(pThis);
}


/**
 * Searches for the start character in the current data buffer.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   cbData              Number of new bytes in the packet buffer.
 * @param   pcbProcessed        Where to store the amount of bytes processed.
 */
static int dbgcGdbStubCtxPktBufSearchStart(PGDBSTUBCTX pThis, size_t cbData, size_t *pcbProcessed)
{
    int rc = VINF_SUCCESS;
    const uint8_t *pbStart = (const uint8_t *)memchr(pThis->pbPktBuf, GDBSTUB_PKT_START, cbData);
    if (pbStart)
    {
        /* Found the start character, align the start to the beginning of the packet buffer and advance the state machine. */
        memmove(pThis->pbPktBuf, pbStart, cbData - (pbStart - pThis->pbPktBuf));
        pThis->enmState = GDBSTUBRECVSTATE_PACKET_RECEIVE_BODY;
        *pcbProcessed = (uintptr_t)(pbStart - pThis->pbPktBuf);
        pThis->offPktBuf = 0;
    }
    else
    {
        /* Check for out of band characters. */
        if (memchr(pThis->pbPktBuf, GDBSTUB_OOB_INTERRUPT, cbData) != NULL)
        {
            /* Stop target and send packet to indicate the target has stopped. */
            if (!DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
                rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
            /* The reply will be send in the event loop. */
        }

        /* Not found, ignore the received data and reset the packet buffer. */
        dbgcGdbStubCtxPktBufReset(pThis);
        *pcbProcessed = cbData;
    }

    return rc;
}


/**
 * Searches for the end character in the current data buffer.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   cbData              Number of new bytes in the packet buffer.
 * @param   pcbProcessed        Where to store the amount of bytes processed.
 */
static int dbgcGdbStubCtxPktBufSearchEnd(PGDBSTUBCTX pThis, size_t cbData, size_t *pcbProcessed)
{
    const uint8_t *pbEnd = (const uint8_t *)memchr(&pThis->pbPktBuf[pThis->offPktBuf], GDBSTUB_PKT_END, cbData);
    if (pbEnd)
    {
        /* Found the end character, next comes the checksum. */
        pThis->enmState = GDBSTUBRECVSTATE_PACKET_RECEIVE_CHECKSUM;

        *pcbProcessed     = (uintptr_t)(pbEnd - &pThis->pbPktBuf[pThis->offPktBuf]) + 1;
        pThis->offPktBuf += *pcbProcessed;
        pThis->cbPkt      = pThis->offPktBuf - 1; /* Don't account for the start and end character. */
    }
    else
    {
        /* Not found, still in the middle of a packet. */
        /** @todo Look for out of band characters. */
        *pcbProcessed    = cbData;
        pThis->offPktBuf += cbData;
    }

    return VINF_SUCCESS;
}


/**
 * Processes the checksum.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   cbData              Number of new bytes in the packet buffer.
 * @param   pcbProcessed        Where to store the amount of bytes processed.
 */
static int dbgcGdbStubCtxPktBufProcessChksum(PGDBSTUBCTX pThis, size_t cbData, size_t *pcbProcessed)
{
    int rc = VINF_SUCCESS;
    size_t cbChksumProcessed = (cbData < pThis->cbChksumRecvLeft) ? cbData : pThis->cbChksumRecvLeft;

    pThis->cbChksumRecvLeft -= cbChksumProcessed;
    if (!pThis->cbChksumRecvLeft)
    {
        /* Verify checksum of the whole packet. */
        uint8_t uChkSum =   dbgcGdbStubCtxChrToHex(pThis->pbPktBuf[pThis->offPktBuf]) << 4
                          | dbgcGdbStubCtxChrToHex(pThis->pbPktBuf[pThis->offPktBuf + 1]);

        uint8_t uSum = 0;
        for (size_t i = 1; i < pThis->cbPkt; i++)
            uSum += pThis->pbPktBuf[i];

        if (uSum == uChkSum)
        {
            /* Checksum matches, send acknowledge and continue processing the complete payload. */
            char chAck = '+';
            rc = dbgcGdbStubCtxWrite(pThis, &chAck, sizeof(chAck));
            if (RT_SUCCESS(rc))
                rc = dbgcGdbStubCtxPktProcess(pThis);
        }
        else
        {
            /* Send NACK and reset for the next packet. */
            char chAck = '-';
            rc = dbgcGdbStubCtxWrite(pThis, &chAck, sizeof(chAck));
        }

        dbgcGdbStubCtxReset(pThis);
    }

    *pcbProcessed += cbChksumProcessed;
    return rc;
}


/**
 * Process read data in the packet buffer based on the current state.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 * @param   cbData              Number of new bytes in the packet buffer.
 */
static int dbgcGdbStubCtxPktBufProcess(PGDBSTUBCTX pThis, size_t cbData)
{
    int rc = VINF_SUCCESS;

    while (   cbData
           && RT_SUCCESS(rc))
    {
        size_t cbProcessed = 0;

        switch (pThis->enmState)
        {
            case GDBSTUBRECVSTATE_PACKET_WAIT_FOR_START:
            {
                rc = dbgcGdbStubCtxPktBufSearchStart(pThis, cbData, &cbProcessed);
                break;
            }
            case GDBSTUBRECVSTATE_PACKET_RECEIVE_BODY:
            {
                rc = dbgcGdbStubCtxPktBufSearchEnd(pThis, cbData, &cbProcessed);
                break;
            }
            case GDBSTUBRECVSTATE_PACKET_RECEIVE_CHECKSUM:
            {
                rc = dbgcGdbStubCtxPktBufProcessChksum(pThis, cbData, &cbProcessed);
                break;
            }
            default:
                /* Should never happen. */
                rc = VERR_INTERNAL_ERROR;
        }

        cbData -= cbProcessed;
    }

    return rc;
}


/**
 * Receive data and processes complete packets.
 *
 * @returns Status code.
 * @param   pThis               The GDB stub context.
 */
static int dbgcGdbStubCtxRecv(PGDBSTUBCTX pThis)
{
    /*
     * Read in 32 bytes chunks for now (need some peek API to get the amount of bytes actually available
     * to make it a bit more optimized).
     */
    int rc = dbgcGdbStubCtxEnsurePktBufSpace(pThis, 32);
    if (RT_SUCCESS(rc))
    {
        size_t cbThisRead = 32;
        rc = pThis->Dbgc.pIo->pfnRead(pThis->Dbgc.pIo, &pThis->pbPktBuf[pThis->offPktBuf], cbThisRead, &cbThisRead);
        if (RT_SUCCESS(rc))
            rc = dbgcGdbStubCtxPktBufProcess(pThis, cbThisRead);
    }

    return rc;
}


/**
 * Processes debugger events.
 *
 * @returns VBox status code.
 * @param   pThis   The GDB stub context data.
 * @param   pEvent  Pointer to event data.
 */
static int dbgcGdbStubCtxProcessEvent(PGDBSTUBCTX pThis, PCDBGFEVENT pEvent)
{
    /*
     * Process the event.
     */
    PDBGC pDbgc = &pThis->Dbgc;
    pThis->Dbgc.pszScratch = &pThis->Dbgc.achInput[0];
    pThis->Dbgc.iArg       = 0;
    int rc = VINF_SUCCESS;
    switch (pEvent->enmType)
    {
        /*
         * The first part is events we have initiated with commands.
         */
        case DBGFEVENT_HALT_DONE:
        {
            rc = dbgcGdbStubCtxReplySendSigTrap(pThis);
            break;
        }


        /*
         * The second part is events which can occur at any time.
         */
        case DBGFEVENT_FATAL_ERROR:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbf event: Fatal error! (%s)\n",
                                         dbgcGetEventCtx(pEvent->enmCtx));
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_BREAKPOINT:
        case DBGFEVENT_BREAKPOINT_IO:
        case DBGFEVENT_BREAKPOINT_MMIO:
        case DBGFEVENT_BREAKPOINT_HYPER:
        {
            rc = dbgcBpExec(pDbgc, pEvent->u.Bp.hBp);
            switch (rc)
            {
                case VERR_DBGC_BP_NOT_FOUND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Unknown breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_DBGC_BP_NO_COMMAND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_BUFFER_OVERFLOW:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! Command too long to execute! (%s)\n",
                                                 pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                default:
                    break;
            }
            if (RT_SUCCESS(rc) && DBGFR3IsHalted(pDbgc->pUVM, VMCPUID_ALL))
            {
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");

                /* Set the resume flag to ignore the breakpoint when resuming execution. */
                if (   RT_SUCCESS(rc)
                    && pEvent->enmType == DBGFEVENT_BREAKPOINT)
                    rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r eflags.rf = 1");
            }

            if (RT_FAILURE(rc))
                pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "Executing the breakpoint %u (%s) failed with %Rrc!\n",
                                        pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx), rc);

            rc = dbgcGdbStubCtxReplySendSigTrap(pThis);
            break;
        }

        case DBGFEVENT_STEPPED:
        case DBGFEVENT_STEPPED_HYPER:
        {
            rc = dbgcGdbStubCtxReplySendSigTrap(pThis);
            break;
        }

        case DBGFEVENT_ASSERTION_HYPER:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\ndbgf event: Hypervisor Assertion! (%s)\n"
                                         "%s"
                                         "%s"
                                         "\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Assert.pszMsg1,
                                         pEvent->u.Assert.pszMsg2);
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_DEV_STOP:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\n"
                                         "dbgf event: DBGFSTOP (%s)\n"
                                         "File:     %s\n"
                                         "Line:     %d\n"
                                         "Function: %s\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Src.pszFile,
                                         pEvent->u.Src.uLine,
                                         pEvent->u.Src.pszFunction);
            if (RT_SUCCESS(rc) && pEvent->u.Src.pszMessage && *pEvent->u.Src.pszMessage)
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                             "Message:  %s\n",
                                             pEvent->u.Src.pszMessage);
            if (RT_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }


        case DBGFEVENT_INVALID_COMMAND:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Invalid command event!\n");
            break;
        }

        case DBGFEVENT_POWERING_OFF:
        {
            pThis->Dbgc.fReady = false;
            pThis->Dbgc.pIo->pfnSetReady(pThis->Dbgc.pIo, false);
            rc = VERR_GENERAL_FAILURE;
            break;
        }

        default:
        {
            /*
             * Probably a generic event. Look it up to find its name.
             */
            PCDBGCSXEVT pEvtDesc = dbgcEventLookup(pEvent->enmType);
            if (pEvtDesc)
            {
                if (pEvtDesc->enmKind == kDbgcSxEventKind_Interrupt)
                {
                    Assert(pEvtDesc->pszDesc);
                    Assert(pEvent->u.Generic.cArgs == 1);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s no %#llx! (%s)\n",
                                                 pEvtDesc->pszDesc, pEvent->u.Generic.auArgs[0], pEvtDesc->pszName);
                }
                else if (pEvtDesc->fFlags & DBGCSXEVT_F_BUGCHECK)
                {
                    Assert(pEvent->u.Generic.cArgs >= 5);
                    char szDetails[512];
                    DBGFR3FormatBugCheck(pDbgc->pUVM, szDetails, sizeof(szDetails), pEvent->u.Generic.auArgs[0],
                                         pEvent->u.Generic.auArgs[1], pEvent->u.Generic.auArgs[2],
                                         pEvent->u.Generic.auArgs[3], pEvent->u.Generic.auArgs[4]);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s %s%s!\n%s", pEvtDesc->pszName,
                                                 pEvtDesc->pszDesc ? "- " : "", pEvtDesc->pszDesc ? pEvtDesc->pszDesc : "",
                                                 szDetails);
                }
                else if (   (pEvtDesc->fFlags & DBGCSXEVT_F_TAKE_ARG)
                         || pEvent->u.Generic.cArgs > 1
                         || (   pEvent->u.Generic.cArgs == 1
                             && pEvent->u.Generic.auArgs[0] != 0))
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s - %s!",
                                                     pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s!", pEvtDesc->pszName);
                    if (RT_SUCCESS(rc))
                    {
                        if (pEvent->u.Generic.cArgs <= 1)
                            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " arg=%#llx\n", pEvent->u.Generic.auArgs[0]);
                        else
                        {
                            for (uint32_t i = 0; i < pEvent->u.Generic.cArgs; i++)
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " args[%u]=%#llx", i, pEvent->u.Generic.auArgs[i]);
                            if (RT_SUCCESS(rc))
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\n");
                        }
                    }
                }
                else
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s - %s!\n",
                                                     pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: %s!\n", pEvtDesc->pszName);
                }
            }
            else
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Unknown event %d!\n", pEvent->enmType);
            break;
        }
    }

    return rc;
}


/**
 * Run the debugger console.
 *
 * @returns VBox status code.
 * @param   pThis   Pointer to the GDB stub context.
 */
static int dbgcGdbStubRun(PGDBSTUBCTX pThis)
{
    /* Select the register set based on the CPU mode. */
    CPUMMODE enmMode   = DBGCCmdHlpGetCpuMode(&pThis->Dbgc.CmdHlp);
    switch (enmMode)
    {
        case CPUMMODE_PROTECTED:
            pThis->paRegs = &g_aGdbRegs32[0];
            pThis->cRegs  = RT_ELEMENTS(g_aGdbRegs32);
            break;
        case CPUMMODE_LONG:
            pThis->paRegs = &g_aGdbRegs64[0];
            pThis->cRegs  = RT_ELEMENTS(g_aGdbRegs64);
            break;
        case CPUMMODE_REAL:
        default:
            return DBGCCmdHlpPrintf(&pThis->Dbgc.CmdHlp, "error: Invalid CPU mode %d.\n", enmMode);
    }

    /*
     * We're ready for commands now.
     */
    pThis->Dbgc.fReady = true;
    pThis->Dbgc.pIo->pfnSetReady(pThis->Dbgc.pIo, true);

    /*
     * Main Debugger Loop.
     *
     * This loop will either block on waiting for input or on waiting on
     * debug events. If we're forwarding the log we cannot wait for long
     * before we must flush the log.
     */
    int rc;
    for (;;)
    {
        rc = VERR_SEM_OUT_OF_TURN;
        if (pThis->Dbgc.pUVM)
            rc = DBGFR3QueryWaitable(pThis->Dbgc.pUVM);

        if (RT_SUCCESS(rc))
        {
            /*
             * Wait for a debug event.
             */
            DBGFEVENT Event;
            rc = DBGFR3EventWait(pThis->Dbgc.pUVM, 32, &Event);
            if (RT_SUCCESS(rc))
            {
                rc = dbgcGdbStubCtxProcessEvent(pThis, &Event);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (rc != VERR_TIMEOUT)
                break;

            /*
             * Check for input.
             */
            if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, 0))
            {
                rc = dbgcGdbStubCtxRecv(pThis);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else if (rc == VERR_SEM_OUT_OF_TURN)
        {
            /*
             * Wait for input.
             */
            if (pThis->Dbgc.pIo->pfnInput(pThis->Dbgc.pIo, 1000))
            {
                rc = dbgcGdbStubCtxRecv(pThis);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else
            break;
    }

    return rc;
}


/**
 * @copydoc DBGC::pfnOutput
 */
static DECLCALLBACK(int) dbgcOutputGdb(void *pvUser, const char *pachChars, size_t cbChars)
{
    PGDBSTUBCTX pThis = (PGDBSTUBCTX)pvUser;

    pThis->fOutput = true;
    int rc = dbgcGdbStubCtxReplySendBegin(pThis);
    if (RT_SUCCESS(rc))
    {
        uint8_t chConOut = 'O';
        rc = dbgcGdbStubCtxReplySendData(pThis, &chConOut, sizeof(chConOut));
        if (RT_SUCCESS(rc))
        {
            /* Convert the characters to hex. */
            const char *pachCur = pachChars;

            while (   cbChars
                   && RT_SUCCESS(rc))
            {
                uint8_t achHex[512 + 1];
                size_t cbThisSend = RT_MIN((sizeof(achHex) - 1) / 2, cbChars); /* Each character needs two bytes. */

                rc = dbgcGdbStubCtxEncodeBinaryAsHex(&achHex[0], cbThisSend * 2 + 1, pachCur, cbThisSend);
                if (RT_SUCCESS(rc))
                    rc = dbgcGdbStubCtxReplySendData(pThis, &achHex[0], cbThisSend * 2);

                pachCur += cbThisSend;
                cbChars -= cbThisSend;
            }
        }

        dbgcGdbStubCtxReplySendEnd(pThis);
    }

    return rc;
}


/**
 * Creates a GDB stub context instance with the given backend.
 *
 * @returns VBox status code.
 * @param   ppGdbStubCtx            Where to store the pointer to the GDB stub context instance on success.
 * @param   pIo                     Pointer to the I/O callback table.
 * @param   fFlags                  Flags controlling the behavior.
 */
static int dbgcGdbStubCtxCreate(PPGDBSTUBCTX ppGdbStubCtx, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pIo, VERR_INVALID_POINTER);
    AssertMsgReturn(!fFlags, ("%#x", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Allocate and initialize.
     */
    PGDBSTUBCTX pThis = (PGDBSTUBCTX)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    dbgcInitCmdHlp(&pThis->Dbgc);
    /*
     * This is compied from the native debug console (will be used for monitor commands)
     * in DBGCConsole.cpp. Try to keep both functions in sync.
     */
    pThis->Dbgc.pIo              = pIo;
    pThis->Dbgc.pfnOutput        = dbgcOutputGdb;
    pThis->Dbgc.pvOutputUser     = pThis;
    pThis->Dbgc.pVM              = NULL;
    pThis->Dbgc.pUVM             = NULL;
    pThis->Dbgc.idCpu            = 0;
    pThis->Dbgc.hDbgAs           = DBGF_AS_GLOBAL;
    pThis->Dbgc.pszEmulation     = "CodeView/WinDbg";
    pThis->Dbgc.paEmulationCmds  = &g_aCmdsCodeView[0];
    pThis->Dbgc.cEmulationCmds   = g_cCmdsCodeView;
    pThis->Dbgc.paEmulationFuncs = &g_aFuncsCodeView[0];
    pThis->Dbgc.cEmulationFuncs  = g_cFuncsCodeView;
    //pThis->Dbgc.fLog             = false;
    pThis->Dbgc.fRegTerse        = true;
    pThis->Dbgc.fStepTraceRegs   = true;
    //pThis->Dbgc.cPagingHierarchyDumps = 0;
    //pThis->Dbgc.DisasmPos        = {0};
    //pThis->Dbgc.SourcePos        = {0};
    //pThis->Dbgc.DumpPos          = {0};
    pThis->Dbgc.pLastPos          = &pThis->Dbgc.DisasmPos;
    //pThis->Dbgc.cbDumpElement    = 0;
    //pThis->Dbgc.cVars            = 0;
    //pThis->Dbgc.paVars           = NULL;
    //pThis->Dbgc.pPlugInHead      = NULL;
    //pThis->Dbgc.pFirstBp         = NULL;
    //pThis->Dbgc.abSearch         = {0};
    //pThis->Dbgc.cbSearch         = 0;
    pThis->Dbgc.cbSearchUnit       = 1;
    pThis->Dbgc.cMaxSearchHits     = 1;
    //pThis->Dbgc.SearchAddr       = {0};
    //pThis->Dbgc.cbSearchRange    = 0;

    //pThis->Dbgc.uInputZero       = 0;
    //pThis->Dbgc.iRead            = 0;
    //pThis->Dbgc.iWrite           = 0;
    //pThis->Dbgc.cInputLines      = 0;
    //pThis->Dbgc.fInputOverflow   = false;
    pThis->Dbgc.fReady           = true;
    pThis->Dbgc.pszScratch       = &pThis->Dbgc.achScratch[0];
    //pThis->Dbgc.iArg             = 0;
    //pThis->Dbgc.rcOutput         = 0;
    //pThis->Dbgc.rcCmd            = 0;

    //pThis->Dbgc.pszHistoryFile       = NULL;
    //pThis->Dbgc.pszGlobalInitScript  = NULL;
    //pThis->Dbgc.pszLocalInitScript   = NULL;

    dbgcEvalInit();

    /* Init the GDB stub specific parts. */
    pThis->cbPktBufMax      = 0;
    pThis->pbPktBuf         = NULL;
    pThis->fFeatures        = GDBSTUBCTX_FEATURES_F_TGT_DESC;
    pThis->pachTgtXmlDesc   = NULL;
    pThis->cbTgtXmlDesc     = 0;
    pThis->fExtendedMode    = false;
    pThis->fOutput          = false;
    pThis->fInThrdInfoQuery = false;
    RTListInit(&pThis->LstTps);
    dbgcGdbStubCtxReset(pThis);

    *ppGdbStubCtx = pThis;
    return VINF_SUCCESS;
}


/**
 * Destroys the given GDB stub context.
 *
 * @param   pThis                   The GDB stub context to destroy.
 */
static void dbgcGdbStubDestroy(PGDBSTUBCTX pThis)
{
    AssertPtr(pThis);

    /* Detach from the VM. */
    if (pThis->Dbgc.pUVM)
        DBGFR3Detach(pThis->Dbgc.pUVM);

    /* Free config strings. */
    RTStrFree(pThis->Dbgc.pszGlobalInitScript);
    pThis->Dbgc.pszGlobalInitScript = NULL;
    RTStrFree(pThis->Dbgc.pszLocalInitScript);
    pThis->Dbgc.pszLocalInitScript = NULL;
    RTStrFree(pThis->Dbgc.pszHistoryFile);
    pThis->Dbgc.pszHistoryFile = NULL;

    /* Finally, free the instance memory. */
    RTMemFree(pThis);
}


DECL_HIDDEN_CALLBACK(int) dbgcGdbStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrNullReturn(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = NULL;
    if (pUVM)
    {
        pVM = VMR3GetVM(pUVM);
        AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    }

    /*
     * Allocate and initialize instance data
     */
    PGDBSTUBCTX pThis;
    int rc = dbgcGdbStubCtxCreate(&pThis, pIo, fFlags);
    if (RT_FAILURE(rc))
        return rc;
    if (!HMR3IsEnabled(pUVM) && !NEMR3IsEnabled(pUVM))
        pThis->Dbgc.hDbgAs = DBGF_AS_RC_AND_GC_GLOBAL;

    /*
     * Attach to the specified VM.
     */
    if (RT_SUCCESS(rc) && pUVM)
    {
        rc = DBGFR3Attach(pUVM);
        if (RT_SUCCESS(rc))
        {
            pThis->Dbgc.pVM   = pVM;
            pThis->Dbgc.pUVM  = pUVM;
            pThis->Dbgc.idCpu = 0;
        }
        else
            rc = pThis->Dbgc.CmdHlp.pfnVBoxError(&pThis->Dbgc.CmdHlp, rc, "When trying to attach to VM %p\n", pThis->Dbgc.pVM);
    }

    /*
     * Load plugins.
     */
    if (RT_SUCCESS(rc))
    {
        if (pVM)
            DBGFR3PlugInLoadAll(pThis->Dbgc.pUVM);
        dbgcEventInit(&pThis->Dbgc);
        //dbgcRunInitScripts(pDbgc); Not yet

        if (!DBGFR3IsHalted(pThis->Dbgc.pUVM, VMCPUID_ALL))
        {
            rc = DBGFR3Halt(pThis->Dbgc.pUVM, VMCPUID_ALL);
            if (RT_FAILURE(rc))
                LogRel(("DBGC/Gdb: Failed to halt VM (%Rrc), debugger might behave unexpectedly\n", rc));
        }

        /*
         * Run the debugger main loop.
         */
        rc = dbgcGdbStubRun(pThis);
        dbgcEventTerm(&pThis->Dbgc);
    }

    /*
     * Cleanup console debugger session.
     */
    dbgcGdbStubDestroy(pThis);
    return rc == VERR_DBGC_QUIT ? VINF_SUCCESS : rc;
}

