/* $Id: req.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTReq header.
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

#ifndef IPRT_INCLUDED_INTERNAL_req_h
#define IPRT_INCLUDED_INTERNAL_req_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>


RT_C_DECLS_BEGIN

/**
 * Request state.
 */
typedef enum RTREQSTATE
{
    /** The state is invalid. */
    RTREQSTATE_INVALID = 0,
    /** The request have been allocated and is in the process of being filed. */
    RTREQSTATE_ALLOCATED,
    /** The request is queued by the requester. */
    RTREQSTATE_QUEUED,
    /** The request is begin processed. */
    RTREQSTATE_PROCESSING,
    /** The request has been cancelled. */
    RTREQSTATE_CANCELLED,
    /** The request is completed, the requester is begin notified. */
    RTREQSTATE_COMPLETED,
    /** The request packet is in the free chain. */
    RTREQSTATE_FREE
} RTREQSTATE;
AssertCompileSize(RTREQSTATE, sizeof(uint32_t));


/**
 * RT Request packet.
 *
 * This is used to request an action in the queue handler thread.
 */
struct RTREQ
{
    /** Magic number (RTREQ_MAGIC). */
    uint32_t                u32Magic;
    /** Set if the event semaphore is clear. */
    volatile bool           fEventSemClear;
    /** Set if the push back semaphore should be signaled when the request
     *  is picked up from the queue. */
    volatile bool           fSignalPushBack;
    /** Set if pool, clear if queue. */
    volatile bool           fPoolOrQueue;
    /** IPRT status code for the completed request. */
    volatile int32_t        iStatusX;
    /** Request state. */
    volatile RTREQSTATE     enmState;
    /** The reference count. */
    volatile uint32_t       cRefs;

    /** Pointer to the next request in the chain. */
    struct RTREQ * volatile pNext;

    union
    {
        /** Pointer to the pool this packet belongs to. */
        RTREQPOOL           hPool;
        /** Pointer to the queue this packet belongs to. */
        RTREQQUEUE          hQueue;
        /** Opaque owner access. */
        void               *pv;
    } uOwner;

    /** Timestamp take when the request was submitted to a pool.  Not used
     * for queued request. */
    uint64_t                uSubmitNanoTs;
    /** Requester completion event sem. */
    RTSEMEVENT              EventSem;
    /** Request pushback event sem.  Allocated lazily. */
    RTSEMEVENTMULTI         hPushBackEvt;
    /** Flags, RTREQ_FLAGS_*. */
    uint32_t                fFlags;
    /** Request type. */
    RTREQTYPE               enmType;
    /** Request specific data. */
    union RTREQ_U
    {
        /** RTREQTYPE_INTERNAL. */
        struct
        {
            /** Pointer to the function to be called. */
            PFNRT               pfn;
            /** Number of arguments. */
            uint32_t            cArgs;
            /** Array of arguments. */
            uintptr_t           aArgs[12];
        } Internal;
    } u;
};

/** Internal request representation. */
typedef RTREQ       RTREQINT;
/** Pointer to an internal request representation. */
typedef RTREQINT   *PRTREQINT;

/**
 * Internal queue instance.
 */
typedef struct RTREQQUEUEINT
{
    /** Magic value (RTREQQUEUE_MAGIC). */
    uint32_t                u32Magic;
    /** Set if busy (pending or processing requests). */
    bool volatile           fBusy;
    /** Head of the request queue (LIFO). Atomic. */
    volatile PRTREQ         pReqs;
    /** List of requests pending after a non-VINF_SUCCESS status code forced
     * RTReqQueueProcess to stop processing requestins.  This is in FIFO order. */
    volatile PRTREQ         pAlreadyPendingReqs;
    /** The last index used during alloc/free. */
    volatile uint32_t       iReqFree;
    /** Number of free request packets. */
    volatile uint32_t       cReqFree;
    /** Array of pointers to lists of free request packets. Atomic. */
    volatile PRTREQ         apReqFree[9];
    /** Requester event sem.
     * The request can use this event semaphore to wait/poll for new requests.
     */
    RTSEMEVENT              EventSem;
} RTREQQUEUEINT;

/** Pointer to an internal queue instance. */
typedef struct RTREQQUEUEINT *PRTREQQUEUEINT;
/** Pointer to a request thread pool instance. */
typedef struct RTREQPOOLINT *PRTREQPOOLINT;


/* req.cpp */
DECLHIDDEN(int)  rtReqAlloc(RTREQTYPE enmType, bool fPoolOrQueue, void *pvOwner, PRTREQ *phReq);
DECLHIDDEN(int)  rtReqReInit(PRTREQINT pReq, RTREQTYPE enmType);
DECLHIDDEN(void) rtReqFreeIt(PRTREQINT pReq);
DECLHIDDEN(int)  rtReqProcessOne(PRTREQ pReq);

/* reqpool.cpp / reqqueue.cpp. */
DECLHIDDEN(void) rtReqQueueSubmit(PRTREQQUEUEINT pQueue, PRTREQINT pReq);
DECLHIDDEN(void) rtReqPoolSubmit(PRTREQPOOLINT pPool, PRTREQINT pReq);
DECLHIDDEN(void) rtReqPoolCancel(PRTREQPOOLINT pPool, PRTREQINT pReq);
DECLHIDDEN(bool) rtReqQueueRecycle(PRTREQQUEUEINT pQueue, PRTREQINT pReq);
DECLHIDDEN(bool) rtReqPoolRecycle(PRTREQPOOLINT pPool, PRTREQINT pReq);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_req_h */

