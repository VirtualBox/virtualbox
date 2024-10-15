/* $Id: RTTraceLogTool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Utility for reading/receiving and dissecting trace logs.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/tracelog.h>
#include <iprt/tracelog-decoder-plugin.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/tcp.h>


typedef struct RTTRACELOGDECODER
{
    /** The tracelog decoder registration structure. */
    PCRTTRACELOGDECODERREG      pReg;
    /** The helper structure for this decoder. */
    RTTRACELOGDECODERHLP        Hlp;
    /** The decoder state if created. */
    void                        *pvDecoderState;
    /** The free callback of any attached decoder state. */
    PFNTRACELOGDECODERSTATEFREE pfnDecoderStateFree;
    /** Current struct nesting level. */
    uint32_t                    cStructNesting;
    /** Current array level nesting. */
    uint32_t                    cArrayNesting;
} RTTRACELOGDECODER;
typedef RTTRACELOGDECODER *PRTTRACELOGDECODER;
typedef const RTTRACELOGDECODER *PCRTTRACELOGDECODER;


/**
 * Loaded tracelog decoders.
 */
typedef struct RTTRACELOGDECODERSTATE
{
    /** Pointer to the array of registered decoders. */
    PRTTRACELOGDECODER            paDecoders;
    /** Number of entries in the decoder array. */
    uint32_t                      cDecoders;
    /** Allocation size of the decoder array. */
    uint32_t                      cDecodersAlloc;
} RTTRACELOGDECODERSTATE;
typedef RTTRACELOGDECODERSTATE *PRTTRACELOGDECODERSTATE;


/**
 * The tracelog tool TCP server/client state.
 */
typedef struct RTTRACELOGTOOLTCP
{
    /** Flag whether this is a server. */
    bool                        fIsServer;
    /** The TCP socket handle for the connection. */
    RTSOCKET                    hSock;
    /** The TCP server. */
    PRTTCPSERVER                pTcpSrv;
    /** File to save the read data to. */
    RTFILE                      hFile;
} RTTRACELOGTOOLTCP;
/** Pointer to the TCP server/client state. */
typedef RTTRACELOGTOOLTCP *PRTTRACELOGTOOLTCP;


static void rtTraceLogTcpDestroy(PRTTRACELOGTOOLTCP pTrcLogTcp)
{
    if (pTrcLogTcp->fIsServer)
        RTTcpServerDestroy(pTrcLogTcp->pTcpSrv);
    if (pTrcLogTcp->hSock != NIL_RTSOCKET)
    {
        if (pTrcLogTcp->fIsServer)
            RTTcpServerDisconnectClient2(pTrcLogTcp->hSock);
        else
            RTTcpClientClose(pTrcLogTcp->hSock);
    }
    if (pTrcLogTcp->hFile != NIL_RTFILE)
    {
        RTFileClose(pTrcLogTcp->hFile);
        pTrcLogTcp->hFile = NIL_RTFILE;
    }
    RTMemFree(pTrcLogTcp);
}


static DECLCALLBACK(int) rtTraceLogToolTcpInput(void *pvUser, void *pvBuf, size_t cbBuf, size_t *pcbRead,
                                                RTMSINTERVAL cMsTimeout)
{
    PRTTRACELOGTOOLTCP pTrcLogTcp = (PRTTRACELOGTOOLTCP)pvUser;
    if (   pTrcLogTcp->fIsServer
        && pTrcLogTcp->hSock == NIL_RTSOCKET)
    {
        int rc = RTTcpServerListen2(pTrcLogTcp->pTcpSrv, &pTrcLogTcp->hSock);
        if (RT_FAILURE(rc))
            return rc;
    }

    int rc = RTTcpSelectOne(pTrcLogTcp->hSock, cMsTimeout);
    if (RT_SUCCESS(rc))
        rc = RTTcpReadNB(pTrcLogTcp->hSock, pvBuf, cbBuf, pcbRead);

    if (   RT_SUCCESS(rc)
        && pTrcLogTcp->hFile != NIL_RTFILE)
    {
        int rc2 = RTFileWrite(pTrcLogTcp->hFile, pvBuf, *pcbRead, NULL);
        if (RT_FAILURE(rc2))
            RTMsgError("Failed to write received data to save file: %Rrc\n", rc2);
    }

    return rc;
}


static DECLCALLBACK(int) rtTraceLogToolTcpClose(void *pvUser)
{
    PRTTRACELOGTOOLTCP pTrcLogTcp = (PRTTRACELOGTOOLTCP)pvUser;
    rtTraceLogTcpDestroy(pTrcLogTcp);
    return VINF_SUCCESS;
}


/**
 * Tries to create a new trace log reader using the given input.
 *
 * @returns IPRT status code.
 * @param   phTraceLogRdr       Where to store the handle to the trace log reader instance on success.
 * @param   pszInput            The input path.
 * @param   pszSave             The optional path to save
 */
static int rtTraceLogToolReaderCreate(PRTTRACELOGRDR phTraceLogRdr, const char *pszInput, const char *pszSave)
{
    /* Try treating the input as a file first. */
    int rc = RTTraceLogRdrCreateFromFile(phTraceLogRdr, pszInput);
    if (RT_FAILURE(rc))
    {
        /*
         * Check whether the input looks like a port number or an address:port pair.
         * The former will create a server listening on the port while the latter tries
         * to connect to the given address:port combination.
         */
        uint32_t     uPort     = 0;
        bool         fIsServer = false;
        PRTTCPSERVER pTcpSrv   = NULL;
        RTSOCKET     hSock     = NIL_RTSOCKET;
        rc = RTStrToUInt32Full(pszInput, 10, &uPort);
        if (rc == VINF_SUCCESS)
        {
            fIsServer = true;
            rc = RTTcpServerCreateEx(NULL, uPort, &pTcpSrv);
        }
        else
        {
            /* Try treating the input as an address:port pair. */
        }

        if (RT_SUCCESS(rc))
        {
            /* Initialize structure and reader. */
            PRTTRACELOGTOOLTCP pTrcLogTcp = (PRTTRACELOGTOOLTCP)RTMemAllocZ(sizeof(*pTrcLogTcp));
            if (pTrcLogTcp)
            {
                pTrcLogTcp->fIsServer = fIsServer;
                pTrcLogTcp->hSock     = hSock;
                pTrcLogTcp->pTcpSrv   = pTcpSrv;
                rc = RTTraceLogRdrCreate(phTraceLogRdr, rtTraceLogToolTcpInput, rtTraceLogToolTcpClose, pTrcLogTcp);
                if (RT_FAILURE(rc))
                    rtTraceLogTcpDestroy(pTrcLogTcp);

                if (pszSave)
                {
                    rc = RTFileOpen(&pTrcLogTcp->hFile, pszSave, RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE | RTFILE_O_READWRITE);
                    if (RT_FAILURE(rc))
                        rtTraceLogTcpDestroy(pTrcLogTcp);
                }
            }
            else
            {
                if (fIsServer)
                    RTTcpServerDestroy(pTcpSrv);
                else
                    RTSocketClose(hSock);
            }
        }
    }
    return rc;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpPrintf(PRTTRACELOGDECODERHLP pHlp, const char *pszFormat, ...)
{
    RT_NOREF(pHlp);
    va_list Args;
    va_start(Args, pszFormat);
    int rc = RTPrintfV(pszFormat, Args);
    va_end(Args);
    return rc;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpErrorMsg(PRTTRACELOGDECODERHLP pHlp, const char *pszFormat, ...)
{
    RT_NOREF(pHlp);
    va_list Args;
    va_start(Args, pszFormat);
    int rc = RTMsgErrorV(pszFormat, Args);
    va_end(Args);
    return rc;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStateCreate(PRTTRACELOGDECODERHLP pHlp, size_t cbState, PFNTRACELOGDECODERSTATEFREE pfnFree,
                                                          void **ppvState)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (pDecoder->pvDecoderState)
    {
        if (pDecoder->pfnDecoderStateFree)
            pDecoder->pfnDecoderStateFree(pHlp, pDecoder->pvDecoderState);
        RTMemFree(pDecoder->pvDecoderState);
        pDecoder->pvDecoderState      = NULL;
        pDecoder->pfnDecoderStateFree = NULL;
    }

    pDecoder->pvDecoderState = RTMemAllocZ(cbState);
    if (pDecoder->pvDecoderState)
    {
        pDecoder->pfnDecoderStateFree = pfnFree;
        *ppvState = pDecoder->pvDecoderState;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}


static DECLCALLBACK(void) rtTraceLogToolDecoderHlpStateDestroy(PRTTRACELOGDECODERHLP pHlp)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (pDecoder->pvDecoderState)
    {
        if (pDecoder->pfnDecoderStateFree)
            pDecoder->pfnDecoderStateFree(pHlp, pDecoder->pvDecoderState);
        RTMemFree(pDecoder->pvDecoderState);
        pDecoder->pvDecoderState      = NULL;
        pDecoder->pfnDecoderStateFree = NULL;
    }
}


static DECLCALLBACK(void*) rtTraceLogToolDecoderHlpStateGet(PRTTRACELOGDECODERHLP pHlp)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    return pDecoder->pvDecoderState;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldBegin(PRTTRACELOGDECODERHLP pHlp, const char *pszName)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    RTPrintf("%*s%s:\n", pDecoder->cStructNesting * 4, "", pszName);
    pDecoder->cStructNesting++;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldEnd(PRTTRACELOGDECODERHLP pHlp)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    pDecoder->cStructNesting--;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddBool(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, bool f)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    RT_NOREF(fFlags);
    RTPrintf("%*s%-16s %32RTbool\n", pDecoder->cStructNesting * 4, "", pszName, f);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddU8(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, uint8_t u8)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (pszName)
    {
        if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
            RTPrintf("%*s%-16s %#32RX8\n", pDecoder->cStructNesting * 4, "", pszName, u8);
        else
            RTPrintf("%*s%-16s %32RU8\n", pDecoder->cStructNesting * 4, "", pszName, u8);
    }
    else
    {
        Assert(pDecoder->cArrayNesting > 0);
        if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
            RTPrintf(" %#02RX8", u8);
        else
            RTPrintf(" %02RU8", u8);
    }
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddU16(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, uint16_t u16)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#32RX16\n", pDecoder->cStructNesting * 4, "", pszName, u16);
    else
        RTPrintf("%*s%-16s %32RU16\n", pDecoder->cStructNesting * 4, "", pszName, u16);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddU32(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, uint32_t u32)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#32RX32\n", pDecoder->cStructNesting * 4, "", pszName, u32);
    else
        RTPrintf("%*s%-16s %32RU32\n", pDecoder->cStructNesting * 4, "", pszName, u32);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddU64(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, uint64_t u64)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#32RX64\n", pDecoder->cStructNesting * 4, "", pszName, u64);
    else
        RTPrintf("%*s%-16s %32RU64\n", pDecoder->cStructNesting * 4, "", pszName, u64);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddS8(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, int8_t i8)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#32RX8\n", pDecoder->cStructNesting * 4, "", pszName, i8);
    else
        RTPrintf("%*s%-16s %32RI8\n", pDecoder->cStructNesting * 4, "", pszName, i8);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddS16(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, int16_t i16)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#32RX16\n", pDecoder->cStructNesting * 4, "", pszName, i16);
    else
        RTPrintf("%*s%-16s %32RI16\n", pDecoder->cStructNesting * 4, "", pszName, i16);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddS32(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, int32_t i32)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#RX32\n", pDecoder->cStructNesting * 4, "", pszName, i32);
    else
        RTPrintf("%*s%-16s %RI32\n", pDecoder->cStructNesting * 4, "", pszName, i32);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddS64(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, int64_t i64)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s %#RX64\n", pDecoder->cStructNesting * 4, "", pszName, i64);
    else
        RTPrintf("%*s%-16s %RI64\n", pDecoder->cStructNesting * 4, "", pszName, i64);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddStr(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, const char *pszStr)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    RT_NOREF(fFlags);
    RTPrintf("%*s%-16s %32s\n", pDecoder->cStructNesting * 4, "", pszName, pszStr);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddBuf(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, const uint8_t *pb, size_t cb)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX_DUMP_STR)
        RTPrintf("%*s%-16s %.*Rhxs\n", pDecoder->cStructNesting * 4, "", pszName, cb, pb);
    else
        RTPrintf("%*s%-16s\n"
                 "%.*Rhxd\n", pDecoder->cStructNesting * 4, "", pszName, cb, pb);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldAddEnum(PRTTRACELOGDECODERHLP pHlp, const char *pszName, uint32_t fFlags, uint8_t cBits,
                                                                  PCRTTRACELOGDECODERSTRUCTBLDENUM paEnums, uint64_t u64Val)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    const char *pszEnum = "<UNKNOWN>";
    while (paEnums->pszString)
    {
        if (paEnums->u64EnumVal == u64Val)
        {
            pszEnum = paEnums->pszString;
            break;
        }
        paEnums++;
    }

    RT_NOREF(cBits);
    if (fFlags & RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX)
        RTPrintf("%*s%-16s 0x%0*RX64 (%s)\n", pDecoder->cStructNesting * 4, "", pszName, cBits / 4, u64Val, pszEnum);
    else
        RTPrintf("%*s%-16s %RU64 (%s)\n", pDecoder->cStructNesting * 4, "", pszName, u64Val, pszEnum);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldArrayBegin(PRTTRACELOGDECODERHLP pHlp, const char *pszName)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);

    RTPrintf("%*s%-16s [", pDecoder->cStructNesting * 4, "", pszName);
    Assert(!pDecoder->cArrayNesting);
    pDecoder->cArrayNesting++;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolDecoderHlpStructBldArrayEnd(PRTTRACELOGDECODERHLP pHlp)
{
    PRTTRACELOGDECODER pDecoder = RT_FROM_MEMBER(pHlp, RTTRACELOGDECODER, Hlp);
    RTPrintf(" ]\n");
    Assert(pDecoder->cArrayNesting > 0);
    pDecoder->cArrayNesting--;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogToolRegisterDecoders(void *pvUser, PCRTTRACELOGDECODERREG paDecoders, uint32_t cDecoders)
{
    PRTTRACELOGDECODERSTATE pDecoderState = (PRTTRACELOGDECODERSTATE)pvUser;

    if (pDecoderState->cDecodersAlloc - pDecoderState->cDecoders <= cDecoders)
    {
        PRTTRACELOGDECODER paNew = (PRTTRACELOGDECODER)RTMemRealloc(pDecoderState->paDecoders,
                                                                    (pDecoderState->cDecodersAlloc + cDecoders) * sizeof(*paNew));
        if (!paNew)
            return VERR_NO_MEMORY;

        pDecoderState->paDecoders      = paNew;
        pDecoderState->cDecodersAlloc += cDecoders;
    }

    for (uint32_t i = 0; i < cDecoders; i++)
    {
        PRTTRACELOGDECODER pDecoder = &pDecoderState->paDecoders[i];

        pDecoder->pReg                       = &paDecoders[i];
        pDecoder->pvDecoderState             = NULL;
        pDecoder->pfnDecoderStateFree        = NULL;
        pDecoder->cStructNesting             = 0;
        pDecoder->cArrayNesting              = 0;
        pDecoder->Hlp.pfnPrintf              = rtTraceLogToolDecoderHlpPrintf;
        pDecoder->Hlp.pfnErrorMsg            = rtTraceLogToolDecoderHlpErrorMsg;
        pDecoder->Hlp.pfnDecoderStateCreate  = rtTraceLogToolDecoderHlpStateCreate;
        pDecoder->Hlp.pfnDecoderStateDestroy = rtTraceLogToolDecoderHlpStateDestroy;
        pDecoder->Hlp.pfnDecoderStateGet     = rtTraceLogToolDecoderHlpStateGet;
        pDecoder->Hlp.pfnStructBldBegin      = rtTraceLogToolDecoderHlpStructBldBegin;
        pDecoder->Hlp.pfnStructBldEnd        = rtTraceLogToolDecoderHlpStructBldEnd;
        pDecoder->Hlp.pfnStructBldAddBool    = rtTraceLogToolDecoderHlpStructBldAddBool;
        pDecoder->Hlp.pfnStructBldAddU8      = rtTraceLogToolDecoderHlpStructBldAddU8;
        pDecoder->Hlp.pfnStructBldAddU16     = rtTraceLogToolDecoderHlpStructBldAddU16;
        pDecoder->Hlp.pfnStructBldAddU32     = rtTraceLogToolDecoderHlpStructBldAddU32;
        pDecoder->Hlp.pfnStructBldAddU64     = rtTraceLogToolDecoderHlpStructBldAddU64;
        pDecoder->Hlp.pfnStructBldAddS8      = rtTraceLogToolDecoderHlpStructBldAddS8;
        pDecoder->Hlp.pfnStructBldAddS16     = rtTraceLogToolDecoderHlpStructBldAddS16;
        pDecoder->Hlp.pfnStructBldAddS32     = rtTraceLogToolDecoderHlpStructBldAddS32;
        pDecoder->Hlp.pfnStructBldAddS64     = rtTraceLogToolDecoderHlpStructBldAddS64;
        pDecoder->Hlp.pfnStructBldAddStr     = rtTraceLogToolDecoderHlpStructBldAddStr;
        pDecoder->Hlp.pfnStructBldAddBuf     = rtTraceLogToolDecoderHlpStructBldAddBuf;
        pDecoder->Hlp.pfnStructBldAddEnum    = rtTraceLogToolDecoderHlpStructBldAddEnum;
        pDecoder->Hlp.pfnStructBldArrayBegin = rtTraceLogToolDecoderHlpStructBldArrayBegin;
        pDecoder->Hlp.pfnStructBldArrayEnd   = rtTraceLogToolDecoderHlpStructBldArrayEnd;
    }

    pDecoderState->cDecoders += cDecoders;
    return VINF_SUCCESS;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--input",        'i', RTGETOPT_REQ_STRING },
        { "--save",         's', RTGETOPT_REQ_STRING },
        { "--load-decoder", 'l', RTGETOPT_REQ_STRING },
        { "--help",         'h', RTGETOPT_REQ_NOTHING },
        { "--version",      'V', RTGETOPT_REQ_NOTHING },
    };

    RTEXITCODE              rcExit   = RTEXITCODE_SUCCESS;
    const char             *pszInput = NULL;
    const char             *pszSave  = NULL;
    RTTRACELOGDECODERSTATE  DecoderState; RT_ZERO(DecoderState);

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'h':
                RTPrintf("Usage: %s [options]\n"
                         "\n"
                         "Options:\n"
                         "  -i,--input=<file|port|address:port>\n"
                         "      Input path, can be a file a port to start listening on for incoming connections or an address:port to connect to\n"
                         "  -s,--save=file\n"
                         "      Save the input to a file for later use\n"
                         "  -l,--load-decoder=<plugin path>\n"
                         "      Loads the given decoder library used for decoding events\n"
                         "  -h, -?, --help\n"
                         "      Display this help text and exit successfully.\n"
                         "  -V, --version\n"
                         "      Display the revision and exit successfully.\n"
                         , RTPathFilename(argv[0]));
                return RTEXITCODE_SUCCESS;
            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;

            case 'i':
                pszInput = ValueUnion.psz;
                break;
            case 's':
                pszSave = ValueUnion.psz;
                break;
            case 'l':
            {
                RTLDRMOD hLdrMod;
                rc = RTLdrLoadEx(ValueUnion.psz, &hLdrMod, RTLDRLOAD_FLAGS_NO_UNLOAD, NULL);
                if (RT_SUCCESS(rc))
                {
                    PFNTRACELOGDECODERPLUGINLOAD pfnLoad = NULL;
                    rc = RTLdrGetSymbol(hLdrMod, RT_TRACELOG_DECODER_PLUGIN_LOAD, (void **)&pfnLoad);
                    if (RT_SUCCESS(rc))
                    {
                        RTTRACELOGDECODERREGISTER RegCb;

                        RegCb.u32Version          = RT_TRACELOG_DECODERREG_CB_VERSION;
                        RegCb.pfnRegisterDecoders = rtTraceLogToolRegisterDecoders;

                        rc = pfnLoad(&DecoderState, &RegCb);
                        if (RT_FAILURE(rc))
                            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to register decoders %Rrc\n", rc);
                    }
                    else
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to lretrieve entry point '%s' %Rrc\n",
                                              RT_TRACELOG_DECODER_PLUGIN_LOAD, rc);

                    RTLdrClose(hLdrMod);
                }
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to load decoder library %Rrc\n", rc);
                break;
            }
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (!pszInput)
    {
        RTPrintf("An input path must be given\n");
        return RTEXITCODE_FAILURE;
    }

    /*
     * Create trace log reader instance.
     */
    RTTRACELOGRDR hTraceLogRdr = NIL_RTTRACELOGRDR;
    rc = rtTraceLogToolReaderCreate(&hTraceLogRdr, pszInput, pszSave);
    if (RT_SUCCESS(rc))
    {
        do
        {
            RTTRACELOGRDRPOLLEVT enmEvt = RTTRACELOGRDRPOLLEVT_INVALID;
            rc = RTTraceLogRdrEvtPoll(hTraceLogRdr, &enmEvt, RT_INDEFINITE_WAIT);
            if (RT_SUCCESS(rc))
            {
                switch (enmEvt)
                {
                    case RTTRACELOGRDRPOLLEVT_HDR_RECVD:
                        RTMsgInfo("A valid header was received\n");
                        break;
                    case RTTRACELOGRDRPOLLEVT_TRACE_EVENT_RECVD:
                    {
                        RTTRACELOGRDREVT hTraceLogEvt;
                        rc = RTTraceLogRdrQueryLastEvt(hTraceLogRdr, &hTraceLogEvt);
                        if (RT_SUCCESS(rc))
                        {
                            PCRTTRACELOGEVTDESC pEvtDesc = RTTraceLogRdrEvtGetDesc(hTraceLogEvt);
                            RTMsgInfo("%llu        %llu        %s\n",
                                      RTTraceLogRdrEvtGetSeqNo(hTraceLogEvt),
                                      RTTraceLogRdrEvtGetTs(hTraceLogEvt),
                                      pEvtDesc->pszId);

                            /*
                             * Look through our registered decoders and pass the decoding on to it.
                             * If there is no decoder registered just dump the raw values.
                             */
                            PRTTRACELOGDECODER    pDecoder = NULL;
                            PCRTTRACELOGDECODEEVT pDecodeEvt  = NULL;
                            for (uint32_t i = 0; (i < DecoderState.cDecoders) && !pDecoder; i++)
                            {
                                PCRTTRACELOGDECODEEVT pTmp = DecoderState.paDecoders[i].pReg->paEvtIds;
                                while (pTmp->pszEvtId)
                                {
                                    if (!strcmp(pTmp->pszEvtId, pEvtDesc->pszId))
                                    {
                                        pDecoder   = &DecoderState.paDecoders[i];
                                        pDecodeEvt = pTmp;
                                        break;
                                    }
                                    pTmp++;
                                }
                            }

                            if (pDecoder)
                            {
                                Assert(pDecodeEvt);

                                /** @todo Dynamic value allocation (too lazy right now). */
                                RTTRACELOGEVTVAL aVals[32];
                                uint32_t cVals = 0;
                                rc = RTTraceLogRdrEvtFillVals(hTraceLogEvt, 0, &aVals[0], RT_ELEMENTS(aVals),
                                                              &cVals);
                                if (   RT_SUCCESS(rc)
                                    || cVals != pEvtDesc->cEvtItems)
                                {
                                    pDecoder->cStructNesting = 0;
                                    pDecoder->cArrayNesting  = 0;
                                    rc = pDecoder->pReg->pfnDecode(&pDecoder->Hlp, pDecodeEvt->idDecodeEvt, hTraceLogEvt,
                                                                   pEvtDesc, &aVals[0], cVals);
                                    if (RT_FAILURE(rc))
                                        RTMsgError("Failed to decode event with ID '%s' -> %Rrc\n", pEvtDesc->pszId, rc);
                                }
                                else
                                    RTMsgError("Failed to fill values for event with ID '%s' -> %Rrc (cVals=%u vs. cEvtItems=%u)\n",
                                               pEvtDesc->pszId, rc, cVals, pEvtDesc->cEvtItems);
                            }
                            else
                                for (unsigned i = 0; i < pEvtDesc->cEvtItems; i++)
                                {
                                    RTTRACELOGEVTVAL Val;
                                    unsigned cVals = 0;
                                    rc = RTTraceLogRdrEvtFillVals(hTraceLogEvt, i, &Val, 1, &cVals);
                                    if (RT_SUCCESS(rc))
                                    {
                                        switch (Val.pItemDesc->enmType)
                                        {
                                            case RTTRACELOGTYPE_BOOL:
                                                RTMsgInfo("    %s: %s\n", Val.pItemDesc->pszName, Val.u.f ? "true" : "false");
                                                break;
                                            case RTTRACELOGTYPE_UINT8:
                                                RTMsgInfo("    %s: %u\n", Val.pItemDesc->pszName, Val.u.u8);
                                                break;
                                            case RTTRACELOGTYPE_INT8:
                                                RTMsgInfo("    %s: %d\n", Val.pItemDesc->pszName, Val.u.i8);
                                                break;
                                            case RTTRACELOGTYPE_UINT16:
                                                RTMsgInfo("    %s: %u\n", Val.pItemDesc->pszName, Val.u.u16);
                                                break;
                                            case RTTRACELOGTYPE_INT16:
                                                RTMsgInfo("    %s: %d\n", Val.pItemDesc->pszName, Val.u.i16);
                                                break;
                                            case RTTRACELOGTYPE_UINT32:
                                                RTMsgInfo("    %s: %u\n", Val.pItemDesc->pszName, Val.u.u32);
                                                break;
                                            case RTTRACELOGTYPE_INT32:
                                                RTMsgInfo("    %s: %d\n", Val.pItemDesc->pszName, Val.u.i32);
                                                break;
                                            case RTTRACELOGTYPE_UINT64:
                                                RTMsgInfo("    %s: %llu\n", Val.pItemDesc->pszName, Val.u.u64);
                                                break;
                                            case RTTRACELOGTYPE_INT64:
                                                RTMsgInfo("    %s: %lld\n", Val.pItemDesc->pszName, Val.u.i64);
                                                break;
                                            case RTTRACELOGTYPE_RAWDATA:
                                                RTMsgInfo("    %s:\n"
                                                          "%.*Rhxd\n", Val.pItemDesc->pszName, Val.u.RawData.cb, Val.u.RawData.pb);
                                                break;
                                            case RTTRACELOGTYPE_FLOAT32:
                                            case RTTRACELOGTYPE_FLOAT64:
                                                RTMsgInfo("    %s: Float32 and Float64 data not supported yet\n", Val.pItemDesc->pszName);
                                                break;
                                            case RTTRACELOGTYPE_POINTER:
                                                RTMsgInfo("    %s: %#llx\n", Val.pItemDesc->pszName, Val.u.uPtr);
                                                break;
                                            case RTTRACELOGTYPE_SIZE:
                                                RTMsgInfo("    %s: %llu\n", Val.pItemDesc->pszName, Val.u.sz);
                                                break;
                                            default:
                                                RTMsgError("    %s: Invalid type given %d\n", Val.pItemDesc->pszName, Val.pItemDesc->enmType);
                                        }
                                    }
                                    else
                                        RTMsgInfo("    Failed to retrieve event data with %Rrc\n", rc);
                                }
                        }
                        break;
                    }
                    default:
                        RTMsgInfo("Invalid event received: %d\n", enmEvt);
                }
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Polling for an event failed with %Rrc\n", rc);
        } while (RT_SUCCESS(rc));

        RTTraceLogRdrDestroy(hTraceLogRdr);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create trace log reader with %Rrc\n", rc);

    return rcExit;
}

