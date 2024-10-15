/* $Id: VD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - Virtual disk container implementation.
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
#define LOG_GROUP LOG_GROUP_VD
#include <VBox/vd.h>
#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/log.h>

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/sg.h>
#include <iprt/semaphore.h>
#include <iprt/vector.h>

#include "VDInternal.h"

/** Buffer size used for merging images. */
#define VD_MERGE_BUFFER_SIZE    (16 * _1M)

/** Maximum number of segments in one I/O task. */
#define VD_IO_TASK_SEGMENTS_MAX 64

/** Threshold after not recently used blocks are removed from the list. */
#define VD_DISCARD_REMOVE_THRESHOLD (10 * _1M) /** @todo experiment */

/**
 * VD async I/O interface storage descriptor.
 */
typedef struct VDIIOFALLBACKSTORAGE
{
    /** File handle. */
    RTFILE              File;
    /** Completion callback. */
    PFNVDCOMPLETED      pfnCompleted;
    /** Thread for async access. */
    RTTHREAD            ThreadAsync;
} VDIIOFALLBACKSTORAGE, *PVDIIOFALLBACKSTORAGE;

/**
 * uModified bit flags.
 */
#define VD_IMAGE_MODIFIED_FLAG                  RT_BIT(0)
#define VD_IMAGE_MODIFIED_FIRST                 RT_BIT(1)
#define VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE   RT_BIT(2)


# define VD_IS_LOCKED(a_pDisk) \
    do \
    { \
        NOREF(a_pDisk); \
        AssertMsg((a_pDisk)->fLocked, \
                  ("Lock not held\n"));\
    } while(0)

/**
 * VBox parent read descriptor, used internally for compaction.
 */
typedef struct VDPARENTSTATEDESC
{
    /** Pointer to disk descriptor. */
    PVDISK pDisk;
    /** Pointer to image descriptor. */
    PVDIMAGE pImage;
} VDPARENTSTATEDESC, *PVDPARENTSTATEDESC;

/**
 * Transfer direction.
 */
typedef enum VDIOCTXTXDIR
{
    /** Read */
    VDIOCTXTXDIR_READ = 0,
    /** Write */
    VDIOCTXTXDIR_WRITE,
    /** Flush */
    VDIOCTXTXDIR_FLUSH,
    /** Discard */
    VDIOCTXTXDIR_DISCARD,
    /** 32bit hack */
    VDIOCTXTXDIR_32BIT_HACK = 0x7fffffff
} VDIOCTXTXDIR, *PVDIOCTXTXDIR;

/** Transfer function */
typedef DECLCALLBACKTYPE(int, FNVDIOCTXTRANSFER ,(PVDIOCTX pIoCtx));
/** Pointer to a transfer function. */
typedef FNVDIOCTXTRANSFER *PFNVDIOCTXTRANSFER;

/**
 * I/O context
 */
typedef struct VDIOCTX
{
    /** Pointer to the next I/O context. */
    struct VDIOCTX * volatile    pIoCtxNext;
    /** Disk this is request is for. */
    PVDISK                       pDisk;
    /** Return code. */
    int                          rcReq;
    /** Various flags for the I/O context. */
    uint32_t                     fFlags;
    /** Number of data transfers currently pending. */
    volatile uint32_t            cDataTransfersPending;
    /** How many meta data transfers are pending. */
    volatile uint32_t            cMetaTransfersPending;
    /** Flag whether the request finished */
    volatile bool                fComplete;
    /** Temporary allocated memory which is freed
     * when the context completes. */
    void                        *pvAllocation;
    /** Transfer function. */
    PFNVDIOCTXTRANSFER           pfnIoCtxTransfer;
    /** Next transfer part after the current one completed. */
    PFNVDIOCTXTRANSFER           pfnIoCtxTransferNext;
    /** Transfer direction */
    VDIOCTXTXDIR                 enmTxDir;
    /** Request type dependent data. */
    union
    {
        /** I/O request (read/write). */
        struct
        {
            /** Number of bytes left until this context completes. */
            volatile uint32_t    cbTransferLeft;
            /** Current offset */
            volatile uint64_t    uOffset;
            /** Number of bytes to transfer */
            volatile size_t      cbTransfer;
            /** Current image in the chain. */
            PVDIMAGE             pImageCur;
            /** Start image to read from. pImageCur is reset to this
             *  value after it reached the first image in the chain. */
            PVDIMAGE             pImageStart;
            /** S/G buffer */
            RTSGBUF              SgBuf;
            /** Number of bytes to clear in the buffer before the current read. */
            size_t               cbBufClear;
            /** Number of images to read. */
            unsigned             cImagesRead;
            /** Override for the parent image to start reading from. */
            PVDIMAGE             pImageParentOverride;
            /** Original offset of the transfer - required for filtering read requests. */
            uint64_t             uOffsetXferOrig;
            /** Original size of the transfer - required for fitlering read requests. */
            size_t               cbXferOrig;
        } Io;
        /** Discard requests. */
        struct
        {
            /** Pointer to the range descriptor array. */
            PCRTRANGE            paRanges;
            /** Number of ranges in the array. */
            unsigned             cRanges;
            /** Range descriptor index which is processed. */
            unsigned             idxRange;
            /** Start offset to discard currently. */
            uint64_t             offCur;
            /** How many bytes left to discard in the current range. */
            size_t               cbDiscardLeft;
            /** How many bytes to discard in the current block (<= cbDiscardLeft). */
            size_t               cbThisDiscard;
            /** Discard block handled currently. */
            PVDDISCARDBLOCK      pBlock;
        } Discard;
    } Req;
    /** Parent I/O context if any. Sets the type of the context (root/child) */
    PVDIOCTX                     pIoCtxParent;
    /** Type dependent data (root/child) */
    union
    {
        /** Root data */
        struct
        {
            /** Completion callback */
            PFNVDASYNCTRANSFERCOMPLETE   pfnComplete;
            /** User argument 1 passed on completion. */
            void                        *pvUser1;
            /** User argument 2 passed on completion. */
            void                        *pvUser2;
        } Root;
        /** Child data */
        struct
        {
            /** Saved start offset */
            uint64_t                     uOffsetSaved;
            /** Saved transfer size */
            size_t                       cbTransferLeftSaved;
            /** Number of bytes transferred from the parent if this context completes. */
            size_t                       cbTransferParent;
            /** Number of bytes to pre read */
            size_t                       cbPreRead;
            /** Number of bytes to post read. */
            size_t                       cbPostRead;
            /** Number of bytes to write left in the parent. */
            size_t                       cbWriteParent;
            /** Write type dependent data. */
            union
            {
                /** Optimized */
                struct
                {
                    /** Bytes to fill to satisfy the block size. Not part of the virtual disk. */
                    size_t               cbFill;
                    /** Bytes to copy instead of reading from the parent */
                    size_t               cbWriteCopy;
                    /** Bytes to read from the image. */
                    size_t               cbReadImage;
                } Optimized;
            } Write;
        } Child;
    } Type;
} VDIOCTX;

/** Default flags for an I/O context, i.e. unblocked and async. */
#define VDIOCTX_FLAGS_DEFAULT                         (0)
/** Flag whether the context is blocked. */
#define VDIOCTX_FLAGS_BLOCKED                RT_BIT_32(0)
/** Flag whether the I/O context is using synchronous I/O. */
#define VDIOCTX_FLAGS_SYNC                   RT_BIT_32(1)
/** Flag whether the read should update the cache. */
#define VDIOCTX_FLAGS_READ_UPDATE_CACHE      RT_BIT_32(2)
/** Flag whether free blocks should be zeroed.
 * If false and no image has data for sepcified
 * range VERR_VD_BLOCK_FREE is returned for the I/O context.
 * Note that unallocated blocks are still zeroed
 * if at least one image has valid data for a part
 * of the range.
 */
#define VDIOCTX_FLAGS_ZERO_FREE_BLOCKS       RT_BIT_32(3)
/** Don't free the I/O context when complete because
 * it was alloacted elsewhere (stack, ...). */
#define VDIOCTX_FLAGS_DONT_FREE              RT_BIT_32(4)
/** Don't set the modified flag for this I/O context when writing. */
#define VDIOCTX_FLAGS_DONT_SET_MODIFIED_FLAG RT_BIT_32(5)
/** The write filter was applied already and shouldn't be applied a second time.
 * Used at the beginning of vdWriteHelperAsync() because it might be called
 * multiple times.
 */
#define VDIOCTX_FLAGS_WRITE_FILTER_APPLIED   RT_BIT_32(6)

/** NIL I/O context pointer value. */
#define NIL_VDIOCTX ((PVDIOCTX)0)

/**
 * List node for deferred I/O contexts.
 */
typedef struct VDIOCTXDEFERRED
{
    /** Node in the list of deferred requests.
     * A request can be deferred if the image is growing
     * and the request accesses the same range or if
     * the backend needs to read or write metadata from the disk
     * before it can continue. */
    RTLISTNODE NodeDeferred;
    /** I/O context this entry points to. */
    PVDIOCTX   pIoCtx;
} VDIOCTXDEFERRED, *PVDIOCTXDEFERRED;

/**
 * I/O task.
 */
typedef struct VDIOTASK
{
    /** Next I/O task waiting in the list. */
    struct VDIOTASK * volatile   pNext;
    /** Storage this task belongs to. */
    PVDIOSTORAGE                 pIoStorage;
    /** Optional completion callback. */
    PFNVDXFERCOMPLETED           pfnComplete;
    /** Opaque user data. */
    void                        *pvUser;
    /** Completion status code for the task. */
    int                          rcReq;
    /** Flag whether this is a meta data transfer. */
    bool                         fMeta;
    /** Type dependent data. */
    union
    {
        /** User data transfer. */
        struct
        {
            /** Number of bytes this task transferred. */
            uint32_t             cbTransfer;
            /** Pointer to the I/O context the task belongs. */
            PVDIOCTX             pIoCtx;
        } User;
        /** Meta data transfer. */
        struct
        {
            /** Meta transfer this task is for. */
            PVDMETAXFER          pMetaXfer;
        } Meta;
    } Type;
} VDIOTASK;

/**
 * Storage handle.
 */
typedef struct VDIOSTORAGE
{
    /** Image I/O state this storage handle belongs to. */
    PVDIO                        pVDIo;
    /** AVL tree for pending async metadata transfers. */
    PAVLRFOFFTREE                pTreeMetaXfers;
    /** Storage handle */
    void                        *pStorage;
} VDIOSTORAGE;

/**
 *  Metadata transfer.
 *
 *  @note This entry can't be freed if either the list is not empty or
 *  the reference counter is not 0.
 *  The assumption is that the backends don't need to read huge amounts of
 *  metadata to complete a transfer so the additional memory overhead should
 *  be relatively small.
 */
typedef struct VDMETAXFER
{
    /** AVL core for fast search (the file offset is the key) */
    AVLRFOFFNODECORE Core;
    /** I/O storage for this transfer. */
    PVDIOSTORAGE     pIoStorage;
    /** Flags. */
    uint32_t         fFlags;
    /** List of I/O contexts waiting for this metadata transfer to complete. */
    RTLISTNODE       ListIoCtxWaiting;
    /** Number of references to this entry. */
    unsigned         cRefs;
    /** Size of the data stored with this entry. */
    size_t           cbMeta;
    /** Shadow buffer which is used in case a write is still active and other
     * writes update the shadow buffer. */
    uint8_t         *pbDataShw;
    /** List of I/O contexts updating the shadow buffer while there is a write
     * in progress. */
    RTLISTNODE       ListIoCtxShwWrites;
    /** Data stored - variable size. */
    uint8_t          abData[1];
} VDMETAXFER;

/**
 * The transfer direction for the metadata.
 */
#define VDMETAXFER_TXDIR_MASK  0x3
#define VDMETAXFER_TXDIR_NONE  0x0
#define VDMETAXFER_TXDIR_WRITE 0x1
#define VDMETAXFER_TXDIR_READ  0x2
#define VDMETAXFER_TXDIR_FLUSH 0x3
#define VDMETAXFER_TXDIR_GET(flags)      ((flags) & VDMETAXFER_TXDIR_MASK)
#define VDMETAXFER_TXDIR_SET(flags, dir) ((flags) = (flags & ~VDMETAXFER_TXDIR_MASK) | (dir))

/** Forward declaration of the async discard helper. */
static DECLCALLBACK(int) vdDiscardHelperAsync(PVDIOCTX pIoCtx);
static DECLCALLBACK(int) vdWriteHelperAsync(PVDIOCTX pIoCtx);
static void vdDiskProcessBlockedIoCtx(PVDISK pDisk);
static int vdDiskUnlock(PVDISK pDisk, PVDIOCTX pIoCtxRc);
static DECLCALLBACK(void) vdIoCtxSyncComplete(void *pvUser1, void *pvUser2, int rcReq);

/**
 * internal: issue error message.
 */
static int vdError(PVDISK pDisk, int rc, RT_SRC_POS_DECL,
                   const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pDisk->pInterfaceError)
        pDisk->pInterfaceError->pfnError(pDisk->pInterfaceError->Core.pvUser, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * internal: thread synchronization, start read.
 */
DECLINLINE(int) vdThreadStartRead(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    if (RT_UNLIKELY(pDisk->pInterfaceThreadSync))
        rc = pDisk->pInterfaceThreadSync->pfnStartRead(pDisk->pInterfaceThreadSync->Core.pvUser);
    return rc;
}

/**
 * internal: thread synchronization, finish read.
 */
DECLINLINE(int) vdThreadFinishRead(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    if (RT_UNLIKELY(pDisk->pInterfaceThreadSync))
        rc = pDisk->pInterfaceThreadSync->pfnFinishRead(pDisk->pInterfaceThreadSync->Core.pvUser);
    return rc;
}

/**
 * internal: thread synchronization, start write.
 */
DECLINLINE(int) vdThreadStartWrite(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    if (RT_UNLIKELY(pDisk->pInterfaceThreadSync))
        rc = pDisk->pInterfaceThreadSync->pfnStartWrite(pDisk->pInterfaceThreadSync->Core.pvUser);
    return rc;
}

/**
 * internal: thread synchronization, finish write.
 */
DECLINLINE(int) vdThreadFinishWrite(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    if (RT_UNLIKELY(pDisk->pInterfaceThreadSync))
        rc = pDisk->pInterfaceThreadSync->pfnFinishWrite(pDisk->pInterfaceThreadSync->Core.pvUser);
    return rc;
}

/**
 * internal: add image structure to the end of images list.
 */
static void vdAddImageToList(PVDISK pDisk, PVDIMAGE pImage)
{
    pImage->pPrev = NULL;
    pImage->pNext = NULL;

    if (pDisk->pBase)
    {
        Assert(pDisk->cImages > 0);
        pImage->pPrev = pDisk->pLast;
        pDisk->pLast->pNext = pImage;
        pDisk->pLast = pImage;
    }
    else
    {
        Assert(pDisk->cImages == 0);
        pDisk->pBase = pImage;
        pDisk->pLast = pImage;
    }

    pDisk->cImages++;
}

/**
 * internal: remove image structure from the images list.
 */
static void vdRemoveImageFromList(PVDISK pDisk, PVDIMAGE pImage)
{
    Assert(pDisk->cImages > 0);

    if (pImage->pPrev)
        pImage->pPrev->pNext = pImage->pNext;
    else
        pDisk->pBase = pImage->pNext;

    if (pImage->pNext)
        pImage->pNext->pPrev = pImage->pPrev;
    else
        pDisk->pLast = pImage->pPrev;

    pImage->pPrev = NULL;
    pImage->pNext = NULL;

    pDisk->cImages--;
}

/**
 * Release a referene to the filter decrementing the counter and destroying the filter
 * when the counter reaches zero.
 *
 * @returns The new reference count.
 * @param   pFilter    The filter to release.
 */
static uint32_t vdFilterRelease(PVDFILTER pFilter)
{
    uint32_t cRefs = ASMAtomicDecU32(&pFilter->cRefs);
    if (!cRefs)
    {
        pFilter->pBackend->pfnDestroy(pFilter->pvBackendData);
        RTMemFree(pFilter);
    }

    return cRefs;
}

/**
 * Increments the reference counter of the given filter.
 *
 * @return The new reference count.
 * @param  pFilter    The filter.
 */
static uint32_t vdFilterRetain(PVDFILTER pFilter)
{
    return ASMAtomicIncU32(&pFilter->cRefs);
}

/**
 * internal: find image by index into the images list.
 */
static PVDIMAGE vdGetImageByNumber(PVDISK pDisk, unsigned nImage)
{
    PVDIMAGE pImage = pDisk->pBase;
    if (nImage == VD_LAST_IMAGE)
        return pDisk->pLast;
    while (pImage && nImage)
    {
        pImage = pImage->pNext;
        nImage--;
    }
    return pImage;
}

/**
 * Creates a new region list from the given one converting to match the flags if necessary.
 *
 * @returns VBox status code.
 * @param   pRegionList     The region list to convert from.
 * @param   fFlags          The flags for the new region list.
 * @param   ppRegionList    Where to store the new region list on success.
 */
static int vdRegionListConv(PCVDREGIONLIST pRegionList, uint32_t fFlags, PPVDREGIONLIST ppRegionList)
{
    int rc = VINF_SUCCESS;
    PVDREGIONLIST pRegionListNew = (PVDREGIONLIST)RTMemDup(pRegionList,
                                                           RT_UOFFSETOF_DYN(VDREGIONLIST, aRegions[pRegionList->cRegions]));
    if (RT_LIKELY(pRegionListNew))
    {
        /* Do we have to convert anything? */
        if (pRegionList->fFlags != fFlags)
        {
            uint64_t offRegionNext = 0;

            pRegionListNew->fFlags = fFlags;
            for (unsigned i = 0; i < pRegionListNew->cRegions; i++)
            {
                PVDREGIONDESC pRegion = &pRegionListNew->aRegions[i];

                if (   (fFlags & VD_REGION_LIST_F_LOC_SIZE_BLOCKS)
                    && !(pRegionList->fFlags & VD_REGION_LIST_F_LOC_SIZE_BLOCKS))
                {
                    Assert(!(pRegion->cRegionBlocksOrBytes % pRegion->cbBlock));

                    /* Convert from bytes to logical blocks. */
                    pRegion->offRegion            = offRegionNext;
                    pRegion->cRegionBlocksOrBytes = pRegion->cRegionBlocksOrBytes / pRegion->cbBlock;
                    offRegionNext += pRegion->cRegionBlocksOrBytes;
                }
                else
                {
                    /* Convert from logical blocks to bytes. */
                    pRegion->offRegion            = offRegionNext;
                    pRegion->cRegionBlocksOrBytes = pRegion->cRegionBlocksOrBytes * pRegion->cbBlock;
                    offRegionNext += pRegion->cRegionBlocksOrBytes;
                }
            }
        }

        *ppRegionList = pRegionListNew;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Returns the virtual size of the image in bytes.
 *
 * @returns Size of the given image in bytes.
 * @param   pImage          The image to get the size from.
 */
static uint64_t vdImageGetSize(PVDIMAGE pImage)
{
    uint64_t cbImage = 0;

    if (pImage->cbImage == VD_IMAGE_SIZE_UNINITIALIZED)
    {
        PCVDREGIONLIST pRegionList = NULL;
        int rc = pImage->Backend->pfnQueryRegions(pImage->pBackendData, &pRegionList);
        if (RT_SUCCESS(rc))
        {
            if (pRegionList->fFlags & VD_REGION_LIST_F_LOC_SIZE_BLOCKS)
            {
                PVDREGIONLIST pRegionListConv = NULL;
                rc = vdRegionListConv(pRegionList, 0, &pRegionListConv);
                if (RT_SUCCESS(rc))
                {
                    for (uint32_t i = 0; i < pRegionListConv->cRegions; i++)
                        cbImage += pRegionListConv->aRegions[i].cRegionBlocksOrBytes;

                    VDRegionListFree(pRegionListConv);
                }
            }
            else
                for (uint32_t i = 0; i < pRegionList->cRegions; i++)
                    cbImage += pRegionList->aRegions[i].cRegionBlocksOrBytes;

            AssertPtr(pImage->Backend->pfnRegionListRelease);
            pImage->Backend->pfnRegionListRelease(pImage->pBackendData, pRegionList);
            pImage->cbImage = cbImage; /* Cache the value. */
        }
    }
    else
        cbImage = pImage->cbImage;

    return cbImage;
}

/**
 * Applies the filter chain to the given write request.
 *
 * @returns VBox status code.
 * @param   pDisk    The HDD container.
 * @param   uOffset  The start offset of the write.
 * @param   cbWrite  Number of bytes to write.
 * @param   pIoCtx   The I/O context associated with the request.
 */
static int vdFilterChainApplyWrite(PVDISK pDisk, uint64_t uOffset, size_t cbWrite,
                                   PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    PVDFILTER pFilter;
    RTListForEach(&pDisk->ListFilterChainWrite, pFilter, VDFILTER, ListNodeChainWrite)
    {
        rc = pFilter->pBackend->pfnFilterWrite(pFilter->pvBackendData, uOffset, cbWrite, pIoCtx);
        if (RT_FAILURE(rc))
            break;
        /* Reset S/G buffer for the next filter. */
        RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
    }

    return rc;
}

/**
 * Applies the filter chain to the given read request.
 *
 * @returns VBox status code.
 * @param   pDisk    The HDD container.
 * @param   uOffset  The start offset of the read.
 * @param   cbRead   Number of bytes read.
 * @param   pIoCtx   The I/O context associated with the request.
 */
static int vdFilterChainApplyRead(PVDISK pDisk, uint64_t uOffset, size_t cbRead,
                                  PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    /* Reset buffer before starting. */
    RTSgBufReset(&pIoCtx->Req.Io.SgBuf);

    PVDFILTER pFilter;
    RTListForEach(&pDisk->ListFilterChainRead, pFilter, VDFILTER, ListNodeChainRead)
    {
        rc = pFilter->pBackend->pfnFilterRead(pFilter->pvBackendData, uOffset, cbRead, pIoCtx);
        if (RT_FAILURE(rc))
            break;
        /* Reset S/G buffer for the next filter. */
        RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
    }

    return rc;
}

DECLINLINE(void) vdIoCtxRootComplete(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    if (   RT_SUCCESS(pIoCtx->rcReq)
        && pIoCtx->enmTxDir == VDIOCTXTXDIR_READ)
        pIoCtx->rcReq = vdFilterChainApplyRead(pDisk, pIoCtx->Req.Io.uOffsetXferOrig,
                                               pIoCtx->Req.Io.cbXferOrig, pIoCtx);

    pIoCtx->Type.Root.pfnComplete(pIoCtx->Type.Root.pvUser1,
                                  pIoCtx->Type.Root.pvUser2,
                                  pIoCtx->rcReq);
}

/**
 * Initialize the structure members of a given I/O context.
 */
DECLINLINE(void) vdIoCtxInit(PVDIOCTX pIoCtx, PVDISK pDisk, VDIOCTXTXDIR enmTxDir,
                             uint64_t uOffset, size_t cbTransfer, PVDIMAGE pImageStart,
                             PCRTSGBUF pSgBuf, void *pvAllocation,
                             PFNVDIOCTXTRANSFER pfnIoCtxTransfer, uint32_t fFlags)
{
    pIoCtx->pDisk                 = pDisk;
    pIoCtx->enmTxDir              = enmTxDir;
    pIoCtx->Req.Io.cbTransferLeft = (uint32_t)cbTransfer; Assert((uint32_t)cbTransfer == cbTransfer);
    pIoCtx->Req.Io.uOffset        = uOffset;
    pIoCtx->Req.Io.cbTransfer     = cbTransfer;
    pIoCtx->Req.Io.pImageStart    = pImageStart;
    pIoCtx->Req.Io.pImageCur      = pImageStart;
    pIoCtx->Req.Io.cbBufClear     = 0;
    pIoCtx->Req.Io.pImageParentOverride = NULL;
    pIoCtx->Req.Io.uOffsetXferOrig      = uOffset;
    pIoCtx->Req.Io.cbXferOrig           = cbTransfer;
    pIoCtx->cDataTransfersPending = 0;
    pIoCtx->cMetaTransfersPending = 0;
    pIoCtx->fComplete             = false;
    pIoCtx->fFlags                = fFlags;
    pIoCtx->pvAllocation          = pvAllocation;
    pIoCtx->pfnIoCtxTransfer      = pfnIoCtxTransfer;
    pIoCtx->pfnIoCtxTransferNext  = NULL;
    pIoCtx->rcReq                 = VINF_SUCCESS;
    pIoCtx->pIoCtxParent          = NULL;

    /* There is no S/G list for a flush request. */
    if (   enmTxDir != VDIOCTXTXDIR_FLUSH
        && enmTxDir != VDIOCTXTXDIR_DISCARD)
        RTSgBufClone(&pIoCtx->Req.Io.SgBuf, pSgBuf);
    else
        memset(&pIoCtx->Req.Io.SgBuf, 0, sizeof(RTSGBUF));
}

/**
 * Internal: Tries to read the desired range from the given cache.
 *
 * @returns VBox status code.
 * @retval  VERR_VD_BLOCK_FREE if the block is not in the cache.
 *          pcbRead will be set to the number of bytes not in the cache.
 *          Everything thereafter might be in the cache.
 * @param   pCache   The cache to read from.
 * @param   uOffset  Offset of the virtual disk to read.
 * @param   cbRead   How much to read.
 * @param   pIoCtx   The I/O context to read into.
 * @param   pcbRead  Where to store the number of bytes actually read.
 *                   On success this indicates the number of bytes read from the cache.
 *                   If VERR_VD_BLOCK_FREE is returned this gives the number of bytes
 *                   which are not in the cache.
 *                   In both cases everything beyond this value
 *                   might or might not be in the cache.
 */
static int vdCacheReadHelper(PVDCACHE pCache, uint64_t uOffset,
                             size_t cbRead, PVDIOCTX pIoCtx, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pCache=%#p uOffset=%llu pIoCtx=%p cbRead=%zu pcbRead=%#p\n",
                 pCache, uOffset, pIoCtx, cbRead, pcbRead));

    AssertPtr(pCache);
    AssertPtr(pcbRead);

    rc = pCache->Backend->pfnRead(pCache->pBackendData, uOffset, cbRead,
                                  pIoCtx, pcbRead);

    LogFlowFunc(("returns rc=%Rrc pcbRead=%zu\n", rc, *pcbRead));
    return rc;
}

/**
 * Internal: Writes data for the given block into the cache.
 *
 * @returns VBox status code.
 * @param   pCache     The cache to write to.
 * @param   uOffset    Offset of the virtual disk to write to the cache.
 * @param   cbWrite    How much to write.
 * @param   pIoCtx     The I/O context to write from.
 * @param   pcbWritten How much data could be written, optional.
 */
static int vdCacheWriteHelper(PVDCACHE pCache, uint64_t uOffset, size_t cbWrite,
                              PVDIOCTX pIoCtx, size_t *pcbWritten)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pCache=%#p uOffset=%llu pIoCtx=%p cbWrite=%zu pcbWritten=%#p\n",
                 pCache, uOffset, pIoCtx, cbWrite, pcbWritten));

    AssertPtr(pCache);
    AssertPtr(pIoCtx);
    Assert(cbWrite > 0);

    if (pcbWritten)
        rc = pCache->Backend->pfnWrite(pCache->pBackendData, uOffset, cbWrite,
                                       pIoCtx, pcbWritten);
    else
    {
        size_t cbWritten = 0;

        do
        {
            rc = pCache->Backend->pfnWrite(pCache->pBackendData, uOffset, cbWrite,
                                           pIoCtx, &cbWritten);
            uOffset += cbWritten;
            cbWrite -= cbWritten;
        } while (   cbWrite
                 && (   RT_SUCCESS(rc)
                     || rc == VERR_VD_ASYNC_IO_IN_PROGRESS));
    }

    LogFlowFunc(("returns rc=%Rrc pcbWritten=%zu\n",
                 rc, pcbWritten ? *pcbWritten : cbWrite));
    return rc;
}

/**
 * Creates a new empty discard state.
 *
 * @returns Pointer to the new discard state or NULL if out of memory.
 */
static PVDDISCARDSTATE vdDiscardStateCreate(void)
{
    PVDDISCARDSTATE pDiscard = (PVDDISCARDSTATE)RTMemAllocZ(sizeof(VDDISCARDSTATE));

    if (pDiscard)
    {
        RTListInit(&pDiscard->ListLru);
        pDiscard->pTreeBlocks = (PAVLRU64TREE)RTMemAllocZ(sizeof(AVLRU64TREE));
        if (!pDiscard->pTreeBlocks)
        {
            RTMemFree(pDiscard);
            pDiscard = NULL;
        }
    }

    return pDiscard;
}

/**
 * Removes the least recently used blocks from the waiting list until
 * the new value is reached.
 *
 * @returns VBox status code.
 * @param   pDisk              VD disk container.
 * @param   pDiscard           The discard state.
 * @param   cbDiscardingNew    How many bytes should be waiting on success.
 *                             The number of bytes waiting can be less.
 */
static int vdDiscardRemoveBlocks(PVDISK pDisk, PVDDISCARDSTATE pDiscard, size_t cbDiscardingNew)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p pDiscard=%#p cbDiscardingNew=%zu\n",
                 pDisk, pDiscard, cbDiscardingNew));

    while (pDiscard->cbDiscarding > cbDiscardingNew)
    {
        PVDDISCARDBLOCK pBlock = RTListGetLast(&pDiscard->ListLru, VDDISCARDBLOCK, NodeLru);

        Assert(!RTListIsEmpty(&pDiscard->ListLru));

        /* Go over the allocation bitmap and mark all discarded sectors as unused. */
        uint64_t offStart = pBlock->Core.Key;
        uint32_t idxStart = 0;
        size_t cbLeft = pBlock->cbDiscard;
        bool fAllocated = ASMBitTest(pBlock->pbmAllocated, idxStart);
        uint32_t cSectors = (uint32_t)(pBlock->cbDiscard / 512);

        while (cbLeft > 0)
        {
            int32_t idxEnd;
            size_t cbThis = cbLeft;

            if (fAllocated)
            {
                /* Check for the first unallocated bit. */
                idxEnd = ASMBitNextClear(pBlock->pbmAllocated, cSectors, idxStart);
                if (idxEnd != -1)
                {
                    cbThis = (idxEnd - idxStart) * 512;
                    fAllocated = false;
                }
            }
            else
            {
                /* Mark as unused and check for the first set bit. */
                idxEnd = ASMBitNextSet(pBlock->pbmAllocated, cSectors, idxStart);
                if (idxEnd != -1)
                    cbThis = (idxEnd - idxStart) * 512;


                VDIOCTX IoCtx;
                vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_DISCARD, 0, 0, NULL,
                            NULL, NULL, NULL, VDIOCTX_FLAGS_SYNC);
                rc = pDisk->pLast->Backend->pfnDiscard(pDisk->pLast->pBackendData,
                                                            &IoCtx, offStart, cbThis, NULL,
                                                            NULL, &cbThis, NULL,
                                                            VD_DISCARD_MARK_UNUSED);
                if (RT_FAILURE(rc))
                    break;

                fAllocated = true;
            }

            idxStart  = idxEnd;
            offStart += cbThis;
            cbLeft   -= cbThis;
        }

        if (RT_FAILURE(rc))
            break;

        PVDDISCARDBLOCK pBlockRemove = (PVDDISCARDBLOCK)RTAvlrU64RangeRemove(pDiscard->pTreeBlocks, pBlock->Core.Key);
        Assert(pBlockRemove == pBlock); NOREF(pBlockRemove);
        RTListNodeRemove(&pBlock->NodeLru);

        pDiscard->cbDiscarding -= pBlock->cbDiscard;
        RTMemFree(pBlock->pbmAllocated);
        RTMemFree(pBlock);
    }

    Assert(RT_FAILURE(rc) || pDiscard->cbDiscarding <= cbDiscardingNew);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Destroys the current discard state, writing any waiting blocks to the image.
 *
 * @returns VBox status code.
 * @param   pDisk    VD disk container.
 */
static int vdDiscardStateDestroy(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;

    if (pDisk->pDiscard)
    {
        rc = vdDiscardRemoveBlocks(pDisk, pDisk->pDiscard, 0 /* Remove all blocks. */);
        AssertRC(rc);
        RTMemFree(pDisk->pDiscard->pTreeBlocks);
        RTMemFree(pDisk->pDiscard);
        pDisk->pDiscard = NULL;
    }

    return rc;
}

/**
 * Marks the given range as allocated in the image.
 * Required if there are discards in progress and a write to a block which can get discarded
 * is written to.
 *
 * @returns VBox status code.
 * @param   pDisk    VD container data.
 * @param   uOffset  First byte to mark as allocated.
 * @param   cbRange  Number of bytes to mark as allocated.
 */
static int vdDiscardSetRangeAllocated(PVDISK pDisk, uint64_t uOffset, size_t cbRange)
{
    PVDDISCARDSTATE pDiscard = pDisk->pDiscard;
    int rc = VINF_SUCCESS;

    if (pDiscard)
    {
        do
        {
            size_t cbThisRange = cbRange;
            PVDDISCARDBLOCK pBlock = (PVDDISCARDBLOCK)RTAvlrU64RangeGet(pDiscard->pTreeBlocks, uOffset);

            if (pBlock)
            {
                int32_t idxStart, idxEnd;

                Assert(!(cbThisRange % 512));
                Assert(!((uOffset - pBlock->Core.Key) % 512));

                cbThisRange = RT_MIN(cbThisRange, pBlock->Core.KeyLast - uOffset + 1);

                idxStart = (uOffset - pBlock->Core.Key) / 512;
                idxEnd = idxStart + (int32_t)(cbThisRange / 512);
                ASMBitSetRange(pBlock->pbmAllocated, idxStart, idxEnd);
            }
            else
            {
                pBlock = (PVDDISCARDBLOCK)RTAvlrU64GetBestFit(pDiscard->pTreeBlocks, uOffset, true);
                if (pBlock)
                    cbThisRange = RT_MIN(cbThisRange, pBlock->Core.Key - uOffset);
            }

            Assert(cbRange >= cbThisRange);

            uOffset += cbThisRange;
            cbRange -= cbThisRange;
        } while (cbRange != 0);
    }

    return rc;
}

DECLINLINE(PVDIOCTX) vdIoCtxAlloc(PVDISK pDisk, VDIOCTXTXDIR enmTxDir,
                                  uint64_t uOffset, size_t cbTransfer,
                                  PVDIMAGE pImageStart,PCRTSGBUF pSgBuf,
                                  void *pvAllocation, PFNVDIOCTXTRANSFER pfnIoCtxTransfer,
                                  uint32_t fFlags)
{
    PVDIOCTX pIoCtx = NULL;

    pIoCtx = (PVDIOCTX)RTMemCacheAlloc(pDisk->hMemCacheIoCtx);
    if (RT_LIKELY(pIoCtx))
    {
        vdIoCtxInit(pIoCtx, pDisk, enmTxDir, uOffset, cbTransfer, pImageStart,
                    pSgBuf, pvAllocation, pfnIoCtxTransfer, fFlags);
    }

    return pIoCtx;
}

DECLINLINE(PVDIOCTX) vdIoCtxRootAlloc(PVDISK pDisk, VDIOCTXTXDIR enmTxDir,
                                      uint64_t uOffset, size_t cbTransfer,
                                      PVDIMAGE pImageStart, PCRTSGBUF pSgBuf,
                                      PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                                      void *pvUser1, void *pvUser2,
                                      void *pvAllocation,
                                      PFNVDIOCTXTRANSFER pfnIoCtxTransfer,
                                      uint32_t fFlags)
{
    PVDIOCTX pIoCtx = vdIoCtxAlloc(pDisk, enmTxDir, uOffset, cbTransfer, pImageStart,
                                   pSgBuf, pvAllocation, pfnIoCtxTransfer, fFlags);

    if (RT_LIKELY(pIoCtx))
    {
        pIoCtx->pIoCtxParent          = NULL;
        pIoCtx->Type.Root.pfnComplete = pfnComplete;
        pIoCtx->Type.Root.pvUser1     = pvUser1;
        pIoCtx->Type.Root.pvUser2     = pvUser2;
    }

    LogFlow(("Allocated root I/O context %#p\n", pIoCtx));
    return pIoCtx;
}

DECLINLINE(void) vdIoCtxDiscardInit(PVDIOCTX pIoCtx, PVDISK pDisk, PCRTRANGE paRanges,
                                    unsigned cRanges, PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                                    void *pvUser1, void *pvUser2, void *pvAllocation,
                                    PFNVDIOCTXTRANSFER pfnIoCtxTransfer, uint32_t fFlags)
{
    pIoCtx->pIoCtxNext                = NULL;
    pIoCtx->pDisk                     = pDisk;
    pIoCtx->enmTxDir                  = VDIOCTXTXDIR_DISCARD;
    pIoCtx->cDataTransfersPending     = 0;
    pIoCtx->cMetaTransfersPending     = 0;
    pIoCtx->fComplete                 = false;
    pIoCtx->fFlags                    = fFlags;
    pIoCtx->pvAllocation              = pvAllocation;
    pIoCtx->pfnIoCtxTransfer          = pfnIoCtxTransfer;
    pIoCtx->pfnIoCtxTransferNext      = NULL;
    pIoCtx->rcReq                     = VINF_SUCCESS;
    pIoCtx->Req.Discard.paRanges      = paRanges;
    pIoCtx->Req.Discard.cRanges       = cRanges;
    pIoCtx->Req.Discard.idxRange      = 0;
    pIoCtx->Req.Discard.cbDiscardLeft = 0;
    pIoCtx->Req.Discard.offCur        = 0;
    pIoCtx->Req.Discard.cbThisDiscard = 0;

    pIoCtx->pIoCtxParent          = NULL;
    pIoCtx->Type.Root.pfnComplete = pfnComplete;
    pIoCtx->Type.Root.pvUser1     = pvUser1;
    pIoCtx->Type.Root.pvUser2     = pvUser2;
}

DECLINLINE(PVDIOCTX) vdIoCtxDiscardAlloc(PVDISK pDisk, PCRTRANGE paRanges,
                                         unsigned cRanges,
                                         PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                                         void *pvUser1, void *pvUser2,
                                         void *pvAllocation,
                                         PFNVDIOCTXTRANSFER pfnIoCtxTransfer,
                                         uint32_t fFlags)
{
    PVDIOCTX pIoCtx = NULL;

    pIoCtx = (PVDIOCTX)RTMemCacheAlloc(pDisk->hMemCacheIoCtx);
    if (RT_LIKELY(pIoCtx))
    {
        vdIoCtxDiscardInit(pIoCtx, pDisk, paRanges, cRanges, pfnComplete, pvUser1,
                           pvUser2, pvAllocation, pfnIoCtxTransfer, fFlags);
    }

    LogFlow(("Allocated discard I/O context %#p\n", pIoCtx));
    return pIoCtx;
}

DECLINLINE(PVDIOCTX) vdIoCtxChildAlloc(PVDISK pDisk, VDIOCTXTXDIR enmTxDir,
                                       uint64_t uOffset, size_t cbTransfer,
                                       PVDIMAGE pImageStart, PCRTSGBUF pSgBuf,
                                       PVDIOCTX pIoCtxParent, size_t cbTransferParent,
                                       size_t cbWriteParent, void *pvAllocation,
                                       PFNVDIOCTXTRANSFER pfnIoCtxTransfer)
{
    PVDIOCTX pIoCtx = vdIoCtxAlloc(pDisk, enmTxDir, uOffset, cbTransfer, pImageStart,
                                   pSgBuf, pvAllocation, pfnIoCtxTransfer, pIoCtxParent->fFlags & ~VDIOCTX_FLAGS_DONT_FREE);

    AssertPtr(pIoCtxParent);
    Assert(!pIoCtxParent->pIoCtxParent);

    if (RT_LIKELY(pIoCtx))
    {
        pIoCtx->pIoCtxParent                   = pIoCtxParent;
        pIoCtx->Type.Child.uOffsetSaved        = uOffset;
        pIoCtx->Type.Child.cbTransferLeftSaved = cbTransfer;
        pIoCtx->Type.Child.cbTransferParent    = cbTransferParent;
        pIoCtx->Type.Child.cbWriteParent       = cbWriteParent;
    }

    LogFlow(("Allocated child I/O context %#p\n", pIoCtx));
    return pIoCtx;
}

DECLINLINE(PVDIOTASK) vdIoTaskUserAlloc(PVDIOSTORAGE pIoStorage, PFNVDXFERCOMPLETED pfnComplete, void *pvUser, PVDIOCTX pIoCtx, uint32_t cbTransfer)
{
    PVDIOTASK pIoTask = NULL;

    pIoTask = (PVDIOTASK)RTMemCacheAlloc(pIoStorage->pVDIo->pDisk->hMemCacheIoTask);
    if (pIoTask)
    {
        pIoTask->pIoStorage           = pIoStorage;
        pIoTask->pfnComplete          = pfnComplete;
        pIoTask->pvUser               = pvUser;
        pIoTask->fMeta                = false;
        pIoTask->Type.User.cbTransfer = cbTransfer;
        pIoTask->Type.User.pIoCtx     = pIoCtx;
    }

    return pIoTask;
}

DECLINLINE(PVDIOTASK) vdIoTaskMetaAlloc(PVDIOSTORAGE pIoStorage, PFNVDXFERCOMPLETED pfnComplete, void *pvUser, PVDMETAXFER pMetaXfer)
{
    PVDIOTASK pIoTask = NULL;

    pIoTask = (PVDIOTASK)RTMemCacheAlloc(pIoStorage->pVDIo->pDisk->hMemCacheIoTask);
    if (pIoTask)
    {
        pIoTask->pIoStorage          = pIoStorage;
        pIoTask->pfnComplete         = pfnComplete;
        pIoTask->pvUser              = pvUser;
        pIoTask->fMeta               = true;
        pIoTask->Type.Meta.pMetaXfer = pMetaXfer;
    }

    return pIoTask;
}

DECLINLINE(void) vdIoCtxFree(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    Log(("Freeing I/O context %#p\n", pIoCtx));

    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_DONT_FREE))
    {
        if (pIoCtx->pvAllocation)
            RTMemFree(pIoCtx->pvAllocation);
#ifdef DEBUG
        memset(&pIoCtx->pDisk, 0xff, sizeof(void *));
#endif
        RTMemCacheFree(pDisk->hMemCacheIoCtx, pIoCtx);
    }
}

DECLINLINE(void) vdIoTaskFree(PVDISK pDisk, PVDIOTASK pIoTask)
{
#ifdef DEBUG
    memset(pIoTask, 0xff, sizeof(VDIOTASK));
#endif
    RTMemCacheFree(pDisk->hMemCacheIoTask, pIoTask);
}

DECLINLINE(void) vdIoCtxChildReset(PVDIOCTX pIoCtx)
{
    AssertPtr(pIoCtx->pIoCtxParent);

    RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
    pIoCtx->Req.Io.uOffset        = pIoCtx->Type.Child.uOffsetSaved;
    pIoCtx->Req.Io.cbTransferLeft = (uint32_t)pIoCtx->Type.Child.cbTransferLeftSaved;
    Assert((uint32_t)pIoCtx->Type.Child.cbTransferLeftSaved == pIoCtx->Type.Child.cbTransferLeftSaved);
}

DECLINLINE(PVDMETAXFER) vdMetaXferAlloc(PVDIOSTORAGE pIoStorage, uint64_t uOffset, size_t cb)
{
    PVDMETAXFER pMetaXfer = (PVDMETAXFER)RTMemAlloc(RT_UOFFSETOF_DYN(VDMETAXFER, abData[cb]));

    if (RT_LIKELY(pMetaXfer))
    {
        pMetaXfer->Core.Key     = uOffset;
        pMetaXfer->Core.KeyLast = uOffset + cb - 1;
        pMetaXfer->fFlags       = VDMETAXFER_TXDIR_NONE;
        pMetaXfer->cbMeta       = cb;
        pMetaXfer->pIoStorage   = pIoStorage;
        pMetaXfer->cRefs        = 0;
        pMetaXfer->pbDataShw    = NULL;
        RTListInit(&pMetaXfer->ListIoCtxWaiting);
        RTListInit(&pMetaXfer->ListIoCtxShwWrites);
    }
    return pMetaXfer;
}

DECLINLINE(void) vdIoCtxAddToWaitingList(volatile PVDIOCTX *ppList, PVDIOCTX pIoCtx)
{
    /* Put it on the waiting list. */
    PVDIOCTX pNext = ASMAtomicUoReadPtrT(ppList, PVDIOCTX);
    PVDIOCTX pHeadOld;
    pIoCtx->pIoCtxNext = pNext;
    while (!ASMAtomicCmpXchgExPtr(ppList, pIoCtx, pNext, &pHeadOld))
    {
        pNext = pHeadOld;
        Assert(pNext != pIoCtx);
        pIoCtx->pIoCtxNext = pNext;
        ASMNopPause();
    }
}

DECLINLINE(void) vdIoCtxDefer(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    LogFlowFunc(("Deferring I/O context pIoCtx=%#p\n", pIoCtx));

    Assert(!pIoCtx->pIoCtxParent && !(pIoCtx->fFlags & VDIOCTX_FLAGS_BLOCKED));
    pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
    vdIoCtxAddToWaitingList(&pDisk->pIoCtxBlockedHead, pIoCtx);
}

static size_t vdIoCtxCopy(PVDIOCTX pIoCtxDst, PVDIOCTX pIoCtxSrc, size_t cbData)
{
    return RTSgBufCopy(&pIoCtxDst->Req.Io.SgBuf, &pIoCtxSrc->Req.Io.SgBuf, cbData);
}

#if 0 /* unused */
static int vdIoCtxCmp(PVDIOCTX pIoCtx1, PVDIOCTX pIoCtx2, size_t cbData)
{
    return RTSgBufCmp(&pIoCtx1->Req.Io.SgBuf, &pIoCtx2->Req.Io.SgBuf, cbData);
}
#endif

static size_t vdIoCtxCopyTo(PVDIOCTX pIoCtx, const uint8_t *pbData, size_t cbData)
{
    return RTSgBufCopyFromBuf(&pIoCtx->Req.Io.SgBuf, pbData, cbData);
}

static size_t vdIoCtxCopyFrom(PVDIOCTX pIoCtx, uint8_t *pbData, size_t cbData)
{
    return RTSgBufCopyToBuf(&pIoCtx->Req.Io.SgBuf, pbData, cbData);
}

static size_t vdIoCtxSet(PVDIOCTX pIoCtx, uint8_t ch, size_t cbData)
{
    return RTSgBufSet(&pIoCtx->Req.Io.SgBuf, ch, cbData);
}

/**
 * Returns whether the given I/O context has completed.
 *
 * @returns Flag whether the I/O context is complete.
 * @param   pIoCtx          The I/O context to check.
 */
DECLINLINE(bool) vdIoCtxIsComplete(PVDIOCTX pIoCtx)
{
    if (   !pIoCtx->cMetaTransfersPending
        && !pIoCtx->cDataTransfersPending
        && !pIoCtx->pfnIoCtxTransfer)
        return true;

    /*
     * We complete the I/O context in case of an error
     * if there is no I/O task pending.
     */
    if (   RT_FAILURE(pIoCtx->rcReq)
        && !pIoCtx->cMetaTransfersPending
        && !pIoCtx->cDataTransfersPending)
        return true;

    return false;
}

/**
 * Returns whether the given I/O context is blocked due to a metadata transfer
 * or because the backend blocked it.
 *
 * @returns Flag whether the I/O context is blocked.
 * @param   pIoCtx          The I/O context to check.
 */
DECLINLINE(bool) vdIoCtxIsBlocked(PVDIOCTX pIoCtx)
{
    /* Don't change anything if there is a metadata transfer pending or we are blocked. */
    if (   pIoCtx->cMetaTransfersPending
        || (pIoCtx->fFlags & VDIOCTX_FLAGS_BLOCKED))
        return true;

    return false;
}

/**
 * Process the I/O context, core method which assumes that the I/O context
 * acquired the lock.
 *
 * @returns VBox status code.
 * @param   pIoCtx    I/O context to process.
 */
static int vdIoCtxProcessLocked(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pIoCtx->pDisk);

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    if (!vdIoCtxIsComplete(pIoCtx))
    {
        if (!vdIoCtxIsBlocked(pIoCtx))
        {
            if (pIoCtx->pfnIoCtxTransfer)
            {
                /* Call the transfer function advancing to the next while there is no error. */
                while (   pIoCtx->pfnIoCtxTransfer
                       && !pIoCtx->cMetaTransfersPending
                       && RT_SUCCESS(rc))
                {
                    LogFlowFunc(("calling transfer function %#p\n", pIoCtx->pfnIoCtxTransfer));
                    rc = pIoCtx->pfnIoCtxTransfer(pIoCtx);

                    /* Advance to the next part of the transfer if the current one succeeded. */
                    if (RT_SUCCESS(rc))
                    {
                        pIoCtx->pfnIoCtxTransfer = pIoCtx->pfnIoCtxTransferNext;
                        pIoCtx->pfnIoCtxTransferNext = NULL;
                    }
                }
            }

            if (   RT_SUCCESS(rc)
                && !pIoCtx->cMetaTransfersPending
                && !pIoCtx->cDataTransfersPending
                && !(pIoCtx->fFlags & VDIOCTX_FLAGS_BLOCKED))
                rc = VINF_VD_ASYNC_IO_FINISHED;
            else if (   RT_SUCCESS(rc)
                     || rc == VERR_VD_NOT_ENOUGH_METADATA
                     || rc == VERR_VD_IOCTX_HALT)
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
            else if (   RT_FAILURE(rc)
                        && (rc != VERR_VD_ASYNC_IO_IN_PROGRESS))
            {
                ASMAtomicCmpXchgS32(&pIoCtx->rcReq, rc, VINF_SUCCESS);

                /*
                 * The I/O context completed if we have an error and there is no data
                 * or meta data transfer pending.
                 */
                if (   !pIoCtx->cMetaTransfersPending
                    && !pIoCtx->cDataTransfersPending)
                    rc = VINF_VD_ASYNC_IO_FINISHED;
                else
                    rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
            }
        }
        else
            rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
    }
    else
        rc = VINF_VD_ASYNC_IO_FINISHED;

    LogFlowFunc(("pIoCtx=%#p rc=%Rrc cDataTransfersPending=%u cMetaTransfersPending=%u fComplete=%RTbool\n",
                 pIoCtx, rc, pIoCtx->cDataTransfersPending, pIoCtx->cMetaTransfersPending,
                 pIoCtx->fComplete));

    return rc;
}

/**
 * Processes the list of waiting I/O contexts.
 *
 * @returns VBox status code, only valid if pIoCtxRc is not NULL, treat as void
 *          function otherwise.
 * @param   pDisk    The disk structure.
 * @param   pIoCtxRc An I/O context handle which waits on the list. When processed
 *                   The status code is returned. NULL if there is no I/O context
 *                   to return the status code for.
 */
static int vdDiskProcessWaitingIoCtx(PVDISK pDisk, PVDIOCTX pIoCtxRc)
{
    int rc = VERR_VD_ASYNC_IO_IN_PROGRESS;

    LogFlowFunc(("pDisk=%#p pIoCtxRc=%#p\n", pDisk, pIoCtxRc));

    VD_IS_LOCKED(pDisk);

    /* Get the waiting list and process it in FIFO order. */
    PVDIOCTX pIoCtxHead = ASMAtomicXchgPtrT(&pDisk->pIoCtxHead, NULL, PVDIOCTX);

    /* Reverse it. */
    PVDIOCTX pCur = pIoCtxHead;
    pIoCtxHead = NULL;
    while (pCur)
    {
        PVDIOCTX pInsert = pCur;
        pCur = pCur->pIoCtxNext;
        pInsert->pIoCtxNext = pIoCtxHead;
        pIoCtxHead = pInsert;
    }

    /* Process now. */
    pCur = pIoCtxHead;
    while (pCur)
    {
        int rcTmp;
        PVDIOCTX pTmp = pCur;

        pCur = pCur->pIoCtxNext;
        pTmp->pIoCtxNext = NULL;

        /*
         * Need to clear the sync flag here if there is a new I/O context
         * with it set and the context is not given in pIoCtxRc.
         * This happens most likely on a different thread and that one shouldn't
         * process the context synchronously.
         *
         * The thread who issued the context will wait on the event semaphore
         * anyway which is signalled when the completion handler is called.
         */
        if (   pTmp->fFlags & VDIOCTX_FLAGS_SYNC
            && pTmp != pIoCtxRc)
            pTmp->fFlags &= ~VDIOCTX_FLAGS_SYNC;

        rcTmp = vdIoCtxProcessLocked(pTmp);
        if (pTmp == pIoCtxRc)
        {
            if (   rcTmp == VINF_VD_ASYNC_IO_FINISHED
                && RT_SUCCESS(pTmp->rcReq)
                && pTmp->enmTxDir == VDIOCTXTXDIR_READ)
            {
                   int rc2 = vdFilterChainApplyRead(pDisk, pTmp->Req.Io.uOffsetXferOrig,
                                                    pTmp->Req.Io.cbXferOrig, pTmp);
                    if (RT_FAILURE(rc2))
                        rcTmp = rc2;
            }

            /* The given I/O context was processed, pass the return code to the caller. */
            if (   rcTmp == VINF_VD_ASYNC_IO_FINISHED
                && (pTmp->fFlags & VDIOCTX_FLAGS_SYNC))
                rc = pTmp->rcReq;
            else
                rc = rcTmp;
        }
        else if (   rcTmp == VINF_VD_ASYNC_IO_FINISHED
                 && ASMAtomicCmpXchgBool(&pTmp->fComplete, true, false))
        {
            LogFlowFunc(("Waiting I/O context completed pTmp=%#p\n", pTmp));
            vdThreadFinishWrite(pDisk);

            bool fFreeCtx = RT_BOOL(!(pTmp->fFlags & VDIOCTX_FLAGS_DONT_FREE));
            vdIoCtxRootComplete(pDisk, pTmp);

            if (fFreeCtx)
                vdIoCtxFree(pDisk, pTmp);
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Processes the list of blocked I/O contexts.
 *
 * @param   pDisk    The disk structure.
 */
static void vdDiskProcessBlockedIoCtx(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));

    VD_IS_LOCKED(pDisk);

    /* Get the waiting list and process it in FIFO order. */
    PVDIOCTX pIoCtxHead = ASMAtomicXchgPtrT(&pDisk->pIoCtxBlockedHead, NULL, PVDIOCTX);

    /* Reverse it. */
    PVDIOCTX pCur = pIoCtxHead;
    pIoCtxHead = NULL;
    while (pCur)
    {
        PVDIOCTX pInsert = pCur;
        pCur = pCur->pIoCtxNext;
        pInsert->pIoCtxNext = pIoCtxHead;
        pIoCtxHead = pInsert;
    }

    /* Process now. */
    pCur = pIoCtxHead;
    while (pCur)
    {
        int rc;
        PVDIOCTX pTmp = pCur;

        pCur = pCur->pIoCtxNext;
        pTmp->pIoCtxNext = NULL;

        Assert(!pTmp->pIoCtxParent);
        Assert(pTmp->fFlags & VDIOCTX_FLAGS_BLOCKED);
        pTmp->fFlags &= ~VDIOCTX_FLAGS_BLOCKED;

        rc = vdIoCtxProcessLocked(pTmp);
        if (   rc == VINF_VD_ASYNC_IO_FINISHED
            && ASMAtomicCmpXchgBool(&pTmp->fComplete, true, false))
        {
            LogFlowFunc(("Waiting I/O context completed pTmp=%#p\n", pTmp));
            vdThreadFinishWrite(pDisk);

            bool fFreeCtx = RT_BOOL(!(pTmp->fFlags & VDIOCTX_FLAGS_DONT_FREE));
            vdIoCtxRootComplete(pDisk, pTmp);
            if (fFreeCtx)
                vdIoCtxFree(pDisk, pTmp);
        }
    }

    LogFlowFunc(("returns\n"));
}

/**
 * Processes the I/O context trying to lock the criticial section.
 * The context is deferred if the critical section is busy.
 *
 * @returns VBox status code.
 * @param   pIoCtx    The I/O context to process.
 */
static int vdIoCtxProcessTryLockDefer(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = pIoCtx->pDisk;

    Log(("Defer pIoCtx=%#p\n", pIoCtx));

    /* Put it on the waiting list first. */
    vdIoCtxAddToWaitingList(&pDisk->pIoCtxHead, pIoCtx);

    if (ASMAtomicCmpXchgBool(&pDisk->fLocked, true, false))
    {
        /* Leave it again, the context will be processed just before leaving the lock. */
        LogFlowFunc(("Successfully acquired the lock\n"));
        rc = vdDiskUnlock(pDisk, pIoCtx);
    }
    else
    {
        LogFlowFunc(("Lock is held\n"));
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
    }

    return rc;
}

/**
 * Process the I/O context in a synchronous manner, waiting
 * for it to complete.
 *
 * @returns VBox status code of the completed request.
 * @param   pIoCtx            The sync I/O context.
 * @param   hEventComplete    Event sempahore to wait on for completion.
 */
static int vdIoCtxProcessSync(PVDIOCTX pIoCtx, RTSEMEVENT hEventComplete)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = pIoCtx->pDisk;

    LogFlowFunc(("pIoCtx=%p\n", pIoCtx));

    AssertMsg(pIoCtx->fFlags & (VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_DONT_FREE),
              ("I/O context is not marked as synchronous\n"));

    rc = vdIoCtxProcessTryLockDefer(pIoCtx);
    if (rc == VINF_VD_ASYNC_IO_FINISHED)
        rc = VINF_SUCCESS;

    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        rc = RTSemEventWait(hEventComplete, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }

    rc = pIoCtx->rcReq;
    vdIoCtxFree(pDisk, pIoCtx);

    return rc;
}

DECLINLINE(bool) vdIoCtxIsDiskLockOwner(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    return pDisk->pIoCtxLockOwner == pIoCtx;
}

static int vdIoCtxLockDisk(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    LogFlowFunc(("pDisk=%#p pIoCtx=%#p\n", pDisk, pIoCtx));

    if (!ASMAtomicCmpXchgPtr(&pDisk->pIoCtxLockOwner, pIoCtx, NIL_VDIOCTX))
    {
        Assert(pDisk->pIoCtxLockOwner != pIoCtx); /* No nesting allowed. */
        vdIoCtxDefer(pDisk, pIoCtx);
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
    }

    LogFlowFunc(("returns -> %Rrc\n", rc));
    return rc;
}

static void vdIoCtxUnlockDisk(PVDISK pDisk, PVDIOCTX pIoCtx, bool fProcessBlockedReqs)
{
    RT_NOREF1(pIoCtx);
    LogFlowFunc(("pDisk=%#p pIoCtx=%#p fProcessBlockedReqs=%RTbool\n",
                 pDisk, pIoCtx, fProcessBlockedReqs));

    VD_IS_LOCKED(pDisk);

    LogFlow(("Unlocking disk lock owner is %#p\n", pDisk->pIoCtxLockOwner));
    Assert(pDisk->pIoCtxLockOwner == pIoCtx);
    ASMAtomicXchgPtrT(&pDisk->pIoCtxLockOwner, NIL_VDIOCTX, PVDIOCTX);

    if (fProcessBlockedReqs)
    {
        /* Process any blocked writes if the current request didn't caused another growing. */
        vdDiskProcessBlockedIoCtx(pDisk);
    }

    LogFlowFunc(("returns\n"));
}

/**
 * Internal: Reads a given amount of data from the image chain of the disk.
 **/
static int vdDiskReadHelper(PVDISK pDisk, PVDIMAGE pImage, PVDIMAGE pImageParentOverride,
                            uint64_t uOffset, size_t cbRead, PVDIOCTX pIoCtx, size_t *pcbThisRead)
{
    RT_NOREF1(pDisk);
    int rc = VINF_SUCCESS;
    size_t cbThisRead = cbRead;

    AssertPtr(pcbThisRead);

    *pcbThisRead = 0;

    /*
     * Try to read from the given image.
     * If the block is not allocated read from override chain if present.
     */
    rc = pImage->Backend->pfnRead(pImage->pBackendData,
                                  uOffset, cbThisRead, pIoCtx,
                                  &cbThisRead);

    if (rc == VERR_VD_BLOCK_FREE)
    {
        for (PVDIMAGE pCurrImage = pImageParentOverride ? pImageParentOverride : pImage->pPrev;
             pCurrImage != NULL && rc == VERR_VD_BLOCK_FREE;
             pCurrImage = pCurrImage->pPrev)
        {
            rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                              uOffset, cbThisRead, pIoCtx,
                                              &cbThisRead);
        }
    }

    if (RT_SUCCESS(rc) || rc == VERR_VD_BLOCK_FREE)
        *pcbThisRead = cbThisRead;

    return rc;
}

/**
 * internal: read the specified amount of data in whatever blocks the backend
 * will give us - async version.
 */
static DECLCALLBACK(int) vdReadHelperAsync(PVDIOCTX pIoCtx)
{
    int rc;
    PVDISK pDisk                = pIoCtx->pDisk;
    size_t cbToRead               = pIoCtx->Req.Io.cbTransfer;
    uint64_t uOffset              = pIoCtx->Req.Io.uOffset;
    PVDIMAGE pCurrImage           = pIoCtx->Req.Io.pImageCur;
    PVDIMAGE pImageParentOverride = pIoCtx->Req.Io.pImageParentOverride;
    unsigned cImagesRead          = pIoCtx->Req.Io.cImagesRead;
    size_t cbThisRead;

    /*
     * Check whether there is a full block write in progress which was not allocated.
     * Defer I/O if the range interferes but only if it does not belong to the
     * write doing the allocation.
     */
    if (   pDisk->pIoCtxLockOwner != NIL_VDIOCTX
        && uOffset >= pDisk->uOffsetStartLocked
        && uOffset < pDisk->uOffsetEndLocked
        && (   !pIoCtx->pIoCtxParent
            || pIoCtx->pIoCtxParent != pDisk->pIoCtxLockOwner))
    {
        Log(("Interferring read while allocating a new block => deferring read\n"));
        vdIoCtxDefer(pDisk, pIoCtx);
        return VERR_VD_ASYNC_IO_IN_PROGRESS;
    }

    /* Loop until all reads started or we have a backend which needs to read metadata. */
    do
    {
        /* Search for image with allocated block. Do not attempt to read more
         * than the previous reads marked as valid. Otherwise this would return
         * stale data when different block sizes are used for the images. */
        cbThisRead = cbToRead;

        if (   pDisk->pCache
            && !pImageParentOverride)
        {
            rc = vdCacheReadHelper(pDisk->pCache, uOffset, cbThisRead,
                                   pIoCtx, &cbThisRead);
            if (rc == VERR_VD_BLOCK_FREE)
            {
                rc = vdDiskReadHelper(pDisk, pCurrImage, NULL, uOffset, cbThisRead,
                                      pIoCtx, &cbThisRead);

                /* If the read was successful, write the data back into the cache. */
                if (   RT_SUCCESS(rc)
                    && pIoCtx->fFlags & VDIOCTX_FLAGS_READ_UPDATE_CACHE)
                {
                    rc = vdCacheWriteHelper(pDisk->pCache, uOffset, cbThisRead,
                                            pIoCtx, NULL);
                }
            }
        }
        else
        {
            /*
             * Try to read from the given image.
             * If the block is not allocated read from override chain if present.
             */
            rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                              uOffset, cbThisRead, pIoCtx,
                                              &cbThisRead);

            if (   rc == VERR_VD_BLOCK_FREE
                && cImagesRead != 1)
            {
                unsigned cImagesToProcess = cImagesRead;

                pCurrImage = pImageParentOverride ? pImageParentOverride : pCurrImage->pPrev;
                pIoCtx->Req.Io.pImageParentOverride = NULL;

                while (pCurrImage && rc == VERR_VD_BLOCK_FREE)
                {
                    rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                                      uOffset, cbThisRead,
                                                      pIoCtx, &cbThisRead);
                    if (cImagesToProcess == 1)
                        break;
                    else if (cImagesToProcess > 0)
                        cImagesToProcess--;

                    if (rc == VERR_VD_BLOCK_FREE)
                        pCurrImage = pCurrImage->pPrev;
                }
            }
        }

        /* The task state will be updated on success already, don't do it here!. */
        if (rc == VERR_VD_BLOCK_FREE)
        {
            /* No image in the chain contains the data for the block. */
            ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbThisRead); Assert(cbThisRead == (uint32_t)cbThisRead);

            /* Fill the free space with 0 if we are told to do so
             * or a previous read returned valid data. */
            if (pIoCtx->fFlags & VDIOCTX_FLAGS_ZERO_FREE_BLOCKS)
                vdIoCtxSet(pIoCtx, '\0', cbThisRead);
            else
                pIoCtx->Req.Io.cbBufClear += cbThisRead;

            if (pIoCtx->Req.Io.pImageCur->uOpenFlags & VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS)
                rc = VINF_VD_NEW_ZEROED_BLOCK;
            else
                rc = VINF_SUCCESS;
        }
        else if (rc == VERR_VD_IOCTX_HALT)
        {
            uOffset  += cbThisRead;
            cbToRead -= cbThisRead;
            pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
        }
        else if (   RT_SUCCESS(rc)
                 || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        {
            /* First not free block, fill the space before with 0. */
            if (   pIoCtx->Req.Io.cbBufClear
                && !(pIoCtx->fFlags & VDIOCTX_FLAGS_ZERO_FREE_BLOCKS))
            {
                RTSGBUF SgBuf;
                RTSgBufClone(&SgBuf, &pIoCtx->Req.Io.SgBuf);
                RTSgBufReset(&SgBuf);
                RTSgBufSet(&SgBuf, 0, pIoCtx->Req.Io.cbBufClear);
                pIoCtx->Req.Io.cbBufClear = 0;
                pIoCtx->fFlags |= VDIOCTX_FLAGS_ZERO_FREE_BLOCKS;
            }
            rc = VINF_SUCCESS;
        }

        if (RT_FAILURE(rc))
            break;

        cbToRead -= cbThisRead;
        uOffset  += cbThisRead;
        pCurrImage = pIoCtx->Req.Io.pImageStart; /* Start with the highest image in the chain. */
    } while (cbToRead != 0 && RT_SUCCESS(rc));

    if (   rc == VERR_VD_NOT_ENOUGH_METADATA
        || rc == VERR_VD_IOCTX_HALT)
    {
        /* Save the current state. */
        pIoCtx->Req.Io.uOffset    = uOffset;
        pIoCtx->Req.Io.cbTransfer = cbToRead;
        pIoCtx->Req.Io.pImageCur  = pCurrImage ? pCurrImage : pIoCtx->Req.Io.pImageStart;
    }

    return (!(pIoCtx->fFlags & VDIOCTX_FLAGS_ZERO_FREE_BLOCKS))
           ? VERR_VD_BLOCK_FREE
           : rc;
}

/**
 * internal: parent image read wrapper for compacting.
 */
static DECLCALLBACK(int) vdParentRead(void *pvUser, uint64_t uOffset, void *pvBuf,
                                      size_t cbRead)
{
    PVDPARENTSTATEDESC pParentState = (PVDPARENTSTATEDESC)pvUser;

    /** @todo
     * Only used for compaction so far which is not possible to mix with async I/O.
     * Needs to be changed if we want to support online compaction of images.
     */
    bool fLocked = ASMAtomicXchgBool(&pParentState->pDisk->fLocked, true);
    AssertMsgReturn(!fLocked,
                    ("Calling synchronous parent read while another thread holds the disk lock\n"),
                    VERR_VD_INVALID_STATE);

    /* Fake an I/O context. */
    RTSGSEG Segment;
    RTSGBUF SgBuf;
    VDIOCTX IoCtx;

    Segment.pvSeg = pvBuf;
    Segment.cbSeg = cbRead;
    RTSgBufInit(&SgBuf, &Segment, 1);
    vdIoCtxInit(&IoCtx, pParentState->pDisk, VDIOCTXTXDIR_READ, uOffset, cbRead, pParentState->pImage,
                &SgBuf, NULL, NULL, VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_ZERO_FREE_BLOCKS);
    int rc = vdReadHelperAsync(&IoCtx);
    ASMAtomicXchgBool(&pParentState->pDisk->fLocked, false);
    return rc;
}

/**
 * Extended version of vdReadHelper(), implementing certain optimizations
 * for image cloning.
 *
 * @returns VBox status code.
 * @param   pDisk                   The disk to read from.
 * @param   pImage                  The image to start reading from.
 * @param   pImageParentOverride    The parent image to read from
 *                                  if the starting image returns a free block.
 *                                  If NULL is passed the real parent of the image
 *                                  in the chain is used.
 * @param   uOffset                 Offset in the disk to start reading from.
 * @param   pvBuf                   Where to store the read data.
 * @param   cbRead                  How much to read.
 * @param   fZeroFreeBlocks         Flag whether free blocks should be zeroed.
 *                                  If false and no image has data for sepcified
 *                                  range VERR_VD_BLOCK_FREE is returned.
 *                                  Note that unallocated blocks are still zeroed
 *                                  if at least one image has valid data for a part
 *                                  of the range.
 * @param   fUpdateCache            Flag whether to update the attached cache if
 *                                  available.
 * @param   cImagesRead             Number of images in the chain to read until
 *                                  the read is cut off. A value of 0 disables the cut off.
 */
static int vdReadHelperEx(PVDISK pDisk, PVDIMAGE pImage, PVDIMAGE pImageParentOverride,
                          uint64_t uOffset, void *pvBuf, size_t cbRead,
                          bool fZeroFreeBlocks, bool fUpdateCache, unsigned cImagesRead)
{
    int rc = VINF_SUCCESS;
    uint32_t fFlags = VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_DONT_FREE;
    RTSGSEG Segment;
    RTSGBUF SgBuf;
    VDIOCTX IoCtx;
    RTSEMEVENT hEventComplete = NIL_RTSEMEVENT;

    rc = RTSemEventCreate(&hEventComplete);
    if (RT_FAILURE(rc))
        return rc;

    if (fZeroFreeBlocks)
        fFlags |= VDIOCTX_FLAGS_ZERO_FREE_BLOCKS;
    if (fUpdateCache)
        fFlags |= VDIOCTX_FLAGS_READ_UPDATE_CACHE;

    Segment.pvSeg = pvBuf;
    Segment.cbSeg = cbRead;
    RTSgBufInit(&SgBuf, &Segment, 1);
    vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_READ, uOffset, cbRead, pImage, &SgBuf,
                NULL, vdReadHelperAsync, fFlags);

    IoCtx.Req.Io.pImageParentOverride = pImageParentOverride;
    IoCtx.Req.Io.cImagesRead = cImagesRead;
    IoCtx.Type.Root.pfnComplete = vdIoCtxSyncComplete;
    IoCtx.Type.Root.pvUser1     = pDisk;
    IoCtx.Type.Root.pvUser2     = hEventComplete;
    rc = vdIoCtxProcessSync(&IoCtx, hEventComplete);
    RTSemEventDestroy(hEventComplete);
    return rc;
}

/**
 * internal: read the specified amount of data in whatever blocks the backend
 * will give us.
 */
static int vdReadHelper(PVDISK pDisk, PVDIMAGE pImage, uint64_t uOffset,
                        void *pvBuf, size_t cbRead, bool fUpdateCache)
{
    return vdReadHelperEx(pDisk, pImage, NULL, uOffset, pvBuf, cbRead,
                          true /* fZeroFreeBlocks */, fUpdateCache, 0);
}

/**
 * internal: mark the disk as not modified.
 */
static void vdResetModifiedFlag(PVDISK pDisk)
{
    if (pDisk->uModified & VD_IMAGE_MODIFIED_FLAG)
    {
        /* generate new last-modified uuid */
        if (!(pDisk->uModified & VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE))
        {
            RTUUID Uuid;

            RTUuidCreate(&Uuid);
            pDisk->pLast->Backend->pfnSetModificationUuid(pDisk->pLast->pBackendData,
                                                          &Uuid);

            if (pDisk->pCache)
                pDisk->pCache->Backend->pfnSetModificationUuid(pDisk->pCache->pBackendData,
                                                               &Uuid);
        }

        pDisk->uModified &= ~VD_IMAGE_MODIFIED_FLAG;
    }
}

/**
 * internal: mark the disk as modified.
 */
static void vdSetModifiedFlag(PVDISK pDisk)
{
    pDisk->uModified |= VD_IMAGE_MODIFIED_FLAG;
    if (pDisk->uModified & VD_IMAGE_MODIFIED_FIRST)
    {
        pDisk->uModified &= ~VD_IMAGE_MODIFIED_FIRST;

        /* First modify, so create a UUID and ensure it's written to disk. */
        vdResetModifiedFlag(pDisk);

        if (!(pDisk->uModified & VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE))
        {
            VDIOCTX IoCtx;
            vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_FLUSH, 0, 0, NULL,
                        NULL, NULL, NULL, VDIOCTX_FLAGS_SYNC);
            pDisk->pLast->Backend->pfnFlush(pDisk->pLast->pBackendData, &IoCtx);
        }
    }
}

/**
 * internal: write buffer to the image, taking care of block boundaries and
 * write optimizations.
 */
static int vdWriteHelperEx(PVDISK pDisk, PVDIMAGE pImage,
                           PVDIMAGE pImageParentOverride, uint64_t uOffset,
                           const void *pvBuf, size_t cbWrite,
                           uint32_t fFlags, unsigned cImagesRead)
{
    int rc = VINF_SUCCESS;
    RTSGSEG Segment;
    RTSGBUF SgBuf;
    VDIOCTX IoCtx;
    RTSEMEVENT hEventComplete = NIL_RTSEMEVENT;

    rc = RTSemEventCreate(&hEventComplete);
    if (RT_FAILURE(rc))
        return rc;

    fFlags |= VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_DONT_FREE;

    Segment.pvSeg = (void *)pvBuf;
    Segment.cbSeg = cbWrite;
    RTSgBufInit(&SgBuf, &Segment, 1);
    vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_WRITE, uOffset, cbWrite, pImage, &SgBuf,
                NULL, vdWriteHelperAsync, fFlags);

    IoCtx.Req.Io.pImageParentOverride = pImageParentOverride;
    IoCtx.Req.Io.cImagesRead = cImagesRead;
    IoCtx.pIoCtxParent          = NULL;
    IoCtx.Type.Root.pfnComplete = vdIoCtxSyncComplete;
    IoCtx.Type.Root.pvUser1     = pDisk;
    IoCtx.Type.Root.pvUser2     = hEventComplete;
    if (RT_SUCCESS(rc))
        rc = vdIoCtxProcessSync(&IoCtx, hEventComplete);

    RTSemEventDestroy(hEventComplete);
    return rc;
}

/**
 * internal: write buffer to the image, taking care of block boundaries and
 * write optimizations.
 */
static int vdWriteHelper(PVDISK pDisk, PVDIMAGE pImage, uint64_t uOffset,
                         const void *pvBuf, size_t cbWrite, uint32_t fFlags)
{
    return vdWriteHelperEx(pDisk, pImage, NULL, uOffset, pvBuf, cbWrite,
                           fFlags, 0);
}

/**
 * Internal: Copies the content of one disk to another one applying optimizations
 * to speed up the copy process if possible.
 */
static int vdCopyHelper(PVDISK pDiskFrom, PVDIMAGE pImageFrom, PVDISK pDiskTo, PVDIMAGE pImageTo,
                        uint64_t cbSize, unsigned cImagesFromRead, unsigned cImagesToRead,
                        bool fSuppressRedundantIo, PVDINTERFACEPROGRESS pIfProgress,
                        PVDINTERFACEPROGRESS pDstIfProgress)
{
    int rc = VINF_SUCCESS;
    int rc2;
    uint64_t uOffset = 0;
    uint64_t cbRemaining = cbSize;
    void *pvBuf = NULL;
    bool fLockReadFrom = false;
    bool fLockWriteTo = false;
    bool fBlockwiseCopy = false;
    unsigned uProgressOld = 0;

    LogFlowFunc(("pDiskFrom=%#p pImageFrom=%#p pDiskTo=%#p pImageTo=%#p cbSize=%llu cImagesFromRead=%u cImagesToRead=%u fSuppressRedundantIo=%RTbool pIfProgress=%#p pDstIfProgress=%#p\n",
                 pDiskFrom, pImageFrom, pDiskTo, pImageTo, cbSize, cImagesFromRead, cImagesToRead, fSuppressRedundantIo, pDstIfProgress, pDstIfProgress));

    if (   (fSuppressRedundantIo || (cImagesFromRead > 0))
        && RTListIsEmpty(&pDiskFrom->ListFilterChainRead))
        fBlockwiseCopy = true;

    /* Allocate tmp buffer. */
    pvBuf = RTMemTmpAlloc(VD_MERGE_BUFFER_SIZE);
    if (!pvBuf)
        return rc;

    do
    {
        size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);

        /* Note that we don't attempt to synchronize cross-disk accesses.
         * It wouldn't be very difficult to do, just the lock order would
         * need to be defined somehow to prevent deadlocks. Postpone such
         * magic as there is no use case for this. */

        rc2 = vdThreadStartRead(pDiskFrom);
        AssertRC(rc2);
        fLockReadFrom = true;

        if (fBlockwiseCopy)
        {
            RTSGSEG SegmentBuf;
            RTSGBUF SgBuf;
            VDIOCTX IoCtx;

            SegmentBuf.pvSeg = pvBuf;
            SegmentBuf.cbSeg = VD_MERGE_BUFFER_SIZE;
            RTSgBufInit(&SgBuf, &SegmentBuf, 1);
            vdIoCtxInit(&IoCtx, pDiskFrom, VDIOCTXTXDIR_READ, 0, 0, NULL,
                        &SgBuf, NULL, NULL, VDIOCTX_FLAGS_SYNC);

            /* Read the source data. */
            rc = pImageFrom->Backend->pfnRead(pImageFrom->pBackendData,
                                              uOffset, cbThisRead, &IoCtx,
                                              &cbThisRead);

            if (   rc == VERR_VD_BLOCK_FREE
                && cImagesFromRead != 1)
            {
                unsigned cImagesToProcess = cImagesFromRead;

                for (PVDIMAGE pCurrImage = pImageFrom->pPrev;
                     pCurrImage != NULL && rc == VERR_VD_BLOCK_FREE;
                     pCurrImage = pCurrImage->pPrev)
                {
                    rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                                           uOffset, cbThisRead,
                                                           &IoCtx, &cbThisRead);
                    if (cImagesToProcess == 1)
                        break;
                    else if (cImagesToProcess > 0)
                        cImagesToProcess--;
                }
            }
        }
        else
            rc = vdReadHelper(pDiskFrom, pImageFrom, uOffset, pvBuf, cbThisRead,
                              false /* fUpdateCache */);

        if (RT_FAILURE(rc) && rc != VERR_VD_BLOCK_FREE)
            break;

        rc2 = vdThreadFinishRead(pDiskFrom);
        AssertRC(rc2);
        fLockReadFrom = false;

        if (rc != VERR_VD_BLOCK_FREE)
        {
            rc2 = vdThreadStartWrite(pDiskTo);
            AssertRC(rc2);
            fLockWriteTo = true;

            /* Only do collapsed I/O if we are copying the data blockwise. */
            rc = vdWriteHelperEx(pDiskTo, pImageTo, NULL, uOffset, pvBuf,
                                 cbThisRead, VDIOCTX_FLAGS_DONT_SET_MODIFIED_FLAG /* fFlags */,
                                 fBlockwiseCopy ? cImagesToRead : 0);
            if (RT_FAILURE(rc))
                break;

            rc2 = vdThreadFinishWrite(pDiskTo);
            AssertRC(rc2);
            fLockWriteTo = false;
        }
        else /* Don't propagate the error to the outside */
            rc = VINF_SUCCESS;

        uOffset += cbThisRead;
        cbRemaining -= cbThisRead;

        unsigned uProgressNew = uOffset * 99 / cbSize;
        if (uProgressNew != uProgressOld)
        {
            uProgressOld = uProgressNew;

            if (pIfProgress && pIfProgress->pfnProgress)
            {
                rc = pIfProgress->pfnProgress(pIfProgress->Core.pvUser,
                                              uProgressOld);
                if (RT_FAILURE(rc))
                    break;
            }
            if (pDstIfProgress && pDstIfProgress->pfnProgress)
            {
                rc = pDstIfProgress->pfnProgress(pDstIfProgress->Core.pvUser,
                                                 uProgressOld);
                if (RT_FAILURE(rc))
                    break;
            }
        }
    } while (uOffset < cbSize);

    RTMemFree(pvBuf);

    if (fLockReadFrom)
    {
        rc2 = vdThreadFinishRead(pDiskFrom);
        AssertRC(rc2);
    }

    if (fLockWriteTo)
    {
        rc2 = vdThreadFinishWrite(pDiskTo);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Flush helper async version.
 */
static DECLCALLBACK(int) vdSetModifiedHelperAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = pIoCtx->Req.Io.pImageCur;

    rc = pImage->Backend->pfnFlush(pImage->pBackendData, pIoCtx);
    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * internal: mark the disk as modified - async version.
 */
static int vdSetModifiedFlagAsync(PVDISK pDisk, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    pDisk->uModified |= VD_IMAGE_MODIFIED_FLAG;
    if (pDisk->uModified & VD_IMAGE_MODIFIED_FIRST)
    {
        rc = vdIoCtxLockDisk(pDisk, pIoCtx);
        if (RT_SUCCESS(rc))
        {
            pDisk->uModified &= ~VD_IMAGE_MODIFIED_FIRST;

            /* First modify, so create a UUID and ensure it's written to disk. */
            vdResetModifiedFlag(pDisk);

            if (!(pDisk->uModified & VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE))
            {
                PVDIOCTX pIoCtxFlush = vdIoCtxChildAlloc(pDisk, VDIOCTXTXDIR_FLUSH,
                                                         0, 0, pDisk->pLast,
                                                         NULL, pIoCtx, 0, 0, NULL,
                                                         vdSetModifiedHelperAsync);

                if (pIoCtxFlush)
                {
                    rc = vdIoCtxProcessLocked(pIoCtxFlush);
                    if (rc == VINF_VD_ASYNC_IO_FINISHED)
                    {
                        vdIoCtxUnlockDisk(pDisk, pIoCtx, false /* fProcessDeferredReqs */);
                        vdIoCtxFree(pDisk, pIoCtxFlush);
                    }
                    else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                    {
                        ASMAtomicIncU32(&pIoCtx->cDataTransfersPending);
                        pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
                    }
                    else /* Another error */
                        vdIoCtxFree(pDisk, pIoCtxFlush);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    return rc;
}

static DECLCALLBACK(int) vdWriteHelperCommitAsync(PVDIOCTX pIoCtx)
{
    int rc             = VINF_SUCCESS;
    PVDIMAGE pImage    = pIoCtx->Req.Io.pImageStart;
    size_t cbPreRead   = pIoCtx->Type.Child.cbPreRead;
    size_t cbPostRead  = pIoCtx->Type.Child.cbPostRead;
    size_t cbThisWrite = pIoCtx->Type.Child.cbTransferParent;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));
    rc = pImage->Backend->pfnWrite(pImage->pBackendData,
                                   pIoCtx->Req.Io.uOffset - cbPreRead,
                                   cbPreRead + cbThisWrite + cbPostRead,
                                   pIoCtx, NULL, &cbPreRead, &cbPostRead, 0);
    Assert(rc != VERR_VD_BLOCK_FREE);
    Assert(rc == VERR_VD_NOT_ENOUGH_METADATA || cbPreRead == 0);
    Assert(rc == VERR_VD_NOT_ENOUGH_METADATA || cbPostRead == 0);
    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        rc = VINF_SUCCESS;
    else if (rc == VERR_VD_IOCTX_HALT)
    {
        pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
        rc = VINF_SUCCESS;
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdWriteHelperOptimizedCmpAndWriteAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    size_t cbThisWrite    = 0;
    size_t cbPreRead      = pIoCtx->Type.Child.cbPreRead;
    size_t cbPostRead     = pIoCtx->Type.Child.cbPostRead;
    size_t cbWriteCopy    = pIoCtx->Type.Child.Write.Optimized.cbWriteCopy;
    size_t cbFill         = pIoCtx->Type.Child.Write.Optimized.cbFill;
    size_t cbReadImage    = pIoCtx->Type.Child.Write.Optimized.cbReadImage;
    PVDIOCTX pIoCtxParent = pIoCtx->pIoCtxParent;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    AssertPtr(pIoCtxParent);
    Assert(!pIoCtxParent->pIoCtxParent);
    Assert(!pIoCtx->Req.Io.cbTransferLeft && !pIoCtx->cMetaTransfersPending);

    vdIoCtxChildReset(pIoCtx);
    cbThisWrite = pIoCtx->Type.Child.cbTransferParent;
    RTSgBufAdvance(&pIoCtx->Req.Io.SgBuf, cbPreRead);

    /* Check if the write would modify anything in this block. */
    if (!RTSgBufCmp(&pIoCtx->Req.Io.SgBuf, &pIoCtxParent->Req.Io.SgBuf, cbThisWrite))
    {
        RTSGBUF SgBufSrcTmp;

        RTSgBufClone(&SgBufSrcTmp, &pIoCtxParent->Req.Io.SgBuf);
        RTSgBufAdvance(&SgBufSrcTmp, cbThisWrite);
        RTSgBufAdvance(&pIoCtx->Req.Io.SgBuf, cbThisWrite);

        if (!cbWriteCopy || !RTSgBufCmp(&pIoCtx->Req.Io.SgBuf, &SgBufSrcTmp, cbWriteCopy))
        {
            /* Block is completely unchanged, so no need to write anything. */
            LogFlowFunc(("Block didn't changed\n"));
            ASMAtomicWriteU32(&pIoCtx->Req.Io.cbTransferLeft, 0);
            RTSgBufAdvance(&pIoCtxParent->Req.Io.SgBuf, cbThisWrite);
            return VINF_VD_ASYNC_IO_FINISHED;
        }
    }

    /* Copy the data to the right place in the buffer. */
    RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
    RTSgBufAdvance(&pIoCtx->Req.Io.SgBuf, cbPreRead);
    vdIoCtxCopy(pIoCtx, pIoCtxParent, cbThisWrite);

    /* Handle the data that goes after the write to fill the block. */
    if (cbPostRead)
    {
        /* Now assemble the remaining data. */
        if (cbWriteCopy)
        {
            /*
             * The S/G buffer of the parent needs to be cloned because
             * it is not allowed to modify the state.
             */
            RTSGBUF SgBufParentTmp;

            RTSgBufClone(&SgBufParentTmp, &pIoCtxParent->Req.Io.SgBuf);
            RTSgBufCopy(&pIoCtx->Req.Io.SgBuf, &SgBufParentTmp, cbWriteCopy);
        }

        /* Zero out the remainder of this block. Will never be visible, as this
         * is beyond the limit of the image. */
        if (cbFill)
        {
            RTSgBufAdvance(&pIoCtx->Req.Io.SgBuf, cbReadImage);
            vdIoCtxSet(pIoCtx, '\0', cbFill);
        }
    }

    /* Write the full block to the virtual disk. */
    RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
    pIoCtx->pfnIoCtxTransferNext = vdWriteHelperCommitAsync;

    return rc;
}

static DECLCALLBACK(int) vdWriteHelperOptimizedPreReadAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    pIoCtx->fFlags |= VDIOCTX_FLAGS_ZERO_FREE_BLOCKS;

    if (   pIoCtx->Req.Io.cbTransferLeft
        && !pIoCtx->cDataTransfersPending)
        rc = vdReadHelperAsync(pIoCtx);

    if (   (   RT_SUCCESS(rc)
            || (rc == VERR_VD_ASYNC_IO_IN_PROGRESS))
        && (   pIoCtx->Req.Io.cbTransferLeft
            || pIoCtx->cMetaTransfersPending))
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
    else
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperOptimizedCmpAndWriteAsync;

    return rc;
}

/**
 * internal: write a complete block (only used for diff images), taking the
 * remaining data from parent images. This implementation optimizes out writes
 * that do not change the data relative to the state as of the parent images.
 * All backends which support differential/growing images support this - async version.
 */
static DECLCALLBACK(int) vdWriteHelperOptimizedAsync(PVDIOCTX pIoCtx)
{
    PVDISK pDisk = pIoCtx->pDisk;
    uint64_t uOffset   = pIoCtx->Type.Child.uOffsetSaved;
    size_t cbThisWrite = pIoCtx->Type.Child.cbTransferParent;
    size_t cbPreRead   = pIoCtx->Type.Child.cbPreRead;
    size_t cbPostRead  = pIoCtx->Type.Child.cbPostRead;
    size_t cbWrite     = pIoCtx->Type.Child.cbWriteParent;
    size_t cbFill = 0;
    size_t cbWriteCopy = 0;
    size_t cbReadImage = 0;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    AssertPtr(pIoCtx->pIoCtxParent);
    Assert(!pIoCtx->pIoCtxParent->pIoCtxParent);

    if (cbPostRead)
    {
        /* Figure out how much we cannot read from the image, because
         * the last block to write might exceed the nominal size of the
         * image for technical reasons. */
        if (uOffset + cbThisWrite + cbPostRead > pDisk->cbSize)
            cbFill = uOffset + cbThisWrite + cbPostRead - pDisk->cbSize;

        /* If we have data to be written, use that instead of reading
         * data from the image. */
        if (cbWrite > cbThisWrite)
            cbWriteCopy = RT_MIN(cbWrite - cbThisWrite, cbPostRead);

        /* The rest must be read from the image. */
        cbReadImage = cbPostRead - cbWriteCopy - cbFill;
    }

    pIoCtx->Type.Child.Write.Optimized.cbFill      = cbFill;
    pIoCtx->Type.Child.Write.Optimized.cbWriteCopy = cbWriteCopy;
    pIoCtx->Type.Child.Write.Optimized.cbReadImage = cbReadImage;

    /* Read the entire data of the block so that we can compare whether it will
     * be modified by the write or not. */
    size_t cbTmp = cbPreRead + cbThisWrite + cbPostRead - cbFill; Assert(cbTmp == (uint32_t)cbTmp);
    pIoCtx->Req.Io.cbTransferLeft = (uint32_t)cbTmp;
    pIoCtx->Req.Io.cbTransfer     = pIoCtx->Req.Io.cbTransferLeft;
    pIoCtx->Req.Io.uOffset       -= cbPreRead;

    /* Next step */
    pIoCtx->pfnIoCtxTransferNext = vdWriteHelperOptimizedPreReadAsync;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vdWriteHelperStandardReadImageAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    pIoCtx->fFlags |= VDIOCTX_FLAGS_ZERO_FREE_BLOCKS;

    if (   pIoCtx->Req.Io.cbTransferLeft
        && !pIoCtx->cDataTransfersPending)
        rc = vdReadHelperAsync(pIoCtx);

    if (   RT_SUCCESS(rc)
        && (   pIoCtx->Req.Io.cbTransferLeft
            || pIoCtx->cMetaTransfersPending))
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
    else
    {
        size_t cbFill = pIoCtx->Type.Child.Write.Optimized.cbFill;

        /* Zero out the remainder of this block. Will never be visible, as this
         * is beyond the limit of the image. */
        if (cbFill)
            vdIoCtxSet(pIoCtx, '\0', cbFill);

        /* Write the full block to the virtual disk. */
        RTSgBufReset(&pIoCtx->Req.Io.SgBuf);

        vdIoCtxChildReset(pIoCtx);
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperCommitAsync;
    }

    return rc;
}

static DECLCALLBACK(int) vdWriteHelperStandardAssemble(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    size_t cbPostRead  = pIoCtx->Type.Child.cbPostRead;
    size_t cbThisWrite = pIoCtx->Type.Child.cbTransferParent;
    PVDIOCTX pIoCtxParent = pIoCtx->pIoCtxParent;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    vdIoCtxCopy(pIoCtx, pIoCtxParent, cbThisWrite);
    if (cbPostRead)
    {
        size_t cbFill = pIoCtx->Type.Child.Write.Optimized.cbFill;
        size_t cbWriteCopy = pIoCtx->Type.Child.Write.Optimized.cbWriteCopy;
        size_t cbReadImage = pIoCtx->Type.Child.Write.Optimized.cbReadImage;

        /* Now assemble the remaining data. */
        if (cbWriteCopy)
        {
            /*
             * The S/G buffer of the parent needs to be cloned because
             * it is not allowed to modify the state.
             */
            RTSGBUF SgBufParentTmp;

            RTSgBufClone(&SgBufParentTmp, &pIoCtxParent->Req.Io.SgBuf);
            RTSgBufCopy(&pIoCtx->Req.Io.SgBuf, &SgBufParentTmp, cbWriteCopy);
        }

        if (cbReadImage)
        {
            /* Read remaining data. */
            pIoCtx->pfnIoCtxTransferNext = vdWriteHelperStandardReadImageAsync;

            /* Read the data that goes before the write to fill the block. */
            pIoCtx->Req.Io.cbTransferLeft = (uint32_t)cbReadImage; Assert(cbReadImage == (uint32_t)cbReadImage);
            pIoCtx->Req.Io.cbTransfer     = pIoCtx->Req.Io.cbTransferLeft;
            pIoCtx->Req.Io.uOffset       += cbWriteCopy;
        }
        else
        {
            /* Zero out the remainder of this block. Will never be visible, as this
             * is beyond the limit of the image. */
            if (cbFill)
                vdIoCtxSet(pIoCtx, '\0', cbFill);

            /* Write the full block to the virtual disk. */
            RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
            vdIoCtxChildReset(pIoCtx);
            pIoCtx->pfnIoCtxTransferNext = vdWriteHelperCommitAsync;
        }
    }
    else
    {
        /* Write the full block to the virtual disk. */
        RTSgBufReset(&pIoCtx->Req.Io.SgBuf);
        vdIoCtxChildReset(pIoCtx);
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperCommitAsync;
    }

    return rc;
}

static DECLCALLBACK(int) vdWriteHelperStandardPreReadAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    pIoCtx->fFlags |= VDIOCTX_FLAGS_ZERO_FREE_BLOCKS;

    if (   pIoCtx->Req.Io.cbTransferLeft
        && !pIoCtx->cDataTransfersPending)
        rc = vdReadHelperAsync(pIoCtx);

    if (   RT_SUCCESS(rc)
        && (   pIoCtx->Req.Io.cbTransferLeft
            || pIoCtx->cMetaTransfersPending))
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
     else
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperStandardAssemble;

    return rc;
}

static DECLCALLBACK(int) vdWriteHelperStandardAsync(PVDIOCTX pIoCtx)
{
    PVDISK pDisk = pIoCtx->pDisk;
    uint64_t uOffset   = pIoCtx->Type.Child.uOffsetSaved;
    size_t cbThisWrite = pIoCtx->Type.Child.cbTransferParent;
    size_t cbPreRead   = pIoCtx->Type.Child.cbPreRead;
    size_t cbPostRead  = pIoCtx->Type.Child.cbPostRead;
    size_t cbWrite     = pIoCtx->Type.Child.cbWriteParent;
    size_t cbFill = 0;
    size_t cbWriteCopy = 0;
    size_t cbReadImage = 0;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    AssertPtr(pIoCtx->pIoCtxParent);
    Assert(!pIoCtx->pIoCtxParent->pIoCtxParent);

    /* Calculate the amount of data to read that goes after the write to fill the block. */
    if (cbPostRead)
    {
        /* If we have data to be written, use that instead of reading
         * data from the image. */
        if (cbWrite > cbThisWrite)
            cbWriteCopy = RT_MIN(cbWrite - cbThisWrite, cbPostRead);
        else
            cbWriteCopy = 0;

        /* Figure out how much we cannot read from the image, because
         * the last block to write might exceed the nominal size of the
         * image for technical reasons. */
        if (uOffset + cbThisWrite + cbPostRead > pDisk->cbSize)
            cbFill = uOffset + cbThisWrite + cbPostRead - pDisk->cbSize;

        /* The rest must be read from the image. */
        cbReadImage = cbPostRead - cbWriteCopy - cbFill;
    }

    pIoCtx->Type.Child.Write.Optimized.cbFill      = cbFill;
    pIoCtx->Type.Child.Write.Optimized.cbWriteCopy = cbWriteCopy;
    pIoCtx->Type.Child.Write.Optimized.cbReadImage = cbReadImage;

    /* Next step */
    if (cbPreRead)
    {
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperStandardPreReadAsync;

        /* Read the data that goes before the write to fill the block. */
        pIoCtx->Req.Io.cbTransferLeft = (uint32_t)cbPreRead; Assert(cbPreRead == (uint32_t)cbPreRead);
        pIoCtx->Req.Io.cbTransfer     = pIoCtx->Req.Io.cbTransferLeft;
        pIoCtx->Req.Io.uOffset       -= cbPreRead;
    }
    else
        pIoCtx->pfnIoCtxTransferNext = vdWriteHelperStandardAssemble;

    return VINF_SUCCESS;
}

/**
 * internal: write buffer to the image, taking care of block boundaries and
 * write optimizations - async version.
 */
static DECLCALLBACK(int) vdWriteHelperAsync(PVDIOCTX pIoCtx)
{
    int rc;
    size_t cbWrite   = pIoCtx->Req.Io.cbTransfer;
    uint64_t uOffset = pIoCtx->Req.Io.uOffset;
    PVDIMAGE pImage  = pIoCtx->Req.Io.pImageCur;
    PVDISK pDisk   = pIoCtx->pDisk;
    unsigned fWrite;
    size_t cbThisWrite;
    size_t cbPreRead, cbPostRead;

    /* Apply write filter chain here if it was not done already. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_WRITE_FILTER_APPLIED))
    {
        rc = vdFilterChainApplyWrite(pDisk, uOffset, cbWrite, pIoCtx);
        if (RT_FAILURE(rc))
            return rc;
        pIoCtx->fFlags |= VDIOCTX_FLAGS_WRITE_FILTER_APPLIED;
    }

    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_DONT_SET_MODIFIED_FLAG))
    {
        rc = vdSetModifiedFlagAsync(pDisk, pIoCtx);
        if (RT_FAILURE(rc)) /* Includes I/O in progress. */
            return rc;
    }

    rc = vdDiscardSetRangeAllocated(pDisk, uOffset, cbWrite);
    if (RT_FAILURE(rc))
        return rc;

    /* Loop until all written. */
    do
    {
        /* Try to write the possibly partial block to the last opened image.
         * This works when the block is already allocated in this image or
         * if it is a full-block write (and allocation isn't suppressed below).
         * For image formats which don't support zero blocks, it's beneficial
         * to avoid unnecessarily allocating unchanged blocks. This prevents
         * unwanted expanding of images. VMDK is an example. */
        cbThisWrite = cbWrite;

        /*
         * Check whether there is a full block write in progress which was not allocated.
         * Defer I/O if the range interferes.
         */
        if (   pDisk->pIoCtxLockOwner != NIL_VDIOCTX
            && uOffset >= pDisk->uOffsetStartLocked
            && uOffset < pDisk->uOffsetEndLocked)
        {
            Log(("Interferring write while allocating a new block => deferring write\n"));
            vdIoCtxDefer(pDisk, pIoCtx);
            rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
            break;
        }

        fWrite =   (pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME)
                 ? 0 : VD_WRITE_NO_ALLOC;
        rc = pImage->Backend->pfnWrite(pImage->pBackendData, uOffset, cbThisWrite,
                                       pIoCtx, &cbThisWrite, &cbPreRead, &cbPostRead,
                                       fWrite);
        if (rc == VERR_VD_BLOCK_FREE)
        {
            /* Lock the disk .*/
            rc = vdIoCtxLockDisk(pDisk, pIoCtx);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Allocate segment and buffer in one go.
                 * A bit hackish but avoids the need to allocate memory twice.
                 */
                PRTSGBUF pTmp = (PRTSGBUF)RTMemAlloc(cbPreRead + cbThisWrite + cbPostRead + sizeof(RTSGSEG) + sizeof(RTSGBUF));
                AssertBreakStmt(pTmp, rc = VERR_NO_MEMORY);
                PRTSGSEG pSeg = (PRTSGSEG)(pTmp + 1);

                pSeg->pvSeg = pSeg + 1;
                pSeg->cbSeg = cbPreRead + cbThisWrite + cbPostRead;
                RTSgBufInit(pTmp, pSeg, 1);

                PVDIOCTX pIoCtxWrite = vdIoCtxChildAlloc(pDisk, VDIOCTXTXDIR_WRITE,
                                                         uOffset, pSeg->cbSeg, pImage,
                                                         pTmp,
                                                         pIoCtx, cbThisWrite,
                                                         cbWrite,
                                                         pTmp,
                                                           (pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME)
                                                         ? vdWriteHelperStandardAsync
                                                         : vdWriteHelperOptimizedAsync);
                if (!pIoCtxWrite)
                {
                    RTMemTmpFree(pTmp);
                    rc = VERR_NO_MEMORY;
                    break;
                }

                LogFlowFunc(("Disk is growing because of pIoCtx=%#p pIoCtxWrite=%#p\n",
                             pIoCtx, pIoCtxWrite));

                /* Save the current range for the growing operation to check for intersecting requests later. */
                pDisk->uOffsetStartLocked = uOffset - cbPreRead;
                pDisk->uOffsetEndLocked = uOffset + cbThisWrite + cbPostRead;

                pIoCtxWrite->Type.Child.cbPreRead  = cbPreRead;
                pIoCtxWrite->Type.Child.cbPostRead = cbPostRead;
                pIoCtxWrite->Req.Io.pImageParentOverride = pIoCtx->Req.Io.pImageParentOverride;

                /* Process the write request */
                rc = vdIoCtxProcessLocked(pIoCtxWrite);

                if (RT_FAILURE(rc) && (rc != VERR_VD_ASYNC_IO_IN_PROGRESS))
                {
                    vdIoCtxUnlockDisk(pDisk, pIoCtx, false /* fProcessDeferredReqs*/ );
                    vdIoCtxFree(pDisk, pIoCtxWrite);
                    break;
                }
                else if (   rc == VINF_VD_ASYNC_IO_FINISHED
                         && ASMAtomicCmpXchgBool(&pIoCtxWrite->fComplete, true, false))
                {
                    LogFlow(("Child write request completed\n"));
                    Assert(pIoCtx->Req.Io.cbTransferLeft >= cbThisWrite);
                    Assert(cbThisWrite == (uint32_t)cbThisWrite);
                    rc = pIoCtxWrite->rcReq;
                    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbThisWrite);
                    vdIoCtxUnlockDisk(pDisk, pIoCtx, false /* fProcessDeferredReqs*/ );
                    vdIoCtxFree(pDisk, pIoCtxWrite);
                }
                else
                {
                    LogFlow(("Child write pending\n"));
                    ASMAtomicIncU32(&pIoCtx->cDataTransfersPending);
                    pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
                    rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
                    cbWrite -= cbThisWrite;
                    uOffset += cbThisWrite;
                    break;
                }
            }
            else
            {
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
                break;
            }
        }

        if (rc == VERR_VD_IOCTX_HALT)
        {
            cbWrite -= cbThisWrite;
            uOffset += cbThisWrite;
            pIoCtx->fFlags |= VDIOCTX_FLAGS_BLOCKED;
            break;
        }
        else if (rc == VERR_VD_NOT_ENOUGH_METADATA)
            break;

        cbWrite -= cbThisWrite;
        uOffset += cbThisWrite;
    } while (cbWrite != 0 && (RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS));

    if (   rc == VERR_VD_ASYNC_IO_IN_PROGRESS
        || rc == VERR_VD_NOT_ENOUGH_METADATA
        || rc == VERR_VD_IOCTX_HALT)
    {
        /*
         * Tell the caller that we don't need to go back here because all
         * writes are initiated.
         */
        if (   !cbWrite
            && rc != VERR_VD_IOCTX_HALT)
            rc = VINF_SUCCESS;

        pIoCtx->Req.Io.uOffset    = uOffset;
        pIoCtx->Req.Io.cbTransfer = cbWrite;
    }

    return rc;
}

/**
 * Flush helper async version.
 */
static DECLCALLBACK(int) vdFlushHelperAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = pIoCtx->pDisk;
    PVDIMAGE pImage = pIoCtx->Req.Io.pImageCur;

    rc = vdIoCtxLockDisk(pDisk, pIoCtx);
    if (RT_SUCCESS(rc))
    {
        /* Mark the whole disk as locked. */
        pDisk->uOffsetStartLocked = 0;
        pDisk->uOffsetEndLocked = UINT64_C(0xffffffffffffffff);

        vdResetModifiedFlag(pDisk);
        rc = pImage->Backend->pfnFlush(pImage->pBackendData, pIoCtx);
        if (   (   RT_SUCCESS(rc)
                || rc == VERR_VD_ASYNC_IO_IN_PROGRESS
                || rc == VERR_VD_IOCTX_HALT)
            && pDisk->pCache)
        {
            rc = pDisk->pCache->Backend->pfnFlush(pDisk->pCache->pBackendData, pIoCtx);
            if (   RT_SUCCESS(rc)
                || (   rc != VERR_VD_ASYNC_IO_IN_PROGRESS
                    && rc != VERR_VD_IOCTX_HALT))
                vdIoCtxUnlockDisk(pDisk, pIoCtx, true /* fProcessBlockedReqs */);
            else if (rc != VERR_VD_IOCTX_HALT)
                rc = VINF_SUCCESS;
        }
        else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            rc = VINF_SUCCESS;
        else if (rc != VERR_VD_IOCTX_HALT)/* Some other error. */
            vdIoCtxUnlockDisk(pDisk, pIoCtx, true /* fProcessBlockedReqs */);
    }

    return rc;
}

/**
 * Async discard helper - discards a whole block which is recorded in the block
 * tree.
 *
 * @returns VBox status code.
 * @param   pIoCtx    The I/O context to operate on.
 */
static DECLCALLBACK(int) vdDiscardWholeBlockAsync(PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = pIoCtx->pDisk;
    PVDDISCARDSTATE pDiscard = pDisk->pDiscard;
    PVDDISCARDBLOCK pBlock = pIoCtx->Req.Discard.pBlock;
    size_t cbPreAllocated, cbPostAllocated, cbActuallyDiscarded;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    AssertPtr(pBlock);

    rc = pDisk->pLast->Backend->pfnDiscard(pDisk->pLast->pBackendData, pIoCtx,
                                                pBlock->Core.Key, pBlock->cbDiscard,
                                                &cbPreAllocated, &cbPostAllocated,
                                                &cbActuallyDiscarded, NULL, 0);
    Assert(rc != VERR_VD_DISCARD_ALIGNMENT_NOT_MET);
    Assert(!cbPreAllocated);
    Assert(!cbPostAllocated);
    Assert(cbActuallyDiscarded == pBlock->cbDiscard || RT_FAILURE(rc));

    /* Remove the block on success. */
    if (   RT_SUCCESS(rc)
        || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        PVDDISCARDBLOCK pBlockRemove = (PVDDISCARDBLOCK)RTAvlrU64RangeRemove(pDiscard->pTreeBlocks, pBlock->Core.Key);
        Assert(pBlockRemove == pBlock); RT_NOREF1(pBlockRemove);

        pDiscard->cbDiscarding -= pBlock->cbDiscard;
        RTListNodeRemove(&pBlock->NodeLru);
        RTMemFree(pBlock->pbmAllocated);
        RTMemFree(pBlock);
        pIoCtx->Req.Discard.pBlock = NULL;/* Safety precaution. */
        pIoCtx->pfnIoCtxTransferNext = vdDiscardHelperAsync; /* Next part. */
        rc = VINF_SUCCESS;
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Removes the least recently used blocks from the waiting list until
 * the new value is reached - version for async I/O.
 *
 * @returns VBox status code.
 * @param   pDisk              VD disk container.
 * @param   pIoCtx             The I/O context associated with this discard operation.
 * @param   cbDiscardingNew    How many bytes should be waiting on success.
 *                             The number of bytes waiting can be less.
 */
static int vdDiscardRemoveBlocksAsync(PVDISK pDisk, PVDIOCTX pIoCtx, size_t cbDiscardingNew)
{
    int rc = VINF_SUCCESS;
    PVDDISCARDSTATE pDiscard = pDisk->pDiscard;

    LogFlowFunc(("pDisk=%#p pDiscard=%#p cbDiscardingNew=%zu\n",
                 pDisk, pDiscard, cbDiscardingNew));

    while (pDiscard->cbDiscarding > cbDiscardingNew)
    {
        PVDDISCARDBLOCK pBlock = RTListGetLast(&pDiscard->ListLru, VDDISCARDBLOCK, NodeLru);

        Assert(!RTListIsEmpty(&pDiscard->ListLru));

        /* Go over the allocation bitmap and mark all discarded sectors as unused. */
        uint64_t offStart = pBlock->Core.Key;
        uint32_t idxStart = 0;
        size_t cbLeft = pBlock->cbDiscard;
        bool fAllocated = ASMBitTest(pBlock->pbmAllocated, idxStart);
        uint32_t cSectors = (uint32_t)(pBlock->cbDiscard / 512);

        while (cbLeft > 0)
        {
            int32_t idxEnd;
            size_t cbThis = cbLeft;

            if (fAllocated)
            {
                /* Check for the first unallocated bit. */
                idxEnd = ASMBitNextClear(pBlock->pbmAllocated, cSectors, idxStart);
                if (idxEnd != -1)
                {
                    cbThis = (idxEnd - idxStart) * 512;
                    fAllocated = false;
                }
            }
            else
            {
                /* Mark as unused and check for the first set bit. */
                idxEnd = ASMBitNextSet(pBlock->pbmAllocated, cSectors, idxStart);
                if (idxEnd != -1)
                    cbThis = (idxEnd - idxStart) * 512;

                rc = pDisk->pLast->Backend->pfnDiscard(pDisk->pLast->pBackendData, pIoCtx,
                                                            offStart, cbThis, NULL, NULL, &cbThis,
                                                            NULL, VD_DISCARD_MARK_UNUSED);
                if (      RT_FAILURE(rc)
                    && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
                    break;

                fAllocated = true;
            }

            idxStart  = idxEnd;
            offStart += cbThis;
            cbLeft   -= cbThis;
        }

        if (   RT_FAILURE(rc)
            && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
            break;

        PVDDISCARDBLOCK pBlockRemove = (PVDDISCARDBLOCK)RTAvlrU64RangeRemove(pDiscard->pTreeBlocks, pBlock->Core.Key);
        Assert(pBlockRemove == pBlock); NOREF(pBlockRemove);
        RTListNodeRemove(&pBlock->NodeLru);

        pDiscard->cbDiscarding -= pBlock->cbDiscard;
        RTMemFree(pBlock->pbmAllocated);
        RTMemFree(pBlock);
    }

    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        rc = VINF_SUCCESS;

    Assert(RT_FAILURE(rc) || pDiscard->cbDiscarding <= cbDiscardingNew);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Async discard helper - discards the current range if there is no matching
 * block in the tree.
 *
 * @returns VBox status code.
 * @param   pIoCtx    The I/O context to operate on.
 */
static DECLCALLBACK(int) vdDiscardCurrentRangeAsync(PVDIOCTX pIoCtx)
{
    PVDISK        pDisk         = pIoCtx->pDisk;
    PVDDISCARDSTATE pDiscard      = pDisk->pDiscard;
    uint64_t        offStart      = pIoCtx->Req.Discard.offCur;
    size_t          cbThisDiscard = pIoCtx->Req.Discard.cbThisDiscard;
    void *pbmAllocated = NULL;
    size_t cbPreAllocated, cbPostAllocated;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    /* No block found, try to discard using the backend first. */
    rc = pDisk->pLast->Backend->pfnDiscard(pDisk->pLast->pBackendData, pIoCtx,
                                                offStart, cbThisDiscard, &cbPreAllocated,
                                                &cbPostAllocated, &cbThisDiscard,
                                                &pbmAllocated, 0);
    if (rc == VERR_VD_DISCARD_ALIGNMENT_NOT_MET)
    {
        /* Create new discard block. */
        PVDDISCARDBLOCK pBlock = (PVDDISCARDBLOCK)RTMemAllocZ(sizeof(VDDISCARDBLOCK));
        if (pBlock)
        {
            pBlock->Core.Key     = offStart - cbPreAllocated;
            pBlock->Core.KeyLast = offStart + cbThisDiscard + cbPostAllocated - 1;
            pBlock->cbDiscard    = cbPreAllocated + cbThisDiscard + cbPostAllocated;
            pBlock->pbmAllocated = pbmAllocated;
            bool fInserted = RTAvlrU64Insert(pDiscard->pTreeBlocks, &pBlock->Core);
            Assert(fInserted); NOREF(fInserted);

            RTListPrepend(&pDiscard->ListLru, &pBlock->NodeLru);
            pDiscard->cbDiscarding += pBlock->cbDiscard;

            Assert(pIoCtx->Req.Discard.cbDiscardLeft >= cbThisDiscard);
            pIoCtx->Req.Discard.cbDiscardLeft -= cbThisDiscard;
            pIoCtx->Req.Discard.offCur        += cbThisDiscard;
            pIoCtx->Req.Discard.cbThisDiscard = cbThisDiscard;

            if (pDiscard->cbDiscarding > VD_DISCARD_REMOVE_THRESHOLD)
                rc = vdDiscardRemoveBlocksAsync(pDisk, pIoCtx, VD_DISCARD_REMOVE_THRESHOLD);
            else
                rc = VINF_SUCCESS;

            if (RT_SUCCESS(rc))
                pIoCtx->pfnIoCtxTransferNext = vdDiscardHelperAsync; /* Next part. */
        }
        else
        {
            RTMemFree(pbmAllocated);
            rc = VERR_NO_MEMORY;
        }
    }
    else if (   RT_SUCCESS(rc)
             || rc == VERR_VD_ASYNC_IO_IN_PROGRESS) /* Save state and andvance to next range. */
    {
        Assert(pIoCtx->Req.Discard.cbDiscardLeft >= cbThisDiscard);
        pIoCtx->Req.Discard.cbDiscardLeft -= cbThisDiscard;
        pIoCtx->Req.Discard.offCur        += cbThisDiscard;
        pIoCtx->Req.Discard.cbThisDiscard  = cbThisDiscard;
        pIoCtx->pfnIoCtxTransferNext       = vdDiscardHelperAsync;
        rc = VINF_SUCCESS;
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Async discard helper - entry point.
 *
 * @returns VBox status code.
 * @param   pIoCtx    The I/O context to operate on.
 */
static DECLCALLBACK(int) vdDiscardHelperAsync(PVDIOCTX pIoCtx)
{
    int rc             = VINF_SUCCESS;
    PVDISK  pDisk    = pIoCtx->pDisk;
    PCRTRANGE paRanges = pIoCtx->Req.Discard.paRanges;
    unsigned  cRanges  = pIoCtx->Req.Discard.cRanges;
    PVDDISCARDSTATE pDiscard = pDisk->pDiscard;

    LogFlowFunc(("pIoCtx=%#p\n", pIoCtx));

    /* Check if the I/O context processed all ranges. */
    if (   pIoCtx->Req.Discard.idxRange == cRanges
        && !pIoCtx->Req.Discard.cbDiscardLeft)
    {
        LogFlowFunc(("All ranges discarded, completing\n"));
        vdIoCtxUnlockDisk(pDisk, pIoCtx, true /* fProcessDeferredReqs*/);
        return VINF_SUCCESS;
    }

    if (pDisk->pIoCtxLockOwner != pIoCtx)
        rc = vdIoCtxLockDisk(pDisk, pIoCtx);

    if (RT_SUCCESS(rc))
    {
        uint64_t offStart      = pIoCtx->Req.Discard.offCur;
        size_t   cbDiscardLeft = pIoCtx->Req.Discard.cbDiscardLeft;
        size_t   cbThisDiscard;

        pDisk->uOffsetStartLocked = offStart;
        pDisk->uOffsetEndLocked = offStart + cbDiscardLeft;

        if (RT_UNLIKELY(!pDiscard))
        {
            pDiscard = vdDiscardStateCreate();
            if (!pDiscard)
                return VERR_NO_MEMORY;

            pDisk->pDiscard = pDiscard;
        }

        if (!pIoCtx->Req.Discard.cbDiscardLeft)
        {
            offStart      = paRanges[pIoCtx->Req.Discard.idxRange].offStart;
            cbDiscardLeft = paRanges[pIoCtx->Req.Discard.idxRange].cbRange;
            LogFlowFunc(("New range descriptor loaded (%u) offStart=%llu cbDiscard=%zu\n",
                         pIoCtx->Req.Discard.idxRange, offStart, cbDiscardLeft));
            pIoCtx->Req.Discard.idxRange++;
        }

        /* Look for a matching block in the AVL tree first. */
        PVDDISCARDBLOCK pBlock = (PVDDISCARDBLOCK)RTAvlrU64GetBestFit(pDiscard->pTreeBlocks, offStart, false);
        if (!pBlock || pBlock->Core.KeyLast < offStart)
        {
            PVDDISCARDBLOCK pBlockAbove = (PVDDISCARDBLOCK)RTAvlrU64GetBestFit(pDiscard->pTreeBlocks, offStart, true);

            /* Clip range to remain in the current block. */
            if (pBlockAbove)
                cbThisDiscard = RT_MIN(cbDiscardLeft, pBlockAbove->Core.KeyLast - offStart + 1);
            else
                cbThisDiscard = cbDiscardLeft;

            Assert(!(cbThisDiscard % 512));
            pIoCtx->Req.Discard.pBlock   = NULL;
            pIoCtx->pfnIoCtxTransferNext = vdDiscardCurrentRangeAsync;
        }
        else
        {
            /* Range lies partly in the block, update allocation bitmap. */
            int32_t idxStart, idxEnd;

            cbThisDiscard = RT_MIN(cbDiscardLeft, pBlock->Core.KeyLast - offStart + 1);

            AssertPtr(pBlock);

            Assert(!(cbThisDiscard % 512));
            Assert(!((offStart - pBlock->Core.Key) % 512));

            idxStart = (offStart - pBlock->Core.Key) / 512;
            idxEnd = idxStart + (int32_t)(cbThisDiscard / 512);

            ASMBitClearRange(pBlock->pbmAllocated, idxStart, idxEnd);

            cbDiscardLeft -= cbThisDiscard;
            offStart      += cbThisDiscard;

            /* Call the backend to discard the block if it is completely unallocated now. */
            if (ASMBitFirstSet((volatile void *)pBlock->pbmAllocated, (uint32_t)(pBlock->cbDiscard / 512)) == -1)
            {
                pIoCtx->Req.Discard.pBlock   = pBlock;
                pIoCtx->pfnIoCtxTransferNext = vdDiscardWholeBlockAsync;
                rc = VINF_SUCCESS;
            }
            else
            {
                RTListNodeRemove(&pBlock->NodeLru);
                RTListPrepend(&pDiscard->ListLru, &pBlock->NodeLru);

                /* Start with next range. */
                pIoCtx->pfnIoCtxTransferNext = vdDiscardHelperAsync;
                rc = VINF_SUCCESS;
            }
        }

        /* Save state in the context. */
        pIoCtx->Req.Discard.offCur        = offStart;
        pIoCtx->Req.Discard.cbDiscardLeft = cbDiscardLeft;
        pIoCtx->Req.Discard.cbThisDiscard = cbThisDiscard;
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * VD async I/O interface open callback.
 */
static DECLCALLBACK(int) vdIOOpenFallback(void *pvUser, const char *pszLocation,
                                          uint32_t fOpen, PFNVDCOMPLETED pfnCompleted,
                                          void **ppStorage)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)RTMemAllocZ(sizeof(VDIIOFALLBACKSTORAGE));

    if (!pStorage)
        return VERR_NO_MEMORY;

    pStorage->pfnCompleted = pfnCompleted;

    /* Open the file. */
    int rc = RTFileOpen(&pStorage->File, pszLocation, fOpen);
    if (RT_SUCCESS(rc))
    {
        *ppStorage = pStorage;
        return VINF_SUCCESS;
    }

    RTMemFree(pStorage);
    return rc;
}

/**
 * VD async I/O interface close callback.
 */
static DECLCALLBACK(int) vdIOCloseFallback(void *pvUser, void *pvStorage)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    RTFileClose(pStorage->File);
    RTMemFree(pStorage);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vdIODeleteFallback(void *pvUser, const char *pcszFilename)
{
    RT_NOREF1(pvUser);
    return RTFileDelete(pcszFilename);
}

static DECLCALLBACK(int) vdIOMoveFallback(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    RT_NOREF1(pvUser);
    return RTFileMove(pcszSrc, pcszDst, fMove);
}

static DECLCALLBACK(int) vdIOGetFreeSpaceFallback(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    RT_NOREF1(pvUser);
    return RTFsQuerySizes(pcszFilename, NULL, pcbFreeSpace, NULL, NULL);
}

static DECLCALLBACK(int) vdIOGetModificationTimeFallback(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    RT_NOREF1(pvUser);
    RTFSOBJINFO info;
    int rc = RTPathQueryInfo(pcszFilename, &info, RTFSOBJATTRADD_NOTHING);
    if (RT_SUCCESS(rc))
        *pModificationTime = info.ModificationTime;
    return rc;
}

/**
 * VD async I/O interface callback for retrieving the file size.
 */
static DECLCALLBACK(int) vdIOGetSizeFallback(void *pvUser, void *pvStorage, uint64_t *pcbSize)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileQuerySize(pStorage->File, pcbSize);
}

/**
 * VD async I/O interface callback for setting the file size.
 */
static DECLCALLBACK(int) vdIOSetSizeFallback(void *pvUser, void *pvStorage, uint64_t cbSize)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileSetSize(pStorage->File, cbSize);
}

/**
 * VD async I/O interface callback for setting the file allocation size.
 */
static DECLCALLBACK(int) vdIOSetAllocationSizeFallback(void *pvUser, void *pvStorage, uint64_t cbSize,
                                                       uint32_t fFlags)
{
    RT_NOREF2(pvUser, fFlags);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileSetAllocationSize(pStorage->File, cbSize, RTFILE_ALLOC_SIZE_F_DEFAULT);
}

/**
 * VD async I/O interface callback for a synchronous write to the file.
 */
static DECLCALLBACK(int) vdIOWriteSyncFallback(void *pvUser, void *pvStorage, uint64_t uOffset,
                                               const void *pvBuf, size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileWriteAt(pStorage->File, uOffset, pvBuf, cbWrite, pcbWritten);
}

/**
 * VD async I/O interface callback for a synchronous read from the file.
 */
static DECLCALLBACK(int) vdIOReadSyncFallback(void *pvUser, void *pvStorage, uint64_t uOffset,
                                              void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileReadAt(pStorage->File, uOffset, pvBuf, cbRead, pcbRead);
}

/**
 * VD async I/O interface callback for a synchronous flush of the file data.
 */
static DECLCALLBACK(int) vdIOFlushSyncFallback(void *pvUser, void *pvStorage)
{
    RT_NOREF1(pvUser);
    PVDIIOFALLBACKSTORAGE pStorage = (PVDIIOFALLBACKSTORAGE)pvStorage;

    return RTFileFlush(pStorage->File);
}

/**
 * Internal - Continues an I/O context after
 * it was halted because of an active transfer.
 */
static int vdIoCtxContinue(PVDIOCTX pIoCtx, int rcReq)
{
    PVDISK pDisk = pIoCtx->pDisk;
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    if (RT_FAILURE(rcReq))
        ASMAtomicCmpXchgS32(&pIoCtx->rcReq, rcReq, VINF_SUCCESS);

    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_BLOCKED))
    {
        /* Continue the transfer */
        rc = vdIoCtxProcessLocked(pIoCtx);

        if (   rc == VINF_VD_ASYNC_IO_FINISHED
            && ASMAtomicCmpXchgBool(&pIoCtx->fComplete, true, false))
        {
            LogFlowFunc(("I/O context completed pIoCtx=%#p\n", pIoCtx));
            bool fFreeCtx = RT_BOOL(!(pIoCtx->fFlags & VDIOCTX_FLAGS_DONT_FREE));
            if (pIoCtx->pIoCtxParent)
            {
                PVDIOCTX pIoCtxParent = pIoCtx->pIoCtxParent;

                Assert(!pIoCtxParent->pIoCtxParent);
                if (RT_FAILURE(pIoCtx->rcReq))
                    ASMAtomicCmpXchgS32(&pIoCtxParent->rcReq, pIoCtx->rcReq, VINF_SUCCESS);

                ASMAtomicDecU32(&pIoCtxParent->cDataTransfersPending);

                if (pIoCtx->enmTxDir == VDIOCTXTXDIR_WRITE)
                {
                    LogFlowFunc(("I/O context transferred %u bytes for the parent pIoCtxParent=%p\n",
                                 pIoCtx->Type.Child.cbTransferParent, pIoCtxParent));

                    /* Update the parent state. */
                    Assert(pIoCtxParent->Req.Io.cbTransferLeft >= pIoCtx->Type.Child.cbTransferParent);
                    ASMAtomicSubU32(&pIoCtxParent->Req.Io.cbTransferLeft, (uint32_t)pIoCtx->Type.Child.cbTransferParent);
                }
                else
                    Assert(pIoCtx->enmTxDir == VDIOCTXTXDIR_FLUSH);

                /*
                 * A completed child write means that we finished growing the image.
                 * We have to process any pending writes now.
                 */
                vdIoCtxUnlockDisk(pDisk, pIoCtxParent, false /* fProcessDeferredReqs */);

                /* Unblock the parent */
                pIoCtxParent->fFlags &= ~VDIOCTX_FLAGS_BLOCKED;

                rc = vdIoCtxProcessLocked(pIoCtxParent);

                if (   rc == VINF_VD_ASYNC_IO_FINISHED
                    && ASMAtomicCmpXchgBool(&pIoCtxParent->fComplete, true, false))
                {
                    LogFlowFunc(("Parent I/O context completed pIoCtxParent=%#p rcReq=%Rrc\n", pIoCtxParent, pIoCtxParent->rcReq));
                    bool fFreeParentCtx = RT_BOOL(!(pIoCtxParent->fFlags & VDIOCTX_FLAGS_DONT_FREE));
                    vdIoCtxRootComplete(pDisk, pIoCtxParent);
                    vdThreadFinishWrite(pDisk);

                    if (fFreeParentCtx)
                        vdIoCtxFree(pDisk, pIoCtxParent);
                    vdDiskProcessBlockedIoCtx(pDisk);
                }
                else if (!vdIoCtxIsDiskLockOwner(pDisk, pIoCtx))
                {
                    /* Process any pending writes if the current request didn't caused another growing. */
                    vdDiskProcessBlockedIoCtx(pDisk);
                }
            }
            else
            {
                if (pIoCtx->enmTxDir == VDIOCTXTXDIR_FLUSH)
                {
                    vdIoCtxUnlockDisk(pDisk, pIoCtx, true /* fProcessDerredReqs */);
                    vdThreadFinishWrite(pDisk);
                }
                else if (   pIoCtx->enmTxDir == VDIOCTXTXDIR_WRITE
                         || pIoCtx->enmTxDir == VDIOCTXTXDIR_DISCARD)
                    vdThreadFinishWrite(pDisk);
                else
                {
                    Assert(pIoCtx->enmTxDir == VDIOCTXTXDIR_READ);
                    vdThreadFinishRead(pDisk);
                }

                LogFlowFunc(("I/O context completed pIoCtx=%#p rcReq=%Rrc\n", pIoCtx, pIoCtx->rcReq));
                vdIoCtxRootComplete(pDisk, pIoCtx);
            }

            if (fFreeCtx)
                vdIoCtxFree(pDisk, pIoCtx);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Internal - Called when user transfer completed.
 */
static int vdUserXferCompleted(PVDIOSTORAGE pIoStorage, PVDIOCTX pIoCtx,
                               PFNVDXFERCOMPLETED pfnComplete, void *pvUser,
                               size_t cbTransfer, int rcReq)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = pIoCtx->pDisk;

    LogFlowFunc(("pIoStorage=%#p pIoCtx=%#p pfnComplete=%#p pvUser=%#p cbTransfer=%zu rcReq=%Rrc\n",
                 pIoStorage, pIoCtx, pfnComplete, pvUser, cbTransfer, rcReq));

    VD_IS_LOCKED(pDisk);

    Assert(pIoCtx->Req.Io.cbTransferLeft >= cbTransfer);
    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbTransfer); Assert(cbTransfer == (uint32_t)cbTransfer);
    ASMAtomicDecU32(&pIoCtx->cDataTransfersPending);

    if (pfnComplete)
        rc = pfnComplete(pIoStorage->pVDIo->pBackendData, pIoCtx, pvUser, rcReq);

    if (RT_SUCCESS(rc))
        rc = vdIoCtxContinue(pIoCtx, rcReq);
    else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        rc = VINF_SUCCESS;

    return rc;
}

static void vdIoCtxContinueDeferredList(PVDIOSTORAGE pIoStorage, PRTLISTANCHOR pListWaiting,
                                        PFNVDXFERCOMPLETED pfnComplete, void *pvUser, int rcReq)
{
    LogFlowFunc(("pIoStorage=%#p pListWaiting=%#p pfnComplete=%#p pvUser=%#p rcReq=%Rrc\n",
                 pIoStorage, pListWaiting, pfnComplete, pvUser, rcReq));

    /* Go through the waiting list and continue the I/O contexts. */
    while (!RTListIsEmpty(pListWaiting))
    {
        int rc = VINF_SUCCESS;
        PVDIOCTXDEFERRED pDeferred = RTListGetFirst(pListWaiting, VDIOCTXDEFERRED, NodeDeferred);
        PVDIOCTX pIoCtx = pDeferred->pIoCtx;
        RTListNodeRemove(&pDeferred->NodeDeferred);

        RTMemFree(pDeferred);
        ASMAtomicDecU32(&pIoCtx->cMetaTransfersPending);

        if (pfnComplete)
            rc = pfnComplete(pIoStorage->pVDIo->pBackendData, pIoCtx, pvUser, rcReq);

        LogFlow(("Completion callback for I/O context %#p returned %Rrc\n", pIoCtx, rc));

        if (RT_SUCCESS(rc))
        {
            rc = vdIoCtxContinue(pIoCtx, rcReq);
            AssertRC(rc);
        }
        else
            Assert(rc == VERR_VD_ASYNC_IO_IN_PROGRESS);
    }
}

/**
 * Internal - Called when a meta transfer completed.
 */
static int vdMetaXferCompleted(PVDIOSTORAGE pIoStorage, PFNVDXFERCOMPLETED pfnComplete, void *pvUser,
                               PVDMETAXFER pMetaXfer, int rcReq)
{
    PVDISK pDisk = pIoStorage->pVDIo->pDisk;
    RTLISTANCHOR ListIoCtxWaiting;
    bool fFlush;

    LogFlowFunc(("pIoStorage=%#p pfnComplete=%#p pvUser=%#p pMetaXfer=%#p rcReq=%Rrc\n",
                 pIoStorage, pfnComplete, pvUser, pMetaXfer, rcReq));

    VD_IS_LOCKED(pDisk);

    fFlush = VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_FLUSH;

    if (!fFlush)
    {
        RTListMove(&ListIoCtxWaiting, &pMetaXfer->ListIoCtxWaiting);

        if (RT_FAILURE(rcReq))
        {
            /* Remove from the AVL tree. */
            LogFlow(("Removing meta xfer=%#p\n", pMetaXfer));
            bool fRemoved = RTAvlrFileOffsetRemove(pIoStorage->pTreeMetaXfers, pMetaXfer->Core.Key) != NULL;
            Assert(fRemoved); NOREF(fRemoved);
            /* If this was a write check if there is a shadow buffer with updated data. */
            if (pMetaXfer->pbDataShw)
            {
                Assert(VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_WRITE);
                Assert(!RTListIsEmpty(&pMetaXfer->ListIoCtxShwWrites));
                RTListConcatenate(&ListIoCtxWaiting, &pMetaXfer->ListIoCtxShwWrites);
                RTMemFree(pMetaXfer->pbDataShw);
                pMetaXfer->pbDataShw = NULL;
            }
            RTMemFree(pMetaXfer);
        }
        else
        {
            /* Increase the reference counter to make sure it doesn't go away before the last context is processed. */
            pMetaXfer->cRefs++;
        }
    }
    else
        RTListMove(&ListIoCtxWaiting, &pMetaXfer->ListIoCtxWaiting);

    VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_NONE);
    vdIoCtxContinueDeferredList(pIoStorage, &ListIoCtxWaiting, pfnComplete, pvUser, rcReq);

    /*
     * If there is a shadow buffer and the previous write was successful update with the
     * new data and trigger a new write.
     */
    if (   pMetaXfer->pbDataShw
        && RT_SUCCESS(rcReq)
        && VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_NONE)
    {
        LogFlowFunc(("pMetaXfer=%#p Updating from shadow buffer and triggering new write\n", pMetaXfer));
        memcpy(pMetaXfer->abData, pMetaXfer->pbDataShw, pMetaXfer->cbMeta);
        RTMemFree(pMetaXfer->pbDataShw);
        pMetaXfer->pbDataShw = NULL;
        Assert(!RTListIsEmpty(&pMetaXfer->ListIoCtxShwWrites));

        /* Setup a new I/O write. */
        PVDIOTASK pIoTask = vdIoTaskMetaAlloc(pIoStorage, pfnComplete, pvUser, pMetaXfer);
        if (RT_LIKELY(pIoTask))
        {
            void *pvTask = NULL;
            RTSGSEG Seg;

            Seg.cbSeg = pMetaXfer->cbMeta;
            Seg.pvSeg = pMetaXfer->abData;

            VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_WRITE);
            rcReq = pIoStorage->pVDIo->pInterfaceIo->pfnWriteAsync(pIoStorage->pVDIo->pInterfaceIo->Core.pvUser,
                                                                   pIoStorage->pStorage,
                                                                   pMetaXfer->Core.Key, &Seg, 1,
                                                                   pMetaXfer->cbMeta, pIoTask,
                                                                   &pvTask);
            if (   RT_SUCCESS(rcReq)
                || rcReq != VERR_VD_ASYNC_IO_IN_PROGRESS)
            {
                VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_NONE);
                vdIoTaskFree(pDisk, pIoTask);
            }
            else
                RTListMove(&pMetaXfer->ListIoCtxWaiting, &pMetaXfer->ListIoCtxShwWrites);
        }
        else
            rcReq = VERR_NO_MEMORY;

        /* Cleanup if there was an error or the request completed already. */
        if (rcReq != VERR_VD_ASYNC_IO_IN_PROGRESS)
            vdIoCtxContinueDeferredList(pIoStorage, &pMetaXfer->ListIoCtxShwWrites, pfnComplete, pvUser, rcReq);
    }

    /* Remove if not used anymore. */
    if (!fFlush)
    {
        pMetaXfer->cRefs--;
        if (!pMetaXfer->cRefs && RTListIsEmpty(&pMetaXfer->ListIoCtxWaiting))
        {
            /* Remove from the AVL tree. */
            LogFlow(("Removing meta xfer=%#p\n", pMetaXfer));
            bool fRemoved = RTAvlrFileOffsetRemove(pIoStorage->pTreeMetaXfers, pMetaXfer->Core.Key) != NULL;
            Assert(fRemoved); NOREF(fRemoved);
            RTMemFree(pMetaXfer);
        }
    }
    else if (fFlush)
        RTMemFree(pMetaXfer);

    return VINF_SUCCESS;
}

/**
 * Processes a list of waiting I/O tasks. The disk lock must be held by caller.
 *
 * @param   pDisk    The disk to process the list for.
 */
static void vdIoTaskProcessWaitingList(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));

    VD_IS_LOCKED(pDisk);

    PVDIOTASK pHead = ASMAtomicXchgPtrT(&pDisk->pIoTasksPendingHead, NULL, PVDIOTASK);

    Log(("I/O task list cleared\n"));

    /* Reverse order. */
    PVDIOTASK pCur = pHead;
    pHead = NULL;
    while (pCur)
    {
        PVDIOTASK pInsert = pCur;
        pCur = pCur->pNext;
        pInsert->pNext = pHead;
        pHead = pInsert;
    }

    while (pHead)
    {
        PVDIOSTORAGE pIoStorage = pHead->pIoStorage;

        if (!pHead->fMeta)
            vdUserXferCompleted(pIoStorage, pHead->Type.User.pIoCtx,
                                pHead->pfnComplete, pHead->pvUser,
                                pHead->Type.User.cbTransfer, pHead->rcReq);
        else
            vdMetaXferCompleted(pIoStorage, pHead->pfnComplete, pHead->pvUser,
                                pHead->Type.Meta.pMetaXfer, pHead->rcReq);

        pCur = pHead;
        pHead = pHead->pNext;
        vdIoTaskFree(pDisk, pCur);
    }
}

/**
 * Process any I/O context on the halted list.
 *
 * @param   pDisk    The disk.
 */
static void vdIoCtxProcessHaltedList(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));

    VD_IS_LOCKED(pDisk);

    /* Get the waiting list and process it in FIFO order. */
    PVDIOCTX pIoCtxHead = ASMAtomicXchgPtrT(&pDisk->pIoCtxHaltedHead, NULL, PVDIOCTX);

    /* Reverse it. */
    PVDIOCTX pCur = pIoCtxHead;
    pIoCtxHead = NULL;
    while (pCur)
    {
        PVDIOCTX pInsert = pCur;
        pCur = pCur->pIoCtxNext;
        pInsert->pIoCtxNext = pIoCtxHead;
        pIoCtxHead = pInsert;
    }

    /* Process now. */
    pCur = pIoCtxHead;
    while (pCur)
    {
        PVDIOCTX pTmp = pCur;

        pCur = pCur->pIoCtxNext;
        pTmp->pIoCtxNext = NULL;

        /* Continue */
        pTmp->fFlags &= ~VDIOCTX_FLAGS_BLOCKED;
        vdIoCtxContinue(pTmp, pTmp->rcReq);
    }
}

/**
 * Unlock the disk and process pending tasks.
 *
 * @returns VBox status code.
 * @param   pDisk    The disk to unlock.
 * @param   pIoCtxRc The I/O context to get the status code from, optional.
 */
static int vdDiskUnlock(PVDISK pDisk, PVDIOCTX pIoCtxRc)
{
    int rc = VINF_SUCCESS;

    VD_IS_LOCKED(pDisk);

    /*
     * Process the list of waiting I/O tasks first
     * because they might complete I/O contexts.
     * Same for the list of halted I/O contexts.
     * Afterwards comes the list of new I/O contexts.
     */
    vdIoTaskProcessWaitingList(pDisk);
    vdIoCtxProcessHaltedList(pDisk);
    rc = vdDiskProcessWaitingIoCtx(pDisk, pIoCtxRc);
    ASMAtomicXchgBool(&pDisk->fLocked, false);

    /*
     * Need to check for new I/O tasks and waiting I/O contexts now
     * again as other threads might added them while we processed
     * previous lists.
     */
    while (   ASMAtomicUoReadPtrT(&pDisk->pIoCtxHead, PVDIOCTX) != NULL
           || ASMAtomicUoReadPtrT(&pDisk->pIoTasksPendingHead, PVDIOTASK) != NULL
           || ASMAtomicUoReadPtrT(&pDisk->pIoCtxHaltedHead, PVDIOCTX) != NULL)
    {
        /* Try lock disk again. */
        if (ASMAtomicCmpXchgBool(&pDisk->fLocked, true, false))
        {
            vdIoTaskProcessWaitingList(pDisk);
            vdIoCtxProcessHaltedList(pDisk);
            vdDiskProcessWaitingIoCtx(pDisk, NULL);
            ASMAtomicXchgBool(&pDisk->fLocked, false);
        }
        else /* Let the other thread everything when he unlocks the disk. */
            break;
    }

    return rc;
}

/**
 * Try to lock the disk to complete pressing of the I/O task.
 * The completion is deferred if the disk is locked already.
 *
 * @param   pIoTask  The I/O task to complete.
 */
static void vdXferTryLockDiskDeferIoTask(PVDIOTASK pIoTask)
{
    PVDIOSTORAGE pIoStorage = pIoTask->pIoStorage;
    PVDISK pDisk = pIoStorage->pVDIo->pDisk;

    Log(("Deferring I/O task pIoTask=%p\n", pIoTask));

    /* Put it on the waiting list. */
    PVDIOTASK pNext = ASMAtomicUoReadPtrT(&pDisk->pIoTasksPendingHead, PVDIOTASK);
    PVDIOTASK pHeadOld;
    pIoTask->pNext = pNext;
    while (!ASMAtomicCmpXchgExPtr(&pDisk->pIoTasksPendingHead, pIoTask, pNext, &pHeadOld))
    {
        pNext = pHeadOld;
        Assert(pNext != pIoTask);
        pIoTask->pNext = pNext;
        ASMNopPause();
    }

    if (ASMAtomicCmpXchgBool(&pDisk->fLocked, true, false))
    {
        /* Release disk lock, it will take care of processing all lists. */
        vdDiskUnlock(pDisk, NULL);
    }
}

static DECLCALLBACK(int) vdIOIntReqCompleted(void *pvUser, int rcReq)
{
    PVDIOTASK pIoTask = (PVDIOTASK)pvUser;

    LogFlowFunc(("Task completed pIoTask=%#p\n", pIoTask));

    pIoTask->rcReq = rcReq;
    vdXferTryLockDiskDeferIoTask(pIoTask);
    return VINF_SUCCESS;
}

/**
 * VD I/O interface callback for opening a file.
 */
static DECLCALLBACK(int) vdIOIntOpen(void *pvUser, const char *pszLocation,
                                     unsigned uOpenFlags, PPVDIOSTORAGE ppIoStorage)
{
    int rc = VINF_SUCCESS;
    PVDIO pVDIo             = (PVDIO)pvUser;
    PVDIOSTORAGE pIoStorage = (PVDIOSTORAGE)RTMemAllocZ(sizeof(VDIOSTORAGE));

    if (!pIoStorage)
        return VERR_NO_MEMORY;

    /* Create the AVl tree. */
    pIoStorage->pTreeMetaXfers = (PAVLRFOFFTREE)RTMemAllocZ(sizeof(AVLRFOFFTREE));
    if (pIoStorage->pTreeMetaXfers)
    {
        rc = pVDIo->pInterfaceIo->pfnOpen(pVDIo->pInterfaceIo->Core.pvUser,
                                          pszLocation, uOpenFlags,
                                          vdIOIntReqCompleted,
                                          &pIoStorage->pStorage);
        if (RT_SUCCESS(rc))
        {
            pIoStorage->pVDIo = pVDIo;
            *ppIoStorage = pIoStorage;
            return VINF_SUCCESS;
        }

        RTMemFree(pIoStorage->pTreeMetaXfers);
    }
    else
        rc = VERR_NO_MEMORY;

    RTMemFree(pIoStorage);
    return rc;
}

static DECLCALLBACK(int) vdIOIntTreeMetaXferDestroy(PAVLRFOFFNODECORE pNode, void *pvUser)
{
    RT_NOREF2(pNode, pvUser);
    AssertMsgFailed(("Tree should be empty at this point!\n"));
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vdIOIntClose(void *pvUser, PVDIOSTORAGE pIoStorage)
{
    int rc = VINF_SUCCESS;
    PVDIO pVDIo = (PVDIO)pvUser;

    /* We free everything here, even if closing the file failed for some reason. */
    rc = pVDIo->pInterfaceIo->pfnClose(pVDIo->pInterfaceIo->Core.pvUser, pIoStorage->pStorage);
    RTAvlrFileOffsetDestroy(pIoStorage->pTreeMetaXfers, vdIOIntTreeMetaXferDestroy, NULL);
    RTMemFree(pIoStorage->pTreeMetaXfers);
    RTMemFree(pIoStorage);
    return rc;
}

static DECLCALLBACK(int) vdIOIntDelete(void *pvUser, const char *pcszFilename)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnDelete(pVDIo->pInterfaceIo->Core.pvUser,
                                          pcszFilename);
}

static DECLCALLBACK(int) vdIOIntMove(void *pvUser, const char *pcszSrc, const char *pcszDst,
                                     unsigned fMove)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnMove(pVDIo->pInterfaceIo->Core.pvUser,
                                        pcszSrc, pcszDst, fMove);
}

static DECLCALLBACK(int) vdIOIntGetFreeSpace(void *pvUser, const char *pcszFilename,
                                             int64_t *pcbFreeSpace)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnGetFreeSpace(pVDIo->pInterfaceIo->Core.pvUser,
                                                pcszFilename, pcbFreeSpace);
}

static DECLCALLBACK(int) vdIOIntGetModificationTime(void *pvUser, const char *pcszFilename,
                                                    PRTTIMESPEC pModificationTime)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnGetModificationTime(pVDIo->pInterfaceIo->Core.pvUser,
                                                       pcszFilename, pModificationTime);
}

static DECLCALLBACK(int) vdIOIntGetSize(void *pvUser, PVDIOSTORAGE pIoStorage,
                                        uint64_t *pcbSize)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnGetSize(pVDIo->pInterfaceIo->Core.pvUser,
                                           pIoStorage->pStorage, pcbSize);
}

static DECLCALLBACK(int) vdIOIntSetSize(void *pvUser, PVDIOSTORAGE pIoStorage,
                                        uint64_t cbSize)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    return pVDIo->pInterfaceIo->pfnSetSize(pVDIo->pInterfaceIo->Core.pvUser,
                                           pIoStorage->pStorage, cbSize);
}

static DECLCALLBACK(int) vdIOIntSetAllocationSize(void *pvUser, PVDIOSTORAGE pIoStorage,
                                                  uint64_t cbSize, uint32_t fFlags,
                                                  PVDINTERFACEPROGRESS pIfProgress,
                                                  unsigned uPercentStart, unsigned uPercentSpan)
{
    PVDIO pVDIo = (PVDIO)pvUser;
    int rc = pVDIo->pInterfaceIo->pfnSetAllocationSize(pVDIo->pInterfaceIo->Core.pvUser,
                                                       pIoStorage->pStorage, cbSize, fFlags);
    if (rc == VERR_NOT_SUPPORTED)
    {
        /* Fallback if the underlying medium does not support optimized storage allocation. */
        uint64_t cbSizeCur = 0;
        rc = pVDIo->pInterfaceIo->pfnGetSize(pVDIo->pInterfaceIo->Core.pvUser,
                                             pIoStorage->pStorage, &cbSizeCur);
        if (RT_SUCCESS(rc))
        {
            if (cbSizeCur < cbSize)
            {
                const size_t cbBuf = 128 * _1K;
                void *pvBuf = RTMemTmpAllocZ(cbBuf);
                if (RT_LIKELY(pvBuf))
                {
                    uint64_t cbFill = cbSize - cbSizeCur;
                    uint64_t uOff = 0;

                    /* Write data to all blocks. */
                    while (   uOff < cbFill
                           && RT_SUCCESS(rc))
                    {
                        size_t cbChunk = (size_t)RT_MIN(cbFill - uOff, cbBuf);

                        rc = pVDIo->pInterfaceIo->pfnWriteSync(pVDIo->pInterfaceIo->Core.pvUser,
                                                               pIoStorage->pStorage, cbSizeCur + uOff,
                                                               pvBuf, cbChunk, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            uOff += cbChunk;

                            rc = vdIfProgress(pIfProgress, uPercentStart + uOff * uPercentSpan / cbFill);
                        }
                    }

                    RTMemTmpFree(pvBuf);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else if (cbSizeCur > cbSize)
                rc = pVDIo->pInterfaceIo->pfnSetSize(pVDIo->pInterfaceIo->Core.pvUser,
                                                     pIoStorage->pStorage, cbSize);
        }
    }

    if (RT_SUCCESS(rc))
        rc = vdIfProgress(pIfProgress, uPercentStart + uPercentSpan);

    return rc;
}

static DECLCALLBACK(int) vdIOIntReadUser(void *pvUser, PVDIOSTORAGE pIoStorage, uint64_t uOffset,
                                         PVDIOCTX pIoCtx, size_t cbRead)
{
    int rc = VINF_SUCCESS;
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;

    LogFlowFunc(("pvUser=%#p pIoStorage=%#p uOffset=%llu pIoCtx=%#p cbRead=%u\n",
                 pvUser, pIoStorage, uOffset, pIoCtx, cbRead));

    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    Assert(cbRead > 0);

    if (   (pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC)
        || !pVDIo->pInterfaceIo->pfnReadAsync)
    {
        RTSGSEG Seg;
        unsigned cSegments = 1;
        size_t cbTaskRead = 0;

        /* Synchronous I/O contexts only have one buffer segment. */
        AssertMsgReturn(pIoCtx->Req.Io.SgBuf.cSegs == 1,
                        ("Invalid number of buffer segments for synchronous I/O context"),
                        VERR_INVALID_PARAMETER);

        cbTaskRead = RTSgBufSegArrayCreate(&pIoCtx->Req.Io.SgBuf, &Seg, &cSegments, cbRead);
        Assert(cbRead == cbTaskRead);
        Assert(cSegments == 1);
        rc = pVDIo->pInterfaceIo->pfnReadSync(pVDIo->pInterfaceIo->Core.pvUser,
                                              pIoStorage->pStorage, uOffset,
                                              Seg.pvSeg, cbRead, NULL);
        if (RT_SUCCESS(rc))
        {
            Assert(cbRead == (uint32_t)cbRead);
            ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbRead);
        }
    }
    else
    {
        /* Build the S/G array and spawn a new I/O task */
        while (cbRead)
        {
            RTSGSEG  aSeg[VD_IO_TASK_SEGMENTS_MAX];
            unsigned cSegments  = VD_IO_TASK_SEGMENTS_MAX;
            size_t   cbTaskRead = RTSgBufSegArrayCreate(&pIoCtx->Req.Io.SgBuf, aSeg, &cSegments, cbRead);

            Assert(cSegments > 0);
            Assert(cbTaskRead > 0);
            AssertMsg(cbTaskRead <= cbRead, ("Invalid number of bytes to read\n"));

            LogFlow(("Reading %u bytes into %u segments\n", cbTaskRead, cSegments));

#ifdef RT_STRICT
            for (unsigned i = 0; i < cSegments; i++)
                    AssertMsg(aSeg[i].pvSeg && !(aSeg[i].cbSeg % 512),
                              ("Segment %u is invalid\n", i));
#endif

            Assert(cbTaskRead == (uint32_t)cbTaskRead);
            PVDIOTASK pIoTask = vdIoTaskUserAlloc(pIoStorage, NULL, NULL, pIoCtx, (uint32_t)cbTaskRead);

            if (!pIoTask)
                return VERR_NO_MEMORY;

            ASMAtomicIncU32(&pIoCtx->cDataTransfersPending);

            void *pvTask;
            Log(("Spawning pIoTask=%p pIoCtx=%p\n", pIoTask, pIoCtx));
            rc = pVDIo->pInterfaceIo->pfnReadAsync(pVDIo->pInterfaceIo->Core.pvUser,
                                                   pIoStorage->pStorage, uOffset,
                                                   aSeg, cSegments, cbTaskRead, pIoTask,
                                                   &pvTask);
            if (RT_SUCCESS(rc))
            {
                AssertMsg(cbTaskRead <= pIoCtx->Req.Io.cbTransferLeft, ("Impossible!\n"));
                ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbTaskRead);
                ASMAtomicDecU32(&pIoCtx->cDataTransfersPending);
                vdIoTaskFree(pDisk, pIoTask);
            }
            else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
            {
                ASMAtomicDecU32(&pIoCtx->cDataTransfersPending);
                vdIoTaskFree(pDisk, pIoTask);
                break;
            }

            uOffset += cbTaskRead;
            cbRead  -= cbTaskRead;
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdIOIntWriteUser(void *pvUser, PVDIOSTORAGE pIoStorage, uint64_t uOffset,
                                          PVDIOCTX pIoCtx, size_t cbWrite, PFNVDXFERCOMPLETED pfnComplete,
                                          void *pvCompleteUser)
{
    int rc = VINF_SUCCESS;
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;

    LogFlowFunc(("pvUser=%#p pIoStorage=%#p uOffset=%llu pIoCtx=%#p cbWrite=%u\n",
                 pvUser, pIoStorage, uOffset, pIoCtx, cbWrite));

    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    Assert(cbWrite > 0);

    if (   (pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC)
        || !pVDIo->pInterfaceIo->pfnWriteAsync)
    {
        RTSGSEG Seg;
        unsigned cSegments = 1;
        size_t cbTaskWrite = 0;

        /* Synchronous I/O contexts only have one buffer segment. */
        AssertMsgReturn(pIoCtx->Req.Io.SgBuf.cSegs == 1,
                        ("Invalid number of buffer segments for synchronous I/O context"),
                        VERR_INVALID_PARAMETER);

        cbTaskWrite = RTSgBufSegArrayCreate(&pIoCtx->Req.Io.SgBuf, &Seg, &cSegments, cbWrite);
        Assert(cbWrite == cbTaskWrite);
        Assert(cSegments == 1);
        rc = pVDIo->pInterfaceIo->pfnWriteSync(pVDIo->pInterfaceIo->Core.pvUser,
                                              pIoStorage->pStorage, uOffset,
                                              Seg.pvSeg, cbWrite, NULL);
        if (RT_SUCCESS(rc))
        {
            Assert(pIoCtx->Req.Io.cbTransferLeft >= cbWrite);
            ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbWrite);
        }
    }
    else
    {
        /* Build the S/G array and spawn a new I/O task */
        while (cbWrite)
        {
            RTSGSEG  aSeg[VD_IO_TASK_SEGMENTS_MAX];
            unsigned cSegments   = VD_IO_TASK_SEGMENTS_MAX;
            size_t   cbTaskWrite = 0;

            cbTaskWrite = RTSgBufSegArrayCreate(&pIoCtx->Req.Io.SgBuf, aSeg, &cSegments, cbWrite);

            Assert(cSegments > 0);
            Assert(cbTaskWrite > 0);
            AssertMsg(cbTaskWrite <= cbWrite, ("Invalid number of bytes to write\n"));

            LogFlow(("Writing %u bytes from %u segments\n", cbTaskWrite, cSegments));

#ifdef DEBUG
            for (unsigned i = 0; i < cSegments; i++)
                    AssertMsg(aSeg[i].pvSeg && !(aSeg[i].cbSeg % 512),
                              ("Segment %u is invalid\n", i));
#endif

            Assert(cbTaskWrite == (uint32_t)cbTaskWrite);
            PVDIOTASK pIoTask = vdIoTaskUserAlloc(pIoStorage, pfnComplete, pvCompleteUser, pIoCtx, (uint32_t)cbTaskWrite);

            if (!pIoTask)
                return VERR_NO_MEMORY;

            ASMAtomicIncU32(&pIoCtx->cDataTransfersPending);

            void *pvTask;
            Log(("Spawning pIoTask=%p pIoCtx=%p\n", pIoTask, pIoCtx));
            rc = pVDIo->pInterfaceIo->pfnWriteAsync(pVDIo->pInterfaceIo->Core.pvUser,
                                                    pIoStorage->pStorage,
                                                    uOffset, aSeg, cSegments,
                                                    cbTaskWrite, pIoTask, &pvTask);
            if (RT_SUCCESS(rc))
            {
                AssertMsg(cbTaskWrite <= pIoCtx->Req.Io.cbTransferLeft, ("Impossible!\n"));
                ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbTaskWrite);
                ASMAtomicDecU32(&pIoCtx->cDataTransfersPending);
                vdIoTaskFree(pDisk, pIoTask);
            }
            else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
            {
                ASMAtomicDecU32(&pIoCtx->cDataTransfersPending);
                vdIoTaskFree(pDisk, pIoTask);
                break;
            }

            uOffset += cbTaskWrite;
            cbWrite -= cbTaskWrite;
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdIOIntReadMeta(void *pvUser, PVDIOSTORAGE pIoStorage, uint64_t uOffset,
                                         void *pvBuf, size_t cbRead, PVDIOCTX pIoCtx,
                                         PPVDMETAXFER ppMetaXfer, PFNVDXFERCOMPLETED pfnComplete,
                                         void *pvCompleteUser)
{
    PVDIO pVDIo     = (PVDIO)pvUser;
    PVDISK pDisk  = pVDIo->pDisk;
    int rc = VINF_SUCCESS;
    RTSGSEG Seg;
    PVDIOTASK pIoTask;
    PVDMETAXFER pMetaXfer = NULL;
    void *pvTask = NULL;

    LogFlowFunc(("pvUser=%#p pIoStorage=%#p uOffset=%llu pvBuf=%#p cbRead=%u\n",
                 pvUser, pIoStorage, uOffset, pvBuf, cbRead));

    AssertMsgReturn(   pIoCtx
                    || (!ppMetaXfer && !pfnComplete && !pvCompleteUser),
                    ("A synchronous metadata read is requested but the parameters are wrong\n"),
                    VERR_INVALID_POINTER);

    /** @todo Enable check for sync I/O later. */
    if (   pIoCtx
        && !(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    if (   !pIoCtx
        || pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC
        || !pVDIo->pInterfaceIo->pfnReadAsync)
    {
        /* Handle synchronous metadata I/O. */
        /** @todo Integrate with metadata transfers below. */
        rc = pVDIo->pInterfaceIo->pfnReadSync(pVDIo->pInterfaceIo->Core.pvUser,
                                               pIoStorage->pStorage, uOffset,
                                               pvBuf, cbRead, NULL);
        if (ppMetaXfer)
            *ppMetaXfer = NULL;
    }
    else
    {
        pMetaXfer = (PVDMETAXFER)RTAvlrFileOffsetGet(pIoStorage->pTreeMetaXfers, uOffset);
        if (!pMetaXfer)
        {
#ifdef RT_STRICT
            pMetaXfer = (PVDMETAXFER)RTAvlrFileOffsetGetBestFit(pIoStorage->pTreeMetaXfers, uOffset, false /* fAbove */);
            AssertMsg(!pMetaXfer || (pMetaXfer->Core.Key + (RTFOFF)pMetaXfer->cbMeta <= (RTFOFF)uOffset),
                      ("Overlapping meta transfers!\n"));
#endif

            /* Allocate a new meta transfer. */
            pMetaXfer = vdMetaXferAlloc(pIoStorage, uOffset, cbRead);
            if (!pMetaXfer)
                return VERR_NO_MEMORY;

            pIoTask = vdIoTaskMetaAlloc(pIoStorage, pfnComplete, pvCompleteUser, pMetaXfer);
            if (!pIoTask)
            {
                RTMemFree(pMetaXfer);
                return VERR_NO_MEMORY;
            }

            Seg.cbSeg = cbRead;
            Seg.pvSeg = pMetaXfer->abData;

            VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_READ);
            rc = pVDIo->pInterfaceIo->pfnReadAsync(pVDIo->pInterfaceIo->Core.pvUser,
                                                   pIoStorage->pStorage,
                                                   uOffset, &Seg, 1,
                                                   cbRead, pIoTask, &pvTask);

            if (RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            {
                bool fInserted = RTAvlrFileOffsetInsert(pIoStorage->pTreeMetaXfers, &pMetaXfer->Core);
                Assert(fInserted); NOREF(fInserted);
            }
            else
                RTMemFree(pMetaXfer);

            if (RT_SUCCESS(rc))
            {
                VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_NONE);
                vdIoTaskFree(pDisk, pIoTask);
            }
            else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS && !pfnComplete)
                rc = VERR_VD_NOT_ENOUGH_METADATA;
        }

        Assert(RT_VALID_PTR(pMetaXfer) || RT_FAILURE(rc));

        if (RT_SUCCESS(rc) || rc == VERR_VD_NOT_ENOUGH_METADATA || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        {
            /* If it is pending add the request to the list. */
            if (VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_READ)
            {
                PVDIOCTXDEFERRED pDeferred = (PVDIOCTXDEFERRED)RTMemAllocZ(sizeof(VDIOCTXDEFERRED));
                AssertPtr(pDeferred);

                RTListInit(&pDeferred->NodeDeferred);
                pDeferred->pIoCtx = pIoCtx;

                ASMAtomicIncU32(&pIoCtx->cMetaTransfersPending);
                RTListAppend(&pMetaXfer->ListIoCtxWaiting, &pDeferred->NodeDeferred);
                rc = VERR_VD_NOT_ENOUGH_METADATA;
            }
            else
            {
                /* Transfer the data. */
                pMetaXfer->cRefs++;
                Assert(pMetaXfer->cbMeta >= cbRead);
                Assert(pMetaXfer->Core.Key == (RTFOFF)uOffset);
                if (pMetaXfer->pbDataShw)
                    memcpy(pvBuf, pMetaXfer->pbDataShw, cbRead);
                else
                    memcpy(pvBuf, pMetaXfer->abData, cbRead);
                *ppMetaXfer = pMetaXfer;
            }
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdIOIntWriteMeta(void *pvUser, PVDIOSTORAGE pIoStorage, uint64_t uOffset,
                                          const void *pvBuf, size_t cbWrite, PVDIOCTX pIoCtx,
                                          PFNVDXFERCOMPLETED pfnComplete, void *pvCompleteUser)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    int rc = VINF_SUCCESS;
    RTSGSEG Seg;
    PVDIOTASK pIoTask;
    PVDMETAXFER pMetaXfer = NULL;
    bool fInTree = false;
    void *pvTask = NULL;

    LogFlowFunc(("pvUser=%#p pIoStorage=%#p uOffset=%llu pvBuf=%#p cbWrite=%u\n",
                 pvUser, pIoStorage, uOffset, pvBuf, cbWrite));

    AssertMsgReturn(   pIoCtx
                    || (!pfnComplete && !pvCompleteUser),
                    ("A synchronous metadata write is requested but the parameters are wrong\n"),
                    VERR_INVALID_POINTER);

    /** @todo Enable check for sync I/O later. */
    if (   pIoCtx
        && !(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    if (   !pIoCtx
        || pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC
        || !pVDIo->pInterfaceIo->pfnWriteAsync)
    {
        /* Handle synchronous metadata I/O. */
        /** @todo Integrate with metadata transfers below. */
        rc = pVDIo->pInterfaceIo->pfnWriteSync(pVDIo->pInterfaceIo->Core.pvUser,
                                               pIoStorage->pStorage, uOffset,
                                               pvBuf, cbWrite, NULL);
    }
    else
    {
        pMetaXfer = (PVDMETAXFER)RTAvlrFileOffsetGet(pIoStorage->pTreeMetaXfers, uOffset);
        if (!pMetaXfer)
        {
            /* Allocate a new meta transfer. */
            pMetaXfer = vdMetaXferAlloc(pIoStorage, uOffset, cbWrite);
            if (!pMetaXfer)
                return VERR_NO_MEMORY;
        }
        else
        {
            Assert(pMetaXfer->cbMeta >= cbWrite);
            Assert(pMetaXfer->Core.Key == (RTFOFF)uOffset);
            fInTree = true;
        }

        if (VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_NONE)
        {
            pIoTask = vdIoTaskMetaAlloc(pIoStorage, pfnComplete, pvCompleteUser, pMetaXfer);
            if (!pIoTask)
            {
                RTMemFree(pMetaXfer);
                return VERR_NO_MEMORY;
            }

            memcpy(pMetaXfer->abData, pvBuf, cbWrite);
            Seg.cbSeg = cbWrite;
            Seg.pvSeg = pMetaXfer->abData;

            ASMAtomicIncU32(&pIoCtx->cMetaTransfersPending);

            VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_WRITE);
            rc = pVDIo->pInterfaceIo->pfnWriteAsync(pVDIo->pInterfaceIo->Core.pvUser,
                                                    pIoStorage->pStorage,
                                                    uOffset, &Seg, 1, cbWrite, pIoTask,
                                                    &pvTask);
            if (RT_SUCCESS(rc))
            {
                VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_NONE);
                ASMAtomicDecU32(&pIoCtx->cMetaTransfersPending);
                vdIoTaskFree(pDisk, pIoTask);
                if (fInTree && !pMetaXfer->cRefs)
                {
                    LogFlow(("Removing meta xfer=%#p\n", pMetaXfer));
                    bool fRemoved = RTAvlrFileOffsetRemove(pIoStorage->pTreeMetaXfers, pMetaXfer->Core.Key) != NULL;
                    AssertMsg(fRemoved, ("Metadata transfer wasn't removed\n")); NOREF(fRemoved);
                    RTMemFree(pMetaXfer);
                    pMetaXfer = NULL;
                }
            }
            else if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            {
                PVDIOCTXDEFERRED pDeferred = (PVDIOCTXDEFERRED)RTMemAllocZ(sizeof(VDIOCTXDEFERRED));
                AssertPtr(pDeferred);

                RTListInit(&pDeferred->NodeDeferred);
                pDeferred->pIoCtx = pIoCtx;

                if (!fInTree)
                {
                    bool fInserted = RTAvlrFileOffsetInsert(pIoStorage->pTreeMetaXfers, &pMetaXfer->Core);
                    Assert(fInserted); NOREF(fInserted);
                }

                RTListAppend(&pMetaXfer->ListIoCtxWaiting, &pDeferred->NodeDeferred);
            }
            else
            {
                RTMemFree(pMetaXfer);
                pMetaXfer = NULL;
            }
        }
        else
        {
            /* I/O is in progress, update shadow buffer and add to waiting list. */
            Assert(VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_WRITE);
            if (!pMetaXfer->pbDataShw)
            {
                /* Allocate shadow buffer and set initial state. */
                LogFlowFunc(("pMetaXfer=%#p Creating shadow buffer\n", pMetaXfer));
                pMetaXfer->pbDataShw = (uint8_t *)RTMemAlloc(pMetaXfer->cbMeta);
                if (RT_LIKELY(pMetaXfer->pbDataShw))
                    memcpy(pMetaXfer->pbDataShw, pMetaXfer->abData, pMetaXfer->cbMeta);
                else
                    rc = VERR_NO_MEMORY;
            }

            if (RT_SUCCESS(rc))
            {
                /* Update with written data and append to waiting list. */
                PVDIOCTXDEFERRED pDeferred = (PVDIOCTXDEFERRED)RTMemAllocZ(sizeof(VDIOCTXDEFERRED));
                if (pDeferred)
                {
                    LogFlowFunc(("pMetaXfer=%#p Updating shadow buffer\n", pMetaXfer));

                    RTListInit(&pDeferred->NodeDeferred);
                    pDeferred->pIoCtx = pIoCtx;
                    ASMAtomicIncU32(&pIoCtx->cMetaTransfersPending);
                    memcpy(pMetaXfer->pbDataShw, pvBuf, cbWrite);
                    RTListAppend(&pMetaXfer->ListIoCtxShwWrites, &pDeferred->NodeDeferred);
                }
                else
                {
                    /*
                     * Free shadow buffer if there is no one depending on it, i.e.
                     * we just allocated it.
                     */
                    if (RTListIsEmpty(&pMetaXfer->ListIoCtxShwWrites))
                    {
                        RTMemFree(pMetaXfer->pbDataShw);
                        pMetaXfer->pbDataShw = NULL;
                    }
                    rc = VERR_NO_MEMORY;
                }
            }
        }
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(void) vdIOIntMetaXferRelease(void *pvUser, PVDMETAXFER pMetaXfer)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    PVDIOSTORAGE pIoStorage;

    /*
     * It is possible that we get called with a NULL metadata xfer handle
     * for synchronous I/O. Just exit.
     */
    if (!pMetaXfer)
        return;

    pIoStorage = pMetaXfer->pIoStorage;

    VD_IS_LOCKED(pDisk);

    Assert(   VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_NONE
           || VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_WRITE);
    Assert(pMetaXfer->cRefs > 0);

    pMetaXfer->cRefs--;
    if (   !pMetaXfer->cRefs
        && RTListIsEmpty(&pMetaXfer->ListIoCtxWaiting)
        && VDMETAXFER_TXDIR_GET(pMetaXfer->fFlags) == VDMETAXFER_TXDIR_NONE)
    {
        /* Free the meta data entry. */
        LogFlow(("Removing meta xfer=%#p\n", pMetaXfer));
        bool fRemoved = RTAvlrFileOffsetRemove(pIoStorage->pTreeMetaXfers, pMetaXfer->Core.Key) != NULL;
        AssertMsg(fRemoved, ("Metadata transfer wasn't removed\n")); NOREF(fRemoved);

        RTMemFree(pMetaXfer);
    }
}

static DECLCALLBACK(int) vdIOIntFlush(void *pvUser, PVDIOSTORAGE pIoStorage, PVDIOCTX pIoCtx,
                                      PFNVDXFERCOMPLETED pfnComplete, void *pvCompleteUser)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    int rc = VINF_SUCCESS;
    PVDIOTASK pIoTask;
    PVDMETAXFER pMetaXfer = NULL;
    void *pvTask = NULL;

    LogFlowFunc(("pvUser=%#p pIoStorage=%#p pIoCtx=%#p\n",
                 pvUser, pIoStorage, pIoCtx));

    AssertMsgReturn(   pIoCtx
                    || (!pfnComplete && !pvCompleteUser),
                    ("A synchronous metadata write is requested but the parameters are wrong\n"),
                    VERR_INVALID_POINTER);

    /** @todo Enable check for sync I/O later. */
    if (   pIoCtx
        && !(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    if (pVDIo->fIgnoreFlush)
        return VINF_SUCCESS;

    if (   !pIoCtx
        || pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC
        || !pVDIo->pInterfaceIo->pfnFlushAsync)
    {
        /* Handle synchronous flushes. */
        /** @todo Integrate with metadata transfers below. */
        rc = pVDIo->pInterfaceIo->pfnFlushSync(pVDIo->pInterfaceIo->Core.pvUser,
                                               pIoStorage->pStorage);
    }
    else
    {
        /* Allocate a new meta transfer. */
        pMetaXfer = vdMetaXferAlloc(pIoStorage, 0, 0);
        if (!pMetaXfer)
            return VERR_NO_MEMORY;

        pIoTask = vdIoTaskMetaAlloc(pIoStorage, pfnComplete, pvUser, pMetaXfer);
        if (!pIoTask)
        {
            RTMemFree(pMetaXfer);
            return VERR_NO_MEMORY;
        }

        ASMAtomicIncU32(&pIoCtx->cMetaTransfersPending);

        PVDIOCTXDEFERRED pDeferred = (PVDIOCTXDEFERRED)RTMemAllocZ(sizeof(VDIOCTXDEFERRED));
        AssertPtr(pDeferred);

        RTListInit(&pDeferred->NodeDeferred);
        pDeferred->pIoCtx = pIoCtx;

        RTListAppend(&pMetaXfer->ListIoCtxWaiting, &pDeferred->NodeDeferred);
        VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_FLUSH);
        rc = pVDIo->pInterfaceIo->pfnFlushAsync(pVDIo->pInterfaceIo->Core.pvUser,
                                                pIoStorage->pStorage,
                                                pIoTask, &pvTask);
        if (RT_SUCCESS(rc))
        {
            VDMETAXFER_TXDIR_SET(pMetaXfer->fFlags, VDMETAXFER_TXDIR_NONE);
            ASMAtomicDecU32(&pIoCtx->cMetaTransfersPending);
            vdIoTaskFree(pDisk, pIoTask);
            RTMemFree(pDeferred);
            RTMemFree(pMetaXfer);
        }
        else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
            RTMemFree(pMetaXfer);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(size_t) vdIOIntIoCtxCopyTo(void *pvUser, PVDIOCTX pIoCtx,
                                               const void *pvBuf, size_t cbBuf)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    size_t cbCopied = 0;

    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    cbCopied = vdIoCtxCopyTo(pIoCtx, (uint8_t *)pvBuf, cbBuf);
    Assert(cbCopied == cbBuf);

    /// @todo Assert(pIoCtx->Req.Io.cbTransferLeft >= cbCopied); - triggers with vdCopyHelper/dmgRead.
    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbCopied);

    return cbCopied;
}

static DECLCALLBACK(size_t) vdIOIntIoCtxCopyFrom(void *pvUser, PVDIOCTX pIoCtx,
                                                 void *pvBuf, size_t cbBuf)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    size_t cbCopied = 0;

    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    cbCopied = vdIoCtxCopyFrom(pIoCtx, (uint8_t *)pvBuf, cbBuf);
    Assert(cbCopied == cbBuf);

    /// @todo Assert(pIoCtx->Req.Io.cbTransferLeft > cbCopied); - triggers with vdCopyHelper/dmgRead.
    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbCopied);

    return cbCopied;
}

static DECLCALLBACK(size_t) vdIOIntIoCtxSet(void *pvUser, PVDIOCTX pIoCtx, int ch, size_t cb)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    size_t cbSet = 0;

    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);

    cbSet = vdIoCtxSet(pIoCtx, ch, cb);
    Assert(cbSet == cb);

    /// @todo Assert(pIoCtx->Req.Io.cbTransferLeft >= cbSet); - triggers with vdCopyHelper/dmgRead.
    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbSet);

    return cbSet;
}

static DECLCALLBACK(size_t) vdIOIntIoCtxSegArrayCreate(void *pvUser, PVDIOCTX pIoCtx,
                                                       PRTSGSEG paSeg, unsigned *pcSeg,
                                                       size_t cbData)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    size_t cbCreated = 0;

    /** @todo It is possible that this gets called from a filter plugin
     * outside of the disk lock. Refine assertion or remove completely. */
#if 0
    /** @todo Enable check for sync I/O later. */
    if (!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC))
        VD_IS_LOCKED(pDisk);
#else
    NOREF(pDisk);
#endif

    cbCreated = RTSgBufSegArrayCreate(&pIoCtx->Req.Io.SgBuf, paSeg, pcSeg, cbData);
    Assert(!paSeg || cbData == cbCreated);

    return cbCreated;
}

static DECLCALLBACK(void) vdIOIntIoCtxCompleted(void *pvUser, PVDIOCTX pIoCtx, int rcReq,
                                                size_t cbCompleted)
{
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;

    LogFlowFunc(("pvUser=%#p pIoCtx=%#p rcReq=%Rrc cbCompleted=%zu\n",
                 pvUser, pIoCtx, rcReq, cbCompleted));

    /*
     * Grab the disk critical section to avoid races with other threads which
     * might still modify the I/O context.
     * Example is that iSCSI is doing an asynchronous write but calls us already
     * while the other thread is still hanging in vdWriteHelperAsync and couldn't update
     * the blocked state yet.
     * It can overwrite the state to true before we call vdIoCtxContinue and the
     * the request would hang indefinite.
     */
    ASMAtomicCmpXchgS32(&pIoCtx->rcReq, rcReq, VINF_SUCCESS);
    Assert(pIoCtx->Req.Io.cbTransferLeft >= cbCompleted);
    ASMAtomicSubU32(&pIoCtx->Req.Io.cbTransferLeft, (uint32_t)cbCompleted);

    /* Set next transfer function if the current one finished.
     * @todo: Find a better way to prevent vdIoCtxContinue from calling the current helper again. */
    if (!pIoCtx->Req.Io.cbTransferLeft)
    {
        pIoCtx->pfnIoCtxTransfer = pIoCtx->pfnIoCtxTransferNext;
        pIoCtx->pfnIoCtxTransferNext = NULL;
    }

    vdIoCtxAddToWaitingList(&pDisk->pIoCtxHaltedHead, pIoCtx);
    if (ASMAtomicCmpXchgBool(&pDisk->fLocked, true, false))
    {
        /* Immediately drop the lock again, it will take care of processing the list. */
        vdDiskUnlock(pDisk, NULL);
    }
}

static DECLCALLBACK(bool) vdIOIntIoCtxIsSynchronous(void *pvUser, PVDIOCTX pIoCtx)
{
    NOREF(pvUser);
    return !!(pIoCtx->fFlags & VDIOCTX_FLAGS_SYNC);
}

static DECLCALLBACK(bool) vdIOIntIoCtxIsZero(void *pvUser, PVDIOCTX pIoCtx, size_t cbCheck,
                                             bool fAdvance)
{
    NOREF(pvUser);

    bool fIsZero = RTSgBufIsZero(&pIoCtx->Req.Io.SgBuf, cbCheck);
    if (fIsZero && fAdvance)
        RTSgBufAdvance(&pIoCtx->Req.Io.SgBuf, cbCheck);

    return fIsZero;
}

static DECLCALLBACK(size_t) vdIOIntIoCtxGetDataUnitSize(void *pvUser, PVDIOCTX pIoCtx)
{
    RT_NOREF1(pIoCtx);
    PVDIO    pVDIo = (PVDIO)pvUser;
    PVDISK pDisk = pVDIo->pDisk;
    size_t cbSector = 0;

    PVDIMAGE pImage = vdGetImageByNumber(pDisk, VD_LAST_IMAGE);
    AssertPtrReturn(pImage, 0);

    PCVDREGIONLIST pRegionList = NULL;
    int rc = pImage->Backend->pfnQueryRegions(pImage->pBackendData, &pRegionList);
    if (RT_SUCCESS(rc))
    {
        cbSector = pRegionList->aRegions[0].cbBlock;

        AssertPtr(pImage->Backend->pfnRegionListRelease);
        pImage->Backend->pfnRegionListRelease(pImage->pBackendData, pRegionList);
    }

    return cbSector;
}

/**
 * VD I/O interface callback for opening a file (limited version for VDGetFormat).
 */
static DECLCALLBACK(int) vdIOIntOpenLimited(void *pvUser, const char *pszLocation,
                                            uint32_t fOpen, PPVDIOSTORAGE ppIoStorage)
{
    int rc = VINF_SUCCESS;
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    PVDIOSTORAGE pIoStorage = (PVDIOSTORAGE)RTMemAllocZ(sizeof(VDIOSTORAGE));

    if (!pIoStorage)
        return VERR_NO_MEMORY;

    rc = pInterfaceIo->pfnOpen(NULL, pszLocation, fOpen, NULL, &pIoStorage->pStorage);
    if (RT_SUCCESS(rc))
        *ppIoStorage = pIoStorage;
    else
        RTMemFree(pIoStorage);

    return rc;
}

static DECLCALLBACK(int) vdIOIntCloseLimited(void *pvUser, PVDIOSTORAGE pIoStorage)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    int rc = pInterfaceIo->pfnClose(NULL, pIoStorage->pStorage);

    RTMemFree(pIoStorage);
    return rc;
}

static DECLCALLBACK(int) vdIOIntDeleteLimited(void *pvUser, const char *pcszFilename)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnDelete(NULL, pcszFilename);
}

static DECLCALLBACK(int) vdIOIntMoveLimited(void *pvUser, const char *pcszSrc,
                                            const char *pcszDst, unsigned fMove)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnMove(NULL, pcszSrc, pcszDst, fMove);
}

static DECLCALLBACK(int) vdIOIntGetFreeSpaceLimited(void *pvUser, const char *pcszFilename,
                                                    int64_t *pcbFreeSpace)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnGetFreeSpace(NULL, pcszFilename, pcbFreeSpace);
}

static DECLCALLBACK(int) vdIOIntGetModificationTimeLimited(void *pvUser,
                                                           const char *pcszFilename,
                                                           PRTTIMESPEC pModificationTime)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnGetModificationTime(NULL, pcszFilename, pModificationTime);
}

static DECLCALLBACK(int) vdIOIntGetSizeLimited(void *pvUser, PVDIOSTORAGE pIoStorage,
                                               uint64_t *pcbSize)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnGetSize(NULL, pIoStorage->pStorage, pcbSize);
}

static DECLCALLBACK(int) vdIOIntSetSizeLimited(void *pvUser, PVDIOSTORAGE pIoStorage,
                                               uint64_t cbSize)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;
    return pInterfaceIo->pfnSetSize(NULL, pIoStorage->pStorage, cbSize);
}

static DECLCALLBACK(int) vdIOIntWriteUserLimited(void *pvUser, PVDIOSTORAGE pStorage,
                                                 uint64_t uOffset, PVDIOCTX pIoCtx,
                                                 size_t cbWrite,
                                                 PFNVDXFERCOMPLETED pfnComplete,
                                                 void *pvCompleteUser)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(pIoCtx);
    NOREF(cbWrite);
    NOREF(pfnComplete);
    NOREF(pvCompleteUser);
    AssertMsgFailedReturn(("This needs to be implemented when called\n"), VERR_NOT_IMPLEMENTED);
}

static DECLCALLBACK(int) vdIOIntReadUserLimited(void *pvUser, PVDIOSTORAGE pStorage,
                                                uint64_t uOffset, PVDIOCTX pIoCtx,
                                                size_t cbRead)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(pIoCtx);
    NOREF(cbRead);
    AssertMsgFailedReturn(("This needs to be implemented when called\n"), VERR_NOT_IMPLEMENTED);
}

static DECLCALLBACK(int) vdIOIntWriteMetaLimited(void *pvUser, PVDIOSTORAGE pStorage,
                                                 uint64_t uOffset, const void *pvBuffer,
                                                 size_t cbBuffer, PVDIOCTX pIoCtx,
                                                 PFNVDXFERCOMPLETED pfnComplete,
                                                 void *pvCompleteUser)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;

    AssertMsgReturn(!pIoCtx && !pfnComplete && !pvCompleteUser,
                    ("Async I/O not implemented for the limited interface"),
                    VERR_NOT_SUPPORTED);

    return pInterfaceIo->pfnWriteSync(NULL, pStorage->pStorage, uOffset, pvBuffer, cbBuffer, NULL);
}

static DECLCALLBACK(int) vdIOIntReadMetaLimited(void *pvUser, PVDIOSTORAGE pStorage,
                                                uint64_t uOffset, void *pvBuffer,
                                                size_t cbBuffer, PVDIOCTX pIoCtx,
                                                PPVDMETAXFER ppMetaXfer,
                                                PFNVDXFERCOMPLETED pfnComplete,
                                                void *pvCompleteUser)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;

    AssertMsgReturn(!pIoCtx && !ppMetaXfer && !pfnComplete && !pvCompleteUser,
                    ("Async I/O not implemented for the limited interface"),
                    VERR_NOT_SUPPORTED);

    return pInterfaceIo->pfnReadSync(NULL, pStorage->pStorage, uOffset, pvBuffer, cbBuffer, NULL);
}

#if 0 /* unsed */
static int vdIOIntMetaXferReleaseLimited(void *pvUser, PVDMETAXFER pMetaXfer)
{
    /* This is a NOP in this case. */
    NOREF(pvUser);
    NOREF(pMetaXfer);
    return VINF_SUCCESS;
}
#endif

static DECLCALLBACK(int) vdIOIntFlushLimited(void *pvUser, PVDIOSTORAGE pStorage,
                                             PVDIOCTX pIoCtx,
                                             PFNVDXFERCOMPLETED pfnComplete,
                                             void *pvCompleteUser)
{
    PVDINTERFACEIO pInterfaceIo = (PVDINTERFACEIO)pvUser;

    AssertMsgReturn(!pIoCtx && !pfnComplete && !pvCompleteUser,
                    ("Async I/O not implemented for the limited interface"),
                    VERR_NOT_SUPPORTED);

    return pInterfaceIo->pfnFlushSync(NULL, pStorage->pStorage);
}

/**
 * internal: send output to the log (unconditionally).
 */
static DECLCALLBACK(int) vdLogMessage(void *pvUser, const char *pszFormat, va_list args)
{
    NOREF(pvUser);
    RTLogPrintfV(pszFormat, args);
    return VINF_SUCCESS;
}

DECLINLINE(int) vdMessageWrapper(PVDISK pDisk, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = pDisk->pInterfaceError->pfnMessage(pDisk->pInterfaceError->Core.pvUser,
                                                pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * internal: adjust PCHS geometry
 */
static void vdFixupPCHSGeometry(PVDGEOMETRY pPCHS, uint64_t cbSize)
{
    /* Fix broken PCHS geometry. Can happen for two reasons: either the backend
     * mixes up PCHS and LCHS, or the application used to create the source
     * image has put garbage in it. Additionally, if the PCHS geometry covers
     * more than the image size, set it back to the default. */
    if (   pPCHS->cHeads > 16
        || pPCHS->cSectors > 63
        || pPCHS->cCylinders == 0
        || (uint64_t)pPCHS->cHeads * pPCHS->cSectors * pPCHS->cCylinders * 512 > cbSize)
    {
        Assert(!(RT_MIN(cbSize / 512 / 16 / 63, 16383) - (uint32_t)RT_MIN(cbSize / 512 / 16 / 63, 16383)));
        pPCHS->cCylinders = (uint32_t)RT_MIN(cbSize / 512 / 16 / 63, 16383);
        pPCHS->cHeads = 16;
        pPCHS->cSectors = 63;
    }
}

/**
 * internal: adjust LCHS geometry
 */
static void vdFixupLCHSGeometry(PVDGEOMETRY pLCHS, uint64_t cbSize)
{
    /* Fix broken LCHS geometry. Can happen for two reasons: either the backend
     * mixes up PCHS and LCHS, or the application used to create the source
     * image has put garbage in it. The fix in this case is to clear the LCHS
     * geometry to trigger autodetection when it is used next. If the geometry
     * already says "please autodetect" (cylinders=0) keep it. */
    if (   (   pLCHS->cHeads > 255
            || pLCHS->cHeads == 0
            || pLCHS->cSectors > 63
            || pLCHS->cSectors == 0)
        && pLCHS->cCylinders != 0)
    {
        pLCHS->cCylinders = 0;
        pLCHS->cHeads = 0;
        pLCHS->cSectors = 0;
    }
    /* Always recompute the number of cylinders stored in the LCHS
     * geometry if it isn't set to "autotedetect" at the moment.
     * This is very useful if the destination image size is
     * larger or smaller than the source image size. Do not modify
     * the number of heads and sectors. Windows guests hate it. */
    if (   pLCHS->cCylinders != 0
        && pLCHS->cHeads != 0 /* paranoia */
        && pLCHS->cSectors != 0 /* paranoia */)
    {
        Assert(!(RT_MIN(cbSize / 512 / pLCHS->cHeads / pLCHS->cSectors, 1024) - (uint32_t)RT_MIN(cbSize / 512 / pLCHS->cHeads / pLCHS->cSectors, 1024)));
        pLCHS->cCylinders = (uint32_t)RT_MIN(cbSize / 512 / pLCHS->cHeads / pLCHS->cSectors, 1024);
    }
}

/**
 * Sets the I/O callbacks of the given interface to the fallback methods
 *
 * @param   pIfIo    The I/O interface to setup.
 */
static void vdIfIoFallbackCallbacksSetup(PVDINTERFACEIO pIfIo)
{
    pIfIo->pfnOpen                = vdIOOpenFallback;
    pIfIo->pfnClose               = vdIOCloseFallback;
    pIfIo->pfnDelete              = vdIODeleteFallback;
    pIfIo->pfnMove                = vdIOMoveFallback;
    pIfIo->pfnGetFreeSpace        = vdIOGetFreeSpaceFallback;
    pIfIo->pfnGetModificationTime = vdIOGetModificationTimeFallback;
    pIfIo->pfnGetSize             = vdIOGetSizeFallback;
    pIfIo->pfnSetSize             = vdIOSetSizeFallback;
    pIfIo->pfnSetAllocationSize   = vdIOSetAllocationSizeFallback;
    pIfIo->pfnReadSync            = vdIOReadSyncFallback;
    pIfIo->pfnWriteSync           = vdIOWriteSyncFallback;
    pIfIo->pfnFlushSync           = vdIOFlushSyncFallback;
    pIfIo->pfnReadAsync           = NULL;
    pIfIo->pfnWriteAsync          = NULL;
    pIfIo->pfnFlushAsync          = NULL;
}

/**
 * Sets the internal I/O callbacks of the given interface.
 *
 * @param   pIfIoInt    The internal I/O interface to setup.
 */
static void vdIfIoIntCallbacksSetup(PVDINTERFACEIOINT pIfIoInt)
{
    pIfIoInt->pfnOpen                 = vdIOIntOpen;
    pIfIoInt->pfnClose                = vdIOIntClose;
    pIfIoInt->pfnDelete               = vdIOIntDelete;
    pIfIoInt->pfnMove                 = vdIOIntMove;
    pIfIoInt->pfnGetFreeSpace         = vdIOIntGetFreeSpace;
    pIfIoInt->pfnGetModificationTime  = vdIOIntGetModificationTime;
    pIfIoInt->pfnGetSize              = vdIOIntGetSize;
    pIfIoInt->pfnSetSize              = vdIOIntSetSize;
    pIfIoInt->pfnSetAllocationSize    = vdIOIntSetAllocationSize;
    pIfIoInt->pfnReadUser             = vdIOIntReadUser;
    pIfIoInt->pfnWriteUser            = vdIOIntWriteUser;
    pIfIoInt->pfnReadMeta             = vdIOIntReadMeta;
    pIfIoInt->pfnWriteMeta            = vdIOIntWriteMeta;
    pIfIoInt->pfnMetaXferRelease      = vdIOIntMetaXferRelease;
    pIfIoInt->pfnFlush                = vdIOIntFlush;
    pIfIoInt->pfnIoCtxCopyFrom        = vdIOIntIoCtxCopyFrom;
    pIfIoInt->pfnIoCtxCopyTo          = vdIOIntIoCtxCopyTo;
    pIfIoInt->pfnIoCtxSet             = vdIOIntIoCtxSet;
    pIfIoInt->pfnIoCtxSegArrayCreate  = vdIOIntIoCtxSegArrayCreate;
    pIfIoInt->pfnIoCtxCompleted       = vdIOIntIoCtxCompleted;
    pIfIoInt->pfnIoCtxIsSynchronous   = vdIOIntIoCtxIsSynchronous;
    pIfIoInt->pfnIoCtxIsZero          = vdIOIntIoCtxIsZero;
    pIfIoInt->pfnIoCtxGetDataUnitSize = vdIOIntIoCtxGetDataUnitSize;
}

/**
 * Internally used completion handler for synchronous I/O contexts.
 */
static DECLCALLBACK(void) vdIoCtxSyncComplete(void *pvUser1, void *pvUser2, int rcReq)
{
    RT_NOREF2(pvUser1, rcReq);
    RTSEMEVENT hEvent = (RTSEMEVENT)pvUser2;

    RTSemEventSignal(hEvent);
}


VBOXDDU_DECL(int) VDInit(void)
{
    int rc = vdPluginInit();
    LogRel(("VD: VDInit finished with %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDShutdown(void)
{
    return vdPluginTerm();
}


VBOXDDU_DECL(int) VDPluginLoadFromFilename(const char *pszFilename)
{
    if (!vdPluginIsInitialized())
    {
        int rc = VDInit();
        if (RT_FAILURE(rc))
            return rc;
    }

    return vdPluginLoadFromFilename(pszFilename);
}

/**
 * Load all plugins from a given path.
 *
 * @returns VBox statuse code.
 * @param   pszPath         The path to load plugins from.
 */
VBOXDDU_DECL(int) VDPluginLoadFromPath(const char *pszPath)
{
    if (!vdPluginIsInitialized())
    {
        int rc = VDInit();
        if (RT_FAILURE(rc))
            return rc;
    }

    return vdPluginLoadFromPath(pszPath);
}


VBOXDDU_DECL(int) VDPluginUnloadFromFilename(const char *pszFilename)
{
    if (!vdPluginIsInitialized())
    {
        int rc = VDInit();
        if (RT_FAILURE(rc))
            return rc;
    }

    return vdPluginUnloadFromFilename(pszFilename);
}


VBOXDDU_DECL(int) VDPluginUnloadFromPath(const char *pszPath)
{
    if (!vdPluginIsInitialized())
    {
        int rc = VDInit();
        if (RT_FAILURE(rc))
            return rc;
    }

    return vdPluginUnloadFromPath(pszPath);
}


VBOXDDU_DECL(int) VDBackendInfo(unsigned cEntriesAlloc, PVDBACKENDINFO pEntries,
                                unsigned *pcEntriesUsed)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("cEntriesAlloc=%u pEntries=%#p pcEntriesUsed=%#p\n", cEntriesAlloc, pEntries, pcEntriesUsed));
    /* Check arguments. */
    AssertMsgReturn(cEntriesAlloc, ("cEntriesAlloc=%u\n", cEntriesAlloc), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pEntries, VERR_INVALID_POINTER);
    AssertPtrReturn(pcEntriesUsed, VERR_INVALID_POINTER);
    if (!vdPluginIsInitialized())
        VDInit();

    uint32_t cBackends = vdGetImageBackendCount();
    if (cEntriesAlloc < cBackends)
    {
        *pcEntriesUsed = cBackends;
        return VERR_BUFFER_OVERFLOW;
    }

    for (unsigned i = 0; i < cBackends; i++)
    {
        PCVDIMAGEBACKEND pBackend;
        rc = vdQueryImageBackend(i, &pBackend);
        AssertRC(rc);

        pEntries[i].pszBackend         = pBackend->pszBackendName;
        pEntries[i].uBackendCaps       = pBackend->uBackendCaps;
        pEntries[i].paFileExtensions   = pBackend->paFileExtensions;
        pEntries[i].paConfigInfo       = pBackend->paConfigInfo;
        pEntries[i].pfnComposeLocation = pBackend->pfnComposeLocation;
        pEntries[i].pfnComposeName     = pBackend->pfnComposeName;
    }

    LogFlowFunc(("returns %Rrc *pcEntriesUsed=%u\n", rc, cBackends));
    *pcEntriesUsed = cBackends;
    return rc;
}


VBOXDDU_DECL(int) VDBackendInfoOne(const char *pszBackend, PVDBACKENDINFO pEntry)
{
    LogFlowFunc(("pszBackend=%#p pEntry=%#p\n", pszBackend, pEntry));
    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry, VERR_INVALID_POINTER);
    if (!vdPluginIsInitialized())
        VDInit();

    PCVDIMAGEBACKEND pBackend;
    int rc = vdFindImageBackend(pszBackend, &pBackend);
    if (RT_SUCCESS(rc))
    {
        pEntry->pszBackend       = pBackend->pszBackendName;
        pEntry->uBackendCaps     = pBackend->uBackendCaps;
        pEntry->paFileExtensions = pBackend->paFileExtensions;
        pEntry->paConfigInfo     = pBackend->paConfigInfo;
    }

    return rc;
}


VBOXDDU_DECL(int) VDFilterInfo(unsigned cEntriesAlloc, PVDFILTERINFO pEntries,
                               unsigned *pcEntriesUsed)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("cEntriesAlloc=%u pEntries=%#p pcEntriesUsed=%#p\n", cEntriesAlloc, pEntries, pcEntriesUsed));
    /* Check arguments. */
    AssertMsgReturn(cEntriesAlloc,
                    ("cEntriesAlloc=%u\n", cEntriesAlloc),
                    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pEntries, VERR_INVALID_POINTER);
    AssertPtrReturn(pcEntriesUsed, VERR_INVALID_POINTER);
    if (!vdPluginIsInitialized())
        VDInit();

    uint32_t cBackends = vdGetFilterBackendCount();
    if (cEntriesAlloc < cBackends)
    {
        *pcEntriesUsed = cBackends;
        return VERR_BUFFER_OVERFLOW;
    }

    for (unsigned i = 0; i < cBackends; i++)
    {
        PCVDFILTERBACKEND pBackend;
        rc = vdQueryFilterBackend(i, &pBackend);
        pEntries[i].pszFilter    = pBackend->pszBackendName;
        pEntries[i].paConfigInfo = pBackend->paConfigInfo;
    }

    LogFlowFunc(("returns %Rrc *pcEntriesUsed=%u\n", rc, cBackends));
    *pcEntriesUsed = cBackends;
    return rc;
}


VBOXDDU_DECL(int) VDFilterInfoOne(const char *pszFilter, PVDFILTERINFO pEntry)
{
    LogFlowFunc(("pszFilter=%#p pEntry=%#p\n", pszFilter, pEntry));
    /* Check arguments. */
    AssertPtrReturn(pszFilter, VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry, VERR_INVALID_POINTER);
    if (!vdPluginIsInitialized())
        VDInit();

    PCVDFILTERBACKEND pBackend;
    int rc = vdFindFilterBackend(pszFilter, &pBackend);
    if (RT_SUCCESS(rc))
    {
        pEntry->pszFilter    = pBackend->pszBackendName;
        pEntry->paConfigInfo = pBackend->paConfigInfo;
    }

    return rc;
}


VBOXDDU_DECL(int) VDCreate(PVDINTERFACE pVDIfsDisk, VDTYPE enmType, PVDISK *ppDisk)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;

    LogFlowFunc(("pVDIfsDisk=%#p\n", pVDIfsDisk));
    /* Check arguments. */
    AssertPtrReturn(ppDisk, VERR_INVALID_POINTER);

    do
    {
        pDisk = (PVDISK)RTMemAllocZ(sizeof(VDISK));
        if (pDisk)
        {
            pDisk->u32Signature            = VDISK_SIGNATURE;
            pDisk->enmType                 = enmType;
            pDisk->cImages                 = 0;
            pDisk->pBase                   = NULL;
            pDisk->pLast                   = NULL;
            pDisk->cbSize                  = 0;
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads     = 0;
            pDisk->PCHSGeometry.cSectors   = 0;
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads     = 0;
            pDisk->LCHSGeometry.cSectors   = 0;
            pDisk->pVDIfsDisk              = pVDIfsDisk;
            pDisk->pInterfaceError         = NULL;
            pDisk->pInterfaceThreadSync    = NULL;
            pDisk->pIoCtxLockOwner         = NULL;
            pDisk->pIoCtxHead              = NULL;
            pDisk->fLocked                 = false;
            pDisk->hMemCacheIoCtx          = NIL_RTMEMCACHE;
            pDisk->hMemCacheIoTask         = NIL_RTMEMCACHE;
            RTListInit(&pDisk->ListFilterChainWrite);
            RTListInit(&pDisk->ListFilterChainRead);

            /* Create the I/O ctx cache */
            rc = RTMemCacheCreate(&pDisk->hMemCacheIoCtx, sizeof(VDIOCTX), 0, UINT32_MAX,
                                  NULL, NULL, NULL, 0);
            if (RT_FAILURE(rc))
                break;

            /* Create the I/O task cache */
            rc = RTMemCacheCreate(&pDisk->hMemCacheIoTask, sizeof(VDIOTASK), 0, UINT32_MAX,
                                  NULL, NULL, NULL, 0);
            if (RT_FAILURE(rc))
                break;

            pDisk->pInterfaceError      = VDIfErrorGet(pVDIfsDisk);
            pDisk->pInterfaceThreadSync = VDIfThreadSyncGet(pVDIfsDisk);

            *ppDisk = pDisk;
        }
        else
        {
            rc = VERR_NO_MEMORY;
            break;
        }
    } while (0);

    if (   RT_FAILURE(rc)
        && pDisk)
    {
        if (pDisk->hMemCacheIoCtx != NIL_RTMEMCACHE)
            RTMemCacheDestroy(pDisk->hMemCacheIoCtx);
        if (pDisk->hMemCacheIoTask != NIL_RTMEMCACHE)
            RTMemCacheDestroy(pDisk->hMemCacheIoTask);
    }

    LogFlowFunc(("returns %Rrc (pDisk=%#p)\n", rc, pDisk));
    return rc;
}


VBOXDDU_DECL(int) VDDestroy(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreak(pDisk);
        AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));
        Assert(!pDisk->fLocked);

        rc = VDCloseAll(pDisk);
        int rc2 = VDFilterRemoveAll(pDisk);
        if (RT_SUCCESS(rc))
            rc = rc2;

        RTMemCacheDestroy(pDisk->hMemCacheIoCtx);
        RTMemCacheDestroy(pDisk->hMemCacheIoTask);
        RTMemFree(pDisk);
    } while (0);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetFormat(PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                              const char *pszFilename, VDTYPE enmDesiredType,
                              char **ppszFormat, VDTYPE *penmType)
{
    int rc = VERR_NOT_SUPPORTED;
    VDINTERFACEIOINT VDIfIoInt;
    VDINTERFACEIO    VDIfIoFallback;
    PVDINTERFACEIO   pInterfaceIo;

    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));
    /* Check arguments. */
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszFormat, VERR_INVALID_POINTER);
    AssertPtrReturn(penmType, VERR_INVALID_POINTER);
    AssertReturn(enmDesiredType >= VDTYPE_INVALID && enmDesiredType <= VDTYPE_FLOPPY, VERR_INVALID_PARAMETER);

    if (!vdPluginIsInitialized())
        VDInit();

    pInterfaceIo = VDIfIoGet(pVDIfsImage);
    if (!pInterfaceIo)
    {
        /*
         * Caller doesn't provide an I/O interface, create our own using the
         * native file API.
         */
        vdIfIoFallbackCallbacksSetup(&VDIfIoFallback);
        pInterfaceIo = &VDIfIoFallback;
    }

    /* Set up the internal I/O interface. */
    AssertReturn(!VDIfIoIntGet(pVDIfsImage), VERR_INVALID_PARAMETER);
    VDIfIoInt.pfnOpen                   = vdIOIntOpenLimited;
    VDIfIoInt.pfnClose                  = vdIOIntCloseLimited;
    VDIfIoInt.pfnDelete                 = vdIOIntDeleteLimited;
    VDIfIoInt.pfnMove                   = vdIOIntMoveLimited;
    VDIfIoInt.pfnGetFreeSpace           = vdIOIntGetFreeSpaceLimited;
    VDIfIoInt.pfnGetModificationTime    = vdIOIntGetModificationTimeLimited;
    VDIfIoInt.pfnGetSize                = vdIOIntGetSizeLimited;
    VDIfIoInt.pfnSetSize                = vdIOIntSetSizeLimited;
    VDIfIoInt.pfnReadUser               = vdIOIntReadUserLimited;
    VDIfIoInt.pfnWriteUser              = vdIOIntWriteUserLimited;
    VDIfIoInt.pfnReadMeta               = vdIOIntReadMetaLimited;
    VDIfIoInt.pfnWriteMeta              = vdIOIntWriteMetaLimited;
    VDIfIoInt.pfnFlush                  = vdIOIntFlushLimited;
    rc = VDInterfaceAdd(&VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                        pInterfaceIo, sizeof(VDINTERFACEIOINT), &pVDIfsImage);
    AssertRC(rc);

    /** @todo r=bird: Would be better to do a scoring approach here, where the
     * backend that scores the highest is choosen.  That way we don't have to depend
     * on registration order and filename suffixes to figure out what RAW should
     * handle and not.   Besides, the registration order won't cut it for plug-ins
     * anyway, as they end up after the builtin ones.
     */

    /* Find the backend supporting this file format. */
    for (unsigned i = 0; i < vdGetImageBackendCount(); i++)
    {
        PCVDIMAGEBACKEND pBackend;
        rc = vdQueryImageBackend(i, &pBackend);
        AssertRC(rc);

        if (pBackend->pfnProbe)
        {
            rc = pBackend->pfnProbe(pszFilename, pVDIfsDisk, pVDIfsImage, enmDesiredType, penmType);
            if (    RT_SUCCESS(rc)
                /* The correct backend has been found, but there is a small
                 * incompatibility so that the file cannot be used. Stop here
                 * and signal success - the actual open will of course fail,
                 * but that will create a really sensible error message. */

                    /** @todo r=bird: this bit of code is _certifiably_ _insane_ as it allows
                     *        simple stuff like VERR_EOF to pass thru.  I've just amended it with
                     *        disallowing VERR_EOF too, but someone needs to pick up the courage to
                     *        fix this stuff properly or at least update the docs!
                     *        (Parallels returns VERR_EOF, btw.) */

                ||  (   rc != VERR_VD_GEN_INVALID_HEADER
                     && rc != VERR_VD_VDI_INVALID_HEADER
                     && rc != VERR_VD_VMDK_INVALID_HEADER
                     && rc != VERR_VD_ISCSI_INVALID_HEADER
                     && rc != VERR_VD_VHD_INVALID_HEADER
                     && rc != VERR_VD_RAW_INVALID_HEADER
                     && rc != VERR_VD_RAW_SIZE_MODULO_512
                     && rc != VERR_VD_RAW_SIZE_MODULO_2048
                     && rc != VERR_VD_RAW_SIZE_OPTICAL_TOO_SMALL
                     && rc != VERR_VD_RAW_SIZE_FLOPPY_TOO_BIG
                     && rc != VERR_VD_PARALLELS_INVALID_HEADER
                     && rc != VERR_VD_DMG_INVALID_HEADER
                     && rc != VERR_EOF /* bird for viso */
                        ))
            {
                /* Copy the name into the new string. */
                char *pszFormat = RTStrDup(pBackend->pszBackendName);
                if (!pszFormat)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }
                *ppszFormat = pszFormat;
                /* Do not consider the typical file access errors as success,
                 * which allows the caller to deal with such issues. */
                if (   rc != VERR_ACCESS_DENIED
                    && rc != VERR_PATH_NOT_FOUND
                    && rc != VERR_FILE_NOT_FOUND)
                    rc = VINF_SUCCESS;
                break;
            }
            rc = VERR_NOT_SUPPORTED;
        }
    }

    /* Try the cache backends. */
    if (rc == VERR_NOT_SUPPORTED)
    {
        for (unsigned i = 0; i < vdGetCacheBackendCount(); i++)
        {
            PCVDCACHEBACKEND pBackend;
            rc = vdQueryCacheBackend(i, &pBackend);
            AssertRC(rc);

            if (pBackend->pfnProbe)
            {
                rc = pBackend->pfnProbe(pszFilename, pVDIfsDisk, pVDIfsImage);
                if (    RT_SUCCESS(rc)
                    ||  (rc != VERR_VD_GEN_INVALID_HEADER))
                {
                    /* Copy the name into the new string. */
                    char *pszFormat = RTStrDup(pBackend->pszBackendName);
                    if (!pszFormat)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    *ppszFormat = pszFormat;
                    rc = VINF_SUCCESS;
                    break;
                }
                rc = VERR_NOT_SUPPORTED;
            }
        }
    }

    LogFlowFunc(("returns %Rrc *ppszFormat=\"%s\"\n", rc, *ppszFormat));
    return rc;
}


VBOXDDU_DECL(int) VDOpen(PVDISK pDisk, const char *pszBackend,
                         const char *pszFilename, unsigned uOpenFlags,
                         PVDINTERFACE pVDIfsImage)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;
    PVDIMAGE pImage = NULL;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" uOpenFlags=%#x, pVDIfsImage=%#p\n",
                 pDisk, pszBackend, pszFilename, uOpenFlags, pVDIfsImage));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_PARAMETER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertReturn(*pszBackend != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0,
                    ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(   !(uOpenFlags & VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)
                    ||  (uOpenFlags & VD_OPEN_FLAGS_READONLY),
                    ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    do
    {
        /*
         * Destroy the current discard state first which might still have pending blocks
         * for the currently opened image which will be switched to readonly mode.
         */
        /* Lock disk for writing, as we modify pDisk information below. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;
        rc = vdDiscardStateDestroy(pDisk);
        if (RT_FAILURE(rc))
            break;
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = false;

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        pImage->cbImage     = VD_IMAGE_SIZE_UNINITIALIZED;
        pImage->VDIo.pDisk  = pDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdFindImageBackend(pszBackend, &pImage->Backend);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        /*
         * Fail if the backend can't do async I/O but the
         * flag is set.
         */
        if (   !(pImage->Backend->uBackendCaps & VD_CAP_ASYNC)
            && (uOpenFlags & VD_OPEN_FLAGS_ASYNC_IO))
        {
            rc = vdError(pDisk, VERR_NOT_SUPPORTED, RT_SRC_POS,
                         N_("VD: Backend '%s' does not support async I/O"), pszBackend);
            break;
        }

        /*
         * Fail if the backend doesn't support the discard operation but the
         * flag is set.
         */
        if (   !(pImage->Backend->uBackendCaps & VD_CAP_DISCARD)
            && (uOpenFlags & VD_OPEN_FLAGS_DISCARD))
        {
            rc = vdError(pDisk, VERR_VD_DISCARD_NOT_SUPPORTED, RT_SRC_POS,
                         N_("VD: Backend '%s' does not support discard"), pszBackend);
            break;
        }

        /* Set up the I/O interface. */
        pImage->VDIo.pInterfaceIo = VDIfIoGet(pVDIfsImage);
        if (!pImage->VDIo.pInterfaceIo)
        {
            vdIfIoFallbackCallbacksSetup(&pImage->VDIo.VDIfIo);
            rc = VDInterfaceAdd(&pImage->VDIo.VDIfIo.Core, "VD_IO", VDINTERFACETYPE_IO,
                                pDisk, sizeof(VDINTERFACEIO), &pVDIfsImage);
            pImage->VDIo.pInterfaceIo = &pImage->VDIo.VDIfIo;
        }

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsImage), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pImage->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pImage->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pImage->VDIo, sizeof(VDINTERFACEIOINT), &pImage->pVDIfsImage);
        AssertRC(rc);

        pImage->uOpenFlags = uOpenFlags & (VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_DISCARD | VD_OPEN_FLAGS_IGNORE_FLUSH | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS);
        pImage->VDIo.fIgnoreFlush = (uOpenFlags & VD_OPEN_FLAGS_IGNORE_FLUSH) != 0;
        rc = pImage->Backend->pfnOpen(pImage->pszFilename,
                                      uOpenFlags & ~(VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_IGNORE_FLUSH | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS),
                                      pDisk->pVDIfsDisk,
                                      pImage->pVDIfsImage,
                                      pDisk->enmType,
                                      &pImage->pBackendData);
        /*
         * If the image is corrupted and there is a repair method try to repair it
         * first if it was openend in read-write mode and open again afterwards.
         */
        if (   RT_UNLIKELY(rc == VERR_VD_IMAGE_CORRUPTED)
            && !(uOpenFlags & VD_OPEN_FLAGS_READONLY)
            && pImage->Backend->pfnRepair)
        {
            rc = pImage->Backend->pfnRepair(pszFilename, pDisk->pVDIfsDisk, pImage->pVDIfsImage, 0 /* fFlags */);
            if (RT_SUCCESS(rc))
                rc = pImage->Backend->pfnOpen(pImage->pszFilename,
                                              uOpenFlags & ~(VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_IGNORE_FLUSH | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS),
                                              pDisk->pVDIfsDisk,
                                              pImage->pVDIfsImage,
                                              pDisk->enmType,
                                              &pImage->pBackendData);
            else
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: error %Rrc repairing corrupted image file '%s'"), rc, pszFilename);
                break;
            }
        }
        else if (RT_UNLIKELY(rc == VERR_VD_IMAGE_CORRUPTED))
        {
            rc = vdError(pDisk, rc, RT_SRC_POS,
                         N_("VD: Image file '%s' is corrupted and can't be opened"), pszFilename);
            break;
        }

        /* If the open in read-write mode failed, retry in read-only mode. */
        if (RT_FAILURE(rc))
        {
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY)
                &&  (   rc == VERR_ACCESS_DENIED
                     || rc == VERR_PERMISSION_DENIED
                     || rc == VERR_WRITE_PROTECT
                     || rc == VERR_SHARING_VIOLATION
                     || rc == VERR_FILE_LOCK_FAILED))
                rc = pImage->Backend->pfnOpen(pImage->pszFilename,
                                                (uOpenFlags & ~(VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS))
                                               | VD_OPEN_FLAGS_READONLY,
                                               pDisk->pVDIfsDisk,
                                               pImage->pVDIfsImage,
                                               pDisk->enmType,
                                               &pImage->pBackendData);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: error %Rrc opening image file '%s'"), rc, pszFilename);
                break;
            }
        }

        /* Lock disk for writing, as we modify pDisk information below. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        pImage->VDIo.pBackendData = pImage->pBackendData;

        /* Check image type. As the image itself has only partial knowledge
         * whether it's a base image or not, this info is derived here. The
         * base image can be fixed or normal, all others must be normal or
         * diff images. Some image formats don't distinguish between normal
         * and diff images, so this must be corrected here. */
        unsigned uImageFlags;
        uImageFlags = pImage->Backend->pfnGetImageFlags(pImage->pBackendData);
        if (RT_FAILURE(rc))
            uImageFlags = VD_IMAGE_FLAGS_NONE;
        if (    RT_SUCCESS(rc)
            &&  !(uOpenFlags & VD_OPEN_FLAGS_INFO))
        {
            if (    pDisk->cImages == 0
                &&  (uImageFlags & VD_IMAGE_FLAGS_DIFF))
            {
                rc = VERR_VD_INVALID_TYPE;
                break;
            }
            else if (pDisk->cImages != 0)
            {
                if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
                {
                    rc = VERR_VD_INVALID_TYPE;
                    break;
                }
                else
                    uImageFlags |= VD_IMAGE_FLAGS_DIFF;
            }
        }

        /* Ensure we always get correct diff information, even if the backend
         * doesn't actually have a stored flag for this. It must not return
         * bogus information for the parent UUID if it is not a diff image. */
        RTUUID parentUuid;
        RTUuidClear(&parentUuid);
        rc2 = pImage->Backend->pfnGetParentUuid(pImage->pBackendData, &parentUuid);
        if (RT_SUCCESS(rc2) && !RTUuidIsNull(&parentUuid))
            uImageFlags |= VD_IMAGE_FLAGS_DIFF;

        pImage->uImageFlags = uImageFlags;

        /* Force sane optimization settings. It's not worth avoiding writes
         * to fixed size images. The overhead would have almost no payback. */
        if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
            pImage->uOpenFlags |= VD_OPEN_FLAGS_HONOR_SAME;

        /** @todo optionally check UUIDs */

        /* Cache disk information. */
        pDisk->cbSize = vdImageGetSize(pImage);

        /* Cache PCHS geometry. */
        rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData,
                                                  &pDisk->PCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads = 0;
            pDisk->PCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the PCHS geometry is properly clipped. */
            pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
            pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
            pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
        }

        /* Cache LCHS geometry. */
        rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData,
                                                  &pDisk->LCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads = 0;
            pDisk->LCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the LCHS geometry is properly clipped. */
            pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
            pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
        }

        if (pDisk->cImages != 0)
        {
            /* Switch previous image to read-only mode. */
            unsigned uOpenFlagsPrevImg;
            uOpenFlagsPrevImg = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pBackendData);
            if (!(uOpenFlagsPrevImg & VD_OPEN_FLAGS_READONLY))
            {
                uOpenFlagsPrevImg |= VD_OPEN_FLAGS_READONLY;
                rc = pDisk->pLast->Backend->pfnSetOpenFlags(pDisk->pLast->pBackendData, uOpenFlagsPrevImg);
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, but image opened. Close image. */
            rc2 = pImage->Backend->pfnClose(pImage->pBackendData, false);
            AssertRC(rc2);
            pImage->pBackendData = NULL;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pImage)
        {
            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCacheOpen(PVDISK pDisk, const char *pszBackend,
                              const char *pszFilename, unsigned uOpenFlags,
                              PVDINTERFACE pVDIfsCache)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;
    PVDCACHE pCache = NULL;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" uOpenFlags=%#x, pVDIfsCache=%#p\n",
                 pDisk, pszBackend, pszFilename, uOpenFlags, pVDIfsCache));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_PARAMETER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertReturn(*pszBackend != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0, ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    do
    {
        /* Set up image descriptor. */
        pCache = (PVDCACHE)RTMemAllocZ(sizeof(VDCACHE));
        if (!pCache)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pCache->pszFilename = RTStrDup(pszFilename);
        if (!pCache->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        pCache->VDIo.pDisk  = pDisk;
        pCache->pVDIfsCache = pVDIfsCache;

        rc = vdFindCacheBackend(pszBackend, &pCache->Backend);
        if (RT_FAILURE(rc))
            break;
        if (!pCache->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        /* Set up the I/O interface. */
        pCache->VDIo.pInterfaceIo = VDIfIoGet(pVDIfsCache);
        if (!pCache->VDIo.pInterfaceIo)
        {
            vdIfIoFallbackCallbacksSetup(&pCache->VDIo.VDIfIo);
            rc = VDInterfaceAdd(&pCache->VDIo.VDIfIo.Core, "VD_IO", VDINTERFACETYPE_IO,
                                pDisk, sizeof(VDINTERFACEIO), &pVDIfsCache);
            pCache->VDIo.pInterfaceIo = &pCache->VDIo.VDIfIo;
        }

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsCache), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pCache->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pCache->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pCache->VDIo, sizeof(VDINTERFACEIOINT), &pCache->pVDIfsCache);
        AssertRC(rc);

        pCache->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        rc = pCache->Backend->pfnOpen(pCache->pszFilename,
                                      uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                      pDisk->pVDIfsDisk,
                                      pCache->pVDIfsCache,
                                      &pCache->pBackendData);
        /* If the open in read-write mode failed, retry in read-only mode. */
        if (RT_FAILURE(rc))
        {
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY)
                &&  (   rc == VERR_ACCESS_DENIED
                     || rc == VERR_PERMISSION_DENIED
                     || rc == VERR_WRITE_PROTECT
                     || rc == VERR_SHARING_VIOLATION
                     || rc == VERR_FILE_LOCK_FAILED))
                rc = pCache->Backend->pfnOpen(pCache->pszFilename,
                                                (uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME)
                                               | VD_OPEN_FLAGS_READONLY,
                                               pDisk->pVDIfsDisk,
                                               pCache->pVDIfsCache,
                                               &pCache->pBackendData);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: error %Rrc opening image file '%s'"), rc, pszFilename);
                break;
            }
        }

        /* Lock disk for writing, as we modify pDisk information below. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        /*
         * Check that the modification UUID of the cache and last image
         * match. If not the image was modified in-between without the cache.
         * The cache might contain stale data.
         */
        RTUUID UuidImage, UuidCache;

        rc = pCache->Backend->pfnGetModificationUuid(pCache->pBackendData,
                                                     &UuidCache);
        if (RT_SUCCESS(rc))
        {
            rc = pDisk->pLast->Backend->pfnGetModificationUuid(pDisk->pLast->pBackendData,
                                                               &UuidImage);
            if (RT_SUCCESS(rc))
            {
                if (RTUuidCompare(&UuidImage, &UuidCache))
                    rc = VERR_VD_CACHE_NOT_UP_TO_DATE;
            }
        }

        /*
         * We assume that the user knows what he is doing if one of the images
         * doesn't support the modification uuid.
         */
        if (rc == VERR_NOT_SUPPORTED)
            rc = VINF_SUCCESS;

        if (RT_SUCCESS(rc))
        {
            /* Cache successfully opened, make it the current one. */
            if (!pDisk->pCache)
                pDisk->pCache = pCache;
            else
                rc = VERR_VD_CACHE_ALREADY_EXISTS;
        }

        if (RT_FAILURE(rc))
        {
            /* Error detected, but image opened. Close image. */
            rc2 = pCache->Backend->pfnClose(pCache->pBackendData, false);
            AssertRC(rc2);
            pCache->pBackendData = NULL;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pCache)
        {
            if (pCache->pszFilename)
                RTStrFree(pCache->pszFilename);
            RTMemFree(pCache);
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDFilterAdd(PVDISK pDisk, const char *pszFilter, uint32_t fFlags,
                              PVDINTERFACE pVDIfsFilter)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;
    PVDFILTER pFilter = NULL;

    LogFlowFunc(("pDisk=%#p pszFilter=\"%s\" pVDIfsFilter=%#p\n",
                 pDisk, pszFilter, pVDIfsFilter));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_PARAMETER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszFilter, VERR_INVALID_POINTER);
    AssertReturn(*pszFilter != '\0', VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fFlags & ~VD_FILTER_FLAGS_MASK), ("Invalid flags set (fFlags=%#x)\n", fFlags),
                    VERR_INVALID_PARAMETER);

    do
    {
        /* Set up image descriptor. */
        pFilter = (PVDFILTER)RTMemAllocZ(sizeof(VDFILTER));
        if (!pFilter)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdFindFilterBackend(pszFilter, &pFilter->pBackend);
        if (RT_FAILURE(rc))
            break;
        if (!pFilter->pBackend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown filter backend name '%s'"), pszFilter);
            break;
        }

        pFilter->VDIo.pDisk   = pDisk;
        pFilter->pVDIfsFilter = pVDIfsFilter;

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsFilter), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pFilter->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pFilter->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pFilter->VDIo, sizeof(VDINTERFACEIOINT), &pFilter->pVDIfsFilter);
        AssertRC(rc);

        rc = pFilter->pBackend->pfnCreate(pDisk->pVDIfsDisk, fFlags & VD_FILTER_FLAGS_INFO,
                                          pFilter->pVDIfsFilter, &pFilter->pvBackendData);
        if (RT_FAILURE(rc))
            break;

        /* Lock disk for writing, as we modify pDisk information below. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        /* Add filter to chains. */
        if (fFlags & VD_FILTER_FLAGS_WRITE)
        {
            RTListAppend(&pDisk->ListFilterChainWrite, &pFilter->ListNodeChainWrite);
            vdFilterRetain(pFilter);
        }

        if (fFlags & VD_FILTER_FLAGS_READ)
        {
            RTListAppend(&pDisk->ListFilterChainRead, &pFilter->ListNodeChainRead);
            vdFilterRetain(pFilter);
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pFilter)
            RTMemFree(pFilter);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCreateBase(PVDISK pDisk, const char *pszBackend,
                               const char *pszFilename, uint64_t cbSize,
                               unsigned uImageFlags, const char *pszComment,
                               PCVDGEOMETRY pPCHSGeometry,
                               PCVDGEOMETRY pLCHSGeometry,
                               PCRTUUID pUuid, unsigned uOpenFlags,
                               PVDINTERFACE pVDIfsImage,
                               PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false, fLockRead = false;
    PVDIMAGE pImage = NULL;
    RTUUID uuid;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" PCHS=%u/%u/%u LCHS=%u/%u/%u Uuid=%RTuuid uOpenFlags=%#x pVDIfsImage=%#p pVDIfsOperation=%#p\n",
                 pDisk, pszBackend, pszFilename, cbSize, uImageFlags, pszComment,
                 pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads,
                 pPCHSGeometry->cSectors, pLCHSGeometry->cCylinders,
                 pLCHSGeometry->cHeads, pLCHSGeometry->cSectors, pUuid,
                 uOpenFlags, pVDIfsImage, pVDIfsOperation));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsgReturn(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature),
                    VERR_INVALID_MAGIC);

    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertReturn(*pszBackend != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbSize || (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK), ("cbSize=%llu\n", cbSize),
                    VERR_INVALID_PARAMETER);
    if (cbSize % 512 && !(uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK))
    {
        rc = vdError(pDisk, VERR_VD_INVALID_SIZE, RT_SRC_POS,
                     N_("VD: The given disk size %llu is not aligned on a sector boundary (512 bytes)"), cbSize);
        LogFlowFunc(("returns %Rrc\n", rc));
        return rc;
    }
    AssertMsgReturn(   ((uImageFlags & ~VD_IMAGE_FLAGS_MASK) == 0)
                    || ((uImageFlags & (VD_IMAGE_FLAGS_FIXED | VD_IMAGE_FLAGS_DIFF)) != VD_IMAGE_FLAGS_FIXED),
                    ("uImageFlags=%#x\n", uImageFlags),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(   !(uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK)
                    || !(uImageFlags & ~(VD_VMDK_IMAGE_FLAGS_RAWDISK | VD_IMAGE_FLAGS_FIXED)),
                    ("uImageFlags=%#x\n", uImageFlags),
                    VERR_INVALID_PARAMETER);
    /* The PCHS geometry fields may be 0 to leave it for later. */
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_PARAMETER);
    AssertMsgReturn(   pPCHSGeometry->cHeads <= 16
                    && pPCHSGeometry->cSectors <= 63,
                    ("PCHS=%u/%u/%u\n", pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors),
                    VERR_INVALID_PARAMETER);
    /* The LCHS geometry fields may be 0 to leave it to later autodetection. */
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);
    AssertMsgReturn(   pLCHSGeometry->cHeads <= 255
                    && pLCHSGeometry->cSectors <= 63,
                    ("LCHS=%u/%u/%u\n", pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors),
                    VERR_INVALID_PARAMETER);
    /* The UUID may be NULL. */
    AssertPtrNullReturn(pUuid, VERR_INVALID_POINTER);
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0, ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    AssertPtrNullReturn(pVDIfsOperation, VERR_INVALID_PARAMETER);
    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do
    {
        /* Check state. Needs a temporary read lock. Holding the write lock
         * all the time would be blocking other activities for too long. */
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;
        AssertMsgBreakStmt(pDisk->cImages == 0,
                           ("Create base image cannot be done with other images open\n"),
                           rc = VERR_VD_INVALID_STATE);
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->cbImage     = VD_IMAGE_SIZE_UNINITIALIZED;
        pImage->VDIo.pDisk  = pDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        /* Set up the I/O interface. */
        pImage->VDIo.pInterfaceIo = VDIfIoGet(pVDIfsImage);
        if (!pImage->VDIo.pInterfaceIo)
        {
            vdIfIoFallbackCallbacksSetup(&pImage->VDIo.VDIfIo);
            rc = VDInterfaceAdd(&pImage->VDIo.VDIfIo.Core, "VD_IO", VDINTERFACETYPE_IO,
                                pDisk, sizeof(VDINTERFACEIO), &pVDIfsImage);
            pImage->VDIo.pInterfaceIo = &pImage->VDIo.VDIfIo;
        }

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsImage), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pImage->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pImage->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pImage->VDIo, sizeof(VDINTERFACEIOINT), &pImage->pVDIfsImage);
        AssertRC(rc);

        rc = vdFindImageBackend(pszBackend, &pImage->Backend);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }
        if (!(pImage->Backend->uBackendCaps & (  VD_CAP_CREATE_FIXED
                                               | VD_CAP_CREATE_DYNAMIC)))
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: backend '%s' cannot create base images"), pszBackend);
            break;
        }
        if (   (   (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
                && !(pImage->Backend->uBackendCaps & VD_CAP_CREATE_SPLIT_2G))
            || (   (uImageFlags & (  VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED
                                   | VD_VMDK_IMAGE_FLAGS_RAWDISK))
                && RTStrICmp(pszBackend, "VMDK")))
        {
            rc =  vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                          N_("VD: backend '%s' does not support the selected image variant"), pszBackend);
            break;
        }

        /* Create UUID if the caller didn't specify one. */
        if (!pUuid)
        {
            rc = RTUuidCreate(&uuid);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: cannot generate UUID for image '%s'"),
                             pszFilename);
                break;
            }
            pUuid = &uuid;
        }

        pImage->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        uImageFlags &= ~VD_IMAGE_FLAGS_DIFF;
        pImage->VDIo.fIgnoreFlush = (uOpenFlags & VD_OPEN_FLAGS_IGNORE_FLUSH) != 0;
        rc = pImage->Backend->pfnCreate(pImage->pszFilename, cbSize,
                                        uImageFlags, pszComment, pPCHSGeometry,
                                        pLCHSGeometry, pUuid,
                                        uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                        0, 99,
                                        pDisk->pVDIfsDisk,
                                        pImage->pVDIfsImage,
                                        pVDIfsOperation,
                                        pDisk->enmType,
                                        &pImage->pBackendData);

        if (RT_SUCCESS(rc))
        {
            pImage->VDIo.pBackendData = pImage->pBackendData;
            pImage->uImageFlags = uImageFlags;

            /* Force sane optimization settings. It's not worth avoiding writes
             * to fixed size images. The overhead would have almost no payback. */
            if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
                pImage->uOpenFlags |= VD_OPEN_FLAGS_HONOR_SAME;

            /* Lock disk for writing, as we modify pDisk information below. */
            rc2 = vdThreadStartWrite(pDisk);
            AssertRC(rc2);
            fLockWrite = true;

            /** @todo optionally check UUIDs */

            /* Re-check state, as the lock wasn't held and another image
             * creation call could have been done by another thread. */
            AssertMsgStmt(pDisk->cImages == 0,
                          ("Create base image cannot be done with other images open\n"),
                          rc = VERR_VD_INVALID_STATE);
        }

        if (RT_SUCCESS(rc))
        {
            /* Cache disk information. */
            pDisk->cbSize = vdImageGetSize(pImage);

            /* Cache PCHS geometry. */
            rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData,
                                                      &pDisk->PCHSGeometry);
            if (RT_FAILURE(rc2))
            {
                pDisk->PCHSGeometry.cCylinders = 0;
                pDisk->PCHSGeometry.cHeads = 0;
                pDisk->PCHSGeometry.cSectors = 0;
            }
            else
            {
                /* Make sure the CHS geometry is properly clipped. */
                pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
                pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
                pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
            }

            /* Cache LCHS geometry. */
            rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData,
                                                      &pDisk->LCHSGeometry);
            if (RT_FAILURE(rc2))
            {
                pDisk->LCHSGeometry.cCylinders = 0;
                pDisk->LCHSGeometry.cHeads = 0;
                pDisk->LCHSGeometry.cSectors = 0;
            }
            else
            {
                /* Make sure the CHS geometry is properly clipped. */
                pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
                pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
            }

            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, image may or may not be opened. Close and delete
             * image if it was opened. */
            if (pImage->pBackendData)
            {
                rc2 = pImage->Backend->pfnClose(pImage->pBackendData, true);
                AssertRC(rc2);
                pImage->pBackendData = NULL;
            }
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pImage)
        {
            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    if (RT_SUCCESS(rc) && pIfProgress && pIfProgress->pfnProgress)
        pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCreateDiff(PVDISK pDisk, const char *pszBackend,
                               const char *pszFilename, unsigned uImageFlags,
                               const char *pszComment, PCRTUUID pUuid,
                               PCRTUUID pParentUuid, unsigned uOpenFlags,
                               PVDINTERFACE pVDIfsImage,
                               PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false, fLockRead = false;
    PVDIMAGE pImage = NULL;
    RTUUID uuid;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" uImageFlags=%#x pszComment=\"%s\" Uuid=%RTuuid uOpenFlags=%#x pVDIfsImage=%#p pVDIfsOperation=%#p\n",
                 pDisk, pszBackend, pszFilename, uImageFlags, pszComment, pUuid, uOpenFlags, pVDIfsImage, pVDIfsOperation));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_PARAMETER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertReturn(*pszBackend != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertMsgReturn((uImageFlags & ~VD_IMAGE_FLAGS_MASK) == 0, ("uImageFlags=%#x\n", uImageFlags),
                    VERR_INVALID_PARAMETER);
    /* The UUID may be NULL. */
    AssertPtrNullReturn(pUuid, VERR_INVALID_POINTER);
    /* The parent UUID may be NULL. */
    AssertPtrNullReturn(pParentUuid, VERR_INVALID_POINTER);
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0, ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);
    do
    {
        /* Check state. Needs a temporary read lock. Holding the write lock
         * all the time would be blocking other activities for too long. */
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;
        AssertMsgBreakStmt(pDisk->cImages != 0,
                           ("Create diff image cannot be done without other images open\n"),
                           rc = VERR_VD_INVALID_STATE);
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        /*
         * Destroy the current discard state first which might still have pending blocks
         * for the currently opened image which will be switched to readonly mode.
         */
        /* Lock disk for writing, as we modify pDisk information below. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;
        rc = vdDiscardStateDestroy(pDisk);
        if (RT_FAILURE(rc))
            break;
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = false;

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdFindImageBackend(pszBackend, &pImage->Backend);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }
        if (   !(pImage->Backend->uBackendCaps & VD_CAP_DIFF)
            || !(pImage->Backend->uBackendCaps & (  VD_CAP_CREATE_FIXED
                                                  | VD_CAP_CREATE_DYNAMIC)))
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: backend '%s' cannot create diff images"), pszBackend);
            break;
        }

        pImage->cbImage     = VD_IMAGE_SIZE_UNINITIALIZED;
        pImage->VDIo.pDisk  = pDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        /* Set up the I/O interface. */
        pImage->VDIo.pInterfaceIo = VDIfIoGet(pVDIfsImage);
        if (!pImage->VDIo.pInterfaceIo)
        {
            vdIfIoFallbackCallbacksSetup(&pImage->VDIo.VDIfIo);
            rc = VDInterfaceAdd(&pImage->VDIo.VDIfIo.Core, "VD_IO", VDINTERFACETYPE_IO,
                                pDisk, sizeof(VDINTERFACEIO), &pVDIfsImage);
            pImage->VDIo.pInterfaceIo = &pImage->VDIo.VDIfIo;
        }

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsImage), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pImage->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pImage->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pImage->VDIo, sizeof(VDINTERFACEIOINT), &pImage->pVDIfsImage);
        AssertRC(rc);

        /* Create UUID if the caller didn't specify one. */
        if (!pUuid)
        {
            rc = RTUuidCreate(&uuid);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: cannot generate UUID for image '%s'"),
                             pszFilename);
                break;
            }
            pUuid = &uuid;
        }

        pImage->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        pImage->VDIo.fIgnoreFlush = (uOpenFlags & VD_OPEN_FLAGS_IGNORE_FLUSH) != 0;
        uImageFlags |= VD_IMAGE_FLAGS_DIFF;
        rc = pImage->Backend->pfnCreate(pImage->pszFilename, pDisk->cbSize,
                                        uImageFlags | VD_IMAGE_FLAGS_DIFF,
                                        pszComment, &pDisk->PCHSGeometry,
                                        &pDisk->LCHSGeometry, pUuid,
                                        uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                        0, 99,
                                        pDisk->pVDIfsDisk,
                                        pImage->pVDIfsImage,
                                        pVDIfsOperation,
                                        pDisk->enmType,
                                        &pImage->pBackendData);

        if (RT_SUCCESS(rc))
        {
            pImage->VDIo.pBackendData = pImage->pBackendData;
            pImage->uImageFlags = uImageFlags;

            /* Lock disk for writing, as we modify pDisk information below. */
            rc2 = vdThreadStartWrite(pDisk);
            AssertRC(rc2);
            fLockWrite = true;

            /* Switch previous image to read-only mode. */
            unsigned uOpenFlagsPrevImg;
            uOpenFlagsPrevImg = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pBackendData);
            if (!(uOpenFlagsPrevImg & VD_OPEN_FLAGS_READONLY))
            {
                uOpenFlagsPrevImg |= VD_OPEN_FLAGS_READONLY;
                rc = pDisk->pLast->Backend->pfnSetOpenFlags(pDisk->pLast->pBackendData, uOpenFlagsPrevImg);
            }

            /** @todo optionally check UUIDs */

            /* Re-check state, as the lock wasn't held and another image
             * creation call could have been done by another thread. */
            AssertMsgStmt(pDisk->cImages != 0,
                          ("Create diff image cannot be done without other images open\n"),
                          rc = VERR_VD_INVALID_STATE);
        }

        if (RT_SUCCESS(rc))
        {
            RTUUID Uuid;
            RTTIMESPEC ts;

            if (pParentUuid && !RTUuidIsNull(pParentUuid))
            {
                Uuid = *pParentUuid;
                pImage->Backend->pfnSetParentUuid(pImage->pBackendData, &Uuid);
            }
            else
            {
                rc2 = pDisk->pLast->Backend->pfnGetUuid(pDisk->pLast->pBackendData,
                                                        &Uuid);
                if (RT_SUCCESS(rc2))
                    pImage->Backend->pfnSetParentUuid(pImage->pBackendData, &Uuid);
            }
            rc2 = pDisk->pLast->Backend->pfnGetModificationUuid(pDisk->pLast->pBackendData,
                                                                &Uuid);
            if (RT_SUCCESS(rc2))
                pImage->Backend->pfnSetParentModificationUuid(pImage->pBackendData,
                                                              &Uuid);
            if (pDisk->pLast->Backend->pfnGetTimestamp)
                rc2 = pDisk->pLast->Backend->pfnGetTimestamp(pDisk->pLast->pBackendData,
                                                             &ts);
            else
                rc2 = VERR_NOT_IMPLEMENTED;
            if (RT_SUCCESS(rc2) && pImage->Backend->pfnSetParentTimestamp)
                pImage->Backend->pfnSetParentTimestamp(pImage->pBackendData, &ts);

            if (pImage->Backend->pfnSetParentFilename)
                rc2 = pImage->Backend->pfnSetParentFilename(pImage->pBackendData, pDisk->pLast->pszFilename);
        }

        if (RT_SUCCESS(rc))
        {
            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, but image opened. Close and delete image. */
            rc2 = pImage->Backend->pfnClose(pImage->pBackendData, true);
            AssertRC(rc2);
            pImage->pBackendData = NULL;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pImage)
        {
            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    if (RT_SUCCESS(rc) && pIfProgress && pIfProgress->pfnProgress)
        pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCreateCache(PVDISK pDisk, const char *pszBackend,
                                const char *pszFilename, uint64_t cbSize,
                                unsigned uImageFlags, const char *pszComment,
                                PCRTUUID pUuid, unsigned uOpenFlags,
                                PVDINTERFACE pVDIfsCache, PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false, fLockRead = false;
    PVDCACHE pCache = NULL;
    RTUUID uuid;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" Uuid=%RTuuid uOpenFlags=%#x pVDIfsImage=%#p pVDIfsOperation=%#p\n",
                 pDisk, pszBackend, pszFilename, cbSize, uImageFlags, pszComment, pUuid, uOpenFlags, pVDIfsCache, pVDIfsOperation));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertReturn(*pszBackend, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertReturn(cbSize > 0, VERR_INVALID_PARAMETER);
    AssertMsgReturn((uImageFlags & ~VD_IMAGE_FLAGS_MASK) == 0, ("uImageFlags=%#x\n", uImageFlags),
                    VERR_INVALID_PARAMETER);
    /* The UUID may be NULL. */
    AssertPtrNullReturn(pUuid, VERR_INVALID_POINTER);
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0, ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do
    {
        /* Check state. Needs a temporary read lock. Holding the write lock
         * all the time would be blocking other activities for too long. */
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;
        AssertMsgBreakStmt(!pDisk->pCache,
                           ("Create cache image cannot be done with a cache already attached\n"),
                           rc = VERR_VD_CACHE_ALREADY_EXISTS);
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        /* Set up image descriptor. */
        pCache = (PVDCACHE)RTMemAllocZ(sizeof(VDCACHE));
        if (!pCache)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pCache->pszFilename = RTStrDup(pszFilename);
        if (!pCache->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdFindCacheBackend(pszBackend, &pCache->Backend);
        if (RT_FAILURE(rc))
            break;
        if (!pCache->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        pCache->VDIo.pDisk        = pDisk;
        pCache->pVDIfsCache       = pVDIfsCache;

        /* Set up the I/O interface. */
        pCache->VDIo.pInterfaceIo = VDIfIoGet(pVDIfsCache);
        if (!pCache->VDIo.pInterfaceIo)
        {
            vdIfIoFallbackCallbacksSetup(&pCache->VDIo.VDIfIo);
            rc = VDInterfaceAdd(&pCache->VDIo.VDIfIo.Core, "VD_IO", VDINTERFACETYPE_IO,
                                pDisk, sizeof(VDINTERFACEIO), &pVDIfsCache);
            pCache->VDIo.pInterfaceIo = &pCache->VDIo.VDIfIo;
        }

        /* Set up the internal I/O interface. */
        AssertBreakStmt(!VDIfIoIntGet(pVDIfsCache), rc = VERR_INVALID_PARAMETER);
        vdIfIoIntCallbacksSetup(&pCache->VDIo.VDIfIoInt);
        rc = VDInterfaceAdd(&pCache->VDIo.VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                            &pCache->VDIo, sizeof(VDINTERFACEIOINT), &pCache->pVDIfsCache);
        AssertRC(rc);

        /* Create UUID if the caller didn't specify one. */
        if (!pUuid)
        {
            rc = RTUuidCreate(&uuid);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: cannot generate UUID for image '%s'"),
                             pszFilename);
                break;
            }
            pUuid = &uuid;
        }

        pCache->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        pCache->VDIo.fIgnoreFlush = (uOpenFlags & VD_OPEN_FLAGS_IGNORE_FLUSH) != 0;
        rc = pCache->Backend->pfnCreate(pCache->pszFilename, cbSize,
                                        uImageFlags,
                                        pszComment, pUuid,
                                        uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                        0, 99,
                                        pDisk->pVDIfsDisk,
                                        pCache->pVDIfsCache,
                                        pVDIfsOperation,
                                        &pCache->pBackendData);

        if (RT_SUCCESS(rc))
        {
            /* Lock disk for writing, as we modify pDisk information below. */
            rc2 = vdThreadStartWrite(pDisk);
            AssertRC(rc2);
            fLockWrite = true;

            pCache->VDIo.pBackendData = pCache->pBackendData;

            /* Re-check state, as the lock wasn't held and another image
             * creation call could have been done by another thread. */
            AssertMsgStmt(!pDisk->pCache,
                          ("Create cache image cannot be done with another cache open\n"),
                          rc = VERR_VD_CACHE_ALREADY_EXISTS);
        }

        if (   RT_SUCCESS(rc)
            && pDisk->pLast)
        {
            RTUUID UuidModification;

            /* Set same modification Uuid as the last image. */
            rc = pDisk->pLast->Backend->pfnGetModificationUuid(pDisk->pLast->pBackendData,
                                                               &UuidModification);
            if (RT_SUCCESS(rc))
            {
                rc = pCache->Backend->pfnSetModificationUuid(pCache->pBackendData,
                                                             &UuidModification);
            }

            if (rc == VERR_NOT_SUPPORTED)
                rc = VINF_SUCCESS;
        }

        if (RT_SUCCESS(rc))
        {
            /* Cache successfully created. */
            pDisk->pCache = pCache;
        }
        else
        {
            /* Error detected, but image opened. Close and delete image. */
            rc2 = pCache->Backend->pfnClose(pCache->pBackendData, true);
            AssertRC(rc2);
            pCache->pBackendData = NULL;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
    {
        if (pCache)
        {
            if (pCache->pszFilename)
                RTStrFree(pCache->pszFilename);
            RTMemFree(pCache);
        }
    }

    if (RT_SUCCESS(rc) && pIfProgress && pIfProgress->pfnProgress)
        pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDMerge(PVDISK pDisk, unsigned nImageFrom,
                          unsigned nImageTo, PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false, fLockRead = false;
    void *pvBuf = NULL;

    LogFlowFunc(("pDisk=%#p nImageFrom=%u nImageTo=%u pVDIfsOperation=%#p\n",
                 pDisk, nImageFrom, nImageTo, pVDIfsOperation));

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* For simplicity reasons lock for writing as the image reopen below
         * might need it. After all the reopen is usually needed. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;
        PVDIMAGE pImageFrom = vdGetImageByNumber(pDisk, nImageFrom);
        PVDIMAGE pImageTo = vdGetImageByNumber(pDisk, nImageTo);
        if (!pImageFrom || !pImageTo)
        {
            rc = VERR_VD_IMAGE_NOT_FOUND;
            break;
        }
        AssertBreakStmt(pImageFrom != pImageTo, rc = VERR_INVALID_PARAMETER);

        /* Make sure destination image is writable. */
        unsigned uOpenFlags = pImageTo->Backend->pfnGetOpenFlags(pImageTo->pBackendData);
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            /*
             * Clear skip consistency checks because the image is made writable now and
             * skipping consistency checks is only possible for readonly images.
             */
            uOpenFlags &= ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS);
            rc = pImageTo->Backend->pfnSetOpenFlags(pImageTo->pBackendData,
                                                    uOpenFlags);
            if (RT_FAILURE(rc))
                break;
        }

        /* Get size of destination image. */
        uint64_t cbSize = vdImageGetSize(pImageTo);
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = false;

        /* Allocate tmp buffer. */
        pvBuf = RTMemTmpAlloc(VD_MERGE_BUFFER_SIZE);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* Merging is done directly on the images itself. This potentially
         * causes trouble if the disk is full in the middle of operation. */
        if (nImageFrom < nImageTo)
        {
            /* Merge parent state into child. This means writing all not
             * allocated blocks in the destination image which are allocated in
             * the images to be merged. */
            uint64_t uOffset = 0;
            uint64_t cbRemaining = cbSize;

            do
            {
                size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);
                RTSGSEG SegmentBuf;
                RTSGBUF SgBuf;
                VDIOCTX IoCtx;

                SegmentBuf.pvSeg = pvBuf;
                SegmentBuf.cbSeg = VD_MERGE_BUFFER_SIZE;
                RTSgBufInit(&SgBuf, &SegmentBuf, 1);
                vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_READ, 0, 0, NULL,
                            &SgBuf, NULL, NULL, VDIOCTX_FLAGS_SYNC);

                /* Need to hold the write lock during a read-write operation. */
                rc2 = vdThreadStartWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = true;

                rc = pImageTo->Backend->pfnRead(pImageTo->pBackendData,
                                                uOffset, cbThisRead,
                                                &IoCtx, &cbThisRead);
                if (rc == VERR_VD_BLOCK_FREE)
                {
                    /* Search for image with allocated block. Do not attempt to
                     * read more than the previous reads marked as valid.
                     * Otherwise this would return stale data when different
                     * block sizes are used for the images. */
                    for (PVDIMAGE pCurrImage = pImageTo->pPrev;
                         pCurrImage != NULL && pCurrImage != pImageFrom->pPrev && rc == VERR_VD_BLOCK_FREE;
                         pCurrImage = pCurrImage->pPrev)
                    {
                        /*
                         * Skip reading when offset exceeds image size which can happen when the target is
                         * bigger than the source.
                         */
                        uint64_t cbImage = vdImageGetSize(pCurrImage);
                        if (uOffset < cbImage)
                        {
                            cbThisRead = RT_MIN(cbThisRead, cbImage - uOffset);
                            rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                                              uOffset, cbThisRead,
                                                              &IoCtx, &cbThisRead);
                        }
                        else
                            rc = VERR_VD_BLOCK_FREE;
                    }

                    if (rc != VERR_VD_BLOCK_FREE)
                    {
                        if (RT_FAILURE(rc))
                            break;
                        /* Updating the cache is required because this might be a live merge. */
                        rc = vdWriteHelperEx(pDisk, pImageTo, pImageFrom->pPrev,
                                             uOffset, pvBuf, cbThisRead,
                                             VDIOCTX_FLAGS_READ_UPDATE_CACHE, 0);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    else
                        rc = VINF_SUCCESS;
                }
                else if (RT_FAILURE(rc))
                    break;

                rc2 = vdThreadFinishWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = false;

                uOffset += cbThisRead;
                cbRemaining -= cbThisRead;

                if (pIfProgress && pIfProgress->pfnProgress)
                {
                    /** @todo r=klaus: this can update the progress to the same
                     * percentage over and over again if the image format makes
                     * relatively small increments. */
                    rc = pIfProgress->pfnProgress(pIfProgress->Core.pvUser,
                                                  uOffset * 99 / cbSize);
                    if (RT_FAILURE(rc))
                        break;
                }
            } while (uOffset < cbSize);
        }
        else
        {
            /*
             * We may need to update the parent uuid of the child coming after
             * the last image to be merged. We have to reopen it read/write.
             *
             * This is done before we do the actual merge to prevent an
             * inconsistent chain if the mode change fails for some reason.
             */
            if (pImageFrom->pNext)
            {
                PVDIMAGE pImageChild = pImageFrom->pNext;

                /* Take the write lock. */
                rc2 = vdThreadStartWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = true;

                /* We need to open the image in read/write mode. */
                uOpenFlags = pImageChild->Backend->pfnGetOpenFlags(pImageChild->pBackendData);

                if (uOpenFlags  & VD_OPEN_FLAGS_READONLY)
                {
                    uOpenFlags  &= ~VD_OPEN_FLAGS_READONLY;
                    rc = pImageChild->Backend->pfnSetOpenFlags(pImageChild->pBackendData,
                                                               uOpenFlags);
                    if (RT_FAILURE(rc))
                        break;
                }

                rc2 = vdThreadFinishWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = false;
            }

            /* If the merge is from the last image we have to relay all writes
             * to the merge destination as well, so that concurrent writes
             * (in case of a live merge) are handled correctly. */
            if (!pImageFrom->pNext)
            {
                /* Take the write lock. */
                rc2 = vdThreadStartWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = true;

                pDisk->pImageRelay = pImageTo;

                rc2 = vdThreadFinishWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = false;
            }

            /* Merge child state into parent. This means writing all blocks
             * which are allocated in the image up to the source image to the
             * destination image. */
            unsigned uProgressOld = 0;
            uint64_t uOffset = 0;
            uint64_t cbRemaining = cbSize;
            do
            {
                size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);
                RTSGSEG SegmentBuf;
                RTSGBUF SgBuf;
                VDIOCTX IoCtx;

                rc = VERR_VD_BLOCK_FREE;

                SegmentBuf.pvSeg = pvBuf;
                SegmentBuf.cbSeg = VD_MERGE_BUFFER_SIZE;
                RTSgBufInit(&SgBuf, &SegmentBuf, 1);
                vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_READ, 0, 0, NULL,
                            &SgBuf, NULL, NULL, VDIOCTX_FLAGS_SYNC);

                /* Need to hold the write lock during a read-write operation. */
                rc2 = vdThreadStartWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = true;

                /* Search for image with allocated block. Do not attempt to
                 * read more than the previous reads marked as valid. Otherwise
                 * this would return stale data when different block sizes are
                 * used for the images. */
                for (PVDIMAGE pCurrImage = pImageFrom;
                     pCurrImage != NULL && pCurrImage != pImageTo && rc == VERR_VD_BLOCK_FREE;
                     pCurrImage = pCurrImage->pPrev)
                {
                    /*
                     * Skip reading when offset exceeds image size which can happen when the target is
                     * bigger than the source.
                     */
                    uint64_t cbImage = vdImageGetSize(pCurrImage);
                    if (uOffset < cbImage)
                    {
                        cbThisRead = RT_MIN(cbThisRead, cbImage - uOffset);
                        rc = pCurrImage->Backend->pfnRead(pCurrImage->pBackendData,
                                                          uOffset, cbThisRead,
                                                          &IoCtx, &cbThisRead);
                    }
                    else
                        rc = VERR_VD_BLOCK_FREE;
                }

                if (rc != VERR_VD_BLOCK_FREE)
                {
                    if (RT_FAILURE(rc))
                        break;
                    rc = vdWriteHelper(pDisk, pImageTo, uOffset, pvBuf,
                                       cbThisRead, VDIOCTX_FLAGS_READ_UPDATE_CACHE);
                    if (RT_FAILURE(rc))
                        break;
                }
                else
                    rc = VINF_SUCCESS;

                rc2 = vdThreadFinishWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = false;

                uOffset += cbThisRead;
                cbRemaining -= cbThisRead;

                unsigned uProgressNew = uOffset * 99 / cbSize;
                if (uProgressNew != uProgressOld)
                {
                    uProgressOld = uProgressNew;

                    if (pIfProgress && pIfProgress->pfnProgress)
                    {
                        rc = pIfProgress->pfnProgress(pIfProgress->Core.pvUser,
                                                      uProgressOld);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }

            } while (uOffset < cbSize);

            /* In case we set up a "write proxy" image above we must clear
             * this again now to prevent stray writes. Failure or not. */
            if (!pImageFrom->pNext)
            {
                /* Take the write lock. */
                rc2 = vdThreadStartWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = true;

                pDisk->pImageRelay = NULL;

                rc2 = vdThreadFinishWrite(pDisk);
                AssertRC(rc2);
                fLockWrite = false;
            }
        }

        /*
         * Leave in case of an error to avoid corrupted data in the image chain
         * (includes cancelling the operation by the user).
         */
        if (RT_FAILURE(rc))
            break;

        /* Need to hold the write lock while finishing the merge. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        /* Update parent UUID so that image chain is consistent.
         * The two attempts work around the problem that some backends
         * (e.g. iSCSI) do not support UUIDs, so we exploit the fact that
         * so far there can only be one such image in the chain. */
        /** @todo needs a better long-term solution, passing the UUID
         * knowledge from the caller or some such */
        RTUUID Uuid;
        PVDIMAGE pImageChild = NULL;
        if (nImageFrom < nImageTo)
        {
            if (pImageFrom->pPrev)
            {
                /* plan A: ask the parent itself for its UUID */
                rc = pImageFrom->pPrev->Backend->pfnGetUuid(pImageFrom->pPrev->pBackendData,
                                                            &Uuid);
                if (RT_FAILURE(rc))
                {
                    /* plan B: ask the child of the parent for parent UUID */
                    rc = pImageFrom->Backend->pfnGetParentUuid(pImageFrom->pBackendData,
                                                               &Uuid);
                }
                AssertRC(rc);
            }
            else
                RTUuidClear(&Uuid);
            rc = pImageTo->Backend->pfnSetParentUuid(pImageTo->pBackendData,
                                                     &Uuid);
            AssertRC(rc);
        }
        else
        {
            /* Update the parent uuid of the child of the last merged image. */
            if (pImageFrom->pNext)
            {
                /* plan A: ask the parent itself for its UUID */
                rc = pImageTo->Backend->pfnGetUuid(pImageTo->pBackendData,
                                                   &Uuid);
                if (RT_FAILURE(rc))
                {
                    /* plan B: ask the child of the parent for parent UUID */
                    rc = pImageTo->pNext->Backend->pfnGetParentUuid(pImageTo->pNext->pBackendData,
                                                                    &Uuid);
                }
                AssertRC(rc);

                rc = pImageFrom->Backend->pfnSetParentUuid(pImageFrom->pNext->pBackendData,
                                                           &Uuid);
                AssertRC(rc);

                pImageChild = pImageFrom->pNext;
            }
        }

        /* Delete the no longer needed images. */
        PVDIMAGE pImg = pImageFrom, pTmp;
        while (pImg != pImageTo)
        {
            if (nImageFrom < nImageTo)
                pTmp = pImg->pNext;
            else
                pTmp = pImg->pPrev;
            vdRemoveImageFromList(pDisk, pImg);
            pImg->Backend->pfnClose(pImg->pBackendData, true);
            RTStrFree(pImg->pszFilename);
            RTMemFree(pImg);
            pImg = pTmp;
        }

        /* Make sure destination image is back to read only if necessary. */
        if (pImageTo != pDisk->pLast)
        {
            uOpenFlags = pImageTo->Backend->pfnGetOpenFlags(pImageTo->pBackendData);
            uOpenFlags |= VD_OPEN_FLAGS_READONLY;
            rc = pImageTo->Backend->pfnSetOpenFlags(pImageTo->pBackendData,
                                                    uOpenFlags);
            if (RT_FAILURE(rc))
                break;
        }

        /*
         * Make sure the child is readonly
         * for the child -> parent merge direction
         * if necessary.
        */
        if (   nImageFrom > nImageTo
            && pImageChild
            && pImageChild != pDisk->pLast)
        {
            uOpenFlags = pImageChild->Backend->pfnGetOpenFlags(pImageChild->pBackendData);
            uOpenFlags |= VD_OPEN_FLAGS_READONLY;
            rc = pImageChild->Backend->pfnSetOpenFlags(pImageChild->pBackendData,
                                                       uOpenFlags);
            if (RT_FAILURE(rc))
                break;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (pvBuf)
        RTMemTmpFree(pvBuf);

    if (RT_SUCCESS(rc) && pIfProgress && pIfProgress->pfnProgress)
        pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCopyEx(PVDISK pDiskFrom, unsigned nImageFrom, PVDISK pDiskTo, unsigned nImageTo,
                           const char *pszBackend, const char *pszFilename,
                           bool fMoveByRename, uint64_t cbSize,
                           unsigned nImageFromSame, unsigned nImageToSame,
                           unsigned uImageFlags, PCRTUUID pDstUuid,
                           unsigned uOpenFlags, PVDINTERFACE pVDIfsOperation,
                           PVDINTERFACE pDstVDIfsImage,
                           PVDINTERFACE pDstVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockReadFrom = false, fLockWriteFrom = false, fLockWriteTo = false;
    PVDIMAGE pImageTo = NULL;

    LogFlowFunc(("pDiskFrom=%#p nImageFrom=%u pDiskTo=%#p nImageTo=%u pszBackend=\"%s\" pszFilename=\"%s\" fMoveByRename=%d cbSize=%llu nImageFromSame=%u nImageToSame=%u uImageFlags=%#x pDstUuid=%#p uOpenFlags=%#x pVDIfsOperation=%#p pDstVDIfsImage=%#p pDstVDIfsOperation=%#p\n",
                 pDiskFrom, nImageFrom, nImageTo, pDiskTo, pszBackend, pszFilename, fMoveByRename, cbSize, nImageFromSame, nImageToSame, uImageFlags, pDstUuid, uOpenFlags, pVDIfsOperation, pDstVDIfsImage, pDstVDIfsOperation));

    /* Check arguments. */
    AssertReturn(pDiskFrom, VERR_INVALID_POINTER);
    AssertMsg(pDiskFrom->u32Signature == VDISK_SIGNATURE,
              ("u32Signature=%08x\n", pDiskFrom->u32Signature));

    PVDINTERFACEPROGRESS pIfProgress    = VDIfProgressGet(pVDIfsOperation);
    PVDINTERFACEPROGRESS pDstIfProgress = VDIfProgressGet(pDstVDIfsOperation);

    do {
        rc2 = vdThreadStartRead(pDiskFrom);
        AssertRC(rc2);
        fLockReadFrom = true;
        PVDIMAGE pImageFrom = vdGetImageByNumber(pDiskFrom, nImageFrom);
        AssertPtrBreakStmt(pImageFrom, rc = VERR_VD_IMAGE_NOT_FOUND);
        AssertPtrBreakStmt(pDiskTo, rc = VERR_INVALID_POINTER);
        AssertMsg(pDiskTo->u32Signature == VDISK_SIGNATURE,
                  ("u32Signature=%08x\n", pDiskTo->u32Signature));
        AssertMsgBreakStmt(   (nImageFromSame < nImageFrom || nImageFromSame == VD_IMAGE_CONTENT_UNKNOWN)
                           && (nImageToSame < pDiskTo->cImages || nImageToSame == VD_IMAGE_CONTENT_UNKNOWN)
                           && (   (nImageFromSame == VD_IMAGE_CONTENT_UNKNOWN && nImageToSame == VD_IMAGE_CONTENT_UNKNOWN)
                               || (nImageFromSame != VD_IMAGE_CONTENT_UNKNOWN && nImageToSame != VD_IMAGE_CONTENT_UNKNOWN)),
                           ("nImageFromSame=%u nImageToSame=%u\n", nImageFromSame, nImageToSame),
                           rc = VERR_INVALID_PARAMETER);

        /* Move the image. */
        if (fMoveByRename)
        {
            /* Rename only works when backends are the same, are file based
             * and the rename method is implemented. */
            if (    fMoveByRename
                &&  !RTStrICmp(pszBackend, pImageFrom->Backend->pszBackendName)
                &&  pImageFrom->Backend->uBackendCaps & VD_CAP_FILE
                &&  pImageFrom->Backend->pfnRename)
            {
                rc2 = vdThreadFinishRead(pDiskFrom);
                AssertRC(rc2);
                fLockReadFrom = false;

                rc2 = vdThreadStartWrite(pDiskFrom);
                AssertRC(rc2);
                fLockWriteFrom = true;
                rc = pImageFrom->Backend->pfnRename(pImageFrom->pBackendData, pszFilename ? pszFilename : pImageFrom->pszFilename);
                break;
            }

            /** @todo Moving (including shrinking/growing) of the image is
             * requested, but the rename attempt failed or it wasn't possible.
             * Must now copy image to temp location. */
            AssertReleaseMsgFailed(("VDCopy: moving by copy/delete not implemented\n"));
        }

        /* pszFilename is allowed to be NULL, as this indicates copy to the existing image. */
        if (pszFilename)
        {
            AssertPtrBreakStmt(pszFilename, rc = VERR_INVALID_POINTER);
            AssertBreakStmt(*pszFilename != '\0', rc = VERR_INVALID_PARAMETER);
        }

        uint64_t cbSizeFrom;
        cbSizeFrom = vdImageGetSize(pImageFrom);
        if (cbSizeFrom == 0)
        {
            rc = VERR_VD_VALUE_NOT_FOUND;
            break;
        }

        VDGEOMETRY PCHSGeometryFrom = {0, 0, 0};
        VDGEOMETRY LCHSGeometryFrom = {0, 0, 0};
        pImageFrom->Backend->pfnGetPCHSGeometry(pImageFrom->pBackendData, &PCHSGeometryFrom);
        pImageFrom->Backend->pfnGetLCHSGeometry(pImageFrom->pBackendData, &LCHSGeometryFrom);

        RTUUID ImageUuid, ImageModificationUuid;
        if (pDiskFrom != pDiskTo)
        {
            if (pDstUuid)
                ImageUuid = *pDstUuid;
            else
                RTUuidCreate(&ImageUuid);
        }
        else
        {
            rc = pImageFrom->Backend->pfnGetUuid(pImageFrom->pBackendData, &ImageUuid);
            if (RT_FAILURE(rc))
                RTUuidCreate(&ImageUuid);
        }
        rc = pImageFrom->Backend->pfnGetModificationUuid(pImageFrom->pBackendData, &ImageModificationUuid);
        if (RT_FAILURE(rc))
            RTUuidClear(&ImageModificationUuid);

        char szComment[1024];
        rc = pImageFrom->Backend->pfnGetComment(pImageFrom->pBackendData, szComment, sizeof(szComment));
        if (RT_FAILURE(rc))
            szComment[0] = '\0';
        else
            szComment[sizeof(szComment) - 1] = '\0';

        rc2 = vdThreadFinishRead(pDiskFrom);
        AssertRC(rc2);
        fLockReadFrom = false;

        rc2 = vdThreadStartRead(pDiskTo);
        AssertRC(rc2);
        unsigned cImagesTo = pDiskTo->cImages;
        rc2 = vdThreadFinishRead(pDiskTo);
        AssertRC(rc2);

        if (pszFilename)
        {
            if (cbSize == 0)
                cbSize = cbSizeFrom;

            /* Create destination image with the properties of source image. */
            /** @todo replace the VDCreateDiff/VDCreateBase calls by direct
             * calls to the backend. Unifies the code and reduces the API
             * dependencies. Would also make the synchronization explicit. */
            if (cImagesTo > 0)
            {
                rc = VDCreateDiff(pDiskTo, pszBackend, pszFilename,
                                  uImageFlags, szComment, &ImageUuid,
                                  NULL /* pParentUuid */,
                                  uOpenFlags & ~VD_OPEN_FLAGS_READONLY,
                                  pDstVDIfsImage, NULL);

                rc2 = vdThreadStartWrite(pDiskTo);
                AssertRC(rc2);
                fLockWriteTo = true;
            } else {
                /** @todo hack to force creation of a fixed image for
                 * the RAW backend, which can't handle anything else. */
                if (!RTStrICmp(pszBackend, "RAW"))
                    uImageFlags |= VD_IMAGE_FLAGS_FIXED;

                vdFixupPCHSGeometry(&PCHSGeometryFrom, cbSize);
                vdFixupLCHSGeometry(&LCHSGeometryFrom, cbSize);

                rc = VDCreateBase(pDiskTo, pszBackend, pszFilename, cbSize,
                                  uImageFlags, szComment,
                                  &PCHSGeometryFrom, &LCHSGeometryFrom,
                                  NULL, uOpenFlags & ~VD_OPEN_FLAGS_READONLY,
                                  pDstVDIfsImage, NULL);

                rc2 = vdThreadStartWrite(pDiskTo);
                AssertRC(rc2);
                fLockWriteTo = true;

                pImageTo = vdGetImageByNumber(pDiskTo, nImageTo);
                AssertPtrBreakStmt(pImageTo, rc = VERR_VD_IMAGE_NOT_FOUND);
                if (RT_SUCCESS(rc) && !RTUuidIsNull(&ImageUuid))
                     pImageTo->Backend->pfnSetUuid(pImageTo->pBackendData, &ImageUuid);
            }
            if (RT_FAILURE(rc))
                break;

            pImageTo = vdGetImageByNumber(pDiskTo, nImageTo);
            AssertPtrBreakStmt(pImageTo, rc = VERR_VD_IMAGE_NOT_FOUND);

            cbSize = RT_MIN(cbSize, cbSizeFrom);
        }
        else
        {
            pImageTo = vdGetImageByNumber(pDiskTo, nImageTo);
            AssertPtrBreakStmt(pImageTo, rc = VERR_VD_IMAGE_NOT_FOUND);

            uint64_t cbSizeTo;
            cbSizeTo = vdImageGetSize(pImageTo);
            if (cbSizeTo == 0)
            {
                rc = VERR_VD_VALUE_NOT_FOUND;
                break;
            }

            if (cbSize == 0)
                cbSize = RT_MIN(cbSizeFrom, cbSizeTo);

            vdFixupPCHSGeometry(&PCHSGeometryFrom, cbSize);
            vdFixupLCHSGeometry(&LCHSGeometryFrom, cbSize);

            /* Update the geometry in the destination image. */
            pImageTo->Backend->pfnSetPCHSGeometry(pImageTo->pBackendData, &PCHSGeometryFrom);
            pImageTo->Backend->pfnSetLCHSGeometry(pImageTo->pBackendData, &LCHSGeometryFrom);
        }

        rc2 = vdThreadFinishWrite(pDiskTo);
        AssertRC(rc2);
        fLockWriteTo = false;

        /* Whether we can take the optimized copy path (false) or not.
         * Don't optimize if the image existed or if it is a child image. */
        bool fSuppressRedundantIo = (   !(pszFilename == NULL || cImagesTo > 0)
                                     || (nImageToSame != VD_IMAGE_CONTENT_UNKNOWN)
                                     || (pDiskTo == pDiskFrom));
        unsigned cImagesFromReadBack, cImagesToReadBack;

        if (nImageFromSame == VD_IMAGE_CONTENT_UNKNOWN)
            cImagesFromReadBack = 0;
        else
        {
            if (nImageFrom == VD_LAST_IMAGE)
                cImagesFromReadBack = pDiskFrom->cImages - nImageFromSame - 1;
            else
                cImagesFromReadBack = nImageFrom - nImageFromSame;
        }

        if (nImageToSame == VD_IMAGE_CONTENT_UNKNOWN)
            cImagesToReadBack = 0;
        else
            cImagesToReadBack = pDiskTo->cImages - nImageToSame - 1;

        /* Copy the data. */
        rc = vdCopyHelper(pDiskFrom, pImageFrom, pDiskTo, pImageTo, cbSize,
                          cImagesFromReadBack, cImagesToReadBack,
                          fSuppressRedundantIo, pIfProgress, pDstIfProgress);

        if (RT_SUCCESS(rc))
        {
            rc2 = vdThreadStartWrite(pDiskTo);
            AssertRC(rc2);
            fLockWriteTo = true;

            /* Only set modification UUID if it is non-null, since the source
             * backend might not provide a valid modification UUID. */
            if (!RTUuidIsNull(&ImageModificationUuid))
                pImageTo->Backend->pfnSetModificationUuid(pImageTo->pBackendData, &ImageModificationUuid);

            /* Set the requested open flags if they differ from the value
             * required for creating the image and copying the contents. */
            if (   pImageTo && pszFilename
                && uOpenFlags != (uOpenFlags & ~VD_OPEN_FLAGS_READONLY))
                rc = pImageTo->Backend->pfnSetOpenFlags(pImageTo->pBackendData,
                                                        uOpenFlags);
        }
    } while (0);

    if (RT_FAILURE(rc) && pImageTo && pszFilename)
    {
        /* Take the write lock only if it is not taken. Not worth making the
         * above code even more complicated. */
        if (RT_UNLIKELY(!fLockWriteTo))
        {
            rc2 = vdThreadStartWrite(pDiskTo);
            AssertRC(rc2);
            fLockWriteTo = true;
        }
        /* Error detected, but new image created. Remove image from list. */
        vdRemoveImageFromList(pDiskTo, pImageTo);

        /* Close and delete image. */
        rc2 = pImageTo->Backend->pfnClose(pImageTo->pBackendData, true);
        AssertRC(rc2);
        pImageTo->pBackendData = NULL;

        /* Free remaining resources. */
        if (pImageTo->pszFilename)
            RTStrFree(pImageTo->pszFilename);

        RTMemFree(pImageTo);
    }

    if (RT_UNLIKELY(fLockWriteTo))
    {
        rc2 = vdThreadFinishWrite(pDiskTo);
        AssertRC(rc2);
    }
    if (RT_UNLIKELY(fLockWriteFrom))
    {
        rc2 = vdThreadFinishWrite(pDiskFrom);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockReadFrom))
    {
        rc2 = vdThreadFinishRead(pDiskFrom);
        AssertRC(rc2);
    }

    if (RT_SUCCESS(rc))
    {
        if (pIfProgress && pIfProgress->pfnProgress)
            pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);
        if (pDstIfProgress && pDstIfProgress->pfnProgress)
            pDstIfProgress->pfnProgress(pDstIfProgress->Core.pvUser, 100);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCopy(PVDISK pDiskFrom, unsigned nImage, PVDISK pDiskTo,
                         const char *pszBackend, const char *pszFilename,
                         bool fMoveByRename, uint64_t cbSize,
                         unsigned uImageFlags, PCRTUUID pDstUuid,
                         unsigned uOpenFlags, PVDINTERFACE pVDIfsOperation,
                         PVDINTERFACE pDstVDIfsImage,
                         PVDINTERFACE pDstVDIfsOperation)
{
    return VDCopyEx(pDiskFrom, nImage, pDiskTo, VD_IMAGE_CONTENT_UNKNOWN, pszBackend, pszFilename, fMoveByRename,
                    cbSize, VD_IMAGE_CONTENT_UNKNOWN, VD_IMAGE_CONTENT_UNKNOWN,
                    uImageFlags, pDstUuid, uOpenFlags, pVDIfsOperation,
                    pDstVDIfsImage, pDstVDIfsOperation);
}


VBOXDDU_DECL(int) VDCompact(PVDISK pDisk, unsigned nImage,
                            PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockRead = false, fLockWrite = false;
    void *pvBuf = NULL;
    void *pvTmp = NULL;

    LogFlowFunc(("pDisk=%#p nImage=%u pVDIfsOperation=%#p\n",
                 pDisk, nImage, pVDIfsOperation));
    /* Check arguments. */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE,
              ("u32Signature=%08x\n", pDisk->u32Signature));

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do {
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VD_IMAGE_NOT_FOUND);

        /* If there is no compact callback for not file based backends then
         * the backend doesn't need compaction. No need to make much fuss about
         * this. For file based ones signal this as not yet supported. */
        if (!pImage->Backend->pfnCompact)
        {
            if (pImage->Backend->uBackendCaps & VD_CAP_FILE)
                rc = VERR_NOT_SUPPORTED;
            else
                rc = VINF_SUCCESS;
            break;
        }

        /* Insert interface for reading parent state into per-operation list,
         * if there is a parent image. */
        VDINTERFACEPARENTSTATE VDIfParent;
        VDPARENTSTATEDESC ParentUser;
        if (pImage->pPrev)
        {
            VDIfParent.pfnParentRead = vdParentRead;
            ParentUser.pDisk = pDisk;
            ParentUser.pImage = pImage->pPrev;
            rc = VDInterfaceAdd(&VDIfParent.Core, "VDCompact_ParentState", VDINTERFACETYPE_PARENTSTATE,
                                &ParentUser, sizeof(VDINTERFACEPARENTSTATE), &pVDIfsOperation);
            AssertRC(rc);
        }

        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        rc = pImage->Backend->pfnCompact(pImage->pBackendData,
                                         0, 99,
                                         pDisk->pVDIfsDisk,
                                         pImage->pVDIfsImage,
                                         pVDIfsOperation);
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (pvBuf)
        RTMemTmpFree(pvBuf);
    if (pvTmp)
        RTMemTmpFree(pvTmp);

    if (RT_SUCCESS(rc))
    {
        if (pIfProgress && pIfProgress->pfnProgress)
            pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDResize(PVDISK pDisk, uint64_t cbSize,
                           PCVDGEOMETRY pPCHSGeometry,
                           PCVDGEOMETRY pLCHSGeometry,
                           PVDINTERFACE pVDIfsOperation)
{
    /** @todo r=klaus resizing was designed to be part of VDCopy, so having a separate function is not desirable. */
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockRead = false, fLockWrite = false;

    LogFlowFunc(("pDisk=%#p cbSize=%llu pVDIfsOperation=%#p\n",
                 pDisk, cbSize, pVDIfsOperation));
    /* Check arguments. */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE,
              ("u32Signature=%08x\n", pDisk->u32Signature));

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do {
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;

        /* Must have at least one image in the chain, will resize last. */
        AssertMsgBreakStmt(pDisk->cImages >= 1, ("cImages=%u\n", pDisk->cImages),
                           rc = VERR_NOT_SUPPORTED);

        PVDIMAGE pImage = pDisk->pLast;

        /* If there is no compact callback for not file based backends then
         * the backend doesn't need compaction. No need to make much fuss about
         * this. For file based ones signal this as not yet supported. */
        if (!pImage->Backend->pfnResize)
        {
            if (pImage->Backend->uBackendCaps & VD_CAP_FILE)
                rc = VERR_NOT_SUPPORTED;
            else
                rc = VINF_SUCCESS;
            break;
        }

        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        VDGEOMETRY PCHSGeometryOld;
        VDGEOMETRY LCHSGeometryOld;
        PCVDGEOMETRY pPCHSGeometryNew;
        PCVDGEOMETRY pLCHSGeometryNew;

        if (pPCHSGeometry->cCylinders == 0)
        {
            /* Auto-detect marker, calculate new value ourself. */
            rc = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData, &PCHSGeometryOld);
            if (RT_SUCCESS(rc) && (PCHSGeometryOld.cCylinders != 0))
                PCHSGeometryOld.cCylinders = RT_MIN(cbSize / 512 / PCHSGeometryOld.cHeads / PCHSGeometryOld.cSectors, 16383);
            else if (rc == VERR_VD_GEOMETRY_NOT_SET)
                rc = VINF_SUCCESS;

            pPCHSGeometryNew = &PCHSGeometryOld;
        }
        else
            pPCHSGeometryNew = pPCHSGeometry;

        if (pLCHSGeometry->cCylinders == 0)
        {
            /* Auto-detect marker, calculate new value ourself. */
            rc = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData, &LCHSGeometryOld);
            if (RT_SUCCESS(rc) && (LCHSGeometryOld.cCylinders != 0))
                LCHSGeometryOld.cCylinders = cbSize / 512 / LCHSGeometryOld.cHeads / LCHSGeometryOld.cSectors;
            else if (rc == VERR_VD_GEOMETRY_NOT_SET)
                rc = VINF_SUCCESS;

            pLCHSGeometryNew = &LCHSGeometryOld;
        }
        else
            pLCHSGeometryNew = pLCHSGeometry;

        if (RT_SUCCESS(rc))
            rc = pImage->Backend->pfnResize(pImage->pBackendData,
                                            cbSize,
                                            pPCHSGeometryNew,
                                            pLCHSGeometryNew,
                                            0, 99,
                                            pDisk->pVDIfsDisk,
                                            pImage->pVDIfsImage,
                                            pVDIfsOperation);
        /* Mark the image size as uninitialized so it gets recalculated the next time. */
        if (RT_SUCCESS(rc))
            pImage->cbImage = VD_IMAGE_SIZE_UNINITIALIZED;
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (RT_SUCCESS(rc))
    {
        if (pIfProgress && pIfProgress->pfnProgress)
            pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

        pDisk->cbSize = cbSize;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

VBOXDDU_DECL(int) VDPrepareWithFilters(PVDISK pDisk, PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockRead = false, fLockWrite = false;

    LogFlowFunc(("pDisk=%#p pVDIfsOperation=%#p\n", pDisk, pVDIfsOperation));
    /* Check arguments. */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE,
              ("u32Signature=%08x\n", pDisk->u32Signature));

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    do {
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;

        /* Must have at least one image in the chain. */
        AssertMsgBreakStmt(pDisk->cImages >= 1, ("cImages=%u\n", pDisk->cImages),
                           rc = VERR_VD_NOT_OPENED);

        unsigned uOpenFlags = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pBackendData);
        AssertMsgBreakStmt(!(uOpenFlags & VD_OPEN_FLAGS_READONLY),
                           ("Last image should be read write"),
                           rc = VERR_VD_IMAGE_READ_ONLY);

        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
        fLockRead = false;

        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        /*
         * Open all images in the chain in read write mode first to avoid running
         * into an error in the middle of the process.
         */
        PVDIMAGE pImage = pDisk->pBase;

        while (pImage)
        {
            uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pBackendData);
            if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
            {
                /*
                 * Clear skip consistency checks because the image is made writable now and
                 * skipping consistency checks is only possible for readonly images.
                 */
                uOpenFlags &= ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS);
                rc = pImage->Backend->pfnSetOpenFlags(pImage->pBackendData, uOpenFlags);
                if (RT_FAILURE(rc))
                    break;
            }
            pImage = pImage->pNext;
        }

        if (RT_SUCCESS(rc))
        {
            unsigned cImgCur = 0;
            unsigned uPercentStart = 0;
            unsigned uPercentSpan = 100 / pDisk->cImages - 1;

            /* Allocate tmp buffer. */
            void *pvBuf = RTMemTmpAlloc(VD_MERGE_BUFFER_SIZE);
            if (!pvBuf)
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            pImage = pDisk->pBase;
            pDisk->fLocked = true;

            while (   pImage
                   && RT_SUCCESS(rc))
            {
                /* Get size of image. */
                uint64_t cbSize = vdImageGetSize(pImage);
                uint64_t cbSizeFile = pImage->Backend->pfnGetFileSize(pImage->pBackendData);
                uint64_t cbFileWritten = 0;
                uint64_t uOffset = 0;
                uint64_t cbRemaining = cbSize;

                do
                {
                    size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);
                    RTSGSEG SegmentBuf;
                    RTSGBUF SgBuf;
                    VDIOCTX IoCtx;

                    SegmentBuf.pvSeg = pvBuf;
                    SegmentBuf.cbSeg = VD_MERGE_BUFFER_SIZE;
                    RTSgBufInit(&SgBuf, &SegmentBuf, 1);
                    vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_READ, 0, 0, NULL,
                                &SgBuf, NULL, NULL, VDIOCTX_FLAGS_SYNC);

                    rc = pImage->Backend->pfnRead(pImage->pBackendData, uOffset,
                                                  cbThisRead, &IoCtx, &cbThisRead);
                    if (rc != VERR_VD_BLOCK_FREE)
                    {
                        if (RT_FAILURE(rc))
                            break;

                        /* Apply filter chains. */
                        rc = vdFilterChainApplyRead(pDisk, uOffset, cbThisRead, &IoCtx);
                        if (RT_FAILURE(rc))
                            break;

                        rc = vdFilterChainApplyWrite(pDisk, uOffset, cbThisRead, &IoCtx);
                        if (RT_FAILURE(rc))
                            break;

                        RTSgBufReset(&SgBuf);
                        size_t cbThisWrite = 0;
                        size_t cbPreRead = 0;
                        size_t cbPostRead = 0;
                        rc = pImage->Backend->pfnWrite(pImage->pBackendData, uOffset,
                                                       cbThisRead, &IoCtx, &cbThisWrite,
                                                       &cbPreRead, &cbPostRead, 0);
                        if (RT_FAILURE(rc))
                            break;
                        Assert(cbThisWrite == cbThisRead);
                        cbFileWritten += cbThisWrite;
                    }
                    else
                        rc = VINF_SUCCESS;

                    uOffset += cbThisRead;
                    cbRemaining -= cbThisRead;

                    if (pIfProgress && pIfProgress->pfnProgress)
                    {
                        rc2 = pIfProgress->pfnProgress(pIfProgress->Core.pvUser,
                                                       uPercentStart + cbFileWritten * uPercentSpan / cbSizeFile);
                        AssertRC(rc2); /* Cancelling this operation without leaving an inconsistent state is not possible. */
                    }
                } while (uOffset < cbSize);

                pImage = pImage->pNext;
                cImgCur++;
                uPercentStart += uPercentSpan;
            }

            pDisk->fLocked = false;
            if (pvBuf)
                RTMemTmpFree(pvBuf);
        }

        /* Change images except last one back to readonly. */
        pImage = pDisk->pBase;
        while (   pImage != pDisk->pLast
               && pImage)
        {
            uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pBackendData);
            uOpenFlags |= VD_OPEN_FLAGS_READONLY;
            rc2 = pImage->Backend->pfnSetOpenFlags(pImage->pBackendData, uOpenFlags);
            if (RT_FAILURE(rc2))
            {
                if (RT_SUCCESS(rc))
                    rc = rc2;
                break;
            }
            pImage = pImage->pNext;
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }
    else if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    if (   RT_SUCCESS(rc)
        && pIfProgress
        && pIfProgress->pfnProgress)
        pIfProgress->pfnProgress(pIfProgress->Core.pvUser, 100);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDClose(PVDISK pDisk, bool fDelete)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;

    LogFlowFunc(("pDisk=%#p fDelete=%d\n", pDisk, fDelete));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Not worth splitting this up into a read lock phase and write
         * lock phase, as closing an image is a relatively fast operation
         * dominated by the part which needs the write lock. */
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        PVDIMAGE pImage = pDisk->pLast;
        if (!pImage)
        {
            rc = VERR_VD_NOT_OPENED;
            break;
        }

        /* Destroy the current discard state first which might still have pending blocks. */
        rc = vdDiscardStateDestroy(pDisk);
        if (RT_FAILURE(rc))
            break;

        unsigned uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pBackendData);
        /* Remove image from list of opened images. */
        vdRemoveImageFromList(pDisk, pImage);
        /* Close (and optionally delete) image. */
        rc = pImage->Backend->pfnClose(pImage->pBackendData, fDelete);
        /* Free remaining resources related to the image. */
        RTStrFree(pImage->pszFilename);
        RTMemFree(pImage);

        pImage = pDisk->pLast;
        if (!pImage)
            break;

        /* If disk was previously in read/write mode, make sure it will stay
         * like this (if possible) after closing this image. Set the open flags
         * accordingly. */
        if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pBackendData);
            uOpenFlags &= ~ VD_OPEN_FLAGS_READONLY;
            rc = pImage->Backend->pfnSetOpenFlags(pImage->pBackendData, uOpenFlags);
        }

        /* Cache disk information. */
        pDisk->cbSize = vdImageGetSize(pImage);

        /* Cache PCHS geometry. */
        rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData,
                                                 &pDisk->PCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads = 0;
            pDisk->PCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the PCHS geometry is properly clipped. */
            pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
            pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
            pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
        }

        /* Cache LCHS geometry. */
        rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData,
                                                  &pDisk->LCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads = 0;
            pDisk->LCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the LCHS geometry is properly clipped. */
            pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
            pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
        }
    } while (0);

    if (RT_UNLIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCacheClose(PVDISK pDisk, bool fDelete)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;
    PVDCACHE pCache = NULL;

    LogFlowFunc(("pDisk=%#p fDelete=%d\n", pDisk, fDelete));

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        AssertPtrBreakStmt(pDisk->pCache, rc = VERR_VD_CACHE_NOT_FOUND);

        pCache = pDisk->pCache;
        pDisk->pCache = NULL;

        pCache->Backend->pfnClose(pCache->pBackendData, fDelete);
        if (pCache->pszFilename)
            RTStrFree(pCache->pszFilename);
        RTMemFree(pCache);
    } while (0);

    if (RT_LIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

VBOXDDU_DECL(int) VDFilterRemove(PVDISK pDisk, uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockWrite = false;
    PVDFILTER pFilter = NULL;

    LogFlowFunc(("pDisk=%#p\n", pDisk));

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        AssertMsgBreakStmt(!(fFlags & ~VD_FILTER_FLAGS_MASK),
                           ("Invalid flags set (fFlags=%#x)\n", fFlags),
                           rc = VERR_INVALID_PARAMETER);

        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);
        fLockWrite = true;

        if (fFlags & VD_FILTER_FLAGS_WRITE)
        {
            AssertBreakStmt(!RTListIsEmpty(&pDisk->ListFilterChainWrite), rc = VERR_VD_NOT_OPENED);
            pFilter = RTListGetLast(&pDisk->ListFilterChainWrite, VDFILTER, ListNodeChainWrite);
            AssertPtr(pFilter);
            RTListNodeRemove(&pFilter->ListNodeChainWrite);
            vdFilterRelease(pFilter);
        }

        if (fFlags & VD_FILTER_FLAGS_READ)
        {
            AssertBreakStmt(!RTListIsEmpty(&pDisk->ListFilterChainRead), rc = VERR_VD_NOT_OPENED);
            pFilter = RTListGetLast(&pDisk->ListFilterChainRead, VDFILTER, ListNodeChainRead);
            AssertPtr(pFilter);
            RTListNodeRemove(&pFilter->ListNodeChainRead);
            vdFilterRelease(pFilter);
        }
    } while (0);

    if (RT_LIKELY(fLockWrite))
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDCloseAll(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Lock the entire operation. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    PVDCACHE pCache = pDisk->pCache;
    if (pCache)
    {
        rc2 = pCache->Backend->pfnClose(pCache->pBackendData, false);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;

        if (pCache->pszFilename)
            RTStrFree(pCache->pszFilename);
        RTMemFree(pCache);
    }

    PVDIMAGE pImage = pDisk->pLast;
    while (RT_VALID_PTR(pImage))
    {
        PVDIMAGE pPrev = pImage->pPrev;
        /* Remove image from list of opened images. */
        vdRemoveImageFromList(pDisk, pImage);
        /* Close image. */
        rc2 = pImage->Backend->pfnClose(pImage->pBackendData, false);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
        /* Free remaining resources related to the image. */
        RTStrFree(pImage->pszFilename);
        RTMemFree(pImage);
        pImage = pPrev;
    }
    Assert(!RT_VALID_PTR(pDisk->pLast));

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDFilterRemoveAll(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Lock the entire operation. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    PVDFILTER pFilter, pFilterNext;
    RTListForEachSafe(&pDisk->ListFilterChainWrite, pFilter, pFilterNext, VDFILTER, ListNodeChainWrite)
    {
        RTListNodeRemove(&pFilter->ListNodeChainWrite);
        vdFilterRelease(pFilter);
    }

    RTListForEachSafe(&pDisk->ListFilterChainRead, pFilter, pFilterNext, VDFILTER, ListNodeChainRead)
    {
        RTListNodeRemove(&pFilter->ListNodeChainRead);
        vdFilterRelease(pFilter);
    }
    Assert(RTListIsEmpty(&pDisk->ListFilterChainRead));
    Assert(RTListIsEmpty(&pDisk->ListFilterChainWrite));

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}


VBOXDDU_DECL(int) VDRead(PVDISK pDisk, uint64_t uOffset, void *pvBuf,
                         size_t cbRead)
{
    int rc = VINF_SUCCESS;
    int rc2;
    bool fLockRead = false;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pvBuf=%p cbRead=%zu\n",
                 pDisk, uOffset, pvBuf, cbRead));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);

    do
    {
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);
        fLockRead = true;

        AssertMsgBreakStmt(   uOffset < pDisk->cbSize
                           && cbRead <= pDisk->cbSize - uOffset,
                           ("uOffset=%llu cbRead=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbRead, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VD_NOT_OPENED);

        if (uOffset + cbRead > pDisk->cbSize)
        {
            /* Floppy images might be smaller than the standard expected by
               the floppy controller code.  So, we won't fail here. */
            AssertMsgBreakStmt(pDisk->enmType == VDTYPE_FLOPPY,
                               ("uOffset=%llu cbRead=%zu pDisk->cbSize=%llu\n",
                                uOffset, cbRead, pDisk->cbSize),
                               rc = VERR_EOF);
            memset(pvBuf, 0xf6, cbRead); /* f6h = format.com filler byte */
            if (uOffset >= pDisk->cbSize)
                break;
            cbRead = pDisk->cbSize - uOffset;
        }

        rc = vdReadHelper(pDisk, pImage, uOffset, pvBuf, cbRead,
                          true /* fUpdateCache */);
    } while (0);

    if (RT_UNLIKELY(fLockRead))
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDWrite(PVDISK pDisk, uint64_t uOffset, const void *pvBuf,
                          size_t cbWrite)
{
    int rc = VINF_SUCCESS;
    int rc2;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pvBuf=%p cbWrite=%zu\n",
                 pDisk, uOffset, pvBuf, cbWrite));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbWrite > 0, VERR_INVALID_PARAMETER);

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        AssertMsgBreakStmt(   uOffset < pDisk->cbSize
                           && cbWrite <= pDisk->cbSize - uOffset,
                           ("uOffset=%llu cbWrite=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbWrite, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VD_NOT_OPENED);

        vdSetModifiedFlag(pDisk);
        rc = vdWriteHelper(pDisk, pImage, uOffset, pvBuf, cbWrite,
                           VDIOCTX_FLAGS_READ_UPDATE_CACHE);
        if (RT_FAILURE(rc))
            break;

        /* If there is a merge (in the direction towards a parent) running
         * concurrently then we have to also "relay" the write to this parent,
         * as the merge position might be already past the position where
         * this write is going. The "context" of the write can come from the
         * natural chain, since merging either already did or will take care
         * of the "other" content which is might be needed to fill the block
         * to a full allocation size. The cache doesn't need to be touched
         * as this write is covered by the previous one. */
        if (RT_UNLIKELY(pDisk->pImageRelay))
            rc = vdWriteHelper(pDisk, pDisk->pImageRelay, uOffset,
                               pvBuf, cbWrite, VDIOCTX_FLAGS_DEFAULT);
    } while (0);

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDFlush(PVDISK pDisk)
{
    int rc = VINF_SUCCESS;
    int rc2;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VD_NOT_OPENED);

        VDIOCTX IoCtx;
        RTSEMEVENT hEventComplete = NIL_RTSEMEVENT;

        rc = RTSemEventCreate(&hEventComplete);
        if (RT_FAILURE(rc))
            break;

        vdIoCtxInit(&IoCtx, pDisk, VDIOCTXTXDIR_FLUSH, 0, 0, pImage, NULL,
                    NULL, vdFlushHelperAsync, VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_DONT_FREE);

        IoCtx.Type.Root.pfnComplete = vdIoCtxSyncComplete;
        IoCtx.Type.Root.pvUser1     = pDisk;
        IoCtx.Type.Root.pvUser2     = hEventComplete;
        rc = vdIoCtxProcessSync(&IoCtx, hEventComplete);

        RTSemEventDestroy(hEventComplete);
    } while (0);

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(unsigned) VDGetCount(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));

    /* sanity check */
    AssertPtrReturn(pDisk, 0);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    unsigned cImages = pDisk->cImages;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %u\n", cImages));
    return cImages;
}


VBOXDDU_DECL(bool) VDIsReadOnly(PVDISK pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, true);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    bool fReadOnly = true;
    PVDIMAGE pImage = pDisk->pLast;
    AssertPtr(pImage);
    if (pImage)
    {
        unsigned uOpenFlags = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pBackendData);
        fReadOnly = !!(uOpenFlags & VD_OPEN_FLAGS_READONLY);
    }

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %d\n", fReadOnly));
    return fReadOnly;
}


VBOXDDU_DECL(uint32_t) VDGetSectorSize(PVDISK pDisk, unsigned nImage)
{
    LogFlowFunc(("pDisk=%#p nImage=%u\n", pDisk, nImage));
    /* sanity check */
    AssertPtrReturn(pDisk, 0);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    uint64_t cbSector = 0;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
    {
        PCVDREGIONLIST pRegionList = NULL;
        int rc = pImage->Backend->pfnQueryRegions(pImage->pBackendData, &pRegionList);
        if (RT_SUCCESS(rc))
        {
            AssertMsg(pRegionList->cRegions == 1, ("%u\n", pRegionList->cRegions));
            if (pRegionList->cRegions == 1)
            {
                cbSector = pRegionList->aRegions[0].cbBlock;

                AssertPtr(pImage->Backend->pfnRegionListRelease);
                pImage->Backend->pfnRegionListRelease(pImage->pBackendData, pRegionList);
            }
        }
    }

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %u\n", cbSector));
    return cbSector;
}


VBOXDDU_DECL(uint64_t) VDGetSize(PVDISK pDisk, unsigned nImage)
{
    LogFlowFunc(("pDisk=%#p nImage=%u\n", pDisk, nImage));
    /* sanity check */
    AssertPtrReturn(pDisk, 0);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    uint64_t cbSize;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        cbSize = vdImageGetSize(pImage);
    else
        cbSize = 0;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %llu (%#RX64)\n", cbSize, cbSize));
    return cbSize;
}


VBOXDDU_DECL(uint64_t) VDGetFileSize(PVDISK pDisk, unsigned nImage)
{
    LogFlowFunc(("pDisk=%#p nImage=%u\n", pDisk, nImage));

    /* sanity check */
    AssertPtrReturn(pDisk, 0);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    uint64_t cbSize = 0;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        cbSize = pImage->Backend->pfnGetFileSize(pImage->pBackendData);

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %llu (%#RX64)\n", cbSize, cbSize));
    return cbSize;
}


VBOXDDU_DECL(int) VDGetPCHSGeometry(PVDISK pDisk, unsigned nImage,
                                    PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pPCHSGeometry=%#p\n",
                 pDisk, nImage, pPCHSGeometry));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
    {
        if (pImage == pDisk->pLast)
        {
            /* Use cached information if possible. */
            if (pDisk->PCHSGeometry.cCylinders != 0)
            {
                *pPCHSGeometry = pDisk->PCHSGeometry;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_VD_GEOMETRY_NOT_SET;
        }
        else
            rc = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData, pPCHSGeometry);
    }
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("%Rrc (PCHS=%u/%u/%u)\n", rc,
                 pDisk->PCHSGeometry.cCylinders, pDisk->PCHSGeometry.cHeads,
                 pDisk->PCHSGeometry.cSectors));
    return rc;
}


VBOXDDU_DECL(int) VDSetPCHSGeometry(PVDISK pDisk, unsigned nImage,
                                    PCVDGEOMETRY pPCHSGeometry)
{
    int rc = VINF_SUCCESS;
    int rc2;

    LogFlowFunc(("pDisk=%#p nImage=%u pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pDisk, nImage, pPCHSGeometry, pPCHSGeometry->cCylinders,
                 pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);
    AssertMsgReturn(   pPCHSGeometry->cHeads <= 16
                    && pPCHSGeometry->cSectors <= 63,
                    ("PCHS=%u/%u/%u\n", pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors),
                    VERR_INVALID_PARAMETER);
    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VD_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            if (    pPCHSGeometry->cCylinders != pDisk->PCHSGeometry.cCylinders
                ||  pPCHSGeometry->cHeads != pDisk->PCHSGeometry.cHeads
                ||  pPCHSGeometry->cSectors != pDisk->PCHSGeometry.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetPCHSGeometry(pImage->pBackendData,
                                                         pPCHSGeometry);

                /* Cache new geometry values in any case. */
                rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData,
                                                          &pDisk->PCHSGeometry);
                if (RT_FAILURE(rc2))
                {
                    pDisk->PCHSGeometry.cCylinders = 0;
                    pDisk->PCHSGeometry.cHeads = 0;
                    pDisk->PCHSGeometry.cSectors = 0;
                }
                else
                {
                    /* Make sure the CHS geometry is properly clipped. */
                    pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 255);
                    pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
                }
            }
        }
        else
        {
            VDGEOMETRY PCHS;
            rc = pImage->Backend->pfnGetPCHSGeometry(pImage->pBackendData,
                                                     &PCHS);
            if (    RT_FAILURE(rc)
                ||  pPCHSGeometry->cCylinders != PCHS.cCylinders
                ||  pPCHSGeometry->cHeads != PCHS.cHeads
                ||  pPCHSGeometry->cSectors != PCHS.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetPCHSGeometry(pImage->pBackendData,
                                                         pPCHSGeometry);
            }
        }
    } while (0);

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetLCHSGeometry(PVDISK pDisk, unsigned nImage,
                                    PVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pLCHSGeometry=%#p\n",
                 pDisk, nImage, pLCHSGeometry));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
    {
        if (pImage == pDisk->pLast)
        {
            /* Use cached information if possible. */
            if (pDisk->LCHSGeometry.cCylinders != 0)
                *pLCHSGeometry = pDisk->LCHSGeometry;
            else
                rc = VERR_VD_GEOMETRY_NOT_SET;
        }
        else
            rc = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData, pLCHSGeometry);
    }
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc((": %Rrc (LCHS=%u/%u/%u)\n", rc,
                 pDisk->LCHSGeometry.cCylinders, pDisk->LCHSGeometry.cHeads,
                 pDisk->LCHSGeometry.cSectors));
    return rc;
}


VBOXDDU_DECL(int) VDSetLCHSGeometry(PVDISK pDisk, unsigned nImage,
                                    PCVDGEOMETRY pLCHSGeometry)
{
    int rc = VINF_SUCCESS;
    int rc2;

    LogFlowFunc(("pDisk=%#p nImage=%u pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pDisk, nImage, pLCHSGeometry, pLCHSGeometry->cCylinders,
                 pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);
    AssertMsgReturn(   pLCHSGeometry->cHeads <= 255
                    && pLCHSGeometry->cSectors <= 63,
                    ("LCHS=%u/%u/%u\n", pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors),
                    VERR_INVALID_PARAMETER);

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VD_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            if (    pLCHSGeometry->cCylinders != pDisk->LCHSGeometry.cCylinders
                ||  pLCHSGeometry->cHeads != pDisk->LCHSGeometry.cHeads
                ||  pLCHSGeometry->cSectors != pDisk->LCHSGeometry.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetLCHSGeometry(pImage->pBackendData,
                                                         pLCHSGeometry);

                /* Cache new geometry values in any case. */
                rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData,
                                                          &pDisk->LCHSGeometry);
                if (RT_FAILURE(rc2))
                {
                    pDisk->LCHSGeometry.cCylinders = 0;
                    pDisk->LCHSGeometry.cHeads = 0;
                    pDisk->LCHSGeometry.cSectors = 0;
                }
                else
                {
                    /* Make sure the CHS geometry is properly clipped. */
                    pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
                    pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
                }
            }
        }
        else
        {
            VDGEOMETRY LCHS;
            rc = pImage->Backend->pfnGetLCHSGeometry(pImage->pBackendData,
                                                     &LCHS);
            if (    RT_FAILURE(rc)
                ||  pLCHSGeometry->cCylinders != LCHS.cCylinders
                ||  pLCHSGeometry->cHeads != LCHS.cHeads
                ||  pLCHSGeometry->cSectors != LCHS.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetLCHSGeometry(pImage->pBackendData,
                                                         pLCHSGeometry);
            }
        }
    } while (0);

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDQueryRegions(PVDISK pDisk, unsigned nImage, uint32_t fFlags,
                                 PPVDREGIONLIST ppRegionList)
{
    LogFlowFunc(("pDisk=%#p nImage=%u fFlags=%#x ppRegionList=%#p\n",
                 pDisk, nImage, fFlags, ppRegionList));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(ppRegionList, VERR_INVALID_POINTER);

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
    {
        PCVDREGIONLIST pRegionList = NULL;
        rc = pImage->Backend->pfnQueryRegions(pImage->pBackendData, &pRegionList);
        if (RT_SUCCESS(rc))
        {
            rc = vdRegionListConv(pRegionList, fFlags, ppRegionList);

            AssertPtr(pImage->Backend->pfnRegionListRelease);
            pImage->Backend->pfnRegionListRelease(pImage->pBackendData, pRegionList);
        }
    }
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc((": %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(void) VDRegionListFree(PVDREGIONLIST pRegionList)
{
    RTMemFree(pRegionList);
}


VBOXDDU_DECL(int) VDGetVersion(PVDISK pDisk, unsigned nImage,
                               unsigned *puVersion)
{
    LogFlowFunc(("pDisk=%#p nImage=%u puVersion=%#p\n",
                 pDisk, nImage, puVersion));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(puVersion, VERR_INVALID_POINTER);

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        *puVersion = pImage->Backend->pfnGetVersion(pImage->pBackendData);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc uVersion=%#x\n", rc, *puVersion));
    return rc;
}


VBOXDDU_DECL(int) VDBackendInfoSingle(PVDISK pDisk, unsigned nImage,
                                      PVDBACKENDINFO pBackendInfo)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pBackendInfo=%#p\n",
                 pDisk, nImage, pBackendInfo));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pBackendInfo, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
    {
        pBackendInfo->pszBackend = pImage->Backend->pszBackendName;
        pBackendInfo->uBackendCaps = pImage->Backend->uBackendCaps;
        pBackendInfo->paFileExtensions = pImage->Backend->paFileExtensions;
        pBackendInfo->paConfigInfo = pImage->Backend->paConfigInfo;
    }
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetImageFlags(PVDISK pDisk, unsigned nImage,
                                  unsigned *puImageFlags)
{
    LogFlowFunc(("pDisk=%#p nImage=%u puImageFlags=%#p\n",
                 pDisk, nImage, puImageFlags));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(puImageFlags, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        *puImageFlags = pImage->uImageFlags;
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc uImageFlags=%#x\n", rc, *puImageFlags));
    return rc;
}


VBOXDDU_DECL(int) VDGetOpenFlags(PVDISK pDisk, unsigned nImage,
                                 unsigned *puOpenFlags)
{
    LogFlowFunc(("pDisk=%#p nImage=%u puOpenFlags=%#p\n",
                 pDisk, nImage, puOpenFlags));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(puOpenFlags, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        *puOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pBackendData);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc uOpenFlags=%#x\n", rc, *puOpenFlags));
    return rc;
}


VBOXDDU_DECL(int) VDSetOpenFlags(PVDISK pDisk, unsigned nImage,
                                 unsigned uOpenFlags)
{
    LogFlowFunc(("pDisk=%#p uOpenFlags=%#u\n", pDisk, uOpenFlags));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertMsgReturn((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0, ("uOpenFlags=%#x\n", uOpenFlags),
                    VERR_INVALID_PARAMETER);

    /* Do the job. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    /* Destroy any discard state because the image might be changed to readonly mode. */
    int rc = vdDiscardStateDestroy(pDisk);
    if (RT_SUCCESS(rc))
    {
        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtr(pImage);
        if (pImage)
        {
            rc = pImage->Backend->pfnSetOpenFlags(pImage->pBackendData,
                                                  uOpenFlags & ~(VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_IGNORE_FLUSH
                                                                 | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS));
            if (RT_SUCCESS(rc))
                pImage->uOpenFlags = uOpenFlags & (VD_OPEN_FLAGS_HONOR_SAME | VD_OPEN_FLAGS_DISCARD | VD_OPEN_FLAGS_IGNORE_FLUSH
                                                   | VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS);
        }
        else
            rc = VERR_VD_IMAGE_NOT_FOUND;
    }

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetFilename(PVDISK pDisk, unsigned nImage,
                                char *pszFilename, unsigned cbFilename)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pszFilename=%#p cbFilename=%u\n",
                 pDisk, nImage, pszFilename, cbFilename));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(cbFilename > 0, VERR_INVALID_PARAMETER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    int rc;
    if (pImage)
        rc = RTStrCopy(pszFilename, cbFilename, pImage->pszFilename);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc, pszFilename=\"%s\"\n", rc, pszFilename));
    return rc;
}


VBOXDDU_DECL(int) VDGetComment(PVDISK pDisk, unsigned nImage,
                               char *pszComment, unsigned cbComment)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pszComment=%#p cbComment=%u\n",
                 pDisk, nImage, pszComment, cbComment));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pszComment, VERR_INVALID_POINTER);
    AssertReturn(cbComment > 0, VERR_INVALID_PARAMETER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnGetComment(pImage->pBackendData, pszComment, cbComment);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc, pszComment=\"%s\"\n", rc, pszComment));
    return rc;
}


VBOXDDU_DECL(int) VDSetComment(PVDISK pDisk, unsigned nImage,
                               const char *pszComment)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pszComment=%#p \"%s\"\n",
                 pDisk, nImage, pszComment, pszComment));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrNullReturn(pszComment, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnSetComment(pImage->pBackendData, pszComment);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetUuid(PVDISK pDisk, unsigned nImage, PRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnGetUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}


VBOXDDU_DECL(int) VDSetUuid(PVDISK pDisk, unsigned nImage, PCRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    RTUUID Uuid;
    if (pUuid)
        AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    else
    {
        int rc = RTUuidCreate(&Uuid);
        AssertRCReturn(rc, rc);
        pUuid = &Uuid;
    }

    /* Do the job. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnSetUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetModificationUuid(PVDISK pDisk, unsigned nImage, PRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnGetModificationUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}


VBOXDDU_DECL(int) VDSetModificationUuid(PVDISK pDisk, unsigned nImage, PCRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    RTUUID Uuid;
    if (pUuid)
        AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    else
    {
        int rc = RTUuidCreate(&Uuid);
        AssertRCReturn(rc, rc);
        pUuid = &Uuid;
    }

    /* Do the job. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    if (pImage)
        rc = pImage->Backend->pfnSetModificationUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDGetParentUuid(PVDISK pDisk, unsigned nImage,
                                  PRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    /* Do the job. */
    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnGetParentUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}


VBOXDDU_DECL(int) VDSetParentUuid(PVDISK pDisk, unsigned nImage,
                                  PCRTUUID pUuid)
{
    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    RTUUID Uuid;
    if (pUuid)
        AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    else
    {
        int rc = RTUuidCreate(&Uuid);
        AssertRCReturn(rc, rc);
        pUuid = &Uuid;
    }

    /* Do the job. */
    int rc2 = vdThreadStartWrite(pDisk);
    AssertRC(rc2);

    int rc;
    PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
    AssertPtr(pImage);
    if (pImage)
        rc = pImage->Backend->pfnSetParentUuid(pImage->pBackendData, pUuid);
    else
        rc = VERR_VD_IMAGE_NOT_FOUND;

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(void) VDDumpImages(PVDISK pDisk)
{
    /* sanity check */
    AssertPtrReturnVoid(pDisk);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    AssertPtrReturnVoid(pDisk->pInterfaceError);
    if (!RT_VALID_PTR(pDisk->pInterfaceError->pfnMessage))
        pDisk->pInterfaceError->pfnMessage = vdLogMessage;

    int rc2 = vdThreadStartRead(pDisk);
    AssertRC(rc2);

    vdMessageWrapper(pDisk, "--- Dumping VD Disk, Images=%u\n", pDisk->cImages);
    for (PVDIMAGE pImage = pDisk->pBase; pImage; pImage = pImage->pNext)
    {
        vdMessageWrapper(pDisk, "Dumping VD image \"%s\" (Backend=%s)\n",
                         pImage->pszFilename, pImage->Backend->pszBackendName);
        pImage->Backend->pfnDump(pImage->pBackendData);
    }

    rc2 = vdThreadFinishRead(pDisk);
    AssertRC(rc2);
}


VBOXDDU_DECL(int) VDDiscardRanges(PVDISK pDisk, PCRTRANGE paRanges, unsigned cRanges)
{
    int rc;
    int rc2;

    LogFlowFunc(("pDisk=%#p paRanges=%#p cRanges=%u\n",
                 pDisk, paRanges, cRanges));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertReturn(cRanges > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(paRanges, VERR_INVALID_POINTER);

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        AssertPtrBreakStmt(pDisk->pLast, rc = VERR_VD_NOT_OPENED);

        AssertMsgBreakStmt(pDisk->pLast->uOpenFlags & VD_OPEN_FLAGS_DISCARD,
                           ("Discarding not supported\n"),
                           rc = VERR_NOT_SUPPORTED);

        VDIOCTX IoCtx;
        RTSEMEVENT hEventComplete = NIL_RTSEMEVENT;

        rc = RTSemEventCreate(&hEventComplete);
        if (RT_FAILURE(rc))
            break;

        vdIoCtxDiscardInit(&IoCtx, pDisk, paRanges, cRanges,
                           vdIoCtxSyncComplete, pDisk, hEventComplete, NULL,
                           vdDiscardHelperAsync, VDIOCTX_FLAGS_SYNC | VDIOCTX_FLAGS_DONT_FREE);
        rc = vdIoCtxProcessSync(&IoCtx, hEventComplete);

        RTSemEventDestroy(hEventComplete);
    } while (0);

    rc2 = vdThreadFinishWrite(pDisk);
    AssertRC(rc2);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDAsyncRead(PVDISK pDisk, uint64_t uOffset, size_t cbRead,
                              PCRTSGBUF pSgBuf,
                              PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                              void *pvUser1, void *pvUser2)
{
    int rc = VERR_VD_BLOCK_FREE;
    int rc2;
    PVDIOCTX pIoCtx = NULL;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pSgBuf=%#p cbRead=%zu pvUser1=%#p pvUser2=%#p\n",
                 pDisk, uOffset, pSgBuf, cbRead, pvUser1, pvUser2));

    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pSgBuf, VERR_INVALID_POINTER);

    do
    {
        rc2 = vdThreadStartRead(pDisk);
        AssertRC(rc2);

        AssertMsgBreakStmt(   uOffset < pDisk->cbSize
                           && cbRead <= pDisk->cbSize - uOffset,
                           ("uOffset=%llu cbRead=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbRead, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);
        AssertPtrBreakStmt(pDisk->pLast, rc = VERR_VD_NOT_OPENED);

        pIoCtx = vdIoCtxRootAlloc(pDisk, VDIOCTXTXDIR_READ, uOffset,
                                  cbRead, pDisk->pLast, pSgBuf,
                                  pfnComplete, pvUser1, pvUser2,
                                  NULL, vdReadHelperAsync,
                                  VDIOCTX_FLAGS_ZERO_FREE_BLOCKS);
        if (!pIoCtx)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdIoCtxProcessTryLockDefer(pIoCtx);
        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            if (ASMAtomicCmpXchgBool(&pIoCtx->fComplete, true, false))
                vdIoCtxFree(pDisk, pIoCtx);
            else
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS; /* Let the other handler complete the request. */
        }
        else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS) /* Another error */
            vdIoCtxFree(pDisk, pIoCtx);

    } while (0);

    if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        rc2 = vdThreadFinishRead(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDAsyncWrite(PVDISK pDisk, uint64_t uOffset, size_t cbWrite,
                               PCRTSGBUF pSgBuf,
                               PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                               void *pvUser1, void *pvUser2)
{
    int rc;
    int rc2;
    PVDIOCTX pIoCtx = NULL;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pSgBuf=%#p cbWrite=%zu pvUser1=%#p pvUser2=%#p\n",
                 pDisk, uOffset, pSgBuf, cbWrite, pvUser1, pvUser2));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    /* Check arguments. */
    AssertReturn(cbWrite > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pSgBuf, VERR_INVALID_POINTER);

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        AssertMsgBreakStmt(   uOffset < pDisk->cbSize
                           && cbWrite <= pDisk->cbSize - uOffset,
                           ("uOffset=%llu cbWrite=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbWrite, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);
        AssertPtrBreakStmt(pDisk->pLast, rc = VERR_VD_NOT_OPENED);

        pIoCtx = vdIoCtxRootAlloc(pDisk, VDIOCTXTXDIR_WRITE, uOffset,
                                  cbWrite, pDisk->pLast, pSgBuf,
                                  pfnComplete, pvUser1, pvUser2,
                                  NULL, vdWriteHelperAsync,
                                  VDIOCTX_FLAGS_DEFAULT);
        if (!pIoCtx)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdIoCtxProcessTryLockDefer(pIoCtx);
        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            if (ASMAtomicCmpXchgBool(&pIoCtx->fComplete, true, false))
                vdIoCtxFree(pDisk, pIoCtx);
            else
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS; /* Let the other handler complete the request. */
        }
        else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS) /* Another error */
            vdIoCtxFree(pDisk, pIoCtx);
    } while (0);

    if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXDDU_DECL(int) VDAsyncFlush(PVDISK pDisk, PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                               void *pvUser1, void *pvUser2)
{
    int rc;
    int rc2;
    PVDIOCTX pIoCtx = NULL;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        AssertPtrBreakStmt(pDisk->pLast, rc = VERR_VD_NOT_OPENED);

        pIoCtx = vdIoCtxRootAlloc(pDisk, VDIOCTXTXDIR_FLUSH, 0,
                                  0, pDisk->pLast, NULL,
                                  pfnComplete, pvUser1, pvUser2,
                                  NULL, vdFlushHelperAsync,
                                  VDIOCTX_FLAGS_DEFAULT);
        if (!pIoCtx)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdIoCtxProcessTryLockDefer(pIoCtx);
        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            if (ASMAtomicCmpXchgBool(&pIoCtx->fComplete, true, false))
                vdIoCtxFree(pDisk, pIoCtx);
            else
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS; /* Let the other handler complete the request. */
        }
        else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS) /* Another error */
            vdIoCtxFree(pDisk, pIoCtx);
    } while (0);

    if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

VBOXDDU_DECL(int) VDAsyncDiscardRanges(PVDISK pDisk, PCRTRANGE paRanges, unsigned cRanges,
                                       PFNVDASYNCTRANSFERCOMPLETE pfnComplete,
                                       void *pvUser1, void *pvUser2)
{
    int rc;
    int rc2;
    PVDIOCTX pIoCtx = NULL;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    /* sanity check */
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertMsg(pDisk->u32Signature == VDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

    do
    {
        rc2 = vdThreadStartWrite(pDisk);
        AssertRC(rc2);

        AssertPtrBreakStmt(pDisk->pLast, rc = VERR_VD_NOT_OPENED);

        pIoCtx = vdIoCtxDiscardAlloc(pDisk, paRanges, cRanges,
                                     pfnComplete, pvUser1, pvUser2, NULL,
                                     vdDiscardHelperAsync,
                                     VDIOCTX_FLAGS_DEFAULT);
        if (!pIoCtx)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdIoCtxProcessTryLockDefer(pIoCtx);
        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            if (ASMAtomicCmpXchgBool(&pIoCtx->fComplete, true, false))
                vdIoCtxFree(pDisk, pIoCtx);
            else
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS; /* Let the other handler complete the request. */
        }
        else if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS) /* Another error */
            vdIoCtxFree(pDisk, pIoCtx);
    } while (0);

    if (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        rc2 = vdThreadFinishWrite(pDisk);
        AssertRC(rc2);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

VBOXDDU_DECL(int) VDRepair(PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                           const char *pszFilename, const char *pszBackend,
                           uint32_t fFlags)
{
    int rc = VERR_NOT_SUPPORTED;
    PCVDIMAGEBACKEND pBackend = NULL;
    VDINTERFACEIOINT VDIfIoInt;
    VDINTERFACEIO    VDIfIoFallback;
    PVDINTERFACEIO   pInterfaceIo;

    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));
    /* Check arguments. */
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszBackend, VERR_INVALID_POINTER);
    AssertMsgReturn((fFlags & ~VD_REPAIR_FLAGS_MASK) == 0, ("fFlags=%#x\n", fFlags),
                    VERR_INVALID_PARAMETER);

    pInterfaceIo = VDIfIoGet(pVDIfsImage);
    if (!pInterfaceIo)
    {
        /*
         * Caller doesn't provide an I/O interface, create our own using the
         * native file API.
         */
        vdIfIoFallbackCallbacksSetup(&VDIfIoFallback);
        pInterfaceIo = &VDIfIoFallback;
    }

    /* Set up the internal I/O interface. */
    AssertReturn(!VDIfIoIntGet(pVDIfsImage), VERR_INVALID_PARAMETER);
    VDIfIoInt.pfnOpen                   = vdIOIntOpenLimited;
    VDIfIoInt.pfnClose                  = vdIOIntCloseLimited;
    VDIfIoInt.pfnDelete                 = vdIOIntDeleteLimited;
    VDIfIoInt.pfnMove                   = vdIOIntMoveLimited;
    VDIfIoInt.pfnGetFreeSpace           = vdIOIntGetFreeSpaceLimited;
    VDIfIoInt.pfnGetModificationTime    = vdIOIntGetModificationTimeLimited;
    VDIfIoInt.pfnGetSize                = vdIOIntGetSizeLimited;
    VDIfIoInt.pfnSetSize                = vdIOIntSetSizeLimited;
    VDIfIoInt.pfnReadUser               = vdIOIntReadUserLimited;
    VDIfIoInt.pfnWriteUser              = vdIOIntWriteUserLimited;
    VDIfIoInt.pfnReadMeta               = vdIOIntReadMetaLimited;
    VDIfIoInt.pfnWriteMeta              = vdIOIntWriteMetaLimited;
    VDIfIoInt.pfnFlush                  = vdIOIntFlushLimited;
    rc = VDInterfaceAdd(&VDIfIoInt.Core, "VD_IOINT", VDINTERFACETYPE_IOINT,
                        pInterfaceIo, sizeof(VDINTERFACEIOINT), &pVDIfsImage);
    AssertRC(rc);

    rc = vdFindImageBackend(pszBackend, &pBackend);
    if (RT_SUCCESS(rc))
    {
        if (pBackend->pfnRepair)
            rc = pBackend->pfnRepair(pszFilename, pVDIfsDisk, pVDIfsImage, fFlags);
        else
            rc = VERR_VD_IMAGE_REPAIR_NOT_SUPPORTED;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/*
 * generic plugin functions
 */

/**
 * @interface_method_impl{VDIMAGEBACKEND,pfnComposeLocation}
 */
DECLCALLBACK(int) genericFileComposeLocation(PVDINTERFACE pConfig, char **pszLocation)
{
    RT_NOREF1(pConfig);
    *pszLocation = NULL;
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VDIMAGEBACKEND,pfnComposeName}
 */
DECLCALLBACK(int) genericFileComposeName(PVDINTERFACE pConfig, char **pszName)
{
    RT_NOREF1(pConfig);
    *pszName = NULL;
    return VINF_SUCCESS;
}
