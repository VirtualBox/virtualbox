/* $Id: tracelogwriter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Trace log writer.
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


#include <iprt/avl.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/tcp.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Trace log writer internal event descriptor.
 */
typedef struct RTTRACELOGWREVTDESC
{
    /** AVL node core data */
    AVLPVNODECORE               Core;
    /** The ID associated with this event descriptor. */
    uint32_t                    u32Id;
    /** Overall size of the event data not counting variable raw data items. */
    size_t                      cbEvtData;
    /** Number of non static raw binary items in the descriptor. */
    uint32_t                    cRawDataNonStatic;
    /** Pointer to the scratch event data buffer when adding events. */
    uint8_t                     *pbEvt;
    /** Embedded event descriptor. */
    RTTRACELOGEVTDESC           EvtDesc;
    /** Array of event item descriptors, variable in size. */
    RTTRACELOGEVTITEMDESC       aEvtItemDesc[1];
} RTTRACELOGWREVTDESC;
/** Pointer to internal trace log writer event descriptor. */
typedef RTTRACELOGWREVTDESC *PRTTRACELOGWREVTDESC;
/** Pointer to const internal trace log writer event descriptor. */
typedef const RTTRACELOGWREVTDESC *PCRTTRACELOGWREVTDESC;


/**
 * Trace log writer instance data.
 */
typedef struct RTTRACELOGWRINT
{
    /** Magic for identification. */
    uint32_t                    u32Magic;
    /** Stream out callback. */
    PFNRTTRACELOGWRSTREAM       pfnStreamOut;
    /** Stream close callback .*/
    PFNRTTRACELOGSTREAMCLOSE    pfnStreamClose;
    /** Opaque user data passed to the stream callback. */
    void                        *pvUser;
    /** Mutex protecting the structure. */
    RTSEMMUTEX                  hMtx;
    /** Next sequence number to use. */
    volatile uint64_t           u64SeqNoNext;
    /** AVL tree root for event descriptor lookups. */
    AVLPVTREE                   pTreeEvtDescs;
    /** Number of event descriptors known. */
    uint32_t                    cEvtDescs;
} RTTRACELOGWRINT;
/** Pointer to a trace log writer instance. */
typedef RTTRACELOGWRINT *PRTTRACELOGWRINT;


/**
 * The TCP server/client state.
 */
typedef struct RTTRACELOGWRTCP
{
    /** Flag whether this is a server or client instance. */
    bool                        fIsServer;
    /** The TCP socket handle for the connection. */
    RTSOCKET                    hSock;
    /** The TCP server. */
    PRTTCPSERVER                pTcpSrv;
} RTTRACELOGWRTCP;
/** Pointer to a TCP server/client state. */
typedef RTTRACELOGWRTCP *PRTTRACELOGWRTCP;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/**
 * Returns the size required for the internal event descriptor representation.
 *
 * @returns Number of bytes required.
 * @param   pEvtDesc            Pointer to the external descriptor.
 */
static size_t rtTraceLogWrtEvtDescGetSz(PCRTTRACELOGEVTDESC pEvtDesc)
{
    size_t cbAlloc = RT_UOFFSETOF_DYN(RTTRACELOGWREVTDESC, aEvtItemDesc[pEvtDesc->cEvtItems]);

    cbAlloc += strlen(pEvtDesc->pszId) + 1;
    if (pEvtDesc->pszDesc)
        cbAlloc += strlen(pEvtDesc->pszDesc) + 1;
    for (unsigned i = 0; i < pEvtDesc->cEvtItems; i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDesc->paEvtItemDesc[i];

        cbAlloc += strlen(pEvtItemDesc->pszName) + 1;
        if (pEvtItemDesc->pszDesc)
            cbAlloc += strlen(pEvtItemDesc->pszDesc) + 1;
    }

    return cbAlloc;
}


/**
 * Copies a string into a supplied buffer assigning the start to the given string pointer.
 *
 * @returns Pointer to the memory after the destination buffer holding the string.
 * @param   ppsz                Where to store the pointer to the start of the string.
 * @param   pszTo               Where to copy the string including the temrinator to.
 * @param   pszFrom             The string to copy.
 */
DECLINLINE(char *) rtTraceLogWrCopyStr(const char **ppsz, char *pszTo, const char *pszFrom)
{
    *ppsz = pszTo;
    size_t cchCopy = strlen(pszFrom) + 1;
    memcpy(pszTo, pszFrom, cchCopy);

    return pszTo + cchCopy;
}


/**
 * Converts the type enum to the size of the the event item data in bytes.
 *
 * @returns Event item data size in bytes.
 * @param   pEvtItemDesc        The event item descriptor.
 */
static size_t rtTraceLogWrGetEvtItemDataSz(PCRTTRACELOGEVTITEMDESC pEvtItemDesc)
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
            cb = sizeof(uintptr_t);
            break;
        }
        case RTTRACELOGTYPE_SIZE:
        {
            cb = sizeof(size_t);
            break;
        }
        default:
            AssertMsgFailed(("Invalid type %d\n", pEvtItemDesc->enmType));
    }

    return cb;
}


/**
 * Converts API severity enum to the stream representation.
 *
 * @returns Stream representation of the severity.
 * @param   enmSeverity         The API severity.
 */
static uint32_t rtTraceLogWrConvSeverityToStream(RTTRACELOGEVTSEVERITY enmSeverity)
{
    switch (enmSeverity)
    {
        case RTTRACELOGEVTSEVERITY_INFO:
            return TRACELOG_EVTDESC_SEVERITY_INFO;
        case RTTRACELOGEVTSEVERITY_WARNING:
            return TRACELOG_EVTDESC_SEVERITY_WARNING;
        case RTTRACELOGEVTSEVERITY_ERROR:
            return TRACELOG_EVTDESC_SEVERITY_ERROR;
        case RTTRACELOGEVTSEVERITY_FATAL:
            return TRACELOG_EVTDESC_SEVERITY_FATAL;
        case RTTRACELOGEVTSEVERITY_DEBUG:
            return TRACELOG_EVTDESC_SEVERITY_DEBUG;
        default:
            AssertMsgFailed(("Invalid severity %d\n", enmSeverity));
    }

    /* Should not happen. */
    return TRACELOG_EVTDESC_SEVERITY_FATAL;
}


/**
 * Converts API type enum to the stream representation.
 *
 * @returns Stream representation of the type.
 * @param   enmType             The API type.
 */
static uint32_t rtTraceLogWrConvTypeToStream(RTTRACELOGTYPE enmType)
{
    switch (enmType)
    {
        case RTTRACELOGTYPE_BOOL:
            return TRACELOG_EVTITEMDESC_TYPE_BOOL;
        case RTTRACELOGTYPE_UINT8:
            return TRACELOG_EVTITEMDESC_TYPE_UINT8;
        case RTTRACELOGTYPE_INT8:
            return TRACELOG_EVTITEMDESC_TYPE_INT8;
        case RTTRACELOGTYPE_UINT16:
            return TRACELOG_EVTITEMDESC_TYPE_UINT16;
        case RTTRACELOGTYPE_INT16:
            return TRACELOG_EVTITEMDESC_TYPE_INT16;
        case RTTRACELOGTYPE_UINT32:
            return TRACELOG_EVTITEMDESC_TYPE_UINT32;
        case RTTRACELOGTYPE_INT32:
            return TRACELOG_EVTITEMDESC_TYPE_INT32;
        case RTTRACELOGTYPE_UINT64:
            return TRACELOG_EVTITEMDESC_TYPE_UINT64;
        case RTTRACELOGTYPE_INT64:
            return TRACELOG_EVTITEMDESC_TYPE_INT64;
        case RTTRACELOGTYPE_FLOAT32:
            return TRACELOG_EVTITEMDESC_TYPE_FLOAT32;
        case RTTRACELOGTYPE_FLOAT64:
            return TRACELOG_EVTITEMDESC_TYPE_FLOAT64;
        case RTTRACELOGTYPE_RAWDATA:
            return TRACELOG_EVTITEMDESC_TYPE_RAWDATA;
        case RTTRACELOGTYPE_POINTER:
            return TRACELOG_EVTITEMDESC_TYPE_POINTER;
        case RTTRACELOGTYPE_SIZE:
            return TRACELOG_EVTITEMDESC_TYPE_SIZE;
        default:
            AssertMsgFailed(("Invalid type %d\n", enmType));
    }

    /* Should not happen. */
    return RTTRACELOGTYPE_RAWDATA;
}


/**
 * Initializes the internal representation of the event descriptor from the given one.
 *
 * @returns Pointer to the internal instance of the event descriptor.
 *          NULL if out of memory.
 * @param   pEvtDesc            Pointer to the external descriptor.
 */
static PRTTRACELOGWREVTDESC rtTraceLogWrEvtDescInit(PCRTTRACELOGEVTDESC pEvtDesc)
{
    size_t cbAlloc = rtTraceLogWrtEvtDescGetSz(pEvtDesc);
    size_t cbEvtData = 0;
    PRTTRACELOGWREVTDESC pEvtDescInt = (PRTTRACELOGWREVTDESC)RTMemAllocZ(cbAlloc);
    if (RT_LIKELY(pEvtDescInt))
    {
        char *pszStrSpace = (char *)&pEvtDescInt->aEvtItemDesc[pEvtDesc->cEvtItems]; /* Get space for strings after the descriptor. */

        pEvtDescInt->EvtDesc.enmSeverity   = pEvtDesc->enmSeverity;
        pEvtDescInt->EvtDesc.cEvtItems     = pEvtDesc->cEvtItems;
        pEvtDescInt->EvtDesc.paEvtItemDesc = &pEvtDescInt->aEvtItemDesc[0];

        /* Copy ID and optional description over. */
        pszStrSpace = rtTraceLogWrCopyStr(&pEvtDescInt->EvtDesc.pszId, pszStrSpace, pEvtDesc->pszId);
        if (pEvtDesc->pszDesc)
            pszStrSpace = rtTraceLogWrCopyStr(&pEvtDescInt->EvtDesc.pszDesc, pszStrSpace, pEvtDesc->pszDesc);

        /* Go through the event item descriptors and initialize them too. */
        for (unsigned i = 0; i < pEvtDesc->cEvtItems; i++)
        {
            PCRTTRACELOGEVTITEMDESC pEvtItemDescFrom = &pEvtDesc->paEvtItemDesc[i];
            PRTTRACELOGEVTITEMDESC pEvtItemDescTo = &pEvtDescInt->aEvtItemDesc[i];

            pEvtItemDescTo->enmType   = pEvtItemDescFrom->enmType;
            pEvtItemDescTo->cbRawData = pEvtItemDescFrom->cbRawData;

            cbEvtData += rtTraceLogWrGetEvtItemDataSz(pEvtItemDescFrom);
            if (   pEvtItemDescTo->enmType == RTTRACELOGTYPE_RAWDATA
                && !pEvtItemDescFrom->cbRawData)
                pEvtDescInt->cRawDataNonStatic++;

            pszStrSpace = rtTraceLogWrCopyStr(&pEvtItemDescTo->pszName, pszStrSpace, pEvtItemDescFrom->pszName);
            if (pEvtItemDescFrom->pszDesc)
                pszStrSpace = rtTraceLogWrCopyStr(&pEvtItemDescTo->pszDesc, pszStrSpace, pEvtItemDescFrom->pszDesc);
        }

        pEvtDescInt->cbEvtData = cbEvtData;
        if (cbEvtData)
        {
            pEvtDescInt->pbEvt = (uint8_t *)RTMemAllocZ(cbEvtData);
            if (!pEvtDescInt->pbEvt)
            {
                RTMemFree(pEvtDescInt);
                pEvtDescInt = NULL;
            }
        }
    }

    return pEvtDescInt;
}


/**
 * Wrapper around the stream callback.
 *
 * @returns IPRT status code returned by the stream callback.
 * @param   pThis               The trace log writer instance.
 * @param   pvBuf               The data to stream.
 * @param   cbBuf               Number of bytes to stream.
 */
DECLINLINE(int) rtTraceLogWrStream(PRTTRACELOGWRINT pThis, const void *pvBuf, size_t cbBuf)
{
    return pThis->pfnStreamOut(pThis->pvUser, pvBuf, cbBuf, NULL);
}


/**
 * Initializes a given event structure.
 *
 * @returns Total number of bytes for the event data associated with this event.
 * @param   pEvt                Pointer to the event structure to initialise.
 * @param   pEvtDescInt         The internal event descriptor to format the data accordingly to.
 * @param   fFlags              Flags to use for this event.
 * @param   uGrpId              The group ID to identify grouped events.
 * @param   uParentGrpId        The parent group ID.
 * @param   pacbRawData         Array of raw data size indicators.
 */
DECLINLINE(size_t) rtTraceLogWrEvtInit(PTRACELOGEVT pEvt,
                                       PRTTRACELOGWREVTDESC pEvtDescInt, uint32_t fFlags,
                                       RTTRACELOGEVTGRPID uGrpId, RTTRACELOGEVTGRPID uParentGrpId,
                                       size_t *pacbRawData)
{
    uint32_t cbEvtData = (uint32_t)pEvtDescInt->cbEvtData;
    for (unsigned i = 0; i < pEvtDescInt->cRawDataNonStatic; i++)
        cbEvtData += (uint32_t)pacbRawData[i];

    uint32_t fEvtFlags = 0;
    if (fFlags & RTTRACELOG_WR_ADD_EVT_F_GRP_START)
        fEvtFlags |= TRACELOG_EVT_F_GRP_START;
    if (fFlags & RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH)
        fEvtFlags |= TRACELOG_EVT_F_GRP_END;

    memcpy(&pEvt->szMagic[0], TRACELOG_EVT_MAGIC, sizeof(pEvt->szMagic));
    pEvt->u64Ts             = RTTimeNanoTS();
    pEvt->u64EvtGrpId       = uGrpId;
    pEvt->u64EvtParentGrpId = uParentGrpId;
    pEvt->fFlags            = fEvtFlags;
    pEvt->u32EvtDescId      = pEvtDescInt->u32Id;
    pEvt->cbEvtData         = cbEvtData;
    pEvt->cRawEvtDataSz     = pEvtDescInt->cRawDataNonStatic;

    return cbEvtData;
}


/**
 * Streams the whole event including associated data.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log writer instance.
 * @param   pEvt                Pointer to the initialised event structure.
 * @param   pvEvtData           The raw event data.
 * @param   cbEvtData           Size of the event data.
 * @param   pacbRawData         Pointer to the array of size indicators for non static
 *                              raw data in the event data stream.
 */
DECLINLINE(int) rtTraceLogWrEvtStream(PRTTRACELOGWRINT pThis, PTRACELOGEVT pEvt, const void *pvEvtData,
                                      size_t cbEvtData, size_t *pacbRawData)
{
    /** @todo Get rid of locking. */
    int rc = RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        pEvt->u64SeqNo = ASMAtomicIncU64(&pThis->u64SeqNoNext);

        /* Write the data out. */
        rc = rtTraceLogWrStream(pThis, pEvt, sizeof(TRACELOGEVT));
        if (   RT_SUCCESS(rc)
            && pEvt->cRawEvtDataSz)
            rc = rtTraceLogWrStream(pThis, pacbRawData, pEvt->cRawEvtDataSz * sizeof(size_t));
        if (   RT_SUCCESS(rc)
            && cbEvtData)
            rc = rtTraceLogWrStream(pThis, pvEvtData, cbEvtData);
        RTSemMutexRelease(pThis->hMtx);
    }

    return rc;
}


/**
 * Returns the intenral event descriptor for the given event descriptor.
 *
 * @returns Pointer to the internal event descriptor or NULL if not found.
 * @param   pThis               The trace log writer instance.
 * @param   pEvtDesc            The event descriptor to search for.
 */
DECLINLINE(PRTTRACELOGWREVTDESC) rtTraceLogWrEvtDescGetInternal(PRTTRACELOGWRINT pThis,
                                                                PCRTTRACELOGEVTDESC pEvtDesc)
{
    int rc = RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        PRTTRACELOGWREVTDESC pEvtDescInt = (PRTTRACELOGWREVTDESC)RTAvlPVGet(&pThis->pTreeEvtDescs, (void *)pEvtDesc);
        RTSemMutexRelease(pThis->hMtx);
        return pEvtDescInt;
    }

    return NULL;
}


/**
 * Initializes the trace log.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log writer instance.
 * @param   pszDesc             The description to use.
 */
static int rtTraceLogWrInit(PRTTRACELOGWRINT pThis, const char *pszDesc)
{
    /* Start by assembling the header. */
    TRACELOGHDR Hdr;

    RT_ZERO(Hdr);
    memcpy(&Hdr.szMagic[0], TRACELOG_HDR_MAGIC, sizeof(Hdr.szMagic));
    Hdr.u32Endianess = TRACELOG_HDR_ENDIANESS; /* Endianess marker. */
    Hdr.u32Version   = TRACELOG_VERSION;
    Hdr.fFlags       = 0;
    Hdr.cbStrDesc    = pszDesc ? (uint32_t)strlen(pszDesc) : 0;
    Hdr.cbTypePtr    = sizeof(uintptr_t);
    Hdr.cbTypeSize   = sizeof(size_t);
    Hdr.u64TsStart   = RTTimeNanoTS();
    int rc = rtTraceLogWrStream(pThis, &Hdr, sizeof(Hdr));
    if (   RT_SUCCESS(rc)
        && pszDesc)
        rc = rtTraceLogWrStream(pThis, pszDesc, Hdr.cbStrDesc);

    return rc;
}


static DECLCALLBACK(int) rtTraceLogWrCheckForOverlappingIds(PAVLPVNODECORE pCore, void *pvParam)
{
    PCRTTRACELOGEVTDESC pEvtDesc = (PCRTTRACELOGEVTDESC)pvParam;
    PRTTRACELOGWREVTDESC pEvtDescInt = (PRTTRACELOGWREVTDESC)pCore;

    if (!RTStrCmp(pEvtDesc->pszId, pEvtDescInt->EvtDesc.pszId))
        return VERR_ALREADY_EXISTS;

    return VINF_SUCCESS;
}


static DECLCALLBACK(int) rtTraceLogWrEvtDescsDestroy(PAVLPVNODECORE pCore, void *pvParam)
{
    PRTTRACELOGWREVTDESC pEvtDesc = (PRTTRACELOGWREVTDESC)pCore;
    RT_NOREF(pvParam);

    RTMemFree(pEvtDesc->pbEvt);
    RTMemFree(pEvtDesc);
    return VINF_SUCCESS;
}


/**
 * Adds a new event descriptor to the trace log.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log writer instance.
 * @param   pEvtDesc            The event descriptor to add.
 * @param   ppEvtDescInt        Where to store the pointer to the internal
 *                              event descriptor - optional.
 */
static int rtTraceLogWrEvtDescAdd(PRTTRACELOGWRINT pThis, PCRTTRACELOGEVTDESC pEvtDesc,
                                  PRTTRACELOGWREVTDESC *ppEvtDescInt)
{
    int rc = RTSemMutexRequest(pThis->hMtx, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        PRTTRACELOGWREVTDESC pEvtDescInt = (PRTTRACELOGWREVTDESC)RTAvlPVGet(&pThis->pTreeEvtDescs, (void *)pEvtDesc);
        if (!pEvtDescInt)
        {
            rc = RTAvlPVDoWithAll(&pThis->pTreeEvtDescs, true, rtTraceLogWrCheckForOverlappingIds, (void *)pEvtDesc);
            if (RT_SUCCESS(rc))
            {
                pEvtDescInt = rtTraceLogWrEvtDescInit(pEvtDesc);
                if (RT_LIKELY(pEvtDescInt))
                {
                    pEvtDescInt->Core.Key = (void *)pEvtDesc;
                    pEvtDescInt->u32Id    = pThis->cEvtDescs++;
                    bool fIns = RTAvlPVInsert(&pThis->pTreeEvtDescs, &pEvtDescInt->Core);
                    Assert(fIns); RT_NOREF(fIns);
                }
                else
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                {
                    TRACELOGEVTDESC EvtDesc;

                    RT_ZERO(EvtDesc);
                    memcpy(&EvtDesc.szMagic[0], TRACELOG_EVTDESC_MAGIC, sizeof(EvtDesc.szMagic));
                    EvtDesc.u32Id       = pEvtDescInt->u32Id;
                    EvtDesc.u32Severity = rtTraceLogWrConvSeverityToStream(pEvtDescInt->EvtDesc.enmSeverity);
                    EvtDesc.cbStrId     = (uint32_t)strlen(pEvtDescInt->EvtDesc.pszId);
                    EvtDesc.cbStrDesc   = pEvtDescInt->EvtDesc.pszDesc ? (uint32_t)strlen(pEvtDescInt->EvtDesc.pszDesc) : 0;
                    EvtDesc.cEvtItems   = pEvtDescInt->EvtDesc.cEvtItems;
                    rc = rtTraceLogWrStream(pThis, &EvtDesc, sizeof(EvtDesc));
                    if (RT_SUCCESS(rc))
                        rc = rtTraceLogWrStream(pThis, pEvtDescInt->EvtDesc.pszId, EvtDesc.cbStrId);
                    if (   RT_SUCCESS(rc)
                        && pEvtDescInt->EvtDesc.pszDesc)
                        rc = rtTraceLogWrStream(pThis, pEvtDescInt->EvtDesc.pszDesc, EvtDesc.cbStrDesc);
                    if (RT_SUCCESS(rc))
                    {
                        /* Go through the event items. */
                        for (unsigned idxEvtItem = 0; idxEvtItem < EvtDesc.cEvtItems && RT_SUCCESS(rc); idxEvtItem++)
                        {
                            PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDescInt->EvtDesc.paEvtItemDesc[idxEvtItem];
                            TRACELOGEVTITEMDESC EvtItemDesc;

                            RT_ZERO(EvtItemDesc);
                            memcpy(&EvtItemDesc.szMagic[0], TRACELOG_EVTITEMDESC_MAGIC, sizeof(EvtItemDesc.szMagic));
                            EvtItemDesc.cbStrName = (uint32_t)strlen(pEvtItemDesc->pszName);
                            EvtItemDesc.cbStrDesc = pEvtItemDesc->pszDesc ? (uint32_t)strlen(pEvtItemDesc->pszDesc) : 0;
                            EvtItemDesc.u32Type   = rtTraceLogWrConvTypeToStream(pEvtItemDesc->enmType);
                            EvtItemDesc.cbRawData = (uint32_t)pEvtItemDesc->cbRawData;
                            rc = rtTraceLogWrStream(pThis, &EvtItemDesc, sizeof(EvtItemDesc));
                            if (RT_SUCCESS(rc))
                                rc = rtTraceLogWrStream(pThis, pEvtItemDesc->pszName, EvtItemDesc.cbStrName);
                            if (   RT_SUCCESS(rc)
                                && pEvtItemDesc->pszDesc)
                                rc = rtTraceLogWrStream(pThis, pEvtItemDesc->pszDesc, EvtItemDesc.cbStrDesc);
                        }
                    }
                }
            }

            if (   RT_SUCCESS(rc)
                && ppEvtDescInt)
                *ppEvtDescInt = pEvtDescInt;
        }
        else
            rc = VERR_ALREADY_EXISTS;
        RTSemMutexRelease(pThis->hMtx);
    }

    return rc;
}


/**
 * Fills a given buffer with the given event data as described in the given descriptor.
 *
 * @returns IPRT status code.
 * @param   pThis               The trace log writer instance.
 * @param   pEvtDescInt         Pointer to the internal event descriptor.
 * @param   pb                  The byte buffer to fill.
 * @param   va                  The event data.
 */
static int rtTraceLogWrEvtFill(PRTTRACELOGWRINT pThis, PRTTRACELOGWREVTDESC pEvtDescInt, uint8_t *pb, va_list va)
{
    int rc = VINF_SUCCESS;
    uint8_t *pbCur = pb;

    RT_NOREF(pThis);

    for (unsigned i = 0; i < pEvtDescInt->EvtDesc.cEvtItems; i++)
    {
        PCRTTRACELOGEVTITEMDESC pEvtItemDesc = &pEvtDescInt->EvtDesc.paEvtItemDesc[i];

        size_t cbItem = rtTraceLogWrGetEvtItemDataSz(pEvtItemDesc);
        switch (cbItem)
        {
            case sizeof(uint8_t):
                *pbCur++ = va_arg(va, /*uint8_t*/ unsigned);
                break;
            case sizeof(uint16_t):
                *(uint16_t *)pbCur = va_arg(va, /*uint16_t*/ unsigned);
                pbCur += sizeof(uint16_t);
                break;
            case sizeof(uint32_t):
                *(uint32_t *)pbCur = va_arg(va, uint32_t);
                pbCur += sizeof(uint32_t);
                break;
            case sizeof(uint64_t):
                *(uint64_t *)pbCur = va_arg(va, uint64_t);
                pbCur += sizeof(uint64_t);
                break;
            default:
                /* Some raw data item. */
                Assert(pEvtItemDesc->enmType == RTTRACELOGTYPE_RAWDATA);
                if (cbItem != 0)
                {
                    /* Static raw data. */
                    void *pvSrc = va_arg(va, void *);
                    memcpy(pbCur, pvSrc, cbItem);
                    pbCur += cbItem;
                }
                else
                {
                    AssertMsgFailed(("Not implemented!\n"));
                    rc = VERR_NOT_IMPLEMENTED;
                }
        }
    }

    return rc;
}


/**
 * @copydoc FNRTTRACELOGWRSTREAM
 */
static DECLCALLBACK(int) rtTraceLogWrFileStream(void *pvUser, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    RTFILE hFile = (RTFILE)pvUser;
    return RTFileWrite(hFile, pvBuf, cbBuf, pcbWritten);
}


/**
 * @copydoc FNRTTRACELOGSTREAMCLOSE
 */
static DECLCALLBACK(int) rtTraceLogWrFileStreamClose(void *pvUser)
{
    RTFILE hFile = (RTFILE)pvUser;
    return RTFileClose(hFile);
}


RTDECL(int) RTTraceLogWrCreate(PRTTRACELOGWR phTraceLogWr, const char *pszDesc,
                               PFNRTTRACELOGWRSTREAM pfnStreamOut,
                               PFNRTTRACELOGSTREAMCLOSE pfnStreamClose, void *pvUser)
{
    AssertPtrReturn(phTraceLogWr,   VERR_INVALID_POINTER);
    AssertPtrReturn(pfnStreamOut,   VERR_INVALID_POINTER);
    AssertPtrReturn(pfnStreamClose, VERR_INVALID_POINTER);
    int rc = VINF_SUCCESS;
    PRTTRACELOGWRINT pThis = (PRTTRACELOGWRINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        rc = RTSemMutexCreate(&pThis->hMtx);
        if (RT_SUCCESS(rc))
        {
            pThis->u32Magic       = RTTRACELOGWR_MAGIC;
            pThis->pfnStreamOut   = pfnStreamOut;
            pThis->pfnStreamClose = pfnStreamClose;
            pThis->pvUser         = pvUser;
            pThis->u64SeqNoNext   = 0;
            pThis->pTreeEvtDescs  = NULL;
            pThis->cEvtDescs      = 0;
            rc = rtTraceLogWrInit(pThis, pszDesc);
            if (RT_SUCCESS(rc))
            {
                *phTraceLogWr = pThis;
                return VINF_SUCCESS;
            }
        }
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTTraceLogWrCreateFile(PRTTRACELOGWR phTraceLogWr, const char *pszDesc,
                                   const char *pszFilename)
{
    AssertPtrReturn(phTraceLogWr,   VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename,   VERR_INVALID_POINTER);

    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        rc = RTTraceLogWrCreate(phTraceLogWr, pszDesc, rtTraceLogWrFileStream,
                                rtTraceLogWrFileStreamClose, hFile);
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFile);
            RTFileDelete(pszFilename);
        }
    }

    return rc;
}


/**
 * @copydoc FNRTTRACELOGWRSTREAM
 */
static DECLCALLBACK(int) rtTraceLogWrTcpStream(void *pvUser, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PRTTRACELOGWRTCP pTraceLogTcp = (PRTTRACELOGWRTCP)pvUser;
    int rc = RTTcpWrite(pTraceLogTcp->hSock, pvBuf, cbBuf);
    if (   RT_SUCCESS(rc)
        && pcbWritten)
        *pcbWritten = cbBuf;

    return rc;
}


/**
 * @copydoc FNRTTRACELOGSTREAMCLOSE
 */
static DECLCALLBACK(int) rtTraceLogWrTcpStreamClose(void *pvUser)
{
    PRTTRACELOGWRTCP pTraceLogTcp = (PRTTRACELOGWRTCP)pvUser;
    if (pTraceLogTcp->fIsServer)
    {
        RTTcpServerDisconnectClient2(pTraceLogTcp->hSock);
        RTTcpServerDestroy(pTraceLogTcp->pTcpSrv);
    }
    else
        RTTcpClientClose(pTraceLogTcp->hSock);

    RTMemFree(pTraceLogTcp);
    return VINF_SUCCESS;
}


RTDECL(int) RTTraceLogWrCreateTcpServer(PRTTRACELOGWR phTraceLogWr, const char *pszDesc,
                                        const char *pszListen, unsigned uPort)
{
    int rc = VINF_SUCCESS;
    PRTTRACELOGWRTCP pTraceLogTcp = (PRTTRACELOGWRTCP)RTMemAllocZ(sizeof(*pTraceLogTcp));
    if (RT_LIKELY(pTraceLogTcp))
    {
        pTraceLogTcp->fIsServer = true;

        rc = RTTcpServerCreateEx(pszListen, uPort, &pTraceLogTcp->pTcpSrv);
        if (RT_SUCCESS(rc))
        {
            rc = RTTcpServerListen2(pTraceLogTcp->pTcpSrv, &pTraceLogTcp->hSock);
            if (RT_SUCCESS(rc))
            {
                rc = RTTraceLogWrCreate(phTraceLogWr, pszDesc, rtTraceLogWrTcpStream,
                                        rtTraceLogWrTcpStreamClose, pTraceLogTcp);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;

                RTTcpServerDisconnectClient2(pTraceLogTcp->hSock);
            }

            RTTcpServerDestroy(pTraceLogTcp->pTcpSrv);
        }

        RTMemFree(pTraceLogTcp);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTTraceLogWrCreateTcpClient(PRTTRACELOGWR phTraceLogWr, const char *pszDesc,
                                        const char *pszAddress, unsigned uPort)
{
    int rc = VINF_SUCCESS;
    PRTTRACELOGWRTCP pTraceLogTcp = (PRTTRACELOGWRTCP)RTMemAllocZ(sizeof(*pTraceLogTcp));
    if (RT_LIKELY(pTraceLogTcp))
    {
        pTraceLogTcp->fIsServer = false;

        rc = RTTcpClientConnect(pszAddress, uPort, &pTraceLogTcp->hSock);
        if (RT_SUCCESS(rc))
        {
            rc = RTTraceLogWrCreate(phTraceLogWr, pszDesc, rtTraceLogWrTcpStream,
                                    rtTraceLogWrTcpStreamClose, pTraceLogTcp);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            RTTcpClientClose(pTraceLogTcp->hSock);
        }

        RTMemFree(pTraceLogTcp);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTTraceLogWrDestroy(RTTRACELOGWR hTraceLogWr)
{
    if (hTraceLogWr == NIL_RTTRACELOGWR)
        return VINF_SUCCESS;
    PRTTRACELOGWRINT pThis = hTraceLogWr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGWR_MAGIC, VERR_INVALID_HANDLE);

    pThis->u32Magic = RTTRACELOGWR_MAGIC_DEAD;
    pThis->pfnStreamClose(pThis->pvUser);
    RTAvlPVDestroy(&pThis->pTreeEvtDescs, rtTraceLogWrEvtDescsDestroy, NULL);
    RTSemMutexDestroy(pThis->hMtx);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTTraceLogWrAddEvtDesc(RTTRACELOGWR hTraceLogWr, PCRTTRACELOGEVTDESC pEvtDesc)
{
    PRTTRACELOGWRINT pThis = hTraceLogWr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGWR_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pEvtDesc, VERR_INVALID_POINTER);

    return rtTraceLogWrEvtDescAdd(pThis, pEvtDesc, NULL);
}


RTDECL(int) RTTraceLogWrEvtAdd(RTTRACELOGWR hTraceLogWr, PCRTTRACELOGEVTDESC pEvtDesc, uint32_t fFlags,
                               RTTRACELOGEVTGRPID uGrpId, RTTRACELOGEVTGRPID uParentGrpId,
                               const void *pvEvtData, size_t *pacbRawData)
{
    PRTTRACELOGWRINT pThis = hTraceLogWr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGWR_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    PRTTRACELOGWREVTDESC pEvtDescInt = rtTraceLogWrEvtDescGetInternal(pThis, pEvtDesc);
    if (RT_UNLIKELY(!pEvtDescInt))
        rc = rtTraceLogWrEvtDescAdd(pThis, pEvtDesc, &pEvtDescInt);

    if (   RT_SUCCESS(rc)
        && RT_VALID_PTR(pEvtDescInt))
    {
        TRACELOGEVT Evt;
        size_t cbEvtData = rtTraceLogWrEvtInit(&Evt, pEvtDescInt, fFlags, uGrpId, uParentGrpId, pacbRawData);

        rc = rtTraceLogWrEvtStream(pThis, &Evt, pvEvtData, cbEvtData, pacbRawData);
    }

    return rc;
}


RTDECL(int) RTTraceLogWrEvtAddSg(RTTRACELOGWR hTraceLogWr, PCRTTRACELOGEVTDESC pEvtDesc, uint32_t fFlags,
                                 RTTRACELOGEVTGRPID uGrpId, RTTRACELOGEVTGRPID uParentGrpId,
                                 PRTSGBUF *pSgBufEvtData, size_t *pacbRawData)
{
    RT_NOREF(hTraceLogWr, pEvtDesc, fFlags, uGrpId, uParentGrpId, pSgBufEvtData, pacbRawData);
    return VERR_NOT_IMPLEMENTED;
}


RTDECL(int) RTTraceLogWrEvtAddLV(RTTRACELOGWR hTraceLogWr, PCRTTRACELOGEVTDESC pEvtDesc, uint32_t fFlags,
                                 RTTRACELOGEVTGRPID uGrpId, RTTRACELOGEVTGRPID uParentGrpId, va_list va)
{
    PRTTRACELOGWRINT pThis = hTraceLogWr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTRACELOGWR_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    PRTTRACELOGWREVTDESC pEvtDescInt = rtTraceLogWrEvtDescGetInternal(pThis, pEvtDesc);
    if (RT_UNLIKELY(!pEvtDescInt))
        rc = rtTraceLogWrEvtDescAdd(pThis, pEvtDesc, &pEvtDescInt);

    if (   RT_SUCCESS(rc)
        && RT_VALID_PTR(pEvtDescInt))
    {
        TRACELOGEVT Evt;
        size_t cbEvtData = rtTraceLogWrEvtInit(&Evt, pEvtDescInt, fFlags, uGrpId, uParentGrpId, NULL);

        if (cbEvtData)
            rc = rtTraceLogWrEvtFill(pThis, pEvtDescInt, pEvtDescInt->pbEvt, va);
        if (RT_SUCCESS(rc))
            rc = rtTraceLogWrEvtStream(pThis, &Evt, pEvtDescInt->pbEvt, cbEvtData, NULL);
    }

    return rc;
}


RTDECL(int) RTTraceLogWrEvtAddL(RTTRACELOGWR hTraceLogWr, PCRTTRACELOGEVTDESC pEvtDesc, uint32_t fFlags,
                                RTTRACELOGEVTGRPID uGrpId, RTTRACELOGEVTGRPID uParentGrpId, ...)
{
    va_list va;
    va_start(va, uParentGrpId);
    int rc = RTTraceLogWrEvtAddLV(hTraceLogWr, pEvtDesc, fFlags, uGrpId, uParentGrpId, va);
    va_end(va);
    return rc;
}

