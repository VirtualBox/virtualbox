/* $Id: tracelogreader.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Trace log reader.
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
#include "internal/iprt.h"
#include <iprt/formats/tracelog.h>
#include <iprt/tracelog.h>


#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/strcache.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a trace log reader instance. */
typedef struct RTTRACELOGRDRINT *PRTTRACELOGRDRINT;

/**
 * State enums the trace log reader can be in.
 */
typedef enum RTTRACELOGRDRSTATE
{
    /** Invalid state. */
    RTTRACELOGRDRSTATE_INVALID = 0,
    /** The header is currently being received. */
    RTTRACELOGRDRSTATE_RECV_HDR,
    /** The header description is being received (if available). */
    RTTRACELOGRDRSTATE_RECV_HDR_DESC,
    /** the magic is being received to decide what to do next. */
    RTTRACELOGRDRSTATE_RECV_MAGIC,
    /** The event descriptor is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_DESC,
    /** The event descriptor ID is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_DESC_ID,
    /** The event descriptor description is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_DESC_DESC,
    /** The event item descriptor is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC,
    /** The event item descriptor name is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC_NAME,
    /** The event item descriptor description is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC_DESC,
    /** The event marker is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_MARKER,
    /** The event data is being received. */
    RTTRACELOGRDRSTATE_RECV_EVT_DATA,
    /** 32bit hack. */
    RTTRACELOGRDRSTATE_32BIT_HACK = 0x7fffffff
} RTTRACELOGRDRSTATE;


/** Pointer to internal trace log reader event descriptor. */
typedef struct RTTRACELOGRDREVTDESC *PRTTRACELOGRDREVTDESC;
/** Pointer to const internal trace log reader event descriptor. */
typedef const RTTRACELOGRDREVTDESC *PCRTTRACELOGRDREVTDESC;


/**
 * Trace log reader event.
 */
typedef struct RTTRACELOGRDREVTINT
{
    /** List node for the global list of events. */
    RTLISTNODE                  NdGlob;
    /** The trace log reader instance the event belongs to. */
    PRTTRACELOGRDRINT           pRdr;
    /** Trace log sequence number. */
    uint64_t                    u64SeqNo;
    /** Marker time stamp. */
    uint64_t                    u64Ts;
    /** Pointer to the event descriptor, describing the data layout. */
    PCRTTRACELOGRDREVTDESC      pEvtDesc;
    /** Parent group ID if assigned. */
    RTTRACELOGEVTGRPID          idGrpParent;
    /** Group ID this event belongs to. */
    RTTRACELOGEVTGRPID          idGrp;
    /** Pointer to the array holding the non static raw data size values. */
    size_t                      *pacbRawData;
    /** Overall event data size in bytes, including non static data. */
    size_t                      cbEvtData;
    /** Event data, variable in size. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t                     abEvtData[RT_FLEXIBLE_ARRAY];
} RTTRACELOGRDREVTINT;
/** Pointer to a trace log reader event. */
typedef RTTRACELOGRDREVTINT *PRTTRACELOGRDREVTINT;
/** Pointer to a const trace log reader event. */
typedef const RTTRACELOGRDREVTINT *PCRTTRACELOGRDREVTINT;


/**
 * Trace log reader internal event descriptor.
 */
typedef struct RTTRACELOGRDREVTDESC
{
    /** Overall size of the event data not counting variable raw data items. */
    size_t                      cbEvtData;
    /** Number of non static raw binary items in the descriptor. */
    uint32_t                    cRawDataNonStatic;
    /** Current event item descriptor to work on. */
    uint32_t                    idxEvtItemCur;
    /** Size of the name of the current item to work on. */
    size_t                      cbStrItemName;
    /** Size of the description of the current item to work on. */
    size_t                      cbStrItemDesc;
    /** Size of the ID in bytes including the terminator. */
    size_t                      cbStrId;
    /** Size of the description in bytes including the terminator. */
    size_t                      cbStrDesc;
    /** Embedded event descriptor. */
    RTTRACELOGEVTDESC           EvtDesc;
    /** Array of event item descriptors, variable in size. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTTRACELOGEVTITEMDESC       aEvtItemDesc[RT_FLEXIBLE_ARRAY];
} RTTRACELOGRDREVTDESC;


/**
 * Trace log reader instance data.
 */
typedef struct RTTRACELOGRDRINT
{
    /** Magic for identification. */
    uint32_t                    u32Magic;
    /** Stream out callback. */
    PFNRTTRACELOGRDRSTREAM      pfnStreamIn;
    /** Stream close callback .*/
    PFNRTTRACELOGSTREAMCLOSE    pfnStreamClose;
    /** Opaque user data passed to the stream callback. */
    void                        *pvUser;
    /** Mutex protecting the structure. */
    RTSEMMUTEX                  hMtx;
    /** Current state the reader is in. */
    RTTRACELOGRDRSTATE          enmState;
    /** Flag whether to convert all inputs to the host endianess. */
    bool                        fConvEndianess;
    /** String cache for descriptions and IDs. */
    RTSTRCACHE                  hStrCache;
    /** Size of the description in characters. */
    size_t                      cchDesc;
    /** Pointer to the description if set. */
    const char                  *pszDesc;
    /** List of received events (PRTTRACELOGRDREVTINT::NdGlob). */
    RTLISTANCHOR                LstEvts;
    /** Number of event descriptors known. */
    uint32_t                    cEvtDescsCur;
    /** Maximum number of event descriptors currently fitting into the array. */
    uint32_t                    cEvtDescsMax;
    /** Pointer to the array of event descriptor pointers. */
    PRTTRACELOGRDREVTDESC       *papEvtDescs;
    /** Current event descriptor being initialised. */
    PRTTRACELOGRDREVTDESC       pEvtDescCur;
    /** The current event being received. */
    PRTTRACELOGRDREVTINT        pEvtCur;
    /** Last seen sequence number. */
    uint64_t                    u64SeqNoLast;
    /** Size of the scratch buffer holding the received data. */
    size_t                      cbScratch;
    /** Pointer to the scratch buffer. */
    uint8_t                     *pbScratch;
    /** Current offset into the scratch buffer to write fetched data to. */
    uint32_t                    offScratch;
    /** Number of bytes left to receive until processing the data. */
    size_t                      cbRecvLeft;
    /** Starting timestamp fetched from the header. */
    uint64_t                    u64TsStart;
    /** Size of the pointer type in the trace log. */
    size_t                      cbTypePtr;
    /** Size of the size_t type in the trace log. */
    size_t                      cbTypeSize;
} RTTRACELOGRDRINT;


/**
 * Internal reader iterator instance data.
 */
typedef struct RTTRACELOGRDRITINT
{
    /** The reader instance this iterator belongs to. */
    PRTTRACELOGRDRINT           pRdr;
    /** The current event. */
    PRTTRACELOGRDREVTINT        pEvt;
} RTTRACELOGRDRITINT;
/** Pointer to an internal reader iterator instance. */
typedef RTTRACELOGRDRITINT *PRTTRACELOGRDRITINT;


/**
 * Trace log handler state callback.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log reader instance.
 * @param   penmEvt             Where to store the event indicator if a user visible event happened.
 * @param   pfContinuePoll      Where to store the flag whether to continue polling.
 */
typedef DECLCALLBACKTYPE(int, FNRTTRACELOGRDRSTATEHANDLER,(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                           bool *pfContinuePoll));
/** Pointer to a trace log reader state handler. */
typedef FNRTTRACELOGRDRSTATEHANDLER *PFNRTTRACELOGRDRSTATEHANDLER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtTraceLogRdrHdrRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrHdrDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrMagicRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtDescIdRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtDescDescriptionRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescNameRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescDescriptionRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtMarkerRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);
static DECLCALLBACK(int) rtTraceLogRdrEvtDataRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt, bool *pfContinuePoll);

/**
 * State handlers.
 * @note The struct wrapper is for working around a Clang nothrow attrib oddity.
 */
static struct { PFNRTTRACELOGRDRSTATEHANDLER pfn; } g_aStateHandlers[] =
{
    { NULL },
    { rtTraceLogRdrHdrRecvd },
    { rtTraceLogRdrHdrDescRecvd },
    { rtTraceLogRdrMagicRecvd },
    { rtTraceLogRdrEvtDescRecvd },
    { rtTraceLogRdrEvtDescIdRecvd },
    { rtTraceLogRdrEvtDescDescriptionRecvd },
    { rtTraceLogRdrEvtItemDescRecvd },
    { rtTraceLogRdrEvtItemDescNameRecvd },
    { rtTraceLogRdrEvtItemDescDescriptionRecvd },
    { rtTraceLogRdrEvtMarkerRecvd },
    { rtTraceLogRdrEvtDataRecvd },
    { NULL }
};

/**
 * Wrapper around the stream in callback.
 *
 * @returns IPRT status code returned by the stream callback.
 * @param   pThis               The trace log reader instance.
 * @param   pvBuf               The data to stream.
 * @param   cbBuf               Number of bytes to read in.
 * @param   pcbRead             Where to store the amount of data read.
 * @param   cMsTimeout          How long to wait for something to arrive.
 */
DECLINLINE(int) rtTraceLogRdrStreamRead(PRTTRACELOGRDRINT pThis, void *pvBuf, size_t cbBuf,
                                        size_t *pcbRead, RTMSINTERVAL cMsTimeout)
{
    return pThis->pfnStreamIn(pThis->pvUser, pvBuf, cbBuf, pcbRead, cMsTimeout);
}


/**
 * Converts the header endianess to the host endianess.
 *
 * @param   pHdr                The trace log header to convert.
 */
static void rtTraceLogRdrHdrEndianessConv(PTRACELOGHDR pHdr)
{
    pHdr->u32Endianess = RT_BSWAP_U32(pHdr->u32Endianess);
    pHdr->u32Version   = RT_BSWAP_U32(pHdr->u32Version);
    pHdr->fFlags       = RT_BSWAP_U32(pHdr->fFlags);
    pHdr->cbStrDesc    = RT_BSWAP_U32(pHdr->cbStrDesc);
    pHdr->u64TsStart   = RT_BSWAP_U64(pHdr->u64TsStart);
}


/**
 * Converts the event descriptor endianess to the host endianess.
 *
 * @param   pEvtDesc            The trace log event descriptor to convert.
 */
static void rtTraceLogRdrEvtDescEndianessConv(PTRACELOGEVTDESC pEvtDesc)
{
    pEvtDesc->u32Id       = RT_BSWAP_U32(pEvtDesc->u32Id);
    pEvtDesc->u32Severity = RT_BSWAP_U32(pEvtDesc->u32Severity);
    pEvtDesc->cbStrId     = RT_BSWAP_U32(pEvtDesc->cbStrId);
    pEvtDesc->cbStrDesc   = RT_BSWAP_U32(pEvtDesc->cbStrDesc);
    pEvtDesc->cEvtItems   = RT_BSWAP_U32(pEvtDesc->cEvtItems);
}


/**
 * Converts the event item descriptor endianess to host endianess.
 *
 * @param   pEvtItemDesc        The trace log event item descriptor to convert.
 */
static void rtTraceLogRdrEvtItemDescEndianessConv(PTRACELOGEVTITEMDESC pEvtItemDesc)
{
    pEvtItemDesc->cbStrName = RT_BSWAP_U32(pEvtItemDesc->cbStrName);
    pEvtItemDesc->cbStrDesc = RT_BSWAP_U32(pEvtItemDesc->cbStrDesc);
    pEvtItemDesc->u32Type   = RT_BSWAP_U32(pEvtItemDesc->u32Type);
    pEvtItemDesc->cbRawData = RT_BSWAP_U32(pEvtItemDesc->cbRawData);
}


/**
 * Converts the event marker endianess to host endianess.
 *
 * @param   pEvt                The trace log event marker to convert.
 */
static void rtTraceLogRdrEvtEndianessConv(PTRACELOGEVT pEvt)
{
    pEvt->u64SeqNo          = RT_BSWAP_U64(pEvt->u64SeqNo);
    pEvt->u64Ts             = RT_BSWAP_U64(pEvt->u64Ts);
    pEvt->u64EvtGrpId       = RT_BSWAP_U64(pEvt->u64EvtGrpId);
    pEvt->u64EvtParentGrpId = RT_BSWAP_U64(pEvt->u64EvtParentGrpId);
    pEvt->fFlags            = RT_BSWAP_U32(pEvt->fFlags);
    pEvt->u32EvtDescId      = RT_BSWAP_U32(pEvt->u32EvtDescId);
    pEvt->cbEvtData         = RT_BSWAP_U32(pEvt->cbEvtData);
    pEvt->cRawEvtDataSz     = RT_BSWAP_U32(pEvt->cRawEvtDataSz);
}


/**
 * Converts severity field from stream to API value.
 *
 * @returns API severity enum, RTTRACELOGEVTSEVERITY_INVALID if the supplied stream value
 *          is invalid.
 * @param   u32Severity         The severity value from the stream.
 */
static RTTRACELOGEVTSEVERITY rtTraceLogRdrConvSeverity(uint32_t u32Severity)
{
    RTTRACELOGEVTSEVERITY enmSeverity = RTTRACELOGEVTSEVERITY_INVALID;

    switch (u32Severity)
    {
        case TRACELOG_EVTDESC_SEVERITY_INFO:
            enmSeverity = RTTRACELOGEVTSEVERITY_INFO;
            break;
        case TRACELOG_EVTDESC_SEVERITY_WARNING:
            enmSeverity = RTTRACELOGEVTSEVERITY_WARNING;
            break;
        case TRACELOG_EVTDESC_SEVERITY_ERROR:
            enmSeverity = RTTRACELOGEVTSEVERITY_ERROR;
            break;
        case TRACELOG_EVTDESC_SEVERITY_FATAL:
            enmSeverity = RTTRACELOGEVTSEVERITY_FATAL;
            break;
        case TRACELOG_EVTDESC_SEVERITY_DEBUG:
            enmSeverity = RTTRACELOGEVTSEVERITY_DEBUG;
            break;
        default:
            enmSeverity = RTTRACELOGEVTSEVERITY_INVALID;
    }

    return enmSeverity;
}


/**
 * Converts type field from stream to API value.
 *
 * @returns API type enum, RTTRACELOGTYPE_INVALID if the supplied stream value
 *          is invalid.
 * @param   u32Type             The type value from the stream.
 */
static RTTRACELOGTYPE rtTraceLogRdrConvType(uint32_t u32Type)
{
    RTTRACELOGTYPE enmType = RTTRACELOGTYPE_INVALID;

    switch (u32Type)
    {
        case TRACELOG_EVTITEMDESC_TYPE_BOOL:
            enmType = RTTRACELOGTYPE_BOOL;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_UINT8:
            enmType = RTTRACELOGTYPE_UINT8;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_INT8:
            enmType = RTTRACELOGTYPE_INT8;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_UINT16:
            enmType = RTTRACELOGTYPE_UINT16;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_INT16:
            enmType = RTTRACELOGTYPE_INT16;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_UINT32:
            enmType = RTTRACELOGTYPE_UINT32;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_INT32:
            enmType = RTTRACELOGTYPE_INT32;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_UINT64:
            enmType = RTTRACELOGTYPE_UINT64;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_INT64:
            enmType = RTTRACELOGTYPE_INT64;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_FLOAT32:
            enmType = RTTRACELOGTYPE_FLOAT32;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_FLOAT64:
            enmType = RTTRACELOGTYPE_FLOAT64;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_RAWDATA:
            enmType = RTTRACELOGTYPE_RAWDATA;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_POINTER:
            enmType = RTTRACELOGTYPE_POINTER;
            break;
        case TRACELOG_EVTITEMDESC_TYPE_SIZE:
            enmType = RTTRACELOGTYPE_SIZE;
            break;
        default:
            enmType = RTTRACELOGTYPE_INVALID;
    }

    return enmType;
}


/**
 * Converts the type enum to the size of the the event item data in bytes.
 *
 * @returns Event item data size in bytes.
 * @param   pThis               The trace log reader instance.
 * @param   pEvtItemDesc        The event item descriptor.
 */
static size_t rtTraceLogRdrGetEvtItemDataSz(PRTTRACELOGRDRINT pThis, PCRTTRACELOGEVTITEMDESC pEvtItemDesc)
{
    size_t cb = 0;

    switch (pEvtItemDesc->enmType)
    {
        case RTTRACELOGTYPE_BOOL:
        case RTTRACELOGTYPE_UINT8:
        case RTTRACELOGTYPE_INT8:
        {
            cb = 1;
            break;
        }
        case RTTRACELOGTYPE_UINT16:
        case RTTRACELOGTYPE_INT16:
        {
            cb = 2;
            break;
        }
        case RTTRACELOGTYPE_UINT32:
        case RTTRACELOGTYPE_INT32:
        case RTTRACELOGTYPE_FLOAT32:
        {
            cb = 4;
            break;
        }
        case RTTRACELOGTYPE_UINT64:
        case RTTRACELOGTYPE_INT64:
        case RTTRACELOGTYPE_FLOAT64:
        {
            cb = 8;
            break;
        }
        case RTTRACELOGTYPE_RAWDATA:
        {
            cb = pEvtItemDesc->cbRawData;
            break;
        }
        case RTTRACELOGTYPE_POINTER:
        {
            cb = pThis->cbTypePtr;
            break;
        }
        case RTTRACELOGTYPE_SIZE:
        {
            cb = pThis->cbTypeSize;
            break;
        }
        default:
            AssertMsgFailed(("Invalid type %d\n", pEvtItemDesc->enmType));
    }

    return cb;
}


/**
 * Calculates the overall event data size from the items in the event descriptor.
 *
 * @param   pThis               The trace log reader instance.
 * @param   pEvtDesc            The event descriptor.
 */
static void rtTraceLogRdrEvtCalcEvtDataSz(PRTTRACELOGRDRINT pThis, PRTTRACELOGRDREVTDESC pEvtDesc)
{
    pEvtDesc->cbEvtData         = 0;
    pEvtDesc->cRawDataNonStatic = 0;

    for (unsigned i = 0; i < pEvtDesc->EvtDesc.cEvtItems; i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDesc->aEvtItemDesc[i];

        pEvtDesc->cbEvtData += rtTraceLogRdrGetEvtItemDataSz(pThis, pEvtItemDesc);
        if (   pEvtItemDesc->enmType == RTTRACELOGTYPE_RAWDATA
            && pEvtItemDesc->cbRawData == 0)
            pEvtDesc->cRawDataNonStatic++;
    }
}


/**
 * Ensures that the scratch buffer can hold at least the given amount of data.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log reader instance.
 * @param   cbScratch           New size of the scratch buffer in bytes.
 */
static int rtTraceLogRdrScratchEnsureSz(PRTTRACELOGRDRINT pThis, size_t cbScratch)
{
    int rc = VINF_SUCCESS;

    if (pThis->cbScratch < cbScratch)
    {
        cbScratch = RT_ALIGN_Z(cbScratch, 64);
        uint8_t *pbScratchNew = (uint8_t *)RTMemRealloc(pThis->pbScratch, cbScratch);
        if (RT_LIKELY(pbScratchNew))
        {
            pThis->cbScratch = cbScratch;
            pThis->pbScratch = pbScratchNew;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Advances to the next state resetting the scratch/receive buffers to the given state.
 *
 * @returns IPRT status.
 * @param   pThis               The trace log reader instance.
 * @param   enmState            The next state.
 * @param   cbRecv              How much to receive before processing the new data.
 * @param   offScratch          Offset to set the receive buffer to (used
 *                              when the magic was received which should still be saved).
 */
static int rtTraceLogRdrStateAdvanceEx(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRSTATE enmState, size_t cbRecv,
                                       uint32_t offScratch)
{
    Assert(cbRecv >= offScratch);

    pThis->enmState   = enmState;
    pThis->cbRecvLeft = cbRecv - offScratch;
    pThis->offScratch = offScratch;
    int rc = rtTraceLogRdrScratchEnsureSz(pThis, cbRecv);

    /* Zero out scratch buffer (don't care whether growing it failed, the old buffer is still there). */
    memset(pThis->pbScratch + offScratch, 0, pThis->cbScratch - offScratch);

    return rc;
}


/**
 * Advances to the next state resetting the scratch/receive buffers.
 *
 * @returns IPRT status.
 * @param   pThis               The trace log reader instance.
 * @param   enmState            The next state.
 * @param   cbRecv              How much to receive before processing the new data.
 */
static int rtTraceLogRdrStateAdvance(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRSTATE enmState, size_t cbRecv)
{
    return rtTraceLogRdrStateAdvanceEx(pThis, enmState, cbRecv, 0);
}


/**
 * Marks a received event descriptor as completed and adds it to the array of known descriptors.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log reader instance.
 * @param   pEvtDesc            The event descriptor which completed.
 */
static int rtTraceLogRdrEvtDescComplete(PRTTRACELOGRDRINT pThis, PRTTRACELOGRDREVTDESC pEvtDesc)
{
    int rc = VINF_SUCCESS;

    rtTraceLogRdrEvtCalcEvtDataSz(pThis, pEvtDesc);
    /* Insert into array of known event descriptors. */
    if (pThis->cEvtDescsCur == pThis->cEvtDescsMax)
    {
        uint32_t cEvtDescsNew = pThis->cEvtDescsMax + 10;
        size_t cbNew = cEvtDescsNew * sizeof(PRTTRACELOGRDREVTDESC *);
        PRTTRACELOGRDREVTDESC *papEvtDescsNew = (PRTTRACELOGRDREVTDESC *)RTMemRealloc(pThis->papEvtDescs, cbNew);
        if (RT_LIKELY(papEvtDescsNew))
        {
            pThis->papEvtDescs  = papEvtDescsNew;
            pThis->cEvtDescsMax = cEvtDescsNew;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        pThis->papEvtDescs[pThis->cEvtDescsCur++] = pEvtDesc;
        pThis->pEvtDescCur = NULL;
        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_MAGIC, TRACELOG_MAGIC_SZ);
    }

    return rc;
}


/**
 * Decides which state to enter next after one event item descriptor was completed successfully.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log reader instance.
 * @param   penmEvt             Where to store the event indicator if a user visible event happened.
 * @param   pfContinuePoll      Where to store the flag whether to continue polling.
 */
static int rtTraceLogRdrEvtItemDescComplete(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                            bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);

    int rc = VINF_SUCCESS;
    PRTTRACELOGRDREVTDESC pEvtDesc = pThis->pEvtDescCur;
    pEvtDesc->idxEvtItemCur++;

    /* If this event descriptor is complete add it to the array of known descriptors. */
    if (pEvtDesc->idxEvtItemCur == pEvtDesc->EvtDesc.cEvtItems)
        rc = rtTraceLogRdrEvtDescComplete(pThis, pEvtDesc);
    else
    {
        /* Not done yet. */
        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC, sizeof(TRACELOGEVTITEMDESC));
    }

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received header.}
 */
static DECLCALLBACK(int) rtTraceLogRdrHdrRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                               bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    PTRACELOGHDR pHdr = (PTRACELOGHDR)pThis->pbScratch;

    /* Verify magic. */
    if (!memcmp(&pHdr->szMagic[0], TRACELOG_HDR_MAGIC, sizeof(pHdr->szMagic)))
    {
        /* Check endianess. */
        if (pHdr->u32Endianess == TRACELOG_HDR_ENDIANESS)
            pThis->fConvEndianess = false;
        else if (RT_BSWAP_U32(pHdr->u32Endianess) == TRACELOG_HDR_ENDIANESS)
        {
            pThis->fConvEndianess = true;
            rtTraceLogRdrHdrEndianessConv(pHdr);
        }
        else
            rc = VERR_TRACELOG_READER_MALFORMED_LOG;

        if (RT_SUCCESS(rc))
        {
            Assert(pHdr->u32Endianess == TRACELOG_HDR_ENDIANESS);

            /* Enforce strict limits to avoid exhausting memory. */
            if (   pHdr->u32Version == TRACELOG_VERSION
                && pHdr->cbStrDesc < _1K
                && pHdr->cbTypePtr <= 8
                && (pHdr->cbTypeSize == 8 || pHdr->cbTypeSize == 4))
            {
                pThis->u64TsStart   = pHdr->u64TsStart;
                pThis->cbTypePtr    = pHdr->cbTypePtr;
                pThis->cbTypeSize   = pHdr->cbTypeSize;
                pThis->cchDesc      = pHdr->cbStrDesc;
                pThis->cEvtDescsMax = 10;

                /* Allocate array to hold event descriptors later on. */
                pThis->papEvtDescs = (PRTTRACELOGRDREVTDESC *)RTMemAllocZ(pThis->cEvtDescsMax * sizeof(PRTTRACELOGRDREVTDESC));
                if (RT_LIKELY(pThis->papEvtDescs))
                {
                    /* Switch to the next state. */
                    if (pHdr->cbStrDesc)
                        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_HDR_DESC, pHdr->cbStrDesc);
                    else
                        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_MAGIC, TRACELOG_MAGIC_SZ);

                    if (RT_SUCCESS(rc))
                    {
                        *penmEvt = RTTRACELOGRDRPOLLEVT_HDR_RECVD;
                        *pfContinuePoll = false;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_TRACELOG_READER_LOG_UNSUPPORTED;
        }
    }
    else
        rc = VERR_TRACELOG_READER_MALFORMED_LOG;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received log description.}
 */
static DECLCALLBACK(int) rtTraceLogRdrHdrDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                   bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    char *pszDesc = (char *)pThis->pbScratch;

    RTStrPurgeEncoding(pszDesc);
    pThis->pszDesc = RTStrCacheEnterN(pThis->hStrCache, pszDesc, pThis->cchDesc);
    if (pThis->pszDesc)
        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_MAGIC, TRACELOG_MAGIC_SZ);
    else
        rc = VERR_NO_STR_MEMORY;
    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received magic.}
 */
static DECLCALLBACK(int) rtTraceLogRdrMagicRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                 bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    char *pszMagic = (char *)pThis->pbScratch;

    if (!memcmp(pszMagic, TRACELOG_EVTDESC_MAGIC, TRACELOG_MAGIC_SZ))
        rc = rtTraceLogRdrStateAdvanceEx(pThis, RTTRACELOGRDRSTATE_RECV_EVT_DESC,
                                         sizeof(TRACELOGEVTDESC), TRACELOG_MAGIC_SZ);
    else if (!memcmp(pszMagic, TRACELOG_EVT_MAGIC, TRACELOG_MAGIC_SZ))
        rc = rtTraceLogRdrStateAdvanceEx(pThis, RTTRACELOGRDRSTATE_RECV_EVT_MARKER,
                                         sizeof(TRACELOGEVT), TRACELOG_MAGIC_SZ);

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event descriptor.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                   bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    PTRACELOGEVTDESC pEvtDesc = (PTRACELOGEVTDESC)pThis->pbScratch;
    if (pThis->fConvEndianess)
        rtTraceLogRdrEvtDescEndianessConv(pEvtDesc);

    if (   !memcmp(&pEvtDesc->szMagic[0], TRACELOG_EVTDESC_MAGIC, sizeof(pEvtDesc->szMagic))
        && pEvtDesc->u32Id == pThis->cEvtDescsCur
        && (pEvtDesc->cbStrId >= 1 && pEvtDesc->cbStrId < 128)
        && pEvtDesc->cbStrDesc < _1K
        && pEvtDesc->cEvtItems < 128)
    {
        RTTRACELOGEVTSEVERITY enmSeverity = rtTraceLogRdrConvSeverity(pEvtDesc->u32Severity);
        if (RT_LIKELY(enmSeverity != RTTRACELOGEVTSEVERITY_INVALID))
        {
            /* Allocate new internal event descriptor state. */
            size_t cbEvtDesc = RT_UOFFSETOF_DYN(RTTRACELOGRDREVTDESC, aEvtItemDesc[pEvtDesc->cEvtItems]);
            PRTTRACELOGRDREVTDESC pEvtDescInt = (PRTTRACELOGRDREVTDESC)RTMemAllocZ(cbEvtDesc);
            if (RT_LIKELY(pEvtDescInt))
            {
                pEvtDescInt->cbStrId               = pEvtDesc->cbStrId;
                pEvtDescInt->cbStrDesc             = pEvtDesc->cbStrDesc;
                pEvtDescInt->EvtDesc.enmSeverity   = enmSeverity;
                pEvtDescInt->EvtDesc.cEvtItems     = pEvtDesc->cEvtItems;
                pEvtDescInt->EvtDesc.paEvtItemDesc = &pEvtDescInt->aEvtItemDesc[0];

                pThis->pEvtDescCur = pEvtDescInt;
                rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_DESC_ID, pEvtDescInt->cbStrId);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_TRACELOG_READER_MALFORMED_LOG;
    }
    else
        rc = VERR_TRACELOG_READER_MALFORMED_LOG;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event descriptor ID.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtDescIdRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                     bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);

    int rc = VINF_SUCCESS;
    pThis->pEvtDescCur->EvtDesc.pszId = RTStrCacheEnterN(pThis->hStrCache, (const char *)pThis->pbScratch,
                                                         pThis->pEvtDescCur->cbStrId);
    if (RT_LIKELY(pThis->pEvtDescCur->EvtDesc.pszId))
    {
        if (pThis->pEvtDescCur->cbStrDesc)
            rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_DESC_DESC, pThis->pEvtDescCur->cbStrDesc);
        else if (pThis->pEvtDescCur->EvtDesc.cEvtItems > 0)
            rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC, sizeof(TRACELOGEVTITEMDESC));
        else
            rc = rtTraceLogRdrEvtDescComplete(pThis, pThis->pEvtDescCur);
    }
    else
        rc = VERR_NO_STR_MEMORY;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event descriptor description.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtDescDescriptionRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                              bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    pThis->pEvtDescCur->EvtDesc.pszDesc = RTStrCacheEnterN(pThis->hStrCache, (const char *)pThis->pbScratch,
                                                           pThis->pEvtDescCur->cbStrDesc);
    if (RT_LIKELY(pThis->pEvtDescCur->EvtDesc.pszDesc))
    {
        if (pThis->pEvtDescCur->EvtDesc.cEvtItems > 0)
            rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC, sizeof(TRACELOGEVTITEMDESC));
        else
            rc = rtTraceLogRdrEvtDescComplete(pThis, pThis->pEvtDescCur);
    }
    else
        rc = VERR_NO_STR_MEMORY;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event item descriptor.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                       bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);
    int rc = VINF_SUCCESS;
    PTRACELOGEVTITEMDESC pEvtItemDesc = (PTRACELOGEVTITEMDESC)pThis->pbScratch;
    if (pThis->fConvEndianess)
        rtTraceLogRdrEvtItemDescEndianessConv(pEvtItemDesc);

    if (   !memcmp(&pEvtItemDesc->szMagic[0], TRACELOG_EVTITEMDESC_MAGIC, sizeof(pEvtItemDesc->szMagic))
        && (pEvtItemDesc->cbStrName >= 1 && pEvtItemDesc->cbStrName < 128)
        && pEvtItemDesc->cbStrDesc < _1K
        && pEvtItemDesc->cbRawData < _1M)
    {
        RTTRACELOGTYPE enmType = rtTraceLogRdrConvType(pEvtItemDesc->u32Type);
        if (RT_LIKELY(enmType != RTTRACELOGTYPE_INVALID))
        {
            PRTTRACELOGEVTITEMDESC pEvtDesc = &pThis->pEvtDescCur->aEvtItemDesc[pThis->pEvtDescCur->idxEvtItemCur];

            pThis->pEvtDescCur->cbStrItemName = pEvtItemDesc->cbStrName;
            pThis->pEvtDescCur->cbStrItemDesc = pEvtItemDesc->cbStrDesc;

            pEvtDesc->enmType   = enmType;
            pEvtDesc->cbRawData = pEvtItemDesc->cbRawData;
            rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC_NAME, pEvtItemDesc->cbStrName);
        }
        else
            rc = VERR_TRACELOG_READER_MALFORMED_LOG;
    }
    else
        rc = VERR_TRACELOG_READER_MALFORMED_LOG;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event item descriptor name.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescNameRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                           bool *pfContinuePoll)
{
    int rc = VINF_SUCCESS;
    PRTTRACELOGEVTITEMDESC pEvtDesc = &pThis->pEvtDescCur->aEvtItemDesc[pThis->pEvtDescCur->idxEvtItemCur];
    pEvtDesc->pszName = RTStrCacheEnterN(pThis->hStrCache, (const char *)pThis->pbScratch, pThis->pEvtDescCur->cbStrItemName);
    if (RT_LIKELY(pEvtDesc->pszName))
    {
        if (pThis->pEvtDescCur->cbStrItemDesc)
            rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_ITEM_DESC_DESC, pThis->pEvtDescCur->cbStrItemDesc);
        else
            rc = rtTraceLogRdrEvtItemDescComplete(pThis, penmEvt, pfContinuePoll);
    }
    else
        rc = VERR_NO_STR_MEMORY;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event item description.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtItemDescDescriptionRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                                  bool *pfContinuePoll)
{
    int rc = VINF_SUCCESS;
    PRTTRACELOGEVTITEMDESC pEvtDesc = &pThis->pEvtDescCur->aEvtItemDesc[pThis->pEvtDescCur->idxEvtItemCur];
    pEvtDesc->pszDesc = RTStrCacheEnterN(pThis->hStrCache, (const char *)pThis->pbScratch, pThis->pEvtDescCur->cbStrItemDesc);
    if (RT_LIKELY(pEvtDesc->pszDesc))
        rc = rtTraceLogRdrEvtItemDescComplete(pThis, penmEvt, pfContinuePoll);
    else
        rc = VERR_NO_STR_MEMORY;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles a received event marker.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtMarkerRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                     bool *pfContinuePoll)
{
    int rc = VINF_SUCCESS;
    PTRACELOGEVT pEvtStrm = (PTRACELOGEVT)pThis->pbScratch;
    if (pThis->fConvEndianess)
        rtTraceLogRdrEvtEndianessConv(pEvtStrm);

    if (   (pEvtStrm->u64SeqNo == pThis->u64SeqNoLast + 1)
        && !(pEvtStrm->fFlags & ~TRACELOG_EVT_F_VALID)
        && pEvtStrm->u32EvtDescId < pThis->cEvtDescsCur)
    {
        PRTTRACELOGRDREVTDESC pEvtDesc = pThis->papEvtDescs[pEvtStrm->u32EvtDescId];
        if (   (   !pEvtDesc->cRawDataNonStatic
                && pEvtStrm->cbEvtData == pEvtDesc->cbEvtData)
            || (   pEvtDesc->cRawDataNonStatic
                && pEvtStrm->cbEvtData >= pEvtDesc->cbEvtData
                && pEvtStrm->cRawEvtDataSz == pEvtDesc->cRawDataNonStatic))
        {
            size_t cbEvt = RT_UOFFSETOF_DYN(RTTRACELOGRDREVTINT, abEvtData[pEvtStrm->cbEvtData]);
            cbEvt += pEvtDesc->cRawDataNonStatic * sizeof(size_t);
            PRTTRACELOGRDREVTINT pEvt = (PRTTRACELOGRDREVTINT)RTMemAllocZ(cbEvt);
            if (RT_LIKELY(pEvt))
            {
                pEvt->pRdr        = pThis;
                pEvt->u64SeqNo    = pEvtStrm->u64SeqNo;
                pEvt->u64Ts       = pEvtStrm->u64Ts;
                pEvt->pEvtDesc    = pEvtDesc;
                pEvt->cbEvtData   = pEvtStrm->cbEvtData;
                pEvt->pacbRawData = pEvtDesc->cRawDataNonStatic ? (size_t *)&pEvt->abEvtData[pEvtStrm->cbEvtData] : NULL;
                /** @todo Group handling and parenting. */

                size_t cbEvtDataRecv = pEvtStrm->cRawEvtDataSz * pThis->cbTypeSize + pEvtStrm->cbEvtData;
                if (cbEvtDataRecv)
                {
                    pThis->pEvtCur = pEvt;
                    rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_EVT_DATA, cbEvtDataRecv);
                }
                else
                {
                    pThis->pEvtCur = NULL;
                    RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
                    pThis->u64SeqNoLast = pEvt->u64SeqNo;
                    RTListAppend(&pThis->LstEvts, &pEvt->NdGlob);
                    RTSemMutexRelease(pThis->hMtx);
                    *penmEvt = RTTRACELOGRDRPOLLEVT_TRACE_EVENT_RECVD;
                    *pfContinuePoll = false;
                    rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_MAGIC, TRACELOG_MAGIC_SZ);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_TRACELOG_READER_MALFORMED_LOG;
    }
    else
        rc = VERR_TRACELOG_READER_MALFORMED_LOG;

    return rc;
}


/**
 * @callback_method_impl{FNRTTRACELOGRDRSTATEHANDLER, Handles received event data.}
 */
static DECLCALLBACK(int) rtTraceLogRdrEvtDataRecvd(PRTTRACELOGRDRINT pThis, RTTRACELOGRDRPOLLEVT *penmEvt,
                                                   bool *pfContinuePoll)
{
    RT_NOREF(penmEvt, pfContinuePoll);

    int rc = VINF_SUCCESS;
    PRTTRACELOGRDREVTINT pEvt = pThis->pEvtCur;
    PCRTTRACELOGRDREVTDESC pEvtDesc = pEvt->pEvtDesc;
    uint8_t *pbData = pThis->pbScratch;
    size_t cbRawDataNonStatic = 0;

    /* Retrieve any raw data size indicators first. */
    for (unsigned i = 0; i < pEvtDesc->cRawDataNonStatic; i++)
    {
        size_t cb = 0;
        if (pThis->cbTypeSize == 4)
        {
            if (pThis->fConvEndianess)
                cb = RT_BSWAP_U32(*(uint32_t *)pbData);
            else
                cb = *(uint32_t *)pbData;
            pbData += 4;
        }
        else if (pThis->cbTypeSize == 8)
        {
            if (pThis->fConvEndianess)
                cb = RT_BSWAP_U64(*(uint64_t *)pbData);
            else
                cb = *(uint64_t *)pbData;
            pbData += 8;
        }
        else
            AssertMsgFailed(("Invalid size_t size %u\n", pThis->cbTypeSize));

        pEvt->pacbRawData[i] = cb;
        cbRawDataNonStatic += cb;
    }

    /* Verify that sizes add up. */
    if (pEvt->cbEvtData == pEvtDesc->cbEvtData + cbRawDataNonStatic)
    {
        /* Copy the data over. */
        memcpy(&pEvt->abEvtData[0], pbData, pEvt->cbEvtData);

        /* Done add event to global list and generate event. */
        pThis->pEvtCur = NULL;
        RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
        pThis->u64SeqNoLast = pEvt->u64SeqNo;
        RTListAppend(&pThis->LstEvts, &pEvt->NdGlob);
        RTSemMutexRelease(pThis->hMtx);
        *penmEvt = RTTRACELOGRDRPOLLEVT_TRACE_EVENT_RECVD;
        *pfContinuePoll = false;
        rc = rtTraceLogRdrStateAdvance(pThis, RTTRACELOGRDRSTATE_RECV_MAGIC, TRACELOG_MAGIC_SZ);
    }
    else
        rc = VERR_TRACELOG_READER_MALFORMED_LOG;

    return rc;
}


/**
 * @copydoc FNRTTRACELOGRDRSTREAM
 */
static DECLCALLBACK(int) rtTraceLogRdrFileStream(void *pvUser, void *pvBuf, size_t cbBuf, size_t *pcbRead,
                                                 RTMSINTERVAL cMsTimeout)
{
    RT_NOREF(cMsTimeout);
    RTFILE hFile = (RTFILE)pvUser;
    return RTFileRead(hFile, pvBuf, cbBuf, pcbRead);
}


/**
 * @copydoc FNRTTRACELOGSTREAMCLOSE
 */
static DECLCALLBACK(int) rtTraceLogRdrFileStreamClose(void *pvUser)
{
    RTFILE hFile = (RTFILE)pvUser;
    return RTFileClose(hFile);
}


/**
 * Returns the size of the data for the given event item descriptor.
 *
 * @returns Size in bytes for the given event item descriptor.
 * @param   pThis               The trace log rader instance.
 * @param   pEvtItemDesc        The event item descriptor.
 * @param   pacbRawData         The raw data size array for he associated event to get the size for non static raw data items.
 * @param   pidxRawData         The index into the raw data size array for the next item to use.
 */
static size_t rtTraceLogRdrEvtItemGetSz(PRTTRACELOGRDRINT pThis, PCRTTRACELOGEVTITEMDESC pEvtItemDesc,
                                        size_t *pacbRawData, unsigned *pidxRawData)
{
    size_t cbRet = 0;

    switch (pEvtItemDesc->enmType)
    {
        case RTTRACELOGTYPE_BOOL:
            cbRet = sizeof(bool);
            break;
        case RTTRACELOGTYPE_UINT8:
            cbRet = sizeof(uint8_t);
            break;
        case RTTRACELOGTYPE_INT8:
            cbRet = sizeof(int8_t);
            break;
        case RTTRACELOGTYPE_UINT16:
            cbRet = sizeof(uint16_t);
            break;
        case RTTRACELOGTYPE_INT16:
            cbRet = sizeof(int16_t);
            break;
        case RTTRACELOGTYPE_UINT32:
            cbRet = sizeof(uint32_t);
            break;
        case RTTRACELOGTYPE_INT32:
            cbRet = sizeof(int32_t);
            break;
        case RTTRACELOGTYPE_UINT64:
            cbRet = sizeof(uint64_t);
            break;
        case RTTRACELOGTYPE_INT64:
            cbRet = sizeof(int64_t);
            break;
        case RTTRACELOGTYPE_FLOAT32:
            cbRet = sizeof(float);
            break;
        case RTTRACELOGTYPE_FLOAT64:
            cbRet = sizeof(double);
            break;
        case RTTRACELOGTYPE_RAWDATA:
            if (pEvtItemDesc->cbRawData == 0)
            {
                cbRet = pacbRawData[*pidxRawData];
                pidxRawData++;
            }
            else
                cbRet = pEvtItemDesc->cbRawData;
            break;
        case RTTRACELOGTYPE_POINTER:
            cbRet = pThis->cbTypePtr;
            break;
        case RTTRACELOGTYPE_SIZE:
            cbRet = pThis->cbTypeSize;
            break;
        default:
            AssertMsgFailed(("Invalid type given %d\n", pEvtItemDesc->enmType));
    }

    return cbRet;
}


/**
 * Resolves the offset of the field with the given name returning the offset and data type.
 *
 * @returns IPRT status code.
 * @param   pEvt                The event to fetch the data for.
 * @param   pszName             The field to fetch.
 * @param   poffData            Where to store the offset to the data on success.
 * @param   pcbEvtData          Where to store the size of the size of the event data.
 * @param   ppEvtItemDesc       Where to store the event item descriptor.
 */
static int rtTraceLogRdrEvtResolveData(PCRTTRACELOGRDREVTINT pEvt, const char *pszName, uint32_t *poffData,
                                       size_t *pcbEvtData, PPCRTTRACELOGEVTITEMDESC ppEvtItemDesc)
{
    PCRTTRACELOGRDREVTDESC pEvtDesc = pEvt->pEvtDesc;
    uint32_t offData = 0;
    unsigned idxRawData = 0;

    for (unsigned i = 0; i < pEvtDesc->EvtDesc.cEvtItems; i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDesc->aEvtItemDesc[i];

        if (!RTStrCmp(pszName, pEvtItemDesc->pszName))
        {
            *poffData = offData;
            *pcbEvtData = rtTraceLogRdrEvtItemGetSz(pEvt->pRdr, pEvtItemDesc, pEvt->pacbRawData, &idxRawData);
            *ppEvtItemDesc = pEvtItemDesc;
            return VINF_SUCCESS;
        }

        offData += (uint32_t)rtTraceLogRdrEvtItemGetSz(pEvt->pRdr, pEvtItemDesc, pEvt->pacbRawData, &idxRawData);
    }

    return VERR_NOT_FOUND;
}


/**
 * Fills a value with the given event data.
 *
 * @returns IPRT status code.
 * @param   pEvt                The event to fetch the data for.
 * @param   offData             Offset the data is located in the event.
 * @param   cbData              Number of bytes for the data.
 * @param   pEvtItemDesc        The event item descriptor.
 * @param   pVal                The value to fill.
 */
static int rtTraceLogRdrEvtFillVal(PCRTTRACELOGRDREVTINT pEvt, uint32_t offData, size_t cbData, PCRTTRACELOGEVTITEMDESC pEvtItemDesc,
                                   PRTTRACELOGEVTVAL pVal)
{
    PRTTRACELOGRDRINT pThis = pEvt->pRdr;
    const uint8_t *pbData = &pEvt->abEvtData[offData];

    pVal->pItemDesc = pEvtItemDesc;
    switch (pEvtItemDesc->enmType)
    {
        case RTTRACELOGTYPE_BOOL:
            pVal->u.f = *(bool *)pbData;
            break;
        case RTTRACELOGTYPE_UINT8:
            pVal->u.u8 = *pbData;
            break;
        case RTTRACELOGTYPE_INT8:
            pVal->u.i8 = *(int8_t *)pbData;
            break;
        case RTTRACELOGTYPE_UINT16:
        {
            uint16_t u16Tmp = *(uint16_t *)pbData;
            if (pThis->fConvEndianess)
                pVal->u.u16 = RT_BSWAP_U16(u16Tmp);
            else
                pVal->u.u16 = u16Tmp;
            break;
        }
        case RTTRACELOGTYPE_INT16:
        {
            uint8_t abData[2];
            if (pThis->fConvEndianess)
            {
                abData[0] = pbData[1];
                abData[1] = pbData[0];
            }
            else
            {
                abData[0] = pbData[0];
                abData[1] = pbData[1];
            }

            pVal->u.i16 = *(int16_t *)&abData[0];
            break;
        }
        case RTTRACELOGTYPE_UINT32:
        {
            uint32_t u32Tmp = *(uint32_t *)pbData;
            if (pThis->fConvEndianess)
                pVal->u.u32 = RT_BSWAP_U32(u32Tmp);
            else
                pVal->u.u32 = u32Tmp;
            break;
        }
        case RTTRACELOGTYPE_INT32:
        {
            uint8_t abData[4];
            if (pThis->fConvEndianess)
            {
                abData[0] = pbData[3];
                abData[1] = pbData[2];
                abData[2] = pbData[1];
                abData[3] = pbData[0];
            }
            else
            {
                abData[0] = pbData[0];
                abData[1] = pbData[1];
                abData[2] = pbData[2];
                abData[3] = pbData[3];
            }

            pVal->u.i32 = *(int32_t *)&abData[0];
            break;
        }
        case RTTRACELOGTYPE_UINT64:
        {
            uint64_t u64Tmp = *(uint64_t *)pbData;
            if (pThis->fConvEndianess)
                pVal->u.u64 = RT_BSWAP_U64(u64Tmp);
            else
                pVal->u.u64 = u64Tmp;
            break;
        }
        case RTTRACELOGTYPE_INT64:
        {
            uint8_t abData[8];
            if (pThis->fConvEndianess)
            {
                abData[0] = pbData[7];
                abData[1] = pbData[6];
                abData[2] = pbData[5];
                abData[3] = pbData[4];
                abData[4] = pbData[3];
                abData[5] = pbData[2];
                abData[6] = pbData[1];
                abData[7] = pbData[0];
            }
            else
            {
                abData[0] = pbData[0];
                abData[1] = pbData[1];
                abData[2] = pbData[2];
                abData[3] = pbData[3];
                abData[4] = pbData[4];
                abData[5] = pbData[5];
                abData[6] = pbData[6];
                abData[7] = pbData[7];
            }

            pVal->u.i32 = *(int64_t *)&abData[0];
            break;
        }
        case RTTRACELOGTYPE_FLOAT32:
        {
            uint8_t abData[4];
            if (pThis->fConvEndianess)
            {
                abData[0] = pbData[3];
                abData[1] = pbData[2];
                abData[2] = pbData[1];
                abData[3] = pbData[0];
            }
            else
            {
                abData[0] = pbData[0];
                abData[1] = pbData[1];
                abData[2] = pbData[2];
                abData[3] = pbData[3];
            }

            pVal->u.f32 = *(float *)&abData[0];
            break;
        }
        case RTTRACELOGTYPE_FLOAT64:
        {
            uint8_t abData[8];
            if (pThis->fConvEndianess)
            {
                abData[0] = pbData[7];
                abData[1] = pbData[6];
                abData[2] = pbData[5];
                abData[3] = pbData[4];
                abData[4] = pbData[3];
                abData[5] = pbData[2];
                abData[6] = pbData[1];
                abData[7] = pbData[0];
            }
            else
            {
                abData[0] = pbData[0];
                abData[1] = pbData[1];
                abData[2] = pbData[2];
                abData[3] = pbData[3];
                abData[4] = pbData[4];
                abData[5] = pbData[5];
                abData[6] = pbData[6];
                abData[7] = pbData[7];
            }

            pVal->u.f64 = *(double *)&abData[0];
            break;
        }
        case RTTRACELOGTYPE_RAWDATA:
            pVal->u.RawData.pb = pbData;
            if (pEvtItemDesc->cbRawData == 0)
                pVal->u.RawData.cb = cbData;
            else
                pVal->u.RawData.cb = pEvtItemDesc->cbRawData;
            break;
        case RTTRACELOGTYPE_POINTER:
        {
            if (pThis->cbTypePtr == 4)
            {
                if (pThis->fConvEndianess)
                    pVal->u.uPtr = RT_BSWAP_U32(*(uint32_t *)pbData);
                else
                    pVal->u.uPtr = *(uint32_t *)pbData;
            }
            else if (pThis->cbTypePtr == 8)
            {
                if (pThis->fConvEndianess)
                    pVal->u.uPtr = RT_BSWAP_U64(*(uint64_t *)pbData);
                else
                    pVal->u.uPtr = *(uint64_t *)pbData;
            }
            else
                AssertMsgFailed(("Invalid pointer size %d, should not happen!\n", pThis->cbTypePtr));
            break;
        }
        case RTTRACELOGTYPE_SIZE:
        {
            if (pThis->cbTypeSize == 4)
            {
                if (pThis->fConvEndianess)
                    pVal->u.sz = RT_BSWAP_U32(*(uint32_t *)pbData);
                else
                    pVal->u.sz = *(uint32_t *)pbData;
            }
            else if (pThis->cbTypeSize == 8)
            {
                if (pThis->fConvEndianess)
                    pVal->u.sz = RT_BSWAP_U64(*(uint64_t *)pbData);
                else
                    pVal->u.sz = *(uint64_t *)pbData;
            }
            else
                AssertMsgFailed(("Invalid size_t size %d, should not happen!\n", pThis->cbTypeSize));
            break;
        }
        default:
            AssertMsgFailed(("Invalid type given %d\n", pEvtItemDesc->enmType));
    }

    return VINF_SUCCESS;
}


/**
 * Finds the mapping descriptor for the given event.
 *
 * @returns Pointer to the mapping descriptor or NULL if not found.
 * @param   paMapDesc           Pointer to the array of mapping descriptors.
 * @param   pEvt                The event to look for the matching mapping descriptor.
 */
static PCRTTRACELOGRDRMAPDESC rtTraceLogRdrMapDescFindForEvt(PCRTTRACELOGRDRMAPDESC paMapDesc, PCRTTRACELOGRDREVTINT pEvt)
{
    AssertPtrReturn(paMapDesc, NULL);
    AssertPtrReturn(pEvt, NULL);

    while (paMapDesc->pszEvtId)
    {
        if (!RTStrCmp(paMapDesc->pszEvtId, pEvt->pEvtDesc->EvtDesc.pszId))
            return paMapDesc;

        paMapDesc++;
    }

    return NULL;
}


/**
 * Fills the given event header with data from the given event using the matching mapping descriptor.
 *
 * @returns IPRT statsu code.
 * @param   pEvtHdr             The event header to fill.
 * @param   pMapDesc            The mapping descriptor to use.
 * @param   pEvt                The raw event to get the data from.
 */
static int rtTraceLogRdrMapFillEvt(PRTTRACELOGRDREVTHDR pEvtHdr, PCRTTRACELOGRDRMAPDESC pMapDesc, PCRTTRACELOGRDREVTINT pEvt)
{
    int rc = VINF_SUCCESS;

    /* Fill in the status parts. */
    pEvtHdr->pEvtMapDesc = pMapDesc;
    pEvtHdr->pEvtDesc    = &pEvt->pEvtDesc->EvtDesc;
    pEvtHdr->idSeqNo     = pEvt->u64SeqNo;
    pEvtHdr->tsEvt       = pEvt->u64Ts;
    pEvtHdr->paEvtItems  = NULL;

    /* Now the individual items if any. */
    if (pMapDesc->cEvtItems)
    {
        /* Allocate values for the items. */
        pEvtHdr->paEvtItems = (PCRTTRACELOGEVTVAL)RTMemAllocZ(pMapDesc->cEvtItems * sizeof(RTTRACELOGEVTVAL));
        if (RT_LIKELY(pEvtHdr->paEvtItems))
        {
            for (uint32_t i = 0; (i < pMapDesc->cEvtItems) && RT_SUCCESS(rc); i++)
            {
                uint32_t offData = 0;
                size_t cbData = 0;
                PCRTTRACELOGEVTITEMDESC pEvtItemDesc = NULL;
                rc = rtTraceLogRdrEvtResolveData(pEvt, pMapDesc->paMapItems[i].pszName, &offData, &cbData, &pEvtItemDesc);
                if (RT_SUCCESS(rc))
                    rc = rtTraceLogRdrEvtFillVal(pEvt, offData, cbData, pEvtItemDesc, (PRTTRACELOGEVTVAL)&pEvtHdr->paEvtItems[i]);
            }

            if (RT_FAILURE(rc))
            {
                RTMemFree((void *)pEvtHdr->paEvtItems);
                pEvtHdr->paEvtItems = NULL;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


RTDECL(int) RTTraceLogRdrCreate(PRTTRACELOGRDR phTraceLogRdr, PFNRTTRACELOGRDRSTREAM pfnStreamIn,
                                PFNRTTRACELOGSTREAMCLOSE pfnStreamClose, void *pvUser)
{
    AssertPtrReturn(phTraceLogRdr,  VERR_INVALID_POINTER);
    AssertPtrReturn(pfnStreamIn,    VERR_INVALID_POINTER);
    AssertPtrReturn(pfnStreamClose, VERR_INVALID_POINTER);
    int rc = VINF_SUCCESS;
    PRTTRACELOGRDRINT pThis = (PRTTRACELOGRDRINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        rc = RTSemMutexCreate(&pThis->hMtx);
        if (RT_SUCCESS(rc))
        {
            rc = RTStrCacheCreate(&pThis->hStrCache, "TRACELOGRDR");
            if (RT_SUCCESS(rc))
            {
                RTListInit(&pThis->LstEvts);
                pThis->u32Magic       = RTTRACELOGRDR_MAGIC;
                pThis->pfnStreamIn    = pfnStreamIn;
                pThis->pfnStreamClose = pfnStreamClose;
                pThis->pvUser         = pvUser;
                pThis->enmState       = RTTRACELOGRDRSTATE_RECV_HDR;
                pThis->fConvEndianess = false;
                pThis->pszDesc        = NULL;
                pThis->cEvtDescsCur   = 0;
                pThis->cEvtDescsMax   = 0;
                pThis->papEvtDescs    = NULL;
                pThis->pEvtDescCur    = NULL;
                pThis->u64SeqNoLast   = 0;
                pThis->cbScratch      = sizeof(TRACELOGHDR);
                pThis->offScratch     = 0;
                pThis->cbRecvLeft     = sizeof(TRACELOGHDR);
                pThis->pbScratch      = (uint8_t *)RTMemAllocZ(pThis->cbScratch);
                if (RT_LIKELY(pThis->pbScratch))
                {
                    *phTraceLogRdr = pThis;
                    return VINF_SUCCESS;
                }
                else
                    rc = VERR_NO_MEMORY;

                RTStrCacheDestroy(pThis->hStrCache);
            }

            RTSemMutexDestroy(pThis->hMtx);
        }
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTTraceLogRdrCreateFromFile(PRTTRACELOGRDR phTraceLogRdr, const char *pszFilename)
{
    AssertPtrReturn(phTraceLogRdr, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename,   VERR_INVALID_POINTER);

    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        rc = RTTraceLogRdrCreate(phTraceLogRdr, rtTraceLogRdrFileStream, rtTraceLogRdrFileStreamClose, hFile);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFile);
            RTFileDelete(pszFilename);
        }
    }

    return rc;
}


RTDECL(int) RTTraceLogRdrDestroy(RTTRACELOGRDR hTraceLogRdr)
{
    if (hTraceLogRdr == NIL_RTTRACELOGRDR)
        return VINF_SUCCESS;
    PRTTRACELOGRDRINT pThis = hTraceLogRdr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGRDR_MAGIC, VERR_INVALID_HANDLE);

    pThis->u32Magic = RTTRACELOGRDR_MAGIC_DEAD;
    int rc = pThis->pfnStreamClose(pThis->pvUser);
    AssertRC(rc);

    for (unsigned i = 0; i < pThis->cEvtDescsCur; i++)
        RTMemFree(pThis->papEvtDescs[i]);
    if (pThis->papEvtDescs)
    {
        RTMemFree(pThis->papEvtDescs);
        pThis->papEvtDescs = NULL;
    }

    if (pThis->pEvtCur)
    {
        RTMemFree(pThis->pEvtCur);
        pThis->pEvtCur = NULL;
    }

    PRTTRACELOGRDREVTINT pCur, pNext;
    RTListForEachSafe(&pThis->LstEvts, pCur, pNext, RTTRACELOGRDREVTINT, NdGlob)
    {
        RTMemFree(pCur);
    }

    RTSemMutexDestroy(pThis->hMtx);
    pThis->hMtx = NIL_RTSEMMUTEX;

    RTMemFree(pThis->pbScratch);
    pThis->pbScratch = NULL;

    RTStrCacheDestroy(pThis->hStrCache);
    pThis->hStrCache = NIL_RTSTRCACHE;

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTTraceLogRdrEvtPoll(RTTRACELOGRDR hTraceLogRdr, RTTRACELOGRDRPOLLEVT *penmEvt, RTMSINTERVAL cMsTimeout)
{
    PRTTRACELOGRDRINT pThis = hTraceLogRdr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGRDR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(penmEvt, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    bool fContinue = true;
    while (   RT_SUCCESS(rc)
           && fContinue)
    {
        size_t cbRecvd = 0;

        rc = rtTraceLogRdrStreamRead(pThis, &pThis->pbScratch[pThis->offScratch],
                                     pThis->cbRecvLeft, &cbRecvd, cMsTimeout);
        if (RT_SUCCESS(rc))
        {
            if (cbRecvd == pThis->cbRecvLeft)
            {
                /* Act according to the current state. */
                rc = g_aStateHandlers[pThis->enmState].pfn(pThis, penmEvt, &fContinue);
            }
            else
                pThis->cbRecvLeft -= cbRecvd;
        }
    }

    return rc;
}


RTDECL(int) RTTraceLogRdrQueryLastEvt(RTTRACELOGRDR hTraceLogRdr, PRTTRACELOGRDREVT phRdrEvt)
{
    PRTTRACELOGRDRINT pThis = hTraceLogRdr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGRDR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(phRdrEvt, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
    PRTTRACELOGRDREVTINT pEvt = RTListGetLast(&pThis->LstEvts, RTTRACELOGRDREVTINT, NdGlob);
    *phRdrEvt = pEvt;
    if (!pEvt)
        rc = VERR_NOT_FOUND;
    RTSemMutexRelease(pThis->hMtx);

    return rc;
}


RTDECL(int) RTTraceLogRdrQueryIterator(RTTRACELOGRDR hTraceLogRdr, PRTTRACELOGRDRIT phIt)
{
    PRTTRACELOGRDRINT pThis = hTraceLogRdr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGRDR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(phIt, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PRTTRACELOGRDRITINT pIt = (PRTTRACELOGRDRITINT)RTMemAllocZ(sizeof(*pIt));
    if (RT_LIKELY(pIt))
    {
        pIt->pRdr = pThis;
        pIt->pEvt = RTListGetFirst(&pThis->LstEvts, RTTRACELOGRDREVTINT, NdGlob);
        *phIt = pIt;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTTraceLogRdrEvtMapToStruct(RTTRACELOGRDR hTraceLogRdr, uint32_t fFlags, uint32_t cEvts,
                                        PCRTTRACELOGRDRMAPDESC paMapDesc, PCRTTRACELOGRDREVTHDR *ppaEvtHdr,
                                        uint32_t *pcEvts)
{
    RT_NOREF(fFlags);

    PRTTRACELOGRDRINT pThis = hTraceLogRdr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGRDR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(paMapDesc, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppaEvtHdr, VERR_INVALID_POINTER);
    AssertPtrReturn(pcEvts, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    uint32_t cEvtsAlloc = cEvts != UINT32_MAX ? cEvts : _4K;
    PRTTRACELOGRDREVTHDR paEvtHdr = (PRTTRACELOGRDREVTHDR)RTMemAllocZ(cEvtsAlloc * sizeof(*paEvtHdr));
    if (RT_LIKELY(paEvtHdr))
    {
        uint32_t cEvtsRecv = 0;

        while (   RT_SUCCESS(rc)
               && cEvtsRecv < cEvts)
        {
            RTTRACELOGRDRPOLLEVT enmEvt = RTTRACELOGRDRPOLLEVT_INVALID;
            rc = RTTraceLogRdrEvtPoll(pThis, &enmEvt, 0 /*cMsTimeout*/);
            if (   RT_SUCCESS(rc)
                && enmEvt == RTTRACELOGRDRPOLLEVT_TRACE_EVENT_RECVD)
            {
                /* Find the mapping descriptor. */
                PRTTRACELOGRDREVTINT pEvt = NULL;
                rc = RTTraceLogRdrQueryLastEvt(hTraceLogRdr, &pEvt);
                if (RT_SUCCESS(rc))
                {
                    PCRTTRACELOGRDRMAPDESC pMapDesc = rtTraceLogRdrMapDescFindForEvt(paMapDesc, pEvt);
                    if (pMapDesc)
                    {
                        if (cEvtsRecv == cEvtsAlloc)
                        {
                            Assert(cEvts == UINT32_MAX);
                            PRTTRACELOGRDREVTHDR paEvtHdrNew = (PRTTRACELOGRDREVTHDR)RTMemRealloc(paEvtHdr, (cEvtsAlloc + _4K) * sizeof(*paEvtHdr));
                            if (RT_LIKELY(paEvtHdrNew))
                            {
                                paEvtHdr = paEvtHdrNew;
                                cEvtsAlloc += _4K;
                            }
                            else
                                rc = VERR_NO_MEMORY;
                        }

                        if (RT_SUCCESS(rc))
                            rc = rtTraceLogRdrMapFillEvt(&paEvtHdr[cEvtsRecv++], pMapDesc, pEvt);
                        cEvtsRecv++;
                    }
                    else
                        rc = VERR_NOT_FOUND;
                }
            }
        }

        if (RT_SUCCESS(rc))
        {
            *ppaEvtHdr = paEvtHdr;
            *pcEvts    = cEvtsRecv;
        }
        else
            RTTraceLogRdrEvtMapFree(paEvtHdr, cEvtsRecv);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(void) RTTraceLogRdrEvtMapFree(PCRTTRACELOGRDREVTHDR paEvtHdr, uint32_t cEvts)
{
    for (uint32_t i = 0; i < cEvts; i++)
    {
        if (paEvtHdr[i].paEvtItems)
            RTMemFree((void *)paEvtHdr[i].paEvtItems);
    }

    RTMemFree((void *)paEvtHdr);
}


RTDECL(void) RTTraceLogRdrIteratorFree(RTTRACELOGRDRIT hIt)
{
    PRTTRACELOGRDRITINT pIt = hIt;
    AssertPtrReturnVoid(pIt);

    RTMemFree(pIt);
}


RTDECL(int) RTTraceLogRdrIteratorNext(RTTRACELOGRDRIT hIt)
{
    PRTTRACELOGRDRITINT pIt = hIt;
    AssertPtrReturn(pIt, VERR_INVALID_HANDLE);

    if (!pIt->pEvt)
        return VERR_TRACELOG_READER_ITERATOR_END;

    int rc = VINF_SUCCESS;
    PRTTRACELOGRDREVTINT pEvtNext = RTListGetNext(&pIt->pRdr->LstEvts, pIt->pEvt, RTTRACELOGRDREVTINT, NdGlob);

    if (pEvtNext)
        pIt->pEvt = pEvtNext;
    else
        rc = VERR_TRACELOG_READER_ITERATOR_END;

    return rc;
}


RTDECL(int) RTTraceLogRdrIteratorQueryEvent(RTTRACELOGRDRIT hIt, PRTTRACELOGRDREVT phRdrEvt)
{
    PRTTRACELOGRDRITINT pIt = hIt;
    AssertPtrReturn(pIt, VERR_INVALID_HANDLE);
    AssertPtrReturn(phRdrEvt, VERR_INVALID_POINTER);

    *phRdrEvt = pIt->pEvt;
    return VINF_SUCCESS;
}


RTDECL(uint64_t) RTTraceLogRdrEvtGetSeqNo(RTTRACELOGRDREVT hRdrEvt)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, 0);

    return pEvt->u64SeqNo;
}


RTDECL(uint64_t) RTTraceLogRdrEvtGetTs(RTTRACELOGRDREVT hRdrEvt)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, 0);

    return pEvt->u64Ts;
}


RTDECL(bool) RTTraceLogRdrEvtIsGrouped(RTTRACELOGRDREVT hRdrEvt)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, false);

    return pEvt->idGrp != 0;
}


RTDECL(PCRTTRACELOGEVTDESC) RTTraceLogRdrEvtGetDesc(RTTRACELOGRDREVT hRdrEvt)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, NULL);

    return &pEvt->pEvtDesc->EvtDesc;
}


RTDECL(int) RTTraceLogRdrEvtQueryVal(RTTRACELOGRDREVT hRdrEvt, const char *pszName, PRTTRACELOGEVTVAL pVal)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, VERR_INVALID_HANDLE);

    uint32_t offData = 0;
    size_t cbData = 0;
    PCRTTRACELOGEVTITEMDESC pEvtItemDesc = NULL;
    int rc = rtTraceLogRdrEvtResolveData(pEvt, pszName, &offData, &cbData, &pEvtItemDesc);
    if (RT_SUCCESS(rc))
        rc = rtTraceLogRdrEvtFillVal(pEvt, offData, cbData, pEvtItemDesc, pVal);
    return rc;
}


RTDECL(int) RTTraceLogRdrEvtFillVals(RTTRACELOGRDREVT hRdrEvt, unsigned idxItemStart, PRTTRACELOGEVTVAL paVals,
                                     unsigned cVals, unsigned *pcVals)
{
    PRTTRACELOGRDREVTINT pEvt = hRdrEvt;
    AssertPtrReturn(pEvt, VERR_INVALID_HANDLE);

    PCRTTRACELOGRDREVTDESC pEvtDesc = pEvt->pEvtDesc;
    AssertReturn(idxItemStart < pEvtDesc->EvtDesc.cEvtItems, VERR_INVALID_PARAMETER);

    /* Advance to the item the caller wants to fill in. */
    uint32_t offData = 0;
    unsigned idxRawData = 0;

    for (unsigned i = 0; i < idxItemStart; i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDesc->aEvtItemDesc[i];
        offData += (uint32_t)rtTraceLogRdrEvtItemGetSz(pEvt->pRdr, pEvtItemDesc, pEvt->pacbRawData, &idxRawData);
    }

    int rc = VINF_SUCCESS;
    unsigned idxItemEnd = RT_MIN(idxItemStart + cVals, pEvtDesc->EvtDesc.cEvtItems);
    for (unsigned i = idxItemStart; i < idxItemEnd && RT_SUCCESS(rc); i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDesc->aEvtItemDesc[i];
        size_t cbData = rtTraceLogRdrEvtItemGetSz(pEvt->pRdr, pEvtItemDesc, pEvt->pacbRawData, &idxRawData);

        rc = rtTraceLogRdrEvtFillVal(pEvt, offData, cbData, pEvtItemDesc, &paVals[i - idxItemStart]);
        offData += (uint32_t)cbData;
    }

    *pcVals = idxItemEnd - idxItemStart;

    return rc;
}

