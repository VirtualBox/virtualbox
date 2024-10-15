/* $Id: DrvDiskIntegrity.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: Disk integrity check.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_DISK_INTEGRITY
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/avl.h>
#include <iprt/mem.h>
#include <iprt/memcache.h>
#include <iprt/message.h>
#include <iprt/sg.h>
#include <iprt/time.h>
#include <iprt/tracelog.h>
#include <iprt/semaphore.h>
#include <iprt/asm.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Transfer direction.
 */
typedef enum DRVDISKAIOTXDIR
{
    /** Invalid. */
    DRVDISKAIOTXDIR_INVALID = 0,
    /** Read */
    DRVDISKAIOTXDIR_READ,
    /** Write */
    DRVDISKAIOTXDIR_WRITE,
    /** Flush */
    DRVDISKAIOTXDIR_FLUSH,
    /** Discard */
    DRVDISKAIOTXDIR_DISCARD,
    /** Read after write for immediate verification. */
    DRVDISKAIOTXDIR_READ_AFTER_WRITE
} DRVDISKAIOTXDIR;

/**
 * async I/O request.
 */
typedef struct DRVDISKAIOREQ
{
    /** Transfer direction. */
    DRVDISKAIOTXDIR enmTxDir;
    /** Start offset. */
    uint64_t        off;
    /** Transfer size. */
    size_t          cbTransfer;
    /** Segment array. */
    PCRTSGSEG       paSeg;
    /** Number of array entries. */
    unsigned        cSeg;
    /** User argument */
    void           *pvUser;
    /** Slot in the array. */
    unsigned        iSlot;
    /** Start timestamp */
    uint64_t        tsStart;
    /** Completion timestamp. */
    uint64_t        tsComplete;
    /** Ranges to discard. */
    PCRTRANGE       paRanges;
    /** Number of ranges. */
    unsigned        cRanges;
    /** I/O segment for the extended media interface
     * to hold the data. */
    RTSGSEG         IoSeg;
} DRVDISKAIOREQ, *PDRVDISKAIOREQ;

/**
 * I/O log entry.
 */
typedef struct IOLOGENT
{
    /** Start offset */
    uint64_t         off;
    /** Write size */
    size_t           cbWrite;
    /** Number of references to this entry. */
    unsigned         cRefs;
} IOLOGENT, *PIOLOGENT;

/**
 * Disk segment.
 */
typedef struct DRVDISKSEGMENT
{
    /** AVL core. */
    AVLRFOFFNODECORE Core;
    /** Size of the segment */
    size_t           cbSeg;
    /** Data for this segment */
    uint8_t         *pbSeg;
    /** Number of entries in the I/O array. */
    unsigned         cIoLogEntries;
    /** Array of I/O log references. */
    PIOLOGENT        apIoLog[1];
} DRVDISKSEGMENT, *PDRVDISKSEGMENT;

/**
 * Active requests list entry.
 */
typedef struct DRVDISKAIOREQACTIVE
{
    /** Pointer to the request. */
    volatile PDRVDISKAIOREQ pIoReq;
    /** Start timestamp. */
    uint64_t  tsStart;
} DRVDISKAIOREQACTIVE, *PDRVDISKAIOREQACTIVE;

/**
 * Disk integrity driver instance data.
 *
 * @implements  PDMIMEDIA
 * @implements  PDMIMEDIAPORT
 * @implements  PDMIMEDIAEX
 * @implements  PDMIMEDIAEXPORT
 * @implements  PDMIMEDIAMOUNT
 * @implements  PDMIMEDIAMOUNTNOTIFY
 */
typedef struct DRVDISKINTEGRITY
{
    /** Pointer driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Pointer to the media driver below us.
     * This is NULL if the media is not mounted. */
    PPDMIMEDIA              pDrvMedia;
    /** Our media interface */
    PDMIMEDIA               IMedia;

    /** The media port interface above. */
    PPDMIMEDIAPORT          pDrvMediaPort;
    /** Media port interface */
    PDMIMEDIAPORT           IMediaPort;

    /** The extended media port interface above. */
    PPDMIMEDIAEXPORT        pDrvMediaExPort;
    /** Our extended media port interface */
    PDMIMEDIAEXPORT         IMediaExPort;

    /** The extended media interface below. */
    PPDMIMEDIAEX            pDrvMediaEx;
    /** Our extended media interface */
    PDMIMEDIAEX             IMediaEx;

    /** The mount interface below. */
    PPDMIMOUNT              pDrvMount;
    /** Our mount interface */
    PDMIMOUNT               IMount;

    /** The mount notify interface above. */
    PPDMIMOUNTNOTIFY        pDrvMountNotify;
    /** Our mount notify interface. */
    PDMIMOUNTNOTIFY         IMountNotify;

    /** Flag whether consistency checks are enabled. */
    bool                    fCheckConsistency;
    /** Flag whether the RAM disk was prepopulated. */
    bool                    fPrepopulateRamDisk;
    /** AVL tree containing the disk blocks to check. */
    PAVLRFOFFTREE           pTreeSegments;

    /** Flag whether async request tracing is enabled. */
    bool                    fTraceRequests;
    /** Interval the thread should check for expired requests (milliseconds). */
    uint32_t                uCheckIntervalMs;
    /** Expire timeout for a request (milliseconds). */
    uint32_t                uExpireIntervalMs;
    /** Thread which checks for lost requests. */
    RTTHREAD                hThread;
    /** Event semaphore */
    RTSEMEVENT              SemEvent;
    /** Flag whether the thread should run. */
    bool                    fRunning;
    /** Array containing active requests. */
    DRVDISKAIOREQACTIVE     apReqActive[128];
    /** Next free slot in the array */
    volatile unsigned       iNextFreeSlot;
    /** Request cache. */
    RTMEMCACHE              hReqCache;

    /** Flag whether we check for requests completing twice. */
    bool                    fCheckDoubleCompletion;
    /** Number of requests we go back. */
    unsigned                cEntries;
    /** Array of completed but still observed requests. */
    PDRVDISKAIOREQ          *papIoReq;
    /** Current entry in the array. */
    unsigned                iEntry;

    /** Flag whether to do a immediate read after write for verification. */
    bool                    fReadAfterWrite;
    /** Flag whether to record the data to write before the write completed successfully.
     * Useful in case the data is modified in place later on (encryption for instance). */
    bool                    fRecordWriteBeforeCompletion;
    /** Flag whether to validate memory buffers when the extended media interface is used. */
    bool                    fValidateMemBufs;

    /** I/O logger to use if enabled. */
    RTTRACELOGWR            hIoLogger;
    /** Size of the opaque handle until our tracking structure starts in bytes. */
    size_t                  cbIoReqOpaque;
} DRVDISKINTEGRITY, *PDRVDISKINTEGRITY;


/**
 * Read/Write event items.
 */
static const RTTRACELOGEVTITEMDESC g_aEvtItemsReadWrite[] =
{
    { "Async",  "Flag whether the request is asynchronous", RTTRACELOGTYPE_BOOL,   0 },
    { "Offset", "Offset to start reading/writing from/to",  RTTRACELOGTYPE_UINT64, 0 },
    { "Size",   "Number of bytes to transfer",              RTTRACELOGTYPE_SIZE,   0 }
};

/**
 * Flush event items.
 */
static const RTTRACELOGEVTITEMDESC g_aEvtItemsFlush[] =
{
    { "Async",  "Flag whether the request is asynchronous", RTTRACELOGTYPE_BOOL,   0 }
};

/**
 * I/O request complete items.
 */
static const RTTRACELOGEVTITEMDESC g_aEvtItemsComplete[] =
{
    { "Status", "Status code the request completed with", RTTRACELOGTYPE_INT32, 0 }
};

/** Read event descriptor. */
static const RTTRACELOGEVTDESC g_EvtRead =
    { "Read", "Read data from disk", RTTRACELOGEVTSEVERITY_DEBUG, RT_ELEMENTS(g_aEvtItemsReadWrite), &g_aEvtItemsReadWrite[0] };
/** Write event descriptor. */
static const RTTRACELOGEVTDESC g_EvtWrite =
    { "Write", "Write data to disk", RTTRACELOGEVTSEVERITY_DEBUG, RT_ELEMENTS(g_aEvtItemsReadWrite), &g_aEvtItemsReadWrite[0] };
/** Flush event descriptor. */
static const RTTRACELOGEVTDESC g_EvtFlush =
    { "Flush", "Flush written data to disk", RTTRACELOGEVTSEVERITY_DEBUG, RT_ELEMENTS(g_aEvtItemsFlush), &g_aEvtItemsFlush[0] };
/** I/O request complete event descriptor. */
static const RTTRACELOGEVTDESC g_EvtComplete =
    { "Complete", "A previously started I/O request completed", RTTRACELOGEVTSEVERITY_DEBUG,
      RT_ELEMENTS(g_aEvtItemsComplete), &g_aEvtItemsComplete[0]};

#define DISKINTEGRITY_IOREQ_HANDLE_2_DRVDISKAIOREQ(a_pThis, a_hIoReq) ((*(PDRVDISKAIOREQ *)((uintptr_t)(a_hIoReq) + (a_pThis)->cbIoReqOpaque)))
#define DISKINTEGRITY_IOREQ_HANDLE_2_UPPER_OPAQUE(a_pThis, a_hIoReq) ((void *)((uintptr_t)(a_hIoReq) + (a_pThis)->cbIoReqOpaque + sizeof(PDRVDISKAIOREQ)))
#define DISKINTEGRITY_IOREQ_ALLOC_2_DRVDISKAIOREQ(a_pvIoReqAlloc) (*(PDRVDISKAIOREQ *)(a_pvIoReqAlloc))
#define DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(a_pvIoReqAlloc) ((void *)((uintptr_t)(a_pvIoReqAlloc) + sizeof(PDRVDISKAIOREQ)))

static void drvdiskintIoReqCheckForDoubleCompletion(PDRVDISKINTEGRITY pThis, PDRVDISKAIOREQ pIoReq,
                                                    bool fMediaEx)
{
    /* Search if the I/O request completed already. */
    for (unsigned i = 0; i < pThis->cEntries; i++)
    {
        if (RT_UNLIKELY(pThis->papIoReq[i] == pIoReq))
        {
            RTMsgError("Request %#p completed already!\n", pIoReq);
            if (!fMediaEx)
                RTMsgError("Start timestamp %llu Completion timestamp %llu (completed after %llu ms)\n",
                           pIoReq->tsStart, pIoReq->tsComplete, pIoReq->tsComplete - pIoReq->tsStart);
            RTAssertDebugBreak();
        }
    }

    pIoReq->tsComplete = RTTimeSystemMilliTS();
    Assert(!pThis->papIoReq[pThis->iEntry]);
    pThis->papIoReq[pThis->iEntry] = pIoReq;

    pThis->iEntry = (pThis->iEntry+1) % pThis->cEntries;
    if (pThis->papIoReq[pThis->iEntry])
    {
        if (!fMediaEx)
            RTMemFree(pThis->papIoReq[pThis->iEntry]);
        pThis->papIoReq[pThis->iEntry] = NULL;
    }
}

static void drvdiskintIoLogEntryRelease(PIOLOGENT pIoLogEnt)
{
    pIoLogEnt->cRefs--;
    if (!pIoLogEnt->cRefs)
        RTMemFree(pIoLogEnt);
}

/**
 * Record a successful write to the virtual disk.
 *
 * @returns VBox status code.
 * @param   pThis    Disk integrity driver instance data.
 * @param   paSeg    Segment array of the write to record.
 * @param   cSeg     Number of segments.
 * @param   off      Start offset.
 * @param   cbWrite  Number of bytes to record.
 */
static int drvdiskintWriteRecord(PDRVDISKINTEGRITY pThis, PCRTSGSEG paSeg, unsigned cSeg,
                                 uint64_t off, size_t cbWrite)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p paSeg=%#p cSeg=%u off=%llx cbWrite=%u\n",
                 pThis, paSeg, cSeg, off, cbWrite));

    /* Update the segments */
    size_t cbLeft   = cbWrite;
    RTFOFF offCurr  = (RTFOFF)off;
    RTSGBUF SgBuf;
    PIOLOGENT pIoLogEnt = (PIOLOGENT)RTMemAllocZ(sizeof(IOLOGENT));
    if (!pIoLogEnt)
        return VERR_NO_MEMORY;

    pIoLogEnt->off     = off;
    pIoLogEnt->cbWrite = cbWrite;
    pIoLogEnt->cRefs   = 0;

    RTSgBufInit(&SgBuf, paSeg, cSeg);

    while (cbLeft)
    {
        PDRVDISKSEGMENT pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetRangeGet(pThis->pTreeSegments, offCurr);
        size_t cbRange  = 0;
        bool fSet       = false;
        unsigned offSeg = 0;

        if (!pSeg)
        {
            /* Get next segment */
            pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetGetBestFit(pThis->pTreeSegments, offCurr, true);
            if (   !pSeg
                || offCurr + (RTFOFF)cbLeft <= pSeg->Core.Key)
                cbRange = cbLeft;
            else
                cbRange = pSeg->Core.Key - offCurr;

            Assert(cbRange % 512 == 0);

            /* Create new segment */
            pSeg = (PDRVDISKSEGMENT)RTMemAllocZ(RT_UOFFSETOF_DYN(DRVDISKSEGMENT, apIoLog[cbRange / 512]));
            if (pSeg)
            {
                pSeg->Core.Key      = offCurr;
                pSeg->Core.KeyLast  = offCurr + (RTFOFF)cbRange - 1;
                pSeg->cbSeg         = cbRange;
                pSeg->pbSeg         = (uint8_t *)RTMemAllocZ(cbRange);
                pSeg->cIoLogEntries = (uint32_t)cbRange / 512;
                if (!pSeg->pbSeg)
                    RTMemFree(pSeg);
                else
                {
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    AssertMsg(fInserted, ("Bug!\n")); RT_NOREF(fInserted);
                    fSet = true;
                }
            }
        }
        else
        {
            fSet    = true;
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));
        }

        if (fSet)
        {
            AssertPtr(pSeg);
            size_t cbCopied = RTSgBufCopyToBuf(&SgBuf, pSeg->pbSeg + offSeg, cbRange);
            Assert(cbCopied == cbRange); RT_NOREF(cbCopied);

            /* Update the I/O log pointers */
            Assert(offSeg % 512 == 0);
            Assert(cbRange % 512 == 0);
            while (offSeg < cbRange)
            {
                uint32_t uSector = offSeg / 512;
                PIOLOGENT pIoLogOld = NULL;

                AssertMsg(uSector < pSeg->cIoLogEntries, ("Internal bug!\n"));

                pIoLogOld = pSeg->apIoLog[uSector];
                if (pIoLogOld)
                {
                    pIoLogOld->cRefs--;
                    if (!pIoLogOld->cRefs)
                        RTMemFree(pIoLogOld);
                }

                pSeg->apIoLog[uSector] = pIoLogEnt;
                pIoLogEnt->cRefs++;

                offSeg += 512;
            }
        }
        else
            RTSgBufAdvance(&SgBuf, cbRange);

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    return rc;
}

/**
 * Verifies a read request.
 *
 * @returns VBox status code.
 * @param   pThis    Disk integrity driver instance data.
 * @param   paSeg    Segment array of the containing the data buffers to verify.
 * @param   cSeg     Number of segments.
 * @param   off      Start offset.
 * @param   cbRead   Number of bytes to verify.
 */
static int drvdiskintReadVerify(PDRVDISKINTEGRITY pThis, PCRTSGSEG paSeg, unsigned cSeg,
                                uint64_t off, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p paSeg=%#p cSeg=%u off=%llx cbRead=%u\n",
                 pThis, paSeg, cSeg, off, cbRead));

    Assert(off % 512 == 0);
    Assert(cbRead % 512 == 0);

    /* Compare read data */
    size_t cbLeft   = cbRead;
    RTFOFF offCurr  = (RTFOFF)off;
    RTSGBUF SgBuf;

    RTSgBufInit(&SgBuf, paSeg, cSeg);

    while (cbLeft)
    {
        PDRVDISKSEGMENT pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetRangeGet(pThis->pTreeSegments, offCurr);
        size_t cbRange  = 0;
        bool fCmp       = false;
        unsigned offSeg = 0;

        if (!pSeg)
        {
            /* Get next segment */
            pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetGetBestFit(pThis->pTreeSegments, offCurr, true);
            if (!pSeg)
            {
                /* No data in the tree for this read. Assume everything is ok. */
                cbRange = cbLeft;
            }
            else if (offCurr + (RTFOFF)cbLeft <= pSeg->Core.Key)
                cbRange = cbLeft;
            else
                cbRange = pSeg->Core.Key - offCurr;

            if (pThis->fPrepopulateRamDisk)
            {
                /* No segment means everything should be 0 for this part. */
                if (!RTSgBufIsZero(&SgBuf, cbRange))
                {
                    RTMsgError("Corrupted disk at offset %llu (expected everything to be 0)!\n",
                               offCurr);
                    RTAssertDebugBreak();
                }
            }
        }
        else
        {
            fCmp    = true;
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));
        }

        if (fCmp)
        {
            RTSGSEG Seg;
            RTSGBUF SgBufCmp;
            size_t cbOff = 0;

            Seg.cbSeg = cbRange;
            Seg.pvSeg = pSeg->pbSeg + offSeg;

            RTSgBufInit(&SgBufCmp, &Seg, 1);
            if (RTSgBufCmpEx(&SgBuf, &SgBufCmp, cbRange, &cbOff, true))
            {
                /* Corrupted disk, print I/O log entry of the last write which accessed this range. */
                uint32_t cSector = (offSeg + (uint32_t)cbOff) / 512;
                AssertMsg(cSector < pSeg->cIoLogEntries, ("Internal bug!\n"));

                RTMsgError("Corrupted disk at offset %llu (%u bytes in the current read buffer)!\n",
                           offCurr + cbOff, cbOff);
                RTMsgError("Last write to this sector started at offset %llu with %u bytes (%u references to this log entry)\n",
                           pSeg->apIoLog[cSector]->off,
                           pSeg->apIoLog[cSector]->cbWrite,
                           pSeg->apIoLog[cSector]->cRefs);
                RTAssertDebugBreak();
            }
        }
        else
            RTSgBufAdvance(&SgBuf, cbRange);

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    return rc;
}

/**
 * Discards the given ranges from the disk.
 *
 * @returns VBox status code.
 * @param   pThis    Disk integrity driver instance data.
 * @param   paRanges Array of ranges to discard.
 * @param   cRanges  Number of ranges in the array.
 */
static int drvdiskintDiscardRecords(PDRVDISKINTEGRITY pThis, PCRTRANGE paRanges, unsigned cRanges)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p paRanges=%#p cRanges=%u\n", pThis, paRanges, cRanges));

    for (unsigned i = 0; i < cRanges; i++)
    {
        uint64_t offStart = paRanges[i].offStart;
        size_t cbLeft = paRanges[i].cbRange;

        LogFlowFunc(("Discarding off=%llu cbRange=%zu\n", offStart, cbLeft));

        while (cbLeft)
        {
            size_t cbRange;
            PDRVDISKSEGMENT pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetRangeGet(pThis->pTreeSegments, offStart);

            if (!pSeg)
            {
                /* Get next segment */
                pSeg = (PDRVDISKSEGMENT)RTAvlrFileOffsetGetBestFit(pThis->pTreeSegments, offStart, true);
                if (   !pSeg
                    || (RTFOFF)offStart + (RTFOFF)cbLeft <= pSeg->Core.Key)
                    cbRange = cbLeft;
                else
                    cbRange = pSeg->Core.Key - offStart;

                Assert(!(cbRange % 512));
            }
            else
            {
                size_t cbPreLeft, cbPostLeft;

                cbRange    = RT_MIN(cbLeft, pSeg->Core.KeyLast - offStart + 1);
                cbPreLeft  = offStart - pSeg->Core.Key;
                cbPostLeft = pSeg->cbSeg - cbRange - cbPreLeft;

                Assert(!(cbRange % 512));
                Assert(!(cbPreLeft % 512));
                Assert(!(cbPostLeft % 512));

                LogFlowFunc(("cbRange=%zu cbPreLeft=%zu cbPostLeft=%zu\n",
                             cbRange, cbPreLeft, cbPostLeft));

                RTAvlrFileOffsetRemove(pThis->pTreeSegments, pSeg->Core.Key);

                if (!cbPreLeft && !cbPostLeft)
                {
                    /* Just free the whole segment. */
                    LogFlowFunc(("Freeing whole segment pSeg=%#p\n", pSeg));
                    RTMemFree(pSeg->pbSeg);
                    for (unsigned idx = 0; idx < pSeg->cIoLogEntries; idx++)
                        drvdiskintIoLogEntryRelease(pSeg->apIoLog[idx]);
                    RTMemFree(pSeg);
                }
                else if (cbPreLeft && !cbPostLeft)
                {
                    /* Realloc to new size and insert. */
                    LogFlowFunc(("Realloc segment pSeg=%#p\n", pSeg));
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPreLeft);
                    for (unsigned idx = (uint32_t)(cbPreLeft / 512); idx < pSeg->cIoLogEntries; idx++)
                        drvdiskintIoLogEntryRelease(pSeg->apIoLog[idx]);
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, RT_UOFFSETOF_DYN(DRVDISKSEGMENT, apIoLog[cbPreLeft / 512]));
                    pSeg->Core.KeyLast = pSeg->Core.Key + cbPreLeft - 1;
                    pSeg->cbSeg = cbPreLeft;
                    pSeg->cIoLogEntries = (uint32_t)(cbPreLeft / 512);
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    Assert(fInserted); RT_NOREF(fInserted);
                }
                else if (!cbPreLeft && cbPostLeft)
                {
                    /* Move data to the front and realloc. */
                    LogFlowFunc(("Move data and realloc segment pSeg=%#p\n", pSeg));
                    memmove(pSeg->pbSeg, pSeg->pbSeg + cbRange, cbPostLeft);
                    for (unsigned idx = 0; idx < cbRange / 512; idx++)
                        drvdiskintIoLogEntryRelease(pSeg->apIoLog[idx]);
                    for (unsigned idx = 0; idx < cbPostLeft /512; idx++)
                        pSeg->apIoLog[idx] = pSeg->apIoLog[(cbRange / 512) + idx];
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, RT_UOFFSETOF_DYN(DRVDISKSEGMENT, apIoLog[cbPostLeft / 512]));
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPostLeft);
                    pSeg->Core.Key += cbRange;
                    pSeg->cbSeg = cbPostLeft;
                    pSeg->cIoLogEntries = (uint32_t)(cbPostLeft / 512);
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    Assert(fInserted); RT_NOREF(fInserted);
                }
                else
                {
                    /* Split the segment into 2 new segments. */
                    LogFlowFunc(("Split segment pSeg=%#p\n", pSeg));
                    PDRVDISKSEGMENT pSegPost = (PDRVDISKSEGMENT)RTMemAllocZ(RT_UOFFSETOF_DYN(DRVDISKSEGMENT, apIoLog[cbPostLeft / 512]));
                    if (pSegPost)
                    {
                        pSegPost->Core.Key      = pSeg->Core.Key + cbPreLeft + cbRange;
                        pSegPost->Core.KeyLast  = pSeg->Core.KeyLast;
                        pSegPost->cbSeg         = cbPostLeft;
                        pSegPost->pbSeg         = (uint8_t *)RTMemAllocZ(cbPostLeft);
                        pSegPost->cIoLogEntries = (uint32_t)(cbPostLeft / 512);
                        if (!pSegPost->pbSeg)
                            RTMemFree(pSegPost);
                        else
                        {
                            memcpy(pSegPost->pbSeg, pSeg->pbSeg + cbPreLeft + cbRange, cbPostLeft);
                            for (unsigned idx = 0; idx < (uint32_t)(cbPostLeft / 512); idx++)
                                pSegPost->apIoLog[idx] = pSeg->apIoLog[((cbPreLeft + cbRange) / 512) + idx];

                            bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSegPost->Core);
                            Assert(fInserted); RT_NOREF(fInserted);
                        }
                    }

                    /* Shrink the current segment. */
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPreLeft);
                    for (unsigned idx = (uint32_t)(cbPreLeft / 512); idx < (uint32_t)((cbPreLeft + cbRange) / 512); idx++)
                        drvdiskintIoLogEntryRelease(pSeg->apIoLog[idx]);
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, RT_UOFFSETOF_DYN(DRVDISKSEGMENT, apIoLog[cbPreLeft / 512]));
                    pSeg->Core.KeyLast = pSeg->Core.Key + cbPreLeft - 1;
                    pSeg->cbSeg = cbPreLeft;
                    pSeg->cIoLogEntries = (uint32_t)(cbPreLeft / 512);
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    Assert(fInserted); RT_NOREF(fInserted);
                } /* if (cbPreLeft && cbPostLeft) */
            }

            offStart += cbRange;
            cbLeft   -= cbRange;
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Adds a request to the active list.
 *
 * @param   pThis    The driver instance data.
 * @param   pIoReq   The request to add.
 */
static void drvdiskintIoReqAdd(PDRVDISKINTEGRITY pThis, PDRVDISKAIOREQ pIoReq)
{
    PDRVDISKAIOREQACTIVE pReqActive = &pThis->apReqActive[pThis->iNextFreeSlot];

    Assert(!pReqActive->pIoReq);
    pReqActive->tsStart = pIoReq->tsStart;
    pReqActive->pIoReq  = pIoReq;
    pIoReq->iSlot = pThis->iNextFreeSlot;

    /* Search for the next one. */
    while (pThis->apReqActive[pThis->iNextFreeSlot].pIoReq)
        pThis->iNextFreeSlot = (pThis->iNextFreeSlot+1) % RT_ELEMENTS(pThis->apReqActive);
}

/**
 * Removes a request from the active list.
 *
 * @param   pThis    The driver instance data.
 * @param   pIoReq   The request to remove.
 */
static void drvdiskintIoReqRemove(PDRVDISKINTEGRITY pThis, PDRVDISKAIOREQ pIoReq)
{
    PDRVDISKAIOREQACTIVE pReqActive = &pThis->apReqActive[pIoReq->iSlot];

    Assert(pReqActive->pIoReq == pIoReq);

    ASMAtomicWriteNullPtr(&pReqActive->pIoReq);
}

/**
 * Thread checking for expired requests.
 *
 * @returns IPRT status code.
 * @param   pThread    Thread handle.
 * @param   pvUser     Opaque user data.
 */
static DECLCALLBACK(int) drvdiskIntIoReqExpiredCheck(RTTHREAD pThread, void *pvUser)
{
    PDRVDISKINTEGRITY pThis = (PDRVDISKINTEGRITY)pvUser;

    RT_NOREF(pThread);

    while (pThis->fRunning)
    {
        int rc = RTSemEventWait(pThis->SemEvent, pThis->uCheckIntervalMs);

        if (!pThis->fRunning)
            break;

        Assert(rc == VERR_TIMEOUT); RT_NOREF(rc);

        /* Get current timestamp for comparison. */
        uint64_t tsCurr = RTTimeSystemMilliTS();

        /* Go through the array and check for expired requests. */
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->apReqActive); i++)
        {
            PDRVDISKAIOREQACTIVE pReqActive = &pThis->apReqActive[i];
            PDRVDISKAIOREQ pIoReq = ASMAtomicReadPtrT(&pReqActive->pIoReq, PDRVDISKAIOREQ);

            if (   pIoReq
                && (tsCurr > pReqActive->tsStart)
                && (tsCurr - pReqActive->tsStart) >= pThis->uExpireIntervalMs)
            {
                RTMsgError("Request %#p expired (active for %llu ms already)\n",
                           pIoReq, tsCurr - pReqActive->tsStart);
                RTAssertDebugBreak();
            }
        }
    }

    return VINF_SUCCESS;
}

/**
 * Verify a completed read after write request.
 *
 * @returns VBox status code.
 * @param   pThis    The driver instance data.
 * @param   pIoReq   The request to be verified.
 */
static int drvdiskintReadAfterWriteVerify(PDRVDISKINTEGRITY pThis, PDRVDISKAIOREQ pIoReq)
{
    int rc = VINF_SUCCESS;

    if (pThis->fCheckConsistency)
        rc = drvdiskintReadVerify(pThis, pIoReq->paSeg, pIoReq->cSeg, pIoReq->off, pIoReq->cbTransfer);
    else /** @todo Implement read after write verification without a memory based image of the disk. */
        AssertMsgFailed(("TODO\n"));

    return rc;
}


/**
 * Fires a read event if enabled.
 *
 * @param   pThis    The driver instance data.
 * @param   uGrp     The group ID.
 * @param   fAsync   Flag whether this is an async request.
 * @param   off      The offset to put into the event log.
 * @param   cbRead   Amount of bytes to read.
 */
DECLINLINE(void) drvdiskintTraceLogFireEvtRead(PDRVDISKINTEGRITY pThis, uintptr_t uGrp, bool fAsync, uint64_t off, size_t cbRead)
{
    if (pThis->hIoLogger)
    {
        int rc = RTTraceLogWrEvtAddL(pThis->hIoLogger, &g_EvtRead, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                     (RTTRACELOGEVTGRPID)uGrp, 0, fAsync, off, cbRead);
        AssertRC(rc);
    }
}


/**
 * Fires a write event if enabled.
 *
 * @param   pThis    The driver instance data.
 * @param   uGrp     The group ID.
 * @param   fAsync   Flag whether this is an async request.
 * @param   off      The offset to put into the event log.
 * @param   cbWrite  Amount of bytes to write.
 */
DECLINLINE(void) drvdiskintTraceLogFireEvtWrite(PDRVDISKINTEGRITY pThis, uintptr_t uGrp, bool fAsync, uint64_t off, size_t cbWrite)
{
    if (pThis->hIoLogger)
    {
        int rc = RTTraceLogWrEvtAddL(pThis->hIoLogger, &g_EvtWrite, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                     (RTTRACELOGEVTGRPID)uGrp, 0, fAsync, off, cbWrite);
        AssertRC(rc);
    }
}


/**
 * Fires a flush event if enabled.
 *
 * @param   pThis    The driver instance data.
 * @param   uGrp     The group ID.
 * @param   fAsync   Flag whether this is an async request.
 */
DECLINLINE(void) drvdiskintTraceLogFireEvtFlush(PDRVDISKINTEGRITY pThis, uintptr_t uGrp, bool fAsync)
{
    if (pThis->hIoLogger)
    {
        int rc = RTTraceLogWrEvtAddL(pThis->hIoLogger, &g_EvtFlush, RTTRACELOG_WR_ADD_EVT_F_GRP_START,
                                     (RTTRACELOGEVTGRPID)uGrp, 0, fAsync);
        AssertRC(rc);
    }
}


/**
 * Fires a request complete event if enabled.
 *
 * @param   pThis    The driver instance data.
 * @param   uGrp     The group ID.
 * @param   rcReq    Status code the request completed with.
 * @param   pSgBuf   The S/G buffer holding the data.
 */
DECLINLINE(void) drvdiskintTraceLogFireEvtComplete(PDRVDISKINTEGRITY pThis, uintptr_t uGrp, int rcReq, PRTSGBUF pSgBuf)
{
    RT_NOREF(pSgBuf);

    if (pThis->hIoLogger)
    {
        int rc = RTTraceLogWrEvtAddL(pThis->hIoLogger, &g_EvtComplete, RTTRACELOG_WR_ADD_EVT_F_GRP_FINISH,
                                     (RTTRACELOGEVTGRPID)uGrp, 0, rcReq);
        AssertRC(rc);
    }
}


/* -=-=-=-=- IMedia -=-=-=-=- */

/** Makes a PDRVDISKINTEGRITY out of a PPDMIMEDIA. */
#define PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface)        ( (PDRVDISKINTEGRITY)((uintptr_t)pInterface - RT_UOFFSETOF(DRVDISKINTEGRITY, IMedia)) )


/*********************************************************************************************************************************
*   Media interface methods                                                                                                      *
*********************************************************************************************************************************/


/** @interface_method_impl{PDMIMEDIA,pfnRead} */
static DECLCALLBACK(int) drvdiskintRead(PPDMIMEDIA pInterface,
                                        uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc = VINF_SUCCESS;
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);

    drvdiskintTraceLogFireEvtRead(pThis, (uintptr_t)pvBuf, false /* fAsync */, off, cbRead);
    rc = pThis->pDrvMedia->pfnRead(pThis->pDrvMedia, off, pvBuf, cbRead);

    if (pThis->hIoLogger)
    {
        RTSGSEG Seg;
        RTSGBUF SgBuf;

        Seg.pvSeg = pvBuf;
        Seg.cbSeg = cbRead;
        RTSgBufInit(&SgBuf, &Seg, 1);
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)pvBuf, rc, &SgBuf);
    }

    if (RT_FAILURE(rc))
        return rc;

    if (pThis->fCheckConsistency)
    {
        /* Verify the read. */
        RTSGSEG Seg;
        Seg.cbSeg = cbRead;
        Seg.pvSeg = pvBuf;
        rc = drvdiskintReadVerify(pThis, &Seg, 1, off, cbRead);
    }

    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnWrite} */
static DECLCALLBACK(int) drvdiskintWrite(PPDMIMEDIA pInterface,
                                         uint64_t off, const void *pvBuf,
                                         size_t cbWrite)
{
    int rc = VINF_SUCCESS;
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);

    drvdiskintTraceLogFireEvtWrite(pThis, (uintptr_t)pvBuf, false /* fAsync */, off, cbWrite);

    if (pThis->fRecordWriteBeforeCompletion)
    {
        RTSGSEG Seg;
        Seg.cbSeg = cbWrite;
        Seg.pvSeg = (void *)pvBuf;

        rc = drvdiskintWriteRecord(pThis, &Seg, 1, off, cbWrite);
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = pThis->pDrvMedia->pfnWrite(pThis->pDrvMedia, off, pvBuf, cbWrite);

    drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)pvBuf, rc, NULL);
    if (RT_FAILURE(rc))
        return rc;

    if (   pThis->fCheckConsistency
        && !pThis->fRecordWriteBeforeCompletion)
    {
        /* Record the write. */
        RTSGSEG Seg;
        Seg.cbSeg = cbWrite;
        Seg.pvSeg = (void *)pvBuf;
        rc = drvdiskintWriteRecord(pThis, &Seg, 1, off, cbWrite);
    }

    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnFlush} */
static DECLCALLBACK(int) drvdiskintFlush(PPDMIMEDIA pInterface)
{
    int rc = VINF_SUCCESS;
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);

    drvdiskintTraceLogFireEvtFlush(pThis, 1, false /* fAsync */);
    rc = pThis->pDrvMedia->pfnFlush(pThis->pDrvMedia);
    drvdiskintTraceLogFireEvtComplete(pThis, 1, rc, NULL);

    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetSize} */
static DECLCALLBACK(uint64_t) drvdiskintGetSize(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnGetSize(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnIsReadOnly} */
static DECLCALLBACK(bool) drvdiskintIsReadOnly(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnIsReadOnly(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosIsVisible} */
static DECLCALLBACK(bool) drvdiskintBiosIsVisible(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnBiosIsVisible(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnGetType} */
static DECLCALLBACK(PDMMEDIATYPE) drvdiskintGetType(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnGetType(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetPCHSGeometry} */
static DECLCALLBACK(int) drvdiskintBiosGetPCHSGeometry(PPDMIMEDIA pInterface,
                                                       PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnBiosGetPCHSGeometry(pThis->pDrvMedia, pPCHSGeometry);
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetPCHSGeometry} */
static DECLCALLBACK(int) drvdiskintBiosSetPCHSGeometry(PPDMIMEDIA pInterface,
                                                       PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnBiosSetPCHSGeometry(pThis->pDrvMedia, pPCHSGeometry);
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetLCHSGeometry} */
static DECLCALLBACK(int) drvdiskintBiosGetLCHSGeometry(PPDMIMEDIA pInterface,
                                                       PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnBiosGetLCHSGeometry(pThis->pDrvMedia, pLCHSGeometry);
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetLCHSGeometry} */
static DECLCALLBACK(int) drvdiskintBiosSetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnBiosSetLCHSGeometry(pThis->pDrvMedia, pLCHSGeometry);
}

/** @interface_method_impl{PDMIMEDIA,pfnGetUuid} */
static DECLCALLBACK(int) drvdiskintGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnGetUuid(pThis->pDrvMedia, pUuid);
}

/** @interface_method_impl{PDMIMEDIA,pfnGetSectorSize} */
static DECLCALLBACK(uint32_t) drvdiskintGetSectorSize(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnGetSectorSize(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnDiscard} */
static DECLCALLBACK(int) drvdiskintDiscard(PPDMIMEDIA pInterface, PCRTRANGE paRanges, unsigned cRanges)
{
    int rc = VINF_SUCCESS;
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);

    rc = pThis->pDrvMedia->pfnDiscard(pThis->pDrvMedia, paRanges, cRanges);
    drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)paRanges, rc, NULL);

    if (pThis->fCheckConsistency)
        rc = drvdiskintDiscardRecords(pThis, paRanges, cRanges);

    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnReadPcBios} */
static DECLCALLBACK(int) drvdiskintReadPcBios(PPDMIMEDIA pInterface,
                                              uint64_t off, void *pvBuf, size_t cbRead)
{
    LogFlowFunc(("\n"));
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);

    return pThis->pDrvMedia->pfnReadPcBios(pThis->pDrvMedia, off, pvBuf, cbRead);
}

/** @interface_method_impl{PDMIMEDIA,pfnIsNonRotational} */
static DECLCALLBACK(bool) drvdiskintIsNonRotational(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnIsNonRotational(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnGetRegionCount} */
static DECLCALLBACK(uint32_t) drvdiskintGetRegionCount(PPDMIMEDIA pInterface)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnGetRegionCount(pThis->pDrvMedia);
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionProperties} */
static DECLCALLBACK(int) drvdiskintQueryRegionProperties(PPDMIMEDIA pInterface, uint32_t uRegion, uint64_t *pu64LbaStart,
                                                         uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                         PVDREGIONDATAFORM penmDataForm)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnQueryRegionProperties(pThis->pDrvMedia, uRegion, pu64LbaStart, pcBlocks, pcbBlock, penmDataForm);
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionPropertiesForLba} */
static DECLCALLBACK(int) drvdiskintQueryRegionPropertiesForLba(PPDMIMEDIA pInterface, uint64_t u64LbaStart,
                                                               uint32_t *puRegion, uint64_t *pcBlocks,
                                                               uint64_t *pcbBlock, PVDREGIONDATAFORM penmDataForm)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIA_2_DRVDISKINTEGRITY(pInterface);
    return pThis->pDrvMedia->pfnQueryRegionPropertiesForLba(pThis->pDrvMedia, u64LbaStart, puRegion, pcBlocks, pcbBlock, penmDataForm);
}

/* -=-=-=-=- IMediaPort -=-=-=-=- */

/** Makes a PDRVBLOCK out of a PPDMIMEDIAPORT. */
#define PDMIMEDIAPORT_2_DRVDISKINTEGRITY(pInterface)    ( (PDRVDISKINTEGRITY((uintptr_t)pInterface - RT_UOFFSETOF(DRVDISKINTEGRITY, IMediaPort))) )

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) drvdiskintQueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                       uint32_t *piInstance, uint32_t *piLUN)
{
    PDRVDISKINTEGRITY pThis = PDMIMEDIAPORT_2_DRVDISKINTEGRITY(pInterface);

    return pThis->pDrvMediaPort->pfnQueryDeviceLocation(pThis->pDrvMediaPort, ppcszController,
                                                        piInstance, piLUN);
}

/* -=-=-=-=- IMediaExPort -=-=-=-=- */

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) drvdiskintIoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                       void *pvIoReqAlloc, int rcReq)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaExPort);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_ALLOC_2_DRVDISKAIOREQ(pvIoReqAlloc);
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pIoReq=%#p\n", pIoReq));

    /* Remove from the active list. */
    if (pThis->fTraceRequests)
        drvdiskintIoReqRemove(pThis, pIoReq);

    if (RT_SUCCESS(rcReq) && pThis->fCheckConsistency)
    {
        if (pIoReq->enmTxDir == DRVDISKAIOTXDIR_READ)
            rc = drvdiskintReadVerify(pThis, &pIoReq->IoSeg, 1, pIoReq->off, pIoReq->cbTransfer);
        else if (   pIoReq->enmTxDir == DRVDISKAIOTXDIR_WRITE
                 && !pThis->fRecordWriteBeforeCompletion)
            rc = drvdiskintWriteRecord(pThis, &pIoReq->IoSeg, 1, pIoReq->off, pIoReq->cbTransfer);
        else if (pIoReq->enmTxDir == DRVDISKAIOTXDIR_DISCARD)
            rc = drvdiskintDiscardRecords(pThis, pIoReq->paRanges, pIoReq->cRanges);
        else if (pIoReq->enmTxDir == DRVDISKAIOTXDIR_READ_AFTER_WRITE)
            rc = drvdiskintReadAfterWriteVerify(pThis, pIoReq);
        else
            AssertMsg(   pIoReq->enmTxDir == DRVDISKAIOTXDIR_FLUSH
                      || (   pIoReq->enmTxDir == DRVDISKAIOTXDIR_WRITE
                          && pThis->fRecordWriteBeforeCompletion), ("Huh?\n"));

        AssertRC(rc);
    }

    if (   RT_SUCCESS(rcReq)
        && pThis->fValidateMemBufs
        && pIoReq->enmTxDir == DRVDISKAIOTXDIR_READ)
    {
        /* Check that the guest memory buffer matches what was written. */
        RTSGSEG SegCmp;
        SegCmp.pvSeg = RTMemAlloc(pIoReq->cbTransfer);
        SegCmp.cbSeg = pIoReq->cbTransfer;

        RTSGBUF SgBufCmp;
        RTSgBufInit(&SgBufCmp, &SegCmp, 1);
        rc = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, hIoReq,
                                                       DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                       0, &SgBufCmp, pIoReq->cbTransfer);
        AssertRC(rc);

        RTSGBUF SgBuf;
        RTSgBufInit(&SgBuf, &pIoReq->IoSeg, 1);
        if (RTSgBufCmp(&SgBuf, &SgBufCmp, pIoReq->cbTransfer))
        {
            RTMsgError("Corrupted memory buffer at offset %llu!\n", 0);
            RTAssertDebugBreak();
        }

        RTMemFree(SegCmp.pvSeg);
    }

    if (pThis->hIoLogger)
    {
        RTSGBUF SgBuf;

        if (pIoReq->enmTxDir == DRVDISKAIOTXDIR_READ)
            RTSgBufInit(&SgBuf, &pIoReq->IoSeg, 1);
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rcReq, &SgBuf);
    }

    if (   pThis->fReadAfterWrite
        && pIoReq->enmTxDir == DRVDISKAIOTXDIR_WRITE)
    {
#if 0 /** @todo */
        pIoReq->enmTxDir = DRVDISKAIOTXDIR_READ_AFTER_WRITE;

        /* Add again because it was removed above. */
        if (pThis->fTraceRequests)
            drvdiskintIoReqAdd(pThis, pIoReq);

        rc = pThis->pDrvMediaAsync->pfnStartRead(pThis->pDrvMediaAsync, pIoReq->off, pIoReq->paSeg, pIoReq->cSeg,
                                                 pIoReq->cbTransfer, pIoReq);
        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            rc = drvdiskintReadAfterWriteVerify(pThis, pIoReq);

            if (pThis->fTraceRequests)
                drvdiskintIoReqRemove(pThis, pIoReq);
            RTMemFree(pIoReq);
        }
        else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
            RTMemFree(pIoReq);
#endif
    }
    else
    {
        rc = pThis->pDrvMediaExPort->pfnIoReqCompleteNotify(pThis->pDrvMediaExPort, hIoReq,
                                                            DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                            rcReq);
        /* Put on the watch list. */
        if (pThis->fCheckDoubleCompletion)
            drvdiskintIoReqCheckForDoubleCompletion(pThis, pIoReq, true /* fMediaEx */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}
 */
static DECLCALLBACK(int) drvdiskintIoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                    void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                    size_t cbCopy)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaExPort);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_ALLOC_2_DRVDISKAIOREQ(pvIoReqAlloc);
    RTSGBUF SgBuf;

    RTSgBufClone(&SgBuf, pSgBuf);

    int rc = pThis->pDrvMediaExPort->pfnIoReqCopyFromBuf(pThis->pDrvMediaExPort, hIoReq,
                                                         DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                         offDst, pSgBuf, cbCopy);
    if (   RT_SUCCESS(rc)
        && pIoReq->IoSeg.pvSeg)
    {
        /* Update our copy. */
        RTSgBufCopyToBuf(&SgBuf, (uint8_t *)pIoReq->IoSeg.pvSeg + offDst, cbCopy);

        /* Validate the just read data against our copy if possible. */
        if (   pThis->fValidateMemBufs
            && pThis->fCheckConsistency
            && pIoReq->enmTxDir == DRVDISKAIOTXDIR_READ)
        {
            RTSGSEG Seg;

            Seg.pvSeg = (uint8_t *)pIoReq->IoSeg.pvSeg + offDst;
            Seg.cbSeg = cbCopy;

            rc = drvdiskintReadVerify(pThis, &Seg, 1, pIoReq->off + offDst,
                                      cbCopy);
        }
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 */
static DECLCALLBACK(int) drvdiskintIoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                  void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                                  size_t cbCopy)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaExPort);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_ALLOC_2_DRVDISKAIOREQ(pvIoReqAlloc);
    RTSGBUF SgBuf;

    RTSgBufClone(&SgBuf, pSgBuf);

    int rc = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, hIoReq,
                                                       DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                       offSrc, pSgBuf, cbCopy);
    if (   RT_SUCCESS(rc)
        && pIoReq->IoSeg.pvSeg)
    {
        if (pThis->fValidateMemBufs)
        {
            /* Make sure what the caller requested matches what we got earlier. */
            RTSGBUF SgBufCmp;
            RTSgBufInit(&SgBufCmp, &pIoReq->IoSeg, 1);
            RTSgBufAdvance(&SgBufCmp, offSrc);

            if (RTSgBufCmp(&SgBuf, &SgBufCmp, cbCopy))
            {
                RTMsgError("Corrupted memory buffer at offset %llu!\n", offSrc);
                RTAssertDebugBreak();
            }
        }
        else
        {
            /* Update our copy. */
            RTSgBufCopyToBuf(&SgBuf, (uint8_t *)pIoReq->IoSeg.pvSeg + offSrc, cbCopy);
        }
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqQueryDiscardRanges}
 */
static DECLCALLBACK(int) drvdiskintIoReqQueryDiscardRanges(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                           void *pvIoReqAlloc, uint32_t idxRangeStart,
                                                           uint32_t cRanges, PRTRANGE paRanges,
                                                           uint32_t *pcRanges)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaExPort);
    return pThis->pDrvMediaExPort->pfnIoReqQueryDiscardRanges(pThis->pDrvMediaExPort, hIoReq,
                                                              DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                              idxRangeStart, cRanges, paRanges, pcRanges);
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) drvdiskintIoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                     void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaExPort);
    pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, hIoReq,
                                                 DISKINTEGRITY_IOREQ_ALLOC_2_UPPER(pvIoReqAlloc),
                                                 enmState);
}

/* -=-=-=-=- IMediaEx -=-=-=-=- */

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnQueryFeatures}
 */
static DECLCALLBACK(int) drvdiskintQueryFeatures(PPDMIMEDIAEX pInterface, uint32_t *pfFeatures)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnQueryFeatures(pThis->pDrvMediaEx, pfFeatures);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnNotifySuspend}
 */
static DECLCALLBACK(void) drvdiskintNotifySuspend(PPDMIMEDIAEX pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnNotifySuspend(pThis->pDrvMediaEx);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAllocSizeSet}
 */
static DECLCALLBACK(int) drvdiskintIoReqAllocSizeSet(PPDMIMEDIAEX pInterface, size_t cbIoReqAlloc)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);

    /* Increase the amount by the size of a pointer to our private tracking structure. */
    cbIoReqAlloc += sizeof(PDRVDISKAIOREQ);

    pThis->fCheckDoubleCompletion = false;

    return pThis->pDrvMediaEx->pfnIoReqAllocSizeSet(pThis->pDrvMediaEx, cbIoReqAlloc);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAlloc}
 */
static DECLCALLBACK(int) drvdiskintIoReqAlloc(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc,
                                              PDMMEDIAEXIOREQID uIoReqId, uint32_t fFlags)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    int rc = VINF_SUCCESS;
    PDRVDISKAIOREQ pIoReq = (PDRVDISKAIOREQ)RTMemCacheAlloc(pThis->hReqCache);
    if (RT_LIKELY(pIoReq))
    {
        pIoReq->enmTxDir    = DRVDISKAIOTXDIR_INVALID;
        pIoReq->off         = 0;
        pIoReq->cbTransfer  = 0;
        pIoReq->paSeg       = NULL;
        pIoReq->cSeg        = 0;
        pIoReq->pvUser      = NULL;
        pIoReq->iSlot       = 0;
        pIoReq->tsStart     = 0;
        pIoReq->tsComplete  = 0;
        pIoReq->IoSeg.pvSeg = NULL;
        pIoReq->IoSeg.cbSeg = 0;

        PDRVDISKAIOREQ *ppIoReq = NULL;
        rc = pThis->pDrvMediaEx->pfnIoReqAlloc(pThis->pDrvMediaEx, phIoReq, (void **)&ppIoReq, uIoReqId, fFlags);
        if RT_SUCCESS(rc)
        {
            /*
             * Store the size off the start of our tracking structure because it is
             * required to access it for the read/write callbacks.
             *
             * ASSUMPTION that the offset is constant.
             */
            if (!pThis->cbIoReqOpaque)
                pThis->cbIoReqOpaque = (uintptr_t)ppIoReq - (uintptr_t)*phIoReq;
            else
                Assert(pThis->cbIoReqOpaque == (uintptr_t)ppIoReq - (uintptr_t)*phIoReq);

            *ppIoReq = pIoReq;
            *ppvIoReqAlloc = ((uint8_t *)ppIoReq) + sizeof(PDRVDISKAIOREQ);
        }
        else
            RTMemCacheFree(pThis->hReqCache, pIoReq);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFree}
 */
static DECLCALLBACK(int) drvdiskintIoReqFree(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_HANDLE_2_DRVDISKAIOREQ(pThis, hIoReq);

    if (pIoReq->IoSeg.pvSeg)
        RTMemFree(pIoReq->IoSeg.pvSeg);

    return pThis->pDrvMediaEx->pfnIoReqFree(pThis->pDrvMediaEx, hIoReq);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryResidual}
 */
static DECLCALLBACK(int) drvdiskintIoReqQueryResidual(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbResidual)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqQueryResidual(pThis->pDrvMediaEx, hIoReq, pcbResidual);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryXferSize}
 */
static DECLCALLBACK(int) drvdiskintIoReqQueryXferSize(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbXfer)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqQueryXferSize(pThis->pDrvMediaEx, hIoReq, pcbXfer);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancelAll}
 */
static DECLCALLBACK(int) drvdiskintIoReqCancelAll(PPDMIMEDIAEX pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqCancelAll(pThis->pDrvMediaEx);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancel}
 */
static DECLCALLBACK(int) drvdiskintIoReqCancel(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQID uIoReqId)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqCancel(pThis->pDrvMediaEx, uIoReqId);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqRead}
 */
static DECLCALLBACK(int) drvdiskintIoReqRead(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbRead)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_HANDLE_2_DRVDISKAIOREQ(pThis, hIoReq);

    pIoReq->enmTxDir    = DRVDISKAIOTXDIR_READ;
    pIoReq->off         = off;
    pIoReq->cbTransfer  = cbRead;

    /* Allocate a I/O buffer if the I/O is verified.*/
    if (pThis->fCheckConsistency)
    {
        pIoReq->IoSeg.pvSeg = RTMemAlloc(cbRead);
        pIoReq->IoSeg.cbSeg = cbRead;
    }

    if (pThis->fTraceRequests)
        drvdiskintIoReqAdd(pThis, pIoReq);

    drvdiskintTraceLogFireEvtRead(pThis, (uintptr_t)hIoReq, true /* fAsync */, off, cbRead);
    int rc = pThis->pDrvMediaEx->pfnIoReqRead(pThis->pDrvMediaEx, hIoReq, off, cbRead);
    if (rc == VINF_SUCCESS)
    {
        /* Verify the read now. */
        if (pThis->fCheckConsistency)
        {
            int rc2 = drvdiskintReadVerify(pThis, &pIoReq->IoSeg, 1, off, cbRead);
            AssertRC(rc2);
        }

        if (pThis->hIoLogger)
        {
            RTSGBUF SgBuf;

            RTSgBufInit(&SgBuf, &pIoReq->IoSeg, 1);
            drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, &SgBuf);
        }

        if (pThis->fTraceRequests)
            drvdiskintIoReqRemove(pThis, pIoReq);
    }
    else if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, NULL);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqWrite}
 */
static DECLCALLBACK(int) drvdiskintIoReqWrite(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbWrite)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_HANDLE_2_DRVDISKAIOREQ(pThis, hIoReq);

    pIoReq->enmTxDir    = DRVDISKAIOTXDIR_WRITE;
    pIoReq->off         = off;
    pIoReq->cbTransfer  = cbWrite;

    /* Allocate a I/O buffer if the I/O is verified.*/
    if (   pThis->fCheckConsistency
        || pThis->fValidateMemBufs
        || pThis->hIoLogger
        || pThis->fRecordWriteBeforeCompletion)
    {
        pIoReq->IoSeg.pvSeg = RTMemAlloc(cbWrite);
        pIoReq->IoSeg.cbSeg = cbWrite;

        /* Sync the memory buffer over if we should validate it. */
        if (   pThis->fValidateMemBufs
            || pThis->hIoLogger
            || pThis->fRecordWriteBeforeCompletion)
        {
            RTSGBUF SgBuf;

            RTSgBufInit(&SgBuf, &pIoReq->IoSeg, 1);
            int rc2 = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, hIoReq,
                                                                DISKINTEGRITY_IOREQ_HANDLE_2_UPPER_OPAQUE(pThis, hIoReq),
                                                                0, &SgBuf, cbWrite);
            AssertRC(rc2);
        }
    }

    if (pThis->fTraceRequests)
        drvdiskintIoReqAdd(pThis, pIoReq);

    drvdiskintTraceLogFireEvtWrite(pThis, (uintptr_t)hIoReq, true /* fAsync */, off, cbWrite);
    if (pThis->fRecordWriteBeforeCompletion)
    {

        int rc2 = drvdiskintWriteRecord(pThis, &pIoReq->IoSeg, 1, off, cbWrite);
        AssertRC(rc2);
    }

    int rc = pThis->pDrvMediaEx->pfnIoReqWrite(pThis->pDrvMediaEx, hIoReq, off, cbWrite);
    if (rc == VINF_SUCCESS)
    {
        /* Record the write. */
        if  (   pThis->fCheckConsistency
             && !pThis->fRecordWriteBeforeCompletion)
        {
            int rc2 = drvdiskintWriteRecord(pThis, &pIoReq->IoSeg, 1, off, cbWrite);
            AssertRC(rc2);
        }

        RTSGBUF SgBuf;
        RTSgBufInit(&SgBuf, &pIoReq->IoSeg, 1);
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, &SgBuf);
        if (pThis->fTraceRequests)
            drvdiskintIoReqRemove(pThis, pIoReq);
    }
    else if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, NULL);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFlush}
 */
static DECLCALLBACK(int) drvdiskintIoReqFlush(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    PDRVDISKAIOREQ pIoReq = DISKINTEGRITY_IOREQ_HANDLE_2_DRVDISKAIOREQ(pThis, hIoReq);

    pIoReq->enmTxDir    = DRVDISKAIOTXDIR_FLUSH;
    pIoReq->off         = 0;
    pIoReq->cbTransfer  = 0;

    if (pThis->fTraceRequests)
        drvdiskintIoReqAdd(pThis, pIoReq);

    drvdiskintTraceLogFireEvtFlush(pThis, (uintptr_t)hIoReq, true /* fAsync */);
    int rc = pThis->pDrvMediaEx->pfnIoReqFlush(pThis->pDrvMediaEx, hIoReq);
    if (rc == VINF_SUCCESS)
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, NULL);
    else if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        drvdiskintTraceLogFireEvtComplete(pThis, (uintptr_t)hIoReq, rc, NULL);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqDiscard}
 */
static DECLCALLBACK(int) drvdiskintIoReqDiscard(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, unsigned cRangesMax)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqDiscard(pThis->pDrvMediaEx, hIoReq, cRangesMax);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetActiveCount}
 */
static DECLCALLBACK(uint32_t) drvdiskintIoReqGetActiveCount(PPDMIMEDIAEX pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqGetActiveCount(pThis->pDrvMediaEx);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetSuspendedCount}
 */
static DECLCALLBACK(uint32_t) drvdiskintIoReqGetSuspendedCount(PPDMIMEDIAEX pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqGetSuspendedCount(pThis->pDrvMediaEx);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedStart}
 */
static DECLCALLBACK(int) drvdiskintIoReqQuerySuspendedStart(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqQuerySuspendedStart(pThis->pDrvMediaEx, phIoReq, ppvIoReqAlloc);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedNext}
 */
static DECLCALLBACK(int) drvdiskintIoReqQuerySuspendedNext(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                           PPDMMEDIAEXIOREQ phIoReqNext, void **ppvIoReqAllocNext)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqQuerySuspendedNext(pThis->pDrvMediaEx, hIoReq, phIoReqNext, ppvIoReqAllocNext);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedSave}
 */
static DECLCALLBACK(int) drvdiskintIoReqSuspendedSave(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqSuspendedSave(pThis->pDrvMediaEx, pSSM, hIoReq);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedLoad}
 */
static DECLCALLBACK(int) drvdiskintIoReqSuspendedLoad(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMediaEx);
    return pThis->pDrvMediaEx->pfnIoReqSuspendedLoad(pThis->pDrvMediaEx, pSSM, hIoReq);
}

/* -=-=-=-=- IMount -=-=-=-=- */

/** @interface_method_impl{PDMIMOUNT,pfnUnmount} */
static DECLCALLBACK(int) drvdiskintUnmount(PPDMIMOUNT pInterface, bool fForce, bool fEject)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMount);
    return pThis->pDrvMount->pfnUnmount(pThis->pDrvMount, fForce, fEject);
}

/** @interface_method_impl{PDMIMOUNT,pfnIsMounted} */
static DECLCALLBACK(bool) drvdiskintIsMounted(PPDMIMOUNT pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMount);
    return pThis->pDrvMount->pfnIsMounted(pThis->pDrvMount);
}

/** @interface_method_impl{PDMIMOUNT,pfnLock} */
static DECLCALLBACK(int) drvdiskintLock(PPDMIMOUNT pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMount);
    return pThis->pDrvMount->pfnLock(pThis->pDrvMount);
}

/** @interface_method_impl{PDMIMOUNT,pfnUnlock} */
static DECLCALLBACK(int) drvdiskintUnlock(PPDMIMOUNT pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMount);
    return pThis->pDrvMount->pfnUnlock(pThis->pDrvMount);
}

/** @interface_method_impl{PDMIMOUNT,pfnIsLocked} */
static DECLCALLBACK(bool) drvdiskintIsLocked(PPDMIMOUNT pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMount);
    return pThis->pDrvMount->pfnIsLocked(pThis->pDrvMount);
}

/* -=-=-=-=- IMountNotify -=-=-=-=- */

/** @interface_method_impl{PDMIMOUNTNOTIFY,pfnMountNotify} */
static DECLCALLBACK(void) drvdiskintMountNotify(PPDMIMOUNTNOTIFY pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMountNotify);
    pThis->pDrvMountNotify->pfnMountNotify(pThis->pDrvMountNotify);
}

/** @interface_method_impl{PDMIMOUNTNOTIFY,pfnUnmountNotify} */
static DECLCALLBACK(void) drvdiskintUnmountNotify(PPDMIMOUNTNOTIFY pInterface)
{
    PDRVDISKINTEGRITY pThis = RT_FROM_MEMBER(pInterface, DRVDISKINTEGRITY, IMountNotify);
    pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
}

/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *)  drvdiskintQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS        pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVDISKINTEGRITY pThis = PDMINS_2_DATA(pDrvIns, PDRVDISKINTEGRITY);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIA, &pThis->IMedia);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pThis->IMediaPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pThis->IMediaExPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEX, pThis->pDrvMediaEx ? &pThis->IMediaEx : NULL);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUNT, pThis->pDrvMount ? &pThis->IMount : NULL);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUNTNOTIFY, &pThis->IMountNotify);
    return NULL;
}


/* -=-=-=-=- driver interface -=-=-=-=- */

static DECLCALLBACK(int) drvdiskintTreeDestroy(PAVLRFOFFNODECORE pNode, void *pvUser)
{
    PDRVDISKSEGMENT pSeg = (PDRVDISKSEGMENT)pNode;

    RT_NOREF(pvUser);

    RTMemFree(pSeg->pbSeg);
    RTMemFree(pSeg);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNPDMDRVDESTRUCT
 */
static DECLCALLBACK(void) drvdiskintDestruct(PPDMDRVINS pDrvIns)
{
    PDRVDISKINTEGRITY pThis = PDMINS_2_DATA(pDrvIns, PDRVDISKINTEGRITY);

    if (pThis->pTreeSegments)
    {
        RTAvlrFileOffsetDestroy(pThis->pTreeSegments, drvdiskintTreeDestroy, NULL);
        RTMemFree(pThis->pTreeSegments);
    }

    if (pThis->fTraceRequests)
    {
        pThis->fRunning = false;
        RTSemEventSignal(pThis->SemEvent);
        RTSemEventDestroy(pThis->SemEvent);
    }

    if (pThis->fCheckDoubleCompletion)
    {
        /* Free all requests */
        while (pThis->papIoReq[pThis->iEntry])
        {
            RTMemFree(pThis->papIoReq[pThis->iEntry]);
            pThis->papIoReq[pThis->iEntry] = NULL;
            pThis->iEntry = (pThis->iEntry+1) % pThis->cEntries;
        }
    }

    if (pThis->hIoLogger)
        RTTraceLogWrDestroy(pThis->hIoLogger);

    if (pThis->hReqCache != NIL_RTMEMCACHE)
    {
        RTMemCacheDestroy(pThis->hReqCache);
        pThis->hReqCache = NIL_RTMEMCACHE;
    }
}

/**
 * Construct a disk integrity driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvdiskintConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVDISKINTEGRITY   pThis = PDMINS_2_DATA(pDrvIns, PDRVDISKINTEGRITY);
    PCPDMDRVHLPR3       pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvdiskintConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,  "CheckConsistency"
                                            "|TraceRequests"
                                            "|CheckIntervalMs"
                                            "|ExpireIntervalMs"
                                            "|CheckDoubleCompletions"
                                            "|HistorySize"
                                            "|IoLogType"
                                            "|IoLogFile"
                                            "|IoLogAddress"
                                            "|IoLogPort"
                                            "|IoLogData"
                                            "|PrepopulateRamDisk"
                                            "|ReadAfterWrite"
                                            "|RecordWriteBeforeCompletion"
                                            "|ValidateMemoryBuffers",
                                            "");

    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CheckConsistency", &pThis->fCheckConsistency, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "TraceRequests", &pThis->fTraceRequests, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "CheckIntervalMs", &pThis->uCheckIntervalMs, 5000); /* 5 seconds */
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "ExpireIntervalMs", &pThis->uExpireIntervalMs, 20000); /* 20 seconds */
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CheckDoubleCompletions", &pThis->fCheckDoubleCompletion, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "HistorySize", &pThis->cEntries, 512);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "PrepopulateRamDisk", &pThis->fPrepopulateRamDisk, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ReadAfterWrite", &pThis->fReadAfterWrite, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "RecordWriteBeforeCompletion", &pThis->fRecordWriteBeforeCompletion, false);
    AssertRC(rc);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ValidateMemoryBuffers", &pThis->fValidateMemBufs, false);
    AssertRC(rc);

    bool fIoLogData = false;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IoLogData", &fIoLogData, false);
    AssertRC(rc);

    char *pszIoLogType = NULL;
    char *pszIoLogFilename = NULL;
    char *pszAddress = NULL;
    uint32_t uPort = 0;
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "IoLogType", &pszIoLogType);
    if (RT_SUCCESS(rc))
    {
        if (!RTStrICmp(pszIoLogType, "File"))
        {
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "IoLogFile", &pszIoLogFilename);
            AssertRC(rc);
        }
        else if (!RTStrICmp(pszIoLogType, "Server"))
        {
            rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "IoLogAddress", &pszAddress, NULL);
            AssertRC(rc);
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IoLogPort", &uPort, 4000);
            AssertRC(rc);
        }
        else if (!RTStrICmp(pszIoLogType, "Client"))
        {
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "IoLogAddress", &pszAddress);
            AssertRC(rc);
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IoLogPort", &uPort, 4000);
            AssertRC(rc);
        }
        else
            AssertMsgFailed(("Invalid I/O log type given: %s\n", pszIoLogType));
    }
    else
        Assert(rc == VERR_CFGM_VALUE_NOT_FOUND);

    /*
     * Initialize most of the data members.
     */
    pThis->pDrvIns                       = pDrvIns;
    pThis->hReqCache                     = NIL_RTMEMCACHE;

    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface     = drvdiskintQueryInterface;

    /* IMedia */
    pThis->IMedia.pfnRead                        = drvdiskintRead;
    pThis->IMedia.pfnWrite                       = drvdiskintWrite;
    pThis->IMedia.pfnFlush                       = drvdiskintFlush;
    pThis->IMedia.pfnGetSize                     = drvdiskintGetSize;
    pThis->IMedia.pfnIsReadOnly                  = drvdiskintIsReadOnly;
    pThis->IMedia.pfnBiosIsVisible               = drvdiskintBiosIsVisible;
    pThis->IMedia.pfnBiosGetPCHSGeometry         = drvdiskintBiosGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry         = drvdiskintBiosSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry         = drvdiskintBiosGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry         = drvdiskintBiosSetLCHSGeometry;
    pThis->IMedia.pfnGetUuid                     = drvdiskintGetUuid;
    pThis->IMedia.pfnGetSectorSize               = drvdiskintGetSectorSize;
    pThis->IMedia.pfnGetType                     = drvdiskintGetType;
    pThis->IMedia.pfnReadPcBios                  = drvdiskintReadPcBios;
    pThis->IMedia.pfnIsNonRotational             = drvdiskintIsNonRotational;
    pThis->IMedia.pfnSendCmd                     = NULL;
    pThis->IMedia.pfnGetRegionCount              = drvdiskintGetRegionCount;
    pThis->IMedia.pfnQueryRegionProperties       = drvdiskintQueryRegionProperties;
    pThis->IMedia.pfnQueryRegionPropertiesForLba = drvdiskintQueryRegionPropertiesForLba;


    /* IMediaEx. */
    pThis->IMediaEx.pfnQueryFeatures            = drvdiskintQueryFeatures;
    pThis->IMediaEx.pfnNotifySuspend            = drvdiskintNotifySuspend;
    pThis->IMediaEx.pfnIoReqAllocSizeSet        = drvdiskintIoReqAllocSizeSet;
    pThis->IMediaEx.pfnIoReqAlloc               = drvdiskintIoReqAlloc;
    pThis->IMediaEx.pfnIoReqFree                = drvdiskintIoReqFree;
    pThis->IMediaEx.pfnIoReqQueryResidual       = drvdiskintIoReqQueryResidual;
    pThis->IMediaEx.pfnIoReqQueryXferSize       = drvdiskintIoReqQueryXferSize;
    pThis->IMediaEx.pfnIoReqCancelAll           = drvdiskintIoReqCancelAll;
    pThis->IMediaEx.pfnIoReqCancel              = drvdiskintIoReqCancel;
    pThis->IMediaEx.pfnIoReqRead                = drvdiskintIoReqRead;
    pThis->IMediaEx.pfnIoReqWrite               = drvdiskintIoReqWrite;
    pThis->IMediaEx.pfnIoReqFlush               = drvdiskintIoReqFlush;
    pThis->IMediaEx.pfnIoReqDiscard             = drvdiskintIoReqDiscard;
    pThis->IMediaEx.pfnIoReqGetActiveCount      = drvdiskintIoReqGetActiveCount;
    pThis->IMediaEx.pfnIoReqGetSuspendedCount   = drvdiskintIoReqGetSuspendedCount;
    pThis->IMediaEx.pfnIoReqQuerySuspendedStart = drvdiskintIoReqQuerySuspendedStart;
    pThis->IMediaEx.pfnIoReqQuerySuspendedNext  = drvdiskintIoReqQuerySuspendedNext;
    pThis->IMediaEx.pfnIoReqSuspendedSave       = drvdiskintIoReqSuspendedSave;
    pThis->IMediaEx.pfnIoReqSuspendedLoad       = drvdiskintIoReqSuspendedLoad;

    /* IMediaPort. */
    pThis->IMediaPort.pfnQueryDeviceLocation = drvdiskintQueryDeviceLocation;

    /* IMediaExPort. */
    pThis->IMediaExPort.pfnIoReqCompleteNotify     = drvdiskintIoReqCompleteNotify;
    pThis->IMediaExPort.pfnIoReqCopyFromBuf        = drvdiskintIoReqCopyFromBuf;
    pThis->IMediaExPort.pfnIoReqCopyToBuf          = drvdiskintIoReqCopyToBuf;
    pThis->IMediaExPort.pfnIoReqQueryDiscardRanges = drvdiskintIoReqQueryDiscardRanges;
    pThis->IMediaExPort.pfnIoReqStateChanged       = drvdiskintIoReqStateChanged;

    /* IMount */
    pThis->IMount.pfnUnmount                       = drvdiskintUnmount;
    pThis->IMount.pfnIsMounted                     = drvdiskintIsMounted;
    pThis->IMount.pfnLock                          = drvdiskintLock;
    pThis->IMount.pfnUnlock                        = drvdiskintUnlock;
    pThis->IMount.pfnIsLocked                      = drvdiskintIsLocked;

    /* IMountNotify */
    pThis->IMountNotify.pfnMountNotify             = drvdiskintMountNotify;
    pThis->IMountNotify.pfnUnmountNotify           = drvdiskintUnmountNotify;

    /* Query the media port interface above us. */
    pThis->pDrvMediaPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAPORT);
    if (!pThis->pDrvMediaPort)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW,
                                N_("No media port interface above"));

    /* Try to attach extended media port interface above.*/
    pThis->pDrvMediaExPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAEXPORT);

    rc = RTMemCacheCreate(&pThis->hReqCache, sizeof(DRVDISKAIOREQ), 0, UINT32_MAX,
                          NULL, NULL, NULL, 0);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Failed to create request tracking structure cache"));

    /*
     * Try attach driver below and query it's media interface.
     */
    PPDMIBASE pBase;
    rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pBase);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Failed to attach driver below us! %Rrc"), rc);

    pThis->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIA);
    if (!pThis->pDrvMedia)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW,
                                N_("No media or async media interface below"));

    pThis->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIAEX);
    pThis->pDrvMount   = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMOUNT);

    if (pThis->pDrvMedia->pfnDiscard)
        pThis->IMedia.pfnDiscard = drvdiskintDiscard;

    if (pThis->fCheckConsistency)
    {
        /* Create the AVL tree. */
        pThis->pTreeSegments = (PAVLRFOFFTREE)RTMemAllocZ(sizeof(AVLRFOFFTREE));
        if (!pThis->pTreeSegments)
            rc = VERR_NO_MEMORY;
    }

    if (pThis->fTraceRequests)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->apReqActive); i++)
        {
            pThis->apReqActive[i].pIoReq  = NULL;
            pThis->apReqActive[i].tsStart = 0;
        }

        pThis->iNextFreeSlot = 0;

        /* Init event semaphore. */
        rc = RTSemEventCreate(&pThis->SemEvent);
        AssertRC(rc);
        pThis->fRunning = true;
        rc = RTThreadCreate(&pThis->hThread, drvdiskIntIoReqExpiredCheck, pThis,
                            0, RTTHREADTYPE_INFREQUENT_POLLER, 0, "DiskIntegrity");
        AssertRC(rc);
    }

    if (pThis->fCheckDoubleCompletion)
    {
        pThis->iEntry = 0;
        pThis->papIoReq = (PDRVDISKAIOREQ *)RTMemAllocZ(pThis->cEntries * sizeof(PDRVDISKAIOREQ));
        AssertPtr(pThis->papIoReq);
    }

    if (pszIoLogType)
    {
        if (!RTStrICmp(pszIoLogType, "File"))
        {
            rc = RTTraceLogWrCreateFile(&pThis->hIoLogger, NULL, pszIoLogFilename);
            PDMDrvHlpMMHeapFree(pDrvIns, pszIoLogFilename);
        }
        else if (!RTStrICmp(pszIoLogType, "Server"))
        {
            rc = RTTraceLogWrCreateTcpServer(&pThis->hIoLogger, NULL, pszAddress, uPort);
            if (pszAddress)
                PDMDrvHlpMMHeapFree(pDrvIns, pszAddress);
        }
        else if (!RTStrICmp(pszIoLogType, "Client"))
        {
            rc = RTTraceLogWrCreateTcpClient(&pThis->hIoLogger, NULL, pszAddress, uPort);
            PDMDrvHlpMMHeapFree(pDrvIns, pszAddress);
        }
        else
            AssertMsgFailed(("Invalid I/O log type given: %s\n", pszIoLogType));

        PDMDrvHlpMMHeapFree(pDrvIns, pszIoLogType);
    }

    /* Read in all data before the start if requested. */
    if (pThis->fPrepopulateRamDisk)
    {
        uint64_t cbDisk = 0;

        LogRel(("DiskIntegrity: Prepopulating RAM disk, this will take some time...\n"));

        cbDisk = pThis->pDrvMedia->pfnGetSize(pThis->pDrvMedia);
        if (cbDisk)
        {
            uint64_t off = 0;
            uint8_t abBuffer[_64K];
            RTSGSEG Seg;

            Seg.pvSeg = abBuffer;

            while (cbDisk)
            {
                size_t cbThisRead = RT_MIN(cbDisk, sizeof(abBuffer));

                rc = pThis->pDrvMedia->pfnRead(pThis->pDrvMedia, off, abBuffer, cbThisRead);
                if (RT_FAILURE(rc))
                    break;

                if (ASMBitFirstSet(abBuffer, sizeof(abBuffer) * 8) != -1)
                {
                    Seg.cbSeg = cbThisRead;
                    rc = drvdiskintWriteRecord(pThis, &Seg, 1,
                                               off, cbThisRead);
                    if (RT_FAILURE(rc))
                        break;
                }

                cbDisk -= cbThisRead;
                off    += cbThisRead;
            }

            LogRel(("DiskIntegrity: Prepopulating RAM disk finished with %Rrc\n", rc));
        }
        else
            return PDMDRV_SET_ERROR(pDrvIns, VERR_INTERNAL_ERROR,
                                    N_("DiskIntegrity: Error querying the media size below"));
    }

    return rc;
}


/**
 * Block driver registration record.
 */
const PDMDRVREG g_DrvDiskIntegrity =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "DiskIntegrity",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Disk integrity driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_BLOCK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVDISKINTEGRITY),
    /* pfnConstruct */
    drvdiskintConstruct,
    /* pfnDestruct */
    drvdiskintDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

