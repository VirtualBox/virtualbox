/* $Id: PDMAsyncCompletionInternal.h 108651 2025-03-06 11:15:45Z alexander.eichner@oracle.com $ */
/** @file
 * PDM - Pluggable Device Manager, Async I/O Completion internal header.
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

#ifndef VMM_INCLUDED_SRC_include_PDMAsyncCompletionInternal_h
#define VMM_INCLUDED_SRC_include_PDMAsyncCompletionInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/critsect.h>
#include <iprt/memcache.h>
#include <iprt/sg.h>
#include <VBox/types.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pdmasynccompletion.h>
#include "PDMInternal.h"

RT_C_DECLS_BEGIN


/**
 * PDM Async completion endpoint operations.
 */
typedef struct PDMASYNCCOMPLETIONEPCLASSOPS
{
    /** Version identifier. */
    uint32_t                      u32Version;
    /** Name of the endpoint class. */
    const char                   *pszName;
    /** Class type. */
    PDMASYNCCOMPLETIONEPCLASSTYPE enmClassType;
    /** Size of the global endpoint class data in bytes. */
    size_t                        cbEndpointClassGlobal;
    /** Size of an endpoint in bytes. */
    size_t                        cbEndpoint;
    /** size of a task in bytes. */
    size_t                        cbTask;

    /**
     * Initializes the global data for a endpoint class.
     *
     * @returns VBox status code.
     * @param   pClassGlobals    Pointer to the uninitialized globals data.
     * @param   pCfgNode         Node for querying configuration data.
     */
    DECLR3CALLBACKMEMBER(int, pfnInitialize, (PPDMASYNCCOMPLETIONEPCLASS pClassGlobals, PCFGMNODE pCfgNode));

    /**
     * Frees all allocated resources which were allocated during init.
     *
     * @returns VBox status code.
     * @param   pClassGlobals    Pointer to the globals data.
     */
    DECLR3CALLBACKMEMBER(void, pfnTerminate, (PPDMASYNCCOMPLETIONEPCLASS pClassGlobals));

    /**
     * Initializes a given endpoint.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Pointer to the uninitialized endpoint.
     * @param   pszUri        Pointer to the string containing the endpoint
     *                        destination (filename, IP address, ...)
     * @param   fFlags        Creation flags.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpInitialize, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                                const char *pszUri, uint32_t fFlags));

    /**
     * Closes a endpoint finishing all tasks.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Pointer to the endpoint to be closed.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpClose, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint));

    /**
     * Initiates a read request from the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   off           Where to start reading from.
     * @param   paSegments    Scatter gather list to store the data in.
     * @param   cSegments     Number of segments in the list.
     * @param   cbRead        The overall number of bytes to read.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpRead, (PPDMASYNCCOMPLETIONTASK pTask,
                                          PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                          PCRTSGSEG paSegments, size_t cSegments,
                                         size_t cbRead));

    /**
     * Initiates a write request to the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   off           Where to start writing to.
     * @param   paSegments    Scatter gather list to store the data in.
     * @param   cSegments     Number of segments in the list.
     * @param   cbRead        The overall number of bytes to write.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpWrite, (PPDMASYNCCOMPLETIONTASK pTask,
                                           PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                           PCRTSGSEG paSegments, size_t cSegments,
                                           size_t cbWrite));

    /**
     * Initiates a flush request on the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpFlush, (PPDMASYNCCOMPLETIONTASK pTask,
                                           PPDMASYNCCOMPLETIONENDPOINT pEndpoint));

    /**
     * Queries the size of the endpoint. Optional.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   pcbSize       Where to store the size of the endpoint.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpGetSize, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                             uint64_t *pcbSize));

    /**
     * Sets the size of the endpoint. Optional.
     * This is a synchronous operation.
     *
     *
     * @returns VBox status code.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   cbSize        New size for the endpoint.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpSetSize, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                             uint64_t cbSize));

    /** Initialization safety marker. */
    uint32_t    u32VersionEnd;
} PDMASYNCCOMPLETIONEPCLASSOPS;
/** Pointer to a async completion endpoint class operation table. */
typedef PDMASYNCCOMPLETIONEPCLASSOPS *PPDMASYNCCOMPLETIONEPCLASSOPS;
/** Const pointer to a async completion endpoint class operation table. */
typedef const PDMASYNCCOMPLETIONEPCLASSOPS *PCPDMASYNCCOMPLETIONEPCLASSOPS;

/** Version for the endpoint class operations structure. */
#define PDMAC_EPCLASS_OPS_VERSION 0x00000001

/** Pointer to a bandwidth control manager. */
typedef struct PDMACBWMGR *PPDMACBWMGR;

/**
 * PDM Async completion endpoint class.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONEPCLASS
{
    /** Pointer to the VM. */
    PVM                                         pVM;
    /** Critical section protecting the lists below. */
    RTCRITSECT                                  CritSect;
    /** Number of endpoints in the list. */
    volatile unsigned                           cEndpoints;
    /** Head of endpoints with this class. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pEndpointsHead;
    /** Head of the bandwidth managers for this class. */
    R3PTRTYPE(PPDMACBWMGR)                      pBwMgrsHead;
    /** Pointer to the callback table. */
    R3PTRTYPE(PCPDMASYNCCOMPLETIONEPCLASSOPS)   pEndpointOps;
    /** Task cache. */
    RTMEMCACHE                                  hMemCacheTasks;
    /** Flag whether to gather advanced statistics about requests. */
    bool                                        fGatherAdvancedStatistics;
} PDMASYNCCOMPLETIONEPCLASS;
/** Pointer to the PDM async completion endpoint class data. */
typedef PDMASYNCCOMPLETIONEPCLASS *PPDMASYNCCOMPLETIONEPCLASS;

/**
 * A PDM Async completion endpoint.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONENDPOINT
{
    /** Next endpoint in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pNext;
    /** Previous endpoint in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pPrev;
    /** Pointer to the class this endpoint belongs to. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONEPCLASS)       pEpClass;
    /** Template associated with this endpoint. */
    PPDMASYNCCOMPLETIONTEMPLATE                 pTemplate;
    /** Statistics ID for endpoints having a similar URI (same filename for example)
     * to avoid assertions. */
    unsigned                                    iStatId;
    /** URI describing the endpoint */
    char                                       *pszUri;
    /** Pointer to the assigned bandwidth manager. */
    volatile PPDMACBWMGR                        pBwMgr;
    /** Aligns following statistic counters on a 8 byte boundary. */
    uint32_t                                    u32Alignment;
    /** @name Request size statistics.
     * @{ */
    STAMCOUNTER                                 StatReqSizeSmaller512;
    STAMCOUNTER                                 StatReqSize512To1K;
    STAMCOUNTER                                 StatReqSize1KTo2K;
    STAMCOUNTER                                 StatReqSize2KTo4K;
    STAMCOUNTER                                 StatReqSize4KTo8K;
    STAMCOUNTER                                 StatReqSize8KTo16K;
    STAMCOUNTER                                 StatReqSize16KTo32K;
    STAMCOUNTER                                 StatReqSize32KTo64K;
    STAMCOUNTER                                 StatReqSize64KTo128K;
    STAMCOUNTER                                 StatReqSize128KTo256K;
    STAMCOUNTER                                 StatReqSize256KTo512K;
    STAMCOUNTER                                 StatReqSizeOver512K;
    STAMCOUNTER                                 StatReqsUnaligned512;
    STAMCOUNTER                                 StatReqsUnaligned4K;
    STAMCOUNTER                                 StatReqsUnaligned8K;
    /** @} */
    /** @name Request completion time statistics.
     * @{ */
    STAMCOUNTER                                 StatTaskRunTimesNs[10];
    STAMCOUNTER                                 StatTaskRunTimesUs[10];
    STAMCOUNTER                                 StatTaskRunTimesMs[10];
    STAMCOUNTER                                 StatTaskRunTimesSec[10];
    STAMCOUNTER                                 StatTaskRunOver100Sec;
    STAMCOUNTER                                 StatIoOpsPerSec;
    STAMCOUNTER                                 StatIoOpsStarted;
    STAMCOUNTER                                 StatIoOpsCompleted;
    uint64_t                                    tsIntervalStartMs;
    uint64_t                                    cIoOpsCompleted;
    /** @} */
} PDMASYNCCOMPLETIONENDPOINT;
AssertCompileMemberAlignment(PDMASYNCCOMPLETIONENDPOINT, StatReqSizeSmaller512, sizeof(uint64_t));
AssertCompileMemberAlignment(PDMASYNCCOMPLETIONENDPOINT, StatTaskRunTimesNs, sizeof(uint64_t));

/**
 * A PDM async completion task handle.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONTASK
{
    /** Next task in the list
     * (for free and assigned tasks). */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTASK)      pNext;
    /** Previous task in the list
     * (for free and assigned tasks). */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTASK)      pPrev;
    /** Endpoint this task is assigned to. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)  pEndpoint;
    /** Opaque user data for this task. */
    void                                   *pvUser;
    /** Start timestamp. */
    uint64_t                                tsNsStart;
} PDMASYNCCOMPLETIONTASK;

void pdmR3AsyncCompletionCompleteTask(PPDMASYNCCOMPLETIONTASK pTask, int rc, bool fCallCompletionHandler);
bool pdmacEpIsTransferAllowed(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t cbTransfer, RTMSINTERVAL *pmsWhenNext);

RT_C_DECLS_END

extern const PDMASYNCCOMPLETIONEPCLASSOPS g_PDMAsyncCompletionEndpointClassFile;

#endif /* !VMM_INCLUDED_SRC_include_PDMAsyncCompletionInternal_h */

