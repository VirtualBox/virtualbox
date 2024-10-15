/* $Id: DrvRamDisk.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: RAM disk driver.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/memcache.h>
#include <iprt/message.h>
#include <iprt/sg.h>
#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/asm.h>
#include <iprt/req.h>
#include <iprt/thread.h>

#include "VBoxDD.h"
#include "IOBufMgmt.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Pointer to a ramdisk driver instance. */
typedef struct DRVRAMDISK *PDRVRAMDISK;

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
} DRVDISKSEGMENT, *PDRVDISKSEGMENT;

/**
 * VD I/O request state.
 */
typedef enum VDIOREQSTATE
{
    /** Invalid. */
    VDIOREQSTATE_INVALID = 0,
    /** The request is not in use and resides on the free list. */
    VDIOREQSTATE_FREE,
    /** The request was just allocated and is not active. */
    VDIOREQSTATE_ALLOCATED,
    /** The request was allocated and is in use. */
    VDIOREQSTATE_ACTIVE,
    /** The request was suspended and is not actively processed. */
    VDIOREQSTATE_SUSPENDED,
    /** The request is in the last step of completion and syncs memory. */
    VDIOREQSTATE_COMPLETING,
    /** The request completed. */
    VDIOREQSTATE_COMPLETED,
    /** The request was aborted but wasn't returned as complete from the storage
     * layer below us. */
    VDIOREQSTATE_CANCELED,
    /** 32bit hack. */
    VDIOREQSTATE_32BIT_HACK = 0x7fffffff
} VDIOREQSTATE;

/**
 * VD I/O Request.
 */
typedef struct PDMMEDIAEXIOREQINT
{
    /** List node for the list of allocated requests. */
    RTLISTNODE                    NdAllocatedList;
    /** List for requests waiting for I/O memory or on the redo list. */
    RTLISTNODE                    NdLstWait;
    /** I/O request type. */
    PDMMEDIAEXIOREQTYPE           enmType;
    /** Request state. */
    volatile VDIOREQSTATE         enmState;
    /** I/O request ID. */
    PDMMEDIAEXIOREQID             uIoReqId;
    /** Pointer to the disk container. */
    PDRVRAMDISK                   pDisk;
    /** Flags. */
    uint32_t                      fFlags;
    /** Timestamp when the request was submitted. */
    uint64_t                      tsSubmit;
    /** Type dependent data. */
    union
    {
        /** Read/Write request sepcific data. */
        struct
        {
            /** Start offset of the request. */
            uint64_t                      offStart;
            /** Size of the request. */
            size_t                        cbReq;
            /** Size left for this request. */
            size_t                        cbReqLeft;
            /** Size of the allocated I/O buffer. */
            size_t                        cbIoBuf;
            /** I/O buffer descriptor. */
            IOBUFDESC                     IoBuf;
        } ReadWrite;
        /** Discard specific data. */
        struct
        {
            /** Pointer to array of ranges to discard. */
            PRTRANGE                      paRanges;
            /** Number of ranges to discard. */
            unsigned                      cRanges;
        } Discard;
    };
    /** Allocator specific memory - variable size. */
    uint8_t                       abAlloc[1];
} PDMMEDIAEXIOREQINT;
/** Pointer to a VD I/O request. */
typedef PDMMEDIAEXIOREQINT *PPDMMEDIAEXIOREQINT;

/**
 * Structure for holding a list of allocated requests.
 */
typedef struct VDLSTIOREQALLOC
{
    /** Mutex protecting the table of allocated requests. */
    RTSEMFASTMUTEX           hMtxLstIoReqAlloc;
    /** List anchor. */
    RTLISTANCHOR             LstIoReqAlloc;
} VDLSTIOREQALLOC;
typedef VDLSTIOREQALLOC *PVDLSTIOREQALLOC;

/** Number of bins for allocated requests. */
#define DRVVD_VDIOREQ_ALLOC_BINS    8

/**
 * Disk integrity driver instance data.
 *
 * @implements  PDMIMEDIA
 */
typedef struct DRVRAMDISK
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

    /** Flag whether the RAM disk was pre allocated. */
    bool                    fPreallocRamDisk;
    /** Flag whether to report a non totating medium. */
    bool                    fNonRotational;
    /** AVL tree containing the disk blocks to check. */
    PAVLRFOFFTREE           pTreeSegments;
    /** Size of the disk. */
    uint64_t                cbDisk;
    /** Size of one sector. */
    uint32_t                cbSector;

    /** Worker request queue. */
    RTREQQUEUE               hReqQ;
    /** Worker thread for async requests. */
    RTTHREAD                 hThrdWrk;

    /** @name IMEDIAEX interface support specific members.
     * @{ */
    /** Pointer to the IMEDIAEXPORT interface above us. */
    PPDMIMEDIAEXPORT         pDrvMediaExPort;
    /** Our extended media interface. */
    PDMIMEDIAEX              IMediaEx;
    /** Memory cache for the I/O requests. */
    RTMEMCACHE               hIoReqCache;
    /** I/O buffer manager. */
    IOBUFMGR                 hIoBufMgr;
    /** Active request counter. */
    volatile uint32_t        cIoReqsActive;
    /** Bins for allocated requests. */
    VDLSTIOREQALLOC          aIoReqAllocBins[DRVVD_VDIOREQ_ALLOC_BINS];
    /** List of requests for I/O memory to be available - VDIOREQ::NdLstWait. */
    RTLISTANCHOR             LstIoReqIoBufWait;
    /** Critical section protecting the list of requests waiting for I/O memory. */
    RTCRITSECT               CritSectIoReqsIoBufWait;
    /** Number of requests waiting for a I/O buffer. */
    volatile uint32_t        cIoReqsWaiting;
    /** Flag whether we have to resubmit requests on resume because the
     * VM was suspended due to a recoverable I/O error.
     */
    volatile bool            fRedo;
    /** List of requests we have to redo. */
    RTLISTANCHOR             LstIoReqRedo;
    /** Criticial section protecting the list of waiting requests. */
    RTCRITSECT               CritSectIoReqRedo;
    /** Number of errors logged so far. */
    unsigned                 cErrors;
    /** @} */

} DRVRAMDISK;


static void drvramdiskMediaExIoReqComplete(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq,
                                           int rcReq);

/**
 * Record a successful write to the virtual disk.
 *
 * @returns VBox status code.
 * @param   pThis    Disk integrity driver instance data.
 * @param   pSgBuf   The S/G buffer holding the data to write.
 * @param   off      Start offset.
 * @param   cbWrite  Number of bytes to record.
 */
static int drvramdiskWriteWorker(PDRVRAMDISK pThis, PRTSGBUF pSgBuf,
                                 uint64_t off, size_t cbWrite)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pSgBuf=%#p off=%llx cbWrite=%u\n",
                 pThis, pSgBuf, off, cbWrite));

    /* Update the segments */
    size_t cbLeft   = cbWrite;
    RTFOFF offCurr  = (RTFOFF)off;

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
            pSeg = (PDRVDISKSEGMENT)RTMemAllocZ(sizeof(DRVDISKSEGMENT));
            if (pSeg)
            {
                pSeg->Core.Key      = offCurr;
                pSeg->Core.KeyLast  = offCurr + (RTFOFF)cbRange - 1;
                pSeg->cbSeg         = cbRange;
                pSeg->pbSeg         = (uint8_t *)RTMemAllocZ(cbRange);
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
            size_t cbCopied = RTSgBufCopyToBuf(pSgBuf, pSeg->pbSeg + offSeg, cbRange);
            Assert(cbCopied == cbRange); RT_NOREF(cbCopied);
        }
        else
            RTSgBufAdvance(pSgBuf, cbRange);

        offCurr += cbRange;
        cbLeft  -= cbRange;
    }

    return rc;
}

/**
 * Read data from the ram disk.
 *
 * @returns VBox status code.
 * @param   pThis    RAM disk driver instance data.
 * @param   pSgBuf   The S/G buffer to store the data.
 * @param   off      Start offset.
 * @param   cbRead   Number of bytes to read.
 */
static int drvramdiskReadWorker(PDRVRAMDISK pThis, PRTSGBUF pSgBuf,
                                uint64_t off, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pSgBuf=%#p off=%llx cbRead=%u\n",
                 pThis, pSgBuf, off, cbRead));

    Assert(off % 512 == 0);
    Assert(cbRead % 512 == 0);

    /* Compare read data */
    size_t cbLeft   = cbRead;
    RTFOFF offCurr  = (RTFOFF)off;

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
            if (   !pSeg
                || offCurr + (RTFOFF)cbLeft <= pSeg->Core.Key)
                cbRange = cbLeft;
            else
                cbRange = pSeg->Core.Key - offCurr;

            /* No segment means everything should be 0 for this part. */
            RTSgBufSet(pSgBuf, 0, cbRange);
        }
        else
        {
            fCmp    = true;
            offSeg  = offCurr - pSeg->Core.Key;
            cbRange = RT_MIN(cbLeft, (size_t)(pSeg->Core.KeyLast + 1 - offCurr));

            RTSGSEG Seg;
            RTSGBUF SgBufSrc;

            Seg.cbSeg = cbRange;
            Seg.pvSeg = pSeg->pbSeg + offSeg;

            RTSgBufInit(&SgBufSrc, &Seg, 1);
            RTSgBufCopy(pSgBuf, &SgBufSrc, cbRange);
        }

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
static int drvramdiskDiscardRecords(PDRVRAMDISK pThis, PCRTRANGE paRanges, unsigned cRanges)
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
                    RTMemFree(pSeg);
                }
                else if (cbPreLeft && !cbPostLeft)
                {
                    /* Realloc to new size and insert. */
                    LogFlowFunc(("Realloc segment pSeg=%#p\n", pSeg));
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPreLeft);
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, sizeof(DRVDISKSEGMENT));
                    pSeg->Core.KeyLast = pSeg->Core.Key + cbPreLeft - 1;
                    pSeg->cbSeg = cbPreLeft;
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    Assert(fInserted); RT_NOREF(fInserted);
                }
                else if (!cbPreLeft && cbPostLeft)
                {
                    /* Move data to the front and realloc. */
                    LogFlowFunc(("Move data and realloc segment pSeg=%#p\n", pSeg));
                    memmove(pSeg->pbSeg, pSeg->pbSeg + cbRange, cbPostLeft);
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, sizeof(DRVDISKSEGMENT));
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPostLeft);
                    pSeg->Core.Key += cbRange;
                    pSeg->cbSeg = cbPostLeft;
                    bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSeg->Core);
                    Assert(fInserted); RT_NOREF(fInserted);
                }
                else
                {
                    /* Split the segment into 2 new segments. */
                    LogFlowFunc(("Split segment pSeg=%#p\n", pSeg));
                    PDRVDISKSEGMENT pSegPost = (PDRVDISKSEGMENT)RTMemAllocZ(sizeof(DRVDISKSEGMENT));
                    if (pSegPost)
                    {
                        pSegPost->Core.Key      = pSeg->Core.Key + cbPreLeft + cbRange;
                        pSegPost->Core.KeyLast  = pSeg->Core.KeyLast;
                        pSegPost->cbSeg         = cbPostLeft;
                        pSegPost->pbSeg         = (uint8_t *)RTMemAllocZ(cbPostLeft);
                        if (!pSegPost->pbSeg)
                            RTMemFree(pSegPost);
                        else
                        {
                            memcpy(pSegPost->pbSeg, pSeg->pbSeg + cbPreLeft + cbRange, cbPostLeft);
                            bool fInserted = RTAvlrFileOffsetInsert(pThis->pTreeSegments, &pSegPost->Core);
                            Assert(fInserted); RT_NOREF(fInserted);
                        }
                    }

                    /* Shrink the current segment. */
                    pSeg->pbSeg = (uint8_t *)RTMemRealloc(pSeg->pbSeg, cbPreLeft);
                    pSeg = (PDRVDISKSEGMENT)RTMemRealloc(pSeg, sizeof(DRVDISKSEGMENT));
                    pSeg->Core.KeyLast = pSeg->Core.Key + cbPreLeft - 1;
                    pSeg->cbSeg = cbPreLeft;
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

/* -=-=-=-=- IMedia -=-=-=-=- */


/*********************************************************************************************************************************
*   Media interface methods                                                                                                      *
*********************************************************************************************************************************/

/** @copydoc PDMIMEDIA::pfnRead */
static DECLCALLBACK(int) drvramdiskRead(PPDMIMEDIA pInterface,
                                        uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    RTSGSEG Seg;
    RTSGBUF SgBuf;

    Seg.cbSeg = cbRead;
    Seg.pvSeg = pvBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return drvramdiskReadWorker(pThis, &SgBuf, off, cbRead);
}

/** @copydoc PDMIMEDIA::pfnWrite */
static DECLCALLBACK(int) drvramdiskWrite(PPDMIMEDIA pInterface,
                                         uint64_t off, const void *pvBuf,
                                         size_t cbWrite)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    RTSGSEG Seg;
    RTSGBUF SgBuf;

    Seg.cbSeg = cbWrite;
    Seg.pvSeg = (void *)pvBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return drvramdiskWriteWorker(pThis, &SgBuf, off, cbWrite);
}

/** @copydoc PDMIMEDIA::pfnFlush */
static DECLCALLBACK(int) drvramdiskFlush(PPDMIMEDIA pInterface)
{
    RT_NOREF1(pInterface);
    /* Nothing to do here. */
    return VINF_SUCCESS;
}

/** @copydoc PDMIMEDIA::pfnGetSize */
static DECLCALLBACK(uint64_t) drvramdiskGetSize(PPDMIMEDIA pInterface)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    return pThis->cbDisk;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosIsVisible} */
static DECLCALLBACK(bool) drvramdiskBiosIsVisible(PPDMIMEDIA pInterface)
{
    RT_NOREF1(pInterface);
    return false;
}

/** @copydoc PDMIMEDIA::pfnGetType */
static DECLCALLBACK(PDMMEDIATYPE) drvramdiskGetType(PPDMIMEDIA pInterface)
{
    RT_NOREF1(pInterface);
    return PDMMEDIATYPE_HARD_DISK;
}

/** @copydoc PDMIMEDIA::pfnIsReadOnly */
static DECLCALLBACK(bool) drvramdiskIsReadOnly(PPDMIMEDIA pInterface)
{
    RT_NOREF1(pInterface);
    return false; /** @todo */
}

/** @copydoc PDMIMEDIA::pfnBiosGetPCHSGeometry */
static DECLCALLBACK(int) drvramdiskBiosGetPCHSGeometry(PPDMIMEDIA pInterface,
                                                       PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    RT_NOREF2(pInterface, pPCHSGeometry);
    return VERR_PDM_GEOMETRY_NOT_SET;
}

/** @copydoc PDMIMEDIA::pfnBiosSetPCHSGeometry */
static DECLCALLBACK(int) drvramdiskBiosSetPCHSGeometry(PPDMIMEDIA pInterface,
                                                       PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    RT_NOREF2(pInterface, pPCHSGeometry);
    return VERR_NOT_IMPLEMENTED;
}

/** @copydoc PDMIMEDIA::pfnBiosGetLCHSGeometry */
static DECLCALLBACK(int) drvramdiskBiosGetLCHSGeometry(PPDMIMEDIA pInterface,
                                                       PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    RT_NOREF2(pInterface, pLCHSGeometry);
    return VERR_PDM_GEOMETRY_NOT_SET;
}

/** @copydoc PDMIMEDIA::pfnBiosSetLCHSGeometry */
static DECLCALLBACK(int) drvramdiskBiosSetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    RT_NOREF2(pInterface, pLCHSGeometry);
    return VERR_NOT_IMPLEMENTED;
}

/** @copydoc PDMIMEDIA::pfnGetUuid */
static DECLCALLBACK(int) drvramdiskGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    RT_NOREF1(pInterface);
    return RTUuidClear(pUuid);
}

/** @copydoc PDMIMEDIA::pfnGetSectorSize */
static DECLCALLBACK(uint32_t) drvramdiskGetSectorSize(PPDMIMEDIA pInterface)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    return pThis->cbSector;
}

/** @copydoc PDMIMEDIA::pfnDiscard */
static DECLCALLBACK(int) drvramdiskDiscard(PPDMIMEDIA pInterface, PCRTRANGE paRanges, unsigned cRanges)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    return drvramdiskDiscardRecords(pThis, paRanges, cRanges);
}

/** @copydoc PDMIMEDIA::pfnReadPcBios */
static DECLCALLBACK(int) drvramdiskReadPcBios(PPDMIMEDIA pInterface,
                                              uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    RTSGSEG Seg;
    RTSGBUF SgBuf;

    Seg.cbSeg = cbRead;
    Seg.pvSeg = pvBuf;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return drvramdiskReadWorker(pThis, &SgBuf, off, cbRead);
}

/** @interface_method_impl{PDMIMEDIA,pfnIsNonRotational} */
static DECLCALLBACK(bool) drvramdiskIsNonRotational(PPDMIMEDIA pInterface)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    return pThis->fNonRotational;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetRegionCount} */
static DECLCALLBACK(uint32_t) drvramdiskGetRegionCount(PPDMIMEDIA pInterface)
{
    RT_NOREF(pInterface);
    return 1;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionProperties} */
static DECLCALLBACK(int) drvramdiskQueryRegionProperties(PPDMIMEDIA pInterface, uint32_t uRegion, uint64_t *pu64LbaStart,
                                                         uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                         PVDREGIONDATAFORM penmDataForm)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    AssertReturn(uRegion == 0, VERR_INVALID_PARAMETER);
    if (pu64LbaStart)
        *pu64LbaStart = 0;
    if (pcBlocks)
        *pcBlocks = pThis->cbDisk / pThis->cbSector;
    if (pcbBlock)
        *pcbBlock = pThis->cbSector;
    if (penmDataForm)
        *penmDataForm = VDREGIONDATAFORM_RAW;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionPropertiesForLba} */
static DECLCALLBACK(int) drvramdiskQueryRegionPropertiesForLba(PPDMIMEDIA pInterface, uint64_t u64LbaStart,
                                                               uint32_t *puRegion, uint64_t *pcBlocks,
                                                               uint64_t *pcbBlock, PVDREGIONDATAFORM penmDataForm)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMedia);
    RT_NOREF(u64LbaStart);
    if (puRegion)
        *puRegion = 0;
    if (pcBlocks)
        *pcBlocks = pThis->cbDisk / pThis->cbSector;
    if (pcbBlock)
        *pcbBlock = pThis->cbSector;
    if (penmDataForm)
        *penmDataForm = VDREGIONDATAFORM_RAW;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Extended media interface methods                                                                                             *
*********************************************************************************************************************************/

static void drvramdiskMediaExIoReqWarningOutOfMemory(PPDMDRVINS pDrvIns)
{
    int rc;
    LogRel(("RamDisk#%u: Out of memory\n", pDrvIns->iInstance));
    rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvRamDisk_OOM",
                                    N_("There is not enough free memory for the ramdisk"));
    AssertRC(rc);
}

/**
 * Checks whether a given status code indicates a recoverable error
 * suspending the VM if it is.
 *
 * @returns Flag indicating whether the status code is a recoverable error
 *          (full disk, broken network connection).
 * @param   pThis     VBox disk container instance data.
 * @param   rc        Status code to check.
 */
static bool drvramdiskMediaExIoReqIsRedoSetWarning(PDRVRAMDISK pThis, int rc)
{
    if (rc == VERR_NO_MEMORY)
    {
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvramdiskMediaExIoReqWarningOutOfMemory(pThis->pDrvIns);
        return true;
    }

    return false;
}

/**
 * Syncs the memory buffers between the I/O request allocator and the internal buffer.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to sync.
 * @param   fToIoBuf  Flag indicating the sync direction.
 *                    true to copy data from the allocators buffer to our internal buffer.
 *                    false for the other direction.
 */
DECLINLINE(int) drvramdiskMediaExIoReqBufSync(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, bool fToIoBuf)
{
    int rc = VINF_SUCCESS;

    Assert(pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE);

    /* Make sure the buffer is reset. */
    RTSgBufReset(&pIoReq->ReadWrite.IoBuf.SgBuf);

    if (fToIoBuf)
        rc = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                       (uint32_t)(pIoReq->ReadWrite.cbReq - pIoReq->ReadWrite.cbReqLeft),
                                                       &pIoReq->ReadWrite.IoBuf.SgBuf,
                                                       RT_MIN(pIoReq->ReadWrite.cbIoBuf, pIoReq->ReadWrite.cbReqLeft));
    else
        rc = pThis->pDrvMediaExPort->pfnIoReqCopyFromBuf(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                         (uint32_t)(pIoReq->ReadWrite.cbReq - pIoReq->ReadWrite.cbReqLeft),
                                                         &pIoReq->ReadWrite.IoBuf.SgBuf,
                                                         RT_MIN(pIoReq->ReadWrite.cbIoBuf, pIoReq->ReadWrite.cbReqLeft));

    RTSgBufReset(&pIoReq->ReadWrite.IoBuf.SgBuf);
    return rc;
}

/**
 * Hashes the I/O request ID to an index for the allocated I/O request bin.
 */
DECLINLINE(unsigned) drvramdiskMediaExIoReqIdHash(PDMMEDIAEXIOREQID uIoReqId)
{
    return uIoReqId % DRVVD_VDIOREQ_ALLOC_BINS; /** @todo Find something better? */
}

/**
 * Inserts the given I/O request in to the list of allocated I/O requests.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to insert.
 */
static int drvramdiskMediaExIoReqInsert(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;
    unsigned idxBin = drvramdiskMediaExIoReqIdHash(pIoReq->uIoReqId);

    rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        /* Search for conflicting I/O request ID. */
        PPDMMEDIAEXIOREQINT pIt;
        RTListForEach(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, pIt, PDMMEDIAEXIOREQINT, NdAllocatedList)
        {
            if (RT_UNLIKELY(pIt->uIoReqId == pIoReq->uIoReqId))
            {
                rc = VERR_PDM_MEDIAEX_IOREQID_CONFLICT;
                break;
            }
        }
        if (RT_SUCCESS(rc))
            RTListAppend(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, &pIoReq->NdAllocatedList);
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * Removes the given I/O request from the list of allocated I/O requests.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to insert.
 */
static int drvramdiskMediaExIoReqRemove(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;
    unsigned idxBin = drvramdiskMediaExIoReqIdHash(pIoReq->uIoReqId);

    rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        RTListNodeRemove(&pIoReq->NdAllocatedList);
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * I/O request completion worker.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to complete.
 * @param   rcReq     The status code the request completed with.
 * @param   fUpNotify Flag whether to notify the driver/device above us about the completion.
 */
static int drvramdiskMediaExIoReqCompleteWorker(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, int rcReq, bool fUpNotify)
{
    int rc;
    bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_COMPLETING, VDIOREQSTATE_ACTIVE);
    if (fXchg)
        ASMAtomicDecU32(&pThis->cIoReqsActive);
    else
    {
        Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
        rcReq = VERR_PDM_MEDIAEX_IOREQ_CANCELED;
    }

    ASMAtomicXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_COMPLETED);

    /*
     * Leave a release log entry if the request was active for more than 25 seconds
     * (30 seconds is the timeout of the guest).
     */
    uint64_t tsNow = RTTimeMilliTS();
    if (tsNow - pIoReq->tsSubmit >= 25 * 1000)
    {
        const char *pcszReq = NULL;

        switch (pIoReq->enmType)
        {
            case PDMMEDIAEXIOREQTYPE_READ:
                pcszReq = "Read";
                break;
            case PDMMEDIAEXIOREQTYPE_WRITE:
                pcszReq = "Write";
                break;
            case PDMMEDIAEXIOREQTYPE_FLUSH:
                pcszReq = "Flush";
                break;
            case PDMMEDIAEXIOREQTYPE_DISCARD:
                pcszReq = "Discard";
                break;
            default:
                pcszReq = "<Invalid>";
        }

        LogRel(("RamDisk#%u: %s request was active for %llu seconds\n",
                pThis->pDrvIns->iInstance, pcszReq, (tsNow - pIoReq->tsSubmit) / 1000));
    }

    if (RT_FAILURE(rcReq))
    {
        /* Log the error. */
        if (pThis->cErrors++ < 100)
        {
            if (rcReq == VERR_PDM_MEDIAEX_IOREQ_CANCELED)
            {
                if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                    LogRel(("RamDisk#%u: Aborted flush returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else
                    LogRel(("RamDisk#%u: Aborted %s (%u bytes left) returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance,
                            pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                            ? "read"
                            : "write",
                            pIoReq->ReadWrite.cbReqLeft, rcReq));
            }
            else
            {
                if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                    LogRel(("RamDisk#%u: Flush returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else
                    LogRel(("RamDisk#%u: %s (%u bytes left) returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance,
                            pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                            ? "Read"
                            : "Write",
                            pIoReq->ReadWrite.cbReqLeft, rcReq));
            }
        }
    }

    if (fUpNotify)
    {
        rc = pThis->pDrvMediaExPort->pfnIoReqCompleteNotify(pThis->pDrvMediaExPort,
                                                            pIoReq, &pIoReq->abAlloc[0], rcReq);
        AssertRC(rc);
    }

    return rcReq;
}

/**
 * Allocates a memory buffer suitable for I/O for the given request.
 *
 * @returns VBox status code.
 * @retval  VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS if there is no I/O memory available to allocate and
 *          the request was placed on a waiting list.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to allocate memory for.
 * @param   cb        Size of the buffer.
 */
DECLINLINE(int) drvramdiskMediaExIoReqBufAlloc(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, size_t cb)
{
    int rc = IOBUFMgrAllocBuf(pThis->hIoBufMgr, &pIoReq->ReadWrite.IoBuf, cb, &pIoReq->ReadWrite.cbIoBuf);
    if (rc == VERR_NO_MEMORY)
    {
        RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
        RTListAppend(&pThis->LstIoReqIoBufWait, &pIoReq->NdLstWait);
        RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
        ASMAtomicIncU32(&pThis->cIoReqsWaiting);
        rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
    }

    return rc;
}

/**
 * Worker for a read request.
 *
 * @returns VBox status code.
 * @param   pThis     RAM disk container instance data.
 * @param   pIoReq    The read request.
 */
static DECLCALLBACK(int) drvramdiskIoReqReadWorker(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    size_t cbReqIo = RT_MIN(pIoReq->ReadWrite.cbReqLeft, pIoReq->ReadWrite.cbIoBuf);
    int rc = drvramdiskReadWorker(pThis, &pIoReq->ReadWrite.IoBuf.SgBuf, pIoReq->ReadWrite.offStart,
                                  cbReqIo);
    drvramdiskMediaExIoReqComplete(pThis, pIoReq, rc);
    return VINF_SUCCESS;
}

/**
 * Worker for a read request.
 *
 * @returns VBox status code.
 * @param   pThis     RAM disk container instance data.
 * @param   pIoReq    The read request.
 */
static DECLCALLBACK(int) drvramdiskIoReqWriteWorker(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    size_t cbReqIo = RT_MIN(pIoReq->ReadWrite.cbReqLeft, pIoReq->ReadWrite.cbIoBuf);
    int rc = drvramdiskWriteWorker(pThis, &pIoReq->ReadWrite.IoBuf.SgBuf, pIoReq->ReadWrite.offStart,
                                   cbReqIo);
    drvramdiskMediaExIoReqComplete(pThis, pIoReq, rc);
    return VINF_SUCCESS;
}

/**
 * Kicks the worker thread out of the loop.
 *
 * @returns VBox status code.
 * @retval  VERR_CANCELLED;
 */
static DECLCALLBACK(int) drvramdiskDestructQueue(void)
{
    return VERR_CANCELLED;
}

/**
 * Processes a read/write request.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 * @param   fUpNotify Flag whether to notify the driver/device above us about the completion.
 */
static int drvramdiskMediaExIoReqReadWriteProcess(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, bool fUpNotify)
{
    int rc = VINF_SUCCESS;

    Assert(pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE);

    while (   pIoReq->ReadWrite.cbReqLeft
           && rc == VINF_SUCCESS)
    {
        if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
            rc = RTReqQueueCallEx(pThis->hReqQ, NULL, 0, RTREQFLAGS_NO_WAIT, (PFNRT)drvramdiskIoReqReadWorker, 2, pThis, pIoReq);
        else
        {
            /* Sync memory buffer from the request initiator. */
            rc = drvramdiskMediaExIoReqBufSync(pThis, pIoReq, true /* fToIoBuf */);
            if (RT_SUCCESS(rc))
                rc = RTReqQueueCallEx(pThis->hReqQ, NULL, 0, RTREQFLAGS_NO_WAIT,
                                      (PFNRT)drvramdiskIoReqWriteWorker, 2, pThis, pIoReq);
        }

        if (rc == VINF_SUCCESS)
            rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
    }

    if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
    {
        Assert(!pIoReq->ReadWrite.cbReqLeft || RT_FAILURE(rc));
        rc = drvramdiskMediaExIoReqCompleteWorker(pThis, pIoReq, rc, fUpNotify);
    }

    return rc;
}


/**
 * Frees a I/O memory buffer allocated previously.
 *
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request for which to free memory.
 */
DECLINLINE(void) drvramdiskMediaExIoReqBufFree(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
    {
        IOBUFMgrFreeBuf(&pIoReq->ReadWrite.IoBuf);

        if (ASMAtomicReadU32(&pThis->cIoReqsWaiting) > 0)
        {
            /* Try to process as many requests as possible. */
            RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
            PPDMMEDIAEXIOREQINT pIoReqCur, pIoReqNext;

            RTListForEachSafe(&pThis->LstIoReqIoBufWait, pIoReqCur, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
            {
                /* Allocate a suitable I/O buffer for this request. */
                int rc = IOBUFMgrAllocBuf(pThis->hIoBufMgr, &pIoReqCur->ReadWrite.IoBuf, pIoReqCur->ReadWrite.cbReq,
                                          &pIoReqCur->ReadWrite.cbIoBuf);
                if (rc == VINF_SUCCESS)
                {
                    ASMAtomicDecU32(&pThis->cIoReqsWaiting);
                    RTListNodeRemove(&pIoReqCur->NdLstWait);

                    bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReqCur->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
                    if (RT_UNLIKELY(!fXchg))
                    {
                        /* Must have been canceled inbetween. */
                        Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
                        drvramdiskMediaExIoReqCompleteWorker(pThis, pIoReqCur, VERR_PDM_MEDIAEX_IOREQ_CANCELED, true /* fUpNotify */);
                    }
                    ASMAtomicIncU32(&pThis->cIoReqsActive);
                    rc = drvramdiskMediaExIoReqReadWriteProcess(pThis, pIoReqCur, true /* fUpNotify */);
                }
                else
                {
                    Assert(rc == VERR_NO_MEMORY);
                    break;
                }
            }
            RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
        }
    }
}


/**
 * Returns whether the VM is in a running state.
 *
 * @returns Flag indicating whether the VM is currently in a running state.
 * @param   pThis     VBox disk container instance data.
 */
DECLINLINE(bool) drvramdiskMediaExIoReqIsVmRunning(PDRVRAMDISK pThis)
{
    VMSTATE enmVmState = PDMDrvHlpVMState(pThis->pDrvIns);
    if (   enmVmState == VMSTATE_RESUMING
        || enmVmState == VMSTATE_RUNNING
        || enmVmState == VMSTATE_RUNNING_LS
        || enmVmState == VMSTATE_RESETTING
        || enmVmState == VMSTATE_RESETTING_LS
        || enmVmState == VMSTATE_SOFT_RESETTING
        || enmVmState == VMSTATE_SOFT_RESETTING_LS
        || enmVmState == VMSTATE_SUSPENDING
        || enmVmState == VMSTATE_SUSPENDING_LS
        || enmVmState == VMSTATE_SUSPENDING_EXT_LS)
        return true;

    return false;
}

/**
 * @copydoc FNVDASYNCTRANSFERCOMPLETE
 */
static void drvramdiskMediaExIoReqComplete(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq,
                                           int rcReq)
{
    /*
     * For a read we need to sync the memory before continuing to process
     * the request further.
     */
    if (   RT_SUCCESS(rcReq)
        && pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
        rcReq = drvramdiskMediaExIoReqBufSync(pThis, pIoReq, false /* fToIoBuf */);

    /*
     * When the request owner instructs us to handle recoverable errors like full disks
     * do it. Mark the request as suspended, notify the owner and put the request on the
     * redo list.
     */
    if (   RT_FAILURE(rcReq)
        && (pIoReq->fFlags & PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR)
        && drvramdiskMediaExIoReqIsRedoSetWarning(pThis, rcReq))
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_SUSPENDED, VDIOREQSTATE_ACTIVE);
        if (fXchg)
        {
            /* Put on redo list and adjust active request counter. */
            RTCritSectEnter(&pThis->CritSectIoReqRedo);
            RTListAppend(&pThis->LstIoReqRedo, &pIoReq->NdLstWait);
            RTCritSectLeave(&pThis->CritSectIoReqRedo);
            ASMAtomicDecU32(&pThis->cIoReqsActive);
            pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                         PDMMEDIAEXIOREQSTATE_SUSPENDED);
        }
        else
        {
            /* Request was canceled inbetween, so don't care and notify the owner about the completed request. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            drvramdiskMediaExIoReqCompleteWorker(pThis, pIoReq, rcReq, true /* fUpNotify */);
        }
    }
    else
    {
        /* Adjust the remaining amount to transfer. */
        size_t cbReqIo = RT_MIN(pIoReq->ReadWrite.cbReqLeft, pIoReq->ReadWrite.cbIoBuf);
        pIoReq->ReadWrite.offStart  += cbReqIo;
        pIoReq->ReadWrite.cbReqLeft -= cbReqIo;

        if (   RT_FAILURE(rcReq)
            || !pIoReq->ReadWrite.cbReqLeft
            || (   pIoReq->enmType != PDMMEDIAEXIOREQTYPE_READ
                && pIoReq->enmType != PDMMEDIAEXIOREQTYPE_WRITE))
            drvramdiskMediaExIoReqCompleteWorker(pThis, pIoReq, rcReq, true /* fUpNotify */);
        else
            drvramdiskMediaExIoReqReadWriteProcess(pThis, pIoReq, true /* fUpNotify */);
    }
}

/**
 * Worker for a flush request.
 *
 * @returns VBox status code.
 * @param   pThis     RAM disk container instance data.
 * @param   pIoReq    The flush request.
 */
static DECLCALLBACK(int) drvramdiskIoReqFlushWorker(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    /* Nothing to do for a ram disk. */
    drvramdiskMediaExIoReqComplete(pThis, pIoReq, VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * Worker for a discard request.
 *
 * @returns VBox status code.
 * @param   pThis     RAM disk container instance data.
 * @param   pIoReq    The discard request.
 */
static DECLCALLBACK(int) drvramdiskIoReqDiscardWorker(PDRVRAMDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = drvramdiskDiscardRecords(pThis, pIoReq->Discard.paRanges, pIoReq->Discard.cRanges);
    drvramdiskMediaExIoReqComplete(pThis, pIoReq, rc);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnQueryFeatures}
 */
static DECLCALLBACK(int) drvramdiskQueryFeatures(PPDMIMEDIAEX pInterface, uint32_t *pfFeatures)
{
    RT_NOREF1(pInterface);
    *pfFeatures = PDMIMEDIAEX_FEATURE_F_ASYNC | PDMIMEDIAEX_FEATURE_F_DISCARD;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIMEDIAEX,pfnNotifySuspend}
 */
static DECLCALLBACK(void) drvramdiskNotifySuspend(PPDMIMEDIAEX pInterface)
{
    RT_NOREF(pInterface);
}


/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAllocSizeSet}
 */
static DECLCALLBACK(int) drvramdiskIoReqAllocSizeSet(PPDMIMEDIAEX pInterface, size_t cbIoReqAlloc)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);

    if (RT_UNLIKELY(pThis->hIoReqCache != NIL_RTMEMCACHE))
        return VERR_INVALID_STATE;

    return RTMemCacheCreate(&pThis->hIoReqCache, sizeof(PDMMEDIAEXIOREQINT) + cbIoReqAlloc, 0, UINT32_MAX,
                            NULL, NULL, NULL, 0);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAlloc}
 */
static DECLCALLBACK(int) drvramdiskIoReqAlloc(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc,
                                              PDMMEDIAEXIOREQID uIoReqId, uint32_t fFlags)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);

    AssertReturn(!(fFlags & ~PDMIMEDIAEX_F_VALID), VERR_INVALID_PARAMETER);

    PPDMMEDIAEXIOREQINT pIoReq = (PPDMMEDIAEXIOREQINT)RTMemCacheAlloc(pThis->hIoReqCache);

    if (RT_UNLIKELY(!pIoReq))
        return VERR_NO_MEMORY;

    pIoReq->uIoReqId      = uIoReqId;
    pIoReq->fFlags        = fFlags;
    pIoReq->pDisk         = pThis;
    pIoReq->enmState      = VDIOREQSTATE_ALLOCATED;
    pIoReq->enmType       = PDMMEDIAEXIOREQTYPE_INVALID;

    int rc = drvramdiskMediaExIoReqInsert(pThis, pIoReq);
    if (RT_SUCCESS(rc))
    {
        *phIoReq = pIoReq;
        *ppvIoReqAlloc = &pIoReq->abAlloc[0];
    }
    else
        RTMemCacheFree(pThis->hIoReqCache, pIoReq);

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFree}
 */
static DECLCALLBACK(int) drvramdiskIoReqFree(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    if (   pIoReq->enmState != VDIOREQSTATE_COMPLETED
        && pIoReq->enmState != VDIOREQSTATE_ALLOCATED)
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    /* Remove from allocated list. */
    int rc = drvramdiskMediaExIoReqRemove(pThis, pIoReq);
    if (RT_FAILURE(rc))
        return rc;

    /* Free any associated I/O memory. */
    drvramdiskMediaExIoReqBufFree(pThis, pIoReq);

    /* For discard request discard the range array. */
    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD
        && pIoReq->Discard.paRanges)
    {
        RTMemFree(pIoReq->Discard.paRanges);
        pIoReq->Discard.paRanges = NULL;
    }

    pIoReq->enmState = VDIOREQSTATE_FREE;
    RTMemCacheFree(pThis->hIoReqCache, pIoReq);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryResidual}
 */
static DECLCALLBACK(int) drvramdiskIoReqQueryResidual(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbResidual)
{
    RT_NOREF2(pInterface, hIoReq);

    *pcbResidual = 0;
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryXferSize}
 */
static DECLCALLBACK(int) drvramdiskIoReqQueryXferSize(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbXfer)
{
    RT_NOREF1(pInterface);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
        *pcbXfer = pIoReq->ReadWrite.cbReq;
    else
        *pcbXfer = 0;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancelAll}
 */
static DECLCALLBACK(int) drvramdiskIoReqCancelAll(PPDMIMEDIAEX pInterface)
{
    RT_NOREF1(pInterface);
    return VINF_SUCCESS; /** @todo */
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancel}
 */
static DECLCALLBACK(int) drvramdiskIoReqCancel(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQID uIoReqId)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    unsigned idxBin = drvramdiskMediaExIoReqIdHash(uIoReqId);

    int rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        /* Search for I/O request with ID. */
        PPDMMEDIAEXIOREQINT pIt;
        rc = VERR_PDM_MEDIAEX_IOREQID_NOT_FOUND;

        RTListForEach(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, pIt, PDMMEDIAEXIOREQINT, NdAllocatedList)
        {
            if (pIt->uIoReqId == uIoReqId)
            {
                bool fXchg = true;
                VDIOREQSTATE enmStateOld = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIt->enmState);

                /*
                 * We might have to try canceling the request multiple times if it transitioned from
                 * ALLOCATED to ACTIVE or to SUSPENDED between reading the state and trying to change it.
                 */
                while (   (   enmStateOld == VDIOREQSTATE_ALLOCATED
                           || enmStateOld == VDIOREQSTATE_ACTIVE
                           || enmStateOld == VDIOREQSTATE_SUSPENDED)
                       && !fXchg)
                {
                    fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIt->enmState, VDIOREQSTATE_CANCELED, enmStateOld);
                    if (!fXchg)
                        enmStateOld = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIt->enmState);
                }

                if (fXchg)
                {
                    ASMAtomicDecU32(&pThis->cIoReqsActive);
                    rc = VINF_SUCCESS;
                }
                break;
            }
        }
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqRead}
 */
static DECLCALLBACK(int) drvramdiskIoReqRead(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbRead)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    pIoReq->enmType             = PDMMEDIAEXIOREQTYPE_READ;
    pIoReq->tsSubmit            = RTTimeMilliTS();
    pIoReq->ReadWrite.offStart  = off;
    pIoReq->ReadWrite.cbReq     = cbRead;
    pIoReq->ReadWrite.cbReqLeft = cbRead;
    /* Allocate a suitable I/O buffer for this request. */
    int rc = drvramdiskMediaExIoReqBufAlloc(pThis, pIoReq, cbRead);
    if (rc == VINF_SUCCESS)
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }
        ASMAtomicIncU32(&pThis->cIoReqsActive);

        rc = drvramdiskMediaExIoReqReadWriteProcess(pThis, pIoReq, false /* fUpNotify */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqWrite}
 */
static DECLCALLBACK(int) drvramdiskIoReqWrite(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbWrite)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    pIoReq->enmType             = PDMMEDIAEXIOREQTYPE_WRITE;
    pIoReq->tsSubmit            = RTTimeMilliTS();
    pIoReq->ReadWrite.offStart  = off;
    pIoReq->ReadWrite.cbReq     = cbWrite;
    pIoReq->ReadWrite.cbReqLeft = cbWrite;
    /* Allocate a suitable I/O buffer for this request. */
    int rc = drvramdiskMediaExIoReqBufAlloc(pThis, pIoReq, cbWrite);
    if (rc == VINF_SUCCESS)
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }
        ASMAtomicIncU32(&pThis->cIoReqsActive);

        rc = drvramdiskMediaExIoReqReadWriteProcess(pThis, pIoReq, false /* fUpNotify */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFlush}
 */
static DECLCALLBACK(int) drvramdiskIoReqFlush(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    pIoReq->enmType  = PDMMEDIAEXIOREQTYPE_FLUSH;
    pIoReq->tsSubmit = RTTimeMilliTS();
    bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
    if (RT_UNLIKELY(!fXchg))
    {
        /* Must have been canceled inbetween. */
        Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
    }

    ASMAtomicIncU32(&pThis->cIoReqsActive);
    return RTReqQueueCallEx(pThis->hReqQ, NULL, 0, RTREQFLAGS_NO_WAIT, (PFNRT)drvramdiskIoReqFlushWorker, 2, pThis, pIoReq);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqDiscard}
 */
static DECLCALLBACK(int) drvramdiskIoReqDiscard(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, unsigned cRangesMax)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    /* Copy the ranges over now, this can be optimized in the future. */
    pIoReq->Discard.paRanges = (PRTRANGE)RTMemAllocZ(cRangesMax * sizeof(RTRANGE));
    if (RT_UNLIKELY(!pIoReq->Discard.paRanges))
        return VERR_NO_MEMORY;

    int rc = pThis->pDrvMediaExPort->pfnIoReqQueryDiscardRanges(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                                0, cRangesMax, pIoReq->Discard.paRanges,
                                                                &pIoReq->Discard.cRanges);
    if (RT_SUCCESS(rc))
    {
        pIoReq->enmType  = PDMMEDIAEXIOREQTYPE_DISCARD;
        pIoReq->tsSubmit = RTTimeMilliTS();

        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }

        ASMAtomicIncU32(&pThis->cIoReqsActive);

        rc = RTReqQueueCallEx(pThis->hReqQ, NULL, 0, RTREQFLAGS_NO_WAIT, (PFNRT)drvramdiskIoReqDiscardWorker, 2, pThis, pIoReq);
        if (rc == VINF_SUCCESS)
            rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetActiveCount}
 */
static DECLCALLBACK(uint32_t) drvramdiskIoReqGetActiveCount(PPDMIMEDIAEX pInterface)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    return ASMAtomicReadU32(&pThis->cIoReqsActive);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetSuspendedCount}
 */
static DECLCALLBACK(uint32_t) drvramdiskIoReqGetSuspendedCount(PPDMIMEDIAEX pInterface)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);

    AssertReturn(!drvramdiskMediaExIoReqIsVmRunning(pThis), 0);

    uint32_t cIoReqSuspended = 0;
    PPDMMEDIAEXIOREQINT pIoReq;
    RTCritSectEnter(&pThis->CritSectIoReqRedo);
    RTListForEach(&pThis->LstIoReqRedo, pIoReq, PDMMEDIAEXIOREQINT, NdLstWait)
    {
        cIoReqSuspended++;
    }
    RTCritSectLeave(&pThis->CritSectIoReqRedo);

    return cIoReqSuspended;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedStart}
 */
static DECLCALLBACK(int) drvramdiskIoReqQuerySuspendedStart(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq,
                                                            void **ppvIoReqAlloc)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);

    AssertReturn(!drvramdiskMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertReturn(!RTListIsEmpty(&pThis->LstIoReqRedo), VERR_NOT_FOUND);

    RTCritSectEnter(&pThis->CritSectIoReqRedo);
    PPDMMEDIAEXIOREQINT pIoReq = RTListGetFirst(&pThis->LstIoReqRedo, PDMMEDIAEXIOREQINT, NdLstWait);
    *phIoReq       = pIoReq;
    *ppvIoReqAlloc = &pIoReq->abAlloc[0];
    RTCritSectLeave(&pThis->CritSectIoReqRedo);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedNext}
 */
static DECLCALLBACK(int) drvramdiskIoReqQuerySuspendedNext(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                           PPDMMEDIAEXIOREQ phIoReqNext, void **ppvIoReqAllocNext)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    AssertReturn(!drvramdiskMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(!RTListNodeIsLast(&pThis->LstIoReqRedo, &pIoReq->NdLstWait), VERR_NOT_FOUND);

    RTCritSectEnter(&pThis->CritSectIoReqRedo);
    PPDMMEDIAEXIOREQINT pIoReqNext = RTListNodeGetNext(&pIoReq->NdLstWait, PDMMEDIAEXIOREQINT, NdLstWait);
    *phIoReqNext       = pIoReqNext;
    *ppvIoReqAllocNext = &pIoReqNext->abAlloc[0];
    RTCritSectLeave(&pThis->CritSectIoReqRedo);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedSave}
 */
static DECLCALLBACK(int) drvramdiskIoReqSuspendedSave(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    RT_NOREF1(pSSM);

    AssertReturn(!drvramdiskMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(pIoReq->enmState == VDIOREQSTATE_SUSPENDED, VERR_INVALID_STATE);

    return VERR_NOT_IMPLEMENTED;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedLoad}
 */
static DECLCALLBACK(int) drvramdiskIoReqSuspendedLoad(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVRAMDISK pThis = RT_FROM_MEMBER(pInterface, DRVRAMDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    RT_NOREF1(pSSM);

    AssertReturn(!drvramdiskMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(pIoReq->enmState == VDIOREQSTATE_ALLOCATED, VERR_INVALID_STATE);

    return VERR_NOT_IMPLEMENTED;
}

static DECLCALLBACK(int) drvramdiskIoReqWorker(RTTHREAD hThrdSelf, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PDRVRAMDISK pThis = (PDRVRAMDISK)pvUser;

    RT_NOREF1(hThrdSelf);

    do
    {
        rc = RTReqQueueProcess(pThis->hReqQ, RT_INDEFINITE_WAIT);
    } while (RT_SUCCESS(rc));

    return VINF_SUCCESS;
}

/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *)  drvramdiskQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVRAMDISK pThis = PDMINS_2_DATA(pDrvIns, PDRVRAMDISK);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIA, &pThis->IMedia);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEX, &pThis->IMediaEx);

    return NULL;
}


/* -=-=-=-=- driver interface -=-=-=-=- */

static DECLCALLBACK(int) drvramdiskTreeDestroy(PAVLRFOFFNODECORE pNode, void *pvUser)
{
    PDRVDISKSEGMENT pSeg = (PDRVDISKSEGMENT)pNode;

    RT_NOREF1(pvUser);

    RTMemFree(pSeg->pbSeg);
    RTMemFree(pSeg);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNPDMDRVDESTRUCT
 */
static DECLCALLBACK(void) drvramdiskDestruct(PPDMDRVINS pDrvIns)
{
    PDRVRAMDISK pThis = PDMINS_2_DATA(pDrvIns, PDRVRAMDISK);

    PRTREQ pReq = NULL;
    int rc = RTReqQueueCallEx(pThis->hReqQ, &pReq, RT_INDEFINITE_WAIT, RTREQFLAGS_IPRT_STATUS, (PFNRT)drvramdiskDestructQueue, 0);
    AssertRC(rc);
    RTReqRelease(pReq);

    if (pThis->pTreeSegments)
    {
        RTAvlrFileOffsetDestroy(pThis->pTreeSegments, drvramdiskTreeDestroy, NULL);
        RTMemFree(pThis->pTreeSegments);
    }
    if (pThis->hIoBufMgr)
        IOBUFMgrDestroy(pThis->hIoBufMgr);

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIoReqAllocBins); i++)
        if (pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc != NIL_RTSEMFASTMUTEX)
            RTSemFastMutexDestroy(pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc);

    if (RTCritSectIsInitialized(&pThis->CritSectIoReqsIoBufWait))
        RTCritSectDelete(&pThis->CritSectIoReqsIoBufWait);

    if (RTCritSectIsInitialized(&pThis->CritSectIoReqRedo))
        RTCritSectDelete(&pThis->CritSectIoReqRedo);

    if (pThis->hIoReqCache != NIL_RTMEMCACHE)
        RTMemCacheDestroy(pThis->hIoReqCache);

    RTReqQueueDestroy(pThis->hReqQ);
}

/**
 * Construct a disk integrity driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvramdiskConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF1(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVRAMDISK     pThis = PDMINS_2_DATA(pDrvIns, PDRVRAMDISK);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvdiskintConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Initialize most of the data members.
     */
    pThis->pDrvIns                       = pDrvIns;

    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface     = drvramdiskQueryInterface;

    /* IMedia */
    pThis->IMedia.pfnRead                        = drvramdiskRead;
    pThis->IMedia.pfnWrite                       = drvramdiskWrite;
    pThis->IMedia.pfnFlush                       = drvramdiskFlush;
    pThis->IMedia.pfnGetSize                     = drvramdiskGetSize;
    pThis->IMedia.pfnBiosIsVisible               = drvramdiskBiosIsVisible;
    pThis->IMedia.pfnGetType                     = drvramdiskGetType;
    pThis->IMedia.pfnIsReadOnly                  = drvramdiskIsReadOnly;
    pThis->IMedia.pfnBiosGetPCHSGeometry         = drvramdiskBiosGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry         = drvramdiskBiosSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry         = drvramdiskBiosGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry         = drvramdiskBiosSetLCHSGeometry;
    pThis->IMedia.pfnGetUuid                     = drvramdiskGetUuid;
    pThis->IMedia.pfnGetSectorSize               = drvramdiskGetSectorSize;
    pThis->IMedia.pfnReadPcBios                  = drvramdiskReadPcBios;
    pThis->IMedia.pfnDiscard                     = drvramdiskDiscard;
    pThis->IMedia.pfnIsNonRotational             = drvramdiskIsNonRotational;
    pThis->IMedia.pfnSendCmd                     = NULL;
    pThis->IMedia.pfnGetRegionCount              = drvramdiskGetRegionCount;
    pThis->IMedia.pfnQueryRegionProperties       = drvramdiskQueryRegionProperties;
    pThis->IMedia.pfnQueryRegionPropertiesForLba = drvramdiskQueryRegionPropertiesForLba;

    /* IMediaEx */
    pThis->IMediaEx.pfnQueryFeatures            = drvramdiskQueryFeatures;
    pThis->IMediaEx.pfnNotifySuspend            = drvramdiskNotifySuspend;
    pThis->IMediaEx.pfnIoReqAllocSizeSet        = drvramdiskIoReqAllocSizeSet;
    pThis->IMediaEx.pfnIoReqAlloc               = drvramdiskIoReqAlloc;
    pThis->IMediaEx.pfnIoReqFree                = drvramdiskIoReqFree;
    pThis->IMediaEx.pfnIoReqQueryResidual       = drvramdiskIoReqQueryResidual;
    pThis->IMediaEx.pfnIoReqQueryXferSize       = drvramdiskIoReqQueryXferSize;
    pThis->IMediaEx.pfnIoReqCancelAll           = drvramdiskIoReqCancelAll;
    pThis->IMediaEx.pfnIoReqCancel              = drvramdiskIoReqCancel;
    pThis->IMediaEx.pfnIoReqRead                = drvramdiskIoReqRead;
    pThis->IMediaEx.pfnIoReqWrite               = drvramdiskIoReqWrite;
    pThis->IMediaEx.pfnIoReqFlush               = drvramdiskIoReqFlush;
    pThis->IMediaEx.pfnIoReqDiscard             = drvramdiskIoReqDiscard;
    pThis->IMediaEx.pfnIoReqGetActiveCount      = drvramdiskIoReqGetActiveCount;
    pThis->IMediaEx.pfnIoReqGetSuspendedCount   = drvramdiskIoReqGetSuspendedCount;
    pThis->IMediaEx.pfnIoReqQuerySuspendedStart = drvramdiskIoReqQuerySuspendedStart;
    pThis->IMediaEx.pfnIoReqQuerySuspendedNext  = drvramdiskIoReqQuerySuspendedNext;
    pThis->IMediaEx.pfnIoReqSuspendedSave       = drvramdiskIoReqSuspendedSave;
    pThis->IMediaEx.pfnIoReqSuspendedLoad       = drvramdiskIoReqSuspendedLoad;

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,  "Size"
                                            "|PreAlloc"
                                            "|IoBufMax"
                                            "|SectorSize"
                                            "|NonRotational",
                                            "");

    int rc = pHlp->pfnCFGMQueryU64(pCfg, "Size", &pThis->cbDisk);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("RamDisk: Error querying the media size"));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "PreAlloc", &pThis->fPreallocRamDisk, false);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("RamDisk: Error querying \"PreAlloc\""));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "NonRotational", &pThis->fNonRotational, true);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("RamDisk: Error querying \"NonRotational\""));

    uint32_t cbIoBufMax;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IoBufMax", &cbIoBufMax, 5 * _1M);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"IoBufMax\" from the config"));
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "SectorSize", &pThis->cbSector, 512);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"SectorSize\" from the config"));

    /* Query the media port interface above us. */
    pThis->pDrvMediaPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAPORT);
    if (!pThis->pDrvMediaPort)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW,
                                N_("No media port interface above"));

    /* Try to attach extended media port interface above.*/
    pThis->pDrvMediaExPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAEXPORT);
    if (pThis->pDrvMediaExPort)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIoReqAllocBins); i++)
        {
            rc = RTSemFastMutexCreate(&pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc);
            if (RT_FAILURE(rc))
                break;
            RTListInit(&pThis->aIoReqAllocBins[i].LstIoReqAlloc);
        }

        if (RT_SUCCESS(rc))
            rc = RTCritSectInit(&pThis->CritSectIoReqsIoBufWait);

        if (RT_SUCCESS(rc))
            rc = RTCritSectInit(&pThis->CritSectIoReqRedo);

        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Creating Mutex failed"));

        RTListInit(&pThis->LstIoReqIoBufWait);
        RTListInit(&pThis->LstIoReqRedo);
    }

    /* Create the AVL tree. */
    pThis->pTreeSegments = (PAVLRFOFFTREE)RTMemAllocZ(sizeof(AVLRFOFFTREE));
    if (!pThis->pTreeSegments)
        rc = VERR_NO_MEMORY;

    if (pThis->pDrvMediaExPort)
    {
        rc = RTReqQueueCreate(&pThis->hReqQ);
        if (RT_SUCCESS(rc))
        {
            /* Spin up the worker thread. */
            rc = RTThreadCreate(&pThis->hThrdWrk, drvramdiskIoReqWorker, pThis, 0,
                                RTTHREADTYPE_IO, 0, "RAMDSK");
        }
    }

    if (pThis->pDrvMediaExPort)
        rc = IOBUFMgrCreate(&pThis->hIoBufMgr, cbIoBufMax, IOBUFMGR_F_DEFAULT);

    /* Read in all data before the start if requested. */
    if (   RT_SUCCESS(rc)
        && pThis->fPreallocRamDisk)
    {
        LogRel(("RamDisk: Preallocating RAM disk...\n"));
        return VERR_NOT_IMPLEMENTED;
    }

    return rc;
}


/**
 * Block driver registration record.
 */
const PDMDRVREG g_DrvRamDisk =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "RamDisk",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "RAM disk driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_BLOCK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVRAMDISK),
    /* pfnConstruct */
    drvramdiskConstruct,
    /* pfnDestruct */
    drvramdiskDestruct,
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

